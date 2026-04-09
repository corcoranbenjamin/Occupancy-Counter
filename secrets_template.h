#ifndef SECRETS_H
#define SECRETS_H

// ============================================================================
// COPY THIS FILE TO  secrets.h  AND FILL IN YOUR VALUES.
// secrets.h is git-ignored and will not be committed.
// ============================================================================

// --- WiFi mode ---------------------------------------------------------------
// By default the firmware connects via WPA2-Enterprise (eduroam).
// To use a standard WPA2-PSK network instead, uncomment the three
// lines below and fill in your home/hotspot credentials.
// ----------------------------------------------------------------------------
// #define USE_HOME_WIFI
// #define HOME_SSID       "your-home-ssid"
// #define HOME_PASSWORD   "your-home-password"

// --- eduroam (WPA2-Enterprise) — used when USE_HOME_WIFI is NOT defined -----
#define EDUROAM_SSID      "eduroam"
#define EDUROAM_USERNAME  "name.#@osu.edu"
#define EDUROAM_PASSWORD  "your-password"

// --- Google Cloud service account -------------------------------------------
#define GCP_PROJECT_ID    "your-gcp-project-id"
#define GCP_CLIENT_EMAIL  "sa-name@project-id.iam.gserviceaccount.com"

const char GCP_PRIVATE_KEY[] PROGMEM = "-----BEGIN PRIVATE KEY-----\n"
"PASTE YOUR KEY HERE (one quoted line per PEM line, \\n at the end)\n"
"-----END PRIVATE KEY-----\n";

// --- Google Sheet -----------------------------------------------------------
#define SPREADSHEET_ID  "your-spreadsheet-id-from-the-url"

#endif // SECRETS_H
