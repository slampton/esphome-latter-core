#include "ui_pages.h"
#include "ui_touch_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace latter_ui {

namespace {

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

// Retryable PSRAM allocation via ui_psram_calloc / ui_psram_free.
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



void render_game_2048(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                      bool game_just_entered, float &marble_touch_x, float &marble_touch_y,
                      bool &game_btn_pressed, int32_t &high_score_2048) {
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto &grid = arena.grid_2048;
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
      const int pick = static_cast<int>(next_rng() % static_cast<uint32_t>(n_empty));
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
    const float tx = marble_touch_x;
    const float ty = marble_touch_y;
    marble_touch_x = -1.0f;
    game_btn_pressed = false;
    if (game_over) {
      reset_board();
    } else if (move_dir == 0 && tx >= 0.0f && ty < static_cast<float>(layout().back_y - scale_y(36))) {
      const float dx = tx - static_cast<float>(cx);
      const float dy = ty - static_cast<float>(board_y + board_sz / 2);
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
        const int r = (move_dir == 1) ? j : (move_dir == 3) ? (3 - j) : i;
        const int c = (move_dir == 4) ? j : (move_dir == 2) ? (3 - j) : i;
        if (grid[r][c] != 0) line[wp++] = grid[r][c];
      }
      int out[4] = {0, 0, 0, 0};
      int op = 0;
      for (int j = 0; j < wp; j++) {
        if (j + 1 < wp && line[j] == line[j + 1]) {
          const int nv = line[j] * 2;
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
        const int r = (move_dir == 1) ? j : (move_dir == 3) ? (3 - j) : i;
        const int c = (move_dir == 4) ? j : (move_dir == 2) ? (3 - j) : i;
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
        if (new_hs) schedule_persisted_prefs_save(now_ms);
        pending_game_sfx() = 14;
      } else {
        if (just_won_2048 && new_hs) schedule_persisted_prefs_save(now_ms);
        pending_game_sfx() = just_won_2048 ? 5 : (merged_any ? 10 : 11);
      }
    }
  }

  // Non-glass flat board container (avoids glass-on-glass double blending under interactive tiles)
  fill_round_rect_aa(it, board_x, board_y, board_sz, board_sz,
                     static_cast<float>(kRadiusCardPx()), kChromeTrack);
  stroke_round_rect_aa(it, board_x, board_y, board_sz, board_sz,
                       static_cast<float>(kRadiusCardPx()), kChromeCardBorder, kChromeCardBorder, 1);

  auto tile_bg_color = [&](int val) -> UiColor {
    if (val == 0) return kChromeGlassTint;
    if (val == 2) return kChromeTrack;
    if (val == 4) return kChromeTrackBorder;
    if (val == 8) return scale_color(ctx.col_orange, 0.72f);
    if (val == 16) return scale_color(ctx.col_orange, 0.85f);
    if (val == 32) return scale_color(ctx.col_red, 0.84f);
    if (val == 64) return ctx.col_red;
    if (val == 128) return scale_color(ctx.col_cyan, 0.78f);
    if (val == 256) return ctx.col_cyan;
    if (val == 512) return ctx.col_green;
    if (val == 1024) return ctx.col_magenta;
    return scale_color(ctx.col_gold, 0.62f);
  };

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      const int tx = board_x + gap + c * (tile_sz + gap);
      const int ty = board_y + gap + r * (tile_sz + gap);
      const int val = grid[r][c];
      const UiColor tc = tile_bg_color(val);
      const UiColor bc = (val >= 2048) ? ctx.col_gold
                                       : (val > 0 ? brighten_color(tc, 1.25f) : kChromeCardBorder);
      render_card_box(it, tx, ty, tile_sz, tile_sz, tc, bc, scale_s(8), GlassVariant::Clear, ctx.col_white);
      if (val > 0) {
        char v_str[12];
        std::snprintf(v_str, sizeof(v_str), "%d", val);
        const UiFontView tf = (val < 100) ? ctx.font_date : ctx.font_small;
        fast_print(it, tx + tile_sz / 2, ty + tile_sz / 2, tf, ctx.col_white, UiTextAlign::CENTER, v_str);
      }
    }
  }

  char hud_buf[64];
  std::snprintf(hud_buf, sizeof(hud_buf), "2048  •  Score: %d  •  Best: %d",
                score_2048, static_cast<int>(high_score_2048));
  render_game_hud_footer(it, cx, hud_buf, ctx.font_small, ctx.col_gold);
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

