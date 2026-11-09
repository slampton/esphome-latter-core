#pragma once

// ==============================================================================
// Latter Core SDK UI, Fast-Math, 3D Projection & Touch Toolkit (ui_core.h)
// Portable across circular and rectangular ESPHome displays (default 466x466)
// Zero-exception (-fno-exceptions), zero-heap-churn embedded graphics primitives
// ==============================================================================

#include "esphome.h"
#ifdef USE_ESP32
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
#define LATTER_FB_DMA_CLEAR 1
#include <esp_async_memcpy.h>
#include <esp_attr.h>
#include <esp_cache.h>
#include <esp_rom_sys.h>
#include <atomic>
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define LATTER_FB_PPA 1
#include <driver/ppa.h>
#endif
#else
#define LATTER_FB_DMA_CLEAR 0
#endif
#endif
#ifndef LATTER_FB_PPA
#define LATTER_FB_PPA 0
#endif
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include "audio_router.h"
#include "hal_imu_qmi8658.h"
#include "hal_power_pm.h"
#include "hal_link_report.h"
#include "latter_state.h"

// SDK revision lockstep guard. The C++ headers arrive through external_components (refresh: 0s) while
// the YAML packages arrive through packages:, which ESPHome refreshes only once per day unless the
// package sets refresh: 0s. Mixing a fresh header set with stale YAML produces builds that compile
// but behave like neither revision, so an on_boot lambda in modules/features/core_system.yaml
// static_asserts its LATTER_YAML_REV against this value and any mismatch fails the build. Bump both
// values together on every core commit.
#define LATTER_SDK_REV 2026110902L

namespace latter_ui {

// Canonical Display Geometry & Hardware Capability Constants (LATTER_* standard)
#ifndef LATTER_SCREEN_W
#define LATTER_SCREEN_W 466
#endif
#ifndef LATTER_SCREEN_H
#define LATTER_SCREEN_H 466
#endif
#ifndef LATTER_ROUND_DISPLAY
#define LATTER_ROUND_DISPLAY 1
#endif
#ifndef LATTER_HAS_IMU
#define LATTER_HAS_IMU 1
#endif
#ifndef LATTER_HAS_BATTERY
#define LATTER_HAS_BATTERY 1
#endif
#ifndef LATTER_IS_STATIONARY
#define LATTER_IS_STATIONARY 0
#endif
#ifndef LATTER_IS_WEARABLE
#define LATTER_IS_WEARABLE 0
#endif
#ifndef LATTER_HAS_TOUCH
#define LATTER_HAS_TOUCH 1
#endif
#ifndef LATTER_HAS_DISPLAY
#define LATTER_HAS_DISPLAY 1
#endif
#ifndef LATTER_HAS_AUDIO
#define LATTER_HAS_AUDIO 1
#endif
#ifndef LATTER_HAS_MIC
#define LATTER_HAS_MIC LATTER_HAS_AUDIO
#endif
#ifndef LATTER_HAS_SPEAKER
#define LATTER_HAS_SPEAKER LATTER_HAS_AUDIO
#endif
#ifndef LATTER_CHIP_P4
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define LATTER_CHIP_P4 1
#else
#define LATTER_CHIP_P4 0
#endif
#endif
#ifndef LATTER_PSRAM_MB
#define LATTER_PSRAM_MB (LATTER_CHIP_P4 ? 32 : 8)
#endif
#ifndef LATTER_PANEL_IPS
#if defined(LATTER_BACKLIGHT_PIN)
#define LATTER_PANEL_IPS 1
#else
#define LATTER_PANEL_IPS 0
#endif
#endif
#ifndef LATTER_PANEL_PPI
#define LATTER_PANEL_PPI 266
#endif

static constexpr int SCREEN_W = LATTER_SCREEN_W;
static constexpr int SCREEN_H = LATTER_SCREEN_H;
static constexpr int SCREEN_CX = SCREEN_W / 2;
static constexpr int SCREEN_CY = SCREEN_H / 2;
static constexpr int SCREEN_R = SCREEN_W / 2;
static constexpr int SCREEN_MIN_DIM = (SCREEN_W < SCREEN_H) ? SCREEN_W : SCREEN_H;
static constexpr int SCREEN_MIN_R = SCREEN_MIN_DIM / 2;

inline constexpr int mm_to_px(float mm) {
  return static_cast<int>((mm * static_cast<float>(LATTER_PANEL_PPI)) / 25.4f + 0.5f);
}

inline constexpr int scale_x(int v466) {
  return (SCREEN_W > 520) ? ((v466 * SCREEN_W + 233) / 466) : v466;
}

inline constexpr int scale_y(int v466) {
  return (SCREEN_H > 520) ? ((v466 * SCREEN_H + 233) / 466) : v466;
}

inline constexpr int scale_s(int v466) {
  return (SCREEN_MIN_DIM > 520) ? ((v466 * SCREEN_MIN_DIM + 233) / 466) : v466;
}

// True all-resolution proportional scaling (scales down on 320x480 / 410x502 and up on 720x720)
inline constexpr int scale_min(int v466) {
  return (v466 * SCREEN_MIN_DIM + 233) / 466;
}

inline constexpr float scale_min_f(float v466) {
  return v466 * (static_cast<float>(SCREEN_MIN_DIM) * (1.0f / 466.0f));
}

static constexpr int STD_ART_SIZE = scale_min(264);
static constexpr int STD_ART_OFFSET = (SCREEN_W - STD_ART_SIZE) / 2;

// Shared chrome rows (UiLayout::back_y and UiLayout::home_bar_y read these).
static constexpr int kBackLabelY = LATTER_ROUND_DISPLAY ? 426 : (SCREEN_H - scale_y(28));
static constexpr int kHomeBarY = LATTER_ROUND_DISPLAY ? 434 : (SCREEN_H - scale_y(11));
static constexpr float kScreensaverBrightness = 0.40f;  // 40 % screensaver brightness cap

// Chrome colors (docs/device-ui-design.md, palette): the shared chrome draws with these named
// constants, never with literals. Palette colors (theme_primary, col_white, ...) are the GUI's
// ESPHome color ids and arrive as arguments. latter-verify [UI COLOR TOKENS] ratchets the
// remaining Color(r, g, b) literals per header.
inline const esphome::Color kChromeShadow(0, 0, 0);             // 1 px drop shadows under chrome and text
inline const esphome::Color kChromeHomePill(175, 190, 210);
inline const esphome::Color kChromeTrack(30, 38, 50);           // scrollbar track, mode-bar slot divider
inline const esphome::Color kChromeTrackBorder(50, 62, 78);
inline const esphome::Color kChromeTrackDim(16, 20, 26);
inline const esphome::Color kChromeTrackBorderDim(24, 30, 38);
inline const esphome::Color kChromeBarTrack(16, 20, 27);        // mode-bar track and capsule, empty-state card
inline const esphome::Color kChromeBarBorder(52, 64, 82);
inline const esphome::Color kChromeCardBorder(38, 46, 58);      // empty-state card border
inline const esphome::Color kChromePipInactive(95, 112, 134);
inline const esphome::Color kChromeBackLabel(158, 158, 158);
inline const esphome::Color kChromeGlassTint(22, 27, 36);       // ambient-tint fallback for glass cards
inline const esphome::Color kChromeGlassSheen(255, 255, 255);   // glass card edge sheen, blended (D-029)

namespace PageId {
  static constexpr int HOME       = 1;
  static constexpr int MUSIC      = 2;
  static constexpr int GAMES      = 3;
  static constexpr int SYSTEM     = 4;
  static constexpr int WEATHER    = 5;
  static constexpr int AREA       = 6;
  static constexpr int REMOTE     = 7;
  static constexpr int TOOLS      = 8;
  static constexpr int WHOLE_HOME = 9;
}

namespace ToolsSubpage {
  static constexpr int LAUNCHER   = 0;
  static constexpr int TIMERS     = 1;
  static constexpr int ALARMS     = 2;
  static constexpr int STOPWATCH  = 3;
  static constexpr int DICE       = 4;
  static constexpr int FLASHLIGHT = 5;
  static constexpr int CALCULATOR = 6;
  static constexpr int LEVEL      = 7;
  static constexpr int COIN_FLIP  = 8;
  static constexpr int CALENDAR   = 9;
  static constexpr int INTERCOM   = 10;
  static constexpr int CAMERAS    = 12;
}

namespace MusicOverlayMode {
  static constexpr int NOW_PLAYING     = 0;
  static constexpr int SPEAKERS        = 1;
  static constexpr int LIBRARY_CATS    = 2;
  static constexpr int ITEM_BROWSER    = 3;
  static constexpr int SPEAKER_ACTIONS = 4;
  static constexpr int MUSIC_HUB       = 5;
  static constexpr int TRACK_INSPECTOR = 6;
}


// 1. Mathematical, Trigonometric & Circular Clipping Lookup Tables (.rodata Flash)
// ------------------------------------------------------------------------------
static constexpr float PI_F = 3.14159265358979323846f;
static constexpr float HALF_PI_F = 1.57079632679489661923f;

// Universal Precomputed 60-Tick Sine/Cosine LUTs (Clock, Stopwatch, Dial Gauges)
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

// Precomputed 466-element circular half-width table in Flash (.rodata, saves 932 B SRAM)
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

// ------------------------------------------------------------------------------
// 1B. Universal Fast-Math, Circular Clipping, 3D Projection & Shader Primitives
// ------------------------------------------------------------------------------

// Display Shape Portability Trait:
//   1 = Circular AMOLED (uses O(1) .rodata CIRCLE_HW chord table)
//   0 = Rectangular / Square Display (uses full [inset .. SCREEN_W-inset] bounds)
#ifndef LATTER_ROUND_DISPLAY
#define LATTER_ROUND_DISPLAY 1
#endif

// O(1) Bezel Half-Span Lookup:
//   - Round Display (LATTER_ROUND_DISPLAY=1, 466x466): O(1) .rodata CIRCLE_HW[c] chord lookup
//   - Rectangular panels (LATTER_ROUND_DISPLAY=0): the full half-width (SCREEN_W/2) on every row.
// LATTER_CORNER_R is the panel's corner radius for bezel effects: W/2 round, 98 wearable, 20 otherwise.
#ifndef LATTER_CORNER_R
#if LATTER_ROUND_DISPLAY
#define LATTER_CORNER_R (LATTER_SCREEN_W / 2)
#elif LATTER_IS_WEARABLE
#define LATTER_CORNER_R 98
#else
#define LATTER_CORNER_R 20
#endif
#endif

// Zero-sqrtf Exact Integer Circle Chord Half-Span:
// Computes floor(sqrt(r^2 - dy^2)) when round_nearest=false, or floor(sqrt(r^2 - dy^2) + 0.5)
// when round_nearest=true, using 2-step integer Newton-Raphson (0 floating-point / libm stalls).
inline int chord_half_span_int(int r, int dy, bool round_nearest = false) {
  if (dy < 0) dy = -dy;
  if (dy == 0) return (r > 0) ? r : 0;
  if (dy >= r) return 0;
  const int n = r * r - dy * dy;
  int x = r - (dy * dy) / (2 * r);
  if (x <= 0) x = 1;
  x = (x + n / x) >> 1;
  x = (x + n / x) >> 1;
  while (x * x > n) x--;
  while ((x + 1) * (x + 1) <= n) x++;
  if (round_nearest && ((x + 1) * x < n)) x++;
  return x;
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
      int dx_corner = chord_half_span_int(cr, dy, false);
      int corner_half_w = (SCREEN_W / 2) - (cr - dx_corner) - inset;
      if (corner_half_w < half_w) half_w = corner_half_w;
    }
  }
  return (half_w > 0) ? half_w : 0;
#endif
}

inline bool is_inside_bezel(int x, int y, int cy = SCREEN_CY, int inset = 5) {
#if LATTER_ROUND_DISPLAY
  int h = circle_half_span(x, inset);
  return (h > 0) && (y >= cy - h) && (y <= cy + h);
#else
  (void)cy;
  return (x >= inset && x < SCREEN_W - inset && y >= inset && y < SCREEN_H - inset);
#endif
}

// Hardware-Trait Driven Wearable Motion Helpers (Zero hardcoded board numbers)
inline float get_pickup_motion_threshold(bool screen_is_active) {
#if LATTER_IS_WEARABLE
  return screen_is_active ? 0.22f : 0.38f;
#else
  return screen_is_active ? 0.068f : 0.15f;
#endif
}

inline bool should_in_hand_block_sleep(bool on_battery_and_picked_up) {
#if LATTER_IS_WEARABLE
  (void)on_battery_and_picked_up;
  return false;
#else
  return on_battery_and_picked_up;
#endif
}

// Fast interpolated sine/cosine backed by .rodata DIAL_SIN[60] (0 hardware FPU trig stalls)
inline float fast_sin(float rad) {
  float idx_f = rad * (60.0f / (2.0f * PI_F));
  int idx_i = static_cast<int>(idx_f);
  if (idx_f < 0.0f && static_cast<float>(idx_i) != idx_f) idx_i--;
  float frac = idx_f - static_cast<float>(idx_i);
  int i0 = idx_i % 60;
  if (i0 < 0) i0 += 60;
  int i1 = (i0 + 1 == 60) ? 0 : (i0 + 1);
  return DIAL_SIN[i0] + (DIAL_SIN[i1] - DIAL_SIN[i0]) * frac;
}

inline float fast_cos(float rad) {
  return fast_sin(rad + (PI_F * 0.5f));
}

inline void fast_sincos(float rad, float &out_s, float &out_c) {
  out_s = fast_sin(rad);
  out_c = fast_cos(rad);
}

// Universal 3D Vector, Precomputed 3x3 Rotation Matrix & Resolution-Adaptive Perspective Projection
struct Vec3 {
  float x, y, z;
};

struct ProjectedPoint {
  int x, y;
  float scale;
  bool visible;
};

// Precomputed 3x3 Rotation Matrix: computes Euler trig once per mesh/object instead of 6 trig calls per vertex
struct Mat3 {
  float m[3][3];

  // Exact match for rotate_3d_xyz(v, rx, ry, rz) intrinsic order: Rz(rz) * Ry(ry) * Rx(rx)
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

  // Returns transformed unit +Z normal (0 multiplies: direct column 2 readout)
  inline Vec3 normal_z() const {
    return {m[0][2], m[1][2], m[2][2]};
  }
};

inline Vec3 rotate_3d_xyz(Vec3 v, float rx, float ry, float rz) {
  return Mat3::from_euler(rx, ry, rz).transform(v);
}

inline ProjectedPoint project_3d(Vec3 v, int cx, int cy, float fov = 240.0f, float viewer_dist = 320.0f) {
  float depth = v.z + viewer_dist;
  if (depth <= 15.0f) return {cx, cy, 0.0f, false};
  float k = scale_min_f(fov) / depth;
  int sx = cx + static_cast<int>(v.x * k);
  int sy = cy + static_cast<int>(v.y * k);
  return {sx, sy, k, is_inside_bezel(sx, sy, cy, 4)};
}

// Deterministic Zero-RAM 2D Hash (0.0f .. 1.0f) for procedural shaders
inline float hash2d_01(int x, int y) {
  uint32_t n = static_cast<uint32_t>(x) * 374761393U + static_cast<uint32_t>(y) * 668265263U;
  n = (n ^ (n >> 13)) * 1274126177U;
  return static_cast<float>((n ^ (n >> 16)) & 0xFFFFU) * (1.0f / 65535.0f);
}

// Fast HSV-to-RGB (h in [0..1], s in [0..1], v in [0..1])
inline esphome::Color hsv_to_rgb(float h, float s, float v) {
  h = h - static_cast<float>(static_cast<int>(h));
  if (h < 0.0f) h += 1.0f;
  float r = v, g = v, b = v;
  if (s > 0.0f) {
    float h6 = h * 6.0f;
    int sector = static_cast<int>(h6);
    float f = h6 - static_cast<float>(sector);
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (sector % 6) {
      case 0: r = v; g = t; b = p; break;
      case 1: r = q; g = v; b = p; break;
      case 2: r = p; g = v; b = t; break;
      case 3: r = p; g = q; b = v; break;
      case 4: r = t; g = p; b = v; break;
      default: r = v; g = p; b = q; break;
    }
  }
  return esphome::Color(
      static_cast<uint8_t>(r * 255.0f),
      static_cast<uint8_t>(g * 255.0f),
      static_cast<uint8_t>(b * 255.0f));
}

// Defined with the other fast primitives below; declared here for draw_glow_line.
template<typename DisplayT>
inline void fast_line(DisplayT &it, int x1, int y1, int x2, int y2, esphome::Color c);

template<typename DisplayT>
inline void draw_glow_line(DisplayT &it, int x0, int y0, int x1, int y1,
                           esphome::Color core_col, esphome::Color halo_col) {
  int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
  int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
  if (dx >= dy) {
    fast_line(it, x0, y0 - 1, x1, y1 - 1, halo_col);
    fast_line(it, x0, y0 + 1, x1, y1 + 1, halo_col);
  } else {
    fast_line(it, x0 - 1, y0, x1 - 1, y1, halo_col);
    fast_line(it, x0 + 1, y0, x1 + 1, y1, halo_col);
  }
  fast_line(it, x0, y0, x1, y1, core_col);
}

// Ambient animation pacer, shared by the docked screensavers, Sky & Weather motion wallpapers and Home's
// live backgrounds: one step per 42 ms while local audio and alerts are idle, 75 ms while local audio
// plays (so audio DMA and PSRAM don't contend) and 120 ms during alerts; never during a voice turn.
inline uint32_t &last_ambient_anim_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline uint32_t &last_frame_duration_ms() {
  static uint32_t dur_ms = 0;
  return dur_ms;
}

inline void record_ambient_frame_ms(uint32_t now_ms) {
  last_ambient_anim_ms() = now_ms;
}

inline uint32_t &last_disp_update_end_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline uint32_t min_interactive_frame_gap_ms(bool mww_running) {
  (void)mww_running;
  return 42U;
}

inline bool should_step_ambient_anim(uint32_t now_ms, bool local_audio_busy,
                                     bool va_busy = false, bool alert_busy = false) {
  if (va_busy) return false;
#if LATTER_CHIP_P4
  const uint32_t min_post_idle = local_audio_busy ? 22U : 8U;
#else
  const uint32_t min_post_idle = local_audio_busy ? 28U : 12U;
#endif
  if (now_ms - last_disp_update_end_ms() < min_post_idle) return false;
  const uint32_t min_period = alert_busy ? 120U : (local_audio_busy ? 75U : 42U);
  return (now_ms - last_ambient_anim_ms() >= min_period);
}

// One motion system (docs/device-ui-design.md): every UI transition is time-based and eases out cubic
// over kMotionMs: the Home dock and mode-bar slides (DockSlideAnimator, ui_layout.h) and the list and
// carousel page transitions (script menu_scroll_transition).
static constexpr uint32_t kMotionMs = 210U;

inline float ease_out_cubic(float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  const float inv = 1.0f - t;
  return 1.0f - (inv * inv * inv);
}

// List / carousel page transition: started by menu_scroll_transition, drawn while menu_scroll_active.
inline uint32_t &menu_scroll_start_ms() {
  static uint32_t ts = 0;
  return ts;
}

// How far (of span_px) the outgoing page has moved at this point of the transition.
inline int menu_scroll_shift(int span_px) {
  const uint32_t elapsed = esphome::millis() - menu_scroll_start_ms();
  const float e = ease_out_cubic(static_cast<float>(elapsed) / static_cast<float>(kMotionMs));
  return static_cast<int>(std::lround(static_cast<float>(span_px) * e));
}



// Linear RGB color interpolation helper (k in [0.0f .. 1.0f])
inline esphome::Color lerp_color(esphome::Color a, esphome::Color b, float k) {
  if (k <= 0.0f) return a;
  if (k >= 1.0f) return b;
  return esphome::Color(
      static_cast<uint8_t>(a.r + (b.r - a.r) * k),
      static_cast<uint8_t>(a.g + (b.g - a.g) * k),
      static_cast<uint8_t>(a.b + (b.b - a.b) * k));
}

inline esphome::Color scale_color(esphome::Color c, float scale) {
  if (scale <= 0.0f) return esphome::Color(0, 0, 0);
  if (scale >= 1.0f) return c;
  return esphome::Color(
      static_cast<uint8_t>(c.r * scale),
      static_cast<uint8_t>(c.g * scale),
      static_cast<uint8_t>(c.b * scale));
}


// ------------------------------------------------------------------------------
// 2. Reusable Geometric & Vector UI Drawing Primitives (Direct RGB565 Scanlines)
// ------------------------------------------------------------------------------
template<typename DispT, bool IsFinal = std::is_final_v<DispT>>
struct DispBufferAccessor {
  static uint8_t *get(DispT *) { return nullptr; }
};

template<typename DispT>
struct DispBufferAccessor<DispT, false> : DispT {
  static uint8_t *get(DispT *d) {
    if constexpr (requires { &DispBufferAccessor::buffer_; }) {
      return reinterpret_cast<uint8_t *>(d->*(&DispBufferAccessor::buffer_));
    } else {
      return nullptr;
    }
  }
};

