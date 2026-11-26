#include "games_internal.h"
#include "ui_pages.h"
#include "ui_touch_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace latter_ui {
using namespace latter_core;

namespace {

GameArena *&game_arena_slot() noexcept {
  static GameArena *s_ptr = nullptr;
  return s_ptr;
}

}  // namespace

GameArena *acquire_game_arena() noexcept {
  auto *&ptr = game_arena_slot();
  if (ptr == nullptr) {
    ptr = static_cast<GameArena *>(ui_psram_calloc(1, sizeof(GameArena)));
  }
  return ptr;
}

bool &game_imu_retare_flag() noexcept {
  static bool flag = false;
  return flag;
}

// ==============================================================================
// 2. Shared Procedural Sprites (Active Games & Games Launcher Previews)
// ==============================================================================
void render_glass_marble(Rgb565Surface &it, int bx, int by, int br,
                         UiColor base_col, UiColor hi_col,
                         int sh_x, int sh_y) {
  if (br <= 1) return;
  if (br >= 4) {
    fast_filled_circle(it, bx + sh_x, by + sh_y, br, UiColor(10, 14, 20));
  }
  fast_filled_circle(it, bx, by, br, base_col);
  fast_circle(it, bx, by, br, UiColor(14, 18, 24));
  if (br >= 6) {
    const UiColor rim_shade(static_cast<uint8_t>((base_col.r * 170) >> 8),
                            static_cast<uint8_t>((base_col.g * 170) >> 8),
                            static_cast<uint8_t>((base_col.b * 170) >> 8));
    fast_circle(it, bx, by, br - 1, rim_shade);
  }
  if (br >= 8) {
    fast_circle(it, bx - 1, by - 1, br / 2, hi_col);
    fast_line(it, bx - br / 3, by + br / 4, bx + br / 3, by - br / 4, hi_col);
  }
  fast_filled_circle(it, bx - (br / 3), by - (br / 3), std::max(1, br / 4), UiColor(255, 255, 255));
  if (br >= 10) {
    fast_filled_circle(it, bx + (br / 3), by + (br / 3), std::max(1, br / 6), hi_col);
  }
}

#if LATTER_HAS_IMU
void render_archery_target(Rgb565Surface &it, int tx, int ty, int r_outer,
                           UiColor col_white, UiColor col_black,
                           UiColor col_cyan, UiColor col_red,
                           UiColor col_gold, UiColor col_slate,
                           UiColor col_dark_gray) {
  if (r_outer <= 4) return;
  const int r_black = (r_outer * 76) / 100;
  const int r_cyan = (r_outer * 54) / 100;
  const int r_red = (r_outer * 34) / 100;
  const int r_gold = std::max(3, (r_outer * 16) / 100);

  fast_filled_circle(it, tx, ty + std::max(2, game_px(3)), r_outer, col_dark_gray);
  fast_filled_circle(it, tx, ty, r_outer, col_white);
  fast_circle(it, tx, ty, r_outer, col_dark_gray);
  fast_filled_circle(it, tx, ty, r_black, col_black);
  fast_circle(it, tx, ty, r_black, col_slate);
  fast_filled_circle(it, tx, ty, r_cyan, col_cyan);
  fast_circle(it, tx, ty, r_cyan, UiColor(0, 175, 205));
  fast_filled_circle(it, tx, ty, r_red, col_red);
  fast_circle(it, tx, ty, r_red, UiColor(180, 20, 20));
  fast_filled_circle(it, tx, ty, r_gold, col_gold);
  fast_pixel(it, tx, ty, col_red);
}

void render_brick_tile(Rgb565Surface &it, int cx_b, int cy_b, int w, int h,
                       UiColor body_col, UiColor hi_col, int hp, int max_hp, UiColor col_white) {
  const int rx = cx_b - w / 2;
  const int ry = cy_b - h / 2;
  fast_hline(it, rx + 1, ry + h, w - 1, UiColor(12, 16, 24));
  fill_rect_row_major(it, rx, ry, w, h, body_col);
  fast_rect_outline(it, rx, ry, w, h, UiColor(18, 22, 30));
  fast_hline(it, rx + 1, ry + 1, w - 2, hi_col);
  fast_line(it, rx + 1, ry + 1, rx + 1, ry + h - 2, hi_col);
  fast_hline(it, rx + 2, ry + h - 2, w - 3, scale_color(body_col, 0.58f));
  if (max_hp >= 2) {
    if (hp >= 2) {
      fast_pixel(it, rx + 3, ry + 3, col_white);
      fast_pixel(it, rx + w - 4, ry + 3, col_white);
      fast_pixel(it, rx + 3, ry + h - 4, col_white);
      fast_pixel(it, rx + w - 4, ry + h - 4, col_white);
      if (hp >= 3) {
        const int bw = std::max(6, game_px(10));
        const int bh = std::max(3, game_px(4));
        fill_rect_row_major(it, cx_b - bw / 2, cy_b - bh / 2, bw, bh, col_white);
      } else if (max_hp == 3) {
        fast_line(it, cx_b - game_px(4), ry + 2, cx_b + game_px(3), ry + h - 3, UiColor(20, 24, 32));
      }
    } else if (hp == 1) {
      fast_line(it, cx_b - game_px(6), ry + 1, cx_b, cy_b, UiColor(16, 20, 28));
      fast_line(it, cx_b, cy_b, cx_b + game_px(6), ry + h - 2, UiColor(16, 20, 28));
      fast_line(it, cx_b - game_px(5), ry + 1, cx_b + 1, cy_b, hi_col);
    }
  }
}

void render_brick_paddle(Rgb565Surface &it, int px, int py, int pw, int ph,
                         UiColor pad_col, UiColor col_white) {
  const int r = ph / 2;
  const int x_l = px - pw / 2 + r;
  const int x_r = px + pw / 2 - r;
  const int body_w = std::max(2, x_r - x_l);
  fast_hline(it, x_l, py + r + 1, body_w, UiColor(12, 16, 24));
  fast_filled_circle(it, x_l, py, r, pad_col);
  fast_filled_circle(it, x_r, py, r, pad_col);
  fill_rect_row_major(it, x_l, py - r, body_w, ph + 1, pad_col);
  fast_hline(it, x_l, py - r, body_w, col_white);
  fast_hline(it, x_l, py + r, body_w, scale_color(pad_col, 0.55f));
  const int core_w = (pw * 42) / 100;
  fill_rect_row_major(it, px - core_w / 2, py - 1, core_w, 3, col_white);
}

