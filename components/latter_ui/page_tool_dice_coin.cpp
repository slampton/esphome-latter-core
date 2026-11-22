#include "tools_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace latter_ui {

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

void render_dice_subpage(Rgb565Surface &it, const UiFrameContext &ctx) {
  auto &ds = dice_state();
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
      hop_y1 = -static_cast<int>(std::fabs(fast_sin(anim_p * PI_F * 2.2f)) * scale_min_f(34.0f) * damp);
      hop_y2 = -static_cast<int>(std::fabs(fast_sin(anim_p * PI_F * 2.2f + 0.7f)) * scale_min_f(32.0f) * damp);
      jostle_x1 = static_cast<int>(fast_sin(anim_p * PI_F * 6.0f) * scale_min_f(8.0f) * damp);
      jostle_x2 = -static_cast<int>(fast_cos(anim_p * PI_F * 6.0f) * scale_min_f(8.0f) * damp);
      shadow_extra = static_cast<int>(scale_min_f(6.0f) * damp);
      const uint32_t step = elapsed / 55U;
      cur_val_1 = 1 + (static_cast<int>(hash2d_01(static_cast<int>(step), 17) * 60.0f) % 6);
      cur_val_2 = 1 + (static_cast<int>(hash2d_01(static_cast<int>(step) + 31, 53) * 60.0f) % 6);
    }
  }

  render_page_header(it, ctx.cx, "DICE ROLL", ctx.font_small, ctx.col_white);
  const int dice_base_y = scale_y(196), die_off = scale_min(75);
  const int d1_cx = ctx.cx - die_off + jostle_x1, d2_cx = ctx.cx + die_off + jostle_x2;
  const int d1_cy = dice_base_y + hop_y1, d2_cy = dice_base_y + hop_y2;
  const int d_size = scale_min(132), cr = std::max(6, scale_min(16));

  auto draw_tumbling_cube_3d = [&](int center_x, int center_y, int hop_y, float rx, float ry, float rz) {
    static constexpr float kCubeV[8][3] = {
        {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f,  1.0f}, {1.0f, -1.0f,  1.0f}, {1.0f, 1.0f,  1.0f}, {-1.0f, 1.0f,  1.0f}};
    static constexpr uint8_t kCubeF[6][4] = {
        {0, 1, 2, 3}, {5, 4, 7, 6}, {4, 5, 1, 0}, {3, 2, 6, 7}, {4, 0, 3, 7}, {1, 5, 6, 2}};
    const float half_s = scale_min_f(46.0f), fov = scale_min_f(260.0f), z_dist = scale_min_f(280.0f);
    ProjectedPoint pts[8];
    Vec3 rv[8];
    for (int i = 0; i < 8; i++) {
      rv[i] = rotate_3d_xyz({kCubeV[i][0] * half_s, kCubeV[i][1] * half_s, kCubeV[i][2] * half_s}, rx, ry, rz);
      pts[i] = project_3d(rv[i], center_x, center_y, fov, z_dist);
    }
    fast_filled_circle(it, center_x + scale_min(6), center_y + scale_min(18) - hop_y / 2, scale_min(40), kChromeModalBg);
    const UiColor c_border(158, 150, 138), c_pip(25, 28, 35);
    const int pip_r3d = std::max(2, scale_min(3));
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
    fill_rbox(x0 + scale_min(6) + shadow_extra, y0 + scale_min(8) + shadow_extra, kChromeModalBg);
    fill_rbox(x0, y0, UiColor(248, 246, 240));
    const UiColor border_col(182, 176, 164), pip_c(25, 28, 35), pip_rim(85, 92, 105);
    fast_line(it, x0 + cr, y0 + 1, x0 + d_size - cr, y0 + 1, ctx.col_white);
    fast_line(it, x0 + 1, y0 + cr, x0 + 1, y0 + d_size - cr, ctx.col_white);
    fast_line(it, x0 + cr, y0 + d_size - 1, x0 + d_size - cr, y0 + d_size - 1, border_col);
    fast_line(it, x0 + d_size - 1, y0 + cr, x0 + d_size - 1, y0 + d_size - cr, border_col);
    const int pr = std::max(5, scale_min(9)), p_dist = scale_min(34);
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
  if (backdrop_active()) fast_print(it, ctx.cx + 1, scale_y(324) + 1, ctx.font_date, kChromeShadow, TextAlign::CENTER, lbl);
  fast_print(it, ctx.cx, scale_y(324), ctx.font_date, lbl_col, TextAlign::CENTER, lbl);
  frame_hits().add_rect(0, 0, SCREEN_W, layout().back_y - scale_y(24), WidgetId::DICE_ROLL_BTN, kHitTap | kHitFocusSlot);
  render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "< Back");
}

