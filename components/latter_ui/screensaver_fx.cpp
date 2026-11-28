#include "ui_pages.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace latter_ui {
using namespace latter_core;

namespace {

// Reciprocal LUT for Aurora overlapping-ribbon normalization (w_sum in [5..768]):
// stores round(2^18 / s) in uint16_t (max 52429 at s == 5), living in .rodata Flash.
static constexpr std::array<uint16_t, 769> kAuroraRecipQ18 = []() constexpr {
  std::array<uint16_t, 769> lut{};
  for (uint32_t s = 5U; s <= 768U; ++s) {
    lut[s] = static_cast<uint16_t>(((1U << 18) + (s >> 1)) / s);
  }
  return lut;
}();

// 64-entry vertical plume intensity LUT in .rodata Flash (0..256 fixed-point scale):
// - Indices 0..7   (bottom 12%): soft lower bloom (115 -> 254)
// - Indices 8..39  (core 50%):   full-bodied luminous ribbon plateau (256 -> 190)
// - Indices 40..63 (upper 38%):  hazy atmospheric tail fade into night sky (182 -> 4)
static constexpr uint16_t AURORA_PLUME_LUT[64] = {
    115, 138, 162, 186, 208, 228, 244, 254,
    256, 256, 256, 255, 254, 252, 250, 248,
    246, 243, 240, 237, 234, 231, 228, 225,
    222, 219, 216, 213, 210, 207, 204, 202,
    200, 198, 196, 194, 192, 190, 186, 182,
    176, 170, 162, 154, 145, 136, 126, 116,
    106,  96,  86,  76,  66,  57,  48,  40,
     33,  26,  20,  15,  11,   8,   6,   4
};

struct AuroraCol {
  int16_t x;
  int16_t col_top, col_bot;
  int16_t y1, top1, step1_fp;
  int16_t y2, top2, step2_fp;
  int16_t y3, top3, step3_fp;
};

struct Star3D {
  float x, y, z;
};

struct ScreensaverFxBuffers {
  AuroraCol *aurora_cols = nullptr;
  Star3D *stars = nullptr;
  bool stars_init = false;
};

ScreensaverFxBuffers &screensaver_fx_buffers() noexcept {
  static ScreensaverFxBuffers bufs{};
  return bufs;
}

void screensaver_fx_cache_drop() noexcept {
  auto &bufs = screensaver_fx_buffers();
  if (bufs.aurora_cols != nullptr) {
    ui_psram_free(bufs.aurora_cols);
    bufs.aurora_cols = nullptr;
  }
  if (bufs.stars != nullptr) {
    ui_psram_free(bufs.stars);
    bufs.stars = nullptr;
  }
  bufs.stars_init = false;
}

struct ScreensaverPsramEvictorRegistrar {
  ScreensaverPsramEvictorRegistrar() noexcept {
    register_psram_cache_evictor(&screensaver_fx_cache_drop, true);
  }
};
const ScreensaverPsramEvictorRegistrar kScreensaverPsramEvictorRegistrar{};

void render_screensaver_aurora(Rgb565Surface &it, int cy, uint32_t now_ms) {
  const float t = static_cast<float>(now_ms) * 0.0010f;

  const float b1 = 0.92f + 0.10f * fast_sin(t * 0.9f);
  const int p1_r = static_cast<int>(192.0f * b1), p1_g = static_cast<int>(24.0f * b1), p1_b = static_cast<int>(205.0f * b1);
  const float b2 = 0.92f + 0.10f * fast_cos(t * 1.1f + 1.2f);
  const int p2_r = static_cast<int>(12.0f * b2), p2_g = static_cast<int>(228.0f * b2), p2_b = static_cast<int>(242.0f * b2);
  const float b3 = 0.92f + 0.10f * fast_sin(t * 0.8f + 2.5f);
  const int p3_r = static_cast<int>(20.0f * b3), p3_g = static_cast<int>(235.0f * b3), p3_b = static_cast<int>(82.0f * b3);

  static constexpr int X_STEP = (SCREEN_W >= 640) ? 4 : 3;
  static constexpr int FILL_W = X_STEP;
  static constexpr int MAX_COLS = (SCREEN_W / X_STEP) + 2;

  // Retryable PSRAM-backed scratch: keeps ~4 KB off ESPHome's 8 KB loopTask stack.
  auto &bufs = screensaver_fx_buffers();
  if (bufs.aurora_cols == nullptr) {
    bufs.aurora_cols = static_cast<AuroraCol *>(ui_psram_calloc(static_cast<size_t>(MAX_COLS), sizeof(AuroraCol)));
    if (bufs.aurora_cols == nullptr) return;
  }
  AuroraCol *const cols = bufs.aurora_cols;
  int num_cols = 0;
  int global_top = SCREEN_H;
  int global_bot = 0;

  const float h_sc = static_cast<float>(SCREEN_H) / 466.0f;
  const float x_sc = 466.0f / static_cast<float>(SCREEN_W);

  for (int x = 6; x <= SCREEN_W - 6 && num_cols < MAX_COLS; x += X_STEP) {
#if LATTER_ROUND_DISPLAY
    const int h_half = circle_half_span(x, 5);
    if (h_half <= 0) continue;
    const int y_min = cy - h_half;
    const int y_max = cy + h_half;
#else
    const int y_min = 6;
    const int y_max = SCREEN_H - 6;
#endif
    const float xf = static_cast<float>(x) * x_sc;

    const int y1 = cy - static_cast<int>(10.0f * h_sc) +
                   static_cast<int>((fast_sin(xf * 0.011f + t * 0.6f) * 58.0f + fast_cos(xf * 0.006f - t * 0.35f) * 28.0f) * h_sc);
    const int len1 = static_cast<int>((120.0f + fast_sin(xf * 0.024f + t * 1.2f) * 35.0f) * h_sc);
    const int top1 = y1 - len1;
    const int step1_fp = (63 << 10) / (len1 > 1 ? len1 : 1);

    const int y2 = cy + static_cast<int>(32.0f * h_sc) +
                   static_cast<int>((fast_sin(xf * 0.014f - t * 0.8f + 1.6f) * 62.0f + fast_sin(xf * 0.008f + t * 0.5f) * 32.0f) * h_sc);
    const int len2 = static_cast<int>((135.0f + fast_cos(xf * 0.028f - t * 1.5f) * 40.0f) * h_sc);
    const int top2 = y2 - len2;
    const int step2_fp = (63 << 10) / (len2 > 1 ? len2 : 1);

    const int y3 = cy + static_cast<int>(78.0f * h_sc) +
                   static_cast<int>((fast_sin(xf * 0.017f + t * 1.0f + 3.1f) * 66.0f + fast_cos(xf * 0.010f - t * 0.7f) * 36.0f) * h_sc);
    const int len3 = static_cast<int>((150.0f + fast_sin(xf * 0.032f + t * 1.8f) * 44.0f) * h_sc);
    const int top3 = y3 - len3;
    const int step3_fp = (63 << 10) / (len3 > 1 ? len3 : 1);

    int c_top = std::min({top1, top2, top3});
    if (c_top < y_min) c_top = y_min;
    int c_bot = std::max({y1, y2, y3});
    if (c_bot > y_max) c_bot = y_max;
    if (c_top > c_bot) continue;

    if (c_top < global_top) global_top = c_top;
    if (c_bot > global_bot) global_bot = c_bot;

    cols[num_cols++] = {
        static_cast<int16_t>(x),
        static_cast<int16_t>(c_top), static_cast<int16_t>(c_bot),
        static_cast<int16_t>(y1), static_cast<int16_t>(top1), static_cast<int16_t>(step1_fp),
        static_cast<int16_t>(y2), static_cast<int16_t>(top2), static_cast<int16_t>(step2_fp),
        static_cast<int16_t>(y3), static_cast<int16_t>(top3), static_cast<int16_t>(step3_fp)};
  }

  const int cy0 = clip_y0();
  const int cy1 = clip_y1();
  if (global_top > global_bot || global_bot < cy0 || global_top > cy1) return;

  int y_start = global_top;
  if (y_start + 1 < cy0) {
    y_start += ((cy0 - y_start) & ~1);
  }
  const int y_end = std::min(global_bot, cy1);
  uint16_t *const fb = active_frame_buf16();

  auto write_span = [](uint16_t *dst, uint16_t px, uint32_t pair) noexcept {
    if constexpr (FILL_W == 4) {
      if ((reinterpret_cast<uintptr_t>(dst) & 2U) == 0U) {
        auto *d32 = reinterpret_cast<uint32_t *>(dst);
        d32[0] = pair;
        d32[1] = pair;
      } else {
        dst[0] = px;
        *reinterpret_cast<uint32_t *>(dst + 1) = pair;
        dst[3] = px;
      }
    } else {
      for (int k = 0; k < FILL_W; ++k) dst[k] = px;
    }
  };

  for (int py = y_start; py <= y_end; py += 2) {
    const bool draw_r0 = (py >= cy0 && py <= cy1);
    const bool draw_r1_band = (py + 1 >= cy0 && py + 1 <= cy1);
    uint16_t *const row0 = (fb != nullptr && draw_r0) ? (fb + static_cast<size_t>(py) * SCREEN_W) : nullptr;
    uint16_t *const row1 = (fb != nullptr && draw_r1_band) ? (fb + static_cast<size_t>(py + 1) * SCREEN_W) : nullptr;

    for (int i = 0; i < num_cols; i++) {
      const auto &c = cols[i];
      if (py < c.col_top || py > c.col_bot) continue;

      int r = 0, g = 0, b = 0;
      uint32_t w_sum = 0;
      uint32_t w_max = 0;

      if (py <= c.y1 && py >= c.top1) {
        const int idx1 = ((c.y1 - py) * c.step1_fp) >> 10;
        const uint32_t w1 = AURORA_PLUME_LUT[idx1 & 63];
        r += (p1_r * static_cast<int>(w1)) >> 8;
        g += (p1_g * static_cast<int>(w1)) >> 8;
        b += (p1_b * static_cast<int>(w1)) >> 8;
        w_sum += w1;
        w_max = w1;
      }
      if (py <= c.y2 && py >= c.top2) {
        const int idx2 = ((c.y2 - py) * c.step2_fp) >> 10;
        const uint32_t w2 = AURORA_PLUME_LUT[idx2 & 63];
        r += (p2_r * static_cast<int>(w2)) >> 8;
        g += (p2_g * static_cast<int>(w2)) >> 8;
        b += (p2_b * static_cast<int>(w2)) >> 8;
        w_sum += w2;
        if (w2 > w_max) w_max = w2;
      }
      if (py <= c.y3 && py >= c.top3) {
        const int idx3 = ((c.y3 - py) * c.step3_fp) >> 10;
        const uint32_t w3 = AURORA_PLUME_LUT[idx3 & 63];
        r += (p3_r * static_cast<int>(w3)) >> 8;
        g += (p3_g * static_cast<int>(w3)) >> 8;
        b += (p3_b * static_cast<int>(w3)) >> 8;
        w_sum += w3;
        if (w3 > w_max) w_max = w3;
      }

      if (w_sum > w_max) {
        const uint32_t w_target = w_max + ((w_sum - w_max) >> 2);
        const uint32_t scale_q18 = w_target * static_cast<uint32_t>(kAuroraRecipQ18[w_sum]);
        r = static_cast<int>((static_cast<uint32_t>(r) * scale_q18) >> 18);
        g = static_cast<int>((static_cast<uint32_t>(g) * scale_q18) >> 18);
        b = static_cast<int>((static_cast<uint32_t>(b) * scale_q18) >> 18);
      }

      if ((r | g | b) > 5) {
        const UiColor veil_col(static_cast<uint8_t>(r > 255 ? 255 : r),
                               static_cast<uint8_t>(g > 255 ? 255 : g),
                               static_cast<uint8_t>(b > 255 ? 255 : b));
        if (fb != nullptr) {
          const uint16_t px = encode_hw_rgb565(veil_col);
          const uint32_t pair = static_cast<uint32_t>(px) | (static_cast<uint32_t>(px) << 16);
          if (row0 != nullptr) write_span(row0 + c.x, px, pair);
          if (row1 != nullptr && py + 1 <= c.col_bot) write_span(row1 + c.x, px, pair);
        } else {
          if (draw_r0) fast_hline(it, c.x, py, FILL_W, veil_col);
          if (draw_r1_band && py + 1 <= c.col_bot) fast_hline(it, c.x, py + 1, FILL_W, veil_col);
        }
      }
    }
  }
}

void render_screensaver_starfield(Rgb565Surface &it, int cx, int cy, uint32_t now_ms) {
  static constexpr int kStarCount = 90;
  auto &bufs = screensaver_fx_buffers();
  if (bufs.stars == nullptr) {
    bufs.stars = static_cast<Star3D *>(ui_psram_calloc(static_cast<size_t>(kStarCount), sizeof(Star3D)));
    if (bufs.stars == nullptr) return;
    bufs.stars_init = false;
  }
  Star3D *const stars = bufs.stars;
  static uint32_t star_seed = 123456789U;
  static uint32_t last_star_ms = 0U;

  auto star_rand = [&]() -> float {
    star_seed = star_seed * 1664525U + 1013904223U;
    return static_cast<float>(star_seed & 0xFFFFU) * (1.0f / 65535.0f);
  };
  auto respawn_star = [&](int i, bool init_z) {
    const float ang = star_rand() * (2.0f * PI_F);
    const float dist = 95.0f + star_rand() * 600.0f;
    stars[i].x = fast_cos(ang) * dist;
    stars[i].y = fast_sin(ang) * dist;
    stars[i].z = init_z ? (30.0f + star_rand() * 950.0f) : (850.0f + star_rand() * 150.0f);
  };
  if (!bufs.stars_init) {
    for (int i = 0; i < kStarCount; i++) respawn_star(i, true);
    bufs.stars_init = true;
    last_star_ms = now_ms;
  }

  float speed = 14.0f;
  if (now_ms > 0U) {
    const uint32_t dt = now_ms - last_star_ms;
    last_star_ms = now_ms;
    if (dt >= 20U && dt <= 300U) {
      speed = static_cast<float>(dt) * 0.28f;
    }
  }

  for (int i = 0; i < kStarCount; i++) {
    stars[i].z -= speed;
    if (stars[i].z <= 25.0f) {
      respawn_star(i, false);
      continue;
    }
    const float k = 230.0f / stars[i].z;
    const int sx = cx + static_cast<int>(stars[i].x * k);
    const int sy = cy + static_cast<int>(stars[i].y * k);
    if (!is_inside_bezel(sx, sy, 6)) {
      respawn_star(i, false);
      continue;
    }
    const float prev_k = 230.0f / (stars[i].z + speed * 1.65f);
    const int px = cx + static_cast<int>(stars[i].x * prev_k);
    const int py = cy + static_cast<int>(stars[i].y * prev_k);
    const bool is_blue = ((i % 5) == 0);
    if (stars[i].z > 650.0f) {
      fast_pixel(it, sx, sy, UiColor(148, 154, 166));
    } else if (stars[i].z > 350.0f) {
      fast_line(it, px, py, sx, sy, is_blue ? UiColor(135, 205, 250) : UiColor(195, 202, 212));
      fast_pixel(it, sx, sy, UiColor(245, 250, 255));
    } else {
      fast_line(it, px, py, sx, sy, is_blue ? UiColor(95, 210, 255) : UiColor(222, 228, 238));
      fast_filled_circle(it, sx, sy, 1, UiColor(255, 255, 255));
    }
  }
}

static constexpr uint16_t DIGITAL_RAIN_GLYPH_MASK[32] = {
    0x121, 0x181, 0x069, 0x085, 0x183, 0x06B, 0x083, 0x129,
    0x0A9, 0x065, 0x0A3, 0x105, 0x214, 0x12B, 0x182, 0x007,
    0x301, 0x381, 0x108, 0x056, 0x141, 0x187, 0x223, 0x207,
    0x114, 0x300, 0x097, 0x067, 0x107, 0x028, 0x07D, 0x204
};

void draw_rain_katakana_glyph(Rgb565Surface &it, int sx, int gy, uint16_t mask,
                              UiColor col, bool slim_tail) {
  if (slim_tail) {
    if (mask & 0x080) fast_line(it, sx, gy - 3, sx, gy + 3, col);
    else if (mask & 0x100) fast_line(it, sx + 2, gy - 2, sx - 2, gy + 3, col);
    else fast_line(it, sx - 2, gy - 3, sx + 2, gy - 3, col);
    if (mask & 0x002) fast_line(it, sx - 2, gy, sx + 2, gy, col);
    return;
  }
  if (mask & 0x001) fast_line(it, sx - 3, gy - 4, sx + 3, gy - 4, col);
  if (mask & 0x002) fast_line(it, sx - 3, gy, sx + 3, gy, col);
  if (mask & 0x004) fast_line(it, sx - 3, gy + 4, sx + 3, gy + 4, col);
  if (mask & 0x008) fast_line(it, sx - 3, gy - 4, sx - 3, gy, col);
  if (mask & 0x010) fast_line(it, sx - 3, gy, sx - 3, gy + 4, col);
  if (mask & 0x020) fast_line(it, sx + 3, gy - 4, sx + 3, gy, col);
  if (mask & 0x040) fast_line(it, sx + 3, gy, sx + 3, gy + 4, col);
  if (mask & 0x080) fast_line(it, sx, gy - 4, sx, gy + 4, col);
  if (mask & 0x100) fast_line(it, sx + 3, gy - 2, sx - 3, gy + 4, col);
  if (mask & 0x200) fast_line(it, sx - 2, gy - 2, sx + 2, gy + 2, col);
}

void render_screensaver_digital_rain(Rgb565Surface &it, int cy, uint32_t now_ms) {
#if !LATTER_ROUND_DISPLAY
  (void)cy;
#endif
  static constexpr int CELL_H = 14;
  static constexpr int NUM_COLS = (SCREEN_W - 24) / 19;
  static constexpr int GRID_ROWS = (SCREEN_H / CELL_H) + 1;
  static constexpr int X_MARGIN = (SCREEN_W - (NUM_COLS - 1) * 19) / 2;

  const float t_drift = static_cast<float>(now_ms) * 0.00007f;
  const int drift_x = static_cast<int>(fast_sin(t_drift) * 4.2f);
  const int drift_y = static_cast<int>(fast_cos(t_drift * 1.3f) * 3.2f);
  const float t_sec = static_cast<float>(now_ms) * 0.001f;

  for (int i = 0; i < NUM_COLS; i++) {
    const int sx = X_MARGIN + i * 19 + drift_x;
#if LATTER_ROUND_DISPLAY
    const int max_dy = circle_half_span(sx, 7);
    if (max_dy <= 0) continue;
    const int y_min = cy - max_dy;
    const int y_max = cy + max_dy;
#else
    if (sx < 7 || sx >= SCREEN_W - 7) continue;
    const int y_min = 7;
    const int y_max = SCREEN_H - 8;
#endif

    const int clip_min_row = (y_min - drift_y) / CELL_H;
    const int clip_max_row = (y_max - drift_y) / CELL_H;
    if (clip_max_row - clip_min_row < 2) continue;

    const int perm_i = (i * 7 + 3) % NUM_COLS;
    const float col_h1 = hash2d_01(perm_i * 41 + 13, i * 19 + 5);
    const float col_h2 = hash2d_01(perm_i * 59 + 29, i * 31 + 11);

    const float rows_per_sec = 5.2f + col_h1 * 6.8f;
    const float cycle_rows_f = static_cast<float>(GRID_ROWS) + 22.0f + col_h2 * 14.0f;
    const float init_phase_frac = std::fmod(static_cast<float>(i) * 0.381966011f + static_cast<float>(perm_i) * 0.145898f, 1.0f);
    const float total_travel = t_sec * rows_per_sec + init_phase_frac * cycle_rows_f;

    const uint32_t pass_idx = static_cast<uint32_t>(total_travel / cycle_rows_f);
    const float local_pos = total_travel - static_cast<float>(pass_idx) * cycle_rows_f;

    const int pass_seed = static_cast<int>(pass_idx * 47U + static_cast<uint32_t>(i) * 113U);
    const float r_len = hash2d_01(pass_seed, 13);
    const float r_tier = hash2d_01(pass_seed, 29);
    const float r_spawn = hash2d_01(pass_seed, 47);
    const float r_end = hash2d_01(pass_seed, 61);

    int slen;
    if (r_len > 0.65f) slen = 18 + static_cast<int>((r_len - 0.65f) * 28.0f);
    else if (r_len > 0.20f) slen = 11 + static_cast<int>((r_len - 0.20f) * 15.0f);
    else slen = 6 + static_cast<int>(r_len * 24.0f);
    const float slen_f = static_cast<float>(slen);
    const float inv_tail_span = 1.0f / (slen_f > 2.4f ? (slen_f - 2.2f) : 1.0f);

    const int spawn_row = (r_spawn < 0.40f)
                              ? (4 + static_cast<int>(r_spawn * 28.0f))
                              : (-2 + static_cast<int>((r_spawn - 0.40f) * 5.0f));
    const int dissolve_row = (r_end < 0.25f)
                                 ? (20 + static_cast<int>(r_end * 34.0f))
                                 : (GRID_ROWS + 4);

    const bool is_bg_stream = (r_tier < 0.28f);
    const float bright_scale = is_bg_stream ? 0.52f : 1.0f;

    const float head_row_f = static_cast<float>(spawn_row) + local_pos;
    const int head_row_top = static_cast<int>(std::floor(head_row_f));

    if (!is_bg_stream && head_row_top <= dissolve_row) {
      const int exact_hy = static_cast<int>(head_row_f * static_cast<float>(CELL_H)) + drift_y;
      if (exact_hy >= y_min && exact_hy <= y_max && head_row_f >= static_cast<float>(spawn_row)) {
        fill_rect_row_major(it, sx - 4, exact_hy - 5, 9, 11, UiColor(0, 62, 18));
      }
    }

    for (int j = 0; j <= slen; j++) {
      const int row = head_row_top - j;
      if (row < spawn_row || row > dissolve_row) continue;
      if (row < clip_min_row || row > clip_max_row) continue;
      const int gy = row * CELL_H + drift_y;
      if (gy < y_min || gy > y_max) continue;

      const float dist = head_row_f - static_cast<float>(row);
      if (dist < 0.0f || dist > slen_f) continue;

      float dissolve_fade = 1.0f;
      if (head_row_f > static_cast<float>(dissolve_row)) {
        const float over = head_row_f - static_cast<float>(dissolve_row);
        dissolve_fade = std::fmax(0.0f, 1.0f - over * (1.0f / (slen_f * 0.75f)));
        if (dissolve_fade <= 0.05f) continue;
      }

      const float eff_scale = bright_scale * dissolve_fade;
      const uint32_t cell_hash = (static_cast<uint32_t>(i) * 73856093U) ^
                                 (static_cast<uint32_t>(row) * 19349663U) ^
                                 (pass_idx * 83492791U);
      const uint32_t glitch_tick = (dist < 1.1f)
                                       ? (now_ms / 85U)
                                       : (((cell_hash & 7U) == 0U) ? (now_ms / (190U + (cell_hash & 255U))) : 0U);
      const uint16_t mask = DIGITAL_RAIN_GLYPH_MASK[(cell_hash + glitch_tick * 19U) & 31U];

      UiColor g_col;
      if (dist < 0.95f && head_row_top <= dissolve_row) {
        const float lead_env = ((dist < 0.30f) ? (0.45f + (dist * (1.0f / 0.30f)) * 0.55f) : 1.0f) * dissolve_fade;
        if (!is_bg_stream) {
          g_col = UiColor(static_cast<uint8_t>(225.0f * lead_env),
                          static_cast<uint8_t>(255.0f * lead_env),
                          static_cast<uint8_t>(235.0f * lead_env));
        } else {
          g_col = UiColor(static_cast<uint8_t>(110.0f * lead_env),
                          static_cast<uint8_t>(225.0f * lead_env),
                          static_cast<uint8_t>(138.0f * lead_env));
        }
      } else if (dist < 2.2f) {
        float u = (dist - 0.95f) * (1.0f / 1.25f);
        if (u < 0.0f) u = 0.0f;
        const float r_c = (115.0f * (1.0f - u) + 24.0f * u) * eff_scale;
        const float g_c = (255.0f * (1.0f - u) + 225.0f * u) * eff_scale;
        const float b_c = (140.0f * (1.0f - u) + 58.0f * u) * eff_scale;
        g_col = UiColor(static_cast<uint8_t>(r_c), static_cast<uint8_t>(g_c), static_cast<uint8_t>(b_c));
      } else {
        float tail_u = (dist - 2.2f) * inv_tail_span;
        if (tail_u > 1.0f) tail_u = 1.0f;
        const float inv_u = 1.0f - tail_u;
        const float fade = (14.0f + 200.0f * inv_u * (0.60f + 0.40f * inv_u)) * eff_scale;
        const bool glitch_flash = (dist < 6.0f) && ((cell_hash & 7U) == 0U) &&
                                  (((now_ms / 160U) + static_cast<uint32_t>(row)) % 6U == 0U);
        if (glitch_flash) {
          g_col = UiColor(static_cast<uint8_t>(90.0f * eff_scale),
                          static_cast<uint8_t>(255.0f * eff_scale),
                          static_cast<uint8_t>(118.0f * eff_scale));
        } else {
          const uint8_t fg = static_cast<uint8_t>(fade);
          g_col = UiColor(0, fg, static_cast<uint8_t>(fg >> 2));
        }
      }

      draw_rain_katakana_glyph(it, sx, gy, mask, g_col, dist > slen_f - 2.0f && slen > 8);
    }
  }
}

static constexpr Vec3 FIB_SPHERE_36[36] = {
    { 0.2333f,  0.9722f,  0.0000f}, {-0.3016f,  0.9167f,  0.2624f}, { 0.0445f,  0.8611f, -0.5065f},
    { 0.3471f,  0.8056f,  0.4800f}, {-0.6258f,  0.7500f, -0.2145f}, { 0.6775f,  0.6944f, -0.2423f},
    {-0.4359f,  0.6389f,  0.6337f}, {-0.0566f,  0.5833f, -0.8103f}, { 0.5624f,  0.5278f,  0.6365f},
    {-0.8588f,  0.4722f, -0.1987f}, { 0.8170f,  0.4167f, -0.3986f}, {-0.4052f,  0.3611f,  0.8399f},
    {-0.2286f,  0.3056f, -0.9243f}, { 0.7661f,  0.2500f,  0.5921f}, {-0.9628f,  0.1944f,  0.1885f},
    { 0.7254f,  0.1389f, -0.6742f}, {-0.1459f,  0.0833f,  0.9858f}, {-0.5313f,  0.0278f, -0.8467f},
    { 0.9469f, -0.0278f,  0.3204f}, {-0.8805f, -0.0833f,  0.4666f}, { 0.3827f, -0.1389f, -0.9134f},
    { 0.2896f, -0.1944f,  0.9372f}, {-0.8184f, -0.2500f, -0.5174f}, { 0.9438f, -0.3056f, -0.1264f},
    {-0.6337f, -0.3611f,  0.6842f}, { 0.0445f, -0.4167f, -0.9080f}, { 0.5798f, -0.4722f,  0.6640f},
    {-0.8501f, -0.5278f, -0.0085f}, { 0.6766f, -0.5833f, -0.4493f}, {-0.1882f, -0.6389f,  0.7459f},
    {-0.3360f, -0.6944f, -0.6363f}, { 0.6540f, -0.7500f,  0.0987f}, {-0.5480f, -0.8056f,  0.2255f},
    { 0.1476f, -0.8611f, -0.4865f}, { 0.2333f, -0.9167f,  0.3249f}, {-0.2333f, -0.9722f, -0.0145f}
};

void render_screensaver_fireworks(Rgb565Surface &it, int cx, int cy, uint32_t now_ms) {
  static constexpr uint32_t SHELL_CYCLE_MS = 3600U;
  static constexpr uint32_t SHELL_STAGGER_MS = 1200U;
  static constexpr int NUM_SPARKS = 36;

  const float fx_scale = static_cast<float>(SCREEN_MIN_DIM) / 466.0f;
  for (int s = 0; s < 3; s++) {
    const uint32_t t_offset = now_ms + static_cast<uint32_t>(s) * SHELL_STAGGER_MS;
    const uint32_t cycle_idx = t_offset / SHELL_CYCLE_MS;
    const uint32_t phase_ms = t_offset % SHELL_CYCLE_MS;
    const float phase = static_cast<float>(phase_ms) * (1.0f / static_cast<float>(SHELL_CYCLE_MS));

    const int seed_base = static_cast<int>(cycle_idx * 17U + static_cast<uint32_t>(s) * 53U);
    const float rx = hash2d_01(seed_base, 11);
    const float ry = hash2d_01(seed_base, 29);
    const float rtype = hash2d_01(seed_base, 47);
    const float rhue = hash2d_01(seed_base, 73);

    int bx = cx + static_cast<int>((rx - 0.5f) * 220.0f * fx_scale);
    int by = (cy - static_cast<int>(32.0f * fx_scale)) + static_cast<int>((ry - 0.5f) * 155.0f * fx_scale);
    const int dx_b = bx - cx;
    const int dy_b = by - cy;
    const int max_burst_r = static_cast<int>(120.0f * fx_scale);
    if (dx_b * dx_b + dy_b * dy_b > max_burst_r * max_burst_r) {
      bx = cx + (dx_b * 105) / 155;
      by = cy + (dy_b * 105) / 155;
    }

    const int launch_x = cx + static_cast<int>((hash2d_01(seed_base, 91) - 0.5f) * 120.0f * fx_scale);
#if LATTER_ROUND_DISPLAY
    const int launch_h = circle_half_span(launch_x, 10);
#else
    const int launch_h = (SCREEN_H / 2) - 10;
#endif
    const int launch_y = cy + (launch_h > 40 ? launch_h : static_cast<int>(180.0f * fx_scale));

    if (phase < 0.22f) {
      const float u = phase * (1.0f / 0.22f);
      const float ease = 1.0f - (1.0f - u) * (1.0f - u);
      const float prev_u = (u > 0.14f) ? (u - 0.14f) : 0.0f;
      const float prev_ease = 1.0f - (1.0f - prev_u) * (1.0f - prev_u);

      const float wob_x = 8.0f * fast_sin(u * 10.0f + static_cast<float>(s) * 2.1f) * (1.0f - u);
      const int hx = launch_x + static_cast<int>(static_cast<float>(bx - launch_x) * ease + wob_x);
      const int hy = launch_y + static_cast<int>(static_cast<float>(by - launch_y) * ease);
      const int tx = launch_x + static_cast<int>(static_cast<float>(bx - launch_x) * prev_ease);
      const int ty = launch_y + static_cast<int>(static_cast<float>(by - launch_y) * prev_ease);
      const int mx = (hx + tx) >> 1;
      const int my = (hy + ty) >> 1;

      if (is_inside_bezel(hx, hy, 5) && is_inside_bezel(tx, ty, 5)) {
        fast_line(it, tx, ty, mx, my, UiColor(160, 65, 10));
        draw_glow_line(it, mx, my, hx, hy, UiColor(255, 235, 150), UiColor(220, 110, 20));
        fast_filled_circle(it, hx, hy, 2, UiColor(255, 255, 220));
      }
      continue;
    }

    const float bp = (phase - 0.22f) * (1.0f / 0.78f);
    const float expand = 1.0f - (1.0f - bp) * (1.0f - bp) * (1.0f - bp);
    const float bp_tail = (bp > 0.15f) ? (bp - 0.15f) : (bp * 0.35f);
    const float expand_tail = 1.0f - (1.0f - bp_tail) * (1.0f - bp_tail) * (1.0f - bp_tail);

    const float grav_head = 54.0f * bp * bp;
    const float grav_tail = 54.0f * bp_tail * bp_tail;
    const float fade = 1.0f - bp;
    const float base_bright = std::fmin(1.0f, (bp < 0.12f) ? 1.0f : (fade * (1.12f - 0.12f * bp)));

    if (bp < 0.16f) {
      const float fl = 1.0f - (bp * 6.25f);
      const int flash_r = static_cast<int>((4.0f + bp * 90.0f) * fx_scale);
      fast_circle(it, bx, by, flash_r, UiColor(
          static_cast<uint8_t>(255.0f * fl),
          static_cast<uint8_t>(240.0f * fl),
          static_cast<uint8_t>(185.0f * fl)));
    }

    const int shell_style = static_cast<int>(rtype * 4.0f) & 3;
    const float max_radius = 98.0f + 32.0f * hash2d_01(seed_base, 61);
    const Mat3 shell_rot = Mat3::from_euler(
        hash2d_01(seed_base, 101) * PI_F + bp * 0.55f,
        hash2d_01(seed_base, 113) * PI_F - bp * 0.45f,
        hash2d_01(seed_base, 127) * PI_F + bp * 0.30f);

    for (int k = 0; k < NUM_SPARKS; k++) {
      const Vec3 dir_3d = shell_rot.transform(FIB_SPHERE_36[k]);
      const bool is_pistil = (shell_style == 1 && (k & 3) == 0);
      const float jitter = is_pistil ? 0.52f : (0.88f + 0.24f * hash2d_01(seed_base + k, k * 7));
      const float spark_rmax = max_radius * jitter;
      const float r_h = spark_rmax * expand;
      const float r_t = spark_rmax * expand_tail;

      const ProjectedPoint p_head = project_3d({dir_3d.x * r_h, dir_3d.y * r_h + grav_head, dir_3d.z * r_h}, bx, by, 260.0f, 320.0f);
      const ProjectedPoint p_tail = project_3d({dir_3d.x * r_t, dir_3d.y * r_t + grav_tail, dir_3d.z * r_t}, bx, by, 260.0f, 320.0f);
      if (!p_head.visible || !p_tail.visible ||
          !is_inside_bezel(p_head.x, p_head.y, 5) || !is_inside_bezel(p_tail.x, p_tail.y, 5)) continue;

      const float depth_boost = std::fmax(0.55f, std::fmin(1.15f, p_head.scale * 1.12f));
      const float bright = std::fmin(1.0f, base_bright * depth_boost);

      UiColor spark_col;
      if (shell_style == 0) {
        spark_col = UiColor(static_cast<uint8_t>(255.0f * bright),
                            static_cast<uint8_t>((215.0f * fade + 35.0f) * bright),
                            static_cast<uint8_t>(110.0f * fade * fade * bright));
      } else if (shell_style == 1) {
        spark_col = is_pistil
                        ? UiColor(static_cast<uint8_t>(40.0f * bright),
                                  static_cast<uint8_t>(245.0f * bright),
                                  static_cast<uint8_t>(255.0f * bright))
                        : UiColor(static_cast<uint8_t>(255.0f * bright),
                                  static_cast<uint8_t>((45.0f + 80.0f * fade) * bright),
                                  static_cast<uint8_t>(220.0f * bright));
      } else if (shell_style == 2) {
        spark_col = hsv_to_rgb(rhue + static_cast<float>(k) * (1.0f / static_cast<float>(NUM_SPARKS)), 0.82f, bright);
      } else {
        spark_col = ((k % 3) == 1)
                        ? UiColor(static_cast<uint8_t>(185.0f * bright),
                                  static_cast<uint8_t>(75.0f * bright),
                                  static_cast<uint8_t>(255.0f * bright))
                        : UiColor(static_cast<uint8_t>(35.0f * bright),
                                  static_cast<uint8_t>(255.0f * bright),
                                  static_cast<uint8_t>(120.0f * bright));
      }

      const int mx = (p_head.x + p_tail.x) >> 1;
      const int my = (p_head.y + p_tail.y) >> 1;
      const UiColor tail_col(static_cast<uint8_t>((spark_col.r * 95U) >> 8),
                             static_cast<uint8_t>((spark_col.g * 95U) >> 8),
                             static_cast<uint8_t>((spark_col.b * 95U) >> 8));
      fast_line(it, p_tail.x, p_tail.y, mx, my, tail_col);
      fast_line(it, mx, my, p_head.x, p_head.y, spark_col);

      if (bp < 0.58f) {
        const UiColor tip_col(static_cast<uint8_t>(spark_col.r + ((255 - spark_col.r) >> 1)),
                              static_cast<uint8_t>(spark_col.g + ((255 - spark_col.g) >> 1)),
                              static_cast<uint8_t>(spark_col.b + ((255 - spark_col.b) >> 1)));
        fast_pixel(it, p_head.x, p_head.y, tip_col);
        if (bp < 0.36f && dir_3d.z < -0.15f) {
          fast_filled_circle(it, p_head.x, p_head.y, 1, tip_col);
        }
      } else if (bp > 0.62f && ((static_cast<uint32_t>(k) + (phase_ms / 90U)) % 4U == 0U)) {
        const uint8_t gl = static_cast<uint8_t>(255.0f * fade);
        fast_pixel(it, p_head.x, p_head.y, UiColor(gl, gl, static_cast<uint8_t>(static_cast<float>(gl) * 0.8f)));
      }
    }
  }
}

}  // namespace

