#include "test_harness.h"

namespace latter_test {

namespace {

struct MockBleCtrl {
  bool active{false};
  bool paused{false};
  bool transport_up{false};
  bool failed{false};
  int enables{0};
  int pauses{0};
  int disables{0};
  bool is_active() const { return active; }
  bool is_paused() const { return paused; }
  bool is_transport_up() const { return transport_up; }
  bool is_failed() const { return failed; }
  void enable() {
    active = true;
    paused = false;
    transport_up = true;
    ++enables;
  }
  void pause() {
    active = false;
    paused = true;
    ++pauses;
  }
  void disable() {
    active = false;
    paused = false;
    transport_up = false;
    ++disables;
  }
};

struct MockWifiCtrl {};

struct MockTouchscreen {
  bool failed{true};
  int clear_err_calls{0};
  int reset_calls{0};
  int setup_calls{0};
  int disable_loop_calls{0};
  int enable_loop_calls{0};
  bool is_failed() const { return failed; }
  void status_clear_error() { ++clear_err_calls; }
  void reset_to_construction_state() { ++reset_calls; }
  void call() { ++setup_calls; }
  void disable_loop() { ++disable_loop_calls; }
  void enable_loop() { ++enable_loop_calls; }
};

struct MockAnimPoller {
  int starts{0};
  int stops{0};
  void start_poller() { ++starts; }
  void stop_poller() { ++stops; }
};

void test_battery_model_and_power_policy() {
  LATTER_CHECK(latter_ui::path_mohm_for_capacity(0) == 30);
  LATTER_CHECK(latter_ui::path_mohm_for_capacity(400) == 180);
  LATTER_CHECK(latter_ui::path_mohm_for_capacity(500) == 150);
  LATTER_CHECK(latter_ui::path_mohm_for_capacity(1000) == 90);
  LATTER_CHECK(latter_ui::precharge_ma_for_capacity(0) == 50);
  LATTER_CHECK(latter_ui::charge_ma_for_capacity(0) == 100);
  LATTER_CHECK(latter_ui::charge_ma_for_capacity(400) == 200);
  LATTER_CHECK(latter_ui::charge_ma_for_capacity(500) == 200);
  LATTER_CHECK(latter_ui::charge_ma_for_capacity(1000) == 500);
  LATTER_CHECK(latter_ui::charge_ma_for_capacity(2000) == 500);
  LATTER_CHECK(latter_ui::axp2101_icc_code(100) == 4);
  LATTER_CHECK(latter_ui::axp2101_icc_code(200) == 8);
  LATTER_CHECK(latter_ui::axp2101_icc_code(500) == 11);

  {
    latter_ui::BatteryModel stab;
    stab.update(3880, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::SCREEN, 1000, 99, 500);
    const float boot_soc = stab.soc_display;
    stab.update(3891, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::QUIET, 90000, 10, 500);
    LATTER_CHECK(std::fabs(stab.soc_display - boot_soc) < 0.5f);
    stab.update(3915, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::QUIET, 120000, 95, 500);
    LATTER_CHECK(stab.soc_display <= boot_soc + 0.01f);
  }

  {
    latter_ui::BatteryModel reboot_bm;
    reboot_bm.seed_from_saved_soc(41, 3830);
    reboot_bm.update(3850, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::SCREEN, 1000, -1, 500);
    LATTER_CHECK(std::fabs(reboot_bm.soc_display - 41.0f) < 0.01f);

    latter_ui::BatteryModel drained_bm;
    drained_bm.seed_from_saved_soc(41, 3830);
    drained_bm.update(3600, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::SCREEN, 1000, -1, 500);
    LATTER_CHECK(drained_bm.soc_display < 20.0f);

    latter_ui::BatteryModel recharged_bm;
    recharged_bm.seed_from_saved_soc(41, 3830);
    recharged_bm.update(4120, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::SCREEN, 1000, -1, 500);
    LATTER_CHECK(recharged_bm.soc_display > 90.0f);
  }

  {
    latter_ui::BatteryModel fault_bm;
    fault_bm.update(4064, false, latter_ui::ChargePhase::DONE, latter_ui::BattLoad::SCREEN, 1000, -1, 400);
    LATTER_CHECK(!fault_bm.full_latched && fault_bm.soc_display < 95.0f);

    uint32_t t_ms = 2000;
    for (int i = 0; i < 3; ++i) {
      fault_bm.note_charger_phase(true, latter_ui::ChargePhase::CC, t_ms);
      t_ms += 1000;
      fault_bm.note_charger_phase(true, latter_ui::ChargePhase::DONE, t_ms);
      t_ms += 3000;
    }
    LATTER_CHECK(fault_bm.fault && !fault_bm.full_latched && fault_bm.state_char() == '!');
    LATTER_CHECK(std::string(latter_ui::battery_charge_state_text(false, 4, fault_bm.fault)) == "Battery Fault");

    fault_bm.update(4200, false, latter_ui::ChargePhase::DONE, latter_ui::BattLoad::SCREEN, t_ms, -1, 400);
    LATTER_CHECK(!fault_bm.full_latched);

    fault_bm.note_charger_phase(true, latter_ui::ChargePhase::CC, t_ms + 1000);
    fault_bm.note_charger_phase(true, latter_ui::ChargePhase::CC, t_ms + 32000);
    LATTER_CHECK(!fault_bm.fault);
    fault_bm.note_charger_phase(true, latter_ui::ChargePhase::DONE, t_ms + 33000);
    fault_bm.update(4180, false, latter_ui::ChargePhase::DONE, latter_ui::BattLoad::SCREEN, t_ms + 34000, -1, 400);
    LATTER_CHECK(fault_bm.full_latched && std::fabs(fault_bm.soc_display - 100.0f) < 0.01f);
    LATTER_CHECK(fault_bm.state_char() == 'f');
    LATTER_CHECK(std::string(latter_ui::battery_charge_state_text(false, 4, fault_bm.fault)) == "Full");
  }

  bool foldback_active = false;
  int active_icc_ma = 0;
  latter_ui::step_thermal_foldback_policy(true, 45.0f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(!foldback_active && active_icc_ma == 500);
  latter_ui::step_thermal_foldback_policy(true, 64.9f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(!foldback_active && active_icc_ma == 500);
  LATTER_CHECK(latter_ui::step_thermal_foldback_policy(true, 65.0f, 500, foldback_active, active_icc_ma));
  LATTER_CHECK(foldback_active && active_icc_ma == 100);
  latter_ui::step_thermal_foldback_policy(true, 60.0f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(foldback_active && active_icc_ma == 100);
  LATTER_CHECK(latter_ui::step_thermal_foldback_policy(true, 58.0f, 500, foldback_active, active_icc_ma));
  LATTER_CHECK(!foldback_active && active_icc_ma == 500);
  latter_ui::step_thermal_foldback_policy(true, 68.0f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(foldback_active && active_icc_ma == 100);
  latter_ui::step_thermal_foldback_policy(false, 68.0f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(!foldback_active && active_icc_ma == 500);

  latter_ui::BatteryModel bm;
  LATTER_CHECK(!bm.update(3280, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::QUIET, 1000, -1, 500));
  LATTER_CHECK(!bm.update(3280, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::QUIET, 11000, -1, 500));
  LATTER_CHECK(!bm.update(3280, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::QUIET, 21000, -1, 500));
  LATTER_CHECK(bm.update(3280, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::QUIET, 31000, -1, 500));
  LATTER_CHECK(bm.cutoff_tripped);
  LATTER_CHECK(!bm.update(3280, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::QUIET, 41000, -1, 500));

  LATTER_CHECK(!latter_ui::can_wake_for_on_battery_alarm_policy(0, 1));
  LATTER_CHECK(latter_ui::can_wake_for_on_battery_alarm_policy(1, 1));
  LATTER_CHECK(latter_ui::can_wake_for_on_battery_alarm_policy(0, 0));
  LATTER_CHECK(latter_ui::resolve_alarm_sleep_seconds(0, 1, false, false, 3600) == -1);
  LATTER_CHECK(latter_ui::resolve_alarm_sleep_seconds(0, 1, false, true, 3600) == 3600);
  LATTER_CHECK(latter_ui::resolve_alarm_sleep_seconds(1, 1, true, false, 3600) == -1);

  LATTER_CHECK(!latter_ui::weak_link_can_sample(4064U, 3899U));
  LATTER_CHECK(latter_ui::weak_link_can_sample(8900U, 3899U));
  LATTER_CHECK(latter_ui::weak_link_can_sample(1000U, 0U));

  latter_ui::WeakLinkGate transient_gate{};
  LATTER_CHECK(!latter_ui::weak_link_feed_rssi(-82, 10000U, transient_gate));
  LATTER_CHECK(transient_gate.below_count == 1U);
  LATTER_CHECK(!latter_ui::weak_link_feed_rssi(-60, 20000U, transient_gate));
  LATTER_CHECK(transient_gate.below_count == 0U && !transient_gate.weak);

  latter_ui::WeakLinkGate gate{};
  uint32_t t = 10000;
  LATTER_CHECK(!latter_ui::weak_link_feed_rssi(-72, t, gate));
  t += 10000;
  LATTER_CHECK(!latter_ui::weak_link_feed_rssi(-72, t, gate));
  LATTER_CHECK(!gate.weak);
  for (int i = 0; i < 6; ++i) {
    t += 10000;
    latter_ui::weak_link_feed_rssi(-86, t, gate);
  }
  LATTER_CHECK(gate.weak);

  for (int i = 0; i < 5; ++i) {
    t += 10000;
    LATTER_CHECK(latter_ui::weak_link_feed_rssi(-76, t, gate));
  }
  LATTER_CHECK(gate.above_since_ms == 0);

  bool saw_hold_before_clear = false;
  for (int i = 0; i < 20; ++i) {
    t += 10000;
    const bool w = latter_ui::weak_link_feed_rssi(-60, t, gate);
    if (gate.above_since_ms != 0 && (t - gate.above_since_ms) < latter_ui::kWeakLinkClearHoldMs) {
      LATTER_CHECK(w);
      saw_hold_before_clear = true;
    }
  }
  LATTER_CHECK(saw_hold_before_clear && !gate.weak);

  latter_ui::WeakLinkGate disc_gate{};
  LATTER_CHECK(!latter_ui::weak_link_feed_rssi(-84, 10000U, disc_gate));
  LATTER_CHECK(latter_ui::weak_link_feed_rssi(-84, 20000U, disc_gate));
  latter_ui::weak_link_note_disconnect(25000U, disc_gate);
  LATTER_CHECK(!disc_gate.weak && !disc_gate.have && disc_gate.below_count == 0U);
  LATTER_CHECK(disc_gate.weak_total_ms == 5000U);

  latter_ui::last_screen_wake_ms() = 1000U;
  latter_ui::last_user_touch_ms() = 1000U;
  LATTER_CHECK(latter_ui::battery_motion_allowed(false, 13000U, 0U));
  LATTER_CHECK(latter_ui::battery_motion_allowed(false, 35500U, 0U));
#if !LATTER_IS_STATIONARY && LATTER_HAS_BATTERY
  LATTER_CHECK(!latter_ui::battery_motion_allowed(false, 36500U, 0U));
  latter_ui::last_user_touch_ms() = 20000U;
  LATTER_CHECK(latter_ui::battery_motion_allowed(false, 54000U, 0U));
  LATTER_CHECK(!latter_ui::battery_motion_allowed(false, 55500U, 0U));
#else
  LATTER_CHECK(latter_ui::battery_motion_allowed(false, 90000U, 0U));
#endif
  LATTER_CHECK(latter_ui::battery_motion_allowed(true, 90000U, 0U));

  const std::string batt_h = read_repo_file("../../include/hal_battery.h");
  const std::string pmu_cpp = read_repo_file("../../components/latter_core/power_battery.cpp");
  const std::string link_h = read_repo_file("../../include/hal_link_report.h");
  const std::string gfx_cpp = read_repo_file("../../components/latter_ui/ui_gfx.cpp");
  const std::string comp_h = read_repo_files({
      "../../include/ui_cadence.h",
      "../../include/ui_compositor.h",
      "../../include/ui_esphome_display.h",
  });
  const std::string comp_cpp = read_repo_files({
      "../../components/latter_ui/ui_compositor.cpp",
      "../../components/latter_ui/overlay_takeover.cpp",
  });
  const std::string timers_yaml = read_repo_file("../../modules/timers_and_fetchers.yaml");
  const std::string pwr_yaml = read_repo_files({
      "../../modules/power_policy.yaml",
      "../../modules/sleep_lifecycle.yaml",
  });
  const std::string weather_cpp = read_repo_file("../../components/latter_ui/page_weather.cpp");

  LATTER_CHECK(gfx_cpp.find("last_ambient_anim_ms() = start_m") != std::string::npos);
  LATTER_CHECK(comp_h.find("sync_disp_poller_for_anim(") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("dispatch_cadence_20ms(") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("dispatch_cadence_screensaver_20ms(") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("speaker_audio_busy") != std::string::npos);
  LATTER_CHECK(comp_cpp.find("app_weather_active") != std::string::npos);
  LATTER_CHECK(comp_cpp.find("mww_running") != std::string::npos);
  LATTER_CHECK(weather_cpp.find("cloud_shape_get(") != std::string::npos);
  LATTER_CHECK(weather_cpp.find("weather_cloud_cache_drop()") != std::string::npos);
  LATTER_CHECK(weather_cpp.find("last_rain_ms") == std::string::npos);
  LATTER_CHECK(weather_cpp.find("AnimeRainDrop") == std::string::npos);

  LATTER_CHECK(batt_h.find("kChargeCapMa = 500") != std::string::npos);
  LATTER_CHECK(batt_h.find("kChargeThermalFoldbackMa = 100") != std::string::npos);
  LATTER_CHECK(batt_h.find("kAxpThermalFoldbackEnterC = 65.0f") != std::string::npos);
  LATTER_CHECK(batt_h.find("kAxpThermalFoldbackClearC = 58.0f") != std::string::npos);
  LATTER_CHECK(batt_h.find("can_wake_for_on_battery_alarm()") != std::string::npos);
  LATTER_CHECK(pmu_cpp.find("step_thermal_foldback_policy") != std::string::npos);
  LATTER_CHECK(pmu_cpp.find("!can_wake_for_on_battery_alarm()") != std::string::npos);
  LATTER_CHECK(latter_ui::kWeakLinkEnterDbm == -78);
  LATTER_CHECK(latter_ui::kWeakLinkClearDbm == -74);
  LATTER_CHECK(link_h.find("LATTER_WEAK_LINK_ENTER_DBM") != std::string::npos);
  LATTER_CHECK(link_h.find("kWeakLinkClearHoldMs = 120000U") != std::string::npos);
}

void test_beacon_boot_gate_and_touch_reboot_safety() {
  LATTER_CHECK(!latter_ui::beacon_boot_gate_open(false, true, 4000U, 8000U));
  LATTER_CHECK(!latter_ui::beacon_boot_gate_open(true, false, 0U, 8000U));
  LATTER_CHECK(!latter_ui::beacon_boot_gate_open(true, true, 4500U, 6000U));
  LATTER_CHECK(latter_ui::beacon_boot_gate_open(true, true, 4500U, 6500U));
  LATTER_CHECK(latter_ui::beacon_boot_gate_open(true, false, 0U, 12500U));

  MockBleCtrl ble{};
  MockWifiCtrl wifi{};
  latter_ui::evaluate_radio_policy(&ble, &wifi, true, true, 0, false, false, false, 0, true, 5500U, true, 4500U);
  LATTER_CHECK(!ble.is_active() && ble.enables == 0);
  latter_ui::evaluate_radio_policy(&ble, &wifi, true, true, 0, false, false, false, 0, true, 6600U, true, 4500U);
  LATTER_CHECK(ble.is_active() && ble.enables == 1);

  MockTouchscreen ts{};
  MockTouchscreen *const ts_const_ptr = &ts;
  latter_ui::hal_recover_touchscreen_if_failed(&ts, 0U, true);
  latter_ui::hal_recover_touchscreen_if_failed(&ts, 1000U);
  LATTER_CHECK(ts.setup_calls == 0);
  latter_ui::hal_recover_touchscreen_if_failed(&ts, 2000U);
  LATTER_CHECK(ts.setup_calls == 1 && ts.clear_err_calls == 1 && ts.reset_calls == 1 && ts.enable_loop_calls == 1);
  latter_ui::hal_recover_touchscreen_if_failed(&ts_const_ptr, 3200U);
  LATTER_CHECK(ts.setup_calls == 1);
  latter_ui::hal_recover_touchscreen_if_failed(&ts_const_ptr, 4100U);
  LATTER_CHECK(ts.setup_calls == 2 && ts.enable_loop_calls == 2);
  latter_ui::hal_recover_touchscreen_if_failed(&ts, 7000U);
  LATTER_CHECK(ts.setup_calls == 2);

  const std::string touch_eng_cpp = read_repo_file("../../components/latter_ui/touch_engine.cpp");
  const std::string touch_all_cpp = read_repo_glob("../../components/latter_ui", "touch_", ".cpp");
  const std::string panel_h = read_repo_file("../../include/hal_panel_power.h");
  const std::string sys_yaml = read_repo_files({
      "../../modules/system_core.yaml",
      "../../modules/system_ui_sync.yaml",
  });
  const std::string pwr_yaml = read_repo_files({
      "../../modules/power_policy.yaml",
      "../../modules/sleep_lifecycle.yaml",
  });

  const auto on_up_pos = touch_eng_cpp.find("void TouchGestureEngine::on_touch_up(");
  LATTER_CHECK(on_up_pos != std::string::npos);
  LATTER_CHECK(touch_eng_cpp.substr(0, on_up_pos).find("UiHostCmd::RESTART_DEVICE") == std::string::npos);
  LATTER_CHECK(touch_eng_cpp.substr(0, on_up_pos).find("handle_tap_system_tools_(") == std::string::npos);
  LATTER_CHECK(touch_all_cpp.find("UiHostCmd::RESTART_DEVICE") != std::string::npos);

  LATTER_CHECK(panel_h.find("hal_prepare_touch_for_restart()") != std::string::npos);
  LATTER_CHECK(panel_h.find("hal_recover_touchscreen_if_failed(") != std::string::npos);
  LATTER_CHECK(sys_yaml.find("hal_prepare_touch_for_restart()") != std::string::npos);
  LATTER_CHECK(sys_yaml.find("flush_persisted_prefs_if_dirty()") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("hal_recover_touchscreen_if_failed(") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("emit_device_telemetry(") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("/*hibernating=*/true") != std::string::npos);
  LATTER_CHECK(sys_yaml.find("area_changed") != std::string::npos);
}

void test_phase1_boot_wake_hibernate_and_ble_roaming() {
  static_assert(std::is_trivially_copyable_v<latter_ui::HibernateSnapshot>);
  LATTER_CHECK(LATTER_SDK_REV == 2026112302L);
  LATTER_CHECK(latter_ui::kHibernateSnapshotVersion == 3U);

  latter_ui::reset_hibernate_snapshot_for_test();
  latter_ui::reset_all_v3_rev_guards();
  latter_ui::area_ctx_state() = latter_ui::AreaContextState{};
  latter_ui::weather_state() = latter_ui::WeatherState{};
  latter_ui::shortcuts_state() = latter_ui::ShortcutsState{};
  latter_ui::runtime_state().area = latter_ui::AreaRuntimeState{};
  latter_ui::persisted_prefs() = latter_ui::PersistedDevicePrefs{};

  LATTER_CHECK(!latter_ui::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(!latter_ui::shortcuts_state().dock_configured);
  LATTER_CHECK(latter_ui::weather_state().condition.empty());

  latter_ui::area_ctx_state().epoch = 42U;
  latter_ui::area_ctx_state().rev = 9U;
  latter_ui::area_ctx_state().area_name = "Primary Suite";
  latter_ui::weather_state().epoch = 42U;
  latter_ui::weather_state().rev = 5U;
  latter_ui::weather_state().condition = "partlycloudy";
  latter_ui::weather_state().temperature = 68.5f;
  latter_ui::weather_state().sun_state = "below_horizon";

  auto &sc_save = latter_ui::shortcuts_state();
  sc_save.epoch = 42U;
  sc_save.rev = 3U;
  sc_save.dock_configured = true;
  sc_save.live_received = true;
  sc_save.dock_items.push_back(latter_ui::ShortcutItem{
      latter_ui::ShortcutKind::ROUTE, latter_ui::RouteId::APP_PINNED, "app:pinned", "", "Pinned", "", false, -1});
  sc_save.dock_items.push_back(latter_ui::ShortcutItem{
      latter_ui::ShortcutKind::ENTITY, latter_ui::RouteId::NONE, "script.call_help", "turn_on",
      "Call Jordan", "mdi:bell-ring", true, -1});
  sc_save.pinned_items.push_back(latter_ui::ShortcutItem{
      latter_ui::ShortcutKind::ROUTE, latter_ui::RouteId::WEATHER_MOON, "weather:moon", "", "Moon Phase", "", false, -1});
  LATTER_CHECK(latter_ui::save_hibernate_snapshot("PST8PDT,M3.2.0,M11.1.0/2"));

  latter_ui::area_ctx_state() = latter_ui::AreaContextState{};
  latter_ui::weather_state() = latter_ui::WeatherState{};
  latter_ui::shortcuts_state() = latter_ui::ShortcutsState{};
  latter_ui::runtime_state().area = latter_ui::AreaRuntimeState{};
  latter_ui::persisted_prefs() = latter_ui::PersistedDevicePrefs{};
  latter_ui::device_prefs_state() = latter_ui::DevicePrefsState{};
  latter_ui::hydrated_posix_tz().clear();

  LATTER_CHECK(latter_ui::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(latter_ui::hydrated_posix_tz() == "PST8PDT,M3.2.0,M11.1.0/2");
  LATTER_CHECK(latter_ui::shortcuts_state().dock_configured);
  LATTER_CHECK(latter_ui::shortcuts_state().dock_items.size() == 2U);
  LATTER_CHECK(latter_ui::shortcuts_state().dock_items[1].target == "script.call_help");
  LATTER_CHECK(latter_ui::shortcuts_state().dock_items[1].confirm);
  LATTER_CHECK(latter_ui::shortcuts_state().pinned_items.size() == 1U);
  LATTER_CHECK(latter_ui::area_ctx_state().area_name == "Primary Suite");
  LATTER_CHECK(latter_ui::weather_state().condition == "partlycloudy");
  LATTER_CHECK(latter_ui::area_ctx_state().epoch == 0U && latter_ui::area_ctx_state().rev == 0U);
  LATTER_CHECK(latter_ui::weather_state().epoch == 0U && latter_ui::weather_state().rev == 0U);
  LATTER_CHECK(latter_ui::shortcuts_state().epoch == 0U && latter_ui::shortcuts_state().rev == 0U);

  latter_ui::shortcuts_state().live_received = true;
  latter_ui::shortcuts_state().dock_items.clear();
  latter_ui::area_ctx_state().area_name = "Office";
  latter_ui::weather_state().condition = "sunny";
  latter_ui::weather_state().temperature = 74.0f;
  latter_ui::weather_state().sun_state = "above_horizon";
  LATTER_CHECK(latter_ui::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(latter_ui::shortcuts_state().dock_items.empty());
  LATTER_CHECK(latter_ui::area_ctx_state().area_name == "Office");

  latter_ui::area_ctx_state() = latter_ui::AreaContextState{};
  latter_ui::weather_state() = latter_ui::WeatherState{};
  latter_ui::shortcuts_state() = latter_ui::ShortcutsState{};
  LATTER_CHECK(latter_ui::load_hibernate_snapshot_if_cold());

  std::vector<std::string> ac_tok(static_cast<size_t>(latter_ui::AreaCtxField::COUNT), "");
  ac_tok[static_cast<size_t>(latter_ui::AreaCtxField::AREA_ID)] = "kitchen";
  ac_tok[static_cast<size_t>(latter_ui::AreaCtxField::AREA_NAME)] = "Kitchen";
  ac_tok[static_cast<size_t>(latter_ui::AreaCtxField::COVERS_OPEN)] = "3";
  LATTER_CHECK(latter_ui::parse_v3_area_ctx_payload(build_v3_packet(1, 1, ac_tok)));
  LATTER_CHECK(latter_ui::area_ctx_state().area_name == "Kitchen" && latter_ui::area_ctx_state().covers_open == 3);

  std::vector<std::string> wx_tok(static_cast<size_t>(latter_ui::WeatherField::COUNT), "");
  wx_tok[static_cast<size_t>(latter_ui::WeatherField::CONDITION)] = "rainy";
  wx_tok[static_cast<size_t>(latter_ui::WeatherField::TEMPERATURE)] = "59.0";
  wx_tok[static_cast<size_t>(latter_ui::WeatherField::SUN_STATE)] = "above_horizon";
  LATTER_CHECK(latter_ui::parse_v3_weather_payload(build_v3_packet(1, 1, wx_tok)));
  LATTER_CHECK(latter_ui::weather_state().condition == "rainy");

  latter_ui::area_ctx_state().area_name = std::string(80, 'y');
  latter_ui::weather_state().sun_state = "above_horizon";
  LATTER_CHECK(latter_ui::save_hibernate_snapshot("EST5EDT,M3.2.0,M11.1.0/2"));
  latter_ui::area_ctx_state() = latter_ui::AreaContextState{};
  latter_ui::weather_state() = latter_ui::WeatherState{};
  LATTER_CHECK(latter_ui::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(latter_ui::area_ctx_state().area_name.size() == 47U);
  LATTER_CHECK(latter_ui::weather_state().sun_state == "above_horizon");

  {
    latter_ui::PersistedDevicePrefs p_wall{};
    latter_ui::seed_hardware_sleep_defaults_for_traits(p_wall, true, false, false, false);
    LATTER_CHECK(std::string_view(latter_ui::pref_wake_word_mode_cstr(p_wall)) == "Always On");
    LATTER_CHECK(std::string_view(latter_ui::pref_screensaver_mode_cstr(p_wall)) == "Never");
    LATTER_CHECK(std::string_view(latter_ui::pref_ble_beacon_mode_cstr(p_wall)) == "Handheld Only");
    LATTER_CHECK(std::string_view(latter_ui::pref_clock_face_cstr(p_wall)) == "Minimal Digital");

    latter_ui::PersistedDevicePrefs p_pocket{};
    latter_ui::seed_hardware_sleep_defaults_for_traits(p_pocket, false, false, true, true);
    LATTER_CHECK(std::string_view(latter_ui::pref_wake_word_mode_cstr(p_pocket)) == "Docked Only");
    LATTER_CHECK(std::string_view(latter_ui::pref_screensaver_mode_cstr(p_pocket)) == "Docked Only");
    LATTER_CHECK(std::string_view(latter_ui::pref_clock_face_cstr(p_pocket)) == "Modern Analog");

    latter_ui::PersistedDevicePrefs p_wear{};
    latter_ui::seed_hardware_sleep_defaults_for_traits(p_wear, false, true, false, true);
    LATTER_CHECK(std::string_view(latter_ui::pref_tap_to_wake_cstr(p_wear)) == "Double Tap");
    LATTER_CHECK(p_wear.day_brightness_pct() == 80U && p_wear.night_brightness_pct() == 25U);
    LATTER_CHECK(std::string_view(latter_ui::pref_clock_face_cstr(p_wear)) == "Minimal Digital");

    latter_ui::PersistedDevicePrefs p_hand{};
    latter_ui::seed_hardware_sleep_defaults_for_traits(p_hand, false, false, false, true);
    LATTER_CHECK(std::string_view(latter_ui::pref_battery_hibernate_timeout_cstr(p_hand)) == "30 min");
    LATTER_CHECK(std::string_view(latter_ui::pref_clock_face_cstr(p_hand)) == "Minimal Digital");
  }

  auto &prefs = latter_ui::persisted_prefs();
  prefs = latter_ui::PersistedDevicePrefs{};
  latter_ui::seed_hardware_sleep_defaults(prefs);
#if LATTER_HAS_BATTERY && !LATTER_IS_STATIONARY
  LATTER_CHECK(prefs.ble_beacon_mode == 1U);
  LATTER_CHECK(std::string_view(latter_ui::pref_ble_beacon_mode_cstr(prefs)) == "Handheld + Screen Off");
#else
  LATTER_CHECK(prefs.ble_beacon_mode == 0U);
  LATTER_CHECK(std::string_view(latter_ui::pref_ble_beacon_mode_cstr(prefs)) == "Handheld Only");
#endif

  prefs.ble_beacon_mode = 1U;
  latter_ui::PrefMask chg{};
  uint32_t eff = 0U;
  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      prefs, latter_ui::PrefKey::BLE_BEACON_MODE, "Handheld Only", true, chg, eff));
  LATTER_CHECK(prefs.ble_beacon_mode == 0U);
  LATTER_CHECK((chg & latter_ui::pref_key_bit(latter_ui::PrefKey::BLE_BEACON_MODE)).any());
  LATTER_CHECK((eff & latter_ui::kPrefEffectPowerPolicy) != 0U);

  MockRpcOutbox roam_outbox{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_ui::link_state().ha_connected = true;
  prefs.dirty_mask = 0ULL;
  eff = 0U;
  LATTER_CHECK(latter_actions::record_local_pref_edit(
      latter_ui::PrefKey::BLE_BEACON_MODE, "Handheld + Screen Off", 3000U, eff, &roam_outbox));
  uint32_t pump_t = 3100U;
  while (latter_rpc::has_pending_frames()) {
    latter_rpc::step_outbox_loop(pump_t);
    pump_t += 50U;
  }
  std::string combined_rpc;
  for (const auto &chunk : roam_outbox.published) combined_rpc += chunk;
  LATTER_CHECK(combined_rpc.find("ble_beacon_mode=Handheld + Screen Off") != std::string::npos);

  MockBleCtrl ble{};
  MockWifiCtrl wifi{};

  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 0, true, 7000U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && !ble.is_paused() && ble.is_transport_up());

  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 0, true, 7500U, true, 4000U, true);
  LATTER_CHECK(!ble.is_active() && ble.is_paused() && ble.is_transport_up());

  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 0, true, 8000U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && !ble.is_paused() && ble.is_transport_up());

#if LATTER_HAS_BATTERY
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 2, false, 9000U, true, 4000U, false);
  LATTER_CHECK(!ble.is_active() && ble.is_paused() && ble.is_transport_up());

  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, true, false, false, 2, false, 9250U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && !ble.is_paused() && ble.is_transport_up());

  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 2, false, 9400U, true, 4000U, false);
  LATTER_CHECK(!ble.is_active() && ble.is_paused() && ble.is_transport_up());

  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 1, false, false, false, 2, false, 9500U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && !ble.is_paused() && ble.is_transport_up());
#endif

  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 1, true, false, false, 0, true, 10250U, true, 4000U, true);
  LATTER_CHECK(!ble.is_active() && ble.is_paused());
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, false, 1, true, false, false, 0, true, 10500U, true, 4000U, false);
  LATTER_CHECK(!ble.is_active() && !ble.is_paused() && !ble.is_transport_up());

  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 1, false, false, false, 2, false, 11000U, true, 4000U, false);
  LATTER_CHECK(ble.is_active());
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, false, true, 1, false, false, false, 2, false, 11500U, false, 4000U, false);
  LATTER_CHECK(!ble.is_active() && ble.is_paused());
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, false, true, 1, false, true, false, 2, false, 12000U, false, 4000U, false);
  LATTER_CHECK(!ble.is_active() && !ble.is_paused() && !ble.is_transport_up());

  auto &r2w = latter_ui::raise_wake_detector();
  r2w.reset();
  const float thresh = latter_ui::get_pickup_motion_threshold(false);
  LATTER_CHECK(!r2w.update(0.0f, thresh, 0.0f, 0.25f, 0.95f, 10000U));
  r2w.arm_from_wom(10000U);
  LATTER_CHECK(r2w.armed);
  LATTER_CHECK(!r2w.update(0.0f, thresh, 0.95f, 0.0f, 0.15f, 10050U));
  LATTER_CHECK(!r2w.update(0.0f, thresh, 0.0f, 0.25f, 0.95f, 10100U));
  LATTER_CHECK(!r2w.update(0.0f, thresh, 0.0f, 0.25f, 0.95f, 10250U));
  LATTER_CHECK(r2w.update(0.0f, thresh, 0.0f, 0.25f, 0.95f, 10300U));
  LATTER_CHECK(!r2w.armed);
  r2w.arm_from_wom(20000U);
  LATTER_CHECK(!r2w.update(0.0f, thresh, 0.0f, 0.25f, 0.95f, 21600U));
  LATTER_CHECK(!r2w.armed);

  latter_ui::reset_hello_sync_on_connect();
  LATTER_CHECK(latter_ui::should_trigger_hello_on_ping(1000U, 0ULL));
  latter_ui::mark_hello_in_flight(true, 1000U);
  LATTER_CHECK(!latter_ui::should_trigger_hello_on_ping(1150U, 0ULL));
  latter_ui::mark_hello_sent(1300U, 0ULL);
  LATTER_CHECK(!latter_ui::should_trigger_hello_on_ping(1800U, 0ULL));
  LATTER_CHECK(latter_ui::should_trigger_hello_on_ping(1800U, 4ULL));
  LATTER_CHECK(latter_ui::should_trigger_hello_on_ping(2900U, 0ULL));

  MockAnimPoller anim{};
  latter_ui::ui_anim_poller_running() = true;
  latter_ui::ensure_ui_anim_poller(&anim);
  LATTER_CHECK(anim.starts == 0 && anim.stops == 0);
  latter_ui::set_ui_anim_poller(&anim, false);
  LATTER_CHECK(!latter_ui::ui_anim_poller_running() && anim.stops == 1);
  latter_ui::ensure_ui_anim_poller(&anim);
  LATTER_CHECK(latter_ui::ui_anim_poller_running() && anim.starts == 1);
}

