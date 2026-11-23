#include "test_harness.h"

namespace latter_test {

namespace {

std::vector<latter_ui::UiHostCmd> &recorded_host_cmds() {
  static std::vector<latter_ui::UiHostCmd> s_cmds;
  return s_cmds;
}

void bind_showcase_test_runtime(latter_ui::TouchGestureEngine &eng,
                                latter_ui::UiColor &primary,
                                latter_ui::UiColor &secondary) {
  recorded_host_cmds().clear();
  auto &rt = eng.runtime();
  rt = latter_ui::UiTouchRuntime{};
  rt.panel_is_on = true;
  rt.usb_connected = true;
  rt.time_valid = true;
  rt.time_year = 2026;
  rt.time_month = 11;
  rt.time_day = 22;
  rt.time_hour = 14;
  rt.time_minute = 30;
  rt.time_day_of_week = 1;
  rt.time_epoch = 1795357800U;
  rt.theme_primary = &primary;
  rt.theme_secondary = &secondary;
  rt.exec_cmd = +[](latter_ui::UiHostCmd cmd, int, int) {
    recorded_host_cmds().push_back(cmd);
  };
}

bool has_host_cmd(latter_ui::UiHostCmd target) {
  for (const auto c : recorded_host_cmds()) {
    if (c == target) return true;
  }
  return false;
}

void seed_full_nav_fixture() {
  using namespace latter_ui;
  auto &rs = runtime_state();
  auto &ac = area_ctx_state();
  ac.epoch = 1U;
  ac.rev = 1U;
  ac.area_id = "living_area";
  ac.area_name = "Living Area";
  ac.available_domains = "scene,light,cover,fan,switch,climate,media,all";
  ac.home_categories = "alarm,intercom,thermostat,entry,vacuum";
  ac.thermostats_summary = "climate.main|Main HVAC|living_area|heat|70|68|79";
  ac.cameras_summary =
      "camera.front|Front Door|front_entry|63|lock.front\x1e"
      "camera.drive|Driveway|driveway|9|";
  ac.screensaver_photo_url = "http://127.0.0.1:8123/api/latter/photo/1?sig=test";
  rs.area.active_area_id = "living_area";
  rs.area.available_domains_csv = ac.available_domains;
  rs.remote.slot_ents[0] = "media_player.living_tv";
  rs.remote.slot_names[0] = "Living TV";
  auto &ms = media_state();
  ms.title = "Midnight City";
  ms.artist = "M83";
  ms.state = "playing";
  ms.active_speaker = "media_player.living_speaker";
}

void test_nav_all_routes_and_trait_gating() {
  using namespace latter_ui;
  reset_runtime_state();
  persisted_prefs() = PersistedDevicePrefs{};
  seed_full_nav_fixture();

  UiColor pri{}, sec{};
  auto &eng = touch_engine();
  bind_showcase_test_runtime(eng, pri, sec);
  auto &rs = runtime_state();

  // Verify every canonical kShortcutRoutes token routes cleanly when supported, and no-ops when unsupported.
  uint32_t t = 10000U;
  for (const auto &spec : kShortcutRoutes) {
    t += 50U;
    rs.nav.gui_page = PageId::HOME;
    rs.nav.home_subpage = 0;
    rs.nav.display_power_state = 2;  // Dark display so we can verify unsupported routes never wake!
    eng.runtime().panel_is_on = false;

    const std::string cmd = std::string("nav:") + spec.token;
    const auto eff = eng.execute_device_ui_command(cmd, t);
    LATTER_CHECK(eff.handled);

    const bool expected_ok = is_route_supported_on_board(spec.id);
    if (!expected_ok) {
      LATTER_CHECK(!eff.wake_display);
      LATTER_CHECK(rs.nav.gui_page == PageId::HOME);
    } else {
      LATTER_CHECK(eff.wake_display);
      if (spec.id == RouteId::SETTINGS_HOME) {
        LATTER_CHECK(rs.nav.gui_page == PageId::HOME && rs.nav.home_subpage == 1);
      } else if (spec.id == RouteId::APP_CLOCK) {
        LATTER_CHECK(rs.nav.gui_page == PageId::CLOCK);
      } else if (spec.id == RouteId::TOOLS_STOPWATCH) {
        LATTER_CHECK(LATTER_ROUND_DISPLAY != 0);
        LATTER_CHECK(rs.nav.gui_page == PageId::TOOLS && rs.nav.tools_subpage == ToolsSubpage::STOPWATCH);
      } else if (spec.id == RouteId::TOOLS_LEVEL) {
        LATTER_CHECK(LATTER_HAS_IMU != 0);
        LATTER_CHECK(rs.nav.gui_page == PageId::TOOLS && rs.nav.tools_subpage == ToolsSubpage::LEVEL);
      }
    }
  }

  // Explicitly test app:settings, nav:home, nav:app:home
  rs.nav.display_power_state = 0;
  eng.runtime().panel_is_on = true;
  {
    const auto eff = eng.execute_device_ui_command("nav:app:settings", 20000U);
    LATTER_CHECK(eff.handled);
    LATTER_CHECK(rs.nav.gui_page == PageId::SYSTEM && rs.nav.system_menu_mode == 0);
  }
  {
    const auto eff = eng.execute_device_ui_command("nav:home", 20100U);
    LATTER_CHECK(eff.handled);
    LATTER_CHECK(rs.nav.gui_page == PageId::HOME && rs.nav.home_subpage == 0);
  }
  {
    (void)eng.execute_device_ui_command("nav:weather:moon", 20200U);
    LATTER_CHECK(rs.nav.gui_page == PageId::WEATHER && rs.nav.sky_weather_subpage == 3);
    const auto eff = eng.execute_device_ui_command("nav:app:home", 20300U);
    LATTER_CHECK(eff.handled);
    LATTER_CHECK(rs.nav.gui_page == PageId::HOME && rs.nav.home_subpage == 0);
  }

  // Test nav:quick_actions clears conflicting modals (shortcut_confirm, PIN pad, VA preview, silent alerts, banner)
  {
    (void)eng.execute_device_ui_command("showcase:on", 20400U);
    (void)eng.execute_device_ui_command("showcase:va:listening", 20450U);
    rs.showcase.silent_find_my = true;
    rs.alert.active = true;
    shortcut_confirm_state().open = true;
    ui::pin_pad_state().open = true;

    const auto eff = eng.execute_device_ui_command("nav:quick_actions", 20500U);
    LATTER_CHECK(eff.handled);
    LATTER_CHECK(rs.nav.quick_actions_open);
    LATTER_CHECK(rs.showcase.va_stage == 0U);
    LATTER_CHECK(!rs.showcase.silent_find_my);
    LATTER_CHECK(!rs.alert.active);
    LATTER_CHECK(!shortcut_confirm_state().open);
    LATTER_CHECK(!ui::pin_pad_state().open);
    LATTER_CHECK(eff.dismiss_notification);
  }

  // Test nav:dock:<0..5> smoothly animates on Home and no-ops off Home
  {
    (void)eng.execute_device_ui_command("nav:home", 21000U);
    home_dock_anim().reset();
    home_dock_page() = 0;

    const auto eff = eng.execute_device_ui_command("nav:dock:1", 21100U);
    LATTER_CHECK(eff.handled);
    LATTER_CHECK(home_dock_page() == 1);
    LATTER_CHECK(home_dock_anim().is_animating());
    LATTER_CHECK(eff.ensure_anim_poller);

    // Clamping high index (dock:5 clamps to last available dock page)
    (void)eng.execute_device_ui_command("nav:dock:5", 21200U);
    LATTER_CHECK(home_dock_page() >= 1 && home_dock_page() <= 5);

    // Off Home: dock command safely no-ops
    (void)eng.execute_device_ui_command("nav:weather:forecast", 21300U);
    const int saved_dock_pg = home_dock_page();
    LATTER_CHECK(!eng.navigate_route("dock:0", 21400U));
    LATTER_CHECK(home_dock_page() == saved_dock_pg);
  }

  // Test nav:music_hud:on / off and showcase HUD auto-fade lock
  {
    (void)eng.execute_device_ui_command("showcase:on", 22000U);
    (void)eng.execute_device_ui_command("nav:app:music", 22100U);
    persisted_prefs().music_overlay_style = 1U;  // "Art + Metadata" (normally auto-fades after 6s)
    (void)eng.execute_device_ui_command("nav:music_hud:on", 22200U);
    LATTER_CHECK(rs.music.hud_active);
    LATTER_CHECK(rs.showcase.music_hud_lock);

    // Advance 12 seconds -> HUD must NOT auto-fade while music_hud_lock is held
    (void)frame_cadence().step_250ms(34200U, false, false, "", 8000U);
    LATTER_CHECK(rs.music.hud_active);

    // nav:music_hud:off clears lock and hides HUD
    (void)eng.execute_device_ui_command("nav:music_hud:off", 34300U);
    LATTER_CHECK(!rs.music.hud_active);
    LATTER_CHECK(!rs.showcase.music_hud_lock);
  }

  // Unconfigured Remote safely no-ops
  {
    rs.remote.slot_ents[0].clear();
    (void)eng.execute_device_ui_command("nav:home", 35000U);
    LATTER_CHECK(!eng.navigate_route("app:remote", 35100U));
    LATTER_CHECK(!eng.navigate_route("remote:devices", 35200U));
    LATTER_CHECK(rs.nav.gui_page == PageId::HOME);
  }
}

void test_bedtime_and_silent_nav() {
  using namespace latter_ui;
  reset_runtime_state();
  persisted_prefs() = PersistedDevicePrefs{};
  seed_full_nav_fixture();

  UiColor pri{}, sec{};
  auto &eng = touch_engine();
  bind_showcase_test_runtime(eng, pri, sec);
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();

  // 1. nav_silent never wakes sleeping/screensaver display
  rs.nav.display_power_state = 2;
  eng.runtime().panel_is_on = false;
  {
    const auto eff = eng.execute_device_ui_command("nav_silent:weather:forecast", 10000U);
    LATTER_CHECK(eff.handled);
    LATTER_CHECK(!eff.wake_display);
    LATTER_CHECK(rs.nav.gui_page == PageId::WEATHER && rs.nav.sky_weather_subpage == 1);
    LATTER_CHECK(rs.nav.display_power_state == 2);
  }

  // 2. Bedtime Mode OUTSIDE showcase: nav:* switches silently without waking bedroom display
  prefs.bedtime_active = true;
  rs.nav.display_power_state = 1;
  eng.runtime().panel_is_on = true;
  {
    const auto eff = eng.execute_device_ui_command("nav:calendar:overview", 11000U);
    LATTER_CHECK(eff.handled);
    LATTER_CHECK(!eff.wake_display);
    LATTER_CHECK(rs.nav.gui_page == PageId::CALENDAR && rs.cal.subpage == CalendarSubpage::OVERVIEW);
  }

  // 3. Bedtime Mode INSIDE showcase: nav:* DOES wake the display
  (void)eng.execute_device_ui_command("showcase:on:120", 12000U);
  rs.nav.display_power_state = 1;
  {
    const auto eff = eng.execute_device_ui_command("nav:time:timers", 12100U);
    LATTER_CHECK(eff.handled);
    LATTER_CHECK(eff.wake_display);
    LATTER_CHECK(rs.nav.gui_page == PageId::TIME && rs.nav.time_subpage == TimeSubpage::TIMERS);
  }
}

void test_showcase_lifecycle_nvs_purity_and_auto_expiry() {
  using namespace latter_ui;
  reset_runtime_state();
  persisted_prefs() = PersistedDevicePrefs{};
  auto &prefs = persisted_prefs();
  prefs.color_profile = 3U;  // Green
  prefs.clock_face = 0U;     // Minimal Digital
  prefs.screensaver_style = 0U;  // Ambient Clock
  prefs.va_visual_profile = 0U;  // Nebula
  prefs.return_to_home_timeout = 0U;  // 15s

  // Snapshot persisted NVS POD before ANY showcase commands
  PersistedDevicePrefs prefs_snapshot{};
  std::memcpy(&prefs_snapshot, &prefs, sizeof(PersistedDevicePrefs));

  UiColor pri{}, sec{};
  auto &eng = touch_engine();
  bind_showcase_test_runtime(eng, pri, sec);
  auto &rs = runtime_state();

  // Default arm duration = 900s (15 min)
  {
    const auto eff = eng.execute_device_ui_command("showcase:on", 10000U);
    LATTER_CHECK(eff.handled);
    LATTER_CHECK(rs.showcase.is_active(10000U));
    LATTER_CHECK(rs.showcase.keep_awake_dur_sec == 900U);
    LATTER_CHECK(rs.showcase.expires_at_ms == 910000U);
    LATTER_CHECK(rs.showcase.is_active(909999U));
    LATTER_CHECK(!rs.showcase.is_active(910000U));
  }

  // Clamping: 5s -> 30s, 9999s -> 3600s
  {
    (void)eng.execute_device_ui_command("showcase:on:5", 20000U);
    LATTER_CHECK(rs.showcase.keep_awake_dur_sec == 30U);
    LATTER_CHECK(rs.showcase.expires_at_ms == 50000U);

    (void)eng.execute_device_ui_command("showcase:on:9999", 20000U);
    LATTER_CHECK(rs.showcase.keep_awake_dur_sec == 3600U);
    LATTER_CHECK(rs.showcase.expires_at_ms == 3620000U);
  }

  // Idle standby, idle hibernate, and return_to_home_timeout blocked while showcase active
  {
    (void)eng.execute_device_ui_command("showcase:on:60", 30000U);
    rs.nav.gui_page = PageId::WEATHER;
    LATTER_CHECK(!should_return_to_home_on_idle(rs, prefs, false, false, false, 35000U));

    auto &dl = display_lifecycle();
    dl.reset();
    dl.note_activity(30000U);
    DisplaySupervisorInputs d_in{};
    d_in.has_battery = true;
    d_in.usb_connected = false;
    d_in.battery_timeout_opt = "10s";
    d_in.hibernate_timeout_opt = "5 min";
    d_in.showcase_active = true;
    int pwr = 0;
    uint32_t sleep_t = 0U, sb_t = 0U;
    bool greet = false;
    const auto act1 = dl.evaluate_1s(50000U, d_in, pwr, sleep_t, sb_t, greet);
    LATTER_CHECK(!act1.enter_standby_now);
    LATTER_CHECK(!act1.start_hibernate_countdown_now);

    // Even in screensaver on battery, hibernate stays blocked while showcase_active == true
    pwr = 1;
    sleep_t = 30000U;
    const auto act2 = dl.evaluate_1s(400000U, d_in, pwr, sleep_t, sb_t, greet);
    LATTER_CHECK(!act2.start_hibernate_countdown_now);
  }

  // Exercise all volatile overrides and verify zero NVS mutation + atomic showcase:off restore
  {
    device_prefs_state().weather_sim_condition = 0U;
    (void)eng.execute_device_ui_command("showcase:on:60", 40000U);
    (void)eng.execute_device_ui_command("showcase:theme:#FF8822", 40100U);
    LATTER_CHECK(pri.r == 0xFF && pri.g == 0x88 && pri.b == 0x22);
    (void)eng.execute_device_ui_command("showcase:brightness:55", 40200U);
    (void)eng.execute_device_ui_command("showcase:weather:Rainy (Day)", 40300U);
    LATTER_CHECK(rs.showcase.weather_sim_override == 9);
    LATTER_CHECK(device_prefs_state().weather_sim_condition == 0U);
    LATTER_CHECK(std::string_view(effective_weather_sim_cstr(device_prefs_state(), rs, 40300U)) == "Rainy (Day)");
    (void)eng.execute_device_ui_command("showcase:clock:Modern Analog", 40400U);
    LATTER_CHECK(rs.nav.gui_page == PageId::CLOCK);
    LATTER_CHECK(std::string_view(effective_clock_face_cstr(prefs, rs, 40400U)) == "Modern Analog");
    (void)eng.execute_device_ui_command("showcase:timer:45", 40500U);
    LATTER_CHECK(rs.timers.running && rs.showcase.timer_owned);
    (void)eng.execute_device_ui_command("showcase:stopwatch:start", 40600U);
    LATTER_CHECK(rs.stopwatch.running && rs.showcase.stopwatch_owned);
    (void)eng.execute_device_ui_command("showcase:va:cycle:Solaris", 40700U);
    LATTER_CHECK(!rs.voice.active && rs.voice.status_text.empty());

    // PersistedDevicePrefs MUST be byte-identical to pre-showcase snapshot!
    LATTER_CHECK(std::memcmp(&prefs_snapshot, &prefs, sizeof(PersistedDevicePrefs)) == 0);

    // showcase:off atomically restores everything
    const auto off_eff = eng.execute_device_ui_command("showcase:off", 41000U);
    LATTER_CHECK(off_eff.handled && off_eff.apply_theme && off_eff.sync_brightness && off_eff.apply_power);
    LATTER_CHECK(!rs.showcase.active);
    LATTER_CHECK(rs.showcase.brightness_override_pct == 0U);
    LATTER_CHECK(rs.showcase.va_stage == 0U);
    LATTER_CHECK(rs.showcase.clock_face_override == -1);
    LATTER_CHECK(rs.showcase.weather_sim_override == -1);
    LATTER_CHECK(std::string_view(effective_weather_sim_cstr(device_prefs_state(), rs, 41000U)) == "Live (Home Assistant)");
    LATTER_CHECK(!rs.timers.running && !timer_slots()[0].running);
    LATTER_CHECK(!rs.stopwatch.running && rs.stopwatch.elapsed_ms == 0U);
    // Green preset restored: (66, 116, 83)
    LATTER_CHECK(pri.r == 66 && pri.g == 116 && pri.b == 83);
    LATTER_CHECK(std::memcmp(&prefs_snapshot, &prefs, sizeof(PersistedDevicePrefs)) == 0);
  }

  // Auto-expiry at 15 min (default 900s) via tick_showcase
  {
    (void)eng.execute_device_ui_command("showcase:theme:Pink", 50000U);  // Auto-arms 900s -> expires at 950000
    LATTER_CHECK(rs.showcase.active && rs.showcase.expires_at_ms == 950000U);
    LATTER_CHECK(pri.r == 168 && pri.g == 120 && pri.b == 132);

    const auto mid_eff = eng.tick_showcase(949000U);
    LATTER_CHECK(!mid_eff.handled);
    LATTER_CHECK(rs.showcase.active);

    const auto exp_eff = eng.tick_showcase(950000U);
    LATTER_CHECK(exp_eff.handled && exp_eff.apply_theme && exp_eff.sync_brightness && exp_eff.apply_power);
    LATTER_CHECK(!rs.showcase.active);
    LATTER_CHECK(pri.r == 66 && pri.g == 116 && pri.b == 83);
  }
}

void test_showcase_va_cycle_and_rendering() {
  using namespace latter_ui;
  reset_runtime_state();
  persisted_prefs() = PersistedDevicePrefs{};

  UiColor pri{}, sec{};
  auto &eng = touch_engine();
  bind_showcase_test_runtime(eng, pri, sec);
  auto &rs = runtime_state();

  const auto eff = eng.execute_device_ui_command("showcase:va:cycle:dot wave", 10000U);
  LATTER_CHECK(eff.handled && eff.ensure_anim_poller);
  // Zero real voice session mutation!
  LATTER_CHECK(!rs.voice.active);
  LATTER_CHECK(rs.voice.status_text.empty());
  LATTER_CHECK(!rs.voice.tts_received);
  LATTER_CHECK(!rs.voice.vad_active);

  HostSurfaceFixture fx{};
  auto check_phase = [&](uint32_t dt_ms, bool exp_vad, bool exp_tts, std::string_view exp_status) {
    fx.fill(0U);
    fx.ctx.now_ms = 10000U + dt_ms;
    fx.ctx.va_visual_profile_opt = effective_va_visual_profile_cstr(persisted_prefs(), rs, fx.ctx.now_ms);
    const auto st = SystemTakeoverState::from_context(fx.ctx);
    LATTER_CHECK(st.va_active);
    LATTER_CHECK(st.va_vad_active == exp_vad);
    LATTER_CHECK(st.va_tts_received == exp_tts);
    LATTER_CHECK(st.va_status_text == exp_status);
    LATTER_CHECK(st.va_visual_profile_opt == "Dot Wave");
    LATTER_CHECK(render_system_takeovers(fx.surf, fx.ctx, st));
  };

  // 0..1800ms Listening -> 1800..3800ms VAD -> 3800..5600ms Thinking -> 5600..8400ms Responding -> wrap!
  check_phase(0U, false, false, "Listening...");
  check_phase(1799U, false, false, "Listening...");
  check_phase(1800U, true, false, "Listening...");
  check_phase(3799U, true, false, "Listening...");
  check_phase(3800U, false, false, "Thinking...");
  check_phase(5599U, false, false, "Thinking...");
  check_phase(5600U, false, true, "Responding...");
  check_phase(8399U, false, true, "Responding...");
  check_phase(8400U, false, false, "Listening...");

  // Cadence controller keeps 20ms anim poller alive for VA preview
  const auto c20 = frame_cadence().step_20ms(15800U, true, false, false, false, false, false, 8000U);
  LATTER_CHECK(!c20.stop_anim_poller);
}

void test_showcase_brightness_and_cordless_motion() {
  using namespace latter_ui;
  reset_runtime_state();
  persisted_prefs() = PersistedDevicePrefs{};

  UiColor pri{}, sec{};
  auto &eng = touch_engine();
  bind_showcase_test_runtime(eng, pri, sec);
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();

  // 1. Verbatim brightness across display_power_state == 0 AND 1 (including Photo Frame & Bedtime!)
  (void)eng.execute_device_ui_command("showcase:brightness:65", 10000U);
  rs.nav.display_power_state = 0;
  LATTER_CHECK(std::fabs(resolve_effective_panel_brightness(rs, prefs, 0.90f, false, 10100U) - 0.65f) < 0.001f);

  rs.nav.display_power_state = 1;
  LATTER_CHECK(std::fabs(resolve_effective_panel_brightness(rs, prefs, 0.90f, false, 10100U) - 0.65f) < 0.001f);
  LATTER_CHECK(std::fabs(resolve_effective_panel_brightness(rs, prefs, 0.90f, true, 10100U) - 0.65f) < 0.001f);

  prefs.bedtime_active = true;
  LATTER_CHECK(std::fabs(resolve_effective_panel_brightness(rs, prefs, 0.15f, false, 10100U) - 0.65f) < 0.001f);

  // Clearing brightness override (brightness:0) restores normal dimming curves
  prefs.bedtime_active = false;
  (void)eng.execute_device_ui_command("showcase:brightness:0", 10200U);
  const float normal_ss = resolve_effective_panel_brightness(rs, prefs, 0.80f, false, 10300U);
  LATTER_CHECK(std::fabs(normal_ss - resolve_screensaver_brightness(0.80f, false)) < 0.001f);

  // 2. Cordless battery motion stepping (hw_usb_connected == false, stale touch past 35s window)
  rs.nav.display_power_state = 0;
  rs.hw_usb_connected = false;
  last_screen_wake_ms() = 1000U;
  last_user_touch_ms() = 1000U;
  touch_gesture().last_touch_ms = 1000U;
  const uint32_t stale_now = 60000U;

  (void)eng.execute_device_ui_command("showcase:clock:Modern Analog", stale_now);
  const bool sweep_with_showcase = clock_smooth_sweep_active(
      rs.hw_usb_connected || rs.showcase.is_active(stale_now), stale_now, 1000U,
      effective_clock_face(prefs, rs, stale_now));
  LATTER_CHECK(sweep_with_showcase);

  const bool sky_with_showcase = weather_sky_motion_stepping(
      rs.hw_usb_connected || rs.showcase.is_active(stale_now), stale_now, 1000U, true);
  LATTER_CHECK(sky_with_showcase);

#if !LATTER_IS_STATIONARY
  // Without showcase on cordless battery after 35s idle, battery_motion_allowed is false
  (void)eng.execute_device_ui_command("showcase:off", stale_now + 100U);
  LATTER_CHECK(!battery_motion_allowed(rs.hw_usb_connected || rs.showcase.is_active(stale_now + 100U),
                                       stale_now + 100U, 1000U));
#endif
}

void test_showcase_live_triggers_and_screensavers() {
  using namespace latter_ui;
  reset_runtime_state();
  persisted_prefs() = PersistedDevicePrefs{};
  seed_full_nav_fixture();

  UiColor pri{}, sec{};
  auto &eng = touch_engine();
  bind_showcase_test_runtime(eng, pri, sec);
  auto &rs = runtime_state();

  // 1. Silent timer slot 0 loops on expiration without ringing
  (void)eng.execute_device_ui_command("showcase:timer:10", 10000U);
  LATTER_CHECK(timer_slots()[0].running && timer_slots()[0].duration_sec == 10 && rs.showcase.timer_owned);
  const auto tick_exp = timer_alarm_controller().tick_timers_250ms(20500U);
  LATTER_CHECK(!tick_exp.any_expired);
  LATTER_CHECK(!rs.timers.ringing);
  LATTER_CHECK(timer_slots()[0].running && timer_slots()[0].remaining_sec == 10);
  (void)eng.execute_device_ui_command("showcase:timer:off", 21000U);
  LATTER_CHECK(!timer_slots()[0].running && !rs.showcase.timer_owned);

  // 2. Stopwatch start / stop / reset
  (void)eng.execute_device_ui_command("showcase:stopwatch:start", 22000U);
  LATTER_CHECK(rs.stopwatch.running && rs.showcase.stopwatch_owned);
  (void)eng.execute_device_ui_command("showcase:stopwatch:stop", 25500U);
  LATTER_CHECK(!rs.stopwatch.running && rs.stopwatch.elapsed_ms == 3500U);
  (void)eng.execute_device_ui_command("showcase:stopwatch:reset", 26000U);
  LATTER_CHECK(!rs.stopwatch.running && rs.stopwatch.elapsed_ms == 0U && !rs.showcase.stopwatch_owned);

  // 3. Silent showcase:roll on Dice and Coin Flip (zero speaker SFX!)
  pending_game_sfx() = 0;
  (void)eng.execute_device_ui_command("nav:tools:dice", 27000U);
  (void)eng.execute_device_ui_command("showcase:roll", 27100U);
  LATTER_CHECK(dice_state().anim_active);
  LATTER_CHECK(pending_game_sfx() == 0);

  (void)eng.execute_device_ui_command("nav:tools:coin_flip", 28000U);
  (void)eng.execute_device_ui_command("showcase:roll", 28100U);
  LATTER_CHECK(coin_state().anim_active);
  LATTER_CHECK(pending_game_sfx() == 0);

  // 4. Silent Find My & Alarm previews (never touch real audio/ringing flags!)
  HostSurfaceFixture fx{};
  (void)eng.execute_device_ui_command("showcase:find_my:on", 29000U);
  LATTER_CHECK(rs.showcase.silent_find_my && !rs.alarms.find_my_active);
  fx.fill(0U);
  fx.ctx.now_ms = 29050U;
  {
    const auto st = SystemTakeoverState::from_context(fx.ctx);
    LATTER_CHECK(st.find_my_active);
    LATTER_CHECK(render_system_takeovers(fx.surf, fx.ctx, st));
  }
  (void)eng.execute_device_ui_command("showcase:alarm:on", 29500U);
  LATTER_CHECK(rs.showcase.silent_alarm && !rs.showcase.silent_find_my && !rs.alarms.ringing);
  fx.fill(0U);
  fx.ctx.now_ms = 29550U;
  {
    const auto st = SystemTakeoverState::from_context(fx.ctx);
    LATTER_CHECK(st.alarm_ringing);
    LATTER_CHECK(render_system_takeovers(fx.surf, fx.ctx, st));
  }
  (void)eng.execute_device_ui_command("showcase:alarm:off", 29800U);
  LATTER_CHECK(!rs.showcase.silent_alarm);

  // 5. Screensaver entry and in-place Photo Frame refresh
  const auto ss1 = eng.execute_device_ui_command("showcase:screensaver:Aurora", 30000U);
  LATTER_CHECK(ss1.handled && ss1.enter_screensaver);
  LATTER_CHECK(rs.nav.display_power_state == 1);
  LATTER_CHECK(std::string_view(effective_screensaver_style_cstr(persisted_prefs(), rs, 30000U)) == "Aurora");

  const auto ss2 = eng.execute_device_ui_command("showcase:screensaver:Photo Frame", 30500U);
  LATTER_CHECK(ss2.handled && !ss2.enter_screensaver && ss2.refresh_screensaver_in_place);
  LATTER_CHECK(ss2.fetch_screensaver_photo && ss2.apply_power && ss2.sync_brightness);
  LATTER_CHECK(std::string_view(effective_screensaver_style_cstr(persisted_prefs(), rs, 30500U)) == "Photo Frame");
}

void test_showcase_touch_rules_and_reel() {
  using namespace latter_ui;
  reset_runtime_state();
  persisted_prefs() = PersistedDevicePrefs{};

  UiColor pri{}, sec{};
  auto &eng = touch_engine();
  bind_showcase_test_runtime(eng, pri, sec);
  auto &rs = runtime_state();
  last_screen_wake_ms() = 0U;

  // 1. HA-directed showcase:on -> tapping VA_CANCEL_BTN clears ONLY va_stage (keep-awake + brightness intact)
  (void)eng.execute_device_ui_command("showcase:on:600", 10000U);
  (void)eng.execute_device_ui_command("showcase:brightness:70", 10100U);
  (void)eng.execute_device_ui_command("showcase:va:speaking:Nebula", 10200U);

  HostSurfaceFixture fx{};
  fx.fill(0U);
  fx.ctx.now_ms = 10250U;
  frame_hits().clear();
  LATTER_CHECK(render_system_takeovers(fx.surf, fx.ctx, SystemTakeoverState::from_context(fx.ctx)));
  const HitRegion *va_btn = frame_hits().find_by_id(WidgetId::VA_CANCEL_BTN);
  LATTER_CHECK(va_btn != nullptr);

  recorded_host_cmds().clear();
  eng.on_touch_down(va_btn->cx(), va_btn->cy(), 11000U);
  LATTER_CHECK(rs.showcase.va_stage == 0U);
  LATTER_CHECK(rs.showcase.is_active(11000U));
  LATTER_CHECK(rs.showcase.brightness_override_pct == 70U);
  LATTER_CHECK(!has_host_cmd(UiHostCmd::CANCEL_VOICE_ASSISTANT));

  // 2. HA-directed showcase:find_my:on -> tapping ALERT_DISMISS_BTN clears ONLY silent_find_my
  (void)eng.execute_device_ui_command("showcase:find_my:on", 12000U);
  fx.fill(0U);
  fx.ctx.now_ms = 12050U;
  frame_hits().clear();
  LATTER_CHECK(render_system_takeovers(fx.surf, fx.ctx, SystemTakeoverState::from_context(fx.ctx)));
  const HitRegion *fm_btn = frame_hits().find_by_id(WidgetId::ALERT_DISMISS_BTN);
  LATTER_CHECK(fm_btn != nullptr);

  eng.on_touch_down(fm_btn->cx(), fm_btn->cy(), 12500U);
  LATTER_CHECK(!rs.showcase.silent_find_my);
  LATTER_CHECK(rs.showcase.is_active(12500U));
  LATTER_CHECK(rs.showcase.brightness_override_pct == 70U);

  // 3. HA-directed showcase:alarm:on -> tapping ALERT_SNOOZE_BTN clears ONLY silent_alarm
  (void)eng.execute_device_ui_command("showcase:alarm:on", 13000U);
  fx.fill(0U);
  fx.ctx.now_ms = 13050U;
  frame_hits().clear();
  LATTER_CHECK(render_system_takeovers(fx.surf, fx.ctx, SystemTakeoverState::from_context(fx.ctx)));
  const HitRegion *snz_btn = frame_hits().find_by_id(WidgetId::ALERT_SNOOZE_BTN);
  LATTER_CHECK(snz_btn != nullptr);

  eng.on_touch_down(snz_btn->cx(), snz_btn->cy(), 13500U);
  LATTER_CHECK(!rs.showcase.silent_alarm);
  LATTER_CHECK(rs.showcase.is_active(13500U));
  LATTER_CHECK(rs.showcase.brightness_override_pct == 70U);

  // 4. Standalone showcase:reel skips unconfigured steps (no media/area/thermostat/cameras/remote)
  // and exits cleanly on first physical touch while staying on current screen!
  reset_runtime_state();
  persisted_prefs() = PersistedDevicePrefs{};
  bind_showcase_test_runtime(eng, pri, sec);
  last_screen_wake_ms() = 0U;

  (void)eng.execute_device_ui_command("showcase:theme:Pink", 20000U);
  (void)eng.execute_device_ui_command("showcase:brightness:60", 20050U);
  const auto r0 = eng.execute_device_ui_command("showcase:reel:3", 20100U);
  LATTER_CHECK(r0.handled && rs.showcase.reel_active && rs.showcase.reel_dwell_sec == 3U);
  LATTER_CHECK(rs.showcase.reel_step_idx == 0U && rs.nav.gui_page == PageId::HOME);

  // Step 0 -> 1 (Weather Forecast)
  (void)eng.tick_showcase(23100U);
  LATTER_CHECK(rs.showcase.reel_step_idx == 1U && rs.nav.gui_page == PageId::WEATHER && rs.nav.sky_weather_subpage == 1);

  // Step 1 -> 2 (Clock Modern Analog)
  (void)eng.tick_showcase(26100U);
  LATTER_CHECK(rs.showcase.reel_step_idx == 2U && rs.nav.gui_page == PageId::CLOCK);

  // Step 2 -> 3 (VA Nebula cycle)
  (void)eng.tick_showcase(29100U);
  LATTER_CHECK(rs.showcase.reel_step_idx == 3U && rs.showcase.has_va_preview(29100U));

  // Step 3 -> skips 4 (music), 5 (area), 6 (thermostat), 7 (cameras), 8 (remote) -> lands on 9 (Calendar Overview)!
  (void)eng.tick_showcase(32100U);
  LATTER_CHECK(rs.showcase.reel_step_idx == 9U && rs.nav.gui_page == PageId::CALENDAR);

  // First physical touch exits reel, clears volatile overrides, restores theme/brightness/power,
  // and leaves the user on the current screen (PageId::CALENDAR)!
  recorded_host_cmds().clear();
  eng.on_touch_down(SCREEN_CX, SCREEN_CY, 33000U);
  LATTER_CHECK(!rs.showcase.reel_active);
  LATTER_CHECK(!rs.showcase.active);
  LATTER_CHECK(rs.showcase.brightness_override_pct == 0U);
  LATTER_CHECK(rs.showcase.theme_override_mode == 0U);
  LATTER_CHECK(rs.nav.gui_page == PageId::CALENDAR);
  LATTER_CHECK(has_host_cmd(UiHostCmd::APPLY_THEME_PRESET));
  LATTER_CHECK(has_host_cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS));
  LATTER_CHECK(has_host_cmd(UiHostCmd::APPLY_POWER_POLICY));

