# Changelog

## v2.57 (2026-06-24)

### New Features
- **Auto-Start Wardriving After Uploads** (`autoStartAfterUpload`): new config option that disconnects from home Wi-Fi immediately after boot uploads complete and begins scanning without delay. Previously the device held the STA connection open, which paused scanning until the link dropped naturally. Configurable via web UI or `wardriver.cfg`. Disabled by default.

  > **Note:** Once enabled, the web UI is not reachable on the home network after boot (device disconnects immediately after uploading). To disable it, either power on away from the home network so the Wardriver AP broadcasts — connect to it and visit `http://192.168.4.1` — or remove the SD card and set `autoStartAfterUpload=false` in `wardriver.cfg` directly.

### Bug Fixes
- **Mesh node mode on S3 / C6**: nodes no longer attempt to scan 5 GHz channels (36–177) on 2.4 GHz-only hardware. Previously those scan attempts failed silently and wasted ~80 ms each per cycle; the node now skips channels > 14 when not running on a C5.

### T-Dongle C5
- Synced mesh WiFi init fix: `enterCoreMode()` and `enterNodeMode()` now use `WiFi.mode(WIFI_OFF) → WIFI_STA` (full deinit/reinit) instead of `WiFi.disconnect`. Matches the XIAO fix that restored Core/Node connectivity.
- Added `[CORE] RX CORE_REQUEST` diagnostic print in `jcmkOnRecv` and channel-verification prints in both enter functions.

---

## v1.3-beta (2026-02-23)

### New Features
- **WiGLE Upload History Tracking**: Web UI now displays upload statistics (new networks discovered, total networks) for uploaded files
- **Automatic Boot Upload with Quota Management**: Configurable `maxBootUploads` setting (default: 25) to control how many files upload automatically at boot
- **24-Hour History Caching**: Upload history API calls are cached for 24 hours to conserve WiGLE API quota (25 calls/day limit)
- **On-Demand History Refresh**: History automatically refreshes in web UI when cache expires (only when connected to home network)

### Improvements
- **Optimized Upload Performance**: Removed token pre-checks and reduced timeouts for faster batch uploads
- **Enhanced WiFi Stability**: Scanning now properly pauses when connected to home network to prevent connection drops
- **Web Server Startup Timing**: Web server now starts after WiGLE operations complete to avoid resource conflicts
- **Improved Configuration Management**: Added `maxBootUploads` and `speedUnits` configuration options
- **Better Status Display**: Config form in web UI now properly displays all saved values including WiGLE token

### Bug Fixes
- Fixed scanning interference causing 100% ping loss when connected to home WiFi
- Fixed web UI configuration display issues (all fields now populate correctly)
- Fixed chunked encoding errors in `/status.json` and `/files.json` endpoints
- Corrected WiGLE token display (now shows actual token instead of "(set)")
- Fixed JSON buffer overflow issues in files endpoint

### Technical Changes
- Increased JSON buffer for files endpoint from 4KB to 8KB to handle upload statistics
- Switched from HTTP/1.1 to HTTP/1.0 for WiGLE API compatibility
- Added proper `client.flush()` to ensure complete data transmission
- Reduced upload timeout from 60s to 25s for better reliability
- History parsing now uses incremental JSON parsing to reduce memory fragmentation

### Configuration
- New config option: `maxBootUploads` - Max CSV files to upload at boot (0-25, default: 25)
- Updated config option: `speedUnits` - Display speed in km/h or mph
- Config file now saves `maxBootUploads` setting to `/wardriver.cfg`

### Requirements
- **CRITICAL**: PSRAM must be enabled in Arduino IDE for reliable TLS/HTTPS uploads
  - ESP32-C5/C6: Use OPI PSRAM
  - ESP32-S3: Use QSPI PSRAM
- Arduino-ESP32 core v3.0.0 or later
- Updated library dependencies documented in README

### Known Issues
- ESP32-C5/C6 require PSRAM enabled or TLS connections will fail due to insufficient heap
- Initial boot may show "Failed to allocate dummy cacheline for PSRAM" warning (can be ignored)

### Migration Notes
- No breaking changes from v1.2
- Existing `/wardriver.cfg` files are compatible
- New `maxBootUploads` setting will default to 25 if not present in config

---

## v1.2 (Previous Release)
- Initial stable release with basic wardriving functionality
- SD card CSV logging
- Web UI for file management
- Manual WiGLE upload support
