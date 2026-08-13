#pragma once

#include <Arduino.h>

struct ResetHistoryEntry {
  uint32_t bootIdNumber;
  char bootId[9];
  char classification[16];
  char resetReason[24];
  char wakeReason[24];
  uint32_t size;
};

void resetHistoryWriteBootLog();
void resetHistoryMarkPlannedShutdown(const char* reason);
size_t resetHistoryList(ResetHistoryEntry* out, size_t maxCount);
bool resetHistoryIsAllowedLogPath(const String& path);
