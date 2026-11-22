#include "hal_display_lifecycle.h"
#include "weather_internal.h"
#include "ui_touch_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

namespace latter_ui {
namespace {

static constexpr int kCloudShapeEntries = 24;
static constexpr int kCloudScratchRuns = 3072;
static constexpr int kCloudMaxCols = 168;
static constexpr int kCloudColStep = (SCREEN_W > 520) ? 3 : 2;
static constexpr int kVeilStep = (SCREEN_W > 520) ? 4 : 3;
static constexpr int kMaxVeilCols = 180;

struct CloudRun {
  int16_t y;
  uint8_t k0, ncols, band;
};

struct CloudShape {
  uint32_t scale_bits;
  int16_t seed, y_min, y_max;
  uint16_t count, cap;
  uint32_t last_use;
  CloudRun *runs;
};

struct CloudBuildCol {
  uint8_t k;
  int16_t y_top, y_c1, y_c2, y_c3, y_mid_b, y_bot;
  uint8_t top_band;
};

struct CumulusCol {
  int16_t x, y_top, y_c1, y_c2, y_c3, y_mid_b, y_bot;
  uint8_t top_band;
};

struct VeilCol {
  int16_t x, w, y_top, y_mid1, y_mid2, y_bot;
};

// Retryable PSRAM cloud cache & scratch state (M-P1-18):
// Never latches nullptr on transient OOM; freed cleanly via weather_cloud_cache_drop().
struct WeatherCloudCache {
  CloudShape *entries = nullptr;
  CloudRun *scratch_runs = nullptr;
  CloudBuildCol *build_cols = nullptr;
  CumulusCol *cumulus_cols = nullptr;
  VeilCol *veil_cols = nullptr;
  uint32_t use_seq = 0U;
};

WeatherCloudCache &weather_cloud_cache() noexcept {
  static WeatherCloudCache cc{};
  return cc;
}

void cloud_span(Rgb565Surface &it, uint16_t *fb, int x0, int y, int len, uint16_t hw, UiColor c) {
  if (!row_in_clip_band(y)) return;
  if (fb == nullptr || len <= 0 || y < 0 || y >= SCREEN_H || x0 < 0 || x0 + len > SCREEN_W) {
    fast_hline(it, x0, y, len, c);
    return;
  }
  uint16_t *dst = fb + static_cast<size_t>(y) * SCREEN_W + static_cast<size_t>(x0);
  for (int k = 0; k < len; k++) dst[k] = hw;
}

bool cumulus_column_profile(int dx, float inv_hw, float scale, float phase_seed,
                            int &h_top, int &h_bot, bool &sunlit_slope) {
  const float u = -static_cast<float>(dx) * inv_hw;
  const float env = 1.0f - u * u;
  if (env <= 0.02f) return false;

  const float d0 = u - 0.08f, t0 = std::fmax(0.0f, 1.0f - d0 * d0 * 4.2f);
  const float d1 = u + 0.36f, t1 = std::fmax(0.0f, 1.0f - d1 * d1 * 6.2f) * 0.80f;
  const float d2 = u - 0.48f, t2 = std::fmax(0.0f, 1.0f - d2 * d2 * 7.5f) * 0.70f;
  const float d3 = u + 0.68f, t3 = std::fmax(0.0f, 1.0f - d3 * d3 * 11.5f) * 0.45f;
  const float d4 = u - 0.74f, t4 = std::fmax(0.0f, 1.0f - d4 * d4 * 13.5f) * 0.38f;

  const float dome = std::fmax(std::fmax(t0, t1), std::fmax(t2, std::fmax(t3, t4)));
  const float ripple = 0.08f * fast_sin(u * 9.5f + phase_seed) * env;
  const float s0 = 0.5f * (1.0f + env);
  const float sqrt_env = 0.5f * (s0 + env / s0);
  const float h_top_f = (14.0f + 40.0f * dome + 36.0f * ripple) * scale * sqrt_env;
  const float bot_lobes = 0.55f + 0.45f * std::fabs(fast_cos(u * 4.2f + phase_seed * 0.7f));
  h_top = static_cast<int>(h_top_f);
  h_bot = static_cast<int>((8.0f + 14.0f * bot_lobes) * env * scale);
  sunlit_slope = (u > -0.45f && dome > 0.32f);
  return true;
}

int cloud_build_runs(int half_w, float scale, int seed, CloudRun *out, int cap, int &y_min, int &y_max) {
  auto &cc = weather_cloud_cache();
  if (cc.build_cols == nullptr) {
    cc.build_cols = static_cast<CloudBuildCol *>(ui_psram_malloc(sizeof(CloudBuildCol) * kCloudMaxCols));
    if (cc.build_cols == nullptr) return -1;
  }
  CloudBuildCol *cols = cc.build_cols;
  int n_cols = 0;
  const float inv_hw = 1.0f / static_cast<float>(half_w);
  const float phase_seed = static_cast<float>(seed) * 0.85f;
  int k = 0;
  for (int dx = -half_w; dx <= half_w; dx += kCloudColStep, k++) {
    if (k >= kCloudMaxCols) return -1;
    int h_top = 0, h_bot = 0;
    bool sunlit = false;
    if (!cumulus_column_profile(dx, inv_hw, scale, phase_seed, h_top, h_bot, sunlit)) continue;
    const int y_top = -h_top, y_bot = h_bot, span = y_bot - y_top;
    if (span <= 2) continue;
    const int y_c3 = y_top + (span * 76) / 100;
    cols[n_cols++] = {static_cast<uint8_t>(k), static_cast<int16_t>(y_top),
                      static_cast<int16_t>(y_top + std::max(2, span / 6)),
                      static_cast<int16_t>(y_top + (span * 46) / 100), static_cast<int16_t>(y_c3),
                      static_cast<int16_t>((y_c3 + y_bot) >> 1), static_cast<int16_t>(y_bot),
                      static_cast<uint8_t>(sunlit ? 1 : 2)};
  }
  if (n_cols == 0) { y_min = y_max = 0; return 0; }
  y_min = SCREEN_H; y_max = -SCREEN_H;
  for (int i = 0; i < n_cols; i++) {
    if (cols[i].y_top < y_min) y_min = cols[i].y_top;
    if (cols[i].y_bot > y_max) y_max = cols[i].y_bot;
  }
  auto band_at = [](const CloudBuildCol &c, int y) -> uint8_t {
    if (y < c.y_top || y > c.y_bot) return 0;
    if (y < c.y_c1) return c.top_band;
    if (y < c.y_c2) return 2;
    if (y < c.y_c3) return 3;
    return (y <= c.y_mid_b) ? 4 : 5;
  };
  int n = 0;
  for (int y = y_min; y <= y_max; y++) {
    int i = 0;
    while (i < n_cols) {
      const uint8_t band = band_at(cols[i], y);
      if (band == 0) { i++; continue; }
      const int k0 = cols[i].k;
      int k_end = k0 + 1;
      i++;
      while (i < n_cols && cols[i].k == k_end && band_at(cols[i], y) == band) { k_end++; i++; }
      if (n >= cap) return -1;
      out[n++] = {static_cast<int16_t>(y), static_cast<uint8_t>(k0), static_cast<uint8_t>(k_end - k0), band};
    }
  }
  return n;
}

const CloudShape *cloud_shape_get(int half_w, float scale, int seed) {
  auto &cc = weather_cloud_cache();
  if (cc.entries == nullptr) cc.entries = static_cast<CloudShape *>(ui_psram_calloc(kCloudShapeEntries, sizeof(CloudShape)));
  if (cc.scratch_runs == nullptr) cc.scratch_runs = static_cast<CloudRun *>(ui_psram_malloc(sizeof(CloudRun) * kCloudScratchRuns));
  if (cc.entries == nullptr || cc.scratch_runs == nullptr) return nullptr;

  uint32_t bits = 0;
  std::memcpy(&bits, &scale, sizeof(bits));
  ++cc.use_seq;
  CloudShape *victim = &cc.entries[0];
  for (int i = 0; i < kCloudShapeEntries; i++) {
    CloudShape &e = cc.entries[i];
    if (e.runs != nullptr && e.scale_bits == bits && e.seed == seed) {
      e.last_use = cc.use_seq;
      return &e;
    }
    if (e.last_use < victim->last_use) victim = &e;
  }
  int y_min = 0, y_max = 0;
  const int n = cloud_build_runs(half_w, scale, seed, cc.scratch_runs, kCloudScratchRuns, y_min, y_max);
  if (n <= 0) return nullptr;
  if (victim->cap < static_cast<uint16_t>(n)) {
    if (victim->runs != nullptr) ui_psram_free(victim->runs);
    victim->runs = static_cast<CloudRun *>(ui_psram_malloc(sizeof(CloudRun) * static_cast<size_t>(n)));
    victim->cap = (victim->runs != nullptr) ? static_cast<uint16_t>(n) : 0U;
    if (victim->runs == nullptr) return nullptr;
  }
  std::memcpy(victim->runs, cc.scratch_runs, sizeof(CloudRun) * static_cast<size_t>(n));
  victim->scale_bits = bits;
  victim->seed = static_cast<int16_t>(seed);
  victim->y_min = static_cast<int16_t>(y_min);
  victim->y_max = static_cast<int16_t>(y_max);
  victim->count = static_cast<uint16_t>(n);
  victim->last_use = cc.use_seq;
  return victim;
}

#if !LATTER_ROUND_DISPLAY
int floor_div_int(int a, int b) { return (a >= 0) ? (a / b) : -((-a + b - 1) / b); }
int ceil_div_int(int a, int b) { return -floor_div_int(-a, b); }
#endif

void render_cumulus_cloud(Rgb565Surface &it, int bx, int by, float scale,
                          bool overcast_mode, bool is_night, int cloud_seed = -1) {
  if (SCREEN_W > 520) {
    scale *= (static_cast<float>(SCREEN_W) / 466.0f);
    by = scale_y(by);
  }
  const int half_w = static_cast<int>(84.0f * scale);
  if (bx + half_w < 5 || bx - half_w >= SCREEN_W - 5 || by + 45 < 5 || by - 65 >= SCREEN_H - 5) return;

  const UiColor col_rim    = is_night ? UiColor(86, 108, 138) : (overcast_mode ? UiColor(124, 154, 188) : UiColor(172, 204, 236));
  const UiColor col_crest  = is_night ? UiColor(60,  78, 102) : (overcast_mode ? UiColor( 90, 118, 150) : UiColor(126, 160, 198));
  const UiColor col_high   = is_night ? UiColor(40,  54,  72) : (overcast_mode ? UiColor( 62,  86, 114) : UiColor( 88, 118, 154));
  const UiColor col_body   = is_night ? UiColor(24,  34,  46) : (overcast_mode ? UiColor( 42,  60,  82) : UiColor( 58,  82, 112));
  const UiColor col_shadow = is_night ? UiColor(14,  20,  28) : (overcast_mode ? UiColor( 24,  38,  56) : UiColor( 36,  54,  76));

  const int eff_seed = (cloud_seed >= 0) ? (cloud_seed & 7)
                                         : ((static_cast<int>(std::lround(scale * 100.0f)) * 5 + (overcast_mode ? 3 : 0)) & 7);
  const float inv_hw = 1.0f / static_cast<float>(half_w), phase_seed = static_cast<float>(eff_seed) * 0.85f;

  if (const CloudShape *shape = cloud_shape_get(half_w, scale, eff_seed)) {
    if (by + shape->y_max >= 5 && by + shape->y_min <= SCREEN_H - 6) {
      const UiColor pal[6] = {kChromeShadow, col_rim, col_crest, col_high, col_body, col_shadow};
      uint16_t pal_hw[6];
      for (int k = 0; k < 6; k++) pal_hw[k] = encode_hw_rgb565(pal[k]);
      uint16_t *fb_c = active_frame_buf16();
      const int x_first = bx - half_w;
#if LATTER_ROUND_DISPLAY
      for (int i = 0; i < shape->count; i++) {
        const CloudRun &r = shape->runs[i];
        const int py = by + r.y;
        if (py < 5 || py > SCREEN_H - 6) continue;
        const int max_dx = circle_half_span(py, 5);
        if (max_dx <= 8) continue;
        const int rx0 = std::max(x_first + static_cast<int>(r.k0) * kCloudColStep, SCREEN_CX - max_dx);
        const int rx1 = std::min(x_first + static_cast<int>(r.k0 + r.ncols) * kCloudColStep, SCREEN_CX + max_dx + 1);
        if (rx1 > rx0) cloud_span(it, fb_c, rx0, py, rx1 - rx0, pal_hw[r.band], pal[r.band]);
      }
#else
      const int k_lo = std::max(0, ceil_div_int(5 - x_first, kCloudColStep));
      const int k_hi = floor_div_int(SCREEN_W - 6 - kCloudColStep - x_first, kCloudColStep);
      for (int i = 0; i < shape->count; i++) {
        const CloudRun &r = shape->runs[i];
        const int py = by + r.y;
        if (py < 5 || py > SCREEN_H - 6) continue;
        const int a = std::max<int>(r.k0, k_lo), b2 = std::min<int>(r.k0 + r.ncols - 1, k_hi);
        if (a <= b2) cloud_span(it, fb_c, x_first + a * kCloudColStep, py, (b2 - a + 1) * kCloudColStep, pal_hw[r.band], pal[r.band]);
      }
#endif
      return;
    }
  }

  auto &cc = weather_cloud_cache();
  if (cc.cumulus_cols == nullptr) {
    cc.cumulus_cols = static_cast<CumulusCol *>(ui_psram_malloc(sizeof(CumulusCol) * kCloudMaxCols));
    if (cc.cumulus_cols == nullptr) return;
  }
  CumulusCol *cols = cc.cumulus_cols;
  int n_cols = 0, min_y = SCREEN_H, max_y = 0;

  for (int dx = -half_w; dx <= half_w && n_cols < kCloudMaxCols; dx += kCloudColStep) {
    const int x = bx + dx;
#if LATTER_ROUND_DISPLAY
    const int max_dy = circle_half_span(x, 5);
    if (max_dy <= 8) continue;
    const int y_min = SCREEN_CY - max_dy, y_max = SCREEN_CY + max_dy;
#else
    if (x < 5 || x + kCloudColStep >= SCREEN_W - 5) continue;
    const int y_min = 5, y_max = SCREEN_H - 6;
#endif
    int h_top = 0, h_bot = 0;
    bool sunlit_slope = false;
    if (!cumulus_column_profile(dx, inv_hw, scale, phase_seed, h_top, h_bot, sunlit_slope)) continue;

    const int y_top = std::max(y_min, by - h_top), y_bot = std::min(y_max, by + h_bot), span = y_bot - y_top;
    if (span <= 2 || x < 5 || x + kCloudColStep > SCREEN_W - 5) continue;
    const int y_c3 = y_top + (span * 76) / 100;
    cols[n_cols++] = {static_cast<int16_t>(x), static_cast<int16_t>(y_top),
                      static_cast<int16_t>(y_top + std::max(2, span / 6)),
                      static_cast<int16_t>(y_top + (span * 46) / 100), static_cast<int16_t>(y_c3),
                      static_cast<int16_t>((y_c3 + y_bot) >> 1), static_cast<int16_t>(y_bot),
                      static_cast<uint8_t>(sunlit_slope ? 1 : 2)};
    if (y_top < min_y) min_y = y_top;
    if (y_bot > max_y) max_y = y_bot;
  }

  if (n_cols <= 0 || min_y > max_y) return;
  const UiColor palette[6] = {kChromeShadow, col_rim, col_crest, col_high, col_body, col_shadow};
  uint16_t *fb = active_frame_buf16();
  uint16_t palette_hw[6];
  for (int k = 0; k < 6; k++) palette_hw[k] = encode_hw_rgb565(palette[k]);

  auto col_band = [](const CumulusCol &c, int y) -> uint8_t {
    if (y < c.y_top || y > c.y_bot) return 0;
    if (y < c.y_c1) return c.top_band;
    if (y < c.y_c2) return 2;
    if (y < c.y_c3) return 3;
    return (y <= c.y_mid_b) ? 4 : 5;
  };
  for (int y = min_y; y <= max_y; y++) {
    int i = 0;
    while (i < n_cols) {
      const uint8_t band = col_band(cols[i], y);
      if (band == 0) { i++; continue; }
      const int run_x0 = cols[i].x;
      int run_end = cols[i].x + kCloudColStep;
      i++;
      while (i < n_cols && cols[i].x == run_end && col_band(cols[i], y) == band) {
        run_end += kCloudColStep;
        i++;
      }
      cloud_span(it, fb, run_x0, y, run_end - run_x0, palette_hw[band], palette[band]);
    }
  }
}

void render_wispy_cloud_veil(Rgb565Surface &it, int center_x, int base_y, int half_w,
                             float phase, int seed_id,
                             UiColor c_crest, UiColor c_body, UiColor c_base, int cy) {
  if (half_w <= 10) return;
  if (SCREEN_W > 520) base_y = scale_y(base_y);
  const float inv_hw = 1.0f / static_cast<float>(half_w);

  auto &cc = weather_cloud_cache();
  if (cc.veil_cols == nullptr) {
    cc.veil_cols = static_cast<VeilCol *>(ui_psram_malloc(sizeof(VeilCol) * kMaxVeilCols));
    if (cc.veil_cols == nullptr) return;
  }
  VeilCol *cols = cc.veil_cols;
  int n_cols = 0, min_y = SCREEN_H, max_y = 0;

  const int x_start = center_x - half_w;
  const float xf0 = static_cast<float>(x_start);
  float s1 = 0.0f, c1 = 1.0f, s2 = 0.0f, c2 = 1.0f, s3 = 0.0f, c3 = 1.0f, s4 = 0.0f, c4 = 1.0f, s5 = 0.0f, c5 = 1.0f;
  fast_sincos(xf0 * 0.028f + phase * 1.6f, s1, c1);
  fast_sincos(xf0 * 0.062f - phase * 1.1f, s2, c2);
  fast_sincos(xf0 * 0.036f - phase * 1.3f + 1.2f, s3, c3);
  fast_sincos(xf0 * 0.085f + phase * 2.0f, s4, c4);
  fast_sincos(xf0 * 0.052f + phase * 1.8f + static_cast<float>(seed_id), s5, c5);

  static const float kSinD1 = std::sin(0.028f * static_cast<float>(kVeilStep)), kCosD1 = std::cos(0.028f * static_cast<float>(kVeilStep));
  static const float kSinD2 = std::sin(0.062f * static_cast<float>(kVeilStep)), kCosD2 = std::cos(0.062f * static_cast<float>(kVeilStep));
  static const float kSinD3 = std::sin(0.036f * static_cast<float>(kVeilStep)), kCosD3 = std::cos(0.036f * static_cast<float>(kVeilStep));
  static const float kSinD4 = std::sin(0.085f * static_cast<float>(kVeilStep)), kCosD4 = std::cos(0.085f * static_cast<float>(kVeilStep));
  static const float kSinD5 = std::sin(0.052f * static_cast<float>(kVeilStep)), kCosD5 = std::cos(0.052f * static_cast<float>(kVeilStep));

  for (int x = x_start; x <= center_x + half_w && n_cols < kMaxVeilCols; x += kVeilStep) {
    const float cur_s1 = s1, cur_c2 = c2, cur_s3 = s3, cur_c4 = c4, cur_s5 = s5;
    const float ns1 = s1 * kCosD1 + c1 * kSinD1; c1 = c1 * kCosD1 - s1 * kSinD1; s1 = ns1;
    const float ns2 = s2 * kCosD2 + c2 * kSinD2; c2 = c2 * kCosD2 - s2 * kSinD2; s2 = ns2;
    const float ns3 = s3 * kCosD3 + c3 * kSinD3; c3 = c3 * kCosD3 - s3 * kSinD3; s3 = ns3;
    const float ns4 = s4 * kCosD4 + c4 * kSinD4; c4 = c4 * kCosD4 - s4 * kSinD4; s4 = ns4;
    const float ns5 = s5 * kCosD5 + c5 * kSinD5; c5 = c5 * kCosD5 - s5 * kSinD5; s5 = ns5;

#if LATTER_ROUND_DISPLAY
    const int max_dy = circle_half_span(x, 6);
    if (max_dy <= 10) continue;
    const int y_min = cy - max_dy, y_max = cy + max_dy;
#else
    (void)cy;
    if (x < 6 || x + kVeilStep >= SCREEN_W - 6) continue;
    const int y_min = 6, y_max = SCREEN_H - 7;
#endif
    const float u = static_cast<float>(x - center_x) * inv_hw, taper = 1.0f - u * u;
    if (taper <= 0.04f) continue;
    const float eff_taper = taper * (0.65f + 0.35f * (0.55f + 0.45f * cur_s5));
    if (eff_taper < 0.14f) continue;

    const int y_top = std::max(y_min, base_y - static_cast<int>((9.0f + std::fmax(0.0f, cur_s1 * 12.0f + cur_c2 * 6.0f)) * eff_taper));
    const int y_bot = std::min(y_max, base_y + static_cast<int>((6.0f + std::fmax(0.0f, cur_s3 * 7.5f + cur_c4 * 4.0f)) * eff_taper));
    if (y_bot <= y_top) continue;

    cols[n_cols++] = {static_cast<int16_t>(x),
                      static_cast<int16_t>((eff_taper > 0.32f) ? kVeilStep : std::max(1, kVeilStep - 1)),
                      static_cast<int16_t>(y_top), static_cast<int16_t>(y_top + ((y_bot - y_top) / 3)),
                      static_cast<int16_t>(y_top + (((y_bot - y_top) * 2) / 3)), static_cast<int16_t>(y_bot)};
    if (y_top < min_y) min_y = y_top;
    if (y_bot > max_y) max_y = y_bot;
  }

  if (n_cols <= 0 || min_y > max_y) return;
  const UiColor veil_pal[4] = {kChromeShadow, c_crest, c_body, c_base};
  uint16_t *fb = active_frame_buf16();
  uint16_t veil_hw[4];
  for (int k = 0; k < 4; k++) veil_hw[k] = encode_hw_rgb565(veil_pal[k]);

  auto veil_band = [](const VeilCol &vc, int y) -> uint8_t {
    if (y < vc.y_top || y > vc.y_bot) return 0;
    return (y <= vc.y_mid1) ? 1 : ((y <= vc.y_mid2) ? 2 : 3);
  };
  for (int y = min_y; y <= max_y; y++) {
    int i = 0;
    while (i < n_cols) {
      const uint8_t band = veil_band(cols[i], y);
      if (band == 0) { i++; continue; }
      const int run_x0 = cols[i].x;
      int run_end = cols[i].x + cols[i].w;
      i++;
      while (i < n_cols && cols[i].x <= run_end && veil_band(cols[i], y) == band) {
        run_end = std::max(run_end, static_cast<int>(cols[i].x + cols[i].w));
        i++;
      }
      cloud_span(it, fb, run_x0, y, run_end - run_x0, veil_hw[band], veil_pal[band]);
    }
  }
}

struct WeatherSkyFadeState {
  float precip_alpha_scale = 1.0f;
  uint32_t last_eval_ms = 0U;
  bool calm_frame_rendered = false;
};

WeatherSkyFadeState &sky_fade_state() {
  static WeatherSkyFadeState st{};
  return st;
}

void step_weather_battery_fade(bool usb_connected, uint32_t now_ms, uint32_t latest_input_ms) {
  auto &st = sky_fade_state();
  const bool want_motion = battery_motion_allowed(usb_connected, now_ms, latest_input_ms);
  if (st.last_eval_ms == 0U) {
    st.last_eval_ms = (now_ms == 0U) ? 1U : now_ms;
    st.precip_alpha_scale = want_motion ? 1.0f : 0.0f;
    st.calm_frame_rendered = !want_motion;
    return;
  }
  const uint32_t raw_dt = now_ms - st.last_eval_ms;
  if (raw_dt == 0U) return;
  st.last_eval_ms = now_ms;
  const float dt_ms = static_cast<float>(std::min<uint32_t>(raw_dt, 180U));
  if (want_motion) {
    st.calm_frame_rendered = false;
    if (st.precip_alpha_scale < 1.0f) st.precip_alpha_scale = std::min(1.0f, st.precip_alpha_scale + dt_ms * (1.0f / 400.0f));
  } else if (st.precip_alpha_scale > 0.0f) {
    st.precip_alpha_scale = std::max(0.0f, st.precip_alpha_scale - dt_ms * (1.0f / 800.0f));
  }
}

uint32_t fnv1a_sky_str(std::string_view sv) noexcept {
  uint32_t h = 2166136261U;
  for (unsigned char ch : sv) { h ^= ch; h *= 16777619U; }
  return h;
}

// Retryable PSRAM static sky base cache (M-P1-18): no permanent alloc_attempted latch.
struct SkyBaseCache {
  uint16_t *px = nullptr;
  bool valid = false, is_night = false, show_celestial = false;
  uint32_t cond_hash = 0U;
  int16_t cel_cx = -1, cel_cy = -1;
  uint8_t quantized_moon_phase = 255U, dim_mode = 255U;
};

SkyBaseCache &sky_base_cache() {
  static SkyBaseCache cache{};
  return cache;
}

void bake_static_sky_base(Rgb565Surface &it, int cx, int cy, std::string_view cond, bool is_night,
                          double moon_cycle, int cel_cx, int cel_cy, bool show_celestial) {
  const bool is_fog_mist = (cond.find("fog") != std::string_view::npos ||
                            cond.find("mist") != std::string_view::npos ||
                            cond.find("haze") != std::string_view::npos);
  const bool is_windy = (cond.find("wind") != std::string_view::npos || cond.find("breez") != std::string_view::npos);
  const bool is_clear_day = (!is_night && (cond == "sunny" || cond == "clear-day" || cond == "clear" || cond == "clearday"));
  const bool is_partly = (cond.find("partly") != std::string_view::npos ||
                          cond.find("mostlysunny") != std::string_view::npos ||
                          cond.find("partlysunny") != std::string_view::npos);
  const bool is_storm = (cond.find("lightning") != std::string_view::npos || cond.find("thunder") != std::string_view::npos);
  const bool is_pouring_precip = (cond == "pouring" || cond.find("heavy") != std::string_view::npos);
  const bool is_rain_precip = (cond.find("rain") != std::string_view::npos ||
                               cond.find("pouring") != std::string_view::npos ||
                               cond.find("drizzle") != std::string_view::npos);
  const bool is_snow_precip = (cond.find("snow") != std::string_view::npos ||
                               cond.find("sleet") != std::string_view::npos ||
                               cond.find("hail") != std::string_view::npos);
  const bool is_overcast_canopy =
      ((cond.find("cloud") != std::string_view::npos || cond.find("overcast") != std::string_view::npos) && !is_partly) ||
      is_rain_precip || is_snow_precip;
  const bool is_clear_or_partly_night =
      (is_night && (cond == "clear-night" || cond == "clearnight" ||
                    cond == "sunny" || cond == "clear" || cond == "clear-day" || is_partly));

  UiColor c_zen(20, 44, 76), c_mid(28, 56, 90), c_hor(40, 74, 112);
  if (is_night) {
    if (is_fog_mist) { c_zen = UiColor(8, 13, 20); c_mid = UiColor(14, 21, 31); c_hor = UiColor(20, 29, 42); }
    else if (is_overcast_canopy || is_storm) { c_zen = UiColor(5, 9, 16); c_mid = UiColor(11, 18, 30); c_hor = UiColor(17, 27, 43); }
    else { c_zen = UiColor(3, 6, 13); c_mid = UiColor(8, 14, 26); c_hor = UiColor(14, 23, 40); }
  } else if (is_fog_mist) {
    c_zen = UiColor(26, 38, 52); c_mid = UiColor(34, 48, 63); c_hor = UiColor(42, 58, 74);
  } else if (is_storm || is_rain_precip) {
    c_zen = UiColor(14, 24, 38); c_mid = UiColor(20, 33, 50); c_hor = UiColor(28, 44, 65);
    if (is_pouring_precip || is_storm) {
      c_zen = scale_color(c_zen, 0.76f); c_mid = scale_color(c_mid, 0.76f); c_hor = scale_color(c_hor, 0.78f);
    }
  } else if (is_overcast_canopy) {
    c_zen = UiColor(18, 32, 50); c_mid = UiColor(25, 42, 63); c_hor = UiColor(34, 54, 78);
  } else if (is_partly) {
    c_zen = UiColor(22, 45, 74); c_mid = UiColor(30, 56, 88); c_hor = UiColor(40, 70, 104);
  }

  const int mid_y = SCREEN_H / 2;
  for (int y = 0; y < SCREEN_H; y++) {
    const UiColor row_a = (y <= mid_y) ? c_zen : c_mid, row_b = (y <= mid_y) ? c_mid : c_hor;
    const int t256 = (y <= mid_y) ? ((mid_y > 0) ? ((y * 256) / mid_y) : 0)
                                  : (((y - mid_y) * 256) / std::max(1, SCREEN_H - 1 - mid_y));
    const uint32_t t_clamped = static_cast<uint32_t>(std::clamp(t256, 0, 256)), inv_t = 256U - t_clamped;
    const uint32_t r88 = static_cast<uint32_t>(row_a.r) * inv_t + static_cast<uint32_t>(row_b.r) * t_clamped;
    const uint32_t g88 = static_cast<uint32_t>(row_a.g) * inv_t + static_cast<uint32_t>(row_b.g) * t_clamped;
    const uint32_t b88 = static_cast<uint32_t>(row_a.b) * inv_t + static_cast<uint32_t>(row_b.b) * t_clamped;
#if LATTER_ROUND_DISPLAY
    if (y < 5 || y >= SCREEN_H - 5) { fast_hline(it, 0, y, SCREEN_W, kChromeShadow); continue; }
    const int hw = circle_half_span(y, 5);
    if (hw <= 0) { fast_hline(it, 0, y, SCREEN_W, kChromeShadow); continue; }
    const int lx = std::max(0, cx - hw), rx = std::min(SCREEN_W, cx + hw + 1);
    if (lx > 0) fast_hline(it, 0, y, lx, kChromeShadow);
    if (rx > lx) fast_hline_dither(it, lx, y, rx - lx, r88, g88, b88);
    if (rx < SCREEN_W) fast_hline(it, rx, y, SCREEN_W - rx, kChromeShadow);
#else
    fast_hline_dither(it, 0, y, SCREEN_W, r88, g88, b88);
#endif
  }

  if (is_clear_or_partly_night) render_night_starfield_dome(it, cx, cy, cel_cx, cel_cy, show_celestial);
  if (show_celestial && cel_cx > 0 && cel_cy > 0) {
    if (is_night) render_lunar_disc(it, cel_cx, cel_cy, LATTER_ROUND_DISPLAY ? 24 : scale_min(30), moon_cycle);
    else if (is_clear_day || is_partly || is_windy || is_fog_mist) render_solar_orb_disc(it, cel_cx, cel_cy, is_fog_mist);
  }

  const float w_scale = static_cast<float>(SCREEN_W) / 466.0f;
  if (is_clear_day) {
    const UiColor c_cir_crest(52, 86, 124), c_cir_body(38, 66, 100), c_cir_base(30, 54, 84);
    render_wispy_cloud_veil(it, static_cast<int>(318.0f * w_scale), 102, static_cast<int>(118.0f * w_scale), 0.4f, 11, c_cir_crest, c_cir_body, c_cir_base, cy);
    render_wispy_cloud_veil(it, static_cast<int>(158.0f * w_scale), 174, static_cast<int>(126.0f * w_scale), 1.4f, 12, c_cir_crest, c_cir_body, c_cir_base, cy);
  } else if (is_partly && !is_rain_precip && !is_snow_precip && !is_storm) {
    const bool sun_on_left = (cel_cx > 0 && cel_cx < cx);
    render_cumulus_cloud(it, static_cast<int>((sun_on_left ? 356.0f : 108.0f) * w_scale), 106, 0.94f, false, is_night, 0);
    render_cumulus_cloud(it, static_cast<int>((sun_on_left ? 132.0f : 344.0f) * w_scale), 196, 0.84f, false, is_night, 1);
    render_cumulus_cloud(it, static_cast<int>((sun_on_left ? 348.0f : 114.0f) * w_scale), 262, 0.82f, false, is_night, 2);
  } else if (is_storm) {
    const UiColor c_scud_crest = is_night ? UiColor(58, 76, 98) : UiColor(102, 128, 158);
    const UiColor c_scud_body  = is_night ? UiColor(36, 50, 68) : UiColor(68, 90, 116);
    const UiColor c_scud_base  = is_night ? UiColor(20, 30, 44) : UiColor(42, 58, 78);
    render_cumulus_cloud(it, static_cast<int>(152.0f * w_scale), 76, 1.16f, true, is_night, 2);
    render_wispy_cloud_veil(it, static_cast<int>(248.0f * w_scale), 96, static_cast<int>(165.0f * w_scale), 0.6f, 21, c_scud_crest, c_scud_body, c_scud_base, cy);
    render_wispy_cloud_veil(it, static_cast<int>(188.0f * w_scale), 162, static_cast<int>(172.0f * w_scale), 1.9f, 22, c_scud_crest, c_scud_body, c_scud_base, cy);
    render_cumulus_cloud(it, static_cast<int>(342.0f * w_scale), 118, 0.98f, true, is_night, 5);
    render_cumulus_cloud(it, static_cast<int>(112.0f * w_scale), 188, 0.88f, true, is_night, 6);
  } else if (is_overcast_canopy) {
    UiColor c_wisp_crest = is_night ? UiColor(70, 90, 116) : (is_rain_precip ? UiColor(106, 132, 162) : UiColor(134, 162, 192));
    UiColor c_wisp_body  = is_night ? UiColor(44, 60, 80)  : (is_rain_precip ? UiColor(72, 94, 120)   : UiColor(92, 118, 146));
    UiColor c_wisp_base  = is_night ? UiColor(26, 36, 50)  : (is_rain_precip ? UiColor(44, 60, 82)    : UiColor(56, 76, 100));
    if (!is_night && is_pouring_precip) {
      c_wisp_crest = scale_color(c_wisp_crest, 0.82f);
      c_wisp_body  = scale_color(c_wisp_body,  0.82f);
      c_wisp_base  = scale_color(c_wisp_base,  0.80f);
    }
    render_cumulus_cloud(it, static_cast<int>(178.0f * w_scale),  86, 1.12f, true, is_night, 0);
    render_cumulus_cloud(it, static_cast<int>(340.0f * w_scale), 122, 1.06f, true, is_night, 1);
    render_cumulus_cloud(it, static_cast<int>(240.0f * w_scale), 208, 1.14f, true, is_night, 2);
    render_wispy_cloud_veil(it, static_cast<int>(238.0f * w_scale), 112, static_cast<int>(165.0f * w_scale), 0.5f, 1, c_wisp_crest, c_wisp_body, c_wisp_base, cy);
    render_wispy_cloud_veil(it, static_cast<int>(186.0f * w_scale), 174, static_cast<int>(175.0f * w_scale), 1.7f, 2, c_wisp_crest, c_wisp_body, c_wisp_base, cy);
    render_cumulus_cloud(it, static_cast<int>(116.0f * w_scale), 168, 0.96f, true, is_night, 3);
    render_cumulus_cloud(it, static_cast<int>(346.0f * w_scale), 252, 0.92f, true, is_night, 4);
    render_wispy_cloud_veil(it, static_cast<int>(228.0f * w_scale), 270, static_cast<int>(158.0f * w_scale), 3.4f, 3, c_wisp_crest, c_wisp_body, c_wisp_base, cy);
  }

  if (is_fog_mist) {
    render_cumulus_cloud(it, static_cast<int>(125.0f * w_scale), 102, 0.90f, true, is_night, 5);
    render_cumulus_cloud(it, static_cast<int>(340.0f * w_scale), 188, 0.85f, true, is_night, 6);
    render_cumulus_cloud(it, static_cast<int>(155.0f * w_scale), 266, 0.80f, true, is_night, 7);
    static constexpr int16_t kPlumeY[5]  = { 76, 128, 178, 232, 284};
    static constexpr int16_t kPlumeX0[5] = {198, 264, 212, 252, 224};
    static constexpr int16_t kPlumeHw[5] = {155, 175, 168, 180, 148};
    const UiColor c_wisp_crest = is_night ? UiColor(68, 86, 108) : UiColor(124, 148, 174);
    const UiColor c_wisp_body  = is_night ? UiColor(44, 58,  76) : UiColor(84, 106, 130);
    const UiColor c_wisp_base  = is_night ? UiColor(26, 36,  48) : UiColor(54, 72,  92);
    for (int p = 0; p < 5; p++) {
      render_wispy_cloud_veil(it, static_cast<int>(static_cast<float>(kPlumeX0[p]) * w_scale), kPlumeY[p],
                              static_cast<int>(static_cast<float>(kPlumeHw[p]) * w_scale),
                              static_cast<float>(p) * 1.7f, p, c_wisp_crest, c_wisp_body, c_wisp_base, cy);
    }
  }
}

void blit_or_bake_sky_base(Rgb565Surface &it, int cx, int cy, std::string_view cond, bool is_night,
                           double moon_cycle, int cel_cx, int cel_cy, bool show_celestial) {
  uint16_t *fb = active_frame_buf16();
  auto &cache = sky_base_cache();
  if (fb != nullptr && cache.px == nullptr) {
#if !LATTER_CHIP_P4
    art_canvas_drop_all();
#endif
    const size_t bytes = static_cast<size_t>(SCREEN_W) * static_cast<size_t>(SCREEN_H) * sizeof(uint16_t);
    cache.px = static_cast<uint16_t *>(ui_psram_malloc(bytes));
    cache.valid = false;
  }

  const uint32_t cond_hash = fnv1a_sky_str(cond);
  const uint8_t quant_moon = (show_celestial && is_night)
      ? static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::lround(moon_cycle * 64.0)), 0, 64)) : 0U;
  const uint8_t cur_dim_mode = static_cast<uint8_t>(active_frame_endian_mode() & 0x0F);
  const int16_t key_cel_x = show_celestial ? static_cast<int16_t>(cel_cx) : static_cast<int16_t>(0);
  const int16_t key_cel_y = show_celestial ? static_cast<int16_t>(cel_cy) : static_cast<int16_t>(0);

  if (fb != nullptr && cache.px != nullptr) {
    if (!cache.valid || cache.cond_hash != cond_hash || cache.is_night != is_night ||
        cache.show_celestial != show_celestial || cache.cel_cx != key_cel_x ||
        cache.cel_cy != key_cel_y || cache.quantized_moon_phase != quant_moon ||
        cache.dim_mode != cur_dim_mode) {
      const int saved_y0 = clip_y0(), saved_y1 = clip_y1();
      reset_clip_y_band();
      active_frame_buf16() = cache.px;
      bake_static_sky_base(it, cx, cy, cond, is_night, moon_cycle, cel_cx, cel_cy, show_celestial);
      active_frame_buf16() = fb;
      set_clip_y_band(saved_y0, saved_y1);

      cache.valid = true;
      cache.cond_hash = cond_hash;
      cache.is_night = is_night;
      cache.show_celestial = show_celestial;
      cache.cel_cx = key_cel_x;
      cache.cel_cy = key_cel_y;
      cache.quantized_moon_phase = quant_moon;
      cache.dim_mode = cur_dim_mode;
    }

    frame_base_claim();
    const int y0 = std::max(0, clip_y0()), y1 = std::min(SCREEN_H - 1, clip_y1());
    if (y0 <= y1) {
#if LATTER_ROUND_DISPLAY
      for (int y = y0; y <= y1; ++y) {
        const int hw = circle_half_span(y, 0) + 2;
        const int x0 = std::max(0, SCREEN_CX - hw), x1 = std::min(SCREEN_W - 1, SCREEN_CX + hw);
        if (x0 <= x1) {
          const size_t off = static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W) + static_cast<size_t>(x0);
          std::memcpy(fb + off, cache.px + off, static_cast<size_t>(x1 - x0 + 1) * sizeof(uint16_t));
        }
      }
#else
      const size_t off = static_cast<size_t>(y0) * static_cast<size_t>(SCREEN_W);
      std::memcpy(fb + off, cache.px + off, static_cast<size_t>(y1 - y0 + 1) * static_cast<size_t>(SCREEN_W) * sizeof(uint16_t));
#endif
    }
  } else {
    frame_base_claim();
    bake_static_sky_base(it, cx, cy, cond, is_night, moon_cycle, cel_cx, cel_cy, show_celestial);
  }
}

