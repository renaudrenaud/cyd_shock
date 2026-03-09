# Changelog

All notable changes to this project will be documented in this file.
Format: [Keep a Changelog](https://keepachangelog.com/en/0.1.0/)

## [Unreleased]

## [0.1.6] — 2026-03-09

### Added
- FreeCAD macro `3D/cyd_case.py` to generate front & back plates for the CYD enclosure (86×60mm, 3.5mm thick)
- Front plate: screen cutout 70×50mm, centred
- Back plate: engraved `C_SHOCK` label, centred
- Rounded external corners (r=3mm) on both plates
- M3 mounting holes at all 4 corners

## [0.1.5] — 2026-03-09

### Changed
- Added two spaces before the MAC suffix for better readability (e.g. `GW-B5600  #A1B2`)

## [0.1.4] — 2026-03-09

### Changed
- Unique watch identification: display name now includes the last 2 bytes of the BLE MAC address (e.g. `GW-B5600#A1B2`) to distinguish multiple watches of the same model

## [0.1.3] — 2026-03-09

### Fixed
- Touch XPT2046: X axis was inverted on this CYD — fixed by software inversion of tx in handleTouch()

## [0.1.2] — 2026-03-09

### Changed
- Replace "WiFi OK"/"No WiFi" text in header with a 4-bar RSSI signal strength indicator (color-coded: green/orange/red)

## [0.1.1] — 2026-03-09

### Changed
- All UI strings and Serial prints translated from French to English

### Planned
- Support for multiple fallback WiFi networks
- Display watch battery level and temperature (bottom-left button)
- Configurable time offset (processing delay compensation)
- Power saving mode (screen off after inactivity)

## [0.1.0] — 2026-03-07

### Added
- BLE scan and time synchronization for G-Shock / Edifice / ProTrek watches
- Dual timezone display on the main screen
- Last 10 sync history (persisted to flash)
- Sidebar with 3 buttons: Home, History, WiFi Portal
- AP portal (network `CYD-Shock-Config`, IP 192.168.4.1) for WiFi and timezone configuration
- Full G-Shock protocol: button press detection, DST read-echo, time write
- Support for variable dstCount models (GW/GMW/MRG: 3 states, others: 1 state)
- Configuration persistence in LittleFS (`/config.json`)
- Sync history persistence in LittleFS (`/log.json`)
