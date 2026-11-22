# Architecture

## Acyclic `L0`–`L4` Firmware DAG

```mermaid
flowchart TD
    D["L4: devices/*.yaml<br/>one per product"] --> B["boards/*.yaml + modules/hal/<br/>hardware abstraction & facts"]
    D --> U["L4: modules/latter_display_firmware.yaml<br/>shared display firmware umbrella"]
    U --> M["L4: modules/<br/>base · system_core · system_ui_sync · power_policy · sleep_lifecycle<br/>audio_pipeline · voice_assistant · audio_scripts · display_pipeline · timers_and_fetchers"]
    U --> G["L4: guis/universal/gui.yaml<br/>ui_esphome_display.h + UiTouchEngine"]
    G --> CUI3["L3: components/latter_ui (Pages & Touch)<br/>14 domain pages · page_router · touch_*.cpp · ui_esphome_display.h"]
    CUI3 --> CUI2["L2: Headless UI Infra<br/>gfx_*.cpp · widgets_*.cpp · compositor_*.cpp · overlay_takeover.cpp · ui_mdi.cpp"]
    M --> CC["L1: components/latter_core & latter_beacon<br/>state_v3_parsers_*.cpp · pm_light_sleep · video_* · audio/power/touch HAL · esphome_bridge"]
    B --> CC
    CUI2 --> L0["L0: Pure C++20 Leaves<br/>include/state/*.h · ui_{color,surface,panel_traits,widget_ids,hit_table,panel_metrics,page_geoms,types}.h<br/>latter_protocol.h · hal_manifest.h · latter_rpc.h · latter_prof.h · audio_router.h"]
    CC --> L0
```

Each product file in `devices/` combines its board HAL with the shared display umbrella and the three compiled C++20 PlatformIO components (`latter_core`, `latter_ui`, `latter_beacon`), keeping every hand-authored file `<= 1,000` LOC (`<= 1,200` LOC hard CI ratchet) with zero `#include` cycles:

```yaml
# devices/pocket-assistant.yaml (abridged)
external_components:
  - source: { type: git, url: https://github.com/slampton/esphome-latter-core, ref: ${latter_ref} }
    components: [latter_core, latter_ui, latter_beacon]
    refresh: 0s

packages:
  hal:             !include ../boards/waveshare_s3_175c.yaml
  latter_firmware: !include ../modules/latter_display_firmware.yaml
```

Device files define only what varies per SKU: `wifi` (`id: wifi_ctrl`), `latter_beacon` (`id: ble_ctrl` + `minor`), `improv_serial`, the `Project Firmware` HTTPS OTA manifest URL, and product default substitutions. All volatile runtime state lives in `DeviceRuntimeState` (`include/state/device_runtime_state.h`) and all persisted device preferences live in `PersistedDevicePrefs` v1 + cold-boot `HibernateSnapshot` v3 (`include/state/state_nvs_snapshot.h`), with **zero YAML `globals:`**.

---

## Directory Map

### `boards/`: Hardware Abstraction & Machine Facts
Each board pairs `boards/<board_id>.yaml` with a schematic-verified `boards/facts/<board_id>.json` ledger checked by `tools/check_boards.py`:
- **Buses & peripherals:** display (`mipi_spi` or `mipi_dsi`), touchscreen (`platform: latter_core`), I2C, and GPIO pins.
- **Shared HAL packages (`modules/hal/`):** chip config (`chip_esp32s3.yaml` or `chip_esp32p4.yaml`), full-duplex 48 kHz audio (`audio_es8311_duplex.yaml` or `audio_es8311_es7210_duplex.yaml`), plus optional `pmu_axp2101.yaml` and `imu_qmi8658.yaml`.
- **Compile-time traits:** explicit `LATTER_*` capability flags (`LATTER_SCREEN_W/H`, `LATTER_PANEL_PPI`, `LATTER_HAS_VIDEO_STREAM`, `LATTER_HAS_IMU`, `LATTER_HAS_BATTERY`, `LATTER_HAS_RTC`, etc.) and HAL scripts (`hal_release_deep_sleep_holds`, `hal_enter_hardware_hibernate`). → [Porting hardware](porting-hardware.md)

