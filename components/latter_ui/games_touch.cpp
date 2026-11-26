#include "games_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace latter_ui {
using namespace latter_core;

#if !LATTER_HAS_IMU
// ==============================================================================
// 5. Touch-Native Games (Blocks, Snake, Drop Four — Stationary / No-IMU Boards)
// ==============================================================================
namespace {

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

}  // namespace

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

// ==============================================================================
// 7. Drop Four Alpha-Beta AI Helpers
// ==============================================================================
namespace {

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

}  // namespace

// ==============================================================================
// 8. Exported Pure Game Logic Helpers (No-IMU Builds)
// ==============================================================================
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

}  // namespace latter_ui