#if defined(USE_ESP32_VARIANT_ESP32P4) && __has_include("esphome/components/mipi_dsi/mipi_dsi.h")
struct MipiDsiBufferTag {
  using type = uint8_t *(esphome::mipi_dsi::MipiDsi::*);
};
template <typename Tag, typename Tag::type M>
struct MipiDsiBufferRob {
  friend constexpr typename Tag::type get_mipi_dsi_buffer_member(Tag) { return M; }
};
constexpr MipiDsiBufferTag::type get_mipi_dsi_buffer_member(MipiDsiBufferTag);
template struct MipiDsiBufferRob<MipiDsiBufferTag, &esphome::mipi_dsi::MipiDsi::buffer_>;

template<>
struct DispBufferAccessor<esphome::mipi_dsi::MipiDsi, true> {
  static uint8_t *get(esphome::mipi_dsi::MipiDsi *d) {
    return (d != nullptr) ? (d->*get_mipi_dsi_buffer_member(MipiDsiBufferTag{})) : nullptr;
  }
};
#endif

inline uint16_t *&active_frame_buf16() {
  static uint16_t *ptr = nullptr;
  return ptr;
}

// 0 = fallback (virtual draw_pixel_at), 1 = LE RGB565 (0xF800), 2 = BE RGB565 (0x00F8),
// 3 = LE BGR565 (0x001F), 4 = BE BGR565 (0x1F00)
inline int &active_frame_endian_mode() {
  static int mode = -1;
  return mode;
}

// Defined with the frame base (below): records the bound framebuffer for the between-frames DMA clear.
inline void fb_clear_bind(esphome::Component *owner, uint16_t *fb);
inline void fb_clear_after_update();

template<typename DispT>
inline void bind_frame_buffer(DispT *disp, esphome::display::Display &it) {
  fb_clear_bind(nullptr, nullptr);
  if (disp == nullptr || it.get_rotation() != esphome::display::DISPLAY_ROTATION_0_DEGREES) {
    active_frame_buf16() = nullptr;
    return;
  }
  uint8_t *raw = DispBufferAccessor<DispT>::get(disp);
  if (raw == nullptr) {
    active_frame_buf16() = nullptr;
    return;
  }
  uint16_t *buf16 = reinterpret_cast<uint16_t *>(raw);
  if (active_frame_endian_mode() < 0) {
    const uint16_t orig_px0 = buf16[0];
    it.draw_pixel_at(0, 0, esphome::Color(255, 0, 0));
    const uint16_t red_probe = buf16[0];
    buf16[0] = orig_px0;
    if (red_probe == 0xF800U) {
      active_frame_endian_mode() = 1;
    } else if (red_probe == 0x00F8U) {
      active_frame_endian_mode() = 2;
    } else if (red_probe == 0x001FU) {
      active_frame_endian_mode() = 3;
    } else if (red_probe == 0x1F00U) {
      active_frame_endian_mode() = 4;
    } else {
      active_frame_endian_mode() = 0;
    }
    ESP_LOGI("disp_fb", "Bound direct RGB565 framebuffer: ptr=%p endian_mode=%d",
             (void *)buf16, active_frame_endian_mode());
  }
  active_frame_buf16() = (active_frame_endian_mode() > 0) ? buf16 : nullptr;
  fb_clear_bind(static_cast<esphome::Component *>(disp), active_frame_buf16());
#if LATTER_ROUND_DISPLAY
  if (active_frame_buf16() != nullptr) {
    it.draw_pixel_at(0, 0, esphome::Color(8, 8, 8));
    it.draw_pixel_at(0, 0, esphome::Color(0, 0, 0));
    it.draw_pixel_at(SCREEN_W - 1, SCREEN_H - 1, esphome::Color(8, 8, 8));
    it.draw_pixel_at(SCREEN_W - 1, SCREEN_H - 1, esphome::Color(0, 0, 0));
  }
#endif
}

template<typename DispT>
inline void bind_frame_buffer(DispT *const *disp_ptr, esphome::display::Display &it) {
  if (disp_ptr != nullptr) bind_frame_buffer(*disp_ptr, it);
}

inline uint16_t encode_hw_rgb565_raw(uint16_t p) {
  const int mode = active_frame_endian_mode();
  if (mode == 1) return p;
  if (mode == 2) return __builtin_bswap16(p);
  const uint16_t bgr = static_cast<uint16_t>(((p & 0x001FU) << 11) | (p & 0x07E0U) | ((p >> 11) & 0x001FU));
  return (mode == 4) ? __builtin_bswap16(bgr) : bgr;
}

inline uint16_t encode_hw_rgb565(esphome::Color c) {
  const uint16_t p = static_cast<uint16_t>(((static_cast<uint16_t>(c.r) & 0xF8U) << 8) |
                                           ((static_cast<uint16_t>(c.g) & 0xFCU) << 3) |
                                           (static_cast<uint16_t>(c.b) >> 3));
  return encode_hw_rgb565_raw(p);
}

template<typename DisplayT>
inline void fast_pixel(DisplayT &it, int x, int y, esphome::Color c) {
  if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
  uint16_t *fb = active_frame_buf16();
  if (fb != nullptr) {
    fb[y * SCREEN_W + x] = encode_hw_rgb565(c);
    return;
  }
  it.draw_pixel_at(x, y, c);
}

template<typename DisplayT>
inline void fast_hline(DisplayT &it, int x, int y, int w, esphome::Color c) {
  if (w <= 0 || y < 0 || y >= SCREEN_H) return;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (x + w > SCREEN_W) {
    w = SCREEN_W - x;
  }
  if (w <= 0) return;
  uint16_t *fb = active_frame_buf16();
  if (fb != nullptr) {
    const uint16_t px = encode_hw_rgb565(c);
    uint16_t *dst = fb + y * SCREEN_W + x;
    int i = 0;
    if ((reinterpret_cast<uintptr_t>(dst) & 2U) != 0U) {
      dst[0] = px;
      i = 1;
    }
    const uint32_t pair = static_cast<uint32_t>(px) | (static_cast<uint32_t>(px) << 16);
    uint32_t *dst32 = reinterpret_cast<uint32_t *>(dst + i);
    const int pairs = (w - i) >> 1;
    for (int p = 0; p < pairs; p++) {
      dst32[p] = pair;
    }
    i += pairs << 1;
    if (i < w) {
      dst[i] = px;
    }
    return;
  }
  it.horizontal_line(x, y, w, c);
}

template<typename DisplayT>
inline void fill_rect_row_major(DisplayT &it, int x, int y, int w, int h, esphome::Color c) {
  if (w <= 0 || h <= 0) return;
  for (int row = 0; row < h; row++) {
    fast_hline(it, x, y + row, w, c);
  }
}

// Fast equivalents of ESPHome's Display drawing primitives. Upstream filled_circle,
// filled_rectangle, horizontal_line, vertical_line, rectangle, line and circle all end in one
// virtual draw_pixel_at per pixel (clip check, rotation switch, store and watermark update).
// With the software framebuffer bound, these write it directly using the same algorithms, so
// pixel coverage is identical; otherwise they fall back to the ESPHome primitive. Clipping
// regions are not supported (the SDK does not use them).
template<typename DisplayT>
inline void fast_vline(DisplayT &it, int x, int y, int h, esphome::Color c) {
  if (h <= 0 || x < 0 || x >= SCREEN_W) return;
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (y + h > SCREEN_H) {
    h = SCREEN_H - y;
  }
  if (h <= 0) return;
  uint16_t *fb = active_frame_buf16();
  if (fb != nullptr) {
    const uint16_t px = encode_hw_rgb565(c);
    uint16_t *dst = fb + y * SCREEN_W + x;
    for (int i = 0; i < h; i++, dst += SCREEN_W) {
      *dst = px;
    }
    return;
  }
  it.vertical_line(x, y, h, c);
}

template<typename DisplayT>
inline void fast_rect_outline(DisplayT &it, int x, int y, int w, int h, esphome::Color c) {
  fast_hline(it, x, y, w, c);
  fast_hline(it, x, y + h - 1, w, c);
  fast_vline(it, x, y, h, c);
  fast_vline(it, x + w - 1, y, h, c);
}

// Same Bresenham walk as Display::filled_circle; each row is emitted once at its widest span
// (later iterations on the same row are narrower subsets), removing the upstream overdraw.
template<typename DisplayT>
inline void fast_filled_circle(DisplayT &it, int cx, int cy, int r, esphome::Color c) {
  if (active_frame_buf16() == nullptr || r < 0) {
    it.filled_circle(cx, cy, r, c);
    return;
  }
  int dx = -r;
  int dy = 0;
  int err = 2 - 2 * r;
  int e2;
  int last_dy = -1;
  do {
    if (dy != last_dy) {
      const int w = 2 * (-dx) + 1;
      fast_hline(it, cx + dx, cy + dy, w, c);
      if (dy != 0) fast_hline(it, cx + dx, cy - dy, w, c);
      last_dy = dy;
    }
    e2 = err;
    if (e2 < dy) {
      err += ++dy * 2 + 1;
      if (-dx == dy && e2 <= dx) {
        e2 = 0;
      }
    }
    if (e2 > dx) {
      err += ++dx * 2 + 1;
    }
  } while (dx <= 0);
}

// Same Bresenham walk as Display::circle.
template<typename DisplayT>
inline void fast_circle(DisplayT &it, int cx, int cy, int r, esphome::Color c) {
  uint16_t *fb = active_frame_buf16();
  if (fb == nullptr || r < 0) {
    it.circle(cx, cy, r, c);
    return;
  }
  const uint16_t px = encode_hw_rgb565(c);
  auto put = [fb, px](int x, int y) {
    if (static_cast<unsigned>(x) < static_cast<unsigned>(SCREEN_W) &&
        static_cast<unsigned>(y) < static_cast<unsigned>(SCREEN_H)) {
      fb[y * SCREEN_W + x] = px;
    }
  };
  int dx = -r;
  int dy = 0;
  int err = 2 - 2 * r;
  int e2;
  do {
    put(cx - dx, cy + dy);
    put(cx + dx, cy + dy);
    put(cx + dx, cy - dy);
    put(cx - dx, cy - dy);
    e2 = err;
    if (e2 < dy) {
      err += ++dy * 2 + 1;
      if (-dx == dy && e2 <= dx) {
        e2 = 0;
      }
    }
    if (e2 > dx) {
      err += ++dx * 2 + 1;
    }
  } while (dx <= 0);
}

// -------------------------------------------------------------------------------------------------
// Anti-aliased shapes and dithered gradients (docs/device-ui-design.md D-027). The chrome draws every
// rounded rectangle, stadium and circle through fill_round_rect_aa / fast_filled_circle_aa /
// fast_circle_aa: straight edges sit on pixel boundaries, and only the curved edge pixels blend by
// coverage against the pixels already in the framebuffer (row-major, full 16-bit output). Gradients
// interpolate at 8.8 fixed point and quantize to RGB565 with a 4x4 ordered (Bayer) dither, so no
// stripes appear on bright IPS panels. No per-pixel sqrtf: coverage uses the distance-squared
// approximation |d - r| ~ |d^2 - r^2| / 2r, exact to well under a pixel at these radii.
// -------------------------------------------------------------------------------------------------
static constexpr uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// Inverse of encode_hw_rgb565_raw: framebuffer word -> native RGB565.
inline uint16_t decode_hw_rgb565_raw(uint16_t hw) {
  const int mode = active_frame_endian_mode();
  if (mode == 1) return hw;
  if (mode == 2) return __builtin_bswap16(hw);
  const uint16_t bgr = (mode == 4) ? __builtin_bswap16(hw) : hw;
  return static_cast<uint16_t>(((bgr & 0x001FU) << 11) | (bgr & 0x07E0U) | ((bgr >> 11) & 0x001FU));
}

