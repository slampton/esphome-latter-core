#include "esphome/core/defines.h"

#include "ui_pages.h"
#include "ui_touch_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef USE_ESP32
#include <esp_heap_caps.h>
#endif

namespace latter_ui {
namespace {

using esphome::Color;
using esphome::display::BaseFont;
using esphome::display::Display;
using esphome::display::TextAlign;

struct GameBallPod {
  float x, y, vx, vy, r;
  int col_idx;
  bool sunk;
  uint32_t sink_start_ms;
  float sink_x, sink_y;
};

struct GamePinnedPod {
  float dx, dy;
  bool active;
};

struct GameBrickPod {
  int16_t x, y, w, h;
  uint8_t row;
  uint8_t hp;
  uint8_t max_hp;
};

struct GameParticlePod {
  float x, y, vx, vy;
  uint8_t r, g, b;
};

struct GameSnackPod {
  float x, y, vx, vy;
  int kind;
  bool eaten;
};

struct GameArenaScratch {
  GameBallPod balls[5];
  GamePinnedPod pinned[10];
  GameBrickPod bricks[18];
  GameParticlePod debris[6];
  GameSnackPod snacks[7];
  GameParticlePod confetti[16];
  int grid_2048[4][4];
  int mem_cards[16];
  bool mem_matched[16];
};

GameArenaScratch &game_arena() {
  static GameArenaScratch *s_ptr = []() -> GameArenaScratch * {
#ifdef USE_ESP32
    void *p = heap_caps_calloc(1, sizeof(GameArenaScratch), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == nullptr) {
      p = heap_caps_calloc(1, sizeof(GameArenaScratch), MALLOC_CAP_8BIT);
    }
    if (p != nullptr) return static_cast<GameArenaScratch *>(p);
#endif
    return new GameArenaScratch{};
  }();
  return *s_ptr;
}

bool &game_imu_retare_flag() {
  static bool flag = false;
  return flag;
}

void render_glass_marble(Display &it, int bx, int by, int br,
                         Color base_col, Color hi_col,
                         int sh_x = 2, int sh_y = 3) {
  if (br <= 1) return;
  if (br >= 4) {
    fast_filled_circle(it, bx + sh_x, by + sh_y, br, Color(10, 14, 20));
  }
  fast_filled_circle(it, bx, by, br, base_col);
  fast_circle(it, bx, by, br, Color(14, 18, 24));
  if (br >= 6) {
    Color rim_shade(static_cast<uint8_t>((base_col.r * 170) >> 8),
                    static_cast<uint8_t>((base_col.g * 170) >> 8),
                    static_cast<uint8_t>((base_col.b * 170) >> 8));
    fast_circle(it, bx, by, br - 1, rim_shade);
  }
  if (br >= 8) {
    fast_circle(it, bx - 1, by - 1, br / 2, hi_col);
    fast_line(it, bx - br / 3, by + br / 4, bx + br / 3, by - br / 4, hi_col);
  }
  fast_filled_circle(it, bx - (br / 3), by - (br / 3), std::max(1, br / 4), Color(255, 255, 255));
  if (br >= 10) {
    fast_filled_circle(it, bx + (br / 3), by + (br / 3), std::max(1, br / 6), hi_col);
  }
}

void render_game_marbles(Display &it, const UiFrameContext &ctx,
                         bool game_just_entered, float roll_delta, float pitch_delta,
                         float &marble_touch_x, float &marble_touch_y,
                         bool &game_btn_pressed, int32_t &high_score_marbles) {
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto *balls = game_arena().balls;
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
    float base_ang = static_cast<float>(w_idx - 1) * 0.65f;
    for (int i = 0; i < 5; i++) {
      float ang = base_ang + static_cast<float>(i) * 1.256637f;
      float s_a = 0.0f, c_a = 1.0f;
      fast_sincos(ang, s_a, c_a);
      float dist = scale_min_f(112.0f + static_cast<float>((i & 1) * 16));
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

  float dt = (last_phys_ms == 0U) ? 0.05f : static_cast<float>(now_ms - last_phys_ms) * 0.001f;
  last_phys_ms = now_ms;
  if (dt < 0.012f) dt = 0.012f;
  if (dt > 0.075f) dt = 0.075f;

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
      if (is_new_high) save_persisted_prefs();
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
  float orbit_offset = (wave_num >= 6)
      ? (static_cast<float>(now_ms % 60000U) * 0.001f * 0.30f * static_cast<float>(std::min(4, wave_num - 5)))
      : 0.0f;
  for (int p = 0; p < num_pegs; p++) {
    float s_p = 0.0f, c_p = 1.0f;
    fast_sincos(PEG_BASE_ANG[p] + orbit_offset, s_p, c_p);
    peg_x[p] = static_cast<float>(cx) + c_p * peg_ring_r;
    peg_y[p] = static_cast<float>(cy) + s_p * peg_ring_r;
  }

  if (marble_touch_x >= 0.0f || game_btn_pressed) {
    float tx = (marble_touch_x >= 0.0f) ? marble_touch_x : static_cast<float>(cx);
    float ty = (marble_touch_y >= 0.0f) ? marble_touch_y : static_cast<float>(cy);
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
        float tdx = balls[i].x - tx;
        float tdy = balls[i].y - ty;
        float tdist = std::sqrt(tdx * tdx + tdy * tdy);
        float mass_scale = 16.0f / balls[i].r;
        if (tdist < 22.0f) {
          float rnd_angle = static_cast<float>(i * 72 + (now_ms % 360U)) * (PI_F / 180.0f);
          float s_rnd = 0.0f, c_rnd = 1.0f;
          fast_sincos(rnd_angle, s_rnd, c_rnd);
          balls[i].vx += c_rnd * 620.0f * mass_scale;
          balls[i].vy += s_rnd * 620.0f * mass_scale;
        } else {
          float impulse = (480.0f + (680.0f / (tdist * 0.08f + 1.0f))) * mass_scale;
          if (impulse > 820.0f) impulse = 820.0f;
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

  float ax = std::clamp(roll_delta * 34.0f, -650.0f, 650.0f);
  float ay = std::clamp(-pitch_delta * 34.0f, -650.0f, 650.0f);

  int sunk_count = 0;
  if (!round_over) {
    for (int i = 0; i < 5; i++) {
      if (balls[i].sunk) {
        sunk_count++;
        continue;
      }

      float dx = balls[i].x - static_cast<float>(cx);
      float dy = balls[i].y - static_cast<float>(cy);
      float dist_hole = std::sqrt(dx * dx + dy * dy);

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
        int pts = 200 + (combo_streak - 1) * 150;
        game_score += pts;
        popup_pts = pts;
        popup_combo = combo_streak;
        popup_ms = now_ms;
        pending_game_sfx() = 6;
        continue;
      }

      if (dist_hole < hole_bevel_r && dist_hole >= hole_cap_r) {
        float bevel_pull = (hole_bevel_r - dist_hole) * 9.5f;
        balls[i].vx -= (dx / dist_hole) * bevel_pull * dt;
        balls[i].vy -= (dy / dist_hole) * bevel_pull * dt;
      } else if (dist_hole > outer_lip_r) {
        float rim_nudge = (dist_hole - outer_lip_r) * 1.15f;
        balls[i].vx -= (dx / dist_hole) * rim_nudge * dt;
        balls[i].vy -= (dy / dist_hole) * rim_nudge * dt;
      }

      float inertia_scale = 0.92f + (scale_min_f(20.0f) - balls[i].r) * 0.02f;
      balls[i].vx = (balls[i].vx + ax * inertia_scale * dt) * friction;
      balls[i].vy = (balls[i].vy + ay * inertia_scale * dt) * friction;
      balls[i].x += balls[i].vx * dt;
      balls[i].y += balls[i].vy * dt;

      for (int p = 0; p < num_pegs; p++) {
        float pdx = balls[i].x - peg_x[p];
        float pdy = balls[i].y - peg_y[p];
        float pdist2 = pdx * pdx + pdy * pdy;
        float min_pdist = balls[i].r + peg_r;
        if (pdist2 < min_pdist * min_pdist && pdist2 > 0.0001f) {
          float pdist = std::sqrt(pdist2);
          float pnx = pdx / pdist;
          float pny = pdy / pdist;
          balls[i].x = peg_x[p] + pnx * min_pdist;
          balls[i].y = peg_y[p] + pny * min_pdist;
          float v_dot_n = balls[i].vx * pnx + balls[i].vy * pny;
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

      float max_bot_y = static_cast<float>(wall_y) - balls[i].r;
      if (balls[i].y > max_bot_y) {
        balls[i].y = max_bot_y;
        if (balls[i].vy > 0.0f) balls[i].vy = -balls[i].vy * bounce;
      }

      float cur_dx = balls[i].x - static_cast<float>(cx);
      float cur_dy = balls[i].y - static_cast<float>(cy);
      float cur_dist = std::sqrt(cur_dx * cur_dx + cur_dy * cur_dy);
      float max_dist = arena_r - balls[i].r;
      if (cur_dist > max_dist && cur_dist > 0.001f) {
        float nx = cur_dx / cur_dist;
        float ny = cur_dy / cur_dist;
        balls[i].x = static_cast<float>(cx) + nx * max_dist;
        balls[i].y = static_cast<float>(cy) + ny * max_dist;
        float v_dot_n = balls[i].vx * nx + balls[i].vy * ny;
        if (v_dot_n > 0.0f) {
          balls[i].vx -= (1.0f + bounce) * v_dot_n * nx;
          balls[i].vy -= (1.0f + bounce) * v_dot_n * ny;
        }
        float tx_w = -ny;
        float ty_w = nx;
        float v_dot_t = balls[i].vx * tx_w + balls[i].vy * ty_w;
        balls[i].vx -= 0.07f * v_dot_t * tx_w;
        balls[i].vy -= 0.07f * v_dot_t * ty_w;
      }
    }

    for (int i = 0; i < 5; i++) {
      if (balls[i].sunk) continue;
      for (int j = i + 1; j < 5; j++) {
        if (balls[j].sunk) continue;
        float cdx = balls[j].x - balls[i].x;
        float cdy = balls[j].y - balls[i].y;
        float min_dist = balls[i].r + balls[j].r;
        float cdist2 = cdx * cdx + cdy * cdy;
        if (cdist2 < min_dist * min_dist && cdist2 > 0.0001f) {
          float cdist = std::sqrt(cdist2);
          float overlap = min_dist - cdist;
          float cnx = cdx / cdist;
          float cny = cdy / cdist;
          float mi = balls[i].r * balls[i].r;
          float mj = balls[j].r * balls[j].r;
          float inv_m_sum = 1.0f / (mi + mj);

          balls[i].x -= cnx * overlap * (mj * inv_m_sum);
          balls[i].y -= cny * overlap * (mj * inv_m_sum);
          balls[j].x += cnx * overlap * (mi * inv_m_sum);
          balls[j].y += cny * overlap * (mi * inv_m_sum);

          float kx = balls[i].vx - balls[j].vx;
          float ky = balls[i].vy - balls[j].vy;
          float v_rel = cnx * kx + cny * ky;
          if (v_rel > 0.0f) {
            float imp = (1.0f + bounce) * v_rel * inv_m_sum;
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
          save_persisted_prefs();
        }
      }
    }
  } else {
    for (int i = 0; i < 5; i++) {
      if (balls[i].sunk) sunk_count++;
    }
  }

  const int hud_y = layout().back_y - scale_y(34);
  const int wall_hw = scale_min(151);
  fast_line(it, cx - wall_hw, wall_y, cx + wall_hw, wall_y, ctx.col_slate * 0.45f);
  fast_line(it, cx - wall_hw + 2, wall_y + 1, cx + wall_hw - 2, wall_y + 1, ctx.col_dark_gray);
  char hud_buf[56];
  std::snprintf(hud_buf, sizeof(hud_buf), "W%d  •  %d/5  •  %ds  •  %d",
                wave_num, sunk_count, time_remaining, game_score);
  fast_print(it, cx, hud_y, ctx.font_small, time_remaining < 10 ? ctx.col_orange : ctx.col_gold,
             TextAlign::CENTER, hud_buf);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "Exit");

  fast_circle(it, cx, cy, scale_min(34), ctx.col_dark_gray);
  fast_circle(it, cx, cy, scale_min(30), ctx.col_slate);
  fast_circle(it, cx, cy, scale_min(29), ctx.col_white);
  fast_circle(it, cx, cy, scale_min(28), ctx.col_white);
  fast_filled_circle(it, cx, cy, scale_min(25), ctx.col_bg);

  float swirl = static_cast<float>((now_ms / 25U) % 360U) * (PI_F / 180.0f);
  for (int s = 0; s < 4; s++) {
    float sa = swirl + static_cast<float>(s) * (PI_F * 0.5f);
    float s_sa = 0.0f, c_sa = 1.0f;
    fast_sincos(sa, s_sa, c_sa);
    fast_line(it, cx + static_cast<int>(c_sa * scale_min_f(26.0f)),
              cy + static_cast<int>(s_sa * scale_min_f(26.0f)),
              cx + static_cast<int>(c_sa * scale_min_f(32.0f)),
              cy + static_cast<int>(s_sa * scale_min_f(32.0f)), ctx.col_slate);
  }

  for (int p = 0; p < num_pegs; p++) {
    int px = static_cast<int>(std::round(peg_x[p]));
    int py = static_cast<int>(std::round(peg_y[p]));
    bool peg_flashing = (peg_hit_ms[p] > 0U && (now_ms - peg_hit_ms[p] < 200U));
    if (peg_flashing) {
      fast_circle(it, px, py, 10, ctx.col_gold);
      fast_circle(it, px, py, 8, ctx.col_cyan);
    }
    fast_filled_circle(it, px + 1, py + 2, 6, Color(10, 14, 20));
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

  Color base_cols[5] = {ctx.col_gold, ctx.col_cyan, ctx.col_green, ctx.col_red, ctx.col_magenta};
  Color hi_cols[5] = {
      Color(255, 245, 157), Color(180, 255, 255), Color(185, 246, 202),
      Color(255, 138, 128), Color(248, 187, 208)};
  int sh_x = static_cast<int>(ax * 0.012f);
  int sh_y = static_cast<int>(ay * 0.012f);

  for (int i = 0; i < 5; i++) {
    if (balls[i].sunk) {
      uint32_t sink_el = now_ms - balls[i].sink_start_ms;
      if (sink_el < 240U) {
        float prog = static_cast<float>(sink_el) * (1.0f / 240.0f);
        int sx = static_cast<int>(balls[i].sink_x + (static_cast<float>(cx) - balls[i].sink_x) * prog);
        int sy = static_cast<int>(balls[i].sink_y + (static_cast<float>(cy) - balls[i].sink_y) * prog);
        int sr = std::max(3, static_cast<int>(balls[i].r * (1.0f - prog * 0.82f)));
        render_glass_marble(it, sx, sy, sr, base_cols[balls[i].col_idx], hi_cols[balls[i].col_idx], 0, 0);
      }
      continue;
    }
    render_glass_marble(it, static_cast<int>(balls[i].x), static_cast<int>(balls[i].y),
                        static_cast<int>(balls[i].r),
                        base_cols[balls[i].col_idx], hi_cols[balls[i].col_idx], sh_x, sh_y);
  }

  if (!round_over && popup_ms > 0U && (now_ms - popup_ms < 650U)) {
    uint32_t pel = now_ms - popup_ms;
    int py = cy - 42 - static_cast<int>(pel / 22U);
    char pop_buf[24];
    if (popup_combo > 1) {
      std::snprintf(pop_buf, sizeof(pop_buf), "+%d %dx!", popup_pts, popup_combo);
    } else {
      std::snprintf(pop_buf, sizeof(pop_buf), "+%d", popup_pts);
    }
    fast_print(it, cx, py, ctx.font_small, popup_combo > 1 ? ctx.col_white : ctx.col_gold,
               TextAlign::CENTER, pop_buf);
  }

  if (!round_over && in_wave_pause) {
    char w_buf[36];
    if (now_ms - wave_clear_ms < 1100U) {
      std::snprintf(w_buf, sizeof(w_buf), "WAVE %d CLEAR! +6s", wave_num - 1);
      fast_print(it, cx, cy - scale_y(107), ctx.font_date, ctx.col_gold, TextAlign::CENTER, w_buf);
    } else {
      std::snprintf(w_buf, sizeof(w_buf), "GET READY! WAVE %d", wave_num);
      fast_print(it, cx, cy - scale_y(107), ctx.font_date, ctx.col_cyan, TextAlign::CENTER, w_buf);
    }
  }

  if (round_over) {
    char title_buf[32];
    Color title_col = ctx.col_gold;
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

void render_archery_target(Display &it, int tx, int ty, int r_outer,
                           Color col_white, Color col_black,
                           Color col_cyan, Color col_red,
                           Color col_gold, Color col_slate,
                           Color col_dark_gray) {
  if (r_outer <= 4) return;
  int r_black = (r_outer * 76) / 100;
  int r_cyan = (r_outer * 54) / 100;
  int r_red = (r_outer * 34) / 100;
  int r_gold = std::max(3, (r_outer * 16) / 100);

  fast_filled_circle(it, tx, ty + 3, r_outer, col_dark_gray);
  fast_filled_circle(it, tx, ty, r_outer, col_white);
  fast_circle(it, tx, ty, r_outer, col_dark_gray);
  fast_filled_circle(it, tx, ty, r_black, col_black);
  fast_circle(it, tx, ty, r_black, col_slate);
  fast_filled_circle(it, tx, ty, r_cyan, col_cyan);
  fast_circle(it, tx, ty, r_cyan, Color(0, 175, 205));
  fast_filled_circle(it, tx, ty, r_red, col_red);
  fast_circle(it, tx, ty, r_red, Color(180, 20, 20));
  fast_filled_circle(it, tx, ty, r_gold, col_gold);
  fast_pixel(it, tx, ty, col_red);
}

void render_game_archery(Display &it, const UiFrameContext &ctx,
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
  static Color feedback_col(255, 215, 0);
  static uint32_t feedback_timer = 0;
  static uint32_t round_banner_ms = 0;
  static bool round_over = false;
  static bool is_new_high = false;
  auto *pinned = game_arena().pinned;
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

  float dt = (last_phys_ms == 0U) ? 0.05f : static_cast<float>(now_ms - last_phys_ms) * 0.001f;
  last_phys_ms = now_ms;
  if (dt < 0.012f) dt = 0.012f;
  if (dt > 0.075f) dt = 0.075f;

  const int target_r = scale_min(std::max(26, 36 - (round_num - 1) * 2));
  const float sway_spd = 0.78f + static_cast<float>(std::min(6, round_num - 1)) * 0.16f;
  target_phase += sway_spd * dt;

  const float target_base_y = static_cast<float>(scale_y(112));
  const float bow_y = static_cast<float>(layout().back_y - scale_y(88));
  float target_x = static_cast<float>(cx) + fast_sin(target_phase) * scale_min_f(102.0f);
  float target_y = target_base_y + fast_cos(target_phase * 1.4f) * scale_min_f(24.0f);

  float max_wind = std::min(8.0f, 2.5f + static_cast<float>(round_num - 1) * 1.3f);
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
    float z = std::min(1.0f, flight_z);
    float wind_drift = launch_wind * 5.2f * z * z;
    float arc_lift = -26.0f * z * (1.0f - z);

    cur_arrow_x = static_cast<float>(cx) + (launch_aim_x - static_cast<float>(cx)) * z + wind_drift;
    cur_arrow_y = bow_y + (launch_aim_y - bow_y) * z + arc_lift;

    float vx = (launch_aim_x - static_cast<float>(cx)) + launch_wind * 10.4f * z;
    float vy = (launch_aim_y - bow_y) - 26.0f * (1.0f - 2.0f * z);
    float vlen = std::sqrt(vx * vx + vy * vy);
    if (vlen > 0.001f) {
      arrow_dx = vx / vlen;
      arrow_dy = vy / vlen;
    }

    if (flight_z >= 1.0f) {
      arrow_in_flight = false;
      float hit_dx = cur_arrow_x - target_x;
      float hit_dy = cur_arrow_y - target_y;
      float dist_hit = std::sqrt(hit_dx * hit_dx + hit_dy * hit_dy);

      float r_out = static_cast<float>(target_r);
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
        int earned = base_pts * streak_mult;
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
          if (is_new_high) save_persisted_prefs();
          round_num++;
          round_pts = 0;
          round_banner_ms = now_ms;
          pending_game_sfx() = 5;
        } else {
          round_over = true;
          if (is_new_high) save_persisted_prefs();
        }
      }
    }
  }

  int tx_i = static_cast<int>(std::round(target_x));
  int ty_i = static_cast<int>(std::round(target_y));
  render_archery_target(it, tx_i, ty_i, target_r,
                        ctx.col_white, ctx.col_black, ctx.col_cyan, ctx.col_red,
                        ctx.col_gold, ctx.col_slate, ctx.col_dark_gray);

  for (int i = 0; i < pinned_count; i++) {
    if (!pinned[i].active) continue;
    int px = tx_i + static_cast<int>(std::round(pinned[i].dx));
    int py = ty_i + static_cast<int>(std::round(pinned[i].dy));
    fast_filled_circle(it, px, py, 2, ctx.col_black);
    fast_line(it, px, py, px - 5, py + 11, ctx.col_gold);
    fast_line(it, px - 5, py + 11, px - 8, py + 10, ctx.col_red);
    fast_line(it, px - 5, py + 11, px - 3, py + 13, ctx.col_red);
  }

  int rx_i = static_cast<int>(std::round(smooth_ax));
  int ry_i = static_cast<int>(std::round(smooth_ay));
  fast_circle(it, rx_i, ry_i, 13, ctx.col_cyan);
  fast_line(it, rx_i - 17, ry_i, rx_i - 5, ry_i, ctx.col_cyan);
  fast_line(it, rx_i + 5, ry_i, rx_i + 17, ry_i, ctx.col_cyan);
  fast_line(it, rx_i, ry_i - 17, rx_i, ry_i - 5, ctx.col_cyan);
  fast_line(it, rx_i, ry_i + 5, rx_i, ry_i + 17, ctx.col_cyan);
  fast_pixel(it, rx_i, ry_i, ctx.col_gold);

  float aim_dx = smooth_ax - static_cast<float>(cx);
  float aim_dy = smooth_ay - bow_y;
  float aim_len = std::sqrt(aim_dx * aim_dx + aim_dy * aim_dy);
  float ux = (aim_len > 1.0f) ? (aim_dx / aim_len) : 0.0f;
  float uy = (aim_len > 1.0f) ? (aim_dy / aim_len) : -1.0f;
  float px_v = -uy;
  float py_v = ux;

  int bow_cx = cx;
  int bow_cy = static_cast<int>(bow_y);
  int left_tip_x = bow_cx - static_cast<int>(px_v * 38.0f + ux * 10.0f);
  int left_tip_y = bow_cy - static_cast<int>(py_v * 38.0f + uy * 10.0f);
  int right_tip_x = bow_cx + static_cast<int>(px_v * 38.0f - ux * 10.0f);
  int right_tip_y = bow_cy + static_cast<int>(py_v * 38.0f - uy * 10.0f);
  int left_mid_x = bow_cx - static_cast<int>(px_v * 20.0f - ux * 4.0f);
  int left_mid_y = bow_cy - static_cast<int>(py_v * 20.0f - uy * 4.0f);
  int right_mid_x = bow_cx + static_cast<int>(px_v * 20.0f + ux * 4.0f);
  int right_mid_y = bow_cy + static_cast<int>(py_v * 20.0f + uy * 4.0f);

  fast_line(it, bow_cx, bow_cy, left_mid_x, left_mid_y, ctx.col_gold);
  fast_line(it, left_mid_x, left_mid_y, left_tip_x, left_tip_y, ctx.col_gold);
  fast_line(it, bow_cx, bow_cy, right_mid_x, right_mid_y, ctx.col_gold);
  fast_line(it, right_mid_x, right_mid_y, right_tip_x, right_tip_y, ctx.col_gold);
  fast_line(it, bow_cx, bow_cy + 1, left_mid_x, left_mid_y + 1, ctx.col_slate);
  fast_line(it, bow_cx, bow_cy + 1, right_mid_x, right_mid_y + 1, ctx.col_slate);
  fast_filled_circle(it, bow_cx, bow_cy, 3, ctx.col_red);

  if (!arrow_in_flight && arrows_left > 0 && !round_over) {
    int nock_x = bow_cx - static_cast<int>(ux * 14.0f);
    int nock_y = bow_cy - static_cast<int>(uy * 14.0f);
    int tip_x = bow_cx + static_cast<int>(ux * 18.0f);
    int tip_y = bow_cy + static_cast<int>(uy * 18.0f);
    fast_line(it, left_tip_x, left_tip_y, nock_x, nock_y, ctx.col_slate);
    fast_line(it, right_tip_x, right_tip_y, nock_x, nock_y, ctx.col_slate);
    fast_line(it, nock_x, nock_y, tip_x, tip_y, ctx.col_white);
    fast_filled_circle(it, tip_x, tip_y, 2, ctx.col_gold);
  } else {
    fast_line(it, left_tip_x, left_tip_y, right_tip_x, right_tip_y, ctx.col_slate);
  }

  if (arrow_in_flight) {
    float z = std::min(1.0f, flight_z);
    float a_len = 32.0f - z * 16.0f;
    int head_x = static_cast<int>(std::round(cur_arrow_x));
    int head_y = static_cast<int>(std::round(cur_arrow_y));
    int tail_x = static_cast<int>(std::round(cur_arrow_x - arrow_dx * a_len));
    int tail_y = static_cast<int>(std::round(cur_arrow_y - arrow_dy * a_len));

    fast_line(it, tail_x, tail_y, head_x, head_y, ctx.col_gold);
    fast_line(it, tail_x + 1, tail_y, head_x + 1, head_y, ctx.col_white);
    fast_filled_circle(it, head_x, head_y, z > 0.65f ? 2 : 3, ctx.col_white);
    int f_px = static_cast<int>(-arrow_dy * 4.0f);
    int f_py = static_cast<int>(arrow_dx * 4.0f);
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
      fast_print(it, cx, cy - scale_y(8), ctx.font_date, ctx.col_gold, TextAlign::CENTER, rb);
    } else {
      std::snprintf(rb, sizeof(rb), "ROUND %d! GOAL %d", round_num, round_goal);
      fast_print(it, cx, cy - scale_y(8), ctx.font_date, ctx.col_cyan, TextAlign::CENTER, rb);
    }
  } else if (!round_over && hit_feedback[0] != '\0' && (now_ms - feedback_timer < 1100U)) {
    fast_print(it, cx, cy - scale_y(8), ctx.font_date, feedback_col, TextAlign::CENTER, hit_feedback);
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
  fast_print(it, cx, layout().back_y - scale_y(34), ctx.font_small, ctx.col_gold, TextAlign::CENTER, hud_buf);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "Exit");

  if (round_over) {
    char title_buf[32];
    Color title_col = ctx.col_gold;
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
                        ctx.theme_secondary, ctx.col_white, ctx.col_gold, ctx.col_slate, WidgetId::GAME_OVER_CTA);
  }
}

void render_brick_tile(Display &it, int cx_b, int cy_b, int w, int h,
                       Color body_col, Color hi_col,
                       int hp, int max_hp, Color col_white) {
  int rx = cx_b - w / 2;
  int ry = cy_b - h / 2;
  fast_hline(it, rx + 1, ry + h, w - 1, Color(12, 16, 24));
  fill_rect_row_major(it, rx, ry, w, h, body_col);
  fast_rect_outline(it, rx, ry, w, h, Color(18, 22, 30));
  fast_hline(it, rx + 1, ry + 1, w - 2, hi_col);
  fast_line(it, rx + 1, ry + 1, rx + 1, ry + h - 2, hi_col);
  Color sh_col = body_col * 0.58f;
  fast_hline(it, rx + 2, ry + h - 2, w - 3, sh_col);

  if (max_hp >= 2) {
    if (hp >= 2) {
      fast_pixel(it, rx + 3, ry + 3, col_white);
      fast_pixel(it, rx + w - 4, ry + 3, col_white);
      fast_pixel(it, rx + 3, ry + h - 4, col_white);
      fast_pixel(it, rx + w - 4, ry + h - 4, col_white);
      if (hp >= 3) {
        fill_rect_row_major(it, cx_b - 5, cy_b - 2, 10, 4, col_white);
      } else if (max_hp == 3) {
        fast_line(it, cx_b - 4, ry + 2, cx_b + 3, ry + h - 3, Color(20, 24, 32));
      }
    } else if (hp == 1) {
      fast_line(it, cx_b - 6, ry + 1, cx_b, cy_b, Color(16, 20, 28));
      fast_line(it, cx_b, cy_b, cx_b + 6, ry + h - 2, Color(16, 20, 28));
      fast_line(it, cx_b - 5, ry + 1, cx_b + 1, cy_b, hi_col);
    }
  }
}

void render_brick_paddle(Display &it, int px, int py, int pw, int ph,
                         Color pad_col, Color col_white) {
  int r = ph / 2;
  int x_l = px - pw / 2 + r;
  int x_r = px + pw / 2 - r;
  int body_w = std::max(2, x_r - x_l);

  fast_hline(it, x_l, py + r + 1, body_w, Color(12, 16, 24));
  fast_filled_circle(it, x_l, py, r, pad_col);
  fast_filled_circle(it, x_r, py, r, pad_col);
  fill_rect_row_major(it, x_l, py - r, body_w, ph + 1, pad_col);
  fast_hline(it, x_l, py - r, body_w, col_white);
  fast_hline(it, x_l, py + r, body_w, pad_col * 0.55f);
  int core_w = (pw * 42) / 100;
  fill_rect_row_major(it, px - core_w / 2, py - 1, core_w, 3, col_white);
}

void render_game_bricks(Display &it, const UiFrameContext &ctx,
                        bool game_just_entered, float roll_delta,
                        float &marble_touch_x, bool &game_btn_pressed,
                        int32_t &high_score_bricks) {
  static constexpr int NUM_BRICKS = 18;
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto *bricks = game_arena().bricks;
  static uint32_t last_phys_ms = 0;
  static int wave_num = 1;
  static int lives_left = 3;
  static int bo_score = 0;
  static int rally_combo = 0;
  static bool hit_brick_since_paddle = false;
  static bool ball_launched = false;
  static bool round_over = false;
  static bool is_new_high = false;

  static float ball_x = 233.0f;
  static float ball_y = 344.0f;
  static float ball_vx = 0.0f;
  static float ball_vy = 0.0f;
  static float trail_x[4] = {233.0f, 233.0f, 233.0f, 233.0f};
  static float trail_y[4] = {344.0f, 344.0f, 344.0f, 344.0f};
  static float smooth_paddle_x = 233.0f;

  auto *debris = game_arena().debris;
  static uint32_t debris_ms = 0;
  static int popup_x = 233;
  static int popup_y = 140;
  static uint32_t popup_ms = 0;
  static char popup_text[32] = "";
  static Color popup_col(255, 215, 0);

  static uint32_t banner_ms = 0;
  static char banner_text[32] = "";

  auto init_brick_wave = [&](int wv) {
    static constexpr int ROW_COUNTS[4] = {4, 5, 5, 4};
    static constexpr int ROW_YS[4] = {82, 104, 126, 148};
    const int brk_pitch = scale_min(46);
    const int brk_w = scale_min(40);
    const int brk_h = scale_min(14);
    int idx = 0;
    for (int r = 0; r < 4; r++) {
      int cnt = ROW_COUNTS[r];
      int start_x = cx - ((cnt - 1) * brk_pitch) / 2;
      int row_y = cy - scale_min(233 - ROW_YS[r]);
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
        bricks[idx].x = static_cast<int16_t>(start_x + c * brk_pitch);
        bricks[idx].y = static_cast<int16_t>(row_y);
        bricks[idx].w = static_cast<int16_t>(brk_w);
        bricks[idx].h = static_cast<int16_t>(brk_h);
        bricks[idx].row = static_cast<uint8_t>(r);
        bricks[idx].hp = hp;
        bricks[idx].max_hp = hp;
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

  float dt = (now_ms >= last_phys_ms) ? ((now_ms - last_phys_ms) * 0.001f) : 0.05f;
  last_phys_ms = now_ms;
  if (dt < 0.012f) dt = 0.012f;
  if (dt > 0.075f) dt = 0.075f;

  const int max_pad_dx = scale_min(136);
  float raw_target_x = static_cast<float>(cx) + roll_delta * scale_min_f(8.4f);
  if (touch_gesture().active && touch_gesture().last_y < layout().back_y - scale_y(38) &&
      (LATTER_HAS_IMU == 0 || touch_gesture().max_travel >= scale_s(8))) {
    raw_target_x = static_cast<float>(touch_gesture().last_x);
  }
  raw_target_x = std::clamp(raw_target_x, static_cast<float>(cx - max_pad_dx), static_cast<float>(cx + max_pad_dx));
  smooth_paddle_x += (raw_target_x - smooth_paddle_x) * 0.34f;
  float paddle_x = smooth_paddle_x;

  float target_speed = scale_min_f(std::min(285.0f, 215.0f + static_cast<float>(wave_num - 1) * 14.0f));

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
      if (std::fabs(launch_ang) < 0.14f) {
        launch_ang = (now_ms & 1U) ? 0.22f : -0.22f;
      }
      ball_vx = target_speed * fast_sin(launch_ang);
      ball_vy = -target_speed * fast_cos(launch_ang);
      ball_launched = true;
      hit_brick_since_paddle = true;
    }
  }

  Color row_cols[4] = {ctx.col_gold, ctx.col_cyan, ctx.col_orange, ctx.col_green};
  Color row_his[4] = {
      Color(255, 245, 165), Color(185, 255, 255),
      Color(255, 195, 100), Color(185, 250, 205)};

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
      float b_dx = ball_x - static_cast<float>(cx);
      float b_dy = ball_y - static_cast<float>(cy);
      float b_dist = std::sqrt(b_dx * b_dx + b_dy * b_dy);
      if (b_dist > dome_ball_r && ball_y < static_cast<float>(paddle_y - 4)) {
        float bnx = b_dx / b_dist;
        float bny = b_dy / b_dist;
        ball_x = static_cast<float>(cx) + bnx * (dome_ball_r - 1.0f);
        ball_y = static_cast<float>(cy) + bny * (dome_ball_r - 1.0f);

        float rnx = bnx;
        float rny = (ball_y > static_cast<float>(cy + 12)) ? 0.0f : bny;
        float r_len = std::sqrt(rnx * rnx + rny * rny);
        if (r_len > 0.001f) {
          rnx /= r_len;
          rny /= r_len;
        }

        float v_dot = ball_vx * rnx + ball_vy * rny;
        if (v_dot > 0.0f) {
          ball_vx -= 2.0f * v_dot * rnx;
          ball_vy -= 2.0f * v_dot * rny;
          wall_bounces++;
          if (wall_bounces >= 2) {
            ball_vy += (ball_vy >= 0.0f) ? 55.0f : 70.0f;
          }
          if (std::fabs(ball_vy) < 95.0f) {
            ball_vy = (ball_vy >= 0.0f) ? 98.0f : -98.0f;
          }
          float spd = std::sqrt(ball_vx * ball_vx + ball_vy * ball_vy);
          if (spd > 1.0f) {
            ball_vx = (ball_vx / spd) * target_speed;
            ball_vy = (ball_vy / spd) * target_speed;
          }
        }
      }

      if (ball_vy > 0.0f && ball_y >= static_cast<float>(paddle_y - 10) &&
          ball_y <= static_cast<float>(paddle_y + 8)) {
        float half_reach = paddle_w * 0.5f + 6.0f;
        if (std::fabs(ball_x - paddle_x) <= half_reach) {
          if (!hit_brick_since_paddle) rally_combo = 0;
          hit_brick_since_paddle = false;
          wall_bounces = 0;
          float offset = std::clamp((ball_x - paddle_x) / (paddle_w * 0.5f), -1.0f, 1.0f);
          float bounce_ang = offset * 1.047f;
          ball_vx = target_speed * fast_sin(bounce_ang);
          ball_vy = -target_speed * fast_cos(bounce_ang);
          ball_y = static_cast<float>(paddle_y - 11);
          pending_game_sfx() = 11;
        }
      }

      const float ball_r = 6.0f;
      for (int b = 0; b < NUM_BRICKS; b++) {
        if (bricks[b].hp == 0) continue;
        float dx = ball_x - static_cast<float>(bricks[b].x);
        float dy = ball_y - static_cast<float>(bricks[b].y);
        float lim_x = bricks[b].w * 0.5f + ball_r;
        float lim_y = bricks[b].h * 0.5f + ball_r;
        if (std::fabs(dx) < lim_x && std::fabs(dy) < lim_y) {
          float overlap_x = lim_x - std::fabs(dx);
          float overlap_y = lim_y - std::fabs(dy);
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
          int mult = std::min(4, rally_combo);

          static constexpr int BASE_ROW_PTS[4] = {150, 120, 90, 60};
          int base_pts = (bricks[b].hp == 0) ? BASE_ROW_PTS[bricks[b].row] : 50;
          int pts = base_pts * mult;
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

          Color frag_c = (bricks[b].hp > 0) ? Color(200, 210, 225) : row_cols[bricks[b].row];
          for (int s = 0; s < 6; s++) {
            float ang = static_cast<float>(s) * 1.047f + 0.35f;
            float f_spd = (bricks[b].hp == 0) ? 95.0f : 55.0f;
            debris[s].x = static_cast<float>(bricks[b].x) + fast_cos(ang) * 6.0f;
            debris[s].y = static_cast<float>(bricks[b].y) + fast_sin(ang) * 4.0f;
            debris[s].vx = fast_cos(ang) * f_spd;
            debris[s].vy = fast_sin(ang) * f_spd - 25.0f;
            const Color sc = (s & 1) ? row_his[bricks[b].row] : frag_c;
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
        int wave_bonus = wave_num * 300;
        bo_score += wave_bonus;
        if (bo_score > high_score_bricks) {
          high_score_bricks = bo_score;
          is_new_high = true;
        }
        if (is_new_high) save_persisted_prefs();
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
          if (is_new_high) save_persisted_prefs();
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
  fast_circle(it, cx, cy, dome_r - 1, ctx.col_slate * 0.35f);

  int bricks_remaining = 0;
  for (int b = 0; b < NUM_BRICKS; b++) {
    if (bricks[b].hp > 0) bricks_remaining++;
  }
  char bo_hud[56];
  if (rally_combo >= 2) {
    int mult = std::min(4, rally_combo);
    std::snprintf(bo_hud, sizeof(bo_hud), "W%d  •  %d/3  •  %dx  •  %d",
                  wave_num, lives_left, mult, bo_score);
  } else {
    std::snprintf(bo_hud, sizeof(bo_hud), "W%d  •  Balls:%d  •  Brk:%d  •  %d",
                  wave_num, lives_left, bricks_remaining, bo_score);
  }
  fast_print(it, cx, layout().back_y - scale_y(34), ctx.font_small, ctx.col_gold, TextAlign::CENTER, bo_hud);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "Exit");

  for (int b = 0; b < NUM_BRICKS; b++) {
    if (bricks[b].hp == 0) continue;
    Color b_col = row_cols[bricks[b].row];
    Color h_col = row_his[bricks[b].row];
    if (bricks[b].hp >= 3) {
      b_col = Color(225, 55, 75);
      h_col = Color(255, 175, 185);
    } else if (bricks[b].hp == 2) {
      b_col = Color(145, 162, 182);
      h_col = Color(230, 240, 252);
    }
    render_brick_tile(it, bricks[b].x, bricks[b].y, bricks[b].w, bricks[b].h,
                      b_col, h_col, bricks[b].hp, bricks[b].max_hp, ctx.col_white);
  }

  if (debris_ms > 0U && (now_ms - debris_ms < 360U)) {
    for (int s = 0; s < 6; s++) {
      fill_rect_row_major(it, static_cast<int>(debris[s].x) - 2, static_cast<int>(debris[s].y) - 2, 4, 4,
                          Color(debris[s].r, debris[s].g, debris[s].b));
    }
  }
  if (popup_ms > 0U && (now_ms - popup_ms < 650U)) {
    int py_off = static_cast<int>((now_ms - popup_ms) * 22U / 650U);
    fast_print(it, popup_x, popup_y - py_off, ctx.font_small, popup_col, TextAlign::CENTER, popup_text);
  }
  if (banner_ms > 0U) {
    uint32_t b_el = now_ms - banner_ms;
    if (b_el >= 1100U && bricks_remaining == 0) {
      init_brick_wave(wave_num);
    }
    if (b_el >= 3000U) {
      banner_ms = 0U;
    } else if (b_el < 1100U) {
      fast_print(it, cx, cy - scale_y(21), ctx.font_date, ctx.col_gold, TextAlign::CENTER, banner_text);
    } else {
      char rdy_buf[32];
      std::snprintf(rdy_buf, sizeof(rdy_buf), "GET READY! WAVE %d", wave_num);
      fast_print(it, cx, cy - scale_y(21), ctx.font_date, ctx.col_cyan, TextAlign::CENTER, rdy_buf);
    }
  }

  render_brick_paddle(it, static_cast<int>(paddle_x), paddle_y, paddle_w, paddle_h,
                      ctx.theme_primary, ctx.col_white);

  if (ball_launched) {
    fast_filled_circle(it, static_cast<int>(trail_x[3]), static_cast<int>(trail_y[3]), 2, Color(65, 52, 18));
    fast_filled_circle(it, static_cast<int>(trail_x[2]), static_cast<int>(trail_y[2]), 3, Color(120, 95, 25));
    fast_filled_circle(it, static_cast<int>(trail_x[0]), static_cast<int>(trail_y[0]), 4, Color(185, 145, 35));
  }
  fast_filled_circle(it, static_cast<int>(ball_x), static_cast<int>(ball_y), 6, ctx.col_gold);
  fast_circle(it, static_cast<int>(ball_x), static_cast<int>(ball_y), 6, ctx.col_white);
  fast_filled_circle(it, static_cast<int>(ball_x) - 2, static_cast<int>(ball_y) - 2, 2, ctx.col_white);

  if (round_over) {
    char title_buf[32];
    Color title_col = ctx.col_gold;
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
    std::snprintf(score_str, sizeof(score_str), "Score: %d", bo_score);
    char best_str[32];
    std::snprintf(best_str, sizeof(best_str), "Best: %d", static_cast<int>(high_score_bricks));
    ui::game_over_sheet(it, cx, cy, title_buf, title_col, score_str, best_str, is_new_high,
                        "PLAY AGAIN", ctx.font_date, ctx.font_small,
                        ctx.theme_primary, ctx.col_white, ctx.col_gold, ctx.col_slate, WidgetId::GAME_OVER_CTA);
  }
}

void render_chomp_snack(Display &it, int tx, int ty, int kind,
                        Color col_white, Color col_red,
                        Color col_gold, Color col_cyan,
                        Color col_green, Color col_magenta,
                        Color col_bg) {
  int k = kind % 5;
  fast_filled_circle(it, tx, ty + 3, 13, Color(14, 18, 26));

  if (k == 0) {
    Color dough(218, 162, 84);
    Color crust(168, 114, 46);
    Color chip(58, 34, 18);
    fast_filled_circle(it, tx, ty, 13, dough);
    fast_circle(it, tx, ty, 13, crust);
    fast_circle(it, tx, ty, 12, Color(238, 188, 112));
    fast_filled_circle(it, tx - 5, ty - 4, 2, chip);
    fast_filled_circle(it, tx + 4, ty - 5, 2, chip);
    fast_filled_circle(it, tx, ty + 1, 2, chip);
    fast_filled_circle(it, tx - 6, ty + 4, 2, chip);
    fast_filled_circle(it, tx + 5, ty + 4, 2, chip);
    fast_pixel(it, tx - 1, ty - 7, chip);
    fast_pixel(it, tx + 2, ty + 7, chip);
  } else if (k == 1) {
    fast_filled_circle(it, tx, ty + 1, 12, col_red);
    fast_circle(it, tx, ty + 1, 12, Color(255, 130, 130));
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
    Color icing(255, 110, 178);
    fast_filled_circle(it, tx, ty, 13, Color(215, 155, 78));
    fast_filled_circle(it, tx, ty, 11, icing);
    fast_circle(it, tx, ty, 13, Color(160, 105, 42));
    fast_filled_circle(it, tx, ty, 4, col_bg);
    fast_circle(it, tx, ty, 4, Color(180, 70, 125));
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
    fast_line(it, tx - 4, ty - 6, tx + 6, ty + 6, Color(255, 190, 230));
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

void render_chomp_character(Display &it, int mx, int my, int mr,
                            float look_dx, float look_dy,
                            bool mouth_open, bool is_chomping,
                            uint32_t now_ms,
                            Color col_white, Color col_black,
                            Color col_dark_gray) {
  Color fur_main(28, 148, 248);
  Color fur_dark(16, 96, 182);
  Color fur_hi(110, 198, 255);
  Color ear_inner(255, 158, 188);
  Color tongue_pink(255, 128, 166);

  int bounce_y = is_chomping ? (((now_ms / 70U) & 1U) ? -3 : 2) : 0;
  int cy_m = my + bounce_y;

  fast_filled_circle(it, mx, cy_m + 4, mr, Color(12, 16, 24));

  int ear_r = (mr * 34) / 100;
  int ear_inner_r = (mr * 19) / 100;
  int ear_dx = (mr * 74) / 100;
  int ear_dy = (mr * 68) / 100;
  fast_filled_circle(it, mx - ear_dx, cy_m - ear_dy, ear_r, fur_dark);
  fast_filled_circle(it, mx - ear_dx, cy_m - ear_dy, ear_r - 1, fur_main);
  fast_circle(it, mx - ear_dx, cy_m - ear_dy, ear_r, fur_hi);
  fast_filled_circle(it, mx - ear_dx + 1, cy_m - ear_dy + 1, ear_inner_r, ear_inner);
  fast_filled_circle(it, mx + ear_dx, cy_m - ear_dy, ear_r, fur_dark);
  fast_filled_circle(it, mx + ear_dx, cy_m - ear_dy, ear_r - 1, fur_main);
  fast_circle(it, mx + ear_dx, cy_m - ear_dy, ear_r, fur_hi);
  fast_filled_circle(it, mx + ear_dx - 1, cy_m - ear_dy + 1, ear_inner_r, ear_inner);

  int tuft_r = (mr * 28) / 100;
  fast_filled_circle(it, mx - mr + 5, cy_m - 4, tuft_r, fur_main);
  fast_filled_circle(it, mx + mr - 5, cy_m - 4, tuft_r, fur_main);
  fast_filled_circle(it, mx - mr + 7, cy_m + 10, tuft_r, fur_main);
  fast_filled_circle(it, mx + mr - 7, cy_m + 10, tuft_r, fur_main);
  fast_filled_circle(it, mx, cy_m - mr + 3, tuft_r, fur_main);

  fast_filled_circle(it, mx, cy_m, mr, fur_main);
  fast_circle(it, mx, cy_m, mr, fur_hi);
  fast_circle(it, mx, cy_m, mr - 1, fur_dark);

  int eye_dx = (mr * 30) / 100;
  int eye_y = cy_m - (mr * 34) / 100;
  int eye_r = std::max(7, (mr * 27) / 100);
  int pup_r = std::max(3, (eye_r * 48) / 100);

  fast_filled_circle(it, mx - eye_dx, eye_y, eye_r, col_white);
  fast_circle(it, mx - eye_dx, eye_y, eye_r, col_dark_gray);
  fast_filled_circle(it, mx + eye_dx, eye_y, eye_r, col_white);
  fast_circle(it, mx + eye_dx, eye_y, eye_r, col_dark_gray);

  int pdx_l = static_cast<int>(look_dx * static_cast<float>(eye_r - pup_r - 1));
  int pdy_l = static_cast<int>(look_dy * static_cast<float>(eye_r - pup_r - 1));
  int pdx_r = pdx_l;
  int pdy_r = pdy_l;
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

  int mouth_y = cy_m + (mr * 26) / 100;
  if (is_chomping) {
    bool jaw_open = ((now_ms / 85U) & 1U) != 0;
    int mr_open = jaw_open ? ((mr * 36) / 100) : ((mr * 20) / 100);
    fast_filled_circle(it, mx, mouth_y, mr_open, col_black);
    fast_filled_circle(it, mx, mouth_y + mr_open / 2, mr_open / 2, ear_inner);
    Color crumb(218, 162, 84);
    int c_off = static_cast<int>((now_ms / 60U) % 8U);
    fast_filled_circle(it, mx - mr / 2 - c_off, mouth_y - 2 + c_off / 2, 2, crumb);
    fast_filled_circle(it, mx + mr / 2 + c_off, mouth_y - 4 + c_off / 2, 2, crumb);
    fast_filled_circle(it, mx + 3, mouth_y + mr / 2 + c_off / 2, 2, crumb);
  } else if (mouth_open) {
    int m_rad = (mr * 44) / 100;
    fast_filled_circle(it, mx, mouth_y + 2, m_rad, col_black);
    fast_circle(it, mx, mouth_y + 2, m_rad, fur_dark);
    int t_rad = (m_rad * 56) / 100;
    fast_filled_circle(it, mx, mouth_y + 2 + m_rad - t_rad - 1, t_rad, tongue_pink);
  } else {
    int sw = (mr * 48) / 100;
    fast_filled_circle(it, mx, mouth_y - 2, sw, col_black);
    fill_rect_row_major(it, mx - sw, mouth_y - sw - 2, sw * 2 + 1, sw, fur_main);
    fast_hline(it, mx - sw, mouth_y - 2, sw * 2 + 1, fur_dark);
    fast_filled_circle(it, mx, mouth_y + sw / 2, sw / 3, tongue_pink);
  }
}

void render_game_chomp(Display &it, const UiFrameContext &ctx,
                       bool game_just_entered, float roll_delta, float pitch_delta,
                       float &marble_touch_x, bool &game_btn_pressed,
                       int32_t &high_score_chomp) {
  static constexpr int NUM_SNACKS = 7;
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto *snacks = game_arena().snacks;
  static float mon_x = 233.0f;
  static float mon_y = 233.0f;
  static float mon_vx = 0.0f;
  static float mon_vy = 0.0f;
  static uint32_t last_phys_ms = 0;
  static uint32_t entry_ms = 0;
  static bool post_play_tared = false;
  static int level_num = 1;
  static int snacks_left = NUM_SNACKS;
  static int snack_score = 0;
  static int next_kind = 0;
  static uint32_t chomp_timer = 0;
  static int chomp_word_idx = 0;
  static uint32_t level_clear_until_ms = 0;
  auto *confetti = game_arena().confetti;

  const int bot_wall_y = layout().back_y - scale_y(60);

  auto spawn_level_platter = [&](int lv, uint32_t seed_base) {
    snacks_left = NUM_SNACKS;
    float base_ang = static_cast<float>(lv) * 0.45f;
    for (int i = 0; i < NUM_SNACKS; i++) {
      int s = static_cast<int>(seed_base + i * 73U + lv * 37U);
      float ang = base_ang + static_cast<float>(i) * (2.0f * PI_F / static_cast<float>(NUM_SNACKS)) +
                  (hash2d_01(s, 11) - 0.5f) * 0.35f;
      float rad = scale_min_f(76.0f + (static_cast<float>((i * 3) % 4) * 26.0f) + hash2d_01(s, 29) * 18.0f);
      float tx = static_cast<float>(cx) + fast_cos(ang) * rad;
      float ty = std::min(static_cast<float>(bot_wall_y - scale_min(18)),
                          static_cast<float>(cy) + fast_sin(ang) * rad);

      float dmx = tx - mon_x;
      float dmy = ty - mon_y;
      float min_sep = scale_min_f(70.0f);
      if (dmx * dmx + dmy * dmy < min_sep * min_sep) {
        tx = static_cast<float>(cx) - dmx * 1.1f;
        ty = static_cast<float>(cy) - dmy * 1.1f;
      }

      float drift_spd = (lv <= 1) ? 0.0f : scale_min_f(std::min(68.0f, 22.0f + static_cast<float>(lv - 2) * 12.0f));
      float v_ang = hash2d_01(s, 47) * (2.0f * PI_F);
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

  float dt = (now_ms >= last_phys_ms) ? ((now_ms - last_phys_ms) * 0.001f) : 0.05f;
  last_phys_ms = now_ms;
  if (dt < 0.012f) dt = 0.012f;
  if (dt > 0.075f) dt = 0.075f;

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
        float edx = snacks[i].x - mon_x;
        float edy = snacks[i].y - mon_y;
        float ed2 = edx * edx + edy * edy;
        if (ed2 < 6724.0f && ed2 > 1.0f) {
          float ed = std::sqrt(ed2);
          float evade = (1.0f - ed / 82.0f) * std::min(110.0f, static_cast<float>(level_num - 2) * 32.0f);
          snacks[i].vx += (edx / ed) * evade * dt;
          snacks[i].vy += (edy / ed) * evade * dt;
        }
      }

      float max_t_spd = std::min(78.0f, 24.0f + static_cast<float>(level_num - 2) * 14.0f);
      float cur_spd = std::sqrt(snacks[i].vx * snacks[i].vx + snacks[i].vy * snacks[i].vy);
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
      float bdx = snacks[i].x - static_cast<float>(cx);
      float bdy = snacks[i].y - static_cast<float>(cy);
      float bd = std::sqrt(bdx * bdx + bdy * bdy);
      if (bd > snack_rim_r && bd > 0.1f) {
        float nx = bdx / bd;
        float ny = bdy / bd;
        snacks[i].x = static_cast<float>(cx) + nx * snack_rim_r;
        snacks[i].y = static_cast<float>(cy) + ny * snack_rim_r;
        float v_dot = snacks[i].vx * nx + snacks[i].vy * ny;
        if (v_dot > 0.0f) {
          snacks[i].vx -= 1.9f * v_dot * nx;
          snacks[i].vy -= 1.9f * v_dot * ny;
        }
      }
    }
  }

  float nearest_dist = 999.0f;
  float look_dx = 0.0f;
  float look_dy = 0.0f;

  for (int i = 0; i < NUM_SNACKS; i++) {
    if (snacks[i].eaten) continue;
    float dx = snacks[i].x - mon_x;
    float dy = snacks[i].y - mon_y;
    float d = std::sqrt(dx * dx + dy * dy);
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

  float ax = std::clamp(roll_delta * 32.0f, -620.0f, 620.0f);
  float ay = std::clamp(-pitch_delta * 32.0f, -620.0f, 620.0f);

  float pull_x = 0.0f;
  float pull_y = 0.0f;
  if (!in_celebration && nearest_dist < 38.0f) {
    float pull_mag = (1.0f - nearest_dist / 38.0f) * 110.0f;
    pull_x = look_dx * pull_mag;
    pull_y = look_dy * pull_mag;
  }

  float damping = std::pow(0.925f, dt / 0.05f);
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
  float mdx = mon_x - static_cast<float>(cx);
  float mdy = mon_y - static_cast<float>(cy);
  float mdist = std::sqrt(mdx * mdx + mdy * mdy);
  const float max_mon_r = scale_min_f(172.0f);
  if (mdist > max_mon_r && mdist > 0.001f) {
    float nx = mdx / mdist;
    float ny = mdy / mdist;
    mon_x = static_cast<float>(cx) + nx * max_mon_r;
    mon_y = static_cast<float>(cy) + ny * max_mon_r;
    float v_dot = mon_vx * nx + mon_vy * ny;
    if (v_dot > 0.0f) {
      mon_vx -= 1.6f * v_dot * nx;
      mon_vy -= 1.6f * v_dot * ny;
    }
  }

  const float chomp_cap_r = scale_min_f(36.0f);
  if (!in_celebration) {
    for (int i = 0; i < NUM_SNACKS; i++) {
      if (snacks[i].eaten) continue;
      float dx = snacks[i].x - mon_x;
      float dy = snacks[i].y - mon_y;
      float dist = std::sqrt(dx * dx + dy * dy);

      if (dist < chomp_cap_r) {
        snacks[i].eaten = true;
        snacks_left--;
        snack_score++;
        chomp_timer = now_ms;
        chomp_word_idx = (chomp_word_idx + 1) % 5;
        if (snack_score > high_score_chomp) {
          high_score_chomp = snack_score;
        }

        if (snacks_left <= 0) {
          snack_score += 3;
          if (snack_score > high_score_chomp) {
            high_score_chomp = snack_score;
          }
          save_persisted_prefs();
          level_clear_until_ms = now_ms + 3000U;
          in_celebration = true;
          pending_game_sfx() = 5;

          Color p_cols[5] = {ctx.col_gold, ctx.col_cyan, ctx.col_magenta, ctx.col_green, ctx.col_orange};
          for (int c = 0; c < 16; c++) {
            float ang = static_cast<float>(c) * (2.0f * PI_F / 16.0f);
            float spd = 105.0f + static_cast<float>((c % 3) * 34);
            confetti[c].x = mon_x + fast_cos(ang) * 20.0f;
            confetti[c].y = mon_y + fast_sin(ang) * 20.0f;
            confetti[c].vx = fast_cos(ang) * spd;
            confetti[c].vy = fast_sin(ang) * spd - 40.0f;
            const Color pc = p_cols[c % 5];
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

  bool is_chomping = in_celebration || (chomp_timer > 0U && (now_ms - chomp_timer < 520U));
  bool mouth_open = (!in_celebration && nearest_dist < 82.0f);

  fast_circle(it, cx, cy, scale_min(208), ctx.col_dark_gray);
  fast_line(it, cx - scale_min(151), bot_wall_y, cx + scale_min(151), bot_wall_y, ctx.col_dark_gray);
  char t_buf[56];
  std::snprintf(t_buf, sizeof(t_buf), "Lv %d  •  Left:%d/7  •  Score:%d  •  Best:%d",
                level_num, snacks_left, snack_score, static_cast<int>(high_score_chomp));
  fast_print(it, cx, layout().back_y - scale_y(34), ctx.font_small, ctx.col_gold, TextAlign::CENTER, t_buf);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "Exit");

  float t_bob = static_cast<float>(now_ms) * 0.0045f;
  for (int i = 0; i < NUM_SNACKS; i++) {
    if (snacks[i].eaten) continue;
    int bob_y = static_cast<int>(fast_sin(t_bob + static_cast<float>(i) * 1.15f) * 2.5f);
    render_chomp_snack(it, static_cast<int>(snacks[i].x), static_cast<int>(snacks[i].y) + bob_y,
                       snacks[i].kind, ctx.col_white, ctx.col_red, ctx.col_gold, ctx.col_cyan,
                       ctx.col_green, ctx.col_magenta, ctx.col_bg);
  }

  int imx = static_cast<int>(mon_x);
  int imy = static_cast<int>(mon_y);
  render_chomp_character(it, imx, imy, mon_r, look_dx, look_dy, mouth_open, is_chomping,
                         now_ms, ctx.col_white, ctx.col_black, ctx.col_dark_gray);

  if (in_celebration) {
    uint32_t rem_cel = level_clear_until_ms - now_ms;
    char p_msg[36];
    if (rem_cel > 1900U) {
      for (int c = 0; c < 16; c++) {
        fast_filled_circle(it, static_cast<int>(confetti[c].x), static_cast<int>(confetti[c].y), scale_min(3),
                           Color(confetti[c].r, confetti[c].g, confetti[c].b));
      }
      std::snprintf(p_msg, sizeof(p_msg), "LEVEL %d CLEAR!", level_num);
      fast_print(it, cx, imy - scale_min(62), ctx.font_date, ctx.col_gold, TextAlign::CENTER, p_msg);
    } else {
      std::snprintf(p_msg, sizeof(p_msg), "GET READY! LEVEL %d", level_num + 1);
      fast_print(it, cx, imy - scale_min(62), ctx.font_date, ctx.col_cyan, TextAlign::CENTER, p_msg);
    }
  } else if (is_chomping) {
    static constexpr const char *CHOMP_WORDS[5] = {
        "NOM NOM!", "YUMMY!", "COOKIE!", "CHOMP!", "DELICIOUS!"};
    int pop_y = (imy > scale_y(150)) ? (imy - scale_min(52)) : (imy + scale_min(52));
    int pop_x = std::clamp(imx, scale_x(115), SCREEN_W - scale_x(115));
    fast_print(it, pop_x, pop_y, ctx.font_date, ctx.col_gold, TextAlign::CENTER, CHOMP_WORDS[chomp_word_idx]);
    fast_filled_circle(it, imx - scale_min(42), imy - scale_min(16), scale_min(3), ctx.col_gold);
    fast_filled_circle(it, imx + scale_min(42), imy - scale_min(16), scale_min(3), ctx.col_cyan);
  }
}

void render_game_2048(Display &it, const UiFrameContext &ctx,
                      bool game_just_entered, float &marble_touch_x, float &marble_touch_y,
                      bool &game_btn_pressed, int32_t &high_score_2048) {
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto &grid = game_arena().grid_2048;
  static int score_2048 = 0;
  static bool game_over = false;
  static bool won_2048 = false;
  static uint32_t rng_state = 0x2048CAFEU;

  auto next_rng = [&]() -> uint32_t {
    rng_state ^= (rng_state << 13) ^ (now_ms & 0xFFFFU);
    rng_state ^= (rng_state >> 17);
    rng_state ^= (rng_state << 5);
    return rng_state;
  };

  auto spawn_tile = [&]() {
    int empty_r[16], empty_c[16], n_empty = 0;
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        if (grid[r][c] == 0) {
          empty_r[n_empty] = r;
          empty_c[n_empty] = c;
          n_empty++;
        }
      }
    }
    if (n_empty > 0) {
      int pick = static_cast<int>(next_rng() % static_cast<uint32_t>(n_empty));
      grid[empty_r[pick]][empty_c[pick]] = ((next_rng() % 10U) == 0U) ? 4 : 2;
    }
  };

  auto has_moves = [&]() -> bool {
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        if (grid[r][c] == 0) return true;
        if (r < 3 && grid[r][c] == grid[r + 1][c]) return true;
        if (c < 3 && grid[r][c] == grid[r][c + 1]) return true;
      }
    }
    return false;
  };

  auto reset_board = [&]() {
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) grid[r][c] = 0;
    }
    score_2048 = 0;
    game_over = false;
    won_2048 = false;
    game_2048_pending_dir() = 0;
    spawn_tile();
    spawn_tile();
  };

  if (game_just_entered) {
    reset_board();
  }

  const int tile_sz = (SCREEN_W < 380) ? 50 : scale_s(62);
  const int gap = (SCREEN_W < 380) ? 5 : scale_s(6);
  const int board_sz = 4 * tile_sz + 5 * gap;
  const int board_x = cx - board_sz / 2;
  const int board_y = cy - scale_y(20) - board_sz / 2;

  int move_dir = game_2048_pending_dir();
  game_2048_pending_dir() = 0;

  if (marble_touch_x >= 0.0f || game_btn_pressed) {
    float tx = marble_touch_x;
    float ty = marble_touch_y;
    marble_touch_x = -1.0f;
    game_btn_pressed = false;
    if (game_over) {
      reset_board();
    } else if (move_dir == 0 && tx >= 0.0f && ty < static_cast<float>(layout().back_y - scale_y(36))) {
      float dx = tx - static_cast<float>(cx);
      float dy = ty - static_cast<float>(board_y + board_sz / 2);
      if (std::fabs(dx) > 18.0f || std::fabs(dy) > 18.0f) {
        if (std::fabs(dx) > std::fabs(dy)) {
          move_dir = (dx > 0.0f) ? 2 : 4;
        } else {
          move_dir = (dy > 0.0f) ? 3 : 1;
        }
      }
    }
  }

  if (move_dir >= 1 && move_dir <= 4 && !game_over) {
    bool moved = false;
    bool merged_any = false;
    bool just_won_2048 = false;
    for (int i = 0; i < 4; i++) {
      int line[4] = {0, 0, 0, 0};
      int wp = 0;
      for (int j = 0; j < 4; j++) {
        int r = (move_dir == 1) ? j : (move_dir == 3) ? (3 - j) : i;
        int c = (move_dir == 4) ? j : (move_dir == 2) ? (3 - j) : i;
        if (grid[r][c] != 0) line[wp++] = grid[r][c];
      }
      int out[4] = {0, 0, 0, 0};
      int op = 0;
      for (int j = 0; j < wp; j++) {
        if (j + 1 < wp && line[j] == line[j + 1]) {
          int nv = line[j] * 2;
          out[op++] = nv;
          score_2048 += nv;
          if (nv >= 2048 && !won_2048) {
            won_2048 = true;
            just_won_2048 = true;
          }
          merged_any = true;
          j++;
        } else {
          out[op++] = line[j];
        }
      }
      for (int j = 0; j < 4; j++) {
        int r = (move_dir == 1) ? j : (move_dir == 3) ? (3 - j) : i;
        int c = (move_dir == 4) ? j : (move_dir == 2) ? (3 - j) : i;
        if (grid[r][c] != out[j]) {
          moved = true;
          grid[r][c] = out[j];
        }
      }
    }
    if (moved) {
      bool new_hs = false;
      if (score_2048 > high_score_2048) {
        high_score_2048 = score_2048;
        new_hs = true;
      }
      spawn_tile();
      if (!has_moves()) {
        game_over = true;
        if (new_hs) save_persisted_prefs();
        pending_game_sfx() = 14;
      } else {
        if (just_won_2048 && new_hs) save_persisted_prefs();
        pending_game_sfx() = just_won_2048 ? 5 : (merged_any ? 10 : 11);
      }
    }
  }

  render_card_box(it, board_x, board_y, board_sz, board_sz,
                  Color(18, 22, 30), ctx.col_slate * 0.45f, scale_s(14));

  auto tile_bg_color = [&](int val) -> Color {
    if (val == 0) return Color(28, 34, 44);
    if (val == 2) return Color(52, 64, 82);
    if (val == 4) return Color(68, 86, 112);
    if (val == 8) return ctx.col_orange * 0.85f;
    if (val == 16) return ctx.col_orange;
    if (val == 32) return ctx.col_red * 0.88f;
    if (val == 64) return ctx.col_red;
    if (val == 128) return ctx.col_cyan * 0.78f;
    if (val == 256) return ctx.col_cyan;
    if (val == 512) return ctx.col_green * 0.85f;
    if (val == 1024) return ctx.col_magenta;
    return ctx.col_gold;
  };

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int tx = board_x + gap + c * (tile_sz + gap);
      int ty = board_y + gap + r * (tile_sz + gap);
      int val = grid[r][c];
      Color tc = tile_bg_color(val);
      Color bc = (val >= 2048) ? ctx.col_white : (val > 0 ? tc * 1.25f : Color(36, 44, 56));
      render_card_box(it, tx, ty, tile_sz, tile_sz, tc, bc, scale_s(8));
      if (val > 0) {
        char v_str[12];
        std::snprintf(v_str, sizeof(v_str), "%d", val);
        Color fg = (val >= 2048) ? ctx.col_bg : ctx.col_white;
        BaseFont *tf = (val < 100) ? ctx.font_date : ctx.font_small;
        fast_print(it, tx + tile_sz / 2, ty + tile_sz / 2, tf, fg, TextAlign::CENTER, v_str);
      }
    }
  }

  char hud_buf[64];
  std::snprintf(hud_buf, sizeof(hud_buf), "2048  •  Score: %d  •  Best: %d",
                score_2048, static_cast<int>(high_score_2048));
  fast_print(it, cx, layout().back_y - scale_y(34), ctx.font_small, ctx.col_gold, TextAlign::CENTER, hud_buf);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "Exit");

  if (game_over) {
    char sc_str[32];
    std::snprintf(sc_str, sizeof(sc_str), "Score: %d", score_2048);
    char best_str[32];
    std::snprintf(best_str, sizeof(best_str), "Best: %d", static_cast<int>(high_score_2048));
    ui::game_over_sheet(it, cx, cy,
                        won_2048 ? "2048 MASTER!" : "NO MOVES LEFT",
                        won_2048 ? ctx.col_gold : ctx.col_red,
                        sc_str, best_str, won_2048 || (score_2048 > 0 && score_2048 >= high_score_2048),
                        "PLAY AGAIN", ctx.font_date, ctx.font_small,
                        ctx.theme_primary, ctx.col_white, ctx.col_gold, ctx.col_slate, WidgetId::GAME_OVER_CTA);
  }
}

void render_game_memory(Display &it, const UiFrameContext &ctx,
                        bool game_just_entered, float &marble_touch_x, float &marble_touch_y,
                        bool &game_btn_pressed, int32_t &high_score_memory) {
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto *cards = game_arena().mem_cards;
  auto *matched = game_arena().mem_matched;
  static int first_pick = -1;
  static int second_pick = -1;
  static uint32_t mismatch_until_ms = 0;
  static int mem_score = 0;
  static int round_num = 1;
  static int streak_mult = 1;
  static int pairs_found = 0;
  static uint32_t rng_seed = 0xA55A2026U;

  auto shuffle_deck = [&]() {
    for (int i = 0; i < 16; i++) {
      cards[i] = i / 2;
      matched[i] = false;
    }
    for (int i = 15; i > 0; i--) {
      rng_seed ^= (rng_seed << 13) ^ (now_ms & 0xFFFFU);
      rng_seed ^= (rng_seed >> 17);
      rng_seed ^= (rng_seed << 5);
      int j = static_cast<int>(rng_seed % static_cast<uint32_t>(i + 1));
      int tmp = cards[i];
      cards[i] = cards[j];
      cards[j] = tmp;
    }
    first_pick = -1;
    second_pick = -1;
    mismatch_until_ms = 0;
    pairs_found = 0;
    streak_mult = 1;
  };

  if (game_just_entered) {
    mem_score = 0;
    round_num = 1;
    shuffle_deck();
  }

  if (mismatch_until_ms > 0U && now_ms >= mismatch_until_ms) {
    first_pick = -1;
    second_pick = -1;
    mismatch_until_ms = 0U;
  }

  const int card_sz = (SCREEN_W < 380) ? 50 : scale_s(62);
  const int gap = (SCREEN_W < 380) ? 5 : scale_s(6);
  const int board_sz = 4 * card_sz + 5 * gap;
  const int board_x = cx - board_sz / 2;
  const int board_y = cy - scale_y(20) - board_sz / 2;

  if (marble_touch_x >= 0.0f || game_btn_pressed) {
    int tx = static_cast<int>(marble_touch_x);
    int ty = static_cast<int>(marble_touch_y);
    marble_touch_x = -1.0f;
    game_btn_pressed = false;

    if (mismatch_until_ms > 0U) {
      first_pick = -1;
      second_pick = -1;
      mismatch_until_ms = 0U;
    }

    for (int idx = 0; idx < 16; idx++) {
      int r = idx / 4;
      int c = idx % 4;
      int cx_card = board_x + gap + c * (card_sz + gap);
      int cy_card = board_y + gap + r * (card_sz + gap);
      if (tx >= cx_card && tx <= cx_card + card_sz && ty >= cy_card && ty <= cy_card + card_sz) {
        if (!matched[idx] && idx != first_pick && second_pick == -1) {
          if (first_pick == -1) {
            first_pick = idx;
            pending_game_sfx() = 11;
          } else {
            second_pick = idx;
            if (cards[first_pick] == cards[second_pick]) {
              matched[first_pick] = true;
              matched[second_pick] = true;
              first_pick = -1;
              second_pick = -1;
              pairs_found++;
              mem_score += 120 * streak_mult;
              streak_mult = std::min(4, streak_mult + 1);
              if (mem_score > high_score_memory) {
                high_score_memory = mem_score;
              }
              if (pairs_found >= 8) {
                mem_score += round_num * 250;
                if (mem_score > high_score_memory) {
                  high_score_memory = mem_score;
                }
                save_persisted_prefs();
                round_num++;
                pending_game_sfx() = 5;
                shuffle_deck();
              } else {
                pending_game_sfx() = 9;
              }
            } else {
              streak_mult = 1;
              mismatch_until_ms = now_ms + 620U;
              pending_game_sfx() = 13;
            }
          }
        }
        break;
      }
    }
  }

  auto draw_card_symbol = [&](int sym, int scx, int scy, int r_sym) {
    if (sym == 0) {
      fast_filled_circle(it, scx, scy, r_sym / 2, ctx.col_gold);
      for (int a = 0; a < 8; a++) {
        float ang = static_cast<float>(a) * 0.785398f;
        fast_line(it, scx, scy, scx + static_cast<int>(fast_cos(ang) * r_sym),
                  scy + static_cast<int>(fast_sin(ang) * r_sym), ctx.col_gold);
      }
    } else if (sym == 1) {
      int hr = std::max(4, r_sym / 2);
      fast_filled_circle(it, scx - hr + 1, scy - hr / 2, hr, ctx.col_red);
      fast_filled_circle(it, scx + hr - 1, scy - hr / 2, hr, ctx.col_red);
      fast_filled_triangle(it, scx - r_sym, scy - 1, scx + r_sym, scy - 1, scx, scy + r_sym, ctx.col_red);
    } else if (sym == 2) {
      fast_filled_triangle(it, scx, scy - r_sym, scx - r_sym, scy, scx + r_sym, scy, ctx.col_cyan);
      fast_filled_triangle(it, scx, scy + r_sym, scx - r_sym, scy, scx + r_sym, scy, ctx.col_cyan * 0.8f);
    } else if (sym == 3) {
      int bw = std::max(4, r_sym / 2);
      fill_rect_row_major(it, scx - bw, scy - r_sym, bw * 2, r_sym * 2, ctx.col_green);
      fill_rect_row_major(it, scx - r_sym, scy - bw, r_sym * 2, bw * 2, ctx.col_green);
    } else if (sym == 4) {
      fill_rect_row_major(it, scx - r_sym, scy + r_sym / 3, r_sym * 2, r_sym / 2, ctx.col_magenta);
      fast_filled_triangle(it, scx - r_sym, scy + r_sym / 3, scx - r_sym, scy - r_sym / 2,
                           scx - r_sym / 3, scy + r_sym / 3, ctx.col_magenta);
      fast_filled_triangle(it, scx - r_sym / 2, scy + r_sym / 3, scx, scy - r_sym,
                           scx + r_sym / 2, scy + r_sym / 3, ctx.col_magenta);
      fast_filled_triangle(it, scx + r_sym / 3, scy + r_sym / 3, scx + r_sym, scy - r_sym / 2,
                           scx + r_sym, scy + r_sym / 3, ctx.col_magenta);
    } else if (sym == 5) {
      fast_filled_triangle(it, scx + 3, scy - r_sym, scx - r_sym + 2, scy + 2, scx + 2, scy + 2, ctx.col_orange);
      fast_filled_triangle(it, scx - 2, scy - 2, scx + r_sym - 2, scy - 2, scx - 3, scy + r_sym, ctx.col_orange);
    } else if (sym == 6) {
      fast_filled_circle(it, scx, scy, r_sym, ctx.col_white);
      fast_filled_circle(it, scx, scy, (r_sym * 2) / 3, ctx.col_bg);
      fast_filled_circle(it, scx, scy, r_sym / 3, ctx.theme_primary);
    } else {
      fill_rect_row_major(it, scx - r_sym + 2, scy - r_sym + 2, (r_sym - 2) * 2, r_sym, ctx.theme_secondary);
      fast_filled_triangle(it, scx - r_sym + 2, scy + 2, scx + r_sym - 2, scy + 2,
                           scx, scy + r_sym, ctx.theme_secondary);
      fast_filled_circle(it, scx, scy, std::max(2, r_sym / 3), ctx.col_gold);
    }
  };

  render_card_box(it, board_x, board_y, board_sz, board_sz,
                  Color(18, 22, 30), ctx.col_slate * 0.45f, scale_s(14));

  for (int idx = 0; idx < 16; idx++) {
    int r = idx / 4;
    int c = idx % 4;
    int tx = board_x + gap + c * (card_sz + gap);
    int ty = board_y + gap + r * (card_sz + gap);
    bool is_open = matched[idx] || (idx == first_pick) || (idx == second_pick);
    Color bg_c = matched[idx] ? Color(24, 38, 34)
               : (is_open ? Color(36, 44, 58) : Color(28, 34, 46));
    Color brd_c = matched[idx] ? ctx.col_green * 0.7f
                : (is_open ? ctx.col_gold : ctx.theme_primary * 0.55f);
    render_card_box(it, tx, ty, card_sz, card_sz, bg_c, brd_c, scale_s(8));
    if (!matched[idx]) {
      frame_hits().add_rect(tx, ty, card_sz, card_sz, WidgetId::GRID_CARD_BASE,
                            kHitTap | kHitFocusSlot, static_cast<int16_t>(idx));
    }
    int scx = tx + card_sz / 2;
    int scy = ty + card_sz / 2;
    if (is_open) {
      draw_card_symbol(cards[idx], scx, scy, std::max(8, card_sz / 3 - 2));
    } else {
      fast_filled_circle(it, scx, scy, std::max(3, card_sz / 8), ctx.theme_primary * 0.55f);
    }
  }

  char hud_buf[64];
  std::snprintf(hud_buf, sizeof(hud_buf), "R%d  •  Pairs: %d/8  •  %dx  •  %d",
                round_num, pairs_found, streak_mult, mem_score);
  fast_print(it, cx, layout().back_y - scale_y(34), ctx.font_small, ctx.col_gold, TextAlign::CENTER, hud_buf);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "Exit");
}

void render_games_launcher(Display &it, const UiFrameContext &ctx,
                           int &game_carousel_idx, bool menu_scroll_active,
                           int menu_scroll_dir, int menu_scroll_target_page) {
  const int cx = ctx.cx;
  const auto &prefs = persisted_prefs();
  const bool game_sound_on = ctx.game_sound_enabled;

  if (game_carousel_idx < 0 || game_carousel_idx >= 6) {
    game_carousel_idx = 0;
  }

  auto draw_game_icon = [&](int g_idx, int icon_x, int icon_y) {
    if (g_idx == 0) {
      fast_filled_circle(it, icon_x, icon_y, 52, Color(18, 24, 28));
      fast_circle(it, icon_x, icon_y, 52, ctx.col_slate);
      fast_circle(it, icon_x, icon_y, 51, ctx.col_slate);
      fast_circle(it, icon_x, icon_y, 46, Color(30, 40, 48));

      fast_filled_circle(it, icon_x, icon_y, 15, ctx.col_black);
      fast_circle(it, icon_x, icon_y, 15, Color(45, 55, 65));
      fast_circle(it, icon_x, icon_y, 14, Color(45, 55, 65));

      for (int a = 0; a < 4; a++) {
        float sa = static_cast<float>(a) * 1.570796f;
        fast_line(it, icon_x + static_cast<int>(fast_cos(sa) * 18.0f),
                  icon_y + static_cast<int>(fast_sin(sa) * 18.0f),
                  icon_x + static_cast<int>(fast_cos(sa + 0.8f) * 40.0f),
                  icon_y + static_cast<int>(fast_sin(sa + 0.8f) * 40.0f),
                  Color(35, 45, 55));
      }
      render_glass_marble(it, icon_x - 24, icon_y - 18, 13, ctx.col_red, Color(255, 138, 128));
      render_glass_marble(it, icon_x + 22, icon_y - 22, 12, ctx.col_cyan, Color(180, 255, 255));
      render_glass_marble(it, icon_x - 20, icon_y + 22, 11, ctx.col_green, Color(185, 246, 202));
      render_glass_marble(it, icon_x + 24, icon_y + 18, 13, ctx.col_gold, Color(255, 245, 157));
      render_glass_marble(it, icon_x + 2, icon_y - 31, 10, ctx.col_magenta, Color(248, 187, 208));

    } else if (g_idx == 1) {
      render_archery_target(it, icon_x, icon_y, 46, ctx.col_white, ctx.col_black,
                            ctx.col_cyan, ctx.col_red, ctx.col_gold, ctx.col_slate, ctx.col_dark_gray);
      int tip_x = icon_x + 2;
      int tip_y = icon_y + 2;
      int tail_x = icon_x + 56;
      int tail_y = icon_y - 36;
      fast_line(it, tip_x, tip_y + 5, tail_x - 8, tail_y + 10, Color(15, 18, 24));
      fast_line(it, tip_x, tip_y + 6, tail_x - 8, tail_y + 11, Color(15, 18, 24));
      fast_circle(it, tip_x, tip_y, 5, Color(180, 110, 0));
      fast_filled_triangle(it, tip_x - 3, tip_y + 2, tip_x + 9, tip_y - 9, tip_x + 12, tip_y - 2, ctx.col_white);
      fast_line(it, tip_x, tip_y, tail_x, tail_y, Color(238, 206, 148));
      fast_line(it, tip_x, tip_y - 1, tail_x, tail_y - 1, Color(250, 228, 178));
      fast_line(it, tip_x, tip_y + 1, tail_x, tail_y + 1, Color(182, 138, 76));
      for (int f = 0; f < 9; f++) {
        int fx = tail_x - 3 - f;
        int fy = tail_y + 2 + (f * 2) / 3;
        fast_line(it, fx, fy - 1, fx + 3, fy - 11, Color(255, 52, 52));
        fast_line(it, fx, fy + 1, fx + 10, fy + 4, Color(220, 32, 32));
      }
      fast_filled_circle(it, tail_x + 1, tail_y - 1, 2, ctx.col_white);

    } else if (g_idx == 2) {
      int b_w = 26, b_h = 11, gap_x = 4, gap_y = 5;
      int start_bx = icon_x - static_cast<int>(2.0f * b_w + 1.5f * gap_x) + b_w / 2;
      int start_by = icon_y - 36;
      Color row_c[3] = {ctx.col_gold, ctx.col_cyan, ctx.col_orange};
      Color row_hi[3] = {Color(255, 245, 165), Color(185, 255, 255), Color(255, 195, 100)};
      for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
          int bx = start_bx + c * (b_w + gap_x);
          int by = start_by + r * (b_h + gap_y);
          if (r == 2 && c == 2) {
            fill_rect_row_major(it, bx - 10, by - 5, 5, 4, row_c[r]);
            fill_rect_row_major(it, bx + 4, by - 7, 5, 4, row_hi[r]);
            fill_rect_row_major(it, bx - 6, by + 3, 4, 4, row_c[r]);
            fill_rect_row_major(it, bx + 5, by + 2, 5, 4, row_hi[r]);
            fast_filled_circle(it, bx, by, 2, ctx.col_white);
            continue;
          }
          int hp = (r == 0 && (c == 1 || c == 2)) ? 2 : 1;
          Color bc = (hp == 2) ? Color(145, 162, 182) : row_c[r];
          Color hc = (hp == 2) ? Color(230, 240, 252) : row_hi[r];
          render_brick_tile(it, bx, by, b_w, b_h, bc, hc, hp, hp, ctx.col_white);
        }
      }
      int pad_x = icon_x - 6, pad_y = icon_y + 42, pad_w = 44, pad_h = 8;
      render_brick_paddle(it, pad_x, pad_y, pad_w, pad_h, ctx.theme_primary, ctx.col_white);

      int bx = icon_x + 10, by = icon_y + 7;
      fast_filled_circle(it, pad_x + 3, pad_y - 7, 2, Color(50, 40, 14));
      fast_filled_circle(it, pad_x + 6, pad_y - 14, 2, Color(85, 68, 20));
      fast_filled_circle(it, pad_x + 9, pad_y - 21, 3, Color(130, 102, 28));
      fast_filled_circle(it, bx - 3, by + 7, 4, Color(185, 145, 35));
      fast_filled_circle(it, bx, by, 5, ctx.col_gold);
      fast_circle(it, bx, by, 5, ctx.col_white);
      fast_filled_circle(it, bx - 2, by - 2, 1, ctx.col_white);

    } else if (g_idx == 3) {
      render_chomp_snack(it, icon_x + 30, icon_y - 22, 0,
                         ctx.col_white, ctx.col_red, ctx.col_gold, ctx.col_cyan,
                         ctx.col_green, ctx.col_magenta, ctx.col_bg);
      render_chomp_snack(it, icon_x + 38, icon_y + 18, 2,
                         ctx.col_white, ctx.col_red, ctx.col_gold, ctx.col_cyan,
                         ctx.col_green, ctx.col_magenta, ctx.col_bg);
      render_chomp_snack(it, icon_x - 34, icon_y - 26, 1,
                         ctx.col_white, ctx.col_red, ctx.col_gold, ctx.col_cyan,
                         ctx.col_green, ctx.col_magenta, ctx.col_bg);
      render_chomp_character(it, icon_x - 8, icon_y + 6, 32, 0.85f, -0.25f,
                             true, false, 0U, ctx.col_white, ctx.col_black, ctx.col_dark_gray);

    } else if (g_idx == 4) {
      render_card_box(it, icon_x - 48, icon_y - 48, 96, 96, Color(18, 22, 30), ctx.col_slate, 12);
      render_card_box(it, icon_x - 42, icon_y - 42, 40, 40, Color(52, 64, 82), ctx.col_slate, 7);
      fast_print(it, icon_x - 22, icon_y - 22, ctx.font_small, ctx.col_white, TextAlign::CENTER, "2");
      render_card_box(it, icon_x + 2, icon_y - 42, 40, 40, ctx.col_orange, ctx.col_white, 7);
      fast_print(it, icon_x + 22, icon_y - 22, ctx.font_small, ctx.col_white, TextAlign::CENTER, "16");
      render_card_box(it, icon_x - 42, icon_y + 2, 40, 40, ctx.col_red, ctx.col_white, 7);
      fast_print(it, icon_x - 22, icon_y + 22, ctx.font_small, ctx.col_white, TextAlign::CENTER, "64");
      render_card_box(it, icon_x + 2, icon_y + 2, 40, 40, ctx.col_gold, ctx.col_white, 7);
      fast_print(it, icon_x + 22, icon_y + 22, ctx.font_small, ctx.col_bg, TextAlign::CENTER, "2K");

    } else {
      render_card_box(it, icon_x - 44, icon_y - 44, 40, 40, Color(36, 44, 58), ctx.col_gold, 8);
      fast_filled_circle(it, icon_x - 24, icon_y - 24, 9, ctx.col_gold);
      render_card_box(it, icon_x + 4, icon_y - 44, 40, 40, Color(28, 34, 46), ctx.theme_primary, 8);
      fast_filled_circle(it, icon_x + 24, icon_y - 24, 5, ctx.theme_primary);
      render_card_box(it, icon_x - 44, icon_y + 4, 40, 40, Color(28, 34, 46), ctx.theme_primary, 8);
      fast_filled_circle(it, icon_x - 24, icon_y + 24, 5, ctx.theme_primary);
      render_card_box(it, icon_x + 4, icon_y + 4, 40, 40, Color(36, 44, 58), ctx.col_gold, 8);
      fast_filled_circle(it, icon_x + 24, icon_y + 24, 9, ctx.col_gold);
    }
  };

