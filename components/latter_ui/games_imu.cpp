#include "games_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace latter_ui {
using namespace latter_core;

// ==============================================================================
// 3. Game 1: Marbles (Universal — All Hardware Traits)
// ==============================================================================
void render_game_marbles(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                         bool game_just_entered, float roll_delta, float pitch_delta,
                         float &marble_touch_x, float &marble_touch_y,
                         bool &game_btn_pressed, int32_t &high_score_marbles) {
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto &st = arena.marbles;
  auto *balls = st.balls;

  auto spawn_wave_balls = [&](int w_idx) {
    static constexpr float RADII[5] = {20.0f, 18.0f, 16.0f, 14.0f, 12.0f};
    const float base_ang = static_cast<float>(w_idx - 1) * 0.65f;
    for (int i = 0; i < 5; i++) {
      const float ang = base_ang + static_cast<float>(i) * 1.256637f;
      float s_a = 0.0f, c_a = 1.0f;
      fast_sincos(ang, s_a, c_a);
      const float dist = game_f(112.0f + static_cast<float>((i & 1) * 16));
      balls[i] = {static_cast<float>(cx) + c_a * dist,
                  static_cast<float>(cy) + s_a * dist,
                  0.0f, 0.0f, game_f(RADII[i]), i, false, 0U, 0.0f, 0.0f};
    }
  };

  auto reset_full_game = [&]() {
    st.wave_num = 1;
    st.time_left_f = 45.0f;
    st.last_phys_ms = now_ms;
    st.last_sink_ms = 0U;
    st.wave_clear_ms = 0U;
    st.popup_ms = 0U;
    st.combo_streak = 0;
    st.game_score = 0;
    st.round_over = false;
    st.is_new_high = false;
    st.ring_wave_r = -1.0f;
    st.ring_wave_x = static_cast<float>(cx);
    st.ring_wave_y = static_cast<float>(cy);
    for (int p = 0; p < 4; p++) st.peg_hit_ms[p] = 0U;
    spawn_wave_balls(1);
  };

  if (game_just_entered) {
    reset_full_game();
  }

  float dt = (st.last_phys_ms == 0U || now_ms < st.last_phys_ms)
                 ? 0.05f
                 : static_cast<float>(now_ms - st.last_phys_ms) * 0.001f;
  st.last_phys_ms = now_ms;
  dt = std::clamp(dt, 0.012f, 0.075f);

  const bool in_wave_pause = (st.wave_clear_ms > 0U && (now_ms - st.wave_clear_ms < 3000U));
  if (st.wave_clear_ms > 0U && (now_ms - st.wave_clear_ms >= 3000U)) {
    st.wave_clear_ms = 0U;
    spawn_wave_balls(st.wave_num);
  }

  if (!st.round_over && !in_wave_pause) {
    st.time_left_f -= dt;
    if (st.time_left_f <= 0.0f) {
      st.time_left_f = 0.0f;
      st.round_over = true;
      if (st.game_score > high_score_marbles) {
        high_score_marbles = st.game_score;
        st.is_new_high = true;
      }
      if (st.is_new_high) schedule_persisted_prefs_save(now_ms);
    }
  }
  int time_remaining = static_cast<int>(std::ceil(st.time_left_f));

  const int num_pegs = std::min(4, std::max(0, st.wave_num - 1));
  const float peg_ring_r = game_f(54.0f);
  const float peg_r = game_f(6.0f);
  const int wall_y = layout().back_y - dp_px(60);
  static constexpr float PEG_BASE_ANG[4] = {-1.5707963f, 1.5707963f, 3.1415926f, 0.0f};
  float peg_x[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float peg_y[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const float orbit_offset = (st.wave_num >= 6)
      ? (static_cast<float>(now_ms % 60000U) * 0.001f * 0.30f * static_cast<float>(std::min(4, st.wave_num - 5)))
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

    if (st.round_over) {
      reset_full_game();
      game_imu_retare_flag() = true;
    } else {
      st.ring_wave_x = tx;
      st.ring_wave_y = ty;
      st.ring_wave_r = game_f(12.0f);
      const float ref_r = game_f(16.0f);
      const float near_d = game_f(22.0f);
      const float imp_near = game_f(620.0f);
      const float imp_base = game_f(480.0f);
      const float imp_peak = game_f(680.0f);
      const float imp_cap = game_f(820.0f);
      const float falloff_ref = std::max(1.0f, game_f(12.5f));
      for (int i = 0; i < 5; i++) {
        if (balls[i].sunk) continue;
        const float tdx = balls[i].x - tx;
        const float tdy = balls[i].y - ty;
        const float tdist = std::sqrt(tdx * tdx + tdy * tdy);
        const float mass_scale = ref_r / std::max(1.0f, balls[i].r);
        if (tdist < near_d) {
          const float rnd_angle = static_cast<float>(static_cast<uint32_t>(i * 72) + (now_ms % 360U)) * (PI_F / 180.0f);
          float s_rnd = 0.0f, c_rnd = 1.0f;
          fast_sincos(rnd_angle, s_rnd, c_rnd);
          balls[i].vx += c_rnd * imp_near * mass_scale;
          balls[i].vy += s_rnd * imp_near * mass_scale;
        } else {
          const float impulse = std::min(imp_cap, (imp_base + (imp_peak / (tdist / falloff_ref + 1.0f))) * mass_scale);
          balls[i].vx += (tdx / tdist) * impulse;
          balls[i].vy += (tdy / tdist) * impulse;
        }
      }
    }
  }

  const float friction = std::pow(0.975f, dt / 0.05f);
  const float bounce = 0.82f;
  const float arena_r = game_f(208.0f);
  const float hole_cap_r = game_f(24.0f);
  const float hole_bevel_r = game_f(42.0f);
  const float outer_lip_r = game_f(180.0f);
  const float unit_scale = std::max(0.25f, game_f(1.0f));

  const float grav_lim = game_f(650.0f);
  const float ax = std::clamp(roll_delta * game_f(34.0f), -grav_lim, grav_lim);
  const float ay = std::clamp(-pitch_delta * game_f(34.0f), -grav_lim, grav_lim);

  int sunk_count = 0;
  if (!st.round_over) {
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

        if (st.last_sink_ms > 0U && (now_ms - st.last_sink_ms < 3500U)) {
          st.combo_streak = std::min(4, st.combo_streak + 1);
        } else {
          st.combo_streak = 1;
        }
        st.last_sink_ms = now_ms;
        const int pts = 200 + (st.combo_streak - 1) * 150;
        st.game_score += pts;
        st.popup_pts = pts;
        st.popup_combo = st.combo_streak;
        st.popup_ms = now_ms;
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

      const float inertia_scale = 0.92f + ((game_f(20.0f) - balls[i].r) / unit_scale) * 0.02f;
      balls[i].vx = (balls[i].vx + ax * inertia_scale * dt) * friction;
      balls[i].vy = (balls[i].vy + ay * inertia_scale * dt) * friction;
      balls[i].x += balls[i].vx * dt;
      balls[i].y += balls[i].vy * dt;

      const float peg_kick = game_f(38.0f);
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
          balls[i].vx += pnx * peg_kick;
          balls[i].vy += pny * peg_kick;
          if (now_ms - st.peg_hit_ms[p] > 180U) {
            st.game_score += 15;
            if (pending_game_sfx() == 0) pending_game_sfx() = 7;
          }
          st.peg_hit_ms[p] = now_ms;
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

    if (sunk_count >= 5 && st.wave_clear_ms == 0U) {
      bool any_animating = false;
      for (int i = 0; i < 5; i++) {
        if (now_ms - balls[i].sink_start_ms < 240U) any_animating = true;
      }
      if (!any_animating) {
        st.game_score += 300 + st.wave_num * 100;
        st.time_left_f = std::min(60.0f, st.time_left_f + 6.0f);
        time_remaining = static_cast<int>(std::ceil(st.time_left_f));
        st.wave_num++;
        st.wave_clear_ms = now_ms;
        pending_game_sfx() = 5;
        if (st.game_score > high_score_marbles) {
          high_score_marbles = st.game_score;
          st.is_new_high = true;
          schedule_persisted_prefs_save(now_ms);
        }
      }
    }
  } else {
    for (int i = 0; i < 5; i++) {
      if (balls[i].sunk) sunk_count++;
    }
  }

  const int wall_hw = game_px(151);
  fast_line(it, cx - wall_hw, wall_y, cx + wall_hw, wall_y, scale_color(ctx.col_slate, 0.45f));
  fast_line(it, cx - wall_hw + 2, wall_y + 1, cx + wall_hw - 2, wall_y + 1, ctx.col_dark_gray);
  char hud_buf[56];
  std::snprintf(hud_buf, sizeof(hud_buf), "W%d  •  %d/5  •  %ds  •  %d",
                st.wave_num, sunk_count, time_remaining, st.game_score);
  render_game_hud_footer(it, cx, hud_buf, ctx.font_small, time_remaining < 10 ? ctx.col_orange : ctx.col_gold);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "Exit");

  fast_circle(it, cx, cy, game_px(34), ctx.col_dark_gray);
  fast_circle(it, cx, cy, game_px(30), ctx.col_slate);
  fast_circle(it, cx, cy, game_px(29), ctx.col_white);
  fast_circle(it, cx, cy, game_px(28), ctx.col_white);
  fast_filled_circle(it, cx, cy, game_px(25), ctx.col_bg);

  const float swirl = static_cast<float>((now_ms / 25U) % 360U) * (PI_F / 180.0f);
  for (int s = 0; s < 4; s++) {
    const float sa = swirl + static_cast<float>(s) * (PI_F * 0.5f);
    float s_sa = 0.0f, c_sa = 1.0f;
    fast_sincos(sa, s_sa, c_sa);
    fast_line(it, cx + static_cast<int>(c_sa * game_f(26.0f)),
              cy + static_cast<int>(s_sa * game_f(26.0f)),
              cx + static_cast<int>(c_sa * game_f(32.0f)),
              cy + static_cast<int>(s_sa * game_f(32.0f)), ctx.col_slate);
  }

  const int peg_r_px = std::max(3, static_cast<int>(std::round(peg_r)));
  const int peg_hi_r = std::max(1, peg_r_px / 3);
  const int peg_sh_x = std::max(1, game_px(1));
  const int peg_sh_y = std::max(1, game_px(2));
  for (int p = 0; p < num_pegs; p++) {
    const int px = static_cast<int>(std::round(peg_x[p]));
    const int py = static_cast<int>(std::round(peg_y[p]));
    const bool peg_flashing = (st.peg_hit_ms[p] > 0U && (now_ms - st.peg_hit_ms[p] < 200U));
    if (peg_flashing) {
      fast_circle(it, px, py, peg_r_px + std::max(2, game_px(4)), ctx.col_gold);
      fast_circle(it, px, py, peg_r_px + std::max(1, game_px(2)), ctx.col_cyan);
    }
    fast_filled_circle(it, px + peg_sh_x, py + peg_sh_y, peg_r_px, UiColor(10, 14, 20));
    fast_filled_circle(it, px, py, peg_r_px, peg_flashing ? ctx.col_gold : ctx.col_slate);
    fast_circle(it, px, py, peg_r_px, ctx.col_white);
    fast_filled_circle(it, px - std::max(1, peg_r_px / 4), py - std::max(1, peg_r_px / 4),
                       peg_hi_r, peg_flashing ? ctx.col_white : ctx.col_cyan);
  }

  if (st.ring_wave_r >= 0.0f) {
    const int r_int = static_cast<int>(st.ring_wave_r);
    fast_circle(it, static_cast<int>(st.ring_wave_x), static_cast<int>(st.ring_wave_y), r_int, ctx.col_gold);
    fast_circle(it, static_cast<int>(st.ring_wave_x), static_cast<int>(st.ring_wave_y),
                r_int + std::max(1, game_px(2)), ctx.col_cyan);
    st.ring_wave_r += game_f(26.0f);
    if (st.ring_wave_r > game_f(190.0f)) st.ring_wave_r = -1.0f;
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

  if (!st.round_over && st.popup_ms > 0U && (now_ms - st.popup_ms < 650U)) {
    const uint32_t pel = now_ms - st.popup_ms;
    const int py = cy - game_px(42) - static_cast<int>((pel * static_cast<uint32_t>(game_px(30))) / 650U);
    char pop_buf[24];
    if (st.popup_combo > 1) {
      std::snprintf(pop_buf, sizeof(pop_buf), "+%d %dx!", st.popup_pts, st.popup_combo);
    } else {
      std::snprintf(pop_buf, sizeof(pop_buf), "+%d", st.popup_pts);
    }
    fast_print(it, cx, py, ctx.font_small, st.popup_combo > 1 ? ctx.col_white : ctx.col_gold,
               UiTextAlign::CENTER, pop_buf);
  }

  if (!st.round_over && in_wave_pause) {
    char w_buf[36];
    if (now_ms - st.wave_clear_ms < 1100U) {
      std::snprintf(w_buf, sizeof(w_buf), "WAVE %d CLEAR! +6s", st.wave_num - 1);
      fast_print(it, cx, cy - game_px(107), ctx.font_date, ctx.col_gold, UiTextAlign::CENTER, w_buf);
    } else {
      std::snprintf(w_buf, sizeof(w_buf), "GET READY! WAVE %d", st.wave_num);
      fast_print(it, cx, cy - game_px(107), ctx.font_date, ctx.col_cyan, UiTextAlign::CENTER, w_buf);
    }
  }

  if (st.round_over) {
    char title_buf[32];
    UiColor title_col = ctx.col_gold;
    if (st.is_new_high) {
      std::snprintf(title_buf, sizeof(title_buf), "NEW HIGH SCORE!");
    } else if (st.wave_num == 1) {
      std::snprintf(title_buf, sizeof(title_buf), "GAME OVER");
      title_col = ctx.col_red;
    } else {
      std::snprintf(title_buf, sizeof(title_buf), "WAVE %d REACHED", st.wave_num);
      title_col = ctx.col_cyan;
    }
    char score_str[32], best_str[32];
    std::snprintf(score_str, sizeof(score_str), "Score: %d", st.game_score);
    std::snprintf(best_str, sizeof(best_str), "Best: %d", static_cast<int>(high_score_marbles));
    ui::game_over_sheet(it, cx, cy, title_buf, title_col, score_str, best_str, st.is_new_high,
                        "PLAY AGAIN", ctx.font_date, ctx.font_small,
                        ctx.theme_primary, ctx.col_white, ctx.col_gold, ctx.col_slate, WidgetId::GAME_OVER_CTA);
  }
}

#if LATTER_HAS_IMU
// ==============================================================================
// 4. IMU-Exclusive Games (Archery, Bricks, Chomp)
// ==============================================================================
void render_game_archery(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                         bool game_just_entered, float roll_delta, float pitch_delta,
                         float &marble_touch_x, bool &game_btn_pressed,
                         int32_t &high_score_archery) {
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto &st = arena.archery;
  auto *pinned = st.pinned;
  const float target_base_y = static_cast<float>(std::max(dp_px(72), (SCREEN_H * 24) / 100));

  auto clear_pinned = [&]() {
    st.pinned_count = 0;
    for (int i = 0; i < 10; i++) pinned[i].active = false;
  };

  auto reset_full_game = [&]() {
    st.last_phys_ms = now_ms;
    st.round_num = 1;
    st.arrows_left = 6;
    st.round_pts = 0;
    st.arch_score = 0;
    st.streak_mult = 1;
    st.target_phase = 0.0f;
    st.smooth_ax = static_cast<float>(cx);
    st.smooth_ay = target_base_y;
    st.arrow_in_flight = false;
    st.flight_z = 0.0f;
    st.hit_feedback[0] = '\0';
    st.feedback_r = ctx.col_gold.r;
    st.feedback_g = ctx.col_gold.g;
    st.feedback_b = ctx.col_gold.b;
    st.feedback_timer = 0U;
    st.round_banner_ms = 0U;
    st.round_over = false;
    st.is_new_high = false;
    clear_pinned();
  };

  if (game_just_entered) {
    reset_full_game();
  }

  float dt = (st.last_phys_ms == 0U || now_ms < st.last_phys_ms)
                 ? 0.05f
                 : static_cast<float>(now_ms - st.last_phys_ms) * 0.001f;
  st.last_phys_ms = now_ms;
  dt = std::clamp(dt, 0.012f, 0.075f);

  const int target_r = game_px(std::max(26, 36 - (st.round_num - 1) * 2));
  const float sway_spd = 0.78f + static_cast<float>(std::min(6, st.round_num - 1)) * 0.16f;
  st.target_phase += sway_spd * dt;

  const float bow_y = static_cast<float>(layout().back_y - dp_px(88));
  const float target_x = static_cast<float>(cx) + fast_sin(st.target_phase) * game_f(102.0f);
  const float target_y = target_base_y + fast_cos(st.target_phase * 1.4f) * game_f(24.0f);

  const float max_wind = std::min(8.0f, 2.5f + static_cast<float>(st.round_num - 1) * 1.3f);
  st.wind_speed = fast_sin(static_cast<float>(now_ms % 62832U) * 0.00065f) * max_wind;

  const float max_aim_dx = game_f(173.0f);
  const float min_aim_y = static_cast<float>(dp_px(48));
  const float max_aim_y = bow_y - static_cast<float>(dp_px(50));
  const float raw_ax = std::clamp(static_cast<float>(cx) + roll_delta * game_f(6.8f),
                                  static_cast<float>(cx) - max_aim_dx, static_cast<float>(cx) + max_aim_dx);
  const float raw_ay = std::clamp(target_base_y - pitch_delta * game_f(6.8f), min_aim_y, max_aim_y);
  st.smooth_ax += (raw_ax - st.smooth_ax) * 0.45f;
  st.smooth_ay += (raw_ay - st.smooth_ay) * 0.45f;

  const int round_goal = 20 + std::min(5, st.round_num - 1) * 4;

  if (game_btn_pressed || marble_touch_x >= 0.0f) {
    game_btn_pressed = false;
    marble_touch_x = -1.0f;

    if (st.round_over) {
      reset_full_game();
      game_imu_retare_flag() = true;
    } else if (!st.arrow_in_flight && st.arrows_left > 0 &&
               (st.round_banner_ms == 0U || now_ms - st.round_banner_ms >= 3000U)) {
      st.arrow_in_flight = true;
      st.flight_z = 0.0f;
      st.launch_aim_x = st.smooth_ax;
      st.launch_aim_y = st.smooth_ay;
      st.launch_wind = st.wind_speed;
      st.arrows_left--;
      pending_game_sfx() = 8;
    }
  }

  float cur_arrow_x = static_cast<float>(cx);
  float cur_arrow_y = bow_y;
  float arrow_dx = 0.0f;
  float arrow_dy = -1.0f;

  if (st.arrow_in_flight) {
    st.flight_z += dt * (1.0f / 0.40f);
    const float z = std::min(1.0f, st.flight_z);
    const float wind_k = game_f(5.2f);
    const float arc_k = game_f(26.0f);
    const float wind_drift = st.launch_wind * wind_k * z * z;
    const float arc_lift = -arc_k * z * (1.0f - z);

    cur_arrow_x = static_cast<float>(cx) + (st.launch_aim_x - static_cast<float>(cx)) * z + wind_drift;
    cur_arrow_y = bow_y + (st.launch_aim_y - bow_y) * z + arc_lift;

    const float vx = (st.launch_aim_x - static_cast<float>(cx)) + st.launch_wind * (2.0f * wind_k) * z;
    const float vy = (st.launch_aim_y - bow_y) - arc_k * (1.0f - 2.0f * z);
    const float vlen = std::sqrt(vx * vx + vy * vy);
    if (vlen > 0.001f) {
      arrow_dx = vx / vlen;
      arrow_dy = vy / vlen;
    }

    if (st.flight_z >= 1.0f) {
      st.arrow_in_flight = false;
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
      } else if (dist_hit <= r_out + game_f(1.5f)) {
        base_pts = 1;
      }

      UiColor fb_col = ctx.col_gold;
      if (base_pts > 0) {
        const int earned = base_pts * st.streak_mult;
        st.arch_score += earned;
        st.round_pts += earned;
        if (st.pinned_count < 10) {
          pinned[st.pinned_count++] = {hit_dx, hit_dy, true};
        }
        if (bullseye) {
          st.arrows_left = std::min(9, st.arrows_left + 1);
          std::snprintf(st.hit_feedback, sizeof(st.hit_feedback), "BULLSEYE! +%d (+1 ARR)", earned);
          fb_col = ctx.col_gold;
          pending_game_sfx() = 9;
        } else if (st.streak_mult > 1) {
          std::snprintf(st.hit_feedback, sizeof(st.hit_feedback), "HIT +%d (%dx!)", earned, st.streak_mult);
          fb_col = ctx.col_cyan;
          pending_game_sfx() = 10;
        } else {
          std::snprintf(st.hit_feedback, sizeof(st.hit_feedback), "HIT +%d", earned);
          fb_col = ctx.col_gold;
          pending_game_sfx() = 10;
        }
        st.streak_mult = std::min(3, st.streak_mult + 1);
      } else {
        st.streak_mult = 1;
        std::snprintf(st.hit_feedback, sizeof(st.hit_feedback), "MISS!");
        fb_col = ctx.col_red;
        pending_game_sfx() = 14;
      }
      st.feedback_r = fb_col.r;
      st.feedback_g = fb_col.g;
      st.feedback_b = fb_col.b;
      st.feedback_timer = now_ms;

      if (st.arch_score > high_score_archery) {
        high_score_archery = st.arch_score;
        st.is_new_high = true;
      }

      if (st.arrows_left <= 0) {
        if (st.round_pts >= round_goal) {
          st.arch_score += st.round_num * 25;
          if (st.arch_score > high_score_archery) {
            high_score_archery = st.arch_score;
            st.is_new_high = true;
          }
          if (st.is_new_high) schedule_persisted_prefs_save(now_ms);
          st.round_num++;
          st.round_pts = 0;
          st.round_banner_ms = now_ms;
          pending_game_sfx() = 5;
        } else {
          st.round_over = true;
          if (st.is_new_high) schedule_persisted_prefs_save(now_ms);
        }
      }
    }
  }

  const int tx_i = static_cast<int>(std::round(target_x));
  const int ty_i = static_cast<int>(std::round(target_y));
  render_archery_target(it, tx_i, ty_i, target_r,
                        ctx.col_white, ctx.col_black, ctx.col_cyan, ctx.col_red,
                        ctx.col_gold, ctx.col_slate, ctx.col_dark_gray);

  const int pin_dx = game_px(5), pin_dy = game_px(11);
  const int pin_f1x = game_px(8), pin_f1y = game_px(10);
  const int pin_f2x = game_px(3), pin_f2y = game_px(13);
  const int pin_dot_r = std::max(2, game_px(2));
  for (int i = 0; i < st.pinned_count; i++) {
    if (!pinned[i].active) continue;
    const int px = tx_i + static_cast<int>(std::round(pinned[i].dx));
    const int py = ty_i + static_cast<int>(std::round(pinned[i].dy));
    fast_filled_circle(it, px, py, pin_dot_r, ctx.col_black);
    fast_line(it, px, py, px - pin_dx, py + pin_dy, ctx.col_gold);
    fast_line(it, px - pin_dx, py + pin_dy, px - pin_f1x, py + pin_f1y, ctx.col_red);
    fast_line(it, px - pin_dx, py + pin_dy, px - pin_f2x, py + pin_f2y, ctx.col_red);
  }

  const int rx_i = static_cast<int>(std::round(st.smooth_ax));
  const int ry_i = static_cast<int>(std::round(st.smooth_ay));
  const int ret_r = std::max(6, game_px(13));
  const int ret_in = std::max(2, game_px(5));
  const int ret_out = std::max(8, game_px(17));
  fast_circle(it, rx_i, ry_i, ret_r, ctx.col_cyan);
  fast_line(it, rx_i - ret_out, ry_i, rx_i - ret_in, ry_i, ctx.col_cyan);
  fast_line(it, rx_i + ret_in, ry_i, rx_i + ret_out, ry_i, ctx.col_cyan);
  fast_line(it, rx_i, ry_i - ret_out, rx_i, ry_i - ret_in, ctx.col_cyan);
  fast_line(it, rx_i, ry_i + ret_in, rx_i, ry_i + ret_out, ctx.col_cyan);
  fast_pixel(it, rx_i, ry_i, ctx.col_gold);

  const float aim_dx = st.smooth_ax - static_cast<float>(cx);
  const float aim_dy = st.smooth_ay - bow_y;
  const float aim_len = std::sqrt(aim_dx * aim_dx + aim_dy * aim_dy);
  const float ux = (aim_len > 1.0f) ? (aim_dx / aim_len) : 0.0f;
  const float uy = (aim_len > 1.0f) ? (aim_dy / aim_len) : -1.0f;
  const float px_v = -uy;
  const float py_v = ux;

  const int bow_cx = cx;
  const int bow_cy = static_cast<int>(bow_y);
  const float limb_w = game_f(38.0f);
  const float limb_pull = game_f(10.0f);
  const float mid_w = game_f(20.0f);
  const float mid_fwd = game_f(4.0f);
  const int left_tip_x = bow_cx - static_cast<int>(px_v * limb_w + ux * limb_pull);
  const int left_tip_y = bow_cy - static_cast<int>(py_v * limb_w + uy * limb_pull);
  const int right_tip_x = bow_cx + static_cast<int>(px_v * limb_w - ux * limb_pull);
  const int right_tip_y = bow_cy + static_cast<int>(py_v * limb_w - uy * limb_pull);
  const int left_mid_x = bow_cx - static_cast<int>(px_v * mid_w - ux * mid_fwd);
  const int left_mid_y = bow_cy - static_cast<int>(py_v * mid_w - uy * mid_fwd);
  const int right_mid_x = bow_cx + static_cast<int>(px_v * mid_w + ux * mid_fwd);
  const int right_mid_y = bow_cy + static_cast<int>(py_v * mid_w + uy * mid_fwd);

  fast_line(it, bow_cx, bow_cy, left_mid_x, left_mid_y, ctx.col_gold);
  fast_line(it, left_mid_x, left_mid_y, left_tip_x, left_tip_y, ctx.col_gold);
  fast_line(it, bow_cx, bow_cy, right_mid_x, right_mid_y, ctx.col_gold);
  fast_line(it, right_mid_x, right_mid_y, right_tip_x, right_tip_y, ctx.col_gold);
  fast_line(it, bow_cx, bow_cy + 1, left_mid_x, left_mid_y + 1, ctx.col_slate);
  fast_line(it, bow_cx, bow_cy + 1, right_mid_x, right_mid_y + 1, ctx.col_slate);
  fast_filled_circle(it, bow_cx, bow_cy, std::max(2, game_px(3)), ctx.col_red);

  if (!st.arrow_in_flight && st.arrows_left > 0 && !st.round_over) {
    const float nock_len = game_f(14.0f);
    const float tip_len = game_f(18.0f);
    const int nock_x = bow_cx - static_cast<int>(ux * nock_len);
    const int nock_y = bow_cy - static_cast<int>(uy * nock_len);
    const int tip_x = bow_cx + static_cast<int>(ux * tip_len);
    const int tip_y = bow_cy + static_cast<int>(uy * tip_len);
    fast_line(it, left_tip_x, left_tip_y, nock_x, nock_y, ctx.col_slate);
    fast_line(it, right_tip_x, right_tip_y, nock_x, nock_y, ctx.col_slate);
    fast_line(it, nock_x, nock_y, tip_x, tip_y, ctx.col_white);
    fast_filled_circle(it, tip_x, tip_y, std::max(2, game_px(2)), ctx.col_gold);
  } else {
    fast_line(it, left_tip_x, left_tip_y, right_tip_x, right_tip_y, ctx.col_slate);
  }

  if (st.arrow_in_flight) {
    const float z = std::min(1.0f, st.flight_z);
    const float a_len = game_f(32.0f - z * 16.0f);
    const int head_x = static_cast<int>(std::round(cur_arrow_x));
    const int head_y = static_cast<int>(std::round(cur_arrow_y));
    const int tail_x = static_cast<int>(std::round(cur_arrow_x - arrow_dx * a_len));
    const int tail_y = static_cast<int>(std::round(cur_arrow_y - arrow_dy * a_len));

    fast_line(it, tail_x, tail_y, head_x, head_y, ctx.col_gold);
    fast_line(it, tail_x + 1, tail_y, head_x + 1, head_y, ctx.col_white);
    fast_filled_circle(it, head_x, head_y, std::max(2, game_px(z > 0.65f ? 2 : 3)), ctx.col_white);
    const float f_sz = game_f(4.0f);
    const int f_px = static_cast<int>(-arrow_dy * f_sz);
    const int f_py = static_cast<int>(arrow_dx * f_sz);
    const int f_bx = static_cast<int>(arrow_dx * f_sz);
    const int f_by = static_cast<int>(arrow_dy * f_sz);
    fast_line(it, tail_x, tail_y, tail_x + f_px - f_bx, tail_y + f_py - f_by, ctx.col_red);
    fast_line(it, tail_x, tail_y, tail_x - f_px - f_bx, tail_y - f_py - f_by, ctx.col_red);
  }

  if (st.round_banner_ms > 0U && (now_ms - st.round_banner_ms >= 1100U) && st.pinned_count > 0) {
    clear_pinned();
  }
  if (st.round_banner_ms > 0U && (now_ms - st.round_banner_ms >= 3000U)) {
    st.round_banner_ms = 0U;
    st.arrows_left = 6;
  }

  if (!st.round_over && st.round_banner_ms > 0U && (now_ms - st.round_banner_ms < 3000U)) {
    char rb[36];
    if (now_ms - st.round_banner_ms < 1100U) {
      std::snprintf(rb, sizeof(rb), "ROUND %d CLEAR!", st.round_num - 1);
      fast_print(it, cx, cy - dp_px(8), ctx.font_date, ctx.col_gold, UiTextAlign::CENTER, rb);
    } else {
      std::snprintf(rb, sizeof(rb), "ROUND %d! GOAL %d", st.round_num, round_goal);
      fast_print(it, cx, cy - dp_px(8), ctx.font_date, ctx.col_cyan, UiTextAlign::CENTER, rb);
    }
  } else if (!st.round_over && st.hit_feedback[0] != '\0' && (now_ms - st.feedback_timer < 1100U)) {
    fast_print(it, cx, cy - dp_px(8), ctx.font_date,
               UiColor(st.feedback_r, st.feedback_g, st.feedback_b), UiTextAlign::CENTER, st.hit_feedback);
  }

  char wind_buf[20];
  if (std::fabs(st.wind_speed) < 1.0f) {
    std::snprintf(wind_buf, sizeof(wind_buf), "CALM");
  } else if (st.wind_speed > 0.0f) {
    std::snprintf(wind_buf, sizeof(wind_buf), "%.0f mph >", st.wind_speed);
  } else {
    std::snprintf(wind_buf, sizeof(wind_buf), "< %.0f mph", -st.wind_speed);
  }
  char hud_buf[56];
  std::snprintf(hud_buf, sizeof(hud_buf), "R%d  •  %d Arr  •  %s  •  %d",
                st.round_num, st.arrows_left, wind_buf, st.arch_score);
  render_game_hud_footer(it, cx, hud_buf, ctx.font_small, ctx.col_gold);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "Exit");

  if (st.round_over) {
    char title_buf[32];
    UiColor title_col = ctx.col_gold;
    if (st.is_new_high) {
      std::snprintf(title_buf, sizeof(title_buf), "NEW HIGH SCORE!");
    } else {
      std::snprintf(title_buf, sizeof(title_buf), "ROUND %d REACHED", st.round_num);
      title_col = ctx.col_cyan;
    }
    char score_str[32], best_str[32];
    std::snprintf(score_str, sizeof(score_str), "Score: %d", st.arch_score);
    std::snprintf(best_str, sizeof(best_str), "Best: %d", static_cast<int>(high_score_archery));
    ui::game_over_sheet(it, cx, cy, title_buf, title_col, score_str, best_str, st.is_new_high,
                        "PLAY AGAIN", ctx.font_date, ctx.font_small,
                        ctx.theme_primary, ctx.col_white, ctx.col_gold, ctx.col_slate, WidgetId::GAME_OVER_CTA);
  }
}