  // Touch exit during a screensaver step of showcase:reel also wakes the display!
  (void)eng.execute_device_ui_command("showcase:reel:3", 40000U);
  (void)eng.execute_device_ui_command("showcase:screensaver:Aurora", 40100U);
  rs.showcase.reel_active = true;
  LATTER_CHECK(rs.nav.display_power_state == 1);
  recorded_host_cmds().clear();
  eng.on_touch_down(SCREEN_CX, SCREEN_CY, 41000U);
  LATTER_CHECK(!rs.showcase.reel_active && !rs.showcase.active);
  LATTER_CHECK(has_host_cmd(UiHostCmd::WAKE_TO_ACTIVE));
}

void test_showcase_verifier_regressions() {
  using namespace latter_ui;
  reset_runtime_state();
  persisted_prefs() = PersistedDevicePrefs{};
  seed_full_nav_fixture();

  UiColor pri{}, sec{};
  auto &eng = touch_engine();
  bind_showcase_test_runtime(eng, pri, sec);
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();

  // Regression #1 & #7b: Static source guards in YAML modules
  {
    const std::string pp_yaml = read_repo_file("../../modules/power_policy.yaml");
    LATTER_CHECK(pp_yaml.find("sync_disp_poller_for_anim(&id(disp1), false)") != std::string::npos);
    const std::string tf_yaml = read_repo_file("../../modules/timers_and_fetchers.yaml");
    LATTER_CHECK(tf_yaml.find("rs.nav.display_power_state != 0 || rs.alert.camera_url.empty()") != std::string::npos);
  }

  // Regression #2: Persisted screensaver eligibility matrix & clear_showcase() exit actions
  {
    PersistedDevicePrefs p{};
    p.bedtime_active = true;
    p.bedtime_display = 0U;  // Screen Off
    LATTER_CHECK(evaluate_persisted_screensaver_exit_action(p, true, true) == ScreensaverExitAction::TURN_SCREEN_OFF);
    LATTER_CHECK(evaluate_persisted_screensaver_exit_action(p, false, false) == ScreensaverExitAction::TURN_SCREEN_OFF);

    p.bedtime_display = 1U;  // Ambient Clock
    LATTER_CHECK(evaluate_persisted_screensaver_exit_action(p, false, false) == ScreensaverExitAction::TURN_SCREEN_OFF);
    LATTER_CHECK(evaluate_persisted_screensaver_exit_action(p, false, true) == ScreensaverExitAction::KEEP_SCREENSAVER);
    LATTER_CHECK(evaluate_persisted_screensaver_exit_action(p, true, false) == ScreensaverExitAction::KEEP_SCREENSAVER);

    p.bedtime_active = false;
    p.screensaver_mode = 0U;  // Docked Only
    LATTER_CHECK(evaluate_persisted_screensaver_exit_action(p, false, false) == ScreensaverExitAction::TURN_SCREEN_OFF);
    LATTER_CHECK(evaluate_persisted_screensaver_exit_action(p, false, true) == ScreensaverExitAction::KEEP_SCREENSAVER);
    LATTER_CHECK(evaluate_persisted_screensaver_exit_action(p, true, false) == ScreensaverExitAction::KEEP_SCREENSAVER);

    p.screensaver_mode = 1U;  // Always
    LATTER_CHECK(evaluate_persisted_screensaver_exit_action(p, false, false) == ScreensaverExitAction::KEEP_SCREENSAVER);

    p.screensaver_mode = 2U;  // Never
    LATTER_CHECK(evaluate_persisted_screensaver_exit_action(p, true, true) == ScreensaverExitAction::WAKE_TO_ACTIVE);
    LATTER_CHECK(evaluate_persisted_screensaver_exit_action(p, false, false) == ScreensaverExitAction::TURN_SCREEN_OFF);

    // Live clear_showcase() under Bedtime Screen Off -> turns screen off!
    prefs = PersistedDevicePrefs{};
    prefs.bedtime_active = true;
    prefs.bedtime_display = 0U;
    (void)eng.execute_device_ui_command("showcase:screensaver:Aurora", 10000U);
    LATTER_CHECK(rs.nav.display_power_state == 1);
    const auto off_bt = eng.execute_device_ui_command("showcase:off", 10500U);
    LATTER_CHECK(off_bt.turn_screen_off && !off_bt.schedule_disp && !off_bt.wake_display);

    // Live clear_showcase() under screensaver_mode=Never -> wake on stationary, turn_screen_off on portable
    prefs = PersistedDevicePrefs{};
    prefs.screensaver_mode = 2U;  // Never
    rs.hw_usb_connected = false;
    rs.nav.display_power_state = 0;
    (void)eng.execute_device_ui_command("showcase:screensaver:Starfield", 11000U);
    const auto off_nev = eng.execute_device_ui_command("showcase:off", 11500U);
#if LATTER_IS_STATIONARY
    LATTER_CHECK(off_nev.wake_display && !off_nev.turn_screen_off);
#else
    LATTER_CHECK(off_nev.turn_screen_off && !off_nev.wake_display);
#endif

    // Live clear_showcase() reverting to allowed Photo Frame -> stamps standby_entered_ms & fetches photo
    prefs = PersistedDevicePrefs{};
    prefs.screensaver_mode = 1U;   // Always
    prefs.screensaver_style = 1U;  // Photo Frame
    rs.nav.display_power_state = 0;
    (void)eng.execute_device_ui_command("showcase:screensaver:Aurora", 12000U);
    rs.nav.standby_entered_ms = 12000U;
    const auto off_pf = eng.execute_device_ui_command("showcase:off", 20000U);
    LATTER_CHECK(!off_pf.turn_screen_off && !off_pf.wake_display);
    LATTER_CHECK(off_pf.refresh_screensaver_in_place && off_pf.fetch_screensaver_photo);
    LATTER_CHECK(rs.nav.standby_entered_ms == 20000U);
  }

  // Regression #3: weather_sim_override immune to mid-showcase HA device_prefs updates & restores HA value
  {
    reset_runtime_state();
    persisted_prefs() = PersistedDevicePrefs{};
    bind_showcase_test_runtime(eng, pri, sec);
    device_prefs_state().weather_sim_condition = 5U;  // Cloudy / Overcast (Day)

    (void)eng.execute_device_ui_command("showcase:weather:Thunderstorm (Night)", 30000U);
    LATTER_CHECK(rs.showcase.weather_sim_override == 15);
    LATTER_CHECK(device_prefs_state().weather_sim_condition == 5U);
    LATTER_CHECK(std::string_view(effective_weather_sim_cstr(device_prefs_state(), rs, 30100U)) == "Thunderstorm (Night)");

    // Simulate inbound HA device_prefs push mid-showcase changing weather_sim_condition to 2 ("Sunny / Clear Day")
    device_prefs_state().weather_sim_condition = 2U;
    LATTER_CHECK(std::string_view(effective_weather_sim_cstr(device_prefs_state(), rs, 30200U)) == "Thunderstorm (Night)");

    // showcase:off cleanly restores HA's current weather_sim_condition (2 = "Sunny / Clear Day")
    (void)eng.execute_device_ui_command("showcase:off", 30300U);
    LATTER_CHECK(rs.showcase.weather_sim_override == -1);
    LATTER_CHECK(std::string_view(effective_weather_sim_cstr(device_prefs_state(), rs, 30300U)) == "Sunny / Clear Day");
  }

  // Regression #4: Suppress fake timer_alarm_state RPC emissions for showcase-owned timer & stopwatch
  {
    reset_runtime_state();
    persisted_prefs() = PersistedDevicePrefs{};
    bind_showcase_test_runtime(eng, pri, sec);
    link_state().ha_connected = true;
    latter_actions::timer_alarm_sync_tracker() = latter_actions::TimerAlarmSyncTracker{};
    MockRpcOutbox outbox{};

    // Baseline idle init
    LATTER_CHECK(!latter_actions::check_and_emit_timer_alarm_state(40000U, false, &outbox));
    LATTER_CHECK(outbox.published.empty());

    // Showcase-owned timer slot 0 + stopwatch must emit ZERO RPCs to HA!
    (void)eng.execute_device_ui_command("showcase:timer:300", 40100U);
    (void)eng.execute_device_ui_command("showcase:stopwatch:start", 40200U);
    LATTER_CHECK(!latter_actions::check_and_emit_timer_alarm_state(41000U, false, &outbox));
    LATTER_CHECK(outbox.published.empty());

    // Real user timer on slot 1 DOES emit cleanly (with slot 1 remaining 120, not slot 0's 300, and sw_run=0)
    auto &s1 = timer_slots()[1];
    s1.duration_sec = 120;
    s1.remaining_sec = 120;
    s1.running = true;
    s1.target_ms = 41000U + 120000U;
    sync_multi_timer_state();
    LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(41500U, false, &outbox));
    LATTER_CHECK(outbox.published.size() == 1U);
    LATTER_CHECK(latter_actions::timer_alarm_sync_tracker().last_emitted_rem == 120);
    LATTER_CHECK(!latter_actions::timer_alarm_sync_tracker().sw_running);
    LATTER_CHECK(outbox.published[0].find("\"remaining_sec\":\"120\"") != std::string::npos);
    LATTER_CHECK(outbox.published[0].find("\"stopwatch_running\":\"0\"") != std::string::npos);
  }

