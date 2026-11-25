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

inline constexpr float game_f(float v466) noexcept {
  return v466 * (static_cast<float>(SCREEN_MIN_DIM) * (1.0f / 466.0f));
}
inline constexpr int game_px(int v466) noexcept {
  return (v466 * SCREEN_MIN_DIM + 233) / 466;
}

// ==============================================================================
// 1. PSRAM-Backed Per-Game State Pods (Zero Idle Internal SRAM)
// ==============================================================================
struct GameBallPod {
  float x, y, vx, vy, r;
  int col_idx;
  bool sunk;
  uint32_t sink_start_ms;
  float sink_x, sink_y;
};

struct GameParticlePod {
  float x, y, vx, vy;
  uint8_t r, g, b;
};

struct MarblesState {
  GameBallPod balls[5];
  uint32_t last_phys_ms;
  uint32_t last_sink_ms;
  uint32_t wave_clear_ms;
  uint32_t popup_ms;
  int popup_pts;
  int popup_combo;
  int combo_streak;
  int wave_num;
  float time_left_f;
  int game_score;
  bool round_over;
  bool is_new_high;
  float ring_wave_r;
  float ring_wave_x;
  float ring_wave_y;
  uint32_t peg_hit_ms[4];
};

#if LATTER_HAS_IMU
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

struct GameSnackPod {
  float x, y, vx, vy;
  int kind;
  bool eaten;
};

struct ArcheryState {
  GamePinnedPod pinned[10];
  int pinned_count;
  uint32_t last_phys_ms;
  int round_num;
  int arrows_left;
  int round_pts;
  int arch_score;
  int streak_mult;
  float target_phase;
  float smooth_ax;
  float smooth_ay;
  bool arrow_in_flight;
  float flight_z;
  float launch_aim_x;
  float launch_aim_y;
  float launch_wind;
  float wind_speed;
  char hit_feedback[36];
  uint8_t feedback_r, feedback_g, feedback_b;
  uint32_t feedback_timer;
  uint32_t round_banner_ms;
  bool round_over;
  bool is_new_high;
};

struct BricksState {
  GameBrickPod bricks[18];
  GameParticlePod debris[6];
  uint32_t last_phys_ms;
  int wave_num;
  int lives_left;
  int bricks_score;
  int rally_combo;
  int wall_bounces;
  bool hit_brick_since_paddle;
  bool ball_launched;
  bool round_over;
  bool is_new_high;
  float ball_x, ball_y, ball_vx, ball_vy;
  float trail_x[4];
  float trail_y[4];
  float smooth_paddle_x;
  uint32_t debris_ms, popup_ms, banner_ms;
  int popup_x, popup_y;
  char popup_text[32];
  uint8_t popup_r, popup_g, popup_b;
  char banner_text[32];
};

struct ChompState {
  GameSnackPod snacks[7];
  GameParticlePod confetti[16];
  float mon_x, mon_y, mon_vx, mon_vy;
  uint32_t last_phys_ms, entry_ms, chomp_timer, level_clear_until_ms;
  bool post_play_tared;
  int level_num, snacks_left, snack_score, next_kind, chomp_word_idx;
};
#else
struct BlocksState {
  int8_t board[20][10];
  int8_t cur_piece;
  int8_t cur_rot;
  int8_t cur_x;
  int8_t cur_y;
  int8_t hold_piece;
  bool hold_used;
  int8_t bag[7];
  int8_t next_bag[7];
  int8_t bag_idx;
  int32_t score;
  int lines;
  int level;
  bool game_over;
  bool is_new_high;
  uint32_t last_fall_ms;
  uint32_t lock_start_ms;
  int lock_resets;
  uint32_t clearing_mask;
  uint32_t clear_start_ms;
  uint32_t rng_state;
};

struct SnakeState {
  static constexpr int kMaxCells = 24 * 24;
  int cols;
  int rows;
  int8_t body_x[kMaxCells];
  int8_t body_y[kMaxCells];
  int len;
  int8_t dir;       // 0=UP, 1=RIGHT, 2=DOWN, 3=LEFT
  int8_t turn_q[2];
  int turn_q_len;
  int8_t apple_x;
  int8_t apple_y;
  bool bonus_active;
  int8_t bonus_x;
  int8_t bonus_y;
  uint32_t bonus_expire_ms;
  int apples_eaten;
  int32_t score;
  bool game_over;
  bool is_new_high;
  uint32_t last_step_ms;
  uint32_t rng_state;
  uint32_t burst_ms;
  int burst_count;
  GameParticlePod burst[8];
};

struct DropFourState {
  uint8_t board[6][7];
  bool mode_2p;
  uint8_t cur_player;   // 1 = P1 (gold), 2 = P2 / AI (red)
  int ai_streak;
  int32_t streak_score;
  int p1_moves;
  uint8_t winner;       // 0 = none, 1 = P1, 2 = P2/AI
  bool is_draw;
  int win_cells[4][2];
  bool falling;
  int fall_col;
  int fall_target_row;
  uint8_t fall_player;
  float fall_y;
  float fall_vy;
  int bounce_count;
  bool ai_thinking;
  uint32_t ai_ready_ms;
  uint32_t last_phys_ms;
  bool is_new_high;
  uint32_t rng_state;
};
#endif

struct GameArena {
  int initialized_game_id;
  MarblesState marbles;
#if LATTER_HAS_IMU
  ArcheryState archery;
  BricksState bricks;
  ChompState chomp;
#else
  BlocksState blocks;
  SnakeState snake;
  DropFourState drop4;
#endif
};

GameArena *&game_arena_slot() noexcept {
  static GameArena *s_ptr = nullptr;
  return s_ptr;
}

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

inline uint32_t xorshift32_step(uint32_t &state, uint32_t salt = 0U) noexcept {
  if (state == 0U) state = 0x9E3779B9U;
  state ^= (state << 13) ^ salt;
  state ^= (state >> 17);
  state ^= (state << 5);
  return state;
}

inline void render_game_hud_footer(Rgb565Surface &it, int cx, const char *text,
                                   UiFontView font, UiColor col) {
  const auto &ly = layout();
  const int hud_y = ly.back_y - dp_px(34);
  const std::string fit = fit_text_to_width_font(
      text != nullptr ? text : "", ly.safe_span(hud_y, dp_px(16)), font);
  ui::shadow_print(it, cx, hud_y, font, col, UiTextAlign::CENTER, fit.c_str());
}

