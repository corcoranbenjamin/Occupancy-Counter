// ============================================================================
// Occupancy Counter — VL53L5CX + ESP32-C6 + eduroam + Google Sheets
// ============================================================================
//
// Multi-blob tracking on an 8×8 ToF grid.  Occupancy snapshots are appended
// to a Google Sheet via the Sheets API (service-account auth over
// WPA2-Enterprise / eduroam).  Uploads only occur when the doorway is clear
// to avoid stalling the tracking pipeline.
//
// Hardware:  Pololu VL53L5CX carrier → ESP32-C6
//   VIN→3V3  GND→GND  SDA→GPIO6  SCL→GPIO7  LPn→3V3  INT→GPIO5
//   SD card: CS→GPIO1  SCK→GPIO18  MOSI→GPIO19  MISO→GPIO20
//   Reset button: GPIO3 → GND
//
// Board:    "ESP32C6 Dev Module"  (esp32 ≥ 3.x)
// Libraries:
//   SparkFun VL53L5CX Arduino Library
//   ESP-Google-Sheet-Client  (mobizt)
//   ESP32WiFiEnterprise      (racoonx65)
//
// Credentials: copy secrets_template.h → secrets.h, fill in your values.
// ============================================================================

#include <Wire.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include "time.h"
#include <ESP_Google_Sheet_Client.h>
#include <SparkFun_VL53L5CX_Library.h>
#include <SPI.h>
#include <SD.h>

#include "config.h"
#include "secrets.h"

#ifndef USE_HOME_WIFI
#include <WiFiEnterprise.h>
#endif

#include "tracking.h"

// ── hardware objects ────────────────────────────────────────────────────────
SparkFun_VL53L5CX       sensor;
VL53L5CX_ResultsData    sensorData;
OccupancyTracker        tracker;

// ── sensor interrupt ────────────────────────────────────────────────────────
volatile bool sensorDataReady = false;
void IRAM_ATTR onSensorInt() { sensorDataReady = true; }

// ── wifi / sheets state ─────────────────────────────────────────────────────
unsigned long lastWifiCheck   = 0;
unsigned long lastUpload      = 0;
unsigned long lastSDLog       = 0;
int           reconnectFails  = 0;
bool          wifiWasUp       = false;

// ── SD card state ───────────────────────────────────────────────────────────
bool sdReady = false;

// ── forward declarations ────────────────────────────────────────────────────
void initSensor();
void initSD();
bool connectWifi();
void checkWifi();
void appendToSheet();
void logToSD();
void handleResetButton();
void printBaseline();
void checkSleepSchedule();
void enterDeepSleep(int sleepSeconds);
void tokenStatusCallback(TokenInfo info);

unsigned long lastSleepCheck = 0;

unsigned long getEpoch() {
    time_t now;
    struct tm t;
    if (!getLocalTime(&t)) return 0;
    time(&now);
    return now;
}

// ============================================================================
// setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Occupancy Counter VL53L5CX + Sheets ===");

    // ── sensor bus + init ───────────────────────────────────────────────────
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(I2C_CLOCK_HZ);
    initSensor();

    // ── SD card ──────────────────────────────────────────────────────────────
#if SD_ENABLED
    initSD();
#endif

    // ── wifi ─────────────────────────────────────────────────────────────────
    connectWifi();

    // ── NTP ─────────────────────────────────────────────────────────────────
    configTzTime(TZ_INFO, NTP_SERVER);
    Serial.println("[TIME] NTP configured");

    // ── Google Sheets auth ──────────────────────────────────────────────────
    GSheet.setTokenCallback(tokenStatusCallback);
    GSheet.setPrerefreshSeconds(10 * 60);
    GSheet.begin(GCP_CLIENT_EMAIL, GCP_PROJECT_ID, GCP_PRIVATE_KEY);

    // ── tracker ─────────────────────────────────────────────────────────────
    tracker.init();

    // Try to restore baseline from NVS (saved before last deep sleep).
    // If valid, skip calibration — mounting hasn't changed overnight.
    if (tracker.loadBaseline()) {
        Serial.println("[CAL]  Baseline restored from flash — skipping calibration");
        printBaseline();
    } else {
        Serial.println("[CAL]  No saved baseline — will calibrate (keep doorway clear)");
    }

    Serial.printf("[RUN]  Active — ceiling %d mm, shift %.1f rows, max %d tracks\n",
                  tracker.detect_ceiling, (float)MIN_CENTROID_SHIFT, MAX_TRACKS);

