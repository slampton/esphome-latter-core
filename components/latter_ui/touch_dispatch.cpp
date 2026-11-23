// ==============================================================================
// Latter Compiled On-Device Page, Navigation & Takeover Touch Dispatch
// (components/latter_ui/touch_dispatch.cpp)
// ==============================================================================

#include "ui_touch_engine.h"
#include "ui_pages.h"
#include "ui_touch_recognizer.h"
#include "audio_router.h"
#include "hal_display_lifecycle.h"
#include "hal_panel_power.h"
#include "hal_sleep_pm.h"
#include "latter_actions.h"
#include "latter_prof.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef USE_ESP32
#include <esp_random.h>
#endif

namespace latter_ui {

namespace {

inline bool is_newer_semver(const std::string &lat_str, const char *cur_cstr) noexcept {
  if (lat_str.empty() || lat_str == "unavailable" || lat_str == "unknown") return false;
  auto parse_ver = [](const char *raw, int &maj, int &min_v, int &pat) noexcept -> bool {
    if (raw == nullptr || *raw == '\0') return false;
    const char *p = (*raw == 'v' || *raw == 'V') ? (raw + 1) : raw;
    char *endp = nullptr;
    maj = static_cast<int>(std::strtol(p, &endp, 10));
    if (endp == p || *endp != '.') return false;
    p = endp + 1;
    min_v = static_cast<int>(std::strtol(p, &endp, 10));
    if (endp == p) return false;
    pat = (*endp == '.') ? static_cast<int>(std::strtol(endp + 1, &endp, 10)) : 0;
    return true;
  };
  const char *lp = lat_str.c_str();
  if (*lp == 'v' || *lp == 'V') ++lp;
  if (std::strncmp(lp, "202", 3) == 0) return false;
  int l_maj = 0, l_min = 0, l_pat = 0, c_maj = 0, c_min = 0, c_pat = 0;
  if (!parse_ver(lat_str.c_str(), l_maj, l_min, l_pat) ||
      !parse_ver(cur_cstr != nullptr ? cur_cstr : "", c_maj, c_min, c_pat)) {
    return false;
  }
  if (l_maj != c_maj) return l_maj > c_maj;
  if (l_min != c_min) return l_min > c_min;
  return l_pat > c_pat;
}

inline int step_brightness_tap_10pct(int cur_pct) noexcept {
  if (cur_pct < 10 || cur_pct >= 100) return 10;
  return std::min(100, ((cur_pct / 10) + 1) * 10);
}

inline void record_stopwatch_lap(StopwatchRuntimeState &sw, uint32_t cur_ms) noexcept {
  sw.record_lap_elapsed(cur_ms);
}

void reset_music_loading_slots(DeviceRuntimeState &rs, const char *msg);
void resolve_music_track_strings(const DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                                 std::string &out_title, std::string &out_artist,
                                 std::string &out_album, bool &out_speech);
void reset_music_to_now_playing(DeviceRuntimeState &rs, uint32_t now_ms, bool show_hud = true);
void do_select_active_area_latter();
void stop_broadcast_dictation_if_active(DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms, const char *msg = "");
void ensure_calendar_window_for_ymd(int32_t anchor_ymd, const PersistedDevicePrefs &prefs, bool force = false);
void stop_calendar_dictation_if_active(DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms, const char *msg = "");
void cycle_music_pref_slot(const UiTouchRuntime &rt, int slot);
void leave_home_hub_cameras_if_active(DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms);
void enter_cameras_app(int target_tab, std::string_view focus_entity, bool start_listen,
                       DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms);


inline void reset_common_nav_fields(DeviceRuntimeState &rs, uint32_t now_ms) {
  shortcut_confirm_state().clear();
  rs.nav.quick_actions_open = false;
  rs.nav.shortcut_origin = ShortcutOrigin::NONE;
  timer_view_slot() = 0;
  rs.alarms.edit_idx = 0;
  rs.cal.drilldown = CalDrilldown::NONE;
  rs.cal.delete_confirm_open = false;
  cam_quick_reply_open() = false;
  cam_expanded_fullscreen() = false;
  reset_music_to_now_playing(rs, now_ms, false);
  rs.nav.system_menu_mode = 0;
  rs.nav.active_game = 0;
  game_input().clear();
  rs.music.direct_nav = false;
  rs.music.clear_lyrics_auto_state();
  rs.nav.sleep_menu_page = 1;
  rs.nav.display_menu_page = 1;
  rs.nav.system_dashboard_page = 1;
  rs.nav.menu_scroll_active = false;
  rs.music.hud_active = false;
  rs.nav.remote_subpage = 0;
  rs.nav.tools_subpage = 0;
  rs.nav.tools_menu_page = 0;
  rs.nav.pinned_menu_page = 0;
  rs.nav.sky_weather_subpage = 0;
  rs.nav.home_settings_page = 1;
}


inline HomeActivityItem *touch_activity_fallback_scratch() {
  static std::vector<HomeActivityItem> s_buf(static_cast<size_t>(kMaxHomeActivity));
  return s_buf.data();
}

inline void do_send_intercom_broadcast() {
  latter_actions::emit_intercom_broadcast(runtime_state().intercom.broadcast_message);
}
void do_select_active_area_latter() {
  latter_actions::emit_set_active_area(runtime_state().area.active_area_id);
}

void stop_broadcast_dictation_if_active(DeviceRuntimeState &rs,
                                        const UiTouchRuntime &rt,
                                        uint32_t now_ms,
                                        const char *msg) {
  if (!broadcast_dictation_active(rs)) return;
  cancel_broadcast_dictation(msg, now_ms, rs);
  rs.voice.intercom_turn = false;
  rt.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
  rt.apply_power_policy();
  rt.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
}


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
  rs.music.reset_overlays();
  rs.music.clear_lyrics_auto_state();
  rs.music.view = MusicView::NOW_PLAYING;
  rs.music.sync_carousel_for_view();
  mode_bar_anim().snap(0, 0);
  rs.music.hud_active = show_hud;
  rs.music.hud_timer = now_ms;
}


inline CalendarSourceMeta *touch_cal_sources_scratch() {
  static std::vector<CalendarSourceMeta> s_buf(static_cast<size_t>(kMaxCalendarSources));
  return s_buf.data();
}

inline void open_calendar_composer(DeviceRuntimeState &rs,
                                   const UiTouchRuntime &rt,
                                   int32_t today_ymd,
                                   uint32_t now_ms) {
  stop_calendar_dictation_if_active(rs, rt, now_ms);
  rs.cal.return_subpage = rs.cal.subpage;
  rs.cal.drilldown = CalDrilldown::ADD_STEP1;
  rs.cal.delete_confirm_open = false;
  rs.cal.picker_page = 0;
  rs.cal.draft_ymd = (rs.cal.subpage == CalendarSubpage::OVERVIEW || rs.cal.selected_ymd <= 0)
                         ? today_ymd
                         : rs.cal.selected_ymd;
  if (rs.cal.draft_title.empty()) {
    rs.cal.draft_title = "Reminder";
  }
  const int next_h = (rt.time_valid && rt.time_hour >= 0) ? std::clamp(rt.time_hour + 1, 8, 22) : 9;
  rs.cal.draft_start_min = static_cast<int16_t>(next_h * 60);
  rs.cal.draft_end_min = static_cast<int16_t>(std::min(1425, (next_h + 1) * 60));
  rs.cal.draft_time_field = 0U;
}

inline void submit_calendar_draft_event(DeviceRuntimeState &rs,
                                        const PersistedDevicePrefs &prefs,
                                        uint32_t now_ms) {
  CalendarSourceMeta *writable_sources = touch_cal_sources_scratch();
  const int writable_cnt = collect_writable_calendar_sources(
      calendar_state().calendars_meta, writable_sources, nullptr);
  std::string_view cal_ent{};
  if (writable_cnt > 0) {
    const int w_idx = std::clamp<int>(rs.cal.draft_cal_idx, 0, writable_cnt - 1);
    cal_ent = writable_sources[w_idx].entity_id;
  }
  const std::string_view title_sv =
      rs.cal.draft_title.empty() ? std::string_view("Reminder") : std::string_view(rs.cal.draft_title);
  const int r_idx = std::clamp<int>(rs.cal.draft_rrule_idx, 0, 6);
  const std::string_view rrule_sv = kCalRepeatRrules[r_idx];
  const int32_t ev_ymd = (rs.cal.draft_ymd > 0) ? rs.cal.draft_ymd : 20260101;

  if (rs.cal.draft_all_day) {
    char s_date[12];
    char e_date[12];
    format_ymd_iso(ev_ymd, s_date);
    format_ymd_iso(add_days_ymd(ev_ymd, 1), e_date);
    latter_actions::emit_calendar_create_event(
        cal_ent, title_sv, true, s_date, e_date, rs.cal.draft_location, rrule_sv);
  } else {
    const int s_min = std::clamp<int>(rs.cal.draft_start_min, 0, 1425);
    const int e_min = std::clamp<int>(std::max<int>(rs.cal.draft_end_min, s_min + 15), 15, 1440);
    char s_date[12];
    char e_date[12];
    format_ymd_iso(ev_ymd, s_date);
    const int32_t end_ymd = (e_min >= 1440) ? add_days_ymd(ev_ymd, 1) : ev_ymd;
    const int eff_e_min = (e_min >= 1440) ? 0 : e_min;
    format_ymd_iso(end_ymd, e_date);
    char s_dt[32];
    char e_dt[32];
    std::snprintf(s_dt, sizeof(s_dt), "%sT%02d:%02d:00", s_date, s_min / 60, s_min % 60);
    std::snprintf(e_dt, sizeof(e_dt), "%sT%02d:%02d:00", e_date, eff_e_min / 60, eff_e_min % 60);
    latter_actions::emit_calendar_create_event(
        cal_ent, title_sv, false, s_dt, e_dt, rs.cal.draft_location, rrule_sv);
  }

  rs.cal.selected_ymd = ev_ymd;
  rs.cal.drilldown = CalDrilldown::NONE;
  rs.cal.delete_confirm_open = false;
  rs.cal.feedback_text = std::string("Added: ") + std::string(title_sv);
  rs.cal.feedback_ms = now_ms;
  ensure_calendar_window_for_ymd(ev_ymd, prefs, true);
}

inline void toggle_timer_slot_run(MultiTimerSlot &s, uint32_t now_ms, const UiTouchRuntime &rt) {
  if (!s.running && !s.paused) {
    s.running = true;
    s.paused = false;
    s.remaining_sec = s.duration_sec;
    s.target_ms = now_ms + static_cast<uint32_t>(s.duration_sec) * 1000U;
  } else if (s.running) {
    s.running = false;
    s.paused = true;
    s.remaining_sec = (s.target_ms > now_ms)
                          ? static_cast<int>((s.target_ms - now_ms + 999U) / 1000U)
                          : 0;
  } else {
    s.running = true;
    s.paused = false;
    s.target_ms = now_ms + static_cast<uint32_t>(s.remaining_sec) * 1000U;
  }
  sync_multi_timer_state();
  rt.apply_power_policy();
  rt.schedule_disp();
}

void ensure_calendar_window_for_ymd(int32_t anchor_ymd,
                                    const PersistedDevicePrefs &prefs,
                                    bool force) {
  auto &store = calendar_window_store();
  const int lookahead = static_cast<int>(snap_calendar_lookahead_days(prefs.calendar_lookahead_days));
  const bool out_of_bounds =
      (store.window_start_ymd == 0) ||
      (anchor_ymd < add_days_ymd(store.window_start_ymd, 14)) ||
      (anchor_ymd > add_days_ymd(store.window_end_ymd, -21));
  if (!force && !out_of_bounds) return;

  const int32_t want_start = add_days_ymd(anchor_ymd, -35);
  const int32_t want_end = add_days_ymd(anchor_ymd, std::max(62, lookahead + 7));
  char s_iso[12];
  char e_iso[12];
  format_ymd_iso(want_start, s_iso);
  format_ymd_iso(want_end, e_iso);
  store.loading = true;
  latter_actions::emit_calendar_fetch_window(s_iso, e_iso, lookahead);
}

void stop_calendar_dictation_if_active(DeviceRuntimeState &rs,
                                       const UiTouchRuntime &rt,
                                       uint32_t now_ms,
                                       const char *msg) {
  if (!cal_dictation_active(rs)) return;
  cancel_calendar_dictation(msg, now_ms, rs);
  rs.voice.intercom_turn = false;
  rt.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
  rt.apply_power_policy();
  rt.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
}


void leave_home_hub_cameras_if_active(DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                                      uint32_t now_ms) {
  if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
    intercom_session().stop_call_with_hw(now_ms);
    if (cam_listen_active()) {
      cam_listen_active() = false;
      rs.intercom.cam_action = "listen_stop";
      intercom_session().emit_current_cmd();
      rt.cmd(UiHostCmd::STOP_DOORBELL_AUDIO);
    }
  }
  rs.cameras.quick_reply_open = false;
  rs.cameras.doorbell_live_popup_active = false;
  cam_expanded_fullscreen() = false;
  rt.cmd(UiHostCmd::STOP_CAMERA_STREAM);
  rt.cmd(UiHostCmd::RELEASE_CAMERA_ART);
  rs.alert.loaded_camera_url.clear();
  rt.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
}

void enter_cameras_app(int target_tab, std::string_view focus_entity, bool start_listen,
                       DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms) {
  const auto &prefs = persisted_prefs();
  const auto &ac = area_ctx_state();
  CameraRecord cams[MAX_CAMERA_SLOTS]{};
  const int cam_cnt = parse_cameras_records(ac.cameras_summary, cams, MAX_CAMERA_SLOTS);

  rs.nav.gui_page = PageId::CAMERAS;
  rs.nav.home_subpage = 0;
  rs.nav.tools_subpage = 0;
  rs.nav.remote_subpage = 0;
  rs.nav.active_game = 0;
  rs.cameras.quick_reply_open = false;
  rs.cameras.doorbell_live_popup_active = false;
  rs.cameras.hud_active = true;
  rs.cameras.hud_timer = now_ms;
  rs.cameras.carousel_last_advance_ms = now_ms;

  if (!focus_entity.empty()) {
    const int idx = find_camera_index_by_entity(cams, cam_cnt, focus_entity);
    if (idx >= 0) {
      rs.cameras.selected_idx = idx;
    }
  } else if (cam_cnt > 0) {
    rs.cameras.selected_idx = std::clamp(rs.cameras.selected_idx, 0, cam_cnt - 1);
  } else {
    rs.cameras.selected_idx = 0;
  }

  int chosen_tab = target_tab;
  if (chosen_tab < 0) {
    if (prefs.camera_default_view == 1U) chosen_tab = CamerasTab::SINGLE;
    else if (prefs.camera_default_view == 2U) chosen_tab = CamerasTab::CAROUSEL;
    else chosen_tab = CamerasTab::GRID;
  }
  if (chosen_tab == CamerasTab::GRID && cam_cnt <= 1) {
    chosen_tab = CamerasTab::SINGLE;
  }
  rs.cameras.tab = std::clamp(chosen_tab, CamerasTab::GRID, CamerasTab::SETTINGS);
  mode_bar_anim().snap(0, rs.cameras.tab);

  if (start_listen) {
    intercom_session().start_call(now_ms);
  }

  cam_error_backoff_until_ms() = 0;
  last_cam_live_fetch_ms() = now_ms;
  if (rs.cameras.tab == CamerasTab::SETTINGS) {
    rt.cmd(UiHostCmd::STOP_CAMERA_STREAM);
  } else {
    rt.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
    if (prefs.camera_stream_mode == 2U || ac.camera_stream_url.empty()) {
      rt.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
    }
  }
}


inline void do_save_custom_color_to_latter() {
  save_persisted_prefs();
  latter_actions::emit_save_custom_color_rgb(persisted_prefs().custom_theme_rgb);
}

inline void do_remote_select_slot(int slot_num) {
  if (!remote_state().slots_summary.empty()) {
    latter_actions::emit_remote_action(slot_num, "select_slot");
  }
}

inline uint32_t next_rng_u32() {
#ifdef USE_ESP32
  return esp_random();
#else
  return static_cast<uint32_t>(std::rand());
#endif
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

}  // namespace


void TouchGestureEngine::launch_dock_app_(int chosen_app, DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms) {
  rs.nav.shortcut_origin = ShortcutOrigin::NONE;
  shortcut_confirm_state().clear();
  rs.nav.quick_actions_open = false;
  home_dock_page() = 0;
  if (chosen_app != PageId::MUSIC) {
    rs.music.clear_lyrics_auto_state();
  }

  if (chosen_app == PageId::HOME_HUB) {
    TouchGestureEngine::enter_home_hub_tab_(-1, false, rs, rt, now_ms);
  } else if (chosen_app == PageId::CAMERAS) {
    stop_calendar_dictation_if_active(rs, rt, now_ms);
    stop_broadcast_dictation_if_active(rs, rt, now_ms);
    enter_cameras_app(-1, "", false, rs, rt, now_ms);
  } else {
    if (is_camera_view_active(rs)) {
      leave_home_hub_cameras_if_active(rs, rt, now_ms);
    }
    stop_calendar_dictation_if_active(rs, rt, now_ms);
    stop_broadcast_dictation_if_active(rs, rt, now_ms);
    rs.nav.gui_page = chosen_app;
    rs.nav.home_subpage = 0;
  }

  if (chosen_app == PageId::PINNED) {
    rs.nav.pinned_menu_page = 0;
    rs.nav.menu_scroll_active = false;
  } else if (chosen_app == PageId::CALENDAR) {
    const int32_t today_ymd = TouchGestureEngine::resolve_touch_today_ymd_(rt);
    rs.cal.subpage = CalendarSubpage::OVERVIEW;
    rs.cal.drilldown = CalDrilldown::NONE;
    rs.cal.delete_confirm_open = false;
    rs.cal.selected_ymd = today_ymd;
    rs.cal.overview_page = 0;
    rs.cal.day_page = 0;
    rs.cal.week_page = 0;
    rs.cal.picker_page = 0;
    ensure_calendar_window_for_ymd(today_ymd, persisted_prefs(),
                                   calendar_window_store().window_start_ymd == 0);
  } else if (chosen_app == PageId::MUSIC) {
    reset_music_to_now_playing(rs, now_ms, true);
    mode_bar_anim().snap(0, 0);
    const auto &ms = media_state();
    if (rt.select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE) != "None" &&
        (!ms.is_local || rt.album_art_w == 0) && ms.art_url != rs.music.loaded_art_url) {
      rt.cmd(UiHostCmd::FETCH_REMOTE_ART_DEBOUNCED);
    }
  } else if (chosen_app == PageId::TIME) {
    rs.nav.time_subpage = TimeSubpage::CLOCK;
    rs.nav.world_clock_page = 0;
    rs.nav.alarm_edit_field = 1;
    timer_view_slot() = 0;
    rs.alarms.edit_idx = 0;
  } else if (chosen_app == PageId::WEATHER) {
    rs.nav.sky_weather_subpage = 0;
  } else if (chosen_app == PageId::GAMES) {
    rs.nav.game_carousel_idx = 0;
    rs.nav.menu_scroll_active = false;
  } else if (chosen_app == PageId::TOOLS) {
    rs.nav.tools_menu_page = 0;
    rs.nav.menu_scroll_active = false;
  } else if (chosen_app == PageId::SYSTEM) {
    rs.nav.system_menu_mode = 0;
    rs.nav.system_dashboard_page = 1;
    rs.nav.sleep_menu_page = 1;
    rs.nav.display_menu_page = 1;
    rs.nav.menu_scroll_active = false;
  } else if (chosen_app == PageId::AREA) {
    area_cat_dock_page() = 0;
    rs.area.domain_filter = resolve_preferred_area_category();
    if (rs.area.active_area_id.empty()) {
      rs.nav.area_subpage = AreaSubpage::PICKER;
      rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST) = 1;
      TouchGestureEngine::do_fetch_area_list_browse_();
    } else {
      rs.nav.area_subpage = AreaSubpage::OVERVIEW;
      rs.area.browse_mode = "entities";
      rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
      if (rs.area.domain_filter.empty()) {
        auto *eslots = rs.slots_for(latter_rpc::SlotChannel::AREA_ENTITIES);
        for (size_t si = 0; si < DeviceRuntimeState::kSlotsPerPage; ++si) {
          eslots[si] = BrowseSlotItem{};
        }
        rs.slot_total_pages_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
      } else {
        TouchGestureEngine::do_fetch_area_browse_();
      }
    }
  }

  rs.nav.tools_subpage = 0;
  rs.nav.remote_subpage = 0;
  rs.nav.active_game = 0;
  mark_dirty_full();
  rt.schedule_disp();
}

bool TouchGestureEngine::return_from_shortcut_origin_(DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms) {
  const ShortcutOrigin orig = rs.nav.shortcut_origin;
  if (orig == ShortcutOrigin::NONE) return false;
  rs.nav.shortcut_origin = ShortcutOrigin::NONE;
  if (orig == ShortcutOrigin::PINNED_APP) {
    const int ret_pg = rs.nav.shortcut_origin_pinned_page;
    rs.nav.tools_subpage = 0;
    rs.nav.remote_subpage = 0;
    rs.nav.active_game = 0;
    rs.nav.game_btn_pressed = false;
    rs.nav.system_menu_mode = 0;
    rs.nav.home_subpage = 0;
    reset_music_to_now_playing(rs, now_ms, false);
    rs.music.direct_nav = false;
    rs.nav.menu_scroll_active = false;
    rs.nav.gui_page = PageId::PINNED;
    rs.nav.pinned_menu_page = std::clamp(ret_pg, 0, std::max(0, pinned_total_pages() - 1));
    mark_dirty_full();
    rt.schedule_disp();
    return true;
  }
  touch_engine().navigate_home(now_ms);
  return true;
}