WeatherParticleEngine &particle_engine() {
  static WeatherParticleEngine eng{};
  return eng;
}

void init_particle_engine(WeatherParticleEngine &eng, uint32_t cond_hash, RainTier tier) {
  eng.inited = true;
  eng.cond_hash = cond_hash;
  init_rain_streaks(eng, tier);
  const float flake_scale = std::max(1.0f, static_cast<float>(std::min(SCREEN_W, SCREEN_H)) * (1.0f / 420.0f));
  const float cycle_h = static_cast<float>(SCREEN_H) + 20.0f * flake_scale;
  for (int i = 0; i < WeatherParticleEngine::kSnowCount; i++) {
    eng.snow_pass[i] = 0U;
    eng.snow_y[i] = std::fmod(static_cast<float>(i) * 0.61803399f + 0.13f, 1.0f) * cycle_h;
  }
  for (int i = 0; i < WeatherParticleEngine::kLeafCount; i++) {
    eng.leaf_x[i] = hash2d_01(i, 7) * 480.0f - 240.0f;
  }
  const float gust_wrap = static_cast<float>(SCREEN_W + 94);
  for (int g = 0; g < 3; g++) {
    eng.gust_x[g] = static_cast<float>(g) * (gust_wrap * (1.0f / 3.0f)) - 50.0f;
  }
}

}  // namespace