void render_chomp_snack(Rgb565Surface &it, int tx, int ty, int kind,
                        UiColor col_white, UiColor col_red, UiColor col_gold,
                        UiColor col_cyan, UiColor col_green, UiColor col_magenta, UiColor col_bg) {
  const int k = kind % 5;
  auto sc = [](int v) -> int { return game_px(v); };
  const int r13 = std::max(6, sc(13));
  const int r12 = std::max(5, sc(12));
  const int r10 = std::max(5, sc(10));
  const int r2 = std::max(1, sc(2));
  fast_filled_circle(it, tx, ty + std::max(2, sc(3)), r13, UiColor(14, 18, 26));
  if (k == 0) {
    const UiColor dough(218, 162, 84), crust(168, 114, 46), chip(58, 34, 18);
    fast_filled_circle(it, tx, ty, r13, dough);
    fast_circle(it, tx, ty, r13, crust);
    fast_circle(it, tx, ty, std::max(4, r13 - 1), UiColor(238, 188, 112));
    fast_filled_circle(it, tx - sc(5), ty - sc(4), r2, chip);
    fast_filled_circle(it, tx + sc(4), ty - sc(5), r2, chip);
    fast_filled_circle(it, tx, ty + sc(1), r2, chip);
    fast_filled_circle(it, tx - sc(6), ty + sc(4), r2, chip);
    fast_filled_circle(it, tx + sc(5), ty + sc(4), r2, chip);
    fast_pixel(it, tx - sc(1), ty - sc(7), chip);
    fast_pixel(it, tx + sc(2), ty + sc(7), chip);
  } else if (k == 1) {
    fast_filled_circle(it, tx, ty + std::max(1, sc(1)), r12, col_red);
    fast_circle(it, tx, ty + std::max(1, sc(1)), r12, UiColor(255, 130, 130));
    fast_filled_triangle(it, tx - sc(8), ty - sc(8), tx, ty - sc(3), tx - sc(2), ty - sc(12), col_green);
    fast_filled_triangle(it, tx + sc(8), ty - sc(8), tx, ty - sc(3), tx + sc(2), ty - sc(12), col_green);
    fast_filled_triangle(it, tx - sc(4), ty - sc(12), tx + sc(4), ty - sc(12), tx, ty - sc(5), col_green);
    fast_line(it, tx, ty - sc(11), tx, ty - sc(15), col_green);
    fast_pixel(it, tx - sc(4), ty - sc(1), col_gold);
    fast_pixel(it, tx + sc(4), ty - sc(1), col_gold);
    fast_pixel(it, tx, ty + sc(3), col_gold);
    fast_pixel(it, tx - sc(5), ty + sc(5), col_gold);
    fast_pixel(it, tx + sc(5), ty + sc(5), col_gold);
    fast_pixel(it, tx, ty + sc(8), col_gold);
    fast_filled_circle(it, tx - sc(4), ty - sc(3), r2, col_white);
  } else if (k == 2) {
    fast_filled_circle(it, tx, ty, r13, UiColor(215, 155, 78));
    fast_filled_circle(it, tx, ty, std::max(4, sc(11)), UiColor(255, 110, 178));
    fast_circle(it, tx, ty, r13, UiColor(160, 105, 42));
    const int hole_r = std::max(2, sc(4));
    fast_filled_circle(it, tx, ty, hole_r, col_bg);
    fast_circle(it, tx, ty, hole_r, UiColor(180, 70, 125));
    fast_line(it, tx - sc(7), ty - sc(5), tx - sc(5), ty - sc(7), col_cyan);
    fast_line(it, tx + sc(5), ty - sc(7), tx + sc(7), ty - sc(5), col_gold);
    fast_line(it, tx - sc(8), ty + sc(3), tx - sc(6), ty + sc(5), col_white);
    fast_line(it, tx + sc(6), ty + sc(4), tx + sc(8), ty + sc(2), col_green);
    fast_line(it, tx - sc(2), ty + sc(8), tx + sc(2), ty + sc(8), col_gold);
  } else if (k == 3) {
    fast_filled_triangle(it, tx - sc(9), ty, tx - sc(17), ty - sc(7), tx - sc(17), ty + sc(7), col_magenta);
    fast_filled_triangle(it, tx + sc(9), ty, tx + sc(17), ty - sc(7), tx + sc(17), ty + sc(7), col_magenta);
    fast_line(it, tx - sc(16), ty - sc(5), tx - sc(10), ty, col_white);
    fast_line(it, tx + sc(16), ty - sc(5), tx + sc(10), ty, col_white);
    fast_filled_circle(it, tx, ty, r10, col_magenta);
    fast_circle(it, tx, ty, r10, col_white);
    fast_line(it, tx - sc(5), ty - sc(6), tx + sc(5), ty + sc(6), col_white);
    fast_line(it, tx - sc(4), ty - sc(6), tx + sc(6), ty + sc(6), UiColor(255, 190, 230));
    fast_filled_circle(it, tx - sc(3), ty - sc(3), r2, col_white);
  } else {
    fast_filled_circle(it, tx, ty, r12, col_cyan);
    fast_circle(it, tx, ty, r12, col_white);
    fast_filled_circle(it, tx - sc(4), ty - sc(4), std::max(2, sc(3)), col_white);
    fast_line(it, tx, ty - sc(6), tx, ty + sc(6), col_gold);
    fast_line(it, tx - sc(6), ty, tx + sc(6), ty, col_gold);
    fast_filled_circle(it, tx, ty, r2, col_gold);
  }
}

