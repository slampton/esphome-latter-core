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

  g.title_y = round ? ly.hdr_y : (ly.hdr_y + scale_y_for(6, h));
  g.sub_y = g.title_y + scale_y_for(24, h);
  g.prog_y = g.sub_y + scale_y_for(18, h);
  g.prog_h = (w >= 640 && h >= 640) ? 6 : 4;
  g.time_y = g.prog_y + g.prog_h + scale_y_for(14, h);

  const int prog_safe = safe_span_for(g.prog_y, w, h, round, wearable, 20);
  const int max_prog_w = (w > 520) ? 420 : 284;
  g.prog_w = std::clamp(std::min(max_prog_w, prog_safe), 96, std::max(96, w - 32));
  g.prog_x0 = g.cx - g.prog_w / 2;

  g.scrub_open_y0 = std::max(8, g.title_y - scale_y_for(12, h));
  const int meta_band_max_y1 = scale_y_for(136, h) - 2;
  const int raw_y1 = std::max(g.scrub_open_y0 + g.min_hit, g.time_y + scale_y_for(13, h));
  const int clamped_y1 = std::max(g.scrub_open_y0 + g.min_hit, std::min(meta_band_max_y1, raw_y1));
  g.scrub_open_h = clamped_y1 - g.scrub_open_y0;
  const int scrub_safe = band_safe_span_for(
      g.scrub_open_y0 + g.scrub_open_h / 2, g.scrub_open_h, w, h, round, wearable, 14);
  g.scrub_open_w = std::max(g.min_hit, std::min((w > 520) ? 480 : (w - 8), scrub_safe));
  if (g.scrub_open_w > w - 8) g.scrub_open_w = w - 8;
  g.scrub_open_x0 = g.cx - g.scrub_open_w / 2;

  g.mode_bar_cy = ly.app_dock_y;
  g.mode_bar_r = ly.dock_r;
  const int min_r = (g.min_hit + 1) / 2;
  g.mode_bar_pad = std::max(scale_s_for(8, min_s), min_r - g.mode_bar_r);
  const int mode_bar_top = ly.app_dock_y - ly.dock_r;
  g.spk_caption_y = mode_bar_top - scale_y_for(22, h);

  g.play_r = std::clamp(scale_min_for(38, min_s), 28, 62);
  g.side_r = std::clamp(scale_min_for(28, min_s), 22, 46);
  g.play_hit_r = std::max(g.play_r, min_r);
  g.side_hit_r = std::max(g.side_r, min_r);

  g.transport_cy = (g.scrub_open_y0 + g.scrub_open_h + (g.spk_caption_y - scale_y_for(16, h))) / 2;
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
      const int max_ws = std::max(0, kMusicPeerTabCount - 4);
      const int ws = std::clamp(mode_win_start, 0, max_ws);
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
      if (ws + 4 < kMusicPeerTabCount) {
        const int rx0 = g.mode_slot_xs[3] + g.mode_bar_r + 1;
        if (g.w - rx0 >= 8) {
          ht.add_rect(rx0, g.mode_bar_cy - g.mode_bar_r - scale_y_for(12, g.h), g.w - rx0,
                      2 * (g.mode_bar_r + scale_y_for(12, g.h)), WidgetId::MODE_BAR_RIGHT, kHitTap);
        }
      }
    }
  }
}

inline constexpr int kMusicPeerListPageSize = 4;
inline constexpr int kMusicFullListPageSize = 5;

inline constexpr int music_browse_page_size_for(
    MusicView /*view*/, MusicDrilldown drilldown, std::string_view mode = {}) noexcept {
  if (mode == "bio" || mode == "track_info") return kMusicFullListPageSize;
  if (drilldown == MusicDrilldown::LIB_LIST) return kMusicFullListPageSize;
  return kMusicPeerListPageSize;
}

inline constexpr MusicView step_music_peer_tab(MusicView cur, int h_dir) noexcept {
  int idx = (static_cast<int>(cur) + h_dir) % kMusicPeerTabCount;
  if (idx < 0) idx += kMusicPeerTabCount;
  return static_cast<MusicView>(idx);
}

inline constexpr bool is_music_playable_container_type(std::string_view media_type) noexcept {
  return media_type == "album" || media_type == "artist" || media_type == "playlist";
}

struct MusicListRowGeom {
  int card_x0{0};
  int card_y0{0};
  int card_w{340};
  int card_h{48};
  int cy_row{200};
  int play_vis_w{0};
  int play_hit_w{0};
  int min_hit{73};
};

