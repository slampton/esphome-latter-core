#pragma once

// ==============================================================================
// Pocket Assistant Core UI, Fast-Math, 3D Projection & Touch Toolkit (ui_core.h)
// Portable across circular ESPHome AMOLED displays (default 466x466)
// Zero-exception (-fno-exceptions), zero-heap-churn embedded graphics primitives
// ==============================================================================

#include "esphome.h"
#ifdef USE_ESP32
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#endif
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

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
#ifndef LATTER_PANEL_IPS
#if defined(LATTER_BACKLIGHT_PIN)
#define LATTER_PANEL_IPS 1
#else
#define LATTER_PANEL_IPS 0
#endif
#endif

static constexpr int SCREEN_W = LATTER_SCREEN_W;
static constexpr int SCREEN_H = LATTER_SCREEN_H;
static constexpr int SCREEN_CX = SCREEN_W / 2;
static constexpr int SCREEN_CY = SCREEN_H / 2;
static constexpr int SCREEN_R = SCREEN_W / 2;
static constexpr int SCREEN_MIN_DIM = (SCREEN_W < SCREEN_H) ? SCREEN_W : SCREEN_H;
static constexpr int SCREEN_MIN_R = SCREEN_MIN_DIM / 2;
static constexpr int STD_ART_SIZE = 328;
static constexpr int STD_ART_OFFSET = (SCREEN_W - STD_ART_SIZE) / 2;

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
//   - Portrait Rectangle Watch (LATTER_ROUND_DISPLAY=0, 410x502): Full horizontal half-width (SCREEN_W/2)
//     across y in [0..SCREEN_H-1] with smooth r=38px rounded watch corners at top and bottom!
#ifndef LATTER_CORNER_R
#if LATTER_ROUND_DISPLAY
#define LATTER_CORNER_R (LATTER_SCREEN_W / 2)
#elif LATTER_IS_WEARABLE
#define LATTER_CORNER_R 62
#else
#define LATTER_CORNER_R 20
#endif
#endif

