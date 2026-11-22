#include "weather_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace latter_ui {

namespace {

void draw_lightning_seg_q8(Rgb565Surface &it, uint16_t *fb,
                           int tx, int ty, int hx, int hy,
                           UiColor c_core, UiColor c_glow,
                           int a0_q8, int a1_q8,
                           int core_hw, int glow_r, int cy) {
  if (a0_q8 <= 0 && a1_q8 <= 0) return;
  const int span = std::max(1, glow_r - core_hw + 1);
  const float inv_span = 1.0f / static_cast<float>(span);
  for (int ox = -glow_r; ox <= glow_r; ox++) {
    const int aox = std::abs(ox);
    if (aox <= core_hw) continue;
    const float u = 1.0f - static_cast<float>(aox - core_hw) * inv_span;
    const float k = 0.54f * u * u;
    draw_tapered_streak_q8(it, fb, tx + ox, ty, hx + ox, hy, c_glow, c_glow,
                           static_cast<int>(static_cast<float>(a0_q8) * k),
                           static_cast<int>(static_cast<float>(a1_q8) * k), cy);
  }
  for (int ox = -core_hw; ox <= core_hw; ox++) {
    const float k = (ox == 0) ? 1.0f : 0.90f;
    const UiColor c_end = (ox == 0 || a1_q8 > 0) ? c_core : c_glow;
    draw_tapered_streak_q8(it, fb, tx + ox, ty, hx + ox, hy, c_core, c_end,
                           static_cast<int>(static_cast<float>(a0_q8) * k),
                           static_cast<int>(static_cast<float>(a1_q8) * k), cy);
  }
}

void draw_soft_snowflake_q8(Rgb565Surface &it, uint16_t *fb,
                            int sx, int sy, float r_px, int peak_q8,
                            bool is_crystal, bool day_contrast,
                            UiColor c_core, UiColor c_arm, int cy) {
  if (peak_q8 <= 1 || r_px <= 0.4f) return;
  const float r_bound = is_crystal ? (r_px * 1.28f) : r_px;
  const int r_i = static_cast<int>(std::ceil(r_bound));
  const int y_lo = std::max(clip_y0(), sy - r_i);
  const int y_hi = std::min(clip_y1(), sy + r_i);
  if (y_lo > y_hi || sx + r_i < 0 || sx - r_i >= SCREEN_W) return;

  static constexpr int kSkyInset = LATTER_ROUND_DISPLAY ? 5 : 0;
  const float inv_r2 = 1.0f / (r_px * r_px);
  const float inv_rc2 = 1.0f / (r_bound * r_bound);

  for (int py = y_lo; py <= y_hi; py++) {
    const int dy = py - sy;
    const int dy2 = dy * dy;
    const int x_lo = std::max(0, sx - r_i);
    const int x_hi = std::min(SCREEN_W - 1, sx + r_i);
    for (int px = x_lo; px <= x_hi; px++) {
      if (!is_inside_bezel(px, py, cy, kSkyInset)) continue;
      const int dx = px - sx;
      const int d2_i = dx * dx + dy2;
      const float d2 = static_cast<float>(d2_i) * inv_r2;

      float cov = 0.0f;
      if (d2 < 1.0f) {
        const float u = 1.0f - d2;
        cov = u * (0.42f + 0.58f * u);
      }
      if (is_crystal) {
        const int adx = std::abs(dx), ady = std::abs(dy);
        if (dx == 0 || dy == 0) {
          const float dc2 = static_cast<float>(d2_i) * inv_rc2;
          if (dc2 < 1.0f) {
            const float uc = 1.0f - dc2;
            cov = std::max(cov, uc * uc * 0.78f);
          }
        } else if (adx == ady) {
          const float dc2 = static_cast<float>(d2_i) * (inv_rc2 * 1.18f);
          if (dc2 < 1.0f) {
            const float uc = 1.0f - dc2;
            cov = std::max(cov, uc * uc * 0.56f);
          }
        }
      }
      if (cov <= 0.02f) continue;

      if (day_contrast && cov < 0.42f) {
        const int rim_q8 = static_cast<int>((0.42f - cov) * 58.0f);
        if (rim_q8 > 1) blend_sky_pixel_q8(it, fb, px, py, kChromeShadow, rim_q8);
      }
      const int a_q8 = std::min(256, static_cast<int>(static_cast<float>(peak_q8) * cov));
      if (a_q8 <= 1) continue;
      blend_sky_pixel_q8(it, fb, px, py, (cov >= 0.52f) ? c_core : c_arm, a_q8);
    }
  }
}

void draw_tumbling_leaf_3d(Rgb565Surface &it, Vec3 center_3d, float rx, float ry, float rz,
                           float scale, UiColor col_base, UiColor col_vein, int cy) {
  const Mat3 rot = Mat3::from_euler(rx, ry, rz);
  auto xform = [&](Vec3 local) -> ProjectedPoint {
    const Vec3 r = rot.transform({local.x * scale, local.y * scale, local.z * scale});
    return project_3d({center_3d.x + r.x, center_3d.y + r.y, center_3d.z + r.z}, SCREEN_CX, cy, 250.0f, 320.0f);
  };

  const ProjectedPoint p_tip  = xform({ 9.5f,  0.0f,  0.0f});
  const ProjectedPoint p_base = xform({-8.5f,  0.0f,  0.0f});
  const ProjectedPoint p_up   = xform({-1.5f, -5.6f,  1.8f});
  const ProjectedPoint p_dn   = xform({-1.5f,  5.6f, -1.8f});
  const ProjectedPoint p_stem = xform({-13.0f, 1.2f,  0.0f});
  if (!p_tip.visible || !p_base.visible) return;

  const Vec3 norm = rot.normal_z();
  const float glint = 0.78f + 0.34f * std::fmax(0.0f, norm.z * 0.7f - norm.y * 0.5f);
  const UiColor c_blade = scale_color(col_base, std::fmin(1.15f, glint));

  fast_filled_triangle(it, p_base.x, p_base.y, p_up.x, p_up.y, p_tip.x, p_tip.y, c_blade);
  fast_filled_triangle(it, p_base.x, p_base.y, p_dn.x, p_dn.y, p_tip.x, p_tip.y, scale_color(c_blade, 0.85f));
  fast_line(it, p_base.x, p_base.y, p_up.x, p_up.y, col_vein);
  fast_line(it, p_up.x,   p_up.y,   p_tip.x, p_tip.y, col_vein);
  fast_line(it, p_base.x, p_base.y, p_dn.x, p_dn.y, c_blade);
  fast_line(it, p_dn.x,   p_dn.y,   p_tip.x, p_tip.y, c_blade);
  fast_line(it, p_base.x, p_base.y, p_tip.x, p_tip.y, col_vein);
  if (p_stem.visible) fast_line(it, p_base.x, p_base.y, p_stem.x, p_stem.y, col_vein);
}

void render_forecast_glyph(Rgb565Surface &it, int gx, int gy, const std::string &c_name) {
  auto S = [](int v) -> int { return scale_s(v); };
  auto SR = [](int v) -> int { return std::max(1, scale_s(v)); };
  const int thick = (SCREEN_MIN_DIM > 520) ? 2 : 1;

  if (c_name == "sunny" || c_name == "clear-day" || c_name == "clear" || c_name == "clearday") {
    fast_filled_circle_aa(it, gx, gy, SR(7), UiColor(250, 185, 30));
    fast_circle_aa(it, gx, gy, static_cast<float>(SR(10)), UiColor(255, 215, 80), static_cast<float>(thick));
    const UiColor ray_c(255, 225, 100);
    for (int t = 0; t < thick; ++t) {
      fast_line(it, gx + t, gy - S(12), gx + t, gy - S(10), ray_c);
      fast_line(it, gx + t, gy + S(10), gx + t, gy + S(12), ray_c);
      fast_line(it, gx - S(12), gy + t, gx - S(10), gy + t, ray_c);
      fast_line(it, gx + S(10), gy + t, gx + S(12), gy + t, ray_c);
    }
  } else if (c_name == "clear-night" || c_name == "clearnight") {
    const UiColor moon_c(225, 235, 250);
    const int ro = SR(8), rc = SR(7), ox_c = S(2), cx_c = S(1);
    for (int dy = -ro; dy <= ro; dy++) {
      for (int dx = -ro + ox_c; dx <= ro + ox_c; dx++) {
        const int ox = dx - ox_c;
        if (ox * ox + dy * dy <= ro * ro) {
          const int cx_d = dx + cx_c, cy_d = dy + cx_c;
          if (cx_d * cx_d + cy_d * cy_d > rc * rc) fast_pixel(it, gx + dx, gy + dy, moon_c);
        }
      }
    }
  } else if (c_name == "partlycloudy" || c_name == "partly-cloudy" || c_name == "mostlycloudy" ||
             c_name == "mostlysunny" || c_name == "partlysunny") {
    fast_filled_circle_aa(it, gx - S(4), gy - S(4), SR(6), UiColor(245, 175, 30));
    fast_filled_circle_aa(it, gx + S(2), gy + S(1), SR(6), UiColor(175, 190, 210));
    fast_filled_circle_aa(it, gx - S(3), gy + S(2), SR(5), UiColor(155, 170, 190));
    fast_filled_circle_aa(it, gx + S(7), gy + S(3), SR(4), UiColor(145, 160, 180));
  } else if (c_name == "rainy" || c_name == "pouring" || c_name == "drizzle" || c_name == "heavy-rain") {
    fast_filled_circle_aa(it, gx, gy - S(2), SR(6), UiColor(130, 145, 170));
    fast_filled_circle_aa(it, gx - S(5), gy - S(1), SR(5), UiColor(115, 130, 155));
    fast_filled_circle_aa(it, gx + S(5), gy, SR(4), UiColor(115, 130, 155));
    for (int t = 0; t < thick; ++t) {
      fast_line(it, gx - S(4) + t, gy + S(6), gx - S(6) + t, gy + S(10), UiColor(70, 170, 245));
      fast_line(it, gx + S(2) + t, gy + S(6), gx + t, gy + S(10), UiColor(70, 170, 245));
    }
  } else if (c_name == "snowy" || c_name == "snowy-rainy" || c_name == "sleet" || c_name == "hail") {
    fast_filled_circle_aa(it, gx, gy - S(2), SR(6), UiColor(160, 175, 195));
    fill_rect_row_major(it, gx - S(3), gy + S(7), thick, thick, UiColor(255, 255, 255));
    fill_rect_row_major(it, gx + S(3), gy + S(7), thick, thick, UiColor(255, 255, 255));
    fill_rect_row_major(it, gx, gy + S(9), thick, thick, UiColor(255, 255, 255));
  } else if (c_name == "lightning" || c_name == "lightning-rainy" || c_name == "thunderstorm") {
    fast_filled_circle_aa(it, gx, gy - S(3), SR(6), UiColor(110, 120, 140));
    for (int t = 0; t < thick; ++t) {
      fast_line(it, gx - S(1) + t, gy + S(3), gx - S(3) + t, gy + S(7), UiColor(255, 220, 40));
      fast_line(it, gx - S(3), gy + S(7) + t, gx + S(1), gy + S(7) + t, UiColor(255, 220, 40));
      fast_line(it, gx + S(1) + t, gy + S(7), gx - S(1) + t, gy + S(11), UiColor(255, 220, 40));
    }
  } else if (c_name == "windy" || c_name == "windy-variant" || c_name == "breezy") {
    for (int t = 0; t < thick; ++t) {
      fast_line(it, gx - S(9), gy - S(3) + t, gx + S(5), gy - S(3) + t, UiColor(160, 190, 220));
      fast_line(it, gx - S(6), gy + S(1) + t, gx + S(8), gy + S(1) + t, UiColor(180, 210, 235));
      fast_line(it, gx - S(4), gy + S(5) + t, gx + S(4), gy + S(5) + t, UiColor(140, 170, 200));
    }
    fast_line(it, gx + S(6), gy - S(5), gx + S(9), gy - S(3), UiColor(125, 195, 95));
    fast_line(it, gx + S(6), gy - S(4), gx + S(9), gy - S(3), UiColor(230, 185, 55));
  } else if (c_name == "fog" || c_name == "foggy" || c_name == "mist" || c_name == "misty" || c_name == "haze") {
    fast_filled_circle_aa(it, gx, gy - S(4), SR(5), UiColor(135, 155, 175));
    for (int t = 0; t < thick; ++t) {
      fast_line(it, gx - S(9), gy - S(1) + t, gx + S(9), gy - S(1) + t, UiColor(185, 205, 225));
      fast_line(it, gx - S(7), gy + S(2) + t, gx + S(10), gy + S(2) + t, UiColor(155, 178, 200));
      fast_line(it, gx - S(10), gy + S(5) + t, gx + S(6), gy + S(5) + t, UiColor(130, 152, 175));
      fast_line(it, gx - S(5), gy + S(8) + t, gx + S(8), gy + S(8) + t, UiColor(105, 125, 148));
    }
  } else {
    fast_filled_circle_aa(it, gx, gy - S(1), SR(7), UiColor(165, 180, 200));
    fast_filled_circle_aa(it, gx - S(5), gy + S(1), SR(5), UiColor(145, 160, 180));
    fast_filled_circle_aa(it, gx + S(5), gy + S(1), SR(5), UiColor(145, 160, 180));
  }
}

void draw_weather_mode_icon(Rgb565Surface &it, int i, int bx, int by, int ir,
                            UiColor fg_c, bool shadow, void *) {
  if (i == 0) {
    render_mdi_icon(it, "mdi:weather-partly-cloudy", bx, by, std::max(16, 2 * ir), fg_c, shadow, "mdi:cloud");
  } else if (i == 1) {
    ui::draw_icon_forecast_chart(it, bx, by, ir, fg_c, shadow);
  } else if (i == 2) {
    ui::draw_icon_sun(it, bx, by, ir, fg_c, shadow);
  } else {
    ui::draw_icon_crescent_moon(it, bx, by, ir, fg_c, shadow);
  }
}

void render_weather_subpage_icons(Rgb565Surface &it, int active_subpage,
                                  UiColor col_active, UiColor col_inactive) {
  ui::mode_bar(it, SCREEN_CX, 4, active_subpage, 0, col_active, col_inactive,
               draw_weather_mode_icon, nullptr);
}

}  // namespace