void render_game_memory(Rgb565Surface &it, const UiFrameContext &ctx, GameArena &arena,
                        bool game_just_entered, float &marble_touch_x, float &marble_touch_y,
                        bool &game_btn_pressed, int32_t &high_score_memory) {
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  auto *cards = arena.mem_cards;
  auto *matched = arena.mem_matched;
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
      const int j = static_cast<int>(rng_seed % static_cast<uint32_t>(i + 1));
      std::swap(cards[i], cards[j]);
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
    const int tx = static_cast<int>(marble_touch_x);
    const int ty = static_cast<int>(marble_touch_y);
    marble_touch_x = -1.0f;
    game_btn_pressed = false;

    if (mismatch_until_ms > 0U) {
      first_pick = -1;
      second_pick = -1;
      mismatch_until_ms = 0U;
    }

    for (int idx = 0; idx < 16; idx++) {
      const int r = idx / 4;
      const int c = idx % 4;
      const int cx_card = board_x + gap + c * (card_sz + gap);
      const int cy_card = board_y + gap + r * (card_sz + gap);
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
                  schedule_persisted_prefs_save(now_ms);
                }
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
        const float ang = static_cast<float>(a) * 0.785398f;
        fast_line(it, scx, scy,
                  scx + static_cast<int>(fast_cos(ang) * static_cast<float>(r_sym)),
                  scy + static_cast<int>(fast_sin(ang) * static_cast<float>(r_sym)),
                  ctx.col_gold);
      }
    } else if (sym == 1) {
      const int hr = std::max(4, r_sym / 2);
      fast_filled_circle(it, scx - hr + 1, scy - hr / 2, hr, ctx.col_red);
      fast_filled_circle(it, scx + hr - 1, scy - hr / 2, hr, ctx.col_red);
      fast_filled_triangle(it, scx - r_sym, scy - 1, scx + r_sym, scy - 1, scx, scy + r_sym, ctx.col_red);
    } else if (sym == 2) {
      fast_filled_triangle(it, scx, scy - r_sym, scx - r_sym, scy, scx + r_sym, scy, ctx.col_cyan);
      fast_filled_triangle(it, scx, scy + r_sym, scx - r_sym, scy, scx + r_sym, scy, scale_color(ctx.col_cyan, 0.8f));
    } else if (sym == 3) {
      const int bw = std::max(4, r_sym / 2);
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

  // Non-glass flat board container (avoids glass-on-glass double blending under interactive tiles)
  fill_round_rect_aa(it, board_x, board_y, board_sz, board_sz,
                     static_cast<float>(kRadiusCardPx()), kChromeTrack);
  stroke_round_rect_aa(it, board_x, board_y, board_sz, board_sz,
                       static_cast<float>(kRadiusCardPx()), kChromeCardBorder, kChromeCardBorder, 1);

  for (int idx = 0; idx < 16; idx++) {
    const int r = idx / 4;
    const int c = idx % 4;
    const int tx = board_x + gap + c * (card_sz + gap);
    const int ty = board_y + gap + r * (card_sz + gap);
    const bool is_open = matched[idx] || (idx == first_pick) || (idx == second_pick);
    const UiColor brd_c = matched[idx] ? ctx.col_green
                        : (is_open ? ctx.col_gold : kChromeCardBorder);
    render_card_box(it, tx, ty, card_sz, card_sz, kChromeGlassTint, brd_c, scale_s(8));
    if (!matched[idx]) {
      frame_hits().add_rect(tx, ty, card_sz, card_sz, WidgetId::GRID_CARD_BASE,
                            kHitTap | kHitFocusSlot, static_cast<int16_t>(idx));
    }
    const int scx = tx + card_sz / 2;
    const int scy = ty + card_sz / 2;
    if (is_open) {
      draw_card_symbol(cards[idx], scx, scy, std::max(8, card_sz / 3 - 2));
    } else {
      fast_filled_circle(it, scx, scy, std::max(3, card_sz / 8), ctx.theme_primary);
    }
  }

  char hud_buf[64];
  std::snprintf(hud_buf, sizeof(hud_buf), "R%d  •  Pairs: %d/8  •  %dx  •  %d",
                round_num, pairs_found, streak_mult, mem_score);
  render_game_hud_footer(it, cx, hud_buf, ctx.font_small, ctx.col_gold);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "Exit");
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



GameArena *&game_arena_slot() noexcept {
  static GameArena *s_ptr = nullptr;
  return s_ptr;
}

