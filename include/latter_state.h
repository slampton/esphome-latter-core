#pragma once

// ==============================================================================
// Latter Core SDK: Protocol v3 Domain State, Runtime State & Persisted POD Prefs
// (include/latter_state.h)
// ==============================================================================

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#if defined(USE_ESP32) && __has_include("esphome/core/preferences.h")
#include "esphome/core/preferences.h"
#define LATTER_HAS_ESP_PREFS 1
#else
#define LATTER_HAS_ESP_PREFS 0
#endif

#include "latter_protocol.h"
#include "latter_rpc.h"

namespace latter_ui {

// Em-dash placeholder rendered when Home Assistant / Protocol v3 state is not yet populated.
inline constexpr const char *kUnknownGlyph = "—";

enum class LockState : uint8_t {
  UNKNOWN  = 0,
  UNLOCKED = 1,
  LOCKED   = 2,
};

enum class GarageState : uint8_t {
  UNKNOWN = 0,
  CLOSED  = 1,
  OPEN    = 2,
};

struct LinkState {
  bool ha_connected = false;
  bool wifi_connected = false;
  uint32_t last_ha_connect_ms = 0;
  uint32_t last_ha_disconnect_ms = 0;
  uint32_t last_wifi_connect_ms = 0;
};

struct WholeHomeState {
  int tab = 0;
  int thermostat_target_f = -1;
  float thermostat_current_f = NAN;
  std::string thermostat_mode = "";
  GarageState garage_state = GarageState::UNKNOWN;
  LockState lock_state = LockState::UNKNOWN;
  std::string vacuum_state = "";
};

// Protocol v3 area_context state (unread wire fields covers_open, thermostat_entity,
// lock_entity, garage_entity, vacuum_entity are parsed on the wire without heap storage).
struct AreaContextState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string area_id = "";
  std::string area_name = "";
  std::string site_id = "";
  std::string ble_closest_proxy = "";
  std::string dock_apps = "";
  std::string wallpaper_url = "";
  std::string theme_color_hex = "";
  std::string available_domains = "";
  float area_temp = NAN;
  float area_humidity = NAN;
  int lights_on = 0;
  std::string alarm_entity = "";
  std::string alarm_state = "";
  std::string camera_snapshot_url = "";
  std::string cameras_summary = "";
  std::string doorbell_talk_state = "";
  std::string intercom_targets_summary = "";
  int alert_id = -1;
  std::string alert_title = "";
  std::string alert_message = "";
  std::string alert_priority = "normal";
  int alert_duration = 10;
  bool alert_chime = true;
  std::string alert_camera_url = "";
  std::string activity_card = "";        // "1" / "0"; empty = not set -> kDefaultActivityCard
  std::string activity_sources = "";     // csv of source keys; empty = not set -> kDefaultActivitySources
  std::string security_pin_policy = "";  // "allow" | "require_pin" | "block"
};

// Protocol v3 active_media state (unread wire fields position_updated_at, supported_features,
// volume_entity are dropped; last_speaker is retained and wired to music_browse).
struct MediaState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string state = "";
  std::string title = "";
  std::string artist = "";
  std::string album = "";
  float duration = 0.0f;
  float position = 0.0f;
  float volume = 0.5f;
  bool has_volume = false;
  std::string media_type = "";
  std::string art_url = "";
  std::string active_speaker = "";
  std::string speaker_name = "";
  std::string last_speaker = "";
  std::string local_speaker = "";
  bool is_local = false;
};

struct RemoteState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  int active_slot = 0;
  int configured_count = 0;
  std::string slots_summary = "";
};

struct WeatherState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string condition = "";
  float temperature = NAN;
  float humidity = NAN;
  float wind_speed = NAN;
  float wind_bearing = NAN;
  float high = NAN;
  float low = NAN;
  std::string forecast_5d = "";
  std::string sun_state = "";
  float sun_elevation = NAN;
  float sun_azimuth = NAN;
  std::string sun_next_rising = "";
  std::string sun_next_setting = "";
  std::string moon_phase = "";
};

// Protocol v3 calendar_agenda state (unread next_event wire field is not heap-stored).
struct CalendarState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string summary = "";
  int count = 0;
};

struct BrowseSlotItem {
  std::string name = "";
  std::string id = "";
  std::string type = "";
  bool active = false;
  int level = -1;
};

// ==============================================================================
// Versioned Persisted Device Preferences (Single ESPPreferenceObject POD Blob)
// ==============================================================================
static constexpr uint32_t kPersistedPrefsMagic = 0x4C505246U;  // "LPRF"
static constexpr uint16_t kPersistedPrefsVersion = 1U;
static constexpr uint32_t kPersistedPrefsHash = 0x4C415450U;   // "LATP"

struct PersistedAlarmPref {
  uint8_t hour = 7;
  uint8_t minute = 0;
  bool enabled = false;
  uint8_t recurrence = 1;  // 0 = Daily, 1 = Weekdays, 2 = Weekends, 3 = Once
};