#if SLEEP_ENABLED
    // If we booted outside the active window, sleep immediately.
    // Wait briefly for NTP to sync so we have a valid clock.
    delay(2000);
    setenv("TZ", TZ_INFO, 1);
    tzset();
    struct tm bootTime;
    if (getLocalTime(&bootTime)) {
        int bootMin = bootTime.tm_hour * 60 + bootTime.tm_min;
        int sleepMin = SLEEP_HOUR * 60 + SLEEP_MINUTE;
        int wakeMin  = WAKE_HOUR * 60 + WAKE_MINUTE;

        bool inActiveWindow;
        if (wakeMin < sleepMin)
            inActiveWindow = (bootMin >= wakeMin && bootMin < sleepMin);
        else
            inActiveWindow = (bootMin >= wakeMin || bootMin < sleepMin);

        if (!inActiveWindow) {
            Serial.printf("[SLEEP] Booted at %02d:%02d — outside active window %02d:%02d–%02d:%02d\n",
                          bootTime.tm_hour, bootTime.tm_min,
                          WAKE_HOUR, WAKE_MINUTE, SLEEP_HOUR, SLEEP_MINUTE);
            int secsUntilWake;
            int nowSec = bootTime.tm_hour * 3600 + bootTime.tm_min * 60 + bootTime.tm_sec;
            int wakeSec = WAKE_HOUR * 3600 + WAKE_MINUTE * 60;
            secsUntilWake = (wakeSec - nowSec + 86400) % 86400;
            if (secsUntilWake < 60) secsUntilWake += 86400;
            enterDeepSleep(secsUntilWake);
        }
    } else {
        Serial.println("[SLEEP] NTP not ready — running without schedule");
    }
#endif
}

// ============================================================================
// loop
// ============================================================================
void loop() {
    unsigned long now = millis();

    // ── sleep schedule ──────────────────────────────────────────────────────
#if SLEEP_ENABLED
    checkSleepSchedule();
#endif

    // ── keep wifi alive ─────────────────────────────────────────────────────
    checkWifi();

    // ── reset button ────────────────────────────────────────────────────────
    handleResetButton();

    // ── sensor frame ────────────────────────────────────────────────────────
    if (!sensorDataReady) { delay(5); return; }
    sensorDataReady = false;

    if (!sensor.getRangingData(&sensorData)) return;

    int16_t dist[64];
    uint8_t stat[64];
    for (int i = 0; i < 64; i++) {
        dist[i] = sensorData.distance_mm[i];
        stat[i] = sensorData.target_status[i];
    }

    tracker.processFrame(dist, stat);

    if (!tracker.calibrated) {
        if (tracker.addCalibrationFrame(dist, stat)) {
            Serial.println("[CAL]  Calibration complete");
            printBaseline();
            if (tracker.saveBaseline())
                Serial.println("[CAL]  Baseline saved to flash");
        }
        return;
    }

    // ── periodic logging (only when doorway is clear to avoid stalling tracking) ──
    if (!tracker.tracking_active && now - lastUpload >= SHEETS_UPLOAD_INTERVAL_MS) {
        lastUpload = now;
        if (GSheet.ready()) appendToSheet();
    }
#if SD_ENABLED
    if (!tracker.tracking_active && now - lastSDLog >= SD_LOG_INTERVAL_MS) {
        lastSDLog = now;
        logToSD();
    }
#endif
}

// ============================================================================
// sensor init
// ============================================================================
void initSensor() {
    Serial.println("[INIT] Connecting to VL53L5CX …");
    int retries = 5;
    while (retries--) {
        if (sensor.begin(SENSOR_I2C_ADDR, Wire)) { Serial.println("[INIT] Sensor found"); break; }
        Serial.println("[INIT] Retrying …");
        delay(2000);
    }
    if (retries < 0) {
        Serial.println("[INIT] ERROR — sensor not detected");
        while (true) delay(1000);
    }
    sensor.setResolution(64);
    sensor.setRangingFrequency(SENSOR_FREQ_HZ);
    sensor.startRanging();

    pinMode(INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(INT_PIN), onSensorInt, FALLING);

    Serial.printf("[INIT] 8x8 @ %d Hz, INT on GPIO%d\n", SENSOR_FREQ_HZ, INT_PIN);
}

