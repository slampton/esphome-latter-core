#pragma once

// ==============================================================================
// Latter UI Graphics Primitives, Adaptive Frosted Glass (D-041), 1-bpp Span Cache,
// ArtCanvasCache, Dirty-Band Frame Base & FrameProfileScope (include/ui_gfx.h)
// Pure C++20 UI graphics header — zero ESPHome or HAL includes.
// ==============================================================================

#include "latter_prof.h"
#include "ui_mdi.h"
#include "ui_types.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

namespace latter_ui {

// ------------------------------------------------------------------------------
// 1. Retryable PSRAM / Host Heap Helpers & Framebuffer Clear Hooks
//    (ESP-IDF guarded inside ui_gfx.cpp only)
// ------------------------------------------------------------------------------
struct PsramPhaseBuf {
  void *raw = nullptr;
  uint16_t *px = nullptr;
};

void *ui_psram_malloc(size_t bytes) noexcept;
void *ui_psram_calloc(size_t count, size_t elem_size) noexcept;
void *ui_psram_aligned_alloc(size_t bytes) noexcept;
void *ui_psram_aligned_calloc(size_t count, size_t elem_size) noexcept;
PsramPhaseBuf ui_psram_phase_alloc(size_t bytes, const void *match_addr, bool zero_init = false) noexcept;
void ui_psram_phase_free(PsramPhaseBuf &buf) noexcept;
void ui_psram_cache_writeback(const void *ptr, size_t bytes) noexcept;
void ui_psram_cache_invalidate(const void *ptr, size_t bytes) noexcept;
void ui_psram_free(void *ptr) noexcept;
bool psram_headroom_below(size_t min_free_bytes, size_t min_largest_block_bytes = 0U) noexcept;
void blit_psram_band_fast(uint16_t *dst_fb, const uint16_t *src_cache, int y0, int y1) noexcept;
bool ppa_srm_scale_rgb565(uint16_t *dst_fb, int dst_stride_px, int dst_x0, int dst_y0,
                          int dst_w, int dst_h, const uint16_t *src, int src_w, int src_h,
                          int crop_x0, int crop_y0, int crop_w, int crop_h,
                          bool sync_m2c = true) noexcept;
bool ppa_blend_uniform_dim_rgb565(uint16_t *dst_buf, const uint16_t *src_buf,
                                  int w, int h, int mul_q8) noexcept;
bool blend_psram_vertical_wash_fast(uint16_t *dst_fb, const uint16_t *src_bg, int y0, int y1,
                                    uint8_t wash_r, uint8_t wash_g, uint8_t wash_b,
                                    int sheet_q8, int falloff_drop = 152, int round_inset = 0,
                                    int cx = SCREEN_CX) noexcept;

using PostFrameRequestFn = void (*)(void *owner, void (*fn)());
void set_fb_post_frame_hook(PostFrameRequestFn hook) noexcept;
void fb_clear_bind_raw(void *owner, uint16_t *fb) noexcept;
void fb_clear_after_update();
void fb_clear_wait();
bool fb_clear_consume(uint16_t hw);
void fb_clear_frame_end();

// ------------------------------------------------------------------------------
// 2. Row-Major RGB565 Fast Drawing Primitives, Active Y-Band Scissor & Bayer Dither
// ------------------------------------------------------------------------------
inline int &active_clip_y0_ref() {
  static int y0 = 0;
  return y0;
}

inline int &active_clip_y1_ref() {
  static int y1 = SCREEN_H - 1;
  return y1;
}

inline int clip_y0() { return active_clip_y0_ref(); }
inline int clip_y1() { return active_clip_y1_ref(); }

inline void set_clip_y_band(int y0, int y1) {
  const int lo = std::max(0, std::min(SCREEN_H - 1, y0));
  const int hi = std::max(-1, std::min(SCREEN_H - 1, y1));
  active_clip_y0_ref() = lo;
  active_clip_y1_ref() = hi;
}

inline void reset_clip_y_band() {
  active_clip_y0_ref() = 0;
  active_clip_y1_ref() = SCREEN_H - 1;
}

inline bool row_in_clip_band(int y) {
  return y >= active_clip_y0_ref() && y <= active_clip_y1_ref();
}

static constexpr uint8_t kBayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

inline constexpr uint32_t bswap16x2_u32(uint32_t w) noexcept {
  return ((w & 0x00FF00FFU) << 8) | ((w >> 8) & 0x00FF00FFU);
}

// Bit-exact 8-bit Q8 RGB565 blend using 2-lane SWAR (R5+B5 packed with 3 guard bits each,
// G6 pre-shifted by 5) — mathematically identical to 3-channel scalar Q8 blending with zero
// clamping branches, zero per-pixel array lookups, and 32-bit aligned PSRAM pair bursts.
template <int Mode>
inline uint16_t blend_swar_q8_px(uint16_t hw, uint32_t inv, uint32_t base_rb, uint32_t base_g_s5) noexcept {
  const uint16_t p = decode_hw_rgb565_mode<Mode>(hw);
  const uint32_t rb = (static_cast<uint32_t>(p & 0xF800U) << 5) | static_cast<uint32_t>(p & 0x001FU);
  const uint32_t rb_sum = base_rb + rb * inv;
  const uint32_t g_sum = base_g_s5 + static_cast<uint32_t>(p & 0x07E0U) * inv;
  const uint16_t out = static_cast<uint16_t>(((rb_sum >> 13) & 0xF800U) |
                                             ((g_sum >> 8) & 0x07E0U) |
                                             ((rb_sum >> 8) & 0x001FU));
  return encode_hw_rgb565_mode<Mode>(out);
}

template <int Mode>
inline uint16_t blend_black_q8_px(uint16_t hw, uint32_t inv) noexcept {
  return blend_swar_q8_px<Mode>(hw, inv, (128U << 16) | 128U, 128U << 5);
}

template <typename DisplayT>
inline void fast_pixel(DisplayT &it, int x, int y, UiColor c) {
  if (x < 0 || x >= SCREEN_W || !row_in_clip_band(y)) return;
  uint16_t *fb = active_frame_buf16();
  if (fb != nullptr) {
    fb[static_cast<size_t>(y) * SCREEN_W + static_cast<size_t>(x)] = encode_hw_rgb565(c);
    return;
  }
  it.draw_pixel_at(x, y, c);
}

inline void fill_hw16_span(uint16_t *dst, int len, uint16_t hw) noexcept {
  if (len <= 0) return;
  if ((reinterpret_cast<uintptr_t>(dst) & 2U) != 0U) {
    *dst++ = hw;
    --len;
  }
  const uint32_t pair = static_cast<uint32_t>(hw) | (static_cast<uint32_t>(hw) << 16);
  uint32_t *d32 = reinterpret_cast<uint32_t *>(dst);
  int pairs = len >> 1;
  while (pairs >= 4) {
    d32[0] = pair;
    d32[1] = pair;
    d32[2] = pair;
    d32[3] = pair;
    d32 += 4;
    pairs -= 4;
  }
  while (pairs-- > 0) {
    *d32++ = pair;
  }
  if ((len & 1) != 0) {
    *reinterpret_cast<uint16_t *>(d32) = hw;
  }
}

template <typename DisplayT>
inline void fast_hline(DisplayT &it, int x, int y, int w, UiColor c) {
  if (w <= 0 || !row_in_clip_band(y)) return;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (x + w > SCREEN_W) {
    w = SCREEN_W - x;
  }
  if (w <= 0) return;
  uint16_t *fb = active_frame_buf16();
  if (fb != nullptr) {
    const uint16_t px = encode_hw_rgb565(c);
    fill_hw16_span(fb + static_cast<size_t>(y) * SCREEN_W + static_cast<size_t>(x), w, px);
    return;
  }
  it.horizontal_line(x, y, w, c);
}

template <typename DisplayT>
inline void fill_rect_row_major(DisplayT &it, int x, int y, int w, int h, UiColor c) {
  if (w <= 0 || h <= 0) return;
  const int cy0 = std::max(y, clip_y0());
  const int cy1 = std::min(y + h - 1, clip_y1());
  for (int row = cy0; row <= cy1; row++) {
    fast_hline(it, x, row, w, c);
  }
}

template <typename DisplayT>
inline void fast_vline(DisplayT &it, int x, int y, int h, UiColor c) {
  if (h <= 0 || x < 0 || x >= SCREEN_W) return;
  const int cy0 = std::max(y, clip_y0());
  const int cy1 = std::min(y + h - 1, clip_y1());
  if (cy1 < cy0) return;
  y = cy0;
  h = cy1 - cy0 + 1;
  uint16_t *fb = active_frame_buf16();
  if (fb != nullptr) {
    const uint16_t px = encode_hw_rgb565(c);
    uint16_t *dst = fb + static_cast<size_t>(y) * SCREEN_W + static_cast<size_t>(x);
    for (int i = 0; i < h; i++, dst += SCREEN_W) {
      *dst = px;
    }
    return;
  }
  it.vertical_line(x, y, h, c);
}

template <typename DisplayT>
inline void fast_rect_outline(DisplayT &it, int x, int y, int w, int h, UiColor c) {
  if (w <= 0 || h <= 0) return;
  fast_hline(it, x, y, w, c);
  fast_hline(it, x, y + h - 1, w, c);
  fast_vline(it, x, y, h, c);
  fast_vline(it, x + w - 1, y, h, c);
}

template <typename DisplayT>
inline void fast_filled_circle(DisplayT &it, int cx, int cy, int r, UiColor c) {
  if (r < 0 || cy + r < clip_y0() || cy - r > clip_y1()) return;
  int dx = -r;
  int dy = 0;
  int err = 2 - 2 * r;
  int e2 = 0;
  int last_dy = -1;
  do {
    if (dy != last_dy) {
      const int w = 2 * (-dx) + 1;
      fast_hline(it, cx + dx, cy + dy, w, c);
      if (dy != 0) fast_hline(it, cx + dx, cy - dy, w, c);
      last_dy = dy;
    }
    e2 = err;
    if (e2 < dy) {
      err += ++dy * 2 + 1;
      if (-dx == dy && e2 <= dx) {
        e2 = 0;
      }
    }
    if (e2 > dx) {
      err += ++dx * 2 + 1;
    }
  } while (dx <= 0);
}

template <typename DisplayT>
inline void fast_circle(DisplayT &it, int cx, int cy, int r, UiColor c) {
  if (r < 0 || cy + r < clip_y0() || cy - r > clip_y1()) return;
  uint16_t *fb = active_frame_buf16();
  const int cy0 = clip_y0();
  const int cy1 = clip_y1();
  const uint16_t px = (fb != nullptr) ? encode_hw_rgb565(c) : 0;
  auto put = [&](int x, int y) {
    if (static_cast<unsigned>(x) < static_cast<unsigned>(SCREEN_W) && y >= cy0 && y <= cy1) {
      if (fb != nullptr) fb[static_cast<size_t>(y) * SCREEN_W + static_cast<size_t>(x)] = px;
      else it.draw_pixel_at(x, y, c);
    }
  };
  int dx = -r;
  int dy = 0;
  int err = 2 - 2 * r;
  int e2 = 0;
  do {
    put(cx - dx, cy + dy);
    put(cx + dx, cy + dy);
    put(cx + dx, cy - dy);
    put(cx - dx, cy - dy);
    e2 = err;
    if (e2 < dy) {
      err += ++dy * 2 + 1;
      if (-dx == dy && e2 <= dx) {
        e2 = 0;
      }
    }
    if (e2 > dx) {
      err += ++dx * 2 + 1;
    }
  } while (dx <= 0);
}

template <typename DisplayT>
inline void fast_line(DisplayT &it, int x1, int y1, int x2, int y2, UiColor c) {
  const int cy0 = clip_y0();
  const int cy1 = clip_y1();
  if (std::max(y1, y2) < cy0 || std::min(y1, y2) > cy1) return;
  if (y1 == y2) {
    fast_hline(it, std::min(x1, x2), y1, std::abs(x2 - x1) + 1, c);
    return;
  }
  if (x1 == x2) {
    fast_vline(it, x1, std::min(y1, y2), std::abs(y2 - y1) + 1, c);
    return;
  }
  uint16_t *fb = active_frame_buf16();
  const uint16_t px = (fb != nullptr) ? encode_hw_rgb565(c) : 0;
  const int32_t dx = std::abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
  const int32_t dy = -std::abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
  int32_t err = dx + dy;
  while (true) {
    if (static_cast<unsigned>(x1) < static_cast<unsigned>(SCREEN_W) && y1 >= cy0 && y1 <= cy1) {
      if (fb != nullptr) fb[static_cast<size_t>(y1) * SCREEN_W + static_cast<size_t>(x1)] = px;
      else it.draw_pixel_at(x1, y1, c);
    }
    if (x1 == x2 && y1 == y2) break;
    const int32_t e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x1 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y1 += sy;
    }
  }
}

