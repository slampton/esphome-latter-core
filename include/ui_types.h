#pragma once

// ==============================================================================
// Latter UI Core Subpage Constants, Fast Trig, 3D Projection & Cadence Math
// (include/ui_types.h)
// Pure C++20 L0 leaf — zero ESPHome, HAL, or runtime-state includes.
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
inline constexpr int scale_x(int v466) { return scale_x_for(v466, SCREEN_W); }
inline constexpr int scale_y(int v466) { return scale_y_for(v466, SCREEN_H); }
inline constexpr int scale_s(int v466) { return scale_s_for(v466, SCREEN_MIN_DIM); }
inline constexpr int scale_min(int v466) { return scale_min_for(v466, SCREEN_MIN_DIM); }
inline constexpr float scale_min_f(float v466) {
  return v466 * (static_cast<float>(SCREEN_MIN_DIM) * (1.0f / 466.0f));
}
inline constexpr int scale_w(int v466) { return scale_w_for(v466, SCREEN_W); }
inline constexpr int kRadiusCardPx() { return radius_card_for(SCREEN_MIN_DIM); }
inline constexpr int kRadiusKeyPx() { return radius_key_for(SCREEN_MIN_DIM); }
static constexpr int kMaxContentWidth = kMaxContentColW;

static constexpr int STD_ART_SIZE = scale_min(264);
static constexpr int kBackLabelY = LATTER_ROUND_DISPLAY ? scale_min(426) : (SCREEN_H - scale_y(28));
static constexpr int kHomeBarY = LATTER_ROUND_DISPLAY ? scale_min(434) : (SCREEN_H - scale_y(11));

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
  static constexpr int HOME_SETTINGS   = 10;
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

static constexpr int16_t CIRCLE_HW[466] = {
    0,  21,  30,  37,  42,  48,  52,  56,  60,  64,  67,  70,
   73,  76,  79,  82,  84,  87,  89,  92,  94,  96,  98, 100,
  102, 105, 106, 108, 110, 112, 114, 116, 117, 119, 121, 122,
  124, 125, 127, 129, 130, 132, 133, 134, 136, 137, 138, 140,
  141, 142, 144, 145, 146, 147, 149, 150, 151, 152, 153, 154,
  156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167,
  168, 169, 170, 171, 172, 173, 173, 174, 175, 176, 177, 178,
  179, 179, 180, 181, 182, 183, 183, 184, 185, 186, 186, 187,
  188, 189, 189, 190, 191, 192, 192, 193, 194, 194, 195, 195,
  196, 197, 197, 198, 199, 199, 200, 200, 201, 202, 202, 203,
  203, 204, 204, 205, 205, 206, 206, 207, 208, 208, 208, 209,
  209, 210, 210, 211, 211, 212, 212, 213, 213, 214, 214, 214,
  215, 215, 216, 216, 216, 217, 217, 218, 218, 218, 219, 219,
  219, 220, 220, 220, 221, 221, 221, 222, 222, 222, 223, 223,
  223, 224, 224, 224, 224, 225, 225, 225, 225, 226, 226, 226,
  226, 227, 227, 227, 227, 228, 228, 228, 228, 228, 228, 229,
  229, 229, 229, 229, 230, 230, 230, 230, 230, 230, 230, 231,
  231, 231, 231, 231, 231, 231, 231, 231, 232, 232, 232, 232,
  232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232,
  232, 232, 232, 232, 232, 233, 232, 232, 232, 232, 232, 232,
  232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232,
  232, 232, 232, 231, 231, 231, 231, 231, 231, 231, 231, 231,
  230, 230, 230, 230, 230, 230, 230, 229, 229, 229, 229, 229,
  228, 228, 228, 228, 228, 228, 227, 227, 227, 227, 226, 226,
  226, 226, 225, 225, 225, 225, 224, 224, 224, 224, 223, 223,
  223, 222, 222, 222, 221, 221, 221, 220, 220, 220, 219, 219,
  219, 218, 218, 218, 217, 217, 216, 216, 216, 215, 215, 214,
  214, 214, 213, 213, 212, 212, 211, 211, 210, 210, 209, 209,
  208, 208, 208, 207, 206, 206, 205, 205, 204, 204, 203, 203,
  202, 202, 201, 200, 200, 199, 199, 198, 197, 197, 196, 195,
  195, 194, 194, 193, 192, 192, 191, 190, 189, 189, 188, 187,
  186, 186, 185, 184, 183, 183, 182, 181, 180, 179, 179, 178,
  177, 176, 175, 174, 173, 173, 172, 171, 170, 169, 168, 167,
  166, 165, 164, 163, 162, 161, 160, 159, 158, 157, 156, 154,
  153, 152, 151, 150, 149, 147, 146, 145, 144, 142, 141, 140,
  138, 137, 136, 134, 133, 132, 130, 129, 127, 125, 124, 122,
  121, 119, 117, 116, 114, 112, 110, 108, 106, 105, 102, 100,
   98,  96,  94,  92,  89,  87,  84,  82,  79,  76,  73,  70,
   67,  64,  60,  56,  52,  48,  42,  37,  30,  21
};

