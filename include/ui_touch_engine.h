#pragma once

// ==============================================================================
// Latter Compiled On-Device Touch, Rotary & Hit-Table Input Engine
// (include/ui_touch_engine.h)
// Pure C++20 UI touch engine header — zero ESPHome or HAL includes; compiles standalone on
// both ESP32 and 5-geometry host tests. Absorbs compute_touch_context().
// ==============================================================================

#include "state/device_runtime_state.h"
#include "ui_cadence.h"
#include "ui_compositor.h"
#include "ui_hit_table.h"
#include "ui_layout.h"
#include "ui_types.h"
#include "ui_widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace latter_ui {

// ==============================================================================
// 1. Canonical Touch Context Fingerprint (kept in UI headers, out of core state PODs)
// ==============================================================================
inline int compute_touch_context(const DeviceRuntimeState &rs = runtime_state()) noexcept {
  const int gp = rs.nav.gui_page;
  int sub = 0;
  int drill = 0;
  switch (gp) {
    case 1:
      sub = rs.nav.home_subpage;
      break;
    case 2:
      sub = ((static_cast<int>(rs.music.view) & 0x0F) << 4) |
            ((static_cast<int>(rs.music.drilldown) & 0x03) << 2) |
            (static_cast<int>(rs.music.takeover) & 0x03);
      if ((rs.music.view == MusicView::LIBRARY && rs.music.drilldown == MusicDrilldown::LIB_LIST) ||
          rs.music.view == MusicView::ALBUM_TRACKS) {
        for (char ch : rs.music.selected_category_mode) {
          drill = ((drill * 31) + static_cast<unsigned char>(ch)) & 0x7F;
        }
      } else if (rs.music.view == MusicView::SETTINGS) {
        drill = ((rs.music.mode_bar_win_start & 0x07) << 3) | (rs.music.settings_page & 0x07);
      } else {
        drill = rs.music.mode_bar_win_start & 0x7F;
      }
      break;
    case 3:
      sub = rs.nav.active_game;
      break;
    case 4:
      sub = rs.nav.system_menu_mode;
      break;
    case 5:
      sub = rs.nav.sky_weather_subpage;
      break;
    case 6:
      sub = rs.nav.area_subpage;
      drill = (rs.area.browse_mode == "areas" || rs.area.browse_mode == "area_areas") ? 1 : 0;
      break;
    case 7:
      sub = rs.nav.remote_subpage;
      break;
    case 8:
      sub = rs.nav.tools_subpage;
      break;
    case 9:
      sub = home_hub_tab();
      drill = (rs.intercom.broadcast_dictation_active ? 4 : 0) |
              (((sub == HomeHubTab::INTERCOM ? home_hub_state().broadcast_page
                                             : home_hub_state().entry_page) & 0x0F) << 3);
      break;
    case 10:
      sub = rs.nav.time_subpage;
      if (sub == 1 && rs.timers.view_slot > 0) {
        drill = rs.timers.view_slot;
      } else if (sub == 2 && rs.alarms.edit_idx > 0) {
        drill = rs.alarms.edit_idx;
      }
      break;
    case 11:
      sub = rs.nav.pinned_menu_page;
      break;
    case 12:
      sub = rs.cal.subpage;
      drill = (rs.cal.drilldown & 0x1F) | (rs.cal.delete_confirm_open ? 0x40 : 0);
      break;
    case 13:
      sub = rs.cameras.tab;
      drill = (rs.cameras.hud_active ? 1 : 0) |
              (rs.cameras.quick_reply_open ? 2 : 0) |
              (((sub == CamerasTab::SETTINGS ? rs.cameras.settings_page
                                             : rs.cameras.grid_page) & 0x07) << 2);
      break;
    default:
      break;
  }
  if (rs.nav.shortcut_confirm.open) {
    drill = (drill & 0x7F) | 0x80;
  }
  int ctx = ((gp & 0xFF) << 16) | ((sub & 0xFF) << 8) | (drill & 0xFF);
  if (rs.nav.quick_actions_open) {
    ctx |= (1 << 24);
  }
  return ctx;
}