struct PersistedDevicePrefs {
  uint32_t magic = kPersistedPrefsMagic;
  uint16_t version = kPersistedPrefsVersion;
  bool clock_face_digital = false;
  bool is_24h_format = false;
  bool power_profile_seeded = false;
  uint8_t reserved_pad[3] = {0, 0, 0};
  uint32_t custom_theme_rgb = 0x3898FFU;
  int32_t high_score_marbles = 0;
  int32_t high_score_archery = 0;
  int32_t high_score_bricks = 0;
  int32_t high_score_chomp = 0;
  int32_t high_score_2048 = 0;
  int32_t high_score_memory = 0;
  PersistedAlarmPref alarms[3] = {
      {7, 0, false, 1},
      {8, 30, false, 2},
      {9, 0, false, 2},
  };
};

static_assert(std::is_trivially_copyable_v<PersistedDevicePrefs>,
              "PersistedDevicePrefs must remain a POD blob for ESPPreferenceObject.");

inline PersistedDevicePrefs &persisted_prefs() {
  static PersistedDevicePrefs prefs{};
  return prefs;
}

#if LATTER_HAS_ESP_PREFS
inline esphome::ESPPreferenceObject &persisted_prefs_handle() {
  static esphome::ESPPreferenceObject pref;
  static bool inited = false;
  if (!inited && esphome::global_preferences != nullptr) {
    pref = esphome::global_preferences->make_preference<PersistedDevicePrefs>(kPersistedPrefsHash);
    inited = true;
  }
  return pref;
}
#endif

inline bool load_persisted_prefs() {
#if LATTER_HAS_ESP_PREFS
  if (esphome::global_preferences == nullptr) return false;
  PersistedDevicePrefs loaded{};
  if (persisted_prefs_handle().load(&loaded) &&
      loaded.magic == kPersistedPrefsMagic &&
      loaded.version == kPersistedPrefsVersion) {
    for (auto &a : loaded.alarms) {
      if (a.hour > 23) a.hour = 7;
      if (a.minute > 59) a.minute = 0;
      if (a.recurrence > 3) a.recurrence = 0;
    }
    persisted_prefs() = loaded;
    return true;
  }
#endif
  persisted_prefs() = PersistedDevicePrefs{};
  return false;
}

inline bool save_persisted_prefs() {
  auto &p = persisted_prefs();
  p.magic = kPersistedPrefsMagic;
  p.version = kPersistedPrefsVersion;
#if LATTER_HAS_ESP_PREFS
  if (esphome::global_preferences == nullptr) return false;
  return persisted_prefs_handle().save(&p);
#else
  return true;
#endif
}

// Unified 6-game high-score accessors (1=Marbles, 2=Archery, 3=Bricks, 4=Chomp, 5=2048, 6=Memory Match)
inline int get_game_high_score(int game_id) {
  const auto &p = persisted_prefs();
  switch (game_id) {
    case 1: return p.high_score_marbles;
    case 2: return p.high_score_archery;
    case 3: return p.high_score_bricks;
    case 4: return p.high_score_chomp;
    case 5: return p.high_score_2048;
    case 6: return p.high_score_memory;
    default: return 0;
  }
}

inline bool update_game_high_score(int game_id, int score) {
  if (score <= 0) return false;
  auto &p = persisted_prefs();
  int32_t *slot = nullptr;
  switch (game_id) {
    case 1: slot = &p.high_score_marbles; break;
    case 2: slot = &p.high_score_archery; break;
    case 3: slot = &p.high_score_bricks; break;
    case 4: slot = &p.high_score_chomp; break;
    case 5: slot = &p.high_score_2048; break;
    case 6: slot = &p.high_score_memory; break;
    default: return false;
  }
  if (score <= *slot) return false;
  *slot = score;
  save_persisted_prefs();
  return true;
}

// ==============================================================================
// Typed C++ Volatile Runtime State (replaces ~199 YAML globals)
// ==============================================================================
struct AlertBannerState {
  bool active = false;
  std::string title = "";
  std::string message = "";
  std::string priority = "normal";
  std::string camera_url = "";
  std::string loaded_camera_url = "";
  uint32_t start_ms = 0;
  uint32_t duration_ms = 10000U;
  int last_seen_alert_id = -1;
};

struct MultiTimerSlot {
  std::string id = "";
  std::string name = "";
  int duration_sec = 300;
  int remaining_sec = 300;
  uint32_t target_ms = 0;
  bool running = false;
  bool paused = false;
};

struct TimerRuntimeState {
  MultiTimerSlot slots[3] = {
      {"", "", 300, 300, 0, false, false},
      {"", "", 600, 600, 0, false, false},
      {"", "", 900, 900, 0, false, false},
  };
  int view_slot = 0;  // 0 = 3-Timer Landing Page, 1..3 = Single-Timer Detail/Editor
  int duration_sec = 300;
  int remaining_sec = 300;
  uint32_t target_ms = 0;
  bool running = false;
  bool paused = false;
  bool ringing = false;
  std::string va_timer_name = "";
};

struct StopwatchRuntimeState {
  bool running = false;
  uint32_t start_time_ms = 0;
  uint32_t elapsed_ms = 0;
  uint32_t lap_ms = 0;
};

struct AlarmRuntimeState {
  int edit_idx = 0;
  bool ringing = false;
  int ringing_idx = 0;
  uint32_t snooze_until = 0;
  uint32_t last_fired_epoch_min = 0;
  bool find_my_active = false;
  uint32_t find_my_start_ms = 0;
};