namespace {

struct RainLayerSpec {
  float vy_base, vy_span, len_base, len_span, alpha_base, alpha_span;
};

struct RainTierSpec {
  int far_cnt, near_cnt;
  float slant, hero_tail;
  RainLayerSpec far_l, hero_l;
};

static constexpr RainTierSpec kRainTiers[4] = {
    {20, 18, 0.08f, 18.0f, {185.0f, 35.0f, 26.0f, 10.0f, 42.0f, 16.0f}, {285.0f, 45.0f, 38.0f, 14.0f, 138.0f, 30.0f}},
    {36, 38, 0.12f, 24.0f, {235.0f, 45.0f, 32.0f, 12.0f, 56.0f, 22.0f}, {372.0f, 58.0f, 46.0f, 18.0f, 168.0f, 36.0f}},
    {48, 52, 0.18f, 28.0f, {310.0f, 55.0f, 36.0f, 14.0f, 68.0f, 28.0f}, {475.0f, 70.0f, 50.0f, 18.0f, 188.0f, 38.0f}},
    {56, 64, 0.21f, 34.0f, {345.0f, 60.0f, 38.0f, 16.0f, 78.0f, 32.0f}, {525.0f, 80.0f, 52.0f, 18.0f, 202.0f, 40.0f}},
};

constexpr int gcd_int(int a, int b) noexcept {
  while (b != 0) { const int t = a % b; a = b; b = t; }
  return a;
}

constexpr int coprime_lane_stride(int n) noexcept {
  if (n <= 2) return 1;
  int s = std::max(1, (n * 382) / 1000);
  while (gcd_int(s, n) != 1) { if (++s >= n) s = 1; }
  return s;
}

void spawn_rain_streak(RainStreak *pool, int active_cnt, int idx, bool is_hero,
                       RainTier tier, float slant, bool initial_spread) {
  RainStreak &st = pool[idx];
  const auto &ts = kRainTiers[static_cast<uint8_t>(tier)];
  const RainLayerSpec &ls = is_hero ? ts.hero_l : ts.far_l;
  const float w_scale = static_cast<float>(SCREEN_W) * (1.0f / 466.0f);
  const float h_scale = static_cast<float>(SCREEN_H) * (1.0f / 466.0f);
  const int seed = static_cast<int>(st.pass_seq * 61U + static_cast<uint32_t>(idx) * 97U + (is_hero ? 503U : 17U));
  st.vy = (ls.vy_base + hash2d_01(seed, 13) * ls.vy_span) * h_scale;
  st.len = (ls.len_base + hash2d_01(seed, 29) * ls.len_span) * h_scale;
  st.alpha_peak = static_cast<uint8_t>(ls.alpha_base + hash2d_01(seed, 59) * ls.alpha_span);

  const int lanes = std::max(1, active_cnt), stride = coprime_lane_stride(lanes), cand_step = std::max(1, lanes / 6);
  const float span_x = static_cast<float>(std::max(32, SCREEN_W - 8)) + static_cast<float>(SCREEN_H) * slant;
  const float lane_w = span_x / static_cast<float>(lanes);
  const float inv_rx = 1.0f / std::max(6.0f, (is_hero ? 18.0f : 14.0f) * w_scale);
  const float inv_ry = 1.0f / std::max(24.0f, st.len * 1.65f);
  const float t_look = (static_cast<float>(SCREEN_H) * 0.45f) / std::max(60.0f, st.vy);
  const int peer_limit = initial_spread ? idx : active_cnt;

  float best_cross = 4.0f, best_y = -st.len, best_score = -1.0f;
  for (int k = 0; k < 6; k++) {
    const int k_seed = seed + k * 131;
    const int lane = (idx * stride + static_cast<int>(st.pass_seq) * 11 + k * cand_step) % lanes;
    const float jx = hash2d_01(k_seed, 43), jy = hash2d_01(k_seed, 71);
    const float cross_c = 4.0f + (static_cast<float>(lane) + 0.12f + jx * 0.76f) * lane_w;
    const float y_cand = initial_spread
        ? (-st.len + std::fmod(static_cast<float>(idx) * 0.61803399f + static_cast<float>(k) * 0.1666667f + jy * 0.09f, 1.0f) *
                         (static_cast<float>(SCREEN_H) + st.len))
        : (-4.0f - (static_cast<float>(k) * 0.16f + jy * 0.24f) * (st.len * 1.45f + 28.0f * h_scale));

    float min_d2 = 1.0e9f;
    for (int j = 0; j < peer_limit; j++) {
      if (j == idx) continue;
      const RainStreak &o = pool[j];
      const float dc = (cross_c - (o.x + o.y * slant)) * inv_rx, dc2 = dc * dc;
      if (dc2 >= min_d2) continue;
      const float dy0 = (y_cand - o.y) * inv_ry;
      const float dy_mid = ((y_cand + st.vy * t_look) - (o.y + o.vy * t_look)) * inv_ry;
      const float pair_d2 = dc2 + std::min(dy0 * dy0, dy_mid * dy_mid);
      if (pair_d2 < min_d2) min_d2 = pair_d2;
    }
    if (min_d2 > best_score) { best_score = min_d2; best_cross = cross_c; best_y = y_cand; }
  }
  st.y = best_y;
  st.x = best_cross - best_y * slant;
}

}  // namespace

