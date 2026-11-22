#include "ui_gfx.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace latter_ui {

// UTF-8 Sanitizer, Memoized Text Fitting & Pure-C++20 1-bpp Span Glyph Cache
namespace {

uint32_t latter_utf8_next(const char *&p) {
  const uint8_t c0 = static_cast<uint8_t>(p[0]);
  if (c0 == 0) return 0;
  if (c0 < 0x80) { p += 1; return c0; }
  if ((c0 & 0xE0) == 0xC0 && p[1] != 0) {
    const uint32_t cp = (static_cast<uint32_t>(c0 & 0x1F) << 6) | (static_cast<uint8_t>(p[1]) & 0x3F);
    p += 2;
    return cp;
  }
  if ((c0 & 0xF0) == 0xE0 && p[1] != 0 && p[2] != 0) {
    const uint32_t cp = (static_cast<uint32_t>(c0 & 0x0F) << 12) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(p[1]) & 0x3F) << 6) |
                        (static_cast<uint8_t>(p[2]) & 0x3F);
    p += 3;
    return cp;
  }
  if ((c0 & 0xF8) == 0xF0 && p[1] != 0 && p[2] != 0 && p[3] != 0) {
    const uint32_t cp = (static_cast<uint32_t>(c0 & 0x07) << 18) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(p[1]) & 0x3F) << 12) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(p[2]) & 0x3F) << 6) |
                        (static_cast<uint8_t>(p[3]) & 0x3F);
    p += 4;
    return cp;
  }
  p += 1;
  return 0xFFFD;
}