struct VoiceSessionState {
  bool active = false;
  bool vad_active = false;
  bool tts_received = false;
  bool resume_music_after = false;
  bool was_cancelled = false;
  bool intercom_turn = false;
  int return_page = 0;
  std::string status_text = "";
  std::string last_detected_wake_word = "";
};

struct NavRuntimeState {
  int gui_page = 1;
  int home_subpage = 0;
  int home_activity_idx = 0;
  int sky_weather_subpage = 0;
  int area_subpage = 0;
  int remote_subpage = 0;
  int tools_subpage = 0;
  int tools_menu_page = 0;
  int display_menu_page = 1;
  int system_dashboard_page = 1;
  int system_menu_mode = 0;
  int sleep_menu_page = 1;
  int audio_menu_parent_page = 4;
  bool menu_scroll_active = false;
  int menu_scroll_dir = 1;
  int menu_scroll_target_page = 1;
  int display_power_state = 0;  // 0 = ACTIVE, 1 = SCREENSAVER, 2 = SCREEN_OFF / STANDBY
  uint32_t sleep_start_time = 0;
  bool is_hibernating = false;
  int hibernate_countdown_val = 0;
  bool hud_wake_finger_released = true;
  bool ota_in_progress = false;
  int ota_progress_pct = 0;
  int update_check_state = 0;   // 0 = Idle, 1 = Checking..., 2 = Checked
  uint32_t update_check_timer = 0;
  int game_carousel_idx = 0;
  int active_game = 0;
  float marble_touch_x = -1.0f;
  float marble_touch_y = -1.0f;
  bool game_btn_pressed = false;
};

struct MusicRuntimeState {
  int overlay_mode = 0;
  bool direct_nav = false;
  bool hud_active = false;
  uint32_t hud_timer = 0;
  int category_page = 1;
  std::string selected_category_mode = "speakers";
  std::string selected_category_title = "SPEAKERS";
  std::string drilldown_artist_name = "";
  std::string drilldown_album_id = "";
  std::string drilldown_parent_mode = "";
  int drilldown_artist_page = 1;
  int drilldown_album_page = 1;
  std::string active_playback_media_type = "music";
  std::string active_speaker_entity = "";
  std::string active_speaker_name = "";
  std::string target_speaker_id = "";
  std::string target_speaker_name = "";
  std::string selected_slot_name = "";
  std::string selected_slot_id = "";
  std::string selected_slot_type = "";
  std::string action_feedback = "";
  uint32_t feedback_timer = 0;
  float pos_base = 0.0f;
  uint32_t pos_sync_time = 0;
  int skip_burst_count = 0;
  uint32_t skip_last_tap_ms = 0;
  std::string loaded_art_url = "";
  std::string loaded_wallpaper_url = "";
};

struct AreaRuntimeState {
  std::string browse_mode = "entities";
  std::string domain_filter = "all";
  std::string active_area_id = "";
  std::string active_area_name = "Area Control";
  std::string alarm_entity = "";
  std::string alarm_state = "disarmed";
  std::string alarm_service = "alarm_control_panel.alarm_disarm";
  std::string target_entity_id = "";
  std::string target_action = "toggle";
  int target_level = -1;
  std::string target_code = "";
  std::string action_feedback = "";
  uint32_t feedback_timer = 0;
};

struct RemoteRuntimeState {
  int active_slot = 0;
  std::string action_feedback = "";
  uint32_t feedback_timer = 0;
};

struct CalendarUiState {
  std::string quick_summary = "Reminder";
  int quick_in_minutes = 30;
  int quick_duration_minutes = 30;
};

struct IntercomRuntimeState {
  std::string broadcast_message = "Dinner is ready";
  std::string broadcast_feedback = "";
  uint32_t broadcast_feedback_ms = 0;
  std::string cam_action = "listen_start";
  std::string cam_message = "Please leave it at the door.";
  std::string cam_noise_reduction = "strong";
  std::string cam_code = "";
  uint32_t cam_status_ms = 0;
};

