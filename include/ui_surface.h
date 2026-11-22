#pragma once

// ==============================================================================
// Latter UI Pure C++20 RGB565 Surface, Font View, Art View & Wall Time
// (include/ui_surface.h — Reconciliation #1)
// Pure C++20 L0 leaf — zero ESPHome, HAL, or runtime-state includes.
// ==============================================================================

#include "ui_color.h"
#include "ui_panel_traits.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <type_traits>

namespace latter_ui {

// Bit-identical to esphome::display::TextAlign so enum conversions are lossless.
enum class UiTextAlign : uint8_t {
  TOP = 0x00,
  CENTER_VERTICAL = 0x01,
  BASELINE = 0x02,
  BOTTOM = 0x04,

  LEFT = 0x00,
  CENTER_HORIZONTAL = 0x08,
  RIGHT = 0x10,

  TOP_LEFT = TOP | LEFT,
  TOP_CENTER = TOP | CENTER_HORIZONTAL,
  TOP_RIGHT = TOP | RIGHT,

  CENTER_LEFT = CENTER_VERTICAL | LEFT,
  CENTER = CENTER_VERTICAL | CENTER_HORIZONTAL,
  CENTER_RIGHT = CENTER_VERTICAL | RIGHT,

  BASELINE_LEFT = BASELINE | LEFT,
  BASELINE_CENTER = BASELINE | CENTER_HORIZONTAL,
  BASELINE_RIGHT = BASELINE | RIGHT,