RainTier classify_rain_tier(std::string_view cond, bool is_storm) noexcept {
  if (cond.find("drizzle") != std::string_view::npos) return RainTier::Drizzle;
  if (cond == "pouring" || cond.find("heavy") != std::string_view::npos) return RainTier::Pouring;
  return is_storm ? RainTier::Storm : RainTier::Normal;
}

void init_rain_streaks(WeatherParticleEngine &eng, RainTier tier) {
  const auto &ts = kRainTiers[static_cast<uint8_t>(tier)];
  for (int i = 0; i < ts.far_cnt; i++) {
    eng.far_veil[i].pass_seq = 0U;
    spawn_rain_streak(eng.far_veil, ts.far_cnt, i, false, tier, ts.slant, true);
  }
  for (int i = 0; i < ts.near_cnt; i++) {
    eng.near_hero[i].pass_seq = 0U;
    spawn_rain_streak(eng.near_hero, ts.near_cnt, i, true, tier, ts.slant, true);
  }
}

void render_rain_layers_fx(Rgb565Surface &it, uint16_t *fb, int cy, bool is_night, RainTier tier,
                           float dt_s, float precip_alpha, float par_x, float par_y,
                           WeatherParticleEngine &eng) {
  const auto &ts = kRainTiers[static_cast<uint8_t>(tier)];
  const float rain_slant = ts.slant;
  const UiColor far_col   = is_night ? UiColor(126, 166, 214) : UiColor(156, 196, 238);
  const UiColor near_tail = is_night ? UiColor(112, 158, 212) : UiColor(142, 188, 236);
  const UiColor near_head = is_night ? UiColor(204, 234, 255) : UiColor(228, 246, 255);
  for (int i = 0; i < ts.far_cnt; i++) {
    RainStreak &st = eng.far_veil[i];
    st.y += st.vy * dt_s;
    st.x -= st.vy * rain_slant * dt_s;
    if (st.y - st.len > static_cast<float>(SCREEN_H + 2)) {
      st.pass_seq++;
      spawn_rain_streak(eng.far_veil, ts.far_cnt, i, false, tier, rain_slant, false);
    }
    const int hx = static_cast<int>(st.x + par_x * 0.25f), hy = static_cast<int>(st.y + par_y * 0.25f);
    draw_tapered_streak_q8(it, fb, hx + static_cast<int>(st.len * rain_slant), hy - static_cast<int>(st.len),
                           hx, hy, far_col, far_col,
                           static_cast<int>(static_cast<float>(st.alpha_peak) * 0.34f * precip_alpha),
                           static_cast<int>(static_cast<float>(st.alpha_peak) * precip_alpha), cy);
  }
  for (int i = 0; i < ts.near_cnt; i++) {
    RainStreak &st = eng.near_hero[i];
    st.y += st.vy * dt_s;
    st.x -= st.vy * rain_slant * dt_s;
    if (st.y - st.len > static_cast<float>(SCREEN_H + 2)) {
      st.pass_seq++;
      spawn_rain_streak(eng.near_hero, ts.near_cnt, i, true, tier, rain_slant, false);
    }
    const int hx = static_cast<int>(st.x + par_x * 0.72f), hy = static_cast<int>(st.y + par_y * 0.72f);
    draw_tapered_streak_q8(it, fb, hx + static_cast<int>(st.len * rain_slant), hy - static_cast<int>(st.len),
                           hx, hy, near_tail, near_head,
                           static_cast<int>(ts.hero_tail * precip_alpha),
                           static_cast<int>(static_cast<float>(st.alpha_peak) * precip_alpha), cy);
  }
}

