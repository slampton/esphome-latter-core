# Porting to New Hardware

Each supported board is one file in `boards/`. That file describes the hardware, and everything above it is shared. Porting means writing that file, plus a short device config that combines it with the core.

**Start from [`examples/custom_display_template.yaml`](../examples/custom_display_template.yaml).** It's a complete 480×480 board definition with stub HAL scripts, and CI validates it on every push.

---

## 1. Required IDs

The core modules refer to these IDs. Your board or device file must define each one.

| ID | Component | Notes |
| :--- | :--- | :--- |
| `disp1` | `display` | Full-frame buffer in PSRAM. The GUI adds its render lambda with `!extend`. |
| `touch_screen` | `touchscreen` | The GUI adds gesture handling with `!extend` |
| `boot_button` | `binary_sensor` | `latter_client.yaml` extends it: wake, dismiss alerts, game button, start or stop a voice turn |
| `bus_internal` | `i2c` | The bus shared by the PMU, IMU, codecs, and touch |
| `latter_mic` | `microphone` | 16 kHz (`audio_in_rate`) mono. On a full-duplex bus faster than that, a `resampler` microphone over the I2S mic |
| `physical_speaker` | `speaker` | Set `timeout: 4s` (longer than the 3 s idle power-down, so gaps between tracks never stop the I2S clocks under a powered amplifier) |
| `speaker_enable` | `switch` | Amplifier enable. Only `route_i2s_to_spk` turns it on, after the I2S clocks run |
| `wifi_ctrl` | `wifi` | Usually defined in the device file |
| `ble_ctrl` | `latter_beacon` (advertising only, no host stack: VHCI on an on-chip controller, esp_hosted on the ESP32-P4) | Defined in the device file |
| `latter_firmware_update` | `update` | Usually defined in the device file |

## 2. HAL scripts

The core calls these. Any of them can be a no-op if the hardware doesn't need it.

| Script | Parameters | Purpose |
| :--- | :--- | :--- |
| `hal_route_i2s_to_spk` | — | Reserved for a board that must hand its I2S bus to the speaker. A no-op on a full-duplex bus (every supported board). |
| `hal_route_i2s_to_mic` | — | The same, towards the microphone. A no-op on a full-duplex bus. |
| `hal_set_dac_mute` | `muted: bool` | Mute the codec output |
| `hal_set_audio_codecs_standby` | `standby: bool` | Put the codecs into low-power standby, or wake them |
| `hal_set_imu_power_mode` | `ctrl7_val: int` | IMU power mode: `0x00` off, `0x01` accelerometer, `0x03` accelerometer + gyro, `0x80` hardware wake-on-motion (no polling) |
| `hal_init_pmu` | — | Configure the PMU at boot: mask IRQs, apply rail, TS, and charger traits (no-op in `pmu_none.yaml`) |
| `hal_release_deep_sleep_holds` | — | Release GPIO holds at boot, and pulse touch reset if the touch driver can't |
| `hal_enter_hardware_hibernate` | — | Hold pins and cut power. Called after the core has saved state and disconnected from the API. |

**Audio routing:**
- **One full-duplex I2S bus** (every supported board): set `full_duplex: true` on the bus and run the codecs, the speaker and an I2S microphone (e.g. `latter_mic_i2s`) at `audio_out_rate` (48000 where the codecs allow). `latter_mic` is then a `resampler` microphone at `audio_in_rate` (16000) for wake word and voice. The routing scripts are no-ops. See [`waveshare_p4_86_panel.yaml`](../boards/waveshare_p4_86_panel.yaml).
- **Separate controllers per codec** (an ES8311 DAC plus an ES7210 ADC, as on the S3 boards): put both data pins on the one full-duplex bus; they share BCLK/WS/MCLK. See [`waveshare_s3_216.yaml`](../boards/waveshare_s3_216.yaml).
- Never put a `resampler` speaker in the output path: every source arrives at `audio_out_rate`, and the sounds come from `sounds/<audio_out_rate>/` (`python tools/gen_sounds.py` renders a new rate).

## 3. PMU and IMU modules

Include exactly one of each from your board file:

```yaml
packages:
  latter_pmu: !include ../modules/pmu_axp2101.yaml   # or pmu_none.yaml for USB or mains power
  latter_imu: !include ../modules/imu_qmi8658.yaml   # or imu_none.yaml
```

