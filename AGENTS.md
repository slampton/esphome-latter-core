# esphome-latter-core Agent Engineering Guide

Universal ESPHome SDK, Hardware Catalog, and compiled C++20 UI/OS for Latter companion displays and headless nodes (ESPHome `2026.11.0+`, ESP-IDF 5.5+, C++20).

## 1. Acyclic Repository Architecture (Tarjan SCC DAG & Coarse Component Boundaries)

Every hand-authored header, translation unit, YAML package, and tool is organized into cohesive functional modules inside an acyclic `#include` graph with zero cycles (enforced by full Tarjan SCC plus coarse component/header boundaries in `tools/verify_core/`—no rigid per-file header or test-TU allowlists):

- **Three External Components (`components/`, flat `.cpp` TUs directly under `components/*/` for ESPHome `recursive_sources = False`)**:
  - `latter_core` (core state, HAL, NVS & bridge—**zero** `ui_*.h` includes or `latter_ui::mark_dirty_*` declarations/calls across `latter_core`, `latter_beacon`, `include/state/*`, `include/hal_*`, and non-`ui_*.h` headers):
    - **Pure Contracts & State PODs**: Protocol v3 contracts (`include/latter_protocol.h`), hardware manifest (`include/hal_manifest.h`, branching video capabilities strictly on `LATTER_HAS_VIDEO_STREAM` / `LATTER_HAS_HW_JPEG`), `[PROF:*]` flight recorder (`include/latter_prof.h`), chunked RPC outbox (`include/latter_rpc.h`), `include/audio_router.h`, and pure C++20 state PODs (`include/state/{state_common,state_prefs,state_shortcuts,state_nav,state_media,state_areas,state_weather_cal,state_time_alarms,state_alerts_intercom,device_runtime_state}.h`).
    - **HAL, NVS & Parsers**: `include/state/state_nvs_snapshot.h` (`PersistedDevicePrefs` v1 + append-only prefix loader & `HibernateSnapshot` v3), `state_v3_parsers.cpp`, `include/hal_{sleep_pm,radio_policy,display_lifecycle,battery,imu_qmi8658,panel_power,touch,video_stream,link_report}.h`, `pm_light_sleep.cpp`, `pm_telemetry_stall.cpp`, `weak_link_policy.cpp`, `display_lifecycle.cpp`, `video_{parser,decoder,client_esp}.cpp`, `power_battery.cpp`, `audio_es8311.cpp`, `audio_session.cpp`, `imu_qmi8658.cpp`, `panel_power.cpp`, `touch_hal.cpp` (`IRAM_ATTR`), `runtime_controllers.cpp`, `include/latter_actions.h`, and `esphome_bridge.cpp`. Automatically loads `latter_ui` unless `latter_core: gui: "none"` is configured.
  - `latter_ui` (host-compilable headless UI + ESPHome display bridge):
    - **Coarse Header Boundary (`include/ui_*.h`)**: Across all `include/ui_*.h` headers (kept 1:1 with `UI_HEADERS` in `components/latter_ui/__init__.py`), **only** `include/ui_esphome_display.h` may include `esphome/*` or `hal_*.h`, keeping geometry/surface leaves, headless GFX/widgets/compositor (`ui_gfx.cpp`, `ui_widgets.cpp`, `ui_compositor.cpp`, `overlay_takeover.cpp`, `hit_table.cpp`, `ui_mdi.cpp` with 7,447 baked MDI icons), pages, and touch headers host-compilable.
    - **Coarse TU Boundary (`components/latter_ui/*.cpp`)**: ESP-IDF heap/DMA headers (`esp_heap_caps.h`, `driver/ppa.h`, `esp_async_memcpy.h`, `esp_cache.h`) are allowed only in `ui_gfx.cpp` and `ui_mdi.cpp`; `page_system.cpp` never includes `esphome_bridge.h`; `page_music*.cpp` never includes `latter_actions.h`; and the 14 cohesive domain renderers (`page_{clock,home,time,music,area_hub,home_hub,cameras,remote,weather,calendar,tools,pinned,games,system}.cpp` + `page_router.cpp`, `screensaver_fx.cpp`, `overlay_*.cpp`, `touch_engine.cpp`, `touch_dispatch.cpp`) never `#include` sibling `page_*.cpp` files (except `page_router.cpp`).
  - `latter_beacon` (independent component): Raw-HCI iBeacon advertiser (`id: ble_ctrl`) over VHCI or `esp32_hosted` with zero Bluetooth host-stack RAM overhead and `< 1 ms` warm `pause()`/`enable()` transitions; never depends on `latter_ui`.
- **Modular YAML, Tooling & Auto-Discovered 5-Geometry Host Tests (`modules/`, `boards/`, `devices/`, `guis/`, `tools/`, `tests/host/`)**:
  - `tests/host/*.cpp` suites are auto-discovered (requiring `test_harness.h`, `test_main.cpp`, and at least one `test_*.cpp` suite, with zero unity-build `#include *.cpp`).
  - `modules/base.yaml`: Shared Native API (`batch_delay: 0ms`, Noise `encryption:`, `outgoing_connection:`), `time:`, `latter_ping`, `latter_push_v3`, `latter_device_command`, `send_device_hello`, and `System: RPC Outbox`.
  - `modules/latter_display_firmware.yaml`: Umbrella package pulling in `system_{core,ui_sync}.yaml`, `power_policy.yaml`, `sleep_lifecycle.yaml`, `audio_pipeline.yaml`, `voice_assistant.yaml`, `audio_scripts.yaml`, `display_pipeline.yaml`, `timers_and_fetchers.yaml`, and `guis/universal/gui.yaml`.
  - `modules/hal/`: Shared chip (`chip_esp32s3.yaml`, `chip_esp32p4.yaml`), full-duplex audio (`audio_es8311_duplex.yaml`, `audio_es8311_es7210_duplex.yaml`), `pmu_axp2101.yaml`, and `imu_qmi8658.yaml` packages.
  - **Zero YAML `globals:`**: All volatile runtime state lives in `DeviceRuntimeState` (`latter_ui::runtime_state()`) and persisted preferences live in `PersistedDevicePrefs` v1 + `HibernateSnapshot` v3 (`LATTER_SDK_REV == LATTER_YAML_REV == 2026112202L`).

