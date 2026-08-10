#include "Config.h"
#include "Globals.h"
#include <SD.h>
#include <ArduinoJson.h>

#if __has_include(<esp_chip_info.h>)
  #include <esp_chip_info.h>
#else
  #include <esp_system.h>
#endif

static const char* CFG_PATH = "/wardriver.cfg";

// ---------------- Board / Pin detection ----------------

const PinMap& detectPinsByChip() {
  String m = ESP.getChipModel();  // e.g. "ESP32-C5"
  Serial.print("[CHIP] Model: ");
  Serial.println(m);

  m.toUpperCase();

  if (m.indexOf("C5") >= 0) return PINS_C5;
  if (m.indexOf("C6") >= 0) return PINS_C6;
  if (m.indexOf("C3") >= 0) return PINS_XIAO_C3;
  if (m.indexOf("S3") >= 0) return PINS_S3;

  // fallback
  return PINS_S3;
}

PinMap pickPinsFromConfig() {
  // cfg.board is expected to be: "auto" | "s3" | "c6" (lowercase)
  if (cfg.board == "s3")  return PINS_S3;
  if (cfg.board == "exp") return PINS_S3_EXP_BASE;
  if (cfg.board == "c6")  return PINS_C6;
  if (cfg.board == "c5")  return PINS_C5;
  if (cfg.board == "c3")  return PINS_XIAO_C3;
  return detectPinsByChip(); // "auto" or anything else
}

// True when THIS wardriver hardware is configured as ESP32-C5 (5GHz capable)
bool wardriverIsC5() {
  // Explicit config override wins
  if (cfg.board == "c5") return true;
  if (cfg.board == "c6" || cfg.board == "s3" || cfg.board == "exp") return false;

  // Auto mode: infer from the active pinmap name (set during setup())
  String n = String(pins.name);
  n.toUpperCase();
  return (n.indexOf("C5") >= 0);
}

// ---------------- Parsing helpers ----------------

String trimCopy(String s) {
  s.trim();
  return s;
}

bool parseKeyValueLine(const String& lineIn, String& keyOut, String& valOut) {
  String line = lineIn;
  line.trim();
  if (line.length() == 0) return false;
  if (line[0] == '#') return false;           // comment
  if (line.startsWith("//")) return false;    // comment

  int eq = line.indexOf('=');
  if (eq < 0) return false;

  String k = line.substring(0, eq);
  String v = line.substring(eq + 1);

  k.trim();
  v.trim();

  // allow quoted values
  if (v.length() >= 2 && ((v[0] == '"' && v[v.length()-1] == '"') || (v[0] == '\'' && v[v.length()-1] == '\''))) {
    v = v.substring(1, v.length()-1);
    v.trim();
  }

  if (k.length() == 0) return false;
  keyOut = k;
  valOut = v;
  return true;
}

static bool parseUint32Strict(const String& raw, uint32_t& out) {
  String s = raw;
  s.trim();
  if (s.length() == 0) return false;

  uint64_t acc = 0;
  for (uint16_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c < '0' || c > '9') return false;
    acc = acc * 10ULL + (uint64_t)(c - '0');
    if (acc > 4294967295ULL) return false;
  }

  out = (uint32_t)acc;
  return true;
}

static bool isValid5GHzChannel(uint32_t ch) {
  static const uint8_t valid[] = {
    36, 40, 44, 48,
    52, 56, 60, 64,
    100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
    149, 153, 157, 161, 165, 169, 173, 177
  };

  for (uint8_t i = 0; i < (uint8_t)sizeof(valid); i++) {
    if (ch == valid[i]) return true;
  }
  return false;
}

static bool channelListContains(const uint8_t* channels, uint8_t count, uint8_t ch) {
  for (uint8_t i = 0; i < count; i++) {
    if (channels[i] == ch) return true;
  }
  return false;
}

static bool parseChannelListStrict(const String& raw, bool fiveGHz,
                                   uint8_t* out, uint8_t maxCount,
                                   uint8_t& outCount) {
  String s = raw;
  s.trim();
  outCount = 0;
  if (s.length() == 0) return true;

  int pos = 0;
  while (pos <= s.length()) {
    int comma = s.indexOf(',', pos);
    if (comma < 0) comma = s.length();

    String token = s.substring(pos, comma);
    token.trim();
    if (token.length() == 0) {
      outCount = 0;
      return false;
    }

    uint32_t value = 0;
    if (!parseUint32Strict(token, value)) {
      outCount = 0;
      return false;
    }

    bool valid = fiveGHz ? isValid5GHzChannel(value) : (value >= 1 && value <= 14);
    if (!valid) {
      outCount = 0;
      return false;
    }

    uint8_t ch = (uint8_t)value;
    if (!channelListContains(out, outCount, ch)) {
      if (outCount >= maxCount) {
        outCount = 0;
        return false;
      }
      out[outCount++] = ch;
    }

    pos = comma + 1;
    if (comma == s.length()) break;
  }

  return true;
}

