#pragma once
#include <Arduino.h>

enum WifiDedupeState : uint8_t {
  WIFI_DEDUPE_ACTIVE = 0,
  WIFI_DEDUPE_OFF_BY_USER,
  WIFI_DEDUPE_DEGRADED,
  WIFI_DEDUPE_DISABLED_FAIL_OPEN_PSRAM,
  WIFI_DEDUPE_DISABLED_FAIL_OPEN_STATE
};

struct WifiDedupeSnapshot {
  WifiDedupeState state = WIFI_DEDUPE_OFF_BY_USER;
  const char* stateText = "OFF_BY_USER";
  uint32_t accepted = 0;
  uint32_t dropped = 0;
  uint32_t overflowAccepted = 0;
};

void wifiDedupeBegin(bool enabled);
void wifiDedupeSetEnabled(bool enabled);
bool wifiDedupeAccept(const uint8_t* bssid);
WifiDedupeSnapshot wifiDedupeGetSnapshot();
const char* wifiDedupeStateText();
const char* wifiDedupeOledStatusText();
const char* wifiDedupeLogPath();
