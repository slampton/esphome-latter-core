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
                        esphome::display::BaseFont *font);

// Center-origin frosted-glass card (cx_card, cy_card), returning resolved surface color.
esphome::Color render_modern_card(esphome::display::Display &it, int cx_card, int cy_card,
                                  int w, int h, int r,
                                  esphome::Color bg_c, esphome::Color border_c,
                                  const char *label, esphome::Color text_c,
                                  esphome::display::BaseFont *font);

// Top-left-origin frosted-glass card box (x0, y0, w, h), returning resolved surface color.
esphome::Color render_card_box(esphome::display::Display &it, int x0, int y0, int w, int h,
                               esphome::Color bg_c, esphome::Color border_c, int r = 10);

esphome::Color render_glass_circle(esphome::display::Display &it, int cx_btn, int cy_btn, int r,
                                   esphome::Color bg_c, esphome::Color border_c);

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
                               esphome::Color col_inactive);

// Back affordance (docs/device-ui-design.md): callers pass layout().back_y ([BACK BUTTON Y]).
void render_back_button(esphome::display::Display &it, int cx, esphome::display::BaseFont *font,
                        int y_pos = kBackLabelY, const char *label = "< Back",
                        esphome::Color text_col = kChromeBackLabel);

void render_page_header(esphome::display::Display &it, int cx, const char *title,
                        esphome::display::BaseFont *font, esphome::Color color, bool shadow = false);
void render_page_header(esphome::display::Display &it, int cx, const std::string &title,
                        esphome::display::BaseFont *font, esphome::Color color, bool shadow = false);

void render_sub_header(esphome::display::Display &it, int cx, const char *text,
                       esphome::display::BaseFont *font, esphome::Color color);
void render_sub_header(esphome::display::Display &it, int cx, const std::string &text,
                       esphome::display::BaseFont *font, esphome::Color color);

void render_empty_state(esphome::display::Display &it, int cx, int cy, const char *text,
                        esphome::display::BaseFont *font, esphome::Color text_c);

void render_time_readout(esphome::display::Display &it, int cx, const char *text,
                         esphome::display::BaseFont *font_time, esphome::Color color);

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
// 3. Deduplicated Canonical Vector Glyph Library (fast_* / AA primitives only)
// ------------------------------------------------------------------------------
void draw_icon_power(esphome::display::Display &it, int cx, int cy, int r,
                     esphome::Color col, esphome::Color bg_cut);
void draw_icon_thermometer(esphome::display::Display &it, int cx, int cy,
                           esphome::Color col, esphome::Color bulb_inner);
void draw_icon_eighth_notes(esphome::display::Display &it, int cx, int cy, esphome::Color col);
void draw_icon_speaker(esphome::display::Display &it, int cx, int cy, esphome::Color col, bool waves = true);
void draw_icon_lock(esphome::display::Display &it, int cx, int cy, esphome::Color col,
                    esphome::Color keyhole_col, bool is_locked = true);
void draw_icon_shield(esphome::display::Display &it, int cx, int cy, esphome::Color col);
void draw_icon_hourglass(esphome::display::Display &it, int cx, int cy,
                         esphome::Color col, esphome::Color accent, int half_h = 13);
void draw_icon_alarm_clock(esphome::display::Display &it, int cx, int cy,
                           esphome::Color col, esphome::Color surf_bg, int radius = 11);
void draw_icon_stopwatch(esphome::display::Display &it, int cx, int cy,
                         esphome::Color col, esphome::Color accent, int radius = 11);
void draw_icon_calendar(esphome::display::Display &it, int cx, int cy,
                        esphome::Color col, esphome::Color accent, int half_w = 11);
void draw_icon_camera(esphome::display::Display &it, int cx, int cy,
                      esphome::Color col, esphome::Color surf_bg, int half_w = 11);
void draw_icon_lightbulb(esphome::display::Display &it, int cx, int cy,
                         esphome::Color col, bool rays = false);
void draw_icon_fan(esphome::display::Display &it, int cx, int cy, esphome::Color col);
void draw_icon_blinds(esphome::display::Display &it, int cx, int cy, esphome::Color col);
void draw_icon_settings_gear(esphome::display::Display &it, int cx, int cy,
                             esphome::Color col, esphome::Color surf_bg);
void draw_icon_remote(esphome::display::Display &it, int cx, int cy,
                      esphome::Color col, esphome::Color surf_bg);
void draw_icon_gamepad(esphome::display::Display &it, int cx, int cy,
                       esphome::Color col, esphome::Color surf_bg);
void draw_icon_wrench(esphome::display::Display &it, int cx, int cy, esphome::Color col);

// ------------------------------------------------------------------------------
// 4. Interactive Hit-Registered Widgets & Modals
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
                 int16_t param = 0);

// Canonical 68 px stride 4-column stepper row (matches UiLayout::hit_4col_stepper).
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
  DOORBELL_UNLOCK = 3,
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
                                   const std::string &action, int level = 100,
                                   bool alarm_has_code = false);

void pin_pad_modal(esphome::display::Display &it, int cx, int cy, uint32_t now_ms,
                   esphome::display::BaseFont *font_hdr, esphome::display::BaseFont *font_key,
                   esphome::display::BaseFont *font_small,
                   esphome::Color theme_primary, esphome::Color col_white,
                   esphome::Color col_slate, esphome::Color col_red);

}  // namespace ui
}  // namespace latter_ui
