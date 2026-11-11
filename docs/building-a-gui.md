# Building a GUI

The universal GUI ([`guis/universal/gui.yaml`](../guis/universal/gui.yaml)) is one consumer of the core. You can replace it with your own. This might be a different visual language, a single-purpose kiosk, or a non-touch device driven by a rotary encoder. You still get everything below it: the Latter protocol, audio and voice, power management, and the board HAL.

The visual, touch and rendering rules the universal GUI follows (tokens, components, navigation, gestures, the frame pipeline, per-board adaptation and performance) are in the [Device UI design guide](device-ui-design.md). It is the canonical reference; this page covers what a replacement GUI must provide and what the core gives it.

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
2. **Touch handling** on the board's touchscreen, `id: touch_screen`, added with `!extend`. Leave this out on non-touch devices and drive input through `ui_input_event`.
3. **The color IDs the core writes.** The theme preset script (`apply_theme_preset` in `modules/features/scripts_actions.yaml`, included by `latter_client.yaml`) writes `theme_primary` and `theme_secondary` when the user picks a color profile. Define them, even if your GUI never draws with them.

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
    auto_clear_enabled: false
    lambda: |-
      latter_ui::frame_hits().clear();
      const uint32_t ov_sig = latter_ui::compute_takeover_signature(
          id(ota_in_progress), id(is_hibernating), id(display_brightness).current_values.is_on(),
          id(display_power_state), id(alarm_ringing) || id(timer_ringing) || id(find_my_active),
          id(va_active) || !id(va_status_text).empty(),
          latter_ui::ui::pin_pad_state().open, id(notif_active));
      latter_ui::dirty_band().begin_frame(id(gui_page), 0, ov_sig, false);
      const int y0 = latter_ui::dirty_band().y0();
      const int y1 = latter_ui::dirty_band().y1();
      it.draw_pixel_at(0, y0, Color(8, 8, 8));
      it.draw_pixel_at(0, y0, id(col_bg));
      it.draw_pixel_at(latter_ui::SCREEN_W - 1, y1, Color(8, 8, 8));
      it.draw_pixel_at(latter_ui::SCREEN_W - 1, y1, id(col_bg));
      latter_ui::bind_frame_buffer(id(disp1), it);
      latter_ui::frame_base_begin();
      auto now = id(ha_time).now();
      if (!now.is_valid()) now = id(sntp_time).now();
      if (latter_ui::render_system_takeovers(
              it, latter_ui::SCREEN_CX, latter_ui::SCREEN_CY, millis(), now, id(is_24h_format),
              id(ota_in_progress), id(ota_progress_pct), id(is_hibernating), id(hibernate_countdown_val),
              id(display_brightness).current_values.is_on(), id(display_power_state),
              id(alarm_ringing), id(alarm_ringing_idx),
              id(alarm1_recurrence), id(alarm2_recurrence), id(alarm3_recurrence),
              id(timer_ringing), id(find_my_active), id(va_timer_name),
              id(va_active), id(va_status_text), id(va_tts_received), id(va_vad_active),
              std::string(id(va_visual_profile).current_option()),
              id(notif_active), id(notif_title), id(notif_message), id(notif_priority),
              id(camera_snapshot_art), id(notif_camera_url), id(current_loaded_camera_url),
              std::string(id(screensaver_style_select).current_option()),
              id(font_body), id(font_body), id(font_body), id(font_body), id(font_body),
              id(theme_primary), id(theme_secondary), id(col_bg), id(col_bg), id(theme_primary),
              id(theme_secondary), id(theme_secondary), id(col_bg), id(col_gold), id(col_red), id(col_gold))) {
        return;
      }
      latter_ui::frame_base(it, id(col_bg));
      latter_ui::fast_print(it, latter_ui::SCREEN_CX, latter_ui::SCREEN_CY, id(font_body),
                            id(theme_primary), TextAlign::CENTER,
                            latter_ui::area_ctx_state().area_name.c_str());

touchscreen:
  - id: !extend touch_screen
    on_release:
      - script.execute: { id: ui_input_event, action: "select", delta: 0 }
