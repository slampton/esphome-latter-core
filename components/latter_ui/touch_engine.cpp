// ==============================================================================
// Latter Compiled On-Device Touch, Gesture & Rotary Input Engine
// (components/latter_ui/touch_engine.cpp)
// Top-level touch-down & touch-up orchestrator routing into per-domain modules.
// ==============================================================================

#include "touch_internal.h"

namespace latter_ui {

TouchGestureEngine &touch_engine() {
  static TouchGestureEngine s_engine;
  return s_engine;
}

void TouchGestureEngine::on_touch_down(int tx, int ty, uint32_t now_touch) {
  cancel_core_timeout(kTimeoutTouchLongPressId);
  if (!TouchEngineAccess::allow_active_touch_down(*this, tx, ty, now_touch)) {
    return;
  }

  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const TouchPt touch{tx, ty};

  auto &gest = touch_gesture();
  clear_ui_focus();
  gest.start_x = touch.x;
  gest.start_y = touch.y;
  gest.last_x = touch.x;
  gest.last_y = touch.y;
  gest.max_travel = 0;
  gest.sample_count = 1;
  gest.start_ms = now_touch;
  gest.last_touch_ms = now_touch;
  wheel_drag_dirty_ = false;
  last_user_touch_ms() = now_touch;

  if (TouchEngineAccess::handle_down_takeovers(*this, touch.x, touch.y, now_touch)) {
    return;
  }

  gest.begin(touch.x, touch.y, now_touch, rs.nav.gui_page, compute_touch_context(rs));

  const auto &ly = layout();
  const HitRegion *hit = frame_hits().hit_test(touch.x, touch.y, kHitTap);
  const uint16_t wid = (hit != nullptr) ? hit->widget_id : WidgetId::NONE;
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  if (wid == WidgetId::BACK_BUTTON) {
    gest.active = false;
  }

  music_vol_dragging_ = false;
  music_scrub_dragging_ = false;
  music_scroll_dragging_ = false;
  music_scroll_last_y_ = touch.y;

  // Universal bottom-edge swipe-up home guard ([HOME PREDICATE])
  if (can_go_home(rs) && wid != WidgetId::BACK_BUTTON && touch.y >= ly.home_zone_y()) {
    return;
  }

  if (rs.nav.gui_page == PageId::CLOCK) {
    if (touch.y <= ly.top_edge_zone_y()) return;
    if (prefs.home_screen_enabled && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
      rs.nav.gui_page = PageId::HOME;
      rs.nav.home_subpage = 0;
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return;
  }

  if (rs.nav.gui_page == PageId::HOME) {
    if (wake_greeting_latched() || rs.nav.wake_greeting_pending) {
      dismiss_wake_greeting();
      rt_.schedule_disp();
    }
    if (rs.nav.home_subpage == 1) {
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rs.nav.home_subpage = 0;
          rs.nav.home_settings_page = 1;
          if (touch_detail::return_from_shortcut_origin(rs, rt_, now_touch)) return;
          rs.nav.gui_page = PageId::SYSTEM;
          rs.nav.system_menu_mode = 0;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID, tkey)) {
        gest.active = false;
        rs.nav.home_settings_page = (wid == WidgetId::CHEVRON_UP) ? 1 : 2;
        rt_.schedule_disp();
      }
      return;
    }
    if (rs.nav.home_subpage == 2) {
      int act_count = 0;
      (void)touch_detail::resolve_home_activities(now_touch, rs, prefs, rt_, act_count);
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          rs.nav.home_subpage = 0;
          home_activity_overlay_page() = 0;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if ((wid == WidgetId::HOME_ACTIVITY_PREV || wid == WidgetId::CHEVRON_UP) &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_SCROLL, tkey)) {
        gest.active = false;
        home_activity_overlay_page() = std::max(0, home_activity_overlay_page() - 1);
        rt_.schedule_disp();
        return;
      }
      if ((wid == WidgetId::HOME_ACTIVITY_NEXT || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_SCROLL, tkey)) {
        gest.active = false;
        const int max_pg = std::max(0, (act_count - 1) / 5);
        home_activity_overlay_page() = std::min(max_pg, home_activity_overlay_page() + 1);
        rt_.schedule_disp();
      }
    }
    return;
  }

  if (rs.nav.gui_page == PageId::MUSIC) {
    if (rs.music.takeover != MusicTakeover::NONE) {
      rs.music.takeover_interacted_ms = (now_touch == 0U) ? 1U : now_touch;
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          (void)touch_detail::handle_music_back_step(rs, rt_, now_touch);
        }
        return;
      }
      if (rs.music.takeover == MusicTakeover::SCRUB) {
        const HitRegion *s_hit = frame_hits().hit_test(touch.x, touch.y, kHitHorizSlider);
        if (s_hit != nullptr && s_hit->widget_id == WidgetId::MUSIC_SCRUB_BAR) {
          music_scrub_dragging_ = true;
        }
      }
      return;
    }
    if (rs.music.view == MusicView::VOLUME && rs.music.drilldown == MusicDrilldown::NONE) {
      const HitRegion *v_hit = frame_hits().hit_test(touch.x, touch.y, kHitVertSlider);
      if (v_hit != nullptr && v_hit->widget_id == WidgetId::MUSIC_VOL_SLIDER) {
        music_vol_dragging_ = true;
        rs.music.hud_timer = now_touch;
      }
      return;
    }
    if (rs.music.view == MusicView::NOW_PLAYING && rs.music.drilldown == MusicDrilldown::NONE) {
      return;
    }
    if (wid == WidgetId::BACK_BUTTON) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        (void)touch_detail::handle_music_back_step(rs, rt_, now_touch);
      }
      return;
    }
    if (hit != nullptr && (hit->flags & kHitLongPress) != 0U) {
      gest.arm_long_press(wid, hit->param);
      schedule_core_timeout(kTimeoutTouchLongPressId, kinematics::kLongPressHoldMs, []() {
        touch_engine().tick(touch_detail::now_or_hal_ms(last_user_touch_ms()));
      });
    }
    return;
  }

  if (rs.nav.gui_page == PageId::AREA) {
    const bool in_area_picker = (rs.nav.area_subpage == AreaSubpage::PICKER ||
                                 rs.area.browse_mode == "areas" ||
                                 rs.area.browse_mode == "area_areas");
    if ((rs.nav.area_subpage > 0 || in_area_picker) && wid == WidgetId::BACK_BUTTON) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (in_area_picker && rs.area.active_area_id.empty()) {
          if (!prefs.home_screen_enabled && is_on_anchor_root(rs, prefs)) return;
          rs.nav.area_subpage = AreaSubpage::OVERVIEW;
          if (rs.area.browse_mode == "areas" || rs.area.browse_mode == "area_areas") {
            rs.area.browse_mode = "entities";
          }
          navigate_home(now_touch);
          return;
        }
        rs.nav.area_subpage = AreaSubpage::OVERVIEW;
        if (rs.area.browse_mode == "areas" || rs.area.browse_mode == "area_areas") {
          rs.area.browse_mode = "entities";
        }
        if (in_area_picker && touch_detail::return_from_shortcut_origin(rs, rt_, now_touch)) return;
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
    if (wid == WidgetId::AREA_PICKER_PILL) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (rs.nav.area_subpage == AreaSubpage::PICKER) {
          if (rs.area.active_area_id.empty()) {
            if (!prefs.home_screen_enabled && is_on_anchor_root(rs, prefs)) return;
            rs.nav.area_subpage = AreaSubpage::OVERVIEW;
            navigate_home(now_touch);
            return;
          }
          rs.nav.area_subpage = AreaSubpage::OVERVIEW;
        } else {
          rs.nav.area_subpage = AreaSubpage::PICKER;
          rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST) = 1;
          auto *aslots = rs.area_list_slots();
          if (aslots[0].name.empty()) {
            aslots[0].name = "Loading areas...";
            aslots[0].id.clear();
          }
          touch_detail::do_fetch_area_list_browse();
        }
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
    if (rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
      const std::string &idom = light_insp_domain();
      if (idom == "climate") return;
      if (idom == "cover" || idom == "fan") {
        if (wid >= WidgetId::ENTITY_PRESET_BASE && wid < WidgetId::ENTITY_PRESET_BASE + 4 &&
            touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID, tkey)) {
          gest.active = false;
          const int pct = std::clamp(static_cast<int>(hit->param), 0, 100);
          light_insp_bri_pct() = pct;
          light_insp_active() = (pct > 0);
          const int sl_i = light_insp_slot();
          if (sl_i >= 0 && sl_i < 3) {
            rs.area_entity_slots()[sl_i].level = pct;
            rs.area_entity_slots()[sl_i].active = (pct > 0);
          }
          rs.area.target_entity_id = light_insp_entity_id();
          if (pct == 0) {
            rs.area.target_action = (idom == "cover") ? "close_cover" : "turn_off";
            rs.area.target_level = 0;
          } else if (pct >= 100 && idom == "cover") {
            rs.area.target_action = "open_cover";
            rs.area.target_level = 100;
          } else {
            rs.area.target_action = "set_level";
            rs.area.target_level = pct;
          }
          rs.area.feedback_timer = now_touch;
          touch_detail::do_execute_area_action();
          rt_.schedule_disp();
          return;
        }
        if (wid == WidgetId::LIGHT_BRI_SLIDER) {
          const int raw_b = static_cast<int>(hit->norm_x(touch.x) * 100.0f);
          const int min_b = (idom == "cover") ? 0 : 5;
          light_insp_bri_pct() = std::clamp(((raw_b + 2) / 5) * 5, min_b, 100);
          light_insp_active() = (light_insp_bri_pct() > 0);
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::LIGHT_COLOR_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        gest.active = false;
        light_insp_mode() = (light_insp_mode() == 0) ? 1 : 0;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::COLOR_WHEEL_RING) {
        float hue = 0.0f, sat = 1.0f;
        if (ui::hsv_wheel_pick(touch.x, touch.y, SCREEN_CX, scale_y(244), 132, 58, hue, sat)) {
          wheel_drag_dirty_ = true;
          light_insp_hue_deg() = hue;
          light_insp_sat() = sat;
          touch_detail::update_light_inspector_hex(hue, sat);
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::LIGHT_TEMP_SLIDER) {
        const int raw_k = kLightKelvinMin +
            static_cast<int>(hit->norm_x(touch.x) * static_cast<float>(kLightKelvinMax - kLightKelvinMin));
        light_insp_kelvin() = std::clamp(((raw_k + 50) / 100) * 100, kLightKelvinMin, kLightKelvinMax);
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::LIGHT_BRI_SLIDER) {
        const int raw_b = static_cast<int>(hit->norm_x(touch.x) * 100.0f);
        const int min_b = (idom == "cover") ? 0 : 5;
        light_insp_bri_pct() = std::clamp(((raw_b + 2) / 5) * 5, min_b, 100);
        rt_.schedule_disp();
      }
    }
    return;
  }

  if (TouchEngineAccess::handle_down_cameras_games(*this, touch.x, touch.y, now_touch, wid) ||
      TouchEngineAccess::handle_down_system_tools(*this, touch.x, touch.y, now_touch, hit, wid, tkey) ||
      TouchEngineAccess::handle_down_time_cal(*this, touch.x, touch.y, now_touch, hit, wid, tkey)) {
    return;
  }
}

