# Building a GUI

The universal GUI ([`guis/universal/gui.yaml`](../guis/universal/gui.yaml)) is one consumer of the core. You can replace it with your own. This might be a different visual language, a single-purpose kiosk, or a non-touch device driven by a rotary encoder. You still get everything below it: the Latter protocol, audio and voice, power management, and the board HAL.

The visual, touch and rendering rules the universal GUI follows (tokens, components, navigation, gestures, the frame pipeline, per-board adaptation and performance) are in the [Device UI design guide](device-ui-design.md). It is the canonical reference; this page covers what a replacement GUI must provide and what the core gives it.

## What stays and what you replace

```yaml
packages:
  hal:              !include boards/<your_board>.yaml             # stays (or your own HAL)
  latter_core:      !include modules/system_core.yaml             # stays (includes modules/base.yaml)
  latter_power:     !include modules/power_manager.yaml           # stays
  latter_audio:     !include modules/audio_voice.yaml             # stays
  latter_display:   !include modules/display_pipeline.yaml        # stays
  latter_timers:    !include modules/timers_and_fetchers.yaml     # stays
  my_gui:           !include guis/my_gui/gui.yaml                 # yours
```

A GUI package needs three things:
1. **A `display:` render lambda** on the board's display, `id: disp1`, added with `!extend`.
2. **Touch handling** on the board's touchscreen, `id: touch_screen`, added with `!extend`. Leave this out on non-touch devices and drive input through `ui_input_event`.
3. **The color IDs the core writes.** The theme preset script (`apply_theme_preset` in `modules/system_core.yaml`) writes `theme_primary` and `theme_secondary` when the user picks a color profile. Define them, even if your GUI never draws with them.

The universal GUI's own top-level blocks are `substitutions`, `font`, `color`, `display`, and `touchscreen`. It defines no API actions and no Home Assistant subscriptions. Those all belong to the core, which is why a GUI can be replaced cleanly.

