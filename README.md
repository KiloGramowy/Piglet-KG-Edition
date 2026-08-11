
# Piglet KG Edition

**Piglet KG Edition** is an ESP32-C5-focused, field-tested fork of Piglet by
KiloGramowy.

This repository is a direct fork of the original
[Hamspiced/piglet](https://github.com/Hamspiced/piglet) project. KG Edition is
focused on incremental, field-tested improvements for mobile wardriving, GPS
resilience, Wi-Fi scanning performance, and future Wi-Fi/BLE tuning. The primary
hardware target for KG-specific tuning is the
[Seeed Studio XIAO ESP32-C5](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C5-p-6609.html).

KG Edition is not intended to replace upstream Piglet. The goal is to keep the
original Piglet identity and compatibility where practical while developing
small, reviewable improvements that can be tested on real hardware.

Original Piglet upstream: [Hamspiced/piglet](https://github.com/Hamspiced/piglet)

## KG Edition Status

✅ Hardware validated on Seeed Studio XIAO ESP32-C5:

- ✅ Configurable GPS cache duration
- ✅ WebUI GPS cache control
- ✅ 720-minute / 12-hour KG GPS field profile
- ✅ Startup GPS Backfill for pre-first-fix Wi-Fi/BLE detections
- ✅ Configurable 2.4 GHz / 5 GHz channel profiles
- ✅ Per-channel asynchronous scheduler for custom channel profiles
- ✅ Separate configurable dwell for 2.4 GHz / 5 GHz custom scanning
- ✅ WebUI Scanning (Solo) controls
- ✅ Tested KG scanning profile:
  - 2.4 GHz: `1,6,11` at `110 ms`
  - 5 GHz: `36,40,44,48` at `100 ms`
- ✅ Save/reboot persistence for the tested KG profile
- ✅ Runtime CSV verified to contain only the selected channels for that tested
  profile

This validates the tested configuration above. It does not claim that every
possible custom channel and dwell combination has been hardware tested.

🐷 Original Piglet all-channel scanning remains available as the default/fallback
mode.

Profile meanings in WebUI:

- 🐷 Original Piglet: upstream-style Wi-Fi scanning, KG BLE disabled
- 🔥 KG Recommended: default for fresh/legacy configs, hardware-tested KG Wi-Fi
  profile, BLE enabled on profile selection, fixed BLE timing `1000 ms` /
  every `5` Wi-Fi cycles
- 🛠️ Custom: explicitly user-selected manual Wi-Fi and BLE tuning. Custom is
  never selected automatically just because values match or differ.

✅ Included in the `kg-c5-ble-lab` KG Recommended profile:

- ✅ BLE timing: `1000 ms` / every `5` Wi-Fi cycles

## Current KG Configuration

For the current KG GPS field-testing profile:

```ini
gpsCacheMinutes=720
```

This keeps the last valid GPS coordinates available for up to 720 minutes
(12 hours) after the current GPS fix is lost. This is a KG Edition
recommendation, not an upstream Piglet default.

For the hardware-tested XIAO ESP32-C5 scanning profile:

```ini
scanProfile=kg
wifi24Channels=1,6,11
wifi5Channels=36,40,44,48
wifi24DwellMs=110
wifi5DwellMs=100
```

Leave `wifi24Channels` and `wifi5Channels` empty to use Original Piglet
all-channel scanning. Empty or `0` dwell values use the existing `scanMode`
dwell timing when custom channel scanning is active.

For the current BLE hardware-tested starting profile:

```ini
bleEnabled=true
bleScanDurationMs=1000
bleEveryNCycles=5
```

These BLE values work on the project XIAO ESP32-C5 and are the fixed BLE timing
used by the WebUI `KG Recommended` scanning profile when BLE is enabled. BLE can
still be disabled by the user. This is a hardware-tested KG Recommended
starting profile, not a claim of universal optimum.

## KG Edition Changes

### 📍 Startup GPS Backfill — Hardware Validated ✅

KG Edition now handles the awkward GPS startup gap without stopping the scan.
Piglet starts Wi-Fi and BLE scanning immediately, even before the GPS has
acquired its first accepted fix, but it does not intentionally write those
startup detections to the final WiGLE CSV as `0.000000,0.000000`.

Before the first quality-approved GPS fix, startup detections are temporarily
retained on SD in an internal pending store. This applies to both Wi-Fi and BLE.
When the first accepted GPS fix arrives, Piglet freezes one immutable GPS
snapshot containing latitude, longitude, altitude, and accuracy. Any Wi-Fi/BLE
work already in flight is allowed to finish cleanly, then all startup detections
are replayed with that same first-fix GPS snapshot.

Each replayed detection keeps its original `FirstSeen` timestamp and original
radio metadata. After replay completes, normal live GPS logging resumes. If GPS
is later lost, the existing configurable GPS cache takes over; the current KG
field-tested profile uses:

```ini
gpsCacheMinutes=720
```

In short: Startup GPS Backfill solves the **before-first-fix** gap. The
720-minute GPS cache solves GPS loss **after** a valid fix has already existed.

Real XIAO ESP32-C5 validation:

- Previous behavior in the tested log: `194` startup detections without GPS
  coordinates (`170` Wi-Fi, `24` BLE).
- Fixed build: `0` startup `0,0` rows in the validated CSV.
- Wi-Fi and BLE startup records were both successfully backfilled.
- Original `FirstSeen` values were preserved.
- Normal live GPS coordinates resumed after replay.

This is hardware validated on the project XIAO ESP32-C5. It is not a guarantee
that every environment or hardware setup behaves perfectly.

Intentional tradeoff: all pre-first-fix startup detections receive the same
first valid GPS snapshot. If the device moves significantly before acquiring
that first fix, those startup detections are geographically approximate and are
assigned to the first-fix position. This is deliberate and preferred over
discarding detections, waiting to scan, or writing `0,0` coordinates.

Startup pending data belongs only to the current boot. If the device never gets
a valid GPS fix and is rebooted or powered off, stale startup pending data from
the previous boot is discarded rather than assigned to a potentially unrelated
location later.

### Configurable GPS cache timeout

KG Edition adds configurable reuse of the last valid GPS coordinates after the
current GPS fix is lost.

Configuration key:

```ini
gpsCacheMinutes=3
```

- Default: `3` minutes
- KG recommended configuration: `gpsCacheMinutes=720`
- `720` minutes = 12 hours
- Valid range: `1..10080` minutes
- If the current GPS fix is lost, Piglet may continue using the last valid GPS
  coordinates for the configured duration.
- When the configured cache expires, normal fallback behavior resumes.
- Configurations without `gpsCacheMinutes` retain the original 3-minute
  behavior.

Long GPS cache periods can associate detections with an older location if the
device continues moving while GPS remains unavailable.

### Configurable scanning profiles and dwell

KG Edition adds custom 2.4 GHz / 5 GHz channel profiles for solo scanning, a
per-channel asynchronous scheduler for those custom profiles, and separate dwell
settings for 2.4 GHz and 5 GHz.

Configuration keys:

```ini
wifi24Channels=
wifi5Channels=
wifi24DwellMs=
wifi5DwellMs=
```

- Empty channel lists select Original Piglet all-channel scanning.
- `wifi24Channels` accepts 2.4 GHz channels `1..14`.
- `wifi5Channels` accepts supported 5 GHz channels on ESP32-C5.
- `wifi24DwellMs` and `wifi5DwellMs` apply only to the custom per-channel
  scheduler.
- Dwell range is `20..1500` ms.
- Empty or `0` dwell values fall back to the existing `scanMode` dwell timing.
- `20 ms` dwell is accepted but experimental.
- The KG Recommended WebUI profile writes `1,6,11` / `36,40,44,48` with
  `110 ms` / `100 ms` dwell, plus BLE timing `1000 ms` / every `5` Wi-Fi
  cycles when BLE is enabled.

## KG Edition Roadmap

The following items are planned or experimental. Some may exist only on the
lab branch and are not yet validated as KG Recommended behavior:

- 🧪 Further ESP32-C5 mobile wardriving scan-profile tuning. The next planned
  5 GHz dwell field-test values are `60 ms` and `40 ms`; they are not yet
  validated.
- 📌 Automatic Log Retention / Auto Delete:
  - Off / On
  - Delete eligibility based on successful upload to WDGWars, WiGLE, or both
  - Configurable "Keep last N uploaded logs"
  - Delete oldest eligible uploaded logs only after the retained count is
    exceeded
  - Never delete a log when the required upload confirmation is missing or
    unsuccessful
  - If "Both" is selected, require confirmed successful upload to both services
    before deletion
- 🚧 Further BLE comparative field profiles and Wi-Fi/BLE coexistence tuning
  remain experimental on `kg-c5-ble-lab`.
- 🧪 Additional field-tested improvements based on real XIAO ESP32-C5 logs and
  hardware testing

🚧 Further BLE tuning remains lab work. Startup GPS Backfill is hardware
validated, including BLE startup rows. KG Recommended BLE timing is
hardware-tested at `1000 ms` / every `5` Wi-Fi cycles; other BLE profiles are
not yet validated.

## Upstream and Credits

Original Piglet: [Hamspiced/piglet](https://github.com/Hamspiced/piglet)

Piglet KG Edition is directly forked from the original Hamspiced project.

[drdray1/piglet](https://github.com/drdray1/piglet) is a valuable secondary
reference and inspiration source for selected experimental ideas. Features are
reviewed and integrated selectively rather than treating another fork as KG
Edition's upstream.

## Development Philosophy

KG Edition changes are developed incrementally: one focused feature at a time,
with source review first, compile/test where possible, real XIAO ESP32-C5
hardware validation, and field log comparison. Stable, useful improvements may
later be proposed upstream.

## Original Piglet Documentation

The original Piglet project documentation is preserved below.

## Piglet Wardriver

**Piglet** is an open-source ESP32-based wardriving platform that scans nearby Wi-Fi networks, records GPS position, saves WiGLE-compatible CSV logs to SD, and provides a real-time web UI for control, uploads, and device status.

Designed for **[Seeed XIAO ESP32-S3](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/), [XIAO ESP32-C5](https://wiki.seeedstudio.com/xiao_esp32c5_getting_started/), [XIAO ESP32-C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/), and [XIAO ESP32-C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/)**, Piglet focuses on:

- Reliable scanning while in motion
- Clean WiGLE-ready data collection
- Simple field deployment
- Fully hackable open firmware


## Features

- 2.4 GHz Wi-Fi scanning  
- 5 GHz scanning on ESP32-C5 hardware  
- GPS position, heading, and speed logging  
- SD card logging in WiGLE CSV format  
- Web UI for:
  - Start / stop scanning  
  - Upload logs to WiGLE  
  - View device status  
  - Manage SD files  
  - Edit configuration  
- OLED live status display  
- Optional battery monitoring (board dependent)  
- Automatic STA connect with AP fallback  
- Optimized for mobile wardriving or warWalking!
- **ESP-Now Mesh Node mode** — pair with a coordinator device for multi-node wardriving
- **Mesh auto-start on boot** — configure `meshModeOnBoot` to automatically enter Core or Node mode after uploads complete, bypassing the AP window
- **Screen rotation** — mount the display upside-down and set `rotateScreen180=true` to flip 180°
- **Auto-start wardriving after uploads** — set `autoStartAfterUpload=true` to disconnect from home Wi-Fi immediately after boot uploads complete and begin scanning without delay
- **PigletNode** — standalone minimal firmware for XIAO ESP32-C5 that boots directly as a mesh node (no display, GPS, or SD required)


## ESP-Now Mesh Network Node Mode

Piglet includes a built-in **Mesh Node mode** that lets it act as a wireless wardriving node alongside a compatible coordinator device. In this mode, Piglet scans Wi-Fi networks and forwards results over ESP-Now — no SD card or GPS fix required on Piglet itself. The coordinator handles GPS stamping and data logging.

### Compatible Coordinator Devices

- **Biscuit Pro** by [Hedge / Biscuit Shop](https://biscuitshop.us)
- **JCMK C5 Wardriver** by JustCallMeKoko

### How to Use

1. Power on your coordinator device (Biscuit Pro or JCMK C5 Wardriver)
2. On Piglet, press the button to cycle pages until you reach **Mesh Node** (the last page after the pig animation)
3. Piglet automatically searches for a coordinator on ESP-Now channel 6
4. Once connected, it receives a channel range assignment and begins forwarding scan data
5. Press the button again to exit Mesh Node mode and return to normal wardriving

### Mesh Node Display

While in Mesh Node mode the OLED shows:
- Link status (Searching / Core linked)
- Coordinator MAC address
- Assigned channel range
- Total networks discovered
- Records forwarded to the coordinator

> **Note:** Entering Mesh Node mode suspends normal WiGLE CSV logging. All data is sent live to the coordinator. Exiting the page restores normal scanning automatically.

### Auto-Start Mesh Mode on Boot

Set `meshModeOnBoot` in `/wardriver.cfg` to automatically enter mesh mode after boot uploads are complete, without needing to navigate pages manually:

| Value | Behaviour |
|-------|-----------|
| `none` | Normal wardriving (default) |
| `core` | Enters Mesh Core mode — acts as coordinator, logs records from nodes |
| `node` | Enters Mesh Node mode — forwards scan results to a Core |

When `core` or `node` is set the SoftAP window is **skipped entirely** (ESP-Now owns the WiFi stack and the AP would be non-functional). The device goes straight from boot uploads to the mesh page. Set via the web UI **Mesh Mode On Boot** dropdown or directly in `/wardriver.cfg`.


## PigletNode — Standalone Mesh Node

A minimal, standalone firmware for the **Seeed XIAO ESP32-C5** in the `PigletNode/` folder. No display, GPS, or SD card required — flash it and it automatically pairs with any Piglet running in Core mode and begins scanning.

- Single-file Arduino sketch, zero external library dependencies
- Boots directly into JCMK-compatible ESP-Now node mode
- Dual-band scanning: 40 channels (2.4 GHz ch 1–14 + 5 GHz UNII-1/2/2e/3)
- Auto-pairs with Piglet Core mode (XIAO or T-Dongle)
- 30-second Core timeout with automatic re-search
- LED: fast blink = searching, slow blink = paired and scanning
- Hold BOOT button > 2 s to force a re-search

**Flash:** Open `PigletNode/PigletNode.ino` in Arduino IDE, select **XIAO_ESP32C5**, upload. No libraries to install.


## T-Dongle C5 Variant

A standalone firmware port is available for the **LilyGo T-Dongle C5** in the `TDongleC5_Piglet/` folder. This variant is a self-contained single-file sketch with its own display driver, LED control, and web UI — no external OLED required.

**Hardware:** LilyGo T-Dongle C5 (ESP32-C5, built-in ST7735 0.96" TFT, APA102 LED, TF card slot)

**Additional GPS:** Connect any UART GPS module via the Qwiic/JST connector (RX=GPIO12, TX=GPIO11)

**Pages:** Status · Networks · Navigation · Pig animation · Mesh Node

### T-Dongle C5 Required Libraries

Install via Arduino Library Manager (`Sketch → Include Library → Manage Libraries`):

| Library | Author |
|---------|--------|
| Adafruit ST7735 and ST7789 Library | Adafruit |
| Adafruit GFX Library | Adafruit |
| Adafruit BusIO | Adafruit |
| TinyGPSPlus | Mikal Hart |
| ArduinoJson | Benoit Blanchon |

All networking, SPI, SD, ESP-Now, and ESP-IDF headers are included in the ESP32 Arduino core — no separate install needed.

**Board setup:** Add `https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json` to Additional Boards Manager URLs, install **esp32 by Espressif v3.x or later**, and select **ESP32C5 Dev Module**.


## Supported Hardware

### Microcontroller Boards

- [Seeed XIAO ESP32-S3](https://www.seeedstudio.com/XIAO-ESP32S3-p-5627.html)  
- [Seeed XIAO ESP32-C5](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C5-p-6609.html) *(required for 5 GHz scanning)*  
- [Seeed XIAO ESP32-C6](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-p-5884.html)  
- [Seeed XIAO ESP32-C3](https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html) *(2.4 GHz only, headless — set `board=c3`)*  
- LilyGo T-Dongle C5 *(standalone variant — see above)*
- [Seeed XIAO ESP32-C5](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C5-p-6609.html)  *(PigletNode — mesh node only, see above)*  

### Required Peripherals

- I2C GPS module (ATGM336H)
- 128×64 SSD1306 OLED display (I2C)
- SPI SD card module
- Optional LiPo battery connected to XIAO battery inputs

### Peripheral Sourcing  
You can get everything on Amazon but its pricey.  if you dont mind waiting on aliexpress heres the build list.
- Xiao-C5 - 7$ [SeedStudio](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C5-p-6609.html)
- SSD1306 128x63 OLED - $2 [aliexpress](https://www.aliexpress.us/item/3256805954920554.html)
- ATGM-336h - $3.39 [aliexpress](https://www.aliexpress.us/item/3256809330278648.html)
- SD-Card Module - $1.33 [aliexpress](https://www.aliexpress.us/item/3256808167816573.html)
- User Button - $0.41 [Item:CS1211 From Digikey](https://www.digikey.com/en/products/detail/cit-relay-and-switch/CS1211/16607858)

$14 if you wanted to breadboard it yourself.


## Wiring / Pinouts

Pin mappings are automatically selected by firmware.

### XIAO ESP32-S3

| Function | Pin |
|----------|-----|
| I2C SDA | GPIO 5 |
| I2C SCL | GPIO 6 |
| GPS RX | GPIO 4 |
| GPS TX | GPIO 7 |
| Button | GPIO 1 |
| SD CS | GPIO 2 |
| SD MOSI | GPIO 10 |
| SD MISO | GPIO 9 |
| SD SCK | GPIO 8 |

### XIAO ESP32-C6 / ESP32-C5

| Function | Pin |
|----------|----- |
| I2C SDA | GPIO 23 |
| I2C SCL | GPIO 24 |
| GPS RX | GPIO 12 |
| GPS TX | GPIO 11 |
| Button | GPIO 0 |
| SD CS | GPIO 7 |
| SD MOSI | GPIO 10 |
| SD MISO | GPIO 9 |
| SD SCK | GPIO 8 |

**Note:** Only the ESP32-C5 supports 5 GHz Wi-Fi scanning.

### XIAO ESP32-C3

Set `board=c3` in `/wardriver.cfg` or select **XIAO C3** in the Web UI. Auto-detected from chip model on first boot.

| Function | Pin |
|----------|----- |
| I2C SDA | GPIO 6 (D4) |
| I2C SCL | GPIO 7 (D5) |
| GPS RX | GPIO 20 (D7) |
| GPS TX | GPIO 21 (D6) |
| Button | *none* (GPIO 9 = SPI MISO conflict — wire externally if needed) |
| SD CS | GPIO 2 (D0) |
| SD MOSI | GPIO 10 (D10) |
| SD MISO | GPIO 9 (D9) |
| SD SCK | GPIO 8 (D8) |

**Note:** 2.4 GHz only. No built-in display — attach an optional SSD1306 OLED on D4/D5.


## 3D Printed Cases

Print-ready STL files are available in the `Case Files/` directory. These cases are designed specifically for the Piglet PCB and module stack.

### 👏 Case Design by Bread — Breadbox Systems

The Piglet case was designed by **Bread** at [Breadbox Systems](https://breadboxsystems.com). If you print one, please take a moment to **like and boost the design on MakerWorld** — it helps the creator and makes the design easier for others to find.

> **[🖨️ Print the Piglet Wardriver Case on MakerWorld →](https://makerworld.com/en/models/2708429-piglet-wardriver-case#profileId-3000037)**

| File | Description |
|------|-------------|
| `Piglet Face.STL` | Front panel / lid |
| `Piglet Butt.STL` | Rear enclosure |
| `Piglet Butt with SMA hole.STL` | Rear enclosure with external antenna cutout |
| `Piglet Midboard.STL` | Internal standoff / mid-layer |
| `Piglet Features.stl` | Feature plate / accessory mount |

For the **T-Dongle C5** variant, GPS antenna mount STLs are included in `TDongleC5_Piglet/GPS STL/`.

> Print with standard PLA or PETG. No supports required on most parts. Recommend 0.2 mm layer height, 3 perimeters.


## PCB Design

Piglet includes custom PCB designs for compact, production-ready builds. KiCad project files and Gerber production files are available in the `PCB Files/` directory.

### PCB Images

| Board Front | Board Back | Board Close-up |
|-------------|------------|----------------|
| ![Board1](Images/Board1.jpg) | ![Board2](Images/Board2.jpg) | ![Board3](Images/Board3.jpg) |

### Assembled Piglet

| Module Arrangement | Front View | Back View |
|--------------------|------------|----------|
| ![Module Arrangement](Images/Module_Arrangement.png) | ![Built Piglet Front](Images/BuiltPiglet.jpg) | ![Built Piglet Back](Images/BackBuiltPiglet.jpg) |

**Assembly Note:** When stacking modules, apply **Kapton tape** between components to prevent electrical shorts. Pay special attention to exposed pins and solder joints that may contact adjacent modules.


## First Time Run Initialization

The Wardriver functions using a config file located on the root of a FAT32-formatted SD card. On first boot the device will start its own access point (`Piglet-WARDRIVE` / `wardrive1234`) so you can connect and fill in your settings via the web interface — or you can place the config file on the SD card manually before powering on.

**AP Timer & Keep-Alive**

The SoftAP runs for **60 seconds** by default. About **30 seconds before** that window expires, the WebUI shows a *"Stay in WebUI?"* prompt — clicking **Stay** extends the window to a **5 minute** rolling timer so you have room to actually use the WebUI. The same prompt re-appears 30 seconds before the 5 minute timer expires; clicking Stay again resets it.

- **Stay** — extends (or re-extends) the window to 5 minutes.
- **Start Scanning Now** — closes the AP immediately and begins wardriving.
- **Ignored / browser closed** — timer runs out, AP closes, scanning starts.

The OLED shows the live countdown (`AP: 192.168.4.1 60s` initially, `m:ss` once extended). Once your home Wi-Fi is configured, the device will connect to it on subsequent boots and the WebUI is reachable on the STA IP shown on the OLED — the AP only comes up if STA fails.

**Location:** `/wardriver.cfg` on the SD card root

A sample config file is included in `Arduino Files/Piglet/wardriver.cfg`. The full default config with all available keys is shown below:

```ini
# ============================================================
# Piglet Wardriver Configuration
# Format: key=value
# Lines starting with # are comments and ignored.
# ============================================================

# ------------------------------------------------------------
# WiGLE Upload
# ------------------------------------------------------------
# Use only the "Encoded for Use" token from wigle.net/account
# Leave empty to disable WiGLE uploads.

wigleBasicToken=EnterWigleTokenHere

# ------------------------------------------------------------
# WDGoWars
# ------------------------------------------------------------
# Get your API key at: https://wdgwars.pl/profile/
# Leave empty to disable WDGoWars uploads.

wdgwarsApiKey=EnterWDGoWarsAPIKeyHere

# ------------------------------------------------------------
# Max Automatic Uploads at Boot
# ------------------------------------------------------------
# -1 = Upload ALL pending files every boot
#  0 = Disabled — no auto-upload at boot (use web UI manually)
# 1+ = Upload up to N files per boot

maxBootUploads=-1

# ------------------------------------------------------------
# Device Name (optional)
# ------------------------------------------------------------
# A short label for this device. Used in WiGLE CSV filenames
# and in the WiGLE upload header so you can tell devices apart.
# Spaces become underscores. Max 20 characters.
# Leave empty to use the default (no prefix).

deviceName=

# ------------------------------------------------------------
# Home Wi-Fi (STA mode)
# ------------------------------------------------------------
# If provided, device connects on boot.
# If connection fails, falls back to SoftAP for 60 seconds
# (can be extended via the "Stay in WebUI?" prompt that appears
# ~30 s before the timer expires).

homeSsid=EnterWifiHere
homePsk=EnterWifiPasswordHere

# ------------------------------------------------------------
# Wardriver Access Point (SoftAP fallback)
# ------------------------------------------------------------
# SSID and password for the temporary config AP.
# Password must be 8+ characters or AP becomes open.

wardriverSsid=Piglet-WARDRIVE
wardriverPsk=wardrive1234

# ------------------------------------------------------------
# GPS Settings
# ------------------------------------------------------------
# UART baud rate for the GPS module.
# Common values: 9600, 38400, 115200

gpsBaud=9600

# Reuse the last valid GPS position for this many minutes after fix loss.
# Default: 3. Valid range: 1-10080. Example: 720 keeps it for 12 hours.
# Long values can stamp detections with stale coordinates if the device moves
# significantly while GPS is unavailable.

gpsCacheMinutes=3

# ------------------------------------------------------------
# Wi-Fi Scan Mode
# ------------------------------------------------------------
# aggressive  — scans every ~3 seconds using async mode (faster, more power)
# powersaving — scans every ~12 seconds (slower, less power)

scanMode=aggressive

# ------------------------------------------------------------
# Solo Scan Profile (KG Edition)
# ------------------------------------------------------------
# Explicit profile identity: original, kg, or custom.
# Missing/invalid legacy values default to kg.
# KG tested XIAO ESP32-C5 profile:
#   scanProfile=kg
#   wifi24Channels=1,6,11
#   wifi5Channels=36,40,44,48
#   wifi24DwellMs=110
#   wifi5DwellMs=100
#   bleEnabled=true
#   bleScanDurationMs=1000
#   bleEveryNCycles=5
# Custom is selected only when scanProfile=custom.
# Empty or 0 dwell values use the scanMode-derived dwell timing.
# Valid explicit dwell range: 20-1500 ms. 20 ms is experimental.

scanProfile=kg
wifi24Channels=1,6,11
wifi5Channels=36,40,44,48
wifi24DwellMs=110
wifi5DwellMs=100
bleEnabled=true
bleScanDurationMs=1000
bleEveryNCycles=5

# ------------------------------------------------------------
# Speed Units (display only)
# ------------------------------------------------------------
# kmh = kilometers per hour
# mph = miles per hour

speedUnits=mph

# ------------------------------------------------------------
# Battery Test
# ------------------------------------------------------------
# true = logs elapsed time on battery to /battery_test.csv
# false = disabled

batteryTest=false

# ------------------------------------------------------------
# Mesh Mode On Boot
# ------------------------------------------------------------
# Automatically enter ESP-Now mesh mode after boot uploads complete.
# Bypasses the SoftAP window and jumps directly to the mesh page.
#
#   none = Normal wardriving mode (default)
#   core = Start as Mesh Core (coordinator) — logs data from nodes
#   node = Start as Mesh Node — forwards scan results to a Core
#
# Requires a compatible coordinator (Biscuit Pro, JCMK C5 Wardriver)
# when using node mode.

meshModeOnBoot=none

# ------------------------------------------------------------
# Screen Rotation
# ------------------------------------------------------------
# Rotate the display 180 degrees for upside-down mounting.
# Values: true or false
# Reboot required after changing.

rotateScreen180=false

# ------------------------------------------------------------
# Auto-Start Wardriving After Uploads
# ------------------------------------------------------------
# When true: disconnects from home Wi-Fi immediately after boot uploads
# complete and begins wardriving without delay. The web UI remains
# accessible if you later connect to the Wardriver AP, but the device
# will not hold the STA link open.
# Values: true or false
# Reboot required after changing.

autoStartAfterUpload=false
```

### Auto-Start Wardriving After Uploads — How to Disable

> **Important:** When `autoStartAfterUpload=true`, the device disconnects from your home Wi-Fi immediately after uploads finish and goes straight into wardriving. Because it no longer holds the STA connection, the web UI is **not** reachable on your home network after boot.

To disable this setting after it has been enabled, you have two options:

**Option 1 — Connect via the Wardriver AP**

When Piglet is away from the saved home network (or the home network is unavailable), it falls back to its own SoftAP. Connect to it and use the web UI:

1. Power on the device somewhere the home Wi-Fi is not in range (or temporarily forget the home network on the device by clearing `homeSsid` in the config)
2. Connect your phone or laptop to the **Wardriver SSID** (default: `Piglet-WARDRIVE` / `wardrive1234`)
3. Open a browser and go to **`http://192.168.4.1`**
4. In the **Configuration** section, set **Auto-Start Wardriving After Uploads** to **Disabled**
5. Click **Save & Reboot**

**Option 2 — Edit the SD card directly**

1. Remove the SD card from the device
2. Open `wardriver.cfg` in any text editor
3. Change `autoStartAfterUpload=true` to `autoStartAfterUpload=false`
4. Save the file, re-insert the SD card, and reboot


## Button Functions

### Press Types

| Press | Timing | Action |
|-------|--------|--------|
| **Single press** | Quick tap | Advance to next page |
| **Double press** | Two taps within 350 ms | Toggle scan pause *(Status page only)* |
| **Long press** | Hold ≥ 2 seconds | Enter deep sleep |
| **Single press** *(while sleeping)* | Any | Wake from deep sleep / reboot |

### Pages (Single Press cycles through these)

| Page | Name | What it Shows | Scanning |
|------|------|---------------|----------|
| 0 | **Status** | Scan state, SD, GPS fix, WiFi, network counts, speed, IP, upload status | ✅ Active |
| 1 | **Networks** | Large display of 2.4 GHz, 5 GHz, and total network counts | ✅ Active |
| 2 | **Navigation** | Compass arrow, heading direction, current speed | ✅ Active |
| 3 | **Paused** | Pause icon — scanning fully stopped | ❌ Paused |
| 4 | **Pig** | Walking pig animation 🐷 | ✅ Active |
| 5 | **Mesh Node** | ESP-Now link state, coordinator MAC, channel range, Found/Sent counts | ↔ Forwarded via ESP-Now |

### Double Press — Status Page Only

When on the **Status page**, double-pressing toggles a scan pause without leaving the page. Useful for a quick stop without navigating to the Pause page.

- **Double press →** Scanning paused on status page
- **Double press again →** Scanning resumed
- **Single press (page change) →** Pause automatically cleared when leaving page 0

### Long Press — Deep Sleep

Hold the button for **2 seconds** from any page:
- Active log file is flushed and closed before sleeping
- OLED displays `Sleep...` then powers off
- A single button press wakes the device (full reboot)

### Mesh Node Page

Entering **page 5** automatically starts ESP-Now node mode. Leaving it (single press to advance) automatically restores normal wardriving. See the [ESP-Now Mesh Network Node Mode](#esp-now-mesh-network-node-mode) section above for details.

## Building Firmware

### Requirements

- **Arduino IDE 2.x** or **PlatformIO**
- **Arduino-ESP32 core** v3.0.0 or later

### Required Libraries — XIAO Variant (S3 / C5 / C6)

Install via Arduino Library Manager (`Sketch → Include Library → Manage Libraries`):

| Library | Author | Notes |
|---------|--------|-------|
| TinyGPSPlus | Mikal Hart | GPS NMEA parsing |
| Adafruit GFX Library | Adafruit | Graphics dependency |
| Adafruit SSD1306 | Adafruit | OLED display driver |
| Adafruit BusIO | Adafruit | Required by SSD1306 |
| ArduinoJson | Benoit Blanchon | v6.x or v7.x |

All other headers (`WiFi`, `WebServer`, `WiFiClientSecure`, `HTTPClient`, `SD`, `SPI`, `Wire`, `esp_now.h`, `esp_wifi.h`) are included in the ESP32 Arduino core — no separate install needed.

### Required Libraries — T-Dongle C5 Variant

Install via Arduino Library Manager:

| Library | Author | Notes |
|---------|--------|-------|
| Adafruit ST7735 and ST7789 Library | Adafruit | TFT display driver |
| Adafruit GFX Library | Adafruit | Graphics dependency |
| Adafruit BusIO | Adafruit | Required by ST7735 |
| TinyGPSPlus | Mikal Hart | GPS NMEA parsing |
| ArduinoJson | Benoit Blanchon | v6.x or v7.x |

All networking, SPI, SD, ESP-Now, and ESP-IDF headers are built into the ESP32 core.

### Flash Steps

1. Select the correct **XIAO ESP32 board** variant (S3, C5, or C6)
2. **CRITICAL:** Enable **PSRAM** (required for TLS/HTTPS uploads)
   - Tools → PSRAM → **OPI PSRAM** (C5/C6) or **QSPI PSRAM** (S3)
3. Use a **large app partition scheme** → **Huge APP (3MB No OTA/1MB SPIFFS)**
4. Upload firmware  
5. Insert **FAT32-formatted SD card**  
6. Add `/wardriver.cfg` to SD card root with your WiGLE API key and WiFi credentials
7. Restart device with RST button or power cycle

### Where to Order

I always order everything from JLCPCB because i get the best deals.  However i do understand that people prefer the all in one project offers of PCBway so i have created this project here:

[PCBWayProjects](https://www.pcbway.com/project/shareproject/Piglet_Opensource_Wardriving_Project_1a21b94b.html)



## License

Creative Commons Attribution-NonCommercial 4.0 (CC BY-NC 4.0)

You may:

- Use  
- Modify  
- Share  

You may **not** use this project for commercial purposes.

https://creativecommons.org/licenses/by-nc/4.0/

---

Created by **Midwewest Gadgets LLC**

