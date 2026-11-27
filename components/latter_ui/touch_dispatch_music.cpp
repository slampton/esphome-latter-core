// ==============================================================================
// Latter Compiled On-Device Music Touch Dispatch
// (components/latter_ui/touch_dispatch_music.cpp)
// ==============================================================================

#include "touch_dispatch_internal.h"
#include "ui_pages.h"
#include "ui_touch_recognizer.h"
#include "audio_router.h"
#include "hal_display_lifecycle.h"
#include "hal_panel_power.h"
#include "latter_actions.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace latter_ui {
using namespace latter_core;
using namespace touch_detail;

namespace {

inline std::string resolve_browse_item_media_type(const DeviceRuntimeState &rs,
                                                  const BrowseSlotItem &item) {
  if (rs.music.selected_category_mode == "artist") return "artist";
  if (rs.music.selected_category_mode == "album" ||
      rs.music.selected_category_mode == "artist_albums") return "album";
  if (rs.music.selected_category_mode == "podcast") return "podcast";
  if (rs.music.selected_category_mode == "podcast_episodes") return "podcast_episode";
  if (rs.music.selected_category_mode == "playlist") return "playlist";
  if (rs.music.selected_category_mode == "radio") return "radio";
  if (rs.music.selected_category_mode == "audiobook") return "audiobook";
  if (!item.type.empty() && item.type != "player" && item.type != "text") return item.type;
  return "track";
}

}  // namespace

namespace touch_detail {

void reset_music_loading_slots(DeviceRuntimeState &rs, const char *msg) {
  auto *slots = rs.music_slots();
  slots[0] = BrowseSlotItem{};
  slots[0].name = msg;
  slots[0].type = "text";
  for (size_t i = 1; i < DeviceRuntimeState::kSlotsPerPage; ++i) {
    slots[i] = BrowseSlotItem{};
    slots[i].type = "player";
  }
}

void resolve_music_track_strings(const DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                                 std::string &out_title, std::string &out_artist,
                                 std::string &out_album, bool &out_speech) {
  const auto &ms = media_state();
  if (!ms.is_local) {
    out_title = ms.title;
    out_artist = ms.artist;
    out_album = ms.album;
  } else {
    out_title = !rt.track_title.empty() ? rt.track_title : ms.title;
    out_artist = !rt.track_artist.empty() ? rt.track_artist : ms.artist;
    out_album = !rt.track_album.empty() ? rt.track_album : ms.album;
  }
  const std::string_view mt = !ms.media_type.empty()
                                  ? std::string_view(ms.media_type)
                                  : std::string_view(rs.music.active_playback_media_type);
  out_speech = (mt == "podcast" || mt == "podcast_episode" || mt == "audiobook");
}

void reset_music_to_now_playing(DeviceRuntimeState &rs, uint32_t now_ms, bool show_hud) {
  cancel_core_timeout(kTimeoutMusicPeerBrowseId);
  rs.music.reset_overlays();
  rs.music.clear_lyrics_auto_state();
  rs.music.view = MusicView::NOW_PLAYING;
  rs.music.sync_carousel_for_view(layout().dock_slot_count);
  mode_bar_anim().snap(0, 0);
  rs.music.hud_active = show_hud;
  rs.music.hud_timer = now_ms;
}

void cycle_music_pref_slot(const UiTouchRuntime &rt, int slot) {
  if (slot == 0) {
    rt.set_select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE,
                      cycle_option(rt.select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE),
                                   {"Full Screen", "Letterbox", "Standard", "None"}));
  } else if (slot == 1) {
    rt.set_select_opt(UiSelectId::MUSIC_OVERLAY_STYLE,
                      cycle_option(rt.select_opt(UiSelectId::MUSIC_OVERLAY_STYLE),
                                   {"Art Only", "Art + Metadata", "Always Visible"}));
  } else if (slot == 2) {
    rt.set_select_opt(UiSelectId::MUSIC_AUTO_OPEN,
                      cycle_option(rt.select_opt(UiSelectId::MUSIC_AUTO_OPEN),
                                   {"Now Playing", "Lyrics", "Off"}));
  } else if (slot == 3) {
    rt.set_select_opt(UiSelectId::MUSIC_ALBUM_SORT_ORDER,
                      cycle_option(rt.select_opt(UiSelectId::MUSIC_ALBUM_SORT_ORDER),
                                   {"Alphabetical", "Newest on Top", "Oldest on Top"}));
  } else if (slot == 4) {
    rt.set_select_opt(UiSelectId::MUSIC_ARTIST_ALBUM_SORT_ORDER,
                      cycle_option(rt.select_opt(UiSelectId::MUSIC_ARTIST_ALBUM_SORT_ORDER),
                                   {"Oldest on Top", "Newest on Top", "Alphabetical"}));
  } else if (slot == 5) {
    rt.set_select_opt(UiSelectId::MUSIC_PODCAST_SORT_ORDER,
                      cycle_option(rt.select_opt(UiSelectId::MUSIC_PODCAST_SORT_ORDER),
                                   {"Newest on Top", "Oldest on Top"}));
  }
}

}  // namespace touch_detail

