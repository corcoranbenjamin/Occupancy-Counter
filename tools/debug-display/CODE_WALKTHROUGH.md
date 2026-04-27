# Occupancy Counter VL53L5CX — Code Walkthrough

## What This Project Does

A VL53L5CX time-of-flight sensor is mounted on the ceiling above a doorway, pointing straight down. It outputs an 8×8 grid of distance measurements 15 times per second. When a person walks underneath, some cells read closer than the empty doorway. The software detects those clusters, tracks them as they move across rows, and when they disappear off the other side, decides: was that an entry or an exit?

An ESP32-C3 microcontroller runs the sensor, the tracking algorithm, and a WiFi access point with a web dashboard showing the live count.

### Hardware

| Component | Role |
|---|---|
| **VL53L5CX** (on Pololu carrier board) | 8×8 multizone time-of-flight sensor, up to 4 m range |
| **ESP32-C3-DevKitC-02** | WiFi-capable RISC-V microcontroller running the firmware |
| **Physical button** on GPIO9 | Hold 2 seconds to reset counts |

Wiring: `VIN→3V3`, `GND→GND`, `SDA→GPIO4`, `SCL→GPIO5`, `LPn→3V3`, `INT→GPIO6`.

### References

- **VL53L5CX datasheet (UM2884):** [st.com/resource/en/user_manual/um2884](https://www.st.com/resource/en/user_manual/um2884-a-guide-to-using-the-vl53l5cx-multizone-timeofflight-ranging-sensor-with-a-wide-field-of-view-stmicroelectronics.pdf) — target_status codes, resolution modes, ranging frequency
- **VL53L5CX ranging guide (UM2600):** [st.com/resource/en/user_manual/um2600](https://www.st.com/resource/en/user_manual/um2600-vl53l1x-ranging-and-calibration-guide-stmicroelectronics.pdf) — min-distance filter technique (§6.2)
- **SparkFun VL53L5CX Arduino Library:** [github.com/sparkfun/SparkFun_VL53L5CX_Arduino_Library](https://github.com/sparkfun/SparkFun_VL53L5CX_Arduino_Library) — the driver used to communicate with the sensor
- **ESP32-C3 Technical Reference:** [docs.espressif.com](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/) — GPIO interrupts, I2C, WiFi AP mode
- **Arduino ESP32 Core:** [github.com/espressif/arduino-esp32](https://github.com/espressif/arduino-esp32) — `WebServer`, `WiFi`, `Wire` libraries

---

## Project Structure

```
OccupancyCounterVL53L5CX/
├── OccupancyCounterVL53L5CX.ino   Main Arduino sketch (setup, loop, web server)
├── config.h                        All tuneable constants (pins, thresholds, timing)
├── tracking.h                      OccupancyTracker class (the detection/tracking algorithm)
└── web_page.h                      HTML/CSS/JS dashboard served by the ESP32
```

---

## File-by-File Breakdown

---

### config.h — Tuneable Constants

This file contains only `#define` values. It is the single place to adjust behavior without touching logic. Every constant is used by name in the other files.

#### Pin Definitions

| Constant | Value | Purpose |
|---|---|---|
| `SDA_PIN` | 4 | I2C data line to the VL53L5CX |
| `SCL_PIN` | 5 | I2C clock line to the VL53L5CX |
| `INT_PIN` | 6 | Interrupt line — sensor pulls LOW when a new frame is ready |
| `RESET_BUTTON_PIN` | 9 | Physical button; hold 2 s to reset occupancy counts |

#### WiFi

| Constant | Value | Purpose |
|---|---|---|
| `AP_MODE` | (defined) | Enables Access Point mode — the ESP32 creates its own WiFi network |
| `AP_SSID` | `"OccupancyCounter"` | Network name |
| `AP_PASSWORD` | `"12345678"` | Network password |
| `STA_MODE` | (commented out) | Alternative: connect to an existing network as a client |

Only one mode should be active at a time. If `STA_MODE` is enabled and the connection fails, the code falls back to AP mode automatically.

#### Sensor

| Constant | Value | Purpose |
|---|---|---|
| `SENSOR_I2C_ADDR` | `0x29` | Default I2C address of the VL53L5CX (per UM2884 §3.2) |
| `SENSOR_FREQ_HZ` | 15 | Frames per second. The sensor supports 1–60 Hz; 15 balances responsiveness and power |
| `I2C_CLOCK_HZ` | 400000 | 400 kHz I2C Fast Mode |

#### Detection

| Constant | Value | Purpose |
|---|---|---|
| `DETECT_CEILING_MM` | 1000 | Anything closer than 1000 mm (1 m) below the sensor is "something is there." Adjustable at runtime via the web UI slider (200–2000 mm range). |

#### Tracking Parameters

| Constant | Value | Purpose |
|---|---|---|
| `MAX_BLOBS` | 4 | Maximum simultaneous blob detections per frame |
| `MAX_TRACKS` | 4 | Maximum simultaneous persistent tracks |
| `MIN_BLOB_SIZE` | 2 | Minimum connected occupied cells to count as a real blob (rejects single-cell noise) |
| `MAX_ASSOC_DIST` | 3.0 | Maximum centroid distance (in grid cells) for matching a blob to an existing track between frames |
| `MIN_CROSSING_FRAMES` | 3 | A track must last at least 3 frames (~200 ms) to count as a real crossing (rejects brief noise) |
| `MAX_CROSSING_FRAMES` | 90 | A track lasting more than 90 frames (~6 s) is force-expired (person standing still, not crossing) |
| `MIN_CENTROID_SHIFT` | 1.5 | The centroid must move at least 1.5 grid rows from entry to exit to count (rejects partial entries) |
| `MISS_GRACE_FRAMES` | 5 | If a track loses its blob match for 5 consecutive frames (~333 ms), expire it (handles brief sensor dropouts) |
| `ENTRY_DIR` | -1 | Which row direction is "entering." `1` = moving toward higher rows = entry. `-1` = moving toward lower rows = entry. Flippable at runtime via the web UI. |

#### Noise Filtering

| Constant | Value | Purpose |
|---|---|---|
| `MIN_FILTER_DEPTH` | 3 | Keep the last 3 frames per cell, take the minimum distance. This is the "min-distance filter" recommended in ST's ranging guide (UM2600 §6.2). Smooths out transient noise spikes. |
| `STATIC_MARGIN_MM` | 100 | After calibration, a cell must be at least 100 mm closer than baseline to be "occupied." This rejects the door frame, walls, and floor from being detected as people. |

#### Calibration

| Constant | Value | Purpose |
|---|---|---|
| `CALIB_FRAMES` | 30 | Average 30 frames (~2 seconds at 15 Hz) of the empty doorway to learn the baseline distances |
| `CALIB_MAX_DIST_MM` | 4000 | If a cell never got a valid reading during calibration, assume 4000 mm (sensor max range) |

---

### tracking.h — The OccupancyTracker Class

This is the brain of the system. It contains all detection, tracking, and counting logic.

#### The 8×8 Grid

The VL53L5CX returns 64 distance readings arranged in an 8×8 grid, stored in row-major order (index 0 = top-left, index 63 = bottom-right):

```
     col  0    1    2    3    4    5    6    7
row 0   [ 0] [ 1] [ 2] [ 3] [ 4] [ 5] [ 6] [ 7]    ← entry side (when ENTRY_DIR = 1)
    1   [ 8] [ 9] [10] [11] [12] [13] [14] [15]
    2   [16] [17] [18] [19] [20] [21] [22] [23]
    3   [24] [25] [26] [27] [28] [29] [30] [31]
    4   [32] [33] [34] [35] [36] [37] [38] [39]
    5   [40] [41] [42] [43] [44] [45] [46] [47]
    6   [48] [49] [50] [51] [52] [53] [54] [55]
    7   [56] [57] [58] [59] [60] [61] [62] [63]    ← exit side
```

To convert a flat index to row/column: `row = index / 8`, `col = index % 8`.
Neighbor offsets: up = `index - 8`, down = `index + 8`, left = `index - 1`, right = `index + 1`.

#### Data Structures

**Public fields** (read by the web server to build JSON responses):

| Field | Type | Purpose |
|---|---|---|
| `current_distances[64]` | `int16_t[]` | Raw distance readings from the latest frame (mm) |
| `occupied[64]` | `bool[]` | Binary mask: is something detected in this cell? |
| `tracking_active` | `bool` | Is at least one track currently alive? |
| `enter_centroid` | `float` | The longest-lived track's starting row position |
| `current_centroid` | `float` | The longest-lived track's current row position |
| `occ_count` | `int` | How many cells are occupied this frame |
| `num_tracks` | `int` | How many active tracks exist right now |
| `occupancy` | `int` | **THE COUNT** — entries minus exits (never goes below 0) |
| `total_entries` | `int` | Cumulative entry count since last reset |
| `total_exits` | `int` | Cumulative exit count since last reset |
| `detect_ceiling` | `int` | Current ceiling threshold in mm (adjustable at runtime) |
| `entry_dir` | `int8_t` | +1 or -1, which row direction counts as "entry" (flippable at runtime) |
| `frames_processed` | `uint32_t` | Total frames processed since boot |
| `baseline[64]` | `int16_t[]` | Learned empty-doorway distances from calibration |
| `calibrated` | `bool` | Has calibration completed? |

**Private fields:**

| Field | Type | Purpose |
|---|---|---|
| `_zone_valid[64]` | `bool[]` | Per-cell: did the sensor return a trustworthy reading this frame? |
| `_min_buf[64][3]` | `int16_t[][]` | Circular buffer holding last 3 distance readings per cell (for min filter) |
| `_filter_idx` | `uint8_t` | Current write position in the circular buffer (cycles 0→1→2→0→...) |
| `_blobs[4]` | `Blob[]` | This frame's detected blobs (each has a row/col centroid) |
| `_num_blobs` | `int` | How many blobs were found this frame |
| `_visited[64]` | `bool[]` | Scratch array for BFS flood-fill |
| `_queue[64]` | `uint8_t[]` | Scratch queue for BFS flood-fill |
| `_tracks[4]` | `Track[]` | Persistent tracked objects that live across frames |
| `_calib_count` | `int` | How many calibration frames collected so far |
| `_calib_accum[64]` | `int32_t[]` | Sum of valid distances per cell during calibration |
| `_calib_n[64]` | `int[]` | Count of valid readings per cell during calibration |

**The Track struct:**

| Field | Type | Purpose |
|---|---|---|
| `active` | `bool` | Is this track slot currently in use? |
| `enter_row` | `float` | Row centroid when the track was first created (where the person "entered" the field of view) |
| `last_row` | `float` | Row centroid the last time the track was matched to a blob |
| `row`, `col` | `float` | Current 2-D centroid position (used for distance matching) |
| `start_frame` | `uint32_t` | Frame number when this track was born |
| `frames` | `int` | Total frames this track has been matched to a blob |
| `misses` | `int` | Consecutive frames where no blob matched this track |

#### Functions

##### `init()`

Zeros out all arrays and state. Sets `detect_ceiling` and `entry_dir` to their config defaults. Fills the min-filter buffer with `DIST_INVALID` (4001) so no ghost readings appear before real data arrives. Marks all track slots as inactive. Called once in `setup()`.

##### `addCalibrationFrame(dist, status)` — Learning the empty doorway

Called once per frame during the first ~2 seconds (while `calibrated` is false). For each of the 64 cells, if the sensor reading is valid, it accumulates the distance into `_calib_accum[i]` and increments `_calib_n[i]`. After 30 frames, it computes the average distance per cell → this becomes `baseline[i]`, the "this is how far away the floor/walls are when nobody is there" reference. If a cell never got a valid reading, it defaults to 4000 mm. Returns `true` when calibration finishes.

##### `resetCalibration()`

Clears the accumulators so calibration can restart from scratch.

##### `processFrame(dist, status)` — The main pipeline

This is the heart of the system. Called every frame (~15 Hz). It runs 5 stages:

**Stage 1: Validate and store raw data** (tracking.h lines 121–124)

For each of the 64 cells, checks if the sensor's `target_status` byte is one of the valid codes (5, 6, or 9 — see `_isValid()` below). Stores the result in `_zone_valid[i]`. Also copies the raw distance into `current_distances[i]`.

**Stage 2: `_updateMinFilter()`** (tracking.h lines 195–199)

Writes this frame's distances into the circular buffer `_min_buf`. Invalid cells get `DIST_INVALID` (4001) written instead, so they can't accidentally register as "close." Advances the circular index. This implements ST's recommended "min-distance filter" (UM2600 §6.2) — by keeping 3 frames and taking the minimum, brief noise spikes where the distance jumps high for one frame get rejected.

**Stage 3: `_detectOccupied()`** (tracking.h lines 212–222)

For each cell, calls `_filteredDist(i)` which returns the minimum distance across the last 3 frames. Then decides if that cell is "occupied":

- **Before calibration:** if the filtered distance is > 0, below the ceiling threshold, and not `DIST_INVALID`, the cell is occupied. This is a simple range check.
- **After calibration:** additionally requires that `baseline[i] - filtered > STATIC_MARGIN_MM` (100 mm). This is the static-object rejection. Example: the door frame consistently reads 800 mm. The baseline learned 800 mm. So `800 - 800 = 0`, which is not > 100, and the door frame is correctly ignored. But a person standing at 600 mm: `800 - 600 = 200 > 100`, so they are detected.

**Stage 4: `_findBlobs()`** (tracking.h lines 225–262)

BFS (Breadth-First Search) flood-fill on the `occupied[]` grid. Scans cells 0 through 63 in order. When it finds an occupied cell that hasn't been visited, it starts a BFS from that cell, visiting all 4-connected neighbors (up, down, left, right) that are also occupied. This groups connected occupied cells into a "blob."

As it visits each cell, it accumulates the row and column coordinates to compute the blob's centroid: `centroid_row = sum_of_rows / count`, `centroid_col = sum_of_cols / count`.

If the blob has at least `MIN_BLOB_SIZE` (2) cells, it's saved. Up to 4 blobs per frame.

**What is a centroid?**

The centroid is the average position of all occupied cells in a blob — its geometric center. It's a fractional (row, col) coordinate that doesn't necessarily land on any actual cell.

For each cell BFS visits, the flat index (0–63) is converted to row/col via `row = cell / 8`, `col = cell % 8`. The row and column values are summed, then divided by the cell count:

```
centroid_row = sum_of_rows / count
centroid_col = sum_of_cols / count
```

Example: a person's head/shoulders occupy 4 cells:

```
     col 0  1  2  3  4  5  6  7
row 3   [ ][ ][XX][XX][ ][ ][ ][ ]
row 4   [ ][ ][XX][XX][ ][ ][ ][ ]
```

Occupied cells are at (3,2), (3,3), (4,2), (4,3):
- `row_sum = 3 + 3 + 4 + 4 = 14`, `col_sum = 2 + 3 + 2 + 3 = 10`, `count = 4`
- **Centroid = (14/4, 10/4) = (3.5, 2.5)**

As the person walks from row 1 to row 6, the centroid's row component smoothly increases from ~1.5 to ~5.5. The tracker records the row at entry (`enter_row`) and the row when the track expires (`last_row`). The difference — the **row shift** — is what decides entry vs exit:

```
shift = last_row - enter_row
      = 5.5 - 1.5
      = +4.0 rows (moved toward higher rows)
```

The column component is only used for **matching** — computing the Euclidean distance between a blob and an existing track so the tracker can figure out which blob belongs to which person across frames.

**Stage 5: `_updateTracks()`** (tracking.h lines 265–339)

The most complex function. It has 4 sub-steps:

**5a. Associate** (lines 269–293): Greedy nearest-centroid matching. For each iteration, finds the globally closest (existing track, new blob) pair by Euclidean distance between their centroids. If the distance is within `MAX_ASSOC_DIST` (3.0 grid cells), pairs them: updates the track's position to the blob's centroid, increments its frame count, resets its miss counter. Marks both as "taken." Repeats until no more valid pairs exist.

**5b. Expire** (lines 295–304): Any active track that didn't get matched to a blob this frame gets a miss. If its misses exceed `MISS_GRACE_FRAMES` (5 frames), or it has been alive longer than `MAX_CROSSING_FRAMES` (90 frames), it is expired: call `_evaluateTrack()` to decide if it was an entry or exit, then deactivate it.

**5c. Spawn** (lines 306–321): Any blob that wasn't matched to an existing track becomes a new track. Its `enter_row` is recorded — this is where the person "entered" the sensor's field of view.

**5d. Expose** (lines 323–338): Counts active tracks and finds the "primary" one (the one that has been alive the longest). Exports its `enter_row` and current `row` as `enter_centroid` and `current_centroid` for the web UI to display.

##### `_evaluateTrack(t)` — The entry/exit decision (tracking.h lines 345–358)

This is the core counting logic. When a track expires (the person has left the field of view):

1. **Duration check:** If the track existed for fewer than `MIN_CROSSING_FRAMES` (3 frames, ~200 ms), discard it — too brief to be a real person.
2. **Shift check:** Compute `shift = last_row - enter_row`. If `|shift| < MIN_CENTROID_SHIFT` (1.5 rows), discard — the person didn't move far enough across the grid (maybe poked their head in and pulled back).
3. **Direction check:** If `shift > 0`, the person moved toward higher row numbers. If that matches `entry_dir`, it's an **entry** (occupancy++ and total_entries++). Otherwise it's an **exit** (total_exits++, and occupancy-- but clamped to never go below 0).

##### `resetCount()`

Zeros `occupancy`, `total_entries`, `total_exits`. Deactivates all tracks. Called by the web UI "Reset" button or the physical reset button.

##### `resetTracking()`

Deactivates all tracks but preserves the counters. Called when the ceiling threshold is changed at runtime (since the occupied mask changes, existing tracks become invalid).

##### `_isValid(status)` — Sensor reading validation (tracking.h line 193)

Returns `true` if the VL53L5CX `target_status` byte is 5, 6, or 9. These are defined in ST's UM2884 datasheet:
- **5** = range valid
- **6** = range valid but large sigma (noisy measurement, still usable)
- **9** = range valid, detected by cross-talk compensation

All other status codes (0–4, 7, 8, 10+) indicate invalid or failed measurements.

##### `_filteredDist(zone)` — Min-filter readout (tracking.h lines 201–206)

Returns the minimum distance across the last `MIN_FILTER_DEPTH` (3) frames for a given cell. This is how the min-distance filter produces its output — by taking the minimum, any single-frame spike where the distance jumps to a large value is overridden by the valid readings from the adjacent frames.

---

### OccupancyCounterVL53L5CX.ino — Main Arduino Sketch

This file wires everything together: initializes hardware, runs the main loop, serves the web interface.

#### Global State

| Variable | Type | Purpose |
|---|---|---|
| `sensor` | `SparkFun_VL53L5CX` | Sensor driver object (from SparkFun library) |
| `sensorData` | `VL53L5CX_ResultsData` | Buffer where the library writes raw results |
| `tracker` | `OccupancyTracker` | The tracking engine (from tracking.h) |
| `server` | `WebServer` | HTTP server on port 80 (from ESP32 Arduino core) |
| `sensorDataReady` | `volatile bool` | Flag set by the interrupt service routine when the sensor has a frame ready |
| `resetBtnDown` | `unsigned long` | Timestamp (ms) when the physical button was first pressed |
| `resetBtnActive` | `bool` | Debounce flag for the physical button |

#### `onSensorInt()` — Interrupt Service Routine (.ino line 37)

```cpp
void IRAM_ATTR onSensorInt() { sensorDataReady = true; }
```

Runs in hardware interrupt context when GPIO6 (INT_PIN) goes LOW — meaning the sensor has a new frame ready. Simply sets a flag. `IRAM_ATTR` keeps the function in RAM for fast execution (required for ISRs on ESP32). The `volatile` keyword on the flag variable ensures the compiler doesn't optimize away the read in `loop()`.

This is **interrupt-driven** sensing — the CPU sleeps between frames instead of continuously polling the sensor over I2C.

#### `setup()` — Runs once at boot (.ino lines 48–67)

1. Start serial at 115200 baud (for debug output via USB)
2. Configure GPIO9 as input with internal pull-up (for the physical reset button)
3. Start I2C bus on GPIO4 (SDA) and GPIO5 (SCL) at 400 kHz
4. `initSensor()` — find and configure the VL53L5CX
5. `connectWiFi()` — start the WiFi access point
6. `setupRoutes()` — register HTTP endpoints
7. Start the web server
8. `tracker.init()` — zero out all tracker state
9. Print startup summary to Serial

#### `loop()` — Runs continuously (.ino lines 69–94)

1. `server.handleClient()` — check if any HTTP request came in; if so, serve it
2. `handleResetButton()` — check if the physical button is being held
3. Check `sensorDataReady` — if the interrupt hasn't fired, call `delay(5)` and return. The 5 ms delay lets the RTOS idle task run, which enables the ESP32's automatic light sleep (CPU sleeps until the next interrupt or WiFi beacon).
4. If a frame is ready: clear the flag, read the data via I2C with `sensor.getRangingData()`, copy `distance_mm[]` and `target_status[]` into local arrays, pass them to `tracker.processFrame()`
5. If the tracker isn't calibrated yet, feed the frame to `addCalibrationFrame()`. When it returns `true` (30 frames collected), print the baseline grid to Serial.

#### `initSensor()` (.ino lines 98–118)

Tries to contact the VL53L5CX over I2C up to 5 times with 2-second delays between attempts. If it never responds, halts forever with an error message (the device is useless without the sensor). On success:
- Sets resolution to 8×8 (64 zones)
- Sets ranging frequency to 15 Hz
- Starts continuous ranging
- Configures GPIO6 as an interrupt input — the sensor pulls it LOW on every new frame

#### `connectWiFi()` (.ino lines 120–139)

Two compile-time paths controlled by `#ifdef`:

- **AP mode** (enabled by default): Creates a WiFi network named "OccupancyCounter" with password "12345678". The ESP32's IP is 192.168.4.1. Connect your phone or laptop to this network and open that IP in a browser.
- **STA mode** (commented out in config.h): Connects to an existing WiFi network as a client. If connection fails after 20 seconds (40 × 500 ms), automatically falls back to AP mode.

#### `setupRoutes()` — HTTP Endpoints (.ino lines 143–167)

| Route | Method | What it does |
|---|---|---|
| `/` | GET | Serves the full HTML/CSS/JS dashboard from `web_page.h` |
| `/data` | GET | Returns JSON with all tracker state (polled by the dashboard every 200 ms) |
| `/reset` | GET | Zeros occupancy, entries, exits; kills all tracks |
| `/flip` | GET | Reverses `entry_dir` (multiplies by -1) — swaps which direction is "in" vs "out" |
| `/config?ceiling=N` | GET | Changes the detection ceiling threshold at runtime (clamped to 200–2000 mm); resets active tracks |

#### `sendJson()` — Build JSON response (.ino lines 171–208)

Builds a JSON string by concatenating tracker fields. Uses the `F()` macro to keep string literals in flash memory (saves RAM on the ESP32). The JSON payload includes:

- `occupancy`, `entries`, `exits` — the counts
- `ceiling`, `frames` — configuration and status
- `occ_n` — how many cells are occupied right now
- `tracking`, `num_tracks`, `enter_row`, `cur_row` — live tracking state for the UI
- `cal` — whether calibration is complete
- `distances[64]` — distance for occupied cells, -1 for unoccupied (used by the heatmap grid)
- `occupied[64]` — 1/0 binary mask (used for the green highlight outlines on the grid)

#### `handleResetButton()` (.ino lines 212–221)

Simple hold-to-reset debouncing:
1. Read GPIO9. If LOW (button pressed) and wasn't previously active, record the timestamp.
2. If released, clear the active flag.
3. If held continuously for ≥ 2000 ms (`RESET_HOLD_MS`), call `tracker.resetCount()`.

#### `printBaseline()` (.ino lines 223–230)

Debug helper. After calibration completes, prints the 8×8 baseline distance grid to Serial in a formatted table so you can verify the sensor is reading reasonable values for the empty doorway.

---

### web_page.h — The Dashboard

Contains a single `const char[]` in PROGMEM (flash memory) with the complete HTML, CSS, and JavaScript for the web dashboard. The ESP32 serves this at `/`.

The dashboard polls `/data` every 200 ms via `fetch()`, parses the JSON, and updates:
- The big occupancy number
- Entry/exit counters
- An 8×8 heatmap grid showing live distance readings (colored by proximity)
- Green outlines on occupied cells
- Centroid tracking lines (enter position and current position)
- A tracking status line showing direction and row shift
- An event log sidebar recording each entry/exit with timestamps
- A ceiling slider for runtime adjustment
- Reset and Flip Direction buttons

---

## How It All Fits Together — One Complete Cycle

```
1.  Sensor finishes measuring
         │
         ▼
2.  Pulls INT_PIN (GPIO6) LOW
         │
         ▼
3.  onSensorInt() ISR fires → sets sensorDataReady = true
         │
         ▼
4.  loop() sees the flag → reads 64 distances + 64 statuses via I2C
         │
         ▼
5.  processFrame() runs the 5-stage pipeline:
         │
         ├─ Stage 1: Validate readings (_isValid checks status codes 5, 6, 9)
         ├─ Stage 2: Min-filter (smooth noise across 3 frames)
         ├─ Stage 3: Detect occupied cells (ceiling check + baseline comparison)
         ├─ Stage 4: BFS flood-fill → find connected blobs → compute centroids
         └─ Stage 5: Match blobs to tracks → expire old tracks → spawn new tracks
                │
                ▼
6.  If a track expired → _evaluateTrack() checks row shift → increments
    occupancy (entry) or decrements it (exit)
         │
         ▼
7.  Meanwhile, the browser calls /data every 200 ms
         │
         ▼
8.  sendJson() reads the tracker's public fields → returns JSON
         │
         ▼
9.  JavaScript updates the dashboard display
```

The sensor measures. The tracker thinks. The web server reports.
