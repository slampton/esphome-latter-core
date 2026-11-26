#pragma once

// ==============================================================================
// Latter UI Multi-Geometry Scaling, Physical Kinematics & Safe-Canvas Metrics
// (include/ui_panel_metrics.h)
// Pure C++20 UI leaf (320x480 .. 1280x800, 9-geometry) — zero ESPHome, HAL, or state includes.
// ==============================================================================

#include "ui_panel_traits.h"
#include "ui_scale.h"

#include <algorithm>
#include <cstdint>

namespace latter_ui {

namespace kinematics {
inline constexpr float kSliderStartMm = 0.65f;
inline constexpr float kSwipeFlickMm = 1.55f;
inline constexpr float kTapSlopMm = 1.65f;
inline constexpr float kAxisLockMm = 1.80f;
inline constexpr float kSwipeLiveMm = 2.35f;
inline constexpr float kFlickMinVelMmPerSec = 38.0f;
inline constexpr float kEdgeBouncePeakMm = 1.35f;
inline constexpr uint32_t kEdgeBounceDurationMs = 135U;
inline constexpr uint32_t kLongPressHoldMs = 480U;
}  // namespace kinematics

enum class GestureAxisLock : uint8_t {
  None = 0,
  Horizontal = 1,
  Vertical = 2,
  Slider = 3,
};

struct KinematicMetrics {
  int tap_slop_px{17};
  int axis_lock_px{19};
  int swipe_live_px{25};
  int swipe_flick_px{16};
  int slider_start_px{7};
  int edge_bounce_px{14};
  int ppi{266};

  static constexpr int mm_px(float mm, int panel_ppi) noexcept {
    const int v = static_cast<int>((mm * static_cast<float>(panel_ppi)) / 25.4f + 0.5f);
    return (v > 1) ? v : 1;
  }