void test_phase4_audio_session_and_music_pause_timeout() {
  using latter_ui::AudioSessionController;
  using latter_ui::AudioSessionPhase;
  using latter_ui::MusicPlayIntent;

  LATTER_CHECK(AudioSessionController::kRecentPausedMusicTimeoutMs == 300000U);

  AudioSessionController ctrl;
  ctrl.reset();
  LATTER_CHECK(ctrl.should_auto_open_music_page(1, true, 1, false, 1000U));
  LATTER_CHECK(ctrl.phase() == AudioSessionPhase::MusicPlaying);
  LATTER_CHECK(!ctrl.should_auto_open_music_page(1, true, 1, false, 2000U));
  LATTER_CHECK(ctrl.should_auto_open_music_page(2, true, 1, false, 2500U));

  ctrl.note_local_playback_stopped();
  LATTER_CHECK(ctrl.should_auto_open_music_page(1, true, 1, false, 3000U));

  ctrl.reset();
  ctrl.set_play_intent(MusicPlayIntent::AutoResumeAfterVa, 5000U);
  LATTER_CHECK(!ctrl.should_auto_open_music_page(1, true, 1, false, 5200U));

  ctrl.reset();
  ctrl.set_play_intent(MusicPlayIntent::HomeQuickToggle, 6000U);
  LATTER_CHECK(!ctrl.should_auto_open_music_page(1, true, 1, false, 6100U));

  ctrl.reset();
  ctrl.set_play_intent(MusicPlayIntent::TrackAdvanceOrSeek, 7000U);
  LATTER_CHECK(!ctrl.should_auto_open_music_page(1, true, 1, false, 7050U));

  ctrl.reset();
  ctrl.set_play_intent(MusicPlayIntent::HomeQuickToggle, 8000U);
  LATTER_CHECK(ctrl.should_auto_open_music_page(
      1, true, 1, false, 8000U + AudioSessionController::kPlayIntentWindowMs + 1U));

  ctrl.reset();
  LATTER_CHECK(!ctrl.should_auto_open_music_page(6, false, 6, false, 9000U));
  ctrl.reset();
  LATTER_CHECK(ctrl.should_auto_open_music_page(2, false, 2, false, 9500U));
  ctrl.reset();
  LATTER_CHECK(!ctrl.should_auto_open_music_page(1, true, 1, true, 10000U));
  ctrl.reset();
  LATTER_CHECK(!ctrl.should_auto_open_music_page(1, true, 1, false, 10100U, true));

  {
    auto &sess = latter_ui::audio_session();
    sess.reset();
    latter_ui::music_queue_cleared() = false;
    latter_ui::clear_remote_play_override();
    auto &ms = latter_ui::media_state();
    auto &ac = latter_ui::area_ctx_state();
    ms = latter_ui::MediaState{};
    ms.is_local = true;
    ms.state = "playing";
    ms.title = "Track 1";
    ms.active_speaker = "media_player.local_puck";
    ac.area_id = "living_room";

    latter_ui::sync_audio_session_media_observation(true, false, 11000U, ms, ac);
    LATTER_CHECK(sess.should_auto_open_music_page(1, true, 1, false, 11000U));
    latter_ui::sync_audio_session_media_observation(false, false, 11400U, ms, ac);
    LATTER_CHECK(sess.is_music_visible_on_home_activity(
        false, latter_ui::is_music_paused_effective(false, ms.is_local, ms.state), true, 11400U));
    ms.title = "Track 2";
    latter_ui::sync_audio_session_media_observation(true, false, 11800U, ms, ac);
    LATTER_CHECK(!sess.should_auto_open_music_page(1, true, 1, false, 11800U));
  }

  ctrl.reset();
  ctrl.note_media_state_observation(false, true, "media_player.kitchen", "kitchen", 1000U);
  LATTER_CHECK(!ctrl.is_music_visible_on_home_activity(false, true, true, 1000U));
  ctrl.note_media_state_observation(true, false, "media_player.kitchen", "kitchen", 5000U);
  LATTER_CHECK(ctrl.is_music_visible_on_home_activity(true, false, true, 5000U));
  ctrl.note_media_state_observation(false, true, "media_player.kitchen", "kitchen", 10000U);
  LATTER_CHECK(ctrl.is_music_visible_on_home_activity(false, true, true, 309999U));
  LATTER_CHECK(!ctrl.is_music_visible_on_home_activity(false, true, true, 310000U));

  ctrl.note_media_state_observation(true, false, "media_player.kitchen", "kitchen", 400000U);
  ctrl.note_media_state_observation(false, true, "media_player.kitchen", "kitchen", 405000U);
  ctrl.note_media_state_observation(false, true, "media_player.office", "office", 407000U);
  LATTER_CHECK(!ctrl.is_music_visible_on_home_activity(false, true, true, 407000U));

  ctrl.note_media_state_observation(true, false, "media_player.patio", "patio", 500000U);
  ctrl.set_paused_for_va(true, 502000U);
  ctrl.note_media_state_observation(false, false, "media_player.patio", "patio", 503000U);
  LATTER_CHECK(ctrl.is_music_visible_on_home_activity(false, false, true, 900000U));
  ctrl.set_paused_for_va(false, 505000U);
  LATTER_CHECK(ctrl.paused_music_expiry_due_ms(505000U) == 297000U);

  ctrl.clear_paused_music();
  LATTER_CHECK(!ctrl.is_music_visible_on_home_activity(false, true, true, 505100U));
  ctrl.preempt_for_alert(520000U);
  LATTER_CHECK(ctrl.phase() == AudioSessionPhase::AlertRinging);

  const std::string audio_yaml = read_repo_files({
      "../../modules/audio_pipeline.yaml",
      "../../modules/audio_scripts.yaml",
      "../../modules/voice_assistant.yaml",
  });
  const std::string timers_yaml = read_repo_file("../../modules/timers_and_fetchers.yaml");
  const std::string home_cpp = read_repo_file("../../components/latter_ui/page_home.cpp");
  const std::string touch_cpp = read_repo_glob("../../components/latter_ui", "touch_", ".cpp");
  const std::string comp_cpp = read_repo_file("../../components/latter_ui/ui_compositor.cpp");

  LATTER_CHECK(audio_yaml.find("- id: preempt_va_for_alert") != std::string::npos);
  LATTER_CHECK(audio_yaml.find("- script.execute: preempt_va_for_alert") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("id(preempt_va_for_alert).execute();") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("cancel_voice_assistant") == std::string::npos);
  LATTER_CHECK(comp_cpp.find("paused_music_expiry_due_ms(") != std::string::npos);
  LATTER_CHECK(touch_cpp.find("set_play_intent(MusicPlayIntent::HomeQuickToggle") != std::string::npos);
  LATTER_CHECK(home_cpp.find("is_music_visible_on_home_activity(") != std::string::npos);
  LATTER_CHECK(home_cpp.find("has_pending_home_activity_fade_settle(") != std::string::npos);
}