- **Different PMU or IMU chip:** write a module that provides the same scripts and sensors.
  - PMU: `hal_init_pmu`, `hal_poll_pmu_1s`, `hal_wait_pwr_key_release`, `usb_connected`, `batt_charging`, `batt_level`, `batt_voltage`. The board file defines the PWR key itself as a GPIO `binary_sensor` with `id: pwr_key`; `pmu_axp2101.yaml` adds the short and long press handling.
  - IMU: `accel_x`, `accel_y`, `accel_z`, `imu_roll`, `imu_pitch`, `imu_temp`, `motion_mag`.
- **QMI8658 interrupt:** set the `imu_int1_pin` substitution to the GPIO wired to the IMU's INT1. With the screen off, raise-to-wake waits on a hardware wake-on-motion interrupt instead of polling the accelerometer.
- **Axis inversions:** keep board-specific inversions, such as `- multiply: -1.0`, in the board file, not in the module.

## 4. Build flags

```yaml
esphome:
  build_flags:
    - "-DLATTER_SCREEN_W=480"
    - "-DLATTER_SCREEN_H=480"
    - "-DLATTER_ROUND_DISPLAY=0"
    - "-DLATTER_HAS_DISPLAY=1"
    - "-DLATTER_HAS_TOUCH=1"
    - "-DLATTER_HAS_AUDIO=1"
    - "-DLATTER_HAS_IMU=1"
    - "-DLATTER_HAS_BATTERY=1"
    - "-DLATTER_IS_STATIONARY=0"
    - "-DLATTER_IS_WEARABLE=0"
    - "-DLATTER_PANEL_IPS=0"
    - "-DLATTER_PANEL_PPI=<ppi>"   # your panel: diagonal pixels ÷ diagonal inches
    - "-DLATTER_CHIP_P4=0"
    - "-DLATTER_PSRAM_MB=8"
```

