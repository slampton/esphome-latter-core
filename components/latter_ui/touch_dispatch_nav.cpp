// ==============================================================================
// Latter Global Takeovers, Quick Actions, Shortcut Routes, Home Dock & Nav
// (components/latter_ui/touch_dispatch_nav.cpp)
// ==============================================================================

#include "touch_internal.h"

namespace latter_ui {

namespace {

inline void reset_common_nav_fields(DeviceRuntimeState &rs, uint32_t now_ms) {
  shortcut_confirm_state().clear();
  rs.nav.quick_actions_open = false;
  rs.nav.shortcut_origin = ShortcutOrigin::NONE;
  timer_view_slot() = 0;
  rs.alarms.edit_idx = 0;
  rs.cal.drilldown = CalDrilldown::NONE;
  rs.cal.delete_confirm_open = false;
  cam_quick_reply_open() = false;
  cam_expanded_fullscreen() = false;
  touch_detail::reset_music_to_now_playing(rs, now_ms, false);
  rs.nav.system_menu_mode = 0;
  rs.nav.active_game = 0;
  rs.music.direct_nav = false;
  rs.nav.sleep_menu_page = 1;
  rs.nav.display_menu_page = 1;
  rs.nav.system_dashboard_page = 1;
  rs.nav.menu_scroll_active = false;
  rs.music.hud_active = false;
  rs.nav.remote_subpage = 0;
  rs.nav.tools_subpage = 0;
  rs.nav.tools_menu_page = 0;
  rs.nav.pinned_menu_page = 0;
  rs.nav.sky_weather_subpage = 0;
  rs.nav.home_settings_page = 1;
}

}  // namespace

namespace touch_detail {

void launch_dock_app(int chosen_app, DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms) {
  rs.nav.shortcut_origin = ShortcutOrigin::NONE;
  shortcut_confirm_state().clear();
  rs.nav.quick_actions_open = false;
  home_dock_page() = 0;

  if (chosen_app == PageId::HOME_HUB) {
    enter_home_hub_tab(-1, false, rs, rt, now_ms);
  } else if (chosen_app == PageId::CAMERAS) {
    stop_calendar_dictation_if_active(rs, rt, now_ms);
    stop_broadcast_dictation_if_active(rs, rt, now_ms);
    enter_cameras_app(-1, "", false, rs, rt, now_ms);
  } else {
    if (is_camera_view_active(rs)) {
      leave_home_hub_cameras_if_active(rs, rt, now_ms);
    }
    stop_calendar_dictation_if_active(rs, rt, now_ms);
    stop_broadcast_dictation_if_active(rs, rt, now_ms);
    rs.nav.gui_page = chosen_app;
    rs.nav.home_subpage = 0;
  }

  if (chosen_app == PageId::PINNED) {
    rs.nav.pinned_menu_page = 0;
    rs.nav.menu_scroll_active = false;
  } else if (chosen_app == PageId::CALENDAR) {
    const int32_t today_ymd = resolve_touch_today_ymd(rt);
    rs.cal.subpage = CalendarSubpage::OVERVIEW;
    rs.cal.drilldown = CalDrilldown::NONE;
    rs.cal.delete_confirm_open = false;
    rs.cal.selected_ymd = today_ymd;
    rs.cal.overview_page = 0;
    rs.cal.day_page = 0;
    rs.cal.week_page = 0;
    rs.cal.picker_page = 0;
    ensure_calendar_window_for_ymd(today_ymd, persisted_prefs(),
                                   calendar_window_store().window_start_ymd == 0);
  } else if (chosen_app == PageId::MUSIC) {
    reset_music_to_now_playing(rs, now_ms, true);
    mode_bar_anim().snap(0, 0);
    const auto &ms = media_state();
    if (rt.select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE) != "None" &&
        (!ms.is_local || rt.album_art_w == 0) && ms.art_url != rs.music.loaded_art_url) {
      rt.cmd(UiHostCmd::FETCH_REMOTE_ART_DEBOUNCED);
    }
  } else if (chosen_app == PageId::TIME) {
    rs.nav.time_subpage = TimeSubpage::CLOCK;
    rs.nav.world_clock_page = 0;
    rs.nav.alarm_edit_field = 1;
    timer_view_slot() = 0;
    rs.alarms.edit_idx = 0;
  } else if (chosen_app == PageId::WEATHER) {
    rs.nav.sky_weather_subpage = 0;
  } else if (chosen_app == PageId::GAMES) {
    rs.nav.game_carousel_idx = 0;
    rs.nav.menu_scroll_active = false;
  } else if (chosen_app == PageId::TOOLS) {
    rs.nav.tools_menu_page = 0;
    rs.nav.menu_scroll_active = false;
  } else if (chosen_app == PageId::SYSTEM) {
    rs.nav.system_menu_mode = 0;
    rs.nav.system_dashboard_page = 1;
    rs.nav.sleep_menu_page = 1;
    rs.nav.display_menu_page = 1;
    rs.nav.menu_scroll_active = false;
  } else if (chosen_app == PageId::AREA) {
    area_cat_dock_page() = 0;
    rs.area.domain_filter = resolve_preferred_area_category();
    if (rs.area.active_area_id.empty()) {
      rs.nav.area_subpage = AreaSubpage::PICKER;
      rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST) = 1;
      do_fetch_area_list_browse();
    } else {
      rs.nav.area_subpage = AreaSubpage::OVERVIEW;
      rs.area.browse_mode = "entities";
      rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
      if (rs.area.domain_filter.empty()) {
        auto *eslots = rs.slots_for(latter_rpc::SlotChannel::AREA_ENTITIES);
        for (size_t si = 0; si < DeviceRuntimeState::kSlotsPerPage; ++si) {
          eslots[si] = BrowseSlotItem{};
        }
        rs.slot_total_pages_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
      } else {
        do_fetch_area_browse();
      }
    }
  }