void draw_games_speaker_icon(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow, void *user_ctx) {
  const bool sound_on = (user_ctx != nullptr);
  ui::draw_icon_speaker(it, cx, cy, r, col, sound_on, shadow, !sound_on);
}

void render_games_launcher(Rgb565Surface &it, const UiFrameContext &ctx,
                           int &game_carousel_idx, bool menu_scroll_active,
                           int menu_scroll_dir, int menu_scroll_target_page) {
  const int cx = ctx.cx;
  const auto &prefs = device_prefs();
  const bool game_sound_on = ctx.game_sound_enabled;
  const bool compact_w = (SCREEN_W < 380);

  int enabled_ids[kMaxGamesLauncherItems]{};
  const int game_count = collect_enabled_games(enabled_ids, prefs);
  if (game_count <= 0) {
    game_carousel_idx = 0;
    const int hdr_y = compact_w ? 64 : scale_y(84);
    ui::shadow_print(it, cx, hdr_y, ctx.font_games_title, ctx.col_white, UiTextAlign::CENTER, "GAMES");
    render_empty_state(it, cx, ctx.cy, "No games enabled", ctx.font_small, ctx.col_slate);
    return;
  }

  if (game_carousel_idx < 0 || game_carousel_idx >= game_count) {
    game_carousel_idx = std::clamp(game_carousel_idx, 0, game_count - 1);
  }

  auto gs = [](int v) constexpr -> int { return scale_min(v); };

  auto draw_game_icon = [&](int g_idx, int icon_x, int icon_y) {
    if (g_idx == 0) {
      fast_filled_circle(it, icon_x, icon_y, gs(52), UiColor(18, 24, 28));
      fast_circle(it, icon_x, icon_y, gs(52), ctx.col_slate);
      fast_circle(it, icon_x, icon_y, gs(51), ctx.col_slate);
      fast_circle(it, icon_x, icon_y, gs(46), UiColor(30, 40, 48));

      fast_filled_circle(it, icon_x, icon_y, gs(15), ctx.col_black);
      fast_circle(it, icon_x, icon_y, gs(15), UiColor(45, 55, 65));
      fast_circle(it, icon_x, icon_y, gs(14), UiColor(45, 55, 65));

      for (int a = 0; a < 4; a++) {
        const float sa = static_cast<float>(a) * 1.570796f;
        fast_line(it, icon_x + static_cast<int>(fast_cos(sa) * scale_min_f(18.0f)),
                  icon_y + static_cast<int>(fast_sin(sa) * scale_min_f(18.0f)),
                  icon_x + static_cast<int>(fast_cos(sa + 0.8f) * scale_min_f(40.0f)),
                  icon_y + static_cast<int>(fast_sin(sa + 0.8f) * scale_min_f(40.0f)),
                  UiColor(35, 45, 55));
      }
      render_glass_marble(it, icon_x - gs(24), icon_y - gs(18), gs(13), ctx.col_red, UiColor(255, 138, 128));
      render_glass_marble(it, icon_x + gs(22), icon_y - gs(22), gs(12), ctx.col_cyan, UiColor(180, 255, 255));
      render_glass_marble(it, icon_x - gs(20), icon_y + gs(22), gs(11), ctx.col_green, UiColor(185, 246, 202));
      render_glass_marble(it, icon_x + gs(24), icon_y + gs(18), gs(13), ctx.col_gold, UiColor(255, 245, 157));
      render_glass_marble(it, icon_x + gs(2), icon_y - gs(31), gs(10), ctx.col_magenta, UiColor(248, 187, 208));

    } else if (g_idx == 1) {
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

    } else if (g_idx == 4) {
      // Decorative 2048 preview (scaled with scale_min, non-glass flat tiles per D-048)
      const int bsz = gs(96), tsz = gs(40), half_b = bsz / 2;
      const int off = gs(42), gap2 = gs(2), mid_t = gs(22);
      const float br = static_cast<float>(gs(12));
      const float tr = static_cast<float>(gs(7));
      fill_round_rect_aa(it, icon_x - half_b, icon_y - half_b, bsz, bsz, br, kChromeTrack);
      stroke_round_rect_aa(it, icon_x - half_b, icon_y - half_b, bsz, bsz, br, kChromeCardBorder, kChromeCardBorder, 1);

      fill_round_rect_aa(it, icon_x - off, icon_y - off, tsz, tsz, tr, kChromeTrackBorder);
      ui::shadow_print(it, icon_x - mid_t, icon_y - mid_t, ctx.font_small, ctx.col_white, UiTextAlign::CENTER, "2");
      fill_round_rect_aa(it, icon_x + gap2, icon_y - off, tsz, tsz, tr, scale_color(ctx.col_orange, 0.85f));
      ui::shadow_print(it, icon_x + mid_t, icon_y - mid_t, ctx.font_small, ctx.col_white, UiTextAlign::CENTER, "16");
      fill_round_rect_aa(it, icon_x - off, icon_y + gap2, tsz, tsz, tr, ctx.col_red);
      ui::shadow_print(it, icon_x - mid_t, icon_y + mid_t, ctx.font_small, ctx.col_white, UiTextAlign::CENTER, "64");
      fill_round_rect_aa(it, icon_x + gap2, icon_y + gap2, tsz, tsz, tr, scale_color(ctx.col_gold, 0.62f));
      ui::shadow_print(it, icon_x + mid_t, icon_y + mid_t, ctx.font_small, ctx.col_white, UiTextAlign::CENTER, "2K");

    } else {
      // Decorative Memory Match preview (scaled with scale_min, non-glass flat cards per D-048)
      const int csz = gs(40), off = gs(44), gap4 = gs(4);
      const float cr = static_cast<float>(gs(8));
      auto draw_mini_card = [&](int x0, int y0, UiColor brd, int dot_r, UiColor dot_c) {
        fill_round_rect_aa(it, x0, y0, csz, csz, cr, kChromeTrack);
        stroke_round_rect_aa(it, x0, y0, csz, csz, cr, brd, brd, 1);
        fast_filled_circle(it, x0 + csz / 2, y0 + csz / 2, dot_r, dot_c);
      };
      draw_mini_card(icon_x - off, icon_y - off, ctx.col_gold, gs(9), ctx.col_gold);
      draw_mini_card(icon_x + gap4, icon_y - off, kChromeCardBorder, gs(5), ctx.theme_primary);
      draw_mini_card(icon_x - off, icon_y + gap4, kChromeCardBorder, gs(5), ctx.theme_primary);
      draw_mini_card(icon_x + gap4, icon_y + gap4, ctx.col_gold, gs(9), ctx.col_gold);
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
    const int gap_px = scale_x(8);
    const int start_x = text_center_x - (w_lbl + gap_px + w_val) / 2;

    ui::shadow_print(it, start_x, score_y, ctx.font_small, ctx.col_gold, UiTextAlign::CENTER_LEFT, lbl_str);
    ui::shadow_print(it, start_x + w_lbl + gap_px, score_y, ctx.font_small, ctx.col_white, UiTextAlign::CENTER_LEFT, val_str);
  };

  const int icon_cx = compact_w ? cx : (cx - scale_x(118));
  const int card_cy = compact_w ? 158 : scale_y(194);
  const int text_cx = compact_w ? cx : (cx + scale_x(57));
  const int base_title_y = compact_w ? 242 : scale_y(176);
  const int base_score_y = compact_w ? 274 : scale_y(216);
  const int pips_y = compact_w ? 314 : scale_y(282);
  const int btn_row_y = layout().app_dock_y;
  const int cur_game_id = enabled_ids[game_carousel_idx];
  const int cur_g_idx = cur_game_id - 1;

  if (menu_scroll_active && game_count > 1) {
    const int full_span = scale_x(252);
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

    // Co-register carousel flank tap zones (when > 1 game enabled) + center hero card tap zone
    const int flank_w = scale_x(72);
    const int hero_top = scale_y(112);
    const int hero_h = std::max(80, pips_y - hero_top - scale_y(8));
    if (game_count > 1) {
      frame_hits().add_rect(0, hero_top, flank_w, hero_h, WidgetId::GAMES_PREV_BTN, kHitTap, -1);
      frame_hits().add_rect(SCREEN_W - flank_w, hero_top, flank_w, hero_h, WidgetId::GAMES_NEXT_BTN, kHitTap, 1);
      frame_hits().add_rect(flank_w, hero_top, SCREEN_W - 2 * flank_w, hero_h,
                            WidgetId::GAMES_HERO_CARD, kHitTap, static_cast<int16_t>(cur_game_id));
    } else {
      frame_hits().add_rect(scale_x(24), hero_top, SCREEN_W - 2 * scale_x(24), hero_h,
                            WidgetId::GAMES_HERO_CARD, kHitTap, static_cast<int16_t>(cur_game_id));
    }
  }

  // Documented arcade carve-out (D-070): Orbitron "GAMES" display header
  const int hdr_y = compact_w ? 64 : scale_y(84);
  ui::shadow_print(it, cx, hdr_y, ctx.font_games_title, ctx.col_white, UiTextAlign::CENTER, "GAMES");

  const int active_pip = (menu_scroll_active && game_count > 1)
                             ? std::clamp(menu_scroll_target_page, 0, game_count - 1)
                             : game_carousel_idx;
  render_dock_page_pips(it, cx, pips_y, game_count, active_pip, ctx.col_white, kChromePipInactive);

  const int play_h = std::max(44, scale_s(44));
  const int play_w = std::min(scale_x(154), safe_span(btn_row_y, 24) - scale_x(72));
  ui::action_pill_centered(it, cx + scale_x(36), play_w, btn_row_y, play_h,
                           ctx.theme_primary, kChromeGlassTint, "PLAY", ctx.col_white, ctx.font_btn_bold,
                           WidgetId::GAMES_PLAY_BTN, kHitTap | kHitFocusSlot, static_cast<int16_t>(cur_game_id));

  const int spk_x = cx - scale_x(91);
  const int spk_r = scale_s(22);
  ui::icon_circle_button(it, spk_x, btn_row_y, spk_r, kChromeGlassTint,
                         game_sound_on ? ctx.theme_secondary : kChromeCardBorder,
                         game_sound_on ? ctx.col_white : ctx.col_slate,
                         draw_games_speaker_icon,
                         game_sound_on ? reinterpret_cast<void *>(1) : nullptr,
                         WidgetId::GAMES_SOUND_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle, 1, 6);
}


GameArena *acquire_game_arena() noexcept {
  auto *&ptr = game_arena_slot();
  if (ptr == nullptr) {
    ptr = static_cast<GameArena *>(ui_psram_calloc(1, sizeof(GameArena)));
  }
  return ptr;
}

}  // namespace

