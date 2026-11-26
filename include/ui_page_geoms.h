#pragma once

// ==============================================================================
// Latter UI Pure-C++20 Page & Modal Hit Geometries (include/ui_page_geoms.h)
// Pure C++20 UI leaf (Home Activity, Home Hub, Music v3, Remote D-Pad, Cameras)
// — zero ESPHome, HAL, or runtime-state includes.
// ==============================================================================

#include "ui_hit_table.h"

#include <algorithm>
#include <cstdint>

namespace latter_ui {

// Registers pixel-exact non-overlapping sub-regions for the Home Activity Card
inline void register_home_activity_hit_regions(FrameHitTable &hits, int cx, int card_cy,
                                               int card_w, int card_h,
                                               bool two_rows_rendered, bool has_drop,
                                               int top_slot, int bot_slot,
                                               int act_col_w, int drop_col_w) {
  if (card_w <= 0 || card_h <= 0 || top_slot < 0) return;
  const int card_x0 = cx - (card_w / 2);
  const int card_y0 = card_cy - (card_h / 2);
  const int eff_drop_w = (has_drop && drop_col_w > 0) ? drop_col_w : 0;
  const int eff_act_w = (act_col_w > 0) ? act_col_w : 0;
  const int body_w = (card_w - eff_drop_w - eff_act_w > 1) ? (card_w - eff_drop_w - eff_act_w) : 1;
  const int act_x0 = card_x0 + body_w;
  const int drop_x0 = card_x0 + card_w - eff_drop_w;
  constexpr uint8_t kFlags = kHitTap | kHitFocusSlot;

  if (two_rows_rendered && bot_slot >= 0) {
    const int row0_h = card_h / 2;
    const int row1_y0 = card_y0 + row0_h;
    const int row1_h = card_h - row0_h;
    hits.add_rect(card_x0, card_y0, body_w, row0_h, WidgetId::HOME_ACT_ROW_0, kFlags,
                  static_cast<int16_t>(top_slot));
    if (eff_act_w > 0) {
      hits.add_rect(act_x0, card_y0, eff_act_w, row0_h, WidgetId::HOME_ACT_ACTION_0, kFlags,
                    static_cast<int16_t>(top_slot));
    }
    hits.add_rect(card_x0, row1_y0, body_w, row1_h, WidgetId::HOME_ACT_ROW_1, kFlags,
                  static_cast<int16_t>(bot_slot));
    if (eff_act_w > 0) {
      hits.add_rect(act_x0, row1_y0, eff_act_w, row1_h, WidgetId::HOME_ACT_ACTION_1, kFlags,
                    static_cast<int16_t>(bot_slot));
    }
  } else {
    hits.add_rect(card_x0, card_y0, body_w, card_h, WidgetId::HOME_ACT_ROW_0, kFlags,
                  static_cast<int16_t>(top_slot));
    if (eff_act_w > 0) {
      hits.add_rect(act_x0, card_y0, eff_act_w, card_h, WidgetId::HOME_ACT_ACTION_0, kFlags,
                    static_cast<int16_t>(top_slot));
    }
  }
  if (eff_drop_w > 0) {
    hits.add_rect(drop_x0, card_y0, eff_drop_w, card_h, WidgetId::HOME_ACT_EXPAND, kFlags, 0);
  }
}

struct HomeHubPillGeom {
  int cx{0};
  int cy{0};
  int w{0};
  int h{0};
  int hit_w{0};
  int hit_h{0};
};

inline constexpr int compute_action_pill_row(
    int count, int cy, int pill_h, int max_row_w, int max_btn_w, int gap,
    int screen_w, int screen_h, bool is_round, bool is_wearable, int ppi,
    HomeHubPillGeom *out_geoms, int inset = 14, int corner_r = -1,
    int view_scale_pct = 0, bool is_stationary = false) noexcept {
  if (count <= 0 || out_geoms == nullptr) return 0;
  const int cx = screen_w / 2;
  const int eff_vs = resolve_view_scale_pct_for(screen_w, screen_h, ppi, is_round, view_scale_pct, is_stationary);
  const int s = compute_ui_scale_milli(ppi, eff_vs, is_stationary);
  const int col_cap = dp_px_for(460, s);
  const int min_hit = mm_to_px_for(7.0f, ppi);
  const int eff_h = (pill_h > 24) ? pill_h : 24;
  const int hit_h = (eff_h > min_hit) ? eff_h : min_hit;
  const int safe_w = band_safe_span_for(cy, eff_h, screen_w, screen_h, is_round, is_wearable, inset, corner_r);
  const int desk_cap = (max_row_w > col_cap && screen_w > col_cap) ? col_cap : max_row_w;
  const int avail_w = (desk_cap < safe_w) ? desk_cap : safe_w;
  int eff_gap = (gap > 2) ? gap : 4;
  int bw = (avail_w - (count - 1) * eff_gap) / count;
  if (bw < 48 && count > 1) {
    eff_gap = 4;
    bw = (avail_w - (count - 1) * eff_gap) / count;
  }
  if (bw < 40) bw = 40;
  if (max_btn_w > 0 && bw > max_btn_w) bw = max_btn_w;
  const int total_w = count * bw + (count - 1) * eff_gap;
  const int x0 = cx - total_w / 2;
  const int hit_w = (count == 1 && bw < min_hit) ? ((min_hit < safe_w) ? min_hit : safe_w) : bw;
  for (int i = 0; i < count; ++i) {
    const int bcx = x0 + i * (bw + eff_gap) + bw / 2;
    out_geoms[i] = HomeHubPillGeom{bcx, cy, bw, eff_h, hit_w, hit_h};
  }
  return count;
}

inline constexpr bool home_hub_uses_two_rows(
    int count, int content_top, int content_bot,
    int screen_w, int screen_h, bool is_round, bool is_wearable, int ppi,
    int corner_r = -1, int view_scale_pct = 0, bool is_stationary = false) noexcept {
  if (count > 4) return true;
  if (count < 4) return false;
  const int eff_vs = resolve_view_scale_pct_for(screen_w, screen_h, ppi, is_round, view_scale_pct, is_stationary);
  const int s = compute_ui_scale_milli(ppi, eff_vs, is_stationary);
  const int avail_h = (content_bot - content_top > 140) ? (content_bot - content_top) : 140;
  const int min_hit = mm_to_px_for(7.0f, ppi);
  const int gap = std::max(6, dp_px_for(8, s));
  const int est_row_y = content_bot - avail_h / 4;
  const int est_h = std::max(38, dp_px_for(46, s));
  const int single_safe = band_safe_span_for(est_row_y, est_h, screen_w, screen_h, is_round, is_wearable, 14, corner_r);
  const int thr = (min_hit < 72) ? min_hit : 72;
  return ((single_safe - 3 * gap) / 4) < thr;
}

inline constexpr int compute_home_hub_pill_deck(
    int count, int deck_top, int content_bot,
    int screen_w, int screen_h, bool is_round, bool is_wearable, int ppi,
    bool two_rows, HomeHubPillGeom *out_geoms, int corner_r = -1,
    int view_scale_pct = 0, bool is_stationary = false) noexcept {
  if (count <= 0 || out_geoms == nullptr) return 0;
  const int cx = screen_w / 2;
  const int eff_vs = resolve_view_scale_pct_for(screen_w, screen_h, ppi, is_round, view_scale_pct, is_stationary);
  const int s = compute_ui_scale_milli(ppi, eff_vs, is_stationary);
  const int col_cap = dp_px_for(460, s);
  const int deck_h = (content_bot - deck_top > 40) ? (content_bot - deck_top) : 40;
  const int min_hit = mm_to_px_for(7.0f, ppi);
  const int gap = std::max(6, dp_px_for(8, s));

  if (two_rows && count >= 4) {
    const int row_stride = deck_h / 2;
    int pill_h = row_stride - 5;
    if (pill_h < 30) pill_h = 30;
    const int max_ph = dp_px_for(46, s);
    if (pill_h > max_ph) pill_h = max_ph;
    const int cap_hit_h = (min_hit < row_stride) ? min_hit : row_stride;
    const int hit_h = (pill_h > cap_hit_h) ? pill_h : cap_hit_h;
    const int top_cnt = (count + 1) / 2;
    const int bot_cnt = count - top_cnt;
    const int row_cnts[2] = {top_cnt, bot_cnt};
    int idx = 0;
    for (int r = 0; r < 2; ++r) {
      const int rc = row_cnts[r];
      if (rc <= 0) continue;
      const int row_cy = deck_top + r * row_stride + row_stride / 2;
      const int safe_w = band_safe_span_for(row_cy, pill_h, screen_w, screen_h, is_round, is_wearable, 14, corner_r);
      const int max_rw = std::min(col_cap, dp_px_for(340, s));
      const int row_w = (max_rw < safe_w) ? max_rw : safe_w;
      const int raw_pw = (row_w - (rc - 1) * gap) / rc;
      const int pill_w = (raw_pw > 44) ? raw_pw : 44;
      const int total_w = rc * pill_w + (rc - 1) * gap;
      const int x_left = cx - total_w / 2;
      const int hit_w = (rc == 1 && pill_w < min_hit) ? min_hit : pill_w;
      for (int c = 0; c < rc; ++c) {
        const int pcx = x_left + c * (pill_w + gap) + pill_w / 2;
        out_geoms[idx++] = HomeHubPillGeom{pcx, row_cy, pill_w, pill_h, hit_w, hit_h};
      }
    }
    return idx;
  }

  const int row_cy = deck_top + deck_h / 2;
  int pill_h = deck_h - 8;
  const int min_ph = (count == 1) ? 40 : 36;
  const int max_ph = dp_px_for((count == 1) ? 54 : 50, s);
  if (pill_h < min_ph) pill_h = min_ph;
  if (pill_h > max_ph) pill_h = max_ph;
  const int cap_hit_h = (min_hit < deck_h) ? min_hit : deck_h;
  const int hit_h = (pill_h > cap_hit_h) ? pill_h : cap_hit_h;
  const int safe_w = band_safe_span_for(row_cy, pill_h, screen_w, screen_h, is_round, is_wearable, 12, corner_r);
  if (count == 1) {
    const int max_single_w = dp_px_for(204, s);
    int pill_w = (max_single_w < safe_w) ? max_single_w : safe_w;
    if (pill_w < 64) pill_w = 64;
    const int hit_w = (pill_w > min_hit) ? pill_w : ((min_hit < safe_w) ? min_hit : pill_w);
    out_geoms[0] = HomeHubPillGeom{cx, row_cy, pill_w, pill_h, hit_w, hit_h};
    return 1;
  }

  const int max_rw = std::min(col_cap, dp_px_for(344, s));
  const int row_w = (max_rw < safe_w) ? max_rw : safe_w;
  const int raw_pw = (row_w - (count - 1) * gap) / count;
  const int pill_w = (raw_pw > 44) ? raw_pw : 44;
  const int total_w = count * pill_w + (count - 1) * gap;
  const int x_left = cx - total_w / 2;
  for (int i = 0; i < count; ++i) {
    const int pcx = x_left + i * (pill_w + gap) + pill_w / 2;
    out_geoms[i] = HomeHubPillGeom{pcx, row_cy, pill_w, pill_h, pill_w, hit_h};
  }
  return count;
}

inline void register_home_hub_pill_deck_hits(
    FrameHitTable &hits, uint16_t base_widget_id,
    const int *codes, const HomeHubPillGeom *geoms, int count) {
  if (codes == nullptr || geoms == nullptr || count <= 0) return;
  for (int i = 0; i < count; ++i) {
    const int hw = (geoms[i].hit_w > geoms[i].w) ? geoms[i].hit_w : geoms[i].w;
    const int hh = (geoms[i].hit_h > geoms[i].h) ? geoms[i].hit_h : geoms[i].h;
    hits.add_centered_rect(geoms[i].cx, geoms[i].cy, hw, hh,
                           static_cast<uint16_t>(base_widget_id + codes[i]),
                           kHitTap | kHitFocusSlot, static_cast<int16_t>(codes[i]));
  }
}

inline void register_thermostat_stepper_hits(
    FrameHitTable &hits, bool show_setpoint_btns,
    int minus_cx, int plus_cx, int hero_cy, int btn_r, int btn_hit_r) {
  if (!show_setpoint_btns || btn_r <= 0) return;
  const int pad = (btn_hit_r > btn_r) ? (btn_hit_r - btn_r) : 0;
  hits.add_circle(minus_cx, hero_cy, btn_r, WidgetId::CLIMATE_MINUS_BTN,
                  kHitTap | kHitFocusCircle, -1, pad);
  hits.add_circle(plus_cx, hero_cy, btn_r, WidgetId::CLIMATE_PLUS_BTN,
                  kHitTap | kHitFocusCircle, 1, pad);
}

// ==============================================================================
// Music App v3 Zero-Container Geometries (D-043 / D-048 / D-059 / D-072)
// ==============================================================================

inline void register_music_modal_backdrop_hits(
    FrameHitTable &ht, int w, int h, int bounds_x0, int bounds_y0, int bounds_w, int bounds_h) {
  const int x0 = std::clamp(bounds_x0, 0, w);
  const int y0 = std::clamp(bounds_y0, 0, h);
  const int x1 = std::clamp(bounds_x0 + bounds_w, x0, w);
  const int y1 = std::clamp(bounds_y0 + bounds_h, y0, h);
  if (y0 > 0) {
    ht.add_rect(0, 0, w, y0, WidgetId::MUSIC_TAKEOVER_BACKDROP, kHitTap, 0);
  }
  if (y1 < h) {
    ht.add_rect(0, y1, w, h - y1, WidgetId::MUSIC_TAKEOVER_BACKDROP, kHitTap, 1);
  }
  if (x0 > 0 && y1 > y0) {
    ht.add_rect(0, y0, x0, y1 - y0, WidgetId::MUSIC_TAKEOVER_BACKDROP, kHitTap, 2);
  }
  if (x1 < w && y1 > y0) {
    ht.add_rect(x1, y0, w - x1, y1 - y0, WidgetId::MUSIC_TAKEOVER_BACKDROP, kHitTap, 3);
  }
}

struct MusicVolumeGeom {
  int w{466};
  int h{466};
  int cx{233};
  int min_hit{73};
  int hdr_title_y{44};
  int hdr_pct_y{66};
  int hdr_max_w{258};
  int mode_bar_top{328};
  int slider_x0{325};
  int slider_y0{85};
  int slider_w{73};
  int slider_h{231};
  int btn_x0{68};
  int btn_w{243};
  int btn_h{73};
  int btn_vis_h{48};
  int up_y0{85};
  int down_y0{164};
  int mute_y0{243};
};

inline constexpr MusicVolumeGeom compute_music_volume_geom(
    int w, int h, bool round, bool wearable, int ppi) noexcept {
  MusicVolumeGeom g{};
  const auto ly = compute_panel_layout(w, h, round, wearable, ppi);
  const int s = ly.scale_milli;
  g.w = w;
  g.h = h;
  g.cx = ly.cx;
  g.min_hit = mm_to_px_for(7.0f, ppi);
  g.mode_bar_top = ly.app_dock_y - ly.dock_r - dp_px_for(14, s);

  g.hdr_title_y = std::max(18, ly.hdr_y - dp_px_for(4, s));
  g.hdr_pct_y = g.hdr_title_y + dp_px_for(23, s);
  g.hdr_max_w = std::max(120, safe_span_for(g.hdr_title_y, w, h, round, wearable, 18, ly.corner_r));

  const int top_limit = ly.hdr_y + dp_px_for(36, s);
  const int bot_limit = g.mode_bar_top - dp_px_for(10, s);
  const int avail_h = std::max(3 * g.min_hit + 12, bot_limit - top_limit);
  const int gap_y = std::clamp((avail_h - 3 * g.min_hit) / 4, 6, dp_px_for(14, s));
  const int max_btn_h = std::max(g.min_hit, dp_px_for(68, s));
  g.btn_h = std::clamp((avail_h - 2 * gap_y) / 3, g.min_hit, max_btn_h);
  g.btn_vis_h = std::clamp(dp_px_for(48, s), 34, g.btn_h);

  const int stack_h = 3 * g.btn_h + 2 * gap_y;
  const int body_y0 = top_limit + std::max(0, (bot_limit - top_limit - stack_h) / 2);
  g.slider_y0 = body_y0;
  g.slider_h = stack_h;
  g.up_y0 = body_y0;
  g.down_y0 = g.up_y0 + g.btn_h + gap_y;
  g.mute_y0 = g.down_y0 + g.btn_h + gap_y;

  const int body_cy = g.slider_y0 + g.slider_h / 2;
  const int safe_w = band_safe_span_for(body_cy, g.slider_h, w, h, round, wearable, 14, ly.corner_r);
  const int max_vol_w = dp_px_for(360, s);
  const int span_w = std::clamp(std::min(max_vol_w, safe_w), 200, w - 20);
  const int span_x0 = g.cx - span_w / 2;
  const int col_gap = std::max(10, dp_px_for(14, s));
  g.slider_w = std::max(dp_px_for(68, s), g.min_hit);
  g.slider_x0 = span_x0 + span_w - g.slider_w;
  g.btn_x0 = span_x0;
  g.btn_w = std::max(g.min_hit, g.slider_x0 - col_gap - g.btn_x0);
  return g;
}

inline void register_music_volume_hits(FrameHitTable &ht, const MusicVolumeGeom &g) {
  ht.add_rect(g.slider_x0, g.slider_y0, g.slider_w, g.slider_h,
              WidgetId::MUSIC_VOL_SLIDER, kHitTap | kHitVertSlider | kHitFocusSlot);
  ht.add_rect(g.btn_x0, g.up_y0, g.btn_w, g.btn_h,
              WidgetId::MUSIC_VOL_UP_STEP, kHitTap | kHitFocusSlot);
  ht.add_rect(g.btn_x0, g.down_y0, g.btn_w, g.btn_h,
              WidgetId::MUSIC_VOL_DOWN_STEP, kHitTap | kHitFocusSlot);
  ht.add_rect(g.btn_x0, g.mute_y0, g.btn_w, g.btn_h,
              WidgetId::MUSIC_VOL_MUTE_BTN, kHitTap | kHitFocusSlot);
}

struct MusicScrubGeom {
  int w{466};
  int h{466};
  int cx{233};
  int min_hit{73};
  int bounds_x0{53};
  int bounds_y0{127};
  int bounds_w{360};
  int bounds_h{235};
  int hdr_title_y{52};
  int hdr_time_y{76};
  int hdr_max_w{272};
  int slider_x0{53};
  int slider_y0{127};
  int slider_w{360};
  int slider_h{73};
  int step_y0{208};
  int step_h{73};
  int step_vis_h{44};
  int step_back_x0{53};
  int step_fwd_x0{237};
  int step_btn_w{176};
  int mode_y0{289};
  int mode_h{73};
  int mode_vis_h{44};
  int shuf_x0{53};
  int rep_x0{176};
  int fav_x0{299};
  int mode_btn_w{114};
};

inline constexpr MusicScrubGeom compute_music_scrub_geom(
    int w, int h, bool round, bool wearable, int ppi) noexcept {
  MusicScrubGeom g{};
  const auto ly = compute_panel_layout(w, h, round, wearable, ppi);
  const int s = ly.scale_milli;
  g.w = w;
  g.h = h;
  g.cx = ly.cx;
  g.min_hit = mm_to_px_for(7.0f, ppi);

  g.hdr_title_y = ly.hdr_y;
  g.hdr_time_y = g.hdr_title_y + dp_px_for(24, s);
  g.hdr_max_w = std::max(120, safe_span_for(g.hdr_title_y, w, h, round, wearable, 18, ly.corner_r));

  const int gap_y = std::max(6, dp_px_for(8, s));
  const int gap_x = std::max(6, dp_px_for(8, s));
  const int row_h = std::max(g.min_hit, dp_px_for(64, s));
  const int body_h = 3 * row_h + 2 * gap_y;
  const int min_top = g.hdr_time_y + dp_px_for(22, s);
  const int max_top = std::max(min_top, h - body_h - dp_px_for(24, s));
  const int centered_top = (h - body_h) / 2 + dp_px_for(12, s);
  const int body_y0 = std::clamp(centered_top, min_top, max_top);

  const int min_span_w = 3 * g.min_hit + 2 * gap_x;
  const int safe_w = band_safe_span_for(body_y0 + body_h / 2, body_h, w, h, round, wearable, 14, ly.corner_r);
  const int max_scrub_w = dp_px_for(376, s);
  const int span_w = std::clamp(
      std::max(min_span_w, std::min(max_scrub_w, safe_w)),
      min_span_w, std::max(min_span_w, w - 16));
  const int span_x0 = g.cx - span_w / 2;

  g.slider_x0 = span_x0;
  g.slider_y0 = body_y0;
  g.slider_w = span_w;
  g.slider_h = row_h;

  g.step_y0 = g.slider_y0 + row_h + gap_y;
  g.step_h = row_h;
  g.step_vis_h = std::clamp(dp_px_for(44, s), 34, row_h);
  g.step_btn_w = std::max(g.min_hit, (span_w - gap_x) / 2);
  g.step_back_x0 = span_x0;
  g.step_fwd_x0 = span_x0 + span_w - g.step_btn_w;

  g.mode_y0 = g.step_y0 + row_h + gap_y;
  g.mode_h = row_h;
  g.mode_vis_h = std::clamp(dp_px_for(44, s), 34, row_h);
  g.mode_btn_w = std::max(g.min_hit, (span_w - 2 * gap_x) / 3);
  g.shuf_x0 = span_x0;
  g.rep_x0 = span_x0 + (span_w - g.mode_btn_w) / 2;
  g.fav_x0 = span_x0 + span_w - g.mode_btn_w;

  g.bounds_x0 = span_x0;
  g.bounds_y0 = g.slider_y0;
  g.bounds_w = span_w;
  g.bounds_h = (g.mode_y0 + g.mode_h) - g.slider_y0;
  return g;
}

inline void register_music_scrub_hits(
    FrameHitTable &ht, const MusicScrubGeom &g, bool can_shuffle = true, bool can_repeat = true) {
  register_music_modal_backdrop_hits(ht, g.w, g.h, g.bounds_x0, g.bounds_y0, g.bounds_w, g.bounds_h);
  ht.add_rect(g.slider_x0, g.slider_y0, g.slider_w, g.slider_h,
              WidgetId::MUSIC_SCRUB_BAR, kHitTap | kHitHorizSlider | kHitFocusSlot);
  ht.add_rect(g.step_back_x0, g.step_y0, g.step_btn_w, g.step_h,
              WidgetId::MUSIC_SCRUB_STEP_BACK, kHitTap | kHitFocusSlot);
  ht.add_rect(g.step_fwd_x0, g.step_y0, g.step_btn_w, g.step_h,
              WidgetId::MUSIC_SCRUB_STEP_FWD, kHitTap | kHitFocusSlot);
  if (can_shuffle) {
    ht.add_rect(g.shuf_x0, g.mode_y0, g.mode_btn_w, g.mode_h,
                WidgetId::MUSIC_SHUFFLE_BTN, kHitTap | kHitFocusSlot);
  }
  if (can_repeat) {
    ht.add_rect(g.rep_x0, g.mode_y0, g.mode_btn_w, g.mode_h,
                WidgetId::MUSIC_REPEAT_BTN, kHitTap | kHitFocusSlot);
  }
  ht.add_rect(g.fav_x0, g.mode_y0, g.mode_btn_w, g.mode_h,
              WidgetId::MUSIC_FAVORITE_BTN, kHitTap | kHitFocusSlot);
}

struct MusicActionSheetGeom {
  int w{466};
  int h{466};
  int cx{233};
  int min_hit{73};
  int hdr_title_y{48};
  int hdr_max_w{258};
  int bounds_y0{72};
  int back_y{426};
  int back_hit_y0{404};
  int item_x0[5]{};
  int item_y0[5]{};
  int item_w[5]{};
  int item_h[5]{};
  int item_hit_pad[5]{};
  int item_vis_h{46};
  int item_count{5};
};

inline constexpr MusicActionSheetGeom compute_music_action_sheet_geom(
    int w, int h, bool round, bool wearable, int ppi, int count = 5) noexcept {
  MusicActionSheetGeom g{};
  const auto ly = compute_panel_layout(w, h, round, wearable, ppi);
  g.w = w;
  g.h = h;
  g.cx = ly.cx;
  g.min_hit = mm_to_px_for(7.0f, ppi);
  g.item_count = std::clamp(count, 1, 5);

  g.hdr_title_y = ly.hdr_y;
  g.hdr_max_w = std::max(120, safe_span_for(g.hdr_title_y, w, h, round, wearable, 18, ly.corner_r));
  g.back_y = ly.back_y;
  g.back_hit_y0 = std::max(0, ly.back_y - dp_px_for(24, ly.scale_milli));

  const int row_h = ly.slot5_h;
  const int uniform_w = ly.list_card_w;
  const int uniform_x0 = g.cx - uniform_w / 2;
  g.item_vis_h = row_h;
  g.bounds_y0 = ly.slot5_y(0) - row_h / 2;

  const int max_gap_pad = std::max(0, (ly.slot5_stride - row_h - 1) / 2);
  for (int i = 0; i < g.item_count; ++i) {
    const int cy = ly.slot5_y(i);
    g.item_y0[i] = cy - row_h / 2;
    g.item_h[i] = row_h;
    g.item_hit_pad[i] = (g.min_hit > row_h) ? std::min(max_gap_pad, (g.min_hit - row_h + 1) / 2) : 0;
    g.item_w[i] = uniform_w;
    g.item_x0[i] = uniform_x0;
  }
  return g;
}

inline void register_music_action_sheet_hits(
    FrameHitTable &ht, const MusicActionSheetGeom &g, const uint16_t *wids) {
  if (g.bounds_y0 > 0) {
    ht.add_rect(0, 0, g.w, g.bounds_y0, WidgetId::MUSIC_TAKEOVER_BACKDROP, kHitTap, 0);
  }
  for (int i = 0; i < g.item_count; ++i) {
    ht.add_rect(g.item_x0[i], g.item_y0[i], g.item_w[i], g.item_h[i],
                wids[i], kHitTap | kHitFocusSlot, static_cast<int16_t>(i), g.item_hit_pad[i]);
  }
}

struct RemoteDpadGeom {
  int cx{233};
  int cy{233};
  int r_in{50};
  int r_out{144};
  int ok_r{42};
  int arr_mid{97};
  int hit_radial{84};
  int hit_tangent{70};
  int outer_pad{10};
};

inline constexpr RemoteDpadGeom compute_remote_dpad_geom(
    int w, int h, bool round, int ppi = LATTER_PANEL_PPI,
    int corner_r = -1, int view_scale_pct = 0, bool is_stationary = false) noexcept {
  RemoteDpadGeom g{};
  const auto ly = compute_panel_layout(w, h, round, false, ppi, corner_r, view_scale_pct, is_stationary);
  const int s = ly.scale_milli;
  const int min_s = (w < h) ? w : h;
  g.cx = w / 2;
  g.outer_pad = std::max(8, dp_px_for(10, s));
  if (round) {
    g.cy = h / 2;
    g.r_out = std::clamp((min_s * 31) / 100, 80, 260);
  } else {
    const int top_bound = ly.sub_hdr_y + dp_px_for(12, s);
    const int bot_bound = h - dp_px_for(22, s);
    g.cy = (top_bound + bot_bound) / 2;
    const int max_r_v = ((bot_bound - top_bound) / 2) - g.outer_pad;
    const int raw_r = std::min((min_s * 38) / 100, max_r_v);
    g.r_out = std::clamp(raw_r, 80, 260);
  }
  g.r_in = std::clamp((g.r_out * 35 + 50) / 100, 34, 96);
  g.ok_r = std::max(26, g.r_in - dp_px_for(8, s));
  g.arr_mid = (g.r_in + g.r_out) / 2;
  const int ring_t = g.r_out - g.r_in;
  g.hit_radial = (ring_t > 20) ? (ring_t - 10) : ring_t;
  g.hit_tangent = (g.r_in * 14) / 10;
  return g;
}

inline constexpr uint16_t resolve_remote_dpad_widget(int tx, int ty, const RemoteDpadGeom &g) noexcept {
  const int dx = tx - g.cx;
  const int dy = ty - g.cy;
  const int d2 = dx * dx + dy * dy;
  const int r_min = g.r_in;
  const int r_max = g.r_out + g.outer_pad;
  if (d2 < r_min * r_min || d2 > r_max * r_max) return WidgetId::NONE;
  const int adx = (dx < 0) ? -dx : dx;
  const int ady = (dy < 0) ? -dy : dy;
  if (adx >= ady) {
    return (dx < 0) ? WidgetId::REMOTE_DPAD_LEFT : WidgetId::REMOTE_DPAD_RIGHT;
  }
  return (dy < 0) ? WidgetId::REMOTE_DPAD_UP : WidgetId::REMOTE_DPAD_DOWN;
}

// ==============================================================================
// Dedicated Cameras App Layout & Uncropped Video Geometry (D-066)
// ==============================================================================
struct CameraRect {
  int x0{0};
  int y0{0};
  int w{0};
  int h{0};
  constexpr int x1() const noexcept { return x0 + w - 1; }
  constexpr int y1() const noexcept { return y0 + h - 1; }
  constexpr int cx() const noexcept { return x0 + w / 2; }
  constexpr int cy() const noexcept { return y0 + h / 2; }
};

struct CameraGridGeom {
  CameraRect frame{};
  CameraRect tiles[4]{};
  int title_y{34};
  int pager_y{34};
  int mode_bar_cy{398};
  int mode_bar_r{28};
  int mode_slot_xs[kMaxDockSlots]{143, 203, 263, 323, 0, 0};
};

inline constexpr CameraRect compute_video_display_rect(
    const CameraRect &bounds, int src_w, int src_h, bool fill) noexcept {
  if (bounds.w <= 0 || bounds.h <= 0) return bounds;
  if (src_w <= 0 || src_h <= 0) return bounds;
  if (fill) return bounds;
  const int64_t lhs = static_cast<int64_t>(bounds.w) * static_cast<int64_t>(src_h);
  const int64_t rhs = static_cast<int64_t>(bounds.h) * static_cast<int64_t>(src_w);
  int dst_w = bounds.w;
  int dst_h = bounds.h;
  if (lhs > rhs) {
    dst_w = static_cast<int>((rhs + (src_h / 2)) / src_h);
  } else {
    dst_h = static_cast<int>((lhs + (src_w / 2)) / src_w);
  }
  if (dst_w < 1) dst_w = 1;
  if (dst_h < 1) dst_h = 1;
  CameraRect r{};
  r.w = dst_w;
  r.h = dst_h;
  r.x0 = bounds.x0 + (bounds.w - dst_w) / 2;
  r.y0 = bounds.y0 + (bounds.h - dst_h) / 2;
  return r;
}

inline constexpr CameraRect compute_uncropped_video_viewport(
    int panel_w, int panel_h, bool round, bool fill) noexcept {
  if (round && !fill) {
    const int r_safe = (panel_w < panel_h ? panel_w : panel_h) / 2 - 8;
    int w = (r_safe * 171) / 100;
    if (w > panel_w - 16) w = panel_w - 16;
    w = (w > 16) ? (w & ~15) : 16;
    int h = (w * 9) / 16;
    h = (h > 16) ? ((h + 15) & ~15) : 16;
    if (h > panel_h - 32) h = panel_h - 32;
    return CameraRect{(panel_w - w) / 2, (panel_h - h) / 2, w, h};
  }
  return CameraRect{0, 0, panel_w, panel_h};
}

inline constexpr CameraGridGeom compute_camera_grid_geom(
    int w, int h, bool round, bool wearable, int ppi) noexcept {
  CameraGridGeom g{};
  const auto ly = compute_panel_layout(w, h, round, wearable, ppi);
  const int cx = w / 2;
  g.title_y = ly.hdr_y;
  g.mode_bar_cy = ly.app_dock_y;
  g.mode_bar_r = ly.dock_r;
  compute_mode_bar_slot_xs(4, w, round, g.mode_slot_xs);

  const int s = ly.scale_milli;
  const int top_y = ly.hdr_y + dp_px_for(24, s);
  const int bot_y = ly.app_dock_y - ly.dock_r - dp_px_for(12, s);
  int avail_h = std::max(80, bot_y - top_y);
  int avail_w = w - dp_px_for(20, s);
  if (round) {
    const int s_top = safe_span_for(top_y, w, h, true, wearable, 14, ly.corner_r);
    const int s_bot = safe_span_for(bot_y, w, h, true, wearable, 14, ly.corner_r);
    const int chord = std::min(s_top, s_bot);
    avail_w = std::clamp(chord, 160, w - 36);
    if (avail_h > avail_w) avail_h = avail_w;
  }
  const int fx = cx - avail_w / 2;
  const int fy = top_y + (bot_y - top_y - avail_h) / 2;
  g.frame = CameraRect{fx, fy, avail_w, avail_h};

  const int half_w = avail_w / 2;
  const int half_h = avail_h / 2;
  g.tiles[0] = CameraRect{fx, fy, half_w, half_h};
  g.tiles[1] = CameraRect{fx + half_w, fy, avail_w - half_w, half_h};
  g.tiles[2] = CameraRect{fx, fy + half_h, half_w, avail_h - half_h};
  g.tiles[3] = CameraRect{fx + half_w, fy + half_h, avail_w - half_w, avail_h - half_h};
  g.pager_y = g.title_y;
  return g;
}

struct WeatherForecastGeom {
  int fc_top_y{88};
  int fc_step_y{47};
  int row_hw{154};
  int x_day{113};
  int x_glyph{171};
  int x_cond{193};
  int x_slash{353};
  int slash_gap{8};
  int cond_max_w{110};
  int temp_half_w{42};
  bool large_font{false};
};

inline constexpr WeatherForecastGeom compute_weather_forecast_geom(
    int w, int h, bool round, int ppi, int corner_r = -1,
    int view_scale_pct = 0, bool is_stationary = false) noexcept {
  WeatherForecastGeom g{};
  const auto ly = compute_panel_layout(w, h, round, false, ppi, corner_r, view_scale_pct, is_stationary);
  const int s = ly.scale_milli;
  const int cx = w / 2;
  const int content_top = ly.hdr_y + dp_px_for(24, s);
  const int content_bot = (ly.app_dock_y - ly.dock_r - dp_px_for(14, s)) - dp_px_for(10, s);
  const int avail_h = std::max(120, content_bot - content_top);
  g.fc_step_y = std::clamp(avail_h / 5, dp_px_for(34, s), dp_px_for(62, s));
  g.fc_top_y = content_top + (avail_h - 4 * g.fc_step_y) / 2;
  const int bot_row_y = g.fc_top_y + 4 * g.fc_step_y;

  const int eff_cr = (corner_r >= 0) ? corner_r : ly.corner_r;
  const int margin = dp_px_for(14, s);
  const int span_top = safe_span_for(g.fc_top_y, w, h, round, false, margin, eff_cr);
  const int span_bot = safe_span_for(bot_row_y, w, h, round, false, margin, eff_cr);
  const int max_col = dp_px_for(400, s);
  const int row_w = std::max(180, std::min({max_col, span_top, span_bot}));
  g.row_hw = row_w / 2;

  const int left = cx - g.row_hw;
  const int right = cx + g.row_hw;
  const int day_w = std::min(dp_px_for(52, s), (row_w * 20) / 100);
  const int glyph_w = std::min(dp_px_for(28, s), (row_w * 12) / 100);
  g.temp_half_w = std::min(dp_px_for(42, s), (row_w * 15) / 100);
  g.slash_gap = std::max(5, dp_px_for(8, s));

  g.x_day = left + day_w / 2;
  g.x_glyph = left + day_w + glyph_w / 2;
  g.x_cond = left + day_w + glyph_w + dp_px_for(6, s);
  g.x_slash = right - g.temp_half_w - g.slash_gap;
  const int cond_right = g.x_slash - g.slash_gap - g.temp_half_w - dp_px_for(4, s);
  g.cond_max_w = std::max(28, cond_right - g.x_cond);
  g.large_font = (!round && ly.size_class != SizeClass::Compact && g.fc_step_y >= dp_px_for(48, s));
  return g;
}

}  // namespace latter_ui
