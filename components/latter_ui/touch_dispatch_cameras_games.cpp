// ==============================================================================
// Latter Dedicated 4-Tab Cameras App, Doorbell Popup & Games Touch Dispatch
// (components/latter_ui/touch_dispatch_cameras_games.cpp)
// ==============================================================================

#include "touch_internal.h"

namespace latter_ui {

namespace touch_detail {

void leave_home_hub_cameras_if_active(DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                                      uint32_t now_ms) {
  if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
    intercom_session().stop_call_with_hw(now_ms);
    if (cam_listen_active()) {
      cam_listen_active() = false;
      rs.intercom.cam_action = "listen_stop";
      intercom_session().emit_current_cmd();
      rt.cmd(UiHostCmd::STOP_DOORBELL_AUDIO);
    }
  }
  rs.cameras.quick_reply_open = false;
  rs.cameras.doorbell_live_popup_active = false;
  cam_expanded_fullscreen() = false;
  rt.cmd(UiHostCmd::STOP_CAMERA_STREAM);
  rt.cmd(UiHostCmd::RELEASE_CAMERA_ART);
  rs.alert.loaded_camera_url.clear();
  rt.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
}

void enter_cameras_app(int target_tab, std::string_view focus_entity, bool start_listen,
                       DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms) {
  const auto &prefs = persisted_prefs();
  const auto &ac = area_ctx_state();
  CameraRecord cams[MAX_CAMERA_SLOTS]{};
  const int cam_cnt = parse_cameras_records(ac.cameras_summary, cams, MAX_CAMERA_SLOTS);

  rs.nav.gui_page = PageId::CAMERAS;
  rs.nav.home_subpage = 0;
  rs.nav.tools_subpage = 0;
  rs.nav.remote_subpage = 0;
  rs.nav.active_game = 0;
  rs.cameras.quick_reply_open = false;
  rs.cameras.doorbell_live_popup_active = false;
  rs.cameras.hud_active = true;
  rs.cameras.hud_timer = now_ms;
  rs.cameras.carousel_last_advance_ms = now_ms;

  if (!focus_entity.empty()) {
    const int idx = find_camera_index_by_entity(cams, cam_cnt, focus_entity);
    if (idx >= 0) {
      rs.cameras.selected_idx = idx;
    }
  } else if (cam_cnt > 0) {
    rs.cameras.selected_idx = std::clamp(rs.cameras.selected_idx, 0, cam_cnt - 1);
  } else {
    rs.cameras.selected_idx = 0;
  }

  int chosen_tab = target_tab;
  if (chosen_tab < 0) {
    if (prefs.camera_default_view == 1U) chosen_tab = CamerasTab::SINGLE;
    else if (prefs.camera_default_view == 2U) chosen_tab = CamerasTab::CAROUSEL;
    else chosen_tab = CamerasTab::GRID;
  }
  if (chosen_tab == CamerasTab::GRID && cam_cnt <= 1) {
    chosen_tab = CamerasTab::SINGLE;
  }
  rs.cameras.tab = std::clamp(chosen_tab, CamerasTab::GRID, CamerasTab::SETTINGS);
  mode_bar_anim().snap(0, rs.cameras.tab);

  if (start_listen) {
    intercom_session().start_call(now_ms);
  }

  cam_error_backoff_until_ms() = 0;
  last_cam_live_fetch_ms() = now_ms;
  if (rs.cameras.tab == CamerasTab::SETTINGS) {
    rt.cmd(UiHostCmd::STOP_CAMERA_STREAM);
  } else {
    rt.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
    if (prefs.camera_stream_mode == 2U || ac.camera_stream_url.empty()) {
      rt.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
    }
  }
}

}  // namespace touch_detail

