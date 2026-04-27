#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// Pin Definitions (ESP32-C6 + Pololu VL53L5CX carrier)
// ============================================================================
#define SDA_PIN             6
#define SCL_PIN             7
#define INT_PIN             5
#define RESET_BUTTON_PIN    3

// ============================================================================
// WiFi
// ============================================================================
#define AP_MODE
#define AP_SSID       "OccupancyCounter"
#define AP_PASSWORD   "12345678"

// #define STA_MODE
// #define STA_SSID     "YourNetwork"
// #define STA_PASSWORD "YourPassword"

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
// Misc
// ============================================================================
#define RESET_HOLD_MS         2000
#define WEB_PORT              80

#endif // CONFIG_H