void render_chomp_character(Rgb565Surface &it, int mx, int my, int mr,
                            float look_dx, float look_dy, bool mouth_open, bool is_chomping,
                            uint32_t now_ms, UiColor col_white, UiColor col_black, UiColor col_dark_gray) {
  const UiColor fur_main(28, 148, 248), fur_dark(16, 96, 182), fur_hi(110, 198, 255);
  const UiColor ear_inner(255, 158, 188), tongue_pink(255, 128, 166);
  const int cy_m = my + (is_chomping ? (((now_ms / 70U) & 1U) ? -game_px(3) : game_px(2)) : 0);

  fast_filled_circle(it, mx, cy_m + std::max(2, game_px(4)), mr, UiColor(12, 16, 24));
  const int ear_r = (mr * 34) / 100, ear_inner_r = (mr * 19) / 100;
  const int ear_dx = (mr * 74) / 100, ear_dy = (mr * 68) / 100;
  fast_filled_circle(it, mx - ear_dx, cy_m - ear_dy, ear_r, fur_dark);
  fast_filled_circle(it, mx - ear_dx, cy_m - ear_dy, ear_r - 1, fur_main);
  fast_circle(it, mx - ear_dx, cy_m - ear_dy, ear_r, fur_hi);
  fast_filled_circle(it, mx - ear_dx + 1, cy_m - ear_dy + 1, ear_inner_r, ear_inner);
  fast_filled_circle(it, mx + ear_dx, cy_m - ear_dy, ear_r, fur_dark);
  fast_filled_circle(it, mx + ear_dx, cy_m - ear_dy, ear_r - 1, fur_main);
  fast_circle(it, mx + ear_dx, cy_m - ear_dy, ear_r, fur_hi);
  fast_filled_circle(it, mx + ear_dx - 1, cy_m - ear_dy + 1, ear_inner_r, ear_inner);

  const int tuft_r = (mr * 28) / 100;
  fast_filled_circle(it, mx - mr + game_px(5), cy_m - game_px(4), tuft_r, fur_main);
  fast_filled_circle(it, mx + mr - game_px(5), cy_m - game_px(4), tuft_r, fur_main);
  fast_filled_circle(it, mx - mr + game_px(7), cy_m + game_px(10), tuft_r, fur_main);
  fast_filled_circle(it, mx + mr - game_px(7), cy_m + game_px(10), tuft_r, fur_main);
  fast_filled_circle(it, mx, cy_m - mr + game_px(3), tuft_r, fur_main);

  fast_filled_circle(it, mx, cy_m, mr, fur_main);
  fast_circle(it, mx, cy_m, mr, fur_hi);
  fast_circle(it, mx, cy_m, mr - 1, fur_dark);

  const int eye_dx = (mr * 30) / 100;
  const int eye_y = cy_m - (mr * 34) / 100;
  const int eye_r = std::max(7, (mr * 27) / 100);
  const int pup_r = std::max(3, (eye_r * 48) / 100);
  fast_filled_circle(it, mx - eye_dx, eye_y, eye_r, col_white);
  fast_circle(it, mx - eye_dx, eye_y, eye_r, col_dark_gray);
  fast_filled_circle(it, mx + eye_dx, eye_y, eye_r, col_white);
  fast_circle(it, mx + eye_dx, eye_y, eye_r, col_dark_gray);

  int pdx_l = static_cast<int>(look_dx * static_cast<float>(eye_r - pup_r - 1));
  int pdy_l = static_cast<int>(look_dy * static_cast<float>(eye_r - pup_r - 1));
  int pdx_r = pdx_l, pdy_r = pdy_l;
  if (is_chomping) {
    pdx_l = ((now_ms / 90U) & 1U) ? -2 : 2;
    pdy_l = -2;
    pdx_r = -pdx_l;
    pdy_r = 1;
  }
  fast_filled_circle(it, mx - eye_dx + pdx_l, eye_y + pdy_l, pup_r, col_black);
  fast_filled_circle(it, mx + eye_dx + pdx_r, eye_y + pdy_r, pup_r, col_black);
  fast_pixel(it, mx - eye_dx + pdx_l - 1, eye_y + pdy_l - 1, col_white);
  fast_pixel(it, mx + eye_dx + pdx_r - 1, eye_y + pdy_r - 1, col_white);

  const int mouth_y = cy_m + (mr * 26) / 100;
  if (is_chomping) {
    const int mr_open = (((now_ms / 85U) & 1U) != 0) ? ((mr * 36) / 100) : ((mr * 20) / 100);
    fast_filled_circle(it, mx, mouth_y, mr_open, col_black);
    fast_filled_circle(it, mx, mouth_y + mr_open / 2, mr_open / 2, ear_inner);
    const UiColor crumb(218, 162, 84);
    const int c_off = static_cast<int>((now_ms / 60U) % 8U);
    fast_filled_circle(it, mx - mr / 2 - c_off, mouth_y - 2 + c_off / 2, std::max(1, game_px(2)), crumb);
    fast_filled_circle(it, mx + mr / 2 + c_off, mouth_y - 4 + c_off / 2, std::max(1, game_px(2)), crumb);
    fast_filled_circle(it, mx + 3, mouth_y + mr / 2 + c_off / 2, std::max(1, game_px(2)), crumb);
  } else if (mouth_open) {
    const int m_rad = (mr * 44) / 100;
    fast_filled_circle(it, mx, mouth_y + 2, m_rad, col_black);
    fast_circle(it, mx, mouth_y + 2, m_rad, fur_dark);
    const int t_rad = (m_rad * 56) / 100;
    fast_filled_circle(it, mx, mouth_y + 2 + m_rad - t_rad - 1, t_rad, tongue_pink);
  } else {
    const int sw = (mr * 48) / 100;
    fast_filled_circle(it, mx, mouth_y - 2, sw, col_black);
    fill_rect_row_major(it, mx - sw, mouth_y - sw - 2, sw * 2 + 1, sw, fur_main);
    fast_hline(it, mx - sw, mouth_y - 2, sw * 2 + 1, fur_dark);
    fast_filled_circle(it, mx, mouth_y + sw / 2, sw / 3, tongue_pink);
  }
}
#endif