Define every capability flag explicitly, even where it matches the default. This is the set [`waveshare_s3_216.yaml`](../boards/waveshare_s3_216.yaml) uses. Defaults are listed in [Building a GUI → Capability flags](building-a-gui.md#capability-flags). `LATTER_PANEL_PPI` is required: the touch thresholds are in millimetres and convert through it. Compute it as the diagonal in pixels divided by the diagonal in inches (for a round panel, the diameter). How each flag changes the UI is in [Device UI design guide → Per-board adaptation](device-ui-design.md#6-per-board-adaptation).

Optional hardware flags:

| Flag | When |
| :--- | :--- |
| `LATTER_BACKLIGHT_PIN`, `LATTER_BACKLIGHT_EN_PIN`, `LATTER_BACKLIGHT_INVERTED` | LCD panels with a PWM backlight |
| `LATTER_BACKLIGHT_PWM_HZ`, `LATTER_BACKLIGHT_PWM_BITS`, `LATTER_BACKLIGHT_DUTY_MIN_PCT`, `LATTER_BACKLIGHT_DUTY_MAX_PCT` | Backlight PWM frequency, resolution, and usable duty window. Defaults: 2000 Hz, 8-bit, 0-100 %. Analog-dimming backlights (PWM into an RC filter on the boost converter's feedback pin) need a frequency well above the RC corner, at least 5 kHz. |
| `LATTER_PWR_KEY_PIN`, `LATTER_PWR_KEY_ACTIVE_HIGH`, `LATTER_PWR_KEY_DEEP_SLEEP_WAKE` | The GPIO that reads the power key (for example the AXP2101 `SYS_OUT` net), its polarity, and whether it is RTC/LP-capable and can wake the chip from deep sleep. Must match the `pwr_key` binary sensor's pin. |
| `LATTER_ACTION_BTN_PIN`, `LATTER_TOUCH_INT_PIN`, `LATTER_IMU_INT1_PIN` | Boards with light sleep (see below): the action button (`boot_button`), touch interrupt and QMI8658 INT1 GPIOs, armed with `LATTER_PWR_KEY_PIN` as light-sleep wake sources. `LATTER_IMU_INT1_PIN` must equal the `imu_int1_pin` substitution. Mark each pin `light_sleep_wake` in the board facts. |
| `LATTER_TOUCH_RST_PIN` | A touch reset the HAL drives with raw `gpio_set_level` (touch platforms without `reset_pin:`), so it keeps driving in light sleep |
| `LATTER_PMU_TS_DISABLE`, `LATTER_PMU_DCDC_KEEP_MASK`, `LATTER_PMU_LDO_KEEP_MASK`, `LATTER_PMU_CHARGE_MA` | AXP2101 traits: no NTC on TS; which DCDC and LDO rails stay on (everything else with no load is turned off); charge current from the fitted cell's capacity, using min(0.8C, 500 mA) rounded down to the AXP2101 step (25 mA steps up to 200 mA, then 100 mA steps). Leave a trait undefined if the schematic doesn't confirm it. |
| `LATTER_PANEL_DCS_SLEEP` | Self-emissive QSPI DCS panels (for example CO5300 AMOLED) on `mipi_spi`: screen-off sends DISPOFF + SLPIN and screen-on sends SLPOUT + DISPON, instead of only setting brightness to 0 |
| `LATTER_CORNER_R` | A corner radius other than the default |
| `LATTER_DSI_IRAM_SAFE` | MIPI-DSI panels. See [ESP32-P4](#6-esp32-p4). |

Copy the exact values from the closest existing board file.

### Light sleep (ESP32-S3 battery boards)

A battery board on the ESP32-S3 enables automatic light sleep in screen-off quiet:

```yaml
substitutions:
  pm_profiling: "false"   # "true" for bench builds: per-lock and per-mode times in [PROF:PM_LOCKS]

esp32:
  framework:
    sdkconfig_options:
      CONFIG_PM_LIGHT_SLEEP_CALLBACKS: "y"   # the wake pins arm inside the light-sleep callbacks
      # Wi-Fi wake timing: sleep code in IRAM, and the beacon-lost receive window
      CONFIG_ESP_WIFI_SLP_IRAM_OPT: "y"
      CONFIG_PM_SLP_IRAM_OPT: "y"
      CONFIG_PM_RTOS_IDLE_OPT: "y"
      CONFIG_ESP_WIFI_SLP_BEACON_LOST_OPT: "y"
      CONFIG_ESP_WIFI_SLP_BEACON_LOST_OVER_THRESHOLD_RECEIVE: "y"
      CONFIG_ESP_WIFI_SLP_BEACON_LOST_THRESHOLD: "2"
      CONFIG_ESP_WIFI_SLP_PHY_ON_DELTA_EARLY_TIME: "16"
      CONFIG_ESP_WIFI_SLP_PHY_OFF_DELTA_TIMEOUT_TIME: "4"
      CONFIG_ESP_WIFI_SLP_DEFAULT_MIN_ACTIVE_TIME: "60"

power_management:
  - platform: esp32_power_management
    max_frequency: 240MHz   # the board's CPU frequency
    min_frequency: 40MHz
    enable_light_sleep: true
    profiling: ${pm_profiling}
```

Declare the wake-pin traits above. Every pin an ESPHome component owns keeps its configuration in light sleep automatically; any output the HAL drives with raw `gpio_set_level` at runtime needs a trait that `hal_power_pm.h` handles (`LATTER_TOUCH_RST_PIN`, the backlight pins). Bench-check the `[PROF:PM]` light-sleep share and the wake latency of each pin before shipping, and leave the RTC sleep clock at its default (the `CONFIG_RTC_CLK_SRC_INT_8MD256` clock caused watchdog resets). Overnight, the System: Link Report sensor shows whether Wi-Fi still hears the access point in light sleep (beacon timeouts, disconnects, signal). Don't enable light sleep on boards that must stay awake for their display bus (MIPI-DSI).

## 5. Substitutions and sdkconfig

- **Substitutions:** font sizes (`font_time_size`, `font_small_size`, and so on), `art_shape`, and power defaults (`default_power_profile`, `default_tap_wake`, ...). Copy the set from the closest existing board and adjust. The font roles and per-board sizes are in [Device UI design guide → Typography](device-ui-design.md#12-typography).
- **sdkconfig, required on every board:**

```yaml
esp32:
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_COMPILER_OPTIMIZATION_PERF: "y"        # -O2
      CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: "256"    # keep small allocations in internal RAM for DMA
```

- **Display buffer:** use a full-frame buffer in PSRAM. A partial buffer makes the driver run the whole render lambda several times per frame.

## 6. ESP32-P4

- **Power rail:** power the MIPI PHY with `esp_ldo: channel: 3, voltage: 2.5V`.
- **Wi-Fi co-processor:** set `use_psram: true` under `esp32_hosted:`.
- **Cache line size:** **do not** set `CONFIG_CACHE_L2_CACHE_LINE_128B`. It breaks the 64-byte-aligned SDIO buffers used by the Wi-Fi co-processor, and boot fails with `ESP_ERR_INVALID_ARG`.
- **MIPI-DSI panels:**
  - Enable `CONFIG_LCD_DSI_ISR_CACHE_SAFE`, `CONFIG_GDMA_ISR_IRAM_SAFE`, and `CONFIG_DW_GDMA_ISR_IRAM_SAFE`.
  - Add `-DLATTER_DSI_IRAM_SAFE=1` and `-Wl,--wrap=esp_lcd_dpi_panel_register_event_callbacks`. This swaps in an IRAM-resident callback, so the panel keeps scanning out during OTA flash erases.
  - See [`waveshare_p4_86_panel.yaml`](../boards/waveshare_p4_86_panel.yaml).
- **Chip revision:** P4 boards declare a silicon profile in `substitutions:` (`p4_silicon`, `p4_engineering_sample`, `p4_cpu_frequency`), with `rev1` = `true` / `360MHz` and `rev3` = `false` / `400MHz`. Builds for one revision don't boot the other, so the release workflow publishes both. See [`waveshare_p4_35.yaml`](../boards/waveshare_p4_35.yaml).

## 7. Checklist

Before you call a board done, check each of these against the schematic:
1. **Display:** bus type, reset pin, offsets, color order, inversion, mirroring.
2. **Touch:** chip and I2C address, interrupt and reset pins. If the driver has no `reset_pin`, pulse reset in `hal_release_deep_sleep_holds`.
3. **Audio:** one full-duplex I2S bus (shared BCLK/WS/MCLK, DIN and DOUT pins); codec addresses; amplifier enable pin; whether the codecs run at 48 kHz.
4. **Power:** the power key pin and polarity (on Waveshare AXP2101 boards that is `SYS_OUT`, not `AXP_IRQ`, which isn't routed); the PMU rail map, TS pin, and charge current; which pins to hold during hibernate.
5. **IMU:** whether one is present, and axis orientation.
6. **P4 only:** LDO, SDIO and esp-hosted settings, DSI IRAM-safe flags.
7. **Memory:** `-O2`, `SPIRAM_MALLOC_ALWAYSINTERNAL`, full-frame buffer.
8. **Layout:** walk every page with **System: Telemetry Profiler** on. Check for clipped elements and `[SLOW_FRAME]` lines; the frame budget is in [Device UI design guide → Performance](device-ui-design.md#8-performance).

## 8. Device config and validation

Create a device file that combines your board with the core. Model it on any file in [`devices/`](../devices/), and give it its own `project.name`, BLE beacon minor, and update source.

```bash
esphome config devices/my-device.yaml    # fast schema check
esphome compile devices/my-device.yaml   # full build
```

### Board facts

Every board also has a hardware facts file, `boards/facts/<board_id>.json` (schema: [`boards/facts/schema.json`](../boards/facts/schema.json)). It lists each routed net with its role, polarity, rail and a confidence level: **H** validated on hardware, **M** schematic or vendor code, **L** unconfirmed. Use the schematic first, vendor board-support code second, and wiki tables last, and record any disagreements in `known_doc_errata`.

```bash
python tools/check_boards.py               # cross-check facts against the board and device YAML
python tools/check_boards.py --render-docs # regenerate the pin maps in docs/hardware.md
```

The checker fails on any GPIO the board file uses without a fact, use of a reserved or unrouted pin, `LATTER_*` flags that disagree with the facts, unknown I2C addresses, a deep-sleep wake pin that can't wake the chip, a backlight PWM below the panel's minimum, a duplicate BLE minor, or a missing release job. A tagged release also fails while any **L** fact remains.

### Scaffolding and bring-up

Once the facts file exists, generate a starting point and a bench probe:

```bash
python tools/new_board.py <board_id>     # boards/<id>.yaml, devices/<name>.yaml, examples/bringup/<id>.yaml
python tools/new_board.py --bringup      # regenerate every examples/bringup/<id>.yaml from the facts
```

The scaffolder never overwrites an existing file without `--force`, and the files it writes mark every remaining decision with `TODO`. The bring-up probe has no Latter core. Flash it over USB and it logs the silicon revision, flash and PSRAM, every expected I2C device as present or missing (plus any unexpected ones), the AXP2101 state on battery boards, and an edge for every key and interrupt pin. It also pulses each output (amplifier, haptics, SD power, backlight enable) and sweeps the backlight. Attach that log to the pull request: it is the evidence that upgrades **M** and **L** facts to **H**.

If you get a new board working, a pull request against `dev` adding its board file and facts file is welcome.