struct DeviceRuntimeState {
  static constexpr size_t kSlotChannelCount = static_cast<size_t>(latter_rpc::SlotChannel::COUNT);
  static constexpr size_t kSlotsPerPage = 5;

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
  void (*on_v3_domain_pushed)(std::string_view domain, const std::string &prev_dock) = nullptr;

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

inline void sync_multi_timer_globals(bool &out_any_running, bool &out_any_paused,
                                     int &out_min_rem_sec, int &out_active_dur_sec,
                                     uint32_t &out_min_target_ms, int view_slot = -1) {
  auto *slots = timer_slots();
  const int eff_view = (view_slot >= 0) ? view_slot : runtime_state().timers.view_slot;
  bool any_run = false;
  bool any_pau = false;
  int min_rem = 0;
  int act_dur = slots[0].duration_sec;
  uint32_t min_t = 0;

  for (int i = 0; i < 3; i++) {
    if (slots[i].running) {
      if (!any_run || slots[i].remaining_sec < min_rem) {
        min_rem = slots[i].remaining_sec;
        act_dur = slots[i].duration_sec;
        min_t   = slots[i].target_ms;
      }
      any_run = true;
    } else if (slots[i].paused && !any_run) {
      if (!any_pau) {
        min_rem = slots[i].remaining_sec;
        act_dur = slots[i].duration_sec;
      }
      any_pau = true;
    }
  }
  if (!any_run && !any_pau) {
    int v_idx = (eff_view >= 1 && eff_view <= 3) ? (eff_view - 1) : 0;
    min_rem = slots[v_idx].duration_sec;
    act_dur = slots[v_idx].duration_sec;
  }
  out_any_running    = any_run;
  out_any_paused     = any_pau;
  out_min_rem_sec    = min_rem;
  out_active_dur_sec = act_dur;
  out_min_target_ms  = min_t;
}

inline void sync_multi_timer_state(int view_slot = -1, TimerRuntimeState &ts = runtime_state().timers) {
  sync_multi_timer_globals(ts.running, ts.paused, ts.remaining_sec, ts.duration_sec, ts.target_ms,
                           view_slot >= 0 ? view_slot : ts.view_slot);
}

inline int upsert_voice_timer_slot(const std::string &id_str, const std::string &name_str,
                                   int total_sec, int left_sec, bool is_act, uint32_t now_ms) {
  auto *slots = timer_slots();
  int target = -1;

  for (int i = 0; i < 3; i++) {
    if (!id_str.empty() && slots[i].id == id_str) { target = i; break; }
    if (!name_str.empty() && !slots[i].name.empty()) {
      std::string a = slots[i].name, b = name_str;
      for (char &c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      for (char &c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (a == b) { target = i; break; }
    }
  }

  if (target < 0) {
    if (!slots[0].running && !slots[0].paused) {
      if (!slots[1].running && !slots[1].paused && !slots[2].running && !slots[2].paused) {
        slots[2] = slots[1];
        slots[1] = slots[0];
      }
      target = 0;
    } else if (!slots[1].running && !slots[1].paused) {
      if (!slots[2].running && !slots[2].paused) {
        slots[2] = slots[1];
      }
      target = 1;
    } else {
      target = 2;
    }
  }

  if (total_sec <= 0) total_sec = std::max(60, left_sec);
  bool same_id = (!id_str.empty() && slots[target].id == id_str);
  slots[target].id = id_str;
  if (!name_str.empty() || !same_id) {
    slots[target].name = name_str;
  }
  slots[target].duration_sec  = total_sec;
  slots[target].remaining_sec = left_sec;
  slots[target].target_ms     = now_ms + static_cast<uint32_t>(left_sec) * 1000U;
  slots[target].running       = is_act;
  slots[target].paused        = !is_act;
  return target;
}

inline LinkState &link_state() {
  static LinkState st;
  return st;
}

inline WholeHomeState &whole_home_state() {
  static WholeHomeState st;
  return st;
}

inline AreaContextState &area_ctx_state() {
  static AreaContextState st;
  return st;
}

inline MediaState &media_state() {
  static MediaState st;
  return st;
}

inline RemoteState &remote_state() {
  static RemoteState st;
  return st;
}

inline WeatherState &weather_state() {
  static WeatherState st;
  return st;
}

inline CalendarState &calendar_state() {
  static CalendarState st;
  return st;
}

// Zero-allocation tokenizer for \x1f-delimited Protocol v3 payloads
inline size_t v3_split_fields(std::string_view payload, std::string_view *out, size_t max_fields, char sep = '\x1f') {
  if (payload.empty() || max_fields == 0) return 0;
  size_t count = 0;
  size_t start = 0;
  while (count < max_fields) {
    size_t pos = payload.find(sep, start);
    if (pos == std::string_view::npos) {
      out[count++] = payload.substr(start);
      break;
    }
    out[count++] = payload.substr(start, pos - start);
    start = pos + 1;
  }
  return count;
}

inline int v3_parse_int(std::string_view sv, int fallback = 0) {
  if (sv.empty()) return fallback;
  char buf[24];
  size_t len = (sv.size() < sizeof(buf) - 1) ? sv.size() : (sizeof(buf) - 1);
  std::memcpy(buf, sv.data(), len);
  buf[len] = '\0';
  char *endp = nullptr;
  long v = std::strtol(buf, &endp, 10);
  if (endp == buf) return fallback;
  return static_cast<int>(v);
}

inline uint32_t v3_parse_u32(std::string_view sv, uint32_t fallback = 0) {
  if (sv.empty()) return fallback;
  char buf[24];
  size_t len = (sv.size() < sizeof(buf) - 1) ? sv.size() : (sizeof(buf) - 1);
  std::memcpy(buf, sv.data(), len);
  buf[len] = '\0';
  char *endp = nullptr;
  unsigned long v = std::strtoul(buf, &endp, 10);
  if (endp == buf) return fallback;
  return static_cast<uint32_t>(v);
}

inline float v3_parse_float(std::string_view sv, float fallback = NAN) {
  if (sv.empty()) return fallback;
  char buf[32];
  size_t len = (sv.size() < sizeof(buf) - 1) ? sv.size() : (sizeof(buf) - 1);
  std::memcpy(buf, sv.data(), len);
  buf[len] = '\0';
  char *endp = nullptr;
  float v = std::strtof(buf, &endp);
  if (endp == buf) return fallback;
  return v;
}

inline bool v3_parse_bool(std::string_view sv) {
  return (sv == "1" || sv == "true" || sv == "True" || sv == "on");
}

inline bool apply_if_newer(uint32_t &cur_epoch, uint32_t &cur_rev, uint32_t in_epoch, uint32_t in_rev) {
  if (in_epoch == 0 || in_rev == 0) return false;
  if (in_epoch != cur_epoch) {
    cur_epoch = in_epoch;
    cur_rev = in_rev;
    return true;
  }
  if (in_rev > cur_rev) {
    cur_rev = in_rev;
    return true;
  }
  return false;
}

inline uint32_t &v3_mismatch_warn_count() {
  static uint32_t cnt = 0;
  return cnt;
}

inline void v3_check_field_count(const char *domain, std::string_view payload, size_t expected) {
  const size_t got = payload.empty() ? 0 : (1U + static_cast<size_t>(
      std::count(payload.begin(), payload.end(), kProtocolFieldSep)));
  if (got != expected) {
    v3_mismatch_warn_count()++;
#ifdef ESP_LOGW
    ESP_LOGW("latter_v3", "Domain %s field count %u != expected %u (tolerated)",
             domain, static_cast<unsigned>(got), static_cast<unsigned>(expected));
#else
    (void)domain;
#endif
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
  v3_mismatch_warn_count() = 0;
}

inline LockState parse_lock_state_str(std::string_view sv) {
  if (sv == "locked") return LockState::LOCKED;
  if (sv == "unlocked" || sv == "unlocking" || sv == "open") return LockState::UNLOCKED;
  return LockState::UNKNOWN;
}

inline GarageState parse_garage_state_str(std::string_view sv) {
  if (sv == "open" || sv == "opening") return GarageState::OPEN;
  if (sv == "closed" || sv == "closing") return GarageState::CLOSED;
  return GarageState::UNKNOWN;
}

inline std::string normalize_vacuum_state_str(std::string_view sv) {
  if (sv.empty()) return "";
  if (sv == "cleaning" || sv == "Cleaning") return "Cleaning";
  if (sv == "paused" || sv == "Paused") return "Paused";
  if (sv == "returning" || sv == "Returning") return "Returning";
  if (sv == "docked" || sv == "Docked" || sv == "idle") return "Docked";
  return std::string(sv);
}

// Parse Protocol v3 area_context payload ("v3\x1f<epoch>\x1f<rev>\x1f...")
inline bool parse_v3_area_ctx_payload(std::string_view payload,
                                      AreaContextState &st = area_ctx_state(),
                                      WholeHomeState &wh = whole_home_state()) {
  constexpr size_t kCount = static_cast<size_t>(AreaCtxField::COUNT);
  std::string_view f[kCount]{};
  size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(AreaCtxField::HEADER_VERSION)] != "v3") return false;
  uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(AreaCtxField::HEADER_EPOCH)], 0);
  uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(AreaCtxField::HEADER_REV)], 0);
  if (!apply_if_newer(st.epoch, st.rev, epoch, rev)) return false;
  v3_check_field_count("area_context", payload, kCount);

  st.area_id           = std::string(f[static_cast<size_t>(AreaCtxField::AREA_ID)]);
  st.area_name         = std::string(f[static_cast<size_t>(AreaCtxField::AREA_NAME)]);
  st.site_id           = std::string(f[static_cast<size_t>(AreaCtxField::SITE_ID)]);
  st.ble_closest_proxy = std::string(f[static_cast<size_t>(AreaCtxField::BLE_CLOSEST_PROXY)]);
  st.dock_apps         = std::string(f[static_cast<size_t>(AreaCtxField::DOCK_APPS)]);
  st.wallpaper_url     = std::string(f[static_cast<size_t>(AreaCtxField::WALLPAPER_URL)]);
  st.theme_color_hex   = std::string(f[static_cast<size_t>(AreaCtxField::THEME_COLOR_HEX)]);
  st.available_domains = std::string(f[static_cast<size_t>(AreaCtxField::AVAILABLE_DOMAINS)]);
  st.area_temp         = v3_parse_float(f[static_cast<size_t>(AreaCtxField::AREA_TEMP)], NAN);
  st.area_humidity     = v3_parse_float(f[static_cast<size_t>(AreaCtxField::AREA_HUMIDITY)], NAN);
  st.lights_on         = v3_parse_int(f[static_cast<size_t>(AreaCtxField::LIGHTS_ON)], 0);
  st.alarm_entity      = std::string(f[static_cast<size_t>(AreaCtxField::ALARM_ENTITY)]);
  st.alarm_state       = std::string(f[static_cast<size_t>(AreaCtxField::ALARM_STATE)]);

  wh.thermostat_mode = std::string(f[static_cast<size_t>(AreaCtxField::THERMOSTAT_STATE)]);
  float th_temp = v3_parse_float(f[static_cast<size_t>(AreaCtxField::THERMOSTAT_TEMP)], NAN);
  wh.thermostat_target_f = (!std::isnan(th_temp) && th_temp >= 40.0f && th_temp <= 100.0f)
                               ? static_cast<int>(std::round(th_temp))
                               : -1;  // no setpoint: unknown
  wh.thermostat_current_f = v3_parse_float(f[static_cast<size_t>(AreaCtxField::THERMOSTAT_CURRENT_TEMP)], NAN);
  wh.lock_state           = parse_lock_state_str(f[static_cast<size_t>(AreaCtxField::LOCK_STATE)]);
  wh.garage_state         = parse_garage_state_str(f[static_cast<size_t>(AreaCtxField::GARAGE_STATE)]);
  wh.vacuum_state         = normalize_vacuum_state_str(f[static_cast<size_t>(AreaCtxField::VACUUM_STATE)]);

  st.camera_snapshot_url      = std::string(f[static_cast<size_t>(AreaCtxField::CAMERA_SNAPSHOT_URL)]);
  st.cameras_summary          = std::string(f[static_cast<size_t>(AreaCtxField::CAMERAS_SUMMARY)]);
  st.doorbell_talk_state      = std::string(f[static_cast<size_t>(AreaCtxField::DOORBELL_TALK_STATE)]);
  st.intercom_targets_summary = std::string(f[static_cast<size_t>(AreaCtxField::INTERCOM_TARGETS_SUMMARY)]);
  st.alert_id                 = v3_parse_int(f[static_cast<size_t>(AreaCtxField::ALERT_ID)], 0);
  st.alert_title              = std::string(f[static_cast<size_t>(AreaCtxField::ALERT_TITLE)]);
  st.alert_message            = std::string(f[static_cast<size_t>(AreaCtxField::ALERT_MESSAGE)]);
  st.alert_priority           = std::string(f[static_cast<size_t>(AreaCtxField::ALERT_PRIORITY)]);
  st.alert_duration           = v3_parse_int(f[static_cast<size_t>(AreaCtxField::ALERT_DURATION)], 10);
  st.alert_chime              = v3_parse_bool(f[static_cast<size_t>(AreaCtxField::ALERT_CHIME)]);
  st.alert_camera_url         = std::string(f[static_cast<size_t>(AreaCtxField::ALERT_CAMERA_URL)]);
  st.activity_card            = std::string(f[static_cast<size_t>(AreaCtxField::ACTIVITY_CARD)]);
  st.activity_sources         = std::string(f[static_cast<size_t>(AreaCtxField::ACTIVITY_SOURCES)]);
  st.security_pin_policy      = std::string(f[static_cast<size_t>(AreaCtxField::SECURITY_PIN_POLICY)]);
  return true;
}

