#pragma once

// ==============================================================================
// Latter UI Clean-Sheet 3-Layer Scaling, SafeFrame & Auto-Typography Solver
// (include/ui_scale.h)
// Pure C++20 UI leaf — zero ESPHome, HAL, or runtime-state includes.
//
// Layer 1: Physical ergonomics (mm via PPI; KinematicMetrics + 7.0/5.5 mm hit floor)
// Layer 2: Single isotropic dp_px() scale s_ui = max(PPI/266 * view_scale, s_min)
//          + deterministic auto-derived 7-font ramp coupled to real font heights
// Layer 3: Exact radial SDF SafeFrame + logical SizeClass (W_dp x H_dp)
//          + Remote-proportioned dock solver + geometry-driven Activity Card
//          + viewport-solved list rows & tile-grid capacities
// ==============================================================================

#include "ui_panel_traits.h"

#include <algorithm>
#include <cstdint>

namespace latter_ui {

enum class SizeClass : uint8_t {
  Compact = 0,   // W_dp < 400 or H_dp < 400 (Wrist, Remote, Mini Round)
  Regular = 1,   // 400..799 dp wide (Pocket, Mobile, Desk)
  Expanded = 2,  // W_dp >= 800 dp (5"/7"/10" wall & tablet panels; 2-column capable)
};

struct UiDisplaySpec {
  int w{466};
  int h{466};
  int ppi{266};
  bool is_round{true};
  int corner_r{233};
  int view_scale_pct{100};
  bool is_stationary{false};
};

// Pure integer chord half-span sqrt(r^2 - dy^2) (exact floor or nearest).
inline constexpr int scale_chord_half_span(int r, int dy, bool round_nearest = false) noexcept {
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

// Pure radial SDF horizontal inset from either side bezel at scanline y so a horizontal
// span [x_inset, w - 1 - x_inset] clears circular and rounded-rectangle bezels by >= margin px.
inline constexpr int bezel_x_inset_for(int y, int w, int h, int corner_r,
                                       bool is_round, int margin = 10) noexcept {
  if (w <= 0 || h <= 0) return margin;
  const int cy = h / 2;
  if (is_round) {
    const int min_d = (w < h) ? w : h;
    const int r_eff = (min_d / 2) - margin;
    if (r_eff <= 0) return w / 2;
    const int dy = (y >= cy) ? (y - cy) : (cy - y);
    const int hw = scale_chord_half_span(r_eff, dy, false);
    const int inset = (w / 2) - hw;
    return (inset > margin) ? inset : margin;
  }
  const int cr = (corner_r > 0) ? corner_r : 0;
  if (cr > margin) {
    int dy = 0;
    if (y < cr) {
      dy = cr - y;
    } else if (y >= h - cr) {
      dy = y - (h - 1 - cr);
    }
    if (dy > 0) {
      const int r_eff = cr - margin;
      const int dx_corner = (dy < r_eff) ? scale_chord_half_span(r_eff, dy, false) : 0;
      const int corner_inset = cr - dx_corner;
      return (corner_inset > margin) ? corner_inset : margin;
    }
  }
  return margin;
}

inline constexpr int effective_view_scale_pct(int view_scale_pct, bool is_stationary) noexcept {
  if (view_scale_pct > 0) {
    return (view_scale_pct < 75) ? 75 : ((view_scale_pct > 200) ? 200 : view_scale_pct);
  }
  return is_stationary ? 145 : 100;
}

// Isotropic UI scale factor in permille (1000 = 1.000x @ 266 PPI handheld reference).
inline constexpr int compute_ui_scale_milli(int ppi, int view_scale_pct = 0,
                                            bool is_stationary = false) noexcept {
  const int eff_ppi = (ppi > 50) ? ppi : 266;
  const int eff_vs = effective_view_scale_pct(view_scale_pct, is_stationary);
  const int s_min = (is_stationary || eff_vs >= 120) ? 900 : 760;
  const int raw = (eff_ppi * eff_vs * 10 + 133) / 266;
  return (raw > s_min) ? raw : s_min;
}

inline constexpr int dp_px_for(int dp, int scale_milli) noexcept {
  if (dp >= 0) return (dp * scale_milli + 500) / 1000;
  return -((-dp * scale_milli + 500) / 1000);
}

inline constexpr float dp_f_for(float dp, int scale_milli) noexcept {
  return dp * (static_cast<float>(scale_milli) * 0.001f);
}

inline constexpr int px_to_dp_for(int px, int scale_milli) noexcept {
  const int s = (scale_milli > 100) ? scale_milli : 1000;
  return (px * 1000 + s / 2) / s;
}

inline constexpr int mm_ceil_px_for(int mm_x10, int ppi) noexcept {
  const int eff_ppi = (ppi > 50) ? ppi : 266;
  return (mm_x10 * eff_ppi + 253) / 254;
}

inline constexpr UiDisplaySpec current_display_spec() noexcept {
  return UiDisplaySpec{
      LATTER_SCREEN_W,
      LATTER_SCREEN_H,
      LATTER_PANEL_PPI,
      LATTER_ROUND_DISPLAY != 0,
      LATTER_CORNER_R,
      LATTER_VIEW_SCALE_PCT,
      LATTER_IS_STATIONARY != 0,
  };
}

inline constexpr int kUiScaleMilli = compute_ui_scale_milli(
    LATTER_PANEL_PPI, LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);

inline constexpr int dp_px(int dp) noexcept {
  return dp_px_for(dp, kUiScaleMilli);
}

inline constexpr float dp_f(float dp) noexcept {
  return dp_f_for(dp, kUiScaleMilli);
}

inline constexpr SizeClass compute_size_class(const UiDisplaySpec &spec) noexcept {
  const int s = compute_ui_scale_milli(spec.ppi, spec.view_scale_pct, spec.is_stationary);
  const int w_dp = px_to_dp_for(spec.w, s);
  const int h_dp = px_to_dp_for(spec.h, s);
  if (w_dp >= 800) return SizeClass::Expanded;
  if (w_dp < 400 || h_dp < 400) return SizeClass::Compact;
  return SizeClass::Regular;
}

inline constexpr SizeClass current_size_class() noexcept {
  return compute_size_class(current_display_spec());
}

// ==============================================================================
// Auto-Derived 7-Font Typography Ramp (mirrored 1:1 in latter_core/__init__.py)
// ==============================================================================
struct AutoFontSizes {
  int micro{18};
  int small{21};
  int btn_bold{25};
  int date{28};
  int chrono{38};
  int games{38};
  int time{76};
};

inline constexpr AutoFontSizes compute_auto_font_sizes(const UiDisplaySpec &spec) noexcept {
  const int eff_w = (spec.w > 120) ? spec.w : 466;
  const int eff_ppi = (spec.ppi > 50) ? spec.ppi : 266;
  const int eff_vs = effective_view_scale_pct(spec.view_scale_pct, spec.is_stationary);
  const int s = compute_ui_scale_milli(eff_ppi, eff_vs, spec.is_stationary);

  const int raw_micro = dp_px_for(14, s);
  const int floor_micro = (165 * eff_ppi + 2539) / 2540;  // >= 1.65 mm legibility floor across all panels
  const int micro = std::max(12, std::max(floor_micro, raw_micro));

  const int raw_small = dp_px_for(21, s);
  const int floor_small = mm_ceil_px_for(20, eff_ppi);
  const int small = std::max(15, std::max(floor_small, raw_small));

  const int cap_btn = (eff_w * 71) / 1000;
  const int raw_btn = dp_px_for(25, s);
  const int btn_bold = std::max(small + 1, (raw_btn < cap_btn) ? raw_btn : cap_btn);

  const int cap_date = (eff_w * 78 + 20) / 1000;
  const int raw_date = dp_px_for(28, s);
  const int date = std::max(btn_bold + 1, (raw_date < cap_date) ? raw_date : cap_date);

  const int cap_chrono = (eff_w * 108) / 1000;
  const int raw_chrono = dp_px_for(38, s);
  const int chrono = std::max(date + 4, (raw_chrono < cap_chrono) ? raw_chrono : cap_chrono);

  const int clock_pm = (eff_vs >= 120) ? 215 : 185;
  const int cap_time = (eff_w * clock_pm + 200) / 1000;
  const int raw_time = dp_px_for(76, s);
  const int time_px = std::max(chrono + 10, (raw_time < cap_time) ? raw_time : cap_time);

  return AutoFontSizes{micro, small, btn_bold, date, chrono, chrono, time_px};
}

inline constexpr AutoFontSizes kAutoFonts = compute_auto_font_sizes(current_display_spec());

// ==============================================================================
// SafeFrame: Exact Radial SDF Safe-Canvas & Content Column Helper
// ==============================================================================
struct SafeFrame {
  UiDisplaySpec spec{};
  int scale_milli{1000};
  int w_dp{466};
  int h_dp{466};
  SizeClass size_class{SizeClass::Regular};

  static constexpr SafeFrame for_spec(const UiDisplaySpec &s) noexcept {
    SafeFrame f{};
    f.spec = s;
    f.scale_milli = compute_ui_scale_milli(s.ppi, s.view_scale_pct, s.is_stationary);
    f.w_dp = px_to_dp_for(s.w, f.scale_milli);
    f.h_dp = px_to_dp_for(s.h, f.scale_milli);
    f.size_class = compute_size_class(s);
    return f;
  }

  constexpr int dp(int v) const noexcept { return dp_px_for(v, scale_milli); }

  constexpr int x_inset(int y, int margin_px = 10) const noexcept {
    return bezel_x_inset_for(y, spec.w, spec.h, spec.corner_r, spec.is_round, margin_px);
  }

  constexpr int safe_span(int y, int margin_px = 12) const noexcept {
    if (y < 0 || y >= spec.h || spec.w <= 0) return 0;
    const int xi = x_inset(y, margin_px);
    const int span = spec.w - 2 * xi;
    return (span > 0) ? span : 0;
  }

  constexpr int band_safe_span(int cy, int band_h, int margin_px = 12) const noexcept {
    const int half_h = (band_h / 2 > 1) ? (band_h / 2) : 1;
    const int y_top = (cy - half_h < 0) ? 0 : ((cy - half_h >= spec.h) ? (spec.h - 1) : (cy - half_h));
    const int y_bot = (cy + half_h < 0) ? 0 : ((cy + half_h >= spec.h) ? (spec.h - 1) : (cy + half_h));
    const int st = safe_span(y_top, margin_px);
    const int sb = safe_span(y_bot, margin_px);
    return (st < sb) ? st : sb;
  }

  // Max single-column reading/control width (in px): scales cleanly with dp without fixed 480 px cliff.
  constexpr int max_content_w(int max_dp = 460) const noexcept {
    const int by_dp = dp(max_dp);
    const int by_screen = spec.w - 2 * dp(12);
    const int cap = (by_dp < by_screen) ? by_dp : by_screen;
    return (cap > 160) ? cap : 160;
  }
};

// ==============================================================================
// Remote-Proportioned Dynamic Dock Solver (4..6 Slots; Taking Extra Radius Down)
// ==============================================================================
inline constexpr int kMaxDockSlots = 6;
inline constexpr int kMaxListSlots = 8;

struct DockMetrics {
  int slot_count{4};
  int max_slots_capable{4};  // 5 on Desk (720x720), 6 on Expanded landscape tablets
  int dock_r{29};
  int dock_y{368};
  int app_dock_y{372};
  int dock_pips_y{407};
  int app_dock_pips_y{410};
  int home_bar_y{440};
  int back_y{430};
  int dock_hit_r{41};
  int slot_stride{84};
  int slot_xs[kMaxDockSlots]{107, 191, 275, 359, 0, 0};
};

// Exact maximum horizontal half-spread (from cx to outer dock slot center) such that BOTH:
//   (1) the outer dock circle + stadium track (r_track) stays strictly inside the circular/corner bezel, AND
//   (2) the overflow scroll chevron (r + chev_pad + ch_half) stays strictly inside the bezel at dock_y.
inline constexpr int compute_dock_max_outer_dx(const UiDisplaySpec &spec, int scale_milli,
                                               int dock_y, int r) noexcept {
  const int margin = std::max(8, dp_px_for(10, scale_milli));
  const int r_track = r + std::max(2, dp_px_for(3, scale_milli));
  const int chev_pad = std::max(9, dp_px_for(12, scale_milli));
  const int ch_half = std::max(4, dp_px_for(5, scale_milli));
  const int half_w = spec.w / 2;

  const int chev_in = bezel_x_inset_for(dock_y, spec.w, spec.h, spec.corner_r, spec.is_round, margin);
  const int dx_chev = half_w - chev_in - (r + chev_pad + ch_half);

  int dx_circle = half_w - margin - r_track;
  if (spec.is_round) {
    const int min_d = (spec.w < spec.h) ? spec.w : spec.h;
    const int r_avail = (min_d / 2) - margin - r_track;
    const int dy = (dock_y >= spec.h / 2) ? (dock_y - spec.h / 2) : (spec.h / 2 - dock_y);
    dx_circle = (r_avail > dy) ? scale_chord_half_span(r_avail, dy, false) : 0;
  } else {
    const int cr = (spec.corner_r > 0) ? spec.corner_r : 0;
    const int arc_cy = spec.h - 1 - cr;
    if (cr > 0 && dock_y + r_track > arc_cy) {
      if (cr - margin > r_track) {
        const int dy_c = (dock_y > arc_cy) ? (dock_y - arc_cy) : 0;
        const int r_avail = cr - margin - r_track;
        const int dx_c = (r_avail > dy_c) ? scale_chord_half_span(r_avail, dy_c, false) : 0;
        const int corner_dx = (half_w - cr) + dx_c;
        if (corner_dx < dx_circle) dx_circle = corner_dx;
      } else {
        const int diag = (r_track * 707) / 1000;
        const int diag_in = bezel_x_inset_for(dock_y + diag, spec.w, spec.h, cr, false, margin);
        const int corner_dx = half_w - diag_in - diag;
        if (corner_dx < dx_circle) dx_circle = corner_dx;
      }
    }
    const int half_cap = dp_px_for(440, scale_milli) - r;
    if (half_cap < dx_circle) dx_circle = half_cap;
  }

  const int lim = (dx_circle < dx_chev) ? dx_circle : dx_chev;
  return (lim > 0) ? lim : 0;
}

inline constexpr DockMetrics compute_dock_metrics(const UiDisplaySpec &spec) noexcept {
  DockMetrics d{};
  const SafeFrame sf = SafeFrame::for_spec(spec);
  const int s = sf.scale_milli;
  const int cx = spec.w / 2;
  const int min_dp = (sf.w_dp < sf.h_dp) ? sf.w_dp : sf.h_dp;

  // Target dock radius in logical dp scales smoothly from 28 dp (Remote) -> 31 dp (Pocket) -> 34 dp (Desk) -> 42 dp (10" Tablet).
  const int target_r_dp = std::clamp(19 + (min_dp * 28) / 1000, 25, 42);
  int r = dp_px_for(target_r_dp, s);
  const int min_r = 18;
  if (r < min_r) r = min_r;

  const int bot_clearance = std::max(26, dp_px_for(30, s));
  const int round_bar_top = (spec.h * 736) / 1000;  // 343 on 466R, 264 on 360R (zero upward loss)
  const int rect_max_bar_top = (spec.h * 820) / 1000;
  auto app_dock_y_for_r = [&](int cur_r) noexcept -> int {
    if (spec.is_round) return round_bar_top + cur_r;
    const int y = spec.h - bot_clearance - cur_r;
    return (y - cur_r > rect_max_bar_top) ? (rect_max_bar_top + cur_r) : y;
  };

  // Step 1: Universal joint (r, stride) solver ensuring >= 4 slots fit at >= 42.5% diameter gap
  // inside exact circular/corner bezel + overflow chevron tangency.
  int max_dx = 0;
  int max_stride4 = 0;
  while (true) {
    const int cur_dock_y = app_dock_y_for_r(r);
    max_dx = compute_dock_max_outer_dx(spec, s, cur_dock_y, r);
    max_stride4 = (2 * max_dx) / 3;
    const int min_comfort_stride = (r * 285 + 50) / 100;
    if (max_stride4 >= min_comfort_stride || r <= min_r) break;
    --r;
  }

  d.dock_r = r;
  d.app_dock_y = app_dock_y_for_r(r);
  const int desired_stride = (r * 290 + 50) / 100;  // Remote golden proportion (~0.45x diameter gap)
  const int min_gap = std::max(6, dp_px_for(8, s));

  // Step 2: Dynamically grow visible slot count (4 -> 5 -> 6) whenever the same icon radius
  // comfortably fits more slots at >= 40% diameter gap without shrinking icons (e.g. 5 on Desk 720x720, 6 on wide tablets).
  const int min_multi_stride = (r * 280 + 50) / 100;
  const int fit_slots = (min_multi_stride > 0) ? (1 + (2 * max_dx) / min_multi_stride) : 4;
  d.slot_count = std::clamp(fit_slots, 4, kMaxDockSlots);
  d.max_slots_capable = (sf.w_dp >= 800) ? kMaxDockSlots : d.slot_count;

  const int max_stride_n = (d.slot_count > 1) ? ((2 * max_dx) / (d.slot_count - 1)) : max_stride4;
  int stride = (desired_stride < max_stride_n) ? desired_stride : max_stride_n;
  if (stride < 2 * r + min_gap) stride = 2 * r + min_gap;
  d.slot_stride = stride;

  if (spec.is_round) {
    d.dock_y = d.app_dock_y - std::max(2, dp_px_for(4, s));
    d.dock_pips_y = d.dock_y + r + std::max(6, dp_px_for(10, s));
    d.app_dock_pips_y = d.app_dock_y + r + std::max(5, dp_px_for(9, s));
    d.home_bar_y = spec.h - std::max(18, (spec.h * 56) / 1000);
    d.back_y = spec.h - std::max(26, (spec.h * 77) / 1000);
    if (d.app_dock_pips_y >= d.home_bar_y - 6) {
      d.app_dock_pips_y = d.home_bar_y - 6;
    }
  } else {
    d.home_bar_y = spec.h - std::max(9, dp_px_for(11, s));
    d.back_y = spec.h - std::max(24, dp_px_for(28, s));
    d.dock_y = d.app_dock_y;
    d.dock_pips_y = d.dock_y + r + std::max(8, (d.home_bar_y - (d.dock_y + r)) / 2);
    d.app_dock_pips_y = d.dock_pips_y;
  }

  const int total_span = (d.slot_count - 1) * d.slot_stride;
  const int start_x = cx - (total_span / 2);
  for (int i = 0; i < kMaxDockSlots; ++i) {
    d.slot_xs[i] = (i < d.slot_count) ? (start_x + i * d.slot_stride) : 0;
  }

  const int min_dock_hit = static_cast<int>((3.6f * static_cast<float>(spec.ppi)) / 25.4f + 0.5f);
  const int raw_hit = d.dock_r + std::max(10, dp_px_for(12, s));
  d.dock_hit_r = (raw_hit > min_dock_hit) ? raw_hit : min_dock_hit;
  return d;
}

// ==============================================================================
// Pure-Geometry Home Activity Card Capacity (1-line vs 2-line)
// ==============================================================================
struct HomeActivityMetrics {
  int max_rows{1};
  int card_w{360};
  int row1_h{60};
  int row2_h{116};
  int pitch{56};
  int dock_gap{24};
};

inline constexpr HomeActivityMetrics compute_home_activity_metrics(
    const UiDisplaySpec &spec, int home_weather_y, int dock_top_y, int font_small_h,
    int font_date_h) noexcept {
  HomeActivityMetrics m{};
  const SafeFrame sf = SafeFrame::for_spec(spec);
  const int s = sf.scale_milli;
  const int eff_ppi = (spec.ppi > 50) ? spec.ppi : 266;
  const int mm_3 = static_cast<int>((3.0f * static_cast<float>(eff_ppi)) / 25.4f + 0.5f);

  m.dock_gap = std::max(dp_px_for(10, s), std::min(dp_px_for(22, s), mm_3));
  const int row_base = std::max(dp_px_for(54, s), font_small_h + dp_px_for(26, s));
  m.row1_h = spec.is_round ? std::max(dp_px_for(62, s), row_base) : row_base;
  m.pitch = std::max(dp_px_for(50, s), font_small_h + dp_px_for(22, s));
  m.row2_h = m.row1_h + m.pitch;

  const int weather_bot = home_weather_y + (font_date_h / 2) + dp_px_for(10, s);
  const int act_bot = dock_top_y - m.dock_gap;
  const int avail_h = act_bot - weather_bot;
  // Pure geometry decision: 2 lines ONLY when two rows comfortably clear weather + dock without crowding.
  // Automatically yields 1 line on Pocket/Mobile/Wrist/Mini Round and 2 lines on Remote/Desk/Tablets.
  m.max_rows = (!spec.is_round && avail_h >= m.row2_h + dp_px_for(32, s)) ? 2 : 1;

  const int est_cy = spec.is_round ? ((weather_bot + act_bot) / 2) : (act_bot - ((m.max_rows >= 2) ? m.row2_h : m.row1_h) / 2);
  const int est_h = (m.max_rows >= 2) ? m.row2_h : m.row1_h;
  const int safe_w = sf.band_safe_span(est_cy, est_h, dp_px_for(14, s));
  const int max_w = sf.max_content_w(420);
  m.card_w = (safe_w < max_w) ? safe_w : max_w;
  if (m.card_w < 180) m.card_w = std::min(spec.w - 16, 180);
  m.card_w &= ~1;
  return m;
}

// ==============================================================================
// Viewport-Solved List Row Metrics & Capacity (4..8 rows from font, 5.5 mm floor & viewport)
// ==============================================================================
inline constexpr int compute_list_row_h(const UiDisplaySpec &spec, int font_small_h) noexcept {
  const int s = compute_ui_scale_milli(spec.ppi, spec.view_scale_pct, spec.is_stationary);
  const int min_5_5mm = static_cast<int>((5.5f * static_cast<float>((spec.ppi > 50) ? spec.ppi : 266)) / 25.4f + 0.5f);
  const int by_dp = dp_px_for(42, s);
  const int by_font = font_small_h + dp_px_for(18, s);
  const int by_ergo = std::min(min_5_5mm, dp_px_for(44, s));
  int h = (by_dp > by_font) ? by_dp : by_font;
  if (by_ergo > h) h = by_ergo;
  return (h > 30) ? h : 30;
}

inline constexpr int compute_list_row_gap(const UiDisplaySpec &spec) noexcept {
  const int s = compute_ui_scale_milli(spec.ppi, spec.view_scale_pct, spec.is_stationary);
  return std::max(6, dp_px_for(8, s));
}

inline constexpr int compute_viewport_list_rows(const UiDisplaySpec &spec, int avail_h,
                                                int font_small_h) noexcept {
  const int row_h = compute_list_row_h(spec, font_small_h);
  const int gap = compute_list_row_gap(spec);
  const int stride = row_h + gap;
  if (stride <= 0 || avail_h <= 0) return 4;
  const int raw = (avail_h + gap) / stride;
  return std::clamp(raw, 4, kMaxListSlots);
}

}  // namespace latter_ui