void append_utf8_cp(std::string &out, uint32_t cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

bool is_supported_font_cp(uint32_t cp) {
  if (cp >= 0x20 && cp <= 0x7E) return true;
  if (cp >= 0xA1 && cp <= 0xFF && cp != 0xAD) return true;
  switch (cp) {
    case 0x0100: case 0x0101: case 0x0102: case 0x0103: case 0x0104: case 0x0105:
    case 0x0106: case 0x0107: case 0x010C: case 0x010D: case 0x010E: case 0x010F:
    case 0x0110: case 0x0111: case 0x0112: case 0x0113: case 0x0116: case 0x0117:
    case 0x0118: case 0x0119: case 0x011A: case 0x011B: case 0x011E: case 0x011F:
    case 0x012A: case 0x012B: case 0x012E: case 0x012F: case 0x0130: case 0x0131:
    case 0x0136: case 0x0137: case 0x0139: case 0x013A: case 0x013B: case 0x013C:
    case 0x013D: case 0x013E: case 0x0141: case 0x0142: case 0x0143: case 0x0144:
    case 0x0145: case 0x0146: case 0x0147: case 0x0148: case 0x014C: case 0x014D:
    case 0x0150: case 0x0151: case 0x0152: case 0x0153: case 0x0154: case 0x0155:
    case 0x0158: case 0x0159: case 0x015A: case 0x015B: case 0x015E: case 0x015F:
    case 0x0160: case 0x0161: case 0x0162: case 0x0163: case 0x0164: case 0x0165:
    case 0x016A: case 0x016B: case 0x016E: case 0x016F: case 0x0170: case 0x0171:
    case 0x0172: case 0x0173: case 0x0178: case 0x0179: case 0x017A: case 0x017B:
    case 0x017C: case 0x017D: case 0x017E:
    case 0x2013: case 0x2014: case 0x2018: case 0x2019: case 0x201C: case 0x201D:
    case 0x2022: case 0x2026: case 0x20AC: case 0x2122:
      return true;
    default:
      return false;
  }
}

constexpr int kTextCacheEntries = 32;
constexpr int kTextScratchSpans = 4096;

// Retryable allocation — never permanently latches nullptr on transient OOM.
TextCacheEntry *text_cache_entries() {
  static TextCacheEntry *entries = nullptr;
  if (entries == nullptr) {
    entries = static_cast<TextCacheEntry *>(
        ui_psram_calloc(static_cast<size_t>(kTextCacheEntries), sizeof(TextCacheEntry)));
  }
  return entries;
}

TextSpan *text_cache_scratch() {
  static TextSpan *scratch = nullptr;
  if (scratch == nullptr) {
    scratch = static_cast<TextSpan *>(
        ui_psram_malloc(sizeof(TextSpan) * static_cast<size_t>(kTextScratchSpans)));
  }
  return scratch;
}

int text_decode_spans_1bpp(const UiFontView &font, const char *text, TextSpan *out, int cap) {
  int n = 0;
  int x_at = 0;
  for (const char *p = text; *p != 0;) {
    UiGlyphData glyph{};
    if (!font.find_glyph(latter_utf8_next(p), glyph)) {
      x_at += std::max(4, font.get_height() / 3);
      continue;
    }
    if (glyph.data != nullptr && glyph.width > 0 && glyph.height > 0) {
      const uint8_t *data = glyph.data;
      const int gx0 = x_at + glyph.offset_x;
      uint8_t bitmask = 0;
      uint8_t pixel_data = 0;
      for (int gy = 0; gy < glyph.height; gy++) {
        int run_start = -1;
        for (int gx = 0; gx < glyph.width; gx++) {
          if (bitmask == 0) {
            pixel_data = *data++;
            bitmask = 0x80U;
          }
          const bool on = (pixel_data & bitmask) != 0U;
          bitmask >>= 1;
          if (on) {
            if (run_start < 0) run_start = gx;
          } else if (run_start >= 0) {
            if (n >= cap) return -1;
            out[n++] = {static_cast<int16_t>(gx0 + run_start),
                        static_cast<int16_t>(glyph.offset_y + gy),
                        static_cast<int16_t>(gx - run_start)};
            run_start = -1;
          }
        }
        if (run_start >= 0) {
          if (n >= cap) return -1;
          out[n++] = {static_cast<int16_t>(gx0 + run_start),
                      static_cast<int16_t>(glyph.offset_y + gy),
                      static_cast<int16_t>(glyph.width - run_start)};
        }
      }
    }
    x_at += glyph.advance;
  }
  return n;
}

const TextSpan *text_cache_get(const UiFontView &font, const char *text, int &count) {
  if (font.handle == nullptr || font.find_glyph_fn == nullptr) return nullptr;
  TextCacheEntry *entries = text_cache_entries();
  TextSpan *scratch = text_cache_scratch();
  const size_t len = std::strlen(text);
  if (entries == nullptr || scratch == nullptr || len >= sizeof(entries[0].text)) return nullptr;
  static uint32_t use_seq = 0;
  ++use_seq;
  TextCacheEntry *victim = &entries[0];
  for (int i = 0; i < kTextCacheEntries; i++) {
    TextCacheEntry &e = entries[i];
    if (e.font == font.handle && std::strcmp(e.text, text) == 0) {
      e.last_use = use_seq;
      count = e.count;
      return e.spans;
    }
    if (e.last_use < victim->last_use) victim = &e;
  }
  const int n = text_decode_spans_1bpp(font, text, scratch, kTextScratchSpans);
  if (n < 0) return nullptr;
  if (victim->cap < n) {
    if (victim->spans != nullptr) ui_psram_free(victim->spans);
    const size_t alloc_n = (n > 0) ? static_cast<size_t>(n) : 1U;
    victim->spans = static_cast<TextSpan *>(ui_psram_malloc(sizeof(TextSpan) * alloc_n));
    victim->cap = (victim->spans != nullptr) ? static_cast<uint16_t>(n) : 0;
    if (victim->spans == nullptr) {
      victim->font = nullptr;
      victim->text[0] = 0;
      victim->count = 0;
      count = n;
      return scratch;
    }
  }
  if (n > 0) std::memcpy(victim->spans, scratch, sizeof(TextSpan) * static_cast<size_t>(n));
  victim->font = font.handle;
  std::memcpy(victim->text, text, len + 1);
  victim->count = static_cast<uint16_t>(n);
  victim->last_use = use_seq;
  count = n;
  return victim->spans;
}

inline uint16_t *resolve_surface_fb(Rgb565Surface &it) noexcept {
  uint16_t *fb = active_frame_buf16();
  if (fb != nullptr) return fb;
  return (it.data != nullptr && it.width == SCREEN_W && it.height == SCREEN_H) ? it.data : nullptr;
}

template <typename PutPixelFn>
void rasterize_glyph_pixels(uint16_t *fb, const UiFontView &font, const char *text,
                            int x_start, int y_start, int eff_cy0, int eff_cy1,
                            int bpp, PutPixelFn &&put_px) {
  int x_at = x_start;
  for (const char *p = text; *p != 0;) {
    UiGlyphData glyph{};
    if (!font.find_glyph(latter_utf8_next(p), glyph)) {
      x_at += std::max(4, font.get_height() / 3);
      continue;
    }
    if (glyph.data != nullptr && glyph.width > 0 && glyph.height > 0) {
      const uint8_t *data = glyph.data;
      const int gx0 = x_at + glyph.offset_x;
      const int gy0 = y_start + glyph.offset_y;
      uint8_t bitmask = 0;
      uint8_t pixel_data = 0;
      for (int gy = 0; gy < glyph.height; gy++) {
        const int py = gy0 + gy;
        uint16_t *row = (py >= eff_cy0 && py <= eff_cy1)
                            ? (fb + static_cast<size_t>(py) * static_cast<size_t>(SCREEN_W))
                            : nullptr;
        for (int gx = 0; gx < glyph.width; gx++) {
          uint8_t pixel = 0;
          for (int bit = 0; bit < bpp; bit++) {
            if (bitmask == 0) {
              pixel_data = *data++;
              bitmask = 0x80U;
            }
            pixel <<= 1;
            if ((pixel_data & bitmask) != 0U) pixel |= 1U;
            bitmask >>= 1;
          }
          if (pixel != 0 && row != nullptr) {
            const int px = gx0 + gx;
            if (px >= 0 && px < SCREEN_W) {
              put_px(row[static_cast<size_t>(px)], pixel);
            }
          }
        }
      }
    }
    x_at += glyph.advance;
  }
}

}  // namespace

