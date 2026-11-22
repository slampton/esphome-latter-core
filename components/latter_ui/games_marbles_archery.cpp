#include "games_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace latter_ui {

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

void render_game_marbles(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                         bool game_just_entered, float roll_delta, float pitch_delta,
                         float &marble_touch_x, float &marble_touch_y,
                         bool &game_btn_pressed, int32_t &high_score_marbles) {
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto *balls = arena.balls;
  static uint32_t last_phys_ms = 0;
  static uint32_t last_sink_ms = 0;
  static uint32_t wave_clear_ms = 0;
  static uint32_t popup_ms = 0;
  static int popup_pts = 0;
  static int popup_combo = 1;
  static int combo_streak = 0;
  static int wave_num = 1;
  static float time_left_f = 45.0f;
  static int game_score = 0;
  static bool round_over = false;
  static bool is_new_high = false;
  static float ring_wave_r = -1.0f;
  static float ring_wave_x = 233.0f;
  static float ring_wave_y = 233.0f;
  static uint32_t peg_hit_ms[4] = {0, 0, 0, 0};

  auto spawn_wave_balls = [&](int w_idx) {
    static constexpr float RADII[5] = {20.0f, 18.0f, 16.0f, 14.0f, 12.0f};
    const float base_ang = static_cast<float>(w_idx - 1) * 0.65f;
    for (int i = 0; i < 5; i++) {
      const float ang = base_ang + static_cast<float>(i) * 1.256637f;
      float s_a = 0.0f, c_a = 1.0f;
      fast_sincos(ang, s_a, c_a);
      const float dist = scale_min_f(112.0f + static_cast<float>((i & 1) * 16));
      balls[i] = {static_cast<float>(cx) + c_a * dist,
                  static_cast<float>(cy) + s_a * dist,
                  0.0f, 0.0f, scale_min_f(RADII[i]), i, false, 0U, 0.0f, 0.0f};
    }
  };

  auto reset_full_game = [&]() {
    wave_num = 1;
    time_left_f = 45.0f;
    last_phys_ms = now_ms;
    last_sink_ms = 0;
    wave_clear_ms = 0;
    popup_ms = 0;
    combo_streak = 0;
    game_score = 0;
    round_over = false;
    is_new_high = false;
    ring_wave_r = -1.0f;
    for (int p = 0; p < 4; p++) peg_hit_ms[p] = 0;
    spawn_wave_balls(1);
  };

  if (game_just_entered) {
    reset_full_game();
  }

  float dt = (last_phys_ms == 0U || now_ms < last_phys_ms)
                 ? 0.05f
                 : static_cast<float>(now_ms - last_phys_ms) * 0.001f;
  last_phys_ms = now_ms;
  dt = std::clamp(dt, 0.012f, 0.075f);

  const bool in_wave_pause = (wave_clear_ms > 0U && (now_ms - wave_clear_ms < 3000U));
  if (wave_clear_ms > 0U && (now_ms - wave_clear_ms >= 3000U)) {
    wave_clear_ms = 0U;
    spawn_wave_balls(wave_num);
  }

  if (!round_over && !in_wave_pause) {
    time_left_f -= dt;
    if (time_left_f <= 0.0f) {
      time_left_f = 0.0f;
      round_over = true;
      if (game_score > high_score_marbles) {
        high_score_marbles = game_score;
        is_new_high = true;
      }
      if (is_new_high) schedule_persisted_prefs_save(now_ms);
    }
  }
  int time_remaining = static_cast<int>(std::ceil(time_left_f));

  const int num_pegs = std::min(4, std::max(0, wave_num - 1));
  const float peg_ring_r = scale_min_f(54.0f);
  const float peg_r = scale_min_f(6.0f);
  const int wall_y = layout().back_y - scale_y(60);
  static constexpr float PEG_BASE_ANG[4] = {-1.5707963f, 1.5707963f, 3.1415926f, 0.0f};
  float peg_x[4] = {0, 0, 0, 0};
  float peg_y[4] = {0, 0, 0, 0};
  const float orbit_offset = (wave_num >= 6)
      ? (static_cast<float>(now_ms % 60000U) * 0.001f * 0.30f * static_cast<float>(std::min(4, wave_num - 5)))
      : 0.0f;
  for (int p = 0; p < num_pegs; p++) {
    float s_p = 0.0f, c_p = 1.0f;
    fast_sincos(PEG_BASE_ANG[p] + orbit_offset, s_p, c_p);
    peg_x[p] = static_cast<float>(cx) + c_p * peg_ring_r;
    peg_y[p] = static_cast<float>(cy) + s_p * peg_ring_r;
  }

  if (marble_touch_x >= 0.0f || game_btn_pressed) {
    const float tx = (marble_touch_x >= 0.0f) ? marble_touch_x : static_cast<float>(cx);
    const float ty = (marble_touch_y >= 0.0f) ? marble_touch_y : static_cast<float>(cy);
    marble_touch_x = -1.0f;
    game_btn_pressed = false;

    if (round_over) {
      reset_full_game();
      game_imu_retare_flag() = true;
    } else {
      ring_wave_x = tx;
      ring_wave_y = ty;
      ring_wave_r = 12.0f;
      for (int i = 0; i < 5; i++) {
        if (balls[i].sunk) continue;
        const float tdx = balls[i].x - tx;
        const float tdy = balls[i].y - ty;
        const float tdist = std::sqrt(tdx * tdx + tdy * tdy);
        const float mass_scale = 16.0f / balls[i].r;
        if (tdist < 22.0f) {
          const float rnd_angle = static_cast<float>(static_cast<uint32_t>(i * 72) + (now_ms % 360U)) * (PI_F / 180.0f);
          float s_rnd = 0.0f, c_rnd = 1.0f;
          fast_sincos(rnd_angle, s_rnd, c_rnd);
          balls[i].vx += c_rnd * 620.0f * mass_scale;
          balls[i].vy += s_rnd * 620.0f * mass_scale;
        } else {
          const float impulse = std::min(820.0f, (480.0f + (680.0f / (tdist * 0.08f + 1.0f))) * mass_scale);
          balls[i].vx += (tdx / tdist) * impulse;
          balls[i].vy += (tdy / tdist) * impulse;
        }
      }
    }
  }

  const float friction = std::pow(0.975f, dt / 0.05f);
  const float bounce = 0.82f;
  const float arena_r = scale_min_f(208.0f);
  const float hole_cap_r = scale_min_f(24.0f);
  const float hole_bevel_r = scale_min_f(42.0f);
  const float outer_lip_r = scale_min_f(180.0f);

  const float ax = std::clamp(roll_delta * 34.0f, -650.0f, 650.0f);
  const float ay = std::clamp(-pitch_delta * 34.0f, -650.0f, 650.0f);

  int sunk_count = 0;
  if (!round_over) {
    for (int i = 0; i < 5; i++) {
      if (balls[i].sunk) {
        sunk_count++;
        continue;
      }

      const float dx = balls[i].x - static_cast<float>(cx);
      const float dy = balls[i].y - static_cast<float>(cy);
      const float dist_hole = std::sqrt(dx * dx + dy * dy);

      if (dist_hole < hole_cap_r) {
        balls[i].sunk = true;
        balls[i].sink_start_ms = now_ms;
        balls[i].sink_x = balls[i].x;
        balls[i].sink_y = balls[i].y;
        balls[i].vx = 0.0f;
        balls[i].vy = 0.0f;
        sunk_count++;

        if (last_sink_ms > 0U && (now_ms - last_sink_ms < 3500U)) {
          combo_streak = std::min(4, combo_streak + 1);
        } else {
          combo_streak = 1;
        }
        last_sink_ms = now_ms;
        const int pts = 200 + (combo_streak - 1) * 150;
        game_score += pts;
        popup_pts = pts;
        popup_combo = combo_streak;
        popup_ms = now_ms;
        pending_game_sfx() = 6;
        continue;
      }

      if (dist_hole < hole_bevel_r && dist_hole >= hole_cap_r) {
        const float bevel_pull = (hole_bevel_r - dist_hole) * 9.5f;
        balls[i].vx -= (dx / dist_hole) * bevel_pull * dt;
        balls[i].vy -= (dy / dist_hole) * bevel_pull * dt;
      } else if (dist_hole > outer_lip_r) {
        const float rim_nudge = (dist_hole - outer_lip_r) * 1.15f;
        balls[i].vx -= (dx / dist_hole) * rim_nudge * dt;
        balls[i].vy -= (dy / dist_hole) * rim_nudge * dt;
      }

      const float inertia_scale = 0.92f + (scale_min_f(20.0f) - balls[i].r) * 0.02f;
      balls[i].vx = (balls[i].vx + ax * inertia_scale * dt) * friction;
      balls[i].vy = (balls[i].vy + ay * inertia_scale * dt) * friction;
      balls[i].x += balls[i].vx * dt;
      balls[i].y += balls[i].vy * dt;

      for (int p = 0; p < num_pegs; p++) {
        const float pdx = balls[i].x - peg_x[p];
        const float pdy = balls[i].y - peg_y[p];
        const float pdist2 = pdx * pdx + pdy * pdy;
        const float min_pdist = balls[i].r + peg_r;
        if (pdist2 < min_pdist * min_pdist && pdist2 > 0.0001f) {
          const float pdist = std::sqrt(pdist2);
          const float pnx = pdx / pdist;
          const float pny = pdy / pdist;
          balls[i].x = peg_x[p] + pnx * min_pdist;
          balls[i].y = peg_y[p] + pny * min_pdist;
          const float v_dot_n = balls[i].vx * pnx + balls[i].vy * pny;
          if (v_dot_n < 0.0f) {
            balls[i].vx -= 1.88f * v_dot_n * pnx;
            balls[i].vy -= 1.88f * v_dot_n * pny;
          }
          balls[i].vx += pnx * 38.0f;
          balls[i].vy += pny * 38.0f;
          if (now_ms - peg_hit_ms[p] > 180U) {
            game_score += 15;
            if (pending_game_sfx() == 0) pending_game_sfx() = 7;
          }
          peg_hit_ms[p] = now_ms;
        }
      }

      const float max_bot_y = static_cast<float>(wall_y) - balls[i].r;
      if (balls[i].y > max_bot_y) {
        balls[i].y = max_bot_y;
        if (balls[i].vy > 0.0f) balls[i].vy = -balls[i].vy * bounce;
      }

      const float cur_dx = balls[i].x - static_cast<float>(cx);
      const float cur_dy = balls[i].y - static_cast<float>(cy);
      const float cur_dist = std::sqrt(cur_dx * cur_dx + cur_dy * cur_dy);
      const float max_dist = arena_r - balls[i].r;
      if (cur_dist > max_dist && cur_dist > 0.001f) {
        const float nx = cur_dx / cur_dist;
        const float ny = cur_dy / cur_dist;
        balls[i].x = static_cast<float>(cx) + nx * max_dist;
        balls[i].y = static_cast<float>(cy) + ny * max_dist;
        const float v_dot_n = balls[i].vx * nx + balls[i].vy * ny;
        if (v_dot_n > 0.0f) {
          balls[i].vx -= (1.0f + bounce) * v_dot_n * nx;
          balls[i].vy -= (1.0f + bounce) * v_dot_n * ny;
        }
        const float tx_w = -ny;
        const float ty_w = nx;
        const float v_dot_t = balls[i].vx * tx_w + balls[i].vy * ty_w;
        balls[i].vx -= 0.07f * v_dot_t * tx_w;
        balls[i].vy -= 0.07f * v_dot_t * ty_w;
      }
    }

    for (int i = 0; i < 5; i++) {
      if (balls[i].sunk) continue;
      for (int j = i + 1; j < 5; j++) {
        if (balls[j].sunk) continue;
        const float cdx = balls[j].x - balls[i].x;
        const float cdy = balls[j].y - balls[i].y;
        const float min_dist = balls[i].r + balls[j].r;
        const float cdist2 = cdx * cdx + cdy * cdy;
        if (cdist2 < min_dist * min_dist && cdist2 > 0.0001f) {
          const float cdist = std::sqrt(cdist2);
          const float overlap = min_dist - cdist;
          const float cnx = cdx / cdist;
          const float cny = cdy / cdist;
          const float mi = balls[i].r * balls[i].r;
          const float mj = balls[j].r * balls[j].r;
          const float inv_m_sum = 1.0f / (mi + mj);

          balls[i].x -= cnx * overlap * (mj * inv_m_sum);
          balls[i].y -= cny * overlap * (mj * inv_m_sum);
          balls[j].x += cnx * overlap * (mi * inv_m_sum);
          balls[j].y += cny * overlap * (mi * inv_m_sum);

          const float kx = balls[i].vx - balls[j].vx;
          const float ky = balls[i].vy - balls[j].vy;
          const float v_rel = cnx * kx + cny * ky;
          if (v_rel > 0.0f) {
            const float imp = (1.0f + bounce) * v_rel * inv_m_sum;
            balls[i].vx -= imp * mj * cnx;
            balls[i].vy -= imp * mj * cny;
            balls[j].vx += imp * mi * cnx;
            balls[j].vy += imp * mi * cny;
          }
        }
      }
    }

    if (sunk_count >= 5 && wave_clear_ms == 0U) {
      bool any_animating = false;
      for (int i = 0; i < 5; i++) {
        if (now_ms - balls[i].sink_start_ms < 240U) any_animating = true;
      }
      if (!any_animating) {
        game_score += 300 + wave_num * 100;
        time_left_f = std::min(60.0f, time_left_f + 6.0f);
        time_remaining = static_cast<int>(std::ceil(time_left_f));
        wave_num++;
        wave_clear_ms = now_ms;
        pending_game_sfx() = 5;
        if (game_score > high_score_marbles) {
          high_score_marbles = game_score;
          is_new_high = true;
          schedule_persisted_prefs_save(now_ms);
        }
      }
    }
  } else {
    for (int i = 0; i < 5; i++) {
      if (balls[i].sunk) sunk_count++;
    }
  }

  const int wall_hw = scale_min(151);
  fast_line(it, cx - wall_hw, wall_y, cx + wall_hw, wall_y, scale_color(ctx.col_slate, 0.45f));
  fast_line(it, cx - wall_hw + 2, wall_y + 1, cx + wall_hw - 2, wall_y + 1, ctx.col_dark_gray);
  char hud_buf[56];
  std::snprintf(hud_buf, sizeof(hud_buf), "W%d  •  %d/5  •  %ds  •  %d",
                wave_num, sunk_count, time_remaining, game_score);
  render_game_hud_footer(it, cx, hud_buf, ctx.font_small, time_remaining < 10 ? ctx.col_orange : ctx.col_gold);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "Exit");

  fast_circle(it, cx, cy, scale_min(34), ctx.col_dark_gray);
  fast_circle(it, cx, cy, scale_min(30), ctx.col_slate);
  fast_circle(it, cx, cy, scale_min(29), ctx.col_white);
  fast_circle(it, cx, cy, scale_min(28), ctx.col_white);
  fast_filled_circle(it, cx, cy, scale_min(25), ctx.col_bg);

  const float swirl = static_cast<float>((now_ms / 25U) % 360U) * (PI_F / 180.0f);
  for (int s = 0; s < 4; s++) {
    const float sa = swirl + static_cast<float>(s) * (PI_F * 0.5f);
    float s_sa = 0.0f, c_sa = 1.0f;
    fast_sincos(sa, s_sa, c_sa);
    fast_line(it, cx + static_cast<int>(c_sa * scale_min_f(26.0f)),
              cy + static_cast<int>(s_sa * scale_min_f(26.0f)),
              cx + static_cast<int>(c_sa * scale_min_f(32.0f)),
              cy + static_cast<int>(s_sa * scale_min_f(32.0f)), ctx.col_slate);
  }

  for (int p = 0; p < num_pegs; p++) {
    const int px = static_cast<int>(std::round(peg_x[p]));
    const int py = static_cast<int>(std::round(peg_y[p]));
    const bool peg_flashing = (peg_hit_ms[p] > 0U && (now_ms - peg_hit_ms[p] < 200U));
    if (peg_flashing) {
      fast_circle(it, px, py, 10, ctx.col_gold);
      fast_circle(it, px, py, 8, ctx.col_cyan);
    }
    fast_filled_circle(it, px + 1, py + 2, 6, UiColor(10, 14, 20));
    fast_filled_circle(it, px, py, 6, peg_flashing ? ctx.col_gold : ctx.col_slate);
    fast_circle(it, px, py, 6, ctx.col_white);
    fast_filled_circle(it, px - 1, py - 1, 2, peg_flashing ? ctx.col_white : ctx.col_cyan);
  }

  if (ring_wave_r >= 0.0f) {
    fast_circle(it, static_cast<int>(ring_wave_x), static_cast<int>(ring_wave_y),
                static_cast<int>(ring_wave_r), ctx.col_gold);
    fast_circle(it, static_cast<int>(ring_wave_x), static_cast<int>(ring_wave_y),
                static_cast<int>(ring_wave_r) + 2, ctx.col_cyan);
    ring_wave_r += 26.0f;
    if (ring_wave_r > 190.0f) ring_wave_r = -1.0f;
  }

  const UiColor base_cols[5] = {ctx.col_gold, ctx.col_cyan, ctx.col_green, ctx.col_red, ctx.col_magenta};
  const UiColor hi_cols[5] = {
      UiColor(255, 245, 157), UiColor(180, 255, 255), UiColor(185, 246, 202),
      UiColor(255, 138, 128), UiColor(248, 187, 208)};
  const int sh_x = static_cast<int>(ax * 0.012f);
  const int sh_y = static_cast<int>(ay * 0.012f);

  for (int i = 0; i < 5; i++) {
    if (balls[i].sunk) {
      const uint32_t sink_el = now_ms - balls[i].sink_start_ms;
      if (sink_el < 240U) {
        const float prog = static_cast<float>(sink_el) * (1.0f / 240.0f);
        const int sx = static_cast<int>(balls[i].sink_x + (static_cast<float>(cx) - balls[i].sink_x) * prog);
        const int sy = static_cast<int>(balls[i].sink_y + (static_cast<float>(cy) - balls[i].sink_y) * prog);
        const int sr = std::max(3, static_cast<int>(balls[i].r * (1.0f - prog * 0.82f)));
        render_glass_marble(it, sx, sy, sr, base_cols[balls[i].col_idx], hi_cols[balls[i].col_idx], 0, 0);
      }
      continue;
    }
    render_glass_marble(it, static_cast<int>(balls[i].x), static_cast<int>(balls[i].y),
                        static_cast<int>(balls[i].r),
                        base_cols[balls[i].col_idx], hi_cols[balls[i].col_idx], sh_x, sh_y);
  }

  if (!round_over && popup_ms > 0U && (now_ms - popup_ms < 650U)) {
    const uint32_t pel = now_ms - popup_ms;
    const int py = cy - 42 - static_cast<int>(pel / 22U);
    char pop_buf[24];
    if (popup_combo > 1) {
      std::snprintf(pop_buf, sizeof(pop_buf), "+%d %dx!", popup_pts, popup_combo);
    } else {
      std::snprintf(pop_buf, sizeof(pop_buf), "+%d", popup_pts);
    }
    fast_print(it, cx, py, ctx.font_small, popup_combo > 1 ? ctx.col_white : ctx.col_gold,
               UiTextAlign::CENTER, pop_buf);
  }

  if (!round_over && in_wave_pause) {
    char w_buf[36];
    if (now_ms - wave_clear_ms < 1100U) {
      std::snprintf(w_buf, sizeof(w_buf), "WAVE %d CLEAR! +6s", wave_num - 1);
      fast_print(it, cx, cy - scale_y(107), ctx.font_date, ctx.col_gold, UiTextAlign::CENTER, w_buf);
    } else {
      std::snprintf(w_buf, sizeof(w_buf), "GET READY! WAVE %d", wave_num);
      fast_print(it, cx, cy - scale_y(107), ctx.font_date, ctx.col_cyan, UiTextAlign::CENTER, w_buf);
    }
  }

  if (round_over) {
    char title_buf[32];
    UiColor title_col = ctx.col_gold;
    if (is_new_high) {
      std::snprintf(title_buf, sizeof(title_buf), "NEW HIGH SCORE!");
      title_col = ctx.col_gold;
    } else if (wave_num == 1) {
      std::snprintf(title_buf, sizeof(title_buf), "GAME OVER");
      title_col = ctx.col_red;
    } else {
      std::snprintf(title_buf, sizeof(title_buf), "WAVE %d REACHED", wave_num);
      title_col = ctx.col_cyan;
    }
    char score_str[32];
    std::snprintf(score_str, sizeof(score_str), "Score: %d", game_score);
    char best_str[32];
    std::snprintf(best_str, sizeof(best_str), "Best: %d", static_cast<int>(high_score_marbles));
    ui::game_over_sheet(it, cx, cy, title_buf, title_col, score_str, best_str, is_new_high,
                        "PLAY AGAIN", ctx.font_date, ctx.font_small,
                        ctx.theme_primary, ctx.col_white, ctx.col_gold, ctx.col_slate, WidgetId::GAME_OVER_CTA);
  }
}

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

  fast_filled_circle(it, tx, ty + 3, r_outer, col_dark_gray);
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

