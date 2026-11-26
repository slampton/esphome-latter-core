#include "ui_gfx.h"
#include "ui_dirty_band.h"
#include "ui_mdi.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace latter_ui {
using namespace latter_core;

// ------------------------------------------------------------------------------
// 5. UTF-8 Sanitizer, Memoized Text Fitting & Pure-C++20 1-bpp Span Glyph Cache
// ------------------------------------------------------------------------------
namespace {

void append_utf8_cp(std::string &out, uint32_t cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFFU) {
    out.push_back(static_cast<char>(0xE0U | ((cp >> 12) & 0x0FU)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if (cp <= 0x10FFFFU) {
    out.push_back(static_cast<char>(0xF0U | ((cp >> 18) & 0x07U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 12) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  }
}

bool font_supports_cp(const UiFontView &font, uint32_t cp) noexcept {
  if (cp >= 0xD800U && cp <= 0xDFFFU) return false;
  if (cp > 0x10FFFFU) return false;
  if (cp >= 0x80U && cp <= 0x9FU) return false;
  if (font.find_glyph_fn != nullptr && font.handle != nullptr) {
    UiGlyphData glyph{};
    return font.find_glyph(cp, glyph);
  }
  return (cp >= 0x20U && cp <= 0x7EU) || (cp >= 0xA1U && cp != 0xADU);
}

constexpr int kTextCacheEntries = 32;
constexpr int kTextScratchSpans = 6144;

TextCacheEntry *&text_cache_entries_slot() {
  static TextCacheEntry *entries = nullptr;
  return entries;
}

TextSpan *&text_cache_scratch_slot() {
  static TextSpan *scratch = nullptr;
  return scratch;
}

void text_cache_drop() noexcept {
  TextCacheEntry *&entries = text_cache_entries_slot();
  if (entries != nullptr) {
    for (int i = 0; i < kTextCacheEntries; ++i) {
      if (entries[i].spans != nullptr) {
        ui_psram_free(entries[i].spans);
        entries[i].spans = nullptr;
      }
      entries[i].font = nullptr;
      entries[i].count = 0;
      entries[i].cap = 0;
      entries[i].bpp = 0;
      entries[i].last_use = 0;
      entries[i].text[0] = '\0';
    }
  }
  TextSpan *&scratch = text_cache_scratch_slot();
  if (scratch != nullptr) {
    ui_psram_free(scratch);
    scratch = nullptr;
  }
}

// Retryable allocation — never permanently latches nullptr on transient OOM.
TextCacheEntry *text_cache_entries() {
  static const bool s_evictor_reg = register_psram_cache_evictor(&text_cache_drop);
  (void)s_evictor_reg;
  TextCacheEntry *&entries = text_cache_entries_slot();
  if (entries == nullptr) {
    entries = static_cast<TextCacheEntry *>(
        ui_psram_calloc(static_cast<size_t>(kTextCacheEntries), sizeof(TextCacheEntry)));
  }
  return entries;
}

TextSpan *text_cache_scratch() {
  TextSpan *&scratch = text_cache_scratch_slot();
  if (scratch == nullptr) {
    scratch = static_cast<TextSpan *>(
        ui_psram_malloc(sizeof(TextSpan) * static_cast<size_t>(kTextScratchSpans)));
  }
  return scratch;
}

int text_decode_spans(const UiFontView &font, const char *text, int bpp, TextSpan *out, int cap) {
  if (bpp <= 0 || bpp > 4) return -1;
  const int bpp_max = (1 << bpp) - 1;
  int n = 0;
  int x_at = 0;
  for (const char *p = text; *p != 0;) {
    UiGlyphData glyph{};
    if (!font.find_glyph(utf8_next_cp(p), glyph)) {
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
        uint8_t run_alpha = 0;
        for (int gx = 0; gx < glyph.width; gx++) {
          uint8_t a = 0;
          if (bpp == 1) {
            if (bitmask == 0) {
              pixel_data = *data++;
              bitmask = 0x80U;
            }
            if ((pixel_data & bitmask) != 0U) a = 255U;
            bitmask >>= 1;
          } else {
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
            if (pixel != 0U) {
              a = (pixel == bpp_max)
                      ? 255U
                      : static_cast<uint8_t>((static_cast<int>(pixel) * 255 + (bpp_max >> 1)) / bpp_max);
            }
          }
          if (a != run_alpha) {
            if (run_start >= 0 && run_alpha > 0U) {
              if (n >= cap) return -1;
              out[n++] = {static_cast<int16_t>(gx0 + run_start),
                          static_cast<int16_t>(glyph.offset_y + gy),
                          static_cast<int16_t>(gx - run_start),
                          run_alpha, 0};
            }
            run_start = (a > 0U) ? gx : -1;
            run_alpha = a;
          }
        }
        if (run_start >= 0 && run_alpha > 0U) {
          if (n >= cap) return -1;
          out[n++] = {static_cast<int16_t>(gx0 + run_start),
                      static_cast<int16_t>(glyph.offset_y + gy),
                      static_cast<int16_t>(glyph.width - run_start),
                      run_alpha, 0};
        }
      }
    }
    x_at += glyph.advance;
  }
  return n;
}

const TextSpan *text_cache_get(const UiFontView &font, const char *text, int &count) {
  const int bpp = font.get_bpp();
  if (font.handle == nullptr || font.find_glyph_fn == nullptr || bpp <= 0 || bpp > 4) return nullptr;
  TextCacheEntry *entries = text_cache_entries();
  TextSpan *scratch = text_cache_scratch();
  const size_t len = std::strlen(text);
  if (entries == nullptr || scratch == nullptr || len >= sizeof(entries[0].text)) return nullptr;
  static uint32_t use_seq = 0;
  ++use_seq;
  TextCacheEntry *victim = &entries[0];
  for (int i = 0; i < kTextCacheEntries; i++) {
    TextCacheEntry &e = entries[i];
    if (e.font == font.handle && e.bpp == static_cast<uint8_t>(bpp) && std::strcmp(e.text, text) == 0) {
      e.last_use = use_seq;
      count = e.count;
      return e.spans;
    }
    if (e.last_use < victim->last_use) victim = &e;
  }
  const int n = text_decode_spans(font, text, bpp, scratch, kTextScratchSpans);
  if (n < 0) return nullptr;
  if (victim->cap < n) {
    if (victim->spans != nullptr) ui_psram_free(victim->spans);
    const size_t alloc_n = (n > 0) ? static_cast<size_t>(n) : 1U;
    victim->spans = static_cast<TextSpan *>(ui_psram_malloc(sizeof(TextSpan) * alloc_n));
    victim->cap = (victim->spans != nullptr) ? static_cast<uint16_t>(n) : 0;
    if (victim->spans == nullptr) {
      victim->font = nullptr;
      victim->bpp = 0;
      victim->text[0] = 0;
      victim->count = 0;
      count = n;
      return scratch;
    }
  }
  if (n > 0) std::memcpy(victim->spans, scratch, sizeof(TextSpan) * static_cast<size_t>(n));
  victim->font = font.handle;
  victim->bpp = static_cast<uint8_t>(bpp);
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
    if (!font.find_glyph(utf8_next_cp(p), glyph)) {
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

std::string sanitize_utf8_for_font(const std::string &str, UiFontView font) {
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
        if (cp >= 0x800 && !(cp >= 0xD800U && cp <= 0xDFFFU)) len = 3;
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
        if (cp >= 0x10000U && cp <= 0x10FFFFU) len = 4;
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
      if (font_supports_cp(font, 0x2014U)) append_utf8_cp(out, 0x2014U);
      else out.push_back('-');
    } else if (cp == 0x02B9 || cp == 0x02BB || cp == 0x02BC || cp == 0x201A || cp == 0x2032 ||
               cp == 0x2039 || cp == 0x203A || cp == 0xFF07) {
      out.push_back('\'');
    } else if (cp == 0x201E || cp == 0x2033 || cp == 0xFF02) {
      out.push_back('"');
    } else if (cp == 0xFF01) {
      out.push_back('!');
    } else if (cp == 0xFF1F) {
      out.push_back('?');
    } else if (font_supports_cp(font, cp)) {
      append_utf8_cp(out, cp);
    } else if (cp == 0x2018U || cp == 0x2019U) {
      out.push_back('\'');
    } else if (cp == 0x201CU || cp == 0x201DU) {
      out.push_back('"');
    } else if (cp == 0x2013U || cp == 0x2014U) {
      out.push_back('-');
    } else if (cp == 0x2026U) {
      out.append("...");
    }
  }
  return out;
}

std::string fit_text_to_width_font(const std::string &raw_str, int max_w, UiFontView font) {
  if (raw_str.empty() || font == nullptr) return "";

  struct FitCacheEntry {
    const void *font_ptr = nullptr;
    UiFontView::FindGlyphFn glyph_fn = nullptr;
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
    if (e.font_ptr == fptr && e.glyph_fn == font.find_glyph_fn && e.font_h == font.height &&
        e.max_w == max_w && e.raw_len == raw_str.size() && e.fnv_hash == h && e.raw_key == raw_str) {
      return e.fitted;
    }
  }

  const std::string str = sanitize_utf8_for_font(raw_str, font);
  if (str.empty()) return "";
  int w = 0, x_off = 0, base = 0, text_h = 0;
  font.measure(str.c_str(), &w, &x_off, &base, &text_h);
  std::string result;
  if (w <= max_w) {
    result = str;
  } else {
    // Binary search on valid UTF-8 codepoint boundaries (zero arbitrary byte cliff,
    // zero mid-multibyte slicing).
    const size_t n = str.size();
    size_t lo = 0;
    size_t hi = n;
    size_t best = 0;
    std::string cand;
    cand.reserve(n + 2U);
    while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2U;
      if (mid == lo) {
        mid = lo + 1U;
      }
      while (mid < hi && (static_cast<unsigned char>(str[mid]) & 0xC0U) == 0x80U) {
        ++mid;
      }
      if (mid >= hi) break;
      cand.assign(str, 0, mid);
      cand.append("..");
      font.measure(cand.c_str(), &w, &x_off, &base, &text_h);
      if (w <= max_w) {
        best = mid;
        lo = mid;
      } else {
        hi = mid;
      }
    }
    result.assign(str, 0, best);
    result.append("..");
  }

  auto &slot = cache[next_slot & 15U];
  next_slot = static_cast<uint8_t>((next_slot + 1U) & 15U);
  slot.font_ptr = fptr;
  slot.glyph_fn = font.find_glyph_fn;
  slot.font_h = font.height;
  slot.max_w = max_w;
  slot.raw_len = raw_str.size();
  slot.fnv_hash = h;
  slot.raw_key = raw_str;
  slot.fitted = result;
  return result;
}

int text_ink_right_edge_impl(Rgb565Surface &it, UiFontView font, const char *text) {
  (void)it;
  if (font == nullptr || text == nullptr || *text == '\0') return 0;
  int ink_x0 = 0, ink_x1 = 0, text_h = 0, baseline = 0;
  measure_text_ink_span(font, text, ink_x0, ink_x1, text_h, baseline);
  return std::max(0, ink_x1);
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

  int x_start = 0, y_start = 0, text_h = 0;
  compute_text_pen_origin(x, y, text, font, align, &x_start, &y_start, nullptr, nullptr, &text_h);
  if (text_h > 0 && (y_start + text_h <= cy0 || y_start > cy1)) return;

  const int eff_cy0 = std::max(0, cy0);
  const int eff_cy1 = std::min(SCREEN_H - 1, cy1);
  if (eff_cy1 < eff_cy0) return;

  const int bpp_max = (1 << bpp) - 1;
  const uint16_t full_hw = encode_hw_rgb565(color);
  const bool solid_bg = background.is_on();
  const int cr5 = (static_cast<int>(color.r) * 31 + 127) / 255;
  const int cg6 = (static_cast<int>(color.g) * 63 + 127) / 255;
  const int cb5 = (static_cast<int>(color.b) * 31 + 127) / 255;

  uint16_t lut[16]{};
  if (solid_bg) {
    const float diff_r = static_cast<float>(color.r) - static_cast<float>(background.r);
    const float diff_g = static_cast<float>(color.g) - static_cast<float>(background.g);
    const float diff_b = static_cast<float>(color.b) - static_cast<float>(background.b);
    for (int lv = 1; lv <= bpp_max; lv++) {
      if (lv == bpp_max) {
        lut[lv] = full_hw;
      } else {
        const float on = static_cast<float>(lv) / static_cast<float>(bpp_max);
        lut[lv] = encode_hw_rgb565(UiColor{
            static_cast<uint8_t>(diff_r * on + static_cast<float>(background.r)),
            static_cast<uint8_t>(diff_g * on + static_cast<float>(background.g)),
            static_cast<uint8_t>(diff_b * on + static_cast<float>(background.b)),
        });
      }
    }
  }

  auto blend_live = [&](uint16_t &dst, uint8_t alpha) {
    if (alpha >= 255U) {
      dst = full_hw;
      return;
    }
    if (solid_bg) {
      const int lv = std::clamp((static_cast<int>(alpha) * bpp_max + 127) / 255, 1, bpp_max);
      dst = lut[lv];
      return;
    }
    const uint16_t bg = decode_hw_rgb565_raw(dst);
    const int a_q8 = static_cast<int>(alpha) + static_cast<int>(alpha >> 7);
    const int ia = 256 - a_q8;
    const int r = (cr5 * a_q8 + ((bg >> 11) & 0x1F) * ia + 128) >> 8;
    const int g = (cg6 * a_q8 + ((bg >> 5) & 0x3F) * ia + 128) >> 8;
    const int b = (cb5 * a_q8 + (bg & 0x1F) * ia + 128) >> 8;
    dst = encode_hw_rgb565_raw(static_cast<uint16_t>(
        (std::min(31, r) << 11) | (std::min(63, g) << 5) | std::min(31, b)));
  };

  {
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
        const uint8_t sa = spans[i].alpha;
        if (sa >= 255U) {
          for (int k = 0; k < len; k++) dst[k] = full_hw;
        } else {
          for (int k = 0; k < len; k++) blend_live(dst[k], sa);
        }
      }
      return;
    }
  }

  rasterize_glyph_pixels(fb, font, text, x_start, y_start, eff_cy0, eff_cy1, bpp,
                         [&](uint16_t &dst, uint8_t pixel) {
                           if (pixel == bpp_max) {
                             dst = full_hw;
                           } else if (solid_bg) {
                             dst = lut[pixel];
                           } else {
                             const uint8_t a = static_cast<uint8_t>(
                                 (static_cast<int>(pixel) * 255 + (bpp_max >> 1)) / bpp_max);
                             blend_live(dst, a);
                           }
                         });
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

  int x_start = 0, y_start = 0, text_h = 0;
  compute_text_pen_origin(x, y, text, font, align, &x_start, &y_start, nullptr, nullptr, &text_h);
  if (text_h > 0 && (y_start + text_h <= cy0 || y_start > cy1)) return;

  const int eff_cy0 = std::max(0, cy0), eff_cy1 = std::min(SCREEN_H - 1, cy1);
  if (eff_cy1 < eff_cy0) return;

  const int bpp_max = (1 << bpp) - 1;
  const int a_clamped = std::min(256, alpha_q8);
  const uint16_t full_hw = encode_hw_rgb565(color);
  const int cr5 = (static_cast<int>(color.r) * 31 + 127) / 255;
  const int cg6 = (static_cast<int>(color.g) * 63 + 127) / 255;
  const int cb5 = (static_cast<int>(color.b) * 31 + 127) / 255;

  auto blend_px = [&](uint16_t &dst, int a) {
    if (a <= 0) return;
    if (a >= 256) { dst = full_hw; return; }
    const uint16_t bg = decode_hw_rgb565_raw(dst);
    const int ia = 256 - a;
    const int r = (cr5 * a + ((bg >> 11) & 0x1F) * ia + 128) >> 8;
    const int g = (cg6 * a + ((bg >> 5) & 0x3F) * ia + 128) >> 8;
    const int b = (cb5 * a + (bg & 0x1F) * ia + 128) >> 8;
    dst = encode_hw_rgb565_raw(static_cast<uint16_t>(
        (std::min(31, r) << 11) | (std::min(63, g) << 5) | std::min(31, b)));
  };

  {
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
        const int eff_a = (spans[i].alpha >= 255U)
                              ? a_clamped
                              : ((a_clamped * static_cast<int>(spans[i].alpha) + 127) / 255);
        for (int k = 0; k < len; k++) blend_px(dst[k], eff_a);
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



// ------------------------------------------------------------------------------
// 6. Album-Art / Wallpaper Composition & Retryable PSRAM ArtCanvasCache
// ------------------------------------------------------------------------------
int compute_art_row_scale_q8(int y, bool show_controls, bool show_metadata, int dim_level) {
  auto ramp = [](int yy, int y_a, int v_a, int y_b, int v_b) -> int {
    if (y_b <= y_a || yy <= y_a) return v_a;
    if (yy >= y_b) return v_b;
    return v_a + ((v_b - v_a) * (yy - y_a)) / (y_b - y_a);
  };
  if (dim_level == kArtDimLyrics) {
    const int top_y0 = dp_px(52);
    const int top_y1 = dp_px(104);
    const int bot_y0 = SCREEN_H - dp_px(104);
    const int bot_y1 = SCREEN_H - dp_px(52);
    if (y < top_y1) return ramp(y, top_y0, 108, top_y1, 132);
    if (y > bot_y0) return ramp(y, bot_y0, 132, bot_y1, 108);
    return 132;
  }
  if (dim_level >= kArtDimMenu) return 64;
  if (show_controls) return 140;
  if (show_metadata) return ramp(y, dp_px(104), 140, dp_px(148), 256);
  return 256;
}

void art_letterbox_bounds(bool show_metadata, bool show_controls, int &out_y0, int &out_y1) {
  if (show_metadata || show_controls) {
    out_y0 = dp_px(132);
    const int bot_pad = LATTER_ROUND_DISPLAY ? dp_px(66) : dp_px(54);
    out_y1 = std::max(out_y0 + dp_px(120), SCREEN_H - bot_pad);
  } else {
    const int half_h = (SCREEN_H * 31) / 100;
    out_y0 = std::max(0, SCREEN_CY - half_h);
    out_y1 = std::min(SCREEN_H, SCREEN_CY + half_h);
  }
}

namespace {

struct ArtScaleScratch {
  uint16_t row_buf[SCREEN_W];
  uint16_t sx_lut[SCREEN_W];
  uint16_t scaled_row[SCREEN_W];
  // Calloc-safe per-(mul_q8, endian_mode) 4x4 Bayer channel LUT (4 KiB in PSRAM, stays L1 D-cache hot).
  // lut_row_mask bit (y & 3) tracks which Bayer rows are populated so uniform dim modes build once per frame
  // and ramp rows build only their 4 active columns.
  int lut_mul_q8;
  int lut_mode;
  uint8_t lut_row_mask;
  uint16_t r_lut[16][32];
  uint16_t g_lut[16][64];
  uint16_t b_lut[16][32];
};

// Retryable allocation — never permanently latches nullptr on transient OOM.
ArtScaleScratch *art_scale_scratch_ptr() {
  static ArtScaleScratch *s_ptr = nullptr;
  if (s_ptr == nullptr) {
    s_ptr = static_cast<ArtScaleScratch *>(ui_psram_calloc(1, sizeof(ArtScaleScratch)));
  }
  return s_ptr;
}

inline void ensure_art_dim_lut_row(ArtScaleScratch &scratch, int mul_q8, int mode, int y) noexcept {
  if (scratch.lut_mul_q8 != mul_q8 || scratch.lut_mode != mode) {
    scratch.lut_mul_q8 = mul_q8;
    scratch.lut_mode = mode;
    scratch.lut_row_mask = 0U;
  }
  const uint8_t ry = static_cast<uint8_t>(y & 3);
  const uint8_t bit = static_cast<uint8_t>(1U << ry);
  if ((scratch.lut_row_mask & bit) != 0U) return;

  const uint32_t m = static_cast<uint32_t>(mul_q8);
  const bool swap_bytes = (mode == 2);
  const uint8_t *th = kBayer4[ry];
  for (int col = 0; col < 4; ++col) {
    const uint8_t bidx = th[col];
    const uint32_t t = static_cast<uint32_t>(bidx) * 16U + 8U;
    uint16_t *rl = scratch.r_lut[bidx];
    uint16_t *gl = scratch.g_lut[bidx];
    uint16_t *bl = scratch.b_lut[bidx];
    for (uint32_t v = 0U; v < 32U; ++v) {
      const uint32_t q = (v * m + t) >> 8;
      const uint16_t r_raw = static_cast<uint16_t>(q << 11);
      const uint16_t b_raw = static_cast<uint16_t>(q);
      rl[v] = swap_bytes ? bswap16_u16(r_raw) : r_raw;
      bl[v] = swap_bytes ? bswap16_u16(b_raw) : b_raw;
    }
    for (uint32_t v = 0U; v < 64U; ++v) {
      const uint32_t q = (v * m + t) >> 8;
      const uint16_t g_raw = static_cast<uint16_t>(q << 5);
      gl[v] = swap_bytes ? bswap16_u16(g_raw) : g_raw;
    }
  }
  scratch.lut_row_mask |= bit;
}

void dim_rgb565_row_dither(uint16_t *row_buf, int span_w, int mul_q8, int x0, int y) {
  if (mul_q8 >= 256 || span_w <= 0) return;
  if (mul_q8 < 0) mul_q8 = 0;
  const uint8_t *th = kBayer4[y & 3];
  const uint32_t m = static_cast<uint32_t>(mul_q8);
  for (int i = 0; i < span_w; i++) {
    const uint32_t p = row_buf[i];
    const uint32_t t = static_cast<uint32_t>(th[(x0 + i) & 3]) * 16U + 8U;
    const uint32_t r = (((p >> 11) & 0x1FU) * m + t) >> 8;
    const uint32_t g = (((p >> 5) & 0x3FU) * m + t) >> 8;
    const uint32_t b = ((p & 0x1FU) * m + t) >> 8;
    row_buf[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
  }
}

void blit_dimmed_rgb565_scanline(Rgb565Surface &it, int start_x, int y, int span_w,
                                 const uint16_t *src_row, int mul_q8, uint16_t *fallback_row_buf,
                                 ArtScaleScratch &scratch, uint16_t *mirror_fb = nullptr,
                                 int mirror_y_lo = 0, int mirror_y_hi = -1) {
  if (span_w <= 0 || y < 0 || y >= SCREEN_H || !row_in_clip_band(y) || src_row == nullptr) return;
  if (start_x >= SCREEN_W) return;
  if (start_x < 0) {
    const int skip = -start_x;
    if (skip >= span_w) return;
    src_row += skip;
    span_w -= skip;
    start_x = 0;
  }
  if (start_x + span_w > SCREEN_W) {
    span_w = SCREEN_W - start_x;
  }
  if (span_w <= 0) return;
  uint16_t *fb = active_frame_buf16();
  const size_t row_off = static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W) + static_cast<size_t>(start_x);
  if (mul_q8 >= 256) {
    blit_rgb565_scanline(it, start_x, y, span_w, src_row);
    if (mirror_fb != nullptr && fb != nullptr && y >= mirror_y_lo && y <= mirror_y_hi) {
      std::memcpy(mirror_fb + row_off, fb + row_off, static_cast<size_t>(span_w) * sizeof(uint16_t));
    }
    return;
  }
  if (fb != nullptr) {
    if (mul_q8 < 0) mul_q8 = 0;
    uint16_t *dst = fb + row_off;
    const uint8_t *th = kBayer4[y & 3];
    const int mode = active_frame_endian_mode();
    if (mode == 1 || mode < 0 || mode == 2) {
      ensure_art_dim_lut_row(scratch, mul_q8, (mode == 2) ? 2 : 1, y);
      const uint16_t *const r0 = scratch.r_lut[th[(start_x + 0) & 3]];
      const uint16_t *const g0 = scratch.g_lut[th[(start_x + 0) & 3]];
      const uint16_t *const b0 = scratch.b_lut[th[(start_x + 0) & 3]];
      const uint16_t *const r1 = scratch.r_lut[th[(start_x + 1) & 3]];
      const uint16_t *const g1 = scratch.g_lut[th[(start_x + 1) & 3]];
      const uint16_t *const b1 = scratch.b_lut[th[(start_x + 1) & 3]];
      const uint16_t *const r2 = scratch.r_lut[th[(start_x + 2) & 3]];
      const uint16_t *const g2 = scratch.g_lut[th[(start_x + 2) & 3]];
      const uint16_t *const b2 = scratch.b_lut[th[(start_x + 2) & 3]];
      const uint16_t *const r3 = scratch.r_lut[th[(start_x + 3) & 3]];
      const uint16_t *const g3 = scratch.g_lut[th[(start_x + 3) & 3]];
      const uint16_t *const b3 = scratch.b_lut[th[(start_x + 3) & 3]];
      int i = 0;
      for (; i + 3 < span_w; i += 4) {
        const uint32_t p0 = src_row[i];
        const uint32_t p1 = src_row[i + 1];
        const uint32_t p2 = src_row[i + 2];
        const uint32_t p3 = src_row[i + 3];
        dst[i]     = static_cast<uint16_t>(r0[p0 >> 11] | g0[(p0 >> 5) & 0x3FU] | b0[p0 & 0x1FU]);
        dst[i + 1] = static_cast<uint16_t>(r1[p1 >> 11] | g1[(p1 >> 5) & 0x3FU] | b1[p1 & 0x1FU]);
        dst[i + 2] = static_cast<uint16_t>(r2[p2 >> 11] | g2[(p2 >> 5) & 0x3FU] | b2[p2 & 0x1FU]);
        dst[i + 3] = static_cast<uint16_t>(r3[p3 >> 11] | g3[(p3 >> 5) & 0x3FU] | b3[p3 & 0x1FU]);
      }
      for (; i < span_w; ++i) {
        const uint32_t p = src_row[i];
        const uint8_t bidx = th[(start_x + i) & 3];
        dst[i] = static_cast<uint16_t>(
            scratch.r_lut[bidx][p >> 11] | scratch.g_lut[bidx][(p >> 5) & 0x3FU] | scratch.b_lut[bidx][p & 0x1FU]);
      }
      if (mirror_fb != nullptr && y >= mirror_y_lo && y <= mirror_y_hi) {
        std::memcpy(mirror_fb + row_off, dst, static_cast<size_t>(span_w) * sizeof(uint16_t));
      }
      return;
    }
    const uint32_t m = static_cast<uint32_t>(mul_q8);
    for (int i = 0; i < span_w; i++) {
      const uint32_t p = src_row[i];
      const uint32_t t = static_cast<uint32_t>(th[(start_x + i) & 3]) * 16U + 8U;
      const uint32_t r = (((p >> 11) & 0x1FU) * m + t) >> 8;
      const uint32_t g = (((p >> 5) & 0x3FU) * m + t) >> 8;
      const uint32_t b = ((p & 0x1FU) * m + t) >> 8;
      dst[i] = encode_hw_rgb565_raw(static_cast<uint16_t>((r << 11) | (g << 5) | b));
    }
    if (mirror_fb != nullptr && y >= mirror_y_lo && y <= mirror_y_hi) {
      std::memcpy(mirror_fb + row_off, dst, static_cast<size_t>(span_w) * sizeof(uint16_t));
    }
    return;
  }
  std::memcpy(fallback_row_buf, src_row, static_cast<size_t>(span_w) * sizeof(uint16_t));
  dim_rgb565_row_dither(fallback_row_buf, span_w, mul_q8, start_x, y);
  blit_rgb565_scanline(it, start_x, y, span_w, fallback_row_buf);
}

void art_row_span(int y, int cx, int &start_x, int &end_x) {
#if LATTER_ROUND_DISPLAY
  const int hw = circle_half_span(y, 0);
  start_x = std::max(0, cx - hw);
  end_x = std::min(SCREEN_W - 1, cx + hw);
#else
  (void)y;
  (void)cx;
  start_x = 0;
  end_x = SCREEN_W - 1;
#endif
}

void draw_album_art(Rgb565Surface &it, const uint16_t *src_data, int src_w, int src_h, int cx,
                    bool is_standard, bool is_letterbox, bool apply_scrim, bool show_metadata, int dim_level,
                    uint16_t *mirror_fb = nullptr, int mirror_y_lo = 0, int mirror_y_hi = -1) {
  ArtScaleScratch *scratch_p = art_scale_scratch_ptr();
  if (scratch_p == nullptr) return;
  auto &scratch = *scratch_p;
  uint16_t *const row_buf = scratch.row_buf;

  if (is_standard) {
    const ArtRect r = art_standard_rect(cx, src_w, src_h, show_metadata, apply_scrim);
    const int step_x = (src_w << 12) / r.w;
    const int step_y = (src_h << 12) / r.h;
    const int mul_q8 = (dim_level == kArtDimLyrics)
                           ? 132
                           : ((dim_level >= kArtDimMenu) ? 64 : (apply_scrim ? 140 : 256));
    int curr_y = 0;
    for (int y = 0; y < r.h; y++) {
      const int sy = std::min(src_h - 1, curr_y >> 12);
      curr_y += step_y;
      const int py = r.y0 + y;
      if (py < 0 || py >= SCREEN_H) continue;
      int x_start = 0;
      int x_end = r.w - 1;
      if (r.x0 + x_start < 0) x_start = -r.x0;
      if (r.x0 + x_end >= SCREEN_W) x_end = SCREEN_W - 1 - r.x0;
      const int span_w = x_end - x_start + 1;
      if (span_w <= 0 || span_w > SCREEN_W) continue;
      const uint16_t *src_row = src_data + static_cast<size_t>(sy) * static_cast<size_t>(src_w);
      int curr_x = x_start * step_x;
      for (int i = 0; i < span_w; i++) {
        row_buf[i] = src_row[std::min(src_w - 1, curr_x >> 12)];
        curr_x += step_x;
      }
      blit_dimmed_rgb565_scanline(it, r.x0 + x_start, py, span_w, row_buf, mul_q8, scratch.scaled_row, scratch);
    }
    fast_hline(it, r.x0 - 1, r.y0 - 1, r.w + 2, UiColor{88, 102, 124});
    fast_hline(it, r.x0 - 1, r.y0 + r.h, r.w + 2, UiColor{38, 45, 58});
    fast_vline(it, r.x0 - 1, r.y0, r.h, UiColor{56, 66, 82});
    fast_vline(it, r.x0 + r.w, r.y0, r.h, UiColor{56, 66, 82});
    return;
  }

  int start_y = 0;
  int end_y = SCREEN_H;
  if (is_letterbox) {
    art_letterbox_bounds(show_metadata, apply_scrim, start_y, end_y);
  }

  if (src_w == SCREEN_W && src_h == SCREEN_H) {
    const int view_h = std::max(1, end_y - start_y);
    const int src_y_off = std::max(0, (src_h - view_h) / 2);
    for (int y = start_y; y < end_y; y++) {
      const int sy = std::min(src_h - 1, src_y_off + (y - start_y));
      const int mul_q8 = compute_art_row_scale_q8(y, apply_scrim, show_metadata, dim_level);
      int start_x = 0, end_x = 0;
      art_row_span(y, cx, start_x, end_x);
      const int span_w = end_x - start_x + 1;
      if (span_w <= 0) continue;
      const uint16_t *src_row = src_data + static_cast<size_t>(sy) * static_cast<size_t>(SCREEN_W);
      blit_dimmed_rgb565_scanline(it, start_x, y, span_w, src_row + start_x, mul_q8, row_buf,
                                  scratch, mirror_fb, mirror_y_lo, mirror_y_hi);
    }
  } else {
    const int view_h = std::max(1, end_y - start_y);
    const int step_x = (src_w << 12) / SCREEN_W;
    const int step_y = (src_h << 12) / view_h;
    const int step_fp = std::min(step_x, step_y);
    const int vis_src_w_fp = SCREEN_W * step_fp;
    const int vis_src_h_fp = view_h * step_fp;
    const int src_x0_fp = std::max(0, ((src_w << 12) - vis_src_w_fp) / 2);
    const int src_y0_fp = std::max(0, ((src_h << 12) - vis_src_h_fp) / 2);

    bool ppa_scaled = false;
#if defined(USE_ESP32) && defined(CONFIG_IDF_TARGET_ESP32P4) && !LATTER_ROUND_DISPLAY
    uint16_t *const fb_tgt = active_frame_buf16();
    if (fb_tgt != nullptr && !is_letterbox && start_y == 0 && end_y == SCREEN_H &&
        clip_y0() <= 0 && clip_y1() >= SCREEN_H - 1) {
      const int crop_w = std::clamp(vis_src_w_fp >> 12, 1, src_w);
      const int crop_h = std::clamp(vis_src_h_fp >> 12, 1, src_h);
      const int crop_x0 = std::clamp((src_w - crop_w) / 2, 0, src_w - crop_w);
      const int crop_y0 = std::clamp((src_h - crop_h) / 2, 0, src_h - crop_h);
      if (ppa_srm_scale_rgb565(fb_tgt, SCREEN_W, 0, 0, SCREEN_W, SCREEN_H,
                               src_data, src_w, src_h, crop_x0, crop_y0, crop_w, crop_h)) {
        ppa_scaled = true;
        for (int y = 0; y < SCREEN_H; ++y) {
          const int mul_q8 = compute_art_row_scale_q8(y, apply_scrim, show_metadata, dim_level);
          uint16_t *const dst_row = fb_tgt + static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
          if (mul_q8 < 256) {
            blit_dimmed_rgb565_scanline(it, 0, y, SCREEN_W, dst_row, mul_q8, row_buf,
                                        scratch, mirror_fb, mirror_y_lo, mirror_y_hi);
          } else if (mirror_fb != nullptr && mirror_fb != fb_tgt && y >= mirror_y_lo && y <= mirror_y_hi) {
            std::memcpy(mirror_fb + static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W),
                        dst_row, static_cast<size_t>(SCREEN_W) * sizeof(uint16_t));
          }
        }
      }
    }
#endif
    if (!ppa_scaled) {
      uint16_t *const sx_lut = scratch.sx_lut;
      uint16_t *const scaled_row = scratch.scaled_row;
      {
        int curr_x = src_x0_fp;
        for (int x = 0; x < SCREEN_W; x++) {
          sx_lut[x] = static_cast<uint16_t>(std::min(src_w - 1, curr_x >> 12));
          curr_x += step_fp;
        }
      }

      int curr_y = src_y0_fp;
      int prev_sy = -1;
      int prev_sx0 = SCREEN_W;
      int prev_sx1 = -1;
      for (int y = start_y; y < end_y; y++) {
        const int sy = std::min(src_h - 1, std::max(0, curr_y >> 12));
        curr_y += step_fp;
        const int mul_q8 = compute_art_row_scale_q8(y, apply_scrim, show_metadata, dim_level);
        int start_x = 0, end_x = 0;
        art_row_span(y, cx, start_x, end_x);
        const int span_w = end_x - start_x + 1;
        if (span_w <= 0) continue;
        if (sy != prev_sy) {
          const uint16_t *src_row = src_data + static_cast<size_t>(sy) * static_cast<size_t>(src_w);
          for (int x = start_x; x <= end_x; x++) {
            scaled_row[x] = src_row[sx_lut[x]];
          }
          prev_sy = sy;
          prev_sx0 = start_x;
          prev_sx1 = end_x;
        } else if (start_x < prev_sx0 || end_x > prev_sx1) {
          // On round displays the visible chord widens toward the equator across repeated source rows.
          const uint16_t *src_row = src_data + static_cast<size_t>(sy) * static_cast<size_t>(src_w);
          for (int x = start_x; x < prev_sx0; x++) {
            scaled_row[x] = src_row[sx_lut[x]];
          }
          for (int x = prev_sx1 + 1; x <= end_x; x++) {
            scaled_row[x] = src_row[sx_lut[x]];
          }
          if (start_x < prev_sx0) prev_sx0 = start_x;
          if (end_x > prev_sx1) prev_sx1 = end_x;
        }
        blit_dimmed_rgb565_scanline(it, start_x, y, span_w, scaled_row + start_x, mul_q8, row_buf + start_x,
                                    scratch, mirror_fb, mirror_y_lo, mirror_y_hi);
      }
    }
  }

  if (is_letterbox) {
    int x0_t = 0, x1_t = 0, x0_b = 0, x1_b = 0;
    art_row_span(start_y, cx, x0_t, x1_t);
    art_row_span(end_y - 1, cx, x0_b, x1_b);
    if (x1_t > x0_t) fast_hline(it, x0_t, start_y, x1_t - x0_t + 1, UiColor{68, 80, 98});
    if (x1_b > x0_b) fast_hline(it, x0_b, end_y - 1, x1_b - x0_b + 1, UiColor{46, 54, 68});
  }
}

ArtCanvasCache *g_art_canvas_entries = nullptr;

// Retryable allocation — never permanently latches nullptr on transient OOM.
ArtCanvasCache *art_canvas_entries() {
  if constexpr (kArtCacheActiveSlots <= 0) {
    return nullptr;
  }
  if (g_art_canvas_entries == nullptr) {
    g_art_canvas_entries = static_cast<ArtCanvasCache *>(
        ui_psram_calloc(static_cast<size_t>(kArtCacheActiveSlots), sizeof(ArtCanvasCache)));
  }
  return g_art_canvas_entries;
}

uint32_t art_sample_hash(const uint16_t *data, int w, int h) {
  uint32_t hash = 2166136261U ^ (static_cast<uint32_t>(w) * 31U) ^ (static_cast<uint32_t>(h) << 16);
  const uint32_t n = static_cast<uint32_t>(w) * static_cast<uint32_t>(h);
  if (n <= 1U) return (n == 1U) ? ((hash ^ data[0]) * 16777619U) : hash;
  for (uint32_t i = 0; i < 64; i++) {
    const uint32_t idx = static_cast<uint32_t>((static_cast<uint64_t>(i) * (n - 1)) / 63U);
    hash = (hash ^ data[idx]) * 16777619U;
  }
  return hash;
}

void art_canvas_spans(ArtCanvasCache &e, int cx, int src_w, int src_h, bool is_standard,
                      bool is_letterbox, bool apply_scrim, bool show_metadata) {
  for (int y = 0; y < SCREEN_H; y++) {
    e.row_x0[y] = 0;
    e.row_len[y] = 0;
  }
  if (is_standard) {
    const ArtRect r = art_standard_rect(cx, src_w, src_h, show_metadata, apply_scrim);
    const int x0 = std::max(0, r.x0 - 1);
    const int x1 = std::min(SCREEN_W - 1, r.x0 + r.w);
    for (int y = std::max(0, r.y0 - 1); y <= std::min(SCREEN_H - 1, r.y0 + r.h); y++) {
      e.row_x0[y] = static_cast<int16_t>(x0);
      e.row_len[y] = static_cast<int16_t>(std::max(0, x1 - x0 + 1));
    }
    return;
  }
  if (!is_letterbox) {
#if LATTER_ROUND_DISPLAY
    for (int y = 0; y < SCREEN_H; y++) {
      int start_x = 0, end_x = 0;
      art_row_span(y, cx, start_x, end_x);
      e.row_x0[y] = static_cast<int16_t>(start_x);
      e.row_len[y] = static_cast<int16_t>(std::max(0, end_x - start_x + 1));
    }
#else
    for (int y = 0; y < SCREEN_H; y++) {
      e.row_x0[y] = 0;
      e.row_len[y] = static_cast<int16_t>(SCREEN_W);
    }
#endif
    return;
  }
  int start_y = 0;
  int end_y = SCREEN_H;
  art_letterbox_bounds(show_metadata, apply_scrim, start_y, end_y);
  for (int y = std::max(0, start_y); y < std::min(SCREEN_H, end_y); y++) {
    int start_x = 0, end_x = 0;
    art_row_span(y, cx, start_x, end_x);
    e.row_x0[y] = static_cast<int16_t>(start_x);
    e.row_len[y] = static_cast<int16_t>(std::max(0, end_x - start_x + 1));
  }
}

}  // namespace

ArtRect art_standard_rect(int cx, int src_w, int src_h, bool show_metadata, bool apply_scrim) {
  const int box = STD_ART_SIZE;
  int art_cy = SCREEN_CY;
  if (show_metadata || apply_scrim) {
    const int meta_bottom = dp_px(128);
    const int bot_pad = LATTER_ROUND_DISPLAY ? dp_px(62) : dp_px(38);
    art_cy = (meta_bottom + (SCREEN_H - bot_pad)) / 2;
  }
  int w = box;
  int h = box;
  if (src_w > src_h && src_w > 0) {
    h = std::max(1, (box * src_h) / src_w);
  } else if (src_h > src_w && src_h > 0) {
    w = std::max(1, (box * src_w) / src_h);
  }
  return {cx - w / 2, art_cy - h / 2, w, h};
}

void art_canvas_drop(const void *data) {
  if (g_art_canvas_entries == nullptr || data == nullptr) return;
  for (int i = 0; i < kArtCacheActiveSlots; i++) {
    ArtCanvasCache &e = g_art_canvas_entries[i];
    if (e.data != data) continue;
    PsramPhaseBuf pb{e.px_raw, e.px};
    ui_psram_phase_free(pb);
    e.px_raw = nullptr;
    e.px = nullptr;
    e.data = nullptr;
    e.last_use = 0;
  }
}

void art_canvas_drop_all() noexcept {
  if (g_art_canvas_entries == nullptr) return;
  for (int i = 0; i < kArtCacheActiveSlots; i++) {
    ArtCanvasCache &e = g_art_canvas_entries[i];
    if (e.px != nullptr || e.px_raw != nullptr) {
      PsramPhaseBuf pb{e.px_raw, e.px};
      ui_psram_phase_free(pb);
      e.px_raw = nullptr;
      e.px = nullptr;
    }
    e.data = nullptr;
    e.last_use = 0;
  }
}

namespace {
constexpr size_t kMaxPsramCacheEvictors = 16U;
PsramCacheEvictorFn g_psram_cache_evictors[kMaxPsramCacheEvictors]{};
size_t g_psram_cache_evictor_count = 0U;
}  // namespace

bool register_psram_cache_evictor(PsramCacheEvictorFn fn) noexcept {
  if (fn == nullptr) return false;
  for (size_t i = 0; i < g_psram_cache_evictor_count; ++i) {
    if (g_psram_cache_evictors[i] == fn) return true;
  }
  if (g_psram_cache_evictor_count >= kMaxPsramCacheEvictors) return false;
  g_psram_cache_evictors[g_psram_cache_evictor_count++] = fn;
  return true;
}

void evict_registered_psram_caches() noexcept {
  for (size_t i = 0; i < g_psram_cache_evictor_count; ++i) {
    if (g_psram_cache_evictors[i] != nullptr) {
      g_psram_cache_evictors[i]();
    }
  }
}

bool render_album_art_canvas_view(Rgb565Surface &it, ArtImageView target_art, int cx,
                                  bool is_standard, bool is_letterbox, bool apply_scrim,
                                  bool show_metadata, int dim_level) {
  if (!target_art.valid()) return false;
  const uint16_t *src_data = target_art.data;
  const int src_w = target_art.width;
  const int src_h = target_art.height;
  const bool full_bleed = !is_standard && !is_letterbox;

  if (full_bleed) {
    frame_base_claim();
  } else {
    frame_base(it, kChromeShadow);
  }
  struct ArtTimer {
    uint64_t t0;
    ~ArtTimer() { prof_art_end(t0); }
  } art_timer{prof_section_begin()};

  uint16_t *fb = active_frame_buf16();
  ArtCanvasCache *entries = art_canvas_entries();
  if (fb == nullptr || entries == nullptr) {
    if (full_bleed) fill_rect_row_major(it, 0, 0, SCREEN_W, SCREEN_H, kChromeShadow);
    draw_album_art(it, src_data, src_w, src_h, cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
    backdrop_active() = true;
    return true;
  }

  const uint32_t mode = (is_standard ? 1U : 0U) | (is_letterbox ? 2U : 0U) | (apply_scrim ? 4U : 0U) |
                        (show_metadata ? 8U : 0U) | (static_cast<uint32_t>(dim_level & 0xF) << 4) |
                        (static_cast<uint32_t>(cx & 0xFFFF) << 8) |
                        (static_cast<uint32_t>(active_frame_endian_mode() & 0x7) << 24);
  const uint32_t sample = art_sample_hash(src_data, src_w, src_h);
  static uint32_t use_seq = 0;
  ++use_seq;

  ArtCanvasCache *hit = nullptr;
  ArtCanvasCache *victim = &entries[0];
  for (int i = 0; i < kArtCacheActiveSlots; i++) {
    ArtCanvasCache &e = entries[i];
    if (e.px != nullptr && e.data == src_data && e.w == src_w && e.h == src_h &&
        e.sample == sample && e.mode == mode) {
      hit = &e;
      break;
    }
    if (e.last_use < victim->last_use) victim = &e;
  }

  const int raw_lo = dirty_band().is_full() ? 0 : std::max(0, dirty_band().band_y0());
  const int raw_hi = dirty_band().is_full() ? (SCREEN_H - 1) : std::min(SCREEN_H - 1, dirty_band().band_y1());
  const int y_lo = std::max(clip_y0(), raw_lo);
  const int y_hi = std::min(clip_y1(), raw_hi);
  bool mirrored_to_fb = false;

  if (hit == nullptr) {
    static const bool s_mdi_evictor_reg = register_psram_cache_evictor(&mdi_raster_cache_drop);
    (void)s_mdi_evictor_reg;
    const bool full_rect_overwrite = full_bleed && (LATTER_ROUND_DISPLAY == 0);
    const size_t px_count = static_cast<size_t>(SCREEN_W) * static_cast<size_t>(SCREEN_H);
    const size_t frame_bytes = px_count * sizeof(uint16_t);
    if (victim->px == nullptr) {
      if (LATTER_PSRAM_MB < 16 || frame_bytes > 1536U * 1024U) {
        evict_registered_psram_caches();
      }
      const PsramPhaseBuf pb = ui_psram_phase_alloc(frame_bytes, fb, !full_rect_overwrite);
      victim->px_raw = pb.raw;
      victim->px = pb.px;
    }
    if (victim->px == nullptr) {
      if (full_bleed) fill_rect_row_major(it, 0, 0, SCREEN_W, SCREEN_H, kChromeShadow);
      draw_album_art(it, src_data, src_w, src_h, cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
      backdrop_active() = true;
      return true;
    }
    const int saved_cy0 = clip_y0();
    const int saved_cy1 = clip_y1();
    reset_clip_y_band();
    active_frame_buf16() = victim->px;
    // On a full-bleed miss, mirror each L1-hot scanline directly to fb to avoid a cold 434 KB PSRAM read-back.
    uint16_t *const mirror_dst = (full_bleed && y_hi >= y_lo) ? fb : nullptr;
    draw_album_art(it, src_data, src_w, src_h, cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level,
                   mirror_dst, y_lo, y_hi);
    active_frame_buf16() = fb;
    set_clip_y_band(saved_cy0, saved_cy1);
    if (full_rect_overwrite) {
      ui_psram_cache_writeback(victim->px, frame_bytes);
    }
    art_canvas_spans(*victim, cx, src_w, src_h, is_standard, is_letterbox, apply_scrim, show_metadata);
    victim->data = src_data;
    victim->w = static_cast<int16_t>(src_w);
    victim->h = static_cast<int16_t>(src_h);
    victim->sample = sample;
    victim->mode = mode;
    hit = victim;
    mirrored_to_fb = (mirror_dst != nullptr);
  }
  hit->last_use = use_seq;

  if (!mirrored_to_fb && y_hi >= y_lo) {
    if (full_bleed && LATTER_ROUND_DISPLAY == 0) {
      blit_psram_band_fast(fb, hit->px, y_lo, y_hi);
    } else {
      for (int y = y_lo; y <= y_hi; y++) {
        const int len = hit->row_len[y];
        if (len <= 0) continue;
        const size_t off = static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W) + static_cast<size_t>(hit->row_x0[y]);
        std::memcpy(fb + off, hit->px + off, static_cast<size_t>(len) * sizeof(uint16_t));
      }
    }
  }
  backdrop_active() = true;
  return true;
}

bool render_lyrics_art_backdrop_view(Rgb565Surface &it, ArtImageView target_art, int cx,
                                     UiColor theme_primary, UiColor col_bg) {
  if (target_art.valid() &&
      render_album_art_canvas_view(it, target_art, cx, /*is_standard=*/false, /*is_letterbox=*/false,
                                   /*apply_scrim=*/true, /*show_metadata=*/false, kArtDimLyrics)) {
    return true;
  }
  frame_base_claim();
  backdrop_active() = true;
  const int ys = std::max(0, clip_y0());
  const int ye = std::min(SCREEN_H - 1, clip_y1());
  const int peak_y = std::max(1, (SCREEN_H * 38) / 100);
  const int tail_den = std::max(1, SCREEN_H - 1 - peak_y);
  for (int py = ys; py <= ye; py++) {
    const int w_q8 = (py <= peak_y)
                         ? (18 + (28 * py) / peak_y)
                         : (46 - (28 * (py - peak_y)) / tail_den);
    const int iw_q8 = 256 - w_q8;
    const uint32_t r88 = static_cast<uint32_t>(static_cast<int>(col_bg.r) * iw_q8 +
                                               static_cast<int>(theme_primary.r) * w_q8);
    const uint32_t g88 = static_cast<uint32_t>(static_cast<int>(col_bg.g) * iw_q8 +
                                               static_cast<int>(theme_primary.g) * w_q8);
    const uint32_t b88 = static_cast<uint32_t>(static_cast<int>(col_bg.b) * iw_q8 +
                                               static_cast<int>(theme_primary.b) * w_q8);
    fast_hline_dither(it, 0, py, SCREEN_W, r88, g88, b88);
  }
  return false;
}

bool render_page_wallpaper_base_view(Rgb565Surface &it, ArtImageView wallpaper_img,
                                     bool has_wallpaper, int cx, UiColor col_bg, int dim_level) {
  if (has_wallpaper && render_album_art_canvas_view(it, wallpaper_img, cx, false, false, true, false, dim_level)) {
    return true;
  }
  frame_base(it, col_bg);
  return false;
}

}  // namespace latter_ui