### `modules/`: Core OS (`L4` YAML Modules)

| File | Role |
| :--- | :--- |
| `latter_display_firmware.yaml` | Umbrella package for display builds: configures `latter_core:`, boot-log replay (`logger:`), `safe_mode:`, all core modules below, and `guis/universal/gui.yaml`. |
| `base.yaml` | Shared foundation for both display and headless nodes: Native API (`batch_delay: 0ms`, Noise `encryption:`, `outgoing_connection:`), `latter_ping`, `latter_push_v3`, `latter_device_command`, `time:` (`ha_time` + `sntp_time`), `send_device_hello`, `System: Hardware Manifest` (internal), and `System: RPC Outbox` (`latter_rpc_outbox`). |
| `system_core.yaml` & `system_ui_sync.yaml` | Includes `base.yaml`, enforces `LATTER_SDK_REV == LATTER_YAML_REV`, declares display Native API actions (`latter_set_slots`, `latter_show_notification`, `latter_set_beacon_uuid`, `latter_factory_reset`, `latter_set_calendar_view`, `latter_set_music_lyrics`), deduplicates pull + Noise-encrypted push OTA (`handle_ota_*`), hydrates `PersistedDevicePrefs` v1 + `HibernateSnapshot` v3, and bridges UI sync & `ui_input_event`. |
| `power_policy.yaml` & `sleep_lifecycle.yaml` | Power state machine (active, screensaver, screen-off quiet, hibernate), `1 s` lifecycle supervisor (`DisplayLifecycleController`), `20 ms` cadence tick (`FrameCadenceController`), hibernate sequencing, and script `apply_power_policy`—the sole owner of the `latter_awake` lock. |
| `audio_pipeline.yaml`, `voice_assistant.yaml` & `audio_scripts.yaml` | Audio mixer, Sendspin `Music` media player, `micro_wake_word` (`models/`), `voice_assistant`, internal WAV `latter_sound_player` (`sounds/<rate>/`), and voice/TTS/game-SFX lifecycle scripts coordinated by `AudioPathArbitrator` and `AudioSessionController`. |
| `display_pipeline.yaml` | `http_request`, Sendspin album art, `online_image` slots (artwork, wallpaper, screensaver photo, camera; baseline JPEG decoded to RGB565 in PSRAM), and internal `Display: Screen Backlight` (`hal_set_display_brightness`). |
| `timers_and_fetchers.yaml` | 1 Hz countdown timer & recurring alarm evaluation (`TimerAlarmController`), event-driven `timer_alarm_state` & `device_telemetry` RPC emission, and outbound fetch helpers. |
| `hal/chip_esp32s3.yaml`, `hal/chip_esp32p4.yaml` | Shared chip `sdkconfig_options`, ESP32-S3 automatic FreeRTOS light sleep (`240/80 MHz`), and ESP32-P4 fixed-`CPLL` / `200 MHz` PSRAM + `esp_ldo` PHY rail + `esp32_hosted` PSRAM SDIO setup (no P4 DFS). |
| `hal/audio_es8311_duplex.yaml`, `hal/audio_es8311_es7210_duplex.yaml` | Full-duplex 48 kHz I2S bus, ES8311 / ES7210 codecs, `physical_speaker` (`timeout: 4s`), `latter_mic` resampler microphone (16 kHz), and codec mute/standby scripts. |
| `hal/pmu_axp2101.yaml`, `hal/imu_qmi8658.yaml` | Optional AXP2101 battery/charger/PWR-key telemetry (+ PCF85063 RTC when `LATTER_HAS_RTC=1`) and QMI8658 accelerometer/tilt/wake-on-motion packages. |

### `include/` & `components/`: 3-Component Compiled C++20 SDK

