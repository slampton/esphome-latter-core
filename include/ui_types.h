#pragma once

// ==============================================================================
// Latter UI Core Subpage Constants, Fast Trig, 3D Projection & Cadence Math
// (include/ui_types.h)
// Pure C++20 UI leaf — zero ESPHome, HAL, or runtime-state includes.
// ==============================================================================

#include "ui_color.h"
#include "ui_hit_table.h"
#include "ui_page_geoms.h"
#include "ui_panel_metrics.h"
#include "ui_panel_traits.h"
#include "ui_surface.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>

namespace latter_ui {

inline constexpr int mm_to_px(float mm) { return mm_to_px_for(mm, LATTER_PANEL_PPI); }
inline constexpr int kRadiusCardPx() { return radius_card_for(SCREEN_MIN_DIM); }
inline constexpr int kRadiusKeyPx() { return radius_key_for(SCREEN_MIN_DIM); }

static constexpr int STD_ART_SIZE = (SCREEN_MIN_DIM * 566) / 1000;
static constexpr int kBackLabelY = compute_dock_metrics(current_display_spec()).back_y;
static constexpr int kHomeBarY = compute_dock_metrics(current_display_spec()).home_bar_y;

namespace HomeSubpage {
  static constexpr int MAIN             = 0;
  static constexpr int SETTINGS         = 1;
  static constexpr int ACTIVITY_OVERLAY = 2;
}  // namespace HomeSubpage

namespace SystemMenuMode {
  static constexpr int DASHBOARD       = 0;
  static constexpr int SLEEP_WAKE      = 1;
  static constexpr int DISPLAY         = 2;
  static constexpr int ACTIONS         = 3;
  static constexpr int CONFIRM_RESTART = 4;
  static constexpr int UPDATE          = 5;
  static constexpr int CONFIRM_UPDATE  = 6;
  static constexpr int SOUND           = 7;
  static constexpr int SYSTEM_INFO     = 8;
  static constexpr int MUSIC           = 9;
  static constexpr int VOICE           = 11;
  static constexpr int WW_SENSITIVITY  = 12;
  static constexpr int COLOR_WHEEL     = 13;
}  // namespace SystemMenuMode

namespace TimeSubpage {
  static constexpr int CLOCK     = 0;
  static constexpr int TIMERS    = 1;
  static constexpr int ALARMS    = 2;
  static constexpr int STOPWATCH = 3;
}  // namespace TimeSubpage

namespace AreaSubpage {
  static constexpr int OVERVIEW     = 0;
  static constexpr int PICKER       = 1;
  static constexpr int LIGHT_DETAIL = 2;
}  // namespace AreaSubpage

// Trigonometric & Circular Clipping Lookup Tables (.rodata Flash)
static constexpr float PI_F = 3.14159265358979323846f;
static constexpr float HALF_PI_F = 1.57079632679489661923f;

static constexpr float DIAL_SIN[60] = {
  0.000000f, 0.104528f, 0.207912f, 0.309017f, 0.406737f, 0.500000f,
  0.587785f, 0.669131f, 0.743145f, 0.809017f, 0.866025f, 0.913545f,
  0.951057f, 0.978148f, 0.994522f, 1.000000f, 0.994522f, 0.978148f,
  0.951057f, 0.913545f, 0.866025f, 0.809017f, 0.743145f, 0.669131f,
  0.587785f, 0.500000f, 0.406737f, 0.309017f, 0.207912f, 0.104528f,
  0.000000f, -0.104528f, -0.207912f, -0.309017f, -0.406737f, -0.500000f,
  -0.587785f, -0.669131f, -0.743145f, -0.809017f, -0.866025f, -0.913545f,
  -0.951057f, -0.978148f, -0.994522f, -1.000000f, -0.994522f, -0.978148f,
  -0.951057f, -0.913545f, -0.866025f, -0.809017f, -0.743145f, -0.669131f,
  -0.587785f, -0.500000f, -0.406737f, -0.309017f, -0.207912f, -0.104528f
};

static constexpr float DIAL_COS[60] = {
  1.000000f, 0.994522f, 0.978148f, 0.951057f, 0.913545f, 0.866025f,
  0.809017f, 0.743145f, 0.669131f, 0.587785f, 0.500000f, 0.406737f,
  0.309017f, 0.207912f, 0.104528f, 0.000000f, -0.104528f, -0.207912f,
  -0.309017f, -0.406737f, -0.500000f, -0.587785f, -0.669131f, -0.743145f,
  -0.809017f, -0.866025f, -0.913545f, -0.951057f, -0.978148f, -0.994522f,
  -1.000000f, -0.994522f, -0.978148f, -0.951057f, -0.913545f, -0.866025f,
  -0.809017f, -0.743145f, -0.669131f, -0.587785f, -0.500000f, -0.406737f,
  -0.309017f, -0.207912f, -0.104528f, 0.000000f, 0.104528f, 0.207912f,
  0.309017f, 0.406737f, 0.500000f, 0.587785f, 0.669131f, 0.743145f,
  0.809017f, 0.866025f, 0.913545f, 0.951057f, 0.978148f, 0.994522f
};

inline int chord_half_span_int(int r, int dy, bool round_nearest = false) {
  return scale_chord_half_span(r, dy, round_nearest);
}

inline int circle_half_span(int c, int inset = 5) {
  if (c < 0 || c >= SCREEN_H) return 0;
  const int x_in = bezel_x_inset_for(c, SCREEN_W, SCREEN_H, LATTER_CORNER_R,
                                     LATTER_ROUND_DISPLAY != 0, inset);
  const int hw = (SCREEN_W / 2) - x_in;
  return (hw > 0) ? hw : 0;
}

inline int safe_span(int y, int inset = 12) {
  return safe_span_for(y, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0,
                       LATTER_IS_WEARABLE != 0, inset, LATTER_CORNER_R);
}

inline bool is_inside_bezel(int x, int y, int cy = SCREEN_CY, int inset = 5) {
  (void)cy;
  return is_inside_bezel_for(
      x, y, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, inset, LATTER_CORNER_R);
}

inline float fast_sin(float rad) {
  if (rad < -64.0f || rad > 64.0f) {
    rad = std::fmod(rad, 2.0f * PI_F);
  }
  const float idx_f = rad * (60.0f / (2.0f * PI_F));
  int idx_i = static_cast<int>(idx_f);
  if (idx_f < 0.0f && static_cast<float>(idx_i) != idx_f) idx_i--;
  const float frac = idx_f - static_cast<float>(idx_i);
  int i0 = idx_i % 60;
  if (i0 < 0) i0 += 60;
  const int i1 = (i0 + 1 == 60) ? 0 : (i0 + 1);
  return DIAL_SIN[i0] + (DIAL_SIN[i1] - DIAL_SIN[i0]) * frac;
}

inline float fast_cos(float rad) { return fast_sin(rad + (PI_F * 0.5f)); }

inline void fast_sincos(float rad, float &out_s, float &out_c) {
  out_s = fast_sin(rad);
  out_c = fast_cos(rad);
}

struct Vec3 {
  float x, y, z;
};

struct ProjectedPoint {
  int x, y;
  float scale;
  bool visible;
};

struct Mat3 {
  float m[3][3];