// ==============================================================================
// 2. Music App v3 Multi-Geometry Layout & Synced LRC Smooth-Scroll Engine
// ==============================================================================
struct MusicNowPlayingGeom {
  int w{466};
  int h{466};
  int cx{233};
  int min_hit{73};

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

  int transport_cy{228};
  int prev_cx{135};
  int play_cx{233};
  int next_cx{331};
  int play_r{38};
  int side_r{28};
  int play_hit_r{38};
  int side_hit_r{37};

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

  compute_mode_bar_slot_xs(4, w, round, g.mode_slot_xs);
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
  (void)has_pips;
  MusicListRowGeom g{};
  const auto ly = compute_panel_layout(w, h, round, wearable, ppi);
  g.min_hit = mm_to_px_for(7.0f, ppi);
  const int rows = std::clamp(visible_rows, 1, 5);
  const int idx = std::clamp(row_idx, 0, rows - 1);

  g.card_w = ly.list_card_w;
  g.card_h = ly.slot5_h;
  g.card_x0 = ly.cx - (g.card_w / 2);

  const int std_top = ly.slot5_y(0) - g.card_h / 2;
  const int std_bot = ly.slot5_y(rows - 1) + g.card_h / 2;
  if (content_top <= std_top + 2 && std_bot <= content_bot + 2) {
    g.cy_row = ly.slot5_y(idx);
  } else {
    const int avail_h = std::max(rows * g.card_h, content_bot - content_top);
    const int std_gap = std::max(4, ly.slot5_stride - g.card_h);
    const int gap = (rows > 1) ? std::clamp((avail_h - rows * g.card_h) / (rows - 1), 4, std_gap) : 0;
    const int block_h = rows * g.card_h + (rows - 1) * gap;
    const int start_y0 = content_top + std::max(0, (avail_h - block_h) / 2);
    g.cy_row = start_y0 + idx * (g.card_h + gap) + g.card_h / 2;
  }
  g.card_y0 = g.cy_row - g.card_h / 2;

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

struct MusicLyricsTransportGeom {
  int cx{233};
  int min_hit{73};
  int transport_cy{288};
  int prev_cx{137};
  int play_cx{233};
  int next_cx{329};
  int play_r{28};
  int side_r{22};
  int play_hit_pad{8};
  int side_hit_pad{14};
  int play_hit_r{36};
  int side_hit_r{36};
  int vp_bot{248};
};

inline constexpr MusicLyricsTransportGeom compute_music_lyrics_transport_geom(
    int w, int h, bool round, bool wearable, int ppi) noexcept {
  MusicLyricsTransportGeom g{};
  const auto ly = compute_panel_layout(w, h, round, wearable, ppi);
  const int min_s = (w < h) ? w : h;
  g.cx = ly.cx;
  g.min_hit = mm_to_px_for(7.0f, ppi);

  g.play_r = std::clamp(scale_min_for(28, min_s), 22, 42);
  g.side_r = std::clamp(scale_min_for(22, min_s), 18, 34);
  g.play_hit_pad = std::max(0, (g.min_hit - (2 * g.play_r + 1) + 1) / 2);
  g.side_hit_pad = std::max(0, (g.min_hit - (2 * g.side_r + 1) + 1) / 2);
  g.play_hit_r = g.play_r + g.play_hit_pad;
  g.side_hit_r = g.side_r + g.side_hit_pad;

  const int max_hit_r = std::max(g.play_hit_r, g.side_hit_r);
  const int r_track = ly.dock_r + scale_s_for(3, min_s);
  const int mode_bar_vis_top = ly.app_dock_y - r_track;
  const int mode_bar_hit_top = std::min(
      ly.app_dock_y - ly.dock_hit_r,
      ly.app_dock_y - ly.dock_r - scale_y_for(12, h));
  const int vis_gap = std::max(10, scale_y_for(14, h));
  const int cy_from_vis = mode_bar_vis_top - vis_gap - g.play_r;
  const int cy_from_hit = mode_bar_hit_top - 2 - max_hit_r;
  g.transport_cy = std::min(cy_from_vis, cy_from_hit);

  const int min_dx = g.play_hit_r + g.side_hit_r + std::max(6, scale_x_for(8, w));
  const int safe_half = band_safe_span_for(
                            g.transport_cy, 2 * g.side_r, w, h, round, wearable, 14) /
                        2;
  const int desk_half = (w > 520) ? (kMaxContentColW / 2) : safe_half;
  const int max_half = std::min(safe_half, desk_half);
  const int max_dx = std::max(min_dx, max_half - g.side_r);
  const int dx = std::clamp(std::max(min_dx, scale_min_for(96, min_s)), min_dx, max_dx);
  g.play_cx = g.cx;
  g.prev_cx = g.cx - dx;
  g.next_cx = g.cx + dx;

  g.vp_bot = (g.transport_cy - max_hit_r) - std::max(4, scale_y_for(4, h));
  return g;
}

inline void register_music_lyrics_transport_hits(
    FrameHitTable &ht, const MusicLyricsTransportGeom &g) {
  ht.add_circle(g.prev_cx, g.transport_cy, g.side_r,
                WidgetId::MUSIC_PREV_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle,
                0, g.side_hit_pad);
  ht.add_circle(g.play_cx, g.transport_cy, g.play_r,
                WidgetId::MUSIC_PLAY_PAUSE, kHitTap | kHitFocusSlot | kHitFocusCircle,
                0, g.play_hit_pad);
  ht.add_circle(g.next_cx, g.transport_cy, g.side_r,
                WidgetId::MUSIC_NEXT_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle,
                0, g.side_hit_pad);
}

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

// ==============================================================================
// 3. Host Hardware / Entity Bridge Enums & TouchGestureEngine
// ==============================================================================
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
  START_INSTANT_HIBERNATION,
  TURN_SCREEN_OFF,
  RESTART_DEVICE,
  CHECK_FIRMWARE_UPDATE,
  TRIGGER_FIRMWARE_UPDATE,
  APPLY_THEME_PRESET,
  MENU_SCROLL_TRANSITION,
  FETCH_ON_DEMAND_CAMERA,
  RELEASE_CAMERA_ART,
  SYNC_CAMERA_STREAM,
  STOP_CAMERA_STREAM,
  STOP_DOORBELL_AUDIO,
  START_DOORBELL_PTT,
  START_CALENDAR_DICTATION,
  START_BROADCAST_DICTATION,
  APPLY_DEVICE_PREFS_EFFECTS,
  NOTIFY_WEATHER_SIM_CHANGED,
  SET_LOCAL_SPEAKER_VOL_PCT,
  ENSURE_UI_ANIM_POLLER,
};