void TouchEngineAccess::handle_down_alert_overlay(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms) {
  auto &rs = runtime_state();
  auto &rt = eng.rt_;
  touch_gesture().active = false;
  last_screen_wake_ms() = now_ms;
  const HitRegion *n_hit = frame_hits().hit_test(tx, ty, kHitTap);
  const uint16_t nwid = (n_hit != nullptr) ? n_hit->widget_id : WidgetId::NONE;
  const bool is_cam_notif = !rs.alert.camera_url.empty() || !rs.alert.camera_entity.empty();

  // Live PTT banner tap during P4 live doorbell popup sends reply immediately without dismissing popup
  if (rs.voice.intercom_turn && nwid == WidgetId::CAM_PTT_BANNER_STOP) {
    rt.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
    intercom_session().resume_listen(now_ms);
    rt.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
    rt.apply_power_policy();
    mark_dirty_full();
    rt.schedule_disp();
    return;
  }

  if (is_cam_notif && rs.cameras.doorbell_live_popup_active) {
    if (nwid == WidgetId::NOTIF_ANSWER_BTN) {
      if (cam_call_active()) {
        intercom_session().stop_call_with_hw(now_ms);
      } else {
        cam_listen_active() = !cam_listen_active();
        rs.intercom.cam_action = cam_listen_active() ? "listen_start" : "listen_stop";
        cam_intercom_status() = cam_listen_active() ? "DOOR AUDIO ON" : "AUDIO MUTED";
        rs.intercom.cam_status_ms = now_ms;
        intercom_session().emit_current_cmd();
        if (!cam_listen_active()) {
          rt.cmd(UiHostCmd::STOP_DOORBELL_AUDIO);
        }
      }
      rt.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }
    if (nwid == WidgetId::NOTIF_TALK_BTN) {
      if (!cam_call_active()) {
        intercom_session().start_call(now_ms);
      }
      rt.cmd(UiHostCmd::START_DOORBELL_PTT);
      rt.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }
    if (nwid == WidgetId::NOTIF_REPLY_BTN) {
      const std::string focus_ent = rs.alert.camera_entity;
      rs.alert.active = false;
      rs.cameras.doorbell_live_popup_active = false;
      rs.nav.shortcut_origin = ShortcutOrigin::NONE;
      touch_detail::enter_cameras_app(CamerasTab::SINGLE, focus_ent, false, rs, rt, now_ms);
      rs.cameras.quick_reply_open = true;
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }
    if (nwid == WidgetId::NOTIF_UNLOCK_BTN) {
      eng.gate_or_dispatch_doorbell_unlock(now_ms);
      return;
    }
    if (nwid == WidgetId::NOTIF_EXPAND_BTN || nwid == WidgetId::NOTIF_VIEW_BTN ||
        nwid == WidgetId::NOTIF_OPEN_CAM_BTN) {
      const std::string focus_ent = rs.alert.camera_entity;
      rs.alert.active = false;
      rs.cameras.doorbell_live_popup_active = false;
      rs.nav.shortcut_origin = ShortcutOrigin::NONE;
      touch_detail::enter_cameras_app(CamerasTab::SINGLE, focus_ent, false, rs, rt, now_ms);
      mark_dirty_full();
      rt.schedule_disp();
      return;
    }
    if (nwid == WidgetId::NOTIF_DISMISS_BTN) {
      rs.cameras.doorbell_live_popup_active = false;
      if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
        intercom_session().stop_call_with_hw(now_ms);
      }
      rt.cmd(UiHostCmd::STOP_CAMERA_STREAM);
      rt.cmd(UiHostCmd::DISMISS_NOTIFICATION);
    }
    return;
  }

  if (is_cam_notif && (nwid == WidgetId::NOTIF_ANSWER_BTN ||
                       nwid == WidgetId::NOTIF_VIEW_BTN ||
                       nwid == WidgetId::NOTIF_OPEN_CAM_BTN)) {
    const std::string focus_ent = rs.alert.camera_entity;
    const bool want_listen = (nwid == WidgetId::NOTIF_ANSWER_BTN || nwid == WidgetId::NOTIF_OPEN_CAM_BTN);
    rs.alert.active = false;
    rs.cameras.doorbell_live_popup_active = false;
    rs.nav.shortcut_origin = ShortcutOrigin::NONE;
    touch_detail::enter_cameras_app(CamerasTab::SINGLE, focus_ent, want_listen, rs, rt, now_ms);
    mark_dirty_full();
    rt.schedule_disp();
    return;
  }
  rs.cameras.doorbell_live_popup_active = false;
  rt.cmd(UiHostCmd::STOP_CAMERA_STREAM);
  rt.cmd(UiHostCmd::DISMISS_NOTIFICATION);
}

