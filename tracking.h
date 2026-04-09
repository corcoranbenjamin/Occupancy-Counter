#ifndef TRACKING_H
#define TRACKING_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

// ============================================================================
// Multi-Blob Occupancy Tracker
//
// The VL53L5CX returns 64 distance readings arranged in an 8×8 grid
// (row-major: index 0 = top-left, index 63 = bottom-right).
// Rows run 0–7 top-to-bottom; columns run 0–7 left-to-right.
// A person walking through the sensor's field of view appears as a
// cluster of cells that are closer than the empty-doorway baseline.
//
//    col  0  1  2  3  4  5  6  7
//  row 0 [ 0][ 1][ 2][ 3][ 4][ 5][ 6][ 7]   ← entry side (if ENTRY_DIR = 1)
//      1 [ 8][ 9][10][11][12][13][14][15]
//      2 [16][17][18]...
//      ...
//      7 [56][57][58][59][60][61][62][63]   ← exit side
//
// Pipeline (every frame at ~15 Hz):
//   1. Min-distance filter  → smooths noise across MIN_FILTER_DEPTH frames
//   2. Ceiling + baseline   → binary occupied[] mask
//   3. BFS flood-fill       → find connected blobs (up to MAX_BLOBS)
//   4. Greedy association   → match blobs to persistent tracks by nearest centroid
//   5. Track evaluation     → when a track disappears, its row-shift
//                              (enter_row → last_row) decides entry vs exit
//
// Two people walking through back-to-back create two separate blobs and
// two independent tracks, so both crossings count correctly.
//
// When two blobs briefly merge into one (shoulders), MERGE_HOLD freezes updates
// so unmatched tracks are not evaluated mid-merge.
// ============================================================================

// Sentinel value meaning "no valid distance reading" in the min-filter buffer.
// Just above the sensor's 4000 mm max range so it never looks like a real reading.
static const int16_t DIST_INVALID = 4001;

class OccupancyTracker {
public:
    // --- per-frame sensor data ---------------------------------------------
    int16_t  current_distances[64];
    bool     occupied[64];

    // --- state (readable by web) -------------------------------------------
    bool     tracking_active;
    float    enter_centroid;        // primary track's enter row
    float    current_centroid;      // primary track's current row
    int      occ_count;
    int      num_tracks;

    // --- counters ----------------------------------------------------------
    int      occupancy;
    int      total_entries;
    int      total_exits;
    int      detect_ceiling;
    int8_t   entry_dir;
    uint32_t frames_processed;

    // --- calibration -------------------------------------------------------
    int16_t  baseline[64];
    bool     calibrated;

    // ======================================================================
    void init() {
        memset(current_distances, 0, sizeof(current_distances));
        memset(occupied, 0, sizeof(occupied));
        memset(_zone_valid, 0, sizeof(_zone_valid));
        tracking_active  = false;
        enter_centroid   = 0;
        current_centroid = 0;
        occ_count        = 0;
        num_tracks       = 0;
        occupancy        = 0;
        total_entries    = 0;
        total_exits      = 0;
        detect_ceiling   = DETECT_CEILING_MM;
        entry_dir        = ENTRY_DIR;
        frames_processed = 0;
        _filter_idx      = 0;
        for (int i = 0; i < 64; i++)
            for (int j = 0; j < MIN_FILTER_DEPTH; j++)
                _min_buf[i][j] = DIST_INVALID;
        for (int t = 0; t < MAX_TRACKS; t++)
            _tracks[t].active = false;
        _num_blobs       = 0;
        _merge_run_len   = 0;
        memset(baseline, 0, sizeof(baseline));
        calibrated       = false;
        _calib_count     = 0;
        memset(_calib_accum, 0, sizeof(_calib_accum));
        memset(_calib_n, 0, sizeof(_calib_n));
    }

