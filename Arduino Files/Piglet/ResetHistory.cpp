#include "ResetHistory.h"
#include "Globals.h"
#include <SD.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <string.h>

static const char* RESET_DEBUG_DIR = "/debug";
static const char* RESET_MARKER_PATH = "/debug/shutdown.marker";
static const size_t RESET_HISTORY_MAX_LOGS = 10;

static void copyText(char* dst, size_t len, const char* src) {
  if (!dst || len == 0) return;
  if (!src) src = "";
  snprintf(dst, len, "%s", src);
}

static bool ensureDebugDir() {
  if (SD.exists(RESET_DEBUG_DIR)) return true;
  return SD.mkdir(RESET_DEBUG_DIR);
}

static bool parseBootLogName(const char* rawName, uint32_t* outId) {
  if (!rawName) return false;

  const char* name = strrchr(rawName, '/');
  name = name ? name + 1 : rawName;

  if (strlen(name) != 17) return false;
  if (strncmp(name, "boot_", 5) != 0) return false;
  if (strcmp(name + 13, ".log") != 0) return false;

  uint32_t id = 0;
  for (int i = 5; i < 13; ++i) {
    if (name[i] < '0' || name[i] > '9') return false;
    id = (id * 10) + (uint32_t)(name[i] - '0');
  }

  if (outId) *outId = id;
  return true;
}

static void formatBootId(uint32_t bootId, char* out, size_t len) {
  if (!out || len == 0) return;
  snprintf(out, len, "%08lu", (unsigned long)bootId);
}

static void formatBootLogPath(uint32_t bootId, char* out, size_t len) {
  if (!out || len == 0) return;
  snprintf(out, len, "/debug/boot_%08lu.log", (unsigned long)bootId);
}

static uint32_t nextBootId() {
  uint32_t maxId = 0;
  File dir = SD.open(RESET_DEBUG_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return 1;
  }

  File file = dir.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      uint32_t id = 0;
      if (parseBootLogName(file.name(), &id) && id > maxId) maxId = id;
    }
    file.close();
    file = dir.openNextFile();
  }
  dir.close();

  return maxId + 1;
}

static const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:   return "ESP_RST_UNKNOWN";
    case ESP_RST_POWERON:   return "ESP_RST_POWERON";
    case ESP_RST_EXT:       return "ESP_RST_EXT";
    case ESP_RST_SW:        return "ESP_RST_SW";
    case ESP_RST_PANIC:     return "ESP_RST_PANIC";
    case ESP_RST_INT_WDT:   return "ESP_RST_INT_WDT";
    case ESP_RST_TASK_WDT:  return "ESP_RST_TASK_WDT";
    case ESP_RST_WDT:       return "ESP_RST_WDT";
    case ESP_RST_DEEPSLEEP: return "ESP_RST_DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "ESP_RST_BROWNOUT";
    case ESP_RST_SDIO:      return "ESP_RST_SDIO";
    default:                return "ESP_RST_OTHER";
  }
}

static const char* wakeReasonName(esp_sleep_wakeup_cause_t reason) {
  switch (reason) {
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "UNDEFINED";
    case ESP_SLEEP_WAKEUP_ALL:       return "ALL";
    case ESP_SLEEP_WAKEUP_EXT0:      return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1:      return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER:     return "TIMER";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:  return "TOUCHPAD";
    case ESP_SLEEP_WAKEUP_ULP:       return "ULP";
    case ESP_SLEEP_WAKEUP_GPIO:      return "GPIO";
    case ESP_SLEEP_WAKEUP_UART:      return "UART";
    case ESP_SLEEP_WAKEUP_WIFI:      return "WIFI";
    case ESP_SLEEP_WAKEUP_BT:        return "BT";
    default:                         return "OTHER";
  }
}

static const char* factualClassification(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "POWER_ON";
    case ESP_RST_SW:        return "SOFTWARE_RESET";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "WATCHDOG";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
    default:                return "OTHER";
  }
}

static bool isUnexpectedReset(esp_reset_reason_t reason) {
  return reason == ESP_RST_PANIC ||
         reason == ESP_RST_INT_WDT ||
         reason == ESP_RST_TASK_WDT ||
         reason == ESP_RST_WDT ||
         reason == ESP_RST_BROWNOUT;
}

static bool readAndConsumeMarker(char* out, size_t len) {
  copyText(out, len, "NONE");
  if (!SD.exists(RESET_MARKER_PATH)) return false;

  File marker = SD.open(RESET_MARKER_PATH, FILE_READ);
  if (marker) {
    String value = marker.readStringUntil('\n');
    value.trim();
    if (value == "DEEP_SLEEP" || value == "WEB_REBOOT") {
      copyText(out, len, value.c_str());
    } else {
      copyText(out, len, "UNKNOWN");
    }
    marker.close();
  }

  SD.remove(RESET_MARKER_PATH);
  return strcmp(out, "DEEP_SLEEP") == 0 || strcmp(out, "WEB_REBOOT") == 0;
}

static void enforceRetention() {
  while (true) {
    size_t count = 0;
    uint32_t oldestId = UINT32_MAX;
    char oldestPath[32] = {0};

    File dir = SD.open(RESET_DEBUG_DIR);
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      return;
    }

    File file = dir.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        uint32_t id = 0;
        if (parseBootLogName(file.name(), &id)) {
          ++count;
          if (id < oldestId) {
            oldestId = id;
            formatBootLogPath(id, oldestPath, sizeof(oldestPath));
          }
        }
      }
      file.close();
      file = dir.openNextFile();
    }
    dir.close();

    if (count <= RESET_HISTORY_MAX_LOGS || oldestPath[0] == '\0') return;
    SD.remove(oldestPath);
  }
}