void weather_cloud_cache_drop() noexcept {
  auto &cc = weather_cloud_cache();
  if (cc.entries != nullptr) {
    for (int i = 0; i < kCloudShapeEntries; ++i) {
      if (cc.entries[i].runs != nullptr) {
        ui_psram_free(cc.entries[i].runs);
        cc.entries[i].runs = nullptr;
      }
    }
    ui_psram_free(cc.entries);
    cc.entries = nullptr;
  }
  if (cc.scratch_runs != nullptr) { ui_psram_free(cc.scratch_runs); cc.scratch_runs = nullptr; }
  if (cc.build_cols != nullptr) { ui_psram_free(cc.build_cols); cc.build_cols = nullptr; }
  if (cc.cumulus_cols != nullptr) { ui_psram_free(cc.cumulus_cols); cc.cumulus_cols = nullptr; }
  if (cc.veil_cols != nullptr) { ui_psram_free(cc.veil_cols); cc.veil_cols = nullptr; }
  cc.use_seq = 0U;
}

void sky_base_cache_drop() noexcept {
  auto &cache = sky_base_cache();
  if (cache.px != nullptr) {
    ui_psram_free(cache.px);
    cache.px = nullptr;
  }
  cache.valid = false;
  lunar_disc_cache_drop();
}

