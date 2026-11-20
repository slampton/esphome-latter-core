// ==============================================================================
// Latter Compiled On-Device Touch, Gesture & Rotary Input Engine
// (components/latter_ui/touch_engine.cpp)
// FrameHitTable-driven tap, horizontal slider, and rotary focus dispatch paired
// with UiLayout millimetre swipe thresholds (docs/device-ui-design.md).
// ==============================================================================

#include "ui_touch_engine.h"
#include "ui_pages.h"

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#if __has_include("esphome/core/hal.h")
#include "esphome/core/hal.h"
#endif
#if __has_include("esphome/core/log.h")
#include "esphome/core/log.h"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef USE_ESP32
#include <esp_random.h>
#endif

namespace latter_ui {

namespace {

inline HomeActivityItem *touch_activity_fallback_scratch() {
  static std::vector<HomeActivityItem> s_buf(static_cast<size_t>(kMaxHomeActivity));
  return s_buf.data();
}

inline CalendarSourceMeta *touch_cal_sources_scratch() {
  static std::vector<CalendarSourceMeta> s_buf(static_cast<size_t>(kMaxCalendarSources));
  return s_buf.data();
}

inline bool resolve_sys_bri_slider_id(int16_t param, std::string_view auto_bri_mode,
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

inline int step_brightness_tap_10pct(int cur_pct) noexcept {
  if (cur_pct < 10 || cur_pct >= 100) return 10;
  return std::min(100, ((cur_pct / 10) + 1) * 10);
}

inline void do_fetch_area_browse() {
  const auto &rs = runtime_state();
  const int page = rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES);
  latter_actions::emit_area_browse_entities(rs.area.active_area_id, rs.area.domain_filter, page, LATTER_CARD_SLOT_COUNT);
}

inline void do_fetch_area_list_browse() {
  const auto &rs = runtime_state();
  const int page = rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST);
  latter_actions::emit_area_browse_list(rs.area.active_area_id, page, LATTER_SLOT_COUNT);
}

inline void do_select_active_area_latter() {
  latter_actions::emit_set_active_area(runtime_state().area.active_area_id);
}

inline void do_execute_area_action() {
  auto &ar = runtime_state().area;
  latter_actions::emit_area_action(
      ar.target_entity_id, ar.target_action, ar.target_level, ar.active_area_id, ar.target_code);
  ar.target_code.clear();
}

inline void do_execute_alarm_action() {
  auto &ar = runtime_state().area;
  latter_actions::emit_alarm_action(ar.alarm_service, ar.target_code);
  ar.target_code.clear();
}

inline void do_save_custom_color_to_latter() {
  save_persisted_prefs();
  latter_actions::emit_save_custom_color_rgb(persisted_prefs().custom_theme_rgb);
}

inline int32_t resolve_touch_today_ymd(const UiTouchRuntime &rt) noexcept {
  if (rt.time_valid && rt.time_year >= 2020) {
    return make_ymd(rt.time_year, rt.time_month, rt.time_day);
  }
  return 20260101;
}

inline void ensure_calendar_window_for_ymd(int32_t anchor_ymd,
                                           const PersistedDevicePrefs &prefs,
                                           bool force = false) {
  auto &store = calendar_window_store();
  const int lookahead = static_cast<int>(snap_calendar_lookahead_days(prefs.calendar_lookahead_days));
  const bool out_of_bounds =
      (store.window_start_ymd == 0) ||
      (anchor_ymd < add_days_ymd(store.window_start_ymd, 14)) ||
      (anchor_ymd > add_days_ymd(store.window_end_ymd, -21));
  if (!force && !out_of_bounds) return;

  const int32_t want_start = add_days_ymd(anchor_ymd, -35);
  const int32_t want_end   = add_days_ymd(anchor_ymd, std::max(62, lookahead + 7));
  char s_iso[12];
  char e_iso[12];
  format_ymd_iso(want_start, s_iso);
  format_ymd_iso(want_end, e_iso);
  store.loading = true;
  latter_actions::emit_calendar_fetch_window(s_iso, e_iso, lookahead);
}

inline void stop_calendar_dictation_if_active(DeviceRuntimeState &rs,
                                              const UiTouchRuntime &rt,
                                              uint32_t now_ms,
                                              const char *msg = "") {
  if (!cal_dictation_active(rs)) return;
  cancel_calendar_dictation(msg, now_ms, rs);
  rs.voice.intercom_turn = false;
  rt.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
  rt.apply_power_policy();
  rt.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
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
    char s_dt[24];
    char e_dt[24];
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

inline void do_send_intercom_broadcast() {
  latter_actions::emit_intercom_broadcast(runtime_state().intercom.broadcast_message);
}

inline void do_remote_select_slot(int slot_num) {
  if (!remote_state().slots_summary.empty()) {
    latter_actions::emit_remote_action(slot_num, "select_slot");
  }
}

inline int remote_key_for_widget(uint16_t wid) noexcept {
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

inline void do_remote_send_key(int key_idx, int target_slot, bool long_press = false,
                               uint32_t now_override_ms = 0U) {
  auto &rs = runtime_state();
  int res_slot = (target_slot == -1) ? resolve_active_remote_slot(0)
                                     : resolve_active_remote_slot(target_slot);
  const int slot_1based = res_slot + 1;

  static constexpr const char *kCanonicalCmds[10] = {
    "up", "down", "left", "right", "select",
    "back", "home", "volume_up", "volume_down", "toggle"
  };
  static constexpr const char *kFeedback[10] = {
    "Up", "Down", "Left", "Right", "Select",
    "Back", "Home", "Vol +", "Vol -", "Toggle Power"
  };
  static constexpr const char *kHoldFeedback[10] = {
    "Hold Up", "Hold Down", "Hold Left", "Hold Right", "Hold OK",
    "Hold Back", "Hold Home", "Vol +", "Vol -", "Toggle Power"
  };
  int k = (key_idx >= 0 && key_idx < 10) ? key_idx : 0;
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
  rs.remote.feedback_timer = (now_override_ms != 0U) ? now_override_ms : esphome::millis();
  if (!remote_state().slots_summary.empty()) {
    latter_actions::emit_remote_action(slot_1based, act, cmd);
  }
}

inline uint32_t next_rng_u32() {
#ifdef USE_ESP32
  return esp_random();
#else
  return static_cast<uint32_t>(std::rand());
#endif
}

inline void reset_music_loading_slots(DeviceRuntimeState &rs, const char *msg) {
  auto *slots = rs.music_slots();
  slots[0].name = msg;
  slots[0].id.clear();
  slots[0].type = "text";
  slots[0].active = false;
  slots[0].queue_item_id.clear();
  slots[0].is_current = false;
  for (size_t i = 1; i < DeviceRuntimeState::kSlotsPerPage; ++i) {
    slots[i] = BrowseSlotItem{};
    slots[i].type = "player";
  }
}

inline std::string resolve_music_speaker_entity(const DeviceRuntimeState &rs) {
  const auto &ms = media_state();
  if (!ms.active_speaker.empty()) return ms.active_speaker;
  if (!rs.music.active_speaker_entity.empty()) return rs.music.active_speaker_entity;
  return ms.local_speaker;
}

inline void resolve_music_track_strings(const DeviceRuntimeState &rs, const UiTouchRuntime &rt,
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

inline int fetch_music_browse_for_current_view(const DeviceRuntimeState &rs, uint32_t now_ms) {
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

inline void reset_music_to_now_playing(DeviceRuntimeState &rs, uint32_t now_ms, bool show_hud = true) {
  rs.music.reset_overlays();
  rs.music.view = MusicView::NOW_PLAYING;
  rs.music.sync_carousel_for_view();
  mode_bar_anim().snap(0, 0);
  rs.music.hud_active = show_hud;
  rs.music.hud_timer = now_ms;
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

inline bool open_music_row_action_sheet(int slot_idx, DeviceRuntimeState &rs, uint32_t now_ms) {
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

inline bool open_music_header_action_sheet(DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms) {
  std::string t_title, t_artist, t_album;
  bool is_speech = false;
  resolve_music_track_strings(rs, rt, t_title, t_artist, t_album, is_speech);

  std::string cid;
  std::string cname;
  std::string ctype = "album";
  if (rs.music.view == MusicView::ALBUM_TRACKS) {
    cid = !rs.music.drilldown_album_id.empty() ? rs.music.drilldown_album_id : t_album;
    cname = !t_album.empty() ? t_album : cid;
    ctype = is_speech ? "podcast" : "album";
  } else if (rs.music.selected_category_mode == "album_tracks") {
    cid = rs.music.drilldown_album_id;
    cname = rs.music.selected_category_title;
    ctype = (rs.music.drilldown_parent_mode == "playlist") ? "playlist" : "album";
  } else if (rs.music.selected_category_mode == "artist_albums") {
    cid = rs.music.drilldown_artist_name;
    cname = rs.music.drilldown_artist_name;
    ctype = "artist";
  }
  if (cid.empty()) return false;

  rs.music.action_sheet_slot = -1;
  rs.music.action_sheet_is_queue = false;
  rs.music.action_sheet_is_container = true;
  rs.music.action_sheet_id = cid;
  rs.music.action_sheet_name = cname;
  rs.music.action_sheet_type = ctype;
  rs.music.action_sheet_queue_item_id.clear();
  rs.music.takeover = MusicTakeover::ACTION_SHEET;
  rs.music.takeover_interacted_ms = (now_ms == 0U) ? 1U : now_ms;
  return true;
}

inline void dispatch_music_play_item(const std::string &media_id, const std::string &media_type,
                                     const std::string &item_name, std::string_view enqueue,
                                     bool radio_mode, DeviceRuntimeState &rs, uint32_t now_ms) {
  if (media_id.empty()) return;
  auto &m = rs.music;
  auto &ms = media_state();
  if (m.active_speaker_entity.empty()) {
    m.active_speaker_entity = !ms.active_speaker.empty() ? ms.active_speaker : ms.local_speaker;
    ms.is_local = !ms.local_speaker.empty() && (m.active_speaker_entity == ms.local_speaker);
  }
  const std::string spk = resolve_music_speaker_entity(rs);
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
  if (radio_mode) {
    m.action_feedback = "Radio: " + item_name;
  } else if (enqueue == "next") {
    m.action_feedback = "Play Next: " + item_name;
  } else if (enqueue == "add") {
    m.action_feedback = "Added to Queue";
  } else if (enqueue == "replace") {
    m.action_feedback = "Playing: " + item_name;
  } else {
    m.action_feedback = "Playing: " + item_name;
  }
  m.feedback_timer = now_ms;
  m.hud_active = true;
  m.hud_timer = now_ms;
  latter_actions::emit_media_play_item(spk, media_id, media_type, enqueue, radio_mode);
}

inline void leave_home_hub_cameras_if_active(DeviceRuntimeState &rs, const UiTouchRuntime &rt,
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

inline void enter_cameras_app(int target_tab, std::string_view focus_entity, bool start_listen,
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
  rs.cameras.framing_override = -1;

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

inline void enter_home_hub_tab(int target_tab, bool /*expand_cam*/, DeviceRuntimeState &rs,
                               const UiTouchRuntime &rt, uint32_t now_ms) {
  const bool was_hub = (rs.nav.gui_page == PageId::HOME_HUB);
  if (is_camera_view_active(rs)) {
    leave_home_hub_cameras_if_active(rs, rt, now_ms);
  }
  rs.nav.gui_page = PageId::HOME_HUB;
  rs.nav.home_subpage = 0;
  rs.nav.tools_subpage = 0;
  if (!was_hub) {
    sync_home_hub_for_active_area(area_ctx_state().area_id, home_hub_state());
  }

  int hh_tabs[kMaxHomeHubTabs]{};
  const int hh_cnt = collect_home_hub_tabs(hh_tabs);
  int slot = -1;
  if (target_tab >= 0) {
    for (int i = 0; i < hh_cnt; ++i) {
      if (hh_tabs[i] == target_tab) {
        slot = i;
        break;
      }
    }
  }
  if (slot >= 0) {
    home_hub_tab() = target_tab;
    sync_mode_bar_window(home_hub_mode_bar_page(), slot, hh_cnt);
  } else if (hh_cnt > 0) {
    slot = 0;
    for (int i = 0; i < hh_cnt; ++i) {
      if (hh_tabs[i] == home_hub_tab()) {
        slot = i;
        break;
      }
    }
    home_hub_tab() = hh_tabs[slot];
    sync_mode_bar_window(home_hub_mode_bar_page(), slot, hh_cnt);
  } else {
    home_hub_mode_bar_page() = 0;
    home_hub_tab() = (target_tab >= 0) ? target_tab : HomeHubTab::ALARM;
  }
}

inline void launch_dock_app(int chosen_app, DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms) {
  rs.nav.shortcut_origin = ShortcutOrigin::NONE;
  shortcut_confirm_state().clear();
  home_dock_page() = 0;

  if (chosen_app == PageId::HOME_HUB) {
    enter_home_hub_tab(-1, false, rs, rt, now_ms);
  } else if (chosen_app == PageId::CAMERAS) {
    stop_calendar_dictation_if_active(rs, rt, now_ms);
    enter_cameras_app(-1, "", false, rs, rt, now_ms);
  } else {
    if (is_camera_view_active(rs) || is_calendar_map_view_active(rs)) {
      leave_home_hub_cameras_if_active(rs, rt, now_ms);
    }
    stop_calendar_dictation_if_active(rs, rt, now_ms);
    rs.nav.gui_page = chosen_app;
    rs.nav.home_subpage = 0;
  }

  if (chosen_app == PageId::PINNED) {
    rs.nav.pinned_menu_page = 0;
    rs.nav.menu_scroll_active = false;
  } else if (chosen_app == PageId::CALENDAR) {
    const int32_t today_ymd = resolve_touch_today_ymd(rt);
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
      do_fetch_area_list_browse();
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
        do_fetch_area_browse();
      }
    }
  }

  rs.nav.tools_subpage = 0;
  rs.nav.remote_subpage = 0;
  rs.nav.active_game = 0;
  mark_dirty_full();
  rt.schedule_disp();
}

inline void apply_optimistic_shortcut_feedback(const std::string &entity_id,
                                               const std::string &action,
                                               const std::string &label,
                                               DeviceRuntimeState &rs,
                                               uint32_t now_ms) {
  rs.nav.pinned_feedback = label.empty() ? entity_id : label;
  rs.nav.pinned_feedback_ms = (now_ms == 0U) ? 1U : now_ms;
  auto flip_list = [&](std::vector<ShortcutItem> &vec) {
    for (auto &it : vec) {
      if (it.kind != ShortcutKind::ENTITY || it.target != entity_id) continue;
      if (it.state == 0 || it.state == 1) {
        if (action == "toggle" || action == "media_play_pause") {
          it.state = (it.state == 1) ? 0 : 1;
        } else if (action == "turn_on" || action == "open_cover" || action == "open_valve" ||
                   action == "unlock" || action == "open" || action == "start") {
          it.state = 1;
        } else if (action == "turn_off" || action == "close_cover" || action == "close_valve" ||
                   action == "lock" || action == "stop" || action == "return_to_base") {
          it.state = 0;
        }
      }
    }
  };
  flip_list(shortcuts_state().dock_items);
  flip_list(shortcuts_state().pinned_items);
}

inline bool return_from_shortcut_origin(DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms) {
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

inline bool handle_music_back_step(DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms) {
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
      if (m.selected_category_mode == "podcast") {
        m.selected_category_title = "PODCASTS";
      } else if (m.selected_category_mode == "playlist") {
        m.selected_category_title = "PLAYLISTS";
      } else if (m.selected_category_mode == "artist_albums") {
        m.selected_category_title = m.drilldown_artist_name;
      } else {
        m.selected_category_title = "ALBUMS";
      }
      b_page = std::max(1, m.drilldown_album_page);
      reset_music_loading_slots(rs, "Loading...");
      fetch_music_browse_for_current_view(rs, now_ms);
      mark_dirty_full();
      rt.schedule_disp();
      return true;
    }
    if (m.selected_category_mode == "artist_albums") {
      m.selected_category_mode = "artist";
      m.selected_category_title = "ARTISTS";
      b_page = std::max(1, m.drilldown_artist_page);
      reset_music_loading_slots(rs, "Loading...");
      fetch_music_browse_for_current_view(rs, now_ms);
      mark_dirty_full();
      rt.schedule_disp();
      return true;
    }
    if (m.direct_nav) {
      m.direct_nav = false;
      reset_music_to_now_playing(rs, now_ms, true);
      if (return_from_shortcut_origin(rs, rt, now_ms)) return true;
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
    if (return_from_shortcut_origin(rs, rt, now_ms)) return true;
    mark_dirty_full();
    rt.schedule_disp();
    return true;
  }
  return false;
}

inline void launch_shortcut_route(RouteId route_id, ShortcutOrigin origin,
                                  DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                                  uint32_t now_ms) {
  if (!is_route_supported_on_board(route_id)) return;
  const int saved_pinned_pg = rs.nav.pinned_menu_page;
  shortcut_confirm_state().clear();

  switch (route_id) {
    case RouteId::APP_PINNED:
      launch_dock_app(PageId::PINNED, rs, rt, now_ms);
      return;
    case RouteId::APP_MUSIC:
      launch_dock_app(PageId::MUSIC, rs, rt, now_ms);
      return;
    case RouteId::APP_HOME_HUB:
      launch_dock_app(PageId::HOME_HUB, rs, rt, now_ms);
      return;
    case RouteId::APP_CAMERAS:
      launch_dock_app(PageId::CAMERAS, rs, rt, now_ms);
      rs.nav.shortcut_origin = origin;
      rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
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
      rs.nav.shortcut_origin = origin;
      rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }
    case RouteId::APP_AREA:
      launch_dock_app(PageId::AREA, rs, rt, now_ms);
      return;
    case RouteId::APP_REMOTE:
      if (has_any_configured_remote()) {
        launch_dock_app(PageId::REMOTE, rs, rt, now_ms);
      }
      return;
    case RouteId::APP_WEATHER:
    case RouteId::WEATHER_CURRENT:
      launch_dock_app(PageId::WEATHER, rs, rt, now_ms);
      return;
    case RouteId::WEATHER_FORECAST:
      launch_dock_app(PageId::WEATHER, rs, rt, now_ms);
      rs.nav.sky_weather_subpage = 1;
      return;
    case RouteId::WEATHER_SUN:
      launch_dock_app(PageId::WEATHER, rs, rt, now_ms);
      rs.nav.sky_weather_subpage = 2;
      return;
    case RouteId::WEATHER_MOON:
      launch_dock_app(PageId::WEATHER, rs, rt, now_ms);
      rs.nav.sky_weather_subpage = 3;
      return;
    case RouteId::APP_TIME:
    case RouteId::TIME_CLOCK:
      launch_dock_app(PageId::TIME, rs, rt, now_ms);
      return;
    case RouteId::TIME_TIMERS:
      launch_dock_app(PageId::TIME, rs, rt, now_ms);
      rs.nav.time_subpage = TimeSubpage::TIMERS;
      return;
    case RouteId::TIME_ALARMS:
      launch_dock_app(PageId::TIME, rs, rt, now_ms);
      rs.nav.time_subpage = TimeSubpage::ALARMS;
      return;
    case RouteId::TIME_STOPWATCH:
      launch_dock_app(PageId::TIME, rs, rt, now_ms);
      rs.nav.time_subpage = TimeSubpage::STOPWATCH;
      return;
    case RouteId::APP_CALENDAR:
    case RouteId::CALENDAR_OVERVIEW:
      launch_dock_app(PageId::CALENDAR, rs, rt, now_ms);
      rs.nav.shortcut_origin = origin;
      rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
      return;
    case RouteId::CALENDAR_DAY:
      launch_dock_app(PageId::CALENDAR, rs, rt, now_ms);
      rs.nav.shortcut_origin = origin;
      rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
      rs.cal.subpage = CalendarSubpage::DAY;
      return;
    case RouteId::CALENDAR_WEEK:
      launch_dock_app(PageId::CALENDAR, rs, rt, now_ms);
      rs.nav.shortcut_origin = origin;
      rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
      rs.cal.subpage = CalendarSubpage::WEEK;
      return;
    case RouteId::CALENDAR_MONTH:
      launch_dock_app(PageId::CALENDAR, rs, rt, now_ms);
      rs.nav.shortcut_origin = origin;
      rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
      rs.cal.subpage = CalendarSubpage::MONTH;
      return;
    case RouteId::APP_TOOLS:
      launch_dock_app(PageId::TOOLS, rs, rt, now_ms);
      return;
    case RouteId::APP_GAMES:
      launch_dock_app(PageId::GAMES, rs, rt, now_ms);
      return;
    case RouteId::APP_CLOCK:
      if (is_camera_view_active(rs)) leave_home_hub_cameras_if_active(rs, rt, now_ms);
      rs.nav.shortcut_origin = ShortcutOrigin::NONE;
      rs.nav.gui_page = PageId::CLOCK;
      rs.nav.home_subpage = 0;
      mark_dirty_full();
      rt.schedule_disp();
      return;
    case RouteId::SETTINGS_ROOT:
      launch_dock_app(PageId::SYSTEM, rs, rt, now_ms);
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
      enter_home_hub_tab(tab, false, rs, rt, now_ms);
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::AREA_PICKER: {
      if (is_camera_view_active(rs)) leave_home_hub_cameras_if_active(rs, rt, now_ms);
      rs.nav.shortcut_origin = origin;
      rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
      rs.nav.gui_page = PageId::AREA;
      rs.nav.home_subpage = 0;
      rs.nav.area_subpage = AreaSubpage::PICKER;
      rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST) = 1;
      do_fetch_area_list_browse();
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::AREA_SCENE:
    case RouteId::AREA_LIGHT:
    case RouteId::AREA_COVER:
    case RouteId::AREA_FAN:
    case RouteId::AREA_SWITCH:
    case RouteId::AREA_CLIMATE:
    case RouteId::AREA_MEDIA:
    case RouteId::AREA_ALL: {
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
        do_fetch_area_list_browse();
      } else {
        rs.nav.area_subpage = AreaSubpage::OVERVIEW;
        rs.area.browse_mode = "entities";
        rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
        do_fetch_area_browse();
      }
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::MUSIC_SPEAKERS:
    case RouteId::MUSIC_LIBRARY:
    case RouteId::MUSIC_FAVORITES:
    case RouteId::MUSIC_PLAYLISTS:
    case RouteId::MUSIC_QUEUE:
    case RouteId::MUSIC_ALBUM_TRACKS:
    case RouteId::MUSIC_LYRICS:
    case RouteId::MUSIC_INFO: {
      launch_dock_app(PageId::MUSIC, rs, rt, now_ms);
      rs.nav.shortcut_origin = origin;
      rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
      rs.music.direct_nav = true;
      rs.slot_page_for(latter_rpc::SlotChannel::MUSIC) = 1;
      rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
      if (route_id == RouteId::MUSIC_SPEAKERS) {
        activate_music_peer_tab(MusicView::SPEAKERS, now_ms);
        mode_bar_anim().snap(rs.music.mode_bar_win_start, static_cast<int>(rs.music.view));
      } else if (route_id == RouteId::MUSIC_LIBRARY) {
        activate_music_peer_tab(MusicView::LIBRARY, now_ms);
        mode_bar_anim().snap(rs.music.mode_bar_win_start, static_cast<int>(rs.music.view));
      } else if (route_id == RouteId::MUSIC_QUEUE) {
        activate_music_peer_tab(MusicView::QUEUE, now_ms);
        mode_bar_anim().snap(rs.music.mode_bar_win_start, static_cast<int>(rs.music.view));
      } else if (route_id == RouteId::MUSIC_ALBUM_TRACKS) {
        activate_music_peer_tab(MusicView::ALBUM_TRACKS, now_ms);
        mode_bar_anim().snap(rs.music.mode_bar_win_start, static_cast<int>(rs.music.view));
      } else if (route_id == RouteId::MUSIC_LYRICS) {
        activate_music_peer_tab(MusicView::LYRICS, now_ms);
        mode_bar_anim().snap(rs.music.mode_bar_win_start, static_cast<int>(rs.music.view));
      } else if (route_id == RouteId::MUSIC_INFO) {
        activate_music_peer_tab(MusicView::INFO, now_ms);
        mode_bar_anim().snap(rs.music.mode_bar_win_start, static_cast<int>(rs.music.view));
      } else {
        rs.music.apply_route(route_id);
        mode_bar_anim().snap(rs.music.mode_bar_win_start, static_cast<int>(rs.music.view));
        reset_music_loading_slots(rs, "Loading...");
        fetch_music_browse_for_current_view(rs, now_ms);
      }
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::REMOTE_DEVICES:
      if (has_any_configured_remote()) {
        launch_dock_app(PageId::REMOTE, rs, rt, now_ms);
        rs.nav.shortcut_origin = origin;
        rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
        rs.nav.remote_subpage = 1;
        remote_control_page() = 0;
        mark_dirty_full();
        rt.schedule_disp();
      }
      return;

    case RouteId::TOOLS_STOPWATCH:
    case RouteId::TOOLS_LEVEL:
    case RouteId::TOOLS_FLASHLIGHT:
    case RouteId::TOOLS_CALCULATOR:
    case RouteId::TOOLS_DICE:
    case RouteId::TOOLS_COIN_FLIP: {
      launch_dock_app(PageId::TOOLS, rs, rt, now_ms);
      rs.nav.shortcut_origin = origin;
      rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
      int sub = ToolsSubpage::LAUNCHER;
      if (route_id == RouteId::TOOLS_STOPWATCH) sub = ToolsSubpage::STOPWATCH;
      else if (route_id == RouteId::TOOLS_LEVEL) sub = ToolsSubpage::LEVEL;
      else if (route_id == RouteId::TOOLS_FLASHLIGHT) sub = ToolsSubpage::FLASHLIGHT;
      else if (route_id == RouteId::TOOLS_CALCULATOR) sub = ToolsSubpage::CALCULATOR;
      else if (route_id == RouteId::TOOLS_DICE) sub = ToolsSubpage::DICE;
      else if (route_id == RouteId::TOOLS_COIN_FLIP) sub = ToolsSubpage::COIN_FLIP;
      rs.nav.tools_subpage = sub;
      if (sub == ToolsSubpage::FLASHLIGHT) {
        rt.cmd(UiHostCmd::FLASHLIGHT_FULL_BRIGHTNESS);
      } else if (sub == ToolsSubpage::CALCULATOR) {
        reset_calculator_state();
      }
      rt.apply_power_policy();
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::GAMES_MARBLES:
    case RouteId::GAMES_ARCHERY:
    case RouteId::GAMES_BRICKS:
    case RouteId::GAMES_CHOMP:
    case RouteId::GAMES_2048:
    case RouteId::GAMES_MEMORY: {
      const int g_id = 1 + (static_cast<int>(route_id) - static_cast<int>(RouteId::GAMES_MARBLES));
      launch_dock_app(PageId::GAMES, rs, rt, now_ms);
      rs.nav.shortcut_origin = origin;
      rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
      int enabled_ids[kMaxGamesLauncherItems]{};
      const int game_cnt = collect_enabled_games(enabled_ids, device_prefs());
      int matched_slot = 0;
      for (int i = 0; i < game_cnt; ++i) {
        if (enabled_ids[i] == g_id) {
          matched_slot = i;
          break;
        }
      }
      rs.nav.game_carousel_idx = matched_slot;
      rs.nav.active_game = g_id;
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
      rs.nav.shortcut_origin = origin;
      rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
      rs.nav.gui_page = PageId::HOME;
      rs.nav.home_subpage = 1;
      rs.nav.home_settings_page = 1;
      mark_dirty_full();
      rt.schedule_disp();
      return;

    case RouteId::SETTINGS_DISPLAY:
    case RouteId::SETTINGS_SLEEP:
    case RouteId::SETTINGS_SOUND:
    case RouteId::SETTINGS_VOICE:
    case RouteId::SETTINGS_MUSIC:
    case RouteId::SETTINGS_ACTIONS:
    case RouteId::SETTINGS_UPDATE:
    case RouteId::SETTINGS_INFO: {
      launch_dock_app(PageId::SYSTEM, rs, rt, now_ms);
      rs.nav.shortcut_origin = origin;
      rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
      if (route_id == RouteId::SETTINGS_DISPLAY) { rs.nav.system_menu_mode = 2; rs.nav.display_menu_page = 1; }
      else if (route_id == RouteId::SETTINGS_SLEEP) { rs.nav.system_menu_mode = 1; rs.nav.sleep_menu_page = 1; }
      else if (route_id == RouteId::SETTINGS_SOUND) { rs.nav.system_menu_mode = 7; }
      else if (route_id == RouteId::SETTINGS_VOICE) { rs.nav.system_menu_mode = 11; }
      else if (route_id == RouteId::SETTINGS_MUSIC) { rs.nav.system_menu_mode = 9; rs.nav.audio_menu_parent_page = PageId::SYSTEM; }
      else if (route_id == RouteId::SETTINGS_ACTIONS) { rs.nav.system_menu_mode = 3; }
      else if (route_id == RouteId::SETTINGS_UPDATE) { rs.nav.system_menu_mode = 5; rs.nav.update_check_state = 0; }
      else if (route_id == RouteId::SETTINGS_INFO) { rs.nav.system_menu_mode = 8; }
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::NONE:
    default:
      return;
  }
}

inline void activate_shortcut_item(const ShortcutItem &item, ShortcutOrigin origin,
                                   DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                                   uint32_t now_ms) {
  if (item.kind == ShortcutKind::ROUTE) {
    launch_shortcut_route(item.route_id, origin, rs, rt, now_ms);
    return;
  }

  if (item.target.empty() || item.action.empty()) return;
  if (item.state == 2) {
    rs.nav.pinned_feedback = "Unavailable: " + item.label;
    rs.nav.pinned_feedback_ms = (now_ms == 0U) ? 1U : now_ms;
    rt.schedule_disp();
    return;
  }

  const std::string &pol = area_ctx_state().security_pin_policy;
  if (ui::is_perimeter_action_blocked(pol, item.target, item.action, 100)) {
    rs.nav.pinned_feedback = "Blocked by Security Policy";
    rs.nav.pinned_feedback_ms = (now_ms == 0U) ? 1U : now_ms;
    rt.schedule_disp();
    return;
  }
  if (ui::requires_perimeter_pin_prompt(pol, item.target, item.action, 100, effective_alarm_caps())) {
    rs.nav.pending_shortcut_entity_id = item.target;
    rs.nav.pending_shortcut_action = item.action;
    rs.nav.pending_shortcut_label = item.label;
    ui::pin_pad_open(ui::PinPadTargetKind::SHORTCUT_ACTION, "SECURITY PIN");
    mark_dirty_full();
    rt.schedule_disp();
    return;
  }

  if (item.confirm) {
    auto &sc = shortcut_confirm_state();
    sc.open = true;
    sc.opened_ms = (now_ms == 0U) ? 1U : now_ms;
    sc.entity_id = item.target;
    sc.action = item.action;
    build_shortcut_confirm_copy(item, sc.title, sc.line1, sc.line2, sc.verb_btn, sc.is_danger);
    mark_dirty_full();
    rt.schedule_disp();
    return;
  }

  latter_actions::emit_shortcut_action(item.target, item.action);
  apply_optimistic_shortcut_feedback(item.target, item.action, item.label, rs, now_ms);
  rt.schedule_disp();
}

inline void record_stopwatch_lap(StopwatchRuntimeState &sw, uint32_t cur_ms) {
  sw.record_lap_elapsed(cur_ms);
}

inline void handle_home_activity_action(const HomeActivityItem *items, int count, int item_idx,
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
      const uint32_t intent_ts = (esphome::millis() != 0U) ? esphome::millis() : now_ms;
      audio_session().set_play_intent(MusicPlayIntent::HomeQuickToggle, intent_ts);
      rt.cmd(UiHostCmd::TOGGLE_HA_MUSIC);
    } else if (kind == 4) {
      if (s_idx == 0) {
        rs.alarms.snooze_until = 0;
      } else if (s_idx >= 1 && s_idx <= 3) {
        prefs.alarms[s_idx - 1].enabled = false;
        if (rs.alarms.ringing_idx == s_idx) rs.alarms.snooze_until = 0;
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
    launch_dock_app(PageId::CALENDAR, rs, rt, now_ms);
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
      enter_home_hub_tab(HomeHubTab::INTERCOM, false, rs, rt, now_ms);
    }
  }
  mark_dirty_full();
  rt.schedule_disp();
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

inline void update_light_inspector_hex(float hue_deg, float sat) {
  const esphome::Color rgb = hsv_to_rgb(hue_deg / 360.0f, sat, 1.0f);
  char hex_buf[12];
  std::snprintf(hex_buf, sizeof(hex_buf), "#%02X%02X%02X", rgb.r, rgb.g, rgb.b);
  area_color_hex_param() = hex_buf;
}

}  // namespace

TouchGestureEngine &touch_engine() {
  static TouchGestureEngine s_engine;
  return s_engine;
}

void activate_music_peer_tab(MusicView target, uint32_t now_ms) {
  auto &rs = runtime_state();
  const auto &rt = touch_engine().runtime();
  auto &m = rs.music;

  m.overlay_mode = 0;
  m.drilldown = MusicDrilldown::NONE;
  m.takeover = MusicTakeover::NONE;
  m.target_speaker_id.clear();
  m.hud_active = true;
  m.hud_timer = now_ms;
  rs.nav.menu_scroll_active = false;
  m.view = target;
  m.sync_carousel_for_view();

  std::string t_title, t_artist, t_album;
  bool is_speech = false;
  resolve_music_track_strings(rs, rt, t_title, t_artist, t_album, is_speech);

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
      m.selected_category_mode = "speakers";
      m.selected_category_title = "SPEAKERS";
      rs.slot_page_for(latter_rpc::SlotChannel::MUSIC) = 1;
      rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
      reset_music_loading_slots(rs, "Loading...");
      fetch_music_browse_for_current_view(rs, now_ms);
      break;
    case MusicView::VOLUME:
      m.takeover_interacted_ms = (now_ms == 0U) ? 1U : now_ms;
      break;
    case MusicView::QUEUE:
      m.selected_category_mode = "active_queue";
      m.selected_category_title = "PLAYBACK QUEUE";
      rs.slot_page_for(latter_rpc::SlotChannel::MUSIC) = 1;
      rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
      reset_music_loading_slots(rs, "Loading queue...");
      fetch_music_browse_for_current_view(rs, now_ms);
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
        latter_actions::emit_music_lyrics_fetch(key, resolve_music_speaker_entity(rs), now_ms);
      }
      break;
    }
    case MusicView::ALBUM_TRACKS:
      m.selected_category_mode = is_speech ? "podcast_episodes" : "current_album_tracks";
      m.drilldown_album_id = !t_album.empty() ? t_album : t_artist;
      m.selected_category_title =
          t_album.empty() ? std::string(is_speech ? "EPISODES" : "ALBUM TRACKS") : t_album;
      rs.slot_page_for(latter_rpc::SlotChannel::MUSIC) = 1;
      rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
      reset_music_loading_slots(rs, is_speech ? "Loading episodes..." : "Loading album tracks...");
      fetch_music_browse_for_current_view(rs, now_ms);
      break;
    case MusicView::INFO:
      m.info_scroll_px = 0;
      m.drilldown_album_id = t_title + "|" + t_artist + "|" + t_album;
      m.selected_category_mode = "bio";
      m.selected_category_title = "TRACK INFO";
      rs.slot_page_for(latter_rpc::SlotChannel::MUSIC) = 1;
      rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
      reset_music_loading_slots(rs, "Loading...");
      fetch_music_browse_for_current_view(rs, now_ms);
      break;
    case MusicView::SETTINGS:
    default:
      m.settings_page = 1;
      m.selected_category_mode = "settings";
      m.selected_category_title = "MUSIC SETTINGS";
      break;
  }
}

void TouchGestureEngine::navigate_to_start_screen(uint32_t now_ms) {
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const int anchor = resolve_anchor_dock_app(prefs);
  if (rs.nav.active_game > 0) {
    rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
  }
  if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
    intercom_session().stop_call_with_hw(now_ms);
  }
  const bool was_cam_page = is_camera_view_active(rs) || is_calendar_map_view_active(rs);
  stop_calendar_dictation_if_active(rs, rt_, now_ms);
  ui::pin_pad_scrub();
  shortcut_confirm_state().clear();
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
  rs.music.direct_nav = false;
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
  rs.nav.time_subpage = TimeSubpage::CLOCK;
  rs.nav.world_clock_page = 0;
  rs.nav.home_settings_page = 1;
  const bool start_cam = is_cameras_start_screen(prefs);
  const bool keep_cam = start_cam || (anchor == PageId::CAMERAS);
  if (was_cam_page && !keep_cam) {
    rt_.cmd(UiHostCmd::STOP_CAMERA_STREAM);
    rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
    rs.alert.loaded_camera_url.clear();
    rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
  }
  if (anchor == PageId::HOME) {
    rs.nav.gui_page = PageId::HOME;
    rs.nav.home_subpage = 0;
    mark_dirty_full();
    rt_.schedule_disp();
    return;
  }
  if (anchor == PageId::CLOCK) {
    rs.nav.gui_page = PageId::CLOCK;
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
  launch_dock_app(anchor, rs, rt_, now_ms);
}

void TouchGestureEngine::navigate_home(uint32_t now_ms) {
  if (!persisted_prefs().home_screen_enabled) {
    navigate_to_start_screen(now_ms);
    return;
  }
  auto &rs = runtime_state();
  if (rs.nav.active_game > 0) {
    rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
  }
  if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
    intercom_session().stop_call_with_hw(now_ms);
  }
  const bool was_cam_page = is_camera_view_active(rs) || is_calendar_map_view_active(rs);
  stop_calendar_dictation_if_active(rs, rt_, now_ms);
  ui::pin_pad_scrub();
  shortcut_confirm_state().clear();
  rs.nav.shortcut_origin = ShortcutOrigin::NONE;
  timer_view_slot() = 0;
  rs.alarms.edit_idx = 0;
  rs.cal.drilldown = CalDrilldown::NONE;
  rs.cal.delete_confirm_open = false;
  cam_quick_reply_open() = false;
  cam_expanded_fullscreen() = false;
  rs.nav.gui_page = PageId::HOME;
  rs.nav.home_subpage = 0;
  reset_music_to_now_playing(rs, now_ms, false);
  rs.nav.system_menu_mode = 0;
  rs.nav.active_game = 0;
  rs.music.direct_nav = false;
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

bool TouchGestureEngine::handle_pin_pad_tap(int tx, int ty, uint32_t now_ms) {
  auto &st = ui::pin_pad_state();
  if (!st.open) return false;

  auto &rs = runtime_state();
  const HitRegion *hit = frame_hits().hit_test(tx, ty, kHitTap);
  if (hit != nullptr && (hit->widget_id == WidgetId::PIN_PAD_CANCEL ||
                         hit->widget_id == WidgetId::BACK_BUTTON)) {
    if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      ui::pin_pad_scrub();
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return true;
  }
  if (hit == nullptr) {
    return true;
  }
  if (hit->widget_id >= WidgetId::PIN_PAD_KEY_BASE &&
      hit->widget_id <= WidgetId::PIN_PAD_KEY_BASE + 9) {
    if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID)) {
      ui::pin_pad_press_digit(static_cast<uint8_t>(hit->widget_id - WidgetId::PIN_PAD_KEY_BASE));
      rt_.schedule_disp();
    }
    return true;
  }
  if (hit->widget_id == WidgetId::PIN_PAD_BACKSPACE) {
    if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID)) {
      ui::pin_pad_backspace();
      rt_.schedule_disp();
    }
    return true;
  }
  if (hit->widget_id == WidgetId::PIN_PAD_SUBMIT) {
    if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      if (st.len < 4) {
        std::snprintf(st.error_msg, sizeof(st.error_msg), "Enter 4-8 digits");
        st.error_ms = now_ms;
        rt_.schedule_disp();
        return true;
      }
      std::string pin_str(st.digits, st.len);
      const auto target = st.target;
      ui::pin_pad_scrub();
      if (target == ui::PinPadTargetKind::AREA_ACTION) {
        rs.area.target_code = pin_str;
        rs.area.feedback_timer = now_ms;
        do_execute_area_action();
      } else if (target == ui::PinPadTargetKind::ALARM_ACTION) {
        rs.area.target_code = pin_str;
        rs.area.feedback_timer = now_ms;
        do_execute_alarm_action();
      } else if (target == ui::PinPadTargetKind::DOORBELL_UNLOCK) {
        rs.intercom.cam_code = pin_str;
        rs.intercom.cam_action = "unlock_door";
        rs.intercom.cam_message.clear();
        rs.intercom.broadcast_feedback = "Unlocked Door / Gate";
        rs.intercom.broadcast_feedback_ms = now_ms;
        cam_intercom_status() = "DOOR / GATE UNLOCKED";
        rs.intercom.cam_status_ms = now_ms;
        intercom_session().emit_current_cmd();
      } else if (target == ui::PinPadTargetKind::SHORTCUT_ACTION) {
        if (!rs.nav.pending_shortcut_entity_id.empty() && !rs.nav.pending_shortcut_action.empty()) {
          latter_actions::emit_shortcut_action(
              rs.nav.pending_shortcut_entity_id, rs.nav.pending_shortcut_action, pin_str);
          apply_optimistic_shortcut_feedback(
              rs.nav.pending_shortcut_entity_id, rs.nav.pending_shortcut_action,
              rs.nav.pending_shortcut_label, rs, now_ms);
        }
        rs.nav.pending_shortcut_entity_id.clear();
        rs.nav.pending_shortcut_action.clear();
        rs.nav.pending_shortcut_label.clear();
      }
      std::fill(pin_str.begin(), pin_str.end(), '\0');
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return true;
  }
  return true;
}

bool TouchGestureEngine::gate_or_dispatch_area_action(const std::string &domain_or_entity,
                                                      const std::string &action, int level,
                                                      const char *pin_prompt, uint32_t now_ms) {
  auto &rs = runtime_state();
  const std::string &pol = area_ctx_state().security_pin_policy;
  if (ui::is_perimeter_action_blocked(pol, domain_or_entity, action, level)) {
    rs.area.action_feedback = "Blocked by Security Policy";
    rs.area.feedback_timer = now_ms;
    rt_.schedule_disp();
    return false;
  }
  rs.area.target_action = action;
  rs.area.target_level = level;
  if (ui::requires_perimeter_pin_prompt(pol, domain_or_entity, action, level)) {
    ui::pin_pad_open(ui::PinPadTargetKind::AREA_ACTION, pin_prompt);
    mark_dirty_full();
    rt_.schedule_disp();
    return false;
  }
  rs.area.feedback_timer = now_ms;
  do_execute_area_action();
  rt_.schedule_disp();
  return true;
}

bool TouchGestureEngine::gate_or_dispatch_alarm_action(const std::string &action, uint32_t now_ms) {
  auto &rs = runtime_state();
  const std::string &pol = area_ctx_state().security_pin_policy;
  if (ui::is_perimeter_action_blocked(pol, "home_hub.alarm", action, 100)) {
    rs.area.action_feedback = "Blocked by Security Policy";
    rs.area.feedback_timer = now_ms;
    rt_.schedule_disp();
    return false;
  }
  rs.area.alarm_service = action;
  if (ui::requires_perimeter_pin_prompt(pol, "home_hub.alarm", action, 100)) {
    ui::pin_pad_open(ui::PinPadTargetKind::ALARM_ACTION,
                     (action == "disarm") ? "PIN TO DISARM" : "PIN TO ARM");
    mark_dirty_full();
    rt_.schedule_disp();
    return false;
  }
  rs.area.feedback_timer = now_ms;
  mark_dirty_full();
  do_execute_alarm_action();
  rt_.schedule_disp();
  return true;
}

bool TouchGestureEngine::gate_or_dispatch_doorbell_unlock(uint32_t now_ms, bool from_intercom) {
  auto &rs = runtime_state();
  const auto &ac = area_ctx_state();
  const std::string target_area = from_intercom
      ? ac.area_id
      : resolve_active_carousel_camera_area_id(ac.cameras_summary);
  const EntryControlItem *target =
      resolve_doorbell_quick_unlock_target(target_area, home_hub_state());
  if (target == nullptr) {
    return false;
  }
  if (!target->is_lock) {
    rs.area.target_entity_id = target->entity_id;
    rs.area.action_feedback = "Opening Garage...";
    return gate_or_dispatch_area_action(target->entity_id, "open_cover", 100, "GARAGE DOOR PIN", now_ms);
  }
  const std::string &pol = ac.security_pin_policy;
  if (ui::is_perimeter_action_blocked(pol, "lock", "unlock", 100)) {
    if (from_intercom) {
      rs.intercom.broadcast_feedback = "Blocked by Security Policy";
      rs.intercom.broadcast_feedback_ms = now_ms;
    } else {
      cam_intercom_status() = "BLOCKED BY POLICY";
      rs.intercom.cam_status_ms = now_ms;
    }
    rt_.schedule_disp();
    return false;
  }
  if (ui::requires_perimeter_pin_prompt(pol, "lock", "unlock", 100)) {
    ui::pin_pad_open(ui::PinPadTargetKind::DOORBELL_UNLOCK, "UNLOCK DOOR PIN");
    mark_dirty_full();
    rt_.schedule_disp();
    return false;
  }
  rs.intercom.cam_action = "unlock_door";
  rs.intercom.cam_message.clear();
  if (from_intercom) {
    rs.intercom.broadcast_feedback = "Unlocked Door / Gate";
    rs.intercom.broadcast_feedback_ms = now_ms;
  } else {
    cam_intercom_status() = "DOOR / GATE UNLOCKED";
    rs.intercom.cam_status_ms = now_ms;
  }
  intercom_session().emit_current_cmd();
  rt_.schedule_disp();
  return true;
}

void TouchGestureEngine::on_touch_down(int tx, int ty, uint32_t now_touch) {
  cancel_core_timeout(kTimeoutTouchLongPressId);
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  const TouchPt touch{tx, ty};

  // ----------------------------------------------------------------------------
  // 1. System Takeover Intercepts ([TAKEOVER ORDER] matches render_system_takeovers)
  //    OTA -> Hibernating -> Screen Off -> Alarm/Timer/FindMy -> VA -> PIN -> Notif
  // ----------------------------------------------------------------------------
  if (rs.nav.ota_in_progress) {
    return;
  }

  if (rs.nav.is_hibernating) {
    if (rs.nav.hibernate_countdown_val > 0) {
      rs.nav.is_hibernating = false;
      rs.nav.hibernate_countdown_val = 0;
      note_wake_reason(WakeReason::TOUCH);
      rt_.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    }
    return;
  }

  if (rs.nav.display_power_state != 0 || !rt_.panel_is_on) {
    const std::string_view tw_mode = rt_.select_opt(UiSelectId::TAP_WAKE_MODE);
    if (!accept_screen_off_tap(tw_mode, rt_.usb_connected, now_touch)) return;
    note_wake_reason(WakeReason::TOUCH);
    rt_.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    return;
  }

  if (now_touch - last_screen_wake_ms() < TOUCH_DEBOUNCE_WAKE) {
    if (latter_ui::profiler_enabled()) {
      LATTER_PROF_LOGI("[PROF:TOUCH_BLOCKED] x=%d y=%d page=%d reason=WAKE_DEBOUNCE",
                       touch.x, touch.y, rs.nav.gui_page);
    }
    return;
  }

  if (now_touch - last_touch_activity_ms_ > 1000U) {
    last_touch_activity_ms_ = now_touch;
    display_lifecycle().note_activity(now_touch);
  }

  auto &gest = touch_gesture();
  clear_ui_focus();
  gest.start_x = touch.x;
  gest.start_y = touch.y;
  gest.last_x = touch.x;
  gest.last_y = touch.y;
  gest.max_travel = 0;
  gest.sample_count = 1;
  gest.start_ms = now_touch;
  gest.last_touch_ms = now_touch;
  last_user_touch_ms() = now_touch;

  // Priority 1: Alarm / Timer / Find-My Ringing Takeover (above Voice Assistant!)
  if (rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active) {
    gest.active = false;
    const HitRegion *t_hit = frame_hits().hit_test(touch.x, touch.y, kHitTap);
    if (rs.alarms.find_my_active) {
      rs.alarms.find_my_active = false;
      last_screen_wake_ms() = now_touch;
      rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
      rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
      mark_dirty_full();
      rt_.schedule_disp();
      return;
    }
    if (rs.alarms.ringing) {
      if (t_hit != nullptr && t_hit->widget_id == WidgetId::ALERT_SNOOZE_BTN) {
        rs.alarms.snooze_until = (rt_.time_epoch > 0) ? (rt_.time_epoch + 300U)
                                                      : ((now_touch / 1000U) + 300U);
        rs.alarms.ringing = false;
        latter_actions::check_and_emit_timer_alarm_state(now_touch);
        last_screen_wake_ms() = now_touch;
        rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (t_hit != nullptr && t_hit->widget_id == WidgetId::ALERT_DISMISS_BTN) {
        if (rs.alarms.ringing_idx >= 1 && rs.alarms.ringing_idx <= 3) {
          auto &ap = prefs.alarms[rs.alarms.ringing_idx - 1];
          if (ap.recurrence == 3) {
            ap.enabled = false;
            latter_actions::sync_local_alarms_edit(now_touch);
          }
        }
        rs.alarms.ringing = false;
        rs.alarms.snooze_until = 0;
        latter_actions::check_and_emit_timer_alarm_state(now_touch);
        last_screen_wake_ms() = now_touch;
        rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
        rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      return;
    }
    if (rs.timers.ringing) {
      if (t_hit != nullptr && t_hit->widget_id == WidgetId::ALERT_DISMISS_BTN) {
        rs.timers.ringing = false;
        rs.timers.va_timer_name.clear();
        sync_multi_timer_state();
        latter_actions::check_and_emit_timer_alarm_state(now_touch);
        last_screen_wake_ms() = now_touch;
        rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
        rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
        rt_.apply_power_policy();
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
  }

  // Priority 2: Voice Assistant Takeover (exempt during live doorbell PTT reply — D-066!)
  if (should_va_takeover_preempt(rs.voice.active, !rs.voice.status_text.empty(),
                                 rs.voice.intercom_turn, rs.nav.gui_page,
                                 rs.cameras.doorbell_live_popup_active)) {
    gest.active = false;
    last_screen_wake_ms() = now_touch;
    rt_.cmd(UiHostCmd::CANCEL_VOICE_ASSISTANT);
    return;
  }

  // Priority 3: Security PIN Pad Modal
  if (ui::pin_pad_state().open) {
    gest.active = false;
    handle_pin_pad_tap(touch.x, touch.y, now_touch);
    return;
  }

  // Priority 4: Push Notification Banner / Doorbell Camera Popup (Snapshot & P4 Live Video)
  if (rs.alert.active) {
    gest.active = false;
    last_screen_wake_ms() = now_touch;
    const HitRegion *n_hit = frame_hits().hit_test(touch.x, touch.y, kHitTap);
    const uint16_t nwid = (n_hit != nullptr) ? n_hit->widget_id : WidgetId::NONE;
    const bool is_cam_notif = !rs.alert.camera_url.empty() || !rs.alert.camera_entity.empty();

    // Live PTT banner tap during P4 live doorbell popup sends reply immediately without dismissing popup
    if (rs.voice.intercom_turn && nwid == WidgetId::CAM_PTT_BANNER_STOP) {
      rt_.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
      intercom_session().resume_listen(now_touch);
      rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      rt_.apply_power_policy();
      mark_dirty_full();
      rt_.schedule_disp();
      return;
    }

    if (is_cam_notif && rs.cameras.doorbell_live_popup_active) {
      // P4 Live Video Doorbell Popup interactive controls in-place
      if (nwid == WidgetId::NOTIF_ANSWER_BTN) {
        if (cam_call_active()) {
          intercom_session().stop_call_with_hw(now_touch);
        } else {
          cam_listen_active() = !cam_listen_active();
          rs.intercom.cam_action = cam_listen_active() ? "listen_start" : "listen_stop";
          cam_intercom_status() = cam_listen_active() ? "DOOR AUDIO ON" : "AUDIO MUTED";
          rs.intercom.cam_status_ms = now_touch;
          intercom_session().emit_current_cmd();
          if (!cam_listen_active()) {
            rt_.cmd(UiHostCmd::STOP_DOORBELL_AUDIO);
          }
        }
        rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (nwid == WidgetId::NOTIF_TALK_BTN) {
        if (!cam_call_active()) {
          intercom_session().start_call(now_touch);
        }
        rt_.cmd(UiHostCmd::START_DOORBELL_PTT);
        rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (nwid == WidgetId::NOTIF_REPLY_BTN) {
        const std::string focus_ent = rs.alert.camera_entity;
        rs.alert.active = false;
        rs.cameras.doorbell_live_popup_active = false;
        rs.nav.shortcut_origin = ShortcutOrigin::NONE;
        enter_cameras_app(CamerasTab::SINGLE, focus_ent, false, rs, rt_, now_touch);
        rs.cameras.quick_reply_open = true;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (nwid == WidgetId::NOTIF_UNLOCK_BTN) {
        gate_or_dispatch_doorbell_unlock(now_touch, false);
        return;
      }
      if (nwid == WidgetId::NOTIF_EXPAND_BTN || nwid == WidgetId::NOTIF_VIEW_BTN ||
          nwid == WidgetId::NOTIF_OPEN_CAM_BTN) {
        const std::string focus_ent = rs.alert.camera_entity;
        rs.alert.active = false;
        rs.cameras.doorbell_live_popup_active = false;
        rs.nav.shortcut_origin = ShortcutOrigin::NONE;
        enter_cameras_app(CamerasTab::SINGLE, focus_ent, false, rs, rt_, now_touch);
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (nwid == WidgetId::NOTIF_DISMISS_BTN) {
        rs.cameras.doorbell_live_popup_active = false;
        if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
          intercom_session().stop_call_with_hw(now_touch);
        }
        rt_.cmd(UiHostCmd::STOP_CAMERA_STREAM);
        rt_.cmd(UiHostCmd::DISMISS_NOTIFICATION);
        return;
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
      enter_cameras_app(CamerasTab::SINGLE, focus_ent, want_listen, rs, rt_, now_touch);
      mark_dirty_full();
      rt_.schedule_disp();
      return;
    }
    rs.cameras.doorbell_live_popup_active = false;
    rt_.cmd(UiHostCmd::STOP_CAMERA_STREAM);
    rt_.cmd(UiHostCmd::DISMISS_NOTIFICATION);
    return;
  }

  // Priority 5: Shortcut Entity Confirmation Modal (resolves on finger lift in on_touch_up)
  if (shortcut_confirm_state().open) {
    gest.begin(touch.x, touch.y, now_touch, rs.nav.gui_page, compute_touch_context(rs));
    return;
  }

  // ----------------------------------------------------------------------------
  // 2. Active Page Touch-Down Initialization & Immediate Hit-Table Controls
  // ----------------------------------------------------------------------------
  gest.begin(touch.x, touch.y, now_touch, rs.nav.gui_page, compute_touch_context(rs));

  const auto &ly = layout();
  const HitRegion *hit = frame_hits().hit_test(touch.x, touch.y, kHitTap);
  const uint16_t wid = (hit != nullptr) ? hit->widget_id : WidgetId::NONE;
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  // Whenever an explicit < Back tap is consumed on touch-down, terminate the gesture
  // immediately so releasing the finger never fires a secondary action on the parent screen.
  if (wid == WidgetId::BACK_BUTTON) {
    gest.active = false;
  }

  music_vol_dragging_ = false;
  music_scrub_dragging_ = false;
  music_scroll_dragging_ = false;
  music_scroll_last_y_ = touch.y;

  // Universal bottom-edge swipe-up home guard: defer bottom-zone touches to on_touch_move / on_touch_up
  // without swallowing direct taps on an explicit < Back button near the bottom bezel.
  if (can_go_home(rs) && wid != WidgetId::BACK_BUTTON && touch.y >= ly.home_zone_y()) {
    return;
  }

  // Active arcade canvas games (Marbles=1, Archery=2, Bricks=3, Chomp=4, 2048=5, Memory=6)
  if (rs.nav.gui_page == PageId::GAMES && rs.nav.active_game > 0) {
    if (wid == WidgetId::BACK_BUTTON) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.active_game = 0;
        rs.nav.game_btn_pressed = false;
        rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
        if (return_from_shortcut_origin(rs, rt_, now_touch)) {
          return;
        }
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
    if (rs.nav.active_game != 5) {
      rs.nav.marble_touch_x = static_cast<float>(touch.x);
      rs.nav.marble_touch_y = static_cast<float>(touch.y);
      rs.nav.game_btn_pressed = true;
      rt_.schedule_disp();
      return;
    }
    return;  // 2048 (active_game == 5) resolves flick vs tap in on_touch_up
  }

  // Page 0: Standalone Clock Face
  if (rs.nav.gui_page == PageId::CLOCK) {
    if (prefs.home_screen_enabled && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
      rs.nav.gui_page = PageId::HOME;
      rs.nav.home_subpage = 0;
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return;
  }

  // Page 1: Home Dashboard & Dock Settings / Activity Overlay
  if (rs.nav.gui_page == PageId::HOME) {
    if (wake_greeting_latched() || rs.nav.wake_greeting_pending) {
      dismiss_wake_greeting();
      rt_.schedule_disp();
    }

    if (rs.nav.home_subpage == 1) {
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rs.nav.home_subpage = 0;
          rs.nav.home_settings_page = 1;
          if (return_from_shortcut_origin(rs, rt_, now_touch)) {
            return;
          }
          rs.nav.gui_page = PageId::SYSTEM;
          rs.nav.system_menu_mode = 0;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID, tkey)) {
        gest.active = false;
        rs.nav.home_settings_page = (wid == WidgetId::CHEVRON_UP) ? 1 : 2;
        rt_.schedule_disp();
        return;
      }
      return;  // List row taps resolve in on_touch_up
    }

    int act_count = 0;
    const HomeActivityItem *act_items = home_activity_rendered_items(act_count);
    if (act_count <= 0) {
      HomeActivityItem *fallback_items = touch_activity_fallback_scratch();
      const auto &ms_hm = media_state();
      const bool m_play = is_music_playing_effective(
          !ms_hm.is_local, rt_.local_mp_playing, ms_hm.state, now_touch);
      const std::string &m_t = (!ms_hm.is_local || rt_.track_title.empty()) ? ms_hm.title : rt_.track_title;
      const std::string &m_a = (!ms_hm.is_local || rt_.track_artist.empty()) ? ms_hm.artist : rt_.track_artist;
      act_count = collect_home_activities(
          now_touch, prefs.is_24h_format,
          rs.stopwatch.running, rs.stopwatch.start_time_ms, rs.stopwatch.elapsed_ms,
          m_play, m_t, m_a, rs.alarms.snooze_until,
          prefs.alarms[0].hour, prefs.alarms[0].minute, prefs.alarms[0].enabled,
          prefs.alarms[1].hour, prefs.alarms[1].minute, prefs.alarms[1].enabled,
          prefs.alarms[2].hour, prefs.alarms[2].minute, prefs.alarms[2].enabled,
          fallback_items, rt_.time_hour, rt_.time_minute, calendar_state().summary,
          rt_.time_day_of_week);
      act_items = fallback_items;
    }

    if (rs.nav.home_subpage == 2) {
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          rs.nav.home_subpage = 0;
          home_activity_overlay_page() = 0;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::HOME_ACTIVITY_PREV || wid == WidgetId::CHEVRON_UP) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_SCROLL, tkey)) {
          gest.active = false;
          home_activity_overlay_page() = std::max(0, home_activity_overlay_page() - 1);
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::HOME_ACTIVITY_NEXT || wid == WidgetId::CHEVRON_DOWN) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_SCROLL, tkey)) {
          gest.active = false;
          const int max_pg = std::max(0, (act_count - 1) / 5);
          home_activity_overlay_page() = std::min(max_pg, home_activity_overlay_page() + 1);
          rt_.schedule_disp();
        }
        return;
      }
      return;  // Vertical page swipe, slot taps & outside-tap dismiss resolve in on_touch_up
    }

    // Home subpage 0: Full-canvas horizontal Dock swipe (D-049) — all hero & dock taps resolve
    // on verified release (<=10 ms) in on_touch_up so swipes anywhere on Home never trigger taps.
    (void)act_items;
    return;
  }

  // Page 2: Clean-Sheet Music App v3 (8 Peer Mode-Bar Tabs, Deep Drilldowns & Floating Takeovers)
  if (rs.nav.gui_page == PageId::MUSIC) {
    if (rs.music.takeover != MusicTakeover::NONE) {
      rs.music.takeover_interacted_ms = (now_touch == 0U) ? 1U : now_touch;
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          (void)handle_music_back_step(rs, rt_, now_touch);
        }
        return;
      }
      if (rs.music.takeover == MusicTakeover::SCRUB) {
        const HitRegion *s_hit = frame_hits().hit_test(touch.x, touch.y, kHitHorizSlider);
        if (s_hit != nullptr && s_hit->widget_id == WidgetId::MUSIC_SCRUB_BAR) {
          music_scrub_dragging_ = true;
        }
      }
      return;  // Floating takeover controls & outside-backdrop dismiss resolve on_touch_move / on_touch_up
    }

    if (rs.music.view == MusicView::VOLUME && rs.music.drilldown == MusicDrilldown::NONE) {
      const HitRegion *v_hit = frame_hits().hit_test(touch.x, touch.y, kHitVertSlider);
      if (v_hit != nullptr && v_hit->widget_id == WidgetId::MUSIC_VOL_SLIDER) {
        music_vol_dragging_ = true;
        rs.music.hud_timer = now_touch;
      }
      return;
    }

    if (rs.music.view == MusicView::NOW_PLAYING && rs.music.drilldown == MusicDrilldown::NONE) {
      // Keep gest.active = true even when HUD is auto-hidden so horizontal canvas swipes
      // step peer tabs (0 -> 1 / 0 -> 7) immediately; wake taps resolve cleanly in on_touch_up.
      return;
    }

    if (wid == WidgetId::BACK_BUTTON) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        (void)handle_music_back_step(rs, rt_, now_touch);
      }
      return;
    }

