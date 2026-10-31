# Latter Core

Firmware for five Waveshare ESP32 touch displays, one-line packages for ESPHome devices you already run, and an SDK for building your own device. Everything here is built on [ESPHome](https://esphome.io) and works with the [Latter](https://github.com/slampton/ha-latter) Home Assistant integration.

[![Validate](https://github.com/slampton/esphome-latter-core/actions/workflows/validate.yml/badge.svg)](https://github.com/slampton/esphome-latter-core/actions/workflows/validate.yml)
[![Release](https://img.shields.io/github/v/release/slampton/esphome-latter-core)](https://github.com/slampton/esphome-latter-core/releases)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)

<!-- MEDIA: docs/media/hero.jpg — all five devices on a desk, each showing a different app. -->

| Repository | Runs on | Responsibility |
| :--- | :--- | :--- |
| **[ha-latter](https://github.com/slampton/ha-latter)** | Home Assistant | The integration and the Studio panel: entity mapping, area presence, image re-encoding, and services. **Start there** if you're setting up a device. |
| **esphome-latter-core** (this repo) | The ESP32 | Firmware, the web installer, mixins, and the SDK. |

---

## Devices

<!-- MEDIA: one front-view photo per device on a plain background (docs/media/device-<slug>.jpg), shown as a row of thumbnails above this table. -->

| Device | Hardware | Screen | Good for |
| :--- | :--- | :--- | :--- |
| **Pocket Assistant** | [ESP32-S3-Touch-AMOLED-1.75C][ws-175c] | 1.75" round AMOLED, 466×466 | Carrying around the house; voice and music control |
| **Mobile Assistant** | [ESP32-S3-Touch-AMOLED-2.16][ws-216] | 2.16" square AMOLED, 480×480 | A handheld with more screen for album art and area controls |
| **Wrist Assist** | [ESP32-S3-Touch-AMOLED-2.06][ws-206] | 2.06" watch AMOLED, 410×502 | Wearing it: timers, alarms, voice, and per-person presence |
| **Remote Assistant** | [ESP32-P4-WIFI6-Touch-LCD-3.5][ws-p4-35] | 3.5" IPS, 320×480 | A couch remote for TVs and media |
| **Desk Assistant** | [ESP32-P4-WIFI6-Touch-LCD-4B][ws-p4-4b] | 4" IPS, 720×720 | An always-on desk or wall panel |

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
| **Tools** | Timers, alarms, stopwatch, calculator, calendar with event maps, intercom, camera, macros, and a few small extras (12 on IMU boards) |
| **Games** | Marbles, Archery, Breakout, Chomp, 2048, Memory Match. On boards with an IMU, the first four use tilt control. |
| **Voice** | On-device wake word (Okay Nabu, Hey Jarvis, Hey Mycroft, Alexa, Hey Luna) with Home Assistant Assist |

→ [Using your device](docs/using-your-device.md)

---

## Install

1. **Get a device** from the [table above](#devices).
2. **Flash it** from Chrome or Edge with the [web installer](https://slampton.github.io/esphome-latter-core/), or build it in ESPHome Device Builder. → [Flashing guide](docs/flashing.md)
3. **Set it up in Home Assistant** with the Latter integration. → [Getting started](https://github.com/slampton/ha-latter/blob/main/docs/getting-started.md)

---

## Add Latter to an existing ESPHome device

Mixins are ESPHome packages you add with one line. Each one also makes the device discoverable by Latter.

| Mixin | Adds | Line |
| :--- | :--- | :--- |
| [`ble_proxy`](docs/mixins.md#ble_proxy) | Active Bluetooth proxy, so this node becomes a presence scanner for its area | `github://slampton/esphome-latter-core/mixins/ble_proxy.yaml@main` |
| [`ble_beacon`](docs/mixins.md#ble_beacon) | An iBeacon, so Latter can track which area this node is in | `github://slampton/esphome-latter-core/mixins/ble_beacon.yaml@main` |
| [`roaming_vpn`](docs/mixins.md#roaming_vpn) | WireGuard tunnel back to Home Assistant over any Wi-Fi | `github://slampton/esphome-latter-core/mixins/roaming_vpn.yaml@main` |
| [`diagnostics`](docs/mixins.md#diagnostics) | Uptime, Wi-Fi signal, restart button | `github://slampton/esphome-latter-core/mixins/diagnostics.yaml@main` |

```yaml
packages:
  latter_proxy: github://slampton/esphome-latter-core/mixins/ble_proxy.yaml@main
```

→ [All mixins, their requirements, and known conflicts](docs/mixins.md)

---

## Build on Latter Core

The firmware is built in four layers, so you can replace any one without touching the others:

```text
boards/      Board HAL: display, touch, audio codecs, PMU, IMU, LATTER_* capability flags
modules/     Core: Latter protocol client, audio and voice, power management
guis/        The GUI: rendering and touch handling (replaceable)
devices/     Top-level configs that combine one board, the core modules, and one GUI
```

| Guide | When you need it |
| :--- | :--- |
| [Architecture](docs/architecture.md) | You want to understand how the pieces fit together |
| [Device protocol](docs/device-protocol.md) | You're building a device that talks to Latter, with or without this SDK |
| [Building a GUI](docs/building-a-gui.md) | You want your own interface on top of the core modules |
| [Porting hardware](docs/porting-hardware.md) | You want to run the firmware on a board that isn't listed |

---

## Engineering notes

These are the details worth checking if you're evaluating the codebase. Each one links to the code.

- **Row-major RGB565 blitting.** Album art and full-screen images are drawn row by row. Runs of identical 16-bit pixels are grouped into single horizontal-line calls, and scrim rows are dimmed by shifting each color channel, which keeps all 16 bits of color. → [`ui_core.h`](include/ui_core.h)
- **Full-window flushes on non-round panels.** Each frame first touches two opposite corner pixels, so the dirty window covers the whole panel and the driver sends it in a single DMA transfer instead of several partial ones. → [`gui.yaml`](guis/universal/gui.yaml)
- **No `sqrtf` or `sinf` in the hot path.** Trig uses a 60-entry lookup table with linear interpolation. Round-screen clipping uses a 466-entry chord table stored in flash, plus integer Newton–Raphson. → [`ui_core.h`](include/ui_core.h)
- **Frame pacing and profiling.**
  - **Pacing:** redraw requests are merged into one redraw every 38 ms or more. Animations run at 18 ms per frame on the P4 and 26 ms on the S3. Ambient screensavers run at about 20 fps and slow down while audio plays.
  - **Profiling:** the **System: Telemetry Profiler** switch logs per-frame render times and flags any frame over 22 ms. → [`ui_core.h`](include/ui_core.h)
- **ESP32-P4 display stays alive during OTA.** The MIPI-DSI panel's frame-done callback is replaced, through a linker wrap, with an IRAM-resident version. Scanout then keeps running while flash is being erased. → [`ui_layout.h`](include/ui_layout.h), [`waveshare_p4_4b.yaml`](boards/waveshare_p4_4b.yaml)
- **Voice doesn't fight music.** When the wake word fires, the firmware pauses music and stops its mixer input so the voice response isn't stuck behind buffered music. After the reply, it resumes only once both the media player and the announcement mixer have drained. Music never resumes over a ringing alarm or timer. → [`audio_voice.yaml`](modules/audio_voice.yaml), [`latter_client.yaml`](modules/latter_client.yaml)
- **Shared I2S clocks on the S3 boards.** Speaker and microphone use separate I2S controllers on the same clock pins. The firmware switches the pins between them through the GPIO matrix. → [`waveshare_s3_175c.yaml`](boards/waveshare_s3_175c.yaml)
- **Hard power-off hibernate.** On battery boards, hibernate saves preferences, disconnects from the API cleanly, puts the codecs and IMU into standby, and then has the AXP2101 cut power. The crown button powers the device back on. → [`pmu_axp2101.yaml`](modules/pmu_axp2101.yaml)

---

## Project status

This is version **2.2.0**, built and maintained by one person and tested on the five devices above.

| Area | Status |
| :--- | :--- |
| The five flagship devices, the universal GUI, audio, voice, and power | Stable |
| `ble_proxy`, `ble_beacon`, `roaming_vpn`, and `diagnostics` mixins on your own configs | Stable, but see each mixin's [conflicts](docs/mixins.md) |
| `rotary_input` and `p4_camera` mixins | **Experimental** |
| Doorbell two-way talk | **Experimental** (implemented on the Home Assistant side) |

Known limitations:
- **Wi-Fi setup after web flashing.** It works over Bluetooth (Improv) on the three ESP32-S3 devices. The two ESP32-P4 devices currently need ESPHome Device Builder to set Wi-Fi. → [Flashing](docs/flashing.md)
- **CI coverage.** CI validates the two example configs with `esphome config`. Full device builds run only in the release workflow.
- **Pinned to `main`.** Device configs pull the core from the `main` branch, so a device rebuilt later gets the latest core.

Bugs and feature requests: [issues](https://github.com/slampton/esphome-latter-core/issues).

## Documentation

| | |
| :--- | :--- |
| **Use** | [Hardware](docs/hardware.md) · [Flashing](docs/flashing.md) · [Using your device](docs/using-your-device.md) |
| **Extend** | [Mixins](docs/mixins.md) |
| **Build** | [Architecture](docs/architecture.md) · [Device protocol](docs/device-protocol.md) · [Building a GUI](docs/building-a-gui.md) · [Porting hardware](docs/porting-hardware.md) |

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
[ws-p4-4b]: https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4b.htm
