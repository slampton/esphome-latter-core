<div align="center">

# Latter Core (`esphome-latter-core`)

**Universal ESPHome C++20 UI Engine, Turnkey Hardware Catalog & 1-Line Drop-In Mixins for Home Assistant**

[![Release](https://img.shields.io/badge/release-v2.2.0-03a9f4?style=for-the-badge&logo=github)](https://github.com/slampton/esphome-latter-core/releases)
[![ESPHome](https://img.shields.io/badge/ESPHome-2026.10%2B-000000?style=for-the-badge&logo=esphome&logoColor=white)](https://esphome.io)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-2026.10%2B-41BDF5?style=for-the-badge&logo=home-assistant&logoColor=white)](https://github.com/slampton/ha-latter)
[![ESP32-S3 & P4](https://img.shields.io/badge/ESP32--S3%20%7C%20ESP32--P4-Supported-E7352C?style=for-the-badge&logo=espressif&logoColor=white)](https://github.com/slampton/esphome-latter-core)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://github.com/slampton/esphome-latter-core/tree/main/include)
[![Validate CI](https://img.shields.io/github/actions/workflow/status/slampton/esphome-latter-core/validate.yml?branch=main&style=for-the-badge&logo=githubactions&logoColor=white&label=ESPHome%20CI)](https://github.com/slampton/esphome-latter-core/actions)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg?style=for-the-badge)](LICENSE)
[![Maintenance](https://img.shields.io/badge/Maintained%3F-yes-brightgreen.svg?style=for-the-badge)](https://github.com/slampton/esphome-latter-core/graphs/commit-activity)
[![Sponsor](https://img.shields.io/badge/Sponsor-Lampton%20Labs-ea4aaa?style=for-the-badge&logo=githubsponsors&logoColor=white)](https://github.com/sponsors/slampton)

</div>

---

## Overview

**`esphome-latter-core`** is the shared operating system, hardware abstraction layer (HAL), and 1-line mixin library for the **Latter** ecosystem:

1. **[`slampton/ha-latter`](https://github.com/slampton/ha-latter)** — Universal Home Assistant Companion Middleware & Web Studio (`custom_components/latter/`).
2. **`slampton/esphome-latter-core` (This Repo)** — Shared C++20 UI engine (`include/*.h`), core ESPHome modules (`modules/*.yaml`), board HAL packages (`boards/*.yaml`), turnkey device configs (`devices/*.yaml`), and 1-line drop-in mixins (`mixins/*.yaml`).
3. **[`slampton/esphome-pocket-assistant`](https://github.com/slampton/esphome-pocket-assistant)** — Dedicated flagship repository for the 1.75" Round AMOLED **Pocket Assistant**.

---

## Supported Turnkey Hardware (`devices/` & `boards/`)

All five flagship displays run the unified **Latter C++20 UI OS** (Watch Faces, 20 FPS Weather Sky, On-Device Wake Word + Assist, Music Assistant Sendspin Player, Area-Aware Room Control, Whole-Home Hub, 10-Slot Scrollable TV Remote, 4 Arcade Games, and 12 Utilities) and auto-discover in **[`ha-latter`](https://github.com/slampton/ha-latter)** with zero YAML edits:

| Product Name | Form Factor | Waveshare Hardware Target | Display & Touch | Audio & Power | Turnkey Device Config |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Pocket Assistant** | *1.75" Round Handheld* | `ESP32-S3-Touch-AMOLED-1.75C` | `466×466` Round QSPI `CO5300` + `CST9220` | `ES8311` + `ES7210`, `AXP2101`, `QMI8658` | `devices/pocket-assistant.yaml` |
| **Mobile Assistant** | *2.16" Square Handheld* | `ESP32-S3-Touch-AMOLED-2.16` *(SKU 33969)* | `480×480` Square QSPI `CO5300` + `CST9220` | `ES8311` + `ES7210`, `AXP2101`, `QMI8658` | `devices/mobile-assistant.yaml` |
| **Wrist Assist** | *2.06" Smartwatch* | `ESP32-S3-Touch-AMOLED-2.06` | `410×502` Rect QSPI `CO5300` + `FT3168` | `ES8311` + `ES7210`, `AXP2101`, `QMI8658` | `devices/wrist-assist.yaml` |
| **Remote Assistant** | *3.5" P4 Widescreen Handheld* | `ESP32-P4-WIFI6-Touch-LCD-3.5` *(SKU 33360)* | `320×480` SPI `ST7796` + `FT6336` + 5MP Cam | `ES8311`, `AXP2101`, ESP32-C6 Wi-Fi 6/BLE | `devices/remote-assistant.yaml` |
| **Desk Assistant** | *4.0" P4 Stationary Display* | `ESP32-P4-WIFI6-Touch-LCD-4B` *(SKU 31416)* | `720×720` MIPI-DSI `ST7703` + `GT911` | `ES8311` + `ES7210`, Mains/5V (`power_generic`) | `devices/desk-assistant.yaml` |

---

## Quick Start: ESPHome Device Builder Configs

Paste the snippet for your hardware into **ESPHome Device Builder** in Home Assistant:

### 1. Pocket Assistant (`1.75"` Round AMOLED — ESP32-S3)
```yaml
substitutions:
  name: "pocket-assistant"
  friendly_name: "Pocket Assistant"

packages:
  device:
    url: https://github.com/slampton/esphome-latter-core
    ref: main
    files:
      - devices/pocket-assistant.yaml
    refresh: 0s

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
```

### 2. Mobile Assistant (`2.16"` Square AMOLED — ESP32-S3)
```yaml
substitutions:
  name: "mobile-assistant"
  friendly_name: "Mobile Assistant"

packages:
  device:
    url: https://github.com/slampton/esphome-latter-core
    ref: main
    files:
      - devices/mobile-assistant.yaml
    refresh: 0s

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
```

### 3. Wrist Assist (`2.06"` Smartwatch — ESP32-S3)
```yaml
substitutions:
  name: "wrist-assist"
  friendly_name: "Wrist Assist"

packages:
  device:
    url: https://github.com/slampton/esphome-latter-core
    ref: main
    files:
      - devices/wrist-assist.yaml
    refresh: 0s

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
```

### 4. Remote Assistant (`3.5"` Widescreen Handheld — ESP32-P4)
```yaml
substitutions:
  name: "remote-assistant"
  friendly_name: "Remote Assistant"

packages:
  device:
    url: https://github.com/slampton/esphome-latter-core
    ref: main
    files:
      - devices/remote-assistant.yaml
    refresh: 0s

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
```

### 5. Desk Assistant (`4.0"` 720×720 Stationary Display — ESP32-P4)
```yaml
substitutions:
  name: "desk-assistant"
  friendly_name: "Desk Assistant"

packages:
  device:
    url: https://github.com/slampton/esphome-latter-core
    ref: main
    files:
      - devices/desk-assistant.yaml
    refresh: 0s

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
```

---

## 1-Line Drop-In Mixins for Any 3rd-Party ESPHome Device

Add any of these 1-line packages to an existing ESPHome node to integrate it with [`ha-latter`](https://github.com/slampton/ha-latter):

```yaml
packages:
  # 1. Roaming BLE iBeacon (tracks portable ESP32 devices room-to-room)
  latter.ble: github://slampton/esphome-latter-core/mixins/ble_beacon.yaml@main

  # 2. Stationary Room Bluetooth Scanner Proxy (feeds Latter's BLE room radar)
  latter.proxy: github://slampton/esphome-latter-core/mixins/ble_proxy.yaml@main

  # 3. Roaming WireGuard VPN Tunnel (remote access over phone hotspots)
  latter.vpn: github://slampton/esphome-latter-core/mixins/roaming_vpn.yaml@main

  # 4. Studio Auto-Discovery & Diagnostics (Uptime, Wi-Fi RSSI, Restart button)
  latter.diag: github://slampton/esphome-latter-core/mixins/diagnostics.yaml@main
```

---

## Repository Structure

```text
esphome-latter-core/
├── components/
│   └── latter_core/
│       └── __init__.py                  # Auto-copies include/*.h into ESPHome build src/
├── include/
│   ├── va_cancel_helper.h               # Voice Assistant session, NVS wake word & Sendspin downmixer
│   ├── ui_core.h                        # Display traits, .rodata LUTs, 3D projection, UTF-8 & blitter
│   ├── ui_layout.h                      # Adaptive layout metrics, LEDC backlight PWM & RTC/Wi-Fi HAL
│   ├── ui_ambient_va.h                  # 4 Docked Screensavers & 5 Voice Assistant visual profiles
│   ├── ui_weather_sky.h                 # 20 FPS Weather Sky shaders, lunar disc & 5-day forecast
│   ├── ui_games.h                       # 4 Arcade Games (Marbles, Archery, Breakout, Chomp)
│   ├── ui_tools.h                       # 12 Utilities (Timers, Alarms, Chrono, Calc, Map, Camera, Macros)
│   ├── ui_music.h                       # Music Assistant Now Playing, Scrubber, Lyrics & Bio reader
│   ├── ui_clock_home.h                  # Analog/Digital Watch Faces, Live Activity Stack & App Dock
│   ├── ui_room_home_hub.h               # Area Room Control (sliders + HSV wheel) & Whole-Home Hub
│   ├── ui_remote.h                      # 10-Slot Scrollable Multi-Protocol TV & Media Remote
│   ├── ui_system.h                      # On-device Settings, Diagnostics & 332px HSV Color Wheel
│   └── ui_tools_apps.h                  # Umbrella header
├── boards/
│   ├── waveshare_s3_175c.yaml           # Pocket Assistant HAL (1.75" 466x466 Round AMOLED, S3)
│   ├── waveshare_s3_206.yaml            # Wrist Assist HAL     (2.06" 410x502 Watch AMOLED, S3)
│   ├── waveshare_s3_216.yaml           # Mobile Assistant HAL (2.16" 480x480 Square AMOLED, S3)
│   ├── waveshare_p4_35.yaml             # Remote Assistant HAL (3.5" 320x480 IPS LCD + Camera, P4)
│   └── waveshare_p4_4b.yaml             # Desk Assistant HAL   (4.0" 720x720 MIPI-DSI Panel, P4)
├── devices/
│   ├── pocket-assistant.yaml            # Turnkey Pocket Assistant config
│   ├── wrist-assist.yaml                # Turnkey Wrist Assist config
│   ├── mobile-assistant.yaml            # Turnkey Mobile Assistant config
│   ├── remote-assistant.yaml            # Turnkey Remote Assistant config
│   └── desk-assistant.yaml              # Turnkey Desk Assistant config
├── modules/
│   ├── base.yaml                        # Minimal identity & contract handshake
│   ├── room_control.yaml                # Standalone Area-Aware Room Control client
│   ├── audio_voice.yaml                 # Sendspin FLAC media pipeline, mixer & micro_wake_word
│   ├── power_axp2101.yaml               # 3-Tier battery/docked/hibernate state machine (AXP2101)
│   ├── power_generic.yaml               # Mains/USB screen-timeout state machine (Non-AXP2101 / P4)
│   └── display_ui.yaml                  # Unified Latter API actions, display renderer & touch router
├── mixins/
│   ├── ble_beacon.yaml                  # 1-line Roaming BLE iBeacon
│   ├── ble_proxy.yaml                   # 1-line Stationary Bluetooth Scanner Proxy
│   ├── roaming_vpn.yaml                 # 1-line Roaming WireGuard VPN
│   └── diagnostics.yaml                 # 1-line Studio Discovery & Diagnostics
├── profiles/
│   ├── full_companion.yaml              # Profile bundle for AXP2101 battery displays
│   └── panel_companion.yaml             # Profile bundle for mains/generic panels
├── examples/
│   ├── minimal_headless_companion.yaml  # CI-validated headless companion example
│   └── custom_display_template.yaml     # CI-validated custom display porting template
└── docs/
    ├── API_CONTRACT.md                  # Sensor, service & callback wire specification
    └── PORTING_NEW_HARDWARE.md          # Step-by-step hardware porting guide
```

---

## License

Licensed under the [Apache License 2.0](LICENSE). Copyright © Lampton Labs.