static void copyChannelList(const uint8_t* src, uint8_t count,
                            uint8_t* dst, uint8_t& dstCount) {
  dstCount = count;
  for (uint8_t i = 0; i < count; i++) {
    dst[i] = src[i];
  }
}

static void writeChannelList(File& f, const uint8_t* channels, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    if (i > 0) f.print(",");
    f.print(channels[i]);
  }
  f.println();
}

void cfgAssignKV(const String& k, const String& v) {
  if (k == "wigleBasicToken") cfg.wigleBasicToken = v;
  else if (k == "homeSsid")   cfg.homeSsid = v;
  else if (k == "homePsk")    cfg.homePsk = v;
  else if (k == "wardriverSsid") cfg.wardriverSsid = v;
  else if (k == "wardriverPsk")  cfg.wardriverPsk = v;
  else if (k == "gpsBaud") {
    uint32_t b = (uint32_t)v.toInt();
    if (b > 0) cfg.gpsBaud = b;
  }
  else if (k == "gpsCacheMinutes") {
    uint32_t n = 0;
    if (parseUint32Strict(v, n) &&
        n >= GPS_CACHE_MIN_MINUTES &&
        n <= GPS_CACHE_MAX_MINUTES) {
      cfg.gpsCacheMinutes = n;
    }
  }
  else if (k == "scanMode") {
    if (v == "aggressive" || v == "powersaving") cfg.scanMode = v;
  }
  else if (k == "wifi24Channels") {
    uint8_t parsed[WIFI24_CHANNEL_MAX_COUNT] = {};
    uint8_t count = 0;
    if (parseChannelListStrict(v, false, parsed, WIFI24_CHANNEL_MAX_COUNT, count)) {
      copyChannelList(parsed, count, cfg.wifi24Channels, cfg.wifi24ChannelCount);
    } else {
      cfg.wifi24ChannelCount = 0;
      Serial.println("[CFG] Invalid wifi24Channels ignored");
    }
  }
  else if (k == "wifi5Channels") {
    uint8_t parsed[WIFI5_CHANNEL_MAX_COUNT] = {};
    uint8_t count = 0;
    if (parseChannelListStrict(v, true, parsed, WIFI5_CHANNEL_MAX_COUNT, count)) {
      copyChannelList(parsed, count, cfg.wifi5Channels, cfg.wifi5ChannelCount);
    } else {
      cfg.wifi5ChannelCount = 0;
      Serial.println("[CFG] Invalid wifi5Channels ignored");
    }
  }
  else if (k == "board") {
    String vv = v; vv.toLowerCase();
    if (vv == "auto" || vv == "s3" || vv == "exp" || vv == "c5" || vv == "c6" || vv == "c3") cfg.board = vv;
  }
  else if (k == "speedUnits") {
    String vv = v; vv.toLowerCase();
    if (vv == "kmh" || vv == "mph") cfg.speedUnits = vv;
  }
  else if (k == "battPin") {
    int p = v.toInt();
    cfg.battPin = (v == "0") ? 0 : (p > 0 ? p : -1);  // allow GPIO 0; treat non-numeric as disabled
  }
  else if (k == "batteryTest") {
    String vv = v; vv.toLowerCase();
    cfg.batteryTest = (vv == "true" || vv == "1");
  }
  else if (k == "maxBootUploads") {
    int n = v.toInt();
    if (n >= -1) cfg.maxBootUploads = n;  // -1=all, 0=disabled, 1+=limited
  }
  else if (k == "wdgwarsApiKey")    cfg.wdgwarsApiKey = v;
  else if (k == "deviceName")       cfg.deviceName = v;
  else if (k == "meshModeOnBoot") {
    String vv = v; vv.toLowerCase();
    if (vv == "core" || vv == "node" || vv == "none") cfg.meshModeOnBoot = vv;
  }
  else if (k == "rotateScreen180") {
    String vv = v; vv.toLowerCase();
    cfg.rotateScreen180 = (vv == "true" || vv == "1");
  }
  else if (k == "autoStartAfterUpload") {
    String vv = v; vv.toLowerCase();
    cfg.autoStartAfterUpload = (vv == "true" || vv == "1");
  }
}

