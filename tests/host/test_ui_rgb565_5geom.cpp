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

bool is_d019_slot5_row_widget(uint16_t wid) noexcept {
  using namespace latter_ui;
  return (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 5) ||
         (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) ||
         wid == WidgetId::SYS_BRI_SLIDER ||
         wid == WidgetId::SYS_VOL_SLIDER ||
         wid == WidgetId::SYS_SECONDARY_BTN ||
         wid == WidgetId::QUICK_VOL_SLIDER ||
         wid == WidgetId::QUICK_BRI_SLIDER ||
         wid == WidgetId::QUICK_BEDTIME_TOGGLE ||
         wid == WidgetId::QUICK_DND_TOGGLE ||
         (wid >= WidgetId::INTERCOM_ROW_BASE && wid < WidgetId::INTERCOM_ROW_BASE + 5) ||
         (wid >= WidgetId::CAM_SETTINGS_ROW_BASE && wid < WidgetId::CAM_SETTINGS_ROW_BASE + 5) ||
         wid == WidgetId::REMOTE_PRIMARY_PWR ||
         (wid >= WidgetId::REMOTE_SLOT_CARD && wid < WidgetId::REMOTE_SLOT_CARD + 4) ||
         (wid >= WidgetId::REMOTE_SEC_PWR && wid < WidgetId::REMOTE_SEC_PWR + 4) ||
         (wid >= WidgetId::CAL_EVENT_BASE && wid < WidgetId::CAL_EVENT_BASE + 5) ||
         (wid >= WidgetId::CAL_PICKER_SLOT_BASE && wid < WidgetId::CAL_PICKER_SLOT_BASE + 5) ||
         (wid >= WidgetId::CAL_ADD_TITLE_BTN && wid <= WidgetId::CAL_ADD_REPEAT_BTN) ||
         (wid >= WidgetId::CAL_ADD_ALLDAY_BTN && wid <= WidgetId::CAL_ADD_END_BTN) ||
         (wid >= WidgetId::MUSIC_ROW_PLAY_BASE && wid < WidgetId::MUSIC_ROW_PLAY_BASE + 5) ||
         (wid >= WidgetId::MUSIC_ACTION_PLAY_NOW && wid <= WidgetId::MUSIC_ACTION_REMOVE_QUEUE) ||
         wid == WidgetId::MUSIC_QUEUE_CLEAR ||
         wid == WidgetId::MUSIC_SHUFFLE_BTN ||
         wid == WidgetId::MUSIC_REPEAT_BTN ||
         wid == WidgetId::LIGHT_COLOR_BTN ||
         wid == WidgetId::ALERT_DISMISS_BTN;
}

bool is_dense_grid_or_gutter_widget(uint16_t wid) noexcept {
  using namespace latter_ui;
  return wid == WidgetId::CHEVRON_UP ||
         wid == WidgetId::CHEVRON_DOWN ||
         wid == WidgetId::MODE_BAR_LEFT ||
         wid == WidgetId::MODE_BAR_RIGHT ||
         wid == WidgetId::MUSIC_TAKEOVER_BACKDROP ||
         wid == WidgetId::MUSIC_ART_TAP ||
         wid == WidgetId::CAM_VIDEO_TAP ||
         (wid >= WidgetId::GRID_CARD_BASE && wid < WidgetId::GRID_CARD_BASE + 6) ||
         wid == WidgetId::GAMES_PREV_BTN ||
         wid == WidgetId::GAMES_NEXT_BTN ||
         wid == WidgetId::GAMES_HERO_CARD ||
         (wid >= WidgetId::STEPPER_BASE && wid < WidgetId::STEPPER_BASE + 4) ||
         (wid >= WidgetId::COLOR_SWATCH_BASE && wid < WidgetId::COLOR_SWATCH_BASE + 10) ||
         wid == WidgetId::COLOR_WHEEL_RING ||
         (wid >= WidgetId::REMOTE_DPAD_UP && wid <= WidgetId::REMOTE_DPAD_OK) ||
         wid == WidgetId::HOME_WEATHER_HERO ||
         wid == WidgetId::HOME_TIME_READOUT ||
         wid == WidgetId::HOME_QUICK_BTN ||
         wid == WidgetId::HOME_DATE_HERO ||
         (wid >= WidgetId::HOME_ACT_ROW_0 && wid <= WidgetId::HOME_ACT_EXPAND) ||
         (wid >= WidgetId::CLIMATE_MODE_BASE && wid < WidgetId::CLIMATE_MODE_BASE + 6) ||
         (wid >= WidgetId::TOOLS_CARD_BASE && wid < WidgetId::TOOLS_CARD_BASE + 6) ||
         (wid >= WidgetId::PINNED_CARD_BASE && wid < WidgetId::PINNED_CARD_BASE + 6) ||
         (wid >= WidgetId::CAL_WEEK_DAY_BASE && wid < WidgetId::CAL_WEEK_DAY_BASE + 7) ||
         (wid >= WidgetId::CAL_MONTH_WEEK_BASE && wid < WidgetId::CAL_MONTH_WEEK_BASE + 6) ||
         (wid >= WidgetId::CAL_MONTH_CELL_BASE && wid < WidgetId::CAL_MONTH_CELL_BASE + 42) ||
         (wid >= WidgetId::MUSIC_LYRIC_LINE_BASE && wid < WidgetId::MUSIC_LYRIC_LINE_BASE + 8);
}

