#include "test_harness.h"

namespace latter_test {

namespace {

void test_device_prefs_v2_and_event_timer_sync() {
  static_assert(std::is_trivially_copyable_v<latter_core::PersistedDevicePrefs>);
  LATTER_CHECK(latter_core::kPersistedPrefsVersion == 1U);
  LATTER_CHECK(static_cast<size_t>(latter_core::PrefKey::COUNT) == 72U);
  LATTER_CHECK(static_cast<size_t>(latter_core::DevicePrefsField::COUNT) == 78U);
  LATTER_CHECK(static_cast<size_t>(latter_core::AreaCtxField::COUNT) == 43U);
  LATTER_CHECK(static_cast<size_t>(latter_core::ShortcutsField::COUNT) == 9U);

  LATTER_CHECK(latter_core::PageId::HOME_HUB == 9);
  LATTER_CHECK(latter_core::PageId::TIME == 10);
  LATTER_CHECK(latter_core::PageId::PINNED == 11);
  LATTER_CHECK(latter_core::PageId::CALENDAR == 12);
  LATTER_CHECK(latter_core::PageId::CAMERAS == 13);
  LATTER_CHECK(latter_core::find_shortcut_route("app:pinned") != nullptr);
  LATTER_CHECK(latter_core::find_shortcut_route("app:calendar") != nullptr);
  LATTER_CHECK(latter_core::find_shortcut_route("calendar:month") != nullptr);
  LATTER_CHECK(latter_core::find_shortcut_route("tools:calendar") == nullptr);
  LATTER_CHECK(latter_core::find_shortcut_route("weather:moon") != nullptr);
  LATTER_CHECK(latter_core::find_shortcut_route("app:cameras") != nullptr);
  LATTER_CHECK(latter_core::find_shortcut_route("cameras:grid") != nullptr);
  LATTER_CHECK(latter_core::find_shortcut_route("home_hub:cameras") == nullptr);

  latter_core::PersistedDevicePrefs cam_prefs{};
  cam_prefs.start_screen = static_cast<uint8_t>(latter_core::find_pref_option_index(latter_core::kPrefOptsStartScreen, "Pinned"));
  LATTER_CHECK(latter_core::resolve_start_screen_page(cam_prefs) == latter_core::PageId::PINNED);
  cam_prefs.start_screen = static_cast<uint8_t>(latter_core::find_pref_option_index(latter_core::kPrefOptsStartScreen, "Calendar"));
  LATTER_CHECK(latter_core::resolve_start_screen_page(cam_prefs) == latter_core::PageId::CALENDAR);
  cam_prefs.start_screen = static_cast<uint8_t>(latter_core::find_pref_option_index(latter_core::kPrefOptsStartScreen, "Cameras"));
  LATTER_CHECK(latter_core::resolve_start_screen_page(cam_prefs) == latter_core::PageId::CAMERAS);
  LATTER_CHECK(latter_core::is_cameras_start_screen(cam_prefs));
  cam_prefs.start_screen = static_cast<uint8_t>(latter_core::find_pref_option_index(latter_core::kPrefOptsStartScreen, "Tools"));
  LATTER_CHECK(latter_core::resolve_start_screen_page(cam_prefs) == 8);

  LATTER_CHECK(latter_core::is_sensitive_perimeter_action_impl("home_hub.garage", "turn_on", 100) == true);
  LATTER_CHECK(latter_core::is_sensitive_perimeter_action_impl("home_hub.garage", "turn_on") == true);
  const std::string retired_target = std::string("whole") + "_home.garage";
  LATTER_CHECK(latter_core::is_sensitive_perimeter_action_impl(retired_target, "turn_on", 100) == false);
  LATTER_CHECK(latter_core::is_sensitive_perimeter_action_impl("lock", "open") == true);
  LATTER_CHECK(latter_core::is_sensitive_perimeter_action_impl("cover.garage_door", "open") == true);
  LATTER_CHECK(!latter_core::is_sensitive_perimeter_action_impl("light.porch", "turn_on"));

  latter_core::home_hub_tab() = 2;
  LATTER_CHECK(latter_core::home_hub_tab() == 2);
  latter_core::home_hub_state().thermostat_count = 1;
  latter_core::home_hub_state().thermostats[0].mode = "heat";
  LATTER_CHECK(latter_core::home_hub_state().thermostats[0].mode == "heat");

  LATTER_CHECK(latter_core::is_configured_remote_slot("remote.living_room"));
  LATTER_CHECK(!latter_core::is_configured_remote_slot(""));

  int hh_tabs[latter_core::kMaxHomeHubTabs] = {-1, -1, -1, -1, -1};
  latter_core::AreaContextState uninit_ac{};
  LATTER_CHECK(uninit_ac.epoch == 0U && uninit_ac.home_categories.empty());
  LATTER_CHECK(latter_core::collect_home_hub_tabs(hh_tabs, uninit_ac) == 0);
  uninit_ac.home_categories = latter_core::kDefaultHomeCategories;
  LATTER_CHECK(latter_core::collect_home_hub_tabs(hh_tabs, uninit_ac) == 0);
  uninit_ac.epoch = 1U;
  LATTER_CHECK(latter_core::collect_home_hub_tabs(hh_tabs, uninit_ac) == 5);
  LATTER_CHECK(hh_tabs[0] == 0 && hh_tabs[1] == 1 && hh_tabs[2] == 2 &&
               hh_tabs[3] == 3 && hh_tabs[4] == 4);
  uninit_ac.epoch = 10U;
  uninit_ac.home_categories = "vacuum,thermostat,unknown,vacuum,intercom";
  LATTER_CHECK(latter_core::collect_home_hub_tabs(hh_tabs, uninit_ac) == 3);
  LATTER_CHECK(hh_tabs[0] == latter_core::HomeHubTab::VACUUM &&
               hh_tabs[1] == latter_core::HomeHubTab::THERMOSTAT &&
               hh_tabs[2] == latter_core::HomeHubTab::INTERCOM);
  uninit_ac.home_categories.clear();
  LATTER_CHECK(latter_core::collect_home_hub_tabs(hh_tabs, uninit_ac) == 0);

  const std::string full_manifest = latter_core::build_hardware_manifest_json();
  LATTER_CHECK(full_manifest.find("\"slot_count\":" + std::to_string(LATTER_SLOT_COUNT)) != std::string::npos);
  LATTER_CHECK(full_manifest.find("\"card_slot_count\":3") != std::string::npos);
  const int exp_gcols = latter_core::tile_grid_cols_for(
      LATTER_SCREEN_W, LATTER_SCREEN_H, LATTER_ROUND_DISPLAY != 0,
      LATTER_PANEL_PPI, LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);
  const int exp_grows = latter_core::tile_grid_rows_for(
      LATTER_SCREEN_W, LATTER_SCREEN_H, LATTER_ROUND_DISPLAY != 0,
      LATTER_PANEL_PPI, LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);
  LATTER_CHECK(full_manifest.find("\"grid_cols\":" + std::to_string(exp_gcols)) != std::string::npos);
  LATTER_CHECK(full_manifest.find("\"grid_rows\":" + std::to_string(exp_grows)) != std::string::npos);
  LATTER_CHECK(full_manifest.find("\"has_rtc\":") != std::string::npos);
  LATTER_CHECK(full_manifest.find("\"ppi\":" + std::to_string(LATTER_PANEL_PPI)) != std::string::npos);
  LATTER_CHECK(full_manifest.find("\"corner_r\":" + std::to_string(LATTER_CORNER_R)) != std::string::npos);
  const int exp_art_edge = (LATTER_SCREEN_W < LATTER_SCREEN_H) ? LATTER_SCREEN_W : LATTER_SCREEN_H;
  LATTER_CHECK(full_manifest.find("\"art_edge\":" + std::to_string(exp_art_edge)) != std::string::npos);
  const char *exp_shape = (LATTER_ROUND_DISPLAY != 0)
                              ? "\"shape\":\"circle\""
                              : ((LATTER_SCREEN_W == LATTER_SCREEN_H) ? "\"shape\":\"square\"" : "\"shape\":\"rect\"");
  LATTER_CHECK(full_manifest.find(exp_shape) != std::string::npos);
  LATTER_CHECK(full_manifest.find("\"core_rev\":2026112501") != std::string::npos);
  LATTER_CHECK(full_manifest.find("\"sdk\":") == std::string::npos);
  LATTER_CHECK(full_manifest.find("\"max_payload_bytes\"") == std::string::npos);
  LATTER_CHECK(full_manifest.find("\"beacon\":null") != std::string::npos);

  std::string parsed_cats[8];
  const int parsed_n = latter_core::parse_area_categories_csv("media,light,unknown,media,all", parsed_cats, 8);
  LATTER_CHECK(parsed_n == 3 && parsed_cats[0] == "media" && parsed_cats[1] == "light" && parsed_cats[2] == "all");
  LATTER_CHECK(latter_core::parse_area_categories_csv("", parsed_cats, 8) == 0);
  const int def_n = latter_core::parse_area_categories_csv(latter_core::kDefaultAreaCategories, parsed_cats, 8);
  LATTER_CHECK(def_n == 7 && parsed_cats[0] == "scene" && parsed_cats[6] == "media");

  auto &p = latter_core::persisted_prefs();
  p = latter_core::PersistedDevicePrefs{};
  latter_core::device_prefs_state() = latter_core::DevicePrefsState{};
  latter_core::seed_default_prefs_if_needed();
  const char *expected_ww_default = (LATTER_IS_STATIONARY != 0) ? "Always On" : "Docked Only";
  const char *expected_clock_default = (LATTER_ROUND_DISPLAY != 0) ? "Modern Analog" : "Minimal Digital";
  LATTER_CHECK(std::string_view(latter_core::pref_wake_word_mode_cstr()) == expected_ww_default);
  LATTER_CHECK(std::string_view(latter_core::pref_clock_face_cstr()) == expected_clock_default);
  LATTER_CHECK(std::string_view(latter_core::pref_start_screen_cstr()) == "Home");
  LATTER_CHECK(std::string_view(latter_core::pref_screensaver_style_cstr()) == "Ambient Clock");
  LATTER_CHECK(std::string_view(latter_core::pref_album_art_style_cstr()) == "Full Screen");
  LATTER_CHECK(std::string_view(latter_core::pref_album_sort_order_cstr()) == "Alphabetical");
  LATTER_CHECK(std::string_view(latter_core::pref_artist_album_sort_order_cstr()) == "Oldest on Top");
  LATTER_CHECK(std::string_view(latter_core::pref_podcast_sort_order_cstr()) == "Newest on Top");
  LATTER_CHECK(p.prefs_seq == 0U && p.dirty_mask == 0ULL);

  // Clean boot (prefs_seq == 0 && dirty_mask empty) emits empty dirty_prefs (never dumps unchanged defaults)
  const std::string seed_dirty = latter_core::build_hello_dirty_prefs(p);
  LATTER_CHECK(seed_dirty.empty());

  latter_core::reset_all_v3_rev_guards();
  std::vector<std::string> dp_tok(static_cast<size_t>(latter_core::DevicePrefsField::COUNT), "");
  dp_tok[static_cast<size_t>(latter_core::DevicePrefsField::PREFS_SEQ)] = "7";
  dp_tok[static_cast<size_t>(latter_core::DevicePrefsField::WAKE_WORD_MODE)] = "Disabled";
  dp_tok[static_cast<size_t>(latter_core::DevicePrefsField::ALERT_VOLUME)] = "65";
  dp_tok[static_cast<size_t>(latter_core::DevicePrefsField::COLOR_PROFILE)] = "Custom";
  dp_tok[static_cast<size_t>(latter_core::DevicePrefsField::THEME_COLOR_HEX)] = "#2288FF";
  dp_tok[static_cast<size_t>(latter_core::DevicePrefsField::WEATHER_SIM_CONDITION)] = "Thunderstorm (Day)";

  p.dirty_mask = (1ULL << static_cast<uint8_t>(latter_core::PrefKey::ALERT_VOLUME));
  uint32_t eff = 0;
  LATTER_CHECK(latter_core::parse_v3_device_prefs_payload(build_v3_packet(300, 1, dp_tok), eff));
  LATTER_CHECK(p.prefs_seq == 7U);
  LATTER_CHECK(p.dirty_mask == 0ULL);
  LATTER_CHECK(p.alert_volume == 65U);
  LATTER_CHECK(std::string_view(latter_core::pref_color_profile_cstr()) == "Custom");
  LATTER_CHECK(p.custom_theme_rgb == 0x2288FFU);
  LATTER_CHECK(std::string_view(latter_core::pref_weather_sim_cstr()) == "Thunderstorm (Day)");
  LATTER_CHECK((eff & latter_core::kPrefEffectThemePreset) != 0U);
  LATTER_CHECK((eff & latter_core::kPrefEffectWakeWordSync) != 0U);
  LATTER_CHECK((eff & latter_core::kPrefEffectRedraw) != 0U);

  MockRpcOutbox outbox{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_core::link_state().ha_connected = true;
  uint32_t local_eff = 0;
  LATTER_CHECK(latter_core::record_local_pref_edit(
      latter_core::PrefKey::WAKE_WORD_MODE, "Always On", 1000U, local_eff, &outbox));
  LATTER_CHECK(std::string_view(latter_core::pref_wake_word_mode_cstr()) == "Always On");
  LATTER_CHECK(!outbox.published.empty());
  LATTER_CHECK(outbox.published.back().find("set_device_prefs") != std::string::npos);
  LATTER_CHECK(outbox.published.back().find("wake_word_mode=Always On") != std::string::npos);

  p.dirty_mask = 0ULL;
  outbox.published.clear();
  latter_core::link_state().ha_connected = false;
  LATTER_CHECK(latter_core::record_local_pref_edit(
      latter_core::PrefKey::TIME_FORMAT, "24-Hour", 2000U, local_eff, &outbox));
  LATTER_CHECK(outbox.published.empty());
  LATTER_CHECK(p.is_24h_format);
  const std::string offline_dirty = latter_core::build_hello_dirty_prefs(p);
  LATTER_CHECK(offline_dirty == "time_format=24-Hour");

  latter_core::timer_alarm_sync_tracker() = latter_core::TimerAlarmSyncTracker{};
  auto &rs = latter_core::runtime_state();
  rs.timers.running = false;
  rs.timers.paused = false;
  rs.timers.ringing = false;
  rs.alarms.ringing = false;
  latter_core::link_state().ha_connected = true;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  outbox.published.clear();

  LATTER_CHECK(!latter_core::check_and_emit_timer_alarm_state(5000U, false, &outbox));
  rs.timers.running = true;
  rs.timers.remaining_sec = 120;
  LATTER_CHECK(latter_core::check_and_emit_timer_alarm_state(5250U, false, &outbox));
  LATTER_CHECK(outbox.published.size() == 1U && outbox.published.back().find("timer_alarm_state") != std::string::npos);

  rs.timers.remaining_sec = 119;
  LATTER_CHECK(!latter_core::check_and_emit_timer_alarm_state(6250U, false, &outbox));

  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  rs.timers.remaining_sec = 179;
  LATTER_CHECK(latter_core::check_and_emit_timer_alarm_state(7250U, false, &outbox));
  LATTER_CHECK(outbox.published.size() == 2U);

  p = latter_core::PersistedDevicePrefs{};
  LATTER_CHECK(p.home_screen_enabled);
  LATTER_CHECK(std::string_view(latter_core::pref_start_screen_cstr(p)) == "Home");
  LATTER_CHECK(std::string_view(latter_core::pref_return_to_home_timeout_cstr(p)) == "2 min");
  LATTER_CHECK(latter_core::return_to_home_timeout_ms(p) == 120000U);

  latter_core::PrefMask nav_chg{};
  uint32_t nav_eff = 0;
  LATTER_CHECK(latter_core::set_pref_by_key_str(
      p, latter_core::PrefKey::HOME_SCREEN_ENABLED, "0", nav_chg, nav_eff));
  LATTER_CHECK(!p.home_screen_enabled);
  LATTER_CHECK(std::string_view(latter_core::pref_start_screen_cstr(p)) == "Time App");
  LATTER_CHECK(latter_core::resolve_anchor_dock_app(p) == 10);
  LATTER_CHECK((nav_eff & latter_core::kPrefEffectNavPolicy) != 0U);
  LATTER_CHECK(latter_core::set_pref_by_key_str(
      p, latter_core::PrefKey::START_SCREEN, "Clock Face", nav_chg, nav_eff));
  LATTER_CHECK(latter_core::resolve_anchor_dock_app(p) == 0);

  LATTER_CHECK(latter_core::set_pref_by_key_str(
      p, latter_core::PrefKey::START_SCREEN, "Area Controls", nav_chg, nav_eff));
  LATTER_CHECK(latter_core::resolve_anchor_dock_app(p) == 6);
  LATTER_CHECK(!latter_core::should_return_to_home_on_idle(rs, p, false));

  LATTER_CHECK(latter_core::set_pref_by_key_str(
      p, latter_core::PrefKey::HOME_SCREEN_ENABLED, "1", nav_chg, nav_eff));
  rs = latter_core::DeviceRuntimeState{};
  rs.nav.gui_page = 6;
  LATTER_CHECK(latter_core::should_return_to_home_on_idle(rs, p, false));
  rs.nav.gui_page = 1;
  rs.nav.home_subpage = 0;
  LATTER_CHECK(!latter_core::should_return_to_home_on_idle(rs, p, false));
  rs.nav.gui_page = 2;
  rs.music.view = latter_core::MusicView::NOW_PLAYING;
  LATTER_CHECK(!latter_core::should_return_to_home_on_idle(rs, p, true));
  LATTER_CHECK(latter_core::should_return_to_home_on_idle(rs, p, false));
  rs.music.view = latter_core::MusicView::SPEAKERS;
  LATTER_CHECK(latter_core::should_return_to_home_on_idle(rs, p, true));
  rs.nav.gui_page = 10;
  rs.nav.time_subpage = 1;
  rs.timers.running = true;
  LATTER_CHECK(!latter_core::should_return_to_home_on_idle(rs, p, false));
  rs.timers.running = false;
  LATTER_CHECK(latter_core::should_return_to_home_on_idle(rs, p, false));
  rs.nav.time_subpage = 3;
  rs.stopwatch.running = true;
  LATTER_CHECK(!latter_core::should_return_to_home_on_idle(rs, p, false));
  rs.stopwatch.running = false;
  rs.nav.gui_page = 8;
  rs.nav.tools_subpage = 5;
  LATTER_CHECK(!latter_core::should_return_to_home_on_idle(rs, p, false));
  // Remote Control app stays active and never auto-returns to Home (D-079)
  rs.nav.gui_page = latter_core::PageId::REMOTE;
  rs.nav.remote_subpage = 0;
  LATTER_CHECK(!latter_core::should_return_to_home_on_idle(rs, p, false, false, false));
  LATTER_CHECK(!latter_core::should_return_to_home_on_idle(rs, p, false, false, true));
  rs.nav.remote_subpage = 1;
  LATTER_CHECK(!latter_core::should_return_to_home_on_idle(rs, p, false, false, false));
  LATTER_CHECK(!latter_core::should_return_to_home_on_idle(rs, p, false, false, true));
  rs.nav.remote_subpage = 0;
  LATTER_CHECK(latter_core::set_pref_by_key_str(
      p, latter_core::PrefKey::RETURN_TO_HOME_TIMEOUT, "Never", nav_chg, nav_eff));
  rs.nav.gui_page = 6;
  LATTER_CHECK(!latter_core::should_return_to_home_on_idle(rs, p, false));
}

void test_universal_shortcuts_and_pinned_app() {
  using namespace latter_ui;
  using namespace latter_core;
  reset_runtime_state();

  ShortcutItem dock_eff[kMaxEffectiveDockItems]{};
  int cnt = collect_effective_dock_items(dock_eff, kMaxEffectiveDockItems);
  LATTER_CHECK(cnt == 6);
  LATTER_CHECK(dock_eff[0].route_id == RouteId::APP_MUSIC);
  LATTER_CHECK(dock_eff[cnt - 1].route_id == RouteId::SETTINGS_ROOT);

  auto build_sc_chunk = [](uint32_t ep, uint32_t rv, int c_idx, int c_cnt,
                           const std::string &dock_blob, int p_tot, const std::string &pin_blob) {
    std::vector<std::string> tok(static_cast<size_t>(ShortcutsField::COUNT), "");
    tok[static_cast<size_t>(ShortcutsField::CHUNK_INDEX)] = std::to_string(c_idx);
    tok[static_cast<size_t>(ShortcutsField::CHUNK_COUNT)] = std::to_string(c_cnt);
    tok[static_cast<size_t>(ShortcutsField::DOCK_ITEMS)] = dock_blob;
    tok[static_cast<size_t>(ShortcutsField::PINNED_TOTAL)] = std::to_string(p_tot);
    tok[static_cast<size_t>(ShortcutsField::PINNED_ITEMS)] = pin_blob;
    return build_v3_packet(ep, rv, tok);
  };

  auto make_pin_slice = [](int start_i, int count_i) {
    std::string out;
    for (int i = 0; i < count_i; ++i) {
      if (i > 0) out.push_back(kProtocolRecordSep);
      const int idx = start_i + i;
      if (idx == 0) {
        out.append("route|weather:moon||Moon Phase||0|-1");
      } else if (idx == 1) {
        out.append("entity|light.nursery|toggle|Nursery Light|mdi:ceiling-light|0|1");
      } else {
        out.append("entity|script.helper_" + std::to_string(idx) + "|turn_on|Helper " +
                   std::to_string(idx) + "|mdi:bell-ring|1|-1");
      }
    }
    return out;
  };

  const std::string dock_blob =
      std::string("route|app:pinned||Pinned||0|-1") + kProtocolRecordSep +
      "entity|script.call_jordan|turn_on|Call Jordan|mdi:bell-ring|1|-1" + kProtocolRecordSep +
      "route|weather:moon||Moon Phase||0|-1";

  LATTER_CHECK(!parse_v3_shortcuts_payload(build_sc_chunk(7, 1, 0, 3, dock_blob, 60, make_pin_slice(0, 24))));
  LATTER_CHECK(shortcuts_state().pinned_items.empty());
  LATTER_CHECK(!parse_v3_shortcuts_payload(build_sc_chunk(7, 1, 1, 3, "", 60, make_pin_slice(24, 24))));
  LATTER_CHECK(shortcuts_state().pinned_items.empty());
  LATTER_CHECK(parse_v3_shortcuts_payload(build_sc_chunk(7, 1, 2, 3, "", 60, make_pin_slice(48, 12))));
  LATTER_CHECK(shortcuts_state().epoch == 7U && shortcuts_state().rev == 1U);
  const int per_pg = tile_grid_cols_for(LATTER_SCREEN_W, LATTER_SCREEN_H, LATTER_ROUND_DISPLAY != 0) *
                     tile_grid_rows_for(LATTER_SCREEN_W, LATTER_SCREEN_H, LATTER_ROUND_DISPLAY != 0);
  const int expected_pages = (60 + per_pg - 1) / per_pg;
  LATTER_CHECK(shortcuts_state().pinned_items.size() == 60U);
  LATTER_CHECK(pinned_total_pages() == expected_pages);
  LATTER_CHECK(!parse_v3_shortcuts_payload(build_sc_chunk(7, 1, 0, 1, dock_blob, 0, "")));

  cnt = collect_effective_dock_items(dock_eff, kMaxEffectiveDockItems);
  LATTER_CHECK(cnt == 4);
  LATTER_CHECK(dock_eff[0].route_id == RouteId::APP_PINNED);
  LATTER_CHECK(dock_eff[1].kind == ShortcutKind::ENTITY && dock_eff[1].confirm);
  LATTER_CHECK(dock_eff[2].route_id == RouteId::WEATHER_MOON);
  LATTER_CHECK(dock_eff[3].route_id == RouteId::SETTINGS_ROOT);

  auto &rt = runtime_state();
  rt.nav.gui_page = PageId::PINNED;
  rt.nav.pinned_menu_page = 25;
  V3ReduceContext rctx{};
  rctx.now_ms = 9000U;
  const auto eff = v3_domain_reducer().reduce("shortcuts", rctx);
  LATTER_CHECK(rt.nav.pinned_menu_page == expected_pages - 1);
  LATTER_CHECK(eff.mark_dirty_full && eff.schedule_disp_update);

  std::string title, l1, l2, verb;
  bool danger = false;
  build_shortcut_confirm_copy(shortcuts_state().pinned_items[1], title, l1, l2, verb, danger);
  LATTER_CHECK(verb == "Turn Off" && !danger);
  ShortcutItem lock_it{ShortcutKind::ENTITY, RouteId::NONE, "lock.front_door", "unlock", "Front Door", "mdi:lock-open", true, 0};
  build_shortcut_confirm_copy(lock_it, title, l1, l2, verb, danger);
  LATTER_CHECK(verb == "Unlock" && danger);

  struct FakeOutbox {
    std::vector<std::string> frames;
    void publish_state(const std::string &s) { frames.push_back(s); }
  } outbox;
  LATTER_CHECK(latter_rpc::set_pin_session_key(
      "0a0b0c0d", "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_core::emit_shortcut_action("script.call_jordan", "turn_on", "123456789012", &outbox);
  LATTER_CHECK(!outbox.frames.empty());
  std::string sc_joined;
  for (const auto &f : outbox.frames) sc_joined += f;
  LATTER_CHECK(sc_joined.find("\"s\":\"shortcut_action\"") != std::string::npos);
  LATTER_CHECK(sc_joined.find("\"entity_id\":\"script.call_jordan\"") != std::string::npos);
  LATTER_CHECK(sc_joined.find("\"code\":\"v1:0a0b0c0d:") != std::string::npos);
  LATTER_CHECK(sc_joined.find("123456789012") == std::string::npos);
  latter_rpc::clear_pin_session_key();
}

void test_tools_games_launcher_prefs_and_stopwatch_commands() {
  using namespace latter_ui;
  using namespace latter_core;
  reset_runtime_state();

  static_assert(kPersistedPrefsVersion == 1U, "PersistedDevicePrefs clean v3.0.0 NVS version == 1");
  static_assert(static_cast<size_t>(DevicePrefsField::TOOLS_ITEMS) == 52U);
  static_assert(static_cast<size_t>(DevicePrefsField::GAMES_ITEMS) == 53U);
  static_assert(static_cast<size_t>(DevicePrefsField::DO_NOT_DISTURB) == 70U);
  static_assert(static_cast<size_t>(DevicePrefsField::MUSIC_AUTO_OPEN) == 71U);
  static_assert(static_cast<size_t>(DevicePrefsField::GLASS_STYLE) == 72U);
  static_assert(static_cast<size_t>(DevicePrefsField::HOME_SHOW_BATTERY) == 77U);
  static_assert(static_cast<size_t>(DevicePrefsField::COUNT) == 78U);
  static_assert(static_cast<uint8_t>(PrefKey::TOOLS_ITEMS) == 46U);
  static_assert(static_cast<uint8_t>(PrefKey::GAMES_ITEMS) == 47U);
  static_assert(static_cast<uint8_t>(PrefKey::DO_NOT_DISTURB) == 64U);
  static_assert(static_cast<uint8_t>(PrefKey::MUSIC_AUTO_OPEN) == 65U);
  static_assert(static_cast<uint8_t>(PrefKey::GLASS_STYLE) == 66U);
  static_assert(static_cast<uint8_t>(PrefKey::HOME_SHOW_BATTERY) == 71U);
  static_assert(static_cast<uint8_t>(PrefKey::COUNT) == 72U);

  LATTER_CHECK(std::string_view(kDefaultToolsItems) ==
               "stopwatch,level,flashlight,calculator,dice,coin_flip");
  LATTER_CHECK(std::string_view(kDefaultGamesItems) ==
               "blocks,snake,drop_four,marbles,archery,bricks,chomp");
  LATTER_CHECK(std::string_view(device_prefs().tools_items) == kDefaultToolsItems);
  LATTER_CHECK(std::string_view(device_prefs().games_items) == kDefaultGamesItems);
  LATTER_CHECK(persisted_prefs().home_date_tap_action == 1U);
  LATTER_CHECK(std::string_view(pref_home_date_tap_cstr()) == "Overview");

  LATTER_CHECK(normalize_tools_items_csv(" coin_flip , flashlight , calendar , dice , coin_flip , bogus ") ==
               "coin_flip,flashlight,dice");
  LATTER_CHECK(normalize_tools_items_csv("") == kDefaultToolsItems);
  LATTER_CHECK(normalize_tools_items_csv("calendar,unknown") == kDefaultToolsItems);

  LATTER_CHECK(normalize_games_items_csv(" drop_four , blocks , snake , chomp , drop_four , 2048 , memory ") ==
               "drop_four,blocks,snake,chomp");
  LATTER_CHECK(normalize_games_items_csv("") == kDefaultGamesItems);
  LATTER_CHECK(normalize_games_items_csv("2048,memory,chess") == kDefaultGamesItems);

  // ShortcutRouteCap::NO_IMU vs IMU route capability gating
  const ShortcutRouteSpec *r_blocks = find_shortcut_route("games:blocks");
  const ShortcutRouteSpec *r_snake = find_shortcut_route("games:snake");
  const ShortcutRouteSpec *r_drop4 = find_shortcut_route("games:drop_four");
  const ShortcutRouteSpec *r_archery = find_shortcut_route("games:archery");
  const ShortcutRouteSpec *r_marbles = find_shortcut_route("games:marbles");
  LATTER_CHECK(r_blocks != nullptr && r_blocks->cap == ShortcutRouteCap::NO_IMU);
  LATTER_CHECK(r_snake != nullptr && r_snake->cap == ShortcutRouteCap::NO_IMU);
  LATTER_CHECK(r_drop4 != nullptr && r_drop4->cap == ShortcutRouteCap::NO_IMU);
  LATTER_CHECK(r_archery != nullptr && r_archery->cap == ShortcutRouteCap::IMU);
  LATTER_CHECK(r_marbles != nullptr && r_marbles->cap == ShortcutRouteCap::NONE);
  LATTER_CHECK(find_shortcut_route("games:2048") == nullptr);
  LATTER_CHECK(find_shortcut_route("games:memory") == nullptr);
  LATTER_CHECK(is_route_supported_on_board(RouteId::GAMES_BLOCKS) == (LATTER_HAS_IMU == 0));
  LATTER_CHECK(is_route_supported_on_board(RouteId::GAMES_SNAKE) == (LATTER_HAS_IMU == 0));
  LATTER_CHECK(is_route_supported_on_board(RouteId::GAMES_DROP_FOUR) == (LATTER_HAS_IMU == 0));
  LATTER_CHECK(is_route_supported_on_board(RouteId::GAMES_ARCHERY) == (LATTER_HAS_IMU != 0));
  LATTER_CHECK(is_route_supported_on_board(RouteId::GAMES_MARBLES));

  {
    PersistedDevicePrefs cold{};
    const std::string seed = build_hello_dirty_prefs(cold);
    LATTER_CHECK(seed.find("tools_items=") == std::string::npos);
    LATTER_CHECK(seed.find("games_items=") == std::string::npos);
  }

  reset_all_v3_rev_guards();
  auto &rt = runtime_state();
  rt.nav.gui_page = PageId::GAMES;
  rt.nav.game_carousel_idx = 5;
  rt.nav.tools_menu_page = 1;

  std::vector<std::string> dp_tok(static_cast<size_t>(DevicePrefsField::COUNT), "");
  dp_tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "21";
  dp_tok[static_cast<size_t>(DevicePrefsField::TOOLS_ITEMS)] = "dice,flashlight";
  dp_tok[static_cast<size_t>(DevicePrefsField::GAMES_ITEMS)] = "blocks,snake,marbles,chomp";
  uint32_t eff = 0U;
  LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(90, 1, dp_tok), eff, 1200U));
  LATTER_CHECK((eff & kPrefEffectNavPolicy) != 0U && (eff & kPrefEffectRedraw) != 0U);
  LATTER_CHECK(std::string_view(persisted_prefs().tools_items) == "dice,flashlight");
  LATTER_CHECK(std::string_view(persisted_prefs().games_items) == "blocks,snake,marbles,chomp");
  LATTER_CHECK(device_prefs_state().nvs_save_pending);

  V3ReduceContext rctx{};
  rctx.now_ms = 1200U;
  const auto red_eff = v3_domain_reducer().reduce("device_prefs", rctx);
  LATTER_CHECK(red_eff.apply_device_prefs_effects);
  LATTER_CHECK(rt.nav.game_carousel_idx == (LATTER_HAS_IMU ? 1 : 2));
  LATTER_CHECK(rt.nav.tools_menu_page == 0);

  // High score fields + append-only NVS prefix compatibility (high_score_drop_four at struct tail)
  persisted_prefs().high_score_blocks = 12400;
  persisted_prefs().high_score_snake = 320;
  persisted_prefs().high_score_drop_four = 740;
  LATTER_CHECK(get_game_high_score(5) == 12400);
  LATTER_CHECK(get_game_high_score(6) == 320);
  LATTER_CHECK(get_game_high_score(7) == 740);
  persisted_prefs().high_score_drop_four = 890;
  LATTER_CHECK(get_game_high_score(7) == 890);

  // Scoped kPrefEffectWeatherSky fires only on weather sky / sim preferences, not unrelated toggles.
  {
    PersistedDevicePrefs wx_p{};
    PrefMask wx_chg{};
    uint32_t wx_eff = 0U;
    LATTER_CHECK(set_pref_by_key_str(wx_p, PrefKey::HOME_SHOW_TIME, "0", wx_chg, wx_eff));
    LATTER_CHECK((wx_eff & kPrefEffectRedraw) != 0U && (wx_eff & kPrefEffectWeatherSky) == 0U);
    wx_eff = 0U;
    LATTER_CHECK(set_pref_by_key_str(wx_p, PrefKey::WEATHER_BACKGROUND, "0", wx_chg, wx_eff));
    LATTER_CHECK((wx_eff & kPrefEffectWeatherSky) != 0U && (wx_eff & kPrefEffectRedraw) != 0U);
    wx_eff = 0U;
    LATTER_CHECK(set_pref_by_key_str(wx_p, PrefKey::WEATHER_APP_BACKGROUND, "0", wx_chg, wx_eff));
    LATTER_CHECK((wx_eff & kPrefEffectWeatherSky) != 0U && (wx_eff & kPrefEffectRedraw) != 0U);
  }

  LATTER_CHECK(save_persisted_prefs());
  alignas(PersistedDevicePrefs) uint8_t raw_nvs[sizeof(PersistedDevicePrefs)]{};
  std::memcpy(raw_nvs, &persisted_prefs(), sizeof(PersistedDevicePrefs));
  PersistedDevicePrefs restored{};
  std::memcpy(&restored, raw_nvs, sizeof(PersistedDevicePrefs));
  LATTER_CHECK(restored.magic == kPersistedPrefsMagic && restored.version == kPersistedPrefsVersion);
  clamp_prefs_to_capabilities(restored);
  LATTER_CHECK(std::string_view(restored.tools_items) == "dice,flashlight");
  LATTER_CHECK(std::string_view(restored.games_items) == "blocks,snake,marbles,chomp");
  LATTER_CHECK(restored.high_score_blocks == 12400);
  LATTER_CHECK(restored.high_score_snake == 320);
  LATTER_CHECK(restored.high_score_drop_four == 890);

  // Pre-v3.0 truncated prefix (before kPersistedPrefsMinSize) is cleanly rejected; v3.0 GA floor succeeds
  PersistedDevicePrefs prefix_dst{};
  prefix_dst.high_score_drop_four = 999;
  const size_t old_prefix_len = offsetof(PersistedDevicePrefs, high_score_drop_four);
  LATTER_CHECK(!load_persisted_prefs_from_buffer(raw_nvs, old_prefix_len, prefix_dst));
  LATTER_CHECK(load_persisted_prefs_from_buffer(raw_nvs, kPersistedPrefsMinSize, prefix_dst));
  LATTER_CHECK(prefix_dst.high_score_blocks == 12400);
  LATTER_CHECK(prefix_dst.high_score_snake == 320);
  LATTER_CHECK(prefix_dst.high_score_drop_four == 890);

  PersistedDevicePrefs empty_nvs{};
  empty_nvs.tools_items[0] = '\0';
  empty_nvs.games_items[0] = '\0';
  clamp_prefs_to_capabilities(empty_nvs);
  LATTER_CHECK(std::string_view(empty_nvs.tools_items) == kDefaultToolsItems);
  LATTER_CHECK(std::string_view(empty_nvs.games_items) == kDefaultGamesItems);

  dp_tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "22";
  dp_tok[static_cast<size_t>(DevicePrefsField::TOOLS_ITEMS)] = "";
  dp_tok[static_cast<size_t>(DevicePrefsField::GAMES_ITEMS)] = "";
  eff = 0U;
  LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(90, 2, dp_tok), eff, 1500U));
  LATTER_CHECK(std::string_view(device_prefs().tools_items) == kDefaultToolsItems);
  LATTER_CHECK(std::string_view(device_prefs().games_items) == kDefaultGamesItems);