| Component | Staged Headers & Compiled Translation Units |
| :--- | :--- |
| **`components/latter_core/`** *(`L0`–`L1`, zero `ui_*.h` coupling)* | **Headers (`include/` & `include/state/`):** `state/{state_common,state_prefs,state_shortcuts,state_nav,state_media,state_areas,state_weather_cal,state_time_alarms,state_alerts_intercom,device_runtime_state,state_nvs_snapshot}.h`, `esphome_bridge.h`, `hal_manifest.h`, `hal_battery.h`, `hal_imu_qmi8658.h`, `hal_panel_power.h`, `hal_{sleep_pm,radio_policy,display_lifecycle}.h`, `hal_touch.h`, `hal_video_stream.h`, `hal_link_report.h`, `audio_router.h`, `latter_protocol.h` (generated), `latter_rpc.h`, `latter_actions.h`, `latter_prof.h`.<br/>**Flat TUs (`components/latter_core/`):** `state_v3_parsers_{core,apps}.cpp`, `power_battery.cpp`, `audio_es8311.cpp`, `audio_session.cpp`, `imu_qmi8658.cpp`, `panel_power.cpp`, `touch_hal.cpp`, `pm_light_sleep.cpp`, `pm_telemetry_stall.cpp`, `weak_link_policy.cpp`, `display_lifecycle.cpp`, `video_{parser,decoder,client_esp}.cpp`, `runtime_controllers.cpp`, `esphome_bridge.cpp`. Auto-loads `latter_ui` unless `latter_core: gui: "none"` is set. |
| **`components/latter_ui/`** *(`L0` / `L2` / `L3`)* | **Headers (`include/`):** `L0` `ui_{color,surface,panel_traits,widget_ids,hit_table,panel_metrics,page_geoms,types}.h`; `L2` `ui_{mdi,frame_context,dirty_band,cadence,compositor,gfx,widgets,layout,touch_recognizer}.h` (pure C++20, zero `esphome/` includes); `L3` `ui_pages.h`, `ui_touch_engine.h`, `ui_esphome_display.h`.<br/>**Flat TUs (`components/latter_ui/`):** `gfx_{fb_clear,primitives,text,art_blit}.cpp`, `ui_mdi.cpp`, `widgets_{chrome,inputs,media_bars,text_layout}.cpp`, `compositor_{dirty_band,cadence}.cpp`, `overlay_takeover.cpp`, `hit_table.cpp`, `touch_*.cpp`, `page_router.cpp`, and **14 domain apps/views** (`page_clock`, `page_home` + `home_activity_model`, `page_time`, `page_music_*`, `page_area_hub`, `page_home_hub`, `page_cameras`, `page_remote`, `page_weather` + `weather_*`, `page_calendar_*`, `page_tools` + `page_tool_*`, `page_pinned`, `page_games` + `games_*`, `screensaver_fx` + `overlay_*`, `page_system`). |
| **`components/latter_beacon/`** *(`L1`)* | `latter_beacon.h` / `latter_beacon.cpp`: raw-HCI BLE iBeacon advertiser (`ble_ctrl`) over VHCI or `esp32_hosted` with zero Bluedroid/NimBLE host-stack RAM and `< 1 ms` warm `pause()`/`enable()` transitions. |

### `guis/`, `examples/`, `manifests/` & `index.html`
- **`guis/universal/gui.yaml`**: Fonts, WCAG-verified palette, `!extend disp1` render lambda (`ui_esphome_display.h` + `DirtyBandTracker` `2px` dirty bands + `render_system_takeovers` + `page_router`), and `!extend touch_screen` forwarding into `UiTouchEngine`. → [Building a GUI](building-a-gui.md)
- **`examples/`**: `minimal_headless_companion.yaml` (`modules/base.yaml` + `gui: "none"`), `custom_display_template.yaml` (standalone 480×480 board + GUI template), and `bringup/*.yaml` (`tools/new_board.py --bringup`).
- **`manifests/` & `index.html`**: GitHub Pages ESP Web Tools installer and per-device/per-silicon release manifests.

