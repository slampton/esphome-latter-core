#include "test_harness.h"

namespace latter_test {

namespace {

void test_music_remote_override_and_volume_feedback() {
  latter_core::clear_remote_play_override();

  LATTER_CHECK(latter_core::is_music_playing_effective(true, false, "playing", 1000U));
  LATTER_CHECK(latter_core::is_music_playing_effective(true, false, "buffering", 1000U));
  LATTER_CHECK(!latter_core::is_music_playing_effective(true, true, "idle", 1000U));
  LATTER_CHECK(latter_core::is_music_playing_effective(false, true, "idle", 1000U));

  latter_core::set_remote_play_override(false, 2000U);
  LATTER_CHECK(!latter_core::is_music_playing_effective(true, false, "playing", 3500U));
  LATTER_CHECK(latter_core::is_music_playing_effective(true, false, "playing", 4600U));

  const std::string touch_cpp = read_repo_glob("../../components/latter_ui", "touch_", ".cpp");
  LATTER_CHECK(touch_cpp.find("set_remote_play_override(") == std::string::npos);
  LATTER_CHECK(touch_cpp.find("step_music_volume_feedback(") != std::string::npos);
  LATTER_CHECK(touch_cpp.find("touch_router().note_tap(") == std::string::npos);
  LATTER_CHECK(touch_cpp.find(".last_tap_ms") == std::string::npos);
  LATTER_CHECK(touch_cpp.find("{\"Full Screen\", \"Letterbox\", \"Standard\", \"None\"}") != std::string::npos);

  const std::string music_cpp = read_repo_file("../../components/latter_ui/page_music.cpp");
  const std::string remote_cpp = read_repo_file("../../components/latter_ui/page_remote.cpp");
  const std::string gui_yaml = read_repo_file("../../guis/universal/gui.yaml");
  const std::string state_h = read_repo_glob("../../include/state", "", ".h");
  LATTER_CHECK(music_cpp.find("art_mode == \"None\"") != std::string::npos);
  LATTER_CHECK(music_cpp.find("render_page_wallpaper_base(") != std::string::npos);
  LATTER_CHECK(remote_cpp.find("render_page_wallpaper_base(") == std::string::npos);
  LATTER_CHECK(gui_yaml.find("render_page_wallpaper_base(") != std::string::npos);
  LATTER_CHECK(latter_ui::resolve_backdrop_mode(
                   latter_core::PageId::REMOTE, 0, true, true, false, true) ==
               latter_ui::BackdropMode::CustomWallpaper);
  LATTER_CHECK(state_h.find("kPrefOptsAlbumArtStyle") != std::string::npos);
  LATTER_CHECK(latter_core::kPrefOptsAlbumArtStyle.size() == 4U &&
               latter_core::kPrefOptsAlbumArtStyle[0] == "Full Screen" &&
               latter_core::kPrefOptsAlbumArtStyle[1] == "Letterbox" &&
               latter_core::kPrefOptsAlbumArtStyle[2] == "Standard" &&
               latter_core::kPrefOptsAlbumArtStyle[3] == "None");

  latter_ui::touch_router().note_tap(10000U);
  LATTER_CHECK(!latter_ui::touch_router().allow_tap(10080U, latter_ui::TOUCH_DEBOUNCE_BUTTON));
  LATTER_CHECK(latter_ui::touch_router().allow_tap(10200U, latter_ui::TOUCH_DEBOUNCE_BUTTON));

  latter_core::MusicRuntimeState m{};
  LATTER_CHECK(latter_core::step_music_volume_feedback(1, 5.0f, true, 0.55f, 5000U, m) == 60);
  LATTER_CHECK(m.action_feedback == "Volume: 60%" && m.feedback_timer == 5000U);
  LATTER_CHECK(latter_core::step_music_volume_feedback(-1, 5.0f, true, 0.12f, 5200U, m) == 10);
  LATTER_CHECK(m.action_feedback == "Volume: 10%");

  latter_core::remote_vol_pct() = 55;
  LATTER_CHECK(latter_core::step_music_volume_feedback(1, 5.0f, false, 0.55f, 6000U, m) == 60);
  LATTER_CHECK(latter_core::remote_vol_pct() == 60 && m.action_feedback == "Volume: 60%");
  LATTER_CHECK(latter_core::step_music_volume_feedback(1, 5.0f, false, 0.55f, 6200U, m) == 65);
  LATTER_CHECK(latter_core::remote_vol_pct() == 65 && m.action_feedback == "Volume: 65%");
}

void test_phase3_weather_sky_backdrop_and_home_scene() {
  using latter_ui::BackdropMode;
  using latter_ui::resolve_backdrop_mode;

  LATTER_CHECK(resolve_backdrop_mode(1, 0, true, true, false, true) == BackdropMode::LiveWeatherSky);
  LATTER_CHECK(resolve_backdrop_mode(5, 0, true, true, false, true) == BackdropMode::LiveWeatherSky);
  LATTER_CHECK(resolve_backdrop_mode(1, 0, false, true, false, true) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(5, 0, false, true, false, true) == BackdropMode::LiveWeatherSky);
  LATTER_CHECK(resolve_backdrop_mode(1, 0, false, false, false, false, true) == BackdropMode::DayNightSky);
  LATTER_CHECK(resolve_backdrop_mode(0, 0, true, true, false, true, true) == BackdropMode::SolidBlack);
  LATTER_CHECK(resolve_backdrop_mode(3, 0, true, true, false, true, true) == BackdropMode::SolidBlack);

  static_assert(!std::is_constructible_v<latter_ui::UiStrView, std::string &&>);
  static_assert(std::is_trivially_copyable_v<latter_ui::UiStrView>);
  const std::string live_s = "Thunderstorm (Night)";
  const latter_ui::UiStrView v_str{live_s};
  LATTER_CHECK(v_str == live_s && v_str.find("Night") != std::string_view::npos);

  latter_ui::FrameHitTable ht{};
  ht.clear();
  latter_ui::register_home_activity_hit_regions(ht, 233, 270, 380, 68, false, true, 3, -1, 56, 52);
  LATTER_CHECK(ht.count == 3 && ht.focus_count() == 3);
  LATTER_CHECK(ht.hit_test(140, 270, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::HOME_ACT_ROW_0);
  LATTER_CHECK(ht.hit_test(423 - 80, 270, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::HOME_ACT_ACTION_0);
  LATTER_CHECK(ht.hit_test(423 - 20, 270, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::HOME_ACT_EXPAND);

  latter_core::reset_all_v3_rev_guards();
  latter_core::area_ctx_state() = latter_core::AreaContextState{};
  std::vector<std::string> ac_tok(static_cast<size_t>(latter_core::AreaCtxField::COUNT), "");
  ac_tok[static_cast<size_t>(latter_core::AreaCtxField::AREA_ID)] = "kitchen";
  ac_tok[static_cast<size_t>(latter_core::AreaCtxField::AREA_NAME)] = "Kitchen";
  ac_tok[static_cast<size_t>(latter_core::AreaCtxField::AREA_ICON)] = "mdi:countertop";
  LATTER_CHECK(latter_core::parse_v3_area_ctx_payload(build_v3_packet(500, 1, ac_tok)));
  LATTER_CHECK(std::string_view(latter_core::resolve_area_dock_icon(latter_core::area_ctx_state())) == "mdi:countertop");

  auto &prefs = latter_core::persisted_prefs();
  auto &rs = latter_core::runtime_state();
  prefs.wake_greeting = true;
  rs.nav.wake_greeting_pending = false;
  rs.nav.display_power_state = 2;
  rs.nav.standby_entered_ms = 1000U;
  latter_core::note_wake_from_standby(60000U);
  LATTER_CHECK(!rs.nav.wake_greeting_pending);
  rs.nav.standby_entered_ms = 1000U;
  latter_core::note_wake_from_standby(1000U + latter_core::kStandbyGreetingIdleMs);
  LATTER_CHECK(rs.nav.wake_greeting_pending);

  const std::string comp_h = read_repo_files({
      "../../include/ui_frame_context.h",
      "../../include/ui_compositor.h",
  });
  const std::string comp_cpp = read_repo_file("../../components/latter_ui/ui_compositor.cpp");
  const std::string weather_cpp = read_repo_file("../../components/latter_ui/page_weather.cpp");
  const std::string home_cpp = read_repo_file("../../components/latter_ui/page_home.cpp");
  const std::string gui_yaml = read_repo_file("../../guis/universal/gui.yaml");

  LATTER_CHECK(comp_h.find("struct UiStrView") != std::string::npos);
  LATTER_CHECK(comp_h.find("resolve_backdrop_mode(") != std::string::npos);
  LATTER_CHECK(weather_cpp.find("SkyBaseCache") != std::string::npos);
  LATTER_CHECK(weather_cpp.find("WeatherSkyFadeState") != std::string::npos);
  LATTER_CHECK(home_cpp.find("register_home_activity_hit_regions(") != std::string::npos);
  LATTER_CHECK(gui_yaml.find("resolve_backdrop_mode(") != std::string::npos);
  LATTER_CHECK(comp_cpp.find("weather_sky_motion_stepping(") != std::string::npos);
  LATTER_CHECK(latter_ui::mdi_catalog_count() == 7447U);
}

void test_home_activity_card_d045() {
  using namespace latter_ui;
  using namespace latter_core;

  LATTER_CHECK(next_alarm_minutes_until(23, 30, true, 0, 22, 0, 3) == 90);
  LATTER_CHECK(next_alarm_minutes_until(3, 0, true, 3, 22, 0, 3) == 300);
  LATTER_CHECK(next_alarm_minutes_until(4, 0, true, 1, 23, 0, 1) == 300);
  LATTER_CHECK(next_alarm_minutes_until(4, 0, true, 2, 23, 0, 6) == 300);

  const HomeActivityItem off_only[2] = {
      {4, 1, false, false, false, "Alarm • 7:00 AM"},
      {5, 0, false, false, false, "FRI 10:00 • Dentist"},
  };
  LATTER_CHECK(resolve_home_activity_view(off_only, 2, 0U, true).visible_rows == 0);

  const HomeActivityItem multi_pin[4] = {
      {4, 1, false, true, true, "Alarm • 7:00 AM"},
      {2, 0, true, true, true, "Stopwatch • 01:12.4"},
      {4, 0, true, true, true, "Alarm • Snoozed"},
      {1, 1, true, true, true, "Timer 2 • 08:00"},
  };
  const HomeActivityView mp0 = resolve_home_activity_view(multi_pin, 4, 0U, true);
  const HomeActivityView mp1 = resolve_home_activity_view(multi_pin, 4, kHomeActivityRotateMs, true);
  LATTER_CHECK(mp0.pinned && mp0.top == 3 && mp0.bottom == 0);
  LATTER_CHECK(mp1.pinned && mp1.top == 3 && mp1.bottom == 1);

  const HomeActivityItem db_vs_timer[2] = {
      {1, 0, true, true, true, "Timer 1 • 00:15"},
      {6, 0, true, true, true, "Doorbell • Ringing"},
  };
  LATTER_CHECK(resolve_home_activity_view(db_vs_timer, 2, 0U, true).top == 1);
}

void test_home_hub_v6_alarm_caps_and_tools_pagination() {
  using namespace latter_ui;
  using namespace latter_core;

  reset_runtime_state();
  int tabs[kMaxHomeHubTabs]{};
  LATTER_CHECK(parse_home_categories_csv(kDefaultHomeCategories, tabs, kMaxHomeHubTabs) == 5);

  const uint16_t caps_disarm_only = kAlarmCapCodeDisarm | kAlarmCapArmHome | kAlarmCapArmAway;
  LATTER_CHECK(evaluate_perimeter_pin_prompt("allow", "home_hub.alarm", "disarm", 0, caps_disarm_only, false));
  LATTER_CHECK(!evaluate_perimeter_pin_prompt("require_pin", "home_hub.alarm", "arm_away", 0, caps_disarm_only, false));
  LATTER_CHECK(evaluate_perimeter_action_blocked("block", "home_hub.alarm", "disarm", 0));

  MockRpcOutbox rpc_box{};
  latter_rpc::clear_pin_session_key();
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_core::emit_alarm_action("disarm", "2468", &rpc_box);
  for (const auto &f : rpc_box.published) LATTER_CHECK(f.find("2468") == std::string::npos);

  LATTER_CHECK(latter_rpc::set_pin_session_key(
      "01020304", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"));
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  rpc_box.published.clear();
  latter_core::emit_alarm_action("disarm", "2468", &rpc_box);
  std::string alarm_joined;
  for (const auto &f : rpc_box.published) alarm_joined += f;
  LATTER_CHECK(alarm_joined.find("\"code\":\"v1:01020304:") != std::string::npos);
  LATTER_CHECK(alarm_joined.find("\"2468\"") == std::string::npos);
  latter_rpc::clear_pin_session_key();

  static_assert(tools_launcher_item_count_for(true, true) == 6);
  static_assert(tools_launcher_page_count(6) == 1);
  static_assert(tools_launcher_page_count(7) == 2);

  const std::string wx_src = read_repo_file("../../components/latter_ui/page_weather.cpp");
  LATTER_CHECK(wx_src.find("enum class RainTier") != std::string::npos);
  LATTER_CHECK(wx_src.find("draw_soft_snowflake_q8(") != std::string::npos);
}

void test_calendar_app_d058() {
  using namespace latter_ui;
  using namespace latter_core;
  reset_runtime_state();
  reset_all_v3_rev_guards();

  static_assert(FrameHitTable::kMaxRegions == 128U);
  static_assert(is_leap_year(2028) && !is_leap_year(2027));
  LATTER_CHECK(parse_ymd_iso("2026-11-17") == 20261117);
  LATTER_CHECK(weekday_sun0_from_ymd(20261117) == 2);
  LATTER_CHECK(add_months_ymd(20280131, 1) == 20280229);

  std::vector<std::string> cal_tok(static_cast<size_t>(CalendarField::COUNT), "");
  cal_tok[static_cast<size_t>(CalendarField::SUMMARY)] = "TODAY 14:00|Design Review|Example HQ|0";
  cal_tok[static_cast<size_t>(CalendarField::EVENT_COUNT)] = "1";
  cal_tok[static_cast<size_t>(CalendarField::CALENDARS_META)] =
      std::string("calendar.family|Family|0|1|1") + kProtocolRecordSep + "calendar.holidays|US Holidays|3|0|0";
  LATTER_CHECK(parse_v3_calendar_payload(build_v3_packet(12, 1, cal_tok)));

  auto make_ev_rec = [](const char *s_ymd, const char *e_ymd, int s_min, int e_min,
                        int all_day, int cal_i, const char *uid, const char *rec_id,
                        const char *title, const char *loc, const char *desc) {
    std::string r;
    r += s_ymd; r.push_back(kProtocolFieldSep);
    r += e_ymd; r.push_back(kProtocolFieldSep);
    r += std::to_string(s_min); r.push_back(kProtocolFieldSep);
    r += std::to_string(e_min); r.push_back(kProtocolFieldSep);
    r += std::to_string(all_day); r.push_back(kProtocolFieldSep);
    r += std::to_string(cal_i); r.push_back(kProtocolFieldSep);
    r += uid; r.push_back(kProtocolFieldSep);
    r += rec_id; r.push_back(kProtocolFieldSep);
    r += title; r.push_back(kProtocolFieldSep);
    r += loc; r.push_back(kProtocolFieldSep);
    r += desc;
    return r;
  };

  const std::string chunk0 =
      make_ev_rec("2026-11-17", "2026-11-17", -1, -1, 1, 1, "u1", "", "Release Cut", "", "Lockstep") +
      kProtocolRecordSep +
      make_ev_rec("2026-11-17", "2026-11-17", 540, 600, 0, 0, "u2", "", "Morning Sync | Eng", "Lab", "");
  const std::string chunk1 =
      make_ev_rec("2026-11-17", "2026-11-17", 840, 900, 0, 0, "u3", "r3", "Design Review", "Example HQ", "Bring prototype");

  auto &store = calendar_window_store();
  reset_calendar_window_store();
  LATTER_CHECK(!apply_calendar_view_action(8, 0, 2, "2026-10-15", "2027-01-20", chunk0));
  LATTER_CHECK(apply_calendar_view_action(8, 1, 2, "2026-10-15", "2027-01-20", chunk1));
  LATTER_CHECK(store.count == 3);
  LATTER_CHECK(std::string_view(store.events[1].title) == "Morning Sync | Eng");

  auto &rt = runtime_state();
  rt.nav.gui_page = PageId::CALENDAR;
  rt.cal.drilldown = CalDrilldown::ADD_STEP1;
  start_calendar_dictation(1U, 5000U, rt);
  LATTER_CHECK(cal_dictation_active(rt));
  LATTER_CHECK(finish_calendar_dictation("  dentist appointment. ", 5800U, rt));
  LATTER_CHECK(rt.cal.draft_title == "Dentist appointment");
}

void test_cameras_app_and_live_video_d066() {
  using namespace latter_ui;
  using namespace latter_core;
  reset_runtime_state();

  {
    MjpegFrameParser parser(64U);
    parser.set_boundary("multipart/x-mixed-replace; boundary=latterframe");
    std::vector<uint8_t> jpeg1 = {0xFF, 0xD8, 0x11, 0x22, 0x33, 0xFF, 0xD9};
    std::string stream = "--latterframe\r\nContent-Type: image/jpeg\r\nContent-Length: 7\r\n\r\n";
    stream.append(reinterpret_cast<const char *>(jpeg1.data()), jpeg1.size());
    stream.append("\r\n--latterframe\r\nContent-Length: 200\r\n\r\n");
    stream.append(200, 'X');
    stream.append("\r\n--latterframe--\r\n");

    size_t count = 0;
    for (char ch : stream) {
      const uint8_t b = static_cast<uint8_t>(ch);
      parser.feed(&b, 1U, [&](const uint8_t *, size_t len) {
        LATTER_CHECK(len == 7U);
        ++count;
      });
    }
    LATTER_CHECK(count == 1U && parser.frames_oversized() == 1U);
  }

  {
    auto &vc = video_client();
    vc.reset_for_test();
    vc.start("http://ha.local/api/latter/camera_stream/desk?sig=abc", 384U, 224U);
    const uint16_t px[4] = {0xF800U, 0x07E0U, 0x001FU, 0xFFFFU};
    vc.inject_host_frame_for_test(px, 2, 2);
    LATTER_CHECK(vc.has_frame() && vc.state() == VideoStreamState::STREAMING);
    vc.stop();
    LATTER_CHECK(vc.state() == VideoStreamState::IDLE);
  }

  {
    uint16_t rw = 0, rh = 0;
    resolve_camera_stream_dims(466, 466, true, false, false, false, rw, rh);
    LATTER_CHECK(rw == 384U && rh == 224U);
    resolve_camera_stream_dims(466, 466, true, true, false, false, rw, rh);
    LATTER_CHECK(rw == 320U && rh == 256U);
    resolve_camera_stream_dims(360, 360, true, false, false, false, rw, rh);
    LATTER_CHECK(rw == 288U && rh == 160U);
    resolve_camera_stream_dims(360, 360, true, true, false, false, rw, rh);
    LATTER_CHECK(rw == 240U && rh == 192U);
    resolve_camera_stream_dims(320, 480, false, false, false, false, rw, rh, true);
    LATTER_CHECK(rw == 256U && rh == 480U);
    LATTER_CHECK(resolve_camera_stream_fps(false, false, false, true) == 2U);
    LATTER_CHECK(resolve_camera_stream_fps(true, false, false, true) == 12U);
  }

  LATTER_CHECK(!should_va_takeover_preempt(true, true, true, PageId::CAMERAS, false));
  LATTER_CHECK(should_va_takeover_preempt(true, true, false, PageId::CAMERAS, false));
}

}  // namespace

void run_pages_apps_tests() {
  test_music_remote_override_and_volume_feedback();
  test_phase3_weather_sky_backdrop_and_home_scene();
  test_home_activity_card_d045();
  test_home_hub_v6_alarm_caps_and_tools_pagination();
  test_calendar_app_d058();
  test_cameras_app_and_live_video_d066();
}

}  // namespace latter_test
