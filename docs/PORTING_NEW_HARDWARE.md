# Porting Latter OS to New Hardware (ESP32-S3 & ESP32-P4)

Because `esphome-latter-core` separates the **Hardware Abstraction Layer (HAL)** from the **UI & Application Engine (`modules/display_ui.yaml` + `include/*.h`)**, porting the full Latter OS to a new screen size, display shape, or processor takes ~15 minutes.

---

## Step 1: Create Your Board HAL (`boards/<my_board>.yaml`)

A board HAL file defines four things:
1. **Compile-Time Display Geometry Flags**:
   ```yaml
   esphome:
     build_flags:
       - "-DPOCKET_UI_SCREEN_W=480"
       - "-DPOCKET_UI_SCREEN_H=480"
       - "-DPOCKET_UI_ROUND_DISPLAY=0" # 1 = Circular bezel chord clipping, 0 = Square/Rectangular
   ```
2. **Display & Touchscreen Components** (`id: disp1` and `id: touch_screen`).
3. **Audio Components** (`id: physical_speaker`, `id: pocket_mic`, `id: es8311_dac`, `id: speaker_enable`) and **IMU** (`id: imu`).
4. **HAL Scripts**:
   - `hal_route_i2s_to_spk` & `hal_route_i2s_to_mic` (can be no-ops on boards with independent I2S buses).
   - `hal_release_deep_sleep_holds`, `hal_init_pmu_irq`, `hal_enter_hardware_hibernate`, `hal_set_imu_power_mode`, `hal_set_audio_codecs_standby`.

---

## Step 2: Choose Your Power Module

- **Battery Handhelds/Watches with AXP2101 PMIC**: Include `modules/power_axp2101.yaml`.
- **Wall Panels, Desk Displays, or Non-AXP2101 Boards**: Include `modules/power_generic.yaml`.

---

## Step 3: Create the Top-Level Device Manifest

In your device YAML, set the geometry substitutions (`screen_w`, `screen_h`, `art_resize`, `art_shape`) and load `esphome-latter-core`:

```yaml
substitutions:
  name: "my-panel"
  friendly_name: "My Panel"
  version: "2.2.0"
  screen_w: "480"
  screen_h: "480"
  art_resize: "480x480"
  art_shape: "square"
  firmware_update_entity: "update.my_panel_project_firmware"
  local_player_id: "media_player.my_panel"
  active_media_sensor: "sensor.my_panel_active_media"
  weather_forecast_sensor: "sensor.my_panel_weather_forecast"
  room_context_sensor: "sensor.my_panel_room_context"
  calendar_agenda_sensor: "sensor.my_panel_calendar_agenda"
  remote_config_sensor: "sensor.my_panel_remote_config"

external_components:
  - source:
      type: git
      url: https://github.com/slampton/esphome-latter-core
      ref: main
    components: [latter_core]

latter_core:

packages:
  hal: !include boards/my_board.yaml
  latter_core:
    url: https://github.com/slampton/esphome-latter-core
    ref: main
    files:
      - modules/audio_voice.yaml
      - modules/power_generic.yaml
      - modules/display_ui.yaml
```

---

## Step 4: How `ui_layout.h` Adapts to New Resolutions & ESP32-P4

- **Center-Relative Coordinates (`SCREEN_CX`, `SCREEN_CY`, `SCREEN_W`, `SCREEN_H`)**: All cards, 4-column steppers (`hit_4col_stepper`), calculator keys (`hit_4col_calc`), docks, and confirmation dialogs automatically center around `SCREEN_CX` and `SCREEN_CY`.
- **ESP32-P4 Readiness**: `pocket_ui::hal_calibrate_rtc()` and `pocket_ui::hal_set_wifi_power_save()` in `include/ui_layout.h` use `#if defined(CONFIG_IDF_TARGET_ESP32S3)` and `#if !defined(CONFIG_IDF_TARGET_ESP32P4)` guards so RISC-V ESP32-P4 targets compile cleanly without Xtensa assembly or S3-specific Wi-Fi calls.
