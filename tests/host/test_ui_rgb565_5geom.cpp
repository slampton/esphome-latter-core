#include "test_harness.h"

namespace latter_test {

namespace {

void populate_rich_host_state(latter_ui::UiFrameContext &ctx) {
  using namespace latter_ui;
  using namespace latter_core;

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
  {
    const std::string tkey = build_music_track_key(ms.title, ms.artist);
    const std::string lrc_blob =
        std::string("0") + kProtocolFieldSep + "Waiting in a car" +
        kProtocolRecordSep + "45000" + kProtocolFieldSep + "Waiting for a ride in the dark" +
        kProtocolRecordSep + "90000" + kProtocolFieldSep + "The night city grows";
    (void)apply_music_lyrics_action(1, 0, 1, tkey, true, lrc_blob, runtime_state().music.lyrics);
  }

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
  using namespace latter_core;
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
  using namespace latter_core;
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
         (wid >= WidgetId::TOOLS_CARD_BASE && wid < WidgetId::TOOLS_CARD_BASE + 16) ||
         (wid >= WidgetId::PINNED_CARD_BASE && wid < WidgetId::PINNED_CARD_BASE + 16) ||
         (wid >= WidgetId::CAL_WEEK_DAY_BASE && wid < WidgetId::CAL_WEEK_DAY_BASE + 7) ||
         (wid >= WidgetId::CAL_MONTH_WEEK_BASE && wid < WidgetId::CAL_MONTH_WEEK_BASE + 6) ||
         (wid >= WidgetId::CAL_MONTH_CELL_BASE && wid < WidgetId::CAL_MONTH_CELL_BASE + 42) ||
         (wid >= WidgetId::MUSIC_LYRIC_LINE_BASE && wid < WidgetId::MUSIC_LYRIC_LINE_BASE + 8);
}

int sibling_family_key(uint16_t wid) noexcept {
  using namespace latter_ui;
  using namespace latter_core;
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
  if (wid >= WidgetId::TOOLS_CARD_BASE && wid < WidgetId::TOOLS_CARD_BASE + 16) return 11;
  if (wid >= WidgetId::CALC_KEY_BASE && wid < WidgetId::CALC_KEY_BASE + 20) return 12;
  if (wid >= WidgetId::PINNED_CARD_BASE && wid < WidgetId::PINNED_CARD_BASE + 16) return 13;
  if (wid >= WidgetId::MUSIC_ACTION_PLAY_NOW && wid <= WidgetId::MUSIC_ACTION_REMOVE_QUEUE) return 14;
  return 0;
}

void assert_rendered_frame_invariants(const HostSurfaceFixture &fix, const char *label) {
  using namespace latter_ui;
  using namespace latter_core;
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
      // 5-row list/form items keep the compact slot5 vertical stride on smaller high-PPI panels,
      // while still guaranteeing >= 7.0 mm on lower-PPI panels where min_7mm <= slot5_h.
      const int slot5_floor = (r.widget_id == WidgetId::LIGHT_COLOR_BTN) ? (ly.slot5_h - 2) : ly.slot5_h;
      const int min_slot5_h = std::min(min_7mm, slot5_floor);
      if (pw < min_7mm || ph < min_slot5_h) {
        std::fprintf(stderr, "[HOST TEST FAIL] %s slot5 widget %u padded=%dx%d < %dx%d\n",
                     label, static_cast<unsigned>(r.widget_id), pw, ph, min_7mm, min_slot5_h);
        std::exit(1);
      }
    } else if ((r.widget_id >= WidgetId::TOOLS_CARD_BASE && r.widget_id < WidgetId::TOOLS_CARD_BASE + 16) ||
               (r.widget_id >= WidgetId::PINNED_CARD_BASE && r.widget_id < WidgetId::PINNED_CARD_BASE + 16)) {
      // Adaptive 2x3 / 3x3 / 4x3 variable-span tile-grid cards strictly >= 7.0 mm across all 6 geometries (D-086).
      LATTER_CHECK(pw >= min_7mm && ph >= min_7mm);
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
  using namespace latter_core;
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
  using namespace latter_core;
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
  using namespace latter_core;

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

  // 3. PageId::GAMES (Launcher + all 7 game IDs + graceful unsupported fallback + game_arena_drop)
  rs.nav.active_game = 0;
  render_page_full_and_check(fix, PageId::GAMES, "PageId::GAMES launcher");
  for (int gid : {1, 2, 3, 4, 5, 6, 7}) {
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
  using namespace latter_core;

  static_assert(LATTER_CORE_REV == 2026112403L, "LATTER_CORE_REV must match 2026112403L");
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
  using namespace latter_core;

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

void test_art_canvas_lut_and_dual_sink_parity_5geom() {
  using namespace latter_ui;
  using namespace latter_core;

  // Enforce exact ui_panel_traits.h <-> hal_manifest.h parity on ArtCanvasCache slot cap.
#if LATTER_CHIP_P4
  static_assert(kArtCacheEntries == 3, "P4 builds must configure 3 ArtCanvasCache entries");
#else
  static_assert(kArtCacheEntries == 1, "8 MB S3 builds must cap ArtCanvasCache to 1 entry");
#endif

  static constexpr uint8_t kRefBayer4[4][4] = {
      {0, 8, 2, 10},
      {12, 4, 14, 6},
      {3, 11, 1, 9},
      {15, 7, 13, 5},
  };
  auto ref_dim_px = [](uint16_t p, int mul_q8, int x, int y, int endian_mode) -> uint16_t {
    uint16_t out = p;
    if (mul_q8 < 256) {
      const uint32_t m = static_cast<uint32_t>(mul_q8 < 0 ? 0 : mul_q8);
      const uint32_t t = static_cast<uint32_t>(kRefBayer4[y & 3][x & 3]) * 16U + 8U;
      const uint32_t r = (((static_cast<uint32_t>(p) >> 11) & 0x1FU) * m + t) >> 8;
      const uint32_t g = (((static_cast<uint32_t>(p) >> 5) & 0x3FU) * m + t) >> 8;
      const uint32_t b = ((static_cast<uint32_t>(p) & 0x1FU) * m + t) >> 8;
      out = static_cast<uint16_t>((r << 11) | (g << 5) | b);
    }
    if (endian_mode == 2) {
      out = static_cast<uint16_t>((out >> 8) | (out << 8));
    }
    return out;
  };

  std::vector<uint16_t> src(static_cast<size_t>(SCREEN_W) * static_cast<size_t>(SCREEN_H));
  for (int y = 0; y < SCREEN_H; ++y) {
    for (int x = 0; x < SCREEN_W; ++x) {
      const uint16_t r = static_cast<uint16_t>((x * 31) / std::max(1, SCREEN_W - 1));
      const uint16_t g = static_cast<uint16_t>((y * 63) / std::max(1, SCREEN_H - 1));
      const uint16_t b = static_cast<uint16_t>(((x + y) * 31) / std::max(1, SCREEN_W + SCREEN_H - 2));
      src[static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W) + static_cast<size_t>(x)] =
          static_cast<uint16_t>((r << 11) | (g << 5) | b);
    }
  }
  const ArtImageView art_view{src.data(), SCREEN_W, SCREEN_H};

  struct Case {
    bool scrim;
    bool meta;
    int dim;
  };
  const Case cases[] = {
      {false, false, 0},
      {true, true, 0},
      {false, true, 0},
      {true, true, kArtDimMenu},
      {true, false, kArtDimLyrics},
  };

  HostSurfaceFixture fix{};
  for (int endian_mode : {1, 2}) {
    fix.surf = bind_host_surface(fix.pixels.data(), SCREEN_W, SCREEN_H, endian_mode);
    art_canvas_drop_all();
    for (const Case &c : cases) {
      // Pass 0 = cache miss (dual-sink L1 mirror), Pass 1 = cache hit (span memcpy).
      for (int pass = 0; pass < 2; ++pass) {
        fix.fill(0U);
        dirty_band() = DirtyBandTracker{};
        (void)dirty_band().begin_frame(PageId::MUSIC, pass, 0U, true);
        LATTER_CHECK(render_album_art_canvas_view(
            fix.surf, art_view, SCREEN_CX, /*is_standard=*/false, /*is_letterbox=*/false,
            c.scrim, c.meta, c.dim));
        // Spot-check every 17th row and every 13th column inside active art span (including ramp rows & edges).
        for (int y = 0; y < SCREEN_H; y += 17) {
          const int mul_q8 = compute_art_row_scale_q8(y, c.scrim, c.meta, c.dim);
          int x0 = 0;
          int x1 = SCREEN_W - 1;
#if LATTER_ROUND_DISPLAY
          const int hw = circle_half_span(y, 0);
          x0 = std::max(0, SCREEN_CX - hw);
          x1 = std::min(SCREEN_W - 1, SCREEN_CX + hw);
#endif
          for (int x = x0; x <= x1; x += 13) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W) + static_cast<size_t>(x);
            const uint16_t want = ref_dim_px(src[idx], mul_q8, x, y, endian_mode);
            LATTER_CHECK(fix.pixels[idx] == want);
          }
          // Always verify span endpoints too (catches tail loop bugs when span_w % 4 != 0).
          for (int x : {x0, x1}) {
            if (x < x0 || x > x1) continue;
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W) + static_cast<size_t>(x);
            const uint16_t want = ref_dim_px(src[idx], mul_q8, x, y, endian_mode);
            LATTER_CHECK(fix.pixels[idx] == want);
          }
        }
      }
    }
  }
  art_canvas_drop_all();
}

