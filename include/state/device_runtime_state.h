#pragma once

// ==============================================================================
// Latter Core SDK: Complete DeviceRuntimeState POD Umbrella & Controller APIs
// (include/state/device_runtime_state.h)
// ==============================================================================

#include "state/state_common.h"
#include "state/state_prefs.h"
#include "state/state_shortcuts.h"
#include "state/state_nav.h"
#include "state/state_media.h"
#include "state/state_areas.h"
#include "state/state_weather_cal.h"
#include "state/state_time_alarms.h"
#include "state/state_alerts_intercom.h"

namespace latter_ui {

struct DeviceRuntimeState {
  static constexpr size_t kSlotChannelCount = static_cast<size_t>(latter_rpc::SlotChannel::COUNT);
  static constexpr size_t kListSlotsPerPage = static_cast<size_t>(LATTER_SLOT_COUNT);
  static constexpr size_t kCardSlotsPerPage = static_cast<size_t>(LATTER_CARD_SLOT_COUNT);
  static constexpr size_t kSlotsPerPage =
      (kListSlotsPerPage > kCardSlotsPerPage) ? kListSlotsPerPage : kCardSlotsPerPage;

  BrowseSlotItem slots[kSlotChannelCount][kSlotsPerPage]{};
  int slot_page[kSlotChannelCount] = {1, 1, 1, 1};
  int slot_total_pages[kSlotChannelCount] = {1, 1, 1, 1};
  uint32_t last_music_page_req_ms = 0;

  AlertBannerState alert{};
  TimerRuntimeState timers{};
  StopwatchRuntimeState stopwatch{};
  AlarmRuntimeState alarms{};
  VoiceSessionState voice{};
  NavRuntimeState nav{};
  MusicRuntimeState music{};
  AreaRuntimeState area{};
  RemoteRuntimeState remote{};
  CalendarUiState cal{};
  IntercomRuntimeState intercom{};
  CamerasRuntimeState cameras{};
  HelloSyncState hello_sync{};

  // Live hardware telemetry & optional peripheral hooks (populated by modules/hal/pmu_axp2101.yaml and
  // modules/hal/imu_qmi8658.yaml when included; safe mains-powered / no-IMU defaults otherwise so
  // pmu_none.yaml & imu_none.yaml are retired without ESPHome Python lambda id(...) lookup errors).
  bool hw_usb_connected = true;
  bool hw_batt_charging = false;
  float hw_batt_level = NAN;
  float hw_batt_voltage = NAN;
  float hw_accel_x = 0.0f;
  float hw_accel_y = 0.0f;
  float hw_accel_z = 1.0f;
  float hw_imu_roll = 0.0f;
  float hw_imu_pitch = 0.0f;
  float hw_imu_temp = 30.0f;
  float hw_motion_mag = 0.0f;
  void (*hw_boot_battery_check)() = nullptr;
  void (*hw_init_pmu)() = nullptr;
  void (*hw_poll_pmu_1s)() = nullptr;
  void (*hw_wait_pwr_key_release)() = nullptr;
  bool (*hw_pwr_key_pressed)() = nullptr;
  void (*hw_update_batt_level)() = nullptr;
  void (*hw_set_imu_power_mode)(int ctrl7_val) = nullptr;

  // Display/session lifecycle hooks bound by modules/system_core.yaml on top of modules/base.yaml
  void (*on_ha_client_connected)(const std::string &client_info, const std::string &client_address) = nullptr;
  void (*on_ha_client_disconnected)(const std::string &client_info) = nullptr;
  void (*on_v3_domain_pushed)(std::string_view domain) = nullptr;
  void (*on_device_command)(std::string_view command) = nullptr;
  void (*on_hello_sent)() = nullptr;

  DeviceRuntimeState() {
    for (size_t i = 0; i < kSlotsPerPage; ++i) {
      slots[static_cast<size_t>(latter_rpc::SlotChannel::MUSIC)][i].type = "player";
    }
    slots[static_cast<size_t>(latter_rpc::SlotChannel::AREA_ENTITIES)][0].name = "Tap Header for Areas";
    slots[static_cast<size_t>(latter_rpc::SlotChannel::AREA_ENTITIES)][0].type = "text";
  }

