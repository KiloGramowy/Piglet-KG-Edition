#include "BleScanner.h"
#include "Config.h"
#include "Globals.h"

#if PIGLET_HAS_BLE

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <ctype.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

struct BleKey {
  char addr[18];
  uint8_t addrType;
};

struct BleDiagState {
  uint32_t initCount = 0;
  uint32_t startAttempts = 0;
  uint32_t startSuccess = 0;
  uint32_t startFailures = 0;
  uint32_t callbackCount = 0;
  uint32_t pendingAccepted = 0;
  uint32_t pendingDropped = 0;
  uint32_t uniqueAccepted = 0;
  uint32_t duplicateRejected = 0;
  uint32_t csvRowsWritten = 0;
  uint32_t startupBackfillRowsRouted = 0;
  uint32_t stopCount = 0;
  uint32_t burstStartMs = 0;
  uint32_t burstStopMs = 0;
  uint32_t burstElapsedMs = 0;
  uint32_t burstCallbacks = 0;
  uint32_t burstPendingAccepted = 0;
  uint32_t burstPendingDropped = 0;
  uint32_t burstUniqueAccepted = 0;
  uint32_t burstDuplicateRejected = 0;
  uint32_t burstCsvRowsWritten = 0;
  uint32_t burstStartupBackfillRowsRouted = 0;
  bool burstActive = false;
  bool firstCallbackLogged = false;
  bool stoppedPendingReport = false;
  bool clearResultsPending = false;
};

SemaphoreHandle_t bleMutex = nullptr;
BLEScan* bleScan = nullptr;
bool bleReady = false;
bool bleRunning = false;
uint32_t bleEndsAtMs = 0;
uint32_t bleDropped = 0;
const char* bleLastStartFailureReason = "none";

BleObservation pending[BLE_PENDING_CAPACITY];
uint16_t pendingHead = 0;
uint16_t pendingCount = 0;

BleKey dedupe[BLE_DEDUPE_CAPACITY];
uint16_t dedupeCount = 0;

BleDiagState bleDiag;

class BleLock {
public:
  BleLock() {
    if (bleMutex) held = (xSemaphoreTake(bleMutex, portMAX_DELAY) == pdTRUE);
  }
  ~BleLock() {
    if (held) xSemaphoreGive(bleMutex);
  }
  bool ok() const { return held; }

private:
  bool held = false;
};

static void copyCStringField(char* dest, size_t destLen, const char* src) {
  if (!dest || destLen == 0) return;
  if (!src) src = "";
  size_t n = strlen(src);
  if (n >= destLen) n = destLen - 1;
  memcpy(dest, src, n);
  dest[n] = '\0';
}

static void copyStringField(char* dest, size_t destLen, const String& src) {
  copyCStringField(dest, destLen, src.c_str());
}

static bool normalizeUuid16(const String& raw, char out[5]) {
  String s = raw;
  s.trim();
  s.toLowerCase();
  if (s.startsWith("0x")) s = s.substring(2);

  if (s.length() == 36 && s.substring(0, 4) == "0000" &&
      s.substring(8) == "-0000-1000-8000-00805f9b34fb") {
    s = s.substring(4, 8);
  }

  if (s.length() > 4 || s.length() == 0) return false;

  for (uint8_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }

  while (s.length() < 4) s = "0" + s;
  s.toUpperCase();
  copyStringField(out, 5, s);
  return true;
}

static void appendServiceUuid(char* dest, size_t destLen, const char uuid[5]) {
  if (!dest || destLen == 0 || !uuid || uuid[0] == '\0') return;

  size_t used = strlen(dest);
  size_t need = strlen(uuid) + (used > 0 ? 1 : 0);
  if (used + need >= destLen) return;

  if (used > 0) {
    dest[used++] = ';';
    dest[used] = '\0';
  }
  strncat(dest, uuid, destLen - used - 1);
}

static bool dedupeContains(const char* addr, uint8_t addrType) {
  for (uint16_t i = 0; i < dedupeCount; i++) {
    if (dedupe[i].addrType == addrType && strcmp(dedupe[i].addr, addr) == 0) {
      return true;
    }
  }
  return false;
}

