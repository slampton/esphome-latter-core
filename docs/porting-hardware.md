# Porting to New Hardware

Each supported board is a concise file in `boards/` that declares its geometry, capabilities, and explicit chip drivers in a validated `latter_core:` YAML block, includes the shared chip/peripheral HAL modules in [`modules/hal/`](../modules/hal/), and pairs with the compiled C++20 hardware drivers inside [`components/latter_core/`](../components/latter_core/). Everything above the board file is shared. Porting means writing `boards/facts/<board_id>.json`, `boards/<board_id>.yaml`, and a short `devices/<slug>.yaml` config that combines the board with `modules/latter_display_firmware.yaml` (full display + audio + touch) or `modules/latter_display_only_firmware.yaml` (audio-less / touch-optional display).

**Start from an existing board in [`boards/`](../boards/) alongside [`boards/facts/`](../boards/facts/) and `tools/new_board.py`.** Validate flagship boards with `tools/check_boards.py --check-docs` and external community boards with `tools/check_boards.py --external <board.yaml>`.

---

## 1. Required IDs

The core modules refer to these IDs (optional IDs are only required when that capability/module is included):

| ID | Component | Notes |
| :--- | :--- | :--- |
| `disp1` | `display` | Full-frame buffer in PSRAM. `guis/universal/gui.yaml` adds its render lambda with `!extend`. |
| `touch_screen` | `touchscreen` | Required when `capabilities.touch: true` (`guis/universal/gui_touch.yaml` extends it); omit on rotary-only (`touch: false`) builds. |
| `boot_button` | `binary_sensor` | `system_core.yaml` extends it: wake, dismiss alerts, game button, or start/stop a voice turn. |
| `bus_internal` | `i2c` | Shared I2C bus for PMU, IMU, codecs, RTC, and touch. |
| `latter_mic` | `microphone` | Audio builds only (`latter_display_firmware.yaml`): 16 kHz (`audio_in_rate`) mono `resampler` microphone over `latter_mic_i2s` on the 48 kHz I2S bus. |
| `physical_speaker` | `speaker` | Audio builds only: set `timeout: 4s` (longer than the 3 s idle power-down so gaps between tracks never stop I2S clocks under a powered amplifier). |
| `speaker_enable` | `switch` | Audio builds only: amplifier enable. Only `enable_speaker_path` turns it on, after I2S clocks run. |
| `wifi_ctrl` | `wifi` | Defined in Wi-Fi device files (Ethernet-only builds omit `wifi:` — Wi-Fi policy code is `#ifdef USE_WIFI`-guarded). |
| `ble_ctrl` | `latter_beacon` | Optional standalone advertising-only iBeacon (VHCI on ESP32-S3, `esp_hosted` on ESP32-P4). When omitted, manifest emits `"beacon":null`. |
| `latter_firmware_update` | `update` | Defined in the device file. |

## 2. HAL scripts

The core modules invoke board/chip hooks through `DeviceRuntimeState` callbacks bound at boot. Shared audio, PMU, and IMU modules in `modules/hal/` provide these scripts automatically when included; boards without audio (`latter_display_only_firmware.yaml`), battery (`capabilities.battery: false`), or IMU (`capabilities.imu: false`) omit those packages and C++ `#if` guards skip them at compile time.

| Script | Provided by | Parameters | Purpose |
| :--- | :--- | :--- | :--- |
| `hal_set_dac_mute` | `modules/hal/audio_es8311*_duplex.yaml` | `muted: bool` | Mute or unmute the DAC output |
| `hal_set_audio_codecs_standby` | `modules/hal/audio_es8311*_duplex.yaml` | `standby: bool` | Put codecs into low-power standby, or wake them |
| `hal_set_imu_power_mode` | `modules/hal/imu_qmi8658.yaml` (`LATTER_HAS_IMU=1`) | `mode_id: int` | Generic `latter_core::ImuPowerMode` (`Off`, `AccelOnly`, `AccelAndGyro`, `WakeOnMotion`) mapped onto chip registers |
| `hal_init_pmu` | `modules/hal/pmu_axp2101.yaml` (`LATTER_HAS_BATTERY=1`) | — | Configure PMU at boot: mask IRQs, apply rail, TS, and charger traits |
| `hal_release_deep_sleep_holds` | `boards/<board_id>.yaml` | — | Release GPIO holds at boot (`LatterTouchscreen` owns non-blocking `reset_pin` pulses) |
| `hal_enter_hardware_hibernate` | `boards/<board_id>.yaml` | — | Hold pins and cut power after state is saved (`return;` on mains-powered boards) |