  BrowseSlotItem *slots_for(latter_rpc::SlotChannel ch) {
    return slots[static_cast<size_t>(ch)];
  }
  const BrowseSlotItem *slots_for(latter_rpc::SlotChannel ch) const {
    return slots[static_cast<size_t>(ch)];
  }
  BrowseSlotItem *music_slots() { return slots_for(latter_rpc::SlotChannel::MUSIC); }
  const BrowseSlotItem *music_slots() const { return slots_for(latter_rpc::SlotChannel::MUSIC); }
  BrowseSlotItem *area_list_slots() { return slots_for(latter_rpc::SlotChannel::AREA_LIST); }
  const BrowseSlotItem *area_list_slots() const { return slots_for(latter_rpc::SlotChannel::AREA_LIST); }
  BrowseSlotItem *area_entity_slots() { return slots_for(latter_rpc::SlotChannel::AREA_ENTITIES); }
  const BrowseSlotItem *area_entity_slots() const { return slots_for(latter_rpc::SlotChannel::AREA_ENTITIES); }
  BrowseSlotItem *calendar_slots() { return slots_for(latter_rpc::SlotChannel::CALENDAR); }
  const BrowseSlotItem *calendar_slots() const { return slots_for(latter_rpc::SlotChannel::CALENDAR); }

  int &slot_page_for(latter_rpc::SlotChannel ch) { return slot_page[static_cast<size_t>(ch)]; }
  int slot_page_for(latter_rpc::SlotChannel ch) const { return slot_page[static_cast<size_t>(ch)]; }
  int &slot_total_pages_for(latter_rpc::SlotChannel ch) { return slot_total_pages[static_cast<size_t>(ch)]; }
  int slot_total_pages_for(latter_rpc::SlotChannel ch) const { return slot_total_pages[static_cast<size_t>(ch)]; }
};

inline DeviceRuntimeState &runtime_state() {
  static DeviceRuntimeState st;
  return st;
}

inline MultiTimerSlot *timer_slots() {
  return runtime_state().timers.slots;
}

inline std::string format_timer_slot_label(int slot_idx) {
  if (slot_idx < 0 || slot_idx >= 3) return "Timer";
  const auto &s = timer_slots()[slot_idx];
  if (!s.name.empty()) return s.name;
  char buf[24];
  int mins = s.duration_sec / 60;
  int secs = s.duration_sec % 60;
  if (secs == 0) std::snprintf(buf, sizeof(buf), "%d Min Timer", mins);
  else std::snprintf(buf, sizeof(buf), "%dm %ds Timer", mins, secs);
  return std::string(buf);
}

inline std::string format_timer_header_upper(int slot_idx) {
  if (slot_idx < 0 || slot_idx >= 3) return "TIMER";
  const auto &s = timer_slots()[slot_idx];
  if (!s.name.empty()) {
    std::string up = s.name;
    for (char &c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return up;
  }
  char buf[20];
  std::snprintf(buf, sizeof(buf), "TIMER %d", slot_idx + 1);
  return std::string(buf);
}


void sync_multi_timer_globals(bool &out_any_running, bool &out_any_paused,
                              int &out_min_rem_sec, int &out_active_dur_sec,
                              uint32_t &out_min_target_ms, int view_slot = -1);

inline void sync_multi_timer_state(int view_slot = -1, TimerRuntimeState &ts = runtime_state().timers) {
  sync_multi_timer_globals(ts.running, ts.paused, ts.remaining_sec, ts.duration_sec, ts.target_ms,
                           view_slot >= 0 ? view_slot : ts.view_slot);
}


int upsert_voice_timer_slot(const std::string &id_str, const std::string &name_str,
                            int total_sec, int left_sec, bool is_act, uint32_t now_ms);

inline ShortcutConfirmState &shortcut_confirm_state() {
  return runtime_state().nav.shortcut_confirm;
}

inline bool &ui_dirty_full() noexcept {
  return runtime_state().nav.ui_dirty_full;
}

inline void note_wake_from_standby(uint32_t now_ms) {
  auto &nav = runtime_state().nav;
  const uint32_t start_ms = (nav.sleep_start_time != 0) ? nav.sleep_start_time : nav.standby_entered_ms;
  nav.standby_entered_ms = 0;
  if (persisted_prefs().wake_greeting &&
      start_ms != 0 &&
      static_cast<uint32_t>(now_ms - start_ms) >= kStandbyGreetingIdleMs) {
    nav.wake_greeting_pending = true;
  }
}


inline void reset_all_v3_rev_guards() {
  area_ctx_state().epoch = 0;
  area_ctx_state().rev = 0;
  area_ctx_state().alert_id = -1;
  runtime_state().alert.last_seen_alert_id = -1;
  media_state().epoch = 0;
  media_state().rev = 0;
  remote_state().epoch = 0;
  remote_state().rev = 0;
  weather_state().epoch = 0;
  weather_state().rev = 0;
  calendar_state().epoch = 0;
  calendar_state().rev = 0;
  device_prefs_state().epoch = 0;
  device_prefs_state().rev = 0;
  shortcuts_state().epoch = 0;
  shortcuts_state().rev = 0;
  shortcuts_state().staging_epoch = 0;
  shortcuts_state().staging_rev = 0;
  shortcuts_state().staging_chunk_count = 0;
  shortcuts_state().staging_next_chunk = 0;
  shortcuts_state().staging_dock_items.clear();
  shortcuts_state().staging_pinned_items.clear();
  v3_mismatch_warn_count() = 0;
}


bool apply_calendar_view_action(int req_id,
                                int chunk_index,
                                int chunk_count,
                                std::string_view window_start,
                                std::string_view window_end,
                                std::string_view events,
                                CalendarWindowStore &store = calendar_window_store(),
                                DeviceRuntimeState &rs = runtime_state());

inline bool is_calendar_map_view_active(const DeviceRuntimeState &rs = runtime_state()) noexcept {
  return rs.nav.gui_page == PageId::CALENDAR && rs.cal.drilldown == CalDrilldown::MAP;
}

inline bool cal_dictation_active(const DeviceRuntimeState &rs = runtime_state()) noexcept {
  return rs.cal.dictation_field != 0U;
}

inline void start_calendar_dictation(uint8_t field,
                                     uint32_t now_ms,
                                     DeviceRuntimeState &rs = runtime_state()) {
  rs.cal.dictation_field = field;
  rs.cal.dictation_started_ms = now_ms;
  rs.cal.feedback_text = "Listening — speak now...";
  rs.cal.feedback_ms = now_ms;
}

inline bool finish_calendar_dictation(std::string_view stt_text,
                                      uint32_t now_ms,
                                      DeviceRuntimeState &rs = runtime_state()) {
  const uint8_t field = rs.cal.dictation_field;
  rs.cal.dictation_field = 0U;
  rs.cal.dictation_started_ms = 0U;
  std::string_view sv = trim_ascii_ws(stt_text);
  while (sv.size() > 1U && (sv.back() == '.' || sv.back() == '!' || sv.back() == '?')) {
    sv.remove_suffix(1);
    sv = trim_ascii_ws(sv);
  }
  if (sv.empty() || field == 0U) {
    rs.cal.feedback_text = "Didn't catch that";
    rs.cal.feedback_ms = now_ms;
    return false;
  }
  std::string cleaned(utf8_safe_prefix(sv, (field == 1U) ? 56U : 64U));
  if (!cleaned.empty() && std::islower(static_cast<unsigned char>(cleaned[0]))) {
    cleaned[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cleaned[0])));
  }
  if (field == 1U) {
    rs.cal.draft_title = std::move(cleaned);
  } else if (field == 2U) {
    rs.cal.draft_location = std::move(cleaned);
  }
  rs.cal.feedback_text = "Captured";
  rs.cal.feedback_ms = now_ms;
  return true;
}