---

## Power Management, Sleep & OTA Safety

- **Single Awake-Lock Owner (`latter_awake`).** Coordinated exclusively in [`components/latter_core/`](../components/latter_core/) (`pm_light_sleep.cpp` / `include/hal_sleep_pm.h`) via `apply_power_policy`. Held while the screen or screensaver is active, USB power is connected, or audio, wake word, Assist, OTA, games, ringing alarms/timers, Find My, alerts, or doorbell audio are active; released only in battery screen-off quiet.
- **ESP32-S3 Automatic Light Sleep (`~5–8 mA`).** `chip_esp32s3.yaml` enables `enable_light_sleep: true` (`240 MHz` max / `80 MHz` min, `CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP: "n"`). Releasing `latter_awake` arms GPIO wake pins (`LATTER_PWR_KEY_PIN`, `LATTER_ACTION_BTN_PIN`, `LATTER_TOUCH_INT_PIN`, `LATTER_IMU_INT1_PIN` with ISR edge re-delivery), pauses `latter_beacon` in `< 1 ms` via `HCI_LE_Set_Adv_Enable(0)` (staying active on USB when `ble_beacon_mode == "Always"`), and stretches the main loop to **`1,000 ms`** (**`250 ms`** while touch wake is armed). Smoothed RSSI below **`-78 dBm`** holds `weak_link` awake until RSSI stays above **`-74 dBm`** for 2 minutes; off-site (`!wifi_connected`), Wi-Fi scans back off exponentially (`15 s -> 60 s -> 300 s`, `~2–4 mA`).
- **ESP32-P4 Fixed-CPLL Quiet Loop & MIPI-DSI OTA.** ESP32-P4 boards intentionally omit `power_management:` (`40 MHz` XTAL DFS retunes MSPI timing across `esp_hosted` SDIO DMA and causes WDT resets), keeping fixed `CPLL` (`360` / `400 MHz`) + `200 MHz` PSRAM and pacing battery screen-off loops at `1,000 ms` (`250 ms` touch wake) so both RISC-V cores idle in `wfi`. On MIPI-DSI targets (`-DLATTER_DSI_IRAM_SAFE=1`), ESPHome `2026.11.0+` natively places the MIPI-DSI refresh callback in IRAM (`CONFIG_LCD_DSI_ISR_CACHE_SAFE`), paired with `CONFIG_GDMA_ISR_IRAM_SAFE: "y"` and `CONFIG_DW_GDMA_ISR_IRAM_SAFE: "y"` (`>= 10%` / `>= 800 ms` OTA redraws) so DPI scanout never tears during flash erases.

---

## Build, CI & Requirements

- **Validation Gate (`validate.yml`):** Runs `tools/verify_core.py`, `tools/gen_protocol.py --check --require-sibling`, `tools/check_protocol_usage.py`, `tools/check_boards.py --check-docs`, `tools/new_board.py --bringup --check`, `tools/gen_sounds.py --check`, `tools/gen_mdi_pack.py --check`, 5-geometry C++20 host unit tests (`tests/host/*.cpp`), `esphome config` across examples/probes/devices, and full 5-device firmware compilation on `dev` before fast-forwarding `main`.
- **Release (`release.yml`):** Enforces zero `L`-confidence board facts (`check_boards.py --release --check-docs`), compiles all device and P4 silicon variants in `esphome/esphome:2026.11.0`, generates MD5 OTA manifests + `.elf` debug symbols, and deploys the Web Installer to GitHub Pages.
- **Compiler Requirements:** ESPHome `2026.11.0+` (`ESPHOME_VERSION_CODE` + `LATTER_SDK_REV == LATTER_YAML_REV` static asserts in `include/esphome_bridge.h`), ESP-IDF 5.5+ C++20, PSRAM full-frame RGB565 display buffers, `CONFIG_COMPILER_OPTIMIZATION_PERF: "y"` (`-O2`), and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: "128"`.