enum class UiSelectId : uint8_t {
  TAP_WAKE_MODE = 0,
  WEATHER_SIM_CONDITION,
  MUSIC_OVERLAY_STYLE,
  MUSIC_AUTO_OPEN,
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
  SCREENSAVER_MODE,
  SCREENSAVER_STYLE,
  SCREENSAVER_DOCKED_TIMEOUT,
  HIBERNATE_TIMEOUT_MODE,
  DEFAULT_START_APP,
  CLOCK_FACE_STYLE,
  TIME_FORMAT,
  HOME_CLOCK_TAP_ACTION,
  HOME_DATE_TAP_ACTION,
  AUTO_BRIGHTNESS_MODE,
  BEDTIME_DISPLAY,
  CAMERA_DEFAULT_VIEW,
  CAMERA_CAROUSEL_INTERVAL,
  CAMERA_FRAMING,
  CAMERA_STREAM_MODE,
  CAMERA_DOORBELL_POPUP,
};

enum class UiSwitchId : uint8_t {
  HOME_WEATHER_BG = 0,
  WEATHER_APP_BG,
  HOME_WAKE_GREETING,
  BLE_AREA_BEACON,
  GAME_SOUND,
  VA_WAKE_SCREEN,
  SCREENSAVER_PHOTO_CLOCK,
  BEDTIME_MODE,
  DO_NOT_DISTURB,
};

