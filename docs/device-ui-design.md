# Device UI design guide

This guide is the standard for the on-device GUI: the universal GUI shell in [`guis/universal/`](../guis/universal/), the compiled `latter_ui` component in [`components/latter_ui/`](../components/latter_ui/) (`ui_gfx.cpp`, `hit_table.cpp`, `ui_widgets.cpp`, `ui_compositor.cpp`, `touch_engine.cpp`, and the 10 `page_*.cpp` translation units), and the 8 C++ UI headers in [`include/`](../include/) (`ui_types.h`, `ui_gfx.h`, `ui_hit_table.h`, `ui_layout.h`, `ui_widgets.h`, `ui_compositor.h`, `ui_touch_engine.h`, and `ui_pages.h`). Read it before changing the device UI. [Building a GUI](building-a-gui.md) explains how to replace the GUI; [Porting hardware](porting-hardware.md) explains how to add a board. Both defer to this guide for visual, touch and rendering rules. Latter Studio, the admin panel in Home Assistant, has its own guide in the `ha-latter` repository (`docs/studio-design.md`).

Each rule is marked:

- **Enforced: `[TAG]`**: a latter-verify check fails the build when the rule is broken.
- **Documented only**: nothing checks it yet.

The [decision log](#10-decision-log) records the standing decisions behind this guide. Changing one needs a new entry and the project maintainer's approval.

---

## 0. Principles

1. **Traits, not names.** UI code branches on capability flags (`LATTER_ROUND_DISPLAY`, `LATTER_IS_WEARABLE`, `LATTER_HAS_IMU`, `LATTER_HAS_TOUCH`, `LATTER_PANEL_IPS`, screen size), never on a product or board name (D-001). **Enforced: `[TRAITS NOT NAMES]`**, **`[TERMINOLOGY]`**.
2. **One canonical path.** Every role (a header, a back affordance, a pager, a toggle, a modal, a hit-test, a motion, a debounce) has one implementation, in `ui_types.h`, `ui_gfx.h`, `ui_layout.h`, or `ui_widgets.h` (`components/latter_ui/`). A page that needs something new adds it there and reuses it.
3. **Geometry is single-source.** Interactive widgets register their exact rendered bounds into `FrameHitTable` (`ui_hit_table.h`) as they draw or share `UiLayout` anchors, and touch thresholds are millimetres (D-002, D-011, D-031, D-032).
4. **One reference grid.** Layout values are written for the 466 × 466 reference panel and adapted with `scale_x`/`scale_y`/`scale_s`/`scale_w`/`scale_min`, `safe_span(y)`, and three width buckets (D-024, D-031).
5. **Unknown renders as unknown.** A value the device hasn't received shows `—` (`kUnknownGlyph`), never a demo default. **Enforced: `[UNKNOWN GLYPH INVARIANT]`**.
6. **Fast, row-major and band-flushed.** Every pixel goes through the `fast_*` primitives in row-major order, every frame paints its background once, and localized updates flush `2px`-aligned full-width horizontal bands via `DirtyBandTracker` (D-004, D-005, D-031).

## 1. Tokens

### 1.1 Color palette

Palette colors are ESPHome `color:` IDs in [`gui.yaml`](../guis/universal/gui.yaml), passed into the page renderers as arguments. The palette is a muted "steel and silver" set that doesn't bloom on AMOLED. Contrast is against black (`col_bg`).

| ID | Hex | Role | Contrast |
| :--- | :--- | :--- | :--- |
| `theme_primary` | `6B8E9E` | Accent: page headers, active state, focus ring, scrollbar thumb, mode-bar capsule. User-themable (below). | 6.0:1 |
| `theme_secondary` | `C0C0C0` | Secondary accent: feedback in the sub-header, values, the stopwatch's silver | 11.5:1 |
| `col_bg` | `000000` | Frame base for every page | — |
| `col_black` | `000000` | Text on light surfaces | — |
| `col_white` | `FFFFFF` | Primary text and icons | 21:1 |
| `col_slate` | `9E9E9E` | Secondary text, empty states | 7.8:1 |
| `col_gray` | `616161` | Disabled text only | 3.4:1 |
| `col_dark_gray` | `212121` | Card fills, tracks, inactive surfaces | 1.3:1 |
| `col_gold` | `C59B27` | Highlights; the Offline pill | 8.1:1 |
| `col_warm_gold` | `A88424` | Sun and celestial art. IPS panels use `Color(255, 178, 28)` on Sky & Weather. | 6.0:1 |
| `col_orange` | `B85C24` | Urgent small text (a countdown under 10 s) | 4.6:1 |
| `col_red`, `col_green`, `col_cyan`, `col_purple`, `col_magenta` | `A84338`, `427453`, `456E8A`, `6D4B7D`, `8E4A7D` | Status and category colors for large text, fills and icons | 3.0–3.9:1 |
| `col_solaris_gold`, `col_accent_cyan` | `FFDC1E`, `1EC8E6` | Motion-only accents for the voice overlay and kinetic visuals (D-021) | 15.5:1, 10.5:1 |

- **Small text contrast.** Text in `font_small` or `font_micro` is at least 4.5:1 on its background; the colors below that are for large text (`font_date` and up), fills and icons. Disabled controls may use `col_gray`. **Enforced: `[TEXT CONTRAST]`** (D-019).
- **Theme presets.** `apply_theme_preset` in [`modules/system_core.yaml`](../modules/system_core.yaml) writes `theme_primary` from **Display: Color Profile**: Steel Blue (default, `6B8E9E`), Blue (`456E8A`), Green (`427453`), Pink (`A87884`), or Custom (a color-wheel pick through `apply_custom_rgb_harmony`). It resets `theme_secondary` to `C0C0C0`. These two are the only color IDs the core modules write.
- **Chrome colors** are named constants in `ui_types.h`, never literals (D-021, D-032): `kChromeShadow`, `kChromeHomePill`, `kChromeTrack`, `kChromeTrackBorder`, `kChromeTrackDim`, `kChromeTrackBorderDim`, `kChromeBarTrack`, `kChromeCardBorder`, `kChromePipInactive`, `kChromeBackLabel`, `kChromeGlassTint`, `kChromeGlassSheen`. The page art (sky, games, weather) still carries `Color(r, g, b)` literals; their count per file may only go down. **Enforced: `[UI COLOR TOKENS]`** (ratchet).

### 1.2 Typography

Every font is a Google font fetched at build time: Roboto, plus Orbitron 700 for game titles. Sizes are pixel sizes set by substitutions. [`gui.yaml`](../guis/universal/gui.yaml) holds the defaults, and a board file overrides them.

| ID | Weight | Glyphs | Default | Wrist, Mobile | Desk | Use |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `font_time` | 400 | digits, `:.-°%—` | 77 | 77 | 112 | Clock and big numbers |
| `font_chrono_time` | 400 | digits, `sAMP°%—` | 38 | 38 | 56 | Secondary clocks, countdowns |
| `font_date` | 400 | Latin-1 + Latin Extended-A subset | 27 | 27 | 38 | Titles, large labels |
| `font_btn_bold` | 700 | same as `font_date` | 26 | 26 | 36 | Buttons, keypad |
| `font_small` | 400 | same | 21 | 25 | 30 | Body text, headers, list rows |
| `font_micro` | 400 | same | 14 | 17 | 20 | Captions; `font_small`'s stand-in on screens narrower than 380 px (clock) |
| `font_games_title` | Orbitron 700 | caps, digits, `:.-•!` | 38 | 38 | 56 | Game titles |

- **Minimum sizes** (D-019): `font_small` at least 2.0 mm and `font_micro` at least 1.3 mm at the board's PPI (size in px × 25.4 ÷ PPI). That's why the Wrist and Mobile boards, the densest panels, set their own two sizes. **Enforced: `[FONT MM]`**.
- **Text from Home Assistant passes through the glyph helpers** before drawing: `sanitize_utf8_for_font`, `is_supported_font_cp` and `fit_text_to_width_font` in `ui_gfx.h`, and `wrap_text_lines` in `ui_widgets.h`. *Documented only.*
- **Headers** (D-025): `render_page_header()` draws an UPPERCASE title in `font_small` and `theme_primary` at `layout().hdr_y` (with an optional drop shadow over wallpaper). `render_sub_header()` draws the line below it at `layout().sub_hdr_y`, always `font_small`: action feedback in `theme_secondary`, otherwise a context line in `col_slate` or `col_white`. *Documented only.*

### 1.3 Spacing and layout anchors

There is no spacing scale. Spacing lives in `UiLayout` ([`ui_layout.h`](../include/ui_layout.h)), one struct of named anchors, reached through `latter_ui::layout()`. Values below are round (466 × 466) / rectangular.

| Anchor | Round | Rectangular | Notes |
| :--- | :--- | :--- | :--- |
| `hdr_y` | 48 | 36 wearable, 28 standard, 48 above 520 px | Page header |
| `sub_hdr_y` | 76 | 64 wearable, 56 standard, 88 above 520 px | Sub-header / feedback line |
| `back_y` | 426 | `H − scale_y(28)` | Back label (`kBackLabelY` in `ui_layout.h`) |
| `home_bar_y` | 434 | `H − scale_y(11)` | Home pill (`kHomeBarY`) |
| `dock_y` | 359 | `H − scale_y(66)` | Home dock icon centers |
| `app_dock_y` | 369 | `H − scale_y(66)` | In-app mode bar centers |
| `dock_pips_y`, `app_dock_pips_y` | 404, 408 | `H − scale_y(27)` | Scrollbar / pips row |
| `dock_r` | 27 | 24 below 360 px wide, 27 standard, 40 above 520 px | Dock icon radius |
| `list_card_w` | 276 | `W − 88` below 380 px, 276 standard, scaled above 520 px | 5-slot list card width |
| `slot5_y(i)`, `hit_slot5()` | 236 + (i − 2) × 63 | scaled above 520 px | 5-slot list rows (height 44) and their hit-test |
| `area_card3_*`, `tool_card3_*` | stride 76 / 100, height 64 / 74 | scaled above 520 px | 3-card pages |
| `tools_row_y`, `tools_col_off` | stride 105, offset 68 | 61 below 380 px | Tools launcher grid |
| `home_*_y` | date 52, clock 128, weather 200 | scaled above 520 px | Home screen zones |
| `home_activity_cy()`, `home_activity_h()` | centered between the weather line and the dock (one line, 68) | `dock_y − dock_r − home_activity_dock_gap()` (3 mm) − half the two-line card (2 × 46 + 12) | Home activity card (D-028) |
| `battery_cx/y`, `celestial_x/y` | — | per width bucket | Corner-safe indicators |

UiLayout also holds the page-specific geometry for music, light sliders, steppers, calculator, map zoom and system swatches, each with its hit-tester, and the touch constants in [1.6](#16-touch-constants). **Rule:** a spacing value used by more than one page, or by both drawing and touch, goes into `UiLayout`. *Documented only.*

### 1.4 Radii

| Radius | Value | Source |
| :--- | :--- | :--- |
| Panel corner (`LATTER_CORNER_R`) | `W/2` round, 98 wearable, 20 other rectangular | Derived in `ui_types.h` from `LATTER_ROUND_DISPLAY` and `LATTER_IS_WEARABLE`; a board can override it |
| List card | 12 | Page code |
| Empty-state card | 14 | `render_empty_state` |
| Card (`render_modern_card`) | clamped to `h/2`, minimum 2; scaled when ≤ 24 on panels above 520 px | `ui_widgets.h` |
| Dock icon | `dock_r` | `UiLayout` |
| Mode-bar track | `dock_r + scale_s(3)` | `render_in_app_mode_bar_track` |

### 1.5 Motion

One motion system (D-015): every UI transition is time-based and eases out cubic over **`kMotionMs` = 210 ms** (`ease_out_cubic`, `ui_types.h`).

| Motion | Implementation |
| :--- | :--- |
| Home dock page slide, mode-bar capsule glide | `DockSlideAnimator` (`ui_layout.h`): `home_dock_anim()` on page 1, `mode_bar_anim()` on pages 5, 6 and 9 |
| List and carousel page transitions (System, Music, Tools and Games launchers) | `start_menu_scroll()` starts the tween (`menu_scroll_start_ms`); renderers read `menu_scroll_shift(span)`; `gui.yaml` requests animation frames while `menu_scroll_active` |

Frame pacing:

| Cadence | Value | Source |
| :--- | :--- | :--- |
| Animation frame (`schedule_anim_disp_update`) | 18 ms on the ESP32-P4, 26 ms on the ESP32-S3 | `DisplayUpdateScheduler::request_anim_ptr` (`ui_compositor.h`) |
| Tools and games physics loop | 25 ms tick, at most one frame per 44 ms | `ui_anim_tick` (`timers_and_fetchers.yaml`), `min_interactive_frame_gap_ms` |
| Ambient visuals (screensavers, weather, voice) | 25 ms tick; one step per 46 ms idle (or passive wake-word), 75 ms while speaker audio plays, 120 ms during alerts | `should_step_ambient_anim` (`ui_types.h`) |

### 1.6 Touch constants

Every gesture threshold in `TouchGestureEngine` ([`ui_touch_engine.h`](../include/ui_touch_engine.h) / [`components/latter_ui/touch_engine.cpp`](../components/latter_ui/touch_engine.cpp)) comes from a millimetre constant in `UiLayout`, converted with `mm_to_px(mm) = mm × LATTER_PANEL_PPI / 25.4` (D-011, D-031, D-032). Each value is the pixel distance the 466 px reference panel (266 ppi) used, so that panel behaves exactly as before and every other panel gets the same physical distance. **Enforced: `[TOUCH MM]`**.

| `layout().` | mm | Use | Pocket (266) | Mobile (313) | Wrist (315) | Remote (165) | Desk (254) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `drag_start()` | 0.6 | A card slider drag starts | 6 px | 7 | 7 | 4 | 6 |
| `flick_min()` | 1.7 | Music track-inspector flick | 18 | 21 | 21 | 11 | 17 |
| `dock_release_swipe()` | 1.9 | Home dock page swipe on release | 20 | 23 | 24 | 12 | 19 |
| `swipe_short()` | 2.1 | Dock swipe while moving, music lists | 22 | 26 | 26 | 14 | 21 |
| `swipe_min()` | 2.3 | Swipe-up home, pill-tap travel, mode bar, 2048 | 24 | 28 | 29 | 15 | 23 |
| `list_swipe()` | 2.5 | Vertical list and carousel paging | 26 | 31 | 31 | 16 | 25 |
| `page_swipe()` | 2.7 | In-app mode bars, map pan, cameras | 28 | 33 | 33 | 18 | 27 |
| `tap_max()`, `wide_swipe()` | 2.9 | A touch that moved less is a tap; Sky & Weather swipe | 30 | 36 | 36 | 19 | 29 |
| `canvas_swipe()` | 3.25 | Area Control canvas category swipe | 34 | 40 | 40 | 21 | 33 |
| `tap_max_loose()` | 3.45 | Music browser taps | 36 | 43 | 43 | 22 | 35 |
| `fast_pan()` | 10.5 | A map drag pans two tiles | 110 | 129 | 130 | 68 | 105 |
| `edge_guard_y()` | 1.5 | Band below in-app content (from the bottom) | 16 | 18 | 19 | 10 | 15 |
| `home_zone_y()` | 5.2 | Swipe-up home starts here (from the bottom; round: y 412) | 412 | 64 | 64 | 34 | 52 |
| `chin_guard_y()` | 2.7 | The home pill's chin (from the bottom; round: y 412) | 412 | 33 | 33 | 18 | 27 |
| `is_home_pill_tap()` | 2.8, half-width max(`scale_x(64)`, 6.5 mm) | Pill tap band | y 422 | 35 | 35 | 18 | 28 |
| `mode_bar_zone_top()` | `app_dock_y − dock_r − scale_s(14)` | Area Control: sliders above, mode bar below | | | | | |
| `dock_hit_r()` | max(`dock_r` + 16, 3.6 mm) | Home dock icon hit radius (≥ 7 mm target) | 43 | 44 | 45 | 40 | 56 |

**Debounce** (`ui_types.h` / `ui_touch_engine.h`, D-023). Taps debounce through `touch_router().allow_tap(now, TOUCH_DEBOUNCE_*)`, one shared timestamp. **Enforced: `[TOUCH DEBOUNCE]`**.

| Constant | ms | Use |
| :--- | :--- | :--- |
| `TOUCH_DEBOUNCE_RAPID` | 100 | Rapid steppers |
| `TOUCH_DEBOUNCE_BUTTON` | 150 | Buttons and pills (the default) |
| `TOUCH_DEBOUNCE_WAKE` | 180 | Settling after wake |
| `TOUCH_DEBOUNCE_DISMISS` | 200 | Dismissing an overlay |
| `TOUCH_DEBOUNCE_SCROLL` | 250 | Page transitions and chevrons |
| `TOUCH_GUARD_DISMISS` | 250 | Minimum delay after wake |
| `TOUCH_POST_SWIPE_LOCKOUT` | 200 | Ignore moves right after a committed swipe |

## 2. Components

Each component has one implementation. Use it; don't hand-draw the same role.

**Primitives** (`ui_gfx.h`): `fast_pixel`, `fast_hline`, `fast_vline`, `fill_rect_row_major`, `fast_rect_outline`, `fast_filled_circle`, `fast_circle`, `fast_line`, `fast_filled_triangle`, `fast_print`, `draw_glow_line`, `blit_rgb565_scanline`. Anti-aliased shapes and dithered gradients (D-027): `fill_round_rect_aa` (rounded rectangle, stadium or circle, flat or vertical gradient), `stroke_round_rect_aa` (its border ring), `fast_filled_circle_aa`, `fast_circle_aa` (ring), `fast_hline_dither`, `fast_pixel_blend`. Universal glass (D-029, D-030): `fill_round_rect_glass` blends a tint over the framebuffer by alpha and coverage, ordered-dithered, calibrated by `resolve_glass_fill` across all backdrops (including solid `col_bg`). Every rounded chrome shape (cards, pill buttons, glass circles, Home dock orbs, mode bar, scrollbar, pips, home pill, focus rings, Offline pill) draws through them. **Enforced: `[FAST PRIMITIVES]`** in `include/ui_*.h`, `components/latter_ui/*.cpp`, and `guis/**/*.yaml` ([section 5](#5-rendering-pipeline)).

| Component | API | Geometry from | Use for |
| :--- | :--- | :--- | :--- |
| Page header | `render_page_header(it, cx, title, font, color, shadow = false)` | `UiLayout::hdr_y` | The title of every page. A `std::string` title is fitted to the header width. |
| Sub-header | `render_sub_header(it, cx, text, font, color)` | `UiLayout::sub_hdr_y` | Action feedback (`theme_secondary`) or a context line (`col_slate`/`col_white`) |
| Empty state | `render_empty_state(it, cx, cy, text, font, text_c)` | Caller's row | One sentence on a quiet glass card that says what's missing or loading ("No upcoming events (Tap +)", "Loading camera snapshot...") |
| Card | `render_modern_card(it, cx, cy, w, h, r, bg, border, label, text_c, font)` (centered, with label) and `render_card_box(it, x0, y0, w, h, r, fill, border)` (top-left box) | Caller, from `UiLayout` | Every list row, tile, keypad key and panel. Always renders frosted glass (`fill_round_rect_glass` + `resolve_glass_fill`, D-030) whether a wallpaper, Weather Sky, album art or solid `col_bg` is behind it. `render_modern_card` resizes: above 520 px it scales `w ≤ 340`, `h ≤ 96`, `r ≤ 24`; below 380 px it forces `w = W − 88` for short wide cards; otherwise it caps `w` at `W − 20`. |
| Pill button | `render_pill_button(it, x1, x2, y_mid, r, border, bg, label, text_c, font)` | Caller | A standalone frosted-glass action button (Games launcher) |
| Glass circle | `render_glass_circle(...)` | Caller | Every round action, transport, zoom, remote and timer/chronograph control (returns blended interior `Color` for icon cutouts) |
| Glass card | `render_glass_card(it, x0, y0, w, h, r, tint, alpha_q8)` with `sample_glass_backdrop()`, `glass_alpha_for_text()`, `glass_alpha_smooth()`, `resolve_glass_fill()` | Caller, from `UiLayout` | Translucent frosted card over any backdrop (Home activity card, mode bar, floating back pills). Tint at least 60 % opaque, raised until text clears 5:1 over bright backdrops and frost-lifted over dark/black backdrops (D-029, D-030) |
| Back label | `render_back_button(it, cx, font, layout().back_y, label, text = kChromeBackLabel)` | `UiLayout::back_y` | Returning from a sub-page ([3.3](#33-return-paths)). `"Home"` draws the home pill. **Enforced: `[BACK BUTTON Y]`**. |
| Home pill | `render_home_indicator_bar(it, cx, layout().home_bar_y)` | `UiLayout::home_bar_y` | Top-level pages; drawn by `gui.yaml` after the page |
| Home dock | `get_home_dock_window_xs` + the Home renderer; `home_dock_anim()` | `UiLayout::dock_y`, `dock_r` | Page 1 only: floating frosted-glass circular app icons, 4 per window |
| In-app mode bar | `render_in_app_mode_bar_track(...)`; `mode_bar_anim()` | `UiLayout::app_dock_y`, `dock_r` | Switching the middle view of a multi-mode app (pages 5, 6, 9). A frosted-glass stadium track with a gliding glass capsule ring; no separators between slots. |
| Viewport scrollbar | `render_viewport_scrollbar(it, cx, y, total, start_idx, thumb, dim)` | `dock_pips_y` / `app_dock_pips_y` | Under the Home dock and a mode bar when there are more than 4 items (D-016) |
| Page pips | `render_dock_page_pips(it, cx, y, pages, active, c_active, c_inactive)` | Caller | Paged carousels: Games launcher, camera carousel (D-016) |
| Vertical chevrons | `render_vertical_page_pips(it, show_up, show_down, bg, active, inactive)` | Right-edge column per width bucket | Paged vertical lists: 5-slot lists and the Tools launcher (D-016) |
| Focus ring | `render_focus_ring_circle`, `render_focus_ring_rect`, `render_active_focus_overlay` | Registered `kHitFocusSlot` / `kHitFocusCircle` regions in `frame_hits()` (`FrameHitTable`), read via `focus_at()` / `focus_count()` | Rotary / button navigation |
| Alert border | `render_bezel_alert_border(it, cx, cy, color)` | `SCREEN_R` (round: 4 rings at `R − 10 … R − 7`); `LATTER_CORNER_R` with padding 5 wearable / 3 other and thickness 5 | A ringing timer or alarm on Tools pages (D-007) |
| Battery indicator | `render_battery_indicator` (`page_clock_home.cpp`) | `UiLayout::battery_cx/y` | Clock and Home |
| Time readout | `render_time_readout(it, cx, text, font_time, color)` (`ui_widgets.h`) | `scale_y(142)` | The big readout on the timer, alarm editor and stopwatch pages (one component, one font) |
| Home activity card | Home renderer + `resolve_home_activity_view()` | `UiLayout::home_activity_*` | Live items (timers, stopwatch, music, alarms, calendar), filtered by the Device's Activity Card setting. One line on the round panel, two elsewhere; glass over the backdrop; rotates every 8 s, sliding upward, only when there are more items than lines (D-028, D-029) |
| Offline pill | `render_offline_pill(it, cx, now, font_micro, col_gold)` | Top edge (a tab on rectangular panels; floating inside the bezel on round), sized from the text and mm padding | Home Assistant disconnected for 8 s or more (D-026) |
| Tool launcher card | `render_tool_launcher_card` (`page_tools.cpp`) | `UiLayout::tools_*` | The Tools grid |
| Toggle switch & row | `ui::toggle_switch`, `ui::toggle_card_row` (`ui_widgets.h`) | `UiLayout` + `FrameHitTable` | Boolean switches across Settings, Music, Area Control and Timers/Alarms (D-031) |
| Shared pill & stepper | `ui::pill_button`, `ui::stepper_row` (`ui_widgets.h`) | `UiLayout` + `FrameHitTable` (`68px` stride) | Frosted-glass pill actions and 4-column steppers with co-located hit regions (D-031, D-032) |
| HSV color wheel | `ui::hsv_color_wheel` (`ui_widgets.h`) | `UiLayout` + `FrameHitTable` | Single LUT-backed custom color picker ring and Save action (D-031) |
| Modals & PIN pad | `ui::confirm_modal`, `ui::game_over_sheet`, `ui::pin_pad_modal` (`ui_widgets.h`) | `UiLayout` + `FrameHitTable` | Destructive confirmation sheets, game-over summaries, and the 3×4 Security PIN keypad modal (`PinPadModalState`) for 3-tier perimeter security actions (D-031) |

**Icons** are monochrome solid-filled silhouettes with negative-space cutouts (D-008), with canonical vector glyphs (thermometer, eighth-notes, speaker, timer, alarm bell, lock, shield) centralized in `ui_widgets.h` / `ui_widgets.cpp` (D-031, D-032). *Documented only.*

**States.** Unknown values use `kUnknownGlyph` (`—`). Empty and loading cards use `render_empty_state`; status lines inside a page (for example "No updates available" under Update) stay plain sub-text.

**Adding shared controls.** Interactive multi-page controls live in [`ui_widgets.h`](../include/ui_widgets.h) / [`components/latter_ui/ui_widgets.cpp`](../components/latter_ui/ui_widgets.cpp) and register their hit bounds into `frame_hits()` as they draw. Add any new multi-page widget there before introducing a page-local copy.

## 3. Navigation

### 3.1 Page map

`runtime_state().nav.gui_page` selects the page; `PageId` in `ui_types.h` names pages 1–9. Each page reads its sub-state from `runtime_state()`.

| Page | `PageId` | User-facing name | Sub-state |
| :--- | :--- | :--- | :--- |
| 0 | — | Clock | — (face from **Display: Clock Face**: Minimal Digital, Modern Roman (Analog), Modern Digital (Bold)) |
| 1 | `HOME` | Home | `home_subpage`: 0 dock, 1 Home settings |
| 2 | `MUSIC` | Music | `music_overlay_mode`: `MusicOverlayMode` 0–6 (now playing, speakers, library categories, item browser, speaker actions, music hub, track inspector) |
| 3 | `GAMES` | Games | `active_game`: 0 launcher, 1 Marbles, 2 Archery, 3 Bricks, 4 Chomp, 5 2048, 6 Memory Match |
| 4 | `SYSTEM` | Settings | `system_menu_mode`: 0 dashboard, 1–12 menus |
| 5 | `WEATHER` | Sky & Weather | `sky_weather_subpage`: 0–3, switched by the mode bar |
| 6 | `AREA` | Area Control | `area_subpage`: 0 entity cards, 1 Area picker; categories switched by the mode bar |
| 7 | `REMOTE` | Remote | `remote_subpage`: 0 remote, 1 control list (master power and up to 10 devices) |
| 8 | `TOOLS` | Tools | `tools_subpage`: `ToolsSubpage` (0 launcher, 1 timers, 2 alarms, 3 stopwatch, 4 dice, 5 flashlight, 6 calculator, 7 level, 8 coin flip, 9 calendar, 10 intercom, 12 cameras) |
| 9 | `WHOLE_HOME` | Whole-Home Hub | `whole_home_tab()`: 0 alarm, 1 thermostat, 2 garage & locks, 3 vacuum, switched by the mode bar; drawn by `render_whole_home_page` (D-013) |

### 3.2 Home dock vs in-app mode bar

- **Home dock (Page 1):** floating circular app icons, four per window, paged by a horizontal swipe in the dock zone, with the viewport scrollbar when there are more than four apps. The list comes from Latter's dock setting (`collect_home_dock_apps`, up to 8); Remote appears only when a remote is configured, and Settings is always last.
- **In-app mode bar (pages 5, 6, 9):** a connected stadium-pill track at `app_dock_y`. Tap a slot or swipe sideways in the lower zone; the capsule glides. More than four modes adds conveyor chevrons and the scrollbar.
- Both use the 210 ms motion and the same slot geometry (D-003).

### 3.3 Return paths

| From | Return | How |
| :--- | :--- | :--- |
| A top-level page | Home (Page 1) | Swipe up from the home zone, or tap the home pill |
| A sub-page | Its parent | "< Back" at `back_y` |
| A game | The Games launcher | "Exit" |
| An edit | The page, without saving | "< Cancel" |
| A tool opened directly from Home | Home | The home pill (timers, alarms, stopwatch, cameras) |

- **One go-home predicate** (D-022): `latter_ui::can_go_home()` (`ui_layout.h`) decides whether a page is top-level. `gui.yaml` draws the home pill when it holds and Music isn't in its art-only view (`music_art_only()`); the down, move and release touch routers accept the swipe whenever it holds. **Enforced: `[HOME PREDICATE]`**.
- **Back vocabulary** (D-017): "< Back", "Exit", "< Cancel", and "Home" (the pill). **Enforced: `[BACK BUTTON Y]`**.

### 3.4 Takeover priority

`latter_ui::render_system_takeovers(...)` ([`ui_compositor.h`](../include/ui_compositor.h)) owns all global takeovers in strict priority order before `gui.yaml` renders `gui_page` (D-031):

| Priority | Takeover | Condition |
| :--- | :--- | :--- |
| 1 | Firmware update | `ota_in_progress` |
| 2 | Hibernation countdown / shutdown | `is_hibernating` |
| 3 | Panel off (draws nothing, `frame_base_skip()`) | brightness off or `display_power_state == 2` |
| 4 | **Alarm / timer ringing, Find My** | `alarm_ringing`, `timer_ringing`, `find_my_active` |
| 5 | Voice assistant | `va_active` or a status text |
| 6 | Security PIN pad modal | `ui::pin_pad_state().open` |
| 7 | Notification / camera snapshot | `notif_active` |
| 8 | Docked screensaver | `display_power_state == 1` |
| 9 | The page | `gui_page` |

After the page, `gui.yaml` draws the chrome in order: the focus overlay (not during takeovers), the home pill, and the Offline pill. Then, while a dock, mode-bar or list transition runs, it schedules the next animation frame. The alarm/timer takeover ranks above the voice assistant (D-006), and custom GUIs invoke the exact same `render_system_takeovers(...)` hook. *Documented only* (order is enforced inside `ui_compositor.h`).

## 4. Gestures and hit-testing

- **Co-located hit registration and `UiLayout` geometry.** Interactive widgets register their exact rendered pixel bounds into `frame_hits()` (`FrameHitTable` in [`ui_hit_table.h`](../include/ui_hit_table.h), up to `kMaxRegions = 48` compact POD `HitRegion` entries tagged with `WidgetId` and `kHitTap` / `kHitHorizSlider` / `kHitFocusSlot` / `kHitFocusCircle`) as they draw, paired with `UiLayout` slot and zone helpers (`hit_slot5`, `hit_area_card3`, `area_slider_pct`, `hit_tool_card3`, `hit_tools_row/col`, `hit_home_*`, the music `hit_*` and `music_scrub_frac`, `hit_map_zoom_*`, `hit_sys_color_swatch`, `is_home_pill_tap`, `dock_hit_r`) (D-002, D-031, D-032).
- **Thresholds in millimetres** ([1.6](#16-touch-constants)). **Enforced: `[TOUCH MM]`**, and **`[TOUCH PPI INVARIANT]`** requires `-DLATTER_PANEL_PPI` in every board file and example board.
- **Home affordances.** Swipe up starting at or below `home_zone_y()`, at least `swipe_min()` and mostly vertical, or tap the pill (travel under `swipe_min()`). While the pill shows, the chin below `chin_guard_y()` belongs to it.
- **Slider vs swipe zones (Page 6).** Above `mode_bar_zone_top()`, a drag that starts on a light, cover or fan card right of its divider scrubs that card's slider (after `drag_start()`) and never changes category. Below it, a horizontal swipe of `swipe_min()` steps the category; a swipe on the cards that isn't a slider drag needs `canvas_swipe()`.
- **Shared zones** (`ui_layout.h`): `is_back_button` (bottom band), `is_right_chevron_zone` (right edge column).
- **Touch flow.** `TouchRouter` (`ui_touch_engine.h`) forwards the first touch point to the compiled `TouchGestureEngine` (`touch_engine()` in [`ui_touch_engine.h`](../include/ui_touch_engine.h) / [`components/latter_ui/touch_engine.cpp`](../components/latter_ui/touch_engine.cpp), bound via `UiTouchRuntime`), which dispatches registered `FrameHitTable` regions and classifies taps, drags and swipes using `UiLayout` millimetre thresholds (D-031, D-032). A gesture is cancelled if the page context or active takeover changes between press and release.
- **Non-touch input.** `touch_engine().on_input_event(...)` / `ui_input_event(action, delta)` takes `home`, `back`, `select`, `next`, `prev`, stepping and activating registered `FrameHitTable` focus regions (`focus_at()` / `focus_count()`). The focus ring shows for 8 s after the last input, and always on `LATTER_HAS_TOUCH=0` boards.
- **Games** use IMU tilt on `LATTER_HAS_IMU=1` boards (touch-drag as a fallback) and touch steering on `LATTER_HAS_IMU=0` boards (D-009).

## 5. Rendering pipeline

### 5.1 Frame lifecycle (the `display:` lambda in `gui.yaml`)

1. `FrameProfileScope` starts: waits for the between-frame DMA clear and timestamps the frame; `frame_hits().clear()` resets the per-frame `FrameHitTable`; `dirty_band().begin_frame(...)` (`DirtyBandTracker` in [`ui_compositor.h`](../include/ui_compositor.h)) resolves the active `2px`-aligned horizontal band `[y0, y1]` (`[0, H − 1]` on page, subpage or takeover changes and active animations; localized even-to-odd row span on partial updates).
2. **Watermark latch** on every panel (D-012, D-031): draw `(0, y0)` and `(W − 1, y1)` with `Color(8, 8, 8)`, then `col_bg`, latching full width `x = 0 .. W − 1` across the `2px`-aligned dirty band (`y0 & ~1` to `min(H − 1, y1 | 1)`). This preserves `MipiSpiBuffer`'s contiguous `stride == SCREEN_W` zero-copy DMA fast path while cutting pixel bus traffic 5–12× on localized updates. These four `it.draw_pixel_at` calls are the only raw primitive calls the GUI makes.
3. `bind_frame_buffer(disp1, it)` binds the software framebuffer for direct scanline writes; `frame_base_begin()` resets the base state.
4. `latter_ui::render_system_takeovers(...)` in priority order ([3.4](#34-takeover-priority)), each starting with `frame_base(it, col_bg)` (or `frame_base_skip()` when the panel is off).
5. The page. Pages other than Home and Music get `frame_base(it, col_bg)` (or `render_page_wallpaper_base` on System Settings, Sky & Weather, Area Control, Remote, Tools, and Whole-Home Hub) from `gui.yaml`; Home paints its own base (the daytime sky or wallpaper), and Music claims the base with `frame_base_claim()` on full-screen art or falls back to `render_page_wallpaper_base` (configured wallpaper or solid `col_bg`) when **Music: Album Art Style** is `None`.
6. Chrome: focus overlay (`render_active_focus_overlay`), home pill, Offline pill.
7. `FrameProfileScope` ends: logs a frame that never painted a base, records timing, and starts the next DMA clear.

### 5.2 Rules

| Rule | Status |
| :--- | :--- |
| `auto_clear_enabled: false`; every frame paints its background exactly once through `frame_base()`, or marks itself with `frame_base_claim()` (full-screen art) or `frame_base_skip()` (panel off) | **Enforced: `[FRAME BASE]`**, and at runtime by `FrameProfileScope`'s warning |
| Draw only with the `fast_*` primitives, never ESPHome's `it.*` primitives (each costs a virtual `draw_pixel_at` per pixel) | **Enforced: `[FAST PRIMITIVES]`** (fallback and probe sites in `ui_gfx.cpp`, the 4 latch calls in `gui.yaml`) |
| Row-major: loop `y` outside and `x` inside | *Documented only* |
| No column-major `it.image()`, no `it.draw_pixels_at()`; images go through `blit_rgb565_scanline` | **Enforced: `[UI INVARIANT]`** |
| Full 16-bit RGB565: no lossy bitmasks (`0xC718`, `0xE79C`), no skipped scanlines | **Enforced: `[UI INVARIANT]`** (masks); skipped scanlines *documented only* |
| Smooth dimming and gradients (D-027): art scrims scale per channel in 1/256 (`compute_art_row_scale_q8`, ramped between levels) through `dim_rgb565_row_dither`; gradient fills interpolate at 8.8 fixed point; both quantize to RGB565 with a 4×4 ordered dither keyed to the screen position. Curved edges blend by coverage against the framebuffer; straight edges stay on pixel boundaries. The dither alternates adjacent RGB565 levels (one step), never lit/black, so the IPS rule below still holds. | *Documented only* |
| IPS panels (`LATTER_PANEL_IPS=1`): no 1-px alternating lit/black row or column patterns | *Documented only* |
| Round and corner clipping uses `circle_half_span(y)`, `safe_span(y)` and `chord_half_span_int`; on rectangular panels never pass `x` into `circle_half_span` to get vertical bounds | *Documented only* |
| No `sinf`, `cosf` or `sqrtf` in per-pixel loops: `fast_sin`/`fast_cos`/`fast_sincos` and the chord table | *Documented only* |
| No global namespace directives for `latter_ui` in headers or docs; game and tool state structs reside in PSRAM BSS (`LATTER_PSRAM_BSS` / `EXT_RAM_BSS_ATTR`) | **Enforced: `[CORE DOCS]`**, `verify_core.py` |
| Redraws go through `schedule_disp_update`, `schedule_band_disp_update` or `schedule_anim_disp_update`, never `disp1.update()`. All accept `DispT *` and `DispT *const *`. | *Documented only* |

### 5.3 Redraw scheduling

- `schedule_disp_update`: marks the full panel dirty (`mark_dirty_full()`) and arms a 0 ms one-shot under one scheduler ID. Requests made in the same loop pass collapse into one redraw.
- `schedule_band_disp_update(disp, y0, y1)`: marks a `2px`-aligned horizontal band `[y0, y1]` on `dirty_band()` (`DirtyBandTracker`) and arms the same coalesced 0 ms one-shot; multiple band requests in one loop pass merge into `[min(y0), max(y1)]`.
- `schedule_anim_disp_update`: the next frame 18 ms (P4) or 26 ms (S3) after the last frame started, under the same ID.
- Interval-driven animation (Tools, games) keeps at least 42 ms between frames; ambient visuals use `should_step_ambient_anim`.
- After each frame, hardware clears the framebuffer to that frame's base color while the CPU is free (the PPA solid fill on the P4, a GDMA copy on the S3). The next frame skips its CPU fill when its base color matches. Any failure falls back to the CPU fill.

## 6. Per-board adaptation

| | Pocket | Mobile | Wrist | Remote | Desk |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Board file | `waveshare_s3_175c` | `waveshare_s3_216` | `waveshare_s3_206` | `waveshare_p4_35` | `waveshare_p4_86_panel` |
| Resolution | 466 × 466 | 480 × 480 | 410 × 502 | 320 × 480 | 720 × 720 |
| Shape | Round | Square | Rounded rectangle | Rectangle | Square |
| Panel | AMOLED | AMOLED | AMOLED | IPS | IPS |
| `LATTER_PANEL_PPI` | 266 | 313 | 315 | 165 | 254 |
| `LATTER_CORNER_R` | 233 | 20 | 98 (wearable) | 20 | 20 |
| Width bucket | 380–520 | 380–520 | 380–520 | < 380 | > 520 |
| `scale_*` | identity | identity | identity | identity | × 720/466 |
| `font_small` / `font_micro` | 21 / 14 | 25 / 17 | 25 / 17 | 21 / 14 | 30 / 20 |
| IMU / battery / wearable | yes / yes / no | yes / yes / no | yes / yes / yes | no / yes / no | no / no / no |

**How a value adapts** (D-024, D-031):

1. `scale_x`/`scale_y`/`scale_s` return the 466-grid value unchanged on panels up to 520 px and scale it proportionally above 520 px. `scale_min` and `scale_w` always scale proportionally across all panel sizes (both down below 466 px and up above 520 px), and `safe_span(y, inset)` returns the bezel- and corner-safe horizontal span (`2 * circle_half_span(y, inset)`) on every panel.
2. On top of that, `UiLayout` and the components branch on three **width buckets**: below 380 px, 380–520 px, and above 520 px.
3. `LATTER_ROUND_DISPLAY` selects hand-placed round coordinates (for example the dock at x = 112, 193, 274, 355).
4. `LATTER_IS_WEARABLE` moves the header, battery and corner radius clear of the watch's rounded corners.
5. Touch distances and minimum text sizes follow the panel's PPI ([1.6](#16-touch-constants), [1.2](#12-typography)).

**PPI** is the diagonal of the panel's active area in pixels divided by that diagonal in inches (for a round panel, the diameter), from the panel drawing or datasheet (D-018). Mobile's 313 comes from Waveshare's outline drawing (38.99 × 38.99 mm for 480 × 480). The board facts checker (`tools/check_boards.py`) cross-checks the `LATTER_*` flags, including the PPI, against `boards/facts/<board>.json`, and latter-verify checks this table against the board files (**`[DEVICE UI DOC]`**).

## 7. Content and terminology

- Use Home Assistant terms: **System**, **Sites**, **Areas**, **Devices**. Never "household", "fleet" or "room". **Enforced: `[TERMINOLOGY]`** (headers, and string literals in the GUI and module YAML).
- No product or board names in UI logic (**`[TRAITS NOT NAMES]`**).
- **Descriptive option names, no third-party names** (D-020): voice-overlay styles are Nebula, Solaris, Rings, Light Bar and Dot Wave; screensavers are Ambient Clock, Aurora, Starfield, Digital Rain and Fireworks; the games are Marbles, Archery, Bricks, Chomp, 2048 and Memory Match. The wake-word models keep their upstream names. **Enforced: `[TERMINOLOGY]`**.
- Unknown values show `—`; no demo defaults (**`[UNKNOWN GLYPH INVARIANT]`**).
- Fixed option labels are part of the product: clock faces (Minimal Digital, Modern Roman (Analog), Modern Digital (Bold)) and **Sleep & Wake: Power Profile** names (**`[POWER PROFILE INVARIANT]`**).
- Casing: page headers and overlay titles in UPPERCASE ("HOME SETTINGS", "FIRMWARE UPDATE"); status lines and empty states in sentence case, with an ellipsis for progress ("Preparing flash..."). *Documented only.*

## 8. Performance

- **Measure** with **System: Telemetry Profiler**: `[PROF:FRAME]` logs render time when the page changes or a frame is slow; `[PROF:FRAME_SUM]` gives frames, fps, average and maximum every 5 s, plus the DMA-clear hit rate; `[PROF:SECT]` splits time by section.
- **Frame budget** (D-010): animated surfaces (dock and mode-bar slides, list transitions, games, ambient visuals) target the chip's animation frame, 26 ms on the ESP32-S3 and 18 ms on the ESP32-P4; everything else targets 42 ms. **`[SLOW_FRAME]`** flags a frame at 65 ms, or 110 ms on panels above 300,000 pixels (`kSlowFrameUs`, `ui_gfx.h`); the flag tightens once profiler walks on each board meet the targets.
- **Costs to avoid:** a second full-screen fill (about 10 ms on the S3), per-pixel virtual calls (`it.*`), partial display buffers (the driver re-runs the lambda per chunk), and floating-point trig in pixel loops.

## 9. Accessibility and legibility

Minimums (D-019):

- **Touch targets:** the hit area is at least 7 mm across; it may extend beyond the drawn element.
- **Text:** `font_small` at least 2.0 mm, `font_micro` at least 1.3 mm (**`[FONT MM]`**).
- **Contrast:** small text at least 4.5:1 on its background; large text (`font_date` and up) and status glyphs at least 3:1; disabled text exempt (**`[TEXT CONTRAST]`**).
- **Non-touch devices** keep the focus ring visible permanently.

As built:

| | Pocket | Mobile | Wrist | Remote | Desk |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `font_small` | 2.0 mm | 2.0 mm | 2.0 mm | 3.2 mm | 3.0 mm |
| `font_micro` | 1.3 mm | 1.4 mm | 1.4 mm | 2.2 mm | 2.0 mm |
| Dock icon, drawn (2 × `dock_r`) | 5.2 mm | 4.4 mm | 4.4 mm | 7.4 mm | 8.0 mm |
| Dock icon, hit (2 × `dock_hit_r()`) | 8.2 mm | 7.1 mm | 7.3 mm | 12.3 mm | 11.2 mm |

## 10. Decision log

A standing decision stays in force until a new entry supersedes it. Changing one requires a new entry that names the entry it supersedes, explains why, and has the project maintainer's approval. Entries are never edited after they're accepted, except to set their status to "Superseded by D-xxx" or to record when they were implemented.

### D-001: Traits, not names
- **Date:** 2026-11-07
- **Decision:** UI code branches on `LATTER_*` capability flags and screen geometry, never on product or board names.
- **Why:** One GUI runs on every board, including boards that don't exist yet.
- **Status:** Accepted.

### D-002: Single-source geometry
- **Date:** 2026-11-07
- **Decision:** Drawing coordinates and touch hit-tests come from the same `UiLayout` value or `scale_*` helper. Touch thresholds are in millimetres via `LATTER_PANEL_PPI`.
- **Why:** Raw pixels in touch handlers are the main cause of layouts that work on one panel and break on another.
- **Status:** Accepted; thresholds implemented 2026-11-07 (D-011).

### D-003: Home dock vs in-app mode bar
- **Date:** 2026-11-07
- **Decision:** Page 1 uses floating circular app icons; multi-mode apps (Sky & Weather, Area Control, Whole-Home Hub) use a connected stadium-pill mode bar at the same height. Both animate with the 210 ms motion and show the proportional scrollbar above 4 items. Both Home affordances (swipe up, tap the pill) exist on every top-level page.
- **Why:** Launching an app and switching a view inside it are different actions and look different.
- **Status:** Accepted.

### D-004: Row-major, full-fidelity RGB565
- **Date:** 2026-11-07
- **Decision:** Every renderer loops `y` outside and `x` inside, writes through `fast_*` primitives and `blit_rgb565_scanline`, keeps all 16 bits (no lossy masks, no skipped scanlines), and never uses `it.image()` or `it.draw_pixels_at()`.
- **Why:** ESPHome's buffers are row-major and its primitives cost a virtual call per pixel; the lossy shortcuts visibly posterize art and camera images.
- **Status:** Accepted.

### D-005: One background paint per frame
- **Date:** 2026-11-07
- **Decision:** Auto-clear is off. Every frame paints its base once through `frame_base()` (or claims or skips it).
- **Why:** A second full-screen fill costs about 10 ms per frame on the S3 boards.
- **Status:** Accepted.

### D-006: Alarms outrank the voice assistant
- **Date:** 2026-11-07
- **Decision:** The alarm/timer/Find My takeover renders above the voice-assistant takeover, and starting an alarm sound stops the voice teardown.
- **Why:** A ringing alarm must never be hidden or silenced by a voice turn.
- **Status:** Accepted.

### D-007: Concentric bezel border
- **Date:** 2026-11-07
- **Decision:** The alert border follows the panel's own corners: rings on round panels, and a single-pass scanline border with `LATTER_CORNER_R` (98 on wearables, 20 on other rectangular panels) elsewhere.
- **Why:** A border that isn't concentric with the bezel looks misaligned.
- **Status:** Accepted.

### D-008: Solid-filled, negative-space icons
- **Date:** 2026-11-07
- **Decision:** Dock, Whole-Home Hub, Area Control and Tools icons are monochrome solid silhouettes with negative-space cutouts.
- **Why:** Outline icons thin out and shimmer at small sizes on these panels.
- **Status:** Accepted.

### D-009: Same apps on every board
- **Date:** 2026-11-07
- **Decision:** All five boards ship the three clock faces and the six-game suite. Games steer by IMU tilt where there is an IMU and by touch elsewhere.
- **Why:** Features follow capabilities, not products.
- **Status:** Accepted.

### D-010: Frame budget
- **Date:** 2026-11-07
- **Decision:** Animated surfaces target the chip's animation frame (26 ms S3, 18 ms P4); everything else targets 42 ms. `[SLOW_FRAME]` stays at 65 ms (110 ms above 300,000 pixels) until profiler walks on each board meet the targets, then tightens.
- **Why:** The earlier 22 ms figure was below the S3's own animation frame and never matched the code.
- **Alternatives:** Flag at 22 ms; keep 65/110 ms with no target.
- **Status:** Accepted.

### D-011: Touch thresholds in millimetres, calibrated on the reference panel
- **Date:** 2026-11-07
- **Decision:** Every gesture threshold is a named millimetre constant in `UiLayout`, equal to the pixel value the 466 px reference panel (266 ppi) used. The Area Control slider zone ends at `mode_bar_zone_top()` (`scale_s(14)` above the mode bar).
- **Why:** The reference panel keeps its tuned feel, and every other panel gets the same physical distances.
- **Alternatives:** Keep pixels and drop the millimetre rule.
- **Status:** Accepted; implemented 2026-11-07.

### D-012: The corner latch runs on every panel
- **Date:** 2026-11-07
- **Decision:** The watermark latch at the top of the display lambda runs on round and rectangular panels.
- **Why:** The round panel's `MipiSpiBuffer` needs the full window too; the earlier "non-round only" wording was stale.
- **Status:** Accepted; amended by D-031 (latches full-width `2px`-aligned horizontal dirty bands `[y0, y1]`).

### D-013: Page 9 is the Whole-Home Hub
- **Date:** 2026-11-07
- **Decision:** Page 9 (`PageId::WHOLE_HOME`) is the Whole-Home Hub with alarm, thermostat, garage & locks and vacuum tabs, drawn by `render_whole_home_page`.
- **Why:** The renderer was named for its first tab only.
- **Status:** Accepted; implemented 2026-11-07.

### D-014: Dimming in eighths
- **Date:** 2026-11-07
- **Decision:** Dimming uses per-channel eighths (`scale_rgb565_q3`, 37.5–87.5 %), with the 50 % and 25 % paired dimmers as fast paths.
- **Why:** Art scrims need more than two levels; the rule that matters is per-channel dimming with no lossy masks.
- **Status:** Superseded by D-027.

### D-015: One motion system
- **Date:** 2026-11-07
- **Decision:** Every UI transition is time-based and eases out cubic over `kMotionMs` (210 ms): the dock and mode-bar slides and the list and carousel page transitions.
- **Why:** The frame-stepped list paging moved at a different speed and curve, and depended on frame timing.
- **Alternatives:** Keep both systems and document them.
- **Status:** Accepted; implemented 2026-11-07.

### D-016: Three pagers, three roles
- **Date:** 2026-11-07
- **Decision:** The viewport scrollbar sits under a horizontal window of more than 4 items (dock, mode bar); page pips mark paged carousels (Games launcher, cameras); vertical chevrons mark paged vertical lists (5-slot lists, Tools launcher).
- **Why:** Each idiom fits one layout; mixing them would make paging harder to read.
- **Status:** Accepted.

### D-017: Back labels
- **Date:** 2026-11-07
- **Decision:** "< Back" returns to the parent; the home pill, not a label, returns Home from a top-level page; "Exit" only ends a game; "< Cancel" only abandons an edit. Every back label sits at `layout().back_y`.
- **Why:** Five spellings and two positions had drifted in.
- **Status:** Accepted; implemented 2026-11-07.

### D-018: PPI from the panel's active area
- **Date:** 2026-11-07
- **Decision:** `LATTER_PANEL_PPI` is the active area's diagonal in pixels divided by its diagonal in inches, taken from the panel drawing or datasheet. Mobile is 313 (Waveshare outline drawing: 38.99 × 38.99 mm for 480 × 480).
- **Why:** Mobile's earlier 222 was width over diagonal, which made every millimetre distance on it about 30 % too small.
- **Status:** Accepted; implemented 2026-11-07.

### D-019: Accessibility minimums
- **Date:** 2026-11-07
- **Decision:** Hit targets at least 7 mm across; `font_small` at least 2.0 mm and `font_micro` at least 1.3 mm; small text at least 4.5:1 and large text and status glyphs at least 3:1 (disabled text exempt); a permanent focus ring on non-touch devices.
- **Why:** Pixel-sized fonts and targets were physically smallest on the densest panels (Wrist, Mobile).
- **Status:** Accepted; implemented 2026-11-07 (Wrist and Mobile fonts, dock hit radius, small-text colors).

### D-020: Descriptive option names
- **Date:** 2026-11-07
- **Decision:** Options never use third-party product names. Voice-overlay styles Jarvis, Alexa and Gemini became Rings, Light Bar and Dot Wave; the screensavers Aurora Borealis and Matrix Rain became Aurora and Digital Rain; the game Breakout became Bricks. Names only: the visuals and gameplay are unchanged, and there are no aliases. Wake-word models keep their upstream names.
- **Why:** Latter is a public product; third-party names are trademarks.
- **Status:** Accepted; implemented 2026-11-07.

### D-021: Chrome tokens
- **Date:** 2026-11-07
- **Decision:** Shared chrome draws with the named `kChrome*` constants in `ui_core.h`. The remaining `Color(r, g, b)` literals in page art may only go down. `col_solaris_gold` and `col_accent_cyan` are motion-only accents. Duplicate palette entries are removed.
- **Why:** Chrome colors were scattered literals, so one change meant hunting through every header.
- **Status:** Accepted; implemented 2026-11-07.

### D-022: One go-home predicate
- **Date:** 2026-11-07
- **Decision:** `latter_ui::can_go_home()` decides whether a page is top-level, for the home pill and every touch router, with one start zone and one distance for the swipe. In Music's art-only view the pill is hidden and the swipe still works.
- **Why:** Four drifted copies disagreed (the pill hid while the swipe still exited).
- **Alternatives:** Disable the swipe while the pill is hidden.
- **Status:** Accepted; implemented 2026-11-07.

### D-023: One debounce pattern
- **Date:** 2026-11-07
- **Decision:** Taps debounce through `touch_router().allow_tap(now, TOUCH_DEBOUNCE_*)`; the constants in [1.6](#16-touch-constants) are the only debounce values.
- **Why:** 29 per-handler timestamps used the same constants in slightly different ways.
- **Status:** Accepted; implemented 2026-11-07.

### D-024: The 466 reference grid and three width buckets
- **Date:** 2026-11-07
- **Decision:** Layout values are written for 466 × 466. `scale_*` keeps them unchanged on panels up to 520 px and scales above; components branch on three width buckets (below 380, 380–520, above 520 px).
- **Why:** It is how every page is built; ratifying it keeps new pages consistent.
- **Status:** Accepted.

### D-025: Shared header, sub-header and empty state
- **Date:** 2026-11-07
- **Decision:** Pages draw their title with `render_page_header`, the line below with `render_sub_header`, and empty or loading cards with `render_empty_state`.
- **Why:** Each page had inlined its own, and their fonts, colors and shadows drifted.
- **Status:** Accepted; implemented 2026-11-07.

### D-027: Anti-aliased shapes and ordered-dithered gradients
- **Date:** 2026-11-08
- **Decision:** Rounded rectangles, stadiums and circles in the chrome draw through one anti-aliased implementation (`fill_round_rect_aa` and its circle and ring forms): straight edges on pixel boundaries, curved edge pixels blended by coverage against what's already in the framebuffer, border and fill each written once. Gradient fills interpolate at 8.8 fixed point, and art scrims dim in 1/256 with smooth ramps; both quantize to RGB565 with a 4×4 ordered (Bayer) dither. Output stays full 16-bit, row-major, with no lossy masks or skipped scanlines. The mode bar has no slot separators. Supersedes D-014 (dimming in eighths).
- **Why:** Hard-edged Bresenham curves looked rough next to each other (the stadium caps met their straight runs with steps), and stepped bands and eighth-step scrims showed stripes on the bright IPS panels.
- **Cost:** Coverage math touches only curved edge pixels (one or two `sqrtf` per corner row, none per pixel). Dithered spans store per pixel instead of in pairs. Estimated well under 1 ms per frame on the S3 boards for a page of cards, a mode bar and the Home dock; art scrims are dithered once when the art cache is composed.
- **Alternatives:** Lower-contrast gradients without dithering; supersampled shapes (slower).
- **Status:** Accepted; implemented 2026-11-08.

### D-028: Home activity card: one line on round, two lines elsewhere
- **Date:** 2026-11-08
- **Decision:** The round panel keeps a one-line activity card, centered between the bottom of the weather line and the top of the dock. Every other panel shows a two-line card (two rows 46 apart plus padding, scaled above 520 px) whose bottom sits `home_activity_dock_gap()` (3 mm) above the dock: data above, controls below. With two or more front-deck items both lines show; every 8 s the bottom line moves up to the top and the next item enters below, wrapping, with the shared motion (kMotionMs, ease-out cubic). A pinned item (running stopwatch, or a timer in its last minute) keeps the top line while the bottom line rotates. One item sits centered in the same fixed-height card; with none the card is hidden. A tap acts on the line it lands on; the right-hand button acts inline; the dropdown column appears when there are more items than lines and opens the full list. While the card shows, the Home dock's swipe and tap zone starts 1 mm below it (`home_dock_zone_top()`, never below the dock's own tap band), so a swipe on the card never pages the dock. Supersedes the earlier single-line card at a fixed y (266 on the 466 grid) with its frame-stepped three-step reel.
- **Why:** On the larger panels the card sat high and close to the weather line, and one line hid most live items.
- **Open:** Resolved by D-029 (glass card).
- **Status:** Accepted; implemented 2026-11-08. Amended by D-029 (no rotation while the items fit, glass, Activity Card setting).

### D-026: An "Offline" pill shows a lost Home Assistant connection
- **Date:** 2026-11-08
- **Decision:** When the Home Assistant API has been disconnected for 8 s, a small pill reading "Offline" appears at the top center: `font_micro` in `col_gold` on a frosted-glass pill/tab (updated by D-030). On rectangular panels it hangs from the top edge as a tab, which keeps it above every page header; on the round panel it floats just inside the bezel. It is drawn after the page, never over a takeover, and not with the screen off, during hibernation or during an update. It replaces the earlier link-off glyph (two rings and a slash, listed in section 2), which had no decision entry of its own.
- **Why:** The glyph was a few pixels wide at fixed size and read as two stray dots; a word can't be mistaken for a defect and says why controls aren't responding.
- **Alternatives:** A larger broken-link glyph; no indicator.
- **Status:** Accepted; implemented 2026-11-08. Amended by D-030 (frosted glass fill).

### D-029: Universal translucent glass surfaces, system-wide wallpaper backdrops, Activity Card setting
- **Date:** 2026-11-08 (expanded 2026-11-09)
- **Decision:** (1) The Home activity card rotates only when it has more items than lines: a two-line card holds one or two items still and cycles from three; the one-line round card holds one still and cycles from two. A pinned item keeps the top line, and the free line cycles only when two or more other items share it. (2) Whenever a wallpaper or live Weather Sky backdrop is active (`backdrop_active()`), the Home activity card, Home dock circular buttons, standard cards (`render_modern_card`), stadium pill buttons (`render_pill_button`), and in-app mode bar tracks (`render_in_app_mode_bar_track`) render as translucent glass: a 1 px white sheen edge (36 % at the top fading to 11 % at the bottom on neutral borders, or the semantic accent border on highlighted controls) around the tint, blended over the backdrop already drawn (`render_glass_card`, `fill_round_rect_glass`; 8-bit alpha in the RGB565 level domain with the D-027 ordered dither and coverage-scaled edges). The tint is at least 60 % opaque (`kGlassAlphaMinQ8`). Each frame the firmware samples the backdrop under the surface (`sample_glass_backdrop`, about 12 sparse rows) and raises the tint until the text clears 5:1 (`kGlassTextContrast`, 4.5:1 plus margin) against the mean of the brightest 3 % of that backdrop (`kGlassBrightPercent`): white clouds, bright wallpapers and daylight sky make the glass darker; night sky leaves it light. The configured wallpaper (`render_page_wallpaper_base`) renders as the full-screen backdrop across Home (when **Home: Weather Background** is off; while **Home: Weather Background** is on, the Home screen renders the live Weather Sky while Home Settings, Activity Overlay, and all other backdrop pages continue to show the active wallpaper), System Settings, Sky & Weather (all four subpages: Current, Forecast, Sun, and Moon), Area Control, Tools (launcher and all utilities except full-white Flashlight and full-bleed Camera/Map frames), and Whole-Home Hub; Clock, Music, Games, and Remote keep their dedicated canvases. (3) The card and each source are optional through the Device's Latter setting, delivered in Protocol v3 `area_context.activity_card` (`1` on, `0` off) and `activity_sources` (CSV of `calendar`, `music_queue`, `stopwatch`, `alarms`, `timers`). Empty fields (not set, or no payload yet) take the spec defaults (card on, every source). Items of a disabled source are dropped in `collect_home_activities()`, so the card, its dropdown list and the touch routers see one list; the card hides when it is off or no enabled source has an item. Amends D-028 (rotation with two items) and resolves its open translucency item.
- **Why:** Swapping two items that both fit was motion without information. The opaque card hid the sky and clouds behind it. Not every Device needs every source on its Home page.
- **Cost:** The glass reads and writes every card pixel instead of a plain span fill: about 95 k pixels on Desk and 26 k on Pocket per Home frame, estimated a few milliseconds on the P4 and 2 to 3 ms on the S3 at the 20 FPS weather cadence (not yet profiled; `[PROF:*]` HOME_TEXT covers it). Sampling is about 1 k pixel reads. A box blur would add a full extra read pass, so it was left out.
- **Alternatives:** A fixed alpha with a darker scrim strip under each text row; a real blur.
- **Status:** Accepted; implemented 2026-11-08. Amended by D-030 (glass is always enabled across all views and backdrops; Sky & Weather paints wallpapers on all four subpages and renders Current/Forecast telemetry directly on the canvas) and D-033 (Remote renders the configured wallpaper across both subpages, and Music's `None` Album Art Style renders the configured wallpaper or solid `col_bg` across Now Playing and Music menus).

### D-030: Glass is the single unconditional UI surface style across all views
- **Date:** 2026-11-10
- **Decision:** Every interactive surface and container card (`render_modern_card`, `render_pill_button`, `render_glass_circle`, `render_glass_card`, `render_in_app_mode_bar_track`, `render_offline_pill`, the Home dock app buttons, System Info, Area sliders, Calculator keys, Remote controls, Games launcher controls, and Stopwatch/Timer/Map circular controls) renders exclusively as frosted glass—regardless of whether a wallpaper, Weather Sky, album art, or solid black (`col_bg`) occupies the frame base. Sky & Weather renders the configured wallpaper across all four subpages (Current, Forecast, Sun, and Moon), with Current high/low & humidity/wind telemetry and Forecast 5-day rows rendered directly on the canvas (like the hero temperature and condition) above the frosted-glass mode bar. The legacy non-glass card, pill, circle, and keypad branches are retired (`backdrop_active()` is retained solely to suppress opaque `col_bg` scroll-clipping masks and add drop shadows on plain text over busy imagery). On pitch-dark or solid-black backdrops (`mean < 42`) and uniform mid-tone skies where a dark tint would blend darker than its surroundings, `resolve_glass_fill()` lifts dark glass tints subtly toward `kChromeHomePill`, applies a top-lit interior frost alpha gradient, and boosts the 1 px top-to-bottom `kChromeGlassSheen` rim; on Home over Weather Sky, the dock buttons harmonize with `resolve_home_card_palette()` so they never crush into dark circles against daytime or nighttime skies. Supersedes the `backdrop_active()` gating and Sun/Moon wallpaper exemption in D-029 (2) and updates the Offline pill fill in D-026.
- **Why:** Switching between frosted glass when a wallpaper was shown and flat opaque buttons when no wallpaper was configured created two competing visual languages, and blending `kChromeGlassTint` at 60 % directly onto `(0, 0, 0)` crushed Home dock app buttons nearly to black. One canonical frosted-glass surface language keeps every page cohesive.
- **Status:** Accepted; implemented 2026-11-10.

### D-031: Compiled `latter_ui` component, co-located `FrameHitTable`, `TouchGestureEngine`, shared widgets and `2px` horizontal-band dirty flush
- **Date:** 2026-11-10
- **Decision:** (1) The on-device UI compiles as a dedicated PlatformIO ESPHome component (`components/latter_ui/`) paired with `include/ui_*.h`, placing inactive game and tool state structs (`Marbles`, `Archery`, `Bricks`, `Chomp`, `2048`, `Memory`, `Calculator`, `Stopwatch`) in PSRAM BSS (`LATTER_PSRAM_BSS` / `EXT_RAM_BSS_ATTR`) so internal SRAM stays free. (2) Interactive controls register their exact rendered pixel bounds into `frame_hits()` (`FrameHitTable`, `ui_hit_table.h`) as they draw, and the compiled `TouchGestureEngine` (`ui_gesture_engine.h`, `components/latter_ui/gesture_engine.cpp`) and `render_active_focus_overlay()` read `frame_hits()` and `UiLayout` millimetre thresholds directly—retiring the 3,556-line `touch_handlers.yaml`. (3) Reusable multi-page widgets and vector icons live in `ui_widgets.h` / `ui_widgets.cpp` and `ui_icons.h`: `ui::toggle_switch`, `ui::toggle_card_row`, `ui::pill_button`, `ui::stepper_row`, `ui::hsv_color_wheel` (single LUT-backed color wheel), `ui::confirm_modal`, `ui::game_over_sheet`, and `ui::pin_pad_modal` (on-device 3×4 PIN keypad for 3-tier perimeter security actions). (4) All system-level takeovers execute through `latter_ui::render_system_takeovers(...)` (`ui_compositor.h`) in strict priority order (inserting the Security PIN pad modal between Voice Assistant and Notification/Camera), shared identically by `guis/universal/gui.yaml` and custom GUIs. (5) Proportional multi-geometry helpers `scale_w(v466)` and `safe_span(y)` scale down below 466 px as well as up above 520 px. (6) `DirtyBandTracker` (`dirty_band()`, `schedule_band_disp_update`) tracks full-width `2px`-aligned horizontal dirty bands (`y0 & ~1` to `min(SCREEN_H - 1, y1 | 1)`) and latches `(0, y0)` and `(SCREEN_W - 1, y1)` at the top of the display lambda, amending D-012's full-panel-only corner latch for localized updates while preserving `MipiSpiBuffer`'s contiguous `stride == SCREEN_W` zero-copy DMA fast path.
- **Why:** Separate YAML touch lambdas drifted from C++ draw coordinates on 320×480 and 720×720 panels and inflated single-TU build times; full-frame SPI/QSPI flushes sent 434 KB on localized 1 Hz clock or progress updates; and 3-tier perimeter security required a first-class modal keypad.
- **Status:** Accepted; implemented 2026-11-10. Amended by D-032 (8-header + 10-`page_*.cpp` + `touch_engine.cpp` layout, pure-C++20 `FrameHitTable` without per-region callbacks, and unified `focus_at()` rotary focus ring).

### D-032: 8 public UI headers, 10 compiled `page_*.cpp` translation units, `UiFrameContext`, and unified `FrameHitTable` rotary focus
- **Date:** 2026-11-11
- **Decision:** (1) Consolidate the 17 header-only `include/ui_*.h` files into 8 clean public headers (`ui_types.h`, `ui_gfx.h`, `ui_hit_table.h`, `ui_layout.h`, `ui_widgets.h`, `ui_compositor.h`, `ui_touch_engine.h`, `ui_pages.h`) backed by 10 compiled `components/latter_ui/page_{clock_home,music,weather,area_hub,remote,system,ambient_va,tools,comms,games}.cpp` translation units plus `ui_gfx.cpp`, `hit_table.cpp`, `ui_widgets.cpp`, `ui_compositor.cpp`, and `touch_engine.cpp` (replacing `ui_gesture_engine.h` / `ui_touch_router.h` / `gesture_engine.cpp` / `ui_icons.h`). (2) Page renderers take concrete `(esphome::display::Display &it, const UiFrameContext &ctx)` signatures instead of header templates, reading typed runtime and persisted state from `latter_state.h` (`runtime_state()`, `persisted_prefs()`). (3) Separate top-left `render_card_box(x0, y0, ...)` from centered `render_modern_card(cx, cy, ...)`, unify 4-column steppers on a `68 px` stride, and keep `ui_hit_table.h` and host-mode `ui_compositor.h` pure C++20 without ESPHome headers (removing unused per-region `UiActionFn`/`on_event` pointers from `HitRegion`). (4) Wire `render_active_focus_overlay()` and rotary `on_input_event("next"|"prev"|"select")` directly to `frame_hits().focus_at()` / `focus_count()`, and skip full-screen clears outside `[band_y0, band_y1]` when `!dirty_band().is_full()`. Amends D-031's file names and `HitRegion` callback field.
- **Why:** Header-only page templates forced full rebuilds of 20 k lines on every edit and hid coordinate drift between manual rotary focus slots and drawn Music/Remote/Tools controls; pure-C++20 `FrameHitTable` and `DirtyBandTracker` allow standalone host unit tests (`tests/host/test_all.cpp`) and save ~576 B of per-frame hit-table RAM.
- **Status:** Accepted; implemented 2026-11-11.

### D-033: `None` Album Art Style on Music and system wallpaper across Remote
- **Date:** 2026-11-12
- **Decision:** (1) Add `None` to **Music: Album Art Style** (`music_fullscreen_art_mode`: `Full Screen`, `Letterbox`, `Standard`, `None`). When `None` is selected, Music skips remote album-art HTTP fetches and renders the Device's configured background (`render_page_wallpaper_base`: active wallpaper if set, otherwise solid `col_bg` black) across both Now Playing (`music_overlay_mode == 0`) and all Music menus (`music_overlay_mode` 1–8), suppressing opaque `col_bg` scroll-clipping masks whenever `backdrop_active()` holds. (2) Render the configured wallpaper (`render_page_wallpaper_base`) across the entire Remote app (`PageId::REMOTE`, `gui_page == 7`), including both the main concentric D-pad remote (`remote_subpage == 0`, with the outer D-pad disc rendered via `render_glass_circle`) and the `CONTROL` device/power menu (`remote_subpage == 1`), leaving Clock and Games as the dedicated non-wallpaper canvases. Amends D-029 (2).
- **Why:** Users who prefer a minimal or uniform aesthetic can hide album art in Music while honoring their selected black or wallpaper background, and extending wallpaper plus frosted-glass D-pad chrome across Remote keeps Remote visually consistent with Home, System, Sky & Weather, Area Control, Tools, and Whole-Home Hub.
- **Status:** Accepted; implemented 2026-11-12.

### D-034: Protocol v3 `device_prefs` + `PersistedDevicePrefs` v2 NVS and zero ESPHome template preference entities
- **Date:** 2026-11-12
- **Decision:** (1) Retire `modules/settings_entities.yaml` (`42` ESPHome `template` `select`/`switch`/`number` preference entities and `3` action buttons) and store all on-device user preferences in one trivially-copyable NVS POD struct `PersistedDevicePrefs` (`kPersistedPrefsVersion = 2U`, `include/latter_state.h`). (2) Render frames (`guis/universal/gui.yaml`) and audio/power scripts read `latter_ui::persisted_prefs()` and `pref_*_cstr()` directly with zero per-frame `std::string` heap allocations. (3) Synchronize preferences two-way with Home Assistant over Protocol v3: inbound via `latter_push_v3(domain="device_prefs")` (`parse_v3_device_prefs_payload`) and `latter_device_command(command=...)`; outbound on touchscreen edits (`UiRuntimeHost::set_select` / `toggle_switch` / `set_number` -> `latter_actions::record_local_pref_edit`) via `latter.set_device_prefs` when connected, or queued in `PersistedDevicePrefs::dirty_mask` (`uint64_t`) with debounced 1.5 s NVS flushes (`flush_persisted_prefs_if_dirty` before restart/deep-sleep) and reconciled via `device_hello` (`prefs_seq` + `dirty_prefs`) on reconnect. (4) Replace the 30 s polled ESPHome `timer_remaining_sensor` / `timer_active_binary_sensor` / `alarm_ringing_binary_sensor` with event-driven `latter.timer_alarm_state` RPC emissions (`check_and_emit_timer_alarm_state`), keeping quiet light-sleep radio traffic at zero.
- **Why:** Exposing 42 ESPHome `template` entities duplicated configuration state across two Home Assistant devices, went `unavailable` whenever battery Devices hibernated, broke sleep-safe Device Groups, allocated ~8–10 KB of ESP32 entity objects, and woke Wi-Fi every 30 s for countdown timer polling.
- **Status:** Accepted; implemented 2026-11-12. Amended by D-035 (`kPersistedPrefsVersion = 3U`, retiring `area_default_category` and `area_show_all_category` in favor of HA-ordered `area_context.available_domains`).

### D-035: Manifest display-geometry slot capacities (`slot_count` / `card_slot_count`) and ordered Area category mode bar
- **Date:** 2026-11-12
- **Decision:** (1) Formalize two display-geometry slot capacities in `hal_manifest.h` and `DeviceRuntimeState`: `LATTER_SLOT_COUNT` (`5`, emitted as `"slot_count"` in `device_hello` `manifest_json` for 5-row vertical lists with no bottom mode bar: Music Library, Area Picker, Calendar) and `LATTER_CARD_SLOT_COUNT` (`3`, emitted as `"card_slot_count"` for tall slider/action cards above a bottom mode bar: Area Control entities), both overridable per board for future larger displays. (2) Retire `area_default_category` and `area_show_all_category` from Protocol v3 `device_prefs`, `PersistedDevicePrefs` (`kPersistedPrefsVersion = 3U`), and on-device settings. Instead, Home Assistant computes `area_context.available_domains` as the exact ordered, non-empty subset of the Device's configured `area_categories` pill list (`scene`, `light`, `cover`, `fan`, `switch`, `climate`, `media`, `all`). Firmware parses `available_domains` in exact token order (`parse_area_categories_csv` / `collect_area_categories`), opens the first available category by default, hides the bottom mode bar when `cat_count == 0`, and renders `"No controls in this Area"` when no Area entity cards exist.
- **Why:** Page capacities follow physical display geometry and whether a bottom mode bar is present—never an arbitrary per-user number—and driving the Area mode bar from one ordered `area_categories` token list gives users full category selection and ordering (matching `dock_apps`) without dead tabs on empty categories.
- **Status:** Accepted; implemented 2026-11-12.

### D-036: Single-App Mode (`home_screen_enabled`), expanded `start_screen`, `weather` Dock App, `return_to_home_timeout`, and ordered Home Hub categories
- **Date:** 2026-11-13
- **Decision:** (1) Expand `start_screen` to `Home`, `Clock`, and all eight Dock Apps (`Media`, `Home Hub`, `Area Controls`, `Remote`, `Weather`, `Cameras`, `Games`, `Tools`), adding `"weather"` (`PageId::WEATHER = 5`, monochrome sun-behind-cloud glyph, `kMaxDockApps = 10`) as an optional Dock App. (2) Add `home_screen_enabled` (default `true`, `kPersistedPrefsVersion = 4U`). When `home_screen_enabled` is `true`, starting on a non-`Home` app preserves the bottom home indicator pill (`can_go_home` == `true`) so swiping up returns to `PageId::HOME` where the App Dock lives. When `home_screen_enabled` is `false` (Single-App Mode), `"Home"` is excluded from `start_screen` (coercing `"Home"` to `"Clock"`), `can_go_home()` returns `false` on the anchor app's root (`is_on_anchor_root`) so the bottom home indicator pill hides and swipe-up is inert, sub-pages retain `< Back` to that app's root, and any temporary takeover jump (such as a doorbell camera popup) routes `navigate_home()` back to the anchor `start_screen` via `navigate_to_start_screen()`. (3) Add `return_to_home_timeout` (`15s`, `30s`, `1 min`, `2 min` default, `5 min`, `10 min`, `Never`): when `home_screen_enabled` is `true`, idle screens reset back to `PageId::HOME` unless an exempt live session is active (`should_return_to_home_on_idle`: active playback on Music Now Playing `overlay_mode == 0`, active game, running countdown on Timers, running Stopwatch, Flashlight/Level, active voice/TTS, ringing alarm/timer/Find My, or active notification/doorbell call). (4) Drive the Whole-Home Hub (`PageId::WHOLE_HOME = 9`) bottom mode bar and horizontal tab swipes from `area_context.home_categories` (`alarm`, `thermostat`, `entry`, `vacuum`, filtered by HA to categories with a configured entity), rendering `"No Home Controls configured"` when empty and hiding an unconfigured half of `Entry & Garage` when only lock or garage is configured.
- **Why:** Dedicated single-purpose Devices (such as a wall Area Controller or dedicated Media controller) need a clean single-app experience with no dead Home gesture, multi-app Devices left on an Area or menu should self-reset to Home unless actively playing music or running a tool, Weather deserves a direct Dock entry point, and Home Hub categories should follow the same ordered, entity-aware pill model as Area Controls.
- **Status:** Accepted; implemented 2026-11-13.

## Known gaps

Nothing here contradicts a standing decision; these are the places the code doesn't fully reach it yet.

- Row-major loops, skipped scanlines, IPS row patterns and icon style are reviewed by hand.