  BOTTOM_LEFT = BOTTOM | LEFT,
  BOTTOM_CENTER = BOTTOM | CENTER_HORIZONTAL,
  BOTTOM_RIGHT = BOTTOM | RIGHT,
};

using TextAlign = UiTextAlign;

template <typename AlignT>
inline constexpr UiTextAlign to_ui_text_align(AlignT align) noexcept {
  return static_cast<UiTextAlign>(static_cast<uint8_t>(align));
}

struct UiGlyphData {
  const uint8_t *data{nullptr};
  int16_t offset_x{0};
  int16_t offset_y{0};
  int16_t width{0};
  int16_t height{0};
  int16_t advance{0};
};

// Built-in compact 5x7 ASCII glyph rows for pure-C++20 host RGB565 rendering & measurement.
namespace detail {

inline const uint8_t *host_glyph_rows_5x7(uint32_t cp) noexcept {
  // Each glyph is 7 rows of 5 MSBs (bits 7..3). Printable ASCII 0x20..0x7E supported.
  static constexpr uint8_t kSpace[7] = {0, 0, 0, 0, 0, 0, 0};
  static constexpr uint8_t kBox[7] = {0xF8, 0x88, 0xA8, 0x88, 0xA8, 0x88, 0xF8};
  static constexpr uint8_t kDigits[10][7] = {
      {0x70, 0x88, 0x98, 0xA8, 0xC8, 0x88, 0x70},  // 0
      {0x20, 0x60, 0x20, 0x20, 0x20, 0x20, 0x70},  // 1
      {0x70, 0x88, 0x08, 0x30, 0x40, 0x80, 0xF8},  // 2
      {0x70, 0x88, 0x08, 0x30, 0x08, 0x88, 0x70},  // 3
      {0x10, 0x30, 0x50, 0x90, 0xF8, 0x10, 0x10},  // 4
      {0xF8, 0x80, 0xF0, 0x08, 0x08, 0x88, 0x70},  // 5
      {0x30, 0x40, 0x80, 0xF0, 0x88, 0x88, 0x70},  // 6
      {0xF8, 0x08, 0x10, 0x20, 0x40, 0x40, 0x40},  // 7
      {0x70, 0x88, 0x88, 0x70, 0x88, 0x88, 0x70},  // 8
      {0x70, 0x88, 0x88, 0x78, 0x08, 0x10, 0x60},  // 9
  };
  static constexpr uint8_t kColon[7] = {0x00, 0x20, 0x20, 0x00, 0x20, 0x20, 0x00};
  static constexpr uint8_t kDot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x20};
  static constexpr uint8_t kDash[7] = {0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00};
  static constexpr uint8_t kPlus[7] = {0x00, 0x20, 0x20, 0xF8, 0x20, 0x20, 0x00};
  static constexpr uint8_t kSlash[7] = {0x08, 0x10, 0x20, 0x20, 0x40, 0x80, 0x00};
  static constexpr uint8_t kPct[7] = {0xC8, 0xC8, 0x10, 0x20, 0x40, 0x98, 0x98};
  static constexpr uint8_t kLt[7] = {0x10, 0x20, 0x40, 0x80, 0x40, 0x20, 0x10};
  static constexpr uint8_t kGt[7] = {0x40, 0x20, 0x10, 0x08, 0x10, 0x20, 0x40};
  static constexpr uint8_t kUpper[26][7] = {
      {0x70, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88},  // A
      {0xF0, 0x88, 0x88, 0xF0, 0x88, 0x88, 0xF0},  // B
      {0x70, 0x88, 0x80, 0x80, 0x80, 0x88, 0x70},  // C
      {0xF0, 0x88, 0x88, 0x88, 0x88, 0x88, 0xF0},  // D
      {0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0xF8},  // E
      {0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0x80},  // F
      {0x70, 0x88, 0x80, 0xB8, 0x88, 0x88, 0x78},  // G
      {0x88, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88},  // H
      {0x70, 0x20, 0x20, 0x20, 0x20, 0x20, 0x70},  // I
      {0x38, 0x10, 0x10, 0x10, 0x90, 0x90, 0x60},  // J
      {0x88, 0x90, 0xA0, 0xC0, 0xA0, 0x90, 0x88},  // K
      {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xF8},  // L
      {0x88, 0xD8, 0xA8, 0xA8, 0x88, 0x88, 0x88},  // M
      {0x88, 0xC8, 0xA8, 0x98, 0x88, 0x88, 0x88},  // N
      {0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70},  // O
      {0xF0, 0x88, 0x88, 0xF0, 0x80, 0x80, 0x80},  // P
      {0x70, 0x88, 0x88, 0x88, 0xA8, 0x90, 0x68},  // Q
      {0xF0, 0x88, 0x88, 0xF0, 0xA0, 0x90, 0x88},  // R
      {0x78, 0x80, 0x80, 0x70, 0x08, 0x08, 0xF0},  // S
      {0xF8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20},  // T
      {0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70},  // U
      {0x88, 0x88, 0x88, 0x88, 0x50, 0x50, 0x20},  // V
      {0x88, 0x88, 0x88, 0xA8, 0xA8, 0xD8, 0x88},  // W
      {0x88, 0x88, 0x50, 0x20, 0x50, 0x88, 0x88},  // X
      {0x88, 0x88, 0x50, 0x20, 0x20, 0x20, 0x20},  // Y
      {0xF8, 0x08, 0x10, 0x20, 0x40, 0x80, 0xF8},  // Z
  };
  if (cp == ' ') return kSpace;
  if (cp >= '0' && cp <= '9') return kDigits[cp - '0'];
  if (cp >= 'A' && cp <= 'Z') return kUpper[cp - 'A'];
  if (cp >= 'a' && cp <= 'z') return kUpper[cp - 'a'];
  if (cp == ':') return kColon;
  if (cp == '.' || cp == ',') return kDot;
  if (cp == '-' || cp == 0x2013 || cp == 0x2014) return kDash;
  if (cp == '+') return kPlus;
  if (cp == '/') return kSlash;
  if (cp == '%') return kPct;
  if (cp == '<') return kLt;
  if (cp == '>') return kGt;
  return kBox;
}

struct HostFontSpec {
  int px_height{20};
};

}  // namespace detail

struct UiFontView {
  using MeasureFn = void (*)(const void *handle, const char *str, int *w, int *x_off, int *base, int *h);
  using FindGlyphFn = bool (*)(const void *handle, uint32_t cp, UiGlyphData &out_glyph);

  const void *handle{nullptr};
  int16_t height{0};
  int16_t baseline{0};
  uint8_t bpp{1};
  MeasureFn measure_fn{nullptr};
  FindGlyphFn find_glyph_fn{nullptr};

  constexpr UiFontView() noexcept = default;
  constexpr UiFontView(std::nullptr_t) noexcept {}