void TouchGestureEngine::launch_shortcut_route_(RouteId route_id, ShortcutOrigin origin,
                           DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                           uint32_t now_ms) {
  if (!is_route_supported_on_board(route_id)) return;
  const int saved_pinned_pg = rs.nav.pinned_menu_page;
  shortcut_confirm_state().clear();
  rs.nav.quick_actions_open = false;
  auto save_origin = [&]() {
    rs.nav.shortcut_origin = origin;
    rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
  };

  switch (route_id) {
    case RouteId::APP_PINNED:   TouchGestureEngine::launch_dock_app_(PageId::PINNED, rs, rt, now_ms); return;
    case RouteId::APP_MUSIC:    TouchGestureEngine::launch_dock_app_(PageId::MUSIC, rs, rt, now_ms); return;
    case RouteId::APP_HOME_HUB: TouchGestureEngine::launch_dock_app_(PageId::HOME_HUB, rs, rt, now_ms); return;
    case RouteId::APP_AREA:     TouchGestureEngine::launch_dock_app_(PageId::AREA, rs, rt, now_ms); return;
    case RouteId::APP_TOOLS:    TouchGestureEngine::launch_dock_app_(PageId::TOOLS, rs, rt, now_ms); return;
    case RouteId::APP_GAMES:    TouchGestureEngine::launch_dock_app_(PageId::GAMES, rs, rt, now_ms); return;
    case RouteId::SETTINGS_ROOT: TouchGestureEngine::launch_dock_app_(PageId::SYSTEM, rs, rt, now_ms); return;

    case RouteId::APP_CAMERAS:
      TouchGestureEngine::launch_dock_app_(PageId::CAMERAS, rs, rt, now_ms);
      save_origin();
      return;

    case RouteId::CAMERAS_GRID:
    case RouteId::CAMERAS_SINGLE:
    case RouteId::CAMERAS_CAROUSEL:
    case RouteId::CAMERAS_SETTINGS: {
      int ctab = CamerasTab::GRID;
      if (route_id == RouteId::CAMERAS_SINGLE) ctab = CamerasTab::SINGLE;
      else if (route_id == RouteId::CAMERAS_CAROUSEL) ctab = CamerasTab::CAROUSEL;
      else if (route_id == RouteId::CAMERAS_SETTINGS) ctab = CamerasTab::SETTINGS;
      enter_cameras_app(ctab, "", false, rs, rt, now_ms);
      save_origin();
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::APP_REMOTE:
      if (has_any_configured_remote()) TouchGestureEngine::launch_dock_app_(PageId::REMOTE, rs, rt, now_ms);
      return;

    case RouteId::APP_WEATHER:
    case RouteId::WEATHER_CURRENT:
    case RouteId::WEATHER_FORECAST:
    case RouteId::WEATHER_SUN:
    case RouteId::WEATHER_MOON:
      TouchGestureEngine::launch_dock_app_(PageId::WEATHER, rs, rt, now_ms);
      if (route_id == RouteId::WEATHER_FORECAST) rs.nav.sky_weather_subpage = 1;
      else if (route_id == RouteId::WEATHER_SUN) rs.nav.sky_weather_subpage = 2;
      else if (route_id == RouteId::WEATHER_MOON) rs.nav.sky_weather_subpage = 3;
      return;

    case RouteId::APP_TIME:
    case RouteId::TIME_CLOCK:
    case RouteId::TIME_TIMERS:
    case RouteId::TIME_ALARMS:
    case RouteId::TIME_STOPWATCH:
      TouchGestureEngine::launch_dock_app_(PageId::TIME, rs, rt, now_ms);
      if (route_id == RouteId::TIME_TIMERS) rs.nav.time_subpage = TimeSubpage::TIMERS;
      else if (route_id == RouteId::TIME_ALARMS) rs.nav.time_subpage = TimeSubpage::ALARMS;
      else if (route_id == RouteId::TIME_STOPWATCH) rs.nav.time_subpage = TimeSubpage::STOPWATCH;
      return;

    case RouteId::APP_CALENDAR:
    case RouteId::CALENDAR_OVERVIEW:
    case RouteId::CALENDAR_DAY:
    case RouteId::CALENDAR_WEEK:
    case RouteId::CALENDAR_MONTH:
      TouchGestureEngine::launch_dock_app_(PageId::CALENDAR, rs, rt, now_ms);
      save_origin();
      if (route_id == RouteId::CALENDAR_DAY) rs.cal.subpage = CalendarSubpage::DAY;
      else if (route_id == RouteId::CALENDAR_WEEK) rs.cal.subpage = CalendarSubpage::WEEK;
      else if (route_id == RouteId::CALENDAR_MONTH) rs.cal.subpage = CalendarSubpage::MONTH;
      return;

    case RouteId::APP_CLOCK:
      if (is_camera_view_active(rs)) leave_home_hub_cameras_if_active(rs, rt, now_ms);
      rs.nav.shortcut_origin = ShortcutOrigin::NONE;
      rs.nav.gui_page = PageId::CLOCK;
      rs.nav.home_subpage = 0;
      mark_dirty_full();
      rt.schedule_disp();
      return;

    case RouteId::HOME_HUB_ALARM:
    case RouteId::HOME_HUB_INTERCOM:
    case RouteId::HOME_HUB_THERMOSTAT:
    case RouteId::HOME_HUB_ENTRY:
    case RouteId::HOME_HUB_VACUUM: {
      int tab = HomeHubTab::ALARM;
      if (route_id == RouteId::HOME_HUB_INTERCOM) tab = HomeHubTab::INTERCOM;
      else if (route_id == RouteId::HOME_HUB_THERMOSTAT) tab = HomeHubTab::THERMOSTAT;
      else if (route_id == RouteId::HOME_HUB_ENTRY) tab = HomeHubTab::ENTRY;
      else if (route_id == RouteId::HOME_HUB_VACUUM) tab = HomeHubTab::VACUUM;
      rs.nav.shortcut_origin = ShortcutOrigin::NONE;
      TouchGestureEngine::enter_home_hub_tab_(tab, false, rs, rt, now_ms);
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::AREA_PICKER:
      if (is_camera_view_active(rs)) leave_home_hub_cameras_if_active(rs, rt, now_ms);
      save_origin();
      rs.nav.gui_page = PageId::AREA;
      rs.nav.home_subpage = 0;
      rs.nav.area_subpage = AreaSubpage::PICKER;
      rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST) = 1;
      TouchGestureEngine::do_fetch_area_list_browse_();
      mark_dirty_full();
      rt.schedule_disp();
      return;

    case RouteId::AREA_SCENE: case RouteId::AREA_LIGHT: case RouteId::AREA_COVER:
    case RouteId::AREA_FAN: case RouteId::AREA_SWITCH: case RouteId::AREA_CLIMATE:
    case RouteId::AREA_MEDIA: case RouteId::AREA_ALL: {
      if (is_camera_view_active(rs)) leave_home_hub_cameras_if_active(rs, rt, now_ms);
      rs.nav.shortcut_origin = ShortcutOrigin::NONE;
      rs.nav.gui_page = PageId::AREA;
      rs.nav.home_subpage = 0;
      const char *dom = "all";
      if (route_id == RouteId::AREA_SCENE) dom = "scene";
      else if (route_id == RouteId::AREA_LIGHT) dom = "light";
      else if (route_id == RouteId::AREA_COVER) dom = "cover";
      else if (route_id == RouteId::AREA_FAN) dom = "fan";
      else if (route_id == RouteId::AREA_SWITCH) dom = "switch";
      else if (route_id == RouteId::AREA_CLIMATE) dom = "climate";
      else if (route_id == RouteId::AREA_MEDIA) dom = "media";
      std::string cats[8];
      const int cnt = collect_area_categories(cats);
      int act_idx = -1;
      for (int i = 0; i < cnt; ++i) {
        if (cats[i] == dom) { act_idx = i; break; }
      }
      rs.area.domain_filter = (act_idx >= 0) ? cats[act_idx] : std::string(dom);
      sync_mode_bar_window(area_cat_dock_page(), std::max(0, act_idx), cnt);
      if (rs.area.active_area_id.empty()) {
        rs.nav.area_subpage = AreaSubpage::PICKER;
        rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST) = 1;
        TouchGestureEngine::do_fetch_area_list_browse_();
      } else {
        rs.nav.area_subpage = AreaSubpage::OVERVIEW;
        rs.area.browse_mode = "entities";
        rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
        TouchGestureEngine::do_fetch_area_browse_();
      }
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::MUSIC_SPEAKERS: case RouteId::MUSIC_LIBRARY:
    case RouteId::MUSIC_FAVORITES: case RouteId::MUSIC_PLAYLISTS:
    case RouteId::MUSIC_QUEUE: case RouteId::MUSIC_ALBUM_TRACKS:
    case RouteId::MUSIC_LYRICS: case RouteId::MUSIC_INFO: {
      TouchGestureEngine::launch_dock_app_(PageId::MUSIC, rs, rt, now_ms);
      save_origin();
      rs.music.direct_nav = true;
      rs.slot_page_for(latter_rpc::SlotChannel::MUSIC) = 1;
      rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
      if (route_id == RouteId::MUSIC_FAVORITES || route_id == RouteId::MUSIC_PLAYLISTS) {
        rs.music.apply_route(route_id);
        mode_bar_anim().snap(rs.music.mode_bar_win_start, static_cast<int>(rs.music.view));
        reset_music_loading_slots(rs, "Loading...");
        TouchGestureEngine::fetch_music_browse_for_current_view_(rs, now_ms);
      } else {
        MusicView mv = MusicView::LIBRARY;
        if (route_id == RouteId::MUSIC_SPEAKERS) mv = MusicView::SPEAKERS;
        else if (route_id == RouteId::MUSIC_QUEUE) mv = MusicView::QUEUE;
        else if (route_id == RouteId::MUSIC_ALBUM_TRACKS) mv = MusicView::ALBUM_TRACKS;
        else if (route_id == RouteId::MUSIC_LYRICS) mv = MusicView::LYRICS;
        else if (route_id == RouteId::MUSIC_INFO) mv = MusicView::INFO;
        activate_music_peer_tab(mv, now_ms);
        mode_bar_anim().snap(rs.music.mode_bar_win_start, static_cast<int>(rs.music.view));
      }
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::REMOTE_DEVICES:
      if (has_any_configured_remote()) {
        TouchGestureEngine::launch_dock_app_(PageId::REMOTE, rs, rt, now_ms);
        save_origin();
        rs.nav.remote_subpage = 1;
        remote_control_page() = 0;
        mark_dirty_full();
        rt.schedule_disp();
      }
      return;

    case RouteId::TOOLS_STOPWATCH: case RouteId::TOOLS_LEVEL:
    case RouteId::TOOLS_FLASHLIGHT: case RouteId::TOOLS_CALCULATOR:
    case RouteId::TOOLS_DICE: case RouteId::TOOLS_COIN_FLIP: {
      TouchGestureEngine::launch_dock_app_(PageId::TOOLS, rs, rt, now_ms);
      save_origin();
      int sub = ToolsSubpage::LAUNCHER;
      if (route_id == RouteId::TOOLS_STOPWATCH) sub = ToolsSubpage::STOPWATCH;
      else if (route_id == RouteId::TOOLS_LEVEL) sub = ToolsSubpage::LEVEL;
      else if (route_id == RouteId::TOOLS_FLASHLIGHT) sub = ToolsSubpage::FLASHLIGHT;
      else if (route_id == RouteId::TOOLS_CALCULATOR) sub = ToolsSubpage::CALCULATOR;
      else if (route_id == RouteId::TOOLS_DICE) sub = ToolsSubpage::DICE;
      else if (route_id == RouteId::TOOLS_COIN_FLIP) sub = ToolsSubpage::COIN_FLIP;
      rs.nav.tools_subpage = sub;
      if (sub == ToolsSubpage::FLASHLIGHT) rt.cmd(UiHostCmd::FLASHLIGHT_FULL_BRIGHTNESS);
      else if (sub == ToolsSubpage::CALCULATOR) reset_calculator_state();
      rt.apply_power_policy();
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::GAMES_MARBLES: case RouteId::GAMES_ARCHERY:
    case RouteId::GAMES_BRICKS: case RouteId::GAMES_CHOMP:
    case RouteId::GAMES_BLOCKS: case RouteId::GAMES_SNAKE:
    case RouteId::GAMES_DROP_FOUR: {
      int g_id = 1;
      switch (route_id) {
        case RouteId::GAMES_MARBLES:   g_id = 1; break;
        case RouteId::GAMES_ARCHERY:   g_id = 2; break;
        case RouteId::GAMES_BRICKS:    g_id = 3; break;
        case RouteId::GAMES_CHOMP:     g_id = 4; break;
        case RouteId::GAMES_BLOCKS:    g_id = 5; break;
        case RouteId::GAMES_SNAKE:     g_id = 6; break;
        case RouteId::GAMES_DROP_FOUR: g_id = 7; break;
        default: break;
      }
      if (!is_route_supported_on_board(route_id) ||
          !game_supported_for_imu(g_id, LATTER_HAS_IMU != 0)) {
        return;
      }
      TouchGestureEngine::launch_dock_app_(PageId::GAMES, rs, rt, now_ms);
      save_origin();
      int enabled_ids[kMaxGamesLauncherItems]{};
      const int game_cnt = collect_enabled_games(enabled_ids, device_prefs());
      int matched_slot = 0;
      for (int i = 0; i < game_cnt; ++i) {
        if (enabled_ids[i] == g_id) { matched_slot = i; break; }
      }
      rs.nav.game_carousel_idx = matched_slot;
      rs.nav.active_game = g_id;
      game_input().clear();
      audio_arbitrator().cancel_media_idle_debounce();
      if (rt.local_mp_playing) {
        rt.cmd(UiHostCmd::PAUSE_LOCAL_MEDIA_PLAYER);
        audio_arbitrator().cancel_media_idle_debounce();
      }
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::SETTINGS_HOME:
      if (is_camera_view_active(rs)) leave_home_hub_cameras_if_active(rs, rt, now_ms);
      save_origin();
      rs.nav.gui_page = PageId::HOME;
      rs.nav.home_subpage = 1;
      rs.nav.home_settings_page = 1;
      mark_dirty_full();
      rt.schedule_disp();
      return;

    case RouteId::SETTINGS_DISPLAY: case RouteId::SETTINGS_SLEEP:
    case RouteId::SETTINGS_SOUND: case RouteId::SETTINGS_VOICE:
    case RouteId::SETTINGS_MUSIC: case RouteId::SETTINGS_ACTIONS:
    case RouteId::SETTINGS_UPDATE: case RouteId::SETTINGS_INFO:
      TouchGestureEngine::launch_dock_app_(PageId::SYSTEM, rs, rt, now_ms);
      save_origin();
      if (route_id == RouteId::SETTINGS_DISPLAY) { rs.nav.system_menu_mode = 2; rs.nav.display_menu_page = 1; }
      else if (route_id == RouteId::SETTINGS_SLEEP) { rs.nav.system_menu_mode = 1; rs.nav.sleep_menu_page = 1; }
      else if (route_id == RouteId::SETTINGS_SOUND) { rs.nav.system_menu_mode = 7; }
      else if (route_id == RouteId::SETTINGS_VOICE) { rs.nav.system_menu_mode = 11; }
      else if (route_id == RouteId::SETTINGS_MUSIC) { rs.nav.system_menu_mode = 9; rs.music.settings_page = 1; rs.nav.audio_menu_parent_page = PageId::SYSTEM; }
      else if (route_id == RouteId::SETTINGS_ACTIONS) { rs.nav.system_menu_mode = 3; }
      else if (route_id == RouteId::SETTINGS_UPDATE) { rs.nav.system_menu_mode = 5; rs.nav.update_check_state = 0; }
      else if (route_id == RouteId::SETTINGS_INFO) { rs.nav.system_menu_mode = 8; }
      mark_dirty_full();
      rt.schedule_disp();
      return;

    case RouteId::NONE:
    default:
      return;
  }
}

void TouchGestureEngine::navigate_to_start_screen(uint32_t now_ms) {
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const int anchor = resolve_anchor_dock_app(prefs);
  if (rs.nav.active_game > 0) rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
  if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
    intercom_session().stop_call_with_hw(now_ms);
  }
  const bool was_cam_page = is_camera_view_active(rs);
  stop_calendar_dictation_if_active(rs, rt_, now_ms);
  stop_broadcast_dictation_if_active(rs, rt_, now_ms);
  TouchGestureEngine::scrub_pin_and_pending_(*this, rs, false);
  reset_common_nav_fields(rs, now_ms);
  rs.nav.time_subpage = TimeSubpage::CLOCK;
  rs.nav.world_clock_page = 0;
  const bool start_cam = is_cameras_start_screen(prefs);
  const bool keep_cam = start_cam || (anchor == PageId::CAMERAS);
  if (was_cam_page && !keep_cam) {
    rt_.cmd(UiHostCmd::STOP_CAMERA_STREAM);
    rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
    rs.alert.loaded_camera_url.clear();
    rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
  }
  if (anchor == PageId::HOME || anchor == PageId::CLOCK) {
    rs.nav.gui_page = anchor;
    rs.nav.home_subpage = 0;
    mark_dirty_full();
    rt_.schedule_disp();
    return;
  }
  if (anchor == PageId::REMOTE && !has_any_configured_remote()) {
    rs.nav.gui_page = prefs.home_screen_enabled ? PageId::HOME : PageId::CLOCK;
    rs.nav.home_subpage = 0;
    mark_dirty_full();
    rt_.schedule_disp();
    return;
  }
  if (start_cam || anchor == PageId::CAMERAS) {
    enter_cameras_app(-1, "", false, rs, rt_, now_ms);
    mark_dirty_full();
    rt_.schedule_disp();
    return;
  }
  TouchGestureEngine::launch_dock_app_(anchor, rs, rt_, now_ms);
}

void TouchGestureEngine::teardown_before_remote_nav_(DeviceRuntimeState &rs,
                                                     uint32_t now_ms,
                                                     bool target_is_camera) {
  if (rs.nav.active_game > 0) rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
  if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
    intercom_session().stop_call_with_hw(now_ms);
  }
  const bool was_cam_page = is_camera_view_active(rs);
  stop_calendar_dictation_if_active(rs, rt_, now_ms);
  stop_broadcast_dictation_if_active(rs, rt_, now_ms);
  TouchGestureEngine::scrub_pin_and_pending_(*this, rs, false);
  rs.showcase.va_stage = 0U;
  rs.showcase.silent_find_my = false;
  rs.showcase.silent_alarm = false;
  rs.showcase.music_hud_lock = false;
  if (rs.alert.active) {
    rs.alert.active = false;
    rt_.cmd(UiHostCmd::DISMISS_NOTIFICATION);
  }
  reset_common_nav_fields(rs, now_ms);
  if (was_cam_page && !target_is_camera) {
    rs.cameras.doorbell_live_popup_active = false;
    rt_.cmd(UiHostCmd::STOP_CAMERA_STREAM);
    rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
    rs.alert.loaded_camera_url.clear();
    rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
  }
}

void TouchGestureEngine::navigate_home(uint32_t now_ms) {
  if (!persisted_prefs().home_screen_enabled) {
    navigate_to_start_screen(now_ms);
    return;
  }
  auto &rs = runtime_state();
  if (rs.nav.active_game > 0) rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
  if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
    intercom_session().stop_call_with_hw(now_ms);
  }
  const bool was_cam_page = is_camera_view_active(rs);
  stop_calendar_dictation_if_active(rs, rt_, now_ms);
  stop_broadcast_dictation_if_active(rs, rt_, now_ms);
  TouchGestureEngine::scrub_pin_and_pending_(*this, rs, false);
  rs.nav.gui_page = PageId::HOME;
  rs.nav.home_subpage = 0;
  reset_common_nav_fields(rs, now_ms);
  if (was_cam_page) {
    rs.cameras.doorbell_live_popup_active = false;
    rt_.cmd(UiHostCmd::STOP_CAMERA_STREAM);
    rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
    rs.alert.loaded_camera_url.clear();
    rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
  }
  mark_dirty_full();
  rt_.schedule_disp();
}

bool TouchGestureEngine::handle_down_takeovers_(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms) {
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  auto &rt = eng.rt_;
  auto &gest = touch_gesture();

  // Priority 1: Alarm / Timer / Find-My Ringing Takeover (above Voice Assistant!)
  if (rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active) {
    gest.active = false;
    const HitRegion *t_hit = frame_hits().hit_test(tx, ty, kHitTap);
    if (rs.alarms.find_my_active) {
      rs.alarms.find_my_active = false;
      last_screen_wake_ms() = now_ms;
      rt.cmd(UiHostCmd::STOP_ALARM_SOUND);
      rt.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
      mark_dirty_full();
      rt.schedule_disp();
      return true;
    }
    if (rs.alarms.ringing) {
      if (t_hit != nullptr && t_hit->widget_id == WidgetId::ALERT_SNOOZE_BTN) {
        snoozed_alarm_slot() =
            (rs.alarms.ringing_idx >= 1 && rs.alarms.ringing_idx <= 3) ? rs.alarms.ringing_idx : 1;
        rs.alarms.snooze_until = (rt.time_epoch > 0) ? (rt.time_epoch + 300U) : ((now_ms / 1000U) + 300U);
        rs.alarms.ringing = false;
        latter_actions::check_and_emit_timer_alarm_state(now_ms);
        last_screen_wake_ms() = now_ms;
        rt.cmd(UiHostCmd::STOP_ALARM_SOUND);
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      if (t_hit != nullptr && t_hit->widget_id == WidgetId::ALERT_DISMISS_BTN) {
        if (rs.alarms.ringing_idx >= 1 && rs.alarms.ringing_idx <= 3) {
          auto &ap = prefs.alarms[rs.alarms.ringing_idx - 1];
          if (ap.recurrence == 3) {
            ap.enabled = false;
            latter_actions::sync_local_alarms_edit(now_ms);
          }
        }
        rs.alarms.ringing = false;
        rs.alarms.snooze_until = 0;
        snoozed_alarm_slot() = 0;
        latter_actions::check_and_emit_timer_alarm_state(now_ms);
        last_screen_wake_ms() = now_ms;
        rt.cmd(UiHostCmd::STOP_ALARM_SOUND);
        rt.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
        mark_dirty_full();
        rt.schedule_disp();
      }
      return true;
    }
    if (rs.timers.ringing && t_hit != nullptr && t_hit->widget_id == WidgetId::ALERT_DISMISS_BTN) {
      rs.timers.ringing = false;
      rs.timers.va_timer_name.clear();
      sync_multi_timer_state();
      latter_actions::check_and_emit_timer_alarm_state(now_ms);
      last_screen_wake_ms() = now_ms;
      rt.cmd(UiHostCmd::STOP_ALARM_SOUND);
      rt.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
      rt.apply_power_policy();
      mark_dirty_full();
      rt.schedule_disp();
    }
    return true;
  }

  // Priority 1b: Showcase Silent Find My / Alarm Previews (zero audio; keep-awake stays armed)
  const bool sc_on = rs.showcase.is_active(now_ms);
  if (sc_on && (rs.showcase.silent_find_my || rs.showcase.silent_alarm)) {
    gest.active = false;
    const HitRegion *t_hit = frame_hits().hit_test(tx, ty, kHitTap);
    if (rs.showcase.silent_find_my) {
      if (t_hit != nullptr && t_hit->widget_id == WidgetId::ALERT_DISMISS_BTN) {
        rs.showcase.silent_find_my = false;
        last_screen_wake_ms() = now_ms;
        rt.apply_power_policy();
        mark_dirty_full();
        rt.schedule_disp();
      }
      return true;
    }
    if (rs.showcase.silent_alarm) {
      if (t_hit != nullptr &&
          (t_hit->widget_id == WidgetId::ALERT_DISMISS_BTN ||
           t_hit->widget_id == WidgetId::ALERT_SNOOZE_BTN)) {
        rs.showcase.silent_alarm = false;
        last_screen_wake_ms() = now_ms;
        rt.apply_power_policy();
        mark_dirty_full();
        rt.schedule_disp();
      }
      return true;
    }
  }

  // Priority 2: Voice Assistant Takeover (exempt during live doorbell PTT reply — D-066!)
  if (should_va_takeover_preempt(rs.voice.active, !rs.voice.status_text.empty(),
                                 rs.voice.intercom_turn, rs.nav.gui_page,
                                 rs.cameras.doorbell_live_popup_active)) {
    gest.active = false;
    last_screen_wake_ms() = now_ms;
    rt.cmd(UiHostCmd::CANCEL_VOICE_ASSISTANT);
    return true;
  }

  // Priority 2b: Showcase Voice Assistant Visual Preview (clears ONLY va_stage; never invokes real VA cancel)
  if (sc_on && rs.showcase.has_va_preview(now_ms)) {
    gest.active = false;
    const HitRegion *v_hit = frame_hits().hit_test(tx, ty, kHitTap);
    if (v_hit == nullptr || v_hit->widget_id == WidgetId::VA_CANCEL_BTN) {
      rs.showcase.va_stage = 0U;
      last_screen_wake_ms() = now_ms;
      rt.apply_power_policy();
      mark_dirty_full();
      rt.schedule_disp();
    }
    return true;
  }

  // Priority 3: Security PIN Pad Modal
  if (ui::pin_pad_state().open) {
    gest.active = false;
    eng.handle_pin_pad_tap(tx, ty, now_ms);
    return true;
  }

  // Priority 4: Push Notification Banner / Doorbell Camera Popup
  if (rs.alert.active) {
    TouchGestureEngine::handle_down_alert_overlay_(eng, tx, ty, now_ms);
    return true;
  }

  // Priority 5: Shortcut Entity Confirmation Modal (resolves on finger lift in on_touch_up)
  if (shortcut_confirm_state().open) {
    gest.begin(tx, ty, now_ms, rs.nav.gui_page, compute_touch_context(rs));
    return true;
  }

  // Priority 6 (Compositor Priority 9): Quick Actions Overlay (D-074)
  if (rs.nav.quick_actions_open) {
    gest.begin(tx, ty, now_ms, rs.nav.gui_page, compute_touch_context(rs));
    const HitRegion *qa_hit = frame_hits().hit_test(tx, ty, kHitTap);
    const uint16_t qwid = (qa_hit != nullptr) ? qa_hit->widget_id : WidgetId::NONE;
    if (qwid == WidgetId::BACK_BUTTON) {
      gest.active = false;
      const uint32_t qtkey = make_tap_target_key(rs.nav.gui_page, qwid, 0);
      if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, qtkey)) {
        rs.nav.quick_actions_open = false;
        mark_dirty_full();
        rt.schedule_disp();
      }
    }
    return true;
  }

  return false;
}