## 2. Verification & Tooling Commands

Run from repository root before opening or updating any change:

```bash
python tools/verify_core.py                            # Full DAG, HAL, UI, protocol, board, sound & 5-geometry host tests
python tools/verify_core.py --fast                     # Sub-second static & AST/DAG checks only
python tools/gen_protocol.py --check --require-sibling # Verify generated protocol headers, TS schema & sibling JSON parity
python tools/check_protocol_usage.py                   # Verify outbound RPC calls & inbound actions match protocol/latter_v3.json
python tools/check_boards.py --check-docs              # Cross-check boards/facts/*.json, boards/*.yaml, devices/*.yaml & docs/hardware.md
python tools/new_board.py --bringup --check            # Verify examples/bringup/*.yaml probes match board facts
python tools/gen_sounds.py --check                     # Verify rendered WAV chimes/alarms match melodies
python tools/gen_mdi_pack.py --check                   # Verify baked 7,447-icon MDI catalog matches @mdi/js
```

## 3. Non-Negotiable Engineering Invariants

- **Terminology & Trait-Only Clean-Slate Code**: Use standard IT and Home Assistant terms (`System`, `Sites`, `Areas`, `Devices`, `Roles`, `Policies`, `Bridges`)—never `"household"`, `"fleet"`, or `"room"`. Never hardcode marketing device names, board filenames, or SoC names into core C++/YAML logic; branch strictly on `LATTER_*` capability traits (`LATTER_HAS_VIDEO_STREAM`, `LATTER_DSI_IRAM_SAFE`, `LATTER_IS_STATIONARY`, `LATTER_HAS_BATTERY`, etc.) with zero legacy shims.
- **Display Buffers, P4 DMA & MIPI-DSI IRAM Safety**:
  - Every board uses 100% full-frame PSRAM display buffers, `CONFIG_COMPILER_OPTIMIZATION_PERF: "y"` (`-O2`), and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: "128"`.
  - On `esp32_hosted:` targets, set `use_psram: true` and **never** set `CONFIG_CACHE_L2_CACHE_LINE_128B: "y"`.
  - On MIPI-DSI targets (`-DLATTER_DSI_IRAM_SAFE=1`), pair `CONFIG_GDMA_ISR_IRAM_SAFE: "y"` and `CONFIG_DW_GDMA_ISR_IRAM_SAFE: "y"` with ESPHome `2026.11.0+` native `CONFIG_LCD_DSI_ISR_CACHE_SAFE` IRAM DPI callbacks.
  - During OTA (`handle_ota_begin` / `handle_ota_progress` in `esphome_bridge.cpp`), stop the display poller on begin and throttle progress redraws (`>= 5%` / `>= 400 ms` on SPI/QSPI; `>= 10%` / `>= 800 ms` on MIPI-DSI). Configure `ledc_channel_config()` only once.
- **Power Management & Light Sleep**:
  - Script `apply_power_policy` (`power_policy.yaml` -> `latter_ui::pm_apply()` in `pm_light_sleep.cpp` / `include/hal_sleep_pm.h`) is the sole owner of the `latter_awake` lock.
  - S3 battery boards enable automatic light sleep (`min_frequency: 80MHz`, `CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP: "n"`, `1,000 ms` quiet loop, `250 ms` while touch wake is armed; never `CONFIG_RTC_CLK_SRC_INT_8MD256`), holding `weak_link` awake below `-78 dBm` smoothed RSSI (clearing above `-74 dBm` for 2 minutes). P4 boards keep fixed `CPLL` + `200 MHz` PSRAM (never `power_management:` / `40 MHz` XTAL DFS across `esp_hosted` SDIO DMA) and stretch battery quiet loops to `1,000 ms` / `250 ms` (`wfi`). Mobile Devices (`LATTER_IS_STATIONARY=0`) set `reboot_timeout: 0s`.
- **Full-Duplex 48 kHz Audio & Voice**:
  - Every board runs one `full_duplex: true` I2S bus at `audio_out_rate: "48000"` with `latter_mic` resampling microphone input to `16000` Hz. Never add a `resampler` speaker or `rtttl:`.
  - Render alarm/timer takeovers above the Voice Assistant takeover, stop VA teardown scripts when an alarm rings, and never clear `continue_conversation_`.
- **Device UI & Protocol v3**:
  - Read `docs/device-ui-design.md` before changing `guis/` or `include/ui_*.h`. Render strictly with row-major RGB565 `fast_*` primitives and `blit_rgb565_scanline` over `Rgb565Surface` (never `it.image()`, `it.draw_pixels_at()`, or lossy masks), paint backgrounds once via `frame_base()`, convert gesture thresholds from `UiLayout` millimetre constants via `LATTER_PANEL_PPI`, and gate home navigation through `latter_ui::can_go_home()`.
  - Inbound state uses Protocol v3 (`protocol/latter_v3.json`); outbound calls use `latter_rpc::emit` on `latter_rpc_outbox` (never `homeassistant.action`). Keep `LATTER_SDK_REV` (`include/esphome_bridge.h`) equal to `LATTER_YAML_REV` (`modules/system_core.yaml`).