## Skeleton

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
      ctx.now_ms = millis();
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

      const auto takeover_st = latter_ui::SystemTakeoverState::from_context(ctx);
      const uint32_t ov_sig = latter_ui::compute_takeover_signature(takeover_st);
      latter_ui::dirty_band().begin_frame(rs.nav.gui_page, 0, ov_sig, false);
      const int y0 = latter_ui::dirty_band().y0();
      const int y1 = latter_ui::dirty_band().y1();
      it.draw_pixel_at(0, y0, Color(8, 8, 8));
      it.draw_pixel_at(0, y0, id(col_bg));
      it.draw_pixel_at(latter_ui::SCREEN_W - 1, y1, Color(8, 8, 8));
      it.draw_pixel_at(latter_ui::SCREEN_W - 1, y1, id(col_bg));
      latter_ui::bind_frame_buffer(id(disp1), it);
      latter_ui::frame_base_begin();
      if (latter_ui::render_system_takeovers(it, ctx, takeover_st)) {
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

`latter_ui::render_system_takeovers(...)` ([`ui_compositor.h`](../include/ui_compositor.h)) gives any custom GUI all 8 system overlays out of the box (OTA progress, hibernation countdown, panel-off skip, ringing alarms/timers/Find My, voice assistant, the 3-tier Security PIN pad modal `ui::pin_pad_modal`, push notifications/camera snapshots, and docked screensavers; see [`examples/custom_display_template.yaml`](../examples/custom_display_template.yaml)). `latter_ui::area_ctx_state().area_name` is the active Area name pushed via `latter_push_v3` (`domain: "area_context"`). All domain and device runtime state structs live in [`latter_state.h`](../include/latter_state.h). Headless nodes without a display set `latter_core: gui: "none"`, which skips compiling `latter_ui` altogether.

---

## What the core gives you

### State (`latter_state.h`)

| Kind | Accessors |
| :--- | :--- |
| Navigation & power | `latter_ui::runtime_state().nav` (`gui_page`, `home_subpage`, `area_subpage`, `remote_subpage`, `tools_subpage`, `sky_weather_subpage`, `system_menu_mode`, `active_game`, `display_power_state` 0 active / 1 screensaver / 2 off, `is_hibernating`, `ota_in_progress`) |
| Persisted preferences | `latter_ui::persisted_prefs()` (`PersistedDevicePrefs` v2 POD synced via Protocol v3 `device_prefs` & `set_device_prefs`: all 45 device preferences, `alarms[3]`, `custom_theme_rgb`, game high scores; save with `latter_ui::schedule_persisted_prefs_save(millis())` or `latter_ui::save_persisted_prefs()`) |
| Voice | `latter_ui::runtime_state().voice` (`active`, `status_text`, `tts_received`, `vad_active`) |
| Alerts & timers | `latter_ui::runtime_state().alert`, `.alarms`, `.timers`, `.stopwatch` |
| List data | `latter_ui::runtime_state().slots` (`SlotBank`: `BROWSE`, `AREA`, `CALENDAR`, `REMOTE`), filled by `set_browse_slots` and `set_area_slots` |
| Home Assistant data | `latter_ui::area_ctx_state()`, `latter_ui::media_state()`, `latter_ui::remote_state()`, `latter_ui::weather_state()` (including `sun_*` and `moon_phase`), and `latter_ui::calendar_state()` in [`latter_state.h`](../include/latter_state.h), populated by `latter_push_v3` in the [device protocol](device-protocol.md#3a-latter_push_v3-protocol-v3-domain-push) |
| Images | `album_art` (Sendspin), `remote_album_art`, `home_wallpaper_art`, `camera_snapshot_art`, wrapped into `ArtImageView` via `latter_ui::make_art_view(...)` (`ui_gfx.h`) |

Page IDs and sub-page enums are in [`ui_types.h`](../include/ui_types.h) as `PageId`, `ToolsSubpage`, and `AreaSubpage`. You can reuse them or ignore them.

### Scripts and typed RPC helpers

| Entry point | Purpose |
| :--- | :--- |
| `ui_input_event(action, delta)` | The input router (`modules/system_core.yaml`). `action` is one of `home`, `back`, `select`, `next`, `prev`. |
| `wake_to_active_state`, `screen_sleep_timer` | Wake the screen, and restart the idle timer after any interaction |
| `toggle_voice_assistant`, `cancel_voice_assistant` | Voice control |
| `latter_actions::emit_*` ([`latter_actions.h`](../include/latter_actions.h)) | Typed C++ RPC emitters for music browsing/playback, area actions, remote commands, alarms, calendar, intercom, dock apps, and custom RGB |
| `play_music_slot`, `remote_send_key`, `play_game_sfx` | High-level playback, remote, and audio effect scripts |

Typed C++ helpers in [`latter_actions.h`](../include/latter_actions.h) wrap `latter_rpc::emit` for all outbound Home Assistant actions. A GUI never constructs raw RPC JSON envelopes itself.

### Redraw scheduling

Don't call `id(disp1).update()` directly. Use the scheduler (`ui_compositor.h`) instead:

```cpp
latter_ui::schedule_disp_update(&id(disp1));                // coalesced full-frame redraw
latter_ui::schedule_band_disp_update(&id(disp1), y0, y1);   // coalesced 2px-aligned horizontal band [y0, y1]
latter_ui::schedule_anim_disp_update(&id(disp1));           // animation frame: 18 ms on P4, 26 ms on S3
```

All three functions accept either `DispT *` or `DispT *const *`, so the same call compiles from YAML action lambdas and from inside the `display:` lambda.

> **ESPHome lambda note.** `id(foo).bar` becomes `foo->bar`, but a bare `id(foo)` becomes the raw pointer `foo`. That's why both overloads exist. For the same reason, pass `id(sel)` without `&` to a generic `[](auto *sel, ...)` helper when working with `TemplateSelect`.

---

## Public UI headers (`include/ui_*.h` & `components/latter_ui/`)

All geometry, graphics primitives, hit registration, touch gestures, compositor scheduling, and page renderers live in 8 public headers backed by compiled `.cpp` translation units in `components/latter_ui/`:

| Header | Role |
| :--- | :--- |
| [`ui_types.h`](../include/ui_types.h) | Screen traits (`SCREEN_W`, `SCREEN_H`, `LATTER_*`), `PageId`/`ToolsSubpage`/`AreaSubpage` enums, `kChrome*` color tokens, LUT/3D/HSV math, `kMotionMs` (210 ms), `ease_out_cubic`, `TOUCH_DEBOUNCE_*` |
| [`ui_gfx.h`](../include/ui_gfx.h) | Row-major RGB565 `fast_*` primitives, WCAG 4.5:1 frosted glass, 1-bpp glyph cache, `ArtCanvasCache`, `frame_base()`, async GDMA/PPA clear, `FrameProfileScope` (`ui_gfx.cpp`) |
| [`ui_hit_table.h`](../include/ui_hit_table.h) | Pure C++20 `FrameHitTable` (`frame_hits()`, up to 48 `HitRegion` entries with focus slots) (`hit_table.cpp`) |
| [`ui_layout.h`](../include/ui_layout.h) | Single-source `UiLayout` (`layout()`), `scale_*` / `safe_span`, millimetre gesture thresholds (`LATTER_PANEL_PPI`), `DockSlideAnimator`, `can_go_home()` |
| [`ui_widgets.h`](../include/ui_widgets.h) | Shared frosted-glass widgets (`render_page_header`, `render_sub_header`, `render_modern_card`, `render_card_box`, `ui::toggle_switch`, `ui::stepper_row`, `ui::hsv_color_wheel`, `ui::pin_pad_modal`) and canonical vector icons (`ui_widgets.cpp`) |
| [`ui_compositor.h`](../include/ui_compositor.h) | `DirtyBandTracker` (`dirty_band()`), `UiFrameContext`, `SystemTakeoverState`, `DisplayUpdateScheduler`, `render_system_takeovers()`, `render_active_focus_overlay()` (`ui_compositor.cpp`) |
| [`ui_touch_engine.h`](../include/ui_touch_engine.h) | `TouchRouter` (`touch_router()`, `allow_tap()`) and `TouchGestureEngine` (`touch_engine()`) (`touch_engine.cpp`) |
| [`ui_pages.h`](../include/ui_pages.h) | Concrete `(esphome::display::Display &it, const UiFrameContext &ctx)` entry points for the 10 compiled `page_*.cpp` translation units |

**Co-locate drawing and hit registration.** Register interactive widgets in `frame_hits()` as they draw (or use `UiLayout` anchors), never raw pixel coordinates in separate touch lambdas.

## Rendering on these chips

What makes the universal GUI fast enough on an ESP32-S3 (the full rules are in [Device UI design guide → Rendering pipeline](device-ui-design.md#5-rendering-pipeline)):
- **Draw row-major.** ESPHome's display buffer is row-major, so loop `y` on the outside and `x` on the inside. Draw with the `fast_*` primitives in `ui_gfx.h`, not ESPHome's `it.*` primitives, which cost a virtual call per pixel. Don't use `it.image()`; draw images with the row blitter (`blit_rgb565_scanline`) instead.
- **Dim without losing color.** Scale per channel in 1/256 with ordered Bayer dithering (`dim_rgb565_row_dither`) or exact per-channel shifts. Never use lossy packed bitmasks (`0xC718`, `0xE79C`), which throw away color depth.
- **Latch `2px`-aligned horizontal dirty bands.** At the top of the render lambda, `dirty_band().begin_frame(...)` (`DirtyBandTracker` in [`ui_compositor.h`](../include/ui_compositor.h)) aligns `[y0, y1]` to even-to-odd rows (`y0 & ~1` to `min(H − 1, y1 | 1)`) across full width `0 .. W − 1` and touches `(0, y0)` and `(W − 1, y1)`. Full transitions transfer `[0, H − 1]`, while localized updates cut SPI/QSPI bus traffic 5–12× without breaking `MipiSpiBuffer`'s contiguous `stride == W` zero-copy DMA fast path.
- **Use the lookup tables and PSRAM BSS.** `fast_sin` and `fast_cos` use a 60-entry LUT with interpolation. Round-screen clipping uses `circle_half_span` / `safe_span`. Keep `sinf` and `sqrtf` out of per-pixel loops, mark large inactive state structs with `LATTER_PSRAM_BSS`, and never put global namespace directives for `latter_ui` in headers or docs.
- **Measure.** Turn on **System: Telemetry Profiler**. `[PROF:FRAME]` logs render time per page, and slow frames are flagged `[SLOW_FRAME]`. The frame budget is in [Device UI design guide → Performance](device-ui-design.md#8-performance).

## Capability flags

Branch on these, not on product names. Defaults are in [`ui_types.h`](../include/ui_types.h). Boards set them as build flags.

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