  {
    ToolsLauncherItemSpec items[kMaxToolsLauncherItems]{};
    int n = collect_tools_launcher_items_for("level,stopwatch,coin_flip", true, true, items);
    LATTER_CHECK(n == 3);
    LATTER_CHECK(items[0].id == ToolsSubpage::LEVEL && std::string_view(items[0].label) == "Level");
    LATTER_CHECK(items[1].id == ToolsSubpage::STOPWATCH && std::string_view(items[1].label) == "Stopwatch");
    LATTER_CHECK(items[2].id == ToolsSubpage::COIN_FLIP && std::string_view(items[2].label) == "Coin Flip");

    n = collect_tools_launcher_items_for("stopwatch,level", false, false, items, kMaxToolsLauncherItems);
    LATTER_CHECK(n == 0 && tools_launcher_page_count(n) == 1);

    n = collect_tools_launcher_items_for(kDefaultToolsItems, true, true, items);
    LATTER_CHECK(n == 6 && tools_launcher_page_count(n) == 1);
    n = collect_tools_launcher_items_for(kDefaultToolsItems, false, false, items);
    LATTER_CHECK(n == 4 && tools_launcher_page_count(n) == 1);

    int g_ids[kMaxGamesLauncherItems]{};
    const int gc_no_imu = collect_enabled_games_for(kDefaultGamesItems, false, g_ids);
    LATTER_CHECK(gc_no_imu == 4 && g_ids[0] == 5 && g_ids[1] == 6 && g_ids[2] == 7 && g_ids[3] == 1);
    const int gc_imu = collect_enabled_games_for(kDefaultGamesItems, true, g_ids);
    LATTER_CHECK(gc_imu == 4 && g_ids[0] == 1 && g_ids[1] == 2 && g_ids[2] == 3 && g_ids[3] == 4);
    const int gc_custom = collect_enabled_games_for("drop_four,bricks,chomp,marbles", true, g_ids);
    LATTER_CHECK(gc_custom == 3 && g_ids[0] == 3 && g_ids[1] == 4 && g_ids[2] == 1);
    LATTER_CHECK(collect_enabled_games_for("unknown,2048,memory", false, g_ids) == 0);
  }