void render_game_bricks(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                        bool game_just_entered, float roll_delta,
                        float &marble_touch_x, bool &game_btn_pressed, int32_t &high_score_bricks) {
  static constexpr int NUM_BRICKS = 18;
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto &st = arena.bricks;
  auto *bricks = st.bricks;
  auto *debris = st.debris;

  auto init_brick_wave = [&](int wv) {
    static constexpr int ROW_COUNTS[4] = {4, 5, 5, 4};
    static constexpr int ROW_YS[4] = {82, 104, 126, 148};
    const int brk_pitch = game_px(46);
    const int brk_w = game_px(40);
    const int brk_h = game_px(14);
    int idx = 0;
    for (int r = 0; r < 4; r++) {
      const int cnt = ROW_COUNTS[r];
      const int start_x = cx - ((cnt - 1) * brk_pitch) / 2;
      const int row_y = cy - game_px(233 - ROW_YS[r]);
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

  const int paddle_y = std::min(layout().back_y - dp_px(76), cy + game_px(117));
  const int paddle_w = game_px(58);
  const int paddle_h = game_px(10);
  const float ball_r = game_f(6.0f);
  const float paddle_rest_off = static_cast<float>(paddle_h) * 0.5f + ball_r;

  auto reset_ball_on_paddle = [&]() {
    st.ball_launched = false;
    st.ball_x = st.smooth_paddle_x;
    st.ball_y = static_cast<float>(paddle_y) - paddle_rest_off;
    st.ball_vx = 0.0f;
    st.ball_vy = 0.0f;
    st.wall_bounces = 0;
    for (int t = 0; t < 4; t++) {
      st.trail_x[t] = st.ball_x;
      st.trail_y[t] = st.ball_y;
    }
  };

  auto reset_bricks_game = [&]() {
    st.last_phys_ms = now_ms;
    st.wave_num = 1;
    st.lives_left = 3;
    st.bricks_score = 0;
    st.rally_combo = 0;
    st.wall_bounces = 0;
    st.hit_brick_since_paddle = false;
    st.round_over = false;
    st.is_new_high = false;
    st.smooth_paddle_x = static_cast<float>(cx);
    st.debris_ms = 0U;
    st.popup_ms = 0U;
    st.banner_ms = 0U;
    st.popup_x = cx;
    st.popup_y = (SCREEN_H * 30) / 100;
    st.popup_text[0] = '\0';
    st.banner_text[0] = '\0';
    init_brick_wave(1);
    reset_ball_on_paddle();
  };

  if (game_just_entered) {
    reset_bricks_game();
  }

  float dt = (now_ms >= st.last_phys_ms) ? (static_cast<float>(now_ms - st.last_phys_ms) * 0.001f) : 0.05f;
  st.last_phys_ms = now_ms;
  dt = std::clamp(dt, 0.012f, 0.075f);

  const int max_pad_dx = game_px(136);
  const float raw_target_x = std::clamp(static_cast<float>(cx) + roll_delta * game_f(8.4f),
                                        static_cast<float>(cx - max_pad_dx),
                                        static_cast<float>(cx + max_pad_dx));
  st.smooth_paddle_x += (raw_target_x - st.smooth_paddle_x) * 0.34f;
  const float paddle_x = st.smooth_paddle_x;
  const float target_speed = game_f(std::min(285.0f, 215.0f + static_cast<float>(st.wave_num - 1) * 14.0f));

  if (marble_touch_x >= 0.0f || game_btn_pressed) {
    marble_touch_x = -1.0f;
    game_btn_pressed = false;
    if (st.round_over) {
      reset_bricks_game();
      game_imu_retare_flag() = true;
    } else if (!st.ball_launched && (st.banner_ms == 0U || now_ms - st.banner_ms >= 3000U)) {
      float launch_ang = std::clamp(roll_delta * 0.025f, -0.52f, 0.52f);
      if (std::fabs(launch_ang) < 0.14f) launch_ang = (now_ms & 1U) ? 0.22f : -0.22f;
      st.ball_vx = target_speed * fast_sin(launch_ang);
      st.ball_vy = -target_speed * fast_cos(launch_ang);
      st.ball_launched = true;
      st.hit_brick_since_paddle = true;
    }
  }

  const UiColor row_cols[4] = {ctx.col_gold, ctx.col_cyan, ctx.col_orange, ctx.col_green};
  const UiColor row_his[4] = {
      UiColor(255, 245, 165), UiColor(185, 255, 255),
      UiColor(255, 195, 100), UiColor(185, 250, 205)};

  if (!st.round_over) {
    if (!st.ball_launched) {
      st.ball_x = paddle_x;
      st.ball_y = static_cast<float>(paddle_y) - paddle_rest_off;
      for (int t = 0; t < 4; t++) {
        st.trail_x[t] = st.ball_x;
        st.trail_y[t] = st.ball_y;
      }
    } else {
      for (int t = 3; t > 0; t--) {
        st.trail_x[t] = st.trail_x[t - 1];
        st.trail_y[t] = st.trail_y[t - 1];
      }
      st.trail_x[0] = st.ball_x;
      st.trail_y[0] = st.ball_y;
      st.ball_x += st.ball_vx * dt;
      st.ball_y += st.ball_vy * dt;

      const float dome_ball_r = game_f(202.0f);
      const float b_dx = st.ball_x - static_cast<float>(cx);
      const float b_dy = st.ball_y - static_cast<float>(cy);
      const float b_dist = std::sqrt(b_dx * b_dx + b_dy * b_dy);
      if (b_dist > dome_ball_r && st.ball_y < static_cast<float>(paddle_y) - game_f(4.0f)) {
        const float bnx = b_dx / b_dist;
        const float bny = b_dy / b_dist;
        st.ball_x = static_cast<float>(cx) + bnx * (dome_ball_r - 1.0f);
        st.ball_y = static_cast<float>(cy) + bny * (dome_ball_r - 1.0f);
        float rnx = bnx;
        float rny = (st.ball_y > static_cast<float>(cy) + game_f(12.0f)) ? 0.0f : bny;
        const float r_len = std::sqrt(rnx * rnx + rny * rny);
        if (r_len > 0.001f) {
          rnx /= r_len;
          rny /= r_len;
        }
        const float v_dot = st.ball_vx * rnx + st.ball_vy * rny;
        if (v_dot > 0.0f) {
          st.ball_vx -= 2.0f * v_dot * rnx;
          st.ball_vy -= 2.0f * v_dot * rny;
          st.wall_bounces++;
          if (st.wall_bounces >= 2) {
            st.ball_vy += (st.ball_vy >= 0.0f) ? game_f(55.0f) : game_f(70.0f);
          }
          const float min_vy = game_f(95.0f);
          const float clamp_vy = game_f(98.0f);
          if (std::fabs(st.ball_vy) < min_vy) st.ball_vy = (st.ball_vy >= 0.0f) ? clamp_vy : -clamp_vy;
          const float spd = std::sqrt(st.ball_vx * st.ball_vx + st.ball_vy * st.ball_vy);
          if (spd > 1.0f) {
            st.ball_vx = (st.ball_vx / spd) * target_speed;
            st.ball_vy = (st.ball_vy / spd) * target_speed;
          }
        }
      }

      if (st.ball_vy > 0.0f &&
          st.ball_y >= static_cast<float>(paddle_y) - game_f(10.0f) &&
          st.ball_y <= static_cast<float>(paddle_y) + game_f(8.0f)) {
        const float half_reach = static_cast<float>(paddle_w) * 0.5f + ball_r;
        if (std::fabs(st.ball_x - paddle_x) <= half_reach) {
          if (!st.hit_brick_since_paddle) st.rally_combo = 0;
          st.hit_brick_since_paddle = false;
          st.wall_bounces = 0;
          const float offset = std::clamp((st.ball_x - paddle_x) / (static_cast<float>(paddle_w) * 0.5f), -1.0f, 1.0f);
          const float bounce_ang = offset * 1.047f;
          st.ball_vx = target_speed * fast_sin(bounce_ang);
          st.ball_vy = -target_speed * fast_cos(bounce_ang);
          st.ball_y = static_cast<float>(paddle_y) - paddle_rest_off;
          pending_game_sfx() = 11;
        }
      }

      for (int b = 0; b < NUM_BRICKS; b++) {
        if (bricks[b].hp == 0) continue;
        const float dx = st.ball_x - static_cast<float>(bricks[b].x);
        const float dy = st.ball_y - static_cast<float>(bricks[b].y);
        const float lim_x = static_cast<float>(bricks[b].w) * 0.5f + ball_r;
        const float lim_y = static_cast<float>(bricks[b].h) * 0.5f + ball_r;
        if (std::fabs(dx) < lim_x && std::fabs(dy) < lim_y) {
          const float overlap_x = lim_x - std::fabs(dx);
          const float overlap_y = lim_y - std::fabs(dy);
          if (overlap_x < overlap_y) {
            st.ball_vx = -st.ball_vx;
            st.ball_x += (dx >= 0.0f) ? overlap_x : -overlap_x;
          } else {
            st.ball_vy = -st.ball_vy;
            st.ball_y += (dy >= 0.0f) ? overlap_y : -overlap_y;
          }
          bricks[b].hp--;
          pending_game_sfx() = (bricks[b].hp == 0) ? 12 : 13;
          st.rally_combo++;
          st.wall_bounces = 0;
          st.hit_brick_since_paddle = true;
          const int mult = std::min(4, st.rally_combo);
          static constexpr int BASE_ROW_PTS[4] = {150, 120, 90, 60};
          const int base_pts = (bricks[b].hp == 0) ? BASE_ROW_PTS[bricks[b].row] : 50;
          const int pts = base_pts * mult;
          st.bricks_score += pts;
          if (st.bricks_score > high_score_bricks) {
            high_score_bricks = st.bricks_score;
            st.is_new_high = true;
          }
          st.popup_x = bricks[b].x;
          st.popup_y = bricks[b].y;
          st.popup_ms = now_ms;
          UiColor pcol = ctx.col_white;
          if (mult > 1) {
            std::snprintf(st.popup_text, sizeof(st.popup_text), "+%d %dx!", pts, mult);
            pcol = ctx.col_gold;
          } else {
            std::snprintf(st.popup_text, sizeof(st.popup_text), "+%d", pts);
          }
          st.popup_r = pcol.r;
          st.popup_g = pcol.g;
          st.popup_b = pcol.b;
          const UiColor frag_c = (bricks[b].hp > 0) ? UiColor(200, 210, 225) : row_cols[bricks[b].row];
          const float f_spd = game_f((bricks[b].hp == 0) ? 95.0f : 55.0f);
          const float off_x = game_f(6.0f);
          const float off_y = game_f(4.0f);
          const float up_kick = game_f(25.0f);
          for (int s = 0; s < 6; s++) {
            const float ang = static_cast<float>(s) * 1.047f + 0.35f;
            debris[s].x = static_cast<float>(bricks[b].x) + fast_cos(ang) * off_x;
            debris[s].y = static_cast<float>(bricks[b].y) + fast_sin(ang) * off_y;
            debris[s].vx = fast_cos(ang) * f_spd;
            debris[s].vy = fast_sin(ang) * f_spd - up_kick;
            const UiColor sc = (s & 1) ? row_his[bricks[b].row] : frag_c;
            debris[s].r = sc.r;
            debris[s].g = sc.g;
            debris[s].b = sc.b;
          }
          st.debris_ms = now_ms;
          break;
        }
      }

      int rem_check = 0;
      for (int b = 0; b < NUM_BRICKS; b++) {
        if (bricks[b].hp > 0) rem_check++;
      }
      if (rem_check == 0 && st.banner_ms == 0U) {
        const int wave_bonus = st.wave_num * 300;
        st.bricks_score += wave_bonus;
        if (st.bricks_score > high_score_bricks) {
          high_score_bricks = st.bricks_score;
          st.is_new_high = true;
        }
        if (st.is_new_high) schedule_persisted_prefs_save(now_ms);
        st.wave_num++;
        if (st.lives_left < 3) st.lives_left++;
        std::snprintf(st.banner_text, sizeof(st.banner_text), "WAVE %d CLEAR! +%d", st.wave_num - 1, wave_bonus);
        st.banner_ms = now_ms;
        pending_game_sfx() = 5;
        reset_ball_on_paddle();
      } else if (st.ball_y > static_cast<float>(paddle_y + game_px(26))) {
        st.lives_left--;
        pending_game_sfx() = 14;
        st.rally_combo = 0;
        st.hit_brick_since_paddle = false;
        reset_ball_on_paddle();
        if (st.lives_left <= 0) {
          st.round_over = true;
          if (st.bricks_score > high_score_bricks) {
            high_score_bricks = st.bricks_score;
            st.is_new_high = true;
          }
          if (st.is_new_high) schedule_persisted_prefs_save(now_ms);
        }
      }
    }
  }

  if (st.debris_ms > 0U && (now_ms - st.debris_ms < 360U)) {
    const float deb_g = game_f(180.0f) * dt;
    for (int s = 0; s < 6; s++) {
      debris[s].x += debris[s].vx * dt;
      debris[s].y += debris[s].vy * dt;
      debris[s].vy += deb_g;
    }
  }

  const int dome_r = game_px(208);
  fast_circle(it, cx, cy, dome_r, ctx.col_dark_gray);
  fast_circle(it, cx, cy, dome_r - 1, scale_color(ctx.col_slate, 0.35f));

  int bricks_remaining = 0;
  for (int b = 0; b < NUM_BRICKS; b++) {
    if (bricks[b].hp > 0) bricks_remaining++;
  }
  char bo_hud[56];
  if (st.rally_combo >= 2) {
    std::snprintf(bo_hud, sizeof(bo_hud), "W%d  •  %d/3  •  %dx  •  %d",
                  st.wave_num, st.lives_left, std::min(4, st.rally_combo), st.bricks_score);
  } else {
    std::snprintf(bo_hud, sizeof(bo_hud), "W%d  •  Balls:%d  •  Brk:%d  •  %d",
                  st.wave_num, st.lives_left, bricks_remaining, st.bricks_score);
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

  if (st.debris_ms > 0U && (now_ms - st.debris_ms < 360U)) {
    const int d_sz = std::max(2, game_px(4));
    const int d_half = d_sz / 2;
    for (int s = 0; s < 6; s++) {
      fill_rect_row_major(it, static_cast<int>(debris[s].x) - d_half, static_cast<int>(debris[s].y) - d_half,
                          d_sz, d_sz, UiColor(debris[s].r, debris[s].g, debris[s].b));
    }
  }
  if (st.popup_ms > 0U && (now_ms - st.popup_ms < 650U)) {
    const int py_off = static_cast<int>((now_ms - st.popup_ms) * static_cast<uint32_t>(game_px(22)) / 650U);
    fast_print(it, st.popup_x, st.popup_y - py_off, ctx.font_small,
               UiColor(st.popup_r, st.popup_g, st.popup_b), UiTextAlign::CENTER, st.popup_text);
  }
  if (st.banner_ms > 0U) {
    const uint32_t b_el = now_ms - st.banner_ms;
    if (b_el >= 1100U && bricks_remaining == 0) init_brick_wave(st.wave_num);
    if (b_el >= 3000U) {
      st.banner_ms = 0U;
    } else if (b_el < 1100U) {
      fast_print(it, cx, cy - dp_px(21), ctx.font_date, ctx.col_gold, UiTextAlign::CENTER, st.banner_text);
    } else {
      char rdy_buf[32];
      std::snprintf(rdy_buf, sizeof(rdy_buf), "GET READY! WAVE %d", st.wave_num);
      fast_print(it, cx, cy - dp_px(21), ctx.font_date, ctx.col_cyan, UiTextAlign::CENTER, rdy_buf);
    }
  }

  render_brick_paddle(it, static_cast<int>(paddle_x), paddle_y, paddle_w, paddle_h,
                      ctx.theme_primary, ctx.col_white);

  const int ball_r_px = std::max(3, static_cast<int>(std::round(ball_r)));
  if (st.ball_launched) {
    fast_filled_circle(it, static_cast<int>(st.trail_x[3]), static_cast<int>(st.trail_y[3]),
                       std::max(1, game_px(2)), UiColor(65, 52, 18));
    fast_filled_circle(it, static_cast<int>(st.trail_x[2]), static_cast<int>(st.trail_y[2]),
                       std::max(2, game_px(3)), UiColor(120, 95, 25));
    fast_filled_circle(it, static_cast<int>(st.trail_x[0]), static_cast<int>(st.trail_y[0]),
                       std::max(2, game_px(4)), UiColor(185, 145, 35));
  }
  fast_filled_circle(it, static_cast<int>(st.ball_x), static_cast<int>(st.ball_y), ball_r_px, ctx.col_gold);
  fast_circle(it, static_cast<int>(st.ball_x), static_cast<int>(st.ball_y), ball_r_px, ctx.col_white);
  const int spec_off = std::max(1, ball_r_px / 3);
  fast_filled_circle(it, static_cast<int>(st.ball_x) - spec_off, static_cast<int>(st.ball_y) - spec_off,
                     std::max(1, game_px(2)), ctx.col_white);

  if (st.round_over) {
    char title_buf[32];
    UiColor title_col = ctx.col_gold;
    if (st.is_new_high) {
      std::snprintf(title_buf, sizeof(title_buf), "NEW HIGH SCORE!");
    } else if (st.wave_num == 1) {
      std::snprintf(title_buf, sizeof(title_buf), "GAME OVER");
      title_col = ctx.col_red;
    } else {
      std::snprintf(title_buf, sizeof(title_buf), "WAVE %d REACHED", st.wave_num);
      title_col = ctx.col_cyan;
    }
    char score_str[32], best_str[32];
    std::snprintf(score_str, sizeof(score_str), "Score: %d", st.bricks_score);
    std::snprintf(best_str, sizeof(best_str), "Best: %d", static_cast<int>(high_score_bricks));
    ui::game_over_sheet(it, cx, cy, title_buf, title_col, score_str, best_str, st.is_new_high,
                        "PLAY AGAIN", ctx.font_date, ctx.font_small,
                        ctx.theme_primary, ctx.col_white, ctx.col_gold, ctx.col_slate, WidgetId::GAME_OVER_CTA);
  }
}

void render_game_chomp(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                       bool game_just_entered, float roll_delta, float pitch_delta,
                       float &marble_touch_x, bool &game_btn_pressed, int32_t &high_score_chomp) {
  static constexpr int NUM_SNACKS = 7;
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto &st = arena.chomp;
  auto *snacks = st.snacks;
  auto *confetti = st.confetti;
  const int bot_wall_y = layout().back_y - dp_px(60);

  auto spawn_level_platter = [&](int lv, uint32_t seed_base) {
    st.snacks_left = NUM_SNACKS;
    const float base_ang = static_cast<float>(lv) * 0.45f;
    for (int i = 0; i < NUM_SNACKS; i++) {
      const int s = static_cast<int>(seed_base + static_cast<uint32_t>(i) * 73U + static_cast<uint32_t>(lv) * 37U);
      const float ang = base_ang + static_cast<float>(i) * (2.0f * PI_F / static_cast<float>(NUM_SNACKS)) +
                        (hash2d_01(s, 11) - 0.5f) * 0.35f;
      const float rad = game_f(76.0f + (static_cast<float>((i * 3) % 4) * 26.0f) + hash2d_01(s, 29) * 18.0f);
      float tx = static_cast<float>(cx) + fast_cos(ang) * rad;
      float ty = std::min(static_cast<float>(bot_wall_y - game_px(18)),
                          static_cast<float>(cy) + fast_sin(ang) * rad);
      const float dmx = tx - st.mon_x, dmy = ty - st.mon_y, min_sep = game_f(70.0f);
      if (dmx * dmx + dmy * dmy < min_sep * min_sep) {
        tx = static_cast<float>(cx) - dmx * 1.1f;
        ty = static_cast<float>(cy) - dmy * 1.1f;
      }
      const float drift_spd = (lv <= 1) ? 0.0f : game_f(std::min(68.0f, 22.0f + static_cast<float>(lv - 2) * 12.0f));
      const float v_ang = hash2d_01(s, 47) * (2.0f * PI_F);
      snacks[i] = {tx, ty, fast_cos(v_ang) * drift_spd, fast_sin(v_ang) * drift_spd, st.next_kind++, false};
    }
  };

  if (game_just_entered) {
    st.last_phys_ms = now_ms;
    st.entry_ms = now_ms;
    st.post_play_tared = false;
    st.mon_x = static_cast<float>(cx);
    st.mon_y = static_cast<float>(cy);
    st.mon_vx = 0.0f;
    st.mon_vy = 0.0f;
    st.level_num = 1;
    st.snack_score = 0;
    st.next_kind = 0;
    st.chomp_word_idx = 0;
    st.chomp_timer = 0U;
    st.level_clear_until_ms = 0U;
    pending_game_sfx() = 0;
    spawn_level_platter(1, now_ms);
  } else if (!st.post_play_tared && (now_ms - st.entry_ms >= 220U)) {
    st.post_play_tared = true;
    game_imu_retare_flag() = true;
  }

  float dt = (now_ms >= st.last_phys_ms) ? (static_cast<float>(now_ms - st.last_phys_ms) * 0.001f) : 0.05f;
  st.last_phys_ms = now_ms;
  dt = std::clamp(dt, 0.012f, 0.075f);

  bool in_celebration = (st.level_clear_until_ms > 0U && now_ms < st.level_clear_until_ms);
  if (in_celebration) {
    st.mon_vx = 0.0f;
    st.mon_vy = 0.0f;
    st.mon_x += (static_cast<float>(cx) - st.mon_x) * 0.25f;
    st.mon_y += (static_cast<float>(cy) - st.mon_y) * 0.25f;
  } else if (st.level_clear_until_ms > 0U && now_ms >= st.level_clear_until_ms) {
    st.level_clear_until_ms = 0U;
    st.mon_x = static_cast<float>(cx);
    st.mon_y = static_cast<float>(cy);
    st.mon_vx = 0.0f;
    st.mon_vy = 0.0f;
    st.level_num++;
    spawn_level_platter(st.level_num, now_ms);
    in_celebration = false;
  }

  if (!in_celebration && st.level_num >= 2) {
    const float evade_r = game_f(82.0f);
    const float evade_r2 = evade_r * evade_r;
    const float evade_cap = game_f(std::min(110.0f, static_cast<float>(st.level_num - 2) * 32.0f));
    const float max_t_spd = game_f(std::min(78.0f, 24.0f + static_cast<float>(st.level_num - 2) * 14.0f));
    const float snack_rim_r = game_f(180.0f);
    const float max_snack_y = static_cast<float>(bot_wall_y - game_px(16));
    for (int i = 0; i < NUM_SNACKS; i++) {
      if (snacks[i].eaten) continue;
      if (st.level_num >= 3) {
        const float edx = snacks[i].x - st.mon_x, edy = snacks[i].y - st.mon_y;
        const float ed2 = edx * edx + edy * edy;
        if (ed2 < evade_r2 && ed2 > 1.0f) {
          const float ed = std::sqrt(ed2);
          const float evade = (1.0f - ed / evade_r) * evade_cap;
          snacks[i].vx += (edx / ed) * evade * dt;
          snacks[i].vy += (edy / ed) * evade * dt;
        }
      }
      const float cur_spd = std::sqrt(snacks[i].vx * snacks[i].vx + snacks[i].vy * snacks[i].vy);
      if (cur_spd > max_t_spd && cur_spd > 0.1f) {
        snacks[i].vx = (snacks[i].vx / cur_spd) * max_t_spd;
        snacks[i].vy = (snacks[i].vy / cur_spd) * max_t_spd;
      }
      snacks[i].x += snacks[i].vx * dt;
      snacks[i].y += snacks[i].vy * dt;
      if (snacks[i].y > max_snack_y) {
        snacks[i].y = max_snack_y;
        if (snacks[i].vy > 0.0f) snacks[i].vy = -snacks[i].vy * 0.9f;
      }
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

  float nearest_dist = game_f(999.0f), look_dx = 0.0f, look_dy = 0.0f;
  for (int i = 0; i < NUM_SNACKS; i++) {
    if (snacks[i].eaten) continue;
    const float dx = snacks[i].x - st.mon_x, dy = snacks[i].y - st.mon_y;
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
    if (!in_celebration && nearest_dist < game_f(900.0f)) {
      const float lunge = game_f(165.0f);
      st.mon_vx += look_dx * lunge;
      st.mon_vy += look_dy * lunge;
    }
  }

  const float grav_lim = game_f(620.0f);
  const float ax = std::clamp(roll_delta * game_f(32.0f), -grav_lim, grav_lim);
  const float ay = std::clamp(-pitch_delta * game_f(32.0f), -grav_lim, grav_lim);
  const float pull_r = game_f(38.0f);
  float pull_x = 0.0f, pull_y = 0.0f;
  if (!in_celebration && nearest_dist < pull_r) {
    const float pull_mag = (1.0f - nearest_dist / pull_r) * game_f(110.0f);
    pull_x = look_dx * pull_mag;
    pull_y = look_dy * pull_mag;
  }

  const float damping = std::pow(0.925f, dt / 0.05f);
  st.mon_vx = (st.mon_vx + (ax + pull_x) * dt) * damping;
  st.mon_vy = (st.mon_vy + (ay + pull_y) * dt) * damping;
  st.mon_x += st.mon_vx * dt;
  st.mon_y += st.mon_vy * dt;

  const int mon_r = game_px(34);
  const float max_mon_y = static_cast<float>(bot_wall_y - mon_r);
  if (st.mon_y > max_mon_y) {
    st.mon_y = max_mon_y;
    if (st.mon_vy > 0.0f) st.mon_vy = -st.mon_vy * 0.6f;
  }
  const float mdx = st.mon_x - static_cast<float>(cx), mdy = st.mon_y - static_cast<float>(cy);
  const float mdist = std::sqrt(mdx * mdx + mdy * mdy);
  const float max_mon_r = game_f(172.0f);
  if (mdist > max_mon_r && mdist > 0.001f) {
    const float nx = mdx / mdist, ny = mdy / mdist;
    st.mon_x = static_cast<float>(cx) + nx * max_mon_r;
    st.mon_y = static_cast<float>(cy) + ny * max_mon_r;
    const float v_dot = st.mon_vx * nx + st.mon_vy * ny;
    if (v_dot > 0.0f) {
      st.mon_vx -= 1.6f * v_dot * nx;
      st.mon_vy -= 1.6f * v_dot * ny;
    }
  }

  const float chomp_cap_r = game_f(36.0f);
  if (!in_celebration) {
    for (int i = 0; i < NUM_SNACKS; i++) {
      if (snacks[i].eaten) continue;
      const float dx = snacks[i].x - st.mon_x, dy = snacks[i].y - st.mon_y;
      if (std::sqrt(dx * dx + dy * dy) < chomp_cap_r) {
        snacks[i].eaten = true;
        st.snacks_left--;
        st.snack_score++;
        st.chomp_timer = now_ms;
        st.chomp_word_idx = (st.chomp_word_idx + 1) % 5;
        if (st.snack_score > high_score_chomp) high_score_chomp = st.snack_score;
        if (st.snacks_left <= 0) {
          st.snack_score += 3;
          if (st.snack_score > high_score_chomp) {
            high_score_chomp = st.snack_score;
            schedule_persisted_prefs_save(now_ms);
          }
          st.level_clear_until_ms = now_ms + 3000U;
          in_celebration = true;
          pending_game_sfx() = 5;
          const UiColor p_cols[5] = {ctx.col_gold, ctx.col_cyan, ctx.col_magenta, ctx.col_green, ctx.col_orange};
          const float c_off = game_f(20.0f);
          const float c_up = game_f(40.0f);
          for (int c = 0; c < 16; c++) {
            const float ang = static_cast<float>(c) * (2.0f * PI_F / 16.0f);
            const float spd = game_f(105.0f + static_cast<float>((c % 3) * 34));
            confetti[c].x = st.mon_x + fast_cos(ang) * c_off;
            confetti[c].y = st.mon_y + fast_sin(ang) * c_off;
            confetti[c].vx = fast_cos(ang) * spd;
            confetti[c].vy = fast_sin(ang) * spd - c_up;
            const UiColor pc = p_cols[c % 5];
            confetti[c].r = pc.r;
            confetti[c].g = pc.g;
            confetti[c].b = pc.b;
          }
        } else {
          pending_game_sfx() = 1 + ((st.snack_score + snacks[i].kind) & 3);
        }
        break;
      }
    }
  }

  if (in_celebration) {
    const float conf_g = game_f(110.0f) * dt;
    for (int c = 0; c < 16; c++) {
      confetti[c].x += confetti[c].vx * dt;
      confetti[c].y += confetti[c].vy * dt;
      confetti[c].vy += conf_g;
    }
  }

  const bool is_chomping = in_celebration || (st.chomp_timer > 0U && (now_ms - st.chomp_timer < 520U));
  const bool mouth_open = (!in_celebration && nearest_dist < game_f(82.0f));

  fast_circle(it, cx, cy, game_px(208), ctx.col_dark_gray);
  fast_line(it, cx - game_px(151), bot_wall_y, cx + game_px(151), bot_wall_y, ctx.col_dark_gray);
  char t_buf[56];
  std::snprintf(t_buf, sizeof(t_buf), "Lv %d  •  Left:%d/7  •  Score:%d  •  Best:%d",
                st.level_num, st.snacks_left, st.snack_score, static_cast<int>(high_score_chomp));
  render_game_hud_footer(it, cx, t_buf, ctx.font_small, ctx.col_gold);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "Exit");

  const float t_bob = static_cast<float>(now_ms) * 0.0045f;
  const float bob_amp = game_f(2.5f);
  for (int i = 0; i < NUM_SNACKS; i++) {
    if (snacks[i].eaten) continue;
    const int bob_y = static_cast<int>(fast_sin(t_bob + static_cast<float>(i) * 1.15f) * bob_amp);
    render_chomp_snack(it, static_cast<int>(snacks[i].x), static_cast<int>(snacks[i].y) + bob_y,
                       snacks[i].kind, ctx.col_white, ctx.col_red, ctx.col_gold, ctx.col_cyan,
                       ctx.col_green, ctx.col_magenta, ctx.col_bg);
  }

  const int imx = static_cast<int>(st.mon_x), imy = static_cast<int>(st.mon_y);
  render_chomp_character(it, imx, imy, mon_r, look_dx, look_dy, mouth_open, is_chomping,
                         now_ms, ctx.col_white, ctx.col_black, ctx.col_dark_gray);

  if (in_celebration) {
    const uint32_t rem_cel = st.level_clear_until_ms - now_ms;
    char p_msg[36];
    if (rem_cel > 1900U) {
      const int conf_r = std::max(2, game_px(3));
      for (int c = 0; c < 16; c++) {
        fast_filled_circle(it, static_cast<int>(confetti[c].x), static_cast<int>(confetti[c].y), conf_r,
                           UiColor(confetti[c].r, confetti[c].g, confetti[c].b));
      }
      std::snprintf(p_msg, sizeof(p_msg), "LEVEL %d CLEAR!", st.level_num);
      fast_print(it, cx, imy - game_px(62), ctx.font_date, ctx.col_gold, UiTextAlign::CENTER, p_msg);
    } else {
      std::snprintf(p_msg, sizeof(p_msg), "GET READY! LEVEL %d", st.level_num + 1);
      fast_print(it, cx, imy - game_px(62), ctx.font_date, ctx.col_cyan, UiTextAlign::CENTER, p_msg);
    }
  } else if (is_chomping) {
    static constexpr const char *CHOMP_WORDS[5] = {
        "NOM NOM!", "YUMMY!", "COOKIE!", "CHOMP!", "DELICIOUS!"};
    const int pop_y = (imy > (SCREEN_H * 32) / 100) ? (imy - game_px(52)) : (imy + game_px(52));
    const int pop_x = std::clamp(imx, dp_px(100), SCREEN_W - dp_px(100));
    fast_print(it, pop_x, pop_y, ctx.font_date, ctx.col_gold, UiTextAlign::CENTER, CHOMP_WORDS[st.chomp_word_idx]);
    fast_filled_circle(it, imx - game_px(42), imy - game_px(16), std::max(2, game_px(3)), ctx.col_gold);
    fast_filled_circle(it, imx + game_px(42), imy - game_px(16), std::max(2, game_px(3)), ctx.col_cyan);
  }
}
#endif  // LATTER_HAS_IMU

}  // namespace latter_ui