void TouchGestureEngine::handle_quick_actions_release_(TouchGestureEngine &eng, uint32_t now_rel,
                                                     const HitRegion *start_slider) {
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  auto &rt = eng.rt_;
  auto &gest = touch_gesture();
  const auto &ly = layout();
  const TouchPt tp{gest.last_x, gest.last_y};

  if (!gest.swipe_committed && start_slider != nullptr &&
      gest.axis_lock == GestureAxisLock::Slider) {
    if (start_slider->widget_id == WidgetId::QUICK_VOL_SLIDER) {
      const int next_pct = std::clamp(
          10 + static_cast<int>(std::lround(start_slider->norm_x(tp.x) * 90.0f)), 10, 100);
      rt.set_num_val(UiNumberId::SPEAKER_VOLUME, static_cast<float>(next_pct));
      rt.schedule_disp();
      gest.tap_cancelled = true;
    } else if (start_slider->widget_id == WidgetId::QUICK_BRI_SLIDER) {
      const auto bri = resolve_active_brightness_target(
          prefs, weather_state().sun_state, rt.time_valid,
          rt.time_valid ? rt.time_hour : 12,
          area_ctx_state().area_ambient_pct, rs.nav.ambient_bri_override_pct);
      const int next_pct = std::clamp(
          1 + static_cast<int>(std::lround(start_slider->norm_x(tp.x) * 99.0f)), 1, 100);
      if (bri.kind == ActiveBrightnessTargetKind::AMBIENT ||
          bri.kind == ActiveBrightnessTargetKind::AMBIENT_OVERRIDE) {
        rs.nav.ambient_bri_override_pct = static_cast<int8_t>(next_pct);
      } else {
        const UiNumberId nid = TouchGestureEngine::brightness_number_id_for_kind_(bri.kind);
        rt.set_num_val(nid, static_cast<float>(next_pct));
      }
      rt.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
      rt.schedule_disp();
      gest.tap_cancelled = true;
    }
  } else {
    (void)eng.try_commit_kinematic_swipe_(true, now_rel);
  }

  const bool tap_valid =
      rs.nav.quick_actions_open && !gest.swipe_committed && !gest.tap_cancelled &&
      (gest.max_travel <= ly.tap_max());
  if (!tap_valid) return;

  const HitRegion *hit = frame_hits().hit_test(tp.x, tp.y, kHitTap);
  if (hit == nullptr) {
    hit = frame_hits().hit_test(gest.start_x, gest.start_y, kHitTap);
  }
  const uint16_t wid = (hit != nullptr) ? hit->widget_id : WidgetId::NONE;
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  if (wid == WidgetId::QUICK_VOL_SLIDER &&
      touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
    const float new_vol = step_wrap_range(rt.local_mp_volume, 0.10f, 0.10f, 1.00f);
    rt.set_num_val(UiNumberId::SPEAKER_VOLUME, std::round(new_vol * 100.0f));
    rt.schedule_disp();
  } else if (wid == WidgetId::QUICK_BRI_SLIDER &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
    const auto bri = resolve_active_brightness_target(
        prefs, weather_state().sun_state, rt.time_valid,
        rt.time_valid ? rt.time_hour : 12,
        area_ctx_state().area_ambient_pct, rs.nav.ambient_bri_override_pct);
    if (bri.kind == ActiveBrightnessTargetKind::AMBIENT ||
        bri.kind == ActiveBrightnessTargetKind::AMBIENT_OVERRIDE) {
      rs.nav.ambient_bri_override_pct = static_cast<int8_t>(
          step_brightness_tap_10pct(static_cast<int>(bri.pct)));
    } else {
      const UiNumberId nid = TouchGestureEngine::brightness_number_id_for_kind_(bri.kind);
      const int cur_pct = static_cast<int>(std::lround(rt.num_val(nid)));
      rt.set_num_val(nid, static_cast<float>(step_brightness_tap_10pct(cur_pct)));
    }
    rt.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
    rt.schedule_disp();
  } else if (wid == WidgetId::QUICK_BEDTIME_TOGGLE &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    rs.nav.ambient_bri_override_pct = -1;
    rt.sw_toggle(UiSwitchId::BEDTIME_MODE);
    rt.schedule_disp();
  } else if (wid == WidgetId::QUICK_DND_TOGGLE &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    rt.sw_toggle(UiSwitchId::DO_NOT_DISTURB);
    rt.schedule_disp();
  } else if (wid == WidgetId::QUICK_HIBERNATE_BTN &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
#if LATTER_HAS_BATTERY
    rs.nav.quick_actions_open = false;
    rt.cmd(UiHostCmd::START_INSTANT_HIBERNATION);
#endif
  } else if (wid == WidgetId::QUICK_SCREEN_OFF_BTN &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    rs.nav.quick_actions_open = false;
    rt.cmd(UiHostCmd::TURN_SCREEN_OFF);
  } else if (wid == WidgetId::QUICK_SCREENSAVER_BTN &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    rs.nav.quick_actions_open = false;
    rt.cmd(UiHostCmd::TRIGGER_SCREENSAVER_NOW);
  } else if (wid == WidgetId::BACK_BUTTON &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    rs.nav.quick_actions_open = false;
    mark_dirty_full();
    rt.schedule_disp();
  }
}

bool TouchGestureEngine::handle_tap_nav_and_takeovers_(
    int /*tx*/, int /*ty*/, uint32_t now_rel, const HitRegion *hit, uint16_t wid) {
  auto &rs = runtime_state();
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  if (shortcut_confirm_state().open) {
    auto &sc = shortcut_confirm_state();
    if ((wid == WidgetId::CONFIRM_CANCEL || wid == WidgetId::BACK_BUTTON || wid == WidgetId::NONE) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      sc.clear();
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CONFIRM_ACCEPT &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const std::string ent_id = sc.entity_id;
      const std::string act = sc.action;
      const std::string lbl = sc.line1;
      sc.clear();
      if (!ent_id.empty() && !act.empty()) {
        latter_actions::emit_shortcut_action(ent_id, act);
        TouchGestureEngine::apply_optimistic_shortcut_feedback_(ent_id, act, lbl, rs, now_rel);
      }
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return true;
  }

  if (rs.nav.gui_page == PageId::HOME && rs.nav.home_subpage == 0 &&
      wid >= WidgetId::DOCK_SLOT_BASE && wid < WidgetId::DOCK_SLOT_BASE + 4) {
    if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      ShortcutItem *dock_items = dock_items_scratch();
      const int dock_cnt = collect_effective_dock_items(dock_items, kMaxEffectiveDockItems);
      if (hit != nullptr && hit->param >= 0 && hit->param < dock_cnt) {
        const ShortcutItem chosen = dock_items[hit->param];
        TouchGestureEngine::activate_shortcut_item_(chosen, ShortcutOrigin::HOME_DOCK, rs, rt_, now_rel);
      }
    }
    return true;
  }

  if (rs.nav.gui_page == PageId::WEATHER) {
    if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4 &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int target_sub = std::clamp(static_cast<int>(hit->param), 0, 3);
      if (target_sub != rs.nav.sky_weather_subpage) {
        rs.nav.sky_weather_subpage = target_sub;
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
    return true;
  }

  return false;
}


void TouchGestureEngine::handle_home_activity_action_(const HomeActivityItem *items, int count, int item_idx,
                                        bool quick_btn_tap, uint32_t now_ms,
                                        DeviceRuntimeState &rs, PersistedDevicePrefs &prefs,
                                        const UiTouchRuntime &rt) {
  if (item_idx < 0 || item_idx >= count) return;
  const HomeActivityItem &act = items[item_idx];
  const int kind = act.kind;
  const int s_idx = act.slot_idx;

  if (quick_btn_tap) {
    if (kind == 1 && s_idx >= 0 && s_idx < 3) {
      auto &s = timer_slots()[s_idx];
      if (s.running) {
        s.running = false;
        s.paused = true;
        s.remaining_sec = (s.target_ms > now_ms)
                              ? static_cast<int>((s.target_ms - now_ms + 999U) / 1000U)
                              : 0;
      } else if (s.paused) {
        s.running = true;
        s.paused = false;
        s.target_ms = now_ms + static_cast<uint32_t>(s.remaining_sec) * 1000U;
      }
      sync_multi_timer_state();
      latter_actions::check_and_emit_timer_alarm_state(now_ms);
      rt.apply_power_policy();
    } else if (kind == 2) {
      if (rs.stopwatch.running) {
        rs.stopwatch.running = false;
        rs.stopwatch.elapsed_ms = now_ms - rs.stopwatch.start_time_ms;
      } else {
        rs.stopwatch.running = true;
        rs.stopwatch.start_time_ms = now_ms - rs.stopwatch.elapsed_ms;
      }
      latter_actions::check_and_emit_timer_alarm_state(now_ms);
    } else if (kind == 3) {
      const uint32_t intent_ts = TouchGestureEngine::now_or_hal_ms_(now_ms);
      audio_session().set_play_intent(MusicPlayIntent::HomeQuickToggle, intent_ts);
      rt.cmd(UiHostCmd::TOGGLE_HA_MUSIC);
    } else if (kind == 4) {
      if (s_idx == 0) {
        rs.alarms.snooze_until = 0;
        snoozed_alarm_slot() = 0;
      } else if (s_idx >= 1 && s_idx <= 3) {
        prefs.alarms[s_idx - 1].enabled = false;
        if (rs.alarms.ringing_idx == s_idx || snoozed_alarm_slot() == s_idx) {
          rs.alarms.snooze_until = 0;
          snoozed_alarm_slot() = 0;
        }
        latter_actions::sync_local_alarms_edit(now_ms);
      }
    } else if (kind == 6) {
      auto &ac = area_ctx_state();
      if (cam_call_active() || cam_intercom_ptt_active()) {
        intercom_session().stop_call_with_hw(now_ms);
      }
      if (cam_listen_active()) {
        cam_listen_active() = false;
        rs.intercom.cam_action = "listen_stop";
        intercom_session().emit_current_cmd();
        rt.cmd(UiHostCmd::STOP_DOORBELL_AUDIO);
      }
      if (!ac.doorbell_talk_state.empty() && ac.doorbell_talk_state != "idle" &&
          ac.doorbell_talk_state != "off") {
        rs.intercom.cam_action = "hangup";
        intercom_session().emit_current_cmd();
        intercom_session().stop_call_with_hw(now_ms);
        ac.doorbell_talk_state = "idle";
      }
      if (rs.voice.intercom_turn) {
        rs.voice.intercom_turn = false;
        rt.cmd(UiHostCmd::CANCEL_VOICE_ASSISTANT);
      }
    }
    rt.schedule_disp();
    return;
  }

  rs.nav.home_subpage = 0;
  home_dock_page() = 0;
  if (kind == 1) {
    rs.nav.shortcut_origin = ShortcutOrigin::NONE;
    rs.nav.gui_page = PageId::TIME;
    rs.nav.time_subpage = TimeSubpage::TIMERS;
    rs.alarms.edit_idx = 0;
    timer_view_slot() = (s_idx >= 0 && s_idx < 3) ? (s_idx + 1) : 0;
  } else if (kind == 2) {
#if LATTER_ROUND_DISPLAY
    if (rs.stopwatch.started_in_analog) {
      rs.nav.shortcut_origin = ShortcutOrigin::HOME_DOCK;
      rs.nav.gui_page = PageId::TOOLS;
      rs.nav.tools_subpage = ToolsSubpage::STOPWATCH;
    } else
#endif
    {
      rs.nav.shortcut_origin = ShortcutOrigin::NONE;
      rs.nav.gui_page = PageId::TIME;
      rs.nav.time_subpage = TimeSubpage::STOPWATCH;
      timer_view_slot() = 0;
      rs.alarms.edit_idx = 0;
    }
  } else if (kind == 3) {
    rs.nav.shortcut_origin = ShortcutOrigin::NONE;
    rs.nav.gui_page = PageId::MUSIC;
    reset_music_to_now_playing(rs, now_ms, true);
    const auto &ms = media_state();
    if (rt.select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE) != "None" &&
        (!ms.is_local || rt.album_art_w == 0) && ms.art_url != rs.music.loaded_art_url) {
      rt.cmd(UiHostCmd::FETCH_REMOTE_ART_DEBOUNCED);
    }
  } else if (kind == 4) {
    rs.nav.shortcut_origin = ShortcutOrigin::NONE;
    rs.nav.gui_page = PageId::TIME;
    rs.nav.time_subpage = TimeSubpage::ALARMS;
    timer_view_slot() = 0;
    rs.alarms.edit_idx = (s_idx >= 1 && s_idx <= 3) ? s_idx : 0;
    rs.nav.alarm_edit_field = 1;
  } else if (kind == 5) {
    TouchGestureEngine::launch_dock_app_(PageId::CALENDAR, rs, rt, now_ms);
    rs.cal.subpage = CalendarSubpage::DAY;
    return;
  } else if (kind == 6) {
    const auto &ac = area_ctx_state();
    const bool db_live = !ac.doorbell_talk_state.empty() &&
                         ac.doorbell_talk_state != "idle" &&
                         ac.doorbell_talk_state != "off" &&
                         ac.doorbell_talk_state != "unavailable" &&
                         ac.doorbell_talk_state != "unknown";
    rs.nav.shortcut_origin = ShortcutOrigin::NONE;
    if (db_live || cam_call_active() || cam_listen_active()) {
      rs.nav.gui_page = PageId::CAMERAS;
      rs.cameras.tab = CamerasTab::SINGLE;
      rs.cameras.quick_reply_open = false;
      rs.cameras.hud_active = true;
      rs.cameras.hud_timer = now_ms;
      rs.cameras.doorbell_live_popup_active = false;
      mode_bar_anim().snap(CamerasTab::SINGLE);
      rt.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
    } else {
      TouchGestureEngine::enter_home_hub_tab_(HomeHubTab::INTERCOM, false, rs, rt, now_ms);
    }
  }
  mark_dirty_full();
  rt.schedule_disp();
}

void TouchGestureEngine::do_fetch_area_browse_() {
  const auto &rs = runtime_state();
  const int page = rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES);
  latter_actions::emit_area_browse_entities(
      rs.area.active_area_id, rs.area.domain_filter, page, LATTER_CARD_SLOT_COUNT);
}

void TouchGestureEngine::do_fetch_area_list_browse_() {
  const auto &rs = runtime_state();
  const int page = rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST);
  latter_actions::emit_area_browse_list(rs.area.active_area_id, page, LATTER_SLOT_COUNT);
}
void TouchGestureEngine::enter_home_hub_tab_(int target_tab, bool /*expand_cam*/, DeviceRuntimeState &rs,
                        const UiTouchRuntime &rt, uint32_t now_ms) {
  const bool was_hub = (rs.nav.gui_page == PageId::HOME_HUB);
  stop_broadcast_dictation_if_active(rs, rt, now_ms);
  if (is_camera_view_active(rs)) {
    leave_home_hub_cameras_if_active(rs, rt, now_ms);
  }
  rs.nav.gui_page = PageId::HOME_HUB;
  rs.nav.home_subpage = 0;
  rs.nav.tools_subpage = 0;
  if (!was_hub || target_tab == HomeHubTab::INTERCOM) {
    home_hub_broadcast_page() = 0;
  }
  if (!was_hub) {
    sync_home_hub_for_active_area(area_ctx_state().area_id, home_hub_state());
  }

  int hh_tabs[kMaxHomeHubTabs]{};
  const int hh_cnt = collect_home_hub_tabs(hh_tabs);
  int slot = -1;
  if (target_tab >= 0) {
    for (int i = 0; i < hh_cnt; ++i) {
      if (hh_tabs[i] == target_tab) { slot = i; break; }
    }
  }
  if (slot >= 0) {
    home_hub_tab() = target_tab;
    sync_mode_bar_window(home_hub_mode_bar_page(), slot, hh_cnt);
  } else if (hh_cnt > 0) {
    slot = 0;
    for (int i = 0; i < hh_cnt; ++i) {
      if (hh_tabs[i] == home_hub_tab()) { slot = i; break; }
    }
    home_hub_tab() = hh_tabs[slot];
    sync_mode_bar_window(home_hub_mode_bar_page(), slot, hh_cnt);
  } else {
    home_hub_mode_bar_page() = 0;
    home_hub_tab() = (target_tab >= 0) ? target_tab : HomeHubTab::ALARM;
  }
}

const HomeActivityItem *TouchGestureEngine::resolve_home_activities_(uint32_t now_ms, const DeviceRuntimeState &rs,
                                                const PersistedDevicePrefs &prefs,
                                                const UiTouchRuntime &rt, int &out_count) {
  out_count = 0;
  const HomeActivityItem *act_items = home_activity_rendered_items(out_count);
  if (out_count > 0) return act_items;
  HomeActivityItem *fallback_items = touch_activity_fallback_scratch();
  const auto &ms_hm = media_state();
  const bool m_play = is_music_playing_effective(
      !ms_hm.is_local, rt.local_mp_playing, ms_hm.state, now_ms);
  const std::string &m_t = (!ms_hm.is_local || rt.track_title.empty()) ? ms_hm.title : rt.track_title;
  const std::string &m_a = (!ms_hm.is_local || rt.track_artist.empty()) ? ms_hm.artist : rt.track_artist;
  out_count = collect_home_activities(
      now_ms, prefs.is_24h_format,
      rs.stopwatch.running, rs.stopwatch.start_time_ms, rs.stopwatch.elapsed_ms,
      m_play, m_t, m_a, rs.alarms.snooze_until,
      prefs.alarms[0].hour, prefs.alarms[0].minute, prefs.alarms[0].enabled,
      prefs.alarms[1].hour, prefs.alarms[1].minute, prefs.alarms[1].enabled,
      prefs.alarms[2].hour, prefs.alarms[2].minute, prefs.alarms[2].enabled,
      fallback_items, rt.time_hour, rt.time_minute, calendar_state().summary,
      rt.time_day_of_week);
  return fallback_items;
}

