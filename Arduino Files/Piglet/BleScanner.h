#pragma once
#include <Arduino.h>

#if __has_include("sdkconfig.h")
  #include "sdkconfig.h"
#endif

#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_NIMBLE_ENABLED) && __has_include(<BLEDevice.h>)
  #define PIGLET_HAS_BLE 1
#else
  #define PIGLET_HAS_BLE 0
#endif

static const uint16_t BLE_PENDING_CAPACITY = 128;
static const uint16_t BLE_DEDUPE_CAPACITY = 200;

struct BleObservation {
  char     addr[18];         // "AA:BB:CC:DD:EE:FF"
  uint8_t  addrType;         // NimBLE BLE_ADDR_* type
  char     name[33];         // Advertised name, truncated
  char     serviceUuids[64]; // ';'-joined 16-bit UUIDs, empty if none
  uint16_t mfgrId;           // Little-endian company identifier, 0 if absent
  int8_t   rssi;             // dBm
  uint32_t observedAtMs;
  uint8_t  channel;          // 0 = unknown/not exposed by local API
};

struct BleDiagSnapshot {
  uint32_t initCount;
  uint32_t startAttempts;
  uint32_t startSuccess;
  uint32_t startFailures;
  uint32_t callbackCount;
  uint32_t pendingAccepted;
  uint32_t pendingDropped;
  uint32_t uniqueAccepted;
  uint32_t duplicateRejected;
  uint32_t csvRowsWritten;
  uint32_t stopCount;
  uint32_t droppedTotal;
  uint32_t burstStartMs;
  uint32_t burstStopMs;
  uint32_t burstElapsedMs;
  uint32_t burstCallbacks;
  uint32_t burstPendingAccepted;
  uint32_t burstPendingDropped;
  uint32_t burstUniqueAccepted;
  uint32_t burstDuplicateRejected;
  uint32_t burstCsvRowsWritten;
  uint16_t pendingDepth;
  bool     ready;
  bool     active;
};

void     bleScannerBegin();
bool     bleScannerReady();
bool     bleScannerStartBurst();
void     bleScannerStop();
void     bleScannerTick();
bool     bleScannerIsScanning();
bool     bleScannerHasPending();
bool     bleScannerConsume(BleObservation& out);
uint32_t bleScannerDroppedCount();
BleDiagSnapshot bleScannerDiagSnapshot();
void     bleScannerDiagPrintConfig();
void     bleScannerDiagNoteDrain(uint16_t pendingRows, uint16_t csvRows);
void     bleScannerDiagAfterDrain();