  rs.nav.tools_subpage = 0;
  rs.nav.remote_subpage = 0;
  rs.nav.active_game = 0;
  mark_dirty_full();
  rt.schedule_disp();
}

bool return_from_shortcut_origin(DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms) {
  const ShortcutOrigin orig = rs.nav.shortcut_origin;
  if (orig == ShortcutOrigin::NONE) return false;
  rs.nav.shortcut_origin = ShortcutOrigin::NONE;
  if (orig == ShortcutOrigin::PINNED_APP) {
    const int ret_pg = rs.nav.shortcut_origin_pinned_page;
    rs.nav.tools_subpage = 0;
    rs.nav.remote_subpage = 0;
    rs.nav.active_game = 0;
    rs.nav.game_btn_pressed = false;
    rs.nav.system_menu_mode = 0;
    rs.nav.home_subpage = 0;
    reset_music_to_now_playing(rs, now_ms, false);
    rs.music.direct_nav = false;
    rs.nav.menu_scroll_active = false;
    rs.nav.gui_page = PageId::PINNED;
    rs.nav.pinned_menu_page = std::clamp(ret_pg, 0, std::max(0, pinned_total_pages() - 1));
    mark_dirty_full();
    rt.schedule_disp();
    return true;
  }
  touch_engine().navigate_home(now_ms);
  return true;
}

