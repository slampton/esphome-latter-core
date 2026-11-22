#include "ui_gfx.h"
#include "ui_dirty_band.h"

#include <algorithm>
#include <cmath>

namespace latter_ui {

// ------------------------------------------------------------------------------
// 1. WCAG 4.5:1 Frosted Glass Backdrop Sampling, Luminance LUT & 2-Tier Fill Solver
// ------------------------------------------------------------------------------
const float *srgb_linear_lut() {
  static float lut[256];
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < 256; i++) {
      const float c = static_cast<float>(i) / 255.0f;
      lut[i] = (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    ready = true;
  }
  return lut;
}

float relative_luminance(UiColor c) {
  const float *lut = srgb_linear_lut();
  return 0.2126f * lut[c.r] + 0.7152f * lut[c.g] + 0.0722f * lut[c.b];
}

float contrast_ratio(float la, float lb) {
  const float hi = std::max(la, lb);
  const float lo = std::min(la, lb);
  return (hi + 0.05f) / (lo + 0.05f);
}

UiColor mix_color_q8(UiColor c_under, UiColor c_over, int a) {
  a = std::max(0, std::min(256, a));
  const int ia = 256 - a;
  return UiColor{
      static_cast<uint8_t>((c_over.r * a + c_under.r * ia) >> 8),
      static_cast<uint8_t>((c_over.g * a + c_under.g * ia) >> 8),
      static_cast<uint8_t>((c_over.b * a + c_under.b * ia) >> 8),
  };
}

GlassBackdrop sample_glass_backdrop(int x0, int y0, int w, int h) {
  GlassBackdrop out;
  if (w <= 0 || h <= 0 || (y0 + h) <= clip_y0() || y0 > clip_y1()) return out;
  const uint16_t *fb = active_frame_buf16();
  const int xs = std::max(0, x0);
  const int xe = std::min(SCREEN_W, x0 + w);
  const int ys = std::max(0, std::max(clip_y0(), y0));
  const int ye = std::min(SCREEN_H, std::min(clip_y1() + 1, y0 + h));
  if (fb == nullptr || xe <= xs || ye <= ys) return out;
  const float *lut = srgb_linear_lut();
  const int mode = active_frame_endian_mode();
  const int step = std::max(3, h / 12);
  uint32_t cnt[32] = {0}, sr[32] = {0}, sg[32] = {0}, sb[32] = {0};
  uint32_t tr = 0, tg = 0, tb = 0, n = 0;
  for (int y = ys + step / 2; y < ye; y += step) {
    const uint16_t *row = fb + static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
    for (int x = xs + step / 2; x < xe; x += step) {
      const uint16_t hw = row[x];
      const uint16_t p = (mode == 1 || mode < 0)
                             ? decode_hw_rgb565_mode<1>(hw)
                             : ((mode == 2) ? decode_hw_rgb565_mode<2>(hw)
                                            : ((mode == 4) ? decode_hw_rgb565_mode<4>(hw)
                                                           : decode_hw_rgb565_mode<3>(hw)));
      const int r5 = (p >> 11) & 0x1F;
      const int g6 = (p >> 5) & 0x3F;
      const int b5 = p & 0x1F;
      const int r = (r5 << 3) | (r5 >> 2);
      const int g = (g6 << 2) | (g6 >> 4);
      const int b = (b5 << 3) | (b5 >> 2);
      const float lum = 0.2126f * lut[r] + 0.7152f * lut[g] + 0.0722f * lut[b];
      const int bin = std::min(31, static_cast<int>(lum * 32.0f));
      cnt[bin]++;
      sr[bin] += static_cast<uint32_t>(r);
      sg[bin] += static_cast<uint32_t>(g);
      sb[bin] += static_cast<uint32_t>(b);
      tr += static_cast<uint32_t>(r);
      tg += static_cast<uint32_t>(g);
      tb += static_cast<uint32_t>(b);
      n++;
    }
  }
  if (n == 0) return out;
  const uint32_t want = std::max<uint32_t>(1U, (n * kGlassBrightPercent + 99U) / 100U);
  uint32_t acc = 0, ar = 0, ag = 0, ab = 0;
  for (int bin = 31; bin >= 0 && acc < want; bin--) {
    acc += cnt[bin];
    ar += sr[bin];
    ag += sg[bin];
    ab += sb[bin];
  }
  out.bright = UiColor{
      static_cast<uint8_t>(ar / acc),
      static_cast<uint8_t>(ag / acc),
      static_cast<uint8_t>(ab / acc),
  };
  out.mean = UiColor{
      static_cast<uint8_t>(tr / n),
      static_cast<uint8_t>(tg / n),
      static_cast<uint8_t>(tb / n),
  };
  out.valid = true;
  return out;
}

GlassBackdrop sample_glass_mean(int x0, int y0, int w, int h) {
  GlassBackdrop out;
  if (w <= 0 || h <= 0 || (y0 + h) <= clip_y0() || y0 > clip_y1()) return out;
  const uint16_t *fb = active_frame_buf16();
  const int xs = std::max(0, x0);
  const int xe = std::min(SCREEN_W, x0 + w);
  const int ys = std::max(0, std::max(clip_y0(), y0));
  const int ye = std::min(SCREEN_H, std::min(clip_y1() + 1, y0 + h));
  if (fb == nullptr || xe <= xs || ye <= ys) return out;
  const int mode = active_frame_endian_mode();
  const int step = std::max(4, std::max(w, h) / 6);
  uint32_t tr = 0, tg = 0, tb = 0, n = 0;
  for (int y = ys + step / 2; y < ye; y += step) {
    const uint16_t *row = fb + static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
    for (int x = xs + step / 2; x < xe; x += step) {
      const uint16_t hw = row[x];
      const uint16_t p = (mode == 1 || mode < 0)
                             ? decode_hw_rgb565_mode<1>(hw)
                             : ((mode == 2) ? decode_hw_rgb565_mode<2>(hw)
                                            : ((mode == 4) ? decode_hw_rgb565_mode<4>(hw)
                                                           : decode_hw_rgb565_mode<3>(hw)));
      const uint32_t r5 = (p >> 11) & 0x1FU;
      const uint32_t g6 = (p >> 5) & 0x3FU;
      const uint32_t b5 = p & 0x1FU;
      tr += (r5 << 3) | (r5 >> 2);
      tg += (g6 << 2) | (g6 >> 4);
      tb += (b5 << 3) | (b5 >> 2);
      n++;
    }
  }
  if (n == 0) return out;
  out.mean = UiColor{
      static_cast<uint8_t>(tr / n),
      static_cast<uint8_t>(tg / n),
      static_cast<uint8_t>(tb / n),
  };
  out.bright = out.mean;
  out.valid = true;
  return out;
}

bool is_semantic_glass_color(UiColor c) {
  const int mx = std::max({static_cast<int>(c.r), static_cast<int>(c.g), static_cast<int>(c.b)});
  const int mn = std::min({static_cast<int>(c.r), static_cast<int>(c.g), static_cast<int>(c.b)});
  const int chroma = mx - mn;
  if (chroma <= 12) return false;
  if (c.r == kChromeHomePill.r && c.g == kChromeHomePill.g && c.b == kChromeHomePill.b) return false;
  if (c.r <= c.g && c.g <= c.b && mx <= 96 && chroma <= 30) return false;
  return true;
}

void normalize_glass_chrome(UiColor caller_bg, UiColor caller_brd,
                            GlassVariant &inout_variant, UiColor &out_base_tint,
                            UiColor &out_rim_tint) {
  const UiColor neutral_base =
      (inout_variant == GlassVariant::ReadableDark) ? kChromeModalBg : kChromeGlassTint;
  const bool sem_bg = is_semantic_glass_color(caller_bg);
  const bool sem_brd = is_semantic_glass_color(caller_brd);

  if (sem_bg) {
    const int bg_mx = std::max({static_cast<int>(caller_bg.r),
                                static_cast<int>(caller_bg.g),
                                static_cast<int>(caller_bg.b)});
    const UiColor wash_src = (bg_mx < 56 && sem_brd) ? caller_brd : caller_bg;
    out_base_tint = mix_color_q8(neutral_base, wash_src, kGlassSemanticWashMaxQ8);
  } else if (sem_brd) {
    out_base_tint = mix_color_q8(neutral_base, caller_brd, 28);
  } else {
    out_base_tint = neutral_base;
  }

  if (sem_brd) {
    out_rim_tint = mix_color_q8(kChromeGlassSheen, caller_brd, kGlassSemanticRimMaxQ8);
  } else if (sem_bg) {
    out_rim_tint = mix_color_q8(kChromeGlassSheen, caller_bg, kGlassSemanticRimMaxQ8);
  } else {
    out_rim_tint = kChromeGlassSheen;
  }
}

int glass_alpha_for_text(UiColor tint, UiColor backdrop, UiColor text, int min_alpha_q8) {
  const float raw_lt = relative_luminance(text);
  const float lt = (raw_lt < 0.18f) ? relative_luminance(kChromeMuted) : raw_lt;
  const int start_a = std::max(0, std::min(256, min_alpha_q8));
  for (int a = start_a; a < 256; a += 8) {
    if (contrast_ratio(lt, relative_luminance(mix_color_q8(backdrop, tint, a))) >= kGlassTextContrast) {
      return a;
    }
  }
  for (int a = start_a; a <= 248; a += 8) {
    if (contrast_ratio(lt, relative_luminance(mix_color_q8(backdrop, tint, a))) >= kGlassTextContrastMin) {
      return a;
    }
  }
  return 248;
}

int glass_alpha_smooth(int &state, int target, uint32_t dt_ms) {
  if (state <= 0 || target >= state) {
    state = target;
  } else {
    const uint32_t clamped_dt = std::min<uint32_t>(dt_ms, 250U);
    const int fall = (clamped_dt == 0U)
                         ? 0
                         : std::max(1, static_cast<int>((static_cast<uint32_t>(kGlassAlphaFallQ8) * clamped_dt + 8U) / 16U));
    state = std::max(target, state - fall);
  }
  return state;
}

void resolve_glass_fill(const GlassBackdrop &bd, UiColor tint, UiColor ref_text,
                        UiColor &out_tint, int &out_a_top, int &out_a_bot,
                        int &out_rim_top, int &out_rim_bot, int explicit_alpha_q8,
                        GlassVariant variant) {
  const bool dark_tint = (std::max({tint.r, tint.g, tint.b}) <= 160);
  const bool is_readable_dark = (variant == GlassVariant::ReadableDark);

  if (!is_readable_dark) {
    const int frost_q8 = dark_tint ? (is_semantic_glass_color(tint) ? 32 : 54) : 0;
    out_tint = (frost_q8 > 0) ? mix_color_q8(tint, kChromeHomePill, frost_q8) : tint;
    const int base_a = (explicit_alpha_q8 >= 0) ? explicit_alpha_q8 : kGlassClearAlphaQ8;
    const int da = dark_tint ? 12 : 0;
    out_a_top = std::min(256, base_a + da);
    out_a_bot = std::max(0, base_a - (da * 2) / 3);
    out_rim_top = std::min(256, kGlassSheenTopQ8 + 18);
    out_rim_bot = std::min(256, kGlassSheenBotQ8 + 12);
    return;
  }

  const int bg_lvl = bd.valid
                         ? ((static_cast<int>(bd.mean.r) + static_cast<int>(bd.mean.g) + static_cast<int>(bd.mean.b)) / 3)
                         : 0;
  out_tint = tint;

  const float raw_fg_lum = relative_luminance(ref_text);
  const UiColor contrast_fg =
      ((static_cast<int>(ref_text.r) + static_cast<int>(ref_text.g) + static_cast<int>(ref_text.b)) < 48)
          ? kChromeGlassSheen
          : ((raw_fg_lum < 0.18f) ? kChromeMuted : ref_text);
  const float fg_lum = relative_luminance(contrast_fg);

  int base_a = explicit_alpha_q8;
  if (base_a < 0) {
    base_a = bd.valid
                 ? glass_alpha_for_text(out_tint, bd.bright, contrast_fg, kGlassReadableAlphaMinQ8)
                 : kGlassReadableAlphaMinQ8;
  }

  const int da = 8;
  out_a_top = std::min(256, base_a + da);
  out_a_bot = std::max(200, base_a - (da * 2) / 3);
  if (bd.valid && explicit_alpha_q8 < 0 &&
      contrast_ratio(fg_lum, relative_luminance(mix_color_q8(bd.bright, out_tint, out_a_bot))) < kGlassTextContrastMin) {
    out_a_bot = base_a;
  }
  const int rim_boost = (bg_lvl < 32) ? ((32 - bg_lvl) * 18) / 32 : 8;
  out_rim_top = std::min(256, kGlassSheenTopQ8 + rim_boost);
  out_rim_bot = std::min(256, kGlassSheenBotQ8 + (rim_boost * 2) / 3);
}

UiColor sample_glass_surface_color(int x0, int y0, int w, int h, UiColor tint, GlassVariant variant) {
  const GlassBackdrop bd = sample_glass_backdrop(x0, y0, w, h);
  GlassVariant eff_var = variant;
  UiColor norm_tint, rim_col;
  normalize_glass_chrome(tint, kChromeCardBorder, eff_var, norm_tint, rim_col);
  UiColor eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, norm_tint, kChromeGlassSheen, eff_tint, a_top, a_bot, rim_top, rim_bot, -1, eff_var);
  return bd.valid ? mix_color_q8(bd.mean, eff_tint, (a_top + a_bot) / 2) : eff_tint;
}

