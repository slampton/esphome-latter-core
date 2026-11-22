// ==============================================================================
// Latter Rotary Encoder, Hardware Button Navigation & Focus Ring Activation
// (components/latter_ui/touch_rotary_focus.cpp)
// ==============================================================================

#include "touch_internal.h"

namespace latter_ui {

void TouchGestureEngine::on_input_event(const std::string &action, int delta, uint32_t now_in) {
  auto &rs = runtime_state();

  if (rs.nav.ota_in_progress) return;

  if (rs.nav.display_power_state != 0 || !rt_.panel_is_on) {
    note_wake_reason(WakeReason::TOUCH);
    rt_.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    return;
  }

  display_lifecycle().note_activity(now_in);
  last_user_touch_ms() = now_in;
  activate_ui_focus(now_in);

  // Voice Assistant cancels immediately on any hardware input
  if (!rs.alarms.ringing && !rs.timers.ringing && !rs.alarms.find_my_active &&
      (rs.voice.active || !rs.voice.status_text.empty())) {
    rt_.cmd(UiHostCmd::CANCEL_VOICE_ASSISTANT);
    return;
  }

  std::string act = action;
  for (char &c : act) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  int step = delta;
  if (step == 0) {
    if (act == "next" || act == "right" || act == "down" || act == "cw") step = 1;
    else if (act == "prev" || act == "left" || act == "up" || act == "ccw") step = -1;
  }

  if (act == "quick_actions") {
    if (!rs.nav.is_hibernating && !rs.alarms.ringing && !rs.timers.ringing &&
        !rs.alarms.find_my_active && !rs.alert.active &&
        !ui::pin_pad_state().open && !shortcut_confirm_state().open) {
      rs.nav.quick_actions_open = true;
      ui_focus_index() = 0;
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return;
  }

  if (act == "home") {
    if (rs.nav.quick_actions_open) {
      rs.nav.quick_actions_open = false;
      ui_focus_index() = 0;
      mark_dirty_full();
      rt_.schedule_disp();
      return;
    }
    if (can_go_home(rs)) {
      ui_focus_index() = 0;
      navigate_home(now_in);
    }
    return;
  }

  if (act == "back") {
    if (ui::pin_pad_state().open) {
      TouchEngineAccess::scrub_pin_and_pending(*this, rs, true);
      ui_focus_index() = 0;
      mark_dirty_full();
      rt_.schedule_disp();
      return;
    }
    if (shortcut_confirm_state().open) {
      shortcut_confirm_state().clear();
      ui_focus_index() = 0;
      mark_dirty_full();
      rt_.schedule_disp();
      return;
    }
    if (rs.nav.quick_actions_open) {
      rs.nav.quick_actions_open = false;
      ui_focus_index() = 0;
      mark_dirty_full();
      rt_.schedule_disp();
      return;
    }
    if (rs.nav.gui_page == PageId::MUSIC &&
        touch_detail::handle_music_back_step(rs, rt_, now_in)) {
      ui_focus_index() = 0;
      activate_ui_focus(now_in);
      return;
    }
    const HitRegion *back_reg = frame_hits().find_by_id(WidgetId::BACK_BUTTON);
    if (back_reg != nullptr) {
      on_touch_down(back_reg->cx(), back_reg->cy(), now_in);
      if (touch_gesture().active) on_touch_up(now_in);
      ui_focus_index() = 0;
      activate_ui_focus(now_in);
      return;
    }
    if (can_go_home(rs)) {
      ui_focus_index() = 0;
      navigate_home(now_in);
    }
    return;
  }

  const int focus_cnt = frame_hits().focus_count();

  if (step != 0) {
    if (!ui::pin_pad_state().open && !shortcut_confirm_state().open &&
        !rs.nav.quick_actions_open &&
        !rs.alarms.ringing && !rs.timers.ringing &&
        !rs.alarms.find_my_active && !rs.alert.active) {
      if (rs.nav.gui_page == PageId::WEATHER) {
        rs.nav.sky_weather_subpage = step_wrap(rs.nav.sky_weather_subpage, step, 4);
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (rs.nav.gui_page == PageId::TIME &&
          rs.nav.time_subpage == TimeSubpage::ALARMS &&
          rs.alarms.edit_idx >= 1 && rs.alarms.edit_idx <= 3) {
        auto &ap = persisted_prefs().alarms[rs.alarms.edit_idx - 1];
        if (rs.nav.alarm_edit_field == 0) {
          ap.hour = static_cast<uint8_t>((static_cast<int>(ap.hour) + 24 + (step % 24)) % 24);
        } else {
          ap.minute = static_cast<uint8_t>((static_cast<int>(ap.minute) + 60 + (step % 60)) % 60);
        }
        latter_actions::sync_local_alarms_edit(now_in);
        rt_.schedule_disp();
        return;
      }
      if (rs.nav.gui_page == PageId::GAMES && rs.nav.active_game == 0 && !rs.nav.menu_scroll_active) {
        const int game_cnt = active_games_launcher_count(device_prefs());
        if (game_cnt > 1) {
          const int cur_slot = std::clamp(rs.nav.game_carousel_idx, 0, game_cnt - 1);
          rs.nav.game_carousel_idx = cur_slot;
          rs.nav.menu_scroll_active = true;
          rs.nav.menu_scroll_dir = (step > 0) ? 1 : -1;
          rs.nav.menu_scroll_target_page = step_wrap(cur_slot, step, game_cnt);
          rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
          return;
        }
      }
    }
    if (focus_cnt > 0) {
      ui_focus_index() = step_wrap(ui_focus_index(), step, focus_cnt);
      rt_.schedule_disp();
    }
    return;
  }

  if (act == "select" || act == "enter") {
    if (rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active) {
      const HitRegion *foc = frame_hits().focus_at(ui_focus_index());
      if (foc != nullptr) {
        on_touch_down(foc->cx(), foc->cy(), now_in);
      } else {
        rs.alarms.ringing = false;
        rs.timers.ringing = false;
        rs.alarms.find_my_active = false;
        latter_actions::check_and_emit_timer_alarm_state(now_in);
        rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
    if (rs.alert.active) {
      const HitRegion *foc = frame_hits().focus_at(ui_focus_index());
      if (foc != nullptr) {
        on_touch_down(foc->cx(), foc->cy(), now_in);
      } else {
        rt_.cmd(UiHostCmd::DISMISS_NOTIFICATION);
      }
      return;
    }
    if (!rs.nav.quick_actions_open &&
        rs.nav.gui_page == PageId::TIME &&
        rs.nav.time_subpage == TimeSubpage::ALARMS &&
        rs.alarms.edit_idx >= 1 && rs.alarms.edit_idx <= 3) {
      rs.nav.alarm_edit_field = (rs.nav.alarm_edit_field == 0) ? 1 : 0;
      rt_.schedule_disp();
      return;
    }
    const HitRegion *foc = frame_hits().focus_at(ui_focus_index());
    if (foc != nullptr) {
      on_touch_down(foc->cx(), foc->cy(), now_in);
      if (touch_gesture().active) on_touch_up(now_in);
      activate_ui_focus(now_in);
    }
  }
}

}  // namespace latter_ui