void render_game_archery(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                         bool game_just_entered, float roll_delta, float pitch_delta,
                         float &marble_touch_x, bool &game_btn_pressed,
                         int32_t &high_score_archery) {
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  static uint32_t last_phys_ms = 0;
  static int round_num = 1;
  static int arrows_left = 6;
  static int round_pts = 0;
  static int arch_score = 0;
  static int streak_mult = 1;
  static float target_phase = 0.0f;
  static float smooth_ax = 233.0f;
  static float smooth_ay = 112.0f;
  static bool arrow_in_flight = false;
  static float flight_z = 0.0f;
  static float launch_aim_x = 233.0f;
  static float launch_aim_y = 112.0f;
  static float launch_wind = 0.0f;
  static float wind_speed = 0.0f;
  static char hit_feedback[36] = "";
  static UiColor feedback_col(255, 215, 0);
  static uint32_t feedback_timer = 0;
  static uint32_t round_banner_ms = 0;
  static bool round_over = false;
  static bool is_new_high = false;
  auto *pinned = arena.pinned;
  static int pinned_count = 0;

  auto clear_pinned = [&]() {
    pinned_count = 0;
    for (int i = 0; i < 10; i++) pinned[i].active = false;
  };

  auto reset_full_game = [&]() {
    last_phys_ms = now_ms;
    round_num = 1;
    arrows_left = 6;
    round_pts = 0;
    arch_score = 0;
    streak_mult = 1;
    target_phase = 0.0f;
    smooth_ax = static_cast<float>(cx);
    smooth_ay = 112.0f;
    arrow_in_flight = false;
    flight_z = 0.0f;
    hit_feedback[0] = '\0';
    feedback_timer = 0;
    round_banner_ms = 0;
    round_over = false;
    is_new_high = false;
    clear_pinned();
  };

  if (game_just_entered) {
    reset_full_game();
  }

  float dt = (last_phys_ms == 0U || now_ms < last_phys_ms)
                 ? 0.05f
                 : static_cast<float>(now_ms - last_phys_ms) * 0.001f;
  last_phys_ms = now_ms;
  dt = std::clamp(dt, 0.012f, 0.075f);

  const int target_r = scale_min(std::max(26, 36 - (round_num - 1) * 2));
  const float sway_spd = 0.78f + static_cast<float>(std::min(6, round_num - 1)) * 0.16f;
  target_phase += sway_spd * dt;

  const float target_base_y = static_cast<float>(scale_y(112));
  const float bow_y = static_cast<float>(layout().back_y - scale_y(88));
  const float target_x = static_cast<float>(cx) + fast_sin(target_phase) * scale_min_f(102.0f);
  const float target_y = target_base_y + fast_cos(target_phase * 1.4f) * scale_min_f(24.0f);

  const float max_wind = std::min(8.0f, 2.5f + static_cast<float>(round_num - 1) * 1.3f);
  wind_speed = fast_sin(static_cast<float>(now_ms % 62832U) * 0.00065f) * max_wind;

  float raw_ax = static_cast<float>(cx) + roll_delta * scale_min_f(6.8f);
  float raw_ay = target_base_y - pitch_delta * scale_min_f(6.8f);
  if (touch_gesture().active && touch_gesture().last_y < layout().back_y - scale_y(42) &&
      (LATTER_HAS_IMU == 0 || touch_gesture().max_travel >= scale_s(8))) {
    raw_ax = static_cast<float>(touch_gesture().last_x);
    raw_ay = static_cast<float>(touch_gesture().last_y);
  }
  const float max_aim_dx = scale_min_f(173.0f);
  raw_ax = std::clamp(raw_ax, static_cast<float>(cx) - max_aim_dx, static_cast<float>(cx) + max_aim_dx);
  const float min_aim_y = static_cast<float>(scale_y(48));
  const float max_aim_y = bow_y - static_cast<float>(scale_y(50));
  raw_ay = std::clamp(raw_ay, min_aim_y, max_aim_y);
  if (LATTER_HAS_IMU == 0 && touch_gesture().active) {
    smooth_ax = raw_ax;
    smooth_ay = raw_ay;
  } else {
    smooth_ax += (raw_ax - smooth_ax) * 0.45f;
    smooth_ay += (raw_ay - smooth_ay) * 0.45f;
  }

  const int round_goal = 20 + std::min(5, round_num - 1) * 4;

  if (game_btn_pressed || marble_touch_x >= 0.0f) {
    game_btn_pressed = false;
    marble_touch_x = -1.0f;

    if (round_over) {
      reset_full_game();
      game_imu_retare_flag() = true;
    } else if (!arrow_in_flight && arrows_left > 0 && (round_banner_ms == 0U || now_ms - round_banner_ms >= 3000U)) {
      arrow_in_flight = true;
      flight_z = 0.0f;
      launch_aim_x = smooth_ax;
      launch_aim_y = smooth_ay;
      launch_wind = wind_speed;
      arrows_left--;
      pending_game_sfx() = 8;
    }
  }

  float cur_arrow_x = static_cast<float>(cx);
  float cur_arrow_y = bow_y;
  float arrow_dx = 0.0f;
  float arrow_dy = -1.0f;

  if (arrow_in_flight) {
    flight_z += dt * (1.0f / 0.40f);
    const float z = std::min(1.0f, flight_z);
    const float wind_drift = launch_wind * 5.2f * z * z;
    const float arc_lift = -26.0f * z * (1.0f - z);

    cur_arrow_x = static_cast<float>(cx) + (launch_aim_x - static_cast<float>(cx)) * z + wind_drift;
    cur_arrow_y = bow_y + (launch_aim_y - bow_y) * z + arc_lift;

    const float vx = (launch_aim_x - static_cast<float>(cx)) + launch_wind * 10.4f * z;
    const float vy = (launch_aim_y - bow_y) - 26.0f * (1.0f - 2.0f * z);
    const float vlen = std::sqrt(vx * vx + vy * vy);
    if (vlen > 0.001f) {
      arrow_dx = vx / vlen;
      arrow_dy = vy / vlen;
    }

    if (flight_z >= 1.0f) {
      arrow_in_flight = false;
      const float hit_dx = cur_arrow_x - target_x;
      const float hit_dy = cur_arrow_y - target_y;
      const float dist_hit = std::sqrt(hit_dx * hit_dx + hit_dy * hit_dy);

      const float r_out = static_cast<float>(target_r);
      int base_pts = 0;
      bool bullseye = false;
      if (dist_hit <= r_out * 0.18f) {
        base_pts = 10;
        bullseye = true;
      } else if (dist_hit <= r_out * 0.36f) {
        base_pts = 7;
      } else if (dist_hit <= r_out * 0.56f) {
        base_pts = 5;
      } else if (dist_hit <= r_out * 0.78f) {
        base_pts = 3;
      } else if (dist_hit <= r_out + 1.5f) {
        base_pts = 1;
      }

      if (base_pts > 0) {
        const int earned = base_pts * streak_mult;
        arch_score += earned;
        round_pts += earned;
        if (pinned_count < 10) {
          pinned[pinned_count++] = {hit_dx, hit_dy, true};
        }
        if (bullseye) {
          arrows_left = std::min(9, arrows_left + 1);
          std::snprintf(hit_feedback, sizeof(hit_feedback), "BULLSEYE! +%d (+1 ARR)", earned);
          feedback_col = ctx.col_gold;
          pending_game_sfx() = 9;
        } else if (streak_mult > 1) {
          std::snprintf(hit_feedback, sizeof(hit_feedback), "HIT +%d (%dx!)", earned, streak_mult);
          feedback_col = ctx.col_cyan;
          pending_game_sfx() = 10;
        } else {
          std::snprintf(hit_feedback, sizeof(hit_feedback), "HIT +%d", earned);
          feedback_col = ctx.col_gold;
          pending_game_sfx() = 10;
        }
        streak_mult = std::min(3, streak_mult + 1);
      } else {
        streak_mult = 1;
        std::snprintf(hit_feedback, sizeof(hit_feedback), "MISS!");
        feedback_col = ctx.col_red;
        pending_game_sfx() = 14;
      }
      feedback_timer = now_ms;

      if (arch_score > high_score_archery) {
        high_score_archery = arch_score;
        is_new_high = true;
      }

      if (arrows_left <= 0) {
        if (round_pts >= round_goal) {
          arch_score += round_num * 25;
          if (arch_score > high_score_archery) {
            high_score_archery = arch_score;
            is_new_high = true;
          }
          if (is_new_high) schedule_persisted_prefs_save(now_ms);
          round_num++;
          round_pts = 0;
          round_banner_ms = now_ms;
          pending_game_sfx() = 5;
        } else {
          round_over = true;
          if (is_new_high) schedule_persisted_prefs_save(now_ms);
        }
      }
    }
  }

  const int tx_i = static_cast<int>(std::round(target_x));
  const int ty_i = static_cast<int>(std::round(target_y));
  render_archery_target(it, tx_i, ty_i, target_r,
                        ctx.col_white, ctx.col_black, ctx.col_cyan, ctx.col_red,
                        ctx.col_gold, ctx.col_slate, ctx.col_dark_gray);

  for (int i = 0; i < pinned_count; i++) {
    if (!pinned[i].active) continue;
    const int px = tx_i + static_cast<int>(std::round(pinned[i].dx));
    const int py = ty_i + static_cast<int>(std::round(pinned[i].dy));
    fast_filled_circle(it, px, py, 2, ctx.col_black);
    fast_line(it, px, py, px - 5, py + 11, ctx.col_gold);
    fast_line(it, px - 5, py + 11, px - 8, py + 10, ctx.col_red);
    fast_line(it, px - 5, py + 11, px - 3, py + 13, ctx.col_red);
  }

  const int rx_i = static_cast<int>(std::round(smooth_ax));
  const int ry_i = static_cast<int>(std::round(smooth_ay));
  fast_circle(it, rx_i, ry_i, 13, ctx.col_cyan);
  fast_line(it, rx_i - 17, ry_i, rx_i - 5, ry_i, ctx.col_cyan);
  fast_line(it, rx_i + 5, ry_i, rx_i + 17, ry_i, ctx.col_cyan);
  fast_line(it, rx_i, ry_i - 17, rx_i, ry_i - 5, ctx.col_cyan);
  fast_line(it, rx_i, ry_i + 5, rx_i, ry_i + 17, ctx.col_cyan);
  fast_pixel(it, rx_i, ry_i, ctx.col_gold);

  const float aim_dx = smooth_ax - static_cast<float>(cx);
  const float aim_dy = smooth_ay - bow_y;
  const float aim_len = std::sqrt(aim_dx * aim_dx + aim_dy * aim_dy);
  const float ux = (aim_len > 1.0f) ? (aim_dx / aim_len) : 0.0f;
  const float uy = (aim_len > 1.0f) ? (aim_dy / aim_len) : -1.0f;
  const float px_v = -uy;
  const float py_v = ux;

  const int bow_cx = cx;
  const int bow_cy = static_cast<int>(bow_y);
  const int left_tip_x = bow_cx - static_cast<int>(px_v * 38.0f + ux * 10.0f);
  const int left_tip_y = bow_cy - static_cast<int>(py_v * 38.0f + uy * 10.0f);
  const int right_tip_x = bow_cx + static_cast<int>(px_v * 38.0f - ux * 10.0f);
  const int right_tip_y = bow_cy + static_cast<int>(py_v * 38.0f - uy * 10.0f);
  const int left_mid_x = bow_cx - static_cast<int>(px_v * 20.0f - ux * 4.0f);
  const int left_mid_y = bow_cy - static_cast<int>(py_v * 20.0f - uy * 4.0f);
  const int right_mid_x = bow_cx + static_cast<int>(px_v * 20.0f + ux * 4.0f);
  const int right_mid_y = bow_cy + static_cast<int>(py_v * 20.0f + uy * 4.0f);

  fast_line(it, bow_cx, bow_cy, left_mid_x, left_mid_y, ctx.col_gold);
  fast_line(it, left_mid_x, left_mid_y, left_tip_x, left_tip_y, ctx.col_gold);
  fast_line(it, bow_cx, bow_cy, right_mid_x, right_mid_y, ctx.col_gold);
  fast_line(it, right_mid_x, right_mid_y, right_tip_x, right_tip_y, ctx.col_gold);
  fast_line(it, bow_cx, bow_cy + 1, left_mid_x, left_mid_y + 1, ctx.col_slate);
  fast_line(it, bow_cx, bow_cy + 1, right_mid_x, right_mid_y + 1, ctx.col_slate);
  fast_filled_circle(it, bow_cx, bow_cy, 3, ctx.col_red);

  if (!arrow_in_flight && arrows_left > 0 && !round_over) {
    const int nock_x = bow_cx - static_cast<int>(ux * 14.0f);
    const int nock_y = bow_cy - static_cast<int>(uy * 14.0f);
    const int tip_x = bow_cx + static_cast<int>(ux * 18.0f);
    const int tip_y = bow_cy + static_cast<int>(uy * 18.0f);
    fast_line(it, left_tip_x, left_tip_y, nock_x, nock_y, ctx.col_slate);
    fast_line(it, right_tip_x, right_tip_y, nock_x, nock_y, ctx.col_slate);
    fast_line(it, nock_x, nock_y, tip_x, tip_y, ctx.col_white);
    fast_filled_circle(it, tip_x, tip_y, 2, ctx.col_gold);
  } else {
    fast_line(it, left_tip_x, left_tip_y, right_tip_x, right_tip_y, ctx.col_slate);
  }

  if (arrow_in_flight) {
    const float z = std::min(1.0f, flight_z);
    const float a_len = 32.0f - z * 16.0f;
    const int head_x = static_cast<int>(std::round(cur_arrow_x));
    const int head_y = static_cast<int>(std::round(cur_arrow_y));
    const int tail_x = static_cast<int>(std::round(cur_arrow_x - arrow_dx * a_len));
    const int tail_y = static_cast<int>(std::round(cur_arrow_y - arrow_dy * a_len));

    fast_line(it, tail_x, tail_y, head_x, head_y, ctx.col_gold);
    fast_line(it, tail_x + 1, tail_y, head_x + 1, head_y, ctx.col_white);
    fast_filled_circle(it, head_x, head_y, z > 0.65f ? 2 : 3, ctx.col_white);
    const int f_px = static_cast<int>(-arrow_dy * 4.0f);
    const int f_py = static_cast<int>(arrow_dx * 4.0f);
    fast_line(it, tail_x, tail_y, tail_x + f_px - static_cast<int>(arrow_dx * 4.0f),
              tail_y + f_py - static_cast<int>(arrow_dy * 4.0f), ctx.col_red);
    fast_line(it, tail_x, tail_y, tail_x - f_px - static_cast<int>(arrow_dx * 4.0f),
              tail_y - f_py - static_cast<int>(arrow_dy * 4.0f), ctx.col_red);
  }

  if (round_banner_ms > 0U && (now_ms - round_banner_ms >= 1100U) && pinned_count > 0) {
    clear_pinned();
  }
  if (round_banner_ms > 0U && (now_ms - round_banner_ms >= 3000U)) {
    round_banner_ms = 0U;
    arrows_left = 6;
  }

  if (!round_over && round_banner_ms > 0U && (now_ms - round_banner_ms < 3000U)) {
    char rb[36];
    if (now_ms - round_banner_ms < 1100U) {
      std::snprintf(rb, sizeof(rb), "ROUND %d CLEAR!", round_num - 1);
      fast_print(it, cx, cy - scale_y(8), ctx.font_date, ctx.col_gold, UiTextAlign::CENTER, rb);
    } else {
      std::snprintf(rb, sizeof(rb), "ROUND %d! GOAL %d", round_num, round_goal);
      fast_print(it, cx, cy - scale_y(8), ctx.font_date, ctx.col_cyan, UiTextAlign::CENTER, rb);
    }
  } else if (!round_over && hit_feedback[0] != '\0' && (now_ms - feedback_timer < 1100U)) {
    fast_print(it, cx, cy - scale_y(8), ctx.font_date, feedback_col, UiTextAlign::CENTER, hit_feedback);
  }

  char wind_buf[20];
  if (std::fabs(wind_speed) < 1.0f) {
    std::snprintf(wind_buf, sizeof(wind_buf), "CALM");
  } else if (wind_speed > 0.0f) {
    std::snprintf(wind_buf, sizeof(wind_buf), "%.0f mph >", wind_speed);
  } else {
    std::snprintf(wind_buf, sizeof(wind_buf), "< %.0f mph", -wind_speed);
  }
  char hud_buf[56];
  std::snprintf(hud_buf, sizeof(hud_buf), "R%d  •  %d Arr  •  %s  •  %d",
                round_num, arrows_left, wind_buf, arch_score);
  render_game_hud_footer(it, cx, hud_buf, ctx.font_small, ctx.col_gold);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "Exit");

  if (round_over) {
    char title_buf[32];
    UiColor title_col = ctx.col_gold;
    if (is_new_high) {
      std::snprintf(title_buf, sizeof(title_buf), "NEW HIGH SCORE!");
      title_col = ctx.col_gold;
    } else {
      std::snprintf(title_buf, sizeof(title_buf), "ROUND %d REACHED", round_num);
      title_col = ctx.col_cyan;
    }
    char score_str[32];
    std::snprintf(score_str, sizeof(score_str), "Score: %d", arch_score);
    char best_str[32];
    std::snprintf(best_str, sizeof(best_str), "Best: %d", static_cast<int>(high_score_archery));
    ui::game_over_sheet(it, cx, cy, title_buf, title_col, score_str, best_str, is_new_high,
                        "PLAY AGAIN", ctx.font_date, ctx.font_small,
                        ctx.theme_primary, ctx.col_white, ctx.col_gold, ctx.col_slate, WidgetId::GAME_OVER_CTA);
  }
}

}  // namespace latter_ui
