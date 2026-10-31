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
| `hal_set_imu_power_mode` | `ctrl7_val: int` | IMU power mode. 0 means off. |
| `hal_init_pmu_irq` | — | Configure the PMU interrupt at boot |
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
  - PMU: `hal_poll_pmu_1s`, `hal_poll_pmu_button_50ms(did_ss_draw: bool)`, `hal_wait_pmu_button_release`, `usb_connected`, `batt_charging`, `batt_level`, `batt_voltage`.
  - IMU: `accel_x`, `accel_y`, `accel_z`, `imu_roll`, `imu_pitch`, `imu_temp`, `motion_mag`.
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
| `LATTER_PMU_IRQ_PIN`, `LATTER_PMU_IRQ_ACTIVE_HIGH`, `LATTER_NO_PMU_IRQ_PIN` | The PMU interrupt pin and its polarity, or no pin at all |
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
  - See [`waveshare_p4_4b.yaml`](../boards/waveshare_p4_4b.yaml).
- **Chip revision:** set `engineering_sample: true` only for P4 silicon earlier than revision 3.0.

## 7. Checklist

Before you call a board done, check each of these against the schematic:
1. **Display:** bus type, reset pin, offsets, color order, inversion, mirroring.
2. **Touch:** chip and I2C address, interrupt and reset pins. If the driver has no `reset_pin`, pulse reset in `hal_release_deep_sleep_holds`.
3. **Audio:** two I2S controllers or one shared bus; codec addresses; amplifier enable pin.
4. **Power:** PMU interrupt pin and polarity; which pins to hold during hibernate.
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

If you get a new board working, a pull request adding its board file is welcome.