  reset_runtime_state();
  MockRpcOutbox sw_box{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  link_state().ha_connected = true;
  auto &rs = runtime_state();

  LATTER_CHECK(!latter_core::check_and_emit_timer_alarm_state(1000U, false, &sw_box));
  LATTER_CHECK(sw_box.published.empty());

  rs.stopwatch.running = true;
  rs.stopwatch.start_time_ms = 1000U;
  rs.stopwatch.elapsed_ms = 0U;
  LATTER_CHECK(latter_core::check_and_emit_timer_alarm_state(1000U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 1U);
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_running\":\"1\"") != std::string::npos);

  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(!latter_core::check_and_emit_timer_alarm_state(4000U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 1U);

  rs.timers.running = true;
  rs.timers.remaining_sec = 60;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_core::check_and_emit_timer_alarm_state(5000U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 2U);

  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  rs.timers.remaining_sec = 58;
  rs.stopwatch.start_time_ms = 1000U;
  LATTER_CHECK(!latter_core::check_and_emit_timer_alarm_state(7000U, false, &sw_box));
  rs.stopwatch.start_time_ms = 0U;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_core::check_and_emit_timer_alarm_state(25000U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 3U);
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_elapsed_sec\":\"25\"") != std::string::npos);

  rs.timers.running = false;
  rs.stopwatch.running = false;
  rs.stopwatch.elapsed_ms = 27400U;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_core::check_and_emit_timer_alarm_state(27400U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 4U);

  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_core::check_and_emit_timer_alarm_state(28000U, /*force_if_active=*/true, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 5U);

