# Porting to New Hardware

Each supported board is a concise file in `boards/` that includes the shared chip, audio, PMU, and IMU HAL modules in [`modules/hal/`](../modules/hal/) and pairs with the compiled C++20 hardware drivers inside [`components/latter_core/`](../components/latter_core/). That board file describes only schematic-specific pins, buses, and geometry traits; everything above it is shared. Porting means writing `boards/facts/<board_id>.json`, `boards/<board_id>.yaml`, and a short `devices/<slug>.yaml` config that combines the board with `modules/latter_display_firmware.yaml`.

**Start from [`examples/custom_display_template.yaml`](../examples/custom_display_template.yaml).** It is a complete 480×480 board definition with stub HAL scripts, and CI validates it on every push.

---

## 1. Required IDs

The core modules refer to these IDs. Your board or device file must define each one.

| ID | Component | Notes |
| :--- | :--- | :--- |
| `disp1` | `display` | Full-frame buffer in PSRAM. The GUI adds its render lambda with `!extend`. |
| `touch_screen` | `touchscreen` | The GUI adds gesture handling with `!extend`. |
| `boot_button` | `binary_sensor` | `system_core.yaml` extends it: wake, dismiss alerts, game button, start or stop a voice turn. |
| `bus_internal` | `i2c` | Shared I2C bus for PMU, IMU, codecs, and touch. |
| `latter_mic` | `microphone` | 16 kHz (`audio_in_rate`) mono: a `resampler` microphone over `latter_mic_i2s` when the I2S bus runs at 48 kHz. |
| `physical_speaker` | `speaker` | Set `timeout: 4s` (longer than the 3 s idle power-down, so gaps between tracks never stop I2S clocks under a powered amplifier). |
| `speaker_enable` | `switch` | Amplifier enable. Only `enable_speaker_path` turns it on, after I2S clocks run. |
| `wifi_ctrl` | `wifi` | Defined in the device file. |
| `ble_ctrl` | `latter_beacon` | Advertising-only iBeacon (VHCI on ESP32-S3, `esp_hosted` on ESP32-P4). Defined in the device file. |
| `latter_firmware_update` | `update` | Defined in the device file. |

## 2. HAL scripts

The core modules call these scripts. Shared audio, PMU, and IMU modules in `modules/hal/` provide the codec, PMU, and IMU scripts automatically when included; boards without a battery (`LATTER_HAS_BATTERY=0`) or without an IMU (`LATTER_HAS_IMU=0`) omit those packages, and core C++ guards skip them at compile time.

| Script | Provided by | Parameters | Purpose |
| :--- | :--- | :--- | :--- |
| `hal_set_dac_mute` | `modules/hal/audio_es8311*_duplex.yaml` | `muted: bool` | Mute or unmute the DAC output |
| `hal_set_audio_codecs_standby` | `modules/hal/audio_es8311*_duplex.yaml` | `standby: bool` | Put codecs into low-power standby, or wake them |
| `hal_set_imu_power_mode` | `modules/hal/imu_qmi8658.yaml` (`LATTER_HAS_IMU=1`) | `ctrl7_val: int` | IMU mode: `0x00` off, `0x01` accel, `0x03` accel + gyro, `0x80` hardware wake-on-motion |
| `hal_init_pmu` | `modules/hal/pmu_axp2101.yaml` (`LATTER_HAS_BATTERY=1`) | — | Configure PMU at boot: mask IRQs, apply rail, TS, and charger traits |
| `hal_release_deep_sleep_holds` | `boards/<board_id>.yaml` | — | Release GPIO holds at boot and pulse touch reset if the touch driver cannot |
| `hal_enter_hardware_hibernate` | `boards/<board_id>.yaml` | — | Hold pins and cut power after state is saved (`return;` on mains-powered boards) |

