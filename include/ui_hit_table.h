#pragma once

// ==============================================================================
// Latter UI Co-Located Frame Hit Table & Touch Gesture State (include/ui_hit_table.h)
// Pure C++20 UI leaf — zero ESPHome, HAL, or runtime-state includes.
// Every interactive widget registers its exact rendered pixel bounds as it draws;
// TouchGestureEngine and render_active_focus_overlay read frame_hits() directly.
// ==============================================================================

#include "ui_panel_metrics.h"
#include "ui_widget_ids.h"

#include <algorithm>
#include <cstdint>

namespace latter_ui {

struct HitRegion {
  int16_t x0{0};
  int16_t y0{0};
  int16_t x1{0};
  int16_t y1{0};
  uint16_t widget_id{0};
  int16_t param{0};
  uint8_t flags{0};
  uint8_t hit_pad{0};

  constexpr int cx() const { return (static_cast<int>(x0) + static_cast<int>(x1)) / 2; }
  constexpr int cy() const { return (static_cast<int>(y0) + static_cast<int>(y1)) / 2; }
  constexpr int width() const { return static_cast<int>(x1) - static_cast<int>(x0) + 1; }
  constexpr int height() const { return static_cast<int>(y1) - static_cast<int>(y0) + 1; }
  constexpr int radius() const {
    const int w = width();
    const int h = height();
    return ((w < h) ? w : h) / 2;
  }
  constexpr bool contains(int tx, int ty) const {
    return tx >= x0 && tx <= x1 && ty >= y0 && ty <= y1;
  }
  constexpr bool contains_padded(int tx, int ty) const {
    const int pad = static_cast<int>(hit_pad);
    return tx >= (static_cast<int>(x0) - pad) && tx <= (static_cast<int>(x1) + pad) &&
           ty >= (static_cast<int>(y0) - pad) && ty <= (static_cast<int>(y1) + pad);
  }
  float norm_x(int tx) const {
    const int span = static_cast<int>(x1) - static_cast<int>(x0);
    if (span <= 0) return 0.0f;
    if (tx <= x0) return 0.0f;
    if (tx >= x1) return 1.0f;
    return static_cast<float>(tx - x0) / static_cast<float>(span);
  }
  float norm_y(int ty) const {
    const int span = static_cast<int>(y1) - static_cast<int>(y0);
    if (span <= 0) return 0.0f;
    if (ty <= y0) return 0.0f;
    if (ty >= y1) return 1.0f;
    return static_cast<float>(ty - y0) / static_cast<float>(span);
  }
};
static_assert(sizeof(HitRegion) == 14, "HitRegion must stay compact at 14 bytes");

struct FrameHitTable {
  static constexpr uint8_t kMaxRegions = 128;
  HitRegion regions[kMaxRegions]{};
  uint8_t count{0};
  uint16_t dropped_count{0};

  void clear() {
    count = 0;
    dropped_count = 0;
  }

  void add_rect(int x0, int y0, int w, int h, uint16_t widget_id,
                uint8_t flags = kHitTap, int16_t param = 0, int hit_pad = 0);

  void add_centered_rect(int cx, int cy, int w, int h, uint16_t widget_id,
                         uint8_t flags = kHitTap, int16_t param = 0, int hit_pad = 0) {
    add_rect(cx - w / 2, cy - h / 2, w, h, widget_id, flags, param, hit_pad);
  }

  void add_circle(int cx, int cy, int r, uint16_t widget_id,
                  uint8_t flags = kHitTap, int16_t param = 0, int hit_pad = 0);

  const HitRegion *hit_test(int tx, int ty, uint8_t required_flags = kHitTap) const;
  const HitRegion *find_by_id(uint16_t widget_id) const;
  const HitRegion *find_by_id_param(uint16_t widget_id, int16_t param) const;
  int focus_count() const;
  const HitRegion *focus_at(int focus_idx) const;
};

FrameHitTable &frame_hits();

// ==============================================================================
// Standardized Touch Timing Constants, Gesture State & TouchRouter
// ==============================================================================
static constexpr uint32_t TOUCH_DEBOUNCE_RAPID     = 65;   // ms: Repeaters (Vol, Prev/Next, D-Pad, Steppers, Keypad)
static constexpr uint32_t TOUCH_DEBOUNCE_BUTTON    = 110;  // ms: Same-target stateful buttons & pills
static constexpr uint32_t TOUCH_DEBOUNCE_WAKE      = 150;  // ms: Post-wake settling lockout
static constexpr uint32_t TOUCH_DEBOUNCE_DISMISS   = 160;  // ms: Overlay/HUD dismissal
static constexpr uint32_t TOUCH_DEBOUNCE_SCROLL    = 160;  // ms: Page transitions & chevrons
static constexpr uint32_t TOUCH_GUARD_DISMISS      = 220;  // ms: Minimum post-wake delay
static constexpr uint32_t TOUCH_POST_SWIPE_LOCKOUT = 150;  // ms: Ignore moves right after swipe

inline uint32_t &touch_last_tap_ms() {
  static uint32_t s_last_tap_ms = 0;
  return s_last_tap_ms;
}

inline uint32_t &touch_last_target_key() {
  static uint32_t s_last_target_key = 0;
  return s_last_target_key;
}

inline bool touch_allow_tap(uint32_t now_ms, uint32_t cooldown_ms = TOUCH_DEBOUNCE_BUTTON,
                            uint32_t target_key = 0) {
  uint32_t &last = touch_last_tap_ms();
  uint32_t &last_key = touch_last_target_key();
  if (last != 0U) {
    if (target_key != 0U && last_key != 0U && target_key != last_key) {
      if ((now_ms - last) < 15U) return false;
    } else {
      if ((now_ms - last) <= cooldown_ms) return false;
    }
  }
  last = (now_ms == 0U) ? 1U : now_ms;
  last_key = target_key;
  return true;
}

struct TouchPt {
  int x = 0;
  int y = 0;
};

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
  uint16_t long_press_wid = 0;
  int16_t long_press_param = 0;
  bool active = false;
  bool swipe_committed = false;
  bool tap_cancelled = false;
  bool long_press_armed = false;
  bool long_press_fired = false;
  GestureAxisLock axis_lock = GestureAxisLock::None;

