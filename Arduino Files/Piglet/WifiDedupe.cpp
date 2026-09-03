#include "WifiDedupe.h"
#include "Globals.h"
#include <SD.h>
#include <esp_heap_caps.h>
#include <string.h>

#ifndef FILE_APPEND
  #define FILE_APPEND FILE_WRITE
#endif

static const uint32_t WIFI_DEDUPE_WINDOW_MS = 3600000UL;
static const uint32_t WIFI_DEDUPE_BUCKETS = 16384UL;
static const uint32_t WIFI_DEDUPE_WAYS = 16UL;
static const uint32_t WIFI_DEDUPE_TOTAL_SLOTS = WIFI_DEDUPE_BUCKETS * WIFI_DEDUPE_WAYS;
static const size_t WIFI_DEDUPE_KEY_BYTES = WIFI_DEDUPE_TOTAL_SLOTS * sizeof(uint64_t);
static const size_t WIFI_DEDUPE_TIME_BYTES = WIFI_DEDUPE_TOTAL_SLOTS * sizeof(uint32_t);
static const size_t WIFI_DEDUPE_TOTAL_BYTES = WIFI_DEDUPE_KEY_BYTES + WIFI_DEDUPE_TIME_BYTES;
static const size_t WIFI_DEDUPE_LOG_MAX_BYTES = 64UL * 1024UL;
static const char* WIFI_DEDUPE_LOG_FILE = "/debug/wifi_dedupe.log";

static uint64_t* wifiDedupeKeys = nullptr;
static uint32_t* wifiDedupeAcceptedAtMs = nullptr;
static WifiDedupeState wifiDedupeState = WIFI_DEDUPE_OFF_BY_USER;
static uint32_t wifiDedupeAccepted = 0;
static uint32_t wifiDedupeDropped = 0;
static uint32_t wifiDedupeOverflowAccepted = 0;
static uint32_t wifiDedupePreviousNowMs = 0;
static bool wifiDedupePreviousNowValid = false;

static const char* wifiDedupeStateName(WifiDedupeState state) {
  switch (state) {
    case WIFI_DEDUPE_ACTIVE: return "ACTIVE";
    case WIFI_DEDUPE_OFF_BY_USER: return "OFF_BY_USER";
    case WIFI_DEDUPE_DEGRADED: return "DEGRADED";
    case WIFI_DEDUPE_DISABLED_FAIL_OPEN_PSRAM: return "FAIL_OPEN_PSRAM";
    case WIFI_DEDUPE_DISABLED_FAIL_OPEN_STATE: return "FAIL_OPEN_STATE";
    default: return "FAIL_OPEN_STATE";
  }
}

static void wifiDedupeResetStats() {
  wifiDedupeAccepted = 0;
  wifiDedupeDropped = 0;
  wifiDedupeOverflowAccepted = 0;
  wifiDedupePreviousNowMs = 0;
  wifiDedupePreviousNowValid = false;
}

static void wifiDedupeDebugLog(const char* event, bool failOpen = false) {
  Serial.printf("[WIFI_DEDUPE] %s%s%s\n", event,
                failOpen ? " ACTION: FAIL_OPEN" : "",
                failOpen ? " WIFI LOGGING CONTINUES" : "");

  if (!sdOk) return;

  if (!SD.exists("/debug")) {
    SD.mkdir("/debug");
  }

  if (SD.exists(WIFI_DEDUPE_LOG_FILE)) {
    File existing = SD.open(WIFI_DEDUPE_LOG_FILE, FILE_READ);
    if (existing) {
      size_t size = existing.size();
      existing.close();
      if (size > WIFI_DEDUPE_LOG_MAX_BYTES) {
        SD.remove(WIFI_DEDUPE_LOG_FILE);
      }
    }
  }

  File f = SD.open(WIFI_DEDUPE_LOG_FILE, FILE_APPEND);
  if (!f) {
    Serial.printf("[WIFI_DEDUPE] debug log open failed: %s\n", WIFI_DEDUPE_LOG_FILE);
    return;
  }

  f.print(millis());
  f.print(" ");
  f.print(event);
  if (failOpen) {
    f.print(" ACTION: FAIL_OPEN WIFI LOGGING CONTINUES");
  }
  f.println();
  f.close();
}

