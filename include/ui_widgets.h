#pragma once

// ==============================================================================
// Latter Reusable On-Device Widget Library (include/ui_widgets.h)
// Unified switches, pills, 4-column steppers, LUT-backed HSV color wheel,
// confirmation sheets, 3-tier security PIN pad modal, and game-over modals.
// Every interactive widget co-registers its drawn bounds in frame_hits().
// ==============================================================================

#include "ui_core.h"
#include "ui_hit_table.h"
#include "ui_icons.h"
#include "ui_layout.h"

namespace latter_ui {
namespace ui {

// Semantic chrome surface tokens defined in components/latter_ui/ui_widgets.cpp
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
esphome::Color chrome_remote_pad_bg();
esphome::Color chrome_remote_pad_brd();
esphome::Color chrome_remote_ok_bg();
esphome::Color chrome_remote_pwr_bg();
esphome::Color chrome_remote_pwr_brd();

// 1. Unified Stadium Toggle Switch (used in Area, Music, Timers/Alarms, and System Settings)
template <typename DisplayT>
inline void toggle_switch(DisplayT &it, int cx_sw, int cy_sw, bool is_on, esphome::Color active_col) {
  const int sw_w = scale_x(40);
  const int sw_h = scale_y(22);
  const float rad = 0.5f * static_cast<float>(sw_h);
  const esphome::Color track_c = is_on ? active_col : kChromeTrack;
  const esphome::Color brd_c = is_on ? active_col : kChromeTrackBorder;
  stroke_round_rect_aa(it, cx_sw - sw_w / 2, cy_sw - sw_h / 2, sw_w, sw_h, rad, brd_c, brd_c, 1);
  fill_round_rect_aa(it, cx_sw - sw_w / 2 + 1, cy_sw - sw_h / 2 + 1, sw_w - 2, sw_h - 2, rad - 1.0f, track_c);
  const int thumb_r = std::max(5, sw_h / 2 - 3);
  const int thumb_cx = is_on ? (cx_sw + sw_w / 2 - thumb_r - 3) : (cx_sw - sw_w / 2 + thumb_r + 3);
  fast_filled_circle_aa(it, thumb_cx + 1, cy_sw + 1, thumb_r, kChromeShadow);
  fast_filled_circle_aa(it, thumb_cx, cy_sw, thumb_r, kChromeGlassSheen);
}

// Boolean setting card row with left-aligned label and right-aligned toggle_switch
template <typename DisplayT, typename FontT>
inline void toggle_card_row(DisplayT &it, int cx, int y_mid, int w, int h,
                            const char *label, bool is_on, FontT *font,
                            esphome::Color active_col, esphome::Color text_on, esphome::Color text_off,
                            uint16_t widget_id = WidgetId::NONE, int16_t param = 0) {
  const esphome::Color bg_c = is_on ? chrome_card_bg_on() : chrome_card_bg_off();
  const esphome::Color brd_c = is_on ? active_col : chrome_card_brd_off();
  render_modern_card(it, cx, y_mid, w, h, 10, bg_c, brd_c, "", text_on, font);
  int eff_w = w;
  if (SCREEN_W > 520 && eff_w <= 340) eff_w = scale_x(eff_w);
  else if (SCREEN_W < 380 && h <= 48 && eff_w >= 240 && eff_w <= 330) eff_w = SCREEN_W - 88;
  else if (eff_w > SCREEN_W - 20) eff_w = SCREEN_W - 20;

  const int text_x = cx - eff_w / 2 + scale_x(14);
  const int sw_cx = cx + eff_w / 2 - scale_x(28);
  const int max_lbl_px = std::max(60, (sw_cx - scale_x(26)) - text_x);
  const std::string fit_lbl = fit_text_to_width_font(
      label != nullptr ? std::string(label) : std::string(), max_lbl_px, font);
  const esphome::Color fg = is_on ? text_on : text_off;
  fast_print(it, text_x + 1, y_mid + 1, font, kChromeShadow, esphome::display::TextAlign::CENTER_LEFT, fit_lbl.c_str());
  fast_print(it, text_x, y_mid, font, fg, esphome::display::TextAlign::CENTER_LEFT, fit_lbl.c_str());
  toggle_switch(it, sw_cx, y_mid, is_on, active_col);

  if (widget_id != WidgetId::NONE) {
    frame_hits().add_centered_rect(cx, y_mid, eff_w, h, widget_id, kHitTap | kHitFocusSlot, param);
  }
}

// 2. Hit-Registered Frosted Pill Button
template <typename DisplayT, typename FontT>
inline void pill_button(DisplayT &it, int x1, int x2, int y_mid, int r,
                        esphome::Color border_c, esphome::Color bg_c,
                        const char *label, esphome::Color text_c, FontT *font,
                        uint16_t widget_id = WidgetId::NONE, uint8_t flags = kHitTap,
                        int16_t param = 0, UiActionFn on_event = nullptr, void *ctx = nullptr) {
  render_pill_button(it, x1, x2, y_mid, r, border_c, bg_c, label, text_c, font);
  if (widget_id != WidgetId::NONE) {
    const int px0 = x1 - r;
    const int py0 = y_mid - r;
    const int pw = (x2 - x1) + 2 * r + 1;
    const int ph = 2 * r + 1;
    frame_hits().add_rect(px0, py0, pw, ph, widget_id, flags, param, on_event, ctx);
  }
}

// 3. Unified 4-Column Stepper Row (Timers & Alarm Editor)
template <typename DisplayT, typename FontT>
inline void stepper_row(DisplayT &it, int cx, int y_top, int h,
                        const char *const labels[4], FontT *font,
                        esphome::Color col_text, uint16_t base_widget_id = WidgetId::STEPPER_BASE) {
  const int btn_w = scale_x(62);
  const int step = scale_x(68);
  const int y_mid = y_top + h / 2;
  const esphome::Color bg_c = chrome_card_bg_on();
  const esphome::Color brd_c = chrome_card_brd_on();
  for (int i = 0; i < 4; i++) {
    const int bx = cx + static_cast<int>((static_cast<float>(i) - 1.5f) * static_cast<float>(step));
    render_modern_card(it, bx, y_mid, btn_w, h, 8, bg_c, brd_c, labels[i], col_text, font);
    if (base_widget_id != WidgetId::NONE) {
      frame_hits().add_centered_rect(
          bx, y_mid, btn_w, h,
          static_cast<uint16_t>(base_widget_id + i),
          kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
    }
  }
}

// 4. LUT-Backed HSV Color Wheel & Exact Hit-to-HSV Picker (System Theme + Light Inspector)
esphome::Color hsv_wheel_sector_color(int sector_deg_5, float sat);
bool hsv_wheel_pick(int tx, int ty, int cx, int cy, int outer_r_466, int inner_r_466,
                    float &out_hue_deg, float &out_sat);

template <typename DisplayT>
inline void hsv_color_wheel(DisplayT &it, int cx, int cy, int outer_r_466, int inner_r_466,
                            float cur_hue_deg, float cur_sat,
                            esphome::Color col_bg, esphome::Color col_white,
                            uint16_t widget_id = WidgetId::COLOR_WHEEL_RING) {
  const float ws = (SCREEN_MIN_DIM > 520) ? (static_cast<float>(SCREEN_MIN_DIM) / 466.0f)
                 : ((SCREEN_MIN_DIM < 380) ? (static_cast<float>(SCREEN_MIN_DIM) / 420.0f) : 1.0f);
  const int r_in = static_cast<int>(static_cast<float>(inner_r_466) * ws);
  const int r_out = static_cast<int>(static_cast<float>(outer_r_466) * ws);
  const int r_span = std::max(1, r_out - r_in);

  for (int a_idx = 0; a_idx < 72; a_idx++) {
    const float rad = static_cast<float>(a_idx * 5) * (PI_F / 180.0f);
    float s_v = 0.0f, c_v = 1.0f;
    fast_sincos(rad, s_v, c_v);
    for (int band = 0; band < 4; band++) {
      const int r0 = r_in + (band * r_span) / 4;
      const int r1 = r_in + ((band + 1) * r_span) / 4;
      const float sat_b = 0.40f + 0.20f * static_cast<float>(band);
      const esphome::Color seg_c = hsv_wheel_sector_color(a_idx, sat_b);
      const int x0 = cx + static_cast<int>(static_cast<float>(r0) * c_v);
      const int y0 = cy + static_cast<int>(static_cast<float>(r0) * s_v);
      const int x1 = cx + static_cast<int>(static_cast<float>(r1) * c_v);
      const int y1 = cy + static_cast<int>(static_cast<float>(r1) * s_v);
      fast_line(it, x0, y0, x1, y1, seg_c);
      fast_line(it, x0 + 1, y0, x1 + 1, y1, seg_c);
      fast_line(it, x0, y0 + 1, x1, y1 + 1, seg_c);
    }
  }

  const esphome::Color cur_rgb = hsv_to_rgb(cur_hue_deg / 360.0f, cur_sat, 1.0f);
  fast_filled_circle_aa(it, cx, cy, std::max(8, r_in - static_cast<int>(10.0f * ws)), cur_rgb);
  fast_circle_aa(it, cx, cy, static_cast<float>(std::max(9, r_in - static_cast<int>(9.0f * ws))),
                 chrome_wheel_inner_ring(), 1.5f);
  fast_circle_aa(it, cx, cy, static_cast<float>(r_out + 2), chrome_wheel_outer_ring(), 1.5f);

  const float sel_rad = cur_hue_deg * (PI_F / 180.0f);
  const float norm_s = std::max(0.0f, std::min(1.0f, (cur_sat - 0.35f) / 0.65f));
  const float sel_r = static_cast<float>(r_in) + norm_s * static_cast<float>(r_span);
  float ss = 0.0f, sc = 1.0f;
  fast_sincos(sel_rad, ss, sc);
  const int sx = cx + static_cast<int>(sel_r * sc);
  const int sy = cy + static_cast<int>(sel_r * ss);
  const int thumb_r = std::max(7, static_cast<int>(9.0f * ws));
  fast_filled_circle_aa(it, sx, sy, thumb_r + 2, col_bg);
  fast_filled_circle_aa(it, sx, sy, thumb_r, col_white);
  fast_filled_circle_aa(it, sx, sy, std::max(3, thumb_r - 3), cur_rgb);

  if (widget_id != WidgetId::NONE) {
    frame_hits().add_circle(cx, cy, r_out + 8, widget_id, kHitTap);
  }
}

// 5. Unified Confirmation Modal (System Restart & Firmware Install)
template <typename DisplayT, typename FontHdrT, typename FontBodyT>
inline void confirm_modal(DisplayT &it, int cx, const char *title,
                          const char *line1, const char *line2,
                          const char *cancel_lbl, const char *confirm_lbl,
                          esphome::Color confirm_border, esphome::Color confirm_bg,
                          FontHdrT *font_hdr, FontBodyT *font_body,
                          esphome::Color theme_primary, esphome::Color col_white, esphome::Color col_slate,
                          uint16_t cancel_wid = WidgetId::CONFIRM_CANCEL,
                          uint16_t confirm_wid = WidgetId::CONFIRM_ACCEPT) {
  render_page_header(it, cx, title, font_hdr, theme_primary);
  if (line1 != nullptr && line1[0] != '\0') {
    fast_print(it, cx, scale_y(132), font_body, col_white, esphome::display::TextAlign::CENTER, line1);
  }
  if (line2 != nullptr && line2[0] != '\0') {
    fast_print(it, cx, scale_y(168), font_body, col_slate, esphome::display::TextAlign::CENTER, line2);
  }
  const int btn_y = scale_y(242);
  const int btn_w = (SCREEN_W < 380) ? 116 : scale_x(126);
  const int btn_h = scale_y(54);
  const int gap_x = (SCREEN_W < 380) ? 64 : scale_x(72);

  render_modern_card(it, cx - gap_x, btn_y, btn_w, btn_h, 12,
                     chrome_card_bg_on(), chrome_card_brd_on(), cancel_lbl, col_white, font_body);
  render_modern_card(it, cx + gap_x, btn_y, btn_w, btn_h, 12,
                     confirm_bg, confirm_border, confirm_lbl, col_white, font_body);

  if (cancel_wid != WidgetId::NONE) {
    frame_hits().add_centered_rect(cx - gap_x, btn_y, btn_w, btn_h, cancel_wid, kHitTap | kHitFocusSlot, 0);
  }
  if (confirm_wid != WidgetId::NONE) {
    frame_hits().add_centered_rect(cx + gap_x, btn_y, btn_w, btn_h, confirm_wid, kHitTap | kHitFocusSlot, 1);
  }
}

// 6. Unified Game-Over Sheet (Marbles, Archery, Bricks, 2048, Memory Match)
template <typename DisplayT, typename FontHdrT, typename FontBodyT>
inline void game_over_sheet(DisplayT &it, int cx, int cy,
                            const char *title, esphome::Color title_col,
                            const char *score_line, const char *best_line, bool is_new_best,
                            const char *cta_lbl, FontHdrT *font_hdr, FontBodyT *font_body,
                            esphome::Color theme_primary, esphome::Color col_white,
                            esphome::Color col_gold, esphome::Color col_slate,
                            uint16_t cta_wid = WidgetId::GAME_OVER_CTA) {
  const int box_w = std::min(SCREEN_W - 28, scale_x(296));
  const int box_h = scale_y(196);
  render_modern_card(it, cx - box_w / 2, cy - scale_y(96), box_w, box_h,
                     chrome_modal_bg(), title_col, 14);
  fast_print(it, cx, cy - scale_y(60), font_hdr, title_col, esphome::display::TextAlign::CENTER, title);
  if (score_line != nullptr && score_line[0] != '\0') {
    fast_print(it, cx, cy - scale_y(20), font_body, col_white, esphome::display::TextAlign::CENTER, score_line);
  }
  if (best_line != nullptr && best_line[0] != '\0') {
    fast_print(it, cx, cy + scale_y(12), font_body, is_new_best ? col_gold : col_slate,
               esphome::display::TextAlign::CENTER, best_line);
  }
  const int pill_r = scale_s(18);
  const int pill_hw = std::min(box_w / 2 - pill_r - 12, scale_x(72));
  pill_button(it, cx - pill_hw, cx + pill_hw, cy + scale_y(58), pill_r,
              theme_primary, chrome_card_bg_on(), cta_lbl, col_white, font_body,
              cta_wid, kHitTap | kHitFocusSlot);
}

// 7. 3-Tier Perimeter Security On-Device PIN Pad Modal
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

template <typename DisplayT, typename FontHdrT, typename FontKeyT, typename FontSmallT>
inline void pin_pad_modal(DisplayT &it, int cx, int cy, uint32_t now_ms,
                          FontHdrT *font_hdr, FontKeyT *font_key, FontSmallT *font_small,
                          esphome::Color theme_primary, esphome::Color col_white,
                          esphome::Color col_slate, esphome::Color col_red) {
  (void)cy;
  auto &st = pin_pad_state();
  const char *hdr = (st.prompt[0] != '\0') ? st.prompt : "ENTER SECURITY PIN";
  render_page_header(it, cx, hdr, font_small, theme_primary);

  // Masked PIN display pill
  const int disp_y = scale_y(84);
  const int disp_w = std::min(SCREEN_W - 44, scale_x(236));
  const int disp_h = scale_y(36);
  const bool show_err = (st.error_msg[0] != '\0' && (now_ms - st.error_ms) < 2200U);
  render_modern_card(it, cx, disp_y, disp_w, disp_h, 10,
                     chrome_modal_bg(), show_err ? col_red : chrome_card_brd_on(),
                     "", col_white, font_small);
  if (show_err) {
    fast_print(it, cx, disp_y, font_small, col_white, esphome::display::TextAlign::CENTER, st.error_msg);
  } else if (st.len == 0) {
    fast_print(it, cx, disp_y, font_small, col_slate, esphome::display::TextAlign::CENTER, "4-8 Digits");
  } else {
    const int dot_step = scale_x(18);
    const int start_x = cx - ((static_cast<int>(st.len) - 1) * dot_step) / 2;
    for (uint8_t i = 0; i < st.len; i++) {
      fast_filled_circle_aa(it, start_x + static_cast<int>(i) * dot_step, disp_y, scale_s(5), col_white);
    }
  }

  // 3x4 keypad grid (rows 0..3, cols 0..2)
  const int key_w = (SCREEN_W < 380) ? 68 : scale_x(78);
  const int key_h = scale_y(48);
  const int col_step = (SCREEN_W < 380) ? 78 : scale_x(90);
  const int row_y0 = scale_y(136);
  const int row_step = scale_y(56);

  static constexpr const char *kLabels[12] = {
    "1", "2", "3",
    "4", "5", "6",
    "7", "8", "9",
    "DEL", "0", "OK"
  };
  static constexpr uint16_t kWids[12] = {
    WidgetId::PIN_PAD_KEY_BASE + 1, WidgetId::PIN_PAD_KEY_BASE + 2, WidgetId::PIN_PAD_KEY_BASE + 3,
    WidgetId::PIN_PAD_KEY_BASE + 4, WidgetId::PIN_PAD_KEY_BASE + 5, WidgetId::PIN_PAD_KEY_BASE + 6,
    WidgetId::PIN_PAD_KEY_BASE + 7, WidgetId::PIN_PAD_KEY_BASE + 8, WidgetId::PIN_PAD_KEY_BASE + 9,
    WidgetId::PIN_PAD_BACKSPACE,    WidgetId::PIN_PAD_KEY_BASE + 0, WidgetId::PIN_PAD_SUBMIT
  };

  for (int idx = 0; idx < 12; idx++) {
    const int r = idx / 3;
    const int c = idx % 3;
    const int kx = cx + (c - 1) * col_step;
    const int ky = row_y0 + r * row_step;
    const bool is_ok = (idx == 11);
    const bool is_del = (idx == 9);
    const bool ok_ready = (st.len >= 4);
    const esphome::Color brd = is_ok ? (ok_ready ? theme_primary : chrome_key_brd()) : chrome_key_brd();
    const esphome::Color bg  = is_ok && ok_ready ? chrome_card_bg_on() : chrome_key_bg();
    const esphome::Color fg  = (is_ok && !ok_ready) ? col_slate : col_white;
    render_modern_card(it, kx, ky, key_w, key_h, 10, bg, brd,
                       kLabels[idx], fg, (is_ok || is_del) ? font_small : font_key);
    frame_hits().add_centered_rect(kx, ky, key_w, key_h, kWids[idx], kHitTap | kHitFocusSlot,
                                   static_cast<int16_t>(idx));
  }

  render_back_button(it, cx, font_small, layout().back_y, "< Cancel");
  frame_hits().add_centered_rect(cx, layout().back_y, scale_x(150), scale_y(36),
                                 WidgetId::PIN_PAD_CANCEL, kHitTap, 0);
  (void)font_hdr;
}

}  // namespace ui
}  // namespace latter_ui