int sibling_family_key(uint16_t wid) noexcept {
  using namespace latter_ui;
  if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 5) return 1;
  if (wid >= WidgetId::DOCK_SLOT_BASE && wid < WidgetId::DOCK_SLOT_BASE + 4) return 2;
  if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4) return 3;
  if (wid >= WidgetId::PIN_PAD_KEY_BASE && wid <= WidgetId::PIN_PAD_CANCEL) return 4;
  if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) return 5;
  if (wid >= WidgetId::AREA_CARD_TAP && wid < WidgetId::AREA_CARD_TAP + 3) return 6;
  if (wid >= WidgetId::AREA_CARD_TOGGLE && wid < WidgetId::AREA_CARD_TOGGLE + 3) return 7;
  if (wid >= WidgetId::ALARM_MODE_BASE && wid < WidgetId::ALARM_MODE_BASE + 5) return 8;
  if (wid >= WidgetId::VACUUM_ACTION_BASE && wid < WidgetId::VACUUM_ACTION_BASE + 6) return 9;
  if (wid >= WidgetId::CLIMATE_MODE_BASE && wid < WidgetId::CLIMATE_MODE_BASE + 6) return 10;
  if (wid >= WidgetId::TOOLS_CARD_BASE && wid < WidgetId::TOOLS_CARD_BASE + 6) return 11;
  if (wid >= WidgetId::CALC_KEY_BASE && wid < WidgetId::CALC_KEY_BASE + 20) return 12;
  if (wid >= WidgetId::PINNED_CARD_BASE && wid < WidgetId::PINNED_CARD_BASE + 6) return 13;
  if (wid >= WidgetId::MUSIC_ACTION_PLAY_NOW && wid <= WidgetId::MUSIC_ACTION_REMOVE_QUEUE) return 14;
  return 0;
}