inline void cancel_calendar_dictation(const char *msg,
                                      uint32_t now_ms,
                                      DeviceRuntimeState &rs = runtime_state()) {
  rs.cal.dictation_field = 0U;
  rs.cal.dictation_started_ms = 0U;
  rs.cal.feedback_text = (msg != nullptr) ? msg : "";
  rs.cal.feedback_ms = now_ms;
}

inline bool broadcast_dictation_active(const DeviceRuntimeState &rs = runtime_state()) noexcept {
  return rs.intercom.broadcast_dictation_active;
}

inline void start_broadcast_dictation(uint32_t now_ms,
                                      DeviceRuntimeState &rs = runtime_state()) {
  rs.intercom.broadcast_dictation_active = true;
  rs.intercom.broadcast_dictation_started_ms = now_ms;
  rs.intercom.broadcast_feedback = "Listening...";
  rs.intercom.broadcast_feedback_ms = now_ms;
}

inline bool finish_broadcast_dictation(std::string_view stt_text,
                                       uint32_t now_ms,
                                       DeviceRuntimeState &rs = runtime_state()) {
  rs.intercom.broadcast_dictation_active = false;
  rs.intercom.broadcast_dictation_started_ms = 0U;
  std::string_view sv = trim_ascii_ws(stt_text);
  if (sv.empty()) {
    rs.intercom.broadcast_feedback = "Didn't catch that";
    rs.intercom.broadcast_feedback_ms = now_ms;
    return false;
  }
  std::string cleaned(utf8_safe_prefix(sv, 160U));
  if (!cleaned.empty() && std::islower(static_cast<unsigned char>(cleaned[0]))) {
    cleaned[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cleaned[0])));
  }
  rs.intercom.broadcast_message = std::move(cleaned);
  rs.intercom.broadcast_feedback = "Broadcast sent";
  rs.intercom.broadcast_feedback_ms = now_ms;
  return true;
}