  // Regression #5: Grace-defer Photo Frame on in-place Aurora -> Photo Frame switch
  {
    reset_runtime_state();
    persisted_prefs() = PersistedDevicePrefs{};
    seed_full_nav_fixture();
    bind_showcase_test_runtime(eng, pri, sec);

    (void)eng.execute_device_ui_command("showcase:screensaver:Aurora", 50000U);
    LATTER_CHECK(rs.nav.display_power_state == 1 && rs.nav.standby_entered_ms == 50000U);

    // 10s later (past 2500ms initial grace window), switch in-place to Photo Frame
    const auto pf_eff = eng.execute_device_ui_command("showcase:screensaver:Photo Frame", 60000U);
    LATTER_CHECK(pf_eff.refresh_screensaver_in_place && pf_eff.fetch_screensaver_photo);
    LATTER_CHECK(rs.nav.standby_entered_ms == 60000U);
    LATTER_CHECK(is_photo_frame_initial_load_pending(
        1U << 3, UiStrView("Photo Frame"), area_ctx_state().screensaver_photo_url,
        false, false, rs.nav.standby_entered_ms, 60500U));
  }

  // Regression #6: nav:home / nav:app:home respects home_screen_enabled == false kiosk mode
  {
    reset_runtime_state();
    persisted_prefs() = PersistedDevicePrefs{};
    bind_showcase_test_runtime(eng, pri, sec);
    prefs.home_screen_enabled = false;
    prefs.start_screen = 2U;  // "Time App" -> PageId::TIME

    (void)eng.execute_device_ui_command("nav:weather:moon", 70000U);
    LATTER_CHECK(rs.nav.gui_page == PageId::WEATHER);
    (void)eng.execute_device_ui_command("nav:home", 70100U);
    LATTER_CHECK(rs.nav.gui_page == PageId::TIME);

    (void)eng.execute_device_ui_command("nav:calendar:month", 70200U);
    LATTER_CHECK(rs.nav.gui_page == PageId::CALENDAR);
    (void)eng.execute_device_ui_command("nav:app:home", 70300U);
    LATTER_CHECK(rs.nav.gui_page == PageId::TIME);
  }