  auto draw_game_card = [&](int g_idx, int icon_x, int icon_y, int text_center_x, int title_y, int score_y) {
    const char *title = "MARBLES";
    int score = prefs.high_score_marbles;
    if (g_idx == 1) {
      title = "ARCHERY";
      score = prefs.high_score_archery;
    } else if (g_idx == 2) {
      title = "BRICKS";
      score = prefs.high_score_bricks;
    } else if (g_idx == 3) {
      title = "CHOMP";
      score = prefs.high_score_chomp;
    } else if (g_idx == 4) {
      title = "2048";
      score = prefs.high_score_2048;
    } else if (g_idx >= 5) {
      title = "MEMORY MATCH";
      score = prefs.high_score_memory;
    }

    draw_game_icon(g_idx, icon_x, icon_y);
    fast_print(it, text_center_x, title_y, ctx.font_btn_bold, ctx.col_white, TextAlign::CENTER, title);

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
    const int gap_px = scale_x(8);
    int start_x = text_center_x - (w_lbl + gap_px + w_val) / 2;

    fast_print(it, start_x, score_y, ctx.font_small, ctx.col_gold, TextAlign::CENTER_LEFT, lbl_str);
    fast_print(it, start_x + w_lbl + gap_px, score_y, ctx.font_small, ctx.col_white, TextAlign::CENTER_LEFT, val_str);
  };