  // Structural adapter for ESPHome Font / BaseFont pointers without including esphome headers.
  template <typename FontT>
    requires(!std::is_same_v<std::remove_cvref_t<FontT>, UiFontView> &&
             requires(FontT *f, const char *s, int *p) { f->measure(s, p, p, p, p); })
  UiFontView(FontT *f) noexcept {
    if (f == nullptr) return;
    handle = static_cast<const void *>(f);
    int w = 0, xo = 0, bs = 0, h = 0;
    f->measure("Ag", &w, &xo, &bs, &h);
    height = static_cast<int16_t>(h > 0 ? h : 16);
    baseline = static_cast<int16_t>(bs > 0 ? bs : (height * 4) / 5);
    if constexpr (requires { f->get_bpp(); }) {
      bpp = static_cast<uint8_t>(f->get_bpp());
    } else {
      bpp = 1U;
    }
    measure_fn = +[](const void *raw, const char *str, int *ow, int *oxo, int *obs, int *oh) {
      static_cast<FontT *>(const_cast<void *>(raw))->measure(str, ow, oxo, obs, oh);
    };
    if constexpr (requires { f->find_glyph(uint32_t{32}); }) {
      find_glyph_fn = +[](const void *raw, uint32_t cp, UiGlyphData &out_g) -> bool {
        auto *font = static_cast<FontT *>(const_cast<void *>(raw));
        const auto *g = font->find_glyph(cp);
        if (g == nullptr) return false;
        out_g.data = g->data;
        out_g.offset_x = static_cast<int16_t>(g->offset_x);
        out_g.offset_y = static_cast<int16_t>(g->offset_y);
        out_g.width = static_cast<int16_t>(g->width);
        out_g.height = static_cast<int16_t>(g->height);
        out_g.advance = static_cast<int16_t>(g->advance);
        return true;
      };
    }
  }

  template <typename FontT>
    requires(!std::is_pointer_v<std::remove_cvref_t<FontT>> &&
             !std::is_same_v<std::remove_cvref_t<FontT>, UiFontView> &&
             requires(FontT &f, const char *s, int *p) { f.measure(s, p, p, p, p); })
  UiFontView(FontT &f) noexcept : UiFontView(&f) {}