    if (hit != nullptr && (hit->flags & kHitLongPress) != 0U) {
      gest.arm_long_press(wid, hit->param);
      schedule_core_timeout(kTimeoutTouchLongPressId, kinematics::kLongPressHoldMs, []() {
        touch_engine().tick(esphome::millis());
      });
      return;  // Short tap (< 480 ms) resolves on_touch_up; 480 ms hold opens ACTION_SHEET in tick()
    }
    return;
  }

  // Page 3: Games Launcher (active_game == 0; carousel swipes & Play/Sound taps resolve on_touch_up)
  if (rs.nav.gui_page == PageId::GAMES) {
    return;
  }

  // Page 4: System Settings & Modals
  if (rs.nav.gui_page == PageId::SYSTEM) {
    if (rs.nav.system_menu_mode == 1 || rs.nav.system_menu_mode == 2) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = 0;
        rs.nav.menu_scroll_active = false;
        if (return_from_shortcut_origin(rs, rt_, now_touch)) return;
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;  // Scrollable list rows resolve in on_touch_up
    }

    if (rs.nav.system_menu_mode == 3) {
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rs.nav.system_menu_mode = 0;
          if (return_from_shortcut_origin(rs, rt_, now_touch)) return;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::SYS_SECONDARY_BTN) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rs.nav.system_menu_mode = 4;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const int slot = hit->param;
          if (slot == 0) {
            rt_.cmd(UiHostCmd::TRIGGER_SCREENSAVER_NOW);
          } else if (slot == 1) {
#if LATTER_HAS_BATTERY
            rt_.cmd(UiHostCmd::START_HIBERNATION_COUNTDOWN);
#else
            rt_.cmd(UiHostCmd::TURN_SCREEN_OFF);
#endif
          } else if (slot == 2) {
            rs.nav.system_menu_mode = 5;
            mark_dirty_full();
            rt_.schedule_disp();
          } else if (slot == 3) {
            rt_.sw_toggle(UiSwitchId::BLE_AREA_BEACON);
            rt_.schedule_disp();
          }
        }
      }
      return;
    }

    if (rs.nav.system_menu_mode == 4) {
      return;  // Confirm modal resolves in on_touch_up so finger is off glass before restart
    }

    if (rs.nav.system_menu_mode == 5) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.update_check_state = 0;
        rs.nav.system_menu_mode = 0;
        if (return_from_shortcut_origin(rs, rt_, now_touch)) return;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::SYS_PRIMARY_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        const bool has_update = is_newer_semver(
            rt_.fw_latest_version, (rt_.fw_version != nullptr) ? rt_.fw_version : "0.0.0");
        if (has_update) {
          rs.nav.system_menu_mode = 6;
          mark_dirty_full();
          rt_.schedule_disp();
        } else if (rs.nav.update_check_state == 0) {
          rs.nav.update_check_state = 1;
          rs.nav.update_check_timer = now_touch;
          rt_.cmd(UiHostCmd::CHECK_FIRMWARE_UPDATE);
          rt_.schedule_disp();
        }
      }
      return;
    }

    if (rs.nav.system_menu_mode == 6) {
      return;  // Confirm modal resolves in on_touch_up
    }

    if (rs.nav.system_menu_mode == 7) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = 0;
        if (return_from_shortcut_origin(rs, rt_, now_touch)) return;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::SYS_VOL_SLIDER) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
          const float new_vol = step_wrap_range(rt_.local_mp_volume, 0.10f, 0.10f, 1.00f);
          rt_.set_num_val(UiNumberId::SPEAKER_VOLUME, std::round(new_vol * 100.0f));
          rt_.schedule_disp();
        }
        return;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
        const int slot = hit->param;
        if (slot == 1 && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
          const float cur_a_f = rt_.num_val(UiNumberId::ALERT_VOLUME);
          const int cur_a = (std::isnan(cur_a_f) || cur_a_f < 20.0f) ? 70 : static_cast<int>(std::round(cur_a_f));
          const int nxt_a = static_cast<int>(step_wrap_range(static_cast<float>(cur_a), 10.0f, 50.0f, 100.0f));
          rt_.set_num_val(UiNumberId::ALERT_VOLUME, static_cast<float>(nxt_a));
          rt_.schedule_disp();
        } else if (slot == 2 && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const int cur_sz = static_cast<int>(std::round(rt_.num_val(UiNumberId::VOLUME_STEP_SIZE)));
          const int nxt_sz = (cur_sz < 5) ? 5 : ((cur_sz < 10) ? 10 : 2);
          rt_.set_num_val(UiNumberId::VOLUME_STEP_SIZE, static_cast<float>(nxt_sz));
          rt_.schedule_disp();
        } else if (slot == 3 && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rt_.sw_toggle(UiSwitchId::GAME_SOUND);
          rt_.schedule_disp();
        }
      }
      return;
    }

    if (rs.nav.system_menu_mode == 8) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = 0;
        if (return_from_shortcut_origin(rs, rt_, now_touch)) return;
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }

    if (rs.nav.system_menu_mode == 9) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (rs.nav.audio_menu_parent_page == PageId::MUSIC) {
          rs.nav.gui_page = PageId::MUSIC;
          reset_music_to_now_playing(rs, now_touch, true);
          rs.nav.audio_menu_parent_page = PageId::SYSTEM;
          rs.nav.system_menu_mode = 0;
          const auto &ms = media_state();
          if (rt_.select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE) != "None" &&
              (!ms.is_local || rt_.album_art_w == 0) &&
              !ms.art_url.empty() && ms.art_url != rs.music.loaded_art_url) {
            rt_.cmd(UiHostCmd::FETCH_REMOTE_ART_DEBOUNCED);
          }
        } else {
          rs.nav.system_menu_mode = 0;
          if (return_from_shortcut_origin(rs, rt_, now_touch)) return;
        }
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const int slot = hit->param;
          if (slot == 0) {
            rt_.set_select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE),
                                            {"Full Screen", "Letterbox", "Standard", "None"}));
          } else if (slot == 1) {
            rt_.set_select_opt(UiSelectId::MUSIC_OVERLAY_STYLE,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_OVERLAY_STYLE),
                                            {"Art Only", "Art + Metadata", "Always Visible"}));
          } else if (slot == 2) {
            rt_.set_select_opt(UiSelectId::MUSIC_ALBUM_SORT_ORDER,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_ALBUM_SORT_ORDER),
                                            {"Alphabetical", "Newest on Top", "Oldest on Top"}));
          } else if (slot == 3) {
            rt_.set_select_opt(UiSelectId::MUSIC_ARTIST_ALBUM_SORT_ORDER,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_ARTIST_ALBUM_SORT_ORDER),
                                            {"Oldest on Top", "Newest on Top", "Alphabetical"}));
          } else if (slot == 4) {
            rt_.set_select_opt(UiSelectId::MUSIC_PODCAST_SORT_ORDER,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_PODCAST_SORT_ORDER),
                                            {"Newest on Top", "Oldest on Top"}));
          }
          rt_.schedule_disp();
        }
      }
      return;
    }

    if (rs.nav.system_menu_mode == 11) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = 0;
        if (return_from_shortcut_origin(rs, rt_, now_touch)) return;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const int slot = hit->param;
          if (slot == 0) {
            rt_.set_select_opt(UiSelectId::VA_WAKE_WORD_MODE,
                               cycle_option(rt_.select_opt(UiSelectId::VA_WAKE_WORD_MODE),
                                            {"Docked Only", "Always On", "Disabled"}));
            rt_.schedule_disp();
          } else if (slot == 1) {
            if (rt_.select_opt(UiSelectId::VA_WAKE_WORD_MODE) != "Disabled") {
              rt_.cmd(UiHostCmd::CYCLE_PRIMARY_WAKE_WORD);
              rt_.schedule_disp();
            }
          } else if (slot == 2) {
            if (rt_.select_opt(UiSelectId::VA_WAKE_WORD_MODE) != "Disabled") {
              rs.nav.system_menu_mode = 12;
              mark_dirty_full();
              rt_.schedule_disp();
            }
          } else if (slot == 3) {
            rt_.set_select_opt(UiSelectId::VA_VISUAL_PROFILE,
                               cycle_option(rt_.select_opt(UiSelectId::VA_VISUAL_PROFILE),
                                            {"Nebula", "Solaris", "Rings", "Light Bar", "Dot Wave"}));
            rt_.schedule_disp();
          } else if (slot == 4) {
            rt_.sw_toggle(UiSwitchId::VA_WAKE_SCREEN);
            rt_.schedule_disp();
          }
        }
      }
      return;
    }

    if (rs.nav.system_menu_mode == 12) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = 11;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const int slot = std::clamp(static_cast<int>(hit->param), 0, 4);
          static constexpr UiSelectId kSensIds[5] = {
              UiSelectId::WW_SENS_OKAY_NABU,
              UiSelectId::WW_SENS_HEY_LUNA,
              UiSelectId::WW_SENS_HEY_JARVIS,
              UiSelectId::WW_SENS_HEY_MYCROFT,
              UiSelectId::WW_SENS_ALEXA,
          };
          rt_.set_select_opt(kSensIds[slot],
                             cycle_option(rt_.select_opt(kSensIds[slot]),
                                          {"Slightly sensitive", "Moderately sensitive", "Very sensitive"}));
          rt_.schedule_disp();
        }
      }
      return;
    }

    if (rs.nav.system_menu_mode == 13) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = 2;
        rt_.cmd(UiHostCmd::APPLY_THEME_PRESET);
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::COLOR_WHEEL_SAVE && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        save_persisted_prefs();
        rt_.set_select_opt(UiSelectId::THEME_PALETTE, "Custom");
        do_save_custom_color_to_latter();
        rs.nav.system_menu_mode = 2;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::COLOR_WHEEL_RING) {
        float hue = 0.0f, sat = 1.0f;
        if (ui::hsv_wheel_pick(touch.x, touch.y, SCREEN_CX, scale_y(180), 156, 56, hue, sat)) {
          color_wheel_hue_deg() = hue;
          color_wheel_sat() = sat;
          const esphome::Color c = hsv_to_rgb(hue / 360.0f, sat, 1.0f);
          prefs.custom_theme_rgb = (static_cast<uint32_t>(c.r) << 16) |
                                   (static_cast<uint32_t>(c.g) << 8) |
                                   static_cast<uint32_t>(c.b);
          if (rt_.theme_primary != nullptr && rt_.theme_secondary != nullptr) {
            apply_custom_rgb_harmony(prefs.custom_theme_rgb, *rt_.theme_primary, *rt_.theme_secondary);
          }
          rt_.schedule_disp();
        }
      }
      return;
    }
    return;
  }

  // Page 5: Weather & Sky (mode bar & horizontal swipes resolve in on_touch_move / on_touch_up)
  if (rs.nav.gui_page == PageId::WEATHER) {
    return;
  }

  // Page 6: Area Control
  if (rs.nav.gui_page == PageId::AREA) {
    const bool in_area_picker = (rs.nav.area_subpage == AreaSubpage::PICKER ||
                                 rs.area.browse_mode == "areas" ||
                                 rs.area.browse_mode == "area_areas");
    if ((rs.nav.area_subpage > 0 || in_area_picker) && wid == WidgetId::BACK_BUTTON) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (in_area_picker && rs.area.active_area_id.empty()) {
          if (!prefs.home_screen_enabled && is_on_anchor_root(rs, prefs)) return;
          rs.nav.area_subpage = AreaSubpage::OVERVIEW;
          if (rs.area.browse_mode == "areas" || rs.area.browse_mode == "area_areas") {
            rs.area.browse_mode = "entities";
          }
          navigate_home(now_touch);
          return;
        }
        rs.nav.area_subpage = AreaSubpage::OVERVIEW;
        if (rs.area.browse_mode == "areas" || rs.area.browse_mode == "area_areas") {
          rs.area.browse_mode = "entities";
        }
        if (in_area_picker && return_from_shortcut_origin(rs, rt_, now_touch)) {
          return;
        }
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
    if (wid == WidgetId::AREA_PICKER_PILL) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (rs.nav.area_subpage == AreaSubpage::PICKER) {
          if (rs.area.active_area_id.empty()) {
            if (!prefs.home_screen_enabled && is_on_anchor_root(rs, prefs)) return;
            rs.nav.area_subpage = AreaSubpage::OVERVIEW;
            navigate_home(now_touch);
            return;
          }
          rs.nav.area_subpage = AreaSubpage::OVERVIEW;
        } else {
          rs.nav.area_subpage = AreaSubpage::PICKER;
          rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST) = 1;
          auto *aslots = rs.area_list_slots();
          if (aslots[0].name.empty()) {
            aslots[0].name = "Loading areas...";
            aslots[0].id.clear();
          }
          do_fetch_area_list_browse();
        }
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
    if (rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
      const std::string &idom = light_insp_domain();
      if (idom == "climate") {
        return;  // Climate stepper & HVAC mode pills resolve cleanly in on_touch_up
      }
      if (idom == "cover" || idom == "fan") {
        if (wid >= WidgetId::ENTITY_PRESET_BASE && wid < WidgetId::ENTITY_PRESET_BASE + 4 &&
            touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID, tkey)) {
          gest.active = false;
          const int pct = std::clamp(static_cast<int>(hit->param), 0, 100);
          light_insp_bri_pct() = pct;
          light_insp_active() = (pct > 0);
          const int sl_i = light_insp_slot();
          if (sl_i >= 0 && sl_i < 3) {
            rs.area_entity_slots()[sl_i].level = pct;
            rs.area_entity_slots()[sl_i].active = (pct > 0);
          }
          rs.area.target_entity_id = light_insp_entity_id();
          if (pct == 0) {
            rs.area.target_action = (idom == "cover") ? "close_cover" : "turn_off";
            rs.area.target_level = 0;
          } else if (pct >= 100 && idom == "cover") {
            rs.area.target_action = "open_cover";
            rs.area.target_level = 100;
          } else {
            rs.area.target_action = "set_level";
            rs.area.target_level = pct;
          }
          rs.area.feedback_timer = now_touch;
          do_execute_area_action();
          rt_.schedule_disp();
          return;
        }
        if (wid == WidgetId::LIGHT_BRI_SLIDER) {
          const float f = hit->norm_x(touch.x);
          const int raw_b = static_cast<int>(f * 100.0f);
          const int min_b = (idom == "cover") ? 0 : 5;
          light_insp_bri_pct() = std::clamp(((raw_b + 2) / 5) * 5, min_b, 100);
          light_insp_active() = (light_insp_bri_pct() > 0);
          rt_.schedule_disp();
          return;
        }
        return;
      }
      if (wid == WidgetId::LIGHT_COLOR_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        gest.active = false;
        light_insp_mode() = (light_insp_mode() == 0) ? 1 : 0;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::COLOR_WHEEL_RING) {
        float hue = 0.0f, sat = 1.0f;
        if (ui::hsv_wheel_pick(touch.x, touch.y, SCREEN_CX, scale_y(212), 140, 62, hue, sat)) {
          light_insp_hue_deg() = hue;
          light_insp_sat() = sat;
          update_light_inspector_hex(hue, sat);
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::LIGHT_TEMP_SLIDER) {
        const float f = hit->norm_x(touch.x);
        const int raw_k = 2000 + static_cast<int>(f * 4500.0f);
        light_insp_kelvin() = std::clamp(((raw_k + 50) / 100) * 100, 2000, 6500);
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::LIGHT_BRI_SLIDER) {
        const float f = hit->norm_x(touch.x);
        const int raw_b = static_cast<int>(f * 100.0f);
        light_insp_bri_pct() = std::clamp(((raw_b + 2) / 5) * 5, 5, 100);
        rt_.schedule_disp();
        return;
      }
    }
    return;
  }

  // Page 7: Universal TV Remote
  if (rs.nav.gui_page == PageId::REMOTE) {
    if (rs.nav.remote_subpage == 1) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.remote.action_feedback.clear();
        rs.nav.remote_subpage = 0;
        if (return_from_shortcut_origin(rs, rt_, now_touch)) {
          return;
        }
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID, tkey)) {
        gest.active = false;
        int valid_slots[MAX_REMOTE_SLOTS] = {0};
        const int valid_count = collect_configured_remote_slots(valid_slots);
        const int total_pages = std::max(1, (valid_count + 3) / 4);
        const int next_pg = std::clamp(
            remote_control_page() + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 0, total_pages - 1);
        if (next_pg != remote_control_page()) {
          remote_control_page() = next_pg;
          rt_.schedule_disp();
        }
      }
      return;  // Slot cards & power buttons resolve in on_touch_up
    }

    const int act_slot = resolve_active_remote_slot(rs.remote.active_slot);
    rs.remote.active_slot = act_slot;

    if (wid == WidgetId::REMOTE_SLOT_PILL) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        gest.active = false;
        rs.remote.action_feedback.clear();
        rs.nav.remote_subpage = 1;
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }

    if (hit != nullptr && (hit->flags & kHitLongPress) != 0U) {
      gest.arm_long_press(wid, hit->param);
      schedule_core_timeout(kTimeoutTouchLongPressId, kinematics::kLongPressHoldMs, []() {
        touch_engine().tick(esphome::millis());
      });
      rt_.schedule_band_disp(std::max(0, static_cast<int>(hit->y0) - 6),
                             std::min(SCREEN_H - 1, static_cast<int>(hit->y1) + 6));
      return;  // Short tap (< 480 ms) resolves on_touch_up; hold (>= 480 ms) fires in tick()
    }

    uint16_t eff_wid = wid;
    uint32_t eff_tkey = tkey;
    if (eff_wid == WidgetId::NONE) {
      const RemoteDpadGeom dp = compute_remote_dpad_geom(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0);
      eff_wid = resolve_remote_dpad_widget(touch.x, touch.y, dp);
      if (eff_wid != WidgetId::NONE) {
        eff_tkey = make_tap_target_key(rs.nav.gui_page, eff_wid, 0);
      }
    }

    const int remote_key = remote_key_for_widget(eff_wid);
    if (remote_key >= 0 && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID, eff_tkey)) {
      gest.active = false;
      do_remote_send_key(remote_key, act_slot, false, now_touch);
      rt_.schedule_band_disp(0, std::min(SCREEN_H - 1, layout().hdr_y + scale_y(24)));
    }
    return;
  }

  // Page 8: Tools & Communications
  if (rs.nav.gui_page == PageId::TOOLS) {
    const int sub = rs.nav.tools_subpage;
    if (sub == ToolsSubpage::LAUNCHER) {
      return;  // Grid cards & vertical page swipes resolve in on_touch_up
    }

#if LATTER_ROUND_DISPLAY
    if (sub == ToolsSubpage::STOPWATCH) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.tools_subpage = 0;
        if (return_from_shortcut_origin(rs, rt_, now_touch)) {
          return;
        }
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::SW_PRIMARY_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (!rs.stopwatch.running) {
          rs.stopwatch.running = true;
          rs.stopwatch.started_in_analog = true;
          rs.stopwatch.start_time_ms = now_touch - rs.stopwatch.elapsed_ms;
          rs.nav.sleep_start_time = 0;
        } else {
          rs.stopwatch.running = false;
          rs.stopwatch.elapsed_ms = now_touch - rs.stopwatch.start_time_ms;
        }
        latter_actions::check_and_emit_timer_alarm_state(now_touch);
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::SW_LAP_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (rs.stopwatch.running) {
          record_stopwatch_lap(rs.stopwatch, now_touch - rs.stopwatch.start_time_ms);
        } else if (rs.stopwatch.elapsed_ms > 0) {
          rs.stopwatch.lap_ms = rs.stopwatch.elapsed_ms;
        }
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::SW_RESET_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.stopwatch.reset();
        latter_actions::check_and_emit_timer_alarm_state(now_touch);
        rt_.schedule_disp();
      }
      return;
    }
