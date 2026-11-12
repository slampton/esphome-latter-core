#include "esphome/core/defines.h"
#include "ui_pages.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace latter_ui {

namespace {

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

void render_screensaver_aurora(esphome::display::Display &it, int cx, int cy, uint32_t now_ms) {
  (void)cx;
  float t = now_ms * 0.0010f;

  float b1 = 0.92f + 0.10f * fast_sin(t * 0.9f);
  int p1_r = static_cast<int>(192.0f * b1);
  int p1_g = static_cast<int>(24.0f * b1);
  int p1_b = static_cast<int>(205.0f * b1);

  float b2 = 0.92f + 0.10f * fast_cos(t * 1.1f + 1.2f);
  int p2_r = static_cast<int>(12.0f * b2);
  int p2_g = static_cast<int>(228.0f * b2);
  int p2_b = static_cast<int>(242.0f * b2);

  float b3 = 0.92f + 0.10f * fast_sin(t * 0.8f + 2.5f);
  int p3_r = static_cast<int>(20.0f * b3);
  int p3_g = static_cast<int>(235.0f * b3);
  int p3_b = static_cast<int>(82.0f * b3);

  static constexpr int X_STEP = (SCREEN_W > 520) ? 4 : 3;
  static constexpr int FILL_W = X_STEP;
  static constexpr int MAX_COLS = (SCREEN_W / X_STEP) + 2;

  struct AuroraCol {
    int16_t x;
    int16_t col_top, col_bot;
    int16_t y1, top1, step1_fp;
    int16_t y2, top2, step2_fp;
    int16_t y3, top3, step3_fp;
  };
  AuroraCol cols[MAX_COLS];
  int num_cols = 0;
  int global_top = SCREEN_H;
  int global_bot = 0;

  const float h_sc = static_cast<float>(SCREEN_H) / 466.0f;
  const float x_sc = 466.0f / static_cast<float>(SCREEN_W);

  for (int x = 6; x <= SCREEN_W - 6 && num_cols < MAX_COLS; x += X_STEP) {
#if LATTER_ROUND_DISPLAY
    int h_half = circle_half_span(x, 5);
    if (h_half <= 0) continue;
    int y_min = cy - h_half;
    int y_max = cy + h_half;
#else
    int y_min = 6;
    int y_max = SCREEN_H - 6;
#endif
    float xf = static_cast<float>(x) * x_sc;

    int y1 = cy - static_cast<int>(10.0f * h_sc) +
             static_cast<int>((fast_sin(xf * 0.011f + t * 0.6f) * 58.0f + fast_cos(xf * 0.006f - t * 0.35f) * 28.0f) * h_sc);
    int len1 = static_cast<int>((120.0f + fast_sin(xf * 0.024f + t * 1.2f) * 35.0f) * h_sc);
    int top1 = y1 - len1;
    int step1_fp = (63 << 10) / (len1 > 1 ? len1 : 1);

    int y2 = cy + static_cast<int>(32.0f * h_sc) +
             static_cast<int>((fast_sin(xf * 0.014f - t * 0.8f + 1.6f) * 62.0f + fast_sin(xf * 0.008f + t * 0.5f) * 32.0f) * h_sc);
    int len2 = static_cast<int>((135.0f + fast_cos(xf * 0.028f - t * 1.5f) * 40.0f) * h_sc);
    int top2 = y2 - len2;
    int step2_fp = (63 << 10) / (len2 > 1 ? len2 : 1);

    int y3 = cy + static_cast<int>(78.0f * h_sc) +
             static_cast<int>((fast_sin(xf * 0.017f + t * 1.0f + 3.1f) * 66.0f + fast_cos(xf * 0.010f - t * 0.7f) * 36.0f) * h_sc);
    int len3 = static_cast<int>((150.0f + fast_sin(xf * 0.032f + t * 1.8f) * 44.0f) * h_sc);
    int top3 = y3 - len3;
    int step3_fp = (63 << 10) / (len3 > 1 ? len3 : 1);

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

  for (int py = global_top; py <= global_bot; py += 2) {
    for (int i = 0; i < num_cols; i++) {
      const auto &c = cols[i];
      if (py < c.col_top || py > c.col_bot) continue;

      int r = 0, g = 0, b = 0;
      uint32_t w_sum = 0;
      uint32_t w_max = 0;

      if (py <= c.y1 && py >= c.top1) {
        int idx1 = ((c.y1 - py) * c.step1_fp) >> 10;
        uint32_t w1 = AURORA_PLUME_LUT[idx1 & 63];
        r += (p1_r * static_cast<int>(w1)) >> 8;
        g += (p1_g * static_cast<int>(w1)) >> 8;
        b += (p1_b * static_cast<int>(w1)) >> 8;
        w_sum += w1;
        w_max = w1;
      }
      if (py <= c.y2 && py >= c.top2) {
        int idx2 = ((c.y2 - py) * c.step2_fp) >> 10;
        uint32_t w2 = AURORA_PLUME_LUT[idx2 & 63];
        r += (p2_r * static_cast<int>(w2)) >> 8;
        g += (p2_g * static_cast<int>(w2)) >> 8;
        b += (p2_b * static_cast<int>(w2)) >> 8;
        w_sum += w2;
        if (w2 > w_max) w_max = w2;
      }
      if (py <= c.y3 && py >= c.top3) {
        int idx3 = ((c.y3 - py) * c.step3_fp) >> 10;
        uint32_t w3 = AURORA_PLUME_LUT[idx3 & 63];
        r += (p3_r * static_cast<int>(w3)) >> 8;
        g += (p3_g * static_cast<int>(w3)) >> 8;
        b += (p3_b * static_cast<int>(w3)) >> 8;
        w_sum += w3;
        if (w3 > w_max) w_max = w3;
      }

      if (w_sum > w_max && w_sum > 0) {
        uint32_t w_target = w_max + ((w_sum - w_max) >> 2);
        r = static_cast<int>((static_cast<uint32_t>(r) * w_target) / w_sum);
        g = static_cast<int>((static_cast<uint32_t>(g) * w_target) / w_sum);
        b = static_cast<int>((static_cast<uint32_t>(b) * w_target) / w_sum);
      }

      if ((r | g | b) > 5) {
        uint8_t cr = static_cast<uint8_t>(r > 255 ? 255 : r);
        uint8_t cg = static_cast<uint8_t>(g > 255 ? 255 : g);
        uint8_t cb = static_cast<uint8_t>(b > 255 ? 255 : b);
        const esphome::Color veil_col(cr, cg, cb);
        fast_hline(it, c.x, py, FILL_W, veil_col);
        if (py + 1 <= c.col_bot) fast_hline(it, c.x, py + 1, FILL_W, veil_col);
      }
    }
  }
}

void render_screensaver_starfield(esphome::display::Display &it, int cx, int cy, uint32_t now_ms) {
  struct Star3D {
    float x, y, z;
  };
  static Star3D stars[90];
  static bool stars_init = false;
  static uint32_t star_seed = 123456789;
  static uint32_t last_star_ms = 0;

  auto star_rand = [&]() -> float {
    star_seed = star_seed * 1664525u + 1013904223u;
    return static_cast<float>(star_seed & 0xFFFFU) * (1.0f / 65535.0f);
  };
  auto respawn_star = [&](int i, bool init_z) {
    float ang = star_rand() * (2.0f * PI_F);
    float dist = 95.0f + star_rand() * 600.0f;
    stars[i].x = fast_cos(ang) * dist;
    stars[i].y = fast_sin(ang) * dist;
    stars[i].z = init_z ? (30.0f + star_rand() * 950.0f) : (850.0f + star_rand() * 150.0f);
  };
  if (!stars_init) {
    for (int i = 0; i < 90; i++) respawn_star(i, true);
    stars_init = true;
    last_star_ms = now_ms;
  }

  float speed = 14.0f;
  if (now_ms > 0) {
    uint32_t dt = now_ms - last_star_ms;
    last_star_ms = now_ms;
    if (dt >= 20U && dt <= 300U) {
      speed = static_cast<float>(dt) * 0.28f;
    }
  }

  for (int i = 0; i < 90; i++) {
    stars[i].z -= speed;
    if (stars[i].z <= 25.0f) {
      respawn_star(i, false);
      continue;
    }
    float k = 230.0f / stars[i].z;
    int sx = cx + static_cast<int>(stars[i].x * k);
    int sy = cy + static_cast<int>(stars[i].y * k);
    if (!is_inside_bezel(sx, sy, cy, 6)) {
      respawn_star(i, false);
      continue;
    }
    float prev_k = 230.0f / (stars[i].z + speed * 1.65f);
    int px = cx + static_cast<int>(stars[i].x * prev_k);
    int py = cy + static_cast<int>(stars[i].y * prev_k);
    bool is_blue = ((i % 5) == 0);
    if (stars[i].z > 650.0f) {
      fast_pixel(it, sx, sy, esphome::Color(148, 154, 166));
    } else if (stars[i].z > 350.0f) {
      fast_line(it, px, py, sx, sy, is_blue ? esphome::Color(135, 205, 250) : esphome::Color(195, 202, 212));
      fast_pixel(it, sx, sy, esphome::Color(245, 250, 255));
    } else {
      fast_line(it, px, py, sx, sy, is_blue ? esphome::Color(95, 210, 255) : esphome::Color(222, 228, 238));
      fast_filled_circle(it, sx, sy, 1, esphome::Color(255, 255, 255));
    }
  }
}

static constexpr uint16_t DIGITAL_RAIN_GLYPH_MASK[32] = {
    0x121, 0x181, 0x069, 0x085, 0x183, 0x06B, 0x083, 0x129,
    0x0A9, 0x065, 0x0A3, 0x105, 0x214, 0x12B, 0x182, 0x007,
    0x301, 0x381, 0x108, 0x056, 0x141, 0x187, 0x223, 0x207,
    0x114, 0x300, 0x097, 0x067, 0x107, 0x028, 0x07D, 0x204
};

void draw_rain_katakana_glyph(esphome::display::Display &it, int sx, int gy, uint16_t mask,
                              esphome::Color col, bool slim_tail) {
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

void render_screensaver_digital_rain(esphome::display::Display &it, int cx, int cy, uint32_t now_ms) {
  (void)cx;
  static constexpr int CELL_H = 14;
  static constexpr int NUM_COLS = (SCREEN_W - 24) / 19;
  static constexpr int GRID_ROWS = (SCREEN_H / CELL_H) + 1;
  static constexpr int X_MARGIN = (SCREEN_W - (NUM_COLS - 1) * 19) / 2;

  float t_drift = now_ms * 0.00007f;
  int drift_x = static_cast<int>(fast_sin(t_drift) * 4.2f);
  int drift_y = static_cast<int>(fast_cos(t_drift * 1.3f) * 3.2f);
  float t_sec = now_ms * 0.001f;

  for (int i = 0; i < NUM_COLS; i++) {
    int base_x = X_MARGIN + i * 19;
    int sx = base_x + drift_x;
#if LATTER_ROUND_DISPLAY
    int max_dy = circle_half_span(sx, 7);
    if (max_dy <= 0) continue;
    int y_min = cy - max_dy;
    int y_max = cy + max_dy;
#else
    if (sx < 7 || sx >= SCREEN_W - 7) continue;
    int y_min = 7;
    int y_max = SCREEN_H - 8;
#endif

    int clip_min_row = (y_min - drift_y) / CELL_H;
    int clip_max_row = (y_max - drift_y) / CELL_H;
    if (clip_max_row - clip_min_row < 2) continue;

    int perm_i = (i * 7 + 3) % NUM_COLS;
    float col_h1 = hash2d_01(perm_i * 41 + 13, i * 19 + 5);
    float col_h2 = hash2d_01(perm_i * 59 + 29, i * 31 + 11);

    float rows_per_sec = 5.2f + col_h1 * 6.8f;
    float cycle_rows_f = static_cast<float>(GRID_ROWS) + 22.0f + col_h2 * 14.0f;

    float init_phase_frac = std::fmod(static_cast<float>(i) * 0.381966011f + static_cast<float>(perm_i) * 0.145898f, 1.0f);
    float total_travel = t_sec * rows_per_sec + init_phase_frac * cycle_rows_f;

    uint32_t pass_idx = static_cast<uint32_t>(total_travel / cycle_rows_f);
    float local_pos = total_travel - static_cast<float>(pass_idx) * cycle_rows_f;

    int pass_seed = static_cast<int>(pass_idx * 47U + static_cast<uint32_t>(i) * 113U);
    float r_len = hash2d_01(pass_seed, 13);
    float r_tier = hash2d_01(pass_seed, 29);
    float r_spawn = hash2d_01(pass_seed, 47);
    float r_end = hash2d_01(pass_seed, 61);

    int slen;
    if (r_len > 0.65f) {
      slen = 18 + static_cast<int>((r_len - 0.65f) * 28.0f);
    } else if (r_len > 0.20f) {
      slen = 11 + static_cast<int>((r_len - 0.20f) * 15.0f);
    } else {
      slen = 6 + static_cast<int>(r_len * 24.0f);
    }
    float slen_f = static_cast<float>(slen);
    float inv_tail_span = 1.0f / (slen_f > 2.4f ? (slen_f - 2.2f) : 1.0f);

    int spawn_row = (r_spawn < 0.40f)
                        ? (4 + static_cast<int>(r_spawn * 28.0f))
                        : (-2 + static_cast<int>((r_spawn - 0.40f) * 5.0f));
    int dissolve_row = (r_end < 0.25f)
                           ? (20 + static_cast<int>(r_end * 34.0f))
                           : (GRID_ROWS + 4);

    bool is_bg_stream = (r_tier < 0.28f);
    float bright_scale = is_bg_stream ? 0.52f : 1.0f;

    float head_row_f = static_cast<float>(spawn_row) + local_pos;
    int head_row_top = static_cast<int>(std::floor(head_row_f));

    if (!is_bg_stream && head_row_top <= dissolve_row) {
      int exact_hy = static_cast<int>(head_row_f * static_cast<float>(CELL_H)) + drift_y;
      if (exact_hy >= y_min && exact_hy <= y_max && head_row_f >= static_cast<float>(spawn_row)) {
        fill_rect_row_major(it, sx - 4, exact_hy - 5, 9, 11, esphome::Color(0, 62, 18));
      }
    }

    for (int j = 0; j <= slen; j++) {
      int row = head_row_top - j;
      if (row < spawn_row || row > dissolve_row) continue;
      if (row < clip_min_row || row > clip_max_row) continue;
      int gy = row * CELL_H + drift_y;
      if (gy < y_min || gy > y_max) continue;

      float dist = head_row_f - static_cast<float>(row);
      if (dist < 0.0f || dist > slen_f) continue;

      float dissolve_fade = 1.0f;
      if (head_row_f > static_cast<float>(dissolve_row)) {
        float over = head_row_f - static_cast<float>(dissolve_row);
        dissolve_fade = std::fmax(0.0f, 1.0f - over * (1.0f / (slen_f * 0.75f)));
        if (dissolve_fade <= 0.05f) continue;
      }

      float eff_scale = bright_scale * dissolve_fade;

      uint32_t cell_hash = static_cast<uint32_t>(i * 73856093) ^
                           static_cast<uint32_t>(row * 19349663) ^
                           (pass_idx * 83492791U);
      uint32_t glitch_tick = (dist < 1.1f)
                                 ? (now_ms / 85U)
                                 : (((cell_hash & 7U) == 0U) ? (now_ms / (190U + (cell_hash & 255U))) : 0U);
      uint16_t mask = DIGITAL_RAIN_GLYPH_MASK[(cell_hash + glitch_tick * 19U) & 31U];

      esphome::Color g_col;
      if (dist < 0.95f && head_row_top <= dissolve_row) {
        float lead_env = ((dist < 0.30f) ? (0.45f + (dist * (1.0f / 0.30f)) * 0.55f) : 1.0f) * dissolve_fade;
        if (!is_bg_stream) {
          g_col = esphome::Color(
              static_cast<uint8_t>(225.0f * lead_env),
              static_cast<uint8_t>(255.0f * lead_env),
              static_cast<uint8_t>(235.0f * lead_env));
        } else {
          g_col = esphome::Color(
              static_cast<uint8_t>(110.0f * lead_env),
              static_cast<uint8_t>(225.0f * lead_env),
              static_cast<uint8_t>(138.0f * lead_env));
        }
      } else if (dist < 2.2f) {
        float u = (dist - 0.95f) * (1.0f / 1.25f);
        if (u < 0.0f) u = 0.0f;
        float r_c = (115.0f * (1.0f - u) + 24.0f * u) * eff_scale;
        float g_c = (255.0f * (1.0f - u) + 225.0f * u) * eff_scale;
        float b_c = (140.0f * (1.0f - u) + 58.0f * u) * eff_scale;
        g_col = esphome::Color(static_cast<uint8_t>(r_c), static_cast<uint8_t>(g_c), static_cast<uint8_t>(b_c));
      } else {
        float tail_u = (dist - 2.2f) * inv_tail_span;
        if (tail_u > 1.0f) tail_u = 1.0f;
        float inv_u = 1.0f - tail_u;
        float fade = (14.0f + 200.0f * inv_u * (0.60f + 0.40f * inv_u)) * eff_scale;

        bool glitch_flash = (dist < 6.0f) && ((cell_hash & 7U) == 0U) &&
                            (((now_ms / 160U) + static_cast<uint32_t>(row)) % 6U == 0U);
        if (glitch_flash) {
          g_col = esphome::Color(
              static_cast<uint8_t>(90.0f * eff_scale),
              static_cast<uint8_t>(255.0f * eff_scale),
              static_cast<uint8_t>(118.0f * eff_scale));
        } else {
          uint8_t fg = static_cast<uint8_t>(fade);
          g_col = esphome::Color(0, fg, static_cast<uint8_t>(fg >> 2));
        }
      }

      bool slim_tail = (dist > slen_f - 2.0f && slen > 8);
      draw_rain_katakana_glyph(it, sx, gy, mask, g_col, slim_tail);
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

void render_screensaver_fireworks(esphome::display::Display &it, int cx, int cy, uint32_t now_ms) {
  static constexpr uint32_t SHELL_CYCLE_MS = 3600U;
  static constexpr uint32_t SHELL_STAGGER_MS = 1200U;
  static constexpr int NUM_SPARKS = 36;

  for (int s = 0; s < 3; s++) {
    uint32_t t_offset = now_ms + static_cast<uint32_t>(s) * SHELL_STAGGER_MS;
    uint32_t cycle_idx = t_offset / SHELL_CYCLE_MS;
    uint32_t phase_ms = t_offset % SHELL_CYCLE_MS;
    float phase = static_cast<float>(phase_ms) * (1.0f / static_cast<float>(SHELL_CYCLE_MS));

    int seed_base = static_cast<int>(cycle_idx * 17U + static_cast<uint32_t>(s) * 53U);
    float rx = hash2d_01(seed_base, 11);
    float ry = hash2d_01(seed_base, 29);
    float rtype = hash2d_01(seed_base, 47);
    float rhue = hash2d_01(seed_base, 73);

    int bx = cx + static_cast<int>((rx - 0.5f) * scale_min_f(220.0f));
    int by = (cy - scale_min(32)) + static_cast<int>((ry - 0.5f) * scale_min_f(155.0f));
    int dx_b = bx - cx, dy_b = by - cy;
    int max_burst_r = scale_min(120);
    if (dx_b * dx_b + dy_b * dy_b > max_burst_r * max_burst_r) {
      bx = cx + (dx_b * 105) / 155;
      by = cy + (dy_b * 105) / 155;
    }

    int launch_x = cx + static_cast<int>((hash2d_01(seed_base, 91) - 0.5f) * scale_min_f(120.0f));
#if LATTER_ROUND_DISPLAY
    int launch_h = circle_half_span(launch_x, 10);
#else
    int launch_h = (SCREEN_H / 2) - 10;
#endif
    int launch_y = cy + (launch_h > 40 ? launch_h : scale_min(180));

    if (phase < 0.22f) {
      float u = phase * (1.0f / 0.22f);
      float ease = 1.0f - (1.0f - u) * (1.0f - u);
      float prev_u = (u > 0.14f) ? (u - 0.14f) : 0.0f;
      float prev_ease = 1.0f - (1.0f - prev_u) * (1.0f - prev_u);

      float wob_x = 8.0f * fast_sin(u * 10.0f + static_cast<float>(s) * 2.1f) * (1.0f - u);
      int hx = launch_x + static_cast<int>((bx - launch_x) * ease + wob_x);
      int hy = launch_y + static_cast<int>((by - launch_y) * ease);
      int tx = launch_x + static_cast<int>((bx - launch_x) * prev_ease);
      int ty = launch_y + static_cast<int>((by - launch_y) * prev_ease);
      int mx = (hx + tx) >> 1;
      int my = (hy + ty) >> 1;

      if (is_inside_bezel(hx, hy, cy, 5) && is_inside_bezel(tx, ty, cy, 5)) {
        fast_line(it, tx, ty, mx, my, esphome::Color(160, 65, 10));
        draw_glow_line(it, mx, my, hx, hy, esphome::Color(255, 235, 150), esphome::Color(220, 110, 20));
        fast_filled_circle(it, hx, hy, 2, esphome::Color(255, 255, 220));
      }
      continue;
    }

    float bp = (phase - 0.22f) * (1.0f / 0.78f);
    float expand = 1.0f - (1.0f - bp) * (1.0f - bp) * (1.0f - bp);
    float bp_tail = (bp > 0.15f) ? (bp - 0.15f) : (bp * 0.35f);
    float expand_tail = 1.0f - (1.0f - bp_tail) * (1.0f - bp_tail) * (1.0f - bp_tail);

    float grav_head = 54.0f * bp * bp;
    float grav_tail = 54.0f * bp_tail * bp_tail;
    float fade = (1.0f - bp);
    float base_bright = (bp < 0.12f) ? 1.0f : (fade * (1.12f - 0.12f * bp));
    if (base_bright > 1.0f) base_bright = 1.0f;

    if (bp < 0.16f) {
      float fl = 1.0f - (bp * 6.25f);
      int flash_r = static_cast<int>(4.0f + bp * 90.0f);
      fast_circle(it, bx, by, flash_r, esphome::Color(
          static_cast<uint8_t>(255.0f * fl),
          static_cast<uint8_t>(240.0f * fl),
          static_cast<uint8_t>(185.0f * fl)));
    }

    int shell_style = static_cast<int>(rtype * 4.0f) & 3;
    float max_radius = 98.0f + 32.0f * hash2d_01(seed_base, 61);

    float rot_x = hash2d_01(seed_base, 101) * PI_F + bp * 0.55f;
    float rot_y = hash2d_01(seed_base, 113) * PI_F - bp * 0.45f;
    float rot_z = hash2d_01(seed_base, 127) * PI_F + bp * 0.30f;
    const Mat3 shell_rot = Mat3::from_euler(rot_x, rot_y, rot_z);

    for (int k = 0; k < NUM_SPARKS; k++) {
      Vec3 dir_3d = shell_rot.transform(FIB_SPHERE_36[k]);

      bool is_pistil = (shell_style == 1 && (k & 3) == 0);
      float jitter = is_pistil ? 0.52f : (0.88f + 0.24f * hash2d_01(seed_base + k, k * 7));
      float spark_rmax = max_radius * jitter;

      float r_h = spark_rmax * expand;
      float r_t = spark_rmax * expand_tail;

      Vec3 v_head{dir_3d.x * r_h, dir_3d.y * r_h + grav_head, dir_3d.z * r_h};
      Vec3 v_tail{dir_3d.x * r_t, dir_3d.y * r_t + grav_tail, dir_3d.z * r_t};

      ProjectedPoint p_head = project_3d(v_head, bx, by, 260.0f, 320.0f);
      ProjectedPoint p_tail = project_3d(v_tail, bx, by, 260.0f, 320.0f);
      if (!is_inside_bezel(p_head.x, p_head.y, cy, 5) || !is_inside_bezel(p_tail.x, p_tail.y, cy, 5)) continue;

      float depth_boost = std::fmax(0.55f, std::fmin(1.15f, p_head.scale * 1.12f));
      float bright = std::fmin(1.0f, base_bright * depth_boost);

      esphome::Color spark_col;
      if (shell_style == 0) {
        spark_col = esphome::Color(
            static_cast<uint8_t>(255.0f * bright),
            static_cast<uint8_t>((215.0f * fade + 35.0f) * bright),
            static_cast<uint8_t>(110.0f * fade * fade * bright));
      } else if (shell_style == 1) {
        if (is_pistil) {
          spark_col = esphome::Color(
              static_cast<uint8_t>(40.0f * bright),
              static_cast<uint8_t>(245.0f * bright),
              static_cast<uint8_t>(255.0f * bright));
        } else {
          spark_col = esphome::Color(
              static_cast<uint8_t>(255.0f * bright),
              static_cast<uint8_t>((45.0f + 80.0f * fade) * bright),
              static_cast<uint8_t>(220.0f * bright));
        }
      } else if (shell_style == 2) {
        float h = rhue + static_cast<float>(k) * (1.0f / static_cast<float>(NUM_SPARKS));
        spark_col = hsv_to_rgb(h, 0.82f, bright);
      } else {
        if ((k % 3) == 1) {
          spark_col = esphome::Color(
              static_cast<uint8_t>(185.0f * bright),
              static_cast<uint8_t>(75.0f * bright),
              static_cast<uint8_t>(255.0f * bright));
        } else {
          spark_col = esphome::Color(
              static_cast<uint8_t>(35.0f * bright),
              static_cast<uint8_t>(255.0f * bright),
              static_cast<uint8_t>(120.0f * bright));
        }
      }

      int mx = (p_head.x + p_tail.x) >> 1;
      int my = (p_head.y + p_tail.y) >> 1;
      esphome::Color tail_col(
          static_cast<uint8_t>((spark_col.r * 95U) >> 8),
          static_cast<uint8_t>((spark_col.g * 95U) >> 8),
          static_cast<uint8_t>((spark_col.b * 95U) >> 8));
      fast_line(it, p_tail.x, p_tail.y, mx, my, tail_col);
      fast_line(it, mx, my, p_head.x, p_head.y, spark_col);

      if (bp < 0.58f) {
        esphome::Color tip_col(
            static_cast<uint8_t>(spark_col.r + ((255 - spark_col.r) >> 1)),
            static_cast<uint8_t>(spark_col.g + ((255 - spark_col.g) >> 1)),
            static_cast<uint8_t>(spark_col.b + ((255 - spark_col.b) >> 1)));
        fast_pixel(it, p_head.x, p_head.y, tip_col);
        if (bp < 0.36f && dir_3d.z < -0.15f) {
          fast_filled_circle(it, p_head.x, p_head.y, 1, tip_col);
        }
      } else if (bp > 0.62f && ((k + (phase_ms / 90U)) % 4 == 0)) {
        uint8_t gl = static_cast<uint8_t>(255.0f * fade);
        fast_pixel(it, p_head.x, p_head.y, esphome::Color(gl, gl, static_cast<uint8_t>(gl * 0.8f)));
      }
    }
  }
}

esphome::Color get_va_radiation_color(bool is_solaris, float t) {
  t = t - std::floor(t);
  if (is_solaris) {
    if (t < 0.333f) {
      float k = t / 0.333f;
      return esphome::Color(static_cast<uint8_t>(35.0f * (1.0f - k) + 255.0f * k),
                            static_cast<uint8_t>(210.0f * (1.0f - k) + 220.0f * k),
                            static_cast<uint8_t>(110.0f * (1.0f - k) + 30.0f * k));
    }
    if (t < 0.667f) {
      float k = (t - 0.333f) / 0.334f;
      return esphome::Color(static_cast<uint8_t>(255.0f * (1.0f - k) + 250.0f * k),
                            static_cast<uint8_t>(220.0f * (1.0f - k) + 100.0f * k),
                            static_cast<uint8_t>(30.0f * (1.0f - k) + 15.0f * k));
    }
    float k = (t - 0.667f) / 0.333f;
    return esphome::Color(static_cast<uint8_t>(250.0f * (1.0f - k) + 35.0f * k),
                          static_cast<uint8_t>(100.0f * (1.0f - k) + 210.0f * k),
                          static_cast<uint8_t>(15.0f * (1.0f - k) + 110.0f * k));
  }
  if (t < 0.333f) {
    float k = t / 0.333f;
    return esphome::Color(static_cast<uint8_t>(138.0f * k),
                          static_cast<uint8_t>(229.0f * (1.0f - k) + 43.0f * k),
                          static_cast<uint8_t>(255.0f * (1.0f - k) + 226.0f * k));
  }
  if (t < 0.667f) {
    float k = (t - 0.333f) / 0.334f;
    return esphome::Color(static_cast<uint8_t>(138.0f + 86.0f * k),
                          static_cast<uint8_t>(43.0f + 21.0f * k),
                          static_cast<uint8_t>(226.0f + 25.0f * k));
  }
  float k = (t - 0.667f) / 0.333f;
  return esphome::Color(static_cast<uint8_t>(224.0f * (1.0f - k)),
                        static_cast<uint8_t>(64.0f * (1.0f - k) + 229.0f * k),
                        255);
}

}  // namespace

void render_docked_screensaver(esphome::display::Display &it, const UiFrameContext &ctx,
                               const SystemTakeoverState &st) {
  const int cx = st.cx;
  const int cy = st.cy;
  const uint32_t now_ms = st.now_ms;
  const std::string &ss_style = st.screensaver_style_opt;

  if (ss_style == "Ambient Clock") {
    static constexpr int8_t kMicroOrbit[12] = {-3, -2, -1, 0, 1, 2, 3, 2, 1, 0, -1, -2};
    const uint32_t min_idx = ctx.now.is_valid()
                                 ? static_cast<uint32_t>(ctx.now.hour * 60 + ctx.now.minute)
                                 : (now_ms / 60000U);
    const int drift_x = static_cast<int>(kMicroOrbit[min_idx % 12U]);
    const int drift_y = static_cast<int>(kMicroOrbit[(min_idx + 3U) % 12U]);

    if (ctx.now.is_valid()) {
      char amb_time[16];
      format_time_hhmm(ctx.now.hour, ctx.now.minute, st.is_24h_format, amb_time, sizeof(amb_time));
      fast_print(it, cx + drift_x, cy - scale_y(20) + drift_y, ctx.font_time, st.col_slate,
                 esphome::display::TextAlign::CENTER, amb_time);
      char date_buf[24];
      esphome::ESPTime now_copy = ctx.now;
      now_copy.strftime(date_buf, sizeof(date_buf), "%a, %b %d");
      fast_print(it, cx + drift_x, cy + scale_y(48) + drift_y, ctx.font_date, kChromePipInactive,
                 esphome::display::TextAlign::CENTER, date_buf);
    } else {
      fast_print(it, cx + drift_x, cy - scale_y(20) + drift_y, ctx.font_time, st.col_slate,
                 esphome::display::TextAlign::CENTER, kUnknownGlyph);
    }
    return;
  }

  if (ss_style == "Starfield") {
    render_screensaver_starfield(it, cx, cy, now_ms);
  } else if (ss_style == "Digital Rain") {
    render_screensaver_digital_rain(it, cx, cy, now_ms);
  } else if (ss_style == "Fireworks") {
    render_screensaver_fireworks(it, cx, cy, now_ms);
  } else {
    render_screensaver_aurora(it, cx, cy, now_ms);
  }
}

void render_voice_assistant_overlay(esphome::display::Display &it, const UiFrameContext &ctx,
                                    const SystemTakeoverState &st) {
  (void)ctx;
  const int cx = st.cx;
  const int cy = st.cy;
  const uint32_t now_ms = st.now_ms;
  const bool is_speaking = st.va_tts_received || st.va_status_text == "Responding..." ||
                           st.va_status_text == "Speaking...";
  const bool is_thinking = !is_speaking &&
                           (st.va_status_text == "Thinking..." || st.va_status_text == "Processing...");
  const bool va_vad_active = st.va_vad_active;
  const std::string &profile_opt = st.va_visual_profile_opt;
  const esphome::Color col_white = st.col_white;
  const esphome::Color col_solaris_gold = st.col_solaris_gold;

  frame_hits().add_rect(0, 0, SCREEN_W, SCREEN_H, WidgetId::VA_CANCEL_BTN, kHitTap);

  const bool is_solaris = (profile_opt == "Solaris");
  const bool is_light_bar = (profile_opt == "Light Bar");
  const bool is_dot_wave = (profile_opt == "Dot Wave");
  const bool is_rings = (profile_opt == "Rings");

  static float smooth_vad = 0.0f;
  static float smooth_think = 0.0f;
  static float smooth_speak = 0.0f;
  static bool prev_speaking = false;
  static uint32_t speak_start_ms = 0;

  if (is_speaking && !prev_speaking) {
    speak_start_ms = now_ms;
  }
  prev_speaking = is_speaking;

  const float vad_target = va_vad_active ? 1.0f : 0.0f;
  const float think_target = is_thinking ? 1.0f : 0.0f;
  const float speak_target = is_speaking ? 1.0f : 0.0f;
  smooth_vad += (vad_target - smooth_vad) * 0.22f;
  smooth_think += (think_target - smooth_think) * 0.22f;
  smooth_speak += (speak_target - smooth_speak) * 0.22f;

  // ============================================================================
  // PROFILE: "LIGHT BAR" — 360-DEGREE SPINNING CYAN & ROYAL BLUE LIGHT RING
  // ============================================================================
  if (is_light_bar) {
    const esphome::Color c_bar_blue(0, 52, 235);
    const esphome::Color c_bar_deep(0, 22, 115);
    const esphome::Color c_bar_cyan(0, 229, 255);
    const esphome::Color c_bar_ice(215, 252, 255);

    float t_sec = static_cast<float>(now_ms % 60000U) * 0.001f;
    float syl_env = is_speaking
                        ? ((0.55f + 0.25f * fast_sin(t_sec * 9.5f) + 0.20f * fast_cos(t_sec * 15.3f)) * smooth_speak)
                        : 0.0f;

    const int max_ring_r = SCREEN_MIN_R - 3;
    const int base_ring_r = max_ring_r - 14;
    int inner_glow_r = (base_ring_r - 6) - static_cast<int>(6.0f * smooth_vad + 8.0f * syl_env);
    for (int r = inner_glow_r; r <= max_ring_r; r++) {
      esphome::Color rc;
      if (r < base_ring_r) {
        float k = static_cast<float>(r - inner_glow_r) / static_cast<float>(base_ring_r - inner_glow_r);
        rc = scale_color(c_bar_deep, k * k * 0.85f);
      } else if (r <= max_ring_r - 3) {
        rc = c_bar_blue;
      } else {
        rc = c_bar_deep;
      }
      fast_circle(it, cx, cy, r, rc);
    }

    float head_f = 0.0f;
    if (is_thinking) {
      head_f = std::fmod(t_sec * 52.0f, 60.0f);
    } else if (is_speaking) {
      head_f = std::fmod(t_sec * 34.0f, 60.0f);
    } else {
      head_f = std::fmod(t_sec * (24.0f + 14.0f * smooth_vad), 60.0f);
    }

    for (int i = 0; i < 60; i++) {
      int next_i = (i + 1) % 60;
      float cyan_w = 0.0f;

      float d1 = head_f - static_cast<float>(i);
      if (d1 < 0.0f) d1 += 60.0f;
      float head2_f = std::fmod(head_f + 30.0f, 60.0f);
      float d2 = head2_f - static_cast<float>(i);
      if (d2 < 0.0f) d2 += 60.0f;

      if (is_speaking) {
        float tail_len = 13.0f + 10.0f * syl_env;
        if (d1 < tail_len) cyan_w = std::fmax(cyan_w, 1.0f - (d1 / tail_len));
        if (d2 < tail_len) cyan_w = std::fmax(cyan_w, 1.0f - (d2 / tail_len));
      } else if (is_thinking) {
        if (d1 < 14.0f) cyan_w = 1.0f - (d1 * (1.0f / 14.0f));
        else if (d2 < 14.0f) cyan_w = 1.0f - (d2 * (1.0f / 14.0f));
      } else {
        float tail_len = 14.0f + 10.0f * smooth_vad;
        if (d1 < tail_len) cyan_w = 1.0f - (d1 / tail_len);
        if (smooth_vad > 0.10f && d2 < tail_len * 0.75f) {
          float w2 = (1.0f - (d2 / (tail_len * 0.75f))) * smooth_vad;
          if (w2 > cyan_w) cyan_w = w2;
        }
      }

      if (cyan_w > 0.05f) {
        esphome::Color seg_c = (cyan_w > 0.74f)
                                   ? lerp_color(c_bar_cyan, c_bar_ice, (cyan_w - 0.74f) * 3.8f)
                                   : lerp_color(c_bar_blue, c_bar_cyan, cyan_w * 1.35f);

        float sx0 = DIAL_SIN[i], cy0 = -DIAL_COS[i];
        float sx1 = DIAL_SIN[next_i], cy1 = -DIAL_COS[next_i];
        int r_in_c = (base_ring_r - 1) - static_cast<int>(cyan_w * (4.0f + 5.0f * (smooth_vad + syl_env)));
        for (int r = r_in_c; r <= max_ring_r - 2; r += 2) {
          int x0 = cx + static_cast<int>(static_cast<float>(r) * sx0);
          int y0 = cy + static_cast<int>(static_cast<float>(r) * cy0);
          int x1 = cx + static_cast<int>(static_cast<float>(r) * sx1);
          int y1 = cy + static_cast<int>(static_cast<float>(r) * cy1);
          fast_line(it, x0, y0, x1, y1, seg_c);
          fast_line(it, x0, y0 + 1, x1, y1 + 1, seg_c);
        }
      }
    }
    return;
  }

  const float va_scale = static_cast<float>(SCREEN_MIN_DIM) / 466.0f;

  // ============================================================================
  // PROFILE: "DOT WAVE" — 3 WHITE DOTS GRACEFUL WAVE, SPINNING ORBIT & EQUALIZER
  // ============================================================================
  if (is_dot_wave) {
    const esphome::Color c_halo_outer(32, 58, 108);
    const esphome::Color c_halo_inner(110, 168, 255);
    const int cap_r = scale_min(12);

    static uint32_t last_g_ms = 0;
    static float g_phase = 0.0f;
    static float spin_blend = 0.0f;
    static uint32_t think_hold_until_ms = 0;

    float dt_g = (last_g_ms > 0 && now_ms >= last_g_ms && (now_ms - last_g_ms) <= 250U)
                     ? (static_cast<float>(now_ms - last_g_ms) * 0.001f)
                     : 0.045f;
    last_g_ms = now_ms;

    if (is_thinking) {
      think_hold_until_ms = now_ms + 950U;
    }
    const bool show_thinking_spin = is_thinking || (think_hold_until_ms > 0 && now_ms < think_hold_until_ms);

    float target_spin = show_thinking_spin ? 1.0f : 0.0f;
    spin_blend += (target_spin - spin_blend) * 0.28f;

    auto draw_glowing_capsule = [&](int bx, int by, int r, int half_h) {
      if (half_h <= 1) {
        fast_filled_circle(it, bx, by, r + 4, c_halo_outer);
        fast_filled_circle(it, bx, by, r + 2, c_halo_inner);
        fast_filled_circle(it, bx, by, r, col_white);
      } else {
        int y_top = by - half_h;
        int y_bot = by + half_h;
        int h_span = 2 * half_h;
        fast_filled_circle(it, bx, y_top, r + 3, c_halo_outer);
        fast_filled_circle(it, bx, y_bot, r + 3, c_halo_outer);
        fill_rect_row_major(it, bx - (r + 3), y_top, 2 * (r + 3) + 1, h_span, c_halo_outer);
        fast_filled_circle(it, bx, y_top, r + 1, c_halo_inner);
        fast_filled_circle(it, bx, y_bot, r + 1, c_halo_inner);
        fill_rect_row_major(it, bx - (r + 1), y_top, 2 * (r + 1) + 1, h_span, c_halo_inner);
        fast_filled_circle(it, bx, y_top, r, col_white);
        fast_filled_circle(it, bx, y_bot, r, col_white);
        fill_rect_row_major(it, bx - r, y_top, 2 * r + 1, h_span, col_white);
      }
    };

    if (show_thinking_spin || spin_blend > 0.15f) {
      g_phase = std::fmod(g_phase + dt_g * 4.2f, 200.0f * PI_F);
      const float orbit_r = 40.0f * va_scale * spin_blend;
      static constexpr float LINE_X[3] = {-50.0f, 0.0f, 50.0f};

      for (int d = 0; d < 3; d++) {
        float ang = g_phase + static_cast<float>(d) * (2.0f * PI_F / 3.0f);
        if (spin_blend > 0.65f) {
          for (int tail = 3; tail >= 1; tail--) {
            float t_ang = ang - static_cast<float>(tail) * 0.18f;
            float ts = 0.0f, tc = 1.0f;
            fast_sincos(t_ang, ts, tc);
            int tx = cx + static_cast<int>(orbit_r * tc);
            int ty = cy + static_cast<int>(orbit_r * ts);
            float fade = static_cast<float>(4 - tail) * 0.22f * spin_blend;
            fast_filled_circle(it, tx, ty, std::max(2, scale_min(11 - tail * 2)), scale_color(c_halo_inner, fade));
          }
        }
        float s = 0.0f, c = 1.0f;
        fast_sincos(ang, s, c);
        int dx = cx + static_cast<int>((1.0f - spin_blend) * LINE_X[d] * va_scale + orbit_r * c);
        int dy = cy + static_cast<int>(orbit_r * s);
        draw_glowing_capsule(dx, dy, cap_r, 0);
      }
      return;
    }

    const int dot_off_x[3] = {-scale_min(50), 0, scale_min(50)};

    if (is_speaking) {
      g_phase = std::fmod(g_phase + dt_g * 5.6f, 200.0f * PI_F);
      float syl_env = (0.50f + 0.30f * fast_sin(g_phase * 0.9f) + 0.20f * fast_cos(g_phase * 1.5f)) * smooth_speak;
      static constexpr float BAR_MULT[3] = {0.95f, 1.25f, 1.08f};
      static constexpr float BAR_PHASE[3] = {0.0f, 1.4f, 2.8f};
      static constexpr float BAR_MAX_H[3] = {22.0f, 36.0f, 22.0f};
      for (int d = 0; d < 3; d++) {
        float w = 0.50f + 0.50f * fast_sin(g_phase * BAR_MULT[d] + BAR_PHASE[d]);
        int half_h = static_cast<int>(w * BAR_MAX_H[d] * va_scale * (0.35f + 0.65f * syl_env));
        draw_glowing_capsule(cx + dot_off_x[d], cy, cap_r, half_h);
      }
      return;
    }

    float wave_spd = 2.9f + 1.3f * smooth_vad;
    float wave_amp = (14.0f + 12.0f * smooth_vad) * va_scale;
    g_phase = std::fmod(g_phase + dt_g * wave_spd, 200.0f * PI_F);

    for (int d = 0; d < 3; d++) {
      float ph = g_phase - static_cast<float>(d) * 2.094395f;
      float wave_y = 0.82f * fast_sin(ph) - 0.28f * fast_cos(2.0f * ph);
      int dy = static_cast<int>(wave_y * wave_amp);
      draw_glowing_capsule(cx + dot_off_x[d], cy + dy, cap_r, 0);
    }
    return;
  }

  // ============================================================================
  // PROFILE: "RINGS" — ELECTRIC CYAN COUNTER-ROTATING CONCENTRIC TECH HUD
  // ============================================================================
  if (is_rings) {
    const esphome::Color c_cyan_bright(28, 214, 255);
    const esphome::Color c_cyan_ice(195, 248, 255);
    const esphome::Color c_cyan_mid(16, 128, 165);
    const esphome::Color c_cyan_dim(8, 62, 84);
    const esphome::Color c_cyan_glow(5, 34, 48);

    static uint32_t last_j_ms = 0;
    static float j_angle = 0.0f;
    float dt_j = (last_j_ms > 0 && now_ms >= last_j_ms && (now_ms - last_j_ms) <= 250U)
                     ? (static_cast<float>(now_ms - last_j_ms) * 0.001f)
                     : 0.045f;
    last_j_ms = now_ms;

    float t_sec = static_cast<float>(now_ms % 60000U) * 0.001f;
    float syl_env = is_speaking
                        ? ((0.50f + 0.30f * fast_sin(t_sec * 10.5f) + 0.20f * fast_cos(t_sec * 16.8f)) * smooth_speak)
                        : 0.0f;
    float energy = std::fmin(1.0f, smooth_vad * 0.85f + syl_env);

    float rot_speed = is_thinking ? 1.85f : (is_speaking ? 0.85f : (0.48f + 0.45f * smooth_vad));
    j_angle = std::fmod(j_angle + dt_j * rot_speed, 2.0f * PI_F);

    auto draw_hud_arc = [&](float r_in, float r_out, float ang_start, float ang_span,
                            esphome::Color col, int step_deg = 3) {
      int steps = std::max(2, static_cast<int>((ang_span * (180.0f / PI_F)) / static_cast<float>(step_deg)));
      float d_ang = ang_span / static_cast<float>(steps);
      for (int s = 0; s < steps; s++) {
        float a0 = ang_start + static_cast<float>(s) * d_ang;
        float a1 = a0 + d_ang;
        float s0, c0, s1, c1;
        fast_sincos(a0, s0, c0);
        fast_sincos(a1, s1, c1);
        for (float r = r_in * va_scale; r <= r_out * va_scale; r += 1.5f) {
          int x0 = cx + static_cast<int>(r * s0);
          int y0 = cy - static_cast<int>(r * c0);
          int x1 = cx + static_cast<int>(r * s1);
          int y1 = cy - static_cast<int>(r * c1);
          fast_line(it, x0, y0, x1, y1, col);
        }
      }
    };

    float a_cw1 = j_angle;
    fast_circle(it, cx, cy, static_cast<int>(198.0f * va_scale), c_cyan_glow);
    fast_circle(it, cx, cy, static_cast<int>(188.0f * va_scale), c_cyan_mid);
    fast_circle(it, cx, cy, static_cast<int>(182.0f * va_scale), c_cyan_dim);

    draw_hud_arc(189.0f, 195.0f, a_cw1 - 0.18f, 0.36f, c_cyan_bright);
    draw_hud_arc(189.0f, 194.0f, a_cw1 + 1.75f, 1.05f, c_cyan_bright);
    draw_hud_arc(189.0f, 195.0f, a_cw1 + 2.92f, 0.42f, c_cyan_ice);
    draw_hud_arc(189.0f, 194.0f, a_cw1 + 5.25f, 0.32f, c_cyan_bright);
    for (int n = 0; n < 3; n++) {
      draw_hud_arc(189.0f, 194.0f, a_cw1 + 0.52f + static_cast<float>(n) * 0.13f, 0.08f, c_cyan_bright, 2);
    }
    draw_hud_arc(173.0f, 177.0f, a_cw1 + 3.45f, 0.85f, c_cyan_bright);
    for (int i = 0; i < 60; i += 2) {
      if ((i >= 8 && i <= 22) || (i >= 34 && i <= 48)) {
        float da = a_cw1 + static_cast<float>(i) * (2.0f * PI_F / 60.0f);
        float ds, dc;
        fast_sincos(da, ds, dc);
        int dx = cx + static_cast<int>(175.0f * va_scale * ds);
        int dy = cy - static_cast<int>(175.0f * va_scale * dc);
        fast_filled_circle(it, dx, dy, (i % 6 == 0) ? 2 : 1, c_cyan_mid);
      }
    }

    float a_ccw2 = -j_angle * 1.15f;
    fast_circle(it, cx, cy, scale_min(160), c_cyan_dim);
    fast_circle(it, cx, cy, scale_min(134), c_cyan_dim);

    draw_hud_arc(155.0f, 160.0f, a_ccw2 - 0.35f, 0.55f, c_cyan_bright);
    draw_hud_arc(146.0f, 148.0f, a_ccw2 - 0.65f, 2.25f, c_cyan_mid);

    for (int b = 0; b < 14; b++) {
      float ba = a_ccw2 - 0.10f + static_cast<float>(b) * 0.125f;
      float bs, bc;
      fast_sincos(ba, bs, bc);
      float px = bc * va_scale, py = bs * va_scale;
      float r_in_b = 120.0f * va_scale;
      float r_out_b = (129.0f + energy * 6.0f * (0.5f + 0.5f * DIAL_SIN[(b * 5 + static_cast<int>(now_ms / 40U)) % 60])) * va_scale;
      int x0 = cx + static_cast<int>(r_in_b * bs - 2.5f * px);
      int y0 = cy - static_cast<int>(r_in_b * bc - 2.5f * py);
      int x1 = cx + static_cast<int>(r_out_b * bs - 2.5f * px);
      int y1 = cy - static_cast<int>(r_out_b * bc - 2.5f * py);
      int x2 = cx + static_cast<int>(r_out_b * bs + 2.5f * px);
      int y2 = cy - static_cast<int>(r_out_b * bc + 2.5f * py);
      int x3 = cx + static_cast<int>(r_in_b * bs + 2.5f * px);
      int y3 = cy - static_cast<int>(r_in_b * bc + 2.5f * py);

      bool active_cell = is_thinking ? (((static_cast<int>(now_ms / 45U) - b) & 3) == 0)
                                     : (energy > 0.15f && (b % 2 == 0));
      esphome::Color cell_c = active_cell ? c_cyan_ice : c_cyan_bright;
      fast_line(it, x0, y0, x1, y1, cell_c);
      fast_line(it, x1, y1, x2, y2, cell_c);
      fast_line(it, x2, y2, x3, y3, cell_c);
      fast_line(it, x3, y3, x0, y0, cell_c);
    }

    for (int d = 0; d < 12; d++) {
      float da = a_ccw2 + 0.55f + static_cast<float>(d) * 0.13f;
      float ds, dc;
      fast_sincos(da, ds, dc);
      int dot_r = (d >= 3 && d <= 8) ? 3 : 2;
      fast_filled_circle(it, cx + static_cast<int>(140.0f * va_scale * ds), cy - static_cast<int>(140.0f * va_scale * dc),
                         dot_r, (d % 3 == 0 && energy > 0.2f) ? c_cyan_ice : c_cyan_bright);
      if (d >= 4 && d <= 9) {
        fast_filled_circle(it, cx + static_cast<int>(151.0f * va_scale * ds), cy - static_cast<int>(151.0f * va_scale * dc),
                           2, c_cyan_mid);
      }
    }

    for (int k = 0; k < 13; k++) {
      float ka = a_ccw2 + 2.85f + static_cast<float>(k) * 0.095f;
      float ks, kc;
      fast_sincos(ka, ks, kc);
      fast_line(it, cx + static_cast<int>(122.0f * va_scale * ks), cy - static_cast<int>(122.0f * va_scale * kc),
                cx + static_cast<int>(132.0f * va_scale * ks), cy - static_cast<int>(132.0f * va_scale * kc), c_cyan_bright);
    }

    float a_cw3 = j_angle * 0.85f;
    fast_circle(it, cx, cy, scale_min(102), c_cyan_mid);

    for (int i = 0; i < 60; i++) {
      float ta = a_cw3 + static_cast<float>(i) * (2.0f * PI_F / 60.0f);
      float ts, tc;
      fast_sincos(ta, ts, tc);
      float r0 = 102.0f * va_scale;
      float r1 = ((i % 5 == 0) ? 108.0f : 105.0f) * va_scale;
      fast_line(it, cx + static_cast<int>(r0 * ts), cy - static_cast<int>(r0 * tc),
                cx + static_cast<int>(r1 * ts), cy - static_cast<int>(r1 * tc),
                (i % 5 == 0) ? c_cyan_bright : c_cyan_dim);
    }
    draw_hud_arc(97.0f, 101.0f, a_cw3 + 0.85f, 1.10f, c_cyan_bright);
    draw_hud_arc(97.0f, 101.0f, a_cw3 + 3.90f, 0.95f, c_cyan_bright);

    int core_pulse = static_cast<int>(energy * 5.0f);
    fast_circle(it, cx, cy, scale_min(82), c_cyan_mid);
    fast_circle(it, cx, cy, scale_min(74), c_cyan_bright);
    fast_circle(it, cx, cy, scale_min(73), c_cyan_mid);
    if (energy > 0.08f || is_thinking) {
      int inner_r = scale_min(64 - core_pulse);
      fast_circle(it, cx, cy, inner_r, scale_color(c_cyan_bright, 0.45f + 0.55f * energy));
    }

    draw_hud_arc(74.0f, 79.0f, 0.35f, 1.35f, (energy > 0.3f) ? c_cyan_ice : c_cyan_bright);
    draw_hud_arc(74.0f, 79.0f, 2.35f, 1.15f, (energy > 0.3f) ? c_cyan_ice : c_cyan_bright);
    draw_hud_arc(74.0f, 79.0f, 4.25f, 1.25f, c_cyan_bright);

    const int r_ret_out = scale_min(88);
    const int r_ret_mid = scale_min(82);
    const int r_ret_in = scale_min(68);
    const int ret_tick = scale_min(4);
    fast_line(it, cx, cy - r_ret_out, cx, cy - r_ret_in, c_cyan_ice);
    fast_line(it, cx - ret_tick, cy - r_ret_mid, cx + ret_tick, cy - r_ret_mid, c_cyan_bright);

    fast_line(it, cx, cy + r_ret_in, cx, cy + r_ret_out, c_cyan_ice);
    fast_line(it, cx - ret_tick, cy + r_ret_mid, cx + ret_tick, cy + r_ret_mid, c_cyan_bright);

    fast_line(it, cx - r_ret_out, cy, cx - r_ret_in, cy, c_cyan_ice);
    fast_line(it, cx - r_ret_mid, cy - ret_tick, cx - r_ret_mid, cy + ret_tick, c_cyan_bright);

    fast_line(it, cx + r_ret_in, cy, cx + r_ret_out, cy, c_cyan_ice);
    fast_line(it, cx + r_ret_mid, cy - ret_tick, cx + r_ret_mid, cy + ret_tick, c_cyan_bright);
    return;
  }

  // ============================================================================
  // PROFILE: "NEBULA" — HYBRID 3D VOLUMETRIC SILK RIBBONS, RADIATION WAVES & ORB
  // ============================================================================
  if (!is_solaris) {
    const esphome::Color c_elec_cyan(0, 229, 255);
    const esphome::Color c_deep_azure(28, 110, 225);
    const esphome::Color c_royal_purple(124, 42, 208);
    const esphome::Color c_amethyst(168, 72, 242);
    const esphome::Color c_neon_magenta(255, 45, 155);
    const esphome::Color c_pink_hi(255, 130, 205);
    const esphome::Color c_pink_spec(255, 185, 232);

    float t_sec = static_cast<float>(now_ms % 60000U) * 0.001f;

    auto render_nebula_purple_orb = [&](int core_r, float energy_boost) {
      const esphome::Color c_violet_edge(68, 20, 130);
      const esphome::Color c_royal_purp(126, 36, 212);
      const esphome::Color c_amethyst_mid(176, 52, 234);
      const esphome::Color c_magenta_rose(234, 58, 174);

      float tide = 0.50f + 0.50f * fast_sin(t_sec * 1.50f);
      float shift = std::fmin(1.0f, tide * 0.65f + energy_boost * 0.35f);

      esphome::Color outer_col = lerp_color(c_violet_edge, c_royal_purp, shift * 0.85f);
      esphome::Color mid_col = lerp_color(c_royal_purp, c_amethyst_mid, 0.35f + 0.65f * shift);
      esphome::Color inner_col = lerp_color(c_amethyst_mid, c_magenta_rose, 0.25f + 0.65f * shift);

      for (int b = 6; b >= 1; b--) {
        float k = static_cast<float>(7 - b) * (1.0f / 7.0f);
        fast_circle(it, cx, cy, core_r + b, scale_color(outer_col, k * k * 0.76f));
      }

      float inv_r = 1.0f / static_cast<float>(core_r > 1 ? core_r : 1);
      for (int r = core_r; r >= 1; r -= 2) {
        float u = 1.0f - static_cast<float>(r) * inv_r;
        float ripple = 0.14f * fast_sin(u * 4.2f - t_sec * 2.3f);
        float s = std::fmax(0.0f, std::fmin(1.0f, u * u * (3.0f - 2.0f * u) + ripple));

        esphome::Color ring_c = (s < 0.55f)
                                    ? lerp_color(outer_col, mid_col, s * (1.0f / 0.55f))
                                    : lerp_color(mid_col, inner_col, (s - 0.55f) * (1.0f / 0.45f));
        fast_filled_circle(it, cx, cy, r, ring_c);
      }
    };

    auto draw_silk_ribbon_3d = [&](float base_r, float a2, float a3, float a5,
                                   int ph2, int ph3, int ph5,
                                   float rx, float ry, float rz,
                                   esphome::Color col_core, esphome::Color col_aura,
                                   bool with_outer_bloom) {
      const Mat3 rot = Mat3::from_euler(rx, ry, rz);
      int prev_x = 0, prev_y = 0, first_x = 0, first_y = 0;
      int prev_ix = 0, prev_iy = 0, first_ix = 0, first_iy = 0;
      int prev_ox = 0, prev_oy = 0, first_ox = 0, first_oy = 0;
      float prev_z = 0.0f;

      for (int i = 0; i < 60; i += 2) {
        float w2 = DIAL_SIN[(i * 2 + ph2) % 60];
        float w3 = DIAL_COS[(i * 3 + ph3) % 60];
        float w5 = DIAL_SIN[(i * 5 + ph5) % 60];
        float r = base_r + a2 * w2 + a3 * w3;
        float z_local = a5 * w5 * 3.2f;

        float ux = DIAL_SIN[i];
        float uy = -DIAL_COS[i];
        Vec3 v_mid = rot.transform({r * ux, r * uy, z_local});
        Vec3 v_in = rot.transform({(r - 2.2f) * ux, (r - 2.2f) * uy, z_local});
        Vec3 v_out = rot.transform({(r + 2.2f) * ux, (r + 2.2f) * uy, z_local});

        ProjectedPoint p = project_3d(v_mid, cx, cy, 260.0f, 320.0f);
        ProjectedPoint pi = project_3d(v_in, cx, cy, 260.0f, 320.0f);
        ProjectedPoint po = project_3d(v_out, cx, cy, 260.0f, 320.0f);

        if (i == 0) {
          first_x = p.x; first_y = p.y;
          first_ix = pi.x; first_iy = pi.y;
          first_ox = po.x; first_oy = po.y;
        } else {
          float z_avg = 0.5f * (v_mid.z + prev_z);
          float depth_k = std::fmax(0.72f, std::fmin(1.0f, 0.92f - (z_avg / (base_r + 15.0f)) * 0.22f));
          esphome::Color c_main = scale_color(col_core, depth_k);
          esphome::Color c_halo = scale_color(col_aura, depth_k * 0.78f);

          if (with_outer_bloom) {
            fast_line(it, prev_ix, prev_iy, pi.x, pi.y, c_halo);
            fast_line(it, prev_ox, prev_oy, po.x, po.y, c_halo);
          }
          draw_glow_line(it, prev_x, prev_y, p.x, p.y, c_main, c_halo);
        }
        prev_x = p.x; prev_y = p.y;
        prev_ix = pi.x; prev_iy = pi.y;
        prev_ox = po.x; prev_oy = po.y;
        prev_z = v_mid.z;
      }
      if (with_outer_bloom) {
        fast_line(it, prev_ix, prev_iy, first_ix, first_iy, scale_color(col_aura, 0.75f));
        fast_line(it, prev_ox, prev_oy, first_ox, first_oy, scale_color(col_aura, 0.75f));
      }
      draw_glow_line(it, prev_x, prev_y, first_x, first_y, col_core, scale_color(col_aura, 0.80f));
    };

    if (is_speaking) {
      float syl_env = (0.55f + 0.25f * fast_sin(t_sec * 11.5f) + 0.20f * fast_cos(t_sec * 19.3f)) * smooth_speak;
      if (syl_env < 0.18f) syl_env = 0.18f;

      uint32_t speak_elapsed = now_ms - speak_start_ms;

      for (int w = 0; w < 5; w++) {
        uint32_t launch_offset = static_cast<uint32_t>(w) * 380U;
        if (speak_elapsed < launch_offset) continue;
        uint32_t wave_t = (speak_elapsed - launch_offset) % 1900U;
        float progress = static_cast<float>(wave_t) * (1.0f / 1900.0f);
        int r_center = static_cast<int>((38.0f + progress * 180.0f) * va_scale);

        float spread = progress * (1.15f - 0.25f * progress);
        int base_w = (w % 2 == 0) ? 4 : 3;
        int band_w = base_w + static_cast<int>((13.0f * spread) * (0.80f + 0.20f * syl_env));
        if (band_w < 3) band_w = 3;
        int half_w = band_w >> 1;

        float wave_color_phase = static_cast<float>(w) * 0.20f + progress * 0.34f +
                                 static_cast<float>(now_ms % 4000U) * 0.00025f;
        float radial_env = (1.0f - progress * 0.62f) * smooth_speak;
        float inv_half = 1.0f / static_cast<float>(half_w + 1);

        for (int d = -half_w; d <= half_w; d += 2) {
          int r_curr = r_center + d;
          if (r_curr < scale_min(34) || r_curr > scale_min(226)) continue;

          float edge_u = static_cast<float>(d < 0 ? -d : d) * inv_half;
          float bell = (1.0f - edge_u * edge_u);
          float diff_profile = (progress > 0.35f) ? (bell * bell) : bell;
          float pix_fade = radial_env * (0.22f + 0.78f * diff_profile);

          esphome::Color base_col = get_va_radiation_color(false, wave_color_phase + static_cast<float>(d) * 0.016f);
          esphome::Color stroke_col = (edge_u < 0.28f)
                                          ? lerp_color(scale_color(base_col, pix_fade), scale_color(c_pink_hi, pix_fade),
                                                       0.32f * (1.0f - progress * 0.5f))
                                          : scale_color(base_col, pix_fade);
          fast_circle(it, cx, cy, r_curr, stroke_col);
        }
      }

      int core_r = scale_min(25 + static_cast<int>(5.0f * syl_env));
      render_nebula_purple_orb(core_r, syl_env);
      return;
    }

    if (is_thinking) {
      int tick = static_cast<int>(now_ms / 22U);
      float pulse = fast_sin(static_cast<float>(now_ms % 650U) * (2.0f * PI_F / 650.0f));

      fast_circle(it, cx, cy, scale_min(74), esphome::Color(28, 18, 46));
      fast_circle(it, cx, cy, scale_min(118), esphome::Color(24, 16, 40));

      float vortex_r = 68.0f + 3.0f * pulse;
      draw_silk_ribbon_3d(vortex_r + 3.0f, 6.0f, 5.0f, 3.5f,
                          (tick * 2) % 60, (tick * 3) % 60, tick % 60,
                          0.68f * fast_sin(t_sec * 1.3f), 0.55f * fast_cos(t_sec * 1.1f), t_sec * 0.8f,
                          c_amethyst, c_royal_purple, true);
      draw_silk_ribbon_3d(vortex_r, -5.0f, 6.0f, -3.0f,
                          (tick * 2 + 30) % 60, (tick + 15) % 60, (tick * 2) % 60,
                          -0.62f * fast_cos(t_sec * 1.2f), 0.65f * fast_sin(t_sec * 1.4f), -t_sec * 0.7f,
                          c_pink_hi, c_neon_magenta, true);
      draw_silk_ribbon_3d(vortex_r - 3.0f, 4.0f, -4.0f, 2.5f,
                          (tick + 45) % 60, (tick * 2 + 20) % 60, (tick * 3) % 60,
                          0.48f * fast_cos(t_sec * 1.6f), -0.52f * fast_sin(t_sec * 1.2f), t_sec * 0.9f,
                          c_elec_cyan, c_deep_azure, false);

      float spin_cw = static_cast<float>(now_ms % 1150U) * (2.0f * PI_F / 1150.0f);
      float orb_rx1 = 0.36f * fast_sin(t_sec * 0.9f), orb_ry1 = 0.32f * fast_cos(t_sec * 0.8f);
      float orb_rx2 = -0.34f * fast_cos(t_sec * 0.85f), orb_ry2 = 0.36f * fast_sin(t_sec * 0.95f);
      const Mat3 rot_cw = Mat3::from_euler(orb_rx1, orb_ry1, 0.0f);
      const Mat3 rot_ccw = Mat3::from_euler(orb_rx2, orb_ry2, 0.0f);

      for (int i = 0; i < 4; i++) {
        float ang_cw = spin_cw + static_cast<float>(i) * (PI_F * 0.5f);
        float ang_ccw = -spin_cw + (PI_F * 0.25f) + static_cast<float>(i) * (PI_F * 0.5f);

        for (int tail = 4; tail >= 0; tail--) {
          float ta = ang_cw - static_cast<float>(tail) * 0.12f;
          float ts, tc;
          fast_sincos(ta, ts, tc);
          Vec3 v_cw = rot_cw.transform({74.0f * tc, 74.0f * ts, 0.0f});
          ProjectedPoint p_cw = project_3d(v_cw, cx, cy, 260.0f, 320.0f);
          if (tail == 0) {
            fast_line(it, p_cw.x - 5, p_cw.y, p_cw.x + 5, p_cw.y, c_elec_cyan);
            fast_line(it, p_cw.x, p_cw.y - 5, p_cw.x, p_cw.y + 5, c_elec_cyan);
            fast_filled_circle(it, p_cw.x, p_cw.y, 3, c_elec_cyan);
            fast_filled_circle(it, p_cw.x, p_cw.y, 1, c_pink_spec);
          } else {
            float k = static_cast<float>(5 - tail) * 0.18f;
            fast_filled_circle(it, p_cw.x, p_cw.y, (tail <= 2) ? 2 : 1, scale_color(c_elec_cyan, k));
          }
        }

        for (int tail = 4; tail >= 0; tail--) {
          float ta = ang_ccw + static_cast<float>(tail) * 0.12f;
          float ts, tc;
          fast_sincos(ta, ts, tc);
          Vec3 v_ccw = rot_ccw.transform({118.0f * tc, 118.0f * ts, 0.0f});
          ProjectedPoint p_ccw = project_3d(v_ccw, cx, cy, 260.0f, 320.0f);
          if (tail == 0) {
            fast_line(it, p_ccw.x - 6, p_ccw.y, p_ccw.x + 6, p_ccw.y, c_amethyst);
            fast_line(it, p_ccw.x, p_ccw.y - 6, p_ccw.x, p_ccw.y + 6, c_amethyst);
            fast_filled_circle(it, p_ccw.x, p_ccw.y, 4, c_neon_magenta);
            fast_filled_circle(it, p_ccw.x, p_ccw.y, 2, c_pink_hi);
          } else {
            float k = static_cast<float>(5 - tail) * 0.18f;
            fast_filled_circle(it, p_ccw.x, p_ccw.y, (tail <= 2) ? 2 : 1, scale_color(c_neon_magenta, k));
          }
        }
      }

      int core_r = scale_min(25 + static_cast<int>(4.0f * pulse));
      render_nebula_purple_orb(core_r, 0.75f);
      return;
    }

    float breath = fast_sin(static_cast<float>(now_ms % 2000U) * (2.0f * PI_F / 2000.0f));
    int tick = static_cast<int>(now_ms / 38U);

    float base_r = 82.0f + 5.0f * breath + 18.0f * smooth_vad;
    float a2 = 5.5f + 10.0f * smooth_vad;
    float a3 = 4.2f + 8.0f * smooth_vad;
    float a5 = 2.8f + 5.5f * smooth_vad;

    draw_silk_ribbon_3d(base_r + 5.0f, a2, a3, a5, tick % 60, (tick + 20) % 60, (tick + 40) % 60,
                        0.48f * fast_sin(t_sec * 0.65f), 0.42f * fast_cos(t_sec * 0.55f), t_sec * 0.28f,
                        c_amethyst, c_royal_purple, true);
    draw_silk_ribbon_3d(base_r, -a2, a3 * 0.9f, -a5, (tick + 15) % 60, (tick * 2) % 60, (tick + 10) % 60,
                        -0.50f * fast_cos(t_sec * 0.60f), 0.46f * fast_sin(t_sec * 0.75f), -t_sec * 0.24f,
                        c_pink_hi, c_neon_magenta, true);
    draw_silk_ribbon_3d(base_r - 4.0f, a2 * 0.8f, -a3, a5, (tick + 35) % 60, (tick + 12) % 60, (tick * 2) % 60,
                        0.38f * fast_cos(t_sec * 0.80f), -0.42f * fast_sin(t_sec * 0.62f), t_sec * 0.32f,
                        c_elec_cyan, c_deep_azure, true);
    draw_silk_ribbon_3d(base_r - 8.0f, a2 * 0.45f, a3 * 0.45f, a5 * 0.5f, (tick + 50) % 60, (tick + 30) % 60, tick % 60,
                        0.24f * fast_sin(t_sec * 0.95f), 0.24f * fast_cos(t_sec * 0.95f), -t_sec * 0.18f,
                        c_pink_spec, c_neon_magenta, false);

    int core_r = scale_min(25 + static_cast<int>(4.0f * breath + 7.0f * smooth_vad));
    render_nebula_purple_orb(core_r, smooth_vad);
    return;
  }

  // ============================================================================
  // PROFILE: "SOLARIS" — RADIAL ACOUSTIC CORONA & 3D GIMBALED SOLAR ASTROLABE
  // ============================================================================
  const esphome::Color c_gold = col_solaris_gold;
  const esphome::Color c_amber(255, 105, 15);
  const esphome::Color c_crimson_ember(185, 35, 10);
  const esphome::Color c_emerald(35, 215, 115);
  const esphome::Color c_warm_white(255, 248, 215);

  float t_sec = static_cast<float>(now_ms % 60000U) * 0.001f;

  if (is_speaking) {
    float syl_env = (0.55f + 0.25f * fast_sin(t_sec * 11.5f) + 0.20f * fast_cos(t_sec * 19.3f)) * smooth_speak;
    if (syl_env < 0.15f) syl_env = 0.15f;
    float breath = fast_sin(static_cast<float>(now_ms % 1800U) * (2.0f * PI_F / 1800.0f));
    int tick = static_cast<int>(now_ms / 35U);
    uint32_t speak_elapsed = now_ms - speak_start_ms;

    for (int w = 0; w < 3; w++) {
      uint32_t launch_offset = static_cast<uint32_t>(w) * 600U;
      if (speak_elapsed < launch_offset) continue;
      uint32_t wave_t = (speak_elapsed - launch_offset) % 1800U;
      float prog = static_cast<float>(wave_t) * (1.0f / 1800.0f);
      float fade = fast_sin(prog * PI_F) * (0.45f + 0.45f * syl_env) * smooth_speak;
      int r_wave = scale_min(82 + static_cast<int>(prog * 132.0f));
      esphome::Color ring_c = scale_color(get_va_radiation_color(true, prog + static_cast<float>(w) * 0.33f), fade);
      esphome::Color glow_c = scale_color(ring_c, 0.45f);
      fast_circle(it, cx, cy, r_wave - 1, glow_c);
      fast_circle(it, cx, cy, r_wave, ring_c);
      fast_circle(it, cx, cy, r_wave + 1, glow_c);
    }

    const float r_inner = (42.0f + 3.0f * breath) * va_scale;
    for (int i = 0; i < 60; i += 2) {
      float wave = DIAL_SIN[(i * 3 + tick) % 60] * 0.6f + DIAL_COS[(i * 2 + tick) % 60] * 0.4f;
      float bar_len = ((16.0f + 22.0f * syl_env) + (6.0f + 10.0f * syl_env) * wave) * va_scale;
      if (bar_len < 6.0f * va_scale) bar_len = 6.0f * va_scale;

      float ux = DIAL_SIN[i];
      float uy = -DIAL_COS[i];
      int x0 = cx + static_cast<int>(r_inner * ux);
      int y0 = cy + static_cast<int>(r_inner * uy);
      int xm = cx + static_cast<int>((r_inner + bar_len * 0.65f) * ux);
      int ym = cy + static_cast<int>((r_inner + bar_len * 0.65f) * uy);
      int x1 = cx + static_cast<int>((r_inner + bar_len) * ux);
      int y1 = cy + static_cast<int>((r_inner + bar_len) * uy);

      fast_line(it, x0, y0, xm, ym, c_amber);
      fast_line(it, xm, ym, x1, y1, (i % 4 == 0) ? c_emerald : c_gold);
    }

    int core_r = scale_min(27 + static_cast<int>(4.0f * breath + 5.0f * syl_env));
    fast_filled_circle(it, cx, cy, core_r + 5, c_crimson_ember);
    fast_filled_circle(it, cx, cy, core_r + 1, c_amber);
    fast_filled_circle(it, cx, cy, core_r - 3, c_gold);
    fast_circle(it, cx, cy, core_r - 5, c_warm_white);
    return;
  }

  if (is_thinking) {
    const int head_1 = static_cast<int>((now_ms / 18U) % 60U);
    const int head_2 = (60 - static_cast<int>((now_ms / 22U) % 60U)) % 60;
    const int head_3 = static_cast<int>((now_ms / 28U) % 60U);

    float rx1 = 0.62f * fast_sin(t_sec * 1.1f), ry1 = 0.52f * fast_cos(t_sec * 0.9f);
    float rx2 = -0.58f * fast_cos(t_sec * 0.95f), ry2 = 0.60f * fast_sin(t_sec * 1.15f);
    float rx3 = 0.42f * fast_cos(t_sec * 0.75f), ry3 = -0.48f * fast_sin(t_sec * 0.85f);
    const Mat3 rot1 = Mat3::from_euler(rx1, ry1, 0.0f);
    const Mat3 rot2 = Mat3::from_euler(rx2, ry2, 0.0f);
    const Mat3 rot3 = Mat3::from_euler(rx3, ry3, 0.0f);

    auto draw_3d_meridian = [&](float rad, const Mat3 &rot, esphome::Color col) {
      int px = 0, py = 0, fx = 0, fy = 0;
      for (int i = 0; i < 60; i += 3) {
        Vec3 v = rot.transform({rad * DIAL_SIN[i], -rad * DIAL_COS[i], 0.0f});
        ProjectedPoint p = project_3d(v, cx, cy, 260.0f, 320.0f);
        if (i == 0) {
          fx = p.x; fy = p.y;
        } else {
          fast_line(it, px, py, p.x, p.y, col);
        }
        px = p.x; py = p.y;
      }
      fast_line(it, px, py, fx, fy, col);
    };

    draw_3d_meridian(66.0f, rot1, scale_color(c_amber, 0.34f * smooth_think));
    draw_3d_meridian(94.0f, rot2, scale_color(c_gold, 0.30f * smooth_think));
    draw_3d_meridian(122.0f, rot3, scale_color(c_emerald, 0.30f * smooth_think));

    for (int n = 0; n < 3; n++) {
      int b1 = (head_1 + n * 20) % 60;
      for (int tail = 5; tail >= 0; tail--) {
        int idx = (b1 - tail * 2 + 60) % 60;
        Vec3 v = rot1.transform({66.0f * DIAL_SIN[idx], -66.0f * DIAL_COS[idx], 0.0f});
        ProjectedPoint p = project_3d(v, cx, cy, 260.0f, 320.0f);
        fast_filled_circle(it, p.x, p.y, (tail == 0) ? 4 : (tail <= 2 ? 2 : 1),
                           (tail == 0) ? scale_color(c_warm_white, smooth_think)
                                       : scale_color(c_gold, (6 - tail) * 0.16f * smooth_think));
      }
      int b2 = (head_2 + n * 20 + 10) % 60;
      for (int tail = 5; tail >= 0; tail--) {
        int idx = (b2 + tail * 2) % 60;
        Vec3 v = rot2.transform({94.0f * DIAL_SIN[idx], -94.0f * DIAL_COS[idx], 0.0f});
        ProjectedPoint p = project_3d(v, cx, cy, 260.0f, 320.0f);
        fast_filled_circle(it, p.x, p.y, (tail == 0) ? 4 : (tail <= 2 ? 2 : 1),
                           (tail == 0) ? scale_color(c_warm_white, smooth_think)
                                       : scale_color(c_emerald, (6 - tail) * 0.16f * smooth_think));
      }
      int b3 = (head_3 + n * 20 + 5) % 60;
      for (int tail = 4; tail >= 0; tail--) {
        int idx = (b3 - tail * 2 + 60) % 60;
        Vec3 v = rot3.transform({122.0f * DIAL_SIN[idx], -122.0f * DIAL_COS[idx], 0.0f});
        ProjectedPoint p = project_3d(v, cx, cy, 260.0f, 320.0f);
        fast_filled_circle(it, p.x, p.y, (tail == 0) ? 3 : 2,
                           (tail == 0) ? scale_color(c_gold, smooth_think)
                                       : scale_color(c_amber, (5 - tail) * 0.18f * smooth_think));
      }
    }

    float pulse = fast_sin(static_cast<float>(now_ms % 600U) * (2.0f * PI_F / 600.0f));
    int core_r = scale_min(25 + static_cast<int>(4.0f * pulse));
    fast_filled_circle(it, cx, cy, core_r + 5, c_crimson_ember);
    fast_filled_circle(it, cx, cy, core_r + 1, c_amber);
    fast_filled_circle(it, cx, cy, core_r - 3, c_gold);
    fast_circle(it, cx, cy, core_r - 5, c_warm_white);
    return;
  }

  float breath = fast_sin(static_cast<float>(now_ms % 1800U) * (2.0f * PI_F / 1800.0f));
  int tick = static_cast<int>(now_ms / 45U);

  const float r_inner = (42.0f + 3.0f * breath) * va_scale;
  for (int i = 0; i < 60; i += 2) {
    float wave = DIAL_SIN[(i * 3 + tick) % 60] * 0.6f + DIAL_COS[(i * 2 + tick) % 60] * 0.4f;
    float bar_len = ((16.0f + 6.0f * breath + 24.0f * smooth_vad) + (6.0f + 10.0f * smooth_vad) * wave) * va_scale;
    if (bar_len < 6.0f * va_scale) bar_len = 6.0f * va_scale;

    float ux = DIAL_SIN[i];
    float uy = -DIAL_COS[i];
    int x0 = cx + static_cast<int>(r_inner * ux);
    int y0 = cy + static_cast<int>(r_inner * uy);
    int xm = cx + static_cast<int>((r_inner + bar_len * 0.65f) * ux);
    int ym = cy + static_cast<int>((r_inner + bar_len * 0.65f) * uy);
    int x1 = cx + static_cast<int>((r_inner + bar_len) * ux);
    int y1 = cy + static_cast<int>((r_inner + bar_len) * uy);

    fast_line(it, x0, y0, xm, ym, c_amber);
    fast_line(it, xm, ym, x1, y1, (smooth_vad > 0.25f && (i % 4 == 0)) ? c_emerald : c_gold);
  }

  int core_r = scale_min(27 + static_cast<int>(4.0f * breath + 7.0f * smooth_vad));
  fast_filled_circle(it, cx, cy, core_r + 5, c_crimson_ember);
  fast_filled_circle(it, cx, cy, core_r + 1, c_amber);
  fast_filled_circle(it, cx, cy, core_r - 3, c_gold);
  fast_circle(it, cx, cy, core_r - 5, c_warm_white);
}

void render_notification_overlay(esphome::display::Display &it, const UiFrameContext &ctx,
                                 const SystemTakeoverState &st) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const int cx = st.cx;
  const int cy = st.cy;
  const uint32_t now_ms = st.now_ms;
  const bool is_crit = (st.notif_priority == "critical");
  const bool is_warn = (st.notif_priority == "warning");
  const bool pulse = ((now_ms / 250U) % 2U) != 0;
  Color accent_c = is_crit ? st.col_red : (is_warn ? st.col_gold : st.theme_primary);

  const bool has_cam_url = !st.notif_camera_url.empty();
  const bool cam_ready = (has_cam_url && st.notif_camera_url == st.current_loaded_camera_url &&
                          ctx.camera_snapshot_art.valid());

  if (cam_ready) {
    render_album_art_canvas(it, ctx.camera_snapshot_art, cx, false, false, false, true);
  }

  Color ring_c = (is_crit && !pulse) ? ui::chrome_danger_bg() : accent_c;
  render_bezel_alert_border(it, cx, cy, ring_c);

  const int wrap_max_w = scale_x(280);
  std::string line1;
  std::string line2;
  std::string clean_msg = sanitize_utf8_for_font(st.notif_message);
  if (!clean_msg.empty() && ctx.font_small != nullptr) {
    int w = 0, xo = 0, b = 0, th = 0;
    ctx.font_small->measure(clean_msg.c_str(), &w, &xo, &b, &th);
    if (w <= wrap_max_w) {
      line1 = clean_msg;
    } else {
      size_t split_pos = std::string::npos;
      size_t space = 0;
      while ((space = clean_msg.find(' ', space)) != std::string::npos) {
        std::string sub = clean_msg.substr(0, space);
        ctx.font_small->measure(sub.c_str(), &w, &xo, &b, &th);
        if (w <= wrap_max_w) {
          split_pos = space;
          space++;
        } else {
          break;
        }
      }
      if (split_pos != std::string::npos) {
        line1 = clean_msg.substr(0, split_pos);
        line2 = fit_text_to_width_font(clean_msg.substr(split_pos + 1), wrap_max_w, ctx.font_small);
      } else {
        line1 = fit_text_to_width_font(clean_msg, wrap_max_w, ctx.font_small);
      }
    }
  }

  std::string fit_title = fit_text_to_width_font(
      st.notif_title.empty() ? "Notification" : st.notif_title, wrap_max_w, ctx.font_date);

  if (cam_ready) {
    fast_print(it, cx + 1, scale_y(52) + 1, ctx.font_date, st.col_black, TextAlign::CENTER, fit_title.c_str());
    fast_print(it, cx, scale_y(52), ctx.font_date, st.col_white, TextAlign::CENTER, fit_title.c_str());

    if (!line1.empty()) {
      int y1 = line2.empty() ? scale_y(356) : scale_y(344);
      fast_print(it, cx + 1, y1 + 1, ctx.font_small, st.col_black, TextAlign::CENTER, line1.c_str());
      fast_print(it, cx, y1, ctx.font_small, st.col_white, TextAlign::CENTER, line1.c_str());
    }
    if (!line2.empty()) {
      int y2 = scale_y(368);
      fast_print(it, cx + 1, y2 + 1, ctx.font_small, st.col_black, TextAlign::CENTER, line2.c_str());
      fast_print(it, cx, y2, ctx.font_small, st.theme_secondary, TextAlign::CENTER, line2.c_str());
    }

    const Color answer_bg = scale_color(ctx.col_green, 0.38f);
    render_modern_card(it, cx - scale_x(72), scale_y(412), 132, 36, 10, answer_bg, ctx.col_green,
                       "Answer", st.col_white, ctx.font_small);
    render_modern_card(it, cx + scale_x(72), scale_y(412), 132, 36, 10, ui::chrome_card_bg_on(), accent_c,
                       "Dismiss", st.col_white, ctx.font_small);

    frame_hits().add_rect(0, 0, SCREEN_W, scale_y(380), WidgetId::NOTIF_OPEN_CAM_BTN, kHitTap);
    frame_hits().add_centered_rect(cx - scale_x(72), scale_y(412), scale_x(132), scale_y(36),
                                   WidgetId::NOTIF_OPEN_CAM_BTN, kHitTap | kHitFocusSlot);
    frame_hits().add_centered_rect(cx + scale_x(72), scale_y(412), scale_x(132), scale_y(36),
                                   WidgetId::NOTIF_DISMISS_BTN, kHitTap | kHitFocusSlot);
  } else {
    const char *hdr_lbl = is_crit ? "CRITICAL ALERT" : (is_warn ? "WARNING" : "NOTIFICATION");
    fast_print(it, cx, scale_y(72), ctx.font_small, accent_c, TextAlign::CENTER, hdr_lbl);

    if (has_cam_url) {
      fast_rect_outline(it, cx - 14, scale_y(112), 22, 18, st.col_white);
      fast_filled_triangle(it, cx + 8, scale_y(121), cx + 15, scale_y(115), cx + 15, scale_y(127), st.theme_secondary);
      fast_circle(it, cx - 3, scale_y(121), 4, accent_c);
    } else {
      fast_filled_circle(it, cx, scale_y(118), 9, accent_c);
      fill_rect_row_major(it, cx - 9, scale_y(118), 19, 8, accent_c);
      fill_rect_row_major(it, cx - 12, scale_y(118) + 8, 25, 2, st.col_white);
      fast_filled_circle(it, cx, scale_y(118) + 12, 3, st.col_white);
    }

    fast_print(it, cx, scale_y(168), ctx.font_date, st.col_white, TextAlign::CENTER, fit_title.c_str());

    if (!line1.empty()) {
      fast_print(it, cx, line2.empty() ? scale_y(222) : scale_y(210), ctx.font_small, st.theme_secondary,
                 TextAlign::CENTER, line1.c_str());
    }
    if (!line2.empty()) {
      fast_print(it, cx, scale_y(238), ctx.font_small, st.theme_secondary, TextAlign::CENTER, line2.c_str());
    }
    if (has_cam_url && !cam_ready) {
      fast_print(it, cx, scale_y(274), ctx.font_small, st.col_slate, TextAlign::CENTER, "Loading camera snapshot...");
      frame_hits().add_rect(0, 0, SCREEN_W, scale_y(308), WidgetId::NOTIF_OPEN_CAM_BTN, kHitTap);
    }

    render_modern_card(it, cx, scale_y(336), 216, 44, 12, ui::chrome_card_bg_on(), accent_c,
                       "Dismiss", st.col_white, ctx.font_small);
    frame_hits().add_centered_rect(cx, scale_y(336), scale_x(216), scale_y(44),
                                   WidgetId::NOTIF_DISMISS_BTN, kHitTap | kHitFocusSlot);
  }
}

}  // namespace latter_ui
