# Latter Core

Turnkey firmware and a compiled C++20 UI/OS for five off-the-shelf Waveshare ESP32-S3 and ESP32-P4 touch displays, plus a trait-driven SDK for building your own display or headless companion node. Built on [ESPHome](https://esphome.io) and paired with the **[Latter](https://github.com/slampton/ha-latter)** Home Assistant integration, every device adapts to the Area it is in at runtime—with zero hardcoded entity IDs and zero YAML reflashing.

[![Validate](https://github.com/slampton/esphome-latter-core/actions/workflows/validate.yml/badge.svg)](https://github.com/slampton/esphome-latter-core/actions/workflows/validate.yml)
[![Release](https://img.shields.io/github/v/release/slampton/esphome-latter-core)](https://github.com/slampton/esphome-latter-core/releases)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)

| Repository | Runs on | Responsibility |
| :--- | :--- | :--- |
| **[ha-latter](https://github.com/slampton/ha-latter)** | Home Assistant | Integration and **Latter Studio**: live entity mapping, BLE Area presence, signed LAN image/video/audio proxy, and automations. **Start there** if you're setting up a home. |
| **esphome-latter-core** *(this repo)* | ESP32-S3 / ESP32-P4 | Compiled C++20 firmware (`latter_core`, `latter_ui`, `latter_beacon`), [Web Installer](https://slampton.github.io/esphome-latter-core/), and hardware/UI SDK. |

---

## What Runs on the Device

| App | Highlights |
| :--- | :--- |
| **Home** | Digital clock, live weather sky, native Day & Night Sky, or `720p` wallpaper, adaptive `0/1/2`-row **Activity Card** (calls, timers, alarms, stopwatch, music queue, next event), and customizable up-to-24-item app dock (with dynamic active-Area icon, or locked single-app kiosk mode) |
| **Clock** | Three zero-chrome SDF clock faces (**Minimal Digital**, **Modern Analog** on round panels, **Hybrid Digital**) with smooth 20 FPS sweep on USB and for 15 s after wake on battery |
| **Time** | 4-tab app: **Clock** (optional custom local label, DST/zone info, UTC, and up to 4 custom-city World Clocks), **Timers** (3 local slots + Assist timers), **Alarms** (3 recurring NVS slots synced 2-way with Studio), and **Stopwatch** (lap splits) |
| **Music** | Now Playing with full-bleed art, seek/skip, speaker picker, one-tap Music Assistant queue transfer, speaker grouping, library browser, LRC/plain lyrics, and Music Assistant artist/album metadata |
| **Area Control** | Entity cards and dedicated inspector scoped to the device's current Area (Scenes, Lights with HSV wheel & 2000–6500 K slider, Blinds & Curtains, Fans, Switches, Climate, Media) |
| **Home Hub** | 5-tab whole-home deck: **Alarm**, **Broadcast** (`Speak` inline STT + Quick Phrases), **Thermostat**, **Entry & Garage**, and **Vacuum**, with separate HA Alarm PIN vs. `v1:` session-key-wrapped Perimeter Security PIN prompts (`4–12` digits) |
| **Cameras** | 4-tab live MJPEG app (**Grid** 2×2, **Single** uncropped feed + doorbell listen/PTT talk/reply/unlock, **Carousel**, and **Settings**) plus live doorbell ring popups |
| **Remote** | Tactile D-pad (with `480 ms` hold-in-place `OK`/`Home`/`Back` long-press), transport, volume, and power for up to 10 TVs, streaming boxes, or AV receivers |
| **Sky & Weather** | Current conditions, 5-day forecast, solar arc, and real-albedo 3D lunar phase over animated clouds, fog, rain, snow, plasma-bolt storms, tumbling leaves, and night meteors |
| **Calendar & Tools** | Dedicated **Calendar** app (`Overview`, `Day`, `Week`, `Month` + `+ Add Event` composer & `zone.*` location picker), plus **Tools** (Calculator, Dice, Coin Flip, Flashlight, Bubble Level on IMU boards, and Analog Chronograph on round displays) |
| **Voice** | On-device [microWakeWord](https://github.com/kahrendt/microWakeWord) (*Okay Nabu*, *Hey Jarvis*, *Hey Mycroft*, *Alexa*, *Hey Luna*) into Home Assistant Assist with 5 reactive visual overlays |
| **Games** | Optional IMU/touch mini-games: Marbles, Archery, Bricks, Chomp, 2048, and Memory Match (tilt-steered with touch fallback on IMU boards; touch-steered elsewhere) |

→ [Using your device](docs/using-your-device.md)

---

## Supported Hardware & 3-Minute Setup

| Device | Waveshare Hardware | Display | Best For |
| :--- | :--- | :--- | :--- |
| **Pocket Assistant** | [ESP32-S3-Touch-AMOLED-1.75C][ws-175c] | 1.75" round AMOLED, 466×466 | Carrying Area to Area; voice, music, and quick Area controls |
| **Mobile Assistant** | [ESP32-S3-Touch-AMOLED-2.16][ws-216] | 2.16" square AMOLED, 480×480 | Handheld with extra space for album art, sliders, and browsing |
| **Wrist Assist** | [ESP32-S3-Touch-AMOLED-2.06][ws-206] | 2.06" watch AMOLED, 410×502 | Wearable timers, alarms, Assist, and per-person BLE presence |
| **Remote Assistant** | [ESP32-P4-WIFI6-Touch-LCD-3.5][ws-p4-35] | 3.5" IPS, 320×480 | Fast P4 couch remote for TVs, AV receivers, and whole-home control |
| **Desk Assistant** | [ESP32-P4-86-Panel-ETH-2RO / WIFI6-Touch-LCD-4B][ws-p4-desk] | 4.0" IPS, 720×720 | Always-on desk or wall panel with sharp HD typography and wake word |

<sub>Hardware links above are Waveshare affiliate links—buying through them supports Latter at no extra cost to you. (On the Desk Assistant page, both `ESP32-P4-86-Panel-ETH-2RO` and `ESP32-P4-WIFI6-Touch-LCD-4B` share the same mainboard and are validated.)</sub>

All five boards run the **exact same firmware and universal GUI**. Each board file (~80–160 lines of YAML) declares only pins, buses, and `LATTER_*` capability traits; layout, slot counts, touch hit targets, and power policy adapt at compile time. → [Hardware specs & pin maps](docs/hardware.md)

### Quickstart
1. **Pick a board** from the table above and connect it to your computer with a USB-C data cable.
2. **Flash in Chrome or Edge** using the **[Latter Web Installer](https://slampton.github.io/esphome-latter-core/)** (~2 minutes, including Wi-Fi setup over USB via Improv Serial), or compile in ESPHome Device Builder. → [Flashing guide](docs/flashing.md)
3. **Adopt in Home Assistant**: add the discovered ESPHome device (leave *"Allow the device to perform Home Assistant actions"* **off**), install **[Latter (`ha-latter`)](https://github.com/slampton/ha-latter)** via the Home Assistant Marketplace, accept **Discovered: Latter**, and open **Latter Studio** in the sidebar. → [Getting started walkthrough](https://github.com/slampton/ha-latter/blob/main/docs/getting-started.md)

---

## Under the Hood

```mermaid
flowchart LR
    DEV["ESP32-S3 / P4 Display<br/>(esphome-latter-core)"] -- "latter_rpc_outbox<br/>(20 bound RPC methods)" --> HA["Latter Integration<br/>(ha-latter)"]
    HA -- "latter_push_v3 & latter_set_slots<br/>(7 domains + signed Zero-WAN LAN media)" --> DEV
    BLE["ESPHome BLE Proxies"] -- "RSSI" --> HA
    HA <--> STUDIO["Latter Studio<br/>(live admin panel)"]
    HA <--> ECOSYS["HA Areas · Assist · Music Assistant"]
```

If you're evaluating the firmware or building on the SDK, these are the core engineering choices—and [why Latter uses a custom C++20 UI instead of LVGL](docs/architecture.md#why-custom-c20-ui-instead-of-lvgl):

- **`2px` Dirty-Band Compositor & Co-Located `FrameHitTable`.** Localized updates (clock ticks, timers, sliders, dock focus) align dirty rows `[y0, y1]` to full-width even-to-odd bands, preserving `MipiSpiBuffer`'s contiguous `stride == W` zero-copy DMA fast path while cutting SPI/QSPI bus traffic 5–12×. Interactive controls draw in row-major RGB565 with Bayer-dithered frosted glass, register their exact bounds into `FrameHitTable` (`kMaxRegions = 56`, `>= 7.0 mm` targets) as they render, and draw from 7,447 baked Material Design Icons (LZSS Bezier bytecode with `4×` AA). → [`ui_compositor.h`](include/ui_compositor.h), [`ui_gfx.h`](include/ui_gfx.h), [`ui_hit_table.h`](include/ui_hit_table.h), [`ui_mdi.h`](include/ui_mdi.h)
- **Procedural `WeatherSkyEngine` & Zero-Trig Hot Path.** Static atmospheric gradients, star domes, cumulus canopies, and a 139×139 spherical-terminator lunar disc bake once into a PSRAM `SkyBaseCache`. Stateless rain, snow, and plasma-corona lightning composite on top in Q8 fixed-point using a 60-entry trig LUT and flash bezel SDFs, keeping `sinf` and `sqrtf` out of per-pixel loops (animating continuously on USB, or for 35 s after wake on battery before fading to 0 FPS). → [`page_weather.cpp`](components/latter_ui/page_weather.cpp), [`ui_types.h`](include/ui_types.h)
- **Zero-Privilege Bound RPC & Sleep-Safe NVS Sync.** Outbound actions emit chunked envelopes on the hidden `latter_rpc_outbox` pipe (with `v1:` HMAC-SHA256 session-key wrapping on `4–12` digit PINs), while inbound state arrives across 7 Protocol v3 domains via `latter_push_v3` and `latter_set_slots` without `allow_service_calls` or `device_slug` substitutions. Storing all 63 preferences in one append-only NVS struct (`PersistedDevicePrefs` v1 + cold-boot `HibernateSnapshot` v3) saves ESP32-S3 internal SRAM and reconciles offline edits on `device_hello`. → [`latter_rpc.h`](include/latter_rpc.h), [`device_runtime_state.h`](include/state/device_runtime_state.h), [`DEVICE_CONTRACT.md`](DEVICE_CONTRACT.md)
- **Full-Duplex 48 kHz Audio & Synchronous Voice Preemption.** Every board runs DAC and ADC together on one shared-clock 48 kHz I2S bus (resampling mic input to 16 kHz for wake word and Assist), eliminating I2S teardown clicks and bus handoffs. `AudioPathArbitrator` and `AudioSessionController` pause media and drain mixer buffers synchronously on voice turns, mute wake-word listening over local playback until on-device AEC lands, and never resume music over a ringing alarm or timer. → [`audio_router.h`](include/audio_router.h), [`audio_session.cpp`](components/latter_core/audio_session.cpp), [`audio_pipeline.yaml`](modules/audio_pipeline.yaml)
- **S3 Light Sleep, `< 1 ms` Raw-HCI Beacon & IRAM-Safe P4 DSI.** ESP32-S3 battery boards use FreeRTOS tickless light sleep (`~5–8 mA` quiet with BLE paused, `~24–35 mA` with BLE modem-sleep active, `~2–4 mA` off-site with scan backoff), while `latter_beacon` speaks raw HCI over VHCI or `esp_hosted` with zero Bluedroid/NimBLE RAM overhead and `< 1 ms` warm transitions. ESP32-P4 panels keep fixed `CPLL` + `200 MHz` PSRAM and pair ESPHome `2026.11.0+` native IRAM MIPI-DSI callbacks with IRAM GDMA ISRs so the 720×720 panel stays tear-free during OTA flash erases. → [`pm_light_sleep.cpp`](components/latter_core/pm_light_sleep.cpp), [`latter_beacon.cpp`](components/latter_beacon/latter_beacon.cpp), [`power_battery.cpp`](components/latter_core/power_battery.cpp), [`waveshare_p4_86_panel.yaml`](boards/waveshare_p4_86_panel.yaml)
- **Adaptive Frame Cadence & `[PROF:*]` Telemetry Profiler.** `FrameCadenceController` coalesces same-loop redraws into 18 ms frames on P4 and 26 ms on S3, stepping ambient screensavers at `16.7 FPS` (`12.5 FPS` during audio). Studio's **Developer & telemetry** toggle enables live `[PROF:*]` telemetry profiler logs over `device_telemetry` RPC alongside `RTC_NOINIT` `PmCrumb` crash breadcrumbs. → [`ui_compositor.h`](include/ui_compositor.h), [`latter_prof.h`](include/latter_prof.h)

---

## Build on Latter Core

The firmware is built around three zero-cycle compiled C++20 PlatformIO components (`latter_core`, `latter_ui`, `latter_beacon`) with coarse component/header boundaries, so you can swap the board HAL, replace the GUI, or run a headless node without forking the OS:

```text
components/  Compiled C++20 SDK: latter_core (state, HAL, Protocol v3, RPC, audio/power), latter_ui (headless UI + display bridge), latter_beacon (raw-HCI BLE)
boards/      Board HAL + facts ledger: pins, buses, codecs, PMU, IMU, RTC, and LATTER_* traits
modules/     Core OS YAML: base, system_core & system_ui_sync, power_policy & sleep_lifecycle, audio_pipeline & voice_assistant, display_pipeline
guis/        Universal GUI: display lambda + UiTouchEngine / FrameHitTable (replaceable; base.yaml for headless)
devices/     Turnkey product configs combining one board HAL with latter_display_firmware.yaml
```

| Guide | When to read it |
| :--- | :--- |
| **[Architecture](docs/architecture.md)** | How the 3 C++20 components, zero-cycle coarse boundaries, YAML modules, custom UI rationale, and S3/P4 power policies fit together |
| **[Device protocol](docs/device-protocol.md)** & **[Contract](DEVICE_CONTRACT.md)** | How `ha-latter` discovers, pushes 7 Protocol v3 domains to, and receives 20 RPC methods from a device |
| **[Building a GUI](docs/building-a-gui.md)** | Writing a custom touch or rotary interface on top of `latter_core` |
| **[Device UI design guide](docs/device-ui-design.md)** | Design tokens, 2-tier glass, layout math, `FrameHitTable`, and rendering budgets |
| **[Porting hardware](docs/porting-hardware.md)** | Bringing up a new ESP32-S3 or ESP32-P4 board with `boards/facts/*.json` and `tools/new_board.py` |

---

## Project Status

Version **3.0.0** — built, bench-validated, and maintained across all five flagship devices above.

| Area | Status |
| :--- | :--- |
| Five flagship devices, universal GUI, 7-domain Protocol v3 sync, full-duplex audio, voice, and power | **Stable** |
| Doorbell listen & push-to-talk reply | **Experimental**: camera audio streams to the device with FFmpeg noise reduction; push-to-talk transcribes speech and speaks it at the door via TTS (full-duplex calls with on-device AEC planned) |

- Every push to `dev` passes static architecture checks, 5-geometry ASan/UBSan C++20 host tests, and full 6-variant ESPHome firmware compilation before CI fast-forwards `main`, alongside a weekly `esphome:dev` canary (see [`CONTRIBUTING.md`](CONTRIBUTING.md) for the local and CI verification checklist).
- Bugs and feature requests: [GitHub Issues](https://github.com/slampton/esphome-latter-core/issues).

## Documentation

| | |
| :--- | :--- |
| **Use** | [Hardware](docs/hardware.md) · [Flashing](docs/flashing.md) · [Using your device](docs/using-your-device.md) |
| **Build** | [Architecture](docs/architecture.md) · [Device protocol](docs/device-protocol.md) · [Protocol v3 contract](DEVICE_CONTRACT.md) · [Building a GUI](docs/building-a-gui.md) · [Device UI design](docs/device-ui-design.md) · [Porting hardware](docs/porting-hardware.md) |

## Credits & Support

Built on [ESPHome](https://esphome.io), [microWakeWord](https://github.com/kahrendt/microWakeWord), [Music Assistant](https://music-assistant.io) & Sendspin, [Pictogrammers Material Design Icons](https://pictogrammers.com/library/mdi/), and [ESP Web Tools](https://esphome.github.io/esp-web-tools/). The *Hey Luna* wake word model is by [Daniel Reimer](https://github.com/dreimer1986/micro-wake-word-models) ([`models/`](models/)).

Latter is free and open source ([Apache License 2.0](LICENSE)), maintained by Sean Lampton (AI coding assistants are used for drafting code and docs; design direction, hardware bring-up, and bench testing are done by hand). Hardware bring-up costs real money—if Latter is useful to you:
- [GitHub Sponsors](https://github.com/sponsors/slampton) · [Buy Me a Coffee](https://www.buymeacoffee.com/slampton)

[ws-175c]: https://www.waveshare.com/esp32-s3-touch-amoled-1.75c.htm?&aff_id=140049
[ws-216]: https://www.waveshare.com/esp32-s3-touch-amoled-2.16.htm?&aff_id=140049
[ws-206]: https://www.waveshare.com/esp32-s3-touch-amoled-2.06.htm?&aff_id=140049
[ws-p4-35]: https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-3.5.htm?&aff_id=140049
[ws-p4-desk]: https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4b.htm?&aff_id=140049