static bool dedupeAdd(const char* addr, uint8_t addrType) {
  if (dedupeContains(addr, addrType)) {
    bleDiag.duplicateRejected++;
    bleDiag.burstDuplicateRejected++;
    return false;
  }
  if (dedupeCount >= BLE_DEDUPE_CAPACITY) {
    bleDropped++;
    bleDiag.pendingDropped++;
    bleDiag.burstPendingDropped++;
    return false;
  }

  copyCStringField(dedupe[dedupeCount].addr, sizeof(dedupe[dedupeCount].addr), addr);
  dedupe[dedupeCount].addrType = addrType;
  dedupeCount++;
  bleUniqueCount++;
  bleDiag.uniqueAccepted++;
  bleDiag.burstUniqueAccepted++;
  return true;
}

static bool pushPending(const BleObservation& obs) {
  if (pendingCount >= BLE_PENDING_CAPACITY) {
    bleDropped++;
    bleDiag.pendingDropped++;
    bleDiag.burstPendingDropped++;
    return false;
  }

  uint16_t idx = (uint16_t)((pendingHead + pendingCount) % BLE_PENDING_CAPACITY);
  pending[idx] = obs;
  pendingCount++;
  bleDiag.pendingAccepted++;
  bleDiag.burstPendingAccepted++;
  return true;
}

static void copyObservation(BLEAdvertisedDevice& dev, BleObservation& obs) {
  memset(&obs, 0, sizeof(obs));

  String addr = dev.getAddress().toString();
  addr.toUpperCase();
  copyStringField(obs.addr, sizeof(obs.addr), addr);

  obs.addrType = dev.getAddressType();
  obs.rssi = dev.haveRSSI() ? (int8_t)dev.getRSSI() : 0;
  obs.observedAtMs = millis();
  obs.channel = 0; // Local BLE wrapper does not expose the advertising channel.

  if (dev.haveName()) {
    copyStringField(obs.name, sizeof(obs.name), dev.getName());
  }

  if (dev.haveManufacturerData()) {
    String md = dev.getManufacturerData();
    if (md.length() >= 2) {
      obs.mfgrId = (uint16_t)((uint8_t)md[0] | ((uint16_t)(uint8_t)md[1] << 8));
    }
  }

  if (dev.haveServiceUUID()) {
    for (int i = 0; i < dev.getServiceUUIDCount(); i++) {
      char uuid[5] = {};
      if (normalizeUuid16(dev.getServiceUUID(i).toString(), uuid)) {
        appendServiceUuid(obs.serviceUuids, sizeof(obs.serviceUuids), uuid);
      }
    }
  }
}

static void fillSnapshotNoLock(BleDiagSnapshot& out) {
  out.initCount = bleDiag.initCount;
  out.startAttempts = bleDiag.startAttempts;
  out.startSuccess = bleDiag.startSuccess;
  out.startFailures = bleDiag.startFailures;
  out.callbackCount = bleDiag.callbackCount;
  out.pendingAccepted = bleDiag.pendingAccepted;
  out.pendingDropped = bleDiag.pendingDropped;
  out.uniqueAccepted = bleDiag.uniqueAccepted;
  out.duplicateRejected = bleDiag.duplicateRejected;
  out.csvRowsWritten = bleDiag.csvRowsWritten;
  out.startupBackfillRowsRouted = bleDiag.startupBackfillRowsRouted;
  out.stopCount = bleDiag.stopCount;
  out.droppedTotal = bleDropped;
  out.burstStartMs = bleDiag.burstStartMs;
  out.burstStopMs = bleDiag.burstStopMs;
  out.burstElapsedMs = bleDiag.burstElapsedMs;
  out.burstCallbacks = bleDiag.burstCallbacks;
  out.burstPendingAccepted = bleDiag.burstPendingAccepted;
  out.burstPendingDropped = bleDiag.burstPendingDropped;
  out.burstUniqueAccepted = bleDiag.burstUniqueAccepted;
  out.burstDuplicateRejected = bleDiag.burstDuplicateRejected;
  out.burstCsvRowsWritten = bleDiag.burstCsvRowsWritten;
  out.burstStartupBackfillRowsRouted = bleDiag.burstStartupBackfillRowsRouted;
  out.pendingDepth = pendingCount;
  out.ready = bleReady;
  out.active = bleRunning;
}