  // Built-in scalable 1-bpp host bitmap font for headless 5-geometry RGB565 verification.
  static UiFontView make_host_font(int px_height) noexcept {
    struct HostGlyphSlot {
      int px_h{0};
      uint8_t bitmaps[96][64]{};
      bool built{false};
    };
    static HostGlyphSlot s_slots[8]{};
    const int clamped_h = std::clamp(px_height, 10, 84);
    HostGlyphSlot *slot = &s_slots[0];
    for (auto &cand : s_slots) {
      if (cand.built && cand.px_h == clamped_h) {
        slot = &cand;
        break;
      }
      if (!cand.built) {
        slot = &cand;
        break;
      }
    }
    const int scale = std::max(1, (clamped_h * 3 + 14) / 28);
    const int gw = std::min(20, 5 * scale);
    const int gh = std::min(24, 7 * scale);
    if (!slot->built || slot->px_h != clamped_h) {
      slot->px_h = clamped_h;
      std::memset(slot->bitmaps, 0, sizeof(slot->bitmaps));
      for (int idx = 0; idx < 95; ++idx) {
        const uint8_t *rows = detail::host_glyph_rows_5x7(static_cast<uint32_t>(32 + idx));
        uint8_t *dst = slot->bitmaps[idx];
        int bit_pos = 0;
        for (int y = 0; y < gh; ++y) {
          const int sy = std::min(6, (y * 7) / gh);
          for (int x = 0; x < gw; ++x) {
            const int sx = std::min(4, (x * 5) / gw);
            const bool on = (rows[sy] & (0x80U >> sx)) != 0U;
            if (on && (bit_pos / 8) < 64) {
              dst[bit_pos / 8] |= static_cast<uint8_t>(0x80U >> (bit_pos & 7));
            }
            ++bit_pos;
          }
        }
      }
      slot->built = true;
    }

    UiFontView view{};
    view.handle = static_cast<const void *>(slot);
    view.height = static_cast<int16_t>(clamped_h);
    view.baseline = static_cast<int16_t>((clamped_h * 4) / 5);
    view.bpp = 1U;
    view.measure_fn = +[](const void *raw, const char *str, int *ow, int *oxo, int *obs, int *oh) {
      const auto *s = static_cast<const HostGlyphSlot *>(raw);
      const int h = s->px_h;
      const int sc = std::max(1, (h * 3 + 14) / 28);
      const int adv = 5 * sc + std::max(1, sc);
      int count = 0;
      if (str != nullptr) {
        for (const unsigned char *p = reinterpret_cast<const unsigned char *>(str); *p != 0; ++p) {
          if ((*p & 0xC0U) != 0x80U) ++count;
        }
      }
      if (ow != nullptr) *ow = (count > 0) ? (count * adv - std::max(1, sc)) : 0;
      if (oxo != nullptr) *oxo = 0;
      if (obs != nullptr) *obs = (h * 4) / 5;
      if (oh != nullptr) *oh = h;
    };
    view.find_glyph_fn = +[](const void *raw, uint32_t cp, UiGlyphData &out_g) -> bool {
      const auto *s = static_cast<const HostGlyphSlot *>(raw);
      const int h = s->px_h;
      const int sc = std::max(1, (h * 3 + 14) / 28);
      const int w_px = std::min(20, 5 * sc);
      const int h_px = std::min(24, 7 * sc);
      const int idx = (cp >= 32U && cp < 127U) ? static_cast<int>(cp - 32U) : static_cast<int>('?' - 32);
      out_g.data = s->bitmaps[idx];
      out_g.offset_x = 0;
      out_g.offset_y = static_cast<int16_t>(std::max(0, (h * 4) / 5 - h_px));
      out_g.width = (cp == 32U) ? 0 : static_cast<int16_t>(w_px);
      out_g.height = (cp == 32U) ? 0 : static_cast<int16_t>(h_px);
      out_g.advance = static_cast<int16_t>(w_px + std::max(1, sc));
      return true;
    };
    return view;
  }

  constexpr const UiFontView *operator->() const noexcept { return this; }
  constexpr explicit operator bool() const noexcept { return handle != nullptr; }
  friend constexpr bool operator==(const UiFontView &f, std::nullptr_t) noexcept { return f.handle == nullptr; }
  friend constexpr bool operator!=(const UiFontView &f, std::nullptr_t) noexcept { return f.handle != nullptr; }
  friend constexpr bool operator==(std::nullptr_t, const UiFontView &f) noexcept { return f.handle == nullptr; }
  friend constexpr bool operator!=(std::nullptr_t, const UiFontView &f) noexcept { return f.handle != nullptr; }

  constexpr int get_height() const noexcept { return height; }
  constexpr int get_baseline() const noexcept { return baseline; }
  constexpr int get_bpp() const noexcept { return bpp; }

  void measure(const char *str, int *w, int *x_off, int *base, int *h) const {
    if (measure_fn != nullptr && handle != nullptr) {
      measure_fn(handle, str != nullptr ? str : "", w, x_off, base, h);
      return;
    }
    if (w != nullptr) *w = 0;
    if (x_off != nullptr) *x_off = 0;
    if (base != nullptr) *base = 0;
    if (h != nullptr) *h = 0;
  }

  bool find_glyph(uint32_t cp, UiGlyphData &out_glyph) const {
    if (find_glyph_fn != nullptr && handle != nullptr) {
      return find_glyph_fn(handle, cp, out_glyph);
    }
    return false;
  }
};

