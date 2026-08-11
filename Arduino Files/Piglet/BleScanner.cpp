#include "BleScanner.h"
#include "Config.h"
#include "Globals.h"

#if PIGLET_HAS_BLE

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

static const uint32_t BLE_DEDUPE_INITIAL_CAPACITY = 256;
static const uint8_t BLE_DEDUPE_MAX_LOAD_NUMERATOR = 3;
static const uint8_t BLE_DEDUPE_MAX_LOAD_DENOMINATOR = 4;

enum BleDedupeResult {
  DEDUPE_INVALID,
  DEDUPE_DUPLICATE,
  DEDUPE_UNIQUE_TRACKED,
  DEDUPE_ACCEPTED_DEGRADED
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
  uint32_t dedupeDegradedAccepted = 0;
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

uint64_t* dedupeKeys = nullptr;
uint32_t dedupeCapacity = 0;
uint32_t dedupeCount = 0;
uint32_t dedupeGrowCount = 0;
bool dedupeDegraded = false;

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

static bool hexNibble(char c, uint8_t& out) {
  if (c >= '0' && c <= '9') {
    out = (uint8_t)(c - '0');
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    out = (uint8_t)(c - 'A' + 10);
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    out = (uint8_t)(c - 'a' + 10);
    return true;
  }
  return false;
}

static bool parseDedupeKey(const char* addr, uint8_t addrType, uint64_t& key) {
  if (!addr || strlen(addr) != 17) return false;

  uint64_t mac = 0;
  for (uint8_t i = 0; i < 6; i++) {
    uint8_t hi = 0;
    uint8_t lo = 0;
    uint8_t pos = (uint8_t)(i * 3);
    if (!hexNibble(addr[pos], hi) || !hexNibble(addr[pos + 1], lo)) return false;
    if (i < 5 && addr[pos + 2] != ':') return false;
    mac = (mac << 8) | (uint64_t)((hi << 4) | lo);
  }

  key = ((uint64_t)addrType << 49) | (mac << 1) | 1ULL;
  return true;
}

static uint32_t hashDedupeKey(uint64_t key) {
  key ^= key >> 33;
  key *= 0xff51afd7ed558ccdULL;
  key ^= key >> 33;
  key *= 0xc4ceb9fe1a85ec53ULL;
  key ^= key >> 33;
  return (uint32_t)(key ^ (key >> 32));
}

static bool dedupeContainsKey(uint64_t key) {
  if (!dedupeKeys || dedupeCapacity == 0) return false;

  uint32_t idx = hashDedupeKey(key) & (dedupeCapacity - 1);
  while (true) {
    uint64_t slot = dedupeKeys[idx];
    if (slot == 0) return false;
    if (slot == key) return true;
    idx = (idx + 1) & (dedupeCapacity - 1);
  }
}

static bool dedupeInsertKey(uint64_t* table, uint32_t capacity, uint64_t key) {
  if (!table || capacity == 0) return false;

  uint32_t idx = hashDedupeKey(key) & (capacity - 1);
  while (true) {
    if (table[idx] == 0) {
      table[idx] = key;
      return true;
    }
    if (table[idx] == key) return false;
    idx = (idx + 1) & (capacity - 1);
  }
}

static void dedupeEnterDegraded(const char* phase, uint32_t requestedCapacity) {
  if (dedupeDegraded) return;

  dedupeDegraded = true;
  Serial.printf("[KG-BLE] WARNING dedupe allocation/growth FAILED phase=%s unique=%lu capacity=%lu requested=%lu freeHeap=%lu; entering degraded logging mode\n",
                (phase && phase[0] != '\0') ? phase : "unknown",
                (unsigned long)bleDiag.uniqueAccepted,
                (unsigned long)dedupeCapacity,
                (unsigned long)requestedCapacity,
                (unsigned long)ESP.getFreeHeap());
}

static bool dedupeAllocateInitial() {
  uint64_t* table = (uint64_t*)calloc(BLE_DEDUPE_INITIAL_CAPACITY, sizeof(uint64_t));
  if (!table) {
    dedupeEnterDegraded("init", BLE_DEDUPE_INITIAL_CAPACITY);
    return false;
  }

  dedupeKeys = table;
  dedupeCapacity = BLE_DEDUPE_INITIAL_CAPACITY;
  Serial.printf("[KG-BLE] dedupe init capacity=%lu bytes=%lu\n",
                (unsigned long)dedupeCapacity,
                (unsigned long)(dedupeCapacity * sizeof(uint64_t)));
  return true;
}

static bool dedupeGrow(uint32_t newCapacity) {
  uint32_t oldCapacity = dedupeCapacity;
  uint64_t* oldKeys = dedupeKeys;
  uint64_t* next = (uint64_t*)calloc(newCapacity, sizeof(uint64_t));
  if (!next) {
    dedupeEnterDegraded("grow", newCapacity);
    return false;
  }

  for (uint32_t i = 0; i < oldCapacity; i++) {
    uint64_t key = oldKeys[i];
    if (key != 0 && !dedupeInsertKey(next, newCapacity, key)) {
      free(next);
      dedupeEnterDegraded("rehash", newCapacity);
      return false;
    }
  }

  dedupeKeys = next;
  dedupeCapacity = newCapacity;
  dedupeGrowCount++;
  free(oldKeys);

  Serial.printf("[KG-BLE] dedupe grow old=%lu new=%lu unique=%lu\n",
                (unsigned long)oldCapacity,
                (unsigned long)newCapacity,
                (unsigned long)bleDiag.uniqueAccepted);
  return true;
}

static bool dedupeEnsureCapacityForInsert() {
  if (dedupeDegraded) return false;
  if (dedupeCapacity == 0) return dedupeAllocateInitial();

  uint64_t projected = ((uint64_t)dedupeCount + 1ULL) * BLE_DEDUPE_MAX_LOAD_DENOMINATOR;
  uint64_t threshold = (uint64_t)dedupeCapacity * BLE_DEDUPE_MAX_LOAD_NUMERATOR;
  if (projected <= threshold) return true;

  uint32_t newCapacity = dedupeCapacity;
  do {
    if (newCapacity >= 0x80000000UL) {
      dedupeEnterDegraded("grow_overflow", newCapacity);
      return false;
    }
    newCapacity *= 2;
    threshold = (uint64_t)newCapacity * BLE_DEDUPE_MAX_LOAD_NUMERATOR;
  } while (projected > threshold);

  return dedupeGrow(newCapacity);
}

static BleDedupeResult dedupeAdd(const char* addr, uint8_t addrType) {
  uint64_t key = 0;
  if (!parseDedupeKey(addr, addrType, key)) return DEDUPE_INVALID;

  if (dedupeContainsKey(key)) {
    bleDiag.duplicateRejected++;
    bleDiag.burstDuplicateRejected++;
    return DEDUPE_DUPLICATE;
  }

  if (!dedupeEnsureCapacityForInsert()) {
    bleDiag.dedupeDegradedAccepted++;
    return DEDUPE_ACCEPTED_DEGRADED;
  }

  if (!dedupeInsertKey(dedupeKeys, dedupeCapacity, key)) {
    dedupeEnterDegraded("insert", dedupeCapacity);
    bleDiag.dedupeDegradedAccepted++;
    return DEDUPE_ACCEPTED_DEGRADED;
  }

  dedupeCount++;
  bleUniqueCount++;
  bleDiag.uniqueAccepted++;
  bleDiag.burstUniqueAccepted++;
  return DEDUPE_UNIQUE_TRACKED;
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
  out.dedupeCapacity = dedupeCapacity;
  out.dedupeGrowCount = dedupeGrowCount;
  out.dedupeDegradedAccepted = bleDiag.dedupeDegradedAccepted;
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
  out.dedupeDegraded = dedupeDegraded;
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

      BleDedupeResult dedupeResult = dedupeAdd(obs.addr, obs.addrType);
      if (dedupeResult == DEDUPE_INVALID) {
        bleDropped++;
        bleDiag.pendingDropped++;
        bleDiag.burstPendingDropped++;
      } else if (dedupeResult == DEDUPE_UNIQUE_TRACKED ||
                 dedupeResult == DEDUPE_ACCEPTED_DEGRADED) {
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

  {
    BleLock lock;
    if (lock.ok()) dedupeEnsureCapacityForInsert();
  }

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
  Serial.printf("[KG-BLE] dedupe exactUnique=%lu capacity=%lu grows=%lu degraded=%u degradedAccepted=%lu\n",
                (unsigned long)bleDiag.uniqueAccepted,
                (unsigned long)dedupeCapacity,
                (unsigned long)dedupeGrowCount,
                dedupeDegraded ? 1 : 0,
                (unsigned long)bleDiag.dedupeDegradedAccepted);
}

void bleScannerDiagNoteDrain(uint16_t pendingRows, uint16_t csvRows) {
  if (pendingRows == 0 && csvRows == 0) return;

  {
    BleLock lock;
    if (!lock.ok()) return;

    bleDiag.csvRowsWritten += csvRows;
    bleDiag.burstCsvRowsWritten += csvRows;
  }
}

void bleScannerDiagNoteStartupBackfill(uint16_t pendingRows, uint16_t routedRows) {
  if (pendingRows == 0 && routedRows == 0) return;

  {
    BleLock lock;
    if (!lock.ok()) return;

    bleDiag.startupBackfillRowsRouted += routedRows;
    bleDiag.burstStartupBackfillRowsRouted += routedRows;
  }
}

void bleScannerDiagAfterDrain() {
  BleDiagSnapshot snap = {};
  bool shouldReport = false;
  bool shouldWarnZeroCallbacks = false;
  bool shouldWarnNoCsv = false;
  bool shouldWarnDrops = false;
  bool shouldClear = false;

  {
    BleLock lock;
    if (!lock.ok()) return;

    if (bleDiag.stoppedPendingReport && pendingCount == 0) {
      fillSnapshotNoLock(snap);
      shouldReport = true;
      shouldWarnZeroCallbacks = (snap.burstCallbacks == 0);
      shouldWarnNoCsv = (snap.burstCallbacks > 0 &&
                         snap.burstCsvRowsWritten == 0 &&
                         snap.burstStartupBackfillRowsRouted == 0);
      shouldWarnDrops = (snap.burstPendingDropped > 0);
      shouldClear = bleDiag.clearResultsPending;
      bleDiag.stoppedPendingReport = false;
      bleDiag.clearResultsPending = false;
    }
  }

  if (!shouldReport) return;

  if (shouldWarnZeroCallbacks) {
    Serial.println("[KG-BLE] WARNING burst completed with zero callbacks");
  }

  if (shouldWarnNoCsv) {
    Serial.println("[KG-BLE] WARNING callbacks received but no direct CSV or startup-backfill rows produced");
  }

  if (shouldWarnDrops) {
    Serial.printf("[KG-BLE] WARNING pending/dedupe drops this burst=%lu\n",
                  (unsigned long)snap.burstPendingDropped);
  }

  Serial.printf("[KG-BLE] summary elapsed=%lu callbacks=%lu newUnique=%lu csv=%lu startupBackfill=%lu dropped=%lu totalUnique=%lu dedupeCap=%lu grows=%lu degraded=%u degradedAccepted=%lu\n",
                (unsigned long)snap.burstElapsedMs,
                (unsigned long)snap.burstCallbacks,
                (unsigned long)snap.burstUniqueAccepted,
                (unsigned long)snap.burstCsvRowsWritten,
                (unsigned long)snap.burstStartupBackfillRowsRouted,
                (unsigned long)snap.burstPendingDropped,
                (unsigned long)snap.uniqueAccepted,
                (unsigned long)snap.dedupeCapacity,
                (unsigned long)snap.dedupeGrowCount,
                snap.dedupeDegraded ? 1 : 0,
                (unsigned long)snap.dedupeDegradedAccepted);

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
  Serial.println("[KG-BLE] dedupe unavailable (BLE library unavailable)");
}
void bleScannerDiagNoteDrain(uint16_t, uint16_t) {}
void bleScannerDiagNoteStartupBackfill(uint16_t, uint16_t) {}
void bleScannerDiagAfterDrain() {}

#endif