// ==============================================================================
// 2. Shared Procedural Sprites (Active Games & Games Launcher Previews)
// ==============================================================================
void render_glass_marble(Rgb565Surface &it, int bx, int by, int br,
                         UiColor base_col, UiColor hi_col,
                         int sh_x = 2, int sh_y = 3) {
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
#else
inline void render_bevel_block_cell(Rgb565Surface &it, int x0, int y0, int sz,
                                    UiColor base_c, UiColor hi_c, UiColor lo_c) {
  if (sz <= 3) return;
  fill_rect_row_major(it, x0 + 1, y0 + 1, sz - 2, sz - 2, base_c);
  fast_hline(it, x0 + 1, y0 + 1, sz - 2, hi_c);
  fast_line(it, x0 + 1, y0 + 1, x0 + 1, y0 + sz - 2, hi_c);
  fast_hline(it, x0 + 1, y0 + sz - 2, sz - 2, lo_c);
  fast_line(it, x0 + sz - 2, y0 + 2, x0 + sz - 2, y0 + sz - 2, lo_c);
  if (sz >= 12) {
    fill_rect_row_major(it, x0 + 3, y0 + 3, std::max(2, sz / 5), std::max(2, sz / 5), hi_c);
  }
}
#endif

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

#if !LATTER_HAS_IMU
// ==============================================================================
// 5. Touch-Native Games (Blocks, Snake, Drop Four — Stationary / No-IMU Boards)
// ==============================================================================

// 7 standard polyomino shapes (0=I, 1=O, 2=T, 3=S, 4=Z, 5=J, 6=L) x 4 CW rotations x 4 (x, y) cells
constexpr int8_t kBlocksShapes[7][4][4][2] = {
    // 0: I (4x4 SRS box)
    {{{0, 1}, {1, 1}, {2, 1}, {3, 1}},
     {{2, 0}, {2, 1}, {2, 2}, {2, 3}},
     {{0, 2}, {1, 2}, {2, 2}, {3, 2}},
     {{1, 0}, {1, 1}, {1, 2}, {1, 3}}},
    // 1: O
    {{{1, 0}, {2, 0}, {1, 1}, {2, 1}},
     {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
     {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
     {{1, 0}, {2, 0}, {1, 1}, {2, 1}}},
    // 2: T (3x3 SRS box)
    {{{1, 0}, {0, 1}, {1, 1}, {2, 1}},
     {{1, 0}, {1, 1}, {2, 1}, {1, 2}},
     {{0, 1}, {1, 1}, {2, 1}, {1, 2}},
     {{1, 0}, {0, 1}, {1, 1}, {1, 2}}},
    // 3: S
    {{{1, 0}, {2, 0}, {0, 1}, {1, 1}},
     {{1, 0}, {1, 1}, {2, 1}, {2, 2}},
     {{1, 1}, {2, 1}, {0, 2}, {1, 2}},
     {{0, 0}, {0, 1}, {1, 1}, {1, 2}}},
    // 4: Z
    {{{0, 0}, {1, 0}, {1, 1}, {2, 1}},
     {{2, 0}, {1, 1}, {2, 1}, {1, 2}},
     {{0, 1}, {1, 1}, {1, 2}, {2, 2}},
     {{1, 0}, {0, 1}, {1, 1}, {0, 2}}},
    // 5: J
    {{{0, 0}, {0, 1}, {1, 1}, {2, 1}},
     {{1, 0}, {2, 0}, {1, 1}, {1, 2}},
     {{0, 1}, {1, 1}, {2, 1}, {2, 2}},
     {{1, 0}, {1, 1}, {0, 2}, {1, 2}}},
    // 6: L
    {{{2, 0}, {0, 1}, {1, 1}, {2, 1}},
     {{1, 0}, {1, 1}, {1, 2}, {2, 2}},
     {{0, 1}, {1, 1}, {2, 1}, {0, 2}},
     {{0, 0}, {1, 0}, {1, 1}, {1, 2}}},
};

// SRS CW wall kick offsets in screen coords (+y down): [from_rot][test 0..4][dx, dy]
constexpr int8_t kBlocksKicksJlstz[4][5][2] = {
    {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}},
    {{0, 0}, {1, 0},  {1, 1},   {0, -2}, {1, -2}},
    {{0, 0}, {1, 0},  {1, -1},  {0, 2},  {1, 2}},
    {{0, 0}, {-1, 0}, {-1, 1},  {0, -2}, {-1, -2}},
};

constexpr int8_t kBlocksKicksI[4][5][2] = {
    {{0, 0}, {-2, 0}, {1, 0},  {-2, 1},  {1, -2}},
    {{0, 0}, {-1, 0}, {2, 0},  {-1, -2}, {2, 1}},
    {{0, 0}, {2, 0},  {-1, 0}, {2, -1},  {-1, 2}},
    {{0, 0}, {1, 0},  {-2, 0}, {1, 2},   {-2, -1}},
};

inline UiColor blocks_piece_color(int kind, const UiFrameContext &ctx) noexcept {
  switch (kind) {
    case 0: return ctx.col_cyan;
    case 1: return ctx.col_gold;
    case 2: return ctx.col_magenta;
    case 3: return ctx.col_green;
    case 4: return ctx.col_red;
    case 5: return UiColor(72, 138, 255);
    case 6: return ctx.col_orange;
    default: return ctx.col_slate;
  }
}

inline void blocks_fill_bag(int8_t bag[7], uint32_t &rng, uint32_t salt) noexcept {
  for (int i = 0; i < 7; ++i) bag[i] = static_cast<int8_t>(i);
  for (int i = 6; i > 0; --i) {
    const int j = static_cast<int>(xorshift32_step(rng, salt) % static_cast<uint32_t>(i + 1));
    std::swap(bag[i], bag[j]);
  }
}

inline void draw_blocks_mini_piece(Rgb565Surface &it, int box_x, int box_y, int box_w, int box_h,
                                   int kind, const UiFrameContext &ctx, bool dimmed = false) {
  if (kind < 0 || kind >= 7) return;
  int min_c = 4, max_c = -1, min_r = 4, max_r = -1;
  for (int i = 0; i < 4; ++i) {
    const int c = kBlocksShapes[kind][0][i][0];
    const int r = kBlocksShapes[kind][0][i][1];
    min_c = std::min(min_c, c);
    max_c = std::max(max_c, c);
    min_r = std::min(min_r, r);
    max_r = std::max(max_r, r);
  }
  const int span_c = max_c - min_c + 1;
  const int span_r = max_r - min_r + 1;
  const int cell = std::clamp(std::min((box_w - 8) / 4, (box_h - 6) / 2), 8, 20);
  const int px0 = box_x + (box_w - span_c * cell) / 2;
  const int py0 = box_y + (box_h - span_r * cell) / 2;
  UiColor base = blocks_piece_color(kind, ctx);
  if (dimmed) base = scale_color(base, 0.45f);
  const UiColor hi = mix_color_q8(base, ctx.col_white, 110);
  const UiColor lo = scale_color(base, 0.52f);
  for (int i = 0; i < 4; ++i) {
    const int cx_i = kBlocksShapes[kind][0][i][0] - min_c;
    const int ry_i = kBlocksShapes[kind][0][i][1] - min_r;
    render_bevel_block_cell(it, px0 + cx_i * cell, py0 + ry_i * cell, cell, base, hi, lo);
  }
}

void render_game_blocks(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                        bool game_just_entered, int32_t &high_score_blocks) {
  const uint32_t now_ms = ctx.now_ms;
  auto &st = arena.blocks;

  auto pop_bag_piece = [&]() -> int8_t {
    const int8_t p = st.bag[st.bag_idx++];
    if (st.bag_idx >= 7) {
      std::memcpy(st.bag, st.next_bag, sizeof(st.bag));
      blocks_fill_bag(st.next_bag, st.rng_state, now_ms);
      st.bag_idx = 0;
    }
    return p;
  };

  auto peek_next_piece = [&]() -> int8_t {
    return st.bag[st.bag_idx];
  };

  auto spawn_piece = [&](int8_t kind) {
    st.cur_piece = kind;
    st.cur_rot = 0;
    st.cur_x = 3;
    st.cur_y = 0;
    st.last_fall_ms = now_ms;
    st.lock_start_ms = 0U;
    st.lock_resets = 0;
    if (!blocks_can_place(st.board, st.cur_piece, st.cur_rot, st.cur_x, st.cur_y)) {
      st.game_over = true;
      if (st.score > high_score_blocks) {
        high_score_blocks = st.score;
        st.is_new_high = true;
      }
      if (st.is_new_high) schedule_persisted_prefs_save(now_ms);
      pending_game_sfx() = 14;
    }
  };

  auto reset_game = [&]() {
    std::memset(st.board, 0, sizeof(st.board));
    st.rng_state = (now_ms != 0U) ? (now_ms ^ 0x5B10C45EU) : 0x5B10C45EU;
    blocks_fill_bag(st.bag, st.rng_state, 17U);
    blocks_fill_bag(st.next_bag, st.rng_state, 53U);
    st.bag_idx = 0;
    st.hold_piece = -1;
    st.hold_used = false;
    st.score = 0;
    st.lines = 0;
    st.level = 1;
    st.game_over = false;
    st.is_new_high = false;
    st.clearing_mask = 0U;
    st.clear_start_ms = 0U;
    game_input().clear();
    spawn_piece(pop_bag_piece());
  };

  auto lock_current_piece = [&]() {
    for (int i = 0; i < 4; ++i) {
      const int bx = st.cur_x + kBlocksShapes[st.cur_piece][st.cur_rot][i][0];
      const int by = st.cur_y + kBlocksShapes[st.cur_piece][st.cur_rot][i][1];
      if (by >= 0 && by < 20 && bx >= 0 && bx < 10) {
        st.board[by][bx] = static_cast<int8_t>(st.cur_piece + 1);
      }
    }
    st.hold_used = false;
    uint32_t mask = 0U;
    int full_cnt = 0;
    for (int r = 0; r < 20; ++r) {
      bool full = true;
      for (int c = 0; c < 10; ++c) {
        if (st.board[r][c] == 0) {
          full = false;
          break;
        }
      }
      if (full) {
        mask |= (1U << static_cast<unsigned>(r));
        ++full_cnt;
      }
    }
    if (full_cnt > 0) {
      st.clearing_mask = mask;
      st.clear_start_ms = now_ms;
      const int next_level = 1 + (st.lines + full_cnt) / 10;
      pending_game_sfx() = (full_cnt >= 4 || next_level > st.level) ? 5 : 10;
    } else {
      pending_game_sfx() = 13;
      spawn_piece(pop_bag_piece());
    }
  };

  auto try_move_x = [&](int dx) -> bool {
    if (blocks_can_place(st.board, st.cur_piece, st.cur_rot, st.cur_x + dx, st.cur_y)) {
      st.cur_x = static_cast<int8_t>(st.cur_x + dx);
      if (st.lock_start_ms != 0U && st.lock_resets < 15) {
        st.lock_start_ms = now_ms;
        st.lock_resets++;
      }
      pending_game_sfx() = 11;
      return true;
    }
    return false;
  };

  if (game_just_entered) {
    reset_game();
  }

  // Resolve active 180 ms line-clear flash
  if (!st.game_over && st.clearing_mask != 0U) {
    if (now_ms - st.clear_start_ms >= 180U) {
      uint32_t cleared_mask = 0U;
      const int cleared = blocks_clear_full_rows(st.board, cleared_mask);
      st.clearing_mask = 0U;
      if (cleared > 0) {
        st.score += blocks_guideline_clear_score(cleared, st.level);
        st.lines += cleared;
        const int next_lvl = 1 + st.lines / 10;
        if (next_lvl > st.level) {
          st.level = next_lvl;
          pending_game_sfx() = 5;
        }
        if (st.score > high_score_blocks) {
          high_score_blocks = st.score;
          st.is_new_high = true;
          schedule_persisted_prefs_save(now_ms);
        }
      }
      spawn_piece(pop_bag_piece());
    }
  }

  // Drain queued touch/button inputs
  auto &qin = game_input();
  GameInputEvent ev{};
  while (qin.pop(ev)) {
    if (ev.cmd == GameInputCmd::PLAY_AGAIN) {
      if (st.game_over) reset_game();
      continue;
    }
    if (st.game_over || st.clearing_mask != 0U) continue;
    switch (ev.cmd) {
      case GameInputCmd::LEFT:
        (void)try_move_x(-1);
        break;
      case GameInputCmd::RIGHT:
        (void)try_move_x(1);
        break;
      case GameInputCmd::ROTATE: {
        int rot = st.cur_rot;
        int px = st.cur_x;
        int py = st.cur_y;
        if (blocks_try_srs_rotate(st.board, st.cur_piece, rot, px, py)) {
          st.cur_rot = static_cast<int8_t>(rot);
          st.cur_x = static_cast<int8_t>(px);
          st.cur_y = static_cast<int8_t>(py);
          if (st.lock_start_ms != 0U && st.lock_resets < 15) {
            st.lock_start_ms = now_ms;
            st.lock_resets++;
          }
          pending_game_sfx() = 11;
        }
        break;
      }
      case GameInputCmd::HOLD:
        if (!st.hold_used) {
          const int8_t prev_hold = st.hold_piece;
          st.hold_piece = st.cur_piece;
          st.hold_used = true;
          pending_game_sfx() = 11;
          spawn_piece((prev_hold >= 0) ? prev_hold : pop_bag_piece());
        }
        break;
      case GameInputCmd::HARD_DROP: {
        int drop_rows = 0;
        while (blocks_can_place(st.board, st.cur_piece, st.cur_rot, st.cur_x, st.cur_y + 1)) {
          st.cur_y++;
          drop_rows++;
        }
        st.score += drop_rows * 2;
        if (st.score > high_score_blocks) {
          high_score_blocks = st.score;
          st.is_new_high = true;
          schedule_persisted_prefs_save(now_ms);
        }
        lock_current_piece();
        break;
      }
      default:
        break;
    }
  }

  // Hold-to-repeat DAS (175 ms) / ARR (65 ms)
  if (!st.game_over && st.clearing_mask == 0U &&
      (qin.held_cmd == GameInputCmd::LEFT || qin.held_cmd == GameInputCmd::RIGHT)) {
    if (now_ms >= qin.held_since_ms && (now_ms - qin.held_since_ms >= 175U) &&
        now_ms >= qin.held_last_rep_ms && (now_ms - qin.held_last_rep_ms >= 65U)) {
      qin.held_last_rep_ms = now_ms;
      (void)try_move_x((qin.held_cmd == GameInputCmd::LEFT) ? -1 : 1);
    }
  }

  // Gravity & 350 ms lock delay
  if (!st.game_over && st.clearing_mask == 0U) {
    const uint32_t fall_ms =
        (st.level <= 1)
            ? uint32_t{750}
            : std::max<uint32_t>(90U, uint32_t{750} - static_cast<uint32_t>(std::min(11, st.level - 1)) * uint32_t{60});
    const bool grounded = !blocks_can_place(st.board, st.cur_piece, st.cur_rot, st.cur_x, st.cur_y + 1);
    if (grounded) {
      if (st.lock_start_ms == 0U) {
        st.lock_start_ms = now_ms;
      } else if (now_ms - st.lock_start_ms >= 350U) {
        lock_current_piece();
      }
    } else {
      st.lock_start_ms = 0U;
      if (now_ms >= st.last_fall_ms && (now_ms - st.last_fall_ms >= fall_ms)) {
        st.last_fall_ms = now_ms;
        st.cur_y++;
      }
    }
  }

  // Compute ghost drop Y
  int ghost_y = st.cur_y;
  if (!st.game_over && st.clearing_mask == 0U) {
    while (blocks_can_place(st.board, st.cur_piece, st.cur_rot, st.cur_x, ghost_y + 1)) {
      ghost_y++;
    }
  }

  // Responsive multi-geometry layout driven by aspect ratio & SizeClass
  const auto &ly = layout();
  const int min_hit = ly.min_hit_px();
  const bool is_portrait_compact = (SCREEN_H > SCREEN_W) && (ly.size_class == SizeClass::Compact);
  const bool is_landscape = (SCREEN_W > SCREEN_H);

  int cell = 18;
  int well_x = dp_px(12);
  int well_y = dp_px(14);
  int well_w = 180;
  int well_h = 360;
  int right_col_w = 0;

  if (is_portrait_compact) {
    const int btn_h_est = std::max(dp_px(48), min_hit);
    const int avail_h = std::max(200, ly.back_y - dp_px(24) - btn_h_est - dp_px(14));
    const int avail_w = (SCREEN_W * 58) / 100;
    cell = std::clamp(std::min(avail_w / 10, avail_h / 20), 12, 28);
    well_w = 10 * cell;
    well_h = 20 * cell;
    well_x = dp_px(10);
    well_y = dp_px(14);
  } else if (is_landscape) {
    cell = std::clamp((ly.back_y - dp_px(36)) / 20, 12, 30);
    well_w = 10 * cell;
    well_h = 20 * cell;
    well_x = (SCREEN_W - well_w) / 2;
    well_y = dp_px(14);
  } else {
    const int avail_h = std::max(240, ly.back_y - dp_px(56));
    cell = std::clamp(std::min((SCREEN_W * 40) / 100 / 10, avail_h / 20), 14, 34);
    well_w = 10 * cell;
    well_h = 20 * cell;
    const int col_gap = dp_px(18);
    right_col_w = std::min(dp_px(192), std::max(dp_px(136), SCREEN_W - well_w - col_gap - dp_px(36)));
    const int total_w = well_w + col_gap + right_col_w;
    well_x = std::max(dp_px(16), (SCREEN_W - total_w) / 2);
    well_y = std::max(dp_px(16), (ly.back_y - dp_px(24) - well_h) / 2);
  }

  // Well backdrop & subtle grid
  fill_rect_row_major(it, well_x, well_y, well_w, well_h, kChromeTrack);
  const UiColor grid_c = UiColor(26, 34, 46);
  for (int c = 1; c < 10; ++c) {
    fast_line(it, well_x + c * cell, well_y + 1, well_x + c * cell, well_y + well_h - 2, grid_c);
  }
  for (int r = 1; r < 20; ++r) {
    fast_hline(it, well_x + 1, well_y + r * cell, well_w - 2, grid_c);
  }
  fast_rect_outline(it, well_x - 1, well_y - 1, well_w + 2, well_h + 2, kChromeCardBorder);

  // Locked blocks + line-clear flash
  const bool flash_hi = (st.clearing_mask != 0U) && (((now_ms - st.clear_start_ms) / 45U) & 1U) == 0U;
  for (int r = 0; r < 20; ++r) {
    const bool clearing_row = ((st.clearing_mask & (1U << static_cast<unsigned>(r))) != 0U);
    for (int c = 0; c < 10; ++c) {
      const int8_t val = st.board[r][c];
      if (val <= 0) continue;
      const int bx = well_x + c * cell;
      const int by = well_y + r * cell;
      if (clearing_row) {
        const UiColor fc = flash_hi ? ctx.col_white : ctx.col_gold;
        fill_rect_row_major(it, bx + 1, by + 1, cell - 2, cell - 2, fc);
      } else {
        const UiColor base = blocks_piece_color(val - 1, ctx);
        render_bevel_block_cell(it, bx, by, cell, base,
                                mix_color_q8(base, ctx.col_white, 110), scale_color(base, 0.52f));
      }
    }
  }

  // Translucent ghost landing preview + active falling piece
  if (!st.game_over && st.clearing_mask == 0U) {
    const UiColor pcol = blocks_piece_color(st.cur_piece, ctx);
    if (ghost_y > st.cur_y) {
      for (int i = 0; i < 4; ++i) {
        const int gx = st.cur_x + kBlocksShapes[st.cur_piece][st.cur_rot][i][0];
        const int gy = ghost_y + kBlocksShapes[st.cur_piece][st.cur_rot][i][1];
        if (gy >= 0 && gy < 20 && gx >= 0 && gx < 10) {
          const int px = well_x + gx * cell;
          const int py = well_y + gy * cell;
          fill_round_rect_glass(it, px + 1, py + 1, cell - 2, cell - 2, 2.0f, pcol, 44, 28);
          fast_rect_outline(it, px + 1, py + 1, cell - 2, cell - 2, mix_color_q8(pcol, ctx.col_white, 80));
        }
      }
    }
    const UiColor hi = mix_color_q8(pcol, ctx.col_white, 115);
    const UiColor lo = scale_color(pcol, 0.52f);
    for (int i = 0; i < 4; ++i) {
      const int ax = st.cur_x + kBlocksShapes[st.cur_piece][st.cur_rot][i][0];
      const int ay = st.cur_y + kBlocksShapes[st.cur_piece][st.cur_rot][i][1];
      if (ay >= 0 && ay < 20 && ax >= 0 && ax < 10) {
        render_bevel_block_cell(it, well_x + ax * cell, well_y + ay * cell, cell, pcol, hi, lo);
      }
    }
  }

  if (!st.game_over) {
    frame_hits().add_rect(well_x, well_y, well_w, well_h, WidgetId::GAME_BOARD_TAP, kHitTap);
  }

  auto draw_ctrl_btn = [&](int bx, int by, int bw, int bh, const char *mdi, uint16_t wid, bool accent = false) {
    const UiColor bg = accent ? ui::chrome_card_bg_on() : kChromeGlassTint;
    const UiColor brd = accent ? ctx.theme_primary : kChromeCardBorder;
    render_card_box(it, bx, by, bw, bh, bg, brd, kRadiusKeyPx(), GlassVariant::Clear, ctx.col_white);
    const int icon_px = std::clamp(std::min(bw, bh) - dp_px(14), 18, dp_px(30));
    render_mdi_icon(it, mdi, bx + bw / 2, by + bh / 2, icon_px, ctx.col_white, true);
    if (!st.game_over) {
      const int pad_x = std::max(0, (min_hit - bw + 1) / 2);
      const int pad_y = std::max(0, (min_hit - bh + 1) / 2);
      const int pad = std::max(pad_x, pad_y);
      frame_hits().add_rect(bx, by, bw, bh, wid, kHitTap | kHitFocusSlot, 0, pad);
    }
  };

  auto draw_hold_card = [&](int hx, int hy, int hw, int hh) {
    const UiColor brd = st.hold_used ? kChromeCardBorder : ctx.theme_primary;
    render_card_box(it, hx, hy, hw, hh, kChromeGlassTint, brd, kRadiusCardPx(), GlassVariant::Clear, ctx.col_white);
    ui::shadow_print(it, hx + hw / 2, hy + dp_px(13), ctx.font_small,
                     st.hold_used ? ctx.col_slate : ctx.col_gold, UiTextAlign::CENTER, "HOLD");
    if (st.hold_piece >= 0) {
      draw_blocks_mini_piece(it, hx + 4, hy + dp_px(24), hw - 8, hh - dp_px(28),
                             st.hold_piece, ctx, st.hold_used);
    } else {
      ui::shadow_print(it, hx + hw / 2, hy + hh / 2 + dp_px(8), ctx.font_small,
                       ctx.col_slate, UiTextAlign::CENTER, "TAP");
    }
    if (!st.game_over) {
      const int pad = std::max(0, (min_hit - std::min(hw, hh) + 1) / 2);
      frame_hits().add_rect(hx, hy, hw, hh, WidgetId::GAME_CTRL_HOLD, kHitTap | kHitFocusSlot, 0, pad);
    }
  };

  char score_buf[20], best_buf[20], lv_buf[24], ln_buf[24];
  std::snprintf(score_buf, sizeof(score_buf), "%d", static_cast<int>(st.score));
  std::snprintf(best_buf, sizeof(best_buf), "%d", static_cast<int>(high_score_blocks));
  std::snprintf(lv_buf, sizeof(lv_buf), "LV %d", st.level);
  std::snprintf(ln_buf, sizeof(ln_buf), "%d LN", st.lines);

  if (is_portrait_compact) {
    const int hx = well_x + well_w + dp_px(10);
    const int hw = std::max(dp_px(84), SCREEN_W - hx - dp_px(10));
    const int hcx = hx + hw / 2;
    const int card_gap = dp_px(8);
    const int next_h = dp_px(80);
    const int hold_h = dp_px(76);
    const int stats_y = well_y + next_h + hold_h + 2 * card_gap;
    const int stats_h = std::max(dp_px(140), well_y + well_h - stats_y);

    render_card_box(it, hx, well_y, hw, next_h, kChromeGlassTint, kChromeCardBorder, kRadiusCardPx(),
                    GlassVariant::Clear, ctx.col_white);
    ui::shadow_print(it, hcx, well_y + dp_px(13), ctx.font_small, ctx.col_slate, UiTextAlign::CENTER, "NEXT");
    draw_blocks_mini_piece(it, hx + 4, well_y + dp_px(24), hw - 8, next_h - dp_px(30), peek_next_piece(), ctx);

    draw_hold_card(hx, well_y + next_h + card_gap, hw, hold_h);

    render_card_box(it, hx, stats_y, hw, stats_h, kChromeGlassTint, kChromeCardBorder, kRadiusCardPx(),
                    GlassVariant::Clear, ctx.col_white);
    ui::shadow_print(it, hcx, stats_y + (stats_h * 11) / 100, ctx.font_small, ctx.col_slate, UiTextAlign::CENTER, "SCORE");
    ui::shadow_print(it, hcx, stats_y + (stats_h * 24) / 100, ctx.font_date, ctx.col_white, UiTextAlign::CENTER, score_buf);
    ui::shadow_print(it, hcx, stats_y + (stats_h * 41) / 100, ctx.font_small, ctx.col_slate, UiTextAlign::CENTER, "BEST");
    ui::shadow_print(it, hcx, stats_y + (stats_h * 54) / 100, ctx.font_small, ctx.col_gold, UiTextAlign::CENTER, best_buf);
    ui::shadow_print(it, hcx, stats_y + (stats_h * 72) / 100, ctx.font_small, ctx.col_cyan, UiTextAlign::CENTER, lv_buf);
    ui::shadow_print(it, hcx, stats_y + (stats_h * 86) / 100, ctx.font_small, ctx.col_slate, UiTextAlign::CENTER, ln_buf);

    // Bottom 4 tactile >= 7.0 mm frosted-glass controls
    const int bh = std::max(dp_px(48), min_hit);
    const int by = std::min(ly.back_y - dp_px(18) - bh, well_y + well_h + dp_px(8));
    const int gap = dp_px(8);
    const int row_safe = ly.band_safe_span(by + bh / 2, bh, dp_px(8));
    const int bw = std::max(40, (row_safe - 3 * gap) / 4);
    const int x0_btn = ctx.cx - (4 * bw + 3 * gap) / 2;
    draw_ctrl_btn(x0_btn + 0 * (bw + gap), by, bw, bh, "mdi:chevron-left", WidgetId::GAME_CTRL_LEFT);
    draw_ctrl_btn(x0_btn + 1 * (bw + gap), by, bw, bh, "mdi:rotate-right", WidgetId::GAME_CTRL_ROTATE, true);
    draw_ctrl_btn(x0_btn + 2 * (bw + gap), by, bw, bh, "mdi:arrow-collapse-down", WidgetId::GAME_CTRL_DROP);
    draw_ctrl_btn(x0_btn + 3 * (bw + gap), by, bw, bh, "mdi:chevron-right", WidgetId::GAME_CTRL_RIGHT);
  } else if (is_landscape) {
    // Landscape: left column NEXT/HOLD/stats, center well, right column 2x2 controls
    const int col_w = std::max(dp_px(76), well_x - dp_px(16));
    const int lx = dp_px(8);
    const int rx = well_x + well_w + dp_px(8);
    const int box_h = dp_px(78);
    render_card_box(it, lx, well_y, col_w, box_h, kChromeGlassTint, kChromeCardBorder, kRadiusCardPx(),
                    GlassVariant::Clear, ctx.col_white);
    ui::shadow_print(it, lx + col_w / 2, well_y + dp_px(14), ctx.font_small, ctx.col_slate, UiTextAlign::CENTER, "NEXT");
    draw_blocks_mini_piece(it, lx + 4, well_y + dp_px(24), col_w - 8, box_h - dp_px(30), peek_next_piece(), ctx);
    draw_hold_card(lx, well_y + box_h + dp_px(8), col_w, box_h);
    ui::shadow_print(it, lx + col_w / 2, well_y + 2 * box_h + dp_px(32), ctx.font_small, ctx.col_white, UiTextAlign::CENTER, score_buf);
    ui::shadow_print(it, lx + col_w / 2, well_y + 2 * box_h + dp_px(58), ctx.font_small, ctx.col_gold, UiTextAlign::CENTER, lv_buf);

    const int gap = dp_px(8);
    const int bw = std::max(min_hit, (SCREEN_W - rx - dp_px(8) - gap) / 2);
    const int bh = std::max(min_hit, (well_h - gap) / 2);
    draw_ctrl_btn(rx, well_y, bw, bh, "mdi:chevron-left", WidgetId::GAME_CTRL_LEFT);
    draw_ctrl_btn(rx + bw + gap, well_y, bw, bh, "mdi:chevron-right", WidgetId::GAME_CTRL_RIGHT);
    draw_ctrl_btn(rx, well_y + bh + gap, bw, bh, "mdi:rotate-right", WidgetId::GAME_CTRL_ROTATE, true);
    draw_ctrl_btn(rx + bw + gap, well_y + bh + gap, bw, bh, "mdi:arrow-collapse-down", WidgetId::GAME_CTRL_DROP);
  } else {
    // Square / Regular side-by-side layout (Desk 720x720, 480x480, etc.)
    const int col_gap = dp_px(16);
    const int rx = well_x + well_w + col_gap;
    const int rw = std::max(dp_px(136), right_col_w);
    const int gap = dp_px(10);
    const int half_w = std::max(dp_px(60), (rw - gap) / 2);
    const int top_h = std::max(dp_px(76), (well_h * 21) / 100);
    const int stats_h = std::max(dp_px(100), (well_h * 34) / 100);

    render_card_box(it, rx, well_y, half_w, top_h, kChromeGlassTint, kChromeCardBorder, kRadiusCardPx(),
                    GlassVariant::Clear, ctx.col_white);
    ui::shadow_print(it, rx + half_w / 2, well_y + dp_px(14), ctx.font_small, ctx.col_slate, UiTextAlign::CENTER, "NEXT");
    draw_blocks_mini_piece(it, rx + 4, well_y + dp_px(26), half_w - 8, top_h - dp_px(32), peek_next_piece(), ctx);

    draw_hold_card(rx + half_w + gap, well_y, half_w, top_h);

    const int stats_y = well_y + top_h + gap;
    render_card_box(it, rx, stats_y, rw, stats_h, kChromeGlassTint, kChromeCardBorder, kRadiusCardPx(),
                    GlassVariant::Clear, ctx.col_white);
    const int rcx = rx + rw / 2;
    ui::shadow_print(it, rcx, stats_y + (stats_h * 14) / 100, ctx.font_small, ctx.col_slate, UiTextAlign::CENTER, "SCORE");
    ui::shadow_print(it, rcx, stats_y + (stats_h * 32) / 100, ctx.font_date, ctx.col_white, UiTextAlign::CENTER, score_buf);
    ui::shadow_print(it, rcx, stats_y + (stats_h * 52) / 100, ctx.font_small, ctx.col_slate, UiTextAlign::CENTER, "BEST");
    ui::shadow_print(it, rcx, stats_y + (stats_h * 66) / 100, ctx.font_small, ctx.col_gold, UiTextAlign::CENTER, best_buf);
    char lv_ln[36];
    std::snprintf(lv_ln, sizeof(lv_ln), "LV %d • %d LN", st.level, st.lines);
    ui::shadow_print(it, rcx, stats_y + (stats_h * 84) / 100, ctx.font_small, ctx.col_cyan, UiTextAlign::CENTER, lv_ln);

    const int ctrl_y = stats_y + stats_h + gap;
    const int bh = std::max(dp_px(48), (well_y + well_h - ctrl_y - gap) / 2);
    draw_ctrl_btn(rx, ctrl_y, half_w, bh, "mdi:chevron-left", WidgetId::GAME_CTRL_LEFT);
    draw_ctrl_btn(rx + half_w + gap, ctrl_y, half_w, bh, "mdi:chevron-right", WidgetId::GAME_CTRL_RIGHT);
    draw_ctrl_btn(rx, ctrl_y + bh + gap, half_w, bh, "mdi:rotate-right", WidgetId::GAME_CTRL_ROTATE, true);
    draw_ctrl_btn(rx + half_w + gap, ctrl_y + bh + gap, half_w, bh, "mdi:arrow-collapse-down", WidgetId::GAME_CTRL_DROP);
  }

  render_back_button(it, ctx.cx, ctx.font_small, ly.back_y, "Exit");

  if (st.game_over) {
    char sc_str[32], bs_str[32];
    std::snprintf(sc_str, sizeof(sc_str), "Score: %d  •  LV %d", static_cast<int>(st.score), st.level);
    std::snprintf(bs_str, sizeof(bs_str), "Best: %d", static_cast<int>(high_score_blocks));
    ui::game_over_sheet(it, ctx.cx, ctx.cy,
                        st.is_new_high ? "NEW HIGH SCORE!" : "GAME OVER",
                        st.is_new_high ? ctx.col_gold : ctx.col_red,
                        sc_str, bs_str, st.is_new_high,
                        "PLAY AGAIN", ctx.font_date, ctx.font_small,
                        ctx.theme_primary, ctx.col_white, ctx.col_gold, ctx.col_slate, WidgetId::GAME_OVER_CTA);
  }
}

void render_game_snake(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                       bool game_just_entered, int32_t &high_score_snake) {
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto &st = arena.snake;
  const auto &ly = layout();

  const int avail_w = std::max(dp_px(160), ly.safe_span(cy, dp_px(12)));
  const int avail_h = std::max(dp_px(140), ly.back_y - dp_px(56));
  const int target_cell = safe_clamp(dp_px(22), 14, 34);
  const int cols = std::clamp(avail_w / target_cell, 12, 22);
  const int rows = std::clamp(avail_h / target_cell, 10, 22);
  const int cell = std::max(12, std::min(avail_w / cols, avail_h / rows));

  const int board_w = cols * cell;
  const int board_h = rows * cell;
  const int board_x = (SCREEN_W - board_w) / 2;
  const int board_y = std::max(dp_px(12), ((ly.back_y - dp_px(48)) - board_h) / 2);

  auto cell_occupied = [&](int x, int y, bool check_apple, bool check_bonus) -> bool {
    for (int i = 0; i < st.len; ++i) {
      if (st.body_x[i] == x && st.body_y[i] == y) return true;
    }
    if (check_apple && st.apple_x == x && st.apple_y == y) return true;
    if (check_bonus && st.bonus_active && st.bonus_x == x && st.bonus_y == y) return true;
    return false;
  };

  auto spawn_free_cell = [&](int8_t &out_x, int8_t &out_y, bool check_apple, bool check_bonus) -> bool {
    const int total = st.cols * st.rows;
    if (st.len >= total) return false;
    const int start = static_cast<int>(xorshift32_step(st.rng_state, now_ms) % static_cast<uint32_t>(total));
    for (int k = 0; k < total; ++k) {
      const int idx = (start + k) % total;
      const int rx = idx % st.cols;
      const int ry = idx / st.cols;
      if (!cell_occupied(rx, ry, check_apple, check_bonus)) {
        out_x = static_cast<int8_t>(rx);
        out_y = static_cast<int8_t>(ry);
        return true;
      }
    }
    return false;
  };

  auto spawn_burst = [&](int gx, int gy, UiColor primary, UiColor secondary) {
    const float bx = static_cast<float>(board_x + gx * cell + cell / 2);
    const float by = static_cast<float>(board_y + gy * cell + cell / 2);
    st.burst_count = 8;
    st.burst_ms = now_ms;
    for (int i = 0; i < 8; ++i) {
      const float ang = static_cast<float>(i) * (2.0f * PI_F / 8.0f) + 0.25f;
      const float spd = static_cast<float>(cell) * (2.2f + static_cast<float>(i & 1) * 0.9f);
      st.burst[i].x = bx;
      st.burst[i].y = by;
      st.burst[i].vx = fast_cos(ang) * spd;
      st.burst[i].vy = fast_sin(ang) * spd;
      const UiColor pc = (i & 1) ? secondary : primary;
      st.burst[i].r = pc.r;
      st.burst[i].g = pc.g;
      st.burst[i].b = pc.b;
    }
  };

  auto reset_game = [&]() {
    st.cols = cols;
    st.rows = rows;
    st.rng_state = (now_ms != 0U) ? (now_ms ^ 0x5A1C3E91U) : 0x5A1C3E91U;
    st.len = 4;
    const int sx = cols / 2;
    const int sy = rows / 2;
    for (int i = 0; i < st.len; ++i) {
      st.body_x[i] = static_cast<int8_t>(sx - i);
      st.body_y[i] = static_cast<int8_t>(sy);
    }
    st.dir = 1;  // RIGHT
    st.turn_q_len = 0;
    st.bonus_active = false;
    st.bonus_expire_ms = 0U;
    st.apples_eaten = 0;
    st.score = 0;
    st.game_over = false;
    st.is_new_high = false;
    st.last_step_ms = now_ms;
    st.burst_ms = 0U;
    st.burst_count = 0;
    st.apple_x = -1;
    st.apple_y = -1;
    (void)spawn_free_cell(st.apple_x, st.apple_y, false, false);
    game_input().clear();
  };

  if (game_just_entered || st.cols != cols || st.rows != rows) {
    reset_game();
  }

  // Drain input events into 2-deep turn queue
  GameInputEvent ev{};
  while (game_input().pop(ev)) {
    if (ev.cmd == GameInputCmd::PLAY_AGAIN) {
      if (st.game_over) reset_game();
      continue;
    }
    if (st.game_over) continue;
    int8_t nd = -1;
    if (ev.cmd == GameInputCmd::UP) nd = 0;
    else if (ev.cmd == GameInputCmd::RIGHT) nd = 1;
    else if (ev.cmd == GameInputCmd::DOWN) nd = 2;
    else if (ev.cmd == GameInputCmd::LEFT) nd = 3;
    if (nd >= 0) {
      (void)snake_enqueue_turn(st.turn_q, st.turn_q_len, st.dir, nd);
    }
  }

  // Expire bonus fruit after 6 seconds
  if (st.bonus_active && now_ms >= st.bonus_expire_ms) {
    st.bonus_active = false;
  }

  // Step snake simulation
  const uint32_t step_ms =
      std::max<uint32_t>(90U, uint32_t{210} - static_cast<uint32_t>(std::clamp(st.apples_eaten, 0, 24)) * uint32_t{5});
  if (!st.game_over && now_ms >= st.last_step_ms && (now_ms - st.last_step_ms >= step_ms)) {
    st.last_step_ms = now_ms;
    if (st.turn_q_len > 0) {
      st.dir = st.turn_q[0];
      st.turn_q[0] = st.turn_q[1];
      st.turn_q_len--;
    }
    static constexpr int8_t DX[4] = {0, 1, 0, -1};
    static constexpr int8_t DY[4] = {-1, 0, 1, 0};
    const int nx = static_cast<int>(st.body_x[0]) + DX[st.dir];
    const int ny = static_cast<int>(st.body_y[0]) + DY[st.dir];

    const bool ate_apple = (nx == st.apple_x && ny == st.apple_y);
    const bool ate_bonus = (st.bonus_active && nx == st.bonus_x && ny == st.bonus_y);
    const bool growing = (ate_apple || ate_bonus) && (st.len < SnakeState::kMaxCells);

    bool crashed = (nx < 0 || nx >= st.cols || ny < 0 || ny >= st.rows);
    if (!crashed) {
      const int check_len = growing ? st.len : (st.len - 1);
      for (int i = 0; i < check_len; ++i) {
        if (st.body_x[i] == nx && st.body_y[i] == ny) {
          crashed = true;
          break;
        }
      }
    }

    if (crashed) {
      st.game_over = true;
      if (st.score > high_score_snake) {
        high_score_snake = st.score;
        st.is_new_high = true;
      }
      if (st.is_new_high) schedule_persisted_prefs_save(now_ms);
      pending_game_sfx() = 14;
    } else {
      if (growing) st.len++;
      for (int i = st.len - 1; i > 0; --i) {
        st.body_x[i] = st.body_x[i - 1];
        st.body_y[i] = st.body_y[i - 1];
      }
      st.body_x[0] = static_cast<int8_t>(nx);
      st.body_y[0] = static_cast<int8_t>(ny);

      if (ate_apple) {
        st.apples_eaten++;
        st.score += 10;
        spawn_burst(nx, ny, ctx.col_red, ctx.col_gold);
        pending_game_sfx() = 1 + (st.apples_eaten & 3);
        if (!spawn_free_cell(st.apple_x, st.apple_y, false, true)) {
          st.game_over = true;  // Board filled!
        } else if ((st.apples_eaten % 5) == 0 && !st.bonus_active) {
          if (spawn_free_cell(st.bonus_x, st.bonus_y, true, false)) {
            st.bonus_active = true;
            st.bonus_expire_ms = now_ms + 6000U;
          }
        }
      } else if (ate_bonus) {
        st.bonus_active = false;
        st.score += 50;
        spawn_burst(nx, ny, ctx.col_gold, ctx.col_white);
        pending_game_sfx() = 9;
      }

      if (st.score > high_score_snake) {
        high_score_snake = st.score;
        st.is_new_high = true;
        schedule_persisted_prefs_save(now_ms);
      }
    }
  }

  // Render arena
  const float br = static_cast<float>(kRadiusCardPx());
  fill_round_rect_aa(it, board_x - 3, board_y - 3, board_w + 6, board_h + 6, br, kChromeTrack);
  stroke_round_rect_aa(it, board_x - 3, board_y - 3, board_w + 6, board_h + 6, br,
                       kChromeCardBorder, kChromeCardBorder, 1);

  // Subtle checkerboard grid cells for clean lane alignment
  const UiColor tile_alt(20, 27, 38);
  for (int r = 0; r < st.rows; ++r) {
    for (int c = (r & 1); c < st.cols; c += 2) {
      fill_rect_row_major(it, board_x + c * cell, board_y + r * cell, cell, cell, tile_alt);
    }
  }

  // Pulsing apple
  const float pulse = 0.5f + 0.5f * fast_sin(static_cast<float>(now_ms % 6283U) * 0.006f);
  if (st.apple_x >= 0 && st.apple_y >= 0) {
    const int ax = board_x + st.apple_x * cell + cell / 2;
    const int ay = board_y + st.apple_y * cell + cell / 2;
    const int ar = std::max(5, (cell * 36) / 100 + static_cast<int>(pulse * 1.5f));
    fast_filled_circle_aa(it, ax, ay + 1, ar, ctx.col_red);
    fast_filled_circle_aa(it, ax - ar / 3, ay - ar / 4, std::max(1, ar / 4), UiColor(255, 190, 190));
    fast_filled_triangle(it, ax, ay - ar, ax + ar / 2, ay - ar - std::max(2, ar / 2),
                         ax + ar / 3, ay - ar + 1, ctx.col_green);
  }

  // Timed golden bonus fruit with countdown ring
  if (st.bonus_active && now_ms < st.bonus_expire_ms) {
    const int bx = board_x + st.bonus_x * cell + cell / 2;
    const int by = board_y + st.bonus_y * cell + cell / 2;
    const float frac = static_cast<float>(st.bonus_expire_ms - now_ms) / 6000.0f;
    const int br_core = std::max(5, (cell * 34) / 100);
    const int br_ring = br_core + std::max(2, static_cast<int>(frac * static_cast<float>(cell) * 0.24f));
    fast_circle_aa(it, bx, by, static_cast<float>(br_ring), ctx.col_gold, 1.8f);
    fast_filled_circle_aa(it, bx, by, br_core, ctx.col_gold);
    fast_filled_circle_aa(it, bx - br_core / 3, by - br_core / 3, std::max(1, br_core / 3), ctx.col_white);
  }

  // Anti-aliased emerald-to-cyan gradient snake body (tail -> head)
  const UiColor col_head(36, 232, 168);
  const UiColor col_tail(28, 172, 238);
  const int max_r = std::max(5, (cell * 44) / 100);
  const int min_r = std::max(3, (cell * 28) / 100);
  for (int i = st.len - 1; i >= 0; --i) {
    const float t = (st.len > 1) ? (static_cast<float>(i) / static_cast<float>(st.len - 1)) : 0.0f;
    const int mix_q = static_cast<int>(t * 255.0f + 0.5f);
    const UiColor seg_c = mix_color_q8(col_head, col_tail, mix_q);
    const int sx = board_x + st.body_x[i] * cell + cell / 2;
    const int sy = board_y + st.body_y[i] * cell + cell / 2;
    const int sr = max_r - static_cast<int>(t * static_cast<float>(max_r - min_r));
    if (i < st.len - 1) {
      const int nx = board_x + st.body_x[i + 1] * cell + cell / 2;
      const int ny = board_y + st.body_y[i + 1] * cell + cell / 2;
      const int mx = (sx + nx) / 2;
      const int my = (sy + ny) / 2;
      fast_filled_circle_aa(it, mx, my, std::max(min_r, sr - 1), seg_c);
    }
    fast_filled_circle_aa(it, sx, sy, sr, seg_c);
    if (i == 0) {
      // Directional eyes on head
      const int eye_off_fwd = std::max(2, sr / 3);
      const int eye_off_side = std::max(2, (sr * 45) / 100);
      const int eye_r = std::max(2, sr / 3);
      const int pup_r = std::max(1, eye_r / 2);
      static constexpr int8_t FX[4] = {0, 1, 0, -1};
      static constexpr int8_t FY[4] = {-1, 0, 1, 0};
      const int fx = FX[st.dir];
      const int fy = FY[st.dir];
      const int px = -fy;
      const int py = fx;
      const int e1x = sx + fx * eye_off_fwd + px * eye_off_side;
      const int e1y = sy + fy * eye_off_fwd + py * eye_off_side;
      const int e2x = sx + fx * eye_off_fwd - px * eye_off_side;
      const int e2y = sy + fy * eye_off_fwd - py * eye_off_side;
      fast_filled_circle_aa(it, e1x, e1y, eye_r, ctx.col_white);
      fast_filled_circle_aa(it, e2x, e2y, eye_r, ctx.col_white);
      fast_filled_circle(it, e1x + fx, e1y + fy, pup_r, ctx.col_black);
      fast_filled_circle(it, e2x + fx, e2y + fy, pup_r, ctx.col_black);
    }
  }

  // Eat particle burst
  if (st.burst_ms > 0U && now_ms >= st.burst_ms && (now_ms - st.burst_ms < 320U)) {
    const float t = static_cast<float>(now_ms - st.burst_ms) * 0.001f;
    const int pr = std::max(2, static_cast<int>(static_cast<float>(cell) * 0.16f * (1.0f - t / 0.34f)));
    for (int i = 0; i < st.burst_count; ++i) {
      const int px = static_cast<int>(st.burst[i].x + st.burst[i].vx * t);
      const int py = static_cast<int>(st.burst[i].y + st.burst[i].vy * t);
      fast_filled_circle(it, px, py, pr, UiColor(st.burst[i].r, st.burst[i].g, st.burst[i].b));
    }
  }

  char hud_buf[64];
  std::snprintf(hud_buf, sizeof(hud_buf), "Score: %d  •  Best: %d  •  Len: %d",
                static_cast<int>(st.score), static_cast<int>(high_score_snake), st.len);
  render_game_hud_footer(it, cx, hud_buf, ctx.font_small, ctx.col_gold);
  render_back_button(it, cx, ctx.font_small, ly.back_y, "Exit");

  if (st.game_over) {
    char sc_str[32], bs_str[32];
    std::snprintf(sc_str, sizeof(sc_str), "Score: %d  •  Len: %d", static_cast<int>(st.score), st.len);
    std::snprintf(bs_str, sizeof(bs_str), "Best: %d", static_cast<int>(high_score_snake));
    ui::game_over_sheet(it, cx, cy,
                        st.is_new_high ? "NEW HIGH SCORE!" : "GAME OVER",
                        st.is_new_high ? ctx.col_gold : ctx.col_red,
                        sc_str, bs_str, st.is_new_high,
                        "PLAY AGAIN", ctx.font_date, ctx.font_small,
                        ctx.theme_primary, ctx.col_white, ctx.col_gold, ctx.col_slate, WidgetId::GAME_OVER_CTA);
  }
}

void render_game_drop_four(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                           bool game_just_entered, int32_t &high_score_drop_four) {
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto &st = arena.drop4;
  const auto &ly = layout();
  const int min_hit = ly.min_hit_px();

  // Layout geometry
  const int toggle_h = std::max(dp_px(36), ly.min_row_hit_px());
  const int toggle_cy = std::max(ly.hdr_y, toggle_h / 2 + dp_px(12));
  const int toggle_w = std::min({dp_px(260), ly.band_safe_span(toggle_cy, toggle_h, dp_px(16)), ly.max_content_w()});
  const int status_y = toggle_cy + toggle_h / 2 + dp_px(14);

  const int avail_top = status_y + dp_px(14);
  const int avail_bot = ly.back_y - dp_px(44);
  const int avail_h = std::max(dp_px(140), avail_bot - avail_top);
  const int avail_w = ly.band_safe_span((avail_top + avail_bot) / 2, avail_h, dp_px(8));

  const int col_w = std::clamp(std::min(avail_w / 7, (avail_h - dp_px(12)) / 6), 24, dp_px(64));
  const int board_pad_y = std::max(4, col_w / 10);

  const int board_w = 7 * col_w;
  const int board_h = 6 * col_w + 2 * board_pad_y;
  const int board_x = (SCREEN_W - board_w) / 2;
  const int board_y = avail_top + std::max(0, (avail_h - board_h) / 2);
  const int disc_r = std::max(8, (col_w * 39) / 100);

  auto row_center_y = [&](int r) -> float {
    return static_cast<float>(board_y + board_pad_y + r * col_w + col_w / 2);
  };

  auto reset_board = [&](bool keep_streak) {
    std::memset(st.board, 0, sizeof(st.board));
    st.cur_player = 1U;
    st.p1_moves = 0;
    st.winner = 0U;
    st.is_draw = false;
    st.falling = false;
    st.ai_thinking = false;
    st.ai_ready_ms = 0U;
    st.last_phys_ms = now_ms;
    st.is_new_high = false;
    if (!keep_streak) {
      st.ai_streak = 0;
      st.streak_score = 0;
    }
  };

  auto start_drop_in_col = [&](int col) -> bool {
    if (col < 0 || col >= 7 || st.falling || st.winner != 0U || st.is_draw) return false;
    int target_r = -1;
    for (int r = 5; r >= 0; --r) {
      if (st.board[r][col] == 0U) {
        target_r = r;
        break;
      }
    }
    if (target_r < 0) return false;
    st.falling = true;
    st.fall_col = col;
    st.fall_target_row = target_r;
    st.fall_player = st.cur_player;
    st.fall_y = static_cast<float>(board_y - disc_r);
    st.fall_vy = 0.0f;
    st.bounce_count = 0;
    if (st.cur_player == 1U) st.p1_moves++;
    pending_game_sfx() = 11;
    return true;
  };

  if (game_just_entered) {
    st.mode_2p = false;
    st.rng_state = (now_ms != 0U) ? (now_ms ^ 0xD40F0417U) : 0xD40F0417U;
    reset_board(false);
    game_input().clear();
  }

  // Drain input events
  GameInputEvent ev{};
  while (game_input().pop(ev)) {
    if (ev.cmd == GameInputCmd::MODE_AI) {
      if (st.mode_2p) {
        st.mode_2p = false;
        reset_board(false);
        pending_game_sfx() = 11;
      }
    } else if (ev.cmd == GameInputCmd::MODE_2P) {
      if (!st.mode_2p) {
        st.mode_2p = true;
        reset_board(false);
        pending_game_sfx() = 11;
      }
    } else if (ev.cmd == GameInputCmd::PLAY_AGAIN) {
      if (st.winner != 0U || st.is_draw) {
        const bool keep = (!st.mode_2p && (st.winner == 1U || st.is_draw));
        reset_board(keep);
      }
    } else if (ev.cmd == GameInputCmd::COL_TAP) {
      if (!st.falling && !st.ai_thinking && st.winner == 0U && !st.is_draw) {
        if (st.mode_2p || st.cur_player == 1U) {
          (void)start_drop_in_col(static_cast<int>(ev.param));
        }
      }
    }
  }

  float dt = (st.last_phys_ms == 0U || now_ms < st.last_phys_ms)
                 ? 0.04f
                 : static_cast<float>(now_ms - st.last_phys_ms) * 0.001f;
  st.last_phys_ms = now_ms;
  dt = std::clamp(dt, 0.012f, 0.075f);

  // Animate gravity drop + micro-bounce physics
  if (st.falling) {
    const float gravity = static_cast<float>(col_w) * 44.0f;
    const float target_y = row_center_y(st.fall_target_row);
    st.fall_vy += gravity * dt;
    st.fall_y += st.fall_vy * dt;
    if (st.fall_y >= target_y) {
      st.fall_y = target_y;
      if (st.bounce_count < 2 && st.fall_vy > static_cast<float>(col_w) * 2.2f) {
        st.fall_vy = -st.fall_vy * 0.34f;
        st.bounce_count++;
        if (st.bounce_count == 1) pending_game_sfx() = 13;
      } else {
        st.falling = false;
        st.board[st.fall_target_row][st.fall_col] = st.fall_player;
        if (st.bounce_count == 0) pending_game_sfx() = 13;

        const int w = drop4_find_winner(st.board, st.win_cells);
        if (w != 0) {
          st.winner = static_cast<uint8_t>(w);
          if (!st.mode_2p) {
            if (w == 1) {
              st.ai_streak++;
              const int eff_bonus = std::max(0, (21 - st.p1_moves) * 15);
              st.streak_score += 100 * st.ai_streak + eff_bonus;
              if (st.streak_score > high_score_drop_four) {
                high_score_drop_four = st.streak_score;
                st.is_new_high = true;
                schedule_persisted_prefs_save(now_ms);
              }
              pending_game_sfx() = 5;
            } else {
              pending_game_sfx() = 14;
            }
          } else {
            pending_game_sfx() = 5;
          }
        } else {
          bool full = true;
          for (int c = 0; c < 7; ++c) {
            if (st.board[0][c] == 0U) {
              full = false;
              break;
            }
          }
          if (full) {
            st.is_draw = true;
            pending_game_sfx() = 10;
          } else {
            st.cur_player = (st.cur_player == 1U) ? 2U : 1U;
            if (!st.mode_2p && st.cur_player == 2U) {
              st.ai_thinking = true;
              st.ai_ready_ms = now_ms + 260U;
            }
          }
        }
      }
    }
  } else if (st.ai_thinking && now_ms >= st.ai_ready_ms && st.winner == 0U && !st.is_draw) {
    st.ai_thinking = false;
    const int ai_col = drop4_choose_ai_col(st.board, st.ai_streak, st.rng_state);
    if (ai_col >= 0) {
      (void)start_drop_in_col(ai_col);
    }
  }

  // Mode toggle ("1P AI" | "2P")
  ui::segmented_toggle(it, cx, toggle_cy, toggle_w, toggle_h,
                       "1P AI", "2P", st.mode_2p, ctx.font_small,
                       ctx.theme_primary, ctx.col_white, ctx.col_slate,
                       WidgetId::GAME_MODE_AI, WidgetId::GAME_MODE_2P);

  // Turn / streak status line
  const bool turn_p1 = (st.cur_player == 1U);
  const UiColor p1_col = ctx.col_gold;
  const UiColor p2_col = ctx.col_red;
  char status_buf[48];
  UiColor status_col = turn_p1 ? p1_col : p2_col;
  if (st.winner != 0U) {
    if (st.mode_2p) {
      std::snprintf(status_buf, sizeof(status_buf), "PLAYER %d WINS!", static_cast<int>(st.winner));
    } else {
      std::snprintf(status_buf, sizeof(status_buf), "%s", (st.winner == 1U) ? "YOU WIN!" : "AI WINS!");
    }
    status_col = (st.winner == 1U) ? p1_col : p2_col;
  } else if (st.is_draw) {
    std::snprintf(status_buf, sizeof(status_buf), "DRAW GAME");
    status_col = ctx.col_cyan;
  } else if (st.mode_2p) {
    std::snprintf(status_buf, sizeof(status_buf), "PLAYER %d TURN", turn_p1 ? 1 : 2);
  } else if (st.ai_thinking) {
    std::snprintf(status_buf, sizeof(status_buf), "AI THINKING...");
  } else if (st.ai_streak > 0) {
    std::snprintf(status_buf, sizeof(status_buf), "YOUR TURN  •  STREAK %d", st.ai_streak);
  } else {
    std::snprintf(status_buf, sizeof(status_buf), "YOUR TURN");
  }
  ui::shadow_print(it, cx, status_y, ctx.font_small, status_col, UiTextAlign::CENTER, status_buf);

  auto draw_disc = [&](int dx, int dy, uint8_t player) {
    const UiColor base = (player == 1U) ? p1_col : p2_col;
    const UiColor hi = (player == 1U) ? UiColor(255, 245, 168) : UiColor(255, 152, 152);
    const UiColor lo = scale_color(base, 0.58f);
    fast_filled_circle_aa(it, dx, dy, disc_r, base);
    fast_circle_aa(it, dx, dy, static_cast<float>(disc_r), lo, 1.4f);
    fast_circle_aa(it, dx, dy, static_cast<float>(std::max(4, (disc_r * 70) / 100)), hi, 1.2f);
    fast_filled_circle_aa(it, dx - disc_r / 3, dy - disc_r / 3, std::max(2, disc_r / 5), hi);
  };

  // Draw falling disc BEHIND the perforated board frame only while still protruding above board_y
  if (st.falling && st.fall_y < static_cast<float>(board_y + disc_r)) {
    const int fx = board_x + st.fall_col * col_w + col_w / 2;
    draw_disc(fx, static_cast<int>(std::round(st.fall_y)), st.fall_player);
  }

  // Cobalt vertical board frame
  const UiColor frame_bg(24, 52, 118);
  const UiColor frame_brd(64, 112, 198);
  const UiColor slot_hole(11, 16, 26);
  const UiColor slot_rim(16, 36, 86);
  const float br = static_cast<float>(kRadiusCardPx());
  fill_round_rect_aa(it, board_x, board_y, board_w, board_h, br, frame_bg);
  stroke_round_rect_aa(it, board_x, board_y, board_w, board_h, br, frame_brd, frame_brd, 1);

  for (int r = 0; r < 6; ++r) {
    const int cy_cell = board_y + board_pad_y + r * col_w + col_w / 2;
    for (int c = 0; c < 7; ++c) {
      const int cx_cell = board_x + c * col_w + col_w / 2;
      fast_filled_circle_aa(it, cx_cell, cy_cell, disc_r + 1, slot_rim);
      fast_filled_circle_aa(it, cx_cell, cy_cell, disc_r, slot_hole);
      const uint8_t cell_v = st.board[r][c];
      if (cell_v != 0U) {
        draw_disc(cx_cell, cy_cell, cell_v);
      }
    }
  }

  // Re-draw falling disc clipped visually inside slots it passes through (smooth drop overlay)
  if (st.falling && st.fall_y >= static_cast<float>(board_y - disc_r / 2)) {
    const int fx = board_x + st.fall_col * col_w + col_w / 2;
    draw_disc(fx, static_cast<int>(std::round(st.fall_y)), st.fall_player);
  }

  // Glowing 4-in-a-row winning ring highlight
  if (st.winner != 0U) {
    const float glow = 0.5f + 0.5f * fast_sin(static_cast<float>(now_ms % 6283U) * 0.008f);
    const UiColor ring_c = mix_color_q8(ctx.col_gold, ctx.col_white, static_cast<int>(glow * 255.0f));
    for (int i = 0; i < 4; ++i) {
      const int wr = st.win_cells[i][0];
      const int wc = st.win_cells[i][1];
      if (wr >= 0 && wr < 6 && wc >= 0 && wc < 7) {
        const int wx = board_x + wc * col_w + col_w / 2;
        const int wy = board_y + board_pad_y + wr * col_w + col_w / 2;
        fast_circle_aa(it, wx, wy, static_cast<float>(disc_r + 2), ring_c, 2.4f);
        fast_circle_aa(it, wx, wy, static_cast<float>(disc_r - 1), ctx.col_white, 1.4f);
      }
    }
  }

  // Register full-column tap targets while round is live
  const bool round_over = (st.winner != 0U || st.is_draw);
  if (!round_over) {
    const int col_pad = std::max(0, (min_hit - col_w + 1) / 2);
    for (int c = 0; c < 7; ++c) {
      frame_hits().add_rect(board_x + c * col_w, board_y, col_w, board_h,
                            static_cast<uint16_t>(WidgetId::GAME_COL_BASE + c),
                            kHitTap | kHitFocusSlot, static_cast<int16_t>(c), col_pad);
    }
  }

  char hud_buf[64];
  if (st.mode_2p) {
    std::snprintf(hud_buf, sizeof(hud_buf), "2P Pass & Play  •  Best AI: %d",
                  static_cast<int>(high_score_drop_four));
  } else {
    std::snprintf(hud_buf, sizeof(hud_buf), "Streak: %d  •  Pts: %d  •  Best: %d",
                  st.ai_streak, static_cast<int>(st.streak_score), static_cast<int>(high_score_drop_four));
  }
  render_game_hud_footer(it, cx, hud_buf, ctx.font_small, ctx.col_gold);
  render_back_button(it, cx, ctx.font_small, ly.back_y, "Exit");

  if (round_over) {
    const char *title = "DRAW!";
    UiColor tcol = ctx.col_cyan;
    const char *cta = "PLAY AGAIN";
    char sc_str[40], bs_str[32];
    if (st.mode_2p) {
      if (st.winner == 1U) {
        title = "PLAYER 1 WINS!";
        tcol = p1_col;
      } else if (st.winner == 2U) {
        title = "PLAYER 2 WINS!";
        tcol = p2_col;
      }
      std::snprintf(sc_str, sizeof(sc_str), "2P Match Complete");
      std::snprintf(bs_str, sizeof(bs_str), "AI Best: %d", static_cast<int>(high_score_drop_four));
    } else {
      if (st.winner == 1U) {
        title = st.is_new_high ? "NEW BEST STREAK!" : "YOU WIN!";
        tcol = ctx.col_gold;
        cta = "NEXT ROUND";
      } else if (st.winner == 2U) {
        title = "AI WINS!";
        tcol = ctx.col_red;
      }
      std::snprintf(sc_str, sizeof(sc_str), "Streak %d  •  Pts: %d",
                    st.ai_streak, static_cast<int>(st.streak_score));
      std::snprintf(bs_str, sizeof(bs_str), "Best: %d", static_cast<int>(high_score_drop_four));
    }
    ui::game_over_sheet(it, cx, cy, title, tcol, sc_str, bs_str, st.is_new_high,
                        cta, ctx.font_date, ctx.font_small,
                        ctx.theme_primary, ctx.col_white, ctx.col_gold, ctx.col_slate, WidgetId::GAME_OVER_CTA);
  }
}
#endif  // !LATTER_HAS_IMU

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

#if !LATTER_HAS_IMU
// ==============================================================================
// 7. Drop Four Alpha-Beta AI Helpers
// ==============================================================================
inline int drop4_landing_row(const uint8_t board[6][7], int col) noexcept {
  if (col < 0 || col >= 7) return -1;
  for (int r = 5; r >= 0; --r) {
    if (board[r][col] == 0U) return r;
  }
  return -1;
}

inline int drop4_score_window(uint8_t a, uint8_t b, uint8_t c, uint8_t d) noexcept {
  int ai = 0, hu = 0, em = 0;
  const uint8_t vals[4] = {a, b, c, d};
  for (uint8_t v : vals) {
    if (v == 2U) ++ai;
    else if (v == 1U) ++hu;
    else ++em;
  }
  if (ai == 4) return 10000;
  if (hu == 4) return -10000;
  if (ai == 3 && em == 1) return 45;
  if (ai == 2 && em == 2) return 7;
  if (hu == 3 && em == 1) return -55;
  if (hu == 2 && em == 2) return -6;
  return 0;
}

int drop4_evaluate_board(const uint8_t board[6][7]) noexcept {
  int score = 0;
  // Strong center-column preference
  for (int r = 0; r < 6; ++r) {
    if (board[r][3] == 2U) score += 6;
    else if (board[r][3] == 1U) score -= 6;
    if (board[r][2] == 2U || board[r][4] == 2U) score += 2;
  }
  // Horizontal
  for (int r = 0; r < 6; ++r) {
    for (int c = 0; c < 4; ++c) {
      score += drop4_score_window(board[r][c], board[r][c + 1], board[r][c + 2], board[r][c + 3]);
    }
  }
  // Vertical
  for (int c = 0; c < 7; ++c) {
    for (int r = 0; r < 3; ++r) {
      score += drop4_score_window(board[r][c], board[r + 1][c], board[r + 2][c], board[r + 3][c]);
    }
  }
  // Diag down-right
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 4; ++c) {
      score += drop4_score_window(board[r][c], board[r + 1][c + 1], board[r + 2][c + 2], board[r + 3][c + 3]);
    }
  }
  // Diag up-right
  for (int r = 3; r < 6; ++r) {
    for (int c = 0; c < 4; ++c) {
      score += drop4_score_window(board[r][c], board[r - 1][c + 1], board[r - 2][c + 2], board[r - 3][c + 3]);
    }
  }
  return score;
}