```

`latter_ui::render_system_takeovers(...)` ([`ui_compositor.h`](../include/ui_compositor.h)) gives any custom GUI all 8 system overlays out of the box (OTA progress, hibernation countdown, panel-off skip, ringing alarms/timers/Find My, voice assistant, the 3-tier Security PIN pad modal `ui::pin_pad_modal`, push notifications/camera snapshots, and docked screensavers; see [`examples/custom_display_template.yaml`](../examples/custom_display_template.yaml)). `latter_ui::area_ctx_state().area_name` is the active Area name pushed via `latter_push_v3` (`domain: "area_context"`). All five domain state structs live in [`latter_state.h`](../include/latter_state.h). Headless nodes without a display set `latter_core: gui: "none"`, which skips compiling `latter_ui` altogether.

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
| Home Assistant data | `latter_ui::area_ctx_state()`, `latter_ui::media_state()`, `latter_ui::remote_state()`, `latter_ui::weather_state()` (including `sun_*` and `moon_phase`), and `latter_ui::calendar_state()` in [`latter_state.h`](../include/latter_state.h), populated by `latter_push_v3` in the [device protocol](device-protocol.md#3a-latter_push_v3-protocol-v3-domain-push) |
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

Everything that emits Latter RPC calls already lives in `latter_client.yaml` (`latter_rpc::emit`). A GUI shouldn't need to emit RPC envelopes itself.

### Redraw scheduling

Don't call `id(disp1).update()` directly. Use the scheduler instead:

```cpp
latter_ui::schedule_disp_update(&id(disp1));                // coalesced full-frame redraw
latter_ui::schedule_band_disp_update(&id(disp1), y0, y1);   // coalesced 2px-aligned horizontal band [y0, y1]
latter_ui::schedule_anim_disp_update(&id(disp1));           // animation frame: 18 ms on P4, 26 ms on S3
```

All three functions accept either `DispT *` or `DispT *const *`, so the same call compiles from YAML action lambdas and from inside the `display:` lambda.

> **ESPHome lambda note.** `id(foo).bar` becomes `foo->bar`, but a bare `id(foo)` becomes the raw pointer `foo`. That's why both overloads exist. For the same reason, pass `id(sel)` without `&` to a generic `[](auto *sel, ...)` helper when working with `TemplateSelect`.

---

## Layout, `FrameHitTable`, `TouchGestureEngine` and shared widgets

All geometry and hit registration go through [`ui_core.h`](../include/ui_core.h), [`ui_layout.h`](../include/ui_layout.h), [`ui_hit_table.h`](../include/ui_hit_table.h), [`ui_gesture_engine.h`](../include/ui_gesture_engine.h), and [`ui_widgets.h`](../include/ui_widgets.h) (`components/latter_ui/`), so one GUI works across every screen size without separate YAML touch scripts. The layout anchors, touch constants and per-board rules are in the [Device UI design guide](device-ui-design.md#13-spacing-and-layout-anchors).

| Symbol | Meaning |
| :--- | :--- |
| `SCREEN_W`, `SCREEN_H`, `SCREEN_CX`, `SCREEN_CY`, `SCREEN_R`, `SCREEN_MIN_DIM` | Compile-time screen constants from `LATTER_SCREEN_W` and `LATTER_SCREEN_H` |
| `scale_x(v)`, `scale_y(v)`, `scale_s(v)` | Scale a value designed for the 466 px reference screen. They return the value unchanged on screens up to 520 px and scale it proportionally above that. |
| `scale_min(v)`, `scale_w(v)` | Always scale proportionally across all resolutions (both down below 466 px and up above 520 px) |
| `safe_span(y, inset)` | Bezel- and corner-safe horizontal span (`2 * circle_half_span(y, inset)`) on round and rectangular panels alike |
| `frame_hits()` (`FrameHitTable`) | Registers up to 48 `HitRegion` boxes (`add_rect`, `add_centered_rect`, `add_circle`) as controls draw; consumed directly by `touch_engine()` (`TouchGestureEngine`) and `render_active_focus_overlay()` |
| `ui::*` (`ui_widgets.h`, `ui_icons.h`) | Shared frosted-glass widgets: `ui::toggle_switch`, `ui::toggle_card_row`, `ui::pill_button`, `ui::stepper_row`, `ui::hsv_color_wheel`, `ui::confirm_modal`, `ui::game_over_sheet`, and `ui::pin_pad_modal` (3×4 Security PIN keypad) |
| `UiLayout` fields & hit-testers | `hdr_y`, `sub_hdr_y`, `home_bar_y`, `dock_y`, `app_dock_y`, `slot5_y()`, `hit_area_card3`, `area_slider_pct`, `hit_tool_card3`, `hit_tools_row/col`, `hit_home_*`, music `hit_*`, `music_scrub_frac`, and millimetre gesture thresholds |
| `circle_half_span(y)`, `chord_half_span_int(y)` | Horizontal half-width of a round screen (or rounded corner) at row `y`, from the chord table |

**Co-locate drawing and hit registration.** Register interactive widgets in `frame_hits()` as they draw (or use `UiLayout` anchors), never raw pixel coordinates in separate touch lambdas.

## Rendering on these chips

What makes the universal GUI fast enough on an ESP32-S3 (the full rules are in [Device UI design guide → Rendering pipeline](device-ui-design.md#5-rendering-pipeline)):
- **Draw row-major.** ESPHome's display buffer is row-major, so loop `y` on the outside and `x` on the inside. Draw with the `fast_*` primitives in `ui_core.h`, not ESPHome's `it.*` primitives, which cost a virtual call per pixel. Don't use `it.image()`; draw images with the row blitter (`blit_rgb565_scanline`) instead.
- **Dim without losing color.** Scale per channel in 1/256 with ordered Bayer dithering (`dim_rgb565_row_dither`) or exact per-channel shifts. Never use lossy packed bitmasks (`0xC718`, `0xE79C`), which throw away color depth.
- **Latch `2px`-aligned horizontal dirty bands.** At the top of the render lambda, `dirty_band().begin_frame(...)` (`DirtyBandTracker` in [`ui_compositor.h`](../include/ui_compositor.h)) aligns `[y0, y1]` to even-to-odd rows (`y0 & ~1` to `min(H − 1, y1 | 1)`) across full width `0 .. W − 1` and touches `(0, y0)` and `(W − 1, y1)`. Full transitions transfer `[0, H − 1]`, while localized updates cut SPI/QSPI bus traffic 5–12× without breaking `MipiSpiBuffer`'s contiguous `stride == W` zero-copy DMA fast path.
- **Use the lookup tables and PSRAM BSS.** `fast_sin` and `fast_cos` use a 60-entry LUT with interpolation. Round-screen clipping uses `circle_half_span` / `safe_span`. Keep `sinf` and `sqrtf` out of per-pixel loops, mark large inactive state structs with `LATTER_PSRAM_BSS`, and never put global namespace directives for `latter_ui` in headers.
- **Measure.** Turn on **System: Telemetry Profiler**. `[PROF:FRAME]` logs render time per page, and slow frames are flagged `[SLOW_FRAME]`. The frame budget is in [Device UI design guide → Performance](device-ui-design.md#8-performance).

## Capability flags

Branch on these, not on product names. Defaults are in [`ui_core.h`](../include/ui_core.h). Boards set them as build flags.

| Flag | Default | Meaning |
| :--- | :--- | :--- |
| `LATTER_SCREEN_W`, `LATTER_SCREEN_H` | 466 | Resolution |
| `LATTER_ROUND_DISPLAY` | 1 | Round bezel clipping |
| `LATTER_CORNER_R` | W/2 if round, 98 if wearable, otherwise 20 | Corner radius for bezel effects |
| `LATTER_PANEL_PPI` | 266 | Pixel density (diagonal pixels ÷ diagonal inches), used to convert millimetre touch thresholds. Every board must set it. |
| `LATTER_HAS_DISPLAY`, `_TOUCH`, `_AUDIO`, `_IMU`, `_BATTERY` | 1 | Hardware present |
| `LATTER_HAS_MIC`, `LATTER_HAS_SPEAKER` | = `HAS_AUDIO` | |
| `LATTER_IS_STATIONARY`, `LATTER_IS_WEARABLE` | 0 | Form factor |
| `LATTER_PANEL_IPS` | 1 if `LATTER_BACKLIGHT_PIN` is set | Backlit LCD (PWM brightness) rather than AMOLED |
| `LATTER_CHIP_P4`, `LATTER_PSRAM_MB` | Derived from the IDF target | |

With `LATTER_HAS_TOUCH=0`, the focus ring stays visible permanently, so a rotary-only device is usable.
