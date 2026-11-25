#include "ui_pages.h"
#include "ui_tile_grid.h"
#include "ui_touch_recognizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace latter_ui {
using namespace latter_core;

namespace {

inline constexpr float tool_art_f(float v466) noexcept {
  return v466 * (static_cast<float>(SCREEN_MIN_DIM) * (1.0f / 466.0f));
}
inline constexpr int tool_art_px(int v466) noexcept {
  return (v466 * SCREEN_MIN_DIM + 233) / 466;
}

struct CalculatorState {
  double prev_val = 0.0;
  int op = 0;  // 0 = none, 1 = +, 2 = -, 3 = *, 4 = /
  char entry[20] = "0";
  char history[48] = "";
  bool new_entry = true;
  bool error = false;
};

CalculatorState &calc_state() {
  static CalculatorState st;
  return st;
}

void format_calc_num_buf(double v, char *buf, size_t buf_len) {
  if (std::isnan(v) || std::isinf(v) || std::fabs(v) > 999999999999.0) {
    std::snprintf(buf, buf_len, "Error");
    calc_state().error = true;
    return;
  }
  if (std::fabs(v) < 1e-9) v = 0.0;
  if (std::fabs(v - std::round(v)) < 1e-7 && std::fabs(v) < 1e12) {
    std::snprintf(buf, buf_len, "%lld", static_cast<long long>(std::round(v)));
    return;
  }
  std::snprintf(buf, buf_len, "%.8g", v);
}


void render_calculator_subpage(Rgb565Surface &it, const UiFrameContext &ctx) {
  const auto &st = calc_state();
  const auto &ly = layout();

  const int hist_y = std::max(dp_px(22), ly.hdr_y - dp_px(8));
  const int entry_y = hist_y + dp_px(28);
  const int div_y = entry_y + dp_px(22);
  const int keys_top = div_y + dp_px(10);
  const int keys_bot = ly.back_y - dp_px(24);
  const int keys_avail = std::max(dp_px(160), keys_bot - keys_top);
  const int row_stride = std::min(dp_px(58), keys_avail / 5);
  const int eff_key_h = safe_clamp(row_stride - dp_px(8), dp_px(32), dp_px(48));
  const int grid_h = 4 * row_stride + eff_key_h;
  const int row0_cy = keys_top + std::max(0, (keys_avail - grid_h) / 2) + eff_key_h / 2;
  const int row4_cy = row0_cy + 4 * row_stride;

  const int keys_band_w = ly.band_safe_span((row0_cy + row4_cy) / 2, grid_h, dp_px(10));
  const int col_stride = std::min({dp_px(80), ly.max_content_w(360) / 4, keys_band_w / 4});
  const int eff_key_w = std::max(dp_px(40), col_stride - dp_px(8));
  const int grid_half_w = (3 * col_stride + eff_key_w) / 2;

  const int hist_right_x  = ctx.cx + std::min(grid_half_w, ly.safe_span(hist_y, dp_px(16)) / 2);
  const int entry_right_x = ctx.cx + std::min(grid_half_w, ly.safe_span(entry_y, dp_px(16)) / 2);
  const int div_half_w    = std::min(grid_half_w, ly.safe_span(div_y, dp_px(14)) / 2);

  if (st.history[0] != '\0') {
    if (backdrop_active()) {
      fast_print(it, hist_right_x + 1, hist_y + 1, ctx.font_small, kChromeShadow,
                 TextAlign::CENTER_RIGHT, st.history);
    }
    fast_print(it, hist_right_x, hist_y, ctx.font_small, ctx.col_slate,
               TextAlign::CENTER_RIGHT, st.history);
  }
  if (st.entry[0] != '\0') {
    const UiFontView entry_font = (std::strlen(st.entry) > 10) ? ctx.font_btn_bold : ctx.font_date;
    const UiColor entry_col = st.error ? ctx.col_red : ctx.col_white;
    if (backdrop_active()) {
      fast_print(it, entry_right_x + 1, entry_y + 1, entry_font, kChromeShadow,
                 TextAlign::CENTER_RIGHT, st.entry);
    }
    fast_print(it, entry_right_x, entry_y, entry_font, entry_col,
               TextAlign::CENTER_RIGHT, st.entry);
  }
  fast_line(it, ctx.cx - div_half_w, div_y, ctx.cx + div_half_w, div_y,
            kChromeCardBorder);

  static constexpr const char *kKeyLabels[5][4] = {
      {"C", "±", "%", "÷"},
      {"7", "8", "9", "×"},
      {"4", "5", "6", "-"},
      {"1", "2", "3", "+"},
      {"0", ".", "DEL", "="},
  };

  for (int r = 0; r < 5; r++) {
    const int ky_mid = row0_cy + r * row_stride;
    for (int c = 0; c < 4; c++) {
      const int kx_mid = ctx.cx - ((col_stride * 3) / 2) + (c * col_stride);

      UiColor bg_c     = kChromeGlassTint;
      UiColor border_c = kChromeCardBorder;
      const UiColor text_c = ctx.col_white;

      if (c == 3) {
        const bool op_selected = st.new_entry &&
                                 ((r == 0 && st.op == 4) || (r == 1 && st.op == 3) ||
                                  (r == 2 && st.op == 2) || (r == 3 && st.op == 1));
        if (op_selected) {
          bg_c     = ctx.col_orange;
          border_c = ctx.col_orange;
        } else {
          border_c = (r == 4) ? ctx.theme_primary : scale_color(ctx.col_orange, 0.72f);
        }
      } else if (r == 0 || (r == 4 && c == 2)) {
        border_c = kChromeCardBrdOn;
      }

      const bool is_minus_key = (r == 2 && c == 3);
      const bool is_del_key   = (r == 4 && c == 2);
      const uint16_t key_idx = static_cast<uint16_t>(r * 4 + c);
      ui::keypad_button(it, kx_mid, ky_mid, eff_key_w, eff_key_h,
                        is_minus_key ? nullptr : kKeyLabels[r][c],
                        bg_c, border_c, text_c,
                        is_del_key ? ctx.font_small : ctx.font_btn_bold,
                        WidgetId::NONE);
      if (is_minus_key) {
        ui::draw_icon_minus(it, kx_mid, ky_mid, std::max(7, dp_px(9)), text_c, true);
      }

      const int min_px = ly.min_row_hit_px();
      const int calc_pad = std::max(
          0, (std::max(min_px - (eff_key_w + 4), min_px - (eff_key_h + 6)) + 1) / 2);
      frame_hits().add_centered_rect(kx_mid, ky_mid, eff_key_w + 4, eff_key_h + 6,
                                     WidgetId::CALC_KEY_BASE + key_idx,
                                     kHitTap | kHitFocusSlot,
                                     static_cast<int16_t>(key_idx), calc_pad);
    }
  }

  render_back_button(it, ctx.cx, ctx.font_small, ly.back_y, "< Back");
}
}  // namespace

void reset_calculator_state() {
  auto &st = calc_state();
  st.prev_val = 0.0;
  st.op = 0;
  std::snprintf(st.entry, sizeof(st.entry), "0");
  st.history[0] = '\0';
  st.new_entry = true;
  st.error = false;
}

const char *calculator_display_text() noexcept {
  return calc_state().entry;
}

