# Building a GUI

The universal GUI ([`guis/universal/gui.yaml`](../guis/universal/gui.yaml)) is one consumer of `latter_core` and `latter_ui`. You can replace it with your own interface—a custom visual style, a single-purpose kiosk, or a rotary-encoder device—while keeping Protocol v3 sync, full-duplex audio and Assist, power management, and the board HAL underneath.

For the universal GUI's visual, touch, glass, and performance rules, read the **[Device UI design guide](device-ui-design.md)**. This page covers the contract between a custom GUI and the core OS.

## What Stays and What You Replace

```yaml
packages:
  hal:                    !include boards/<your_board>.yaml             # board HAL & LATTER_* traits
  latter_system:          !include modules/system_core.yaml             # includes modules/base.yaml
  latter_system_ui_sync:  !include modules/system_ui_sync.yaml          # Protocol v3 UI sync & themes
  latter_power_policy:    !include modules/power_policy.yaml            # DisplayLifecycleController & radio policy
  latter_sleep_lifecycle: !include modules/sleep_lifecycle.yaml         # light-sleep & hibernate lifecycle
  latter_audio_pipeline:  !include modules/audio_pipeline.yaml          # 48 kHz audio mixer, Sendspin & sounds
  latter_voice_assistant: !include modules/voice_assistant.yaml         # micro_wake_word & Assist satellite
  latter_audio_scripts:   !include modules/audio_scripts.yaml           # alerts, chimes & audio routing scripts
  latter_display:         !include modules/display_pipeline.yaml        # online_image slots & backlight
  latter_timers:          !include modules/timers_and_fetchers.yaml     # local timers, alarms & RPC ticks
  my_gui:                 !include guis/my_gui/gui.yaml                 # your custom GUI
```

> **Headless vs. Custom Display Builds:** `display_pipeline.yaml` expects `id: disp1` and compiles `latter_ui`. For a truly screenless node (no display at all), include **`modules/base.yaml`** with `latter_core: { gui: "none" }` instead—see [`examples/minimal_headless_companion.yaml`](../examples/minimal_headless_companion.yaml).

A custom GUI package provides three things:
1. **A `display:` render lambda** extending `id: disp1` (`auto_clear_enabled: false`).
2. **Touch handling** extending `id: touch_screen` (omit on non-touch hardware and drive input via `ui_input_event`).
3. **Theme color IDs** (`theme_primary`, `theme_secondary`, `col_bg`, `col_gold`, `col_warm_gold`, `col_orange`, `col_red`, `col_green`), written by `apply_theme_preset` in `modules/system_ui_sync.yaml`.

---

## Minimal Custom GUI Skeleton