template <typename DisplayT>
inline void draw_glow_line(DisplayT &it, int x0, int y0, int x1, int y1,
                           UiColor core_col, UiColor halo_col) {
  const int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
  const int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
  if (dx >= dy) {
    fast_line(it, x0, y0 - 1, x1, y1 - 1, halo_col);
    fast_line(it, x0, y0 + 1, x1, y1 + 1, halo_col);
  } else {
    fast_line(it, x0 - 1, y0, x1 - 1, y1, halo_col);
    fast_line(it, x0 + 1, y0, x1 + 1, y1, halo_col);
  }
  fast_line(it, x0, y0, x1, y1, core_col);
}

template <typename DisplayT>
inline void fast_filled_flat_side_triangle(DisplayT &it, int x1, int y1, int x2, int y2, int x3, int y3,
                                           UiColor c) {
  int s1_x = x1, s1_y = y1;
  bool s1_swap = false;
  int s1_dx = std::abs(x2 - x1), s1_dy = std::abs(y2 - y1);
  const int s1_sx = ((x2 - x1) >= 0) ? 1 : -1;
  const int s1_sy = ((y2 - y1) >= 0) ? 1 : -1;
  if (s1_dy > s1_dx) {
    std::swap(s1_dx, s1_dy);
    s1_swap = true;
  }
  int s1_err = 2 * s1_dy - s1_dx;

  int s2_x = x1, s2_y = y1;
  bool s2_swap = false;
  int s2_dx = std::abs(x3 - x1), s2_dy = std::abs(y3 - y1);
  const int s2_sx = ((x3 - x1) >= 0) ? 1 : -1;
  const int s2_sy = ((y3 - y1) >= 0) ? 1 : -1;
  if (s2_dy > s2_dx) {
    std::swap(s2_dx, s2_dy);
    s2_swap = true;
  }
  int s2_err = 2 * s2_dy - s2_dx;

  for (int i = 0; i <= s1_dx; i++) {
    if (s1_x <= s2_x) {
      fast_hline(it, s1_x, s1_y, s2_x - s1_x + 1, c);
    } else {
      fast_hline(it, s2_x, s2_y, s1_x - s2_x + 1, c);
    }
    while (s1_err >= 0) {
      if (s1_swap) s1_x += s1_sx;
      else s1_y += s1_sy;
      s1_err -= 2 * s1_dx;
    }
    if (s1_swap) s1_y += s1_sy;
    else s1_x += s1_sx;
    s1_err += 2 * s1_dy;
    while (s2_y != s1_y) {
      while (s2_err >= 0) {
        if (s2_swap) s2_x += s2_sx;
        else s2_y += s2_sy;
        s2_err -= 2 * s2_dx;
      }
      if (s2_swap) s2_y += s2_sy;
      else s2_x += s2_sx;
      s2_err += 2 * s2_dy;
    }
  }
}

