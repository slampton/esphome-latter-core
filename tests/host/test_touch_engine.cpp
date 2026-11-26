#include "test_harness.h"

namespace latter_test {

namespace {

void test_touch_hal_kinematic_gestures_and_d049_d050() {
  using namespace latter_ui;
  using namespace latter_core;

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

    // Sticky early Vertical jitter lock near curved bezel / mode bar must recover to Horizontal
    // once displacement reaches >= 2x swipe_live_px with >= 2:1 dominance, without ever breaking Slider locks.
    g.begin(320, 395, 5000U, PageId::MUSIC, 139264);
    g.record_move(318, 395 - km.axis_lock_px - 2, 5020U, ppi);
    LATTER_CHECK(g.axis_lock == GestureAxisLock::Vertical);
    g.record_move(320 - km.swipe_live_px * 3, 395, 5140U, ppi);
    LATTER_CHECK(g.axis_lock == GestureAxisLock::Horizontal);
    LATTER_CHECK(eval_kinematic_horiz_swipe(-km.swipe_live_px * 3, 0, 140U, true, ppi, GestureAxisLock::Vertical) == +1);
    LATTER_CHECK(eval_kinematic_horiz_swipe(-km.swipe_live_px * 3, 0, 140U, true, ppi, GestureAxisLock::Slider) == 0);
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
  using namespace latter_core;

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
  p.auto_brightness_mode = static_cast<uint8_t>(find_pref_option_index(kPrefOptsAutoBrightnessMode, "Sun Synchronized"));
  LATTER_CHECK(resolve_active_brightness_target(p, "above_horizon", true, 12).kind == ActiveBrightnessTargetKind::BEDTIME);

  p.bedtime_active = false;
  p.auto_brightness_mode = static_cast<uint8_t>(find_pref_option_index(kPrefOptsAutoBrightnessMode, "Disabled"));
  LATTER_CHECK(resolve_active_brightness_target(p, "above_horizon", true, 12).pct == 62U);

  p.auto_brightness_mode = static_cast<uint8_t>(find_pref_option_index(kPrefOptsAutoBrightnessMode, "Sun Synchronized"));
  LATTER_CHECK(resolve_active_brightness_target(p, "above_horizon", true, 22).pct == 84U);
  LATTER_CHECK(resolve_active_brightness_target(p, "below_horizon", true, 13).pct == 28U);

  p.auto_brightness_mode = static_cast<uint8_t>(find_pref_option_index(kPrefOptsAutoBrightnessMode, "Time Schedule"));
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
  using namespace latter_core;

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
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::SHOW_UTC_CLOCK, "1", chg, eff));
  LATTER_CHECK(set_pref_by_key_str(
      p, PrefKey::WORLD_CLOCKS,
      "Kyoto|Asia/Tokyo;Bengaluru|Asia/Kolkata;Hanoi|Asia/Bangkok;Bangkok|Asia/Bangkok",
      chg, eff));
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
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::ALARMS_SUMMARY, "06:47|1|1;08:15|0|2;21:05|1|3", chg, eff));
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
  using namespace latter_core;

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

  // 5. Music Lyrics tap-to-wake transport overlay (Play/Pause, Prev, Next) & scroll guard (D-079)
  {
    s_cmds.clear();
    MockRpcOutbox lyr_box{};
    latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
    latter_rpc::bind_sensor_sink(&lyr_box);
    link_state().ha_connected = true;

    auto &ms = media_state();
    ms = MediaState{};
    ms.playing = true;
    ms.state = "playing";
    ms.title = "Midnight City";
    ms.artist = "M83";
    ms.active_speaker = "media_player.office";
    ms.speaker_name = "Office";
    ms.duration = 244.0f;
    ms.position = 12.0f;
    ms.position_updated_ms = 13000U;

    rs.nav.gui_page = PageId::MUSIC;
    rs.music.view = MusicView::LYRICS;
    rs.music.drilldown = MusicDrilldown::NONE;
    rs.music.takeover = MusicTakeover::NONE;
    rs.music.hud_active = false;
    rs.music.hud_timer = 0U;
    const std::string tkey = build_music_track_key(ms.title, ms.artist);
    std::string lrc_blob;
    for (int i = 0; i < 16; ++i) {
      if (i > 0) lrc_blob.push_back(kProtocolRecordSep);
      lrc_blob += std::to_string(i * 10000);
      lrc_blob.push_back(kProtocolFieldSep);
      lrc_blob += "Lyric line " + std::to_string(i + 1) + " shining through the night";
    }
    (void)apply_music_lyrics_action(2, 0, 1, tkey, true, lrc_blob, rs.music.lyrics);
    rs.music.lyrics.scroll_px = 0;
    rs.music.lyrics.target_scroll_px = 0;

    auto render_lyrics_frame = [&](uint32_t now_ms) {
      fix.ctx.now_ms = now_ms;
      dirty_band() = DirtyBandTracker{};
      (void)dirty_band().begin_frame(PageId::MUSIC, compute_active_subpage_id(), 0U, true);
      frame_hits().clear();
      render_active_page(fix.surf, fix.ctx);
    };

    // Ambient mode: only MUSIC_ART_TAP is registered, no transport buttons yet
    render_lyrics_frame(13000U);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::MUSIC_ART_TAP) != nullptr);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::MUSIC_PLAY_PAUSE) == nullptr);

    // Tap anywhere to wake Lyrics HUD -> exposes Prev, Play/Pause, Next
    eng.on_touch_down(SCREEN_CX, SCREEN_CY, 13000U);
    eng.on_touch_up(13030U);
    LATTER_CHECK(rs.music.hud_active);

    render_lyrics_frame(13050U);
    const HitRegion *prev_r = frame_hits().find_by_id(WidgetId::MUSIC_PREV_BTN);
    const HitRegion *play_r = frame_hits().find_by_id(WidgetId::MUSIC_PLAY_PAUSE);
    const HitRegion *next_r = frame_hits().find_by_id(WidgetId::MUSIC_NEXT_BTN);
    LATTER_CHECK(prev_r != nullptr && play_r != nullptr && next_r != nullptr);

    // Tap Play/Pause with a small vertical finger wobble (> drag_start, < tap_max):
    // must NOT start lyrics scroll and must fire TOGGLE_HA_MUSIC cleanly.
    const int wobble_dy = std::min(layout().tap_max() - 1, layout().drag_start() + 2);
    const int scroll_before = rs.music.lyrics.scroll_px;
    eng.on_touch_down(play_r->cx(), play_r->cy(), 13080U);
    eng.on_touch_move(play_r->cx(), play_r->cy() + wobble_dy, 13100U);
    eng.on_touch_up(13120U);
    LATTER_CHECK(rs.music.hud_active);
    LATTER_CHECK(rs.music.lyrics.scroll_px == scroll_before);
    LATTER_CHECK(!s_cmds.empty() &&
                 std::find(s_cmds.begin(), s_cmds.end(), UiHostCmd::TOGGLE_HA_MUSIC) != s_cmds.end());

    // Tap Next Track and Previous Track -> emit canonical media_command RPCs
    eng.on_touch_down(next_r->cx(), next_r->cy(), 13300U);
    eng.on_touch_up(13330U);
    eng.on_touch_down(prev_r->cx(), prev_r->cy(), 13500U);
    eng.on_touch_up(13530U);
    uint32_t rpc_t = 13600U;
    while (latter_rpc::has_pending_frames()) {
      latter_rpc::step_outbox_loop(rpc_t);
      rpc_t += 30U;
    }
    std::string lyr_wire;
    for (const auto &f : lyr_box.published) lyr_wire += f;
    LATTER_CHECK(lyr_wire.find("\"command\":\"next\"") != std::string::npos);
    LATTER_CHECK(lyr_wire.find("\"command\":\"previous\"") != std::string::npos);

    // Vertical drag inside lyrics viewport (above tg.vp_bot) still scrolls lyrics
    const auto tg = compute_music_lyrics_transport_geom(
        SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);
    const int drag_start_y = std::max(layout().top_edge_zone_y() + 12, tg.vp_bot - 20);
    const int drag_dy = layout().drag_start() + 24;
    const int pre_scroll = rs.music.lyrics.scroll_px;
    eng.on_touch_down(SCREEN_CX, drag_start_y, 13800U);
    eng.on_touch_move(SCREEN_CX, drag_start_y - drag_dy, 13840U);
    eng.on_touch_up(13860U);
    LATTER_CHECK(rs.music.lyrics.scroll_px > pre_scroll);

    // Blank-area tap after TOUCH_GUARD_DISMISS dismisses HUD back to ambient full-bleed lyrics
    const int blank_x = 12;
    const int blank_y = std::max(layout().hdr_y + 24, tg.vp_bot - 12);
    eng.on_touch_down(blank_x, blank_y, 14300U);
    eng.on_touch_up(14330U);
    LATTER_CHECK(!rs.music.hud_active);

    // 6. Mode-bar horizontal swipe with initial vertical thumb jitter on Music SPEAKERS -> advances to VOLUME
    rs.nav.gui_page = PageId::MUSIC;
    rs.music.view = MusicView::SPEAKERS;
    rs.music.drilldown = MusicDrilldown::NONE;
    rs.music.takeover = MusicTakeover::NONE;
    dirty_band() = DirtyBandTracker{};
    (void)dirty_band().begin_frame(PageId::MUSIC, compute_active_subpage_id(), 0U, true);
    frame_hits().clear();
    render_active_page(fix.surf, fix.ctx);

    const KinematicMetrics km_cur = KinematicMetrics::for_ppi(LATTER_PANEL_PPI);
    const int mb_y = std::min(layout().home_zone_y() - 6, layout().app_dock_y + layout().dock_r / 2);
    const int start_x = std::min(SCREEN_W - 24, SCREEN_CX + km_cur.swipe_live_px * 2);
    eng.on_touch_down(start_x, mb_y, 15000U);
    eng.on_touch_move(start_x - 2, mb_y - km_cur.axis_lock_px - 2, 15025U);
    eng.on_touch_move(std::max(12, start_x - km_cur.swipe_live_px * 3), mb_y, 15110U);
    eng.on_touch_up(15140U);
    LATTER_CHECK(rs.music.view == MusicView::VOLUME);

    // 7. Peer-tab browse debouncing: rapid tab traversal cancels intermediate RPCs and emits only
    // the settled tab once the 380 ms debounce fires, without latching lyrics.track_key on pass-through.
    {
      struct DebounceSink {
        uint32_t armed_id{0};
        uint32_t armed_delay{0};
        uint32_t cancels{0};
        std::function<void()> pending{};
      } d_sink{};
      core_timeout_bridge().bind(
          &d_sink,
          +[](void *ctx, uint32_t id, uint32_t delay_ms, std::function<void()> &&fn) {
            auto *s = static_cast<DebounceSink *>(ctx);
            if (id == kTimeoutMusicPeerBrowseId) {
              s->armed_id = id;
              s->armed_delay = delay_ms;
              s->pending = std::move(fn);
            }
          },
          +[](void *ctx, uint32_t id) {
            auto *s = static_cast<DebounceSink *>(ctx);
            if (id == kTimeoutMusicPeerBrowseId) {
              s->cancels++;
              s->pending = nullptr;
            }
          });

      MockRpcOutbox deb_box{};
      latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
      latter_rpc::bind_sensor_sink(&deb_box);
      rs.nav.gui_page = PageId::MUSIC;
      rs.music.drilldown = MusicDrilldown::NONE;
      rs.music.takeover = MusicTakeover::NONE;
      rs.music.lyrics = MusicLyricsState{};
      media_state().title = "Waiting Room";
      media_state().artist = "Fugazi";
      media_state().album = "13 Songs";
      media_state().active_speaker = "media_player.pocket_assistant_music";

      activate_music_peer_tab(MusicView::SPEAKERS, 16000U);
      activate_music_peer_tab(MusicView::QUEUE, 16050U);
      activate_music_peer_tab(MusicView::LYRICS, 16100U);
      activate_music_peer_tab(MusicView::ALBUM_TRACKS, 16150U);

      // Zero RPCs emitted during rapid traversal; lyrics.track_key stayed empty on pass-through.
      uint32_t pump_t = 16200U;
      while (latter_rpc::has_pending_frames()) {
        latter_rpc::step_outbox_loop(pump_t += 40U);
      }
      LATTER_CHECK(deb_box.published.empty());
      LATTER_CHECK(rs.music.lyrics.track_key.empty());
      LATTER_CHECK(d_sink.armed_id == kTimeoutMusicPeerBrowseId && d_sink.armed_delay == 380U && d_sink.pending);

      // Settle on ALBUM_TRACKS -> fires single-chunk current_album_tracks RPC only.
      auto cb = std::move(d_sink.pending);
      cb();
      while (latter_rpc::has_pending_frames()) {
        latter_rpc::step_outbox_loop(pump_t += 40U);
      }
      LATTER_CHECK(deb_box.published.size() == 1U);
      LATTER_CHECK(deb_box.published[0].find(":1/1|") != std::string::npos);
      LATTER_CHECK(deb_box.published[0].find("\"mode\":\"current_album_tracks\"") != std::string::npos);

      // Returning to LYRICS and settling must still fetch lyrics cleanly (not stuck from pass-through).
      deb_box.published.clear();
      activate_music_peer_tab(MusicView::LYRICS, 16600U);
      LATTER_CHECK(d_sink.pending);
      cb = std::move(d_sink.pending);
      cb();
      while (latter_rpc::has_pending_frames()) {
        latter_rpc::step_outbox_loop(pump_t += 40U);
      }
      LATTER_CHECK(deb_box.published.size() == 1U);
      LATTER_CHECK(deb_box.published[0].find(":1/1|") != std::string::npos);
      LATTER_CHECK(deb_box.published[0].find("\"mode\":\"lyrics_fetch\"") != std::string::npos);
      LATTER_CHECK(rs.music.lyrics.track_key == "Waiting Room|Fugazi");

      core_timeout_bridge().reset();
      latter_rpc::bind_sensor_sink(static_cast<MockRpcOutbox *>(nullptr));
    }

    latter_rpc::bind_sensor_sink(static_cast<MockRpcOutbox *>(nullptr));
  }
  eng.runtime().exec_cmd = nullptr;
}