void handle_calculator_key_index(int key_idx) {
  if (key_idx < 0 || key_idx >= 20) return;
  const int r = key_idx / 4;
  const int c = key_idx % 4;

  auto &st = calc_state();
  if (st.error && !(r == 0 && c == 0)) {
    reset_calculator_state();
  }

  auto compute_op = [](double a, double b, int op_code) -> double {
    if (op_code == 1) return a + b;
    if (op_code == 2) return a - b;
    if (op_code == 3) return a * b;
    if (op_code == 4) return (std::fabs(b) > 1e-12) ? (a / b) : NAN;
    return b;
  };
  auto op_to_str = [](int op_code) -> const char * {
    if (op_code == 1) return "+";
    if (op_code == 2) return "-";
    if (op_code == 3) return "×";
    if (op_code == 4) return "÷";
    return "";
  };

  if (r == 0 && c == 0) {
    reset_calculator_state();
    return;
  }
  if (r == 0 && c == 1) {
    if (std::strcmp(st.entry, "0") != 0 && !st.error) {
      const size_t len = std::strlen(st.entry);
      if (st.entry[0] == '-') {
        std::memmove(st.entry, st.entry + 1, len);
      } else if (len + 1 < sizeof(st.entry)) {
        std::memmove(st.entry + 1, st.entry, len + 1);
        st.entry[0] = '-';
      }
    }
    return;
  }
  if (r == 0 && c == 2) {
    const double cur = std::strtod(st.entry, nullptr);
    if (st.op == 1 || st.op == 2) {
      format_calc_num_buf(st.prev_val * (cur / 100.0), st.entry, sizeof(st.entry));
      st.new_entry = false;
    } else {
      format_calc_num_buf(cur / 100.0, st.entry, sizeof(st.entry));
      st.new_entry = true;
    }
    return;
  }
  if (r == 4 && c == 2) {
    if (!st.new_entry && !st.error) {
      const size_t len = std::strlen(st.entry);
      if (len <= 1 || (len == 2 && st.entry[0] == '-')) {
        std::snprintf(st.entry, sizeof(st.entry), "0");
      } else {
        st.entry[len - 1] = '\0';
      }
    }
    return;
  }
  if (c == 3) {
    const int new_op = (r == 0) ? 4 : (r == 1) ? 3 : (r == 2) ? 2 : (r == 3) ? 1 : 5;
    const double cur = (st.entry[0] != '\0') ? std::strtod(st.entry, nullptr) : st.prev_val;

    if (new_op == 5) {
      if (st.op != 0) {
        char prev_b[20];
        format_calc_num_buf(st.prev_val, prev_b, sizeof(prev_b));
        std::snprintf(st.history, sizeof(st.history), "%.18s %.3s %.20s =", prev_b, op_to_str(st.op), st.entry);
        const double res = compute_op(st.prev_val, cur, st.op);
        format_calc_num_buf(res, st.entry, sizeof(st.entry));
        st.op = 0;
        st.new_entry = true;
      }
    } else {
      if (st.op != 0 && !st.new_entry && st.entry[0] != '\0') {
        const double res = compute_op(st.prev_val, cur, st.op);
        if (std::isnan(res) || std::isinf(res)) {
          format_calc_num_buf(res, st.entry, sizeof(st.entry));
          st.op = 0;
          st.new_entry = true;
          return;
        }
        st.prev_val = res;
      } else {
        st.prev_val = cur;
      }
      st.op = new_op;
      st.new_entry = true;
      char prev_b[20];
      format_calc_num_buf(st.prev_val, prev_b, sizeof(prev_b));
      std::snprintf(st.history, sizeof(st.history), "%s %s", prev_b, op_to_str(st.op));
      std::snprintf(st.entry, sizeof(st.entry), "%s", prev_b);
    }
    return;
  }
  if (r == 4 && c == 1) {
    if (st.new_entry || st.entry[0] == '\0') {
      std::snprintf(st.entry, sizeof(st.entry), "0.");
      st.new_entry = false;
    } else {
      const size_t len = std::strlen(st.entry);
      if (std::strchr(st.entry, '.') == nullptr && len < 11) {
        st.entry[len] = '.';
        st.entry[len + 1] = '\0';
      }
    }
    return;
  }

  int digit = 0;
  if (r == 1) digit = 7 + c;
  else if (r == 2) digit = 4 + c;
  else if (r == 3) digit = 1 + c;
  else if (r == 4 && c == 0) digit = 0;

  if (st.new_entry || std::strcmp(st.entry, "0") == 0) {
    std::snprintf(st.entry, sizeof(st.entry), "%d", digit);
    st.new_entry = false;
  } else {
    const size_t len = std::strlen(st.entry);
    if (len < 10) {
      st.entry[len] = static_cast<char>('0' + digit);
      st.entry[len + 1] = '\0';
    }
  }
}


DiceRollState &dice_state() { static DiceRollState st; return st; }

void trigger_dice_roll(uint32_t now_ms, uint32_t rand_a, uint32_t rand_b) {
  auto &ds = dice_state();
  ds.val_1 = static_cast<int>((rand_a % 6U) + 1U);
  ds.val_2 = static_cast<int>((rand_b % 6U) + 1U);
  ds.anim_active = true;
  ds.anim_start_ms = now_ms;
}

CoinFlipState &coin_state() { static CoinFlipState st; return st; }

void trigger_coin_flip(uint32_t now_ms, uint32_t rand_val) {
  auto &cs = coin_state();
  cs.side = static_cast<int>(rand_val & 1U);
  cs.anim_active = true;
  cs.anim_start_ms = now_ms;
}

