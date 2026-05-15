# PigletNode — Standalone Mesh Node

A minimal, standalone firmware for the **Seeed XIAO ESP32-C5** that turns it into a dedicated wardriving node for the Piglet platform.

No display. No GPS. No SD card. Just plug it in, and it automatically pairs with any Piglet running in Core mode and starts scanning.

---

## Hardware

| Item | Notes |
|------|-------|
| **Seeed XIAO ESP32-C5** | Required — dual-band Wi-Fi 6 (2.4 + 5 GHz) |
| USB-C cable | For flashing and power |
| Optional: LiPo battery | XIAO C5 has an onboard charger |

No other components needed.

---

## Flashing (Arduino IDE)

1. Install the **ESP32 Arduino core** (v3.x) via Boards Manager:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`

2. Select board: **XIAO_ESP32C5**
   - Tools → Board → ESP32 Arduino → XIAO_ESP32C5

3. Settings:
   - Partition Scheme: **Default** (or any with ≥1 MB app)
   - USB CDC On Boot: **Enabled** (for Serial monitor)

4. Open `PigletNode.ino` and upload.

### Required libraries
None beyond the built-in ESP32 Arduino core. No external libraries needed.

---

## How it works

1. **Boot** — Initialises ESP-Now on channel 6 (the JCMK home channel).
2. **Searching** — Broadcasts `CORE_REQUEST` packets with exponential backoff (300 ms → 5 s).
3. **Paired** — Receives a channel assignment from the Core and begins scanning all assigned channels (80 ms dwell per channel).
4. **Scanning** — After each full channel cycle, returns to ch 6 to send results and heartbeat, then starts the next cycle.
5. **Auto-reconnect** — If the Core goes silent for 30 seconds, automatically resets and starts searching again.

The firmware is wire-compatible with the JCMK protocol used by:
- Piglet (any variant) in Core mode
- justcallmekoko/ESP32DualBandWardriver (core role)

---

## LED status

| Pattern | Meaning |
|---------|---------|
| Fast blink (200 ms) | Searching for Core |
| Slow blink (1 s) | Paired and scanning |
| Rapid blink (50 ms) | Fatal error — power-cycle to retry |

---

## Button

The BOOT button (GPIO0) doubles as a user control at runtime:

| Action | Result |
|--------|--------|
| Hold > 2 s | Force re-search (useful when moving between Cores) |

---

## Serial monitor

Connect at **115200 baud**. Useful output:

```
=== PigletNode Boot ===
JCMK channels: 40  |  home ch: 6
[NODE] Stagger 1423 ms...
[NODE] Ready — searching for Core on ch 6...
[NODE] Core found: AA:BB:CC:DD:EE:FF
[NODE] Core AA:BB:CC:DD:EE:FF | ch 1-14 (v1) | found 42 | sent 42
```

---

## License

CC BY-NC-SA 4.0
