#pragma once

// ==============================================================================
// Latter Unified Physical-Kinematics Gesture Engine & Target-Scoped Debouncer
// (include/ui_touch_recognizer.h)
// Pure C++20 UI header — zero ESPHome or HAL includes (D-049).
// ==============================================================================

#include "ui_panel_metrics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace latter_ui {

inline constexpr float compute_velocity_mm_per_s(int delta_px, uint32_t dt_ms, int ppi) noexcept {
  if (ppi <= 0) return 0.0f;
  const int abs_px = (delta_px < 0) ? -delta_px : delta_px;
  const uint32_t eff_dt = (dt_ms < 8U) ? 8U : dt_ms;
  const float mm = (static_cast<float>(abs_px) * 25.4f) / static_cast<float>(ppi);
  return (mm * 1000.0f) / static_cast<float>(eff_dt);
}

// Evaluates horizontal swipe direction (+1 = finger moved LEFT -> next page/tab; -1 = finger moved RIGHT -> prev).
inline constexpr int eval_kinematic_horiz_swipe(
    int dx, int dy, uint32_t duration_ms, bool is_release, int ppi,
    GestureAxisLock current_lock = GestureAxisLock::None) noexcept {
  if (current_lock == GestureAxisLock::Slider) {
    return 0;
  }
  const KinematicMetrics m = KinematicMetrics::for_ppi(ppi);
  const int adx = (dx < 0) ? -dx : dx;
  const int ady = (dy < 0) ? -dy : dy;
  if (current_lock == GestureAxisLock::Vertical &&
      !(is_release && adx >= m.swipe_live_px * 2 && adx >= ady * 2)) {
    return 0;
  }
  if (!is_axis_dominant(dx, dy)) return 0;
  if (adx >= m.swipe_live_px) {
    return (dx < 0) ? 1 : -1;
  }
  if (is_release && adx >= m.swipe_flick_px) {
    const float vel = compute_velocity_mm_per_s(adx, duration_ms, m.ppi);
    if (vel >= kinematics::kFlickMinVelMmPerSec) {
      return (dx < 0) ? 1 : -1;
    }
  }
  return 0;
}

// Evaluates vertical swipe direction (+1 = finger moved UP -> next page; -1 = finger moved DOWN -> prev page).
inline constexpr int eval_kinematic_vert_swipe(
    int dx, int dy, uint32_t duration_ms, bool is_release, int ppi,
    GestureAxisLock current_lock = GestureAxisLock::None) noexcept {
  if (current_lock == GestureAxisLock::Slider) {
    return 0;
  }
  const KinematicMetrics m = KinematicMetrics::for_ppi(ppi);
  const int adx = (dx < 0) ? -dx : dx;
  const int ady = (dy < 0) ? -dy : dy;
  if (current_lock == GestureAxisLock::Horizontal &&
      !(is_release && ady >= m.swipe_live_px * 2 && ady >= adx * 2)) {
    return 0;
  }
  if (!is_axis_dominant(dy, dx)) return 0;
  if (ady >= m.swipe_live_px) {
    return (dy < 0) ? 1 : -1;
  }
  if (is_release && ady >= m.swipe_flick_px) {
    const float vel = compute_velocity_mm_per_s(ady, duration_ms, m.ppi);
    if (vel >= kinematics::kFlickMinVelMmPerSec) {
      return (dy < 0) ? 1 : -1;
    }
  }
  return 0;
}

// Tactile rubber-band nudge state when swiping against a clamped page boundary (Home Dock or Vertical Lists).
struct EdgeBounceState {
  GestureAxisLock axis{GestureAxisLock::None};
  int8_t dir{0};  // +1 = tried to advance past max_page (nudge content left/up); -1 = tried before min_page
  int16_t page_id{-1};
  uint32_t start_ms{0};

  void trigger(GestureAxisLock ax, int step_dir, int gui_page, uint32_t now_ms) noexcept {
    axis = ax;
    dir = (step_dir >= 0) ? 1 : -1;
    page_id = static_cast<int16_t>(gui_page);
    start_ms = (now_ms == 0U) ? 1U : now_ms;
  }

  void clear() noexcept {
    axis = GestureAxisLock::None;
    dir = 0;
    page_id = -1;
    start_ms = 0U;
  }

  bool is_active(int gui_page, GestureAxisLock want_axis, uint32_t now_ms) const noexcept {
    if (start_ms == 0U || axis != want_axis || page_id != gui_page) return false;
    return (now_ms >= start_ms) && ((now_ms - start_ms) < kinematics::kEdgeBounceDurationMs);
  }

  int offset_px(int gui_page, GestureAxisLock want_axis, uint32_t now_ms, int ppi) const noexcept {
    if (!is_active(gui_page, want_axis, now_ms)) return 0;
    const float u = static_cast<float>(now_ms - start_ms) /
                    static_cast<float>(kinematics::kEdgeBounceDurationMs);
    const float env = 4.0f * u * (1.0f - u);
    const int peak = KinematicMetrics::for_ppi(ppi).edge_bounce_px;
    const int signed_peak = (dir > 0) ? -peak : peak;
    return static_cast<int>(std::lround(static_cast<float>(signed_peak) * env));
  }
};

inline EdgeBounceState &edge_bounce() noexcept {
  static EdgeBounceState s_bounce{};
  return s_bounce;
}

inline int step_pager_index(int cur_page, int min_page, int max_page, int step_dir, bool wrap,
                            GestureAxisLock axis, int gui_page, uint32_t now_ms,
                            bool &out_changed) noexcept {
  out_changed = false;
  if (max_page < min_page) max_page = min_page;
  const int span = (max_page - min_page) + 1;
  if (span <= 1) {
    if (!wrap) edge_bounce().trigger(axis, step_dir, gui_page, now_ms);
    return min_page;
  }
  const int clamped_cur = std::clamp(cur_page, min_page, max_page);
  int next = clamped_cur + step_dir;
  if (wrap) {
    int rel = (clamped_cur - min_page + step_dir) % span;
    if (rel < 0) rel += span;
    next = min_page + rel;
  } else if (next < min_page || next > max_page) {
    next = std::clamp(next, min_page, max_page);
    if (next == clamped_cur) {
      edge_bounce().trigger(axis, step_dir, gui_page, now_ms);
      return clamped_cur;
    }
  }
  out_changed = (next != clamped_cur);
  return next;
}

// Encodes (page_id, widget_id, slot_param) into a nonzero 32-bit key for target-scoped debouncing.
inline constexpr uint32_t make_tap_target_key(int page_id, uint16_t widget_id, int16_t param = 0) noexcept {
  const uint32_t p = static_cast<uint32_t>(page_id & 0x3F);
  const uint32_t w = static_cast<uint32_t>(widget_id);
  const uint32_t s = static_cast<uint32_t>(static_cast<uint16_t>(param) & 0x00FFU);
  return (1U << 31U) | (p << 24U) | (w << 8U) | s;
}

}  // namespace latter_ui