inline int chord_half_span_int(int r, int dy, bool round_nearest = false) {
  return chord_half_span_constexpr(r, dy, round_nearest);
}

inline int circle_half_span(int c, int inset = 5) {
#if LATTER_ROUND_DISPLAY
  if (c < 0 || c >= SCREEN_H) return 0;
  int span = 0;
  if constexpr (SCREEN_W == 466 && SCREEN_H == 466) {
    span = static_cast<int>(CIRCLE_HW[c]) - inset;
  } else {
    int lut_idx = (c * 465) / ((SCREEN_H > 1) ? (SCREEN_H - 1) : 1);
    if (lut_idx < 0) lut_idx = 0;
    if (lut_idx > 465) lut_idx = 465;
    span = ((static_cast<int>(CIRCLE_HW[lut_idx]) * SCREEN_W) / 466) - inset;
  }
  return (span > 0) ? span : 0;
#else
  if (c < 0 || c >= SCREEN_H) return 0;
  int half_w = (SCREEN_W / 2) - inset;
  const int cr = LATTER_CORNER_R;
  if (cr > 8) {
    int dy = 0;
    if (c < cr) dy = cr - c;
    else if (c >= SCREEN_H - cr) dy = c - (SCREEN_H - 1 - cr);
    if (dy > 0 && dy < cr) {
      const int dx_corner = chord_half_span_int(cr, dy, false);
      const int corner_half_w = (SCREEN_W / 2) - (cr - dx_corner) - inset;
      if (corner_half_w < half_w) half_w = corner_half_w;
    }
  }
  return (half_w > 0) ? half_w : 0;
#endif
}

inline int safe_span(int y, int inset = 12) { return 2 * circle_half_span(y, inset); }

inline bool is_inside_bezel(int x, int y, int cy = SCREEN_CY, int inset = 5) {
#if LATTER_ROUND_DISPLAY
  const int h = circle_half_span(x, inset);
  return (h > 0) && (y >= cy - h) && (y <= cy + h);
#else
  (void)cy;
  return is_inside_bezel_for(
      x, y, SCREEN_W, SCREEN_H, false, LATTER_IS_WEARABLE != 0, inset, LATTER_CORNER_R);
#endif
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
  const float k = scale_min_f(fov) / depth;
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

inline uint32_t &last_frame_duration_ms() {
  static uint32_t dur_ms = 0;
  return dur_ms;
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

inline uint32_t min_interactive_frame_gap_ms(bool mww_running) {
#if LATTER_CHIP_P4
  (void)mww_running;
  return 36U;
#else
  return mww_running ? 54U : 36U;
#endif
}

inline constexpr uint32_t min_ambient_frame_gap_ms(bool speaker_audio_busy,
                                                   bool va_busy = false, bool alert_busy = false,
                                                   bool mww_passive_busy = false) noexcept {
  if (va_busy || alert_busy) return 120U;
#if LATTER_CHIP_P4
  (void)mww_passive_busy;
  return speaker_audio_busy ? 80U : 60U;
#else
  return (speaker_audio_busy || mww_passive_busy) ? 80U : 60U;
#endif
}

inline bool should_step_ambient_anim(uint32_t now_ms, bool speaker_audio_busy,
                                     bool va_busy = false, bool alert_busy = false,
                                     bool mww_passive_busy = false) {
  if (va_busy || disp_render_in_flight(now_ms)) return false;
#if LATTER_CHIP_P4
  (void)mww_passive_busy;
  const uint32_t min_post_idle = speaker_audio_busy ? 26U : 16U;
  const uint32_t min_period = alert_busy ? 120U : (speaker_audio_busy ? 74U : 54U);
#else
  const bool audio_or_mww = speaker_audio_busy || mww_passive_busy;
  const uint32_t min_post_idle = audio_or_mww ? 32U : 18U;
  const uint32_t min_period = alert_busy ? 120U : (audio_or_mww ? 74U : 54U);
#endif
  if (now_ms - last_disp_update_end_ms() < min_post_idle) return false;
  return (now_ms - last_ambient_anim_ms() >= min_period);
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

}  // namespace latter_ui