inline void compute_text_bounds(int x, int y, const char *text, const UiFontView &font,
                                UiTextAlign align, int *x1, int *y1, int *width, int *height) {
  int w = 0, x_off = 0, baseline = 0, h = 0;
  font.measure(text, &w, &x_off, &baseline, &h);
  const uint8_t bits = static_cast<uint8_t>(align);
  const uint8_t h_align = static_cast<uint8_t>(bits & 0x18U);
  const uint8_t v_align = static_cast<uint8_t>(bits & 0x07U);

  int xs = x;
  if (h_align == static_cast<uint8_t>(UiTextAlign::CENTER_HORIZONTAL)) {
    xs = x - w / 2;
  } else if (h_align == static_cast<uint8_t>(UiTextAlign::RIGHT)) {
    xs = x - w;
  }

  int ys = y;
  if (v_align == static_cast<uint8_t>(UiTextAlign::CENTER_VERTICAL)) {
    ys = y - h / 2;
  } else if (v_align == static_cast<uint8_t>(UiTextAlign::BASELINE)) {
    ys = y - baseline;
  } else if (v_align == static_cast<uint8_t>(UiTextAlign::BOTTOM)) {
    ys = y - h;
  }

  if (x1 != nullptr) *x1 = xs + x_off;
  if (y1 != nullptr) *y1 = ys;
  if (width != nullptr) *width = w;
  if (height != nullptr) *height = h;
}

inline constexpr uint16_t bswap16_u16(uint16_t v) noexcept {
  return static_cast<uint16_t>((v >> 8) | (v << 8));
}

template <int Mode>
inline constexpr uint16_t encode_hw_rgb565_mode(uint16_t p) noexcept {
  if constexpr (Mode == 1) {
    return p;
  } else if constexpr (Mode == 2) {
    return bswap16_u16(p);
  } else {
    const uint16_t bgr = static_cast<uint16_t>(((p & 0x001FU) << 11) | (p & 0x07E0U) | ((p >> 11) & 0x001FU));
    if constexpr (Mode == 4) {
      return bswap16_u16(bgr);
    } else {
      return bgr;
    }
  }
}

template <int Mode>
inline constexpr uint16_t decode_hw_rgb565_mode(uint16_t hw) noexcept {
  if constexpr (Mode == 1) {
    return hw;
  } else if constexpr (Mode == 2) {
    return bswap16_u16(hw);
  } else {
    const uint16_t bgr = [&]() constexpr noexcept {
      if constexpr (Mode == 4) return bswap16_u16(hw);
      else return hw;
    }();
    return static_cast<uint16_t>(((bgr & 0x001FU) << 11) | (bgr & 0x07E0U) | ((bgr >> 11) & 0x001FU));
  }
}

inline uint16_t *&active_frame_buf16() noexcept {
  static uint16_t *ptr = nullptr;
  return ptr;
}

// 0 = fallback, 1 = LE RGB565 (0xF800), 2 = BE RGB565 (0x00F8), 3 = LE BGR565 (0x001F), 4 = BE BGR565 (0x1F00)
inline int &active_frame_endian_mode() noexcept {
  static int mode = -1;
  return mode;
}

inline uint16_t encode_hw_rgb565_raw(uint16_t p) noexcept {
  const int mode = active_frame_endian_mode();
  if (mode == 1 || mode < 0) return encode_hw_rgb565_mode<1>(p);
  if (mode == 2) return encode_hw_rgb565_mode<2>(p);
  return (mode == 4) ? encode_hw_rgb565_mode<4>(p) : encode_hw_rgb565_mode<3>(p);
}

inline uint16_t encode_hw_rgb565(UiColor c) noexcept {
  const uint16_t p = static_cast<uint16_t>(((static_cast<uint16_t>(c.r) & 0xF8U) << 8) |
                                           ((static_cast<uint16_t>(c.g) & 0xFCU) << 3) |
                                           (static_cast<uint16_t>(c.b) >> 3));
  return encode_hw_rgb565_raw(p);
}

inline uint16_t decode_hw_rgb565_raw(uint16_t hw) noexcept {
  const int mode = active_frame_endian_mode();
  if (mode == 1 || mode < 0) return decode_hw_rgb565_mode<1>(hw);
  if (mode == 2) return decode_hw_rgb565_mode<2>(hw);
  return (mode == 4) ? decode_hw_rgb565_mode<4>(hw) : decode_hw_rgb565_mode<3>(hw);
}

// Headless & on-device RGB565 drawing surface (Reconciliation #1).
struct Rgb565Surface {
  using FallbackPixelFn = void (*)(void *ctx, int x, int y, UiColor c);
  using FallbackLineFn = void (*)(void *ctx, int x, int y, int len, UiColor c);
  using FallbackPrintFn = void (*)(void *ctx, int x, int y, const UiFontView &font,
                                   UiColor color, UiTextAlign align, const char *text,
                                   UiColor background, int cy0, int cy1);

