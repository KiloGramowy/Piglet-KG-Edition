#include "StartupGpsBackfill.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <string.h>

#include "Globals.h"
#include "SDUtils.h"

#ifndef FILE_APPEND
  #define FILE_APPEND FILE_WRITE
#endif

static const char* STARTUP_GPS_PENDING_PATH = "/startup_gps.pending";
static const uint8_t STARTUP_GPS_REPLAY_BATCH = 20;

enum StartupGpsBackfillState : uint8_t {
  STARTUP_GPS_WAITING_FOR_FIRST_FIX,
  STARTUP_GPS_CLOSING_STARTUP,
  STARTUP_GPS_REPLAYING,
  STARTUP_GPS_NORMAL
};

static StartupGpsBackfillState startupGpsState = STARTUP_GPS_WAITING_FOR_FIRST_FIX;
static bool replayStartedState = false;
static bool storageErrorLogged = false;
static File pendingWriteFile;
static File pendingReadFile;
static StartupGpsBackfillSnapshot firstFixSnapshot;
static uint32_t pendingCountState = 0;
static uint32_t replayWrittenCount = 0;
static uint32_t replayFailedCount = 0;
static bool closeoutWaitingLogged = false;
static bool closeoutLastWifiBusy = false;
static bool closeoutLastBleBusy = false;
static uint16_t closeoutLastBlePending = 0;
static uint32_t closeoutLastLogMs = 0;

bool startupGpsBackfillFixSeen() {
  return startupGpsState != STARTUP_GPS_WAITING_FOR_FIRST_FIX;
}

bool startupGpsBackfillAcceptingPending() {
  return startupGpsState == STARTUP_GPS_WAITING_FOR_FIRST_FIX ||
         startupGpsState == STARTUP_GPS_CLOSING_STARTUP;
}

bool startupGpsBackfillCloseoutActive() {
  return startupGpsState == STARTUP_GPS_CLOSING_STARTUP;
}

bool startupGpsBackfillReplayActive() {
  return startupGpsState == STARTUP_GPS_REPLAYING;
}

bool startupGpsBackfillScannerBlocked() {
  return startupGpsState == STARTUP_GPS_CLOSING_STARTUP ||
         startupGpsState == STARTUP_GPS_REPLAYING;
}

uint32_t startupGpsBackfillPendingCount() {
  return pendingCountState;
}

static void logStorageError(const char* detail) {
  if (storageErrorLogged) return;
  storageErrorLogged = true;
  Serial.print("[KG-GPS] ERROR startup pending store unavailable");
  if (detail && detail[0] != '\0') {
    Serial.print(": ");
    Serial.print(detail);
  }
  Serial.println();
}

void startupGpsBackfillBeginSession() {
  startupGpsState = STARTUP_GPS_WAITING_FOR_FIRST_FIX;
  replayStartedState = false;
  storageErrorLogged = false;
  pendingCountState = 0;
  replayWrittenCount = 0;
  replayFailedCount = 0;
  closeoutWaitingLogged = false;
  closeoutLastWifiBusy = false;
  closeoutLastBleBusy = false;
  closeoutLastBlePending = 0;
  closeoutLastLogMs = 0;
  firstFixSnapshot = {};

  if (pendingWriteFile) pendingWriteFile.close();
  if (pendingReadFile) pendingReadFile.close();

  if (sdOk && SD.exists(STARTUP_GPS_PENDING_PATH)) {
    if (SD.remove(STARTUP_GPS_PENDING_PATH)) {
      Serial.println("[KG-GPS] stale startup pending removed");
    } else {
      Serial.println("[KG-GPS] ERROR stale startup pending remove failed");
    }
  }

  Serial.println("[KG-GPS] startup backfill armed");
}

static bool ensurePendingWriteFile() {
  if (!sdOk) {
    logStorageError("SD not OK");
    return false;
  }

  if (pendingWriteFile) return true;

  pendingWriteFile = SD.open(STARTUP_GPS_PENDING_PATH, FILE_APPEND);
  if (!pendingWriteFile) {
    logStorageError("open failed");
    return false;
  }

  return true;
}

