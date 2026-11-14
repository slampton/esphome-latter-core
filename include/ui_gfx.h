#pragma once

// ==============================================================================
// Latter UI Graphics Primitives, WCAG 4.5:1 Frosted Glass, 1-bpp Span Cache,
// ArtCanvasCache, Dirty-Band Frame Base & FrameProfileScope (include/ui_gfx.h)
// ==============================================================================

#include "esphome/core/defines.h"
#include "ui_types.h"
#include "esphome_bridge.h"
#include "latter_prof.h"
#include "esphome/core/component.h"
#include "esphome/components/display/display.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) && __has_include("esphome/components/mipi_dsi/mipi_dsi.h")
#include "esphome/components/mipi_dsi/mipi_dsi.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

namespace latter_ui {

// ------------------------------------------------------------------------------
// 1. Direct RGB565 Software Framebuffer Binding & Hardware Endianness Encoding
// ------------------------------------------------------------------------------
template <typename DispT, bool IsFinal = std::is_final_v<DispT>>
struct DispBufferAccessor {
  static uint8_t *get(DispT *) { return nullptr; }
};

template <typename DispT>
struct DispBufferAccessor<DispT, false> : DispT {
  static uint8_t *get(DispT *d) {
    if constexpr (requires { &DispBufferAccessor::buffer_; }) {
      return reinterpret_cast<uint8_t *>(d->*(&DispBufferAccessor::buffer_));
    } else {
      return nullptr;
    }
  }
};

#if defined(USE_ESP32_VARIANT_ESP32P4) && __has_include("esphome/components/mipi_dsi/mipi_dsi.h")
template <>
struct DispBufferAccessor<esphome::mipi_dsi::MipiDsi, true> {
  static uint8_t *get(esphome::mipi_dsi::MipiDsi *d) {
    return ::latter_hal::get_mipi_dsi_buffer(d);
  }
};
#endif

inline uint16_t *&active_frame_buf16() {
  static uint16_t *ptr = nullptr;
  return ptr;
}

// 0 = fallback, 1 = LE RGB565 (0xF800), 2 = BE RGB565 (0x00F8), 3 = LE BGR565 (0x001F), 4 = BE BGR565 (0x1F00)
inline int &active_frame_endian_mode() {
  static int mode = -1;
  return mode;
}

void probe_frame_buffer_endian(esphome::display::Display &it, uint16_t *buf16);
void fb_clear_bind(esphome::Component *owner, uint16_t *fb);
void fb_clear_after_update();
void fb_clear_wait();
bool fb_clear_consume(uint16_t hw);
void fb_clear_frame_end();

template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void bind_frame_buffer(DispT *disp, esphome::display::Display &it) {
  fb_clear_bind(nullptr, nullptr);
  if (disp == nullptr || it.get_rotation() != esphome::display::DISPLAY_ROTATION_0_DEGREES) {
    active_frame_buf16() = nullptr;
    return;
  }
  uint8_t *raw = DispBufferAccessor<DispT>::get(disp);
  if (raw == nullptr) {
    active_frame_buf16() = nullptr;
    return;
  }
  uint16_t *buf16 = reinterpret_cast<uint16_t *>(raw);
  if (active_frame_endian_mode() < 0) {
    probe_frame_buffer_endian(it, buf16);
  }
  active_frame_buf16() = (active_frame_endian_mode() > 0) ? buf16 : nullptr;
  fb_clear_bind(static_cast<esphome::Component *>(disp), active_frame_buf16());
}

template <typename DispT>
inline void bind_frame_buffer(DispT *const *disp_ptr, esphome::display::Display &it) {
  if (disp_ptr != nullptr) bind_frame_buffer(*disp_ptr, it);
}

inline constexpr uint16_t bswap16_u16(uint16_t v) noexcept {
  return static_cast<uint16_t>((v >> 8) | (v << 8));
}

template <int Mode>
inline constexpr uint16_t encode_hw_rgb565_mode(uint16_t p) noexcept {
  if constexpr (Mode == 1) return p;
  if constexpr (Mode == 2) return bswap16_u16(p);
  const uint16_t bgr = static_cast<uint16_t>(((p & 0x001FU) << 11) | (p & 0x07E0U) | ((p >> 11) & 0x001FU));
  if constexpr (Mode == 4) return bswap16_u16(bgr);
  return bgr;
}

template <int Mode>
inline constexpr uint16_t decode_hw_rgb565_mode(uint16_t hw) noexcept {
  if constexpr (Mode == 1) return hw;
  if constexpr (Mode == 2) return bswap16_u16(hw);
  const uint16_t bgr = (Mode == 4) ? bswap16_u16(hw) : hw;
  return static_cast<uint16_t>(((bgr & 0x001FU) << 11) | (bgr & 0x07E0U) | ((bgr >> 11) & 0x001FU));
}

inline uint16_t encode_hw_rgb565_raw(uint16_t p) {
  const int mode = active_frame_endian_mode();
  if (mode == 1) return encode_hw_rgb565_mode<1>(p);
  if (mode == 2) return encode_hw_rgb565_mode<2>(p);
  return (mode == 4) ? encode_hw_rgb565_mode<4>(p) : encode_hw_rgb565_mode<3>(p);
}

inline uint16_t encode_hw_rgb565(esphome::Color c) {
  const uint16_t p = static_cast<uint16_t>(((static_cast<uint16_t>(c.r) & 0xF8U) << 8) |
                                           ((static_cast<uint16_t>(c.g) & 0xFCU) << 3) |
                                           (static_cast<uint16_t>(c.b) >> 3));
  return encode_hw_rgb565_raw(p);
}

inline uint16_t decode_hw_rgb565_raw(uint16_t hw) {
  const int mode = active_frame_endian_mode();
  if (mode == 1) return decode_hw_rgb565_mode<1>(hw);
  if (mode == 2) return decode_hw_rgb565_mode<2>(hw);
  return (mode == 4) ? decode_hw_rgb565_mode<4>(hw) : decode_hw_rgb565_mode<3>(hw);
}

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
inline void fast_pixel(DisplayT &it, int x, int y, esphome::Color c) {
  if (x < 0 || x >= SCREEN_W || !row_in_clip_band(y)) return;
  uint16_t *fb = active_frame_buf16();
  if (fb != nullptr) {
    fb[y * SCREEN_W + x] = encode_hw_rgb565(c);
    return;
  }
  it.draw_pixel_at(x, y, c);
}

template <typename DisplayT>
inline void fast_hline(DisplayT &it, int x, int y, int w, esphome::Color c) {
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
    uint16_t *dst = fb + y * SCREEN_W + x;
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
inline void fill_rect_row_major(DisplayT &it, int x, int y, int w, int h, esphome::Color c) {
  if (w <= 0 || h <= 0) return;
  const int cy0 = std::max(y, clip_y0());
  const int cy1 = std::min(y + h - 1, clip_y1());
  for (int row = cy0; row <= cy1; row++) {
    fast_hline(it, x, row, w, c);
  }
}

template <typename DisplayT>
inline void fast_vline(DisplayT &it, int x, int y, int h, esphome::Color c) {
  if (h <= 0 || x < 0 || x >= SCREEN_W) return;
  const int cy0 = std::max(y, clip_y0());
  const int cy1 = std::min(y + h - 1, clip_y1());
  if (cy1 < cy0) return;
  y = cy0;
  h = cy1 - cy0 + 1;
  uint16_t *fb = active_frame_buf16();
  if (fb != nullptr) {
    const uint16_t px = encode_hw_rgb565(c);
    uint16_t *dst = fb + y * SCREEN_W + x;
    for (int i = 0; i < h; i++, dst += SCREEN_W) {
      *dst = px;
    }
    return;
  }
  it.vertical_line(x, y, h, c);
}

template <typename DisplayT>
inline void fast_rect_outline(DisplayT &it, int x, int y, int w, int h, esphome::Color c) {
  fast_hline(it, x, y, w, c);
  fast_hline(it, x, y + h - 1, w, c);
  fast_vline(it, x, y, h, c);
  fast_vline(it, x + w - 1, y, h, c);
}

template <typename DisplayT>
inline void fast_filled_circle(DisplayT &it, int cx, int cy, int r, esphome::Color c) {
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
inline void fast_circle(DisplayT &it, int cx, int cy, int r, esphome::Color c) {
  if (r < 0 || cy + r < clip_y0() || cy - r > clip_y1()) return;
  uint16_t *fb = active_frame_buf16();
  const int cy0 = clip_y0();
  const int cy1 = clip_y1();
  const uint16_t px = (fb != nullptr) ? encode_hw_rgb565(c) : 0;
  auto put = [&](int x, int y) {
    if (static_cast<unsigned>(x) < static_cast<unsigned>(SCREEN_W) && y >= cy0 && y <= cy1) {
      if (fb != nullptr) fb[y * SCREEN_W + x] = px;
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
inline void fast_line(DisplayT &it, int x1, int y1, int x2, int y2, esphome::Color c) {
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
      if (fb != nullptr) fb[y1 * SCREEN_W + x1] = px;
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
                           esphome::Color core_col, esphome::Color halo_col) {
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
                                           esphome::Color c) {
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
                                 esphome::Color c) {
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
    const esphome::Color c{static_cast<uint8_t>(r88 >> 8),
                           static_cast<uint8_t>(g88 >> 8),
                           static_cast<uint8_t>(b88 >> 8)};
    fast_hline(it, x, y, w, c);
    return;
  }
  const uint8_t *th = kBayer4[y & 3];
  uint16_t pat[4];
  for (int k = 0; k < 4; k++) pat[k] = encode_hw_rgb565_raw(dither_rgb565(r88, g88, b88, th[k]));
  uint16_t *dst = fb + y * SCREEN_W + x;
  for (int i = 0; i < w; i++) dst[i] = pat[(x + i) & 3];
}

template <typename DisplayT>
inline void fast_pixel_blend(DisplayT &it, int x, int y, esphome::Color c, int a) {
  if (a <= 0 || x < 0 || x >= SCREEN_W || !row_in_clip_band(y)) return;
  uint16_t *fb = active_frame_buf16();
  if (fb == nullptr) {
    if (a >= 128) fast_pixel(it, x, y, c);
    return;
  }
  uint16_t &dst = fb[y * SCREEN_W + x];
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
  const esphome::Color blended{static_cast<uint8_t>(rr), static_cast<uint8_t>(rg), static_cast<uint8_t>(rb)};
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

template <typename DisplayT>
inline void fill_round_rect_aa(DisplayT &it, int x0, int y0, int w, int h, float rad,
                               esphome::Color c_top, esphome::Color c_bot, int hole = 0) {
  if (w <= 0 || h <= 0) return;
  const int ys = std::max(clip_y0(), y0);
  const int ye = std::min(clip_y1() + 1, y0 + h);
  if (ye <= ys) return;
  AaRoundRect o;
  o.init(x0, y0, w, h, rad);
  AaRoundRect in;
  const bool has_hole = (hole > 0 && w > 2 * hole && h > 2 * hole);
  if (has_hole) in.init(x0 + hole, y0 + hole, w - 2 * hole, h - 2 * hole, rad - static_cast<float>(hole));
  const bool flat = (c_top.r == c_bot.r && c_top.g == c_bot.g && c_top.b == c_bot.b);
  for (int py = ys; py < ye; py++) {
    float dy = 0.0f;
    int s0 = 0, s1 = -1, e0 = 0, e1 = -1;
    if (!o.row(py, dy, s0, s1, e0, e1)) continue;
    uint32_t r88 = static_cast<uint32_t>(c_top.r) << 8;
    uint32_t g88 = static_cast<uint32_t>(c_top.g) << 8;
    uint32_t b88 = static_cast<uint32_t>(c_top.b) << 8;
    esphome::Color rc = c_top;
    if (!flat && h > 1) {
      const int num = py - y0;
      const int den = h - 1;
      r88 = static_cast<uint32_t>((static_cast<int>(c_top.r) << 8) + ((static_cast<int>(c_bot.r) - static_cast<int>(c_top.r)) * 256 * num) / den);
      g88 = static_cast<uint32_t>((static_cast<int>(c_top.g) << 8) + ((static_cast<int>(c_bot.g) - static_cast<int>(c_top.g)) * 256 * num) / den);
      b88 = static_cast<uint32_t>((static_cast<int>(c_top.b) << 8) + ((static_cast<int>(c_bot.b) - static_cast<int>(c_top.b)) * 256 * num) / den);
      rc = {static_cast<uint8_t>(r88 >> 8), static_cast<uint8_t>(g88 >> 8), static_cast<uint8_t>(b88 >> 8)};
    }
    auto span = [&](int a, int b) {
      if (b < a) return;
      if (flat) fast_hline(it, a, py, b - a + 1, rc);
      else fast_hline_dither(it, a, py, b - a + 1, r88, g88, b88);
    };
    if (s1 >= s0) {
      int hs0 = 1, hs1 = 0;
      float idy = 0.0f;
      int ie0 = 0, ie1 = -1;
      const bool in_hit = has_hole && in.row(py, idy, hs0, hs1, ie0, ie1);
      if (in_hit && hole > 1) {
        span(s0, std::min(s1, ie0 - 1));
        span(std::max(s0, ie1 + 1), s1);
        if (hs1 >= hs0) {
          for (int px = std::max(s0, ie0); px <= std::min(s1, hs0 - 1); px++) {
            fast_pixel_blend(it, px, py, rc, 255 - in.coverage(px, idy));
          }
          for (int px = std::max(s0, hs1 + 1); px <= std::min(s1, ie1); px++) {
            fast_pixel_blend(it, px, py, rc, 255 - in.coverage(px, idy));
          }
        } else {
          for (int px = std::max(s0, ie0); px <= std::min(s1, ie1); px++) {
            fast_pixel_blend(it, px, py, rc, 255 - in.coverage(px, idy));
          }
        }
      } else if (in_hit && hs1 >= hs0) {
        span(s0, std::min(s1, hs0 - 1));
        span(std::max(s0, hs1 + 1), s1);
      } else {
        span(s0, s1);
      }
    }
    if (s1 < s0) {
      for (int px = e0; px <= e1; px++) fast_pixel_blend(it, px, py, rc, o.coverage(px, dy));
    } else {
      for (int px = e0; px < s0; px++) fast_pixel_blend(it, px, py, rc, o.coverage(px, dy));
      for (int px = s1 + 1; px <= e1; px++) fast_pixel_blend(it, px, py, rc, o.coverage(px, dy));
    }
  }
}

template <typename DisplayT>
inline void fill_round_rect_aa(DisplayT &it, int x0, int y0, int w, int h, float rad, esphome::Color c) {
  fill_round_rect_aa(it, x0, y0, w, h, rad, c, c, 0);
}

template <typename DisplayT>
inline void stroke_round_rect_aa(DisplayT &it, int x0, int y0, int w, int h, float rad,
                                 esphome::Color c_top, esphome::Color c_bot, int thick = 1) {
  fill_round_rect_aa(it, x0, y0, w, h, rad, c_top, c_bot, thick);
}

template <typename DisplayT>
inline void fast_filled_circle_aa(DisplayT &it, int cx, int cy, int r, esphome::Color c) {
  if (r < 0) return;
  fill_round_rect_aa(it, cx - r, cy - r, 2 * r + 1, 2 * r + 1, static_cast<float>(r) + 0.5f, c, c);
}

template <typename DisplayT>
inline void fast_circle_aa(DisplayT &it, int cx, int cy, float r, esphome::Color c, float thick = 1.5f) {
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

static constexpr int kGlassAlphaMinQ8 = 148;          // ~58% Clear base floor
static constexpr int kGlassReadableAlphaMinQ8 = 208;  // ~81% ReadableDark base floor (200..216/256)
static constexpr int kGlassSemanticWashMaxQ8 = 38;    // <= 15% semantic interior wash
static constexpr int kGlassSemanticRimMaxQ8 = 64;     // <= 25% semantic specular rim tint
static constexpr float kGlassTextContrast = 5.0f;
static constexpr float kGlassTextContrastMin = 4.5f;
static constexpr int kGlassBrightPercent = 3;
static constexpr int kGlassSheenTopQ8 = 92;
static constexpr int kGlassSheenBotQ8 = 28;
static constexpr int kGlassAlphaFallQ8 = 4;

struct GlassBackdrop {
  esphome::Color bright{};
  esphome::Color mean{};
  bool valid = false;
};

const float *srgb_linear_lut();
float relative_luminance(esphome::Color c);
float contrast_ratio(float la, float lb);
esphome::Color mix_color_q8(esphome::Color c_under, esphome::Color c_over, int a);
GlassBackdrop sample_glass_backdrop(int x0, int y0, int w, int h);
bool is_semantic_glass_color(esphome::Color c);
void normalize_glass_chrome(esphome::Color caller_bg, esphome::Color caller_brd,
                            GlassVariant &inout_variant, esphome::Color &out_base_tint,
                            esphome::Color &out_rim_tint);
int glass_alpha_for_text(esphome::Color tint, esphome::Color backdrop, esphome::Color text,
                         int min_alpha_q8 = kGlassAlphaMinQ8);
int glass_alpha_smooth(int &state, int target, uint32_t dt_ms = 16U);
void resolve_glass_fill(const GlassBackdrop &bd, esphome::Color tint, esphome::Color ref_text,
                        esphome::Color &out_tint, int &out_a_top, int &out_a_bot,
                        int &out_rim_top, int &out_rim_bot, int explicit_alpha_q8 = -1,
                        GlassVariant variant = GlassVariant::Clear);
esphome::Color sample_glass_surface_color(int x0, int y0, int w, int h,
                                          esphome::Color tint = kChromeGlassTint,
                                          GlassVariant variant = GlassVariant::Clear);

template <int Mode>
inline void fill_round_rect_glass_mode(uint16_t *fb, int x0, int y0, int w, int h, float rad,
                                       esphome::Color tint, int a_top, int a_bot, int hole) {
  const int ys = std::max(clip_y0(), y0);
  const int ye = std::min(clip_y1() + 1, y0 + h);
  if (ye <= ys) return;
  AaRoundRect o;
  o.init(x0, y0, w, h, rad);
  AaRoundRect in;
  const bool has_hole = (hole > 0 && w > 2 * hole && h > 2 * hole);
  if (has_hole) in.init(x0 + hole, y0 + hole, w - 2 * hole, h - 2 * hole, rad - static_cast<float>(hole));
  const uint32_t tr = (static_cast<uint32_t>(tint.r) * 31U * 256U + 127U) / 255U;
  const uint32_t tg = (static_cast<uint32_t>(tint.g) * 63U * 256U + 127U) / 255U;
  const uint32_t tb = (static_cast<uint32_t>(tint.b) * 31U * 256U + 127U) / 255U;
  auto mix = [&](uint16_t &dst, int a, int t) {
    const uint16_t bg = decode_hw_rgb565_mode<Mode>(dst);
    const uint32_t ia = static_cast<uint32_t>(256 - a);
    const uint32_t ua = static_cast<uint32_t>(a);
    uint32_t r = ((tr * ua) / 256U + ((bg >> 11) & 0x1FU) * ia + static_cast<uint32_t>(t)) >> 8;
    uint32_t g = ((tg * ua) / 256U + ((bg >> 5) & 0x3FU) * ia + static_cast<uint32_t>(t)) >> 8;
    uint32_t b = ((tb * ua) / 256U + (bg & 0x1FU) * ia + static_cast<uint32_t>(t)) >> 8;
    if (r > 31U) r = 31U;
    if (g > 63U) g = 63U;
    if (b > 31U) b = 31U;
    dst = encode_hw_rgb565_mode<Mode>(static_cast<uint16_t>((r << 11) | (g << 5) | b));
  };
  for (int py = ys; py < ye; py++) {
    float dy = 0.0f;
    int s0 = 0, s1 = -1, e0 = 0, e1 = -1;
    if (!o.row(py, dy, s0, s1, e0, e1)) continue;
    const int a = (h > 1) ? (a_top + ((a_bot - a_top) * (py - y0)) / (h - 1)) : a_top;
    if (a <= 0) continue;
    const uint8_t *th = kBayer4[py & 3];
    uint16_t *row = fb + py * SCREEN_W;
    const uint32_t ua = static_cast<uint32_t>(a);
    const uint32_t ia = static_cast<uint32_t>(256 - a);
    const uint32_t tr_a = (tr * ua) >> 8;
    const uint32_t tg_a = (tg * ua) >> 8;
    const uint32_t tb_a = (tb * ua) >> 8;
    uint32_t base_r[4], base_g[4], base_b[4];
    for (int k = 0; k < 4; k++) {
      const uint32_t tk = static_cast<uint32_t>(th[k]) * 16U + 8U;
      base_r[k] = tr_a + tk;
      base_g[k] = tg_a + tk;
      base_b[k] = tb_a + tk;
    }
    auto span = [&](int xa, int xb) {
      xa = std::max(xa, 0);
      xb = std::min(xb, SCREEN_W - 1);
      for (int x = xa; x <= xb; x++) {
        const uint16_t bg = decode_hw_rgb565_mode<Mode>(row[x]);
        const int k = x & 3;
        uint32_t r = (base_r[k] + ((bg >> 11) & 0x1FU) * ia) >> 8;
        uint32_t g = (base_g[k] + ((bg >> 5) & 0x3FU) * ia) >> 8;
        uint32_t b = (base_b[k] + (bg & 0x1FU) * ia) >> 8;
        if (r > 31U) r = 31U;
        if (g > 63U) g = 63U;
        if (b > 31U) b = 31U;
        row[x] = encode_hw_rgb565_mode<Mode>(static_cast<uint16_t>((r << 11) | (g << 5) | b));
      }
    };
    auto edge = [&](int px) {
      if (px < 0 || px >= SCREEN_W) return;
      const int cov = o.coverage(px, dy);
      if (cov <= 0) return;
      mix(row[px], (a * cov + 127) / 255, th[px & 3] * 16 + 8);
    };
    auto inner_edge = [&](int px, float idy) {
      if (px < 0 || px >= SCREEN_W) return;
      const int cov = 255 - in.coverage(px, idy);
      if (cov <= 0) return;
      mix(row[px], (a * cov + 127) / 255, th[px & 3] * 16 + 8);
    };
    if (s1 >= s0) {
      int hs0 = 1, hs1 = 0;
      float idy = 0.0f;
      int ie0 = 0, ie1 = -1;
      const bool in_hit = has_hole && in.row(py, idy, hs0, hs1, ie0, ie1);
      if (in_hit && hole > 1) {
        span(s0, std::min(s1, ie0 - 1));
        span(std::max(s0, ie1 + 1), s1);
        if (hs1 >= hs0) {
          for (int px = std::max(s0, ie0); px <= std::min(s1, hs0 - 1); px++) inner_edge(px, idy);
          for (int px = std::max(s0, hs1 + 1); px <= std::min(s1, ie1); px++) inner_edge(px, idy);
        } else {
          for (int px = std::max(s0, ie0); px <= std::min(s1, ie1); px++) inner_edge(px, idy);
        }
      } else if (in_hit && hs1 >= hs0) {
        span(s0, std::min(s1, hs0 - 1));
        span(std::max(s0, hs1 + 1), s1);
      } else {
        span(s0, s1);
      }
      for (int px = e0; px < s0; px++) edge(px);
      for (int px = s1 + 1; px <= e1; px++) edge(px);
    } else {
      for (int px = e0; px <= e1; px++) edge(px);
    }
  }
}

template <typename DisplayT>
inline void fill_round_rect_glass(DisplayT &it, int x0, int y0, int w, int h, float rad,
                                  esphome::Color tint, int a_top, int a_bot, int hole = 0) {
  if (w <= 0 || h <= 0) return;
  if (y0 + h <= clip_y0() || y0 > clip_y1()) return;
  uint16_t *fb = active_frame_buf16();
  if (fb == nullptr) {
    fill_round_rect_aa(it, x0, y0, w, h, rad, tint, tint, hole);
    return;
  }
  const int mode = active_frame_endian_mode();
  if (mode == 1) {
    fill_round_rect_glass_mode<1>(fb, x0, y0, w, h, rad, tint, a_top, a_bot, hole);
  } else if (mode == 2) {
    fill_round_rect_glass_mode<2>(fb, x0, y0, w, h, rad, tint, a_top, a_bot, hole);
  } else if (mode == 4) {
    fill_round_rect_glass_mode<4>(fb, x0, y0, w, h, rad, tint, a_top, a_bot, hole);
  } else {
    fill_round_rect_glass_mode<3>(fb, x0, y0, w, h, rad, tint, a_top, a_bot, hole);
  }
}

template <typename DisplayT>
inline void render_glass_card(DisplayT &it, int x0, int y0, int w, int h, int r,
                              esphome::Color tint, int alpha_q8, const GlassBackdrop &bd,
                              GlassVariant variant = GlassVariant::Clear,
                              esphome::Color ref_text = kChromeGlassSheen) {
  if (w <= 4 || h <= 4 || (y0 + h) <= clip_y0() || y0 > clip_y1()) return;
  r = std::max(2, std::min(r, std::min(w, h) / 2));
  GlassVariant eff_var = variant;
  esphome::Color norm_tint, rim_col;
  normalize_glass_chrome(tint, kChromeCardBorder, eff_var, norm_tint, rim_col);
  esphome::Color eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, norm_tint, ref_text, eff_tint, a_top, a_bot, rim_top, rim_bot, alpha_q8, eff_var);
  fill_round_rect_glass(it, x0, y0, w, h, static_cast<float>(r), rim_col, rim_top, rim_bot, 1);
  fill_round_rect_glass(it, x0 + 1, y0 + 1, w - 2, h - 2, static_cast<float>(r) - 1.0f, eff_tint, a_top, a_bot);
}

template <typename DisplayT>
inline void render_glass_card(DisplayT &it, int x0, int y0, int w, int h, int r, esphome::Color tint, int alpha_q8,
                              GlassVariant variant = GlassVariant::Clear,
                              esphome::Color ref_text = kChromeGlassSheen) {
  if (w <= 4 || h <= 4 || (y0 + h) <= clip_y0() || y0 > clip_y1()) return;
  render_glass_card(it, x0, y0, w, h, r, tint, alpha_q8, sample_glass_backdrop(x0, y0, w, h), variant, ref_text);
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
std::string fit_text_to_width_font_base(const std::string &raw_str, int max_w, esphome::display::BaseFont *font);
void fast_print_impl(esphome::display::Display &it, int x, int y, esphome::display::BaseFont *font,
                     esphome::Color color, esphome::display::TextAlign align, const char *text,
                     esphome::Color background);

template <typename FontT>
inline std::string fit_text_to_width_font(const std::string &raw_str, int max_w, FontT *font) {
  return fit_text_to_width_font_base(raw_str, max_w, static_cast<esphome::display::BaseFont *>(font));
}

template <typename DisplayT, typename FontT>
inline void fast_print(DisplayT &it, int x, int y, FontT *font, esphome::Color color,
                       esphome::display::TextAlign align, const char *text,
                       esphome::Color background = esphome::display::COLOR_OFF) {
  fast_print_impl(it, x, y, static_cast<esphome::display::BaseFont *>(font), color, align, text, background);
}

// ------------------------------------------------------------------------------
// 5. RGB565 Scanline Blitter, ArtImageView & PSRAM ArtCanvasCache
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
    uint16_t *dst = fb + y * SCREEN_W + start_x;
    const int mode = active_frame_endian_mode();
    if (!skip_black) {
      if (mode == 1) {
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
    const esphome::Color c{static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
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

struct ArtImageView {
  const uint16_t *data = nullptr;
  int width = 0;
  int height = 0;

  constexpr bool valid() const { return data != nullptr && width > 0 && height > 0; }
  constexpr int get_width() const { return width; }
  constexpr int get_height() const { return height; }
  inline const uint8_t *get_data_start() const { return reinterpret_cast<const uint8_t *>(data); }
};

inline ArtImageView make_art_view(const ArtImageView &v) { return v; }
inline ArtImageView make_art_view(const ArtImageView *v) { return (v != nullptr) ? *v : ArtImageView{}; }

template <typename ImageT>
  requires(!std::is_same_v<std::remove_cvref_t<ImageT>, ArtImageView>)
inline ArtImageView make_art_view(ImageT *img) {
  if (img == nullptr) return {};
  return {
      reinterpret_cast<const uint16_t *>(img->get_data_start()),
      img->get_width(),
      img->get_height(),
  };
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

struct ArtRect {
  int x0, y0, w, h;
};

ArtRect art_standard_rect(int cx, int src_w, int src_h, bool show_metadata, bool apply_scrim);
void art_canvas_drop(const void *data);
esphome::Color sample_image_ambient_tint_view(ArtImageView img, esphome::Color base_bg = kChromeGlassTint);
bool render_album_art_canvas_view(esphome::display::Display &it, ArtImageView target_art, int cx,
                                  bool is_standard, bool is_letterbox, bool apply_scrim,
                                  bool show_metadata, int dim_level = 0);
bool render_page_wallpaper_base_view(esphome::display::Display &it, ArtImageView wallpaper_img,
                                     bool has_wallpaper, int cx, esphome::Color col_bg, int dim_level = 0);

template <typename ImageT>
inline void release_art(ImageT *img) {
  if (img == nullptr) return;
  art_canvas_drop(img->get_data_start());
  img->release();
}

inline esphome::Color sample_image_ambient_tint(ArtImageView img, esphome::Color base_bg = kChromeGlassTint) {
  return sample_image_ambient_tint_view(img, base_bg);
}

template <typename ImageT>
  requires(!std::is_same_v<std::remove_cvref_t<ImageT>, ArtImageView>)
inline esphome::Color sample_image_ambient_tint(ImageT *img, esphome::Color base_bg = kChromeGlassTint) {
  return sample_image_ambient_tint_view(make_art_view(img), base_bg);
}

inline bool render_album_art_canvas(esphome::display::Display &it, ArtImageView target_art, int cx,
                                    bool is_standard, bool is_letterbox, bool apply_scrim,
                                    bool show_metadata, int dim_level = 0) {
  return render_album_art_canvas_view(it, target_art, cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
}

template <typename DisplayT, typename ImageT>
  requires(!std::is_same_v<std::remove_cvref_t<ImageT>, ArtImageView>)
inline bool render_album_art_canvas(DisplayT &it, ImageT *target_art, int cx,
                                    bool is_standard, bool is_letterbox, bool apply_scrim,
                                    bool show_metadata, int dim_level = 0) {
  return render_album_art_canvas_view(it, make_art_view(target_art), cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
}

inline bool render_page_wallpaper_base(esphome::display::Display &it, ArtImageView wallpaper_img,
                                       bool has_wallpaper, int cx, esphome::Color col_bg, int dim_level = 0) {
  return render_page_wallpaper_base_view(it, wallpaper_img, has_wallpaper, cx, col_bg, dim_level);
}

template <typename DisplayT, typename ImageT>
  requires(!std::is_same_v<std::remove_cvref_t<ImageT>, ArtImageView>)
inline bool render_page_wallpaper_base(DisplayT &it, ImageT *wallpaper_img, bool has_wallpaper,
                                       int cx, esphome::Color col_bg, int dim_level = 0) {
  return render_page_wallpaper_base_view(it, make_art_view(wallpaper_img), has_wallpaper, cx, col_bg, dim_level);
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
bool frame_base_impl(esphome::display::Display &it, esphome::Color c);

template <typename DisplayT>
inline bool frame_base(DisplayT &it, esphome::Color c) {
  return frame_base_impl(it, c);
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