static constexpr uint8_t kActivitySourceAll = 0x3E;

inline uint8_t activity_source_mask(const AreaContextState &st = area_ctx_state()) {
  const std::string_view card = st.activity_card.empty() ? std::string_view(kDefaultActivityCard)
                                                         : std::string_view(st.activity_card);
  if (!v3_parse_bool(card)) return 0;
  const std::string_view csv = st.activity_sources.empty() ? std::string_view(kDefaultActivitySources)
                                                           : std::string_view(st.activity_sources);
  uint8_t mask = 0;
  size_t start = 0;
  while (start <= csv.size()) {
    size_t end = csv.find(',', start);
    if (end == std::string_view::npos) end = csv.size();
    const std::string_view key = csv.substr(start, end - start);
    if (key == "timers") mask |= 1U << 1;
    else if (key == "stopwatch") mask |= 1U << 2;
    else if (key == "music_queue") mask |= 1U << 3;
    else if (key == "alarms") mask |= 1U << 4;
    else if (key == "calendar") mask |= 1U << 5;
    start = end + 1;
  }
  return mask;
}

// Parse Protocol v3 active_media payload ("v3\x1f<epoch>\x1f<rev>\x1f...")
inline bool parse_v3_media_payload(std::string_view payload, MediaState &st = media_state()) {
  constexpr size_t kCount = static_cast<size_t>(MediaField::COUNT);
  std::string_view f[kCount]{};
  size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(MediaField::HEADER_VERSION)] != "v3") return false;
  uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(MediaField::HEADER_EPOCH)], 0);
  uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(MediaField::HEADER_REV)], 0);
  if (!apply_if_newer(st.epoch, st.rev, epoch, rev)) return false;
  v3_check_field_count("active_media", payload, kCount);

  st.state          = std::string(f[static_cast<size_t>(MediaField::STATE)]);
  st.title          = std::string(f[static_cast<size_t>(MediaField::TITLE)]);
  st.artist         = std::string(f[static_cast<size_t>(MediaField::ARTIST)]);
  st.album          = std::string(f[static_cast<size_t>(MediaField::ALBUM)]);
  st.duration       = v3_parse_float(f[static_cast<size_t>(MediaField::DURATION)], 0.0f);
  st.position       = v3_parse_float(f[static_cast<size_t>(MediaField::POSITION)], 0.0f);
  float raw_vol     = v3_parse_float(f[static_cast<size_t>(MediaField::VOLUME)], NAN);
  if (!std::isnan(raw_vol)) {
    st.volume     = std::max(0.0f, std::min(1.0f, raw_vol * 0.01f));
    st.has_volume = true;
  } else {
    st.has_volume = false;
  }
  st.media_type     = std::string(f[static_cast<size_t>(MediaField::MEDIA_TYPE)]);
  st.art_url        = std::string(f[static_cast<size_t>(MediaField::ART_URL)]);
  st.active_speaker = std::string(f[static_cast<size_t>(MediaField::ACTIVE_SPEAKER)]);
  st.speaker_name   = std::string(f[static_cast<size_t>(MediaField::SPEAKER_NAME)]);
  st.last_speaker   = std::string(f[static_cast<size_t>(MediaField::LAST_SPEAKER)]);
  st.local_speaker  = std::string(f[static_cast<size_t>(MediaField::LOCAL_SPEAKER)]);
  st.is_local       = v3_parse_bool(f[static_cast<size_t>(MediaField::IS_LOCAL)]);
  return true;
}