void render_storm_lightning_fx(Rgb565Surface &it, uint16_t *fb, int cx, int cy,
                               uint32_t now_ms, float precip_alpha, LightningFlashState &lt) {
  if (lt.next_flash_ms == 0U || static_cast<int32_t>(now_ms - lt.next_flash_ms) > 10000) {
    lt.next_flash_ms = now_ms + 850U;
    lt.active = false;
  }
  if (static_cast<int32_t>(now_ms - lt.next_flash_ms) >= 0) {
    lt.flash_start_ms = now_ms;
    lt.active = true;
    lt.flash_seq++;
    const uint32_t gap_ms =
        2600U + static_cast<uint32_t>(hash2d_01(static_cast<int>(lt.flash_seq * 37U + 11U), 29) * 3200.0f);
    lt.next_flash_ms = now_ms + gap_ms;
  }
  if (!lt.active) return;

  const uint32_t el_ms = now_ms - lt.flash_start_ms;
  if (el_ms >= 480U) {
    lt.active = false;
    return;
  }

  const float t_ms = static_cast<float>(el_ms);
  const float primary = (t_ms <= 75.0f)
      ? (1.0f - (t_ms * (0.14f / 75.0f)))
      : (0.86f * std::exp(-(t_ms - 75.0f) * (1.0f / 115.0f)));
  const bool has_restrike = ((lt.flash_seq % 3U) != 2U);
  const float dt_re = (t_ms - 185.0f) * (1.0f / 54.0f);
  const float restrike = has_restrike ? (0.88f * std::exp(-dt_re * dt_re)) : 0.0f;
  const float env = std::min(1.0f, std::max(primary, restrike)) * precip_alpha;
  const int sheet_q8 = static_cast<int>(env * 78.0f);
  if (sheet_q8 > 0 && fb != nullptr) {
    static constexpr uint8_t kFlashBayer4[4][4] = {
        {  0, 128,  32, 160},
        {192,  64, 224,  96},
        { 48, 176,  16, 144},
        {240, 112, 208,  80},
    };
    static constexpr int kFlashInset = LATTER_ROUND_DISPLAY ? 5 : 0;
    const uint16_t wash_rgb = ((190 >> 3) << 11) | ((220 >> 2) << 5) | (255 >> 3);
    const int sr = (wash_rgb >> 11) & 0x1F, sg = (wash_rgb >> 5) & 0x3F, sb = wash_rgb & 0x1F;
    const int y_lo = std::max(kFlashInset, clip_y0());
    const int y_hi = std::min(SCREEN_H - 1 - kFlashInset, clip_y1());
    const int span_h = std::max(1, SCREEN_H - 1 - 2 * kFlashInset);
    for (int py = y_lo; py <= y_hi; py++) {
      const int falloff_q8 = 256 - (((py - kFlashInset) * 152) / span_h);
      const int a = (sheet_q8 * std::max(0, falloff_q8)) >> 8;
      if (a <= 0) continue;
      const int inv = 256 - a;
      const uint8_t *bayer_row = kFlashBayer4[py & 3];
#if LATTER_ROUND_DISPLAY
      const int hw = circle_half_span(py, 5);
      if (hw <= 0) continue;
      const int x0 = std::max(0, cx - hw), x1 = std::min(SCREEN_W - 1, cx + hw);
#else
      (void)cx;
      const int x0 = 0, x1 = SCREEN_W - 1;
#endif
      uint16_t *row = fb + static_cast<size_t>(py) * SCREEN_W;
      for (int px = x0; px <= x1; px++) {
        const uint16_t dst = decode_hw_rgb565_raw(row[px]);
        const int dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
        const int d = bayer_row[px & 3];
        const uint16_t out = static_cast<uint16_t>((((sr * a + dr * inv + d) >> 8) << 11) |
                                                   (((sg * a + dg * inv + d) >> 8) << 5) |
                                                   ((sb * a + db * inv + d) >> 8));
        row[px] = encode_hw_rgb565_raw(out);
      }
    }
  } else {
    (void)cx;
  }

  if (el_ms < 430U) {
    const float w_scale = static_cast<float>(SCREEN_W) / 466.0f;
    const int b_seed = static_cast<int>(lt.flash_seq * 43U + 19U);
    const int x0 = static_cast<int>((132.0f + hash2d_01(b_seed, 11) * 202.0f) * w_scale), y0 = scale_y(54);
    const int x1 = x0 + static_cast<int>((hash2d_01(b_seed, 23) - 0.5f) * 52.0f * w_scale), y1 = scale_y(104);
    const int x2 = x1 + static_cast<int>((hash2d_01(b_seed, 37) - 0.5f) * 56.0f * w_scale), y2 = scale_y(158);
    const int x3 = x2 + static_cast<int>((hash2d_01(b_seed, 53) - 0.5f) * 60.0f * w_scale), y3 = scale_y(214);
    const int x4 = x3 + static_cast<int>((hash2d_01(b_seed, 71) - 0.5f) * 54.0f * w_scale), y4 = scale_y(270);
    const int x5 = x4 + static_cast<int>((hash2d_01(b_seed, 89) - 0.5f) * 46.0f * w_scale), y5 = scale_y(324);

    const int core_q8 = std::clamp(static_cast<int>(env * 256.0f), 0, 256);
    const int mid_q8  = (core_q8 * 3) / 4;
    const int fork_q8 = std::clamp(static_cast<int>(env * 210.0f), 0, 256);
    const int trunk_hw = (SCREEN_W >= 600) ? 2 : 1;
    const int glow_hi  = std::max(3, scale_min(4));
    const int glow_mid = std::max(2, scale_min(3));
    const int glow_tip = std::max(2, scale_min(2));
    const UiColor c_core(255, 255, 255), c_fork(150, 212, 255);

    draw_lightning_seg_q8(it, fb, x0, y0, x1, y1, c_core, c_fork, core_q8, core_q8, trunk_hw, glow_hi, cy);
    draw_lightning_seg_q8(it, fb, x1, y1, x2, y2, c_core, c_fork, core_q8, core_q8, trunk_hw, glow_hi, cy);
    draw_lightning_seg_q8(it, fb, x2, y2, x3, y3, c_core, c_fork, core_q8, core_q8, 1, glow_mid, cy);
    draw_lightning_seg_q8(it, fb, x3, y3, x4, y4, c_core, c_fork, core_q8, mid_q8,  1, glow_mid, cy);
    draw_lightning_seg_q8(it, fb, x4, y4, x5, y5, c_core, c_fork, mid_q8,  0,       0, glow_tip, cy);

    const int f1_dir = (x2 >= x1) ? 1 : -1;
    const int fx1 = x1 + f1_dir * scale_x(30), fy1 = y1 + scale_y(30);
    const int fx2 = fx1 + f1_dir * scale_x(22), fy2 = fy1 + scale_y(32);
    draw_lightning_seg_q8(it, fb, x1, y1, fx1, fy1, c_core, c_fork, fork_q8, (fork_q8 * 3) / 5, 0, glow_tip, cy);
    draw_lightning_seg_q8(it, fb, fx1, fy1, fx2, fy2, c_fork, c_fork, (fork_q8 * 3) / 5, 0,     0, glow_tip, cy);

    const int f2_dir = -f1_dir;
    const int gx1 = x3 + f2_dir * scale_x(26), gy1 = y3 + scale_y(28);
    const int gx2 = gx1 + f2_dir * scale_x(18), gy2 = gy1 + scale_y(26);
    const int f2_q8 = (fork_q8 * 4) / 5;
    draw_lightning_seg_q8(it, fb, x3, y3, gx1, gy1, c_core, c_fork, f2_q8, f2_q8 / 2, 0, glow_tip, cy);
    draw_lightning_seg_q8(it, fb, gx1, gy1, gx2, gy2, c_fork, c_fork, f2_q8 / 2, 0,   0, glow_tip, cy);
  }
}