  const bool compact_w = (SCREEN_W < 380);
  const int icon_cx = compact_w ? cx : (cx - scale_x(118));
  const int card_cy = compact_w ? 158 : scale_y(194);
  const int text_cx = compact_w ? cx : (cx + scale_x(57));
  const int base_title_y = compact_w ? 242 : scale_y(176);
  const int base_score_y = compact_w ? 274 : scale_y(216);
  const int pips_y = compact_w ? 314 : scale_y(282);
  const int btn_row_y = layout().app_dock_y;

  if (menu_scroll_active) {
    int offset = menu_scroll_shift(scale_x(252));
    int full_span = scale_x(252);
    int out_dx = -menu_scroll_dir * offset;
    int in_dx = menu_scroll_dir * (full_span - offset);

    draw_game_card(game_carousel_idx, icon_cx + out_dx, card_cy, text_cx + out_dx, base_title_y, base_score_y);
    draw_game_card(menu_scroll_target_page, icon_cx + in_dx, card_cy, text_cx + in_dx, base_title_y, base_score_y);
  } else {
    draw_game_card(game_carousel_idx, icon_cx, card_cy, text_cx, base_title_y, base_score_y);

    // Co-register carousel flank tap zones + center hero card tap zone
    const int flank_w = scale_x(72);
    const int hero_top = scale_y(112);
    const int hero_h = std::max(80, pips_y - hero_top - scale_y(8));
    frame_hits().add_rect(0, hero_top, flank_w, hero_h, WidgetId::GAMES_PREV_BTN, kHitTap, -1);
    frame_hits().add_rect(SCREEN_W - flank_w, hero_top, flank_w, hero_h, WidgetId::GAMES_NEXT_BTN, kHitTap, 1);
    frame_hits().add_rect(flank_w, hero_top, SCREEN_W - 2 * flank_w, hero_h,
                          WidgetId::GAMES_HERO_CARD, kHitTap, static_cast<int16_t>(game_carousel_idx));
  }