int drop4_minimax(uint8_t board[6][7], int depth, int alpha, int beta, bool maximizing) noexcept {
  static constexpr int kColOrder[7] = {3, 2, 4, 1, 5, 0, 6};
  const int w = drop4_find_winner(board, nullptr);
  if (w == 2) return 100000 + depth;
  if (w == 1) return -100000 - depth;

  bool any_move = false;
  for (int c = 0; c < 7; ++c) {
    if (board[0][c] == 0U) {
      any_move = true;
      break;
    }
  }
  if (!any_move) return 0;
  if (depth <= 0) return drop4_evaluate_board(board);

  if (maximizing) {
    int best = -1000000;
    for (int col : kColOrder) {
      const int r = drop4_landing_row(board, col);
      if (r < 0) continue;
      board[r][col] = 2U;
      const int val = drop4_minimax(board, depth - 1, alpha, beta, false);
      board[r][col] = 0U;
      if (val > best) best = val;
      if (best > alpha) alpha = best;
      if (alpha >= beta) break;
    }
    return best;
  } else {
    int best = 1000000;
    for (int col : kColOrder) {
      const int r = drop4_landing_row(board, col);
      if (r < 0) continue;
      board[r][col] = 1U;
      const int val = drop4_minimax(board, depth - 1, alpha, beta, true);
      board[r][col] = 0U;
      if (val < best) best = val;
      if (best < beta) beta = best;
      if (alpha >= beta) break;
    }
    return best;
  }
}
#endif  // !LATTER_HAS_IMU

}  // namespace