std::string sanitize_utf8_for_font(const std::string &str) {
  if (str.empty()) return str;
  std::string out;
  out.reserve(str.size());
  const size_t n = str.size();
  size_t i = 0;
  while (i < n) {
    const unsigned char b0 = static_cast<unsigned char>(str[i]);
    if (b0 < 0x80) {
      if (b0 >= 0x20 && b0 <= 0x7E) out.push_back(static_cast<char>(b0));
      i++;
      continue;
    }
    uint32_t cp = 0;
    size_t len = 0;
    if ((b0 & 0xE0) == 0xC0 && i + 1 < n) {
      const unsigned char b1 = static_cast<unsigned char>(str[i + 1]);
      if ((b1 & 0xC0) == 0x80) {
        cp = (static_cast<uint32_t>(b0 & 0x1F) << 6) | static_cast<uint32_t>(b1 & 0x3F);
        if (cp >= 0x80) len = 2;
      }
    } else if ((b0 & 0xF0) == 0xE0 && i + 2 < n) {
      const unsigned char b1 = static_cast<unsigned char>(str[i + 1]);
      const unsigned char b2 = static_cast<unsigned char>(str[i + 2]);
      if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
        cp = (static_cast<uint32_t>(b0 & 0x0F) << 12) |
             (static_cast<uint32_t>(b1 & 0x3F) << 6) |
             static_cast<uint32_t>(b2 & 0x3F);
        if (cp >= 0x800) len = 3;
      }
    } else if ((b0 & 0xF8) == 0xF0 && i + 3 < n) {
      const unsigned char b1 = static_cast<unsigned char>(str[i + 1]);
      const unsigned char b2 = static_cast<unsigned char>(str[i + 2]);
      const unsigned char b3 = static_cast<unsigned char>(str[i + 3]);
      if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
        cp = (static_cast<uint32_t>(b0 & 0x07) << 18) |
             (static_cast<uint32_t>(b1 & 0x3F) << 12) |
             (static_cast<uint32_t>(b2 & 0x3F) << 6) |
             static_cast<uint32_t>(b3 & 0x3F);
        if (cp >= 0x10000) len = 4;
      }
    }
    if (len == 0) {
      i++;
      continue;
    }
    i += len;

    if (cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x205F || cp == 0x3000) {
      out.push_back(' ');
    } else if (cp == 0x00AD || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
               (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF) {
      continue;
    } else if (cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2212 || cp == 0xFF0D) {
      out.push_back('-');
    } else if (cp == 0x2015) {
      append_utf8_cp(out, 0x2014);
    } else if (cp == 0x02B9 || cp == 0x02BB || cp == 0x02BC || cp == 0x201A || cp == 0x2032 ||
               cp == 0x2039 || cp == 0x203A || cp == 0xFF07) {
      out.push_back('\'');
    } else if (cp == 0x201E || cp == 0x2033 || cp == 0xFF02) {
      out.push_back('"');
    } else if (cp == 0xFF01) {
      out.push_back('!');
    } else if (cp == 0xFF1F) {
      out.push_back('?');
    } else if (is_supported_font_cp(cp)) {
      append_utf8_cp(out, cp);
    }
  }
  return out;
}