void fill_round_rect_aa_impl(Rgb565Surface &it, int x0, int y0, int w, int h, float rad,
                             UiColor c_top, UiColor c_bot, int hole) {
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
    UiColor rc = c_top;
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

namespace {

template <int Mode>
void fill_round_rect_glass_mode(uint16_t *fb, int x0, int y0, int w, int h, float rad,
                                UiColor tint, int a_top, int a_bot, int hole) {
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
    uint16_t *row = fb + static_cast<size_t>(py) * SCREEN_W;
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

}  // namespace

void fill_round_rect_glass_impl(Rgb565Surface &it, int x0, int y0, int w, int h, float rad,
                                UiColor tint, int a_top, int a_bot, int hole) {
  if (w <= 0 || h <= 0) return;
  if (y0 + h <= clip_y0() || y0 > clip_y1()) return;
  uint16_t *fb = active_frame_buf16();
  if (fb == nullptr) {
    fill_round_rect_aa_impl(it, x0, y0, w, h, rad, tint, tint, hole);
    return;
  }
  const int mode = active_frame_endian_mode();
  if (mode == 1 || mode < 0) {
    fill_round_rect_glass_mode<1>(fb, x0, y0, w, h, rad, tint, a_top, a_bot, hole);
  } else if (mode == 2) {
    fill_round_rect_glass_mode<2>(fb, x0, y0, w, h, rad, tint, a_top, a_bot, hole);
  } else if (mode == 4) {
    fill_round_rect_glass_mode<4>(fb, x0, y0, w, h, rad, tint, a_top, a_bot, hole);
  } else {
    fill_round_rect_glass_mode<3>(fb, x0, y0, w, h, rad, tint, a_top, a_bot, hole);
  }
}

// ------------------------------------------------------------------------------
// 2. Dirty-Band Aware Frame Base State
// ------------------------------------------------------------------------------
FrameBaseState &frame_base_state() {
  static FrameBaseState st;
  return st;
}

void frame_base_begin() {
  frame_base_state().painted = false;
  frame_base_state().has_color = false;
  backdrop_active() = false;
  set_clip_y_band(dirty_band().band_y0(), dirty_band().band_y1());
}

void frame_base_skip() { frame_base_state().painted = true; }
void frame_base_claim() { frame_base_state().painted = true; }

bool frame_base_impl(Rgb565Surface &it, UiColor c) {
  auto &st = frame_base_state();
  if (st.painted) return false;
  st.painted = true;
  const uint16_t hw = encode_hw_rgb565(c);
  st.color_hw = hw;
  const bool full = dirty_band().is_full();
  st.has_color = full;
  if (fb_clear_consume(hw)) return true;

  const uint64_t t0 = profiler_enabled() ? prof_now_us() : 0;
  if (full) {
    fill_rect_row_major(it, 0, 0, SCREEN_W, SCREEN_H, c);
  } else {
    const int y0 = std::max(0, dirty_band().y0());
    const int y1 = std::min(SCREEN_H - 1, dirty_band().y1());
    if (y1 >= y0) {
      fill_rect_row_major(it, 0, y0, SCREEN_W, y1 - y0 + 1, c);
    }
  }
  if (t0 != 0) prof_sect_us()[static_cast<size_t>(ProfSect::BASE)] += static_cast<uint32_t>(prof_now_us() - t0);
  return true;
}

}  // namespace latter_ui
