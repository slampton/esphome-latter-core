#include "test_harness.h"

namespace latter_test {

namespace {

void test_touch_hal_kinematic_gestures_and_d049_d050() {
  using namespace latter_ui;

  {
    const uint8_t ft_down[5] = {0x01U, 0x81U, 0x2CU, 0x00U, 0xF0U};
    const auto s_ft = decode_focaltech_frame(ft_down);
    LATTER_CHECK(s_ft.kind == TouchSampleKind::Contact && s_ft.raw_x == 300 && s_ft.raw_y == 240 && !s_ft.ack_buffer);

    const uint8_t ft_lift_cnt0[5] = {0x00U, 0x81U, 0x2CU, 0x00U, 0xF0U};
    LATTER_CHECK(decode_focaltech_frame(ft_lift_cnt0).kind == TouchSampleKind::Released);
    const uint8_t ft_corrupt[5] = {0x0FU, 0x80U, 0x10U, 0x00U, 0x10U};
    LATTER_CHECK(decode_focaltech_frame(ft_corrupt).kind == TouchSampleKind::Invalid);

    const uint8_t gt_pt[4] = {0x68U, 0x01U, 0xAEU, 0x02U};
    LATTER_CHECK(decode_gt911_frame(0x01U, gt_pt).kind == TouchSampleKind::NotReady);
    const auto s_gt_down = decode_gt911_frame(0x81U, gt_pt);
    LATTER_CHECK(s_gt_down.kind == TouchSampleKind::Contact && s_gt_down.ack_buffer &&
                 s_gt_down.raw_x == 360 && s_gt_down.raw_y == 686);
    LATTER_CHECK(decode_gt911_frame(0x80U, nullptr).kind == TouchSampleKind::Released);

    const uint8_t cst_down[7] = {0x06U, 0x17U, 0x1DU, 0x42U, 0x20U, 0x01U, 0xABU};
    const auto s_cst = decode_cst9217_frame(cst_down);
    LATTER_CHECK(s_cst.kind == TouchSampleKind::Contact && s_cst.raw_x == 372 && s_cst.raw_y == 466);

    const uint8_t c816_down[6] = {0x00U, 0x01U, 0x80U, 0xFAU, 0x01U, 0x04U};
    const auto s_816 = decode_cst816s_frame(c816_down);
    LATTER_CHECK(s_816.kind == TouchSampleKind::Contact && s_816.raw_x == 250 && s_816.raw_y == 260);
    const uint8_t c816_idle_ff[6] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
    LATTER_CHECK(decode_cst816s_frame(c816_idle_ff).kind == TouchSampleKind::Released);
    const uint8_t c816_cnt5[6] = {0x00U, 0x05U, 0x80U, 0x64U, 0x00U, 0xC8U};
    LATTER_CHECK(decode_cst816s_frame(c816_cnt5).kind == TouchSampleKind::Invalid);

    static_assert(default_active_poll_ms(TouchHalProtocol::GT911) == 8U);
    static_assert(default_active_poll_ms(TouchHalProtocol::FT6336) == 10U);
    static_assert(default_active_poll_ms(TouchHalProtocol::CST9217) == 16U);
  }

  for (int ppi : {165, 254, 266, 313, 315}) {
    const KinematicMetrics km = KinematicMetrics::for_ppi(ppi);
    LATTER_CHECK(km.swipe_flick_px <= km.tap_slop_px && km.tap_slop_px < km.swipe_live_px);

    TouchGestureState g{};
    g.begin(160, 240, 1000U, PageId::HOME, 101);
    const int roll_px = std::max(1, mm_to_px_for(1.20f, ppi));
    g.record_move(160 + roll_px, 240, 1025U, ppi);
    LATTER_CHECK(!g.tap_cancelled && !g.swipe_committed);

    g.begin(160, 240, 3000U, PageId::HOME, 101);
    const int fast_px = std::max(km.swipe_flick_px, km.tap_slop_px + 1);
    g.record_move(160 - fast_px, 240, 3035U, ppi);
    LATTER_CHECK(g.tap_cancelled);
    LATTER_CHECK(eval_kinematic_horiz_swipe(-fast_px, 0, 35U, true, ppi, g.axis_lock) == +1);

    g.begin(160, 240, 4000U, PageId::HOME, 101);
    const int full_px = km.swipe_live_px + 2;
    g.record_move(160 + full_px, 242, 4090U, ppi);
    LATTER_CHECK(g.axis_lock == GestureAxisLock::Horizontal);
    LATTER_CHECK(eval_kinematic_horiz_swipe(full_px, 2, 90U, false, ppi, g.axis_lock) == -1);
  }

  {
    edge_bounce().clear();
    bool changed = true;
    const int p0 = step_pager_index(0, 0, 2, -1, false, GestureAxisLock::Horizontal, PageId::HOME, 5000U, changed);
    LATTER_CHECK(p0 == 0 && !changed && edge_bounce().is_active(PageId::HOME, GestureAxisLock::Horizontal, 5040U));
  }

  {
    touch_router() = TouchRouter{};
    const uint32_t key_vol_up = make_tap_target_key(PageId::REMOTE, WidgetId::REMOTE_VOL_UP, 0);
    const uint32_t key_vol_dn = make_tap_target_key(PageId::REMOTE, WidgetId::REMOTE_VOL_DOWN, 0);
    LATTER_CHECK(touch_router().allow_tap(1000U, TOUCH_DEBOUNCE_RAPID, key_vol_up));
    LATTER_CHECK(!touch_router().allow_tap(1040U, TOUCH_DEBOUNCE_RAPID, key_vol_up));
    LATTER_CHECK(touch_router().allow_tap(1025U, TOUCH_DEBOUNCE_RAPID, key_vol_dn));
  }

  {
    (void)accept_screen_off_tap("Disabled", false, 900U);
    LATTER_CHECK(accept_screen_off_tap("Single Tap", false, 1000U));
    (void)accept_screen_off_tap("Disabled", false, 1900U);
    LATTER_CHECK(!accept_screen_off_tap("Double Tap", false, 2000U));
    LATTER_CHECK(!accept_screen_off_tap("Double Tap", false, 2020U));
    LATTER_CHECK(accept_screen_off_tap("Double Tap", false, 2240U));
    LATTER_CHECK(!accept_screen_off_tap("Docked Only", false, 5000U));
    LATTER_CHECK(accept_screen_off_tap("Docked Only", true, 5100U));
  }

  {
    const std::string area_src = read_repo_file("../../components/latter_ui/page_area_hub.cpp");
    const std::string touch_src = read_repo_glob("../../components/latter_ui", "touch_", ".cpp");
    LATTER_CHECK(area_src.find("AREA_CARD_SLIDER") == std::string::npos);
    LATTER_CHECK(area_src.find("WidgetId::AREA_CARD_TOGGLE") != std::string::npos);
    LATTER_CHECK(touch_src.find("tap_max_loose") == std::string::npos);
    LATTER_CHECK(touch_src.find("try_commit_kinematic_swipe_") != std::string::npos);
  }
}

void test_quick_actions_d074() {
  using namespace latter_ui;

  LATTER_CHECK(WidgetId::QUICK_VOL_SLIDER == 70);
  LATTER_CHECK(WidgetId::QUICK_DND_TOGGLE == 76);

  PersistedDevicePrefs p{};
  p.manual_brightness = 62U;
  p.day_brightness = 84U;
  p.night_brightness = 28U;
  p.bedtime_brightness = 11U;
  p.day_start_hour = 7U;
  p.night_start_hour = 20U;

  p.bedtime_active = true;
  p.auto_brightness_mode = static_cast<uint8_t>(find_opt_index(kAutoBrightnessModeOpts, "Sun Synchronized"));
  LATTER_CHECK(resolve_active_brightness_target(p, "above_horizon", true, 12).kind == ActiveBrightnessTargetKind::BEDTIME);

  p.bedtime_active = false;
  p.auto_brightness_mode = static_cast<uint8_t>(find_opt_index(kAutoBrightnessModeOpts, "Disabled"));
  LATTER_CHECK(resolve_active_brightness_target(p, "above_horizon", true, 12).pct == 62U);

  p.auto_brightness_mode = static_cast<uint8_t>(find_opt_index(kAutoBrightnessModeOpts, "Sun Synchronized"));
  LATTER_CHECK(resolve_active_brightness_target(p, "above_horizon", true, 22).pct == 84U);
  LATTER_CHECK(resolve_active_brightness_target(p, "below_horizon", true, 13).pct == 28U);

  p.auto_brightness_mode = static_cast<uint8_t>(find_opt_index(kAutoBrightnessModeOpts, "Time Schedule"));
  LATTER_CHECK(resolve_active_brightness_target(p, "above_horizon", true, 6).kind == ActiveBrightnessTargetKind::NIGHT);
  LATTER_CHECK(resolve_active_brightness_target(p, "below_horizon", true, 12).kind == ActiveBrightnessTargetKind::DAY);

  const uint32_t sig_qa = compute_takeover_signature(false, false, true, 0, false, false, false, false, false, true);
  LATTER_CHECK(sig_qa == (1U << 11));

  DeviceRuntimeState rs{};
  rs.nav.gui_page = PageId::AREA;
  p = PersistedDevicePrefs{};
  p.home_screen_enabled = true;
  p.return_to_home_timeout = 3U;
  const int ctx_closed = compute_touch_context(rs);
  rs.nav.quick_actions_open = true;
  LATTER_CHECK(!should_return_to_home_on_idle(rs, p, false));
  LATTER_CHECK(compute_touch_context(rs) != ctx_closed);

  const std::string layout_h = read_repo_file("../../include/ui_layout.h");
  const std::string state_h = read_repo_glob("../../include/state", "", ".h");
  const std::string touch_h = read_repo_file("../../include/ui_touch_engine.h");
  const std::string comp_cpp = read_repo_file("../../components/latter_ui/overlay_takeover.cpp");
  const std::string qa_cpp = read_repo_file("../../components/latter_ui/overlay_quick_actions.cpp");
  const std::string touch_cpp = read_repo_glob("../../components/latter_ui", "touch_", ".cpp");
  const std::string gui_yaml = read_repo_file("../../guis/universal/gui.yaml");

  LATTER_CHECK(layout_h.find("top_edge_zone_y()") != std::string::npos);
  LATTER_CHECK(comp_cpp.find("render_quick_actions_overlay(") != std::string::npos);
  LATTER_CHECK(qa_cpp.find("void render_quick_actions_overlay(") != std::string::npos);
  LATTER_CHECK(gui_yaml.find("quick_actions_only = (takeover_sig == (1U << 11))") != std::string::npos);

  for (const std::string *src : {&state_h, &touch_h, &touch_cpp, &gui_yaml}) {
    LATTER_CHECK(src->find("hold_crown_power_off") == std::string::npos);
    LATTER_CHECK(src->find("instant_crown_hibernate") == std::string::npos);
  }
}

void test_time_app_world_clocks_alarms_and_horology() {
  using namespace latter_ui;

  reset_runtime_state();
  auto &p = persisted_prefs();
  p = PersistedDevicePrefs{};

  LATTER_CHECK(std::string_view(pref_home_clock_tap_cstr(p)) == "Time App");
  LATTER_CHECK(!p.show_utc_clock);
  LATTER_CHECK(resolve_backdrop_mode(10, 0, true, true, false, true) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(0, 0, true, true, false, true) == BackdropMode::SolidBlack);

  LATTER_CHECK(sanitize_clock_label("  San  Francisco | Bay ; Home  ") == "San Francisco Bay Home");
  LATTER_CHECK(normalize_world_clocks_spec(
                   " Kyoto | Asia/Tokyo ; UTC|UTC ; |Europe/London ; kyoto|Asia/Tokyo ; "
                   "Bengaluru|Asia/Kolkata ; Hanoi|Asia/Bangkok ; Bangkok|Asia/Bangkok ")
               == "Kyoto|Asia/Tokyo;London|Europe/London;Bengaluru|Asia/Kolkata;Hanoi|Asia/Bangkok");

  const LocalTzInfo tz = parse_local_tz_info("America/Los_Angeles|Example City|PDT|-420|1");
  LATTER_CHECK(tz.valid && tz.utc_offset_min == -420 && tz.is_dst);

  PrefMask chg{};
  uint32_t eff = 0U;
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::SHOW_UTC_CLOCK, "1", false, chg, eff));
  LATTER_CHECK(set_pref_by_key_str(
      p, PrefKey::WORLD_CLOCKS,
      "Kyoto|Asia/Tokyo;Bengaluru|Asia/Kolkata;Hanoi|Asia/Bangkok;Bangkok|Asia/Bangkok",
      false, chg, eff));
  auto &ac = area_ctx_state();
  ac = AreaContextState{};
  ac.world_clocks_summary =
      "Asia/Tokyo|Kyoto|JST|540|0;Asia/Bangkok|Hanoi|ICT|420|0;Asia/Bangkok|Bangkok|ICT|420|0";