    // --- calibration -------------------------------------------------------
    bool addCalibrationFrame(const int16_t* dist, const uint8_t* status) {
        for (int i = 0; i < 64; i++) {
            if (_isValid(status[i]) && dist[i] > 0) {
                _calib_accum[i] += dist[i];
                _calib_n[i]++;
            }
        }
        if (++_calib_count >= CALIB_FRAMES) {
            for (int i = 0; i < 64; i++)
                baseline[i] = _calib_n[i] ? (int16_t)(_calib_accum[i] / _calib_n[i])
                                           : CALIB_MAX_DIST_MM;
            calibrated = true;
            return true;
        }
        return false;
    }

    void resetCalibration() {
        calibrated = false;
        _calib_count = 0;
        memset(_calib_accum, 0, sizeof(_calib_accum));
        memset(_calib_n, 0, sizeof(_calib_n));
    }

    // --- NVS baseline persistence ------------------------------------------
    bool saveBaseline() {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, false);
        size_t written = prefs.putBytes("bl", baseline, sizeof(baseline));
        prefs.putBool("bl_ok", true);
        prefs.end();
        return (written == sizeof(baseline));
    }

    bool loadBaseline() {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, true);
        bool valid = prefs.getBool("bl_ok", false);
        if (valid) {
            size_t read = prefs.getBytes("bl", baseline, sizeof(baseline));
            if (read == sizeof(baseline)) {
                calibrated = true;
                prefs.end();
                return true;
            }
        }
        prefs.end();
        return false;
    }

    void clearSavedBaseline() {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, false);
        prefs.remove("bl");
        prefs.remove("bl_ok");
        prefs.end();
    }

    // --- main per-frame entry point ----------------------------------------
    void processFrame(const int16_t* dist, const uint8_t* status) {
        for (int i = 0; i < 64; i++) {
            _zone_valid[i]       = _isValid(status[i]) && dist[i] > 0;
            current_distances[i] = dist[i];
        }
        _updateMinFilter();
        _detectOccupied();

        occ_count = 0;
        for (int i = 0; i < 64; i++)
            if (occupied[i]) occ_count++;

        _findBlobs();
        _updateTracks();

        frames_processed++;
    }

    void resetCount() {
        occupancy     = 0;
        total_entries = 0;
        total_exits   = 0;
        for (int t = 0; t < MAX_TRACKS; t++)
            _tracks[t].active = false;
        num_tracks      = 0;
        tracking_active = false;
        _merge_run_len  = 0;
    }

    void resetTracking() {
        for (int t = 0; t < MAX_TRACKS; t++)
            _tracks[t].active = false;
        num_tracks      = 0;
        tracking_active = false;
        _merge_run_len  = 0;
    }