bool TouchGestureEngine::handle_tap_home_area_(
    int tx, int /*ty*/, uint32_t now_rel, const HitRegion *hit, uint16_t wid) {
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  if (rs.nav.gui_page == PageId::CLOCK) {
    if (prefs.home_screen_enabled && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.nav.gui_page = PageId::HOME;
      rs.nav.home_subpage = 0;
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return true;
  }

  if (rs.nav.gui_page == PageId::HOME) {
    if (rs.nav.home_subpage == 0) {
      int act_count = 0;
      const HomeActivityItem *act_items =
          TouchGestureEngine::resolve_home_activities_(now_rel, rs, prefs, rt_, act_count);
      if (wid == WidgetId::HOME_QUICK_BTN && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.nav.gui_page = PageId::WEATHER;
        rs.nav.sky_weather_subpage = (weather_state().sun_state == "below_horizon") ? 3 : 2;
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (wid == WidgetId::HOME_DATE_HERO && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        TouchGestureEngine::launch_dock_app_(PageId::CALENDAR, rs, rt_, now_rel);
        rs.cal.subpage = (prefs.home_date_tap_action == 0U) ? CalendarSubpage::DAY : CalendarSubpage::OVERVIEW;
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (wid == WidgetId::HOME_TIME_READOUT && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        if (prefs.home_clock_tap_action == 0U) {
          TouchGestureEngine::launch_dock_app_(PageId::TIME, rs, rt_, now_rel);
        } else {
          rs.nav.gui_page = PageId::CLOCK;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (wid == WidgetId::HOME_WEATHER_HERO && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.nav.gui_page = PageId::WEATHER;
        rs.nav.sky_weather_subpage = 0;
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (wid == WidgetId::HOME_ACT_EXPAND && act_count > 0 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        home_activity_overlay_page() = 0;
        rs.nav.home_subpage = 2;
        mark_dirty_full();
        rt_.schedule_disp();
      } else if ((wid == WidgetId::HOME_ACT_ROW_0 || wid == WidgetId::HOME_ACT_ROW_1 ||
                  wid == WidgetId::HOME_ACT_ACTION_0 || wid == WidgetId::HOME_ACT_ACTION_1) &&
                 act_count > 0 && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const bool quick_tap = (wid == WidgetId::HOME_ACT_ACTION_0 || wid == WidgetId::HOME_ACT_ACTION_1);
        TouchGestureEngine::handle_home_activity_action_(act_items, act_count, hit->param, quick_tap, now_rel, rs, prefs, rt_);
      }
      return true;
    }

    if (rs.nav.home_subpage == 1) {
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        rs.nav.home_settings_page = (wid == WidgetId::CHEVRON_UP) ? 1 : 2;
        rt_.schedule_disp();
      } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 5 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int item_idx = hit->param;
        if (item_idx == 0) {
          rt_.set_select_opt(UiSelectId::HOME_CLOCK_TAP_ACTION,
                             (prefs.home_clock_tap_action == 0U) ? "Clock Face" : "Time App");
        } else if (item_idx == 1) {
          rt_.set_select_opt(UiSelectId::HOME_DATE_TAP_ACTION,
                             (prefs.home_date_tap_action == 0U) ? "Overview" : "Day View");
        } else if (item_idx == 2) {
          rt_.sw_toggle(UiSwitchId::HOME_WEATHER_BG);
        } else if (item_idx == 3) {
          rt_.sw_toggle(UiSwitchId::HOME_WAKE_GREETING);
        } else if (item_idx == 4 || item_idx == 5) {
          const PrefKey pk = (item_idx == 4) ? PrefKey::HOME_SUN : PrefKey::HOME_MOON;
          const bool next_v = (item_idx == 4) ? !prefs.home_sun : !prefs.home_moon;
          uint32_t eff = 0U;
          if (latter_actions::record_local_pref_edit(pk, next_v ? "1" : "0", now_rel, eff)) {
            device_prefs_state().last_effects |= eff;
            rt_.cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
          }
        } else if (item_idx == 6) {
          rt_.sw_toggle(UiSwitchId::WEATHER_APP_BG);
        }
        rt_.schedule_disp();
      }
      return true;
    }

    if (rs.nav.home_subpage == 2) {
      int act_count = 0;
      const HomeActivityItem *act_items = home_activity_rendered_items(act_count);
      if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 5 && act_count > 0) {
        if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const bool quick_tap = (tx >= hit->x1 - scale_x(56));
          TouchGestureEngine::handle_home_activity_action_(act_items, act_count, hit->param, quick_tap, now_rel, rs, prefs, rt_);
        }
      } else if (wid == WidgetId::NONE &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_DISMISS, tkey)) {
        rs.nav.home_subpage = 0;
        home_activity_overlay_page() = 0;
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
    return true;
  }

  if (rs.nav.gui_page == PageId::PINNED) {
    if (!rs.nav.menu_scroll_active) {
      const int total_pages = pinned_total_pages();
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        if (total_pages > 1) {
          const int cur_pg = std::clamp(rs.nav.pinned_menu_page, 0, total_pages - 1);
          rs.nav.pinned_menu_page = cur_pg;
          const int target_pg = std::clamp(cur_pg + ((wid == WidgetId::CHEVRON_DOWN) ? 1 : -1), 0, total_pages - 1);
          if (target_pg != cur_pg) {
            rs.nav.menu_scroll_active = true;
            rs.nav.menu_scroll_dir = (target_pg > cur_pg) ? 1 : -1;
            rs.nav.menu_scroll_target_page = target_pg;
            rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
          }
        }
      } else if (wid >= WidgetId::PINNED_CARD_BASE && wid < WidgetId::PINNED_CARD_BASE + 6 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const auto &sc = shortcuts_state();
        const int idx = hit->param;
        if (idx >= 0 && static_cast<size_t>(idx) < sc.pinned_items.size()) {
          const ShortcutItem item_copy = sc.pinned_items[static_cast<size_t>(idx)];
          TouchGestureEngine::activate_shortcut_item_(item_copy, ShortcutOrigin::PINNED_APP, rs, rt_, now_rel);
        }
      }
    }
    return true;
  }

  static constexpr const char *kClimModes[6] = {"heat", "cool", "heat_cool", "off", "fan_only", "dry"};
  static constexpr const char *kClimLabels[6] = {"Heat", "Cool", "Auto", "Off", "Fan", "Dry"};

  if (rs.nav.gui_page == PageId::AREA) {
    if (rs.nav.area_subpage == AreaSubpage::PICKER || rs.area.browse_mode == "areas") {
      int &a_pg = rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST);
      const int a_tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::AREA_LIST);
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) && a_tot > 1 &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        const int next_pg = std::clamp(a_pg + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, a_tot);
        if (next_pg != a_pg) {
          a_pg = next_pg;
          TouchGestureEngine::do_fetch_area_list_browse_();
          rt_.schedule_disp();
        }
      } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 5 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int s_idx = std::clamp(static_cast<int>(hit->param), 0, 4);
        const auto &asl = rs.area_list_slots()[s_idx];
        if (!asl.id.empty()) {
          rs.area.active_area_id = asl.id;
          rs.area.active_area_name = strip_trailing_paren_state(asl.name);
          rs.nav.area_subpage = AreaSubpage::OVERVIEW;
          rs.area.browse_mode = "entities";
          rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
          sync_home_hub_for_active_area(asl.id, home_hub_state());
          do_select_active_area_latter();
          TouchGestureEngine::do_fetch_area_browse_();
          mark_dirty_full();
          rt_.schedule_disp();
        }
      }
      return true;
    }

    if (rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL && light_insp_domain() == "climate") {
      auto &hub = home_hub_state();
      ThermostatZoneItem *ci = find_climate_item_by_id(light_insp_entity_id(), hub);
      const uint16_t c_caps = (ci != nullptr && ci->caps != 0U) ? ci->caps : kClimateCapDefault;
      const bool is_c = (c_caps & kClimateCapCelsius) != 0U;
      const int sl_i = light_insp_slot();
      if ((wid == WidgetId::CLIMATE_MINUS_BTN || wid == WidgetId::CLIMATE_PLUS_BTN) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        const int cur_t = (ci != nullptr && ci->target_temp > 0)
                              ? ci->target_temp
                              : ((light_insp_bri_pct() > 0) ? light_insp_bri_pct() : (is_c ? 21 : 70));
        const int next_t = step_thermostat_setpoint(
            cur_t, (wid == WidgetId::CLIMATE_PLUS_BTN) ? 1 : -1, c_caps);
        if (ci != nullptr) {
          ci->target_temp = next_t;
          for (int ti = 0; ti < hub.thermostat_count; ++ti) {
            if (hub.thermostats[ti].entity_id == ci->entity_id) hub.thermostats[ti].target_temp = next_t;
          }
        }
        light_insp_bri_pct() = next_t;
        if (sl_i >= 0 && sl_i < 3) rs.area_entity_slots()[sl_i].level = next_t;
        ++hub.local_mutation_seq;
        rs.area.target_entity_id = light_insp_entity_id();
        rs.area.target_action = "set_temperature";
        rs.area.target_level = next_t;
        char fb[36];
        std::snprintf(fb, sizeof(fb), "Setpoint -> %d\xC2\xB0%c", next_t, is_c ? 'C' : 'F');
        rs.area.action_feedback = fb;
        rs.area.feedback_timer = now_rel;
        TouchGestureEngine::do_execute_area_action_();
        rt_.schedule_disp();
      } else if (wid >= WidgetId::CLIMATE_MODE_BASE && wid < WidgetId::CLIMATE_MODE_BASE + 6 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int m_i = static_cast<int>(wid - WidgetId::CLIMATE_MODE_BASE);
        const bool next_on = (m_i != 3);
        if (ci != nullptr) {
          ci->mode = kClimModes[m_i];
          for (int ti = 0; ti < hub.thermostat_count; ++ti) {
            if (hub.thermostats[ti].entity_id == ci->entity_id) hub.thermostats[ti].mode = kClimModes[m_i];
          }
        }
        light_insp_active() = next_on;
        if (sl_i >= 0 && sl_i < 3) rs.area_entity_slots()[sl_i].active = next_on;
        ++hub.local_mutation_seq;
        rs.area.target_entity_id = light_insp_entity_id();
        rs.area.target_action = std::string("hvac_") + kClimModes[m_i];
        rs.area.target_level = (ci != nullptr && ci->target_temp > 0) ? ci->target_temp : light_insp_bri_pct();
        rs.area.action_feedback = std::string("HVAC: ") + kClimLabels[m_i];
        rs.area.feedback_timer = now_rel;
        mark_dirty_full();
        TouchGestureEngine::do_execute_area_action_();
        rt_.schedule_disp();
      }
      return true;
    }

    if (rs.nav.area_subpage == AreaSubpage::OVERVIEW) {
      std::string cats[8];
      const int cat_cnt = collect_area_categories(cats);
      if ((wid == WidgetId::MODE_BAR_LEFT || wid == WidgetId::MODE_BAR_RIGHT) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        if (cat_cnt > 4) {
          area_cat_dock_page() = std::clamp(
              area_cat_dock_page() + ((wid == WidgetId::MODE_BAR_RIGHT) ? 4 : -4), 0, cat_cnt - 4);
          rt_.schedule_disp();
        }
      } else if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        if (cat_cnt > 0) {
          const int c_idx = std::clamp(static_cast<int>(hit->param), 0, cat_cnt - 1);
          if (cats[c_idx] != rs.area.domain_filter) {
            rs.area.domain_filter = cats[c_idx];
            sync_mode_bar_window(area_cat_dock_page(), c_idx, cat_cnt);
            rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
            TouchGestureEngine::do_fetch_area_browse_();
            mark_dirty_full();
            rt_.schedule_disp();
          }
        }
      } else if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        int &pg = rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES);
        const int tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::AREA_ENTITIES);
        const int next_pg = std::clamp(pg + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, std::max(1, tot));
        if (next_pg != pg) {
          pg = next_pg;
          TouchGestureEngine::do_fetch_area_browse_();
          rt_.schedule_disp();
        }
      } else if (wid == WidgetId::LIGHT_ORB_BTN &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int idx = std::clamp(static_cast<int>(hit->param), 0, 2);
        const auto &sl = rs.area_entity_slots()[idx];
        if (!sl.id.empty()) {
          light_insp_entity_id() = sl.id;
          light_insp_name() = strip_trailing_paren_state(sl.name);
          light_insp_domain() = "light";
          light_insp_slot() = idx;
          light_insp_active() = sl.active;
          light_insp_mode() = 0;
          if (sl.level > 0) light_insp_bri_pct() = sl.level;
          rs.nav.area_subpage = AreaSubpage::LIGHT_DETAIL;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (((wid >= WidgetId::AREA_CARD_TOGGLE && wid < WidgetId::AREA_CARD_TOGGLE + 3) ||
                  (wid >= WidgetId::AREA_CARD_TAP && wid < WidgetId::AREA_CARD_TAP + 3)) &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int idx = std::clamp(static_cast<int>(hit->param), 0, 2);
        auto &sl = rs.area_entity_slots()[idx];
        if (!sl.id.empty() && sl.type != "text") {
          const bool is_card_body = (wid >= WidgetId::AREA_CARD_TAP && wid < WidgetId::AREA_CARD_TAP + 3);
          if (is_card_body &&
              (sl.type == "light" || sl.type == "cover" || sl.type == "fan" || sl.type == "climate")) {
            light_insp_entity_id() = sl.id;
            light_insp_name() = strip_trailing_paren_state(sl.name);
            light_insp_domain() = sl.type;
            light_insp_slot() = idx;
            light_insp_active() = sl.active;
            light_insp_mode() = 1;
            if (sl.type == "climate") {
              const ThermostatZoneItem *ci = find_climate_item_by_id_const(sl.id);
              light_insp_bri_pct() = (ci != nullptr && ci->target_temp > 0)
                                         ? ci->target_temp
                                         : ((sl.level > 0) ? sl.level : 70);
            } else if (sl.level >= 0) {
              light_insp_bri_pct() = sl.level;
            } else {
              light_insp_bri_pct() = sl.active ? 100 : ((sl.type == "light") ? 50 : 0);
            }
            rs.nav.area_subpage = AreaSubpage::LIGHT_DETAIL;
            mark_dirty_full();
            rt_.schedule_disp();
          } else if (is_card_body && sl.type == "media_player") {
            TouchGestureEngine::launch_dock_app_(PageId::MUSIC, rs, rt_, now_rel);
          } else {
            rs.area.target_entity_id = sl.id;
            const bool next_act = (sl.type == "scene") ? true : !sl.active;
            std::string verb = "toggle";
            if (sl.type == "scene") verb = "turn_on";
            else if (sl.type == "cover") verb = next_act ? "open_cover" : "close_cover";
            else if (sl.type == "lock" || sl.id.rfind("lock.", 0) == 0) verb = next_act ? "unlock" : "lock";
            else verb = next_act ? "turn_on" : "turn_off";
            const std::string ok_fb = strip_trailing_paren_state(sl.name);
            if (gate_or_dispatch_area_action(sl.id, verb, next_act ? 100 : 0,
                                             "PERIMETER PIN", now_rel, ok_fb)) {
              sl.active = next_act;
            }
          }
        }
      }
    }
    return true;
  }

  if (rs.nav.gui_page != PageId::HOME_HUB) return false;

  int hh_tabs[kMaxHomeHubTabs]{};
  const int hh_tab_cnt = collect_home_hub_tabs(hh_tabs);
  if (hh_tab_cnt <= 0) return true;

  if ((wid == WidgetId::MODE_BAR_LEFT || wid == WidgetId::MODE_BAR_RIGHT) &&
      touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
    if (hh_tab_cnt > 4) {
      home_hub_mode_bar_page() = std::clamp(
          home_hub_mode_bar_page() + ((wid == WidgetId::MODE_BAR_RIGHT) ? 4 : -4), 0, hh_tab_cnt - 4);
      rt_.schedule_disp();
    }
  } else if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4 &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    const int slot = std::clamp(static_cast<int>(hit->param), 0, hh_tab_cnt - 1);
    const int target_tab = hh_tabs[slot];
    if (target_tab != home_hub_tab()) {
      TouchGestureEngine::enter_home_hub_tab_(target_tab, false, rs, rt_, now_rel);
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (home_hub_tab() == HomeHubTab::ALARM &&
             !area_ctx_state().alarm_entity.empty() &&
             wid >= WidgetId::ALARM_MODE_BASE && wid < WidgetId::ALARM_MODE_BASE + 5 &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    static constexpr const char *kAlarmActs[5] = {
        "disarm", "arm_home", "arm_away", "arm_night", "arm_vacation"};
    static constexpr const char *kAlarmFb[5] = {
        "Disarming System...", "Arming Home...", "Arming Away...", "Arming Night...", "Arming Vacation..."};
    const int mode_i = static_cast<int>(wid - WidgetId::ALARM_MODE_BASE);
    gate_or_dispatch_alarm_action(kAlarmActs[mode_i], now_rel, kAlarmFb[mode_i]);
  } else if (home_hub_tab() == HomeHubTab::INTERCOM) {
    if (wid == WidgetId::INTERCOM_SPEAK_BTN &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      if (broadcast_dictation_active(rs)) {
        stop_broadcast_dictation_if_active(rs, rt_, now_rel, "Cancelled");
      } else {
        start_broadcast_dictation(now_rel, rs);
        rt_.cmd(UiHostCmd::START_BROADCAST_DICTATION);
      }
      mark_dirty_full();
      rt_.schedule_disp();
    } else if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
               !broadcast_dictation_active(rs) &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      std::vector<std::string> phrases;
      const int total_cnt = parse_broadcast_phrases(area_ctx_state().broadcast_phrases, phrases);
      const int max_pg = std::max(0, (total_cnt - 1) / 3);
      home_hub_broadcast_page() = std::clamp(
          home_hub_broadcast_page() + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 0, max_pg);
      rt_.schedule_disp();
    } else if (wid >= WidgetId::INTERCOM_ROW_BASE && wid < WidgetId::INTERCOM_ROW_BASE + 5 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      stop_broadcast_dictation_if_active(rs, rt_, now_rel);
      std::vector<std::string> phrases;
      const int cnt = parse_broadcast_phrases(area_ctx_state().broadcast_phrases, phrases);
      const int phrase_idx = (hit != nullptr) ? static_cast<int>(hit->param) : -1;
      if (phrase_idx >= 0 && phrase_idx < cnt) {
        const std::string &phrase = phrases[static_cast<size_t>(phrase_idx)];
        rs.intercom.broadcast_message = phrase;
        rs.intercom.broadcast_feedback = std::string("Sent: ") + phrase;
        rs.intercom.broadcast_feedback_ms = now_rel;
        do_send_intercom_broadcast();
        rt_.schedule_disp();
      }
    }
  } else if (home_hub_tab() == HomeHubTab::THERMOSTAT && home_hub_state().thermostat_count > 0) {
    auto &hub = home_hub_state();
    int &z_idx = hub.thermostat_idx;
    z_idx = std::clamp(z_idx, 0, hub.thermostat_count - 1);
    if ((wid == WidgetId::THERMOSTAT_PREV_BTN || wid == WidgetId::THERMOSTAT_NEXT_BTN) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      if (hub.thermostat_count > 1) {
        z_idx = step_wrap(z_idx, (wid == WidgetId::THERMOSTAT_NEXT_BTN) ? 1 : -1, hub.thermostat_count);
        ++hub.local_mutation_seq;
        mark_dirty_full();
        rt_.schedule_disp();
      }
    } else if ((wid == WidgetId::CLIMATE_MINUS_BTN || wid == WidgetId::CLIMATE_PLUS_BTN) &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      auto &z = hub.thermostats[z_idx];
      const uint16_t c_caps = (z.caps != 0U) ? z.caps : kClimateCapDefault;
      const bool is_c = (c_caps & kClimateCapCelsius) != 0U;
      z.target_temp = step_thermostat_setpoint(
          z.target_temp, (wid == WidgetId::CLIMATE_PLUS_BTN) ? 1 : -1, c_caps);
      for (int ai = 0; ai < hub.area_climate_count; ++ai) {
        if (hub.area_climates[ai].entity_id == z.entity_id) hub.area_climates[ai].target_temp = z.target_temp;
      }
      ++hub.local_mutation_seq;
      rs.area.target_entity_id = z.entity_id;
      rs.area.target_action = "set_temperature";
      rs.area.target_level = z.target_temp;
      char fb[36];
      std::snprintf(fb, sizeof(fb), "Setpoint -> %d\xC2\xB0%c", z.target_temp, is_c ? 'C' : 'F');
      rs.area.action_feedback = fb;
      rs.area.feedback_timer = now_rel;
      TouchGestureEngine::do_execute_area_action_();
      rt_.schedule_disp();
    } else if (wid >= WidgetId::CLIMATE_MODE_BASE && wid < WidgetId::CLIMATE_MODE_BASE + 6 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int m_i = static_cast<int>(wid - WidgetId::CLIMATE_MODE_BASE);
      auto &z = hub.thermostats[z_idx];
      z.mode = kClimModes[m_i];
      for (int ai = 0; ai < hub.area_climate_count; ++ai) {
        if (hub.area_climates[ai].entity_id == z.entity_id) hub.area_climates[ai].mode = kClimModes[m_i];
      }
      ++hub.local_mutation_seq;
      rs.area.target_entity_id = z.entity_id;
      rs.area.target_action = std::string("hvac_") + kClimModes[m_i];
      rs.area.target_level = z.target_temp;
      rs.area.action_feedback = std::string("HVAC: ") + kClimLabels[m_i];
      rs.area.feedback_timer = now_rel;
      mark_dirty_full();
      TouchGestureEngine::do_execute_area_action_();
      rt_.schedule_disp();
    }
  } else if (home_hub_tab() == HomeHubTab::ENTRY && home_hub_state().entry_count > 0) {
    auto &hub = home_hub_state();
    if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      const int max_pg = std::max(0, (hub.entry_count - 1) / 2);
      hub.entry_page = std::clamp(hub.entry_page + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 0, max_pg);
      ++hub.local_mutation_seq;
      rt_.schedule_disp();
    } else if (wid >= WidgetId::ENTRY_ACTION_BASE && wid < WidgetId::ENTRY_ACTION_BASE + 2 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int e_idx = (hit != nullptr) ? static_cast<int>(hit->param) : -1;
      if (e_idx >= 0 && e_idx < hub.entry_count) {
        auto &item = hub.entries[e_idx];
        rs.area.target_entity_id = item.entity_id;
        if (item.is_lock) {
          const bool want_lock = (item.lock_state == LockState::UNLOCKED);
          const std::string ok_fb =
              (!item.name.empty() ? item.name : std::string("Door Lock")) +
              (want_lock ? ": Locked" : ": Unlocked");
          (void)gate_or_dispatch_area_action(
              item.entity_id, want_lock ? "lock" : "unlock",
              want_lock ? 100 : 0, "UNLOCK DOOR PIN", now_rel, ok_fb);
        } else {
          const bool want_open = (item.garage_state != GarageState::OPEN);
          const std::string g_lbl = !item.name.empty() ? item.name : std::string("Garage Door");
          const std::string ok_fb = (want_open ? "Opening " : "Closing ") + g_lbl + "...";
          (void)gate_or_dispatch_area_action(
              item.entity_id, want_open ? "open_cover" : "close_cover",
              want_open ? 100 : 0, "GARAGE DOOR PIN", now_rel, ok_fb);
        }
      }
    }
  } else if (home_hub_tab() == HomeHubTab::VACUUM && home_hub_state().vacuum_count > 0) {
    auto &hub = home_hub_state();
    int &v_idx = hub.vacuum_idx;
    v_idx = std::clamp(v_idx, 0, hub.vacuum_count - 1);
    if ((wid == WidgetId::VACUUM_PREV_BTN || wid == WidgetId::VACUUM_NEXT_BTN) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      if (hub.vacuum_count > 1) {
        v_idx = step_wrap(v_idx, (wid == WidgetId::VACUUM_NEXT_BTN) ? 1 : -1, hub.vacuum_count);
        ++hub.local_mutation_seq;
        mark_dirty_full();
        rt_.schedule_disp();
      }
    } else if (wid >= WidgetId::VACUUM_ACTION_BASE && wid < WidgetId::VACUUM_ACTION_BASE + 6 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      static constexpr const char *kVacActs[6] = {
          "start", "pause", "return_to_base", "stop", "clean_spot", "locate"};
      static constexpr const char *kVacStates[6] = {
          "Cleaning", "Paused", "Returning", "Idle", "Cleaning", nullptr};
      static constexpr const char *kVacFb[6] = {
          "Vacuum Started", "Vacuum Paused", "Returning to Dock",
          "Vacuum Stopped", "Spot Cleaning", "Locating Vacuum..."};
      auto &v = hub.vacuums[v_idx];
      const int v_act = static_cast<int>(wid - WidgetId::VACUUM_ACTION_BASE);
      rs.area.target_entity_id = v.entity_id;
      if (kVacStates[v_act] != nullptr) v.state = kVacStates[v_act];
      rs.area.target_action = kVacActs[v_act];
      rs.area.action_feedback = kVacFb[v_act];
      ++hub.local_mutation_seq;
      rs.area.feedback_timer = now_rel;
      mark_dirty_full();
      TouchGestureEngine::do_execute_area_action_();
      rt_.schedule_disp();
    }
  }

  return true;
}


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
  latter_actions::emit_media_play_item(spk, media_id, media_type, enqueue, radio_mode);
}
std::string TouchGestureEngine::resolve_music_speaker_entity_(const DeviceRuntimeState &rs) {
  const auto &ms = media_state();
  if (!ms.active_speaker.empty()) return ms.active_speaker;
  if (!rs.music.active_speaker_entity.empty()) return rs.music.active_speaker_entity;
  return ms.local_speaker;
}
int TouchGestureEngine::fetch_music_browse_for_current_view_(const DeviceRuntimeState &rs, uint32_t now_ms) {
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
  return latter_actions::emit_music_browse(
      mode_s, page, search_sv, rs.music.active_speaker_entity,
      media_state().last_speaker, alb_sort, art_sort, pod_sort, psize, now_ms);
}
bool TouchGestureEngine::open_music_row_action_sheet_(int slot_idx, DeviceRuntimeState &rs, uint32_t now_ms) {
  if (slot_idx < 0 || slot_idx >= LATTER_SLOT_COUNT) return false;
  const auto &sl = rs.music_slots()[slot_idx];
  const bool is_queue = (rs.music.view == MusicView::QUEUE);
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
    m.drilldown = MusicDrilldown::NONE;
    m.target_speaker_id.clear();
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
  latter_actions::emit_music_lyrics_fetch(track_key, speaker_entity, now_ms);
}

