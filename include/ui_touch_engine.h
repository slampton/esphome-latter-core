#pragma once

// ==============================================================================
// Latter Compiled On-Device Touch, Rotary & Hit-Table Input Engine
// (include/ui_touch_engine.h)
// Unifies TouchRouter (TOUCH_DEBOUNCE_*, allow_tap) + TouchGestureEngine
// (FrameHitTable tap/slider/focus dispatch + mm swipe gestures).
// ==============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "latter_state.h"
#include "ui_hit_table.h"

#if __has_include("esphome/core/defines.h")
#include "audio_router.h"
#include "hal_power_pm.h"
#include "latter_actions.h"
#include "ui_compositor.h"
#include "ui_layout.h"
#include "ui_types.h"
#include "ui_widgets.h"
#endif

namespace latter_ui {

// ==============================================================================
// Pure-C++20 Clean-Sheet Music App v3 Multi-Geometry Layout, Modal Hit Isolation
// & Synced LRC Smooth-Scroll Engine (shared by page_music.cpp, touch_engine.cpp
// & standalone host unit tests across 466x466, 480x480, 410x502, 320x480, 720x720)
// ==============================================================================
struct MusicNowPlayingGeom {
  int w{466};
  int h{466};
  int cx{233};
  int min_hit{73};

  // Top Metadata + Progress Bar Zone (within [0, music_metadata_band_y1])
  int title_y{38};
  int sub_y{66};
  int prog_x0{93};
  int prog_y{88};
  int prog_w{280};
  int prog_h{4};
  int time_y{102};
  int scrub_open_x0{46};
  int scrub_open_y0{26};
  int scrub_open_w{374};
  int scrub_open_h{92};

  // Center Transport Zone (GlassVariant::Clear)
  int transport_cy{228};
  int prev_cx{135};
  int play_cx{233};
  int next_cx{331};
  int play_r{38};
  int side_r{28};
  int play_hit_r{38};
  int side_hit_r{37};

