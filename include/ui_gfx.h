#pragma once

// ==============================================================================
// Latter UI Graphics Primitives, 2-Tier Frosted Glass (D-041), 1-bpp Span Cache,
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
void *ui_psram_malloc(size_t bytes) noexcept;
void *ui_psram_calloc(size_t count, size_t elem_size) noexcept;
void ui_psram_free(void *ptr) noexcept;

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
    uint16_t *dst = fb + static_cast<size_t>(y) * SCREEN_W + static_cast<size_t>(x);
    int i = 0;
    if ((reinterpret_cast<uintptr_t>(dst) & 2U) != 0U) {
      dst[0] = px;
      i = 1;
    }
    const uint32_t pair = static_cast<uint32_t>(px) | (static_cast<uint32_t>(px) << 16);
    uint32_t *dst32 = reinterpret_cast<uint32_t *>(dst + i);
    const int pairs = (w - i) >> 1;
    for (int p = 0; p < pairs; p++) {
      dst32[p] = pair;
    }
    i += pairs << 1;
    if (i < w) {
      dst[i] = px;
    }
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
  for (int i = 0; i < w; i++) dst[i] = pat[(x + i) & 3];
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
  const int rr = br + (((static_cast<int>(c.r) - br) * a + 127) / 255);
  const int rg = bgc + (((static_cast<int>(c.g) - bgc) * a + 127) / 255);
  const int rb = bb + (((static_cast<int>(c.b) - bb) * a + 127) / 255);
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

template <typename DisplayT>
inline void render_mdi_icon(DisplayT &it, std::string_view name, int cx, int cy, int size_px,
                            UiColor color, bool drop_shadow = true,
                            std::string_view fallback_name = "mdi:map-marker") {
  if (size_px <= 0) return;
  const int half = (size_px + 3) / 2;
  if ((cy + half) < clip_y0() || (cy - half) > clip_y1()) return;
  const std::string_view eff_name = has_icon(name) ? name : fallback_name;
  if (eff_name.empty()) return;
  if (drop_shadow) {
    rasterize_icon_fn(eff_name, cx + 1, cy + 1, size_px,
                      [&](int x0, int x1_excl, int y, uint8_t a) {
                        const int sa = (static_cast<int>(a) * 184) >> 8;
                        if (sa <= 0) return;
                        for (int px = x0; px < x1_excl; ++px) {
                          fast_pixel_blend(it, px, y, kChromeShadow, sa);
                        }
                      });
  }
  rasterize_icon_fn(eff_name, cx, cy, size_px,
                    [&](int x0, int x1_excl, int y, uint8_t a) {
                      if (a == 255) {
                        fast_hline(it, x0, y, x1_excl - x0, color);
                      } else if (a > 0) {
                        for (int px = x0; px < x1_excl; ++px) {
                          fast_pixel_blend(it, px, y, color, static_cast<int>(a));
                        }
                      }
                    });
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
static constexpr int kGlassAlphaFallQ8 = 4;

struct GlassBackdrop {
  UiColor bright{};
  UiColor mean{};
  bool valid = false;
};

const float *srgb_linear_lut();
float relative_luminance(UiColor c);
float contrast_ratio(float la, float lb);
UiColor mix_color_q8(UiColor c_under, UiColor c_over, int a);
GlassBackdrop sample_glass_backdrop(int x0, int y0, int w, int h);
GlassBackdrop sample_glass_mean(int x0, int y0, int w, int h);
bool is_semantic_glass_color(UiColor c);
void normalize_glass_chrome(UiColor caller_bg, UiColor caller_brd,
                            GlassVariant &inout_variant, UiColor &out_base_tint,
                            UiColor &out_rim_tint);
int glass_alpha_for_text(UiColor tint, UiColor backdrop, UiColor text,
                         int min_alpha_q8 = kGlassAlphaMinQ8);
int glass_alpha_smooth(int &state, int target, uint32_t dt_ms = 16U);
void resolve_glass_fill(const GlassBackdrop &bd, UiColor tint, UiColor ref_text,
                        UiColor &out_tint, int &out_a_top, int &out_a_bot,
                        int &out_rim_top, int &out_rim_bot, int explicit_alpha_q8 = -1,
                        GlassVariant variant = GlassVariant::Clear);
UiColor sample_glass_surface_color(int x0, int y0, int w, int h,
                                   UiColor tint = kChromeGlassTint,
                                   GlassVariant variant = GlassVariant::Clear);

void fill_round_rect_glass_impl(Rgb565Surface &it, int x0, int y0, int w, int h, float rad,
                                UiColor tint, int a_top, int a_bot, int hole = 0);

template <typename DisplayT>
inline void fill_round_rect_glass(DisplayT &it, int x0, int y0, int w, int h, float rad,
                                  UiColor tint, int a_top, int a_bot, int hole = 0) {
  Rgb565Surface surf(it);
  fill_round_rect_glass_impl(surf, x0, y0, w, h, rad, tint, a_top, a_bot, hole);
}

template <typename DisplayT>
inline void render_glass_card(DisplayT &it, int x0, int y0, int w, int h, int r,
                              UiColor tint, int alpha_q8, const GlassBackdrop &bd,
                              GlassVariant variant = GlassVariant::Clear,
                              UiColor ref_text = kChromeGlassSheen) {
  if (w <= 4 || h <= 4 || (y0 + h) <= clip_y0() || y0 > clip_y1()) return;
  r = std::max(2, std::min(r, std::min(w, h) / 2));
  GlassVariant eff_var = variant;
  UiColor norm_tint, rim_col;
  normalize_glass_chrome(tint, kChromeCardBorder, eff_var, norm_tint, rim_col);
  UiColor eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, norm_tint, ref_text, eff_tint, a_top, a_bot, rim_top, rim_bot, alpha_q8, eff_var);
  fill_round_rect_glass(it, x0, y0, w, h, static_cast<float>(r), rim_col, rim_top, rim_bot, 1);
  fill_round_rect_glass(it, x0 + 1, y0 + 1, w - 2, h - 2, static_cast<float>(r) - 1.0f, eff_tint, a_top, a_bot);
}

