// ==============================================================================
// Latter Compiled Remote Page Navigation & Volatile Showcase / Filming Engine
// (components/latter_ui/showcase_engine.cpp)
// Implements nav:* / nav_silent:* / showcase:* on latter_device_command (D-077).
// All showcase overrides live strictly in RAM (DeviceRuntimeState::showcase) —
// never written to NVS (PersistedDevicePrefs) and never emitted via set_device_prefs.
// ==============================================================================

#include "ui_touch_engine.h"
#include "ui_pages.h"
#include "hal_display_lifecycle.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

#ifdef USE_ESP32
#include <esp_random.h>
#endif

namespace latter_ui {
using namespace latter_core;

namespace {

inline char ascii_lower(char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool ascii_ieq(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
  }
  return true;
}

inline bool starts_with(std::string_view s, std::string_view prefix) noexcept {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

inline bool parse_uint_sv(std::string_view sv, uint32_t &out) noexcept {
  sv = trim_ascii_ws(sv);
  if (sv.empty()) return false;
  uint32_t val = 0U;
  for (char ch : sv) {
    if (ch < '0' || ch > '9') return false;
    if (val > 400000U) {
      val = 400000U;
      continue;
    }
    val = val * 10U + static_cast<uint32_t>(ch - '0');
  }
  out = val;
  return true;
}

inline bool parse_hex6_rgb_sv(std::string_view sv, uint32_t &out_rgb) noexcept {
  sv = trim_ascii_ws(sv);
  if (sv.size() != 7U || sv[0] != '#') return false;
  uint32_t rgb = 0U;
  for (size_t i = 1U; i < 7U; ++i) {
    const char c = sv[i];
    uint32_t nib = 0U;
    if (c >= '0' && c <= '9') nib = static_cast<uint32_t>(c - '0');
    else if (c >= 'a' && c <= 'f') nib = static_cast<uint32_t>(10 + (c - 'a'));
    else if (c >= 'A' && c <= 'F') nib = static_cast<uint32_t>(10 + (c - 'A'));
    else return false;
    rgb = (rgb << 4) | nib;
  }
  out_rgb = rgb & 0xFFFFFFU;
  return true;
}

inline uint32_t next_showcase_rng(uint32_t now_ms) noexcept {
#ifdef USE_ESP32
  (void)now_ms;
  return esp_random();
#else
  static uint32_t s_rng = 0x51A7C329U;
  s_rng = (s_rng * 1664525U + 1013904223U) ^ (now_ms * 2654435761U);
  return s_rng;
#endif
}

inline int match_va_profile_idx(std::string_view sv) noexcept {
  sv = trim_ascii_ws(sv);
  if (sv.empty()) return -1;
  for (size_t i = 0; i < kPrefOptsVaVisualProfile.size(); ++i) {
    if (ascii_ieq(sv, kPrefOptsVaVisualProfile[i])) return static_cast<int>(i);
  }
  if (ascii_ieq(sv, "light_bar") || ascii_ieq(sv, "lightbar")) {
    return find_pref_option_index(kPrefOptsVaVisualProfile, "Light Bar");
  }
  if (ascii_ieq(sv, "dot_wave") || ascii_ieq(sv, "dotwave")) {
    return find_pref_option_index(kPrefOptsVaVisualProfile, "Dot Wave");
  }
  return -1;
}

inline int match_screensaver_style_idx(std::string_view sv) noexcept {
  sv = trim_ascii_ws(sv);
  if (sv.empty()) return -1;
  for (size_t i = 0; i < kPrefOptsScreensaverStyle.size(); ++i) {
    if (ascii_ieq(sv, kPrefOptsScreensaverStyle[i])) return static_cast<int>(i);
  }
  if (ascii_ieq(sv, "ambient_clock") || ascii_ieq(sv, "ambient")) {
    return find_pref_option_index(kPrefOptsScreensaverStyle, "Ambient Clock");
  }
  if (ascii_ieq(sv, "photo_frame") || ascii_ieq(sv, "photo")) {
    return find_pref_option_index(kPrefOptsScreensaverStyle, "Photo Frame");
  }
  if (ascii_ieq(sv, "digital_rain") || ascii_ieq(sv, "rain")) {
    return find_pref_option_index(kPrefOptsScreensaverStyle, "Digital Rain");
  }
  return -1;
}

inline int match_clock_face_idx(std::string_view sv) noexcept {
  sv = trim_ascii_ws(sv);
  if (sv.empty()) return -1;
  for (size_t i = 0; i < kPrefOptsClockFace.size(); ++i) {
    if (ascii_ieq(sv, kPrefOptsClockFace[i])) return static_cast<int>(i);
  }
  if (ascii_ieq(sv, "minimal_digital") || ascii_ieq(sv, "minimal") || ascii_ieq(sv, "digital")) {
    return find_pref_option_index(kPrefOptsClockFace, "Minimal Digital");
  }
  if (ascii_ieq(sv, "modern_analog") || ascii_ieq(sv, "analog")) {
    return find_pref_option_index(kPrefOptsClockFace, "Modern Analog");
  }
  if (ascii_ieq(sv, "hybrid_digital") || ascii_ieq(sv, "hybrid")) {
    return find_pref_option_index(kPrefOptsClockFace, "Hybrid Digital");
  }
  return -1;
}

inline int match_weather_sim_idx(std::string_view sv) noexcept {
  sv = trim_ascii_ws(sv);
  if (sv.empty()) return -1;
  if (ascii_ieq(sv, "live")) return 0;
  for (size_t i = 0; i < kPrefOptsWeatherSimCondition.size(); ++i) {
    if (ascii_ieq(sv, kPrefOptsWeatherSimCondition[i])) return static_cast<int>(i);
  }
  auto idx_of = [](std::string_view opt) constexpr -> int {
    return find_pref_option_index(kPrefOptsWeatherSimCondition, opt);
  };
  if (ascii_ieq(sv, "clear-night") || ascii_ieq(sv, "clear_night")) return idx_of("Clear Night");
  if (ascii_ieq(sv, "sunny") || ascii_ieq(sv, "clear")) return idx_of("Sunny / Clear Day");
  if (ascii_ieq(sv, "partlycloudy") || ascii_ieq(sv, "partly_cloudy")) return idx_of("Partly Cloudy (Day)");
  if (ascii_ieq(sv, "cloudy") || ascii_ieq(sv, "overcast")) return idx_of("Cloudy / Overcast (Day)");
  if (ascii_ieq(sv, "fog") || ascii_ieq(sv, "mist")) return idx_of("Fog / Mist (Day)");
  if (ascii_ieq(sv, "rain") || ascii_ieq(sv, "rainy")) return idx_of("Rainy (Day)");
  if (ascii_ieq(sv, "pouring") || ascii_ieq(sv, "heavy_rain")) return idx_of("Pouring / Heavy Rain (Day)");
  if (ascii_ieq(sv, "pouring_night") || ascii_ieq(sv, "heavy_rain_night")) return idx_of("Pouring / Heavy Rain (Night)");
  if (ascii_ieq(sv, "snow") || ascii_ieq(sv, "snowy")) return idx_of("Snow (Day)");
  if (ascii_ieq(sv, "thunderstorm") || ascii_ieq(sv, "lightning")) return idx_of("Thunderstorm (Day)");
  if (ascii_ieq(sv, "windy")) return idx_of("Windy (Day)");
  if (ascii_ieq(sv, "windy_night")) return idx_of("Windy (Night)");
  return -1;
}

inline bool is_camera_route_id(RouteId rid) noexcept {
  return rid == RouteId::APP_CAMERAS ||
         rid == RouteId::CAMERAS_GRID ||
         rid == RouteId::CAMERAS_SINGLE ||
         rid == RouteId::CAMERAS_CAROUSEL ||
         rid == RouteId::CAMERAS_SETTINGS;
}

static constexpr uint8_t kShowcaseReelStepCount = 15U;

inline int count_configured_cameras() {
  CameraRecord cams[MAX_CAMERA_SLOTS]{};
  return parse_cameras_records(area_ctx_state().cameras_summary, cams, MAX_CAMERA_SLOTS);
}

inline bool is_reel_step_eligible(uint8_t step, const DeviceRuntimeState &rs, const UiTouchRuntime &rt) {
  switch (step) {
    case 0:   // Home + live weather
    case 1:   // Weather Forecast
    case 2:   // Clock Modern Analog
    case 3:   // VA Nebula cycle
      return true;
    case 4: { // Music Now Playing (only when media is configured/present)
      const auto &ms = media_state();
      return !ms.title.empty() || !ms.state.empty() || !ms.active_speaker.empty() || !rt.track_title.empty();
    }
    case 5:   // Area Control
      return !rs.area.active_area_id.empty();
    case 6:   // Home Hub Thermostat
      return !area_ctx_state().thermostats_summary.empty();
    case 7:   // Cameras Grid / Single
      return count_configured_cameras() > 0;
    case 8:   // Remote
      return has_any_configured_remote();
    case 9:   // Calendar Overview
    case 10:  // Time Timers
    case 11:  // Quick Actions
    case 12:  // Screensaver Aurora
    case 13:  // Screensaver Starfield
    case 14:  // Screensaver Fireworks
      return true;
    default:
      return false;
  }
}

inline uint8_t find_next_eligible_reel_step(uint8_t start_step,
                                            const DeviceRuntimeState &rs,
                                            const UiTouchRuntime &rt) {
  for (uint8_t offset = 0U; offset < kShowcaseReelStepCount; ++offset) {
    const uint8_t cand = static_cast<uint8_t>((start_step + offset) % kShowcaseReelStepCount);
    if (is_reel_step_eligible(cand, rs, rt)) return cand;
  }
  return 0U;
}

inline void merge_effects(DeviceUiCommandEffects &dst, const DeviceUiCommandEffects &src) noexcept {
  dst.handled |= src.handled;
  dst.wake_display |= src.wake_display;
  dst.apply_theme |= src.apply_theme;
  dst.sync_brightness |= src.sync_brightness;
  dst.apply_power |= src.apply_power;
  dst.enter_screensaver |= src.enter_screensaver;
  dst.refresh_screensaver_in_place |= src.refresh_screensaver_in_place;
  dst.turn_screen_off |= src.turn_screen_off;
  dst.fetch_screensaver_photo |= src.fetch_screensaver_photo;
  dst.dismiss_notification |= src.dismiss_notification;
  dst.ensure_anim_poller |= src.ensure_anim_poller;
  dst.schedule_disp |= src.schedule_disp;
  dst.mark_dirty_full |= src.mark_dirty_full;
}

inline void reset_showcase_timer_slot0() {
  auto &s0 = timer_slots()[0];
  s0.running = false;
  s0.paused = false;
  s0.remaining_sec = s0.duration_sec;
  s0.target_ms = 0U;
  s0.name.clear();
  sync_multi_timer_state();
}

inline DeviceUiCommandEffects apply_reel_step(TouchGestureEngine &eng,
                                              uint8_t step,
                                              uint32_t now_ms) {
  auto &rs = runtime_state();
  DeviceUiCommandEffects eff{};
  eff.handled = true;

  // Steps 0..11 are active-screen highlights; clear transient clock/screensaver overrides when leaving them.
  if (step != 2U) rs.showcase.clock_face_override = -1;
  if (step < 12U) rs.showcase.screensaver_style_override = -1;

  switch (step) {
    case 0:
      merge_effects(eff, eng.execute_device_ui_command_impl_("nav:home", now_ms, true));
      break;
    case 1:
      merge_effects(eff, eng.execute_device_ui_command_impl_("nav:weather:forecast", now_ms, true));
      break;
    case 2:
      merge_effects(eff, eng.execute_device_ui_command_impl_("showcase:clock:Modern Analog", now_ms, true));
      break;
    case 3:
      merge_effects(eff, eng.execute_device_ui_command_impl_("showcase:va:cycle:Nebula", now_ms, true));
      break;
    case 4:
      merge_effects(eff, eng.execute_device_ui_command_impl_("nav:app:music", now_ms, true));
      merge_effects(eff, eng.execute_device_ui_command_impl_("nav:music_hud:on", now_ms, true));
      break;
    case 5:
      merge_effects(eff, eng.execute_device_ui_command_impl_("nav:app:area", now_ms, true));
      break;
    case 6:
      merge_effects(eff, eng.execute_device_ui_command_impl_("nav:home_hub:thermostat", now_ms, true));
      break;
    case 7:
      merge_effects(eff, eng.execute_device_ui_command_impl_(
                             (count_configured_cameras() > 1) ? "nav:cameras:grid" : "nav:cameras:single",
                             now_ms, true));
      break;
    case 8:
      merge_effects(eff, eng.execute_device_ui_command_impl_("nav:app:remote", now_ms, true));
      break;
    case 9:
      merge_effects(eff, eng.execute_device_ui_command_impl_("nav:calendar:overview", now_ms, true));
      break;
    case 10:
      merge_effects(eff, eng.execute_device_ui_command_impl_("nav:time:timers", now_ms, true));
      if (!rs.timers.running) {
        merge_effects(eff, eng.execute_device_ui_command_impl_("showcase:timer:300", now_ms, true));
      }
      break;
    case 11:
      merge_effects(eff, eng.execute_device_ui_command_impl_("nav:quick_actions", now_ms, true));
      break;
    case 12:
      merge_effects(eff, eng.execute_device_ui_command_impl_("showcase:screensaver:Aurora", now_ms, true));
      break;
    case 13:
      merge_effects(eff, eng.execute_device_ui_command_impl_("showcase:screensaver:Starfield", now_ms, true));
      break;
    case 14:
      merge_effects(eff, eng.execute_device_ui_command_impl_("showcase:screensaver:Fireworks", now_ms, true));
      break;
    default:
      break;
  }

  // Ensure reel_active stays armed across internal step execution.
  rs.showcase.reel_active = true;
  rs.showcase.reel_step_idx = step;
  rs.showcase.reel_last_step_ms = now_ms;
  return eff;
}

}  // namespace

bool TouchGestureEngine::navigate_route(std::string_view token, uint32_t now_ms) {
  token = trim_ascii_ws(token);
  if (token.empty()) return false;

  auto &rs = runtime_state();

  auto clear_transient_overlays = [&]() {
    shortcut_confirm_state().clear();
    if (ui::pin_pad_state().open) {
      TouchGestureEngine::scrub_pin_and_pending_(*this, rs, false);
    }
    rs.showcase.va_stage = 0U;
    rs.showcase.silent_find_my = false;
    rs.showcase.silent_alarm = false;
    if (rs.alert.active) {
      rs.alert.active = false;
      rt_.cmd(UiHostCmd::DISMISS_NOTIFICATION);
    }
  };

  if (token == "home" || token == "app:home") {
    teardown_before_remote_nav_(rs, now_ms, false);
    if (!persisted_prefs().home_screen_enabled) {
      navigate_to_start_screen(now_ms);
    } else {
      rs.nav.gui_page = PageId::HOME;
      rs.nav.home_subpage = 0;
      mark_dirty_full();
      rt_.schedule_disp();
    }
    touch_gesture().last_touch_ms = now_ms;
    return true;
  }

  if (token == "quick_actions") {
    clear_transient_overlays();
    rs.nav.quick_actions_open = true;
    touch_gesture().last_touch_ms = now_ms;
    mark_dirty_full();
    rt_.schedule_disp();
    return true;
  }

  if (starts_with(token, "dock:")) {
    uint32_t req_page = 0U;
    if (!parse_uint_sv(token.substr(5), req_page)) return false;
    if (rs.nav.gui_page != PageId::HOME || rs.nav.home_subpage != 0) return false;

    clear_transient_overlays();
    rs.nav.quick_actions_open = false;

    ShortcutItem *dock_items = dock_items_scratch();
    const int dock_cnt = collect_effective_dock_items(dock_items, kMaxEffectiveDockItems);
    const int dock_cap = std::clamp(layout().dock_slot_count, 1, kMaxDockSlots);
    const int dock_pages = std::max(1, (dock_cnt + dock_cap - 1) / dock_cap);
    const int cur_pg = std::clamp(home_dock_page(), 0, dock_pages - 1);
    const int target_pg = std::clamp<int>(std::clamp<int>(static_cast<int>(req_page), 0, 5), 0, dock_pages - 1);

    const int cur_start = get_home_dock_start_index(dock_cnt, cur_pg);
    const int new_start = get_home_dock_start_index(dock_cnt, target_pg);
    auto &anim = home_dock_anim();
    if (anim.target_start < 0) {
      anim.snap(cur_start);
    }
    home_dock_page() = target_pg;
    if (new_start != anim.target_start) {
      anim.from_start = anim.cur_start;
      anim.target_start = new_start;
      anim.win_start_ms = now_ms;
      anim.win_animating = true;
    } else {
      (void)anim.step_window(new_start, now_ms);
    }
    touch_gesture().last_touch_ms = now_ms;
    rt_.cmd(UiHostCmd::ENSURE_UI_ANIM_POLLER);
    rt_.schedule_band_disp(dock_dirty_band_y0(), SCREEN_H - 1);
    rt_.schedule_disp();
    return true;
  }

  if (token == "music_hud:on") {
    rs.music.hud_active = true;
    rs.music.hud_timer = now_ms;
    if (rs.showcase.is_active(now_ms)) {
      rs.showcase.music_hud_lock = true;
    }
    touch_gesture().last_touch_ms = now_ms;
    mark_dirty_full();
    rt_.schedule_disp();
    return true;
  }

  if (token == "music_hud:off") {
    rs.music.hud_active = false;
    rs.showcase.music_hud_lock = false;
    touch_gesture().last_touch_ms = now_ms;
    mark_dirty_full();
    rt_.schedule_disp();
    return true;
  }

  RouteId target_rid = RouteId::NONE;
  if (token == "app:settings") {
    target_rid = RouteId::SETTINGS_ROOT;
  } else {
    const ShortcutRouteSpec *spec = find_shortcut_route(token);
    if (spec == nullptr) return false;
    target_rid = spec->id;
  }

  if (!is_route_supported_on_board(target_rid)) {
    return false;
  }
  if ((target_rid == RouteId::APP_REMOTE || target_rid == RouteId::REMOTE_DEVICES) &&
      !has_any_configured_remote()) {
    return false;
  }

  teardown_before_remote_nav_(rs, now_ms, is_camera_route_id(target_rid));
  TouchGestureEngine::launch_shortcut_route_(target_rid, ShortcutOrigin::NONE, rs, rt_, now_ms);
  touch_gesture().last_touch_ms = now_ms;
  return true;
}

DeviceUiCommandEffects TouchGestureEngine::clear_showcase(uint32_t now_ms) {
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  DeviceUiCommandEffects eff{};
  eff.handled = true;

  const bool had_weather_sim = (rs.showcase.weather_sim_override >= 0);
  const bool had_timer = rs.showcase.timer_owned;
  const bool had_stopwatch = rs.showcase.stopwatch_owned;
  const bool had_hud_lock = rs.showcase.music_hud_lock;
  const bool had_ss_override = (rs.showcase.screensaver_style_override >= 0);

  rs.showcase.clear_all();

  if (had_weather_sim) {
    weather_sim_dirty() = true;
    record_ambient_frame_ms(0);
  }
  if (had_timer) {
    reset_showcase_timer_slot0();
  }
  if (had_stopwatch) {
    rs.stopwatch.reset();
  }
  if (had_hud_lock) {
    rs.music.hud_timer = now_ms;
  }

  if (rt_.theme_primary != nullptr && rt_.theme_secondary != nullptr) {
    apply_effective_theme_slots(rs, prefs, now_ms, rt_.theme_primary, rt_.theme_secondary);
  }

  display_lifecycle().note_activity(now_ms);
  eff.apply_theme = true;
  eff.sync_brightness = true;
  eff.apply_power = true;
  eff.mark_dirty_full = true;
  eff.schedule_disp = (rs.nav.display_power_state != 2);
  if (had_ss_override && rs.nav.display_power_state == 1) {
    const auto exit_act = evaluate_persisted_screensaver_exit_action(
        prefs, LATTER_IS_STATIONARY != 0, rs.hw_usb_connected);
    if (exit_act == ScreensaverExitAction::TURN_SCREEN_OFF) {
      eff.turn_screen_off = true;
      eff.schedule_disp = false;
    } else if (exit_act == ScreensaverExitAction::WAKE_TO_ACTIVE) {
      eff.wake_display = true;
    } else {
      eff.refresh_screensaver_in_place = true;
      if (std::string_view(effective_screensaver_style_cstr(prefs, rs, now_ms)) == "Photo Frame") {
        rs.nav.standby_entered_ms = (now_ms != 0U) ? now_ms : 1U;
        if (!area_ctx_state().screensaver_photo_url.empty()) {
          rs.music.screensaver_photo_failed = false;
          eff.fetch_screensaver_photo = true;
        }
      }
    }
  }
  return eff;
}

DeviceUiCommandEffects TouchGestureEngine::tick_showcase(uint32_t now_ms) {
  auto &rs = runtime_state();
  DeviceUiCommandEffects eff{};
  if (!rs.showcase.active) return eff;

  if (rs.showcase.is_expired(now_ms)) {
    return clear_showcase(now_ms);
  }

  if (rs.showcase.reel_active) {
    const uint32_t dwell_ms =
        static_cast<uint32_t>(std::clamp<uint8_t>(
            rs.showcase.reel_dwell_sec, ShowcaseState::kMinReelDwellSec, ShowcaseState::kMaxReelDwellSec)) *
        1000U;
    if (static_cast<uint32_t>(now_ms - rs.showcase.reel_last_step_ms) >= dwell_ms) {
      const uint8_t next_step = find_next_eligible_reel_step(
          static_cast<uint8_t>((rs.showcase.reel_step_idx + 1U) % kShowcaseReelStepCount), rs, rt_);
      return apply_reel_step(*this, next_step, now_ms);
    }
  }
  return eff;
}

DeviceUiCommandEffects TouchGestureEngine::execute_device_ui_command(std::string_view cmd, uint32_t now_ms) {
  return execute_device_ui_command_impl_(cmd, now_ms, false);
}

DeviceUiCommandEffects TouchGestureEngine::execute_device_ui_command_impl_(std::string_view cmd,
                                                                           uint32_t now_ms,
                                                                           bool from_reel) {
  DeviceUiCommandEffects eff{};
  cmd = trim_ascii_ws(cmd);
  if (cmd.empty()) return eff;

  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();

  // 1. Remote navigation: nav:<route> and nav_silent:<route>
  if (starts_with(cmd, "nav:") || starts_with(cmd, "nav_silent:")) {
    const bool silent = starts_with(cmd, "nav_silent:");
    const std::string_view route = trim_ascii_ws(cmd.substr(silent ? 11U : 4U));
    eff.handled = true;

    const bool had_alert = rs.alert.active;
    if (!navigate_route(route, now_ms)) {
      return eff;
    }
    if (!from_reel) {
      rs.showcase.reel_active = false;
    }
    if (had_alert && !rs.alert.active) {
      eff.dismiss_notification = true;
    }
    if (should_wake_for_nav(silent, rs, prefs, now_ms, rt_.panel_is_on)) {
      eff.wake_display = true;
    } else if (rs.nav.display_power_state == 0 && rt_.panel_is_on) {
      display_lifecycle().note_activity(now_ms);
      eff.mark_dirty_full = true;
      eff.schedule_disp = true;
    }
    if (starts_with(route, "dock:")) {
      eff.ensure_anim_poller = true;
    }
    eff.apply_power = true;
    return eff;
  }

  // 2. Volatile Showcase / Filming Engine: showcase:<subcmd>
  if (!starts_with(cmd, "showcase:")) {
    return eff;
  }

  const std::string_view sub = trim_ascii_ws(cmd.substr(9U));
  if (sub.empty()) return eff;

  if (ascii_ieq(sub, "off")) {
    return clear_showcase(now_ms);
  }

  // showcase:on or showcase:on:<sec>
  if (ascii_ieq(sub, "on") || starts_with(sub, "on:")) {
    uint32_t dur_sec = ShowcaseState::kDefaultKeepAwakeSec;
    if (starts_with(sub, "on:")) {
      if (!parse_uint_sv(sub.substr(3U), dur_sec)) return eff;
    }
    eff.handled = true;
    if (!from_reel) rs.showcase.reel_active = false;
    rs.showcase.arm(now_ms, dur_sec);
    display_lifecycle().note_activity(now_ms);
    if (rs.nav.display_power_state != 0 || !rt_.panel_is_on) {
      eff.wake_display = true;
    }
    eff.apply_power = true;
    eff.ensure_anim_poller = true;
    eff.schedule_disp = true;
    return eff;
  }

  // showcase:va:<stage>[:<profile>]
  if (starts_with(sub, "va:")) {
    const std::string_view rest = sub.substr(3U);
    const size_t colon = rest.find(':');
    const std::string_view stage_sv = trim_ascii_ws((colon == std::string_view::npos) ? rest : rest.substr(0, colon));
    const std::string_view prof_sv = (colon == std::string_view::npos) ? std::string_view{} : trim_ascii_ws(rest.substr(colon + 1U));

    uint8_t stage_val = 255U;
    if (ascii_ieq(stage_sv, "off")) stage_val = static_cast<uint8_t>(ShowcaseVaStage::OFF);
    else if (ascii_ieq(stage_sv, "listening")) stage_val = static_cast<uint8_t>(ShowcaseVaStage::LISTENING);
    else if (ascii_ieq(stage_sv, "vad")) stage_val = static_cast<uint8_t>(ShowcaseVaStage::VAD);
    else if (ascii_ieq(stage_sv, "thinking")) stage_val = static_cast<uint8_t>(ShowcaseVaStage::THINKING);
    else if (ascii_ieq(stage_sv, "speaking") || ascii_ieq(stage_sv, "responding")) {
      stage_val = static_cast<uint8_t>(ShowcaseVaStage::SPEAKING);
    } else if (ascii_ieq(stage_sv, "cycle")) {
      stage_val = static_cast<uint8_t>(ShowcaseVaStage::CYCLE);
    } else {
      return eff;
    }

    int prof_idx = -1;
    if (!prof_sv.empty()) {
      prof_idx = match_va_profile_idx(prof_sv);
      if (prof_idx < 0) return eff;
    }

    eff.handled = true;
    if (!from_reel) rs.showcase.reel_active = false;
    rs.showcase.touch_keep_awake(now_ms);
    rs.showcase.va_stage = stage_val;
    rs.showcase.va_stage_start_ms = now_ms;
    if (prof_idx >= 0) {
      rs.showcase.va_profile_override = static_cast<int8_t>(prof_idx);
    }
    if (stage_val != 0U) {
      rs.showcase.silent_find_my = false;
      rs.showcase.silent_alarm = false;
      if (rs.nav.display_power_state != 0 || !rt_.panel_is_on) {
        eff.wake_display = true;
      }
      eff.ensure_anim_poller = true;
    }
    eff.apply_power = true;
    eff.mark_dirty_full = true;
    eff.schedule_disp = true;
    return eff;
  }

  // showcase:screensaver:<style>
  if (starts_with(sub, "screensaver:")) {
    const int style_idx = match_screensaver_style_idx(sub.substr(12U));
    if (style_idx < 0) return eff;

    eff.handled = true;
    if (!from_reel) rs.showcase.reel_active = false;
    rs.showcase.touch_keep_awake(now_ms);
    rs.showcase.va_stage = 0U;
    rs.showcase.silent_find_my = false;
    rs.showcase.silent_alarm = false;
    rs.nav.quick_actions_open = false;
    rs.showcase.screensaver_style_override = static_cast<int8_t>(style_idx);

    const bool is_photo = (style_idx == find_pref_option_index(kPrefOptsScreensaverStyle, "Photo Frame"));
    if (rs.nav.display_power_state == 1) {
      eff.refresh_screensaver_in_place = true;
      if (is_photo) {
        rs.nav.standby_entered_ms = (now_ms != 0U) ? now_ms : 1U;
      }
    } else {
      display_lifecycle().on_enter_screensaver(
          now_ms, rs.nav.display_power_state, rs.nav.sleep_start_time, rs.nav.standby_entered_ms);
      eff.enter_screensaver = true;
    }
    if (is_photo && !area_ctx_state().screensaver_photo_url.empty()) {
      rs.music.screensaver_photo_failed = false;
      eff.fetch_screensaver_photo = true;
    }
    eff.apply_power = true;
    eff.sync_brightness = true;
    eff.mark_dirty_full = true;
    eff.schedule_disp = true;
    return eff;
  }

  // showcase:weather:<condition>
  if (starts_with(sub, "weather:")) {
    const int cond_idx = match_weather_sim_idx(sub.substr(8U));
    if (cond_idx < 0) return eff;

    eff.handled = true;
    if (!from_reel) rs.showcase.reel_active = false;
    rs.showcase.touch_keep_awake(now_ms);
    rs.showcase.weather_sim_override = static_cast<int8_t>(cond_idx);
    weather_sim_dirty() = true;
    record_ambient_frame_ms(0);
    eff.ensure_anim_poller = true;
    eff.apply_power = true;
    eff.mark_dirty_full = true;
    eff.schedule_disp = (rs.nav.display_power_state != 2);
    return eff;
  }

  // showcase:clock:<style>
  if (starts_with(sub, "clock:")) {
    const int face_idx = match_clock_face_idx(sub.substr(6U));
    if (face_idx < 0) return eff;

    eff.handled = true;
    if (!from_reel) rs.showcase.reel_active = false;
    rs.showcase.touch_keep_awake(now_ms);
    rs.showcase.clock_face_override = static_cast<int8_t>(face_idx);
    (void)navigate_route("app:clock", now_ms);
    if (rs.nav.display_power_state != 0 || !rt_.panel_is_on) {
      eff.wake_display = true;
    }
    eff.ensure_anim_poller = true;
    eff.apply_power = true;
    eff.mark_dirty_full = true;
    eff.schedule_disp = true;
    return eff;
  }

  // showcase:theme:<preset|#RRGGBB>
  if (starts_with(sub, "theme:")) {
    const std::string_view arg = trim_ascii_ws(sub.substr(6U));
    if (arg.empty()) return eff;

    uint8_t mode = 0U;
    uint32_t rgb = rs.showcase.theme_override_rgb;
    if (ascii_ieq(arg, "steel blue") || ascii_ieq(arg, "steel_blue")) {
      mode = static_cast<uint8_t>(ShowcaseThemeMode::STEEL_BLUE);
    } else if (ascii_ieq(arg, "pink")) {
      mode = static_cast<uint8_t>(ShowcaseThemeMode::PINK);
    } else if (ascii_ieq(arg, "blue")) {
      mode = static_cast<uint8_t>(ShowcaseThemeMode::BLUE);
    } else if (ascii_ieq(arg, "green")) {
      mode = static_cast<uint8_t>(ShowcaseThemeMode::GREEN);
    } else if (parse_hex6_rgb_sv(arg, rgb)) {
      mode = static_cast<uint8_t>(ShowcaseThemeMode::CUSTOM_RGB);
    } else if (ascii_ieq(arg, "off") || ascii_ieq(arg, "default") || ascii_ieq(arg, "reset")) {
      mode = static_cast<uint8_t>(ShowcaseThemeMode::NONE);
    } else {
      return eff;
    }

    eff.handled = true;
    if (!from_reel) rs.showcase.reel_active = false;
    rs.showcase.touch_keep_awake(now_ms);
    rs.showcase.theme_override_mode = mode;
    if (mode == static_cast<uint8_t>(ShowcaseThemeMode::CUSTOM_RGB)) {
      rs.showcase.theme_override_rgb = rgb;
    }
    if (rt_.theme_primary != nullptr && rt_.theme_secondary != nullptr) {
      apply_effective_theme_slots(rs, prefs, now_ms, rt_.theme_primary, rt_.theme_secondary);
    }
    eff.apply_theme = true;
    eff.apply_power = true;
    eff.mark_dirty_full = true;
    eff.schedule_disp = (rs.nav.display_power_state != 2);
    return eff;
  }

  // showcase:brightness:<1..100|0>
  if (starts_with(sub, "brightness:")) {
    uint32_t pct = 0U;
    if (!parse_uint_sv(sub.substr(11U), pct)) return eff;

    eff.handled = true;
    if (!from_reel) rs.showcase.reel_active = false;
    rs.showcase.touch_keep_awake(now_ms);
    rs.showcase.brightness_override_pct = (pct == 0U) ? 0U : static_cast<uint8_t>(std::clamp<uint32_t>(pct, 1U, 100U));
    eff.sync_brightness = true;
    eff.apply_power = true;
    return eff;
  }

  // showcase:timer:<sec|off>
  if (starts_with(sub, "timer:")) {
    const std::string_view arg = trim_ascii_ws(sub.substr(6U));
    if (ascii_ieq(arg, "off")) {
      eff.handled = true;
      if (!from_reel) rs.showcase.reel_active = false;
      rs.showcase.touch_keep_awake(now_ms);
      if (rs.showcase.timer_owned) {
        rs.showcase.timer_owned = false;
        reset_showcase_timer_slot0();
      }
      eff.apply_power = true;
      eff.mark_dirty_full = true;
      eff.schedule_disp = (rs.nav.display_power_state != 2);
      return eff;
    }

    uint32_t raw_sec = 0U;
    if (!parse_uint_sv(arg, raw_sec)) return eff;
    const int sec = static_cast<int>(std::clamp<uint32_t>(raw_sec, 5U, 3600U));

    eff.handled = true;
    if (!from_reel) rs.showcase.reel_active = false;
    rs.showcase.touch_keep_awake(now_ms);
    auto &s0 = timer_slots()[0];
    s0.duration_sec = sec;
    s0.remaining_sec = sec;
    s0.running = true;
    s0.paused = false;
    s0.target_ms = now_ms + static_cast<uint32_t>(sec) * 1000U;
    s0.name.clear();
    rs.showcase.timer_owned = true;
    sync_multi_timer_state();
    eff.apply_power = true;
    eff.mark_dirty_full = true;
    eff.schedule_disp = (rs.nav.display_power_state != 2);
    return eff;
  }

  // showcase:stopwatch:<start|stop|reset>
  if (starts_with(sub, "stopwatch:")) {
    const std::string_view arg = trim_ascii_ws(sub.substr(10U));
    if (ascii_ieq(arg, "start")) {
      eff.handled = true;
      if (!from_reel) rs.showcase.reel_active = false;
      rs.showcase.touch_keep_awake(now_ms);
      if (!rs.stopwatch.running) {
        rs.stopwatch.start_time_ms = now_ms - rs.stopwatch.elapsed_ms;
        rs.stopwatch.running = true;
        rs.stopwatch.started_in_analog =
            (rs.nav.gui_page == PageId::TOOLS && rs.nav.tools_subpage == ToolsSubpage::STOPWATCH);
      }
      rs.showcase.stopwatch_owned = true;
      eff.ensure_anim_poller = true;
      eff.apply_power = true;
      eff.mark_dirty_full = true;
      eff.schedule_disp = (rs.nav.display_power_state != 2);
      return eff;
    }
    if (ascii_ieq(arg, "stop")) {
      eff.handled = true;
      if (!from_reel) rs.showcase.reel_active = false;
      rs.showcase.touch_keep_awake(now_ms);
      if (rs.stopwatch.running) {
        rs.stopwatch.elapsed_ms = (now_ms >= rs.stopwatch.start_time_ms)
                                      ? (now_ms - rs.stopwatch.start_time_ms)
                                      : rs.stopwatch.elapsed_ms;
        rs.stopwatch.running = false;
      }
      rs.showcase.stopwatch_owned = true;
      eff.apply_power = true;
      eff.mark_dirty_full = true;
      eff.schedule_disp = (rs.nav.display_power_state != 2);
      return eff;
    }
    if (ascii_ieq(arg, "reset")) {
      eff.handled = true;
      if (!from_reel) rs.showcase.reel_active = false;
      rs.showcase.touch_keep_awake(now_ms);
      rs.stopwatch.reset();
      rs.showcase.stopwatch_owned = false;
      eff.apply_power = true;
      eff.mark_dirty_full = true;
      eff.schedule_disp = (rs.nav.display_power_state != 2);
      return eff;
    }
    return eff;
  }

  // showcase:roll (silent Dice roll or Coin flip animation — zero speaker SFX)
  if (ascii_ieq(sub, "roll")) {
    eff.handled = true;
    if (!from_reel) rs.showcase.reel_active = false;
    rs.showcase.touch_keep_awake(now_ms);
    if (rs.nav.gui_page == PageId::TOOLS && rs.nav.tools_subpage == ToolsSubpage::COIN_FLIP) {
      trigger_coin_flip(now_ms, next_showcase_rng(now_ms));
    } else {
      if (rs.nav.gui_page != PageId::TOOLS || rs.nav.tools_subpage != ToolsSubpage::DICE) {
        (void)navigate_route("tools:dice", now_ms);
      }
      trigger_dice_roll(now_ms, next_showcase_rng(now_ms), next_showcase_rng(now_ms ^ 0x9E3779B9U));
    }
    if (rs.nav.display_power_state != 0 || !rt_.panel_is_on) {
      eff.wake_display = true;
    }
    eff.ensure_anim_poller = true;
    eff.apply_power = true;
    eff.mark_dirty_full = true;
    eff.schedule_disp = true;
    return eff;
  }

  // showcase:find_my:<on|off>
  if (starts_with(sub, "find_my:")) {
    const std::string_view arg = trim_ascii_ws(sub.substr(8U));
    const bool turn_on = ascii_ieq(arg, "on");
    const bool turn_off = ascii_ieq(arg, "off");
    if (!turn_on && !turn_off) return eff;

    eff.handled = true;
    if (!from_reel) rs.showcase.reel_active = false;
    rs.showcase.touch_keep_awake(now_ms);
    rs.showcase.silent_find_my = turn_on;
    if (turn_on) {
      rs.showcase.silent_alarm = false;
      rs.showcase.va_stage = 0U;
      if (rs.nav.display_power_state != 0 || !rt_.panel_is_on) {
        eff.wake_display = true;
      }
      eff.ensure_anim_poller = true;
    }
    eff.apply_power = true;
    eff.mark_dirty_full = true;
    eff.schedule_disp = true;
    return eff;
  }

  // showcase:alarm:<on|off>
  if (starts_with(sub, "alarm:")) {
    const std::string_view arg = trim_ascii_ws(sub.substr(6U));
    const bool turn_on = ascii_ieq(arg, "on");
    const bool turn_off = ascii_ieq(arg, "off");
    if (!turn_on && !turn_off) return eff;

    eff.handled = true;
    if (!from_reel) rs.showcase.reel_active = false;
    rs.showcase.touch_keep_awake(now_ms);
    rs.showcase.silent_alarm = turn_on;
    if (turn_on) {
      rs.showcase.silent_find_my = false;
      rs.showcase.va_stage = 0U;
      if (rs.nav.display_power_state != 0 || !rt_.panel_is_on) {
        eff.wake_display = true;
      }
      eff.ensure_anim_poller = true;
    }
    eff.apply_power = true;
    eff.mark_dirty_full = true;
    eff.schedule_disp = true;
    return eff;
  }

  // showcase:reel or showcase:reel:<sec>
  if (ascii_ieq(sub, "reel") || starts_with(sub, "reel:")) {
    uint32_t dwell_sec = ShowcaseState::kDefaultReelDwellSec;
    if (starts_with(sub, "reel:")) {
      if (!parse_uint_sv(sub.substr(5U), dwell_sec)) return eff;
    }
    const uint8_t clamped_dwell = static_cast<uint8_t>(std::clamp<uint32_t>(
        dwell_sec, ShowcaseState::kMinReelDwellSec, ShowcaseState::kMaxReelDwellSec));
    rs.showcase.touch_keep_awake(now_ms);
    rs.showcase.reel_active = true;
    rs.showcase.reel_dwell_sec = clamped_dwell;
    const uint8_t first_step = find_next_eligible_reel_step(0U, rs, rt_);
    return apply_reel_step(*this, first_step, now_ms);
  }

  return eff;
}

}  // namespace latter_ui
