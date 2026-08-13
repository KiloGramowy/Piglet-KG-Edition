#pragma once
#include <Arduino.h>
#include "PinMapDefs.h"

static const uint32_t GPS_CACHE_DEFAULT_MINUTES = 3;
static const uint32_t GPS_CACHE_MIN_MINUTES     = 1;
static const uint32_t GPS_CACHE_MAX_MINUTES     = 7UL * 24UL * 60UL;  // 1 week
static const uint8_t WIFI24_CHANNEL_MAX_COUNT   = 14;
static const uint8_t WIFI5_CHANNEL_MAX_COUNT    = 28;
static const uint16_t WIFI_DWELL_MIN_MS         = 20;
static const uint16_t WIFI_DWELL_MAX_MS         = 1500;
static const uint16_t BLE_SCAN_DURATION_DEFAULT_MS = 1000;
static const uint16_t BLE_SCAN_DURATION_MIN_MS     = 250;
static const uint16_t BLE_SCAN_DURATION_MAX_MS     = 5000;
static const uint16_t BLE_EVERY_N_CYCLES_DEFAULT  = 5;
static const uint16_t BLE_EVERY_N_CYCLES_MIN      = 1;
static const uint16_t BLE_EVERY_N_CYCLES_MAX      = 100;
static const char* SCAN_PROFILE_DEFAULT            = "kg";
static const uint16_t WIFI24_DWELL_KG_RECOMMENDED_MS = 110;
static const uint16_t WIFI5_DWELL_KG_RECOMMENDED_MS  = 100;
static const uint16_t UPLOADED_LOGS_TO_KEEP_DEFAULT  = 10;
static const uint16_t UPLOADED_LOGS_TO_KEEP_MIN      = 1;
static const uint16_t UPLOADED_LOGS_TO_KEEP_MAX      = 9999;

struct Config {
  String wigleBasicToken;
  String homeSsid;
  String homePsk;
  String wardriverSsid = "Piglet-WARDRIVE";
  String wardriverPsk  = "wardrive1234";
  uint32_t gpsBaud     = 9600;
  uint32_t gpsCacheMinutes = GPS_CACHE_DEFAULT_MINUTES;
  String scanMode      = "aggressive"; // aggressive | powersaving
  String scanProfile   = SCAN_PROFILE_DEFAULT; // original | kg | custom
  uint8_t wifi24ChannelCount = 3;
  uint8_t wifi24Channels[WIFI24_CHANNEL_MAX_COUNT] = {1, 6, 11};
  uint8_t wifi5ChannelCount = 4;
  uint8_t wifi5Channels[WIFI5_CHANNEL_MAX_COUNT] = {36, 40, 44, 48};
  uint16_t wifi24DwellMs = WIFI24_DWELL_KG_RECOMMENDED_MS; // 0 = use scanMode-derived dwell
  uint16_t wifi5DwellMs = WIFI5_DWELL_KG_RECOMMENDED_MS;  // 0 = use scanMode-derived dwell
  bool bleEnabled = true;
  uint16_t bleScanDurationMs = BLE_SCAN_DURATION_DEFAULT_MS;
  uint16_t bleEveryNCycles = BLE_EVERY_N_CYCLES_DEFAULT;
  String board = "auto"; // auto | s3 | c5 | c6 | c3 | exp  (pins selected at boot; reboot required after change)
  String speedUnits  = "kmh"; // kmh | mph
  int battPin        = -1;    // GPIO for battery voltage ADC (-1 = disabled). Expects 1:2 voltage divider from LiPo.
  bool batteryTest   = false; // Enable battery test (logs elapsed time to /battery_test.csv)
  
  // Boot auto-upload limit:
  //  -1 = upload ALL files at boot (no limit)
  //   0 = disabled (no auto-upload at boot)
  //  1+ = upload up to N files at boot (WiGLE allows 25 API calls/day)
  // IMPORTANT: Requires PSRAM enabled in Arduino IDE for reliable TLS connections.
  int maxBootUploads = 25;

  // WDGoWars API key from https://wdgwars.pl/profile -> "Generate API key".
  // If set, CSVs are uploaded to WDGoWars BEFORE WiGLE at every boot.
  // Leave empty to disable WDGoWars uploads.
  String wdgwarsApiKey;

  // Optional device name — appended to WiGLE CSV header and filename so
  // multiple Piglets uploading to the same account can be distinguished.
  // E.g. deviceName=rover1  →  device=Piglet-rover1  /  rover1_Piglet_WiGLE_....csv
  // Leave empty for default ("Piglet-Wardriver" / "WiGLE_....csv").
  String deviceName;

  // Auto-start mesh mode after boot uploads: core, node, or none (default).
  // core — become the mesh coordinator (receives wardriving records from nodes).
  // node — become a scanning node that forwards records to the Core.
  // none — normal solo wardriving mode.
  String meshModeOnBoot = "none";

  // Rotate the OLED display 180° (true = upside-down mount, false = normal).
  // Requires reboot to take effect.
  bool rotateScreen180 = false;

  // When true: after boot uploads complete, disconnect from home WiFi and
  // begin wardriving immediately instead of staying on the STA connection.
  // The web UI is still reachable if you connect to the Wardriver AP later,
  // but the device will not hold the STA link open. Requires reboot.
  bool autoStartAfterUpload = false;

  // Retention for CSV files already moved to /uploaded.
  // Pending /logs files are never affected.
  bool autoDeleteUploadedLogs = true;
  uint16_t uploadedLogsToKeep = UPLOADED_LOGS_TO_KEEP_DEFAULT;
};

const PinMap& detectPinsByChip();
PinMap pickPinsFromConfig();
bool wardriverIsC5();

String trimCopy(String s);
bool parseKeyValueLine(const String& lineIn, String& keyOut, String& valOut);
void cfgAssignKV(const String& k, const String& v);
void validateConfig();
bool wigleConfigured();
bool wdgwarsConfigured();
bool loadConfigFromSD();
bool saveConfigToSD();