static void prepareBurstDiagNoLock(uint32_t startMs) {
  bleDiag.burstStartMs = startMs;
  bleDiag.burstStopMs = 0;
  bleDiag.burstElapsedMs = 0;
  bleDiag.burstCallbacks = 0;
  bleDiag.burstPendingAccepted = 0;
  bleDiag.burstPendingDropped = 0;
  bleDiag.burstUniqueAccepted = 0;
  bleDiag.burstDuplicateRejected = 0;
  bleDiag.burstCsvRowsWritten = 0;
  bleDiag.burstStartupBackfillRowsRouted = 0;
  bleDiag.burstActive = true;
  bleDiag.firstCallbackLogged = false;
  bleDiag.stoppedPendingReport = false;
  bleDiag.clearResultsPending = false;
}

static void recordBurstStop(uint32_t nowMs) {
  BleDiagSnapshot snap = {};
  bool shouldLog = false;

  {
    BleLock lock;
    if (!lock.ok()) return;

    if (!bleDiag.burstActive && bleDiag.burstStopMs != 0) return;

    bleDiag.burstActive = false;
    bleDiag.burstStopMs = nowMs;
    bleDiag.burstElapsedMs = nowMs - bleDiag.burstStartMs;
    bleDiag.stopCount++;
    bleDiag.stoppedPendingReport = true;
    bleDiag.clearResultsPending = true;
    fillSnapshotNoLock(snap);
    shouldLog = true;
  }

  if (!shouldLog) return;

  Serial.printf("[KG-BLE] stop elapsed=%lu callbacks=%lu pending=%u dropped=%lu\n",
                (unsigned long)snap.burstElapsedMs,
                (unsigned long)snap.burstCallbacks,
                snap.pendingDepth,
                (unsigned long)snap.droppedTotal);

  if (snap.burstCallbacks == 0) {
    Serial.println("[KG-BLE] WARNING burst completed with zero callbacks");
  }
}

class KgBleCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    BleObservation obs;
    copyObservation(advertisedDevice, obs);

    bool logFirst = false;
    char firstAddr[18] = {};
    int8_t firstRssi = obs.rssi;

    {
      BleLock lock;
      if (!lock.ok()) return;

      bleDiag.callbackCount++;
      bleDiag.burstCallbacks++;

      if (!bleDiag.firstCallbackLogged) {
        bleDiag.firstCallbackLogged = true;
        logFirst = true;
        copyCStringField(firstAddr, sizeof(firstAddr), obs.addr);
      }

      if (strlen(obs.addr) != 17) {
        bleDropped++;
        bleDiag.pendingDropped++;
        bleDiag.burstPendingDropped++;
      } else if (dedupeAdd(obs.addr, obs.addrType)) {
        pushPending(obs);
      }
    }

    if (logFirst) {
      Serial.printf("[KG-BLE] first callback addr=%s rssi=%d\n", firstAddr, (int)firstRssi);
    }
  }
};

KgBleCallbacks bleCallbacks;

static uint32_t safeBleDurationMs() {
  uint32_t d = cfg.bleScanDurationMs;
  if (d < BLE_SCAN_DURATION_MIN_MS || d > BLE_SCAN_DURATION_MAX_MS) {
    d = BLE_SCAN_DURATION_DEFAULT_MS;
  }
  return d;
}

static uint32_t bleApiDurationSeconds(uint32_t durationMs) {
  uint32_t seconds = (durationMs + 999UL) / 1000UL;
  return seconds == 0 ? 1 : seconds;
}

} // namespace

void bleScannerBegin() {
  if (bleReady) return;

  if (!bleMutex) bleMutex = xSemaphoreCreateMutex();
  if (!bleMutex) {
    Serial.println("[KG-BLE] init FAILED mutex allocation");
    return;
  }

  {
    BleLock lock;
    if (lock.ok()) bleDiag.initCount++;
  }

  bool initOk = BLEDevice::init("piglet");
  if (!initOk) {
    Serial.println("[KG-BLE] init FAILED BLEDevice::init");
    return;
  }

  bleScan = BLEDevice::getScan();
  if (!bleScan) {
    Serial.println("[KG-BLE] init FAILED BLEDevice::getScan");
    return;
  }

  bleScan->setAdvertisedDeviceCallbacks(&bleCallbacks, false, true);
  bleScan->setActiveScan(false);
  bleScan->setInterval(100);
  bleScan->setWindow(100);
  bleScan->setDuplicateFilter(true);

  bleReady = true;
  Serial.println("[KG-BLE] init OK");
}

