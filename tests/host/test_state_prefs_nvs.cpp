#include "test_harness.h"

namespace latter_test {

namespace {

void test_device_prefs_v2_and_event_timer_sync() {
  static_assert(std::is_trivially_copyable_v<latter_ui::PersistedDevicePrefs>);
  LATTER_CHECK(latter_ui::kPersistedPrefsVersion == 1U);
  LATTER_CHECK(static_cast<size_t>(latter_ui::PrefKey::COUNT) == 65U);
  LATTER_CHECK(static_cast<size_t>(latter_ui::DevicePrefsField::COUNT) == 71U);
  LATTER_CHECK(static_cast<size_t>(latter_ui::AreaCtxField::COUNT) == 43U);
  LATTER_CHECK(static_cast<size_t>(latter_ui::ShortcutsField::COUNT) == 8U);

  LATTER_CHECK(latter_ui::PageId::HOME_HUB == 9);
  LATTER_CHECK(latter_ui::PageId::TIME == 10);
  LATTER_CHECK(latter_ui::PageId::PINNED == 11);
  LATTER_CHECK(latter_ui::PageId::CALENDAR == 12);
  LATTER_CHECK(latter_ui::PageId::CAMERAS == 13);
  LATTER_CHECK(latter_ui::find_shortcut_route("app:pinned") != nullptr);
  LATTER_CHECK(latter_ui::find_shortcut_route("app:calendar") != nullptr);
  LATTER_CHECK(latter_ui::find_shortcut_route("calendar:month") != nullptr);
  LATTER_CHECK(latter_ui::find_shortcut_route("tools:calendar") == nullptr);
  LATTER_CHECK(latter_ui::find_shortcut_route("weather:moon") != nullptr);
  LATTER_CHECK(latter_ui::find_shortcut_route("app:cameras") != nullptr);
  LATTER_CHECK(latter_ui::find_shortcut_route("cameras:grid") != nullptr);
  LATTER_CHECK(latter_ui::find_shortcut_route("home_hub:cameras") == nullptr);

  latter_ui::PersistedDevicePrefs cam_prefs{};
  cam_prefs.start_screen = static_cast<uint8_t>(latter_ui::find_opt_index(latter_ui::kStartScreenOpts, "Pinned"));
  LATTER_CHECK(latter_ui::resolve_start_screen_page(cam_prefs) == latter_ui::PageId::PINNED);
  cam_prefs.start_screen = static_cast<uint8_t>(latter_ui::find_opt_index(latter_ui::kStartScreenOpts, "Calendar"));
  LATTER_CHECK(latter_ui::resolve_start_screen_page(cam_prefs) == latter_ui::PageId::CALENDAR);
  cam_prefs.start_screen = static_cast<uint8_t>(latter_ui::find_opt_index(latter_ui::kStartScreenOpts, "Cameras"));
  LATTER_CHECK(latter_ui::resolve_start_screen_page(cam_prefs) == latter_ui::PageId::CAMERAS);
  LATTER_CHECK(latter_ui::is_cameras_start_screen(cam_prefs));
  cam_prefs.start_screen = static_cast<uint8_t>(latter_ui::find_opt_index(latter_ui::kStartScreenOpts, "Tools"));
  LATTER_CHECK(latter_ui::resolve_start_screen_page(cam_prefs) == 8);

  LATTER_CHECK(latter_ui::is_sensitive_perimeter_action_impl("home_hub.garage", "turn_on", 100) == true);
  LATTER_CHECK(latter_ui::is_sensitive_perimeter_action_impl("home_hub.garage", "turn_on") == true);
  const std::string retired_target = std::string("whole") + "_home.garage";
  LATTER_CHECK(latter_ui::is_sensitive_perimeter_action_impl(retired_target, "turn_on", 100) == false);
  LATTER_CHECK(latter_ui::is_sensitive_perimeter_action_impl("lock", "open") == true);
  LATTER_CHECK(latter_ui::is_sensitive_perimeter_action_impl("cover.garage_door", "open") == true);
  LATTER_CHECK(!latter_ui::is_sensitive_perimeter_action_impl("light.porch", "turn_on"));

  latter_ui::home_hub_tab() = 2;
  LATTER_CHECK(latter_ui::home_hub_tab() == 2);
  latter_ui::home_hub_state().thermostat_count = 1;
  latter_ui::home_hub_state().thermostats[0].mode = "heat";
  LATTER_CHECK(latter_ui::home_hub_state().thermostats[0].mode == "heat");

  LATTER_CHECK(latter_ui::is_configured_remote_slot("remote.living_room"));
  LATTER_CHECK(!latter_ui::is_configured_remote_slot(""));

  int hh_tabs[latter_ui::kMaxHomeHubTabs] = {-1, -1, -1, -1, -1};
  latter_ui::AreaContextState uninit_ac{};
  LATTER_CHECK(uninit_ac.epoch == 0U && uninit_ac.home_categories.empty());
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(hh_tabs, uninit_ac) == 0);
  uninit_ac.home_categories = latter_ui::kDefaultHomeCategories;
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(hh_tabs, uninit_ac) == 0);
  uninit_ac.epoch = 1U;
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(hh_tabs, uninit_ac) == 5);
  LATTER_CHECK(hh_tabs[0] == 0 && hh_tabs[1] == 1 && hh_tabs[2] == 2 &&
               hh_tabs[3] == 3 && hh_tabs[4] == 4);
  uninit_ac.epoch = 10U;
  uninit_ac.home_categories = "vacuum,thermostat,unknown,vacuum,intercom";
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(hh_tabs, uninit_ac) == 3);
  LATTER_CHECK(hh_tabs[0] == latter_ui::HomeHubTab::VACUUM &&
               hh_tabs[1] == latter_ui::HomeHubTab::THERMOSTAT &&
               hh_tabs[2] == latter_ui::HomeHubTab::INTERCOM);
  uninit_ac.home_categories.clear();
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(hh_tabs, uninit_ac) == 0);

  const std::string full_manifest = latter_ui::build_hardware_manifest_json();
  const std::string compact_manifest = latter_ui::build_hardware_manifest_state_json();
  LATTER_CHECK(full_manifest.find("\"slot_count\":5") != std::string::npos);
  LATTER_CHECK(full_manifest.find("\"card_slot_count\":3") != std::string::npos);
  LATTER_CHECK(full_manifest.find("\"has_rtc\":") != std::string::npos);
  LATTER_CHECK(!compact_manifest.empty() && compact_manifest.size() <= 255U);

  std::string parsed_cats[8];
  const int parsed_n = latter_ui::parse_area_categories_csv("media,light,unknown,media,all", parsed_cats, 8);
  LATTER_CHECK(parsed_n == 3 && parsed_cats[0] == "media" && parsed_cats[1] == "light" && parsed_cats[2] == "all");
  LATTER_CHECK(latter_ui::parse_area_categories_csv("", parsed_cats, 8) == 0);
  const int def_n = latter_ui::parse_area_categories_csv(latter_ui::kDefaultAreaCategories, parsed_cats, 8);
  LATTER_CHECK(def_n == 7 && parsed_cats[0] == "scene" && parsed_cats[6] == "media");

  auto &p = latter_ui::persisted_prefs();
  p = latter_ui::PersistedDevicePrefs{};
  latter_ui::device_prefs_state() = latter_ui::DevicePrefsState{};
  latter_ui::seed_default_prefs_if_needed();
  const char *expected_ww_default = (LATTER_IS_STATIONARY != 0) ? "Always On" : "Docked Only";
  const char *expected_clock_default = (LATTER_ROUND_DISPLAY != 0) ? "Modern Analog" : "Minimal Digital";
  LATTER_CHECK(std::string_view(latter_ui::pref_wake_word_mode_cstr()) == expected_ww_default);
  LATTER_CHECK(std::string_view(latter_ui::pref_clock_face_cstr()) == expected_clock_default);
  LATTER_CHECK(std::string_view(latter_ui::pref_start_screen_cstr()) == "Home");
  LATTER_CHECK(std::string_view(latter_ui::pref_screensaver_style_cstr()) == "Ambient Clock");
  LATTER_CHECK(std::string_view(latter_ui::pref_album_art_style_cstr()) == "Full Screen");
  LATTER_CHECK(std::string_view(latter_ui::pref_album_sort_order_cstr()) == "Alphabetical");
  LATTER_CHECK(std::string_view(latter_ui::pref_artist_album_sort_order_cstr()) == "Oldest on Top");
  LATTER_CHECK(std::string_view(latter_ui::pref_podcast_sort_order_cstr()) == "Newest on Top");
  LATTER_CHECK(p.prefs_seq == 0U && p.dirty_mask == 0ULL);

  const std::string seed_dirty = latter_ui::build_hello_dirty_prefs(p);
  LATTER_CHECK(seed_dirty.find(std::string("wake_word_mode=") + expected_ww_default) != std::string::npos);
  LATTER_CHECK(seed_dirty.find(std::string("clock_face=") + expected_clock_default) != std::string::npos);
  LATTER_CHECK(seed_dirty.find("screensaver_style=Ambient Clock") != std::string::npos);
  LATTER_CHECK(seed_dirty.find("ble_beacon_enabled=") == std::string::npos);

  latter_ui::reset_all_v3_rev_guards();
  std::vector<std::string> dp_tok(static_cast<size_t>(latter_ui::DevicePrefsField::COUNT), "");
  dp_tok[static_cast<size_t>(latter_ui::DevicePrefsField::PREFS_SEQ)] = "7";
  dp_tok[static_cast<size_t>(latter_ui::DevicePrefsField::WAKE_WORD_MODE)] = "Disabled";
  dp_tok[static_cast<size_t>(latter_ui::DevicePrefsField::ALERT_VOLUME)] = "65";
  dp_tok[static_cast<size_t>(latter_ui::DevicePrefsField::COLOR_PROFILE)] = "Custom";
  dp_tok[static_cast<size_t>(latter_ui::DevicePrefsField::THEME_COLOR_HEX)] = "#2288FF";
  dp_tok[static_cast<size_t>(latter_ui::DevicePrefsField::WEATHER_SIM_CONDITION)] = "Thunderstorm (Day)";

  p.dirty_mask = (1ULL << static_cast<uint8_t>(latter_ui::PrefKey::ALERT_VOLUME));
  uint32_t eff = 0;
  LATTER_CHECK(latter_ui::parse_v3_device_prefs_payload(build_v3_packet(300, 1, dp_tok), eff));
  LATTER_CHECK(p.prefs_seq == 7U);
  LATTER_CHECK(p.dirty_mask == 0ULL);
  LATTER_CHECK(p.alert_volume == 65U);
  LATTER_CHECK(std::string_view(latter_ui::pref_color_profile_cstr()) == "Custom");
  LATTER_CHECK(p.custom_theme_rgb == 0x2288FFU);
  LATTER_CHECK(std::string_view(latter_ui::pref_weather_sim_cstr()) == "Thunderstorm (Day)");
  LATTER_CHECK((eff & latter_ui::kPrefEffectThemePreset) != 0U);
  LATTER_CHECK((eff & latter_ui::kPrefEffectWakeWordSync) != 0U);
  LATTER_CHECK((eff & latter_ui::kPrefEffectRedraw) != 0U);

  MockRpcOutbox outbox{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_ui::link_state().ha_connected = true;
  uint32_t local_eff = 0;
  LATTER_CHECK(latter_actions::record_local_pref_edit(
      latter_ui::PrefKey::WAKE_WORD_MODE, "Always On", 1000U, local_eff, &outbox));
  LATTER_CHECK(std::string_view(latter_ui::pref_wake_word_mode_cstr()) == "Always On");
  LATTER_CHECK(!outbox.published.empty());
  LATTER_CHECK(outbox.published.back().find("set_device_prefs") != std::string::npos);
  LATTER_CHECK(outbox.published.back().find("wake_word_mode=Always On") != std::string::npos);

  p.dirty_mask = 0ULL;
  outbox.published.clear();
  latter_ui::link_state().ha_connected = false;
  LATTER_CHECK(latter_actions::record_local_pref_edit(
      latter_ui::PrefKey::TIME_FORMAT, "24-Hour", 2000U, local_eff, &outbox));
  LATTER_CHECK(outbox.published.empty());
  LATTER_CHECK(p.is_24h_format);
  const std::string offline_dirty = latter_ui::build_hello_dirty_prefs(p);
  LATTER_CHECK(offline_dirty == "time_format=24-Hour");

  latter_actions::timer_alarm_sync_tracker() = latter_actions::TimerAlarmSyncTracker{};
  auto &rs = latter_ui::runtime_state();
  rs.timers.running = false;
  rs.timers.paused = false;
  rs.timers.ringing = false;
  rs.alarms.ringing = false;
  latter_ui::link_state().ha_connected = true;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  outbox.published.clear();

  LATTER_CHECK(!latter_actions::check_and_emit_timer_alarm_state(5000U, false, &outbox));
  rs.timers.running = true;
  rs.timers.remaining_sec = 120;
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(5250U, false, &outbox));
  LATTER_CHECK(outbox.published.size() == 1U && outbox.published.back().find("timer_alarm_state") != std::string::npos);

  rs.timers.remaining_sec = 119;
  LATTER_CHECK(!latter_actions::check_and_emit_timer_alarm_state(6250U, false, &outbox));

  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  rs.timers.remaining_sec = 179;
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(7250U, false, &outbox));
  LATTER_CHECK(outbox.published.size() == 2U);

  p = latter_ui::PersistedDevicePrefs{};
  LATTER_CHECK(p.home_screen_enabled);
  LATTER_CHECK(std::string_view(latter_ui::pref_start_screen_cstr(p)) == "Home");
  LATTER_CHECK(std::string_view(latter_ui::pref_return_to_home_timeout_cstr(p)) == "2 min");
  LATTER_CHECK(latter_ui::return_to_home_timeout_ms(p) == 120000U);

  latter_ui::PrefMask nav_chg{};
  uint32_t nav_eff = 0;
  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      p, latter_ui::PrefKey::HOME_SCREEN_ENABLED, "0", false, nav_chg, nav_eff));
  LATTER_CHECK(!p.home_screen_enabled);
  LATTER_CHECK(std::string_view(latter_ui::pref_start_screen_cstr(p)) == "Time App");
  LATTER_CHECK(latter_ui::resolve_anchor_dock_app(p) == 10);
  LATTER_CHECK((nav_eff & latter_ui::kPrefEffectNavPolicy) != 0U);
  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      p, latter_ui::PrefKey::START_SCREEN, "Clock Face", false, nav_chg, nav_eff));
  LATTER_CHECK(latter_ui::resolve_anchor_dock_app(p) == 0);

  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      p, latter_ui::PrefKey::START_SCREEN, "Area Controls", false, nav_chg, nav_eff));
  LATTER_CHECK(latter_ui::resolve_anchor_dock_app(p) == 6);
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, false));

  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      p, latter_ui::PrefKey::HOME_SCREEN_ENABLED, "1", false, nav_chg, nav_eff));
  rs = latter_ui::DeviceRuntimeState{};
  rs.nav.gui_page = 6;
  LATTER_CHECK(latter_ui::should_return_to_home_on_idle(rs, p, false));
  rs.nav.gui_page = 1;
  rs.nav.home_subpage = 0;
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, false));
  rs.nav.gui_page = 2;
  rs.music.overlay_mode = 0;
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, true));
  LATTER_CHECK(latter_ui::should_return_to_home_on_idle(rs, p, false));
  rs.music.overlay_mode = 2;
  LATTER_CHECK(latter_ui::should_return_to_home_on_idle(rs, p, true));
  rs.nav.gui_page = 10;
  rs.nav.time_subpage = 1;
  rs.timers.running = true;
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, false));
  rs.timers.running = false;
  LATTER_CHECK(latter_ui::should_return_to_home_on_idle(rs, p, false));
  rs.nav.time_subpage = 3;
  rs.stopwatch.running = true;
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, false));
  rs.stopwatch.running = false;
  rs.nav.gui_page = 8;
  rs.nav.tools_subpage = 5;
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, false));
  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      p, latter_ui::PrefKey::RETURN_TO_HOME_TIMEOUT, "Never", false, nav_chg, nav_eff));
  rs.nav.gui_page = 6;
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, false));
}

