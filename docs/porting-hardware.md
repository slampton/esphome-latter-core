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
| `boot_button` | `binary_sensor` | `latter_client.yaml` extends it with wake and dismiss behavior |
| `bus_internal` | `i2c` | The bus shared by the PMU, IMU, codecs, and touch |
| `latter_mic` | `microphone` | |
| `physical_speaker` | `speaker` | On a single shared I2S bus, set `timeout: 500ms` |
| `speaker_enable` | `switch` | Amplifier enable |
| `wifi_ctrl` | `wifi` | Usually defined in the device file |
| `ble_ctrl` | `esp32_ble` | Usually defined in the device file |
| `latter_firmware_update` | `update` | Usually defined in the device file |

## 2. HAL scripts

The core calls these. Any of them can be a no-op if the hardware doesn't need it.

| Script | Parameters | Purpose |
| :--- | :--- | :--- |
| `hal_route_i2s_to_spk` | — | Switch shared I2S clock pins to the speaker controller |
| `hal_route_i2s_to_mic` | — | Switch them to the microphone controller |
| `hal_set_dac_mute` | `muted: bool` | Mute the codec output |
| `hal_set_audio_codecs_standby` | `standby: bool` | Put the codecs into low-power standby, or wake them |
| `hal_set_imu_power_mode` | `ctrl7_val: int` | IMU power mode: `0x00` off, `0x01` accelerometer, `0x03` accelerometer + gyro, `0x80` hardware wake-on-motion (no polling) |
| `hal_init_pmu` | — | Configure the PMU at boot: mask IRQs, apply rail, TS, and charger traits (no-op in `pmu_none.yaml`) |
| `hal_release_deep_sleep_holds` | — | Release GPIO holds at boot, and pulse touch reset if the touch driver can't |
| `hal_enter_hardware_hibernate` | — | Hold pins and cut power. Called after the core has saved state and disconnected from the API. |

**Audio routing:**
- **Two I2S controllers sharing clock pins** (all the S3 boards): implement the two `hal_route_*` scripts by remapping the clock pins through the GPIO matrix with `esp_rom_gpio_connect_out_signal`. See [`waveshare_s3_175c.yaml`](../boards/waveshare_s3_175c.yaml).
- **One shared I2S bus** (the P4 boards): the routing scripts are no-ops. The core stops one direction and waits for it to release before starting the other.

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
    - "-DLATTER_CHIP_P4=0"
    - "-DLATTER_PSRAM_MB=8"
```

Define every capability flag explicitly, even where it matches the default. This is the set [`waveshare_s3_216.yaml`](../boards/waveshare_s3_216.yaml) uses. Defaults are listed in [Building a GUI → Capability flags](building-a-gui.md#capability-flags).

Optional hardware flags:

| Flag | When |
| :--- | :--- |
| `LATTER_BACKLIGHT_PIN`, `LATTER_BACKLIGHT_EN_PIN`, `LATTER_BACKLIGHT_INVERTED` | LCD panels with a PWM backlight |
| `LATTER_BACKLIGHT_PWM_HZ`, `LATTER_BACKLIGHT_PWM_BITS`, `LATTER_BACKLIGHT_DUTY_MIN_PCT`, `LATTER_BACKLIGHT_DUTY_MAX_PCT` | Backlight PWM frequency, resolution, and usable duty window. Defaults: 2000 Hz, 8-bit, 0-100 %. Analog-dimming backlights (PWM into an RC filter on the boost converter's feedback pin) need a frequency well above the RC corner, at least 5 kHz. |
| `LATTER_PWR_KEY_PIN`, `LATTER_PWR_KEY_ACTIVE_HIGH`, `LATTER_PWR_KEY_DEEP_SLEEP_WAKE` | The GPIO that reads the power key (for example the AXP2101 `SYS_OUT` net), its polarity, and whether it is RTC/LP-capable and can wake the chip from deep sleep. Must match the `pwr_key` binary sensor's pin. |
| `LATTER_PMU_TS_DISABLE`, `LATTER_PMU_DCDC_KEEP_MASK`, `LATTER_PMU_LDO_KEEP_MASK`, `LATTER_PMU_CHARGE_MA` | AXP2101 traits: no NTC on TS; which DCDC and LDO rails stay on (everything else with no load is turned off); charge current from the fitted cell's capacity, using min(0.8C, 500 mA) rounded down to the AXP2101 step (25 mA steps up to 200 mA, then 100 mA steps). Leave a trait undefined if the schematic doesn't confirm it. |
| `LATTER_PANEL_DCS_SLEEP` | Self-emissive QSPI DCS panels (for example CO5300 AMOLED) on `mipi_spi`: screen-off sends DISPOFF + SLPIN and screen-on sends SLPOUT + DISPON, instead of only setting brightness to 0 |
| `LATTER_CORNER_R` | A corner radius other than the default |
| `LATTER_DSI_IRAM_SAFE` | MIPI-DSI panels. See [ESP32-P4](#6-esp32-p4). |

Copy the exact values from the closest existing board file.

## 5. Substitutions and sdkconfig

- **Substitutions:** font sizes (`font_time_size`, `font_small_size`, and so on), `art_shape`, and power defaults (`default_power_profile`, `default_tap_wake`, ...). Copy the set from the closest existing board and adjust.
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
3. **Audio:** two I2S controllers or one shared bus; codec addresses; amplifier enable pin.
4. **Power:** the power key pin and polarity (on Waveshare AXP2101 boards that is `SYS_OUT`, not `AXP_IRQ`, which isn't routed); the PMU rail map, TS pin, and charge current; which pins to hold during hibernate.
5. **IMU:** whether one is present, and axis orientation.
6. **P4 only:** LDO, SDIO and esp-hosted settings, DSI IRAM-safe flags.
7. **Memory:** `-O2`, `SPIRAM_MALLOC_ALWAYSINTERNAL`, full-frame buffer.
8. **Layout:** walk every page with **System: Telemetry Profiler** on. Check for clipped elements and frames over 22 ms.

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

If you get a new board working, a pull request adding its board file and facts file is welcome.