  rs.stopwatch.elapsed_ms = 42000U;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_core::check_and_emit_timer_alarm_state(28500U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 6U);

  dirty_band() = DirtyBandTracker{};
  dirty_band().begin_frame(PageId::TIME, 3, 0U, false);
  mark_dirty_band(10, 30);
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  sw_box.published.clear();
  const auto rst_sw_eff = latter_core::handle_device_cmd_reset_stopwatch(29000U, &sw_box);
  LATTER_CHECK(rst_sw_eff.apply_power && !rst_sw_eff.stop_sound);
  LATTER_CHECK(!rs.stopwatch.running && rs.stopwatch.elapsed_ms == 0U && rs.stopwatch.lap_count == 0U);
  dirty_band().begin_frame(PageId::TIME, 3, 0U, false);
  LATTER_CHECK(dirty_band().is_full());
  LATTER_CHECK(!sw_box.published.empty());

  timer_slots()[0].duration_sec = 300;
  timer_slots()[0].remaining_sec = 95;
  timer_slots()[0].running = true;
  timer_slots()[0].target_ms = 125000U;
  timer_slots()[1].duration_sec = 600;
  timer_slots()[1].remaining_sec = 210;
  timer_slots()[1].paused = true;
  rs.timers.ringing = true;
  rs.timers.va_timer_name = "Tea";
  rs.nav.home_subpage = 2;
  sync_multi_timer_state();
  mark_dirty_band(20, 40);
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  sw_box.published.clear();

  const auto can_tmr_eff = latter_core::handle_device_cmd_cancel_timer(30000U, &sw_box);
  LATTER_CHECK(can_tmr_eff.stop_sound && can_tmr_eff.sync_brightness && can_tmr_eff.apply_power);
  LATTER_CHECK(!rs.timers.running && !rs.timers.paused && !rs.timers.ringing);
  LATTER_CHECK(timer_slots()[0].remaining_sec == 300 && !timer_slots()[0].running);
  LATTER_CHECK(timer_slots()[1].remaining_sec == 600 && !timer_slots()[1].paused);
  LATTER_CHECK(rs.timers.va_timer_name.empty() && rs.nav.home_subpage == 0);
  dirty_band().begin_frame(PageId::TIME, 1, 0U, false);
  LATTER_CHECK(dirty_band().is_full());

  auto &prefs = persisted_prefs();
  prefs.alarms[1].hour = 6;
  prefs.alarms[1].minute = 45;
  prefs.alarms[1].enabled = true;
  prefs.alarms[1].recurrence = 3;
  rs.alarms.ringing = true;
  rs.alarms.ringing_idx = 2;
  rs.alarms.snooze_until = 99999U;
  rs.timers.ringing = true;
  mark_dirty_band(10, 40);
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  sw_box.published.clear();

  const auto dis_eff = latter_core::handle_device_cmd_dismiss_ringing(35000U, &sw_box);
  LATTER_CHECK(dis_eff.stop_sound && dis_eff.sync_brightness && dis_eff.apply_power);
  LATTER_CHECK(!rs.alarms.ringing && !rs.timers.ringing && rs.alarms.snooze_until == 0U);
  LATTER_CHECK(!prefs.alarms[1].enabled);
  dirty_band().begin_frame(PageId::TIME, 2, 0U, false);
  LATTER_CHECK(dirty_band().is_full());

  uint32_t pump_ms = 35100U;
  while (latter_rpc::has_pending_frames()) {
    latter_rpc::step_outbox_loop(pump_ms);
    pump_ms += 25U;
  }
  std::string all_frames;
  for (const auto &fr : sw_box.published) all_frames += fr;
  LATTER_CHECK(all_frames.find("set_device_prefs") != std::string::npos);
  LATTER_CHECK(all_frames.find("timer_alarm_state") != std::string::npos);
  LATTER_CHECK(all_frames.find("\"ringing\":\"0\"") != std::string::npos);

  const std::string sys_yaml = read_repo_files({
      "../../modules/system_core.yaml",
      "../../modules/system_ui_sync.yaml",
      "../../components/latter_core/device_sync_controllers.cpp",
  });
  const std::string tools_cpp = read_repo_file("../../components/latter_ui/page_tools.cpp");
  const std::string games_cpp = read_repo_file("../../components/latter_ui/page_games.cpp");
  const std::string ui_doc = read_repo_file("../../docs/device-ui-design.md");
  LATTER_CHECK(sys_yaml.find("sv_cmd == \"dismiss_ringing\"") != std::string::npos);
  LATTER_CHECK(sys_yaml.find("sv_cmd == \"cancel_timer\"") != std::string::npos);
  LATTER_CHECK(sys_yaml.find("sv_cmd == \"reset_stopwatch\"") != std::string::npos);
  LATTER_CHECK(tools_cpp.find("collect_tools_launcher_items(") != std::string::npos);
  LATTER_CHECK(games_cpp.find("collect_enabled_games(") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-057:") != std::string::npos);
}

void test_bedtime_mode_d063() {
  using namespace latter_ui;
  using namespace latter_core;
  reset_runtime_state();

  static_assert(is_hour_in_bedtime_window(22, 22U, 7U));
  static_assert(is_hour_in_bedtime_window(2, 22U, 7U));
  static_assert(!is_hour_in_bedtime_window(7, 22U, 7U));
  static_assert(!is_hour_in_bedtime_window(21, 22U, 7U));
  static_assert(is_hour_in_bedtime_window(1, 1U, 6U));
  static_assert(!is_hour_in_bedtime_window(0, 1U, 6U));
  static_assert(!is_hour_in_bedtime_window(6, 1U, 6U));
  static_assert(!is_hour_in_bedtime_window(22, 22U, 22U));

  BedtimeScheduleTracker tr{};
  LATTER_CHECK(tr.evaluate_hour(false, 22U, 7U, 23) == -1);
  LATTER_CHECK(tr.evaluate_hour(true, 22U, 7U, 23) == 1);
  LATTER_CHECK(tr.evaluate_hour(true, 22U, 7U, 23) == -1);
  LATTER_CHECK(tr.evaluate_hour(true, 22U, 7U, 0) == -1);
  LATTER_CHECK(tr.evaluate_hour(true, 22U, 7U, 6) == -1);
  LATTER_CHECK(tr.evaluate_hour(true, 22U, 7U, 7) == 0);
  LATTER_CHECK(tr.evaluate_hour(true, 22U, 7U, 12) == -1);
  LATTER_CHECK(tr.evaluate_hour(true, 22U, 7U, 22) == 1);

  LATTER_CHECK(resolve_idle_screen_timeout_ms(true, true, 2U, "Never", "30s", false) == 0U);
  LATTER_CHECK(resolve_idle_screen_timeout_ms(true, true, 2U, "Never", "30s", true) == kBedtimeIdleTimeoutMs);

  DisplayLifecycleController dlc{};
  int nav_ps = 0;
  uint32_t sleep_ts = 1000U;
  uint32_t sb_ts = 1000U;
  bool greet = false;
  DisplaySupervisorInputs d_in{};
  d_in.wake_greeting_enabled = true;
  d_in.bedtime_active = true;
  d_in.panel_brightness_on = true;
  (void)dlc.evaluate_1s(2000000U, d_in, nav_ps, sleep_ts, sb_ts, greet);
  LATTER_CHECK(!greet);

  static_assert(should_suppress_alert_in_bedtime(true, AlertPriority::Normal));
  static_assert(should_suppress_alert_in_bedtime(true, parse_alert_priority("warning")));
  static_assert(!should_suppress_alert_in_bedtime(true, AlertPriority::Critical));
  static_assert(!should_suppress_alert_in_bedtime(false, AlertPriority::Normal));

  {
    PersistedDevicePrefs cold{};
    const std::string seed = build_hello_dirty_prefs(cold);
    LATTER_CHECK(seed.find("bedtime_active=") == std::string::npos);
  }
  reset_all_v3_rev_guards();
  std::vector<std::string> dp_tok(static_cast<size_t>(DevicePrefsField::COUNT), "");
  dp_tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "55";
  dp_tok[static_cast<size_t>(DevicePrefsField::BEDTIME_ACTIVE)] = "1";
  dp_tok[static_cast<size_t>(DevicePrefsField::BEDTIME_DISPLAY)] = "Ambient Clock";
  dp_tok[static_cast<size_t>(DevicePrefsField::BEDTIME_SCHEDULE)] = "1";
  dp_tok[static_cast<size_t>(DevicePrefsField::BEDTIME_START_HOUR)] = "21";
  dp_tok[static_cast<size_t>(DevicePrefsField::BEDTIME_END_HOUR)] = "6";
  uint32_t eff = 0U;
  LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(95, 1, dp_tok), eff, 3000U));
  LATTER_CHECK((eff & kPrefEffectBedtime) != 0U && (eff & kPrefEffectBrightness) != 0U);
  const auto &p = persisted_prefs();
  LATTER_CHECK(p.bedtime_active && p.bedtime_schedule);
  LATTER_CHECK(std::string_view(pref_bedtime_display_cstr(p)) == "Ambient Clock");
  LATTER_CHECK(p.bedtime_start_hour == 21U && p.bedtime_end_hour == 6U);
  {
    PersistedDevicePrefs fp = p;
    fp.screensaver_style = 5U;
    LATTER_CHECK(std::string_view(effective_screensaver_style_cstr(fp)) == "Ambient Clock");
    fp.bedtime_active = false;
    LATTER_CHECK(std::string_view(effective_screensaver_style_cstr(fp)) == "Fireworks");
  }
  LATTER_CHECK(format_agenda_time_label("TODAY 14:30", false) == "TODAY 2:30 PM");
  LATTER_CHECK(format_agenda_time_label("TODAY 14:30", true) == "TODAY 14:30");
  LATTER_CHECK(format_agenda_time_label("TODAY 00:05", false) == "TODAY 12:05 AM");
  LATTER_CHECK(format_agenda_time_label("TOM 12:00", false) == "TOM 12:00 PM");
  LATTER_CHECK(format_agenda_time_label("NOV 08 09:15", false) == "NOV 08 9:15 AM");
  LATTER_CHECK(format_agenda_time_label("TODAY ALL DAY", false) == "TODAY ALL DAY");
  LATTER_CHECK(format_agenda_time_label("NOV 08", false) == "NOV 08");

  char sched_buf[36]{};
  format_schedule_range_label(7, 20, false, sched_buf, sizeof(sched_buf));
  LATTER_CHECK(std::string_view(sched_buf) == "Schedule: 7AM-8PM");
  format_schedule_range_label(7, 20, true, sched_buf, sizeof(sched_buf));
  LATTER_CHECK(std::string_view(sched_buf) == "Schedule: 07:00-20:00");
  format_schedule_range_label(0, 12, false, sched_buf, sizeof(sched_buf));
  LATTER_CHECK(std::string_view(sched_buf) == "Schedule: 12AM-12PM");
}