  uint16_t *data{nullptr};
  int width{SCREEN_W};
  int height{SCREEN_H};
  int endian_mode{1};
  void *native_display{nullptr};
  FallbackPixelFn fallback_pixel_fn{nullptr};
  FallbackLineFn fallback_hline_fn{nullptr};
  FallbackLineFn fallback_vline_fn{nullptr};
  FallbackPrintFn fallback_print_fn{nullptr};

  constexpr Rgb565Surface() noexcept = default;
  constexpr Rgb565Surface(uint16_t *buf, int w = SCREEN_W, int h = SCREEN_H, int mode = 1) noexcept
      : data(buf), width(w), height(h), endian_mode(mode) {}

  template <typename NativeDispT>
    requires(!std::is_same_v<std::remove_cvref_t<NativeDispT>, Rgb565Surface> &&
             requires(NativeDispT &d, int x, int y) {
               d.get_width();
               d.get_height();
               (void)x;
               (void)y;
             })
  Rgb565Surface(NativeDispT &disp) noexcept
      : data(active_frame_buf16()),
        width(SCREEN_W),
        height(SCREEN_H),
        endian_mode(active_frame_endian_mode() > 0 ? active_frame_endian_mode() : 1),
        native_display(static_cast<void *>(&disp)) {
    if constexpr (requires(NativeDispT &d, UiColor c) { d.draw_pixel_at(0, 0, c); }) {
      fallback_pixel_fn = +[](void *raw, int x, int y, UiColor c) {
        static_cast<NativeDispT *>(raw)->draw_pixel_at(x, y, c);
      };
    }
    if constexpr (requires(NativeDispT &d, UiColor c) { d.horizontal_line(0, 0, 1, c); }) {
      fallback_hline_fn = +[](void *raw, int x, int y, int len, UiColor c) {
        static_cast<NativeDispT *>(raw)->horizontal_line(x, y, len, c);
      };
    }
    if constexpr (requires(NativeDispT &d, UiColor c) { d.vertical_line(0, 0, 1, c); }) {
      fallback_vline_fn = +[](void *raw, int x, int y, int len, UiColor c) {
        static_cast<NativeDispT *>(raw)->vertical_line(x, y, len, c);
      };
    }
  }

  void draw_pixel_at(int x, int y, UiColor c) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    if (data != nullptr) {
      data[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = encode_hw_rgb565(c);
    } else if (fallback_pixel_fn != nullptr && native_display != nullptr) {
      fallback_pixel_fn(native_display, x, y, c);
    }
  }

  void horizontal_line(int x, int y, int w, UiColor c) {
    if (w <= 0 || y < 0 || y >= height) return;
    if (x < 0) {
      w += x;
      x = 0;
    }
    if (x + w > width) w = width - x;
    if (w <= 0) return;
    if (data != nullptr) {
      const uint16_t px = encode_hw_rgb565(c);
      uint16_t *dst = data + static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
      for (int i = 0; i < w; ++i) dst[i] = px;
    } else if (fallback_hline_fn != nullptr && native_display != nullptr) {
      fallback_hline_fn(native_display, x, y, w, c);
    }
  }

  void vertical_line(int x, int y, int h, UiColor c) {
    if (h <= 0 || x < 0 || x >= width) return;
    if (y < 0) {
      h += y;
      y = 0;
    }
    if (y + h > height) h = height - y;
    if (h <= 0) return;
    if (data != nullptr) {
      const uint16_t px = encode_hw_rgb565(c);
      uint16_t *dst = data + static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
      for (int i = 0; i < h; ++i, dst += width) *dst = px;
    } else if (fallback_vline_fn != nullptr && native_display != nullptr) {
      fallback_vline_fn(native_display, x, y, h, c);
    }
  }

  void get_text_bounds(int x, int y, const char *text, const UiFontView &font, UiTextAlign align,
                       int *x1, int *y1, int *w, int *h) const {
    compute_text_bounds(x, y, text, font, align, x1, y1, w, h);
  }
};

