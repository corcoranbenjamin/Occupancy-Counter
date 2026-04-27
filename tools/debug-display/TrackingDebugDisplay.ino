// ============================================================================
// Tracking Debug Display — VL53L5CX standalone visualization tool
// ============================================================================
//
// A companion to the production Occupancy Counter firmware. Hosts its own
// WiFi access point and serves a live web page that visualizes the tracking
// algorithm in real time: 8x8 distance grid, occupied cells, detected blobs,
// persistent tracks, and entry/exit decisions. No cloud dependencies and no
// secrets.h — useful for sensor placement, calibration validation, and
// tuning the tracker before deploying production firmware.
//
// Hardware:  Pololu VL53L5CX carrier → ESP32-C6
//   VIN→3V3  GND→GND  SDA→GPIO6  SCL→GPIO7  LPn→3V3  INT→GPIO5
//   Reset button: GPIO3 → GND
//
// Pin assignments live in config.h. They match the production firmware in
// the parent repository, so the same physical hardware works for both.
//
// Board:   "ESP32C6 Dev Module"  (esp32 >= 3.x)
// Library: "SparkFun VL53L5CX Arduino Library"
// WiFi:    join "OccupancyCounter" (pw: 12345678), open http://192.168.4.1
// ============================================================================

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SparkFun_VL53L5CX_Library.h>

#include "config.h"
#include "tracking.h"
#include "web_page.h"

SparkFun_VL53L5CX       sensor;
VL53L5CX_ResultsData    sensorData;
OccupancyTracker        tracker;
WebServer               server(WEB_PORT);

volatile bool sensorDataReady = false;
unsigned long resetBtnDown    = 0;
bool          resetBtnActive  = false;

void IRAM_ATTR onSensorInt() { sensorDataReady = true; }

void initSensor();
void connectWiFi();
void setupRoutes();
void handleResetButton();
void sendJson();
void printBaseline();

// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Occupancy Counter VL53L5CX ===");

    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(I2C_CLOCK_HZ);

    initSensor();
    connectWiFi();

    setupRoutes();
    server.begin();
    Serial.println("[WEB]  Server on port 80");

    tracker.init();
    Serial.printf("[RUN]  Active — ceiling %d mm, shift %.1f rows, max %d tracks\n",
                  tracker.detect_ceiling, (float)MIN_CENTROID_SHIFT, MAX_TRACKS);
}

void loop() {
    server.handleClient();
    handleResetButton();

    if (!sensorDataReady) { delay(5); return; }
    sensorDataReady = false;

    if (!sensor.getRangingData(&sensorData)) return;

    // Extract the two arrays the tracker needs from the library's result struct
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
        }
    }
}

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

void connectWiFi() {
#ifdef AP_MODE
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("[WIFI] AP — SSID: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
#endif
#ifdef STA_MODE
    WiFi.mode(WIFI_STA);
    WiFi.begin(STA_SSID, STA_PASSWORD);
    Serial.printf("[WIFI] Connecting to %s ", STA_SSID);
    int tries = 40;
    while (WiFi.status() != WL_CONNECTED && tries-- > 0) { delay(500); Serial.print('.'); }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED)
        Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
    else {
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        Serial.printf("[WIFI] Fallback AP — %s  %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
    }
#endif
}

// ============================================================================

void setupRoutes() {
    server.on("/", []() { server.send_P(200, "text/html", INDEX_HTML); });
    server.on("/data", HTTP_GET, []() { sendJson(); });
    server.on("/reset", HTTP_GET, []() {
        tracker.resetCount();
        Serial.println("[WEB]  Reset");
        server.send(200, "text/plain", "ok");
    });
    server.on("/flip", HTTP_GET, []() {
        tracker.entry_dir = -tracker.entry_dir;
        Serial.printf("[WEB]  Dir → %d\n", tracker.entry_dir);
        server.send(200, "text/plain", "ok");
    });
    server.on("/config", HTTP_GET, []() {
        if (server.hasArg("ceiling")) {
            int v = server.arg("ceiling").toInt();
            if (v >= 200 && v <= 2000) {
                tracker.detect_ceiling = v;
                tracker.resetTracking();
                Serial.printf("[WEB]  Ceiling → %d mm\n", v);
            }
        }
        server.send(200, "text/plain", "ok");
    });
}

// ============================================================================

void sendJson() {
    String j;
    j.reserve(1600);
    j += F("{\"occupancy\":");  j += tracker.occupancy;
    j += F(",\"entries\":");    j += tracker.total_entries;
    j += F(",\"exits\":");      j += tracker.total_exits;
    j += F(",\"ceiling\":");    j += tracker.detect_ceiling;
    j += F(",\"frames\":");     j += tracker.frames_processed;
    j += F(",\"occ_n\":");        j += tracker.occ_count;
    j += F(",\"tracking\":");     j += tracker.tracking_active ? F("true") : F("false");
    j += F(",\"num_tracks\":");   j += tracker.num_tracks;
    j += F(",\"enter_row\":");    j += String(tracker.enter_centroid, 1);
    j += F(",\"cur_row\":");      j += String(tracker.current_centroid, 1);

    j += F(",\"cal\":");
    j += tracker.calibrated ? F("true") : F("false");

    j += F(",\"distances\":[");
    for (int i = 0; i < 64; i++) {
        // After calibration, only send distance for occupied zones;
        // everything at its baseline (door frame, walls) shows as -1.
        if (tracker.occupied[i])
            j += String(tracker.current_distances[i]);
        else
            j += F("-1");
        if (i < 63) j += ',';
    }
    j += ']';

    j += F(",\"occupied\":[");
    for (int i = 0; i < 64; i++) {
        j += tracker.occupied[i] ? '1' : '0';
        if (i < 63) j += ',';
    }
    j += F("]}");

    server.send(200, "application/json", j);
}

// ============================================================================

void handleResetButton() {
    bool pressed = (digitalRead(RESET_BUTTON_PIN) == LOW);
    if (pressed && !resetBtnActive) { resetBtnDown = millis(); resetBtnActive = true; }
    if (!pressed) { resetBtnActive = false; return; }
    if (resetBtnActive && (millis() - resetBtnDown >= RESET_HOLD_MS)) {
        tracker.resetCount();
        resetBtnActive = false;
        Serial.println("[BTN]  Reset");
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