enum class UiNumberId : uint8_t {
  VOLUME_STEP_SIZE = 0,
  SPEAKER_VOLUME,
  ALERT_VOLUME,
  MANUAL_BRIGHTNESS,
  DAY_BRIGHTNESS,
  NIGHT_BRIGHTNESS,
  BEDTIME_BRIGHTNESS,
  DAY_START_HOUR,
  NIGHT_START_HOUR,
};

struct UiTouchRuntime {
  using ExecCmdFn = void (*)(UiHostCmd cmd, int arg0, int arg1);

  ExecCmdFn exec_cmd{nullptr};

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

  UiThemeSlot theme_primary{};
  UiThemeSlot theme_secondary{};

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

template <typename ColorT>
inline void apply_effective_theme_preset(const DeviceRuntimeState &rs,
                                         const PersistedDevicePrefs &prefs,
                                         uint32_t now_ms,
                                         ColorT &out_primary,
                                         ColorT &out_secondary) noexcept {
  out_secondary = ColorT{192, 192, 192};
  if (rs.showcase.is_active(now_ms) && rs.showcase.theme_override_mode != 0U) {
    switch (static_cast<ShowcaseThemeMode>(rs.showcase.theme_override_mode)) {
      case ShowcaseThemeMode::CUSTOM_RGB:
        apply_custom_rgb_harmony(rs.showcase.theme_override_rgb, out_primary, out_secondary);
        return;
      case ShowcaseThemeMode::PINK:
        out_primary = ColorT{168, 120, 132};
        return;
      case ShowcaseThemeMode::GREEN:
        out_primary = ColorT{66, 116, 83};
        return;
      case ShowcaseThemeMode::BLUE:
        out_primary = ColorT{69, 110, 138};
        return;
      case ShowcaseThemeMode::STEEL_BLUE:
      default:
        out_primary = ColorT{107, 142, 158};
        return;
    }
  }
  const std::string_view p(pref_color_profile_cstr(prefs));
  if (p == "Custom") {
    apply_custom_rgb_harmony(prefs.custom_theme_rgb, out_primary, out_secondary);
  } else if (p == "Pink") {
    out_primary = ColorT{168, 120, 132};
  } else if (p == "Green") {
    out_primary = ColorT{66, 116, 83};
  } else if (p == "Blue") {
    out_primary = ColorT{69, 110, 138};
  } else {
    out_primary = ColorT{107, 142, 158};
  }
}

inline void apply_effective_theme_slots(const DeviceRuntimeState &rs,
                                        const PersistedDevicePrefs &prefs,
                                        uint32_t now_ms,
                                        UiThemeSlot out_primary,
                                        UiThemeSlot out_secondary) noexcept {
  UiColor p{};
  UiColor s{};
  apply_effective_theme_preset(rs, prefs, now_ms, p, s);
  out_primary.assign(p);
  out_secondary.assign(s);
}

struct DeviceUiCommandEffects {
  bool handled{false};
  bool wake_display{false};
  bool turn_screen_off{false};
  bool apply_theme{false};
  bool sync_brightness{false};
  bool apply_power{false};
  bool enter_screensaver{false};
  bool refresh_screensaver_in_place{false};
  bool fetch_screensaver_photo{false};
  bool dismiss_notification{false};
  bool ensure_anim_poller{false};
  bool schedule_disp{false};
  bool mark_dirty_full{false};
};

void activate_music_peer_tab(MusicView target, uint32_t now_ms);

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
  bool navigate_route(std::string_view token, uint32_t now_ms);
  DeviceUiCommandEffects execute_device_ui_command(std::string_view cmd, uint32_t now_ms);
  DeviceUiCommandEffects clear_showcase(uint32_t now_ms);
  DeviceUiCommandEffects tick_showcase(uint32_t now_ms);