  void begin(int tx, int ty, uint32_t now_ms, int page_id, int ctx_id) noexcept {
    start_x = tx;
    start_y = ty;
    last_x = tx;
    last_y = ty;
    max_travel = 0;
    sample_count = 1;
    start_ms = now_ms;
    last_touch_ms = now_ms;
    start_page = page_id;
    start_context = ctx_id;
    long_press_wid = 0;
    long_press_param = 0;
    active = true;
    swipe_committed = false;
    tap_cancelled = false;
    long_press_armed = false;
    long_press_fired = false;
    axis_lock = GestureAxisLock::None;
  }

  void arm_long_press(uint16_t wid, int16_t param = 0) noexcept {
    long_press_wid = wid;
    long_press_param = param;
    long_press_armed = (wid != 0U);
    long_press_fired = false;
  }

  bool should_fire_long_press(uint32_t now_ms) const noexcept {
    return active && long_press_armed && !long_press_fired && !tap_cancelled && !swipe_committed &&
           (now_ms >= start_ms) && ((now_ms - start_ms) >= kinematics::kLongPressHoldMs);
  }

  void mark_long_press_fired() noexcept {
    long_press_fired = true;
    long_press_armed = false;
    tap_cancelled = true;
  }

  void record_move(int tx, int ty, uint32_t now_ms, int ppi) noexcept {
    last_touch_ms = now_ms;
    if (tx == last_x && ty == last_y) return;
    last_x = tx;
    last_y = ty;
    sample_count += 1;
    const int dx = tx - start_x;
    const int dy = ty - start_y;
    const int adx = (dx < 0) ? -dx : dx;
    const int ady = (dy < 0) ? -dy : dy;
    const int cur_dist = (adx > ady) ? adx : ady;
    if (cur_dist > max_travel) max_travel = cur_dist;

    const KinematicMetrics km = KinematicMetrics::for_ppi(ppi);
    if (max_travel > km.tap_slop_px) {
      tap_cancelled = true;
      long_press_armed = false;
    }
    if (axis_lock == GestureAxisLock::None && cur_dist >= km.axis_lock_px) {
      if (is_axis_dominant(dx, dy)) {
        axis_lock = GestureAxisLock::Horizontal;
      } else if (is_axis_dominant(dy, dx)) {
        axis_lock = GestureAxisLock::Vertical;
      }
    } else if (!swipe_committed && axis_lock != GestureAxisLock::Slider) {
      // Recover from an early touch-down jitter lock (e.g. thumb roll near a curved bezel or mode bar)
      // once displacement overwhelmingly dominates the orthogonal axis (>= 2x swipe_live_px at >= 2:1 ratio).
      const int break_px = km.swipe_live_px * 2;
      if (axis_lock == GestureAxisLock::Vertical && adx >= break_px && adx >= ady * 2) {
        axis_lock = GestureAxisLock::Horizontal;
      } else if (axis_lock == GestureAxisLock::Horizontal && ady >= break_px && ady >= adx * 2) {
        axis_lock = GestureAxisLock::Vertical;
      }
    }
  }
};

inline TouchGestureState &touch_gesture() {
  static TouchGestureState st;
  return st;
}

struct TouchRouter {
  inline bool allow_tap(uint32_t now_ms, uint32_t cooldown_ms = TOUCH_DEBOUNCE_BUTTON,
                        uint32_t target_key = 0) const {
    return touch_allow_tap(now_ms, cooldown_ms, target_key);
  }

  inline void note_tap(uint32_t now_ms, uint32_t target_key = 0) const {
    touch_last_tap_ms() = (now_ms == 0U) ? 1U : now_ms;
    touch_last_target_key() = target_key;
  }

  inline void clear_tap() const {
    touch_last_tap_ms() = 0U;
    touch_last_target_key() = 0U;
  }
};

inline TouchRouter &touch_router() {
  static TouchRouter s_router;
  return s_router;
}

}  // namespace latter_ui