// Quantizes a color at 8.8 fixed point per channel (0..65280) to RGB565 with ordered-dither
// threshold t (0..15): each channel rounds up when its fraction of a level exceeds the threshold.
inline uint16_t dither_rgb565(uint32_t r88, uint32_t g88, uint32_t b88, int t) {
  const uint32_t th = static_cast<uint32_t>(t) * 16U + 8U;
  uint32_t r = ((r88 * 31U) / 255U + th) >> 8;
  uint32_t g = ((g88 * 63U) / 255U + th) >> 8;
  uint32_t b = ((b88 * 31U) / 255U + th) >> 8;
  if (r > 31U) r = 31U;
  if (g > 63U) g = 63U;
  if (b > 31U) b = 31U;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// Horizontal span of an 8.8 color, ordered-dithered against the screen position.
template<typename DisplayT>
inline void fast_hline_dither(DisplayT &it, int x, int y, int w, uint32_t r88, uint32_t g88, uint32_t b88) {
  if (w <= 0 || y < 0 || y >= SCREEN_H) return;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (x + w > SCREEN_W) w = SCREEN_W - x;
  if (w <= 0) return;
  uint16_t *fb = active_frame_buf16();
  if (fb == nullptr) {
    fast_hline(it, x, y, w, esphome::Color(static_cast<uint8_t>(r88 >> 8), static_cast<uint8_t>(g88 >> 8),
                                           static_cast<uint8_t>(b88 >> 8)));
    return;
  }
  const uint8_t *th = kBayer4[y & 3];
  uint16_t pat[4];
  for (int k = 0; k < 4; k++) pat[k] = encode_hw_rgb565_raw(dither_rgb565(r88, g88, b88, th[k]));
  uint16_t *dst = fb + y * SCREEN_W + x;
  for (int i = 0; i < w; i++) dst[i] = pat[(x + i) & 3];
}

// Blends color c over the framebuffer pixel (x, y) with coverage a (0..255).
template<typename DisplayT>
inline void fast_pixel_blend(DisplayT &it, int x, int y, esphome::Color c, int a) {
  if (a <= 0 || x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
  uint16_t *fb = active_frame_buf16();
  if (fb == nullptr) {
    if (a >= 128) fast_pixel(it, x, y, c);
    return;
  }
  uint16_t &dst = fb[y * SCREEN_W + x];
  if (a >= 255) {
    dst = encode_hw_rgb565(c);
    return;
  }
  const uint16_t bg = decode_hw_rgb565_raw(dst);
  const int r5 = (bg >> 11) & 0x1F, g6 = (bg >> 5) & 0x3F, b5 = bg & 0x1F;
  const int br = (r5 << 3) | (r5 >> 2), bgc = (g6 << 2) | (g6 >> 4), bb = (b5 << 3) | (b5 >> 2);
  const int rr = br + (((static_cast<int>(c.r) - br) * a + 127) / 255);
  const int rg = bgc + (((static_cast<int>(c.g) - bgc) * a + 127) / 255);
  const int rb = bb + (((static_cast<int>(c.b) - bb) * a + 127) / 255);
  dst = encode_hw_rgb565(esphome::Color(static_cast<uint8_t>(rr), static_cast<uint8_t>(rg), static_cast<uint8_t>(rb)));
}

inline int aa_coverage_8(float a) {
  if (a <= 0.0f) return 0;
  if (a >= 1.0f) return 255;
  return static_cast<int>(a * 255.0f + 0.5f);
}

// Geometry of one anti-aliased rounded rectangle [x0, x0 + w) x [y0, y0 + h) (pixel edges).
struct AaRoundRect {
  float cxl = 0, cxr = 0, cyt = 0, cyb = 0, rad = 0, r_in = 0, r_out = 0, rad2 = 0, inv2r = 1;
  int x0 = 0, x_last = -1, y0 = 0, y_end = 0;

  void init(int x, int y, int w, int h, float r) {
    const float max_rad = 0.5f * static_cast<float>(std::min(w, h));
    rad = std::max(0.0f, std::min(r, max_rad));
    x0 = x;
    x_last = x + w - 1;
    y0 = y;
    y_end = y + h;
    cxl = static_cast<float>(x) + rad;
    cxr = static_cast<float>(x + w) - rad;
    cyt = static_cast<float>(y) + rad;
    cyb = static_cast<float>(y + h) - rad;
    r_in = rad - 0.5f;
    r_out = rad + 0.5f;
    rad2 = rad * rad;
    inv2r = (rad > 0.5f) ? (0.5f / rad) : 1.0f;
  }

  // Row py: the fully covered range [s0, s1] (empty when s1 < s0) and the touched range [e0, e1].
  // Returns false when the row misses the shape.
  bool row(int py, float &dy, int &s0, int &s1, int &e0, int &e1) const {
    if (py < y0 || py >= y_end || x_last < x0) return false;
    const float yc = static_cast<float>(py) + 0.5f;
    dy = 0.0f;
    if (yc < cyt) dy = cyt - yc;
    else if (yc > cyb) dy = yc - cyb;
    s0 = x0;
    s1 = x_last;
    e0 = x0;
    e1 = x_last;
    if (dy <= 0.0f || rad <= 0.0f) return true;
    if (dy >= r_out) return false;
    const float xo = sqrtf(r_out * r_out - dy * dy);
    e0 = std::max(x0, static_cast<int>(floorf(cxl - xo - 0.5f)));
    e1 = std::min(x_last, static_cast<int>(ceilf(cxr + xo - 0.5f)));
    if (dy < r_in) {
      const float xi = sqrtf(r_in * r_in - dy * dy);
      s0 = std::max(e0, static_cast<int>(ceilf(cxl - xi - 0.5f)));
      s1 = std::min(e1, static_cast<int>(floorf(cxr + xi - 0.5f)));
    } else {
      s0 = e1 + 1;
      s1 = e1;
    }
    return true;
  }

  // Coverage (0..255) of pixel px on a row at vertical corner distance dy.
  int coverage(int px, float dy) const {
    const float xc = static_cast<float>(px) + 0.5f;
    const float ox = std::max(0.0f, std::max(cxl - xc, xc - cxr));
    return aa_coverage_8(0.5f - (ox * ox + dy * dy - rad2) * inv2r);
  }
};

// Fills a rounded rectangle [x0, x0 + w) x [y0, y0 + h) with corner radius rad (rad = h / 2 is a
// stadium; w == h == 2 * rad a circle) and a vertical gradient from c_top (first row) to c_bot (last
// row), ordered-dithered; c_top == c_bot fills the exact color. hole > 0 leaves out the pixels fully
// inside the same shape inset by hole (a border pass whose inside the next call fills), so the
// border and the fill each write their pixels once. Rows outside the screen are clipped, so a shape
// may extend past an edge (a tab hanging from the top edge).
template<typename DisplayT>
inline void fill_round_rect_aa(DisplayT &it, int x0, int y0, int w, int h, float rad,
                               esphome::Color c_top, esphome::Color c_bot, int hole = 0) {
  if (w <= 0 || h <= 0) return;
  AaRoundRect o;
  o.init(x0, y0, w, h, rad);
  AaRoundRect in;
  const bool has_hole = (hole > 0 && w > 2 * hole && h > 2 * hole);
  if (has_hole) in.init(x0 + hole, y0 + hole, w - 2 * hole, h - 2 * hole, rad - static_cast<float>(hole));
  const bool flat = (c_top.r == c_bot.r && c_top.g == c_bot.g && c_top.b == c_bot.b);
  const int ys = std::max(0, y0);
  const int ye = std::min(SCREEN_H, y0 + h);
  for (int py = ys; py < ye; py++) {
    float dy = 0.0f;
    int s0 = 0, s1 = -1, e0 = 0, e1 = -1;
    if (!o.row(py, dy, s0, s1, e0, e1)) continue;
    uint32_t r88 = static_cast<uint32_t>(c_top.r) << 8;
    uint32_t g88 = static_cast<uint32_t>(c_top.g) << 8;
    uint32_t b88 = static_cast<uint32_t>(c_top.b) << 8;
    esphome::Color rc = c_top;
    if (!flat && h > 1) {
      const int num = py - y0;
      const int den = h - 1;
      r88 = static_cast<uint32_t>((static_cast<int>(c_top.r) << 8) + ((static_cast<int>(c_bot.r) - static_cast<int>(c_top.r)) * 256 * num) / den);
      g88 = static_cast<uint32_t>((static_cast<int>(c_top.g) << 8) + ((static_cast<int>(c_bot.g) - static_cast<int>(c_top.g)) * 256 * num) / den);
      b88 = static_cast<uint32_t>((static_cast<int>(c_top.b) << 8) + ((static_cast<int>(c_bot.b) - static_cast<int>(c_top.b)) * 256 * num) / den);
      rc = esphome::Color(static_cast<uint8_t>(r88 >> 8), static_cast<uint8_t>(g88 >> 8), static_cast<uint8_t>(b88 >> 8));
    }
    auto span = [&](int a, int b) {
      if (b < a) return;
      if (flat) fast_hline(it, a, py, b - a + 1, rc);
      else fast_hline_dither(it, a, py, b - a + 1, r88, g88, b88);
    };
    if (s1 >= s0) {
      int hs0 = 1, hs1 = 0;
      if (has_hole) {
        float idy = 0.0f;
        int ie0 = 0, ie1 = -1;
        if (!in.row(py, idy, hs0, hs1, ie0, ie1)) {
          hs0 = 1;
          hs1 = 0;
        }
      }
      if (hs1 >= hs0) {
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

template<typename DisplayT>
inline void fill_round_rect_aa(DisplayT &it, int x0, int y0, int w, int h, float rad, esphome::Color c) {
  fill_round_rect_aa(it, x0, y0, w, h, rad, c, c, 0);
}

// Border ring of a rounded rectangle, `thick` px wide, for a fill drawn next with the inset shape.
template<typename DisplayT>
inline void stroke_round_rect_aa(DisplayT &it, int x0, int y0, int w, int h, float rad,
                                 esphome::Color c_top, esphome::Color c_bot, int thick = 1) {
  fill_round_rect_aa(it, x0, y0, w, h, rad, c_top, c_bot, thick);
}

// Anti-aliased filled circle with the same footprint as fast_filled_circle (center at pixel (cx, cy)).
template<typename DisplayT>
inline void fast_filled_circle_aa(DisplayT &it, int cx, int cy, int r, esphome::Color c) {
  if (r < 0) return;
  fill_round_rect_aa(it, cx - r, cy - r, 2 * r + 1, 2 * r + 1, static_cast<float>(r) + 0.5f, c, c);
}

template<typename DisplayT>
inline void fast_filled_circle_aa_grad(DisplayT &it, int cx, int cy, int r, esphome::Color c_top, esphome::Color c_bot) {
  if (r < 0) return;
  fill_round_rect_aa(it, cx - r, cy - r, 2 * r + 1, 2 * r + 1, static_cast<float>(r) + 0.5f, c_top, c_bot);
}

// Anti-aliased ring centered on pixel (cx, cy): radius r to the middle of a stroke `thick` pixels wide.
template<typename DisplayT>
inline void fast_circle_aa(DisplayT &it, int cx, int cy, float r, esphome::Color c, float thick = 1.5f) {
  if (r <= 1.0f) return;
  const float half = 0.5f * thick;
  const float ro = r + half + 0.5f;
  const float ri = r - half - 0.5f;
  const float r2 = r * r;
  const float inv2r = 0.5f / r;
  const int span = static_cast<int>(ceilf(ro));
  for (int dyi = -span; dyi <= span; dyi++) {
    const int py = cy + dyi;
    if (py < 0 || py >= SCREEN_H) continue;
    const float dy = static_cast<float>(dyi);
    const float dy2 = dy * dy;
    if (dy2 >= ro * ro) continue;
    const int ox_hi = static_cast<int>(ceilf(sqrtf(ro * ro - dy2)));
    const int ox_lo = (ri > 0.0f && dy2 < ri * ri) ? static_cast<int>(floorf(sqrtf(ri * ri - dy2))) : 0;
    for (int ox = ox_lo; ox <= ox_hi; ox++) {
      const float d2 = static_cast<float>(ox * ox) + dy2;
      const int a = aa_coverage_8(half + 0.5f - std::fabs(d2 - r2) * inv2r);
      if (a <= 0) continue;
      fast_pixel_blend(it, cx - ox, py, c, a);
      if (ox > 0) fast_pixel_blend(it, cx + ox, py, c, a);
    }
  }
}

// -------------------------------------------------------------------------------------------------
// Glass surfaces (docs/device-ui-design.md D-029). A glass card blends its tint over the pixels already
// in the framebuffer, so the sky, weather or wallpaper behind it shows through. The blend runs at 8-bit
// alpha in the RGB565 level domain and quantizes with the same 4x4 ordered dither as the gradients
// (D-027); edge pixels scale the alpha by the anti-aliased coverage. Readability comes first: before
// drawing, sample_glass_backdrop() reads the backdrop under the card and glass_alpha_for_text() picks
// the lightest tint (never below kGlassAlphaMinQ8) at which the card's text still clears
// kGlassTextContrast against the mean of the brightest kGlassBrightPercent of that backdrop (white
// clouds, bright wallpapers, daylight sky). No blur: a box blur of the card area would cost a full
// extra read pass per frame for little legibility gain.
// -------------------------------------------------------------------------------------------------
static constexpr int kGlassAlphaMinQ8 = 154;       // 60 % tint: the lightest the glass ever gets
static constexpr float kGlassTextContrast = 5.0f;  // card text vs the glass (4.5:1 plus margin for sampling)
static constexpr int kGlassBrightPercent = 3;      // backdrop share the contrast is computed against
static constexpr int kGlassSheenTopQ8 = 92;        // 1 px edge: 36 % white along the top...
static constexpr int kGlassSheenBotQ8 = 28;        // ...fading to 11 % along the bottom
static constexpr int kGlassAlphaFallQ8 = 4;        // per-frame step toward a lighter glass (darker is immediate)

// sRGB 8-bit -> linear light, for WCAG relative luminance.
inline const float *srgb_linear_lut() {
  static float lut[256];
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < 256; i++) {
      const float c = static_cast<float>(i) / 255.0f;
      lut[i] = (c <= 0.04045f) ? (c / 12.92f) : powf((c + 0.055f) / 1.055f, 2.4f);
    }
    ready = true;
  }
  return lut;
}

inline float relative_luminance(esphome::Color c) {
  const float *lut = srgb_linear_lut();
  return 0.2126f * lut[c.r] + 0.7152f * lut[c.g] + 0.0722f * lut[c.b];
}

inline float contrast_ratio(float la, float lb) {
  const float hi = std::max(la, lb), lo = std::min(la, lb);
  return (hi + 0.05f) / (lo + 0.05f);
}

// c_over blended over c_under at alpha a (0..256).
inline esphome::Color mix_color_q8(esphome::Color c_under, esphome::Color c_over, int a) {
  a = std::max(0, std::min(256, a));
  const int ia = 256 - a;
  return esphome::Color(static_cast<uint8_t>((c_over.r * a + c_under.r * ia) >> 8),
                        static_cast<uint8_t>((c_over.g * a + c_under.g * ia) >> 8),
                        static_cast<uint8_t>((c_over.b * a + c_under.b * ia) >> 8));
}

struct GlassBackdrop {
  esphome::Color bright{};  // mean of the brightest kGlassBrightPercent of the samples
  esphome::Color mean{};    // mean of all samples
  bool valid = false;
};

// Samples the framebuffer under [x0, x0 + w) x [y0, y0 + h) on a sparse grid (about 12 rows).
inline GlassBackdrop sample_glass_backdrop(int x0, int y0, int w, int h) {
  GlassBackdrop out;
  const uint16_t *fb = active_frame_buf16();
  const int xs = std::max(0, x0), xe = std::min(SCREEN_W, x0 + w);
  const int ys = std::max(0, y0), ye = std::min(SCREEN_H, y0 + h);
  if (fb == nullptr || xe <= xs || ye <= ys) return out;
  const float *lut = srgb_linear_lut();
  const int step = std::max(3, h / 12);
  uint32_t cnt[32] = {0}, sr[32] = {0}, sg[32] = {0}, sb[32] = {0};
  uint32_t tr = 0, tg = 0, tb = 0, n = 0;
  for (int y = ys + step / 2; y < ye; y += step) {
    const uint16_t *row = fb + y * SCREEN_W;
    for (int x = xs + step / 2; x < xe; x += step) {
      const uint16_t p = decode_hw_rgb565_raw(row[x]);
      const int r5 = (p >> 11) & 0x1F, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
      const int r = (r5 << 3) | (r5 >> 2), g = (g6 << 2) | (g6 >> 4), b = (b5 << 3) | (b5 >> 2);
      const float lum = 0.2126f * lut[r] + 0.7152f * lut[g] + 0.0722f * lut[b];
      const int bin = std::min(31, static_cast<int>(lum * 32.0f));
      cnt[bin]++;
      sr[bin] += r;
      sg[bin] += g;
      sb[bin] += b;
      tr += r;
      tg += g;
      tb += b;
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
  out.bright = esphome::Color(static_cast<uint8_t>(ar / acc), static_cast<uint8_t>(ag / acc),
                              static_cast<uint8_t>(ab / acc));
  out.mean = esphome::Color(static_cast<uint8_t>(tr / n), static_cast<uint8_t>(tg / n), static_cast<uint8_t>(tb / n));
  out.valid = true;
  return out;
}

// Lightest glass alpha (kGlassAlphaMinQ8..256) at which `text` clears kGlassTextContrast over the
// tint blended onto `backdrop`. 256 = opaque tint.
inline int glass_alpha_for_text(esphome::Color tint, esphome::Color backdrop, esphome::Color text) {
  const float lt = relative_luminance(text);
  for (int a = kGlassAlphaMinQ8; a < 256; a += 8) {
    if (contrast_ratio(lt, relative_luminance(mix_color_q8(backdrop, tint, a))) >= kGlassTextContrast) return a;
  }
  return 256;
}

// Per-surface smoothing: darker at once (readability), lighter by kGlassAlphaFallQ8 per frame, so
// drifting clouds never make the glass flicker.
inline int glass_alpha_smooth(int &state, int target) {
  if (state <= 0 || target >= state) state = target;
  else state = std::max(target, state - kGlassAlphaFallQ8);
  return state;
}

// Rounded rectangle (same geometry and hole rule as fill_round_rect_aa) blending `tint` over the
// framebuffer with alpha a_top (first row) -> a_bot (last row), 0..256, ordered-dithered.
template<typename DisplayT>
inline void fill_round_rect_glass(DisplayT &it, int x0, int y0, int w, int h, float rad,
                                  esphome::Color tint, int a_top, int a_bot, int hole = 0) {
  if (w <= 0 || h <= 0) return;
  uint16_t *fb = active_frame_buf16();
  if (fb == nullptr) {
    fill_round_rect_aa(it, x0, y0, w, h, rad, tint, tint, hole);  // no framebuffer to read: opaque
    return;
  }
  AaRoundRect o;
  o.init(x0, y0, w, h, rad);
  AaRoundRect in;
  const bool has_hole = (hole > 0 && w > 2 * hole && h > 2 * hole);
  if (has_hole) in.init(x0 + hole, y0 + hole, w - 2 * hole, h - 2 * hole, rad - static_cast<float>(hole));
  const uint32_t tr = (static_cast<uint32_t>(tint.r) * 31U * 256U + 127U) / 255U;  // tint at 5.8 / 6.8 levels
  const uint32_t tg = (static_cast<uint32_t>(tint.g) * 63U * 256U + 127U) / 255U;
  const uint32_t tb = (static_cast<uint32_t>(tint.b) * 31U * 256U + 127U) / 255U;
  auto mix = [&](uint16_t &dst, int a, int t) {
    const uint16_t bg = decode_hw_rgb565_raw(dst);
    const uint32_t ia = static_cast<uint32_t>(256 - a);
    const uint32_t ua = static_cast<uint32_t>(a);
    uint32_t r = ((tr * ua) / 256U + ((bg >> 11) & 0x1FU) * ia + static_cast<uint32_t>(t)) >> 8;
    uint32_t g = ((tg * ua) / 256U + ((bg >> 5) & 0x3FU) * ia + static_cast<uint32_t>(t)) >> 8;
    uint32_t b = ((tb * ua) / 256U + (bg & 0x1FU) * ia + static_cast<uint32_t>(t)) >> 8;
    if (r > 31U) r = 31U;
    if (g > 63U) g = 63U;
    if (b > 31U) b = 31U;
    dst = encode_hw_rgb565_raw(static_cast<uint16_t>((r << 11) | (g << 5) | b));
  };
  const int ys = std::max(0, y0);
  const int ye = std::min(SCREEN_H, y0 + h);
  for (int py = ys; py < ye; py++) {
    float dy = 0.0f;
    int s0 = 0, s1 = -1, e0 = 0, e1 = -1;
    if (!o.row(py, dy, s0, s1, e0, e1)) continue;
    const int a = (h > 1) ? (a_top + ((a_bot - a_top) * (py - y0)) / (h - 1)) : a_top;
    if (a <= 0) continue;
    const uint8_t *th = kBayer4[py & 3];
    uint16_t *row = fb + py * SCREEN_W;
    auto span = [&](int xa, int xb) {
      xa = std::max(xa, 0);
      xb = std::min(xb, SCREEN_W - 1);
      for (int x = xa; x <= xb; x++) mix(row[x], a, th[x & 3] * 16 + 8);
    };
    auto edge = [&](int px) {
      if (px < 0 || px >= SCREEN_W) return;
      const int cov = o.coverage(px, dy);
      if (cov <= 0) return;
      mix(row[px], (a * cov + 127) / 255, th[px & 3] * 16 + 8);
    };
    if (s1 >= s0) {
      int hs0 = 1, hs1 = 0;
      if (has_hole) {
        float idy = 0.0f;
        int ie0 = 0, ie1 = -1;
        if (!in.row(py, idy, hs0, hs1, ie0, ie1)) {
          hs0 = 1;
          hs1 = 0;
        }
      }
      if (hs1 >= hs0) {
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

// Glass card: a 1 px white sheen edge (kGlassSheenTopQ8 -> kGlassSheenBotQ8) around the tint at
// alpha_q8 (from glass_alpha_for_text / glass_alpha_smooth).
template<typename DisplayT>
inline void render_glass_card(DisplayT &it, int x0, int y0, int w, int h, int r, esphome::Color tint, int alpha_q8) {
  if (w <= 4 || h <= 4) return;
  r = std::max(2, std::min(r, std::min(w, h) / 2));
  fill_round_rect_glass(it, x0, y0, w, h, static_cast<float>(r), kChromeGlassSheen, kGlassSheenTopQ8,
                        kGlassSheenBotQ8, 1);
  fill_round_rect_glass(it, x0 + 1, y0 + 1, w - 2, h - 2, static_cast<float>(r) - 1.0f, tint, alpha_q8, alpha_q8);
}

// Same Bresenham walk as Display::line (endpoints inclusive); axis-aligned lines use spans.
template<typename DisplayT>
inline void fast_line(DisplayT &it, int x1, int y1, int x2, int y2, esphome::Color c) {
  uint16_t *fb = active_frame_buf16();
  if (fb == nullptr) {
    it.line(x1, y1, x2, y2, c);
    return;
  }
  if (y1 == y2) {
    fast_hline(it, std::min(x1, x2), y1, std::abs(x2 - x1) + 1, c);
    return;
  }
  if (x1 == x2) {
    fast_vline(it, x1, std::min(y1, y2), std::abs(y2 - y1) + 1, c);
    return;
  }
  const uint16_t px = encode_hw_rgb565(c);
  const int32_t dx = std::abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
  const int32_t dy = -std::abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
  int32_t err = dx + dy;
  while (true) {
    if (static_cast<unsigned>(x1) < static_cast<unsigned>(SCREEN_W) &&
        static_cast<unsigned>(y1) < static_cast<unsigned>(SCREEN_H)) {
      fb[y1 * SCREEN_W + x1] = px;
    }
    if (x1 == x2 && y1 == y2) break;
    const int32_t e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x1 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y1 += sy;
    }
  }
}

// Port of Display::filled_flat_side_triangle_ (y2 == y3), drawing spans with fast_hline.
template<typename DisplayT>
inline void fast_filled_flat_side_triangle(DisplayT &it, int x1, int y1, int x2, int y2, int x3, int y3,
                                           esphome::Color c) {
  int s1_x = x1, s1_y = y1;
  bool s1_swap = false;
  int s1_dx = std::abs(x2 - x1), s1_dy = std::abs(y2 - y1);
  const int s1_sx = ((x2 - x1) >= 0) ? 1 : -1;
  const int s1_sy = ((y2 - y1) >= 0) ? 1 : -1;
  if (s1_dy > s1_dx) {
    std::swap(s1_dx, s1_dy);
    s1_swap = true;
  }
  int s1_err = 2 * s1_dy - s1_dx;

  int s2_x = x1, s2_y = y1;
  bool s2_swap = false;
  int s2_dx = std::abs(x3 - x1), s2_dy = std::abs(y3 - y1);
  const int s2_sx = ((x3 - x1) >= 0) ? 1 : -1;
  const int s2_sy = ((y3 - y1) >= 0) ? 1 : -1;
  if (s2_dy > s2_dx) {
    std::swap(s2_dx, s2_dy);
    s2_swap = true;
  }
  int s2_err = 2 * s2_dy - s2_dx;

  for (int i = 0; i <= s1_dx; i++) {
    if (s1_x <= s2_x) {
      fast_hline(it, s1_x, s1_y, s2_x - s1_x + 1, c);
    } else {
      fast_hline(it, s2_x, s2_y, s1_x - s2_x + 1, c);
    }
    while (s1_err >= 0) {
      if (s1_swap) {
        s1_x += s1_sx;
      } else {
        s1_y += s1_sy;
      }
      s1_err -= 2 * s1_dx;
    }
    if (s1_swap) {
      s1_y += s1_sy;
    } else {
      s1_x += s1_sx;
    }
    s1_err += 2 * s1_dy;
    while (s2_y != s1_y) {
      while (s2_err >= 0) {
        if (s2_swap) {
          s2_x += s2_sx;
        } else {
          s2_y += s2_sy;
        }
        s2_err -= 2 * s2_dx;
      }
      if (s2_swap) {
        s2_y += s2_sy;
      } else {
        s2_x += s2_sx;
      }
      s2_err += 2 * s2_dy;
    }
  }
}

// Same fill as Display::filled_triangle (identical pixels), drawing spans with fast_hline.
// Upstream never terminates when all three vertices share a row and the first two (after its y
// sort) coincide: the first edge has zero length, so its Bresenham error never goes negative and
// the task watchdog resets the device (seen with edge-on tumbling leaves). That case is the span
// between the two distinct x values.
template<typename DisplayT>
inline void fast_filled_triangle(DisplayT &it, int x1, int y1, int x2, int y2, int x3, int y3,
                                 esphome::Color c) {
  if (y1 > y2) {
    std::swap(x1, x2);
    std::swap(y1, y2);
  }
  if (y1 > y3) {
    std::swap(x1, x3);
    std::swap(y1, y3);
  }
  if (y2 > y3) {
    std::swap(x2, x3);
    std::swap(y2, y3);
  }
  if (y1 == y3 && x1 == x2) {
    fast_hline(it, std::min(x1, x3), y1, std::abs(x3 - x1) + 1, c);
    return;
  }
  if (y2 == y3) {
    fast_filled_flat_side_triangle(it, x1, y1, x2, y2, x3, y3, c);
  } else if (y1 == y2) {
    fast_filled_flat_side_triangle(it, x3, y3, x1, y1, x2, y2, c);
  } else {
    const int x_temp = static_cast<int>(x1 + (static_cast<float>(y2 - y1) / static_cast<float>(y3 - y1)) * (x3 - x1));
    fast_filled_flat_side_triangle(it, x1, y1, x2, y2, x_temp, y2, c);
    fast_filled_flat_side_triangle(it, x3, y3, x2, y2, x_temp, y2, c);
  }
}

// UTF-8 decoder matching ESPHome's unicode::extract_unicode_codepoint for well-formed text.
// Advances p; returns 0 at the terminator.
inline uint32_t latter_utf8_next(const char *&p) {
  const uint8_t c0 = static_cast<uint8_t>(p[0]);
  if (c0 == 0) return 0;
  if (c0 < 0x80) {
    p += 1;
    return c0;
  }
  if ((c0 & 0xE0) == 0xC0 && p[1] != 0) {
    const uint32_t cp = (static_cast<uint32_t>(c0 & 0x1F) << 6) | (static_cast<uint8_t>(p[1]) & 0x3F);
    p += 2;
    return cp;
  }
  if ((c0 & 0xF0) == 0xE0 && p[1] != 0 && p[2] != 0) {
    const uint32_t cp = (static_cast<uint32_t>(c0 & 0x0F) << 12) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(p[1]) & 0x3F) << 6) |
                        (static_cast<uint8_t>(p[2]) & 0x3F);
    p += 3;
    return cp;
  }
  if ((c0 & 0xF8) == 0xF0 && p[1] != 0 && p[2] != 0 && p[3] != 0) {
    const uint32_t cp = (static_cast<uint32_t>(c0 & 0x07) << 18) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(p[1]) & 0x3F) << 12) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(p[2]) & 0x3F) << 6) |
                        (static_cast<uint8_t>(p[3]) & 0x3F);
    p += 4;
    return cp;
  }
  p += 1;  // malformed byte: skip it
  return 0xFFFD;
}

// fast_print can render the text itself: framebuffer bound, font bpp <= 4 and every codepoint has a
// glyph (Font::print draws a placeholder box for missing glyphs, so those strings use it.print).
template<typename FontT>
inline bool fast_print_supported(FontT *font, const char *text) {
  if (active_frame_buf16() == nullptr || font == nullptr || text == nullptr) return false;
  if (font->get_bpp() > 4) return false;
  for (const char *p = text; *p != 0;) {
    if (font->find_glyph(latter_utf8_next(p)) == nullptr) return false;
  }
  return true;
}

// 1-bpp text span cache. Decoding glyph bits is most of fast_print's cost, and the same strings are
// drawn every frame (clock, date, labels, drop shadows at +1/+1). Each (font, text) is decoded once
// into horizontal runs relative to the text origin and kept in PSRAM (16-entry LRU, strings up to 47
// bytes); frames then just fill the runs. Pixel coverage is identical to the per-pixel decode.
struct TextSpan {
  int16_t x;
  int16_t y;
  int16_t len;
};

struct TextCacheEntry {
  const void *font;
  uint32_t last_use;
  TextSpan *spans;
  uint16_t count;
  uint16_t cap;
  char text[48];
};

static constexpr int kTextCacheEntries = 16;
static constexpr int kTextScratchSpans = 4096;

inline TextCacheEntry *text_cache_entries() {
  static TextCacheEntry *entries = static_cast<TextCacheEntry *>(
      heap_caps_calloc(kTextCacheEntries, sizeof(TextCacheEntry), MALLOC_CAP_SPIRAM));
  return entries;
}

inline TextSpan *text_cache_scratch() {
  static TextSpan *scratch = static_cast<TextSpan *>(
      heap_caps_malloc(sizeof(TextSpan) * kTextScratchSpans, MALLOC_CAP_SPIRAM));
  return scratch;
}

// Decodes a 1-bpp string into runs relative to the text origin. Returns the run count, or -1 if the
// scratch buffer would overflow.
template<typename FontT>
inline int text_decode_spans_1bpp(FontT *font, const char *text, TextSpan *out, int cap) {
  int n = 0;
  int x_at = 0;
  for (const char *p = text; *p != 0;) {
    const auto *glyph = font->find_glyph(latter_utf8_next(p));
    const uint8_t *data = glyph->data;
    const int gx0 = x_at + glyph->offset_x;
    uint8_t bitmask = 0;
    uint8_t pixel_data = 0;
    for (int gy = 0; gy < glyph->height; gy++) {
      int run_start = -1;
      for (int gx = 0; gx < glyph->width; gx++) {
        if (bitmask == 0) {
          pixel_data = esphome::progmem_read_byte(data++);
          bitmask = 0x80;
        }
        const bool on = (pixel_data & bitmask) != 0;
        bitmask >>= 1;
        if (on) {
          if (run_start < 0) run_start = gx;
        } else if (run_start >= 0) {
          if (n >= cap) return -1;
          out[n++] = {static_cast<int16_t>(gx0 + run_start), static_cast<int16_t>(glyph->offset_y + gy),
                      static_cast<int16_t>(gx - run_start)};
          run_start = -1;
        }
      }
      if (run_start >= 0) {
        if (n >= cap) return -1;
        out[n++] = {static_cast<int16_t>(gx0 + run_start), static_cast<int16_t>(glyph->offset_y + gy),
                    static_cast<int16_t>(glyph->width - run_start)};
      }
    }
    x_at += glyph->advance;
  }
  return n;
}

// Returns the cached runs for (font, text), decoding on a miss. nullptr if the text can't be cached
// (too long, PSRAM exhausted, or more runs than the scratch buffer holds).
template<typename FontT>
inline const TextSpan *text_cache_get(FontT *font, const char *text, int &count) {
  TextCacheEntry *entries = text_cache_entries();
  TextSpan *scratch = text_cache_scratch();
  const size_t len = std::strlen(text);
  if (entries == nullptr || scratch == nullptr || len >= sizeof(entries[0].text)) return nullptr;
  static uint32_t use_seq = 0;
  ++use_seq;
  TextCacheEntry *victim = &entries[0];
  for (int i = 0; i < kTextCacheEntries; i++) {
    TextCacheEntry &e = entries[i];
    if (e.font == font && std::strcmp(e.text, text) == 0) {
      e.last_use = use_seq;
      count = e.count;
      return e.spans;
    }
    if (e.last_use < victim->last_use) victim = &e;
  }
  const int n = text_decode_spans_1bpp(font, text, scratch, kTextScratchSpans);
  if (n < 0) return nullptr;
  if (victim->cap < n) {
    if (victim->spans != nullptr) heap_caps_free(victim->spans);
    victim->spans = static_cast<TextSpan *>(heap_caps_malloc(sizeof(TextSpan) * (n > 0 ? n : 1), MALLOC_CAP_SPIRAM));
    victim->cap = (victim->spans != nullptr) ? static_cast<uint16_t>(n) : 0;
    if (victim->spans == nullptr) {
      victim->font = nullptr;
      victim->text[0] = 0;
      victim->count = 0;
      count = n;
      return scratch;  // draw this frame from scratch; not cached
    }
  }
  if (n > 0) std::memcpy(victim->spans, scratch, sizeof(TextSpan) * n);
  victim->font = font;
  std::memcpy(victim->text, text, len + 1);
  victim->count = static_cast<uint16_t>(n);
  victim->last_use = use_seq;
  count = n;
  return victim->spans;
}

// Fast equivalent of Display::print(x, y, font, color, align, text, background). Same bounds
// (Display::get_text_bounds), same glyph walk, bit unpacking and background blend as Font::print,
// but glyph pixels are stored straight into the bound framebuffer instead of one virtual
// draw_pixel_at each. Pixel output is identical.
template<typename DisplayT, typename FontT>
inline void fast_print(DisplayT &it, int x, int y, FontT *font, esphome::Color color,
                       esphome::display::TextAlign align, const char *text,
                       esphome::Color background = esphome::display::COLOR_OFF) {
  if (!fast_print_supported(font, text)) {
    it.print(x, y, font, color, align, text, background);
    return;
  }
  uint16_t *fb = active_frame_buf16();
  int x_start = 0, y_start = 0, text_w = 0, text_h = 0;
  it.get_text_bounds(x, y, text, font, align, &x_start, &y_start, &text_w, &text_h);

  const int bpp = font->get_bpp();
  const int bpp_max = (1 << bpp) - 1;
  // Hardware pixel per coverage level, blended exactly as Font::print does.
  uint16_t lut[16];
  const float diff_r = static_cast<float>(color.r) - static_cast<float>(background.r);
  const float diff_g = static_cast<float>(color.g) - static_cast<float>(background.g);
  const float diff_b = static_cast<float>(color.b) - static_cast<float>(background.b);
  const float diff_w = static_cast<float>(color.w) - static_cast<float>(background.w);
  for (int lv = 1; lv <= bpp_max; lv++) {
    if (lv == bpp_max) {
      lut[lv] = encode_hw_rgb565(color);
    } else {
      const float on = static_cast<float>(lv) / static_cast<float>(bpp_max);
      lut[lv] = encode_hw_rgb565(esphome::Color(static_cast<uint8_t>(diff_r * on + static_cast<float>(background.r)),
                                                static_cast<uint8_t>(diff_g * on + static_cast<float>(background.g)),
                                                static_cast<uint8_t>(diff_b * on + static_cast<float>(background.b)),
                                                static_cast<uint8_t>(diff_w * on + static_cast<float>(background.w))));
    }
  }

  if (bpp == 1) {
    int span_count = 0;
    const TextSpan *spans = text_cache_get(font, text, span_count);
    if (spans != nullptr) {
      const uint16_t hw = lut[1];
      for (int i = 0; i < span_count; i++) {
        const int py = y_start + spans[i].y;
        if (static_cast<unsigned>(py) >= static_cast<unsigned>(SCREEN_H)) continue;
        int px = x_start + spans[i].x;
        int len = spans[i].len;
        if (px < 0) {
          len += px;
          px = 0;
        }
        if (px + len > SCREEN_W) len = SCREEN_W - px;
        uint16_t *dst = fb + py * SCREEN_W + px;
        for (int k = 0; k < len; k++) dst[k] = hw;
      }
      return;
    }
  }

  int x_at = x_start;
  for (const char *p = text; *p != 0;) {
    const auto *glyph = font->find_glyph(latter_utf8_next(p));
    const uint8_t *data = glyph->data;
    const int gx0 = x_at + glyph->offset_x;
    const int gy0 = y_start + glyph->offset_y;
    uint8_t bitmask = 0;
    uint8_t pixel_data = 0;
    for (int gy = 0; gy < glyph->height; gy++) {
      const int py = gy0 + gy;
      uint16_t *row = (static_cast<unsigned>(py) < static_cast<unsigned>(SCREEN_H)) ? fb + py * SCREEN_W : nullptr;
      for (int gx = 0; gx < glyph->width; gx++) {
        uint8_t pixel = 0;
        for (int bit = 0; bit < bpp; bit++) {
          if (bitmask == 0) {
            pixel_data = esphome::progmem_read_byte(data++);
            bitmask = 0x80;
          }
          pixel <<= 1;
          if ((pixel_data & bitmask) != 0) pixel |= 1;
          bitmask >>= 1;
        }
        if (pixel != 0 && row != nullptr) {
          const int px = gx0 + gx;
          if (static_cast<unsigned>(px) < static_cast<unsigned>(SCREEN_W)) row[px] = lut[pixel];
        }
      }
    }
    x_at += glyph->advance;
  }
}

template<typename DisplayT, typename FontT>
inline void render_pill_button(DisplayT &it, int x1, int x2, int y_mid, int r,
                               esphome::Color border_c, esphome::Color bg_c,
                               const char *label, esphome::Color text_c, FontT *font) {
  // Anti-aliased stadium: cap centers at x1 and x2, radius r, a 2 px border.
  const int px0 = x1 - r;
  const int pw = (x2 - x1) + 2 * r + 1;
  const int ph = 2 * r + 1;
  stroke_round_rect_aa(it, px0, y_mid - r, pw, ph, static_cast<float>(r) + 0.5f, border_c, border_c, 2);
  if (r > 2) fill_round_rect_aa(it, px0 + 2, y_mid - r + 2, pw - 4, ph - 4, static_cast<float>(r) - 1.5f, bg_c);

  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    int btn_cx = (x1 + x2) / 2;
    fast_print(it, btn_cx, y_mid, font, text_c, esphome::display::TextAlign::CENTER, label);
  }
}

template<typename DisplayT, typename FontT>
inline void render_modern_card(DisplayT &it, int cx_card, int cy_card, int w, int h, int r,
                               esphome::Color bg_c, esphome::Color border_c,
                               const char *label, esphome::Color text_c, FontT *font) {
  if (SCREEN_W > 520) {
    if (w <= 340) w = scale_x(w);
    if (h <= 96)  h = scale_y(h);
    if (r <= 24)  r = scale_s(r);
  } else if (std::abs(cx_card - SCREEN_CX) <= 4) {
    if (SCREEN_W < 380 && h <= 48 && w >= 240 && w <= 330) {
      w = SCREEN_W - 88; // 232px on 320px screen: leaves a clean 26px gap before right-flank scroll arrows!
    } else if (w > SCREEN_W - 20) {
      w = SCREEN_W - 20;
    }
  }
  if (r > h / 2) r = h / 2;
  if (r < 2) r = 2;
  const int x0 = cx_card - w / 2;
  const int y0 = cy_card - h / 2;

  const bool is_lit_glass = (static_cast<int>(bg_c.r) + static_cast<int>(bg_c.g) + static_cast<int>(bg_c.b)) > 12;
  const esphome::Color brd_top = is_lit_glass
      ? esphome::Color(static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.r) + 28)),
                       static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.g) + 32)),
                       static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.b) + 38)))
      : border_c;
  const esphome::Color brd_btm = is_lit_glass
      ? esphome::Color(static_cast<uint8_t>((static_cast<int>(border_c.r) * 180) >> 8),
                       static_cast<uint8_t>((static_cast<int>(border_c.g) * 180) >> 8),
                       static_cast<uint8_t>((static_cast<int>(border_c.b) * 180) >> 8))
      : border_c;
  const esphome::Color bg_top = is_lit_glass
      ? esphome::Color(static_cast<uint8_t>(std::min(255, static_cast<int>(bg_c.r) + 6)),
                       static_cast<uint8_t>(std::min(255, static_cast<int>(bg_c.g) + 8)),
                       static_cast<uint8_t>(std::min(255, static_cast<int>(bg_c.b) + 11)))
      : bg_c;
  const esphome::Color bg_btm = is_lit_glass
      ? esphome::Color(static_cast<uint8_t>(std::max(0, static_cast<int>(bg_c.r) - 4)),
                       static_cast<uint8_t>(std::max(0, static_cast<int>(bg_c.g) - 5)),
                       static_cast<uint8_t>(std::max(0, static_cast<int>(bg_c.b) - 7)))
      : bg_c;

  // Anti-aliased rounded rectangle: a 1 px border lit from the top (brd_top -> brd_btm) around a
  // smooth, dithered sheen (bg_top -> bg_btm). Unlit cards are flat.
  stroke_round_rect_aa(it, x0, y0, w, h, static_cast<float>(r), brd_top, brd_btm, 1);
  fill_round_rect_aa(it, x0 + 1, y0 + 1, w - 2, h - 2, static_cast<float>(r) - 1.0f, bg_top, bg_btm);

  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    fast_print(it, cx_card, cy_card, font, text_c, esphome::display::TextAlign::CENTER, label);
  }
}