  // Internal cross-TU helpers (implemented in touch_engine.cpp / touch_dispatch.cpp / showcase_engine.cpp)
  void teardown_before_remote_nav_(DeviceRuntimeState &rs, uint32_t now_ms, bool target_is_camera);
  DeviceUiCommandEffects execute_device_ui_command_impl_(std::string_view cmd, uint32_t now_ms, bool from_reel);
  bool try_commit_long_press_(uint32_t now_ms);
  bool try_commit_kinematic_swipe_(bool is_release, uint32_t now_ms);
  bool handle_pin_pad_tap(int tx, int ty, uint32_t now_ms);
  bool gate_or_dispatch_area_action(const std::string &domain_or_entity,
                                    const std::string &action, int level,
                                    const char *pin_prompt, uint32_t now_ms,
                                    std::string_view success_feedback = "");
  bool gate_or_dispatch_alarm_action(const std::string &action, uint32_t now_ms,
                                     std::string_view success_feedback = "");
  bool gate_or_dispatch_doorbell_unlock(uint32_t now_ms);

  bool handle_tap_nav_and_takeovers_(int tx, int ty, uint32_t now_ms, const HitRegion *hit, uint16_t hit_wid);
  bool handle_tap_music_(int tx, int ty, uint32_t now_ms, const HitRegion *hit, uint16_t hit_wid);
  bool handle_tap_home_area_(int tx, int ty, uint32_t now_ms, const HitRegion *hit, uint16_t hit_wid);
  bool handle_tap_time_cal_(int tx, int ty, uint32_t now_ms, const HitRegion *hit, uint16_t hit_wid);
  bool handle_tap_system_tools_(int tx, int ty, uint32_t now_ms, const HitRegion *hit, uint16_t hit_wid);
  bool handle_tap_cameras_games_(int tx, int ty, uint32_t now_ms, const HitRegion *hit, uint16_t hit_wid);

 private:
  friend struct UiTouchRuntime;
  friend void activate_music_peer_tab(MusicView target, uint32_t now_ms);

  static uint32_t now_or_hal_ms_(uint32_t fallback_ms = 0U) noexcept {
    const uint32_t h = hal_millis_now();
    return (h != 0U) ? h : fallback_ms;
  }

  static bool resolve_sys_bri_slider_id_(int16_t param, std::string_view auto_bri_mode,
                                         UiNumberId &out_id) noexcept {
    const bool ab_off = (auto_bri_mode == "Disabled" || auto_bri_mode.empty());
    if (param == 5 && ab_off) {
      out_id = UiNumberId::MANUAL_BRIGHTNESS;
      return true;
    }
    if (param == 7 && !ab_off) {
      out_id = UiNumberId::DAY_BRIGHTNESS;
      return true;
    }
    if (param == 8 && !ab_off) {
      out_id = UiNumberId::NIGHT_BRIGHTNESS;
      return true;
    }
    return false;
  }

  static UiNumberId brightness_number_id_for_kind_(ActiveBrightnessTargetKind kind) noexcept {
    switch (kind) {
      case ActiveBrightnessTargetKind::DAY:     return UiNumberId::DAY_BRIGHTNESS;
      case ActiveBrightnessTargetKind::NIGHT:   return UiNumberId::NIGHT_BRIGHTNESS;
      case ActiveBrightnessTargetKind::BEDTIME: return UiNumberId::BEDTIME_BRIGHTNESS;
      case ActiveBrightnessTargetKind::MANUAL:
      default:                                  return UiNumberId::MANUAL_BRIGHTNESS;
    }
  }