// ==============================================================================
// 8. Exported Pure Game Logic Helpers (No-IMU Builds)
// ==============================================================================
#if !LATTER_HAS_IMU
bool blocks_can_place(const int8_t board[20][10], int kind, int rot, int px, int py) noexcept {
  if (kind < 0 || kind >= 7) return false;
  const int r_idx = ((rot % 4) + 4) % 4;
  for (int i = 0; i < 4; ++i) {
    const int x = px + kBlocksShapes[kind][r_idx][i][0];
    const int y = py + kBlocksShapes[kind][r_idx][i][1];
    if (x < 0 || x >= 10 || y >= 20) return false;
    if (y >= 0 && board[y][x] != 0) return false;
  }
  return true;
}

bool blocks_try_srs_rotate(const int8_t board[20][10], int kind, int &rot, int &px, int &py) noexcept {
  if (kind < 0 || kind >= 7) return false;
  const int from_rot = ((rot % 4) + 4) % 4;
  const int to_rot = (from_rot + 1) % 4;
  if (kind == 1) {  // O-piece
    rot = to_rot;
    return true;
  }
  const auto &kicks = (kind == 0) ? kBlocksKicksI[from_rot] : kBlocksKicksJlstz[from_rot];
  for (int t = 0; t < 5; ++t) {
    const int nx = px + kicks[t][0];
    const int ny = py + kicks[t][1];
    if (blocks_can_place(board, kind, to_rot, nx, ny)) {
      rot = to_rot;
      px = nx;
      py = ny;
      return true;
    }
  }
  return false;
}

