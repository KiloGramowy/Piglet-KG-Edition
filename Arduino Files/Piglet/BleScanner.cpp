#include "BleScanner.h"

#if PIGLET_HAS_BLE

#include "Config.h"
#include "Globals.h"

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

SemaphoreHandle_t bleMutex = nullptr;
BLEScan* bleScan = nullptr;
bool bleReady = false;
bool bleRunning = false;
uint32_t bleEndsAtMs = 0;
uint32_t bleDropped = 0;

BleObservation pending[BLE_PENDING_CAPACITY];
uint16_t pendingHead = 0;
uint16_t pendingCount = 0;

BleKey dedupe[BLE_DEDUPE_CAPACITY];
uint16_t dedupeCount = 0;

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

static void copyStringField(char* dest, size_t destLen, const String& src) {
  if (!dest || destLen == 0) return;
  size_t n = src.length();
  if (n >= destLen) n = destLen - 1;
  memcpy(dest, src.c_str(), n);
  dest[n] = '\0';
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
  if (dedupeContains(addr, addrType)) return false;
  if (dedupeCount >= BLE_DEDUPE_CAPACITY) {
    bleDropped++;
    return false;
  }

  copyStringField(dedupe[dedupeCount].addr, sizeof(dedupe[dedupeCount].addr), String(addr));
  dedupe[dedupeCount].addrType = addrType;
  dedupeCount++;
  bleUniqueCount++;
  return true;
}

static bool pushPending(const BleObservation& obs) {
  if (pendingCount >= BLE_PENDING_CAPACITY) {
    bleDropped++;
    return false;
  }

  uint16_t idx = (uint16_t)((pendingHead + pendingCount) % BLE_PENDING_CAPACITY);
  pending[idx] = obs;
  pendingCount++;
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

class KgBleCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    BleObservation obs;
    copyObservation(advertisedDevice, obs);
    if (strlen(obs.addr) != 17) return;

    BleLock lock;
    if (!lock.ok()) return;

    if (!dedupeAdd(obs.addr, obs.addrType)) return;
    pushPending(obs);
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

} // namespace

void bleScannerBegin() {
  if (bleReady) return;

  if (!bleMutex) bleMutex = xSemaphoreCreateMutex();
  if (!bleMutex) {
    Serial.println("[BLE] Mutex allocation failed");
    return;
  }

  BLEDevice::init("piglet");
  bleScan = BLEDevice::getScan();
  if (!bleScan) {
    Serial.println("[BLE] BLEDevice::getScan failed");
    return;
  }

  bleScan->setAdvertisedDeviceCallbacks(&bleCallbacks, false, true);
  bleScan->setActiveScan(false);
  bleScan->setInterval(100);
  bleScan->setWindow(100);
  bleScan->setDuplicateFilter(true);

  bleReady = true;
  Serial.println("[BLE] Passive scanner ready (interval=100ms window=100ms)");
}

bool bleScannerReady() {
  return bleReady;
}

bool bleScannerStartBurst() {
  if (!cfg.bleEnabled) return false;
  if (!bleReady) bleScannerBegin();
  if (!bleReady || !bleScan || bleRunning) return false;

  uint32_t durationMs = safeBleDurationMs();
  bool ok = bleScan->start(0, nullptr, false);
  if (!ok) {
    Serial.println("[BLE] Failed to start passive scan");
    return false;
  }

  bleRunning = true;
  bleEndsAtMs = millis() + durationMs;
  Serial.printf("[BLE] Passive burst start (%lu ms)\n", (unsigned long)durationMs);
  return true;
}

void bleScannerStop() {
  if (!bleReady || !bleScan) {
    bleRunning = false;
    return;
  }

  if (bleRunning || bleScan->isScanning()) {
    bleScan->stop();
  }
  bleScan->clearResults();
  bleRunning = false;
}

void bleScannerTick() {
  if (!bleReady || !bleScan) return;

  if (bleRunning && (int32_t)(millis() - bleEndsAtMs) >= 0) {
    bleScannerStop();
    Serial.printf("[BLE] Passive burst end (unique=%lu dropped=%lu)\n",
                  (unsigned long)bleUniqueCount,
                  (unsigned long)bleDropped);
  } else if (bleRunning && !bleScan->isScanning()) {
    bleScan->clearResults();
    bleRunning = false;
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

#endif