inline void cancel_broadcast_dictation(const char *msg,
                                       uint32_t now_ms,
                                       DeviceRuntimeState &rs = runtime_state()) {
  rs.intercom.broadcast_dictation_active = false;
  rs.intercom.broadcast_dictation_started_ms = 0U;
  if (msg != nullptr && msg[0] != '\0') {
    rs.intercom.broadcast_feedback = msg;
    rs.intercom.broadcast_feedback_ms = now_ms;
  }
}


size_t parse_v3_slots_payload(std::string_view slots_blob, BrowseSlotItem *out_slots, size_t max_slots);

template <typename StrT, typename = decltype(std::declval<const StrT &>().c_str())>
inline size_t parse_v3_slots_payload(const StrT &slots_blob, BrowseSlotItem *out_slots, size_t max_slots) {
  return parse_v3_slots_payload(std::string_view(slots_blob.c_str(), slots_blob.size()), out_slots, max_slots);
}

bool apply_music_lyrics_action(int req_id,
                               int chunk_index,
                               int chunk_count,
                               std::string_view track_key,
                               bool synced,
                               std::string_view lines_blob,
                               MusicLyricsState &st = runtime_state().music.lyrics);

bool apply_slots_action(int req_id, std::string_view mode,
                        int page, int total_pages, std::string_view slots_blob,
                        uint32_t now_ms = 0, bool force_area_entities = false,
                        DeviceRuntimeState &rt = runtime_state());

// Shared notification/alert banner populator for area_context push and show_notification action.
inline bool trigger_alert_banner(std::string_view title, std::string_view message,
                                 std::string_view priority, int duration_s,
                                 std::string_view camera_url, int alert_id,
                                 uint32_t now_ms, bool seed_only_if_unseen = false,
                                 AlertBannerState &ab = runtime_state().alert,
                                 std::string_view camera_entity = "") {
  if (seed_only_if_unseen) {
    if (alert_id < 0) return false;
    if (ab.last_seen_alert_id < 0) {
      ab.last_seen_alert_id = alert_id;
      return false;
    }
    if (alert_id <= ab.last_seen_alert_id) return false;
    ab.last_seen_alert_id = alert_id;
  } else {
    if (alert_id > 0 && alert_id <= ab.last_seen_alert_id) return false;
    if (alert_id > 0) ab.last_seen_alert_id = alert_id;
  }

  ab.title = title.empty() ? std::string("Notification") : std::string(title);
  ab.message = std::string(message);
  ab.priority = priority.empty() ? std::string("normal") : std::string(priority);
  ab.priority_level = parse_alert_priority(ab.priority);
  int dur = (duration_s > 0) ? duration_s : 10;
  if (dur < 3) dur = 3;
  if (dur > 60) dur = 60;
  ab.duration_ms = static_cast<uint32_t>(dur) * 1000U;
  ab.start_ms = now_ms;
  ab.camera_url = std::string(camera_url);
  ab.camera_entity = std::string(camera_entity);
  if (ab.camera_entity.empty() && !ab.camera_url.empty()) {
    const size_t epos = ab.camera_url.find("entity_id=");
    if (epos != std::string::npos) {
      const size_t start = epos + 10U;
      const size_t amp = ab.camera_url.find('&', start);
      ab.camera_entity = (amp == std::string::npos)
                             ? ab.camera_url.substr(start)
                             : ab.camera_url.substr(start, amp - start);
    }
  }
  ab.active = true;
  return true;
}

// ==============================================================================
// Canonical Music Playback Override & Volume Feedback Helpers
// ==============================================================================

inline int &remote_vol_pct() { return runtime_state().music.remote_vol_pct; }
inline uint32_t &last_local_vol_tap_ms() { return runtime_state().music.last_local_vol_tap_ms; }
inline uint32_t &remote_play_override_ms() { return runtime_state().music.remote_play_override_ms; }
inline bool &remote_play_override_state() { return runtime_state().music.remote_play_override_state; }

inline void set_remote_play_override(bool playing, uint32_t now_ms) {
  remote_play_override_state() = playing;
  remote_play_override_ms() = now_ms;
}

inline void clear_remote_play_override() {
  remote_play_override_ms() = 0;
  remote_play_override_state() = false;
}

inline bool is_music_playing_effective(bool is_remote, bool local_playing,
                                       std::string_view remote_state_str, uint32_t now_ms) {
  if (!is_remote) {
    return local_playing;
  }
  if (remote_play_override_ms() > 0U && (now_ms - remote_play_override_ms() < 2500U)) {
    return remote_play_override_state();
  }
  return (remote_state_str == "playing" || remote_state_str == "buffering");
}