  WorldClockEntry entries[kMaxWorldClocks + 1]{};
  LATTER_CHECK(collect_active_world_clocks(p, ac, entries) == 5);
  LATTER_CHECK(entries[0].is_pinned_utc && entries[2].city == "Bengaluru" && entries[2].utc_offset_min == 330);

  int h24 = 0, mm = 0, day_delta = 0;
  compute_world_clock_wall_time(86400U * 10U + 2U * 3600U, -420, 540, h24, mm, day_delta);
  LATTER_CHECK(h24 == 11 && mm == 0 && day_delta == 1);

  chg = 0ULL;
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::ALARMS_SUMMARY, "06:47|1|1;08:15|0|2;21:05|1|3", false, chg, eff));
  LATTER_CHECK(format_alarms_summary(p) == "06:47|1|1;08:15|0|2;21:05|1|3");

  StopwatchRuntimeState sw{};
  sw.running = true;
  sw.record_lap_elapsed(12450U);
  sw.record_lap_elapsed(27900U);
  LATTER_CHECK(sw.lap_count == 2 && sw.lap_splits_ms[0] == 12450U && sw.lap_splits_ms[1] == 15450U);

  LATTER_CHECK(!alarm_editor_blink_dim(1000U, 1000U) && alarm_editor_blink_dim(1500U, 1000U));

  const std::string time_cpp = read_repo_file("../../components/latter_ui/page_time.cpp");
  const std::string clock_cpp = read_repo_file("../../components/latter_ui/page_clock.cpp");
  const std::string hit_ids_h = read_repo_file("../../include/ui_widget_ids.h");
  LATTER_CHECK(time_cpp.find("render_time_page(") != std::string::npos);
  LATTER_CHECK(clock_cpp.find("draw_aa_tapered_hand(") != std::string::npos);
  LATTER_CHECK(hit_ids_h.find("HOME_BAR") == std::string::npos);
}

