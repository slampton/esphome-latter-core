#include "games_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace latter_ui {

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

}  // namespace latter_ui