template <typename DisplayT>
inline void render_glass_card(DisplayT &it, int x0, int y0, int w, int h, int r, UiColor tint, int alpha_q8,
                              GlassVariant variant = GlassVariant::Clear,
                              UiColor ref_text = kChromeGlassSheen) {
  if (w <= 4 || h <= 4 || (y0 + h) <= clip_y0() || y0 > clip_y1()) return;
  const GlassBackdrop bd =
      (variant == GlassVariant::ReadableDark) ? sample_glass_backdrop(x0, y0, w, h) : GlassBackdrop{};
  render_glass_card(it, x0, y0, w, h, r, tint, alpha_q8, bd, variant, ref_text);
}

inline bool &backdrop_active() {
  static bool active = false;
  return active;
}

// ------------------------------------------------------------------------------
// 4. Compiled 1-bpp Span Glyph Cache, UTF-8 Sanitizer & Text Fitting
// ------------------------------------------------------------------------------
struct TextSpan {
  int16_t x;
  int16_t y;
  int16_t len;
};

struct TextCacheEntry {
  const void *font;
  uint32_t last_use;
  TextSpan *spans;
  uint16_t count;
  uint16_t cap;
  char text[96];
};

std::string sanitize_utf8_for_font(const std::string &str);
std::string fit_text_to_width_font(const std::string &raw_str, int max_w, UiFontView font);
int text_ink_right_edge_impl(Rgb565Surface &it, UiFontView font, const char *text);
void fast_print_impl(Rgb565Surface &it, int x, int y, UiFontView font,
                     UiColor color, UiTextAlign align, const char *text,
                     UiColor background = kColorOff);
void fast_print_alpha_impl(Rgb565Surface &it, int x, int y, UiFontView font,
                           UiColor color, UiTextAlign align, const char *text,
                           int alpha_q8);