void render_coin_flip_subpage(Rgb565Surface &it, const UiFrameContext &ctx) {
  auto &cs = coin_state();
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
      y_hop = -scale_min_f(74.0f) * arc;
      z_zoom = 1.0f + 0.18f * arc;
      float raw_cos = 1.0f;
      fast_sincos((p * (1.18f - 0.18f * p)) * 5.0f * PI_F, raw_sin, raw_cos);
      coin_sy = std::max(0.05f, std::fabs(raw_cos));
      is_edge_flash = (coin_sy < 0.16f);
      show_heads = ((raw_cos < 0.0f) ? cs.side : (1 - cs.side)) == 0;
    }
  }

  render_page_header(it, ctx.cx, "COIN FLIP", ctx.font_small, ctx.col_white);
  const int cx = ctx.cx, coin_cy = scale_y(220) + static_cast<int>(y_hop);
  const float s_zoom = scale_min_f(z_zoom);
  const int r_outer = std::max(16, static_cast<int>(108.0f * s_zoom));

  const int sh_base_y = scale_y(316);
  const float shadow_scale = std::max(0.32f, 1.0f - (-y_hop / std::max(1.0f, scale_min_f(95.0f))));
  const int sh_rx = static_cast<int>(scale_min_f(88.0f) * shadow_scale);
  const int sh_ry = std::max(3, static_cast<int>(scale_min_f(16.0f + 10.0f * coin_sy) * shadow_scale));
  for (int sdy = -sh_ry; sdy <= sh_ry; sdy++) {
    const int sdx = (sh_rx * CIRCLE_HW[std::clamp(233 + (sdy * 232) / sh_ry, 0, 465)]) / 233;
    fast_hline(it, cx - sdx, sh_base_y + sdy, 2 * sdx + 1, kChromePauseGlyph);
  }

  const UiColor col_rim_dark(64, 72, 84), col_rim_mid(134, 144, 160), col_bevel_hi(248, 252, 255);
  const UiColor col_groove_sh(82, 92, 108), col_relief_shadow(52, 60, 72), col_relief_hi(252, 254, 255);
  const UiColor col_engrave_recess(74, 84, 98);
  const float spec_center = cs.anim_active ? (0.68f * raw_sin) : -0.28f;
  const int b_outer = std::max(4, static_cast<int>(static_cast<float>(r_outer) * coin_sy));
  const float edge_signed = cs.anim_active ? (16.0f * raw_sin) : 4.0f;
  const bool edge_on_top = (edge_signed < 0.0f);
  const int edge_h = cs.anim_active ? scale_min(static_cast<int>(std::lround(std::fabs(edge_signed)))) : std::max(3, scale_min(4));

  for (int dy = 1; edge_h > 0 && dy <= b_outer + edge_h; dy++) {
    const int src_dy = (dy > edge_h) ? (dy - edge_h) : 0;
    const int dx_outer = (r_outer * CIRCLE_HW[std::clamp(233 + (src_dy * 232) / b_outer, 0, 465)]) / 233;
    const UiColor ec = (dy >= b_outer + edge_h - 1) ? col_rim_dark : (is_edge_flash ? UiColor(202, 212, 228) : col_rim_mid);
    fast_hline(it, cx - dx_outer, edge_on_top ? (coin_cy - dy) : (coin_cy + dy), 2 * dx_outer + 1, ec);
  }

  const int dish_in = std::max(5, scale_min(8));
  for (int dy = -b_outer; dy <= b_outer; dy++) {
    const int dx_outer = (r_outer * CIRCLE_HW[std::clamp(233 + (dy * 232) / b_outer, 0, 465)]) / 233;
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
    const int reed_step = std::max(4, scale_min(is_edge_flash ? 5 : 6));
    for (int rx_off = -r_outer + 6; rx_off <= r_outer - 6; rx_off += reed_step) {
      const int y_off = (b_outer * CIRCLE_HW[std::clamp(233 + (rx_off * 232) / r_outer, 0, 465)]) / 233;
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
      const UiFontView coin_lbl_font = (SCREEN_MIN_DIM < 380) ? ctx.font_small : ctx.font_date;
      const char *face_lbl = show_heads ? "HEADS" : "TAILS";
      const int ty = coin_cy + static_cast<int>(74.0f * coin_sy * s_zoom);
      if (!cs.anim_active) {
        fast_print(it, cx + 2, ty + 2, coin_lbl_font, col_relief_shadow, TextAlign::CENTER, face_lbl);
        fast_print(it, cx - 1, ty - 1, coin_lbl_font, col_relief_hi, TextAlign::CENTER, face_lbl);
      }
      fast_print(it, cx, ty, coin_lbl_font, UiColor(220, 228, 240), TextAlign::CENTER, face_lbl);
    }
  }

  frame_hits().add_rect(0, 0, SCREEN_W, layout().back_y - scale_y(24), WidgetId::COIN_FLIP_BTN, kHitTap | kHitFocusSlot);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back");
}

}  // namespace latter_ui