static void stripResetPrefix(char* value) {
  static const char prefix[] = "ESP_RST_";
  if (!value || strncmp(value, prefix, strlen(prefix)) != 0) return;
  memmove(value, value + strlen(prefix), strlen(value + strlen(prefix)) + 1);
}

static void loadEntryDetails(ResetHistoryEntry& entry, const char* path) {
  copyText(entry.classification, sizeof(entry.classification), "OTHER");
  copyText(entry.resetReason, sizeof(entry.resetReason), "UNKNOWN");
  copyText(entry.wakeReason, sizeof(entry.wakeReason), "UNDEFINED");

  File file = SD.open(path, FILE_READ);
  if (!file) return;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    if (line.startsWith("Classification: ")) {
      String value = line.substring(16);
      int slash = value.indexOf(" / ");
      if (slash >= 0) value = value.substring(0, slash);
      value.trim();
      copyText(entry.classification, sizeof(entry.classification), value.c_str());
    } else if (line.startsWith("Reset reason: ")) {
      String value = line.substring(14);
      value.trim();
      copyText(entry.resetReason, sizeof(entry.resetReason), value.c_str());
      stripResetPrefix(entry.resetReason);
    } else if (line.startsWith("Wake reason: ")) {
      String value = line.substring(13);
      value.trim();
      copyText(entry.wakeReason, sizeof(entry.wakeReason), value.c_str());
    }
  }

  file.close();
}

static void insertEntryNewestFirst(ResetHistoryEntry* out, size_t maxCount, size_t& count, const ResetHistoryEntry& entry) {
  size_t pos = 0;
  while (pos < count && out[pos].bootIdNumber > entry.bootIdNumber) ++pos;
  if (pos >= maxCount && count >= maxCount) return;

  if (count < maxCount) ++count;
  for (size_t i = count - 1; i > pos; --i) out[i] = out[i - 1];
  out[pos] = entry;
}

void resetHistoryWriteBootLog() {
  if (!sdOk) return;
  if (!ensureDebugDir()) return;

  uint32_t bootId = nextBootId();
  esp_reset_reason_t resetReason = esp_reset_reason();
  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();

  char plannedShutdown[16];
  bool planned = readAndConsumeMarker(plannedShutdown, sizeof(plannedShutdown));

  const char* classification = planned ? "NORMAL" : factualClassification(resetReason);
  const char* outcome = planned ? "PLANNED" : (isUnexpectedReset(resetReason) ? "UNEXPECTED" : "UNPLANNED");

  char bootIdText[9];
  char path[32];
  formatBootId(bootId, bootIdText, sizeof(bootIdText));
  formatBootLogPath(bootId, path, sizeof(path));

  File file = SD.open(path, FILE_WRITE);
  if (!file) return;

  file.println("Piglet KG Edition");
  file.print("Boot ID: ");
  file.println(bootIdText);
  file.print("Reset reason: ");
  file.println(resetReasonName(resetReason));
  file.print("Reset code: ");
  file.println((int)resetReason);
  file.print("Wake reason: ");
  file.println(wakeReasonName(wakeReason));
  file.print("Wake code: ");
  file.println((int)wakeReason);
  file.print("Planned shutdown: ");
  file.println(plannedShutdown);
  file.print("Classification: ");
  file.print(classification);
  file.print(" / ");
  file.println(outcome);
  file.close();

  enforceRetention();
}

void resetHistoryMarkPlannedShutdown(const char* reason) {
  if (!sdOk || !reason) return;
  if (strcmp(reason, "DEEP_SLEEP") != 0 && strcmp(reason, "WEB_REBOOT") != 0) return;
  if (!ensureDebugDir()) return;

  SD.remove(RESET_MARKER_PATH);
  File marker = SD.open(RESET_MARKER_PATH, FILE_WRITE);
  if (!marker) return;
  marker.println(reason);
  marker.close();
}

size_t resetHistoryList(ResetHistoryEntry* out, size_t maxCount) {
  if (!sdOk || !out || maxCount == 0) return 0;

  size_t count = 0;
  File dir = SD.open(RESET_DEBUG_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return 0;
  }

  File file = dir.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      uint32_t id = 0;
      if (parseBootLogName(file.name(), &id)) {
        ResetHistoryEntry entry = {};
        entry.bootIdNumber = id;
        formatBootId(id, entry.bootId, sizeof(entry.bootId));
        entry.size = (uint32_t)file.size();

        char path[32];
        formatBootLogPath(id, path, sizeof(path));
        loadEntryDetails(entry, path);
        insertEntryNewestFirst(out, maxCount, count, entry);
      }
    }
    file.close();
    file = dir.openNextFile();
  }
  dir.close();

  return count;
}

bool resetHistoryIsAllowedLogPath(const String& path) {
  if (path.length() != 24) return false;
  if (!path.startsWith("/debug/boot_")) return false;
  if (!path.endsWith(".log")) return false;

  for (int i = 12; i < 20; ++i) {
    char c = path.charAt(i);
    if (c < '0' || c > '9') return false;
  }

  return true;
}