void game_arena_drop() noexcept {
  auto *&ptr = game_arena_slot();
  if (ptr != nullptr) {
    ui_psram_free(ptr);
    ptr = nullptr;
  }
}

namespace {

bool &game_imu_retare_flag() noexcept {
  static bool flag = false;
  return flag;
}

}  // namespace

void compute_3d_game_tilt(float raw_ax, float raw_ay, float raw_az,
                          float euler_roll_deg, float euler_pitch_deg,
                          bool game_just_entered,
                          float &out_roll_delta, float &out_pitch_delta) {
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

  if (touch_gesture().active && touch_gesture().last_y < (layout().back_y - scale_y(50)) &&
      (LATTER_HAS_IMU == 0 || touch_gesture().max_travel >= scale_s(10))) {
    const float span = static_cast<float>(std::max(110, scale_s(160)));
    out_roll_delta = std::clamp((static_cast<float>(touch_gesture().last_x - SCREEN_CX) / span) * 28.0f, -35.0f, 35.0f);
    out_pitch_delta = std::clamp((static_cast<float>(SCREEN_CY - touch_gesture().last_y) / span) * 28.0f, -35.0f, 35.0f);
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

  const bool game_just_entered =
      (rs.nav.active_game != s_prev_active_game) ||
      (arena->initialized_game_id != rs.nav.active_game);
  s_prev_active_game = rs.nav.active_game;
  arena->initialized_game_id = rs.nav.active_game;

  float roll_delta = 0.0f;
  float pitch_delta = 0.0f;
  compute_3d_game_tilt(rs.hw_accel_x, rs.hw_accel_y, rs.hw_accel_z,
                       rs.hw_imu_roll, rs.hw_imu_pitch,
                       game_just_entered, roll_delta, pitch_delta);

  switch (rs.nav.active_game) {
    case 1:
      render_game_marbles(it, ctx, *arena, game_just_entered, roll_delta, pitch_delta,
                          rs.nav.marble_touch_x, rs.nav.marble_touch_y,
                          rs.nav.game_btn_pressed, prefs.high_score_marbles);
      break;
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
    case 5:
      render_game_2048(it, ctx, *arena, game_just_entered,
                       rs.nav.marble_touch_x, rs.nav.marble_touch_y,
                       rs.nav.game_btn_pressed, prefs.high_score_2048);
      break;
    case 6:
    default:
      render_game_memory(it, ctx, *arena, game_just_entered,
                         rs.nav.marble_touch_x, rs.nav.marble_touch_y,
                         rs.nav.game_btn_pressed, prefs.high_score_memory);
      break;
  }
}


}  // namespace latter_ui
