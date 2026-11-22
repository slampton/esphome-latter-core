#include "test_harness.h"

namespace latter_test {

namespace {

void populate_rich_host_state(latter_ui::UiFrameContext &ctx) {
  using namespace latter_ui;

  reset_runtime_state();
  reset_all_v3_rev_guards();
  reset_calendar_window_store();

  auto &prefs = persisted_prefs();
  prefs = PersistedDevicePrefs{};
  seed_hardware_sleep_defaults(prefs);
  prefs.alarms[0].enabled = true;
  prefs.alarms[0].hour = 7;
  prefs.alarms[0].minute = 30;
  prefs.alarms[0].recurrence = 0;

  auto &ac = area_ctx_state();
  ac = AreaContextState{};
  ac.epoch = 1U;
  ac.rev = 1U;
  ac.area_id = "living_room";
  ac.area_name = "Living Room";
  ac.area_icon = "mdi:sofa";
  ac.area_temp = 71.5f;
  ac.lights_on = 2;
  ac.covers_open = 1;
  ac.available_domains = "scene,light,cover,climate,fan,switch,media";
  ac.home_categories = kDefaultHomeCategories;
  ac.alarm_entity = "alarm_control_panel.home";
  ac.alarm_state = "disarmed";
  ac.alarm_caps = kAlarmCapDefault | kAlarmCapArmNight;
  ac.cameras_summary =
      std::string("camera.front_door|Front Door|porch|63|lock.front_door") + kProtocolRecordSep +
      "camera.patio|Patio|backyard|40|";
  ac.camera_stream_url = "http://ha.local/api/latter/camera_stream/dev?sig=test";
  ac.world_clocks_summary = "Asia/Tokyo|Kyoto|JST|540|0;Europe/London|London|GMT|0|0";

  auto &hh = home_hub_state();
  hh = HomeHubState{};
  hh.thermostat_count = 1;
  hh.thermostats[0].entity_id = "climate.living_room";
  hh.thermostats[0].name = "Living Room";
  hh.thermostats[0].area_id = "living_room";
  hh.thermostats[0].mode = "heat";
  hh.thermostats[0].target_temp = 71;
  hh.thermostats[0].current_temp = 69.5f;
  hh.thermostats[0].caps = kClimateCapDefault;
  hh.entry_count = 2;
  hh.entries[0].is_lock = true;
  hh.entries[0].entity_id = "lock.front_door";
  hh.entries[0].name = "Front Door";
  hh.entries[0].lock_state = LockState::LOCKED;
  hh.entries[1].is_lock = false;
  hh.entries[1].entity_id = "cover.garage";
  hh.entries[1].name = "Main Garage";
  hh.entries[1].garage_state = GarageState::CLOSED;
  hh.vacuum_count = 1;
  hh.vacuums[0].entity_id = "vacuum.main_floor";
  hh.vacuums[0].name = "Main Floor";
  hh.vacuums[0].state = "Docked";
  hh.vacuums[0].caps = kVacuumCapDefault;

  auto &ms = media_state();
  ms = MediaState{};
  ms.epoch = 1U;
  ms.rev = 1U;
  ms.state = "playing";
  ms.playing = true;
  ms.title = "Midnight City";
  ms.artist = "M83";
  ms.album = "Hurry Up, We're Dreaming";
  ms.speaker_name = "Living Room";
  ms.active_speaker = "media_player.living_room";
  ms.has_volume = true;
  ms.volume = 0.65f;
  ms.duration = 244.0f;
  ms.position = 48.0f;
  ms.position_updated_ms = 20000U;
  ms.media_caps = kMediaCapShuffleSet | kMediaCapRepeatSet;

  auto &wx = weather_state();
  wx = WeatherState{};
  wx.epoch = 1U;
  wx.rev = 1U;
  wx.condition = "partlycloudy";
  wx.temperature = 68.0f;
  wx.humidity = 52.0f;
  wx.wind_speed = 8.5f;
  wx.wind_bearing = 245.0f;
  wx.high = 72.0f;
  wx.low = 54.0f;
  wx.sun_state = "above_horizon";
  wx.sun_next_rising = "2026-11-21T06:52:00";
  wx.sun_next_setting = "2026-11-21T16:56:00";
  wx.moon_phase = "waxing_gibbous";
  wx.temp_unit = "F";
  wx.forecast_5d = "Sat|70|54|partlycloudy\x1e" "Sun|72|55|sunny\x1e" "Mon|66|51|rainy";

  auto &rem = remote_state();
  rem = RemoteState{};
  rem.epoch = 1U;
  rem.rev = 1U;
  rem.active_slot = 0;
  rem.configured_count = 1;
  rem.slots_summary = "Apple TV\x1e" "remote.apple_tv";

  auto &sc = shortcuts_state();
  sc = ShortcutsState{};
  sc.epoch = 1U;
  sc.rev = 1U;
  sc.live_received = true;
  sc.pinned_items.push_back(ShortcutItem{
      ShortcutKind::ROUTE, RouteId::WEATHER_MOON, "weather:moon", "", "Moon Phase", "mdi:moon-waning-crescent", false, -1});
  sc.pinned_items.push_back(ShortcutItem{
      ShortcutKind::ENTITY, RouteId::NONE, "light.living_lamp", "toggle", "Reading Lamp", "mdi:lamp", false, 1});

  auto &cal = calendar_state();
  cal = CalendarState{};
  cal.epoch = 1U;
  cal.rev = 1U;
  cal.count = 1;
  cal.summary = "15:00|Design Review|Lab|0";
  cal.calendars_meta = "calendar.family|Family|0|1|1";
  const std::string ev_chunk =
      std::string("2026-11-21") + kProtocolFieldSep + "2026-11-21" + kProtocolFieldSep +
      "900" + kProtocolFieldSep + "960" + kProtocolFieldSep + "0" + kProtocolFieldSep +
      "0" + kProtocolFieldSep + "u1" + kProtocolFieldSep + "" + kProtocolFieldSep +
      "Design Review" + kProtocolFieldSep + "Lab" + kProtocolFieldSep + "Phase 5";
  (void)apply_calendar_view_action(1, 0, 1, "2026-11-01", "2026-12-31", ev_chunk);

  auto &rs = runtime_state();
  rs.area.active_area_id = "living_room";
  rs.area.active_area_name = "Living Room";
  rs.area.domain_filter = "light";
  BrowseSlotItem *area_slots = rs.area_entity_slots();
  area_slots[0].id = "light.sofa";
  area_slots[0].name = "Sofa Lights";
  area_slots[0].type = "light";
  area_slots[0].active = true;
  area_slots[0].level = 80;

  BrowseSlotItem *music_slots = rs.music_slots();
  music_slots[0].id = "library://album/1";
  music_slots[0].name = "Hurry Up, We're Dreaming";
  music_slots[0].type = "album";

  timer_slots()[0].duration_sec = 300;
  timer_slots()[0].remaining_sec = 180;
  timer_slots()[0].running = true;
  timer_slots()[0].target_ms = ctx.now_ms + 180000U;
  sync_multi_timer_state();
}

void assert_rendered_frame_invariants(const HostSurfaceFixture &fix, const char *label) {
  using namespace latter_ui;
  const auto &ht = frame_hits();
  if (ht.dropped_count != 0U) {
    std::fprintf(stderr, "[HOST TEST FAIL] %s dropped %u hit regions\n",
                 label, static_cast<unsigned>(ht.dropped_count));
    std::exit(1);
  }
  const int min_7mm = mm_to_px(7.0f);
  for (uint8_t i = 0; i < ht.count; ++i) {
    const auto &r = ht.regions[i];
    LATTER_CHECK(r.x0 >= 0 && r.y0 >= 0 && r.x1 < SCREEN_W && r.y1 < SCREEN_H);
    LATTER_CHECK(r.x0 <= r.x1 && r.y0 <= r.y1);
    const int pw = r.width() + 2 * static_cast<int>(r.hit_pad);
    const int ph = r.height() + 2 * static_cast<int>(r.hit_pad);
    const bool is_primary_circle_or_back =
        (r.widget_id >= WidgetId::DOCK_SLOT_BASE && r.widget_id < WidgetId::DOCK_SLOT_BASE + 4) ||
        (r.widget_id >= WidgetId::MODE_BAR_SLOT_BASE && r.widget_id < WidgetId::MODE_BAR_SLOT_BASE + 4) ||
        r.widget_id == WidgetId::MUSIC_PLAY_PAUSE ||
        r.widget_id == WidgetId::MUSIC_PREV_BTN ||
        r.widget_id == WidgetId::MUSIC_NEXT_BTN ||
        r.widget_id == WidgetId::BACK_BUTTON;
    if (is_primary_circle_or_back) {
      if (pw < min_7mm || ph < min_7mm) {
        std::fprintf(stderr, "[HOST TEST FAIL] %s widget %u padded=%dx%d < 7mm (%dpx)\n",
                     label, static_cast<unsigned>(r.widget_id), pw, ph, min_7mm);
        std::exit(1);
      }
    }
  }

  size_t non_zero = 0;
  for (uint16_t px : fix.pixels) {
    if (px != 0U) ++non_zero;
  }
  if (non_zero == 0U) {
    std::fprintf(stderr, "[HOST TEST FAIL] %s rendered 0 lit pixels\n", label);
    std::exit(1);
  }
}

void render_page_full_and_check(HostSurfaceFixture &fix, int page_id, const char *label) {
  using namespace latter_ui;
  auto &rs = runtime_state();
  rs.nav.gui_page = page_id;
  fix.fill(0U);
  reset_clip_y_band();
  dirty_band() = DirtyBandTracker{};
  (void)dirty_band().begin_frame(page_id, compute_active_subpage_id(), 0U, true);
  frame_hits().clear();
  render_active_page(fix.surf, fix.ctx);
  assert_rendered_frame_invariants(fix, label);
}

void verify_scissor_band_isolation(HostSurfaceFixture &fix, int page_id, int y0, int y1) {
  using namespace latter_ui;
  constexpr uint16_t kSentinel = 0x5A5AU;
  auto &rs = runtime_state();
  rs.nav.gui_page = page_id;
  fix.fill(kSentinel);

  const int sub = compute_active_subpage_id();
  dirty_band() = DirtyBandTracker{};
  (void)dirty_band().begin_frame(page_id, sub, 0U, false);
  mark_dirty_band(y0, y1);
  (void)dirty_band().begin_frame(page_id, sub, 0U, false);
  LATTER_CHECK(!dirty_band().is_full());
  const int by0 = dirty_band().y0();
  const int by1 = dirty_band().y1();
  set_clip_y_band(by0, by1);
  frame_hits().clear();

  render_active_page(fix.surf, fix.ctx);
  reset_clip_y_band();

  for (int y = 0; y < SCREEN_H; ++y) {
    if (y >= by0 && y <= by1) continue;
    const size_t row_off = static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
    for (int x = 0; x < SCREEN_W; ++x) {
      if (fix.pixels[row_off + static_cast<size_t>(x)] != kSentinel) {
        std::fprintf(stderr, "[HOST TEST FAIL] Scissor leak on page %d at (%d,%d) outside [%d,%d]\n",
                     page_id, x, y, by0, by1);
        std::exit(1);
      }
    }
  }
}

void test_all_14_pages_and_system_takeovers_rgb565_5geom() {
  using namespace latter_ui;

  HostSurfaceFixture fix{};
  populate_rich_host_state(fix.ctx);
  auto &rs = runtime_state();

  // 0. PageId::CLOCK (all 3 clock faces)
  for (const char *face : {"Minimal Digital", "Modern Analog", "Hybrid Digital"}) {
    fix.ctx.clock_face_style_opt = face;
    render_page_full_and_check(fix, PageId::CLOCK, face);
  }
  verify_scissor_band_isolation(fix, PageId::CLOCK, 110, 249);

  // 1. PageId::HOME (Main, Settings, Activity Overlay)
  for (int sub : {HomeSubpage::MAIN, HomeSubpage::SETTINGS, HomeSubpage::ACTIVITY_OVERLAY}) {
    rs.nav.home_subpage = sub;
    render_page_full_and_check(fix, PageId::HOME, "PageId::HOME");
  }
  rs.nav.home_subpage = HomeSubpage::MAIN;
  verify_scissor_band_isolation(fix, PageId::HOME, 80, 219);

  // 2. PageId::MUSIC (Now Playing, Volume, Lyrics, Info, Settings, Library, Speakers, Queue, Scrub, Action Sheet)
  rs.music.hud_active = true;
  rs.music.hud_timer = fix.ctx.now_ms;
  for (MusicView v : {MusicView::NOW_PLAYING, MusicView::VOLUME, MusicView::LYRICS,
                      MusicView::INFO, MusicView::SETTINGS, MusicView::LIBRARY,
                      MusicView::SPEAKERS, MusicView::QUEUE, MusicView::ALBUM_TRACKS}) {
    rs.music.view = v;
    rs.music.takeover = MusicTakeover::NONE;
    render_page_full_and_check(fix, PageId::MUSIC, "PageId::MUSIC");
  }
  rs.music.view = MusicView::NOW_PLAYING;
  rs.music.takeover = MusicTakeover::SCRUB;
  render_page_full_and_check(fix, PageId::MUSIC, "MusicTakeover::SCRUB");
  rs.music.takeover = MusicTakeover::ACTION_SHEET;
  render_page_full_and_check(fix, PageId::MUSIC, "MusicTakeover::ACTION_SHEET");
  rs.music.takeover = MusicTakeover::NONE;
  verify_scissor_band_isolation(fix, PageId::MUSIC, 100, 259);

  // 3. PageId::GAMES (Launcher + active games + game_arena_drop)
  rs.nav.active_game = 0;
  render_page_full_and_check(fix, PageId::GAMES, "PageId::GAMES launcher");
  for (int gid : {1, 3, 5, 6}) {
    rs.nav.active_game = gid;
    render_page_full_and_check(fix, PageId::GAMES, "PageId::GAMES active");
  }
  rs.nav.active_game = 0;
  game_arena_drop();
  verify_scissor_band_isolation(fix, PageId::GAMES, 120, 279);

  // 4. PageId::SYSTEM (Dashboard, Sleep, Display, Sound, Voice, Info, Confirm Restart)
  for (int mode : {SystemMenuMode::DASHBOARD, SystemMenuMode::SLEEP_WAKE, SystemMenuMode::DISPLAY,
                   SystemMenuMode::SOUND, SystemMenuMode::SYSTEM_INFO, SystemMenuMode::CONFIRM_RESTART,
                   SystemMenuMode::COLOR_WHEEL}) {
    rs.nav.system_menu_mode = mode;
    render_page_full_and_check(fix, PageId::SYSTEM, "PageId::SYSTEM");
  }
  rs.nav.system_menu_mode = SystemMenuMode::DASHBOARD;
  verify_scissor_band_isolation(fix, PageId::SYSTEM, 120, 279);

  // 5. PageId::WEATHER (all 4 tabs + cache drop verification)
  for (int sub = 0; sub < 4; ++sub) {
    rs.nav.sky_weather_subpage = sub;
    render_page_full_and_check(fix, PageId::WEATHER, "PageId::WEATHER");
  }
  rs.nav.sky_weather_subpage = 0;
  verify_scissor_band_isolation(fix, PageId::WEATHER, 96, 239);
  weather_cloud_cache_drop();
  sky_base_cache_drop();

  // 6. PageId::AREA (Overview, Picker, Light Detail)
  for (int sub : {AreaSubpage::OVERVIEW, AreaSubpage::PICKER, AreaSubpage::LIGHT_DETAIL}) {
    rs.nav.area_subpage = sub;
    render_page_full_and_check(fix, PageId::AREA, "PageId::AREA");
  }
  rs.nav.area_subpage = AreaSubpage::OVERVIEW;
  verify_scissor_band_isolation(fix, PageId::AREA, 110, 269);

  // 7. PageId::REMOTE
  for (int sub : {0, 1}) {
    rs.nav.remote_subpage = sub;
    render_page_full_and_check(fix, PageId::REMOTE, "PageId::REMOTE");
  }
  rs.nav.remote_subpage = 0;
  verify_scissor_band_isolation(fix, PageId::REMOTE, 110, 269);

  // 8. PageId::TOOLS (Launcher + Stopwatch, Level, Flashlight, Calculator, Dice, Coin Flip)
  for (int sub : {0, ToolsSubpage::STOPWATCH, ToolsSubpage::LEVEL, ToolsSubpage::FLASHLIGHT,
                  ToolsSubpage::CALCULATOR, ToolsSubpage::DICE, ToolsSubpage::COIN_FLIP}) {
    rs.nav.tools_subpage = sub;
    render_page_full_and_check(fix, PageId::TOOLS, "PageId::TOOLS");
  }
  rs.nav.tools_subpage = 0;
  verify_scissor_band_isolation(fix, PageId::TOOLS, 110, 269);

  // 9. PageId::HOME_HUB (Alarm, Broadcast, Thermostat, Entry, Vacuum)
  for (int tab = 0; tab < kMaxHomeHubTabs; ++tab) {
    home_hub_tab() = tab;
    render_page_full_and_check(fix, PageId::HOME_HUB, "PageId::HOME_HUB");
  }
  home_hub_tab() = HomeHubTab::ALARM;
  verify_scissor_band_isolation(fix, PageId::HOME_HUB, 110, 269);

  // 10. PageId::TIME (World Clock, Timers, Alarms, Stopwatch)
  for (int sub = 0; sub < 4; ++sub) {
    rs.nav.time_subpage = sub;
    render_page_full_and_check(fix, PageId::TIME, "PageId::TIME");
  }
  rs.nav.time_subpage = 0;
  verify_scissor_band_isolation(fix, PageId::TIME, 110, 269);

  // 11. PageId::PINNED
  render_page_full_and_check(fix, PageId::PINNED, "PageId::PINNED");
  verify_scissor_band_isolation(fix, PageId::PINNED, 110, 269);

  // 12. PageId::CALENDAR (Overview, Day, Week, Month, Detail, Add Step 1)
  for (int sub : {CalendarSubpage::OVERVIEW, CalendarSubpage::DAY,
                  CalendarSubpage::WEEK, CalendarSubpage::MONTH}) {
    rs.cal.subpage = sub;
    rs.cal.drilldown = CalDrilldown::NONE;
    render_page_full_and_check(fix, PageId::CALENDAR, "PageId::CALENDAR");
  }
  rs.cal.selected_ymd = 20261121;
  rs.cal.selected_event_idx = 0;
  rs.cal.drilldown = CalDrilldown::DETAIL;
  render_page_full_and_check(fix, PageId::CALENDAR, "CalDrilldown::DETAIL");
  rs.cal.drilldown = CalDrilldown::ADD_STEP1;
  render_page_full_and_check(fix, PageId::CALENDAR, "CalDrilldown::ADD_STEP1");
  rs.cal.drilldown = CalDrilldown::NONE;
  verify_scissor_band_isolation(fix, PageId::CALENDAR, 110, 269);

  // 13. PageId::CAMERAS (Single w/ injected live RGB565 frame, Grid, Carousel, Settings)
  const uint16_t cam_px[16] = {
      0xF800U, 0xF800U, 0x07E0U, 0x07E0U,
      0xF800U, 0xF800U, 0x07E0U, 0x07E0U,
      0x001FU, 0x001FU, 0xFFFFU, 0xFFFFU,
      0x001FU, 0x001FU, 0xFFFFU, 0xFFFFU,
  };
  video_client().reset_for_test();
  video_client().start("http://ha.local/api/latter/camera_stream/dev?sig=test", 384U, 224U);
  video_client().inject_host_frame_for_test(cam_px, 4, 4);
  rs.cameras.hud_active = true;
  for (int tab : {CamerasTab::SINGLE, CamerasTab::GRID, CamerasTab::CAROUSEL, CamerasTab::SETTINGS}) {
    rs.cameras.tab = tab;
    render_page_full_and_check(fix, PageId::CAMERAS, "PageId::CAMERAS");
  }
  video_client().stop();
  verify_scissor_band_isolation(fix, PageId::CAMERAS, 110, 269);

  // 14. All System Takeovers (OTA, Hibernating, Alert Ringing, VA x5, PIN Pad, Notification, Shortcut Confirm, Quick Actions, Screensavers x6)
  auto run_takeover = [&](const char *t_label, auto configure_fn) {
    fix.fill(0U);
    reset_clip_y_band();
    dirty_band() = DirtyBandTracker{};
    (void)dirty_band().begin_frame(PageId::HOME, 0, 1U, true);
    frame_hits().clear();
    SystemTakeoverState st = SystemTakeoverState::from_context(fix.ctx);
    configure_fn(st);
    LATTER_CHECK(render_system_takeovers(fix.surf, fix.ctx, st));
    assert_rendered_frame_invariants(fix, t_label);
  };

  run_takeover("Takeover::OTA", [](SystemTakeoverState &st) {
    st.ota_in_progress = true;
    st.ota_progress_pct = 42;
  });
  run_takeover("Takeover::Hibernating", [](SystemTakeoverState &st) {
    st.is_hibernating = true;
    st.hibernate_countdown_val = 3;
  });
  run_takeover("Takeover::AlertRinging", [](SystemTakeoverState &st) {
    st.timer_ringing = true;
    st.va_timer_name = "Pasta";
  });
  for (const char *va_style : {"Nebula", "Waveform", "Solaris", "Pulse Ring", "Minimal"}) {
    run_takeover(va_style, [&](SystemTakeoverState &st) {
      st.va_active = true;
      st.va_vad_active = true;
      st.va_status_text = "Listening...";
      st.va_visual_profile_opt = va_style;
    });
  }
  ui::pin_pad_open(ui::PinPadTargetKind::ALARM_ACTION, "Enter Security PIN", fix.ctx.now_ms);
  run_takeover("Takeover::PinPad", [](SystemTakeoverState &) {});
  ui::pin_pad_scrub();

  run_takeover("Takeover::Notification", [](SystemTakeoverState &st) {
    st.notif_active = true;
    st.notif_title = "Front Door";
    st.notif_message = "Motion detected";
    st.notif_priority = "critical";
  });

  shortcut_confirm_state().open = true;
  shortcut_confirm_state().title = "Unlock Door?";
  shortcut_confirm_state().line1 = "Front Door";
  shortcut_confirm_state().verb_btn = "Unlock";
  run_takeover("Takeover::ShortcutConfirm", [](SystemTakeoverState &) {});
  shortcut_confirm_state().clear();

  run_takeover("Takeover::QuickActions", [](SystemTakeoverState &st) {
    st.quick_actions_open = true;
  });

  for (const char *ss_style : {"Ambient Clock", "Aurora", "Starfield",
                               "Digital Rain", "Fireworks", "Photo Frame"}) {
    run_takeover(ss_style, [&](SystemTakeoverState &st) {
      st.display_power_state = 1;
      st.screensaver_style_opt = ss_style;
    });
  }

  art_canvas_drop_all();
  weather_cloud_cache_drop();
  sky_base_cache_drop();
  game_arena_drop();
}

void test_home_hub_d048_button_only_glass_and_conditional_controls() {
  using namespace latter_ui;

  static_assert(LATTER_SDK_REV == 2026112007L, "LATTER_SDK_REV must match 2026112007L");
  static_assert(static_cast<size_t>(AreaCtxField::THERMOSTATS_SUMMARY) == 15U);
  static_assert(static_cast<size_t>(AreaCtxField::ALARM_CAPS) == 38U);
  static_assert(static_cast<size_t>(AreaCtxField::AREA_AMBIENT_LIGHT) == 42U);
  static_assert(static_cast<size_t>(AreaCtxField::COUNT) == 43U);

  reset_runtime_state();

  auto build_hub_caps_payload = [](uint32_t rev, std::string_view alarm_caps,
                                   std::string_view climate_caps, std::string_view vacuum_caps,
                                   std::string_view t_temp, std::string_view t_mode,
                                   std::string_view vac_st, bool with_entities = true,
                                   std::string_view area_id = "downstairs") {
    std::string p = "v3\x1f" "1\x1f" + std::to_string(rev);
    for (size_t i = 3; i < static_cast<size_t>(AreaCtxField::COUNT); ++i) {
      p.push_back(kProtocolFieldSep);
      if (i == static_cast<size_t>(AreaCtxField::AREA_ID)) p.append(area_id);
      else if (with_entities && i == static_cast<size_t>(AreaCtxField::ALARM_ENTITY)) {
        p.append("alarm_control_panel.home");
      } else if (with_entities && i == static_cast<size_t>(AreaCtxField::THERMOSTATS_SUMMARY)) {
        p.append("climate.downstairs|Downstairs|downstairs|");
        p.append(t_mode);
        p.push_back('|');
        p.append(t_temp);
        p.append("|68.5|");
        p.append(climate_caps);
        p.push_back(kProtocolRecordSep);
        p.append("climate.upstairs|Upstairs|upstairs|cool|74|75.0|127");
      } else if (with_entities && i == static_cast<size_t>(AreaCtxField::LOCKS_SUMMARY)) {
        p.append("lock.front_door|Front Door|foyer|locked");
        p.push_back(kProtocolRecordSep);
        p.append("lock.back_door|Back Door|patio|unlocked");
      } else if (with_entities && i == static_cast<size_t>(AreaCtxField::GARAGES_SUMMARY)) {
        p.append("cover.main_garage|Main Garage|garage|closed");
        p.push_back(kProtocolRecordSep);
        p.append("cover.workshop_garage|Workshop Bay|garage|open");
      } else if (with_entities && i == static_cast<size_t>(AreaCtxField::VACUUMS_SUMMARY)) {
        p.append("vacuum.main_floor|Main Floor|downstairs|");
        p.append(vac_st);
        p.push_back('|');
        p.append(vacuum_caps);
        p.push_back(kProtocolRecordSep);
        p.append("vacuum.upstairs|Upstairs Vac|upstairs|docked|11");
      } else if (with_entities && i == static_cast<size_t>(AreaCtxField::AREA_CLIMATES_SUMMARY)) {
        p.append("climate.office_mini|Office Mini|cool|70|71.2|127");
      } else if (i == static_cast<size_t>(AreaCtxField::ALARM_CAPS)) {
        p.append(alarm_caps);
      }
    }
    return p;
  };

  AreaContextState ac{};
  HomeHubState hub{};
  LATTER_CHECK(parse_v3_area_ctx_payload(
      build_hub_caps_payload(1U, "0", "0", "0", "72", "heat", "idle", true, "downstairs"), ac, hub));
  LATTER_CHECK(effective_alarm_caps(ac) == kAlarmCapDefault);
  LATTER_CHECK(hub.thermostat_count == 2 && hub.entry_count == 4 && hub.vacuum_count == 2);
  LATTER_CHECK(effective_climate_caps(hub) == kClimateCapDefault);
  LATTER_CHECK(effective_vacuum_caps(hub) == kVacuumCapDefault);

  LATTER_CHECK(parse_v3_area_ctx_payload(
      build_hub_caps_payload(2U, "63", "255", "63", "21.5", "heat_cool", "returning", true, "upstairs"), ac, hub));
  LATTER_CHECK(hub.thermostat_idx == 1 && hub.vacuum_idx == 1);

  AreaContextState empty_ent_ac{};
  HomeHubState empty_ent_hub{};
  LATTER_CHECK(parse_v3_area_ctx_payload(
      build_hub_caps_payload(3U, "63", "255", "63", "72", "heat", "cleaning", false),
      empty_ent_ac, empty_ent_hub));
  LATTER_CHECK(effective_alarm_caps(empty_ent_ac) == 0U);
  LATTER_CHECK(effective_climate_caps(empty_ent_hub) == 0U);
  LATTER_CHECK(effective_vacuum_caps(empty_ent_hub) == 0U);

  LATTER_CHECK(!thermostat_shows_setpoint_buttons("off", kClimateCapDefault));
  LATTER_CHECK(thermostat_shows_setpoint_buttons("heat", kClimateCapDefault));
  LATTER_CHECK(step_thermostat_setpoint(50, -1, kClimateCapDefault) == 50);
  LATTER_CHECK(step_thermostat_setpoint(90, +1, kClimateCapDefault) == 90);
  const uint16_t caps_celsius = kClimateCapDefault | kClimateCapCelsius;
  LATTER_CHECK(step_thermostat_setpoint(10, -1, caps_celsius) == 10);
  LATTER_CHECK(step_thermostat_setpoint(35, +1, caps_celsius) == 35);

  const std::string hh_src = read_repo_file("../../components/latter_ui/page_home_hub.cpp");
  const std::string te_src = read_repo_glob("../../components/latter_ui", "touch_", ".cpp");
  LATTER_CHECK(hh_src.find("No Security Alarm configured") != std::string::npos);
  LATTER_CHECK(hh_src.find("ui::header_pager_bar(") != std::string::npos);
  LATTER_CHECK(te_src.find("\"arm_vacation\"") != std::string::npos);
  LATTER_CHECK(te_src.find("\"clean_spot\"") != std::string::npos);
}

}  // namespace

void run_ui_rgb565_5geom_tests() {
  test_all_14_pages_and_system_takeovers_rgb565_5geom();
  test_home_hub_d048_button_only_glass_and_conditional_controls();
}

}  // namespace latter_test
