#pragma once
#include <Arduino.h>

void doScanOnce();
bool wifiScanServiceInFlight();
uint32_t wifiScanCompletedCycles();

struct GpsLogSnapshot {
  double lat = 0;
  double lon = 0;
  double altM = 0;
  double accM = 0;
  bool usedFix = false;
  bool usedCache = false;
};

GpsLogSnapshot captureGpsLogSnapshot();

// GPS last-known-good position cache (maintained by loop(), read by Scanner)
extern bool     lastGpsValid;
extern double   lastLat, lastLon, lastAlt, lastAcc;
extern uint32_t lastGpsValidMs;