void launch_shortcut_route(RouteId route_id, ShortcutOrigin origin,
                           DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                           uint32_t now_ms) {
  if (!is_route_supported_on_board(route_id)) return;
  const int saved_pinned_pg = rs.nav.pinned_menu_page;
  shortcut_confirm_state().clear();
  rs.nav.quick_actions_open = false;
  auto save_origin = [&]() {
    rs.nav.shortcut_origin = origin;
    rs.nav.shortcut_origin_pinned_page = saved_pinned_pg;
  };

  switch (route_id) {
    case RouteId::APP_PINNED:   launch_dock_app(PageId::PINNED, rs, rt, now_ms); return;
    case RouteId::APP_MUSIC:    launch_dock_app(PageId::MUSIC, rs, rt, now_ms); return;
    case RouteId::APP_HOME_HUB: launch_dock_app(PageId::HOME_HUB, rs, rt, now_ms); return;
    case RouteId::APP_AREA:     launch_dock_app(PageId::AREA, rs, rt, now_ms); return;
    case RouteId::APP_TOOLS:    launch_dock_app(PageId::TOOLS, rs, rt, now_ms); return;
    case RouteId::APP_GAMES:    launch_dock_app(PageId::GAMES, rs, rt, now_ms); return;
    case RouteId::SETTINGS_ROOT: launch_dock_app(PageId::SYSTEM, rs, rt, now_ms); return;

    case RouteId::APP_CAMERAS:
      launch_dock_app(PageId::CAMERAS, rs, rt, now_ms);
      save_origin();
      return;

    case RouteId::CAMERAS_GRID:
    case RouteId::CAMERAS_SINGLE:
    case RouteId::CAMERAS_CAROUSEL:
    case RouteId::CAMERAS_SETTINGS: {
      int ctab = CamerasTab::GRID;
      if (route_id == RouteId::CAMERAS_SINGLE) ctab = CamerasTab::SINGLE;
      else if (route_id == RouteId::CAMERAS_CAROUSEL) ctab = CamerasTab::CAROUSEL;
      else if (route_id == RouteId::CAMERAS_SETTINGS) ctab = CamerasTab::SETTINGS;
      enter_cameras_app(ctab, "", false, rs, rt, now_ms);
      save_origin();
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::APP_REMOTE:
      if (has_any_configured_remote()) launch_dock_app(PageId::REMOTE, rs, rt, now_ms);
      return;

    case RouteId::APP_WEATHER:
    case RouteId::WEATHER_CURRENT:
    case RouteId::WEATHER_FORECAST:
    case RouteId::WEATHER_SUN:
    case RouteId::WEATHER_MOON:
      launch_dock_app(PageId::WEATHER, rs, rt, now_ms);
      if (route_id == RouteId::WEATHER_FORECAST) rs.nav.sky_weather_subpage = 1;
      else if (route_id == RouteId::WEATHER_SUN) rs.nav.sky_weather_subpage = 2;
      else if (route_id == RouteId::WEATHER_MOON) rs.nav.sky_weather_subpage = 3;
      return;

    case RouteId::APP_TIME:
    case RouteId::TIME_CLOCK:
    case RouteId::TIME_TIMERS:
    case RouteId::TIME_ALARMS:
    case RouteId::TIME_STOPWATCH:
      launch_dock_app(PageId::TIME, rs, rt, now_ms);
      if (route_id == RouteId::TIME_TIMERS) rs.nav.time_subpage = TimeSubpage::TIMERS;
      else if (route_id == RouteId::TIME_ALARMS) rs.nav.time_subpage = TimeSubpage::ALARMS;
      else if (route_id == RouteId::TIME_STOPWATCH) rs.nav.time_subpage = TimeSubpage::STOPWATCH;
      return;

    case RouteId::APP_CALENDAR:
    case RouteId::CALENDAR_OVERVIEW:
    case RouteId::CALENDAR_DAY:
    case RouteId::CALENDAR_WEEK:
    case RouteId::CALENDAR_MONTH:
      launch_dock_app(PageId::CALENDAR, rs, rt, now_ms);
      save_origin();
      if (route_id == RouteId::CALENDAR_DAY) rs.cal.subpage = CalendarSubpage::DAY;
      else if (route_id == RouteId::CALENDAR_WEEK) rs.cal.subpage = CalendarSubpage::WEEK;
      else if (route_id == RouteId::CALENDAR_MONTH) rs.cal.subpage = CalendarSubpage::MONTH;
      return;

    case RouteId::APP_CLOCK:
      if (is_camera_view_active(rs)) leave_home_hub_cameras_if_active(rs, rt, now_ms);
      rs.nav.shortcut_origin = ShortcutOrigin::NONE;
      rs.nav.gui_page = PageId::CLOCK;
      rs.nav.home_subpage = 0;
      mark_dirty_full();
      rt.schedule_disp();
      return;

    case RouteId::HOME_HUB_ALARM:
    case RouteId::HOME_HUB_INTERCOM:
    case RouteId::HOME_HUB_THERMOSTAT:
    case RouteId::HOME_HUB_ENTRY:
    case RouteId::HOME_HUB_VACUUM: {
      int tab = HomeHubTab::ALARM;
      if (route_id == RouteId::HOME_HUB_INTERCOM) tab = HomeHubTab::INTERCOM;
      else if (route_id == RouteId::HOME_HUB_THERMOSTAT) tab = HomeHubTab::THERMOSTAT;
      else if (route_id == RouteId::HOME_HUB_ENTRY) tab = HomeHubTab::ENTRY;
      else if (route_id == RouteId::HOME_HUB_VACUUM) tab = HomeHubTab::VACUUM;
      rs.nav.shortcut_origin = ShortcutOrigin::NONE;
      enter_home_hub_tab(tab, false, rs, rt, now_ms);
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::AREA_PICKER:
      if (is_camera_view_active(rs)) leave_home_hub_cameras_if_active(rs, rt, now_ms);
      save_origin();
      rs.nav.gui_page = PageId::AREA;
      rs.nav.home_subpage = 0;
      rs.nav.area_subpage = AreaSubpage::PICKER;
      rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST) = 1;
      do_fetch_area_list_browse();
      mark_dirty_full();
      rt.schedule_disp();
      return;

    case RouteId::AREA_SCENE: case RouteId::AREA_LIGHT: case RouteId::AREA_COVER:
    case RouteId::AREA_FAN: case RouteId::AREA_SWITCH: case RouteId::AREA_CLIMATE:
    case RouteId::AREA_MEDIA: case RouteId::AREA_ALL: {
      if (is_camera_view_active(rs)) leave_home_hub_cameras_if_active(rs, rt, now_ms);
      rs.nav.shortcut_origin = ShortcutOrigin::NONE;
      rs.nav.gui_page = PageId::AREA;
      rs.nav.home_subpage = 0;
      const char *dom = "all";
      if (route_id == RouteId::AREA_SCENE) dom = "scene";
      else if (route_id == RouteId::AREA_LIGHT) dom = "light";
      else if (route_id == RouteId::AREA_COVER) dom = "cover";
      else if (route_id == RouteId::AREA_FAN) dom = "fan";
      else if (route_id == RouteId::AREA_SWITCH) dom = "switch";
      else if (route_id == RouteId::AREA_CLIMATE) dom = "climate";
      else if (route_id == RouteId::AREA_MEDIA) dom = "media";
      std::string cats[8];
      const int cnt = collect_area_categories(cats);
      int act_idx = -1;
      for (int i = 0; i < cnt; ++i) {
        if (cats[i] == dom) { act_idx = i; break; }
      }
      rs.area.domain_filter = (act_idx >= 0) ? cats[act_idx] : std::string(dom);
      sync_mode_bar_window(area_cat_dock_page(), std::max(0, act_idx), cnt);
      if (rs.area.active_area_id.empty()) {
        rs.nav.area_subpage = AreaSubpage::PICKER;
        rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST) = 1;
        do_fetch_area_list_browse();
      } else {
        rs.nav.area_subpage = AreaSubpage::OVERVIEW;
        rs.area.browse_mode = "entities";
        rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
        do_fetch_area_browse();
      }
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::MUSIC_SPEAKERS: case RouteId::MUSIC_LIBRARY:
    case RouteId::MUSIC_FAVORITES: case RouteId::MUSIC_PLAYLISTS:
    case RouteId::MUSIC_QUEUE: case RouteId::MUSIC_ALBUM_TRACKS:
    case RouteId::MUSIC_LYRICS: case RouteId::MUSIC_INFO: {
      launch_dock_app(PageId::MUSIC, rs, rt, now_ms);
      save_origin();
      rs.music.direct_nav = true;
      rs.slot_page_for(latter_rpc::SlotChannel::MUSIC) = 1;
      rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
      if (route_id == RouteId::MUSIC_FAVORITES || route_id == RouteId::MUSIC_PLAYLISTS) {
        rs.music.apply_route(route_id);
        mode_bar_anim().snap(rs.music.mode_bar_win_start, static_cast<int>(rs.music.view));
        reset_music_loading_slots(rs, "Loading...");
        fetch_music_browse_for_current_view(rs, now_ms);
      } else {
        MusicView mv = MusicView::LIBRARY;
        if (route_id == RouteId::MUSIC_SPEAKERS) mv = MusicView::SPEAKERS;
        else if (route_id == RouteId::MUSIC_QUEUE) mv = MusicView::QUEUE;
        else if (route_id == RouteId::MUSIC_ALBUM_TRACKS) mv = MusicView::ALBUM_TRACKS;
        else if (route_id == RouteId::MUSIC_LYRICS) mv = MusicView::LYRICS;
        else if (route_id == RouteId::MUSIC_INFO) mv = MusicView::INFO;
        activate_music_peer_tab(mv, now_ms);
        mode_bar_anim().snap(rs.music.mode_bar_win_start, static_cast<int>(rs.music.view));
      }
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::REMOTE_DEVICES:
      if (has_any_configured_remote()) {
        launch_dock_app(PageId::REMOTE, rs, rt, now_ms);
        save_origin();
        rs.nav.remote_subpage = 1;
        remote_control_page() = 0;
        mark_dirty_full();
        rt.schedule_disp();
      }
      return;

    case RouteId::TOOLS_STOPWATCH: case RouteId::TOOLS_LEVEL:
    case RouteId::TOOLS_FLASHLIGHT: case RouteId::TOOLS_CALCULATOR:
    case RouteId::TOOLS_DICE: case RouteId::TOOLS_COIN_FLIP: {
      launch_dock_app(PageId::TOOLS, rs, rt, now_ms);
      save_origin();
      int sub = ToolsSubpage::LAUNCHER;
      if (route_id == RouteId::TOOLS_STOPWATCH) sub = ToolsSubpage::STOPWATCH;
      else if (route_id == RouteId::TOOLS_LEVEL) sub = ToolsSubpage::LEVEL;
      else if (route_id == RouteId::TOOLS_FLASHLIGHT) sub = ToolsSubpage::FLASHLIGHT;
      else if (route_id == RouteId::TOOLS_CALCULATOR) sub = ToolsSubpage::CALCULATOR;
      else if (route_id == RouteId::TOOLS_DICE) sub = ToolsSubpage::DICE;
      else if (route_id == RouteId::TOOLS_COIN_FLIP) sub = ToolsSubpage::COIN_FLIP;
      rs.nav.tools_subpage = sub;
      if (sub == ToolsSubpage::FLASHLIGHT) rt.cmd(UiHostCmd::FLASHLIGHT_FULL_BRIGHTNESS);
      else if (sub == ToolsSubpage::CALCULATOR) reset_calculator_state();
      rt.apply_power_policy();
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::GAMES_MARBLES: case RouteId::GAMES_ARCHERY:
    case RouteId::GAMES_BRICKS: case RouteId::GAMES_CHOMP:
    case RouteId::GAMES_2048: case RouteId::GAMES_MEMORY: {
      const int g_id = 1 + (static_cast<int>(route_id) - static_cast<int>(RouteId::GAMES_MARBLES));
      launch_dock_app(PageId::GAMES, rs, rt, now_ms);
      save_origin();
      int enabled_ids[kMaxGamesLauncherItems]{};
      const int game_cnt = collect_enabled_games(enabled_ids, device_prefs());
      int matched_slot = 0;
      for (int i = 0; i < game_cnt; ++i) {
        if (enabled_ids[i] == g_id) { matched_slot = i; break; }
      }
      rs.nav.game_carousel_idx = matched_slot;
      rs.nav.active_game = g_id;
      audio_arbitrator().cancel_media_idle_debounce();
      if (rt.local_mp_playing) {
        rt.cmd(UiHostCmd::PAUSE_LOCAL_MEDIA_PLAYER);
        audio_arbitrator().cancel_media_idle_debounce();
      }
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }

    case RouteId::SETTINGS_HOME:
      if (is_camera_view_active(rs)) leave_home_hub_cameras_if_active(rs, rt, now_ms);
      save_origin();
      rs.nav.gui_page = PageId::HOME;
      rs.nav.home_subpage = 1;
      rs.nav.home_settings_page = 1;
      mark_dirty_full();
      rt.schedule_disp();
      return;

    case RouteId::SETTINGS_DISPLAY: case RouteId::SETTINGS_SLEEP:
    case RouteId::SETTINGS_SOUND: case RouteId::SETTINGS_VOICE:
    case RouteId::SETTINGS_MUSIC: case RouteId::SETTINGS_ACTIONS:
    case RouteId::SETTINGS_UPDATE: case RouteId::SETTINGS_INFO:
      launch_dock_app(PageId::SYSTEM, rs, rt, now_ms);
      save_origin();
      if (route_id == RouteId::SETTINGS_DISPLAY) { rs.nav.system_menu_mode = 2; rs.nav.display_menu_page = 1; }
      else if (route_id == RouteId::SETTINGS_SLEEP) { rs.nav.system_menu_mode = 1; rs.nav.sleep_menu_page = 1; }
      else if (route_id == RouteId::SETTINGS_SOUND) { rs.nav.system_menu_mode = 7; }
      else if (route_id == RouteId::SETTINGS_VOICE) { rs.nav.system_menu_mode = 11; }
      else if (route_id == RouteId::SETTINGS_MUSIC) { rs.nav.system_menu_mode = 9; rs.nav.audio_menu_parent_page = PageId::SYSTEM; }
      else if (route_id == RouteId::SETTINGS_ACTIONS) { rs.nav.system_menu_mode = 3; }
      else if (route_id == RouteId::SETTINGS_UPDATE) { rs.nav.system_menu_mode = 5; rs.nav.update_check_state = 0; }
      else if (route_id == RouteId::SETTINGS_INFO) { rs.nav.system_menu_mode = 8; }
      mark_dirty_full();
      rt.schedule_disp();
      return;

    case RouteId::NONE:
    default:
      return;
  }
}

}  // namespace touch_detail

void TouchGestureEngine::navigate_to_start_screen(uint32_t now_ms) {
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const int anchor = resolve_anchor_dock_app(prefs);
  if (rs.nav.active_game > 0) rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
  if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
    intercom_session().stop_call_with_hw(now_ms);
  }
  const bool was_cam_page = is_camera_view_active(rs);
  touch_detail::stop_calendar_dictation_if_active(rs, rt_, now_ms);
  touch_detail::stop_broadcast_dictation_if_active(rs, rt_, now_ms);
  TouchEngineAccess::scrub_pin_and_pending(*this, rs, false);
  reset_common_nav_fields(rs, now_ms);
  rs.nav.time_subpage = TimeSubpage::CLOCK;
  rs.nav.world_clock_page = 0;
  const bool start_cam = is_cameras_start_screen(prefs);
  const bool keep_cam = start_cam || (anchor == PageId::CAMERAS);
  if (was_cam_page && !keep_cam) {
    rt_.cmd(UiHostCmd::STOP_CAMERA_STREAM);
    rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
    rs.alert.loaded_camera_url.clear();
    rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
  }
  if (anchor == PageId::HOME || anchor == PageId::CLOCK) {
    rs.nav.gui_page = anchor;
    rs.nav.home_subpage = 0;
    mark_dirty_full();
    rt_.schedule_disp();
    return;
  }
  if (anchor == PageId::REMOTE && !has_any_configured_remote()) {
    rs.nav.gui_page = prefs.home_screen_enabled ? PageId::HOME : PageId::CLOCK;
    rs.nav.home_subpage = 0;
    mark_dirty_full();
    rt_.schedule_disp();
    return;
  }
  if (start_cam || anchor == PageId::CAMERAS) {
    touch_detail::enter_cameras_app(-1, "", false, rs, rt_, now_ms);
    mark_dirty_full();
    rt_.schedule_disp();
    return;
  }
  touch_detail::launch_dock_app(anchor, rs, rt_, now_ms);
}

void TouchGestureEngine::navigate_home(uint32_t now_ms) {
  if (!persisted_prefs().home_screen_enabled) {
    navigate_to_start_screen(now_ms);
    return;
  }
  auto &rs = runtime_state();
  if (rs.nav.active_game > 0) rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
  if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
    intercom_session().stop_call_with_hw(now_ms);
  }
  const bool was_cam_page = is_camera_view_active(rs);
  touch_detail::stop_calendar_dictation_if_active(rs, rt_, now_ms);
  touch_detail::stop_broadcast_dictation_if_active(rs, rt_, now_ms);
  TouchEngineAccess::scrub_pin_and_pending(*this, rs, false);
  rs.nav.gui_page = PageId::HOME;
  rs.nav.home_subpage = 0;
  reset_common_nav_fields(rs, now_ms);
  if (was_cam_page) {
    rs.cameras.doorbell_live_popup_active = false;
    rt_.cmd(UiHostCmd::STOP_CAMERA_STREAM);
    rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
    rs.alert.loaded_camera_url.clear();
    rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
  }
  mark_dirty_full();
  rt_.schedule_disp();
}