void test_panel_dcs_timeout_isolation_and_wake_grace() {
  using namespace latter_ui;

  static_assert(kTimeoutPanelDcsId == 0x4C410010U);
  static_assert(kTimeoutPanelDcsId != kTimeoutAsyncDrawId);
  static_assert(kTimeoutPanelDcsId != kTimeoutFbClearId);
  static_assert(kTimeoutPanelDcsId != kTimeoutWeatherStepId);
  static_assert(kTimeoutPanelDcsId != kTimeoutActivityRotateId);

  PanelDcsState dcs{};
  dcs.want_bri = 0U;
  auto s_off = step_panel_dcs_state(dcs, 5000U);
  LATTER_CHECK(dcs.phase == PanelDcsState::ASLEEP && s_off.send_dispoff && s_off.send_slpin);

  dcs.want_bri = 230U;
  auto s_wake = step_panel_dcs_state(dcs, 10000U);
  LATTER_CHECK(dcs.phase == PanelDcsState::WAKING && s_wake.send_slpout && s_wake.rearm_ms == 120U);

  auto s_done = step_panel_dcs_state(dcs, 10120U);
  LATTER_CHECK(dcs.phase == PanelDcsState::AWAKE && s_done.send_dispon && s_done.hw_bri == 230U);

  static_assert(kPmWakeGraceMs == 600U);
  PmWakeGraceWindow grace{};
  grace.arm(1000U);
  LATTER_CHECK(grace.active(1000U) && grace.active(1599U) && !grace.active(1600U));
  grace.arm(0xFFFFFFFFU - 599U, 600U);
  LATTER_CHECK(grace.until_ms == 1U && grace.active(0xFFFFFFFFU) && grace.active(0U) && !grace.active(1U));

  const std::string pm_yaml = read_repo_files({
      "../../modules/power_policy.yaml",
      "../../modules/sleep_lifecycle.yaml",
  });
  const std::string axp_yaml = read_repo_file("../../modules/hal/pmu_axp2101.yaml");
  const std::string core_h = read_repo_file("../../components/latter_core/latter_core.h");
  const std::string pm_cpp = read_repo_glob("../../components/latter_core", "pm_", ".cpp");

  LATTER_CHECK(pm_yaml.find("latter_ui::hal_panel_dcs_poll(&id(disp1));") != std::string::npos);
  LATTER_CHECK(axp_yaml.find("latter_ui::PanelDcsState::AWAKE") != std::string::npos);
  LATTER_CHECK(core_h.find("::latter_ui::pm_poll_loop(now);") != std::string::npos);
  LATTER_CHECK(pm_cpp.find("PmWakeGraceWindow grace") != std::string::npos);
}