void render_wind_gusts_and_leaves(Rgb565Surface &it, uint16_t *fb, int cy, bool is_night,
                                  float dt_s, float precip_alpha, float par_x, float par_y,
                                  WeatherParticleEngine &eng) {
  static constexpr int16_t kGustY[3] = {82, 168, 236};
  const UiColor gust_c = is_night ? UiColor(92, 128, 168) : UiColor(150, 194, 236);
  const float gust_wrap = static_cast<float>(SCREEN_W + 94);
  const float gust_spd = 190.0f * (static_cast<float>(SCREEN_W) / 466.0f);
  const int gust_a = static_cast<int>(88.0f * precip_alpha);
  for (int g = 0; g < 3; g++) {
    eng.gust_x[g] += gust_spd * dt_s;
    if (eng.gust_x[g] > gust_wrap - 50.0f) eng.gust_x[g] -= gust_wrap;
    const int gx_head = static_cast<int>(eng.gust_x[g]);
    const int gy_base = scale_y(kGustY[g]);
    int prev_x = 0, prev_y = 0;
    for (int s = 0; s <= 6; s++) {
      const int sx = gx_head - s * scale_min(18);
      const int sy = gy_base + static_cast<int>(fast_sin(static_cast<float>(sx) * 0.025f + static_cast<float>(g) * 1.7f) * 7.0f);
      if (s > 0) {
        const int seg_a = (s >= 2 && s <= 4) ? gust_a : (gust_a / 2);
        draw_tapered_streak_q8(it, fb, sx, sy, prev_x, prev_y, gust_c, gust_c, seg_a, seg_a, cy);
      }
      prev_x = sx;
      prev_y = sy;
    }
  }

  if (precip_alpha <= 0.20f) return;
  for (int i = 0; i < WeatherParticleEngine::kLeafCount; i++) {
    const float h_spd = 135.0f + hash2d_01(i * 19 + 5, 13) * 85.0f;
    const float h_y0  = -155.0f + hash2d_01(i * 37 + 9, 27) * 235.0f;
    const float h_z0  = -95.0f + hash2d_01(i * 53 + 3, 41) * 205.0f;
    eng.leaf_x[i] += h_spd * dt_s;
    if (eng.leaf_x[i] > 240.0f) eng.leaf_x[i] -= 480.0f;
    const float fi = static_cast<float>(i);
    const float lx_3d = eng.leaf_x[i];
    const float ly_3d = h_y0 + fast_sin(eng.phase_s * 2.5f + fi * 0.9f) * 18.0f + fast_cos(lx_3d * 0.018f + fi) * 9.0f;
    const float lz_3d = h_z0 + fast_sin(eng.phase_s * 1.8f + fi * 1.4f) * 32.0f;
    const float rx = eng.phase_s * (2.6f + static_cast<float>(i % 3) * 0.7f) + fi * 1.1f;
    const float ry = eng.phase_s * (3.1f + static_cast<float>(i % 4) * 0.5f) + fi * 0.7f;
    const float rz = 0.25f * fast_sin(eng.phase_s * 2.2f + fi) + 0.15f;

    const float lum_scale = (is_night ? 0.72f : 1.0f) * precip_alpha;
    UiColor c_leaf, c_vein;
    const int pal_id = i & 3;
    if (pal_id == 0) {
      c_leaf = scale_color(UiColor(110, 182, 82), lum_scale);
      c_vein = scale_color(UiColor(165, 225, 130), lum_scale);
    } else if (pal_id == 1) {
      c_leaf = scale_color(UiColor(235, 188, 48), lum_scale);
      c_vein = scale_color(UiColor(255, 230, 120), lum_scale);
    } else if (pal_id == 2) {
      c_leaf = scale_color(UiColor(228, 118, 42), lum_scale);
      c_vein = scale_color(UiColor(255, 175, 95), lum_scale);
    } else {
      c_leaf = scale_color(UiColor(82, 152, 92), lum_scale);
      c_vein = scale_color(UiColor(135, 205, 145), lum_scale);
    }
    draw_tumbling_leaf_3d(it, {lx_3d + par_x * 0.7f, ly_3d + par_y * 0.7f, lz_3d},
                          rx, ry, rz, 1.05f, c_leaf, c_vein, cy);
  }
}