namespace {

void render_dice_subpage(Rgb565Surface &it, const UiFrameContext &ctx) {
  auto &ds = dice_state();
  const auto &ly = layout();
  int cur_val_1 = ds.val_1, cur_val_2 = ds.val_2;
  int hop_y1 = 0, hop_y2 = 0, jostle_x1 = 0, jostle_x2 = 0, shadow_extra = 0;
  float anim_p = 1.0f;

  if (ds.anim_active) {
    const uint32_t elapsed = ctx.now_ms - ds.anim_start_ms;
    static constexpr uint32_t kRollDurMs = 680U;
    if (elapsed >= kRollDurMs) {
      ds.anim_active = false;
    } else {
      anim_p = static_cast<float>(elapsed) / static_cast<float>(kRollDurMs);
      const float damp = 1.0f - anim_p;
      hop_y1 = -static_cast<int>(std::fabs(fast_sin(anim_p * PI_F * 2.2f)) * tool_art_f(34.0f) * damp);
      hop_y2 = -static_cast<int>(std::fabs(fast_sin(anim_p * PI_F * 2.2f + 0.7f)) * tool_art_f(32.0f) * damp);
      jostle_x1 = static_cast<int>(fast_sin(anim_p * PI_F * 6.0f) * tool_art_f(8.0f) * damp);
      jostle_x2 = -static_cast<int>(fast_cos(anim_p * PI_F * 6.0f) * tool_art_f(8.0f) * damp);
      shadow_extra = static_cast<int>(tool_art_f(6.0f) * damp);
      const uint32_t step = elapsed / 55U;
      cur_val_1 = 1 + (static_cast<int>(hash2d_01(static_cast<int>(step), 17) * 60.0f) % 6);
      cur_val_2 = 1 + (static_cast<int>(hash2d_01(static_cast<int>(step) + 31, 53) * 60.0f) % 6);
    }
  }

  render_page_header(it, ctx.cx, "DICE ROLL", ctx.font_small, ctx.col_white);
  const int dice_base_y = ly.hdr_y + ((ly.back_y - ly.hdr_y) * 42) / 100;
  const int die_off = tool_art_px(75);
  const int d1_cx = ctx.cx - die_off + jostle_x1, d2_cx = ctx.cx + die_off + jostle_x2;
  const int d1_cy = dice_base_y + hop_y1, d2_cy = dice_base_y + hop_y2;
  const int d_size = tool_art_px(132), cr = std::max(6, tool_art_px(16));

  auto draw_tumbling_cube_3d = [&](int center_x, int center_y, int hop_y, float rx, float ry, float rz) {
    static constexpr float kCubeV[8][3] = {
        {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f,  1.0f}, {1.0f, -1.0f,  1.0f}, {1.0f, 1.0f,  1.0f}, {-1.0f, 1.0f,  1.0f}};
    static constexpr uint8_t kCubeF[6][4] = {
        {0, 1, 2, 3}, {5, 4, 7, 6}, {4, 5, 1, 0}, {3, 2, 6, 7}, {4, 0, 3, 7}, {1, 5, 6, 2}};
    const float half_s = tool_art_f(46.0f), fov = tool_art_f(260.0f), z_dist = tool_art_f(280.0f);
    ProjectedPoint pts[8];
    Vec3 rv[8];
    for (int i = 0; i < 8; i++) {
      rv[i] = rotate_3d_xyz({kCubeV[i][0] * half_s, kCubeV[i][1] * half_s, kCubeV[i][2] * half_s}, rx, ry, rz);
      pts[i] = project_3d(rv[i], center_x, center_y, fov, z_dist);
    }
    fast_filled_circle(it, center_x + tool_art_px(6), center_y + tool_art_px(18) - hop_y / 2, tool_art_px(40), kChromeModalBg);
    const UiColor c_border(158, 150, 138), c_pip(25, 28, 35);
    const int pip_r3d = std::max(2, tool_art_px(3));
    for (int f = 0; f < 6; f++) {
      const uint8_t idx[4] = {kCubeF[f][0], kCubeF[f][1], kCubeF[f][2], kCubeF[f][3]};
      const uint8_t i0 = idx[0], i1 = idx[1], i2 = idx[2], i3 = idx[3];
      const int cross = (pts[i1].x - pts[i0].x) * (pts[i2].y - pts[i0].y) -
                        (pts[i1].y - pts[i0].y) * (pts[i2].x - pts[i0].x);
      if (cross <= 0) continue;
      const float inv_s = 0.25f / std::max(1.0f, half_s);
      const float nx = (rv[i0].x + rv[i1].x + rv[i2].x + rv[i3].x) * inv_s;
      const float ny = (rv[i0].y + rv[i1].y + rv[i2].y + rv[i3].y) * inv_s;
      const float nz = (rv[i0].z + rv[i1].z + rv[i2].z + rv[i3].z) * inv_s;
      const float light = std::clamp(0.84f + (-nz * 0.12f - ny * 0.10f - nx * 0.06f), 0.74f, 1.0f);
      const UiColor face_col(static_cast<uint8_t>(248.0f * light),
                             static_cast<uint8_t>(244.0f * light),
                             static_cast<uint8_t>(236.0f * light));
      fast_filled_triangle(it, pts[i0].x, pts[i0].y, pts[i1].x, pts[i1].y, pts[i2].x, pts[i2].y, face_col);
      fast_filled_triangle(it, pts[i0].x, pts[i0].y, pts[i2].x, pts[i2].y, pts[i3].x, pts[i3].y, face_col);
      for (int e = 0; e < 4; ++e) {
        fast_line(it, pts[idx[e]].x, pts[idx[e]].y, pts[idx[(e + 1) & 3]].x, pts[idx[(e + 1) & 3]].y, c_border);
      }
      auto draw_face_pip = [&](float u, float v) {
        const float px = (1.0f - u) * (1.0f - v) * static_cast<float>(pts[i0].x) + u * (1.0f - v) * static_cast<float>(pts[i1].x) +
                         u * v * static_cast<float>(pts[i2].x) + (1.0f - u) * v * static_cast<float>(pts[i3].x);
        const float py = (1.0f - u) * (1.0f - v) * static_cast<float>(pts[i0].y) + u * (1.0f - v) * static_cast<float>(pts[i1].y) +
                         u * v * static_cast<float>(pts[i2].y) + (1.0f - u) * v * static_cast<float>(pts[i3].y);
        fast_filled_circle(it, static_cast<int>(px), static_cast<int>(py), pip_r3d, c_pip);
      };
      if (f == 0 || f == 4 || f == 3) draw_face_pip(0.50f, 0.50f);
      if (f >= 1) { draw_face_pip(0.26f, 0.26f); draw_face_pip(0.74f, 0.74f); }
      if (f == 1 || f == 3 || f == 5) { draw_face_pip(0.74f, 0.26f); draw_face_pip(0.26f, 0.74f); }
      if (f == 1) { draw_face_pip(0.26f, 0.50f); draw_face_pip(0.74f, 0.50f); }
    }
  };

  auto draw_rounded_d6 = [&](int center_x, int center_y, int val) {
    const int x0 = center_x - d_size / 2, y0 = center_y - d_size / 2;
    auto fill_rbox = [&](int rx, int ry, UiColor col) {
      fill_rect_row_major(it, rx + cr, ry, d_size - 2 * cr, d_size, col);
      fill_rect_row_major(it, rx, ry + cr, d_size, d_size - 2 * cr, col);
      for (int cy_c : {ry + cr, ry + d_size - cr}) {
        fast_filled_circle(it, rx + cr, cy_c, cr, col);
        fast_filled_circle(it, rx + d_size - cr, cy_c, cr, col);
      }
    };
    fill_rbox(x0 + tool_art_px(6) + shadow_extra, y0 + tool_art_px(8) + shadow_extra, kChromeModalBg);
    fill_rbox(x0, y0, UiColor(248, 246, 240));
    const UiColor border_col(182, 176, 164), pip_c(25, 28, 35), pip_rim(85, 92, 105);
    fast_line(it, x0 + cr, y0 + 1, x0 + d_size - cr, y0 + 1, ctx.col_white);
    fast_line(it, x0 + 1, y0 + cr, x0 + 1, y0 + d_size - cr, ctx.col_white);
    fast_line(it, x0 + cr, y0 + d_size - 1, x0 + d_size - cr, y0 + d_size - 1, border_col);
    fast_line(it, x0 + d_size - 1, y0 + cr, x0 + d_size - 1, y0 + d_size - cr, border_col);
    const int pr = std::max(5, tool_art_px(9)), p_dist = tool_art_px(34);
    auto draw_pip = [&](int px, int py) {
      fast_filled_circle(it, px, py, pr, pip_c);
      fast_pixel(it, px + pr / 2, py + pr / 2 + 1, pip_rim);
      fast_pixel(it, px + pr / 2 + 1, py + pr / 2, pip_rim);
    };
    if (val == 1 || val == 3 || val == 5) draw_pip(center_x, center_y);
    if (val >= 2) { draw_pip(center_x - p_dist, center_y - p_dist); draw_pip(center_x + p_dist, center_y + p_dist); }
    if (val >= 4) { draw_pip(center_x + p_dist, center_y - p_dist); draw_pip(center_x - p_dist, center_y + p_dist); }
    if (val == 6) { draw_pip(center_x - p_dist, center_y); draw_pip(center_x + p_dist, center_y); }
  };

  if (ds.anim_active && anim_p < 0.80f) {
    const float spin = anim_p * 7.8f;
    draw_tumbling_cube_3d(d1_cx, d1_cy, hop_y1, spin * 1.1f, spin * 1.4f + 0.5f, spin * 0.7f);
    draw_tumbling_cube_3d(d2_cx, d2_cy, hop_y2, -spin * 1.2f + 0.8f, spin * 1.0f, -spin * 0.9f);
  } else {
    draw_rounded_d6(d1_cx, d1_cy, ds.val_1);
    draw_rounded_d6(d2_cx, d2_cy, ds.val_2);
  }

  char total_buf[32];
  std::snprintf(total_buf, sizeof(total_buf), "TOTAL: %d", cur_val_1 + cur_val_2);
  const char *lbl = ds.anim_active ? "ROLLING..." : total_buf;
  const UiColor lbl_col = ds.anim_active ? ctx.theme_secondary : ctx.col_white;
  const int total_y = std::min(ly.back_y - dp_px(40), dice_base_y + d_size / 2 + dp_px(44));
  if (backdrop_active()) fast_print(it, ctx.cx + 1, total_y + 1, ctx.font_date, kChromeShadow, TextAlign::CENTER, lbl);
  fast_print(it, ctx.cx, total_y, ctx.font_date, lbl_col, TextAlign::CENTER, lbl);
  frame_hits().add_rect(0, 0, SCREEN_W, ly.back_y - dp_px(24), WidgetId::DICE_ROLL_BTN, kHitTap | kHitFocusSlot);
  render_back_button(it, ctx.cx, ctx.font_small, ly.back_y, "< Back");
}

void render_coin_flip_subpage(Rgb565Surface &it, const UiFrameContext &ctx) {
  auto &cs = coin_state();
  const auto &ly = layout();
  float coin_sy = 1.0f, y_hop = 0.0f, z_zoom = 1.0f, raw_sin = 0.25f;
  bool show_heads = (cs.side == 0), is_edge_flash = false;

  if (cs.anim_active) {
    static constexpr uint32_t kTossDurationMs = 820U;
    const uint32_t el_ms = (ctx.now_ms >= cs.anim_start_ms) ? (ctx.now_ms - cs.anim_start_ms) : kTossDurationMs;
    if (el_ms >= kTossDurationMs) {
      cs.anim_active = false;
    } else {
      const float p = static_cast<float>(el_ms) / static_cast<float>(kTossDurationMs);
      const float arc = 4.0f * p * (1.0f - p);
      y_hop = -tool_art_f(74.0f) * arc;
      z_zoom = 1.0f + 0.18f * arc;
      float raw_cos = 1.0f;
      fast_sincos((p * (1.18f - 0.18f * p)) * 5.0f * PI_F, raw_sin, raw_cos);
      coin_sy = std::max(0.05f, std::fabs(raw_cos));
      is_edge_flash = (coin_sy < 0.16f);
      show_heads = ((raw_cos < 0.0f) ? cs.side : (1 - cs.side)) == 0;
    }
  }

  render_page_header(it, ctx.cx, "COIN FLIP", ctx.font_small, ctx.col_white);
  const int cx = ctx.cx;
  const int coin_base_y = ly.hdr_y + ((ly.back_y - ly.hdr_y) * 48) / 100;
  const int coin_cy = coin_base_y + static_cast<int>(y_hop);
  const float s_zoom = tool_art_f(z_zoom);
  const int r_outer = std::max(16, static_cast<int>(108.0f * s_zoom));

  const int sh_base_y = std::min(ly.back_y - dp_px(32), coin_base_y + tool_art_px(96));
  const float shadow_scale = std::max(0.32f, 1.0f - (-y_hop / std::max(1.0f, tool_art_f(95.0f))));
  const int sh_rx = static_cast<int>(tool_art_f(88.0f) * shadow_scale);
  const int sh_ry = std::max(3, static_cast<int>(tool_art_f(16.0f + 10.0f * coin_sy) * shadow_scale));
  for (int sdy = -sh_ry; sdy <= sh_ry; sdy++) {
    const int sdx = (sh_rx * chord_half_span_int(sh_ry, sdy)) / sh_ry;
    fast_hline(it, cx - sdx, sh_base_y + sdy, 2 * sdx + 1, kChromePauseGlyph);
  }

  const UiColor col_rim_dark(64, 72, 84), col_rim_mid(134, 144, 160), col_bevel_hi(248, 252, 255);
  const UiColor col_groove_sh(82, 92, 108), col_relief_shadow(52, 60, 72), col_relief_hi(252, 254, 255);
  const UiColor col_engrave_recess(74, 84, 98);
  const float spec_center = cs.anim_active ? (0.68f * raw_sin) : -0.28f;
  const int b_outer = std::max(4, static_cast<int>(static_cast<float>(r_outer) * coin_sy));
  const float edge_signed = cs.anim_active ? (16.0f * raw_sin) : 4.0f;
  const bool edge_on_top = (edge_signed < 0.0f);
  const int edge_h = cs.anim_active ? tool_art_px(static_cast<int>(std::lround(std::fabs(edge_signed)))) : std::max(3, tool_art_px(4));

  for (int dy = 1; edge_h > 0 && dy <= b_outer + edge_h; dy++) {
    const int src_dy = (dy > edge_h) ? (dy - edge_h) : 0;
    const int dx_outer = (r_outer * chord_half_span_int(b_outer, src_dy)) / b_outer;
    const UiColor ec = (dy >= b_outer + edge_h - 1) ? col_rim_dark : (is_edge_flash ? UiColor(202, 212, 228) : col_rim_mid);
    fast_hline(it, cx - dx_outer, edge_on_top ? (coin_cy - dy) : (coin_cy + dy), 2 * dx_outer + 1, ec);
  }

  const int dish_in = std::max(5, tool_art_px(8));
  for (int dy = -b_outer; dy <= b_outer; dy++) {
    const int dx_outer = (r_outer * chord_half_span_int(b_outer, dy)) / b_outer;
    const float u_y = static_cast<float>(dy) / static_cast<float>(b_outer);
    const float sheen = (is_edge_flash ? 215.0f : 156.0f) - u_y * 18.0f + std::max(0.0f, 1.0f - std::fabs(u_y - spec_center) * 2.1f) * 42.0f;
    fast_hline(it, cx - dx_outer, coin_cy + dy, 2 * dx_outer + 1,
               UiColor(static_cast<uint8_t>(std::min(248.0f, sheen * 0.96f)),
                       static_cast<uint8_t>(std::min(250.0f, sheen * 0.99f)),
                       static_cast<uint8_t>(std::min(255.0f, sheen * 1.04f))));
    fast_pixel(it, cx - dx_outer, coin_cy + dy, col_rim_dark);
    fast_pixel(it, cx + dx_outer, coin_cy + dy, col_rim_dark);
    if (dx_outer > 4) {
      fast_pixel(it, cx - dx_outer + 1, coin_cy + dy, col_bevel_hi);
      fast_pixel(it, cx - dx_outer + 2, coin_cy + dy, col_bevel_hi);
      fast_pixel(it, cx + dx_outer - 1, coin_cy + dy, col_rim_mid);
      fast_pixel(it, cx + dx_outer - 2, coin_cy + dy, col_rim_mid);
    }
    if (dx_outer > dish_in + 3 && !is_edge_flash) {
      const UiColor dish_l = (dy < 0) ? col_groove_sh : col_bevel_hi;
      const UiColor dish_r = (dy < 0) ? col_bevel_hi : col_groove_sh;
      fast_pixel(it, cx - dx_outer + dish_in, coin_cy + dy, dish_l);
      fast_pixel(it, cx - dx_outer + dish_in + 1, coin_cy + dy, dish_l);
      fast_pixel(it, cx + dx_outer - dish_in, coin_cy + dy, dish_r);
      fast_pixel(it, cx + dx_outer - dish_in - 1, coin_cy + dy, dish_r);
    }
  }

  if (edge_h >= 2) {
    const int reed_step = std::max(4, tool_art_px(is_edge_flash ? 5 : 6));
    for (int rx_off = -r_outer + 6; rx_off <= r_outer - 6; rx_off += reed_step) {
      const int y_off = (b_outer * chord_half_span_int(r_outer, rx_off)) / r_outer;
      const UiColor reed_c = ((rx_off / reed_step) & 1) ? col_bevel_hi : col_rim_dark;
      if (edge_on_top) fast_line(it, cx + rx_off, coin_cy - y_off - edge_h + 1, cx + rx_off, coin_cy - y_off - 1, reed_c);
      else fast_line(it, cx + rx_off, coin_cy + y_off + 1, cx + rx_off, coin_cy + y_off + edge_h - 1, reed_c);
    }
  }

  if (coin_sy >= 0.96f && !cs.anim_active) {
    fast_circle(it, cx, coin_cy, r_outer, col_rim_dark);
    fast_circle(it, cx, coin_cy, r_outer - 1, col_bevel_hi);
    fast_circle(it, cx, coin_cy, r_outer - dish_in, col_groove_sh);
    fast_circle(it, cx, coin_cy, r_outer - dish_in - 1, col_bevel_hi);
  }

  if (coin_sy > 0.22f) {
    const int hx = cx, hy = coin_cy - static_cast<int>(18.0f * coin_sy * s_zoom), hw = static_cast<int>(62.0f * s_zoom);
    const int y_top = hy - static_cast<int>(56.0f * coin_sy * s_zoom);
    const int y_eave = hy - static_cast<int>(2.0f * coin_sy * s_zoom);
    const int y_bot = hy + static_cast<int>(54.0f * coin_sy * s_zoom);
    auto house_hw = [&](int y) -> int {
      if (y < y_eave) {
        return (y_eave > y_top) ? static_cast<int>(static_cast<float>(y - y_top) / static_cast<float>(y_eave - y_top) * static_cast<float>(hw)) : 0;
      }
      const int dy_b = y_bot - y;
      return (dy_b < 5 && coin_sy > 0.75f) ? (hw - (5 - dy_b)) : hw;
    };
    for (int y = y_top; y <= y_bot; y++) {
      const int cur_hw = house_hw(y);
      if (cur_hw > 0) {
        fast_hline(it, hx - cur_hw + 2, y + 2, 2 * cur_hw + 2, col_relief_shadow);
        fast_hline(it, hx - cur_hw - 2, y - 1, 2 * cur_hw + 1, col_relief_hi);
      }
    }
    for (int y = y_top; y <= y_bot; y++) {
      const int cur_hw = house_hw(y);
      if (cur_hw > 0) {
        const uint8_t rl = static_cast<uint8_t>(232.0f - (static_cast<float>(y - y_top) / static_cast<float>(y_bot - y_top + 1)) * 26.0f);
        fast_hline(it, hx - cur_hw, y, 2 * cur_hw + 1, UiColor(rl, static_cast<uint8_t>(rl + 2), static_cast<uint8_t>(std::min(255.0f, rl + 7.0f))));
        fast_pixel(it, hx - cur_hw, y, col_groove_sh);
        fast_pixel(it, hx + cur_hw, y, col_relief_shadow);
      }
    }

    if (show_heads) {
      const int eye_dx = static_cast<int>(26.0f * s_zoom), eye_y = hy + static_cast<int>(7.0f * coin_sy * s_zoom);
      const int eye_r = std::max(2, static_cast<int>(8.0f * coin_sy * s_zoom));
      for (int ex : {hx - eye_dx, hx + eye_dx}) fast_filled_circle(it, ex + 1, eye_y + 1, eye_r, col_relief_hi);
      for (int ex : {hx - eye_dx, hx + eye_dx}) fast_filled_circle(it, ex, eye_y, eye_r, col_relief_shadow);
      for (int ex : {hx - eye_dx, hx + eye_dx}) fast_filled_circle(it, ex + 1, eye_y + 1, std::max(1, eye_r - 2), col_engrave_recess);
      const int smile_top_y = hy + static_cast<int>(22.0f * coin_sy * s_zoom), smile_mid_y = hy + static_cast<int>(28.0f * coin_sy * s_zoom);
      const int smile_bot_y = hy + static_cast<int>(40.0f * coin_sy * s_zoom), smile_hw = static_cast<int>(20.0f * s_zoom);
      for (int y = smile_top_y; y <= smile_mid_y; y++) {
        for (int t = -2; t <= 1; t++) {
          fast_pixel(it, hx - smile_hw + t + 1, y + 1, col_relief_hi);
          fast_pixel(it, hx + smile_hw + t + 1, y + 1, col_relief_hi);
          fast_pixel(it, hx - smile_hw + t, y, col_relief_shadow);
          fast_pixel(it, hx + smile_hw + t, y, col_relief_shadow);
        }
      }
      for (int i = 0; i <= 32; i++) {
        const float sa = static_cast<float>(i) * (PI_F / 32.0f);
        const int sx = hx - static_cast<int>(static_cast<float>(smile_hw) * fast_cos(sa));
        const int sy = smile_mid_y + static_cast<int>(static_cast<float>(smile_bot_y - smile_mid_y) * fast_sin(sa));
        for (int t = -1; t <= 1; t++) {
          fast_pixel(it, sx + 1, sy + t + 1, col_relief_hi);
          fast_pixel(it, sx, sy + t, col_relief_shadow);
          fast_pixel(it, sx + t, sy, col_relief_shadow);
        }
      }
    } else {
      const int h_total = y_bot - y_top, branch_x = static_cast<int>(31.0f * s_zoom);
      const int top_node_y = y_top + static_cast<int>(0.32f * static_cast<float>(h_total));
      const int right_node_x = hx + branch_x, right_node_y = y_top + static_cast<int>(0.59f * static_cast<float>(h_total));
      const int left_node_x = hx - branch_x, left_node_y = y_top + static_cast<int>(0.73f * static_cast<float>(h_total));
      const int node_r = std::max(3, static_cast<int>(10.0f * coin_sy * s_zoom));
      const int line_hw = std::max(1, static_cast<int>(3.0f * coin_sy * s_zoom));
      const int branch_drop = static_cast<int>(29.0f * coin_sy * s_zoom);
      auto draw_tails_layer = [&](int dx, int dy_line, int dy_circ, UiColor col) {
        for (int w = -line_hw; w <= line_hw; w++) {
          fast_line(it, hx + w + dx, top_node_y + dy_line, hx + w + dx, y_bot, col);
          fast_line(it, hx + w + dx, right_node_y + branch_drop + dy_line, right_node_x + w + dx, right_node_y + dy_line, col);
          fast_line(it, hx + w + dx, left_node_y + branch_drop + dy_line, left_node_x + w + dx, left_node_y + dy_line, col);
        }
        fast_filled_circle(it, hx + dx, top_node_y + dy_circ, node_r, col);
        fast_filled_circle(it, right_node_x + dx, right_node_y + dy_circ, node_r, col);
        fast_filled_circle(it, left_node_x + dx, left_node_y + dy_circ, node_r, col);
      };
      draw_tails_layer(2, 1, 2, col_relief_hi);
      draw_tails_layer(0, 0, 0, col_relief_shadow);
    }

    if (coin_sy > 0.76f) {
      const UiFontView coin_lbl_font = (ly.size_class == SizeClass::Compact) ? ctx.font_small : ctx.font_date;
      const char *face_lbl = show_heads ? "HEADS" : "TAILS";
      const int ty = coin_cy + static_cast<int>(74.0f * coin_sy * s_zoom);
      if (!cs.anim_active) {
        fast_print(it, cx + 2, ty + 2, coin_lbl_font, col_relief_shadow, TextAlign::CENTER, face_lbl);
        fast_print(it, cx - 1, ty - 1, coin_lbl_font, col_relief_hi, TextAlign::CENTER, face_lbl);
      }
      fast_print(it, cx, ty, coin_lbl_font, UiColor(220, 228, 240), TextAlign::CENTER, face_lbl);
    }
  }

  frame_hits().add_rect(0, 0, SCREEN_W, ly.back_y - dp_px(24), WidgetId::COIN_FLIP_BTN, kHitTap | kHitFocusSlot);
  render_back_button(it, cx, ctx.font_small, ly.back_y, "< Back");
}



RouteId tool_subpage_to_route(int app_id) {
#if LATTER_ROUND_DISPLAY
  if (app_id == ToolsSubpage::STOPWATCH) return RouteId::TOOLS_STOPWATCH;
#endif
  if (app_id == ToolsSubpage::DICE) return RouteId::TOOLS_DICE;
  if (app_id == ToolsSubpage::FLASHLIGHT) return RouteId::TOOLS_FLASHLIGHT;
  if (app_id == ToolsSubpage::CALCULATOR) return RouteId::TOOLS_CALCULATOR;
  if (app_id == ToolsSubpage::LEVEL) return RouteId::TOOLS_LEVEL;
  if (app_id == ToolsSubpage::COIN_FLIP) return RouteId::TOOLS_COIN_FLIP;
  return RouteId::APP_TOOLS;
}

void draw_tool_tile_icon(Rgb565Surface &it, int ix, int iy, int ir,
                         UiColor col, bool shadow, void *user_ctx) {
  const int app_id = static_cast<int>(reinterpret_cast<intptr_t>(user_ctx));
  ui::draw_route_icon(it, tool_subpage_to_route(app_id), ix, iy, ir, col, kChromeGlassTint, shadow);
}

void render_tools_launcher(Rgb565Surface &it, const UiFrameContext &ctx,
                           const DeviceRuntimeState &rs) {
  ToolsLauncherItemSpec items[kMaxToolsLauncherItems]{};
  const int total_items = collect_tools_launcher_items(items, device_prefs());

  if (total_items <= 0) {
    runtime_state().nav.tools_menu_page = 0;
    ui::TileGridPageSpec empty_spec{};
    empty_spec.page_id = PageId::TOOLS;
    empty_spec.card_widget_base = WidgetId::TOOLS_CARD_BASE;
    empty_spec.page_title_upper = "TOOLS";
    empty_spec.empty_message = "No tools enabled";
    ui::render_tile_grid_page(it, ctx, empty_spec);
    return;
  }

  const int cols = tile_grid_cols_for(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0);
  const int rows = tile_grid_rows_for(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0);
  const int per_page = std::max(1, cols * rows);
  const int total_pages = (total_items <= per_page) ? 1 : ((total_items + per_page - 1) / per_page);
  const int cur_pg = std::clamp(rs.nav.tools_menu_page, 0, total_pages - 1);
  if (rs.nav.tools_menu_page != cur_pg) {
    runtime_state().nav.tools_menu_page = cur_pg;
  }

  ui::TileGridEntry entries[kMaxToolsLauncherItems]{};
  int entry_count = 0;
  for (int i = 0; i < total_items && entry_count < kMaxToolsLauncherItems; ++i) {
    const int app_id = items[i].id;
    if (app_id == 0) continue;
    const int pg = i / per_page;
    const int local = i % per_page;
    ui::TileGridEntry e{};
    e.label = items[i].label;
    e.active = false;
    e.disabled = false;
    e.state = -1;
    e.inverted_badge = false;
    e.has_accent = false;
    e.accent_c = ctx.theme_primary;
    e.text_c = ctx.col_white;
    e.draw_icon_fn = draw_tool_tile_icon;
    e.icon_ctx = reinterpret_cast<void *>(static_cast<intptr_t>(app_id));
    e.widget_param = static_cast<int16_t>(app_id);
    e.col_span = 1U;
    e.row_span = 1U;
    e.col = static_cast<int8_t>(local % cols);
    e.row = static_cast<int8_t>(local / cols);
    e.visual_page = static_cast<int8_t>(pg);
    entries[entry_count++] = e;
  }

  ui::TileGridPageSpec spec{};
  spec.page_id = PageId::TOOLS;
  spec.card_widget_base = WidgetId::TOOLS_CARD_BASE;
  spec.cur_page = cur_pg;
  spec.total_pages = total_pages;
  spec.page_title_upper = "TOOLS";
  spec.empty_message = "No tools enabled";
  spec.entries = entries;
  spec.entry_count = entry_count;
  ui::render_tile_grid_page(it, ctx, spec);
}

#if LATTER_ROUND_DISPLAY
void render_stopwatch_subpage(Rgb565Surface &it, const UiFrameContext &ctx,
                              const StopwatchRuntimeState &sw) {
  const uint32_t total_ms = sw.running ? (ctx.now_ms - sw.start_time_ms) : sw.elapsed_ms;
  const int mins   = static_cast<int>((total_ms / 60000U) % 60U);
  const int secs   = static_cast<int>((total_ms / 1000U) % 60U);
  const int tenths = static_cast<int>((total_ms / 100U) % 10U);

  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const float cx_f = static_cast<float>(cx) + 0.5f;
  const float cy_f = static_cast<float>(cy) + 0.5f;

  fast_circle(it, cx, cy, tool_art_px(230), ctx.col_white);
  fast_circle(it, cx, cy, tool_art_px(228), ctx.theme_secondary);
  ui::render_analog_chapter_ring(it, cx, cy, tool_art_px(227),
                                 ctx.theme_secondary, ctx.col_white, ctx.col_slate, false);
  fast_circle(it, cx, cy, tool_art_px(196), ctx.col_dark_gray);

  static constexpr int kChronoTicks[11] = {0, 5, 10, 15, 20, 25, 35, 40, 45, 50, 55};
  static constexpr const char *kChronoStrs[11] = {"0", "5", "10", "15", "20", "25", "35", "40", "45", "50", "55"};
  for (int i = 0; i < 11; i++) {
    const int t_idx = kChronoTicks[i];
    const int nx = cx + static_cast<int>(tool_art_f(172.0f) * DIAL_SIN[t_idx]);
    const int ny = cy - static_cast<int>(tool_art_f(172.0f) * DIAL_COS[t_idx]);
    if (backdrop_active()) {
      fast_print(it, nx + 1, ny + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, kChronoStrs[i]);
    }
    fast_print(it, nx, ny, ctx.font_small, ctx.col_white, TextAlign::CENTER, kChronoStrs[i]);
  }

  char main_buf[32];
  std::snprintf(main_buf, sizeof(main_buf), "%02d:%02d.%d", mins, secs, tenths);
  if (backdrop_active()) {
    fast_print(it, cx + 1, tool_art_px(238) + 1, ctx.font_date, kChromeShadow, TextAlign::CENTER, main_buf);
  }
  fast_print(it, cx, tool_art_px(238), ctx.font_date, ctx.col_white, TextAlign::CENTER, main_buf);

  if (sw.lap_ms > 0) {
    const int l_mins   = static_cast<int>((sw.lap_ms / 60000U) % 60U);
    const int l_secs   = static_cast<int>((sw.lap_ms / 1000U) % 60U);
    const int l_tenths = static_cast<int>((sw.lap_ms / 100U) % 10U);
    char lap_buf[32];
    std::snprintf(lap_buf, sizeof(lap_buf), "SPLIT  %02d:%02d.%d", l_mins, l_secs, l_tenths);
    if (backdrop_active()) {
      fast_print(it, cx + 1, tool_art_px(274) + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, lap_buf);
    }
    fast_print(it, cx, tool_art_px(274), ctx.font_small, ctx.theme_primary, TextAlign::CENTER, lap_buf);
  }

  const int btn_y     = cy + tool_art_px(99);
  const int btn_lap_x = cx - tool_art_px(57);
  const int btn_rst_x = cx + tool_art_px(57);
  const int btn_r     = tool_art_px(36);

  render_glass_circle(it, btn_lap_x, btn_y, btn_r, kChromeGlassTint,
                      (sw.lap_ms > 0) ? ctx.col_red : ctx.theme_secondary);
  fast_print(it, btn_lap_x + 1, btn_y + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, "LAP");
  fast_print(it, btn_lap_x, btn_y, ctx.font_small, ctx.col_white, TextAlign::CENTER, "LAP");

  render_glass_circle(it, btn_rst_x, btn_y, btn_r, kChromeGlassTint, kChromeCardBorder);
  fast_print(it, btn_rst_x + 1, btn_y + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, "RESET");
  fast_print(it, btn_rst_x, btn_y, ctx.font_small, ctx.theme_secondary, TextAlign::CENTER, "RESET");

  if (sw.lap_ms > 0) {
    const float lap_angle = (static_cast<float>(sw.lap_ms % 60000U) / 60000.0f) * (2.0f * PI_F);
    const float l_sin = std::sin(lap_angle);
    const float l_cos = std::cos(lap_angle);
    ui::draw_aa_line(it, cx_f, cy_f,
                     cx_f + tool_art_f(154.0f) * l_sin,
                     cy_f - tool_art_f(154.0f) * l_cos,
                     std::max(1.8f, tool_art_f(2.0f)), ctx.col_red);
    ui::draw_aa_ring(it,
                     cx_f + tool_art_f(158.0f) * l_sin,
                     cy_f - tool_art_f(158.0f) * l_cos,
                     tool_art_f(4.0f), std::max(1.4f, tool_art_f(1.6f)), ctx.col_red);
  }

  const float chrono_angle = (static_cast<float>(total_ms % 60000U) / 60000.0f) * (2.0f * PI_F);
  const float s_sin = std::sin(chrono_angle);
  const float s_cos = std::cos(chrono_angle);
  const float tail_len = tool_art_f(36.0f);
  const float tip_len  = tool_art_f(178.0f);
  const float tail_xf  = cx_f - tail_len * s_sin;
  const float tail_yf  = cy_f + tail_len * s_cos;
  const float tip_xf   = cx_f + tip_len * s_sin;
  const float tip_yf   = cy_f - tip_len * s_cos;
  const float needle_w = std::max(2.0f, tool_art_f(2.2f));
  const float cw_outer = tool_art_f(7.0f);
  const float cw_inner = tool_art_f(5.6f);
  const float cw_pip   = tool_art_f(3.0f);

  ui::draw_aa_line(it, tail_xf + 1.0f, tail_yf + 1.2f, tip_xf + 1.0f, tip_yf + 1.2f,
                   needle_w, kChromeShadow, 96);
  ui::draw_aa_disc(it, tail_xf + 1.0f, tail_yf + 1.2f, cw_outer, kChromeShadow, 96);
  ui::draw_aa_line(it, tail_xf, tail_yf, tip_xf, tip_yf, needle_w, ctx.col_white);
  ui::draw_aa_disc(it, tail_xf, tail_yf, cw_outer, ctx.theme_secondary);
  ui::draw_aa_disc(it, tail_xf, tail_yf, cw_inner, ctx.col_dark_gray);
  ui::draw_aa_disc(it, tail_xf, tail_yf, cw_pip, ctx.theme_secondary);

  ui::render_center_arbor_aa(it, cx, cy, ctx.theme_secondary, ctx.col_dark_gray, ctx.col_white);

  const int sw_min_r = (layout().min_hit_px() + 1) / 2;
  const int sw_pad = std::max(8, sw_min_r - btn_r);
  frame_hits().add_rect(tool_art_px(40), tool_art_px(69),
                        SCREEN_W - 2 * tool_art_px(40), tool_art_px(210),
                        WidgetId::SW_PRIMARY_BTN, kHitTap | kHitFocusSlot);
  frame_hits().add_circle(btn_lap_x, btn_y, btn_r,
                          WidgetId::SW_LAP_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle, 0, sw_pad);
  frame_hits().add_circle(btn_rst_x, btn_y, btn_r,
                          WidgetId::SW_RESET_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle, 0, sw_pad);

  render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back", ctx.col_white);
}
#endif

void render_level_subpage(Rgb565Surface &it, const UiFrameContext &ctx,
                          const DeviceRuntimeState &rs) {
  render_page_header(it, ctx.cx, "LEVEL", ctx.font_small, ctx.col_white);

  const float roll_deg = rs.hw_imu_roll;
  const float pitch_deg = rs.hw_imu_pitch;
  if (std::isnan(roll_deg) || std::isnan(pitch_deg) || std::isnan(rs.hw_accel_z)) {
    render_empty_state(it, ctx.cx, ctx.cy, "Waiting for the motion sensor...", ctx.font_small, ctx.col_slate);
    render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "< Back");
    return;
  }