void test_display_lifecycle_controller() {
  using namespace latter_ui;

  static_assert(resolve_idle_screen_timeout_ms(false, true, 0U, "15s", "30s") == 15000U);
  static_assert(resolve_idle_screen_timeout_ms(false, false, 0U, "5 min", "2 min") == 120000U);
  static_assert(resolve_battery_hibernate_timeout_ms("5 min") == 300000U);
  static_assert(resolve_battery_hibernate_timeout_ms("Disabled") == 0U);

  struct MockBridgeSink {
    uint32_t last_id{0U};
    uint32_t last_delay{0U};
    uint32_t cancelled_id{0U};
    int fired{0};
  } sink{};

  core_timeout_bridge().bind(
      &sink,
      +[](void *ctx, uint32_t id, uint32_t delay_ms, std::function<void()> &&fn) {
        auto *s = static_cast<MockBridgeSink *>(ctx);
        s->last_id = id;
        s->last_delay = delay_ms;
        if (fn) fn();
      },
      +[](void *ctx, uint32_t id) {
        static_cast<MockBridgeSink *>(ctx)->cancelled_id = id;
      });
  schedule_core_timeout(kTimeoutAsyncDrawId, 18U, [&]() { ++sink.fired; });
  LATTER_CHECK(sink.last_id == kTimeoutAsyncDrawId && sink.last_delay == 18U && sink.fired == 1);
  cancel_core_timeout(kTimeoutWeatherStepId);
  LATTER_CHECK(sink.cancelled_id == kTimeoutWeatherStepId);
  core_timeout_bridge().reset();

  auto &dl = display_lifecycle();
  dl.reset();
  int nav_pwr = 0;
  uint32_t sleep_start = 0U, standby_entered = 0U;
  bool wake_greeting = false;

  DisplaySupervisorInputs in{};
  in.has_battery = true;
  in.usb_connected = false;
  in.battery_timeout_opt = "30s";
  in.hibernate_timeout_opt = "5 min";
  in.panel_brightness_on = true;

  dl.note_activity(1000U);
  LATTER_CHECK(!dl.evaluate_1s(25000U, in, nav_pwr, sleep_start, standby_entered, wake_greeting).enter_standby_now);
  in.voice_or_announce_busy = true;
  (void)dl.evaluate_1s(31000U, in, nav_pwr, sleep_start, standby_entered, wake_greeting);
  in.voice_or_announce_busy = false;
  in.active_game_running = true;
  (void)dl.evaluate_1s(62000U, in, nav_pwr, sleep_start, standby_entered, wake_greeting);
  in.active_game_running = false;
  LATTER_CHECK(!dl.evaluate_1s(63000U, in, nav_pwr, sleep_start, standby_entered, wake_greeting).enter_standby_now);
  LATTER_CHECK(dl.evaluate_1s(64000U, in, nav_pwr, sleep_start, standby_entered, wake_greeting).enter_standby_now);

  dl.on_enter_standby_off(64000U, nav_pwr, sleep_start, standby_entered);
  in.panel_brightness_on = false;
  LATTER_CHECK(dl.evaluate_1s(364000U, in, nav_pwr, sleep_start, standby_entered, wake_greeting).start_hibernate_countdown_now);

  in.panel_brightness_on = true;
  dl.on_wake_active(1900000U, nav_pwr, sleep_start, standby_entered, true, wake_greeting);
  LATTER_CHECK(wake_greeting && standby_entered == 0U);
  dl.on_ota_begin(nav_pwr, sleep_start);
  LATTER_CHECK(dl.state == DisplayPowerState::Ota && disp_poller_held());
  dl.on_ota_end(1910000U);
  LATTER_CHECK(dl.state == DisplayPowerState::Active && !disp_poller_held());
  dl.reset();
}

