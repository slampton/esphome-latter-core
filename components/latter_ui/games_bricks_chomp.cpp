#include "games_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace latter_ui {

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
        fill_rect_row_major(it, cx_b - 5, cy_b - 2, 10, 4, col_white);
      } else if (max_hp == 3) {
        fast_line(it, cx_b - 4, ry + 2, cx_b + 3, ry + h - 3, UiColor(20, 24, 32));
      }
    } else if (hp == 1) {
      fast_line(it, cx_b - 6, ry + 1, cx_b, cy_b, UiColor(16, 20, 28));
      fast_line(it, cx_b, cy_b, cx_b + 6, ry + h - 2, UiColor(16, 20, 28));
      fast_line(it, cx_b - 5, ry + 1, cx_b + 1, cy_b, hi_col);
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

void render_game_bricks(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                        bool game_just_entered, float roll_delta,
                        float &marble_touch_x, bool &game_btn_pressed, int32_t &high_score_bricks) {
  static constexpr int NUM_BRICKS = 18;
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto *bricks = arena.bricks;
  auto *debris = arena.debris;
  static uint32_t last_phys_ms = 0;
  static int wave_num = 1;
  static int lives_left = 3;
  static int bo_score = 0;
  static int rally_combo = 0;
  static bool hit_brick_since_paddle = false;
  static bool ball_launched = false;
  static bool round_over = false;
  static bool is_new_high = false;
  static float ball_x = 233.0f, ball_y = 344.0f, ball_vx = 0.0f, ball_vy = 0.0f;
  static float trail_x[4] = {233.0f, 233.0f, 233.0f, 233.0f};
  static float trail_y[4] = {344.0f, 344.0f, 344.0f, 344.0f};
  static float smooth_paddle_x = 233.0f;
  static uint32_t debris_ms = 0, popup_ms = 0, banner_ms = 0;
  static int popup_x = 233, popup_y = 140;
  static char popup_text[32] = "";
  static UiColor popup_col(255, 215, 0);
  static char banner_text[32] = "";

  auto init_brick_wave = [&](int wv) {
    static constexpr int ROW_COUNTS[4] = {4, 5, 5, 4};
    static constexpr int ROW_YS[4] = {82, 104, 126, 148};
    const int brk_pitch = scale_min(46);
    const int brk_w = scale_min(40);
    const int brk_h = scale_min(14);
    int idx = 0;
    for (int r = 0; r < 4; r++) {
      const int cnt = ROW_COUNTS[r];
      const int start_x = cx - ((cnt - 1) * brk_pitch) / 2;
      const int row_y = cy - scale_min(233 - ROW_YS[r]);
      for (int c = 0; c < cnt; c++) {
        uint8_t hp = 1;
        if (wv == 2 && r == 0) {
          hp = 2;
        } else if (wv == 3 && (r == 0 || (r == 1 && c >= 1 && c <= 3))) {
          hp = 2;
        } else if (wv == 4) {
          if (r == 0) hp = 3;
          else if (r == 1) hp = 2;
        } else if (wv >= 5) {
          if (r == 0) hp = 3;
          else if (r == 1) hp = 2;
          else if (r == 2 && (c == 0 || c == 2 || c == 4)) hp = 2;
        }
        bricks[idx] = {static_cast<int16_t>(start_x + c * brk_pitch), static_cast<int16_t>(row_y),
                       static_cast<int16_t>(brk_w), static_cast<int16_t>(brk_h),
                       static_cast<uint8_t>(r), hp, hp};
        idx++;
      }
    }
  };

  const int paddle_y = std::min(layout().back_y - scale_y(76), cy + scale_min(117));
  const int paddle_w = scale_min(58);
  const int paddle_h = scale_min(10);

  auto reset_ball_on_paddle = [&]() {
    ball_launched = false;
    ball_x = smooth_paddle_x;
    ball_y = static_cast<float>(paddle_y - scale_min(11));
    ball_vx = 0.0f;
    ball_vy = 0.0f;
    for (int t = 0; t < 4; t++) {
      trail_x[t] = ball_x;
      trail_y[t] = ball_y;
    }
  };

  if (game_just_entered) {
    last_phys_ms = now_ms;
    wave_num = 1;
    lives_left = 3;
    bo_score = 0;
    rally_combo = 0;
    hit_brick_since_paddle = false;
    round_over = false;
    is_new_high = false;
    smooth_paddle_x = static_cast<float>(cx);
    debris_ms = 0;
    popup_ms = 0;
    banner_ms = 0;
    init_brick_wave(1);
    reset_ball_on_paddle();
  }

  float dt = (now_ms >= last_phys_ms) ? (static_cast<float>(now_ms - last_phys_ms) * 0.001f) : 0.05f;
  last_phys_ms = now_ms;
  dt = std::clamp(dt, 0.012f, 0.075f);

  const int max_pad_dx = scale_min(136);
  float raw_target_x = static_cast<float>(cx) + roll_delta * scale_min_f(8.4f);
  if (touch_gesture().active && touch_gesture().last_y < layout().back_y - scale_y(38) &&
      (LATTER_HAS_IMU == 0 || touch_gesture().max_travel >= scale_s(8))) {
    raw_target_x = static_cast<float>(touch_gesture().last_x);
  }
  raw_target_x = std::clamp(raw_target_x, static_cast<float>(cx - max_pad_dx), static_cast<float>(cx + max_pad_dx));
  smooth_paddle_x += (raw_target_x - smooth_paddle_x) * 0.34f;
  const float paddle_x = smooth_paddle_x;
  const float target_speed = scale_min_f(std::min(285.0f, 215.0f + static_cast<float>(wave_num - 1) * 14.0f));

  if (marble_touch_x >= 0.0f || game_btn_pressed) {
    marble_touch_x = -1.0f;
    game_btn_pressed = false;
    if (round_over) {
      wave_num = 1;
      lives_left = 3;
      bo_score = 0;
      rally_combo = 0;
      hit_brick_since_paddle = false;
      round_over = false;
      is_new_high = false;
      debris_ms = 0;
      popup_ms = 0;
      banner_ms = 0;
      init_brick_wave(1);
      reset_ball_on_paddle();
      game_imu_retare_flag() = true;
    } else if (!ball_launched && (banner_ms == 0U || now_ms - banner_ms >= 3000U)) {
      float launch_ang = std::clamp(roll_delta * 0.025f, -0.52f, 0.52f);
      if (std::fabs(launch_ang) < 0.14f) launch_ang = (now_ms & 1U) ? 0.22f : -0.22f;
      ball_vx = target_speed * fast_sin(launch_ang);
      ball_vy = -target_speed * fast_cos(launch_ang);
      ball_launched = true;
      hit_brick_since_paddle = true;
    }
  }

  const UiColor row_cols[4] = {ctx.col_gold, ctx.col_cyan, ctx.col_orange, ctx.col_green};
  const UiColor row_his[4] = {
      UiColor(255, 245, 165), UiColor(185, 255, 255),
      UiColor(255, 195, 100), UiColor(185, 250, 205)};

  if (!round_over) {
    if (!ball_launched) {
      ball_x = paddle_x;
      ball_y = static_cast<float>(paddle_y - 11);
      for (int t = 0; t < 4; t++) {
        trail_x[t] = ball_x;
        trail_y[t] = ball_y;
      }
    } else {
      for (int t = 3; t > 0; t--) {
        trail_x[t] = trail_x[t - 1];
        trail_y[t] = trail_y[t - 1];
      }
      trail_x[0] = ball_x;
      trail_y[0] = ball_y;
      ball_x += ball_vx * dt;
      ball_y += ball_vy * dt;

      static int wall_bounces = 0;
      const float dome_ball_r = scale_min_f(202.0f);
      const float b_dx = ball_x - static_cast<float>(cx);
      const float b_dy = ball_y - static_cast<float>(cy);
      const float b_dist = std::sqrt(b_dx * b_dx + b_dy * b_dy);
      if (b_dist > dome_ball_r && ball_y < static_cast<float>(paddle_y - 4)) {
        const float bnx = b_dx / b_dist;
        const float bny = b_dy / b_dist;
        ball_x = static_cast<float>(cx) + bnx * (dome_ball_r - 1.0f);
        ball_y = static_cast<float>(cy) + bny * (dome_ball_r - 1.0f);
        float rnx = bnx;
        float rny = (ball_y > static_cast<float>(cy + 12)) ? 0.0f : bny;
        const float r_len = std::sqrt(rnx * rnx + rny * rny);
        if (r_len > 0.001f) {
          rnx /= r_len;
          rny /= r_len;
        }
        const float v_dot = ball_vx * rnx + ball_vy * rny;
        if (v_dot > 0.0f) {
          ball_vx -= 2.0f * v_dot * rnx;
          ball_vy -= 2.0f * v_dot * rny;
          wall_bounces++;
          if (wall_bounces >= 2) ball_vy += (ball_vy >= 0.0f) ? 55.0f : 70.0f;
          if (std::fabs(ball_vy) < 95.0f) ball_vy = (ball_vy >= 0.0f) ? 98.0f : -98.0f;
          const float spd = std::sqrt(ball_vx * ball_vx + ball_vy * ball_vy);
          if (spd > 1.0f) {
            ball_vx = (ball_vx / spd) * target_speed;
            ball_vy = (ball_vy / spd) * target_speed;
          }
        }
      }

      if (ball_vy > 0.0f && ball_y >= static_cast<float>(paddle_y - 10) &&
          ball_y <= static_cast<float>(paddle_y + 8)) {
        const float half_reach = static_cast<float>(paddle_w) * 0.5f + 6.0f;
        if (std::fabs(ball_x - paddle_x) <= half_reach) {
          if (!hit_brick_since_paddle) rally_combo = 0;
          hit_brick_since_paddle = false;
          wall_bounces = 0;
          const float offset = std::clamp((ball_x - paddle_x) / (static_cast<float>(paddle_w) * 0.5f), -1.0f, 1.0f);
          const float bounce_ang = offset * 1.047f;
          ball_vx = target_speed * fast_sin(bounce_ang);
          ball_vy = -target_speed * fast_cos(bounce_ang);
          ball_y = static_cast<float>(paddle_y - 11);
          pending_game_sfx() = 11;
        }
      }

      const float ball_r = 6.0f;
      for (int b = 0; b < NUM_BRICKS; b++) {
        if (bricks[b].hp == 0) continue;
        const float dx = ball_x - static_cast<float>(bricks[b].x);
        const float dy = ball_y - static_cast<float>(bricks[b].y);
        const float lim_x = static_cast<float>(bricks[b].w) * 0.5f + ball_r;
        const float lim_y = static_cast<float>(bricks[b].h) * 0.5f + ball_r;
        if (std::fabs(dx) < lim_x && std::fabs(dy) < lim_y) {
          const float overlap_x = lim_x - std::fabs(dx);
          const float overlap_y = lim_y - std::fabs(dy);
          if (overlap_x < overlap_y) {
            ball_vx = -ball_vx;
            ball_x += (dx >= 0.0f) ? overlap_x : -overlap_x;
          } else {
            ball_vy = -ball_vy;
            ball_y += (dy >= 0.0f) ? overlap_y : -overlap_y;
          }
          bricks[b].hp--;
          pending_game_sfx() = (bricks[b].hp == 0) ? 12 : 13;
          rally_combo++;
          wall_bounces = 0;
          hit_brick_since_paddle = true;
          const int mult = std::min(4, rally_combo);
          static constexpr int BASE_ROW_PTS[4] = {150, 120, 90, 60};
          const int base_pts = (bricks[b].hp == 0) ? BASE_ROW_PTS[bricks[b].row] : 50;
          const int pts = base_pts * mult;
          bo_score += pts;
          if (bo_score > high_score_bricks) {
            high_score_bricks = bo_score;
            is_new_high = true;
          }
          popup_x = bricks[b].x;
          popup_y = bricks[b].y;
          popup_ms = now_ms;
          if (mult > 1) {
            std::snprintf(popup_text, sizeof(popup_text), "+%d %dx!", pts, mult);
            popup_col = ctx.col_gold;
          } else {
            std::snprintf(popup_text, sizeof(popup_text), "+%d", pts);
            popup_col = ctx.col_white;
          }
          const UiColor frag_c = (bricks[b].hp > 0) ? UiColor(200, 210, 225) : row_cols[bricks[b].row];
          for (int s = 0; s < 6; s++) {
            const float ang = static_cast<float>(s) * 1.047f + 0.35f;
            const float f_spd = (bricks[b].hp == 0) ? 95.0f : 55.0f;
            debris[s].x = static_cast<float>(bricks[b].x) + fast_cos(ang) * 6.0f;
            debris[s].y = static_cast<float>(bricks[b].y) + fast_sin(ang) * 4.0f;
            debris[s].vx = fast_cos(ang) * f_spd;
            debris[s].vy = fast_sin(ang) * f_spd - 25.0f;
            const UiColor sc = (s & 1) ? row_his[bricks[b].row] : frag_c;
            debris[s].r = sc.r;
            debris[s].g = sc.g;
            debris[s].b = sc.b;
          }
          debris_ms = now_ms;
          break;
        }
      }

      int rem_check = 0;
      for (int b = 0; b < NUM_BRICKS; b++) {
        if (bricks[b].hp > 0) rem_check++;
      }
      if (rem_check == 0 && banner_ms == 0U) {
        const int wave_bonus = wave_num * 300;
        bo_score += wave_bonus;
        if (bo_score > high_score_bricks) {
          high_score_bricks = bo_score;
          is_new_high = true;
        }
        if (is_new_high) schedule_persisted_prefs_save(now_ms);
        wave_num++;
        if (lives_left < 3) lives_left++;
        std::snprintf(banner_text, sizeof(banner_text), "WAVE %d CLEAR! +%d", wave_num - 1, wave_bonus);
        banner_ms = now_ms;
        pending_game_sfx() = 5;
        reset_ball_on_paddle();
      } else if (ball_y > static_cast<float>(paddle_y + scale_min(26))) {
        lives_left--;
        pending_game_sfx() = 14;
        rally_combo = 0;
        hit_brick_since_paddle = false;
        reset_ball_on_paddle();
        if (lives_left <= 0) {
          round_over = true;
          if (bo_score > high_score_bricks) {
            high_score_bricks = bo_score;
            is_new_high = true;
          }
          if (is_new_high) schedule_persisted_prefs_save(now_ms);
        }
      }
    }
  }

  if (debris_ms > 0U && (now_ms - debris_ms < 360U)) {
    for (int s = 0; s < 6; s++) {
      debris[s].x += debris[s].vx * dt;
      debris[s].y += debris[s].vy * dt;
      debris[s].vy += 180.0f * dt;
    }
  }

  const int dome_r = scale_min(208);
  fast_circle(it, cx, cy, dome_r, ctx.col_dark_gray);
  fast_circle(it, cx, cy, dome_r - 1, scale_color(ctx.col_slate, 0.35f));

  int bricks_remaining = 0;
  for (int b = 0; b < NUM_BRICKS; b++) {
    if (bricks[b].hp > 0) bricks_remaining++;
  }
  char bo_hud[56];
  if (rally_combo >= 2) {
    std::snprintf(bo_hud, sizeof(bo_hud), "W%d  •  %d/3  •  %dx  •  %d",
                  wave_num, lives_left, std::min(4, rally_combo), bo_score);
  } else {
    std::snprintf(bo_hud, sizeof(bo_hud), "W%d  •  Balls:%d  •  Brk:%d  •  %d",
                  wave_num, lives_left, bricks_remaining, bo_score);
  }
  render_game_hud_footer(it, cx, bo_hud, ctx.font_small, ctx.col_gold);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "Exit");

  for (int b = 0; b < NUM_BRICKS; b++) {
    if (bricks[b].hp == 0) continue;
    UiColor b_col = row_cols[bricks[b].row];
    UiColor h_col = row_his[bricks[b].row];
    if (bricks[b].hp >= 3) {
      b_col = UiColor(225, 55, 75);
      h_col = UiColor(255, 175, 185);
    } else if (bricks[b].hp == 2) {
      b_col = UiColor(145, 162, 182);
      h_col = UiColor(230, 240, 252);
    }
    render_brick_tile(it, bricks[b].x, bricks[b].y, bricks[b].w, bricks[b].h,
                      b_col, h_col, bricks[b].hp, bricks[b].max_hp, ctx.col_white);
  }

  if (debris_ms > 0U && (now_ms - debris_ms < 360U)) {
    for (int s = 0; s < 6; s++) {
      fill_rect_row_major(it, static_cast<int>(debris[s].x) - 2, static_cast<int>(debris[s].y) - 2, 4, 4,
                          UiColor(debris[s].r, debris[s].g, debris[s].b));
    }
  }
  if (popup_ms > 0U && (now_ms - popup_ms < 650U)) {
    const int py_off = static_cast<int>((now_ms - popup_ms) * 22U / 650U);
    fast_print(it, popup_x, popup_y - py_off, ctx.font_small, popup_col, UiTextAlign::CENTER, popup_text);
  }
  if (banner_ms > 0U) {
    const uint32_t b_el = now_ms - banner_ms;
    if (b_el >= 1100U && bricks_remaining == 0) init_brick_wave(wave_num);
    if (b_el >= 3000U) {
      banner_ms = 0U;
    } else if (b_el < 1100U) {
      fast_print(it, cx, cy - scale_y(21), ctx.font_date, ctx.col_gold, UiTextAlign::CENTER, banner_text);
    } else {
      char rdy_buf[32];
      std::snprintf(rdy_buf, sizeof(rdy_buf), "GET READY! WAVE %d", wave_num);
      fast_print(it, cx, cy - scale_y(21), ctx.font_date, ctx.col_cyan, UiTextAlign::CENTER, rdy_buf);
    }
  }

  render_brick_paddle(it, static_cast<int>(paddle_x), paddle_y, paddle_w, paddle_h,
                      ctx.theme_primary, ctx.col_white);

  if (ball_launched) {
    fast_filled_circle(it, static_cast<int>(trail_x[3]), static_cast<int>(trail_y[3]), 2, UiColor(65, 52, 18));
    fast_filled_circle(it, static_cast<int>(trail_x[2]), static_cast<int>(trail_y[2]), 3, UiColor(120, 95, 25));
    fast_filled_circle(it, static_cast<int>(trail_x[0]), static_cast<int>(trail_y[0]), 4, UiColor(185, 145, 35));
  }
  fast_filled_circle(it, static_cast<int>(ball_x), static_cast<int>(ball_y), 6, ctx.col_gold);
  fast_circle(it, static_cast<int>(ball_x), static_cast<int>(ball_y), 6, ctx.col_white);
  fast_filled_circle(it, static_cast<int>(ball_x) - 2, static_cast<int>(ball_y) - 2, 2, ctx.col_white);

  if (round_over) {
    char title_buf[32];
    UiColor title_col = ctx.col_gold;
    if (is_new_high) {
      std::snprintf(title_buf, sizeof(title_buf), "NEW HIGH SCORE!");
    } else if (wave_num == 1) {
      std::snprintf(title_buf, sizeof(title_buf), "GAME OVER");
      title_col = ctx.col_red;
    } else {
      std::snprintf(title_buf, sizeof(title_buf), "WAVE %d REACHED", wave_num);
      title_col = ctx.col_cyan;
    }
    char score_str[32], best_str[32];
    std::snprintf(score_str, sizeof(score_str), "Score: %d", bo_score);
    std::snprintf(best_str, sizeof(best_str), "Best: %d", static_cast<int>(high_score_bricks));
    ui::game_over_sheet(it, cx, cy, title_buf, title_col, score_str, best_str, is_new_high,
                        "PLAY AGAIN", ctx.font_date, ctx.font_small,
                        ctx.theme_primary, ctx.col_white, ctx.col_gold, ctx.col_slate, WidgetId::GAME_OVER_CTA);
  }
}