inline Rgb565Surface bind_host_surface(uint16_t *buf, int w = SCREEN_W, int h = SCREEN_H,
                                       int endian_mode = 1) noexcept {
  active_frame_buf16() = buf;
  active_frame_endian_mode() = endian_mode;
  return Rgb565Surface{buf, w, h, endian_mode};
}

struct ArtImageView {
  const uint16_t *data = nullptr;
  int width = 0;
  int height = 0;

  constexpr bool valid() const noexcept { return data != nullptr && width > 0 && height > 0; }
  constexpr int get_width() const noexcept { return width; }
  constexpr int get_height() const noexcept { return height; }
  inline const uint8_t *get_data_start() const noexcept {
    return reinterpret_cast<const uint8_t *>(data);
  }
};

inline ArtImageView make_art_view(const ArtImageView &v) noexcept { return v; }
inline ArtImageView make_art_view(const ArtImageView *v) noexcept {
  return (v != nullptr) ? *v : ArtImageView{};
}

template <typename ImageT>
  requires(!std::is_same_v<std::remove_cvref_t<ImageT>, ArtImageView>)
inline ArtImageView make_art_view(ImageT *img) noexcept {
  if (img == nullptr) return {};
  return {
      reinterpret_cast<const uint16_t *>(img->get_data_start()),
      img->get_width(),
      img->get_height(),
  };
}

// Pure C++20 wall-clock value (seamlessly constructible from esphome::ESPTime without ESPHome headers).
struct UiWallTime {
  uint8_t second{0};
  uint8_t minute{0};
  uint8_t hour{0};
  uint8_t day_of_week{1};   // 1 = Sunday .. 7 = Saturday (matches esphome::ESPTime)
  uint8_t day_of_month{1};  // 1..31
  uint8_t month{1};         // 1..12
  uint16_t day_of_year{1};  // 1..366
  uint16_t year{1970};
  bool is_dst{false};
  int64_t timestamp{0};

  constexpr UiWallTime() noexcept = default;

  template <typename ExtTimeT>
    requires(!std::is_same_v<std::remove_cvref_t<ExtTimeT>, UiWallTime> &&
             requires(const ExtTimeT &t) {
               t.year;
               t.month;
               t.day_of_month;
               t.hour;
               t.minute;
               t.second;
               t.timestamp;
             })
  constexpr UiWallTime(const ExtTimeT &t) noexcept
      : second(static_cast<uint8_t>(t.second)),
        minute(static_cast<uint8_t>(t.minute)),
        hour(static_cast<uint8_t>(t.hour)),
        day_of_week(static_cast<uint8_t>(t.day_of_week)),
        day_of_month(static_cast<uint8_t>(t.day_of_month)),
        month(static_cast<uint8_t>(t.month)),
        day_of_year(static_cast<uint16_t>(t.day_of_year)),
        year(static_cast<uint16_t>(t.year)),
        is_dst(static_cast<bool>(t.is_dst)),
        timestamp(static_cast<int64_t>(t.timestamp)) {}

  constexpr bool is_valid() const noexcept { return year >= 2019; }

  size_t strftime(char *buf, size_t buf_len, const char *format) const {
    if (buf == nullptr || buf_len == 0) return 0;
    if (!is_valid() || format == nullptr) {
      buf[0] = '\0';
      return 0;
    }
    std::tm tm_val{};
    tm_val.tm_sec = second;
    tm_val.tm_min = minute;
    tm_val.tm_hour = hour;
    tm_val.tm_mday = day_of_month;
    tm_val.tm_mon = (month >= 1 && month <= 12) ? (month - 1) : 0;
    tm_val.tm_year = static_cast<int>(year) - 1900;
    tm_val.tm_wday = (day_of_week >= 1 && day_of_week <= 7) ? (day_of_week - 1) : 0;
    tm_val.tm_yday = (day_of_year >= 1) ? (day_of_year - 1) : 0;
    tm_val.tm_isdst = is_dst ? 1 : 0;
    return std::strftime(buf, buf_len, format, &tm_val);
  }

  std::string strftime(const char *format) const {
    char buf[128]{};
    const size_t n = strftime(buf, sizeof(buf), format);
    return std::string(buf, n);
  }
};

}  // namespace latter_ui