bool &weather_sim_dirty() {
  static bool dirty = true;
  return dirty;
}

bool has_weather_motion_sv(std::string_view cond, bool is_night, uint32_t now_ms) {
  if (cond.find("rain") != std::string_view::npos || cond.find("pouring") != std::string_view::npos ||
      cond.find("drizzle") != std::string_view::npos || cond.find("snow") != std::string_view::npos ||
      cond.find("sleet") != std::string_view::npos || cond.find("hail") != std::string_view::npos ||
      cond.find("lightning") != std::string_view::npos || cond.find("thunder") != std::string_view::npos ||
      cond.find("wind") != std::string_view::npos || cond.find("breez") != std::string_view::npos ||
      cond.find("fog") != std::string_view::npos || cond.find("mist") != std::string_view::npos ||
      cond.find("haze") != std::string_view::npos || cond.find("cloud") != std::string_view::npos ||
      cond.find("overcast") != std::string_view::npos || cond.find("partly") != std::string_view::npos ||
      cond.find("mostlysunny") != std::string_view::npos || cond.find("partlysunny") != std::string_view::npos) {
    return true;
  }
  if (is_night && (cond == "clear-night" || cond == "clearnight" ||
                   cond == "sunny" || cond == "clear" || cond == "clear-day")) {
    return is_night_meteor_active(now_ms);
  }
  return !is_night && (cond == "sunny" || cond == "clear-day" || cond == "clear" || cond == "clearday");
}