#endif

    if (sub == ToolsSubpage::DICE) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.tools_subpage = 0;
        rt_.apply_power_policy();
        if (return_from_shortcut_origin(rs, rt_, now_touch)) {
          return;
        }
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::DICE_ROLL_BTN && !dice_state().anim_active &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        trigger_dice_roll(now_touch, next_rng_u32(), next_rng_u32());
        rt_.schedule_disp();
      }
      return;
    }

    if (sub == ToolsSubpage::FLASHLIGHT) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.tools_subpage = 0;
        rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
        if (return_from_shortcut_origin(rs, rt_, now_touch)) {
          return;
        }
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }

    if (sub == ToolsSubpage::CALCULATOR) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.tools_subpage = 0;
        reset_calculator_state();
        if (return_from_shortcut_origin(rs, rt_, now_touch)) {
          return;
        }
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid >= WidgetId::CALC_KEY_BASE && wid < WidgetId::CALC_KEY_BASE + 20) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
          handle_calculator_keypad_tap(touch.x, touch.y);
          rt_.schedule_disp();
        }
      }
      return;
    }

    if (sub == ToolsSubpage::LEVEL) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.tools_subpage = 0;
        rt_.apply_power_policy();
        if (return_from_shortcut_origin(rs, rt_, now_touch)) {
          return;
        }
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }

    if (sub == ToolsSubpage::COIN_FLIP) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.tools_subpage = 0;
        rt_.apply_power_policy();
        if (return_from_shortcut_origin(rs, rt_, now_touch)) {
          return;
        }
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::COIN_FLIP_BTN && !coin_state().anim_active &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        trigger_coin_flip(now_touch, next_rng_u32());
        rt_.schedule_disp();
      }
      return;
    }
  }

  // Page 10: Consolidated 4-Tab Time App (Clock, Timers, Alarms, Digital Stopwatch)
  if (rs.nav.gui_page == PageId::TIME) {
    const int sub = std::clamp(rs.nav.time_subpage, 0, 3);

    // Drilldown 1: Single-Timer Detail View (Tab 1, view_slot == 1..3)
    if (sub == TimeSubpage::TIMERS && timer_view_slot() > 0) {
      int &view_idx = timer_view_slot();
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        gest.active = false;
        view_idx = 0;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      const int s_idx = std::clamp(view_idx - 1, 0, 2);
      auto &s = timer_slots()[s_idx];
      if (wid == WidgetId::TIMER_PRIMARY_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        gest.active = false;
        toggle_timer_slot_run(s, now_touch, rt_);
        return;
      }
      if (wid >= WidgetId::STEPPER_BASE && wid < WidgetId::STEPPER_BASE + 4) {
        gest.active = false;
        if (!s.running && !s.paused && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
          const int col = static_cast<int>(wid - WidgetId::STEPPER_BASE);
          if (col == 0) s.duration_sec = std::clamp(s.duration_sec - 300, 60, 3600);
          else if (col == 1) s.duration_sec = std::clamp(s.duration_sec - 60, 60, 3600);
          else if (col == 2) s.duration_sec = std::clamp(s.duration_sec + 60, 60, 3600);
          else if (col == 3) s.duration_sec = std::clamp(s.duration_sec + 300, 60, 3600);
          s.remaining_sec = s.duration_sec;
          sync_multi_timer_state();
          rt_.apply_power_policy();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::TIMER_PLUS_1M_BTN && (s.running || s.paused) &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
        gest.active = false;
        if (s.running) {
          const int cur_rem = (s.target_ms > now_touch)
                                  ? static_cast<int>((s.target_ms - now_touch + 999U) / 1000U)
                                  : 0;
          const int next_rem = std::min(36000, cur_rem + 60);
          s.remaining_sec = next_rem;
          s.target_ms = now_touch + static_cast<uint32_t>(next_rem) * 1000U;
        } else {
          s.remaining_sec = std::min(36000, s.remaining_sec + 60);
        }
        s.duration_sec = std::max(s.duration_sec, s.remaining_sec);
        sync_multi_timer_state();
        rt_.apply_power_policy();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::TIMER_RESET_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
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
        rt_.apply_power_policy();
        rt_.schedule_disp();
      }
      return;
    }

    // Drilldown 3: 1-Minute Precision Alarm Editor (Tab 2, edit_idx == 1..3)
    if (sub == TimeSubpage::ALARMS && rs.alarms.edit_idx > 0) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        gest.active = false;
        rs.alarms.edit_idx = 0;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::ALARM_HH_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
        gest.active = false;
        rs.nav.alarm_edit_field = 0;
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::ALARM_MM_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
        gest.active = false;
        rs.nav.alarm_edit_field = 1;
        rt_.schedule_disp();
        return;
      }
      const int a_idx = std::clamp(rs.alarms.edit_idx - 1, 0, 2);
      auto &ap = prefs.alarms[a_idx];
      if (wid >= WidgetId::STEPPER_BASE && wid < WidgetId::STEPPER_BASE + 4) {
        gest.active = false;
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
          const int col = static_cast<int>(wid - WidgetId::STEPPER_BASE);
          if (rs.nav.alarm_edit_field == 0) {
            static constexpr int kHSteps[4] = {-6, -1, 1, 6};
            ap.hour = static_cast<uint8_t>((static_cast<int>(ap.hour) + 24 + kHSteps[col]) % 24);
          } else {
            static constexpr int kMSteps[4] = {-5, -1, 1, 5};
            ap.minute = static_cast<uint8_t>((static_cast<int>(ap.minute) + 60 + kMSteps[col]) % 60);
          }
          latter_actions::sync_local_alarms_edit(now_touch);
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::ALARM_RECUR_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        gest.active = false;
        ap.recurrence = static_cast<uint8_t>((ap.recurrence + 1) % 4);
        latter_actions::sync_local_alarms_edit(now_touch);
        rt_.schedule_disp();
      }
      return;
    }

    // Zero-latency Stopwatch split/start/pause buttons on Tab 3
    if (sub == TimeSubpage::STOPWATCH) {
      if (wid == WidgetId::SW_PRIMARY_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        gest.active = false;
        if (!rs.stopwatch.running) {
          rs.stopwatch.running = true;
          rs.stopwatch.started_in_analog = false;
          rs.stopwatch.start_time_ms = now_touch - rs.stopwatch.elapsed_ms;
          rs.nav.sleep_start_time = 0;
        } else {
          rs.stopwatch.running = false;
          rs.stopwatch.elapsed_ms = now_touch - rs.stopwatch.start_time_ms;
        }
        latter_actions::check_and_emit_timer_alarm_state(now_touch);
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::SW_LAP_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
        gest.active = false;
        if (rs.stopwatch.running) {
          record_stopwatch_lap(rs.stopwatch, now_touch - rs.stopwatch.start_time_ms);
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::SW_RESET_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        gest.active = false;
        rs.stopwatch.reset();
        latter_actions::check_and_emit_timer_alarm_state(now_touch);
        rt_.schedule_disp();
        return;
      }
    }

    return;  // Mode bar, horizontal tab swipes, and overview card taps resolve in on_touch_move / on_touch_up
  }

  // Page 9: Home Hub (5-tab whole-home controls resolve in on_touch_move / on_touch_up)
  if (rs.nav.gui_page == PageId::HOME_HUB) {
    return;
  }

  // Page 13: Dedicated 4-Tab Cameras App (Quick-Reply < Back & Live PTT Banner Stop)
  if (rs.nav.gui_page == PageId::CAMERAS) {
    if (rs.voice.intercom_turn && wid == WidgetId::CAM_PTT_BANNER_STOP &&
        touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
      gest.active = false;
      rt_.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
      intercom_session().resume_listen(now_touch);
      rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      rt_.apply_power_policy();
      mark_dirty_full();
      rt_.schedule_disp();
      return;
    }
    if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
      gest.active = false;
      if (rs.cameras.quick_reply_open) {
        rs.cameras.quick_reply_open = false;
        rs.cameras.hud_active = true;
        rs.cameras.hud_timer = now_touch;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (return_from_shortcut_origin(rs, rt_, now_touch)) {
        return;
      }
    }
    return;
  }

  // Page 11: Pinned App (2x3 grid cards & vertical page swipes resolve in on_touch_move / on_touch_up)
  if (rs.nav.gui_page == PageId::PINNED) {
    return;
  }

  // Page 12: Dedicated 4-Tab Calendar App (Overview, Day, Week, Month + Event Details/Map/Composer)
  if (rs.nav.gui_page == PageId::CALENDAR) {
    const int32_t today_ymd = resolve_touch_today_ymd(rt_);
    if (rs.cal.selected_ymd <= 0) rs.cal.selected_ymd = today_ymd;
    if (rs.cal.draft_ymd <= 0) rs.cal.draft_ymd = rs.cal.selected_ymd;

    if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
      gest.active = false;
      if (rs.cal.delete_confirm_open) {
        rs.cal.delete_confirm_open = false;
      } else if (rs.cal.drilldown == CalDrilldown::MAP) {
        rs.cal.drilldown = CalDrilldown::DETAIL;
        rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
        rs.alert.loaded_camera_url.clear();
        rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
      } else if (rs.cal.drilldown == CalDrilldown::DETAIL) {
        rs.cal.drilldown = CalDrilldown::NONE;
      } else if (rs.cal.drilldown >= CalDrilldown::PICK_TITLE &&
                 rs.cal.drilldown <= CalDrilldown::PICK_REPEAT) {
        rs.cal.drilldown = CalDrilldown::ADD_STEP1;
      } else if (rs.cal.drilldown == CalDrilldown::ADD_STEP2) {
        rs.cal.drilldown = CalDrilldown::ADD_STEP1;
      } else if (rs.cal.drilldown == CalDrilldown::ADD_STEP1) {
        stop_calendar_dictation_if_active(rs, rt_, now_touch);
        rs.cal.drilldown = CalDrilldown::NONE;
      } else if (return_from_shortcut_origin(rs, rt_, now_touch)) {
        return;
      }
      mark_dirty_full();
      rt_.schedule_disp();
      return;
    }

    // Immediate rapid period stepping (< / >) across Overview / Day / Week / Month
    if (rs.cal.drilldown == CalDrilldown::NONE &&
        (wid == WidgetId::CAL_NAV_PREV || wid == WidgetId::CAL_NAV_NEXT) &&
        touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID, tkey)) {
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
          if (latter_actions::record_local_pref_edit(PrefKey::CALENDAR_LOOKAHEAD_DAYS, d_str, now_touch, eff)) {
            device_prefs_state().last_effects = eff;
            rt_.cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
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
      rt_.schedule_disp();
      return;
    }

    // Immediate rapid date & time stepping inside + Add Event Step 2
    if (rs.cal.drilldown == CalDrilldown::ADD_STEP2) {
      if ((wid == WidgetId::CAL_ADD_DATE_PREV || wid == WidgetId::CAL_ADD_DATE_NEXT) &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID, tkey)) {
        gest.active = false;
        rs.cal.draft_ymd = add_days_ymd(rs.cal.draft_ymd,
                                        (wid == WidgetId::CAL_ADD_DATE_NEXT) ? 1 : -1);
        rt_.schedule_disp();
        return;
      }
      if (!rs.cal.draft_all_day &&
          wid >= WidgetId::STEPPER_BASE && wid < WidgetId::STEPPER_BASE + 4 &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID, tkey)) {
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
        rt_.schedule_disp();
        return;
      }
    }
    return;
  }
}