bool TouchEngineAccess::handle_down_takeovers(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms) {
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  auto &rt = eng.rt_;
  auto &gest = touch_gesture();

  // Priority 1: Alarm / Timer / Find-My Ringing Takeover (above Voice Assistant!)
  if (rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active) {
    gest.active = false;
    const HitRegion *t_hit = frame_hits().hit_test(tx, ty, kHitTap);
    if (rs.alarms.find_my_active) {
      rs.alarms.find_my_active = false;
      last_screen_wake_ms() = now_ms;
      rt.cmd(UiHostCmd::STOP_ALARM_SOUND);
      rt.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
      mark_dirty_full();
      rt.schedule_disp();
      return true;
    }
    if (rs.alarms.ringing) {
      if (t_hit != nullptr && t_hit->widget_id == WidgetId::ALERT_SNOOZE_BTN) {
        snoozed_alarm_slot() =
            (rs.alarms.ringing_idx >= 1 && rs.alarms.ringing_idx <= 3) ? rs.alarms.ringing_idx : 1;
        rs.alarms.snooze_until = (rt.time_epoch > 0) ? (rt.time_epoch + 300U) : ((now_ms / 1000U) + 300U);
        rs.alarms.ringing = false;
        latter_actions::check_and_emit_timer_alarm_state(now_ms);
        last_screen_wake_ms() = now_ms;
        rt.cmd(UiHostCmd::STOP_ALARM_SOUND);
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      if (t_hit != nullptr && t_hit->widget_id == WidgetId::ALERT_DISMISS_BTN) {
        if (rs.alarms.ringing_idx >= 1 && rs.alarms.ringing_idx <= 3) {
          auto &ap = prefs.alarms[rs.alarms.ringing_idx - 1];
          if (ap.recurrence == 3) {
            ap.enabled = false;
            latter_actions::sync_local_alarms_edit(now_ms);
          }
        }
        rs.alarms.ringing = false;
        rs.alarms.snooze_until = 0;
        snoozed_alarm_slot() = 0;
        latter_actions::check_and_emit_timer_alarm_state(now_ms);
        last_screen_wake_ms() = now_ms;
        rt.cmd(UiHostCmd::STOP_ALARM_SOUND);
        rt.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
        mark_dirty_full();
        rt.schedule_disp();
      }
      return true;
    }
    if (rs.timers.ringing && t_hit != nullptr && t_hit->widget_id == WidgetId::ALERT_DISMISS_BTN) {
      rs.timers.ringing = false;
      rs.timers.va_timer_name.clear();
      sync_multi_timer_state();
      latter_actions::check_and_emit_timer_alarm_state(now_ms);
      last_screen_wake_ms() = now_ms;
      rt.cmd(UiHostCmd::STOP_ALARM_SOUND);
      rt.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
      rt.apply_power_policy();
      mark_dirty_full();
      rt.schedule_disp();
    }
    return true;
  }

  // Priority 2: Voice Assistant Takeover (exempt during live doorbell PTT reply — D-066!)
  if (should_va_takeover_preempt(rs.voice.active, !rs.voice.status_text.empty(),
                                 rs.voice.intercom_turn, rs.nav.gui_page,
                                 rs.cameras.doorbell_live_popup_active)) {
    gest.active = false;
    last_screen_wake_ms() = now_ms;
    rt.cmd(UiHostCmd::CANCEL_VOICE_ASSISTANT);
    return true;
  }

  // Priority 3: Security PIN Pad Modal
  if (ui::pin_pad_state().open) {
    gest.active = false;
    eng.handle_pin_pad_tap(tx, ty, now_ms);
    return true;
  }

  // Priority 4: Push Notification Banner / Doorbell Camera Popup
  if (rs.alert.active) {
    handle_down_alert_overlay(eng, tx, ty, now_ms);
    return true;
  }

  // Priority 5: Shortcut Entity Confirmation Modal (resolves on finger lift in on_touch_up)
  if (shortcut_confirm_state().open) {
    gest.begin(tx, ty, now_ms, rs.nav.gui_page, compute_touch_context(rs));
    return true;
  }

  // Priority 6 (Compositor Priority 9): Quick Actions Overlay (D-074)
  if (rs.nav.quick_actions_open) {
    gest.begin(tx, ty, now_ms, rs.nav.gui_page, compute_touch_context(rs));
    const HitRegion *qa_hit = frame_hits().hit_test(tx, ty, kHitTap);
    const uint16_t qwid = (qa_hit != nullptr) ? qa_hit->widget_id : WidgetId::NONE;
    if (qwid == WidgetId::BACK_BUTTON) {
      gest.active = false;
      const uint32_t qtkey = make_tap_target_key(rs.nav.gui_page, qwid, 0);
      if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, qtkey)) {
        rs.nav.quick_actions_open = false;
        mark_dirty_full();
        rt.schedule_disp();
      }
    }
    return true;
  }

  return false;
}

