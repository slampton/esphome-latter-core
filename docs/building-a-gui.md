# Building a GUI

The universal GUI ([`guis/universal/gui.yaml`](../guis/universal/gui.yaml)) is one consumer of the core. You can replace it with your own. This might be a different visual language, a single-purpose kiosk, or a non-touch device driven by a rotary encoder. You still get everything below it: the Latter protocol, audio and voice, power management, and the board HAL.

## What stays and what you replace

```yaml
packages:
  hal:                !include boards/<your_board>.yaml        # stays (or your own HAL)
  latter_audio_voice: !include modules/audio_voice.yaml        # stays
  latter_power:       !include modules/power_manager.yaml      # stays
  latter_client:      !include modules/latter_client.yaml      # stays
  my_gui:             !include guis/my_gui/gui.yaml            # yours
```

A GUI package needs three things:
1. **A `display:` render lambda** on the board's display, `id: disp1`, added with `!extend`.
2. **Touch handling** on the board's touchscreen, `id: touch_screen`, added with `!extend`. Leave this out on non-touch devices and drive input through `ui_input_event`. See [`rotary_input`](mixins.md#rotary_input).
3. **The color IDs that `latter_client.yaml` references.** It uses `theme_primary`, `theme_secondary`, `col_gold`, `col_warm_gold`, `col_orange`, `col_red`, and `col_green` in its theme presets. Define them, even if your GUI never draws with them.

The universal GUI's own top-level blocks are `substitutions`, `font`, `color`, `display`, and `touchscreen`. It defines no API actions and no Home Assistant subscriptions. Those all belong to the core, which is why a GUI can be replaced cleanly.

## Skeleton

```yaml
# guis/my_gui/gui.yaml
font:
  - file: "gfonts://Roboto"
    id: font_body
    size: 24

color:
  - { id: theme_primary,   hex: "4682B4" }
  - { id: theme_secondary, hex: "1E2A38" }
  - { id: col_gold,        hex: "FFC857" }
  - { id: col_warm_gold,   hex: "E0A040" }
  - { id: col_orange,      hex: "FF8C42" }
  - { id: col_red,         hex: "E5484D" }
  - { id: col_green,       hex: "3DD68C" }
  - { id: col_bg,          hex: "000000" }

display:
  - id: !extend disp1
    lambda: |-
      it.fill(id(col_bg));
      if (id(display_power_state) != 0) return;   // screensaver / off handled by core
      it.printf(SCREEN_CX, SCREEN_CY, id(font_body), id(theme_primary),
                TextAlign::CENTER, "%s", id(ha_area_context_name).state.c_str());

touchscreen:
  - id: !extend touch_screen
    on_release:
      - script.execute: { id: ui_input_event, action: "select", delta: 0 }
```

`ha_area_context_name` is the text sensor `latter_client.yaml` keeps in sync with the device's current area name. The other `ha_*` IDs are in the same file. Treat this as a starting shape, not a finished GUI.

---

## What the core gives you

### State (globals in `latter_client.yaml` and `power_manager.yaml`)