inline constexpr MusicListRowGeom compute_music_list_row_geom(
    int row_idx, int visible_rows, int content_top, int content_bot,
    bool has_leading_play, bool has_pips,
    int w, int h, bool round, bool wearable, int ppi) noexcept {
  MusicListRowGeom g{};
  g.min_hit = mm_to_px_for(7.0f, ppi);
  const int rows = std::max(1, visible_rows);
  const int gap = (rows <= 3) ? std::max(6, scale_y_for(8, h)) : std::max(5, scale_y_for(6, h));
  const int avail_h = std::max(rows * 36, content_bot - content_top);
  const int raw_h = (avail_h - (rows - 1) * gap) / rows;
  if (rows <= 3) {
    const int max_card_h = std::max(g.min_hit, scale_y_for(68, h));
    const int min_card_h = std::min(g.min_hit, std::max(44, raw_h));
    g.card_h = std::clamp(raw_h, min_card_h, max_card_h);
  } else {
    const int max_card_h = scale_y_for(54, h);
    g.card_h = std::clamp(raw_h, 40, max_card_h);
  }

  const int block_h = rows * g.card_h + (rows - 1) * gap;
  int start_y0 = content_top + std::max(0, (avail_h - block_h) / 2);
  if (round && content_bot <= (h / 2 + 110)) {
    start_y0 = std::min(std::max(content_top, content_bot - block_h), start_y0 + 6);
  }
  g.cy_row = start_y0 + row_idx * (g.card_h + gap) + g.card_h / 2;
  g.card_y0 = g.cy_row - g.card_h / 2;

  const int cy_first = start_y0 + g.card_h / 2;
  const int cy_last = start_y0 + (rows - 1) * (g.card_h + gap) + g.card_h / 2;
  const int inset = 12;
  const int safe_top = band_safe_span_for(cy_first, g.card_h, w, h, round, wearable, inset);
  const int safe_bot = band_safe_span_for(cy_last, g.card_h, w, h, round, wearable, inset);
  const int stack_safe_w = std::min(safe_top, safe_bot);
  const int max_w = (w > 520) ? 480 : 384;

  int pip_mask_x = 0, pip_chev_x = 0, pip_hit_x0 = w, pip_hit_w = 0, pip_min_h = 0;
  if (has_pips) {
    compute_vertical_page_pips_x(w, round, ppi, pip_mask_x, pip_chev_x, pip_hit_x0, pip_hit_w, pip_min_h);
  }
  const int left_bound = (!round && w >= 460)
                             ? std::max((w - stack_safe_w) / 2, (w - max_w) / 2)
                             : ((w - stack_safe_w) / 2);
  const int right_bound = has_pips
                              ? std::min((w + stack_safe_w) / 2, pip_hit_x0 - 4)
                              : ((w + stack_safe_w) / 2);
  const int avail_w = std::max(140, right_bound - left_bound);
  g.card_w = std::clamp(std::min(max_w, avail_w),
                        std::min(avail_w, std::max(140, g.min_hit)),
                        std::max(140, w - 24));
  const int centered_x0 = (w - g.card_w) / 2;
  if (centered_x0 >= left_bound && centered_x0 + g.card_w <= right_bound) {
    g.card_x0 = centered_x0;
  } else {
    g.card_x0 = std::clamp(right_bound - g.card_w, left_bound, std::max(left_bound, w - g.card_w - inset));
  }

  if (has_leading_play) {
    g.play_vis_w = std::clamp(scale_x_for(44, w), 36, g.card_w / 3);
    g.play_hit_w = std::clamp(std::max(g.play_vis_w, g.min_hit), g.play_vis_w, g.card_w / 2);
  }
  return g;
}

inline void register_music_list_row_hits(
    FrameHitTable &ht, const MusicListRowGeom &g, int row_idx, bool enable_long_press) {
  const uint8_t flags = static_cast<uint8_t>(
      kHitTap | kHitFocusSlot | (enable_long_press ? kHitLongPress : 0U));
  if (g.play_hit_w > 0 && g.card_w > g.play_hit_w + 24) {
    ht.add_rect(g.card_x0, g.card_y0, g.play_hit_w, g.card_h,
                static_cast<uint16_t>(WidgetId::MUSIC_ROW_PLAY_BASE + row_idx),
                flags, static_cast<int16_t>(row_idx));
    ht.add_rect(g.card_x0 + g.play_hit_w, g.card_y0, g.card_w - g.play_hit_w, g.card_h,
                static_cast<uint16_t>(WidgetId::LIST_SLOT_BASE + row_idx),
                flags, static_cast<int16_t>(row_idx));
  } else {
    ht.add_rect(g.card_x0, g.card_y0, g.card_w, g.card_h,
                static_cast<uint16_t>(WidgetId::LIST_SLOT_BASE + row_idx),
                flags, static_cast<int16_t>(row_idx));
  }
}

struct MusicBentoTileGeom {
  int tile_x0{0};
  int tile_y0{0};
  int tile_w{160};
  int tile_h{68};
  int gap_x{12};
  int gap_y{12};
};

