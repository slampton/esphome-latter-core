#pragma once

#include "ui_pages.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace latter_ui {

inline constexpr float game_f(float v466) noexcept {
  return canvas_f(v466);
}
inline constexpr int game_px(int v466) noexcept {
  return canvas_px(v466);
}

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

GameArena *acquire_game_arena() noexcept;
bool &game_imu_retare_flag() noexcept;

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

void render_glass_marble(Rgb565Surface &it, int bx, int by, int br,
                         UiColor base_col, UiColor hi_col,
                         int sh_x = 2, int sh_y = 3);
#if LATTER_HAS_IMU
void render_archery_target(Rgb565Surface &it, int tx, int ty, int r_outer,
                           UiColor col_white, UiColor col_black,
                           UiColor col_cyan, UiColor col_red,
                           UiColor col_gold, UiColor col_slate,
                           UiColor col_dark_gray);
void render_brick_tile(Rgb565Surface &it, int cx_b, int cy_b, int w, int h,
                       UiColor body_col, UiColor hi_col, int hp, int max_hp, UiColor col_white);
void render_brick_paddle(Rgb565Surface &it, int px, int py, int pw, int ph,
                         UiColor pad_col, UiColor col_white);
void render_chomp_snack(Rgb565Surface &it, int tx, int ty, int kind,
                        UiColor col_white, UiColor col_red, UiColor col_gold,
                        UiColor col_cyan, UiColor col_green, UiColor col_magenta, UiColor col_bg);
void render_chomp_character(Rgb565Surface &it, int mx, int my, int mr,
                            float look_dx, float look_dy, bool mouth_open, bool is_chomping,
                            uint32_t now_ms, UiColor col_white, UiColor col_black, UiColor col_dark_gray);
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

void render_game_marbles(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                         bool game_just_entered, float roll_delta, float pitch_delta,
                         float &marble_touch_x, float &marble_touch_y,
                         bool &game_btn_pressed, int32_t &high_score_marbles);
#if LATTER_HAS_IMU
void render_game_archery(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                         bool game_just_entered, float roll_delta, float pitch_delta,
                         float &marble_touch_x, bool &game_btn_pressed,
                         int32_t &high_score_archery);
void render_game_bricks(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                        bool game_just_entered, float roll_delta,
                        float &marble_touch_x, bool &game_btn_pressed,
                        int32_t &high_score_bricks);
void render_game_chomp(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                       bool game_just_entered, float roll_delta, float pitch_delta,
                       float &marble_touch_x, bool &game_btn_pressed,
                       int32_t &high_score_chomp);
#else
void render_game_blocks(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                        bool game_just_entered, int32_t &high_score_blocks);
void render_game_snake(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                       bool game_just_entered, int32_t &high_score_snake);
void render_game_drop_four(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                           bool game_just_entered, int32_t &high_score_drop_four);
#endif

}  // namespace latter_ui