**Audio bus (when `capabilities.audio: true`, full duplex at 48 kHz is required):**
- **One full-duplex I2S bus**: set `full_duplex: true` on the bus and run the codecs, `physical_speaker`, and `latter_mic_i2s` at `audio_out_rate` (`"48000"`). `latter_mic` is a `resampler` microphone converting to `audio_in_rate` (`"16000"`) inside the I2S reader callback for wake word and voice assistant. Boards without speakers or mics simply use `modules/latter_display_only_firmware.yaml` with `capabilities.audio: false`.
- **Single-codec vs dual-codec boards**: include `modules/hal/audio_es8311_duplex.yaml` (single ES8311 DAC+ADC, as on `waveshare_p4_35.yaml`) or `modules/hal/audio_es8311_es7210_duplex.yaml` (ES8311 DAC + ES7210 ADC sharing BCLK/WS/MCLK, as on S3 boards and `waveshare_p4_86_panel.yaml`).
- **Never add a `resampler` speaker**: every output source arrives at `audio_out_rate`, and local WAV chimes/alarms come from `sounds/<audio_out_rate>/` (`python tools/gen_sounds.py`).

## 3. Shared `modules/hal/` YAML & `components/latter_core/` C++ HAL drivers

### Shared YAML HAL & service modules

Include only the hardware modules fitted on your board (no `*_none.yaml` stub files exist; chip HALs under `modules/hal/` have zero UI/audio UX coupling):

```yaml
packages:
  latter_chip:            !include ../modules/hal/chip_esp32s3.yaml               # or chip_esp32p4.yaml (+ radio_esp32c6_sdio.yaml for Wi-Fi/BLE P4)
  latter_audio_hal:       !include ../modules/hal/audio_es8311_es7210_duplex.yaml # omit for audio-less displays
  latter_imu:             !include ../modules/hal/imu_qmi8658.yaml                # omit when capabilities.imu: false
  latter_pmu:             !include ../modules/hal/pmu_axp2101.yaml                # omit when no AXP2101 fitted
  latter_battery_service: !include ../modules/battery_service.yaml                # include on any battery board (AXP2101 or custom fuel gauge)
```

For **audio-less / touch-optional displays**, include `modules/latter_display_only_firmware.yaml` in your device YAML (adding `latter_gui_touch: !include ../guis/universal/gui_touch.yaml` if the board has a capacitive touchscreen, or omitting it for pure rotary-knob navigation via `ui_input_event`).

### Compiled C++20 HAL drivers (`components/latter_core/` & `include/hal_*.h`)