  static constexpr KinematicMetrics for_ppi(int panel_ppi) noexcept {
    KinematicMetrics m{};
    m.ppi = (panel_ppi > 50) ? panel_ppi : 266;
    m.slider_start_px = mm_px(kinematics::kSliderStartMm, m.ppi);
    m.swipe_flick_px = mm_px(kinematics::kSwipeFlickMm, m.ppi);
    m.tap_slop_px = mm_px(kinematics::kTapSlopMm, m.ppi);
    if (m.tap_slop_px <= m.slider_start_px) m.tap_slop_px = m.slider_start_px + 2;
    m.axis_lock_px = mm_px(kinematics::kAxisLockMm, m.ppi);
    m.swipe_live_px = mm_px(kinematics::kSwipeLiveMm, m.ppi);
    if (m.swipe_live_px <= m.tap_slop_px + 2) m.swipe_live_px = m.tap_slop_px + 3;
    if (m.swipe_flick_px >= m.swipe_live_px) m.swipe_flick_px = m.swipe_live_px - 2;
    m.edge_bounce_px = mm_px(kinematics::kEdgeBouncePeakMm, m.ppi);
    return m;
  }
};

// Strict 1.25:1 directional dominance check (4 * |major| >= 5 * |minor|) in pure integer math.
inline constexpr bool is_axis_dominant(int major_delta, int minor_delta) noexcept {
  const int maj = (major_delta < 0) ? -major_delta : major_delta;
  const int min = (minor_delta < 0) ? -minor_delta : minor_delta;
  return (maj * 4) >= (min * 5);
}

inline constexpr int safe_clamp(int v, int lo, int hi) noexcept {
  if (lo >= hi) return lo;
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline constexpr int mm_to_px_for(float mm, int ppi) {
  return static_cast<int>((mm * static_cast<float>(ppi)) / 25.4f + 0.5f);
}

// Honest two-tier ergonomic hit-target floors (D-019):
//   - Primary/isolated buttons & full cards: 7.0 mm
//   - Dense paged/scrollable single-line rows: 5.5 mm (guaranteed via stride + Pass-2 halo)
inline constexpr float kHitFloorPrimaryMm = 7.0f;
inline constexpr float kHitFloorDenseRowMm = 5.5f;

inline constexpr int min_row_hit_px_for(int ppi) noexcept {
  return mm_to_px_for(kHitFloorDenseRowMm, ppi);
}

// Canonical container-proportional custom icon half-size tiers (D-051):
inline constexpr int dock_icon_r_for(int dock_r) {
  const int r = (dock_r * 15 + 13) / 27;
  return (r < 8) ? 8 : r;
}

inline constexpr int card_icon_r_for(int card_h) {
  const int r = (card_h * 22 + 50) / 100;
  return (r < 10) ? 10 : r;
}

inline constexpr int hero_icon_r_for(int hero_h, int scale_milli = kUiScaleMilli) {
  const int lo = dp_px_for(13, scale_milli);
  const int hi = dp_px_for(24, scale_milli);
  const int raw = (hero_h * 20 + 50) / 100;
  return (raw < lo) ? lo : ((raw > hi) ? hi : raw);
}

inline constexpr int chord_half_span_constexpr(int r, int dy, bool round_nearest = false) {
  return scale_chord_half_span(r, dy, round_nearest);
}

inline constexpr int default_corner_r_for(int screen_w, bool is_round, bool is_wearable) noexcept {
#if defined(LATTER_CORNER_R) && defined(LATTER_SCREEN_W) && defined(LATTER_ROUND_DISPLAY) && defined(LATTER_IS_WEARABLE)
  if (screen_w == LATTER_SCREEN_W && is_round == (LATTER_ROUND_DISPLAY != 0) &&
      is_wearable == (LATTER_IS_WEARABLE != 0)) {
    return LATTER_CORNER_R;
  }
#endif
  return is_round ? (screen_w / 2) : (is_wearable ? 98 : 0);
}

inline constexpr int safe_span_for(int y, int screen_w, int screen_h, bool is_round,
                                   bool is_wearable = false, int inset = 12,
                                   int corner_r = -1) {
  if (y < 0 || y >= screen_h || screen_w <= 0) return 0;
  const int cr = (corner_r >= 0) ? corner_r : default_corner_r_for(screen_w, is_round, is_wearable);
  const int x_in = bezel_x_inset_for(y, screen_w, screen_h, cr, is_round, inset);
  const int span = screen_w - 2 * x_in;
  return (span > 0) ? span : 0;
}

inline constexpr bool is_inside_bezel_for(int x, int y, int screen_w, int screen_h,
                                          bool is_round, bool is_wearable = false,
                                          int inset = 5, int corner_r = -1) noexcept {
  if (x < inset || x >= screen_w - inset || y < inset || y >= screen_h - inset) return false;
  if (is_round) {
    const int min_d = (screen_w < screen_h) ? screen_w : screen_h;
    const int r = (min_d / 2) - inset;
    if (r <= 0) return false;
    const int dx = x - (screen_w / 2);
    const int dy = y - (screen_h / 2);
    return (dx * dx + dy * dy) <= (r * r);
  }
  const int cr = (corner_r >= 0) ? corner_r : default_corner_r_for(screen_w, false, is_wearable);
  if (cr <= inset) return true;
  int dx = 0;
  if (x < cr) dx = cr - x;
  else if (x >= screen_w - cr) dx = x - (screen_w - 1 - cr);
  int dy = 0;
  if (y < cr) dy = cr - y;
  else if (y >= screen_h - cr) dy = y - (screen_h - 1 - cr);
  if (dx > 0 && dy > 0) {
    const int r_eff = cr - inset;
    return (dx * dx + dy * dy) <= (r_eff * r_eff);
  }
  return true;
}

struct UiPanelLayout {
  int w{466};
  int h{466};
  int cx{233};
  int cy{233};
  bool is_round{true};
  int hdr_y{48};
  int sub_hdr_y{76};
  int back_y{426};
  int home_bar_y{434};
  int dock_y{359};
  int app_dock_y{369};
  int dock_pips_y{404};
  int app_dock_pips_y{408};
  int dock_r{27};
  int celestial_x{100};
  int celestial_y{92};
  int battery_cx{233};
  int battery_y{435};
  int list_card_w{276};
  int list_card_left{95};
  int slot5_stride{63};
  int slot5_h{44};
  int slot5_cy{236};
  int area_card3_stride{76};
  int area_card3_h{64};
  int area_card3_w{316};
  int area_card3_cy{208};
  int tool_card3_stride{100};
  int home_date_y{52};
  int home_clock_y{128};
  int home_weather_y{200};
  int dock_hit_r{41};
  int dock_stride{84};
  int dock_slot_count{4};
  int list_rows{5};
  int peer_list_rows{4};
  int queue_list_rows{3};
  int list_h{48};
  int list_stride{64};
  int peer_list_stride{64};
  int list_top_cy{110};
  int peer_list_top_cy{96};
  int scale_milli{1000};
  int corner_r{233};
  SizeClass size_class{SizeClass::Regular};

  constexpr int slot5_y(int idx_0_to_4) const {
    return slot5_cy + (idx_0_to_4 - 2) * slot5_stride;
  }
  constexpr int list_row_y(int idx) const {
    return list_top_cy + idx * list_stride;
  }
  constexpr int peer_list_row_y(int idx) const {
    return peer_list_top_cy + idx * peer_list_stride;
  }
  constexpr int area_card3_y(int idx_0_to_2) const {
    return area_card3_cy + (idx_0_to_2 - 1) * area_card3_stride;
  }
};

using PanelLayoutMetrics = UiPanelLayout;

inline constexpr bool resolve_is_stationary_for(int w, int h, int ppi, bool is_round,
                                                int is_stationary_override) noexcept {
  if (is_stationary_override >= 0) return is_stationary_override != 0;
#if defined(LATTER_IS_STATIONARY) && defined(LATTER_SCREEN_W) && defined(LATTER_SCREEN_H) && defined(LATTER_PANEL_PPI)
  if (w == LATTER_SCREEN_W && h == LATTER_SCREEN_H && ppi == LATTER_PANEL_PPI) {
    return LATTER_IS_STATIONARY != 0;
  }
#endif
  return !is_round && (w >= 600 && h >= 400);
}

inline constexpr int resolve_view_scale_pct_for(int w, int h, int ppi, bool is_round,
                                                int view_scale_pct, bool is_stationary) noexcept {
  if (view_scale_pct > 0) return effective_view_scale_pct(view_scale_pct, is_stationary);
#if defined(LATTER_VIEW_SCALE_PCT) && defined(LATTER_SCREEN_W) && defined(LATTER_SCREEN_H) && defined(LATTER_PANEL_PPI)
  if (w == LATTER_SCREEN_W && h == LATTER_SCREEN_H && ppi == LATTER_PANEL_PPI) {
    return LATTER_VIEW_SCALE_PCT;
  }
#endif
  if (!is_round && (is_stationary || w >= 600 || h >= 600 || (ppi > 0 && ppi <= 190))) {
    if (w >= 1200) return 150;
    if (w >= 800 && h <= 500) return 135;
    return 145;
  }
  return is_stationary ? 145 : 100;
}

inline constexpr UiPanelLayout compute_panel_layout(int w, int h, bool is_round,
                                                    bool is_wearable, int ppi,
                                                    int corner_r = -1,
                                                    int view_scale_pct = 0,
                                                    int is_stationary_override = -1) {
  UiPanelLayout L{};
  const int cr = (corner_r >= 0) ? corner_r : default_corner_r_for(w, is_round, is_wearable);
  const bool is_stationary = resolve_is_stationary_for(w, h, ppi, is_round, is_stationary_override);
  const int eff_vs = resolve_view_scale_pct_for(w, h, ppi, is_round, view_scale_pct, is_stationary);
  const UiDisplaySpec spec{w, h, ppi, is_round, cr, eff_vs, is_stationary};
  const SafeFrame sf = SafeFrame::for_spec(spec);
  const int s = sf.scale_milli;
  const AutoFontSizes fonts = compute_auto_font_sizes(spec);
  const DockMetrics dm = compute_dock_metrics(spec);

  L.w = w;
  L.h = h;
  L.cx = w / 2;
  L.cy = h / 2;
  L.is_round = is_round;
  L.scale_milli = s;
  L.corner_r = cr;
  L.size_class = sf.size_class;

  // Header & sub-header: derived from corner geometry + real auto font_small height
  if (is_round) {
    L.hdr_y = std::max(28, (h * 103) / 1000);
  } else if (cr >= 84) {
    L.hdr_y = std::max(30, dp_px_for(30, s));
  } else {
    const int by_h = (h * 66) / 1000;
    L.hdr_y = std::clamp(by_h, dp_px_for(26, s), dp_px_for(35, s));
  }
  L.sub_hdr_y = L.hdr_y + std::max(fonts.small + dp_px_for(5, s), dp_px_for(25, s));

  // Dock & bottom chrome (Remote-proportioned solver taking extra radius downward)
  L.back_y = dm.back_y;
  L.home_bar_y = dm.home_bar_y;
  L.dock_y = dm.dock_y;
  L.app_dock_y = dm.app_dock_y;
  L.dock_pips_y = dm.dock_pips_y;
  L.app_dock_pips_y = dm.app_dock_pips_y;
  L.dock_r = dm.dock_r;
  L.dock_hit_r = dm.dock_hit_r;
  L.dock_stride = dm.slot_stride;
  L.dock_slot_count = dm.slot_count;

  // Home Dashboard hero typography, top-right battery, and celestial orb (pure geometry math):
  // - Round panels keep battery centered in the bottom chin and sun/moon in the upper-left circular arc.
  // - Non-round panels tuck top-right battery strictly into the corner curve (cr=0 sharp -> symmetric edge pad;
  //   cr>0 rounded -> exact bezel_x_inset_for at batt_top), push Date below Battery ONLY when horizontal extents
  //   would crowd, and balance the celestial orb midway between the left bezel at celestial_y and Date left edge.
  const int est_date_hw = (fonts.date * 34) / 10;  // "Wed, Nov 25" ~= 6.8x font_date height
  if (is_round) {
    L.home_date_y = std::max(38, (h * 112) / 1000);
    L.home_clock_y = L.home_date_y + (fonts.date / 2) + (fonts.time / 2) + dp_px_for(22, s);
    L.home_weather_y = L.home_clock_y + (fonts.time / 2) + (fonts.date / 2) + dp_px_for(20, s);
    L.celestial_x = (w * 215) / 1000;
    L.celestial_y = (h * 197) / 1000;
    L.battery_cx = L.cx;
    L.battery_y = std::min(h - std::max(20, dp_px_for(24, s)),
                           std::max(L.dock_pips_y + dp_px_for(22, s), (h * 935) / 1000));
  } else {
    const int batt_fh = fonts.micro;
    const int batt_half_h = std::max(6, (batt_fh + 1) / 2);
    const int batt_half_w = std::max(20, (batt_fh * 155) / 100);
    const int pad_edge = std::max(6, dp_px_for(8, s));
    const int batt_top = std::max(pad_edge, (cr * 22) / 100);
    L.battery_y = batt_top + batt_half_h;
    const int b_in = bezel_x_inset_for(batt_top, w, h, cr, false, pad_edge);
    L.battery_cx = w - b_in - batt_half_w;

    const int base_date_y = std::max(36, (h * 104) / 1000);
    const int batt_left = L.battery_cx - batt_half_w;
    const int batt_bot = L.battery_y + batt_half_h;
    const bool batt_crowds_date = (L.cx + est_date_hw + dp_px_for(12, s)) > batt_left;
    if (batt_crowds_date) {
      L.home_date_y = std::max(base_date_y, batt_bot + dp_px_for(14, s) + fonts.date / 2);
      L.home_clock_y = L.home_date_y + (fonts.date / 2) + (fonts.time / 2) + dp_px_for(14, s);
      L.home_weather_y = L.home_clock_y + (fonts.time / 2) + (fonts.date / 2) + dp_px_for(14, s);
    } else {
      L.home_date_y = base_date_y;
      L.home_clock_y = L.home_date_y + (fonts.date / 2) + (fonts.time / 2) + dp_px_for(18, s);
      L.home_weather_y = L.home_clock_y + (fonts.time / 2) + (fonts.date / 2) + dp_px_for(16, s);
    }

    const int min_d = (w < h) ? w : h;
    const int orb_r = std::max(22, (min_d * 32 + 233) / 466);
    L.celestial_y = std::max({dp_px_for(68, s), (h * 162) / 1000, L.home_date_y + dp_px_for(24, s)});
    const int cel_left = bezel_x_inset_for(L.celestial_y, w, h, cr, false, dp_px_for(4, s));
    const int date_left = L.cx - est_date_hw;
    const int cel_mid_x = (cel_left + date_left) / 2;
    const int cel_min_x = std::max(cel_left + orb_r + dp_px_for(4, s), dp_px_for(34, s) + 2);
    const int cel_max_x = std::max(cel_min_x, (w * 155) / 1000);
    L.celestial_x = std::clamp(cel_mid_x, cel_min_x, cel_max_x);
  }

  // Dynamic viewport-solved list geometry (full-height hdr..back, mode-bar hdr..mode_bar, and Queue w/ control strip)
  const int dyn_list_top = L.hdr_y + dp_px_for(24, s);
  const int dyn_list_bot = L.back_y - dp_px_for(24, s);
  const int dyn_list_avail_h = std::max(140, dyn_list_bot - dyn_list_top);
  const int peer_zone_top = L.app_dock_y - L.dock_r - dp_px_for(14, s);
  const int peer_list_bot = peer_zone_top - dp_px_for(8, s);
  const int peer_avail_h = std::max(120, peer_list_bot - dyn_list_top);
  const int q_strip_h = std::clamp(dp_px_for(32, s), 28, 46);
  const int q_top = L.hdr_y + dp_px_for(22, s) + q_strip_h + dp_px_for(6, s);
  const int q_avail_h = std::max(100, peer_list_bot - q_top);

  L.list_h = compute_list_row_h(spec, fonts.small);
  const int max_gap = compute_list_row_max_gap(spec);
  L.list_rows = compute_viewport_list_rows(spec, dyn_list_avail_h, fonts.small, 4);
  L.peer_list_rows = compute_viewport_list_rows(spec, peer_avail_h, fonts.small, 4);
  L.queue_list_rows = compute_viewport_list_rows(spec, q_avail_h, fonts.small, 3);

  const int full_gap = (L.list_rows > 1)
                           ? std::clamp((dyn_list_avail_h - L.list_rows * L.list_h) / (L.list_rows - 1), 4, max_gap)
                           : 0;
  const int peer_gap = (L.peer_list_rows > 1)
                           ? std::clamp((peer_avail_h - L.peer_list_rows * L.list_h) / (L.peer_list_rows - 1), 4, max_gap)
                           : 0;
  L.list_stride = L.list_h + full_gap;
  L.peer_list_stride = L.list_h + peer_gap;

  const int full_block_h = L.list_rows * L.list_h + (L.list_rows - 1) * full_gap;
  const int full_y0 = dyn_list_top + std::max(0, (dyn_list_avail_h - full_block_h) / 2);
  L.list_top_cy = full_y0 + L.list_h / 2;

  const int peer_block_h = L.peer_list_rows * L.list_h + (L.peer_list_rows - 1) * peer_gap;
  const int peer_y0 = dyn_list_top + std::max(0, (peer_avail_h - peer_block_h) / 2);
  L.peer_list_top_cy = peer_y0 + L.list_h / 2;

  // Fixed 5-slot geometry (kept for fixed 3..5 row overlays like Quick Actions with sub_hdr_y)
  const int list_top = L.hdr_y + dp_px_for(28, s);
  const int list_bot = L.back_y - dp_px_for(18, s);
  const int list_avail_h = std::max(160, list_bot - list_top);
  const int target_stride = std::max({dp_px_for(58, s), fonts.small + dp_px_for(30, s),
                                      L.list_h + compute_list_row_gap(spec)});
  L.slot5_stride = std::min(target_stride, list_avail_h / 5);
  if (L.slot5_stride < 36) L.slot5_stride = 36;
  const int target_slot_h = std::max(L.list_h, fonts.small + dp_px_for(18, s));
  L.slot5_h = std::clamp(target_slot_h, 30, std::max(30, L.slot5_stride - std::max(6, dp_px_for(8, s))));
  L.slot5_cy = list_top + list_avail_h / 2;

  const int stack_span_w = sf.band_safe_span(L.slot5_cy, 4 * L.slot5_stride + L.slot5_h, dp_px_for(12, s));
  const int full_dyn_span_w = sf.band_safe_span(full_y0 + full_block_h / 2, full_block_h, dp_px_for(12, s));
  const int peer_dyn_span_w = sf.band_safe_span(peer_y0 + peer_block_h / 2, peer_block_h, dp_px_for(12, s));
  const int min_stack_span_w = std::min({stack_span_w, full_dyn_span_w, peer_dyn_span_w});
  const int chev_reserve = dp_px_for(44, s);
  const int chev_safe_w = (w > 2 * chev_reserve + 120) ? (w - 2 * chev_reserve) : (w - 24);
  const int target_list_w = dp_px_for(is_round ? 304 : 380, s);
  L.list_card_w = std::min({target_list_w, min_stack_span_w, chev_safe_w, sf.max_content_w(460)});
  if (L.list_card_w < 160) L.list_card_w = std::min(w - 16, 160);
  L.list_card_left = L.cx - (L.list_card_w / 2);

  // 3-card Area & Tool stack geometry (strictly >= 7.0 mm hit height when space permits)
  const int min_7mm = mm_to_px_for(kHitFloorPrimaryMm, ppi);
  const int area_top = L.sub_hdr_y + dp_px_for(14, s);
  const int area_bot = (L.app_dock_y - L.dock_r) - dp_px_for(14, s);
  const int area_avail_h = std::max(120, area_bot - area_top);
  L.area_card3_stride = std::min(std::max(dp_px_for(76, s), min_7mm + dp_px_for(8, s)), area_avail_h / 3);
  if (L.area_card3_stride < 44) L.area_card3_stride = 44;
  const int max_area_h = std::max(34, L.area_card3_stride - std::max(6, dp_px_for(10, s)));
  L.area_card3_h = std::clamp(std::max(dp_px_for(62, s), min_7mm), 34, max_area_h);
  L.area_card3_cy = area_top + area_avail_h / 2;
  const int area_safe_w = sf.band_safe_span(L.area_card3_cy, 2 * L.area_card3_stride + L.area_card3_h, dp_px_for(12, s));
  L.area_card3_w = std::min({dp_px_for(is_round ? 326 : 400, s), area_safe_w, chev_safe_w, sf.max_content_w(460)});
  if (L.area_card3_w < 160) L.area_card3_w = std::min(w - 16, 160);

  const int tool_avail_h = std::max(140, (L.back_y - dp_px_for(20, s)) - (L.hdr_y + dp_px_for(26, s)));
  L.tool_card3_stride = std::min(std::max(dp_px_for(96, s), min_7mm + dp_px_for(14, s)), tool_avail_h / 3);
  if (L.tool_card3_stride < 48) L.tool_card3_stride = 48;
  return L;
}

inline constexpr int compute_top_edge_zone_y(int w, int h, bool is_round,
                                             bool is_wearable, int ppi,
                                             int corner_r = -1) noexcept {
  const auto L = compute_panel_layout(w, h, is_round, is_wearable, ppi, corner_r);
  if (is_round) return std::max(dp_px_for(44, L.scale_milli), (h * 116) / 1000);
  return std::min(mm_to_px_for(5.2f, ppi), std::max(16, L.sub_hdr_y - 8));
}

inline constexpr int compute_area_card_div_offset(int ppi, int view_scale_pct = 0,
                                                  bool is_stationary = false) noexcept {
  const int s = compute_ui_scale_milli(ppi, view_scale_pct, is_stationary);
  const int min_hit = std::max(dp_px_for(36, s), mm_to_px_for(kHitFloorPrimaryMm, ppi));
  return std::max(dp_px_for(64, s), min_hit);
}

inline constexpr void compute_vertical_page_pips_x(
    int screen_w, bool is_round, int ppi,
    int &out_mask_x, int &out_chev_x, int &out_hit_x0, int &out_hit_w, int &out_min_hit_h,
    int view_scale_pct = 0, bool is_stationary = false) noexcept {
  const int s = compute_ui_scale_milli(ppi, view_scale_pct, is_stationary);
  if (is_round) {
    out_mask_x = (screen_w * 848) / 1000;
    out_chev_x = (screen_w * 891) / 1000;
  } else {
    const int mask_in = std::clamp(dp_px_for(42, s), 28, std::max(28, screen_w / 6));
    const int chev_in = std::max(14, mask_in / 2);
    out_mask_x = screen_w - mask_in;
    out_chev_x = screen_w - chev_in;
  }
  const int min_hit = mm_to_px_for(kHitFloorPrimaryMm, ppi);
  const int raw_x0 = out_mask_x - dp_px_for(12, s);
  const int raw_w = screen_w - raw_x0;
  out_hit_w = (raw_w >= min_hit) ? raw_w : ((min_hit <= screen_w) ? min_hit : screen_w);
  out_hit_x0 = screen_w - out_hit_w;
  if (out_hit_x0 < 0) out_hit_x0 = 0;
  out_min_hit_h = min_hit;
}

inline constexpr int radius_card_for() noexcept {
  const int r = dp_px(12);
  return (r > 6) ? r : 6;
}

inline constexpr int radius_key_for() noexcept {
  const int r = dp_px(10);
  return (r > 5) ? r : 5;
}

inline constexpr void compute_mode_bar_slot_xs(int vis_count, int w, bool is_round,
                                               int out_xs[kMaxDockSlots],
                                               int h = -1, int ppi = LATTER_PANEL_PPI,
                                               int corner_r = -1,
                                               int view_scale_pct = 0,
                                               bool is_stationary = false) noexcept {
  const int cx = w / 2;
  if (vis_count <= 0) return;
  const int eff_h = (h > 0) ? h : ((w == LATTER_SCREEN_W) ? LATTER_SCREEN_H : w);
  const int eff_cr = (corner_r >= 0)
                         ? corner_r
                         : ((w == LATTER_SCREEN_W && eff_h == LATTER_SCREEN_H)
                                ? LATTER_CORNER_R
                                : (is_round ? (w / 2) : 0));
  const int eff_vs = resolve_view_scale_pct_for(w, eff_h, ppi, is_round, view_scale_pct, is_stationary);
  const DockMetrics dm = compute_dock_metrics(
      UiDisplaySpec{w, eff_h, ppi, is_round, eff_cr, eff_vs, is_stationary});
  const int n = std::clamp(vis_count, 1, kMaxDockSlots);
  const int stride = dm.slot_stride;
  const int total_span = (n - 1) * stride;
  const int start_x = cx - (total_span / 2);
  for (int i = 0; i < n; ++i) {
    out_xs[i] = start_x + i * stride;
  }
}

inline constexpr int band_safe_span_for(
    int cy_band, int h_band, int screen_w, int screen_h, bool is_round,
    bool is_wearable = false, int inset = 14, int corner_r = -1) noexcept {
  const int half_h = (h_band / 2 > 1) ? (h_band / 2) : 1;
  int y_top = cy_band - half_h;
  if (y_top < 0) y_top = 0;
  if (y_top >= screen_h) y_top = screen_h - 1;
  int y_bot = cy_band + half_h;
  if (y_bot < 0) y_bot = 0;
  if (y_bot >= screen_h) y_bot = screen_h - 1;
  const int s_top = safe_span_for(y_top, screen_w, screen_h, is_round, is_wearable, inset, corner_r);
  const int s_bot = safe_span_for(y_bot, screen_w, screen_h, is_round, is_wearable, inset, corner_r);
  return (s_top < s_bot) ? s_top : s_bot;
}

inline constexpr int compute_uniform_stack_w_for(
    int top_cy, int bot_cy, int row_h, int max_w,
    int screen_w, int screen_h, bool is_round, bool is_wearable = false,
    int inset = 14, int corner_r = -1, int scale_milli = kUiScaleMilli) noexcept {
  const int s_top = band_safe_span_for(top_cy, row_h, screen_w, screen_h, is_round, is_wearable, inset, corner_r);
  const int s_bot = band_safe_span_for(bot_cy, row_h, screen_w, screen_h, is_round, is_wearable, inset, corner_r);
  const int safe_w = (s_top < s_bot) ? s_top : s_bot;
  const int col_cap = dp_px_for(460, scale_milli);
  const int eff_max = (max_w > col_cap && screen_w > col_cap) ? col_cap : max_w;
  const int screen_cap = (screen_w > 2 * inset) ? (screen_w - 2 * inset) : screen_w;
  int w = safe_w;
  if (eff_max < w) w = eff_max;
  if (screen_cap < w) w = screen_cap;
  return (w > 64) ? w : 64;
}

}  // namespace latter_ui