// Parse Protocol v3 remote_config payload ("v3\x1f<epoch>\x1f<rev>\x1f<active_slot>\x1f<count>\x1f<slots_summary>")
inline bool parse_v3_remote_payload(std::string_view payload, RemoteState &st = remote_state()) {
  constexpr size_t kCount = static_cast<size_t>(RemoteField::COUNT);
  std::string_view f[kCount]{};
  size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(RemoteField::HEADER_VERSION)] != "v3") return false;
  uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(RemoteField::HEADER_EPOCH)], 0);
  uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(RemoteField::HEADER_REV)], 0);
  if (!apply_if_newer(st.epoch, st.rev, epoch, rev)) return false;
  v3_check_field_count("remote_config", payload, kCount);

  int slot_1based     = v3_parse_int(f[static_cast<size_t>(RemoteField::ACTIVE_SLOT)], 1);
  st.active_slot      = (slot_1based > 0) ? (slot_1based - 1) : 0;
  st.configured_count = v3_parse_int(f[static_cast<size_t>(RemoteField::CONFIGURED_COUNT)], 0);
  st.slots_summary    = std::string(f[static_cast<size_t>(RemoteField::SLOTS_SUMMARY)]);
  return true;
}

// Parse Protocol v3 weather payload ("v3\x1f<epoch>\x1f<rev>\x1f...")
inline bool parse_v3_weather_payload(std::string_view payload, WeatherState &st = weather_state()) {
  constexpr size_t kCount = static_cast<size_t>(WeatherField::COUNT);
  std::string_view f[kCount]{};
  size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(WeatherField::HEADER_VERSION)] != "v3") return false;
  uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(WeatherField::HEADER_EPOCH)], 0);
  uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(WeatherField::HEADER_REV)], 0);
  if (!apply_if_newer(st.epoch, st.rev, epoch, rev)) return false;
  v3_check_field_count("weather_forecast", payload, kCount);

  st.condition        = std::string(f[static_cast<size_t>(WeatherField::CONDITION)]);
  st.temperature      = v3_parse_float(f[static_cast<size_t>(WeatherField::TEMPERATURE)], NAN);
  st.humidity         = v3_parse_float(f[static_cast<size_t>(WeatherField::HUMIDITY)], NAN);
  st.wind_speed       = v3_parse_float(f[static_cast<size_t>(WeatherField::WIND_SPEED)], NAN);
  st.wind_bearing     = v3_parse_float(f[static_cast<size_t>(WeatherField::WIND_BEARING)], NAN);
  st.high             = v3_parse_float(f[static_cast<size_t>(WeatherField::HIGH)], NAN);
  st.low              = v3_parse_float(f[static_cast<size_t>(WeatherField::LOW)], NAN);
  st.forecast_5d      = std::string(f[static_cast<size_t>(WeatherField::FORECAST_5D)]);
  st.sun_state        = std::string(f[static_cast<size_t>(WeatherField::SUN_STATE)]);
  st.sun_elevation    = v3_parse_float(f[static_cast<size_t>(WeatherField::SUN_ELEVATION)], NAN);
  st.sun_azimuth      = v3_parse_float(f[static_cast<size_t>(WeatherField::SUN_AZIMUTH)], NAN);
  st.sun_next_rising  = std::string(f[static_cast<size_t>(WeatherField::SUN_NEXT_RISING)]);
  st.sun_next_setting = std::string(f[static_cast<size_t>(WeatherField::SUN_NEXT_SETTING)]);
  st.moon_phase       = std::string(f[static_cast<size_t>(WeatherField::MOON_PHASE)]);
  return true;
}