Hardware drivers compile inside [`components/latter_core/`](../components/latter_core/) alongside header contracts in [`include/`](../include/) (full component inventory in [Architecture](architecture.md#include--components-3-component-compiled-c20-platform)). Every chip-specific I2C bus write is gated strictly on explicit `LATTER_DRV_*` driver-identity flags (`drivers:` in `latter_core:`)—never on generic capability flags—so third-party boards with different PMICs, IMUs, RTCs, codecs, or touch ICs never get spurious I2C traffic on `0x34`, `0x51`, `0x6B`, `0x18`/`0x40`, or `0x38`:

| File(s) | Responsibility |
| :--- | :--- |
| `power_battery.cpp` + `include/hal_battery.h` | AXP2101 boot init (`LATTER_DRV_PMU_AXP2101`), 0.5C charge current derived from `LATTER_BATTERY_MAH` (up to `20000` mAh), `65 °C` die-temperature foldback, rail keep-masks, hibernate/alarm-sleep entry, load-compensated OCV + hardware fuel-gauge `hw_gauge_pct` (`0..100`, including SoC-only gauges) SoC modeling, and PCF85063 hardware RTC restore/sync (`LATTER_DRV_RTC_PCF85063`) |
| `imu_qmi8658.cpp` + `include/hal_imu_qmi8658.h` | QMI8658 (`LATTER_DRV_IMU_QMI8658`) `ImuPowerMode` active, accelerometer-only, power-down, and hardware wake-on-motion (`INT1`) register programming |
| `include/hal_touch.h` + `touch_hal.cpp` + `touchscreen.py` | First-party `platform: latter_core` I2C capacitive touch driver (`protocol: ft6336 | ft3168 | gt911 | cst9217 | cst816s`), IRAM GPIO edge ISR wake (`pm_wake_loop_from_isr` -> `App.wake_loop_threadsafe`), non-blocking `reset_pin:` + boot recovery, polymorphic FocalTech low-power monitor-mode (`0xA3` chip-ID verified), high-rate `8–16 ms` active-finger tracking, hardware-verified `0 ms` lift release, race-free `GT911` `0x814E` ack, and `[PROF:TOUCH_HAL]` diagnostics |
| `include/hal_panel_power.h` | Backlight PWM (`ledc_channel_config` initialized once, active-low `en_inverted`, or `custom` via `hw_set_backlight_level`), and `mipi_spi` DCS `0x10 SLPIN` / `0x11 SLPOUT` panel sleep templates |
| `audio_es8311.cpp` + `include/audio_router.h` | ES8311 / ES7210 (`LATTER_DRV_CODEC_ES8311`) low-power standby and wake register transitions |
| `pm_light_sleep.cpp` + `include/hal_{sleep_pm,radio_policy,display_lifecycle}.h` | `latter_awake` power-management lock (`latter_core::pm_apply`), `DisplayLifecycleController`, light-sleep wake-pin critical section & ISR edge re-delivery, `< 1 ms` `latter_beacon` pause/resume, raw GPIO retention (`LATTER_TOUCH_RST_PIN`), and `#ifdef USE_WIFI`-guarded Wi-Fi modem-sleep policy |
| `esphome_bridge.cpp` + `include/esphome_bridge.h` | `ESPHOME_VERSION_CODE` `static_assert` guard, 6 `latter_core::` VoiceAssistant/API helpers, `MipiDsi` zero-copy framebuffer access, and throttled OTA lifecycle handlers (`handle_ota_*`) |

- **QMI8658 interrupt:** set `imu_int1_pin` in `substitutions:` to match `latter_core.power.imu_int1_pin`. With the screen off, raise-to-wake arms hardware wake-on-motion instead of polling the accelerometer.
- **Axis inversions:** apply board-specific axis inversions (`- multiply: -1.0` via `!extend`) in the board file, not in the shared module.

## 4. Typed `latter_core:` board schema

All board geometry, capabilities, and hardware drivers are declared in a validated `latter_core:` YAML block inside `boards/<board_id>.yaml` (never raw `-DLATTER_*` `build_flags`). `components/latter_core/__init__.py` validates every key at ESPHome config time and emits the corresponding `LATTER_*` defines into `esphome/core/defines.h`:

```yaml
latter_core:
  board_id: my_board_id
  ble_minor: 6
  gui: universal              # universal (default) | none
  chip: esp32s3               # esp32s3 | esp32p4 | esp32s2 | esp32c6 | esp32c3 | esp32
  psram_mb: 8
  display:
    width: 480
    height: 480
    ppi: 313                  # diagonal pixels / active-area diagonal inches
    round: false
    ips: false
    dcs_sleep: true
    touch_int_pin: 11
    touch_rst_pin: 40
  power:
    battery_mah: 1000         # fitted cell (0..20000, matching boards/facts power.battery_mah)
    pwr_key_pin: 16
    pwr_key_active_high: true
    pwr_key_deep_sleep_wake: true
    action_btn_pin: 18
    imu_int1_pin: 17
    pmu_ts_disable: true
    pmu_dcdc_keep_mask: 0x01
    pmu_ldo_keep_mask: 0x001
  audio:
    out_rate: 48000
    out_channels: 1
    out_bits: 16
    in_rate: 16000
  capabilities:
    display: true
    touch: true
    audio: true               # optionally override mic: / speaker: independently
    imu: true
    battery: true
    rtc: false
    stationary: false
    wearable: false
    hw_jpeg: false            # true on ESP32-P4 hardware JPEG panels
  drivers:                    # explicit opt-in I2C guards (all default false)
    pmu_axp2101: true         # LATTER_DRV_PMU_AXP2101 (0x34)
    rtc_pcf85063: false       # LATTER_DRV_RTC_PCF85063 (0x51)
    imu_qmi8658: true         # LATTER_DRV_IMU_QMI8658 (0x6B)
    codec_es8311: true        # LATTER_DRV_CODEC_ES8311 (0x18 / 0x40)
```

`display.ppi` (`LATTER_PANEL_PPI`) converts physical millimetre touch and typography thresholds in `UiLayout`. See [Device UI design guide → Per-board adaptation](device-ui-design.md#6-per-board-adaptation).

Optional `latter_core:` keys:

| Schema Path | Generated Macro(s) & When |
| :--- | :--- |
| `backlight.{pin, en_pin, en_inverted, inverted, custom}` | `LATTER_BACKLIGHT_*` — LCD panels with PWM/analog-dimmed backlight, active-low `EN` (`en_inverted: true`), or custom driver (`custom: true` binding `rs.hw_set_backlight_level`) |
| `backlight.{pwm_hz, pwm_bits, duty_min_pm, duty_max_pm}` | `LATTER_BACKLIGHT_PWM_*` / `DUTY_*_PM` — PWM frequency, resolution, and usable duty window in permille (`0..1000`; use `>= 5000 Hz` for RC-filtered analog-dimming boost converters) |
| `display.{dc_pin, corner_r, dsi_iram_safe, slot_count, card_slot_count, bri_gamma_x100, bri_dcs_min, bri_dcs_max}` | 1-lane SPI DC pin, custom bezel corner radius, MIPI-DSI IRAM-safe OTA throttling, list capacities, and panel brightness curve overrides |
| `power.{c6_reset_pin, battery_mah, pmu_*}` | ESP32-C6 co-processor reset pin, cell capacity (`<= 20000` mAh; AXP2101 0.5C charge current, OCV curve & empty cutoff), and AXP2101 rail keep-masks |
| `capabilities.{video_stream, hw_jpeg, mic, speaker}` | Independent video, hardware JPEG decode, mic, and speaker capability flags (gating Cameras Listen/Talk/Live Doorbell Popup cleanly) |
| `diagnostics.{nvs_guard, twdt_breadcrumb}` | Enable/disable NVS write-guard linker wraps (`--wrap=nvs_set_blob` / `esp_flash_erase_region`) and TWDT ISR breadcrumbs (both `true` by default) |

### Light sleep (ESP32-S3 battery boards)

ESP32-S3 battery boards include `modules/hal/chip_esp32s3.yaml`, which configures `esp32_power_management` (`max_frequency: 240MHz`, `min_frequency: 80MHz`, `enable_light_sleep: true`, `profiling: ${pm_profiling}`), `CONFIG_PM_LIGHT_SLEEP_CALLBACKS: "y"`, `CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP: "n"`, Wi-Fi sleep IRAM/beacon-window options, `CONFIG_LWIP_DHCP_COARSE_TIMER_SECS: "10"`, `CONFIG_MDNS_TIMER_PERIOD_MS: "500"`, and `CONFIG_ESP_PANIC_HANDLER_IRAM: "y"` (never enable `CONFIG_RTC_CLK_SRC_INT_8MD256`, which causes watchdog resets).

In screen-off quiet on battery, `apply_power_policy` releases `latter_awake` via `latter_core::pm_apply()` (`components/latter_core/pm_light_sleep.cpp` / `include/hal_sleep_pm.h`), arms the light-sleep wake pins, and stretches the main loop interval to **`1,000 ms`** (**`250 ms`** while touch wake is armed). With `CONFIG_BT_CTRL_MODEM_SLEEP` and `CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP` enabled in `latter_beacon`, S3 boards sustain ~65–68% light-sleep duty even while BLE advertising stays active. If smoothed Wi-Fi RSSI drops below **`-78 dBm`** (clearing only after staying above **`-74 dBm`** for 2 minutes; `kWeakLink*` in `include/hal_link_report.h`), `weak_link` holds `latter_awake` so the Home Assistant API link stays stable at weak signal. Never enable light sleep on MIPI-DSI boards.

## 5. Substitutions and sdkconfig

- **Substitutions:** font sizes (`font_time_size` .. `font_games_size`, derived by `tools/new_board.py` to satisfy `[FONT MM]`), image resize bounds (`art_resize`, `sendspin_art_resize`), Native API watchdog (`api_reboot_timeout`: `"15min"` stationary, `"0s"` battery), audio rates (`audio_out_rate: "48000"`, `audio_out_channels: "1"`, `audio_out_bits: "16"`, `audio_in_rate: "16000"`), buffer sizes (`sendspin_buffer_size`, `announcement_buffer_size`), and `mww_internal_ram`. Display dimensions, roundness, initial device preferences (including round vs. rectangular `clock_face`), and sleep/wake policies come strictly from `latter_core:` traits in C++ (`seed_default_prefs_if_needed()` — zero `default_*` or `screen_w`/`screen_h`/`art_shape` YAML substitutions).
- **sdkconfig (via `modules/hal/chip_esp32*.yaml`):** `CONFIG_COMPILER_OPTIMIZATION_PERF: "y"` (`-O2`) and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: "128"` on every board.

## 6. ESP32-P4

- **MIPI PHY rail (`chip_esp32p4.yaml`):** powered by `esp_ldo: channel: 3, voltage: 2.5V`.
- **Wi-Fi / BLE co-processor (`modules/hal/radio_esp32c6_sdio.yaml`):** parameterized `esp32_hosted:` SDIO pins (`c6_sdio_*` substitutions) with `use_psram: true`. Ethernet-only ESP32-P4 boards simply omit `radio_esp32c6_sdio.yaml` and include only `chip_esp32p4.yaml`.
- **Cache line size:** **never** set `CONFIG_CACHE_L2_CACHE_LINE_128B: "y"` (breaks 64-byte-aligned `esp_hosted` PSRAM SDIO buffers with `ESP_ERR_INVALID_ARG`).
- **MIPI-DSI panels (`waveshare_p4_86_panel.yaml`):**
  - Enable `CONFIG_DW_GDMA_ISR_IRAM_SAFE: "y"` (ESPHome `2026.11.0+` `mipi_dsi` automatically enables `CONFIG_LCD_DSI_ISR_CACHE_SAFE` and marks its DPI refresh callback `IRAM_ATTR`; do not set legacy `CONFIG_GDMA_ISR_IRAM_SAFE`, which was removed in ESP-IDF 6.0).
  - Set `display.dsi_iram_safe: true` in `latter_core:` so OTA progress redraws throttle to `>= 10%` / `>= 800 ms` while DW-GDMA DPI scanout continues cleanly across flash erases.
- **Chip revision & power:** P4 boards declare `p4_silicon` (`rev1` = `true` / `360MHz`, `rev3` = `false` / `400MHz`; CI validates and publishes dual variants for SKUs that shipped across both silicon steppings, such as `waveshare_p4_86_panel`) and keep `CPLL` + `200 MHz` PSRAM fixed—never declare `power_management:` (`min_frequency: 40MHz` XTAL DFS retunes MSPI speed modes across `esp_hosted` SDIO DMA and causes `int-wdt`/panic resets). Battery P4 boards stretch the screen-off main loop to `1,000 ms` (`250 ms` with touch wake) in `pm_light_sleep.cpp`.

## 7. Device config, board facts, and validation

1. Author `boards/facts/<board_id>.json` against [`boards/facts/schema.json`](../boards/facts/schema.json).
2. Scaffold initial files and bring-up probes (`--repo` / `--facts` supported for out-of-tree boards):
   ```bash
   python tools/new_board.py <board_id>
   python tools/new_board.py --bringup
   ```
3. Run the static and board-facts verifiers (use `--external` to validate an external community board without adding it to flagship docs):
   ```bash
   python tools/check_boards.py --render-docs
   python tools/check_boards.py --external path/to/my_board.yaml
   python tools/verify_core.py
   esphome compile devices/<my-device>.yaml
   ```