void activate_music_peer_tab(MusicView target, uint32_t now_ms) {
  auto &rs = runtime_state();
  const auto &rt = touch_engine().runtime();
  auto &m = rs.music;

  m.overlay_mode = 0;
  m.drilldown = MusicDrilldown::NONE;
  m.takeover = MusicTakeover::NONE;
  m.clear_lyrics_auto_state();
  m.target_speaker_id.clear();
  m.hud_active = true;
  m.hud_timer = now_ms;
  rs.nav.menu_scroll_active = false;
  m.view = target;
  m.sync_carousel_for_view();

  std::string t_title, t_artist, t_album;
  bool is_speech = false;
  resolve_music_track_strings(rs, rt, t_title, t_artist, t_album, is_speech);

  auto prime_browse = [&](const char *mode, std::string title, const char *loading_msg) {
    m.selected_category_mode = mode;
    m.selected_category_title = std::move(title);
    rs.slot_page_for(latter_rpc::SlotChannel::MUSIC) = 1;
    rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
    reset_music_loading_slots(rs, loading_msg);
    TouchGestureEngine::fetch_music_browse_for_current_view_(rs, now_ms);
  };

  switch (target) {
    case MusicView::NOW_PLAYING:
      break;
    case MusicView::LIBRARY:
      m.selected_category_mode = "categories";
      m.selected_category_title = "LIBRARY";
      m.category_page = 1;
      m.drilldown_parent_mode.clear();
      break;
    case MusicView::SPEAKERS:
      prime_browse("speakers", "SPEAKERS", "Loading...");
      break;
    case MusicView::VOLUME:
      m.takeover_interacted_ms = (now_ms == 0U) ? 1U : now_ms;
      break;
    case MusicView::QUEUE:
      prime_browse("active_queue", "PLAYBACK QUEUE", "Loading queue...");
      break;
    case MusicView::LYRICS: {
      auto &lyr = m.lyrics;
      lyr.manual_override_until_ms = 0U;
      const std::string key = build_music_track_key(t_title, t_artist);
      if (!key.empty() && lyr.track_key != key) {
        lyr.track_key = key;
        lyr.loading = true;
        lyr.lines.clear();
        lyr.active_line_idx = -1;
        lyr.scroll_px = 0;
        lyr.target_scroll_px = 0;
        lyr.scroll_anim_start_ms = 0U;
        request_music_lyrics_refresh(key, TouchGestureEngine::resolve_music_speaker_entity_(rs), now_ms);
      }
      break;
    }
    case MusicView::ALBUM_TRACKS:
      m.drilldown_album_id = !t_album.empty() ? t_album : t_artist;
      prime_browse(
          is_speech ? "podcast_episodes" : "current_album_tracks",
          t_album.empty() ? std::string(is_speech ? "EPISODES" : "ALBUM TRACKS") : t_album,
          is_speech ? "Loading episodes..." : "Loading album tracks...");
      break;
    case MusicView::INFO:
      m.info_scroll_px = 0;
      m.drilldown_album_id = t_title + "|" + t_artist + "|" + t_album;
      prime_browse("bio", "TRACK INFO", "Loading...");
      break;
    case MusicView::SETTINGS:
    default:
      m.settings_page = 1;
      m.selected_category_mode = "settings";
      m.selected_category_title = "MUSIC SETTINGS";
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
        latter_actions::emit_media_shuffle_toggle(spk);
        rt_.schedule_disp();
      } else if (wid == WidgetId::MUSIC_REPEAT_BTN &&
                 (ms.media_caps & kMediaCapRepeatSet) != 0U &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        ms.repeat_mode = static_cast<uint8_t>((ms.repeat_mode + 1U) % 3U);
        ms.repeat = (ms.repeat_mode != 0U);
        latter_actions::emit_media_repeat_cycle(spk);
        rt_.schedule_disp();
      } else if (wid == WidgetId::MUSIC_FAVORITE_BTN &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        ms.favorite = !ms.favorite;
        latter_actions::emit_media_favorite_toggle("", spk);
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
            latter_actions::emit_media_queue_play(qid, spk);
          } else if (m.action_sheet_slot >= 0) {
            rt_.cmd(UiHostCmd::PLAY_MUSIC_SLOT, m.action_sheet_slot + 1);
          }
          return_now_playing_dirty();
        } else if ((wid == WidgetId::MUSIC_ACTION_PLAY_NEXT ||
                    wid == WidgetId::MUSIC_ACTION_MOVE_UP ||
                    wid == WidgetId::MUSIC_ACTION_MOVE_DOWN ||
                    wid == WidgetId::MUSIC_ACTION_REMOVE_QUEUE) && !qid.empty()) {
          if (wid == WidgetId::MUSIC_ACTION_PLAY_NEXT) {
            latter_actions::emit_media_queue_move(qid, "next", spk);
            m.action_feedback = "Moved Next";
          } else if (wid == WidgetId::MUSIC_ACTION_MOVE_UP) {
            latter_actions::emit_media_queue_move(qid, -1, spk);
            m.action_feedback = "Moved Up";
          } else if (wid == WidgetId::MUSIC_ACTION_MOVE_DOWN) {
            latter_actions::emit_media_queue_move(qid, 1, spk);
            m.action_feedback = "Moved Down";
          } else {
            latter_actions::emit_media_queue_delete(qid, spk);
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
      m.direct_nav = false;
      activate_music_peer_tab(step_music_peer_tab(m.view, (wid == WidgetId::MODE_BAR_RIGHT) ? 1 : -1), now_rel);
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return true;
  }
  if (m.drilldown == MusicDrilldown::NONE &&
      wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4) {
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

  if (m.view == MusicView::VOLUME) {
    if ((wid == WidgetId::MUSIC_VOL_UP_STEP || wid == WidgetId::MUSIC_VOL_DOWN_STEP) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      const int dir = (wid == WidgetId::MUSIC_VOL_UP_STEP) ? 1 : -1;
      if (ms.muted && dir > 0) ms.muted = false;
      step_music_volume_feedback(dir, rt_.num_val(UiNumberId::VOLUME_STEP_SIZE),
                                 ms.is_local, rt_.local_mp_volume, now_rel, m);
      m.hud_timer = now_rel;
      rt_.cmd(UiHostCmd::MUSIC_STEP_VOLUME, dir);
      rt_.schedule_disp();
    } else if (wid == WidgetId::MUSIC_VOL_MUTE_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      ms.muted = !ms.muted;
      m.hud_timer = now_rel;
      latter_actions::emit_media_mute_toggle(spk);
      rt_.schedule_disp();
    }
    return true;
  }
  if (m.view == MusicView::LYRICS || m.view == MusicView::INFO) {
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
    if (m.view == MusicView::LYRICS) {
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
          latter_actions::emit_media_seek(static_cast<int>(t_ms / 1000));
          rt_.schedule_disp();
        }
        return true;
      }
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

  if (m.view == MusicView::SETTINGS) {
    if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      m.settings_page = (wid == WidgetId::CHEVRON_UP) ? 1 : 2;
      m.hud_timer = now_rel;
      rt_.schedule_disp();
    } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 4 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int idx = (std::clamp(m.settings_page, 1, 2) - 1) * 4 + static_cast<int>(hit->param);
      cycle_music_pref_slot(rt_, idx);
      m.hud_timer = now_rel;
      rt_.schedule_disp();
    }
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

  if (m.view == MusicView::SPEAKERS) {
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
    } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + kMusicPeerListPageSize &&
               now_rel >= music_scroll_tap_lockout_until_ms() &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int si = std::clamp(static_cast<int>(hit->param), 0, kMusicPeerListPageSize - 1);
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

  if (m.view == MusicView::QUEUE) {
    if (wid == WidgetId::MUSIC_SHUFFLE_BTN && (ms.media_caps & kMediaCapShuffleSet) != 0U &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      ms.shuffle = !ms.shuffle;
      m.hud_timer = now_rel;
      latter_actions::emit_media_shuffle_toggle(spk);
      rt_.schedule_disp();
    } else if (wid == WidgetId::MUSIC_REPEAT_BTN && (ms.media_caps & kMediaCapRepeatSet) != 0U &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      ms.repeat_mode = static_cast<uint8_t>((ms.repeat_mode + 1U) % 3U);
      ms.repeat = (ms.repeat_mode != 0U);
      m.hud_timer = now_rel;
      latter_actions::emit_media_repeat_cycle(spk);
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
    } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + kMusicPeerListPageSize &&
               now_rel >= music_scroll_tap_lockout_until_ms() &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int si = std::clamp(static_cast<int>(hit->param), 0, kMusicPeerListPageSize - 1);
      const auto &sl = rs.music_slots()[si];
      if (!sl.queue_item_id.empty()) {
        audio_session().set_play_intent(MusicPlayIntent::ExternalOrUserExplicit, now_rel);
        music_queue_cleared() = false;
        m.action_feedback = "Playing: " + sl.name;
        m.feedback_timer = now_rel;
        latter_actions::emit_media_queue_play(sl.queue_item_id, spk);
        return_now_playing_dirty();
      } else if (!sl.id.empty() && sl.type != "text") {
        rt_.cmd(UiHostCmd::PLAY_MUSIC_SLOT, si + 1);
        return_now_playing_dirty();
      }
    }
    return true;
  }

  if (m.view == MusicView::LIBRARY || m.view == MusicView::ALBUM_TRACKS) {
    const bool on_cat_root = (m.view == MusicView::LIBRARY) && (m.drilldown == MusicDrilldown::NONE);
    if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      m.hud_timer = now_rel;
      if (on_cat_root) {
        m.category_page = (wid == WidgetId::CHEVRON_UP) ? 1 : 2;
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
               wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 4 &&
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
    } else if (wid >= WidgetId::MUSIC_ROW_PLAY_BASE && wid < WidgetId::MUSIC_ROW_PLAY_BASE + 5 &&
               now_rel >= music_scroll_tap_lockout_until_ms() &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int si = std::clamp(static_cast<int>(hit->param), 0, 4);
      const auto &sl = rs.music_slots()[si];
      if (!sl.id.empty() && sl.type != "text") {
        TouchGestureEngine::dispatch_music_play_item_(sl.id, resolve_browse_item_media_type(rs, sl), sl.name, "play", false, rs, now_rel);
        return_now_playing_dirty();
      }
    } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 5 &&
               now_rel >= music_scroll_tap_lockout_until_ms() &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int si = std::clamp(static_cast<int>(hit->param), 0, 4);
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



bool TouchGestureEngine::handle_down_time_cal_(TouchGestureEngine &eng, int /*tx*/, int /*ty*/,
                                             uint32_t now_ms, const HitRegion * /*hit*/,
                                             uint16_t wid, uint32_t tkey) {
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  auto &rt = eng.rt_;
  auto &gest = touch_gesture();

  if (rs.nav.gui_page == PageId::TIME) {
    const int sub = std::clamp(rs.nav.time_subpage, 0, 3);

    if (sub == TimeSubpage::TIMERS && timer_view_slot() > 0) {
      int &view_idx = timer_view_slot();
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        gest.active = false;
        view_idx = 0;
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      const int s_idx = std::clamp(view_idx - 1, 0, 2);
      auto &s = timer_slots()[s_idx];
      if (wid == WidgetId::TIMER_PRIMARY_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        gest.active = false;
        toggle_timer_slot_run(s, now_ms, rt);
        return true;
      }
      if (wid >= WidgetId::STEPPER_BASE && wid < WidgetId::STEPPER_BASE + 4) {
        gest.active = false;
        if (!s.running && !s.paused && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID)) {
          static constexpr int kStepSecs[4] = {-300, -60, 60, 300};
          const int col = static_cast<int>(wid - WidgetId::STEPPER_BASE);
          s.duration_sec = std::clamp(s.duration_sec + kStepSecs[col], 60, 3600);
          s.remaining_sec = s.duration_sec;
          sync_multi_timer_state();
          rt.apply_power_policy();
          rt.schedule_disp();
        }
        return true;
      }
      if (wid == WidgetId::TIMER_PLUS_1M_BTN && (s.running || s.paused) &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID)) {
        gest.active = false;
        if (s.running) {
          const int cur_rem = (s.target_ms > now_ms)
                                  ? static_cast<int>((s.target_ms - now_ms + 999U) / 1000U)
                                  : 0;
          const int next_rem = std::min(36000, cur_rem + 60);
          s.remaining_sec = next_rem;
          s.target_ms = now_ms + static_cast<uint32_t>(next_rem) * 1000U;
        } else {
          s.remaining_sec = std::min(36000, s.remaining_sec + 60);
        }
        s.duration_sec = std::max(s.duration_sec, s.remaining_sec);
        sync_multi_timer_state();
        rt.apply_power_policy();
        rt.schedule_disp();
        return true;
      }
      if (wid == WidgetId::TIMER_RESET_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        gest.active = false;
        if (s.running || s.paused) {
          s.running = false;
          s.paused = false;
          s.remaining_sec = s.duration_sec;
        } else {
          s.name.clear();
          s.id.clear();
          s.duration_sec = (s_idx + 1) * 300;
          s.remaining_sec = s.duration_sec;
        }
        sync_multi_timer_state();
        rt.apply_power_policy();
        rt.schedule_disp();
      }
      return true;
    }

    if (sub == TimeSubpage::ALARMS && rs.alarms.edit_idx > 0) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        gest.active = false;
        rs.alarms.edit_idx = 0;
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      if ((wid == WidgetId::ALARM_HH_BTN || wid == WidgetId::ALARM_MM_BTN) &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID)) {
        gest.active = false;
        rs.nav.alarm_edit_field = (wid == WidgetId::ALARM_HH_BTN) ? 0 : 1;
        rt.schedule_disp();
        return true;
      }
      const int a_idx = std::clamp(rs.alarms.edit_idx - 1, 0, 2);
      auto &ap = prefs.alarms[a_idx];
      if (wid >= WidgetId::STEPPER_BASE && wid < WidgetId::STEPPER_BASE + 4) {
        gest.active = false;
        if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID)) {
          const int col = static_cast<int>(wid - WidgetId::STEPPER_BASE);
          if (rs.nav.alarm_edit_field == 0) {
            static constexpr int kHSteps[4] = {-6, -1, 1, 6};
            ap.hour = static_cast<uint8_t>((static_cast<int>(ap.hour) + 24 + kHSteps[col]) % 24);
          } else {
            static constexpr int kMSteps[4] = {-5, -1, 1, 5};
            ap.minute = static_cast<uint8_t>((static_cast<int>(ap.minute) + 60 + kMSteps[col]) % 60);
          }
          latter_actions::sync_local_alarms_edit(now_ms);
          rt.schedule_disp();
        }
        return true;
      }
      if (wid == WidgetId::ALARM_RECUR_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        gest.active = false;
        ap.recurrence = static_cast<uint8_t>((ap.recurrence + 1) % 4);
        latter_actions::sync_local_alarms_edit(now_ms);
        rt.schedule_disp();
      }
      return true;
    }

    if (sub == TimeSubpage::STOPWATCH) {
      if (wid == WidgetId::SW_PRIMARY_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        gest.active = false;
        if (!rs.stopwatch.running) {
          rs.stopwatch.running = true;
          rs.stopwatch.started_in_analog = false;
          rs.stopwatch.start_time_ms = now_ms - rs.stopwatch.elapsed_ms;
          rs.nav.sleep_start_time = 0;
        } else {
          rs.stopwatch.running = false;
          rs.stopwatch.elapsed_ms = now_ms - rs.stopwatch.start_time_ms;
        }
        latter_actions::check_and_emit_timer_alarm_state(now_ms);
        rt.schedule_disp();
        return true;
      }
      if (wid == WidgetId::SW_LAP_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID)) {
        gest.active = false;
        if (rs.stopwatch.running) {
          record_stopwatch_lap(rs.stopwatch, now_ms - rs.stopwatch.start_time_ms);
          rt.schedule_disp();
        }
        return true;
      }
      if (wid == WidgetId::SW_RESET_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        gest.active = false;
        rs.stopwatch.reset();
        latter_actions::check_and_emit_timer_alarm_state(now_ms);
        rt.schedule_disp();
      }
    }
    return true;
  }

  if (rs.nav.gui_page != PageId::CALENDAR) return false;

  const bool touch_time_ok = rt.time_valid && rt.time_year >= 2020;
  const int32_t today_ymd = TouchGestureEngine::resolve_touch_today_ymd_(rt);
  static bool s_touch_cal_anchored = false;
  if (!touch_time_ok) {
    s_touch_cal_anchored = false;
  }
  if (rs.cal.selected_ymd <= 0 ||
      (touch_time_ok && !s_touch_cal_anchored && rs.cal.selected_ymd == 20260101)) {
    rs.cal.selected_ymd = today_ymd;
  }
  if (rs.cal.draft_ymd <= 0 ||
      (touch_time_ok && !s_touch_cal_anchored && rs.cal.draft_ymd == 20260101)) {
    rs.cal.draft_ymd = today_ymd;
  }
  if (touch_time_ok) {
    s_touch_cal_anchored = true;
  }

  if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
    gest.active = false;
    if (rs.cal.delete_confirm_open) {
      rs.cal.delete_confirm_open = false;
    } else if (rs.cal.drilldown == CalDrilldown::DETAIL) {
      rs.cal.drilldown = CalDrilldown::NONE;
    } else if ((rs.cal.drilldown >= CalDrilldown::PICK_TITLE &&
                rs.cal.drilldown <= CalDrilldown::PICK_REPEAT) ||
               rs.cal.drilldown == CalDrilldown::ADD_STEP2) {
      rs.cal.drilldown = CalDrilldown::ADD_STEP1;
    } else if (rs.cal.drilldown == CalDrilldown::ADD_STEP1) {
      stop_calendar_dictation_if_active(rs, rt, now_ms);
      rs.cal.drilldown = CalDrilldown::NONE;
    } else if (TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms)) {
      return true;
    }
    mark_dirty_full();
    rt.schedule_disp();
    return true;
  }

  if (rs.cal.drilldown == CalDrilldown::NONE &&
      (wid == WidgetId::CAL_NAV_PREV || wid == WidgetId::CAL_NAV_NEXT) &&
      touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
    gest.active = false;
    const int dir = (wid == WidgetId::CAL_NAV_NEXT) ? 1 : -1;
    if (rs.cal.subpage == CalendarSubpage::OVERVIEW) {
      static constexpr uint8_t kHorizons[4] = {7U, 14U, 30U, 60U};
      const uint8_t cur_d = snap_calendar_lookahead_days(prefs.calendar_lookahead_days);
      int idx = 2;
      for (int i = 0; i < 4; ++i) {
        if (kHorizons[i] == cur_d) { idx = i; break; }
      }
      const uint8_t next_d = kHorizons[std::clamp(idx + dir, 0, 3)];
      if (next_d != cur_d) {
        char d_str[8];
        std::snprintf(d_str, sizeof(d_str), "%u", static_cast<unsigned>(next_d));
        uint32_t eff = 0U;
        if (latter_actions::record_local_pref_edit(PrefKey::CALENDAR_LOOKAHEAD_DAYS, d_str, now_ms, eff)) {
          device_prefs_state().last_effects |= eff;
          rt.cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
        }
        rs.cal.overview_page = 0;
        ensure_calendar_window_for_ymd(today_ymd, prefs);
      }
    } else if (rs.cal.subpage == CalendarSubpage::DAY) {
      rs.cal.selected_ymd = add_days_ymd(rs.cal.selected_ymd, dir);
      rs.cal.day_page = 0;
      ensure_calendar_window_for_ymd(rs.cal.selected_ymd, prefs);
    } else if (rs.cal.subpage == CalendarSubpage::WEEK) {
      rs.cal.selected_ymd = add_days_ymd(rs.cal.selected_ymd, dir * 7);
      rs.cal.week_page = 0;
      ensure_calendar_window_for_ymd(rs.cal.selected_ymd, prefs);
    } else if (rs.cal.subpage == CalendarSubpage::MONTH) {
      rs.cal.selected_ymd = add_months_ymd(rs.cal.selected_ymd, dir);
      ensure_calendar_window_for_ymd(rs.cal.selected_ymd, prefs);
    }
    mark_dirty_full();
    rt.schedule_disp();
    return true;
  }

  if (rs.cal.drilldown == CalDrilldown::ADD_STEP2) {
    if ((wid == WidgetId::CAL_ADD_DATE_PREV || wid == WidgetId::CAL_ADD_DATE_NEXT) &&
        touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
      gest.active = false;
      rs.cal.draft_ymd = add_days_ymd(rs.cal.draft_ymd, (wid == WidgetId::CAL_ADD_DATE_NEXT) ? 1 : -1);
      rt.schedule_disp();
      return true;
    }
    if (!rs.cal.draft_all_day &&
        wid >= WidgetId::STEPPER_BASE && wid < WidgetId::STEPPER_BASE + 4 &&
        touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
      gest.active = false;
      static constexpr int kDeltas[4] = {-60, -15, 15, 60};
      const int delta = kDeltas[static_cast<int>(wid - WidgetId::STEPPER_BASE)];
      if (rs.cal.draft_time_field == 0U) {
        const int dur = std::max(15, static_cast<int>(rs.cal.draft_end_min) - static_cast<int>(rs.cal.draft_start_min));
        const int next_s = std::clamp(static_cast<int>(rs.cal.draft_start_min) + delta, 0, 1425);
        rs.cal.draft_start_min = static_cast<int16_t>(next_s);
        rs.cal.draft_end_min = static_cast<int16_t>(std::clamp(next_s + dur, next_s + 15, 1440));
      } else {
        const int min_e = static_cast<int>(rs.cal.draft_start_min) + 15;
        rs.cal.draft_end_min = static_cast<int16_t>(
            std::clamp(static_cast<int>(rs.cal.draft_end_min) + delta, min_e, 1440));
      }
      rt.schedule_disp();
    }
  }
  return true;
}

