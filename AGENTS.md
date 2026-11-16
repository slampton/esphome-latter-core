# esphome-latter-core Agent Engineering Guide

Universal ESPHome SDK, Hardware Catalog, and compiled C++20 UI/OS for Latter companion displays and headless nodes (ESPHome `2026.11.0b5+`, ESP-IDF 5.5+, C++20).

## 1. Repository Architecture

- **Three External Components (`components/`)**:
  - `latter_core`: Compiled HAL & runtime controllers (`power_battery.cpp`, `audio_es8311.cpp`, `audio_session.cpp`, `imu_qmi8658.cpp`, `panel_power.cpp`, `power_pm.cpp`, `runtime_controllers.cpp`), `esphome_bridge.cpp` (isolating `VoiceAssistant` synchronous abort, `MipiDsi` framebuffer access, and OTA helpers), Protocol v3 state (`include/latter_state.h`), zero-privilege RPC outbox (`include/latter_rpc.h`, `include/latter_actions.h`), hardware manifest (`include/hal_manifest.h`), link telemetry (`include/hal_link_report.h`), and `[PROF:*]` flight recorder (`include/latter_prof.h`). Automatically loads `latter_ui` unless `latter_core: gui: "none"` is configured.
  - `latter_ui`: Compiled C++20 UI engine across 9 public `include/ui_*.h` headers (`ui_mdi.h` 7,447 baked MDI icons), 11 domain renderers (`page_*.cpp`), `ui_gfx.cpp`, `ui_mdi.cpp`, `FrameHitTable` (`hit_table.cpp`), `UiTouchEngine` (`touch_engine.cpp`), frosted-glass widgets (`ui_widgets.cpp`), and `DirtyBandTracker` `2px` horizontal dirty-band compositor (`ui_compositor.cpp`).
  - `latter_beacon`: Raw-HCI iBeacon advertiser (`id: ble_ctrl`) over VHCI (ESP32-S3) or `esp_hosted` (ESP32-P4) with zero Bluetooth host-stack RAM overhead and `< 1 ms` warm `pause()`/`enable()` transitions.
- **Modular YAML (`modules/`, `boards/`, `devices/`, `guis/`)**:
  - `modules/base.yaml`: Shared Native API (`batch_delay: 0ms`, Noise `encryption:`, `outgoing_connection:`), `time:`, `latter_ping` (`id: latter_ping_action`), `latter_push_v3`, `latter_device_command`, `send_device_hello`, `System: Hardware Manifest`, and `System: RPC Outbox`.
  - `modules/latter_display_firmware.yaml`: Umbrella package included by `devices/*.yaml` pulling in `system_core.yaml`, `power_manager.yaml`, `audio_voice.yaml`, `display_pipeline.yaml`, `timers_and_fetchers.yaml`, and `guis/universal/gui.yaml`.
  - `modules/hal/`: Shared chip (`chip_esp32s3.yaml`, `chip_esp32p4.yaml`), full-duplex audio (`audio_es8311_duplex.yaml`, `audio_es8311_es7210_duplex.yaml`), `pmu_axp2101.yaml`, and `imu_qmi8658.yaml` packages.
  - **Zero YAML `globals:`**: All volatile runtime state lives in `DeviceRuntimeState` (`latter_ui::runtime_state()`) and all 50 persisted preferences live in `PersistedDevicePrefs` v6 + cold-boot `HibernateSnapshot` v2 (`latter_ui::persisted_prefs()`, `latter_ui::hibernate_snapshot()`).

## 2. Verification & Tooling Commands

Run from repository root before opening or updating any change:

```bash
python tools/verify_core.py                  # Full static, HAL, UI, protocol, board, sound & C++20 host-test suite
python tools/verify_core.py --fast           # Sub-second static & AST checks only
python tools/gen_protocol.py --check         # Verify include/latter_protocol.h & DEVICE_CONTRACT.md match protocol/latter_v3.json
python tools/check_protocol_usage.py         # Verify outbound RPC calls & inbound actions match protocol/latter_v3.json
python tools/check_boards.py --check-docs    # Cross-check boards/facts/*.json, boards/*.yaml, devices/*.yaml & docs/hardware.md
python tools/new_board.py --bringup --check  # Verify examples/bringup/*.yaml probes match board facts
python tools/gen_sounds.py --check           # Verify rendered WAV chimes/alarms match melodies
python tools/gen_mdi_pack.py --check         # Verify baked 7,447-icon MDI catalog matches @mdi/js
```