std::string fit_text_to_width_font(const std::string &raw_str, int max_w, UiFontView font) {
  if (raw_str.empty() || font == nullptr) return "";

  struct FitCacheEntry {
    const void *font_ptr = nullptr;
    int16_t font_h = 0;
    int max_w = 0;
    size_t raw_len = 0;
    uint32_t fnv_hash = 0;
    std::string raw_key;
    std::string fitted;
  };
  static FitCacheEntry cache[16];
  static uint8_t next_slot = 0;

  uint32_t h = 2166136261U;
  for (unsigned char ch : raw_str) h = (h ^ ch) * 16777619U;
  const void *fptr = font.handle;
  for (int i = 0; i < 16; i++) {
    const auto &e = cache[i];
    if (e.font_ptr == fptr && e.font_h == font.height && e.max_w == max_w &&
        e.raw_len == raw_str.size() && e.fnv_hash == h && e.raw_key == raw_str) {
      return e.fitted;
    }
  }

  const std::string str = sanitize_utf8_for_font(raw_str);
  if (str.empty()) return "";
  int w = 0, x_off = 0, base = 0, text_h = 0;
  font.measure(str.c_str(), &w, &x_off, &base, &text_h);
  std::string result;
  if (w <= max_w) {
    result = str;
  } else {
    std::string cur = str;
    if (cur.length() > 32) {
      while (cur.length() > 30) {
        while (!cur.empty() && (static_cast<unsigned char>(cur.back()) & 0xC0) == 0x80) cur.pop_back();
        if (!cur.empty()) cur.pop_back();
      }
    }
    result = "..";
    while (!cur.empty()) {
      const std::string test_str = cur + "..";
      font.measure(test_str.c_str(), &w, &x_off, &base, &text_h);
      if (w <= max_w) {
        result = test_str;
        break;
      }
      while (!cur.empty() && (static_cast<unsigned char>(cur.back()) & 0xC0) == 0x80) cur.pop_back();
      if (!cur.empty()) cur.pop_back();
    }
  }

  auto &slot = cache[next_slot & 15U];
  next_slot = static_cast<uint8_t>((next_slot + 1U) & 15U);
  slot.font_ptr = fptr;
  slot.font_h = font.height;
  slot.max_w = max_w;
  slot.raw_len = raw_str.size();
  slot.fnv_hash = h;
  slot.raw_key = raw_str;
  slot.fitted = result;
  return result;
}