template<typename DisplayT>
inline void render_modern_card(DisplayT &it, int x0, int y0, int w, int h,
                               esphome::Color bg_c, esphome::Color border_c, int r = 10) {
  if (w <= 4 || h <= 4) return;
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  if (r < 2) r = 2;

  const bool is_lit_glass = (static_cast<int>(bg_c.r) + static_cast<int>(bg_c.g) + static_cast<int>(bg_c.b)) > 12;
  const esphome::Color brd_top = is_lit_glass
      ? esphome::Color(static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.r) + 28)),
                       static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.g) + 32)),
                       static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.b) + 38)))
      : border_c;
  const esphome::Color brd_btm = is_lit_glass
      ? esphome::Color(static_cast<uint8_t>((static_cast<int>(border_c.r) * 180) >> 8),
                       static_cast<uint8_t>((static_cast<int>(border_c.g) * 180) >> 8),
                       static_cast<uint8_t>((static_cast<int>(border_c.b) * 180) >> 8))
      : border_c;
  const esphome::Color bg_top = is_lit_glass
      ? esphome::Color(static_cast<uint8_t>(std::min(255, static_cast<int>(bg_c.r) + 6)),
                       static_cast<uint8_t>(std::min(255, static_cast<int>(bg_c.g) + 8)),
                       static_cast<uint8_t>(std::min(255, static_cast<int>(bg_c.b) + 11)))
      : bg_c;
  const esphome::Color bg_btm = is_lit_glass
      ? esphome::Color(static_cast<uint8_t>(std::max(0, static_cast<int>(bg_c.r) - 4)),
                       static_cast<uint8_t>(std::max(0, static_cast<int>(bg_c.g) - 5)),
                       static_cast<uint8_t>(std::max(0, static_cast<int>(bg_c.b) - 7)))
      : bg_c;

  // Anti-aliased rounded rectangle: a 1 px border lit from the top (brd_top -> brd_btm) around a
  // smooth, dithered sheen (bg_top -> bg_btm). Unlit cards are flat.
  stroke_round_rect_aa(it, x0, y0, w, h, static_cast<float>(r), brd_top, brd_btm, 1);
  fill_round_rect_aa(it, x0 + 1, y0 + 1, w - 2, h - 2, static_cast<float>(r) - 1.0f, bg_top, bg_btm);
}

// Frosted-glass circular button with a top-lit rim
template<typename DisplayT>
inline void render_glass_circle(DisplayT &it, int cx_btn, int cy_btn, int r,
                                esphome::Color bg_c, esphome::Color border_c) {
  if (r <= 1) return;
  const esphome::Color brd_top(
      static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.r) + 34)),
      static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.g) + 38)),
      static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.b) + 44)));
  const esphome::Color brd_btm(
      static_cast<uint8_t>((static_cast<int>(border_c.r) * 165) >> 8),
      static_cast<uint8_t>((static_cast<int>(border_c.g) * 165) >> 8),
      static_cast<uint8_t>((static_cast<int>(border_c.b) * 165) >> 8));
  const esphome::Color bg_top(
      static_cast<uint8_t>(std::min(255, static_cast<int>(bg_c.r) + 7)),
      static_cast<uint8_t>(std::min(255, static_cast<int>(bg_c.g) + 9)),
      static_cast<uint8_t>(std::min(255, static_cast<int>(bg_c.b) + 12)));
  const esphome::Color bg_btm(
      static_cast<uint8_t>(std::max(0, static_cast<int>(bg_c.r) - 5)),
      static_cast<uint8_t>(std::max(0, static_cast<int>(bg_c.g) - 6)),
      static_cast<uint8_t>(std::max(0, static_cast<int>(bg_c.b) - 8)));

  // Anti-aliased: a 1 px rim lit from the top (brd_top -> brd_btm) around a dithered sheen.
  stroke_round_rect_aa(it, cx_btn - r, cy_btn - r, 2 * r + 1, 2 * r + 1, static_cast<float>(r) + 0.5f, brd_top, brd_btm, 1);
  fast_filled_circle_aa_grad(it, cx_btn, cy_btn, r - 1, bg_top, bg_btm);
}

// Ultra-fast (<2us) 4x4 grid sampler that extracts an ambient color tint from an RGB565 image buffer
// so frosted glass cards and HUD controls subtly pick up the color palette of the underlying artwork.
template<typename ImageT>
inline esphome::Color sample_image_ambient_tint(ImageT *img,
                                                esphome::Color base_bg = kChromeGlassTint) {
  if (img == nullptr) return base_bg;
  const uint16_t *src = reinterpret_cast<const uint16_t *>(img->get_data_start());
  int w = img->get_width();
  int h = img->get_height();
  if (src == nullptr || w < 16 || h < 16) return base_bg;

  uint32_t sum_r = 0, sum_g = 0, sum_b = 0;
  for (int gy = 1; gy <= 4; gy++) {
    int sy = (h * gy) / 5;
    const uint16_t *row = src + sy * w;
    for (int gx = 1; gx <= 4; gx++) {
      uint16_t p = row[(w * gx) / 5];
      sum_r += (p & 0xF800U) >> 8;
      sum_g += (p & 0x07E0U) >> 3;
      sum_b += (p & 0x001FU) << 3;
    }
  }
  uint8_t avg_r = static_cast<uint8_t>(sum_r >> 4);
  uint8_t avg_g = static_cast<uint8_t>(sum_g >> 4);
  uint8_t avg_b = static_cast<uint8_t>(sum_b >> 4);
  return esphome::Color(
      static_cast<uint8_t>(std::min(68, static_cast<int>(base_bg.r) + (avg_r >> 3))),
      static_cast<uint8_t>(std::min(74, static_cast<int>(base_bg.g) + (avg_g >> 3))),
      static_cast<uint8_t>(std::min(88, static_cast<int>(base_bg.b) + (avg_b >> 3))));
}

template<typename DisplayT>
inline void render_scroll_chevron(DisplayT &it, int x, int y, bool is_up,
                                  esphome::Color c, esphome::Color shadow_c, bool shadow = false) {
  const int dx = scale_s(7);
  const int dy_hi = scale_s(5);
  const int dy_lo = scale_s(4);
  const int thick = (SCREEN_W > 520) ? 3 : 2;
  if (shadow) {
    for (int t = 0; t < thick; t++) {
      if (is_up) {
        fast_line(it, x - dx, y + dy_lo + 1 + t, x, y - dy_hi + 1 + t, shadow_c);
        fast_line(it, x + dx, y + dy_lo + 1 + t, x, y - dy_hi + 1 + t, shadow_c);
      } else {
        fast_line(it, x - dx, y - dy_lo + 1 + t, x, y + dy_hi + 1 + t, shadow_c);
        fast_line(it, x + dx, y - dy_lo + 1 + t, x, y + dy_hi + 1 + t, shadow_c);
      }
    }
  }
  for (int t = 0; t < thick; t++) {
    if (is_up) {
      fast_line(it, x - dx, y + dy_lo + t, x, y - dy_hi + t, c);
      fast_line(it, x + dx, y + dy_lo + t, x, y - dy_hi + t, c);
    } else {
      fast_line(it, x - dx, y - dy_hi + t, x, y + dy_lo + t, c);
      fast_line(it, x + dx, y - dy_hi + t, x, y + dy_lo + t, c);
    }
  }
}

template <typename DisplayT>
inline void render_bezel_alert_border(DisplayT &it, int cx, int cy, esphome::Color ring_c) {
#if LATTER_ROUND_DISPLAY
  for (int r = SCREEN_R - 10; r <= SCREEN_R - 7; r++) {
    fast_circle(it, cx, cy, r, ring_c);
  }
#else
  (void)cx;
  (void)cy;
  const int pad = LATTER_IS_WEARABLE ? 5 : 3;
  const int thick = 5;
  const int cr_out = std::max(8, LATTER_CORNER_R - pad);
  const int cr_in  = std::max(4, cr_out - thick);
  const int x_lc   = pad + cr_out;
  const int x_rc   = SCREEN_W - 1 - pad - cr_out;
  const int y_tc   = pad + cr_out;
  const int y_bc   = SCREEN_H - 1 - pad - cr_out;

  for (int y = pad; y <= SCREEN_H - 1 - pad; y++) {
    if (y >= y_tc && y <= y_bc) {
      fast_hline(it, pad, y, thick, ring_c);
      fast_hline(it, SCREEN_W - pad - thick, y, thick, ring_c);
    } else {
      int dy = (y < y_tc) ? (y_tc - y) : (y - y_bc);
      if (dy > cr_out) continue;
      int dx_out = chord_half_span_int(cr_out, dy, true);
      if (dy >= cr_in) {
        int x0 = x_lc - dx_out;
        int x1 = x_rc + dx_out;
        if (x1 >= x0) {
          fast_hline(it, x0, y, x1 - x0 + 1, ring_c);
        }
      } else {
        int dx_in = chord_half_span_int(cr_in, dy, false);
        int seg = std::max(thick, dx_out - dx_in + 1);
        fast_hline(it, x_lc - dx_out, y, seg, ring_c);
        fast_hline(it, x_rc + dx_out - seg + 1, y, seg, ring_c);
      }
    }
  }
#endif
}