bool has_weather_motion(const std::string &cond, bool is_night, uint32_t now_ms) {
  return has_weather_motion_sv(cond, is_night, now_ms);
}

bool evaluate_sky_has_motion(std::string_view sim_opt, std::string_view raw_cond,
                             std::string_view sun_state, uint32_t now_ms) {
  const bool sim_active = (!sim_opt.empty() && sim_opt != "Live (Home Assistant)");
  const bool sun_ok = (!sun_state.empty() && sun_state != "unknown" && sun_state != "unavailable");
  if (!sun_ok && !sim_active) return false;
  std::string_view cond = raw_cond;
  bool is_night = (sun_state == "below_horizon" || cond == "clear-night" || cond == "clearnight" ||
                   (cond.find("night") != std::string_view::npos));
  resolve_weather_sim_view(sim_opt, cond, is_night);
  return has_weather_motion_sv(cond, is_night, now_ms);
}

float weather_precip_alpha_scale() {
  return sky_fade_state().precip_alpha_scale;
}

bool weather_sky_motion_stepping(bool usb_connected, uint32_t now_ms, uint32_t latest_input_ms,
                                 bool has_motion_cond) {
  if (!has_motion_cond || touch_gesture().active) return false;
  if (battery_motion_allowed(usb_connected, now_ms, latest_input_ms)) return true;
  const auto &st = sky_fade_state();
  return (st.precip_alpha_scale > 0.001f) || !st.calm_frame_rendered;
}