inline bool &music_queue_cleared() noexcept { return runtime_state().music.queue_cleared; }

inline bool is_music_paused_effective(bool is_effectively_playing,
                                      bool is_local_speaker,
                                      std::string_view ms_state,
                                      bool local_mp_paused = false) noexcept {
  return !is_effectively_playing && !music_queue_cleared() &&
         (local_mp_paused || ms_state == "paused" ||
          (is_local_speaker && (ms_state == "playing" || ms_state == "buffering")));
}


void sync_audio_session_media_observation(bool local_mp_playing,
                                          bool local_mp_paused,
                                          uint32_t now_ms,
                                          const MediaState &ms = media_state(),
                                          const AreaContextState &ac = area_ctx_state());

inline int step_music_volume_feedback(int dir, float step_pct_cfg, bool is_local,
                                      float local_mp_vol, uint32_t now_ms,
                                      MusicRuntimeState &m = runtime_state().music) {
  const int v_step = (std::isnan(step_pct_cfg) || step_pct_cfg <= 0.0f)
                         ? 5
                         : std::max(1, static_cast<int>(std::lround(step_pct_cfg)));
  const int delta = (dir >= 0) ? v_step : -v_step;
  last_local_vol_tap_ms() = now_ms;
  int next_pct = -1;
  if (is_local) {
    const int cur_pct = (std::isnan(local_mp_vol) || local_mp_vol <= 0.0f)
                            ? 70
                            : static_cast<int>(std::lround(local_mp_vol * 100.0f));
    next_pct = std::clamp(cur_pct + delta, 10, 100);
    m.action_feedback = "Volume: " + std::to_string(next_pct) + "%";
  } else if (remote_vol_pct() >= 0) {
    next_pct = std::clamp(remote_vol_pct() + delta, 0, 100);
    remote_vol_pct() = next_pct;
    m.action_feedback = "Volume: " + std::to_string(next_pct) + "%";
  } else {
    m.action_feedback = (dir >= 0) ? "Volume +" : "Volume -";
  }
  m.feedback_timer = now_ms;
  return next_pct;
}


inline bool should_return_to_home_on_idle(const DeviceRuntimeState &rt,
                                          const PersistedDevicePrefs &p,
                                          bool media_playing,
                                          bool cam_session_active = false,
                                          bool is_stationary = (LATTER_IS_STATIONARY != 0)) {
  if (!p.home_screen_enabled) return false;
  if (return_to_home_timeout_ms(p) == 0U) return false;
  if (rt.nav.gui_page == 1 && rt.nav.home_subpage == 0) return false;
  if (rt.nav.is_hibernating || rt.nav.ota_in_progress) return false;
  if (rt.alarms.ringing || rt.timers.ringing || rt.alarms.find_my_active || rt.alert.active) {
    return false;
  }
  if (rt.voice.active || rt.voice.tts_received || !rt.voice.status_text.empty() ||
      cam_session_active || rt.nav.shortcut_confirm.open || rt.nav.quick_actions_open) {
    return false;
  }
  if (rt.nav.gui_page == 3 && rt.nav.active_game > 0) return false;
  if (rt.nav.gui_page == PageId::MUSIC &&
      is_music_ambient_tab(rt.music.view) &&
      rt.music.drilldown == MusicDrilldown::NONE &&
      rt.music.takeover == MusicTakeover::NONE &&
      rt.music.overlay_mode == 0 && media_playing) {
    return false;
  }
  if (rt.nav.gui_page == PageId::CAMERAS) {
    if (is_stationary &&
        (rt.cameras.tab == CamerasTab::CAROUSEL || rt.cameras.tab == CamerasTab::GRID)) {
      return false;
    }
  }
  if (rt.nav.gui_page == 10) {
    if (rt.nav.time_subpage == 1 && rt.timers.running) return false;
    if (rt.nav.time_subpage == 3 && rt.stopwatch.running) return false;
  }
  if (rt.nav.gui_page == 8) {
    if (rt.nav.tools_subpage == 3 && rt.stopwatch.running) return false;
    if (rt.nav.tools_subpage == 5 || rt.nav.tools_subpage == 7) {
      return false;
    }
  }
  if (rt.nav.gui_page == PageId::CALENDAR) {
    if (rt.cal.drilldown >= CalDrilldown::ADD_STEP1 || rt.cal.dictation_field != 0U ||
        rt.cal.delete_confirm_open) {
      return false;
    }
  }
  return true;
}