| Kind | IDs |
| :--- | :--- |
| Navigation | `gui_page`, `home_subpage`, `music_overlay_mode`, `area_subpage`, `remote_subpage`, `tools_subpage`, `sky_weather_subpage`, `system_menu_mode`, `active_game` |
| Power | `display_power_state` (0 active, 1 screensaver, 2 off), `is_hibernating` |
| Voice | `va_active`, `va_status_text`, `va_tts_received` |
| Alerts | `notif_*`, `alarm_*`, `timer_*` |
| List data | `slot_*` (browse), `area_slot_*`, `cal_slot_*`, filled by `set_browse_slots` and `set_area_slots` |
| Home Assistant data | `ha_*` sensors and text sensors for every attribute in the [device protocol](device-protocol.md#3c-sensors-to-subscribe-to) |
| Images | `album_art` (Sendspin), `remote_album_art`, `home_wallpaper_art`, `camera_snapshot_art`. These are decoded as RGB565 at `${screen_w}x${screen_h}`. |

Page IDs and sub-page enums are in [`ui_core.h`](../include/ui_core.h) as `PageId` and `ToolsSubpage`. You can reuse them or ignore them.

### Scripts you can call

| Script | Purpose |
| :--- | :--- |
| `ui_input_event(action, delta)` | The input router. `action` is one of `home`, `back`, `select`, `next`, `prev`. |
| `wake_to_active_state`, `screen_sleep_timer` | Wake the screen, and restart the idle timer after any interaction |
| `toggle_voice_assistant`, `cancel_voice_assistant` | Voice control |
| `fetch_*` | Request data pages from Latter: music, areas, calendar |
| `play_music_slot(int)`, `remote_send_key(int, int)`, `play_game_sfx(int)` | Actions |

Everything that calls `latter.*` services already lives in `latter_client.yaml`. A GUI shouldn't need to call `homeassistant.action` itself.

### Redraw scheduling

Don't call `id(disp1).update()` directly. Use the scheduler instead:

```cpp
latter_ui::schedule_disp_update(&id(disp1));       // coalesced: at most one redraw per ≥38 ms
latter_ui::schedule_anim_disp_update(&id(disp1));  // animation frame: 18 ms on P4, 26 ms on S3
```

Both functions accept either `DispT *` or `DispT *const *`, so the same call compiles from YAML action lambdas and from inside the `display:` lambda.

> **ESPHome lambda note.** `id(foo).bar` becomes `foo->bar`, but a bare `id(foo)` becomes the raw pointer `foo`. That's why both overloads exist. For the same reason, pass `id(sel)` without `&` to a generic `[](auto *sel, ...)` helper when working with `TemplateSelect`.

---

## Layout: `UiLayout` and scaling

All geometry should go through [`ui_core.h`](../include/ui_core.h) and [`ui_layout.h`](../include/ui_layout.h), so one GUI works on every screen size.

| Symbol | Meaning |
| :--- | :--- |
| `SCREEN_W`, `SCREEN_H`, `SCREEN_CX`, `SCREEN_CY`, `SCREEN_R`, `SCREEN_MIN_DIM` | Compile-time screen constants from `LATTER_SCREEN_W` and `LATTER_SCREEN_H` |
| `scale_x(v)`, `scale_y(v)`, `scale_s(v)` | Scale a value designed for the 466 px reference screen. They return the value unchanged on screens up to 520 px and scale it proportionally above that. |
| `scale_min(v)` | Always scales proportionally |
| `UiLayout` fields | `hdr_y`, `sub_hdr_y`, `home_bar_y`, `dock_y`, `app_dock_y`, pager rows, and `slot5_y()` |
| `UiLayout` hit-testers | `hit_area_card3`, `area_slider_pct`, `hit_tool_card3`, `hit_tools_row`, `hit_tools_col`, `hit_home_*`, music `hit_*`, `music_scrub_frac`, `hit_4col_stepper`, `hit_4col_calc` |
| `circle_half_span(y)`, `chord_half_span_int(y)` | Horizontal half-width of a round screen at row `y`, from the chord table |

**Pair every drawing coordinate with its hit-tester in `UiLayout`.** Raw pixel values in touch handlers are the most common cause of layouts that work on one screen and break on another.

## Rendering on these chips

What makes the universal GUI fast enough on an ESP32-S3:
- **Draw row-major.** ESPHome's display buffer is row-major, so loop `y` on the outside and `x` on the inside. Group runs of identical pixels into `horizontal_line` calls. Avoid `it.image()` for large images; draw with the row blitter in `ui_core.h` instead.
- **Dim without losing color.** Halve each channel by shifting (`r >>= 1; g >>= 1; b >>= 1`). Don't use packed bitmasks, which throw away color depth.
- **Latch the full window on rectangular panels.** At the top of the render lambda, touch `(0,0)` and `(W−1,H−1)`. The panel driver then flushes the whole frame in one transfer.
- **Use the lookup tables.** `fast_sin` and `fast_cos` use a 60-entry LUT with interpolation. Round-screen clipping uses `circle_half_span`. Keep `sinf` and `sqrtf` out of per-pixel loops.
- **Measure.** Turn on **System: Telemetry Profiler**. `[PROF:FRAME]` logs render time per page, and frames over 22 ms are flagged.

## Capability flags

Branch on these, not on product names. Defaults are in [`ui_core.h`](../include/ui_core.h). Boards set them as build flags.

| Flag | Default | Meaning |
| :--- | :--- | :--- |
| `LATTER_SCREEN_W`, `LATTER_SCREEN_H` | 466 | Resolution |
| `LATTER_ROUND_DISPLAY` | 1 | Round bezel clipping |
| `LATTER_CORNER_R` | W/2 if round, 98 if wearable, otherwise 20 | Corner radius for bezel effects |
| `LATTER_HAS_DISPLAY`, `_TOUCH`, `_AUDIO`, `_IMU`, `_BATTERY` | 1 | Hardware present |
| `LATTER_HAS_MIC`, `LATTER_HAS_SPEAKER` | = `HAS_AUDIO` | |
| `LATTER_IS_STATIONARY`, `LATTER_IS_WEARABLE` | 0 | Form factor |
| `LATTER_PANEL_IPS` | 1 if `LATTER_BACKLIGHT_PIN` is set | Backlit LCD (PWM brightness) rather than AMOLED |
| `LATTER_CHIP_P4`, `LATTER_PSRAM_MB` | Derived from the IDF target | |

With `LATTER_HAS_TOUCH=0`, the focus ring stays visible permanently, so a rotary-only device is usable.