void render_snowfall_fx(Rgb565Surface &it, uint16_t *fb, int cy, bool is_night,
                        float dt_s, float precip_alpha, float par_x, float par_y,
                        WeatherParticleEngine &eng) {
  const UiColor c_core(255, 255, 255);
  const UiColor c_arm  = is_night ? UiColor(215, 235, 255) : UiColor(230, 245, 255);
  const float h_scale = static_cast<float>(SCREEN_H) * (1.0f / 466.0f);
  const float flake_scale = std::max(1.0f, static_cast<float>(std::min(SCREEN_W, SCREEN_H)) * (1.0f / 420.0f));
  const float margin_y = 10.0f * flake_scale;
  const float cycle_h = static_cast<float>(SCREEN_H) + 2.0f * margin_y;
  const int margin_x = static_cast<int>(std::ceil(8.0f * flake_scale));
  const int snow_span_w = std::max(32, SCREEN_W + 2 * margin_x);
  const bool day_contrast = !is_night;

  for (int i = 0; i < WeatherParticleEngine::kSnowCount; i++) {
    const int perm_i = (i * 23 + 7) % WeatherParticleEngine::kSnowCount;
    const int tier_mod = i % 10;
    const float depth_u = (tier_mod >= 7) ? 0.96f : ((tier_mod >= 3) ? 0.56f : 0.18f);
    const float depth_par = 0.28f + 0.65f * depth_u;
    const float spd_y = (30.0f + depth_u * 22.0f + hash2d_01(perm_i * 19 + 3, 23) * 14.0f) * h_scale;

    eng.snow_y[i] += spd_y * dt_s;
    if (eng.snow_y[i] >= cycle_h) {
      eng.snow_y[i] -= cycle_h;
      eng.snow_pass[i]++;
    }

    const int pass_seed = static_cast<int>(eng.snow_pass[i] * 43U + static_cast<uint32_t>(i) * 97U);
    const float rx_u  = hash2d_01(pass_seed, 13);
    const float r_jit = hash2d_01(pass_seed, 29);
    const float phase = hash2d_01(pass_seed, 41) * (2.0f * PI_F);
    const float base_lane =
        (static_cast<float>(perm_i) + 0.15f + rx_u * 0.70f) *
        (1.0f / static_cast<float>(WeatherParticleEngine::kSnowCount));
    const float swirl_ang = eng.phase_s * (1.10f + 0.35f * depth_u) + phase;
    const float swirl_px = fast_sin(swirl_ang) * (7.5f + 5.5f * depth_u) * flake_scale;
    int sx = -margin_x + static_cast<int>(base_lane * static_cast<float>(snow_span_w) +
                                          swirl_px + par_x * depth_par);
    const int sy = static_cast<int>(eng.snow_y[i] - margin_y + par_y * depth_par);
    while (sx < -margin_x) sx += snow_span_w;
    while (sx >= SCREEN_W + margin_x) sx -= snow_span_w;

    if (tier_mod >= 7) {
      draw_soft_snowflake_q8(it, fb, sx, sy, (3.15f + r_jit * 0.55f) * flake_scale,
                             static_cast<int>(255.0f * precip_alpha), true, day_contrast, c_core, c_arm, cy);
    } else if (tier_mod >= 3) {
      draw_soft_snowflake_q8(it, fb, sx, sy, (2.25f + r_jit * 0.45f) * flake_scale,
                             static_cast<int>(242.0f * precip_alpha), false, day_contrast, c_core, c_arm, cy);
    } else {
      draw_soft_snowflake_q8(it, fb, sx, sy, (1.55f + r_jit * 0.30f) * flake_scale,
                             static_cast<int>(212.0f * precip_alpha), false, false, c_core, c_arm, cy);
    }
  }
}

void render_weather_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const auto &ws = weather_state();
  const auto &prefs = persisted_prefs();

  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const int sky_weather_subpage = rs.nav.sky_weather_subpage;
  const auto &now = ctx.now;
  const UiStrView weather_sim_opt = ctx.weather_sim_opt;

  const UiFontView font_time = ctx.font_time;
  const UiFontView font_chrono_time = ctx.font_chrono_time;
  const UiFontView font_date = ctx.font_date;
  const UiFontView font_small = ctx.font_small;
  const UiFontView font_micro = ctx.font_micro;

  const UiColor theme_primary = ctx.theme_primary;
  const UiColor theme_secondary = ctx.theme_secondary;
  const UiColor col_white = ctx.col_white;
  const UiColor col_slate = ctx.col_slate;
#if LATTER_PANEL_IPS
  const UiColor col_warm_gold(255, 178, 28);
#else
  const UiColor col_warm_gold = ctx.col_warm_gold;