  static int32_t resolve_touch_today_ymd_(const UiTouchRuntime &rt) noexcept {
    if (rt.time_valid && rt.time_year >= 2020) {
      return make_ymd(rt.time_year, rt.time_month, rt.time_day);
    }
    return 20260101;
  }

  static void scrub_pin_and_pending_(TouchGestureEngine &eng, DeviceRuntimeState &rs,
                                     bool clear_feedback_and_shortcut = true);
  static bool allow_active_touch_down_(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms);
  static bool handle_down_takeovers_(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms);
  static void handle_quick_actions_release_(TouchGestureEngine &eng, uint32_t now_ms,
                                            const HitRegion *start_slider);
  static void handle_down_alert_overlay_(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms);
  static bool handle_down_cameras_games_(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms,
                                         uint16_t wid);
  static bool handle_down_system_tools_(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms,
                                        const HitRegion *hit, uint16_t wid, uint32_t tkey);
  static bool handle_down_time_cal_(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms,
                                    const HitRegion *hit, uint16_t wid, uint32_t tkey);

  static void do_execute_area_action_();
  static void apply_optimistic_shortcut_feedback_(const std::string &entity_id,
                                                  const std::string &action,
                                                  const std::string &label,
                                                  DeviceRuntimeState &rs,
                                                  uint32_t now_ms);
  static void activate_shortcut_item_(const ShortcutItem &item, ShortcutOrigin origin,
                                      DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                                      uint32_t now_ms);
  static void launch_dock_app_(int chosen_app, DeviceRuntimeState &rs,
                               const UiTouchRuntime &rt, uint32_t now_ms);
  static void launch_shortcut_route_(RouteId route_id, ShortcutOrigin origin,
                                     DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                                     uint32_t now_ms);
  static bool return_from_shortcut_origin_(DeviceRuntimeState &rs,
                                           const UiTouchRuntime &rt,
                                           uint32_t now_ms);
  static void handle_home_activity_action_(const HomeActivityItem *items, int count, int item_idx,
                                           bool quick_btn_tap, uint32_t now_ms,
                                           DeviceRuntimeState &rs, PersistedDevicePrefs &prefs,
                                           const UiTouchRuntime &rt);
  static void dispatch_music_play_item_(const std::string &media_id, const std::string &media_type,
                                        const std::string &item_name, std::string_view enqueue,
                                        bool radio_mode, DeviceRuntimeState &rs, uint32_t now_ms);
  static std::string resolve_music_speaker_entity_(const DeviceRuntimeState &rs);
  static int fetch_music_browse_for_current_view_(const DeviceRuntimeState &rs, uint32_t now_ms);
  static bool open_music_row_action_sheet_(int slot_idx, DeviceRuntimeState &rs, uint32_t now_ms);
  static bool handle_music_back_step_(DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms);
  static void do_fetch_area_browse_();
  static void do_fetch_area_list_browse_();
  static void enter_home_hub_tab_(int target_tab, bool expand_cam, DeviceRuntimeState &rs,
                                  const UiTouchRuntime &rt, uint32_t now_ms);
  static const HomeActivityItem *resolve_home_activities_(uint32_t now_ms, const DeviceRuntimeState &rs,
                                                          const PersistedDevicePrefs &prefs,
                                                          const UiTouchRuntime &rt, int &out_count);
  static int remote_key_for_widget_(uint16_t wid) noexcept;
  static void do_remote_send_key_(int key_idx, int target_slot, bool long_press = false,
                                  uint32_t now_override_ms = 0U);

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
  bool wheel_drag_dirty_{false};
  bool pending_doorbell_unlock_{false};
  int music_scroll_last_y_{0};
  std::string pending_pin_feedback_{};
};

TouchGestureEngine &touch_engine();
void activate_music_peer_tab(MusicView target, uint32_t now_ms);
void request_music_lyrics_refresh(const std::string &track_key, const std::string &speaker_entity,
                                  uint32_t now_ms);

}  // namespace latter_ui
