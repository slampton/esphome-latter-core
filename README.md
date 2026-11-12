# Latter Core

Firmware for five Waveshare ESP32 touch displays, plus an SDK for building your own Latter device. Everything here is built on [ESPHome](https://esphome.io) and works with the [Latter](https://github.com/slampton/ha-latter) Home Assistant integration.

[![Validate](https://github.com/slampton/esphome-latter-core/actions/workflows/validate.yml/badge.svg)](https://github.com/slampton/esphome-latter-core/actions/workflows/validate.yml)
[![Release](https://img.shields.io/github/v/release/slampton/esphome-latter-core)](https://github.com/slampton/esphome-latter-core/releases)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)

<!-- MEDIA: docs/media/hero.jpg — all five devices on a desk, each showing a different app. -->

| Repository | Runs on | Responsibility |
| :--- | :--- | :--- |
| **[ha-latter](https://github.com/slampton/ha-latter)** | Home Assistant | The integration and the Studio panel: entity mapping, area presence, image re-encoding, and services. **Start there** if you're setting up a device. |
| **esphome-latter-core** (this repo) | The ESP32 | Firmware, the web installer, and the SDK. |

---

## Devices

<!-- MEDIA: one front-view photo per device on a plain background (docs/media/device-<slug>.jpg), shown as a row of thumbnails above this table. -->

| Device | Hardware | Screen | Good for |
| :--- | :--- | :--- | :--- |
| **Pocket Assistant** | [ESP32-S3-Touch-AMOLED-1.75C][ws-175c] | 1.75" round AMOLED, 466×466 | Carrying around the house; voice and music control |
| **Mobile Assistant** | [ESP32-S3-Touch-AMOLED-2.16][ws-216] | 2.16" square AMOLED, 480×480 | A handheld with more screen for album art and area controls |
| **Wrist Assist** | [ESP32-S3-Touch-AMOLED-2.06][ws-206] | 2.06" watch AMOLED, 410×502 | Wearing it: timers, alarms, voice, and per-person presence |
| **Remote Assistant** | [ESP32-P4-WIFI6-Touch-LCD-3.5][ws-p4-35] | 3.5" IPS, 320×480 | A couch remote for TVs and media |
| **Desk Assistant** | [ESP32-P4-86-Panel-ETH-2RO][ws-p4-86] | 4" IPS, 720×720 | An always-on desk or wall panel |

<sub>Some hardware links are affiliate links. Buying through them supports the project at no extra cost to you.</sub>

All five run the same firmware and GUI. The only per-board code is a hardware abstraction file that describes the display, touch, audio, and power hardware. Layout adapts to screen size and shape at compile time. → [Specs and what else you need](docs/hardware.md)

## What's on the device

<!-- MEDIA: docs/media/apps-tour.gif — swiping through Home → Music → Area → Weather on Mobile Assistant. -->

| App | What it does |
| :--- | :--- |
| **Clock & Home** | 3 clock faces, weather, calendar agenda, and a home dock you can reorder |
| **Music** | Now playing; browse Music Assistant; lyrics and bios; move playback between speakers; group speakers |
| **Area Control** | Slider cards for lights, covers, fans, climate, and media in your current area; color wheel and color temperature |
| **Whole-Home Hub** | Alarm panel, thermostat, garage and locks, vacuum |
| **Remote** | Up to 10 TVs, streaming boxes, or AV receivers; D-pad, volume, power |
| **Sky & Weather** | Animated rain, snow, fog, and storm effects; 5-day forecast; sun position; moon phase |
| **Tools** | Timers, alarms, stopwatch, calculator, calendar with event maps, intercom, camera, and a few small extras (11 on IMU boards) |
| **Games** | Marbles, Archery, Bricks, Chomp, 2048, Memory Match. On boards with an IMU, the first four use tilt control. |
| **Voice** | On-device wake word (Okay Nabu, Hey Jarvis, Hey Mycroft, Alexa, Hey Luna) with Home Assistant Assist |

→ [Using your device](docs/using-your-device.md)

---

## Install

1. **Get a device** from the [table above](#devices).
2. **Flash it** from Chrome or Edge with the [web installer](https://slampton.github.io/esphome-latter-core/), or build it in ESPHome Device Builder. → [Flashing guide](docs/flashing.md)
3. **Set it up in Home Assistant** with the Latter integration. → [Getting started](https://github.com/slampton/ha-latter/blob/main/docs/getting-started.md)

---

## Build on Latter Core

The firmware is built in four YAML layers backed by three compiled C++20 PlatformIO components (`latter_core`, `latter_ui`, and `latter_beacon`), so you can replace any layer without touching the others:

```text
components/  Compiled SDK: latter_core (HAL, Protocol v3, RPC, OTA), latter_ui, latter_beacon
boards/      Board HAL (~80 lines each + modules/hal/): display, touch, codecs, PMU, IMU, LATTER_* flags
modules/     Core YAML: Zero-Privilege RPC + Protocol v3 client, full-duplex audio & voice, power policy
guis/        The GUI: display lambda + UiTouchEngine / FrameHitTable (replaceable; gui: "none" for headless)
devices/     Top-level configs that combine one board, the core modules, and one GUI
```

| Guide | When you need it |
| :--- | :--- |
| [Architecture](docs/architecture.md) | You want to understand how the pieces fit together |
| [Device protocol](docs/device-protocol.md) | You're building a device that talks to Latter, with or without this SDK |
| [Building a GUI](docs/building-a-gui.md) | You want your own interface on top of the core modules |
| [Device UI design guide](docs/device-ui-design.md) | You're changing the on-device interface: tokens, components, navigation, touch, rendering |
| [Porting hardware](docs/porting-hardware.md) | You want to run the firmware on a board that isn't listed |

---

## Engineering notes

These are the details worth checking if you're evaluating the codebase. Each one links to the code.

- **Row-major RGB565 blitting.** Album art and full-screen images are drawn row by row. Runs of identical 16-bit pixels are grouped into single horizontal-line calls, and scrim rows are dimmed in 1/256 with ordered Bayer dithering, keeping all 16 bits of color. → [`ui_gfx.h`](include/ui_gfx.h)
- **`2px`-aligned horizontal-band dirty flushes & co-located `FrameHitTable`.** Localized updates (clock ticks, timers, media progress, slider drags) align dirty vertical spans `[y0, y1]` to even-to-odd rows across full width `0 .. W − 1`, preserving `MipiSpiBuffer`'s contiguous `stride == W` zero-copy DMA fast path while cutting pixel bus traffic 5–12×. Interactive widgets register their exact rendered bounds into `FrameHitTable` as they draw, driving `UiTouchEngine` and focus rings with zero coordinate drift. → [`ui_compositor.h`](include/ui_compositor.h), [`ui_hit_table.h`](include/ui_hit_table.h), [`ui_touch_engine.h`](include/ui_touch_engine.h)
- **No `sqrtf` or `sinf` in the hot path.** Trig uses a 60-entry lookup table with linear interpolation. Round-screen clipping uses a 466-entry chord table stored in flash, plus integer Newton–Raphson. → [`ui_types.h`](include/ui_types.h)
- **Frame pacing and profiling.**
  - **Pacing:** redraw requests made in the same loop pass are merged into one redraw. Animations run at 18 ms per frame on the P4 and 26 ms on the S3. Ambient screensavers run at about 20 fps and slow down while audio plays.
  - **Profiling:** the **System: Telemetry Profiler** switch logs per-frame render times and flags slow frames. → [`ui_gfx.h`](include/ui_gfx.h), [`latter_prof.h`](include/latter_prof.h), [Device UI design guide](docs/device-ui-design.md#8-performance)
- **ESP32-P4 display stays alive during OTA.** The MIPI-DSI panel's frame-done callback is replaced, through a linker wrap isolated in `latter_core`, with an IRAM-resident version. Scanout then keeps running while flash is being erased. → [`panel_power.cpp`](components/latter_core/panel_power.cpp), [`esphome_bridge.cpp`](components/latter_core/esphome_bridge.cpp), [`waveshare_p4_86_panel.yaml`](boards/waveshare_p4_86_panel.yaml)
- **Zero-Privilege RPC & 3-tier Perimeter Security.** Outbound device actions emit compact frames on the diagnostic `System: RPC Outbox` (`latter_rpc::emit`) and inbound Protocol v3 domains push directly over the bound Native API (`latter_push_v3`)—requiring neither `allow_service_calls` nor `device_slug` YAML substitutions. Opening actions (unlocking doors, disarming alarms, opening garage/gate covers) prompt the on-device `ui::pin_pad_modal` when required by the device's Studio policy. → [`latter_rpc.h`](include/latter_rpc.h), [`ui_widgets.h`](include/ui_widgets.h)
- **Voice doesn't fight music.** When a voice turn starts (button, tap, or the wake word while music plays on another speaker), the firmware pauses music and stops its mixer input so the voice response isn't stuck behind buffered music. The wake word itself is paused while this device plays media, because there is no on-device echo cancellation yet. After the reply, it resumes only once both the media player and the announcement mixer have drained. Music never resumes over a ringing alarm or timer. → [`audio_voice.yaml`](modules/audio_voice.yaml), [`system_core.yaml`](modules/system_core.yaml)
- **Full-duplex 48 kHz audio on every board.** One I2S bus carries the DAC and the ADC on shared BCLK/WS/MCLK, so the microphone and speaker run at the same time and never hand the bus over. Music and announcements arrive at 48 kHz (no on-device output resampling); the microphone is resampled to 16 kHz for the wake word and Assist. → [`audio_es8311_duplex.yaml`](modules/hal/audio_es8311_duplex.yaml), [`audio_voice.yaml`](modules/audio_voice.yaml)
- **`5–8 mA` S3 light-sleep standby & clean AXP2101 hibernate.** On ESP32-S3 battery boards, screen-off quiet stretches the main loop to `1,000 ms` (`250 ms` when touch/raise-to-wake polling is armed; woken immediately by GPIO interrupts via `App.wake_loop_threadsafe()`) in `WIFI_PS_MAX_MODEM` (`~5–8 mA` at home; `~2–4 mA` off-site with exponential Wi-Fi backoff and paused `latter_beacon`). Hibernate saves state, disconnects from the API cleanly, puts codecs and the panel (`SLPIN`) into sleep, and cuts power on the AXP2101; on `Remote Assistant` (`waveshare_p4_35`), battery hibernate powers off the PMU cleanly (woken by `K1`), while scheduled alarms run whenever USB-docked. → [`power_pm.cpp`](components/latter_core/power_pm.cpp), [`pmu_axp2101.yaml`](modules/hal/pmu_axp2101.yaml)

---

## Project status

This is version **3.0.0**, built and maintained by one person and tested on the five devices above.

| Area | Status |
| :--- | :--- |
| The five flagship devices, the universal GUI, audio, voice, and power | Stable |
| Doorbell talkback | **Experimental**: push-to-talk. Your speech is transcribed and spoken at the door by Home Assistant; door audio plays on the device. Full-duplex calls with on-device echo cancellation are planned. |

Known limitations:
- **Wi-Fi setup after web flashing.** The web installer sets Wi-Fi over the USB cable (Improv Serial) on all five devices. → [Flashing](docs/flashing.md)
- **CI coverage.** CI runs `tools/verify_core.py`, `tools/gen_protocol.py --check`, `tools/check_boards.py --release --check-docs`, `tools/gen_sounds.py --check`, C++20 host unit tests, `esphome config` across both examples and all five device configs, and full 5-device firmware compilation on `dev` before promoting `main`.
- **Pinned to `main`.** Device configs pull the core from the `main` branch during development, and release builds pin every reference to the release tag. Work lands on `dev`; CI moves `main` forward only after every check passes and all five devices compile from that commit.

Bugs and feature requests: [issues](https://github.com/slampton/esphome-latter-core/issues).

## Documentation

| | |
| :--- | :--- |
| **Use** | [Hardware](docs/hardware.md) · [Flashing](docs/flashing.md) · [Using your device](docs/using-your-device.md) |
| **Build** | [Architecture](docs/architecture.md) · [Device protocol](docs/device-protocol.md) · [Building a GUI](docs/building-a-gui.md) · [Device UI design](docs/device-ui-design.md) · [Porting hardware](docs/porting-hardware.md) |

## Credits

The firmware stands on [ESPHome](https://esphome.io), [micro_wake_word](https://github.com/kahrendt/microWakeWord), [Music Assistant](https://music-assistant.io) and Sendspin, and [ESP Web Tools](https://esphome.github.io/esp-web-tools/). The Hey Luna wake word model is by [Daniel Reimer](https://github.com/dreimer1986/micro-wake-word-models) and is vendored in [`models/`](models/).

## Support

- [GitHub Sponsors](https://github.com/sponsors/slampton)
- [Buy Me a Coffee](https://www.buymeacoffee.com/slampton)

## How this was built

Latter is designed, tested on hardware, and maintained by Sean Lampton, a systems engineer. AI coding assistants are used for drafting code and documentation. Architecture decisions, hardware bring-up, and testing on physical devices are done by hand. If something here is wrong, please [open an issue](https://github.com/slampton/esphome-latter-core/issues).

## License

[Apache License 2.0](LICENSE)

[ws-175c]: https://www.waveshare.com/esp32-s3-touch-amoled-1.75c.htm
[ws-216]: https://www.waveshare.com/esp32-s3-touch-amoled-2.16.htm
[ws-206]: https://www.waveshare.com/esp32-s3-touch-amoled-2.06.htm
[ws-p4-35]: https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-3.5.htm
[ws-p4-86]: https://www.waveshare.com/esp32-p4-86-panel-eth-2ro.htm