template <typename DisplayT>
inline void fast_filled_triangle(DisplayT &it, int x1, int y1, int x2, int y2, int x3, int y3,
                                 UiColor c) {
  if (y1 > y2) {
    std::swap(x1, x2);
    std::swap(y1, y2);
  }
  if (y1 > y3) {
    std::swap(x1, x3);
    std::swap(y1, y3);
  }
  if (y2 > y3) {
    std::swap(x2, x3);
    std::swap(y2, y3);
  }
  if (y3 < clip_y0() || y1 > clip_y1()) return;
  if (y1 == y3 && x1 == x2) {
    fast_hline(it, std::min(x1, x3), y1, std::abs(x3 - x1) + 1, c);
    return;
  }
  if (y2 == y3) {
    fast_filled_flat_side_triangle(it, x1, y1, x2, y2, x3, y3, c);
  } else if (y1 == y2) {
    fast_filled_flat_side_triangle(it, x3, y3, x1, y1, x2, y2, c);
  } else {
    const int x_temp = static_cast<int>(x1 + (static_cast<float>(y2 - y1) / static_cast<float>(y3 - y1)) * (x3 - x1));
    fast_filled_flat_side_triangle(it, x1, y1, x2, y2, x_temp, y2, c);
    fast_filled_flat_side_triangle(it, x3, y3, x2, y2, x_temp, y2, c);
  }
}