void test_rotated_fallback_print_and_adaptive_glass_style() {
  using namespace latter_ui;
  using namespace latter_core;

  // 1. Rotated display (active_frame_buf16() == nullptr) text rendering via fallback_print_fn
  struct MockRotatedDisplay {
    int w{SCREEN_W};
    int h{SCREEN_H};
    int pixel_calls{0};
    int hline_calls{0};
    int min_y{99999};
    int max_y{-1};

    int get_width() const noexcept { return w; }
    int get_height() const noexcept { return h; }
    void draw_pixel_at(int x, int y, UiColor) noexcept {
      if (x >= 0 && x < w && y >= 0 && y < h) {
        ++pixel_calls;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
      }
    }
    void horizontal_line(int x, int y, int len, UiColor) noexcept {
      if (len > 0 && x >= 0 && x + len <= w && y >= 0 && y < h) {
        ++hline_calls;
        pixel_calls += len;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
      }
    }
    void vertical_line(int, int, int, UiColor) noexcept {}
  };

  {
    uint16_t *saved_fb = active_frame_buf16();
    active_frame_buf16() = nullptr;
    reset_clip_y_band();
    MockRotatedDisplay rot_disp{};
    Rgb565Surface surf(rot_disp);
    LATTER_CHECK(surf.data == nullptr);
    LATTER_CHECK(surf.fallback_print_fn != nullptr);

    const UiFontView fnt = UiFontView::make_host_font(20);
    fast_print(surf, SCREEN_CX, 60, fnt, UiColor(255, 255, 255), UiTextAlign::CENTER, "Rotated OK");
    LATTER_CHECK(rot_disp.pixel_calls > 0);
    LATTER_CHECK(rot_disp.hline_calls > 0);

    // Clip band enforcement on fallback print
    MockRotatedDisplay clipped_disp{};
    Rgb565Surface clipped_surf(clipped_disp);
    set_clip_y_band(100, 110);
    fast_print(clipped_surf, SCREEN_CX, 105, fnt, UiColor(255, 255, 255), UiTextAlign::CENTER, "Clipped");
    LATTER_CHECK(clipped_disp.pixel_calls > 0);
    LATTER_CHECK(clipped_disp.min_y >= 100 && clipped_disp.max_y <= 110);

    // Fully outside clip band -> zero pixels emitted
    MockRotatedDisplay out_disp{};
    Rgb565Surface out_surf(out_disp);
    fast_print(out_surf, SCREEN_CX, 20, fnt, UiColor(255, 255, 255), UiTextAlign::CENTER, "Offband");
    LATTER_CHECK(out_disp.pixel_calls == 0);

    reset_clip_y_band();
    active_frame_buf16() = saved_fb;
  }

  // 2. 3-way adaptive glass_style (Auto | Clear | Dark) over dark vs bright backdrops
  {
    HostSurfaceFixture fix{};
    const uint8_t saved_gs = persisted_prefs().glass_style;
    const UiColor white_fg(255, 255, 255);
    const UiColor dark_bg(10, 14, 20);
    const UiColor bright_bg(235, 245, 255);
    const uint16_t dark_hw = encode_hw_rgb565(dark_bg);
    const uint16_t bright_hw = encode_hw_rgb565(bright_bg);
    const int bx = std::max(8, SCREEN_CX - 80);
    const int by = std::max(8, SCREEN_CY - 24);
    const int bw = std::min(160, SCREEN_W - 16);
    const int bh = 48;

    auto decode_px = [](uint16_t hw) -> UiColor {
      const uint16_t p = decode_hw_rgb565_raw(hw);
      const uint8_t r5 = static_cast<uint8_t>((p >> 11) & 0x1FU);
      const uint8_t g6 = static_cast<uint8_t>((p >> 5) & 0x3FU);
      const uint8_t b5 = static_cast<uint8_t>(p & 0x1FU);
      return UiColor{
          static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
          static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
          static_cast<uint8_t>((b5 << 3) | (b5 >> 2)),
      };
    };

    // Auto with !backdrop_active() -> short-circuits with zero PSRAM scan even over bright pixels
    persisted_prefs().glass_style = static_cast<uint8_t>(GlassStyle::Auto);
    fix.fill(bright_hw);
    LATTER_CHECK(!backdrop_active());
    GlassBackdrop bd{};
    UiColor eff_tint{}, rim_col{};
    int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
    resolve_styled_glass(bx, by, bw, bh, kChromeGlassTint, kChromeCardBorder,
                         GlassVariant::Clear, white_fg, -1, false,
                         bd, eff_tint, rim_col, a_top, a_bot, rim_top, rim_bot);
    LATTER_CHECK(!bd.valid && bd.sample_count == 0U);
    LATTER_CHECK(a_top == kGlassClearAlphaMaxQ8 && a_bot == kGlassAlphaMinQ8);
    const GlassBackdrop mean_off = sample_glass_mean(bx, by, bw, bh);
    LATTER_CHECK(!mean_off.valid && mean_off.sample_count == 0U);

    // Auto over active dark backdrop -> samples bounded pixels and stays crisp airy Clear
    fix.fill(dark_hw);
    backdrop_active() = true;
    resolve_styled_glass(bx, by, bw, bh, kChromeGlassTint, kChromeCardBorder,
                         GlassVariant::Clear, white_fg, -1, false,
                         bd, eff_tint, rim_col, a_top, a_bot, rim_top, rim_bot);
    LATTER_CHECK(bd.valid && bd.sample_count > 0U && bd.sample_count <= 200U);
    LATTER_CHECK(a_top == kGlassClearAlphaMaxQ8 && a_bot == kGlassAlphaMinQ8);

    // Wide card sample budget check (independent step_x / step_y keeps S3 PSRAM reads <= 192)
    const int wide_w = std::min(kMaxContentColW, SCREEN_W - 8);
    const GlassBackdrop wide_bd = sample_glass_backdrop((SCREEN_W - wide_w) / 2, by, wide_w, 44);
    LATTER_CHECK(wide_bd.valid && wide_bd.sample_count > 0U && wide_bd.sample_count <= 192U);

    // Auto over active bright backdrop -> smoothly darkens and achieves >= 4.5:1 WCAG AA contrast
    fix.fill(bright_hw);
    backdrop_active() = true;
    resolve_styled_glass(bx, by, bw, bh, kChromeGlassTint, kChromeCardBorder,
                         GlassVariant::Clear, white_fg, -1, false,
                         bd, eff_tint, rim_col, a_top, a_bot, rim_top, rim_bot);
    LATTER_CHECK(bd.valid && bd.sample_count > 0U && bd.sample_count <= 200U);
    LATTER_CHECK(a_top > kGlassClearAlphaMaxQ8 && a_bot > kGlassClearAlphaQ8);
    const float fg_lum = relative_luminance(white_fg);
    LATTER_CHECK(contrast_ratio(fg_lum, relative_luminance(mix_color_q8(bright_bg, eff_tint, a_top))) >= 4.5f);
    LATTER_CHECK(contrast_ratio(fg_lum, relative_luminance(mix_color_q8(bright_bg, eff_tint, a_bot))) >= 4.5f);
    render_card_box(fix.surf, bx, by, bw, bh, kChromeGlassTint, kChromeCardBorder, kRadiusCardPx(),
                    GlassVariant::Clear, white_fg);
    const UiColor auto_rendered = decode_px(fix.pixels[static_cast<size_t>(by + bh / 2) * SCREEN_W + (bx + bw / 2)]);
    LATTER_CHECK(contrast_ratio(fg_lum, relative_luminance(auto_rendered)) >= 4.5f);

    // Clear over active bright backdrop -> stays airy Clear (zero darkening)
    persisted_prefs().glass_style = static_cast<uint8_t>(GlassStyle::Clear);
    fix.fill(bright_hw);
    backdrop_active() = true;
    resolve_styled_glass(bx, by, bw, bh, kChromeGlassTint, kChromeCardBorder,
                         GlassVariant::Clear, white_fg, -1, false,
                         bd, eff_tint, rim_col, a_top, a_bot, rim_top, rim_bot);
    LATTER_CHECK(a_top == kGlassClearAlphaMaxQ8 && a_bot == kGlassAlphaMinQ8);

    // Dark over active bright backdrop -> ReadableDark with >= 4.5:1 WCAG AA contrast
    persisted_prefs().glass_style = static_cast<uint8_t>(GlassStyle::Dark);
    fix.fill(bright_hw);
    backdrop_active() = true;
    resolve_styled_glass(bx, by, bw, bh, kChromeGlassTint, kChromeCardBorder,
                         GlassVariant::Clear, white_fg, -1, false,
                         bd, eff_tint, rim_col, a_top, a_bot, rim_top, rim_bot);
    LATTER_CHECK(a_top >= kGlassReadableAlphaMinQ8 && a_bot >= 200);
    render_card_box(fix.surf, bx, by, bw, bh, kChromeGlassTint, kChromeCardBorder, kRadiusCardPx(),
                    GlassVariant::Clear, white_fg);
    const UiColor dark_rendered = decode_px(fix.pixels[static_cast<size_t>(by + bh / 2) * SCREEN_W + (bx + bw / 2)]);
    LATTER_CHECK(contrast_ratio(fg_lum, relative_luminance(dark_rendered)) >= 4.5f);

    persisted_prefs().glass_style = saved_gs;
  }

  // 3. Camera scanline blitter scales cleanly across wide landscape widths and marks backdrop_active()
  {
    HostSurfaceFixture fix{};
    fix.fill(0U);
    LATTER_CHECK(!backdrop_active());
    const uint16_t src_px[4] = {0xF800U, 0x07E0U, 0x001FU, 0xFFFFU};
    const CameraRect vp{0, 0, SCREEN_W, SCREEN_H};
    LATTER_CHECK(blit_rgb565_fitted_frame(fix.surf, src_px, 2, 2, vp, /*fill=*/true, /*clip_round_bezel=*/false));
    LATTER_CHECK(backdrop_active());
    const uint16_t right_px = fix.pixels[static_cast<size_t>(SCREEN_CY) * SCREEN_W + static_cast<size_t>(SCREEN_W - 2)];
    LATTER_CHECK(right_px != 0U);
    evict_registered_psram_caches();
  }

  // 4. Rendered Home Activity Card & Camera Single Action Pills hit-rect alignment (zero landscape crush)
  {
    HostSurfaceFixture fix{};
    populate_rich_host_state(fix.ctx);
    auto &rs = runtime_state();
    // Add 3+ active items so the Activity Card renders with the [v] expand column.
    rs.stopwatch.running = true;
    rs.stopwatch.start_time_ms = 10000U;
    rs.timers.slots[0].running = true;
    rs.timers.slots[0].remaining_sec = 95;
    rs.timers.slots[0].duration_sec = 120;
    rs.timers.slots[0].target_ms = fix.ctx.now_ms + 95000U;
    rs.timers.slots[1].running = true;
    rs.timers.slots[1].remaining_sec = 240;
    rs.timers.slots[1].duration_sec = 300;
    rs.timers.slots[1].target_ms = fix.ctx.now_ms + 240000U;
    rs.nav.gui_page = PageId::HOME;
    rs.nav.home_subpage = 0;

    fix.fill(0U);
    frame_hits().clear();
    render_home_page(fix.surf, fix.ctx);
    const HitRegion *row0 = frame_hits().find_by_id(WidgetId::HOME_ACT_ROW_0);
    const HitRegion *act0 = frame_hits().find_by_id(WidgetId::HOME_ACT_ACTION_0);
    const HitRegion *exp = frame_hits().find_by_id(WidgetId::HOME_ACT_EXPAND);
    LATTER_CHECK(row0 != nullptr && act0 != nullptr && exp != nullptr);
    const int row0_w = row0->x1 - row0->x0 + 1;
    const int act0_w = act0->x1 - act0->x0 + 1;
    const int exp_w = exp->x1 - exp->x0 + 1;
    LATTER_CHECK(act0_w == scale_s(56) && exp_w == scale_s(52));
    LATTER_CHECK(row0->x1 + 1 == act0->x0 && act0->x1 + 1 == exp->x0);
    LATTER_CHECK(row0_w + act0_w + exp_w == layout().home_activity_w());
    const int text_avail = row0_w - scale_s(48) - scale_s(8);
    LATTER_CHECK(text_avail >= ((SCREEN_W >= 466) ? 180 : 120));

    // Camera Single HUD with 4 configured actions (Listen / Talk / Reply / Unlock)
    rs.nav.gui_page = PageId::CAMERAS;
    rs.cameras.tab = CamerasTab::SINGLE;
    rs.cameras.hud_active = true;
    rs.cameras.quick_reply_open = false;
    fix.fill(0U);
    frame_hits().clear();
    render_cameras_page(fix.surf, fix.ctx);
    const uint16_t cam_wids[4] = {
        WidgetId::CAM_LISTEN_BTN, WidgetId::CAM_PTT_BTN,
        WidgetId::CAM_REPLY_MENU_BTN, WidgetId::CAM_UNLOCK_BTN,
    };
    const HitRegion *prev_pill = nullptr;
    int pill_found = 0;
    for (uint16_t wid : cam_wids) {
      const HitRegion *reg = frame_hits().find_by_id(wid);
      if (reg == nullptr) continue;
      ++pill_found;
      const int pw = reg->x1 - reg->x0 + 1;
      const int ph_padded = (reg->y1 - reg->y0 + 1) + 2 * static_cast<int>(reg->hit_pad);
      LATTER_CHECK(reg->x0 >= 0 && reg->x1 < SCREEN_W);
      LATTER_CHECK(pw >= ((SCREEN_W >= 720) ? 100 : 64));
      LATTER_CHECK(ph_padded >= layout().min_hit_px());
      if (prev_pill != nullptr) {
        LATTER_CHECK(prev_pill->x1 < reg->x0);
      }
      prev_pill = reg;
    }
    LATTER_CHECK(pill_found >= 2);
    evict_registered_psram_caches();
  }
}