  const float r_rad = roll_deg * (PI_F / 180.0f);
  const float p_rad = pitch_deg * (PI_F / 180.0f);
  const float sin_x = std::clamp(std::sin(r_rad) * std::cos(p_rad), -1.0f, 1.0f);
  const float raw_x_deg = -std::asin(sin_x) * (180.0f / PI_F);
  const float raw_y_deg = -pitch_deg;

  static float smooth_x_deg = 0.0f;
  static float smooth_y_deg = 0.0f;
  static uint32_t last_level_ms = 0;
  static bool init_level = false;
  const uint32_t dt_ms = (ctx.now_ms >= last_level_ms) ? (ctx.now_ms - last_level_ms) : 401U;
  last_level_ms = ctx.now_ms;
  if (!init_level || dt_ms > 400U) {
    smooth_x_deg = raw_x_deg;
    smooth_y_deg = raw_y_deg;
    init_level = true;
  } else {
    const float alpha = std::clamp(static_cast<float>(dt_ms) / 115.0f, 0.20f, 0.65f);
    smooth_x_deg += (raw_x_deg - smooth_x_deg) * alpha;
    smooth_y_deg += (raw_y_deg - smooth_y_deg) * alpha;
  }

  const float total_tilt = std::sqrt(smooth_x_deg * smooth_x_deg + smooth_y_deg * smooth_y_deg);
  const bool is_level = (total_tilt <= 0.6f);
  const UiColor ring_c = is_level ? ctx.col_green : ctx.col_slate;

