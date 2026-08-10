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

void     bleScannerBegin();
bool     bleScannerReady();
bool     bleScannerStartBurst();
void     bleScannerStop();
void     bleScannerTick();
bool     bleScannerIsScanning();
bool     bleScannerHasPending();
bool     bleScannerConsume(BleObservation& out);
uint32_t bleScannerDroppedCount();