void render_chomp_snack(Rgb565Surface &it, int tx, int ty, int kind,
                        UiColor col_white, UiColor col_red, UiColor col_gold,
                        UiColor col_cyan, UiColor col_green, UiColor col_magenta, UiColor col_bg) {
  const int k = kind % 5;
  fast_filled_circle(it, tx, ty + 3, 13, UiColor(14, 18, 26));
  if (k == 0) {
    const UiColor dough(218, 162, 84), crust(168, 114, 46), chip(58, 34, 18);
    fast_filled_circle(it, tx, ty, 13, dough);
    fast_circle(it, tx, ty, 13, crust);
    fast_circle(it, tx, ty, 12, UiColor(238, 188, 112));
    fast_filled_circle(it, tx - 5, ty - 4, 2, chip);
    fast_filled_circle(it, tx + 4, ty - 5, 2, chip);
    fast_filled_circle(it, tx, ty + 1, 2, chip);
    fast_filled_circle(it, tx - 6, ty + 4, 2, chip);
    fast_filled_circle(it, tx + 5, ty + 4, 2, chip);
    fast_pixel(it, tx - 1, ty - 7, chip);
    fast_pixel(it, tx + 2, ty + 7, chip);
  } else if (k == 1) {
    fast_filled_circle(it, tx, ty + 1, 12, col_red);
    fast_circle(it, tx, ty + 1, 12, UiColor(255, 130, 130));
    fast_filled_triangle(it, tx - 8, ty - 8, tx, ty - 3, tx - 2, ty - 12, col_green);
    fast_filled_triangle(it, tx + 8, ty - 8, tx, ty - 3, tx + 2, ty - 12, col_green);
    fast_filled_triangle(it, tx - 4, ty - 12, tx + 4, ty - 12, tx, ty - 5, col_green);
    fast_line(it, tx, ty - 11, tx, ty - 15, col_green);
    fast_pixel(it, tx - 4, ty - 1, col_gold);
    fast_pixel(it, tx + 4, ty - 1, col_gold);
    fast_pixel(it, tx, ty + 3, col_gold);
    fast_pixel(it, tx - 5, ty + 5, col_gold);
    fast_pixel(it, tx + 5, ty + 5, col_gold);
    fast_pixel(it, tx, ty + 8, col_gold);
    fast_filled_circle(it, tx - 4, ty - 3, 2, col_white);
  } else if (k == 2) {
    fast_filled_circle(it, tx, ty, 13, UiColor(215, 155, 78));
    fast_filled_circle(it, tx, ty, 11, UiColor(255, 110, 178));
    fast_circle(it, tx, ty, 13, UiColor(160, 105, 42));
    fast_filled_circle(it, tx, ty, 4, col_bg);
    fast_circle(it, tx, ty, 4, UiColor(180, 70, 125));
    fast_line(it, tx - 7, ty - 5, tx - 5, ty - 7, col_cyan);
    fast_line(it, tx + 5, ty - 7, tx + 7, ty - 5, col_gold);
    fast_line(it, tx - 8, ty + 3, tx - 6, ty + 5, col_white);
    fast_line(it, tx + 6, ty + 4, tx + 8, ty + 2, col_green);
    fast_line(it, tx - 2, ty + 8, tx + 2, ty + 8, col_gold);
  } else if (k == 3) {
    fast_filled_triangle(it, tx - 9, ty, tx - 17, ty - 7, tx - 17, ty + 7, col_magenta);
    fast_filled_triangle(it, tx + 9, ty, tx + 17, ty - 7, tx + 17, ty + 7, col_magenta);
    fast_line(it, tx - 16, ty - 5, tx - 10, ty, col_white);
    fast_line(it, tx + 16, ty - 5, tx + 10, ty, col_white);
    fast_filled_circle(it, tx, ty, 10, col_magenta);
    fast_circle(it, tx, ty, 10, col_white);
    fast_line(it, tx - 5, ty - 6, tx + 5, ty + 6, col_white);
    fast_line(it, tx - 4, ty - 6, tx + 6, ty + 6, UiColor(255, 190, 230));
    fast_filled_circle(it, tx - 3, ty - 3, 2, col_white);
  } else {
    fast_filled_circle(it, tx, ty, 12, col_cyan);
    fast_circle(it, tx, ty, 12, col_white);
    fast_filled_circle(it, tx - 4, ty - 4, 3, col_white);
    fast_line(it, tx, ty - 6, tx, ty + 6, col_gold);
    fast_line(it, tx - 6, ty, tx + 6, ty, col_gold);
    fast_filled_circle(it, tx, ty, 2, col_gold);
  }
}