void TouchGestureEngine::dispatch_music_play_item_(const std::string &media_id, const std::string &media_type,
                                     const std::string &item_name, std::string_view enqueue,
                                     bool radio_mode, DeviceRuntimeState &rs, uint32_t now_ms) {
  if (media_id.empty()) return;
  auto &m = rs.music;
  auto &ms = media_state();
  if (m.active_speaker_entity.empty()) {
    m.active_speaker_entity = !ms.active_speaker.empty() ? ms.active_speaker : ms.local_speaker;
    ms.is_local = !ms.local_speaker.empty() && (m.active_speaker_entity == ms.local_speaker);
  }
  const std::string spk = TouchGestureEngine::resolve_music_speaker_entity_(rs);
  const bool immediate = (enqueue == "play" || enqueue == "replace" || radio_mode);
  if (immediate) {
    audio_session().set_play_intent(MusicPlayIntent::ExternalOrUserExplicit, now_ms);
    music_queue_cleared() = false;
    if (media_type == "podcast" || media_type == "podcast_episode") {
      m.active_playback_media_type = "podcast";
    } else if (media_type == "audiobook") {
      m.active_playback_media_type = "audiobook";
    } else {
      m.active_playback_media_type = "music";
    }
  }
  if (radio_mode) m.action_feedback = "Radio: " + item_name;
  else if (enqueue == "next") m.action_feedback = "Play Next: " + item_name;
  else if (enqueue == "add") m.action_feedback = "Added to Queue";
  else m.action_feedback = "Playing: " + item_name;
  m.feedback_timer = now_ms;
  m.hud_active = true;
  m.hud_timer = now_ms;
  latter_core::emit_media_play_item(spk, media_id, media_type, enqueue, radio_mode);
}

std::string TouchGestureEngine::resolve_music_speaker_entity_(const DeviceRuntimeState &rs) {
  const auto &ms = media_state();
  if (!ms.active_speaker.empty()) return ms.active_speaker;
  if (!rs.music.active_speaker_entity.empty()) return rs.music.active_speaker_entity;
  return ms.local_speaker;
}

int TouchGestureEngine::fetch_music_browse_for_current_view_(const DeviceRuntimeState &rs, uint32_t now_ms) {
  cancel_core_timeout(kTimeoutMusicPeerBrowseId);
  const std::string &mode_s = rs.music.selected_category_mode;
  const int page = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
  std::string_view search_sv{};
  if (mode_s == "artist_albums") {
    search_sv = rs.music.drilldown_artist_name;
  } else if (mode_s == "current_album_tracks" || mode_s == "album_tracks" ||
             mode_s == "podcast_episodes" || mode_s == "lyrics" || mode_s == "bio") {
    search_sv = rs.music.drilldown_album_id;
  }
  const std::string_view alb_opt(pref_album_sort_order_cstr());
  const char *alb_sort =
      (alb_opt == "Newest on Top") ? "year_desc" : ((alb_opt == "Oldest on Top") ? "year" : "name");
  const std::string_view art_opt(pref_artist_album_sort_order_cstr());
  const char *art_sort =
      (art_opt == "Alphabetical") ? "name" : ((art_opt == "Newest on Top") ? "year_desc" : "year");
  const std::string_view pod_opt(pref_podcast_sort_order_cstr());
  const char *pod_sort = (pod_opt == "Oldest on Top") ? "oldest" : "newest";
  const int psize = music_browse_page_size_for(rs.music.view, rs.music.drilldown, mode_s);
  return latter_core::emit_music_browse(
      mode_s, page, search_sv, rs.music.active_speaker_entity,
      media_state().last_speaker, alb_sort, art_sort, pod_sort, psize, now_ms);
}

bool TouchGestureEngine::open_music_row_action_sheet_(int slot_idx, DeviceRuntimeState &rs, uint32_t now_ms) {
  if (slot_idx < 0 || slot_idx >= LATTER_SLOT_COUNT) return false;
  const auto &sl = rs.music_slots()[slot_idx];
  const bool is_queue = (rs.music.view == MusicView::TRACK &&
                         rs.music.drilldown == MusicDrilldown::TRACK_QUEUE);
  if (sl.name.empty() || sl.type == "text") return false;
  if (!is_queue && sl.id.empty()) return false;
  if (is_queue && sl.id.empty() && sl.queue_item_id.empty()) return false;

  rs.music.action_sheet_slot = static_cast<int8_t>(slot_idx);
  rs.music.action_sheet_is_queue = is_queue;
  rs.music.action_sheet_id = sl.id;
  rs.music.action_sheet_name = sl.name;
  rs.music.action_sheet_type = resolve_browse_item_media_type(rs, sl);
  rs.music.action_sheet_is_container =
      is_music_playable_container_type(rs.music.action_sheet_type) ||
      (rs.music.action_sheet_type == "podcast");
  rs.music.action_sheet_queue_item_id = sl.queue_item_id;
  rs.music.takeover = MusicTakeover::ACTION_SHEET;
  rs.music.takeover_interacted_ms = (now_ms == 0U) ? 1U : now_ms;
  return true;
}

