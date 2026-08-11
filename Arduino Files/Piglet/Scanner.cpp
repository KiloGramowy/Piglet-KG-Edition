#include "Scanner.h"
#include "Globals.h"
#include "Config.h"
#include "GPS.h"
#include "SDUtils.h"
#include "StartupGpsBackfill.h"

static String authModeToString(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPAWPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2EAP";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2WPA3";
    default: return "UNKNOWN";
  }
}

// Last-known GPS position — used when fix is temporarily lost so networks
// aren't logged at 0,0 (null island).
// Updated every loop() iteration (not just on scan) so position stays current
// even when driving through areas with no networks.
// Quality-gated: requires HDOP ≤ 10 and ≥ 3 satellites to prevent a brief
// low-quality re-acquisition from overwriting a good cached position.
bool     lastGpsValid   = false;
double   lastLat = 0, lastLon = 0, lastAlt = 0, lastAcc = 0;
uint32_t lastGpsValidMs = 0;          // millis() when position was last cached
static uint32_t completedWifiCycles = 0;
static uint32_t customLastCycleCompleteMs = 0;
static bool     customScanInProgress = false;
static uint8_t  customChannelIndex = 0;
static uint8_t  customActiveChannel = 0;
static uint8_t  customFailureCount = 0;
static uint32_t normalLastScanStartMs = 0;
static bool     normalScanInProgress = false;
static uint8_t  normalZeroScanCount = 0;

static uint32_t gpsCacheTimeoutMs() {
  uint32_t minutes = cfg.gpsCacheMinutes;
  if (minutes < GPS_CACHE_MIN_MINUTES) minutes = GPS_CACHE_DEFAULT_MINUTES;
  if (minutes > GPS_CACHE_MAX_MINUTES) minutes = GPS_CACHE_MAX_MINUTES;

  uint64_t ms = (uint64_t)minutes * 60ULL * 1000ULL;
  return (ms > 4294967295ULL) ? 4294967295UL : (uint32_t)ms;
}

uint32_t wifiScanCompletedCycles() {
  return completedWifiCycles;
}

GpsLogSnapshot captureGpsLogSnapshot() {
  GpsLogSnapshot snap;
  if (gpsHasFix) {
    snap.lat = gps.location.lat();
    snap.lon = gps.location.lng();
    snap.altM = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
    snap.accM = gps.hdop.isValid() ? gps.hdop.hdop() : 0.0;
    snap.usedFix = true;
  } else if (lastGpsValid && (millis() - lastGpsValidMs) <= gpsCacheTimeoutMs()) {
    snap.lat = lastLat;
    snap.lon = lastLon;
    snap.altM = lastAlt;
    snap.accM = lastAcc;
    snap.usedCache = true;
  }
  return snap;
}

// ---- Result processor (shared between sync and async paths) ----
static void processScanResults(int n) {
  if (n <= 0) { WiFi.scanDelete(); return; }

  String firstSeen = iso8601NowUTC();
  bool startupQueue = startupGpsBackfillAcceptingPending();
  double lat = 0, lon = 0, altM = 0, accM = 0;
  if (!startupQueue && gpsHasFix) {
    lat  = gps.location.lat();
    lon  = gps.location.lng();
    altM = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
    accM = gps.hdop.isValid()     ? gps.hdop.hdop()       : 0.0;
    // lastLat/lastLon is maintained by loop() — no update here.
  } else if (!startupQueue && lastGpsValid && (millis() - lastGpsValidMs) <= gpsCacheTimeoutMs()) {
    // Use last-known position (quality-gated, configurable expiry) until fix returns
    lat = lastLat; lon = lastLon; altM = lastAlt; accM = lastAcc;
  }

  uint32_t wrote = 0;
  for (int i = 0; i < n; i++) {
    int ch = WiFi.channel(i);
    bool chUnknown = (ch == 0);
    bool is2g = (ch >= 1 && ch <= 14) || chUnknown;
    bool is5g = (ch >= 32 && ch <= 177);

    if (!is2g) {
      if (!(wardriverIsC5() && is5g)) continue;
    }

    String ssid   = WiFi.SSID(i);
    String mac    = WiFi.BSSIDstr(i);
    int    rssi   = WiFi.RSSI(i);
    String authStr = authModeToString(WiFi.encryptionType(i));

    if (is2g) networksFound2G++;
    else      networksFound5G++;

    if (startupQueue) {
      if (startupGpsBackfillQueueWifi(mac, ssid, authStr, firstSeen, ch, rssi)) wrote++;
    } else {
      appendWigleRow(mac, ssid, authStr, firstSeen, ch, rssi, lat, lon, altM, accM);
      wrote++;
    }
  }

  WiFi.scanDelete();

  // Force flush after each scan batch so data reaches the SD card promptly.
  // Minimises data loss if the device loses power between scan cycles.
  if (!startupQueue && wrote > 0 && sdOk && logFile) logFile.flush();

  if (!startupQueue) {
    Serial.printf("[SCAN] Wrote %lu rows\n", (unsigned long)wrote);
  }
}