void test_audio_path_and_intercom_arbitrator() {
  using namespace latter_ui;

  static_assert(match_local_media_voice_cmd("stop") == LocalVoiceMediaCmd::PauseOrStop);
  static_assert(match_local_media_voice_cmd("  Pause!  ") == LocalVoiceMediaCmd::PauseOrStop);
  static_assert(match_local_media_voice_cmd("What is the next bus stop?") == LocalVoiceMediaCmd::None);
  LATTER_CHECK(match_local_media_voice_cmd("PAUSE") == LocalVoiceMediaCmd::PauseOrStop);
  LATTER_CHECK(match_local_media_voice_cmd("stop the kitchen lights") == LocalVoiceMediaCmd::None);

  alignas(8) uint8_t raw_buf[32]{};
  std::span<uint8_t> unaligned_quiet(raw_buf + 1, 16);
  echo_meter_active().store(true, std::memory_order_relaxed);
  echo_last_loud_ms().store(777U, std::memory_order_relaxed);
  echo_meter_feed(std::span<const uint8_t>(unaligned_quiet));
  LATTER_CHECK(echo_last_loud_ms().load(std::memory_order_relaxed) == 777U);
  for (size_t i = 0; i < 8; ++i) {
    const int16_t loud = 1000;
    std::memcpy(unaligned_quiet.data() + i * sizeof(int16_t), &loud, sizeof(int16_t));
  }
  echo_meter_feed(std::span<const uint8_t>(unaligned_quiet));
  LATTER_CHECK(echo_last_loud_ms().load(std::memory_order_relaxed) == 0U);
  echo_meter_active().store(false, std::memory_order_relaxed);

  auto &arb = audio_arbitrator();
  arb.reset();
  arb.arm_echo_gate(1000U, 1500U);
  AudioArbitratorInputs in{};
  in.ann_has_buffered = false;
  in.phys_spk_has_buffered = true;
  echo_last_loud_ms().store(1350U, std::memory_order_relaxed);
  (void)arb.step(1300U, in);
  in.phys_spk_has_buffered = false;
  auto act = arb.step(1460U, in);
  LATTER_CHECK(act.mute_speaker_path && arb.echo_stage() == EchoGateStage::SettleUnmute);
  act = arb.step(1495U, in);
  LATTER_CHECK(act.unmute_mic && arb.echo_stage() == EchoGateStage::Inactive);
  arb.reset();

  struct FakeOutbox {
    std::vector<std::string> frames;
    void publish_state(const std::string &s) { frames.push_back(s); }
  } outbox;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_rpc::bind_sensor_sink(&outbox);

  auto &ic = intercom_session();
  ic.reset();
  area_ctx_state().cameras_summary = "camera.front_door|Front Door|porch|/api/latter/cam?entity_id=camera.front_door";
  cam_carousel_idx() = 0;
  ic.start_call(20000U);
  LATTER_CHECK(ic.phase() == IntercomPhase::TwoWayListening && cam_call_active());
  LATTER_CHECK(ic.begin_ptt(20200U, true));
  LATTER_CHECK(!ic.finish_ptt_stt("", 20500U));
  LATTER_CHECK(ic.begin_ptt(21000U, true));
  outbox.frames.clear();
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_rpc::bind_sensor_sink(&outbox);
  LATTER_CHECK(ic.finish_ptt_stt("Leave it on the porch", 21500U));
  LATTER_CHECK(!outbox.frames.empty() && outbox.frames.back().find("talk_message") != std::string::npos);
  ic.reset();
  arb.reset();

  const std::string av_yaml = read_repo_files({
      "../../modules/audio_pipeline.yaml",
      "../../modules/audio_scripts.yaml",
      "../../modules/voice_assistant.yaml",
  });
  LATTER_CHECK(av_yaml.find("id: va_echo_gate") == std::string::npos);
  LATTER_CHECK(av_yaml.find("id: send_doorbell_intercom_cmd") == std::string::npos);
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
}

}  // namespace

void run_power_audio_hal_tests() {
  test_battery_model_and_power_policy();
  test_beacon_boot_gate_and_touch_reboot_safety();
  test_phase1_boot_wake_hibernate_and_ble_roaming();
  test_phase4_audio_session_and_music_pause_timeout();
  test_panel_dcs_timeout_isolation_and_wake_grace();
  test_display_lifecycle_controller();
  test_audio_path_and_intercom_arbitrator();
}

}  // namespace latter_test