void assert_rendered_frame_invariants(const HostSurfaceFixture &fix, const char *label) {
  using namespace latter_ui;
  const auto &ht = frame_hits();
  if (ht.dropped_count != 0U || ht.count > FrameHitTable::kMaxRegions) {
    std::fprintf(stderr, "[HOST TEST FAIL] %s dropped=%u count=%u\n",
                 label, static_cast<unsigned>(ht.dropped_count), static_cast<unsigned>(ht.count));
    std::exit(1);
  }
  const int min_7mm = mm_to_px(7.0f);
  const auto &ly = layout();
  for (uint8_t i = 0; i < ht.count; ++i) {
    const auto &r = ht.regions[i];
    LATTER_CHECK(r.x0 >= 0 && r.y0 >= 0 && r.x1 < SCREEN_W && r.y1 < SCREEN_H);
    LATTER_CHECK(r.x0 <= r.x1 && r.y0 <= r.y1);

    // Bezel check across all 5 canonical geometries (round R=233 & rounded-rect R=20/76/98).
    if (r.widget_id == WidgetId::MODE_BAR_LEFT) {
      LATTER_CHECK(is_inside_bezel_for(r.x1, r.y0, SCREEN_W, SCREEN_H,
                                       LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 1, LATTER_CORNER_R));
    } else if (r.widget_id == WidgetId::MODE_BAR_RIGHT) {
      LATTER_CHECK(is_inside_bezel_for(r.x0, r.y0, SCREEN_W, SCREEN_H,
                                       LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 1, LATTER_CORNER_R));
    } else if (r.widget_id != WidgetId::MUSIC_TAKEOVER_BACKDROP) {
      if (!is_inside_bezel_for(r.cx(), r.cy(), SCREEN_W, SCREEN_H,
                               LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 1, LATTER_CORNER_R)) {
        std::fprintf(stderr, "[HOST TEST FAIL] %s widget %u center (%d,%d) clipped by bezel R=%d\n",
                     label, static_cast<unsigned>(r.widget_id), r.cx(), r.cy(), LATTER_CORNER_R);
        std::exit(1);
      }
    }

    const int pw = r.width() + 2 * static_cast<int>(r.hit_pad);
    const int ph = r.height() + 2 * static_cast<int>(r.hit_pad);
    if (is_d019_slot5_row_widget(r.widget_id)) {
      // 5-row list/form items keep the compact slot5 vertical stride on smaller high-PPI panels.
      const int min_slot5_h = (r.widget_id == WidgetId::LIGHT_COLOR_BTN) ? (ly.slot5_h - 2) : ly.slot5_h;
      if (pw < min_7mm || ph < min_slot5_h) {
        std::fprintf(stderr, "[HOST TEST FAIL] %s slot5 widget %u padded=%dx%d < %dx%d\n",
                     label, static_cast<unsigned>(r.widget_id), pw, ph, min_7mm, min_slot5_h);
        std::exit(1);
      }
    } else if ((r.widget_id >= WidgetId::TOOLS_CARD_BASE && r.widget_id < WidgetId::TOOLS_CARD_BASE + 6) ||
               (r.widget_id >= WidgetId::PINNED_CARD_BASE && r.widget_id < WidgetId::PINNED_CARD_BASE + 6)) {
      // 2x3 launcher grid cards (>= 6.90 mm on Wrist 315 PPI; >= 7.0 mm on all other geometries).
      const int min_grid_px = mm_to_px(6.90f);
      LATTER_CHECK(pw >= min_7mm && ph >= min_grid_px);
    } else if (!is_dense_grid_or_gutter_widget(r.widget_id)) {
      if (pw < min_7mm || ph < min_7mm) {
        std::fprintf(stderr, "[HOST TEST FAIL] %s widget %u padded=%dx%d < 7.0mm (%dpx)\n",
                     label, static_cast<unsigned>(r.widget_id), pw, ph, min_7mm);
        std::exit(1);
      }
    }

    // Zero sibling-family AABB overlaps across docks, mode bars, lists, grids, keypads & Home Hub pills.
    const int fam_i = sibling_family_key(r.widget_id);
    if (fam_i != 0) {
      for (uint8_t j = static_cast<uint8_t>(i + 1U); j < ht.count; ++j) {
        const auto &b = ht.regions[j];
        if (sibling_family_key(b.widget_id) != fam_i) continue;
        const bool overlap = (r.x0 <= b.x1) && (b.x0 <= r.x1) && (r.y0 <= b.y1) && (b.y0 <= r.y1);
        if (overlap) {
          std::fprintf(stderr, "[HOST TEST FAIL] %s sibling overlap (%u vs %u)\n",
                       label, static_cast<unsigned>(r.widget_id), static_cast<unsigned>(b.widget_id));
          std::exit(1);
        }
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

  // 3. PageId::GAMES (Launcher + all 6 active games + game_arena_drop)
  rs.nav.active_game = 0;
  render_page_full_and_check(fix, PageId::GAMES, "PageId::GAMES launcher");
  for (int gid : {1, 2, 3, 4, 5, 6}) {
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
  rs.cal.drilldown = CalDrilldown::ADD_STEP2;
  render_page_full_and_check(fix, PageId::CALENDAR, "CalDrilldown::ADD_STEP2");
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

  static_assert(LATTER_SDK_REV == 2026112201L, "LATTER_SDK_REV must match 2026112201L");
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

void test_wcag_palette_and_surface_contrast_5geom() {
  using namespace latter_ui;

  auto srgb_lin = [](uint8_t c) noexcept -> double {
    const double x = static_cast<double>(c) / 255.0;
    return (x <= 0.04045) ? (x / 12.92) : std::pow((x + 0.055) / 1.055, 2.4);
  };
  auto rel_lum = [&](UiColor c) noexcept -> double {
    return 0.2126 * srgb_lin(c.r) + 0.7152 * srgb_lin(c.g) + 0.0722 * srgb_lin(c.b);
  };
  auto quantize_rgb565 = [](UiColor c) noexcept -> UiColor {
    const uint16_t px = static_cast<uint16_t>(
        ((static_cast<uint16_t>(c.r >> 3) & 0x1FU) << 11) |
        ((static_cast<uint16_t>(c.g >> 2) & 0x3FU) << 5) |
        (static_cast<uint16_t>(c.b >> 3) & 0x1FU));
    const uint8_t r5 = static_cast<uint8_t>((px >> 11) & 0x1FU);
    const uint8_t g6 = static_cast<uint8_t>((px >> 5) & 0x3FU);
    const uint8_t b5 = static_cast<uint8_t>(px & 0x1FU);
    return UiColor(static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
                   static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
                   static_cast<uint8_t>((b5 << 3) | (b5 >> 2)));
  };
  auto contrast_ratio = [&](UiColor fg, UiColor bg) noexcept -> double {
    const double l1 = rel_lum(fg);
    const double l2 = rel_lum(bg);
    return (l1 > l2) ? ((l1 + 0.05) / (l2 + 0.05)) : ((l2 + 0.05) / (l1 + 0.05));
  };

  const UiFrameContext ctx{};
  const UiColor surfaces[] = {
      ctx.col_bg,
      kChromeCardBgOff,
      kChromeCardBgOn,
      kChromeModalBg,
      kChromeKeyBg,
      kChromeCaptionBg,
      kChromeGridBg,
  };
  const UiColor text_tokens[] = {
      ctx.col_white,
      ctx.col_slate,
      ctx.theme_primary,
      ctx.theme_secondary,
      ctx.col_gold,
      ctx.col_warm_gold,
      ctx.col_solaris_gold,
      ctx.col_accent_cyan,
      ctx.col_green,
      ctx.col_red,
      kChromeMuted,
      kChromeSubtle,
      kChromeBackLabel,
      kChromeCustomSecondary,
      kChromePlayGlyph,
      kChromeDegSilver,
      kChromeTeleIce,
      kChromeDayGold,
      kChromeWarmKelvin,
      kChromeCoolKelvin,
      kChromeRgbRed,
      kChromeRgbGreen,
      kChromeRgbBlue,
      kChromeFanAccent,
  };

  for (UiColor bg : surfaces) {
    const UiColor bg565 = quantize_rgb565(bg);
    for (UiColor fg : text_tokens) {
      LATTER_CHECK(contrast_ratio(fg, bg) >= 4.5);
      LATTER_CHECK(contrast_ratio(quantize_rgb565(fg), bg565) >= 4.5);
    }
  }
  LATTER_CHECK(contrast_ratio(ctx.col_white, kChromeDangerBg) >= 4.5);
  LATTER_CHECK(contrast_ratio(ctx.col_red, kChromeDangerBg) >= 4.5);
  LATTER_CHECK(contrast_ratio(quantize_rgb565(ctx.col_red), quantize_rgb565(kChromeDangerBg)) >= 4.5);
}

}  // namespace

void run_ui_rgb565_5geom_tests() {
  test_all_14_pages_and_system_takeovers_rgb565_5geom();
  test_home_hub_d048_button_only_glass_and_conditional_controls();
  test_wcag_palette_and_surface_contrast_5geom();
}

}  // namespace latter_test