inline int circle_half_span(int c, int inset = 5) {
#if LATTER_ROUND_DISPLAY
  if (c < 0 || c >= SCREEN_W) return 0;
  int span = static_cast<int>(CIRCLE_HW[c]) - inset;
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
      int dx_corner = static_cast<int>(std::sqrt(static_cast<float>(cr * cr - dy * dy)));
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

// Hardware-Trait Driven PMU IRQ & Wearable Motion Helpers (Zero hardcoded board numbers)
inline bool hal_pmu_irq_asserted() {
#if defined(LATTER_PMU_IRQ_ACTIVE_HIGH) && LATTER_PMU_IRQ_ACTIVE_HIGH
  static int prev_lvl = 1;
  int cur_lvl = gpio_get_level(static_cast<gpio_num_t>(LATTER_PMU_IRQ_PIN));
  bool rising_edge = (cur_lvl == 1 && prev_lvl == 0);
  prev_lvl = cur_lvl;
  return rising_edge;
#elif (defined(LATTER_NO_PMU_IRQ_PIN) && LATTER_NO_PMU_IRQ_PIN) || defined(CONFIG_IDF_TARGET_ESP32P4)
  return false;
#else
  return (gpio_get_level(GPIO_NUM_3) == 0);
#endif
}

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

// Lightweight 3D Vector, Euler Rotation & Perspective Projection Primitives
struct Vec3 {
  float x, y, z;
};

struct ProjectedPoint {
  int x, y;
  float scale;
  bool visible;
};

inline Vec3 rotate_3d_xyz(Vec3 v, float rx, float ry, float rz) {
  float sx, cx_r, sy, cy_r, sz, cz_r;
  fast_sincos(rx, sx, cx_r);
  fast_sincos(ry, sy, cy_r);
  fast_sincos(rz, sz, cz_r);

  // Rotate around X
  float y1 = v.y * cx_r - v.z * sx;
  float z1 = v.y * sx + v.z * cx_r;
  // Rotate around Y
  float x2 = v.x * cy_r + z1 * sy;
  float z2 = -v.x * sy + z1 * cy_r;
  // Rotate around Z
  float x3 = x2 * cz_r - y1 * sz;
  float y3 = x2 * sz + y1 * cz_r;
  return {x3, y3, z2};
}

inline ProjectedPoint project_3d(Vec3 v, int cx, int cy, float fov = 240.0f, float viewer_dist = 320.0f) {
  float depth = v.z + viewer_dist;
  if (depth <= 15.0f) return {cx, cy, 0.0f, false};
  float k = fov / depth;
  int sx = cx + static_cast<int>(v.x * k);
  int sy = cy + static_cast<int>(v.y * k);
  return {sx, sy, k, is_inside_bezel(sx, sy, cy, 4)};
}

// Deterministic Zero-RAM 2D Hash & Smooth Value Noise (0.0f .. 1.0f) for procedural shaders
inline float hash2d_01(int x, int y) {
  uint32_t n = static_cast<uint32_t>(x) * 374761393U + static_cast<uint32_t>(y) * 668265263U;
  n = (n ^ (n >> 13)) * 1274126177U;
  return static_cast<float>((n ^ (n >> 16)) & 0xFFFFU) * (1.0f / 65535.0f);
}

inline float value_noise_2d(float x, float y) {
  int xi = static_cast<int>(x);
  if (x < 0.0f && static_cast<float>(xi) != x) xi--;
  int yi = static_cast<int>(y);
  if (y < 0.0f && static_cast<float>(yi) != y) yi--;
  float xf = x - static_cast<float>(xi);
  float yf = y - static_cast<float>(yi);
  float u = xf * xf * (3.0f - 2.0f * xf);
  float v = yf * yf * (3.0f - 2.0f * yf);
  float a = hash2d_01(xi, yi);
  float b = hash2d_01(xi + 1, yi);
  float c = hash2d_01(xi, yi + 1);
  float d = hash2d_01(xi + 1, yi + 1);
  return (a + (b - a) * u) + ((c - a) + (a - b - c + d) * u) * v;
}

// Fast HSV-to-RGB (h in [0..1], s in [0..1], v in [0..1]) & Saturating Additive Blend
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

inline esphome::Color blend_additive(esphome::Color a, esphome::Color b) {
  int r = static_cast<int>(a.r) + static_cast<int>(b.r);
  int g = static_cast<int>(a.g) + static_cast<int>(b.g);
  int bl = static_cast<int>(a.b) + static_cast<int>(b.b);
  return esphome::Color(
      static_cast<uint8_t>(r > 255 ? 255 : r),
      static_cast<uint8_t>(g > 255 ? 255 : g),
      static_cast<uint8_t>(bl > 255 ? 255 : bl));
}

template<typename DisplayT>
inline void draw_glow_line(DisplayT &it, int x0, int y0, int x1, int y1,
                           esphome::Color core_col, esphome::Color halo_col) {
  int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
  int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
  if (dx >= dy) {
    it.line(x0, y0 - 1, x1, y1 - 1, halo_col);
    it.line(x0, y0 + 1, x1, y1 + 1, halo_col);
  } else {
    it.line(x0 - 1, y0, x1 - 1, y1, halo_col);
    it.line(x0 + 1, y0, x1 + 1, y1, halo_col);
  }
  it.line(x0, y0, x1, y1, core_col);
}

// Universal Audio-Aware Ambient Animation Pacer (20 FPS Idle / ~13.3 FPS Audio-Protected):
// Shared across all Docked Screensavers, Sky & Weather motion wallpapers, and future Home Page
// live backgrounds. Unlocks full 20 FPS (50ms interval, 42ms coalesce guard against disp1's 1s timer)
// whenever local audio/alerts are idle, and automatically paces at 75ms (~13.3 FPS) during active
// local Sendspin FLAC playback so audio DMA and Octal PSRAM never contend.
inline uint32_t &last_ambient_anim_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline uint32_t &last_screensaver_draw_ms() {
  return last_ambient_anim_ms();
}

inline void record_ambient_frame_ms(uint32_t now_ms) {
  last_ambient_anim_ms() = now_ms;
}

inline bool should_step_ambient_anim(uint32_t now_ms, bool local_audio_busy,
                                     bool va_busy = false, bool alert_busy = false) {
  (void)local_audio_busy;
  if (va_busy) return false;
  const uint32_t min_gap = alert_busy ? 120U : 42U;
  return (now_ms - last_ambient_anim_ms() >= min_gap);
}

// Standardized Dynamic Scroll Easing Constants (5-Step Cubic Eased Curve)
static constexpr int16_t SHARED_SCROLL_EASE_116[5] = {0, 25, 63, 99, 116}; // 2-Pill 116px shift
static constexpr int16_t SHARED_SCROLL_EASE_174[5] = {0, 38, 95, 148, 174}; // 3-Pill 174px shift
static constexpr int16_t SHARED_SCROLL_EASE_232[5] = {0, 50, 126, 198, 232}; // 4-Pill 232px shift
static constexpr int16_t SHARED_SCROLL_EASE_252[5] = {0, 54, 136, 215, 252}; // 4-Card 252px shift (63px stride)



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
// 2. Reusable Geometric & Vector UI Drawing Primitives
// ------------------------------------------------------------------------------
template<typename DisplayT, typename FontT>
inline void render_pill_button(DisplayT &it, int x1, int x2, int y_mid, int r,
                               esphome::Color border_c, esphome::Color bg_c,
                               const char *label, esphome::Color text_c, FontT *font) {
  int ri = (r > 2) ? (r - 2) : 1;
  int span_w = x2 - x1;
  // Draw end caps + single-pass interior fill + 2px top/bottom border bands (avoids 2x center rectangle overdraw!)
  it.filled_circle(x1, y_mid, r, border_c);
  it.filled_circle(x2, y_mid, r, border_c);
  it.filled_circle(x1, y_mid, ri, bg_c);
  it.filled_circle(x2, y_mid, ri, bg_c);
  it.filled_rectangle(x1, y_mid - r,      span_w, 2,          border_c);
  it.filled_rectangle(x1, y_mid + r - 1,  span_w, 2,          border_c);
  it.filled_rectangle(x1, y_mid - ri,     span_w, 2 * ri + 1, bg_c);

  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    int btn_cx = (x1 + x2) / 2;
    it.print(btn_cx, y_mid, font, text_c, esphome::display::TextAlign::CENTER, label);
  }
}

