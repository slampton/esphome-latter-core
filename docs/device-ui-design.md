# Device UI design guide

This guide is the standard for the on-device GUI: the universal GUI in [`guis/universal/`](../guis/universal/) and the C++ UI headers in [`include/`](../include/) (`ui_core.h`, `ui_layout.h`, `ui_touch_router.h`, and the `ui_*.h` page headers). Read it before changing the device UI. [Building a GUI](building-a-gui.md) explains how to replace the GUI; [Porting hardware](porting-hardware.md) explains how to add a board. Both defer to this guide for visual, touch and rendering rules. Latter Studio, the admin panel in Home Assistant, has its own guide in the `ha-latter` repository (`docs/studio-design.md`).

The guide describes the GUI **as built**. Each rule is marked:

- **Enforced: `[TAG]`**: a latter-verify check fails the build when the rule is broken.
- **Documented only**: nothing checks it yet.

Where the code and an earlier document disagree, this guide doesn't pick a winner. The disagreement is listed under [Decisions needed](#decisions-needed) with options and a recommendation, and the rule stays as built until the project maintainer decides. The [decision log](#10-decision-log) records the standing decisions.

---

## 0. Principles

1. **Traits, not names.** UI code branches on capability flags (`LATTER_ROUND_DISPLAY`, `LATTER_IS_WEARABLE`, `LATTER_HAS_IMU`, `LATTER_HAS_TOUCH`, `LATTER_PANEL_IPS`, screen size), never on a product or board name. **Enforced: `[TRAITS NOT NAMES]`** (no `waveshare_*` board IDs in core headers, modules or the GUI) and **`[TERMINOLOGY]`** (no product names or "household"/"fleet" in `include/*.h` code lines).
2. **One canonical path.** Every role (a header, a back affordance, a pager, a hit-test) has one implementation, in `ui_core.h` or `UiLayout`. A page that needs something new adds it there and reuses it. *Documented only* (see [section 2](#2-components) for where the GUI falls short today).
3. **Geometry is single-source.** A drawn element and its touch target come from the same `UiLayout` value or helper ([section 4](#4-gestures-and-hit-testing)). *Documented only.*
4. **One reference grid.** Layout values are written for the 466 × 466 reference panel and adapted with `scale_x`/`scale_y`/`scale_s` and the width buckets in [section 6](#6-per-board-adaptation).
5. **Unknown renders as unknown.** A value the device hasn't received shows `—` (`kUnknownGlyph`), never a demo default. **Enforced: `[UNKNOWN GLYPH INVARIANT]`** (no `--` placeholders in `include/*.h`).
6. **Fast and row-major.** Every pixel goes through the `fast_*` primitives in row-major order, and every frame paints its background exactly once ([section 5](#5-rendering-pipeline)).

## 1. Tokens

### 1.1 Color palette

Colors are ESPHome `color:` IDs in [`gui.yaml`](../guis/universal/gui.yaml), passed into the page renderers as arguments. The palette's intent, stated in its comments, is a muted "steel and silver" set that doesn't bloom on AMOLED. Contrast is against black (`col_bg`), computed with the WCAG formula.

| ID | Hex | Role | Contrast |
| :--- | :--- | :--- | :--- |
| `theme_primary` | `6B8E9E` | Accent: page headers, active state, focus ring, scrollbar thumb, mode-bar capsule. User-themable (below). | 6.0:1 |
| `theme_secondary` | `C0C0C0` | Secondary accent: sub-headers while feedback shows, values | 11.5:1 |
| `col_bg` | `000000` | Frame base for every page | — |
| `col_black` | `000000` | Text shadows, text on light surfaces | — |
| `col_white` | `FFFFFF` | Primary text and icons | 21:1 |
| `col_slate` | `9E9E9E` | Secondary text | 7.8:1 |
| `col_gray` | `616161` | Tertiary text | 3.4:1 |
| `col_dark_gray` | `212121` | Card fills, tracks, inactive surfaces | 1.3:1 |
| `col_gold` | `C59B27` | Highlights; the Home Assistant link-off indicator | 8.1:1 |
| `col_warm_gold` | `A88424` | Sun and celestial art. IPS panels use `Color(255, 178, 28)` on Sky & Weather instead (`gui.yaml`, Page 5). | 6.0:1 |
| `col_red` | `A84338` | Alerts, destructive state | 3.5:1 |
| `col_green` | `427453` | Positive / on state | 3.9:1 |
| `col_cyan`, `col_orange`, `col_purple`, `col_magenta` | `456E8A`, `B85C24`, `6D4B7D`, `8E4A7D` | Category and game colors | 3.0–4.6:1 |
| `col_solaris_gold`, `col_accent_cyan` | `FFDC1E`, `1EC8E6` | High-energy accents for the voice overlay and kinetic visuals | 15.5:1, 10.5:1 |
| `col_silver`, `col_light_blue` | `C0C0C0`, `6B8E9E` | Duplicates of `theme_secondary` and the default `theme_primary` | — |

**Theme presets.** `apply_theme_preset` in [`modules/features/scripts_actions.yaml`](../modules/features/scripts_actions.yaml) writes `theme_primary` from **Display: Color Profile**: Steel Blue (default, `6B8E9E`), Blue (`456E8A`), Green (`427453`), Pink (`A87884`), or Custom (a color-wheel pick through `apply_custom_rgb_harmony`). It resets `theme_secondary` to `C0C0C0`. These two are the only color IDs the core modules write, so a replacement GUI must define at least them.

**Chrome colors are literals, not tokens (as built).** The shared chrome draws with fixed RGB values: home pill `175,190,210`, scrollbar track `30,38,50` / border `50,62,78`, mode-bar track `16,20,27` / border `52,64,82` / divider `30,38,50`, back label `158,158,158`, glass tint base `22,27,36`. Across the `ui_*.h` headers there are 438 `Color(r, g, b)` literals (Sky & Weather 141, Tools 91, Games 80, Clock & Home 41, `ui_core.h` 29, Area 27, ambient/voice 16, Music 9, Remote 2, System 2). *Documented only*; see [N-13](#n-13-palette-tokens-and-literals).

### 1.2 Typography

Every font is a Google font fetched at build time: Roboto, plus Orbitron 700 for game titles. Sizes are pixel sizes set by substitutions. [`gui.yaml`](../guis/universal/gui.yaml) holds the defaults, and a board file overrides them.

| ID | Weight | Glyphs | Default size | Desk (720 × 720) | Use |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `font_time` | 400 | digits, `:.-°%—` | 77 | 112 | Clock and big numbers |
| `font_chrono_time` | 400 | digits, `sAMP°%—` | 38 | 56 | Secondary clocks, countdowns |
| `font_date` | 400 | Latin-1 + Latin Extended-A subset | 27 | 38 | Titles, large labels |
| `font_btn_bold` | 700 | same as `font_date` | 26 | 36 | Buttons, keypad |
| `font_small` | 400 | same | 21 | 30 | Body text, headers, list rows |
| `font_micro` | 400 | same | 14 | 20 | Captions; `font_small`'s stand-in on screens narrower than 380 px (clock) |
| `font_games_title` | Orbitron 700 | caps, digits, `:.-•!` | 38 | 56 | Game titles |

- The four boards up to 520 px use the defaults; only the Desk board sets its own sizes (in its board file, repeated in `devices/desk-assistant.yaml`).
- Sizes are pixels, not millimetres, so the same font is physically smaller on a denser panel (`font_micro` is about 1.1 mm on the Wrist and 2.2 mm on the Remote). See [section 9](#9-accessibility-and-legibility).
- **Text from Home Assistant passes through the glyph helpers** before drawing: `sanitize_utf8_for_font`, `is_supported_font_cp` and `fit_text_to_width_font` in `ui_core.h`, and `wrap_text_lines` in `ui_tools.h`. A glyph outside the font's set is replaced, never drawn as a missing box. *Documented only.*
- **Headers (as built):** UPPERCASE, `font_small`, `theme_primary`, centered at `layout().hdr_y`. The sub-header sits at `layout().sub_hdr_y` in `theme_secondary` while action feedback shows, otherwise in `col_slate` or `col_white`. Pages inline this; only Music and System use `render_menu_header()` (`ui_music.h`). *Documented only*; see [N-17](#n-17-shared-header-empty-state-and-feedback-components).

### 1.3 Spacing and layout anchors

There is no spacing scale. Spacing lives in `UiLayout` ([`ui_layout.h`](../include/ui_layout.h)), one struct of named anchors, reached through `latter_ui::layout()`. Values below are round (466 × 466) / rectangular.

| Anchor | Round | Rectangular | Notes |
| :--- | :--- | :--- | :--- |
| `hdr_y` | 48 | 36 wearable, 28 standard, 48 above 520 px | Page header baseline center |
| `sub_hdr_y` | 76 | 64 wearable, 56 standard, 88 above 520 px | Sub-header / feedback line |
| `back_y` | 426 | `H − scale_y(28)` | "< Back" label |
| `home_bar_y` | 434 | `H − scale_y(11)` | Home pill |
| `dock_y` | 359 | `H − scale_y(66)` | Home dock icon centers |
| `app_dock_y` | 369 | `H − scale_y(66)` | In-app mode bar centers |
| `dock_pips_y`, `app_dock_pips_y` | 404, 408 | `H − scale_y(27)` | Scrollbar / pips row |
| `dock_r` | 27 | 24 below 360 px wide, 27 standard, 40 above 520 px | Dock icon radius |
| `list_card_w` | 276 | `W − 88` below 380 px, 276 standard, scaled above 520 px | 5-slot list card width |
| `slot5_y(i)` | 236 + (i − 2) × 63 | scaled above 520 px | 5-slot list rows; row height 44 |
| `area_card3_*`, `tool_card3_*` | stride 76 / 100, height 64 / 74 | scaled above 520 px | 3-card pages |
| `tools_row_y`, `tools_col_off` | stride 105, offset 68 | 61 below 380 px | Tools launcher grid |
| `home_*_y` | date 52, clock 128, weather 200, activity 266 | scaled above 520 px | Home screen zones |
| `battery_cx/y`, `celestial_x/y` | — | per width bucket | Corner-safe indicators |

UiLayout also holds the page-specific geometry for music, light sliders, steppers, calculator, map zoom and system swatches, each paired with its hit-tester. **Rule:** a new spacing value used by more than one page, or used by both drawing and touch, goes into `UiLayout`. *Documented only.*

### 1.4 Radii

| Radius | Value | Source |
| :--- | :--- | :--- |
| Panel corner (`LATTER_CORNER_R`) | `W/2` round, 98 wearable, 20 other rectangular | Derived in `ui_core.h` from `LATTER_ROUND_DISPLAY` and `LATTER_IS_WEARABLE`; a board can override it |
| List card | 12 (Home Settings and 5-slot lists) | Page code |
| Card (`render_modern_card`) | clamped to `h/2`, minimum 2; scaled when ≤ 24 on panels above 520 px | `ui_core.h` |
| Dock icon | `dock_r` | `UiLayout` |
| Mode-bar track | `dock_r + scale_s(3)` | `render_in_app_mode_bar_track` |

### 1.5 Motion

| Motion | Constant | Source |
| :--- | :--- | :--- |
| Home dock page slide and mode-bar capsule glide | 210 ms, ease-out cubic, time-based | `DockSlideAnimator::kDurationMs` (`ui_layout.h`); runs only on pages 1, 5, 6 and 9 |
| List page transition (System, Music, Tools, Games launchers) | 2 intermediate frames 16 ms apart, then the settled frame; frame-stepped | `menu_scroll_transition` (`scripts_actions.yaml`) with `SHARED_SCROLL_EASE_116` / `_252` (`ui_core.h`) |
| Animation frame (`schedule_anim_disp_update`) | 18 ms on the ESP32-P4, 26 ms on the ESP32-S3 | `DisplayUpdateScheduler::request_anim_ptr` |
| Tools and games physics loop | 50 ms tick, at most one frame per 42 ms | `ui_anim_tick` (`timers_intervals.yaml`), `min_interactive_frame_gap_ms` |
| Ambient visuals (screensavers, weather, voice) | one step per 42 ms idle, 75 ms while local audio plays, 120 ms during alerts | `should_step_ambient_anim` (`ui_core.h`) |

Two motion systems exist side by side; see [N-07](#n-07-one-motion-system).

### 1.6 Touch constants

Millimetre constants (`UiLayout`, section 10), converted with `mm_to_px(mm) = mm × LATTER_PANEL_PPI / 25.4`:

| Constant | mm | Pocket (266 ppi) | Mobile (222) | Wrist (315) | Remote (165) | Desk (254) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `tap_slop()` | 2.1 | 22 px | 18 | 26 | 14 | 21 |
| `swipe_min()` | 3.6 | 38 | 31 | 45 | 23 | 36 |
| `home_zone_y()` (from bottom) | 5.2 | fixed y 412 | 45 | 64 | 34 | 52 |
| Home pill hit band (from bottom) | 2.8 | fixed y 422 | 24 | 35 | 18 | 28 |
| Home pill hit half-width | max(`scale_x(64)`, 6.5 mm) | 68 | 64 | 81 | 64 | 99 |

**As built, most gesture thresholds are raw pixels**, not these constants: taps are `max_travel < 30` (11 sites in `touch_handlers.yaml`), swipes need |dx| or |dy| of 18–34 px (about 20 sites), a slider drag starts at 6 px, the dock swipe at 14/22 px (move) or 18/20 px (release), and the home swipe at 26 px (move) or 24 px (release). The mm constants are used once each. A 30 px tap is 2.4 mm on the Wrist and 4.6 mm on the Remote. *Documented only*; see [N-02](#n-02-touch-thresholds-pixels-or-millimetres).

**Debounce (`ui_core.h`):**

| Constant | ms | Use |
| :--- | :--- | :--- |
| `TOUCH_DEBOUNCE_RAPID` | 100 | Rapid steppers |
| `TOUCH_DEBOUNCE_BUTTON` | 150 | Buttons and pills (the default for `touch_allow_tap`) |
| `TOUCH_DEBOUNCE_WAKE` | 180 | Settling after wake |
| `TOUCH_DEBOUNCE_DISMISS` | 200 | Dismissing an overlay |
| `TOUCH_DEBOUNCE_SCROLL` | 250 | Page transitions and chevrons |
| `TOUCH_GUARD_DISMISS` | 250 | Minimum delay after wake |
| Post-swipe lockout | 200 | Literal in the move router |

Handlers debounce two ways today: `touch_router().allow_tap(...)` and per-handler `static uint32_t last_*` timestamps.

## 2. Components

Each component has one implementation. Use it; don't hand-draw the same role. Primitives first:

**Primitives** (`ui_core.h`): `fast_pixel`, `fast_hline`, `fast_vline`, `fill_rect_row_major`, `fast_rect_outline`, `fast_filled_circle`, `fast_circle`, `fast_line`, `fast_filled_triangle`, `fast_print`, `draw_glow_line`, `blit_rgb565_scanline`. **Enforced: `[FAST PRIMITIVES]`** for `include/ui_*.h` ([section 5](#5-rendering-pipeline)).

| Component | API | Geometry from | Use for |
| :--- | :--- | :--- | :--- |
| Card | `render_modern_card(it, cx, cy, w, h, r, bg, border, label, text_c, font)` (centered, with label) and an `x0, y0` overload | Caller, from `UiLayout` | Every list row, tile and panel. Single-pass scanline fill with a glass bevel derived from the background brightness. The centered overload **resizes silently**: above 520 px it scales `w ≤ 340`, `h ≤ 96`, `r ≤ 24`; below 380 px it forces `w = W − 88` for short wide cards; otherwise it caps `w` at `W − 20`. |
| Pill button | `render_pill_button(it, x1, x2, y_mid, r, border, bg, label, text_c, font)` | Caller | A standalone action button (today only the Games launcher) |
| Glass circle | `render_glass_circle(...)` | Caller | Round transport and clock controls (Music, Clock & Home) |
| Page header | Inline: `fast_print` at `layout().hdr_y` ([1.2](#12-typography)); `render_menu_header()` in Music and System | `UiLayout::hdr_y`, `sub_hdr_y` | The title of every page |
| Back label | `render_back_button(it, cx, font, y_pos = 426, label = "< Back", text = 158 gray)` | Should be `layout().back_y` | Returning from a sub-page. Passing `"Home"` draws the home pill instead. `"< BACK"` is normalized to `"< Back"`. |
| Home pill | `render_home_indicator_bar(it, cx, y = layout().home_bar_y)` | `UiLayout::home_bar_y` | Top-level pages; drawn by `gui.yaml` after the page ([section 3](#3-navigation)) |
| Home dock | `get_home_dock_window_xs` + the Home renderer; `home_dock_anim()` | `UiLayout::dock_y`, `dock_r` | Page 1 only: floating circular app icons, 4 per window |
| In-app mode bar | `render_in_app_mode_bar_track(...)`; `mode_bar_anim()` | `UiLayout::app_dock_y`, `dock_r` | Switching the middle view of a multi-mode app (pages 5, 6, 9): a connected stadium track with a gliding capsule and conveyor chevrons |
| Viewport scrollbar | `render_viewport_scrollbar(it, cx, y, total, start_idx, thumb, dim)` | `dock_pips_y` / `app_dock_pips_y` | Under the Home dock and a mode bar when there are more than 4 items. Proportional thumb; draws nothing at 4 or fewer. |
| Page pips | `render_dock_page_pips(it, cx, y, pages, active, c_active, c_inactive)` | Caller | Paged carousels: Games launcher, camera carousel |
| Vertical chevrons | `render_vertical_page_pips(it, show_up, show_down, bg, active, inactive)` | Fixed right-edge column per width bucket | Paged vertical lists: 5-slot lists (Music, System, Area, Calendar, Remote) and the Tools launcher |
| Focus ring | `render_focus_ring_circle`, `render_focus_ring_rect`, `render_active_focus_overlay` | Per page, inside the overlay | Rotary / button navigation ([section 4](#4-gestures-and-hit-testing)) |
| Alert border | `render_bezel_alert_border(it, cx, cy, color)` | `SCREEN_R` (round: 4 rings at `R − 10 … R − 7`); `LATTER_CORNER_R` with padding 5 wearable / 3 other and thickness 5 (rectangular) | A ringing timer or alarm on Tools pages. Concentric with the panel's corners. |
| Battery indicator | `render_battery_indicator` (`ui_clock_home.h`) | `UiLayout::battery_cx/y` | Clock and Home |
| Link-off indicator | `render_link_state_indicator(it, cx, now, warn, bg)` | `scale_y(14)` | Drawn by `gui.yaml` when Home Assistant has been disconnected for 8 s or more |
| Tool launcher card | `render_tool_launcher_card` (`ui_tools.h`) | `UiLayout::tools_*` | The Tools grid |

**Icons** are monochrome solid-filled silhouettes with negative-space cutouts (Home dock, Whole-Home Hub, Area Control and Tools launcher). *Documented only.*

**States.** Unknown values use `kUnknownGlyph` (`—`, `latter_state.h`). **Enforced: `[UNKNOWN GLYPH INVARIANT]`.** Empty and loading text is written per page today ("Connecting...", "Loading camera snapshot...", "No upcoming events (Tap +)", "No active activities", "No updates available"), and action feedback is a per-page `*_action_feedback` string with a timer shown in the sub-header. There is no shared empty-state or toast component yet ([N-17](#n-17-shared-header-empty-state-and-feedback-components)).

**Missing shared components.** Toggle, slider, list row, feedback toast and empty state are hand-built on each page. Build the shared one before adding another copy.

## 3. Navigation

### 3.1 Page map

`gui_page` selects the page; `PageId` in `ui_core.h` names pages 1–9. Each page has its own sub-state global.

| Page | `PageId` | User-facing name | Sub-state |
| :--- | :--- | :--- | :--- |
| 0 | — | Clock | — (face from **Display: Clock Face**: Minimal Digital, Modern Roman (Analog), Modern Digital (Bold)) |
| 1 | `HOME` | Home | `home_subpage`: 0 dock, 1 Home settings |
| 2 | `MUSIC` | Music | `music_overlay_mode`: `MusicOverlayMode` 0–6 (now playing, speakers, library categories, item browser, speaker actions, music hub, track inspector) |
| 3 | `GAMES` | Games | `active_game`: 0 launcher, 1 Marbles, 2 Archery, 3 Breakout, 4 Chomp, 5 2048, 6 Memory Match |
| 4 | `SYSTEM` | Settings | `system_menu_mode`: 0 dashboard, 1–12 menus (bare ints) |
| 5 | `WEATHER` | Sky & Weather | `sky_weather_subpage`: 0–3, switched by the mode bar |
| 6 | `AREA` | Area Control | `area_subpage`: 0 entity cards, 1 Area picker; categories switched by the mode bar |
| 7 | `REMOTE` | Remote | `remote_subpage`: 0 remote, 1 control list (master power and up to 10 devices) |
| 8 | `TOOLS` | Tools | `tools_subpage`: `ToolsSubpage` (0 launcher, 1 timers, 2 alarms, 3 stopwatch, 4 dice, 5 flashlight, 6 calculator, 7 level, 8 coin flip, 9 calendar, 10 intercom, 12 cameras) |
| 9 | `WHOLE_HOME` | Whole-Home Hub | `whole_home_tab()`: 0 alarm, 1 thermostat, 2 garage & locks, 3 vacuum, switched by the mode bar |

### 3.2 Home dock vs in-app mode bar

- **Home dock (Page 1):** floating circular app icons, four per window, paged by a horizontal swipe in the dock zone, with the viewport scrollbar when there are more than four apps. The list comes from Latter's dock setting (`collect_home_dock_apps`, up to 8); Remote appears only when a remote is configured, and Settings is always last. The window slides with `home_dock_anim()`.
- **In-app mode bar (pages 5, 6, 9):** a connected stadium-pill track at `app_dock_y`. Tap a slot or swipe sideways in the lower zone; the capsule glides with `mode_bar_anim()`. More than four modes adds conveyor chevrons and the scrollbar.
- Both use the same 210 ms ease-out cubic animator and the same slot geometry (`get_home_dock_window_xs`). *Documented only.*

### 3.3 Return paths

| From | Return | How |
| :--- | :--- | :--- |
| A top-level page (an app's root) | Home (Page 1) | Swipe up from the bottom zone, or tap the home pill. Both exist on every top-level page. |
| A sub-page | Its parent | The "< Back" label at `back_y` (or the page's own Back control) |
| A game | The Games launcher | "Exit" |
| An edit | The page without saving | "< Cancel" (calendar quick-add) |
| Tools opened directly from Home (`tools_direct_home`) | Home | The home pill on timers, alarms, stopwatch and cameras |

The "can go home" condition is computed in three places: `show_top_level_back` in `gui.yaml` (which also hides the pill in Music's art-only view) and `can_swipe_home` in the move and release routers of `touch_handlers.yaml` (which don't). See [N-14](#n-14-one-go-home-predicate).

Labels in use: "< Back", "< BACK" (normalized at draw time), "Exit", "< Exit", "< Cancel", and "Home" (draws the pill). See [N-09](#n-09-back-label-vocabulary-and-position).

### 3.4 Takeover priority

`gui.yaml` draws the first match and returns:

| Priority | Takeover | Condition |
| :--- | :--- | :--- |
| 1 | Firmware update | `ota_in_progress` |
| 2 | Hibernation countdown / shutdown | `is_hibernating` |
| 3 | Panel off (draws nothing, `frame_base_skip()`) | brightness off or `display_power_state == 2` |
| 4 | **Alarm / timer ringing, Find My** | `alarm_ringing`, `timer_ringing`, `find_my_active` |
| 5 | Voice assistant | `va_active` or a status text |
| 6 | Notification / camera snapshot | `notif_active` |
| 7 | Docked screensaver | `display_power_state == 1` |
| 8 | The page | `gui_page` |

After the page, `gui.yaml` draws the chrome in order: the focus overlay (not during takeovers), the home pill (when `show_top_level_back`), and the link-off indicator. Then, if a dock or mode-bar slide is running, it schedules the next animation frame.

The alarm/timer takeover ranks above the voice assistant, and starting an alarm sound stops the voice teardown. *Documented only* (order is by code position in `gui.yaml`).

## 4. Gestures and hit-testing

- **Pair drawing and touch.** Every touch target comes from the same `UiLayout` value or helper as the thing drawn: `hit_slot5`, `hit_area_card3`, `area_slider_pct`, `hit_tool_card3`, `hit_tools_row/col`, `hit_home_*`, the music `hit_*` and `music_scrub_frac`, `hit_map_zoom_*`, `hit_sys_color_swatch`, `is_home_pill_tap`. No raw coordinates in `touch_handlers.yaml`. *Documented only.* As built, some raw coordinates remain in `touch_handlers.yaml` (for example `touch.y <= 72`, `touch.y < 220`, `412`, `SCREEN_W − 64`), and `get_5slot_index` re-implements the 5-slot geometry instead of reading `UiLayout`.
- **Thresholds in millimetres.** Tap slop, swipe distances and edge zones come from `mm_to_px` through `UiLayout` ([1.6](#16-touch-constants)). **Enforced** only in part: **`[TOUCH PPI INVARIANT]`** requires every `boards/*.yaml` to define `-DLATTER_PANEL_PPI`. The thresholds themselves are mostly pixels today ([N-02](#n-02-touch-thresholds-pixels-or-millimetres)).
- **Home affordances.** Swipe up from the bottom zone, or tap the pill. Release handler: start at or below `min(H − 54, home_zone_y())`, |dy| ≥ 24 and mostly vertical, or a pill tap with travel under `max(24, tap_slop())`. The move handler commits earlier with start ≥ `H − 54` and dy ≤ −26.
- **Slider vs swipe zones (Page 6).** Above `app_dock_y − dock_r − 14`, a drag that starts on a light, cover or fan card right of its divider scrubs that card's slider and never changes category. Below that line, a horizontal swipe steps the category. A swipe on the cards that isn't a slider drag (|dx| ≥ 34) also steps the category. The boundary is a raw `− 14` written inline at four sites ([N-03](#n-03-slider-and-mode-bar-boundary)).
- **Shared zones** (`ui_core.h`): `is_back_button` (bottom band), `is_right_chevron_zone` (right edge column), with hand-placed values on round panels.
- **Touch flow.** `TouchRouter` (`ui_touch_router.h`) is a single-touch pass-through: `on_touch`, `on_update` and `on_release` forward the first touch point to the `latter_touch_down_router`, `latter_touch_move_router` and `latter_touch_up_router` scripts in `touch_handlers.yaml`, which classify taps and swipes by page. A gesture is cancelled if the page context changes between press and release.
- **Non-touch input.** `ui_input_event(action, delta)` takes `home`, `back`, `select`, `next`, `prev`. The focus ring shows for 8 s after the last input, and always on `LATTER_HAS_TOUCH=0` boards. The Home-dock focus ring uses its own stride (56 / `scale_x(64)`), so it doesn't line up with the dock icons ([fix list](#fixes-that-need-no-decision)).
- **Games** use IMU tilt on `LATTER_HAS_IMU=1` boards (touch-drag as a fallback) and touch steering on `LATTER_HAS_IMU=0` boards.

## 5. Rendering pipeline

### 5.1 Frame lifecycle (the `display:` lambda in `gui.yaml`)

1. `FrameProfileScope` starts: waits for the between-frame DMA clear, timestamps the frame.
2. **Watermark latch:** draw `(0, 0)` and `(W − 1, H − 1)` with `Color(8, 8, 8)`, then `col_bg`, so the dirty window covers the whole panel and the driver sends one full-frame transfer. As built this runs on **every** panel, round and rectangular ([N-04](#n-04-corner-latch-wording)). These four `it.draw_pixel_at` calls are the only raw primitive calls the GUI needs.
3. `bind_frame_buffer(disp1, it)` binds the software framebuffer for direct scanline writes; `frame_base_begin()` resets the base state.
4. Takeovers in priority order ([3.4](#34-takeover-priority)), each starting with `frame_base(it, col_bg)`.
5. The page. Pages other than Home and Music get `frame_base(it, col_bg)` from `gui.yaml`; Home paints its own base (the daytime sky on rectangular panels), and Music's full-screen art claims the base with `frame_base_claim()`.
6. Chrome: focus overlay, home pill, link-off indicator.
7. `FrameProfileScope` ends: logs a frame that never painted a base, records timing, and starts the next DMA clear.

### 5.2 Rules

| Rule | Status |
| :--- | :--- |
| `auto_clear_enabled: false`; every frame paints its background exactly once through `frame_base()`, or marks itself with `frame_base_claim()` (full-screen art) or `frame_base_skip()` (panel off) | **Enforced: `[FRAME BASE]`** (the two strings exist in `gui.yaml`) and at runtime by `FrameProfileScope`'s warning |
| Draw only with the `fast_*` primitives, never ESPHome's `it.*` primitives (each costs a virtual `draw_pixel_at` per pixel) | **Enforced: `[FAST PRIMITIVES]`** for `include/ui_*.h` (16 fallback and probe sites allowed in `ui_core.h`). Not checked in `guis/**/*.yaml`: the OTA and hibernate overlays in `gui.yaml` still call `it.print` and `it.line` ([fix list](#fixes-that-need-no-decision)). |
| Row-major: loop `y` outside and `x` inside (`render_screensaver_aurora`, `render_album_art_canvas` and every scanline renderer) | *Documented only* |
| No column-major `it.image()`, no `it.draw_pixels_at()` (rejects little-endian buffers on `MipiSpi`, bypasses the buffer on `MipiDsi`); images go through `blit_rgb565_scanline` | **Enforced: `[UI INVARIANT]`** in `include/*.h` |
| Full 16-bit RGB565: no lossy bitmasks (`0xC718`, `0xE79C`), no skipped scanlines | **Enforced: `[UI INVARIANT]`** (masks); skipped scanlines *documented only* |
| Dimming keeps every channel: `dim_rgb565_pair_50pct` / `_25pct` (two pixels per 32-bit operation) and `scale_rgb565_q3` eighths (37.5–87.5 %) through `compute_art_row_scale_q3` | *Documented only* ([N-06](#n-06-dimming-tiers)) |
| IPS panels (`LATTER_PANEL_IPS=1`): no 1-px alternating lit/black row or column patterns; they flicker with line/dot inversion | *Documented only* |
| Round clipping uses `circle_half_span(y)` / `chord_half_span_int` from the chord table; on rectangular panels never pass `x` into `circle_half_span` to get vertical bounds | *Documented only* |
| No `sinf`, `cosf` or `sqrtf` in per-pixel loops: `fast_sin`/`fast_cos`/`fast_sincos` (60-entry LUT) and the chord table | *Documented only* |
| Redraws go through `schedule_disp_update` / `schedule_anim_disp_update`, never `disp1.update()`. Both accept `DispT *` and `DispT *const *`, so the same call compiles in YAML action lambdas and in the `display:` lambda. | *Documented only* |

### 5.3 Redraw scheduling (as built)

- `schedule_disp_update`: a 0 ms one-shot under one scheduler ID. Requests made in the same loop pass collapse into one redraw, which runs after the current work returns.
- `schedule_anim_disp_update`: the next frame 18 ms (P4) or 26 ms (S3) after the last frame started, under the same ID, so it replaces a pending event redraw.
- Interval-driven animation (Tools, games) keeps at least 42 ms between frames; ambient visuals use `should_step_ambient_anim` ([1.5](#15-motion)).
- After each frame, the framebuffer is cleared to that frame's base color by hardware while the CPU is free (the PPA solid fill on the P4, a GDMA copy on the S3). The next frame skips its CPU fill when its base color matches. Any failure falls back to the CPU fill.

## 6. Per-board adaptation

| | Pocket | Mobile | Wrist | Remote | Desk |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Board file | `waveshare_s3_175c` | `waveshare_s3_216` | `waveshare_s3_206` | `waveshare_p4_35` | `waveshare_p4_86_panel` |
| Resolution | 466 × 466 | 480 × 480 | 410 × 502 | 320 × 480 | 720 × 720 |
| Shape | Round | Square | Rounded rectangle | Rectangle | Square |
| Panel | AMOLED | AMOLED | AMOLED | IPS | IPS |
| `LATTER_PANEL_PPI` | 266 | 222 ([N-10](#n-10-mobile-panel-ppi)) | 315 | 165 | 254 |
| `LATTER_CORNER_R` | 233 | 20 | 98 (wearable) | 20 | 20 |
| Width bucket | 380–520 | 380–520 | 380–520 | < 380 | > 520 |
| `scale_*` | identity | identity | identity | identity | × 720/466 |
| Fonts | defaults | defaults | defaults | defaults | Desk set |
| IMU / battery / wearable | yes / yes / no | yes / yes / no | yes / yes / yes | no / yes / no | no / no / no |

**How a value adapts (as built):**

1. `scale_x`/`scale_y`/`scale_s` return the 466-grid value unchanged on panels up to 520 px and scale it proportionally above 520 px. `scale_min` always scales. So on the 320 × 480 and 410 × 502 panels every 466-grid literal is used as-is.
2. On top of that, `UiLayout` and the components branch on three **width buckets**: below 380 px, 380–520 px, and above 520 px.
3. `LATTER_ROUND_DISPLAY` selects hand-placed round coordinates (for example the dock at x = 112, 193, 274, 355 and the OTA ring at r = 220–228).
4. `LATTER_IS_WEARABLE` moves the header, battery and corner radius clear of the watch's rounded corners.

PPI is the panel's diagonal in pixels divided by its diagonal in inches (for a round panel, the diameter). Four boards follow this; Mobile's value is open ([N-10](#n-10-mobile-panel-ppi)). Adding a board: see [Porting hardware](porting-hardware.md); the board facts checker (`tools/check_boards.py`) cross-checks the `LATTER_*` flags, including the PPI, against `boards/facts/<board>.json`.

## 7. Content and terminology

- Use Home Assistant terms: **System**, **Sites**, **Areas**, **Devices**. Never "household", "fleet" or "room". **Enforced: `[TERMINOLOGY]`** in `include/*.h` code lines; not yet applied to strings in `guis/` or `modules/` YAML.
- No product or board names in UI logic (**`[TRAITS NOT NAMES]`**), and no third-party brand names as option labels ([N-12](#n-12-voice-overlay-style-names)).
- Unknown values show `—`; no demo defaults (**`[UNKNOWN GLYPH INVARIANT]`**).
- Fixed option labels are part of the product: clock faces (Minimal Digital, Modern Roman (Analog), Modern Digital (Bold)), games (Marbles, Archery, Breakout, Chomp, 2048, Memory Match), and **Sleep & Wake: Power Profile** names. The legacy power-profile names are banned (**`[POWER PROFILE INVARIANT]`**).
- Casing (as built): page headers and overlay titles in UPPERCASE ("HOME SETTINGS", "FIRMWARE UPDATE"); status lines and empty states in sentence case with an ellipsis for progress ("Preparing flash..."). *Documented only.*
- Back labels: see [N-09](#n-09-back-label-vocabulary-and-position).

## 8. Performance

- **Measure** with **System: Telemetry Profiler**: `[PROF:FRAME]` logs render time when the page changes or a frame is slow; `[PROF:FRAME_SUM]` gives frames, fps, average and maximum every 5 s, plus the DMA-clear hit rate; `[PROF:SECT]` splits time by section (sky, celestial, clouds, precipitation, Home text, dock, base, art).
- **Slow-frame flag (as built):** `[SLOW_FRAME]` at 65 ms, or 110 ms on panels above 300,000 pixels (`kSlowFrameUs`, `ui_core.h`). Earlier documents give a 22 ms budget; the two disagree ([N-01](#n-01-frame-budget)).
- **Cadence:** animation frames 18 ms (P4) / 26 ms (S3); interval animation ≥ 42 ms; ambient 42 / 75 / 120 ms ([1.5](#15-motion)).
- **Costs to avoid:** a full-screen fill (about 10 ms on the S3, where PSRAM writes cost about 45 ns per pixel; `frame_base()` and the DMA clear exist to avoid a second one), per-pixel virtual calls (`it.*`), partial display buffers (the driver re-runs the lambda per chunk), and floating-point trig in pixel loops.

## 9. Accessibility and legibility

No minimums are defined yet ([N-11](#n-11-accessibility-minimums)). The facts as built:

| | Pocket | Mobile (222 / 314 ppi) | Wrist | Remote | Desk |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Smallest font (`font_micro`) | 1.3 mm | 1.6 / 1.1 mm | 1.1 mm | 2.2 mm | 2.0 mm |
| Dock icon, drawn (2 × `dock_r`) | 5.2 mm | 6.2 / 4.4 mm | 4.4 mm | 7.4 mm | 8.0 mm |
| Dock icon, hit (2 × (`dock_r` + 16)) | 8.2 mm | 9.8 / 7.0 mm | 6.9 mm | 12.3 mm | 11.2 mm |

- Text colors below 4.5:1 on black: `col_gray` (3.4:1), `col_red` (3.5:1), `col_green` and `col_cyan` (3.9:1), `col_magenta` (3.4:1), `col_purple` (3.0:1).
- Non-touch devices keep the focus ring visible permanently.

## 10. Decision log

A standing decision stays in force until a new entry supersedes it. Changing one requires a new entry that names the entry it supersedes, explains why, and has the project maintainer's approval. Entries are never edited after they're accepted, except to set their status to "Superseded by D-xxx".

### D-001: Traits, not names
- **Date:** 2026-11-07
- **Decision:** UI code branches on `LATTER_*` capability flags and screen geometry, never on product or board names.
- **Why:** One GUI runs on every board, including boards that don't exist yet.
- **Status:** Accepted.

### D-002: Single-source geometry
- **Date:** 2026-11-07
- **Decision:** Drawing coordinates and touch hit-tests come from the same `UiLayout` value or `scale_*` helper. Touch thresholds are in millimetres via `LATTER_PANEL_PPI`.
- **Why:** Raw pixels in touch handlers are the main cause of layouts that work on one panel and break on another.
- **Status:** Accepted. The code doesn't fully follow it yet ([N-02](#n-02-touch-thresholds-pixels-or-millimetres), [N-03](#n-03-slider-and-mode-bar-boundary)).

### D-003: Home dock vs in-app mode bar
- **Date:** 2026-11-07
- **Decision:** Page 1 uses floating circular app icons; multi-mode apps (Sky & Weather, Area Control, Whole-Home Hub) use a connected stadium-pill mode bar at the same height. Both animate with the 210 ms ease-out cubic `DockSlideAnimator` and show the proportional scrollbar above 4 items. Both Home affordances (swipe up, tap the pill) exist on every top-level page.
- **Why:** Launching an app and switching a view inside it are different actions and look different.
- **Status:** Accepted.

### D-004: Row-major, full-fidelity RGB565
- **Date:** 2026-11-07
- **Decision:** Every renderer loops `y` outside and `x` inside, writes through `fast_*` primitives and `blit_rgb565_scanline`, keeps all 16 bits (dimming shifts or scales each channel; no lossy masks, no skipped scanlines), and never uses `it.image()` or `it.draw_pixels_at()`.
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

---

## Decisions needed

These are places where the code and an earlier rule disagree, or where the code makes a choice no document records. Each needs the maintainer's decision; the accepted outcome becomes a D-entry above, and the code or docs are changed to match.

### N-01: Frame budget
- **Conflict:** Earlier docs say frames must stay under 22 ms. The code flags `[SLOW_FRAME]` only at 65 ms (110 ms on the 720 × 720 panel).
- **Options:** (a) Flag at 22 ms everywhere. (b) Keep 65/110 ms and drop the 22 ms claim. (c) Two numbers: a target and an alarm.
- **Recommendation:** (c). Target: the chip's animation frame (26 ms S3, 18 ms P4) for animated surfaces (dock and mode-bar slides, games, ambient), and 42 ms for everything else. Alarm: keep 65/110 ms as `[SLOW_FRAME]` until a profiler walk on each board shows the targets are met, then tighten it. 22 ms is below the S3's animation frame and below what a full-screen QSPI frame allows.

### N-02: Touch thresholds, pixels or millimetres
- **Conflict:** D-002 says thresholds are millimetres. As built, taps use `max_travel < 30`, swipes 18–34 px, slider drags 6 px, and two sites use `scale_s(...)` thresholds; the mm constants are used once each.
- **Options:** (a) Move every threshold into `UiLayout` as mm constants. (b) Allow pixels and drop the mm rule.
- **Recommendation:** (a). Add a small set of named mm constants (tap travel, list swipe, page swipe, drag start, edge zone), calibrated so the Pocket (the 466 reference) behaves exactly as today: 30 px → 2.9 mm, 26 → 2.5 mm, 22 → 2.1 mm, 6 → 0.6 mm. Fix N-10 first, since it changes Mobile's conversion. Add a latter-verify ratchet that forbids numeric thresholds in `touch_handlers.yaml`.

### N-03: Slider and mode-bar boundary
- **Conflict:** The rule says the Area Control slider zone ends at `app_dock_y − dock_r − scale_s(14)`, derived in `UiLayout`. As built it's `− 14` raw, inline at four sites in `touch_handlers.yaml`.
- **Options:** (a) A `UiLayout::mode_bar_zone_top()` using `scale_s(14)`. (b) The same helper in mm.
- **Recommendation:** (a): no change on the four boards up to 520 px, 22 px on the Desk; then fold it into N-02's mm set if that lands.

### N-04: Corner latch wording
- **Conflict:** The rule says the corner latch runs on non-round panels. The code runs it on every panel, and its comment says that's deliberate (the round panel's `MipiSpiBuffer` needs it too).
- **Recommendation:** Follow the code: the latch runs on every panel. Update the rule.

### N-05: Page 9 naming
- **Conflict:** Page 9 is the Whole-Home Hub (`PageId::WHOLE_HOME`, four tabs), but its renderer is `render_security_alarm_page` and `gui.yaml` labels it "DEDICATED HOME SECURITY ALARM APP".
- **Recommendation:** Rename the renderer to `render_whole_home_page` and fix the comment. No behavior change.

### N-06: Dimming tiers
- **Conflict:** The rule names only the 50 % and 25 % dimmers. The code also uses eighths (`scale_rgb565_q3`, 37.5–87.5 %) for art scrims.
- **Recommendation:** Make the eighths the documented dim scale, with 50 % and 25 % as the fast paths. The rule that matters is "per-channel, no lossy masks", which both follow.

### N-07: One motion system
- **Conflict:** Dock and mode-bar slides are time-based (210 ms ease-out cubic). List paging is frame-stepped (two frames 16 ms apart from fixed tables), and its comment says the first step is 35 % when the tables give 54 %.
- **Options:** (a) Generalize `DockSlideAnimator` into one tween and drive list paging with it. (b) Keep both and document them.
- **Recommendation:** (a). Until then, fix the comment.

### N-08: Pager idioms
- **Conflict:** Three pagers exist (viewport scrollbar, page pips, vertical chevrons), and only the scrollbar was documented.
- **Recommendation:** Keep all three, with fixed roles: scrollbar under a horizontal window of more than 4 items (dock, mode bar); pips for paged carousels (Games launcher, cameras); chevrons for paged vertical lists (5-slot lists, Tools launcher). Recorded in [section 2](#2-components).

### N-09: Back-label vocabulary and position
- **Conflict:** Labels in use are "< Back", "< BACK", "Exit", "< Exit", "< Cancel" and "Home". 17 callers pass a literal `426` instead of `layout().back_y` (the function re-derives the rectangular position from it).
- **Recommendation:** "< Back" returns to the parent; the home pill (not a label) returns Home from a top-level page; "Exit" only ends a game session; "< Cancel" only abandons an edit. Remove the "< BACK" and "< Exit" literals, make `back_y` the default argument, and add a check that no caller passes `426`.

### N-10: Mobile panel PPI
- **Conflict:** Mobile's `LATTER_PANEL_PPI` is 222, which equals 480 ÷ 2.16 (width over diagonal). The other four boards use diagonal pixels over diagonal inches; for a 2.16" 480 × 480 panel that's about 314.
- **Effect:** If the panel's diagonal is 2.16", every mm constant on Mobile is about 30 % smaller than intended (tap slop 18 px instead of 26).
- **Recommendation:** Check the panel datasheet's active area. If 2.16" is the diagonal, set 314 in the board file and the facts file together (the facts checker compares them), and hardware-test touch on Mobile.

### N-11: Accessibility minimums
- **Gap:** No minimum target size, text size or contrast is defined.
- **Recommendation:**
  - Touch targets: hit area at least 7 mm across (it may extend beyond the drawn element). Wrist dock icons are 6.9 mm today.
  - Text: primary text at least 2.0 mm font size, captions at least 1.3 mm. That means per-board font substitutions for the Wrist (and Mobile if N-10 changes its PPI).
  - Contrast: text at least 4.5:1 on its background, large text (`font_date` and up) and status glyphs at least 3:1. `col_gray`, `col_red`, `col_green`, `col_cyan`, `col_magenta` and `col_purple` fall below 4.5:1 and would be limited to large text, fills and icons.
  - Keep the permanent focus ring on non-touch devices.

### N-12: Voice overlay style names
- **Conflict:** **Voice: Visual Profile** offers "Jarvis", "Alexa" and "Gemini", which are third-party product names, in a public product.
- **Recommendation:** Rename them to descriptive names (for example "Rings", "Light Bar", "Dot Wave"), keep Nebula and Solaris, and update `docs/using-your-device.md`. A stored option that no longer exists falls back to the default (Nebula); no alias. The wake-word models ("Hey Jarvis", "Alexa") are upstream model names and stay as they are.

### N-13: Palette tokens and literals
- **Gap:** The shared chrome uses fixed RGB literals, there are 438 color literals across the UI headers, two palette entries duplicate others, and the two electric accents aren't covered by the "muted" intent.
- **Recommendation:** Add named chrome constants in `ui_core.h` (pill, track, track border, divider, back label, glass tint) and use them in the components. Remove `col_silver` and `col_light_blue`. Document the two accents as motion-only (voice overlay, kinetic visuals). Add a latter-verify ratchet so the literal count per header can only go down.

### N-14: One go-home predicate
- **Conflict:** "Can go home" is computed three times. `gui.yaml` hides the home pill in Music's art-only view, but both touch routers still let swipe-up exit there. The move and release routers also use different thresholds (start `H − 54`, dy −26 vs `min(H − 54, home_zone_y())`, dy −24).
- **Options:** (a) In art-only view, swipe-up works and the pill is hidden (the gesture is always available; the pill is chrome). (b) Swipe-up is disabled while the pill is hidden.
- **Recommendation:** One helper, `latter_ui::can_go_home(...)`, used by all three sites, and one threshold pair from `UiLayout`. Option (a), written explicitly as `show_home_pill = can_go_home && !music_clean_art`.

### N-15: Debounce pattern
- **Conflict:** Handlers debounce through `touch_router().allow_tap(...)` and through their own `static last_*` timestamps.
- **Recommendation:** One pattern: `touch_router().allow_tap(now, TOUCH_DEBOUNCE_*)`, with the table in [1.6](#16-touch-constants) as the only constants; move the 200 ms post-swipe lockout into the table.

### N-16: Ratify the reference grid and width buckets
- **Gap:** The 466 px design grid, identity scaling up to 520 px (so smaller panels use the 466 values unscaled), the three width buckets, and pixel-sized fonts with one shared set for panels up to 520 px are implicit.
- **Recommendation:** Ratify the grid, the scaling policy and the buckets as D-entries as they stand. Revisit pixel-sized fonts under N-11.

### N-17: Shared header, empty-state and feedback components
- **Gap:** Each page inlines its header, empty and loading text, and feedback line; sub-header styling drifts (one page uses `font_micro`, one uses `col_silver`, some headers have a drop shadow).
- **Recommendation:** Ratify the header convention in [1.2](#12-typography) and build `render_page_header`, `render_empty_state` and `render_feedback_line`, then migrate the pages.

## Fixes that need no decision

These break a rule that already stands; they're bugs to fix, not choices.

- `gui.yaml`'s OTA and hibernate overlays call `it.print` and `it.line` (breaks D-004/`[FAST PRIMITIVES]`); use `fast_print` and `fast_line`, and extend `[FAST PRIMITIVES]` to `guis/**/*.yaml`, allowing only the four latch calls.
- The Home-dock focus ring uses stride 56 / `scale_x(64)`, but the dock icons sit at round x = 112, 193, 274, 355 (stride 81) or stride 74 / `scale_x(98)`; read the positions from `get_home_dock_window_xs`.
- `render_back_button` computes the home pill at `H − scale_y(12)`, while `UiLayout::home_bar_y` is `H − scale_y(11)`; use `layout().home_bar_y`.
- `get_5slot_index` re-implements the 5-slot geometry; read it from `UiLayout`.
- Stale comments: `ui_core.h` ("r=38px" watch corners), `gui.yaml` ("y = 434 for all top-level apps"), `touch_handlers.yaml` ("start_y >= 412"), `scripts_actions.yaml` ("35% eased frame"), and the ambient pacer's "20 FPS / 13.3 FPS" note.
- `examples/custom_display_template.yaml` doesn't define `LATTER_PANEL_PPI`, so a board copied from it falls back to the 266 default; add it (and extend `[TOUCH PPI INVARIANT]` to `examples/*.yaml`).