int blocks_clear_full_rows(int8_t board[20][10], uint32_t &out_mask) noexcept {
  out_mask = 0U;
  int cleared = 0;
  int dst_r = 19;
  for (int src_r = 19; src_r >= 0; --src_r) {
    bool full = true;
    for (int c = 0; c < 10; ++c) {
      if (board[src_r][c] == 0) {
        full = false;
        break;
      }
    }
    if (full) {
      out_mask |= (1U << static_cast<unsigned>(src_r));
      ++cleared;
    } else {
      if (dst_r != src_r) {
        std::memcpy(board[dst_r], board[src_r], 10);
      }
      --dst_r;
    }
  }
  for (int r = dst_r; r >= 0; --r) {
    std::memset(board[r], 0, 10);
  }
  return cleared;
}

int blocks_guideline_clear_score(int lines_cleared, int level) noexcept {
  const int lv = std::max(1, level);
  switch (lines_cleared) {
    case 1: return 100 * lv;
    case 2: return 300 * lv;
    case 3: return 500 * lv;
    case 4: return 800 * lv;
    default: return (lines_cleared > 4) ? (800 * lv) : 0;
  }
}

bool snake_enqueue_turn(int8_t turn_q[2], int &turn_q_len, int8_t cur_dir, int8_t new_dir) noexcept {
  if (new_dir < 0 || new_dir > 3 || turn_q_len >= 2) return false;
  const int8_t eff_dir = (turn_q_len > 0) ? turn_q[turn_q_len - 1] : cur_dir;
  if (new_dir == eff_dir || ((new_dir ^ 2) == eff_dir)) return false;
  turn_q[turn_q_len++] = new_dir;
  return true;
}