void test_full_spectrum_brightness_d065() {
  using namespace latter_ui;
  using namespace latter_core;

  LATTER_CHECK(hal_map_perceptual_u32(0.0f, 10U, 255U, 2.20f) == 0U);
  LATTER_CHECK(hal_map_perceptual_u32(0.00005f, 10U, 255U, 2.20f) == 0U);

  LATTER_CHECK(hal_map_dcs_brightness(0.0f) == 0U);
  LATTER_CHECK(hal_map_dcs_brightness(0.01f) == 10U);
  LATTER_CHECK(hal_map_dcs_brightness(1.00f) == 255U);
  const uint8_t dcs_35 = hal_map_dcs_brightness(0.35f);
  const uint8_t dcs_50 = hal_map_dcs_brightness(0.50f);
  LATTER_CHECK(dcs_35 >= 55U && dcs_35 <= 65U);
  LATTER_CHECK(dcs_50 > dcs_35 && dcs_50 < 128U);
  uint8_t prev_dcs = 0U;
  for (int pct = 1; pct <= 100; ++pct) {
    const uint8_t cur = hal_map_dcs_brightness(static_cast<float>(pct) * 0.01f);
    LATTER_CHECK(cur >= 10U && cur >= prev_dcs);
    prev_dcs = cur;
  }

  LATTER_CHECK(hal_backlight_max_duty(12U) == 4095U);
  LATTER_CHECK(hal_map_pwm_on_duty(0.0f, 12U, 3U, 1000U) == 0U);
  const uint32_t pwm_fet_1 = hal_map_pwm_on_duty(0.01f, 12U, 3U, 1000U);
  const uint32_t pwm_fet_35 = hal_map_pwm_on_duty(0.35f, 12U, 3U, 1000U);
  const uint32_t pwm_fet_50 = hal_map_pwm_on_duty(0.50f, 12U, 3U, 1000U);
  const uint32_t pwm_fet_100 = hal_map_pwm_on_duty(1.00f, 12U, 3U, 1000U);
  LATTER_CHECK(pwm_fet_1 == 12U);
  LATTER_CHECK(pwm_fet_35 > 350U && pwm_fet_35 < 450U);
  LATTER_CHECK(pwm_fet_50 > pwm_fet_35 && pwm_fet_50 < 2048U);
  LATTER_CHECK(pwm_fet_100 == 4095U);

  LATTER_CHECK(hal_map_pwm_on_duty(0.0f, 12U, 480U, 1000U) == 0U);
  const uint32_t pwm_rc_1 = hal_map_pwm_on_duty(0.01f, 12U, 480U, 1000U);
  const uint32_t pwm_rc_35 = hal_map_pwm_on_duty(0.35f, 12U, 480U, 1000U);
  const uint32_t pwm_rc_50 = hal_map_pwm_on_duty(0.50f, 12U, 480U, 1000U);
  const uint32_t pwm_rc_100 = hal_map_pwm_on_duty(1.00f, 12U, 480U, 1000U);
  LATTER_CHECK(pwm_rc_1 == 1965U);
  LATTER_CHECK(pwm_rc_35 > 2100U && pwm_rc_35 < 2250U);
  LATTER_CHECK(pwm_rc_50 > pwm_rc_35 && pwm_rc_50 < 3030U);
  LATTER_CHECK(pwm_rc_100 == 4095U);

  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(1.00f) - 0.45f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(0.80f) - 0.36f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(0.10f) - 0.05f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(0.03f) - 0.03f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(0.01f) - 0.01f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_bedtime_ambient_brightness(0.15f) - 0.0525f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_bedtime_ambient_brightness(0.05f) - 0.02f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_bedtime_ambient_brightness(0.01f) - 0.01f) < 1e-4f);

  PersistedDevicePrefs p{};
  PrefMask chg{};
  uint32_t eff = 0U;
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::MANUAL_BRIGHTNESS, "42", chg, eff));
  LATTER_CHECK(p.manual_brightness == 42U && get_pref_value_str(PrefKey::MANUAL_BRIGHTNESS, p) == "42");
  LATTER_CHECK((eff & kPrefEffectBrightness) != 0U);
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::DAY_BRIGHTNESS, "150", chg, eff));
  LATTER_CHECK(p.day_brightness == 100U);
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::NIGHT_BRIGHTNESS, "0", chg, eff));
  LATTER_CHECK(p.night_brightness == 1U);
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::BEDTIME_BRIGHTNESS, "12", chg, eff));
  LATTER_CHECK(p.bedtime_brightness == 12U && get_pref_value_str(PrefKey::BEDTIME_BRIGHTNESS, p) == "12");
  const std::string encoded = encode_prefs_mask(
      pref_key_bit(PrefKey::MANUAL_BRIGHTNESS) | pref_key_bit(PrefKey::BEDTIME_BRIGHTNESS), p);
  LATTER_CHECK(encoded.find("manual_brightness=42") != std::string::npos);
  LATTER_CHECK(encoded.find("bedtime_brightness=12") != std::string::npos);

  device_prefs_state() = DevicePrefsState{};
  persisted_prefs() = PersistedDevicePrefs{};
  std::vector<std::string> dp_tok(static_cast<size_t>(DevicePrefsField::COUNT), "");
  dp_tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "61";
  dp_tok[static_cast<size_t>(DevicePrefsField::MANUAL_BRIGHTNESS)] = "27";
  dp_tok[static_cast<size_t>(DevicePrefsField::DAY_BRIGHTNESS)] = "85";
  dp_tok[static_cast<size_t>(DevicePrefsField::NIGHT_BRIGHTNESS)] = "18";
  dp_tok[static_cast<size_t>(DevicePrefsField::BEDTIME_BRIGHTNESS)] = "9";
  uint32_t p_eff = 0U;
  LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(96, 1, dp_tok), p_eff, 4000U));
  LATTER_CHECK((p_eff & kPrefEffectBrightness) != 0U);
  LATTER_CHECK(persisted_prefs().manual_brightness_pct() == 27U);
  LATTER_CHECK(persisted_prefs().day_brightness_pct() == 85U);
  LATTER_CHECK(persisted_prefs().night_brightness_pct() == 18U);
  LATTER_CHECK(persisted_prefs().bedtime_brightness_pct() == 9U);
}

void test_ambient_light_brightness_d075() {
  using namespace latter_ui;
  using namespace latter_core;

  int8_t pct = -1;
  std::string lux;
  LATTER_CHECK(parse_area_ambient_light("57|5.6", pct, lux));
  LATTER_CHECK(pct == 57 && lux == "5.6");
  LATTER_CHECK(parse_area_ambient_light("0|0", pct, lux));
  LATTER_CHECK(pct == 0 && lux == "0");
  LATTER_CHECK(parse_area_ambient_light("100|250", pct, lux));
  LATTER_CHECK(pct == 100 && lux == "250");
  LATTER_CHECK(!parse_area_ambient_light("", pct, lux));
  LATTER_CHECK(pct == -1 && lux.empty());
  LATTER_CHECK(!parse_area_ambient_light("101|999", pct, lux));
  LATTER_CHECK(pct == -1);
  LATTER_CHECK(!parse_area_ambient_light("abc|10", pct, lux));
  LATTER_CHECK(pct == -1);

  PersistedDevicePrefs p{};
  p.auto_brightness_mode = 3;
  p.day_brightness = 90;
  p.night_brightness = 20;
  p.bedtime_brightness = 12;
  p.bedtime_active = false;
  LATTER_CHECK(std::string_view(pref_auto_brightness_mode_cstr(p)) == "Ambient Light");

  const auto b_dark = resolve_active_brightness_target(p, "below_horizon", true, 22, 0, -1);
  LATTER_CHECK(b_dark.kind == ActiveBrightnessTargetKind::AMBIENT && b_dark.pct == 20U);

  const auto b_mid = resolve_active_brightness_target(p, "below_horizon", true, 22, 50, -1);
  LATTER_CHECK(b_mid.kind == ActiveBrightnessTargetKind::AMBIENT && b_mid.pct == 55U);

  const auto b_bright = resolve_active_brightness_target(p, "below_horizon", true, 22, 100, -1);
  LATTER_CHECK(b_bright.kind == ActiveBrightnessTargetKind::AMBIENT && b_bright.pct == 90U);

  const auto b_ovr = resolve_active_brightness_target(p, "below_horizon", true, 22, 50, 77);
  LATTER_CHECK(b_ovr.kind == ActiveBrightnessTargetKind::AMBIENT_OVERRIDE && b_ovr.pct == 77U);

  p.bedtime_active = true;
  const auto b_bed = resolve_active_brightness_target(p, "above_horizon", true, 12, 100, 88);
  LATTER_CHECK(b_bed.kind == ActiveBrightnessTargetKind::BEDTIME && b_bed.pct == 12U);

  p.bedtime_active = false;
  const auto b_fb_night = resolve_active_brightness_target(p, "below_horizon", true, 22, -1, -1);
  LATTER_CHECK(b_fb_night.kind == ActiveBrightnessTargetKind::NIGHT && b_fb_night.pct == 20U);

  const auto b_fb_day = resolve_active_brightness_target(p, "above_horizon", true, 12, -1, -1);
  LATTER_CHECK(b_fb_day.kind == ActiveBrightnessTargetKind::DAY && b_fb_day.pct == 90U);

  // Cold-boot empty sun_state falls back to local RTC clock (D-085) instead of blinding daytime brightness at night.
  const auto b_cold_night = resolve_active_brightness_target(p, "", true, 23, -1, -1);
  LATTER_CHECK(b_cold_night.kind == ActiveBrightnessTargetKind::NIGHT && b_cold_night.pct == 20U);
  const auto b_cold_day = resolve_active_brightness_target(p, "", true, 14, -1, -1);
  LATTER_CHECK(b_cold_day.kind == ActiveBrightnessTargetKind::DAY && b_cold_day.pct == 90U);
  p.auto_brightness_mode = static_cast<uint8_t>(AutoBrightnessMode::SunSynchronized);
  LATTER_CHECK(resolve_active_brightness_target(p, "", true, 2).kind == ActiveBrightnessTargetKind::NIGHT);
  LATTER_CHECK(resolve_active_brightness_target(p, "", true, 12).kind == ActiveBrightnessTargetKind::DAY);
  p.auto_brightness_mode = static_cast<uint8_t>(AutoBrightnessMode::AmbientLight);

  // Inverted night_brightness > day_brightness configuration never inverts or underflows.
  PersistedDevicePrefs inv{};
  inv.day_brightness = 25;
  inv.night_brightness = 85;
  inv.day_start_hour = 7;
  inv.night_start_hour = 21;
  inv.bedtime_active = false;
  inv.auto_brightness_mode = static_cast<uint8_t>(AutoBrightnessMode::AmbientLight);
  LATTER_CHECK(resolve_active_brightness_target(inv, "below_horizon", true, 23, 0, -1).pct == 25U);
  LATTER_CHECK(resolve_active_brightness_target(inv, "below_horizon", true, 23, 50, -1).pct == 55U);
  LATTER_CHECK(resolve_active_brightness_target(inv, "above_horizon", true, 12, 100, -1).pct == 85U);
  LATTER_CHECK(resolve_active_brightness_target(inv, "below_horizon", true, 23, -1, -1).pct == 25U);
  LATTER_CHECK(resolve_active_brightness_target(inv, "above_horizon", true, 12, -1, -1).pct == 85U);
  inv.auto_brightness_mode = static_cast<uint8_t>(AutoBrightnessMode::SunSynchronized);
  LATTER_CHECK(resolve_active_brightness_target(inv, "below_horizon", true, 23).pct == 25U);
  LATTER_CHECK(resolve_active_brightness_target(inv, "above_horizon", true, 12).pct == 85U);
  inv.auto_brightness_mode = static_cast<uint8_t>(AutoBrightnessMode::TimeSchedule);
  LATTER_CHECK(resolve_active_brightness_target(inv, "above_horizon", true, 2).pct == 25U);
  LATTER_CHECK(resolve_active_brightness_target(inv, "below_horizon", true, 14).pct == 85U);

  reset_runtime_state();
  v3_domain_reducer().reset();
  persisted_prefs() = p;
  runtime_state().nav.display_power_state = 0;
  runtime_state().nav.ambient_bri_override_pct = 75;

  auto make_area_payload = [](uint32_t rev, std::string_view area_id, std::string_view amb) {
    std::string out = "v3\x1f" "1\x1f" + std::to_string(rev);
    for (size_t i = 3; i < static_cast<size_t>(AreaCtxField::COUNT); ++i) {
      out.push_back(kProtocolFieldSep);
      if (i == static_cast<size_t>(AreaCtxField::AREA_ID)) out.append(area_id);
      else if (i == static_cast<size_t>(AreaCtxField::AREA_AMBIENT_LIGHT)) out.append(amb);
    }
    return out;
  };

  V3ReduceContext rctx{};
  rctx.now_ms = 5000U;
  LATTER_CHECK(parse_v3_area_ctx_payload(make_area_payload(1U, "living_room", "58|5.6"),
                                         area_ctx_state(), home_hub_state()));
  LATTER_CHECK(area_ctx_state().area_ambient_pct == 58);
  LATTER_CHECK(area_ctx_state().area_lux_display == "5.6");
  const auto eff1 = v3_domain_reducer().reduce("area_context", rctx);
  LATTER_CHECK(eff1.area_changed && eff1.sync_display_brightness && eff1.sync_display_brightness_smooth);
  LATTER_CHECK(runtime_state().nav.ambient_bri_override_pct == -1);

  LATTER_CHECK(parse_v3_area_ctx_payload(make_area_payload(2U, "living_room", "58|5.6"),
                                         area_ctx_state(), home_hub_state()));
  const auto eff2 = v3_domain_reducer().reduce("area_context", rctx);
  LATTER_CHECK(!eff2.sync_display_brightness);

  LATTER_CHECK(parse_v3_area_ctx_payload(make_area_payload(3U, "living_room", "82|18"),
                                         area_ctx_state(), home_hub_state()));
  const auto eff3 = v3_domain_reducer().reduce("area_context", rctx);
  LATTER_CHECK(eff3.sync_display_brightness && eff3.sync_display_brightness_smooth);
  LATTER_CHECK(area_ctx_state().area_ambient_pct == 82);
}

