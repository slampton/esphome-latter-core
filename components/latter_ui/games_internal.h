#pragma once

// ==============================================================================
// Internal shared declarations for the Latter Games subsystem
// Pure C++20 internal L3 header — zero ESPHome includes.
// ==============================================================================

#include "ui_pages.h"
#include "ui_touch_engine.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace latter_ui {

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

struct GameArena {
  int initialized_game_id;
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

// Retryable PSRAM allocation via ui_psram_calloc / ui_psram_free (M-P1-18).
// Never latches nullptr on transient allocation failure; game_arena_drop() frees cleanly.
GameArena *acquire_game_arena() noexcept;
bool &game_imu_retare_flag() noexcept;

inline UiColor brighten_color(UiColor c, float factor) noexcept {
  return UiColor(
      static_cast<uint8_t>(std::clamp(static_cast<int>(static_cast<float>(c.r) * factor + 0.5f), 0, 255)),
      static_cast<uint8_t>(std::clamp(static_cast<int>(static_cast<float>(c.g) * factor + 0.5f), 0, 255)),
      static_cast<uint8_t>(std::clamp(static_cast<int>(static_cast<float>(c.b) * factor + 0.5f), 0, 255)));
}

inline void render_game_hud_footer(Rgb565Surface &it, int cx, const char *text,
                                   UiFontView font, UiColor col) {
  const int hud_y = layout().back_y - scale_y(34);
  const std::string fit = fit_text_to_width_font(
      text != nullptr ? text : "", safe_span(hud_y, 16), font);
  ui::shadow_print(it, cx, hud_y, font, col, UiTextAlign::CENTER, fit.c_str());
}

// Shared arcade sprites reused between active gameplay and the Games Launcher preview
void render_glass_marble(Rgb565Surface &it, int bx, int by, int br,
                         UiColor base_col, UiColor hi_col,
                         int sh_x = 2, int sh_y = 3);

void render_archery_target(Rgb565Surface &it, int tx, int ty, int r_outer,
                           UiColor col_white, UiColor col_black,
                           UiColor col_cyan, UiColor col_red,
                           UiColor col_gold, UiColor col_slate,
                           UiColor col_dark_gray);

void render_brick_tile(Rgb565Surface &it, int cx_b, int cy_b, int w, int h,
                       UiColor body_col, UiColor hi_col,
                       int hp, int max_hp, UiColor col_white);

void render_brick_paddle(Rgb565Surface &it, int px, int py, int pw, int ph,
                         UiColor pad_col, UiColor col_white);

void render_chomp_snack(Rgb565Surface &it, int tx, int ty, int kind,
                        UiColor col_white, UiColor col_red,
                        UiColor col_gold, UiColor col_cyan,
                        UiColor col_green, UiColor col_magenta,
                        UiColor col_bg);

void render_chomp_character(Rgb565Surface &it, int mx, int my, int mr,
                            float look_dx, float look_dy,
                            bool mouth_open, bool is_chomping,
                            uint32_t now_ms,
                            UiColor col_white, UiColor col_black,
                            UiColor col_dark_gray);

// Individual game engines
void render_game_marbles(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                         bool game_just_entered, float roll_delta, float pitch_delta,
                         float &marble_touch_x, float &marble_touch_y,
                         bool &game_btn_pressed, int32_t &high_score_marbles);

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

void render_game_2048(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                      bool game_just_entered, float &marble_touch_x, float &marble_touch_y,
                      bool &game_btn_pressed, int32_t &high_score_2048);

void render_game_memory(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                        bool game_just_entered, float &marble_touch_x, float &marble_touch_y,
                        bool &game_btn_pressed, int32_t &high_score_memory);

}  // namespace latter_ui
