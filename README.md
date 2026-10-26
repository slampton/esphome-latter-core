# ESPHome Latter Core (`esphome-latter-core`)

[![Version](https://img.shields.io/badge/Version-v2.1.0-03a9f4.svg)](https://github.com/slampton/esphome-latter-core)
[![ESPHome](https://img.shields.io/badge/ESPHome-2026.10.0%2B-blue.svg)](https://esphome.io)
[![Backend](https://img.shields.io/badge/Backend-ha--latter%20v2.1%2B-41BDF5.svg)](https://github.com/slampton/ha-latter)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)

**The universal ESPHome SDK, modular firmware core, and 1-line mixin library for [Latter (`ha-latter`)](https://github.com/slampton/ha-latter).**

`esphome-latter-core` separates **shared ESPHome system engines, Latter wire contracts, audio/voice pipelines, power management, and graphics primitives** from **device-specific board pinouts (`boards/`) and form-factor GUIs (`ui/`)**.

Whether you are building a flagship circular handheld (**[Pocket Assistant](https://github.com/slampton/esphome-pocket-assistant)**), a rectangular smartwatch (**Wrist Assist**), a living-room touchscreen remote, a stationary wall panel, or upgrading an existing 3rd-party ESPHome sensor, `esphome-latter-core` provides modular packages you can include in a single YAML line.

---

## 1. One-Line Drop-In Mixins (`mixins/`)

Add any of these 1-line packages under `packages:` in an existing ESPHome device's YAML to unlock Latter capabilities with zero substitution variables required:

### A. Roaming BLE Room Presence Beacon (`mixins/ble_beacon.yaml`)
Makes any portable or battery-powered ESP32 device trackable room-to-room by Latter's BLE proxy engine:
```yaml
packages:
  latter.ble: github://slampton/esphome-latter-core/mixins/ble_beacon.yaml@main
```

### B. Roaming WireGuard VPN Tunnel (`mixins/roaming_vpn.yaml`)
Adds encrypted remote access over a phone hotspot or travel Wi-Fi + live VPN status & handshake sensors in Latter Studio:
```yaml
packages:
  latter.vpn: github://slampton/esphome-latter-core/mixins/roaming_vpn.yaml@main
```

### C. Stationary Room BLE Scanner Proxy (`mixins/ble_proxy.yaml`)
Turns any mains-powered ESP32 node into an active room Bluetooth scanner for Latter's Room Calibration radar:
```yaml
packages:
  latter.proxy: github://slampton/esphome-latter-core/mixins/ble_proxy.yaml@main
```

### D. Core Studio Discovery & Diagnostics (`mixins/diagnostics.yaml`)
Adds Uptime, Wi-Fi Signal, Restart button, and automatic Latter discovery for custom nodes:
```yaml
packages:
  latter.diag: github://slampton/esphome-latter-core/mixins/diagnostics.yaml@main
```

---

## 2. Modular Native Device Building Blocks (`modules/` & `profiles/`)

When building a native Latter device (like **Pocket Assistant** or **Wrist Assist**), combine a **Board HAL**, **Latter Core Modules**, and your **Form-Factor UI**:

| Path | Responsibility |
| :--- | :--- |
| `modules/base.yaml` | Core Latter identity, time sync, and API handshake. |
| `modules/room_control.yaml` | Area-aware room & whole-home control client (`room_context`, `fetch_room_browse`, `execute_room_action`). |
| `modules/audio_voice.yaml` | Sendspin lossless FLAC pipeline, Mixer, 5-model `micro_wake_word`, Voice Assistant state machine, and RTTTL chimes. |
| `modules/power_axp2101.yaml` | 3-tier power state machine (`Active` -> `Standby` -> `<40 µA` AXP2101 Hardware Hibernate), fuel gauge smoothing, and `GPIO3` IRQ wake. |
| `include/ui_core.h` | Display-shape-agnostic geometry (`POCKET_UI_SCREEN_W`, `POCKET_UI_SCREEN_H`, `POCKET_UI_ROUND_DISPLAY`), `.rodata` trig LUTs, 3D projection, UTF-8 font sanitizer, and scanline blitter. |
| `include/va_cancel_helper.h` | Voice Assistant session lifecycle, `L+R -> Mono` Sendspin downmixer, dual-slot wake word NVS persistence, and clean API disconnect. |
| `include/ui_weather_sky.h` | 20 FPS 3D Weather Sky shaders & Telescopic Lunar Albedo renderer. |
| `include/ui_ambient_va.h` | 5 Docked 20 FPS Screensavers & 5 Voice Assistant visual profiles. |
| `include/ui_games.h` | 360° IMU tilt calibration & 4-game 20 FPS arcade physics engine. |

---

## License

Distributed under the Apache 2.0 License. Built and maintained by Sean Lampton (**Lampton Labs**).