bool TouchGestureEngine::handle_tap_time_cal_(
    int /*tx*/, int /*ty*/, uint32_t now_rel, const HitRegion *hit, uint16_t wid) {
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  if (rs.nav.gui_page == PageId::TIME) {
    const int sub = std::clamp(rs.nav.time_subpage, 0, 3);
    if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4 &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int target_sub = std::clamp(static_cast<int>(hit->param), 0, 3);
      if (target_sub != sub) {
        rs.nav.time_subpage = target_sub;
        mark_dirty_full();
        rt_.schedule_disp();
      }
    } else if (sub == TimeSubpage::CLOCK) {
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        WorldClockEntry wc_entries[kMaxWorldClocks + 1]{};
        const int tot_wc = collect_active_world_clocks(prefs, area_ctx_state(), wc_entries);
        const int max_pg = std::max(0, (tot_wc - 1) / 2);
        rs.nav.world_clock_page = std::clamp(
            rs.nav.world_clock_page + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 0, max_pg);
        rt_.schedule_disp();
      }
    } else if (sub == TimeSubpage::TIMERS && timer_view_slot() == 0) {
      if (wid >= WidgetId::TIMER_TAB_BASE && wid < WidgetId::TIMER_TAB_BASE + 3 &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        timer_view_slot() = std::clamp(static_cast<int>(hit->param), 0, 2) + 1;
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (wid >= WidgetId::TIMER_PRESET_BASE && wid < WidgetId::TIMER_PRESET_BASE + 3 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int idx = std::clamp(static_cast<int>(hit->param), 0, 2);
        toggle_timer_slot_run(timer_slots()[idx], now_rel, rt_);
      }
    } else if (sub == TimeSubpage::ALARMS && rs.alarms.edit_idx == 0) {
      if (wid == WidgetId::ALARM_SNOOZE_CANCEL &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.alarms.snooze_until = 0;
        snoozed_alarm_slot() = 0;
        rt_.schedule_disp();
      } else if (wid >= WidgetId::ALARM_TOGGLE_BASE && wid < WidgetId::ALARM_TOGGLE_BASE + 3 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int a_idx = std::clamp(static_cast<int>(hit->param), 0, 2);
        prefs.alarms[a_idx].enabled = !prefs.alarms[a_idx].enabled;
        if (!prefs.alarms[a_idx].enabled &&
            (rs.alarms.ringing_idx == a_idx + 1 || snoozed_alarm_slot() == a_idx + 1)) {
          rs.alarms.snooze_until = 0;
          snoozed_alarm_slot() = 0;
        }
        latter_actions::sync_local_alarms_edit(now_rel);
        rt_.schedule_disp();
      } else if (wid >= WidgetId::ALARM_CARD_BASE && wid < WidgetId::ALARM_CARD_BASE + 3 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int a_idx = std::clamp(static_cast<int>(hit->param), 0, 2);
        rs.alarms.edit_idx = a_idx + 1;
        rs.nav.alarm_edit_field = 1;
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
    return true;
  }

  if (rs.nav.gui_page != PageId::CALENDAR) return false;

  const int32_t today_ymd = TouchGestureEngine::resolve_touch_today_ymd_(rt_);
  const auto &store = calendar_window_store();

  if (rs.cal.delete_confirm_open) {
    if ((wid == WidgetId::CONFIRM_CANCEL || wid == WidgetId::NONE) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cal.delete_confirm_open = false;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CONFIRM_ACCEPT &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      if (rs.cal.selected_event_idx >= 0 && rs.cal.selected_event_idx < store.count) {
        const auto &ev = store.events[rs.cal.selected_event_idx];
        CalendarSourceMeta *sources = touch_cal_sources_scratch();
        const int src_cnt = parse_calendar_sources_meta(calendar_state().calendars_meta, sources);
        const std::string_view ent_sv =
            (static_cast<int>(ev.cal_idx) < src_cnt) ? std::string_view(sources[ev.cal_idx].entity_id)
                                                     : std::string_view{};
        if (!ent_sv.empty() && ev.uid[0] != '\0') {
          latter_actions::emit_calendar_delete_event(ent_sv, ev.uid, ev.recurrence_id);
          rs.cal.feedback_text = "Event deleted";
          rs.cal.feedback_ms = now_rel;
        }
      }
      rs.cal.delete_confirm_open = false;
      rs.cal.drilldown = CalDrilldown::NONE;
      ensure_calendar_window_for_ymd(rs.cal.selected_ymd, prefs, true);
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (rs.cal.drilldown == CalDrilldown::DETAIL) {
    if (wid == WidgetId::CAL_DELETE_BTN &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cal.delete_confirm_open = true;
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (rs.cal.drilldown == CalDrilldown::ADD_STEP1) {
    if ((wid == WidgetId::CAL_ADD_TITLE_MIC || wid == WidgetId::CAL_ADD_LOC_MIC) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const uint8_t target_f = (wid == WidgetId::CAL_ADD_TITLE_MIC) ? 1U : 2U;
      if (cal_dictation_active(rs)) {
        stop_calendar_dictation_if_active(rs, rt_, now_rel, "Cancelled");
      } else {
        start_calendar_dictation(target_f, now_rel, rs);
        rt_.cmd(UiHostCmd::START_CALENDAR_DICTATION);
      }
      rt_.schedule_disp();
    } else if ((wid == WidgetId::CAL_ADD_TITLE_BTN || wid == WidgetId::CAL_ADD_LOC_BTN ||
                wid == WidgetId::CAL_ADD_CAL_BTN || wid == WidgetId::CAL_ADD_REPEAT_BTN ||
                wid == WidgetId::CAL_ADD_NEXT_BTN) &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      stop_calendar_dictation_if_active(rs, rt_, now_rel);
      rs.cal.picker_page = 0;
      if (wid == WidgetId::CAL_ADD_TITLE_BTN) rs.cal.drilldown = CalDrilldown::PICK_TITLE;
      else if (wid == WidgetId::CAL_ADD_LOC_BTN) rs.cal.drilldown = CalDrilldown::PICK_LOCATION;
      else if (wid == WidgetId::CAL_ADD_CAL_BTN) rs.cal.drilldown = CalDrilldown::PICK_CALENDAR;
      else if (wid == WidgetId::CAL_ADD_REPEAT_BTN) rs.cal.drilldown = CalDrilldown::PICK_REPEAT;
      else rs.cal.drilldown = CalDrilldown::ADD_STEP2;
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (rs.cal.drilldown == CalDrilldown::ADD_STEP2) {
    if (wid == WidgetId::CAL_ADD_ALLDAY_BTN &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cal.draft_all_day = !rs.cal.draft_all_day;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if ((wid == WidgetId::CAL_ADD_START_BTN || wid == WidgetId::CAL_ADD_END_BTN) &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      rs.cal.draft_time_field = (wid == WidgetId::CAL_ADD_START_BTN) ? 0U : 1U;
      rt_.schedule_disp();
    } else if (wid == WidgetId::CAL_ADD_SAVE_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      submit_calendar_draft_event(rs, prefs, now_rel);
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (rs.cal.drilldown >= CalDrilldown::PICK_TITLE &&
             rs.cal.drilldown <= CalDrilldown::PICK_REPEAT) {
    if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      rs.cal.picker_page = std::max(0, rs.cal.picker_page + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1));
      rt_.schedule_disp();
    } else if (wid >= WidgetId::CAL_PICKER_SLOT_BASE && wid < WidgetId::CAL_PICKER_SLOT_BASE + 5 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int opt_idx = std::max(0, static_cast<int>(hit->param));
      if (rs.cal.drilldown == CalDrilldown::PICK_TITLE && opt_idx < 10) {
        rs.cal.draft_title = kCalTitlePresets[opt_idx];
      } else if (rs.cal.drilldown == CalDrilldown::PICK_LOCATION) {
        if (opt_idx == 0) {
          rs.cal.draft_location.clear();
        } else {
          std::string_view locs[kMaxCalendarLocations]{};
          const int n_locs = parse_calendar_locations_catalog(calendar_state().locations_catalog, locs);
          if (opt_idx - 1 < n_locs) {
            rs.cal.draft_location = std::string(locs[opt_idx - 1]);
          }
        }
      } else if (rs.cal.drilldown == CalDrilldown::PICK_CALENDAR) {
        rs.cal.draft_cal_idx = static_cast<uint8_t>(opt_idx);
      } else if (rs.cal.drilldown == CalDrilldown::PICK_REPEAT && opt_idx < 7) {
        rs.cal.draft_rrule_idx = static_cast<uint8_t>(opt_idx);
      }
      rs.cal.drilldown = CalDrilldown::ADD_STEP1;
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4 &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    const int target_sub = std::clamp(static_cast<int>(hit->param), 0, 3);
    if (target_sub != rs.cal.subpage) {
      rs.cal.subpage = target_sub;
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (wid == WidgetId::CAL_ADD_BTN &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    open_calendar_composer(rs, rt_, today_ymd, now_rel);
    mark_dirty_full();
    rt_.schedule_disp();
  } else if (wid == WidgetId::CAL_TODAY_BTN &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    rs.cal.selected_ymd = today_ymd;
    rs.cal.day_page = 0;
    rs.cal.week_page = 0;
    ensure_calendar_window_for_ymd(today_ymd, prefs);
    mark_dirty_full();
    rt_.schedule_disp();
  } else if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
    const int step_d = (wid == WidgetId::CHEVRON_UP) ? -1 : 1;
    if (rs.cal.subpage == CalendarSubpage::OVERVIEW) rs.cal.overview_page = std::max(0, rs.cal.overview_page + step_d);
    else if (rs.cal.subpage == CalendarSubpage::DAY) rs.cal.day_page = std::max(0, rs.cal.day_page + step_d);
    else if (rs.cal.subpage == CalendarSubpage::WEEK) rs.cal.week_page = std::max(0, rs.cal.week_page + step_d);
    rt_.schedule_disp();
  } else if (wid >= WidgetId::CAL_EVENT_BASE && wid < WidgetId::CAL_EVENT_BASE + 5 &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    const int ev_idx = static_cast<int>(hit->param);
    if (ev_idx >= 0 && ev_idx < store.count) {
      rs.cal.selected_event_idx = ev_idx;
      rs.cal.return_subpage = rs.cal.subpage;
      rs.cal.drilldown = CalDrilldown::DETAIL;
      rs.cal.delete_confirm_open = false;
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (wid >= WidgetId::CAL_WEEK_DAY_BASE && wid < WidgetId::CAL_WEEK_DAY_BASE + 7 &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    const int32_t week_start = start_of_week_ymd(rs.cal.selected_ymd, prefs.calendar_week_start == 1U);
    rs.cal.selected_ymd = add_days_ymd(week_start, std::clamp(static_cast<int>(hit->param), 0, 6));
    rs.cal.subpage = CalendarSubpage::DAY;
    rs.cal.day_page = 0;
    ensure_calendar_window_for_ymd(rs.cal.selected_ymd, prefs);
    mark_dirty_full();
    rt_.schedule_disp();
  } else if (wid >= WidgetId::CAL_MONTH_CELL_BASE && wid < WidgetId::CAL_MONTH_CELL_BASE + 42 &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    int sy = 2026, sm = 1, sd = 1;
    (void)split_ymd(rs.cal.selected_ymd, sy, sm, sd);
    const int32_t grid_start = start_of_week_ymd(make_ymd(sy, sm, 1), prefs.calendar_week_start == 1U);
    rs.cal.selected_ymd = add_days_ymd(grid_start, std::clamp(static_cast<int>(hit->param), 0, 41));
    rs.cal.subpage = CalendarSubpage::DAY;
    rs.cal.day_page = 0;
    ensure_calendar_window_for_ymd(rs.cal.selected_ymd, prefs);
    mark_dirty_full();
    rt_.schedule_disp();
  } else if (wid >= WidgetId::CAL_MONTH_WEEK_BASE && wid < WidgetId::CAL_MONTH_WEEK_BASE + 6 &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    int sy = 2026, sm = 1, sd = 1;
    (void)split_ymd(rs.cal.selected_ymd, sy, sm, sd);
    const int32_t grid_start = start_of_week_ymd(make_ymd(sy, sm, 1), prefs.calendar_week_start == 1U);
    const int row = std::clamp(static_cast<int>(hit->param), 0, 5);
    int32_t target_ymd = add_days_ymd(grid_start, row * 7);
    for (int d = 0; d < 7; ++d) {
      const int32_t cand = add_days_ymd(target_ymd, d);
      int cy = 0, cm = 0, cd = 0;
      if (split_ymd(cand, cy, cm, cd) && cy == sy && cm == sm) {
        target_ymd = cand;
        break;
      }
    }
    rs.cal.selected_ymd = target_ymd;
    rs.cal.subpage = CalendarSubpage::WEEK;
    rs.cal.week_page = 0;
    ensure_calendar_window_for_ymd(rs.cal.selected_ymd, prefs);
    mark_dirty_full();
    rt_.schedule_disp();
  }

  return true;
}



void TouchGestureEngine::handle_down_alert_overlay_(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms) {
  auto &rs = runtime_state();
  auto &rt = eng.rt_;
  touch_gesture().active = false;
  last_screen_wake_ms() = now_ms;
  const HitRegion *n_hit = frame_hits().hit_test(tx, ty, kHitTap);
  const uint16_t nwid = (n_hit != nullptr) ? n_hit->widget_id : WidgetId::NONE;
  const bool is_cam_notif = !rs.alert.camera_url.empty() || !rs.alert.camera_entity.empty();

  // Live PTT banner tap during P4 live doorbell popup sends reply immediately without dismissing popup
  if (rs.voice.intercom_turn && nwid == WidgetId::CAM_PTT_BANNER_STOP) {
    rt.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
    intercom_session().resume_listen(now_ms);
    rt.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
    rt.apply_power_policy();
    mark_dirty_full();
    rt.schedule_disp();
    return;
  }

  if (is_cam_notif && rs.cameras.doorbell_live_popup_active) {
    if (nwid == WidgetId::NOTIF_ANSWER_BTN) {
      if (cam_call_active()) {
        intercom_session().stop_call_with_hw(now_ms);
      } else {
        cam_listen_active() = !cam_listen_active();
        rs.intercom.cam_action = cam_listen_active() ? "listen_start" : "listen_stop";
        cam_intercom_status() = cam_listen_active() ? "DOOR AUDIO ON" : "AUDIO MUTED";
        rs.intercom.cam_status_ms = now_ms;
        intercom_session().emit_current_cmd();
        if (!cam_listen_active()) {
          rt.cmd(UiHostCmd::STOP_DOORBELL_AUDIO);
        }
      }
      rt.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }
    if (nwid == WidgetId::NOTIF_TALK_BTN) {
      if (!cam_call_active()) {
        intercom_session().start_call(now_ms);
      }
      rt.cmd(UiHostCmd::START_DOORBELL_PTT);
      rt.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }
    if (nwid == WidgetId::NOTIF_REPLY_BTN) {
      const std::string focus_ent = rs.alert.camera_entity;
      rs.alert.active = false;
      rs.cameras.doorbell_live_popup_active = false;
      rs.nav.shortcut_origin = ShortcutOrigin::NONE;
      enter_cameras_app(CamerasTab::SINGLE, focus_ent, false, rs, rt, now_ms);
      rs.cameras.quick_reply_open = true;
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }
    if (nwid == WidgetId::NOTIF_UNLOCK_BTN) {
      eng.gate_or_dispatch_doorbell_unlock(now_ms);
      return;
    }
    if (nwid == WidgetId::NOTIF_EXPAND_BTN || nwid == WidgetId::NOTIF_VIEW_BTN ||
        nwid == WidgetId::NOTIF_OPEN_CAM_BTN) {
      const std::string focus_ent = rs.alert.camera_entity;
      rs.alert.active = false;
      rs.cameras.doorbell_live_popup_active = false;
      rs.nav.shortcut_origin = ShortcutOrigin::NONE;
      enter_cameras_app(CamerasTab::SINGLE, focus_ent, false, rs, rt, now_ms);
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }
    if (nwid == WidgetId::NOTIF_DISMISS_BTN) {
      rs.cameras.doorbell_live_popup_active = false;
      if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
        intercom_session().stop_call_with_hw(now_ms);
      }
      rt.cmd(UiHostCmd::STOP_CAMERA_STREAM);
      rt.cmd(UiHostCmd::DISMISS_NOTIFICATION);
    }
    return;
  }

  if (is_cam_notif && (nwid == WidgetId::NOTIF_ANSWER_BTN ||
                       nwid == WidgetId::NOTIF_VIEW_BTN ||
                       nwid == WidgetId::NOTIF_OPEN_CAM_BTN)) {
    const std::string focus_ent = rs.alert.camera_entity;
    const bool want_listen = (nwid == WidgetId::NOTIF_ANSWER_BTN || nwid == WidgetId::NOTIF_OPEN_CAM_BTN);
    rs.alert.active = false;
    rs.cameras.doorbell_live_popup_active = false;
    rs.nav.shortcut_origin = ShortcutOrigin::NONE;
    enter_cameras_app(CamerasTab::SINGLE, focus_ent, want_listen, rs, rt, now_ms);
    mark_dirty_full();
    rt.schedule_disp();
    return;
  }
  rs.cameras.doorbell_live_popup_active = false;
  rt.cmd(UiHostCmd::STOP_CAMERA_STREAM);
  rt.cmd(UiHostCmd::DISMISS_NOTIFICATION);
}

bool TouchGestureEngine::handle_down_cameras_games_(TouchGestureEngine &eng, int tx, int ty,
                                                  uint32_t now_ms, uint16_t wid) {
  auto &rs = runtime_state();
  auto &rt = eng.rt_;
  auto &gest = touch_gesture();

  if (rs.nav.gui_page == PageId::GAMES) {
    if (rs.nav.active_game > 0) {
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
          rs.nav.active_game = 0;
          rs.nav.game_btn_pressed = false;
          game_input().clear();
          eng.snake_drag_active_ = false;
          rt.cmd(UiHostCmd::STOP_ALARM_SOUND);
          if (TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms)) {
            return true;
          }
          mark_dirty_full();
          rt.schedule_disp();
        }
        return true;
      }
      if (rs.nav.active_game == 5) {
        if (wid == WidgetId::GAME_CTRL_LEFT) {
          game_input().push(GameInputCmd::LEFT);
          game_input().set_held(GameInputCmd::LEFT, now_ms);
          gest.tap_cancelled = true;
          rt.schedule_disp();
        } else if (wid == WidgetId::GAME_CTRL_RIGHT) {
          game_input().push(GameInputCmd::RIGHT);
          game_input().set_held(GameInputCmd::RIGHT, now_ms);
          gest.tap_cancelled = true;
          rt.schedule_disp();
        } else if (wid == WidgetId::GAME_CTRL_ROTATE) {
          game_input().push(GameInputCmd::ROTATE);
          gest.tap_cancelled = true;
          rt.schedule_disp();
        } else if (wid == WidgetId::GAME_CTRL_DROP) {
          game_input().push(GameInputCmd::HARD_DROP);
          gest.tap_cancelled = true;
          rt.schedule_disp();
        } else if (wid == WidgetId::GAME_CTRL_HOLD) {
          game_input().push(GameInputCmd::HOLD);
          gest.tap_cancelled = true;
          rt.schedule_disp();
        } else if (wid == WidgetId::GAME_OVER_CTA) {
          game_input().push(GameInputCmd::PLAY_AGAIN);
          gest.tap_cancelled = true;
          rt.schedule_disp();
        }
        return true;
      }
      if (rs.nav.active_game == 6) {
        if (wid == WidgetId::GAME_OVER_CTA) {
          game_input().push(GameInputCmd::PLAY_AGAIN);
          gest.tap_cancelled = true;
          eng.snake_drag_active_ = false;
          rt.schedule_disp();
        } else {
          eng.snake_drag_x_ = tx;
          eng.snake_drag_y_ = ty;
          eng.snake_drag_active_ = true;
        }
        return true;
      }
      if (rs.nav.active_game == 7) {
        if (wid == WidgetId::GAME_MODE_AI) {
          game_input().push(GameInputCmd::MODE_AI);
          gest.tap_cancelled = true;
          rt.schedule_disp();
        } else if (wid == WidgetId::GAME_MODE_2P) {
          game_input().push(GameInputCmd::MODE_2P);
          gest.tap_cancelled = true;
          rt.schedule_disp();
        } else if (wid >= WidgetId::GAME_COL_BASE && wid < WidgetId::GAME_COL_BASE + 7) {
          game_input().push(GameInputCmd::COL_TAP, static_cast<int8_t>(wid - WidgetId::GAME_COL_BASE));
          gest.tap_cancelled = true;
          rt.schedule_disp();
        } else if (wid == WidgetId::GAME_OVER_CTA) {
          game_input().push(GameInputCmd::PLAY_AGAIN);
          gest.tap_cancelled = true;
          rt.schedule_disp();
        }
        return true;
      }
      if (rs.nav.active_game >= 1 && rs.nav.active_game <= 4) {
        rs.nav.marble_touch_x = static_cast<float>(tx);
        rs.nav.marble_touch_y = static_cast<float>(ty);
        rs.nav.game_btn_pressed = true;
        rt.schedule_disp();
      }
    }
    return true;
  }

  if (rs.nav.gui_page == PageId::CAMERAS) {
    if (rs.voice.intercom_turn && wid == WidgetId::CAM_PTT_BANNER_STOP &&
        touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      gest.active = false;
      rt.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
      intercom_session().resume_listen(now_ms);
      rt.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      rt.apply_power_policy();
      mark_dirty_full();
      rt.schedule_disp();
      return true;
    }
    if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      gest.active = false;
      if (rs.cameras.quick_reply_open) {
        rs.cameras.quick_reply_open = false;
        rs.cameras.hud_active = true;
        rs.cameras.hud_timer = now_ms;
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      (void)TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms);
    }
    return true;
  }

  return false;
}

bool TouchGestureEngine::handle_tap_cameras_games_(
    int tx, int ty, uint32_t now_rel, const HitRegion *hit, uint16_t wid) {
  (void)tx;
  (void)ty;
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  if (rs.nav.gui_page == PageId::GAMES) {
    if (rs.nav.active_game == 5) {
      if (wid == WidgetId::GAME_BOARD_TAP) {
        game_input().push(GameInputCmd::ROTATE);
        rt_.schedule_disp();
      } else if (wid == WidgetId::GAME_OVER_CTA) {
        game_input().push(GameInputCmd::PLAY_AGAIN);
        rt_.schedule_disp();
      }
    } else if (rs.nav.active_game == 6) {
      if (wid == WidgetId::GAME_OVER_CTA) {
        game_input().push(GameInputCmd::PLAY_AGAIN);
        rt_.schedule_disp();
      }
    } else if (rs.nav.active_game == 0 && !rs.nav.menu_scroll_active) {
      if (wid == WidgetId::GAMES_SOUND_BTN &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rt_.sw_toggle(UiSwitchId::GAME_SOUND);
        rt_.schedule_disp();
      } else if ((wid == WidgetId::GAMES_PLAY_BTN || wid == WidgetId::GAMES_HERO_CARD) &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        int enabled_ids[kMaxGamesLauncherItems]{};
        const int game_cnt = collect_enabled_games(enabled_ids, device_prefs());
        if (game_cnt > 0) {
          const int slot = std::clamp(rs.nav.game_carousel_idx, 0, game_cnt - 1);
          rs.nav.game_carousel_idx = slot;
          const int resolved_game =
              (hit != nullptr && hit->param >= 1 && hit->param <= 7 &&
               game_supported_for_imu(static_cast<int>(hit->param), LATTER_HAS_IMU != 0))
                  ? static_cast<int>(hit->param)
                  : enabled_ids[slot];
          rs.nav.active_game = resolved_game;
          game_input().clear();
          audio_arbitrator().cancel_media_idle_debounce();
          if (rt_.local_mp_playing) {
            rt_.cmd(UiHostCmd::PAUSE_LOCAL_MEDIA_PLAYER);
            audio_arbitrator().cancel_media_idle_debounce();
          }
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if ((wid == WidgetId::GAMES_PREV_BTN || wid == WidgetId::GAMES_NEXT_BTN) &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        const int game_cnt = active_games_launcher_count(device_prefs());
        if (game_cnt > 1) {
          const int cur_slot = std::clamp(rs.nav.game_carousel_idx, 0, game_cnt - 1);
          rs.nav.game_carousel_idx = cur_slot;
          const int dir = (wid == WidgetId::GAMES_NEXT_BTN) ? 1 : -1;
          rs.nav.menu_scroll_active = true;
          rs.nav.menu_scroll_dir = dir;
          rs.nav.menu_scroll_target_page = step_wrap(cur_slot, dir, game_cnt);
          rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
        }
      }
    }
    return true;
  }

  if (rs.nav.gui_page != PageId::CAMERAS) return false;

  const auto &ac = area_ctx_state();
  CameraRecord cams[MAX_CAMERA_SLOTS]{};
  const int c_total = parse_cameras_records(ac.cameras_summary, cams, MAX_CAMERA_SLOTS);
  const bool snap_only = (prefs.camera_stream_mode == 2U) || ac.camera_stream_url.empty();

  auto trigger_cam_refresh = [&]() {
    if (rs.cameras.tab == CamerasTab::SETTINGS) {
      rt_.cmd(UiHostCmd::STOP_CAMERA_STREAM);
    } else {
      rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      if (snap_only) {
        rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
        rs.alert.loaded_camera_url.clear();
        cam_error_backoff_until_ms() = 0;
        last_cam_live_fetch_ms() = now_rel;
        rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
      }
    }
  };

  if (rs.cameras.quick_reply_open) {
    if (wid >= WidgetId::CAM_REPLY_BASE && wid < WidgetId::CAM_REPLY_BASE + 3 &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      static constexpr const char *kDoorbellReplies[3] = {
          "On my way!",
          "Please leave it at the door.",
          "One moment, please!",
      };
      const int r_idx = static_cast<int>(wid - WidgetId::CAM_REPLY_BASE);
      rs.cameras.quick_reply_open = false;
      rs.cameras.hud_active = true;
      rs.cameras.hud_timer = now_rel;
      rs.intercom.cam_action = "quick_reply";
      rs.intercom.cam_message = kDoorbellReplies[r_idx];
      cam_intercom_status() = "REPLY SENT";
      rs.intercom.cam_status_ms = now_rel;
      intercom_session().emit_current_cmd();
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CAM_UNLOCK_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.quick_reply_open = false;
      rs.cameras.hud_active = true;
      rs.cameras.hud_timer = now_rel;
      mark_dirty_full();
      gate_or_dispatch_doorbell_unlock(now_rel);
    }
  } else if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4 &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    int target_tab = std::clamp(static_cast<int>(hit->param), CamerasTab::GRID, CamerasTab::SETTINGS);
    if (target_tab == CamerasTab::GRID && c_total <= 1) {
      target_tab = CamerasTab::SINGLE;
    }
    if (target_tab != rs.cameras.tab) {
      rs.cameras.tab = target_tab;
      rs.cameras.hud_active = true;
      rs.cameras.hud_timer = now_rel;
      rs.cameras.carousel_last_advance_ms = now_rel;
      trigger_cam_refresh();
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (rs.cameras.tab == CamerasTab::GRID && c_total > 1) {
    if (wid >= WidgetId::CAM_GRID_TILE_BASE && wid < WidgetId::CAM_GRID_TILE_BASE + 4 &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.selected_idx = std::clamp(static_cast<int>(hit->param), 0, c_total - 1);
      rs.cameras.tab = CamerasTab::SINGLE;
      rs.cameras.hud_active = true;
      rs.cameras.hud_timer = now_rel;
      trigger_cam_refresh();
      mark_dirty_full();
      rt_.schedule_disp();
    } else if ((wid == WidgetId::CAM_GRID_PREV_BTN || wid == WidgetId::CAM_GRID_NEXT_BTN) &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      const int total_pg = std::max(1, (c_total + 3) / 4);
      if (total_pg > 1) {
        rs.cameras.grid_page = step_wrap(
            rs.cameras.grid_page, (wid == WidgetId::CAM_GRID_NEXT_BTN) ? 1 : -1, total_pg);
        trigger_cam_refresh();
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
  } else if (rs.cameras.tab == CamerasTab::SETTINGS) {
#if LATTER_CHIP_P4
    if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      rs.cameras.settings_page = (wid == WidgetId::CHEVRON_UP) ? 0 : 1;
      mark_dirty_full();
      rt_.schedule_disp();
      return true;
    }
#endif
    if (wid >= WidgetId::CAM_SETTINGS_ROW_BASE && wid < WidgetId::CAM_SETTINGS_ROW_BASE + 5 &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int r_idx = static_cast<int>(wid - WidgetId::CAM_SETTINGS_ROW_BASE);
      auto cycle_opt = [&]<size_t N>(UiSelectId sid, const char *const (&opts)[N], uint8_t cur_val) {
        const uint8_t next_v = static_cast<uint8_t>((cur_val + 1U) % static_cast<uint8_t>(N));
        rt_.set_select_opt(sid, opts[next_v], true);
      };
      if (r_idx == 0) cycle_opt(UiSelectId::CAMERA_DEFAULT_VIEW, kCameraDefaultViewOpts, prefs.camera_default_view);
      else if (r_idx == 1) cycle_opt(UiSelectId::CAMERA_CAROUSEL_INTERVAL, kCameraCarouselIntervalOpts, prefs.camera_carousel_interval);
      else if (r_idx == 2) cycle_opt(UiSelectId::CAMERA_FRAMING, kCameraFramingOpts, prefs.camera_framing);
      else if (r_idx == 3) cycle_opt(UiSelectId::CAMERA_STREAM_MODE, kCameraStreamModeOpts, prefs.camera_stream_mode);
#if LATTER_CHIP_P4
      else if (r_idx == 4) cycle_opt(UiSelectId::CAMERA_DOORBELL_POPUP, kCameraDoorbellPopupOpts, prefs.camera_doorbell_popup);
#endif
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (rs.cameras.tab == CamerasTab::CAROUSEL) {
    int car[MAX_CAMERA_SLOTS]{};
    const int car_cnt = collect_carousel_camera_indices(cams, c_total, car, MAX_CAMERA_SLOTS);
    if (wid == WidgetId::CAM_VIDEO_TAP &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.hud_active = !rs.cameras.hud_active;
      rs.cameras.hud_timer = now_rel;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CAM_CAROUSEL_PAUSE_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.carousel_paused = !rs.cameras.carousel_paused;
      rs.cameras.carousel_last_advance_ms = now_rel;
      rs.cameras.hud_timer = now_rel;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if ((wid == WidgetId::CAM_PREV_BTN || wid == WidgetId::CAM_NEXT_BTN) && car_cnt > 1 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      rs.cameras.carousel_idx = step_wrap(
          rs.cameras.carousel_idx, (wid == WidgetId::CAM_NEXT_BTN) ? 1 : -1, car_cnt);
      rs.cameras.carousel_last_advance_ms = now_rel;
      rs.cameras.hud_timer = now_rel;
      trigger_cam_refresh();
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else {
    if (wid == WidgetId::CAM_VIDEO_TAP &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.hud_active = !rs.cameras.hud_active;
      rs.cameras.hud_timer = now_rel;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if ((wid == WidgetId::CAM_PREV_BTN || wid == WidgetId::CAM_NEXT_BTN) && c_total > 1 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      rs.cameras.selected_idx = step_wrap(
          rs.cameras.selected_idx, (wid == WidgetId::CAM_NEXT_BTN) ? 1 : -1, c_total);
      rs.cameras.hud_timer = now_rel;
      trigger_cam_refresh();
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CAM_ORIENT_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.landscape_mode = !rs.cameras.landscape_mode;
      cam_intercom_status() = rs.cameras.landscape_mode ? "90\xC2\xB0 LANDSCAPE" : "PORTRAIT VIEW";
      rs.intercom.cam_status_ms = now_rel;
      rs.cameras.hud_timer = now_rel;
      trigger_cam_refresh();
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CAM_LISTEN_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.hud_timer = now_rel;
      if (cam_call_active()) {
        intercom_session().stop_call_with_hw(now_rel);
      } else {
        cam_listen_active() = !cam_listen_active();
        rs.intercom.cam_action = cam_listen_active() ? "listen_start" : "listen_stop";
        cam_intercom_status() = cam_listen_active() ? "DOOR AUDIO ON" : "AUDIO MUTED";
        rs.intercom.cam_status_ms = now_rel;
        intercom_session().emit_current_cmd();
        if (!cam_listen_active()) {
          rt_.cmd(UiHostCmd::STOP_DOORBELL_AUDIO);
        }
      }
      rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CAM_PTT_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.hud_timer = now_rel;
      if (!cam_call_active()) {
        intercom_session().start_call(now_rel);
        rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (cam_intercom_ptt_active()) {
        rt_.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
        intercom_session().resume_listen(now_rel);
        rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
        rt_.apply_power_policy();
        mark_dirty_full();
        rt_.schedule_disp();
      } else {
        rt_.cmd(UiHostCmd::START_DOORBELL_PTT);
        rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      }
    } else if (wid == WidgetId::CAM_REPLY_MENU_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.quick_reply_open = true;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CAM_UNLOCK_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.hud_timer = now_rel;
      gate_or_dispatch_doorbell_unlock(now_rel);
    }
  }

  return true;
}



int TouchGestureEngine::remote_key_for_widget_(uint16_t wid) noexcept {
  switch (wid) {
    case WidgetId::REMOTE_DPAD_UP:    return 0;
    case WidgetId::REMOTE_DPAD_DOWN:  return 1;
    case WidgetId::REMOTE_DPAD_LEFT:  return 2;
    case WidgetId::REMOTE_DPAD_RIGHT: return 3;
    case WidgetId::REMOTE_DPAD_OK:    return 4;
    case WidgetId::REMOTE_NAV_BACK:   return 5;
    case WidgetId::REMOTE_NAV_HOME:   return 6;
    case WidgetId::REMOTE_VOL_UP:     return 7;
    case WidgetId::REMOTE_VOL_DOWN:   return 8;
    default: return -1;
  }
}

void TouchGestureEngine::do_remote_send_key_(int key_idx, int target_slot, bool long_press, uint32_t now_override_ms) {
  auto &rs = runtime_state();
  const int res_slot = (target_slot == -1) ? resolve_active_remote_slot(0)
                                           : resolve_active_remote_slot(target_slot);
  const int slot_1based = res_slot + 1;

  static constexpr const char *kCanonicalCmds[10] = {
      "up", "down", "left", "right", "select",
      "back", "home", "volume_up", "volume_down", "toggle"};
  static constexpr const char *kFeedback[10] = {
      "Up", "Down", "Left", "Right", "Select",
      "Back", "Home", "Vol +", "Vol -", "Toggle Power"};
  static constexpr const char *kHoldFeedback[10] = {
      "Hold Up", "Hold Down", "Hold Left", "Hold Right", "Hold OK",
      "Hold Back", "Hold Home", "Vol +", "Vol -", "Toggle Power"};
  const int k = (key_idx >= 0 && key_idx < 10) ? key_idx : 0;
  const char *act = long_press
                        ? "long_press"
                        : ((k == 7) ? "volume_up"
                                    : ((k == 8) ? "volume_down"
                                                : ((k == 9) ? "toggle" : "command")));
  const char *cmd = kCanonicalCmds[k];
  const std::string *t_names = remote_slots_name();
  if (k == 9 && !t_names[res_slot].empty()) {
    rs.remote.action_feedback = "Power: " + t_names[res_slot];
  } else {
    rs.remote.action_feedback = long_press ? kHoldFeedback[k] : kFeedback[k];
  }
  rs.remote.feedback_timer = TouchGestureEngine::now_or_hal_ms_(now_override_ms);
  if (!remote_state().slots_summary.empty()) {
    latter_actions::emit_remote_action(slot_1based, act, cmd);
  }
}

bool TouchGestureEngine::handle_down_system_tools_(TouchGestureEngine &eng, int tx, int ty,
                                                 uint32_t now_ms, const HitRegion *hit,
                                                 uint16_t wid, uint32_t tkey) {
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  auto &rt = eng.rt_;
  auto &gest = touch_gesture();
  const auto &ly = layout();

  auto back_to_sys_root = [&]() {
    rs.nav.system_menu_mode = 0;
    rs.nav.menu_scroll_active = false;
    if (TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms)) return;
    mark_dirty_full();
    rt.schedule_disp();
  };

  if (rs.nav.gui_page == PageId::SYSTEM) {
    const int mode = rs.nav.system_menu_mode;
    if (mode == 1 || mode == 2 || mode == 8) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        back_to_sys_root();
      }
      return true;
    }
    if (mode == 3) {
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) back_to_sys_root();
        return true;
      }
      if (wid == WidgetId::SYS_SECONDARY_BTN) {
        if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
          rs.nav.system_menu_mode = 4;
          mark_dirty_full();
          rt.schedule_disp();
        }
        return true;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5 &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        const int slot = hit->param;
        if (slot == 0) {
          rt.cmd(UiHostCmd::TRIGGER_SCREENSAVER_NOW);
        } else if (slot == 1) {
#if LATTER_HAS_BATTERY
          rt.cmd(UiHostCmd::START_HIBERNATION_COUNTDOWN);
#else
          rt.cmd(UiHostCmd::TURN_SCREEN_OFF);
#endif
        } else if (slot == 2) {
          rs.nav.system_menu_mode = 5;
          mark_dirty_full();
          rt.schedule_disp();
        } else if (slot == 3) {
          rt.sw_toggle(UiSwitchId::BLE_AREA_BEACON);
          rt.schedule_disp();
        }
      }
      return true;
    }
    if (mode == 4 || mode == 6) {
      return true;
    }
    if (mode == 5) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.update_check_state = 0;
        back_to_sys_root();
        return true;
      }
      if (wid == WidgetId::SYS_PRIMARY_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        const bool has_update = is_newer_semver(
            rt.fw_latest_version, (rt.fw_version != nullptr) ? rt.fw_version : "0.0.0");
        if (has_update) {
          rs.nav.system_menu_mode = 6;
          mark_dirty_full();
          rt.schedule_disp();
        } else if (rs.nav.update_check_state == 0) {
          rs.nav.update_check_state = 1;
          rs.nav.update_check_timer = now_ms;
          rt.cmd(UiHostCmd::CHECK_FIRMWARE_UPDATE);
          rt.schedule_disp();
        }
      }
      return true;
    }
    if (mode == 7) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        back_to_sys_root();
        return true;
      }
      if (wid == WidgetId::SYS_VOL_SLIDER) return true;
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
        const int slot = hit->param;
        if (slot == 1 && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID)) {
          const float cur_a_f = rt.num_val(UiNumberId::ALERT_VOLUME);
          const int cur_a = (std::isnan(cur_a_f) || cur_a_f < 20.0f) ? 70 : static_cast<int>(std::round(cur_a_f));
          const int nxt_a = static_cast<int>(step_wrap_range(static_cast<float>(cur_a), 10.0f, 50.0f, 100.0f));
          rt.set_num_val(UiNumberId::ALERT_VOLUME, static_cast<float>(nxt_a));
          rt.schedule_disp();
        } else if (slot == 2 && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
          const int cur_sz = static_cast<int>(std::round(rt.num_val(UiNumberId::VOLUME_STEP_SIZE)));
          const int nxt_sz = (cur_sz < 5) ? 5 : ((cur_sz < 10) ? 10 : 2);
          rt.set_num_val(UiNumberId::VOLUME_STEP_SIZE, static_cast<float>(nxt_sz));
          rt.schedule_disp();
        } else if (slot == 3 && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
          rt.sw_toggle(UiSwitchId::GAME_SOUND);
          rt.schedule_disp();
        }
      }
      return true;
    }
    if (mode == 9) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        if (rs.nav.audio_menu_parent_page == PageId::MUSIC) {
          rs.nav.gui_page = PageId::MUSIC;
          reset_music_to_now_playing(rs, now_ms, true);
          rs.nav.audio_menu_parent_page = PageId::SYSTEM;
          rs.nav.system_menu_mode = 0;
          const auto &ms = media_state();
          if (rt.select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE) != "None" &&
              (!ms.is_local || rt.album_art_w == 0) &&
              !ms.art_url.empty() && ms.art_url != rs.music.loaded_art_url) {
            rt.cmd(UiHostCmd::FETCH_REMOTE_ART_DEBOUNCED);
          }
          mark_dirty_full();
          rt.schedule_disp();
        } else {
          back_to_sys_root();
        }
        return true;
      }
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID)) {
        rs.music.settings_page = (wid == WidgetId::CHEVRON_UP) ? 1 : 2;
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5 &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        const int pg = std::clamp(rs.music.settings_page, 1, 2);
        cycle_music_pref_slot(rt, (pg - 1) * 5 + hit->param);
        rt.schedule_disp();
      }
      return true;
    }
    if (mode == 11) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        back_to_sys_root();
        return true;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5 &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        const int slot = hit->param;
        if (slot == 0) {
          rt.set_select_opt(UiSelectId::VA_WAKE_WORD_MODE,
                            cycle_option(rt.select_opt(UiSelectId::VA_WAKE_WORD_MODE),
                                         {"Docked Only", "Always On", "Disabled"}));
          rt.schedule_disp();
        } else if (slot == 1) {
          if (rt.select_opt(UiSelectId::VA_WAKE_WORD_MODE) != "Disabled") {
            rt.cmd(UiHostCmd::CYCLE_PRIMARY_WAKE_WORD);
            rt.schedule_disp();
          }
        } else if (slot == 2) {
          if (rt.select_opt(UiSelectId::VA_WAKE_WORD_MODE) != "Disabled") {
            rs.nav.system_menu_mode = 12;
            mark_dirty_full();
            rt.schedule_disp();
          }
        } else if (slot == 3) {
          rt.set_select_opt(UiSelectId::VA_VISUAL_PROFILE,
                            cycle_option(rt.select_opt(UiSelectId::VA_VISUAL_PROFILE),
                                         {"Nebula", "Solaris", "Rings", "Light Bar", "Dot Wave"}));
          rt.schedule_disp();
        } else if (slot == 4) {
          rt.sw_toggle(UiSwitchId::VA_WAKE_SCREEN);
          rt.schedule_disp();
        }
      }
      return true;
    }
    if (mode == 12) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = 11;
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5 &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        const int slot = std::clamp(static_cast<int>(hit->param), 0, 4);
        static constexpr UiSelectId kSensIds[5] = {
            UiSelectId::WW_SENS_OKAY_NABU, UiSelectId::WW_SENS_HEY_LUNA,
            UiSelectId::WW_SENS_HEY_JARVIS, UiSelectId::WW_SENS_HEY_MYCROFT,
            UiSelectId::WW_SENS_ALEXA};
        rt.set_select_opt(kSensIds[slot],
                          cycle_option(rt.select_opt(kSensIds[slot]),
                                       {"Slightly sensitive", "Moderately sensitive", "Very sensitive"}));
        rt.schedule_disp();
      }
      return true;
    }
    if (mode == 13) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = 2;
        rt.cmd(UiHostCmd::APPLY_THEME_PRESET);
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      if (wid == WidgetId::COLOR_WHEEL_SAVE && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
        save_persisted_prefs();
        rt.set_select_opt(UiSelectId::THEME_PALETTE, "Custom");
        do_save_custom_color_to_latter();
        rs.nav.system_menu_mode = 2;
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      if (wid == WidgetId::COLOR_WHEEL_RING) {
        float hue = 0.0f, sat = 1.0f;
        if (ui::hsv_wheel_pick(tx, ty, SCREEN_CX, scale_y(180), 156, 56, hue, sat)) {
          color_wheel_hue_deg() = hue;
          color_wheel_sat() = sat;
          const UiColor c = hsv_to_rgb(hue / 360.0f, sat, 1.0f);
          prefs.custom_theme_rgb = (static_cast<uint32_t>(c.r) << 16) |
                                   (static_cast<uint32_t>(c.g) << 8) |
                                   static_cast<uint32_t>(c.b);
          if (rt.theme_primary != nullptr && rt.theme_secondary != nullptr) {
            apply_custom_rgb_harmony_slots(prefs.custom_theme_rgb, rt.theme_primary, rt.theme_secondary);
          }
          rt.schedule_disp();
        }
      }
      return true;
    }
    return true;
  }

  if (rs.nav.gui_page == PageId::REMOTE) {
    if (rs.nav.remote_subpage == 1) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.remote.action_feedback.clear();
        rs.nav.remote_subpage = 0;
        if (TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms)) return true;
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
        gest.active = false;
        int valid_slots[MAX_REMOTE_SLOTS] = {0};
        const int valid_count = collect_configured_remote_slots(valid_slots);
        const int total_pages = std::max(1, (valid_count + 3) / 4);
        const int next_pg = std::clamp(
            remote_control_page() + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 0, total_pages - 1);
        if (next_pg != remote_control_page()) {
          remote_control_page() = next_pg;
          rt.schedule_disp();
        }
      }
      return true;
    }

    const int act_slot = resolve_active_remote_slot(rs.remote.active_slot);
    rs.remote.active_slot = act_slot;

    if (wid == WidgetId::REMOTE_SLOT_PILL) {
      if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        gest.active = false;
        rs.remote.action_feedback.clear();
        rs.nav.remote_subpage = 1;
        mark_dirty_full();
        rt.schedule_disp();
      }
      return true;
    }

    if (hit != nullptr && (hit->flags & kHitLongPress) != 0U) {
      gest.arm_long_press(wid, hit->param);
      schedule_core_timeout(kTimeoutTouchLongPressId, kinematics::kLongPressHoldMs, []() {
        touch_engine().tick(TouchGestureEngine::now_or_hal_ms_(last_user_touch_ms()));
      });
      rt.schedule_band_disp(std::max(0, static_cast<int>(hit->y0) - 6),
                            std::min(SCREEN_H - 1, static_cast<int>(hit->y1) + 6));
      return true;
    }

    uint16_t eff_wid = wid;
    uint32_t eff_tkey = tkey;
    if (eff_wid == WidgetId::NONE) {
      const RemoteDpadGeom dp = compute_remote_dpad_geom(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0);
      eff_wid = resolve_remote_dpad_widget(tx, ty, dp);
      if (eff_wid != WidgetId::NONE) {
        eff_tkey = make_tap_target_key(rs.nav.gui_page, eff_wid, 0);
      }
    }

    const int remote_key = TouchGestureEngine::remote_key_for_widget_(eff_wid);
    if (remote_key >= 0 && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, eff_tkey)) {
      gest.active = false;
      TouchGestureEngine::do_remote_send_key_(remote_key, act_slot, false, now_ms);
      rt.schedule_band_disp(0, std::min(SCREEN_H - 1, ly.hdr_y + scale_y(24)));
    }
    return true;
  }

  if (rs.nav.gui_page != PageId::TOOLS) return false;

  const int sub = rs.nav.tools_subpage;
  if (sub == ToolsSubpage::LAUNCHER) return true;

  auto exit_tool_subpage = [&](bool sync_policy) {
    rs.nav.tools_subpage = 0;
    if (sync_policy) rt.apply_power_policy();
    if (TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms)) return;
    mark_dirty_full();
    rt.schedule_disp();
  };

