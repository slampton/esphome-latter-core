#include "hal_display_lifecycle.h"
#include "ui_pages.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace latter_ui {
using namespace latter_core;

// ==============================================================================
// Page 0: Clock Page Engine (Digital & Analog Watch Faces)
// ==============================================================================

void render_clock_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  frame_base(it, ctx.col_bg);

  const auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const bool is_24h_format = prefs.is_24h_format;
  const uint8_t eff_clock_face = effective_clock_face(prefs, rs, ctx.now_ms);
  const UiStrView clock_face_opt =
      (rs.showcase.is_active(ctx.now_ms) && rs.showcase.clock_face_override >= 0)
          ? UiStrView(effective_clock_face_cstr(prefs, rs, ctx.now_ms))
          : ctx.clock_face_style_opt;

  const bool is_minimal_digital = (clock_face_opt == "Minimal Digital") ||
                                  (clock_face_opt.empty() && eff_clock_face == 0U);
  const bool is_ring_digital    = (clock_face_opt == "Hybrid Digital") ||
                                  (clock_face_opt.empty() && eff_clock_face == 2U);

  frame_hits().add_rect(0, 0, SCREEN_W, SCREEN_H, WidgetId::HOME_TIME_READOUT, kHitTap);

  if (is_minimal_digital) {
    const int time_cy = cy - dp_px(6);
    if (!ctx.now.is_valid()) {
      fast_print(it, cx, time_cy, ctx.font_time, ctx.col_slate,
                 UiTextAlign::CENTER, kUnknownGlyph);
      return;
    }
    const int date_cy = time_cy - (kAutoFonts.time / 2) - (kAutoFonts.date / 2) - dp_px(18);
    const int sec_cy = time_cy + (kAutoFonts.time / 2) + (kAutoFonts.small / 2) + dp_px(18);
    char date_buf[32];
    ctx.now.strftime(date_buf, sizeof(date_buf), "%a, %b %d");
    fast_print(it, cx, date_cy, ctx.font_date, ctx.theme_primary,
               UiTextAlign::CENTER, date_buf);

    char sq_dig[16], sq_ap[8] = "";
    format_time_hhmm(ctx.now.hour, ctx.now.minute, is_24h_format, sq_dig, sizeof(sq_dig), sq_ap);
    fast_print(it, cx, time_cy, ctx.font_time, ctx.col_white,
               UiTextAlign::CENTER, sq_dig);

    char sq_sec[32];
    if (is_24h_format) {
      std::snprintf(sq_sec, sizeof(sq_sec), "SEC  •  %02ds", ctx.now.second);
    } else {
      std::snprintf(sq_sec, sizeof(sq_sec), "%s  •  %02ds", sq_ap, ctx.now.second);
    }
    fast_print(it, cx, sec_cy, ctx.font_small, ctx.theme_secondary,
               UiTextAlign::CENTER, sq_sec);
    return;
  }

  // FACE 1 (MODERN ANALOG) & FACE 2 (HYBRID DIGITAL) — SDF anti-aliased horology
  const int r_dial = canvas_px(224);

  ui::render_analog_chapter_ring(it, cx, cy, r_dial,
                                 ctx.col_white, ctx.col_slate, ctx.col_gray, true);

  if (!ctx.now.is_valid()) {
    if (is_ring_digital) {
      fast_print(it, cx, cy - canvas_px(12), ctx.font_time, ctx.col_slate,
                 UiTextAlign::CENTER, kUnknownGlyph);
    } else {
      fast_print(it, cx, cy + canvas_px(78), ctx.font_date, ctx.col_slate,
                 UiTextAlign::CENTER, kUnknownGlyph);
      ui::render_center_arbor_aa(it, cx, cy, ctx.col_white, ctx.col_red, ctx.col_dark_gray);
    }
    return;
  }

  const int hour = ctx.now.hour;
  const int min = ctx.now.minute;
  const int sec = ctx.now.second;
  const uint32_t latest_in = std::max(last_user_touch_ms(), last_screen_wake_ms());
  const bool smooth_on =
      clock_smooth_sweep_active(rs.hw_usb_connected || rs.showcase.is_active(ctx.now_ms),
                                ctx.now_ms, latest_in, eff_clock_face);
  const float sec_f = smooth_second_with_frac(sec, ctx.now_ms, smooth_on);
  const float s_angle = (sec_f / 60.0f) * (2.0f * PI_F);

  char date_buf[32];
  ctx.now.strftime(date_buf, sizeof(date_buf), "%a, %b %d");

  if (is_ring_digital) {
    const float sin_s = std::sin(s_angle);
    const float cos_s = std::cos(s_angle);
    const float r_s_in  = canvas_f(200.0f);
    const float r_s_out = canvas_f(219.0f);
    const float cx_f = static_cast<float>(cx) + 0.5f;
    const float cy_f = static_cast<float>(cy) + 0.5f;
    ui::draw_aa_line(it,
                     cx_f + r_s_in * sin_s, cy_f - r_s_in * cos_s,
                     cx_f + r_s_out * sin_s, cy_f - r_s_out * cos_s,
                     std::max(2.4f, canvas_f(3.0f)), ctx.theme_primary);

    fast_print(it, cx, cy - canvas_px(86), ctx.font_date, ctx.theme_primary,
               UiTextAlign::CENTER, date_buf);

    char dig_buf[16];
    char ap_str[8] = "";
    format_time_hhmm(hour, min, is_24h_format, dig_buf, sizeof(dig_buf), ap_str);
    fast_print(it, cx, cy - canvas_px(8), ctx.font_time, ctx.col_white,
               UiTextAlign::CENTER, dig_buf);

    char sec_buf[32];
    if (is_24h_format) {
      std::snprintf(sec_buf, sizeof(sec_buf), "SEC  •  %02ds", sec);
    } else {
      std::snprintf(sec_buf, sizeof(sec_buf), "%s  •  %02ds", ap_str, sec);
    }
    fast_print(it, cx, cy + canvas_px(52), ctx.font_small, ctx.theme_secondary,
               UiTextAlign::CENTER, sec_buf);
  } else {
    fast_print(it, cx, cy - canvas_px(118), ctx.font_date, ctx.theme_primary,
               UiTextAlign::CENTER, date_buf);
    char time_buf[32];
    if (is_24h_format) {
      std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d", hour, min);
    } else {
      int h12 = hour % 12;
      if (h12 == 0) h12 = 12;
      std::snprintf(time_buf, sizeof(time_buf), "%d:%02d %s", h12, min, hour >= 12 ? "pm" : "am");
    }
    fast_print(it, cx, cy + canvas_px(82), ctx.font_date, ctx.col_slate,
               UiTextAlign::CENTER, time_buf);

    const float h_angle = (static_cast<float>(hour % 12) + static_cast<float>(min) / 60.0f + sec_f / 3600.0f) *
                          (2.0f * PI_F / 12.0f);
    const float m_angle = (static_cast<float>(min) + sec_f / 60.0f) * (2.0f * PI_F / 60.0f);
    const float cx_f = static_cast<float>(cx) + 0.5f;
    const float cy_f = static_cast<float>(cy) + 0.5f;

    ui::draw_aa_tapered_hand(it, cx_f, cy_f, h_angle,
                             canvas_f(16.0f), canvas_f(94.0f),
                             std::max(6.5f, canvas_f(8.2f)), std::max(2.6f, canvas_f(3.4f)),
                             ctx.col_white, ctx.theme_primary, true, true);

    ui::draw_aa_tapered_hand(it, cx_f, cy_f, m_angle,
                             canvas_f(20.0f), canvas_f(154.0f),
                             std::max(5.0f, canvas_f(6.2f)), std::max(2.0f, canvas_f(2.5f)),
                             ctx.col_slate, ctx.col_white, true, true);

    {
      const float sin_s = std::sin(s_angle);
      const float cos_s = std::cos(s_angle);
      const float tip_x  = cx_f + canvas_f(174.0f) * sin_s;
      const float tip_y  = cy_f - canvas_f(174.0f) * cos_s;
      const float tail_x = cx_f - canvas_f(32.0f) * sin_s;
      const float tail_y = cy_f + canvas_f(32.0f) * cos_s;
      const float tail_r = std::max(3.0f, canvas_f(4.0f));
      ui::draw_aa_line(it, tail_x + 1.0f, tail_y + 1.5f, tip_x + 1.0f, tip_y + 1.5f,
                       std::max(1.6f, canvas_f(2.0f)), kChromeShadow, 128);
      ui::draw_aa_disc(it, tail_x + 1.0f, tail_y + 1.5f, tail_r, kChromeShadow, 128);
      ui::draw_aa_line(it, tail_x, tail_y, tip_x, tip_y,
                       std::max(1.8f, canvas_f(2.0f)), ctx.col_red);
      ui::draw_aa_disc(it, tail_x, tail_y, tail_r, ctx.col_red);
    }

    ui::render_center_arbor_aa(it, cx, cy, ctx.col_white, ctx.col_red, ctx.col_dark_gray);
  }
}

}  // namespace latter_ui
