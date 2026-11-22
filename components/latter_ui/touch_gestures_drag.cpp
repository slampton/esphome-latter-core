// ==============================================================================
// Latter Kinematic Swipes, Continuous Sliders/Color-Wheels, Lyrics Scroll & Holds
// (components/latter_ui/touch_gestures_drag.cpp)
// ==============================================================================

#include "touch_internal.h"

namespace latter_ui {

bool TouchGestureEngine::try_commit_kinematic_swipe_(bool is_release, uint32_t now_ms) {
  auto &gest = touch_gesture();
  if (!gest.active || gest.swipe_committed || gest.axis_lock == GestureAxisLock::Slider) {
    return gest.swipe_committed;
  }
  if (shortcut_confirm_state().open || (!is_release && (now_ms - gest.last_swipe_ms < TOUCH_POST_SWIPE_LOCKOUT))) {
    return false;
  }

  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &ly = layout();
  const int dx = gest.last_x - gest.start_x;
  const int dy = gest.last_y - gest.start_y;
  const uint32_t dur_ms = (now_ms >= gest.start_ms) ? (now_ms - gest.start_ms) : 0U;
  const int v_dir = eval_kinematic_vert_swipe(dx, dy, dur_ms, is_release, LATTER_PANEL_PPI, gest.axis_lock);
  const int h_dir = eval_kinematic_horiz_swipe(dx, dy, dur_ms, is_release, LATTER_PANEL_PPI, gest.axis_lock);

  auto commit_swipe = [&]() {
    gest.swipe_committed = true;
    gest.tap_cancelled = true;
    gest.last_swipe_ms = now_ms;
  };
  auto step_v_pager = [&](int cur, int lo, int hi, bool wrap, int page_id, bool &changed) {
    commit_swipe();
    return step_pager_index(cur, lo, hi, v_dir, wrap, GestureAxisLock::Vertical, page_id, now_ms, changed);
  };
  auto step_anim_v_pager = [&](int cur, int lo, int hi, int page_id) {
    bool changed = false;
    const int target_pg = step_v_pager(cur, lo, hi, false, page_id, changed);
    if (changed) {
      rs.nav.menu_scroll_active = true;
      rs.nav.menu_scroll_dir = (target_pg > cur) ? 1 : -1;
      rs.nav.menu_scroll_target_page = target_pg;
      rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
    } else {
      rt_.schedule_disp();
    }
    return true;
  };

  // 0. Quick Actions Overlay Takeover: bottom-edge swipe-up dismisses; block all underlying swipes
  if (rs.nav.quick_actions_open) {
    if (gest.start_y >= ly.home_zone_y() && v_dir > 0) {
      commit_swipe();
      rs.nav.quick_actions_open = false;
      mark_dirty_full();
      rt_.schedule_disp();
      return true;
    }
    return false;
  }

  // 1. Universal Bottom-Edge Steep Upward Swipe -> Home ([HOME PREDICATE])
  if (can_go_home(rs) && gest.start_y >= ly.home_zone_y() && v_dir > 0) {
    commit_swipe();
    navigate_home(now_ms);
    return true;
  }

  // 1b. Universal Top-Edge Swipe-Down -> Quick Actions Overlay (D-074)
  if (!rs.nav.ota_in_progress && !rs.nav.is_hibernating &&
      rs.nav.display_power_state == 0 && rt_.panel_is_on &&
      !rs.alarms.ringing && !rs.timers.ringing && !rs.alarms.find_my_active &&
      !rs.alert.active && !ui::pin_pad_state().open && !shortcut_confirm_state().open &&
      !should_va_takeover_preempt(rs.voice.active, !rs.voice.status_text.empty(),
                                  rs.voice.intercom_turn, rs.nav.gui_page,
                                  rs.cameras.doorbell_live_popup_active) &&
      rs.nav.active_game == 0 &&
      gest.start_y <= ly.top_edge_zone_y() && v_dir < 0 &&
      frame_hits().hit_test(gest.start_x, gest.start_y, kHitHorizSlider | kHitVertSlider) == nullptr) {
    commit_swipe();
    rs.nav.quick_actions_open = true;
    mark_dirty_full();
    rt_.schedule_disp();
    return true;
  }

  // 2. Page 1: Home (Full-Canvas App Dock Horizontal Swipe & Active Items Vertical Paging)
  if (rs.nav.gui_page == PageId::HOME) {
    if (rs.nav.home_subpage == 0) {
      if (h_dir != 0) {
        commit_swipe();
        ShortcutItem *dock_items = dock_items_scratch();
        const int dock_cnt = collect_effective_dock_items(dock_items, kMaxEffectiveDockItems);
        const int dock_pages = std::max(1, (dock_cnt + 3) / 4);
        bool changed = false;
        home_dock_page() = step_pager_index(
            home_dock_page(), 0, dock_pages - 1, h_dir, false,
            GestureAxisLock::Horizontal, PageId::HOME, now_ms, changed);
        if (changed) rt_.schedule_disp();
        else rt_.schedule_band_disp(dock_dirty_band_y0(), SCREEN_H - 1);
        return true;
      }
      return false;
    }
    if (rs.nav.home_subpage == 1 && v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
      bool changed = false;
      rs.nav.home_settings_page = step_v_pager(
          std::clamp(rs.nav.home_settings_page, 1, 2), 1, 2, false, PageId::HOME, changed);
      rt_.schedule_disp();
      return true;
    }
    if (rs.nav.home_subpage == 2 && v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
      int act_cnt = 0;
      (void)home_activity_rendered_items(act_cnt);
      bool changed = false;
      home_activity_overlay_page() = step_v_pager(
          home_activity_overlay_page(), 0, std::max(0, (act_cnt - 1) / 5), false, PageId::HOME, changed);
      rt_.schedule_disp();
      return true;
    }
    return false;
  }

  // 3. Page 2: Clean-Sheet Music App v3 (Peer Tab Swipe + List Paging — D-059 / D-060)
  if (rs.nav.gui_page == PageId::MUSIC) {
    if (rs.music.takeover != MusicTakeover::NONE) return false;
    auto step_music_slots = [&](bool wrap, bool touch_hud) {
      music_scroll_tap_lockout_until_ms() = now_ms + 160U;
      int &b_page = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
      const int b_tot = std::max(1, rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC));
      bool changed = false;
      const int next_pg = step_v_pager(b_page, 1, b_tot, wrap, PageId::MUSIC, changed);
      if (touch_hud) rs.music.hud_timer = now_ms;
      if (changed) {
        b_page = next_pg;
        last_music_page_req_ms() = now_ms;
        touch_detail::fetch_music_browse_for_current_view(rs, now_ms);
      }
      rt_.schedule_disp();
      return true;
    };
    if (rs.music.drilldown == MusicDrilldown::NONE) {
      if (h_dir != 0) {
        commit_swipe();
        rs.music.direct_nav = false;
        activate_music_peer_tab(step_music_peer_tab(rs.music.view, h_dir), now_ms);
        mark_dirty_full();
        rt_.schedule_disp();
        return true;
      }
      if (v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
        if (rs.music.view == MusicView::LIBRARY || rs.music.view == MusicView::SETTINGS) {
          music_scroll_tap_lockout_until_ms() = now_ms + 160U;
          bool changed = false;
          int &pg = (rs.music.view == MusicView::LIBRARY) ? rs.music.category_page : rs.music.settings_page;
          pg = step_v_pager(std::clamp(pg, 1, 2), 1, 2, false, PageId::MUSIC, changed);
          rs.music.hud_timer = now_ms;
          rt_.schedule_disp();
          return true;
        }
        if (rs.music.view == MusicView::SPEAKERS ||
            rs.music.view == MusicView::QUEUE ||
            rs.music.view == MusicView::ALBUM_TRACKS) {
          return step_music_slots(false, true);
        }
      }
      return false;
    }
    if (rs.music.drilldown == MusicDrilldown::LIB_LIST && v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
      return step_music_slots(
          rs.music.selected_category_mode == "artist" || rs.music.selected_category_mode == "album", false);
    }
    return false;
  }

