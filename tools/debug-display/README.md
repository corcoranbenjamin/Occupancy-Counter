# Tracking Debug Display

A standalone Arduino sketch for visually debugging the VL53L5CX tracking algorithm. It runs on the same hardware as the production firmware but replaces the cloud logging stack with a self-hosted WiFi access point and an in-browser debug page.

Use it for sensor placement, calibration validation, and tuning the tracker without needing eduroam, Google Sheets, or Grafana.

---

## What it shows

When you join the device's access point and load `http://192.168.4.1` you get a live page with:

- The full 8x8 distance grid, color-coded by depth.
- The learned baseline distance for each cell.
- The occupied mask after baseline subtraction.
- Detected blobs (BFS connected components) with their centroids.
- Persistent tracks across frames, including their `enter_row -> last_row` shift.
- Live entry / exit counts and the most recent crossing decision.

This makes it easy to answer questions like:

- Is the sensor mounted high enough?
- Is the baseline being learned with the doorway actually clear?
- Is a person being detected as one blob or fragmenting into several?
- Is the row-shift threshold appropriate for this doorway width?

The web UI is the same one shown in the GIF at the top of the parent repository's README.

---

## Hardware

Identical to the production firmware (parent repo `OccupancyCounter.ino`):

| Pin | Connection |
|---|---|
| `GPIO6` | I2C SDA |
| `GPIO7` | I2C SCL |
| `GPIO5` | Sensor INT |
| `GPIO3` | Reset button to GND |
| `3V3` / `GND` | Sensor power, sensor `LPn` tied to `3V3` |

Board: **ESP32C6 Dev Module** (`esp32` core 3.x).

The SD card and Google Sheets pipeline are not used here — the debug build only needs the sensor and the ESP32.

---

## Library

- [SparkFun VL53L5CX Arduino Library](https://github.com/sparkfun/SparkFun_VL53L5CX_Arduino_Library)

That is the only required library. No `secrets.h`, no service account, no eduroam credentials.

---

## Usage

1. Open `TrackingDebugDisplay.ino` in the Arduino IDE.
2. Select board: **ESP32C6 Dev Module**, with **USB CDC On Boot** enabled.
3. Hold `BOOT` while plugging in the USB cable, then upload.
4. Once flashed, the device broadcasts an open SSID:
   - **SSID:** `OccupancyCounter`
   - **Password:** `12345678`
5. Connect a laptop or phone to that SSID.
6. Browse to **http://192.168.4.1**.
7. Hold the reset button for 2 s to wipe the saved baseline and recalibrate (clear the doorway during the 10 s countdown).

---

## How it relates to the production firmware

| | Production (`OccupancyCounter.ino`) | Debug display (this folder) |
|---|---|---|
| WiFi | WPA2-Enterprise (eduroam) or WPA2-PSK | Open access point hosted by the device |
| Logging | Google Sheets + SD card CSV | None — live web UI only |
| Dashboard | Grafana Cloud | Built-in HTML page |
| Sleep schedule | 21:30 - 06:30 deep sleep | Always on while powered |
| Tracking algorithm | Identical | Identical |

Both sketches share the same `tracking.h` algorithm philosophy (multi-blob BFS, greedy track association, row-shift direction evaluation) but each carries its own copy. **Do not try to share `tracking.h` between the two folders** — they are tuned independently and may diverge over time.

---

## Further reading

- [`CODE_WALKTHROUGH.md`](CODE_WALKTHROUGH.md) — line-by-line explanation of the tracking algorithm written for someone learning it for the first time.
- Parent repo [README](../../README.md) — full project context, production firmware, dashboard setup.