// ============================================================================
// SD card
// ============================================================================
void initSD() {
    SPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[SD]   Card mount failed — continuing without SD");
        sdReady = false;
        return;
    }
    sdReady = true;
    Serial.printf("[SD]   Card mounted — %lluMB\n",
                  SD.cardSize() / (1024 * 1024));
}

void logToSD() {
    if (!sdReady) return;

    setenv("TZ", TZ_INFO, 1);
    tzset();
    struct tm t;
    if (!getLocalTime(&t)) return;

    char filename[20];
    snprintf(filename, sizeof(filename), "/%04d-%02d-%02d.csv",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

    bool newFile = !SD.exists(filename);
    File f = SD.open(filename, FILE_APPEND);
    if (!f) {
        Serial.println("[SD]   Write failed");
        return;
    }

    if (newFile)
        f.println("EpochTimestamp,Occupancy,Entry,Exit,Time");

    unsigned long epoch = getEpoch();
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%m/%d/%Y %H:%M:%S", &t);

    f.printf("%lu,%d,%d,%d,%s\n",
             epoch, tracker.occupancy, tracker.total_entries,
             tracker.total_exits, timeStr);
    f.close();
    Serial.println("[SD]   Row logged");
}

// ============================================================================
// WiFi — home (WPA2-PSK) or eduroam (WPA2-Enterprise)
// ============================================================================
bool connectWifi() {
#ifdef USE_HOME_WIFI
    Serial.printf("[WIFI] Connecting to %s …\n", HOME_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(HOME_SSID, HOME_PASSWORD);
    int tries = 40;
    while (WiFi.status() != WL_CONNECTED && tries-- > 0) { delay(500); Serial.print('.'); }
    Serial.println();
    bool ok = (WiFi.status() == WL_CONNECTED);
#else
    Serial.printf("[WIFI] Connecting to %s as %s …\n", EDUROAM_SSID, EDUROAM_USERNAME);
    bool ok = WiFiEnterprise.begin(EDUROAM_SSID, EDUROAM_USERNAME, EDUROAM_PASSWORD, true);
#endif
    if (ok) {
        Serial.printf("[WIFI] Connected — IP %s  RSSI %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        wifiWasUp = true;
        reconnectFails = 0;
    } else {
        Serial.println("[WIFI] Connection failed");
    }
    return ok;
}

void checkWifi() {
    unsigned long now = millis();
    if (now - lastWifiCheck < WIFI_CHECK_INTERVAL_MS) return;
    lastWifiCheck = now;

    bool connected;
#ifdef USE_HOME_WIFI
    connected = (WiFi.status() == WL_CONNECTED);
#else
    connected = WiFiEnterprise.isConnected();
#endif

    if (connected) {
        if (!wifiWasUp) {
            Serial.printf("[WIFI] Reconnected — IP %s\n",
                          WiFi.localIP().toString().c_str());
            reconnectFails = 0;
        }
        wifiWasUp = true;
        return;
    }

    wifiWasUp = false;
    if (reconnectFails >= MAX_RECONNECT_ATTEMPTS) return;

    Serial.printf("[WIFI] Reconnect attempt %d/%d …\n",
                  reconnectFails + 1, MAX_RECONNECT_ATTEMPTS);
    if (!connectWifi()) reconnectFails++;
}

// ============================================================================
// Google Sheets append:
//   Column A = EpochTimestamp
//   Column B = Count (occupancy)
//   Column C = Entry (total_entries)
//   Column D = Exit  (total_exits)
//   Column E = Time (EST)  — human-readable
// ============================================================================
void appendToSheet() {
    unsigned long epoch = getEpoch();
    if (epoch == 0) {
        Serial.println("[SHEET] Skipped — no valid time yet");
        return;
    }

    setenv("TZ", TZ_INFO, 1);
    tzset();
    struct tm t;
    char timeStr[20];
    if (getLocalTime(&t))
        strftime(timeStr, sizeof(timeStr), "%m/%d/%Y %H:%M:%S", &t);
    else
        snprintf(timeStr, sizeof(timeStr), "unknown");

    FirebaseJson valueRange;
    valueRange.add("majorDimension", "COLUMNS");
    valueRange.set("values/[0]/[0]", epoch);                   // A
    valueRange.set("values/[1]/[0]", tracker.occupancy);       // B
    valueRange.set("values/[2]/[0]", tracker.total_entries);   // C
    valueRange.set("values/[3]/[0]", tracker.total_exits);     // D
    valueRange.set("values/[4]/[0]", timeStr);                 // E

    FirebaseJson response;
    bool ok = GSheet.values.append(&response,
                                   SPREADSHEET_ID,
                                   "Data!A2",
                                   &valueRange);
    if (ok) {
        Serial.printf("[SHEET] OK — occ:%d  in:%d  out:%d  heap:%lu\n",
                      tracker.occupancy, tracker.total_entries,
                      tracker.total_exits, (unsigned long)ESP.getFreeHeap());
    } else {
        Serial.printf("[SHEET] FAIL — %s\n", GSheet.errorReason().c_str());
    }
}

// ============================================================================
// sleep schedule
// ============================================================================
void checkSleepSchedule() {
    unsigned long now = millis();
    if (now - lastSleepCheck < SLEEP_CHECK_INTERVAL_MS) return;
    lastSleepCheck = now;

    setenv("TZ", TZ_INFO, 1);
    tzset();
    struct tm t;
    if (!getLocalTime(&t)) return;

    int nowMin   = t.tm_hour * 60 + t.tm_min;
    int sleepMin = SLEEP_HOUR * 60 + SLEEP_MINUTE;

    if (nowMin == sleepMin || (nowMin > sleepMin && nowMin <= sleepMin + 1)) {
        Serial.printf("[SLEEP] %02d:%02d — time to sleep\n", t.tm_hour, t.tm_min);
        Serial.printf("[SLEEP] Final: occ=%d  in=%d  out=%d\n",
                      tracker.occupancy, tracker.total_entries, tracker.total_exits);

        tracker.resetCount();
        appendToSheet();

        if (tracker.calibrated && tracker.saveBaseline())
            Serial.println("[SLEEP] Baseline saved to flash");

        sensor.stopRanging();

#if SD_ENABLED
        if (sdReady) { SD.end(); Serial.println("[SD]   Unmounted"); }
#endif

        int nowSec  = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
        int wakeSec = WAKE_HOUR * 3600 + WAKE_MINUTE * 60;
        int sleepSecs = (wakeSec - nowSec + 86400) % 86400;
        if (sleepSecs < 60) sleepSecs += 86400;

        enterDeepSleep(sleepSecs);
    }
}

void enterDeepSleep(int sleepSeconds) {
    // ESP32-C3 can be unreliable with very long deep sleep timers.
    // Cap each sleep to 1 hour; setup() re-checks the clock on wake
    // and sleeps again if still outside the active window.
    const int MAX_SLEEP_SEC = 3600;
    int actual = (sleepSeconds > MAX_SLEEP_SEC) ? MAX_SLEEP_SEC : sleepSeconds;

    Serial.printf("[SLEEP] Deep sleep for %dm (total remaining %dh %dm)\n",
                  actual / 60, sleepSeconds / 3600, (sleepSeconds % 3600) / 60);
    Serial.flush();

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    esp_sleep_enable_timer_wakeup((uint64_t)actual * 1000000ULL);
    esp_deep_sleep_start();
}

// ============================================================================
// misc
// ============================================================================
unsigned long resetBtnDown   = 0;
bool          resetBtnActive = false;

void handleResetButton() {
    bool pressed = (digitalRead(RESET_BUTTON_PIN) == LOW);
    if (pressed && !resetBtnActive) { resetBtnDown = millis(); resetBtnActive = true; }
    if (!pressed) { resetBtnActive = false; return; }
    if (resetBtnActive && (millis() - resetBtnDown >= RESET_HOLD_MS)) {
        tracker.resetCount();
        tracker.clearSavedBaseline();
        tracker.resetCalibration();
        resetBtnActive = false;
        Serial.println("[BTN]  Reset — clear the doorway!");
        for (int i = 10; i > 0; i--) {
            Serial.printf("[BTN]  Calibrating in %d …\n", i);
            delay(1000);
        }
        Serial.println("[BTN]  Calibrating now");
    }
}

void printBaseline() {
    Serial.println("[CAL]  Baseline (mm):");
    for (int r = 0; r < 8; r++) {
        Serial.print("  ");
        for (int c = 0; c < 8; c++) Serial.printf("%5d", tracker.baseline[r * 8 + c]);
        Serial.println();
    }
}

void tokenStatusCallback(TokenInfo info) {
    if (info.status == token_status_error)
        GSheet.printf("[AUTH] ERROR — %s\n", GSheet.getTokenError(info).c_str());
    else
        GSheet.printf("[AUTH] %s — %s\n",
                      GSheet.getTokenType(info).c_str(),
                      GSheet.getTokenStatus(info).c_str());
}