  auto fmt_axis = [](char *dst, size_t cap, char axis, float deg) {
    if (std::fabs(deg) < 0.05f) {
      std::snprintf(dst, cap, "%c 0.0°", axis);
    } else {
      std::snprintf(dst, cap, "%c %+.1f°", axis, deg);
    }
  };
  char x_buf[20], y_buf[20], deg_buf[48];
  fmt_axis(x_buf, sizeof(x_buf), 'X', smooth_x_deg);
  fmt_axis(y_buf, sizeof(y_buf), 'Y', smooth_y_deg);
  std::snprintf(deg_buf, sizeof(deg_buf), "%s   %s", x_buf, y_buf);
  render_sub_header(it, ctx.cx, deg_buf, ctx.font_small,
                    is_level ? ctx.col_green : ctx.col_white);

  const auto &ly = layout();
  const int level_top = ly.sub_hdr_y + dp_px(18);
  const int level_bot = ly.back_y - dp_px(24);
  const int level_cy  = (level_top + level_bot) / 2;
  const int outer_r   = std::max(dp_px(48), std::min(tool_art_px(136), (level_bot - level_top) / 2 - dp_px(6)));
  const int mid_r     = (outer_r * 70) / 136;
  const int inner_r   = (outer_r * 27) / 136;
  const int cross_out = outer_r + std::max(3, tool_art_px(5));
  const int cross_in  = inner_r + std::max(3, tool_art_px(6));
  const float max_bub = static_cast<float>(outer_r - std::max(12, tool_art_px(22)));
  const int bub_r     = std::max(12, tool_art_px(20));