  fast_print(it, cx, compact_w ? 64 : scale_y(84), ctx.font_games_title, ctx.theme_primary, TextAlign::CENTER, "GAMES");

  int active_pip = menu_scroll_active ? menu_scroll_target_page : game_carousel_idx;
  render_dock_page_pips(it, cx, pips_y, 6, active_pip, ctx.col_white, kChromePipInactive);

  ui::pill_button(it, cx - scale_x(19), cx + scale_x(91), btn_row_y, scale_s(22),
                  ctx.theme_primary, kChromeGlassTint, "PLAY", ctx.col_white, ctx.font_btn_bold,
                  WidgetId::GAMES_PLAY_BTN, kHitTap | kHitFocusSlot, 0);

  const int spk_x = cx - scale_x(91);
  const int spk_y = btn_row_y;
  const int spk_r = scale_s(22);
  render_glass_circle(it, spk_x, spk_y, spk_r, kChromeGlassTint,
                      game_sound_on ? ctx.theme_secondary : kChromeCardBorder);
  frame_hits().add_circle(spk_x, spk_y, spk_r + 6, WidgetId::GAMES_SOUND_BTN, kHitTap | kHitFocusCircle, 1);
  Color spk_c = game_sound_on ? ctx.col_white : ctx.col_slate;
  ui::draw_icon_speaker(it, spk_x, spk_y, spk_c, game_sound_on);
  if (!game_sound_on) {
    const int sl = scale_s(10);
    fast_line(it, spk_x - sl - 1, spk_y - sl, spk_x + sl, spk_y + sl + 1, ctx.col_red);
    fast_line(it, spk_x - sl, spk_y - sl, spk_x + sl + 1, spk_y + sl + 1, ctx.col_red);
    fast_line(it, spk_x - sl - 1, spk_y - sl + 1, spk_x + sl, spk_y + sl + 2, ctx.col_red);
  }
}

}  // namespace