bool TouchGestureEngine::handle_music_back_step_(DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms) {
  auto &m = rs.music;
  if (m.takeover != MusicTakeover::NONE) {
    m.takeover = MusicTakeover::NONE;
    m.hud_active = true;
    m.hud_timer = now_ms;
    mark_dirty_full();
    rt.schedule_disp();
    return true;
  }
  if (m.drilldown == MusicDrilldown::SPEAKER_ACTIONS) {
    m.drilldown = MusicDrilldown::SPEAKER_PICKER;
    m.target_speaker_id.clear();
    m.target_speaker_name.clear();
    m.hud_active = true;
    m.hud_timer = now_ms;
    mark_dirty_full();
    rt.schedule_disp();
    return true;
  }
  if (m.drilldown == MusicDrilldown::SPEAKER_PICKER ||
      m.drilldown == MusicDrilldown::TRACK_QUEUE ||
      m.drilldown == MusicDrilldown::TRACK_ALBUM ||
      m.drilldown == MusicDrilldown::TRACK_INFO) {
    cancel_core_timeout(kTimeoutMusicPeerBrowseId);
    if (m.direct_nav) {
      m.direct_nav = false;
      reset_music_to_now_playing(rs, now_ms, true);
      if (TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms)) return true;
      mark_dirty_full();
      rt.schedule_disp();
      return true;
    }
    m.drilldown = MusicDrilldown::NONE;
    m.target_speaker_id.clear();
    m.target_speaker_name.clear();
    if (m.view == MusicView::AUDIO) {
      m.takeover_interacted_ms = (now_ms == 0U) ? 1U : now_ms;
    } else {
      m.selected_category_mode = "track_menu";
      m.selected_category_title = "TRACK";
    }
    m.hud_active = true;
    m.hud_timer = now_ms;
    mark_dirty_full();
    rt.schedule_disp();
    return true;
  }
  if (m.drilldown == MusicDrilldown::LIB_LIST) {
    int &b_page = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
    if (m.selected_category_mode == "album_tracks" || m.selected_category_mode == "podcast_episodes") {
      m.selected_category_mode = m.drilldown_parent_mode.empty() ? "album" : m.drilldown_parent_mode;
      if (m.selected_category_mode == "podcast") m.selected_category_title = "PODCASTS";
      else if (m.selected_category_mode == "playlist") m.selected_category_title = "PLAYLISTS";
      else if (m.selected_category_mode == "artist_albums") m.selected_category_title = m.drilldown_artist_name;
      else m.selected_category_title = "ALBUMS";
      b_page = std::max(1, m.drilldown_album_page);
      reset_music_loading_slots(rs, "Loading...");
      TouchGestureEngine::fetch_music_browse_for_current_view_(rs, now_ms);
      mark_dirty_full();
      rt.schedule_disp();
      return true;
    }
    if (m.selected_category_mode == "artist_albums") {
      m.selected_category_mode = "artist";
      m.selected_category_title = "ARTISTS";
      b_page = std::max(1, m.drilldown_artist_page);
      reset_music_loading_slots(rs, "Loading...");
      TouchGestureEngine::fetch_music_browse_for_current_view_(rs, now_ms);
      mark_dirty_full();
      rt.schedule_disp();
      return true;
    }
    if (m.direct_nav) {
      m.direct_nav = false;
      reset_music_to_now_playing(rs, now_ms, true);
      if (TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms)) return true;
      mark_dirty_full();
      rt.schedule_disp();
      return true;
    }
    m.drilldown = MusicDrilldown::NONE;
    m.selected_category_mode = "categories";
    m.selected_category_title = "LIBRARY";
    m.drilldown_artist_page = 1;
    m.drilldown_album_page = 1;
    m.hud_active = true;
    m.hud_timer = now_ms;
    mark_dirty_full();
    rt.schedule_disp();
    return true;
  }
  if (m.direct_nav) {
    m.direct_nav = false;
    reset_music_to_now_playing(rs, now_ms, true);
    if (TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms)) return true;
    mark_dirty_full();
    rt.schedule_disp();
    return true;
  }
  return false;
}

void request_music_lyrics_refresh(const std::string &track_key, const std::string &speaker_entity,
                                  uint32_t now_ms) {
  cancel_core_timeout(kTimeoutMusicPeerBrowseId);
  latter_core::emit_music_lyrics_fetch(track_key, speaker_entity, now_ms);
}

void enter_music_drilldown(MusicView tab, MusicDrilldown drill, uint32_t now_ms) {
  cancel_core_timeout(kTimeoutMusicPeerBrowseId);
  auto &rs = runtime_state();
  const auto &rt = touch_engine().runtime();
  auto &m = rs.music;

  m.reset_overlays();
  m.clear_lyrics_auto_state();
  m.hud_active = true;
  m.hud_timer = now_ms;
  rs.nav.menu_scroll_active = false;
  m.view = tab;
  m.drilldown = drill;
  m.sync_carousel_for_view(layout().dock_slot_count);

  std::string t_title, t_artist, t_album;
  bool is_speech = false;
  resolve_music_track_strings(rs, rt, t_title, t_artist, t_album, is_speech);

  auto fetch_now = [&](const char *mode, std::string title, const char *loading_msg) {
    m.selected_category_mode = mode;
    m.selected_category_title = std::move(title);
    rs.slot_page_for(latter_rpc::SlotChannel::MUSIC) = 1;
    rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
    reset_music_loading_slots(rs, loading_msg);
    TouchGestureEngine::fetch_music_browse_for_current_view_(rs, now_ms);
  };

  switch (drill) {
    case MusicDrilldown::SPEAKER_PICKER:
      fetch_now("speakers", "SPEAKERS", "Loading...");
      break;
    case MusicDrilldown::TRACK_QUEUE:
      fetch_now("active_queue", "PLAYBACK QUEUE", "Loading queue...");
      break;
    case MusicDrilldown::TRACK_ALBUM:
      m.drilldown_album_id = !t_album.empty() ? t_album : t_artist;
      fetch_now(
          is_speech ? "podcast_episodes" : "current_album_tracks",
          t_album.empty() ? std::string(is_speech ? "EPISODES" : "ALBUM TRACKS") : t_album,
          is_speech ? "Loading episodes..." : "Loading album tracks...");
      break;
    case MusicDrilldown::TRACK_INFO:
      m.info_scroll_px = 0;
      m.drilldown_album_id = t_title + "|" + t_artist + "|" + t_album;
      fetch_now("bio", "TRACK INFO", "Loading...");
      break;
    default:
      break;
  }
}