int text_ink_right_edge_impl(Rgb565Surface &it, UiFontView font, const char *text) {
  if (font == nullptr || text == nullptr || *text == '\0') return 0;
  int x_start = 0, y_start = 0, text_w = 0, text_h = 0;
  it.get_text_bounds(0, 0, text, font, UiTextAlign::CENTER_LEFT, &x_start, &y_start, &text_w, &text_h);
  if (font.find_glyph_fn != nullptr) {
    int x_at = x_start;
    int max_x1 = 0;
    bool any = false;
    for (const char *p = text; *p != 0;) {
      UiGlyphData glyph{};
      if (!font.find_glyph(latter_utf8_next(p), glyph)) {
        x_at += std::max(4, font.get_height() / 3);
        continue;
      }
      if (glyph.width > 0) {
        const int gx1 = x_at + glyph.offset_x + glyph.width;
        max_x1 = any ? std::max(max_x1, gx1) : gx1;
        any = true;
      }
      x_at += glyph.advance;
    }
    if (any) return max_x1;
  }
  return x_start + text_w;
}

void fast_print_impl(Rgb565Surface &it, int x, int y, UiFontView font,
                     UiColor color, UiTextAlign align, const char *text,
                     UiColor background) {
  if (font == nullptr || text == nullptr) return;
  const int cy0 = clip_y0();
  const int cy1 = clip_y1();
  if (cy1 < cy0) return;

  uint16_t *fb = resolve_surface_fb(it);
  const int bpp = font.get_bpp();
  if (fb == nullptr || bpp <= 0 || bpp > 4 || font.find_glyph_fn == nullptr) {
    if (it.fallback_print_fn != nullptr) {
      it.fallback_print_fn(it.native_display, x, y, font, color, align, text, background, cy0, cy1);
    }
    return;
  }

  int x_start = 0, y_start = 0, text_w = 0, text_h = 0;
  it.get_text_bounds(x, y, text, font, align, &x_start, &y_start, &text_w, &text_h);
  if (text_h > 0 && (y_start + text_h <= cy0 || y_start > cy1)) return;

  const int eff_cy0 = std::max(0, cy0);
  const int eff_cy1 = std::min(SCREEN_H - 1, cy1);
  if (eff_cy1 < eff_cy0) return;

  const int bpp_max = (1 << bpp) - 1;
  uint16_t lut[16]{};
  const float diff_r = static_cast<float>(color.r) - static_cast<float>(background.r);
  const float diff_g = static_cast<float>(color.g) - static_cast<float>(background.g);
  const float diff_b = static_cast<float>(color.b) - static_cast<float>(background.b);
  const float diff_w = static_cast<float>(color.w) - static_cast<float>(background.w);
  for (int lv = 1; lv <= bpp_max; lv++) {
    if (lv == bpp_max) {
      lut[lv] = encode_hw_rgb565(color);
    } else {
      const float on = static_cast<float>(lv) / static_cast<float>(bpp_max);
      lut[lv] = encode_hw_rgb565(UiColor{
          static_cast<uint8_t>(diff_r * on + static_cast<float>(background.r)),
          static_cast<uint8_t>(diff_g * on + static_cast<float>(background.g)),
          static_cast<uint8_t>(diff_b * on + static_cast<float>(background.b)),
          static_cast<uint8_t>(diff_w * on + static_cast<float>(background.w)),
      });
    }
  }

  if (bpp == 1) {
    int span_count = 0;
    const TextSpan *spans = text_cache_get(font, text, span_count);
    if (spans != nullptr) {
      const uint16_t hw = lut[1];
      for (int i = 0; i < span_count; i++) {
        const int py = y_start + spans[i].y;
        if (py < eff_cy0 || py > eff_cy1) continue;
        int px = x_start + spans[i].x, len = spans[i].len;
        if (px >= SCREEN_W || len <= 0) continue;
        if (px < 0) { len += px; px = 0; }
        if (len <= 0) continue;
        if (len > SCREEN_W - px) len = SCREEN_W - px;
        uint16_t *dst = fb + static_cast<size_t>(py) * static_cast<size_t>(SCREEN_W) + static_cast<size_t>(px);
        for (int k = 0; k < len; k++) dst[k] = hw;
      }
      return;
    }
  }

  rasterize_glyph_pixels(fb, font, text, x_start, y_start, eff_cy0, eff_cy1, bpp,
                         [&](uint16_t &dst, uint8_t pixel) { dst = lut[pixel]; });
}