void TouchEngineAccess::handle_quick_actions_release(TouchGestureEngine &eng, uint32_t now_rel,
                                                     const HitRegion *start_slider) {
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  auto &rt = eng.rt_;
  auto &gest = touch_gesture();
  const auto &ly = layout();
  const TouchPt tp{gest.last_x, gest.last_y};

  if (!gest.swipe_committed && start_slider != nullptr &&
      gest.axis_lock == GestureAxisLock::Slider) {
    if (start_slider->widget_id == WidgetId::QUICK_VOL_SLIDER) {
      const int next_pct = std::clamp(
          10 + static_cast<int>(std::lround(start_slider->norm_x(tp.x) * 90.0f)), 10, 100);
      rt.set_num_val(UiNumberId::SPEAKER_VOLUME, static_cast<float>(next_pct));
      rt.schedule_disp();
      gest.tap_cancelled = true;
    } else if (start_slider->widget_id == WidgetId::QUICK_BRI_SLIDER) {
      const auto bri = resolve_active_brightness_target(
          prefs, weather_state().sun_state, rt.time_valid,
          rt.time_valid ? rt.time_hour : 12,
          area_ctx_state().area_ambient_pct, rs.nav.ambient_bri_override_pct);
      const int next_pct = std::clamp(
          1 + static_cast<int>(std::lround(start_slider->norm_x(tp.x) * 99.0f)), 1, 100);
      if (bri.kind == ActiveBrightnessTargetKind::AMBIENT ||
          bri.kind == ActiveBrightnessTargetKind::AMBIENT_OVERRIDE) {
        rs.nav.ambient_bri_override_pct = static_cast<int8_t>(next_pct);
      } else {
        const UiNumberId nid = touch_detail::brightness_number_id_for_kind(bri.kind);
        rt.set_num_val(nid, static_cast<float>(next_pct));
      }
      rt.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
      rt.schedule_disp();
      gest.tap_cancelled = true;
    }
  } else {
    (void)eng.try_commit_kinematic_swipe_(true, now_rel);
  }

  const bool tap_valid =
      rs.nav.quick_actions_open && !gest.swipe_committed && !gest.tap_cancelled &&
      (gest.max_travel <= ly.tap_max());
  if (!tap_valid) return;

  const HitRegion *hit = frame_hits().hit_test(tp.x, tp.y, kHitTap);
  if (hit == nullptr) {
    hit = frame_hits().hit_test(gest.start_x, gest.start_y, kHitTap);
  }
  const uint16_t wid = (hit != nullptr) ? hit->widget_id : WidgetId::NONE;
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  if (wid == WidgetId::QUICK_VOL_SLIDER &&
      touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
    const float new_vol = step_wrap_range(rt.local_mp_volume, 0.10f, 0.10f, 1.00f);
    rt.set_num_val(UiNumberId::SPEAKER_VOLUME, std::round(new_vol * 100.0f));
    rt.schedule_disp();
  } else if (wid == WidgetId::QUICK_BRI_SLIDER &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
    const auto bri = resolve_active_brightness_target(
        prefs, weather_state().sun_state, rt.time_valid,
        rt.time_valid ? rt.time_hour : 12,
        area_ctx_state().area_ambient_pct, rs.nav.ambient_bri_override_pct);
    if (bri.kind == ActiveBrightnessTargetKind::AMBIENT ||
        bri.kind == ActiveBrightnessTargetKind::AMBIENT_OVERRIDE) {
      rs.nav.ambient_bri_override_pct = static_cast<int8_t>(
          touch_detail::step_brightness_tap_10pct(static_cast<int>(bri.pct)));
    } else {
      const UiNumberId nid = touch_detail::brightness_number_id_for_kind(bri.kind);
      const int cur_pct = static_cast<int>(std::lround(rt.num_val(nid)));
      rt.set_num_val(nid, static_cast<float>(touch_detail::step_brightness_tap_10pct(cur_pct)));
    }
    rt.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
    rt.schedule_disp();
  } else if (wid == WidgetId::QUICK_BEDTIME_TOGGLE &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    rs.nav.ambient_bri_override_pct = -1;
    rt.sw_toggle(UiSwitchId::BEDTIME_MODE);
    rt.schedule_disp();
  } else if (wid == WidgetId::QUICK_DND_TOGGLE &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    rt.sw_toggle(UiSwitchId::DO_NOT_DISTURB);
    rt.schedule_disp();
  } else if (wid == WidgetId::QUICK_HIBERNATE_BTN &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
#if LATTER_HAS_BATTERY
    rs.nav.quick_actions_open = false;
    rt.cmd(UiHostCmd::START_INSTANT_HIBERNATION);
#endif
  } else if (wid == WidgetId::QUICK_SCREEN_OFF_BTN &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    rs.nav.quick_actions_open = false;
    rt.cmd(UiHostCmd::TURN_SCREEN_OFF);
  } else if (wid == WidgetId::QUICK_SCREENSAVER_BTN &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    rs.nav.quick_actions_open = false;
    rt.cmd(UiHostCmd::TRIGGER_SCREENSAVER_NOW);
  } else if (wid == WidgetId::BACK_BUTTON &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    rs.nav.quick_actions_open = false;
    mark_dirty_full();
    rt.schedule_disp();
  }
}