```yaml
# guis/my_gui/gui.yaml
font:
  - file: "gfonts://Roboto"
    id: font_body
    size: 24

color:
  - { id: theme_primary,   hex: "6B8E9E" }
  - { id: theme_secondary, hex: "C0C0C0" }
  - { id: col_gold,        hex: "C59B27" }
  - { id: col_warm_gold,   hex: "A88424" }
  - { id: col_orange,      hex: "B85C24" }
  - { id: col_red,         hex: "A84338" }
  - { id: col_green,       hex: "427453" }
  - { id: col_bg,          hex: "000000" }

display:
  - id: !extend disp1
    auto_clear_enabled: false
    lambda: |-
      const auto &rs = latter_ui::runtime_state();
      latter_ui::frame_hits().clear();
      latter_ui::UiFrameContext ctx;
      ctx.now = id(ha_time).now();
      if (!ctx.now.is_valid()) ctx.now = id(sntp_time).now();
      ctx.now_ms = esphome::millis();
      ctx.cx = latter_ui::SCREEN_CX;
      ctx.cy = latter_ui::SCREEN_CY;
      ctx.panel_is_on = id(display_brightness).current_values.is_on();
      ctx.camera_snapshot_art = latter_ui::make_art_view(id(camera_snapshot_art));
      ctx.va_visual_profile_opt = latter_ui::pref_va_visual_profile_cstr();
      ctx.screensaver_style_opt = latter_ui::pref_screensaver_style_cstr();
      ctx.font_time = id(font_body);
      ctx.font_chrono_time = id(font_body);
      ctx.font_date = id(font_body);
      ctx.font_btn_bold = id(font_body);
      ctx.font_small = id(font_body);
      ctx.font_micro = id(font_body);
      ctx.theme_primary = id(theme_primary);
      ctx.theme_secondary = id(theme_secondary);
      ctx.col_bg = id(col_bg);
      ctx.col_black = id(col_bg);
      ctx.col_white = id(theme_primary);
      ctx.col_slate = id(theme_secondary);
      ctx.col_gray = id(theme_secondary);
      ctx.col_dark_gray = id(col_bg);
      ctx.col_gold = id(col_gold);
      ctx.col_red = id(col_red);
      ctx.col_solaris_gold = id(col_gold);

      const auto &takeover_st = latter_ui::acquire_takeover_state(ctx);
      const uint32_t ov_sig = latter_ui::compute_takeover_signature(takeover_st);
      if (!latter_ui::dirty_band().begin_frame(rs.nav.gui_page, 0, ov_sig, false)) return;
      const int band_y0 = latter_ui::dirty_band().band_y0();
      const int band_y1 = latter_ui::dirty_band().band_y1();
      it.draw_pixel_at(0, band_y0, Color(8, 8, 8));
      it.draw_pixel_at(0, band_y0, id(col_bg));
      it.draw_pixel_at(latter_ui::SCREEN_W - 1, band_y1, Color(8, 8, 8));
      it.draw_pixel_at(latter_ui::SCREEN_W - 1, band_y1, id(col_bg));
      latter_ui::bind_frame_buffer(id(disp1), it);
      latter_ui::frame_base_begin();
      latter_ui::set_clip_y_band(band_y0, band_y1);

      if (latter_ui::render_system_takeovers(it, ctx, takeover_st)) {
        latter_ui::reset_clip_y_band();
        return;
      }
      latter_ui::Rgb565Surface surf = latter_ui::make_surface(it);
      latter_ui::frame_base(surf, id(col_bg));
      latter_ui::fast_print(surf, latter_ui::SCREEN_CX, latter_ui::SCREEN_CY, id(font_body),
                            id(theme_primary), TextAlign::CENTER,
                            latter_ui::area_ctx_state().area_name.c_str());
      latter_ui::reset_clip_y_band();

touchscreen:
  - id: !extend touch_screen
    on_release:
      - script.execute: { id: ui_input_event, action: "select", delta: 0 }
```

`latter_ui::render_system_takeovers(...)` ([`ui_compositor.h`](../include/ui_compositor.h)) gives any custom GUI all **8 system overlays** automatically: OTA progress, hibernate countdown, panel-off skip, ringing alarms/timers/Find My, Voice Assistant, the Security/Alarm PIN pad modal (`ui::pin_pad_modal`), push notifications with camera snapshots, and docked screensavers (see [`examples/custom_display_template.yaml`](../examples/custom_display_template.yaml)).

---

## What the Core Gives You

### 1. Runtime, Persisted & Protocol v3 State ([`include/state/`](../include/state/))

| Kind | Accessors |
| :--- | :--- |
| **Navigation & lifecycle** | `latter_ui::runtime_state().nav` (`gui_page`, `home_subpage`, `time_subpage`, `area_subpage`, `remote_subpage`, `tools_subpage`, `sky_weather_subpage`, `system_menu_mode`, `active_game`, `display_power_state` `0` active / `1` screensaver / `2` off, `is_hibernating`, `ota_in_progress`) |
| **Persisted preferences** | `latter_ui::persisted_prefs()` (`PersistedDevicePrefs` **v1** append-only POD: typed `PrefKey`s, `alarms[3]`, `custom_theme_rgb`, game high scores; synced 2-way via `device_prefs` & `set_device_prefs`) plus `latter_ui::hibernate_snapshot()` (cold-boot NVS hydration in `include/state/state_nvs_snapshot.h`) |
| **Protocol v3 domains (7)** | Reduced by `latter_ui::v3_domain_reducer()` (`state_v3_parsers.cpp`) into `area_ctx_state()`, `media_state()`, `remote_state()`, `weather_state()`, `calendar_state()`, `shortcuts_state()`, and `device_prefs_state()` / `persisted_prefs()` |
| **Paginated slots** | `latter_ui::runtime_state().slots` (`SlotBank`: `BROWSE`, `AREA`, `CALENDAR`, `REMOTE`), populated by `latter_set_slots` |
| **Voice, alerts & timers** | `latter_ui::runtime_state().voice`, `.alert`, `.alarms`, `.timers`, `.stopwatch` |
| **PSRAM images** | `album_art` (Sendspin), `remote_album_art`, `home_wallpaper_art`, `screensaver_photo_art`, `camera_snapshot_art`, wrapped into `ArtImageView` via `latter_ui::make_art_view(...)` (`ui_gfx.h`) |

