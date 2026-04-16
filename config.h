#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// Pin Definitions (ESP32 + Pololu VL53L5CX carrier)
// ============================================================================
#define SDA_PIN             6
#define SCL_PIN             7
#define INT_PIN             5
#define RESET_BUTTON_PIN    3

// ============================================================================
// SD Card (SPI) — micro SD breakout module
//   Change these if your wiring differs.
// ============================================================================
#define SD_CS_PIN           1
#define SD_CLK_PIN          18
#define SD_MOSI_PIN         19
#define SD_MISO_PIN         20
#define SD_ENABLED          1     // set to 0 to disable SD logging entirely

// ============================================================================
// Google Sheets logging
// ============================================================================
#define SHEETS_UPLOAD_INTERVAL_MS  30000   // append a row every 30 s
#define SD_LOG_INTERVAL_MS         300000  // SD backup every 5 min
#define WIFI_CHECK_INTERVAL_MS     5000    // check eduroam link every 5 s
#define MAX_RECONNECT_ATTEMPTS     5       // then stop retrying until next reboot

// ============================================================================
// NTP / timezone
// ============================================================================
#define NTP_SERVER    "pool.ntp.org"
#define TZ_INFO       "EST5EDT,M3.2.0/02:00:00,M11.1.0/02:00:00"

// ============================================================================
// Sensor
// ============================================================================
#define SENSOR_I2C_ADDR       0x29
#define SENSOR_FREQ_HZ        15
#define I2C_CLOCK_HZ          400000

// ============================================================================
// Detection
// ============================================================================
#define DETECT_FLOOR_MM       100       // ignore closer than this (doors, fixtures, unreliable close zone)
#define DETECT_CEILING_MM     1000

// ============================================================================
// Blob detection & multi-target tracking
//
// Each frame: find connected blobs of occupied cells via BFS.
// Associate blobs to persistent tracks by nearest centroid.
// When a track disappears, evaluate its enter→exit row shift to
// determine crossing direction.  Each person gets their own track,
// so back-to-back crossings count correctly.
// ============================================================================
#define MAX_BLOBS             4
#define MAX_TRACKS            4
#define MIN_BLOB_SIZE         2     // min cells to form a valid blob
#define MAX_ASSOC_DIST        3.0f  // max centroid distance (grid cells) for track↔blob match
#define MIN_CROSSING_FRAMES   3     // min frames for a real crossing (~200 ms at 15 Hz)
#define MAX_CROSSING_FRAMES   90    // timeout: person lingering (~6 s at 15 Hz)
#define MIN_CENTROID_SHIFT    1.5f  // min row shift (grid rows) to count as a crossing
#define MISS_GRACE_FRAMES     5     // frames to tolerate track dropout (~333 ms at 15 Hz)
#define ENTRY_DIR             -1    // 1 = row-increasing = entry, -1 = row-decreasing = entry

// ============================================================================
// Min-distance filter (UM2600 §6.2)
// ============================================================================
#define MIN_FILTER_DEPTH      3

// ============================================================================
// Static-object rejection
// ============================================================================
#define STATIC_MARGIN_MM      100

// ============================================================================
// Calibration (runs automatically during first ~2 s — keep doorway empty!)
// ============================================================================
#define CALIB_FRAMES          30
#define CALIB_MAX_DIST_MM     4000

// ============================================================================
// Sleep schedule — device deep-sleeps outside these hours.
// On wake it reboots fresh (counts reset, baseline restored from flash).
// Set SLEEP_ENABLED to 0 to run 24/7.
// ============================================================================
#define SLEEP_ENABLED         1
#define SLEEP_HOUR            21    // go to sleep at this hour (24h format)
#define SLEEP_MINUTE          30    // ... and this minute  → 21:30 = 9:30 PM
#define WAKE_HOUR             6    // wake up at this hour
#define WAKE_MINUTE           30    // ... and this minute  → 06:30 = 6:30 AM
#define SLEEP_CHECK_INTERVAL_MS 30000  // check clock every 30 s

// ============================================================================
// Misc
// ============================================================================
#define RESET_HOLD_MS         2000
#define NVS_NAMESPACE         "occ"   // Preferences namespace for baseline storage

#endif // CONFIG_H