void test_rotary_navigation_and_glass_style_display_setting() {
  using namespace latter_ui;
  using namespace latter_core;

  reset_runtime_state();
  reset_all_v3_rev_guards();
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  prefs = PersistedDevicePrefs{};
  prefs.home_screen_enabled = true;
  prefs.glass_style = static_cast<uint8_t>(GlassStyle::Auto);

  auto &eng = touch_engine();
  eng.runtime().exec_cmd = [](UiHostCmd, int, int) {};

  // 1. Rotary escape from PageId::CLOCK when focus_count == 0 (never permanently trapped!)
  rs.nav.gui_page = PageId::CLOCK;
  frame_hits().clear();
  eng.on_input_event("select", 0, 1000U);
  LATTER_CHECK(rs.nav.gui_page == PageId::HOME);

  rs.nav.gui_page = PageId::CLOCK;
  frame_hits().clear();
  eng.on_input_event("back", 0, 1500U);
  LATTER_CHECK(rs.nav.gui_page == PageId::HOME);

  // Also works when home_screen_enabled == false and CLOCK is not the anchor root.
  prefs.home_screen_enabled = false;
  prefs.start_screen = 1;  // Pinned
  rs.nav.gui_page = PageId::CLOCK;
  frame_hits().clear();
  eng.on_input_event("select", 0, 2000U);
  LATTER_CHECK(rs.nav.gui_page == PageId::PINNED);
  prefs.home_screen_enabled = true;
  prefs.start_screen = 0;

  // 2. Focusable kHitFocusSlot on pagination chevrons, back button, Home Dock pages, mode-bar, VA cancel
  std::vector<uint16_t> fb(static_cast<size_t>(SCREEN_W) * static_cast<size_t>(SCREEN_H), 0U);
  Rgb565Surface surf = bind_host_surface(fb.data(), SCREEN_W, SCREEN_H, 1);
  UiFrameContext ctx{};
  ctx.now_ms = 5000U;
  ctx.now.year = 2026;
  ctx.now.month = 11;
  ctx.now.day_of_month = 22;
  ctx.now.hour = 14;
  ctx.now.minute = 30;
  ctx.font_time = UiFontView::make_host_font(48);
  ctx.font_chrono_time = UiFontView::make_host_font(42);
  ctx.font_date = UiFontView::make_host_font(22);
  ctx.font_btn_bold = UiFontView::make_host_font(20);
  ctx.font_small = UiFontView::make_host_font(16);
  ctx.font_micro = UiFontView::make_host_font(12);
  ctx.font_games_title = UiFontView::make_host_font(26);

  // Home Dock multi-page focusable MODE_BAR_RIGHT / MODE_BAR_LEFT
  shortcuts_state().dock_configured = true;
  shortcuts_state().dock_items.clear();
  static constexpr RouteId kTestDockRoutes[8] = {
      RouteId::MUSIC_LIBRARY, RouteId::CALENDAR_OVERVIEW, RouteId::TIME_ALARMS,
      RouteId::TIME_STOPWATCH, RouteId::TOOLS_CALCULATOR, RouteId::TOOLS_DICE,
      RouteId::SETTINGS_SLEEP, RouteId::SETTINGS_DISPLAY,
  };
  for (int i = 0; i < 8; ++i) {
    ShortcutItem it{};
    it.kind = ShortcutKind::ROUTE;
    it.route_id = kTestDockRoutes[i];
    it.label = "App " + std::to_string(i);
    shortcuts_state().dock_items.push_back(it);
  }
  home_dock_page() = 0;
  rs.nav.gui_page = PageId::HOME;
  rs.nav.home_subpage = HomeSubpage::MAIN;
  dirty_band() = DirtyBandTracker{};
  reset_clip_y_band();
  frame_hits().clear();
  frame_base_begin();
  render_active_page(surf, ctx);
  const HitRegion *dock_right = frame_hits().find_by_id(WidgetId::MODE_BAR_RIGHT);
  LATTER_CHECK(dock_right != nullptr && (dock_right->flags & kHitFocusSlot) != 0U);
  eng.on_touch_down(dock_right->cx(), dock_right->cy(), 5100U);
  eng.on_touch_up(5160U);
  LATTER_CHECK(home_dock_page() == 1);

  frame_hits().clear();
  frame_base_begin();
  render_active_page(surf, ctx);
  const HitRegion *dock_left = frame_hits().find_by_id(WidgetId::MODE_BAR_LEFT);
  LATTER_CHECK(dock_left != nullptr && (dock_left->flags & kHitFocusSlot) != 0U);
  eng.on_touch_down(dock_left->cx(), dock_left->cy(), 5600U);
  eng.on_touch_up(5660U);
  LATTER_CHECK(home_dock_page() == 0);

  // Voice Assistant cancel pill has kHitFocusSlot
  frame_hits().clear();
  frame_base_begin();
  SystemTakeoverState va_st{};
  va_st.va_active = true;
  va_st.now_ms = 5700U;
  render_voice_assistant_overlay(surf, va_st);
  const HitRegion *va_cancel = frame_hits().find_by_id(WidgetId::VA_CANCEL_BTN);
  LATTER_CHECK(va_cancel != nullptr && (va_cancel->flags & kHitFocusSlot) != 0U);

  // 3. Settings -> Display: BACK_BUTTON & CHEVRON_DOWN focusable, 3-page pagination, and "Glass Style" cycling
  MockRpcOutbox pref_box{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_rpc::bind_sensor_sink(&pref_box);
  link_state().ha_connected = true;

  rs.nav.gui_page = PageId::SYSTEM;
  rs.nav.system_menu_mode = SystemMenuMode::DISPLAY;
  rs.nav.display_menu_page = 1;
  rs.nav.menu_scroll_active = false;
  frame_hits().clear();
  frame_base_begin();
  render_active_page(surf, ctx);

  const HitRegion *back_btn = frame_hits().find_by_id(WidgetId::BACK_BUTTON);
  const HitRegion *chev_dn = frame_hits().find_by_id(WidgetId::CHEVRON_DOWN);
  const HitRegion *glass_row = frame_hits().find_by_id(static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 4));
  LATTER_CHECK(back_btn != nullptr && (back_btn->flags & kHitFocusSlot) != 0U);
  LATTER_CHECK(chev_dn != nullptr && (chev_dn->flags & kHitFocusSlot) != 0U);
  LATTER_CHECK(glass_row != nullptr && (glass_row->flags & kHitFocusSlot) != 0U && glass_row->param == 4);

  // Cycle Glass Style: Auto -> Clear -> Dark -> Auto
  auto tap_region = [&](const HitRegion *r, uint32_t t_ms) {
    eng.on_touch_down(r->cx(), r->cy(), t_ms);
    eng.on_touch_up(t_ms + 50U);
  };
  tap_region(glass_row, 6200U);
  LATTER_CHECK(prefs.glass_style_enum() == GlassStyle::Clear);
  LATTER_CHECK(build_hello_dirty_prefs(prefs).find("glass_style=Clear") != std::string::npos);
  tap_region(glass_row, 6800U);
  LATTER_CHECK(prefs.glass_style_enum() == GlassStyle::Dark);
  LATTER_CHECK(build_hello_dirty_prefs(prefs).find("glass_style=Dark") != std::string::npos);
  tap_region(glass_row, 7400U);
  LATTER_CHECK(prefs.glass_style_enum() == GlassStyle::Auto);
  LATTER_CHECK(build_hello_dirty_prefs(prefs).find("glass_style=Auto") != std::string::npos);

  // Chevron pagination reaches every viewport-solved Display page cleanly (2 pages when list_rows >= 6, 3 when == 5)
  const int disp_per_pg = std::clamp(layout().list_rows, 4, kMaxListSlots);
  const int disp_total_pages = std::max(1, (11 + disp_per_pg - 1) / disp_per_pg);
  tap_region(chev_dn, 8000U);
  LATTER_CHECK(rs.nav.menu_scroll_active && rs.nav.menu_scroll_target_page == 2);
  rs.nav.menu_scroll_active = false;
  rs.nav.display_menu_page = 2;

  frame_hits().clear();
  frame_base_begin();
  render_active_page(surf, ctx);
  const HitRegion *chev_up2 = frame_hits().find_by_id(WidgetId::CHEVRON_UP);
  const HitRegion *chev_dn2 = frame_hits().find_by_id(WidgetId::CHEVRON_DOWN);
  LATTER_CHECK(chev_up2 != nullptr && (chev_up2->flags & kHitFocusSlot) != 0U);
  if (disp_total_pages >= 3) {
    LATTER_CHECK(chev_dn2 != nullptr && (chev_dn2->flags & kHitFocusSlot) != 0U);
    tap_region(chev_dn2, 8600U);
    LATTER_CHECK(rs.nav.menu_scroll_active && rs.nav.menu_scroll_target_page == 3);
    rs.nav.menu_scroll_active = false;
    rs.nav.display_menu_page = 3;

    frame_hits().clear();
    frame_base_begin();
    render_active_page(surf, ctx);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::CHEVRON_UP) != nullptr);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::CHEVRON_DOWN) == nullptr);
  } else {
    LATTER_CHECK(chev_dn2 == nullptr);
  }

  latter_rpc::bind_sensor_sink(static_cast<MockRpcOutbox *>(nullptr));
  eng.runtime().exec_cmd = nullptr;
}

