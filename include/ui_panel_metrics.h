#pragma once

// ==============================================================================
// Latter UI Multi-Geometry Scaling, Physical Kinematics & Safe-Canvas Metrics
// (include/ui_panel_metrics.h)
// Pure C++20 L0 leaf (320x480 .. 720x720) — zero ESPHome, HAL, or state includes.
// ==============================================================================

#include "ui_panel_traits.h"

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

inline constexpr int mm_to_px_for(float mm, int ppi) {
  return static_cast<int>((mm * static_cast<float>(ppi)) / 25.4f + 0.5f);
}

inline constexpr int scale_x_for(int v466, int screen_w) {
  return (screen_w > 520) ? ((v466 * screen_w + 233) / 466) : v466;
}

inline constexpr int scale_y_for(int v466, int screen_h) {
  return (screen_h > 520) ? ((v466 * screen_h + 233) / 466) : v466;
}

inline constexpr int scale_s_for(int v466, int screen_min_dim) {
  return (screen_min_dim > 520) ? ((v466 * screen_min_dim + 233) / 466) : v466;
}

inline constexpr int scale_min_for(int v466, int screen_min_dim) {
  return (v466 * screen_min_dim + 233) / 466;
}

inline constexpr int scale_w_for(int v466, int screen_w) {
  return (v466 * screen_w + 233) / 466;
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

inline constexpr int hero_icon_r_for(int hero_h, int screen_min_dim) {
  const int lo = scale_s_for(13, screen_min_dim);
  const int hi = scale_s_for(24, screen_min_dim);
  const int raw = (hero_h * 20 + 50) / 100;
  return (raw < lo) ? lo : ((raw > hi) ? hi : raw);
}

inline constexpr int chord_half_span_constexpr(int r, int dy, bool round_nearest = false) {
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

inline constexpr int default_corner_r_for(int screen_w, bool is_round, bool is_wearable) noexcept {
#if defined(LATTER_CORNER_R) && defined(LATTER_SCREEN_W) && defined(LATTER_ROUND_DISPLAY) && defined(LATTER_IS_WEARABLE)
  if (screen_w == LATTER_SCREEN_W && is_round == (LATTER_ROUND_DISPLAY != 0) &&
      is_wearable == (LATTER_IS_WEARABLE != 0)) {
    return LATTER_CORNER_R;
  }
#endif
  return is_round ? (screen_w / 2) : (is_wearable ? 98 : 20);
}

inline constexpr int safe_span_for(int y, int screen_w, int screen_h, bool is_round,
                                   bool is_wearable = false, int inset = 12,
                                   int corner_r = -1) {
  if (y < 0 || y >= screen_h || screen_w <= 0) return 0;
  if (is_round) {
    const int r = screen_w / 2;
    const int cy = screen_h / 2;
    const int dy = (y >= cy) ? (y - cy) : (cy - y);
    const int hw = chord_half_span_constexpr(r, dy, false) - inset;
    return (hw > 0) ? (2 * hw) : 0;
  }
  int half_w = (screen_w / 2) - inset;
  const int cr = (corner_r >= 0) ? corner_r : default_corner_r_for(screen_w, is_round, is_wearable);
  if (cr > 8) {
    int dy = 0;
    if (y < cr) dy = cr - y;
    else if (y >= screen_h - cr) dy = y - (screen_h - 1 - cr);
    if (dy > 0 && dy < cr) {
      const int dx_corner = chord_half_span_constexpr(cr, dy, false);
      const int corner_half_w = (screen_w / 2) - (cr - dx_corner) - inset;
      if (corner_half_w < half_w) half_w = corner_half_w;
    }
  }
  return (half_w > 0) ? (2 * half_w) : 0;
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
  int tool_card3_h{74};
  int tool_card3_w{316};
  int tool_card3_cy{222};
  int tools_col_off{68};
  int tools_row_stride{105};
  int tools_grid_cy{236};
  int home_date_y{52};
  int home_clock_y{128};
  int home_weather_y{200};
  int drag_start{6};
  int swipe_min{24};
  int tap_max{30};
  int dock_hit_r{43};

  constexpr int slot5_y(int idx_0_to_4) const {
    return slot5_cy + (idx_0_to_4 - 2) * slot5_stride;
  }
  constexpr int area_card3_y(int idx_0_to_2) const {
    return area_card3_cy + (idx_0_to_2 - 1) * area_card3_stride;
  }
  constexpr int tool_card3_y(int idx_0_to_2) const {
    return tool_card3_cy + (idx_0_to_2 - 1) * tool_card3_stride;
  }
  constexpr int tools_row_y(int row_0_to_2) const {
    return tools_grid_cy + (row_0_to_2 - 1) * tools_row_stride;
  }
};

using PanelLayoutMetrics = UiPanelLayout;

inline constexpr UiPanelLayout compute_panel_layout(int w, int h, bool is_round,
                                                    bool is_wearable, int ppi) {
  UiPanelLayout L{};
  L.w = w;
  L.h = h;
  L.cx = w / 2;
  L.cy = h / 2;
  L.is_round = is_round;
  L.hdr_y = is_round ? scale_min_for(48, h) : (is_wearable ? 36 : ((h > 520) ? 48 : 28));
  L.sub_hdr_y = is_round ? scale_min_for(76, h) : (is_wearable ? 64 : ((h > 520) ? 88 : 56));
  L.back_y = is_round ? scale_min_for(426, h) : (h - scale_y_for(28, h));
  L.home_bar_y = is_round ? scale_min_for(434, h) : (h - scale_y_for(11, h));
  L.dock_y = is_round ? scale_min_for(359, h) : (h - scale_y_for(66, h));
  L.app_dock_y = is_round ? scale_min_for(369, h) : (h - scale_y_for(66, h));
  L.dock_pips_y = is_round ? scale_min_for(404, h) : (h - scale_y_for(27, h));
  L.app_dock_pips_y = is_round ? scale_min_for(408, h) : (h - scale_y_for(27, h));
  L.dock_r = (w < 360) ? 24 : ((w > 520) ? 40 : 27);
  L.celestial_x = is_round ? scale_min_for(100, w) : ((w < 380) ? 44 : (is_wearable ? 68 : ((w > 520) ? 118 : 78)));
  L.celestial_y = is_round ? scale_min_for(92, h) : ((w < 380) ? 72 : ((h > 520) ? 118 : 78));
  L.battery_cx = is_round ? L.cx : (is_wearable ? (w - 68) : ((w < 380) ? (w - 36) : ((w > 520) ? (w - 86) : (w - 58))));
  L.battery_y = is_round ? scale_min_for(435, h) : (is_wearable ? 43 : ((w < 380) ? 24 : ((h > 520) ? 42 : 28)));
  L.list_card_w = (w < 380) ? (w - 88) : ((w > 520) ? ((w * 276) / 466) : 276);
  L.list_card_left = L.cx - (L.list_card_w / 2);
  L.slot5_stride = (h > 520) ? ((h * 63) / 466) : 63;
  L.slot5_h = (h > 520) ? ((h * 44) / 466) : 44;
  L.slot5_cy = (h > 520) ? L.cy : 236;
  const bool hi_ppi_compact_rect = (!is_round && h >= 480 && h <= 520 && ppi >= 280);
  L.area_card3_stride = (h > 520) ? ((h * 76) / 466) : (hi_ppi_compact_rect ? (is_wearable ? 92 : 90) : 76);
  L.area_card3_h = (h > 520) ? ((h * 64) / 466) : (hi_ppi_compact_rect ? 76 : 64);
  L.area_card3_w = (w < 380) ? (w - 68) : ((w > 520) ? std::min(480, (w * 316) / 466) : 316);
  L.area_card3_cy = (h > 520) ? ((h * 208) / 466) : (hi_ppi_compact_rect ? (is_wearable ? 230 : 222) : 208);
  L.tool_card3_stride = (h > 520) ? ((h * 100) / 466) : 100;
  L.tool_card3_h = (h > 520) ? ((h * 74) / 466) : 74;
  L.tool_card3_w = (w < 380) ? (w - 36) : ((w > 520) ? std::min(480, (w * 316) / 466) : 316);
  L.tool_card3_cy = (h > 520) ? ((h * 222) / 466) : 222;
  L.tools_col_off = (w < 380) ? 61 : ((w > 520) ? ((w * 74) / 466) : 68);
  L.tools_row_stride = (h > 520) ? ((h * 105) / 466) : 105;
  L.tools_grid_cy = (h > 520) ? L.cy : 236;
  L.home_date_y = (h > 520) ? ((h * 52) / 466) : 52;
  L.home_clock_y = (h > 520) ? ((h * 128) / 466) : 128;
  L.home_weather_y = (h > 520) ? ((h * 200) / 466) : 200;
  const auto km = KinematicMetrics::for_ppi(ppi);
  L.drag_start = km.slider_start_px;
  L.tap_max = km.tap_slop_px;
  L.swipe_min = km.swipe_live_px;
  const int min_dock_hit = mm_to_px_for(3.6f, ppi);
  L.dock_hit_r = ((L.dock_r + 16) > min_dock_hit) ? (L.dock_r + 16) : min_dock_hit;
  return L;
}

inline constexpr int compute_top_edge_zone_y(int w, int h, bool is_round,
                                             bool is_wearable, int ppi) noexcept {
  const int min_d = (w < h) ? w : h;
  if (is_round) return scale_min_for(54, min_d);
  const auto L = compute_panel_layout(w, h, is_round, is_wearable, ppi);
  return std::min(mm_to_px_for(5.2f, ppi), std::max(16, L.sub_hdr_y - 8));
}

inline constexpr int compute_area_card_div_offset(int w, int h, int ppi) noexcept {
  const int min_d = (w < h) ? w : h;
  const int min_hit = std::max(scale_s_for(36, min_d), mm_to_px_for(7.0f, ppi));
  return std::max(scale_x_for(64, w), min_hit);
}

inline constexpr int tools_launcher_item_count_for(bool round_display, bool has_imu) noexcept {
  return 4 + (round_display ? 1 : 0) + (has_imu ? 1 : 0);
}

inline constexpr int tools_launcher_page_count(int item_count) noexcept {
  return (item_count <= 6) ? 1 : ((item_count + 5) / 6);
}

inline constexpr void compute_vertical_page_pips_x(
    int screen_w, bool is_round, int ppi,
    int &out_mask_x, int &out_chev_x, int &out_hit_x0, int &out_hit_w, int &out_min_hit_h) noexcept {
  if (is_round) {
    out_mask_x = scale_w_for(395, screen_w);
    out_chev_x = scale_w_for(415, screen_w);
  } else {
    out_mask_x = (screen_w < 380) ? (screen_w - 30) : ((screen_w > 520) ? (screen_w - 88) : (screen_w - 42));
    out_chev_x = (screen_w < 380) ? (screen_w - 15) : ((screen_w > 520) ? (screen_w - 48) : (screen_w - 21));
  }
  const int min_hit = mm_to_px_for(7.0f, ppi);
  const int raw_x0 = out_mask_x - scale_x_for(12, screen_w);
  const int raw_w = screen_w - raw_x0;
  out_hit_w = (raw_w >= min_hit) ? raw_w : ((min_hit <= screen_w) ? min_hit : screen_w);
  out_hit_x0 = screen_w - out_hit_w;
  if (out_hit_x0 < 0) out_hit_x0 = 0;
  out_min_hit_h = min_hit;
}

static constexpr int kMaxContentColW = 480;

inline constexpr int radius_card_for(int min_dim) noexcept {
  return scale_s_for(12, min_dim);
}

inline constexpr int radius_key_for(int min_dim) noexcept {
  return scale_s_for(10, min_dim);
}

inline constexpr void compute_mode_bar_slot_xs(int vis_count, int w, bool is_round,
                                               int out_xs[4]) noexcept {
  const int cx = w / 2;
  if (vis_count <= 0) return;
  if (vis_count == 1) {
    out_xs[0] = cx;
  } else if (vis_count == 2) {
    out_xs[0] = cx - scale_x_for(44, w);
    out_xs[1] = cx + scale_x_for(44, w);
  } else if (vis_count == 3) {
    out_xs[0] = cx - scale_x_for(84, w);
    out_xs[1] = cx;
    out_xs[2] = cx + scale_x_for(84, w);
  } else if (is_round) {
    out_xs[0] = scale_w_for(112, w);
    out_xs[1] = scale_w_for(193, w);
    out_xs[2] = scale_w_for(274, w);
    out_xs[3] = scale_w_for(355, w);
  } else {
    const int stride = (w < 380) ? 74 : scale_x_for(98, w);
    out_xs[0] = cx - (stride * 3) / 2;
    out_xs[1] = cx - (stride / 2);
    out_xs[2] = cx + (stride / 2);
    out_xs[3] = cx + (stride * 3) / 2;
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
    int inset = 14) noexcept {
  const int s_top = band_safe_span_for(top_cy, row_h, screen_w, screen_h, is_round, is_wearable, inset);
  const int s_bot = band_safe_span_for(bot_cy, row_h, screen_w, screen_h, is_round, is_wearable, inset);
  const int safe_w = (s_top < s_bot) ? s_top : s_bot;
  const int desk_cap = (screen_w > 520 && max_w > kMaxContentColW) ? kMaxContentColW : max_w;
  const int screen_cap = (screen_w > 2 * inset) ? (screen_w - 2 * inset) : screen_w;
  int w = safe_w;
  if (desk_cap < w) w = desk_cap;
  if (screen_cap < w) w = screen_cap;
  return (w > 64) ? w : 64;
}

}  // namespace latter_ui