#if LATTER_ROUND_DISPLAY
  if (sub == ToolsSubpage::STOPWATCH) {
    if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      exit_tool_subpage(false);
      return true;
    }
    if (wid == WidgetId::SW_PRIMARY_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      if (!rs.stopwatch.running) {
        rs.stopwatch.running = true;
        rs.stopwatch.started_in_analog = true;
        rs.stopwatch.start_time_ms = now_ms - rs.stopwatch.elapsed_ms;
        rs.nav.sleep_start_time = 0;
      } else {
        rs.stopwatch.running = false;
        rs.stopwatch.elapsed_ms = now_ms - rs.stopwatch.start_time_ms;
      }
      latter_actions::check_and_emit_timer_alarm_state(now_ms);
      rt.schedule_disp();
      return true;
    }
    if (wid == WidgetId::SW_LAP_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      if (rs.stopwatch.running) {
        record_stopwatch_lap(rs.stopwatch, now_ms - rs.stopwatch.start_time_ms);
      } else if (rs.stopwatch.elapsed_ms > 0) {
        rs.stopwatch.lap_ms = rs.stopwatch.elapsed_ms;
      }
      rt.schedule_disp();
      return true;
    }
    if (wid == WidgetId::SW_RESET_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      rs.stopwatch.reset();
      latter_actions::check_and_emit_timer_alarm_state(now_ms);
      rt.schedule_disp();
    }
    return true;
  }