void compute_3d_game_tilt(float raw_ax, float raw_ay, float raw_az,
                          float euler_roll_deg, float euler_pitch_deg,
                          bool game_just_entered,
                          float &out_roll_delta, float &out_pitch_delta) {
  constexpr float DEG_TO_RAD = 3.14159265f / 180.0f;
  constexpr float RAD_TO_DEG = 180.0f / 3.14159265f;

  float roll = std::isnan(euler_roll_deg) ? 0.0f : euler_roll_deg;
  float raw_pitch = std::isnan(euler_pitch_deg) ? -45.0f : euler_pitch_deg;

  float r_rad = roll * DEG_TO_RAD;
  float p_rad = raw_pitch * DEG_TO_RAD;
  float gr = std::clamp(std::sin(r_rad) * std::cos(p_rad), -1.0f, 1.0f);
  float clean_roll = std::asin(gr) * RAD_TO_DEG;
  float clean_pitch = raw_pitch;

  float gp = std::sin(p_rad);
  float gz = std::cos(p_rad);
  if (!std::isnan(raw_ax) && !std::isnan(raw_ay) && !std::isnan(raw_az)) {
    float g_norm = std::sqrt(raw_ax * raw_ax + raw_ay * raw_ay + raw_az * raw_az);
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
    float pz_norm = std::sqrt(gp * gp + gz * gz);
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
    float cross_pz = gp * zero_gz - gz * zero_gp;
    float dot_pz = gp * zero_gp + gz * zero_gz;
    out_pitch_delta = std::atan2(cross_pz, dot_pz) * RAD_TO_DEG;
  } else {
    out_pitch_delta = clean_pitch - zero_pitch;
  }

  if (touch_gesture().active && touch_gesture().last_y < scale_y(376) &&
      (LATTER_HAS_IMU == 0 || touch_gesture().max_travel >= scale_s(10))) {
    float span = static_cast<float>(std::max(110, scale_s(160)));
    out_roll_delta = std::clamp(((touch_gesture().last_x - SCREEN_CX) / span) * 28.0f, -35.0f, 35.0f);
    out_pitch_delta = std::clamp(((SCREEN_CY - touch_gesture().last_y) / span) * 28.0f, -35.0f, 35.0f);
  }
}