void test_music_auto_open_and_lyrics_fallback_d080() {
  using namespace latter_ui;
  using namespace latter_core;
  reset_runtime_state();
  v3_domain_reducer().reset();

  // 1. NVS v3.0 GA floor rejects pre-v3.0 truncated prefixes and accepts v3.0+ blobs cleanly
  {
    PersistedDevicePrefs legacy{};
    legacy.do_not_disturb = true;
    legacy.music_auto_open = 2U;
    PersistedDevicePrefs restored{};
    restored.music_auto_open = 2U;
    LATTER_CHECK(!load_persisted_prefs_from_buffer(
        &legacy, offsetof(PersistedDevicePrefs, music_auto_open), restored));
    LATTER_CHECK(load_persisted_prefs_from_buffer(
        &legacy, kPersistedPrefsMinSize, restored));
    LATTER_CHECK(restored.do_not_disturb);
    LATTER_CHECK(restored.music_auto_open == 2U);
    LATTER_CHECK(std::string_view(pref_music_auto_open_cstr(restored)) == "Off");
  }

  // Round-trip PrefKey::MUSIC_AUTO_OPEN via set_pref_by_key_str
  {
    PersistedDevicePrefs p{};
    PrefMask chg{};
    uint32_t eff = 0U;
    LATTER_CHECK(set_pref_by_key_str(p, PrefKey::MUSIC_AUTO_OPEN, "Lyrics", chg, eff));
    LATTER_CHECK(p.music_auto_open == 1U && p.music_auto_open_enum() == MusicAutoOpenMode::Lyrics);
    LATTER_CHECK(get_pref_value_str(PrefKey::MUSIC_AUTO_OPEN, p) == "Lyrics");
    LATTER_CHECK(set_pref_by_key_str(p, PrefKey::MUSIC_AUTO_OPEN, "Off", chg, eff));
    LATTER_CHECK(p.music_auto_open == 2U && p.music_auto_open_enum() == MusicAutoOpenMode::Off);
    LATTER_CHECK(set_pref_by_key_str(p, PrefKey::MUSIC_AUTO_OPEN, "Now Playing", chg, eff));
    LATTER_CHECK(p.music_auto_open == 0U && p.music_auto_open_enum() == MusicAutoOpenMode::NowPlaying);
  }

  auto push_media = [](uint32_t rev, std::string_view st, std::string_view title,
                       std::string_view artist, std::string_view mtype, std::string_view spk,
                       bool is_def, std::string_view art_url, uint32_t now_ms,
                       bool pin_open = false) -> V3DomainEffects {
    std::vector<std::string> tok(static_cast<size_t>(MediaField::COUNT), "");
    tok[static_cast<size_t>(MediaField::STATE)] = std::string(st);
    tok[static_cast<size_t>(MediaField::TITLE)] = std::string(title);
    tok[static_cast<size_t>(MediaField::ARTIST)] = std::string(artist);
    tok[static_cast<size_t>(MediaField::MEDIA_TYPE)] = std::string(mtype);
    tok[static_cast<size_t>(MediaField::ART_URL)] = std::string(art_url);
    tok[static_cast<size_t>(MediaField::ACTIVE_SPEAKER)] = std::string(spk);
    tok[static_cast<size_t>(MediaField::SPEAKER_NAME)] = "Office Sonos";
    tok[static_cast<size_t>(MediaField::IS_LOCAL)] = "0";
    tok[static_cast<size_t>(MediaField::IS_DEFAULT_SPEAKER)] = is_def ? "1" : "0";
    LATTER_CHECK(parse_v3_media_payload(build_v3_packet(100U, rev, tok), media_state(), now_ms));
    V3ReduceContext ctx{};
    ctx.now_ms = now_ms;
    ctx.pin_pad_open = pin_open;
    return v3_domain_reducer().reduce("active_media", ctx);
  };

  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  prefs.home_screen_enabled = true;
  prefs.music_auto_open = 0U;  // Now Playing
  rs.nav.gui_page = PageId::HOME;
  rs.nav.display_power_state = 0;

  // 2. Cold boot prime: initial idle snapshot does not auto-open
  (void)push_media(1U, "idle", "", "", "music", "media_player.office_sonos", true, "", 1000U);
  LATTER_CHECK(rs.nav.gui_page == PageId::HOME);

  // Non-default speaker starting to play MUST NOT auto-open
  (void)push_media(2U, "playing", "Kitchen Song", "Chef", "music", "media_player.kitchen", false,
                   "http://ha/art/k", 2000U);
  LATTER_CHECK(rs.nav.gui_page == PageId::HOME);

  // 3. External default speaker starts playing -> auto-opens Music Now Playing & fetches art
  const auto eff_np = push_media(3U, "playing", "Time", "Pink Floyd", "music",
                                 "media_player.office_sonos", true, "http://ha/art/1", 3000U);
  LATTER_CHECK(rs.nav.gui_page == PageId::MUSIC);
  LATTER_CHECK(rs.music.view == MusicView::NOW_PLAYING);
  LATTER_CHECK(!rs.music.lyrics_auto_managed);
  LATTER_CHECK(eff_np.mark_dirty_full && eff_np.fetch_remote_art && !eff_np.wake_from_screensaver);

  // User swipes back to Home mid-session -> next track while still playing must NOT re-hijack
  rs.nav.gui_page = PageId::HOME;
  (void)push_media(4U, "playing", "Money", "Pink Floyd", "music",
                   "media_player.office_sonos", true, "http://ha/art/2", 4000U);
  LATTER_CHECK(rs.nav.gui_page == PageId::HOME);

  // 4. Pause then resume with music_auto_open == "Off" stays on Home
  prefs.music_auto_open = 2U;  // Off
  (void)push_media(5U, "paused", "Money", "Pink Floyd", "music",
                   "media_player.office_sonos", true, "http://ha/art/2", 5000U);
  (void)push_media(6U, "playing", "Money", "Pink Floyd", "music",
                   "media_player.office_sonos", true, "http://ha/art/2", 6000U);
  LATTER_CHECK(rs.nav.gui_page == PageId::HOME);

  // 5. Screensaver wake vs Bedtime / Screen Off silent switching
  prefs.music_auto_open = 1U;  // Lyrics
  prefs.bedtime_active = false;
  (void)push_media(7U, "idle", "", "", "music", "media_player.office_sonos", true, "", 7000U);
  rs.nav.gui_page = PageId::HOME;
  rs.nav.display_power_state = 1;  // Screensaver
  const auto eff_wake = push_media(8U, "playing", "Bohemian Rhapsody", "Queen", "music",
                                   "media_player.office_sonos", true, "http://ha/art/3", 8000U);
  LATTER_CHECK(rs.nav.gui_page == PageId::MUSIC);
  LATTER_CHECK(rs.music.view == MusicView::LYRICS);
  LATTER_CHECK(rs.music.lyrics_auto_managed && !rs.music.lyrics_auto_fallback);
  LATTER_CHECK(eff_wake.wake_from_screensaver && eff_wake.fetch_music_lyrics && eff_wake.fetch_remote_art);

  // 6. Lyrics -> Now Playing auto-fallback when empty lyrics arrive, then auto-return on next track with lyrics
  rs.nav.display_power_state = 0;  // Awake after wake_to_active_state
  LATTER_CHECK(apply_music_lyrics_action(1, 0, 1, "Bohemian Rhapsody|Queen", false, ""));
  const auto fb_res = reconcile_lyrics_auto_view(rs, media_state(), prefs);
  LATTER_CHECK(fb_res.view_changed);
  LATTER_CHECK(rs.music.view == MusicView::NOW_PLAYING);
  LATTER_CHECK(rs.music.lyrics_auto_managed && rs.music.lyrics_auto_fallback);

  // Next track starts -> stays silently on Now Playing (no loading flash) while checking lyrics in background
  const auto eff_t2 = push_media(9U, "playing", "Somebody To Love", "Queen", "music",
                                 "media_player.office_sonos", true, "http://ha/art/4", 9000U);
  LATTER_CHECK(rs.music.view == MusicView::NOW_PLAYING);
  LATTER_CHECK(eff_t2.fetch_music_lyrics);

  // Non-empty lyrics arrive -> auto-returns straight to Lyrics!
  LATTER_CHECK(apply_music_lyrics_action(
      2, 0, 1, "Somebody To Love|Queen", true, "1000\x1f" "Can anybody find me"));
  const auto ret_res = reconcile_lyrics_auto_view(rs, media_state(), prefs);
  LATTER_CHECK(ret_res.view_changed);
  LATTER_CHECK(rs.music.view == MusicView::LYRICS);
  LATTER_CHECK(rs.music.lyrics_auto_managed && !rs.music.lyrics_auto_fallback);

  // Podcast track while auto-managed falls back to Now Playing immediately
  (void)push_media(10U, "playing", "Ep 1", "Podcaster", "podcast",
                   "media_player.office_sonos", true, "http://ha/art/5", 10000U);
  LATTER_CHECK(rs.music.view == MusicView::NOW_PLAYING);
  LATTER_CHECK(rs.music.lyrics_auto_managed && rs.music.lyrics_auto_fallback);

  // Manual user override clears lyrics_auto_managed so future tracks stay on user's chosen view
  rs.music.clear_lyrics_auto_state();
  LATTER_CHECK(apply_music_lyrics_action(
      3, 0, 1, "Next Song|Band", true, "500\x1f" "Line one"));
  (void)push_media(11U, "playing", "Next Song", "Band", "music",
                   "media_player.office_sonos", true, "http://ha/art/6", 11000U);
  LATTER_CHECK(rs.music.view == MusicView::NOW_PLAYING);

  // 7. Bedtime Mode on Screensaver stays dark/quiet, switches page silently in background
  (void)push_media(12U, "idle", "", "", "music", "media_player.office_sonos", true, "", 12000U);
  rs.nav.gui_page = PageId::HOME;
  rs.nav.display_power_state = 1;
  prefs.bedtime_active = true;
  const auto eff_bt = push_media(13U, "playing", "Lullaby", "Calm", "music",
                                 "media_player.office_sonos", true, "http://ha/art/7", 13000U);
  LATTER_CHECK(rs.nav.gui_page == PageId::MUSIC);
  LATTER_CHECK(rs.music.view == MusicView::LYRICS);
  LATTER_CHECK(!eff_bt.wake_from_screensaver && !eff_bt.fetch_remote_art && !eff_bt.fetch_music_lyrics);

  // 8. Screen Off (display_power_state == 2) outside Bedtime stays dark, switches page silently
  prefs.bedtime_active = false;
  (void)push_media(14U, "idle", "", "", "music", "media_player.office_sonos", true, "", 14000U);
  rs.nav.gui_page = PageId::HOME;
  rs.nav.display_power_state = 2;
  const auto eff_off = push_media(15U, "playing", "Dawn", "Calm", "music",
                                  "media_player.office_sonos", true, "http://ha/art/8", 15000U);
  LATTER_CHECK(rs.nav.gui_page == PageId::MUSIC);
  LATTER_CHECK(!eff_off.wake_from_screensaver && !eff_off.fetch_remote_art && !eff_off.fetch_music_lyrics);

  // 9. Protected contexts (PIN pad, active Game, Single-App Mode) are never interrupted
  (void)push_media(16U, "idle", "", "", "music", "media_player.office_sonos", true, "", 16000U);
  rs.nav.display_power_state = 0;
  rs.nav.gui_page = PageId::HOME;
  (void)push_media(17U, "playing", "Locked", "Artist", "music",
                   "media_player.office_sonos", true, "", 17000U, /*pin_open=*/true);
  LATTER_CHECK(rs.nav.gui_page == PageId::HOME);

  (void)push_media(18U, "idle", "", "", "music", "media_player.office_sonos", true, "", 18000U);
  rs.nav.gui_page = PageId::GAMES;
  rs.nav.active_game = 1;
  (void)push_media(19U, "playing", "Gaming", "Artist", "music",
                   "media_player.office_sonos", true, "", 19000U);
  LATTER_CHECK(rs.nav.gui_page == PageId::GAMES);
  rs.nav.active_game = 0;

  (void)push_media(20U, "idle", "", "", "music", "media_player.office_sonos", true, "", 20000U);
  prefs.home_screen_enabled = false;
  prefs.start_screen = kStartScreenTimeAppIdx;
  rs.nav.gui_page = PageId::TIME;
  (void)push_media(21U, "playing", "Kiosk", "Artist", "music",
                   "media_player.office_sonos", true, "", 21000U);
  LATTER_CHECK(rs.nav.gui_page == PageId::TIME);
}