void render_static_day_night_sky_base(Rgb565Surface &it, int cx, int cy, bool is_night) {
  backdrop_active() = true;
  blit_or_bake_sky_base(it, cx, cy, is_night ? std::string_view("clear-night") : std::string_view("sunny"),
                        is_night, 0.5, 0, 0, false);
}

void render_weather_motion_wallpaper(Rgb565Surface &it, int cx, int cy, uint32_t now_ms,
                                     const std::string &cond_in, bool is_night,
                                     double moon_cycle, int cel_cx, int cel_cy,
                                     bool mask_hero_temp, float accel_x, float accel_y,
                                     bool show_celestial) {
  (void)mask_hero_temp;
  const std::string_view cond(cond_in);
  if (cond.empty()) return;

  ProfLap lap;
  const auto &rs = runtime_state();
  const uint32_t latest_input_ms = std::max(touch_gesture().last_touch_ms, ui_focus_last_ms());
  step_weather_battery_fade(rs.hw_usb_connected, now_ms, latest_input_ms);
  float precip_alpha = sky_fade_state().precip_alpha_scale;
  if (precip_alpha <= 0.001f) {
    precip_alpha = 0.0f;
    sky_fade_state().precip_alpha_scale = 0.0f;
    sky_fade_state().calm_frame_rendered = true;
  }

  static float par_x = 0.0f, par_y = 0.0f;
  if (!std::isnan(accel_x) && !std::isnan(accel_y)) {
    par_x += (std::clamp(accel_x * 20.0f, -18.0f, 18.0f) - par_x) * 0.22f;
    par_y += (std::clamp(accel_y * 20.0f, -18.0f, 18.0f) - par_y) * 0.22f;
  }

  blit_or_bake_sky_base(it, cx, cy, cond, is_night, moon_cycle, cel_cx, cel_cy, show_celestial);
  uint16_t *fb = active_frame_buf16();
  const uint32_t cond_hash = fnv1a_sky_str(cond);
  lap.mark(ProfSect::BG_SKY);

  if (precip_alpha <= 0.0f) {
    lap.mark(ProfSect::BG_PRECIP);
    return;
  }

  const bool is_fog_mist = (cond.find("fog") != std::string_view::npos ||
                            cond.find("mist") != std::string_view::npos ||
                            cond.find("haze") != std::string_view::npos);
  const bool is_windy = (cond.find("wind") != std::string_view::npos || cond.find("breez") != std::string_view::npos);
  const bool is_partly = (cond.find("partly") != std::string_view::npos ||
                          cond.find("mostlysunny") != std::string_view::npos ||
                          cond.find("partlysunny") != std::string_view::npos);
  const bool is_storm = (cond.find("lightning") != std::string_view::npos || cond.find("thunder") != std::string_view::npos);
  const bool is_rain = (cond.find("rain") != std::string_view::npos ||
                        cond.find("pouring") != std::string_view::npos ||
                        cond.find("drizzle") != std::string_view::npos || is_storm);
  const RainTier rain_tier = classify_rain_tier(cond, is_storm);
  const bool is_snow = (cond.find("snow") != std::string_view::npos ||
                        cond.find("sleet") != std::string_view::npos ||
                        cond.find("hail") != std::string_view::npos);
  const bool is_clear_or_partly_night =
      (is_night && (cond == "clear-night" || cond == "clearnight" ||
                    cond == "sunny" || cond == "clear" || cond == "clear-day" || is_partly));

  auto &eng = particle_engine();
  float dt_s = 0.0f;
  if (!eng.inited || eng.cond_hash != cond_hash) {
    init_particle_engine(eng, cond_hash, rain_tier);
    eng.last_step_ms = now_ms;
  } else if (now_ms != eng.last_step_ms) {
    const uint32_t raw_dt = now_ms - eng.last_step_ms;
    eng.last_step_ms = now_ms;
    dt_s = std::clamp(static_cast<float>(raw_dt) * 0.001f, 0.0f, 0.12f);
  }
  eng.phase_s += dt_s;
  if (eng.phase_s > 62831.853f) eng.phase_s -= 62831.853f;

  if (is_clear_or_partly_night) {
    for (int k = 0; k < 24; k++) {
      const int idx = k * 3;
      const float tw = 0.5f + 0.5f * fast_sin(eng.phase_s * 2.4f + static_cast<float>(idx) * 0.9f);
      if (tw < 0.55f) continue;
      const float r_norm = std::sqrt((static_cast<float>(idx) + 0.5f) * (1.0f / 72.0f));
      const float r_px = 18.0f + r_norm * (static_cast<float>(std::max(SCREEN_W, SCREEN_H)) * 0.65f);
      const float ang = static_cast<float>(idx) * 2.39996323f + (hash2d_01(idx, 19) - 0.5f) * 0.18f;
      float sa = 0.0f, ca = 1.0f;
      fast_sincos(ang, sa, ca);
      const int sx = cx + static_cast<int>(ca * r_px), sy = cy + static_cast<int>(sa * r_px);
      if (!is_inside_bezel(sx, sy, cy, 6)) continue;
      const int excl_r = LATTER_ROUND_DISPLAY ? 30 : scale_min(36);
      if (show_celestial && cel_cx > 0 && cel_cy > 0 &&
          (sx - cel_cx) * (sx - cel_cx) + (sy - cel_cy) * (sy - cel_cy) <= excl_r * excl_r) {
        continue;
      }
      blend_sky_pixel_q8(it, fb, sx, sy, UiColor(255, 255, 255), static_cast<int>((tw - 0.5f) * 220.0f * precip_alpha));
    }
    if (precip_alpha > 0.35f) render_night_shooting_star(it, now_ms, cy);
  }

  if (!is_rain && !is_snow && !is_windy) {
    const int drift_bands = is_fog_mist ? 3 : 1;
    const UiColor haze_c = is_night ? UiColor(110, 140, 178) : UiColor(188, 216, 246);
    const int base_q8 = static_cast<int>((is_fog_mist ? 26.0f : 14.0f) * precip_alpha);
    if (base_q8 > 1 && fb != nullptr) {
      for (int b = 0; b < drift_bands; b++) {
        const float dir = (b & 1) ? -1.0f : 1.0f;
        const int center_x = cx + static_cast<int>(
            fast_sin(eng.phase_s * 0.45f * dir + static_cast<float>(b) * 1.9f) * static_cast<float>(scale_x(64)) + par_x * 0.35f);
        const int center_y = scale_y(118 + b * 68) + static_cast<int>(par_y * 0.25f);
        const int hw = scale_x(110), hh = scale_y(8);
        const int y_lo = std::max(clip_y0(), center_y - hh), y_hi = std::min(clip_y1(), center_y + hh);
        for (int py = y_lo; py <= y_hi; py += 2) {
          const float vy_u = static_cast<float>(py - center_y) / static_cast<float>(std::max(1, hh));
          const float row_fade = 1.0f - vy_u * vy_u;
          if (row_fade <= 0.05f) continue;
          const int row_a = static_cast<int>(static_cast<float>(base_q8) * row_fade);
          if (row_a <= 1) continue;
          const int x_lo = std::max(8, center_x - hw), x_hi = std::min(SCREEN_W - 9, center_x + hw);
          for (int px = x_lo; px <= x_hi; px += 2) {
            if (is_inside_bezel(px, py, cy, 8)) blend_sky_pixel_q8(it, fb, px, py, haze_c, row_a);
          }
        }
      }
    }
  }

  if (is_storm) render_storm_lightning_fx(it, fb, cx, cy, now_ms, precip_alpha, eng.lightning);
  lap.mark(ProfSect::BG_CLOUDS);

  if (is_windy) render_wind_gusts_and_leaves(it, fb, cy, is_night, dt_s, precip_alpha, par_x, par_y, eng);
  if (is_rain) render_rain_layers_fx(it, fb, cy, is_night, rain_tier, dt_s, precip_alpha, par_x, par_y, eng);
  if (is_snow) render_snowfall_fx(it, fb, cy, is_night, dt_s, precip_alpha, par_x, par_y, eng);
  lap.mark(ProfSect::BG_PRECIP);
}

}  // namespace latter_ui