namespace {

// ==============================================================================
// 6. Games Launcher Carousel (7 Games, Filtered by Hardware IMU Capability)
// ==============================================================================
void draw_games_speaker_icon(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow, void *user_ctx) {
  const bool sound_on = (user_ctx != nullptr);
  ui::draw_icon_speaker(it, cx, cy, r, col, sound_on, shadow, !sound_on);
}

void render_games_launcher(Rgb565Surface &it, const UiFrameContext &ctx,
                           int &game_carousel_idx, bool menu_scroll_active,
                           int menu_scroll_dir, int menu_scroll_target_page) {
  const int cx = ctx.cx;
  const auto &ly = layout();
  const auto &prefs = device_prefs();
  const bool game_sound_on = ctx.game_sound_enabled;
  const bool stacked_hero = (ly.size_class == SizeClass::Compact && SCREEN_H > SCREEN_W);
  const int hdr_y = ly.hdr_y + dp_px(18);

  int enabled_ids[kMaxGamesLauncherItems]{};
  const int game_count = collect_enabled_games(enabled_ids, prefs);
  if (game_count <= 0) {
    game_carousel_idx = 0;
    ui::shadow_print(it, cx, hdr_y, ctx.font_games_title, ctx.col_white, UiTextAlign::CENTER, "GAMES");
    render_empty_state(it, cx, ctx.cy, "No games enabled", ctx.font_small, ctx.col_slate);
    return;
  }

  if (game_carousel_idx < 0 || game_carousel_idx >= game_count) {
    game_carousel_idx = std::clamp(game_carousel_idx, 0, game_count - 1);
  }

  auto gs = [](int v) constexpr -> int { return game_px(v); };

  auto draw_game_icon = [&](int g_idx, int icon_x, int icon_y) {
    if (g_idx == 0) {
      // Marbles
      fast_filled_circle(it, icon_x, icon_y, gs(52), UiColor(18, 24, 28));
      fast_circle(it, icon_x, icon_y, gs(52), ctx.col_slate);
      fast_circle(it, icon_x, icon_y, gs(51), ctx.col_slate);
      fast_circle(it, icon_x, icon_y, gs(46), UiColor(30, 40, 48));
      fast_filled_circle(it, icon_x, icon_y, gs(15), ctx.col_black);
      fast_circle(it, icon_x, icon_y, gs(15), UiColor(45, 55, 65));
      fast_circle(it, icon_x, icon_y, gs(14), UiColor(45, 55, 65));
      for (int a = 0; a < 4; a++) {
        const float sa = static_cast<float>(a) * 1.570796f;
        fast_line(it, icon_x + static_cast<int>(fast_cos(sa) * game_f(18.0f)),
                  icon_y + static_cast<int>(fast_sin(sa) * game_f(18.0f)),
                  icon_x + static_cast<int>(fast_cos(sa + 0.8f) * game_f(40.0f)),
                  icon_y + static_cast<int>(fast_sin(sa + 0.8f) * game_f(40.0f)),
                  UiColor(35, 45, 55));
      }
      render_glass_marble(it, icon_x - gs(24), icon_y - gs(18), gs(13), ctx.col_red, UiColor(255, 138, 128));
      render_glass_marble(it, icon_x + gs(22), icon_y - gs(22), gs(12), ctx.col_cyan, UiColor(180, 255, 255));
      render_glass_marble(it, icon_x - gs(20), icon_y + gs(22), gs(11), ctx.col_green, UiColor(185, 246, 202));
      render_glass_marble(it, icon_x + gs(24), icon_y + gs(18), gs(13), ctx.col_gold, UiColor(255, 245, 157));
      render_glass_marble(it, icon_x + gs(2), icon_y - gs(31), gs(10), ctx.col_magenta, UiColor(248, 187, 208));

#if LATTER_HAS_IMU
    } else if (g_idx == 1) {
      // Archery
      render_archery_target(it, icon_x, icon_y, gs(46), ctx.col_white, ctx.col_black,
                            ctx.col_cyan, ctx.col_red, ctx.col_gold, ctx.col_slate, ctx.col_dark_gray);
      const int tip_x = icon_x + gs(2);
      const int tip_y = icon_y + gs(2);
      const int tail_x = icon_x + gs(56);
      const int tail_y = icon_y - gs(36);
      fast_line(it, tip_x, tip_y + gs(5), tail_x - gs(8), tail_y + gs(10), UiColor(15, 18, 24));
      fast_line(it, tip_x, tip_y + gs(6), tail_x - gs(8), tail_y + gs(11), UiColor(15, 18, 24));
      fast_circle(it, tip_x, tip_y, gs(5), UiColor(180, 110, 0));
      fast_filled_triangle(it, tip_x - gs(3), tip_y + gs(2), tip_x + gs(9), tip_y - gs(9),
                           tip_x + gs(12), tip_y - gs(2), ctx.col_white);
      fast_line(it, tip_x, tip_y, tail_x, tail_y, UiColor(238, 206, 148));
      fast_line(it, tip_x, tip_y - 1, tail_x, tail_y - 1, UiColor(250, 228, 178));
      fast_line(it, tip_x, tip_y + 1, tail_x, tail_y + 1, UiColor(182, 138, 76));
      for (int f = 0; f < 9; f++) {
        const int fx = tail_x - gs(3 + f);
        const int fy = tail_y + gs(2 + (f * 2) / 3);
        fast_line(it, fx, fy - 1, fx + gs(3), fy - gs(11), UiColor(255, 52, 52));
        fast_line(it, fx, fy + 1, fx + gs(10), fy + gs(4), UiColor(220, 32, 32));
      }
      fast_filled_circle(it, tail_x + 1, tail_y - 1, std::max(2, gs(2)), ctx.col_white);

    } else if (g_idx == 2) {
      // Bricks
      const int b_w = gs(26), b_h = gs(11), gap_x = gs(4), gap_y = gs(5);
      const int start_bx = icon_x - static_cast<int>(2.0f * static_cast<float>(b_w) + 1.5f * static_cast<float>(gap_x)) + b_w / 2;
      const int start_by = icon_y - gs(36);
      const UiColor row_c[3] = {ctx.col_gold, ctx.col_cyan, ctx.col_orange};
      const UiColor row_hi[3] = {UiColor(255, 245, 165), UiColor(185, 255, 255), UiColor(255, 195, 100)};
      for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
          const int bx = start_bx + c * (b_w + gap_x);
          const int by = start_by + r * (b_h + gap_y);
          if (r == 2 && c == 2) {
            fill_rect_row_major(it, bx - gs(10), by - gs(5), gs(5), gs(4), row_c[r]);
            fill_rect_row_major(it, bx + gs(4), by - gs(7), gs(5), gs(4), row_hi[r]);
            fill_rect_row_major(it, bx - gs(6), by + gs(3), gs(4), gs(4), row_c[r]);
            fill_rect_row_major(it, bx + gs(5), by + gs(2), gs(5), gs(4), row_hi[r]);
            fast_filled_circle(it, bx, by, std::max(2, gs(2)), ctx.col_white);
            continue;
          }
          const int hp = (r == 0 && (c == 1 || c == 2)) ? 2 : 1;
          const UiColor bc = (hp == 2) ? UiColor(145, 162, 182) : row_c[r];
          const UiColor hc = (hp == 2) ? UiColor(230, 240, 252) : row_hi[r];
          render_brick_tile(it, bx, by, b_w, b_h, bc, hc, hp, hp, ctx.col_white);
        }
      }
      const int pad_x = icon_x - gs(6), pad_y = icon_y + gs(42), pad_w = gs(44), pad_h = gs(8);
      render_brick_paddle(it, pad_x, pad_y, pad_w, pad_h, ctx.theme_primary, ctx.col_white);

      const int bx = icon_x + gs(10), by = icon_y + gs(7);
      fast_filled_circle(it, pad_x + gs(3), pad_y - gs(7), std::max(2, gs(2)), UiColor(50, 40, 14));
      fast_filled_circle(it, pad_x + gs(6), pad_y - gs(14), std::max(2, gs(2)), UiColor(85, 68, 20));
      fast_filled_circle(it, pad_x + gs(9), pad_y - gs(21), std::max(3, gs(3)), UiColor(130, 102, 28));
      fast_filled_circle(it, bx - gs(3), by + gs(7), std::max(3, gs(4)), UiColor(185, 145, 35));
      fast_filled_circle(it, bx, by, gs(5), ctx.col_gold);
      fast_circle(it, bx, by, gs(5), ctx.col_white);
      fast_filled_circle(it, bx - gs(2), by - gs(2), std::max(1, gs(1)), ctx.col_white);

    } else if (g_idx == 3) {
      // Chomp
      render_chomp_snack(it, icon_x + gs(30), icon_y - gs(22), 0,
                         ctx.col_white, ctx.col_red, ctx.col_gold, ctx.col_cyan,
                         ctx.col_green, ctx.col_magenta, ctx.col_bg);
      render_chomp_snack(it, icon_x + gs(38), icon_y + gs(18), 2,
                         ctx.col_white, ctx.col_red, ctx.col_gold, ctx.col_cyan,
                         ctx.col_green, ctx.col_magenta, ctx.col_bg);
      render_chomp_snack(it, icon_x - gs(34), icon_y - gs(26), 1,
                         ctx.col_white, ctx.col_red, ctx.col_gold, ctx.col_cyan,
                         ctx.col_green, ctx.col_magenta, ctx.col_bg);
      render_chomp_character(it, icon_x - gs(8), icon_y + gs(6), gs(32), 0.85f, -0.25f,
                             true, false, 0U, ctx.col_white, ctx.col_black, ctx.col_dark_gray);

#else
    } else if (g_idx == 4) {
      // Blocks preview (scaled with scale_min, interlocking beveled polyominoes in dark well)
      const int bsz = gs(98);
      const int x0 = icon_x - bsz / 2;
      const int y0 = icon_y - bsz / 2;
      const float br = static_cast<float>(gs(12));
      fill_round_rect_aa(it, x0, y0, bsz, bsz, br, kChromeTrack);
      stroke_round_rect_aa(it, x0, y0, bsz, bsz, br, kChromeCardBorder, kChromeCardBorder, 1);

      const int cs = std::max(8, gs(16));
      const int gx0 = icon_x - 2 * cs - cs / 2;
      const int gy0 = icon_y - 2 * cs - cs / 2;
      auto cell_at = [&](int c, int r, UiColor col) {
        render_bevel_block_cell(it, gx0 + c * cs, gy0 + r * cs, cs, col,
                                mix_color_q8(col, ctx.col_white, 110), scale_color(col, 0.52f));
      };
      // Cyan I-bar bottom left
      cell_at(0, 4, ctx.col_cyan);
      cell_at(1, 4, ctx.col_cyan);
      cell_at(2, 4, ctx.col_cyan);
      cell_at(3, 4, ctx.col_cyan);
      // Orange L-piece bottom right
      cell_at(4, 4, ctx.col_orange);
      cell_at(4, 3, ctx.col_orange);
      cell_at(4, 2, ctx.col_orange);
      cell_at(3, 3, ctx.col_orange);
      // Gold O-square left middle
      cell_at(0, 2, ctx.col_gold);
      cell_at(1, 2, ctx.col_gold);
      cell_at(0, 3, ctx.col_gold);
      cell_at(1, 3, ctx.col_gold);
      // Floating Magenta T-piece falling above
      cell_at(2, 0, ctx.col_magenta);
      cell_at(1, 1, ctx.col_magenta);
      cell_at(2, 1, ctx.col_magenta);
      cell_at(3, 1, ctx.col_magenta);
      // Ghost outline at row 2-3
      fast_rect_outline(it, gx0 + 2 * cs + 1, gy0 + 3 * cs + 1, cs - 2, cs - 2, ctx.col_magenta);

    } else if (g_idx == 5) {
      // Snake preview (emerald-to-cyan winding snake + crimson apple)
      const int bsz = gs(98);
      const int x0 = icon_x - bsz / 2;
      const int y0 = icon_y - bsz / 2;
      const float br = static_cast<float>(gs(12));
      fill_round_rect_aa(it, x0, y0, bsz, bsz, br, kChromeTrack);
      stroke_round_rect_aa(it, x0, y0, bsz, bsz, br, kChromeCardBorder, kChromeCardBorder, 1);

      static constexpr int8_t PATH[9][2] = {
          {-28, 26}, {-14, 26}, {0, 26}, {0, 12}, {0, -2}, {-14, -2}, {-14, -18}, {0, -18}, {14, -18}};
      const UiColor c_head(36, 232, 168);
      const UiColor c_tail(28, 172, 238);
      for (int i = 0; i < 9; ++i) {
        const int mix_q = ((8 - i) * 255) / 8;
        const UiColor sc = mix_color_q8(c_head, c_tail, mix_q);
        const int px = icon_x + gs(PATH[i][0]);
        const int py = icon_y + gs(PATH[i][1]);
        const int sr = std::max(4, gs(6 + (i * 3) / 8));
        if (i > 0) {
          const int prev_x = icon_x + gs(PATH[i - 1][0]);
          const int prev_y = icon_y + gs(PATH[i - 1][1]);
          fast_filled_circle_aa(it, (px + prev_x) / 2, (py + prev_y) / 2, std::max(3, sr - 1), sc);
        }
        fast_filled_circle_aa(it, px, py, sr, sc);
      }
      // Head eyes looking right at apple
      const int hx = icon_x + gs(14);
      const int hy = icon_y + gs(-18);
      const int er = std::max(2, gs(3));
      fast_filled_circle_aa(it, hx + gs(3), hy - gs(3), er, ctx.col_white);
      fast_filled_circle_aa(it, hx + gs(3), hy + gs(3), er, ctx.col_white);
      fast_pixel(it, hx + gs(4), hy - gs(3), ctx.col_black);
      fast_pixel(it, hx + gs(4), hy + gs(3), ctx.col_black);
      // Apple ahead
      const int ax = icon_x + gs(32);
      const int ay = icon_y + gs(-18);
      fast_filled_circle_aa(it, ax, ay, std::max(4, gs(7)), ctx.col_red);
      fast_filled_circle_aa(it, ax - gs(2), ay - gs(2), std::max(1, gs(2)), UiColor(255, 195, 195));
      fast_filled_triangle(it, ax, ay - gs(7), ax + gs(4), ay - gs(11), ax + gs(3), ay - gs(6), ctx.col_green);

    } else {
      // Drop Four preview (cobalt board + diagonal 4-in-a-row win)
      const int bw = gs(98);
      const int bh = gs(82);
      const int x0 = icon_x - bw / 2;
      const int y0 = icon_y - bh / 2;
      const float br = static_cast<float>(gs(10));
      fill_round_rect_aa(it, x0, y0, bw, bh, br, UiColor(24, 52, 118));
      stroke_round_rect_aa(it, x0, y0, bw, bh, br, UiColor(64, 112, 198), UiColor(64, 112, 198), 1);

      const int step_x = bw / 5;
      const int step_y = bh / 4;
      const int dr = std::max(4, std::min(step_x, step_y) * 38 / 100);
      static constexpr uint8_t MINI[4][5] = {
          {0, 0, 0, 1, 0},
          {0, 0, 1, 2, 0},
          {0, 1, 2, 2, 0},
          {1, 2, 1, 2, 1},
      };
      for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 5; ++c) {
          const int dx = x0 + step_x / 2 + c * step_x;
          const int dy = y0 + step_y / 2 + r * step_y;
          const uint8_t v = MINI[r][c];
          if (v == 0U) {
            fast_filled_circle_aa(it, dx, dy, dr, UiColor(11, 16, 26));
          } else {
            const UiColor dc = (v == 1U) ? ctx.col_gold : ctx.col_red;
            fast_filled_circle_aa(it, dx, dy, dr, dc);
            if (r + c == 3) {
              fast_circle_aa(it, dx, dy, static_cast<float>(dr + 1), ctx.col_white, 1.5f);
            }
          }
        }
      }