void test_live_touch_engine_end_to_end_5geom() {
  using namespace latter_ui;

  HostSurfaceFixture fix{};
  reset_runtime_state();
  reset_all_v3_rev_guards();
  touch_router() = TouchRouter{};

  auto &eng = touch_engine();
  static std::vector<UiHostCmd> s_cmds;
  s_cmds.clear();
  eng.runtime() = UiTouchRuntime{};
  eng.runtime().panel_is_on = true;
  eng.runtime().time_valid = true;
  eng.runtime().time_year = 2026;
  eng.runtime().time_month = 11;
  eng.runtime().time_day = 21;
  eng.runtime().time_hour = 14;
  eng.runtime().time_minute = 35;
  eng.runtime().exec_cmd = +[](UiHostCmd cmd, int, int) { s_cmds.push_back(cmd); };

  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  prefs = PersistedDevicePrefs{};
  prefs.home_screen_enabled = true;
  prefs.do_not_disturb = false;

  // 1. Render Home, then swipe down from top bezel to open Quick Actions (D-074)
  rs.nav.gui_page = PageId::HOME;
  rs.nav.home_subpage = HomeSubpage::MAIN;
  rs.nav.quick_actions_open = false;
  dirty_band() = DirtyBandTracker{};
  (void)dirty_band().begin_frame(PageId::HOME, 0, 0U, true);
  frame_hits().clear();
  render_active_page(fix.surf, fix.ctx);

  const int top_y = std::max(2, layout().top_edge_zone_y() / 2);
  const int swipe_dy = KinematicMetrics::for_ppi(LATTER_PANEL_PPI).swipe_live_px + 12;
  eng.on_touch_down(SCREEN_CX, top_y, 5000U);
  eng.on_touch_move(SCREEN_CX, top_y + swipe_dy, 5045U);
  eng.on_touch_up(5060U);
  LATTER_CHECK(rs.nav.quick_actions_open);

  // 2. Render Quick Actions overlay and tap QUICK_DND_TOGGLE -> toggles Do Not Disturb!
  dirty_band() = DirtyBandTracker{};
  (void)dirty_band().begin_frame(PageId::HOME, 0, (1U << 11), true);
  frame_hits().clear();
  SystemTakeoverState qa_st = SystemTakeoverState::from_context(fix.ctx);
  qa_st.quick_actions_open = true;
  LATTER_CHECK(render_system_takeovers(fix.surf, fix.ctx, qa_st));

  const HitRegion *dnd_hit = frame_hits().find_by_id(WidgetId::QUICK_DND_TOGGLE);
  LATTER_CHECK(dnd_hit != nullptr);
  eng.on_touch_down(dnd_hit->cx(), dnd_hit->cy(), 6000U);
  eng.on_touch_up(6040U);
  LATTER_CHECK(prefs.do_not_disturb);

  // Close Quick Actions via BACK_BUTTON
  const HitRegion *qa_back = frame_hits().find_by_id(WidgetId::BACK_BUTTON);
  LATTER_CHECK(qa_back != nullptr);
  eng.on_touch_down(qa_back->cx(), qa_back->cy(), 7000U);
  eng.on_touch_up(7040U);
  LATTER_CHECK(!rs.nav.quick_actions_open);

  // 3. Live PIN Gate entry (6-digit PIN + v1: session-key wrapping + zero plaintext leak)
  MockRpcOutbox pin_box{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_rpc::bind_sensor_sink(&pin_box);
  link_state().ha_connected = true;
  LATTER_CHECK(latter_rpc::set_pin_session_key(
      "cafebabe", "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"));

  area_ctx_state().security_pin_policy = "require_pin";
  rs.area.target_entity_id = "lock.front_door";
  LATTER_CHECK(!eng.gate_or_dispatch_area_action("lock.front_door", "unlock", 0, "Unlock Front Door", 8000U));
  LATTER_CHECK(ui::pin_pad_state().open);

  // Render PIN pad into frame_hits() and tap 6 digits "246810" + PIN_PAD_SUBMIT
  dirty_band() = DirtyBandTracker{};
  (void)dirty_band().begin_frame(PageId::HOME, 0, (1U << 8), true);
  frame_hits().clear();
  SystemTakeoverState pin_st = SystemTakeoverState::from_context(fix.ctx);
  LATTER_CHECK(render_system_takeovers(fix.surf, fix.ctx, pin_st));

  const int digits[6] = {2, 4, 6, 8, 1, 0};
  uint32_t tap_t = 8200U;
  for (int d : digits) {
    const HitRegion *key_r = frame_hits().find_by_id(static_cast<uint16_t>(WidgetId::PIN_PAD_KEY_BASE + d));
    LATTER_CHECK(key_r != nullptr);
    eng.on_touch_down(key_r->cx(), key_r->cy(), tap_t);
    eng.on_touch_up(tap_t + 25U);
    tap_t += 120U;
  }
  LATTER_CHECK(ui::pin_pad_state().len == 6U);

  const HitRegion *ok_r = frame_hits().find_by_id(WidgetId::PIN_PAD_SUBMIT);
  LATTER_CHECK(ok_r != nullptr);
  eng.on_touch_down(ok_r->cx(), ok_r->cy(), tap_t);
  eng.on_touch_up(tap_t + 25U);
  LATTER_CHECK(!ui::pin_pad_state().open);
  LATTER_CHECK(ui::pin_pad_state().len == 0U);

  uint32_t drain_t = tap_t + 100U;
  while (latter_rpc::has_pending_frames()) {
    latter_rpc::step_outbox_loop(drain_t);
    drain_t += 30U;
  }
  std::string pin_wire;
  for (const auto &f : pin_box.published) pin_wire += f;
  LATTER_CHECK(pin_wire.find("v1:cafebabe:") != std::string::npos);
  LATTER_CHECK(pin_wire.find("246810") == std::string::npos);
  latter_rpc::clear_pin_session_key();
  latter_rpc::bind_sensor_sink(static_cast<MockRpcOutbox *>(nullptr));

  // 4. Rotary crown focus step & activation on Home Dock
  rs.nav.gui_page = PageId::HOME;
  rs.nav.home_subpage = HomeSubpage::MAIN;
  dirty_band() = DirtyBandTracker{};
  (void)dirty_band().begin_frame(PageId::HOME, 0, 0U, true);
  frame_hits().clear();
  render_active_page(fix.surf, fix.ctx);
  LATTER_CHECK(frame_hits().focus_count() > 0);
  eng.on_input_event("cw", 1, 12000U);
  LATTER_CHECK(ui_focus_active(12000U));
  eng.runtime().exec_cmd = nullptr;
}

}  // namespace

void run_touch_engine_tests() {
  test_touch_hal_kinematic_gestures_and_d049_d050();
  test_quick_actions_d074();
  test_time_app_world_clocks_alarms_and_horology();
  test_live_touch_engine_end_to_end_5geom();
}

}  // namespace latter_test