template <typename DisplayT>
inline void render_home_indicator_bar(DisplayT &it, int cx, int y_pos = kHomeBarY,
                                      esphome::Color bar_col = kChromeHomePill) {
  const int half_w = scale_x(54);
  const int bar_h  = (SCREEN_H > 520) ? 7 : 5;
  const int cap_r  = bar_h / 2;
  const float cap = 0.5f * static_cast<float>(bar_h);
  fill_round_rect_aa(it, cx - half_w, y_pos - cap_r + 1, 2 * half_w + 1, bar_h, cap, kChromeShadow);
  fill_round_rect_aa(it, cx - half_w, y_pos - cap_r, 2 * half_w + 1, bar_h, cap, bar_col);
}

template <typename DisplayT>
inline void render_viewport_scrollbar(DisplayT &it, int cx, int y_pos, int total_items,
                                      float cur_start_idx, esphome::Color col_thumb,
                                      bool is_dim = false) {
  if (total_items <= 4) return;
  const int track_w = (SCREEN_W < 380) ? 84 : ((SCREEN_W > 520) ? 168 : 112);
  const int track_h = (SCREEN_W > 520) ? 6 : 4;
  const int r_cap   = track_h / 2;
  const int track_x0 = cx - (track_w / 2);
  const int track_y0 = y_pos - r_cap;

  const esphome::Color col_track = is_dim ? kChromeTrackDim : kChromeTrack;
  const esphome::Color col_brd   = is_dim ? kChromeTrackBorderDim : kChromeTrackBorder;

  const float cap = static_cast<float>(r_cap);
  if (!is_dim) {
    fill_round_rect_aa(it, track_x0 + 1, track_y0 + 1, track_w, track_h, cap, kChromeShadow);
  }
  stroke_round_rect_aa(it, track_x0, track_y0, track_w, track_h, cap, col_brd, col_brd, 1);
  fill_round_rect_aa(it, track_x0 + 1, track_y0 + 1, track_w - 2, track_h - 2, cap - 1.0f, col_track);

  const int min_thumb_w = (SCREEN_W > 520) ? 28 : 20;
  const int thumb_w = std::max(min_thumb_w, std::min(track_w, (track_w * 4) / total_items));
  const float max_start = static_cast<float>(std::max(1, total_items - 4));
  const float clamped_start = std::max(0.0f, std::min(max_start, cur_start_idx));
  const float frac = clamped_start / max_start;
  const int thumb_x0 = track_x0 + static_cast<int>( std::roundf(frac * static_cast<float>(track_w - thumb_w)) );

  fill_round_rect_aa(it, thumb_x0, track_y0, thumb_w, track_h, cap, col_thumb);
}

template <typename DisplayT>
inline void render_dock_page_pips(DisplayT &it, int cx, int y_pos, int total_pages, int active_page,
                                  esphome::Color col_active, esphome::Color col_inactive) {
  if (total_pages <= 1) return;
  const int spacing = scale_x(14);
  const int dot_r   = (SCREEN_W > 520) ? 3 : 2;
  const int total_w = (total_pages - 1) * spacing;
  const int start_x = cx - (total_w / 2);
  for (int p = 0; p < total_pages; p++) {
    int px = start_x + p * spacing;
    if (p == active_page) {
      int pw = scale_x(7);
      int ph = dot_r * 2 + 1;
      const int sx0 = px - pw / 2 - dot_r;
      const int sw = pw + 2 * dot_r + 1;
      const float cap = static_cast<float>(dot_r) + 0.5f;
      fill_round_rect_aa(it, sx0 + 1, y_pos - dot_r + 1, sw, ph, cap, kChromeShadow);
      fill_round_rect_aa(it, sx0, y_pos - dot_r, sw, ph, cap, col_active);
    } else {
      fast_filled_circle_aa(it, px + 1, y_pos + 1, dot_r, kChromeShadow);
      fast_filled_circle_aa(it, px, y_pos, dot_r, col_inactive);
    }
  }
}

template <typename DisplayT>
inline void render_vertical_page_pips(DisplayT &it, bool show_up, bool show_down,
                                      esphome::Color col_bg, esphome::Color col_active,
                                      esphome::Color col_inactive) {
  (void)col_inactive;
  if (!show_up && !show_down) return;
#if LATTER_ROUND_DISPLAY
  const int mask_x = 395;
  const int chev_x = 415;
#else
  const int mask_x = (SCREEN_W < 380) ? (SCREEN_W - 30) : ((SCREEN_W > 520) ? (SCREEN_W - 88) : (SCREEN_W - 42));
  const int chev_x = (SCREEN_W < 380) ? (SCREEN_W - 15) : ((SCREEN_W > 520) ? (SCREEN_W - 48) : (SCREEN_W - 21));
#endif
  const int up_y = scale_y(162);
  const int dn_y = scale_y(278);
  fill_rect_row_major(it, mask_x, up_y - scale_y(22), SCREEN_W - mask_x, (dn_y - up_y) + scale_y(44), col_bg);
  if (show_up) render_scroll_chevron(it, chev_x, up_y, true, col_active, kChromeShadow, false);
  if (show_down) render_scroll_chevron(it, chev_x, dn_y, false, col_active, kChromeShadow, false);
}

// Back affordance (docs/device-ui-design.md, return paths): "< Back" returns to the parent, "Exit" ends a
// game, "< Cancel" abandons an edit, and "Home" draws the home pill instead of a label. Callers pass
// layout().back_y (latter-verify [BACK BUTTON Y]).
template<typename DisplayT, typename FontT>
inline void render_back_button(DisplayT &it, int cx, FontT *font,
                               int y_pos = kBackLabelY, const char *label = "< Back",
                               esphome::Color text_col = kChromeBackLabel) {
  const int back_lbl_y = LATTER_ROUND_DISPLAY ? y_pos : ((y_pos >= 400) ? kBackLabelY : scale_y(y_pos));
  if (label == nullptr) return;
  if (std::strcmp(label, "Home") == 0) {
    render_home_indicator_bar(it, cx, kHomeBarY);
    return;
  }
  fast_print(it, cx + 1, back_lbl_y + 1, font, kChromeShadow, esphome::display::TextAlign::CENTER, label);
  fast_print(it, cx, back_lbl_y, font, text_col, esphome::display::TextAlign::CENTER, label);
}

struct TouchGestureState {
  int start_x = 0;
  int start_y = 0;
  int last_x = 0;
  int last_y = 0;
  int max_travel = 0;
  int sample_count = 0;
  uint32_t start_ms = 0;
  uint32_t last_touch_ms = 0;
  uint32_t last_swipe_ms = 0;
  int start_page = 0;
  int start_context = 0;
  bool active = false;
  bool swipe_committed = false;
};

inline TouchGestureState &touch_gesture() {
  static TouchGestureState st;
  return st;
}

inline uint32_t &last_music_page_req_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline void apply_custom_rgb_harmony(uint32_t rgb, esphome::Color &out_primary, esphome::Color &out_secondary) {
  uint8_t r = static_cast<uint8_t>((rgb >> 16) & 0xFF);
  uint8_t g = static_cast<uint8_t>((rgb >> 8) & 0xFF);
  uint8_t b = static_cast<uint8_t>(rgb & 0xFF);
  out_primary = esphome::Color(r, g, b);
  out_secondary = esphome::Color(192, 192, 192);
}

inline uint32_t parse_hex_rgb(const std::string &hex_str, uint32_t fallback = 0x3898FFU) {
  std::string s = hex_str;
  if (!s.empty() && s[0] == '#') s = s.substr(1);
  if (s.size() != 6) return fallback;
  char *endptr = nullptr;
  unsigned long val = std::strtoul(s.c_str(), &endptr, 16);
  if (endptr == s.c_str()) return fallback;
  return static_cast<uint32_t>(val & 0xFFFFFFUL);
}

inline std::string format_hex_rgb(uint32_t rgb) {
  char buf[12];
  snprintf(buf, sizeof(buf), "#%06lX", static_cast<unsigned long>(rgb & 0xFFFFFFUL));
  return std::string(buf);
}

inline float &color_wheel_hue_deg() {
  static float h = 210.0f;
  return h;
}

inline float &color_wheel_sat() {
  static float s = 0.78f;
  return s;
}

inline uint32_t &music_scroll_tap_lockout_until_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline int compute_touch_context(int gui_page, int home_sub, int music_mode, int active_game,
                                 int sys_mode, int weather_sub, int area_sub, int rem_sub, int tools_sub,
                                 int wh_tab = 0) {
  int sub = 0;
  if (gui_page == 1) sub = home_sub;
  else if (gui_page == 2) sub = music_mode;
  else if (gui_page == 3) sub = active_game;
  else if (gui_page == 4) sub = sys_mode;
  else if (gui_page == 5) sub = weather_sub;
  else if (gui_page == 6) sub = area_sub;
  else if (gui_page == 7) sub = rem_sub;
  else if (gui_page == 8) sub = tools_sub;
  else if (gui_page == 9) sub = wh_tab;
  return (gui_page * 100) + sub;
}

// Art dimming per row in 1/256 (256 = unchanged), with smooth ramps between levels so a scrim never
// shows a step. Applied with dim_rgb565_row_dither (ordered dither, full 16-bit output).
inline int compute_art_row_scale_q8(int y, bool apply_scrim, bool show_metadata, int dim_level) {
  if (dim_level >= 2) return 64;  // 25% ambient backdrop behind glass menu cards
  auto ramp = [](int yy, int y_a, int v_a, int y_b, int v_b) -> int {
    if (yy <= y_a) return v_a;
    if (yy >= y_b) return v_b;
    return v_a + ((v_b - v_a) * (yy - y_a)) / (y_b - y_a);
  };
  if (apply_scrim) {
    // 37.5% behind the header and the metadata, 50% behind the HUD transport buttons.
    const int ramp_h = scale_y(12);
    int v = ramp(y, scale_y(76) - ramp_h, 96, scale_y(76) + ramp_h, 128);
    if (show_metadata) v = std::min(v, ramp(y, scale_y(324) - ramp_h, 128, scale_y(324) + ramp_h, 96));
    return v;
  }
  if (show_metadata) {
    // Fade in behind the title and artist (100% -> 37.5%), and back out below the metadata.
    if (y < scale_y(410)) return ramp(y, scale_y(298), 256, scale_y(358), 96);
    return ramp(y, scale_y(410), 96, scale_y(422), 256);
  }
  return 256;
}

// Scales a row of native RGB565 by mul_q8/256 with a 4x4 ordered dither (screen position x0 + i, y).
inline void dim_rgb565_row_dither(uint16_t *row_buf, int span_w, int mul_q8, int x0, int y) {
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

template<typename DisplayT>
inline void blit_rgb565_scanline(DisplayT &it, int start_x, int y, int span_w,
                                 const uint16_t *row_buf, bool skip_black = false) {
  if (span_w <= 0 || y < 0 || y >= SCREEN_H || row_buf == nullptr) return;
  if (start_x < 0) {
    row_buf -= start_x;
    span_w += start_x;
    start_x = 0;
  }
  if (start_x + span_w > SCREEN_W) {
    span_w = SCREEN_W - start_x;
  }
  if (span_w <= 0) return;

  // ESPHome's MipiSpi::draw_pixels_at rejects native little-endian RGB565 buffers when the
  // SPI buffer is big_endian ("[E][display.mipi_spi:220]: Unsupported color depth or bit order"),
  // while MipiDsi::draw_pixels_at bypasses the software draw buffer (this->buffer_) entirely.
  // When active_frame_buf16() is bound, write directly into the software buffer with automatic
  // hardware endianness/RGB565 handling; otherwise fall back to draw_pixel_at/horizontal_line.
  uint16_t *fb = active_frame_buf16();
  if (fb != nullptr) {
    uint16_t *dst = fb + y * SCREEN_W + start_x;
    const int mode = active_frame_endian_mode();
    if (!skip_black) {
      if (mode == 1) {
        std::memcpy(dst, row_buf, static_cast<size_t>(span_w) * sizeof(uint16_t));
        return;
      }
      if (mode == 2) {
        for (int i = 0; i < span_w; i++) {
          dst[i] = __builtin_bswap16(row_buf[i]);
        }
        return;
      }
    }
    for (int i = 0; i < span_w; i++) {
      const uint16_t p = row_buf[i];
      if (skip_black && (p & 0xFFDFU) == 0U) continue;
      dst[i] = encode_hw_rgb565_raw(p);
    }
    return;
  }

  int i = 0;
  while (i < span_w) {
    const uint16_t p = row_buf[i];
    int run = 1;
    while (i + run < span_w && row_buf[i + run] == p) {
      run++;
    }
    if (skip_black && (p & 0xFFDFU) == 0U) {
      i += run;
      continue;
    }
    const uint8_t r5 = static_cast<uint8_t>((p >> 11) & 0x1FU);
    const uint8_t g6 = static_cast<uint8_t>((p >> 5) & 0x3FU);
    const uint8_t b5 = static_cast<uint8_t>(p & 0x1FU);
    const esphome::Color c(static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
                           static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
                           static_cast<uint8_t>((b5 << 3) | (b5 >> 2)));
    const int x0 = start_x + i;
    if (run == 1) {
      it.draw_pixel_at(x0, y, c);
    } else if (run == 2) {
      it.draw_pixel_at(x0, y, c);
      it.draw_pixel_at(x0 + 1, y, c);
    } else {
      it.horizontal_line(x0, y, run, c);
    }
    i += run;
  }
}

template<typename DisplayT>
inline bool frame_base(DisplayT &it, esphome::Color c);
inline void frame_base_claim();
inline uint64_t prof_section_begin();
inline void prof_art_end(uint64_t t0);

// Horizontal extent of album art on row y: the circle on round panels; the full row elsewhere
// (album art is never rounded to the panel's corner radius; hidden corners just aren't visible).
inline void art_row_span(int y, int cx, int &start_x, int &end_x) {
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

// Standard mode: the art keeps its own aspect ratio, fitted inside the STD_ART_SIZE square.
struct ArtRect {
  int x0, y0, w, h;
};

inline ArtRect art_standard_rect(int cx, int src_w, int src_h, bool show_metadata, bool apply_scrim) {
  const int box = STD_ART_SIZE;
  const int art_cy = (show_metadata || apply_scrim) ? scale_y(198) : SCREEN_CY;
  int w = box;
  int h = box;
  if (src_w > src_h) {
    h = std::max(1, (box * src_h) / src_w);
  } else if (src_h > src_w) {
    w = std::max(1, (box * src_w) / src_h);
  }
  return {cx - w / 2, art_cy - h / 2, w, h};
}

// Draws album art into the bound framebuffer (row-major, full 16-bit RGB565).
template<typename DisplayT>
inline void draw_album_art(DisplayT &it, const uint16_t *src_data, int src_w, int src_h, int cx,
                           bool is_standard, bool is_letterbox, bool apply_scrim, bool show_metadata,
                           int dim_level) {
  static uint16_t row_buf[SCREEN_W];

  if (is_standard) {
    const ArtRect r = art_standard_rect(cx, src_w, src_h, show_metadata, apply_scrim);
    const int step_x = (src_w << 12) / r.w;
    const int step_y = (src_h << 12) / r.h;
    const int mul_q8 = (dim_level >= 2) ? 64 : (apply_scrim ? 128 : 256);
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
      const uint16_t *src_row = src_data + sy * src_w;
      int curr_x = x_start * step_x;
      for (int i = 0; i < span_w; i++) {
        row_buf[i] = src_row[std::min(src_w - 1, curr_x >> 12)];
        curr_x += step_x;
      }
      dim_rgb565_row_dither(row_buf, span_w, mul_q8, r.x0 + x_start, py);
      blit_rgb565_scanline(it, r.x0 + x_start, py, span_w, row_buf, false);
    }
    // Thin top-lit frame just outside the art, so every art pixel stays visible.
    fast_hline(it, r.x0 - 1, r.y0 - 1, r.w + 2, esphome::Color(88, 102, 124));
    fast_hline(it, r.x0 - 1, r.y0 + r.h, r.w + 2, esphome::Color(38, 45, 58));
    fast_vline(it, r.x0 - 1, r.y0, r.h, esphome::Color(56, 66, 82));
    fast_vline(it, r.x0 + r.w, r.y0, r.h, esphome::Color(56, 66, 82));
    return;
  }

  const int start_y = is_letterbox ? scale_y(62) : 0;
  const int end_y = is_letterbox ? (show_metadata ? scale_y(342) : scale_y(385)) : SCREEN_H;

  if (src_w == SCREEN_W && src_h == SCREEN_H) {
    for (int y = start_y; y < end_y; y++) {
      const int mul_q8 = compute_art_row_scale_q8(y, apply_scrim, show_metadata, dim_level);
      int start_x = 0, end_x = 0;
      art_row_span(y, cx, start_x, end_x);
      const int span_w = end_x - start_x + 1;
      if (span_w <= 0) continue;
      const uint16_t *src_row = src_data + y * SCREEN_W;
      if (mul_q8 >= 256) {
        blit_rgb565_scanline(it, start_x, y, span_w, src_row + start_x, false);
      } else {
        std::memcpy(row_buf, src_row + start_x, static_cast<size_t>(span_w) * sizeof(uint16_t));
        dim_rgb565_row_dither(row_buf, span_w, mul_q8, start_x, y);
        blit_rgb565_scanline(it, start_x, y, span_w, row_buf, false);
      }
    }
  } else {
    // Aspect-preserving center-crop ("cover") mapping so square album art is never stretched on portrait screens
    const int view_h = std::max(1, end_y - start_y);
    const int step_x = (src_w << 12) / SCREEN_W;
    const int step_y = (src_h << 12) / view_h;
    const int step_fp = std::min(step_x, step_y);
    const int vis_src_w_fp = SCREEN_W * step_fp;
    const int vis_src_h_fp = view_h * step_fp;
    const int src_x0_fp = std::max(0, ((src_w << 12) - vis_src_w_fp) / 2);
    const int src_y0_fp = std::max(0, ((src_h << 12) - vis_src_h_fp) / 2);

    static uint16_t sx_lut[SCREEN_W];
    static uint16_t scaled_row[SCREEN_W];
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
        const uint16_t *src_row = src_data + sy * src_w;
        for (int x = start_x; x <= end_x; x++) {
          scaled_row[x] = src_row[sx_lut[x]];
        }
        prev_sy = sy;
      }
      if (mul_q8 >= 256) {
        blit_rgb565_scanline(it, start_x, y, span_w, scaled_row + start_x, false);
      } else {
        // The dither pattern follows the screen row, so each row is dimmed on its own.
        std::memcpy(row_buf + start_x, scaled_row + start_x, static_cast<size_t>(span_w) * sizeof(uint16_t));
        dim_rgb565_row_dither(row_buf + start_x, span_w, mul_q8, start_x, y);
        blit_rgb565_scanline(it, start_x, y, span_w, row_buf + start_x, false);
      }
    }
  }

  if (is_letterbox) {
    int x0_t = 0, x1_t = 0, x0_b = 0, x1_b = 0;
    art_row_span(start_y, cx, x0_t, x1_t);
    art_row_span(end_y - 1, cx, x0_b, x1_b);
    if (x1_t > x0_t) fast_hline(it, x0_t, start_y, x1_t - x0_t + 1, esphome::Color(68, 80, 98));
    if (x1_b > x0_b) fast_hline(it, x0_b, end_y - 1, x1_b - x0_b + 1, esphome::Color(46, 54, 68));
  }
}

// Composed album-art cache. The art only changes with the cover, art mode or overlay state, but
// drawing it converts every pixel (hardware byte order, scaling, scrims) on every frame. The
// finished canvas is kept in PSRAM in hardware byte order (2 entries: now-playing and menu
// backdrop) and frames copy rows. The cover is identified by its buffer, size and a 64-point
// pixel sample, because the image downloader reuses its buffer for the next cover.
struct ArtCanvasCache {
  uint16_t *px;
  const void *data;
  int16_t w, h;
  uint32_t sample;
  uint32_t mode;
  uint32_t last_use;
  int16_t row_x0[SCREEN_H];
  int16_t row_len[SCREEN_H];
};

static constexpr int kArtCacheEntries = 2;

inline ArtCanvasCache *art_canvas_entries() {
  static ArtCanvasCache *entries = static_cast<ArtCanvasCache *>(
      heap_caps_calloc(kArtCacheEntries, sizeof(ArtCanvasCache), MALLOC_CAP_SPIRAM));
  return entries;
}

// Frees the composed canvases built from this image buffer (PSRAM back to the heap).
inline void art_canvas_drop(const void *data) {
  ArtCanvasCache *entries = art_canvas_entries();
  if (entries == nullptr || data == nullptr) return;
  for (int i = 0; i < kArtCacheEntries; i++) {
    ArtCanvasCache &e = entries[i];
    if (e.data != data) continue;
    heap_caps_free(e.px);
    e.px = nullptr;
    e.data = nullptr;
    e.last_use = 0;
  }
}

// The one way to release a downloaded image: drops its composed canvas, then frees its pixels.
template<typename ImageT>
inline void release_art(ImageT *img) {
  if (img == nullptr) return;
  art_canvas_drop(img->get_data_start());
  img->release();
}