#endif

  // Authoritative backdrop precedence (resolve_backdrop_mode, D-067)
  {
    std::string sky_cond = ws.condition;
    bool sky_night = (ws.sun_state == "below_horizon" || sky_cond == "clear-night" ||
                      sky_cond == "clearnight" || (sky_cond.find("night") != std::string::npos));
    apply_weather_sim_override(weather_sim_opt, sky_cond, sky_night);
    const bool sim_override_on = (!weather_sim_opt.empty() && weather_sim_opt != "Live (Home Assistant)");
    const bool sun_ready = (!ws.sun_state.empty() && ws.sun_state != "unknown" && ws.sun_state != "unavailable");
    const auto &ac = area_ctx_state();
    const bool has_day_night_sky = is_day_night_sky_wallpaper_url(ac.wallpaper_url);
    const bool has_custom_wallpaper =
        (!ac.wallpaper_url.empty() && !has_day_night_sky &&
         ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
         ctx.home_wallpaper_art.valid());
    const bool dns_night = resolve_day_night_sky_is_night(ws.sun_state, now.is_valid(), now.hour);
    const BackdropMode bd_mode = resolve_backdrop_mode(
        PageId::WEATHER, 0, prefs.weather_background, prefs.weather_app_background,
        sim_override_on, has_custom_wallpaper, has_day_night_sky);

    if (bd_mode == BackdropMode::LiveWeatherSky) {
      if (((!std::isnan(ws.temperature) && sun_ready) || sim_override_on) &&
          format_weather_title(sky_cond) != "Unavailable") {
        double sky_moon_cycle = 0.5;
        compute_synodic_moon_phase(now.is_valid(), now.is_valid() ? now.timestamp : 0LL,
                                   ws.moon_phase, sky_moon_cycle);
        backdrop_active() = true;
        render_weather_motion_wallpaper(it, cx, cy, ctx.now_ms, sky_cond, sky_night,
                                        sky_moon_cycle, 0, 0, true,
                                        rs.hw_accel_x, rs.hw_accel_y, false);
      } else if (!backdrop_active()) {
        if (has_day_night_sky) {
          render_static_day_night_sky_base(it, cx, cy, dns_night);
        } else if (has_custom_wallpaper) {
          render_page_wallpaper_base(it, ctx.home_wallpaper_art, true, cx, ctx.col_bg);
        } else {
          frame_base(it, ctx.col_bg);
        }
      }
    } else if (bd_mode == BackdropMode::DayNightSky) {
      if (!backdrop_active()) render_static_day_night_sky_base(it, cx, cy, dns_night);
    } else if (bd_mode == BackdropMode::CustomWallpaper) {
      if (!backdrop_active()) render_page_wallpaper_base(it, ctx.home_wallpaper_art, true, cx, ctx.col_bg);
    } else if (!backdrop_active()) {
      frame_base(it, ctx.col_bg);
    }
  }

  if (sky_weather_subpage == 0) {
    // SUBPAGE 0: CURRENT CONDITIONS
    std::string cond = ws.condition;
    bool is_night = (ws.sun_state == "below_horizon" || cond == "clear-night" ||
                     cond == "clearnight" || (cond.find("night") != std::string::npos));
    apply_weather_sim_override(weather_sim_opt, cond, is_night);
    const bool sim_override_on = (!weather_sim_opt.empty() && weather_sim_opt != "Live (Home Assistant)");
    const bool sun_ready = (!ws.sun_state.empty() && ws.sun_state != "unknown" && ws.sun_state != "unavailable");
    if (!sim_override_on && !sun_ready) cond = "";

    render_page_header(it, cx, "CURRENT", font_small, col_white);

    char temp_num[16];
    if (!std::isnan(ws.temperature)) std::snprintf(temp_num, sizeof(temp_num), "%.0f", ws.temperature);
    else std::snprintf(temp_num, sizeof(temp_num), "%s", kUnknownGlyph);
    int w_num = 0, x_off = 0, base = 0, th = 0;
    font_time->measure(temp_num, &w_num, &x_off, &base, &th);
    const int num_cx = cx - scale_x(5);
    const int deg_cx = num_cx + (w_num / 2) + scale_x(18);
    const int deg_cy = scale_y(100);

    if (backdrop_active()) {
      fast_print(it, num_cx + 2, scale_y(120) + 2, font_time, kChromeShadow, UiTextAlign::CENTER, temp_num);
    }
    fast_print(it, num_cx, scale_y(120), font_time, col_white, UiTextAlign::CENTER, temp_num);

    const float deg_r = std::max(5.0f, static_cast<float>(scale_s(7)));
    const float deg_th = std::max(2.0f, static_cast<float>(scale_s(2)) + 0.35f);
    if (backdrop_active()) fast_circle_aa(it, deg_cx + 1, deg_cy + 1, deg_r, kChromeShadow, deg_th);
    fast_circle_aa(it, deg_cx, deg_cy, deg_r, kChromeDegSilver, deg_th);

    const int cond_max_w = std::min({kMaxContentColW, safe_span(scale_y(182), 16), scale_x(340)});
    const std::string cond_title = fit_text_to_width_font(format_weather_title(cond), cond_max_w, font_date);
    ui::shadow_print(it, cx, scale_y(182), font_date, theme_secondary, UiTextAlign::CENTER, cond_title.c_str());

    const bool large_cur = (!LATTER_ROUND_DISPLAY && SCREEN_W >= 400 && SCREEN_H >= 480);
    const int hilo_y = (SCREEN_H > 520) ? scale_y(248) : ((SCREEN_H >= 500) ? 256 : (large_cur ? 248 : 236));
    const int tele_y = (SCREEN_H > 520) ? scale_y(316) : ((SCREEN_H >= 500) ? 326 : (large_cur ? 314 : 284));
    const UiColor icon_c = kChromeTeleIce;
    const bool has_bd_shadow = backdrop_active();

    if (!std::isnan(ws.high) || !std::isnan(ws.low)) {
      char hi_str[16], lo_str[16];
      if (!std::isnan(ws.high)) std::snprintf(hi_str, sizeof(hi_str), "%.0f°", ws.high);
      else std::snprintf(hi_str, sizeof(hi_str), "%s°", kUnknownGlyph);
      if (!std::isnan(ws.low)) std::snprintf(lo_str, sizeof(lo_str), "%.0f°", ws.low);
      else std::snprintf(lo_str, sizeof(lo_str), "%s°", kUnknownGlyph);

      int w_hi = 0, w_lo = 0;
      const UiFontView hilo_font = (large_cur && font_chrono_time != nullptr) ? font_chrono_time : font_date;
      hilo_font->measure(hi_str, &w_hi, &x_off, &base, &th);
      hilo_font->measure(lo_str, &w_lo, &x_off, &base, &th);
      const int arr_slot = scale_x(14), arr_pad = scale_x(10), pair_gap = scale_x(32);
      const int total_hilo_w = arr_slot + arr_pad + w_hi + pair_gap + arr_slot + arr_pad + w_lo;
      const int hilo_left = cx - (total_hilo_w / 2);
      const int up_arr_x = hilo_left + arr_slot / 2;
      const int hi_txt_x = hilo_left + arr_slot + arr_pad;
      const int dn_arr_x = hi_txt_x + w_hi + pair_gap + arr_slot / 2;
      const int lo_txt_x = dn_arr_x + (arr_slot - arr_slot / 2) + arr_pad;
      const int arr_r = std::max(7, scale_s(10));

      ui::draw_icon_arrow_v(it, up_arr_x, hilo_y, arr_r, true, icon_c, has_bd_shadow);
      ui::shadow_print(it, hi_txt_x, hilo_y, hilo_font, col_white, UiTextAlign::CENTER_LEFT, hi_str);
      ui::draw_icon_arrow_v(it, dn_arr_x, hilo_y, arr_r, false, icon_c, has_bd_shadow);
      ui::shadow_print(it, lo_txt_x, hilo_y, hilo_font, col_slate, UiTextAlign::CENTER_LEFT, lo_str);
    }

    const int drop_r = scale_s(10);
    const int drop_x = cx - ((SCREEN_W < 380) ? 96 : (large_cur ? scale_x(120) : 105));
    ui::draw_icon_droplet(it, drop_x, tele_y, drop_r, icon_c, has_bd_shadow);
    char hum_str[20];
    if (!std::isnan(ws.humidity)) std::snprintf(hum_str, sizeof(hum_str), "%.0f%%", ws.humidity);
    else std::snprintf(hum_str, sizeof(hum_str), "%s%%", kUnknownGlyph);
    const UiFontView tele_font = large_cur ? font_date : font_small;
    ui::shadow_print(it, drop_x + drop_r + scale_x(8), tele_y, tele_font, col_white, UiTextAlign::CENTER_LEFT, hum_str);

    const int w_icon_x = cx - scale_x(5);
    ui::draw_icon_wind(it, w_icon_x, tele_y, std::max(9, scale_s(12)), icon_c, has_bd_shadow);
    char wind_str[28];
    const char *w_dir = wind_bearing_to_cardinal(ws.wind_bearing);
    if (!std::isnan(ws.wind_speed)) {
      if (w_dir[0] != '\0') std::snprintf(wind_str, sizeof(wind_str), "%.0f mph %s", std::roundf(ws.wind_speed), w_dir);
      else std::snprintf(wind_str, sizeof(wind_str), "%.0f mph", std::roundf(ws.wind_speed));
    } else {
      std::snprintf(wind_str, sizeof(wind_str), "%s mph", kUnknownGlyph);
    }
    ui::shadow_print(it, w_icon_x + scale_x(20), tele_y, tele_font, col_white, UiTextAlign::CENTER_LEFT, wind_str);

  } else if (sky_weather_subpage == 1) {
    // SUBPAGE 1: 5-DAY FORECAST
    render_page_header(it, cx, "FORECAST", font_small, col_white);

    struct DayForecast {
      std::string day, high, low, cond;
    };
    DayForecast fc_days[5];
    int fc_count = 0;
    {
      std::string_view recs[5];
      const size_t n = v3_split_fields(ws.forecast_5d, recs, 5, kProtocolRecordSep);
      for (size_t r = 0; r < n; r++) {
        std::string_view f[4];
        if (v3_split_fields(recs[r], f, 4, '|') != 4) continue;
        DayForecast &df = fc_days[fc_count++];
        df.day = std::string(f[0]);
        df.cond = std::string(f[1]);
        df.high = f[2].empty() ? std::string(kUnknownGlyph) : std::string(f[2]);
        df.low = f[3].empty() ? std::string(kUnknownGlyph) : std::string(f[3]);
      }
    }
    if (fc_count == 0) {
      render_empty_state(it, cx, SCREEN_CY, "Forecast not available yet", font_small, col_slate);
    } else {
      bool dummy_night = false;
      apply_weather_sim_override(weather_sim_opt, fc_days[0].cond, dummy_night);
    }

    const bool large_fc = (!LATTER_ROUND_DISPLAY && SCREEN_W >= 460);
    const int fc_top_y  = layout().hdr_y + (LATTER_ROUND_DISPLAY ? 48 : scale_y(54));
    const int fc_step_y = LATTER_ROUND_DISPLAY ? 47 : ((SCREEN_W < 380) ? 46 : ((SCREEN_H > 520) ? scale_y(54) : ((SCREEN_H >= 500) ? 55 : 52)));
    const int row_hw    = (SCREEN_W < 380) ? 132 : std::min(kMaxContentColW / 2, large_fc ? scale_x(190) : 154);
    const int x_day     = cx - row_hw + ((SCREEN_W < 380) ? 28 : (large_fc ? scale_x(36) : 34));
    const int x_glyph   = cx - row_hw + ((SCREEN_W < 380) ? 76 : (large_fc ? scale_x(94) : 92));
    const int x_cond    = x_glyph + ((SCREEN_W < 380) ? 18 : (large_fc ? scale_x(24) : 22));
    const int x_slash   = cx + row_hw - ((SCREEN_W < 380) ? 30 : (large_fc ? scale_x(42) : 34));
    const int slash_gap = large_fc ? scale_x(10) : scale_x(8);
    const int cond_pad  = large_fc ? scale_x(56) : scale_x(44);
    const UiFontView row_font = large_fc ? font_date : font_small;
    const UiFontView sep_font = large_fc ? font_small : font_micro;

    for (int i = 0; i < fc_count && i < 5; i++) {
      const int ry = fc_top_y + i * fc_step_y;
      const int ry_data = ry + 1;
      const auto &d = fc_days[i];
      if (!d.cond.empty()) render_forecast_glyph(it, x_glyph, ry_data, d.cond);

      const std::string hi_str = (d.high == kUnknownGlyph) ? d.high : d.high + "°";
      const std::string lo_str = (d.low == kUnknownGlyph) ? d.low : d.low + "°";
      const std::string c_txt = fit_text_to_width_font(
          format_forecast_short_name(d.cond), std::max(36, (x_slash - cond_pad) - x_cond), row_font);
      ui::shadow_print(it, x_day, ry, row_font, (i == 0) ? theme_primary : col_white, UiTextAlign::CENTER, d.day.c_str());
      ui::shadow_print(it, x_cond, ry_data, row_font, col_slate, UiTextAlign::CENTER_LEFT, c_txt.c_str());
      ui::shadow_print(it, x_slash - slash_gap, ry_data, row_font, col_white, UiTextAlign::CENTER_RIGHT, hi_str.c_str());
      ui::shadow_print(it, x_slash, ry_data, sep_font, col_slate, UiTextAlign::CENTER, "/");
      ui::shadow_print(it, x_slash + slash_gap, ry_data, row_font, col_slate, UiTextAlign::CENTER_LEFT, lo_str.c_str());
    }

  } else if (sky_weather_subpage == 2) {
    // SUBPAGE 2: SUN
    render_page_header(it, cx, "SUN", font_small, col_white);
    render_weather_sun_subpage(it, ctx, col_warm_gold);

  } else if (sky_weather_subpage == 3) {
    // SUBPAGE 3: MOON
    render_page_header(it, cx, "MOON", font_small, col_white);
    if (!render_weather_moon_subpage(it, ctx)) {
      render_weather_subpage_icons(it, sky_weather_subpage, theme_primary, col_white);
      return;
    }
  }

  render_weather_subpage_icons(it, sky_weather_subpage, theme_primary, col_white);
}

}  // namespace latter_ui
