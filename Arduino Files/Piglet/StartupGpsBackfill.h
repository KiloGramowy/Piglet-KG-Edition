#pragma once
#include <Arduino.h>
#include "BleScanner.h"

struct StartupGpsBackfillSnapshot {
  double lat = 0.0;
  double lon = 0.0;
  double altM = 0.0;
  double accM = 0.0;
};

void     startupGpsBackfillBeginSession();
bool     startupGpsBackfillFixSeen();
bool     startupGpsBackfillAcceptingPending();
bool     startupGpsBackfillCloseoutActive();
bool     startupGpsBackfillReplayActive();
bool     startupGpsBackfillScannerBlocked();
uint32_t startupGpsBackfillPendingCount();

bool startupGpsBackfillQueueWifi(const String& mac, const String& ssid,
                                 const String& auth, const String& firstSeen,
                                 int channel, int rssi);
bool startupGpsBackfillQueueBle(const BleObservation& obs, const String& firstSeen);

void startupGpsBackfillCaptureFirstFix(double lat, double lon, double altM, double accM);
void startupGpsBackfillNoteCloseoutWaiting(bool wifiBusy, bool bleBusy, uint16_t blePending);
void startupGpsBackfillCompleteCloseout();
void startupGpsBackfillTick();