template<typename DisplayT, typename FontT>
inline void render_modern_card(DisplayT &it, int cx_card, int cy_card, int w, int h, int r,
                               esphome::Color bg_c, esphome::Color border_c,
                               const char *label, esphome::Color text_c, FontT *font) {
  if (std::abs(cx_card - SCREEN_CX) <= 4) {
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

  // Single-pass scanline rounded rectangle (zero filled_circle calls, zero pixel overdraw):
  // 1. Top and bottom outer border horizontal lines (row 0 and row h-1)
  int top_in = r / 2 + 2;
  it.horizontal_line(x0 + top_in, y0,         w - 2 * top_in, border_c);
  it.horizontal_line(x0 + top_in, y0 + h - 1, w - 2 * top_in, border_c);

  // 2. Top & bottom rounded corner rows (dy = 1 .. r-1)
  for (int dy = 1; dy < r; dy++) {
    int ry = r - dy;
    int dx_out = r - static_cast<int>(std::sqrt(static_cast<float>(r * r - ry * ry)) + 0.5f);
    int dx_in  = dx_out + ((dy <= 2) ? 2 : 1);
    int span_out = w - 2 * dx_out;
    int span_in  = w - 2 * dx_in;
    int yt = y0 + dy;
    int yb = y0 + h - 1 - dy;
    if (span_in > 0) {
      it.horizontal_line(x0 + dx_out, yt, dx_in - dx_out, border_c);
      it.horizontal_line(x0 + dx_in,  yt, span_in,        bg_c);
      it.horizontal_line(x0 + w - dx_in, yt, dx_in - dx_out, border_c);

      it.horizontal_line(x0 + dx_out, yb, dx_in - dx_out, border_c);
      it.horizontal_line(x0 + dx_in,  yb, span_in,        bg_c);
      it.horizontal_line(x0 + w - dx_in, yb, dx_in - dx_out, border_c);
    } else if (span_out > 0) {
      it.horizontal_line(x0 + dx_out, yt, span_out, border_c);
      it.horizontal_line(x0 + dx_out, yb, span_out, border_c);
    }
  }

  // 3. Center body fill + 1px left/right vertical borders (rows r .. h-r-1)
  int mid_h = h - 2 * r;
  if (mid_h > 0) {
    it.filled_rectangle(x0 + 1, y0 + r, w - 2, mid_h, bg_c);
    it.vertical_line(x0,         y0 + r, mid_h, border_c);
    it.vertical_line(x0 + w - 1, y0 + r, mid_h, border_c);
  }

  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    it.print(cx_card, cy_card, font, text_c, esphome::display::TextAlign::CENTER, label);
  }
}

