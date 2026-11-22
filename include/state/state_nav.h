#pragma once

// ==============================================================================
// Latter Core SDK: Navigation, Voice Session, Launchers & Start-Screen Policy
// (include/state/state_nav.h)
// ==============================================================================

#include "state/state_common.h"
#include "state/state_prefs.h"
#include "state/state_shortcuts.h"

namespace latter_ui {

struct ToolsLauncherItemSpec {
  int id = 0;
  const char *label = "";
};

inline constexpr int kMaxToolsLauncherItems = 6;
inline constexpr int kMaxGamesLauncherItems = 6;

inline int collect_tools_launcher_items_for(std::string_view csv,
                                            bool round_display,
                                            bool has_imu,
                                            ToolsLauncherItemSpec *out_items,
                                            int max_items = kMaxToolsLauncherItems) noexcept {
  if (out_items == nullptr || max_items <= 0) return 0;
  int count = 0;
  size_t start = 0;
  const int utility_cap = std::min(max_items, kMaxToolsLauncherItems);
  while (!csv.empty() && start <= csv.size() && count < utility_cap) {
    const size_t pos = csv.find(',', start);
    const std::string_view tok = trim_ascii_ws(
        (pos == std::string_view::npos) ? csv.substr(start) : csv.substr(start, pos - start));
    const int sub = tools_subpage_for_token(tok);
    if (sub > 0) {
      const bool cap_ok =
          (sub != ToolsSubpage::STOPWATCH || round_display) &&
          (sub != ToolsSubpage::LEVEL || has_imu);
      if (cap_ok) {
        bool dup = false;
        for (int i = 0; i < count; ++i) {
          if (out_items[i].id == sub) {
            dup = true;
            break;
          }
        }
        if (!dup) {
          out_items[count++] = ToolsLauncherItemSpec{sub, tools_launcher_label_for_subpage(sub)};
        }
      }
    }
    if (pos == std::string_view::npos) break;
    start = pos + 1;
  }
  return count;
}

inline int collect_tools_launcher_items(ToolsLauncherItemSpec *out_items,
                                        const PersistedDevicePrefs &p = persisted_prefs(),
                                        int max_items = kMaxToolsLauncherItems) noexcept {
  return collect_tools_launcher_items_for(
      effective_tools_items_sv(p), LATTER_ROUND_DISPLAY != 0, LATTER_HAS_IMU != 0,
      out_items, max_items);
}

inline int active_tools_launcher_item_count(const PersistedDevicePrefs &p = persisted_prefs()) noexcept {
  ToolsLauncherItemSpec items[kMaxToolsLauncherItems]{};
  return collect_tools_launcher_items(items, p, kMaxToolsLauncherItems);
}

inline int active_tools_launcher_page_count(const PersistedDevicePrefs &p = persisted_prefs()) noexcept {
  const int cnt = active_tools_launcher_item_count(p);
  return (cnt <= 6) ? 1 : ((cnt + 5) / 6);
}

inline int collect_enabled_games_for(std::string_view csv,
                                     int *out_game_ids,
                                     int max_games = kMaxGamesLauncherItems) noexcept {
  if (out_game_ids == nullptr || max_games <= 0 || csv.empty()) return 0;
  int count = 0;
  size_t start = 0;
  const int cap = std::min(max_games, kMaxGamesLauncherItems);
  while (start <= csv.size() && count < cap) {
    const size_t pos = csv.find(',', start);
    const std::string_view tok = trim_ascii_ws(
        (pos == std::string_view::npos) ? csv.substr(start) : csv.substr(start, pos - start));
    const int gid = game_id_for_token(tok);
    if (gid > 0) {
      bool dup = false;
      for (int i = 0; i < count; ++i) {
        if (out_game_ids[i] == gid) {
          dup = true;
          break;
        }
      }
      if (!dup) out_game_ids[count++] = gid;
    }
    if (pos == std::string_view::npos) break;
    start = pos + 1;
  }
  return count;
}

inline int collect_enabled_games(int *out_game_ids,
                                 const PersistedDevicePrefs &p = persisted_prefs(),
                                 int max_games = kMaxGamesLauncherItems) noexcept {
  return collect_enabled_games_for(effective_games_items_sv(p), out_game_ids, max_games);
}

inline int active_games_launcher_count(const PersistedDevicePrefs &p = persisted_prefs()) noexcept {
  int ids[kMaxGamesLauncherItems]{};
  return collect_enabled_games(ids, p, kMaxGamesLauncherItems);
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
  int home_settings_page = 1;
  int home_activity_idx = 0;
  int home_dock_page = 0;
  int time_subpage = 0;
  int world_clock_page = 0;
  uint8_t alarm_edit_field = 1;  // 0 = HH focused, 1 = MM focused
  int sky_weather_subpage = 0;
  int area_subpage = 0;
  int remote_subpage = 0;
  int tools_subpage = 0;
  int tools_menu_page = 0;
  int pinned_menu_page = 0;
  ShortcutOrigin shortcut_origin = ShortcutOrigin::NONE;
  int shortcut_origin_pinned_page = 0;
  std::string pinned_feedback = "";
  uint32_t pinned_feedback_ms = 0;
  ShortcutConfirmState shortcut_confirm{};
  bool quick_actions_open = false;
  std::string pending_shortcut_entity_id = "";
  std::string pending_shortcut_action = "";
  std::string pending_shortcut_label = "";
  int display_menu_page = 1;
  int system_dashboard_page = 1;
  int system_menu_mode = 0;
  int sleep_menu_page = 1;
  int audio_menu_parent_page = 4;
  bool menu_scroll_active = false;
  int menu_scroll_dir = 1;
  int menu_scroll_target_page = 1;
  uint32_t menu_scroll_start_ms = 0;
  int display_power_state = 0;  // 0 = ACTIVE, 1 = SCREENSAVER, 2 = SCREEN_OFF / STANDBY
  uint32_t sleep_start_time = 0;
  uint32_t standby_entered_ms = 0;
  int8_t ambient_bri_override_pct = -1;  // -1 = none, 1..100 = temporary Quick Actions override in Ambient Light mode
  bool ambient_bri_ramp_pending = false; // true when sync_display_brightness should ramp smoothly (600 ms)
  bool wake_greeting_pending = true;
  bool is_hibernating = false;
  int hibernate_countdown_val = 0;
  bool hud_wake_finger_released = true;
  bool pwr_key_wake_consumed = false;
  bool ui_anim_poller_running = true;
  bool ui_dirty_full = false;
  bool ota_in_progress = false;
  int ota_progress_pct = 0;
  int update_check_state = 0;   // 0 = Idle, 1 = Checking..., 2 = Checked
  uint32_t update_check_timer = 0;
  int game_carousel_idx = 0;
  int active_game = 0;
  float marble_touch_x = -1.0f;
  float marble_touch_y = -1.0f;
  bool game_btn_pressed = false;
  int last_pub_display_power_state = -1;
  int last_pub_display_bri_pct = -1;
};


struct HelloSyncState {
  bool in_flight = false;
  uint32_t started_ms = 0;
  uint32_t last_sent_ms = 0;
  PrefMask last_sent_dirty_mask{};
};


inline constexpr uint32_t kStandbyGreetingIdleMs = 1800000U;

inline bool is_cameras_start_screen(const PersistedDevicePrefs &p = persisted_prefs()) noexcept {
  return std::string_view(pref_start_screen_cstr(p)) == "Cameras";
}

inline int resolve_anchor_dock_app(const PersistedDevicePrefs &p = persisted_prefs()) {
  const std::string_view opt(pref_start_screen_cstr(p));
  if (opt == "Home") return p.home_screen_enabled ? PageId::HOME : PageId::TIME;
  if (opt == "Pinned") return PageId::PINNED;
  if (opt == "Time App") return PageId::TIME;
  if (opt == "Calendar") return PageId::CALENDAR;
  if (opt == "Clock Face") return PageId::CLOCK;
  if (opt == "Media") return PageId::MUSIC;
  if (opt == "Home Hub") return PageId::HOME_HUB;
  if (opt == "Cameras") return PageId::CAMERAS;
  if (opt == "Area Controls") return PageId::AREA;
  if (opt == "Remote") return PageId::REMOTE;
  if (opt == "Weather") return PageId::WEATHER;
  if (opt == "Games") return PageId::GAMES;
  if (opt == "Tools") return PageId::TOOLS;
  return p.home_screen_enabled ? PageId::HOME : PageId::TIME;
}

inline int resolve_start_screen_page(const PersistedDevicePrefs &p = persisted_prefs()) {
  return resolve_anchor_dock_app(p);
}

inline int resolve_start_screen_page(int app) {
  return app;
}


}  // namespace latter_ui
