#pragma once

// ==============================================================================
// Latter Reusable On-Device Widget, Chrome & Vector Glyph Library
// (include/ui_widgets.h)
// Pure C++20 UI header — zero ESPHome or HAL includes.
// Every interactive widget co-registers its drawn bounds in frame_hits().
// ==============================================================================

#include "ui_frame_context.h"
#include "ui_gfx.h"
#include "ui_hit_table.h"
#include "ui_layout.h"

namespace latter_ui {

// ------------------------------------------------------------------------------
// 1. Shared Frosted-Glass Chrome, Headers, Mode Bars & Navigation Primitives
// ------------------------------------------------------------------------------
void render_pill_button(Rgb565Surface &it, int x1, int x2, int y_mid, int r,
                        UiColor border_c, UiColor bg_c,
                        const char *label, UiColor text_c,
                        UiFontView font,
                        GlassVariant variant = GlassVariant::Clear);

UiColor render_modern_card(Rgb565Surface &it, int cx_card, int cy_card,
                           int w, int h, int r,
                           UiColor bg_c, UiColor border_c,
                           const char *label, UiColor text_c,
                           UiFontView font,
                           GlassVariant variant = GlassVariant::Clear,
                           bool sample_surface = false);

UiColor render_card_box(Rgb565Surface &it, int x0, int y0, int w, int h,
                        UiColor bg_c, UiColor border_c, int r = 10,
                        GlassVariant variant = GlassVariant::Clear,
                        UiColor ref_text = kChromeGlassSheen,
                        bool sample_surface = false);

UiColor render_glass_circle(Rgb565Surface &it, int cx_btn, int cy_btn, int r,
                            UiColor bg_c, UiColor border_c,
                            int inner_hole_r = 0,
                            GlassVariant variant = GlassVariant::Clear,
                            UiColor ref_text = kChromeGlassSheen,
                            bool sample_surface = false);

inline UiColor render_glass_circle(Rgb565Surface &it, int cx_btn, int cy_btn, int r,
                                   UiColor bg_c, UiColor border_c,
                                   GlassVariant variant,
                                   UiColor ref_text = kChromeGlassSheen) {
  return render_glass_circle(it, cx_btn, cy_btn, r, bg_c, border_c, 0, variant, ref_text);
}

inline UiColor render_glass_ring(Rgb565Surface &it, int cx_btn, int cy_btn,
                                 int outer_r, int inner_hole_r,
                                 UiColor bg_c, UiColor border_c,
                                 GlassVariant variant = GlassVariant::Clear,
                                 UiColor ref_text = kChromeGlassSheen) {
  return render_glass_circle(it, cx_btn, cy_btn, outer_r, bg_c, border_c, inner_hole_r, variant, ref_text);
}

void render_scroll_chevron(Rgb565Surface &it, int x, int y, bool is_up,
                           UiColor c, UiColor shadow_c, bool shadow = false);

void render_bezel_alert_border(Rgb565Surface &it, int cx, int cy, UiColor ring_c);

void render_home_indicator_bar(Rgb565Surface &it, int cx, int y_pos = kHomeBarY,
                               UiColor bar_col = kChromeHomePill);

void render_viewport_scrollbar(Rgb565Surface &it, int cx, int y_pos, int total_items,
                               float cur_start_idx, UiColor col_thumb, bool is_dim = false);

void render_dock_page_pips(Rgb565Surface &it, int cx, int y_pos, int total_pages,
                           int active_page, UiColor col_active, UiColor col_inactive);

void render_vertical_page_pips(Rgb565Surface &it, bool show_up, bool show_down,
                               UiColor col_bg, UiColor col_active,
                               UiColor col_inactive, int bottom_limit_y = SCREEN_H);

void render_back_button(Rgb565Surface &it, int cx, UiFontView font,
                        int y_pos = kBackLabelY, const char *label = "< Back",
                        UiColor text_col = kChromeBackLabel);

void render_page_header(Rgb565Surface &it, int cx, const char *title,
                        UiFontView font,
                        UiColor color = UiColor::WHITE, bool shadow = true);
void render_page_header(Rgb565Surface &it, int cx, const std::string &title,
                        UiFontView font,
                        UiColor color = UiColor::WHITE, bool shadow = true);

void render_sub_header(Rgb565Surface &it, int cx, const char *text,
                       UiFontView font, UiColor color);
void render_sub_header(Rgb565Surface &it, int cx, const std::string &text,
                       UiFontView font, UiColor color);

void render_empty_state(Rgb565Surface &it, int cx, int cy, const char *text,
                        UiFontView font, UiColor text_c);

void render_time_readout(Rgb565Surface &it, int cx, const char *text,
                         UiFontView font_time, UiColor color,
                         int y_pos = scale_y(202));

inline constexpr bool offline_pill_due(bool ha_connected, uint32_t ha_disconnect_ms,
                                       uint32_t now_ms) noexcept {
  return !ha_connected && ((now_ms - ha_disconnect_ms) >= 8000U);
}

inline bool offline_pill_due(const UiFrameContext &ctx) noexcept {
  return offline_pill_due(ctx.ha_connected, ctx.ha_disconnect_ms, ctx.now_ms);
}

void render_offline_pill(Rgb565Surface &it, int cx, uint32_t now_ms,
                         UiFontView font, UiColor text_c,
                         bool ha_connected = true, uint32_t ha_disconnect_ms = 0U);
inline void render_offline_pill(Rgb565Surface &it, const UiFrameContext &ctx) {
  render_offline_pill(it, ctx.cx, ctx.now_ms, ctx.font_micro, ctx.col_gold,
                      ctx.ha_connected, ctx.ha_disconnect_ms);
}

void render_in_app_mode_bar_track(Rgb565Surface &it, int cx, const int *win_xs,
                                  int vis_count, int active_vis_idx, bool has_left_arrow,
                                  bool has_right_arrow, int total_count, int active_global_idx,
                                  UiColor theme_primary, UiColor col_white,
                                  int explicit_win_start = -1);

void render_progress_arc_r224(Rgb565Surface &it, int cx, int cy, float prog,
                              UiColor col);

namespace ui {

// ------------------------------------------------------------------------------
// 2. Semantic Chrome Surface Tokens
// ------------------------------------------------------------------------------
UiColor chrome_card_bg_on();
UiColor chrome_card_bg_off();
UiColor chrome_card_brd_on();
UiColor chrome_card_brd_off();
UiColor chrome_danger_bg();
UiColor chrome_modal_bg();
UiColor chrome_key_bg();
UiColor chrome_key_brd();
UiColor chrome_wheel_inner_ring();
UiColor chrome_wheel_outer_ring();

// ------------------------------------------------------------------------------
// 3. Canonical Solid + Negative-Space Custom Glyph Library (D-008 / D-051)
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

void draw_icon_power(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_clock_solid(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_hourglass(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_alarm_clock(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_stopwatch(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_shield_home(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_camera(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_speaker(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool waves = true,
                       bool shadow = false, bool muted = false);
void draw_icon_thermometer(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_lock(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool is_locked = true, bool shadow = false);
void draw_icon_garage_door(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool is_open = false, bool shadow = false);
void draw_icon_robot_vacuum(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_eighth_notes(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_calendar(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_cal_agenda(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_cal_day(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_cal_week(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_mic(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_lightbulb(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_ceiling_fan(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_curtains(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_blinds_slats(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool is_open = false, bool shadow = false);
void draw_icon_switch_pill(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool is_on = true, bool shadow = false);
void draw_icon_sparkle(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_bento_grid(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_droplet(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_intercom_handset(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_bell(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_thumbtack(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_play_pause(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_hamburger(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_grid_2x2(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_carousel(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_gear(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_chevron(Rgb565Surface &it, int cx, int cy, int r, bool is_right, UiColor col, bool shadow = false);
void draw_icon_plus(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_minus(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_home(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_forecast_chart(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_sun(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_crescent_moon(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_wind(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow = false);
void draw_icon_arrow_v(Rgb565Surface &it, int cx, int cy, int r, bool is_up, UiColor col, bool shadow = false);
void draw_route_icon(Rgb565Surface &it, RouteId route, int cx, int cy, int r,
                     UiColor col, UiColor cut_c, bool shadow = false);

// ------------------------------------------------------------------------------
// 4. Canonical Text, Stack Width & Functional Component Library (D-070)
// ------------------------------------------------------------------------------
void shadow_print(Rgb565Surface &it, int x, int y,
                  UiFontView font, UiColor col,
                  UiTextAlign align, const char *text,
                  bool double_shadow = false);

int wrap_text_lines(const std::string &text, int max_w, UiFontView font,
                    std::string *out_lines, int max_lines);

inline constexpr int compute_uniform_stack_w(int top_cy, int bot_cy, int row_h,
                                             int max_w = kMaxContentColW, int inset = 14) {
  return compute_uniform_stack_w_for(
      top_cy, bot_cy, row_h, max_w, SCREEN_W, SCREEN_H,
      LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, inset);
}

using ModeBarDrawIconFn = void (*)(Rgb565Surface &it, int global_idx,
                                   int bx, int by, int icon_r,
                                   UiColor fg_col, bool shadow, void *user_ctx);

void mode_bar(Rgb565Surface &it, int cx,
              int total_count, int active_global_idx, int win_start,
              UiColor theme_primary, UiColor col_white,
              ModeBarDrawIconFn draw_icon_fn, void *user_ctx = nullptr);

void header_pager_bar(Rgb565Surface &it, int cx, int cy,
                      const std::string &title, UiFontView font,
                      UiColor col_white, bool show_arrows,
                      uint16_t prev_wid = WidgetId::NONE,
                      uint16_t next_wid = WidgetId::NONE,
                      int16_t prev_param = -1, int16_t next_param = 1);

void action_pill(Rgb565Surface &it, int x0, int w, int y_mid, int h,
                 UiColor border_c, UiColor bg_c,
                 const char *label, UiColor text_c, UiFontView font,
                 uint16_t widget_id = WidgetId::NONE,
                 uint8_t flags = kHitTap | kHitFocusSlot, int16_t param = 0,
                 int hit_h_min = -1, const char *leading_mdi = nullptr);

void action_pill_centered(Rgb565Surface &it, int cx, int w, int y_mid, int h,
                          UiColor border_c, UiColor bg_c,
                          const char *label, UiColor text_c,
                          UiFontView font,
                          uint16_t widget_id = WidgetId::NONE,
                          uint8_t flags = kHitTap | kHitFocusSlot, int16_t param = 0,
                          int hit_h_min = -1, const char *leading_mdi = nullptr);

struct ActionPillSpec {
  const char *label{nullptr};
  uint16_t widget_id{WidgetId::NONE};
  UiColor border_c{kChromeCardBrdOn};
  UiColor bg_c{kChromeCardBgOff};
  UiColor text_c{UiColor::WHITE};
  int16_t param{0};
  const char *leading_mdi{nullptr};
};

void action_pill_row(Rgb565Surface &it, int cy, int h,
                     int max_row_w, int max_btn_w, int gap,
                     const ActionPillSpec *items, int count,
                     UiFontView font, int inset = 14);

void segmented_toggle(Rgb565Surface &it, int cx, int cy, int total_w, int h,
                      const char *left_label, const char *right_label, bool right_active,
                      UiFontView font,
                      UiColor theme_primary, UiColor col_white,
                      UiColor col_slate,
                      uint16_t left_wid, uint16_t right_wid);

void setting_cycle_row(Rgb565Surface &it, int cx, int cy, int w, int h,
                       const char *label, bool enabled,
                       UiFontView font,
                       UiColor col_white, UiColor col_slate,
                       UiColor chevron_col,
                       uint16_t widget_id = WidgetId::NONE,
                       uint8_t flags = kHitTap | kHitFocusSlot, int16_t param = 0);

void slider_card_row(Rgb565Surface &it, int cx, int cy, int w, int h,
                     const char *label, int pct, int min_pct, int max_pct, bool enabled,
                     UiColor fill_c, UiColor brd_c,
                     UiFontView font, UiColor text_c,
                     uint16_t widget_id = WidgetId::NONE, int16_t param = 0);

void list_row(Rgb565Surface &it, int cx, int cy, int w, int h,
              const char *label, const char *sub_label,
              UiColor bg_c, UiColor brd_c,
              UiColor text_c, UiColor sub_c,
              UiFontView font,
              uint16_t widget_id = WidgetId::NONE,
              uint8_t flags = kHitTap | kHitFocusSlot, int16_t param = 0,
              const char *leading_mdi = nullptr,
              const char *right_badge = nullptr,
              UiColor right_badge_c = kChromeBackLabel);

using TileDrawIconFn = void (*)(Rgb565Surface &it, int cx, int cy,
                                int icon_r, UiColor col, bool shadow, void *user_ctx);

void labeled_action_row(Rgb565Surface &it, int cx, int cy, int w, int h,
                        const char *title, const char *status,
                        UiColor title_c, UiColor status_c,
                        UiFontView title_font,
                        UiFontView status_font,
                        const char *cta_label, int cta_w, int cta_h,
                        UiColor cta_border_c, UiColor cta_bg_c,
                        UiColor cta_text_c,
                        UiFontView cta_font,
                        uint16_t cta_wid, uint8_t cta_flags = kHitTap | kHitFocusSlot,
                        int16_t cta_param = 0, int cta_hit_h_min = -1,
                        TileDrawIconFn draw_icon_fn = nullptr, void *icon_ctx = nullptr,
                        UiColor icon_c = UiColor::WHITE);

void grid_tile(Rgb565Surface &it, int cx, int cy, int w, int h,
               const char *label, bool active, bool disabled,
               UiColor accent_c, UiColor text_c,
               UiFontView font,
               TileDrawIconFn draw_icon_fn, void *user_ctx,
               uint16_t widget_id = WidgetId::NONE, int16_t param = 0);

void icon_circle_button(Rgb565Surface &it, int cx, int cy, int r,
                        UiColor bg_c, UiColor brd_c,
                        UiColor icon_c,
                        TileDrawIconFn draw_icon_fn, void *user_ctx,
                        uint16_t widget_id = WidgetId::NONE,
                        uint8_t flags = kHitTap | kHitFocusSlot | kHitFocusCircle,
                        int16_t param = 0, int hit_pad = -1);

void keypad_button(Rgb565Surface &it, int cx, int cy, int w, int h,
                   const char *label, UiColor bg_c, UiColor brd_c,
                   UiColor text_c, UiFontView font,
                   uint16_t widget_id = WidgetId::NONE, int16_t param = 0);

// ------------------------------------------------------------------------------
// 5. Interactive Hit-Registered Widgets, PIN Modal & SDF Horology Primitives
// ------------------------------------------------------------------------------
void toggle_switch(Rgb565Surface &it, int cx_sw, int cy_sw, bool is_on, UiColor active_col);

void toggle_card_row(Rgb565Surface &it, int cx, int y_mid, int w, int h,
                     const char *label, bool is_on, UiFontView font,
                     UiColor active_col, UiColor text_on, UiColor text_off,
                     uint16_t widget_id = WidgetId::NONE, int16_t param = 0);

void pill_button(Rgb565Surface &it, int x1, int x2, int y_mid, int r,
                 UiColor border_c, UiColor bg_c,
                 const char *label, UiColor text_c, UiFontView font,
                 uint16_t widget_id = WidgetId::NONE, uint8_t flags = kHitTap,
                 int16_t param = 0, GlassVariant variant = GlassVariant::Clear);

void pill_button_box(Rgb565Surface &it, int x0, int w, int y_mid, int r,
                     UiColor border_c, UiColor bg_c,
                     const char *label, UiColor text_c, UiFontView font,
                     uint16_t widget_id = WidgetId::NONE, uint8_t flags = kHitTap,
                     int16_t param = 0, GlassVariant variant = GlassVariant::Clear);

void stepper_row(Rgb565Surface &it, int cx, int y_top, int h,
                 const char *const labels[4], UiFontView font,
                 UiColor col_text, uint16_t base_widget_id = WidgetId::STEPPER_BASE);

UiColor hsv_wheel_sector_color(int sector_idx_72, float sat);
bool hsv_wheel_pick(int tx, int ty, int cx, int cy, int outer_r_466, int inner_r_466,
                    float &out_hue_deg, float &out_sat);

void hsv_color_wheel(Rgb565Surface &it, int cx, int cy, int outer_r_466, int inner_r_466,
                     float cur_hue_deg, float cur_sat,
                     UiColor col_bg, UiColor col_white,
                     uint16_t widget_id = WidgetId::COLOR_WHEEL_RING);

void confirm_modal(Rgb565Surface &it, int cx, const char *title,
                   const char *line1, const char *line2,
                   const char *cancel_lbl, const char *confirm_lbl,
                   UiColor confirm_border, UiColor confirm_bg,
                   UiFontView font_hdr, UiFontView font_body,
                   UiColor theme_primary, UiColor col_white, UiColor col_slate,
                   uint16_t cancel_wid = WidgetId::CONFIRM_CANCEL,
                   uint16_t confirm_wid = WidgetId::CONFIRM_ACCEPT);

void game_over_sheet(Rgb565Surface &it, int cx, int cy,
                     const char *title, UiColor title_col,
                     const char *score_line, const char *best_line, bool is_new_best,
                     const char *cta_lbl,
                     UiFontView font_hdr, UiFontView font_body,
                     UiColor theme_primary, UiColor col_white,
                     UiColor col_gold, UiColor col_slate,
                     uint16_t cta_wid = WidgetId::GAME_OVER_CTA);

enum class PinPadTargetKind : uint8_t {
  NONE = 0,
  AREA_ACTION = 1,
  ALARM_ACTION = 2,
  SHORTCUT_ACTION = 3,
};

struct PinPadModalState {
  static constexpr uint8_t kMinDigits = 4;
  static constexpr uint8_t kMaxDigits = 12;
  static constexpr uint32_t kAutoDismissMs = 30000U;
  bool open{false};
  PinPadTargetKind target{PinPadTargetKind::NONE};
  char digits[13]{};
  uint8_t len{0};
  char prompt[40]{};
  char error_msg[32]{};
  uint32_t error_ms{0};
  uint32_t last_activity_ms{0};
};

PinPadModalState &pin_pad_state();
void pin_pad_scrub();
void pin_pad_open(PinPadTargetKind kind, const char *prompt_label, uint32_t now_ms = 0U);
bool pin_pad_press_digit(uint8_t digit, uint32_t now_ms = 0U);
void pin_pad_backspace(uint32_t now_ms = 0U);
bool is_perimeter_action_blocked(const std::string &policy, const std::string &domain_or_entity,
                                 const std::string &action, int level = 100);
bool requires_perimeter_pin_prompt(const std::string &policy, const std::string &domain_or_entity,
                                   const std::string &action, int level = 100);
bool requires_perimeter_pin_prompt(const std::string &policy, const std::string &domain_or_entity,
                                   const std::string &action, int level, uint16_t alarm_caps);

void pin_pad_modal(Rgb565Surface &it, int cx, int cy, uint32_t now_ms,
                   UiFontView font_hdr, UiFontView font_key,
                   UiFontView font_small,
                   UiColor theme_primary, UiColor col_white,
                   UiColor col_slate, UiColor col_red);

void draw_aa_line(Rgb565Surface &it, float x0, float y0, float x1, float y1,
                  float width, UiColor col, int alpha_max = 255);

void draw_aa_disc(Rgb565Surface &it, float cx, float cy, float r,
                  UiColor col, int alpha_max = 255);

void draw_aa_ring(Rgb565Surface &it, float cx, float cy, float r,
                  float thick, UiColor col, int alpha_max = 255);

void draw_aa_tapered_hand(Rgb565Surface &it, float cx, float cy, float angle_rad,
                          float tail_len, float tip_len, float root_w, float tip_w,
                          UiColor body_col, UiColor lume_col,
                          bool draw_lume = true, bool draw_shadow = true);

void render_analog_chapter_ring(Rgb565Surface &it, int cx, int cy, int r_dial,
                                UiColor cardinal_col, UiColor major_col,
                                UiColor minor_col, bool double_twelve = true);

void render_center_arbor_aa(Rgb565Surface &it, int cx, int cy,
                            UiColor outer_col, UiColor cap_col,
                            UiColor jewel_col);

void split_action_card_row(Rgb565Surface &it, int cx, int cy, int w, int h,
                           const char *primary_txt, UiFontView font_pri,
                           UiColor pri_col,
                           const char *secondary_txt, UiFontView font_sec,
                           UiColor sec_col,
                           bool card_active, UiColor accent_col,
                           bool action_is_toggle, bool action_on,
                           uint16_t left_wid, uint16_t right_wid, int16_t param = 0);

}  // namespace ui
}  // namespace latter_ui