template<typename DisplayT>
inline void render_scroll_chevron(DisplayT &it, int x, int y, bool is_up,
                                  esphome::Color c, esphome::Color shadow_c, bool shadow = false) {
  if (shadow) {
    if (is_up) {
      it.line(x - 7, y + 5, x, y - 4, shadow_c);
      it.line(x - 7, y + 6, x, y - 3, shadow_c);
      it.line(x + 7, y + 5, x, y - 4, shadow_c);
      it.line(x + 7, y + 6, x, y - 3, shadow_c);
    } else {
      it.line(x - 7, y - 3, x, y + 6, shadow_c);
      it.line(x - 7, y - 2, x, y + 7, shadow_c);
      it.line(x + 7, y - 3, x, y + 6, shadow_c);
      it.line(x + 7, y - 2, x, y + 7, shadow_c);
    }
  }
  if (is_up) {
    it.line(x - 7, y + 4, x, y - 5, c);
    it.line(x - 7, y + 5, x, y - 4, c);
    it.line(x + 7, y + 4, x, y - 5, c);
    it.line(x + 7, y + 5, x, y - 4, c);
  } else {
    it.line(x - 7, y - 4, x, y + 5, c);
    it.line(x - 7, y - 5, x, y + 4, c);
    it.line(x + 7, y - 4, x, y + 5, c);
    it.line(x + 7, y - 5, x, y + 4, c);
  }
}

template <typename DisplayT>
inline void render_home_indicator_bar(DisplayT &it, int cx, int y_pos = 434,
                                      esphome::Color bar_col = esphome::Color(175, 190, 210)) {
  // Sleek 108x5px rounded bottom gesture pill bar for Swipe-Up-to-Home
  const int half_w = 54;
  // 1px dark drop shadow for contrast over bright daytime skies
  it.filled_rectangle(cx - half_w + 2, y_pos - 1, (half_w - 2) * 2, 5, esphome::Color(0, 0, 0));
  // Pill core
  it.filled_rectangle(cx - half_w + 2, y_pos - 2, (half_w - 2) * 2, 5, bar_col);
  it.filled_circle(cx - half_w + 2, y_pos, 2, bar_col);
  it.filled_circle(cx + half_w - 2, y_pos, 2, bar_col);
}