  for (int i = 0; i < 60; i += 5) {
    const int x1 = ctx.cx + static_cast<int>(DIAL_SIN[i] * static_cast<float>(outer_r - tool_art_px(8)));
    const int y1 = level_cy - static_cast<int>(DIAL_COS[i] * static_cast<float>(outer_r - tool_art_px(8)));
    const int x2 = ctx.cx + static_cast<int>(DIAL_SIN[i] * static_cast<float>(outer_r));
    const int y2 = level_cy - static_cast<int>(DIAL_COS[i] * static_cast<float>(outer_r));
    fast_line(it, x1, y1, x2, y2, ctx.col_gray);
  }

  fast_circle(it, ctx.cx, level_cy, outer_r, ctx.col_slate);
  fast_circle(it, ctx.cx, level_cy, outer_r - 1, ctx.col_dark_gray);
  fast_circle(it, ctx.cx, level_cy, mid_r, ctx.col_gray);
  fast_circle(it, ctx.cx, level_cy, inner_r, ring_c);
  fast_circle(it, ctx.cx, level_cy, inner_r - 1, ring_c);

  fast_line(it, ctx.cx - cross_out, level_cy, ctx.cx - cross_in, level_cy, ring_c);
  fast_line(it, ctx.cx + cross_in, level_cy, ctx.cx + cross_out, level_cy, ring_c);
  fast_line(it, ctx.cx, level_cy - cross_out, ctx.cx, level_cy - cross_in, ring_c);
  fast_line(it, ctx.cx, level_cy + cross_in, ctx.cx, level_cy + cross_out, ring_c);
  fast_filled_circle(it, ctx.cx, level_cy, 2, ring_c);