void render_chomp_character(Rgb565Surface &it, int mx, int my, int mr,
                            float look_dx, float look_dy, bool mouth_open, bool is_chomping,
                            uint32_t now_ms, UiColor col_white, UiColor col_black, UiColor col_dark_gray) {
  const UiColor fur_main(28, 148, 248), fur_dark(16, 96, 182), fur_hi(110, 198, 255);
  const UiColor ear_inner(255, 158, 188), tongue_pink(255, 128, 166);
  const int cy_m = my + (is_chomping ? (((now_ms / 70U) & 1U) ? -3 : 2) : 0);

  fast_filled_circle(it, mx, cy_m + 4, mr, UiColor(12, 16, 24));
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
  fast_filled_circle(it, mx - mr + 5, cy_m - 4, tuft_r, fur_main);
  fast_filled_circle(it, mx + mr - 5, cy_m - 4, tuft_r, fur_main);
  fast_filled_circle(it, mx - mr + 7, cy_m + 10, tuft_r, fur_main);
  fast_filled_circle(it, mx + mr - 7, cy_m + 10, tuft_r, fur_main);
  fast_filled_circle(it, mx, cy_m - mr + 3, tuft_r, fur_main);

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
    fast_filled_circle(it, mx - mr / 2 - c_off, mouth_y - 2 + c_off / 2, 2, crumb);
    fast_filled_circle(it, mx + mr / 2 + c_off, mouth_y - 4 + c_off / 2, 2, crumb);
    fast_filled_circle(it, mx + 3, mouth_y + mr / 2 + c_off / 2, 2, crumb);
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

void render_game_chomp(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                       bool game_just_entered, float roll_delta, float pitch_delta,
                       float &marble_touch_x, bool &game_btn_pressed, int32_t &high_score_chomp) {
  static constexpr int NUM_SNACKS = 7;
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto *snacks = arena.snacks;
  auto *confetti = arena.confetti;
  static float mon_x = 233.0f, mon_y = 233.0f, mon_vx = 0.0f, mon_vy = 0.0f;
  static uint32_t last_phys_ms = 0, entry_ms = 0, chomp_timer = 0, level_clear_until_ms = 0;
  static bool post_play_tared = false;
  static int level_num = 1, snacks_left = NUM_SNACKS, snack_score = 0, next_kind = 0, chomp_word_idx = 0;
  const int bot_wall_y = layout().back_y - scale_y(60);

  auto spawn_level_platter = [&](int lv, uint32_t seed_base) {
    snacks_left = NUM_SNACKS;
    const float base_ang = static_cast<float>(lv) * 0.45f;
    for (int i = 0; i < NUM_SNACKS; i++) {
      const int s = static_cast<int>(seed_base + static_cast<uint32_t>(i) * 73U + static_cast<uint32_t>(lv) * 37U);
      const float ang = base_ang + static_cast<float>(i) * (2.0f * PI_F / static_cast<float>(NUM_SNACKS)) +
                        (hash2d_01(s, 11) - 0.5f) * 0.35f;
      const float rad = scale_min_f(76.0f + (static_cast<float>((i * 3) % 4) * 26.0f) + hash2d_01(s, 29) * 18.0f);
      float tx = static_cast<float>(cx) + fast_cos(ang) * rad;
      float ty = std::min(static_cast<float>(bot_wall_y - scale_min(18)),
                          static_cast<float>(cy) + fast_sin(ang) * rad);
      const float dmx = tx - mon_x, dmy = ty - mon_y, min_sep = scale_min_f(70.0f);
      if (dmx * dmx + dmy * dmy < min_sep * min_sep) {
        tx = static_cast<float>(cx) - dmx * 1.1f;
        ty = static_cast<float>(cy) - dmy * 1.1f;
      }
      const float drift_spd = (lv <= 1) ? 0.0f : scale_min_f(std::min(68.0f, 22.0f + static_cast<float>(lv - 2) * 12.0f));
      const float v_ang = hash2d_01(s, 47) * (2.0f * PI_F);
      snacks[i] = {tx, ty, fast_cos(v_ang) * drift_spd, fast_sin(v_ang) * drift_spd, next_kind++, false};
    }
  };

  if (game_just_entered) {
    last_phys_ms = now_ms;
    entry_ms = now_ms;
    post_play_tared = false;
    mon_x = static_cast<float>(cx);
    mon_y = static_cast<float>(cy);
    mon_vx = 0.0f;
    mon_vy = 0.0f;
    level_num = 1;
    snack_score = 0;
    next_kind = 0;
    chomp_timer = 0;
    level_clear_until_ms = 0;
    pending_game_sfx() = 0;
    spawn_level_platter(1, now_ms);
  } else if (!post_play_tared && (now_ms - entry_ms >= 220U)) {
    post_play_tared = true;
    game_imu_retare_flag() = true;
  }

  float dt = (now_ms >= last_phys_ms) ? (static_cast<float>(now_ms - last_phys_ms) * 0.001f) : 0.05f;
  last_phys_ms = now_ms;
  dt = std::clamp(dt, 0.012f, 0.075f);

  bool in_celebration = (level_clear_until_ms > 0U && now_ms < level_clear_until_ms);
  if (in_celebration) {
    mon_vx = 0.0f;
    mon_vy = 0.0f;
    mon_x += (static_cast<float>(cx) - mon_x) * 0.25f;
    mon_y += (static_cast<float>(cy) - mon_y) * 0.25f;
  } else if (level_clear_until_ms > 0U && now_ms >= level_clear_until_ms) {
    level_clear_until_ms = 0U;
    mon_x = static_cast<float>(cx);
    mon_y = static_cast<float>(cy);
    mon_vx = 0.0f;
    mon_vy = 0.0f;
    level_num++;
    spawn_level_platter(level_num, now_ms);
    in_celebration = false;
  }

  if (!in_celebration && level_num >= 2) {
    for (int i = 0; i < NUM_SNACKS; i++) {
      if (snacks[i].eaten) continue;
      if (level_num >= 3) {
        const float edx = snacks[i].x - mon_x, edy = snacks[i].y - mon_y;
        const float ed2 = edx * edx + edy * edy;
        if (ed2 < 6724.0f && ed2 > 1.0f) {
          const float ed = std::sqrt(ed2);
          const float evade = (1.0f - ed / 82.0f) * std::min(110.0f, static_cast<float>(level_num - 2) * 32.0f);
          snacks[i].vx += (edx / ed) * evade * dt;
          snacks[i].vy += (edy / ed) * evade * dt;
        }
      }
      const float max_t_spd = std::min(78.0f, 24.0f + static_cast<float>(level_num - 2) * 14.0f);
      const float cur_spd = std::sqrt(snacks[i].vx * snacks[i].vx + snacks[i].vy * snacks[i].vy);
      if (cur_spd > max_t_spd && cur_spd > 0.1f) {
        snacks[i].vx = (snacks[i].vx / cur_spd) * max_t_spd;
        snacks[i].vy = (snacks[i].vy / cur_spd) * max_t_spd;
      }
      snacks[i].x += snacks[i].vx * dt;
      snacks[i].y += snacks[i].vy * dt;
      const float max_snack_y = static_cast<float>(bot_wall_y - scale_min(16));
      if (snacks[i].y > max_snack_y) {
        snacks[i].y = max_snack_y;
        if (snacks[i].vy > 0.0f) snacks[i].vy = -snacks[i].vy * 0.9f;
      }
      const float snack_rim_r = scale_min_f(180.0f);
      const float bdx = snacks[i].x - static_cast<float>(cx), bdy = snacks[i].y - static_cast<float>(cy);
      const float bd = std::sqrt(bdx * bdx + bdy * bdy);
      if (bd > snack_rim_r && bd > 0.1f) {
        const float nx = bdx / bd, ny = bdy / bd;
        snacks[i].x = static_cast<float>(cx) + nx * snack_rim_r;
        snacks[i].y = static_cast<float>(cy) + ny * snack_rim_r;
        const float v_dot = snacks[i].vx * nx + snacks[i].vy * ny;
        if (v_dot > 0.0f) {
          snacks[i].vx -= 1.9f * v_dot * nx;
          snacks[i].vy -= 1.9f * v_dot * ny;
        }
      }
    }
  }

  float nearest_dist = 999.0f, look_dx = 0.0f, look_dy = 0.0f;
  for (int i = 0; i < NUM_SNACKS; i++) {
    if (snacks[i].eaten) continue;
    const float dx = snacks[i].x - mon_x, dy = snacks[i].y - mon_y;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d < nearest_dist) {
      nearest_dist = d;
      look_dx = dx / (d + 0.1f);
      look_dy = dy / (d + 0.1f);
    }
  }

  if (marble_touch_x >= 0.0f || game_btn_pressed) {
    marble_touch_x = -1.0f;
    game_btn_pressed = false;
    if (!in_celebration && nearest_dist < 900.0f) {
      mon_vx += look_dx * 165.0f;
      mon_vy += look_dy * 165.0f;
    }
  }

  const float ax = std::clamp(roll_delta * 32.0f, -620.0f, 620.0f);
  const float ay = std::clamp(-pitch_delta * 32.0f, -620.0f, 620.0f);
  float pull_x = 0.0f, pull_y = 0.0f;
  if (!in_celebration && nearest_dist < 38.0f) {
    const float pull_mag = (1.0f - nearest_dist / 38.0f) * 110.0f;
    pull_x = look_dx * pull_mag;
    pull_y = look_dy * pull_mag;
  }

  const float damping = std::pow(0.925f, dt / 0.05f);
  mon_vx = (mon_vx + (ax + pull_x) * dt) * damping;
  mon_vy = (mon_vy + (ay + pull_y) * dt) * damping;
  mon_x += mon_vx * dt;
  mon_y += mon_vy * dt;

  const int mon_r = scale_min(34);
  const float max_mon_y = static_cast<float>(bot_wall_y - mon_r);
  if (mon_y > max_mon_y) {
    mon_y = max_mon_y;
    if (mon_vy > 0.0f) mon_vy = -mon_vy * 0.6f;
  }
  const float mdx = mon_x - static_cast<float>(cx), mdy = mon_y - static_cast<float>(cy);
  const float mdist = std::sqrt(mdx * mdx + mdy * mdy);
  const float max_mon_r = scale_min_f(172.0f);
  if (mdist > max_mon_r && mdist > 0.001f) {
    const float nx = mdx / mdist, ny = mdy / mdist;
    mon_x = static_cast<float>(cx) + nx * max_mon_r;
    mon_y = static_cast<float>(cy) + ny * max_mon_r;
    const float v_dot = mon_vx * nx + mon_vy * ny;
    if (v_dot > 0.0f) {
      mon_vx -= 1.6f * v_dot * nx;
      mon_vy -= 1.6f * v_dot * ny;
    }
  }

  const float chomp_cap_r = scale_min_f(36.0f);
  if (!in_celebration) {
    for (int i = 0; i < NUM_SNACKS; i++) {
      if (snacks[i].eaten) continue;
      const float dx = snacks[i].x - mon_x, dy = snacks[i].y - mon_y;
      if (std::sqrt(dx * dx + dy * dy) < chomp_cap_r) {
        snacks[i].eaten = true;
        snacks_left--;
        snack_score++;
        chomp_timer = now_ms;
        chomp_word_idx = (chomp_word_idx + 1) % 5;
        if (snack_score > high_score_chomp) high_score_chomp = snack_score;
        if (snacks_left <= 0) {
          snack_score += 3;
          if (snack_score > high_score_chomp) {
            high_score_chomp = snack_score;
            schedule_persisted_prefs_save(now_ms);
          }
          level_clear_until_ms = now_ms + 3000U;
          in_celebration = true;
          pending_game_sfx() = 5;
          const UiColor p_cols[5] = {ctx.col_gold, ctx.col_cyan, ctx.col_magenta, ctx.col_green, ctx.col_orange};
          for (int c = 0; c < 16; c++) {
            const float ang = static_cast<float>(c) * (2.0f * PI_F / 16.0f);
            const float spd = 105.0f + static_cast<float>((c % 3) * 34);
            confetti[c].x = mon_x + fast_cos(ang) * 20.0f;
            confetti[c].y = mon_y + fast_sin(ang) * 20.0f;
            confetti[c].vx = fast_cos(ang) * spd;
            confetti[c].vy = fast_sin(ang) * spd - 40.0f;
            const UiColor pc = p_cols[c % 5];
            confetti[c].r = pc.r;
            confetti[c].g = pc.g;
            confetti[c].b = pc.b;
          }
        } else {
          pending_game_sfx() = 1 + ((snack_score + snacks[i].kind) & 3);
        }
        break;
      }
    }
  }

  if (in_celebration) {
    for (int c = 0; c < 16; c++) {
      confetti[c].x += confetti[c].vx * dt;
      confetti[c].y += confetti[c].vy * dt;
      confetti[c].vy += 110.0f * dt;
    }
  }

  const bool is_chomping = in_celebration || (chomp_timer > 0U && (now_ms - chomp_timer < 520U));
  const bool mouth_open = (!in_celebration && nearest_dist < 82.0f);

  fast_circle(it, cx, cy, scale_min(208), ctx.col_dark_gray);
  fast_line(it, cx - scale_min(151), bot_wall_y, cx + scale_min(151), bot_wall_y, ctx.col_dark_gray);
  char t_buf[56];
  std::snprintf(t_buf, sizeof(t_buf), "Lv %d  •  Left:%d/7  •  Score:%d  •  Best:%d",
                level_num, snacks_left, snack_score, static_cast<int>(high_score_chomp));
  render_game_hud_footer(it, cx, t_buf, ctx.font_small, ctx.col_gold);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "Exit");

  const float t_bob = static_cast<float>(now_ms) * 0.0045f;
  for (int i = 0; i < NUM_SNACKS; i++) {
    if (snacks[i].eaten) continue;
    const int bob_y = static_cast<int>(fast_sin(t_bob + static_cast<float>(i) * 1.15f) * 2.5f);
    render_chomp_snack(it, static_cast<int>(snacks[i].x), static_cast<int>(snacks[i].y) + bob_y,
                       snacks[i].kind, ctx.col_white, ctx.col_red, ctx.col_gold, ctx.col_cyan,
                       ctx.col_green, ctx.col_magenta, ctx.col_bg);
  }

  const int imx = static_cast<int>(mon_x), imy = static_cast<int>(mon_y);
  render_chomp_character(it, imx, imy, mon_r, look_dx, look_dy, mouth_open, is_chomping,
                         now_ms, ctx.col_white, ctx.col_black, ctx.col_dark_gray);

  if (in_celebration) {
    const uint32_t rem_cel = level_clear_until_ms - now_ms;
    char p_msg[36];
    if (rem_cel > 1900U) {
      for (int c = 0; c < 16; c++) {
        fast_filled_circle(it, static_cast<int>(confetti[c].x), static_cast<int>(confetti[c].y), scale_min(3),
                           UiColor(confetti[c].r, confetti[c].g, confetti[c].b));
      }
      std::snprintf(p_msg, sizeof(p_msg), "LEVEL %d CLEAR!", level_num);
      fast_print(it, cx, imy - scale_min(62), ctx.font_date, ctx.col_gold, UiTextAlign::CENTER, p_msg);
    } else {
      std::snprintf(p_msg, sizeof(p_msg), "GET READY! LEVEL %d", level_num + 1);
      fast_print(it, cx, imy - scale_min(62), ctx.font_date, ctx.col_cyan, UiTextAlign::CENTER, p_msg);
    }
  } else if (is_chomping) {
    static constexpr const char *CHOMP_WORDS[5] = {
        "NOM NOM!", "YUMMY!", "COOKIE!", "CHOMP!", "DELICIOUS!"};
    const int pop_y = (imy > scale_y(150)) ? (imy - scale_min(52)) : (imy + scale_min(52));
    const int pop_x = std::clamp(imx, scale_x(115), SCREEN_W - scale_x(115));
    fast_print(it, pop_x, pop_y, ctx.font_date, ctx.col_gold, UiTextAlign::CENTER, CHOMP_WORDS[chomp_word_idx]);
    fast_filled_circle(it, imx - scale_min(42), imy - scale_min(16), scale_min(3), ctx.col_gold);
    fast_filled_circle(it, imx + scale_min(42), imy - scale_min(16), scale_min(3), ctx.col_cyan);
  }
}

}  // namespace latter_ui