void test_area_media_player_switches_active_speaker() {
  using namespace latter_ui;
  using namespace latter_core;

  reset_runtime_state();
  reset_all_v3_rev_guards();
  auto &rs = runtime_state();
  auto &ms = media_state();
  ms = MediaState{};
  ms.local_speaker = "media_player.pocket_assistant_music";
  ms.active_speaker = "media_player.pocket_assistant_music";
  ms.speaker_name = "Pocket Assistant";
  ms.is_local = true;
  ms.state = "playing";
  ms.playback_state = MediaPlaybackState::Playing;
  ms.playing = true;
  ms.title = "Old Local Track";
  ms.artist = "Old Artist";
  ms.art_url = "http://ha/old_art.jpg";
  rs.music.active_speaker_entity = ms.active_speaker;
  rs.music.active_speaker_name = ms.speaker_name;
  rs.music.loaded_art_url = ms.art_url;

  static std::vector<UiHostCmd> s_area_cmds;
  s_area_cmds.clear();
  MockRpcOutbox rpc_box{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_rpc::bind_sensor_sink(&rpc_box);
  link_state().ha_connected = true;

  auto &eng = touch_engine();
  eng.runtime().local_mp_playing = false;
  eng.runtime().exec_cmd = +[](UiHostCmd cmd, int, int) {
    s_area_cmds.push_back(cmd);
    if (cmd == UiHostCmd::SPEAKER_CONTROL_TARGET) {
      (void)latter_core::speaker_activate_selected(false, false, false, 1500U);
    }
  };

  // Render Area Control with Living Room media_player slot
  rs.nav.gui_page = PageId::AREA;
  rs.nav.area_subpage = AreaSubpage::OVERVIEW;
  rs.area.browse_mode = "entities";
  rs.area.active_area_id = "living_room";
  rs.area.active_area_name = "Living Room";
  rs.area.domain_filter = "media";
  rs.area.available_domains_csv = "light,media";
  auto &sl0 = rs.area_entity_slots()[0];
  sl0.id = "media_player.living_room_tv";
  sl0.name = "Speaker: Living Room TV";
  sl0.type = "media_player";
  sl0.active = false;
  sl0.level = -1;

  std::vector<uint16_t> fb(static_cast<size_t>(SCREEN_W) * static_cast<size_t>(SCREEN_H), 0U);
  Rgb565Surface surf = bind_host_surface(fb.data(), SCREEN_W, SCREEN_H, 1);
  UiFrameContext ctx{};
  ctx.now_ms = 1000U;
  ctx.font_small = UiFontView::make_host_font(16);
  ctx.font_micro = UiFontView::make_host_font(12);
  ctx.col_bg = UiColor(0, 0, 0);
  ctx.col_white = UiColor(255, 255, 255);
  ctx.col_slate = UiColor(158, 158, 158);
  ctx.col_gold = UiColor(197, 155, 39);
  ctx.theme_primary = UiColor(107, 142, 158);
  ctx.theme_secondary = UiColor(192, 192, 192);

  auto render_area_frame = [&]() {
    dirty_band() = DirtyBandTracker{};
    (void)dirty_band().begin_frame(PageId::AREA, AreaSubpage::OVERVIEW, 0U, true);
    frame_hits().clear();
    frame_base_begin();
    render_active_page(surf, ctx);
  };
  auto drain_rpc = [&](uint32_t t_ms) {
    while (latter_rpc::has_pending_frames()) {
      latter_rpc::step_outbox_loop(t_ms);
      t_ms += 40U;
    }
  };

  render_area_frame();
  const HitRegion *body_hit = frame_hits().find_by_id(WidgetId::AREA_CARD_TAP);
  LATTER_CHECK(body_hit != nullptr);

  // 1. Tap media_player card body -> switches to Music AND activates Living Room TV speaker
  eng.on_touch_down(body_hit->cx(), body_hit->cy(), 1500U);
  eng.on_touch_up(1540U);
  drain_rpc(1600U);

  LATTER_CHECK(rs.nav.gui_page == PageId::MUSIC);
  LATTER_CHECK(rs.music.view == MusicView::NOW_PLAYING);
  LATTER_CHECK(ms.active_speaker == "media_player.living_room_tv");
  LATTER_CHECK(rs.music.active_speaker_entity == "media_player.living_room_tv");
  LATTER_CHECK(ms.speaker_name == "Living Room TV");
  LATTER_CHECK(ms.last_speaker == "media_player.pocket_assistant_music");
  LATTER_CHECK(!ms.is_local);
  LATTER_CHECK(ms.title.empty());
  LATTER_CHECK(ms.state == "idle");
  LATTER_CHECK(rs.music.action_feedback == "Controlling: Living Room TV");
  LATTER_CHECK(std::find(s_area_cmds.begin(), s_area_cmds.end(), UiHostCmd::SPEAKER_CONTROL_TARGET) !=
               s_area_cmds.end());
  std::string wire;
  for (const auto &f : rpc_box.published) wire += f;
  LATTER_CHECK(wire.find("\"s\":\"set_active_speaker\"") != std::string::npos);
  LATTER_CHECK(wire.find("\"entity_id\":\"media_player.living_room_tv\"") != std::string::npos);
  LATTER_CHECK(wire.find("\"transfer_queue\":\"false\"") != std::string::npos);

  // 2. Tapping the already-active media_player opens Music without re-issuing SPEAKER_CONTROL_TARGET
  s_area_cmds.clear();
  rpc_box.published.clear();
  ms.title = "TV Movie Audio";
  ms.state = "playing";
  rs.nav.gui_page = PageId::AREA;
  rs.nav.area_subpage = AreaSubpage::OVERVIEW;
  render_area_frame();
  body_hit = frame_hits().find_by_id(WidgetId::AREA_CARD_TAP);
  LATTER_CHECK(body_hit != nullptr);
  eng.on_touch_down(body_hit->cx(), body_hit->cy(), 2200U);
  eng.on_touch_up(2240U);
  drain_rpc(2300U);
  LATTER_CHECK(rs.nav.gui_page == PageId::MUSIC);
  LATTER_CHECK(std::find(s_area_cmds.begin(), s_area_cmds.end(), UiHostCmd::SPEAKER_CONTROL_TARGET) ==
               s_area_cmds.end());
  LATTER_CHECK(rpc_box.published.empty());
  LATTER_CHECK(ms.title == "TV Movie Audio");

  // 3. Tapping the left icon (AREA_CARD_TOGGLE) on a media_player emits area_action "toggle" (play/pause)
  rpc_box.published.clear();
  rs.nav.gui_page = PageId::AREA;
  rs.nav.area_subpage = AreaSubpage::OVERVIEW;
  render_area_frame();
  const HitRegion *tog_hit = frame_hits().find_by_id(WidgetId::AREA_CARD_TOGGLE);
  LATTER_CHECK(tog_hit != nullptr);
  eng.on_touch_down(tog_hit->cx(), tog_hit->cy(), 3000U);
  eng.on_touch_up(3040U);
  drain_rpc(3100U);
  std::string tog_wire;
  for (const auto &f : rpc_box.published) tog_wire += f;
  LATTER_CHECK(tog_wire.find("\"s\":\"area_action\"") != std::string::npos);
  LATTER_CHECK(tog_wire.find("\"entity_id\":\"media_player.living_room_tv\"") != std::string::npos);
  LATTER_CHECK(tog_wire.find("\"action\":\"toggle\"") != std::string::npos);

  // 4. Control-only switch does not request local DAC mute while local playback is active; transfer does
  ms.active_speaker = ms.local_speaker;
  rs.music.active_speaker_entity = ms.local_speaker;
  ms.is_local = true;
  rs.music.target_speaker_id = "media_player.living_room_tv";
  rs.music.target_speaker_name = "Living Room TV";
  LATTER_CHECK(!speaker_activate_selected(/*transfer_queue=*/false, /*local_mp_playing=*/true, false, 4000U));
  ms.active_speaker = ms.local_speaker;
  rs.music.active_speaker_entity = ms.local_speaker;
  ms.is_local = true;
  rs.music.target_speaker_id = "media_player.living_room_tv";
  LATTER_CHECK(speaker_activate_selected(/*transfer_queue=*/true, /*local_mp_playing=*/true, false, 4100U));

  latter_rpc::bind_sensor_sink(static_cast<MockRpcOutbox *>(nullptr));
  eng.runtime().exec_cmd = nullptr;
}

void test_home_screen_customizability_and_settings() {
  using namespace latter_ui;
  using namespace latter_core;

  reset_runtime_state();
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  auto &ws = weather_state();
  auto &eng = touch_engine();

  prefs = PersistedDevicePrefs{};
  ws.condition = "sunny";
  ws.temperature = 72.0f;
  ws.sun_state = "above_horizon";
  ws.temp_unit = "F";

  rs.nav.gui_page = PageId::HOME;
  rs.nav.home_subpage = HomeSubpage::MAIN;
  rs.nav.display_power_state = 0;
  rs.nav.home_settings_page = 1;
  eng.runtime().panel_is_on = true;
  eng.runtime().time_valid = true;
  eng.runtime().time_year = 2026;
  eng.runtime().time_month = 11;
  eng.runtime().time_day = 24;
  eng.runtime().time_hour = 10;
  eng.runtime().time_minute = 30;
  eng.runtime().time_day_of_week = 3;

  std::vector<uint16_t> fb(static_cast<size_t>(SCREEN_W) * static_cast<size_t>(SCREEN_H), 0U);
  Rgb565Surface surf = bind_host_surface(fb.data(), SCREEN_W, SCREEN_H, 1);
  auto render_home = [&](uint32_t now_ms, const char *sim_opt = "Live (Home Assistant)") {
    UiFrameContext ctx{};
    ctx.now_ms = now_ms;
    ctx.cx = SCREEN_CX;
    ctx.cy = SCREEN_CY;
    ctx.col_bg = UiColor(0, 0, 0);
    ctx.col_white = UiColor(255, 255, 255);
    ctx.col_black = UiColor(0, 0, 0);
    ctx.col_slate = UiColor(160, 160, 160);
    ctx.col_gray = UiColor(90, 90, 90);
    ctx.col_gold = UiColor(235, 195, 70);
    ctx.col_green = UiColor(70, 200, 120);
    ctx.theme_primary = UiColor(107, 142, 158);
    ctx.theme_secondary = UiColor(192, 192, 192);
    ctx.font_small = UiFontView::make_host_font(16);
    ctx.font_date = UiFontView::make_host_font(20);
    ctx.font_time = UiFontView::make_host_font(48);
    ctx.font_micro = UiFontView::make_host_font(12);
    ctx.home_weather_bg = prefs.weather_background;
    ctx.home_wake_greeting = false;
    ctx.home_sun_arc = prefs.home_sun;
    ctx.home_moon_phase = prefs.home_moon;
    ctx.weather_sim_opt = sim_opt;
    ctx.now.year = 2026;
    ctx.now.month = 11;
    ctx.now.day_of_month = 24;
    ctx.now.hour = 10;
    ctx.now.minute = 30;
    ctx.now.day_of_week = 3;
    ctx.now.timestamp = 1795551300LL;
    dirty_band() = DirtyBandTracker{};
    (void)dirty_band().begin_frame(PageId::HOME, rs.nav.home_subpage, 0U, true);
    frame_hits().clear();
    frame_base_begin();
    set_clip_y_band(0, SCREEN_H - 1);
    render_active_page(surf, ctx);
  };

  // 1. Default Home registers Date, Time, and Weather hit targets
  render_home(1000U);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_DATE_HERO) != nullptr);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_TIME_READOUT) != nullptr);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_WEATHER_HERO) != nullptr);

  // 2. Hiding Date / Time / Weather omits their hit targets cleanly
  prefs.home_show_date = false;
  prefs.home_show_time = false;
  prefs.home_show_weather = false;
  render_home(1100U);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_DATE_HERO) == nullptr);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_TIME_READOUT) == nullptr);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_WEATHER_HERO) == nullptr);

  // 3. Showing Date / Time / Weather with tap action "None" omits hit regions too
  prefs.home_show_date = true;
  prefs.home_show_time = true;
  prefs.home_show_weather = true;
  prefs.home_date_tap_action = 2U;     // None
  prefs.home_clock_tap_action = 2U;    // None
  prefs.home_weather_tap_action = 2U;  // None
  render_home(1200U);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_DATE_HERO) == nullptr);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_TIME_READOUT) == nullptr);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_WEATHER_HERO) == nullptr);

  // 4. Weather tap "5-Day Forecast" opens Weather Forecast subpage (1), "Current Weather" opens Current (0)
  prefs.home_weather_tap_action = 1U;
  render_home(1300U);
  const HitRegion *w_hit = frame_hits().find_by_id(WidgetId::HOME_WEATHER_HERO);
  LATTER_CHECK(w_hit != nullptr);
  eng.on_touch_down(w_hit->cx(), w_hit->cy(), 1400U);
  eng.on_touch_up(1440U);
  LATTER_CHECK(rs.nav.gui_page == PageId::WEATHER);
  LATTER_CHECK(rs.nav.sky_weather_subpage == 1);

  rs.nav.gui_page = PageId::HOME;
  rs.nav.home_subpage = HomeSubpage::MAIN;
  prefs.home_weather_tap_action = 0U;
  render_home(2000U);
  w_hit = frame_hits().find_by_id(WidgetId::HOME_WEATHER_HERO);
  LATTER_CHECK(w_hit != nullptr);
  eng.on_touch_down(w_hit->cx(), w_hit->cy(), 2100U);
  eng.on_touch_up(2140U);
  LATTER_CHECK(rs.nav.gui_page == PageId::WEATHER);
  LATTER_CHECK(rs.nav.sky_weather_subpage == 0);

  // 4b. HOME_QUICK_BTN routes strictly from rendered celestial orb (Day=Sun subpage 2, Night=Moon subpage 3),
  //     even when Weather Simulator overrides the live sun_state (e.g., Windy (Day) tested at night).
  rs.nav.gui_page = PageId::HOME;
  rs.nav.home_subpage = HomeSubpage::MAIN;
  ws.sun_state = "below_horizon";  // Real night
  ws.moon_phase = "full_moon";
  prefs.weather_background = true;
  prefs.home_sun = true;
  prefs.home_moon = true;
  render_home(2300U, "Windy (Day)");
  const HitRegion *orb_hit = frame_hits().find_by_id(WidgetId::HOME_QUICK_BTN);
  LATTER_CHECK(orb_hit != nullptr && orb_hit->param == 0);
  eng.on_touch_down(orb_hit->cx(), orb_hit->cy(), 2400U);
  eng.on_touch_up(2440U);
  LATTER_CHECK(rs.nav.gui_page == PageId::WEATHER);
  LATTER_CHECK(rs.nav.sky_weather_subpage == 2);  // Sun subpage, NOT Moon!

  rs.nav.gui_page = PageId::HOME;
  rs.nav.home_subpage = HomeSubpage::MAIN;
  ws.sun_state = "above_horizon";  // Real day
  render_home(2600U, "Windy (Night)");
  orb_hit = frame_hits().find_by_id(WidgetId::HOME_QUICK_BTN);
  LATTER_CHECK(orb_hit != nullptr && orb_hit->param == 1);
  eng.on_touch_down(orb_hit->cx(), orb_hit->cy(), 2700U);
  eng.on_touch_up(2740U);
  LATTER_CHECK(rs.nav.gui_page == PageId::WEATHER);
  LATTER_CHECK(rs.nav.sky_weather_subpage == 3);  // Moon subpage, NOT Sun!

  // Pouring / Heavy Rain (Day & Night) hide celestial orb and omit HOME_QUICK_BTN.
  rs.nav.gui_page = PageId::HOME;
  rs.nav.home_subpage = HomeSubpage::MAIN;
  for (const char *pour_opt : {"Pouring / Heavy Rain (Day)", "Pouring / Heavy Rain (Night)"}) {
    render_home(2800U, pour_opt);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_QUICK_BTN) == nullptr);
  }

  // 5. Dynamic on-device Home Settings filtering, viewport-solved pagination, and 3-way cycling through "None"
  prefs = PersistedDevicePrefs{};
  HomeSettingsItemId items[12]{};
  const int per_pg = std::clamp(layout().list_rows, 4, kMaxListSlots);
  const int full_cnt = collect_home_settings_items(prefs, items);
  LATTER_CHECK(full_cnt == (LATTER_HAS_BATTERY ? 12 : 11));
  const int exp_full_pages = std::max(1, (full_cnt + per_pg - 1) / per_pg);
  LATTER_CHECK(home_settings_page_count(prefs) == exp_full_pages);

  prefs.home_show_time = false;
  prefs.home_show_date = false;
  prefs.home_show_weather = false;
  prefs.weather_background = false;
  const int min_cnt = collect_home_settings_items(prefs, items);
  LATTER_CHECK(min_cnt == (LATTER_HAS_BATTERY ? 7 : 6));
  const int exp_min_pages = std::max(1, (min_cnt + per_pg - 1) / per_pg);
  LATTER_CHECK(home_settings_page_count(prefs) == exp_min_pages);

  // Re-enable all and step through Home Settings pages + cycle Time Tap -> Clock Face -> None -> Time App
  prefs = PersistedDevicePrefs{};
  rs.nav.gui_page = PageId::HOME;
  rs.nav.home_subpage = HomeSubpage::SETTINGS;
  rs.nav.home_settings_page = 1;
  render_home(3000U);
  const HitRegion *time_tap_hit = frame_hits().find_by_id(WidgetId::LIST_SLOT_BASE + 1);
  LATTER_CHECK(time_tap_hit != nullptr &&
               time_tap_hit->param == static_cast<int16_t>(HomeSettingsItemId::TIME_TAP));
  eng.on_touch_down(time_tap_hit->cx(), time_tap_hit->cy(), 3100U);
  eng.on_touch_up(3140U);
  LATTER_CHECK(prefs.home_clock_tap_action == 1U);
  eng.on_touch_down(time_tap_hit->cx(), time_tap_hit->cy(), 3700U);
  eng.on_touch_up(3740U);
  LATTER_CHECK(prefs.home_clock_tap_action == 2U);  // None
  eng.on_touch_down(time_tap_hit->cx(), time_tap_hit->cy(), 4300U);
  eng.on_touch_up(4340U);
  LATTER_CHECK(prefs.home_clock_tap_action == 0U);  // Back to Time App

  // Chevron down steps through all viewport-solved pages (1 -> 2, and -> 3 if needed)
  if (exp_full_pages >= 2) {
    const HitRegion *chev_dn = frame_hits().find_by_id(WidgetId::CHEVRON_DOWN);
    LATTER_CHECK(chev_dn != nullptr);
    eng.on_touch_down(chev_dn->cx(), chev_dn->cy(), 4900U);
    eng.on_touch_up(4940U);
    LATTER_CHECK(rs.nav.home_settings_page == 2);
    if (exp_full_pages >= 3) {
      render_home(5000U);
      chev_dn = frame_hits().find_by_id(WidgetId::CHEVRON_DOWN);
      LATTER_CHECK(chev_dn != nullptr);
      eng.on_touch_down(chev_dn->cx(), chev_dn->cy(), 5500U);
      eng.on_touch_up(5540U);
      LATTER_CHECK(rs.nav.home_settings_page == 3);
    }
  }
}