void TouchGestureEngine::on_touch_up(uint32_t now_rel) {
  auto &gest = touch_gesture();
  if (!gest.active) return;

  const uint64_t rel_t0_us = profiler_enabled() ? prof_now_us() : 0ULL;
  auto &rs = runtime_state();

  if (!rs.nav.hud_wake_finger_released) {
    rs.nav.hud_wake_finger_released = true;
  }

  const auto &ly = layout();
  const int dx = gest.last_x - gest.start_x;
  const int dy = gest.last_y - gest.start_y;
  const TouchPt tp{gest.last_x, gest.last_y};
  const HitRegion *start_vslider = frame_hits().hit_test(gest.start_x, gest.start_y, kHitVertSlider);
  const HitRegion *start_slider = frame_hits().hit_test(gest.start_x, gest.start_y, kHitHorizSlider);
  const int cur_ctx = compute_touch_context(rs);

  if (cur_ctx == gest.start_context && rs.nav.quick_actions_open) {
    TouchEngineAccess::handle_quick_actions_release(*this, now_rel, start_slider);
  } else if (cur_ctx == gest.start_context) {
    if (!gest.swipe_committed && start_vslider != nullptr &&
        start_vslider->widget_id == WidgetId::MUSIC_VOL_SLIDER &&
        rs.nav.gui_page == PageId::MUSIC &&
        rs.music.view == MusicView::VOLUME &&
        rs.music.drilldown == MusicDrilldown::NONE &&
        rs.music.takeover == MusicTakeover::NONE) {
      rs.music.hud_timer = now_rel;
      rs.music.takeover_interacted_ms = (now_rel == 0U) ? 1U : now_rel;
      auto &ms = media_state();
      const int min_v = ms.is_local ? 10 : 0;
      const int next_pct = std::clamp(
          static_cast<int>(std::lround((1.0f - start_vslider->norm_y(tp.y)) * 100.0f)), min_v, 100);
      if (ms.muted && next_pct > 0) ms.muted = false;
      last_local_vol_tap_ms() = now_rel;
      if (ms.is_local) {
        rt_.set_num_val(UiNumberId::SPEAKER_VOLUME, static_cast<float>(next_pct));
      } else {
        remote_vol_pct() = next_pct;
        latter_actions::emit_media_volume(touch_detail::resolve_music_speaker_entity(rs), "set", next_pct);
      }
      gest.tap_cancelled = true;
      rt_.schedule_disp();
    } else if (!gest.swipe_committed && start_slider != nullptr) {
      if (start_slider->widget_id == WidgetId::MUSIC_SCRUB_BAR &&
          rs.nav.gui_page == PageId::MUSIC && rs.music.takeover == MusicTakeover::SCRUB) {
        rs.music.takeover_interacted_ms = (now_rel == 0U) ? 1U : now_rel;
        auto &ms = media_state();
        const float dur = ms.duration;
        if (!std::isnan(dur) && dur > 0.0f) {
          const float next_pos = start_slider->norm_x(tp.x) * dur;
          ms.position = next_pos;
          ms.position_updated_ms = now_rel;
          rs.music.pos_base = next_pos;
          rs.music.pos_sync_time = now_rel;
          media_seek_absolute(now_rel);
          rt_.schedule_disp();
        }
        gest.tap_cancelled = true;
      } else if (start_slider->widget_id == WidgetId::LIGHT_TEMP_SLIDER &&
                 rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
        const int raw_k = kLightKelvinMin +
            static_cast<int>(start_slider->norm_x(tp.x) * static_cast<float>(kLightKelvinMax - kLightKelvinMin));
        light_insp_kelvin() = std::clamp(((raw_k + 50) / 100) * 100, kLightKelvinMin, kLightKelvinMax);
        rs.area.target_entity_id = light_insp_entity_id();
        rs.area.target_action = "set_kelvin";
        rs.area.target_level = light_insp_kelvin();
        rs.area.feedback_timer = now_rel;
        touch_detail::do_execute_area_action();
        gest.tap_cancelled = true;
        rt_.schedule_disp();
      } else if (start_slider->widget_id == WidgetId::LIGHT_BRI_SLIDER &&
                 rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
        const std::string &idom = light_insp_domain();
        const int raw_b = static_cast<int>(start_slider->norm_x(tp.x) * 100.0f);
        const int min_b = (idom == "cover") ? 0 : 5;
        const int pct = std::clamp(((raw_b + 2) / 5) * 5, min_b, 100);
        light_insp_bri_pct() = pct;
        if (idom == "cover" || idom == "fan") light_insp_active() = (pct > 0);
        const int sl_i = light_insp_slot();
        if (sl_i >= 0 && sl_i < 3) {
          rs.area_entity_slots()[sl_i].level = pct;
          rs.area_entity_slots()[sl_i].active = (pct > 0);
        }
        rs.area.target_entity_id = light_insp_entity_id();
        if (pct == 0 && (idom == "cover" || idom == "fan")) {
          rs.area.target_action = (idom == "cover") ? "close_cover" : "turn_off";
          rs.area.target_level = 0;
        } else if (pct >= 100 && idom == "cover") {
          rs.area.target_action = "open_cover";
          rs.area.target_level = 100;
        } else {
          rs.area.target_action = "set_level";
          rs.area.target_level = pct;
        }
        rs.area.feedback_timer = now_rel;
        touch_detail::do_execute_area_action();
        gest.tap_cancelled = true;
        rt_.schedule_disp();
      } else if (start_slider->widget_id == WidgetId::SYS_BRI_SLIDER &&
                 rs.nav.gui_page == PageId::SYSTEM && rs.nav.system_menu_mode == 2) {
        if (gest.axis_lock == GestureAxisLock::Slider) {
          UiNumberId nid = UiNumberId::MANUAL_BRIGHTNESS;
          if (touch_detail::resolve_sys_bri_slider_id(
                  start_slider->param, rt_.select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE), nid)) {
            const int next_pct = std::clamp(
                1 + static_cast<int>(std::lround(start_slider->norm_x(tp.x) * 99.0f)), 1, 100);
            rt_.set_num_val(nid, static_cast<float>(next_pct));
            rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
            rt_.schedule_disp();
          }
          gest.tap_cancelled = true;
        } else {
          (void)try_commit_kinematic_swipe_(true, now_rel);
        }
      } else if (start_slider->widget_id == WidgetId::SYS_VOL_SLIDER &&
                 rs.nav.gui_page == PageId::SYSTEM && rs.nav.system_menu_mode == 7) {
        if (gest.axis_lock == GestureAxisLock::Slider) {
          const int next_pct = std::clamp(
              10 + static_cast<int>(std::lround(start_slider->norm_x(tp.x) * 90.0f)), 10, 100);
          rt_.set_num_val(UiNumberId::SPEAKER_VOLUME, static_cast<float>(next_pct));
          rt_.schedule_disp();
          gest.tap_cancelled = true;
        } else {
          (void)try_commit_kinematic_swipe_(true, now_rel);
        }
      }
    } else if (!gest.swipe_committed && rs.nav.gui_page == PageId::AREA &&
               rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL &&
               light_insp_domain() == "light" && light_insp_mode() == 0) {
      float hue = 0.0f, sat = 1.0f;
      const bool picked = ui::hsv_wheel_pick(tp.x, tp.y, SCREEN_CX, scale_y(244), 132, 58, hue, sat);
      if (picked) {
        light_insp_hue_deg() = hue;
        light_insp_sat() = sat;
        touch_detail::update_light_inspector_hex(hue, sat);
      }
      if (picked || wheel_drag_dirty_) {
        wheel_drag_dirty_ = false;
        gest.tap_cancelled = true;
        rs.area.target_entity_id = light_insp_entity_id();
        rs.area.target_action = "set_color";
        rs.area.target_level = light_insp_bri_pct();
        rs.area.feedback_timer = now_rel;
        touch_detail::do_execute_area_action();
        rt_.schedule_disp();
      } else {
        (void)try_commit_kinematic_swipe_(true, now_rel);
      }
    } else {
      (void)try_commit_kinematic_swipe_(true, now_rel);
    }

    const bool tap_valid = !gest.swipe_committed && !gest.tap_cancelled && (gest.max_travel <= ly.tap_max());
    if (tap_valid) {
      const HitRegion *hit = frame_hits().hit_test(tp.x, tp.y, kHitTap);
      if (hit == nullptr) {
        hit = frame_hits().hit_test(gest.start_x, gest.start_y, kHitTap);
      }
      const uint16_t wid = (hit != nullptr) ? hit->widget_id : WidgetId::NONE;
      (void)(handle_tap_nav_and_takeovers_(tp.x, tp.y, now_rel, hit, wid) ||
             handle_tap_music_(tp.x, tp.y, now_rel, hit, wid) ||
             handle_tap_home_area_(tp.x, tp.y, now_rel, hit, wid) ||
             handle_tap_time_cal_(tp.x, tp.y, now_rel, hit, wid) ||
             handle_tap_system_tools_(tp.x, tp.y, now_rel, hit, wid) ||
             handle_tap_cameras_games_(tp.x, tp.y, now_rel, hit, wid));
    }
  }

  if (rel_t0_us > 0ULL && profiler_enabled()) {
    const uint32_t rel_exec_us = static_cast<uint32_t>(prof_now_us() - rel_t0_us);
    const int end_ctx = compute_touch_context(rs);
    const uint32_t dur_p = (now_rel >= gest.start_ms) ? (now_rel - gest.start_ms) : 0U;
    LATTER_PROF_LOGI(
        "[PROF:TOUCH_UP] start=(%d,%d) end=(%d,%d) dx=%d dy=%d travel=%d samples=%d dur=%lums swipe=%d ctx=%d->%d exec=%luus",
        gest.start_x, gest.start_y, gest.last_x, gest.last_y,
        dx, dy, gest.max_travel, gest.sample_count,
        static_cast<unsigned long>(dur_p), static_cast<int>(gest.swipe_committed),
        gest.start_context, end_ctx, static_cast<unsigned long>(rel_exec_us));
  }
  cancel_core_timeout(kTimeoutTouchLongPressId);
  const uint16_t lp_wid = gest.long_press_wid;
  gest.active = false;
  gest.long_press_armed = false;
  if (lp_wid != 0U) {
    if (rs.nav.gui_page == PageId::REMOTE && rs.nav.remote_subpage == 0) {
      const HitRegion *lp_reg = frame_hits().find_by_id(lp_wid);
      if (lp_reg != nullptr) {
        rt_.schedule_band_disp(std::max(0, static_cast<int>(lp_reg->y0) - 6),
                               std::min(SCREEN_H - 1, static_cast<int>(lp_reg->y1) + 6));
      } else {
        rt_.schedule_disp();
      }
    } else {
      rt_.schedule_disp();
    }
  }
}

}  // namespace latter_ui