static int g_custom_psram_evictor_calls = 0;
static void test_custom_psram_evictor() noexcept { ++g_custom_psram_evictor_calls; }

void test_wave2_overhaul_contracts() {
  using namespace latter_ui;
  using namespace latter_core;

  // 1. Static schema & limit invariants
  static_assert(latter_rpc::kMaxRpcSlices == 32U);
  static_assert(FrameHitTable::kMaxRegions == 128U);
  static_assert(static_cast<size_t>(DevicePrefsField::COUNT) == 78U);
  static_assert(static_cast<size_t>(PrefKey::COUNT) == 72U);
  static_assert(static_cast<uint8_t>(PrefKey::GLASS_STYLE) == 66U);
  static_assert(static_cast<uint8_t>(PrefKey::HOME_SHOW_BATTERY) == 71U);
  static_assert(kHibernateSnapshotVersion == 3U);
  static_assert(kPersistedPrefsMinSize == offsetof(PersistedDevicePrefs, glass_style) + sizeof(PersistedDevicePrefs::glass_style));

  // 2. device_hello clean-boot byte ceiling (zero unchanged defaults dumped; compact RPC payload)
  {
    auto &p = persisted_prefs();
    p = PersistedDevicePrefs{};
    device_prefs_state() = DevicePrefsState{};
    seed_default_prefs_if_needed();
    LATTER_CHECK(p.prefs_seq == 0U && p.dirty_mask.empty());
    LATTER_CHECK(build_hello_dirty_prefs(p).empty());

    MockRpcOutbox outbox{};
    latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
    emit_device_hello(&outbox);
    uint32_t t = 100U;
    while (latter_rpc::has_pending_frames()) {
      latter_rpc::step_outbox_loop(t);
      t += 50U;
    }
    LATTER_CHECK(!outbox.published.empty() && outbox.published.size() <= 5U);
    size_t total_bytes = 0U;
    std::string combined;
    for (const auto &fr : outbox.published) {
      total_bytes += fr.size();
      const size_t sep = fr.find('|');
      combined += (sep != std::string::npos) ? fr.substr(sep + 1U) : fr;
    }
    LATTER_CHECK(total_bytes < 1024U);
    LATTER_CHECK(combined.find("core_rev") != std::string::npos);
    LATTER_CHECK(combined.find("grid_cols") != std::string::npos);
    LATTER_CHECK(combined.find("grid_rows") != std::string::npos);
    LATTER_CHECK(combined.find("max_payload_bytes") == std::string::npos);
    LATTER_CHECK(combined.find("wake_word_mode=") == std::string::npos);

    // Editing a single local pref emits ONLY that key in build_hello_dirty_prefs()
    PrefMask chg{};
    uint32_t eff = 0U;
    LATTER_CHECK(set_pref_by_key_str(p, PrefKey::GLASS_STYLE, "Dark", chg, eff));
    p.dirty_mask |= chg;
    LATTER_CHECK(build_hello_dirty_prefs(p) == "glass_style=Dark");
  }

  // 3. v3_unescape_subfield() single-pass backslash/pipe/semicolon symmetry with HA backend
  {
    LATTER_CHECK(v3_unescape_subfield("") == "");
    LATTER_CHECK(v3_unescape_subfield("plain text") == "plain text");
    LATTER_CHECK(v3_unescape_subfield(R"(a\pb\sc\\d)") == R"(a|b;c\d)");
    LATTER_CHECK(v3_unescape_subfield(R"(\\p\\s\\\\)") == R"(\p\s\\)");
    LATTER_CHECK(v3_unescape_subfield(R"(trail\)") == R"(trail\)");
    LATTER_CHECK(v3_unescape_subfield(R"(keep\x)") == R"(keep\x)");
    LATTER_CHECK(v3_unescape_subfield("Caf\xC3\xA9\\pLive\\s2026\\\\OK") == "Caf\xC3\xA9|Live;2026\\OK");
  }

  // 4. load_persisted_prefs_from_buffer() arbitrary prefix lengths & v3.1 rollback resilience
  {
    PersistedDevicePrefs src{};
    src.prefs_seq = 42U;
    src.glass_style = 2U;
    src.do_not_disturb = true;

    PersistedDevicePrefs dst{};
    // Reject truncated/under-floor buffers, bad magic, wrong version
    LATTER_CHECK(!load_persisted_prefs_from_buffer(nullptr, sizeof(src), dst));
    LATTER_CHECK(!load_persisted_prefs_from_buffer(&src, 0U, dst));
    LATTER_CHECK(!load_persisted_prefs_from_buffer(&src, 8U, dst));
    LATTER_CHECK(!load_persisted_prefs_from_buffer(&src, kPersistedPrefsMinSize - 1U, dst));

    PersistedDevicePrefs bad_magic = src;
    bad_magic.magic = 0xDEADBEEFU;
    LATTER_CHECK(!load_persisted_prefs_from_buffer(&bad_magic, sizeof(bad_magic), dst));

    PersistedDevicePrefs bad_ver = src;
    bad_ver.version = kPersistedPrefsVersion + 1U;
    LATTER_CHECK(!load_persisted_prefs_from_buffer(&bad_ver, sizeof(bad_ver), dst));

    // Accept exact v3.0 GA floor (kPersistedPrefsMinSize) and full sizeof:
    // appended tail fields (home_show_date/time/weather/battery, home_weather_tap_action) auto-default cleanly
    src.home_show_date = false;
    src.home_show_time = false;
    src.home_show_weather = false;
    src.home_weather_tap_action = 2U;
    src.home_show_battery = false;
    dst = PersistedDevicePrefs{};
    LATTER_CHECK(load_persisted_prefs_from_buffer(&src, kPersistedPrefsMinSize, dst));
    LATTER_CHECK(dst.prefs_seq == 42U && dst.glass_style == 2U && dst.do_not_disturb);
    LATTER_CHECK(dst.home_show_date && dst.home_show_time && dst.home_show_weather &&
                 dst.home_weather_tap_action == 0U && dst.home_show_battery);

    dst = PersistedDevicePrefs{};
    LATTER_CHECK(load_persisted_prefs_from_buffer(&src, sizeof(src), dst));
    LATTER_CHECK(dst.prefs_seq == 42U && dst.glass_style == 2U && dst.do_not_disturb);
    LATTER_CHECK(!dst.home_show_date && !dst.home_show_time && !dst.home_show_weather &&
                 dst.home_weather_tap_action == 2U && !dst.home_show_battery);

    // Accept future oversized v3.1+ append-only blob (rollback resilience: copies our prefix cleanly)
    std::vector<uint8_t> future_blob(sizeof(PersistedDevicePrefs) + 32U, 0xA5U);
    std::memcpy(future_blob.data(), &src, sizeof(src));
    dst = PersistedDevicePrefs{};
    LATTER_CHECK(load_persisted_prefs_from_buffer(future_blob.data(), future_blob.size(), dst));
    LATTER_CHECK(dst.prefs_seq == 42U && dst.glass_style == 2U && dst.do_not_disturb);
  }

  // 5. PrefKey::GLASS_STYLE wire round-trip, validation & clamping
  {
    PersistedDevicePrefs p{};
    LATTER_CHECK(p.glass_style == 0U && p.glass_style_enum() == GlassStyle::Auto);
    LATTER_CHECK(std::string_view(pref_glass_style_cstr(p)) == "Auto");

    PrefMask chg{};
    uint32_t eff = 0U;
    LATTER_CHECK(set_pref_by_key_str(p, PrefKey::GLASS_STYLE, "Clear", chg, eff));
    LATTER_CHECK(p.glass_style == 1U && p.glass_style_enum() == GlassStyle::Clear);
    LATTER_CHECK((eff & kPrefEffectRedraw) != 0U);
    LATTER_CHECK(get_pref_value_str(PrefKey::GLASS_STYLE, p) == "Clear");

    LATTER_CHECK(set_pref_by_key_str(p, PrefKey::GLASS_STYLE, "Dark", chg, eff));
    LATTER_CHECK(p.glass_style == 2U && p.glass_style_enum() == GlassStyle::Dark);
    LATTER_CHECK(get_pref_value_str(PrefKey::GLASS_STYLE, p) == "Dark");

    LATTER_CHECK(!set_pref_by_key_str(p, PrefKey::GLASS_STYLE, "FrostedInvalid", chg, eff));
    LATTER_CHECK(p.glass_style == 2U);

    // Out-of-range byte clamps safely via pref_glass_style_cstr
    p.glass_style = 99U;
    LATTER_CHECK(std::string_view(pref_glass_style_cstr(p)) == "Auto");

    // 78-field DevicePrefs v3 wire parse sets glass_style and Home screen visibility/tap prefs cleanly
    reset_all_v3_rev_guards();
    persisted_prefs() = PersistedDevicePrefs{};
    device_prefs_state() = DevicePrefsState{};
    std::vector<std::string> dp_tok(static_cast<size_t>(DevicePrefsField::COUNT), "");
    dp_tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "88";
    dp_tok[static_cast<size_t>(DevicePrefsField::GLASS_STYLE)] = "Clear";
    dp_tok[static_cast<size_t>(DevicePrefsField::HOME_CLOCK_TAP_ACTION)] = "None";
    dp_tok[static_cast<size_t>(DevicePrefsField::HOME_DATE_TAP_ACTION)] = "None";
    dp_tok[static_cast<size_t>(DevicePrefsField::HOME_SHOW_DATE)] = "0";
    dp_tok[static_cast<size_t>(DevicePrefsField::HOME_SHOW_TIME)] = "0";
    dp_tok[static_cast<size_t>(DevicePrefsField::HOME_SHOW_WEATHER)] = "0";
    dp_tok[static_cast<size_t>(DevicePrefsField::HOME_WEATHER_TAP_ACTION)] = "5-Day Forecast";
    dp_tok[static_cast<size_t>(DevicePrefsField::HOME_SHOW_BATTERY)] = "0";
    uint32_t p_eff = 0U;
    LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(99, 1, dp_tok), p_eff, 5000U));
    LATTER_CHECK(persisted_prefs().glass_style == 1U);
    LATTER_CHECK(persisted_prefs().home_clock_tap_action == 2U &&
                 std::string_view(pref_home_clock_tap_cstr(persisted_prefs())) == "None");
    LATTER_CHECK(persisted_prefs().home_date_tap_action == 2U &&
                 std::string_view(pref_home_date_tap_cstr(persisted_prefs())) == "None");
    LATTER_CHECK(!persisted_prefs().home_show_date && !persisted_prefs().home_show_time &&
                 !persisted_prefs().home_show_weather && !persisted_prefs().home_show_battery);
    LATTER_CHECK(persisted_prefs().home_weather_tap_action == 1U &&
                 std::string_view(pref_home_weather_tap_cstr(persisted_prefs())) == "5-Day Forecast");
    LATTER_CHECK(get_pref_value_str(PrefKey::HOME_WEATHER_TAP_ACTION, persisted_prefs()) == "5-Day Forecast");
    LATTER_CHECK((p_eff & kPrefEffectRedraw) != 0U);
  }

  // 6. PSRAM cache evictor registry (breaks ui_gfx -> page_weather link inversion)
  {
    g_custom_psram_evictor_calls = 0;
    LATTER_CHECK(register_psram_cache_evictor(&test_custom_psram_evictor));
    LATTER_CHECK(register_psram_cache_evictor(&test_custom_psram_evictor));  // idempotent deduplication
    evict_registered_psram_caches();
    LATTER_CHECK(g_custom_psram_evictor_calls == 1);
    evict_registered_psram_caches();
    LATTER_CHECK(g_custom_psram_evictor_calls == 2);
  }
}