private:
    // --- per-frame intermediate --------------------------------------------
    bool     _zone_valid[64];

    // --- min-distance filter -----------------------------------------------
    int16_t  _min_buf[64][MIN_FILTER_DEPTH];
    uint8_t  _filter_idx;

    // --- blob detection scratch --------------------------------------------
    struct Blob { float row, col; int cells; };
    Blob     _blobs[MAX_BLOBS];
    int      _num_blobs;
    bool     _visited[64];
    uint8_t  _queue[64];

    // --- persistent tracks -------------------------------------------------
    struct Track {
        bool     active;
        float    enter_row;     // row centroid when track was created
        float    last_row;      // most recently matched row centroid
        float    row, col;      // current 2-D centroid (for association)
        uint32_t start_frame;
        int      frames;        // total frames this track has been matched
        int      misses;        // consecutive unmatched frames
    };
    Track    _tracks[MAX_TRACKS];

    // --- calibration accumulators ------------------------------------------
    int      _calib_count;
    int32_t  _calib_accum[64];
    int      _calib_n[64];

    // --- short merge (two tracks, one blob) --------------------------------
    uint8_t  _merge_run_len;

    // --- helpers -----------------------------------------------------------

    // VL53L5CX target_status values we accept as valid readings (ST UM2884):
    //   5 = range valid
    //   6 = range valid but large sigma (noisy, still usable)
    //   9 = range valid, detected by cross-talk compensation
    static bool _isValid(uint8_t s) { return s == 5 || s == 6 || s == 9; }

    void _updateMinFilter() {
        for (int i = 0; i < 64; i++)
            _min_buf[i][_filter_idx] = _zone_valid[i] ? current_distances[i] : DIST_INVALID;
        _filter_idx = (_filter_idx + 1) % MIN_FILTER_DEPTH;
    }

    int16_t _filteredDist(int z) const {
        int16_t m = _min_buf[z][0];
        for (int j = 1; j < MIN_FILTER_DEPTH; j++)
            if (_min_buf[z][j] < m) m = _min_buf[z][j];
        return m;
    }

    // Before calibration: any reading under the ceiling counts as occupied.
    // After calibration: a zone is occupied only if the filtered distance is
    // significantly shorter than the learned baseline (empty-doorway distance),
    // rejecting static objects like the door frame and walls.
    void _detectOccupied() {
        for (int i = 0; i < 64; i++) {
            int16_t filtered = _filteredDist(i);
            bool underCeiling = (filtered > DETECT_FLOOR_MM && filtered < detect_ceiling
                                 && filtered < DIST_INVALID);

            if (calibrated && underCeiling)
                occupied[i] = (baseline[i] - filtered > STATIC_MARGIN_MM);
            else
                occupied[i] = underCeiling;
        }
    }

    // --- BFS blob detection ------------------------------------------------
    void _findBlobs() {
        memset(_visited, 0, sizeof(_visited));
        _num_blobs = 0;

        for (int i = 0; i < 64 && _num_blobs < MAX_BLOBS; i++) {
            if (!occupied[i] || _visited[i]) continue;

            int head = 0, tail = 0;
            _queue[tail++] = (uint8_t)i;
            _visited[i] = true;

            float rsum = 0, csum = 0;
            int   cnt  = 0;

            while (head < tail) {
                uint8_t cell = _queue[head++];
                int r = cell / 8, c = cell % 8;
                rsum += r;
                csum += c;
                cnt++;

                if (r > 0 && occupied[cell-8] && !_visited[cell-8])
                    { _visited[cell-8] = true; _queue[tail++] = cell - 8; }
                if (r < 7 && occupied[cell+8] && !_visited[cell+8])
                    { _visited[cell+8] = true; _queue[tail++] = cell + 8; }
                if (c > 0 && occupied[cell-1] && !_visited[cell-1])
                    { _visited[cell-1] = true; _queue[tail++] = cell - 1; }
                if (c < 7 && occupied[cell+1] && !_visited[cell+1])
                    { _visited[cell+1] = true; _queue[tail++] = cell + 1; }
            }

            if (cnt >= (int)MIN_BLOB_SIZE) {
                _blobs[_num_blobs].row   = rsum / cnt;
                _blobs[_num_blobs].col   = csum / cnt;
                _blobs[_num_blobs].cells = cnt;
                _num_blobs++;
            }
        }
    }

    // --- multi-target track update -----------------------------------------
    void _updateTracks() {
        int n_active = 0;
        for (int t = 0; t < MAX_TRACKS; t++)
            if (_tracks[t].active) n_active++;

        bool freeze_merge = false;
        if (n_active >= 2 && _num_blobs == 1 && _blobs[0].cells >= MERGE_MIN_BLOB_CELLS) {
            if (_merge_run_len < MERGE_HOLD_MAX_FRAMES) {
                freeze_merge = true;
                _merge_run_len++;
            } else
                _merge_run_len = 0;
        } else
            _merge_run_len = 0;

        if (freeze_merge) {
            _publishPrimaryTrack();
            return;
        }

        bool blobTaken[MAX_BLOBS]   = {};
        bool trackMatched[MAX_TRACKS] = {};

        // ── 1. Associate: greedily pair each track with its nearest blob ──
        for (int iter = 0; iter < MAX_TRACKS; iter++) {
            float bestDist  = MAX_ASSOC_DIST + 1;
            int   bestTrack = -1, bestBlob = -1;

            for (int t = 0; t < MAX_TRACKS; t++) {
                if (!_tracks[t].active || trackMatched[t]) continue;
                for (int b = 0; b < _num_blobs; b++) {
                    if (blobTaken[b]) continue;
                    float deltaRow = _blobs[b].row - _tracks[t].row;
                    float deltaCol = _blobs[b].col - _tracks[t].col;
                    float dist     = sqrtf(deltaRow * deltaRow + deltaCol * deltaCol);
                    if (dist < bestDist) { bestDist = dist; bestTrack = t; bestBlob = b; }
                }
            }
            if (bestTrack < 0) break;

            blobTaken[bestBlob]           = true;
            trackMatched[bestTrack]       = true;
            _tracks[bestTrack].row        = _blobs[bestBlob].row;
            _tracks[bestTrack].col        = _blobs[bestBlob].col;
            _tracks[bestTrack].last_row   = _blobs[bestBlob].row;
            _tracks[bestTrack].frames++;
            _tracks[bestTrack].misses     = 0;
        }

        // ── 2. Expire: unmatched tracks accumulate misses, then get evaluated ──
        for (int t = 0; t < MAX_TRACKS; t++) {
            if (!_tracks[t].active || trackMatched[t]) continue;
            _tracks[t].misses++;
            if (_tracks[t].misses > MISS_GRACE_FRAMES ||
                (frames_processed - _tracks[t].start_frame) > (uint32_t)MAX_CROSSING_FRAMES) {
                _evaluateTrack(t);
                _tracks[t].active = false;
            }
        }

        // ── 3. Spawn: unmatched blobs become new tracks ──
        for (int b = 0; b < _num_blobs; b++) {
            if (blobTaken[b]) continue;
            for (int t = 0; t < MAX_TRACKS; t++) {
                if (_tracks[t].active) continue;
                _tracks[t].active      = true;
                _tracks[t].enter_row   = _blobs[b].row;
                _tracks[t].last_row    = _blobs[b].row;
                _tracks[t].row         = _blobs[b].row;
                _tracks[t].col         = _blobs[b].col;
                _tracks[t].start_frame = frames_processed;
                _tracks[t].frames      = 1;
                _tracks[t].misses      = 0;
                break;
            }
        }

        // ── 4. Expose: publish primary track info for the web UI ──
        _publishPrimaryTrack();
    }

    void _publishPrimaryTrack() {
        num_tracks = 0;
        int primary = -1;
        for (int t = 0; t < MAX_TRACKS; t++) {
            if (!_tracks[t].active) continue;
            num_tracks++;
            if (primary < 0 || _tracks[t].frames > _tracks[primary].frames)
                primary = t;
        }
        tracking_active = (num_tracks > 0);
        if (primary >= 0) {
            enter_centroid   = _tracks[primary].enter_row;
            current_centroid = _tracks[primary].row;
        } else {
            current_centroid = -1;
        }
    }

    // Decide whether a completed track was an entry or exit.
    // shift = last_row − enter_row.  Positive shift means the person moved
    // toward higher row numbers.  If that direction matches entry_dir,
    // the person entered; otherwise they exited.
    void _evaluateTrack(int t) {
        if (_tracks[t].frames < MIN_CROSSING_FRAMES) return;

        float shift = _tracks[t].last_row - _tracks[t].enter_row;
        if (fabsf(shift) < MIN_CENTROID_SHIFT) return;

        bool movedToHigherRows = (shift > 0);
        if (movedToHigherRows == (entry_dir > 0)) {
            occupancy++;
            total_entries++;
        } else {
            if (occupancy > 0) occupancy--;
            total_exits++;
        }
    }
};

#endif // TRACKING_H