  // 4. Page 3: Games Carousel & 2048 Board Flicks
  if (rs.nav.gui_page == PageId::GAMES) {
    if (rs.nav.active_game == 0 && !rs.nav.menu_scroll_active && h_dir != 0) {
      commit_swipe();
      const int game_cnt = active_games_launcher_count(device_prefs());
      if (game_cnt > 1) {
        const int cur_slot = std::clamp(rs.nav.game_carousel_idx, 0, game_cnt - 1);
        rs.nav.game_carousel_idx = cur_slot;
        rs.nav.menu_scroll_active = true;
        rs.nav.menu_scroll_dir = h_dir;
        rs.nav.menu_scroll_target_page = step_wrap(cur_slot, h_dir, game_cnt);
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      } else {
        bool changed = false;
        (void)step_pager_index(0, 0, 0, h_dir, false, GestureAxisLock::Horizontal, PageId::GAMES, now_ms, changed);
        rt_.schedule_disp();
      }
      return true;
    }
    if (rs.nav.active_game == 5 && is_release &&
        (std::abs(dx) >= ly.flick_min() || std::abs(dy) >= ly.flick_min())) {
      commit_swipe();
      if (std::abs(dx) > std::abs(dy)) game_2048_pending_dir() = (dx > 0) ? 2 : 4;
      else game_2048_pending_dir() = (dy > 0) ? 3 : 1;
      rt_.schedule_disp();
      return true;
    }
    return false;
  }