static void wifiDedupeFreeTable() {
  if (wifiDedupeKeys) {
    heap_caps_free(wifiDedupeKeys);
    wifiDedupeKeys = nullptr;
  }
  if (wifiDedupeAcceptedAtMs) {
    heap_caps_free(wifiDedupeAcceptedAtMs);
    wifiDedupeAcceptedAtMs = nullptr;
  }
}

static void wifiDedupeEnterFailOpenState() {
  wifiDedupeFreeTable();
  wifiDedupeState = WIFI_DEDUPE_DISABLED_FAIL_OPEN_STATE;
  wifiDedupeDebugLog("DISABLED_FAIL_OPEN_STATE", true);
}

static bool wifiDedupeStorageValid() {
  return wifiDedupeKeys && wifiDedupeAcceptedAtMs &&
         WIFI_DEDUPE_TOTAL_SLOTS == (WIFI_DEDUPE_BUCKETS * WIFI_DEDUPE_WAYS);
}

static void wifiDedupeLogPsramRequest() {
  Serial.printf("[WIFI_DEDUPE] PSRAM total=%u free=%u requested=%u\n",
                (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                (unsigned)WIFI_DEDUPE_TOTAL_BYTES);

  char msg[128];
  snprintf(msg, sizeof(msg), "PSRAM total=%u free=%u requested=%u",
           (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)WIFI_DEDUPE_TOTAL_BYTES);
  wifiDedupeDebugLog(msg);
}

static bool wifiDedupeAllocateTable(const char* activeEvent) {
  wifiDedupeFreeTable();
  wifiDedupeResetStats();
  wifiDedupeLogPsramRequest();

  wifiDedupeKeys = (uint64_t*)heap_caps_calloc(WIFI_DEDUPE_TOTAL_SLOTS, sizeof(uint64_t),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  wifiDedupeAcceptedAtMs = (uint32_t*)heap_caps_calloc(WIFI_DEDUPE_TOTAL_SLOTS, sizeof(uint32_t),
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (!wifiDedupeKeys || !wifiDedupeAcceptedAtMs) {
    wifiDedupeFreeTable();
    wifiDedupeState = WIFI_DEDUPE_DISABLED_FAIL_OPEN_PSRAM;
    wifiDedupeDebugLog("PSRAM_ALLOCATION_FAILED", true);
    return false;
  }

  wifiDedupeState = WIFI_DEDUPE_ACTIVE;
  Serial.printf("[WIFI_DEDUPE] PSRAM free after=%u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  char msg[128];
  snprintf(msg, sizeof(msg), "%s PSRAM_free_after=%u",
           activeEvent, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  wifiDedupeDebugLog(msg);
  return true;
}

static uint64_t wifiDedupePackBssid(const uint8_t* bssid) {
  uint64_t packed = 0;
  for (uint8_t i = 0; i < 6; i++) {
    packed = (packed << 8) | (uint64_t)bssid[i];
  }
  return packed + 1ULL;
}

static uint32_t wifiDedupeBucketForKey(uint64_t key) {
  key ^= key >> 33;
  key *= 0xff51afd7ed558ccdULL;
  key ^= key >> 33;
  key *= 0xc4ceb9fe1a85ec53ULL;
  key ^= key >> 33;
  return (uint32_t)key & (WIFI_DEDUPE_BUCKETS - 1UL);
}

static void wifiDedupeHandleMillisWrap(uint32_t nowMs) {
  if (!wifiDedupePreviousNowValid) {
    wifiDedupePreviousNowMs = nowMs;
    wifiDedupePreviousNowValid = true;
    return;
  }

  if (nowMs < wifiDedupePreviousNowMs) {
    if (wifiDedupeStorageValid()) {
      memset(wifiDedupeKeys, 0, WIFI_DEDUPE_KEY_BYTES);
      memset(wifiDedupeAcceptedAtMs, 0, WIFI_DEDUPE_TIME_BYTES);
    }
    if (wifiDedupeState == WIFI_DEDUPE_ACTIVE) {
      wifiDedupeState = WIFI_DEDUPE_ACTIVE;
    }
    wifiDedupeDebugLog("MILLIS_WRAP_RESET");
  }

  wifiDedupePreviousNowMs = nowMs;
}

void wifiDedupeBegin(bool enabled) {
  if (!enabled) {
    wifiDedupeFreeTable();
    wifiDedupeResetStats();
    wifiDedupeState = WIFI_DEDUPE_OFF_BY_USER;
    return;
  }

  wifiDedupeAllocateTable("INIT ACTIVE");
}

void wifiDedupeSetEnabled(bool enabled) {
  if (!enabled) {
    wifiDedupeFreeTable();
    wifiDedupeResetStats();
    wifiDedupeState = WIFI_DEDUPE_OFF_BY_USER;
    wifiDedupeDebugLog("DISABLED_BY_USER");
    return;
  }

  wifiDedupeDebugLog("ENABLED_BY_USER");
  wifiDedupeAllocateTable("INIT ACTIVE");
}

bool wifiDedupeAccept(const uint8_t* bssid) {
  if (wifiDedupeState == WIFI_DEDUPE_OFF_BY_USER ||
      wifiDedupeState == WIFI_DEDUPE_DISABLED_FAIL_OPEN_PSRAM ||
      wifiDedupeState == WIFI_DEDUPE_DISABLED_FAIL_OPEN_STATE) {
    return true;
  }

  if (!bssid) return true;

  if (!wifiDedupeStorageValid()) {
    wifiDedupeEnterFailOpenState();
    return true;
  }

  uint32_t nowMs = millis();
  wifiDedupeHandleMillisWrap(nowMs);

  uint64_t key = wifiDedupePackBssid(bssid);
  if (key == 0) return true;

  uint32_t bucket = wifiDedupeBucketForKey(key);
  uint32_t base = bucket * WIFI_DEDUPE_WAYS;
  int32_t emptySlot = -1;
  int32_t expiredSlot = -1;

  for (uint32_t way = 0; way < WIFI_DEDUPE_WAYS; way++) {
    uint32_t idx = base + way;
    uint64_t slotKey = wifiDedupeKeys[idx];

    if (slotKey == key) {
      uint32_t ageMs = (uint32_t)(nowMs - wifiDedupeAcceptedAtMs[idx]);
      if (ageMs < WIFI_DEDUPE_WINDOW_MS) {
        wifiDedupeDropped++;
        return false;
      }

      wifiDedupeAcceptedAtMs[idx] = nowMs;
      wifiDedupeAccepted++;
      return true;
    }

    if (slotKey == 0) {
      if (emptySlot < 0) emptySlot = (int32_t)idx;
    } else if (expiredSlot < 0) {
      uint32_t ageMs = (uint32_t)(nowMs - wifiDedupeAcceptedAtMs[idx]);
      if (ageMs >= WIFI_DEDUPE_WINDOW_MS) expiredSlot = (int32_t)idx;
    }
  }

  int32_t targetSlot = (emptySlot >= 0) ? emptySlot : expiredSlot;
  if (targetSlot >= 0) {
    wifiDedupeKeys[targetSlot] = key;
    wifiDedupeAcceptedAtMs[targetSlot] = nowMs;
    wifiDedupeAccepted++;
    return true;
  }

  wifiDedupeOverflowAccepted++;
  if (wifiDedupeState == WIFI_DEDUPE_ACTIVE) {
    wifiDedupeState = WIFI_DEDUPE_DEGRADED;
    wifiDedupeDebugLog("DEGRADED_BUCKET_FULL", true);
  }
  return true;
}

WifiDedupeSnapshot wifiDedupeGetSnapshot() {
  WifiDedupeSnapshot snap;
  snap.state = wifiDedupeState;
  snap.stateText = wifiDedupeStateText();
  snap.accepted = wifiDedupeAccepted;
  snap.dropped = wifiDedupeDropped;
  snap.overflowAccepted = wifiDedupeOverflowAccepted;
  return snap;
}

const char* wifiDedupeStateText() {
  return wifiDedupeStateName(wifiDedupeState);
}

const char* wifiDedupeOledStatusText() {
  switch (wifiDedupeState) {
    case WIFI_DEDUPE_ACTIVE: return "Filter:OK";
    case WIFI_DEDUPE_OFF_BY_USER: return "Filter:OFF";
    case WIFI_DEDUPE_DEGRADED: return "Filter:DEG";
    case WIFI_DEDUPE_DISABLED_FAIL_OPEN_PSRAM: return "F:PSRAM";
    case WIFI_DEDUPE_DISABLED_FAIL_OPEN_STATE: return "F:STATE";
    default: return "F:STATE";
  }
}

const char* wifiDedupeLogPath() {
  return WIFI_DEDUPE_LOG_FILE;
}