void test_wave2_reconnect_dirty_prefs_and_contract_wiring() {
  using namespace latter_core;

  // 1. Pref contract wiring: pref_key_name() covers all 67 PrefKey values and matches kPrefContractSpecs.
  size_t contract_matched = 0U;
  for (uint8_t k = 0; k < static_cast<uint8_t>(PrefKey::COUNT); ++k) {
    const auto pk = static_cast<PrefKey>(k);
    const std::string_view nm = pref_key_name(pk);
    LATTER_CHECK(!nm.empty());
    if (pk == PrefKey::BLE_BEACON_ENABLED || pk == PrefKey::TOOLS_ITEMS || pk == PrefKey::GAMES_ITEMS) {
      LATTER_CHECK(find_pref_contract(nm) == nullptr);
    } else {
      const PrefContractSpec *spec = find_pref_contract(nm);
      LATTER_CHECK(spec != nullptr && spec->key == nm);
      ++contract_matched;
    }
  }
  LATTER_CHECK(contract_matched == 69U);
  LATTER_CHECK(std::string_view(pref_key_name(PrefKey::BLE_BEACON_ENABLED)) == "ble_beacon_enabled");
  LATTER_CHECK(std::string_view(pref_key_name(PrefKey::TOOLS_ITEMS)) == "tools_items");
  LATTER_CHECK(std::string_view(pref_key_name(PrefKey::GAMES_ITEMS)) == "games_items");
  LATTER_CHECK(std::string_view(pref_key_name(PrefKey::MANUAL_BRIGHTNESS)) == "manual_brightness");
  LATTER_CHECK(std::string_view(pref_key_name(PrefKey::GLASS_STYLE)) == "glass_style");
  LATTER_CHECK(std::string_view(pref_key_name(PrefKey::HOME_SHOW_DATE)) == "home_show_date");
  LATTER_CHECK(std::string_view(pref_key_name(PrefKey::HOME_SHOW_TIME)) == "home_show_time");
  LATTER_CHECK(std::string_view(pref_key_name(PrefKey::HOME_SHOW_WEATHER)) == "home_show_weather");
  LATTER_CHECK(std::string_view(pref_key_name(PrefKey::HOME_WEATHER_TAP_ACTION)) == "home_weather_tap_action");
  LATTER_CHECK(std::string_view(pref_key_name(PrefKey::HOME_SHOW_BATTERY)) == "home_show_battery");
  // The single non-PersistedDevicePrefs contract spec is volatile weather_sim_condition.
  LATTER_CHECK(kPrefContractSpecs.size() == 70U);
  LATTER_CHECK(find_pref_contract("weather_sim_condition") != nullptr);

  // Generated default & clamp wiring on PersistedDevicePrefs.
  {
    PersistedDevicePrefs def{};
    LATTER_CHECK(def.manual_brightness == static_cast<uint8_t>(kPrefDefaultManualBrightness));
    LATTER_CHECK(def.day_brightness == static_cast<uint8_t>(kPrefDefaultDayBrightness));
    LATTER_CHECK(def.night_brightness == static_cast<uint8_t>(kPrefDefaultNightBrightness));
    LATTER_CHECK(def.bedtime_brightness == static_cast<uint8_t>(kPrefDefaultBedtimeBrightness));
    LATTER_CHECK(def.alert_volume == static_cast<uint8_t>(kPrefDefaultAlertVolume));
    LATTER_CHECK(def.album_art_style == kPrefDefaultAlbumArtStyleIdx);
    LATTER_CHECK(def.glass_style == kPrefDefaultGlassStyleIdx);
    LATTER_CHECK(def.music_auto_open == kPrefDefaultMusicAutoOpenIdx);
    LATTER_CHECK(def.do_not_disturb == kPrefDefaultDoNotDisturb);
    def.manual_brightness = 250U;
    def.day_brightness = 0U;
    def.alert_volume = 1U;
    clamp_prefs_to_capabilities(def);
    LATTER_CHECK(def.manual_brightness == static_cast<uint8_t>(kPrefMaxManualBrightness));
    LATTER_CHECK(def.day_brightness == static_cast<uint8_t>(kPrefMinDayBrightness));
    LATTER_CHECK(def.alert_volume == static_cast<uint8_t>(kPrefMinAlertVolume));
  }

  // 2. P0 Reconnect Pref-Loss Fix: offline dirty prefs survive pre-hello pushes (even with in_seq > prefs_seq)
  //    and stale post-hello pushes (in_seq <= prefs_seq), and only clear after hello completes with newer in_seq.
  reset_all_v3_rev_guards();
  auto &p = persisted_prefs();
  p = PersistedDevicePrefs{};
  device_prefs_state() = DevicePrefsState{};
  hello_sync_state() = HelloSyncState{};

  p.prefs_seq = 10U;
  p.manual_brightness = 80U;
  p.day_brightness = 70U;
  p.time_format = 0U;  // 12-hour
  p.dirty_mask.clear();

  // Simulate offline on-device pref edits prior to reconnect.
  p.manual_brightness = 35U;
  p.time_format = 1U;  // 24-hour
  p.dirty_mask |= pref_key_bit(PrefKey::MANUAL_BRIGHTNESS);
  p.dirty_mask |= pref_key_bit(PrefKey::TIME_FORMAT);

  // Case A: device_hello is in-flight; HA pushes a pre-hello snapshot with higher in_seq=12.
  hello_sync_state().in_flight = true;
  {
    std::vector<std::string> tok(static_cast<size_t>(DevicePrefsField::COUNT), "");
    tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "12";
    tok[static_cast<size_t>(DevicePrefsField::MANUAL_BRIGHTNESS)] = "90";
    tok[static_cast<size_t>(DevicePrefsField::TIME_FORMAT)] = "12-hour";
    tok[static_cast<size_t>(DevicePrefsField::DAY_BRIGHTNESS)] = "60";
    uint32_t eff = 0U;
    LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(201, 1, tok), eff, 1000U));
    LATTER_CHECK(p.manual_brightness == 35U);
    LATTER_CHECK(p.time_format == 1U);
    LATTER_CHECK(p.day_brightness == 60U);
    LATTER_CHECK((p.dirty_mask & pref_key_bit(PrefKey::MANUAL_BRIGHTNESS)).any());
    LATTER_CHECK((p.dirty_mask & pref_key_bit(PrefKey::TIME_FORMAT)).any());
    LATTER_CHECK(p.prefs_seq == 10U);
  }

  // Case B: device_hello is still in-flight; HA pushes an older in_seq=8 snapshot.
  {
    std::vector<std::string> tok(static_cast<size_t>(DevicePrefsField::COUNT), "");
    tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "8";
    tok[static_cast<size_t>(DevicePrefsField::MANUAL_BRIGHTNESS)] = "95";
    tok[static_cast<size_t>(DevicePrefsField::TIME_FORMAT)] = "12-hour";
    tok[static_cast<size_t>(DevicePrefsField::DAY_BRIGHTNESS)] = "55";
    uint32_t eff = 0U;
    LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(201, 2, tok), eff, 1100U));
    LATTER_CHECK(p.manual_brightness == 35U);
    LATTER_CHECK(p.time_format == 1U);
    LATTER_CHECK(p.day_brightness == 55U);
    LATTER_CHECK((p.dirty_mask & pref_key_bit(PrefKey::MANUAL_BRIGHTNESS)).any());
    LATTER_CHECK((p.dirty_mask & pref_key_bit(PrefKey::TIME_FORMAT)).any());
    LATTER_CHECK(p.prefs_seq == 10U);
  }

  // Case C: hello finishes, but a stale push arrives with in_seq=10 <= p.prefs_seq.
  hello_sync_state().in_flight = false;
  {
    std::vector<std::string> tok(static_cast<size_t>(DevicePrefsField::COUNT), "");
    tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "10";
    tok[static_cast<size_t>(DevicePrefsField::MANUAL_BRIGHTNESS)] = "88";
    tok[static_cast<size_t>(DevicePrefsField::TIME_FORMAT)] = "12-hour";
    tok[static_cast<size_t>(DevicePrefsField::DAY_BRIGHTNESS)] = "50";
    uint32_t eff = 0U;
    LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(201, 3, tok), eff, 1200U));
    LATTER_CHECK(p.manual_brightness == 35U);
    LATTER_CHECK(p.time_format == 1U);
    LATTER_CHECK(p.day_brightness == 50U);
    LATTER_CHECK((p.dirty_mask & pref_key_bit(PrefKey::MANUAL_BRIGHTNESS)).any());
    LATTER_CHECK((p.dirty_mask & pref_key_bit(PrefKey::TIME_FORMAT)).any());
    LATTER_CHECK(p.prefs_seq == 10U);
  }

  // Case D: post-hello authoritative push arrives with in_seq=12 > p.prefs_seq (even matching Case A's in_seq!).
  {
    std::vector<std::string> tok(static_cast<size_t>(DevicePrefsField::COUNT), "");
    tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "12";
    tok[static_cast<size_t>(DevicePrefsField::MANUAL_BRIGHTNESS)] = "35";
    tok[static_cast<size_t>(DevicePrefsField::TIME_FORMAT)] = "24-hour";
    tok[static_cast<size_t>(DevicePrefsField::DAY_BRIGHTNESS)] = "65";
    uint32_t eff = 0U;
    LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(201, 4, tok), eff, 1300U));
    LATTER_CHECK(p.manual_brightness == 35U);
    LATTER_CHECK(p.time_format == 1U);
    LATTER_CHECK(p.day_brightness == 65U);
    LATTER_CHECK(p.dirty_mask.empty());
    LATTER_CHECK(p.prefs_seq == 12U);
  }

  // Case E: clean device (empty dirty_mask) during hello in_flight still accepts HA prefs and advances prefs_seq.
  hello_sync_state().in_flight = true;
  {
    std::vector<std::string> tok(static_cast<size_t>(DevicePrefsField::COUNT), "");
    tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "15";
    tok[static_cast<size_t>(DevicePrefsField::MANUAL_BRIGHTNESS)] = "75";
    uint32_t eff = 0U;
    LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(201, 5, tok), eff, 1400U));
    LATTER_CHECK(p.manual_brightness == 75U);
    LATTER_CHECK(p.dirty_mask.empty());
    LATTER_CHECK(p.prefs_seq == 15U);
  }
  hello_sync_state() = HelloSyncState{};
}

void test_core_store_consolidation() {
  LATTER_CHECK(&latter_core::link_state() == &latter_core::g_link_state);
  LATTER_CHECK(&latter_core::v3_mismatch_warn_count() == &latter_core::g_v3_mismatch_warn_count);
  LATTER_CHECK(&latter_core::device_prefs_state() == &latter_core::g_device_prefs_state);
  LATTER_CHECK(&latter_core::persisted_prefs() == &latter_core::g_persisted_prefs);
  LATTER_CHECK(&latter_core::device_prefs() == &latter_core::g_persisted_prefs);
  LATTER_CHECK(&latter_core::bedtime_schedule_tracker() == &latter_core::g_bedtime_schedule_tracker);
  LATTER_CHECK(&latter_core::runtime_state() == &latter_core::g_runtime_state);
  LATTER_CHECK(&latter_core::media_state() == &latter_core::g_media_state);
  LATTER_CHECK(&latter_core::home_hub_state() == &latter_core::g_home_hub_state);
  LATTER_CHECK(&latter_core::area_ctx_state() == &latter_core::g_area_ctx_state);
  LATTER_CHECK(&latter_core::remote_state() == &latter_core::g_remote_state);
  LATTER_CHECK(&latter_core::weather_state() == &latter_core::g_weather_state);
  LATTER_CHECK(&latter_core::calendar_state() == &latter_core::g_calendar_state);
  LATTER_CHECK(&latter_core::shortcuts_state() == &latter_core::g_shortcuts_state);

  const latter_core::CoreStoreFootprint fp = latter_core::core_store_footprint();
  LATTER_CHECK(fp.pod_bytes > 0U);
  LATTER_CHECK(fp.dynamic_bytes > 0U);
  LATTER_CHECK(fp.total_bytes == fp.pod_bytes + fp.dynamic_bytes);

  const uint32_t prev_seq = latter_core::g_persisted_prefs.prefs_seq;
  latter_core::g_persisted_prefs.prefs_seq = 777U;
  LATTER_CHECK(latter_core::persisted_prefs().prefs_seq == 777U);
  latter_core::g_persisted_prefs.prefs_seq = prev_seq;

  latter_core::reset_runtime_state(true);
  LATTER_CHECK(&latter_core::runtime_state() == &latter_core::g_runtime_state);
}

}  // namespace

void run_state_prefs_nvs_tests() {
  test_device_prefs_v2_and_event_timer_sync();
  test_universal_shortcuts_and_pinned_app();
  test_tools_games_launcher_prefs_and_stopwatch_commands();
  test_bedtime_mode_d063();
  test_full_spectrum_brightness_d065();
  test_ambient_light_brightness_d075();
  test_music_auto_open_and_lyrics_fallback_d080();
  test_wave2_overhaul_contracts();
  test_wave2_reconnect_dirty_prefs_and_contract_wiring();
  test_core_store_consolidation();
}

}  // namespace latter_test