bool TouchGestureEngine::try_commit_kinematic_swipe_(bool is_release, uint32_t now_ms) {
  auto &gest = touch_gesture();
  if (!gest.active || gest.swipe_committed || gest.axis_lock == GestureAxisLock::Slider) {
    return gest.swipe_committed;
  }
  if (shortcut_confirm_state().open) {
    return false;
  }
  if (!is_release && (now_ms - gest.last_swipe_ms < TOUCH_POST_SWIPE_LOCKOUT)) {
    return false;
  }

  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  const auto &ly = layout();
  const int dx = gest.last_x - gest.start_x;
  const int dy = gest.last_y - gest.start_y;
  const uint32_t dur_ms = (now_ms >= gest.start_ms) ? (now_ms - gest.start_ms) : 0U;

  const int v_dir = eval_kinematic_vert_swipe(
      dx, dy, dur_ms, is_release, LATTER_PANEL_PPI, gest.axis_lock);
  const int h_dir = eval_kinematic_horiz_swipe(
      dx, dy, dur_ms, is_release, LATTER_PANEL_PPI, gest.axis_lock);

  auto commit_swipe = [&]() {
    gest.swipe_committed = true;
    gest.tap_cancelled = true;
    gest.last_swipe_ms = now_ms;
  };

  // 1. Universal Bottom-Edge Steep Upward Swipe -> Home ([HOME PREDICATE])
  if (can_go_home(rs) && gest.start_y >= ly.home_zone_y() && v_dir > 0) {
    commit_swipe();
    navigate_home(now_ms);
    return true;
  }

  // 2. Page 1: Home (Full-Canvas App Dock Horizontal Swipe & Active Items Vertical Paging)
  if (rs.nav.gui_page == PageId::HOME) {
    if (rs.nav.home_subpage == 0) {
      if (h_dir != 0) {
        commit_swipe();
        ShortcutItem *dock_items = dock_items_scratch();
        const int dock_cnt = collect_effective_dock_items(dock_items, kMaxEffectiveDockItems);
        const int dock_pages = std::max(1, (dock_cnt + 3) / 4);
        bool changed = false;
        home_dock_page() = step_pager_index(
            home_dock_page(), 0, dock_pages - 1, h_dir, /*wrap=*/false,
            GestureAxisLock::Horizontal, PageId::HOME, now_ms, changed);
        if (changed) {
          rt_.schedule_disp();
        } else {
          rt_.schedule_band_disp(dock_dirty_band_y0(), SCREEN_H - 1);
        }
        return true;
      }
      return false;
    }
    if (rs.nav.home_subpage == 1 && v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
      commit_swipe();
      bool changed = false;
      rs.nav.home_settings_page = step_pager_index(
          std::clamp(rs.nav.home_settings_page, 1, 2), 1, 2, v_dir, /*wrap=*/false,
          GestureAxisLock::Vertical, PageId::HOME, now_ms, changed);
      rt_.schedule_disp();
      return true;
    }
    if (rs.nav.home_subpage == 2 && v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
      commit_swipe();
      int act_cnt = 0;
      (void)home_activity_rendered_items(act_cnt);
      const int max_pg = std::max(0, (act_cnt - 1) / 5);
      bool changed = false;
      home_activity_overlay_page() = step_pager_index(
          home_activity_overlay_page(), 0, max_pg, v_dir, /*wrap=*/false,
          GestureAxisLock::Vertical, PageId::HOME, now_ms, changed);
      rt_.schedule_disp();
      return true;
    }
    return false;
  }

  // 3. Page 2: Clean-Sheet Music App v3 (9-Slot Peer Tab Canvas & Mode-Bar Swipe + List Paging — D-059 / D-060)
  if (rs.nav.gui_page == PageId::MUSIC) {
    if (rs.music.takeover != MusicTakeover::NONE) {
      return false;
    }

    if (rs.music.drilldown == MusicDrilldown::NONE) {
      if (h_dir != 0) {
        commit_swipe();
        rs.music.direct_nav = false;
        activate_music_peer_tab(step_music_peer_tab(rs.music.view, h_dir), now_ms);
        mark_dirty_full();
        rt_.schedule_disp();
        return true;
      }

      if (v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
        if (rs.music.view == MusicView::LIBRARY) {
          commit_swipe();
          music_scroll_tap_lockout_until_ms() = now_ms + 160U;
          bool changed = false;
          rs.music.category_page = step_pager_index(
              std::clamp(rs.music.category_page, 1, 2), 1, 2, v_dir, /*wrap=*/false,
              GestureAxisLock::Vertical, PageId::MUSIC, now_ms, changed);
          rs.music.hud_timer = now_ms;
          rt_.schedule_disp();
          return true;
        }
        if (rs.music.view == MusicView::SETTINGS) {
          commit_swipe();
          music_scroll_tap_lockout_until_ms() = now_ms + 160U;
          bool changed = false;
          rs.music.settings_page = step_pager_index(
              std::clamp(rs.music.settings_page, 1, 2), 1, 2, v_dir, /*wrap=*/false,
              GestureAxisLock::Vertical, PageId::MUSIC, now_ms, changed);
          rs.music.hud_timer = now_ms;
          rt_.schedule_disp();
          return true;
        }
        if (rs.music.view == MusicView::SPEAKERS ||
            rs.music.view == MusicView::QUEUE ||
            rs.music.view == MusicView::ALBUM_TRACKS) {
          commit_swipe();
          music_scroll_tap_lockout_until_ms() = now_ms + 160U;
          int &b_page = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
          const int b_tot = std::max(1, rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC));
          bool changed = false;
          const int next_pg = step_pager_index(
              b_page, 1, b_tot, v_dir, /*wrap=*/false,
              GestureAxisLock::Vertical, PageId::MUSIC, now_ms, changed);
          rs.music.hud_timer = now_ms;
          if (changed) {
            b_page = next_pg;
            last_music_page_req_ms() = now_ms;
            fetch_music_browse_for_current_view(rs, now_ms);
          }
          rt_.schedule_disp();
          return true;
        }
      }
      return false;
    }

    if (rs.music.drilldown == MusicDrilldown::LIB_LIST && v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
      commit_swipe();
      music_scroll_tap_lockout_until_ms() = now_ms + 160U;
      int &b_page = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
      const int b_tot = std::max(1, rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC));
      const bool wrap_list = (rs.music.selected_category_mode == "artist" ||
                              rs.music.selected_category_mode == "album");
      bool changed = false;
      const int next_pg = step_pager_index(
          b_page, 1, b_tot, v_dir, wrap_list,
          GestureAxisLock::Vertical, PageId::MUSIC, now_ms, changed);
      if (changed) {
        b_page = next_pg;
        last_music_page_req_ms() = now_ms;
        fetch_music_browse_for_current_view(rs, now_ms);
      }
      rt_.schedule_disp();
      return true;
    }
    return false;
  }

  // 4. Page 3: Games Carousel & 2048 Board Flicks
  if (rs.nav.gui_page == PageId::GAMES) {
    if (rs.nav.active_game == 0 && !rs.nav.menu_scroll_active && h_dir != 0) {
      commit_swipe();
      const int game_cnt = active_games_launcher_count(device_prefs());
      if (game_cnt > 1) {
        const int cur_slot = std::clamp(rs.nav.game_carousel_idx, 0, game_cnt - 1);
        rs.nav.game_carousel_idx = cur_slot;
        rs.nav.menu_scroll_active = true;
        rs.nav.menu_scroll_dir = h_dir;
        rs.nav.menu_scroll_target_page = step_wrap(cur_slot, h_dir, game_cnt);
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      } else {
        bool changed = false;
        (void)step_pager_index(0, 0, 0, h_dir, /*wrap=*/false,
                               GestureAxisLock::Horizontal, PageId::GAMES, now_ms, changed);
        rt_.schedule_disp();
      }
      return true;
    }
    if (rs.nav.active_game == 5 && is_release &&
        (std::abs(dx) >= ly.flick_min() || std::abs(dy) >= ly.flick_min())) {
      commit_swipe();
      if (std::abs(dx) > std::abs(dy)) {
        game_2048_pending_dir() = (dx > 0) ? 2 : 4;
      } else {
        game_2048_pending_dir() = (dy > 0) ? 3 : 1;
      }
      rt_.schedule_disp();
      return true;
    }
    return false;
  }

  // 5. Page 4: System Animated 2-Page Vertical Menus (Modes 0, 1, 2)
  if (rs.nav.gui_page == PageId::SYSTEM && !rs.nav.menu_scroll_active &&
      (rs.nav.system_menu_mode == 0 || rs.nav.system_menu_mode == 1 || rs.nav.system_menu_mode == 2) &&
      v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
    commit_swipe();
    const int cur_pg = (rs.nav.system_menu_mode == 0)
                           ? rs.nav.system_dashboard_page
                           : ((rs.nav.system_menu_mode == 1) ? rs.nav.sleep_menu_page
                                                             : rs.nav.display_menu_page);
    bool changed = false;
    const int target_pg = step_pager_index(
        cur_pg, 1, 2, v_dir, /*wrap=*/false,
        GestureAxisLock::Vertical, PageId::SYSTEM, now_ms, changed);
    if (changed) {
      rs.nav.menu_scroll_active = true;
      rs.nav.menu_scroll_dir = (target_pg == 2) ? 1 : -1;
      rs.nav.menu_scroll_target_page = target_pg;
      rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
    } else {
      rt_.schedule_disp();
    }
    return true;
  }

  // 6. Page 5: Weather & Sky (Unified Canvas + Mode Bar Horizontal Wrapping Swipe)
  if (rs.nav.gui_page == PageId::WEATHER && h_dir != 0) {
    commit_swipe();
    rs.nav.sky_weather_subpage = step_wrap(rs.nav.sky_weather_subpage, h_dir, 4);
    mark_dirty_full();
    rt_.schedule_disp();
    return true;
  }

  // 7. Page 6: Area Control (Picker List, Unified Canvas+ModeBar Category Swipe, Full-Canvas Entity Paging)
  if (rs.nav.gui_page == PageId::AREA) {
    if (rs.nav.area_subpage == AreaSubpage::PICKER || rs.area.browse_mode == "areas") {
      if (v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
        commit_swipe();
        int &a_pg = rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST);
        const int a_tot = std::max(1, rs.slot_total_pages_for(latter_rpc::SlotChannel::AREA_LIST));
        bool changed = false;
        const int next_pg = step_pager_index(
            a_pg, 1, a_tot, v_dir, /*wrap=*/false,
            GestureAxisLock::Vertical, PageId::AREA, now_ms, changed);
        if (changed) {
          a_pg = next_pg;
          do_fetch_area_list_browse();
        }
        rt_.schedule_disp();
        return true;
      }
      return false;
    }
    if (rs.nav.area_subpage == AreaSubpage::OVERVIEW) {
      if (h_dir != 0) {
        commit_swipe();
        std::string cats[8];
        const int cat_cnt = collect_area_categories(cats);
        if (cat_cnt > 1) {
          int cur_c = 0;
          for (int i = 0; i < cat_cnt; i++) {
            if (cats[i] == rs.area.domain_filter) {
              cur_c = i;
              break;
            }
          }
          const int next_c = step_wrap(cur_c, h_dir, cat_cnt);
          rs.area.domain_filter = cats[next_c];
          sync_mode_bar_window(area_cat_dock_page(), next_c, cat_cnt);
          rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
          do_fetch_area_browse();
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return true;
      }
      if (v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
        commit_swipe();
        int &pg = rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES);
        const int tot = std::max(1, rs.slot_total_pages_for(latter_rpc::SlotChannel::AREA_ENTITIES));
        bool changed = false;
        const int next_pg = step_pager_index(
            pg, 1, tot, v_dir, /*wrap=*/false,
            GestureAxisLock::Vertical, PageId::AREA, now_ms, changed);
        if (changed) {
          pg = next_pg;
          do_fetch_area_browse();
        }
        rt_.schedule_disp();
        return true;
      }
    }
    return false;
  }

  // 8. Page 7: Remote Slot Selector Vertical List
  if (rs.nav.gui_page == PageId::REMOTE && rs.nav.remote_subpage == 1 &&
      v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
    commit_swipe();
    int valid_slots[MAX_REMOTE_SLOTS] = {0};
    const int valid_count = collect_configured_remote_slots(valid_slots);
    const int total_pages = std::max(1, (valid_count + 3) / 4);
    bool changed = false;
    remote_control_page() = step_pager_index(
        remote_control_page(), 0, total_pages - 1, v_dir, /*wrap=*/false,
        GestureAxisLock::Vertical, PageId::REMOTE, now_ms, changed);
    rt_.schedule_disp();
    return true;
  }

  // 9. Page 8: Tools Launcher Grid Vertical Paging
  if (rs.nav.gui_page == PageId::TOOLS) {
    if (rs.nav.tools_subpage == ToolsSubpage::LAUNCHER && !rs.nav.menu_scroll_active &&
        v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
      commit_swipe();
      const int total_pages = std::max(1, active_tools_launcher_page_count(device_prefs()));
      const int cur_pg = std::clamp(rs.nav.tools_menu_page, 0, total_pages - 1);
      rs.nav.tools_menu_page = cur_pg;
      bool changed = false;
      const int target_pg = step_pager_index(
          cur_pg, 0, total_pages - 1, v_dir, /*wrap=*/false,
          GestureAxisLock::Vertical, PageId::TOOLS, now_ms, changed);
      if (changed) {
        rs.nav.menu_scroll_active = true;
        rs.nav.menu_scroll_dir = (target_pg > cur_pg) ? 1 : -1;
        rs.nav.menu_scroll_target_page = target_pg;
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      } else {
        rt_.schedule_disp();
      }
      return true;
    }
    return false;
  }

  // 10. Page 9: Home Hub (5-Tab Whole-Home Controls)
  if (rs.nav.gui_page == PageId::HOME_HUB) {
    if (home_hub_tab() == HomeHubTab::ENTRY &&
        v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
      commit_swipe();
      const int tot_pg = std::max(1, (home_hub_state().entry_count + 1) / 2);
      bool changed = false;
      home_hub_entry_page() = step_pager_index(
          home_hub_entry_page(), 0, tot_pg - 1, v_dir, /*wrap=*/false,
          GestureAxisLock::Vertical, PageId::HOME_HUB, now_ms, changed);
      rt_.schedule_disp();
      return true;
    }
    if (h_dir != 0) {
      commit_swipe();
      int hh_tabs[kMaxHomeHubTabs]{};
      const int hh_cnt = collect_home_hub_tabs(hh_tabs);
      if (hh_cnt > 1) {
        int cur_slot = 0;
        for (int i = 0; i < hh_cnt; i++) {
          if (hh_tabs[i] == home_hub_tab()) {
            cur_slot = i;
            break;
          }
        }
        const int next_slot = step_wrap(cur_slot, h_dir, hh_cnt);
        enter_home_hub_tab(hh_tabs[next_slot], false, rs, rt_, now_ms);
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return true;
    }
    return false;
  }

  // 10b. Page 13: Dedicated 4-Tab Cameras App (Grid pagination, Single/Carousel camera swipe, Tab swipe)
  if (rs.nav.gui_page == PageId::CAMERAS) {
    if (rs.cameras.quick_reply_open || rs.voice.intercom_turn) return false;
    CameraRecord cams[MAX_CAMERA_SLOTS]{};
    const int c_total = parse_cameras_records(area_ctx_state().cameras_summary, cams, MAX_CAMERA_SLOTS);
    if (rs.cameras.tab == CamerasTab::GRID && v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
      const int total_pg = std::max(1, (c_total + 3) / 4);
      if (total_pg > 1) {
        commit_swipe();
        bool changed = false;
        rs.cameras.grid_page = step_pager_index(
            rs.cameras.grid_page, 0, total_pg - 1, v_dir, /*wrap=*/false,
            GestureAxisLock::Vertical, PageId::CAMERAS, now_ms, changed);
        if (changed) {
          rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
          mark_dirty_full();
        }
        rt_.schedule_disp();
        return true;
      }
      return false;
    }
    if (h_dir != 0) {
      commit_swipe();
      if (rs.cameras.tab == CamerasTab::SINGLE && !rs.cameras.hud_active && c_total > 1) {
        rs.cameras.selected_idx = step_wrap(rs.cameras.selected_idx, h_dir, c_total);
        rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
        if (persisted_prefs().camera_stream_mode == 2U || area_ctx_state().camera_stream_url.empty()) {
          rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
          rs.alert.loaded_camera_url.clear();
          rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
        }
        mark_dirty_full();
        rt_.schedule_disp();
        return true;
      }
      if (rs.cameras.tab == CamerasTab::CAROUSEL && !rs.cameras.hud_active && c_total > 1) {
        int car[MAX_CAMERA_SLOTS]{};
        const int car_cnt = collect_carousel_camera_indices(cams, c_total, car, MAX_CAMERA_SLOTS);
        if (car_cnt > 1) {
          rs.cameras.carousel_idx = step_wrap(rs.cameras.carousel_idx, h_dir, car_cnt);
          rs.cameras.carousel_last_advance_ms = now_ms;
          rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
          mark_dirty_full();
          rt_.schedule_disp();
          return true;
        }
      }
      int next_tab = step_wrap(rs.cameras.tab, h_dir, 4);
      if (next_tab == CamerasTab::GRID && c_total <= 1) {
        next_tab = step_wrap(next_tab, h_dir, 4);
      }
      rs.cameras.tab = next_tab;
      rs.cameras.hud_active = true;
      rs.cameras.hud_timer = now_ms;
      rs.cameras.carousel_last_advance_ms = now_ms;
      if (next_tab == CamerasTab::SETTINGS) {
        rt_.cmd(UiHostCmd::STOP_CAMERA_STREAM);
      } else {
        rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      }
      mark_dirty_full();
      rt_.schedule_disp();
      return true;
    }
    return false;
  }

  // 11. Page 10: Time App (Unified Canvas + Mode Bar 4-Tab Swipe & World Clock Vertical Paging)
  if (rs.nav.gui_page == PageId::TIME) {
    const int sub = std::clamp(rs.nav.time_subpage, 0, 3);
    const bool in_drilldown =
        (sub == TimeSubpage::TIMERS && timer_view_slot() != 0) ||
        (sub == TimeSubpage::ALARMS && rs.alarms.edit_idx != 0);
    if (!in_drilldown && h_dir != 0) {
      commit_swipe();
      rs.nav.time_subpage = step_wrap(sub, h_dir, 4);
      mark_dirty_full();
      rt_.schedule_disp();
      return true;
    }
    if (!in_drilldown && sub == TimeSubpage::CLOCK &&
        v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
      commit_swipe();
      WorldClockEntry wc_entries[kMaxWorldClocks + 1]{};
      const int tot_wc = collect_active_world_clocks(prefs, area_ctx_state(), wc_entries);
      const int tot_pg = std::max(1, (tot_wc + 1) / 2);
      bool changed = false;
      rs.nav.world_clock_page = step_pager_index(
          rs.nav.world_clock_page, 0, tot_pg - 1, v_dir, /*wrap=*/false,
          GestureAxisLock::Vertical, PageId::TIME, now_ms, changed);
      rt_.schedule_disp();
      return true;
    }
    return false;
  }

  // 12. Page 11: Pinned App (Unlimited 6-Button-Per-Page Vertical Paging)
  if (rs.nav.gui_page == PageId::PINNED) {
    if (!rs.nav.menu_scroll_active && v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
      commit_swipe();
      const int total_pages = pinned_total_pages();
      const int cur_pg = std::clamp(rs.nav.pinned_menu_page, 0, total_pages - 1);
      rs.nav.pinned_menu_page = cur_pg;
      bool changed = false;
      const int target_pg = step_pager_index(
          cur_pg, 0, total_pages - 1, v_dir, /*wrap=*/false,
          GestureAxisLock::Vertical, PageId::PINNED, now_ms, changed);
      if (changed) {
        rs.nav.menu_scroll_active = true;
        rs.nav.menu_scroll_dir = (target_pg > cur_pg) ? 1 : -1;
        rs.nav.menu_scroll_target_page = target_pg;
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      } else {
        rt_.schedule_disp();
      }
      return true;
    }
    return false;
  }

  // 13. Page 12: Calendar App (Unified Canvas + Mode Bar 4-Tab Swipe, Vertical Event Paging & Map Pan)
  if (rs.nav.gui_page == PageId::CALENDAR) {
    if (rs.cal.delete_confirm_open) return false;
    if (rs.cal.drilldown == CalDrilldown::MAP) {
      if (is_release && (h_dir != 0 || v_dir != 0)) {
        commit_swipe();
        if (std::abs(dx) >= std::abs(dy)) {
          const int step_x = (std::abs(dx) >= ly.fast_pan()) ? 2 : 1;
          rs.cal.map_pan_x = std::clamp(rs.cal.map_pan_x + ((dx < 0) ? step_x : -step_x), -4, 4);
        } else {
          const int step_y = (std::abs(dy) >= ly.fast_pan()) ? 2 : 1;
          rs.cal.map_pan_y = std::clamp(rs.cal.map_pan_y + ((dy < 0) ? step_y : -step_y), -4, 4);
        }
        rt_.cmd(UiHostCmd::FETCH_CALENDAR_MAP_SNAPSHOT);
        rt_.schedule_disp();
        return true;
      }
      return false;
    }

    if (rs.cal.drilldown >= CalDrilldown::PICK_TITLE &&
        rs.cal.drilldown <= CalDrilldown::PICK_REPEAT &&
        v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
      commit_swipe();
      int opt_cnt = 10;
      if (rs.cal.drilldown == CalDrilldown::PICK_LOCATION) {
        std::string_view locs[kMaxCalendarLocations]{};
        opt_cnt = 1 + parse_calendar_locations_catalog(calendar_state().locations_catalog, locs);
      } else if (rs.cal.drilldown == CalDrilldown::PICK_CALENDAR) {
        opt_cnt = std::max(1, collect_writable_calendar_sources(calendar_state().calendars_meta, nullptr, nullptr));
      } else if (rs.cal.drilldown == CalDrilldown::PICK_REPEAT) {
        opt_cnt = 7;
      }
      const int max_pg = std::max(0, (opt_cnt - 1) / 5);
      bool changed = false;
      rs.cal.picker_page = step_pager_index(
          rs.cal.picker_page, 0, max_pg, v_dir, /*wrap=*/false,
          GestureAxisLock::Vertical, PageId::CALENDAR, now_ms, changed);
      rt_.schedule_disp();
      return true;
    }

    if (rs.cal.drilldown != CalDrilldown::NONE) {
      return false;
    }

    if (h_dir != 0) {
      commit_swipe();
      rs.cal.subpage = step_wrap(std::clamp(rs.cal.subpage, 0, 3), h_dir, 4);
      mark_dirty_full();
      rt_.schedule_disp();
      return true;
    }

    if (v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
      const int per_page = (SCREEN_H > 520) ? 5 : (LATTER_ROUND_DISPLAY ? 3 : 4);
      const auto &store = calendar_window_store();
      const int32_t today_ymd = resolve_touch_today_ymd(rt_);
      const int now_min = (rt_.time_valid && rt_.time_hour >= 0) ? (rt_.time_hour * 60 + rt_.time_minute) : -1;
      int idxs[kMaxCalendarWindowEvents]{};

      if (rs.cal.subpage == CalendarSubpage::OVERVIEW) {
        commit_swipe();
        const int cnt = collect_calendar_overview_events(
            today_ymd, now_min, snap_calendar_lookahead_days(prefs.calendar_lookahead_days),
            idxs, kMaxCalendarWindowEvents, store);
        const int max_pg = std::max(0, (std::max(1, cnt) - 1) / per_page);
        bool changed = false;
        rs.cal.overview_page = step_pager_index(
            rs.cal.overview_page, 0, max_pg, v_dir, /*wrap=*/false,
            GestureAxisLock::Vertical, PageId::CALENDAR, now_ms, changed);
        rt_.schedule_disp();
        return true;
      }
      if (rs.cal.subpage == CalendarSubpage::DAY) {
        commit_swipe();
        const int cnt = collect_calendar_day_events(rs.cal.selected_ymd, idxs, kMaxCalendarWindowEvents, store);
        const int max_pg = std::max(0, (std::max(1, cnt) - 1) / per_page);
        bool changed = false;
        rs.cal.day_page = step_pager_index(
            rs.cal.day_page, 0, max_pg, v_dir, /*wrap=*/false,
            GestureAxisLock::Vertical, PageId::CALENDAR, now_ms, changed);
        rt_.schedule_disp();
        return true;
      }
      if constexpr (SCREEN_W <= 520) {
        if (rs.cal.subpage == CalendarSubpage::WEEK) {
          commit_swipe();
          const int32_t ws = start_of_week_ymd(rs.cal.selected_ymd, prefs.calendar_week_start == 1U);
          const int32_t we = add_days_ymd(ws, 6);
          int wcnt = 0;
          for (int i = 0; i < store.count && wcnt < kMaxCalendarWindowEvents; ++i) {
            if (store.events[i].end_ymd >= ws && store.events[i].start_ymd <= we) wcnt++;
          }
          const int max_pg = std::max(0, (std::max(1, wcnt) - 1) / per_page);
          bool changed = false;
          rs.cal.week_page = step_pager_index(
              rs.cal.week_page, 0, max_pg, v_dir, /*wrap=*/false,
              GestureAxisLock::Vertical, PageId::CALENDAR, now_ms, changed);
          rt_.schedule_disp();
          return true;
        }
      }
    }
    return false;
  }

  return false;
}