bool bleScannerReady() {
  return bleReady;
}

bool bleScannerStartBurst() {
  bleLastStartFailureReason = "none";

  if (!cfg.bleEnabled) {
    bleLastStartFailureReason = "config_disabled";
    return false;
  }
  if (!bleReady) bleScannerBegin();
  if (!bleReady || !bleScan) {
    bleLastStartFailureReason = "init_failed";
    return false;
  }
  if (bleRunning) {
    bleLastStartFailureReason = "ble_already_active";
    return false;
  }

  uint32_t startMs = millis();
  uint32_t durationMs = safeBleDurationMs();
  uint32_t apiSeconds = bleApiDurationSeconds(durationMs);

  {
    BleLock lock;
    if (lock.ok()) {
      bleDiag.startAttempts++;
      prepareBurstDiagNoLock(startMs);
    }
  }

  Serial.printf("[KG-BLE] start attempt duration=%lu\n", (unsigned long)durationMs);

  bool ok = bleScan->start(apiSeconds, nullptr, false);
  bool active = ok && bleScan->isScanning();
  if (!ok || !active) {
    bleLastStartFailureReason = ok ? "api_not_active" : "api_start_failed";
  }

  {
    BleLock lock;
    if (lock.ok()) {
      if (ok && active) bleDiag.startSuccess++;
      else {
        bleDiag.startFailures++;
        bleDiag.burstActive = false;
        bleDiag.clearResultsPending = true;
        bleLastStartFailureReason = ok ? "api_not_active" : "api_start_failed";
      }
    }
  }

  Serial.printf("[KG-BLE] start result=%s active=%u apiDurationSeconds=%lu\n",
                (ok ? "success" : "failure"),
                active ? 1 : 0,
                (unsigned long)apiSeconds);

  if (!ok || !active) {
    if (bleScan) bleScan->clearResults();
    return false;
  }

  bleRunning = true;
  bleEndsAtMs = startMs + durationMs;
  return true;
}

void bleScannerStop() {
  bool wasRunning = bleRunning;

  if (!bleReady || !bleScan) {
    bleRunning = false;
    return;
  }

  bool stackActive = bleScan->isScanning();
  if (stackActive) {
    bleScan->stop();
  }

  if (wasRunning || stackActive) {
    bleRunning = false;
    recordBurstStop(millis());
  } else {
    bleRunning = false;
  }
}

void bleScannerTick() {
  if (!bleReady || !bleScan || !bleRunning) return;

  if ((int32_t)(millis() - bleEndsAtMs) >= 0) {
    bleScannerStop();
  } else if (!bleScan->isScanning()) {
    bleRunning = false;
    recordBurstStop(millis());
  }
}

bool bleScannerIsScanning() {
  return bleRunning;
}

bool bleScannerHasPending() {
  BleLock lock;
  return lock.ok() && pendingCount > 0;
}

bool bleScannerConsume(BleObservation& out) {
  BleLock lock;
  if (!lock.ok() || pendingCount == 0) return false;

  out = pending[pendingHead];
  pendingHead = (uint16_t)((pendingHead + 1) % BLE_PENDING_CAPACITY);
  pendingCount--;
  return true;
}

uint32_t bleScannerDroppedCount() {
  BleLock lock;
  return lock.ok() ? bleDropped : 0;
}

const char* bleScannerLastStartFailureReason() {
  return bleLastStartFailureReason;
}

BleDiagSnapshot bleScannerDiagSnapshot() {
  BleDiagSnapshot snap = {};
  BleLock lock;
  if (lock.ok()) fillSnapshotNoLock(snap);
  return snap;
}

void bleScannerDiagPrintConfig() {
  Serial.printf("[KG-BLE] config enabled=%u duration=%u everyCycles=%u\n",
                cfg.bleEnabled ? 1 : 0,
                cfg.bleScanDurationMs,
                cfg.bleEveryNCycles);
}