Page and subpage enums (`PageId`, `HomeSubpage`, `TimeSubpage`, `ToolsSubpage`, `SystemMenuMode`, `GlassVariant`) live in [`ui_types.h`](../include/ui_types.h) and [`include/state/`](../include/state/).

### 2. Core Controllers, Scripts & Outbound RPC Helpers

| Entry Point | Purpose |
| :--- | :--- |
| `latter_ui::display_lifecycle().note_activity(esphome::millis())` | Record user interaction and reset screen/return-to-home idle timers (`include/hal_display_lifecycle.h`) |
| `wake_to_active_state` *(script)* | Wake the panel and backlight from screen-off or screensaver (`modules/sleep_lifecycle.yaml`) |
| `start_voice_assistant`, `toggle_voice_assistant`, `cancel_voice_assistant` *(scripts)* | Start, toggle, or synchronously abort a Voice Assistant session (`modules/voice_assistant.yaml` / `modules/audio_scripts.yaml`) |
| `latter_ui::media_play_slot(...)`, `media_transport_cmd(...)`, `media_adjust_volume(...)` | Compiled C++20 media playback and speaker actions (`include/audio_router.h`) |
| `latter_ui::timer_alarm_controller()` + `stop_alarm_sound` *(script)* | Manage local countdown timers, recurring alarms, snooze, and chime teardown |
| `latter_actions::emit_*` ([`latter_actions.h`](../include/latter_actions.h)) | Typed C++ wrappers around `latter_rpc::emit` for all 20 outbound RPC methods (`emit_music_browse`, `emit_area_action`, `emit_remote_action`, `emit_set_device_prefs`, etc.) |
| `ui_input_event(action, delta)` *(script)* | Canonical input router (`up`, `down`, `left`, `right`, `select`, `back`, `home` in `modules/system_ui_sync.yaml`) |

### 3. Coalesced Redraw Scheduling ([`ui_compositor.h`](../include/ui_compositor.h))

Never call `id(disp1).update()` directly—use the scheduler (overloaded for both `DispT *` and `DispT *const *`):

```cpp
latter_ui::schedule_disp_update(&id(disp1));                // coalesced full-frame redraw
latter_ui::schedule_band_disp_update(&id(disp1), y0, y1);   // coalesced 2px-aligned horizontal band [y0, y1]
latter_ui::schedule_anim_disp_update(&id(disp1));           // animation cadence: 18 ms on P4, 26 ms on S3
```

---

## Public UI Headers (`include/ui_*.h` & `components/latter_ui/`)

All UI geometry, pure-C++20 `Rgb565Surface` primitives, icons, hit-testing, touch gestures, compositing, and the **14 domain apps/views** live in host-compilable `include/ui_*.h` headers (with `ui_esphome_display.h` as the sole ESPHome display bridge):

