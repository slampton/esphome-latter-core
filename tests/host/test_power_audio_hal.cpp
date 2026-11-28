#include "test_harness.h"
#include "../../include/hal_imu_qmi8658.h"

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

struct MockAnimPoller {
  int starts{0};
  int stops{0};
  void start_poller() { ++starts; }
  void stop_poller() { ++stops; }
};

void test_battery_model_and_power_policy() {
  LATTER_CHECK(latter_core::path_mohm_for_capacity(0) == 30);
  LATTER_CHECK(latter_core::path_mohm_for_capacity(400) == 180);
  LATTER_CHECK(latter_core::path_mohm_for_capacity(500) == 150);
  LATTER_CHECK(latter_core::path_mohm_for_capacity(1000) == 90);
  LATTER_CHECK(latter_core::precharge_ma_for_capacity(0) == 50);
  LATTER_CHECK(latter_core::charge_ma_for_capacity(0) == 100);
  LATTER_CHECK(latter_core::charge_ma_for_capacity(400) == 200);
  LATTER_CHECK(latter_core::charge_ma_for_capacity(500) == 200);
  LATTER_CHECK(latter_core::charge_ma_for_capacity(1000) == 500);
  LATTER_CHECK(latter_core::charge_ma_for_capacity(2000) == 500);
  LATTER_CHECK(latter_core::axp2101_icc_code(100) == 4);
  LATTER_CHECK(latter_core::axp2101_icc_code(200) == 8);
  LATTER_CHECK(latter_core::axp2101_icc_code(500) == 11);

  {
    latter_core::BatteryModel stab;
    // AXP2101 callers pass -1 for hw_gauge_pct (uncalibrated OTP gauge stays diagnostic-only).
    stab.update(3880, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::SCREEN, 1000, -1, 500);
    const float boot_soc = stab.soc_display;
    stab.update(3891, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::QUIET, 90000, -1, 500);
    LATTER_CHECK(std::fabs(stab.soc_display - boot_soc) < 0.5f);
    stab.update(3915, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::QUIET, 120000, -1, 500);
    LATTER_CHECK(stab.soc_display <= boot_soc + 0.01f);
  }

  // Dedicated fuel-gauge IC support (0..100%) + >5000 mAh tablet battery support (up to 20000 mAh).
  {
    LATTER_CHECK(latter_core::path_mohm_for_capacity(10000) == 36);
    LATTER_CHECK(latter_core::path_mohm_for_capacity(20000) == 33);
    LATTER_CHECK(latter_core::charge_ma_for_capacity(10000) == 500);
    LATTER_CHECK(latter_core::charge_ma_for_capacity(20000) == 500);

    latter_core::BatteryModel fg_bm;
    LATTER_CHECK(!fg_bm.update(3900, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::SCREEN, 1000, 73, 10000));
    LATTER_CHECK(std::fabs(fg_bm.soc_display - 73.0f) < 0.01f);
    LATTER_CHECK(!fg_bm.update(3860, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::SCREEN, 11000, 68, 10000));
    LATTER_CHECK(std::fabs(fg_bm.soc_display - 68.0f) < 0.01f);
    // Charging CC/CV honors real fuel-gauge SoC rather than synthetic CV timer ramp.
    LATTER_CHECK(!fg_bm.update(4160, false, latter_core::ChargePhase::CV, latter_core::BattLoad::SCREEN, 21000, 91, 10000));
    LATTER_CHECK(std::fabs(fg_bm.soc_display - 91.0f) < 0.01f);
    // Low-battery notice triggers from fuel-gauge SoC on discharge.
    LATTER_CHECK(!fg_bm.update(3550, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::SCREEN, 31000, 9, 10000));
    LATTER_CHECK(fg_bm.low && fg_bm.low_notice_pending && std::fabs(fg_bm.soc_display - 9.0f) < 0.01f);

    // SoC-only fuel gauge (vbat_mv == 0) works cleanly and never falsely trips voltage cutoff.
    latter_core::BatteryModel soc_only;
    LATTER_CHECK(!soc_only.update(0, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::QUIET, 1000, 42, 8000));
    LATTER_CHECK(soc_only.has_sample && std::fabs(soc_only.soc_display - 42.0f) < 0.01f);
    LATTER_CHECK(soc_only.sample_interval_ms(true) == latter_core::BatteryModel::kSampleQuietMs);
    for (uint32_t i = 1; i <= 5; ++i) {
      LATTER_CHECK(!soc_only.update(0, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::QUIET, 1000 + i * 10000U, 40, 8000));
    }
    LATTER_CHECK(!soc_only.cutoff_tripped);
    // Out-of-range hw_gauge_pct (101) falls back to OCV model (rejecting vbat_mv == 0).
    LATTER_CHECK(!soc_only.update(0, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::QUIET, 70000, 101, 8000));
    LATTER_CHECK(std::fabs(soc_only.soc_display - 40.0f) < 0.01f);
  }

  {
    latter_core::BatteryModel reboot_bm;
    reboot_bm.seed_from_saved_soc(41, 3830);
    reboot_bm.update(3850, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::SCREEN, 1000, -1, 500);
    LATTER_CHECK(std::fabs(reboot_bm.soc_display - 41.0f) < 0.01f);

    latter_core::BatteryModel drained_bm;
    drained_bm.seed_from_saved_soc(41, 3830);
    drained_bm.update(3600, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::SCREEN, 1000, -1, 500);
    LATTER_CHECK(drained_bm.soc_display < 20.0f);

    latter_core::BatteryModel recharged_bm;
    recharged_bm.seed_from_saved_soc(41, 3830);
    recharged_bm.update(4120, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::SCREEN, 1000, -1, 500);
    LATTER_CHECK(recharged_bm.soc_display > 90.0f);
  }

  {
    latter_core::BatteryModel fault_bm;
    fault_bm.update(4064, false, latter_core::ChargePhase::DONE, latter_core::BattLoad::SCREEN, 1000, -1, 400);
    LATTER_CHECK(!fault_bm.full_latched && fault_bm.soc_display < 95.0f);

    uint32_t t_ms = 2000;
    for (int i = 0; i < 3; ++i) {
      fault_bm.note_charger_phase(true, latter_core::ChargePhase::CC, t_ms);
      t_ms += 1000;
      fault_bm.note_charger_phase(true, latter_core::ChargePhase::DONE, t_ms);
      t_ms += 3000;
    }
    LATTER_CHECK(fault_bm.fault && !fault_bm.full_latched && fault_bm.state_char() == '!');
    LATTER_CHECK(std::string(latter_core::battery_charge_state_text(false, 4, fault_bm.fault)) == "Battery Fault");

    fault_bm.update(4200, false, latter_core::ChargePhase::DONE, latter_core::BattLoad::SCREEN, t_ms, -1, 400);
    LATTER_CHECK(!fault_bm.full_latched);

    fault_bm.note_charger_phase(true, latter_core::ChargePhase::CC, t_ms + 1000);
    fault_bm.note_charger_phase(true, latter_core::ChargePhase::CC, t_ms + 32000);
    LATTER_CHECK(!fault_bm.fault);
    fault_bm.note_charger_phase(true, latter_core::ChargePhase::DONE, t_ms + 33000);
    fault_bm.update(4180, false, latter_core::ChargePhase::DONE, latter_core::BattLoad::SCREEN, t_ms + 34000, -1, 400);
    LATTER_CHECK(fault_bm.full_latched && std::fabs(fault_bm.soc_display - 100.0f) < 0.01f);
    LATTER_CHECK(fault_bm.state_char() == 'f');
    LATTER_CHECK(std::string(latter_core::battery_charge_state_text(false, 4, fault_bm.fault)) == "Full");
  }

  bool foldback_active = false;
  int active_icc_ma = 0;
  latter_core::step_thermal_foldback_policy(true, 45.0f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(!foldback_active && active_icc_ma == 500);
  latter_core::step_thermal_foldback_policy(true, 64.9f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(!foldback_active && active_icc_ma == 500);
  LATTER_CHECK(latter_core::step_thermal_foldback_policy(true, 65.0f, 500, foldback_active, active_icc_ma));
  LATTER_CHECK(foldback_active && active_icc_ma == 100);
  latter_core::step_thermal_foldback_policy(true, 60.0f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(foldback_active && active_icc_ma == 100);
  LATTER_CHECK(latter_core::step_thermal_foldback_policy(true, 58.0f, 500, foldback_active, active_icc_ma));
  LATTER_CHECK(!foldback_active && active_icc_ma == 500);
  latter_core::step_thermal_foldback_policy(true, 68.0f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(foldback_active && active_icc_ma == 100);
  latter_core::step_thermal_foldback_policy(false, 68.0f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(!foldback_active && active_icc_ma == 500);

  latter_core::BatteryModel bm;
  LATTER_CHECK(!bm.update(3280, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::QUIET, 1000, -1, 500));
  LATTER_CHECK(!bm.update(3280, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::QUIET, 11000, -1, 500));
  LATTER_CHECK(!bm.update(3280, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::QUIET, 21000, -1, 500));
  LATTER_CHECK(bm.update(3280, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::QUIET, 31000, -1, 500));
  LATTER_CHECK(bm.cutoff_tripped);
  LATTER_CHECK(!bm.update(3280, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::QUIET, 41000, -1, 500));

  // BatteryModel::update() capacity_mah vs active_chg_ma parameter order regression:
  // Passing capacity=500mAh and active_chg_ma=200mA must compute R=150mOhm (+19mV screen / +28mV audio),
  // never R=330mOhm (+42mV / +62mV) from shifting icc_ma into capacity_mah, and thermal foldback (100mA)
  // must keep R=150mOhm (-15mV CC) instead of R=630mOhm.
  {
    latter_core::BatteryModel ir_bm;
    ir_bm.update(3755, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::SCREEN, 1000, 59, 500, 200);
    LATTER_CHECK(ir_bm.vrest_mv == 3755 + (130 * 150 / 1000));
    ir_bm.update(3734, true, latter_core::ChargePhase::IDLE, latter_core::BattLoad::AUDIO, 11000, 58, 500, 200);
    LATTER_CHECK(ir_bm.vrest_mv == 3734 + (190 * 150 / 1000));
    ir_bm.update(4000, false, latter_core::ChargePhase::CC, latter_core::BattLoad::SCREEN, 21000, 58, 500, 100);
    LATTER_CHECK(ir_bm.vrest_mv == 4000 - (100 * 150 / 1000));
  }

  LATTER_CHECK(!latter_core::can_wake_for_on_battery_alarm_policy(0, 1));
  LATTER_CHECK(latter_core::can_wake_for_on_battery_alarm_policy(1, 1));
  LATTER_CHECK(latter_core::can_wake_for_on_battery_alarm_policy(0, 0));
  LATTER_CHECK(latter_core::resolve_alarm_sleep_seconds(0, 1, false, false, 3600) == -1);
  LATTER_CHECK(latter_core::resolve_alarm_sleep_seconds(0, 1, false, true, 3600) == 3600);
  LATTER_CHECK(latter_core::resolve_alarm_sleep_seconds(1, 1, true, false, 3600) == -1);

  LATTER_CHECK(!latter_core::weak_link_can_sample(4064U, 3899U));
  LATTER_CHECK(latter_core::weak_link_can_sample(8900U, 3899U));
  LATTER_CHECK(latter_core::weak_link_can_sample(1000U, 0U));

  latter_core::WeakLinkGate transient_gate{};
  LATTER_CHECK(!latter_core::weak_link_feed_rssi(-82, 10000U, transient_gate));
  LATTER_CHECK(transient_gate.below_count == 1U);
  LATTER_CHECK(!latter_core::weak_link_feed_rssi(-60, 20000U, transient_gate));
  LATTER_CHECK(transient_gate.below_count == 0U && !transient_gate.weak);

  latter_core::WeakLinkGate gate{};
  uint32_t t = 10000;
  LATTER_CHECK(!latter_core::weak_link_feed_rssi(-72, t, gate));
  t += 10000;
  LATTER_CHECK(!latter_core::weak_link_feed_rssi(-72, t, gate));
  LATTER_CHECK(!gate.weak);
  for (int i = 0; i < 6; ++i) {
    t += 10000;
    latter_core::weak_link_feed_rssi(-86, t, gate);
  }
  LATTER_CHECK(gate.weak);

  for (int i = 0; i < 5; ++i) {
    t += 10000;
    LATTER_CHECK(latter_core::weak_link_feed_rssi(-76, t, gate));
  }
  LATTER_CHECK(gate.above_since_ms == 0);

  bool saw_hold_before_clear = false;
  for (int i = 0; i < 20; ++i) {
    t += 10000;
    const bool w = latter_core::weak_link_feed_rssi(-60, t, gate);
    if (gate.above_since_ms != 0 && (t - gate.above_since_ms) < latter_core::kWeakLinkClearHoldMs) {
      LATTER_CHECK(w);
      saw_hold_before_clear = true;
    }
  }
  LATTER_CHECK(saw_hold_before_clear && !gate.weak);

  latter_core::WeakLinkGate disc_gate{};
  LATTER_CHECK(!latter_core::weak_link_feed_rssi(-84, 10000U, disc_gate));
  LATTER_CHECK(latter_core::weak_link_feed_rssi(-84, 20000U, disc_gate));
  latter_core::weak_link_note_disconnect(25000U, disc_gate);
  LATTER_CHECK(!disc_gate.weak && !disc_gate.have && disc_gate.below_count == 0U);
  LATTER_CHECK(disc_gate.weak_total_ms == 5000U);

  latter_core::last_screen_wake_ms() = 1000U;
  latter_core::last_user_touch_ms() = 1000U;
  LATTER_CHECK(latter_core::battery_motion_allowed(false, 13000U, 0U));
  LATTER_CHECK(latter_core::battery_motion_allowed(false, 35500U, 0U));
#if !LATTER_IS_STATIONARY && LATTER_HAS_BATTERY
  LATTER_CHECK(!latter_core::battery_motion_allowed(false, 36500U, 0U));
  latter_core::last_user_touch_ms() = 20000U;
  LATTER_CHECK(latter_core::battery_motion_allowed(false, 54000U, 0U));
  LATTER_CHECK(!latter_core::battery_motion_allowed(false, 55500U, 0U));
#else
  LATTER_CHECK(latter_core::battery_motion_allowed(false, 90000U, 0U));
#endif
  LATTER_CHECK(latter_core::battery_motion_allowed(true, 90000U, 0U));

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
  const std::string weather_cpp = read_repo_files({
      "../../components/latter_ui/page_weather.cpp",
      "../../components/latter_ui/weather_sky_fx.cpp",
  });

  LATTER_CHECK(gfx_cpp.find("last_ambient_anim_ms() = start_m") != std::string::npos);
  LATTER_CHECK(comp_h.find("sync_disp_poller_for_anim(") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("dispatch_cadence_20ms(") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("dispatch_cadence_screensaver_20ms(") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("au.speaker_busy()") != std::string::npos);
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
  LATTER_CHECK(pmu_cpp.find("kBattCapacityMah, tel.icc_ma") != std::string::npos);
  const std::string axp_yaml = read_repo_file("../../modules/hal/pmu_axp2101.yaml");
  LATTER_CHECK(axp_yaml.find("latter_core::kBattCapacityMah") != std::string::npos);
  const std::string va_yaml = read_repo_file("../../modules/voice_assistant.yaml");
  LATTER_CHECK(va_yaml.find("art_canvas_drop_all()") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("art_canvas_drop_all()") != std::string::npos);
  LATTER_CHECK(latter_core::kWeakLinkEnterDbm == -78);
  LATTER_CHECK(latter_core::kWeakLinkClearDbm == -74);
  LATTER_CHECK(link_h.find("LATTER_WEAK_LINK_ENTER_DBM") != std::string::npos);
  LATTER_CHECK(link_h.find("kWeakLinkClearHoldMs = 120000U") != std::string::npos);
}

void test_beacon_boot_gate_and_touch_reboot_safety() {
  LATTER_CHECK(!latter_core::beacon_boot_gate_open(false, true, 4000U, 8000U));
  LATTER_CHECK(!latter_core::beacon_boot_gate_open(true, false, 0U, 8000U));
  LATTER_CHECK(!latter_core::beacon_boot_gate_open(true, true, 4500U, 6000U));
  LATTER_CHECK(latter_core::beacon_boot_gate_open(true, true, 4500U, 6500U));
  LATTER_CHECK(latter_core::beacon_boot_gate_open(true, false, 0U, 12500U));

  MockBleCtrl ble{};
  MockWifiCtrl wifi{};
  latter_core::evaluate_radio_policy(&ble, &wifi, true, true, 0, false, false, false, 0, true, 5500U, true, 4500U);
  LATTER_CHECK(!ble.is_active() && ble.enables == 0);
  latter_core::evaluate_radio_policy(&ble, &wifi, true, true, 0, false, false, false, 0, true, 6600U, true, 4500U);
  LATTER_CHECK(ble.is_active() && ble.enables == 1);

  // First-party LatterTouchscreen recovery policy & TouchControlBridge dispatch (replaces retired hal_panel_power touch helpers).
  {
    latter_core::TouchRecoveryPolicy pol{};
    LATTER_CHECK(!pol.should_attempt(true, 1000U));
    LATTER_CHECK(pol.should_attempt(true, 2000U));
    LATTER_CHECK(pol.attempts == 1U);
    LATTER_CHECK(!pol.should_attempt(true, 3200U));
    LATTER_CHECK(pol.should_attempt(true, 4100U));
    LATTER_CHECK(pol.attempts == 2U);
    LATTER_CHECK(!pol.should_attempt(true, 7000U));
    pol.reset();
    LATTER_CHECK(pol.attempts == 0U && !pol.should_attempt(false, 2500U));

    static int s_slp = -1;
    static uint32_t s_rec_ms = 0U;
    static int s_rst = 0;
    s_slp = -1;
    s_rec_ms = 0U;
    s_rst = 0;
    latter_core::touch_control_bridge().bind(
        nullptr,
        +[](void *, bool s) { s_slp = s ? 1 : 0; },
        +[](void *, uint32_t ms) { s_rec_ms = ms; },
        +[](void *) { ++s_rst; });
    latter_core::hal_touch_set_sleep_mode(true);
    latter_core::hal_touch_recover_if_failed(2500U);
    latter_core::hal_touch_prepare_for_restart();
    LATTER_CHECK(s_slp == 1 && s_rec_ms == 2500U && s_rst == 1);
    latter_core::touch_control_bridge().reset();
  }

  const std::string touch_eng_cpp = read_repo_file("../../components/latter_ui/touch_engine.cpp");
  const std::string touch_all_cpp = read_repo_glob("../../components/latter_ui", "touch_", ".cpp");
  const std::string touch_h = read_repo_file("../../include/hal_touch.h");
  const std::string touch_hal_cpp = read_repo_file("../../components/latter_core/touch_hal.cpp");
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

  const std::string gui_touch_yaml = read_repo_file("../../guis/universal/gui_touch.yaml");
  LATTER_CHECK(touch_h.find("hal_touch_prepare_for_restart()") != std::string::npos);
  LATTER_CHECK(touch_h.find("hal_touch_recover_if_failed(") != std::string::npos);
  LATTER_CHECK(touch_h.find("hal_touch_set_sleep_mode(") != std::string::npos);
  LATTER_CHECK(touch_hal_cpp.find("is_known_focaltech_chip_id(") != std::string::npos);
  LATTER_CHECK(sys_yaml.find("hal_touch_prepare_for_restart()") != std::string::npos);
  LATTER_CHECK(sys_yaml.find("pm_wake_loop_from_isr") != std::string::npos);
  LATTER_CHECK(sys_yaml.find("flush_persisted_prefs_if_dirty()") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("hal_touch_recover_if_failed(") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("hal_touch_set_sleep_mode(") != std::string::npos);
  LATTER_CHECK(gui_touch_yaml.find("on_touch_down(") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("emit_device_telemetry(") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("/*hibernating=*/true") != std::string::npos);

  // Explicit driver-identity I2C guards, FocalTech 0x38 chip-ID filter, ImuPowerMode,
  // backlight en_inverted / custom callback, and #ifdef USE_WIFI guards.
  static_assert(latter_core::is_known_focaltech_chip_id(0x55));
  static_assert(latter_core::is_known_focaltech_chip_id(0x08));
  static_assert(latter_core::is_known_focaltech_chip_id(0x54));
  static_assert(latter_core::is_known_focaltech_chip_id(0x58));
  static_assert(latter_core::is_known_focaltech_chip_id(0x64));
  static_assert(!latter_core::is_known_focaltech_chip_id(0x00));  // PCA9554A
  static_assert(!latter_core::is_known_focaltech_chip_id(0xFF));  // PCA9554A / floating bus
  static_assert(!latter_core::is_known_focaltech_chip_id(0x18));  // AHT20 status
  static_assert(!latter_core::is_known_focaltech_chip_id(0x1C));  // AHT20 status
  static_assert(!latter_core::is_known_focaltech_chip_id(0xA3));

  static_assert(latter_core::qmi8658_ctrl7_for_power_mode(latter_core::ImuPowerMode::Off) == 0x00U);
  static_assert(latter_core::qmi8658_ctrl7_for_power_mode(latter_core::ImuPowerMode::AccelOnly) == 0x01U);
  static_assert(latter_core::qmi8658_ctrl7_for_power_mode(latter_core::ImuPowerMode::AccelAndGyro) == 0x03U);
  static_assert(latter_core::qmi8658_ctrl7_for_power_mode(latter_core::ImuPowerMode::WakeOnMotion) == 0x01U);

  static_assert(latter_core::hal_backlight_en_gpio_level(true, false) == 1);
  static_assert(latter_core::hal_backlight_en_gpio_level(false, false) == 0);
  static_assert(latter_core::hal_backlight_en_gpio_level(true, true) == 0);
  static_assert(latter_core::hal_backlight_en_gpio_level(false, true) == 1);
  LATTER_CHECK(latter_core::hal_backlight_float_to_pct(0.0f) == 0U);
  LATTER_CHECK(latter_core::hal_backlight_float_to_pct(0.004f) == 1U);
  LATTER_CHECK(latter_core::hal_backlight_float_to_pct(0.75f) == 75U);
  LATTER_CHECK(latter_core::hal_backlight_float_to_pct(1.0f) == 100U);

  {
    // Custom I2C-expander backlight path: must compile for display drivers without set_brightness().
    struct MockNoBrightnessDisplay {};
    MockNoBrightnessDisplay mock_disp{};
    static int s_last_custom_bl = -1;
    static int s_custom_bl_calls = 0;
    s_last_custom_bl = -1;
    s_custom_bl_calls = 0;
    auto &rs = latter_core::runtime_state();
    auto prev_cb = rs.hw_set_backlight_level;
    rs.hw_set_backlight_level = +[](uint8_t pct) {
      s_last_custom_bl = static_cast<int>(pct);
      ++s_custom_bl_calls;
    };
    latter_core::hal_set_display_brightness_impl<true>(&mock_disp, 0.75f);
    LATTER_CHECK(s_custom_bl_calls == 1 && s_last_custom_bl == 75);
    latter_core::hal_set_display_brightness_impl<true>(&mock_disp, 0.0f);
    LATTER_CHECK(s_custom_bl_calls == 2 && s_last_custom_bl == 0);
    rs.hw_set_backlight_level = prev_cb;
  }

  const std::string audio_cpp = read_repo_file("../../components/latter_core/audio_es8311.cpp");
  const std::string imu_cpp = read_repo_file("../../components/latter_core/imu_qmi8658.cpp");
  const std::string pmu_cpp = read_repo_file("../../components/latter_core/power_battery.cpp");
  const std::string radio_h = read_repo_file("../../include/hal_radio_policy.h");
  const std::string weak_cpp = read_repo_file("../../components/latter_core/weak_link_policy.cpp");
  const std::string b216_yaml = read_repo_file("../../boards/waveshare_s3_216.yaml");
  const std::string chip_p4_yaml = read_repo_file("../../modules/hal/chip_esp32p4.yaml");
  const std::string radio_c6_yaml = read_repo_file("../../modules/hal/radio_esp32c6_sdio.yaml");
  const std::string imu_yaml = read_repo_file("../../modules/hal/imu_qmi8658.yaml");
  const std::string pmu_yaml = read_repo_file("../../modules/hal/pmu_axp2101.yaml");
  const std::string disp_only_yaml = read_repo_file("../../modules/latter_display_only_firmware.yaml");

  LATTER_CHECK(audio_cpp.find("#if LATTER_DRV_CODEC_ES8311") != std::string::npos);
  LATTER_CHECK(audio_cpp.find("#if LATTER_DRV_IMU_QMI8658") != std::string::npos);
  LATTER_CHECK(imu_cpp.find("#if LATTER_DRV_IMU_QMI8658") != std::string::npos);
  LATTER_CHECK(pmu_cpp.find("LATTER_DRV_PMU_AXP2101") != std::string::npos);
  LATTER_CHECK(pmu_cpp.find("LATTER_DRV_RTC_PCF85063") != std::string::npos);
  LATTER_CHECK(b216_yaml.find("touch_ft5x06") == std::string::npos);
  LATTER_CHECK(radio_h.find("#ifdef USE_WIFI") != std::string::npos);
  LATTER_CHECK(weak_cpp.find("#if defined(USE_ESP32) && defined(USE_WIFI)") != std::string::npos);
  LATTER_CHECK(chip_p4_yaml.find("esp32_hosted:") == std::string::npos);
  LATTER_CHECK(radio_c6_yaml.find("esp32_hosted:") != std::string::npos);
  LATTER_CHECK(imu_yaml.find("latter_ui::") == std::string::npos);
  LATTER_CHECK(imu_yaml.find("coin_state") == std::string::npos);
  LATTER_CHECK(pmu_yaml.find("latter_ui::") == std::string::npos);
  LATTER_CHECK(pmu_yaml.find("gui_page") == std::string::npos);
  LATTER_CHECK(pwr_yaml.find("Mode::WakeOnMotion") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("0x80") == std::string::npos);
  const auto pkgs_pos = disp_only_yaml.find("packages:");
  LATTER_CHECK(pkgs_pos != std::string::npos);
  const std::string disp_only_pkgs = disp_only_yaml.substr(pkgs_pos);
  LATTER_CHECK(disp_only_pkgs.find("audio_pipeline.yaml") == std::string::npos);
  LATTER_CHECK(disp_only_pkgs.find("voice_assistant.yaml") == std::string::npos);
  LATTER_CHECK(disp_only_pkgs.find("audio_scripts.yaml") == std::string::npos);
  LATTER_CHECK(disp_only_pkgs.find("gui_touch.yaml") == std::string::npos);
}

void test_phase1_boot_wake_hibernate_and_ble_roaming() {
  static_assert(std::is_trivially_copyable_v<latter_core::HibernateSnapshot>);
  LATTER_CHECK(LATTER_CORE_REV == 2026112501L);
  LATTER_CHECK(latter_core::kHibernateSnapshotVersion == 3U);

  latter_core::reset_hibernate_snapshot_for_test();
  latter_core::reset_all_v3_rev_guards();
  latter_core::area_ctx_state() = latter_core::AreaContextState{};
  latter_core::weather_state() = latter_core::WeatherState{};
  latter_core::shortcuts_state() = latter_core::ShortcutsState{};
  latter_core::runtime_state().area = latter_core::AreaRuntimeState{};
  latter_core::persisted_prefs() = latter_core::PersistedDevicePrefs{};

  LATTER_CHECK(!latter_core::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(!latter_core::shortcuts_state().dock_configured);
  LATTER_CHECK(latter_core::weather_state().condition.empty());

  latter_core::area_ctx_state().epoch = 42U;
  latter_core::area_ctx_state().rev = 9U;
  latter_core::area_ctx_state().area_name = "Primary Suite";
  latter_core::area_ctx_state().wallpaper_url = std::string(latter_core::kDayNightSkyWallpaperUrl);
  latter_core::weather_state().epoch = 42U;
  latter_core::weather_state().rev = 5U;
  latter_core::weather_state().condition = "partlycloudy";
  latter_core::weather_state().temperature = 68.5f;
  latter_core::weather_state().sun_state = "below_horizon";

  auto &sc_save = latter_core::shortcuts_state();
  sc_save.epoch = 42U;
  sc_save.rev = 3U;
  sc_save.dock_configured = true;
  sc_save.live_received = true;
  sc_save.dock_items.push_back(latter_core::ShortcutItem{
      latter_core::ShortcutKind::ROUTE, latter_core::RouteId::APP_PINNED, "app:pinned", "", "Pinned", "", false, -1});
  sc_save.dock_items.push_back(latter_core::ShortcutItem{
      latter_core::ShortcutKind::ENTITY, latter_core::RouteId::NONE, "light.desk_lamp", "toggle",
      "Desk Lamp", "mdi:desk-lamp", true, 1});
  sc_save.pinned_items.push_back(latter_core::ShortcutItem{
      latter_core::ShortcutKind::ENTITY, latter_core::RouteId::NONE, "switch.fan", "toggle",
      "Fan", "mdi:fan", false, 0});
  LATTER_CHECK(latter_core::save_hibernate_snapshot("PST8PDT,M3.2.0,M11.1.0/2"));

  latter_core::area_ctx_state() = latter_core::AreaContextState{};
  latter_core::weather_state() = latter_core::WeatherState{};
  latter_core::shortcuts_state() = latter_core::ShortcutsState{};
  latter_core::runtime_state().area = latter_core::AreaRuntimeState{};
  latter_core::persisted_prefs() = latter_core::PersistedDevicePrefs{};
  latter_core::device_prefs_state() = latter_core::DevicePrefsState{};
  latter_core::hydrated_posix_tz().clear();

  LATTER_CHECK(latter_core::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(latter_core::hydrated_posix_tz() == "PST8PDT,M3.2.0,M11.1.0/2");
  LATTER_CHECK(latter_core::shortcuts_state().dock_configured);
  LATTER_CHECK(latter_core::shortcuts_state().dock_items.size() == 2U);
  LATTER_CHECK(latter_core::shortcuts_state().dock_items[1].target == "light.desk_lamp");
  LATTER_CHECK(latter_core::shortcuts_state().dock_items[1].confirm);
  // Additive-only cold boot (D-085): entity shortcuts unpack neutral (-1), never stale ON/OFF.
  LATTER_CHECK(latter_core::shortcuts_state().dock_items[1].state == -1);
  LATTER_CHECK(latter_core::shortcuts_state().pinned_items.size() == 1U);
  LATTER_CHECK(latter_core::shortcuts_state().pinned_items[0].state == -1);
  LATTER_CHECK(latter_core::area_ctx_state().area_name == "Primary Suite");
  LATTER_CHECK(latter_core::is_day_night_sky_wallpaper_url(latter_core::area_ctx_state().wallpaper_url));
  // Additive-only cold boot (D-085): volatile weather/temp/sun never hydrate from snapshot.
  LATTER_CHECK(latter_core::weather_state().condition.empty());
  LATTER_CHECK(std::isnan(latter_core::weather_state().temperature));
  LATTER_CHECK(latter_core::weather_state().sun_state.empty());
  LATTER_CHECK(latter_core::area_ctx_state().epoch == 0U && latter_core::area_ctx_state().rev == 0U);
  LATTER_CHECK(latter_core::weather_state().epoch == 0U && latter_core::weather_state().rev == 0U);
  LATTER_CHECK(latter_core::shortcuts_state().epoch == 0U && latter_core::shortcuts_state().rev == 0U);

  latter_core::shortcuts_state().live_received = true;
  latter_core::shortcuts_state().dock_items.clear();
  latter_core::area_ctx_state().area_name = "Office";
  latter_core::area_ctx_state().epoch = 1U;
  latter_core::area_ctx_state().rev = 1U;
  latter_core::area_ctx_state().wallpaper_url.clear();
  LATTER_CHECK(latter_core::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(latter_core::shortcuts_state().dock_items.empty());
  LATTER_CHECK(latter_core::area_ctx_state().area_name == "Office");
  LATTER_CHECK(latter_core::area_ctx_state().wallpaper_url.empty());

  latter_core::area_ctx_state() = latter_core::AreaContextState{};
  latter_core::weather_state() = latter_core::WeatherState{};
  latter_core::shortcuts_state() = latter_core::ShortcutsState{};
  LATTER_CHECK(latter_core::load_hibernate_snapshot_if_cold());

  std::vector<std::string> ac_tok(static_cast<size_t>(latter_core::AreaCtxField::COUNT), "");
  ac_tok[static_cast<size_t>(latter_core::AreaCtxField::AREA_ID)] = "kitchen";
  ac_tok[static_cast<size_t>(latter_core::AreaCtxField::AREA_NAME)] = "Kitchen";
  ac_tok[static_cast<size_t>(latter_core::AreaCtxField::COVERS_OPEN)] = "3";
  LATTER_CHECK(latter_core::parse_v3_area_ctx_payload(build_v3_packet(1, 1, ac_tok)));
  LATTER_CHECK(latter_core::area_ctx_state().area_name == "Kitchen" && latter_core::area_ctx_state().covers_open == 3);
  LATTER_CHECK(latter_core::area_ctx_state().wallpaper_url.empty());

  std::vector<std::string> wx_tok(static_cast<size_t>(latter_core::WeatherField::COUNT), "");
  wx_tok[static_cast<size_t>(latter_core::WeatherField::CONDITION)] = "rainy";
  wx_tok[static_cast<size_t>(latter_core::WeatherField::TEMPERATURE)] = "59.0";
  wx_tok[static_cast<size_t>(latter_core::WeatherField::SUN_STATE)] = "above_horizon";
  LATTER_CHECK(latter_core::parse_v3_weather_payload(build_v3_packet(1, 1, wx_tok)));
  LATTER_CHECK(latter_core::weather_state().condition == "rainy");

  latter_core::area_ctx_state().area_name = std::string(80, 'y');
  latter_core::area_ctx_state().wallpaper_url = "http://ha.local/api/latter/wallpaper/1";
  LATTER_CHECK(latter_core::save_hibernate_snapshot("EST5EDT,M3.2.0,M11.1.0/2"));
  latter_core::area_ctx_state() = latter_core::AreaContextState{};
  latter_core::weather_state() = latter_core::WeatherState{};
  LATTER_CHECK(latter_core::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(latter_core::area_ctx_state().area_name.size() == 47U);
  LATTER_CHECK(latter_core::area_ctx_state().wallpaper_url.empty());
  LATTER_CHECK(latter_core::weather_state().sun_state.empty());

  const std::string sys_core_yaml = read_repo_file("../../modules/system_core.yaml");
  LATTER_CHECK(sys_core_yaml.find("latter_core::save_hibernate_snapshot();") != std::string::npos);
  LATTER_CHECK(sys_core_yaml.find("bedtime_schedule_tracker().evaluate_hour(") != std::string::npos);

  {
    latter_core::PersistedDevicePrefs p_wall{};
    latter_core::seed_hardware_sleep_defaults_for_traits(p_wall, true, false, false, false);
    LATTER_CHECK(std::string_view(latter_core::pref_wake_word_mode_cstr(p_wall)) == "Always On");
    LATTER_CHECK(std::string_view(latter_core::pref_screensaver_mode_cstr(p_wall)) == "Never");
    LATTER_CHECK(std::string_view(latter_core::pref_ble_beacon_mode_cstr(p_wall)) == "Handheld Only");
    LATTER_CHECK(std::string_view(latter_core::pref_clock_face_cstr(p_wall)) == "Minimal Digital");

    latter_core::PersistedDevicePrefs p_pocket{};
    latter_core::seed_hardware_sleep_defaults_for_traits(p_pocket, false, false, true, true);
    LATTER_CHECK(std::string_view(latter_core::pref_wake_word_mode_cstr(p_pocket)) == "Docked Only");
    LATTER_CHECK(std::string_view(latter_core::pref_screensaver_mode_cstr(p_pocket)) == "Docked Only");
    LATTER_CHECK(std::string_view(latter_core::pref_clock_face_cstr(p_pocket)) == "Modern Analog");

    latter_core::PersistedDevicePrefs p_wear{};
    latter_core::seed_hardware_sleep_defaults_for_traits(p_wear, false, true, false, true);
    LATTER_CHECK(std::string_view(latter_core::pref_tap_to_wake_cstr(p_wear)) == "Double Tap");
    LATTER_CHECK(p_wear.day_brightness_pct() == 80U && p_wear.night_brightness_pct() == 25U);
    LATTER_CHECK(std::string_view(latter_core::pref_clock_face_cstr(p_wear)) == "Minimal Digital");

    latter_core::PersistedDevicePrefs p_hand{};
    latter_core::seed_hardware_sleep_defaults_for_traits(p_hand, false, false, false, true);
    LATTER_CHECK(std::string_view(latter_core::pref_battery_hibernate_timeout_cstr(p_hand)) == "30 min");
    LATTER_CHECK(std::string_view(latter_core::pref_clock_face_cstr(p_hand)) == "Minimal Digital");
  }

  auto &prefs = latter_core::persisted_prefs();
  prefs = latter_core::PersistedDevicePrefs{};
  latter_core::seed_hardware_sleep_defaults(prefs);
#if LATTER_HAS_BATTERY && !LATTER_IS_STATIONARY
  LATTER_CHECK(prefs.ble_beacon_mode == 1U);
  LATTER_CHECK(std::string_view(latter_core::pref_ble_beacon_mode_cstr(prefs)) == "Handheld + Screen Off");
#else
  LATTER_CHECK(prefs.ble_beacon_mode == 0U);
  LATTER_CHECK(std::string_view(latter_core::pref_ble_beacon_mode_cstr(prefs)) == "Handheld Only");
#endif

  prefs.ble_beacon_mode = 1U;
  latter_core::PrefMask chg{};
  uint32_t eff = 0U;
  LATTER_CHECK(latter_core::set_pref_by_key_str(
      prefs, latter_core::PrefKey::BLE_BEACON_MODE, "Handheld Only", chg, eff));
  LATTER_CHECK(prefs.ble_beacon_mode == 0U);
  LATTER_CHECK((chg & latter_core::pref_key_bit(latter_core::PrefKey::BLE_BEACON_MODE)).any());
  LATTER_CHECK((eff & latter_core::kPrefEffectPowerPolicy) != 0U);

  MockRpcOutbox roam_outbox{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_core::link_state().ha_connected = true;
  prefs.dirty_mask = 0ULL;
  eff = 0U;
  LATTER_CHECK(latter_core::record_local_pref_edit(
      latter_core::PrefKey::BLE_BEACON_MODE, "Handheld + Screen Off", 3000U, eff, &roam_outbox));
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

  latter_core::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 0, true, 7000U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && !ble.is_paused() && ble.is_transport_up());

  latter_core::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 0, true, 7500U, true, 4000U, true);
  LATTER_CHECK(!ble.is_active() && ble.is_paused() && ble.is_transport_up());

  latter_core::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 0, true, 8000U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && !ble.is_paused() && ble.is_transport_up());

#if LATTER_HAS_BATTERY
  latter_core::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 2, false, 9000U, true, 4000U, false);
  LATTER_CHECK(!ble.is_active() && ble.is_paused() && ble.is_transport_up());

  latter_core::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, true, false, false, 2, false, 9250U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && !ble.is_paused() && ble.is_transport_up());

  latter_core::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 2, false, 9400U, true, 4000U, false);
  LATTER_CHECK(!ble.is_active() && ble.is_paused() && ble.is_transport_up());

  latter_core::evaluate_radio_policy(
      &ble, &wifi, true, true, 1, false, false, false, 2, false, 9500U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && !ble.is_paused() && ble.is_transport_up());
#endif

  latter_core::evaluate_radio_policy(
      &ble, &wifi, true, true, 1, true, false, false, 0, true, 10250U, true, 4000U, true);
  LATTER_CHECK(!ble.is_active() && ble.is_paused());
  latter_core::evaluate_radio_policy(
      &ble, &wifi, true, false, 1, true, false, false, 0, true, 10500U, true, 4000U, false);
  LATTER_CHECK(!ble.is_active() && !ble.is_paused() && !ble.is_transport_up());

  latter_core::evaluate_radio_policy(
      &ble, &wifi, true, true, 1, false, false, false, 2, false, 11000U, true, 4000U, false);
  LATTER_CHECK(ble.is_active());
  latter_core::evaluate_radio_policy(
      &ble, &wifi, false, true, 1, false, false, false, 2, false, 11500U, false, 4000U, false);
  LATTER_CHECK(!ble.is_active() && ble.is_paused());
  latter_core::evaluate_radio_policy(
      &ble, &wifi, false, true, 1, false, true, false, 2, false, 12000U, false, 4000U, false);
  LATTER_CHECK(!ble.is_active() && !ble.is_paused() && !ble.is_transport_up());

  auto &r2w = latter_core::raise_to_wake();
  r2w.reset();
  const float thresh = latter_core::get_pickup_motion_threshold(false);
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

  latter_core::reset_hello_sync_on_connect();
  LATTER_CHECK(latter_core::should_trigger_hello_on_ping(1000U, 0ULL));
  latter_core::mark_hello_in_flight(true, 1000U);
  LATTER_CHECK(!latter_core::should_trigger_hello_on_ping(1150U, 0ULL));
  latter_core::mark_hello_sent(1300U, 0ULL);
  LATTER_CHECK(!latter_core::should_trigger_hello_on_ping(1800U, 0ULL));
  LATTER_CHECK(latter_core::should_trigger_hello_on_ping(1800U, 4ULL));
  LATTER_CHECK(latter_core::should_trigger_hello_on_ping(2900U, 0ULL));

  MockAnimPoller anim{};
  latter_core::ui_anim_poller_running() = true;
  latter_core::ensure_ui_anim_poller(&anim);
  LATTER_CHECK(anim.starts == 0 && anim.stops == 0);
  latter_core::set_ui_anim_poller(&anim, false);
  LATTER_CHECK(!latter_core::ui_anim_poller_running() && anim.stops == 1);
  latter_core::ensure_ui_anim_poller(&anim);
  LATTER_CHECK(latter_core::ui_anim_poller_running() && anim.starts == 1);
}

void test_phase4_audio_session_and_music_pause_timeout() {
  using latter_core::AudioSessionController;
  using latter_core::AudioSessionPhase;
  using latter_core::MusicPlayIntent;

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
    auto &sess = latter_core::audio_session();
    sess.reset();
    latter_core::music_queue_cleared() = false;
    latter_core::clear_remote_play_override();
    auto &ms = latter_core::media_state();
    auto &ac = latter_core::area_ctx_state();
    ms = latter_core::MediaState{};
    ms.is_local = true;
    ms.state = "playing";
    ms.title = "Track 1";
    ms.active_speaker = "media_player.local_puck";
    ac.area_id = "living_room";

    latter_core::sync_audio_session_media_observation(true, false, 11000U, ms, ac);
    LATTER_CHECK(sess.should_auto_open_music_page(1, true, 1, false, 11000U));
    latter_core::sync_audio_session_media_observation(false, false, 11400U, ms, ac);
    LATTER_CHECK(sess.is_music_visible_on_home_activity(
        false, latter_core::is_music_paused_effective(false, ms.is_local, ms.state), true, 11400U));
    ms.title = "Track 2";
    latter_core::sync_audio_session_media_observation(true, false, 11800U, ms, ac);
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
  LATTER_CHECK(timers_yaml.find("preempt_va_for_alert_now()") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("cancel_voice_assistant") == std::string::npos);
  LATTER_CHECK(comp_cpp.find("paused_music_expiry_due_ms(") != std::string::npos);
  LATTER_CHECK(touch_cpp.find("set_play_intent(MusicPlayIntent::HomeQuickToggle") != std::string::npos);
  LATTER_CHECK(home_cpp.find("is_music_visible_on_home_activity(") != std::string::npos);
  LATTER_CHECK(home_cpp.find("has_pending_home_activity_fade_settle(") != std::string::npos);
}

void test_panel_dcs_timeout_isolation_and_wake_grace() {
  using namespace latter_ui;
  using namespace latter_core;

  static_assert(kTimeoutPanelDcsId == 0x4C410010U);
  static_assert(latter_rpc::kTimeoutRpcDrainId == 0x4C410011U);
  static_assert(latter_rpc::kTimeoutRpcDrainId != kTimeoutPanelDcsId);
  static_assert(latter_rpc::kTimeoutRpcDrainId != kTimeoutAsyncDrawId);
  static_assert(latter_rpc::kTimeoutRpcDrainId != kTimeoutFbClearId);
  static_assert(latter_rpc::kTimeoutRpcDrainId != kTimeoutWeatherStepId);
  static_assert(latter_rpc::kTimeoutRpcDrainId != kTimeoutActivityRotateId);
  static_assert(latter_rpc::kTimeoutRpcDrainId != kTimeoutTouchLongPressId);
  static_assert(latter_rpc::kTimeoutRpcDrainId != kTimeoutMusicPeerBrowseId);
  static_assert(kTimeoutPanelDcsId != kTimeoutAsyncDrawId);
  static_assert(kTimeoutPanelDcsId != kTimeoutFbClearId);
  static_assert(kTimeoutPanelDcsId != kTimeoutWeatherStepId);
  static_assert(kTimeoutPanelDcsId != kTimeoutActivityRotateId);
  static_assert(kTimeoutPanelDcsId != kTimeoutTouchLongPressId);
  static_assert(kTimeoutPanelDcsId != kTimeoutMusicPeerBrowseId);
  static_assert(kTimeoutMusicPeerBrowseId != kTimeoutTouchLongPressId);

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
  const std::string batt_svc_yaml = read_repo_file("../../modules/battery_service.yaml");
  const std::string core_h = read_repo_file("../../components/latter_core/latter_core.h");
  const std::string pm_cpp = read_repo_glob("../../components/latter_core", "pm_", ".cpp");

  LATTER_CHECK(pm_yaml.find("latter_core::hal_panel_dcs_poll(&id(disp1));") != std::string::npos);
  LATTER_CHECK(batt_svc_yaml.find("latter_core::PanelDcsState::AWAKE") != std::string::npos);
  LATTER_CHECK(core_h.find("pm_poll_loop(now);") != std::string::npos);
  LATTER_CHECK(pm_cpp.find("PmWakeGraceWindow grace") != std::string::npos);
}

void test_display_lifecycle_controller() {
  using namespace latter_ui;
  using namespace latter_core;

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
  using namespace latter_core;

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

  // AudioPathArbitrator fast explicit-pause settle vs default 1200ms track-gap debounce vs 2500ms VA resume grace vs 3000ms track-advance grace
  {
    static_assert(AudioPathArbitrator::kDefaultMediaIdleDebounceMs == 1200U);
    static_assert(AudioPathArbitrator::kTrackAdvanceGraceMs == 3000U);
    static_assert(AudioPathArbitrator::kVaResumeMusicGraceMs == 2500U);
    static_assert(AudioPathArbitrator::kExplicitPauseFastIdleWindowMs == 2000U);

    audio_session().reset();
    AudioArbitratorInputs m_in{};
    m_in.media_mixer_running = false;

    arb.arm_media_pause_settle(1000U);
    arb.arm_media_idle_debounce(1100U);  // e.g. Sendspin transitions IDLE inside explicit-pause window
    act = arb.step(1120U, m_in);
    LATTER_CHECK(act.run_media_idle_finish);
    LATTER_CHECK(!arb.media_idle_debounce_active());

    // Even if pause_settle already fired once before Sendspin reported IDLE, a follow-up default
    // idle debounce inside kExplicitPauseFastIdleWindowMs stays on the fast settle path.
    arb.arm_media_pause_settle(1000U);
    LATTER_CHECK(arb.step(1020U, m_in).run_media_idle_finish);
    arb.arm_media_idle_debounce(1110U);
    LATTER_CHECK(arb.step(1120U, m_in).run_media_idle_finish);

    // Explicit non-zero delay_ms overrides the explicit-pause window (VA music resume grace = 2500 ms).
    arb.arm_media_pause_settle(1000U);
    arb.arm_media_idle_debounce(1150U, AudioPathArbitrator::kVaResumeMusicGraceMs);
    LATTER_CHECK(!arb.in_explicit_pause_window(1150U));
    LATTER_CHECK(!arb.step(3649U, m_in).run_media_idle_finish);
    LATTER_CHECK(arb.step(3650U, m_in).run_media_idle_finish);

    // After clear_explicit_pause(), natural IDLE waits kDefaultMediaIdleDebounceMs (1200 ms).
    arb.arm_media_pause_settle(1000U);
    arb.clear_explicit_pause();
    arb.arm_media_idle_debounce(2000U);
    LATTER_CHECK(!arb.step(3199U, m_in).run_media_idle_finish);
    LATTER_CHECK(arb.step(3200U, m_in).run_media_idle_finish);

    // TrackAdvanceOrSeek (next/prev/seek) waits kTrackAdvanceGraceMs (3000 ms) on natural IDLE,
    // while an explicit user pause still preempts it immediately.
    audio_session().set_play_intent(MusicPlayIntent::TrackAdvanceOrSeek, 5000U);
    arb.arm_media_idle_debounce(5800U);
    LATTER_CHECK(!arb.step(8799U, m_in).run_media_idle_finish);
    LATTER_CHECK(arb.step(8800U, m_in).run_media_idle_finish);

    audio_session().set_play_intent(MusicPlayIntent::TrackAdvanceOrSeek, 5900U);
    arb.arm_media_pause_settle(6000U);
    arb.arm_media_idle_debounce(6100U);
    LATTER_CHECK(arb.step(6120U, m_in).run_media_idle_finish);
    arb.clear_explicit_pause();

    // finish_va_tts_drain_state() clears any lingering media_idle_debounce so MWW re-arms promptly.
    arb.arm_media_idle_debounce(4000U);
    LATTER_CHECK(arb.media_idle_debounce_active());
    (void)finish_va_tts_drain_state();
    LATTER_CHECK(!arb.media_idle_debounce_active());
    arb.reset();
    audio_session().reset();
  }

  struct FakeOutbox {
    std::vector<std::string> frames;
    void publish_state(const std::string &s) { frames.push_back(s); }
  } outbox;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_rpc::bind_sensor_sink(&outbox);

  auto &ic = intercom_session();
  ic.reset();
  area_ctx_state().cameras_summary = "camera.front_door|Front Door|porch|0|";
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
  LATTER_CHECK(av_yaml.find("codecs: [FLAC, PCM]") != std::string::npos);
  LATTER_CHECK(av_yaml.find("timeout: 600ms") != std::string::npos);
  LATTER_CHECK(av_yaml.find("in_explicit_pause_window(") != std::string::npos);
  const std::string art_cpp = read_repo_file("../../components/latter_ui/ui_gfx_text_art.cpp");
  LATTER_CHECK(art_cpp.find("uni_mul == 256 && active_frame_endian_mode() == 1") != std::string::npos);
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
}

void test_profiler_timer_and_logging_policy() {
  using namespace latter_ui;
  using namespace latter_core;

  static_assert(kProfilerDefaultTimeoutMs == 24UL * 3600UL * 1000UL);

  // Clean baseline: profiler off, not expired, 0 s remaining.
  profiler_stop();
  LATTER_CHECK(!profiler_enabled());
  LATTER_CHECK(!profiler_is_expired(1000U));
  LATTER_CHECK(profiler_remaining_sec(1000U) == 0U);

  // Default 24 h timer semantics (remains enabled at expiry until caller logs [PROF:STOP] & calls profiler_stop()).
  profiler_start(1000U);
  LATTER_CHECK(profiler_enabled());
  LATTER_CHECK(!profiler_is_expired(1000U));
  LATTER_CHECK(profiler_remaining_sec(1000U) == 86400U);
  LATTER_CHECK(!profiler_is_expired(1000U + kProfilerDefaultTimeoutMs - 1U));
  LATTER_CHECK(profiler_remaining_sec(1000U + kProfilerDefaultTimeoutMs - 1U) == 1U);
  LATTER_CHECK(profiler_is_expired(1000U + kProfilerDefaultTimeoutMs));
  LATTER_CHECK(profiler_remaining_sec(1000U + kProfilerDefaultTimeoutMs) == 0U);
  LATTER_CHECK(profiler_enabled());
  profiler_stop();
  LATTER_CHECK(!profiler_enabled());
  LATTER_CHECK(!profiler_is_expired(1000U + kProfilerDefaultTimeoutMs));
  LATTER_CHECK(profiler_remaining_sec(1000U + kProfilerDefaultTimeoutMs) == 0U);

  // Custom timeout with uint32_t wrap-around across 0xFFFFFFFFU -> 0U.
  profiler_start(0xFFFFFF00U, 5000U);
  LATTER_CHECK(profiler_enabled());
  LATTER_CHECK(!profiler_is_expired(0xFFFFFF00U));
  LATTER_CHECK(profiler_remaining_sec(0xFFFFFF00U) == 5U);
  LATTER_CHECK(!profiler_is_expired(0xFFFFFFFFU));
  LATTER_CHECK(profiler_remaining_sec(0xFFFFFFFFU) == 5U);
  LATTER_CHECK(!profiler_is_expired(0U));
  LATTER_CHECK(profiler_remaining_sec(0U) == 5U);
  LATTER_CHECK(!profiler_is_expired(4743U));  // 0x100 + 4743 = 4999 ms elapsed
  LATTER_CHECK(profiler_remaining_sec(4743U) == 1U);
  LATTER_CHECK(profiler_is_expired(4744U));   // 5000 ms elapsed
  LATTER_CHECK(profiler_remaining_sec(4744U) == 0U);
  LATTER_CHECK(profiler_is_expired(10000U));

  // Starting at exactly now_ms == 0U and restarting while active to extend deadline.
  profiler_start(0U, 3000U);
  LATTER_CHECK(!profiler_is_expired(2999U));
  LATTER_CHECK(profiler_remaining_sec(2999U) == 1U);
  LATTER_CHECK(profiler_is_expired(3000U));
  profiler_start(2500U, 2000U);
  LATTER_CHECK(!profiler_is_expired(4499U));
  LATTER_CHECK(profiler_is_expired(4500U));
  profiler_stop();

  // link_report_log_line() is quiet when profiler is off while link_report_line() remains functional.
  const std::string raw_line = link_report_line(60000U, "power-on");
  LATTER_CHECK(raw_line.find("up=60") != std::string::npos);
  LATTER_CHECK(raw_line.find("rst=power-on") != std::string::npos);
  LATTER_CHECK(link_report_log_line(60000U, "power-on").empty());

  profiler_start(60000U, 10000U);
  const std::string prof_line = link_report_log_line(60000U, "power-on");
  LATTER_CHECK(prof_line.find("up=60") != std::string::npos);
  LATTER_CHECK(prof_line.find("rst=power-on") != std::string::npos);
  profiler_stop();

  // Source-level logging & battery-safe telemetry invariants.
  const std::string prof_h = read_repo_file("../../include/latter_prof.h");
  const std::string disp_only_yaml = read_repo_file("../../modules/latter_display_only_firmware.yaml");
  const std::string disp_fw_yaml = read_repo_file("../../modules/latter_display_firmware.yaml");
  const std::string sys_core_yaml = read_repo_file("../../modules/system_core.yaml");
  const std::string sys_sync_src = read_repo_files({
      "../../modules/system_ui_sync.yaml",
      "../../components/latter_core/device_sync_controllers.cpp",
  });
  const std::string pwr_yaml = read_repo_file("../../modules/power_policy.yaml");
  const std::string sleep_yaml = read_repo_file("../../modules/sleep_lifecycle.yaml");
  const std::string axp_yaml = read_repo_file("../../modules/hal/pmu_axp2101.yaml");
  const std::string batt_svc_yaml = read_repo_file("../../modules/battery_service.yaml");

  LATTER_CHECK(prof_h.find("log_heap_summary()") != std::string::npos);
  LATTER_CHECK(prof_h.find("log_memory_report") == std::string::npos);
  LATTER_CHECK(prof_h.find("[PROF:TASK]") == std::string::npos);
  LATTER_CHECK(disp_only_yaml.find("boot_log_buffer_size: 1kB") != std::string::npos);
  LATTER_CHECK(disp_only_yaml.find("PROF: INFO") == std::string::npos);
  LATTER_CHECK(disp_fw_yaml.find("!include latter_display_only_firmware.yaml") != std::string::npos);
  LATTER_CHECK(disp_fw_yaml.find("logger:") == std::string::npos);
  LATTER_CHECK(disp_fw_yaml.find("safe_mode:") == std::string::npos);
  LATTER_CHECK(sys_sync_src.find("profiler_start(") != std::string::npos);
  LATTER_CHECK(sys_sync_src.find("log_heap_summary()") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("profiler_is_expired(") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("profiler_stop()") != std::string::npos);
  LATTER_CHECK(sleep_yaml.find("Entering hibernate (reason=%s") != std::string::npos);
  LATTER_CHECK(sys_core_yaml.find("900000U") != std::string::npos);
  LATTER_CHECK(sys_core_yaml.find("delta: 2.0") != std::string::npos);
  LATTER_CHECK(batt_svc_yaml.find("delta: 0.01") != std::string::npos);
  LATTER_CHECK(axp_yaml.find("!bm.fault && latter_core::profiler_enabled()") != std::string::npos);
}

void test_compiled_camera_and_power_supervisors() {
  using namespace latter_core;

  reset_runtime_state();
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  prefs = PersistedDevicePrefs{};

  // Camera supervisor: HUD auto-hide after 6 s when not talking/touching
  rs.nav.gui_page = PageId::CAMERAS;
  rs.nav.display_power_state = 0;
  rs.cameras.tab = CamerasTab::SINGLE;
  rs.cameras.hud_active = true;
  rs.cameras.hud_timer = 1000U;
  area_ctx_state().camera_stream_url = "http://127.0.0.1/mjpeg";
  const auto c_act = camera_supervisor_controller().step_250ms(7500U, false);
  LATTER_CHECK(!rs.cameras.hud_active);
  LATTER_CHECK(c_act.mark_dirty_full && c_act.schedule_disp && c_act.sync_stream);

  // Power supervisor 1s: forgotten game exits after 3 min untouched
  rs.nav.gui_page = PageId::GAMES;
  rs.nav.active_game = 1;
  rs.nav.sleep_start_time = 1000U;
  last_screen_wake_ms() = 1000U;
  PowerSupervisor1sInputs p_in{};
  p_in.panel_brightness_on = true;
  p_in.last_touch_ms = 1000U;
  p_in.ui_focus_last_ms = 1000U;
  const auto p_act = evaluate_power_supervisor_1s(182000U, p_in);
  LATTER_CHECK(rs.nav.active_game == 0);
  LATTER_CHECK(p_act.stop_alarm_sound && p_act.note_activity);

  // Extracted audio lifecycle & 1s supervisor helpers
  reset_runtime_state();
  auto &r_au = runtime_state();
  auto &p_au = persisted_prefs();
  p_au = PersistedDevicePrefs{};
  p_au.ww_sens_okay_nabu = 2;  // Very sensitive
  p_au.ww_sens_hey_luna = 0;   // Slightly sensitive
  const auto cutoffs = resolve_wake_word_cutoffs();
  LATTER_CHECK(cutoffs.okay_nabu == 224U && cutoffs.hey_luna == 248U && cutoffs.hey_jarvis == 237U);

  p_au.wake_word_mode = 1;  // Docked Only (opts: Disabled=0, Docked Only=1, Always On=2 -> check helper with explicit strings via pref index)
  // Verify should_audio_codecs_standby & should_wake_word_run under active wake word mode
  {
    uint32_t eff = 0U;
    PrefMask cm{};
    (void)set_pref_by_key_str(p_au, PrefKey::WAKE_WORD_MODE, "Docked Only", cm, eff);
    LATTER_CHECK(!should_audio_codecs_standby(true, false));
    LATTER_CHECK(should_audio_codecs_standby(false, false));
    LATTER_CHECK(!should_audio_codecs_standby(false, true));

    AudioHwSnapshot hw{};
    hw.wifi_connected = true;
    r_au.hw_usb_connected = true;
    LATTER_CHECK(!should_wake_word_run(11000U, hw));  // <12s boot holdoff
    LATTER_CHECK(should_wake_word_run(13000U, hw));

    const auto a_init = audio_1s_supervisor().step_1s(13000U, hw);
    LATTER_CHECK(a_init.init_ww && !a_init.cancel_va_watchdog);
    const auto a_sync = audio_1s_supervisor().step_1s(14000U, hw);
    LATTER_CHECK(a_sync.sync_ww && !a_sync.cancel_va_watchdog);

    // Stuck VA watchdog (>35s idle with va_running == false)
    r_au.voice.active = true;
    LATTER_CHECK(!audio_1s_supervisor().step_1s(15000U, hw).cancel_va_watchdog);
    LATTER_CHECK(audio_1s_supervisor().step_1s(51000U, hw).cancel_va_watchdog);
    r_au.voice.active = false;
  }

  // VA session start, TTS drain finish, preempt, cancel, pause/resume & dictation
  r_au.nav.gui_page = PageId::MUSIC;
  r_au.nav.display_power_state = 2;
  p_au.wake_screen_on_assist = true;
  LATTER_CHECK(begin_va_session_state(60000U, false, true));
  LATTER_CHECK(r_au.voice.active && r_au.voice.return_page == PageId::MUSIC);
  r_au.nav.gui_page = PageId::HOME;
  LATTER_CHECK(finish_va_tts_drain_state());  // restored return_page == MUSIC -> fetch_remote_art
  LATTER_CHECK(!r_au.voice.active && r_au.nav.gui_page == PageId::MUSIC);

  r_au.voice.status_text = "Listening";
  r_au.voice.return_page = PageId::HOME;
  LATTER_CHECK(preempt_va_for_alert_state(62000U, false));
  LATTER_CHECK(r_au.voice.was_cancelled && r_au.nav.gui_page == PageId::HOME);

  r_au.voice.active = true;
  r_au.voice.return_page = PageId::WEATHER;
  cancel_va_session_state(true);
  LATTER_CHECK(!r_au.voice.active && r_au.voice.was_cancelled && r_au.nav.gui_page == PageId::WEATHER);

  // Pause & Resume music branches
  LATTER_CHECK(prepare_pause_all_music_for_va(true, 63000U));
  LATTER_CHECK(r_au.voice.resume_music_after);
  media_state().is_local = true;
  r_au.music.active_speaker_entity.clear();
  LATTER_CHECK(prepare_resume_music_after_va(false, 64000U) == ResumeMusicBranch::ResumeLocal);
  LATTER_CHECK(audio_session().last_resume_branch() == ResumeMusicBranch::ResumeLocal);
  LATTER_CHECK(prepare_resume_music_after_va(false, 65000U) == ResumeMusicBranch::IdleMute);
  r_au.alarms.ringing = true;
  LATTER_CHECK(prepare_resume_music_after_va(false, 66000U) == ResumeMusicBranch::RingingAbort);
  r_au.alarms.ringing = false;

  // Media idle finish + dictation start
  AudioHwSnapshot idle_hw{};
  LATTER_CHECK(can_finish_media_idle(idle_hw));
  r_au.nav.display_power_state = 2;
  r_au.hw_usb_connected = false;
  r_au.nav.sleep_start_time = 0U;
  (void)apply_media_idle_finish_state(67000U);
  LATTER_CHECK(r_au.nav.sleep_start_time == 67000U);
  LATTER_CHECK(!begin_dictation_session_state(true, false, 68000U));
  LATTER_CHECK(begin_dictation_session_state(true, true, 69000U) && r_au.voice.intercom_turn);
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
  test_profiler_timer_and_logging_policy();
  test_compiled_camera_and_power_supervisors();
}

}  // namespace latter_test