// ------------------------------------------------------------------------------
// 3. Anti-Aliased Shapes, Ordered-Dither Gradients & WCAG 4.5:1 Frosted Glass
// ------------------------------------------------------------------------------
inline uint16_t dither_rgb565(uint32_t r88, uint32_t g88, uint32_t b88, int t) {
  const uint32_t th = static_cast<uint32_t>(t) * 16U + 8U;
  uint32_t r = ((r88 * 31U) / 255U + th) >> 8;
  uint32_t g = ((g88 * 63U) / 255U + th) >> 8;
  uint32_t b = ((b88 * 31U) / 255U + th) >> 8;
  if (r > 31U) r = 31U;
  if (g > 63U) g = 63U;
  if (b > 31U) b = 31U;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

template <typename DisplayT>
inline void fast_hline_dither(DisplayT &it, int x, int y, int w, uint32_t r88, uint32_t g88, uint32_t b88) {
  if (w <= 0 || !row_in_clip_band(y)) return;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (x + w > SCREEN_W) w = SCREEN_W - x;
  if (w <= 0) return;
  uint16_t *fb = active_frame_buf16();
  if (fb == nullptr) {
    const UiColor c{static_cast<uint8_t>(r88 >> 8),
                    static_cast<uint8_t>(g88 >> 8),
                    static_cast<uint8_t>(b88 >> 8)};
    fast_hline(it, x, y, w, c);
    return;
  }
  const uint8_t *th = kBayer4[y & 3];
  uint16_t pat[4];
  for (int k = 0; k < 4; k++) pat[k] = encode_hw_rgb565_raw(dither_rgb565(r88, g88, b88, th[k]));
  uint16_t *dst = fb + static_cast<size_t>(y) * SCREEN_W + static_cast<size_t>(x);
  int i = 0;
  if ((reinterpret_cast<uintptr_t>(dst) & 2U) != 0U) {
    dst[0] = pat[x & 3];
    i = 1;
  }
  const int phase = (x + i) & 3;
  const uint32_t p0 = static_cast<uint32_t>(pat[phase]) |
                      (static_cast<uint32_t>(pat[(phase + 1) & 3]) << 16);
  const uint32_t p1 = static_cast<uint32_t>(pat[(phase + 2) & 3]) |
                      (static_cast<uint32_t>(pat[(phase + 3) & 3]) << 16);
  uint32_t *d32 = reinterpret_cast<uint32_t *>(dst + i);
  int pairs = (w - i) >> 1;
  while (pairs >= 2) {
    d32[0] = p0;
    d32[1] = p1;
    d32 += 2;
    pairs -= 2;
    i += 4;
  }
  if (pairs == 1) {
    *d32 = p0;
    i += 2;
  }
  while (i < w) {
    dst[i] = pat[(x + i) & 3];
    ++i;
  }
}

template <typename DisplayT>
inline void fast_pixel_blend(DisplayT &it, int x, int y, UiColor c, int a) {
  if (a <= 0 || x < 0 || x >= SCREEN_W || !row_in_clip_band(y)) return;
  uint16_t *fb = active_frame_buf16();
  if (fb == nullptr) {
    if (a >= 128) fast_pixel(it, x, y, c);
    return;
  }
  uint16_t &dst = fb[static_cast<size_t>(y) * SCREEN_W + static_cast<size_t>(x)];
  if (a >= 255) {
    dst = encode_hw_rgb565(c);
    return;
  }
  const uint16_t bg = decode_hw_rgb565_raw(dst);
  const int r5 = (bg >> 11) & 0x1F, g6 = (bg >> 5) & 0x3F, b5 = bg & 0x1F;
  const int br = (r5 << 3) | (r5 >> 2), bgc = (g6 << 2) | (g6 >> 4), bb = (b5 << 3) | (b5 >> 2);
  const int a_q8 = a + (a >> 7);
  const int ia = 256 - a_q8;
  const int rr = std::min(255, (br * ia + static_cast<int>(c.r) * a_q8 + 128) >> 8);
  const int rg = std::min(255, (bgc * ia + static_cast<int>(c.g) * a_q8 + 128) >> 8);
  const int rb = std::min(255, (bb * ia + static_cast<int>(c.b) * a_q8 + 128) >> 8);
  const UiColor blended{static_cast<uint8_t>(rr), static_cast<uint8_t>(rg), static_cast<uint8_t>(rb)};
  dst = encode_hw_rgb565(blended);
}

inline int aa_coverage_8(float a) {
  if (a <= 0.0f) return 0;
  if (a >= 1.0f) return 255;
  return static_cast<int>(a * 255.0f + 0.5f);
}

struct AaRoundRect {
  float cxl = 0, cxr = 0, cyt = 0, cyb = 0, rad = 0, r_in = 0, r_out = 0, rad2 = 0, inv2r = 1;
  int x0 = 0, x_last = -1, y0 = 0, y_end = 0;

  void init(int x, int y, int w, int h, float r) {
    const float max_rad = 0.5f * static_cast<float>(std::min(w, h));
    rad = std::max(0.0f, std::min(r, max_rad));
    x0 = x;
    x_last = x + w - 1;
    y0 = y;
    y_end = y + h;
    cxl = static_cast<float>(x) + rad;
    cxr = static_cast<float>(x + w) - rad;
    cyt = static_cast<float>(y) + rad;
    cyb = static_cast<float>(y + h) - rad;
    r_in = rad - 0.5f;
    r_out = rad + 0.5f;
    rad2 = rad * rad;
    inv2r = (rad > 0.5f) ? (0.5f / rad) : 1.0f;
  }

  bool row(int py, float &dy, int &s0, int &s1, int &e0, int &e1) const {
    if (py < y0 || py >= y_end || x_last < x0) return false;
    const float yc = static_cast<float>(py) + 0.5f;
    dy = 0.0f;
    if (yc < cyt) dy = cyt - yc;
    else if (yc > cyb) dy = yc - cyb;
    s0 = x0;
    s1 = x_last;
    e0 = x0;
    e1 = x_last;
    if (dy <= 0.0f || rad <= 0.0f) return true;
    if (dy >= r_out) return false;
    const float xo = std::sqrt(r_out * r_out - dy * dy);
    e0 = std::max(x0, static_cast<int>(std::floor(cxl - xo - 0.5f)));
    e1 = std::min(x_last, static_cast<int>(std::ceil(cxr + xo - 0.5f)));
    if (dy < r_in) {
      const float xi = std::sqrt(r_in * r_in - dy * dy);
      s0 = std::max(e0, static_cast<int>(std::ceil(cxl - xi - 0.5f)));
      s1 = std::min(e1, static_cast<int>(std::floor(cxr + xi - 0.5f)));
    } else {
      s0 = e1 + 1;
      s1 = e1;
    }
    return true;
  }

  int coverage(int px, float dy) const {
    const float xc = static_cast<float>(px) + 0.5f;
    const float ox = std::max(0.0f, std::max(cxl - xc, xc - cxr));
    return aa_coverage_8(0.5f - (ox * ox + dy * dy - rad2) * inv2r);
  }
};

void fill_round_rect_aa_impl(Rgb565Surface &it, int x0, int y0, int w, int h, float rad,
                             UiColor c_top, UiColor c_bot, int hole = 0);

template <typename DisplayT>
inline void fill_round_rect_aa(DisplayT &it, int x0, int y0, int w, int h, float rad,
                               UiColor c_top, UiColor c_bot, int hole = 0) {
  Rgb565Surface surf(it);
  fill_round_rect_aa_impl(surf, x0, y0, w, h, rad, c_top, c_bot, hole);
}

template <typename DisplayT>
inline void fill_round_rect_aa(DisplayT &it, int x0, int y0, int w, int h, float rad, UiColor c) {
  fill_round_rect_aa(it, x0, y0, w, h, rad, c, c, 0);
}

template <typename DisplayT>
inline void stroke_round_rect_aa(DisplayT &it, int x0, int y0, int w, int h, float rad,
                                 UiColor c_top, UiColor c_bot, int thick = 1) {
  fill_round_rect_aa(it, x0, y0, w, h, rad, c_top, c_bot, thick);
}

template <typename DisplayT>
inline void fast_filled_circle_aa(DisplayT &it, int cx, int cy, int r, UiColor c) {
  if (r < 0) return;
  fill_round_rect_aa(it, cx - r, cy - r, 2 * r + 1, 2 * r + 1, static_cast<float>(r) + 0.5f, c, c);
}

template <typename DisplayT>
inline void fast_circle_aa(DisplayT &it, int cx, int cy, float r, UiColor c, float thick = 1.5f) {
  if (r <= 1.0f) return;
  const float half = 0.5f * thick;
  const float ro = r + half + 0.5f;
  const float ri = r - half - 0.5f;
  const float r2 = r * r;
  const float inv2r = 0.5f / r;
  const int span = static_cast<int>(std::ceil(ro));
  const int dy_lo = std::max(-span, clip_y0() - cy);
  const int dy_hi = std::min(span, clip_y1() - cy);
  for (int dyi = dy_lo; dyi <= dy_hi; dyi++) {
    const int py = cy + dyi;
    const float dy = static_cast<float>(dyi);
    const float dy2 = dy * dy;
    if (dy2 >= ro * ro) continue;
    const int ox_hi = static_cast<int>(std::ceil(std::sqrt(ro * ro - dy2)));
    const int ox_lo = (ri > 0.0f && dy2 < ri * ri) ? static_cast<int>(std::floor(std::sqrt(ri * ri - dy2))) : 0;
    for (int ox = ox_lo; ox <= ox_hi; ox++) {
      const float d2 = static_cast<float>(ox * ox) + dy2;
      const int a = aa_coverage_8(half + 0.5f - std::fabs(d2 - r2) * inv2r);
      if (a <= 0) continue;
      fast_pixel_blend(it, cx - ox, py, c, a);
      if (ox > 0) fast_pixel_blend(it, cx + ox, py, c, a);
    }
  }
}

void fast_hline_blend_impl(Rgb565Surface &it, int x0, int w, int y, UiColor color, int alpha_255);

template <typename DisplayT>
inline void fast_hline_blend(DisplayT &it, int x0, int w, int y, UiColor color, int alpha_255) {
  Rgb565Surface surf(it);
  fast_hline_blend_impl(surf, x0, w, y, color, alpha_255);
}

void render_mdi_icon_impl(Rgb565Surface &it, std::string_view name, int cx, int cy, int size_px,
                          UiColor color, bool drop_shadow = true,
                          std::string_view fallback_name = "mdi:map-marker");

template <typename DisplayT>
inline void render_mdi_icon(DisplayT &it, std::string_view name, int cx, int cy, int size_px,
                            UiColor color, bool drop_shadow = true,
                            std::string_view fallback_name = "mdi:map-marker") {
  Rgb565Surface surf(it);
  render_mdi_icon_impl(surf, name, cx, cy, size_px, color, drop_shadow, fallback_name);
}

static constexpr int kGlassAlphaMinQ8 = 96;
static constexpr int kGlassClearAlphaQ8 = 104;
static constexpr int kGlassClearAlphaMaxQ8 = 116;
static constexpr int kGlassReadableAlphaMinQ8 = 208;
static constexpr int kGlassSemanticWashMaxQ8 = 38;
static constexpr int kGlassSemanticRimMaxQ8 = 64;
static constexpr float kGlassTextContrast = 5.0f;
static constexpr float kGlassTextContrastMin = 4.5f;
static constexpr int kGlassBrightPercent = 3;
static constexpr int kGlassSheenTopQ8 = 92;
static constexpr int kGlassSheenBotQ8 = 28;

struct GlassBackdrop {
  UiColor bright{};
  UiColor mean{};
  uint16_t sample_count = 0;
  bool valid = false;
};

const float *srgb_linear_lut();
float relative_luminance(UiColor c);
float contrast_ratio(float la, float lb);
UiColor mix_color_q8(UiColor c_under, UiColor c_over, int a);
uint8_t active_glass_style_raw() noexcept;
GlassBackdrop sample_glass_backdrop(int x0, int y0, int w, int h);
GlassBackdrop sample_glass_mean(int x0, int y0, int w, int h);
bool is_semantic_glass_color(UiColor c);
void normalize_glass_chrome(UiColor caller_bg, UiColor caller_brd,
                            GlassVariant &inout_variant, UiColor &out_base_tint,
                            UiColor &out_rim_tint);
int glass_alpha_for_text(UiColor tint, UiColor backdrop, UiColor text,
                         int min_alpha_q8 = kGlassAlphaMinQ8);
void resolve_glass_fill(const GlassBackdrop &bd, UiColor tint, UiColor ref_text,
                        UiColor &out_tint, int &out_a_top, int &out_a_bot,
                        int &out_rim_top, int &out_rim_bot, int explicit_alpha_q8 = -1,
                        GlassVariant variant = GlassVariant::Clear);
void resolve_styled_glass(int x0, int y0, int w, int h,
                          UiColor caller_bg, UiColor caller_brd,
                          GlassVariant explicit_variant, UiColor ref_text,
                          int explicit_alpha_q8, bool sample_surface,
                          GlassBackdrop &out_bd, UiColor &out_tint, UiColor &out_rim,
                          int &out_a_top, int &out_a_bot, int &out_rim_top, int &out_rim_bot);

void fill_round_rect_glass_impl(Rgb565Surface &it, int x0, int y0, int w, int h, float rad,
                                UiColor tint, int a_top, int a_bot, int hole = 0);
void fill_round_rect_glass_rimmed_impl(Rgb565Surface &it, int x0, int y0, int w, int h, float rad,
                                       UiColor rim_tint, int rim_top, int rim_bot,
                                       UiColor fill_tint, int fill_top, int fill_bot,
                                       float rad_in = -1.0f);

template <typename DisplayT>
inline void fill_round_rect_glass(DisplayT &it, int x0, int y0, int w, int h, float rad,
                                  UiColor tint, int a_top, int a_bot, int hole = 0) {
  Rgb565Surface surf(it);
  fill_round_rect_glass_impl(surf, x0, y0, w, h, rad, tint, a_top, a_bot, hole);
}

template <typename DisplayT>
inline void fill_round_rect_glass_rimmed(DisplayT &it, int x0, int y0, int w, int h, float rad,
                                         UiColor rim_tint, int rim_top, int rim_bot,
                                         UiColor fill_tint, int fill_top, int fill_bot,
                                         float rad_in = -1.0f) {
  Rgb565Surface surf(it);
  fill_round_rect_glass_rimmed_impl(surf, x0, y0, w, h, rad, rim_tint, rim_top, rim_bot,
                                    fill_tint, fill_top, fill_bot, rad_in);
}

template <typename DisplayT>
inline void render_glass_card(DisplayT &it, int x0, int y0, int w, int h, int r, UiColor tint, int alpha_q8,
                              GlassVariant variant = GlassVariant::Clear,
                              UiColor ref_text = kChromeGlassSheen) {
  if (w <= 4 || h <= 4 || (y0 + h) <= clip_y0() || y0 > clip_y1()) return;
  r = std::max(2, std::min(r, std::min(w, h) / 2));
  GlassBackdrop bd{};
  UiColor eff_tint{}, rim_col{};
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_styled_glass(x0, y0, w, h, tint, kChromeCardBorder, variant, ref_text, alpha_q8, false,
                       bd, eff_tint, rim_col, a_top, a_bot, rim_top, rim_bot);
  fill_round_rect_glass_rimmed(it, x0, y0, w, h, static_cast<float>(r),
                               rim_col, rim_top, rim_bot, eff_tint, a_top, a_bot,
                               static_cast<float>(r) - 1.0f);
}

inline bool &backdrop_active() {
  static bool active = false;
  return active;
}

inline int &backdrop_max_lum_q8() noexcept {
  static int q8 = 256;
  return q8;
}

// ------------------------------------------------------------------------------
// 4. Compiled 1/2/4-bpp Anti-Aliased Span Glyph Cache, UTF-8 Sanitizer & Text Fitting
// ------------------------------------------------------------------------------
struct TextSpan {
  int16_t x;
  int16_t y;
  int16_t len;
  uint8_t alpha;
  uint8_t pad{0};
};

struct TextCacheEntry {
  const void *font;
  uint32_t last_use;
  TextSpan *spans;
  uint16_t count;
  uint16_t cap;
  int16_t ink_x0;
  int16_t ink_x1;
  int16_t text_h;
  int16_t baseline;
  uint8_t bpp;
  char text[96];
};

std::string sanitize_utf8_for_font(const std::string &str, UiFontView font);
std::string fit_text_to_width_font(const std::string &raw_str, int max_w, UiFontView font);
int text_ink_right_edge_impl(UiFontView font, const char *text);
void fast_print_impl(Rgb565Surface &it, int x, int y, UiFontView font,
                     UiColor color, UiTextAlign align, const char *text,
                     UiColor background = kColorOff);
void fast_print_alpha_impl(Rgb565Surface &it, int x, int y, UiFontView font,
                           UiColor color, UiTextAlign align, const char *text,
                           int alpha_q8);
void shadow_print_impl(Rgb565Surface &it, int x, int y, UiFontView font,
                       UiColor color, UiTextAlign align, const char *text,
                       bool double_shadow = false);

template <typename FontT>
inline int text_ink_right_edge(FontT font, const char *text) {
  return text_ink_right_edge_impl(UiFontView(font), text);
}

template <typename DisplayT, typename FontT, typename AlignT>
inline void fast_print(DisplayT &it, int x, int y, FontT font, UiColor color,
                       AlignT align, const char *text, UiColor background = kColorOff) {
  Rgb565Surface surf(it);
  fast_print_impl(surf, x, y, UiFontView(font), color, to_ui_text_align(align), text, background);
}

template <typename DisplayT, typename FontT, typename AlignT>
inline void fast_print_alpha(DisplayT &it, int x, int y, FontT font, UiColor color,
                             AlignT align, const char *text, int alpha_q8) {
  Rgb565Surface surf(it);
  fast_print_alpha_impl(surf, x, y, UiFontView(font), color, to_ui_text_align(align), text, alpha_q8);
}

// ------------------------------------------------------------------------------
// 5. RGB565 Scanline Blitter & PSRAM ArtCanvasCache / Cache Drop Hooks
// ------------------------------------------------------------------------------
template <typename DisplayT>
inline void blit_rgb565_scanline(DisplayT &it, int start_x, int y, int span_w,
                                 const uint16_t *row_buf) {
  if (span_w <= 0 || !row_in_clip_band(y) || row_buf == nullptr) return;
  if (start_x < 0) {
    row_buf -= start_x;
    span_w += start_x;
    start_x = 0;
  }
  if (start_x + span_w > SCREEN_W) {
    span_w = SCREEN_W - start_x;
  }
  if (span_w <= 0) return;

  uint16_t *fb = active_frame_buf16();
  if (fb != nullptr) {
    uint16_t *dst = fb + static_cast<size_t>(y) * SCREEN_W + static_cast<size_t>(start_x);
    const int mode = active_frame_endian_mode();
    if (mode == 1 || mode < 0) {
      std::memcpy(dst, row_buf, static_cast<size_t>(span_w) * sizeof(uint16_t));
      return;
    }
    if (mode == 2) {
      for (int i = 0; i < span_w; i++) {
        dst[i] = bswap16_u16(row_buf[i]);
      }
      return;
    }
    for (int i = 0; i < span_w; i++) {
      dst[i] = encode_hw_rgb565_raw(row_buf[i]);
    }
    return;
  }

  int i = 0;
  while (i < span_w) {
    const uint16_t p = row_buf[i];
    int run = 1;
    while (i + run < span_w && row_buf[i + run] == p) {
      run++;
    }
    const uint8_t r5 = static_cast<uint8_t>((p >> 11) & 0x1FU);
    const uint8_t g6 = static_cast<uint8_t>((p >> 5) & 0x3FU);
    const uint8_t b5 = static_cast<uint8_t>(p & 0x1FU);
    const UiColor c{static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
                    static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
                    static_cast<uint8_t>((b5 << 3) | (b5 >> 2))};
    const int x0 = start_x + i;
    if (run == 1) {
      it.draw_pixel_at(x0, y, c);
    } else if (run == 2) {
      it.draw_pixel_at(x0, y, c);
      it.draw_pixel_at(x0 + 1, y, c);
    } else {
      it.horizontal_line(x0, y, run, c);
    }
    i += run;
  }
}

struct ArtCanvasCache {
  void *px_raw;
  uint16_t *px;
  const void *data;
  int16_t w, h;
  int16_t max_mul_q8;
  uint32_t sample;
  uint32_t mode;
  uint32_t last_use;
  int16_t row_x0[SCREEN_H];
  int16_t row_len[SCREEN_H];
};

inline constexpr int kArtDimMusicHud = 1;
inline constexpr int kArtDimMenu = 2;
inline constexpr int kArtDimLyrics = 3;
inline constexpr int kArtDimLyricsHud = 4;
inline constexpr int kArtCacheEntries = LATTER_ART_CACHE_ENTRIES;

// PSRAM byte-budgeted cache sizing so large landscape displays (e.g. 1280x800 = 2 MB/frame)
// never blow up PSRAM by blindly multiplying 3 * W * H * 2.
inline constexpr size_t art_cache_psram_budget_bytes(int psram_mb) noexcept {
  if (psram_mb >= 32) return 6144U * 1024U;
  if (psram_mb >= 16) return 3584U * 1024U;
  if (psram_mb >= 8) return 768U * 1024U;
  if (psram_mb >= 4) return 768U * 1024U;
  return 0U;
}

inline constexpr int compute_art_cache_slot_budget(int w, int h, int psram_mb,
                                                   int max_slots) noexcept {
  if (w <= 0 || h <= 0 || max_slots <= 0) return 0;
  const size_t frame_bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(uint16_t);
  const size_t budget = art_cache_psram_budget_bytes(psram_mb);
  if (frame_bytes == 0U || budget < frame_bytes) return 0;
  const size_t by_budget = budget / frame_bytes;
  return (by_budget < static_cast<size_t>(max_slots)) ? static_cast<int>(by_budget) : max_slots;
}

inline constexpr int kArtCacheActiveSlots =
    compute_art_cache_slot_budget(SCREEN_W, SCREEN_H, LATTER_PSRAM_MB, kArtCacheEntries);

inline constexpr size_t sky_cache_psram_budget_bytes(int psram_mb) noexcept {
  if (psram_mb >= 16) return 2304U * 1024U;
  if (psram_mb >= 4) return 1024U * 1024U;
  return 0U;
}

inline constexpr bool sky_base_cache_allowed(int w, int h, int psram_mb) noexcept {
  if (w <= 0 || h <= 0) return false;
  const size_t frame_bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(uint16_t);
  return frame_bytes <= sky_cache_psram_budget_bytes(psram_mb);
}

inline constexpr int compute_sky_cache_slot_budget(int w, int h, int psram_mb) noexcept {
  if (!sky_base_cache_allowed(w, h, psram_mb)) return 0;
  const size_t frame_bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(uint16_t);
  if (psram_mb >= 16 && frame_bytes <= 1536U * 1024U) return 2;
  return 1;
}

inline constexpr int kSkyCacheActiveSlots =
    compute_sky_cache_slot_budget(SCREEN_W, SCREEN_H, LATTER_PSRAM_MB);

struct ArtRect {
  int x0, y0, w, h;
};

int compute_art_row_scale_q8(int y, bool show_controls, bool show_metadata, int dim_level = 0);
void art_letterbox_bounds(bool show_metadata, bool show_controls, int &out_y0, int &out_y1);
ArtRect art_standard_rect(int cx, int src_w, int src_h, bool show_metadata, bool apply_scrim);
void art_canvas_drop(const void *data);
void art_canvas_trim_to_one() noexcept;
void art_canvas_drop_all() noexcept;
using PsramCacheEvictorFn = void (*)() noexcept;
bool register_psram_cache_evictor(PsramCacheEvictorFn fn) noexcept;
void evict_registered_psram_caches() noexcept;
bool render_album_art_canvas_view(Rgb565Surface &it, ArtImageView target_art, int cx,
                                  bool is_standard, bool is_letterbox, bool apply_scrim,
                                  bool show_metadata, int dim_level = 0);
bool render_lyrics_art_backdrop_view(Rgb565Surface &it, ArtImageView target_art, int cx,
                                     UiColor theme_primary, UiColor col_bg = kChromeShadow,
                                     int dim_level = kArtDimLyrics);
bool render_page_wallpaper_base_view(Rgb565Surface &it, ArtImageView wallpaper_img,
                                     bool has_wallpaper, int cx, UiColor col_bg, int dim_level = 0);

template <typename ImageT>
inline void release_art(ImageT *img) {
  if (img == nullptr) return;
  art_canvas_drop(img->get_data_start());
  img->release();
}

template <typename DisplayT>
inline bool render_album_art_canvas(DisplayT &it, ArtImageView target_art, int cx,
                                    bool is_standard, bool is_letterbox, bool apply_scrim,
                                    bool show_metadata, int dim_level = 0) {
  Rgb565Surface surf(it);
  return render_album_art_canvas_view(surf, target_art, cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
}

template <typename DisplayT>
inline bool render_lyrics_art_backdrop(DisplayT &it, ArtImageView target_art, int cx,
                                       UiColor theme_primary, UiColor col_bg = kChromeShadow,
                                       int dim_level = kArtDimLyrics) {
  Rgb565Surface surf(it);
  return render_lyrics_art_backdrop_view(surf, target_art, cx, theme_primary, col_bg, dim_level);
}

template <typename DisplayT>
inline bool render_page_wallpaper_base(DisplayT &it, ArtImageView wallpaper_img,
                                       bool has_wallpaper, int cx, UiColor col_bg, int dim_level = 0) {
  Rgb565Surface surf(it);
  return render_page_wallpaper_base_view(surf, wallpaper_img, has_wallpaper, cx, col_bg, dim_level);
}

// ------------------------------------------------------------------------------
// 6. Frame Base State (Dirty-Band Aware) & FrameProfileScope ([SLOW_FRAME])
// ------------------------------------------------------------------------------
struct FrameBaseState {
  bool painted = false;
  bool warned = false;
  bool has_color = false;
  uint16_t color_hw = 0;
};

FrameBaseState &frame_base_state();
void frame_base_begin();
void frame_base_skip();
void frame_base_claim();
bool frame_base_impl(Rgb565Surface &it, UiColor c);

template <typename DisplayT>
inline bool frame_base(DisplayT &it, UiColor c) {
  Rgb565Surface surf(it);
  return frame_base_impl(surf, c);
}

struct ProfLap {
  uint64_t t = 0;
  ProfLap() : t(latter_core::profiler_enabled() ? latter_core::prof_now_us() : 0) {}
  void mark(latter_core::ProfSect sect) {
    if (t == 0) return;
    const uint64_t now = latter_core::prof_now_us();
    latter_core::prof_sect_us()[static_cast<size_t>(sect)] += static_cast<uint32_t>(now - t);
    t = now;
  }
};

struct FrameProfileScope {
  static constexpr uint32_t kSlowFrameUs = (SCREEN_W * SCREEN_H > 300000) ? 110000U : 65000U;

  uint64_t t0_us = 0;
  uint32_t prev_disp_ms = 0;
  uint32_t prev_ambient_ms = 0;
  int page = 0;
  int sub = 0;
  int pwr = 0;

  FrameProfileScope(int p, int s, int pw);
  ~FrameProfileScope();
  void abort_idle();
};

}  // namespace latter_ui