## 3. Non-Negotiable Engineering Invariants

- **Terminology & Clean-Slate Code**: Use standard IT and Home Assistant terms (`System`, `Sites`, `Areas`, `Devices`, `Roles`, `Policies`, `Bridges`)—never `"household"`, `"fleet"`, or `"room"`. Never hardcode marketing device names into C++ or core modules; branch strictly on `LATTER_*` capability flags. Maintain one canonical implementation path with zero legacy shims.
- **Display Buffers, P4 DMA & MIPI-DSI IRAM Safety**:
  - Every board uses 100% full-frame PSRAM display buffers, `CONFIG_COMPILER_OPTIMIZATION_PERF: "y"` (`-O2`), and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: "128"`.
  - On ESP32-P4 boards, set `use_psram: true` under `esp32_hosted:` and **never** set `CONFIG_CACHE_L2_CACHE_LINE_128B: "y"`.
  - On MIPI-DSI (`waveshare_p4_86_panel.yaml`), pair `CONFIG_GDMA_ISR_IRAM_SAFE: "y"` and `CONFIG_DW_GDMA_ISR_IRAM_SAFE: "y"` with `-DLATTER_DSI_IRAM_SAFE=1` (ESPHome `2026.11.0b4+` `mipi_dsi` sets `CONFIG_LCD_DSI_ISR_CACHE_SAFE` and `IRAM_ATTR` DPI callback natively).
  - During OTA (`handle_ota_begin` / `handle_ota_progress` in `esphome_bridge.cpp`), stop the display poller on begin and throttle progress redraws (`>= 5%` / `>= 400 ms` on SPI/QSPI; `>= 10%` / `>= 800 ms` on MIPI-DSI). Configure `ledc_channel_config()` only once.
- **Power Management & Light Sleep**:
  - Script `apply_power_policy` (`power_manager.yaml` -> `latter_ui::pm_apply()` in `power_pm.cpp`) is the sole owner of the `latter_awake` lock.
  - S3 battery boards enable automatic light sleep (`min_frequency: 80MHz`, `CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP: "n"`, `1,000 ms` quiet loop, `250 ms` while touch wake is armed; never `CONFIG_RTC_CLK_SRC_INT_8MD256`), holding `weak_link` awake below `-65 dBm` smoothed RSSI (clearing above `-61 dBm` for 2 minutes). P4 boards keep fixed `CPLL` + `200 MHz` PSRAM (never `power_management:` / `40 MHz` XTAL DFS, which retunes MSPI across `esp_hosted` SDIO DMA) and stretch battery quiet loops to `1,000 ms` / `250 ms` (`wfi`). Mobile Devices (`LATTER_IS_STATIONARY=0`) set `reboot_timeout: 0s`.
- **Full-Duplex 48 kHz Audio & Voice**:
  - Every board runs one `full_duplex: true` I2S bus at `audio_out_rate: "48000"` with `latter_mic` resampling microphone input to `16000` Hz. Never add a `resampler` speaker or `rtttl:`.
  - Render alarm/timer takeovers above the Voice Assistant takeover, stop VA teardown scripts when an alarm rings, and never clear `continue_conversation_`.
- **Device UI & Protocol v3**:
  - Read `docs/device-ui-design.md` before changing `guis/` or `include/ui_*.h`. Render strictly with row-major RGB565 `fast_*` primitives and `blit_rgb565_scanline` (never `it.image()`, `it.draw_pixels_at()`, or lossy masks), paint backgrounds once via `frame_base()`, convert gesture thresholds from `UiLayout` millimetre constants via `LATTER_PANEL_PPI`, and gate home navigation through `latter_ui::can_go_home()`.
  - Inbound state uses Protocol v3 (`protocol/latter_v3.json`); outbound calls use `latter_rpc::emit` on `latter_rpc_outbox` (never `homeassistant.action`). Keep `LATTER_SDK_REV` (`include/esphome_bridge.h`) equal to `LATTER_YAML_REV` (`modules/system_core.yaml`).