**Audio bus (full duplex at 48 kHz is required):**
- **One full-duplex I2S bus**: set `full_duplex: true` on the bus and run the codecs, `physical_speaker`, and `latter_mic_i2s` at `audio_out_rate` (`"48000"`). `latter_mic` is a `resampler` microphone converting to `audio_in_rate` (`"16000"`) inside the I2S reader callback for wake word and voice assistant. Latter never hands the bus over; boards that cannot run one full-duplex bus are not supported.
- **Single-codec vs dual-codec boards**: include `modules/hal/audio_es8311_duplex.yaml` (single ES8311 DAC+ADC, as on `waveshare_p4_35.yaml`) or `modules/hal/audio_es8311_es7210_duplex.yaml` (ES8311 DAC + ES7210 ADC sharing BCLK/WS/MCLK, as on S3 boards and `waveshare_p4_86_panel.yaml`).
- **Never add a `resampler` speaker**: every output source arrives at `audio_out_rate`, and local WAV chimes/alarms come from `sounds/<audio_out_rate>/` (`python tools/gen_sounds.py`).

## 3. Shared `modules/hal/` YAML & `components/latter_core/` C++ HAL drivers

### Shared YAML HAL modules (`modules/hal/`)

Include only the hardware modules fitted on your board (no `*_none.yaml` stub files exist):

```yaml
packages:
  latter_chip:      !include ../modules/hal/chip_esp32s3.yaml               # or chip_esp32p4.yaml
  latter_audio_hal: !include ../modules/hal/audio_es8311_es7210_duplex.yaml # or audio_es8311_duplex.yaml
  latter_imu:       !include ../modules/hal/imu_qmi8658.yaml                # omit when LATTER_HAS_IMU=0
  latter_pmu:       !include ../modules/hal/pmu_axp2101.yaml                # omit when LATTER_HAS_BATTERY=0
```

### Compiled C++20 HAL translation units (`components/latter_core/`)

Hardware support compiles directly inside [`components/latter_core/`](../components/latter_core/) alongside header contracts in [`include/`](../include/):

| Translation unit | Header | Responsibility |
| :--- | :--- | :--- |
| `power_battery.cpp` | `include/hal_battery.h` | AXP2101 boot init, 0.5C charge current derived from `LATTER_BATTERY_MAH`, `65 °C` die-temperature foldback, rail keep-masks, hibernate/alarm-sleep entry, and load-compensated OCV + fuel-gauge state-of-charge modeling |
| `imu_qmi8658.cpp` | `include/hal_imu_qmi8658.h` | QMI8658 active, accelerometer-only, power-down, and hardware wake-on-motion (`INT1`) register programming |
| `panel_power.cpp` | `include/hal_panel_power.h` | Backlight PWM (`ledc_channel_config` initialized once), `mipi_spi` DCS `0x10 SLPIN` / `0x11 SLPOUT` panel sleep, and FocalTech touch low-power monitor mode |
| `audio_es8311.cpp` | `include/audio_router.h` | ES8311 / ES7210 low-power standby and wake register transitions |
| `power_pm.cpp` | `include/hal_power_pm.h` | `latter_awake` power-management lock, light-sleep wake-pin critical section and ISR edge re-delivery, raw runtime GPIO retention (`LATTER_TOUCH_RST_PIN`), Wi-Fi modem-sleep policy, and wake/memory diagnostics |
| `esphome_bridge.cpp` | `include/esphome_bridge.h` | `ESPHOME_VERSION_CODE` `static_assert` guard, `VoiceAssistant` synchronous abort, `MipiDsi` zero-copy framebuffer access, and throttled OTA lifecycle handlers (`handle_ota_*`) |

- **QMI8658 interrupt:** set `imu_int1_pin` in `substitutions:` to match `-DLATTER_IMU_INT1_PIN=<gpio>`. With the screen off, raise-to-wake arms hardware wake-on-motion instead of polling the accelerometer.
- **Axis inversions:** apply board-specific axis inversions (`- multiply: -1.0` via `!extend`) in the board file, not in the shared module.

## 4. Build flags