void activate_music_peer_tab(MusicView target, uint32_t now_ms) {
  static constexpr uint32_t kMusicPeerBrowseDebounceMs = 380U;
  cancel_core_timeout(kTimeoutMusicPeerBrowseId);
  auto &rs = runtime_state();
  const auto &rt = touch_engine().runtime();
  auto &m = rs.music;

  m.reset_overlays();
  m.clear_lyrics_auto_state();
  m.hud_active = true;
  m.hud_timer = now_ms;
  rs.nav.menu_scroll_active = false;
  m.view = target;
  m.sync_carousel_for_view(layout().dock_slot_count);

  std::string t_title, t_artist, t_album;
  bool is_speech = false;
  resolve_music_track_strings(rs, rt, t_title, t_artist, t_album, is_speech);

  switch (target) {
    case MusicView::NOW_PLAYING:
      break;
    case MusicView::LYRICS: {
      auto &lyr = m.lyrics;
      lyr.manual_override_until_ms = 0U;
      const std::string key = build_music_track_key(t_title, t_artist);
      if (!key.empty() && lyr.track_key != key) {
        lyr.track_key.clear();
        lyr.loading = true;
        lyr.lines.clear();
        lyr.active_line_idx = -1;
        lyr.scroll_px = 0;
        lyr.target_scroll_px = 0;
        lyr.scroll_anim_start_ms = 0U;
        if (core_timeout_bridge().schedule_fn != nullptr) {
          schedule_core_timeout(kTimeoutMusicPeerBrowseId, kMusicPeerBrowseDebounceMs, [key]() {
            auto &rs_now = runtime_state();
            if (rs_now.nav.gui_page != PageId::MUSIC ||
                rs_now.music.view != MusicView::LYRICS ||
                rs_now.music.drilldown != MusicDrilldown::NONE ||
                rs_now.music.takeover != MusicTakeover::NONE) {
              return;
            }
            rs_now.music.lyrics.track_key = key;
            request_music_lyrics_refresh(
                key, TouchGestureEngine::resolve_music_speaker_entity_(rs_now),
                TouchGestureEngine::now_or_hal_ms_(last_user_touch_ms()));
          });
        } else {
          lyr.track_key = key;
          request_music_lyrics_refresh(key, TouchGestureEngine::resolve_music_speaker_entity_(rs), now_ms);
        }
      }
      break;
    }
    case MusicView::LIBRARY:
      m.selected_category_mode = "categories";
      m.selected_category_title = "LIBRARY";
      m.category_page = 1;
      m.drilldown_parent_mode.clear();
      break;
    case MusicView::AUDIO:
      m.takeover_interacted_ms = (now_ms == 0U) ? 1U : now_ms;
      break;
    case MusicView::TRACK:
    default:
      m.selected_category_mode = "track_menu";
      m.selected_category_title = "TRACK";
      break;
  }
}