void TouchGestureEngine::on_touch_move(int tx, int ty, uint32_t now_u) {
  auto &gest = touch_gesture();
  if (!gest.active) return;

  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  last_user_touch_ms() = now_u;

  if (rs.nav.display_power_state == 0 && !rs.voice.active &&
      (now_u - last_drag_sleep_rst_ms_ >= 2000U)) {
    last_drag_sleep_rst_ms_ = now_u;
    display_lifecycle().note_activity(now_u);
  }

  gest.record_move(tx, ty, now_u, LATTER_PANEL_PPI);

  // Active steering games receive continuous touch coordinates
  if (rs.nav.gui_page == PageId::GAMES && rs.nav.active_game > 0 && rs.nav.active_game != 5) {
    if (ty < layout().back_y - scale_y(30)) {
      rs.nav.marble_touch_x = static_cast<float>(tx);
      rs.nav.marble_touch_y = static_cast<float>(ty);
    }
    return;
  }

  const int cur_ctx = compute_touch_context(rs);
  if (cur_ctx != gest.start_context) return;

  // Custom Theme Color Wheel continuous drag (System Mode 13)
  if (rs.nav.gui_page == PageId::SYSTEM && rs.nav.system_menu_mode == 13 &&
      (now_u - last_disp_update_ms() >= 42U)) {
    float hue = 0.0f, sat = 1.0f;
    if (ui::hsv_wheel_pick(tx, ty, SCREEN_CX, scale_y(180), 156, 56, hue, sat)) {
      gest.axis_lock = GestureAxisLock::Slider;
      gest.tap_cancelled = true;
      color_wheel_hue_deg() = hue;
      color_wheel_sat() = sat;
      const esphome::Color c = hsv_to_rgb(hue / 360.0f, sat, 1.0f);
      prefs.custom_theme_rgb = (static_cast<uint32_t>(c.r) << 16) |
                               (static_cast<uint32_t>(c.g) << 8) |
                               static_cast<uint32_t>(c.b);
      if (rt_.theme_primary != nullptr && rt_.theme_secondary != nullptr) {
        apply_custom_rgb_harmony(prefs.custom_theme_rgb, *rt_.theme_primary, *rt_.theme_secondary);
      }
      rt_.schedule_disp();
    }
    return;
  }

  if (gest.swipe_committed) return;

  const auto &ly = layout();
  const int dx = tx - gest.start_x;
  const int dy = ty - gest.start_y;

  // Dedicated Vertical Slider (Music Volume Peer Tab)
  const HitRegion *vsl_hit = frame_hits().hit_test(gest.start_x, gest.start_y, kHitVertSlider);
  if (vsl_hit != nullptr && vsl_hit->widget_id == WidgetId::MUSIC_VOL_SLIDER &&
      rs.nav.gui_page == PageId::MUSIC &&
      rs.music.view == MusicView::VOLUME &&
      rs.music.drilldown == MusicDrilldown::NONE &&
      rs.music.takeover == MusicTakeover::NONE &&
      (gest.axis_lock == GestureAxisLock::Slider ||
       (std::abs(dy) >= ly.drag_start() && std::abs(dy) >= std::abs(dx)))) {
    gest.axis_lock = GestureAxisLock::Slider;
    gest.tap_cancelled = true;
    music_vol_dragging_ = true;
    rs.music.hud_timer = now_u;
    rs.music.takeover_interacted_ms = (now_u == 0U) ? 1U : now_u;
    auto &ms = media_state();
    const int min_v = ms.is_local ? 10 : 0;
    const int next_pct = std::clamp(static_cast<int>(std::lround((1.0f - vsl_hit->norm_y(ty)) * 100.0f)),
                                    min_v, 100);
    if (ms.muted && next_pct > 0) ms.muted = false;
    last_local_vol_tap_ms() = now_u;
    if (ms.is_local) {
      rt_.set_num_val(UiNumberId::SPEAKER_VOLUME, static_cast<float>(next_pct));
    } else {
      remote_vol_pct() = next_pct;
      if (now_u - last_music_vol_drag_ms_ >= 120U) {
        last_music_vol_drag_ms_ = now_u;
        latter_actions::emit_media_volume(resolve_music_speaker_entity(rs), "set", next_pct);
      }
      if (now_u - last_disp_update_ms() >= 42U) {
        rt_.schedule_disp();
      }
    }
    return;
  }

  // Smooth Free Vertical Touch Scroll on Music Lyrics & Track Info (above Mode Bar when HUD shown)
  if (rs.nav.gui_page == PageId::MUSIC &&
      rs.music.drilldown == MusicDrilldown::NONE &&
      rs.music.takeover == MusicTakeover::NONE &&
      (rs.music.view == MusicView::LYRICS || rs.music.view == MusicView::INFO) &&
      (!rs.music.hud_active || gest.start_y < ly.mode_bar_zone_top()) &&
      (gest.axis_lock == GestureAxisLock::Slider ||
       (std::abs(dy) >= ly.drag_start() && std::abs(dy) > std::abs(dx)))) {
    gest.axis_lock = GestureAxisLock::Slider;
    gest.tap_cancelled = true;
    const int step_px = music_scroll_last_y_ - ty;
    music_scroll_last_y_ = ty;
    music_scroll_dragging_ = true;
    if (rs.music.hud_active) {
      rs.music.hud_timer = now_u;
    }
    if (step_px != 0) {
      if (rs.music.view == MusicView::LYRICS) {
        auto &lyr = rs.music.lyrics;
        lyr.scroll_px = std::max(0, lyr.scroll_px + step_px);
        lyr.target_scroll_px = lyr.scroll_px;
        lyr.scroll_anim_start_ms = 0U;
        lyr.manual_override_until_ms = now_u + 6000U;
      } else {
        rs.music.info_scroll_px = std::max(0, rs.music.info_scroll_px + step_px);
      }
      if (now_u - last_disp_update_ms() >= 36U) {
        rt_.schedule_disp();
      }
    }
    return;
  }

  // Dedicated Horizontal Sliders (Music Scrubber Takeover & Area Entity Level/Kelvin Inspector)
  const HitRegion *sl_hit = frame_hits().hit_test(gest.start_x, gest.start_y, kHitHorizSlider);
  if (sl_hit != nullptr &&
      (gest.axis_lock == GestureAxisLock::Slider ||
       (std::abs(dx) >= ly.drag_start() && std::abs(dx) >= std::abs(dy)))) {
    gest.axis_lock = GestureAxisLock::Slider;
    gest.tap_cancelled = true;
    if (sl_hit->widget_id == WidgetId::MUSIC_SCRUB_BAR &&
        rs.nav.gui_page == PageId::MUSIC && rs.music.takeover == MusicTakeover::SCRUB) {
      music_scrub_dragging_ = true;
      rs.music.takeover_interacted_ms = (now_u == 0U) ? 1U : now_u;
      const float dur = media_state().duration;
      if (!std::isnan(dur) && dur > 0.0f && (now_u - last_disp_update_ms() >= 42U)) {
        rs.music.pos_base = sl_hit->norm_x(tx) * dur;
        rs.music.pos_sync_time = now_u;
        rt_.schedule_disp();
      }
      return;
    }
    if (sl_hit->widget_id == WidgetId::LIGHT_TEMP_SLIDER &&
        rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
      const int raw_k = 2000 + static_cast<int>(sl_hit->norm_x(tx) * 4500.0f);
      const int next_k = std::clamp(((raw_k + 50) / 100) * 100, 2000, 6500);
      if (next_k != light_insp_kelvin() && (now_u - last_disp_update_ms() >= 42U)) {
        light_insp_kelvin() = next_k;
        rt_.schedule_disp();
      }
      return;
    }
    if (sl_hit->widget_id == WidgetId::LIGHT_BRI_SLIDER &&
        rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
      const int raw_b = static_cast<int>(sl_hit->norm_x(tx) * 100.0f);
      const int min_b = (light_insp_domain() == "cover") ? 0 : 5;
      const int next_b = std::clamp(((raw_b + 2) / 5) * 5, min_b, 100);
      if (next_b != light_insp_bri_pct() && (now_u - last_disp_update_ms() >= 42U)) {
        light_insp_bri_pct() = next_b;
        if (light_insp_domain() == "cover" || light_insp_domain() == "fan") {
          light_insp_active() = (next_b > 0);
        }
        rt_.schedule_disp();
      }
      return;
    }
    if (sl_hit->widget_id == WidgetId::SYS_BRI_SLIDER &&
        rs.nav.gui_page == PageId::SYSTEM && rs.nav.system_menu_mode == 2) {
      UiNumberId nid = UiNumberId::MANUAL_BRIGHTNESS;
      if (resolve_sys_bri_slider_id(sl_hit->param, rt_.select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE), nid)) {
        const int next_pct = std::clamp(1 + static_cast<int>(std::lround(sl_hit->norm_x(tx) * 99.0f)), 1, 100);
        const int cur_pct = static_cast<int>(std::lround(rt_.num_val(nid)));
        if (next_pct != cur_pct && (now_u - last_disp_update_ms() >= 42U)) {
          rt_.set_num_val(nid, static_cast<float>(next_pct));
          rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
          rt_.schedule_disp();
        }
      }
      return;
    }
  }

  // Light Inspector HSV Wheel continuous drag (Area Subpage 2, light domain)
  if (rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL &&
      light_insp_domain() == "light" && light_insp_mode() == 0 &&
      (now_u - last_disp_update_ms() >= 42U)) {
    float hue = 0.0f, sat = 1.0f;
    if (ui::hsv_wheel_pick(tx, ty, SCREEN_CX, scale_y(212), 140, 62, hue, sat)) {
      gest.axis_lock = GestureAxisLock::Slider;
      gest.tap_cancelled = true;
      light_insp_hue_deg() = hue;
      light_insp_sat() = sat;
      update_light_inspector_hex(hue, sat);
      rt_.schedule_disp();
      return;
    }
  }

  if (!gest.long_press_armed) {
    cancel_core_timeout(kTimeoutTouchLongPressId);
  }
  if (try_commit_long_press_(now_u)) {
    return;
  }
  (void)try_commit_kinematic_swipe_(false, now_u);
}

void TouchGestureEngine::tick(uint32_t now_ms) {
  auto &rs = runtime_state();
  auto &sc = shortcut_confirm_state();
  if (sc.open && sc.opened_ms > 0U &&
      static_cast<uint32_t>(now_ms - sc.opened_ms) >= ShortcutConfirmState::kAutoDismissMs) {
    sc.clear();
    mark_dirty_full();
    rt_.schedule_disp();
  }
  if (rs.nav.gui_page == PageId::MUSIC && !touch_gesture().active &&
      rs.music.takeover_interacted_ms > 0U) {
    const uint32_t idle_ms = (now_ms >= rs.music.takeover_interacted_ms)
                                 ? (now_ms - rs.music.takeover_interacted_ms)
                                 : 0U;
    if (rs.music.takeover == MusicTakeover::SCRUB && idle_ms >= 5000U) {
      rs.music.takeover = MusicTakeover::NONE;
      rs.music.hud_active = true;
      rs.music.hud_timer = now_ms;
      mark_dirty_full();
      rt_.schedule_disp();
    }
  }
  (void)try_commit_long_press_(now_ms);
}

bool TouchGestureEngine::try_commit_long_press_(uint32_t now_ms) {
  auto &gest = touch_gesture();
  if (!gest.should_fire_long_press(now_ms)) return false;

  auto &rs = runtime_state();
  if (compute_touch_context(rs) != gest.start_context) {
    gest.long_press_armed = false;
    cancel_core_timeout(kTimeoutTouchLongPressId);
    return false;
  }

  const HitRegion *reg = frame_hits().find_by_id_param(gest.long_press_wid, gest.long_press_param);
  if (reg != nullptr && !reg->contains_padded(gest.last_x, gest.last_y)) {
    gest.long_press_armed = false;
    cancel_core_timeout(kTimeoutTouchLongPressId);
    return false;
  }

  gest.mark_long_press_fired();
  cancel_core_timeout(kTimeoutTouchLongPressId);

  if (rs.nav.gui_page == PageId::MUSIC &&
      ((gest.long_press_wid >= WidgetId::LIST_SLOT_BASE &&
        gest.long_press_wid < WidgetId::LIST_SLOT_BASE + 5) ||
       (gest.long_press_wid >= WidgetId::MUSIC_ROW_PLAY_BASE &&
        gest.long_press_wid < WidgetId::MUSIC_ROW_PLAY_BASE + 5))) {
    if (open_music_row_action_sheet(gest.long_press_param, rs, now_ms)) {
      mark_dirty_full();
      rt_.schedule_disp();
      return true;
    }
  }

  if (rs.nav.gui_page == PageId::REMOTE && rs.nav.remote_subpage == 0) {
    const int remote_key = remote_key_for_widget(gest.long_press_wid);
    if (remote_key >= 0) {
      const int act_slot = resolve_active_remote_slot(rs.remote.active_slot);
      rs.remote.active_slot = act_slot;
      const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, gest.long_press_wid, gest.long_press_param);
      if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        do_remote_send_key(remote_key, act_slot, /*long_press=*/true, now_ms);
        rt_.schedule_disp();
        return true;
      }
    }
  }
  rt_.schedule_disp();
  return true;
}