  const float deg_px = tool_art_f(7.6f);
  float fbx = static_cast<float>(ctx.cx) + smooth_x_deg * deg_px;
  float fby = static_cast<float>(level_cy) - smooth_y_deg * deg_px;
  const float bdx = fbx - static_cast<float>(ctx.cx);
  const float bdy = fby - static_cast<float>(level_cy);
  const float b_dist_sq = bdx * bdx + bdy * bdy;
  if (b_dist_sq > max_bub * max_bub) {
    const float b_dist = std::sqrt(b_dist_sq);
    fbx = static_cast<float>(ctx.cx) + (bdx / b_dist) * max_bub;
    fby = static_cast<float>(level_cy) + (bdy / b_dist) * max_bub;
  }
  const int bx = static_cast<int>(std::lround(fbx));
  const int by = static_cast<int>(std::lround(fby));

  fast_filled_circle(it, bx + 2, by + 3, bub_r, kChromePauseGlyph);
  const UiColor bub_fill = is_level ? ctx.col_green : ctx.theme_secondary;
  fast_filled_circle(it, bx, by, bub_r, bub_fill);
  fast_circle(it, bx, by, bub_r, ctx.col_white);
  fast_circle(it, bx, by, bub_r - 2, ctx.col_white);
  fast_filled_circle(it, bx - bub_r / 3, by - bub_r / 3, std::max(2, tool_art_px(4)), ctx.col_white);
  fast_filled_circle(it, bx + bub_r / 3, by + bub_r / 3, std::max(1, tool_art_px(2)), ctx.col_white);

  render_back_button(it, ctx.cx, ctx.font_small, ly.back_y, "< Back");
}


}  // namespace

void render_tools_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const int sub = rs.nav.tools_subpage;

  if (sub == ToolsSubpage::FLASHLIGHT) {
    if (!frame_base(it, ctx.col_white)) {
      fill_rect_row_major(it, 0, 0, SCREEN_W, SCREEN_H, ctx.col_white);
    }
    return;
  }

  switch (sub) {
#if LATTER_ROUND_DISPLAY
    case ToolsSubpage::STOPWATCH:
      render_stopwatch_subpage(it, ctx, rs.stopwatch);
      break;
#endif
    case ToolsSubpage::DICE:
      render_dice_subpage(it, ctx);
      break;
    case ToolsSubpage::CALCULATOR:
      render_calculator_subpage(it, ctx);
      break;
    case ToolsSubpage::LEVEL:
      render_level_subpage(it, ctx, rs);
      break;
    case ToolsSubpage::COIN_FLIP:
      render_coin_flip_subpage(it, ctx);
      break;
    case ToolsSubpage::LAUNCHER:
    default:
      render_tools_launcher(it, ctx, rs);
      break;
  }
}


}  // namespace latter_ui