// Parse Protocol v3 calendar payload ("v3\x1f<epoch>\x1f<rev>\x1f<summary>\x1f<count>\x1f<next_event>")
inline bool parse_v3_calendar_payload(std::string_view payload, CalendarState &st = calendar_state()) {
  constexpr size_t kCount = static_cast<size_t>(CalendarField::COUNT);
  std::string_view f[kCount]{};
  size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(CalendarField::HEADER_VERSION)] != "v3") return false;
  uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(CalendarField::HEADER_EPOCH)], 0);
  uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(CalendarField::HEADER_REV)], 0);
  if (!apply_if_newer(st.epoch, st.rev, epoch, rev)) return false;
  v3_check_field_count("calendar_agenda", payload, kCount);

  st.summary = std::string(f[static_cast<size_t>(CalendarField::SUMMARY)]);
  st.count   = v3_parse_int(f[static_cast<size_t>(CalendarField::EVENT_COUNT)], 0);
  return true;
}

struct CalendarRecord {
  std::string_view time;
  std::string_view title;
  std::string_view location;
  bool all_day = false;
};

inline size_t parse_calendar_records(std::string_view summary, CalendarRecord *out, size_t max_records) {
  if (summary.empty() || out == nullptr || max_records == 0) return 0;
  constexpr size_t kMaxRecords = 8;
  std::string_view recs[kMaxRecords];
  const size_t n = v3_split_fields(summary, recs, max_records < kMaxRecords ? max_records : kMaxRecords,
                                   kProtocolRecordSep);
  size_t count = 0;
  for (size_t i = 0; i < n; i++) {
    std::string_view f[4];
    if (v3_split_fields(recs[i], f, 4, '|') != 4) continue;
    out[count].time = f[0];
    out[count].title = f[1];
    out[count].location = f[2];
    out[count].all_day = (f[3] == "1");
    count++;
  }
  return count;
}