```yaml
esphome:
  build_flags:
    - '-DLATTER_BOARD_ID=\"my_board_id\"'
    - "-DLATTER_BLE_MINOR=6"
    - "-DLATTER_SCREEN_W=480"
    - "-DLATTER_SCREEN_H=480"
    - "-DLATTER_ROUND_DISPLAY=0"
    - "-DLATTER_HAS_DISPLAY=1"
    - "-DLATTER_HAS_TOUCH=1"
    - "-DLATTER_HAS_AUDIO=1"
    - "-DLATTER_HAS_IMU=1"
    - "-DLATTER_HAS_BATTERY=1"
    - "-DLATTER_BATTERY_MAH=1000"  # fitted cell (boards/facts power.battery_mah)
    - "-DLATTER_IS_STATIONARY=0"
    - "-DLATTER_IS_WEARABLE=0"
    - "-DLATTER_PANEL_IPS=0"
    - "-DLATTER_PANEL_PPI=313"     # diagonal pixels / active-area diagonal inches
    - "-DLATTER_CHIP_P4=0"
    - "-DLATTER_PSRAM_MB=8"
    - "-DLATTER_AUDIO_OUT_RATE=48000"
    - "-DLATTER_AUDIO_OUT_CH=1"
    - "-DLATTER_AUDIO_OUT_BITS=16"
    - "-DLATTER_AUDIO_IN_RATE=16000"
```