| Header(s) | Role |
| :--- | :--- |
| [`ui_color.h`](../include/ui_color.h), [`ui_surface.h`](../include/ui_surface.h), [`ui_panel_traits.h`](../include/ui_panel_traits.h), [`ui_types.h`](../include/ui_types.h) | Pure C++20 RGB565 surface (`Rgb565Surface`, `UiFontView`, `ArtImageView`), screen traits (`SCREEN_W`, `SCREEN_H`, `LATTER_*`), `GlassVariant`, `kChrome*` tokens, LUT/3D/HSV math |
| [`ui_widget_ids.h`](../include/ui_widget_ids.h), [`ui_hit_table.h`](../include/ui_hit_table.h), [`ui_panel_metrics.h`](../include/ui_panel_metrics.h), [`ui_page_geoms.h`](../include/ui_page_geoms.h) | `FrameHitTable` (`frame_hits()`, up to `kMaxRegions = 56` co-located `HitRegion` entries in `hit_table.cpp`), `TOUCH_DEBOUNCE_*`, and physical millimetre panel metrics (`LATTER_PANEL_PPI`) |
| [`ui_gfx.h`](../include/ui_gfx.h) | Row-major RGB565 `fast_*` primitives, backdrop-invariant `GlassVariant::Clear` frosted-glass template (`37.5–45.3%`, plus opt-in `ReadableDark`), 1-bpp glyph cache, `ArtCanvasCache`, `frame_base()` (`ui_gfx.cpp`) |
| [`ui_mdi.h`](../include/ui_mdi.h) | **7,447 Material Design Icons** compiled into LZSS Bezier bytecode buckets (`mdi_icon_hash`, `render_mdi_icon`, 4× AA rasterizer + LRU cache in `ui_mdi.cpp`) |
| [`ui_layout.h`](../include/ui_layout.h), [`ui_touch_recognizer.h`](../include/ui_touch_recognizer.h) | `UiLayout` (`layout()`), `scale_*` / `safe_span`, `EdgeBounceState`, `can_go_home()` |
| [`ui_widgets.h`](../include/ui_widgets.h) | Interactive glass cards/pills (`ui::action_pill*`, `ui::list_row`, `ui::setting_cycle_row`), headers, toggles, steppers, `ui::hsv_color_wheel`, `ui::pin_pad_modal` (`ui_widgets.cpp`) |
| [`ui_frame_context.h`](../include/ui_frame_context.h), [`ui_dirty_band.h`](../include/ui_dirty_band.h), [`ui_cadence.h`](../include/ui_cadence.h), [`ui_compositor.h`](../include/ui_compositor.h) | `DirtyBandTracker` (`dirty_band()`), `FrameCadenceController`, `UiFrameContext`, `SystemTakeoverState` (`ui_compositor.cpp`, `overlay_takeover.cpp`) |
| [`ui_pages.h`](../include/ui_pages.h), [`ui_touch_engine.h`](../include/ui_touch_engine.h), [`ui_esphome_display.h`](../include/ui_esphome_display.h) | Entry points for all 14 domain renderers (`page_*.cpp`), `TouchRouter` / `TouchGestureEngine` (`touch_engine.cpp`, `touch_dispatch.cpp`), and the single ESPHome `Display` bridge (`make_surface`, `render_system_takeovers`) |

**Key Rendering Rules** (full spec in [Device UI design guide §5](device-ui-design.md#5-rendering-pipeline)):
- **Row-major `fast_*` only:** never call ESPHome's `it.*` drawing methods (`it.image()`, `it.rectangle()`, `it.print()`) outside `ui_gfx`, and never dim RGB565 with lossy bitmasks (`0xC718`, `0xE79C`).
- **Interactive-only 2-tier glass:** reserve `GlassVariant` for tappable controls (`Clear` fixed smoked template vs. `ReadableDark` over bright backdrops); passive tiles use flat `kChromeSurface`.
- **Co-locate drawing & hit regions:** register widgets into `frame_hits()` as they draw so touch hitboxes and rotary focus rings never drift.
- **Branch on `LATTER_*` traits** (`LATTER_SCREEN_W/H`, `LATTER_ROUND_DISPLAY`, `LATTER_PANEL_PPI`, `LATTER_SLOT_COUNT`, `LATTER_CARD_SLOT_COUNT`, `LATTER_HAS_TOUCH`, `LATTER_HAS_IMU`, `LATTER_HAS_BATTERY`, `LATTER_HAS_RTC`, `LATTER_IS_STATIONARY`, `LATTER_IS_WEARABLE`)—never on board names. With `LATTER_HAS_TOUCH=0`, focus rings stay permanently visible for rotary-only operation.