void test_universal_shortcuts_and_pinned_app() {
  using namespace latter_ui;
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
  LATTER_CHECK(shortcuts_state().pinned_items.size() == 60U);
  LATTER_CHECK(pinned_total_pages() == 10);
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
  LATTER_CHECK(rt.nav.pinned_menu_page == 9);
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
  latter_actions::emit_shortcut_action("script.call_jordan", "turn_on", "123456789012", &outbox);
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
  reset_runtime_state();

  static_assert(kPersistedPrefsVersion == 1U, "PersistedDevicePrefs clean v3.0.0 NVS version == 1");
  static_assert(static_cast<size_t>(DevicePrefsField::TOOLS_ITEMS) == 52U);
  static_assert(static_cast<size_t>(DevicePrefsField::GAMES_ITEMS) == 53U);
  static_assert(static_cast<size_t>(DevicePrefsField::DO_NOT_DISTURB) == 70U);
  static_assert(static_cast<size_t>(DevicePrefsField::COUNT) == 71U);
  static_assert(static_cast<uint8_t>(PrefKey::TOOLS_ITEMS) == 46U);
  static_assert(static_cast<uint8_t>(PrefKey::GAMES_ITEMS) == 47U);
  static_assert(static_cast<uint8_t>(PrefKey::DO_NOT_DISTURB) == 64U);
  static_assert(static_cast<uint8_t>(PrefKey::COUNT) == 65U);

  LATTER_CHECK(std::string_view(kDefaultToolsItems) ==
               "stopwatch,level,flashlight,calculator,dice,coin_flip");
  LATTER_CHECK(std::string_view(kDefaultGamesItems) ==
               "marbles,archery,bricks,chomp,2048,memory");
  LATTER_CHECK(device_prefs_state().tools_items == kDefaultToolsItems);
  LATTER_CHECK(device_prefs_state().games_items == kDefaultGamesItems);
  LATTER_CHECK(std::string_view(device_prefs().tools_items) == kDefaultToolsItems);
  LATTER_CHECK(std::string_view(device_prefs().games_items) == kDefaultGamesItems);
  LATTER_CHECK(persisted_prefs().home_date_tap_action == 1U);
  LATTER_CHECK(std::string_view(pref_home_date_tap_cstr()) == "Overview");

  LATTER_CHECK(normalize_tools_items_csv(" coin_flip , flashlight , calendar , dice , coin_flip , bogus ") ==
               "coin_flip,flashlight,dice");
  LATTER_CHECK(normalize_tools_items_csv("") == kDefaultToolsItems);
  LATTER_CHECK(normalize_tools_items_csv("calendar,unknown") == kDefaultToolsItems);

  LATTER_CHECK(normalize_games_items_csv(" memory , 2048 , chomp , memory , invalid ") ==
               "memory,2048,chomp");
  LATTER_CHECK(normalize_games_items_csv("") == kDefaultGamesItems);
  LATTER_CHECK(normalize_games_items_csv("chess,checkers") == kDefaultGamesItems);

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
  dp_tok[static_cast<size_t>(DevicePrefsField::GAMES_ITEMS)] = "2048,marbles";
  uint32_t eff = 0U;
  LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(90, 1, dp_tok), eff, 1200U));
  LATTER_CHECK((eff & kPrefEffectNavPolicy) != 0U && (eff & kPrefEffectRedraw) != 0U);
  LATTER_CHECK(std::string_view(persisted_prefs().tools_items) == "dice,flashlight");
  LATTER_CHECK(std::string_view(persisted_prefs().games_items) == "2048,marbles");
  LATTER_CHECK(device_prefs_state().tools_items == "dice,flashlight");
  LATTER_CHECK(device_prefs_state().games_items == "2048,marbles");
  LATTER_CHECK(device_prefs_state().nvs_save_pending);

  V3ReduceContext rctx{};
  rctx.now_ms = 1200U;
  const auto red_eff = v3_domain_reducer().reduce("device_prefs", rctx);
  LATTER_CHECK(red_eff.apply_device_prefs_effects);
  LATTER_CHECK(rt.nav.game_carousel_idx == 1);
  LATTER_CHECK(rt.nav.tools_menu_page == 0);

  LATTER_CHECK(save_persisted_prefs());
  alignas(PersistedDevicePrefs) uint8_t raw_nvs[sizeof(PersistedDevicePrefs)]{};
  std::memcpy(raw_nvs, &persisted_prefs(), sizeof(PersistedDevicePrefs));
  PersistedDevicePrefs restored{};
  std::memcpy(&restored, raw_nvs, sizeof(PersistedDevicePrefs));
  LATTER_CHECK(restored.magic == kPersistedPrefsMagic && restored.version == kPersistedPrefsVersion);
  clamp_prefs_to_capabilities(restored);
  LATTER_CHECK(std::string_view(restored.tools_items) == "dice,flashlight");
  LATTER_CHECK(std::string_view(restored.games_items) == "2048,marbles");

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
    const int gc = collect_enabled_games_for("memory,bricks,chomp", g_ids);
    LATTER_CHECK(gc == 3 && g_ids[0] == 6 && g_ids[1] == 3 && g_ids[2] == 4);
    LATTER_CHECK(collect_enabled_games_for("unknown", g_ids) == 0);
  }

  reset_runtime_state();
  MockRpcOutbox sw_box{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  link_state().ha_connected = true;
  auto &rs = runtime_state();

  LATTER_CHECK(!latter_actions::check_and_emit_timer_alarm_state(1000U, false, &sw_box));
  LATTER_CHECK(sw_box.published.empty());

  rs.stopwatch.running = true;
  rs.stopwatch.start_time_ms = 1000U;
  rs.stopwatch.elapsed_ms = 0U;
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(1000U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 1U);
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_running\":\"1\"") != std::string::npos);

  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(!latter_actions::check_and_emit_timer_alarm_state(4000U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 1U);

  rs.timers.running = true;
  rs.timers.remaining_sec = 60;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(5000U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 2U);

  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  rs.timers.remaining_sec = 58;
  rs.stopwatch.start_time_ms = 1000U;
  LATTER_CHECK(!latter_actions::check_and_emit_timer_alarm_state(7000U, false, &sw_box));
  rs.stopwatch.start_time_ms = 0U;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(25000U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 3U);
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_elapsed_sec\":\"25\"") != std::string::npos);

  rs.timers.running = false;
  rs.stopwatch.running = false;
  rs.stopwatch.elapsed_ms = 27400U;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(27400U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 4U);

  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(28000U, /*force_if_active=*/true, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 5U);

  rs.stopwatch.elapsed_ms = 42000U;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(28500U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 6U);

  dirty_band() = DirtyBandTracker{};
  dirty_band().begin_frame(PageId::TIME, 3, 0U, false);
  mark_dirty_band(10, 30);
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  sw_box.published.clear();
  const auto rst_sw_eff = latter_actions::handle_device_cmd_reset_stopwatch(29000U, &sw_box);
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

  const auto can_tmr_eff = latter_actions::handle_device_cmd_cancel_timer(30000U, &sw_box);
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

  const auto dis_eff = latter_actions::handle_device_cmd_dismiss_ringing(35000U, &sw_box);
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
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::MANUAL_BRIGHTNESS, "42", false, chg, eff));
  LATTER_CHECK(p.manual_brightness == 42U && get_pref_value_str(PrefKey::MANUAL_BRIGHTNESS, p) == "42");
  LATTER_CHECK((eff & kPrefEffectBrightness) != 0U);
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::DAY_BRIGHTNESS, "150", false, chg, eff));
  LATTER_CHECK(p.day_brightness == 100U);
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::NIGHT_BRIGHTNESS, "0", false, chg, eff));
  LATTER_CHECK(p.night_brightness == 1U);
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::BEDTIME_BRIGHTNESS, "12", false, chg, eff));
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

}  // namespace

void run_state_prefs_nvs_tests() {
  test_device_prefs_v2_and_event_timer_sync();
  test_universal_shortcuts_and_pinned_app();
  test_tools_games_launcher_prefs_and_stopwatch_commands();
  test_bedtime_mode_d063();
  test_full_spectrum_brightness_d065();
  test_ambient_light_brightness_d075();
}

}  // namespace latter_test