static bool writePendingRecord(DynamicJsonDocument& doc) {
  if (!startupGpsBackfillAcceptingPending()) return false;
  if (!ensurePendingWriteFile()) return false;

  size_t jsonBytes = serializeJson(doc, pendingWriteFile);
  size_t newlineBytes = pendingWriteFile.println();
  if (jsonBytes == 0 || newlineBytes == 0) {
    logStorageError("write failed");
    return false;
  }

  pendingCountState++;
  if (pendingCountState == 1 || (pendingCountState % 50) == 0) {
    Serial.printf("[KG-GPS] startup pending queued=%lu\n",
                  (unsigned long)pendingCountState);
  }
  if ((pendingCountState % 25) == 0) pendingWriteFile.flush();
  return true;
}

bool startupGpsBackfillQueueWifi(const String& mac, const String& ssid,
                                 const String& auth, const String& firstSeen,
                                 int channel, int rssi) {
  DynamicJsonDocument doc(512);
  doc["type"] = "WIFI";
  doc["mac"] = mac;
  doc["ssid"] = ssid;
  doc["auth"] = auth;
  doc["first"] = firstSeen;
  doc["ch"] = channel;
  doc["rssi"] = rssi;
  return writePendingRecord(doc);
}

bool startupGpsBackfillQueueBle(const BleObservation& obs, const String& firstSeen) {
  DynamicJsonDocument doc(640);
  doc["type"] = "BLE";
  doc["mac"] = obs.addr;
  doc["name"] = obs.name;
  doc["first"] = firstSeen;
  doc["ch"] = obs.channel;
  doc["rssi"] = obs.rssi;
  doc["addrType"] = obs.addrType;
  doc["rcois"] = obs.serviceUuids;
  doc["mfgr"] = obs.mfgrId;
  return writePendingRecord(doc);
}

void startupGpsBackfillCaptureFirstFix(double lat, double lon, double altM, double accM) {
  if (startupGpsState != STARTUP_GPS_WAITING_FOR_FIRST_FIX) return;

  firstFixSnapshot.lat = lat;
  firstFixSnapshot.lon = lon;
  firstFixSnapshot.altM = altM;
  firstFixSnapshot.accM = accM;
  startupGpsState = STARTUP_GPS_CLOSING_STARTUP;
  closeoutWaitingLogged = false;
  closeoutLastLogMs = 0;

  Serial.printf("[KG-GPS] first fix captured lat=%.6f lon=%.6f accuracy=%.1f pending=%lu\n",
                lat, lon, accM, (unsigned long)pendingCountState);
  Serial.println("[KG-GPS] startup closeout begin");
}

void startupGpsBackfillCompleteCloseout() {
  if (startupGpsState != STARTUP_GPS_CLOSING_STARTUP) return;

  if (pendingWriteFile) {
    pendingWriteFile.flush();
    pendingWriteFile.close();
  }

  Serial.printf("[KG-GPS] startup closeout complete pending=%lu\n",
                (unsigned long)pendingCountState);

  if (sdOk && pendingCountState > 0 && SD.exists(STARTUP_GPS_PENDING_PATH)) {
    startupGpsState = STARTUP_GPS_REPLAYING;
    replayStartedState = false;
    replayWrittenCount = 0;
    replayFailedCount = 0;
  } else if (pendingCountState > 0) {
    Serial.println("[KG-GPS] ERROR startup pending missing at closeout");
    startupGpsState = STARTUP_GPS_NORMAL;
  } else if (sdOk && SD.exists(STARTUP_GPS_PENDING_PATH)) {
    SD.remove(STARTUP_GPS_PENDING_PATH);
    startupGpsState = STARTUP_GPS_NORMAL;
  } else {
    startupGpsState = STARTUP_GPS_NORMAL;
  }
}

void startupGpsBackfillNoteCloseoutWaiting(bool wifiBusy, bool bleBusy, uint16_t blePending) {
  if (startupGpsState != STARTUP_GPS_CLOSING_STARTUP) return;

  uint32_t nowMs = millis();
  bool changed = !closeoutWaitingLogged ||
                 wifiBusy != closeoutLastWifiBusy ||
                 bleBusy != closeoutLastBleBusy ||
                 blePending != closeoutLastBlePending;

  if (!changed && (nowMs - closeoutLastLogMs) < 2000) return;

  closeoutWaitingLogged = true;
  closeoutLastWifiBusy = wifiBusy;
  closeoutLastBleBusy = bleBusy;
  closeoutLastBlePending = blePending;
  closeoutLastLogMs = nowMs;

  Serial.printf("[KG-GPS] startup closeout waiting wifi=%u ble=%u blePending=%u\n",
                wifiBusy ? 1 : 0,
                bleBusy ? 1 : 0,
                blePending);
}

