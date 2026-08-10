#pragma once
#include <Arduino.h>

inline const char* bleAddrTypeText(uint8_t addrType) {
  switch (addrType) {
    case 0x00: return "[LE Public]";
    case 0x01: return "[LE Random]";
    case 0x02: return "[LE Public ID]";
    case 0x03: return "[LE Random ID]";
    case 0xFF: return "[LE Anonymous]";
    default:   return "[LE Unknown]";
  }
}

inline uint32_t bleChannelToFreqMHz(uint8_t channel) {
  switch (channel) {
    case 37: return 2402;
    case 38: return 2426;
    case 39: return 2480;
    default: return 0;
  }
}

inline String bleCsvEscaped(const char* value) {
  String out = value ? String(value) : String("");
  out.replace("\"", "\"\"");
  return out;
}

inline String formatBleWigleRow(const char* addr, uint8_t addrType,
                                const char* name, const String& firstSeen,
                                uint8_t channel, int rssi,
                                double lat, double lon, double altM, double accM,
                                const char* serviceUuids, uint16_t mfgrId) {
  String line;
  line.reserve(256);
  line += addr ? addr : "";
  line += ",\"";
  line += bleCsvEscaped(name);
  line += "\",";
  line += bleAddrTypeText(addrType);
  line += ",";
  line += firstSeen;
  line += ",";
  line += String(channel);
  line += ",";
  line += String(bleChannelToFreqMHz(channel));
  line += ",";
  line += String(rssi);
  line += ",";
  line += String(lat, 6);
  line += ",";
  line += String(lon, 6);
  line += ",";
  line += String(altM, 1);
  line += ",";
  line += String(accM, 1);
  line += ",";
  line += serviceUuids ? serviceUuids : "";
  line += ",";
  line += String(mfgrId);
  line += ",BLE";
  return line;
}