#endif

  if (sub == ToolsSubpage::DICE) {
    if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      exit_tool_subpage(true);
    } else if (wid == WidgetId::DICE_ROLL_BTN && !dice_state().anim_active &&
               touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      trigger_dice_roll(now_ms, next_rng_u32(), next_rng_u32());
      rt.schedule_disp();
    }
    return true;
  }

  if (sub == ToolsSubpage::FLASHLIGHT) {
    if (ty <= ly.top_edge_zone_y()) return true;
    if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      rs.nav.tools_subpage = 0;
      rt.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
      if (!TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms)) {
        mark_dirty_full();
        rt.schedule_disp();
      }
    }
    return true;
  }

  if (sub == ToolsSubpage::CALCULATOR) {
    if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      reset_calculator_state();
      exit_tool_subpage(false);
    } else if (wid >= WidgetId::CALC_KEY_BASE && wid < WidgetId::CALC_KEY_BASE + 20 &&
               touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID)) {
      handle_calculator_keypad_tap(tx, ty);
      rt.schedule_disp();
    }
    return true;
  }

  if (sub == ToolsSubpage::LEVEL) {
    if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      exit_tool_subpage(true);
    }
    return true;
  }

  if (sub == ToolsSubpage::COIN_FLIP) {
    if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      exit_tool_subpage(true);
    } else if (wid == WidgetId::COIN_FLIP_BTN && !coin_state().anim_active &&
               touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      trigger_coin_flip(now_ms, next_rng_u32());
      rt.schedule_disp();
    }
  }
  return true;
}

bool TouchGestureEngine::handle_tap_system_tools_(
    int /*tx*/, int /*ty*/, uint32_t now_rel, const HitRegion *hit, uint16_t wid) {
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  if (rs.nav.gui_page == PageId::SYSTEM) {
    const int mode = rs.nav.system_menu_mode;
    if (!rs.nav.menu_scroll_active && (mode == 0 || mode == 1 || mode == 2)) {
      const int cur_pg = (mode == 0) ? rs.nav.system_dashboard_page
                                     : ((mode == 1) ? rs.nav.sleep_menu_page : rs.nav.display_menu_page);
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        const int target_pg = (cur_pg == 1) ? 2 : 1;
        rs.nav.menu_scroll_active = true;
        rs.nav.menu_scroll_dir = (target_pg == 2) ? 1 : -1;
        rs.nav.menu_scroll_target_page = target_pg;
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      } else if (mode == 0 && wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int item_idx = (rs.nav.system_dashboard_page == 1) ? hit->param : (hit->param + 3);
        if (item_idx == 0) { rs.nav.system_menu_mode = 2; rs.nav.display_menu_page = 1; }
        else if (item_idx == 1) { rs.nav.system_menu_mode = 1; rs.nav.sleep_menu_page = 1; }
        else if (item_idx == 2) { rs.nav.system_menu_mode = 7; }
        else if (item_idx == 3) { rs.nav.system_menu_mode = 11; }
        else if (item_idx == 4) { rs.nav.system_menu_mode = 9; rs.music.settings_page = 1; rs.nav.audio_menu_parent_page = PageId::SYSTEM; }
        else if (item_idx == 5 && prefs.home_screen_enabled) {
          rs.nav.gui_page = PageId::HOME;
          rs.nav.home_subpage = 1;
          rs.nav.home_settings_page = 1;
        }
        else if (item_idx == 6) { rs.nav.system_menu_mode = 3; }
        else if (item_idx == 7) { rs.nav.system_menu_mode = 8; }
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (mode == 1 && wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int idx = (rs.nav.sleep_menu_page == 1) ? hit->param : (hit->param + 5);
        if (idx == 0) {
#if LATTER_HAS_IMU
          rt_.set_select_opt(UiSelectId::PICKUP_WAKE_MODE,
                             cycle_option(rt_.select_opt(UiSelectId::PICKUP_WAKE_MODE),
                                          {"Always On", "Docked Only", "Disabled"}));
#endif
        } else if (idx == 1) {
          rt_.set_select_opt(UiSelectId::TAP_WAKE_MODE,
                             cycle_option(rt_.select_opt(UiSelectId::TAP_WAKE_MODE),
                                          {"Single Tap", "Double Tap", "Docked Only", "Disabled"}));
        } else if (idx == 2) {
#if LATTER_HAS_BATTERY
          rt_.set_select_opt(UiSelectId::DISPLAY_OFF_TIMEOUT,
                             cycle_option(rt_.select_opt(UiSelectId::DISPLAY_OFF_TIMEOUT),
                                          {"10s", "15s", "30s", "1 min", "2 min", "Never"}));
#endif
        } else if (idx == 3) {
          rt_.set_select_opt(UiSelectId::SCREENSAVER_MODE,
                             cycle_option(rt_.select_opt(UiSelectId::SCREENSAVER_MODE),
                                          {"Docked Only", "Always", "Never"}));
        } else if (idx == 4 && rt_.select_opt(UiSelectId::SCREENSAVER_MODE) != "Never") {
          rt_.set_select_opt(UiSelectId::SCREENSAVER_STYLE,
                             cycle_option(rt_.select_opt(UiSelectId::SCREENSAVER_STYLE),
                                          {"Ambient Clock", "Photo Frame", "Aurora",
                                           "Starfield", "Digital Rain", "Fireworks"}));
        } else if (idx == 5) {
          rt_.set_select_opt(UiSelectId::SCREENSAVER_DOCKED_TIMEOUT,
                             cycle_option(rt_.select_opt(UiSelectId::SCREENSAVER_DOCKED_TIMEOUT),
                                          {"10s", "15s", "30s", "1 min", "2 min", "5 min", "Never"}));
        } else if (idx == 6) {
#if LATTER_HAS_BATTERY
          rt_.set_select_opt(UiSelectId::HIBERNATE_TIMEOUT_MODE,
                             cycle_option(rt_.select_opt(UiSelectId::HIBERNATE_TIMEOUT_MODE),
                                          {"5 min", "10 min", "15 min", "30 min", "Disabled"}));
#endif
        } else if (idx == 7) {
          rs.nav.ambient_bri_override_pct = -1;
          rt_.sw_toggle(UiSwitchId::BEDTIME_MODE);
        } else if (idx == 8) {
          rt_.set_select_opt(UiSelectId::BEDTIME_DISPLAY,
                             cycle_option(rt_.select_opt(UiSelectId::BEDTIME_DISPLAY),
                                          {"Screen Off", "Ambient Clock"}));
        } else if (idx == 9) {
          rt_.sw_toggle(UiSwitchId::DO_NOT_DISTURB);
        }
        rt_.schedule_disp();
      } else if (mode == 2 && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        if (wid >= WidgetId::COLOR_SWATCH_BASE && wid < WidgetId::COLOR_SWATCH_BASE + 10) {
          rs.nav.system_menu_mode = 13;
          mark_dirty_full();
          rt_.schedule_disp();
        } else if (wid == WidgetId::SYS_BRI_SLIDER) {
          UiNumberId nid = UiNumberId::MANUAL_BRIGHTNESS;
          if (TouchGestureEngine::resolve_sys_bri_slider_id_(
                  hit->param, rt_.select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE), nid)) {
            const int cur_pct = static_cast<int>(std::lround(rt_.num_val(nid)));
            rt_.set_num_val(nid, static_cast<float>(step_brightness_tap_10pct(cur_pct)));
            rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
            rt_.schedule_disp();
          }
        } else if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
          const int item_idx = (rs.nav.display_menu_page == 1) ? hit->param : (hit->param + 5);
          const std::string_view ab = rt_.select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE);
          if (item_idx == 0) {
            if (prefs.home_screen_enabled) {
              rt_.set_select_opt(UiSelectId::DEFAULT_START_APP,
                                 cycle_option(rt_.select_opt(UiSelectId::DEFAULT_START_APP),
                                              {"Home", "Pinned", "Time App", "Calendar", "Clock Face",
                                               "Media", "Home Hub", "Area Controls", "Remote",
                                               "Weather", "Cameras", "Games", "Tools"}));
            } else {
              rt_.set_select_opt(UiSelectId::DEFAULT_START_APP,
                                 cycle_option(rt_.select_opt(UiSelectId::DEFAULT_START_APP),
                                              {"Pinned", "Time App", "Calendar", "Clock Face",
                                               "Media", "Home Hub", "Area Controls", "Remote",
                                               "Weather", "Cameras", "Games", "Tools"}));
            }
          } else if (item_idx == 1) {
#if LATTER_ROUND_DISPLAY
            const std::string next_cf = cycle_option(
                rt_.select_opt(UiSelectId::CLOCK_FACE_STYLE),
                {"Minimal Digital", "Modern Analog", "Hybrid Digital"});
#else
            const std::string next_cf = cycle_option(
                rt_.select_opt(UiSelectId::CLOCK_FACE_STYLE),
                {"Minimal Digital", "Hybrid Digital"});
#endif
            rt_.set_select_opt(UiSelectId::CLOCK_FACE_STYLE, next_cf);
          } else if (item_idx == 2) {
            rt_.set_select_opt(UiSelectId::TIME_FORMAT,
                               prefs.is_24h_format ? "12-Hour (AM/PM)" : "24-Hour");
          } else if (item_idx == 3) {
            const std::string next_pal = cycle_option(
                rt_.select_opt(UiSelectId::THEME_PALETTE),
                {"Steel Blue", "Pink", "Blue", "Green", "Custom"});
            rt_.set_select_opt(UiSelectId::THEME_PALETTE, next_pal);
            if (next_pal == "Custom") {
              rs.nav.system_menu_mode = 13;
              mark_dirty_full();
            }
          } else if (item_idx == 4) {
            const std::string next_m =
                cycle_option(ab, {"Disabled", "Sun Synchronized", "Time Schedule", "Ambient Light"});
            rs.nav.ambient_bri_override_pct = -1;
            rt_.set_select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE, next_m, true);
            rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
          } else if (item_idx == 6 && ab == "Time Schedule") {
            const int dh = static_cast<int>(rt_.num_val(UiNumberId::DAY_START_HOUR));
            const int nh = static_cast<int>(rt_.num_val(UiNumberId::NIGHT_START_HOUR));
            int nxt_dh = 7, nxt_nh = 22;
            if (dh == 7 && nh == 22) { nxt_dh = 6; nxt_nh = 23; }
            else if (dh == 6 && nh == 23) { nxt_dh = 8; nxt_nh = 22; }
            else if (dh == 8 && nh == 22) { nxt_dh = 7; nxt_nh = 21; }
            rt_.set_num_val(UiNumberId::DAY_START_HOUR, static_cast<float>(nxt_dh));
            rt_.set_num_val(UiNumberId::NIGHT_START_HOUR, static_cast<float>(nxt_nh));
            rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
          } else if (item_idx == 9) {
            rt_.sw_toggle(UiSwitchId::SCREENSAVER_PHOTO_CLOCK);
          }
          rt_.schedule_disp();
        }
      }
    } else if (!rs.nav.menu_scroll_active && (mode == 4 || mode == 6)) {
      if (wid == WidgetId::CONFIRM_CANCEL && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.nav.system_menu_mode = (mode == 4) ? 3 : 5;
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (wid == WidgetId::CONFIRM_ACCEPT && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rt_.cmd((mode == 4) ? UiHostCmd::RESTART_DEVICE : UiHostCmd::TRIGGER_FIRMWARE_UPDATE);
      }
    } else if (mode == 7 && wid == WidgetId::SYS_VOL_SLIDER &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      const float new_vol = step_wrap_range(rt_.local_mp_volume, 0.10f, 0.10f, 1.00f);
      rt_.set_num_val(UiNumberId::SPEAKER_VOLUME, std::round(new_vol * 100.0f));
      rt_.schedule_disp();
    }
    return true;
  }

  if (rs.nav.gui_page == PageId::REMOTE) {
    if (rs.nav.remote_subpage == 0) {
      const int remote_key = TouchGestureEngine::remote_key_for_widget_(wid);
      if (remote_key >= 0 && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        const int act_slot = resolve_active_remote_slot(rs.remote.active_slot);
        rs.remote.active_slot = act_slot;
        TouchGestureEngine::do_remote_send_key_(remote_key, act_slot, false, now_rel);
        rt_.schedule_band_disp(0, std::min(SCREEN_H - 1, layout().hdr_y + scale_y(24)));
      }
    } else if (rs.nav.remote_subpage == 1) {
      if (wid == WidgetId::REMOTE_PRIMARY_PWR &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        TouchGestureEngine::do_remote_send_key_(9, -1, false, now_rel);
        rt_.schedule_disp();
      } else if (wid >= WidgetId::REMOTE_SEC_PWR && wid < WidgetId::REMOTE_SEC_PWR + 4 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        TouchGestureEngine::do_remote_send_key_(9, hit->param, false, now_rel);
        rt_.schedule_disp();
      } else if (wid >= WidgetId::REMOTE_SLOT_CARD && wid < WidgetId::REMOTE_SLOT_CARD + 4 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.remote.active_slot = hit->param;
        rs.remote.action_feedback.clear();
        rs.nav.remote_subpage = 0;
        do_remote_select_slot(hit->param + 1);
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
    return true;
  }

  if (rs.nav.gui_page != PageId::TOOLS) return false;

  if (rs.nav.tools_subpage == ToolsSubpage::LAUNCHER && !rs.nav.menu_scroll_active) {
    const int total_pages = active_tools_launcher_page_count(device_prefs());
    if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      if (total_pages > 1) {
        const int cur_pg = std::clamp(rs.nav.tools_menu_page, 0, total_pages - 1);
        rs.nav.tools_menu_page = cur_pg;
        const int target_pg = std::clamp(cur_pg + ((wid == WidgetId::CHEVRON_DOWN) ? 1 : -1), 0, total_pages - 1);
        if (target_pg != cur_pg) {
          rs.nav.menu_scroll_active = true;
          rs.nav.menu_scroll_dir = (target_pg > cur_pg) ? 1 : -1;
          rs.nav.menu_scroll_target_page = target_pg;
          rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
        }
      }
    } else if (wid >= WidgetId::TOOLS_CARD_BASE && wid < WidgetId::TOOLS_CARD_BASE + 6 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int target_sub = hit->param;
      if (target_sub > 0) {
        rs.nav.tools_subpage = target_sub;
        if (target_sub == ToolsSubpage::FLASHLIGHT) rt_.cmd(UiHostCmd::FLASHLIGHT_FULL_BRIGHTNESS);
        else if (target_sub == ToolsSubpage::CALCULATOR) reset_calculator_state();
        rt_.apply_power_policy();
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
  } else if (rs.nav.tools_subpage == ToolsSubpage::FLASHLIGHT &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    rs.nav.tools_subpage = 0;
    rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
    if (!TouchGestureEngine::return_from_shortcut_origin_(rs, rt_, now_rel)) {
      mark_dirty_full();
      rt_.schedule_disp();
    }
  }
  return true;
}

}  // namespace latter_ui