void TouchGestureEngine::on_touch_up(uint32_t now_rel) {
  auto &gest = touch_gesture();
  if (!gest.active) return;

  const uint64_t rel_t0_us = profiler_enabled() ? prof_now_us() : 0ULL;
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();

  if (!rs.nav.hud_wake_finger_released) {
    rs.nav.hud_wake_finger_released = true;
  }

  const auto &ly = layout();
  const int dx = gest.last_x - gest.start_x;
  const int dy = gest.last_y - gest.start_y;
  const TouchPt tp{gest.last_x, gest.last_y};
  const HitRegion *start_vslider = frame_hits().hit_test(gest.start_x, gest.start_y, kHitVertSlider);
  const HitRegion *start_slider = frame_hits().hit_test(gest.start_x, gest.start_y, kHitHorizSlider);

  const int cur_ctx = compute_touch_context(rs);

  if (cur_ctx == gest.start_context) {
    // 1. Commit Dedicated Vertical/Horizontal Sliders & Color Wheel Releases First
    if (!gest.swipe_committed && start_vslider != nullptr &&
        start_vslider->widget_id == WidgetId::MUSIC_VOL_SLIDER &&
        rs.nav.gui_page == PageId::MUSIC &&
        rs.music.view == MusicView::VOLUME &&
        rs.music.drilldown == MusicDrilldown::NONE &&
        rs.music.takeover == MusicTakeover::NONE) {
      rs.music.hud_timer = now_rel;
      rs.music.takeover_interacted_ms = (now_rel == 0U) ? 1U : now_rel;
      auto &ms = media_state();
      const int min_v = ms.is_local ? 10 : 0;
      const int next_pct = std::clamp(
          static_cast<int>(std::lround((1.0f - start_vslider->norm_y(tp.y)) * 100.0f)), min_v, 100);
      if (ms.muted && next_pct > 0) ms.muted = false;
      last_local_vol_tap_ms() = now_rel;
      if (ms.is_local) {
        rt_.set_num_val(UiNumberId::SPEAKER_VOLUME, static_cast<float>(next_pct));
      } else {
        remote_vol_pct() = next_pct;
        latter_actions::emit_media_volume(resolve_music_speaker_entity(rs), "set", next_pct);
      }
      gest.tap_cancelled = true;
      rt_.schedule_disp();
    } else if (!gest.swipe_committed && start_slider != nullptr) {
      if (start_slider->widget_id == WidgetId::MUSIC_SCRUB_BAR &&
          rs.nav.gui_page == PageId::MUSIC && rs.music.takeover == MusicTakeover::SCRUB) {
        rs.music.takeover_interacted_ms = (now_rel == 0U) ? 1U : now_rel;
        auto &ms = media_state();
        const float dur = ms.duration;
        if (!std::isnan(dur) && dur > 0.0f) {
          const float next_pos = start_slider->norm_x(tp.x) * dur;
          ms.position = next_pos;
          ms.position_updated_ms = now_rel;
          rs.music.pos_base = next_pos;
          rs.music.pos_sync_time = now_rel;
          media_seek_absolute(now_rel);
          rt_.schedule_disp();
        }
        gest.tap_cancelled = true;
      } else if (start_slider->widget_id == WidgetId::LIGHT_TEMP_SLIDER &&
                 rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
        rs.area.target_entity_id = light_insp_entity_id();
        rs.area.target_action = "set_kelvin";
        rs.area.target_level = light_insp_kelvin();
        rs.area.feedback_timer = now_rel;
        do_execute_area_action();
        rt_.schedule_disp();
      } else if (start_slider->widget_id == WidgetId::LIGHT_BRI_SLIDER &&
                 rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
        const std::string &idom = light_insp_domain();
        const int pct = light_insp_bri_pct();
        const int sl_i = light_insp_slot();
        if (sl_i >= 0 && sl_i < 3) {
          rs.area_entity_slots()[sl_i].level = pct;
          rs.area_entity_slots()[sl_i].active = (pct > 0);
        }
        rs.area.target_entity_id = light_insp_entity_id();
        if (pct == 0 && (idom == "cover" || idom == "fan")) {
          rs.area.target_action = (idom == "cover") ? "close_cover" : "turn_off";
          rs.area.target_level = 0;
        } else if (pct >= 100 && idom == "cover") {
          rs.area.target_action = "open_cover";
          rs.area.target_level = 100;
        } else {
          rs.area.target_action = "set_level";
          rs.area.target_level = pct;
        }
        rs.area.feedback_timer = now_rel;
        do_execute_area_action();
        rt_.schedule_disp();
      } else if (start_slider->widget_id == WidgetId::SYS_BRI_SLIDER &&
                 rs.nav.gui_page == PageId::SYSTEM && rs.nav.system_menu_mode == 2) {
        if (gest.axis_lock == GestureAxisLock::Slider) {
          UiNumberId nid = UiNumberId::MANUAL_BRIGHTNESS;
          if (resolve_sys_bri_slider_id(start_slider->param, rt_.select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE), nid)) {
            const int next_pct = std::clamp(1 + static_cast<int>(std::lround(start_slider->norm_x(tp.x) * 99.0f)), 1, 100);
            rt_.set_num_val(nid, static_cast<float>(next_pct));
            rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
            rt_.schedule_disp();
          }
          gest.tap_cancelled = true;
        } else {
          (void)try_commit_kinematic_swipe_(true, now_rel);
        }
      }
    } else if (!gest.swipe_committed && rs.nav.gui_page == PageId::AREA &&
               rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL &&
               light_insp_domain() == "light" && light_insp_mode() == 0) {
      float hue = 0.0f, sat = 1.0f;
      if (ui::hsv_wheel_pick(tp.x, tp.y, SCREEN_CX, scale_y(212), 140, 62, hue, sat)) {
        light_insp_hue_deg() = hue;
        light_insp_sat() = sat;
        update_light_inspector_hex(hue, sat);
        rs.area.target_entity_id = light_insp_entity_id();
        rs.area.target_action = "set_color";
        rs.area.target_level = light_insp_bri_pct();
        rs.area.feedback_timer = now_rel;
        do_execute_area_action();
        rt_.schedule_disp();
      }
    } else {
      // 2. Evaluate Unified Kinematic Swipe / Fast-Flick Commit Across All Pages
      (void)try_commit_kinematic_swipe_(true, now_rel);
    }

    // 3. Strict Tap Resolution (D-049): Only eligible when !swipe_committed && !tap_cancelled
    const bool tap_valid = !gest.swipe_committed && !gest.tap_cancelled && (gest.max_travel <= ly.tap_max());
    const HitRegion *hit = tap_valid ? frame_hits().hit_test(tp.x, tp.y, kHitTap) : nullptr;
    if (hit == nullptr && tap_valid) {
      hit = frame_hits().hit_test(gest.start_x, gest.start_y, kHitTap);
    }
    const uint16_t wid = (hit != nullptr) ? hit->widget_id : WidgetId::NONE;
    const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

    if (tap_valid && shortcut_confirm_state().open) {
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
          apply_optimistic_shortcut_feedback(ent_id, act, lbl, rs, now_rel);
        }
        mark_dirty_full();
        rt_.schedule_disp();
      }
    } else if (tap_valid && rs.nav.gui_page == PageId::HOME && rs.nav.home_subpage == 0) {
      int act_count = 0;
      const HomeActivityItem *act_items = home_activity_rendered_items(act_count);
      if (act_count <= 0) {
        HomeActivityItem *fallback_items = touch_activity_fallback_scratch();
        const auto &ms_hm = media_state();
        const bool m_play = is_music_playing_effective(
            !ms_hm.is_local, rt_.local_mp_playing, ms_hm.state, now_rel);
        const std::string &m_t = (!ms_hm.is_local || rt_.track_title.empty()) ? ms_hm.title : rt_.track_title;
        const std::string &m_a = (!ms_hm.is_local || rt_.track_artist.empty()) ? ms_hm.artist : rt_.track_artist;
        act_count = collect_home_activities(
            now_rel, prefs.is_24h_format,
            rs.stopwatch.running, rs.stopwatch.start_time_ms, rs.stopwatch.elapsed_ms,
            m_play, m_t, m_a, rs.alarms.snooze_until,
            prefs.alarms[0].hour, prefs.alarms[0].minute, prefs.alarms[0].enabled,
            prefs.alarms[1].hour, prefs.alarms[1].minute, prefs.alarms[1].enabled,
            prefs.alarms[2].hour, prefs.alarms[2].minute, prefs.alarms[2].enabled,
            fallback_items, rt_.time_hour, rt_.time_minute, calendar_state().summary,
            rt_.time_day_of_week);
        act_items = fallback_items;
      }

      if (wid >= WidgetId::DOCK_SLOT_BASE && wid < WidgetId::DOCK_SLOT_BASE + 4) {
        if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          ShortcutItem *dock_items = dock_items_scratch();
          const int dock_cnt = collect_effective_dock_items(dock_items, kMaxEffectiveDockItems);
          if (hit->param >= 0 && hit->param < dock_cnt) {
            const ShortcutItem chosen = dock_items[hit->param];
            activate_shortcut_item(chosen, ShortcutOrigin::HOME_DOCK, rs, rt_, now_rel);
          }
        }
      } else if (wid == WidgetId::HOME_QUICK_BTN) {
        if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const bool is_night = (weather_state().sun_state == "below_horizon");
          rs.nav.gui_page = PageId::WEATHER;
          rs.nav.sky_weather_subpage = is_night ? 3 : 2;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (wid == WidgetId::HOME_AREA_ICON) {
        if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const auto &ac = area_ctx_state();
          if (!ac.area_id.empty()) {
            rs.area.active_area_id = ac.area_id;
            rs.area.active_area_name = ac.area_name.empty() ? latter_area_label() : ac.area_name;
          }
          launch_dock_app(PageId::AREA, rs, rt_, now_rel);
        }
      } else if (wid == WidgetId::HOME_DATE_HERO) {
        if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          launch_dock_app(PageId::CALENDAR, rs, rt_, now_rel);
          rs.cal.subpage = (prefs.home_date_tap_action == 0U)
                               ? CalendarSubpage::DAY
                               : CalendarSubpage::OVERVIEW;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (wid == WidgetId::HOME_TIME_READOUT) {
        if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          if (prefs.home_clock_tap_action == 0U) {
            launch_dock_app(PageId::TIME, rs, rt_, now_rel);
          } else {
            rs.nav.gui_page = PageId::CLOCK;
            mark_dirty_full();
            rt_.schedule_disp();
          }
        }
      } else if (wid == WidgetId::HOME_WEATHER_HERO) {
        if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          rs.nav.gui_page = PageId::WEATHER;
          rs.nav.sky_weather_subpage = 0;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (wid == WidgetId::HOME_ACT_EXPAND && act_count > 0) {
        if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          home_activity_overlay_page() = 0;
          rs.nav.home_subpage = 2;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if ((wid == WidgetId::HOME_ACT_ROW_0 || wid == WidgetId::HOME_ACT_ROW_1 ||
                  wid == WidgetId::HOME_ACT_ACTION_0 || wid == WidgetId::HOME_ACT_ACTION_1) &&
                 act_count > 0) {
        if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const bool quick_tap =
              (wid == WidgetId::HOME_ACT_ACTION_0 || wid == WidgetId::HOME_ACT_ACTION_1);
          handle_home_activity_action(act_items, act_count, hit->param, quick_tap,
                                      now_rel, rs, prefs, rt_);
        }
      }

    } else if (tap_valid && rs.nav.gui_page == PageId::HOME && rs.nav.home_subpage == 1) {
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
          rt_.schedule_disp();
        } else if (item_idx == 1) {
          rt_.set_select_opt(UiSelectId::HOME_DATE_TAP_ACTION,
                             (prefs.home_date_tap_action == 0U) ? "Overview" : "Day View");
          rt_.schedule_disp();
        } else if (item_idx == 2) {
          rt_.sw_toggle(UiSwitchId::HOME_WEATHER_BG);
          rt_.schedule_disp();
        } else if (item_idx == 3) {
          rt_.sw_toggle(UiSwitchId::HOME_WAKE_GREETING);
          rt_.schedule_disp();
        } else if (item_idx == 4 || item_idx == 5) {
          const PrefKey pk = (item_idx == 4) ? PrefKey::HOME_SUN : PrefKey::HOME_MOON;
          const bool next_v = (item_idx == 4) ? !prefs.home_sun : !prefs.home_moon;
          uint32_t eff = 0U;
          if (latter_actions::record_local_pref_edit(pk, next_v ? "1" : "0", now_rel, eff)) {
            device_prefs_state().last_effects = eff;
            rt_.cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
          }
          rt_.schedule_disp();
        } else if (item_idx == 6) {
          rt_.sw_toggle(UiSwitchId::WEATHER_APP_BG);
          rt_.schedule_disp();
        }
      }

    } else if (tap_valid && rs.nav.gui_page == PageId::HOME && rs.nav.home_subpage == 2) {
      int act_count = 0;
      const HomeActivityItem *act_items = home_activity_rendered_items(act_count);
      if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 5 && act_count > 0) {
        if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const bool quick_tap = (tp.x >= hit->x1 - scale_x(56));
          handle_home_activity_action(act_items, act_count, hit->param, quick_tap,
                                      now_rel, rs, prefs, rt_);
        }
      } else if (wid == WidgetId::NONE &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_DISMISS, tkey)) {
        rs.nav.home_subpage = 0;
        home_activity_overlay_page() = 0;
        mark_dirty_full();
        rt_.schedule_disp();
      }

    // 5. Page 2 Clean-Sheet Music App v3 (8 Peer Mode-Bar Tabs, Deep Drilldowns & Floating Takeovers)
    } else if (tap_valid && rs.nav.gui_page == PageId::MUSIC) {
      auto &m = rs.music;
      auto &ms = media_state();
      const std::string spk = resolve_music_speaker_entity(rs);
      const std::string_view ov_style = rt_.select_opt(UiSelectId::MUSIC_OVERLAY_STYLE);
      const bool always_vis = (ov_style == "Always Visible");

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
                     touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
            ms.shuffle = !ms.shuffle;
            latter_actions::emit_media_shuffle_toggle(spk);
            rt_.schedule_disp();
          } else if (wid == WidgetId::MUSIC_REPEAT_BTN &&
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
              reset_music_to_now_playing(rs, now_rel, true);
              mark_dirty_full();
              rt_.schedule_disp();
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
              fetch_music_browse_for_current_view(rs, now_rel);
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
              dispatch_music_play_item(m.action_sheet_id, m.action_sheet_type,
                                       m.action_sheet_name, enq, is_rad, rs, now_rel);
              if (is_play_now || is_rep || is_rad) {
                reset_music_to_now_playing(rs, now_rel, true);
              } else {
                m.takeover = MusicTakeover::NONE;
              }
              mark_dirty_full();
              rt_.schedule_disp();
            }
          }
        }
      } else if (m.drilldown == MusicDrilldown::NONE &&
                 (wid == WidgetId::MODE_BAR_LEFT || wid == WidgetId::MODE_BAR_RIGHT)) {
        if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
          m.direct_nav = false;
          const int step_dir = (wid == WidgetId::MODE_BAR_RIGHT) ? 1 : -1;
          activate_music_peer_tab(step_music_peer_tab(m.view, step_dir), now_rel);
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (m.drilldown == MusicDrilldown::NONE &&
                 wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4) {
        if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          m.direct_nav = false;
          const auto target_view = static_cast<MusicView>(
              std::clamp(static_cast<int>(hit->param), 0, kMusicPeerTabCount - 1));
          activate_music_peer_tab(target_view, now_rel);
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (m.view == MusicView::NOW_PLAYING) {
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
        } else if (wid == WidgetId::MUSIC_PLAY_PAUSE &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const bool was_playing = is_music_playing_effective(
              !ms.is_local, rt_.local_mp_playing, ms.state, now_rel);
          m.action_feedback = was_playing ? "Paused" : "Playing";
          m.feedback_timer = now_rel;
          m.hud_timer = now_rel;
          rt_.cmd(UiHostCmd::TOGGLE_HA_MUSIC);
          rt_.schedule_disp();
        } else if ((wid == WidgetId::MUSIC_PREV_BTN || wid == WidgetId::MUSIC_NEXT_BTN) &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
          const bool is_next = (wid == WidgetId::MUSIC_NEXT_BTN);
          std::string t_title, t_artist, t_album;
          bool is_speech = false;
          resolve_music_track_strings(rs, rt_, t_title, t_artist, t_album, is_speech);
          if (is_speech) {
            media_seek_relative(is_next ? 1 : -1, now_rel);
          } else if (is_next) {
            media_next_track(now_rel);
          } else {
            media_prev_track(now_rel);
          }
          m.hud_timer = now_rel;
          rt_.schedule_disp();
        } else if ((wid == WidgetId::NONE || wid == WidgetId::MUSIC_ART_TAP) &&
                   !always_vis && m.hud_active &&
                   (now_rel - m.hud_timer >= TOUCH_GUARD_DISMISS) &&
                   (now_rel - hud_just_woken_ms_ >= TOUCH_GUARD_DISMISS) &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_DISMISS, tkey)) {
          m.hud_active = false;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (m.view == MusicView::VOLUME) {
        if ((wid == WidgetId::MUSIC_VOL_UP_STEP || wid == WidgetId::MUSIC_VOL_DOWN_STEP) &&
            touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
          const int dir = (wid == WidgetId::MUSIC_VOL_UP_STEP) ? 1 : -1;
          if (ms.muted && dir > 0) ms.muted = false;
          step_music_volume_feedback(
              dir, rt_.num_val(UiNumberId::VOLUME_STEP_SIZE),
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
      } else if (m.view == MusicView::LYRICS) {
        if (!m.hud_active) {
          if (wid == WidgetId::MUSIC_ART_TAP || wid == WidgetId::NONE) {
            m.hud_active = true;
            m.hud_timer = now_rel;
            hud_just_woken_ms_ = now_rel;
            mark_dirty_full();
            rt_.schedule_disp();
          }
        } else if (wid == WidgetId::MUSIC_LYRIC_SYNC_BTN &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          m.lyrics.manual_override_until_ms = 0U;
          m.lyrics.target_scroll_px = -1;
          m.hud_timer = now_rel;
          rt_.schedule_disp();
        } else if (wid >= WidgetId::MUSIC_LYRIC_LINE_BASE &&
                   wid < WidgetId::MUSIC_LYRIC_LINE_BASE + 8 &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
          const int li = static_cast<int>(hit->param);
          if (li >= 0 && static_cast<size_t>(li) < m.lyrics.lines.size()) {
            const int32_t t_ms = m.lyrics.lines[static_cast<size_t>(li)].t_ms;
            if (t_ms >= 0) {
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
          }
        } else if (wid == WidgetId::NONE && m.hud_active &&
                   (now_rel - m.hud_timer >= TOUCH_GUARD_DISMISS) &&
                   (now_rel - hud_just_woken_ms_ >= TOUCH_GUARD_DISMISS) &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_DISMISS, tkey)) {
          m.hud_active = false;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (m.view == MusicView::INFO) {
        if (!m.hud_active) {
          if (wid == WidgetId::MUSIC_ART_TAP || wid == WidgetId::NONE) {
            m.hud_active = true;
            m.hud_timer = now_rel;
            hud_just_woken_ms_ = now_rel;
            mark_dirty_full();
            rt_.schedule_disp();
          }
        } else if (wid == WidgetId::NONE && m.hud_active &&
                   (now_rel - m.hud_timer >= TOUCH_GUARD_DISMISS) &&
                   (now_rel - hud_just_woken_ms_ >= TOUCH_GUARD_DISMISS) &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_DISMISS, tkey)) {
          m.hud_active = false;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (m.view == MusicView::SETTINGS) {
        if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
            touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
          m.settings_page = (wid == WidgetId::CHEVRON_UP) ? 1 : 2;
          m.hud_timer = now_rel;
          rt_.schedule_disp();
        } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 3 &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const int idx = (std::clamp(m.settings_page, 1, 2) - 1) * 3 + static_cast<int>(hit->param);
          if (idx == 0) {
            rt_.set_select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE),
                                            {"Full Screen", "Letterbox", "Standard", "None"}));
          } else if (idx == 1) {
            rt_.set_select_opt(UiSelectId::MUSIC_OVERLAY_STYLE,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_OVERLAY_STYLE),
                                            {"Art Only", "Art + Metadata", "Always Visible"}));
          } else if (idx == 2) {
            rt_.set_select_opt(UiSelectId::MUSIC_ALBUM_SORT_ORDER,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_ALBUM_SORT_ORDER),
                                            {"Alphabetical", "Newest on Top", "Oldest on Top"}));
          } else if (idx == 3) {
            rt_.set_select_opt(UiSelectId::MUSIC_ARTIST_ALBUM_SORT_ORDER,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_ARTIST_ALBUM_SORT_ORDER),
                                            {"Oldest on Top", "Newest on Top", "Alphabetical"}));
          } else if (idx == 4) {
            rt_.set_select_opt(UiSelectId::MUSIC_PODCAST_SORT_ORDER,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_PODCAST_SORT_ORDER),
                                            {"Newest on Top", "Oldest on Top"}));
          }
          m.hud_timer = now_rel;
          rt_.schedule_disp();
        }
      } else if (m.view == MusicView::SPEAKERS) {
        if (m.drilldown == MusicDrilldown::SPEAKER_ACTIONS && !m.target_speaker_id.empty()) {
          if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
            if (wid == WidgetId::MUSIC_SPK_CONTROL_BTN) {
              rt_.cmd(UiHostCmd::SPEAKER_CONTROL_TARGET);
              reset_music_to_now_playing(rs, now_rel, true);
              mark_dirty_full();
              rt_.schedule_disp();
            } else if (wid == WidgetId::MUSIC_SPK_TRANSFER_BTN) {
              rt_.cmd(UiHostCmd::SPEAKER_TRANSFER_TO_TARGET);
              reset_music_to_now_playing(rs, now_rel, true);
              mark_dirty_full();
              rt_.schedule_disp();
            } else if (wid == WidgetId::MUSIC_SPK_GROUP_BTN) {
              if (m.target_speaker_id == spk) {
                rt_.cmd(UiHostCmd::SPEAKER_UNJOIN_TARGET);
              } else {
                rt_.cmd(UiHostCmd::SPEAKER_JOIN_TARGET);
              }
              reset_music_to_now_playing(rs, now_rel, true);
              mark_dirty_full();
              rt_.schedule_disp();
            }
          }
        } else {
          int &b_page = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
          const int b_tot = std::max(1, rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC));
          if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
              touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
            const int next_pg = std::clamp(b_page + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, b_tot);
            m.hud_timer = now_rel;
            if (next_pg != b_page) {
              b_page = next_pg;
              fetch_music_browse_for_current_view(rs, now_rel);
              rt_.schedule_disp();
            }
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
        }
      } else if (m.view == MusicView::QUEUE) {
        int &b_page = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
        const int b_tot = std::max(1, rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC));
        if (wid == WidgetId::MUSIC_SHUFFLE_BTN &&
            touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          ms.shuffle = !ms.shuffle;
          m.hud_timer = now_rel;
          latter_actions::emit_media_shuffle_toggle(spk);
          rt_.schedule_disp();
        } else if (wid == WidgetId::MUSIC_REPEAT_BTN &&
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
          const int next_pg = std::clamp(b_page + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, b_tot);
          m.hud_timer = now_rel;
          if (next_pg != b_page) {
            b_page = next_pg;
            fetch_music_browse_for_current_view(rs, now_rel);
            rt_.schedule_disp();
          }
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
            reset_music_to_now_playing(rs, now_rel, true);
            mark_dirty_full();
            rt_.schedule_disp();
          } else if (!sl.id.empty() && sl.type != "text") {
            rt_.cmd(UiHostCmd::PLAY_MUSIC_SLOT, si + 1);
            reset_music_to_now_playing(rs, now_rel, true);
            mark_dirty_full();
            rt_.schedule_disp();
          }
        }
      } else if (m.view == MusicView::LIBRARY || m.view == MusicView::ALBUM_TRACKS) {
        int &b_page = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
        const int b_tot = std::max(1, rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC));
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
                                (m.selected_category_mode == "artist" ||
                                 m.selected_category_mode == "album");
            const int next_pg = wrap_l ? step_wrap_1based(b_page, step_d, b_tot)
                                       : std::clamp(b_page + step_d, 1, b_tot);
            if (next_pg != b_page) {
              b_page = next_pg;
              fetch_music_browse_for_current_view(rs, now_rel);
              rt_.schedule_disp();
            }
          }
        } else if (on_cat_root &&
                   wid >= WidgetId::GRID_CARD_BASE && wid < WidgetId::GRID_CARD_BASE + 6 &&
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
            fetch_music_browse_for_current_view(rs, now_rel);
            mark_dirty_full();
            rt_.schedule_disp();
          }
        } else if (wid == WidgetId::MUSIC_HEADER_ENQUEUE_BTN &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          if (open_music_header_action_sheet(rs, rt_, now_rel)) {
            mark_dirty_full();
            rt_.schedule_disp();
          }
        } else if (wid >= WidgetId::MUSIC_ROW_PLAY_BASE && wid < WidgetId::MUSIC_ROW_PLAY_BASE + 5 &&
                   now_rel >= music_scroll_tap_lockout_until_ms() &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const int si = std::clamp(static_cast<int>(hit->param), 0, 4);
          const auto &sl = rs.music_slots()[si];
          if (!sl.id.empty() && sl.type != "text") {
            const std::string mtype = resolve_browse_item_media_type(rs, sl);
            dispatch_music_play_item(sl.id, mtype, sl.name, "play", false, rs, now_rel);
            reset_music_to_now_playing(rs, now_rel, true);
            mark_dirty_full();
            rt_.schedule_disp();
          }
        } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 5 &&
                   now_rel >= music_scroll_tap_lockout_until_ms() &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const int si = std::clamp(static_cast<int>(hit->param), 0, 4);
          const auto &sl = rs.music_slots()[si];
          if (!sl.id.empty() && sl.type != "text") {
            const std::string mtype = resolve_browse_item_media_type(rs, sl);
            if (m.selected_category_mode == "podcast" || mtype == "podcast") {
              m.drilldown = MusicDrilldown::LIB_LIST;
              m.drilldown_album_id = sl.id;
              m.drilldown_album_page = b_page;
              m.drilldown_parent_mode = m.selected_category_mode;
              m.selected_category_mode = "podcast_episodes";
              m.selected_category_title = sl.name;
              reset_music_loading_slots(rs, "Loading episodes...");
              b_page = 1;
              fetch_music_browse_for_current_view(rs, now_rel);
              mark_dirty_full();
              rt_.schedule_disp();
            } else if (m.selected_category_mode == "artist" || mtype == "artist") {
              m.drilldown = MusicDrilldown::LIB_LIST;
              m.drilldown_artist_name = sl.name;
              m.drilldown_artist_page = b_page;
              m.selected_category_mode = "artist_albums";
              m.selected_category_title = sl.name;
              reset_music_loading_slots(rs, "Loading albums...");
              b_page = 1;
              fetch_music_browse_for_current_view(rs, now_rel);
              mark_dirty_full();
              rt_.schedule_disp();
            } else if (m.selected_category_mode == "album" ||
                       m.selected_category_mode == "artist_albums" ||
                       m.selected_category_mode == "playlist" ||
                       mtype == "album" ||
                       mtype == "playlist") {
              m.drilldown = MusicDrilldown::LIB_LIST;
              m.drilldown_album_id = sl.id;
              m.drilldown_album_page = b_page;
              m.drilldown_parent_mode = m.selected_category_mode;
              m.selected_category_mode = "album_tracks";
              m.selected_category_title = sl.name;
              reset_music_loading_slots(rs, "Loading tracks...");
              b_page = 1;
              fetch_music_browse_for_current_view(rs, now_rel);
              mark_dirty_full();
              rt_.schedule_disp();
            } else {
              dispatch_music_play_item(sl.id, mtype, sl.name, "play", false, rs, now_rel);
              reset_music_to_now_playing(rs, now_rel, true);
              mark_dirty_full();
              rt_.schedule_disp();
            }
          }
        }
      }

    // 6. Page 3 Games (2048 Tap & Launcher Play/Sound/Carousel Taps)
    } else if (tap_valid && rs.nav.gui_page == PageId::GAMES && rs.nav.active_game == 5) {
      rs.nav.marble_touch_x = static_cast<float>(tp.x);
      rs.nav.marble_touch_y = static_cast<float>(tp.y);
      rs.nav.game_btn_pressed = true;
      rt_.schedule_disp();
    } else if (tap_valid && rs.nav.gui_page == PageId::GAMES &&
               rs.nav.active_game == 0 && !rs.nav.menu_scroll_active) {
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
              (hit != nullptr && hit->param >= 1 && hit->param <= 6)
                  ? static_cast<int>(hit->param)
                  : enabled_ids[slot];
          rs.nav.active_game = resolved_game;
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

    // 7. Page 4 System Scrollable Menus (Modes 0, 1, 2) via SYS_TOGGLE_ROW / SYS_BRI_SLIDER / COLOR_SWATCH_BASE
    } else if (tap_valid && rs.nav.gui_page == PageId::SYSTEM &&
               !rs.nav.menu_scroll_active &&
               (rs.nav.system_menu_mode == 0 || rs.nav.system_menu_mode == 1 ||
                rs.nav.system_menu_mode == 2)) {
      const int cur_pg = (rs.nav.system_menu_mode == 0)
                             ? rs.nav.system_dashboard_page
                             : ((rs.nav.system_menu_mode == 1) ? rs.nav.sleep_menu_page
                                                               : rs.nav.display_menu_page);
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        const int target_pg = (cur_pg == 1) ? 2 : 1;
        rs.nav.menu_scroll_active = true;
        rs.nav.menu_scroll_dir = (target_pg == 2) ? 1 : -1;
        rs.nav.menu_scroll_target_page = target_pg;
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      } else if (rs.nav.system_menu_mode == 0 &&
                 wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int slot = hit->param;
        const int item_idx = (rs.nav.system_dashboard_page == 1) ? slot : (slot + 3);
        if (item_idx == 0) { rs.nav.system_menu_mode = 2; rs.nav.display_menu_page = 1; }
        else if (item_idx == 1) { rs.nav.system_menu_mode = 1; rs.nav.sleep_menu_page = 1; }
        else if (item_idx == 2) { rs.nav.system_menu_mode = 7; }
        else if (item_idx == 3) { rs.nav.system_menu_mode = 11; }
        else if (item_idx == 4) { rs.nav.system_menu_mode = 9; rs.nav.audio_menu_parent_page = PageId::SYSTEM; }
        else if (item_idx == 5) {
          if (prefs.home_screen_enabled) {
            rs.nav.gui_page = PageId::HOME;
            rs.nav.home_subpage = 1;
            rs.nav.home_settings_page = 1;
          }
        }
        else if (item_idx == 6) { rs.nav.system_menu_mode = 3; }
        else if (item_idx == 7) { rs.nav.system_menu_mode = 8; }
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (rs.nav.system_menu_mode == 1 &&
                 wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int slot_idx = hit->param;
        const int idx = (rs.nav.sleep_menu_page == 1) ? slot_idx : (slot_idx + 5);
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
        } else if (idx == 4) {
          if (rt_.select_opt(UiSelectId::SCREENSAVER_MODE) != "Never") {
            rt_.set_select_opt(UiSelectId::SCREENSAVER_STYLE,
                               cycle_option(rt_.select_opt(UiSelectId::SCREENSAVER_STYLE),
                                            {"Ambient Clock", "Photo Frame", "Aurora",
                                             "Starfield", "Digital Rain", "Fireworks"}));
          }
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
#if LATTER_HAS_BATTERY
          rt_.sw_toggle(UiSwitchId::INSTANT_CROWN_HIBERNATE);
#endif
        } else if (idx == 8) {
          rt_.sw_toggle(UiSwitchId::BEDTIME_MODE);
        } else if (idx == 9) {
          rt_.set_select_opt(UiSelectId::BEDTIME_DISPLAY,
                             cycle_option(rt_.select_opt(UiSelectId::BEDTIME_DISPLAY),
                                          {"Screen Off", "Ambient Clock"}));
        }
        rt_.schedule_disp();
      } else if (rs.nav.system_menu_mode == 2 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        if (wid >= WidgetId::COLOR_SWATCH_BASE && wid < WidgetId::COLOR_SWATCH_BASE + 10) {
          rs.nav.system_menu_mode = 13;
          mark_dirty_full();
          rt_.schedule_disp();
        } else if (wid == WidgetId::SYS_BRI_SLIDER) {
          UiNumberId nid = UiNumberId::MANUAL_BRIGHTNESS;
          if (resolve_sys_bri_slider_id(hit->param, rt_.select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE), nid)) {
            const int cur_pct = static_cast<int>(std::lround(rt_.num_val(nid)));
            const int next_pct = step_brightness_tap_10pct(cur_pct);
            rt_.set_num_val(nid, static_cast<float>(next_pct));
            rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
            rt_.schedule_disp();
          }
        } else if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
          const int slot_idx = hit->param;
          const int item_idx = (rs.nav.display_menu_page == 1) ? slot_idx : (slot_idx + 5);
          const std::string_view ab = rt_.select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE);
          if (item_idx == 0) {
            if (prefs.home_screen_enabled) {
              rt_.set_select_opt(UiSelectId::DEFAULT_START_APP,
                                 cycle_option(rt_.select_opt(UiSelectId::DEFAULT_START_APP),
                                              {"Home", "Pinned", "Time App", "Calendar", "Clock Face", "Media", "Home Hub",
                                               "Area Controls", "Remote", "Weather", "Cameras",
                                               "Games", "Tools"}));
            } else {
              rt_.set_select_opt(UiSelectId::DEFAULT_START_APP,
                                 cycle_option(rt_.select_opt(UiSelectId::DEFAULT_START_APP),
                                              {"Pinned", "Time App", "Calendar", "Clock Face", "Media", "Home Hub",
                                               "Area Controls", "Remote", "Weather", "Cameras",
                                               "Games", "Tools"}));
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
                cycle_option(ab, {"Disabled", "Sun Synchronized", "Time Schedule"});
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

    // 7b. Page 4 System Confirm Modals (Mode 4 Reboot / Mode 6 Firmware Update — release-triggered)
    } else if (tap_valid && rs.nav.gui_page == PageId::SYSTEM &&
               !rs.nav.menu_scroll_active &&
               (rs.nav.system_menu_mode == 4 || rs.nav.system_menu_mode == 6)) {
      if (wid == WidgetId::CONFIRM_CANCEL && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.nav.system_menu_mode = (rs.nav.system_menu_mode == 4) ? 3 : 5;
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (wid == WidgetId::CONFIRM_ACCEPT && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rt_.cmd((rs.nav.system_menu_mode == 4) ? UiHostCmd::RESTART_DEVICE
                                               : UiHostCmd::TRIGGER_FIRMWARE_UPDATE);
      }

    // 8. Page 5 Weather Mode Bar Taps
    } else if (tap_valid && rs.nav.gui_page == PageId::WEATHER) {
      if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4 &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int target_sub = std::clamp(static_cast<int>(hit->param), 0, 3);
        if (target_sub != rs.nav.sky_weather_subpage) {
          rs.nav.sky_weather_subpage = target_sub;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      }

    // 9. Page 6 Area Control Taps (Picker List & D-050 Split Toggle / Entity Inspector Cards)
    } else if (tap_valid && rs.nav.gui_page == PageId::AREA) {
      if (rs.nav.area_subpage == AreaSubpage::PICKER || rs.area.browse_mode == "areas") {
        int &a_pg = rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST);
        const int a_tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::AREA_LIST);
        if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) && a_tot > 1 &&
            touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
          const int next_pg =
              std::clamp(a_pg + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, a_tot);
          if (next_pg != a_pg) {
            a_pg = next_pg;
            do_fetch_area_list_browse();
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
            do_fetch_area_browse();
            mark_dirty_full();
            rt_.schedule_disp();
          }
        }
      } else if (rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL &&
                 light_insp_domain() == "climate") {
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
              if (hub.thermostats[ti].entity_id == ci->entity_id) {
                hub.thermostats[ti].target_temp = next_t;
              }
            }
          }
          light_insp_bri_pct() = next_t;
          if (sl_i >= 0 && sl_i < 3) {
            rs.area_entity_slots()[sl_i].level = next_t;
          }
          ++hub.local_mutation_seq;
          rs.area.target_entity_id = light_insp_entity_id();
          rs.area.target_action = "set_temperature";
          rs.area.target_level = next_t;
          char fb[36];
          std::snprintf(fb, sizeof(fb), "Setpoint -> %d\xC2\xB0%c", next_t, is_c ? 'C' : 'F');
          rs.area.action_feedback = fb;
          rs.area.feedback_timer = now_rel;
          do_execute_area_action();
          rt_.schedule_disp();
        } else if (wid >= WidgetId::CLIMATE_MODE_BASE && wid < WidgetId::CLIMATE_MODE_BASE + 6 &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          static constexpr const char *kClimModes[6] = {
              "heat", "cool", "heat_cool", "off", "fan_only", "dry"};
          static constexpr const char *kClimLabels[6] = {
              "Heat", "Cool", "Auto", "Off", "Fan", "Dry"};
          const int m_i = static_cast<int>(wid - WidgetId::CLIMATE_MODE_BASE);
          const bool next_on = (m_i != 3);
          if (ci != nullptr) {
            ci->mode = kClimModes[m_i];
            for (int ti = 0; ti < hub.thermostat_count; ++ti) {
              if (hub.thermostats[ti].entity_id == ci->entity_id) {
                hub.thermostats[ti].mode = kClimModes[m_i];
              }
            }
          }
          light_insp_active() = next_on;
          if (sl_i >= 0 && sl_i < 3) {
            rs.area_entity_slots()[sl_i].active = next_on;
          }
          ++hub.local_mutation_seq;
          rs.area.target_entity_id = light_insp_entity_id();
          rs.area.target_action = std::string("hvac_") + kClimModes[m_i];
          rs.area.target_level = (ci != nullptr && ci->target_temp > 0)
                                     ? ci->target_temp
                                     : light_insp_bri_pct();
          rs.area.action_feedback = std::string("HVAC: ") + kClimLabels[m_i];
          rs.area.feedback_timer = now_rel;
          mark_dirty_full();
          do_execute_area_action();
          rt_.schedule_disp();
        }
      } else if (rs.nav.area_subpage == AreaSubpage::OVERVIEW) {
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
              do_fetch_area_browse();
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
            do_fetch_area_browse();
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
            light_insp_mode() = 0;  // HSV Color Wheel mode
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
            const bool is_card_body =
                (wid >= WidgetId::AREA_CARD_TAP && wid < WidgetId::AREA_CARD_TAP + 3);
            if (is_card_body &&
                (sl.type == "light" || sl.type == "cover" || sl.type == "fan" ||
                 sl.type == "climate")) {
              // D-050 / D-060: Right body opens generalized Entity Inspector (Brightness / Position / Speed / Climate)
              light_insp_entity_id() = sl.id;
              light_insp_name() = strip_trailing_paren_state(sl.name);
              light_insp_domain() = sl.type;
              light_insp_slot() = idx;
              light_insp_active() = sl.active;
              light_insp_mode() = 1;  // Level / White Temp / Climate mode
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
              launch_dock_app(PageId::MUSIC, rs, rt_, now_rel);
            } else {
              rs.area.target_entity_id = sl.id;
              const bool prev_act = sl.active;
              const bool next_act = (sl.type == "scene") ? true : !sl.active;
              sl.active = next_act;
              std::string verb = "toggle";
              if (sl.type == "scene") verb = "turn_on";
              else if (sl.type == "cover") verb = next_act ? "open_cover" : "close_cover";
              else if (sl.type == "lock" || sl.id.rfind("lock.", 0) == 0) verb = next_act ? "unlock" : "lock";
              else verb = next_act ? "turn_on" : "turn_off";
              rs.area.action_feedback = strip_trailing_paren_state(sl.name);
              if (!gate_or_dispatch_area_action(sl.id, verb, next_act ? 100 : 0,
                                                "PERIMETER PIN", now_rel)) {
                sl.active = prev_act;
              }
            }
          }
        }
      }

    // 10. Page 7 Remote Release Taps (Subpage 0 Long-Press-Capable Keys & Subpage 1 Slot/Power)
    } else if (tap_valid && rs.nav.gui_page == PageId::REMOTE) {
      if (rs.nav.remote_subpage == 0) {
        const int remote_key = remote_key_for_widget(wid);
        if (remote_key >= 0 && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
          const int act_slot = resolve_active_remote_slot(rs.remote.active_slot);
          rs.remote.active_slot = act_slot;
          do_remote_send_key(remote_key, act_slot, /*long_press=*/false, now_rel);
          rt_.schedule_band_disp(0, std::min(SCREEN_H - 1, layout().hdr_y + scale_y(24)));
        }
      } else if (rs.nav.remote_subpage == 1) {
        if (wid == WidgetId::REMOTE_PRIMARY_PWR &&
            touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          do_remote_send_key(9, -1, false, now_rel);
          rt_.schedule_disp();
        } else if (wid >= WidgetId::REMOTE_SEC_PWR && wid < WidgetId::REMOTE_SEC_PWR + 4 &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          do_remote_send_key(9, hit->param, false, now_rel);
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

    // 11. Page 8 Tools Launcher & Calendar Release Taps
    } else if (tap_valid && rs.nav.gui_page == PageId::TOOLS) {
      if (rs.nav.tools_subpage == ToolsSubpage::LAUNCHER && !rs.nav.menu_scroll_active) {
        const int total_pages = active_tools_launcher_page_count(device_prefs());
        if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
            touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
          if (total_pages > 1) {
            const int cur_pg = std::clamp(rs.nav.tools_menu_page, 0, total_pages - 1);
            rs.nav.tools_menu_page = cur_pg;
            const int step_dir = (wid == WidgetId::CHEVRON_DOWN) ? 1 : -1;
            const int target_pg = std::clamp(cur_pg + step_dir, 0, total_pages - 1);
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
            if (target_sub == ToolsSubpage::FLASHLIGHT) {
              rt_.cmd(UiHostCmd::FLASHLIGHT_FULL_BRIGHTNESS);
            } else if (target_sub == ToolsSubpage::CALCULATOR) {
              reset_calculator_state();
            }
            rt_.apply_power_policy();
            mark_dirty_full();
            rt_.schedule_disp();
          }
        }
      }

    // 11B. Page 11 Pinned App Release Taps & Chevrons
    } else if (tap_valid && rs.nav.gui_page == PageId::PINNED && !rs.nav.menu_scroll_active) {
      const int total_pages = pinned_total_pages();
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        if (total_pages > 1) {
          const int cur_pg = std::clamp(rs.nav.pinned_menu_page, 0, total_pages - 1);
          rs.nav.pinned_menu_page = cur_pg;
          const int step_dir = (wid == WidgetId::CHEVRON_DOWN) ? 1 : -1;
          const int target_pg = std::clamp(cur_pg + step_dir, 0, total_pages - 1);
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
          activate_shortcut_item(item_copy, ShortcutOrigin::PINNED_APP, rs, rt_, now_rel);
        }
      }

    // 12. Page 9 Home Hub Controls (Alarm, Intercom, Thermostat, Entry, Vacuum)
    } else if (tap_valid && rs.nav.gui_page == PageId::HOME_HUB) {
      {
        int hh_tabs[kMaxHomeHubTabs]{};
        const int hh_tab_cnt = collect_home_hub_tabs(hh_tabs);
        if (hh_tab_cnt <= 0) {
          // No Home Controls configured: ignore taps on empty state.
        } else if ((wid == WidgetId::MODE_BAR_LEFT || wid == WidgetId::MODE_BAR_RIGHT) &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
          if (hh_tab_cnt > 4) {
            home_hub_mode_bar_page() = std::clamp(
                home_hub_mode_bar_page() + ((wid == WidgetId::MODE_BAR_RIGHT) ? 4 : -4),
                0, hh_tab_cnt - 4);
            rt_.schedule_disp();
          }
        } else if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4 &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const int slot = std::clamp(static_cast<int>(hit->param), 0, hh_tab_cnt - 1);
          const int target_tab = hh_tabs[slot];
          if (target_tab != home_hub_tab()) {
            enter_home_hub_tab(target_tab, false, rs, rt_, now_rel);
            mark_dirty_full();
            rt_.schedule_disp();
          }
        } else if (home_hub_tab() == HomeHubTab::ALARM &&
                   !area_ctx_state().alarm_entity.empty() &&
                   wid >= WidgetId::ALARM_MODE_BASE && wid < WidgetId::ALARM_MODE_BASE + 5 &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const int mode_i = static_cast<int>(wid - WidgetId::ALARM_MODE_BASE);
          static constexpr const char *kAlarmActs[5] = {
              "disarm",
              "arm_home",
              "arm_away",
              "arm_night",
              "arm_vacation",
          };
          static constexpr const char *kAlarmFb[5] = {
              "Disarming System...",
              "Arming Home...",
              "Arming Away...",
              "Arming Night...",
              "Arming Vacation...",
          };
          rs.area.action_feedback = kAlarmFb[mode_i];
          gate_or_dispatch_alarm_action(kAlarmActs[mode_i], now_rel);
        } else if (home_hub_tab() == HomeHubTab::INTERCOM) {
          if (wid == WidgetId::INTERCOM_SEG_BROADCAST &&
              touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
            if (intercom_live_call_mode()) {
              intercom_live_call_mode() = false;
              mark_dirty_full();
              rt_.schedule_disp();
            }
          } else if (wid == WidgetId::INTERCOM_SEG_CALL &&
                     touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
            if (!intercom_live_call_mode()) {
              intercom_live_call_mode() = true;
              mark_dirty_full();
              rt_.schedule_disp();
            }
          } else if (wid == WidgetId::INTERCOM_MODE_BTN &&
                     touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
            intercom_live_call_mode() = !intercom_live_call_mode();
            mark_dirty_full();
            rt_.schedule_disp();
          } else if (wid == WidgetId::CAM_LISTEN_BTN &&
                     touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
            if (cam_call_active()) {
              intercom_session().stop_call_with_hw(now_rel);
              mark_dirty_full();
              rt_.schedule_disp();
            }
          } else if (wid == WidgetId::INTERCOM_UNLOCK_BTN &&
                     touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
            gate_or_dispatch_doorbell_unlock(now_rel, true);
          } else if (wid >= WidgetId::INTERCOM_ROW_BASE && wid < WidgetId::INTERCOM_ROW_BASE + 5 &&
                     touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
            const int slot_idx = static_cast<int>(wid - WidgetId::INTERCOM_ROW_BASE);
            if (!intercom_live_call_mode()) {
              static constexpr const char *kBroadcastPresets[3] = {
                  "Dinner is ready!",
                  "Come upstairs!",
                  "Leaving soon!",
              };
              if (slot_idx >= 0 && slot_idx < 3) {
                rs.intercom.broadcast_message = kBroadcastPresets[slot_idx];
                rs.intercom.broadcast_feedback = std::string("Sent: ") + kBroadcastPresets[slot_idx];
                rs.intercom.broadcast_feedback_ms = now_rel;
                do_send_intercom_broadcast();
                rt_.schedule_disp();
              }
            } else {
              std::string t_ids[5], t_lbls[5];
              const int cnt = parse_intercom_targets_list(
                  area_ctx_state().intercom_targets_summary, t_ids, t_lbls);
              if (slot_idx >= 0 && slot_idx < cnt) {
                rs.intercom.cam_action = "call_target";
                rs.intercom.cam_message = t_ids[slot_idx];
                rs.intercom.broadcast_feedback = "Call: " + t_lbls[slot_idx];
                rs.intercom.broadcast_feedback_ms = now_rel;
                intercom_session().emit_current_cmd();
                intercom_session().start_call(now_rel);
                mark_dirty_full();
                rt_.schedule_disp();
              }
            }
          }
        } else if (home_hub_tab() == HomeHubTab::THERMOSTAT &&
                   home_hub_state().thermostat_count > 0) {
          auto &hub = home_hub_state();
          int &z_idx = hub.thermostat_idx;
          z_idx = std::clamp(z_idx, 0, hub.thermostat_count - 1);
          if ((wid == WidgetId::THERMOSTAT_PREV_BTN || wid == WidgetId::THERMOSTAT_NEXT_BTN) &&
              touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
            if (hub.thermostat_count > 1) {
              z_idx = step_wrap(z_idx, (wid == WidgetId::THERMOSTAT_NEXT_BTN) ? 1 : -1,
                                hub.thermostat_count);
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
              if (hub.area_climates[ai].entity_id == z.entity_id) {
                hub.area_climates[ai].target_temp = z.target_temp;
              }
            }
            ++hub.local_mutation_seq;
            rs.area.target_entity_id = z.entity_id;
            rs.area.target_action = "set_temperature";
            rs.area.target_level = z.target_temp;
            char fb[36];
            std::snprintf(fb, sizeof(fb), "Setpoint -> %d\xC2\xB0%c", z.target_temp, is_c ? 'C' : 'F');
            rs.area.action_feedback = fb;
            rs.area.feedback_timer = now_rel;
            do_execute_area_action();
            rt_.schedule_disp();
          } else if (wid >= WidgetId::CLIMATE_MODE_BASE && wid < WidgetId::CLIMATE_MODE_BASE + 6 &&
                     touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
            static constexpr const char *kClimModes[6] = {
                "heat", "cool", "heat_cool", "off", "fan_only", "dry"};
            static constexpr const char *kClimLabels[6] = {
                "Heat", "Cool", "Auto", "Off", "Fan", "Dry"};
            const int m_i = static_cast<int>(wid - WidgetId::CLIMATE_MODE_BASE);
            auto &z = hub.thermostats[z_idx];
            z.mode = kClimModes[m_i];
            for (int ai = 0; ai < hub.area_climate_count; ++ai) {
              if (hub.area_climates[ai].entity_id == z.entity_id) {
                hub.area_climates[ai].mode = kClimModes[m_i];
              }
            }
            ++hub.local_mutation_seq;
            rs.area.target_entity_id = z.entity_id;
            rs.area.target_action = std::string("hvac_") + kClimModes[m_i];
            rs.area.target_level = z.target_temp;
            rs.area.action_feedback = std::string("HVAC: ") + kClimLabels[m_i];
            rs.area.feedback_timer = now_rel;
            mark_dirty_full();
            do_execute_area_action();
            rt_.schedule_disp();
          }
        } else if (home_hub_tab() == HomeHubTab::ENTRY &&
                   home_hub_state().entry_count > 0) {
          auto &hub = home_hub_state();
          if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
              touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
            const int max_pg = std::max(0, (hub.entry_count - 1) / 2);
            hub.entry_page = std::clamp(
                hub.entry_page + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 0, max_pg);
            ++hub.local_mutation_seq;
            rt_.schedule_disp();
          } else if (((wid >= WidgetId::ENTRY_CARD_BASE && wid < WidgetId::ENTRY_CARD_BASE + 2) ||
                      (wid >= WidgetId::ENTRY_ACTION_BASE && wid < WidgetId::ENTRY_ACTION_BASE + 2)) &&
                     touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
            const int e_idx = (hit != nullptr) ? static_cast<int>(hit->param) : -1;
            if (e_idx >= 0 && e_idx < hub.entry_count) {
              auto &item = hub.entries[e_idx];
              if (item.is_lock) {
                const LockState prev_l = item.lock_state;
                const bool want_lock = (prev_l == LockState::UNLOCKED);
                item.lock_state = want_lock ? LockState::LOCKED : LockState::UNLOCKED;
                ++hub.local_mutation_seq;
                rs.area.target_entity_id = item.entity_id;
                rs.area.action_feedback =
                    (!item.name.empty() ? item.name : std::string("Door Lock")) +
                    (want_lock ? ": Locked" : ": Unlocked");
                if (!gate_or_dispatch_area_action(item.entity_id, want_lock ? "lock" : "unlock",
                                                  want_lock ? 100 : 0, "UNLOCK DOOR PIN", now_rel)) {
                  item.lock_state = prev_l;
                  ++hub.local_mutation_seq;
                }
              } else {
                const GarageState prev_g = item.garage_state;
                const bool want_open = (prev_g != GarageState::OPEN);
                item.garage_state = want_open ? GarageState::OPEN : GarageState::CLOSED;
                ++hub.local_mutation_seq;
                rs.area.target_entity_id = item.entity_id;
                const std::string g_lbl = !item.name.empty() ? item.name : std::string("Garage Door");
                rs.area.action_feedback =
                    (want_open ? "Opening " : "Closing ") + g_lbl + "...";
                if (!gate_or_dispatch_area_action(item.entity_id, want_open ? "open_cover" : "close_cover",
                                                  want_open ? 100 : 0, "GARAGE DOOR PIN", now_rel)) {
                  item.garage_state = prev_g;
                  ++hub.local_mutation_seq;
                }
              }
            }
          }
        } else if (home_hub_tab() == HomeHubTab::VACUUM &&
                   home_hub_state().vacuum_count > 0) {
          auto &hub = home_hub_state();
          int &v_idx = hub.vacuum_idx;
          v_idx = std::clamp(v_idx, 0, hub.vacuum_count - 1);
          if ((wid == WidgetId::VACUUM_PREV_BTN || wid == WidgetId::VACUUM_NEXT_BTN) &&
              touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
            if (hub.vacuum_count > 1) {
              v_idx = step_wrap(v_idx, (wid == WidgetId::VACUUM_NEXT_BTN) ? 1 : -1,
                                hub.vacuum_count);
              ++hub.local_mutation_seq;
              mark_dirty_full();
              rt_.schedule_disp();
            }
          } else if (wid >= WidgetId::VACUUM_ACTION_BASE && wid < WidgetId::VACUUM_ACTION_BASE + 6 &&
                     touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
            auto &v = hub.vacuums[v_idx];
            const int v_act = static_cast<int>(wid - WidgetId::VACUUM_ACTION_BASE);
            rs.area.target_entity_id = v.entity_id;
            if (v_act == 0) {
              v.state = "Cleaning";
              rs.area.target_action = "start";
              rs.area.action_feedback = "Vacuum Started";
            } else if (v_act == 1) {
              v.state = "Paused";
              rs.area.target_action = "pause";
              rs.area.action_feedback = "Vacuum Paused";
            } else if (v_act == 2) {
              v.state = "Returning";
              rs.area.target_action = "return_to_base";
              rs.area.action_feedback = "Returning to Dock";
            } else if (v_act == 3) {
              v.state = "Idle";
              rs.area.target_action = "stop";
              rs.area.action_feedback = "Vacuum Stopped";
            } else if (v_act == 4) {
              v.state = "Cleaning";
              rs.area.target_action = "clean_spot";
              rs.area.action_feedback = "Spot Cleaning";
            } else {
              rs.area.target_action = "locate";
              rs.area.action_feedback = "Locating Vacuum...";
            }
            ++hub.local_mutation_seq;
            rs.area.feedback_timer = now_rel;
            mark_dirty_full();
            do_execute_area_action();
            rt_.schedule_disp();
          }
        }
      }

    // 14. Page 10 Consolidated 4-Tab Time App (Mode Bar, World Clocks, Timers & Alarms Overviews)
    } else if (tap_valid && rs.nav.gui_page == PageId::TIME) {
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
          rt_.schedule_disp();
        } else if (wid >= WidgetId::ALARM_TOGGLE_BASE && wid < WidgetId::ALARM_TOGGLE_BASE + 3 &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const int a_idx = std::clamp(static_cast<int>(hit->param), 0, 2);
          prefs.alarms[a_idx].enabled = !prefs.alarms[a_idx].enabled;
          if (!prefs.alarms[a_idx].enabled && rs.alarms.ringing_idx == a_idx + 1) {
            rs.alarms.snooze_until = 0;
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

    // 15. Page 12 Dedicated 4-Tab Calendar App Release Taps
    } else if (tap_valid && rs.nav.gui_page == PageId::CALENDAR) {
      const int32_t today_ymd = resolve_touch_today_ymd(rt_);
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
      } else if (rs.cal.drilldown == CalDrilldown::MAP) {
        if (wid == WidgetId::CAL_ZOOM_OUT_BTN && rs.cal.map_zoom > 10 &&
            touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
          rs.cal.map_zoom--;
          rs.cal.map_pan_x /= 2;
          rs.cal.map_pan_y /= 2;
          rt_.schedule_disp();
          rt_.cmd(UiHostCmd::FETCH_CALENDAR_MAP_SNAPSHOT);
        } else if (wid == WidgetId::CAL_ZOOM_IN_BTN && rs.cal.map_zoom < 18 &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
          rs.cal.map_zoom++;
          rs.cal.map_pan_x = std::clamp(rs.cal.map_pan_x * 2, -4, 4);
          rs.cal.map_pan_y = std::clamp(rs.cal.map_pan_y * 2, -4, 4);
          rt_.schedule_disp();
          rt_.cmd(UiHostCmd::FETCH_CALENDAR_MAP_SNAPSHOT);
        } else if (wid == WidgetId::CAL_TODAY_BTN &&
                   (rs.cal.map_pan_x != 0 || rs.cal.map_pan_y != 0) &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          rs.cal.map_pan_x = 0;
          rs.cal.map_pan_y = 0;
          rt_.cmd(UiHostCmd::FETCH_CALENDAR_MAP_SNAPSHOT);
          rt_.schedule_disp();
        }
      } else if (rs.cal.drilldown == CalDrilldown::DETAIL) {
        if (wid == WidgetId::CAL_VIEW_MAP_BTN &&
            rs.cal.selected_event_idx >= 0 && rs.cal.selected_event_idx < store.count &&
            touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const auto &ev = store.events[rs.cal.selected_event_idx];
          if (ev.location[0] != '\0') {
            rs.cal.detail_map_location = ev.location;
            rs.cal.detail_map_url.clear();
            rs.cal.map_zoom = 15;
            rs.cal.map_pan_x = 0;
            rs.cal.map_pan_y = 0;
            rs.cal.drilldown = CalDrilldown::MAP;
            rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
            rs.alert.loaded_camera_url.clear();
            latter_actions::emit_calendar_fetch_map(ev.location);
            mark_dirty_full();
            rt_.schedule_disp();
          }
        } else if (wid == WidgetId::CAL_DELETE_BTN &&
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
        } else if (wid == WidgetId::CAL_ADD_TITLE_BTN &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          stop_calendar_dictation_if_active(rs, rt_, now_rel);
          rs.cal.picker_page = 0;
          rs.cal.drilldown = CalDrilldown::PICK_TITLE;
          mark_dirty_full();
          rt_.schedule_disp();
        } else if (wid == WidgetId::CAL_ADD_LOC_BTN &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          stop_calendar_dictation_if_active(rs, rt_, now_rel);
          rs.cal.picker_page = 0;
          rs.cal.drilldown = CalDrilldown::PICK_LOCATION;
          mark_dirty_full();
          rt_.schedule_disp();
        } else if (wid == WidgetId::CAL_ADD_CAL_BTN &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          stop_calendar_dictation_if_active(rs, rt_, now_rel);
          rs.cal.picker_page = 0;
          rs.cal.drilldown = CalDrilldown::PICK_CALENDAR;
          mark_dirty_full();
          rt_.schedule_disp();
        } else if (wid == WidgetId::CAL_ADD_REPEAT_BTN &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          stop_calendar_dictation_if_active(rs, rt_, now_rel);
          rs.cal.picker_page = 0;
          rs.cal.drilldown = CalDrilldown::PICK_REPEAT;
          mark_dirty_full();
          rt_.schedule_disp();
        } else if (wid == WidgetId::CAL_ADD_NEXT_BTN &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          stop_calendar_dictation_if_active(rs, rt_, now_rel);
          rs.cal.drilldown = CalDrilldown::ADD_STEP2;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (rs.cal.drilldown == CalDrilldown::ADD_STEP2) {
        if (wid == WidgetId::CAL_ADD_ALLDAY_BTN &&
            touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          rs.cal.draft_all_day = !rs.cal.draft_all_day;
          mark_dirty_full();
          rt_.schedule_disp();
        } else if (wid == WidgetId::CAL_ADD_START_BTN &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
          rs.cal.draft_time_field = 0U;
          rt_.schedule_disp();
        } else if (wid == WidgetId::CAL_ADD_END_BTN &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
          rs.cal.draft_time_field = 1U;
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
      } else {
        // Root 4-tab views (Overview, Day, Week, Month)
        if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4 &&
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
          if (rs.cal.subpage == CalendarSubpage::OVERVIEW) {
            rs.cal.overview_page = std::max(0, rs.cal.overview_page + step_d);
          } else if (rs.cal.subpage == CalendarSubpage::DAY) {
            rs.cal.day_page = std::max(0, rs.cal.day_page + step_d);
          } else if (rs.cal.subpage == CalendarSubpage::WEEK) {
            rs.cal.week_page = std::max(0, rs.cal.week_page + step_d);
          }
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
          // Prefer a date inside the displayed month when the week straddles the 1st
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
      }

    // 15. Page 13 Dedicated 4-Tab Cameras App (Grid, Single, Carousel, Settings)
    } else if (tap_valid && rs.nav.gui_page == PageId::CAMERAS) {
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
          gate_or_dispatch_doorbell_unlock(now_rel, false);
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
          const int c_idx = std::clamp(static_cast<int>(hit->param), 0, c_total - 1);
          rs.cameras.selected_idx = c_idx;
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
        if (wid >= WidgetId::CAM_SETTINGS_ROW_BASE && wid < WidgetId::CAM_SETTINGS_ROW_BASE + 5 &&
            touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const int r_idx = static_cast<int>(wid - WidgetId::CAM_SETTINGS_ROW_BASE);
          auto cycle_opt = [&]<size_t N>(UiSelectId sid, const char *const (&opts)[N], uint8_t cur_val) {
            const uint8_t next_v = static_cast<uint8_t>((cur_val + 1U) % static_cast<uint8_t>(N));
            rt_.set_select_opt(sid, opts[next_v], true);
          };
          if (r_idx == 0) {
            cycle_opt(UiSelectId::CAMERA_DEFAULT_VIEW, kCameraDefaultViewOpts, prefs.camera_default_view);
          } else if (r_idx == 1) {
            cycle_opt(UiSelectId::CAMERA_CAROUSEL_INTERVAL, kCameraCarouselIntervalOpts, prefs.camera_carousel_interval);
          } else if (r_idx == 2) {
            cycle_opt(UiSelectId::CAMERA_FRAMING, kCameraFramingOpts, prefs.camera_framing);
            rs.cameras.framing_override = -1;
          } else if (r_idx == 3) {
            cycle_opt(UiSelectId::CAMERA_STREAM_MODE, kCameraStreamModeOpts, prefs.camera_stream_mode);
          } else if (r_idx == 4 && LATTER_CHIP_P4) {
            cycle_opt(UiSelectId::CAMERA_DOORBELL_POPUP, kCameraDoorbellPopupOpts, prefs.camera_doorbell_popup);
          }
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
        // Single Live View
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
        } else if (wid == WidgetId::CAM_FRAMING_BTN &&
                   touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const bool next_fill = !rs.cameras.effective_fill(prefs);
          rs.cameras.framing_override = next_fill ? 1 : 0;
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
          gate_or_dispatch_doorbell_unlock(now_rel, false);
        }
      }
    }
  }

  if (rel_t0_us > 0ULL && profiler_enabled()) {
    const uint32_t rel_exec_us = static_cast<uint32_t>(prof_now_us() - rel_t0_us);
    const int end_ctx = compute_touch_context(rs);
    const uint32_t dur_p = (now_rel >= gest.start_ms) ? (now_rel - gest.start_ms) : 0U;
    LATTER_PROF_LOGI(
        "[PROF:TOUCH_UP] start=(%d,%d) end=(%d,%d) dx=%d dy=%d travel=%d samples=%d dur=%lums swipe=%d ctx=%d->%d exec=%luus",
        gest.start_x, gest.start_y, gest.last_x, gest.last_y,
        dx, dy, gest.max_travel, gest.sample_count,
        static_cast<unsigned long>(dur_p), static_cast<int>(gest.swipe_committed),
        gest.start_context, end_ctx, static_cast<unsigned long>(rel_exec_us));
  }
  cancel_core_timeout(kTimeoutTouchLongPressId);
  const uint16_t lp_wid = gest.long_press_wid;
  gest.active = false;
  gest.long_press_armed = false;
  if (lp_wid != 0U) {
    if (rs.nav.gui_page == PageId::REMOTE && rs.nav.remote_subpage == 0) {
      const HitRegion *lp_reg = frame_hits().find_by_id(lp_wid);
      if (lp_reg != nullptr) {
        rt_.schedule_band_disp(std::max(0, static_cast<int>(lp_reg->y0) - 6),
                               std::min(SCREEN_H - 1, static_cast<int>(lp_reg->y1) + 6));
      } else {
        rt_.schedule_disp();
      }
    } else {
      rt_.schedule_disp();
    }
  }
}