// ==============================================================================
// Handshake Deduplication, Crown Wake Latch & Animation Poller Coordination
// ==============================================================================
inline HelloSyncState &hello_sync_state() { return runtime_state().hello_sync; }

inline void reset_hello_sync_on_connect() {
  auto &st = hello_sync_state();
  st.in_flight = false;
  st.started_ms = 0;
  st.last_sent_ms = 0;
  st.last_sent_dirty_mask.clear();
}

inline void mark_hello_in_flight(bool active, uint32_t now_ms = 0) {
  auto &st = hello_sync_state();
  st.in_flight = active;
  st.started_ms = active ? ((now_ms == 0U) ? 1U : now_ms) : 0U;
}

inline void mark_hello_sent(uint32_t now_ms, PrefMask dirty_mask) {
  auto &st = hello_sync_state();
  st.in_flight = false;
  st.started_ms = 0;
  st.last_sent_ms = (now_ms == 0U) ? 1U : now_ms;
  st.last_sent_dirty_mask = dirty_mask;
}

inline bool should_trigger_hello_on_ping(uint32_t now_ms, PrefMask dirty_mask) {
  const auto &st = hello_sync_state();
  if (st.in_flight && (st.started_ms == 0U || static_cast<uint32_t>(now_ms - st.started_ms) < 31000U)) {
    return false;
  }
  if (st.last_sent_ms != 0U &&
      static_cast<uint32_t>(now_ms - st.last_sent_ms) < 1500U &&
      dirty_mask == st.last_sent_dirty_mask) {
    return false;
  }
  return true;
}

inline bool &pwr_key_wake_consumed() { return runtime_state().nav.pwr_key_wake_consumed; }

inline bool &ui_anim_poller_running() { return runtime_state().nav.ui_anim_poller_running; }

template <typename PollerT>
inline void set_ui_anim_poller(PollerT *tick, bool want) {
  if (tick == nullptr) return;
  if (ui_anim_poller_running() == want) return;
  ui_anim_poller_running() = want;
  if (want) {
    tick->start_poller();
  } else {
    tick->stop_poller();
  }
}

template <typename PollerT>
inline void ensure_ui_anim_poller(PollerT *tick) {
  set_ui_anim_poller(tick, true);
}

// ==============================================================================
// Consolidated UI / Domain State Accessors & Pure Helpers
// ==============================================================================
inline uint32_t &menu_scroll_start_ms() { return runtime_state().nav.menu_scroll_start_ms; }
inline int &pinned_menu_page() { return runtime_state().nav.pinned_menu_page; }
inline int &timer_view_slot() { return runtime_state().timers.view_slot; }
inline bool &cam_quick_reply_open() { return runtime_state().cameras.quick_reply_open; }
inline int &home_dock_page() { return runtime_state().nav.home_dock_page; }
inline uint32_t &last_music_page_req_ms() { return runtime_state().last_music_page_req_ms; }
inline uint32_t &music_scroll_tap_lockout_until_ms() { return runtime_state().music.scroll_tap_lockout_until_ms; }


inline std::string *remote_slots_ent() { return runtime_state().remote.slot_ents; }
inline std::string *remote_slots_name() { return runtime_state().remote.slot_names; }
inline bool *remote_slot_states_on() { return runtime_state().remote.slot_states_on; }
inline int &remote_control_page() { return runtime_state().remote.control_page; }

inline bool is_configured_remote_slot(const std::string &ent) {
  return !ent.empty();
}

inline bool has_any_configured_remote() {
  const std::string *t_ents = remote_slots_ent();
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    if (is_configured_remote_slot(t_ents[i])) return true;
  }
  return false;
}