bool TouchGestureEngine::handle_tap_music_(
    int /*tx*/, int /*ty*/, uint32_t now_rel, const HitRegion *hit, uint16_t wid) {
  auto &rs = runtime_state();
  if (rs.nav.gui_page != PageId::MUSIC) return false;

  auto &m = rs.music;
  auto &ms = media_state();
  const std::string spk = TouchGestureEngine::resolve_music_speaker_entity_(rs);
  const bool always_vis = (rt_.select_opt(UiSelectId::MUSIC_OVERLAY_STYLE) == "Always Visible");
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  auto return_now_playing_dirty = [&]() {
    reset_music_to_now_playing(rs, now_rel, true);
    mark_dirty_full();
    rt_.schedule_disp();
  };

  if (m.takeover != MusicTakeover::NONE) {
    m.takeover_interacted_ms = (now_rel == 0U) ? 1U : now_rel;
    if (wid == WidgetId::MUSIC_TAKEOVER_BACKDROP &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_DISMISS, tkey)) {
      m.takeover = MusicTakeover::NONE;
      m.hud_active = true;
      m.hud_timer = now_rel;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (m.takeover == MusicTakeover::SCRUB) {
      if ((wid == WidgetId::MUSIC_SCRUB_STEP_BACK || wid == WidgetId::MUSIC_SCRUB_STEP_FWD) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        const float cur_s = static_cast<float>(ms.interpolated_position_ms(now_rel)) * 0.001f;
        const float delta_s = (wid == WidgetId::MUSIC_SCRUB_STEP_FWD) ? 30.0f : -15.0f;
        float next_s = std::max(0.0f, cur_s + delta_s);
        if (!std::isnan(ms.duration) && ms.duration > 0.0f) {
          next_s = std::min(ms.duration, next_s);
        }
        ms.position = next_s;
        ms.position_updated_ms = now_rel;
        m.pos_base = next_s;
        m.pos_sync_time = now_rel;
        media_seek_absolute(now_rel);
        rt_.schedule_disp();
      } else if (wid == WidgetId::MUSIC_SHUFFLE_BTN &&
                 (ms.media_caps & kMediaCapShuffleSet) != 0U &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        ms.shuffle = !ms.shuffle;
        latter_core::emit_media_shuffle_toggle(spk);
        rt_.schedule_disp();
      } else if (wid == WidgetId::MUSIC_REPEAT_BTN &&
                 (ms.media_caps & kMediaCapRepeatSet) != 0U &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        ms.repeat_mode = static_cast<uint8_t>((ms.repeat_mode + 1U) % 3U);
        ms.repeat = (ms.repeat_mode != 0U);
        latter_core::emit_media_repeat_cycle(spk);
        rt_.schedule_disp();
      } else if (wid == WidgetId::MUSIC_FAVORITE_BTN &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        ms.favorite = !ms.favorite;
        latter_core::emit_media_favorite_toggle("", spk);
        m.action_feedback = ms.favorite ? "Saved to Favorites" : "Removed Favorite";
        m.feedback_timer = now_rel;
        rt_.schedule_disp();
      }
    } else if (m.takeover == MusicTakeover::ACTION_SHEET &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      if (m.action_sheet_is_queue) {
        const std::string qid = m.action_sheet_queue_item_id;
        if (wid == WidgetId::MUSIC_ACTION_PLAY_NOW) {
          if (!qid.empty()) {
            audio_session().set_play_intent(MusicPlayIntent::ExternalOrUserExplicit, now_rel);
            music_queue_cleared() = false;
            m.action_feedback = "Playing: " + m.action_sheet_name;
            m.feedback_timer = now_rel;
            latter_core::emit_media_queue_play(qid, spk);
          } else if (m.action_sheet_slot >= 0) {
            rt_.cmd(UiHostCmd::PLAY_MUSIC_SLOT, m.action_sheet_slot + 1);
          }
          return_now_playing_dirty();
        } else if ((wid == WidgetId::MUSIC_ACTION_PLAY_NEXT ||
                    wid == WidgetId::MUSIC_ACTION_MOVE_UP ||
                    wid == WidgetId::MUSIC_ACTION_MOVE_DOWN ||
                    wid == WidgetId::MUSIC_ACTION_REMOVE_QUEUE) && !qid.empty()) {
          if (wid == WidgetId::MUSIC_ACTION_PLAY_NEXT) {
            latter_core::emit_media_queue_move(qid, "next", spk);
            m.action_feedback = "Moved Next";
          } else if (wid == WidgetId::MUSIC_ACTION_MOVE_UP) {
            latter_core::emit_media_queue_move(qid, -1, spk);
            m.action_feedback = "Moved Up";
          } else if (wid == WidgetId::MUSIC_ACTION_MOVE_DOWN) {
            latter_core::emit_media_queue_move(qid, 1, spk);
            m.action_feedback = "Moved Down";
          } else {
            latter_core::emit_media_queue_delete(qid, spk);
            m.action_feedback = "Removed from Queue";
          }
          m.feedback_timer = now_rel;
          m.takeover = MusicTakeover::NONE;
          m.hud_timer = now_rel;
          TouchGestureEngine::fetch_music_browse_for_current_view_(rs, now_rel);
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else {
        const bool is_play_now = (wid == WidgetId::MUSIC_ACTION_PLAY_NOW);
        const bool is_next = (wid == WidgetId::MUSIC_ACTION_PLAY_NEXT);
        const bool is_add = (wid == WidgetId::MUSIC_ACTION_ADD_QUEUE);
        const bool is_rep = (wid == WidgetId::MUSIC_ACTION_REPLACE_QUEUE);
        const bool is_rad = (wid == WidgetId::MUSIC_ACTION_START_RADIO);
        if (is_play_now || is_next || is_add || is_rep || is_rad) {
          const char *enq = is_next ? "next" : (is_add ? "add" : (is_rep ? "replace" : "play"));
          TouchGestureEngine::dispatch_music_play_item_(m.action_sheet_id, m.action_sheet_type,
                                   m.action_sheet_name, enq, is_rad, rs, now_rel);
          if (is_play_now || is_rep || is_rad) {
            return_now_playing_dirty();
          } else {
            m.takeover = MusicTakeover::NONE;
            mark_dirty_full();
            rt_.schedule_disp();
          }
        }
      }
    }
    return true;
  }

  if (m.drilldown == MusicDrilldown::NONE &&
      (wid == WidgetId::MODE_BAR_LEFT || wid == WidgetId::MODE_BAR_RIGHT)) {
    if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      const int vis_cap = std::clamp(layout().dock_slot_count, 1, kMaxDockSlots);
      if (step_mode_bar_window_page(
              m.mode_bar_win_start, (wid == WidgetId::MODE_BAR_RIGHT) ? 1 : -1,
              kMusicPeerTabCount, vis_cap)) {
        m.hud_timer = now_rel;
        rt_.schedule_disp();
      }
    }
    return true;
  }
  if (m.drilldown == MusicDrilldown::NONE &&
      wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + kMaxDockSlots) {
    if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      m.direct_nav = false;
      activate_music_peer_tab(
          static_cast<MusicView>(std::clamp(static_cast<int>(hit->param), 0, kMusicPeerTabCount - 1)), now_rel);
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return true;
  }

  auto handle_transport_tap = [&]() -> bool {
    if (wid == WidgetId::MUSIC_PLAY_PAUSE) {
      if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const bool was_playing = is_music_playing_effective(!ms.is_local, rt_.local_mp_playing, ms.state, now_rel);
        m.action_feedback = was_playing ? "Paused" : "Playing";
        m.feedback_timer = now_rel;
        m.hud_timer = now_rel;
        rt_.cmd(UiHostCmd::TOGGLE_HA_MUSIC);
        rt_.schedule_disp();
      }
      return true;
    }
    if (wid == WidgetId::MUSIC_PREV_BTN || wid == WidgetId::MUSIC_NEXT_BTN) {
      if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        const bool is_next = (wid == WidgetId::MUSIC_NEXT_BTN);
        std::string t_title, t_artist, t_album;
        bool is_speech = false;
        resolve_music_track_strings(rs, rt_, t_title, t_artist, t_album, is_speech);
        if (is_speech) media_seek_relative(is_next ? 1 : -1, now_rel);
        else if (is_next) media_next_track(now_rel);
        else media_prev_track(now_rel);
        m.hud_timer = now_rel;
        rt_.schedule_disp();
      }
      return true;
    }
    return false;
  };

  if (m.view == MusicView::NOW_PLAYING) {
    const bool controls_shown = always_vis || m.hud_active;
    if (!controls_shown && wid != WidgetId::MUSIC_SCRUB_OPEN_BTN) {
      m.hud_active = true;
      m.hud_timer = now_rel;
      hud_just_woken_ms_ = now_rel;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::MUSIC_SCRUB_OPEN_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      m.pos_base = static_cast<float>(ms.interpolated_position_ms(now_rel)) * 0.001f;
      m.pos_sync_time = now_rel;
      m.takeover = MusicTakeover::SCRUB;
      m.takeover_interacted_ms = (now_rel == 0U) ? 1U : now_rel;
      m.hud_active = true;
      m.hud_timer = now_rel;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (controls_shown && wid == WidgetId::MUSIC_SPEAKER_PICKER_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      m.direct_nav = false;
      enter_music_drilldown(MusicView::AUDIO, MusicDrilldown::SPEAKER_PICKER, now_rel);
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (controls_shown && handle_transport_tap()) {
      return true;
    } else if ((wid == WidgetId::NONE || wid == WidgetId::MUSIC_ART_TAP) &&
               !always_vis && m.hud_active &&
               (now_rel - m.hud_timer >= TOUCH_GUARD_DISMISS) &&
               (now_rel - hud_just_woken_ms_ >= TOUCH_GUARD_DISMISS) &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_DISMISS, tkey)) {
      m.hud_active = false;
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return true;
  }

  if (m.view == MusicView::AUDIO && m.drilldown == MusicDrilldown::NONE) {
    if (wid == WidgetId::MUSIC_SPEAKER_PICKER_BTN &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      m.direct_nav = false;
      enter_music_drilldown(MusicView::AUDIO, MusicDrilldown::SPEAKER_PICKER, now_rel);
      mark_dirty_full();
      rt_.schedule_disp();
    } else if ((wid == WidgetId::MUSIC_VOL_UP_STEP || wid == WidgetId::MUSIC_VOL_DOWN_STEP) &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      const int dir = (wid == WidgetId::MUSIC_VOL_UP_STEP) ? 1 : -1;
      const bool was_muted = ms.muted;
      if (ms.muted && dir > 0) ms.muted = false;
      step_music_volume_feedback(dir, rt_.num_val(UiNumberId::VOLUME_STEP_SIZE),
                                 ms.is_local, rt_.local_mp_volume, now_rel, m);
      m.hud_timer = now_rel;
      rt_.cmd(UiHostCmd::MUSIC_STEP_VOLUME, dir);
      if (!was_muted) {
        int vy0 = 0, vy1 = SCREEN_H - 1;
        music_volume_dirty_band(vy0, vy1);
        rt_.schedule_band_disp(vy0, vy1);
      } else {
        mark_dirty_full();
        rt_.schedule_disp();
      }
    } else if (wid == WidgetId::MUSIC_VOL_MUTE_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      ms.muted = !ms.muted;
      m.hud_timer = now_rel;
      latter_core::emit_media_mute_toggle(spk);
      rt_.schedule_disp();
    }
    return true;
  }

  if (m.view == MusicView::LYRICS) {
    if (!m.hud_active) {
      if (wid == WidgetId::MUSIC_ART_TAP || wid == WidgetId::NONE) {
        m.hud_active = true;
        m.hud_timer = now_rel;
        hud_just_woken_ms_ = now_rel;
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return true;
    }
    if (handle_transport_tap()) return true;
    if (wid == WidgetId::MUSIC_LYRIC_SYNC_BTN &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      m.lyrics.manual_override_until_ms = 0U;
      m.lyrics.target_scroll_px = -1;
      m.hud_timer = now_rel;
      rt_.schedule_disp();
      return true;
    }
    if (wid >= WidgetId::MUSIC_LYRIC_LINE_BASE && wid < WidgetId::MUSIC_LYRIC_LINE_BASE + 8 &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      const int li = static_cast<int>(hit->param);
      if (li >= 0 && static_cast<size_t>(li) < m.lyrics.lines.size() &&
          m.lyrics.lines[static_cast<size_t>(li)].t_ms >= 0) {
        const int32_t t_ms = m.lyrics.lines[static_cast<size_t>(li)].t_ms;
        const float seek_s = static_cast<float>(t_ms) * 0.001f;
        ms.position = seek_s;
        ms.position_updated_ms = now_rel;
        m.pos_base = seek_s;
        m.pos_sync_time = now_rel;
        m.lyrics.manual_override_until_ms = 0U;
        m.lyrics.target_scroll_px = -1;
        m.hud_timer = now_rel;
        audio_session().set_play_intent(MusicPlayIntent::TrackAdvanceOrSeek, now_rel);
        latter_core::emit_media_seek(static_cast<int>(t_ms / 1000));
        rt_.schedule_disp();
      }
      return true;
    }
    if (wid == WidgetId::NONE && m.hud_active &&
        (now_rel - m.hud_timer >= TOUCH_GUARD_DISMISS) &&
        (now_rel - hud_just_woken_ms_ >= TOUCH_GUARD_DISMISS) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_DISMISS, tkey)) {
      m.hud_active = false;
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return true;
  }

  if (m.view == MusicView::TRACK && m.drilldown == MusicDrilldown::NONE) {
    if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + kMaxListSlots &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int idx = static_cast<int>(hit->param);
      if (idx == 0) {
        m.direct_nav = false;
        enter_music_drilldown(MusicView::TRACK, MusicDrilldown::TRACK_QUEUE, now_rel);
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (idx == 1) {
        m.direct_nav = false;
        enter_music_drilldown(MusicView::TRACK, MusicDrilldown::TRACK_ALBUM, now_rel);
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (idx == 2) {
        m.direct_nav = false;
        enter_music_drilldown(MusicView::TRACK, MusicDrilldown::TRACK_INFO, now_rel);
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
    return true;
  }

  if (m.view == MusicView::TRACK && m.drilldown == MusicDrilldown::TRACK_INFO) {
    return true;
  }

  int &b_page = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
  const int b_tot = std::max(1, rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC));
  auto step_peer_chevron = [&]() {
    const int next_pg = std::clamp(b_page + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, b_tot);
    m.hud_timer = now_rel;
    if (next_pg != b_page) {
      b_page = next_pg;
      TouchGestureEngine::fetch_music_browse_for_current_view_(rs, now_rel);
      rt_.schedule_disp();
    }
  };

  if (m.view == MusicView::AUDIO) {
    if (m.drilldown == MusicDrilldown::SPEAKER_ACTIONS && !m.target_speaker_id.empty()) {
      if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        if (wid == WidgetId::MUSIC_SPK_CONTROL_BTN) {
          rt_.cmd(UiHostCmd::SPEAKER_CONTROL_TARGET);
          return_now_playing_dirty();
        } else if (wid == WidgetId::MUSIC_SPK_TRANSFER_BTN) {
          rt_.cmd(UiHostCmd::SPEAKER_TRANSFER_TO_TARGET);
          return_now_playing_dirty();
        } else if (wid == WidgetId::MUSIC_SPK_GROUP_BTN) {
          rt_.cmd((m.target_speaker_id == spk) ? UiHostCmd::SPEAKER_UNJOIN_TARGET
                                               : UiHostCmd::SPEAKER_JOIN_TARGET);
          return_now_playing_dirty();
        }
      }
    } else if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      step_peer_chevron();
    } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + kMaxListSlots &&
               now_rel >= music_scroll_tap_lockout_until_ms() &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int spk_cap = std::clamp(layout().list_rows, 4, kMaxListSlots);
      const int si = std::clamp(static_cast<int>(hit->param), 0, spk_cap - 1);
      const auto &sl = rs.music_slots()[si];
      if (!sl.name.empty() && !sl.id.empty() && sl.type != "text") {
        m.target_speaker_id = sl.id;
        m.target_speaker_name = sl.name;
        m.drilldown = MusicDrilldown::SPEAKER_ACTIONS;
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
    return true;
  }

  if (m.view == MusicView::TRACK && m.drilldown == MusicDrilldown::TRACK_QUEUE) {
    if (wid == WidgetId::MUSIC_SHUFFLE_BTN && (ms.media_caps & kMediaCapShuffleSet) != 0U &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      ms.shuffle = !ms.shuffle;
      m.hud_timer = now_rel;
      latter_core::emit_media_shuffle_toggle(spk);
      rt_.schedule_disp();
    } else if (wid == WidgetId::MUSIC_REPEAT_BTN && (ms.media_caps & kMediaCapRepeatSet) != 0U &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      ms.repeat_mode = static_cast<uint8_t>((ms.repeat_mode + 1U) % 3U);
      ms.repeat = (ms.repeat_mode != 0U);
      m.hud_timer = now_rel;
      latter_core::emit_media_repeat_cycle(spk);
      rt_.schedule_disp();
    } else if (wid == WidgetId::MUSIC_QUEUE_CLEAR &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rt_.cmd(UiHostCmd::CLEAR_HA_MUSIC_QUEUE);
      reset_music_loading_slots(rs, "Queue Cleared");
      rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
      music_queue_cleared() = true;
      m.hud_timer = now_rel;
      rt_.schedule_disp();
    } else if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      step_peer_chevron();
    } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + kMaxListSlots &&
               now_rel >= music_scroll_tap_lockout_until_ms() &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int q_cap = std::clamp(layout().queue_list_rows, 3, kMaxListSlots);
      const int si = std::clamp(static_cast<int>(hit->param), 0, q_cap - 1);
      const auto &sl = rs.music_slots()[si];
      if (!sl.queue_item_id.empty()) {
        audio_session().set_play_intent(MusicPlayIntent::ExternalOrUserExplicit, now_rel);
        music_queue_cleared() = false;
        m.action_feedback = "Playing: " + sl.name;
        m.feedback_timer = now_rel;
        latter_core::emit_media_queue_play(sl.queue_item_id, spk);
        return_now_playing_dirty();
      } else if (!sl.id.empty() && sl.type != "text") {
        rt_.cmd(UiHostCmd::PLAY_MUSIC_SLOT, si + 1);
        return_now_playing_dirty();
      }
    }
    return true;
  }

  if (m.view == MusicView::LIBRARY ||
      (m.view == MusicView::TRACK && m.drilldown == MusicDrilldown::TRACK_ALBUM)) {
    const bool on_cat_root = (m.view == MusicView::LIBRARY) && (m.drilldown == MusicDrilldown::NONE);
    if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      m.hud_timer = now_rel;
      if (on_cat_root) {
        const int cat_per_pg = std::clamp(layout().peer_list_rows, 4, kMaxListSlots);
        const int cat_max_pg = std::max(1, (7 + cat_per_pg - 1) / cat_per_pg);
        m.category_page = std::clamp(m.category_page + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, cat_max_pg);
        rt_.schedule_disp();
      } else if (b_tot > 1) {
        const int step_d = (wid == WidgetId::CHEVRON_UP) ? -1 : 1;
        const bool wrap_l = (m.drilldown == MusicDrilldown::LIB_LIST) &&
                            (m.selected_category_mode == "artist" || m.selected_category_mode == "album");
        const int next_pg = wrap_l ? step_wrap_1based(b_page, step_d, b_tot)
                                   : std::clamp(b_page + step_d, 1, b_tot);
        if (next_pg != b_page) {
          b_page = next_pg;
          TouchGestureEngine::fetch_music_browse_for_current_view_(rs, now_rel);
          rt_.schedule_disp();
        }
      }
    } else if (on_cat_root &&
               wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + kMaxListSlots &&
               now_rel >= music_scroll_tap_lockout_until_ms() &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int idx = static_cast<int>(hit->param);
      m.drilldown_parent_mode.clear();
      if (idx == 0) { m.selected_category_mode = "favorites"; m.selected_category_title = "FAVORITES"; }
      else if (idx == 1) { m.selected_category_mode = "playlist"; m.selected_category_title = "PLAYLISTS"; }
      else if (idx == 2) { m.selected_category_mode = "artist"; m.selected_category_title = "ARTISTS"; m.drilldown_artist_page = 1; }
      else if (idx == 3) { m.selected_category_mode = "album"; m.selected_category_title = "ALBUMS"; m.drilldown_album_page = 1; }
      else if (idx == 4) { m.selected_category_mode = "podcast"; m.selected_category_title = "PODCASTS"; }
      else if (idx == 5) { m.selected_category_mode = "radio"; m.selected_category_title = "RADIO"; }
      else if (idx == 6) { m.selected_category_mode = "audiobook"; m.selected_category_title = "AUDIOBOOKS"; }
      if (idx >= 0 && idx <= 6) {
        m.drilldown = MusicDrilldown::LIB_LIST;
        reset_music_loading_slots(rs, "Loading...");
        b_page = 1;
        rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
        TouchGestureEngine::fetch_music_browse_for_current_view_(rs, now_rel);
        mark_dirty_full();
        rt_.schedule_disp();
      }
    } else if (wid >= WidgetId::MUSIC_ROW_PLAY_BASE && wid < WidgetId::MUSIC_ROW_PLAY_BASE + kMaxListSlots &&
               now_rel >= music_scroll_tap_lockout_until_ms() &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int si = std::clamp(static_cast<int>(hit->param), 0, kMaxListSlots - 1);
      const auto &sl = rs.music_slots()[si];
      if (!sl.id.empty() && sl.type != "text") {
        TouchGestureEngine::dispatch_music_play_item_(sl.id, resolve_browse_item_media_type(rs, sl), sl.name, "play", false, rs, now_rel);
        return_now_playing_dirty();
      }
    } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + kMaxListSlots &&
               now_rel >= music_scroll_tap_lockout_until_ms() &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int si = std::clamp(static_cast<int>(hit->param), 0, kMaxListSlots - 1);
      const auto &sl = rs.music_slots()[si];
      if (!sl.id.empty() && sl.type != "text") {
        const std::string mtype = resolve_browse_item_media_type(rs, sl);
        auto enter_lib_sub = [&](const char *next_mode, const char *loading_msg) {
          m.drilldown = MusicDrilldown::LIB_LIST;
          m.selected_category_mode = next_mode;
          m.selected_category_title = sl.name;
          reset_music_loading_slots(rs, loading_msg);
          b_page = 1;
          TouchGestureEngine::fetch_music_browse_for_current_view_(rs, now_rel);
          mark_dirty_full();
          rt_.schedule_disp();
        };
        if (m.selected_category_mode == "podcast" || mtype == "podcast") {
          m.drilldown_album_id = sl.id;
          m.drilldown_album_page = b_page;
          m.drilldown_parent_mode = m.selected_category_mode;
          enter_lib_sub("podcast_episodes", "Loading episodes...");
        } else if (m.selected_category_mode == "artist" || mtype == "artist") {
          m.drilldown_artist_name = sl.name;
          m.drilldown_artist_page = b_page;
          enter_lib_sub("artist_albums", "Loading albums...");
        } else if (m.selected_category_mode == "album" ||
                   m.selected_category_mode == "artist_albums" ||
                   m.selected_category_mode == "playlist" ||
                   mtype == "album" || mtype == "playlist") {
          m.drilldown_album_id = sl.id;
          m.drilldown_album_page = b_page;
          m.drilldown_parent_mode = m.selected_category_mode;
          enter_lib_sub("album_tracks", "Loading tracks...");
        } else {
          TouchGestureEngine::dispatch_music_play_item_(sl.id, mtype, sl.name, "play", false, rs, now_rel);
          return_now_playing_dirty();
        }
      }
    }
  }

  return true;
}

}  // namespace latter_ui