void fast_print_alpha_impl(Rgb565Surface &it, int x, int y, UiFontView font,
                           UiColor color, UiTextAlign align, const char *text,
                           int alpha_q8) {
  if (font == nullptr || text == nullptr || alpha_q8 <= 0) return;
  if (alpha_q8 >= 255) { fast_print_impl(it, x, y, font, color, align, text, kColorOff); return; }
  const int cy0 = clip_y0(), cy1 = clip_y1();
  if (cy1 < cy0) return;

  uint16_t *fb = resolve_surface_fb(it);
  const int bpp = font.get_bpp();
  if (fb == nullptr || bpp <= 0 || bpp > 4 || font.find_glyph_fn == nullptr) {
    if (alpha_q8 >= 128) fast_print_impl(it, x, y, font, color, align, text, kColorOff);
    return;
  }

  int x_start = 0, y_start = 0, text_w = 0, text_h = 0;
  it.get_text_bounds(x, y, text, font, align, &x_start, &y_start, &text_w, &text_h);
  if (text_h > 0 && (y_start + text_h <= cy0 || y_start > cy1)) return;

  const int eff_cy0 = std::max(0, cy0), eff_cy1 = std::min(SCREEN_H - 1, cy1);
  if (eff_cy1 < eff_cy0) return;

  const int bpp_max = (1 << bpp) - 1;
  const int a_clamped = std::min(256, alpha_q8);
  const int cr5 = (static_cast<int>(color.r) * 31 + 127) / 255;
  const int cg6 = (static_cast<int>(color.g) * 63 + 127) / 255;
  const int cb5 = (static_cast<int>(color.b) * 31 + 127) / 255;

  auto blend_px = [&](uint16_t &dst, int a) {
    if (a <= 0) return;
    if (a >= 256) { dst = encode_hw_rgb565(color); return; }
    const uint16_t bg = decode_hw_rgb565_raw(dst);
    const int ia = 256 - a;
    const int r = (cr5 * a + ((bg >> 11) & 0x1F) * ia + 128) >> 8;
    const int g = (cg6 * a + ((bg >> 5) & 0x3F) * ia + 128) >> 8;
    const int b = (cb5 * a + (bg & 0x1F) * ia + 128) >> 8;
    dst = encode_hw_rgb565_raw(static_cast<uint16_t>(
        (std::min(31, r) << 11) | (std::min(63, g) << 5) | std::min(31, b)));
  };

  if (bpp == 1) {
    int span_count = 0;
    const TextSpan *spans = text_cache_get(font, text, span_count);
    if (spans != nullptr) {
      for (int i = 0; i < span_count; i++) {
        const int py = y_start + spans[i].y;
        if (py < eff_cy0 || py > eff_cy1) continue;
        int px = x_start + spans[i].x, len = spans[i].len;
        if (px >= SCREEN_W || len <= 0) continue;
        if (px < 0) { len += px; px = 0; }
        if (len <= 0) continue;
        if (len > SCREEN_W - px) len = SCREEN_W - px;
        uint16_t *dst = fb + static_cast<size_t>(py) * static_cast<size_t>(SCREEN_W) + static_cast<size_t>(px);
        for (int k = 0; k < len; k++) blend_px(dst[k], a_clamped);
      }
      return;
    }
  }

  rasterize_glyph_pixels(fb, font, text, x_start, y_start, eff_cy0, eff_cy1, bpp,
                         [&](uint16_t &dst, uint8_t pixel) {
                           const int eff_a = (bpp_max == 1)
                                                 ? a_clamped
                                                 : ((a_clamped * static_cast<int>(pixel) + (bpp_max >> 1)) / bpp_max);
                           blend_px(dst, eff_a);
                         });
}

}  // namespace latter_ui