  static inline Mat3 from_euler(float rx, float ry, float rz) {
    float sx, cx_r, sy, cy_r, sz, cz_r;
    fast_sincos(rx, sx, cx_r);
    fast_sincos(ry, sy, cy_r);
    fast_sincos(rz, sz, cz_r);
    Mat3 out;
    out.m[0][0] = cy_r * cz_r;
    out.m[0][1] = sx * sy * cz_r - cx_r * sz;
    out.m[0][2] = cx_r * sy * cz_r + sx * sz;
    out.m[1][0] = cy_r * sz;
    out.m[1][1] = sx * sy * sz + cx_r * cz_r;
    out.m[1][2] = cx_r * sy * sz - sx * cz_r;
    out.m[2][0] = -sy;
    out.m[2][1] = sx * cy_r;
    out.m[2][2] = cx_r * cy_r;
    return out;
  }

  inline Vec3 transform(Vec3 v) const {
    return {
        m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
        m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
        m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z,
    };
  }

  inline Vec3 normal_z() const { return {m[0][2], m[1][2], m[2][2]}; }
};

inline Vec3 rotate_3d_xyz(Vec3 v, float rx, float ry, float rz) {
  return Mat3::from_euler(rx, ry, rz).transform(v);
}

inline ProjectedPoint project_3d(Vec3 v, int cx, int cy, float fov = 240.0f, float viewer_dist = 320.0f) {
  const float depth = v.z + viewer_dist;
  if (depth <= 15.0f) return {cx, cy, 0.0f, false};
  const float k = dp_f(fov) / depth;
  const int sx = cx + static_cast<int>(v.x * k);
  const int sy = cy + static_cast<int>(v.y * k);
  return {sx, sy, k, is_inside_bezel(sx, sy, cy, 4)};
}

inline float hash2d_01(int x, int y) {
  uint32_t n = static_cast<uint32_t>(x) * 374761393U + static_cast<uint32_t>(y) * 668265263U;
  n = (n ^ (n >> 13)) * 1274126177U;
  return static_cast<float>((n ^ (n >> 16)) & 0xFFFFU) * (1.0f / 65535.0f);
}

// One motion system (docs/device-ui-design.md): kMotionMs = 210 ms, ease-out cubic.
static constexpr uint32_t kMotionMs = 210U;

inline float ease_out_cubic(float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  const float inv = 1.0f - t;
  return 1.0f - (inv * inv * inv);
}

inline uint32_t &last_ambient_anim_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline void record_ambient_frame_ms(uint32_t now_ms) { last_ambient_anim_ms() = now_ms; }

inline uint32_t &last_disp_update_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline uint32_t &last_disp_update_end_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline bool disp_render_in_flight(uint32_t now_ms) {
  const uint32_t start_ms = last_disp_update_ms();
  const uint32_t end_ms = last_disp_update_end_ms();
  return (start_ms != end_ms) &&
         (static_cast<int32_t>(start_ms - end_ms) > 0) &&
         ((now_ms - start_ms) < 250U);
}

inline constexpr uint32_t min_ambient_frame_gap_ms(bool speaker_audio_busy,
                                                   bool va_busy = false, bool alert_busy = false,
                                                   bool mww_passive_busy = false) noexcept {
  if (va_busy || alert_busy) return 120U;
#if LATTER_CHIP_P4
  (void)mww_passive_busy;
  return speaker_audio_busy ? 74U : 54U;
#else
  return (speaker_audio_busy || mww_passive_busy) ? 74U : 54U;
#endif
}

inline int menu_scroll_shift_elapsed(int span_px, uint32_t elapsed_ms) {
  const float e = ease_out_cubic(static_cast<float>(elapsed_ms) / static_cast<float>(kMotionMs));
  return static_cast<int>(std::lround(static_cast<float>(span_px) * e));
}

inline int step_wrap(int val, int delta, int count) {
  if (count <= 0) return 0;
  int res = (val + delta) % count;
  if (res < 0) res += count;
  return res;
}

inline int step_wrap_1based(int val, int delta, int count) {
  if (count <= 1) return 1;
  int zero_based = (val - 1 + delta) % count;
  if (zero_based < 0) zero_based += count;
  return zero_based + 1;
}

template <typename ValT, typename DeltaT, typename MinT, typename MaxT>
inline auto step_wrap_range(ValT val, DeltaT delta, MinT min_val, MaxT max_val) {
  auto next = val + delta;
  if (next > max_val + 0.001f) return static_cast<decltype(next)>(min_val);
  if (next < min_val - 0.001f) return static_cast<decltype(next)>(max_val);
  return next;
}

template <typename CurT>
inline const char *cycle_option(const CurT &cur, std::initializer_list<const char *> opts) {
  const std::string_view cur_sv(cur);
  for (auto it = opts.begin(); it != opts.end(); ++it) {
    if (cur_sv == std::string_view(*it)) {
      ++it;
      return (it != opts.end()) ? *it : *opts.begin();
    }
  }
  return (opts.size() > 0) ? *opts.begin() : "";
}

inline void format_time_hhmm(int hour, int minute, bool is_24h, char *out_buf,
                             size_t out_sz, char *ap_buf = nullptr, bool pad_zero = false) {
  if (is_24h) {
    std::snprintf(out_buf, out_sz, "%02d:%02d", hour, minute);
    if (ap_buf != nullptr) ap_buf[0] = '\0';
  } else {
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    if (pad_zero) {
      std::snprintf(out_buf, out_sz, "%02d:%02d", h12, minute);
    } else {
      std::snprintf(out_buf, out_sz, "%d:%02d", h12, minute);
    }
    if (ap_buf != nullptr) {
      ap_buf[0] = (hour >= 12) ? 'P' : 'A';
      ap_buf[1] = 'M';
      ap_buf[2] = '\0';
    }
  }
}

inline bool clock_smooth_sweep_active(bool usb_connected, uint32_t now_ms,
                                      uint32_t latest_input_ms, uint8_t clock_face) noexcept {
  if (clock_face == 0U) return false;
  if (usb_connected || LATTER_IS_STATIONARY || !LATTER_HAS_BATTERY) return true;
  return (latest_input_ms != 0U) && ((now_ms - latest_input_ms) < 15000U);
}

inline float smooth_second_with_frac(int sec, uint32_t now_ms, bool smooth_active) noexcept {
  const int clamped_sec = std::clamp(sec, 0, 59);
  if (!smooth_active) {
    return static_cast<float>(clamped_sec);
  }
  static int s_last_sec = -1;
  static uint32_t s_anchor_ms = 0U;
  if (clamped_sec != s_last_sec || (now_ms - s_anchor_ms) > 1500U) {
    s_last_sec = clamped_sec;
    s_anchor_ms = now_ms;
  }
  const float frac = std::clamp(static_cast<float>(now_ms - s_anchor_ms) * 0.001f, 0.0f, 0.999f);
  return static_cast<float>(clamped_sec) + frac;
}

inline bool compute_synodic_moon_phase(bool time_valid, int64_t epoch_sec, const std::string &fallback_phase,
                                       double &out_cycle, int *out_illum = nullptr, std::string *out_label = nullptr) {
  double cycle = 0.5;
  int illum_pct = 50;
  std::string mp_label;
  bool known = true;

  if (time_valid) {
    const double diff = static_cast<double>(epoch_sec - 947182440LL);
    cycle = std::fmod(diff, 2551442.86) / 2551442.86;
    if (cycle < 0.0) cycle += 1.0;
    illum_pct = static_cast<int>(std::round((1.0 - std::cos(2.0 * 3.1415926535 * cycle)) / 2.0 * 100.0));

    if (cycle < 0.03 || cycle > 0.97) mp_label = "New Moon";
    else if (cycle < 0.22) mp_label = "Waxing Crescent";
    else if (cycle < 0.28) mp_label = "First Quarter";
    else if (cycle < 0.47) mp_label = "Waxing Gibbous";
    else if (cycle < 0.53) mp_label = "Full Moon";
    else if (cycle < 0.72) mp_label = "Waning Gibbous";
    else if (cycle < 0.78) mp_label = "Last Quarter";
    else mp_label = "Waning Crescent";
  } else {
    if (fallback_phase == "new_moon") { cycle = 0.0; illum_pct = 0; mp_label = "New Moon"; }
    else if (fallback_phase == "waxing_crescent") { cycle = 0.15; illum_pct = 25; mp_label = "Waxing Crescent"; }
    else if (fallback_phase == "first_quarter") { cycle = 0.25; illum_pct = 50; mp_label = "First Quarter"; }
    else if (fallback_phase == "waxing_gibbous") { cycle = 0.38; illum_pct = 75; mp_label = "Waxing Gibbous"; }
    else if (fallback_phase == "full_moon") { cycle = 0.50; illum_pct = 100; mp_label = "Full Moon"; }
    else if (fallback_phase == "waning_gibbous") { cycle = 0.68; illum_pct = 68; mp_label = "Waning Gibbous"; }
    else if (fallback_phase == "last_quarter") { cycle = 0.75; illum_pct = 50; mp_label = "Last Quarter"; }
    else if (fallback_phase == "waning_crescent") { cycle = 0.88; illum_pct = 22; mp_label = "Waning Crescent"; }
    else known = false;
  }
  if (!known) return false;

  out_cycle = cycle;
  if (out_illum != nullptr) *out_illum = illum_pct;
  if (out_label != nullptr) *out_label = mp_label;
  return true;
}

inline std::string strip_trailing_paren_state(const std::string &s) {
  if (s.size() > 4U && s.back() == ')') {
    const size_t lp = s.rfind(" (");
    if (lp != std::string::npos && lp > 0U) {
      return s.substr(0U, lp);
    }
  }
  return s;
}

}  // namespace latter_ui