void bleScannerDiagNoteDrain(uint16_t pendingRows, uint16_t csvRows) {
  if (pendingRows == 0 && csvRows == 0) return;

  BleDiagSnapshot snap = {};
  {
    BleLock lock;
    if (!lock.ok()) return;

    bleDiag.csvRowsWritten += csvRows;
    bleDiag.burstCsvRowsWritten += csvRows;
    fillSnapshotNoLock(snap);
  }

  Serial.printf("[KG-BLE] drain accepted=%u unique=%lu duplicates=%lu csv=%lu\n",
                pendingRows,
                (unsigned long)snap.uniqueAccepted,
                (unsigned long)snap.duplicateRejected,
                (unsigned long)snap.csvRowsWritten);
}

void bleScannerDiagNoteStartupBackfill(uint16_t pendingRows, uint16_t routedRows) {
  if (pendingRows == 0 && routedRows == 0) return;

  BleDiagSnapshot snap = {};
  {
    BleLock lock;
    if (!lock.ok()) return;

    bleDiag.startupBackfillRowsRouted += routedRows;
    bleDiag.burstStartupBackfillRowsRouted += routedRows;
    fillSnapshotNoLock(snap);
  }

  if (routedRows > 0) {
    Serial.printf("[KG-BLE] routed to GPS startup backfill rows=%u total=%lu\n",
                  routedRows,
                  (unsigned long)snap.startupBackfillRowsRouted);
  }
}

void bleScannerDiagAfterDrain() {
  BleDiagSnapshot snap = {};
  bool shouldReport = false;
  bool shouldWarnNoCsv = false;
  bool shouldClear = false;

  {
    BleLock lock;
    if (!lock.ok()) return;

    if (bleDiag.stoppedPendingReport && pendingCount == 0) {
      fillSnapshotNoLock(snap);
      shouldReport = true;
      shouldWarnNoCsv = (snap.burstCallbacks > 0 &&
                         snap.burstCsvRowsWritten == 0 &&
                         snap.burstStartupBackfillRowsRouted == 0);
      shouldClear = bleDiag.clearResultsPending;
      bleDiag.stoppedPendingReport = false;
      bleDiag.clearResultsPending = false;
    }
  }

  if (!shouldReport) return;

  if (shouldWarnNoCsv) {
    Serial.println("[KG-BLE] WARNING callbacks received but no BLE CSV rows written");
  }

  Serial.printf("[KG-BLE] summary starts=%lu callbacks=%lu pending=%u unique=%lu csv=%lu startupBackfill=%lu dropped=%lu\n",
                (unsigned long)snap.startSuccess,
                (unsigned long)snap.callbackCount,
                snap.pendingDepth,
                (unsigned long)snap.uniqueAccepted,
                (unsigned long)snap.csvRowsWritten,
                (unsigned long)snap.startupBackfillRowsRouted,
                (unsigned long)snap.droppedTotal);

  if (shouldClear && bleScan && !bleRunning && !bleScan->isScanning()) {
    bleScan->clearResults();
  }
}

#else

void bleScannerBegin() {}
bool bleScannerReady() { return false; }
bool bleScannerStartBurst() { return false; }
void bleScannerStop() {}
void bleScannerTick() {}
bool bleScannerIsScanning() { return false; }
bool bleScannerHasPending() { return false; }
bool bleScannerConsume(BleObservation&) { return false; }
uint32_t bleScannerDroppedCount() { return 0; }
const char* bleScannerLastStartFailureReason() { return "ble_unavailable"; }
BleDiagSnapshot bleScannerDiagSnapshot() { return {}; }
void bleScannerDiagPrintConfig() {
  Serial.printf("[KG-BLE] config enabled=%u duration=%u everyCycles=%u\n",
                cfg.bleEnabled ? 1 : 0,
                cfg.bleScanDurationMs,
                cfg.bleEveryNCycles);
  Serial.println("[KG-BLE] init FAILED BLE library unavailable");
}
void bleScannerDiagNoteDrain(uint16_t, uint16_t) {}
void bleScannerDiagNoteStartupBackfill(uint16_t, uint16_t) {}
void bleScannerDiagAfterDrain() {}

#endif