template <typename DisplayT, typename FontT>
inline int text_ink_right_edge(DisplayT &it, FontT font, const char *text) {
  Rgb565Surface surf(it);
  return text_ink_right_edge_impl(surf, UiFontView(font), text);
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
                                 const uint16_t *row_buf, bool skip_black = false) {
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
    if (!skip_black) {
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
    }
    for (int i = 0; i < span_w; i++) {
      const uint16_t p = row_buf[i];
      if (skip_black && (p & 0xFFDFU) == 0U) continue;
      dst[i] = encode_hw_rgb565_raw(p);
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
    if (skip_black && (p & 0xFFDFU) == 0U) {
      i += run;
      continue;
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
  uint16_t *px;
  const void *data;
  int16_t w, h;
  uint32_t sample;
  uint32_t mode;
  uint32_t last_use;
  int16_t row_x0[SCREEN_H];
  int16_t row_len[SCREEN_H];
};

inline constexpr int kArtDimMenu = 2;
inline constexpr int kArtDimLyrics = 3;
inline constexpr int kArtCacheEntries = LATTER_ART_CACHE_ENTRIES;

struct ArtRect {
  int x0, y0, w, h;
};

int compute_art_row_scale_q8(int y, bool show_controls, bool show_metadata, int dim_level = 0);
void art_letterbox_bounds(bool show_metadata, bool show_controls, int &out_y0, int &out_y1);
ArtRect art_standard_rect(int cx, int src_w, int src_h, bool show_metadata, bool apply_scrim);
void art_canvas_drop(const void *data);
void art_canvas_drop_all() noexcept;
void sky_base_cache_drop() noexcept;
void weather_cloud_cache_drop() noexcept;
void game_arena_drop() noexcept;
UiColor sample_image_ambient_tint_view(ArtImageView img, UiColor base_bg = kChromeGlassTint);
bool render_album_art_canvas_view(Rgb565Surface &it, ArtImageView target_art, int cx,
                                  bool is_standard, bool is_letterbox, bool apply_scrim,
                                  bool show_metadata, int dim_level = 0);
bool render_lyrics_art_backdrop_view(Rgb565Surface &it, ArtImageView target_art, int cx,
                                     UiColor theme_primary, UiColor col_bg = kChromeShadow);
bool render_page_wallpaper_base_view(Rgb565Surface &it, ArtImageView wallpaper_img,
                                     bool has_wallpaper, int cx, UiColor col_bg, int dim_level = 0);

template <typename ImageT>
inline void release_art(ImageT *img) {
  if (img == nullptr) return;
  art_canvas_drop(img->get_data_start());
  img->release();
}

inline UiColor sample_image_ambient_tint(ArtImageView img, UiColor base_bg = kChromeGlassTint) {
  return sample_image_ambient_tint_view(img, base_bg);
}

template <typename ImageT>
  requires(!std::is_same_v<std::remove_cvref_t<ImageT>, ArtImageView>)
inline UiColor sample_image_ambient_tint(ImageT *img, UiColor base_bg = kChromeGlassTint) {
  return sample_image_ambient_tint_view(make_art_view(img), base_bg);
}

template <typename DisplayT>
inline bool render_album_art_canvas(DisplayT &it, ArtImageView target_art, int cx,
                                    bool is_standard, bool is_letterbox, bool apply_scrim,
                                    bool show_metadata, int dim_level = 0) {
  Rgb565Surface surf(it);
  return render_album_art_canvas_view(surf, target_art, cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
}

template <typename DisplayT, typename ImageT>
  requires(!std::is_same_v<std::remove_cvref_t<ImageT>, ArtImageView>)
inline bool render_album_art_canvas(DisplayT &it, ImageT *target_art, int cx,
                                    bool is_standard, bool is_letterbox, bool apply_scrim,
                                    bool show_metadata, int dim_level = 0) {
  Rgb565Surface surf(it);
  return render_album_art_canvas_view(surf, make_art_view(target_art), cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
}

template <typename DisplayT>
inline bool render_lyrics_art_backdrop(DisplayT &it, ArtImageView target_art, int cx,
                                       UiColor theme_primary, UiColor col_bg = kChromeShadow) {
  Rgb565Surface surf(it);
  return render_lyrics_art_backdrop_view(surf, target_art, cx, theme_primary, col_bg);
}

template <typename DisplayT, typename ImageT>
  requires(!std::is_same_v<std::remove_cvref_t<ImageT>, ArtImageView>)
inline bool render_lyrics_art_backdrop(DisplayT &it, ImageT *target_art, int cx,
                                       UiColor theme_primary, UiColor col_bg = kChromeShadow) {
  Rgb565Surface surf(it);
  return render_lyrics_art_backdrop_view(surf, make_art_view(target_art), cx, theme_primary, col_bg);
}

template <typename DisplayT>
inline bool render_page_wallpaper_base(DisplayT &it, ArtImageView wallpaper_img,
                                       bool has_wallpaper, int cx, UiColor col_bg, int dim_level = 0) {
  Rgb565Surface surf(it);
  return render_page_wallpaper_base_view(surf, wallpaper_img, has_wallpaper, cx, col_bg, dim_level);
}

template <typename DisplayT, typename ImageT>
  requires(!std::is_same_v<std::remove_cvref_t<ImageT>, ArtImageView>)
inline bool render_page_wallpaper_base(DisplayT &it, ImageT *wallpaper_img, bool has_wallpaper,
                                       int cx, UiColor col_bg, int dim_level = 0) {
  Rgb565Surface surf(it);
  return render_page_wallpaper_base_view(surf, make_art_view(wallpaper_img), has_wallpaper, cx, col_bg, dim_level);
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
  ProfLap() : t(profiler_enabled() ? prof_now_us() : 0) {}
  void mark(ProfSect sect) {
    if (t == 0) return;
    const uint64_t now = prof_now_us();
    prof_sect_us()[static_cast<size_t>(sect)] += static_cast<uint32_t>(now - t);
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