int drop4_find_winner(const uint8_t board[6][7], int out_cells[4][2]) noexcept {
  static constexpr int DIRS[4][2] = {{0, 1}, {1, 0}, {1, 1}, {-1, 1}};
  for (int r = 0; r < 6; ++r) {
    for (int c = 0; c < 7; ++c) {
      const uint8_t p = board[r][c];
      if (p == 0U) continue;
      for (const auto &d : DIRS) {
        const int r3 = r + 3 * d[0];
        const int c3 = c + 3 * d[1];
        if (r3 < 0 || r3 >= 6 || c3 < 0 || c3 >= 7) continue;
        if (board[r + d[0]][c + d[1]] == p &&
            board[r + 2 * d[0]][c + 2 * d[1]] == p &&
            board[r3][c3] == p) {
          if (out_cells != nullptr) {
            for (int k = 0; k < 4; ++k) {
              out_cells[k][0] = r + k * d[0];
              out_cells[k][1] = c + k * d[1];
            }
          }
          return static_cast<int>(p);
        }
      }
    }
  }
  return 0;
}

int drop4_choose_ai_col(const uint8_t board[6][7], int streak, uint32_t &rng_state) noexcept {
  static constexpr int kColOrder[7] = {3, 2, 4, 1, 5, 0, 6};
  uint8_t tmp[6][7]{};
  std::memcpy(tmp, board, sizeof(tmp));

  // 1. Always take an immediate winning move
  for (int col : kColOrder) {
    const int r = drop4_landing_row(tmp, col);
    if (r < 0) continue;
    tmp[r][col] = 2U;
    const bool win = (drop4_find_winner(tmp, nullptr) == 2);
    tmp[r][col] = 0U;
    if (win) return col;
  }

  // 2. Always block an immediate opponent 3-in-a-row threat
  for (int col : kColOrder) {
    const int r = drop4_landing_row(tmp, col);
    if (r < 0) continue;
    tmp[r][col] = 1U;
    const bool opp_win = (drop4_find_winner(tmp, nullptr) == 1);
    tmp[r][col] = 0U;
    if (opp_win) return col;
  }

  // 3. Adaptive alpha-beta search (streak 0: depth 3, streak 1: depth 4, streak 2+: depth 5)
  const int depth = (streak <= 0) ? 3 : ((streak == 1) ? 4 : 5);
  int best_col = -1;
  int best_val = -2000000;
  int valid_cols[7]{};
  int valid_vals[7]{};
  int n_valid = 0;

  for (int col : kColOrder) {
    const int r = drop4_landing_row(tmp, col);
    if (r < 0) continue;
    tmp[r][col] = 2U;
    const int val = drop4_minimax(tmp, depth - 1, -2000000, 2000000, false);
    tmp[r][col] = 0U;
    valid_cols[n_valid] = col;
    valid_vals[n_valid] = val;
    n_valid++;
    if (val > best_val) {
      best_val = val;
      best_col = col;
    }
  }

  // Friendly variety on streak 0 when multiple non-critical moves are near-tied
  if (streak <= 0 && n_valid > 1 && best_val > -50000 && best_val < 50000) {
    int tied[7]{};
    int n_tied = 0;
    for (int i = 0; i < n_valid; ++i) {
      if (valid_vals[i] >= best_val - 6) {
        tied[n_tied++] = valid_cols[i];
      }
    }
    if (n_tied > 1) {
      const uint32_t rv = xorshift32_step(rng_state, 0x31U);
      best_col = tied[rv % static_cast<uint32_t>(n_tied)];
    }
  }
  return best_col;
}
#endif  // !LATTER_HAS_IMU

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