inline std::string format_remote_entity_fallback(const std::string &ent) {
  if (!is_configured_remote_slot(ent)) return "";
  std::string s = ent;
  const size_t dot = s.find('.');
  if (dot != std::string::npos) s = s.substr(dot + 1);
  bool cap = true;
  for (size_t i = 0; i < s.length(); i++) {
    if (s[i] == '_') {
      s[i] = ' ';
      cap = true;
    } else if (cap) {
      s[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
      cap = false;
    }
  }
  return s;
}


void parse_latter_remote_slots_summary(const std::string &summary);
int collect_configured_remote_slots(int out_slots[MAX_REMOTE_SLOTS]);
int resolve_active_remote_slot(int active_slot);

inline std::string &area_available_domains_csv() { return runtime_state().area.available_domains_csv; }
inline int &area_cat_dock_page() { return runtime_state().area.cat_dock_page; }
inline std::string &light_insp_entity_id() { return runtime_state().area.light_insp_entity_id; }
inline std::string &light_insp_name() { return runtime_state().area.light_insp_name; }
inline std::string &light_insp_domain() { return runtime_state().area.light_insp_domain; }
inline int &light_insp_mode() { return runtime_state().area.light_insp_mode; }
inline int &light_insp_slot() { return runtime_state().area.light_insp_slot; }
inline bool &light_insp_active() { return runtime_state().area.light_insp_active; }
inline float &light_insp_hue_deg() { return runtime_state().area.light_insp_hue_deg; }
inline float &light_insp_sat() { return runtime_state().area.light_insp_sat; }
inline int &light_insp_kelvin() { return runtime_state().area.light_insp_kelvin; }
inline int &light_insp_bri_pct() { return runtime_state().area.light_insp_bri_pct; }
inline std::string &area_color_hex_param() { return runtime_state().area.color_hex_param; }

inline int &home_hub_tab() { return home_hub_state().tab; }
inline int &home_hub_mode_bar_page() { return home_hub_state().mode_bar_page; }
inline int &home_hub_thermostat_idx() { return home_hub_state().thermostat_idx; }
inline int &home_hub_entry_page() { return home_hub_state().entry_page; }
inline int &home_hub_broadcast_page() { return home_hub_state().broadcast_page; }
inline int &home_hub_vacuum_idx() { return home_hub_state().vacuum_idx; }

inline int collect_area_categories(std::string *out_cats) {
  const std::string_view src =
      (area_ctx_state().epoch == 0U) ? std::string_view(kDefaultAreaCategories)
                                     : std::string_view(area_available_domains_csv());
  return parse_area_categories_csv(src, out_cats, 8);
}

inline std::string resolve_preferred_area_category() {
  std::string cats[8];
  const int cnt = collect_area_categories(cats);
  return (cnt > 0) ? cats[0] : std::string{};
}

inline std::string &latter_site_id() { return runtime_state().area.site_id; }
inline std::string &latter_ble_proxy() { return runtime_state().area.ble_proxy; }
inline std::string &latter_area_label() { return runtime_state().area.area_label; }


// ==============================================================================
// Compiled Protocol v3 Domain Reducer & Timer/Alarm Controller Declarations
// (Implemented in components/latter_core/runtime_controllers.cpp)
// ==============================================================================
struct V3ReduceContext {
  uint32_t now_ms{0U};
  const char *friendly_name{""};
  bool local_mp_playing{false};
  bool local_mp_paused{false};
  int local_album_art_w{0};
  bool remote_art_fetch_running{false};
  uint32_t last_touch_ms{0U};
};

struct V3DomainEffects {
  bool area_changed{false};
  bool mark_dirty_full{false};
  bool fetch_area_browse{false};
  bool fetch_on_demand_camera{false};
  bool apply_theme_preset{false};
  bool alert_triggered{false};
  int alert_sfx_id{0};
  bool fetch_alert_camera{false};
  bool release_alert_camera_art{false};
  bool stop_wallpaper_fetch{false};
  bool release_wallpaper_art{false};
  bool fetch_wallpaper{false};
  bool stop_screensaver_photo_fetch{false};
  bool release_screensaver_photo_art{false};
  bool fetch_screensaver_photo{false};
  bool weather_sim_dirty{false};
  bool reset_ambient_frame_ms{false};
  bool ensure_anim_poller{false};
  bool schedule_disp_update{false};
  bool throttled_disp_update{false};
  bool sync_local_volume{false};
  float local_volume_0_to_1{0.0f};
  bool stop_remote_art_fetch{false};
  bool release_remote_art{false};
  bool fetch_remote_art{false};
  bool fetch_music_lyrics{false};
  bool navigate_home_from_remote{false};
  bool navigate_start_screen_remote{false};
  bool sync_display_brightness{false};
  bool sync_display_brightness_smooth{false};
  bool apply_device_prefs_effects{false};
};

class V3DomainReducer {
 public:
  void reset();
  V3DomainEffects reduce(std::string_view domain, const V3ReduceContext &ctx);

 private:
  std::string prev_area_icon_{};
  std::string prev_area_ambient_light_{};
  std::string prev_title_{};
  std::string prev_artist_{};
  std::string prev_album_{};
  std::string prev_state_{};
  std::string prev_spk_{};
  std::string prev_spk_name_{};
  std::string prev_art_{};
  bool prev_is_local_{false};
  int prev_vol_pct_{-1};
  bool prev_shuffle_{false};
  uint8_t prev_repeat_mode_{0};
  bool prev_muted_{false};
  bool prev_favorite_{false};
  int prev_queue_count_{0};
  uint16_t prev_media_caps_{0};
  std::string prev_sun_{};
  std::string prev_wallpaper_url_{};
};

V3DomainReducer &v3_domain_reducer() noexcept;

struct TimerTickEffects {
  bool any_expired{false};
  bool sec_changed{false};
  bool auto_silenced{false};
};

struct AlarmTickEffects {
  bool snooze_fired{false};
  int triggered_idx{0};
  bool prefs_saved{false};
  bool auto_silenced{false};
};

struct VaTimerEffects {
  bool fire_alert{false};
  bool stop_sound{false};
  bool schedule_disp{false};
};

class TimerAlarmController {
 public:
  static constexpr uint32_t kAutoSilenceMs = 300000U;
  static constexpr uint32_t kSnoozeSeconds = 300U;

  void reset() noexcept { *this = TimerAlarmController{}; }

  TimerTickEffects tick_timers_250ms(uint32_t now_ms);
  AlarmTickEffects tick_alarms_1s(bool time_valid, uint32_t now_epoch,
                                  int hour, int minute, int second, int day_of_week,
                                  uint32_t now_ms);

  VaTimerEffects on_va_timer_started(const std::string &id, const std::string &name,
                                     int total_sec, int left_sec, bool is_active,
                                     uint32_t now_ms);
  VaTimerEffects on_va_timer_updated(const std::string &id, const std::string &name,
                                     int total_sec, int left_sec, bool is_active,
                                     uint32_t now_ms);
  VaTimerEffects on_va_timer_cancelled(const std::string &id, const std::string &name,
                                       uint32_t now_ms);
  VaTimerEffects on_va_timer_finished(const std::string &id, const std::string &name,
                                      uint32_t now_ms);

  bool snooze_active_alarm(bool time_valid, uint32_t now_epoch, uint32_t now_ms) noexcept;
  bool dismiss_ringing() noexcept;

  uint32_t ringing_started_ms() const noexcept { return ringing_started_ms_; }

 private:
  void stamp_ringing_start_(uint32_t now_ms) noexcept {
    ringing_started_ms_ = (now_ms == 0U) ? 1U : now_ms;
  }
  bool check_auto_silence_(uint32_t now_ms) noexcept;
  static bool should_redraw_va_timer_(const DeviceRuntimeState &rs) noexcept;

  uint32_t ringing_started_ms_{0U};
};

TimerAlarmController &timer_alarm_controller() noexcept;

// Resets volatile runtime state, domain state, RPC state, and all core C++20 controllers
// deterministically in a single call (preserving bound hardware/lifecycle function pointers
// unless preserve_hooks is false).
void reset_runtime_state(bool preserve_hooks = false);


inline int &cam_carousel_idx() { return runtime_state().cameras.selected_idx; }
inline bool &cam_call_active() { return runtime_state().intercom.cam_call_active; }
inline bool &cam_listen_active() { return runtime_state().intercom.cam_listen_active; }
inline bool &cam_intercom_ptt_active() { return runtime_state().intercom.cam_intercom_ptt_active; }
inline std::string &cam_intercom_status() { return runtime_state().intercom.cam_intercom_status; }
inline bool &cam_landscape_mode() { return runtime_state().cameras.landscape_mode; }
inline bool &cam_expanded_fullscreen() { return runtime_state().intercom.cam_expanded_fullscreen; }
inline uint32_t &last_cam_live_fetch_ms() { return runtime_state().intercom.last_cam_live_fetch_ms; }
inline uint32_t &cam_error_backoff_until_ms() { return runtime_state().intercom.cam_error_backoff_until_ms; }
inline bool &cam_download_in_progress() { return runtime_state().intercom.cam_download_in_progress; }


inline bool is_camera_view_active(const DeviceRuntimeState &rs = runtime_state()) noexcept {
  return rs.nav.gui_page == PageId::CAMERAS;
}

inline bool is_p4_live_doorbell_popup_enabled(
    const PersistedDevicePrefs &p = persisted_prefs(),
    const AreaContextState &ac = area_ctx_state(),
    const AlertBannerState &ab = runtime_state().alert,
    bool is_p4 = (LATTER_CHIP_P4 != 0)) noexcept {
  if (!is_p4 || p.camera_doorbell_popup != 1U) return false;
  if (ac.camera_stream_url.empty()) return false;
  return !ab.camera_entity.empty() || !ab.camera_url.empty();
}


std::string resolve_active_carousel_camera_area_id(const std::string &cameras_summary);
std::string resolve_active_carousel_camera_id(const std::string &cameras_summary,
                                              const std::string &fallback_cam_url);
std::string build_carousel_camera_url(const std::string &base_cam_url,
                                      const std::string &cameras_summary,
                                      uint32_t now_ms);

}  // namespace latter_ui
