#include "ui_gfx.h"
#include "ui_dirty_band.h"

#include <algorithm>
#include <cstring>

namespace latter_ui {

// ------------------------------------------------------------------------------
// Album-Art / Wallpaper Composition & Retryable PSRAM ArtCanvasCache
// ------------------------------------------------------------------------------
int compute_art_row_scale_q8(int y, bool show_controls, bool show_metadata, int dim_level) {
  auto ramp = [](int yy, int y_a, int v_a, int y_b, int v_b) -> int {
    if (y_b <= y_a || yy <= y_a) return v_a;
    if (yy >= y_b) return v_b;
    return v_a + ((v_b - v_a) * (yy - y_a)) / (y_b - y_a);
  };
  if (dim_level == kArtDimLyrics) {
    const int top_y0 = scale_y(52);
    const int top_y1 = scale_y(104);
    const int bot_y0 = SCREEN_H - scale_y(104);
    const int bot_y1 = SCREEN_H - scale_y(52);
    if (y < top_y1) return ramp(y, top_y0, 108, top_y1, 132);
    if (y > bot_y0) return ramp(y, bot_y0, 132, bot_y1, 108);
    return 132;
  }
  if (dim_level >= kArtDimMenu) return 64;
  if (show_controls) return 140;
  if (show_metadata) return ramp(y, scale_y(104), 140, scale_y(148), 256);
  return 256;
}

void art_letterbox_bounds(bool show_metadata, bool show_controls, int &out_y0, int &out_y1) {
  if (show_metadata || show_controls) {
    out_y0 = scale_y(132);
    const int bot_pad = LATTER_ROUND_DISPLAY ? scale_y(66) : scale_y(54);
    out_y1 = std::max(out_y0 + scale_y(120), SCREEN_H - bot_pad);
  } else {
    const int half_h = (SCREEN_H * 31) / 100;
    out_y0 = std::max(0, SCREEN_CY - half_h);
    out_y1 = std::min(SCREEN_H, SCREEN_CY + half_h);
  }
}

namespace {

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
                                 const uint16_t *src_row, int mul_q8, uint16_t *fallback_row_buf) {
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
  if (mul_q8 >= 256) {
    blit_rgb565_scanline(it, start_x, y, span_w, src_row, false);
    return;
  }
  uint16_t *fb = active_frame_buf16();
  if (fb != nullptr) {
    if (mul_q8 < 0) mul_q8 = 0;
    uint16_t *dst = fb + static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W) + static_cast<size_t>(start_x);
    const uint8_t *th = kBayer4[y & 3];
    const uint32_t m = static_cast<uint32_t>(mul_q8);
    const int mode = active_frame_endian_mode();
    if (mode == 1 || mode < 0) {
      for (int i = 0; i < span_w; i++) {
        const uint32_t p = src_row[i];
        const uint32_t t = static_cast<uint32_t>(th[(start_x + i) & 3]) * 16U + 8U;
        const uint32_t r = (((p >> 11) & 0x1FU) * m + t) >> 8;
        const uint32_t g = (((p >> 5) & 0x3FU) * m + t) >> 8;
        const uint32_t b = ((p & 0x1FU) * m + t) >> 8;
        dst[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
      }
      return;
    }
    if (mode == 2) {
      for (int i = 0; i < span_w; i++) {
        const uint32_t p = src_row[i];
        const uint32_t t = static_cast<uint32_t>(th[(start_x + i) & 3]) * 16U + 8U;
        const uint32_t r = (((p >> 11) & 0x1FU) * m + t) >> 8;
        const uint32_t g = (((p >> 5) & 0x3FU) * m + t) >> 8;
        const uint32_t b = ((p & 0x1FU) * m + t) >> 8;
        dst[i] = bswap16_u16(static_cast<uint16_t>((r << 11) | (g << 5) | b));
      }
      return;
    }
    for (int i = 0; i < span_w; i++) {
      const uint32_t p = src_row[i];
      const uint32_t t = static_cast<uint32_t>(th[(start_x + i) & 3]) * 16U + 8U;
      const uint32_t r = (((p >> 11) & 0x1FU) * m + t) >> 8;
      const uint32_t g = (((p >> 5) & 0x3FU) * m + t) >> 8;
      const uint32_t b = ((p & 0x1FU) * m + t) >> 8;
      dst[i] = encode_hw_rgb565_raw(static_cast<uint16_t>((r << 11) | (g << 5) | b));
    }
    return;
  }
  std::memcpy(fallback_row_buf, src_row, static_cast<size_t>(span_w) * sizeof(uint16_t));
  dim_rgb565_row_dither(fallback_row_buf, span_w, mul_q8, start_x, y);
  blit_rgb565_scanline(it, start_x, y, span_w, fallback_row_buf, false);
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

struct ArtScaleScratch {
  uint16_t row_buf[SCREEN_W];
  uint16_t sx_lut[SCREEN_W];
  uint16_t scaled_row[SCREEN_W];
};

// Retryable allocation — never permanently latches nullptr on transient OOM.
ArtScaleScratch *art_scale_scratch_ptr() {
  static ArtScaleScratch *s_ptr = nullptr;
  if (s_ptr == nullptr) {
    s_ptr = static_cast<ArtScaleScratch *>(ui_psram_calloc(1, sizeof(ArtScaleScratch)));
  }
  return s_ptr;
}

void draw_album_art(Rgb565Surface &it, const uint16_t *src_data, int src_w, int src_h, int cx,
                    bool is_standard, bool is_letterbox, bool apply_scrim, bool show_metadata, int dim_level) {
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
      dim_rgb565_row_dither(row_buf, span_w, mul_q8, r.x0 + x_start, py);
      blit_rgb565_scanline(it, r.x0 + x_start, py, span_w, row_buf, false);
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
      blit_dimmed_rgb565_scanline(it, start_x, y, span_w, src_row + start_x, mul_q8, row_buf);
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
      }
      blit_dimmed_rgb565_scanline(it, start_x, y, span_w, scaled_row + start_x, mul_q8, row_buf + start_x);
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
  if constexpr (kArtCacheEntries <= 0) {
    return nullptr;
  }
  if (g_art_canvas_entries == nullptr) {
    g_art_canvas_entries = static_cast<ArtCanvasCache *>(
        ui_psram_calloc(static_cast<size_t>(kArtCacheEntries), sizeof(ArtCanvasCache)));
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
    for (int y = 0; y < SCREEN_H; y++) {
      e.row_x0[y] = 0;
      e.row_len[y] = static_cast<int16_t>(SCREEN_W);
    }
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
    const int meta_bottom = scale_y(128);
    const int bot_pad = LATTER_ROUND_DISPLAY ? scale_y(62) : scale_y(38);
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
  for (int i = 0; i < kArtCacheEntries; i++) {
    ArtCanvasCache &e = g_art_canvas_entries[i];
    if (e.data != data) continue;
    ui_psram_free(e.px);
    e.px = nullptr;
    e.data = nullptr;
    e.last_use = 0;
  }
}

void art_canvas_drop_all() noexcept {
  if (g_art_canvas_entries == nullptr) return;
  for (int i = 0; i < kArtCacheEntries; i++) {
    ArtCanvasCache &e = g_art_canvas_entries[i];
    if (e.px != nullptr) {
      ui_psram_free(e.px);
      e.px = nullptr;
    }
    e.data = nullptr;
    e.last_use = 0;
  }
}

UiColor sample_image_ambient_tint_view(ArtImageView img, UiColor base_bg) {
  if (img.data == nullptr || img.width < 16 || img.height < 16) return base_bg;
  uint32_t sum_r = 0, sum_g = 0, sum_b = 0;
  for (int gy = 1; gy <= 4; gy++) {
    const int sy = (img.height * gy) / 5;
    const uint16_t *row = img.data + static_cast<size_t>(sy) * static_cast<size_t>(img.width);
    for (int gx = 1; gx <= 4; gx++) {
      const uint16_t p = row[(img.width * gx) / 5];
      sum_r += (p & 0xF800U) >> 8;
      sum_g += (p & 0x07E0U) >> 3;
      sum_b += (p & 0x001FU) << 3;
    }
  }
  const uint8_t avg_r = static_cast<uint8_t>(sum_r >> 4);
  const uint8_t avg_g = static_cast<uint8_t>(sum_g >> 4);
  const uint8_t avg_b = static_cast<uint8_t>(sum_b >> 4);
  return UiColor{
      static_cast<uint8_t>(std::min(68, static_cast<int>(base_bg.r) + (avg_r >> 3))),
      static_cast<uint8_t>(std::min(74, static_cast<int>(base_bg.g) + (avg_g >> 3))),
      static_cast<uint8_t>(std::min(88, static_cast<int>(base_bg.b) + (avg_b >> 3))),
  };
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
  for (int i = 0; i < kArtCacheEntries; i++) {
    ArtCanvasCache &e = entries[i];
    if (e.px != nullptr && e.data == src_data && e.w == src_w && e.h == src_h &&
        e.sample == sample && e.mode == mode) {
      hit = &e;
      break;
    }
    if (e.last_use < victim->last_use) victim = &e;
  }

  if (hit == nullptr) {
    const bool full_rect_overwrite = full_bleed && (LATTER_ROUND_DISPLAY == 0);
    bool freshly_zeroed = false;
    if (victim->px == nullptr) {
#if !LATTER_CHIP_P4
      sky_base_cache_drop();
#endif
      const size_t px_count = static_cast<size_t>(SCREEN_W) * static_cast<size_t>(SCREEN_H);
      victim->px = static_cast<uint16_t *>(
          full_rect_overwrite
              ? ui_psram_malloc(px_count * sizeof(uint16_t))
              : ui_psram_calloc(px_count, sizeof(uint16_t)));
      freshly_zeroed = (!full_rect_overwrite && victim->px != nullptr);
    }
    if (victim->px == nullptr) {
      if (full_bleed) fill_rect_row_major(it, 0, 0, SCREEN_W, SCREEN_H, kChromeShadow);
      draw_album_art(it, src_data, src_w, src_h, cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
      backdrop_active() = true;
      return true;
    }
    if (!freshly_zeroed && full_bleed && !full_rect_overwrite) {
      std::memset(victim->px, 0, static_cast<size_t>(SCREEN_W) * SCREEN_H * sizeof(uint16_t));
    }
    const int saved_cy0 = clip_y0();
    const int saved_cy1 = clip_y1();
    reset_clip_y_band();
    active_frame_buf16() = victim->px;
    draw_album_art(it, src_data, src_w, src_h, cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
    active_frame_buf16() = fb;
    set_clip_y_band(saved_cy0, saved_cy1);
    art_canvas_spans(*victim, cx, src_w, src_h, is_standard, is_letterbox, apply_scrim, show_metadata);
    victim->data = src_data;
    victim->w = static_cast<int16_t>(src_w);
    victim->h = static_cast<int16_t>(src_h);
    victim->sample = sample;
    victim->mode = mode;
    hit = victim;
  }
  hit->last_use = use_seq;

  const int raw_lo = dirty_band().is_full() ? 0 : std::max(0, dirty_band().y0());
  const int raw_hi = dirty_band().is_full() ? (SCREEN_H - 1) : std::min(SCREEN_H - 1, dirty_band().y1());
  const int y_lo = std::max(clip_y0(), raw_lo);
  const int y_hi = std::min(clip_y1(), raw_hi);
  if (full_bleed && y_hi >= y_lo) {
    const size_t off = static_cast<size_t>(y_lo) * static_cast<size_t>(SCREEN_W);
    const size_t count = static_cast<size_t>(y_hi - y_lo + 1) * static_cast<size_t>(SCREEN_W);
    std::memcpy(fb + off, hit->px + off, count * sizeof(uint16_t));
  } else {
    for (int y = y_lo; y <= y_hi; y++) {
      const int len = hit->row_len[y];
      if (len <= 0) continue;
      const size_t off = static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W) + static_cast<size_t>(hit->row_x0[y]);
      std::memcpy(fb + off, hit->px + off, static_cast<size_t>(len) * sizeof(uint16_t));
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