static void copyJsonString(char* dst, size_t dstSize, const char* src) {
  if (!dst || dstSize == 0) return;
  if (!src) src = "";
  snprintf(dst, dstSize, "%s", src);
}

static bool ensureLogFileForReplay() {
  if (!sdOk) return false;
  if (logFile) return true;
  return openLogFile();
}

static bool replayWifiRecord(DynamicJsonDocument& doc) {
  if (!ensureLogFileForReplay()) return false;

  String mac = doc["mac"] | "";
  String ssid = doc["ssid"] | "";
  String auth = doc["auth"] | "UNKNOWN";
  String firstSeen = doc["first"] | "";
  int channel = doc["ch"] | 0;
  int rssi = doc["rssi"] | 0;

  appendWigleRow(mac, ssid, auth, firstSeen, channel, rssi,
                 firstFixSnapshot.lat, firstFixSnapshot.lon,
                 firstFixSnapshot.altM, firstFixSnapshot.accM);
  return sdOk;
}

static bool replayBleRecord(DynamicJsonDocument& doc) {
  if (!ensureLogFileForReplay()) return false;

  BleObservation obs = {};
  copyJsonString(obs.addr, sizeof(obs.addr), doc["mac"] | "");
  copyJsonString(obs.name, sizeof(obs.name), doc["name"] | "");
  copyJsonString(obs.serviceUuids, sizeof(obs.serviceUuids), doc["rcois"] | "");
  obs.addrType = doc["addrType"] | 0;
  obs.mfgrId = doc["mfgr"] | 0;
  obs.rssi = doc["rssi"] | 0;
  obs.channel = doc["ch"] | 0;
  obs.observedAtMs = millis();

  String firstSeen = doc["first"] | "";
  return appendBleRow(obs, firstSeen,
                      firstFixSnapshot.lat, firstFixSnapshot.lon,
                      firstFixSnapshot.altM, firstFixSnapshot.accM);
}

static bool replayPendingLine(const String& line) {
  if (line.length() == 0) return true;

  DynamicJsonDocument doc(768);
  DeserializationError err = deserializeJson(doc, line);
  if (err) return false;

  const char* type = doc["type"] | "";
  if (strcmp(type, "WIFI") == 0) return replayWifiRecord(doc);
  if (strcmp(type, "BLE") == 0) return replayBleRecord(doc);
  return false;
}

static void finishReplay() {
  if (pendingReadFile) pendingReadFile.close();
  if (logFile) logFile.flush();

  if (sdOk && SD.exists(STARTUP_GPS_PENDING_PATH) && !SD.remove(STARTUP_GPS_PENDING_PATH)) {
    Serial.println("[KG-GPS] ERROR startup pending remove failed after replay");
  }

  startupGpsState = STARTUP_GPS_NORMAL;
  replayStartedState = false;
  pendingCountState = 0;

  Serial.printf("[KG-GPS] startup replay complete written=%lu failed=%lu\n",
                (unsigned long)replayWrittenCount,
                (unsigned long)replayFailedCount);
}

void startupGpsBackfillTick() {
  if (startupGpsState != STARTUP_GPS_REPLAYING) return;

  if (!replayStartedState) {
    if (!sdOk) {
      Serial.println("[KG-GPS] ERROR startup replay skipped: SD not OK");
      startupGpsState = STARTUP_GPS_NORMAL;
      return;
    }

    pendingReadFile = SD.open(STARTUP_GPS_PENDING_PATH, FILE_READ);
    if (!pendingReadFile) {
      Serial.println("[KG-GPS] ERROR startup replay open failed");
      startupGpsState = STARTUP_GPS_NORMAL;
      return;
    }

    replayStartedState = true;
    Serial.printf("[KG-GPS] startup replay begin pending=%lu\n",
                  (unsigned long)pendingCountState);
  }

  uint8_t processed = 0;
  while (pendingReadFile && pendingReadFile.available() && processed < STARTUP_GPS_REPLAY_BATCH) {
    String line = pendingReadFile.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) {
      processed++;
      continue;
    }
    if (replayPendingLine(line)) replayWrittenCount++;
    else replayFailedCount++;
    processed++;
  }

  if (!pendingReadFile || !pendingReadFile.available()) finishReplay();
}