  // Bottom Active Speaker Caption + 8-Slot 2-Page Stadium Mode Bar
  int spk_caption_y{352};
  int mode_bar_cy{398};
  int mode_bar_r{28};
  int mode_bar_pad{10};
  int mode_slot_xs[4]{143, 203, 263, 323};
};

inline constexpr MusicNowPlayingGeom compute_music_now_playing_geom(
    int w, int h, bool round, bool wearable, int ppi) noexcept {
  MusicNowPlayingGeom g{};
  g.w = w;
  g.h = h;
  g.cx = w / 2;
  g.min_hit = mm_to_px_for(7.0f, ppi);
  const auto ly = compute_panel_layout(w, h, round, wearable, ppi);
  const int min_s = (w < h) ? w : h;

  g.title_y = ly.hdr_y + scale_y_for(6, h);
  g.sub_y = g.title_y + scale_y_for(28, h);
  g.prog_y = g.sub_y + scale_y_for(21, h);
  g.prog_h = (w >= 640 && h >= 640) ? 6 : 4;
  g.time_y = g.prog_y + g.prog_h + scale_y_for(10, h);

  const int prog_safe = safe_span_for(g.prog_y, w, h, round, wearable, 20);
  g.prog_w = std::clamp(std::min(scale_x_for(284, w), prog_safe), 96, std::max(96, w - 32));
  g.prog_x0 = g.cx - g.prog_w / 2;

  g.scrub_open_y0 = std::max(8, g.title_y - scale_y_for(12, h));
  const int meta_band_max_y1 = scale_y_for(136, h) - 2;
  const int raw_y1 = std::max(g.scrub_open_y0 + g.min_hit, g.time_y + scale_y_for(12, h));
  const int clamped_y1 = std::max(g.scrub_open_y0 + g.min_hit, std::min(meta_band_max_y1, raw_y1));
  g.scrub_open_h = clamped_y1 - g.scrub_open_y0;
  const int scrub_safe = band_safe_span_for(
      g.scrub_open_y0 + g.scrub_open_h / 2, g.scrub_open_h, w, h, round, wearable, 14);
  g.scrub_open_w = std::max(g.min_hit, scrub_safe);
  if (g.scrub_open_w > w - 8) g.scrub_open_w = w - 8;
  g.scrub_open_x0 = g.cx - g.scrub_open_w / 2;

  g.mode_bar_cy = ly.app_dock_y;
  g.mode_bar_r = ly.dock_r;
  const int min_r = (g.min_hit + 1) / 2;
  g.mode_bar_pad = std::max(scale_s_for(8, min_s), min_r - g.mode_bar_r);
  const int mode_bar_top = ly.app_dock_y - ly.dock_r;
  g.spk_caption_y = mode_bar_top - scale_y_for(18, h);

  g.play_r = std::clamp(scale_min_for(38, min_s), 28, 62);
  g.side_r = std::clamp(scale_min_for(28, min_s), 22, 46);
  g.play_hit_r = std::max(g.play_r, min_r);
  g.side_hit_r = std::max(g.side_r, min_r);

  g.transport_cy = (g.scrub_open_y0 + g.scrub_open_h + (g.spk_caption_y - scale_y_for(14, h))) / 2;
  const int min_dx = g.play_hit_r + g.side_hit_r + std::max(6, scale_x_for(10, w));
  const int max_dx = std::max(min_dx, (w / 2) - g.side_hit_r - 6);
  g.play_cx = g.cx;
  const int dx = std::clamp(std::max(min_dx, scale_min_for(100, min_s)), min_dx, max_dx);
  g.prev_cx = g.cx - dx;
  g.next_cx = g.cx + dx;

  const int dock_step = (w < 380)
                            ? ((w - 64) / 3)
                            : ((w > 520) ? ((w * 74) / 466)
                                         : (round ? scale_x_for(60, w) : scale_x_for(72, w)));
  g.mode_slot_xs[0] = g.cx - (3 * dock_step) / 2;
  g.mode_slot_xs[1] = g.cx - dock_step / 2;
  g.mode_slot_xs[2] = g.cx + dock_step / 2;
  g.mode_slot_xs[3] = g.cx + (3 * dock_step) / 2;
  return g;
}

inline void register_music_modal_backdrop_hits(
    FrameHitTable &ht, int w, int h, int card_x0, int card_y0, int card_w, int card_h) {
  const int x0 = std::clamp(card_x0, 0, w);
  const int y0 = std::clamp(card_y0, 0, h);
  const int x1 = std::clamp(card_x0 + card_w, x0, w);
  const int y1 = std::clamp(card_y0 + card_h, y0, h);
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

inline void register_music_now_playing_hits(
    FrameHitTable &ht, const MusicNowPlayingGeom &g,
    bool show_metadata, bool show_controls,
    MusicTakeover takeover, int mode_win_start = 0,
    bool include_mode_bar = true) {
  if (takeover != MusicTakeover::NONE) return;
  if (!show_metadata && !show_controls) {
    ht.add_rect(0, 0, g.w, g.h, WidgetId::MUSIC_ART_TAP, kHitTap);
    return;
  }
  if (show_metadata) {
    ht.add_rect(g.scrub_open_x0, g.scrub_open_y0, g.scrub_open_w, g.scrub_open_h,
                WidgetId::MUSIC_SCRUB_OPEN_BTN, kHitTap | kHitFocusSlot);
  }
  if (show_controls) {
    ht.add_circle(g.prev_cx, g.transport_cy, g.side_hit_r,
                  WidgetId::MUSIC_PREV_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle);
    ht.add_circle(g.play_cx, g.transport_cy, g.play_hit_r,
                  WidgetId::MUSIC_PLAY_PAUSE, kHitTap | kHitFocusSlot | kHitFocusCircle);
    ht.add_circle(g.next_cx, g.transport_cy, g.side_hit_r,
                  WidgetId::MUSIC_NEXT_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle);
    if (include_mode_bar) {
      const int ws = std::clamp(mode_win_start, 0, 4);
      for (int i = 0; i < 4; ++i) {
        ht.add_circle(g.mode_slot_xs[i], g.mode_bar_cy, g.mode_bar_r,
                      static_cast<uint16_t>(WidgetId::MODE_BAR_SLOT_BASE + i),
                      kHitTap | kHitFocusSlot | kHitFocusCircle,
                      static_cast<int16_t>(ws + i), g.mode_bar_pad);
      }
      if (ws > 0) {
        const int lw = g.mode_slot_xs[0] - g.mode_bar_r;
        if (lw >= 8) {
          ht.add_rect(0, g.mode_bar_cy - g.mode_bar_r - scale_y_for(12, g.h), lw,
                      2 * (g.mode_bar_r + scale_y_for(12, g.h)), WidgetId::MODE_BAR_LEFT, kHitTap);
        }
      }
      if (ws + 4 < 8) {
        const int rx0 = g.mode_slot_xs[3] + g.mode_bar_r + 1;
        if (g.w - rx0 >= 8) {
          ht.add_rect(rx0, g.mode_bar_cy - g.mode_bar_r - scale_y_for(12, g.h), g.w - rx0,
                      2 * (g.mode_bar_r + scale_y_for(12, g.h)), WidgetId::MODE_BAR_RIGHT, kHitTap);
        }
      }
    }
  }
}

struct MusicVolumeGeom {
  int w{466};
  int h{466};
  int min_hit{73};
  int card_x0{84};
  int card_y0{66};
  int card_w{298};
  int card_h{334};
  int hdr_title_y{84};
  int hdr_pct_y{106};
  int slider_x0{296};
  int slider_y0{120};
  int slider_w{74};
  int slider_h{266};
  int btn_x0{98};
  int btn_w{186};
  int btn_h{74};
  int btn_vis_h{50};
  int up_y0{120};
  int down_y0{206};
  int mute_y0{292};
};

inline constexpr MusicVolumeGeom compute_music_volume_geom(
    int w, int h, bool round, bool wearable, int ppi) noexcept {
  MusicVolumeGeom g{};
  g.w = w;
  g.h = h;
  g.min_hit = mm_to_px_for(7.0f, ppi);
  const int hdr_h = std::max(scale_y_for(50, h), 44);
  const int gap_y = std::max(6, scale_y_for(8, h));
  const int pad_y = std::max(8, scale_y_for(12, h));
  const int min_body_h = 3 * g.min_hit + 2 * gap_y;
  const int target_card_h = std::max(scale_y_for(334, h), hdr_h + min_body_h + 2 * pad_y);
  g.card_h = std::min(h - 24, target_card_h);
  g.card_y0 = (h - g.card_h) / 2;

  const int safe_w = band_safe_span_for(h / 2, g.card_h, w, h, round, wearable, 12);
  g.card_w = std::clamp(std::min(scale_x_for(360, w), safe_w), 240, w - 16);
  g.card_x0 = (w - g.card_w) / 2;

  g.hdr_title_y = g.card_y0 + scale_y_for(18, h);
  g.hdr_pct_y = g.hdr_title_y + scale_y_for(22, h);

  const int pad_x = std::max(10, scale_x_for(14, w));
  const int col_gap = std::max(10, scale_x_for(14, w));
  g.slider_w = (w >= 640) ? std::max(96, g.min_hit) : std::max(64, g.min_hit);
  g.slider_x0 = g.card_x0 + g.card_w - pad_x - g.slider_w;
  g.btn_x0 = g.card_x0 + pad_x;
  g.btn_w = std::max(g.min_hit, g.slider_x0 - col_gap - g.btn_x0);

  const int body_y0 = g.card_y0 + hdr_h;
  const int body_h = std::max(min_body_h, (g.card_y0 + g.card_h - pad_y) - body_y0);
  g.slider_y0 = body_y0;
  g.slider_h = body_h;

  g.btn_h = std::max(g.min_hit, (body_h - 2 * gap_y) / 3);
  g.btn_vis_h = std::clamp(scale_y_for(48, h), 38, g.btn_h);
  g.up_y0 = body_y0;
  g.down_y0 = g.up_y0 + g.btn_h + gap_y;
  g.mute_y0 = g.down_y0 + g.btn_h + gap_y;
  return g;
}

inline void register_music_volume_hits(FrameHitTable &ht, const MusicVolumeGeom &g) {
  register_music_modal_backdrop_hits(ht, g.w, g.h, g.card_x0, g.card_y0, g.card_w, g.card_h);
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
  int min_hit{73};
  int card_x0{80};
  int card_y0{66};
  int card_w{306};
  int card_h{334};
  int hdr_title_y{84};
  int hdr_time_y{108};
  int slider_x0{94};
  int slider_y0{120};
  int slider_w{278};
  int slider_h{74};
  int step_y0{202};
  int step_h{74};
  int step_vis_h{44};
  int step_back_x0{94};
  int step_fwd_x0{238};
  int step_btn_w{134};
  int mode_y0{284};
  int mode_h{74};
  int mode_vis_h{44};
  int shuf_x0{94};
  int rep_x0{189};
  int fav_x0{284};
  int mode_btn_w{88};
};

inline constexpr MusicScrubGeom compute_music_scrub_geom(
    int w, int h, bool round, bool wearable, int ppi) noexcept {
  MusicScrubGeom g{};
  g.w = w;
  g.h = h;
  g.min_hit = mm_to_px_for(7.0f, ppi);
  const int hdr_h = std::max(scale_y_for(50, h), 44);
  const int gap_y = std::max(6, scale_y_for(8, h));
  const int pad_y = std::max(8, scale_y_for(10, h));
  const int min_body_h = 3 * g.min_hit + 2 * gap_y;
  const int target_card_h = std::max(scale_y_for(334, h), hdr_h + min_body_h + 2 * pad_y);
  g.card_h = std::min(h - 24, target_card_h);
  g.card_y0 = (h - g.card_h) / 2;

  const int pad_x = std::max(8, scale_x_for(12, w));
  const int gap_x = std::max(6, scale_x_for(8, w));
  const int min_inner_w = 3 * g.min_hit + 2 * gap_x;
  const int safe_w = band_safe_span_for(h / 2, g.card_h, w, h, round, wearable, 10);
  g.card_w = std::clamp(std::max(min_inner_w + 2 * pad_x, std::min(scale_x_for(376, w), safe_w)),
                        min_inner_w + 2 * pad_x, w - 12);
  g.card_x0 = (w - g.card_w) / 2;

  g.hdr_title_y = g.card_y0 + scale_y_for(18, h);
  g.hdr_time_y = g.hdr_title_y + scale_y_for(22, h);

  const int inner_x0 = g.card_x0 + pad_x;
  const int inner_w = g.card_w - 2 * pad_x;
  const int body_y0 = g.card_y0 + hdr_h;
  const int body_h = std::max(min_body_h, (g.card_y0 + g.card_h - pad_y) - body_y0);
  const int row_h = std::max(g.min_hit, (body_h - 2 * gap_y) / 3);

  g.slider_x0 = inner_x0;
  g.slider_y0 = body_y0;
  g.slider_w = inner_w;
  g.slider_h = row_h;

  g.step_y0 = g.slider_y0 + row_h + gap_y;
  g.step_h = row_h;
  g.step_vis_h = std::clamp(scale_y_for(44, h), 36, row_h);
  g.step_btn_w = std::max(g.min_hit, (inner_w - gap_x) / 2);
  g.step_back_x0 = inner_x0;
  g.step_fwd_x0 = inner_x0 + inner_w - g.step_btn_w;

  g.mode_y0 = g.step_y0 + row_h + gap_y;
  g.mode_h = row_h;
  g.mode_vis_h = std::clamp(scale_y_for(44, h), 36, row_h);
  g.mode_btn_w = std::max(g.min_hit, (inner_w - 2 * gap_x) / 3);
  g.shuf_x0 = inner_x0;
  g.rep_x0 = inner_x0 + (inner_w - g.mode_btn_w) / 2;
  g.fav_x0 = inner_x0 + inner_w - g.mode_btn_w;
  return g;
}

inline void register_music_scrub_hits(FrameHitTable &ht, const MusicScrubGeom &g) {
  register_music_modal_backdrop_hits(ht, g.w, g.h, g.card_x0, g.card_y0, g.card_w, g.card_h);
  ht.add_rect(g.slider_x0, g.slider_y0, g.slider_w, g.slider_h,
              WidgetId::MUSIC_SCRUB_BAR, kHitTap | kHitHorizSlider | kHitFocusSlot);
  ht.add_rect(g.step_back_x0, g.step_y0, g.step_btn_w, g.step_h,
              WidgetId::MUSIC_SCRUB_STEP_BACK, kHitTap | kHitFocusSlot);
  ht.add_rect(g.step_fwd_x0, g.step_y0, g.step_btn_w, g.step_h,
              WidgetId::MUSIC_SCRUB_STEP_FWD, kHitTap | kHitFocusSlot);
  ht.add_rect(g.shuf_x0, g.mode_y0, g.mode_btn_w, g.mode_h,
              WidgetId::MUSIC_SHUFFLE_BTN, kHitTap | kHitFocusSlot);
  ht.add_rect(g.rep_x0, g.mode_y0, g.mode_btn_w, g.mode_h,
              WidgetId::MUSIC_REPEAT_BTN, kHitTap | kHitFocusSlot);
  ht.add_rect(g.fav_x0, g.mode_y0, g.mode_btn_w, g.mode_h,
              WidgetId::MUSIC_FAVORITE_BTN, kHitTap | kHitFocusSlot);
}

struct MusicActionSheetGeom {
  int w{466};
  int h{466};
  int min_hit{73};
  int card_x0{80};
  int card_y0{66};
  int card_w{306};
  int card_h{334};
  int hdr_title_y{88};
  int item_x0[5]{};
  int item_y0[5]{};
  int item_w[5]{};
  int item_h[5]{};
  int item_vis_h{46};
  int item_count{5};
};

inline constexpr MusicActionSheetGeom compute_music_action_sheet_geom(
    int w, int h, bool round, bool wearable, int ppi, int count = 5) noexcept {
  MusicActionSheetGeom g{};
  g.w = w;
  g.h = h;
  g.min_hit = mm_to_px_for(7.0f, ppi);
  g.item_count = std::clamp(count, 1, 5);
  const int rows = (g.item_count + 1) / 2;
  const int hdr_h = std::max(scale_y_for(44, h), 38);
  const int gap_y = std::max(6, scale_y_for(8, h));
  const int pad_y = std::max(8, scale_y_for(10, h));
  const int min_body_h = rows * g.min_hit + std::max(0, rows - 1) * gap_y;
  const int target_card_h = std::max(scale_y_for(326, h), hdr_h + min_body_h + 2 * pad_y);
  g.card_h = std::min(h - 24, target_card_h);
  g.card_y0 = (h - g.card_h) / 2;

  const int pad_x = std::max(8, scale_x_for(12, w));
  const int gap_x = std::max(6, scale_x_for(8, w));
  const int min_inner_w = 2 * g.min_hit + gap_x;
  const int safe_w = band_safe_span_for(h / 2, g.card_h, w, h, round, wearable, 12);
  g.card_w = std::clamp(std::max(min_inner_w + 2 * pad_x, std::min(scale_x_for(372, w), safe_w)),
                        min_inner_w + 2 * pad_x, w - 12);
  g.card_x0 = (w - g.card_w) / 2;
  g.hdr_title_y = g.card_y0 + scale_y_for(20, h);

  const int inner_x0 = g.card_x0 + pad_x;
  const int inner_w = g.card_w - 2 * pad_x;
  const int body_y0 = g.card_y0 + hdr_h;
  const int body_h = std::max(min_body_h, (g.card_y0 + g.card_h - pad_y) - body_y0);
  const int row_h = std::max(g.min_hit, (body_h - std::max(0, rows - 1) * gap_y) / std::max(1, rows));
  g.item_vis_h = std::clamp(scale_y_for(46, h), 38, row_h);
  const int col_w = std::max(g.min_hit, (inner_w - gap_x) / 2);

  for (int i = 0; i < g.item_count; ++i) {
    const int r = i / 2;
    const int c = i % 2;
    const bool solo_last = (i == g.item_count - 1) && ((g.item_count % 2) == 1);
    g.item_y0[i] = body_y0 + r * (row_h + gap_y);
    g.item_h[i] = row_h;
    if (solo_last) {
      g.item_x0[i] = inner_x0;
      g.item_w[i] = inner_w;
    } else if (c == 0) {
      g.item_x0[i] = inner_x0;
      g.item_w[i] = col_w;
    } else {
      g.item_x0[i] = inner_x0 + inner_w - col_w;
      g.item_w[i] = col_w;
    }
  }
  return g;
}

inline void register_music_action_sheet_hits(
    FrameHitTable &ht, const MusicActionSheetGeom &g, const uint16_t *wids) {
  register_music_modal_backdrop_hits(ht, g.w, g.h, g.card_x0, g.card_y0, g.card_w, g.card_h);
  for (int i = 0; i < g.item_count; ++i) {
    ht.add_rect(g.item_x0[i], g.item_y0[i], g.item_w[i], g.item_h[i],
                wids[i], kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
  }
}

// Advances synced LRC scroll_px toward the active lyric line anchored at ~38% of screen height
// via a smooth 240 ms ease_out_cubic curve (honoring the 6 s manual scroll override window).
inline void step_music_lyrics_auto_scroll(
    MusicLyricsState &lyr, uint32_t pos_ms, uint32_t now_ms,
    int anchor_y_in_viewport, const int *line_doc_cys, int line_count, int max_scroll_px) noexcept {
  const int clamped_max = std::max(0, max_scroll_px);
  if (!lyr.synced || line_count <= 0 || line_doc_cys == nullptr) {
    lyr.scroll_px = std::clamp(lyr.scroll_px, 0, clamped_max);
    lyr.target_scroll_px = lyr.scroll_px;
    lyr.scroll_anim_start_ms = 0U;
    return;
  }

  const int act = lyr.find_active_line(pos_ms);
  const bool manual_override = (now_ms < lyr.manual_override_until_ms);
  if (manual_override) {
    lyr.active_line_idx = act;
    lyr.scroll_px = std::clamp(lyr.scroll_px, 0, clamped_max);
    lyr.target_scroll_px = lyr.scroll_px;
    lyr.scroll_anim_start_ms = 0U;
    return;
  }

  const int desired = (act >= 0 && act < line_count)
                          ? std::clamp(line_doc_cys[act] - anchor_y_in_viewport, 0, clamped_max)
                          : 0;
  if (lyr.active_line_idx < 0 && act >= 0) {
    // Initial entry into synced lyrics: snap cleanly to the live verse.
    lyr.active_line_idx = act;
    lyr.scroll_px = desired;
    lyr.from_scroll_px = desired;
    lyr.target_scroll_px = desired;
    lyr.scroll_anim_start_ms = 0U;
    return;
  }

  if (act != lyr.active_line_idx || desired != lyr.target_scroll_px) {
    lyr.active_line_idx = act;
    lyr.from_scroll_px = std::clamp(lyr.scroll_px, 0, clamped_max);
    lyr.target_scroll_px = desired;
    lyr.scroll_anim_start_ms = (lyr.from_scroll_px == desired) ? 0U : ((now_ms == 0U) ? 1U : now_ms);
  }

  if (lyr.scroll_anim_start_ms != 0U) {
    static constexpr uint32_t kLyricsEaseMs = 240U;
    const uint32_t elapsed = (now_ms >= lyr.scroll_anim_start_ms) ? (now_ms - lyr.scroll_anim_start_ms) : kLyricsEaseMs;
    if (elapsed >= kLyricsEaseMs) {
      lyr.scroll_px = lyr.target_scroll_px;
      lyr.scroll_anim_start_ms = 0U;
    } else {
      const float t = static_cast<float>(elapsed) * (1.0f / static_cast<float>(kLyricsEaseMs));
      const float u = 1.0f - t;
      const float eased = 1.0f - (u * u * u);
      const float cur = static_cast<float>(lyr.from_scroll_px) +
                        static_cast<float>(lyr.target_scroll_px - lyr.from_scroll_px) * eased;
      lyr.scroll_px = std::clamp(static_cast<int>(std::lround(cur)), 0, clamped_max);
    }
  } else {
    lyr.scroll_px = std::clamp(lyr.target_scroll_px, 0, clamped_max);
  }
}

#if __has_include("esphome/core/defines.h")

// ------------------------------------------------------------------------------
// 1. Touch Timing Constants, TouchGestureState & TouchRouter
//    Defined in include/ui_hit_table.h (TOUCH_DEBOUNCE_*, touch_router(),
//    touch_gesture(), TouchPt) so host unit tests exercise them directly.
// ------------------------------------------------------------------------------

// ------------------------------------------------------------------------------
// 2. Host Hardware / Entity Bridge Enums (implemented in modules/system_core.yaml)
// ------------------------------------------------------------------------------
enum class UiHostCmd : uint8_t {
  SCHEDULE_DISP_UPDATE = 0,
  SCHEDULE_BAND_DISP_UPDATE,
  WAKE_TO_ACTIVE,
  APPLY_POWER_POLICY,
  CANCEL_VOICE_ASSISTANT,
  ABORT_VOICE_ASSISTANT,
  CYCLE_PRIMARY_WAKE_WORD,
  SYNC_WAKE_WORD_STATE,
  DISMISS_NOTIFICATION,
  STOP_ALARM_SOUND,
  SYNC_DISPLAY_BRIGHTNESS,
  FLASHLIGHT_FULL_BRIGHTNESS,
  PAUSE_LOCAL_MEDIA_PLAYER,
  TOGGLE_HA_MUSIC,
  STOP_HA_MUSIC,
  CLEAR_HA_MUSIC_QUEUE,
  MUSIC_STEP_VOLUME,
  PLAY_MUSIC_SLOT,
  FETCH_REMOTE_ART_DEBOUNCED,
  SPEAKER_TRANSFER_TO_TARGET,
  SPEAKER_CONTROL_TARGET,
  SPEAKER_JOIN_TARGET,
  SPEAKER_UNJOIN_TARGET,
  TRIGGER_SCREENSAVER_NOW,
  START_HIBERNATION_COUNTDOWN,
  TURN_SCREEN_OFF,
  RESTART_DEVICE,
  CHECK_FIRMWARE_UPDATE,
  TRIGGER_FIRMWARE_UPDATE,
  APPLY_THEME_PRESET,
  MENU_SCROLL_TRANSITION,
  FETCH_CALENDAR_MAP_SNAPSHOT,
  FETCH_ON_DEMAND_CAMERA,
  RELEASE_CAMERA_ART,
  STOP_DOORBELL_AUDIO,
  START_DOORBELL_PTT,
  START_CALENDAR_DICTATION,
  APPLY_DEVICE_PREFS_EFFECTS,
  NOTIFY_WEATHER_SIM_CHANGED,
  SET_LOCAL_SPEAKER_VOL_PCT,
};

enum class UiSelectId : uint8_t {
  TAP_WAKE_MODE = 0,
  WEATHER_SIM_CONDITION,
  MUSIC_OVERLAY_STYLE,
  MUSIC_FULLSCREEN_ART_MODE,
  MUSIC_ALBUM_SORT_ORDER,
  MUSIC_ARTIST_ALBUM_SORT_ORDER,
  MUSIC_PODCAST_SORT_ORDER,
  VA_WAKE_WORD_MODE,
  VA_VISUAL_PROFILE,
  WW_SENS_OKAY_NABU,
  WW_SENS_HEY_LUNA,
  WW_SENS_HEY_JARVIS,
  WW_SENS_HEY_MYCROFT,
  WW_SENS_ALEXA,
  THEME_PALETTE,
  PICKUP_WAKE_MODE,
  DISPLAY_OFF_TIMEOUT,
  SCREENSAVER_STYLE,
  SCREENSAVER_DOCKED_TIMEOUT,
  HIBERNATE_TIMEOUT_MODE,
  DEFAULT_START_APP,
  CLOCK_FACE_STYLE,
  TIME_FORMAT,
  HOME_CLOCK_TAP_ACTION,
  HOME_DATE_TAP_ACTION,
  AUTO_BRIGHTNESS_MODE,
  DISPLAY_BRIGHTNESS_PRESET,
  DAY_BRIGHTNESS_PRESET,
  NIGHT_BRIGHTNESS_PRESET,
};

enum class UiSwitchId : uint8_t {
  HOME_WEATHER_BG = 0,
  HOME_WAKE_GREETING,
  BLE_AREA_BEACON,
  GAME_SOUND,
  VA_WAKE_SCREEN,
  DOCKED_SCREENSAVER,
  INSTANT_CROWN_HIBERNATE,
};

enum class UiNumberId : uint8_t {
  VOLUME_STEP_SIZE = 0,
  SPEAKER_VOLUME,
  ALERT_VOLUME,
  DAY_START_HOUR,
  NIGHT_START_HOUR,
};

struct UiTouchRuntime {
  using ExecCmdFn = void (*)(UiHostCmd cmd, int arg0, int arg1);

  ExecCmdFn exec_cmd{nullptr};

  // Per-event live hardware/ESPHome snapshot
  const char *friendly_name{""};
  const char *fw_version{""};
  std::string fw_latest_version{};
  bool usb_connected{false};
  bool panel_is_on{true};
  bool local_mp_playing{false};
  mutable float local_mp_volume{0.6f};
  int album_art_w{0};
  std::string track_title{};
  std::string track_artist{};
  std::string track_album{};
  bool time_valid{false};
  int time_year{2026};
  int time_month{1};
  int time_day{1};
  int time_hour{-1};
  int time_minute{-1};
  int time_day_of_week{-1};
  uint32_t time_epoch{0};

  esphome::Color *theme_primary{nullptr};
  esphome::Color *theme_secondary{nullptr};

  void cmd(UiHostCmd c, int a = 0, int b = 0) const {
    if (exec_cmd != nullptr) exec_cmd(c, a, b);
  }
  void schedule_disp() const { cmd(UiHostCmd::SCHEDULE_DISP_UPDATE); }
  void schedule_band_disp(int y0, int y1) const { cmd(UiHostCmd::SCHEDULE_BAND_DISP_UPDATE, y0, y1); }
  void apply_power_policy() const { cmd(UiHostCmd::APPLY_POWER_POLICY); }

  std::string_view select_opt(UiSelectId id) const noexcept;
  void set_select_opt(UiSelectId id, std::string_view opt, bool pub = false) const;
  bool sw_state(UiSwitchId id) const noexcept;
  void sw_toggle(UiSwitchId id) const;
  float num_val(UiNumberId id) const noexcept;
  void set_num_val(UiNumberId id, float v) const;
};

class TouchGestureEngine {
 public:
  UiTouchRuntime &runtime() { return rt_; }
  const UiTouchRuntime &runtime() const { return rt_; }

  void on_touch_down(int tx, int ty, uint32_t now_ms);
  void on_touch_move(int tx, int ty, uint32_t now_ms);
  void on_touch_up(uint32_t now_ms);
  void tick(uint32_t now_ms);
  void on_touch(int tx, int ty, uint32_t now_ms) { on_touch_down(tx, ty, now_ms); }
  void on_update(int tx, int ty, uint32_t now_ms) { on_touch_move(tx, ty, now_ms); }
  void on_release(uint32_t now_ms) { on_touch_up(now_ms); }
  void on_input_event(const std::string &action, int delta, uint32_t now_ms);
  void navigate_home(uint32_t now_ms);
  void navigate_to_start_screen(uint32_t now_ms);

 private:
  UiTouchRuntime rt_{};
  uint32_t last_touch_activity_ms_{0};
  uint32_t last_drag_sleep_rst_ms_{0};
  uint32_t last_rapid_chev_ms_{0};
  uint32_t hud_just_woken_ms_{0};
  uint32_t last_area_slider_ms_{0};
  uint32_t last_music_vol_drag_ms_{0};
  bool music_vol_dragging_{false};
  bool music_scrub_dragging_{false};
  bool music_scroll_dragging_{false};
  int music_scroll_last_y_{0};

  bool try_commit_long_press_(uint32_t now_ms);
  bool try_commit_kinematic_swipe_(bool is_release, uint32_t now_ms);
  bool handle_pin_pad_tap(int tx, int ty, uint32_t now_ms);
  bool gate_or_dispatch_area_action(const std::string &domain_or_entity,
                                    const std::string &action, int level,
                                    const char *pin_prompt, uint32_t now_ms);
  bool gate_or_dispatch_alarm_action(const std::string &action, uint32_t now_ms);
  bool gate_or_dispatch_doorbell_unlock(uint32_t now_ms, bool from_intercom);
};

TouchGestureEngine &touch_engine();

#endif  // __has_include("esphome/core/defines.h")

}  // namespace latter_ui
