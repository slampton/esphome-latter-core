# ESPHome Latter Core (`esphome-latter-core`)

[![Release](https://img.shields.io/badge/version-2.2.1-blue.svg)](https://github.com/slampton/esphome-latter-core)
[![ESPHome](https://img.shields.io/badge/ESPHome-2026.10.0%2B-000000.svg)](https://esphome.io/)

**`esphome-latter-core`** is the shared C++20 operating system, resolution-adaptive UI engine, and 1-line YAML mixin SDK for **[Latter](https://github.com/slampton/ha-latter)** companion devices in Home Assistant.

It serves two purposes:
1. **1-Line Drop-In Mixins (`mixins/*.yaml`)**: Upgrade any existing 3rd-party ESPHome device with roaming BLE room presence, stationary BLE proxy scanning, WireGuard roaming VPN, or Studio diagnostics—with **zero substitutions required**.
2. **Full Companion Display OS (`components/latter_core`, `modules/*.yaml`, `include/*.h`)**: The single-source-of-truth C++20 UI, audio/voice pipeline, and 3-tier power state machine shared by flagship devices like **[Pocket Assistant](https://github.com/slampton/esphome-pocket-assistant)** (`466x466` round AMOLED) and **[Wrist Assist](https://github.com/slampton/esphome-wrist-assist)** (`410x502` rectangular watch), and ready for upcoming **ESP32-P4** square/round MIPI-DSI panels.

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
└── profiles/
    ├── full_companion.yaml      # Bundle for AXP2101 battery handhelds/watches (audio_voice + power_axp2101 + display_ui)
    └── panel_companion.yaml     # Bundle for non-AXP2101 / ESP32-P4 panels (audio_voice + power_generic + display_ui)
```

---

## 3. Building a New Companion Display (ESP32-S3 or ESP32-P4)

Because `components/latter_core` and `modules/display_ui.yaml` live in `esphome-latter-core`, creating a new device repository requires **zero duplicated UI YAML or `.h` files**:

1. Create a board HAL file (`boards/my_board.yaml`) defining your display, touchscreen, I2S audio pins, and compile-time geometry flags:
   ```yaml
   esphome:
     build_flags:
       - "-DPOCKET_UI_SCREEN_W=480"
       - "-DPOCKET_UI_SCREEN_H=480"
       - "-DPOCKET_UI_ROUND_DISPLAY=0"
   ```
2. In your top-level device manifest, set `screen_w`, `screen_h`, `art_resize`, and import `esphome-latter-core`:
   ```yaml
   external_components:
     - source:
         type: git
         url: https://github.com/slampton/esphome-latter-core
         ref: main
       components: [latter_core]

   latter_core:

   packages:
     hal: !include boards/my_board.yaml
     latter_core:
       url: https://github.com/slampton/esphome-latter-core
       ref: main
       files:
         - modules/audio_voice.yaml
         - modules/power_generic.yaml
         - modules/display_ui.yaml
   ```