bool TouchEngineAccess::handle_down_cameras_games(TouchGestureEngine &eng, int tx, int ty,
                                                  uint32_t now_ms, uint16_t wid) {
  auto &rs = runtime_state();
  auto &rt = eng.rt_;
  auto &gest = touch_gesture();

  if (rs.nav.gui_page == PageId::GAMES) {
    if (rs.nav.active_game > 0) {
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
          rs.nav.active_game = 0;
          rs.nav.game_btn_pressed = false;
          rt.cmd(UiHostCmd::STOP_ALARM_SOUND);
          if (touch_detail::return_from_shortcut_origin(rs, rt, now_ms)) {
            return true;
          }
          mark_dirty_full();
          rt.schedule_disp();
        }
        return true;
      }
      if (rs.nav.active_game != 5) {
        rs.nav.marble_touch_x = static_cast<float>(tx);
        rs.nav.marble_touch_y = static_cast<float>(ty);
        rs.nav.game_btn_pressed = true;
        rt.schedule_disp();
      }
    }
    return true;
  }

  if (rs.nav.gui_page == PageId::CAMERAS) {
    if (rs.voice.intercom_turn && wid == WidgetId::CAM_PTT_BANNER_STOP &&
        touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      gest.active = false;
      rt.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
      intercom_session().resume_listen(now_ms);
      rt.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      rt.apply_power_policy();
      mark_dirty_full();
      rt.schedule_disp();
      return true;
    }
    if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      gest.active = false;
      if (rs.cameras.quick_reply_open) {
        rs.cameras.quick_reply_open = false;
        rs.cameras.hud_active = true;
        rs.cameras.hud_timer = now_ms;
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      (void)touch_detail::return_from_shortcut_origin(rs, rt, now_ms);
    }
    return true;
  }

  return false;
}