void test_additive_cold_boot_sky_and_celestial_d085() {
  using namespace latter_ui;
  using namespace latter_core;

  // 1. Consistent Sun & Moon visibility across weather conditions (both hidden under overcast/rain/snow/storm)
  LATTER_CHECK(weather_condition_shows_celestial("sunny", false));
  LATTER_CHECK(weather_condition_shows_celestial("partlycloudy", false));
  LATTER_CHECK(weather_condition_shows_celestial("windy", false));
  LATTER_CHECK(weather_condition_shows_celestial("fog", false));
  LATTER_CHECK(!weather_condition_shows_celestial("cloudy", false));
  LATTER_CHECK(!weather_condition_shows_celestial("overcast", false));
  LATTER_CHECK(!weather_condition_shows_celestial("rainy", false));
  LATTER_CHECK(!weather_condition_shows_celestial("pouring", false));
  LATTER_CHECK(!weather_condition_shows_celestial("snowy", false));
  LATTER_CHECK(!weather_condition_shows_celestial("lightning-rainy", false));
  LATTER_CHECK(!weather_condition_shows_celestial("neutral", false));

  LATTER_CHECK(weather_condition_shows_celestial("clear-night", true));
  LATTER_CHECK(weather_condition_shows_celestial("partlycloudy", true));
  LATTER_CHECK(weather_condition_shows_celestial("windy", true));
  LATTER_CHECK(weather_condition_shows_celestial("fog", true));
  LATTER_CHECK(!weather_condition_shows_celestial("cloudy", true));
  LATTER_CHECK(!weather_condition_shows_celestial("overcast", true));
  LATTER_CHECK(!weather_condition_shows_celestial("rainy", true));
  LATTER_CHECK(!weather_condition_shows_celestial("pouring", true));
  LATTER_CHECK(!weather_condition_shows_celestial("snowy", true));
  LATTER_CHECK(!weather_condition_shows_celestial("lightning-rainy", true));
  LATTER_CHECK(!weather_condition_shows_celestial("neutral", true));

  // 2. Pure-gradient pre-sync Home sky (no wisps, no stars, no Sun/Moon, no weather text/hitboxes)
  {
    HostSurfaceFixture fix{};
    populate_rich_host_state(fix.ctx);
    auto &rs = runtime_state();
    auto &prefs = persisted_prefs();
    auto &ws = weather_state();
    auto &ac = area_ctx_state();
    prefs.weather_background = true;
    prefs.home_sun = true;
    prefs.home_moon = true;
    prefs.home_show_weather = true;
    prefs.wake_greeting = true;
    fix.ctx.home_weather_bg = true;
    fix.ctx.home_sun_arc = true;
    fix.ctx.home_moon_phase = true;
    fix.ctx.home_wake_greeting = true;
    timer_slots()[0].running = false;
    sync_multi_timer_state();
    media_state() = MediaState{};
    ws = WeatherState{};
    ac.wallpaper_url.clear();
    rs.nav.gui_page = PageId::HOME;
    rs.nav.home_subpage = 0;

    // 2a. Non-RTC cold boot (!now.is_valid()): solid black backdrop (never flashes daytime blue at midnight)
    //     and wake_greeting_pending stays armed during the 15 s boot grace window.
    auto make_wall_time = [](uint16_t yr, uint8_t mo, uint8_t dom, uint8_t hr, uint8_t min, int64_t ts) {
      UiWallTime wt{};
      wt.year = yr;
      wt.month = mo;
      wt.day_of_month = dom;
      wt.day_of_week = 3;
      wt.day_of_year = 328;
      wt.hour = hr;
      wt.minute = min;
      wt.second = 0;
      wt.timestamp = ts;
      return wt;
    };
    dismiss_wake_greeting();
    rs.nav.wake_greeting_pending = true;
    fix.ctx.now = UiWallTime{};
    fix.ctx.now_ms = 2000U;
    fix.fill(0x1234U);
    frame_base_begin();
    frame_hits().clear();
    render_home_page(fix.surf, fix.ctx);
    LATTER_CHECK(!backdrop_active());
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_QUICK_BTN) == nullptr);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_WEATHER_HERO) == nullptr);
    LATTER_CHECK(rs.nav.wake_greeting_pending);

    // 2b. RTC / synced daytime cold boot (hour = 12, ws empty): pure day blue gradient, no celestial/weather hitboxes,
    //     and wake greeting latches once (surviving live weather push without mutating mid-display).
    fix.ctx.now = make_wall_time(2026, 11, 24, 12, 15, 1795551300LL);
    fix.ctx.now_ms = 2500U;
    fix.fill(0U);
    frame_base_begin();
    frame_hits().clear();
    render_home_page(fix.surf, fix.ctx);
    LATTER_CHECK(backdrop_active());
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_QUICK_BTN) == nullptr);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_WEATHER_HERO) == nullptr);
    LATTER_CHECK(!rs.nav.wake_greeting_pending);
    LATTER_CHECK(is_wake_greeting_visible(2500U, true));

    // 2c. Pure-gradient pre-sync night sky (hour = 23, ws empty): zero bright star pixels vs static Day & Night Sky.
    auto decode_px_rgb = [](uint16_t hw_px, uint8_t &r, uint8_t &g, uint8_t &b) {
      const uint16_t raw = decode_hw_rgb565_raw(hw_px);
      r = static_cast<uint8_t>(((raw >> 11) & 0x1FU) << 3);
      g = static_cast<uint8_t>(((raw >> 5) & 0x3FU) << 2);
      b = static_cast<uint8_t>((raw & 0x1FU) << 3);
    };
    fix.ctx.now = make_wall_time(2026, 11, 24, 23, 5, 1795590300LL);
    fix.fill(0U);
    frame_base_begin();
    render_neutral_day_night_sky_base(fix.surf, fix.ctx.cx, fix.ctx.cy, true);
    int neutral_bright_px = 0;
    for (uint16_t raw : fix.pixels) {
      uint8_t r = 0, g = 0, b = 0;
      decode_px_rgb(raw, r, g, b);
      if (r > 40 || g > 55 || b > 75) ++neutral_bright_px;
    }
    LATTER_CHECK(neutral_bright_px == 0);

    fix.fill(0U);
    frame_base_begin();
    render_static_day_night_sky_base(fix.surf, fix.ctx.cx, fix.ctx.cy, true);
    int static_star_px = 0;
    for (uint16_t raw : fix.pixels) {
      uint8_t r = 0, g = 0, b = 0;
      decode_px_rgb(raw, r, g, b);
      if (r > 80 && g > 80 && b > 80) ++static_star_px;
    }
    LATTER_CHECK(static_star_px > 20);

    // 2d. Live night overcast/rain hides Moon & omits HOME_QUICK_BTN; live clear-night with valid time shows Moon.
    ws.epoch = 1U;
    ws.rev = 1U;
    ws.condition = "rainy";
    ws.temperature = 58.0f;
    ws.sun_state = "below_horizon";
    ws.moon_phase = "first_quarter";
    fix.ctx.now_ms = 3200U;
    fix.fill(0U);
    frame_base_begin();
    frame_hits().clear();
    render_home_page(fix.surf, fix.ctx);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_QUICK_BTN) == nullptr);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_WEATHER_HERO) != nullptr);
    LATTER_CHECK(is_wake_greeting_visible(3200U, true));

    // Clear night with unknown time & empty moon_phase -> never renders a bogus Full Moon or hitbox.
    ws.condition = "clear-night";
    ws.moon_phase.clear();
    fix.ctx.now = UiWallTime{};
    fix.fill(0U);
    frame_base_begin();
    frame_hits().clear();
    render_home_page(fix.surf, fix.ctx);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_QUICK_BTN) == nullptr);

    // Clear night with valid time -> Moon renders and registers HOME_QUICK_BTN.
    fix.ctx.now = make_wall_time(2026, 11, 24, 23, 5, 1795590300LL);
    fix.fill(0U);
    frame_base_begin();
    frame_hits().clear();
    render_home_page(fix.surf, fix.ctx);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::HOME_QUICK_BTN) != nullptr);

    // 3. Weather App pre-sync parity: neutral day/night base + Syncing... state with zero live weather.
    ws = WeatherState{};
    rs.nav.gui_page = PageId::WEATHER;
    rs.nav.sky_weather_subpage = 0;
    prefs.weather_app_background = true;
    fix.fill(0U);
    frame_base_begin();
    frame_hits().clear();
    render_weather_page(fix.surf, fix.ctx);
    LATTER_CHECK(backdrop_active());

    dismiss_wake_greeting();
    evict_registered_psram_caches();
  }
}

}  // namespace

void run_ui_rgb565_5geom_tests() {
  test_all_14_pages_and_system_takeovers_rgb565_5geom();
  test_home_hub_d048_button_only_glass_and_conditional_controls();
  test_wcag_palette_and_surface_contrast_5geom();
  test_art_canvas_lut_and_dual_sink_parity_5geom();
  test_rotated_fallback_print_and_adaptive_glass_style();
  test_additive_cold_boot_sky_and_celestial_d085();
}

}  // namespace latter_test