void TouchGestureEngine::on_input_event(const std::string &action, int delta, uint32_t now_in) {
  auto &rs = runtime_state();

  if (rs.nav.ota_in_progress) return;

  if (rs.nav.display_power_state != 0 || !rt_.panel_is_on) {
    note_wake_reason(WakeReason::TOUCH);
    rt_.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    return;
  }

  display_lifecycle().note_activity(now_in);
  last_user_touch_ms() = now_in;
  activate_ui_focus(now_in);

  // Voice Assistant cancels immediately on any hardware input
  if (!rs.alarms.ringing && !rs.timers.ringing && !rs.alarms.find_my_active &&
      (rs.voice.active || !rs.voice.status_text.empty())) {
    rt_.cmd(UiHostCmd::CANCEL_VOICE_ASSISTANT);
    return;
  }

  std::string act = action;
  for (char &c : act) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  int step = delta;
  if (step == 0) {
    if (act == "next" || act == "right" || act == "down" || act == "cw") step = 1;
    else if (act == "prev" || act == "left" || act == "up" || act == "ccw") step = -1;
  }

  if (act == "home") {
    if (can_go_home(rs)) {
      ui_focus_index() = 0;
      navigate_home(now_in);
    }
    return;
  }

  if (act == "back") {
    if (ui::pin_pad_state().open) {
      ui::pin_pad_scrub();
      ui_focus_index() = 0;
      mark_dirty_full();
      rt_.schedule_disp();
      return;
    }
    if (shortcut_confirm_state().open) {
      shortcut_confirm_state().clear();
      ui_focus_index() = 0;
      mark_dirty_full();
      rt_.schedule_disp();
      return;
    }
    if (rs.nav.gui_page == PageId::MUSIC && handle_music_back_step(rs, rt_, now_in)) {
      ui_focus_index() = 0;
      activate_ui_focus(now_in);
      return;
    }
    const HitRegion *back_reg = frame_hits().find_by_id(WidgetId::BACK_BUTTON);
    if (back_reg != nullptr) {
      on_touch_down(back_reg->cx(), back_reg->cy(), now_in);
      if (touch_gesture().active) on_touch_up(now_in);
      ui_focus_index() = 0;
      activate_ui_focus(now_in);
      return;
    }
    if (can_go_home(rs)) {
      ui_focus_index() = 0;
      navigate_home(now_in);
    }
    return;
  }

  const int focus_cnt = frame_hits().focus_count();

  if (step != 0) {
    if (!ui::pin_pad_state().open && !rs.alarms.ringing && !rs.timers.ringing &&
        !rs.alarms.find_my_active && !rs.alert.active) {
      if (rs.nav.gui_page == PageId::WEATHER) {
        rs.nav.sky_weather_subpage = step_wrap(rs.nav.sky_weather_subpage, step, 4);
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (rs.nav.gui_page == PageId::TIME &&
          rs.nav.time_subpage == TimeSubpage::ALARMS &&
          rs.alarms.edit_idx >= 1 && rs.alarms.edit_idx <= 3) {
        auto &ap = persisted_prefs().alarms[rs.alarms.edit_idx - 1];
        if (rs.nav.alarm_edit_field == 0) {
          ap.hour = static_cast<uint8_t>((static_cast<int>(ap.hour) + 24 + (step % 24)) % 24);
        } else {
          ap.minute = static_cast<uint8_t>((static_cast<int>(ap.minute) + 60 + (step % 60)) % 60);
        }
        latter_actions::sync_local_alarms_edit(now_in);
        rt_.schedule_disp();
        return;
      }
      if (rs.nav.gui_page == PageId::GAMES && rs.nav.active_game == 0 && !rs.nav.menu_scroll_active) {
        const int game_cnt = active_games_launcher_count(device_prefs());
        if (game_cnt > 1) {
          const int cur_slot = std::clamp(rs.nav.game_carousel_idx, 0, game_cnt - 1);
          rs.nav.game_carousel_idx = cur_slot;
          rs.nav.menu_scroll_active = true;
          rs.nav.menu_scroll_dir = (step > 0) ? 1 : -1;
          rs.nav.menu_scroll_target_page = step_wrap(cur_slot, step, game_cnt);
          rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
          return;
        }
      }
    }
    if (focus_cnt > 0) {
      ui_focus_index() = step_wrap(ui_focus_index(), step, focus_cnt);
      rt_.schedule_disp();
    }
    return;
  }

  if (act == "select" || act == "enter") {
    if (rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active) {
      const HitRegion *foc = frame_hits().focus_at(ui_focus_index());
      if (foc != nullptr) {
        on_touch_down(foc->cx(), foc->cy(), now_in);
      } else {
        rs.alarms.ringing = false;
        rs.timers.ringing = false;
        rs.alarms.find_my_active = false;
        latter_actions::check_and_emit_timer_alarm_state(now_in);
        rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
    if (rs.alert.active) {
      const HitRegion *foc = frame_hits().focus_at(ui_focus_index());
      if (foc != nullptr) {
        on_touch_down(foc->cx(), foc->cy(), now_in);
      } else {
        rt_.cmd(UiHostCmd::DISMISS_NOTIFICATION);
      }
      return;
    }
    if (rs.nav.gui_page == PageId::TIME &&
        rs.nav.time_subpage == TimeSubpage::ALARMS &&
        rs.alarms.edit_idx >= 1 && rs.alarms.edit_idx <= 3) {
      rs.nav.alarm_edit_field = (rs.nav.alarm_edit_field == 0) ? 1 : 0;
      rt_.schedule_disp();
      return;
    }
    const HitRegion *foc = frame_hits().focus_at(ui_focus_index());
    if (foc != nullptr) {
      on_touch_down(foc->cx(), foc->cy(), now_in);
      if (touch_gesture().active) on_touch_up(now_in);
      activate_ui_focus(now_in);
    }
  }
}

// ==============================================================================
// Compiled UiTouchRuntime Preference Accessors & Mutators
// (Replaces ~195 lines of YAML function-pointer tables in modules/system_core.yaml)
// ==============================================================================
std::string_view UiTouchRuntime::select_opt(UiSelectId sid) const noexcept {
  using S = UiSelectId;
  const auto &p = persisted_prefs();
  switch (sid) {
    case S::TAP_WAKE_MODE: return pref_tap_to_wake_cstr(p);
    case S::WEATHER_SIM_CONDITION: return pref_weather_sim_cstr();
    case S::MUSIC_OVERLAY_STYLE: return pref_music_overlay_style_cstr(p);
    case S::MUSIC_FULLSCREEN_ART_MODE: return pref_album_art_style_cstr(p);
    case S::MUSIC_ALBUM_SORT_ORDER: return pref_album_sort_order_cstr(p);
    case S::MUSIC_ARTIST_ALBUM_SORT_ORDER: return pref_artist_album_sort_order_cstr(p);
    case S::MUSIC_PODCAST_SORT_ORDER: return pref_podcast_sort_order_cstr(p);
    case S::VA_WAKE_WORD_MODE: return pref_wake_word_mode_cstr(p);
    case S::VA_VISUAL_PROFILE: return pref_va_visual_profile_cstr(p);
    case S::WW_SENS_OKAY_NABU: return pref_ww_sens_cstr(p.ww_sens_okay_nabu);
    case S::WW_SENS_HEY_LUNA: return pref_ww_sens_cstr(p.ww_sens_hey_luna);
    case S::WW_SENS_HEY_JARVIS: return pref_ww_sens_cstr(p.ww_sens_hey_jarvis);
    case S::WW_SENS_HEY_MYCROFT: return pref_ww_sens_cstr(p.ww_sens_hey_mycroft);
    case S::WW_SENS_ALEXA: return pref_ww_sens_cstr(p.ww_sens_alexa);
    case S::THEME_PALETTE: return pref_color_profile_cstr(p);
    case S::PICKUP_WAKE_MODE: return pref_pickup_to_wake_cstr(p);
    case S::DISPLAY_OFF_TIMEOUT: return pref_battery_screen_timeout_cstr(p);
    case S::SCREENSAVER_MODE: return pref_screensaver_mode_cstr(p);
    case S::SCREENSAVER_STYLE: return pref_screensaver_style_cstr(p);
    case S::SCREENSAVER_DOCKED_TIMEOUT: return pref_docked_idle_timeout_cstr(p);
    case S::HIBERNATE_TIMEOUT_MODE: return pref_battery_hibernate_timeout_cstr(p);
    case S::DEFAULT_START_APP: return pref_start_screen_cstr(p);
    case S::CLOCK_FACE_STYLE: return pref_clock_face_cstr(p);
    case S::TIME_FORMAT: return pref_time_format_cstr(p);
    case S::HOME_CLOCK_TAP_ACTION: return pref_home_clock_tap_cstr(p);
    case S::HOME_DATE_TAP_ACTION: return pref_home_date_tap_cstr(p);
    case S::AUTO_BRIGHTNESS_MODE: return pref_auto_brightness_mode_cstr(p);
    case S::BEDTIME_DISPLAY: return pref_bedtime_display_cstr(p);
    case S::CAMERA_DEFAULT_VIEW: return pref_camera_default_view_cstr(p);
    case S::CAMERA_CAROUSEL_INTERVAL: return pref_camera_carousel_interval_cstr(p);
    case S::CAMERA_FRAMING: return pref_camera_framing_cstr(p);
    case S::CAMERA_STREAM_MODE: return pref_camera_stream_mode_cstr(p);
    case S::CAMERA_DOORBELL_POPUP: return pref_camera_doorbell_popup_cstr(p);
  }
  return {};
}

void UiTouchRuntime::set_select_opt(UiSelectId sid, std::string_view opt, bool /*pub*/) const {
  using S = UiSelectId;
  using K = PrefKey;
  const uint32_t now_ms = esphome::millis();
  if (sid == S::WEATHER_SIM_CONDITION) {
    const int idx = find_opt_index(kWeatherSimOpts, opt);
    if (idx >= 0) {
      device_prefs_state().weather_sim_condition = static_cast<uint8_t>(idx);
      if (opt != "Live (Home Assistant)" && !persisted_prefs().weather_background) {
        uint32_t bg_eff = 0U;
        latter_actions::record_local_pref_edit(K::WEATHER_BACKGROUND, "1", now_ms, bg_eff);
      }
      if (link_state().ha_connected) {
        std::string chg = "weather_sim_condition=";
        chg.append(opt.data(), opt.size());
        latter_actions::emit_set_device_prefs(persisted_prefs().prefs_seq, chg);
      }
      night_meteor_epoch_initialized() = false;
      weather_sim_dirty() = true;
      record_ambient_frame_ms(0U);
      cmd(UiHostCmd::NOTIFY_WEATHER_SIM_CHANGED);
    }
    return;
  }
  K pk = K::COUNT;
  switch (sid) {
    case S::TAP_WAKE_MODE: pk = K::TAP_TO_WAKE; break;
    case S::WEATHER_SIM_CONDITION: break;
    case S::MUSIC_OVERLAY_STYLE: pk = K::MUSIC_OVERLAY_STYLE; break;
    case S::MUSIC_FULLSCREEN_ART_MODE: pk = K::ALBUM_ART_STYLE; break;
    case S::MUSIC_ALBUM_SORT_ORDER: pk = K::ALBUM_SORT_ORDER; break;
    case S::MUSIC_ARTIST_ALBUM_SORT_ORDER: pk = K::ARTIST_ALBUM_SORT_ORDER; break;
    case S::MUSIC_PODCAST_SORT_ORDER: pk = K::PODCAST_SORT_ORDER; break;
    case S::VA_WAKE_WORD_MODE: pk = K::WAKE_WORD_MODE; break;
    case S::VA_VISUAL_PROFILE: pk = K::VA_VISUAL_PROFILE; break;
    case S::WW_SENS_OKAY_NABU: pk = K::WW_SENS_OKAY_NABU; break;
    case S::WW_SENS_HEY_LUNA: pk = K::WW_SENS_HEY_LUNA; break;
    case S::WW_SENS_HEY_JARVIS: pk = K::WW_SENS_HEY_JARVIS; break;
    case S::WW_SENS_HEY_MYCROFT: pk = K::WW_SENS_HEY_MYCROFT; break;
    case S::WW_SENS_ALEXA: pk = K::WW_SENS_ALEXA; break;
    case S::THEME_PALETTE: pk = K::COLOR_PROFILE; break;
    case S::PICKUP_WAKE_MODE: pk = K::PICKUP_TO_WAKE; break;
    case S::DISPLAY_OFF_TIMEOUT: pk = K::BATTERY_SCREEN_TIMEOUT; break;
    case S::SCREENSAVER_MODE: pk = K::SCREENSAVER_MODE; break;
    case S::SCREENSAVER_STYLE: pk = K::SCREENSAVER_STYLE; break;
    case S::SCREENSAVER_DOCKED_TIMEOUT: pk = K::DOCKED_IDLE_TIMEOUT; break;
    case S::HIBERNATE_TIMEOUT_MODE: pk = K::BATTERY_HIBERNATE_TIMEOUT; break;
    case S::DEFAULT_START_APP: pk = K::START_SCREEN; break;
    case S::CLOCK_FACE_STYLE: pk = K::CLOCK_FACE; break;
    case S::TIME_FORMAT: pk = K::TIME_FORMAT; break;
    case S::HOME_CLOCK_TAP_ACTION: pk = K::HOME_CLOCK_TAP_ACTION; break;
    case S::HOME_DATE_TAP_ACTION: pk = K::HOME_DATE_TAP_ACTION; break;
    case S::AUTO_BRIGHTNESS_MODE: pk = K::AUTO_BRIGHTNESS_MODE; break;
    case S::BEDTIME_DISPLAY: pk = K::BEDTIME_DISPLAY; break;
    case S::CAMERA_DEFAULT_VIEW: pk = K::CAMERA_DEFAULT_VIEW; break;
    case S::CAMERA_CAROUSEL_INTERVAL: pk = K::CAMERA_CAROUSEL_INTERVAL; break;
    case S::CAMERA_FRAMING: pk = K::CAMERA_FRAMING; break;
    case S::CAMERA_STREAM_MODE: pk = K::CAMERA_STREAM_MODE; break;
    case S::CAMERA_DOORBELL_POPUP: pk = K::CAMERA_DOORBELL_POPUP; break;
  }
  if (pk == K::COUNT) return;
  uint32_t eff = 0U;
  if (latter_actions::record_local_pref_edit(pk, opt, now_ms, eff)) {
    device_prefs_state().last_effects = eff;
    cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
  }
}

bool UiTouchRuntime::sw_state(UiSwitchId swid) const noexcept {
  using W = UiSwitchId;
  const auto &p = persisted_prefs();
  switch (swid) {
    case W::HOME_WEATHER_BG: return p.weather_background;
    case W::WEATHER_APP_BG: return p.weather_app_background;
    case W::HOME_WAKE_GREETING: return p.wake_greeting;
    case W::BLE_AREA_BEACON: return p.ble_beacon_enabled;
    case W::GAME_SOUND: return p.game_sound_effects;
    case W::VA_WAKE_SCREEN: return p.wake_screen_on_assist;
    case W::SCREENSAVER_PHOTO_CLOCK: return p.screensaver_photo_clock;
    case W::INSTANT_CROWN_HIBERNATE: return p.hold_crown_power_off;
    case W::BEDTIME_MODE: return p.bedtime_active;
  }
  return false;
}

void UiTouchRuntime::sw_toggle(UiSwitchId swid) const {
  using W = UiSwitchId;
  using K = PrefKey;
  const auto &p = persisted_prefs();
  K pk = K::COUNT;
  bool next_val = false;
  switch (swid) {
    case W::HOME_WEATHER_BG: pk = K::WEATHER_BACKGROUND; next_val = !p.weather_background; break;
    case W::WEATHER_APP_BG: pk = K::WEATHER_APP_BACKGROUND; next_val = !p.weather_app_background; break;
    case W::HOME_WAKE_GREETING: pk = K::WAKE_GREETING; next_val = !p.wake_greeting; break;
    case W::BLE_AREA_BEACON: pk = K::BLE_BEACON_ENABLED; next_val = !p.ble_beacon_enabled; break;
    case W::GAME_SOUND: pk = K::GAME_SOUND_EFFECTS; next_val = !p.game_sound_effects; break;
    case W::VA_WAKE_SCREEN: pk = K::WAKE_SCREEN_ON_ASSIST; next_val = !p.wake_screen_on_assist; break;
    case W::SCREENSAVER_PHOTO_CLOCK: pk = K::SCREENSAVER_PHOTO_CLOCK; next_val = !p.screensaver_photo_clock; break;
    case W::INSTANT_CROWN_HIBERNATE: pk = K::HOLD_CROWN_POWER_OFF; next_val = !p.hold_crown_power_off; break;
    case W::BEDTIME_MODE: pk = K::BEDTIME_ACTIVE; next_val = !p.bedtime_active; break;
  }
  if (pk == K::COUNT) return;
  uint32_t eff = 0U;
  if (latter_actions::record_local_pref_edit(pk, next_val ? "1" : "0", esphome::millis(), eff)) {
    device_prefs_state().last_effects = eff;
    cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
  }
}

float UiTouchRuntime::num_val(UiNumberId nid) const noexcept {
  using N = UiNumberId;
  const auto &p = persisted_prefs();
  switch (nid) {
    case N::VOLUME_STEP_SIZE: return static_cast<float>(p.volume_step_size);
    case N::SPEAKER_VOLUME:
      if (!std::isnan(local_mp_volume) && local_mp_volume >= 0.0f && local_mp_volume <= 1.0f) {
        return std::round(local_mp_volume * 100.0f);
      }
      return static_cast<float>(p.speaker_volume_pct);
    case N::ALERT_VOLUME: return static_cast<float>(p.alert_volume);
    case N::MANUAL_BRIGHTNESS: return static_cast<float>(p.manual_brightness_pct());
    case N::DAY_BRIGHTNESS: return static_cast<float>(p.day_brightness_pct());
    case N::NIGHT_BRIGHTNESS: return static_cast<float>(p.night_brightness_pct());
    case N::DAY_START_HOUR: return static_cast<float>(p.day_start_hour);
    case N::NIGHT_START_HOUR: return static_cast<float>(p.night_start_hour);
  }
  return 0.0f;
}

void UiTouchRuntime::set_num_val(UiNumberId nid, float val) const {
  using N = UiNumberId;
  using K = PrefKey;
  const uint32_t now_ms = esphome::millis();
  if (nid == N::SPEAKER_VOLUME) {
    const int cl_pct = std::clamp(static_cast<int>(std::lround(val)), 10, 100);
    auto &p = persisted_prefs();
    if (p.speaker_volume_pct != static_cast<uint8_t>(cl_pct)) {
      p.speaker_volume_pct = static_cast<uint8_t>(cl_pct);
      schedule_persisted_prefs_save(now_ms);
    }
    local_mp_volume = static_cast<float>(cl_pct) * 0.01f;
    cmd(UiHostCmd::SET_LOCAL_SPEAKER_VOL_PCT, cl_pct);
    return;
  }
  K pk = K::COUNT;
  int cl_val = static_cast<int>(std::lround(val));
  switch (nid) {
    case N::VOLUME_STEP_SIZE: pk = K::VOLUME_STEP_SIZE; cl_val = std::clamp(cl_val, 1, 10); break;
    case N::SPEAKER_VOLUME: break;
    case N::ALERT_VOLUME: pk = K::ALERT_VOLUME; cl_val = std::clamp(cl_val, 20, 100); break;
    case N::MANUAL_BRIGHTNESS: pk = K::MANUAL_BRIGHTNESS; cl_val = std::clamp(cl_val, 1, 100); break;
    case N::DAY_BRIGHTNESS: pk = K::DAY_BRIGHTNESS; cl_val = std::clamp(cl_val, 1, 100); break;
    case N::NIGHT_BRIGHTNESS: pk = K::NIGHT_BRIGHTNESS; cl_val = std::clamp(cl_val, 1, 100); break;
    case N::DAY_START_HOUR: pk = K::DAY_START_HOUR; cl_val = std::clamp(cl_val, 0, 23); break;
    case N::NIGHT_START_HOUR: pk = K::NIGHT_START_HOUR; cl_val = std::clamp(cl_val, 0, 23); break;
  }
  if (pk == K::COUNT) return;
  char buf[16];
  const int n = std::snprintf(buf, sizeof(buf), "%d", cl_val);
  if (n <= 0) return;
  uint32_t eff = 0U;
  if (latter_actions::record_local_pref_edit(pk, std::string_view(buf, static_cast<size_t>(n)), now_ms, eff)) {
    device_prefs_state().last_effects = eff;
    cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
  }
}

}  // namespace latter_ui