static uint8_t customScanTotalChannels() {
  uint8_t total = cfg.wifi24ChannelCount;
  if (wardriverIsC5()) total += cfg.wifi5ChannelCount;
  return total;
}

static uint8_t customScanChannelAt(uint8_t idx) {
  if (idx < cfg.wifi24ChannelCount) return cfg.wifi24Channels[idx];
  return cfg.wifi5Channels[idx - cfg.wifi24ChannelCount];
}

static void logCustomScanMode(uint8_t total) {
  static uint8_t last24 = 255;
  static uint8_t last5 = 255;
  static bool lastC5 = false;
  static bool ignored5Logged = false;

  bool c5 = wardriverIsC5();
  uint8_t usable5 = c5 ? cfg.wifi5ChannelCount : 0;

  if (!c5 && cfg.wifi5ChannelCount > 0 && !ignored5Logged) {
    Serial.println("[SCAN] wifi5Channels ignored on non-C5 board");
    ignored5Logged = true;
  } else if (c5 || cfg.wifi5ChannelCount == 0) {
    ignored5Logged = false;
  }

  if (total > 0 && (cfg.wifi24ChannelCount != last24 || usable5 != last5 || c5 != lastC5)) {
    Serial.printf("[SCAN] Custom channel scheduler active (2.4=%u, 5=%u)\n",
                  cfg.wifi24ChannelCount, usable5);
    last24 = cfg.wifi24ChannelCount;
    last5 = usable5;
    lastC5 = c5;
  }
}

static void advanceCustomScanChannel(uint8_t& channelIndex, uint8_t total,
                                     uint32_t& lastCycleCompleteMs,
                                     bool countCompletedCycle) {
  channelIndex++;
  if (channelIndex >= total) {
    channelIndex = 0;
    lastCycleCompleteMs = millis();
    if (countCompletedCycle) completedWifiCycles++;
    Serial.println("[SCAN] Custom channel cycle complete");
  }
}

static void recoverCustomScanIfStuck(uint8_t& failureCount) {
  failureCount++;
  Serial.printf("[SCAN] Custom scan failed (%u)\n", failureCount);
  if (failureCount >= 3) {
    Serial.println("[SCAN] Resetting WiFi radio (custom scan stuck recovery)");
    WiFi.mode(WIFI_OFF); delay(200);
    WiFi.mode(WIFI_STA); delay(200);
    failureCount = 0;
  }
}

static uint32_t customDwellForChannel(uint8_t channel, uint32_t defaultDwellMs) {
  if (channel >= 1 && channel <= 14 &&
      cfg.wifi24DwellMs >= WIFI_DWELL_MIN_MS &&
      cfg.wifi24DwellMs <= WIFI_DWELL_MAX_MS) {
    return cfg.wifi24DwellMs;
  }
  if (channel > 14 &&
      cfg.wifi5DwellMs >= WIFI_DWELL_MIN_MS &&
      cfg.wifi5DwellMs <= WIFI_DWELL_MAX_MS) {
    return cfg.wifi5DwellMs;
  }
  return defaultDwellMs;
}

static bool serviceCustomScanInFlight(uint8_t total) {
  if (!customScanInProgress) return false;

  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return true;

  customScanInProgress = false;

  if (n == WIFI_SCAN_FAILED || n < 0) {
    WiFi.scanDelete();
    recoverCustomScanIfStuck(customFailureCount);
    if (total > 0) {
      advanceCustomScanChannel(customChannelIndex, total, customLastCycleCompleteMs, false);
    }
    return false;
  }

  customFailureCount = 0;
  Serial.printf("[SCAN] Custom channel %u complete: %d networks\n", customActiveChannel, n);
  processScanResults(n);
  if (total > 0) {
    advanceCustomScanChannel(customChannelIndex, total, customLastCycleCompleteMs, true);
  }
  return false;
}

