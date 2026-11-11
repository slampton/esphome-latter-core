#pragma once

// ==============================================================================
// Latter Shared Vector Glyph Library (include/ui_icons.h)
// Crisp, resolution-adaptive vector icons rendered exclusively with fast_* / AA
// primitives and zero raw Color(r, g, b) literals.
// ==============================================================================

#include "ui_core.h"

namespace latter_ui {
namespace ui {

template <typename DisplayT>
inline void draw_icon_power(DisplayT &it, int cx, int cy, int r, esphome::Color col, esphome::Color bg_cut) {
  const int ring_r = std::max(5, (r * 52) / 100);
  fast_circle_aa(it, cx, cy + 1, static_cast<float>(ring_r), col, 2.0f);
  fill_rect_row_major(it, cx - scale_s(4), cy - ring_r - 2, scale_s(8) + 1, scale_s(6), bg_cut);
  fill_rect_row_major(it, cx - 1, cy - ring_r - 2, 3, ring_r + 2, col);
}

template <typename DisplayT>
inline void draw_icon_thermometer(DisplayT &it, int cx, int cy, esphome::Color col, esphome::Color bulb_inner) {
  const int stem_h = scale_s(14);
  const int bulb_r = scale_s(5);
  fill_round_rect_aa(it, cx - scale_s(2), cy - stem_h / 2 - 2, 2 * scale_s(2) + 1, stem_h,
                     static_cast<float>(scale_s(2)), col);
  fast_filled_circle_aa(it, cx, cy + stem_h / 2, bulb_r, col);
  if (bulb_r > 2) {
    fast_filled_circle_aa(it, cx, cy + stem_h / 2, bulb_r - 2, bulb_inner);
  }
}

template <typename DisplayT>
inline void draw_icon_eighth_notes(DisplayT &it, int cx, int cy, esphome::Color col) {
  const int dx = scale_s(6);
  const int top_y = cy - scale_s(9);
  const int bot_y = cy + scale_s(5);
  const int head_r = std::max(3, scale_s(4));
  fast_filled_circle_aa(it, cx - dx, bot_y + 1, head_r, col);
  fast_filled_circle_aa(it, cx + dx - 1, bot_y - 1, head_r, col);
  fill_rect_row_major(it, cx - dx + head_r - 1, top_y + 2, 2, (bot_y - top_y), col);
  fill_rect_row_major(it, cx + dx + head_r - 2, top_y, 2, (bot_y - top_y), col);
  fast_line(it, cx - dx + head_r - 1, top_y + 2, cx + dx + head_r - 1, top_y, col);
  fast_line(it, cx - dx + head_r - 1, top_y + 3, cx + dx + head_r - 1, top_y + 1, col);
}

template <typename DisplayT>
inline void draw_icon_speaker(DisplayT &it, int cx, int cy, esphome::Color col, bool waves = true) {
  const int bx = cx - scale_s(7);
  fill_rect_row_major(it, bx, cy - scale_s(4), scale_s(4), scale_s(8) + 1, col);
  fast_filled_triangle(it, bx + scale_s(3), cy - scale_s(4),
                       bx + scale_s(9), cy - scale_s(9),
                       bx + scale_s(9), cy + scale_s(9), col);
  fast_filled_triangle(it, bx + scale_s(3), cy - scale_s(4),
                       bx + scale_s(3), cy + scale_s(4),
                       bx + scale_s(9), cy + scale_s(9), col);
  if (waves) {
    const int wx = bx + scale_s(12);
    fast_line(it, wx, cy - scale_s(4), wx + scale_s(2), cy, col);
    fast_line(it, wx + scale_s(2), cy, wx, cy + scale_s(4), col);
    fast_line(it, wx + scale_s(4), cy - scale_s(7), wx + scale_s(7), cy, col);
    fast_line(it, wx + scale_s(7), cy, wx + scale_s(4), cy + scale_s(7), col);
  }
}

template <typename DisplayT>
inline void draw_icon_lock(DisplayT &it, int cx, int cy, esphome::Color col,
                           esphome::Color keyhole_col, bool is_locked = true) {
  const int bw = scale_s(16);
  const int bh = scale_s(12);
  const int by = cy - scale_s(1);
  const int sh_r = scale_s(5);
  const int sh_cx = is_locked ? cx : (cx + scale_s(4));
  fast_circle_aa(it, sh_cx, by - scale_s(2), static_cast<float>(sh_r), col, 2.0f);
  fill_round_rect_aa(it, cx - bw / 2, by, bw, bh, 3.0f, col);
  fast_filled_circle_aa(it, cx, by + bh / 2 - 1, std::max(1, scale_s(2)), keyhole_col);
}

template <typename DisplayT>
inline void draw_icon_shield(DisplayT &it, int cx, int cy, esphome::Color col) {
  const int hw = scale_s(9);
  const int top_y = cy - scale_s(10);
  const int mid_y = cy + scale_s(1);
  const int bot_y = cy + scale_s(10);
  fast_line(it, cx, top_y, cx - hw, top_y + scale_s(3), col);
  fast_line(it, cx, top_y, cx + hw, top_y + scale_s(3), col);
  fast_vline(it, cx - hw, top_y + scale_s(3), mid_y - (top_y + scale_s(3)), col);
  fast_vline(it, cx + hw, top_y + scale_s(3), mid_y - (top_y + scale_s(3)), col);
  fast_line(it, cx - hw, mid_y, cx, bot_y, col);
  fast_line(it, cx + hw, mid_y, cx, bot_y, col);
}

}  // namespace ui
}  // namespace latter_ui