inline constexpr MusicBentoTileGeom compute_music_bento_tile_geom(
    int local_idx, int page_item_count, int content_top, int content_bot,
    int w, int h, bool round, bool wearable, int ppi) noexcept {
  MusicBentoTileGeom g{};
  const int min_hit = mm_to_px_for(7.0f, ppi);
  g.gap_x = (w < 380) ? 8 : ((w > 520) ? 18 : 12);
  g.gap_y = (w < 380) ? 10 : ((w > 520) ? 18 : 12);
  const int avail_h = std::max(132, content_bot - content_top);
  const int max_th = (h > 520) ? 108 : std::max(min_hit, 70);
  g.tile_h = std::clamp((avail_h - 2 * g.gap_y) / 3, 44, max_th);
  const int grid_h = 3 * g.tile_h + 2 * g.gap_y;
  int grid_y0 = content_top + std::max(0, (avail_h - grid_h) / 2);
  if (round && content_bot <= (h / 2 + 110)) {
    grid_y0 = std::min(std::max(content_top, content_bot - grid_h), grid_y0 + 6);
  }

  const int r = local_idx / 2;
  const int c = local_idx % 2;
  g.tile_y0 = grid_y0 + r * (g.tile_h + g.gap_y);

  const int inset = 12;
  const int safe_r0 = band_safe_span_for(grid_y0 + g.tile_h / 2, g.tile_h, w, h, round, wearable, inset);
  const int safe_r2 = band_safe_span_for(grid_y0 + 2 * (g.tile_h + g.gap_y) + g.tile_h / 2, g.tile_h,
                                         w, h, round, wearable, inset);
  const int stack_safe_w = std::min(safe_r0, safe_r2);
  const int max_grid_w = (w > 520) ? 480 : 384;

  int pip_mask_x = 0, pip_chev_x = 0, pip_hit_x0 = w, pip_hit_w = 0, pip_min_h = 0;
  compute_vertical_page_pips_x(w, round, ppi, pip_mask_x, pip_chev_x, pip_hit_x0, pip_hit_w, pip_min_h);
  const int left_bound = (!round && w >= 460)
                             ? std::max((w - stack_safe_w) / 2, (w - max_grid_w) / 2)
                             : ((w - stack_safe_w) / 2);
  const int right_bound = std::min((w + stack_safe_w) / 2, pip_hit_x0 - 4);
  const int grid_w = std::clamp(
      std::min(max_grid_w, right_bound - left_bound),
      148, std::max(148, w - 20));
  const int centered_x0 = (w - grid_w) / 2;
  const int grid_x0 = (centered_x0 >= left_bound && centered_x0 + grid_w <= right_bound)
                          ? centered_x0
                          : std::max(left_bound, right_bound - grid_w);
  const int col_w = std::max(68, (grid_w - g.gap_x) / 2);

  const bool solo_last = (local_idx == page_item_count - 1) && ((page_item_count % 2) == 1);
  if (solo_last) {
    g.tile_x0 = grid_x0;
    g.tile_w = grid_w;
  } else if (c == 0) {
    g.tile_x0 = grid_x0;
    g.tile_w = col_w;
  } else {
    g.tile_x0 = grid_x0 + grid_w - col_w;
    g.tile_w = col_w;
  }
  return g;
}

struct MusicInfoGeom {
  int vp_top{72};
  int vp_bot_hud{320};
  int vp_bot_ambient{442};
};

inline constexpr MusicInfoGeom compute_music_info_geom(
    int w, int h, bool round, bool wearable, int ppi) noexcept {
  MusicInfoGeom g{};
  const auto ly = compute_panel_layout(w, h, round, wearable, ppi);
  const int min_s = (w < h) ? w : h;
  g.vp_top = ly.hdr_y + scale_y_for(24, h);
  const int mode_bar_top = ly.app_dock_y - ly.dock_r - scale_s_for(14, min_s);
  g.vp_bot_hud = std::max(g.vp_top + 60, mode_bar_top - scale_y_for(8, h));
  g.vp_bot_ambient = std::max(g.vp_top + 80, h - (round ? scale_y_for(28, h) : scale_y_for(20, h)));
  return g;
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
  BEDTIME_DISPLAY,
};

enum class UiSwitchId : uint8_t {
  HOME_WEATHER_BG = 0,
  HOME_WAKE_GREETING,
  BLE_AREA_BEACON,
  GAME_SOUND,
  VA_WAKE_SCREEN,
  DOCKED_SCREENSAVER,
  INSTANT_CROWN_HIBERNATE,
  BEDTIME_MODE,
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
void activate_music_peer_tab(MusicView target, uint32_t now_ms);

#endif  // __has_include("esphome/core/defines.h")

}  // namespace latter_ui