void validateConfig() {
  if (cfg.gpsCacheMinutes < GPS_CACHE_MIN_MINUTES)
    cfg.gpsCacheMinutes = GPS_CACHE_DEFAULT_MINUTES;
  if (cfg.gpsCacheMinutes > GPS_CACHE_MAX_MINUTES)
    cfg.gpsCacheMinutes = GPS_CACHE_MAX_MINUTES;
}

// ---------------- Load / Save ----------------

bool loadConfigFromSD() {
  Serial.println("[CFG] loadConfigFromSD() start");
  if (!sdOk) {
    Serial.println("[CFG] SD not OK, skipping config load");
    return false;
  }

  // Prefer new plain-text config
  if (SD.exists(CFG_PATH)) {
    File f = SD.open(CFG_PATH, FILE_READ);
    if (!f) {
      Serial.println("[CFG] Failed to open /wardriver.cfg for read");
      return false;
    }

    while (f.available()) {
      String line = f.readStringUntil('\n');
      String k, v;
      if (parseKeyValueLine(line, k, v)) {
        cfgAssignKV(k, v);
      }
    }
    f.close();

    validateConfig();

    Serial.println("[CFG] Loaded config from /wardriver.cfg:");
    Serial.print("      wardriverSsid: "); Serial.println(cfg.wardriverSsid);
    Serial.print("      wardriverPsk:  "); Serial.println(cfg.wardriverPsk.length() ? "(set)" : "(empty)");
    Serial.print("      homeSsid:      "); Serial.println(cfg.homeSsid);
    Serial.print("      homePsk:       "); Serial.println(cfg.homePsk.length() ? "(set)" : "(empty)");
    Serial.print("      gpsBaud:       "); Serial.println(cfg.gpsBaud);
    Serial.print("      gpsCacheMin:   "); Serial.println(cfg.gpsCacheMinutes);
    Serial.print("      scanMode:      "); Serial.println(cfg.scanMode);
    Serial.print("      wifi24Ch:      "); Serial.println(cfg.wifi24ChannelCount);
    Serial.print("      wifi5Ch:       "); Serial.println(cfg.wifi5ChannelCount);
    Serial.print("      wigle token:   "); Serial.println(cfg.wigleBasicToken.length() ? "(set)" : "(empty)");
    return true;
  }

  // Backward compat: if old JSON exists, load it once, then save as CFG
  if (SD.exists("/wardriver.json")) {
    Serial.println("[CFG] Found legacy /wardriver.json, importing once -> /wardriver.cfg");

    File f = SD.open("/wardriver.json", FILE_READ);
    if (!f) {
      Serial.println("[CFG] Failed to open /wardriver.json for read");
      return false;
    }

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      Serial.print("[CFG] JSON parse error: ");
      Serial.println(err.c_str());
      return false;
    }

    cfg.wigleBasicToken = doc["wigleBasicToken"] | "";
    cfg.homeSsid        = doc["homeSsid"]        | "";
    cfg.homePsk         = doc["homePsk"]         | "";
    cfg.wardriverSsid   = doc["wardriverSsid"]   | cfg.wardriverSsid;
    cfg.wardriverPsk    = doc["wardriverPsk"]    | cfg.wardriverPsk;
    cfg.gpsBaud         = doc["gpsBaud"]         | cfg.gpsBaud;
    cfg.scanMode        = doc["scanMode"]        | cfg.scanMode;

    validateConfig();

    // Save as new format
    bool ok = saveConfigToSD();
    Serial.println(ok ? "[CFG] Legacy JSON imported + saved to CFG" : "[CFG] Legacy JSON imported but CFG save failed");
    return ok;
  }

  Serial.println("[CFG] No /wardriver.cfg (or legacy json). Using defaults.");
  return false;
}