int &pending_game_sfx() {
  static int sfx = 0;
  return sfx;
}

int &game_2048_pending_dir() {
  static int dir = 0;
  return dir;
}

void render_games_page(Display &it, const UiFrameContext &ctx) {
  if (!frame_base_state().painted) {
    frame_base(it, ctx.col_bg);
  }

  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  static int s_prev_active_game = 0;

  if (rs.nav.active_game <= 0) {
    if (s_prev_active_game != 0) {
      s_prev_active_game = 0;
      save_persisted_prefs();
    }
    render_games_launcher(it, ctx, rs.nav.game_carousel_idx,
                          rs.nav.menu_scroll_active, rs.nav.menu_scroll_dir,
                          rs.nav.menu_scroll_target_page);
    return;
  }

  const bool game_just_entered = (rs.nav.active_game != s_prev_active_game);
  s_prev_active_game = rs.nav.active_game;

  float roll_delta = 0.0f;
  float pitch_delta = 0.0f;
  compute_3d_game_tilt(rs.hw_accel_x, rs.hw_accel_y, rs.hw_accel_z,
                       rs.hw_imu_roll, rs.hw_imu_pitch,
                       game_just_entered, roll_delta, pitch_delta);

  switch (rs.nav.active_game) {
    case 1:
      render_game_marbles(it, ctx, game_just_entered, roll_delta, pitch_delta,
                          rs.nav.marble_touch_x, rs.nav.marble_touch_y,
                          rs.nav.game_btn_pressed, prefs.high_score_marbles);
      break;
    case 2:
      render_game_archery(it, ctx, game_just_entered, roll_delta, pitch_delta,
                          rs.nav.marble_touch_x, rs.nav.game_btn_pressed,
                          prefs.high_score_archery);
      break;
    case 3:
      render_game_bricks(it, ctx, game_just_entered, roll_delta,
                         rs.nav.marble_touch_x, rs.nav.game_btn_pressed,
                         prefs.high_score_bricks);
      break;
    case 4:
      render_game_chomp(it, ctx, game_just_entered, roll_delta, pitch_delta,
                        rs.nav.marble_touch_x, rs.nav.game_btn_pressed,
                        prefs.high_score_chomp);
      break;
    case 5:
      render_game_2048(it, ctx, game_just_entered,
                       rs.nav.marble_touch_x, rs.nav.marble_touch_y,
                       rs.nav.game_btn_pressed, prefs.high_score_2048);
      break;
    case 6:
    default:
      render_game_memory(it, ctx, game_just_entered,
                         rs.nav.marble_touch_x, rs.nav.marble_touch_y,
                         rs.nav.game_btn_pressed, prefs.high_score_memory);
      break;
  }
}

}  // namespace latter_ui