  // Regression #7a: Wrap-safe effective_va_phase across uint32_t rollover
  {
    ShowcaseState sc{};
    const uint32_t t_start = 0xFFFFFC18U;  // UINT32_MAX - 999
    sc.arm(t_start, 60U);
    sc.va_stage = static_cast<uint8_t>(ShowcaseVaStage::CYCLE);
    sc.va_stage_start_ms = t_start;
    LATTER_CHECK(sc.effective_va_phase(t_start + 100U) == static_cast<uint8_t>(ShowcaseVaStage::LISTENING));
    LATTER_CHECK(sc.effective_va_phase(t_start + 2000U) == static_cast<uint8_t>(ShowcaseVaStage::VAD));
    LATTER_CHECK(sc.effective_va_phase(t_start + 4000U) == static_cast<uint8_t>(ShowcaseVaStage::THINKING));
    LATTER_CHECK(sc.effective_va_phase(t_start + 6000U) == static_cast<uint8_t>(ShowcaseVaStage::SPEAKING));
  }

  // Regression #7c: External manual command clears reel_active (garbage command does not)
  {
    reset_runtime_state();
    persisted_prefs() = PersistedDevicePrefs{};
    bind_showcase_test_runtime(eng, pri, sec);

    (void)eng.execute_device_ui_command("showcase:reel:3", 80000U);
    LATTER_CHECK(rs.showcase.reel_active);
    const auto bad_eff = eng.execute_device_ui_command("showcase:not_a_real_cmd", 80500U);
    LATTER_CHECK(!bad_eff.handled && rs.showcase.reel_active);

    const auto man_eff = eng.execute_device_ui_command("nav:weather:moon", 81000U);
    LATTER_CHECK(man_eff.handled && !rs.showcase.reel_active);
    LATTER_CHECK(rs.nav.gui_page == PageId::WEATHER && rs.nav.sky_weather_subpage == 3);

    // Advancing past dwell time does NOT overwrite the manual command
    (void)eng.tick_showcase(90000U);
    LATTER_CHECK(rs.nav.gui_page == PageId::WEATHER && rs.nav.sky_weather_subpage == 3);
  }
}

}  // namespace

void run_showcase_nav_tests() {
  test_nav_all_routes_and_trait_gating();
  test_bedtime_and_silent_nav();
  test_showcase_lifecycle_nvs_purity_and_auto_expiry();
  test_showcase_va_cycle_and_rendering();
  test_showcase_brightness_and_cordless_motion();
  test_showcase_live_triggers_and_screensavers();
  test_showcase_touch_rules_and_reel();
  test_showcase_verifier_regressions();
}

}  // namespace latter_test