#endif
    }
  };

  auto draw_game_card = [&](int g_idx, int icon_x, int icon_y, int text_center_x, int title_y, int score_y) {
    static constexpr const char *kGameTitles[7] = {
        "MARBLES", "ARCHERY", "BRICKS", "CHOMP", "BLOCKS", "SNAKE", "DROP FOUR"};
    const int safe_idx = std::clamp(g_idx, 0, 6);
    const char *title = kGameTitles[safe_idx];
    const int score = get_game_high_score(safe_idx + 1);

    draw_game_icon(safe_idx, icon_x, icon_y);
    ui::shadow_print(it, text_center_x, title_y, ctx.font_btn_bold, ctx.col_white, UiTextAlign::CENTER, title);

    const char *lbl_str = "HIGH SCORE:";
    char val_str[20];
    if (score <= 0) {
      std::snprintf(val_str, sizeof(val_str), "%s", kUnknownGlyph);
    } else if (score >= 1000000) {
      std::snprintf(val_str, sizeof(val_str), "%d,%03d,%03d", score / 1000000, (score / 1000) % 1000, score % 1000);
    } else if (score >= 1000) {
      std::snprintf(val_str, sizeof(val_str), "%d,%03d", score / 1000, score % 1000);
    } else {
      std::snprintf(val_str, sizeof(val_str), "%d", score);
    }

    int w_lbl = 0, w_val = 0, x_off = 0, base = 0, th = 0;
    ctx.font_small->measure(lbl_str, &w_lbl, &x_off, &base, &th);
    ctx.font_small->measure(val_str, &w_val, &x_off, &base, &th);
    const int gap_px = dp_px(8);
    const int start_x = text_center_x - (w_lbl + gap_px + w_val) / 2;

    ui::shadow_print(it, start_x, score_y, ctx.font_small, ctx.col_gold, UiTextAlign::CENTER_LEFT, lbl_str);
    ui::shadow_print(it, start_x + w_lbl + gap_px, score_y, ctx.font_small, ctx.col_white, UiTextAlign::CENTER_LEFT, val_str);
  };

  const int btn_row_y = ly.app_dock_y;
  const int play_h = std::max(ly.slot5_h, dp_px(44));
  const int spk_r = std::max(dp_px(20), play_h / 2);
  const int top_zone = hdr_y + dp_px(26);
  const int pips_y = btn_row_y - spk_r - dp_px(24);
  const int zone_h = std::max(dp_px(110), pips_y - dp_px(10) - top_zone);
  const int hero_mid_y = top_zone + zone_h / 2;
  const int hero_span = std::min(dp_px(340), ly.safe_span(hero_mid_y, dp_px(18)));

  const int icon_cx = stacked_hero ? cx : (cx - (hero_span * 32) / 100);
  const int card_cy = stacked_hero ? (top_zone + (zone_h * 34) / 100) : hero_mid_y;
  const int text_cx = stacked_hero ? cx : (cx + (hero_span * 17) / 100);
  const int base_title_y = stacked_hero
      ? std::min(pips_y - dp_px(44), card_cy + game_px(54) + dp_px(18))
      : (card_cy - dp_px(16));
  const int base_score_y = stacked_hero
      ? std::min(pips_y - dp_px(18), base_title_y + dp_px(28))
      : (card_cy + dp_px(22));
  const int cur_game_id = enabled_ids[game_carousel_idx];
  const int cur_g_idx = cur_game_id - 1;

  if (menu_scroll_active && game_count > 1) {
    const int full_span = dp_px(252);
    const int offset = menu_scroll_shift(full_span, ctx.now_ms);
    const int out_dx = -menu_scroll_dir * offset;
    const int in_dx = menu_scroll_dir * (full_span - offset);
    const int tgt_slot = std::clamp(menu_scroll_target_page, 0, game_count - 1);
    const int tgt_g_idx = enabled_ids[tgt_slot] - 1;

    draw_game_card(cur_g_idx, icon_cx + out_dx, card_cy, text_cx + out_dx, base_title_y, base_score_y);
    draw_game_card(tgt_g_idx, icon_cx + in_dx, card_cy, text_cx + in_dx, base_title_y, base_score_y);
  } else {
    const int bounce_x = edge_bounce().offset_px(
        PageId::GAMES, GestureAxisLock::Horizontal, ctx.now_ms, LATTER_PANEL_PPI);
    draw_game_card(cur_g_idx, icon_cx + bounce_x, card_cy, text_cx + bounce_x, base_title_y, base_score_y);

    const int flank_w = dp_px(72);
    const int hero_top = top_zone;
    const int hero_h = std::max(dp_px(80), pips_y - hero_top - dp_px(8));
    if (game_count > 1) {
      frame_hits().add_rect(0, hero_top, flank_w, hero_h, WidgetId::GAMES_PREV_BTN, kHitTap, -1);
      frame_hits().add_rect(SCREEN_W - flank_w, hero_top, flank_w, hero_h, WidgetId::GAMES_NEXT_BTN, kHitTap, 1);
      frame_hits().add_rect(flank_w, hero_top, SCREEN_W - 2 * flank_w, hero_h,
                            WidgetId::GAMES_HERO_CARD, kHitTap, static_cast<int16_t>(cur_game_id));
    } else {
      frame_hits().add_rect(dp_px(24), hero_top, SCREEN_W - 2 * dp_px(24), hero_h,
                            WidgetId::GAMES_HERO_CARD, kHitTap, static_cast<int16_t>(cur_game_id));
    }
  }

  // Documented arcade carve-out (D-070): Orbitron "GAMES" display header
  ui::shadow_print(it, cx, hdr_y, ctx.font_games_title, ctx.col_white, UiTextAlign::CENTER, "GAMES");

  const int active_pip = (menu_scroll_active && game_count > 1)
                             ? std::clamp(menu_scroll_target_page, 0, game_count - 1)
                             : game_carousel_idx;
  render_dock_page_pips(it, cx, pips_y, game_count, active_pip, ctx.col_white, kChromePipInactive);

  const int row_gap = dp_px(12);
  const int row_safe = ly.band_safe_span(btn_row_y, play_h, dp_px(18));
  const int play_w = std::min(dp_px(154), std::max(dp_px(88), row_safe - 2 * spk_r - row_gap));
  const int total_row_w = 2 * spk_r + row_gap + play_w;
  const int spk_x = cx - total_row_w / 2 + spk_r;
  const int play_cx = spk_x + spk_r + row_gap + play_w / 2;
  const int spk_pad = std::max(4, (ly.min_hit_px() - (2 * spk_r + 1) + 1) / 2);

  ui::action_pill_centered(it, play_cx, play_w, btn_row_y, play_h,
                           ctx.theme_primary, kChromeGlassTint, "PLAY", ctx.col_white, ctx.font_btn_bold,
                           WidgetId::GAMES_PLAY_BTN, kHitTap | kHitFocusSlot, static_cast<int16_t>(cur_game_id));

  ui::icon_circle_button(it, spk_x, btn_row_y, spk_r, kChromeGlassTint,
                         game_sound_on ? ctx.theme_secondary : kChromeCardBorder,
                         game_sound_on ? ctx.col_white : ctx.col_slate,
                         draw_games_speaker_icon,
                         game_sound_on ? reinterpret_cast<void *>(1) : nullptr,
                         WidgetId::GAMES_SOUND_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle, 1, spk_pad);
}

}  // namespace