  // 5. Page 4: System Animated 2-Page Vertical Menus (Modes 0, 1, 2)
  if (rs.nav.gui_page == PageId::SYSTEM && !rs.nav.menu_scroll_active &&
      (rs.nav.system_menu_mode == 0 || rs.nav.system_menu_mode == 1 || rs.nav.system_menu_mode == 2) &&
      v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
    const int cur_pg = (rs.nav.system_menu_mode == 0)
                           ? rs.nav.system_dashboard_page
                           : ((rs.nav.system_menu_mode == 1) ? rs.nav.sleep_menu_page : rs.nav.display_menu_page);
    return step_anim_v_pager(cur_pg, 1, 2, PageId::SYSTEM);
  }

  // 6. Page 5: Weather & Sky (Unified Canvas + Mode Bar Horizontal Wrapping Swipe)
  if (rs.nav.gui_page == PageId::WEATHER && h_dir != 0) {
    commit_swipe();
    rs.nav.sky_weather_subpage = step_wrap(rs.nav.sky_weather_subpage, h_dir, 4);
    mark_dirty_full();
    rt_.schedule_disp();
    return true;
  }

  // 7. Page 6: Area Control (Picker List, Category Swipe, Entity Paging)
  if (rs.nav.gui_page == PageId::AREA) {
    auto step_area_chan = [&](latter_rpc::SlotChannel ch, void (*fetch_fn)()) {
      int &pg = rs.slot_page_for(ch);
      const int tot = std::max(1, rs.slot_total_pages_for(ch));
      bool changed = false;
      const int next_pg = step_v_pager(pg, 1, tot, false, PageId::AREA, changed);
      if (changed) { pg = next_pg; fetch_fn(); }
      rt_.schedule_disp();
      return true;
    };
    if (rs.nav.area_subpage == AreaSubpage::PICKER || rs.area.browse_mode == "areas") {
      return (v_dir != 0 && gest.start_y < ly.chin_guard_y())
                 ? step_area_chan(latter_rpc::SlotChannel::AREA_LIST, &touch_detail::do_fetch_area_list_browse)
                 : false;
    }
    if (rs.nav.area_subpage == AreaSubpage::OVERVIEW) {
      if (h_dir != 0) {
        commit_swipe();
        std::string cats[8];
        const int cat_cnt = collect_area_categories(cats);
        if (cat_cnt > 1) {
          int cur_c = 0;
          for (int i = 0; i < cat_cnt; ++i) {
            if (cats[i] == rs.area.domain_filter) { cur_c = i; break; }
          }
          const int next_c = step_wrap(cur_c, h_dir, cat_cnt);
          rs.area.domain_filter = cats[next_c];
          sync_mode_bar_window(area_cat_dock_page(), next_c, cat_cnt);
          rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
          touch_detail::do_fetch_area_browse();
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return true;
      }
      if (v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
        return step_area_chan(latter_rpc::SlotChannel::AREA_ENTITIES, &touch_detail::do_fetch_area_browse);
      }
    }
    return false;
  }

  // 8. Page 7: Remote Slot Selector Vertical List
  if (rs.nav.gui_page == PageId::REMOTE && rs.nav.remote_subpage == 1 &&
      v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
    int valid_slots[MAX_REMOTE_SLOTS] = {0};
    const int valid_count = collect_configured_remote_slots(valid_slots);
    bool changed = false;
    remote_control_page() = step_v_pager(
        remote_control_page(), 0, std::max(1, (valid_count + 3) / 4) - 1, false, PageId::REMOTE, changed);
    rt_.schedule_disp();
    return true;
  }

  // 9. Page 8: Tools Launcher Grid Vertical Paging
  if (rs.nav.gui_page == PageId::TOOLS) {
    if (rs.nav.tools_subpage == ToolsSubpage::LAUNCHER && !rs.nav.menu_scroll_active &&
        v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
      const int total_pages = std::max(1, active_tools_launcher_page_count(device_prefs()));
      rs.nav.tools_menu_page = std::clamp(rs.nav.tools_menu_page, 0, total_pages - 1);
      return step_anim_v_pager(rs.nav.tools_menu_page, 0, total_pages - 1, PageId::TOOLS);
    }
    return false;
  }

  // 10. Page 9: Home Hub (5-Tab Whole-Home Controls)
  if (rs.nav.gui_page == PageId::HOME_HUB) {
    if (home_hub_tab() == HomeHubTab::ENTRY && v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
      bool changed = false;
      home_hub_entry_page() = step_v_pager(
          home_hub_entry_page(), 0, std::max(1, (home_hub_state().entry_count + 1) / 2) - 1,
          false, PageId::HOME_HUB, changed);
      rt_.schedule_disp();
      return true;
    }
    if (home_hub_tab() == HomeHubTab::INTERCOM &&
        !broadcast_dictation_active(rs) && v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
      std::vector<std::string> phrases;
      const int total_cnt = parse_broadcast_phrases(area_ctx_state().broadcast_phrases, phrases);
      const int tot_pg = std::max(1, (total_cnt + 2) / 3);
      if (tot_pg > 1) {
        bool changed = false;
        home_hub_broadcast_page() = step_v_pager(
            home_hub_broadcast_page(), 0, tot_pg - 1, false, PageId::HOME_HUB, changed);
        rt_.schedule_disp();
        return true;
      }
      return false;
    }
    if (h_dir != 0) {
      commit_swipe();
      int hh_tabs[kMaxHomeHubTabs]{};
      const int hh_cnt = collect_home_hub_tabs(hh_tabs);
      if (hh_cnt > 1) {
        int cur_slot = 0;
        for (int i = 0; i < hh_cnt; ++i) {
          if (hh_tabs[i] == home_hub_tab()) { cur_slot = i; break; }
        }
        touch_detail::enter_home_hub_tab(hh_tabs[step_wrap(cur_slot, h_dir, hh_cnt)], false, rs, rt_, now_ms);
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return true;
    }
    return false;
  }

  // 10b. Page 13: Dedicated 4-Tab Cameras App
  if (rs.nav.gui_page == PageId::CAMERAS) {
    if (rs.cameras.quick_reply_open || rs.voice.intercom_turn) return false;
    CameraRecord cams[MAX_CAMERA_SLOTS]{};
    const int c_total = parse_cameras_records(area_ctx_state().cameras_summary, cams, MAX_CAMERA_SLOTS);
    if (rs.cameras.tab == CamerasTab::GRID && v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
      const int total_pg = std::max(1, (c_total + 3) / 4);
      if (total_pg > 1) {
        bool changed = false;
        rs.cameras.grid_page = step_v_pager(rs.cameras.grid_page, 0, total_pg - 1, false, PageId::CAMERAS, changed);
        if (changed) {
          rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
          mark_dirty_full();
        }
        rt_.schedule_disp();
        return true;
      }
      return false;
    }
    if (rs.cameras.tab == CamerasTab::SETTINGS && v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
#if LATTER_CHIP_P4
      bool changed = false;
      rs.cameras.settings_page = step_v_pager(rs.cameras.settings_page, 0, 1, false, PageId::CAMERAS, changed);
      if (changed) mark_dirty_full();
      rt_.schedule_disp();
      return true;
#else
      return false;
#endif
    }
    if (h_dir != 0) {
      commit_swipe();
      if (rs.cameras.tab == CamerasTab::SINGLE && !rs.cameras.hud_active && c_total > 1) {
        rs.cameras.selected_idx = step_wrap(rs.cameras.selected_idx, h_dir, c_total);
        rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
        if (persisted_prefs().camera_stream_mode == 2U || area_ctx_state().camera_stream_url.empty()) {
          rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
          rs.alert.loaded_camera_url.clear();
          rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
        }
        mark_dirty_full();
        rt_.schedule_disp();
        return true;
      }
      if (rs.cameras.tab == CamerasTab::CAROUSEL && !rs.cameras.hud_active && c_total > 1) {
        int car[MAX_CAMERA_SLOTS]{};
        const int car_cnt = collect_carousel_camera_indices(cams, c_total, car, MAX_CAMERA_SLOTS);
        if (car_cnt > 1) {
          rs.cameras.carousel_idx = step_wrap(rs.cameras.carousel_idx, h_dir, car_cnt);
          rs.cameras.carousel_last_advance_ms = now_ms;
          rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
          mark_dirty_full();
          rt_.schedule_disp();
          return true;
        }
      }
      int next_tab = step_wrap(rs.cameras.tab, h_dir, 4);
      if (next_tab == CamerasTab::GRID && c_total <= 1) next_tab = step_wrap(next_tab, h_dir, 4);
      rs.cameras.tab = next_tab;
      rs.cameras.hud_active = true;
      rs.cameras.hud_timer = now_ms;
      rs.cameras.carousel_last_advance_ms = now_ms;
      rt_.cmd((next_tab == CamerasTab::SETTINGS) ? UiHostCmd::STOP_CAMERA_STREAM : UiHostCmd::SYNC_CAMERA_STREAM);
      mark_dirty_full();
      rt_.schedule_disp();
      return true;
    }
    return false;
  }

  // 11. Page 10: Time App (4-Tab Swipe & World Clock Vertical Paging)
  if (rs.nav.gui_page == PageId::TIME) {
    const int sub = std::clamp(rs.nav.time_subpage, 0, 3);
    const bool in_drilldown =
        (sub == TimeSubpage::TIMERS && timer_view_slot() != 0) ||
        (sub == TimeSubpage::ALARMS && rs.alarms.edit_idx != 0);
    if (!in_drilldown && h_dir != 0) {
      commit_swipe();
      rs.nav.time_subpage = step_wrap(sub, h_dir, 4);
      mark_dirty_full();
      rt_.schedule_disp();
      return true;
    }
    if (!in_drilldown && sub == TimeSubpage::CLOCK && v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
      WorldClockEntry wc_entries[kMaxWorldClocks + 1]{};
      const int tot_wc = collect_active_world_clocks(prefs, area_ctx_state(), wc_entries);
      bool changed = false;
      rs.nav.world_clock_page = step_v_pager(
          rs.nav.world_clock_page, 0, std::max(1, (tot_wc + 1) / 2) - 1, false, PageId::TIME, changed);
      rt_.schedule_disp();
      return true;
    }
    return false;
  }

  // 12. Page 11: Pinned App (6-Button-Per-Page Vertical Paging)
  if (rs.nav.gui_page == PageId::PINNED) {
    if (!rs.nav.menu_scroll_active && v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
      const int total_pages = pinned_total_pages();
      rs.nav.pinned_menu_page = std::clamp(rs.nav.pinned_menu_page, 0, total_pages - 1);
      return step_anim_v_pager(rs.nav.pinned_menu_page, 0, total_pages - 1, PageId::PINNED);
    }
    return false;
  }

  // 13. Page 12: Calendar App (4-Tab Swipe & Vertical Event/Picker Paging)
  if (rs.nav.gui_page == PageId::CALENDAR) {
    if (rs.cal.delete_confirm_open) return false;
    if (rs.cal.drilldown >= CalDrilldown::PICK_TITLE &&
        rs.cal.drilldown <= CalDrilldown::PICK_REPEAT &&
        v_dir != 0 && gest.start_y < ly.chin_guard_y()) {
      int opt_cnt = 10;
      if (rs.cal.drilldown == CalDrilldown::PICK_LOCATION) {
        std::string_view locs[kMaxCalendarLocations]{};
        opt_cnt = 1 + parse_calendar_locations_catalog(calendar_state().locations_catalog, locs);
      } else if (rs.cal.drilldown == CalDrilldown::PICK_CALENDAR) {
        opt_cnt = std::max(1, collect_writable_calendar_sources(calendar_state().calendars_meta, nullptr, nullptr));
      } else if (rs.cal.drilldown == CalDrilldown::PICK_REPEAT) {
        opt_cnt = 7;
      }
      bool changed = false;
      rs.cal.picker_page = step_v_pager(
          rs.cal.picker_page, 0, std::max(0, (opt_cnt - 1) / 5), false, PageId::CALENDAR, changed);
      rt_.schedule_disp();
      return true;
    }
    if (rs.cal.drilldown != CalDrilldown::NONE) return false;
    if (h_dir != 0) {
      commit_swipe();
      rs.cal.subpage = step_wrap(std::clamp(rs.cal.subpage, 0, 3), h_dir, 4);
      mark_dirty_full();
      rt_.schedule_disp();
      return true;
    }
    if (v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
      const int per_page = (SCREEN_H > 520) ? 5 : (LATTER_ROUND_DISPLAY ? 3 : 4);
      const auto &store = calendar_window_store();
      const int32_t today_ymd = touch_detail::resolve_touch_today_ymd(rt_);
      const int now_min = (rt_.time_valid && rt_.time_hour >= 0) ? (rt_.time_hour * 60 + rt_.time_minute) : -1;
      int idxs[kMaxCalendarWindowEvents]{};
      auto step_cal_pg = [&](int &pg_ref, int cnt) {
        bool changed = false;
        pg_ref = step_v_pager(pg_ref, 0, std::max(0, (std::max(1, cnt) - 1) / per_page), false, PageId::CALENDAR, changed);
        rt_.schedule_disp();
        return true;
      };
      if (rs.cal.subpage == CalendarSubpage::OVERVIEW) {
        return step_cal_pg(rs.cal.overview_page, collect_calendar_overview_events(
            today_ymd, now_min, snap_calendar_lookahead_days(prefs.calendar_lookahead_days),
            idxs, kMaxCalendarWindowEvents, store));
      }
      if (rs.cal.subpage == CalendarSubpage::DAY) {
        return step_cal_pg(rs.cal.day_page, collect_calendar_day_events(
            rs.cal.selected_ymd, idxs, kMaxCalendarWindowEvents, store));
      }
      if constexpr (SCREEN_W <= 520) {
        if (rs.cal.subpage == CalendarSubpage::WEEK) {
          const int32_t ws = start_of_week_ymd(rs.cal.selected_ymd, prefs.calendar_week_start == 1U);
          const int32_t we = add_days_ymd(ws, 6);
          int wcnt = 0;
          for (int i = 0; i < store.count && wcnt < kMaxCalendarWindowEvents; ++i) {
            if (store.events[i].end_ymd >= ws && store.events[i].start_ymd <= we) wcnt++;
          }
          return step_cal_pg(rs.cal.week_page, wcnt);
        }
      }
    }
  }
  return false;
}

void TouchGestureEngine::on_touch_move(int tx, int ty, uint32_t now_u) {
  auto &gest = touch_gesture();
  if (!gest.active) return;

  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  last_user_touch_ms() = now_u;

  if (rs.nav.display_power_state == 0 && !rs.voice.active &&
      (now_u - last_drag_sleep_rst_ms_ >= 2000U)) {
    last_drag_sleep_rst_ms_ = now_u;
    display_lifecycle().note_activity(now_u);
  }

  gest.record_move(tx, ty, now_u, LATTER_PANEL_PPI);

  if (rs.nav.gui_page == PageId::GAMES && rs.nav.active_game > 0 && rs.nav.active_game != 5) {
    if (ty < layout().back_y - scale_y(30)) {
      rs.nav.marble_touch_x = static_cast<float>(tx);
      rs.nav.marble_touch_y = static_cast<float>(ty);
    }
    return;
  }

  if (compute_touch_context(rs) != gest.start_context) return;

  if (rs.nav.gui_page == PageId::SYSTEM && rs.nav.system_menu_mode == 13 &&
      (now_u - last_disp_update_ms() >= 42U)) {
    float hue = 0.0f, sat = 1.0f;
    if (ui::hsv_wheel_pick(tx, ty, SCREEN_CX, scale_y(180), 156, 56, hue, sat)) {
      gest.axis_lock = GestureAxisLock::Slider;
      gest.tap_cancelled = true;
      color_wheel_hue_deg() = hue;
      color_wheel_sat() = sat;
      const UiColor c = hsv_to_rgb(hue / 360.0f, sat, 1.0f);
      prefs.custom_theme_rgb = (static_cast<uint32_t>(c.r) << 16) |
                               (static_cast<uint32_t>(c.g) << 8) |
                               static_cast<uint32_t>(c.b);
      if (rt_.theme_primary != nullptr && rt_.theme_secondary != nullptr) {
        apply_custom_rgb_harmony_slots(prefs.custom_theme_rgb, rt_.theme_primary, rt_.theme_secondary);
      }
      rt_.schedule_disp();
    }
    return;
  }

  if (gest.swipe_committed) return;

  const auto &ly = layout();
  const int dx = tx - gest.start_x;
  const int dy = ty - gest.start_y;

  auto drag_local_spk_vol = [&](const HitRegion *sl) {
    const int next_pct = std::clamp(10 + static_cast<int>(std::lround(sl->norm_x(tx) * 90.0f)), 10, 100);
    const int cur_pct = static_cast<int>(std::lround(rt_.num_val(UiNumberId::SPEAKER_VOLUME)));
    if (next_pct != cur_pct && (now_u - last_disp_update_ms() >= 42U)) {
      rt_.set_num_val(UiNumberId::SPEAKER_VOLUME, static_cast<float>(next_pct));
      rt_.schedule_disp();
    }
  };

  auto drag_bri_num = [&](UiNumberId nid, const HitRegion *sl) {
    const int next_pct = std::clamp(1 + static_cast<int>(std::lround(sl->norm_x(tx) * 99.0f)), 1, 100);
    const int cur_pct = static_cast<int>(std::lround(rt_.num_val(nid)));
    if (next_pct != cur_pct && (now_u - last_disp_update_ms() >= 42U)) {
      rt_.set_num_val(nid, static_cast<float>(next_pct));
      rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
      rt_.schedule_disp();
    }
  };

  if (rs.nav.quick_actions_open) {
    const HitRegion *qa_sl = frame_hits().hit_test(gest.start_x, gest.start_y, kHitHorizSlider);
    if (qa_sl != nullptr &&
        (gest.axis_lock == GestureAxisLock::Slider ||
         (std::abs(dx) >= ly.drag_start() && std::abs(dx) >= std::abs(dy)))) {
      gest.axis_lock = GestureAxisLock::Slider;
      gest.tap_cancelled = true;
      if (qa_sl->widget_id == WidgetId::QUICK_VOL_SLIDER) {
        drag_local_spk_vol(qa_sl);
        return;
      }
      if (qa_sl->widget_id == WidgetId::QUICK_BRI_SLIDER) {
        const auto bri = resolve_active_brightness_target(
            prefs, weather_state().sun_state, rt_.time_valid, rt_.time_valid ? rt_.time_hour : 12,
            area_ctx_state().area_ambient_pct, rs.nav.ambient_bri_override_pct);
        if (bri.kind == ActiveBrightnessTargetKind::AMBIENT ||
            bri.kind == ActiveBrightnessTargetKind::AMBIENT_OVERRIDE) {
          const int next_pct = std::clamp(1 + static_cast<int>(std::lround(qa_sl->norm_x(tx) * 99.0f)), 1, 100);
          if (next_pct != static_cast<int>(bri.pct) && (now_u - last_disp_update_ms() >= 42U)) {
            rs.nav.ambient_bri_override_pct = static_cast<int8_t>(next_pct);
            rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
            rt_.schedule_disp();
          }
          return;
        }
        drag_bri_num(touch_detail::brightness_number_id_for_kind(bri.kind), qa_sl);
        return;
      }
    }
    (void)try_commit_kinematic_swipe_(false, now_u);
    return;
  }

  const HitRegion *vsl_hit = frame_hits().hit_test(gest.start_x, gest.start_y, kHitVertSlider);
  if (vsl_hit != nullptr && vsl_hit->widget_id == WidgetId::MUSIC_VOL_SLIDER &&
      rs.nav.gui_page == PageId::MUSIC &&
      rs.music.view == MusicView::VOLUME &&
      rs.music.drilldown == MusicDrilldown::NONE &&
      rs.music.takeover == MusicTakeover::NONE &&
      (gest.axis_lock == GestureAxisLock::Slider ||
       (std::abs(dy) >= ly.drag_start() && std::abs(dy) >= std::abs(dx)))) {
    gest.axis_lock = GestureAxisLock::Slider;
    gest.tap_cancelled = true;
    music_vol_dragging_ = true;
    rs.music.hud_timer = now_u;
    rs.music.takeover_interacted_ms = (now_u == 0U) ? 1U : now_u;
    auto &ms = media_state();
    const int min_v = ms.is_local ? 10 : 0;
    const int next_pct = std::clamp(
        static_cast<int>(std::lround((1.0f - vsl_hit->norm_y(ty)) * 100.0f)), min_v, 100);
    if (ms.muted && next_pct > 0) ms.muted = false;
    last_local_vol_tap_ms() = now_u;
    if (ms.is_local) {
      rt_.set_num_val(UiNumberId::SPEAKER_VOLUME, static_cast<float>(next_pct));
    } else {
      remote_vol_pct() = next_pct;
      if (now_u - last_music_vol_drag_ms_ >= 120U) {
        last_music_vol_drag_ms_ = now_u;
        latter_actions::emit_media_volume(touch_detail::resolve_music_speaker_entity(rs), "set", next_pct);
      }
      if (now_u - last_disp_update_ms() >= 42U) rt_.schedule_disp();
    }
    return;
  }

  if (rs.nav.gui_page == PageId::MUSIC &&
      rs.music.drilldown == MusicDrilldown::NONE &&
      rs.music.takeover == MusicTakeover::NONE &&
      (rs.music.view == MusicView::LYRICS || rs.music.view == MusicView::INFO) &&
      (!rs.music.hud_active || gest.start_y < ly.mode_bar_zone_top()) &&
      !(gest.axis_lock != GestureAxisLock::Slider && gest.start_y <= ly.top_edge_zone_y() && dy > 0) &&
      (gest.axis_lock == GestureAxisLock::Slider ||
       (std::abs(dy) >= ly.drag_start() && std::abs(dy) > std::abs(dx)))) {
    gest.axis_lock = GestureAxisLock::Slider;
    gest.tap_cancelled = true;
    const int step_px = music_scroll_last_y_ - ty;
    music_scroll_last_y_ = ty;
    music_scroll_dragging_ = true;
    if (rs.music.hud_active) rs.music.hud_timer = now_u;
    if (step_px != 0) {
      if (rs.music.view == MusicView::LYRICS) {
        auto &lyr = rs.music.lyrics;
        lyr.scroll_px = std::max(0, lyr.scroll_px + step_px);
        lyr.target_scroll_px = lyr.scroll_px;
        lyr.scroll_anim_start_ms = 0U;
        lyr.manual_override_until_ms = now_u + 6000U;
      } else {
        rs.music.info_scroll_px = std::max(0, rs.music.info_scroll_px + step_px);
      }
      if (now_u - last_disp_update_ms() >= 36U) rt_.schedule_disp();
    }
    return;
  }

  const HitRegion *sl_hit = frame_hits().hit_test(gest.start_x, gest.start_y, kHitHorizSlider);
  if (sl_hit != nullptr &&
      (gest.axis_lock == GestureAxisLock::Slider ||
       (std::abs(dx) >= ly.drag_start() && std::abs(dx) >= std::abs(dy)))) {
    gest.axis_lock = GestureAxisLock::Slider;
    gest.tap_cancelled = true;
    if (sl_hit->widget_id == WidgetId::MUSIC_SCRUB_BAR &&
        rs.nav.gui_page == PageId::MUSIC && rs.music.takeover == MusicTakeover::SCRUB) {
      music_scrub_dragging_ = true;
      rs.music.takeover_interacted_ms = (now_u == 0U) ? 1U : now_u;
      const float dur = media_state().duration;
      if (!std::isnan(dur) && dur > 0.0f && (now_u - last_disp_update_ms() >= 42U)) {
        rs.music.pos_base = sl_hit->norm_x(tx) * dur;
        rs.music.pos_sync_time = now_u;
        rt_.schedule_disp();
      }
      return;
    }
    if (sl_hit->widget_id == WidgetId::LIGHT_TEMP_SLIDER &&
        rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
      const int raw_k = kLightKelvinMin +
          static_cast<int>(sl_hit->norm_x(tx) * static_cast<float>(kLightKelvinMax - kLightKelvinMin));
      const int next_k = std::clamp(((raw_k + 50) / 100) * 100, kLightKelvinMin, kLightKelvinMax);
      if (next_k != light_insp_kelvin() && (now_u - last_disp_update_ms() >= 42U)) {
        light_insp_kelvin() = next_k;
        rt_.schedule_disp();
      }
      return;
    }
    if (sl_hit->widget_id == WidgetId::LIGHT_BRI_SLIDER &&
        rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
      const int raw_b = static_cast<int>(sl_hit->norm_x(tx) * 100.0f);
      const int min_b = (light_insp_domain() == "cover") ? 0 : 5;
      const int next_b = std::clamp(((raw_b + 2) / 5) * 5, min_b, 100);
      if (next_b != light_insp_bri_pct() && (now_u - last_disp_update_ms() >= 42U)) {
        light_insp_bri_pct() = next_b;
        if (light_insp_domain() == "cover" || light_insp_domain() == "fan") {
          light_insp_active() = (next_b > 0);
        }
        rt_.schedule_disp();
      }
      return;
    }
    if (sl_hit->widget_id == WidgetId::SYS_BRI_SLIDER &&
        rs.nav.gui_page == PageId::SYSTEM && rs.nav.system_menu_mode == 2) {
      UiNumberId nid = UiNumberId::MANUAL_BRIGHTNESS;
      if (touch_detail::resolve_sys_bri_slider_id(
              sl_hit->param, rt_.select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE), nid)) {
        drag_bri_num(nid, sl_hit);
      }
      return;
    }
    if (sl_hit->widget_id == WidgetId::SYS_VOL_SLIDER &&
        rs.nav.gui_page == PageId::SYSTEM && rs.nav.system_menu_mode == 7) {
      drag_local_spk_vol(sl_hit);
      return;
    }
  }

  if (rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL &&
      light_insp_domain() == "light" && light_insp_mode() == 0) {
    float hue = 0.0f, sat = 1.0f;
    if (ui::hsv_wheel_pick(tx, ty, SCREEN_CX, scale_y(244), 132, 58, hue, sat)) {
      wheel_drag_dirty_ = true;
      gest.axis_lock = GestureAxisLock::Slider;
      gest.tap_cancelled = true;
      light_insp_hue_deg() = hue;
      light_insp_sat() = sat;
      touch_detail::update_light_inspector_hex(hue, sat);
      if (now_u - last_disp_update_ms() >= 42U) rt_.schedule_disp();
      return;
    }
  }

  if (!gest.long_press_armed) cancel_core_timeout(kTimeoutTouchLongPressId);
  if (try_commit_long_press_(now_u)) return;
  (void)try_commit_kinematic_swipe_(false, now_u);
}

bool TouchGestureEngine::try_commit_long_press_(uint32_t now_ms) {
  auto &gest = touch_gesture();
  if (!gest.should_fire_long_press(now_ms)) return false;

  auto &rs = runtime_state();
  const HitRegion *reg = frame_hits().find_by_id_param(gest.long_press_wid, gest.long_press_param);
  if (compute_touch_context(rs) != gest.start_context ||
      (reg != nullptr && !reg->contains_padded(gest.last_x, gest.last_y))) {
    gest.long_press_armed = false;
    cancel_core_timeout(kTimeoutTouchLongPressId);
    return false;
  }

  gest.mark_long_press_fired();
  cancel_core_timeout(kTimeoutTouchLongPressId);

  if (rs.nav.gui_page == PageId::MUSIC &&
      ((gest.long_press_wid >= WidgetId::LIST_SLOT_BASE &&
        gest.long_press_wid < WidgetId::LIST_SLOT_BASE + 5) ||
       (gest.long_press_wid >= WidgetId::MUSIC_ROW_PLAY_BASE &&
        gest.long_press_wid < WidgetId::MUSIC_ROW_PLAY_BASE + 5))) {
    if (touch_detail::open_music_row_action_sheet(gest.long_press_param, rs, now_ms)) {
      mark_dirty_full();
      rt_.schedule_disp();
      return true;
    }
  }

  if (rs.nav.gui_page == PageId::REMOTE && rs.nav.remote_subpage == 0) {
    const int remote_key = touch_detail::remote_key_for_widget(gest.long_press_wid);
    if (remote_key >= 0) {
      const int act_slot = resolve_active_remote_slot(rs.remote.active_slot);
      rs.remote.active_slot = act_slot;
      const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, gest.long_press_wid, gest.long_press_param);
      if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        touch_detail::do_remote_send_key(remote_key, act_slot, true, now_ms);
        rt_.schedule_disp();
        return true;
      }
    }
  }
  rt_.schedule_disp();
  return true;
}

}  // namespace latter_ui
