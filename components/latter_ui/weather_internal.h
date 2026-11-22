#pragma once

// ==============================================================================
// Internal shared declarations for the Latter Weather subsystem
// Pure C++20 internal L3 header — zero ESPHome includes.
// ==============================================================================

#include "ui_pages.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace latter_ui {

enum class RainTier : uint8_t { Drizzle = 0, Normal = 1, Storm = 2, Pouring = 3 };

struct RainStreak {
  float x = 0.0f, y = 0.0f, vy = 100.0f, len = 30.0f;
  uint8_t alpha_peak = 32U;
  uint16_t pass_seq = 0U;
};

struct LightningFlashState {
  uint32_t next_flash_ms = 0U, flash_start_ms = 0U, flash_seq = 0U;
  bool active = false;
};

struct WeatherParticleEngine {
  static constexpr int kFarVeilCount = 56;
  static constexpr int kNearHeroCount = 64;
  static constexpr int kSnowCount = 64;
  static constexpr int kLeafCount = 10;

  bool inited = false;
  uint32_t last_step_ms = 0U, cond_hash = 0U;
  float phase_s = 0.0f;

  RainStreak far_veil[kFarVeilCount]{};
  RainStreak near_hero[kNearHeroCount]{};
  float snow_y[kSnowCount]{};
  uint16_t snow_pass[kSnowCount]{};
  float leaf_x[kLeafCount]{};
  float gust_x[3]{};
  LightningFlashState lightning{};
};

inline void blend_sky_pixel_q8(Rgb565Surface &it, uint16_t *fb, int x, int y,
                               UiColor fg, int alpha_q8) {
  if (alpha_q8 <= 0 || x < 0 || x >= SCREEN_W || y < clip_y0() || y > clip_y1()) return;
  if (fb == nullptr) {
    if (alpha_q8 >= 64) fast_pixel(it, x, y, scale_color(fg, static_cast<float>(alpha_q8) * (1.0f / 256.0f)));
    return;
  }
  uint16_t &dst_hw = fb[static_cast<size_t>(y) * SCREEN_W + static_cast<size_t>(x)];
  if (alpha_q8 >= 256) {
    dst_hw = encode_hw_rgb565(fg);
    return;
  }
  const uint16_t dst = decode_hw_rgb565_raw(dst_hw);
  const int dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
  const int sr = fg.r >> 3, sg = fg.g >> 2, sb = fg.b >> 3, inv = 256 - alpha_q8;
  dst_hw = encode_hw_rgb565_raw(static_cast<uint16_t>((((sr * alpha_q8 + dr * inv) >> 8) << 11) |
                                                      (((sg * alpha_q8 + dg * inv) >> 8) << 5) |
                                                      ((sb * alpha_q8 + db * inv) >> 8)));
}

inline void draw_tapered_streak_q8(Rgb565Surface &it, uint16_t *fb,
                                   int tx, int ty, int hx, int hy,
                                   UiColor col_tail, UiColor col_head,
                                   int a_tail_q8, int a_head_q8, int cy) {
  const int steps = std::max(std::abs(hx - tx), std::abs(hy - ty));
  if (steps <= 0 || (a_tail_q8 <= 0 && a_head_q8 <= 0)) return;
  const int y_lo = clip_y0(), y_hi = clip_y1();
  if (std::max(ty, hy) < y_lo || std::min(ty, hy) > y_hi) return;

  static constexpr int kSkyInset = LATTER_ROUND_DISPLAY ? 5 : 0;
  const int32_t dx_fp = ((hx - tx) << 16) / steps, dy_fp = ((hy - ty) << 16) / steps;
  const int32_t da_fp = ((a_head_q8 - a_tail_q8) << 16) / steps;
  const bool same_col = (col_tail == col_head);
  const int32_t dr_fp = same_col ? 0 : (((static_cast<int>(col_head.r) - col_tail.r) << 16) / steps);
  const int32_t dg_fp = same_col ? 0 : (((static_cast<int>(col_head.g) - col_tail.g) << 16) / steps);
  const int32_t db_fp = same_col ? 0 : (((static_cast<int>(col_head.b) - col_tail.b) << 16) / steps);

  int32_t x_fp = (tx << 16) + 0x8000, y_fp = (ty << 16) + 0x8000, a_fp = (a_tail_q8 << 16) + 0x8000;
  int32_t r_fp = (static_cast<int>(col_tail.r) << 16) + 0x8000;
  int32_t g_fp = (static_cast<int>(col_tail.g) << 16) + 0x8000;
  int32_t b_fp = (static_cast<int>(col_tail.b) << 16) + 0x8000;

  for (int s = 0; s <= steps; s++) {
    const int py = y_fp >> 16, px = x_fp >> 16, a_q8 = a_fp >> 16;
    if (a_q8 > 0 && py >= y_lo && py <= y_hi && is_inside_bezel(px, py, cy, kSkyInset)) {
      const UiColor c = same_col ? col_head
                                 : UiColor(static_cast<uint8_t>(r_fp >> 16),
                                           static_cast<uint8_t>(g_fp >> 16),
                                           static_cast<uint8_t>(b_fp >> 16));
      blend_sky_pixel_q8(it, fb, px, py, c, a_q8);
    }
    x_fp += dx_fp; y_fp += dy_fp; a_fp += da_fp;
    if (!same_col) { r_fp += dr_fp; g_fp += dg_fp; b_fp += db_fp; }
  }
}

void lunar_disc_cache_drop() noexcept;
void render_lunar_disc(Rgb565Surface &it, int moon_cx, int moon_cy, int moon_r, double cycle);
void render_night_starfield_dome(Rgb565Surface &it, int cx, int cy, int cel_cx, int cel_cy, bool show_celestial);
void render_solar_orb_disc(Rgb565Surface &it, int cel_cx, int cel_cy, bool is_fog_mist);
void render_night_shooting_star(Rgb565Surface &it, uint32_t now_ms, int cy);
bool is_night_meteor_active(uint32_t now_ms);
const char *wind_bearing_to_cardinal(float deg);
std::string format_forecast_short_name(const std::string &c);
void render_weather_sun_subpage(Rgb565Surface &it, const UiFrameContext &ctx, UiColor col_warm_gold);
bool render_weather_moon_subpage(Rgb565Surface &it, const UiFrameContext &ctx);

RainTier classify_rain_tier(std::string_view cond, bool is_storm) noexcept;
void init_rain_streaks(WeatherParticleEngine &eng, RainTier tier);
void render_rain_layers_fx(Rgb565Surface &it, uint16_t *fb, int cy, bool is_night, RainTier tier,
                           float dt_s, float precip_alpha, float par_x, float par_y,
                           WeatherParticleEngine &eng);
void render_storm_lightning_fx(Rgb565Surface &it, uint16_t *fb, int cx, int cy,
                               uint32_t now_ms, float precip_alpha, LightningFlashState &lt);
void render_wind_gusts_and_leaves(Rgb565Surface &it, uint16_t *fb, int cy, bool is_night,
                                  float dt_s, float precip_alpha, float par_x, float par_y,
                                  WeatherParticleEngine &eng);
void render_snowfall_fx(Rgb565Surface &it, uint16_t *fb, int cy, bool is_night,
                        float dt_s, float precip_alpha, float par_x, float par_y,
                        WeatherParticleEngine &eng);

}  // namespace latter_ui