// ==============================================================================
// 9. Exported Lifecycle, Input Queue, Tilt & Page Dispatcher
// ==============================================================================
void game_arena_drop() noexcept {
  auto *&ptr = game_arena_slot();
  if (ptr != nullptr) {
    ui_psram_free(ptr);
    ptr = nullptr;
  }
}

namespace {

struct GameArenaPsramEvictorRegistrar {
  GameArenaPsramEvictorRegistrar() noexcept {
    register_psram_cache_evictor(&game_arena_drop);
  }
};
const GameArenaPsramEvictorRegistrar kGameArenaPsramEvictorRegistrar{};

void compute_3d_game_tilt(float raw_ax, float raw_ay, float raw_az,
                          float euler_roll_deg, float euler_pitch_deg,
                          bool game_just_entered,
                          float &out_roll_delta, float &out_pitch_delta) {
#if LATTER_HAS_IMU
  constexpr float DEG_TO_RAD = 3.14159265f / 180.0f;
  constexpr float RAD_TO_DEG = 180.0f / 3.14159265f;

  const float roll = std::isnan(euler_roll_deg) ? 0.0f : euler_roll_deg;
  const float raw_pitch = std::isnan(euler_pitch_deg) ? -45.0f : euler_pitch_deg;

  const float r_rad = roll * DEG_TO_RAD;
  const float p_rad = raw_pitch * DEG_TO_RAD;
  const float gr = std::clamp(std::sin(r_rad) * std::cos(p_rad), -1.0f, 1.0f);
  const float clean_roll = std::asin(gr) * RAD_TO_DEG;
  const float clean_pitch = raw_pitch;

  const float gp = std::sin(p_rad);
  float gz = std::cos(p_rad);
  if (!std::isnan(raw_ax) && !std::isnan(raw_ay) && !std::isnan(raw_az)) {
    const float g_norm = std::sqrt(raw_ax * raw_ax + raw_ay * raw_ay + raw_az * raw_az);
    if (g_norm > 0.05f) {
      gz = raw_az / g_norm;
    }
  }

  static float zero_roll = 0.0f;
  static float zero_pitch = -45.0f;
  static float zero_gp = -0.7071f;
  static float zero_gz = 0.7071f;
  static bool vertical_pitch_mode = false;
  static bool tare_initialized = false;

  if (!tare_initialized || game_just_entered || game_imu_retare_flag()) {
    tare_initialized = true;
    game_imu_retare_flag() = false;
    zero_roll = clean_roll;
    zero_pitch = clean_pitch;
    const float pz_norm = std::sqrt(gp * gp + gz * gz);
    if (pz_norm > 0.05f) {
      zero_gp = gp / pz_norm;
      zero_gz = gz / pz_norm;
    } else {
      zero_gp = gp;
      zero_gz = gz;
    }
    vertical_pitch_mode = (std::fabs(gz) < 0.38f);
  }

  out_roll_delta = clean_roll - zero_roll;
  if (vertical_pitch_mode) {
    const float cross_pz = gp * zero_gz - gz * zero_gp;
    const float dot_pz = gp * zero_gp + gz * zero_gz;
    out_pitch_delta = std::atan2(cross_pz, dot_pz) * RAD_TO_DEG;
  } else {
    out_pitch_delta = clean_pitch - zero_pitch;
  }
#else
  (void)raw_ax;
  (void)raw_ay;
  (void)raw_az;
  (void)euler_roll_deg;
  (void)euler_pitch_deg;
  (void)game_just_entered;
  out_roll_delta = 0.0f;
  out_pitch_delta = 0.0f;
  // Universal Marbles touch-tilt fallback on stationary / no-IMU displays
  if (touch_gesture().active && touch_gesture().last_y < (layout().back_y - dp_px(50))) {
    const float span = static_cast<float>(std::max(dp_px(110), game_px(160)));
    out_roll_delta = std::clamp((static_cast<float>(touch_gesture().last_x - SCREEN_CX) / span) * 28.0f, -35.0f, 35.0f);
    out_pitch_delta = std::clamp((static_cast<float>(SCREEN_CY - touch_gesture().last_y) / span) * 28.0f, -35.0f, 35.0f);
  }
#endif
}

}  // namespace