Define every required capability flag explicitly (`LATTER_SCREEN_W`, `LATTER_SCREEN_H`, `LATTER_ROUND_DISPLAY`, `LATTER_HAS_IMU`, `LATTER_HAS_BATTERY`, `LATTER_IS_STATIONARY`, `LATTER_IS_WEARABLE`, `LATTER_HAS_TOUCH`, `LATTER_PANEL_IPS`, `LATTER_PANEL_PPI`). `LATTER_PANEL_PPI` converts physical millimetre touch and typography thresholds in `UiLayout`. See [Device UI design guide → Per-board adaptation](device-ui-design.md#6-per-board-adaptation).

Optional hardware flags:

| Flag | When |
| :--- | :--- |
| `LATTER_BACKLIGHT_PIN`, `LATTER_BACKLIGHT_EN_PIN`, `LATTER_BACKLIGHT_INVERTED` | LCD panels with a PWM or analog-dimmed backlight |
| `LATTER_BACKLIGHT_PWM_HZ`, `LATTER_BACKLIGHT_PWM_BITS`, `LATTER_BACKLIGHT_DUTY_MIN_PCT`, `LATTER_BACKLIGHT_DUTY_MAX_PCT` | Backlight PWM frequency, resolution, and usable duty window (use `>= 5000 Hz` for RC-filtered analog-dimming boost converters) |
| `LATTER_DISPLAY_DC_PIN` | Required on 1-lane SPI IPS panels (`LATTER_PANEL_IPS=1` + `LATTER_PANEL_DCS_SLEEP=1`) |
| `LATTER_PWR_KEY_PIN`, `LATTER_PWR_KEY_ACTIVE_HIGH`, `LATTER_PWR_KEY_DEEP_SLEEP_WAKE` | Power key GPIO, polarity, and whether it is RTC/LP wake-capable in deep sleep |
| `LATTER_ACTION_BTN_PIN`, `LATTER_TOUCH_INT_PIN`, `LATTER_IMU_INT1_PIN` | Light-sleep wake pins (`LATTER_IMU_INT1_PIN` must match `imu_int1_pin`; mark each pin `light_sleep_wake` in `boards/facts/<id>.json`) |
| `LATTER_TOUCH_RST_PIN` | Touch reset driven with raw `gpio_set_level` (kept driven across light sleep by `pm_keep_raw_outputs_driven()`) |
| `LATTER_BATTERY_MAH` | Required when `LATTER_HAS_BATTERY=1`; `include/hal_battery.h` derives the charge current (`power.charge_ma`), internal resistance, state-of-charge curve, and empty cutoff |
| `LATTER_PMU_TS_DISABLE`, `LATTER_PMU_DCDC_KEEP_MASK`, `LATTER_PMU_LDO_KEEP_MASK` | AXP2101 thermistor and rail keep-mask traits confirmed from the schematic |
| `LATTER_PANEL_DCS_SLEEP` | `mipi_spi` panels supporting `DISPOFF`/`SLPIN` in screen-off |
| `LATTER_DSI_IRAM_SAFE` | MIPI-DSI panels on ESP32-P4 (`800 ms` / `10%` OTA progress redraw throttle in `include/esphome_bridge.h`, paired with `CONFIG_GDMA_ISR_IRAM_SAFE` + `CONFIG_DW_GDMA_ISR_IRAM_SAFE`) |

### Light sleep (ESP32-S3 battery boards)

ESP32-S3 battery boards include `modules/hal/chip_esp32s3.yaml`, which configures `esp32_power_management` (`max_frequency: 240MHz`, `min_frequency: 40MHz`, `enable_light_sleep: true`, `profiling: ${pm_profiling}`), `CONFIG_PM_LIGHT_SLEEP_CALLBACKS: "y"`, Wi-Fi sleep IRAM/beacon-window options, `CONFIG_LWIP_DHCP_COARSE_TIMER_SECS: "10"`, `CONFIG_MDNS_TIMER_PERIOD_MS: "500"`, and `CONFIG_ESP_PANIC_HANDLER_IRAM: "y"` (never enable `CONFIG_RTC_CLK_SRC_INT_8MD256`, which causes watchdog resets).

In screen-off quiet on battery, `apply_power_policy` releases `latter_awake` via `latter_ui::pm_apply()` (`components/latter_core/power_pm.cpp`), arms the light-sleep wake pins, and stretches the main loop interval to **`1,000 ms`** (**`250 ms`** while touch wake is armed). If smoothed Wi-Fi RSSI drops below **`-65 dBm`** (clearing only after staying above **`-61 dBm`** for 2 minutes; `kWeakLink*` in `include/hal_link_report.h`), `weak_link` holds `latter_awake` so the Home Assistant API link stays stable at weak signal. Never enable light sleep on MIPI-DSI boards.

## 5. Substitutions and sdkconfig

- **Substitutions:** font sizes (`font_time_size` .. `font_games_size`), image resize bounds (`art_resize`, `sendspin_art_resize`), audio rates (`audio_out_rate: "48000"`, `audio_out_channels: "1"`, `audio_out_bits: "16"`, `audio_in_rate: "16000"`), buffer sizes (`sendspin_buffer_size`, `announcement_buffer_size`), `mww_internal_ram`, and product defaults (`default_clock_face`, `default_power_profile`, etc.). Display dimensions and roundness come strictly from `-DLATTER_SCREEN_W`, `-DLATTER_SCREEN_H`, and `-DLATTER_ROUND_DISPLAY` (no `screen_w`, `screen_h`, or `art_shape` substitutions).
- **sdkconfig (via `modules/hal/chip_esp32*.yaml`):** `CONFIG_COMPILER_OPTIMIZATION_PERF: "y"` (`-O2`) and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: "256"` on every board.

## 6. ESP32-P4

- **MIPI PHY rail (`chip_esp32p4.yaml`):** powered by `esp_ldo: channel: 3, voltage: 2.5V`.
- **Wi-Fi co-processor (`chip_esp32p4.yaml`):** `use_psram: true` under `esp32_hosted:`.
- **Cache line size:** **never** set `CONFIG_CACHE_L2_CACHE_LINE_128B: "y"` (breaks 64-byte-aligned `esp_hosted` PSRAM SDIO buffers with `ESP_ERR_INVALID_ARG`).
- **MIPI-DSI panels (`waveshare_p4_86_panel.yaml`):**
  - Enable `CONFIG_GDMA_ISR_IRAM_SAFE: "y"` and `CONFIG_DW_GDMA_ISR_IRAM_SAFE: "y"` (ESPHome `2026.11.0b4+` `mipi_dsi` automatically enables `CONFIG_LCD_DSI_ISR_CACHE_SAFE` and marks its DPI refresh callback `IRAM_ATTR`).
  - Add `-DLATTER_DSI_IRAM_SAFE=1` so OTA progress redraws throttle to `>= 10%` / `>= 800 ms` while DW-GDMA DPI scanout continues cleanly across flash erases.
- **Chip revision & DFS:** P4 boards run `esp32_power_management` in DFS-only mode (`min_frequency: 40MHz`, `enable_light_sleep: false`) and declare `p4_silicon` (`rev1` = `true` / `360MHz`, `rev3` = `false` / `400MHz`). Release CI publishes both silicon variants.

## 7. Device config, board facts, and validation

1. Author `boards/facts/<board_id>.json` against [`boards/facts/schema.json`](../boards/facts/schema.json).
2. Scaffold initial files and bring-up probes:
   ```bash
   python tools/new_board.py <board_id>
   python tools/new_board.py --bringup
   ```
3. Run the static and board-facts verifiers:
   ```bash
   python tools/check_boards.py --render-docs
   python tools/verify_core.py
   esphome compile devices/<my-device>.yaml
   ```