inline uint32_t art_sample_hash(const uint16_t *data, int w, int h) {
  uint32_t hash = 2166136261U ^ (static_cast<uint32_t>(w) * 31U) ^ (static_cast<uint32_t>(h) << 16);
  const uint32_t n = static_cast<uint32_t>(w) * static_cast<uint32_t>(h);
  for (uint32_t i = 0; i < 64; i++) {
    const uint32_t idx = static_cast<uint32_t>((static_cast<uint64_t>(i) * (n - 1)) / 63U);
    hash = (hash ^ data[idx]) * 16777619U;
  }
  return hash;
}

// Rows and columns a draw_album_art call covers (Standard includes its outside frame).
inline void art_canvas_spans(ArtCanvasCache &e, int cx, int src_w, int src_h, bool is_standard,
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
  const int start_y = is_letterbox ? scale_y(62) : 0;
  const int end_y = is_letterbox ? (show_metadata ? scale_y(342) : scale_y(385)) : SCREEN_H;
  for (int y = std::max(0, start_y); y < std::min(SCREEN_H, end_y); y++) {
    int start_x = 0, end_x = 0;
    art_row_span(y, cx, start_x, end_x);
    e.row_x0[y] = static_cast<int16_t>(start_x);
    e.row_len[y] = static_cast<int16_t>(std::max(0, end_x - start_x + 1));
  }
}