int &pending_game_sfx() {
  static int sfx = 0;
  return sfx;
}

GameInputQueue &game_input() {
  static GameInputQueue queue{};
  return queue;
}

void render_games_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  if (!frame_base_state().painted) {
    frame_base(it, ctx.col_bg);
  }

  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  static int s_prev_active_game = 0;

  if (rs.nav.active_game <= 0) {
    if (s_prev_active_game != 0) {
      s_prev_active_game = 0;
      game_input().clear();
      game_arena_drop();
      flush_persisted_prefs_if_dirty();
    }
    render_games_launcher(it, ctx, rs.nav.game_carousel_idx,
                          rs.nav.menu_scroll_active, rs.nav.menu_scroll_dir,
                          rs.nav.menu_scroll_target_page);
    return;
  }

  GameArena *arena = acquire_game_arena();
  if (arena == nullptr) {
    render_empty_state(it, ctx.cx, ctx.cy, "Memory busy", ctx.font_small, ctx.col_slate);
    render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "Exit");
    return;
  }

  // Clamp unsupported game IDs gracefully to Marbles (1, universal across all boards)
  int eff_game = rs.nav.active_game;
  if (!game_supported_for_imu(eff_game, LATTER_HAS_IMU != 0)) {
    eff_game = 1;
  }

  const bool game_just_entered =
      (eff_game != s_prev_active_game) ||
      (arena->initialized_game_id != eff_game);
  s_prev_active_game = eff_game;
  arena->initialized_game_id = eff_game;
  if (game_just_entered) {
    game_input().clear();
  }

  float roll_delta = 0.0f;
  float pitch_delta = 0.0f;
  if (eff_game >= 1 && eff_game <= 4) {
    compute_3d_game_tilt(rs.hw_accel_x, rs.hw_accel_y, rs.hw_accel_z,
                         rs.hw_imu_roll, rs.hw_imu_pitch,
                         game_just_entered, roll_delta, pitch_delta);
  }

  switch (eff_game) {
    case 1:
      render_game_marbles(it, ctx, *arena, game_just_entered, roll_delta, pitch_delta,
                          rs.nav.marble_touch_x, rs.nav.marble_touch_y,
                          rs.nav.game_btn_pressed, prefs.high_score_marbles);
      break;
#if LATTER_HAS_IMU
    case 2:
      render_game_archery(it, ctx, *arena, game_just_entered, roll_delta, pitch_delta,
                          rs.nav.marble_touch_x, rs.nav.game_btn_pressed,
                          prefs.high_score_archery);
      break;
    case 3:
      render_game_bricks(it, ctx, *arena, game_just_entered, roll_delta,
                         rs.nav.marble_touch_x, rs.nav.game_btn_pressed,
                         prefs.high_score_bricks);
      break;
    case 4:
      render_game_chomp(it, ctx, *arena, game_just_entered, roll_delta, pitch_delta,
                        rs.nav.marble_touch_x, rs.nav.game_btn_pressed,
                        prefs.high_score_chomp);
      break;
#else
    case 5:
      render_game_blocks(it, ctx, *arena, game_just_entered, prefs.high_score_blocks);
      break;
    case 6:
      render_game_snake(it, ctx, *arena, game_just_entered, prefs.high_score_snake);
      break;
    case 7:
      render_game_drop_four(it, ctx, *arena, game_just_entered, prefs.high_score_drop_four);
      break;
#endif
    default:
      render_game_marbles(it, ctx, *arena, game_just_entered, roll_delta, pitch_delta,
                          rs.nav.marble_touch_x, rs.nav.marble_touch_y,
                          rs.nav.game_btn_pressed, prefs.high_score_marbles);
      break;
  }
}

}  // namespace latter_ui