bool TouchGestureEngine::handle_tap_cameras_games_(
    int tx, int ty, uint32_t now_rel, const HitRegion *hit, uint16_t wid) {
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  if (rs.nav.gui_page == PageId::GAMES) {
    if (rs.nav.active_game == 5) {
      rs.nav.marble_touch_x = static_cast<float>(tx);
      rs.nav.marble_touch_y = static_cast<float>(ty);
      rs.nav.game_btn_pressed = true;
      rt_.schedule_disp();
    } else if (rs.nav.active_game == 0 && !rs.nav.menu_scroll_active) {
      if (wid == WidgetId::GAMES_SOUND_BTN &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rt_.sw_toggle(UiSwitchId::GAME_SOUND);
        rt_.schedule_disp();
      } else if ((wid == WidgetId::GAMES_PLAY_BTN || wid == WidgetId::GAMES_HERO_CARD) &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        int enabled_ids[kMaxGamesLauncherItems]{};
        const int game_cnt = collect_enabled_games(enabled_ids, device_prefs());
        if (game_cnt > 0) {
          const int slot = std::clamp(rs.nav.game_carousel_idx, 0, game_cnt - 1);
          rs.nav.game_carousel_idx = slot;
          const int resolved_game =
              (hit != nullptr && hit->param >= 1 && hit->param <= 6)
                  ? static_cast<int>(hit->param)
                  : enabled_ids[slot];
          rs.nav.active_game = resolved_game;
          audio_arbitrator().cancel_media_idle_debounce();
          if (rt_.local_mp_playing) {
            rt_.cmd(UiHostCmd::PAUSE_LOCAL_MEDIA_PLAYER);
            audio_arbitrator().cancel_media_idle_debounce();
          }
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if ((wid == WidgetId::GAMES_PREV_BTN || wid == WidgetId::GAMES_NEXT_BTN) &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        const int game_cnt = active_games_launcher_count(device_prefs());
        if (game_cnt > 1) {
          const int cur_slot = std::clamp(rs.nav.game_carousel_idx, 0, game_cnt - 1);
          rs.nav.game_carousel_idx = cur_slot;
          const int dir = (wid == WidgetId::GAMES_NEXT_BTN) ? 1 : -1;
          rs.nav.menu_scroll_active = true;
          rs.nav.menu_scroll_dir = dir;
          rs.nav.menu_scroll_target_page = step_wrap(cur_slot, dir, game_cnt);
          rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
        }
      }
    }
    return true;
  }

  if (rs.nav.gui_page != PageId::CAMERAS) return false;

  const auto &ac = area_ctx_state();
  CameraRecord cams[MAX_CAMERA_SLOTS]{};
  const int c_total = parse_cameras_records(ac.cameras_summary, cams, MAX_CAMERA_SLOTS);
  const bool snap_only = (prefs.camera_stream_mode == 2U) || ac.camera_stream_url.empty();

  auto trigger_cam_refresh = [&]() {
    if (rs.cameras.tab == CamerasTab::SETTINGS) {
      rt_.cmd(UiHostCmd::STOP_CAMERA_STREAM);
    } else {
      rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      if (snap_only) {
        rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
        rs.alert.loaded_camera_url.clear();
        cam_error_backoff_until_ms() = 0;
        last_cam_live_fetch_ms() = now_rel;
        rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
      }
    }
  };

  if (rs.cameras.quick_reply_open) {
    if (wid >= WidgetId::CAM_REPLY_BASE && wid < WidgetId::CAM_REPLY_BASE + 3 &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      static constexpr const char *kDoorbellReplies[3] = {
          "On my way!",
          "Please leave it at the door.",
          "One moment, please!",
      };
      const int r_idx = static_cast<int>(wid - WidgetId::CAM_REPLY_BASE);
      rs.cameras.quick_reply_open = false;
      rs.cameras.hud_active = true;
      rs.cameras.hud_timer = now_rel;
      rs.intercom.cam_action = "quick_reply";
      rs.intercom.cam_message = kDoorbellReplies[r_idx];
      cam_intercom_status() = "REPLY SENT";
      rs.intercom.cam_status_ms = now_rel;
      intercom_session().emit_current_cmd();
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CAM_UNLOCK_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.quick_reply_open = false;
      rs.cameras.hud_active = true;
      rs.cameras.hud_timer = now_rel;
      mark_dirty_full();
      gate_or_dispatch_doorbell_unlock(now_rel);
    }
  } else if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4 &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    int target_tab = std::clamp(static_cast<int>(hit->param), CamerasTab::GRID, CamerasTab::SETTINGS);
    if (target_tab == CamerasTab::GRID && c_total <= 1) {
      target_tab = CamerasTab::SINGLE;
    }
    if (target_tab != rs.cameras.tab) {
      rs.cameras.tab = target_tab;
      rs.cameras.hud_active = true;
      rs.cameras.hud_timer = now_rel;
      rs.cameras.carousel_last_advance_ms = now_rel;
      trigger_cam_refresh();
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (rs.cameras.tab == CamerasTab::GRID && c_total > 1) {
    if (wid >= WidgetId::CAM_GRID_TILE_BASE && wid < WidgetId::CAM_GRID_TILE_BASE + 4 &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.selected_idx = std::clamp(static_cast<int>(hit->param), 0, c_total - 1);
      rs.cameras.tab = CamerasTab::SINGLE;
      rs.cameras.hud_active = true;
      rs.cameras.hud_timer = now_rel;
      trigger_cam_refresh();
      mark_dirty_full();
      rt_.schedule_disp();
    } else if ((wid == WidgetId::CAM_GRID_PREV_BTN || wid == WidgetId::CAM_GRID_NEXT_BTN) &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      const int total_pg = std::max(1, (c_total + 3) / 4);
      if (total_pg > 1) {
        rs.cameras.grid_page = step_wrap(
            rs.cameras.grid_page, (wid == WidgetId::CAM_GRID_NEXT_BTN) ? 1 : -1, total_pg);
        trigger_cam_refresh();
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
  } else if (rs.cameras.tab == CamerasTab::SETTINGS) {
#if LATTER_CHIP_P4
    if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      rs.cameras.settings_page = (wid == WidgetId::CHEVRON_UP) ? 0 : 1;
      mark_dirty_full();
      rt_.schedule_disp();
      return true;
    }
#endif
    if (wid >= WidgetId::CAM_SETTINGS_ROW_BASE && wid < WidgetId::CAM_SETTINGS_ROW_BASE + 5 &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int r_idx = static_cast<int>(wid - WidgetId::CAM_SETTINGS_ROW_BASE);
      auto cycle_opt = [&]<size_t N>(UiSelectId sid, const char *const (&opts)[N], uint8_t cur_val) {
        const uint8_t next_v = static_cast<uint8_t>((cur_val + 1U) % static_cast<uint8_t>(N));
        rt_.set_select_opt(sid, opts[next_v], true);
      };
      if (r_idx == 0) cycle_opt(UiSelectId::CAMERA_DEFAULT_VIEW, kCameraDefaultViewOpts, prefs.camera_default_view);
      else if (r_idx == 1) cycle_opt(UiSelectId::CAMERA_CAROUSEL_INTERVAL, kCameraCarouselIntervalOpts, prefs.camera_carousel_interval);
      else if (r_idx == 2) cycle_opt(UiSelectId::CAMERA_FRAMING, kCameraFramingOpts, prefs.camera_framing);
      else if (r_idx == 3) cycle_opt(UiSelectId::CAMERA_STREAM_MODE, kCameraStreamModeOpts, prefs.camera_stream_mode);
#if LATTER_CHIP_P4
      else if (r_idx == 4) cycle_opt(UiSelectId::CAMERA_DOORBELL_POPUP, kCameraDoorbellPopupOpts, prefs.camera_doorbell_popup);
#endif
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (rs.cameras.tab == CamerasTab::CAROUSEL) {
    int car[MAX_CAMERA_SLOTS]{};
    const int car_cnt = collect_carousel_camera_indices(cams, c_total, car, MAX_CAMERA_SLOTS);
    if (wid == WidgetId::CAM_VIDEO_TAP &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.hud_active = !rs.cameras.hud_active;
      rs.cameras.hud_timer = now_rel;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CAM_CAROUSEL_PAUSE_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.carousel_paused = !rs.cameras.carousel_paused;
      rs.cameras.carousel_last_advance_ms = now_rel;
      rs.cameras.hud_timer = now_rel;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if ((wid == WidgetId::CAM_PREV_BTN || wid == WidgetId::CAM_NEXT_BTN) && car_cnt > 1 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      rs.cameras.carousel_idx = step_wrap(
          rs.cameras.carousel_idx, (wid == WidgetId::CAM_NEXT_BTN) ? 1 : -1, car_cnt);
      rs.cameras.carousel_last_advance_ms = now_rel;
      rs.cameras.hud_timer = now_rel;
      trigger_cam_refresh();
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else {
    if (wid == WidgetId::CAM_VIDEO_TAP &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.hud_active = !rs.cameras.hud_active;
      rs.cameras.hud_timer = now_rel;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if ((wid == WidgetId::CAM_PREV_BTN || wid == WidgetId::CAM_NEXT_BTN) && c_total > 1 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      rs.cameras.selected_idx = step_wrap(
          rs.cameras.selected_idx, (wid == WidgetId::CAM_NEXT_BTN) ? 1 : -1, c_total);
      rs.cameras.hud_timer = now_rel;
      trigger_cam_refresh();
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CAM_ORIENT_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.landscape_mode = !rs.cameras.landscape_mode;
      cam_intercom_status() = rs.cameras.landscape_mode ? "90\xC2\xB0 LANDSCAPE" : "PORTRAIT VIEW";
      rs.intercom.cam_status_ms = now_rel;
      rs.cameras.hud_timer = now_rel;
      trigger_cam_refresh();
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CAM_LISTEN_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.hud_timer = now_rel;
      if (cam_call_active()) {
        intercom_session().stop_call_with_hw(now_rel);
      } else {
        cam_listen_active() = !cam_listen_active();
        rs.intercom.cam_action = cam_listen_active() ? "listen_start" : "listen_stop";
        cam_intercom_status() = cam_listen_active() ? "DOOR AUDIO ON" : "AUDIO MUTED";
        rs.intercom.cam_status_ms = now_rel;
        intercom_session().emit_current_cmd();
        if (!cam_listen_active()) {
          rt_.cmd(UiHostCmd::STOP_DOORBELL_AUDIO);
        }
      }
      rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CAM_PTT_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.hud_timer = now_rel;
      if (!cam_call_active()) {
        intercom_session().start_call(now_rel);
        rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (cam_intercom_ptt_active()) {
        rt_.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
        intercom_session().resume_listen(now_rel);
        rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
        rt_.apply_power_policy();
        mark_dirty_full();
        rt_.schedule_disp();
      } else {
        rt_.cmd(UiHostCmd::START_DOORBELL_PTT);
        rt_.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
      }
    } else if (wid == WidgetId::CAM_REPLY_MENU_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.quick_reply_open = true;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CAM_UNLOCK_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cameras.hud_timer = now_rel;
      gate_or_dispatch_doorbell_unlock(now_rel);
    }
  }

  return true;
}

}  // namespace latter_ui