template <typename DisplayT>
inline void render_dock_page_pips(DisplayT &it, int cx, int y_pos, int total_pages, int active_page,
                                  esphome::Color col_active, esphome::Color col_inactive) {
  if (total_pages <= 1) return;
  const int spacing = 14;
  const int total_w = (total_pages - 1) * spacing;
  const int start_x = cx - (total_w / 2);
  for (int p = 0; p < total_pages; p++) {
    int px = start_x + p * spacing;
    if (p == active_page) {
      // Active page: 8x5px rounded pill with 1px drop shadow
      it.filled_rectangle(px - 2, y_pos - 1, 5, 5, esphome::Color(0, 0, 0));
      it.filled_rectangle(px - 3, y_pos - 2, 7, 5, col_active);
      it.filled_circle(px - 3, y_pos, 2, col_active);
      it.filled_circle(px + 3, y_pos, 2, col_active);
    } else {
      // Inactive page: r = 2 dot with 1px drop shadow
      it.filled_circle(px + 1, y_pos + 1, 2, esphome::Color(0, 0, 0));
      it.filled_circle(px,     y_pos,     2, col_inactive);
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
  const int mask_x = (SCREEN_W < 380) ? (SCREEN_W - 30) : (SCREEN_W - 42);
  const int chev_x = (SCREEN_W < 380) ? (SCREEN_W - 15) : (SCREEN_W - 21);
#endif
  it.filled_rectangle(mask_x, 140, SCREEN_W - mask_x, 160, col_bg);
  if (show_up) render_scroll_chevron(it, chev_x, 162, true, col_active, esphome::Color(0, 0, 0), false);
  if (show_down) render_scroll_chevron(it, chev_x, 278, false, col_active, esphome::Color(0, 0, 0), false);
}

// Universal Low-Profile Back Label or Swipe-Up Home Indicator Bar
template<typename DisplayT, typename FontT>
inline void render_back_button(DisplayT &it, int cx, FontT *font,
                               int y_pos = 426, const char *label = "< Back",
                               esphome::Color text_col = esphome::Color(158, 158, 158)) {
  const int home_bar_y = LATTER_ROUND_DISPLAY ? 434 : (SCREEN_H - 12);
  const int back_lbl_y = LATTER_ROUND_DISPLAY ? y_pos : ((y_pos >= 400) ? (SCREEN_H - 28) : y_pos);
  if (label != nullptr && std::strcmp(label, "Home") == 0) {
    render_home_indicator_bar(it, cx, home_bar_y);
    return;
  }
  const char *eff_label = (label != nullptr && std::strcmp(label, "< BACK") == 0) ? "< Back" : label;
  it.print(cx + 1, back_lbl_y + 1, font, esphome::Color(0, 0, 0),
           esphome::display::TextAlign::CENTER, eff_label);
  it.print(cx, back_lbl_y, font, text_col,
           esphome::display::TextAlign::CENTER, eff_label);
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
                                 int sys_mode, int weather_sub, int room_sub, int rem_sub, int stuff_sub) {
  int sub = 0;
  if (gui_page == 1) sub = home_sub;
  else if (gui_page == 2) sub = music_mode;
  else if (gui_page == 3) sub = active_game;
  else if (gui_page == 4) sub = sys_mode;
  else if (gui_page == 5) sub = weather_sub;
  else if (gui_page == 6) sub = room_sub;
  else if (gui_page == 7) sub = rem_sub;
  else if (gui_page == 8) sub = stuff_sub;
  return (gui_page * 100) + sub;
}

// Zero-overdraw album art blitter with fused 2x-stride checkerboard scrim,
// portable across circular (LATTER_ROUND_DISPLAY=1) and square/rectangular displays.
template<typename DisplayT, typename ImageT>
inline bool render_album_art_canvas(DisplayT &it, ImageT *target_art, int cx,
                                    bool is_standard, bool is_letterbox,
                                    bool apply_scrim, bool show_metadata) {
  if (target_art == nullptr) return false;
  const uint16_t *src_data = reinterpret_cast<const uint16_t *>(target_art->get_data_start());
  int src_w = target_art->get_width();
  int src_h = target_art->get_height();
  if (src_data == nullptr || src_w <= 0 || src_h <= 0) return false;

  int start_y = is_letterbox ? 60 : 0;
  int end_y = is_letterbox ? 385 : SCREEN_H;
  int scrim_end = is_letterbox ? 398 : 405;

  if (src_w == SCREEN_W && src_h == SCREEN_H && !is_standard) {
    for (int y = start_y; y < end_y; y++) {
      bool row_scrim = apply_scrim || (show_metadata && y >= 340 && y < scrim_end);
      if (row_scrim && ((y & 1) != 0)) {
        continue; // Odd row stays pure black in pre-cleared framebuffer with zero CPU/PSRAM cost!
      }
      int hw = circle_half_span(y, 0);
      int start_x = std::max(0, cx - hw);
      int end_x = std::min(SCREEN_W - 1, cx + hw);
      const uint16_t *src_row = src_data + y * SCREEN_W;
      int x = start_x;
      while (x <= end_x) {
        uint16_t p = src_row[x];
        if ((p & 0xE79C) == 0) {
          x++;
          continue;
        }
        uint16_t p_masked = p & 0xE79C;
        int run_start = x++;
        while (x <= end_x && ((src_row[x] & 0xE79C) == p_masked)) {
          x++;
        }
        esphome::Color col(
            static_cast<uint8_t>((p & 0xF800) >> 8),
            static_cast<uint8_t>((p & 0x07E0) >> 3),
            static_cast<uint8_t>((p & 0x001F) << 3));
        int run_len = x - run_start;
        if (run_len == 1) {
          it.draw_pixel_at(run_start, y, col);
        } else {
          it.horizontal_line(run_start, y, run_len, col);
        }
      }
    }
    return true;
  }

  if (is_standard) {
    int step_x = (src_w << 12) / STD_ART_SIZE;
    int step_y = (src_h << 12) / STD_ART_SIZE;
    int curr_y = 0;
    for (int y = 0; y < STD_ART_SIZE; y++) {
      int sy = curr_y >> 12;
      curr_y += step_y;
      const uint16_t *src_row = src_data + sy * src_w;
      int py = STD_ART_OFFSET + y;
      int curr_x = 0;
      for (int x = 0; x < STD_ART_SIZE; x++) {
        if (!apply_scrim || ((x + py) & 1) != 0) {
          uint16_t p = src_row[curr_x >> 12];
          if (p != 0) {
            uint8_t r = (p & 0xF800) >> 8;
            uint8_t g = (p & 0x07E0) >> 3;
            uint8_t b = (p & 0x001F) << 3;
            it.draw_pixel_at(STD_ART_OFFSET + x, py, esphome::Color(r, g, b));
          }
        }
        curr_x += step_x;
      }
    }
    return true;
  }

  int step_x = (src_w << 12) / SCREEN_W;
  int step_y = (src_h << 12) / SCREEN_H;
  int curr_y = start_y * step_y;
  for (int y = start_y; y < end_y; y++) {
    int sy = curr_y >> 12;
    curr_y += step_y;
    const uint16_t *src_row = src_data + sy * src_w;
    int hw = circle_half_span(y, 0);
    int start_x = std::max(0, cx - hw);
    int end_x = std::min(SCREEN_W - 1, cx + hw);
    bool row_scrim = apply_scrim || (show_metadata && y >= 340 && y < scrim_end);
    if (row_scrim) {
      int x0 = start_x + (((start_x + y) & 1) == 0 ? 1 : 0);
      for (int x = x0; x <= end_x; x += 2) {
        uint16_t p = src_row[(x * step_x) >> 12];
        if (p != 0) {
          uint8_t r = (p & 0xF800) >> 8;
          uint8_t g = (p & 0x07E0) >> 3;
          uint8_t b = (p & 0x001F) << 3;
          it.draw_pixel_at(x, y, esphome::Color(r, g, b));
        }
      }
    } else {
      int x = start_x;
      while (x <= end_x) {
        uint16_t p = src_row[(x * step_x) >> 12];
        if ((p & 0xE79C) == 0) {
          x++;
          continue;
        }
        uint16_t p_masked = p & 0xE79C;
        int run_start = x++;
        while (x <= end_x && ((src_row[(x * step_x) >> 12] & 0xE79C) == p_masked)) {
          x++;
        }
        esphome::Color col(
            static_cast<uint8_t>((p & 0xF800) >> 8),
            static_cast<uint8_t>((p & 0x07E0) >> 3),
            static_cast<uint8_t>((p & 0x001F) << 3));
        int run_len = x - run_start;
        if (run_len == 1) {
          it.draw_pixel_at(run_start, y, col);
        } else {
          it.horizontal_line(run_start, y, run_len, col);
        }
      }
    }
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
  std::string str = sanitize_utf8_for_font(raw_str);
  if (str.empty() || font == nullptr) return "";
  int w = 0, x_off = 0, base = 0, text_h = 0;
  font->measure(str.c_str(), &w, &x_off, &base, &text_h);
  if (w <= max_w) return str;

  std::string cur = str;
  if (cur.length() > 32) {
    while (cur.length() > 30) {
      while (!cur.empty() && (static_cast<unsigned char>(cur.back()) & 0xC0) == 0x80) cur.pop_back();
      if (!cur.empty()) cur.pop_back();
    }
  }
  while (!cur.empty()) {
    std::string test_str = cur + "..";
    font->measure(test_str.c_str(), &w, &x_off, &base, &text_h);
    if (w <= max_w) return test_str;
    while (!cur.empty() && (static_cast<unsigned char>(cur.back()) & 0xC0) == 0x80) cur.pop_back();
    if (!cur.empty()) cur.pop_back();
  }
  return "..";
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

inline void compute_synodic_moon_phase(bool time_valid, int64_t epoch_sec, const std::string &fallback_phase,
                                       double &out_cycle, int *out_illum = nullptr, std::string *out_label = nullptr) {
  double cycle = 0.5;
  int illum_pct = 50;
  std::string mp_label = "Moon";

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
    else if (fallback_phase == "last_quarter" || fallback_phase == "third_quarter") { cycle = 0.75; illum_pct = 50; mp_label = "Last Quarter"; }
    else if (fallback_phase == "waning_crescent") { cycle = 0.88; illum_pct = 22; mp_label = "Waning Crescent"; }
  }

  out_cycle = cycle;
  if (out_illum != nullptr) *out_illum = illum_pct;
  if (out_label != nullptr) *out_label = mp_label;
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

inline uint32_t &last_screen_wake_ms() {
  static uint32_t ts = 0;
  return ts;
}

template<typename TouchT>
inline bool is_back_button(const TouchT &t) {
#if LATTER_ROUND_DISPLAY
  return (t.y >= 404 && t.y <= 466 && t.x >= 116 && t.x <= 350);
#else
  return (t.y >= (SCREEN_H - 64) && t.y <= SCREEN_H && t.x >= (SCREEN_CX - 120) && t.x <= (SCREEN_CX + 120));
#endif
}

template<typename TouchT>
inline bool is_right_chevron_zone(const TouchT &t) {
#if LATTER_ROUND_DISPLAY
  return (t.x >= 376 && t.y >= 70 && t.y <= 395);
#else
  const int chev_min_x = (SCREEN_W < 380) ? (SCREEN_W - 36) : (SCREEN_W - 48);
  return (t.x >= chev_min_x && t.y >= 70 && t.y <= (SCREEN_H - 70));
#endif
}

template<typename TouchT>
inline int get_5slot_index(const TouchT &t, int x_min, int x_max) {
  const int eff_min = (SCREEN_W == 466) ? x_min : std::max(12, SCREEN_CX - 150);
  const int eff_max = (SCREEN_W == 466) ? x_max : std::min(SCREEN_W - 12, SCREEN_CX + 150);
  if (t.x < eff_min || t.x > eff_max) return -1;
  // Continuous 5-slot hit zones (cards at y_mid = 110, 173, 236, 299, 362; h = 44) with zero dead space
  if (t.y >= 78  && t.y < 146) return 0;
  if (t.y >= 146 && t.y < 209) return 1;
  if (t.y >= 209 && t.y < 272) return 2;
  if (t.y >= 272 && t.y < 335) return 3;
  if (t.y >= 335 && t.y < 404) return 4;
  return -1;
}

template<typename ValT, typename DeltaT, typename MinT, typename MaxT>
inline auto step_clamp(ValT val, DeltaT delta, MinT min_val, MaxT max_val) {
  auto res = val + delta;
  if (res < min_val) return static_cast<decltype(res)>(min_val);
  if (res > max_val) return static_cast<decltype(res)>(max_val);
  return res;
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

struct DisplayUpdateScheduler : public esphome::Component {
  template<typename DispT>
  static void request_ptr(DispT *disp) {
    if (disp != nullptr && !screen_is_off_standby()) {
      uint32_t now_m = esphome::millis();
      uint32_t elapsed = now_m - last_disp_update_ms();
      uint32_t delay_ms = (elapsed < 38U) ? (38U - elapsed) : 0U;
      auto *comp = static_cast<esphome::Component *>(disp);
      static_cast<DisplayUpdateScheduler *>(comp)->set_timeout(
          "async_draw", delay_ms, [disp]() {
            if (screen_is_off_standby()) return;
            if (esphome::millis() - last_disp_update_ms() < 32U) return;
            disp->update();
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

// Converts a linear media_player volume (0.0 .. 1.0) into the exact dB attenuation
// used by ESPHome's SourceSpeaker::set_volume (-20 * log10(volume)) so un-ducking
// media_mixing_input after an alert or Voice Assistant session restores the exact
// pre-duck volume level.
inline float volume_to_db_reduction(float vol) {
  if (std::isnan(vol) || vol <= 0.05f) return 26.0f;
  if (vol >= 1.0f) return 0.0f;
  float db = -20.0f * std::log10(vol);
  return (db < 0.0f) ? 0.0f : db;
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

struct FrameProfileScope {
  uint64_t t0_us = 0;
  int page = 0;
  int sub = 0;
  int pwr = 0;

  FrameProfileScope(int p, int s, int pw) : page(p), sub(s), pwr(pw) {
    if (profiler_enabled()) {
      t0_us = prof_now_us();
    }
  }

  ~FrameProfileScope() {
    if (t0_us == 0) return;
    uint32_t render_us = static_cast<uint32_t>(prof_now_us() - t0_us);
    uint32_t now_m = esphome::millis();
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
    const bool is_slow = (render_us >= 22000U);
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
      ESP_LOGI("PROF", "[PROF:FRAME_SUM] frames=%lu fps=%.1f avg=%luus max=%luus(p%d/%d) slow=%lu",
               (unsigned long)fr.window_frames, fps, (unsigned long)avg_us,
               (unsigned long)fr.max_render_us, fr.max_render_page, fr.max_render_sub,
               (unsigned long)fr.slow_frames);
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
           "[PROF:POWER] trig=%s pwr=%d usb=%d chg=%d batt=%.0f%%(%.2fV) spk_pa=%d mww=%d va=%d mp=%d ble=%d game=%d sleep_age=%lus",
           trigger, pwr_state, (int)usb, (int)chg,
           std::isnan(batt_pct) ? -1.0f : batt_pct,
           std::isnan(batt_v) ? 0.0f : batt_v,
           (int)spk_pa, (int)mww_run, (int)va_act, (int)mp_play, (int)ble_act, game_id,
           (unsigned long)sleep_age_s);
#ifdef USE_ESP32
  size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U;
  size_t int_max  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024U;
  size_t ps_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U;
  size_t ps_min   = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM) / 1024U;
  ESP_LOGI("PROF", "[PROF:MEM] int_free=%luKB int_max_blk=%luKB psram_free=%luKB psram_min=%luKB",
           (unsigned long)int_free, (unsigned long)int_max,
           (unsigned long)ps_free, (unsigned long)ps_min);
#endif
}

// ------------------------------------------------------------------------------

}  // namespace latter_ui