static void doCustomChannelScan(uint32_t gapMs, uint32_t defaultDwellMs) {
  uint8_t total = customScanTotalChannels();
  if (total == 0) return;
  if (customChannelIndex >= total) customChannelIndex = 0;

  logCustomScanMode(total);

  if (customScanInProgress) {
    serviceCustomScanInFlight(total);
    return;
  }

  if (customChannelIndex == 0 && millis() - customLastCycleCompleteMs < gapMs) return;

  uint8_t channel = customScanChannelAt(customChannelIndex);
  uint32_t dwellMs = customDwellForChannel(channel, defaultDwellMs);
  int16_t rc = WiFi.scanNetworks(/*async*/true, /*show_hidden*/true,
                                 /*passive*/false, dwellMs, channel);
  if (rc == WIFI_SCAN_RUNNING || rc == 0) {
    customActiveChannel = channel;
    customScanInProgress = true;
    Serial.printf("[SCAN] Custom channel scan started (ch=%u, dwell=%lu ms)\n",
                  channel, (unsigned long)dwellMs);
  } else {
    WiFi.scanDelete();
    Serial.printf("[SCAN] Custom channel %u start failed (%d)\n", channel, rc);
    recoverCustomScanIfStuck(customFailureCount);
    advanceCustomScanChannel(customChannelIndex, total, customLastCycleCompleteMs, false);
  }
}

static bool serviceNormalScanInFlight() {
  if (!normalScanInProgress) return false;

  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return true;

  normalScanInProgress = false;
  normalLastScanStartMs = millis();

  if (n == WIFI_SCAN_FAILED || n < 0) {
    WiFi.scanDelete();
    normalZeroScanCount++;
    Serial.printf("[SCAN] Failed/empty (%u)\n", normalZeroScanCount);
    if (normalZeroScanCount >= 3) {
      Serial.println("[SCAN] Resetting WiFi radio (stuck recovery)");
      WiFi.mode(WIFI_OFF); delay(200);
      WiFi.mode(WIFI_STA); delay(200);
      normalZeroScanCount = 0;
    }
    return false;
  }

  normalZeroScanCount = 0;
  Serial.printf("[SCAN] Async complete: %d networks\n", n);
  processScanResults(n);
  completedWifiCycles++;
  return false;
}

bool wifiScanServiceInFlight() {
  bool busy = false;
  if (customScanInProgress) busy = serviceCustomScanInFlight(customScanTotalChannels()) || busy;
  if (normalScanInProgress) busy = serviceNormalScanInFlight() || busy;
  return busy || customScanInProgress || normalScanInProgress;
}

void doScanOnce() {
  // ---- Timing ----
  // aggressive:  100 ms/channel dwell, 1500 ms minimum gap between scan starts
  // powersaving: 200 ms/channel dwell, 10000 ms gap
  //
  // With 100 ms/channel the hardware finishes a 13-channel 2.4 GHz sweep in
  // ~1.3 s instead of the old ~3.9 s (default 300 ms dwell).  Using async
  // mode means that time no longer blocks the main loop — GPS parsing, the
  // web server and OLED updates all continue while the radio hops channels.
  bool powersave     = (cfg.scanMode == "powersaving");
  uint32_t gapMs     = powersave ? 10000 : 1500;
  uint32_t dwellMs   = powersave ?   200 :  100;

  if (customScanTotalChannels() > 0) {
    doCustomChannelScan(gapMs, dwellMs);
    return;
  }

  // ---- Check if the async scan launched last iteration has finished ----
  if (normalScanInProgress) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;  // still running — come back next tick

    normalScanInProgress = false;
    normalLastScanStartMs = millis();

    if (n == WIFI_SCAN_FAILED || n < 0) {
      WiFi.scanDelete();
      normalZeroScanCount++;
      Serial.printf("[SCAN] Failed/empty (%u)\n", normalZeroScanCount);
      if (normalZeroScanCount >= 3) {
        Serial.println("[SCAN] Resetting WiFi radio (stuck recovery)");
        WiFi.mode(WIFI_OFF); delay(200);
        WiFi.mode(WIFI_STA); delay(200);
        normalZeroScanCount = 0;
      }
      return;
    }

    normalZeroScanCount = 0;
    Serial.printf("[SCAN] Async complete: %d networks\n", n);
    processScanResults(n);
    completedWifiCycles++;
    return;
  }

  // ---- Wait for the minimum gap before starting the next scan ----
  if (millis() - normalLastScanStartMs < gapMs) return;

  // ---- Kick off a new async scan ----
  // async=true, show_hidden=true, passive=false, max_ms_per_chan=dwellMs
  int16_t rc = WiFi.scanNetworks(/*async*/true, /*show_hidden*/true,
                                 /*passive*/false, dwellMs);
  if (rc == WIFI_SCAN_RUNNING || rc == 0) {
    normalScanInProgress = true;
    Serial.printf("[SCAN] Async scan started (dwell=%lu ms)\n", (unsigned long)dwellMs);
  } else {
    // Shouldn’t normally happen; fall back and retry after gap
    Serial.printf("[SCAN] scanNetworks start failed (%d)\n", rc);
    normalLastScanStartMs = millis();
  }
}