void test_phase1_touch_unification_and_bugfixes() {
  using namespace latter_ui;
  using namespace latter_core;

  reset_runtime_state();
  reset_all_v3_rev_guards();
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  prefs = PersistedDevicePrefs{};
  auto &eng = touch_engine();

  static int s_wake_calls = 0;
  s_wake_calls = 0;
  eng.runtime().panel_is_on = true;
  eng.runtime().usb_connected = false;
  eng.runtime().exec_cmd = +[](UiHostCmd cmd, int, int) {
    if (cmd == UiHostCmd::WAKE_TO_ACTIVE) s_wake_calls++;
  };

  // 1. B4: Visible Screensaver (display_power_state == 1) ALWAYS wakes on a single tap,
  //    even when tap_to_wake == Disabled or Double Tap; Screen Off (== 2) stays gated!
  prefs.tap_to_wake = 3U;  // Disabled
  rs.nav.display_power_state = 1;
  eng.on_touch_down(SCREEN_CX, SCREEN_H / 2, 1000U);
  eng.on_touch_up(1040U);
  LATTER_CHECK(s_wake_calls == 1);

  s_wake_calls = 0;
  rs.nav.display_power_state = 2;
  eng.on_touch_down(SCREEN_CX, SCREEN_H / 2, 1500U);
  eng.on_touch_up(1540U);
  LATTER_CHECK(s_wake_calls == 0);

  // 2. Calculator Pass-2 halo hit dispatches cleanly via wid - CALC_KEY_BASE.
  reset_calculator_state();
  rs.nav.display_power_state = 0;
  rs.nav.gui_page = PageId::TOOLS;
  rs.nav.tools_subpage = ToolsSubpage::CALCULATOR;
  frame_hits().clear();
  HostSurfaceFixture fix{};
  fix.ctx.now_ms = 2000U;
  dirty_band() = DirtyBandTracker{};
  reset_clip_y_band();
  fix.fill(0U);
  render_active_page(fix.surf, fix.ctx);
  const HitRegion *calc_k7 = frame_hits().find_by_id(static_cast<uint16_t>(WidgetId::CALC_KEY_BASE + 4));  // '7'
  LATTER_CHECK(calc_k7 != nullptr);
  const int halo_x = (calc_k7->hit_pad >= 2 && calc_k7->x0 >= 2)
                         ? (static_cast<int>(calc_k7->x0) - 2)
                         : calc_k7->cx();
  eng.on_touch_down(halo_x, calc_k7->cy(), 2100U);
  eng.on_touch_up(2140U);
  LATTER_CHECK(std::string_view(calculator_display_text()) == "7");

  // 3. Home Activity Overlay two-zone hit regions (body opens app; right pill fires action).
  rs.nav.gui_page = PageId::HOME;
  rs.nav.home_subpage = HomeSubpage::ACTIVITY_OVERLAY;
  home_activity_overlay_page() = 0;
  media_state().playing = true;
  media_state().state = "playing";
  media_state().title = "Blue in Green";
  media_state().artist = "Miles Davis";
  media_state().active_speaker = "media_player.living_room";
  media_state().is_local = false;
  fix.ctx.local_media_playing = true;
  frame_hits().clear();
  fix.fill(0U);
  render_active_page(fix.surf, fix.ctx);
  const HitRegion *act_body = frame_hits().find_by_id(WidgetId::LIST_SLOT_BASE);
  const HitRegion *act_btn = frame_hits().find_by_id(WidgetId::HOME_ACT_OVERLAY_ACTION_BASE);
  LATTER_CHECK(act_body != nullptr && act_btn != nullptr);
  LATTER_CHECK(act_body->x1 <= act_btn->x0);
  eng.on_touch_down(act_body->cx(), act_body->cy(), 2600U);
  eng.on_touch_up(2640U);
  LATTER_CHECK(rs.nav.gui_page == PageId::MUSIC);

  // 4. Blocks & Snake kinematic swipes through eval_kinematic_*_swipe.
  rs.nav.gui_page = PageId::GAMES;
  rs.nav.active_game = 5;  // Blocks
  frame_hits().clear();
  game_input().clear();
  const int sw_px = layout().swipe_min() + 14;
  GameInputEvent gev{};
  eng.on_touch_down(SCREEN_CX, SCREEN_H / 2, 3000U);
  eng.on_touch_move(SCREEN_CX + sw_px, SCREEN_H / 2, 3040U);
  eng.on_touch_up(3080U);
  LATTER_CHECK(game_input().pop(gev) && gev.cmd == GameInputCmd::RIGHT);

  eng.on_touch_down(SCREEN_CX, SCREEN_H / 2, 3300U);
  eng.on_touch_move(SCREEN_CX, SCREEN_H / 2 + sw_px, 3340U);
  eng.on_touch_up(3380U);
  LATTER_CHECK(game_input().pop(gev) && gev.cmd == GameInputCmd::HARD_DROP);

  // Snake release-flick fallback (quick flick commits on release via eval_kinematic_horiz_swipe).
  rs.nav.active_game = 6;
  game_input().clear();
  const int flick_px = std::max(layout().flick_min(), layout().swipe_min() - 2);
  eng.on_touch_down(SCREEN_CX, SCREEN_H / 2, 3900U);
  touch_gesture().record_move(SCREEN_CX - flick_px, SCREEN_H / 2, 3915U, LATTER_PANEL_PPI);
  eng.on_touch_up(3920U);
  LATTER_CHECK(game_input().pop(gev) && gev.cmd == GameInputCmd::LEFT);

  // 5. System Mode 9 (Settings -> Music, 6 items) vertical swipe paging: 1 page when list_rows >= 6, else 1 -> 2.
  rs.nav.gui_page = PageId::SYSTEM;
  rs.nav.system_menu_mode = 9;
  rs.music.settings_page = 1;
  frame_hits().clear();
  eng.on_touch_down(SCREEN_CX, SCREEN_H / 2, 4200U);
  eng.on_touch_move(SCREEN_CX, SCREEN_H / 2 - sw_px, 4240U);
  eng.on_touch_up(4280U);
  const int exp_sys_music_pg = (std::clamp(layout().list_rows, 4, kMaxListSlots) >= 6) ? 1 : 2;
  LATTER_CHECK(rs.music.settings_page == exp_sys_music_pg);

  // 6. on_input_event logs WakeReason::CROWN (not TOUCH) on dark wake.
  rs.nav.display_power_state = 2;
  eng.on_input_event("select", 0, 4600U);
  LATTER_CHECK(wake_stats().pending == WakeReason::CROWN);

  eng.runtime().exec_cmd = nullptr;
}

}  // namespace

void run_touch_engine_tests() {
  test_touch_hal_kinematic_gestures_and_d049_d050();
  test_quick_actions_d074();
  test_time_app_world_clocks_alarms_and_horology();
  test_live_touch_engine_end_to_end_5geom();
  test_rotary_navigation_and_glass_style_display_setting();
  test_area_media_player_switches_active_speaker();
  test_home_screen_customizability_and_settings();
  test_phase1_touch_unification_and_bugfixes();
}

}  // namespace latter_test