bool saveConfigToSD() {
  Serial.println("[CFG] saveConfigToSD() start");
  if (!sdOk) {
    Serial.println("[CFG] SD not OK, cannot save config");
    return false;
  }

  validateConfig();

  // Remove old file if present
  if (SD.exists(CFG_PATH)) {
    if (!SD.remove(CFG_PATH)) {
      Serial.println("[CFG] WARNING: failed to remove existing /wardriver.cfg");
    }
  }

  File f = SD.open(CFG_PATH, FILE_WRITE);
  if (!f) {
    Serial.println("[CFG] Failed to open /wardriver.cfg for write");
    return false;
  }

  // Simple key=value format (human editable)
  f.println("# Piglet Wardriver config (key=value)");
  f.println("# Lines starting with # are comments");
  f.println("");

  f.println("# WiGLE API 'Encoded for use' token from wigle.net/account");
  f.print("wigleBasicToken="); f.println(cfg.wigleBasicToken);
  f.println("");

  f.println("# Home Wi-Fi credentials (for STA connection and uploads)");
  f.print("homeSsid=");        f.println(cfg.homeSsid);
  f.print("homePsk=");         f.println(cfg.homePsk);
  f.println("");

  f.println("# Device Access Point credentials (for web UI when home WiFi unavailable)");
  f.print("wardriverSsid=");   f.println(cfg.wardriverSsid);
  f.print("wardriverPsk=");    f.println(cfg.wardriverPsk);
  f.println("");

  f.println("# GPS UART baud rate (default: 9600)");
  f.print("gpsBaud=");         f.println(cfg.gpsBaud);
  f.println("");

  f.println("# GPS cache timeout in minutes (default: 3, valid range: 1-10080)");
  f.println("# Reuses the last valid position while GPS is lost; long values can stamp old locations.");
  f.print("gpsCacheMinutes="); f.println(cfg.gpsCacheMinutes);
  f.println("");

  f.println("# Scan interval: aggressive (4.5s) or powersaving (12s)");
  f.print("scanMode=");        f.println(cfg.scanMode);
  f.println("");

  f.println("# Optional solo scan channel lists. Leave empty for original all-channel scanning.");
  f.println("# wifi24Channels accepts 1-14. wifi5Channels is used only on ESP32-C5.");
  f.print("wifi24Channels=");  writeChannelList(f, cfg.wifi24Channels, cfg.wifi24ChannelCount);
  f.print("wifi5Channels=");   writeChannelList(f, cfg.wifi5Channels, cfg.wifi5ChannelCount);
  f.println("");

  f.println("# Speed display: kmh or mph");
  f.print("speedUnits=");     f.println(cfg.speedUnits);
  f.println("");

  f.println("# Board type: auto, s3, c5, c6, exp (requires reboot)");
  f.print("board=");          f.println(cfg.board);
  f.println("");

  f.println("# Battery voltage ADC GPIO (-1 = disabled, requires 1:2 voltage divider)");
  f.print("battPin=");        f.println(cfg.battPin);
  f.println("");

  f.println("# Enable battery test (logs elapsed time to /battery_test.csv): true or false");
  f.print("batteryTest=");    f.println(cfg.batteryTest ? "true" : "false");
  f.println("");

  f.println("# Max CSV files to auto-upload at boot (-1=all, 0=disabled, 1-25=limited)");
  f.print("maxBootUploads="); f.println(cfg.maxBootUploads);

  f.println("");
  f.println("# WDGoWars API key from https://wdgwars.pl/profile (leave empty to disable)");
  f.print("wdgwarsApiKey="); f.println(cfg.wdgwarsApiKey);

  f.println("# Device name — identifies this Piglet in WiGLE headers and filenames.");
  f.println("# Alphanumeric + _ - only.  E.g.: rover1  backpack  car");
  f.print("deviceName="); f.println(cfg.deviceName);

  f.println("");
  f.println("# Mesh mode on boot: Core, Node, or None (default).");
  f.println("# Core  — become the mesh coordinator after uploads complete.");
  f.println("# Node  — become a scanning mesh node after uploads complete.");
  f.println("# None  — normal solo wardriving (default).");
  f.print("meshModeOnBoot="); f.println(cfg.meshModeOnBoot);

  f.println("");
  f.println("# Rotate screen 180 degrees (true = upside-down mount). Requires reboot.");
  f.print("rotateScreen180="); f.println(cfg.rotateScreen180 ? "true" : "false");

  f.println("");
  f.println("# Disconnect from home WiFi after boot uploads and start wardriving immediately.");
  f.println("# true = wardrive right after uploads complete (headless mode).");
  f.println("# false = stay on home WiFi, keep web UI accessible (default).");
  f.print("autoStartAfterUpload="); f.println(cfg.autoStartAfterUpload ? "true" : "false");

  f.flush();
  f.close();

  Serial.println("[CFG] Saved /wardriver.cfg OK");
  return true;
}