// Parse Contract v3 packed browse/area slots ("name\x1fid\x1ftype\x1factive\x1flevel\x1e...")
inline size_t parse_v3_slots_payload(std::string_view slots_blob, BrowseSlotItem *out_slots, size_t max_slots) {
  for (size_t i = 0; i < max_slots; i++) {
    out_slots[i] = BrowseSlotItem{};
  }
  if (slots_blob.empty() || max_slots == 0) return 0;

  std::string_view recs[16];
  size_t rec_cnt = v3_split_fields(slots_blob, recs, (max_slots < 16) ? max_slots : 16, '\x1e');
  size_t valid_cnt = 0;
  for (size_t i = 0; i < rec_cnt && valid_cnt < max_slots; i++) {
    if (recs[i].empty()) continue;
    std::string_view cols[6];
    size_t col_cnt = v3_split_fields(recs[i], cols, 6, '\x1f');
    if (col_cnt >= 2) {
      out_slots[valid_cnt].name   = std::string(cols[0]);
      out_slots[valid_cnt].id     = std::string(cols[1]);
      out_slots[valid_cnt].type   = (col_cnt >= 3) ? std::string(cols[2]) : "";
      out_slots[valid_cnt].active = (col_cnt >= 4) ? v3_parse_bool(cols[3]) : false;
      out_slots[valid_cnt].level  = (col_cnt >= 5) ? v3_parse_int(cols[4], -1) : -1;
      valid_cnt++;
    }
  }
  return valid_cnt;
}

template <typename StrT, typename = decltype(std::declval<const StrT &>().c_str())>
inline size_t parse_v3_slots_payload(const StrT &slots_blob, BrowseSlotItem *out_slots, size_t max_slots) {
  return parse_v3_slots_payload(std::string_view(slots_blob.c_str(), slots_blob.size()), out_slots, max_slots);
}

// Shared slot-response applier for both set_browse_slots and set_area_slots.
inline bool apply_slots_action(int contract, int req_id, std::string_view mode,
                               int page, int total_pages, std::string_view slots_blob,
                               uint32_t now_ms = 0, bool force_area_entities = false,
                               DeviceRuntimeState &rt = runtime_state()) {
  (void)contract;
  const latter_rpc::SlotChannel ch =
      force_area_entities ? latter_rpc::SlotChannel::AREA_ENTITIES
                          : latter_rpc::classify_browse_mode(mode);
  if (!latter_rpc::accept_req_id(ch, req_id)) return false;

  const size_t max_items = (ch == latter_rpc::SlotChannel::AREA_ENTITIES) ? 3U : DeviceRuntimeState::kSlotsPerPage;
  parse_v3_slots_payload(slots_blob, rt.slots_for(ch), max_items);

  if (ch == latter_rpc::SlotChannel::MUSIC) {
    if (now_ms == 0 || (now_ms - rt.last_music_page_req_ms) > 350U || page == rt.slot_page_for(ch)) {
      rt.slot_page_for(ch) = page;
    }
  } else {
    rt.slot_page_for(ch) = page;
  }
  rt.slot_total_pages_for(ch) = total_pages;

  if (ch == latter_rpc::SlotChannel::AREA_ENTITIES) {
    rt.area.browse_mode = force_area_entities ? std::string(mode) : std::string("entities");
  }
  return true;
}

// Shared notification/alert banner populator for area_context push and show_notification action.
inline bool trigger_alert_banner(std::string_view title, std::string_view message,
                                 std::string_view priority, int duration_s,
                                 std::string_view camera_url, int alert_id,
                                 uint32_t now_ms, bool seed_only_if_unseen = false,
                                 AlertBannerState &ab = runtime_state().alert) {
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
  int dur = (duration_s > 0) ? duration_s : 10;
  if (dur < 3) dur = 3;
  if (dur > 60) dur = 60;
  ab.duration_ms = static_cast<uint32_t>(dur) * 1000U;
  ab.start_ms = now_ms;
  ab.camera_url = std::string(camera_url);
  ab.active = true;
  return true;
}

// ==============================================================================
// Canonical Music Playback Override & Volume Feedback Helpers
// ==============================================================================

inline int &remote_vol_pct() {
  static int pct = 70;
  return pct;
}

inline uint32_t &last_local_vol_tap_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline uint32_t &remote_play_override_ms() {
  static uint32_t ms = 0;
  return ms;
}

inline bool &remote_play_override_state() {
  static bool st = false;
  return st;
}

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

}  // namespace latter_ui