template<typename DisplayT, typename ImageT>
inline bool render_album_art_canvas(DisplayT &it, ImageT *target_art, int cx,
                                    bool is_standard, bool is_letterbox,
                                    bool apply_scrim, bool show_metadata,
                                    int dim_level = 0) {
  if (target_art == nullptr) return false;
  const uint16_t *src_data = reinterpret_cast<const uint16_t *>(target_art->get_data_start());
  const int src_w = target_art->get_width();
  const int src_h = target_art->get_height();
  if (src_data == nullptr || src_w <= 0 || src_h <= 0) return false;

  // Frame base: full-screen art on non-round panels covers every pixel, so it is the base; other
  // modes and round panels paint the black base first.
#if LATTER_ROUND_DISPLAY
  frame_base(it, esphome::Color(0, 0, 0));
#else
  if (!is_standard && !is_letterbox) {
    frame_base_claim();
  } else {
    frame_base(it, esphome::Color(0, 0, 0));
  }
#endif
  struct ArtTimer {
    uint64_t t0;
    ~ArtTimer() { prof_art_end(t0); }
  } art_timer{prof_section_begin()};

  uint16_t *fb = active_frame_buf16();
  ArtCanvasCache *entries = art_canvas_entries();
  if (fb == nullptr || entries == nullptr) {
    draw_album_art(it, src_data, src_w, src_h, cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
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
    if (e.px != nullptr && e.data == src_data && e.w == src_w && e.h == src_h && e.sample == sample && e.mode == mode) {
      hit = &e;
      break;
    }
    if (e.last_use < victim->last_use) victim = &e;
  }

  if (hit == nullptr) {
    if (victim->px == nullptr) {
      victim->px = static_cast<uint16_t *>(
          heap_caps_malloc(sizeof(uint16_t) * SCREEN_W * SCREEN_H, MALLOC_CAP_SPIRAM));
    }
    if (victim->px == nullptr) {
      draw_album_art(it, src_data, src_w, src_h, cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
      return true;
    }
    // Compose into the cache by binding it as the framebuffer (same size, stride and encoding).
    active_frame_buf16() = victim->px;
    draw_album_art(it, src_data, src_w, src_h, cx, is_standard, is_letterbox, apply_scrim, show_metadata, dim_level);
    active_frame_buf16() = fb;
    art_canvas_spans(*victim, cx, src_w, src_h, is_standard, is_letterbox, apply_scrim, show_metadata);
    victim->data = src_data;
    victim->w = static_cast<int16_t>(src_w);
    victim->h = static_cast<int16_t>(src_h);
    victim->sample = sample;
    victim->mode = mode;
    hit = victim;
  }
  hit->last_use = use_seq;

  for (int y = 0; y < SCREEN_H; y++) {
    const int len = hit->row_len[y];
    if (len <= 0) continue;
    const size_t off = static_cast<size_t>(y) * SCREEN_W + hit->row_x0[y];
    std::memcpy(fb + off, hit->px + off, static_cast<size_t>(len) * sizeof(uint16_t));
  }
  return true;
}

// ------------------------------------------------------------------------------
// 3. Text, Time, SemVer & Synodic Astronomy Helpers
// ------------------------------------------------------------------------------
// UTF-8 sanitizer & normalizer for Roboto / Orbitron embedded bitmap fonts.
// Preserves ASCII (0x20..0x7E), full Latin-1 Supplement (U+00A1..U+00FF, e.g. ¡, ¿, ñ, é, ü, ß),
// Latin Extended-A (U+0100..U+017F), and core typographic symbols (•—–…‘’“”€™),
// while normalizing exotic Unicode spaces/dashes/quotes and stripping unsupported emojis
// or malformed UTF-8 sequences so ESPHome's font renderer never logs missing-glyph errors.
inline void append_utf8_cp(std::string &out, uint32_t cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

inline bool is_supported_font_cp(uint32_t cp) {
  if (cp >= 0x20 && cp <= 0x7E) return true;
  if (cp >= 0xA1 && cp <= 0xFF && cp != 0xAD) return true;  // Full Latin-1 Supplement (¡, ¿, À..ÿ)
  switch (cp) {
    // Supported Latin Extended-A & B (European languages in GF_Latin_Core)
    case 0x0100: case 0x0101: case 0x0102: case 0x0103: case 0x0104: case 0x0105:
    case 0x0106: case 0x0107: case 0x010C: case 0x010D: case 0x010E: case 0x010F:
    case 0x0110: case 0x0111: case 0x0112: case 0x0113: case 0x0116: case 0x0117:
    case 0x0118: case 0x0119: case 0x011A: case 0x011B: case 0x011E: case 0x011F:
    case 0x012A: case 0x012B: case 0x012E: case 0x012F: case 0x0130: case 0x0131:
    case 0x0136: case 0x0137: case 0x0139: case 0x013A: case 0x013B: case 0x013C:
    case 0x013D: case 0x013E: case 0x0141: case 0x0142: case 0x0143: case 0x0144:
    case 0x0145: case 0x0146: case 0x0147: case 0x0148: case 0x014C: case 0x014D:
    case 0x0150: case 0x0151: case 0x0152: case 0x0153: case 0x0154: case 0x0155:
    case 0x0158: case 0x0159: case 0x015A: case 0x015B: case 0x015E: case 0x015F:
    case 0x0160: case 0x0161: case 0x0162: case 0x0163: case 0x0164: case 0x0165:
    case 0x016A: case 0x016B: case 0x016E: case 0x016F: case 0x0170: case 0x0171:
    case 0x0172: case 0x0173: case 0x0178: case 0x0179: case 0x017A: case 0x017B:
    case 0x017C: case 0x017D: case 0x017E:
    // Typographic punctuation & symbols compiled into font_micro / font_small / font_date / font_btn_bold
    case 0x2013: case 0x2014: case 0x2018: case 0x2019: case 0x201C: case 0x201D:
    case 0x2022: case 0x2026: case 0x20AC: case 0x2122:
      return true;
    default:
      return false;
  }
}

inline std::string sanitize_utf8_for_font(const std::string &str) {
  if (str.empty()) return str;
  std::string out;
  out.reserve(str.size());
  const size_t n = str.size();
  size_t i = 0;
  while (i < n) {
    unsigned char b0 = static_cast<unsigned char>(str[i]);
    if (b0 < 0x80) {
      if (b0 >= 0x20 && b0 <= 0x7E) out.push_back(static_cast<char>(b0));
      i++;
      continue;
    }
    uint32_t cp = 0;
    size_t len = 0;
    if ((b0 & 0xE0) == 0xC0 && i + 1 < n) {
      unsigned char b1 = static_cast<unsigned char>(str[i + 1]);
      if ((b1 & 0xC0) == 0x80) {
        cp = (static_cast<uint32_t>(b0 & 0x1F) << 6) | static_cast<uint32_t>(b1 & 0x3F);
        if (cp >= 0x80) len = 2;
      }
    } else if ((b0 & 0xF0) == 0xE0 && i + 2 < n) {
      unsigned char b1 = static_cast<unsigned char>(str[i + 1]);
      unsigned char b2 = static_cast<unsigned char>(str[i + 2]);
      if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
        cp = (static_cast<uint32_t>(b0 & 0x0F) << 12) | (static_cast<uint32_t>(b1 & 0x3F) << 6) | static_cast<uint32_t>(b2 & 0x3F);
        if (cp >= 0x800) len = 3;
      }
    } else if ((b0 & 0xF8) == 0xF0 && i + 3 < n) {
      unsigned char b1 = static_cast<unsigned char>(str[i + 1]);
      unsigned char b2 = static_cast<unsigned char>(str[i + 2]);
      unsigned char b3 = static_cast<unsigned char>(str[i + 3]);
      if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
        cp = (static_cast<uint32_t>(b0 & 0x07) << 18) | (static_cast<uint32_t>(b1 & 0x3F) << 12) |
             (static_cast<uint32_t>(b2 & 0x3F) << 6) | static_cast<uint32_t>(b3 & 0x3F);
        if (cp >= 0x10000) len = 4;
      }
    }
    if (len == 0) {
      i++;
      continue;
    }
    i += len;

    // Normalize common Unicode whitespace, dashes, quotes, and zero-width characters
    if (cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x205F || cp == 0x3000) {
      out.push_back(' ');
      continue;
    }
    if (cp == 0x00AD || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
        (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF) {
      continue;
    }
    if (cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2212 || cp == 0xFF0D) {
      out.push_back('-');
      continue;
    }
    if (cp == 0x2015) {
      append_utf8_cp(out, 0x2014);  // Horizontal bar -> em-dash
      continue;
    }
    if (cp == 0x02B9 || cp == 0x02BB || cp == 0x02BC || cp == 0x201A || cp == 0x2032 || cp == 0x2039 || cp == 0x203A || cp == 0xFF07) {
      out.push_back('\'');
      continue;
    }
    if (cp == 0x201E || cp == 0x2033 || cp == 0xFF02) {
      out.push_back('"');
      continue;
    }
    if (cp == 0xFF01) { out.push_back('!'); continue; }
    if (cp == 0xFF1F) { out.push_back('?'); continue; }

    if (is_supported_font_cp(cp)) {
      append_utf8_cp(out, cp);
    }
  }
  return out;
}

template<typename FontT>
inline std::string fit_text_to_width_font(const std::string &raw_str, int max_w, FontT *font) {
  if (raw_str.empty() || font == nullptr) return "";

  // 16-Entry Memoization Ring Cache: turns repeated per-frame UTF-8 sanitization + font->measure()
  // calls into O(1) cache hits across 20 FPS ambient/UI redraws.
  struct FitCacheEntry {
    const void *font_ptr = nullptr;
    int max_w = 0;
    size_t raw_len = 0;
    uint32_t fnv_hash = 0;
    std::string raw_key;
    std::string fitted;
  };
  static FitCacheEntry cache[16];
  static uint8_t next_slot = 0;

  uint32_t h = 2166136261U;
  for (unsigned char ch : raw_str) {
    h = (h ^ ch) * 16777619U;
  }
  const void *fptr = static_cast<const void *>(font);
  for (int i = 0; i < 16; i++) {
    const auto &e = cache[i];
    if (e.font_ptr == fptr && e.max_w == max_w && e.raw_len == raw_str.size() &&
        e.fnv_hash == h && e.raw_key == raw_str) {
      return e.fitted;
    }
  }

  std::string str = sanitize_utf8_for_font(raw_str);
  if (str.empty()) return "";
  int w = 0, x_off = 0, base = 0, text_h = 0;
  font->measure(str.c_str(), &w, &x_off, &base, &text_h);
  std::string result;
  if (w <= max_w) {
    result = str;
  } else {
    std::string cur = str;
    if (cur.length() > 32) {
      while (cur.length() > 30) {
        while (!cur.empty() && (static_cast<unsigned char>(cur.back()) & 0xC0) == 0x80) cur.pop_back();
        if (!cur.empty()) cur.pop_back();
      }
    }
    result = "..";
    while (!cur.empty()) {
      std::string test_str = cur + "..";
      font->measure(test_str.c_str(), &w, &x_off, &base, &text_h);
      if (w <= max_w) {
        result = test_str;
        break;
      }
      while (!cur.empty() && (static_cast<unsigned char>(cur.back()) & 0xC0) == 0x80) cur.pop_back();
      if (!cur.empty()) cur.pop_back();
    }
  }

  auto &slot = cache[next_slot & 15U];
  next_slot = static_cast<uint8_t>((next_slot + 1U) & 15U);
  slot.font_ptr = fptr;
  slot.max_w = max_w;
  slot.raw_len = raw_str.size();
  slot.fnv_hash = h;
  slot.raw_key = raw_str;
  slot.fitted = result;
  return result;
}

inline bool is_newer_semver(const std::string &lat_str, const std::string &cur_str) {
  if (lat_str.empty() || lat_str == "unavailable" || lat_str == "unknown") return false;
  std::string l = lat_str;
  if (!l.empty() && (l[0] == 'v' || l[0] == 'V')) l = l.substr(1);
  std::string c = cur_str;
  if (!c.empty() && (c[0] == 'v' || c[0] == 'V')) c = c.substr(1);
  if (l.rfind("202", 0) == 0) return false;
  int l_maj = 0, l_min = 0, l_pat = 0;
  int c_maj = 0, c_min = 0, c_pat = 0;
  if (sscanf(l.c_str(), "%d.%d.%d", &l_maj, &l_min, &l_pat) < 2) return false;
  if (sscanf(c.c_str(), "%d.%d.%d", &c_maj, &c_min, &c_pat) < 2) return false;
  if (l_maj > c_maj) return true;
  if (l_maj < c_maj) return false;
  if (l_min > c_min) return true;
  if (l_min < c_min) return false;
  return l_pat > c_pat;
}

inline bool is_newer_semver(const std::string &lat_str, const char *cur_str) {
  return is_newer_semver(lat_str, std::string(cur_str != nullptr ? cur_str : ""));
}

inline void format_time_hhmm(int hour, int minute, bool is_24h, char *out_buf,
                             size_t out_sz, char *ap_buf = nullptr, bool pad_zero = false) {
  if (is_24h) {
    snprintf(out_buf, out_sz, "%02d:%02d", hour, minute);
    if (ap_buf) ap_buf[0] = '\0';
  } else {
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    if (pad_zero) {
      snprintf(out_buf, out_sz, "%02d:%02d", h12, minute);
    } else {
      snprintf(out_buf, out_sz, "%d:%02d", h12, minute);
    }
    if (ap_buf) {
      strcpy(ap_buf, (hour >= 12) ? "PM" : "AM");
    }
  }
}

// Returns false when neither the clock nor Home Assistant's moon phase is known (outputs untouched then).
inline bool compute_synodic_moon_phase(bool time_valid, int64_t epoch_sec, const std::string &fallback_phase,
                                       double &out_cycle, int *out_illum = nullptr, std::string *out_label = nullptr) {
  double cycle = 0.5;
  int illum_pct = 50;
  std::string mp_label;
  bool known = true;

  if (time_valid) {
    double diff = (double)(epoch_sec - 947182440LL);
    cycle = fmod(diff, 2551442.86) / 2551442.86;
    if (cycle < 0.0) cycle += 1.0;
    illum_pct = (int)round((1.0 - cos(2.0 * 3.1415926535 * cycle)) / 2.0 * 100.0);

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

// ------------------------------------------------------------------------------
// 4. Standardized Touch Timing Constants, Hit-Testing & Value Steppers (30 Hz)
// ------------------------------------------------------------------------------
static constexpr uint32_t TOUCH_DEBOUNCE_RAPID   = 100; // ms: Rapid steppers
static constexpr uint32_t TOUCH_DEBOUNCE_BUTTON  = 150; // ms: Standard buttons & pills
static constexpr uint32_t TOUCH_DEBOUNCE_WAKE    = 180; // ms: Post-wake settling lockout
static constexpr uint32_t TOUCH_DEBOUNCE_DISMISS = 200; // ms: Overlay/HUD dismissal
static constexpr uint32_t TOUCH_DEBOUNCE_SCROLL  = 250; // ms: Page transitions & chevrons
static constexpr uint32_t TOUCH_GUARD_DISMISS    = 250; // ms: Minimum post-wake delay
static constexpr uint32_t TOUCH_POST_SWIPE_LOCKOUT = 200; // ms: Ignore moves right after a committed swipe

inline uint32_t &last_screen_wake_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline bool touch_allow_tap(uint32_t now_ms, uint32_t cooldown_ms = TOUCH_DEBOUNCE_BUTTON) {
  static uint32_t s_last_tap_ms = 0;
  if (now_ms - s_last_tap_ms <= cooldown_ms) return false;
  s_last_tap_ms = now_ms;
  return true;
}

template<typename TouchT>
inline bool is_back_button(const TouchT &t) {
#if LATTER_ROUND_DISPLAY
  return (t.y >= 404 && t.y <= 466 && t.x >= 116 && t.x <= 350);
#else
  const int bh = scale_y(64);
  const int bw = scale_x(120);
  return (t.y >= (SCREEN_H - bh) && t.y <= SCREEN_H && t.x >= (SCREEN_CX - bw) && t.x <= (SCREEN_CX + bw));
#endif
}

template<typename TouchT>
inline bool is_right_chevron_zone(const TouchT &t) {
#if LATTER_ROUND_DISPLAY
  return (t.x >= 376 && t.y >= 70 && t.y <= 395);
#else
  const int chev_min_x = (SCREEN_W < 380) ? (SCREEN_W - 36) : ((SCREEN_W > 520) ? (SCREEN_W - 96) : (SCREEN_W - 48));
  const int margin_y = scale_y(70);
  return (t.x >= chev_min_x && t.y >= margin_y && t.y <= (SCREEN_H - margin_y));
#endif
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

template<typename ValT, typename DeltaT, typename MinT, typename MaxT>
inline auto step_wrap_range(ValT val, DeltaT delta, MinT min_val, MaxT max_val) {
  auto next = val + delta;
  if (next > max_val + 0.001f) return static_cast<decltype(next)>(min_val);
  if (next < min_val - 0.001f) return static_cast<decltype(next)>(max_val);
  return next;
}

template<typename CurT>
inline const char *cycle_option(const CurT &cur, std::initializer_list<const char *> opts) {
  for (auto it = opts.begin(); it != opts.end(); ++it) {
    if (cur == *it) {
      ++it;
      return (it != opts.end()) ? *it : *opts.begin();
    }
  }
  return (opts.size() > 0) ? *opts.begin() : "";
}


// Schedules an immediate (0ms) display update on the Display component itself so that
// touchscreen, interval, and api callbacks return in <1ms and the QSPI frame render is
// attributed to Display (140ms budget) instead of a 50ms non-display component.
inline uint32_t &last_disp_update_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline bool &screen_is_off_standby() {
  static bool off = false;
  return off;
}

inline bool &disp_poller_held() {
  static bool held = false;
  return held;
}

struct DisplayUpdateScheduler : public esphome::Component {
  // Numeric scheduler ids (ESPHome 2026.11+). Matched per component and timer type; high values keep clear
  // of the small ids the display component's own code uses.
  static constexpr uint32_t kTimeoutAsyncDraw = 0x4C410001U;
  static constexpr uint32_t kTimeoutFbClear = 0x4C410002U;
  static constexpr uint32_t kTimeoutPanelDcs = 0x4C410003U;

  // One-shot on the display component after delay_ms (replaces a pending one with the same id), so the
  // work is attributed to Display and never blocks the caller (e.g. the AMOLED DCS wake in ui_layout.h).
  static void request_timeout(esphome::Component *comp, uint32_t timeout_id, uint32_t delay_ms,
                              std::function<void()> &&fn) {
    if (comp == nullptr) return;
    static_cast<DisplayUpdateScheduler *>(comp)->set_timeout(timeout_id, delay_ms, std::move(fn));
  }

  // Post-frame hook: a 0 ms timeout runs after the current update() (and its panel transfer) returns.
  static void request_post_frame(esphome::Component *comp, void (*fn)()) {
    if (comp == nullptr || fn == nullptr) return;
    static_cast<DisplayUpdateScheduler *>(comp)->set_timeout(kTimeoutFbClear, 0, [fn]() { fn(); });
  }

  template<typename DispT>
  static void request_ptr(DispT *disp) {
    if (disp != nullptr && !screen_is_off_standby() && !disp_poller_held()) {
      auto *comp = static_cast<esphome::Component *>(disp);
      static_cast<DisplayUpdateScheduler *>(comp)->set_timeout(
          kTimeoutAsyncDraw, 0, [disp]() {
            if (screen_is_off_standby() || disp_poller_held()) return;
            disp->update();
            fb_clear_after_update();
          });
    }
  }

  template<typename DispT>
  static void request_anim_ptr(DispT *disp) {
    if (disp != nullptr && !screen_is_off_standby() && !disp_poller_held()) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
      constexpr uint32_t kFrameMs = 18U;
#else
      constexpr uint32_t kFrameMs = 26U;
#endif
      uint32_t now_m = esphome::millis();
      const bool inside_render = (last_disp_update_ms() > last_disp_update_end_ms());
      uint32_t elapsed = now_m - last_disp_update_ms();
      uint32_t delay_ms = (elapsed < kFrameMs) ? (kFrameMs - elapsed) : (inside_render ? 4U : 0U);
      auto *comp = static_cast<esphome::Component *>(disp);
      static_cast<DisplayUpdateScheduler *>(comp)->set_timeout(
          kTimeoutAsyncDraw, delay_ms, [disp]() {
            if (screen_is_off_standby() || disp_poller_held()) return;
            disp->update();
            fb_clear_after_update();
          });
    }
  }
};

template<typename DispT>
inline void schedule_disp_update(DispT *disp) {
  DisplayUpdateScheduler::request_ptr(disp);
}

template<typename DispT>
inline void schedule_disp_update(DispT *const *disp_ptr) {
  if (disp_ptr != nullptr) DisplayUpdateScheduler::request_ptr(*disp_ptr);
}

template<typename DispT>
inline void schedule_anim_disp_update(DispT *disp) {
  DisplayUpdateScheduler::request_anim_ptr(disp);
}

template<typename DispT>
inline void schedule_anim_disp_update(DispT *const *disp_ptr) {
  if (disp_ptr != nullptr) DisplayUpdateScheduler::request_anim_ptr(*disp_ptr);
}

inline uint32_t &last_local_vol_tap_ms() {
  static uint32_t ts = 0;
  return ts;
}

// ------------------------------------------------------------------------------
// 5. On-Demand Flight Recorder & Telemetry Profiler ([PROF:*])
// ------------------------------------------------------------------------------
struct FlightRecorderStats {
  bool enabled = false;
  uint32_t window_frames = 0;
  uint32_t window_render_us = 0;
  uint32_t max_render_us = 0;
  int max_render_page = 0;
  int max_render_sub = 0;
  uint32_t slow_frames = 0;
  uint32_t last_frame_ms = 0;
  uint32_t last_summary_ms = 0;
  int last_logged_page = -1;
  int last_logged_sub = -1;
  uint32_t va_session_start_ms = 0;
};

inline FlightRecorderStats &flight_recorder() {
  static FlightRecorderStats st;
  return st;
}

inline bool &profiler_enabled() {
  return flight_recorder().enabled;
}

inline uint64_t prof_now_us() {
#ifdef USE_ESP32
  return static_cast<uint64_t>(esp_timer_get_time());
#else
  return static_cast<uint64_t>(esphome::micros());
#endif
}

// Per-section render timing. A ProfLap attributes the time since construction (or the previous
// mark) to a section; totals are averaged per frame and logged as [PROF:SECT] with each
// [PROF:FRAME_SUM] window. Costs nothing while the Telemetry Profiler is off.
enum class ProfSect : uint8_t { BG_SKY, BG_CELESTIAL, BG_CLOUDS, BG_PRECIP, HOME_TEXT, HOME_DOCK, BASE, ART, COUNT };

inline uint32_t *prof_sect_us() {
  static uint32_t acc[static_cast<size_t>(ProfSect::COUNT)] = {};
  return acc;
}

inline uint64_t prof_section_begin() { return profiler_enabled() ? prof_now_us() : 0; }
inline void prof_art_end(uint64_t t0) {
  if (t0 != 0) prof_sect_us()[static_cast<size_t>(ProfSect::ART)] += static_cast<uint32_t>(prof_now_us() - t0);
}

// Frame base. The universal GUI disables ESPHome's auto-clear, so every frame paints its background
// exactly once through frame_base(): pages pass black, and Home's daytime weather sky on rectangular
// panels passes the sky color, which saves a full-screen framebuffer write (~10 ms on the S3 boards,
// where PSRAM writes are ~45 ns per pixel). frame_base_begin() runs at the top of the display lambda;
// frame_base_skip() marks frames that intentionally draw nothing (screen off). A frame that ends
// without either is logged once by FrameProfileScope (it would show the previous frame's pixels).
struct FrameBaseState {
  bool painted = false;
  bool warned = false;
  bool has_color = false;    // this frame's base was a color fill (not claimed, not skipped)
  uint16_t color_hw = 0;     // that color, hardware-encoded
};

inline FrameBaseState &frame_base_state() {
  static FrameBaseState st;
  return st;
}

inline void frame_base_begin() {
  frame_base_state().painted = false;
  frame_base_state().has_color = false;
}
inline void frame_base_skip() { frame_base_state().painted = true; }

// Between-frames DMA clear (ESP32-S3 / ESP32-P4). ESPHome's mipi_spi and mipi_dsi drivers finish
// sending a frame before update() returns, so the framebuffer is idle until the next frame. Right
// after a frame, hardware fills it with the color that frame used as its base while the CPU is free:
//  - ESP32-P4 with a little-endian RGB565 framebuffer: the PPA's solid fill (2D-DMA). Required on
//    boards with CONFIG_GDMA_ISR_IRAM_SAFE, where IDF's async memcpy cannot install (its internal
//    GDMA callback is not in IRAM). The aligned interior is described to the PPA as a 128 px wide
//    picture, so ESPHome's (unaligned) buffer allocation does not matter.
//  - Otherwise: a GDMA memory-to-memory copy (esp_async_memcpy) from a 128 KB PSRAM pattern.
// The next frame waits for completion at its start and, if its base is the same color, skips its
// CPU fill. Cache rules: the cleared range is written back + invalidated before the DMA starts (no
// dirty or cached lines can overwrite or shadow the fill) and invalidated again after it completes.
// Only the aligned interior is DMA-filled; the CPU writes the short head and tail. Any failure or
// color mismatch falls back to the CPU fill, so output is always identical.
struct FbClearState {
  esphome::Component *owner = nullptr;
  uint16_t *fb = nullptr;
  bool init_tried = false;
  bool ok = false;
  bool pending_start = false;  // a post-frame start is scheduled
  bool active = false;         // DMA issued for the current fb contents
  bool ready = false;          // completed and cache-invalidated, waiting to be consumed
  bool error = false;
  uint16_t color_hw = 0;
  uint16_t pattern_hw = 0;
  bool pattern_valid = false;
  uint8_t *interior = nullptr;
  size_t interior_len = 0;
  uint32_t hits = 0;
  uint32_t misses = 0;
  uint32_t starts = 0;  // clears issued since boot (heap check-ins at 1 and 256)
  const char *why = nullptr;  // last reason a clear could not run (shown in [PROF:FRAME_SUM])
  int why_err = 0;
#if LATTER_FB_DMA_CLEAR
  async_memcpy_handle_t mcp = nullptr;
  uint16_t *pattern = nullptr;
  size_t align = 64;
#endif
#if LATTER_FB_PPA
  ppa_client_handle_t ppa = nullptr;
  bool use_ppa = false;
  bool ppa_checked = false;  // first PPA fill read back and compared
  int64_t ppa_t0_us = 0;
#endif
};

inline FbClearState &fb_clear_state() {
  static FbClearState st;
  return st;
}

// Records why the DMA clear could not run; [PROF:FRAME_SUM] reports it until a clear succeeds.
inline void fb_clear_fail(const char *why, int err = 0) {
  auto &st = fb_clear_state();
  if (st.why != why || st.why_err != err) {
    ESP_LOGW("disp_fb", "DMA clear skipped: %s (err=%d)", why, err);
  }
  st.why = why;
  st.why_err = err;
}

inline const char *fb_clear_why_err_str() {
  static char buf[16];
  const int err = fb_clear_state().why_err;
  if (err == 0) return "";
  snprintf(buf, sizeof(buf), "(%d)", err);
  return buf;
}

#if LATTER_FB_DMA_CLEAR
// 128 KB keeps a frame to a few transfers (Mobile 4, Desk 8). The internal-RAM GDMA descriptors
// scale with bytes, not with the transfer count, but each pooled transaction slot can hold its
// descriptor lists, so the backlog is sized to one frame's transfers rather than padded.
static constexpr size_t kFbClearPatternBytes = 128 * 1024;
static constexpr size_t kFbClearFrameBytes = static_cast<size_t>(SCREEN_W) * SCREEN_H * sizeof(uint16_t);
static constexpr size_t kFbClearChunks = (kFbClearFrameBytes + kFbClearPatternBytes - 1) / kFbClearPatternBytes;

inline void fb_clear_log_heap(const char *when) {
  ESP_LOGI("disp_fb", "DMA clear heap %s: int_free=%uB int_max_blk=%uB", when,
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

// Outstanding DMA chunks. A plain global (DRAM): the completion ISR may run with the flash cache
// disabled on boards with IRAM-safe GDMA ISRs, so it must not call into flash.
inline std::atomic<int> g_fb_clear_pending{0};

static IRAM_ATTR bool fb_clear_chunk_done(async_memcpy_handle_t, async_memcpy_event_t *, void *) {
  g_fb_clear_pending.fetch_sub(1, std::memory_order_acq_rel);
  return false;
}

#if LATTER_FB_PPA
// PPA fill geometry: 128 px (256 B) rows, a multiple of every L1/L2 cache line.
static constexpr uint32_t kFbPpaRowPx = 128;
static constexpr size_t kFbPpaRowBytes = kFbPpaRowPx * sizeof(uint16_t);
static constexpr size_t kFbPpaMaxRows = 16383;  // 2D-DMA descriptor height field
inline int64_t g_fb_clear_done_us = 0;          // completion time of the last PPA fill

static IRAM_ATTR bool fb_clear_ppa_done(ppa_client_handle_t, ppa_event_data_t *, void *) {
  g_fb_clear_done_us = esp_timer_get_time();
  g_fb_clear_pending.fetch_sub(1, std::memory_order_acq_rel);
  return false;
}
#endif

inline bool fb_clear_init() {
  auto &st = fb_clear_state();
  if (st.init_tried) return st.ok;
  st.init_tried = true;
#if LATTER_FB_PPA
  // PPA fill needs a little-endian RGB565/BGR565 framebuffer (it writes 565 words as-is).
  const int endian = active_frame_endian_mode();
  if (endian == 1 || endian == 3) {
    ppa_client_config_t pcfg = {};
    pcfg.oper_type = PPA_OPERATION_FILL;
    pcfg.max_pending_trans_num = 1;
    esp_err_t perr = ppa_register_client(&pcfg, &st.ppa);
    if (perr == ESP_OK) {
      ppa_event_callbacks_t cbs = {};
      cbs.on_trans_done = fb_clear_ppa_done;
      perr = ppa_client_register_event_callbacks(st.ppa, &cbs);
    }
    if (perr == ESP_OK) {
      st.use_ppa = true;
      st.align = kFbPpaRowBytes;
      st.ok = true;
      ESP_LOGI("disp_fb", "Between-frames clear enabled: PPA fill (%u px rows)", static_cast<unsigned>(kFbPpaRowPx));
      return true;
    }
    fb_clear_fail("ppa_register", static_cast<int>(perr));
  }
#endif
  fb_clear_log_heap("before install");
  async_memcpy_config_t cfg = ASYNC_MEMCPY_DEFAULT_CONFIG();
  cfg.backlog = kFbClearChunks + 1;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  esp_err_t err = esp_async_memcpy_install_gdma_axi(&cfg, &st.mcp);
#else
  esp_err_t err = esp_async_memcpy_install_gdma_ahb(&cfg, &st.mcp);
#endif
  // Cache-line alignment for esp_cache_msync on PSRAM: 64 B on every Latter board (S3 boards set
  // CONFIG_ESP32S3_DATA_CACHE_LINE_64B; P4 boards never enable the 128 B L2 line). A larger line
  // makes esp_cache_msync reject the range, and fb_clear_start falls back to the CPU fill.
#if defined(CONFIG_CACHE_L2_CACHE_LINE_128B)
  st.align = 128;
#else
  st.align = 64;
#endif
  if (err == ESP_OK) {
    st.pattern = static_cast<uint16_t *>(heap_caps_aligned_alloc(st.align, kFbClearPatternBytes, MALLOC_CAP_SPIRAM));
  }
  st.ok = (err == ESP_OK && st.pattern != nullptr);
  if (err != ESP_OK) {
    fb_clear_fail("install", static_cast<int>(err));
  } else if (st.pattern == nullptr) {
    fb_clear_fail("pattern_alloc");
  }
  ESP_LOGI("disp_fb", "Between-frames DMA clear %s (err=%d, align=%u, chunks=%u)", st.ok ? "enabled" : "unavailable",
           static_cast<int>(err), static_cast<unsigned>(st.align), static_cast<unsigned>(kFbClearChunks));
  fb_clear_log_heap("after install");
  return st.ok;
}
#endif

inline void fb_clear_bind(esphome::Component *owner, uint16_t *fb) {
  auto &st = fb_clear_state();
  st.owner = owner;
  st.fb = fb;
}

// Runs after update() returned (post-frame timeout): starts the DMA clear for the next frame.
inline void fb_clear_start() {
#if LATTER_FB_DMA_CLEAR
  auto &st = fb_clear_state();
  st.pending_start = false;
  if (st.active) return;
  if (st.fb == nullptr) {
    fb_clear_fail("no_fb");
    return;
  }
  if (!fb_clear_init()) return;
  const uintptr_t base = reinterpret_cast<uintptr_t>(st.fb);
  const size_t bytes = static_cast<size_t>(SCREEN_W) * SCREEN_H * sizeof(uint16_t);
  const uintptr_t lo = (base + st.align - 1) & ~(static_cast<uintptr_t>(st.align) - 1);
  const uintptr_t hi = (base + bytes) & ~(static_cast<uintptr_t>(st.align) - 1);
  if (hi <= lo) {
    fb_clear_fail("range");
    return;
  }
  st.interior = reinterpret_cast<uint8_t *>(lo);
  st.interior_len = hi - lo;
  st.color_hw = frame_base_state().color_hw;
#if LATTER_FB_PPA
  if (st.use_ppa) {
    const size_t rows = st.interior_len / kFbPpaRowBytes;
    if (rows > kFbPpaMaxRows) {
      fb_clear_fail("ppa_rows", static_cast<int>(rows));
      return;
    }
    ppa_fill_oper_config_t cfg = {};
    cfg.out.buffer = st.interior;
    cfg.out.buffer_size = static_cast<uint32_t>(st.interior_len);
    cfg.out.pic_w = kFbPpaRowPx;
    cfg.out.pic_h = static_cast<uint32_t>(rows);
    cfg.out.fill_cm = PPA_FILL_COLOR_MODE_RGB565;
    cfg.fill_block_w = kFbPpaRowPx;
    cfg.fill_block_h = static_cast<uint32_t>(rows);
    // Exact 565 -> 888 expansion so the PPA's 888 -> 565 conversion returns the same word. The word's
    // fields go through unchanged, so a BGR565 framebuffer needs no swap.
    const uint32_t hw = st.color_hw;
    const uint32_t r8 = ((hw >> 11) & 0x1FU) << 3;
    const uint32_t g8 = ((hw >> 5) & 0x3FU) << 2;
    const uint32_t b8 = (hw & 0x1FU) << 3;
    cfg.fill_argb_color.val = 0xFF000000U | (r8 << 16) | (g8 << 8) | b8;
    cfg.mode = PPA_TRANS_MODE_NON_BLOCKING;
    st.error = false;
    st.ready = false;
    st.active = true;
    g_fb_clear_pending.store(1, std::memory_order_release);
    st.ppa_t0_us = esp_timer_get_time();
    // ppa_do_fill writes back + invalidates the target window itself.
    const esp_err_t ppa_err = ppa_do_fill(st.ppa, &cfg);
    if (ppa_err != ESP_OK) {
      g_fb_clear_pending.store(0, std::memory_order_release);
      st.error = true;
      fb_clear_fail("ppa_fill", static_cast<int>(ppa_err));
    }
    st.starts++;
    return;
  }
#endif
  if (!st.pattern_valid || st.pattern_hw != st.color_hw) {
    for (size_t i = 0; i < kFbClearPatternBytes / sizeof(uint16_t); i++) st.pattern[i] = st.color_hw;
    esp_cache_msync(st.pattern, kFbClearPatternBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    st.pattern_hw = st.color_hw;
    st.pattern_valid = true;
  }
  const esp_err_t sync_err =
      esp_cache_msync(st.interior, st.interior_len, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
  if (sync_err != ESP_OK) {
    fb_clear_fail("msync", static_cast<int>(sync_err));
    return;
  }
  st.error = false;
  st.ready = false;
  st.active = true;
  size_t off = 0;
  while (off < st.interior_len) {
    const size_t n = std::min(kFbClearPatternBytes, st.interior_len - off);
    g_fb_clear_pending.fetch_add(1, std::memory_order_acq_rel);
    const esp_err_t cp_err = esp_async_memcpy(st.mcp, st.interior + off, st.pattern, n, fb_clear_chunk_done, nullptr);
    if (cp_err != ESP_OK) {
      g_fb_clear_pending.fetch_sub(1, std::memory_order_acq_rel);
      st.error = true;
      fb_clear_fail("memcpy", static_cast<int>(cp_err));
      break;
    }
    off += n;
  }
  st.starts++;
  if (st.starts == 1 || st.starts == 256) fb_clear_log_heap(st.starts == 1 ? "after 1st clear" : "after 256 clears");
#endif
}

// Starts a scheduled clear right after an async_draw update() so the next frame always finds one
// (the post-frame timeout can otherwise lose the race with the next async_draw timeout).
inline void fb_clear_after_update() {
  if (fb_clear_state().pending_start) fb_clear_start();
}

// Frame start: no drawing may touch the framebuffer while the DMA runs.
inline void fb_clear_wait() {
#if LATTER_FB_DMA_CLEAR
  auto &st = fb_clear_state();
  if (!st.active || st.ready) return;
  const int64_t t0 = esp_timer_get_time();
  while (g_fb_clear_pending.load(std::memory_order_acquire) > 0) {
    if (esp_timer_get_time() - t0 > 200000) {
      // A transfer that never completes: stop using DMA clears (drawing could race it).
      st.ok = false;
      fb_clear_fail("timeout");
      break;
    }
    esp_rom_delay_us(20);
  }
  esp_cache_msync(st.interior, st.interior_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  st.ready = !st.error && st.ok;
  if (!st.ready) st.active = false;
#if LATTER_FB_PPA
  // First PPA fill: read back 8 pixels across the interior. Any mismatch (byte order, color
  // conversion) turns the PPA clear off for this boot and the CPU fill carries on.
  if (st.use_ppa && st.ready && !st.ppa_checked) {
    st.ppa_checked = true;
    const uint16_t *px = reinterpret_cast<const uint16_t *>(st.interior);
    const size_t n = st.interior_len / sizeof(uint16_t);
    for (size_t i = 0; i < 8; i++) {
      const size_t idx = (i * (n - 1)) / 7;
      if (px[idx] != st.color_hw) {
        ESP_LOGW("disp_fb", "PPA fill read-back mismatch at %u: expected 0x%04X, got 0x%04X; PPA clear off",
                 static_cast<unsigned>(idx), static_cast<unsigned>(st.color_hw), static_cast<unsigned>(px[idx]));
        st.ok = false;
        st.ready = false;
        st.active = false;
        fb_clear_fail("ppa_verify", px[idx]);
        return;
      }
    }
    ESP_LOGI("disp_fb", "PPA fill verified: %u KB in %ld us", static_cast<unsigned>(st.interior_len / 1024),
             static_cast<long>(g_fb_clear_done_us - st.ppa_t0_us));
  }
#endif
#endif
}

// Inside frame_base(): true if the DMA already filled the frame with this color.
inline bool fb_clear_consume(uint16_t hw) {
  auto &st = fb_clear_state();
  if (!st.ready) return false;
  st.ready = false;
  st.active = false;
  if (hw != st.color_hw || st.fb == nullptr) {
    st.misses++;
    return false;
  }
  uint16_t *const fb = st.fb;
  uint16_t *const end = fb + static_cast<size_t>(SCREEN_W) * SCREEN_H;
  for (uint16_t *q = fb; q < reinterpret_cast<uint16_t *>(st.interior); q++) *q = hw;
  for (uint16_t *q = reinterpret_cast<uint16_t *>(st.interior + st.interior_len); q < end; q++) *q = hw;
  st.hits++;
  st.why = nullptr;
  return true;
}

// End of the display lambda: drop an unconsumed clear and schedule the next one.
inline void fb_clear_frame_end() {
#if LATTER_FB_DMA_CLEAR
  auto &st = fb_clear_state();
  if (st.ready) {
    st.ready = false;
    st.active = false;
  }
  if (frame_base_state().has_color && (st.owner == nullptr || st.fb == nullptr)) fb_clear_fail("no_fb");
  if (!frame_base_state().has_color || st.owner == nullptr || st.fb == nullptr || st.pending_start) return;
  if (st.init_tried && !st.ok) return;
  st.pending_start = true;
  // Runs after update() returns, i.e. after the panel transfer (ESPHome's loop is single-threaded).
  DisplayUpdateScheduler::request_post_frame(st.owner, &fb_clear_start);
#endif
}

// Paints the full-screen base if this frame has none yet. Returns true if it painted.
template<typename DisplayT>
inline bool frame_base(DisplayT &it, esphome::Color c) {
  auto &st = frame_base_state();
  if (st.painted) return false;
  st.painted = true;
  st.has_color = true;
  st.color_hw = encode_hw_rgb565(c);
  if (fb_clear_consume(st.color_hw)) return true;
  const uint64_t t0 = profiler_enabled() ? prof_now_us() : 0;
  it.fill(c);
  if (t0 != 0) prof_sect_us()[static_cast<size_t>(ProfSect::BASE)] += static_cast<uint32_t>(prof_now_us() - t0);
  return true;
}

// For a draw that covers every pixel of the frame (full-screen album art on non-round panels):
// that draw is the base, so nothing is filled.
inline void frame_base_claim() { frame_base_state().painted = true; }

struct ProfLap {
  uint64_t t = 0;
  ProfLap() : t(profiler_enabled() ? prof_now_us() : 0) {}
  void mark(ProfSect sect) {
    if (t == 0) return;
    const uint64_t now = prof_now_us();
    prof_sect_us()[static_cast<size_t>(sect)] += static_cast<uint32_t>(now - t);
    t = now;
  }
};

struct FrameProfileScope {
  uint64_t t0_us = 0;
  int page = 0;
  int sub = 0;
  int pwr = 0;

  FrameProfileScope(int p, int s, int pw) : page(p), sub(s), pwr(pw) {
    fb_clear_wait();
    t0_us = prof_now_us();
    uint32_t start_m = esphome::millis();
    last_disp_update_ms() = start_m;
    last_ambient_anim_ms() = start_m;
  }

  ~FrameProfileScope() {
    if (t0_us == 0) return;
    fb_clear_frame_end();
    auto &fbs = frame_base_state();
    if (!fbs.painted && !fbs.warned) {
      fbs.warned = true;
      ESP_LOGW("disp", "Frame on page %d sub %d drew without frame_base(); it shows stale pixels", page, sub);
    }
    uint32_t render_us = static_cast<uint32_t>(prof_now_us() - t0_us);
    uint32_t now_m = esphome::millis();
    last_frame_duration_ms() = render_us / 1000U;
    last_disp_update_end_ms() = now_m;
    if (!profiler_enabled()) return;
    auto &fr = flight_recorder();
    uint32_t dt_ms = (fr.last_frame_ms > 0 && now_m >= fr.last_frame_ms) ? (now_m - fr.last_frame_ms) : 0U;
    fr.last_frame_ms = now_m;
    fr.window_frames++;
    fr.window_render_us += render_us;
    if (render_us > fr.max_render_us) {
      fr.max_render_us = render_us;
      fr.max_render_page = page;
      fr.max_render_sub = sub;
    }
    static constexpr uint32_t kSlowFrameUs = (SCREEN_W * SCREEN_H > 300000) ? 110000U : 65000U;
    const bool is_slow = (render_us >= kSlowFrameUs);
    if (is_slow) fr.slow_frames++;

    const bool page_changed = (page != fr.last_logged_page || sub != fr.last_logged_sub);
    if (page_changed || is_slow) {
      fr.last_logged_page = page;
      fr.last_logged_sub = sub;
      ESP_LOGI("PROF", "[PROF:FRAME] page=%d sub=%d pwr=%d render=%luus dt=%lums%s",
               page, sub, pwr, (unsigned long)render_us, (unsigned long)dt_ms,
               is_slow ? " [SLOW_FRAME]" : "");
    }

    if (fr.last_summary_ms == 0) fr.last_summary_ms = now_m;
    uint32_t win_el = now_m - fr.last_summary_ms;
    if (win_el >= 5000U && fr.window_frames > 0) {
      float fps = (static_cast<float>(fr.window_frames) * 1000.0f) / static_cast<float>(win_el);
      uint32_t avg_us = fr.window_render_us / fr.window_frames;
      ESP_LOGI("PROF", "[PROF:FRAME_SUM] frames=%lu fps=%.1f avg=%luus max=%luus(p%d/%d) slow=%lu dma_clear=%lu/%lu%s%s%s",
               (unsigned long)fr.window_frames, fps, (unsigned long)avg_us,
               (unsigned long)fr.max_render_us, fr.max_render_page, fr.max_render_sub,
               (unsigned long)fr.slow_frames, (unsigned long)fb_clear_state().hits,
               (unsigned long)(fb_clear_state().hits + fb_clear_state().misses),
               fb_clear_state().why != nullptr ? " dma_off=" : "",
               fb_clear_state().why != nullptr ? fb_clear_state().why : "",
               fb_clear_state().why != nullptr ? fb_clear_why_err_str() : "");
      fb_clear_state().hits = 0;
      fb_clear_state().misses = 0;
      uint32_t *sect = prof_sect_us();
      uint32_t any_sect = 0;
      for (size_t i = 0; i < static_cast<size_t>(ProfSect::COUNT); i++) any_sect |= sect[i];
      if (any_sect != 0U) {
        const uint32_t n = fr.window_frames;
        ESP_LOGI("PROF", "[PROF:SECT] avg/frame base=%luus art=%luus sky=%luus celestial=%luus clouds=%luus precip=%luus text=%luus dock=%luus",
                 (unsigned long)(sect[6] / n), (unsigned long)(sect[7] / n),
                 (unsigned long)(sect[0] / n), (unsigned long)(sect[1] / n), (unsigned long)(sect[2] / n),
                 (unsigned long)(sect[3] / n), (unsigned long)(sect[4] / n), (unsigned long)(sect[5] / n));
        for (size_t i = 0; i < static_cast<size_t>(ProfSect::COUNT); i++) sect[i] = 0;
      }
      fr.window_frames = 0;
      fr.window_render_us = 0;
      fr.max_render_us = 0;
      fr.slow_frames = 0;
      fr.last_summary_ms = now_m;
    }
  }
};

inline void log_audio_profile(const char *event_tag, const char *detail = "") {
  if (!profiler_enabled()) return;
  uint32_t now_m = esphome::millis();
  auto &fr = flight_recorder();
  if (std::strcmp(event_tag, "VA_LISTEN_START") == 0) {
    fr.va_session_start_ms = now_m;
  }
  uint32_t va_el = (fr.va_session_start_ms > 0 && now_m >= fr.va_session_start_ms)
                       ? (now_m - fr.va_session_start_ms)
                       : 0U;
  ESP_LOGI("PROF", "[PROF:AUDIO] event=%s va_elapsed=%lums %s",
           event_tag, (unsigned long)va_el, detail ? detail : "");
}

// PMIC telemetry shared by the PMU module and the profiler. gauge_raw is the AXP2101 fuel gauge
// (REG A4) before the display smoothing in pmu_axp2101.yaml, which can lag the cell by many percent.
// on_src / off_src are REG20 / REG21 (power-on / power-off source of the previous cycle), latched at boot.
struct PmuTelemetry {
  int gauge_raw = -1;
  bool boot_valid = false;
  uint8_t on_src = 0;
  uint8_t off_src = 0;
  uint8_t cv_reg = 0;
  int icc_ma = 0;
};

inline PmuTelemetry &pmu_telemetry() {
  static PmuTelemetry t;
  return t;
}

// Critical-battery guard: on battery, hibernate cleanly before the PMIC's DCDC under-voltage cutoff
// cuts power mid-write (Wrist: last_off=dcdc-undervolt at a displayed 60 %). Trips after
// kCriticalSamples consecutive VBAT samples (one per kCriticalSampleMs) below LATTER_BATT_CRITICAL_MV,
// so a single Wi-Fi TX sag never triggers it.
#ifndef LATTER_BATT_CRITICAL_MV
#define LATTER_BATT_CRITICAL_MV 3450
#endif
struct CriticalBatteryGuard {
  static constexpr uint32_t kCriticalSampleMs = 10000U;
  static constexpr uint8_t kCriticalSamples = 3;
  uint32_t last_sample_ms = 0;
  uint8_t below_count = 0;
  bool tripped = false;

  // Returns true when a VBAT sample is due.
  bool sample_due(uint32_t now_ms) const {
    return last_sample_ms == 0 || (now_ms - last_sample_ms) >= kCriticalSampleMs;
  }
  // Feeds one sample; returns true exactly once, when the guard trips.
  bool feed(bool on_battery, int vbat_mv, uint32_t now_ms) {
    last_sample_ms = (now_ms == 0) ? 1U : now_ms;
    if (!on_battery || vbat_mv <= 0) {
      below_count = 0;
      tripped = false;
      return false;
    }
    if (vbat_mv < LATTER_BATT_CRITICAL_MV) {
      if (below_count < 255) below_count++;
    } else {
      below_count = 0;
    }
    if (!tripped && below_count >= kCriticalSamples) {
      tripped = true;
      return true;
    }
    return false;
  }
};

// Voice sessions return the display to the state it was in before the session started:
// active -> normal timeout, screensaver -> screensaver (or off if no longer allowed), off -> off.
// A user-initiated start (wake word / PTT) always captures; HA-initiated starts and continued-conversation
// turns capture only if no session is armed. Touching the screen during the session keeps it active.
struct VaDisplayRestore {
  bool armed = false;
  int prior_state = 0;  // 0 active, 1 screensaver, 2 off (display_power_state semantics)
  uint32_t session_start_ms = 0;

  void capture(int display_power_state, bool brightness_on, uint32_t now_ms, bool force) {
    if (armed && !force) return;
    armed = true;
    prior_state = brightness_on ? display_power_state : 2;
    session_start_ms = (now_ms == 0) ? 1U : now_ms;
  }
  bool touched_since_start(uint32_t last_touch_ms) const {
    return armed && last_touch_ms != 0 && static_cast<int32_t>(last_touch_ms - session_start_ms) > 0;
  }
};

inline VaDisplayRestore &va_display_restore() {
  static VaDisplayRestore r;
  return r;
}

inline CriticalBatteryGuard &critical_battery_guard() {
  static CriticalBatteryGuard g;
  return g;
}
// Modem power-save mode the Wi-Fi driver is actually in: 0 none, 1 min modem, 2 max modem, -1 unknown.
inline int wifi_ps_mode_now() {
#ifdef USE_ESP32
  wifi_ps_type_t t;
  if (esp_wifi_get_ps(&t) == ESP_OK) return static_cast<int>(t);
#endif
  return -1;
}
inline void log_power_and_mem_profile(const char *trigger, int pwr_state, bool usb, bool chg,
                                      float batt_pct, float batt_v, bool spk_pa, bool mww_run,
                                      bool va_act, bool mp_play, bool ble_act, int game_id,
                                      uint32_t sleep_start_ms) {
  if (!profiler_enabled()) return;
  uint32_t now_m = esphome::millis();
  uint32_t sleep_age_s = (sleep_start_ms > 0 && now_m >= sleep_start_ms)
                             ? ((now_m - sleep_start_ms) / 1000U)
                             : 0U;
  ESP_LOGI("PROF",
           "[PROF:POWER] trig=%s pwr=%d usb=%d chg=%d batt=%.0f%%(%.2fV) gauge=%d%% spk_pa=%d mww=%d va=%d mp=%d ble=%d game=%d sleep_age=%lus wom=%d/%lu wifi_ps=%d",
           trigger, pwr_state, (int)usb, (int)chg,
           std::isnan(batt_pct) ? -1.0f : batt_pct,
           std::isnan(batt_v) ? 0.0f : batt_v, pmu_telemetry().gauge_raw,
           (int)spk_pa, (int)mww_run, (int)va_act, (int)mp_play, (int)ble_act, game_id,
           (unsigned long)sleep_age_s, (int)imu_wom().armed, (unsigned long)imu_wom().events,
           wifi_ps_mode_now());
  pm_log_profile();
  pm_log_wake_profile();
#ifdef USE_ESP32
  size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U;
  size_t int_max  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024U;
  size_t ps_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U;
  size_t ps_min   = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM) / 1024U;
  size_t int_min  = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024U;
  size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024U;
  ESP_LOGI("PROF", "[PROF:MEM] int_free=%luKB int_max_blk=%luKB int_min=%luKB dma_free=%luKB psram_free=%luKB psram_min=%luKB",
           (unsigned long)int_free, (unsigned long)int_max, (unsigned long)int_min, (unsigned long)dma_free,
           (unsigned long)ps_free, (unsigned long)ps_min);
#endif
}

// Internal RAM low-water watch, polled every second whether or not the profiler is on: when the
// lowest-ever internal free memory falls by 4 KB or more, log by how much and what was active in that
// second (Bluetooth, wake word, voice turn, media, page), so the consumer behind a dip shows up in the
// log instead of only in the low-water sensor's history.
inline void watch_internal_low_water(bool ble_act, bool mww_run, bool va_act, bool mp_play, int page) {
#ifdef USE_ESP32
  static size_t last_min = 0;
  const size_t cur_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  if (last_min == 0) {
    last_min = cur_min;
    return;
  }
  if (cur_min + 4096U <= last_min) {
    ESP_LOGW("PROF", "[PROF:LOWWATER] internal min %luKB -> %luKB (free=%luKB max_blk=%luKB) ble=%d mww=%d va=%d mp=%d page=%d",
             (unsigned long)(last_min / 1024U), (unsigned long)(cur_min / 1024U),
             (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U),
             (unsigned long)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024U),
             (int)ble_act, (int)mww_run, (int)va_act, (int)mp_play, page);
    last_min = cur_min;
  }
#else
  (void)ble_act; (void)mww_run; (void)va_act; (void)mp_play; (void)page;
#endif
}

// On-demand memory report ("System: Log Memory Report"): heap totals per region plus every FreeRTOS
// task with its stack location (internal RAM or PSRAM) and high-water mark (bytes never used), so
// internal-RAM consumers can be identified on the device instead of guessed.
inline void log_memory_report() {
#ifdef USE_ESP32
  auto region = [](const char *name, uint32_t caps) {
    multi_heap_info_t hi;
    heap_caps_get_info(&hi, caps);
    ESP_LOGI("PROF", "[PROF:HEAP] %s free=%uKB largest=%uKB min_ever=%uKB alloc=%uKB blocks=%u/%u", name,
             (unsigned)(hi.total_free_bytes / 1024U), (unsigned)(hi.largest_free_block / 1024U),
             (unsigned)(hi.minimum_free_bytes / 1024U), (unsigned)(hi.total_allocated_bytes / 1024U),
             (unsigned)hi.allocated_blocks, (unsigned)hi.free_blocks);
  };
  region("internal", MALLOC_CAP_INTERNAL);
  region("dma", MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  region("psram", MALLOC_CAP_SPIRAM);
#if configUSE_TRACE_FACILITY
  UBaseType_t n = uxTaskGetNumberOfTasks();
  auto *ts = static_cast<TaskStatus_t *>(heap_caps_malloc(sizeof(TaskStatus_t) * (n + 4), MALLOC_CAP_SPIRAM));
  if (ts == nullptr) return;
  n = uxTaskGetSystemState(ts, n + 4, nullptr);
  unsigned int_stacks = 0;
  for (UBaseType_t i = 0; i < n; i++) {
    const bool ext = esp_ptr_external_ram(ts[i].pxStackBase);
    if (!ext) int_stacks++;
    ESP_LOGI("PROF", "[PROF:TASK] %-16s stack=%s unused=%uB prio=%u", ts[i].pcTaskName, ext ? "PSRAM" : "INT",
             (unsigned)ts[i].usStackHighWaterMark, (unsigned)ts[i].uxCurrentPriority);
  }
  ESP_LOGI("PROF", "[PROF:TASK] %u tasks, %u with internal-RAM stacks", (unsigned)n, int_stacks);
  heap_caps_free(ts);
#else
  ESP_LOGI("PROF", "[PROF:TASK] task list needs CONFIG_FREERTOS_USE_TRACE_FACILITY");
#endif
#endif
}

inline bool &applying_power_profile() {
  static bool v = false;
  return v;
}

template<typename SelT>
inline void mark_power_profile_custom_if_manual(SelT *prof_sel) {
  if (prof_sel == nullptr || applying_power_profile() || esphome::millis() < 3500U) return;
  if (prof_sel->current_option() != "Custom") {
    prof_sel->publish_state("Custom");
  }
}

// ------------------------------------------------------------------------------
// Phase 2 Power Model: Wake Attribution, Raise-to-Wake, Double-Tap & Render Budget
// ------------------------------------------------------------------------------

// Why the screen woke. Counted always (cheap) and reported by the [PROF:*] flight recorder so a
// day of telemetry shows which inputs keep a battery device awake.
enum class WakeReason : uint8_t { TOUCH = 0, RAISE, CROWN, VOICE, NOTIFY, ALERT, MEDIA, OTHER, COUNT };

inline const char *wake_reason_name(WakeReason r) {
  switch (r) {
    case WakeReason::TOUCH:  return "touch";
    case WakeReason::RAISE:  return "raise";
    case WakeReason::CROWN:  return "crown";
    case WakeReason::VOICE:  return "voice";
    case WakeReason::NOTIFY: return "notify";
    case WakeReason::ALERT:  return "alert";
    case WakeReason::MEDIA:  return "media";
    default:                 return "other";
  }
}

struct WakeStats {
  uint32_t counts[static_cast<int>(WakeReason::COUNT)] = {};
  WakeReason pending = WakeReason::OTHER;
};

inline WakeStats &wake_stats() {
  static WakeStats st;
  return st;
}

// Call immediately before executing wake_to_active_state from a specific input path.
inline void note_wake_reason(WakeReason r) { wake_stats().pending = r; }

// Called by wake_to_active_state when it actually transitions from screen-off/screensaver.
inline void record_screen_wake() {
  auto &ws = wake_stats();
  const WakeReason r = ws.pending;
  ws.pending = WakeReason::OTHER;
  ws.counts[static_cast<int>(r)]++;
  if (!profiler_enabled()) return;
  ESP_LOGI("PROF", "[PROF:WAKE] reason=%s total_touch=%lu raise=%lu crown=%lu voice=%lu notify=%lu alert=%lu media=%lu other=%lu",
           wake_reason_name(r),
           (unsigned long)ws.counts[0], (unsigned long)ws.counts[1], (unsigned long)ws.counts[2],
           (unsigned long)ws.counts[3], (unsigned long)ws.counts[4], (unsigned long)ws.counts[5],
           (unsigned long)ws.counts[6], (unsigned long)ws.counts[7]);
}

// Orientation-qualified raise-to-wake (screen-off only). A deliberate motion spike must be followed
// within 1.5 s by the screen settling within ~55 deg of face-up/face-down (|z| dominant) for 200 ms.
// Rejects arm swing, walking and pocket jostle, where the screen stays roughly vertical (|z| small).
struct RaiseToWakeDetector {
  uint32_t motion_ms = 0;
  uint32_t face_up_since_ms = 0;

  void reset() {
    motion_ms = 0;
    face_up_since_ms = 0;
  }

  bool update(float dyn, float thresh, float ax, float ay, float az, uint32_t now_ms) {
    if (std::isnan(dyn) || std::isnan(ax) || std::isnan(ay) || std::isnan(az)) return false;
    if (dyn > thresh) motion_ms = (now_ms == 0) ? 1U : now_ms;
    if (motion_ms == 0 || (now_ms - motion_ms) > 1500U) {
      face_up_since_ms = 0;
      return false;
    }
    const float horiz = sqrtf(ax * ax + ay * ay);
    const float vert = fabsf(az);
    const bool face_up = (vert >= 0.5f) && (vert >= horiz * 0.7f);
    if (!face_up) {
      face_up_since_ms = 0;
      return false;
    }
    if (face_up_since_ms == 0) face_up_since_ms = (now_ms == 0) ? 1U : now_ms;
    if ((now_ms - face_up_since_ms) >= 200U) {
      reset();
      return true;
    }
    return false;
  }
};

inline RaiseToWakeDetector &raise_to_wake() {
  static RaiseToWakeDetector det;
  return det;
}

// Wearables on battery require a double tap to wake from screen-off (sleeve and skin contact
// produce single touches all day). Docked wearables and all other devices keep single-tap wake.
inline bool tap_wake_needs_double_tap(bool usb_on) {
#if LATTER_IS_WEARABLE
  return !usb_on;
#else
  (void)usb_on;
  return false;
#endif
}

// Returns true when this touch-down completes a valid wake gesture from screen-off.
inline bool accept_screen_off_tap(bool usb_on, uint32_t now_ms) {
  if (!tap_wake_needs_double_tap(usb_on)) return true;
  static uint32_t first_tap_ms = 0;
  if (first_tap_ms == 0 || (now_ms - first_tap_ms) > 450U) {
    first_tap_ms = (now_ms == 0) ? 1U : now_ms;
    return false;
  }
  if ((now_ms - first_tap_ms) < 40U) return false;  // contact bounce
  first_tap_ms = 0;
  return true;
}

// Battery render budget: ambient motion (animated weather canvas) runs at full rate on USB and for
// 10 s after the last wake or touch on battery, then the page falls back to the 1 Hz display refresh.
inline bool battery_motion_allowed(bool usb_on, uint32_t now_ms) {
#if LATTER_IS_STATIONARY
  (void)usb_on;
  (void)now_ms;
  return true;
#else
  if (usb_on) return true;
  const uint32_t last = std::max(last_screen_wake_ms(), touch_gesture().last_touch_ms);
  return (now_ms - last) < 10000U;
#endif
}

// ------------------------------------------------------------------------------

}  // namespace latter_ui