void render_docked_screensaver(Rgb565Surface &it, const UiFrameContext &ctx,
                               const SystemTakeoverState &st) {
  const int cx = st.cx;
  const int cy = st.cy;
  const uint32_t now_ms = st.now_ms;
  const UiStrView ss_style = st.screensaver_style_opt;

  static constexpr int8_t kMicroOrbit[12] = {-3, -2, -1, 0, 1, 2, 3, 2, 1, 0, -1, -2};
  const uint32_t min_idx = ctx.now.is_valid()
                               ? static_cast<uint32_t>(ctx.now.hour * 60 + ctx.now.minute)
                               : (now_ms / 60000U);
  const int drift_x = static_cast<int>(kMicroOrbit[min_idx % 12U]);
  const int drift_y = static_cast<int>(kMicroOrbit[(min_idx + 3U) % 12U]);

  if (ss_style == "Photo Frame") {
    const auto &ac = area_ctx_state();
    const ArtImageView photo_art = resolve_photo_frame_art(
        ctx.screensaver_photo_art, ctx.home_wallpaper_art,
        ac.screensaver_photo_url, runtime_state().music.loaded_wallpaper_url);
    if (photo_art.valid()) {
      const CameraRect full_vp{0, 0, SCREEN_W, SCREEN_H};
      blit_rgb565_fitted_frame(it, photo_art.data,
                               photo_art.width, photo_art.height,
                               full_vp, /*fill=*/true,
                               /*clip_round_bezel=*/(LATTER_ROUND_DISPLAY != 0));
      if (st.screensaver_photo_clock) {
        char photo_time[16];
        if (ctx.now.is_valid()) {
          format_time_hhmm(ctx.now.hour, ctx.now.minute, st.is_24h_format, photo_time, sizeof(photo_time));
        } else {
          std::snprintf(photo_time, sizeof(photo_time), "%s", kUnknownGlyph);
        }
        const UiFontView clk_font = (ctx.font_chrono_time != nullptr) ? ctx.font_chrono_time : ctx.font_date;
#if LATTER_ROUND_DISPLAY
        const int clk_x = cx + drift_x;
        const int clk_y = SCREEN_H - dp_px(58) + drift_y;
        const UiTextAlign align = UiTextAlign::CENTER;
#else
        const int clk_y = SCREEN_H - ((LATTER_CORNER_R >= 64) ? dp_px(40) : dp_px(28)) + drift_y;
        const int safe_half = circle_half_span(clk_y, dp_px(16));
        const int clk_x = cx + safe_half + drift_x;
        const UiTextAlign align = UiTextAlign::CENTER_RIGHT;
#endif
        fast_print(it, clk_x + 2, clk_y + 2, clk_font, kChromeShadow, align, photo_time);
        fast_print(it, clk_x + 1, clk_y + 1, clk_font, kChromeShadow, align, photo_time);
        fast_print(it, clk_x, clk_y, clk_font, st.col_white, align, photo_time);
      }
      return;
    }
    // Graceful fallback to Ambient Clock until Photo Frame image finishes downloading.
  }

  if (ss_style == "Ambient Clock" || ss_style == "Photo Frame") {
    const int amb_time_y = cy - dp_px(18) + drift_y;
    if (ctx.now.is_valid()) {
      char amb_time[16];
      format_time_hhmm(ctx.now.hour, ctx.now.minute, st.is_24h_format, amb_time, sizeof(amb_time));
      fast_print(it, cx + drift_x, amb_time_y, ctx.font_time, st.col_slate,
                 UiTextAlign::CENTER, amb_time);
      char date_buf[24];
      ctx.now.strftime(date_buf, sizeof(date_buf), "%a, %b %d");
      const int amb_date_y = amb_time_y + (kAutoFonts.time / 2) + (kAutoFonts.date / 2) + dp_px(12);
      fast_print(it, cx + drift_x, amb_date_y, ctx.font_date, kChromePipInactive,
                 UiTextAlign::CENTER, date_buf);
    } else {
      fast_print(it, cx + drift_x, amb_time_y, ctx.font_time, st.col_slate,
                 UiTextAlign::CENTER, kUnknownGlyph);
    }
    return;
  }

  if (ss_style == "Starfield") {
    render_screensaver_starfield(it, cx, cy, now_ms);
  } else if (ss_style == "Digital Rain") {
    render_screensaver_digital_rain(it, cy, now_ms);
  } else if (ss_style == "Fireworks") {
    render_screensaver_fireworks(it, cx, cy, now_ms);
  } else {
    render_screensaver_aurora(it, cy, now_ms);
  }
}

}  // namespace latter_ui
