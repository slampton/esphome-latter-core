# ESPHome Latter Core (`esphome-latter-core`)

[![Validate SDK](https://github.com/slampton/esphome-latter-core/actions/workflows/validate.yml/badge.svg)](https://github.com/slampton/esphome-latter-core/actions/workflows/validate.yml)
[![Version](https://img.shields.io/badge/Version-v2.2.0-blue)](https://github.com/slampton/esphome-latter-core)
[![Latter Ecosystem](https://img.shields.io/badge/Latter-v2.2%2B-03a9f4)](https://github.com/slampton/ha-latter)
[![ESPHome](https://img.shields.io/badge/ESPHome-2026.10.0%2B-blue?logo=esphome&logoColor=white)](https://esphome.io)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-2026.10%2B-41BDF5?logo=homeassistant&logoColor=white)](https://www.home-assistant.io)
[![Music Assistant](https://img.shields.io/badge/Music%20Assistant-2.10.8%2B-purple)](https://music-assistant.io)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-ffdd00?logo=buy-me-a-coffee&logoColor=black)](https://www.buymeacoffee.com/slampton)
[![GitHub Sponsors](https://img.shields.io/badge/Sponsor-%E2%9D%A4-ea4aaa?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/slampton)

**`esphome-latter-core`** is the shared C++20 operating system, resolution-adaptive UI engine, and 1-line YAML mixin SDK for **[Latter](https://github.com/slampton/ha-latter)** companion devices in Home Assistant.

It serves two purposes:
1. **1-Line Drop-In Mixins (`mixins/*.yaml`)**: Upgrade any existing 3rd-party ESPHome device with roaming BLE room presence, stationary BLE proxy scanning, WireGuard roaming VPN, or Studio diagnostics—with **zero substitutions required**.
2. **Full Companion Display OS (`components/latter_core`, `boards/*.yaml`, `devices/*.yaml`, `modules/*.yaml`, `include/*.h`)**: The single-source-of-truth C++20 UI, voice assistant pipeline, Music Assistant player, and power state machine powering Latter's lineup of turnkey smart displays (flashable in 1 click via the **[Unified Latter Web Installer](https://slampton.github.io/esphome-latter-core/)** or via ESPHome Device Builder):

| Device | Form Factor | Screen | Best For | Waveshare Hardware |
| :--- | :--- | :--- | :--- | :--- |
| **[Pocket Assistant](devices/pocket-assistant.yaml)** | Compact Round Handheld | 1.75" Round AMOLED (`466×466`) | Everyday pocket carry, watch-style home hub, voice & music control | [ESP32-S3-Touch-AMOLED-1.75C](https://www.waveshare.com/esp32-s3-touch-amoled-1.75c.htm) |
| **[Mobile Assistant](devices/mobile-assistant.yaml)** | Square Handheld | 2.16" Square AMOLED (`480×480`) | Edge-to-edge square UI, room sliders & album artwork in your hand | [ESP32-S3-Touch-AMOLED-2.16](https://www.waveshare.com/esp32-s3-touch-amoled-2.16.htm) |
| **[Wrist Assist](devices/wrist-assist.yaml)** | Smartwatch | 2.06" Rectangular AMOLED (`410×502`) | Hands-free wearable voice assistant, timers, alarms & quick room control | [ESP32-S3-Touch-AMOLED-2.06](https://www.waveshare.com/esp32-s3-touch-amoled-2.06.htm) |
| **[Remote Assistant](devices/remote-assistant.yaml)** | Widescreen Handheld | 3.5" Portrait Touch LCD (`320×480`) | Couch TV/media remote, multi-room controller & camera handheld | [ESP32-P4-WIFI6-Touch-LCD-3.5](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-3.5.htm) |
| **[Desk Assistant](devices/desk-assistant.yaml)** | Stationary Display | 4.0" Square HD IPS (`720×720`) | Always-on desk stand or wall-mounted room & whole-home control panel | [ESP32-P4-WIFI6-Touch-LCD-4B](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4b.htm) |

---

## Part of the Latter Ecosystem

| Repository | Role in the Ecosystem |
| :--- | :--- |
| **[Latter (`ha-latter`)](https://github.com/slampton/ha-latter)** | **Home Assistant Middleware & Web Studio** — Zero-YAML entity mapping, BLE room presence radar, Music Assistant browser, Baseline-JPEG transcoder, and backup vault. |
| 👉 **[Latter Core (`esphome-latter-core`)](https://github.com/slampton/esphome-latter-core)** | **Shared ESPHome OS, Device Catalog & 1-Line Mixin SDK (You Are Here)** — Modular C++20 UI engine, board HALs (`boards/`), turnkey device configs (`devices/` for **Pocket Assistant**, **Mobile Assistant**, **Wrist Assist**, **Remote Assistant**, and **Desk Assistant**), and 1-line drop-in mixins. |

---

## Developer Documentation & Compilable Examples

- 🧪 **[Example 1: Minimal Headless / Mixin Node (`examples/minimal_headless_companion.yaml`)](examples/minimal_headless_companion.yaml)** — Compiles on any bare $5 ESP32-S3 DevKit with zero external peripherals; demonstrates `modules/base.yaml`, `modules/room_control.yaml`, and `mixins/ble_beacon.yaml`.
- 🖥️ **[Example 2: Custom 480×480 Display Porting Template (`examples/custom_display_template.yaml`)](examples/custom_display_template.yaml)** — Complete, CI-validated reference HAL & manifest for porting the full Latter C++20 UI OS (`components/latter_core`, `include/*.h`, `modules/audio_voice.yaml`, `modules/power_generic.yaml`, `modules/display_ui.yaml`) to a new screen.
- 🔌 **[Latter Wire & API Contract (`docs/API_CONTRACT.md`)](docs/API_CONTRACT.md)** — Complete specification of the 5 `sensor.<slug>_*` push sensors, ESPHome API actions (`set_browse_slots`, `set_room_slots`, `show_notification`, `set_timer`, `cancel_timer`, `latter_ping`), and outbound `latter.*` services for building custom clients.
- 🛠️ **[Porting & New Hardware Guide (`docs/PORTING_NEW_HARDWARE.md`)](docs/PORTING_NEW_HARDWARE.md)** — How to bring up a new screen size/shape (`ui_layout.h`), power backend (`power_axp2101.yaml` vs. `power_generic.yaml`), or **ESP32-P4** board in 15 minutes.

---

## 1. One-Line Drop-In Mixins for Any ESPHome Device

Paste any of these single-line packages into an existing ESPHome device's YAML to unlock Latter capabilities immediately:

### A. Roaming BLE Room Presence Beacon (`mixins/ble_beacon.yaml`)
Broadcasts a calibrated iBeacon (`504F434B-4554-4153-5354-000000000210`) + exposes a `"Room: BLE Presence Beacon"` switch so Latter tracks which room the device is in:
```yaml
packages:
  latter.ble: github://slampton/esphome-latter-core/mixins/ble_beacon.yaml@main
```

### B. Stationary Room BLE Scanner Proxy (`mixins/ble_proxy.yaml`)
Turns any mains-powered ESP32 node into an active room Bluetooth scanner for Latter's Room Calibration radar:
```yaml
packages:
  latter.proxy: github://slampton/esphome-latter-core/mixins/ble_proxy.yaml@main
```

### C. Roaming WireGuard VPN Tunnel (`mixins/roaming_vpn.yaml`)
Keeps a portable ESPHome device connected to Home Assistant over phone hotspots with live VPN diagnostic sensors:
```yaml
packages:
  latter.vpn: github://slampton/esphome-latter-core/mixins/roaming_vpn.yaml@main
```

### D. Core Studio Discovery & Diagnostics (`mixins/diagnostics.yaml`)
Adds Uptime, Wi-Fi Signal, Restart button, and automatic Latter discovery:
```yaml
packages:
  latter.diag: github://slampton/esphome-latter-core/mixins/diagnostics.yaml@main
```

---

## 2. Repository Architecture (`v2.2+`)

```text
esphome-latter-core/
├── components/
│   └── latter_core/
│       └── __init__.py          # External component: stages all C++20 headers from include/ into build src/
├── include/                     # Modular C++20 UI, Shader, Physics & Audio Headers
│   ├── va_cancel_helper.h       # Voice Assistant session lifecycle, NVS wake word persistence & mono downmixer
│   ├── ui_core.h                # Display traits, .rodata trig LUTs, 3D math, UTF-8 sanitizer & scanline blitter
│   ├── ui_layout.h              # Resolution/shape-adaptive UiLayout metrics & ESP32-S3 / ESP32-P4 HAL helpers
│   ├── ui_ambient_va.h          # 20 FPS Aurora, Starfield, Matrix & Fireworks screensavers + 5 VA profiles
│   ├── ui_weather_sky.h         # 20 FPS Weather motion shaders, 5-day forecast, sun & synodic moon engine
│   ├── ui_games.h               # 4 IMU/Touch arcade games (Marbles, Archery, Breakout, Chomp)
│   ├── ui_tools.h               # 12-Tool Suite (Timers, Alarms, Chrono, Level, Calc, Calendar+Map, Intercom, Cam)
│   ├── ui_music.h               # Now Playing HUD, Track Inspector, Lyrics/Bio Reader & MA Library Browser
│   ├── ui_clock_home.h          # Analog/Digital Watch Faces, Live Activity Stack, Wake Greeting & Sliding Dock
│   ├── ui_room_home_hub.h       # Area Room Control (3-card fill-sliders + RGB/Kelvin) & Whole-Home Hub
│   ├── ui_remote.h              # Universal 4-Slot TV & AVR Remote
│   ├── ui_system.h              # System Settings, 5-Ring HSV Color Wheel & Diagnostics
│   └── ui_tools_apps.h          # Umbrella header exporting pocket_ui
├── boards/
│   ├── waveshare_s3_175c.yaml   # Pocket Assistant HAL (1.75" 466x466 Round AMOLED, S3)
│   ├── waveshare_s3_206.yaml    # Wrist Assist HAL     (2.06" 410x502 Watch AMOLED, S3)
│   ├── waveshare_s3_216.yaml    # Mobile Assistant HAL (2.16" 480x480 Square AMOLED, S3)
│   ├── waveshare_p4_35.yaml     # Remote Assistant HAL (3.5" 320x480 IPS LCD + Camera, P4)
│   └── waveshare_p4_4b.yaml     # Desk Assistant HAL   (4.0" 720x720 MIPI-DSI Panel, P4)
├── devices/
│   ├── pocket-assistant.yaml    # Turnkey Pocket Assistant config
│   ├── wrist-assist.yaml        # Turnkey Wrist Assist config
│   ├── mobile-assistant.yaml    # Turnkey Mobile Assistant config
│   ├── remote-assistant.yaml    # Turnkey Remote Assistant config
│   └── desk-assistant.yaml      # Turnkey Desk Assistant config
├── modules/
│   ├── base.yaml                # Base identity, time sync & latter_ping
│   ├── audio_voice.yaml         # Sendspin 48kHz FLAC player, PSRAM mixer, MicroWakeWord & Assist pipeline
│   ├── power_axp2101.yaml       # AXP2101 PMU 3-tier power state machine (Active, Standby, <40uA Hibernate)
│   ├── power_generic.yaml       # Generic power & screen-timeout state machine for non-AXP2101 / ESP32-P4 panels
│   ├── display_ui.yaml          # Unified lifecycle, Latter API contract actions, display renderer & touch router
│   └── room_control.yaml        # Standalone headless/custom room control client module
├── mixins/
│   ├── ble_beacon.yaml
│   ├── ble_proxy.yaml
│   ├── roaming_vpn.yaml
│   └── diagnostics.yaml
├── profiles/
│   ├── full_companion.yaml      # Bundle for AXP2101 battery handhelds/watches (audio_voice + power_axp2101 + display_ui)
│   └── panel_companion.yaml     # Bundle for non-AXP2101 / ESP32-P4 panels (audio_voice + power_generic + display_ui)
├── examples/
│   ├── minimal_headless_companion.yaml # Zero-peripheral ESP32-S3 demo (base + room_control + ble_beacon)
│   └── custom_display_template.yaml    # 480x480 square display porting template & CI target
└── docs/
    ├── API_CONTRACT.md          # Complete Latter wire & service contract specification
    └── PORTING_NEW_HARDWARE.md  # Step-by-step ESP32-S3 / ESP32-P4 board HAL porting guide
```

---

## Support the Project

If you enjoy using **Latter**, **Pocket Assistant**, or **Wrist Assist**, consider supporting ongoing hardware bring-up and open-source development:

- ☕ **[Buy Me a Coffee](https://www.buymeacoffee.com/slampton)**
- ❤️ **[Sponsor on GitHub](https://github.com/sponsors/slampton)**

---

## Development & AI Disclosure

This project is architected, hardware-validated, and maintained by **Sean Lampton** ([Lampton Labs](https://github.com/slampton)), a Senior Systems Engineer. AI coding assistants are used as a pair-programming accelerator for C++20 shader math, Python boilerplate, and documentation—while all system architecture, ESP32-S3 hardware/PMIC/I2S bus engineering, power-state validation, and real-world Home Assistant testing are human-directed and verified on physical hardware.

---

## License

Licensed under the **[Apache License 2.0](LICENSE)**.
