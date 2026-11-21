#pragma once

// ==============================================================================
// Latter Reusable On-Device Widget, Chrome & Vector Glyph Library
// (include/ui_widgets.h)
// Unified headers, frosted-glass cards (`render_modern_card` center vs
// `render_card_box` top-left), stadium switches, pills, 4-column steppers
// (canonical 68 px stride), LUT-backed HSV color wheel, confirmation modals,
// 3-tier security PIN pad modal, game-over sheets, and canonical vector glyphs.
// Every interactive widget co-registers its drawn bounds in frame_hits().
// ==============================================================================

#include "ui_gfx.h"
#include "ui_hit_table.h"
#include "ui_layout.h"

namespace latter_ui {

// ------------------------------------------------------------------------------
// 1. Shared Frosted-Glass Chrome, Headers, Mode Bars & Navigation Primitives
// ------------------------------------------------------------------------------
void render_pill_button(esphome::display::Display &it, int x1, int x2, int y_mid, int r,
                        esphome::Color border_c, esphome::Color bg_c,
                        const char *label, esphome::Color text_c,
                        esphome::display::BaseFont *font,
                        GlassVariant variant = GlassVariant::Clear);

// Center-origin 2-tier smoked-glass card (cx_card, cy_card), returning resolved surface color.
esphome::Color render_modern_card(esphome::display::Display &it, int cx_card, int cy_card,
                                  int w, int h, int r,
                                  esphome::Color bg_c, esphome::Color border_c,
                                  const char *label, esphome::Color text_c,
                                  esphome::display::BaseFont *font,
                                  GlassVariant variant = GlassVariant::Clear,
                                  bool sample_surface = false);

// Top-left-origin 2-tier smoked-glass card box (x0, y0, w, h), returning resolved surface color.
esphome::Color render_card_box(esphome::display::Display &it, int x0, int y0, int w, int h,
                               esphome::Color bg_c, esphome::Color border_c, int r = 10,
                               GlassVariant variant = GlassVariant::Clear,
                               esphome::Color ref_text = kChromeGlassSheen,
                               bool sample_surface = false);

esphome::Color render_glass_circle(esphome::display::Display &it, int cx_btn, int cy_btn, int r,
                                   esphome::Color bg_c, esphome::Color border_c,
                                   int inner_hole_r = 0,
                                   GlassVariant variant = GlassVariant::Clear,
                                   esphome::Color ref_text = kChromeGlassSheen,
                                   bool sample_surface = false);

inline esphome::Color render_glass_circle(esphome::display::Display &it, int cx_btn, int cy_btn, int r,
                                          esphome::Color bg_c, esphome::Color border_c,
                                          GlassVariant variant,
                                          esphome::Color ref_text = kChromeGlassSheen) {
  return render_glass_circle(it, cx_btn, cy_btn, r, bg_c, border_c, 0, variant, ref_text);
}

inline esphome::Color render_glass_ring(esphome::display::Display &it, int cx_btn, int cy_btn,
                                        int outer_r, int inner_hole_r,
                                        esphome::Color bg_c, esphome::Color border_c,
                                        GlassVariant variant = GlassVariant::Clear,
                                        esphome::Color ref_text = kChromeGlassSheen) {
  return render_glass_circle(it, cx_btn, cy_btn, outer_r, bg_c, border_c, inner_hole_r, variant, ref_text);
}

void render_scroll_chevron(esphome::display::Display &it, int x, int y, bool is_up,
                           esphome::Color c, esphome::Color shadow_c, bool shadow = false);

void render_bezel_alert_border(esphome::display::Display &it, int cx, int cy, esphome::Color ring_c);

void render_home_indicator_bar(esphome::display::Display &it, int cx, int y_pos = kHomeBarY,
                               esphome::Color bar_col = kChromeHomePill);

void render_viewport_scrollbar(esphome::display::Display &it, int cx, int y_pos, int total_items,
                               float cur_start_idx, esphome::Color col_thumb, bool is_dim = false);

void render_dock_page_pips(esphome::display::Display &it, int cx, int y_pos, int total_pages,
                           int active_page, esphome::Color col_active, esphome::Color col_inactive);

void render_vertical_page_pips(esphome::display::Display &it, bool show_up, bool show_down,
                               esphome::Color col_bg, esphome::Color col_active,
                               esphome::Color col_inactive, int bottom_limit_y = SCREEN_H);

// Back affordance (docs/device-ui-design.md): callers pass layout().back_y ([BACK BUTTON Y]).
void render_back_button(esphome::display::Display &it, int cx, esphome::display::BaseFont *font,
                        int y_pos = kBackLabelY, const char *label = "< Back",
                        esphome::Color text_col = kChromeBackLabel);

void render_page_header(esphome::display::Display &it, int cx, const char *title,
                        esphome::display::BaseFont *font,
                        esphome::Color color = esphome::Color::WHITE, bool shadow = true);
void render_page_header(esphome::display::Display &it, int cx, const std::string &title,
                        esphome::display::BaseFont *font,
                        esphome::Color color = esphome::Color::WHITE, bool shadow = true);

void render_sub_header(esphome::display::Display &it, int cx, const char *text,
                       esphome::display::BaseFont *font, esphome::Color color);
void render_sub_header(esphome::display::Display &it, int cx, const std::string &text,
                       esphome::display::BaseFont *font, esphome::Color color);

void render_empty_state(esphome::display::Display &it, int cx, int cy, const char *text,
                        esphome::display::BaseFont *font, esphome::Color text_c);

void render_time_readout(esphome::display::Display &it, int cx, const char *text,
                         esphome::display::BaseFont *font_time, esphome::Color color,
                         int y_pos = scale_y(202));

bool offline_pill_due(uint32_t now_ms);
void render_offline_pill(esphome::display::Display &it, int cx, uint32_t now_ms,
                         esphome::display::BaseFont *font, esphome::Color text_c);

void render_in_app_mode_bar_track(esphome::display::Display &it, int cx, const int *win_xs,
                                  int vis_count, int active_vis_idx, bool has_left_arrow,
                                  bool has_right_arrow, int total_count, int active_global_idx,
                                  esphome::Color theme_primary, esphome::Color col_white,
                                  int explicit_win_start = -1);

void render_progress_arc_r224(esphome::display::Display &it, int cx, int cy, float prog,
                              esphome::Color col);

namespace ui {

// ------------------------------------------------------------------------------
// 2. Semantic Chrome Surface Tokens (defined in components/latter_ui/ui_widgets.cpp)
// ------------------------------------------------------------------------------
esphome::Color chrome_card_bg_on();
esphome::Color chrome_card_bg_off();
esphome::Color chrome_card_brd_on();
esphome::Color chrome_card_brd_off();
esphome::Color chrome_danger_bg();
esphome::Color chrome_modal_bg();
esphome::Color chrome_key_bg();
esphome::Color chrome_key_brd();
esphome::Color chrome_wheel_inner_ring();
esphome::Color chrome_wheel_outer_ring();

// ------------------------------------------------------------------------------
// 3. Canonical Solid + Negative-Space Custom Glyph Library (D-008 / D-051)
//    Every glyph takes an explicit container-resolved pixel half-size `r`
//    (zero internal `scale_s` double-scaling) and renders row-major AA solid
//    silhouettes with true transparent negative-space cutouts.
// ------------------------------------------------------------------------------
inline constexpr int dock_icon_r(int dock_r = layout().dock_r) {
  return dock_icon_r_for(dock_r);
}
inline constexpr int card_icon_r(int card_h) {
  return card_icon_r_for(card_h);
}
inline constexpr int hero_icon_r(int hero_h) {
  return hero_icon_r_for(hero_h, SCREEN_MIN_DIM);
}

void draw_icon_power(esphome::display::Display &it, int cx, int cy, int r,
                     esphome::Color col, bool shadow = false);
void draw_icon_clock_solid(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow = false);
void draw_icon_hourglass(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow = false);
void draw_icon_alarm_clock(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow = false);
void draw_icon_stopwatch(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow = false);
void draw_icon_shield_home(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow = false);
void draw_icon_camera(esphome::display::Display &it, int cx, int cy, int r,
                      esphome::Color col, bool shadow = false);
void draw_icon_speaker(esphome::display::Display &it, int cx, int cy, int r,
                       esphome::Color col, bool waves = true, bool shadow = false,
                       bool muted = false);
void draw_icon_thermometer(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow = false);
void draw_icon_lock(esphome::display::Display &it, int cx, int cy, int r,
                    esphome::Color col, bool is_locked = true, bool shadow = false);
void draw_icon_garage_door(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool is_open = false, bool shadow = false);
void draw_icon_robot_vacuum(esphome::display::Display &it, int cx, int cy, int r,
                            esphome::Color col, bool shadow = false);
void draw_icon_eighth_notes(esphome::display::Display &it, int cx, int cy, int r,
                            esphome::Color col, bool shadow = false);
void draw_icon_calendar(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow = false);
void draw_icon_cal_agenda(esphome::display::Display &it, int cx, int cy, int r,
                          esphome::Color col, bool shadow = false);
void draw_icon_cal_day(esphome::display::Display &it, int cx, int cy, int r,
                       esphome::Color col, bool shadow = false);
void draw_icon_cal_week(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow = false);
void draw_icon_mic(esphome::display::Display &it, int cx, int cy, int r,
                   esphome::Color col, bool shadow = false);
void draw_icon_lightbulb(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow = false);
void draw_icon_ceiling_fan(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow = false);
void draw_icon_curtains(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow = false);
void draw_icon_blinds_slats(esphome::display::Display &it, int cx, int cy, int r,
                            esphome::Color col, bool is_open = false, bool shadow = false);
void draw_icon_switch_pill(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool is_on = true, bool shadow = false);
void draw_icon_sparkle(esphome::display::Display &it, int cx, int cy, int r,
                       esphome::Color col, bool shadow = false);
void draw_icon_bento_grid(esphome::display::Display &it, int cx, int cy, int r,
                          esphome::Color col, bool shadow = false);
void draw_icon_droplet(esphome::display::Display &it, int cx, int cy, int r,
                       esphome::Color col, bool shadow = false);
void draw_icon_intercom_handset(esphome::display::Display &it, int cx, int cy, int r,
                                esphome::Color col, bool shadow = false);
void draw_icon_bell(esphome::display::Display &it, int cx, int cy, int r,
                    esphome::Color col, bool shadow = false);
void draw_icon_thumbtack(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow = false);
void draw_icon_play_pause(esphome::display::Display &it, int cx, int cy, int r,
                          esphome::Color col, bool shadow = false);
void draw_icon_hamburger(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow = false);
void draw_icon_grid_2x2(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow = false);
void draw_icon_carousel(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow = false);
void draw_icon_gear(esphome::display::Display &it, int cx, int cy, int r,
                    esphome::Color col, bool shadow = false);
void draw_icon_chevron(esphome::display::Display &it, int cx, int cy, int r,
                       bool is_right, esphome::Color col, bool shadow = false);
void draw_icon_plus(esphome::display::Display &it, int cx, int cy, int r,
                    esphome::Color col, bool shadow = false);
void draw_icon_minus(esphome::display::Display &it, int cx, int cy, int r,
                     esphome::Color col, bool shadow = false);
void draw_icon_home(esphome::display::Display &it, int cx, int cy, int r,
                    esphome::Color col, bool shadow = false);
void draw_icon_forecast_chart(esphome::display::Display &it, int cx, int cy, int r,
                              esphome::Color col, bool shadow = false);
void draw_icon_sun(esphome::display::Display &it, int cx, int cy, int r,
                   esphome::Color col, bool shadow = false);
void draw_icon_crescent_moon(esphome::display::Display &it, int cx, int cy, int r,
                             esphome::Color col, bool shadow = false);
void draw_icon_wind(esphome::display::Display &it, int cx, int cy, int r,
                    esphome::Color col, bool shadow = false);
void draw_icon_arrow_v(esphome::display::Display &it, int cx, int cy, int r,
                       bool is_up, esphome::Color col, bool shadow = false);
void draw_route_icon(esphome::display::Display &it, RouteId route, int cx, int cy, int r,
                     esphome::Color col, esphome::Color cut_c, bool shadow = false);

// ------------------------------------------------------------------------------
// 4. Canonical Text, Stack Width & Functional Component Library (D-070)
//    Role-driven widgets with single-source radii (kRadiusCardPx / kRadiusKeyPx / h/2),
//    outer-box geometry, strict button-only glass, and co-located FrameHitTable bounds.
// ------------------------------------------------------------------------------
void shadow_print(esphome::display::Display &it, int x, int y,
                  esphome::display::BaseFont *font, esphome::Color col,
                  esphome::display::TextAlign align, const char *text,
                  bool double_shadow = false);

int wrap_text_lines(const std::string &text, int max_w, esphome::display::BaseFont *font,
                    std::string *out_lines, int max_lines);

inline constexpr int compute_uniform_stack_w(int top_cy, int bot_cy, int row_h,
                                             int max_w = kMaxContentColW, int inset = 14) {
  return compute_uniform_stack_w_for(
      top_cy, bot_cy, row_h, max_w, SCREEN_W, SCREEN_H,
      LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, inset);
}

// Unified in-app stadium mode bar (pages 2, 5, 6, 9, 10, 12, 13):
// Draws track, gliding active capsule, conveyor chevrons, viewport scrollbar,
// hit targets, and per-slot icons in one canonical path.
using ModeBarDrawIconFn = void (*)(esphome::display::Display &it, int global_idx,
                                   int bx, int by, int icon_r,
                                   esphome::Color fg_col, bool shadow, void *user_ctx);

void mode_bar(esphome::display::Display &it, int cx,
              int total_count, int active_global_idx, int win_start,
              esphome::Color theme_primary, esphome::Color col_white,
              ModeBarDrawIconFn draw_icon_fn, void *user_ctx = nullptr);

// Multi-item header pager bar (D-048 / D-070):
// When show_arrows == false: renders standard bare shadowed header (zero glass).
// When show_arrows == true: renders [‹] glass circle + bare shadowed title + [›] glass circle.
void header_pager_bar(esphome::display::Display &it, int cx, int cy,
                      const std::string &title, esphome::display::BaseFont *font,
                      esphome::Color col_white, bool show_arrows,
                      uint16_t prev_wid = WidgetId::NONE,
                      uint16_t next_wid = WidgetId::NONE,
                      int16_t prev_param = -1, int16_t next_param = 1);

// Outer-box stadium action pill (r = h / 2): for standalone & horizontal-row CTAs only.
void action_pill(esphome::display::Display &it, int x0, int w, int y_mid, int h,
                 esphome::Color border_c, esphome::Color bg_c,
                 const char *label, esphome::Color text_c, esphome::display::BaseFont *font,
                 uint16_t widget_id = WidgetId::NONE,
                 uint8_t flags = kHitTap | kHitFocusSlot, int16_t param = 0,
                 int hit_h_min = -1, const char *leading_mdi = nullptr);

void action_pill_centered(esphome::display::Display &it, int cx, int w, int y_mid, int h,
                          esphome::Color border_c, esphome::Color bg_c,
                          const char *label, esphome::Color text_c,
                          esphome::display::BaseFont *font,
                          uint16_t widget_id = WidgetId::NONE,
                          uint8_t flags = kHitTap | kHitFocusSlot, int16_t param = 0,
                          int hit_h_min = -1, const char *leading_mdi = nullptr);

struct ActionPillSpec {
  const char *label{nullptr};
  uint16_t widget_id{WidgetId::NONE};
  esphome::Color border_c{kChromeCardBrdOn};
  esphome::Color bg_c{kChromeCardBgOff};
  esphome::Color text_c{esphome::Color::WHITE};
  int16_t param{0};
  const char *leading_mdi{nullptr};
};

void action_pill_row(esphome::display::Display &it, int cy, int h,
                     int max_row_w, int max_btn_w, int gap,
                     const ActionPillSpec *items, int count,
                     esphome::display::BaseFont *font, int inset = 14);

// Paired 2-way stadium segmented toggle (Broadcast | Call Display, White Temp | Color Wheel).
void segmented_toggle(esphome::display::Display &it, int cx, int cy, int total_w, int h,
                      const char *left_label, const char *right_label, bool right_active,
                      esphome::display::BaseFont *font,
                      esphome::Color theme_primary, esphome::Color col_white,
                      esphome::Color col_slate,
                      uint16_t left_wid, uint16_t right_wid);

// Squared-off configuration / setting cycle row (kRadiusCardPx, < label >).
void setting_cycle_row(esphome::display::Display &it, int cx, int cy, int w, int h,
                       const char *label, bool enabled,
                       esphome::display::BaseFont *font,
                       esphome::Color col_white, esphome::Color col_slate,
                       esphome::Color chevron_col,
                       uint16_t widget_id = WidgetId::NONE,
                       uint8_t flags = kHitTap | kHitFocusSlot, int16_t param = 0);

// Squared-off horizontal slider card row (kRadiusCardPx, inner tinted fill bar).
void slider_card_row(esphome::display::Display &it, int cx, int cy, int w, int h,
                     const char *label, int pct, int min_pct, int max_pct, bool enabled,
                     esphome::Color fill_c, esphome::Color brd_c,
                     esphome::display::BaseFont *font, esphome::Color text_c,
                     uint16_t widget_id = WidgetId::NONE, int16_t param = 0);

// Squared-off interactive list / menu / action-sheet row (kRadiusCardPx).
// If widget_id == WidgetId::NONE, renders bare passive shadowed text (zero glass per D-048).
void list_row(esphome::display::Display &it, int cx, int cy, int w, int h,
              const char *label, const char *sub_label,
              esphome::Color bg_c, esphome::Color brd_c,
              esphome::Color text_c, esphome::Color sub_c,
              esphome::display::BaseFont *font,
              uint16_t widget_id = WidgetId::NONE,
              uint8_t flags = kHitTap | kHitFocusSlot, int16_t param = 0,
              const char *leading_mdi = nullptr,
              const char *right_badge = nullptr,
              esphome::Color right_badge_c = kChromeBackLabel);

// Squared-off 2x3 launcher / bento tile (kRadiusCardPx).
using TileDrawIconFn = void (*)(esphome::display::Display &it, int cx, int cy,
                                int icon_r, esphome::Color col, bool shadow, void *user_ctx);

// Button-only glass row (D-048 / D-070 / D-073):
// Left: non-interactive bare shadowed optional icon + title + optional status (zero glass).
// Right: single frosted-glass action_pill as the sole interactive control.
void labeled_action_row(esphome::display::Display &it, int cx, int cy, int w, int h,
                        const char *title, const char *status,
                        esphome::Color title_c, esphome::Color status_c,
                        esphome::display::BaseFont *title_font,
                        esphome::display::BaseFont *status_font,
                        const char *cta_label, int cta_w, int cta_h,
                        esphome::Color cta_border_c, esphome::Color cta_bg_c,
                        esphome::Color cta_text_c,
                        esphome::display::BaseFont *cta_font,
                        uint16_t cta_wid, uint8_t cta_flags = kHitTap | kHitFocusSlot,
                        int16_t cta_param = 0, int cta_hit_h_min = -1,
                        TileDrawIconFn draw_icon_fn = nullptr, void *icon_ctx = nullptr,
                        esphome::Color icon_c = esphome::Color::WHITE);

void grid_tile(esphome::display::Display &it, int cx, int cy, int w, int h,
               const char *label, bool active, bool disabled,
               esphome::Color accent_c, esphome::Color text_c,
               esphome::display::BaseFont *font,
               TileDrawIconFn draw_icon_fn, void *user_ctx,
               uint16_t widget_id = WidgetId::NONE, int16_t param = 0);

// Frosted-glass circular icon button with >= 7 mm default hit padding.
void icon_circle_button(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color bg_c, esphome::Color brd_c,
                        esphome::Color icon_c,
                        TileDrawIconFn draw_icon_fn, void *user_ctx,
                        uint16_t widget_id = WidgetId::NONE,
                        uint8_t flags = kHitTap | kHitFocusSlot | kHitFocusCircle,
                        int16_t param = 0, int hit_pad = -1);

// Compact squared-off keypad button (kRadiusKeyPx) for Calculator & PIN Pad.
void keypad_button(esphome::display::Display &it, int cx, int cy, int w, int h,
                   const char *label, esphome::Color bg_c, esphome::Color brd_c,
                   esphome::Color text_c, esphome::display::BaseFont *font,
                   uint16_t widget_id = WidgetId::NONE, int16_t param = 0);

// ------------------------------------------------------------------------------
// 5. Interactive Hit-Registered Widgets & Modals (Legacy/Specialized)
// ------------------------------------------------------------------------------
void toggle_switch(esphome::display::Display &it, int cx_sw, int cy_sw, bool is_on,
                   esphome::Color active_col);

void toggle_card_row(esphome::display::Display &it, int cx, int y_mid, int w, int h,
                     const char *label, bool is_on, esphome::display::BaseFont *font,
                     esphome::Color active_col, esphome::Color text_on, esphome::Color text_off,
                     uint16_t widget_id = WidgetId::NONE, int16_t param = 0);

void pill_button(esphome::display::Display &it, int x1, int x2, int y_mid, int r,
                 esphome::Color border_c, esphome::Color bg_c,
                 const char *label, esphome::Color text_c, esphome::display::BaseFont *font,
                 uint16_t widget_id = WidgetId::NONE, uint8_t flags = kHitTap,
                 int16_t param = 0, GlassVariant variant = GlassVariant::Clear);

// Outer-box pill button: exact visual & hit bounds [x0, x0 + w - 1] x [y_mid - r, y_mid + r].
void pill_button_box(esphome::display::Display &it, int x0, int w, int y_mid, int r,
                     esphome::Color border_c, esphome::Color bg_c,
                     const char *label, esphome::Color text_c, esphome::display::BaseFont *font,
                     uint16_t widget_id = WidgetId::NONE, uint8_t flags = kHitTap,
                     int16_t param = 0, GlassVariant variant = GlassVariant::Clear);

// Canonical 4-column stepper row (80 px reference stride; matches UiLayout::hit_4col_stepper).
void stepper_row(esphome::display::Display &it, int cx, int y_top, int h,
                 const char *const labels[4], esphome::display::BaseFont *font,
                 esphome::Color col_text, uint16_t base_widget_id = WidgetId::STEPPER_BASE);

esphome::Color hsv_wheel_sector_color(int sector_idx_72, float sat);
bool hsv_wheel_pick(int tx, int ty, int cx, int cy, int outer_r_466, int inner_r_466,
                    float &out_hue_deg, float &out_sat);

void hsv_color_wheel(esphome::display::Display &it, int cx, int cy, int outer_r_466, int inner_r_466,
                     float cur_hue_deg, float cur_sat,
                     esphome::Color col_bg, esphome::Color col_white,
                     uint16_t widget_id = WidgetId::COLOR_WHEEL_RING);

void confirm_modal(esphome::display::Display &it, int cx, const char *title,
                   const char *line1, const char *line2,
                   const char *cancel_lbl, const char *confirm_lbl,
                   esphome::Color confirm_border, esphome::Color confirm_bg,
                   esphome::display::BaseFont *font_hdr, esphome::display::BaseFont *font_body,
                   esphome::Color theme_primary, esphome::Color col_white, esphome::Color col_slate,
                   uint16_t cancel_wid = WidgetId::CONFIRM_CANCEL,
                   uint16_t confirm_wid = WidgetId::CONFIRM_ACCEPT);

void game_over_sheet(esphome::display::Display &it, int cx, int cy,
                     const char *title, esphome::Color title_col,
                     const char *score_line, const char *best_line, bool is_new_best,
                     const char *cta_lbl,
                     esphome::display::BaseFont *font_hdr, esphome::display::BaseFont *font_body,
                     esphome::Color theme_primary, esphome::Color col_white,
                     esphome::Color col_gold, esphome::Color col_slate,
                     uint16_t cta_wid = WidgetId::GAME_OVER_CTA);

enum class PinPadTargetKind : uint8_t {
  NONE = 0,
  AREA_ACTION = 1,
  ALARM_ACTION = 2,
  SHORTCUT_ACTION = 3,
};

struct PinPadModalState {
  bool open{false};
  PinPadTargetKind target{PinPadTargetKind::NONE};
  char digits[13]{};
  uint8_t len{0};
  char prompt[40]{};
  char error_msg[32]{};
  uint32_t error_ms{0};
};

PinPadModalState &pin_pad_state();
void pin_pad_scrub();
void pin_pad_open(PinPadTargetKind kind, const char *prompt_label);
bool pin_pad_press_digit(uint8_t digit);
void pin_pad_backspace();
bool is_perimeter_action_blocked(const std::string &policy, const std::string &domain_or_entity,
                                 const std::string &action, int level = 100);
bool requires_perimeter_pin_prompt(const std::string &policy, const std::string &domain_or_entity,
                                   const std::string &action, int level = 100);
bool requires_perimeter_pin_prompt(const std::string &policy, const std::string &domain_or_entity,
                                   const std::string &action, int level, uint16_t alarm_caps);

void pin_pad_modal(esphome::display::Display &it, int cx, int cy, uint32_t now_ms,
                   esphome::display::BaseFont *font_hdr, esphome::display::BaseFont *font_key,
                   esphome::display::BaseFont *font_small,
                   esphome::Color theme_primary, esphome::Color col_white,
                   esphome::Color col_slate, esphome::Color col_red);

// ------------------------------------------------------------------------------
// 4. Shared Sub-Pixel SDF Horology Primitives & Split Action Card Row
// ------------------------------------------------------------------------------
void draw_aa_line(esphome::display::Display &it, float x0, float y0, float x1, float y1,
                  float width, esphome::Color col, int alpha_max = 255);

void draw_aa_disc(esphome::display::Display &it, float cx, float cy, float r,
                  esphome::Color col, int alpha_max = 255);

void draw_aa_ring(esphome::display::Display &it, float cx, float cy, float r,
                  float thick, esphome::Color col, int alpha_max = 255);

void draw_aa_tapered_hand(esphome::display::Display &it, float cx, float cy, float angle_rad,
                          float tail_len, float tip_len, float root_w, float tip_w,
                          esphome::Color body_col, esphome::Color lume_col,
                          bool draw_lume = true, bool draw_shadow = true);

void render_analog_chapter_ring(esphome::display::Display &it, int cx, int cy, int r_dial,
                                esphome::Color cardinal_col, esphome::Color major_col,
                                esphome::Color minor_col, bool double_twelve = true);

void render_center_arbor_aa(esphome::display::Display &it, int cx, int cy,
                            esphome::Color outer_col, esphome::Color cap_col,
                            esphome::Color jewel_col);

// Shared 2-zone glass card row used by Time App -> Timers & Alarms:
//   - Left 72%: primary readout + secondary caption (taps open detail/editor)
//   - Right 28%: toggle switch (action_is_toggle=true) or play/pause button (action_is_toggle=false)
void split_action_card_row(esphome::display::Display &it, int cx, int cy, int w, int h,
                           const char *primary_txt, esphome::display::BaseFont *font_pri,
                           esphome::Color pri_col,
                           const char *secondary_txt, esphome::display::BaseFont *font_sec,
                           esphome::Color sec_col,
                           bool card_active, esphome::Color accent_col,
                           bool action_is_toggle, bool action_on,
                           uint16_t left_wid, uint16_t right_wid, int16_t param = 0);

}  // namespace ui
}  // namespace latter_ui