bool TouchGestureEngine::handle_tap_nav_and_takeovers_(
    int /*tx*/, int /*ty*/, uint32_t now_rel, const HitRegion *hit, uint16_t wid) {
  auto &rs = runtime_state();
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  if (shortcut_confirm_state().open) {
    auto &sc = shortcut_confirm_state();
    if ((wid == WidgetId::CONFIRM_CANCEL || wid == WidgetId::BACK_BUTTON || wid == WidgetId::NONE) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      sc.clear();
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CONFIRM_ACCEPT &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const std::string ent_id = sc.entity_id;
      const std::string act = sc.action;
      const std::string lbl = sc.line1;
      sc.clear();
      if (!ent_id.empty() && !act.empty()) {
        latter_actions::emit_shortcut_action(ent_id, act);
        touch_detail::apply_optimistic_shortcut_feedback(ent_id, act, lbl, rs, now_rel);
      }
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return true;
  }

  if (rs.nav.gui_page == PageId::HOME && rs.nav.home_subpage == 0 &&
      wid >= WidgetId::DOCK_SLOT_BASE && wid < WidgetId::DOCK_SLOT_BASE + 4) {
    if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      ShortcutItem *dock_items = dock_items_scratch();
      const int dock_cnt = collect_effective_dock_items(dock_items, kMaxEffectiveDockItems);
      if (hit != nullptr && hit->param >= 0 && hit->param < dock_cnt) {
        const ShortcutItem chosen = dock_items[hit->param];
        touch_detail::activate_shortcut_item(chosen, ShortcutOrigin::HOME_DOCK, rs, rt_, now_rel);
      }
    }
    return true;
  }

  if (rs.nav.gui_page == PageId::WEATHER) {
    if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4 &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int target_sub = std::clamp(static_cast<int>(hit->param), 0, 3);
      if (target_sub != rs.nav.sky_weather_subpage) {
        rs.nav.sky_weather_subpage = target_sub;
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
    return true;
  }

  return false;
}

}  // namespace latter_ui
