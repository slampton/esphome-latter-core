// ==============================================================================
// Latter Compiled On-Device Gesture & Hit-Table Input Engine
// (components/latter_ui/gesture_engine.cpp)
// ==============================================================================

#include "ui_gesture_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

#ifdef USE_ESP32
#include <esp_random.h>
#endif

#include "latter_state.h"
#include "ui_ambient_va.h"
#include "ui_area_home_hub.h"
#include "ui_clock_home.h"
#include "ui_compositor.h"
#include "ui_games.h"
#include "ui_music.h"
#include "ui_remote.h"
#include "ui_system.h"
#include "ui_tools.h"
#include "ui_touch_router.h"
#include "ui_weather_sky.h"

namespace latter_ui {

namespace {

inline uint32_t next_rng_u32() {
#ifdef USE_ESP32
  return esp_random();
#else
  return static_cast<uint32_t>(std::rand());
#endif
}

}  // namespace

TouchGestureEngine &touch_engine() {
  static TouchGestureEngine s_engine;
  return s_engine;
}

void TouchGestureEngine::navigate_home(uint32_t /*now_ms*/) {
  if (*rt_.active_game > 0) {
    rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
  }
  if (latter_ui::cam_call_active() || latter_ui::cam_intercom_ptt_active() || latter_ui::cam_listen_active()) {
    rt_.cmd(UiHostCmd::STOP_DOORBELL_CALL);
  }
  const bool was_cam_page = (*rt_.gui_page == 8 && *rt_.tools_subpage == 12);
  ui::pin_pad_scrub();
  latter_ui::tools_direct_home() = false;
  latter_ui::timer_view_slot() = 0;
  *rt_.alarm_edit_idx = 0;
  latter_ui::cal_detail_mode() = 0;
  latter_ui::cal_quick_add_open() = false;
  *rt_.gui_page = 1;
  *rt_.home_subpage = 0;
  *rt_.music_overlay_mode = 0;
  *rt_.system_menu_mode = 0;
  *rt_.active_game = 0;
  *rt_.music_direct_nav = false;
  *rt_.sleep_menu_page = 1;
  *rt_.display_menu_page = 1;
  *rt_.system_dashboard_page = 1;
  *rt_.menu_scroll_active = false;
  *rt_.music_hud_active = false;
  *rt_.remote_subpage = 0;
  *rt_.tools_subpage = 0;
  *rt_.tools_menu_page = 0;
  *rt_.sky_weather_subpage = 0;
  if (was_cam_page) {
    rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
    if (rt_.current_loaded_camera_url != nullptr) {
      rt_.current_loaded_camera_url->clear();
    }
    rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
  }
  mark_dirty_full();
  rt_.schedule_disp();
}

bool TouchGestureEngine::handle_pin_pad_tap(int tx, int ty, uint32_t now_ms) {
  auto &st = ui::pin_pad_state();
  if (!st.open) return false;

  const TouchPt touch{tx, ty};
  const HitRegion *hit = frame_hits().hit_test(tx, ty, kHitTap);
  if ((hit != nullptr && hit->widget_id == WidgetId::PIN_PAD_CANCEL) || latter_ui::is_back_button(touch)) {
    if (latter_ui::touch_router().allow_tap(now_ms, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
      ui::pin_pad_scrub();
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return true;
  }
  if (hit == nullptr) {
    return true;
  }
  if (hit->widget_id >= WidgetId::PIN_PAD_KEY_BASE && hit->widget_id <= WidgetId::PIN_PAD_KEY_BASE + 9) {
    if (latter_ui::touch_router().allow_tap(now_ms, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
      ui::pin_pad_press_digit(static_cast<uint8_t>(hit->widget_id - WidgetId::PIN_PAD_KEY_BASE));
      rt_.schedule_disp();
    }
    return true;
  }
  if (hit->widget_id == WidgetId::PIN_PAD_BACKSPACE) {
    if (latter_ui::touch_router().allow_tap(now_ms, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
      ui::pin_pad_backspace();
      rt_.schedule_disp();
    }
    return true;
  }
  if (hit->widget_id == WidgetId::PIN_PAD_SUBMIT) {
    if (latter_ui::touch_router().allow_tap(now_ms, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
      if (st.len < 4) {
        std::snprintf(st.error_msg, sizeof(st.error_msg), "Enter 4-8 digits");
        st.error_ms = now_ms;
        rt_.schedule_disp();
        return true;
      }
      std::string pin_str(st.digits, st.len);
      const auto target = st.target;
      ui::pin_pad_scrub();
      if (target == ui::PinPadTargetKind::AREA_ACTION) {
        *rt_.area_target_code = pin_str;
        *rt_.area_feedback_timer = now_ms;
        rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
      } else if (target == ui::PinPadTargetKind::ALARM_ACTION) {
        *rt_.area_target_code = pin_str;
        *rt_.area_feedback_timer = now_ms;
        rt_.cmd(UiHostCmd::EXECUTE_ALARM_ACTION);
      } else if (target == ui::PinPadTargetKind::DOORBELL_UNLOCK) {
        *rt_.cam_intercom_code_str = pin_str;
        *rt_.cam_intercom_action = "unlock_door";
        *rt_.cam_intercom_message = "";
        *rt_.intercom_feedback_str = "Unlocked Door / Gate";
        *rt_.intercom_feedback_ms = now_ms;
        latter_ui::cam_intercom_status() = "DOOR / GATE UNLOCKED";
        *rt_.cam_intercom_status_ms = now_ms;
        rt_.cmd(UiHostCmd::SEND_DOORBELL_INTERCOM_CMD);
      }
      std::fill(pin_str.begin(), pin_str.end(), '\0');
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return true;
  }
  return true;
}

bool TouchGestureEngine::gate_or_dispatch_area_action(const std::string &domain_or_entity,
                                                      const std::string &action, int level,
                                                      const char *pin_prompt, uint32_t now_ms) {
  const std::string &pol = latter_ui::area_ctx_state().security_pin_policy;
  const bool has_alarm = (rt_.area_alarm_entity != nullptr && !rt_.area_alarm_entity->empty());
  if (ui::is_perimeter_action_blocked(pol, domain_or_entity, action, level)) {
    *rt_.area_action_feedback = "Blocked by Security Policy";
    *rt_.area_feedback_timer = now_ms;
    rt_.schedule_disp();
    return false;
  }
  *rt_.area_target_action = action;
  *rt_.area_target_level = level;
  if (ui::requires_perimeter_pin_prompt(pol, domain_or_entity, action, level, has_alarm)) {
    ui::pin_pad_open(ui::PinPadTargetKind::AREA_ACTION, pin_prompt);
    mark_dirty_full();
    rt_.schedule_disp();
    return false;
  }
  *rt_.area_feedback_timer = now_ms;
  rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
  rt_.schedule_disp();
  return true;
}

bool TouchGestureEngine::gate_or_dispatch_alarm_action(const std::string &action, uint32_t now_ms) {
  const std::string &pol = latter_ui::area_ctx_state().security_pin_policy;
  const bool has_alarm = (rt_.area_alarm_entity != nullptr && !rt_.area_alarm_entity->empty());
  if (ui::is_perimeter_action_blocked(pol, "alarm_control_panel", action, 100)) {
    *rt_.area_action_feedback = "Blocked by Security Policy";
    *rt_.area_feedback_timer = now_ms;
    rt_.schedule_disp();
    return false;
  }
  *rt_.area_alarm_service = action;
  if (ui::requires_perimeter_pin_prompt(pol, "alarm_control_panel", action, 100, has_alarm)) {
    ui::pin_pad_open(ui::PinPadTargetKind::ALARM_ACTION, "ENTER ALARM PIN");
    mark_dirty_full();
    rt_.schedule_disp();
    return false;
  }
  *rt_.area_feedback_timer = now_ms;
  rt_.cmd(UiHostCmd::EXECUTE_ALARM_ACTION);
  rt_.schedule_disp();
  return true;
}

bool TouchGestureEngine::gate_or_dispatch_doorbell_unlock(uint32_t now_ms, bool from_intercom) {
  const std::string &pol = latter_ui::area_ctx_state().security_pin_policy;
  const bool has_alarm = (rt_.area_alarm_entity != nullptr && !rt_.area_alarm_entity->empty());
  if (ui::is_perimeter_action_blocked(pol, "lock", "unlock", 100)) {
    if (from_intercom) {
      *rt_.intercom_feedback_str = "Blocked by Security Policy";
      *rt_.intercom_feedback_ms = now_ms;
    } else {
      latter_ui::cam_intercom_status() = "BLOCKED BY POLICY";
      *rt_.cam_intercom_status_ms = now_ms;
    }
    rt_.schedule_disp();
    return false;
  }
  if (ui::requires_perimeter_pin_prompt(pol, "lock", "unlock", 100, has_alarm)) {
    ui::pin_pad_open(ui::PinPadTargetKind::DOORBELL_UNLOCK, "UNLOCK DOOR PIN");
    mark_dirty_full();
    rt_.schedule_disp();
    return false;
  }
  *rt_.cam_intercom_action = "unlock_door";
  *rt_.cam_intercom_message = "";
  if (from_intercom) {
    *rt_.intercom_feedback_str = "Unlocked Door / Gate";
    *rt_.intercom_feedback_ms = now_ms;
  } else {
    latter_ui::cam_intercom_status() = "DOOR / GATE UNLOCKED";
    *rt_.cam_intercom_status_ms = now_ms;
  }
  rt_.cmd(UiHostCmd::SEND_DOORBELL_INTERCOM_CMD);
  rt_.schedule_disp();
  return true;
}

void TouchGestureEngine::on_touch_down(int tx, int ty, uint32_t now_touch) {
  if (!rt_.bound) return;
  const TouchPt touch{tx, ty};
  ESP_LOGD("touch", ">>> Touch Down: x=%d y=%d page=%d", touch.x, touch.y, *rt_.gui_page);

  if (*rt_.is_hibernating) {
    if (*rt_.hibernate_countdown_val > 0) {
      *rt_.is_hibernating = false;
      *rt_.hibernate_countdown_val = 0;
      latter_ui::note_wake_reason(latter_ui::WakeReason::TOUCH);
      rt_.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    }
    return;
  }

  if (*rt_.display_power_state != 0 || !rt_.panel_is_on) {
    const std::string tw_mode = rt_.select_opt(UiSelectId::TAP_WAKE_MODE);
    if (tw_mode == "Disabled") return;
    if (tw_mode == "Docked Only" && !rt_.usb_connected) return;
    if (!latter_ui::accept_screen_off_tap(rt_.usb_connected, now_touch)) return;
    latter_ui::note_wake_reason(latter_ui::WakeReason::TOUCH);
    rt_.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    return;
  }

  if (now_touch - latter_ui::last_screen_wake_ms() < latter_ui::TOUCH_DEBOUNCE_WAKE) {
    if (latter_ui::profiler_enabled()) {
      LATTER_PROF_LOGI("[PROF:TOUCH_BLOCKED] x=%d y=%d page=%d reason=WAKE_DEBOUNCE",
                       touch.x, touch.y, *rt_.gui_page);
    }
    return;
  }

  if (now_touch - last_touch_activity_ms_ > 1000U) {
    last_touch_activity_ms_ = now_touch;
    rt_.cmd(UiHostCmd::SCREEN_SLEEP_TIMER);
  }

  auto &tg_init = latter_ui::touch_gesture();
  latter_ui::clear_ui_focus();
  tg_init.start_x = touch.x;
  tg_init.start_y = touch.y;
  tg_init.last_x = touch.x;
  tg_init.last_y = touch.y;
  tg_init.max_travel = 0;
  tg_init.sample_count = 1;
  tg_init.start_ms = now_touch;
  tg_init.last_touch_ms = now_touch;

  if (*rt_.va_active || !rt_.va_status_text->empty()) {
    latter_ui::touch_gesture().active = false;
    latter_ui::last_screen_wake_ms() = now_touch;
    rt_.cmd(UiHostCmd::CANCEL_VOICE_ASSISTANT);
    return;
  }

  if (ui::pin_pad_state().open) {
    latter_ui::touch_gesture().active = false;
    handle_pin_pad_tap(touch.x, touch.y, now_touch);
    return;
  }

  if (*rt_.notif_active) {
    latter_ui::touch_gesture().active = false;
    latter_ui::last_screen_wake_ms() = now_touch;
    const bool has_notif_cam = !rt_.notif_camera_url->empty();
    if (has_notif_cam &&
        ((touch.y >= latter_ui::scale_y(390) && touch.x < latter_ui::SCREEN_CX) ||
         touch.y < latter_ui::scale_y(380))) {
      *rt_.notif_active = false;
      latter_ui::tools_direct_home() = true;
      latter_ui::cam_quick_reply_open() = false;
      *rt_.gui_page = 8;
      *rt_.tools_subpage = 12;
      rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
      rt_.cmd(UiHostCmd::START_DOORBELL_CALL);
      rt_.schedule_disp();
      return;
    }
    rt_.cmd(UiHostCmd::DISMISS_NOTIFICATION);
    return;
  }

  if (*rt_.alarm_ringing || *rt_.timer_ringing || *rt_.find_my_active) {
    latter_ui::touch_gesture().active = false;
    if (*rt_.find_my_active) {
      *rt_.find_my_active = false;
      latter_ui::last_screen_wake_ms() = now_touch;
      rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
      rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
      rt_.schedule_disp();
      return;
    }
    if (*rt_.alarm_ringing) {
      if (touch.y >= latter_ui::scale_y(268) && touch.y <= latter_ui::scale_y(324) &&
          touch.x >= latter_ui::SCREEN_CX - latter_ui::scale_x(140) &&
          touch.x <= latter_ui::SCREEN_CX + latter_ui::scale_x(140)) {
        *rt_.alarm_snooze_until = (rt_.time_epoch > 0) ? (rt_.time_epoch + 300U) : ((now_touch / 1000U) + 300U);
        *rt_.alarm_ringing = false;
        latter_ui::last_screen_wake_ms() = now_touch;
        rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
        rt_.schedule_disp();
        return;
      }
      if (touch.y >= latter_ui::scale_y(330) && touch.y <= latter_ui::SCREEN_H) {
        *rt_.alarm_ringing = false;
        latter_ui::last_screen_wake_ms() = now_touch;
        rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
        rt_.schedule_disp();
        return;
      }
    } else if (*rt_.timer_ringing) {
      *rt_.timer_ringing = false;
      latter_ui::last_screen_wake_ms() = now_touch;
      rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
      rt_.schedule_disp();
      return;
    }
    return;
  }

  auto &gest = latter_ui::touch_gesture();
  gest.start_x = touch.x;
  gest.start_y = touch.y;
  gest.last_x = touch.x;
  gest.last_y = touch.y;
  gest.max_travel = 0;
  gest.sample_count = 1;
  gest.start_ms = now_touch;
  gest.last_touch_ms = now_touch;
  gest.start_page = *rt_.gui_page;
  gest.start_context = latter_ui::compute_touch_context(
      *rt_.gui_page, *rt_.home_subpage, *rt_.music_overlay_mode, *rt_.active_game,
      *rt_.system_menu_mode, *rt_.sky_weather_subpage, *rt_.area_subpage,
      *rt_.remote_subpage, *rt_.tools_subpage, latter_ui::whole_home_tab());
  gest.active = true;
  gest.swipe_committed = false;

  const bool is_top_level_home_bar =
      latter_ui::can_go_home(
          *rt_.gui_page, *rt_.system_menu_mode, *rt_.active_game, *rt_.music_overlay_mode,
          *rt_.area_subpage, *rt_.area_browse_mode == "areas", *rt_.remote_subpage, *rt_.tools_subpage,
          *rt_.alarm_edit_idx, *rt_.alarm_ringing || *rt_.timer_ringing || *rt_.find_my_active) &&
      !latter_ui::music_art_only(*rt_.gui_page,
                                 rt_.select_opt(UiSelectId::MUSIC_OVERLAY_STYLE) == "Always Visible",
                                 *rt_.music_hud_active);
  const int home_bar_guard_y = latter_ui::layout().chin_guard_y();
  if (is_top_level_home_bar && touch.y >= home_bar_guard_y) {
    if (latter_ui::profiler_enabled()) {
      LATTER_PROF_LOGI("[PROF:TOUCH_HOLD] x=%d y=%d page=%d reason=HOME_BAR_SWIPE_ZONE",
                       touch.x, touch.y, *rt_.gui_page);
    }
    return;
  }

  if (latter_ui::is_right_chevron_zone(touch)) {
    const int sign = (touch.y < latter_ui::SCREEN_CY) ? -1 : 1;
    if (*rt_.gui_page == 2 && (*rt_.music_overlay_mode == 1 || *rt_.music_overlay_mode == 3) &&
        *rt_.music_browse_total_pages > 1) {
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_touch;
      latter_ui::music_scroll_tap_lockout_until_ms() = now_touch + 550U;
      const uint32_t gap_c = now_touch - last_rapid_chev_ms_;
      if (gap_c >= 110U) {
        last_rapid_chev_ms_ = now_touch;
        const int step_c = sign * ((gap_c < 260U && *rt_.music_browse_total_pages >= 12) ? 2 : 1);
        const bool wrap_list = (*rt_.music_overlay_mode == 3) &&
                               (*rt_.selected_category_mode == "artist" || *rt_.selected_category_mode == "album");
        const int next_pg = wrap_list
            ? latter_ui::step_wrap_1based(*rt_.music_browse_page, step_c, *rt_.music_browse_total_pages)
            : std::max(1, std::min(*rt_.music_browse_total_pages, *rt_.music_browse_page + step_c));
        if (next_pg != *rt_.music_browse_page) {
          *rt_.music_browse_page = next_pg;
          latter_ui::last_music_page_req_ms() = now_touch;
          *rt_.menu_scroll_active = false;
          rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
        }
      }
      return;
    }
  }

  const HitRegion *hit = frame_hits().hit_test(touch.x, touch.y, kHitTap);

  if (*rt_.gui_page == 1) {
    if (*rt_.home_subpage == 1) {
      const int slot_idx = latter_ui::layout().hit_slot5(touch, 90, 376);
      if (slot_idx >= 0 && slot_idx <= 4 &&
          latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        const std::string next_csv = latter_ui::toggled_dock_csv(latter_ui::kHomeSettingsDockTokens[slot_idx]);
        if (!next_csv.empty()) {
          latter_ui::latter_dock_order_csv() = next_csv;
          rt_.cmd(UiHostCmd::REQUEST_DOCK_APPS);
        }
        latter_ui::home_dock_page() = 0;
        rt_.schedule_disp();
        return;
      }
      if (latter_ui::is_back_button(touch) || touch.y <= latter_ui::scale_y(72)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.home_subpage = 0;
          *rt_.gui_page = 4;
          *rt_.system_menu_mode = 0;
          rt_.schedule_disp();
        }
        return;
      }
      return;
    }

    if (*rt_.home_subpage == 0) {
      const bool hit_celestial =
          (touch.x <= (LATTER_ROUND_DISPLAY ? 156 : latter_ui::scale_x(116)) && touch.y <= latter_ui::scale_y(130));
      if (rt_.sw_state(UiSwitchId::HOME_WEATHER_BG) && hit_celestial) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          const std::string s_state = latter_ui::weather_state().sun_state;
          std::string cond = latter_ui::weather_state().condition;
          bool is_night = (s_state == "below_horizon" || cond == "clear-night" || cond == "clearnight" ||
                           (cond.find("night") != std::string::npos));
          latter_ui::apply_weather_sim_override(rt_.select_opt(UiSelectId::WEATHER_SIM_CONDITION), cond, is_night);
          *rt_.sky_weather_subpage = is_night ? 3 : 2;
          *rt_.gui_page = 5;
          rt_.schedule_disp();
        }
        return;
      }
      if (latter_ui::layout().hit_home_clock(touch.x, touch.y)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.gui_page = 0;
          rt_.schedule_disp();
        }
        return;
      }
      if (latter_ui::layout().hit_home_weather(touch.x, touch.y)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.sky_weather_subpage = 0;
          *rt_.gui_page = 5;
          rt_.schedule_disp();
        }
        return;
      }
    }

    latter_ui::HomeActivityItem act_items[8];
    const auto &ms_hm = latter_ui::media_state();
    const bool is_rem_h = !ms_hm.is_local;
    const bool mus_play_h = latter_ui::is_music_playing_effective(is_rem_h, rt_.local_mp_playing, ms_hm.state, now_touch);
    const std::string m_title_h = (is_rem_h || rt_.track_title.empty()) ? ms_hm.title : rt_.track_title;
    const std::string m_artist_h = (is_rem_h || rt_.track_artist.empty()) ? ms_hm.artist : rt_.track_artist;
    const int act_count = latter_ui::collect_home_activities(
        now_touch, *rt_.is_24h_format,
        *rt_.stopwatch_running, *rt_.stopwatch_start_time, *rt_.stopwatch_elapsed_ms,
        mus_play_h, m_title_h, m_artist_h,
        *rt_.alarm_snooze_until,
        *rt_.alarm1_hour, *rt_.alarm1_minute, *rt_.alarm1_enabled,
        *rt_.alarm2_hour, *rt_.alarm2_minute, *rt_.alarm2_enabled,
        *rt_.alarm3_hour, *rt_.alarm3_minute, *rt_.alarm3_enabled,
        act_items,
        rt_.time_valid ? rt_.time_hour : -1,
        rt_.time_valid ? rt_.time_minute : -1,
        latter_ui::calendar_state().summary);

    auto handle_activity_item_action = [&](int item_i, bool is_inline_btn) {
      const int kind = act_items[item_i].kind;
      const int s_idx = act_items[item_i].slot_idx;
      if (is_inline_btn && kind != 5) {
        if (kind == 1) {
          auto *t_slots = latter_ui::timer_slots();
          const int ti = (s_idx >= 0 && s_idx < 3) ? s_idx : 0;
          auto &s = t_slots[ti];
          if (s.running) {
            s.running = false;
            s.paused = true;
            s.remaining_sec = (s.target_ms > now_touch)
                                  ? static_cast<int>((s.target_ms - now_touch + 999U) / 1000U)
                                  : 0;
          } else {
            s.running = true;
            s.paused = false;
            s.target_ms = now_touch + static_cast<uint32_t>(s.remaining_sec) * 1000U;
          }
          latter_ui::sync_multi_timer_globals(
              *rt_.timer_running, *rt_.timer_paused, *rt_.timer_remaining_sec,
              *rt_.timer_duration_sec, *rt_.timer_target_ms);
          rt_.apply_power_policy();
        } else if (kind == 2) {
          if (!*rt_.stopwatch_running) {
            *rt_.stopwatch_running = true;
            *rt_.stopwatch_start_time = now_touch - *rt_.stopwatch_elapsed_ms;
            *rt_.sleep_start_time = 0;
          } else {
            *rt_.stopwatch_running = false;
            *rt_.stopwatch_elapsed_ms = now_touch - *rt_.stopwatch_start_time;
          }
        } else if (kind == 3) {
          rt_.cmd(UiHostCmd::TOGGLE_HA_MUSIC);
        } else if (kind == 4) {
          if (s_idx == 0 && *rt_.alarm_snooze_until > 0) *rt_.alarm_snooze_until = 0;
          else if (s_idx == 1) *rt_.alarm1_enabled = false;
          else if (s_idx == 2) *rt_.alarm2_enabled = false;
          else if (s_idx == 3) *rt_.alarm3_enabled = false;
        }
        if (*rt_.home_subpage == 2) {
          latter_ui::HomeActivityItem rem_items[8];
          const int rem_cnt = latter_ui::collect_home_activities(
              now_touch, *rt_.is_24h_format,
              *rt_.stopwatch_running, *rt_.stopwatch_start_time, *rt_.stopwatch_elapsed_ms,
              mus_play_h, m_title_h, m_artist_h,
              *rt_.alarm_snooze_until,
              *rt_.alarm1_hour, *rt_.alarm1_minute, *rt_.alarm1_enabled,
              *rt_.alarm2_hour, *rt_.alarm2_minute, *rt_.alarm2_enabled,
              *rt_.alarm3_hour, *rt_.alarm3_minute, *rt_.alarm3_enabled,
              rem_items,
              rt_.time_valid ? rt_.time_hour : -1,
              rt_.time_valid ? rt_.time_minute : -1,
              latter_ui::calendar_state().summary);
          if (rem_cnt < 2) {
            *rt_.home_subpage = 0;
            latter_ui::home_activity_overlay_page() = 0;
          }
        }
        rt_.schedule_disp();
        return;
      }

      *rt_.home_subpage = 0;
      latter_ui::home_dock_page() = 0;
      if (kind == 1) {
        latter_ui::tools_direct_home() = true;
        *rt_.gui_page = 8;
        *rt_.tools_subpage = 1;
        latter_ui::timer_view_slot() = (s_idx >= 0 && s_idx < 3) ? (s_idx + 1) : 0;
      } else if (kind == 2) {
        latter_ui::tools_direct_home() = true;
        *rt_.gui_page = 8;
        *rt_.tools_subpage = 3;
      } else if (kind == 3) {
        latter_ui::tools_direct_home() = false;
        *rt_.gui_page = 2;
        *rt_.music_overlay_mode = 0;
        *rt_.music_hud_active = true;
        *rt_.music_hud_timer = now_touch;
        if ((!ms_hm.is_local || rt_.album_art_w == 0) && ms_hm.art_url != *rt_.current_loaded_art_url) {
          rt_.cmd(UiHostCmd::FETCH_REMOTE_ART_DEBOUNCED);
        }
      } else if (kind == 4) {
        latter_ui::tools_direct_home() = true;
        *rt_.gui_page = 8;
        *rt_.tools_subpage = 2;
        *rt_.alarm_edit_idx = (s_idx >= 1 && s_idx <= 3) ? s_idx : 0;
      } else if (kind == 5) {
        latter_ui::tools_direct_home() = true;
        *rt_.gui_page = 8;
        *rt_.tools_subpage = 9;
        latter_ui::cal_detail_mode() = 0;
        latter_ui::cal_quick_add_open() = false;
        *rt_.cal_browse_page = 1;
        rt_.cmd(UiHostCmd::FETCH_CALENDAR_BROWSE);
      }
      rt_.schedule_disp();
    };

    if (*rt_.home_subpage == 2) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.home_subpage = 0;
          latter_ui::home_activity_overlay_page() = 0;
          rt_.schedule_disp();
        }
        return;
      }
      if (act_count > 5 && latter_ui::is_right_chevron_zone(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_SCROLL)) {
          latter_ui::home_activity_overlay_page() = (touch.y < latter_ui::scale_y(220)) ? 0 : 1;
          rt_.schedule_disp();
        }
        return;
      }
      const int start_i = latter_ui::home_activity_overlay_page() * 5;
      const int vis_cnt = std::min(5, act_count - start_i);
      int hit_row = -1;
      const int ov_half_w = latter_ui::layout().list_card_w / 2;
      if (std::abs(touch.x - latter_ui::SCREEN_CX) <= ov_half_w && vis_cnt > 0) {
        for (int r = 0; r < vis_cnt; r++) {
          const int cy_r = latter_ui::get_activity_overlay_card_y(vis_cnt, r);
          if (touch.y >= cy_r - latter_ui::scale_y(28) && touch.y <= cy_r + latter_ui::scale_y(32)) {
            hit_row = r;
            break;
          }
        }
      }
      if (hit_row >= 0 && latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        handle_activity_item_action(start_i + hit_row, (touch.x >= latter_ui::SCREEN_CX + ov_half_w - latter_ui::scale_x(56)));
        return;
      }
      if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_DISMISS)) {
        *rt_.home_subpage = 0;
        latter_ui::home_activity_overlay_page() = 0;
        rt_.schedule_disp();
      }
      return;
    }

    const bool greet_blocking = rt_.sw_state(UiSwitchId::HOME_WAKE_GREETING) &&
                                latter_ui::wake_greeting_latched() &&
                                (now_touch >= latter_ui::wake_greeting_start_ms()) &&
                                (now_touch - latter_ui::wake_greeting_start_ms() < 10500U);
    const auto &act_ly = latter_ui::layout();
    const int act_weather_h = latter_ui::home_weather_text_h();
    if (!greet_blocking && act_count > 0 && act_ly.hit_home_activity(touch.x, touch.y, act_weather_h)) {
      if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        const int act_right = latter_ui::SCREEN_CX + act_ly.home_activity_w() / 2;
        const bool act_drop = act_ly.home_activity_has_drop(act_count);
        if (act_drop && touch.x >= act_right - latter_ui::scale_x(50)) {
          latter_ui::home_activity_overlay_page() = 0;
          *rt_.home_subpage = 2;
          rt_.schedule_disp();
          return;
        }
        const latter_ui::HomeActivityView act_view = latter_ui::resolve_home_activity_view(
            act_items, act_count, now_touch, act_ly.home_activity_two_line());
        int act_item = (act_view.top >= 0) ? act_view.top : 0;
        if (act_view.bottom >= 0 && touch.y >= act_ly.home_activity_cy(act_weather_h)) act_item = act_view.bottom;
        handle_activity_item_action(act_item, touch.x >= act_right - latter_ui::scale_x(act_drop ? 106 : 54));
      }
      return;
    }
    return;
  } else if (*rt_.gui_page == 2) {
    const auto &ly = latter_ui::layout();
    if (*rt_.music_overlay_mode == 0) {
      if (ly.hit_music_header(touch.x, touch.y)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          latter_ui::music_scroll_tap_lockout_until_ms() = now_touch + 280U;
          *rt_.music_direct_nav = false;
          *rt_.music_overlay_mode = 5;
          rt_.schedule_disp();
        }
        return;
      }

      std::string overlay_mode = rt_.select_opt(UiSelectId::MUSIC_OVERLAY_STYLE);
      if (overlay_mode.empty()) overlay_mode = "Art Only";
      const bool hud_enabled = (overlay_mode != "Always Visible");

      if (!*rt_.music_hud_active && overlay_mode == "Art + Metadata" && ly.hit_music_metadata(touch.x, touch.y)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          latter_ui::music_scroll_tap_lockout_until_ms() = now_touch + 280U;
          *rt_.music_overlay_mode = 6;
          rt_.schedule_disp();
        }
        return;
      }

      if (hud_enabled && !*rt_.music_hud_active) {
        *rt_.music_hud_active = true;
        *rt_.music_hud_timer = now_touch;
        hud_just_woken_ms_ = now_touch;
        *rt_.hud_wake_finger_released = false;
        rt_.schedule_disp();
        return;
      }

      if (hud_enabled && (!*rt_.hud_wake_finger_released || (now_touch - hud_just_woken_ms_ < 50U))) {
        return;
      }

      if (ly.hit_music_play_pause(touch.x, touch.y)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          const auto &ms_pp = latter_ui::media_state();
          const bool is_playing = latter_ui::is_music_playing_effective(!ms_pp.is_local, rt_.local_mp_playing, ms_pp.state, now_touch);
          *rt_.music_action_feedback = is_playing ? "Pausing..." : "Playing...";
          *rt_.music_feedback_timer = now_touch;
          *rt_.music_hud_timer = now_touch;
          rt_.cmd(UiHostCmd::TOGGLE_HA_MUSIC);
        }
        return;
      }

      if (rt_.sw_state(UiSwitchId::MUSIC_SHOW_SPEAKER_SHORTCUT) && ly.hit_music_spk_shortcut(touch.x, touch.y)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          latter_ui::music_scroll_tap_lockout_until_ms() = now_touch + 280U;
          *rt_.selected_category_mode = "speakers";
          *rt_.selected_category_title = "SPEAKERS";
          *rt_.slot_names[0] = "Loading...";
          *rt_.slot_ids[0] = ""; *rt_.slot_types[0] = "text";
          for (int i = 1; i < 5; i++) { *rt_.slot_names[i] = ""; *rt_.slot_ids[i] = ""; }
          *rt_.music_browse_page = 1;
          *rt_.music_browse_total_pages = 1;
          *rt_.music_direct_nav = true;
          *rt_.music_overlay_mode = 1;
          rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
          rt_.schedule_disp();
        }
        return;
      }

      if (rt_.sw_state(UiSwitchId::MUSIC_SHOW_LIBRARY_SHORTCUT) && ly.hit_music_lib_shortcut(touch.x, touch.y)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          latter_ui::music_scroll_tap_lockout_until_ms() = now_touch + 280U;
          *rt_.music_category_page = 1;
          *rt_.menu_scroll_active = false;
          *rt_.music_direct_nav = true;
          *rt_.music_overlay_mode = 2;
          rt_.schedule_disp();
        }
        return;
      }

      if (ly.hit_music_vol_up(touch.x, touch.y)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
          *rt_.music_hud_timer = now_touch;
          int v_step = static_cast<int>(rt_.num_val(UiNumberId::VOLUME_STEP_SIZE));
          if (v_step <= 0) v_step = 5;
          if (!latter_ui::media_state().is_local) {
            if (latter_ui::remote_vol_pct() >= 0) {
              const int v_next = std::min(100, latter_ui::remote_vol_pct() + v_step);
              latter_ui::remote_vol_pct() = v_next;
              latter_ui::last_local_vol_tap_ms() = now_touch;
              *rt_.music_action_feedback = "Volume: " + std::to_string(v_next) + "%";
            } else {
              *rt_.music_action_feedback = "Volume +";
            }
          } else {
            const int v_next = std::min(100, static_cast<int>(std::round(rt_.local_mp_volume * 100.0f)) + v_step);
            *rt_.music_action_feedback = "Volume: " + std::to_string(v_next) + "%";
          }
          *rt_.music_feedback_timer = now_touch;
          rt_.cmd(UiHostCmd::MUSIC_VOL_UP);
          rt_.schedule_disp();
        }
        return;
      }

      if (ly.hit_music_vol_down(touch.x, touch.y)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
          *rt_.music_hud_timer = now_touch;
          int v_step = static_cast<int>(rt_.num_val(UiNumberId::VOLUME_STEP_SIZE));
          if (v_step <= 0) v_step = 5;
          if (!latter_ui::media_state().is_local) {
            if (latter_ui::remote_vol_pct() >= 0) {
              const int v_next = std::max(0, latter_ui::remote_vol_pct() - v_step);
              latter_ui::remote_vol_pct() = v_next;
              latter_ui::last_local_vol_tap_ms() = now_touch;
              *rt_.music_action_feedback = "Volume: " + std::to_string(v_next) + "%";
            } else {
              *rt_.music_action_feedback = "Volume -";
            }
          } else {
            const int v_next = std::max(0, static_cast<int>(std::round(rt_.local_mp_volume * 100.0f)) - v_step);
            *rt_.music_action_feedback = "Volume: " + std::to_string(v_next) + "%";
          }
          *rt_.music_feedback_timer = now_touch;
          rt_.cmd(UiHostCmd::MUSIC_VOL_DOWN);
          rt_.schedule_disp();
        }
        return;
      }

      if (ly.hit_music_next(touch.x, touch.y)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          const bool is_speech = (*rt_.active_playback_media_type == "podcast" ||
                                  *rt_.active_playback_media_type == "podcast_episode" ||
                                  *rt_.active_playback_media_type == "audiobook");
          if (is_speech) {
            rt_.cmd(UiHostCmd::MUSIC_SEEK_RELATIVE, 1);
          } else {
            *rt_.music_action_feedback = "Next Track >>";
            rt_.cmd(UiHostCmd::MUSIC_NEXT_TRACK);
          }
          *rt_.music_feedback_timer = now_touch;
          *rt_.music_hud_timer = now_touch;
          rt_.schedule_disp();
        }
        return;
      }

      if (ly.hit_music_prev(touch.x, touch.y)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          const bool is_speech = (*rt_.active_playback_media_type == "podcast" ||
                                  *rt_.active_playback_media_type == "podcast_episode" ||
                                  *rt_.active_playback_media_type == "audiobook");
          if (is_speech) {
            rt_.cmd(UiHostCmd::MUSIC_SEEK_RELATIVE, -1);
          } else {
            *rt_.music_action_feedback = "<< Prev Track";
            rt_.cmd(UiHostCmd::MUSIC_PREV_TRACK);
          }
          *rt_.music_feedback_timer = now_touch;
          *rt_.music_hud_timer = now_touch;
          rt_.schedule_disp();
        }
        return;
      }

      if (ly.hit_music_metadata(touch.x, touch.y)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.music_overlay_mode = 6;
          rt_.schedule_disp();
        }
        return;
      }

      if (hud_enabled && *rt_.music_hud_active) {
        if (std::abs(touch.x - ly.cx) <= latter_ui::scale_x(75) &&
            (touch.y < latter_ui::scale_y(150) ||
             (touch.y > latter_ui::scale_y(274) && touch.y < latter_ui::scale_y(400)))) {
          if (now_touch - *rt_.music_hud_timer >= latter_ui::TOUCH_GUARD_DISMISS) {
            if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_DISMISS)) {
              *rt_.music_hud_active = false;
              rt_.schedule_disp();
            }
          }
        }
        return;
      }
    } else if (*rt_.music_overlay_mode == 5) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.music_overlay_mode = 0;
          *rt_.music_hud_active = true;
          *rt_.music_hud_timer = now_touch;
          *rt_.system_menu_mode = 0;
          rt_.schedule_disp();
        }
        return;
      }
      const int m_x0 = ly.cx - ly.list_card_w / 2 - 8;
      const int m_x1 = ly.cx + ly.list_card_w / 2 + 8;
      if (touch.y >= latter_ui::scale_y(96) && touch.y < latter_ui::scale_y(160) && touch.x >= m_x0 && touch.x <= m_x1) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.selected_category_mode = "speakers";
          *rt_.selected_category_title = "SPEAKERS";
          *rt_.music_browse_page = 1;
          *rt_.music_direct_nav = false;
          *rt_.music_overlay_mode = 1;
          rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
          rt_.schedule_disp();
        }
        return;
      }
      if (touch.y >= latter_ui::scale_y(160) && touch.y < latter_ui::scale_y(223) && touch.x >= m_x0 && touch.x <= m_x1) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.music_category_page = 1;
          *rt_.menu_scroll_active = false;
          *rt_.music_direct_nav = false;
          *rt_.music_overlay_mode = 2;
          rt_.schedule_disp();
        }
        return;
      }
      if (touch.y >= latter_ui::scale_y(223) && touch.y < latter_ui::scale_y(286) && touch.x >= m_x0 && touch.x <= m_x1) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.selected_category_mode = "active_queue";
          *rt_.selected_category_title = "QUEUE";
          *rt_.drilldown_parent_mode = "music_hub";
          *rt_.slot_names[0] = "Loading queue...";
          *rt_.slot_ids[0] = ""; *rt_.slot_types[0] = "text";
          for (int i = 1; i < 5; i++) { *rt_.slot_names[i] = ""; *rt_.slot_ids[i] = ""; }
          *rt_.music_browse_page = 1;
          *rt_.music_browse_total_pages = 1;
          *rt_.music_overlay_mode = 3;
          rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
          rt_.schedule_disp();
        }
        return;
      }
      if (touch.y >= latter_ui::scale_y(286) && touch.y <= latter_ui::scale_y(368) && touch.x >= m_x0 && touch.x <= m_x1) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.gui_page = 4;
          *rt_.system_menu_mode = 9;
          *rt_.audio_menu_parent_page = 2;
          *rt_.menu_scroll_active = false;
          rt_.schedule_disp();
        }
        return;
      }
    } else if (*rt_.music_overlay_mode == 1) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          if (*rt_.music_direct_nav) { *rt_.music_overlay_mode = 0; *rt_.music_direct_nav = false; }
          else { *rt_.music_overlay_mode = 5; }
          *rt_.music_hud_active = true;
          *rt_.music_hud_timer = now_touch;
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.music_overlay_mode == 2) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          if (*rt_.music_direct_nav) { *rt_.music_overlay_mode = 0; *rt_.music_direct_nav = false; }
          else { *rt_.music_overlay_mode = 5; }
          *rt_.music_category_page = 1;
          *rt_.menu_scroll_active = false;
          *rt_.music_hud_active = true;
          *rt_.music_hud_timer = now_touch;
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.music_overlay_mode == 3) {
      if (*rt_.selected_category_mode == "active_queue" &&
          ((hit != nullptr && hit->widget_id == WidgetId::MUSIC_QUEUE_CLEAR) ||
           (touch.y >= latter_ui::scale_y(24) && touch.y <= latter_ui::scale_y(74) &&
            touch.x >= latter_ui::SCREEN_CX + latter_ui::scale_x(43) &&
            touch.x <= latter_ui::SCREEN_CX + latter_ui::scale_x(147)))) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          rt_.cmd(UiHostCmd::CLEAR_HA_MUSIC_QUEUE);
          *rt_.slot_names[0] = "Queue Cleared";
          *rt_.slot_ids[0] = ""; *rt_.slot_types[0] = "text";
          for (int i = 1; i < 5; i++) { *rt_.slot_names[i] = ""; *rt_.slot_ids[i] = ""; }
          *rt_.music_browse_total_pages = 1;
          rt_.schedule_disp();
        }
        return;
      }
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          if (*rt_.selected_category_mode == "active_queue" && *rt_.drilldown_parent_mode == "music_hub") {
            *rt_.drilldown_parent_mode = "";
            *rt_.music_overlay_mode = 5;
            rt_.schedule_disp();
            return;
          } else if (*rt_.selected_category_mode == "current_album_tracks" || *rt_.selected_category_mode == "active_queue" ||
                     (*rt_.selected_category_mode == "artist_albums" && *rt_.drilldown_parent_mode == "track_inspector")) {
            *rt_.drilldown_parent_mode = "";
            *rt_.music_overlay_mode = 6;
            rt_.schedule_disp();
            return;
          } else if (*rt_.selected_category_mode == "album_tracks" || *rt_.selected_category_mode == "podcast_episodes") {
            *rt_.selected_category_mode = rt_.drilldown_parent_mode->empty() ? "album" : *rt_.drilldown_parent_mode;
            *rt_.selected_category_title = (*rt_.selected_category_mode == "podcast")
                                               ? "PODCASTS"
                                               : ((*rt_.selected_category_mode == "artist_albums") ? *rt_.drilldown_artist_name : "ALBUMS");
            *rt_.music_browse_page = std::max(1, *rt_.drilldown_album_page);
            rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
          } else if (*rt_.selected_category_mode == "artist_albums") {
            *rt_.selected_category_mode = "artist";
            *rt_.selected_category_title = "ARTISTS";
            *rt_.music_browse_page = std::max(1, *rt_.drilldown_artist_page);
            rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
          } else {
            *rt_.drilldown_artist_page = 1;
            *rt_.drilldown_album_page = 1;
            *rt_.menu_scroll_active = false;
            *rt_.music_overlay_mode = 2;
            rt_.schedule_disp();
          }
        }
      }
      return;
    } else if (*rt_.music_overlay_mode == 4) {
      const bool is_current = (*rt_.target_speaker_id == *rt_.active_speaker_entity);
      const bool in_sheet_x = (std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(143));
      if (!is_current) {
        if (in_sheet_x && touch.y >= latter_ui::scale_y(104) && touch.y < latter_ui::scale_y(166)) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) rt_.cmd(UiHostCmd::SPEAKER_TRANSFER_TO_TARGET);
          return;
        } else if (in_sheet_x && touch.y >= latter_ui::scale_y(166) && touch.y < latter_ui::scale_y(226)) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) rt_.cmd(UiHostCmd::SPEAKER_CONTROL_TARGET);
          return;
        } else if (in_sheet_x && touch.y >= latter_ui::scale_y(226) && touch.y < latter_ui::scale_y(286)) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) rt_.cmd(UiHostCmd::SPEAKER_JOIN_TARGET);
          return;
        } else if (in_sheet_x && touch.y >= latter_ui::scale_y(286) && touch.y <= latter_ui::scale_y(365)) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) rt_.cmd(UiHostCmd::SPEAKER_UNJOIN_TARGET);
          return;
        }
      } else {
        const auto &ms_sh = latter_ui::media_state();
        const bool is_remote = !ms_sh.is_local && !ms_sh.local_speaker.empty();
        if (is_remote) {
          if (in_sheet_x && touch.y >= latter_ui::scale_y(114) && touch.y < latter_ui::scale_y(180)) {
            if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
              *rt_.target_speaker_id = ms_sh.local_speaker;
              *rt_.target_speaker_name = (rt_.friendly_name != nullptr) ? rt_.friendly_name : "This Device";
              rt_.cmd(UiHostCmd::SPEAKER_TRANSFER_TO_TARGET);
            }
            return;
          } else if (in_sheet_x && touch.y >= latter_ui::scale_y(180) && touch.y < latter_ui::scale_y(242)) {
            if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) rt_.cmd(UiHostCmd::SPEAKER_UNJOIN_TARGET);
            return;
          } else if (in_sheet_x && touch.y >= latter_ui::scale_y(242) && touch.y <= latter_ui::scale_y(322)) {
            if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) rt_.cmd(UiHostCmd::STOP_HA_MUSIC);
            return;
          }
        } else {
          if (in_sheet_x && touch.y >= latter_ui::scale_y(130) && touch.y < latter_ui::scale_y(200)) {
            if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) rt_.cmd(UiHostCmd::SPEAKER_UNJOIN_TARGET);
            return;
          } else if (in_sheet_x && touch.y >= latter_ui::scale_y(200) && touch.y <= latter_ui::scale_y(285)) {
            if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) rt_.cmd(UiHostCmd::STOP_HA_MUSIC);
            return;
          }
        }
      }
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.music_overlay_mode = 1;
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.music_overlay_mode == 6) {
      const auto &ms_in = latter_ui::media_state();
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.music_overlay_mode = 0;
          *rt_.music_hud_active = true;
          *rt_.music_hud_timer = now_touch;
          rt_.schedule_disp();
        }
        return;
      }
      if (touch.y >= latter_ui::scale_y(104) && touch.y <= latter_ui::scale_y(178) &&
          std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(148)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          const bool is_rem = !ms_in.is_local;
          const std::string alb = (is_rem || rt_.track_album.empty()) ? ms_in.album : rt_.track_album;
          *rt_.selected_category_mode = "current_album_tracks";
          *rt_.selected_category_title = alb.empty() ? "ALBUM TRACKS" : alb;
          *rt_.drilldown_album_id = alb;
          *rt_.music_browse_page = 1;
          *rt_.music_browse_total_pages = 1;
          *rt_.slot_names[0] = "Loading album tracks...";
          *rt_.slot_ids[0] = "";
          for (int i = 1; i < 5; i++) *rt_.slot_names[i] = "";
          *rt_.music_overlay_mode = 3;
          rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
          rt_.schedule_disp();
        }
        return;
      }
      if (touch.y >= latter_ui::scale_y(210) && touch.y <= latter_ui::scale_y(284)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
          const float dur = ms_in.duration;
          float cur_pos = *rt_.music_pos_base;
          if (*rt_.music_pos_sync_time > 0) {
            cur_pos += static_cast<float>(now_touch - *rt_.music_pos_sync_time) * 0.001f;
          }
          if (touch.x < ly.music_scrub_x0() - 6) {
            *rt_.music_pos_base = std::max(0.0f, cur_pos - 15.0f);
            *rt_.music_pos_sync_time = now_touch;
            rt_.cmd(UiHostCmd::MUSIC_SEEK_ABSOLUTE);
            rt_.schedule_disp();
          } else if (touch.x > ly.music_scrub_x1() + 6) {
            float new_p = cur_pos + 15.0f;
            if (!std::isnan(dur) && dur > 0.0f) new_p = std::min(dur, new_p);
            *rt_.music_pos_base = new_p;
            *rt_.music_pos_sync_time = now_touch;
            rt_.cmd(UiHostCmd::MUSIC_SEEK_ABSOLUTE);
            rt_.schedule_disp();
          } else if (!std::isnan(dur) && dur > 0.0f) {
            *rt_.music_pos_base = ly.music_scrub_frac(touch.x) * dur;
            *rt_.music_pos_sync_time = now_touch;
            rt_.cmd(UiHostCmd::MUSIC_SEEK_ABSOLUTE);
            rt_.schedule_disp();
          }
        }
        return;
      }
      if (touch.y >= latter_ui::scale_y(292) && touch.y < latter_ui::scale_y(412) &&
          std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(138)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          const bool is_bio = (touch.x >= latter_ui::SCREEN_CX);
          const bool is_rem = !ms_in.is_local;
          const std::string t_name = (is_rem || rt_.track_title.empty()) ? ms_in.title : rt_.track_title;
          const std::string ar_name = (is_rem || rt_.track_artist.empty()) ? ms_in.artist : rt_.track_artist;
          const std::string al_name = (is_rem || rt_.track_album.empty()) ? ms_in.album : rt_.track_album;
          *rt_.drilldown_album_id = t_name + "|" + ar_name + "|" + al_name;
          *rt_.selected_category_mode = is_bio ? "bio" : "lyrics";
          *rt_.selected_category_title = is_bio ? "BIO" : "LYRICS";
          *rt_.music_browse_page = 1;
          *rt_.music_browse_total_pages = 1;
          *rt_.slot_names[0] = is_bio ? "Loading artist & album bio..." : "Loading lyrics...";
          for (int i = 1; i < 5; i++) *rt_.slot_names[i] = "";
          *rt_.music_overlay_mode = is_bio ? 8 : 7;
          rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
          rt_.schedule_disp();
        }
        return;
      }
    } else if (*rt_.music_overlay_mode == 7 || *rt_.music_overlay_mode == 8) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.music_overlay_mode = 6;
          rt_.schedule_disp();
        }
        return;
      }
      if (latter_ui::is_right_chevron_zone(touch) && *rt_.music_browse_total_pages > 1) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
          const int step_d = (touch.y < latter_ui::scale_y(220)) ? -1 : 1;
          const int next_pg = std::max(1, std::min(*rt_.music_browse_total_pages, *rt_.music_browse_page + step_d));
          if (next_pg != *rt_.music_browse_page) {
            *rt_.music_browse_page = next_pg;
            rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
            rt_.schedule_disp();
          }
        }
      }
      return;
    }
  } else if (*rt_.gui_page == 3) {
    if (*rt_.active_game == 0) {
      return;
    }
    if (latter_ui::is_back_button(touch)) {
      if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        *rt_.active_game = 0;
        rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
        rt_.schedule_disp();
      }
      return;
    }
    if (hit != nullptr && hit->widget_id == WidgetId::GAME_OVER_CTA) {
      if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        *rt_.marble_touch_x = static_cast<float>(touch.x);
        *rt_.marble_touch_y = static_cast<float>(touch.y);
        *rt_.game_btn_pressed = true;
        rt_.schedule_disp();
      }
      return;
    }
    if (touch.y >= latter_ui::layout().back_y - latter_ui::scale_y(36)) return;
    if (*rt_.active_game == 5) return;
    *rt_.marble_touch_x = static_cast<float>(touch.x);
    *rt_.marble_touch_y = static_cast<float>(touch.y);
    *rt_.game_btn_pressed = true;
    if (*rt_.active_game == 6) {
      rt_.schedule_disp();
    }
  } else if (*rt_.gui_page == 4) {
    if (*rt_.system_menu_mode == 0) {
      return;
    } else if (*rt_.system_menu_mode == 1) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.system_menu_mode = 0;
          *rt_.sleep_menu_page = 1;
          *rt_.menu_scroll_active = false;
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.system_menu_mode == 2) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.system_menu_mode = 0;
          *rt_.display_menu_page = 1;
          *rt_.menu_scroll_active = false;
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.system_menu_mode == 3) {
      const int slot = latter_ui::layout().hit_slot5(touch, 88, 371);
      if (slot >= 0 && slot <= 4 && latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        if (slot == 0) { rt_.cmd(UiHostCmd::TRIGGER_SCREENSAVER_NOW); return; }
        if (slot == 1) {
#if LATTER_HAS_BATTERY
          rt_.cmd(UiHostCmd::START_HIBERNATION_COUNTDOWN);
#else
          rt_.cmd(UiHostCmd::TURN_SCREEN_OFF);
#endif
          return;
        }
        if (slot == 2) { *rt_.system_menu_mode = 5; rt_.schedule_disp(); return; }
        if (slot == 3) { rt_.sw_toggle(UiSwitchId::BLE_AREA_BEACON); rt_.schedule_disp(); return; }
        if (slot == 4) { *rt_.system_menu_mode = 4; rt_.schedule_disp(); return; }
      }
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.system_menu_mode = 0;
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.system_menu_mode == 4) {
      if ((hit != nullptr && hit->widget_id == WidgetId::CONFIRM_CANCEL) ||
          (touch.y >= latter_ui::scale_y(195) && touch.y <= latter_ui::scale_y(285) &&
           touch.x >= latter_ui::SCREEN_CX - latter_ui::scale_x(140) && touch.x < latter_ui::SCREEN_CX - 5)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.system_menu_mode = 3;
          rt_.schedule_disp();
        }
        return;
      }
      if ((hit != nullptr && hit->widget_id == WidgetId::CONFIRM_ACCEPT) ||
          (touch.y >= latter_ui::scale_y(195) && touch.y <= latter_ui::scale_y(285) &&
           touch.x > latter_ui::SCREEN_CX + 5 && touch.x <= latter_ui::SCREEN_CX + latter_ui::scale_x(140))) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          rt_.cmd(UiHostCmd::RESTART_DEVICE);
        }
        return;
      }
    } else if (*rt_.system_menu_mode == 5) {
      if (touch.y >= latter_ui::scale_y(188) && touch.y <= latter_ui::scale_y(264) &&
          std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(143)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          const bool has_update = latter_ui::is_newer_semver(
              rt_.fw_latest_version, (rt_.fw_version != nullptr) ? rt_.fw_version : "0.0.0");
          if (has_update) {
            *rt_.system_menu_mode = 6;
            rt_.schedule_disp();
          } else if (*rt_.update_check_state == 0) {
            *rt_.update_check_state = 1;
            *rt_.update_check_timer = now_touch;
            rt_.cmd(UiHostCmd::CHECK_FIRMWARE_UPDATE);
            rt_.schedule_disp();
          }
        }
        return;
      }
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.update_check_state = 0;
          *rt_.system_menu_mode = 0;
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.system_menu_mode == 6) {
      if ((hit != nullptr && hit->widget_id == WidgetId::CONFIRM_CANCEL) ||
          (touch.y >= latter_ui::scale_y(210) && touch.y <= latter_ui::scale_y(290) &&
           touch.x >= latter_ui::SCREEN_CX - latter_ui::scale_x(140) && touch.x < latter_ui::SCREEN_CX - 5)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.system_menu_mode = 5;
          rt_.schedule_disp();
        }
        return;
      }
      if ((hit != nullptr && hit->widget_id == WidgetId::CONFIRM_ACCEPT) ||
          (touch.y >= latter_ui::scale_y(210) && touch.y <= latter_ui::scale_y(290) &&
           touch.x > latter_ui::SCREEN_CX + 5 && touch.x <= latter_ui::SCREEN_CX + latter_ui::scale_x(140))) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          rt_.cmd(UiHostCmd::TRIGGER_FIRMWARE_UPDATE);
        }
        return;
      }
    } else if (*rt_.system_menu_mode == 7) {
      const int slot_idx = latter_ui::layout().hit_slot5(touch, 88, 371);
      if (slot_idx >= 0 && slot_idx <= 3) {
        if (slot_idx == 0) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
            const float new_vol = latter_ui::step_wrap_range(rt_.local_mp_volume, 0.10f, 0.10f, 1.00f);
            rt_.set_num_val(UiNumberId::SPEAKER_VOLUME, std::round(new_vol * 100.0f));
            rt_.schedule_disp();
          }
          return;
        } else if (slot_idx == 1) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
            const float cur_a_f = rt_.num_val(UiNumberId::ALERT_VOLUME);
            const int cur_a = (std::isnan(cur_a_f) || cur_a_f < 20.0f) ? 70 : static_cast<int>(std::round(cur_a_f));
            const int nxt_a = static_cast<int>(latter_ui::step_wrap_range(static_cast<float>(cur_a), 10.0f, 50.0f, 100.0f));
            rt_.set_num_val(UiNumberId::ALERT_VOLUME, static_cast<float>(nxt_a));
            rt_.schedule_disp();
          }
          return;
        } else if (slot_idx == 2) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
            const int cur_sz = static_cast<int>(std::round(rt_.num_val(UiNumberId::VOLUME_STEP_SIZE)));
            const int nxt_sz = (cur_sz < 5) ? 5 : ((cur_sz < 10) ? 10 : 2);
            rt_.set_num_val(UiNumberId::VOLUME_STEP_SIZE, static_cast<float>(nxt_sz));
            rt_.schedule_disp();
          }
          return;
        } else if (slot_idx == 3) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
            rt_.sw_toggle(UiSwitchId::GAME_SOUND);
            rt_.schedule_disp();
          }
          return;
        }
      }
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.system_menu_mode = 0;
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.system_menu_mode == 8) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.system_menu_mode = 0;
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.system_menu_mode == 9) {
      const int slot_idx = latter_ui::layout().hit_slot5(touch, 88, 371);
      if (slot_idx >= 0 && slot_idx <= 4 && latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        if (slot_idx == 0) {
          rt_.set_select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE,
                             latter_ui::cycle_option(rt_.select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE), {"Full Screen", "Letterbox", "Standard"}));
        } else if (slot_idx == 1) {
          rt_.set_select_opt(UiSelectId::MUSIC_OVERLAY_STYLE,
                             latter_ui::cycle_option(rt_.select_opt(UiSelectId::MUSIC_OVERLAY_STYLE), {"Art Only", "Art + Metadata", "Always Visible"}));
        } else if (slot_idx == 2) {
          rt_.set_select_opt(UiSelectId::MUSIC_ALBUM_SORT_ORDER,
                             latter_ui::cycle_option(rt_.select_opt(UiSelectId::MUSIC_ALBUM_SORT_ORDER), {"Alphabetical", "Newest on Top", "Oldest on Top"}));
        } else if (slot_idx == 3) {
          rt_.set_select_opt(UiSelectId::MUSIC_ARTIST_ALBUM_SORT_ORDER,
                             latter_ui::cycle_option(rt_.select_opt(UiSelectId::MUSIC_ARTIST_ALBUM_SORT_ORDER), {"Oldest on Top", "Newest on Top", "Alphabetical"}));
        } else if (slot_idx == 4) {
          rt_.set_select_opt(UiSelectId::MUSIC_PODCAST_SORT_ORDER,
                             latter_ui::cycle_option(rt_.select_opt(UiSelectId::MUSIC_PODCAST_SORT_ORDER), {"Newest on Top", "Oldest on Top"}));
        }
        rt_.schedule_disp();
        return;
      }
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          if (*rt_.audio_menu_parent_page == 2) {
            *rt_.gui_page = 2;
            *rt_.music_overlay_mode = 5;
            *rt_.audio_menu_parent_page = 4;
            *rt_.system_menu_mode = 0;
          } else {
            *rt_.system_menu_mode = 0;
          }
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.system_menu_mode == 11) {
      const int slot = latter_ui::layout().hit_slot5(touch, 88, 371);
      if (slot >= 0 && slot <= 4 && latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        if (slot == 0) {
          rt_.set_select_opt(UiSelectId::VA_WAKE_WORD_MODE,
                             latter_ui::cycle_option(rt_.select_opt(UiSelectId::VA_WAKE_WORD_MODE), {"Docked Only", "Always On", "Disabled"}));
          rt_.schedule_disp();
          return;
        } else if (slot == 1) {
          if (rt_.select_opt(UiSelectId::VA_WAKE_WORD_MODE) != "Disabled") {
            rt_.cmd(UiHostCmd::CYCLE_PRIMARY_WAKE_WORD);
            rt_.schedule_disp();
          }
          return;
        } else if (slot == 2) {
          if (rt_.select_opt(UiSelectId::VA_WAKE_WORD_MODE) != "Disabled") {
            *rt_.system_menu_mode = 12;
            rt_.schedule_disp();
          }
          return;
        } else if (slot == 3) {
          rt_.set_select_opt(UiSelectId::VA_VISUAL_PROFILE,
                             latter_ui::cycle_option(rt_.select_opt(UiSelectId::VA_VISUAL_PROFILE), {"Nebula", "Solaris", "Rings", "Light Bar", "Dot Wave"}));
          rt_.schedule_disp();
          return;
        } else if (slot == 4) {
          rt_.sw_toggle(UiSwitchId::VA_WAKE_SCREEN);
          rt_.schedule_disp();
          return;
        }
      }
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.system_menu_mode = 0;
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.system_menu_mode == 12) {
      const int slot = latter_ui::layout().hit_slot5(touch, 88, 371);
      if (slot >= 0 && slot <= 4 && latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        static constexpr UiSelectId kSensIds[5] = {
            UiSelectId::WW_SENS_OKAY_NABU,
            UiSelectId::WW_SENS_HEY_LUNA,
            UiSelectId::WW_SENS_HEY_JARVIS,
            UiSelectId::WW_SENS_HEY_MYCROFT,
            UiSelectId::WW_SENS_ALEXA,
        };
        rt_.set_select_opt(kSensIds[slot],
                           latter_ui::cycle_option(rt_.select_opt(kSensIds[slot]),
                                                   {"Slightly sensitive", "Moderately sensitive", "Very sensitive"}));
        rt_.schedule_disp();
        return;
      }
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.system_menu_mode = 11;
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.system_menu_mode == 13) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.system_menu_mode = 2;
          rt_.cmd(UiHostCmd::APPLY_THEME_PRESET);
        }
        return;
      }
      if ((hit != nullptr && hit->widget_id == WidgetId::COLOR_WHEEL_SAVE) ||
          (touch.y >= latter_ui::scale_y(352) && touch.y <= latter_ui::scale_y(398) &&
           std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(123))) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          rt_.set_select_opt(UiSelectId::THEME_PALETTE, "Custom");
          rt_.cmd(UiHostCmd::SAVE_CUSTOM_COLOR_TO_LATTER);
          *rt_.system_menu_mode = 2;
          rt_.schedule_disp();
        }
        return;
      }
      float hue = 0.0f, sat = 1.0f;
      if (ui::hsv_wheel_pick(touch.x, touch.y, latter_ui::SCREEN_CX, latter_ui::scale_y(180), 156, 56, hue, sat)) {
        latter_ui::color_wheel_hue_deg() = hue;
        latter_ui::color_wheel_sat() = sat;
        const esphome::Color c = latter_ui::hsv_to_rgb(hue / 360.0f, sat, 1.0f);
        *rt_.custom_theme_rgb = (static_cast<uint32_t>(c.r) << 16) |
                                (static_cast<uint32_t>(c.g) << 8) |
                                static_cast<uint32_t>(c.b);
        latter_ui::apply_custom_rgb_harmony(*rt_.custom_theme_rgb, *rt_.theme_primary, *rt_.theme_secondary);
        rt_.schedule_disp();
      }
      return;
    }
  } else if (*rt_.gui_page == 5) {
    return;
  } else if (*rt_.gui_page == 6) {
    if (*rt_.area_subpage > 0 && latter_ui::is_back_button(touch)) {
      if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        if (*rt_.area_subpage == 1 && rt_.active_area_id->empty()) {
          *rt_.area_subpage = 0;
          *rt_.gui_page = 1;
          *rt_.home_subpage = 0;
        } else {
          *rt_.area_subpage = 0;
        }
        rt_.schedule_disp();
      }
      return;
    }
    if (touch.y <= latter_ui::scale_y(88) && std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(143)) {
      if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        if (*rt_.area_subpage == 1) {
          if (rt_.active_area_id->empty()) {
            *rt_.area_subpage = 0;
            *rt_.gui_page = 1;
            *rt_.home_subpage = 0;
          } else {
            *rt_.area_subpage = 0;
          }
        } else {
          *rt_.area_subpage = 1;
          *rt_.area_list_page = 1;
          if (rt_.area_list_names[0]->empty()) {
            *rt_.area_list_names[0] = "Loading areas...";
            *rt_.area_list_ids[0] = "";
          }
          rt_.cmd(UiHostCmd::FETCH_AREA_LIST_BROWSE);
        }
        rt_.schedule_disp();
      }
      return;
    }
    return;
  } else if (*rt_.gui_page == 7) {
    if (*rt_.remote_subpage == 1) {
      if (latter_ui::is_back_button(touch) || touch.y <= latter_ui::scale_y(72)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.remote_action_feedback = "";
          *rt_.remote_subpage = 0;
          rt_.schedule_disp();
        }
        return;
      }
      int valid_slots[latter_ui::MAX_REMOTE_SLOTS] = {0};
      const int valid_count = latter_ui::collect_configured_remote_slots(valid_slots);
      const int total_pages = std::max(1, (valid_count + 3) / 4);
      if (total_pages > 1 && latter_ui::is_right_chevron_zone(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
          const int step_d = (touch.y < latter_ui::scale_y(220)) ? -1 : 1;
          const int next_pg = std::max(0, std::min(total_pages - 1, latter_ui::remote_control_page() + step_d));
          if (next_pg != latter_ui::remote_control_page()) {
            latter_ui::remote_control_page() = next_pg;
            rt_.schedule_disp();
          }
        }
        return;
      }
      return;
    }

    const int act_slot = latter_ui::resolve_active_remote_slot(*rt_.active_remote_slot);
    *rt_.active_remote_slot = act_slot;

    if (std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(130) && touch.y <= latter_ui::scale_y(85)) {
      if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        *rt_.remote_action_feedback = "";
        *rt_.remote_subpage = 1;
        rt_.schedule_disp();
      }
      return;
    }

    const int rem_cy = LATTER_ROUND_DISPLAY ? latter_ui::SCREEN_CY : (latter_ui::SCREEN_CY + latter_ui::scale_y(8));
    const int rem_l_fx = LATTER_ROUND_DISPLAY ? 64 : ((latter_ui::SCREEN_W < 380) ? 34 : ((latter_ui::SCREEN_W > 520) ? 104 : ((latter_ui::SCREEN_W >= 480) ? 44 : 48)));
    const int rem_r_fx = latter_ui::SCREEN_W - rem_l_fx;
    const int rem_dy = LATTER_ROUND_DISPLAY ? 99 : ((latter_ui::SCREEN_W < 380) ? 128 : ((latter_ui::SCREEN_W > 520) ? 196 : ((latter_ui::SCREEN_W >= 480) ? 148 : 116)));
    const int rem_top_fy = rem_cy - rem_dy;
    const int rem_btm_fy = rem_cy + rem_dy;
    const int ok_hit_r = (latter_ui::SCREEN_W < 380) ? 46 : ((latter_ui::SCREEN_W > 520) ? 86 : ((!LATTER_ROUND_DISPLAY && latter_ui::SCREEN_W >= 480) ? 60 : 52));
    const int flank_hit_r2 = latter_ui::scale_s(38) * latter_ui::scale_s(38);

    const int rdx = touch.x - latter_ui::SCREEN_CX;
    const int rdy = touch.y - rem_cy;
    const int dist_sq = rdx * rdx + rdy * rdy;

    if (dist_sq <= (ok_hit_r * ok_hit_r)) {
      if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        rt_.cmd(UiHostCmd::REMOTE_SEND_KEY, 4, act_slot);
        rt_.schedule_disp();
      }
      return;
    }
    if ((touch.x - rem_l_fx) * (touch.x - rem_l_fx) + (touch.y - rem_top_fy) * (touch.y - rem_top_fy) <= flank_hit_r2) {
      if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        rt_.cmd(UiHostCmd::REMOTE_SEND_KEY, 6, act_slot);
        rt_.schedule_disp();
      }
      return;
    }
    if ((touch.x - rem_r_fx) * (touch.x - rem_r_fx) + (touch.y - rem_top_fy) * (touch.y - rem_top_fy) <= flank_hit_r2) {
      if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
        rt_.cmd(UiHostCmd::REMOTE_SEND_KEY, 7, act_slot);
        rt_.schedule_disp();
      }
      return;
    }
    if ((touch.x - rem_l_fx) * (touch.x - rem_l_fx) + (touch.y - rem_btm_fy) * (touch.y - rem_btm_fy) <= flank_hit_r2) {
      if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        rt_.cmd(UiHostCmd::REMOTE_SEND_KEY, 5, act_slot);
        rt_.schedule_disp();
      }
      return;
    }
    if ((touch.x - rem_r_fx) * (touch.x - rem_r_fx) + (touch.y - rem_btm_fy) * (touch.y - rem_btm_fy) <= flank_hit_r2) {
      if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
        rt_.cmd(UiHostCmd::REMOTE_SEND_KEY, 8, act_slot);
        rt_.schedule_disp();
      }
      return;
    }

    const int max_dpad_r = (latter_ui::SCREEN_W < 380) ? 138 : ((latter_ui::SCREEN_W > 520) ? 252 : ((!LATTER_ROUND_DISPLAY && latter_ui::SCREEN_W >= 480) ? 192 : 158));
    if (dist_sq > (ok_hit_r * ok_hit_r) && dist_sq <= (max_dpad_r * max_dpad_r)) {
      if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
        const int k_dir = (std::abs(rdy) >= std::abs(rdx)) ? ((rdy < 0) ? 0 : 1) : ((rdx < 0) ? 2 : 3);
        rt_.cmd(UiHostCmd::REMOTE_SEND_KEY, k_dir, act_slot);
        rt_.schedule_disp();
      }
      return;
    }
  } else if (*rt_.gui_page == 8) {
    if (*rt_.tools_subpage == 0) {
      return;
    } else if (*rt_.tools_subpage == 1) {
      auto *slots = latter_ui::timer_slots();
      int &view_idx = latter_ui::timer_view_slot();

      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          if (latter_ui::tools_direct_home()) {
            latter_ui::tools_direct_home() = false;
            view_idx = 0;
            *rt_.tools_subpage = 0;
            *rt_.gui_page = 1;
            *rt_.home_subpage = 0;
          } else if (view_idx > 0) {
            view_idx = 0;
          } else {
            *rt_.tools_subpage = 0;
          }
          rt_.schedule_disp();
        }
        return;
      }

      if (view_idx == 0) {
        const int card_i = latter_ui::layout().hit_tool_card3(touch.x, touch.y);
        if (card_i >= 0) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
            auto &s = slots[card_i];
            if (touch.x >= latter_ui::layout().tool_card3_div_x()) {
              if (!s.running && !s.paused) {
                s.running = true;
                s.paused = false;
                s.remaining_sec = s.duration_sec;
                s.target_ms = now_touch + static_cast<uint32_t>(s.duration_sec) * 1000U;
              } else if (s.running) {
                s.running = false;
                s.paused = true;
                s.remaining_sec = (s.target_ms > now_touch)
                                      ? static_cast<int>((s.target_ms - now_touch + 999U) / 1000U)
                                      : 0;
              } else {
                s.running = true;
                s.paused = false;
                s.target_ms = now_touch + static_cast<uint32_t>(s.remaining_sec) * 1000U;
              }
              latter_ui::sync_multi_timer_globals(
                  *rt_.timer_running, *rt_.timer_paused, *rt_.timer_remaining_sec,
                  *rt_.timer_duration_sec, *rt_.timer_target_ms);
              rt_.apply_power_policy();
            } else {
              view_idx = card_i + 1;
            }
            rt_.schedule_disp();
          }
        }
        return;
      }

      const int s_idx = (view_idx >= 1 && view_idx <= 3) ? (view_idx - 1) : 0;
      auto &s = slots[s_idx];

      if (touch.y >= latter_ui::scale_y(218) && touch.y <= latter_ui::scale_y(286) &&
          std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(103)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          if (!s.running && !s.paused) {
            s.running = true;
            s.paused = false;
            s.remaining_sec = s.duration_sec;
            s.target_ms = now_touch + static_cast<uint32_t>(s.duration_sec) * 1000U;
          } else if (s.running) {
            s.running = false;
            s.paused = true;
            s.remaining_sec = (s.target_ms > now_touch)
                                  ? static_cast<int>((s.target_ms - now_touch + 999U) / 1000U)
                                  : 0;
          } else {
            s.running = true;
            s.paused = false;
            s.target_ms = now_touch + static_cast<uint32_t>(s.remaining_sec) * 1000U;
          }
          latter_ui::sync_multi_timer_globals(
              *rt_.timer_running, *rt_.timer_paused, *rt_.timer_remaining_sec,
              *rt_.timer_duration_sec, *rt_.timer_target_ms);
          rt_.apply_power_policy();
          rt_.schedule_disp();
        }
        return;
      }

      if ((hit != nullptr && hit->widget_id >= WidgetId::STEPPER_BASE && hit->widget_id < WidgetId::STEPPER_BASE + 4) ||
          (touch.y >= latter_ui::scale_y(290) && touch.y <= latter_ui::scale_y(376))) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
          if (!s.running && !s.paused) {
            const int st_col = (hit != nullptr && hit->widget_id >= WidgetId::STEPPER_BASE && hit->widget_id < WidgetId::STEPPER_BASE + 4)
                                   ? static_cast<int>(hit->widget_id - WidgetId::STEPPER_BASE)
                                   : latter_ui::hit_4col_stepper(touch.x);
            if (st_col == 0) s.duration_sec = std::clamp(s.duration_sec - 300, 60, 3600);
            else if (st_col == 1) s.duration_sec = std::clamp(s.duration_sec - 60, 60, 3600);
            else if (st_col == 2) s.duration_sec = std::clamp(s.duration_sec + 60, 60, 3600);
            else if (st_col == 3) s.duration_sec = std::clamp(s.duration_sec + 300, 60, 3600);
            s.remaining_sec = s.duration_sec;
            latter_ui::sync_multi_timer_globals(
                *rt_.timer_running, *rt_.timer_paused, *rt_.timer_remaining_sec,
                *rt_.timer_duration_sec, *rt_.timer_target_ms);
            rt_.apply_power_policy();
            rt_.schedule_disp();
            return;
          } else if (std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(85)) {
            s.running = false;
            s.paused = false;
            s.remaining_sec = s.duration_sec;
            latter_ui::sync_multi_timer_globals(
                *rt_.timer_running, *rt_.timer_paused, *rt_.timer_remaining_sec,
                *rt_.timer_duration_sec, *rt_.timer_target_ms);
            rt_.apply_power_policy();
            rt_.schedule_disp();
            return;
          }
        }
      }

      if (touch.y >= latter_ui::scale_y(110) && touch.y <= latter_ui::scale_y(175) &&
          std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(93)) {
        if (!s.running && !s.paused) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
            s.name = "";
            s.id = "";
            s.duration_sec = (s_idx + 1) * 300;
            s.remaining_sec = s.duration_sec;
            latter_ui::sync_multi_timer_globals(
                *rt_.timer_running, *rt_.timer_paused, *rt_.timer_remaining_sec,
                *rt_.timer_duration_sec, *rt_.timer_target_ms);
            rt_.apply_power_policy();
            rt_.schedule_disp();
          }
        }
        return;
      }
    } else if (*rt_.tools_subpage == 2) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          if (latter_ui::tools_direct_home()) {
            latter_ui::tools_direct_home() = false;
            *rt_.alarm_edit_idx = 0;
            *rt_.tools_subpage = 0;
            *rt_.gui_page = 1;
            *rt_.home_subpage = 0;
          } else if (*rt_.alarm_edit_idx > 0) {
            *rt_.alarm_edit_idx = 0;
          } else {
            *rt_.tools_subpage = 0;
          }
          rt_.schedule_disp();
        }
        return;
      }

      if (*rt_.alarm_edit_idx == 0) {
        const int hit_i = latter_ui::layout().hit_tool_card3(touch.x, touch.y);
        const int card_i = (hit_i >= 0) ? (hit_i + 1) : 0;
        if (card_i >= 1) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
            if (*rt_.alarm_snooze_until > 0 && (*rt_.alarm_ringing_idx == card_i || *rt_.alarm_ringing_idx == 0)) {
              *rt_.alarm_snooze_until = 0;
              rt_.schedule_disp();
              return;
            }
            if (touch.x >= latter_ui::layout().tool_card3_div_x()) {
              if (card_i == 1) {
                *rt_.alarm1_enabled = !*rt_.alarm1_enabled;
                if (!*rt_.alarm1_enabled && *rt_.alarm_ringing_idx <= 1) *rt_.alarm_snooze_until = 0;
              } else if (card_i == 2) {
                *rt_.alarm2_enabled = !*rt_.alarm2_enabled;
                if (!*rt_.alarm2_enabled && *rt_.alarm_ringing_idx == 2) *rt_.alarm_snooze_until = 0;
              } else {
                *rt_.alarm3_enabled = !*rt_.alarm3_enabled;
                if (!*rt_.alarm3_enabled && *rt_.alarm_ringing_idx == 3) *rt_.alarm_snooze_until = 0;
              }
            } else {
              *rt_.alarm_edit_idx = card_i;
            }
            rt_.schedule_disp();
          }
        }
        return;
      } else {
        const int edit_idx = *rt_.alarm_edit_idx;
        int &cur_h = (edit_idx == 3) ? *rt_.alarm3_hour : ((edit_idx == 2) ? *rt_.alarm2_hour : *rt_.alarm1_hour);
        int &cur_m = (edit_idx == 3) ? *rt_.alarm3_minute : ((edit_idx == 2) ? *rt_.alarm2_minute : *rt_.alarm1_minute);
        int &cur_rec = (edit_idx == 3) ? *rt_.alarm3_recurrence : ((edit_idx == 2) ? *rt_.alarm2_recurrence : *rt_.alarm1_recurrence);

        if ((hit != nullptr && hit->widget_id >= WidgetId::STEPPER_BASE && hit->widget_id < WidgetId::STEPPER_BASE + 4) ||
            (touch.y >= latter_ui::scale_y(204) && touch.y <= latter_ui::scale_y(276))) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
            const int st_col = (hit != nullptr && hit->widget_id >= WidgetId::STEPPER_BASE && hit->widget_id < WidgetId::STEPPER_BASE + 4)
                                   ? static_cast<int>(hit->widget_id - WidgetId::STEPPER_BASE)
                                   : latter_ui::hit_4col_stepper(touch.x);
            if (st_col >= 0) {
              if (st_col == 0) cur_h = (cur_h - 1 + 24) % 24;
              else if (st_col == 1) cur_h = (cur_h + 1) % 24;
              else if (st_col == 2) cur_m = (cur_m - 5 + 60) % 60;
              else if (st_col == 3) cur_m = (cur_m + 5 + 60) % 60;
              rt_.schedule_disp();
              return;
            }
          }
        }
        if (touch.y >= latter_ui::scale_y(280) && touch.y <= latter_ui::scale_y(372) &&
            std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(143)) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
            cur_rec = (cur_rec + 1) % 4;
            rt_.schedule_disp();
          }
          return;
        }
      }
    } else if (*rt_.tools_subpage == 3) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          if (latter_ui::tools_direct_home()) {
            latter_ui::tools_direct_home() = false;
            *rt_.tools_subpage = 0;
            *rt_.gui_page = 1;
            *rt_.home_subpage = 0;
          } else {
            *rt_.tools_subpage = 0;
          }
          rt_.schedule_disp();
        }
        return;
      }
      if (touch.y >= latter_ui::scale_y(280) && touch.y < latter_ui::scale_y(396)) {
        if (touch.x < latter_ui::SCREEN_CX) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
            if (*rt_.stopwatch_running) *rt_.stopwatch_lap_ms = now_touch - *rt_.stopwatch_start_time;
            else if (*rt_.stopwatch_elapsed_ms > 0) *rt_.stopwatch_lap_ms = *rt_.stopwatch_elapsed_ms;
            rt_.schedule_disp();
          }
          return;
        } else {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
            *rt_.stopwatch_running = false;
            *rt_.stopwatch_elapsed_ms = 0;
            *rt_.stopwatch_lap_ms = 0;
            rt_.schedule_disp();
          }
          return;
        }
      } else if (LATTER_ROUND_DISPLAY && touch.y <= 68 &&
                 touch.x >= latter_ui::SCREEN_CX - 110 && touch.x <= latter_ui::SCREEN_CX + 110) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          latter_ui::stopwatch_modern_digital() = !latter_ui::stopwatch_modern_digital();
          rt_.schedule_disp();
        }
        return;
      } else if (touch.y >= latter_ui::scale_y(45) && touch.y < latter_ui::scale_y(280) &&
                 touch.x >= latter_ui::scale_x(40) && touch.x <= latter_ui::SCREEN_W - latter_ui::scale_x(40)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          if (!*rt_.stopwatch_running) {
            *rt_.stopwatch_running = true;
            *rt_.stopwatch_start_time = now_touch - *rt_.stopwatch_elapsed_ms;
            *rt_.sleep_start_time = 0;
          } else {
            *rt_.stopwatch_running = false;
            *rt_.stopwatch_elapsed_ms = now_touch - *rt_.stopwatch_start_time;
          }
          rt_.schedule_disp();
        }
        return;
      }
    } else if (*rt_.tools_subpage == 4) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.tools_subpage = 0;
          rt_.schedule_disp();
        }
        return;
      }
      if (touch.y >= latter_ui::scale_y(385)) return;
      if (!latter_ui::dice_state().anim_active) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          latter_ui::trigger_dice_roll(now_touch, next_rng_u32(), next_rng_u32());
          rt_.schedule_disp();
        }
      }
    } else if (*rt_.tools_subpage == 5) {
      if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        *rt_.tools_subpage = 0;
        rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
        rt_.schedule_disp();
      }
      return;
    } else if (*rt_.tools_subpage == 6) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.tools_subpage = 0;
          latter_ui::reset_calculator_state();
          rt_.schedule_disp();
        }
        return;
      }
      if (std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(165) &&
          touch.y >= latter_ui::scale_y(104) && touch.y < latter_ui::scale_y(412)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
          latter_ui::handle_calculator_keypad_tap(touch.x, touch.y);
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.tools_subpage == 7) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.tools_subpage = 0;
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.tools_subpage == 8) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.tools_subpage = 0;
          rt_.schedule_disp();
        }
        return;
      }
      if (touch.y >= latter_ui::scale_y(385)) return;
      if (!latter_ui::coin_state().anim_active) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          latter_ui::trigger_coin_flip(now_touch, next_rng_u32());
          rt_.schedule_disp();
        }
      }
    } else if (*rt_.tools_subpage == 9) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          if (latter_ui::cal_detail_mode() == 2) {
            latter_ui::cal_detail_mode() = 1;
            rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
            rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
          } else if (latter_ui::cal_detail_mode() == 1) {
            latter_ui::cal_detail_mode() = 0;
          } else if (latter_ui::cal_quick_add_open()) {
            latter_ui::cal_quick_add_open() = false;
          } else if (latter_ui::tools_direct_home()) {
            latter_ui::tools_direct_home() = false;
            *rt_.tools_subpage = 0;
            *rt_.gui_page = 1;
            *rt_.home_subpage = 0;
          } else {
            *rt_.tools_subpage = 0;
          }
          rt_.schedule_disp();
        }
        return;
      }
      if (latter_ui::cal_detail_mode() == 2) return;
      if (latter_ui::cal_detail_mode() == 1) {
        if (!latter_ui::cal_detail_location().empty() &&
            touch.y >= latter_ui::scale_y(330) && touch.y <= latter_ui::scale_y(390) &&
            std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(123)) {
          if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
            latter_ui::cal_map_zoom() = 15;
            latter_ui::cal_map_pan_x() = 0;
            latter_ui::cal_map_pan_y() = 0;
            latter_ui::cal_detail_mode() = 2;
            rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
            rt_.cmd(UiHostCmd::FETCH_CALENDAR_MAP_SNAPSHOT);
            rt_.schedule_disp();
          }
        }
        return;
      }
      if (latter_ui::cal_quick_add_open()) {
        const int q_idx = latter_ui::layout().hit_slot5(touch, 85, 381);
        if (q_idx >= 0 && q_idx <= 4 && latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          static constexpr const char *kQuickSummaries[5] = {
              "Quick Reminder", "Check-In / Task", "Follow-Up Call", "Errand / Pickup", "Morning Reminder"
          };
          static constexpr int kQuickInMins[5] = {15, 30, 60, 180, 720};
          *rt_.cal_quick_summary = kQuickSummaries[q_idx];
          *rt_.cal_quick_in_minutes = kQuickInMins[q_idx];
          *rt_.cal_quick_duration_minutes = 30;
          latter_ui::cal_quick_add_open() = false;
          latter_ui::cal_feedback_text() = std::string("Added: ") + kQuickSummaries[q_idx];
          latter_ui::cal_feedback_ms() = now_touch;
          rt_.cmd(UiHostCmd::CREATE_CALENDAR_QUICK_EVENT);
          rt_.schedule_disp();
        }
        return;
      }
      if (*rt_.cal_browse_total_pages > 1 && latter_ui::is_right_chevron_zone(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_RAPID)) {
          const int step_d = (touch.y < latter_ui::scale_y(220)) ? -1 : 1;
          const int next_pg = std::max(1, std::min(*rt_.cal_browse_total_pages, *rt_.cal_browse_page + step_d));
          if (next_pg != *rt_.cal_browse_page) {
            *rt_.cal_browse_page = next_pg;
            rt_.cmd(UiHostCmd::FETCH_CALENDAR_BROWSE);
            rt_.schedule_disp();
          }
        }
        return;
      }
      if ((touch.y <= latter_ui::scale_y(78) && std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(123)) ||
          (rt_.cal_slot_names[0]->empty() &&
           touch.y >= latter_ui::scale_y(190) && touch.y <= latter_ui::scale_y(255) &&
           std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(148))) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          latter_ui::cal_quick_add_open() = true;
          rt_.schedule_disp();
        }
      }
      return;
    } else if (*rt_.tools_subpage == 10) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          *rt_.tools_subpage = 0;
          rt_.schedule_disp();
        }
        return;
      }
      if (touch.y <= latter_ui::scale_y(72) && std::abs(touch.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(120)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          latter_ui::intercom_live_call_mode() = !latter_ui::intercom_live_call_mode();
          rt_.schedule_disp();
        }
        return;
      }
      const int slot_idx = latter_ui::layout().hit_slot5(touch, latter_ui::SCREEN_CX - 145, latter_ui::SCREEN_CX + 145);
      if (slot_idx >= 0 && slot_idx <= 4 && latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
        if (latter_ui::intercom_live_call_mode()) {
          std::string t_ids[5], t_lbls[5];
          const int cnt = latter_ui::parse_intercom_targets_list(
              latter_ui::area_ctx_state().intercom_targets_summary, t_ids, t_lbls);
          if (slot_idx == 4 && cnt < 5) {
            gate_or_dispatch_doorbell_unlock(now_touch, true);
            return;
          } else if (slot_idx < cnt) {
            *rt_.cam_intercom_action = "call_target";
            *rt_.cam_intercom_message = t_ids[slot_idx];
            *rt_.intercom_feedback_str = "Call: " + t_lbls[slot_idx];
            *rt_.intercom_feedback_ms = now_touch;
            rt_.cmd(UiHostCmd::SEND_DOORBELL_INTERCOM_CMD);
            latter_ui::cam_quick_reply_open() = false;
            *rt_.tools_subpage = 12;
            rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
            rt_.cmd(UiHostCmd::START_DOORBELL_CALL);
          }
        } else {
          static constexpr const char *kIntercomPresets[5] = {
              "Dinner is ready!", "Time to head out!", "Kids, please come downstairs!",
              "Could use a hand in here!", "Movie night is starting!"
          };
          *rt_.intercom_message_str = kIntercomPresets[slot_idx];
          *rt_.intercom_feedback_str = std::string("Sent: ") + kIntercomPresets[slot_idx];
          *rt_.intercom_feedback_ms = now_touch;
          rt_.cmd(UiHostCmd::SEND_INTERCOM_BROADCAST);
        }
        rt_.schedule_disp();
      }
    } else if (*rt_.tools_subpage == 12) {
      if (latter_ui::is_back_button(touch)) {
        if (latter_ui::touch_router().allow_tap(now_touch, latter_ui::TOUCH_DEBOUNCE_BUTTON)) {
          if (latter_ui::cam_quick_reply_open()) {
            latter_ui::cam_quick_reply_open() = false;
            rt_.schedule_disp();
            return;
          }
          if (latter_ui::cam_call_active() || latter_ui::cam_intercom_ptt_active() || latter_ui::cam_listen_active()) {
            rt_.cmd(UiHostCmd::STOP_DOORBELL_CALL);
          }
          if (latter_ui::tools_direct_home()) {
            latter_ui::tools_direct_home() = false;
            *rt_.tools_subpage = 0;
            *rt_.gui_page = 1;
            *rt_.home_subpage = 0;
          } else {
            *rt_.tools_subpage = 0;
          }
          rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
          rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
          rt_.schedule_disp();
        }
        return;
      }
    }
  }
}

void TouchGestureEngine::on_touch_move(int tx, int ty, uint32_t now_u) {
  if (!rt_.bound) return;
  auto &gest = latter_ui::touch_gesture();
  if (!gest.active) return;
  gest.last_touch_ms = now_u;
  if (*rt_.display_power_state == 0 && !*rt_.va_active && (now_u - last_drag_sleep_rst_ms_ >= 2000U)) {
    last_drag_sleep_rst_ms_ = now_u;
    rt_.cmd(UiHostCmd::SCREEN_SLEEP_TIMER);
  }
  if (tx != gest.last_x || ty != gest.last_y) {
    gest.sample_count++;
  }
  gest.last_x = tx;
  gest.last_y = ty;
  const int dx = gest.last_x - gest.start_x;
  const int dy = gest.last_y - gest.start_y;
  const int cur_travel = std::abs(dx) + std::abs(dy);
  if (cur_travel > gest.max_travel) {
    gest.max_travel = cur_travel;
  }

  const int cur_ctx_u = latter_ui::compute_touch_context(
      *rt_.gui_page, *rt_.home_subpage, *rt_.music_overlay_mode, *rt_.active_game,
      *rt_.system_menu_mode, *rt_.sky_weather_subpage, *rt_.area_subpage,
      *rt_.remote_subpage, *rt_.tools_subpage, latter_ui::whole_home_tab());
  if (cur_ctx_u != gest.start_context) {
    return;
  }

  if (*rt_.gui_page == 4 && *rt_.system_menu_mode == 13 && (now_u - latter_ui::last_disp_update_ms() >= 42U)) {
    float hue = 0.0f, sat = 1.0f;
    if (ui::hsv_wheel_pick(gest.last_x, gest.last_y, latter_ui::SCREEN_CX, latter_ui::scale_y(180), 156, 56, hue, sat)) {
      latter_ui::color_wheel_hue_deg() = hue;
      latter_ui::color_wheel_sat() = sat;
      const esphome::Color c = latter_ui::hsv_to_rgb(hue / 360.0f, sat, 1.0f);
      *rt_.custom_theme_rgb = (static_cast<uint32_t>(c.r) << 16) |
                              (static_cast<uint32_t>(c.g) << 8) |
                              static_cast<uint32_t>(c.b);
      latter_ui::apply_custom_rgb_harmony(*rt_.custom_theme_rgb, *rt_.theme_primary, *rt_.theme_secondary);
      rt_.schedule_disp();
    }
    return;
  }

  if (gest.swipe_committed) return;
  if (now_u - gest.last_swipe_ms < latter_ui::TOUCH_POST_SWIPE_LOCKOUT) return;

  const bool can_swipe_home = latter_ui::can_go_home(
      *rt_.gui_page, *rt_.system_menu_mode, *rt_.active_game, *rt_.music_overlay_mode,
      *rt_.area_subpage, *rt_.area_browse_mode == "areas", *rt_.remote_subpage, *rt_.tools_subpage,
      *rt_.alarm_edit_idx, *rt_.alarm_ringing || *rt_.timer_ringing || *rt_.find_my_active);

  if (can_swipe_home && gest.start_y >= latter_ui::layout().home_zone_y() &&
      dy <= -latter_ui::layout().swipe_min() && std::abs(dy) > std::abs(dx)) {
    gest.swipe_committed = true;
    gest.last_swipe_ms = now_u;
    ESP_LOGD("touch", "Swipe-Up Home Bar -> Home");
    navigate_home(now_u);
    return;
  }

  if (*rt_.gui_page == 1 && *rt_.home_subpage == 0 &&
      gest.start_y >= latter_ui::layout().home_dock_zone_top(latter_ui::home_weather_text_h(), latter_ui::home_activity_card_shown())) {
    const int dock_swipe_min = latter_ui::layout().swipe_short();
    if (std::abs(dx) >= dock_swipe_min && std::abs(dx) >= std::abs(dy)) {
      int dock_apps[8] = {0, 0, 0, 0, 0, 0, 0, 0};
      const int dock_count = latter_ui::collect_home_dock_apps(dock_apps);
      const int total_pages = (dock_count + 3) / 4;
      if (total_pages > 1) {
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_u;
        latter_ui::home_dock_page() = (dx < 0)
            ? ((latter_ui::home_dock_page() + 1) % total_pages)
            : ((latter_ui::home_dock_page() + total_pages - 1) % total_pages);
        rt_.schedule_disp();
      }
      return;
    }
  }

  if (*rt_.gui_page == 2 && !*rt_.menu_scroll_active && *rt_.music_overlay_mode == 2 && gest.start_y < latter_ui::scale_y(404)) {
    if (std::abs(dy) >= latter_ui::layout().list_swipe() && std::abs(dy) > std::abs(dx)) {
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_u;
      const int target_pg = (dy < 0) ? 2 : 1;
      if (target_pg != *rt_.music_category_page) {
        *rt_.menu_scroll_active = true;
        *rt_.menu_scroll_dir = (target_pg == 2) ? 1 : -1;
        *rt_.menu_scroll_target_page = target_pg;
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      }
      return;
    }
  }

  if (*rt_.gui_page == 3 && *rt_.active_game == 0 && !*rt_.menu_scroll_active && gest.start_y < latter_ui::scale_y(410)) {
    if (std::abs(dx) >= latter_ui::layout().list_swipe() && std::abs(dx) > std::abs(dy)) {
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_u;
      const int dir = (dx < 0) ? 1 : -1;
      *rt_.menu_scroll_active = true;
      *rt_.menu_scroll_dir = dir;
      *rt_.menu_scroll_target_page = latter_ui::step_wrap(*rt_.game_carousel_idx, dir, 6);
      rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      return;
    }
  } else if (*rt_.gui_page == 3 && *rt_.active_game == 5 &&
             gest.start_y < latter_ui::layout().back_y - latter_ui::scale_y(36)) {
    if (std::abs(dx) >= latter_ui::layout().swipe_min() || std::abs(dy) >= latter_ui::layout().swipe_min()) {
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_u;
      if (std::abs(dx) > std::abs(dy)) {
        latter_ui::game_2048_pending_dir() = (dx > 0) ? 2 : 4;
      } else {
        latter_ui::game_2048_pending_dir() = (dy > 0) ? 3 : 1;
      }
      rt_.schedule_disp();
      return;
    }
  }

  if (*rt_.gui_page == 4 && !*rt_.menu_scroll_active &&
      (*rt_.system_menu_mode == 0 || *rt_.system_menu_mode == 1 || *rt_.system_menu_mode == 2) &&
      gest.start_y < latter_ui::scale_y(404)) {
    if (std::abs(dy) >= latter_ui::layout().list_swipe() && std::abs(dy) > std::abs(dx)) {
      const int cur_pg = (*rt_.system_menu_mode == 0)
                             ? *rt_.system_dashboard_page
                             : ((*rt_.system_menu_mode == 1) ? *rt_.sleep_menu_page : *rt_.display_menu_page);
      const int target_pg = (dy < 0) ? 2 : 1;
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_u;
      if (target_pg != cur_pg) {
        *rt_.menu_scroll_active = true;
        *rt_.menu_scroll_dir = (target_pg == 2) ? 1 : -1;
        *rt_.menu_scroll_target_page = target_pg;
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      }
      return;
    }
  }

  if (*rt_.gui_page == 5 && gest.start_y < latter_ui::scale_y(410)) {
    if (std::abs(dx) >= latter_ui::layout().wide_swipe() && std::abs(dx) > std::abs(dy)) {
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_u;
      *rt_.sky_weather_subpage = (dx < 0)
          ? ((*rt_.sky_weather_subpage + 1) % 4)
          : ((*rt_.sky_weather_subpage + 3) % 4);
      rt_.schedule_disp();
      return;
    }
  }

  if (*rt_.gui_page == 6 && *rt_.area_subpage == 2 && (now_u - latter_ui::last_disp_update_ms() >= 45U)) {
    if (latter_ui::light_insp_mode() == 0) {
      float hue = 0.0f, sat = 1.0f;
      if (ui::hsv_wheel_pick(gest.last_x, gest.last_y, latter_ui::SCREEN_CX, latter_ui::scale_y(212), 140, 62, hue, sat)) {
        gest.swipe_committed = true;
        latter_ui::light_insp_hue_deg() = hue;
        latter_ui::light_insp_sat() = sat;
        const esphome::Color c = latter_ui::hsv_to_rgb(hue / 360.0f, sat, 1.0f);
        const uint32_t rgb_v = (static_cast<uint32_t>(c.r) << 16) |
                               (static_cast<uint32_t>(c.g) << 8) |
                               static_cast<uint32_t>(c.b);
        latter_ui::area_color_hex_param() = latter_ui::format_hex_rgb(rgb_v);
        *rt_.area_target_code = latter_ui::area_color_hex_param();
        rt_.schedule_disp();
      }
    } else {
      const auto &ly = latter_ui::layout();
      const int sl_card_w = ly.light_slider_card_w();
      const int k_bar_w = ly.light_kelvin_bar_w();
      const int k_bar_x0 = ly.light_kelvin_bar_x0();
      const int b_max_w = ly.light_bri_bar_w();
      const int b_bar_x0 = ly.light_bri_bar_x0();
      if (gest.start_y >= latter_ui::scale_y(120) && gest.start_y <= latter_ui::scale_y(204) &&
          std::abs(gest.last_x - ly.cx) <= (sl_card_w / 2)) {
        gest.swipe_committed = true;
        const int k_val = 2700 + ((gest.last_x - k_bar_x0) * (6500 - 2700)) / std::max(1, k_bar_w);
        latter_ui::light_insp_kelvin() = std::max(2700, std::min(6500, (k_val / 100) * 100));
        rt_.schedule_band_disp(latter_ui::scale_y(116), latter_ui::scale_y(208));
      } else if (gest.start_y >= latter_ui::scale_y(218) && gest.start_y <= latter_ui::scale_y(302) &&
                 std::abs(gest.last_x - ly.cx) <= (sl_card_w / 2)) {
        gest.swipe_committed = true;
        const int b_val = ((gest.last_x - b_bar_x0) * 100) / std::max(1, b_max_w);
        latter_ui::light_insp_bri_pct() = std::max(5, std::min(100, ((b_val + 2) / 5) * 5));
        rt_.schedule_band_disp(latter_ui::scale_y(214), latter_ui::scale_y(306));
      }
    }
    return;
  }

  if (*rt_.gui_page == 6 && *rt_.area_subpage == 0 &&
      gest.start_y > latter_ui::scale_y(88) && gest.start_y < latter_ui::layout().edge_guard_y()) {
    const int dock_top_y = latter_ui::layout().mode_bar_zone_top();
    const int start_card = (gest.start_y < dock_top_y) ? latter_ui::layout().hit_area_card3(gest.start_x, gest.start_y) : -1;
    const std::string start_type = (start_card >= 0 && start_card < 3) ? *rt_.area_slot_types[start_card] : "";
    const int start_lvl = (start_card >= 0 && start_card < 3) ? *rt_.area_slot_levels[start_card] : -1;
    const bool is_card_slider_drag =
        (start_card >= 0 && (start_type == "light" || start_type == "cover" || start_type == "fan") &&
         start_lvl >= 0 && gest.start_x > latter_ui::layout().area_card_div_x());

    if (is_card_slider_drag) {
      if (std::abs(dx) >= latter_ui::layout().drag_start() && std::abs(dx) >= std::abs(dy)) {
        const int pct = latter_ui::layout().area_slider_pct(gest.last_x);
        bool &s_act = *rt_.area_slot_actives[start_card];
        int &s_lvl = *rt_.area_slot_levels[start_card];
        if (s_lvl != pct || !s_act) {
          s_act = true;
          s_lvl = pct;
          if (now_u - last_area_slider_ms_ > 45U) {
            last_area_slider_ms_ = now_u;
            const int cy_c = latter_ui::layout().area_card3_y(start_card);
            const int hh_c = latter_ui::layout().area_card3_h / 2 + 4;
            rt_.schedule_band_disp(cy_c - hh_c, cy_c + hh_c);
          }
        }
      }
      return;
    }

    if (gest.start_y >= dock_top_y && std::abs(dx) >= latter_ui::layout().swipe_min() && std::abs(dx) > std::abs(dy)) {
      std::string cats[8];
      const int cat_cnt = latter_ui::collect_area_categories(rt_.sw_state(UiSwitchId::AREA_SHOW_ALL_CATEGORY), cats);
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_u;
      int cur_i = 0;
      for (int i = 0; i < cat_cnt; i++) {
        if (cats[i] == *rt_.area_domain_filter) { cur_i = i; break; }
      }
      if (cat_cnt > 4) {
        const int cur_win = latter_ui::get_mode_bar_start_index(cat_cnt, latter_ui::area_cat_dock_page());
        const int next_win = std::max(0, std::min(cat_cnt - 4, cur_win + ((dx < 0) ? 1 : -1)));
        int next_i = cur_i;
        if (next_win != cur_win) {
          latter_ui::area_cat_dock_page() = next_win;
          if (next_i < next_win) next_i = next_win;
          else if (next_i > next_win + 3) next_i = next_win + 3;
        } else {
          next_i = std::max(0, std::min(cat_cnt - 1, cur_i + ((dx < 0) ? 1 : -1)));
          latter_ui::sync_mode_bar_window(latter_ui::area_cat_dock_page(), next_i, cat_cnt);
        }
        if (next_i != cur_i) {
          *rt_.area_domain_filter = cats[next_i];
          *rt_.area_browse_mode = "entities";
          *rt_.area_browse_page = 1;
          rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
        }
        rt_.schedule_disp();
      } else if (cat_cnt > 1) {
        const int next_i = (dx < 0) ? ((cur_i + 1) % cat_cnt) : ((cur_i + cat_cnt - 1) % cat_cnt);
        *rt_.area_domain_filter = cats[next_i];
        *rt_.area_browse_mode = "entities";
        *rt_.area_browse_page = 1;
        rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
        rt_.schedule_disp();
      }
      return;
    }

    if (gest.start_y < dock_top_y && std::abs(dx) >= latter_ui::layout().canvas_swipe() &&
        (std::abs(dx) * 2) > (std::abs(dy) * 3)) {
      std::string cats[8];
      const int cat_cnt = latter_ui::collect_area_categories(rt_.sw_state(UiSwitchId::AREA_SHOW_ALL_CATEGORY), cats);
      if (cat_cnt > 1) {
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_u;
        int cur_i = 0;
        for (int i = 0; i < cat_cnt; i++) {
          if (cats[i] == *rt_.area_domain_filter) { cur_i = i; break; }
        }
        const int next_i = (dx < 0) ? ((cur_i + 1) % cat_cnt) : ((cur_i + cat_cnt - 1) % cat_cnt);
        *rt_.area_domain_filter = cats[next_i];
        latter_ui::sync_mode_bar_window(latter_ui::area_cat_dock_page(), next_i, cat_cnt);
        *rt_.area_browse_mode = "entities";
        *rt_.area_browse_page = 1;
        rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
        rt_.schedule_disp();
      }
      return;
    }
  }

  if (*rt_.gui_page == 6 && gest.start_y > latter_ui::scale_y(88) && gest.start_y < latter_ui::scale_y(324)) {
    if (std::abs(dy) >= latter_ui::layout().list_swipe() && std::abs(dy) > std::abs(dx)) {
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_u;
      const int step_d = (dy < 0) ? 1 : -1;
      if (*rt_.area_subpage == 1 && *rt_.area_list_total_pages > 1) {
        const int next_pg = std::max(1, std::min(*rt_.area_list_total_pages, *rt_.area_list_page + step_d));
        if (next_pg != *rt_.area_list_page) {
          *rt_.area_list_page = next_pg;
          rt_.cmd(UiHostCmd::FETCH_AREA_LIST_BROWSE);
          rt_.schedule_disp();
        }
      } else if (*rt_.area_subpage == 0 && *rt_.area_browse_total_pages > 1) {
        const int next_pg = std::max(1, std::min(*rt_.area_browse_total_pages, *rt_.area_browse_page + step_d));
        if (next_pg != *rt_.area_browse_page) {
          *rt_.area_browse_page = next_pg;
          rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
          rt_.schedule_disp();
        }
      }
      return;
    }
  }

  if (*rt_.gui_page == 7 && *rt_.remote_subpage == 1 &&
      gest.start_y > latter_ui::scale_y(140) && gest.start_y < latter_ui::scale_y(404)) {
    if (std::abs(dy) >= latter_ui::layout().list_swipe() && std::abs(dy) > std::abs(dx)) {
      int valid_slots_u[latter_ui::MAX_REMOTE_SLOTS] = {0};
      const int valid_cnt_u = latter_ui::collect_configured_remote_slots(valid_slots_u);
      const int total_pg_u = std::max(1, (valid_cnt_u + 3) / 4);
      if (total_pg_u > 1) {
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_u;
        const int step_d = (dy < 0) ? 1 : -1;
        const int next_pg = std::max(0, std::min(total_pg_u - 1, latter_ui::remote_control_page() + step_d));
        if (next_pg != latter_ui::remote_control_page()) {
          latter_ui::remote_control_page() = next_pg;
          rt_.schedule_disp();
        }
      }
      return;
    }
  }

  if (*rt_.gui_page == 8 && !*rt_.menu_scroll_active && gest.start_y < latter_ui::scale_y(404)) {
    if (*rt_.tools_subpage == 0 && std::abs(dy) >= latter_ui::layout().list_swipe() && std::abs(dy) > std::abs(dx)) {
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_u;
      const int target_pg = (dy < 0) ? 1 : 0;
      if (target_pg != *rt_.tools_menu_page) {
        *rt_.menu_scroll_active = true;
        *rt_.menu_scroll_dir = (target_pg == 1) ? 1 : -1;
        *rt_.menu_scroll_target_page = target_pg;
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      }
      return;
    } else if (*rt_.tools_subpage == 9 && !latter_ui::cal_quick_add_open() &&
               *rt_.cal_browse_total_pages > 1 &&
               std::abs(dy) >= latter_ui::layout().list_swipe() && std::abs(dy) > std::abs(dx)) {
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_u;
      const int step_d = (dy < 0) ? 1 : -1;
      const int next_pg = std::max(1, std::min(*rt_.cal_browse_total_pages, *rt_.cal_browse_page + step_d));
      if (next_pg != *rt_.cal_browse_page) {
        *rt_.cal_browse_page = next_pg;
        rt_.cmd(UiHostCmd::FETCH_CALENDAR_BROWSE);
        rt_.schedule_disp();
      }
      return;
    } else if (*rt_.tools_subpage == 12 && !latter_ui::cam_quick_reply_open() &&
               std::abs(dx) >= latter_ui::layout().page_swipe() && std::abs(dx) > std::abs(dy)) {
      std::string c_ids[latter_ui::MAX_CAMERA_SLOTS], c_names[latter_ui::MAX_CAMERA_SLOTS];
      const int c_total = latter_ui::parse_cameras_summary_list(latter_ui::area_ctx_state().cameras_summary, c_ids, c_names);
      if (c_total > 1) {
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_u;
        const int dir = (dx < 0) ? 1 : -1;
        latter_ui::cam_carousel_idx() = latter_ui::step_wrap(latter_ui::cam_carousel_idx(), dir, c_total);
        rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
        rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
        rt_.schedule_disp();
      }
      return;
    }
  }
}

void TouchGestureEngine::on_touch_up(uint32_t now_rel) {
  if (!rt_.bound) return;
  const uint64_t rel_t0_us = latter_ui::profiler_enabled() ? latter_ui::prof_now_us() : 0ULL;
  *rt_.hud_wake_finger_released = true;
  auto &gest = latter_ui::touch_gesture();
  gest.last_touch_ms = now_rel;
  if (*rt_.display_power_state == 0 && !*rt_.va_active) {
    rt_.cmd(UiHostCmd::SCREEN_SLEEP_TIMER);
  }
  const int cur_ctx_rel = latter_ui::compute_touch_context(
      *rt_.gui_page, *rt_.home_subpage, *rt_.music_overlay_mode, *rt_.active_game,
      *rt_.system_menu_mode, *rt_.sky_weather_subpage, *rt_.area_subpage,
      *rt_.remote_subpage, *rt_.tools_subpage, latter_ui::whole_home_tab());

  if (gest.active && cur_ctx_rel == gest.start_context) {
    const int dx = gest.last_x - gest.start_x;
    const int dy = gest.last_y - gest.start_y;
    const TouchPt tp{gest.last_x, gest.last_y};
    const HitRegion *hit = frame_hits().hit_test(tp.x, tp.y, kHitTap);

    const bool can_swipe_home = latter_ui::can_go_home(
        *rt_.gui_page, *rt_.system_menu_mode, *rt_.active_game, *rt_.music_overlay_mode,
        *rt_.area_subpage, *rt_.area_browse_mode == "areas", *rt_.remote_subpage, *rt_.tools_subpage,
        *rt_.alarm_edit_idx, *rt_.alarm_ringing || *rt_.timer_ringing || *rt_.find_my_active);

    const bool is_home_pill_tap = (gest.max_travel < latter_ui::layout().swipe_min() &&
                                   latter_ui::layout().is_home_pill_tap(tp));
    if (!gest.swipe_committed && can_swipe_home &&
        ((gest.start_y >= latter_ui::layout().home_zone_y() &&
          dy <= -latter_ui::layout().swipe_min() && std::abs(dy) > std::abs(dx)) ||
         is_home_pill_tap)) {
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_rel;
      navigate_home(now_rel);
    } else if (!gest.swipe_committed && *rt_.gui_page == 1 && *rt_.home_subpage == 0 &&
               gest.start_y >= latter_ui::layout().home_dock_zone_top(latter_ui::home_weather_text_h(), latter_ui::home_activity_card_shown())) {
      int dock_apps[8] = {0, 0, 0, 0, 0, 0, 0, 0};
      const int dock_count = latter_ui::collect_home_dock_apps(dock_apps);
      const int total_pages = (dock_count + 3) / 4;

      const int dock_rel_swipe = latter_ui::layout().dock_release_swipe();
      if (std::abs(dx) >= dock_rel_swipe && std::abs(dx) > (std::abs(dy) * 3) / 2 && total_pages > 1) {
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_rel;
        latter_ui::home_dock_page() = (dx < 0)
            ? ((latter_ui::home_dock_page() + 1) % total_pages)
            : ((latter_ui::home_dock_page() + total_pages - 1) % total_pages);
        rt_.schedule_disp();
      } else if (gest.max_travel < latter_ui::layout().tap_max() && dock_count > 0) {
        const int dock_cy = latter_ui::layout().dock_y;
        const int dock_hit_r = latter_ui::layout().dock_hit_r();
        if (gest.last_y >= (dock_cy - dock_hit_r) && gest.last_y <= (dock_cy + dock_hit_r)) {
          int win_xs[4] = {0, 0, 0, 0};
          bool dummy_l = false, dummy_r = false;
          latter_ui::get_home_dock_window_xs(dock_count, latter_ui::home_dock_page(), latter_ui::SCREEN_CX, win_xs, dummy_l, dummy_r);
          const int start_idx = latter_ui::get_home_dock_start_index(dock_count, latter_ui::home_dock_page());
          const int vis_count = std::min(4, dock_count - start_idx);
          int best_i = -1;
          int best_dist = (vis_count < 4) ? latter_ui::scale_x(50) : latter_ui::scale_x(42);
          for (int i = 0; i < vis_count; i++) {
            const int d = std::abs(gest.last_x - win_xs[i]);
            if (d < best_dist) { best_dist = d; best_i = i; }
          }
          if (best_i >= 0) {
            const int chosen_app = dock_apps[start_idx + best_i];
            if (chosen_app == 12) {
              *rt_.gui_page = 8;
              *rt_.tools_subpage = 12;
              latter_ui::tools_direct_home() = true;
              latter_ui::cam_quick_reply_open() = false;
              rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
              rt_.schedule_disp();
            } else {
              *rt_.gui_page = chosen_app;
              latter_ui::tools_direct_home() = false;
              *rt_.music_overlay_mode = 0;
              if (*rt_.gui_page == 4) {
                *rt_.system_menu_mode = 0;
                *rt_.system_dashboard_page = 1;
              } else if (*rt_.gui_page == 2) {
                *rt_.music_hud_active = true;
                *rt_.music_hud_timer = now_rel;
                const auto &ms_dk = latter_ui::media_state();
                if ((!ms_dk.is_local || rt_.album_art_w == 0) && ms_dk.art_url != *rt_.current_loaded_art_url) {
                  rt_.cmd(UiHostCmd::FETCH_REMOTE_ART_DEBOUNCED);
                }
              } else if (*rt_.gui_page == 6) {
                latter_ui::area_cat_dock_page() = 0;
                *rt_.area_domain_filter = latter_ui::resolve_preferred_area_category(
                    rt_.select_opt(UiSelectId::AREA_DEFAULT_CATEGORY),
                    rt_.sw_state(UiSwitchId::AREA_SHOW_ALL_CATEGORY));
                if (rt_.active_area_id->empty()) {
                  *rt_.area_subpage = 1;
                  *rt_.area_list_page = 1;
                  rt_.cmd(UiHostCmd::FETCH_AREA_LIST_BROWSE);
                } else {
                  *rt_.area_subpage = 0;
                  *rt_.area_browse_mode = "entities";
                  *rt_.area_browse_page = 1;
                  rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
                }
              }
              *rt_.remote_subpage = 0;
              *rt_.tools_subpage = 0;
              *rt_.active_game = 0;
              rt_.schedule_disp();
            }
          }
        }
      }
    } else if (!gest.swipe_committed && *rt_.gui_page == 2 &&
               (*rt_.music_overlay_mode == 7 || *rt_.music_overlay_mode == 8) &&
               gest.start_y < latter_ui::scale_y(404)) {
      if (std::abs(dy) >= latter_ui::layout().swipe_short() && std::abs(dy) > std::abs(dx) &&
          *rt_.music_browse_total_pages > 1) {
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_rel;
        const int step_d = (dy < 0) ? 1 : -1;
        const int next_pg = std::max(1, std::min(*rt_.music_browse_total_pages, *rt_.music_browse_page + step_d));
        if (next_pg != *rt_.music_browse_page) {
          *rt_.music_browse_page = next_pg;
          rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
          rt_.schedule_disp();
        }
      }
    } else if (!gest.swipe_committed && *rt_.gui_page == 2 &&
               (*rt_.music_overlay_mode >= 1 && *rt_.music_overlay_mode <= 3) &&
               gest.start_context == (200 + *rt_.music_overlay_mode) &&
               gest.start_y < latter_ui::scale_y(404)) {
      if ((*rt_.music_overlay_mode == 1 || *rt_.music_overlay_mode == 3) &&
          std::abs(dy) >= latter_ui::layout().swipe_short() && std::abs(dy) > std::abs(dx) &&
          *rt_.music_browse_total_pages > 1) {
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_rel;
        latter_ui::music_scroll_tap_lockout_until_ms() = now_rel + 180U;
        const int step_d = (dy < 0) ? 1 : -1;
        const bool wrap_list = (*rt_.music_overlay_mode == 3) &&
                               (*rt_.selected_category_mode == "artist" || *rt_.selected_category_mode == "album");
        const int next_pg = wrap_list
            ? latter_ui::step_wrap_1based(*rt_.music_browse_page, step_d, *rt_.music_browse_total_pages)
            : std::max(1, std::min(*rt_.music_browse_total_pages, *rt_.music_browse_page + step_d));
        if (next_pg != *rt_.music_browse_page) {
          *rt_.music_browse_page = next_pg;
          latter_ui::last_music_page_req_ms() = now_rel;
          *rt_.menu_scroll_active = false;
          rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
        }
      } else if (!*rt_.menu_scroll_active && gest.max_travel < latter_ui::layout().tap_max_loose() &&
                 gest.last_y < latter_ui::scale_y(404) &&
                 ((now_rel - gest.last_swipe_ms >= 180U && now_rel >= latter_ui::music_scroll_tap_lockout_until_ms()) ||
                  latter_ui::is_right_chevron_zone(tp))) {
        if (latter_ui::is_right_chevron_zone(tp)) {
          if (*rt_.music_overlay_mode == 2) {
            const int target_pg = (*rt_.music_category_page == 1) ? 2 : 1;
            *rt_.menu_scroll_active = true;
            *rt_.menu_scroll_dir = (target_pg == 2) ? 1 : -1;
            *rt_.menu_scroll_target_page = target_pg;
            rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
          } else if (*rt_.music_browse_total_pages > 1) {
            const int step_d = (tp.y < latter_ui::SCREEN_CY) ? -1 : 1;
            const bool wrap_list = (*rt_.music_overlay_mode == 3) &&
                                   (*rt_.selected_category_mode == "artist" || *rt_.selected_category_mode == "album");
            const int next_pg = wrap_list
                ? latter_ui::step_wrap_1based(*rt_.music_browse_page, step_d, *rt_.music_browse_total_pages)
                : std::max(1, std::min(*rt_.music_browse_total_pages, *rt_.music_browse_page + step_d));
            if (next_pg != *rt_.music_browse_page) {
              *rt_.music_browse_page = next_pg;
              rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
            }
          }
        } else if (*rt_.music_overlay_mode == 1) {
          const int slot_idx = latter_ui::layout().hit_slot5(tp, 75, 385);
          if (slot_idx >= 0 && slot_idx < 5 && !rt_.slot_names[slot_idx]->empty() && !rt_.slot_ids[slot_idx]->empty()) {
            *rt_.target_speaker_id = *rt_.slot_ids[slot_idx];
            *rt_.target_speaker_name = *rt_.slot_names[slot_idx];
            *rt_.music_overlay_mode = 4;
            rt_.schedule_disp();
          }
        } else if (*rt_.music_overlay_mode == 2) {
          const int slot_idx = latter_ui::layout().hit_slot5(tp, 75, 385);
          if (slot_idx >= 0) {
            const int idx = (*rt_.music_category_page == 1) ? slot_idx : (slot_idx + 2);
            if (idx == 0) { *rt_.selected_category_mode = "favorites"; *rt_.selected_category_title = "FAVORITES"; }
            else if (idx == 1) { *rt_.selected_category_mode = "playlist"; *rt_.selected_category_title = "PLAYLISTS"; }
            else if (idx == 2) { *rt_.selected_category_mode = "artist"; *rt_.selected_category_title = "ARTISTS"; *rt_.drilldown_artist_page = 1; }
            else if (idx == 3) { *rt_.selected_category_mode = "album"; *rt_.selected_category_title = "ALBUMS"; *rt_.drilldown_album_page = 1; }
            else if (idx == 4) { *rt_.selected_category_mode = "podcast"; *rt_.selected_category_title = "PODCASTS"; }
            else if (idx == 5) { *rt_.selected_category_mode = "radio"; *rt_.selected_category_title = "RADIO"; }
            else if (idx == 6) { *rt_.selected_category_mode = "audiobook"; *rt_.selected_category_title = "AUDIOBOOKS"; }
            *rt_.slot_names[0] = "Loading...";
            *rt_.slot_ids[0] = ""; *rt_.slot_types[0] = "text";
            for (int i = 1; i < 5; i++) { *rt_.slot_names[i] = ""; *rt_.slot_ids[i] = ""; }
            *rt_.music_browse_page = 1;
            *rt_.music_browse_total_pages = 1;
            *rt_.music_overlay_mode = 3;
            rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
            rt_.schedule_disp();
          }
        } else if (*rt_.music_overlay_mode == 3) {
          const bool is_drill = (*rt_.selected_category_mode == "artist" ||
                                 *rt_.selected_category_mode == "album" ||
                                 *rt_.selected_category_mode == "artist_albums");
          const bool is_podcast_list = (*rt_.selected_category_mode == "podcast");
          const int slot_idx = latter_ui::layout().hit_slot5(tp, 12, 415);
          if (slot_idx >= 0 && slot_idx < 5) {
            const int slot_num = slot_idx + 1;
            const std::string s_id = *rt_.slot_ids[slot_idx];
            const std::string s_name = *rt_.slot_names[slot_idx];
            if (!s_id.empty()) {
              if (is_podcast_list) {
                *rt_.drilldown_album_id = s_id;
                *rt_.drilldown_album_page = *rt_.music_browse_page;
                *rt_.drilldown_parent_mode = "podcast";
                *rt_.selected_category_mode = "podcast_episodes";
                *rt_.selected_category_title = s_name;
                *rt_.slot_names[0] = "Loading episodes...";
                *rt_.slot_ids[0] = ""; *rt_.slot_types[0] = "text";
                for (int i = 1; i < 5; i++) { *rt_.slot_names[i] = ""; *rt_.slot_ids[i] = ""; }
                *rt_.music_browse_page = 1;
                rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
                rt_.schedule_disp();
              } else if (is_drill && latter_ui::layout().hit_music_drill_play(tp.x)) {
                rt_.cmd(UiHostCmd::PLAY_MUSIC_SLOT, slot_num);
              } else if (is_drill) {
                if (*rt_.selected_category_mode == "artist") {
                  *rt_.drilldown_artist_name = s_name;
                  *rt_.drilldown_artist_page = *rt_.music_browse_page;
                  *rt_.selected_category_mode = "artist_albums";
                  *rt_.selected_category_title = s_name;
                  *rt_.music_browse_page = 1;
                  rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
                } else {
                  *rt_.drilldown_album_id = s_id;
                  *rt_.drilldown_album_page = *rt_.music_browse_page;
                  *rt_.drilldown_parent_mode = *rt_.selected_category_mode;
                  *rt_.selected_category_mode = "album_tracks";
                  *rt_.selected_category_title = s_name;
                  *rt_.music_browse_page = 1;
                  rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE);
                }
              } else {
                rt_.cmd(UiHostCmd::PLAY_MUSIC_SLOT, slot_num);
              }
            }
          }
        }
      }
    } else if (!gest.swipe_committed && *rt_.gui_page == 3 && *rt_.active_game == 5) {
      if (gest.last_y < latter_ui::layout().back_y - latter_ui::scale_y(36)) {
        if (std::abs(dx) >= latter_ui::layout().flick_min() || std::abs(dy) >= latter_ui::layout().flick_min()) {
          if (std::abs(dx) > std::abs(dy)) {
            latter_ui::game_2048_pending_dir() = (dx > 0) ? 2 : 4;
          } else {
            latter_ui::game_2048_pending_dir() = (dy > 0) ? 3 : 1;
          }
        } else {
          *rt_.marble_touch_x = static_cast<float>(gest.last_x);
          *rt_.marble_touch_y = static_cast<float>(gest.last_y);
          *rt_.game_btn_pressed = true;
        }
        rt_.schedule_disp();
      }
    } else if (!gest.swipe_committed && *rt_.gui_page == 3 && *rt_.active_game == 0 && !*rt_.menu_scroll_active) {
      if (gest.start_y < latter_ui::scale_y(410) &&
          std::abs(dx) >= latter_ui::layout().swipe_short() && std::abs(dx) > std::abs(dy)) {
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_rel;
        const int dir = (dx < 0) ? 1 : -1;
        *rt_.menu_scroll_active = true;
        *rt_.menu_scroll_dir = dir;
        *rt_.menu_scroll_target_page = latter_ui::step_wrap(*rt_.game_carousel_idx, dir, 6);
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      } else if (gest.max_travel < latter_ui::layout().tap_max()) {
        const int g_btn_y = latter_ui::layout().app_dock_y;
        if (gest.last_x >= latter_ui::SCREEN_CX - latter_ui::scale_x(125) &&
            gest.last_x <= latter_ui::SCREEN_CX - latter_ui::scale_x(55) &&
            gest.last_y >= g_btn_y - latter_ui::scale_y(34) &&
            gest.last_y <= g_btn_y + latter_ui::scale_y(34)) {
          rt_.sw_toggle(UiSwitchId::GAME_SOUND);
          rt_.schedule_disp();
        } else if (gest.last_x >= latter_ui::SCREEN_CX - latter_ui::scale_x(140) &&
                   gest.last_x < latter_ui::SCREEN_CX + latter_ui::scale_x(140) &&
                   gest.last_y >= latter_ui::scale_y(130) &&
                   gest.last_y <= g_btn_y + latter_ui::scale_y(34)) {
          *rt_.active_game = *rt_.game_carousel_idx + 1;
          rt_.cmd(UiHostCmd::STOP_MP_IDLE_DEBOUNCE);
          if (rt_.local_mp_playing) {
            rt_.cmd(UiHostCmd::PAUSE_LOCAL_MEDIA_PLAYER);
            rt_.cmd(UiHostCmd::STOP_MP_IDLE_DEBOUNCE);
          }
          rt_.schedule_disp();
        }
      }
    } else if (!gest.swipe_committed && *rt_.gui_page == 4 && !*rt_.menu_scroll_active &&
               (*rt_.system_menu_mode == 0 || *rt_.system_menu_mode == 1 || *rt_.system_menu_mode == 2)) {
      const int cur_pg = (*rt_.system_menu_mode == 0)
                             ? *rt_.system_dashboard_page
                             : ((*rt_.system_menu_mode == 1) ? *rt_.sleep_menu_page : *rt_.display_menu_page);
      if (gest.start_y < latter_ui::scale_y(410) &&
          std::abs(dy) >= latter_ui::layout().swipe_short() && std::abs(dy) > std::abs(dx)) {
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_rel;
        const int target_pg = (dy < 0) ? 2 : 1;
        if (target_pg != cur_pg) {
          *rt_.menu_scroll_active = true;
          *rt_.menu_scroll_dir = (target_pg == 2) ? 1 : -1;
          *rt_.menu_scroll_target_page = target_pg;
          rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
        }
      } else if (gest.max_travel < latter_ui::layout().tap_max() && gest.last_y < latter_ui::scale_y(404)) {
        if (latter_ui::is_right_chevron_zone(tp)) {
          const int target_pg = (cur_pg == 1) ? 2 : 1;
          *rt_.menu_scroll_active = true;
          *rt_.menu_scroll_dir = (target_pg == 2) ? 1 : -1;
          *rt_.menu_scroll_target_page = target_pg;
          rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
        } else if (*rt_.system_menu_mode == 0) {
          const int slot = latter_ui::layout().hit_slot5(tp, 75, 385);
          if (slot >= 0) {
            const int item_idx = (*rt_.system_dashboard_page == 1) ? slot : (slot + 3);
            if (item_idx == 0) { *rt_.system_menu_mode = 2; *rt_.display_menu_page = 1; }
            else if (item_idx == 1) { *rt_.system_menu_mode = 1; *rt_.sleep_menu_page = 1; }
            else if (item_idx == 2) { *rt_.system_menu_mode = 7; }
            else if (item_idx == 3) { *rt_.system_menu_mode = 11; }
            else if (item_idx == 4) { *rt_.system_menu_mode = 9; *rt_.audio_menu_parent_page = 4; }
            else if (item_idx == 5) { *rt_.gui_page = 1; *rt_.home_subpage = 1; }
            else if (item_idx == 6) { *rt_.system_menu_mode = 3; }
            else if (item_idx == 7) { *rt_.system_menu_mode = 8; }
            rt_.schedule_disp();
          }
        } else if (*rt_.system_menu_mode == 1) {
          const int slot_idx = latter_ui::layout().hit_slot5(tp, 75, 385);
          if (slot_idx >= 0) {
            const int idx = (*rt_.sleep_menu_page == 1) ? slot_idx : (slot_idx + 5);
            if (idx == 0) {
              rt_.set_select_opt(UiSelectId::POWER_PROFILE,
                                 latter_ui::cycle_option(rt_.select_opt(UiSelectId::POWER_PROFILE),
                                                         {"Portable Carry", "Handheld Roaming", "Wearable Watch",
                                                          "Tabletop Dock", "Always-On Wall / Desk"}));
            } else if (idx == 1) {
#if LATTER_HAS_IMU
              rt_.set_select_opt(UiSelectId::PICKUP_WAKE_MODE,
                                 latter_ui::cycle_option(rt_.select_opt(UiSelectId::PICKUP_WAKE_MODE),
                                                         {"Always On", "Docked Only", "Disabled"}));
#endif
            } else if (idx == 2) {
              rt_.set_select_opt(UiSelectId::TAP_WAKE_MODE,
                                 latter_ui::cycle_option(rt_.select_opt(UiSelectId::TAP_WAKE_MODE),
                                                         {"Always On", "Docked Only", "Disabled"}));
            } else if (idx == 3) {
#if LATTER_HAS_BATTERY
              rt_.set_select_opt(UiSelectId::DISPLAY_OFF_TIMEOUT,
                                 latter_ui::cycle_option(rt_.select_opt(UiSelectId::DISPLAY_OFF_TIMEOUT),
                                                         {"10s", "15s", "30s", "1 min", "2 min", "Never"}));
#endif
            } else if (idx == 4) {
              rt_.sw_toggle(UiSwitchId::DOCKED_SCREENSAVER);
            } else if (idx == 5) {
              if (rt_.sw_state(UiSwitchId::DOCKED_SCREENSAVER)) {
                rt_.set_select_opt(UiSelectId::SCREENSAVER_STYLE,
                                   latter_ui::cycle_option(rt_.select_opt(UiSelectId::SCREENSAVER_STYLE),
                                                           {"Ambient Clock", "Aurora", "Starfield", "Digital Rain", "Fireworks"}));
              }
            } else if (idx == 6) {
              rt_.set_select_opt(UiSelectId::SCREENSAVER_DOCKED_TIMEOUT,
                                 latter_ui::cycle_option(rt_.select_opt(UiSelectId::SCREENSAVER_DOCKED_TIMEOUT),
                                                         {"10s", "15s", "30s", "1 min", "2 min", "5 min", "Never"}));
            } else if (idx == 7) {
#if LATTER_HAS_BATTERY
              rt_.set_select_opt(UiSelectId::HIBERNATE_TIMEOUT_MODE,
                                 latter_ui::cycle_option(rt_.select_opt(UiSelectId::HIBERNATE_TIMEOUT_MODE),
                                                         {"5 min", "10 min", "15 min", "30 min", "Disabled"}));
#endif
            } else if (idx == 8) {
#if LATTER_HAS_BATTERY
              rt_.sw_toggle(UiSwitchId::INSTANT_CROWN_HIBERNATE);
#endif
            }
            rt_.schedule_disp();
          }
        } else if (*rt_.system_menu_mode == 2) {
          const int slot_idx = latter_ui::layout().hit_slot5(tp, 75, 385);
          if (slot_idx >= 0) {
            const int item_idx = (*rt_.display_menu_page == 1) ? slot_idx : (slot_idx + 4);
            const std::string ab = rt_.select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE);
            if (item_idx == 0) {
              rt_.set_select_opt(UiSelectId::DEFAULT_START_APP,
                                 latter_ui::cycle_option(rt_.select_opt(UiSelectId::DEFAULT_START_APP), {"Home", "Clock"}));
            } else if (item_idx == 1) {
              const std::string next_cf = latter_ui::cycle_option(
                  rt_.select_opt(UiSelectId::CLOCK_FACE_STYLE),
                  {"Minimal Digital", "Modern Roman (Analog)", "Modern Digital (Bold)"});
              *rt_.clock_face_digital = (next_cf != "Modern Roman (Analog)");
              rt_.set_select_opt(UiSelectId::CLOCK_FACE_STYLE, next_cf);
            } else if (item_idx == 2) {
              *rt_.is_24h_format = !*rt_.is_24h_format;
              rt_.set_select_opt(UiSelectId::TIME_FORMAT, *rt_.is_24h_format ? "24-Hour" : "12-Hour (AM/PM)");
            } else if (item_idx == 3) {
              if (latter_ui::layout().hit_sys_color_swatch(tp.x)) {
                *rt_.system_menu_mode = 13;
                rt_.schedule_disp();
                gest.active = false;
                return;
              }
              const std::string next_pal = latter_ui::cycle_option(
                  rt_.select_opt(UiSelectId::THEME_PALETTE),
                  {"Steel Blue", "Pink", "Blue", "Green", "Custom"});
              rt_.set_select_opt(UiSelectId::THEME_PALETTE, next_pal);
              if (next_pal == "Custom") {
                *rt_.system_menu_mode = 13;
              }
            } else if (item_idx == 4) {
              const std::string next_m = latter_ui::cycle_option(ab, {"Disabled", "Sun Synchronized", "Time Schedule"});
              rt_.set_select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE, next_m, true);
              rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
            } else if (item_idx == 5 && ab == "Disabled") {
              const std::string next_b = latter_ui::cycle_option(
                  rt_.select_opt(UiSelectId::DISPLAY_BRIGHTNESS_PRESET), {"60%", "70%", "80%", "90%", "100%"});
              rt_.set_select_opt(UiSelectId::DISPLAY_BRIGHTNESS_PRESET, next_b, true);
              rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
            } else if (item_idx == 6 && ab == "Time Schedule") {
              const int dh = static_cast<int>(rt_.num_val(UiNumberId::DAY_START_HOUR));
              const int nh = static_cast<int>(rt_.num_val(UiNumberId::NIGHT_START_HOUR));
              int nxt_dh = 7, nxt_nh = 22;
              if (dh == 7 && nh == 22) { nxt_dh = 6; nxt_nh = 23; }
              else if (dh == 6 && nh == 23) { nxt_dh = 8; nxt_nh = 22; }
              else if (dh == 8 && nh == 22) { nxt_dh = 7; nxt_nh = 21; }
              rt_.set_num_val(UiNumberId::DAY_START_HOUR, static_cast<float>(nxt_dh));
              rt_.set_num_val(UiNumberId::NIGHT_START_HOUR, static_cast<float>(nxt_nh));
              rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
            } else if (item_idx == 7 && ab != "Disabled") {
              const std::string next_db = latter_ui::cycle_option(
                  rt_.select_opt(UiSelectId::DAY_BRIGHTNESS_PRESET), {"60%", "70%", "80%", "90%", "100%"});
              rt_.set_select_opt(UiSelectId::DAY_BRIGHTNESS_PRESET, next_db, true);
              rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
            } else if (item_idx == 8 && ab != "Disabled") {
              const std::string next_nb = latter_ui::cycle_option(
                  rt_.select_opt(UiSelectId::NIGHT_BRIGHTNESS_PRESET), {"60%", "70%", "80%", "90%", "100%"});
              rt_.set_select_opt(UiSelectId::NIGHT_BRIGHTNESS_PRESET, next_nb, true);
              rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
            }
            rt_.schedule_disp();
          }
        }
      }
    } else if (!gest.swipe_committed && *rt_.gui_page == 5) {
      if (gest.start_y < latter_ui::layout().home_zone_y() &&
          std::abs(dx) >= latter_ui::layout().page_swipe() && std::abs(dx) > std::abs(dy)) {
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_rel;
        *rt_.sky_weather_subpage = (dx < 0)
            ? ((*rt_.sky_weather_subpage + 1) % 4)
            : ((*rt_.sky_weather_subpage + 3) % 4);
        rt_.schedule_disp();
      } else if (gest.max_travel < latter_ui::layout().tap_max() &&
                 gest.last_y >= latter_ui::layout().mode_bar_zone_top() &&
                 gest.last_y <= latter_ui::layout().app_dock_y + latter_ui::layout().dock_r + 16) {
        int w_xs[4] = {0, 0, 0, 0};
        bool d_l = false, d_r = false;
        latter_ui::get_home_dock_window_xs(4, 0, latter_ui::SCREEN_CX, w_xs, d_l, d_r);
        int target = -1;
        int best_d = latter_ui::scale_x(46);
        for (int i = 0; i < 4; i++) {
          const int d = std::abs(gest.last_x - w_xs[i]);
          if (d < best_d) { best_d = d; target = i; }
        }
        if (target >= 0 && *rt_.sky_weather_subpage != target) {
          *rt_.sky_weather_subpage = target;
          rt_.schedule_disp();
        }
      }
    } else if (*rt_.gui_page == 6 && *rt_.area_subpage == 2 && gest.last_y < latter_ui::scale_y(404)) {
      if (gest.max_travel < latter_ui::layout().tap_max() && tp.y <= latter_ui::scale_y(68) &&
          std::abs(tp.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(123)) {
        latter_ui::light_insp_mode() = (latter_ui::light_insp_mode() == 0) ? 1 : 0;
        rt_.schedule_disp();
      } else if (latter_ui::light_insp_mode() == 0) {
        float hue = 0.0f, sat = 1.0f;
        if (ui::hsv_wheel_pick(tp.x, tp.y, latter_ui::SCREEN_CX, latter_ui::scale_y(212), 140, 62, hue, sat)) {
          latter_ui::light_insp_hue_deg() = hue;
          latter_ui::light_insp_sat() = sat;
          const esphome::Color c = latter_ui::hsv_to_rgb(hue / 360.0f, sat, 1.0f);
          const uint32_t rgb_v = (static_cast<uint32_t>(c.r) << 16) |
                                 (static_cast<uint32_t>(c.g) << 8) |
                                 static_cast<uint32_t>(c.b);
          latter_ui::area_color_hex_param() = latter_ui::format_hex_rgb(rgb_v);
          *rt_.area_target_code = latter_ui::area_color_hex_param();
          *rt_.area_target_entity_id = latter_ui::light_insp_entity_id();
          *rt_.area_target_action = "set_color";
          *rt_.area_target_level = latter_ui::light_insp_bri_pct();
          rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
          rt_.schedule_disp();
        }
      } else {
        const auto &ly = latter_ui::layout();
        const int sl_card_w = ly.light_slider_card_w();
        const int k_bar_w = ly.light_kelvin_bar_w();
        const int k_bar_x0 = ly.light_kelvin_bar_x0();
        const int b_max_w = ly.light_bri_bar_w();
        const int b_bar_x0 = ly.light_bri_bar_x0();
        if (gest.start_y >= latter_ui::scale_y(120) && gest.start_y <= latter_ui::scale_y(204) &&
            std::abs(tp.x - ly.cx) <= (sl_card_w / 2)) {
          const int k_val = 2700 + ((tp.x - k_bar_x0) * (6500 - 2700)) / std::max(1, k_bar_w);
          latter_ui::light_insp_kelvin() = std::max(2700, std::min(6500, (k_val / 100) * 100));
          *rt_.area_target_entity_id = latter_ui::light_insp_entity_id();
          *rt_.area_target_action = "set_kelvin";
          *rt_.area_target_level = latter_ui::light_insp_kelvin();
          rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
          rt_.schedule_disp();
        } else if (gest.start_y >= latter_ui::scale_y(218) && gest.start_y <= latter_ui::scale_y(302) &&
                   std::abs(tp.x - latter_ui::SCREEN_CX) <= (sl_card_w / 2)) {
          const int b_val = ((tp.x - b_bar_x0) * 100) / std::max(1, b_max_w);
          latter_ui::light_insp_bri_pct() = std::max(5, std::min(100, ((b_val + 2) / 5) * 5));
          *rt_.area_target_entity_id = latter_ui::light_insp_entity_id();
          *rt_.area_target_action = "set_level";
          *rt_.area_target_level = latter_ui::light_insp_bri_pct();
          rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
          rt_.schedule_disp();
        }
      }
    } else if (!gest.swipe_committed && *rt_.gui_page == 6 &&
               gest.last_y > latter_ui::scale_y(88) && gest.last_y < latter_ui::layout().edge_guard_y()) {
      const int dock_top_y = latter_ui::layout().mode_bar_zone_top();
      const int start_card = (*rt_.area_subpage == 0 && gest.start_y < dock_top_y)
                                 ? latter_ui::layout().hit_area_card3(gest.start_x, gest.start_y)
                                 : -1;
      const std::string start_type = (start_card >= 0 && start_card < 3) ? *rt_.area_slot_types[start_card] : "";
      const int start_lvl = (start_card >= 0 && start_card < 3) ? *rt_.area_slot_levels[start_card] : -1;
      const bool is_card_slider_drag =
          (start_card >= 0 && (start_type == "light" || start_type == "cover" || start_type == "fan") &&
           start_lvl >= 0 && gest.start_x > latter_ui::layout().area_card_div_x());

      if (is_card_slider_drag && gest.max_travel >= latter_ui::layout().tap_max()) {
        const std::string s_name = *rt_.area_slot_names[start_card];
        const std::string s_id = *rt_.area_slot_ids[start_card];
        bool &s_act = *rt_.area_slot_actives[start_card];
        int &s_lvl = *rt_.area_slot_levels[start_card];
        if (!s_id.empty()) {
          const int pct = latter_ui::layout().area_slider_pct(tp.x);
          s_act = true;
          s_lvl = pct;
          *rt_.area_target_entity_id = s_id;
          *rt_.area_action_feedback = latter_ui::strip_trailing_paren_state(s_name) + ": " + std::to_string(pct) + "%";
          gate_or_dispatch_area_action(start_type, "set_level", pct, "OPEN COVER PIN", now_rel);
        }
      } else if (*rt_.area_subpage == 0 && !is_card_slider_drag &&
                 std::abs(tp.x - gest.start_x) >= latter_ui::layout().swipe_min() &&
                 std::abs(tp.x - gest.start_x) > std::abs(tp.y - gest.start_y)) {
        const int dx_rel = tp.x - gest.start_x;
        std::string cats[8];
        const int cat_cnt = latter_ui::collect_area_categories(rt_.sw_state(UiSwitchId::AREA_SHOW_ALL_CATEGORY), cats);
        int cur_i = 0;
        for (int i = 0; i < cat_cnt; i++) {
          if (cats[i] == *rt_.area_domain_filter) { cur_i = i; break; }
        }
        if (gest.start_y >= dock_top_y && cat_cnt > 4) {
          const int cur_win = latter_ui::get_mode_bar_start_index(cat_cnt, latter_ui::area_cat_dock_page());
          const int next_win = std::max(0, std::min(cat_cnt - 4, cur_win + ((dx_rel < 0) ? 1 : -1)));
          int next_i = cur_i;
          if (next_win != cur_win) {
            latter_ui::area_cat_dock_page() = next_win;
            if (next_i < next_win) next_i = next_win;
            else if (next_i > next_win + 3) next_i = next_win + 3;
          } else {
            next_i = std::max(0, std::min(cat_cnt - 1, cur_i + ((dx_rel < 0) ? 1 : -1)));
            latter_ui::sync_mode_bar_window(latter_ui::area_cat_dock_page(), next_i, cat_cnt);
          }
          if (next_i != cur_i) {
            *rt_.area_domain_filter = cats[next_i];
            *rt_.area_browse_mode = "entities";
            *rt_.area_browse_page = 1;
            rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
          }
          rt_.schedule_disp();
        } else if (cat_cnt > 1) {
          const int next_i = (dx_rel < 0) ? ((cur_i + 1) % cat_cnt) : ((cur_i + cat_cnt - 1) % cat_cnt);
          *rt_.area_domain_filter = cats[next_i];
          latter_ui::sync_mode_bar_window(latter_ui::area_cat_dock_page(), next_i, cat_cnt);
          *rt_.area_browse_mode = "entities";
          *rt_.area_browse_page = 1;
          rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
          rt_.schedule_disp();
        }
      } else if (gest.max_travel < latter_ui::layout().tap_max()) {
        if (latter_ui::is_right_chevron_zone(tp) && *rt_.area_subpage != 2 &&
            ((*rt_.area_subpage == 1 && *rt_.area_list_total_pages > 1) ||
             (*rt_.area_subpage == 0 && *rt_.area_browse_total_pages > 1))) {
          const int step_d = (tp.y < latter_ui::SCREEN_CY) ? -1 : 1;
          if (*rt_.area_subpage == 1 && *rt_.area_list_total_pages > 1) {
            const int next_pg = std::max(1, std::min(*rt_.area_list_total_pages, *rt_.area_list_page + step_d));
            if (next_pg != *rt_.area_list_page) {
              *rt_.area_list_page = next_pg;
              rt_.cmd(UiHostCmd::FETCH_AREA_LIST_BROWSE);
              rt_.schedule_disp();
            }
          } else if (*rt_.area_subpage == 0 && *rt_.area_browse_total_pages > 1) {
            const int next_pg = std::max(1, std::min(*rt_.area_browse_total_pages, *rt_.area_browse_page + step_d));
            if (next_pg != *rt_.area_browse_page) {
              *rt_.area_browse_page = next_pg;
              rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
              rt_.schedule_disp();
            }
          }
        } else if (*rt_.area_subpage == 0 &&
                   tp.y >= dock_top_y &&
                   tp.y <= latter_ui::layout().app_dock_y + latter_ui::layout().dock_r + 16 &&
                   tp.x >= latter_ui::SCREEN_CX - latter_ui::scale_x(215) &&
                   tp.x <= latter_ui::SCREEN_CX + latter_ui::scale_x(215)) {
          std::string cats[8];
          const int cat_cnt = latter_ui::collect_area_categories(rt_.sw_state(UiSwitchId::AREA_SHOW_ALL_CATEGORY), cats);
          int win_xs[4] = {0, 0, 0, 0};
          bool has_l = false, has_r = false;
          latter_ui::get_mode_bar_window_xs(cat_cnt, latter_ui::area_cat_dock_page(), latter_ui::SCREEN_CX, win_xs, has_l, has_r);
          const int start_idx = latter_ui::get_mode_bar_start_index(cat_cnt, latter_ui::area_cat_dock_page());
          const int vis_count = std::min(4, cat_cnt - start_idx);
          int cur_i = 0;
          for (int i = 0; i < cat_cnt; i++) {
            if (cats[i] == *rt_.area_domain_filter) { cur_i = i; break; }
          }
          if (has_l && vis_count > 0 && tp.x < win_xs[0] - latter_ui::layout().dock_r) {
            const int next_win = std::max(0, start_idx - 1);
            latter_ui::area_cat_dock_page() = next_win;
            if (cur_i > next_win + 3) {
              *rt_.area_domain_filter = cats[next_win + 3];
              *rt_.area_browse_mode = "entities";
              *rt_.area_browse_page = 1;
              rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
            }
            rt_.schedule_disp();
          } else if (has_r && vis_count > 0 && tp.x > win_xs[vis_count - 1] + latter_ui::layout().dock_r) {
            const int next_win = std::min(cat_cnt - 4, start_idx + 1);
            latter_ui::area_cat_dock_page() = next_win;
            if (cur_i < next_win) {
              *rt_.area_domain_filter = cats[next_win];
              *rt_.area_browse_mode = "entities";
              *rt_.area_browse_page = 1;
              rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
            }
            rt_.schedule_disp();
          } else {
            int best_i = -1;
            int best_dist = (vis_count < 4) ? latter_ui::scale_x(50) : latter_ui::scale_x(42);
            for (int i = 0; i < vis_count; i++) {
              const int d = std::abs(tp.x - win_xs[i]);
              if (d < best_dist) { best_dist = d; best_i = i; }
            }
            if (best_i >= 0) {
              const int chosen_idx = start_idx + best_i;
              const std::string new_flt = cats[chosen_idx];
              const int prev_win = latter_ui::area_cat_dock_page();
              latter_ui::sync_mode_bar_window(latter_ui::area_cat_dock_page(), chosen_idx, cat_cnt);
              if (*rt_.area_domain_filter != new_flt) {
                *rt_.area_domain_filter = new_flt;
                *rt_.area_browse_mode = "entities";
                *rt_.area_browse_page = 1;
                rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
                rt_.schedule_disp();
              } else if (latter_ui::area_cat_dock_page() != prev_win) {
                rt_.schedule_disp();
              }
            }
          }
        } else if (*rt_.area_subpage == 1) {
          const int slot_idx = latter_ui::layout().hit_slot5(tp, 75, 385);
          if (slot_idx >= 0 && slot_idx < 5) {
            const std::string s_name = *rt_.area_list_names[slot_idx];
            const std::string s_id = *rt_.area_list_ids[slot_idx];
            if (!s_id.empty()) {
              *rt_.active_area_id = s_id;
              *rt_.active_area_name = latter_ui::strip_trailing_paren_state(s_name);
              latter_ui::area_cat_dock_page() = 0;
              *rt_.area_domain_filter = latter_ui::resolve_preferred_area_category(
                  rt_.select_opt(UiSelectId::AREA_DEFAULT_CATEGORY),
                  rt_.sw_state(UiSwitchId::AREA_SHOW_ALL_CATEGORY));
              *rt_.area_subpage = 0;
              *rt_.area_browse_mode = "entities";
              *rt_.area_browse_page = 1;
              *rt_.area_action_feedback = "Area: " + *rt_.active_area_name;
              *rt_.area_feedback_timer = now_rel;
              rt_.cmd(UiHostCmd::SELECT_ACTIVE_AREA_LATTER);
              rt_.schedule_disp();
            }
          }
        } else if (*rt_.area_subpage == 0) {
          const int card_idx = latter_ui::layout().hit_area_card3(tp.x, tp.y);
          if (card_idx >= 0 && card_idx < 3) {
            const std::string s_name = *rt_.area_slot_names[card_idx];
            const std::string s_id = *rt_.area_slot_ids[card_idx];
            const std::string s_type = *rt_.area_slot_types[card_idx];
            bool &s_act = *rt_.area_slot_actives[card_idx];
            int &s_lvl = *rt_.area_slot_levels[card_idx];

            if (!s_id.empty() && s_type != "text") {
              if (s_type == "media_player") {
                *rt_.target_speaker_id = s_id;
                *rt_.target_speaker_name = latter_ui::strip_trailing_paren_state(s_name);
                *rt_.gui_page = 2;
                rt_.cmd(UiHostCmd::SPEAKER_CONTROL_TARGET);
              } else {
                const std::string clean_lbl = latter_ui::strip_trailing_paren_state(s_name);
                if (s_type == "light" && latter_ui::layout().is_area_light_orb(tp.x)) {
                  latter_ui::light_insp_entity_id() = s_id;
                  latter_ui::light_insp_name() = clean_lbl;
                  latter_ui::light_insp_bri_pct() = (s_lvl > 0) ? s_lvl : 80;
                  *rt_.area_subpage = 2;
                  rt_.schedule_disp();
                } else {
                  *rt_.area_target_entity_id = s_id;
                  const bool has_slider = ((s_type == "light" || s_type == "cover" || s_type == "fan") && s_lvl >= 0);
                  if (has_slider && tp.x > latter_ui::layout().area_card_div_x()) {
                    const int pct = latter_ui::layout().area_slider_pct(tp.x);
                    s_act = true;
                    s_lvl = pct;
                    *rt_.area_action_feedback = clean_lbl + ": " + std::to_string(pct) + "%";
                    gate_or_dispatch_area_action(s_type, "set_level", pct, "OPEN COVER PIN", now_rel);
                  } else {
                    const bool prev_act = s_act;
                    const int prev_lvl = s_lvl;
                    s_act = !s_act;
                    int next_lvl = s_act ? 100 : 0;
                    std::string next_act = "toggle";
                    const char *pin_title = "ENTER SECURITY PIN";
                    if (s_type == "lock") {
                      next_act = s_act ? "lock" : "unlock";
                      *rt_.area_action_feedback = clean_lbl + (s_act ? ": Locked" : ": Unlocked");
                      pin_title = "UNLOCK DOOR PIN";
                    } else if (s_type == "alarm_control_panel") {
                      next_act = s_act ? "arm_home" : "disarm";
                      *rt_.area_action_feedback = clean_lbl + (s_act ? ": Armed Home" : ": Disarmed");
                      pin_title = "DISARM ALARM PIN";
                    } else if (s_type == "scene" || s_type == "script") {
                      s_act = true;
                      next_lvl = 100;
                      next_act = "toggle";
                      *rt_.area_action_feedback = "Activated: " + clean_lbl;
                    } else if (s_type == "cover") {
                      s_lvl = s_act ? 100 : 0;
                      next_lvl = s_lvl;
                      next_act = s_act ? "turn_on" : "turn_off";
                      *rt_.area_action_feedback = clean_lbl + (s_act ? ": Open" : ": Closed");
                      pin_title = "OPEN COVER PIN";
                    } else {
                      if (s_act && s_lvl <= 0) s_lvl = 100;
                      next_lvl = s_act ? s_lvl : 0;
                      next_act = "toggle";
                      *rt_.area_action_feedback = clean_lbl + (s_act ? ": On" : ": Off");
                    }
                    if (!gate_or_dispatch_area_action(s_type, next_act, next_lvl, pin_title, now_rel)) {
                      s_act = prev_act;
                      s_lvl = prev_lvl;
                    }
                  }
                }
              }
            }
          }
        }
      }
    } else if (!gest.swipe_committed && *rt_.gui_page == 9 && gest.last_y < latter_ui::layout().edge_guard_y()) {
      if (gest.start_y < latter_ui::layout().home_zone_y() &&
          std::abs(dx) >= latter_ui::layout().page_swipe() && std::abs(dx) > std::abs(dy)) {
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_rel;
        latter_ui::whole_home_tab() = (dx < 0)
            ? ((latter_ui::whole_home_tab() + 1) % 4)
            : ((latter_ui::whole_home_tab() + 3) % 4);
        rt_.schedule_disp();
      } else if (gest.max_travel < latter_ui::layout().tap_max()) {
        if (tp.y >= latter_ui::layout().mode_bar_zone_top() &&
            tp.y <= latter_ui::layout().app_dock_y + latter_ui::layout().dock_r + 16 &&
            tp.x >= latter_ui::SCREEN_CX - latter_ui::scale_x(205) &&
            tp.x <= latter_ui::SCREEN_CX + latter_ui::scale_x(205)) {
          int wh_xs[4] = {0, 0, 0, 0};
          bool d_l = false, d_r = false;
          latter_ui::get_home_dock_window_xs(4, 0, latter_ui::SCREEN_CX, wh_xs, d_l, d_r);
          int new_tab = -1;
          int best_d = latter_ui::scale_x(46);
          for (int i = 0; i < 4; i++) {
            const int d = std::abs(tp.x - wh_xs[i]);
            if (d < best_d) { best_d = d; new_tab = i; }
          }
          if (new_tab >= 0) {
            latter_ui::whole_home_tab() = new_tab;
            rt_.schedule_disp();
          }
        } else if (latter_ui::whole_home_tab() == 0) {
          const int a_card = latter_ui::layout().hit_area_card3(tp.x, tp.y);
          if (a_card >= 0) {
            const std::string prev_st = *rt_.area_alarm_state;
            std::string svc = "arm_away";
            if (a_card == 0) { svc = "disarm"; *rt_.area_alarm_state = "disarmed"; *rt_.area_action_feedback = "Disarmed"; }
            else if (a_card == 1) { svc = "arm_home"; *rt_.area_alarm_state = "armed_home"; *rt_.area_action_feedback = "Armed Home"; }
            else { svc = "arm_away"; *rt_.area_alarm_state = "armed_away"; *rt_.area_action_feedback = "Armed Away"; }
            if (!gate_or_dispatch_alarm_action(svc, now_rel)) {
              *rt_.area_alarm_state = prev_st;
            }
          }
        } else if (latter_ui::whole_home_tab() == 1) {
          if (tp.y >= latter_ui::scale_y(112) && tp.y <= latter_ui::scale_y(200)) {
            if (latter_ui::sim_thermostat_target_f() < 40) latter_ui::sim_thermostat_target_f() = 70;
            if (tp.x >= latter_ui::SCREEN_CX - latter_ui::scale_x(155) &&
                tp.x <= latter_ui::SCREEN_CX - latter_ui::scale_x(70)) {
              latter_ui::sim_thermostat_target_f() = std::max(55, latter_ui::sim_thermostat_target_f() - 1);
            } else if (tp.x >= latter_ui::SCREEN_CX + latter_ui::scale_x(70) &&
                       tp.x <= latter_ui::SCREEN_CX + latter_ui::scale_x(155)) {
              latter_ui::sim_thermostat_target_f() = std::min(88, latter_ui::sim_thermostat_target_f() + 1);
            }
            *rt_.area_target_entity_id = "whole_home.thermostat";
            *rt_.area_target_action = "set_level";
            *rt_.area_target_level = latter_ui::sim_thermostat_target_f();
            rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
            rt_.schedule_disp();
          } else if (tp.y >= latter_ui::scale_y(234) && tp.y <= latter_ui::scale_y(298) &&
                     tp.x >= latter_ui::SCREEN_CX - latter_ui::scale_x(172) &&
                     tp.x <= latter_ui::SCREEN_CX + latter_ui::scale_x(172)) {
            const int s86 = latter_ui::scale_x(86);
            const char *m_id = (tp.x < latter_ui::SCREEN_CX - s86)
                                   ? "heat"
                                   : ((tp.x < latter_ui::SCREEN_CX) ? "cool" : ((tp.x < latter_ui::SCREEN_CX + s86) ? "auto" : "off"));
            latter_ui::sim_thermostat_mode() = m_id;
            *rt_.area_target_entity_id = "whole_home.thermostat";
            *rt_.area_target_action = m_id;
            rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
            rt_.schedule_disp();
          }
        } else if (latter_ui::whole_home_tab() == 2 && std::abs(tp.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(160)) {
          if (tp.y >= latter_ui::scale_y(112) && tp.y < latter_ui::scale_y(204)) {
            const auto prev_g = latter_ui::wh_garage_state();
            const bool want_open = (prev_g != latter_ui::GarageState::OPEN);
            latter_ui::wh_garage_state() = want_open ? latter_ui::GarageState::OPEN : latter_ui::GarageState::CLOSED;
            *rt_.area_target_entity_id = "whole_home.garage";
            *rt_.area_action_feedback = want_open ? "Opening Garage..." : "Closing Garage...";
            if (!gate_or_dispatch_area_action("cover", want_open ? "turn_on" : "turn_off", want_open ? 100 : 0,
                                              "OPEN GARAGE PIN", now_rel)) {
              latter_ui::wh_garage_state() = prev_g;
            }
          } else if (tp.y >= latter_ui::scale_y(208) && tp.y < latter_ui::scale_y(304)) {
            const auto prev_l = latter_ui::wh_lock_state();
            const bool want_lock = (prev_l != latter_ui::LockState::LOCKED);
            latter_ui::wh_lock_state() = want_lock ? latter_ui::LockState::LOCKED : latter_ui::LockState::UNLOCKED;
            *rt_.area_target_entity_id = "whole_home.lock";
            *rt_.area_action_feedback = want_lock ? "Front Door Locked" : "Front Door Unlocked";
            if (!gate_or_dispatch_area_action("lock", want_lock ? "lock" : "unlock", want_lock ? 100 : 0,
                                              "UNLOCK DOOR PIN", now_rel)) {
              latter_ui::wh_lock_state() = prev_l;
            }
          }
        } else if (latter_ui::whole_home_tab() == 3) {
          const int v_card = latter_ui::layout().hit_area_card3(tp.x, tp.y);
          if (v_card >= 0) {
            *rt_.area_target_entity_id = "whole_home.vacuum";
            if (v_card == 0) { latter_ui::sim_vacuum_state() = "Cleaning"; *rt_.area_target_action = "start"; *rt_.area_action_feedback = "Vacuum Started"; }
            else if (v_card == 1) { latter_ui::sim_vacuum_state() = "Paused"; *rt_.area_target_action = "pause"; *rt_.area_action_feedback = "Vacuum Paused"; }
            else { latter_ui::sim_vacuum_state() = "Docked"; *rt_.area_target_action = "dock"; *rt_.area_action_feedback = "Returning to Dock"; }
            *rt_.area_feedback_timer = now_rel;
            rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
            rt_.schedule_disp();
          }
        }
      }
    } else if (!gest.swipe_committed && *rt_.gui_page == 8 && *rt_.tools_subpage == 0 &&
               !*rt_.menu_scroll_active && gest.last_y < latter_ui::scale_y(408)) {
      if (gest.max_travel < latter_ui::layout().tap_max()) {
        if (latter_ui::is_right_chevron_zone(tp)) {
          const int target_pg = (*rt_.tools_menu_page == 0) ? 1 : 0;
          *rt_.menu_scroll_active = true;
          *rt_.menu_scroll_dir = (target_pg == 1) ? 1 : -1;
          *rt_.menu_scroll_target_page = target_pg;
          rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
        } else {
          const int row = latter_ui::layout().hit_tools_row(tp.y);
          const int col = latter_ui::layout().hit_tools_col(tp.x);
          if (row >= 0 && col >= 0) {
            int target_subpage = 0;
#if LATTER_HAS_IMU
            if (*rt_.tools_menu_page == 0) {
              static constexpr int P0_MAP[3][2] = {{1, 2}, {3, 7}, {5, 6}};
              target_subpage = P0_MAP[row][col];
            } else {
              static constexpr int P1_MAP[3][2] = {{4, 8}, {9, 10}, {12, 0}};
              target_subpage = P1_MAP[row][col];
            }
#else
            if (*rt_.tools_menu_page == 0) {
              static constexpr int P0_MAP[3][2] = {{1, 2}, {3, 5}, {6, 9}};
              target_subpage = P0_MAP[row][col];
            } else {
              static constexpr int P1_MAP[3][2] = {{10, 12}, {4, 8}, {0, 0}};
              target_subpage = P1_MAP[row][col];
            }
#endif
            if (target_subpage > 0) {
              *rt_.tools_subpage = target_subpage;
              if (*rt_.tools_subpage == 5) {
                rt_.cmd(UiHostCmd::FLASHLIGHT_FULL_BRIGHTNESS);
              } else if (*rt_.tools_subpage == 6) {
                latter_ui::reset_calculator_state();
              } else if (*rt_.tools_subpage == 9) {
                latter_ui::cal_detail_mode() = 0;
                latter_ui::cal_quick_add_open() = false;
                *rt_.cal_browse_page = 1;
                rt_.cmd(UiHostCmd::FETCH_CALENDAR_BROWSE);
              } else if (*rt_.tools_subpage == 12) {
                rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
              }
              rt_.schedule_disp();
            }
          }
        }
      }
    } else if (!gest.swipe_committed && *rt_.gui_page == 8 && *rt_.tools_subpage == 9 &&
               latter_ui::cal_detail_mode() == 2 && gest.start_y < latter_ui::scale_y(404)) {
      if (std::abs(dx) >= latter_ui::layout().page_swipe() || std::abs(dy) >= latter_ui::layout().page_swipe()) {
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_rel;
        if (std::abs(dx) >= std::abs(dy)) {
          const int step_x = (std::abs(dx) >= latter_ui::layout().fast_pan()) ? 2 : 1;
          latter_ui::cal_map_pan_x() = std::clamp(latter_ui::cal_map_pan_x() + ((dx < 0) ? step_x : -step_x), -4, 4);
        } else {
          const int step_y = (std::abs(dy) >= latter_ui::layout().fast_pan()) ? 2 : 1;
          latter_ui::cal_map_pan_y() = std::clamp(latter_ui::cal_map_pan_y() + ((dy < 0) ? step_y : -step_y), -4, 4);
        }
        rt_.cmd(UiHostCmd::FETCH_CALENDAR_MAP_SNAPSHOT);
      } else if (gest.max_travel < latter_ui::layout().tap_max()) {
        if (latter_ui::layout().hit_map_zoom_out(tp.x, tp.y)) {
          if (latter_ui::cal_map_zoom() > 10) {
            latter_ui::cal_map_zoom()--;
            latter_ui::cal_map_pan_x() /= 2;
            latter_ui::cal_map_pan_y() /= 2;
            rt_.schedule_disp();
            rt_.cmd(UiHostCmd::FETCH_CALENDAR_MAP_SNAPSHOT);
          }
        } else if (latter_ui::layout().hit_map_zoom_in(tp.x, tp.y)) {
          if (latter_ui::cal_map_zoom() < 18) {
            latter_ui::cal_map_zoom()++;
            latter_ui::cal_map_pan_x() = std::clamp(latter_ui::cal_map_pan_x() * 2, -4, 4);
            latter_ui::cal_map_pan_y() = std::clamp(latter_ui::cal_map_pan_y() * 2, -4, 4);
            rt_.schedule_disp();
            rt_.cmd(UiHostCmd::FETCH_CALENDAR_MAP_SNAPSHOT);
          }
        } else if (tp.y <= latter_ui::scale_y(68) && std::abs(tp.x - latter_ui::SCREEN_CX) <= latter_ui::scale_x(140)) {
          if (latter_ui::cal_map_pan_x() != 0 || latter_ui::cal_map_pan_y() != 0) {
            latter_ui::cal_map_pan_x() = 0;
            latter_ui::cal_map_pan_y() = 0;
            rt_.cmd(UiHostCmd::FETCH_CALENDAR_MAP_SNAPSHOT);
          }
        }
      }
    } else if (!gest.swipe_committed && *rt_.gui_page == 7 && *rt_.remote_subpage == 1 &&
               gest.max_travel < latter_ui::layout().tap_max() &&
               gest.last_y >= latter_ui::scale_y(84) && gest.last_y < latter_ui::scale_y(404)) {
      if (hit != nullptr && hit->widget_id == WidgetId::REMOTE_PRIMARY_PWR) {
        rt_.cmd(UiHostCmd::REMOTE_SEND_KEY, 9, -1);
        rt_.schedule_disp();
      } else if (hit != nullptr && hit->widget_id >= WidgetId::REMOTE_SEC_PWR && hit->widget_id < WidgetId::REMOTE_SEC_PWR + 4) {
        rt_.cmd(UiHostCmd::REMOTE_SEND_KEY, 9, hit->param);
        rt_.schedule_disp();
      } else if (hit != nullptr && hit->widget_id >= WidgetId::REMOTE_SLOT_CARD && hit->widget_id < WidgetId::REMOTE_SLOT_CARD + 4) {
        *rt_.active_remote_slot = hit->param;
        *rt_.remote_action_feedback = "";
        *rt_.remote_subpage = 0;
        rt_.cmd(UiHostCmd::REMOTE_SELECT_SLOT_LATTER, hit->param + 1);
        rt_.schedule_disp();
      } else {
        const int r_hw = latter_ui::scale_x(143);
        if (tp.x >= latter_ui::SCREEN_CX - r_hw && tp.x <= latter_ui::SCREEN_CX + r_hw &&
            tp.y >= latter_ui::layout().slot5_y(0) - latter_ui::scale_y(28) &&
            tp.y < latter_ui::layout().slot5_y(0) + latter_ui::scale_y(30)) {
          rt_.cmd(UiHostCmd::REMOTE_SEND_KEY, 9, -1);
          rt_.schedule_disp();
        }
      }
    } else if (!gest.swipe_committed && *rt_.gui_page == 8 && *rt_.tools_subpage == 12 &&
               gest.max_travel < latter_ui::layout().tap_max() && gest.last_y < latter_ui::scale_y(404)) {
      std::string c_ids[latter_ui::MAX_CAMERA_SLOTS], c_names[latter_ui::MAX_CAMERA_SLOTS];
      const int c_total = latter_ui::parse_cameras_summary_list(latter_ui::area_ctx_state().cameras_summary, c_ids, c_names);
      if (latter_ui::cam_quick_reply_open()) {
        const int q_row = latter_ui::layout().hit_slot5(tp, latter_ui::SCREEN_CX - 148, latter_ui::SCREEN_CX + 148);
        if (q_row == 0) {
          latter_ui::cam_quick_reply_open() = false;
          gate_or_dispatch_doorbell_unlock(now_rel, false);
        } else if (q_row >= 1 && q_row <= 3) {
          static constexpr const char *kDoorbellReplies[3] = {
              "Please leave the package at the door, thank you!",
              "We'll be right there!",
              "No soliciting, thank you!"
          };
          latter_ui::cam_quick_reply_open() = false;
          *rt_.cam_intercom_action = "quick_reply";
          *rt_.cam_intercom_message = kDoorbellReplies[q_row - 1];
          latter_ui::cam_intercom_status() = "REPLY SENT";
          *rt_.cam_intercom_status_ms = now_rel;
          rt_.cmd(UiHostCmd::SEND_DOORBELL_INTERCOM_CMD);
          rt_.schedule_disp();
        } else if (q_row == 4) {
          latter_ui::cam_landscape_mode() = !latter_ui::cam_landscape_mode();
          if (!latter_ui::cam_landscape_mode()) {
            latter_ui::cam_noise_idx() = (latter_ui::cam_noise_idx() + 1) % 3;
            static constexpr const char *kNoiseCodes[3] = {"strong", "light", "off"};
            *rt_.cam_intercom_noise_str = kNoiseCodes[latter_ui::cam_noise_idx()];
            *rt_.cam_intercom_action = "set_noise_reduction";
            rt_.cmd(UiHostCmd::SEND_DOORBELL_INTERCOM_CMD);
          }
          rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
          rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
          rt_.schedule_disp();
        }
      } else if (tp.y <= latter_ui::scale_y(68)) {
        if (!LATTER_ROUND_DISPLAY && (latter_ui::SCREEN_W != latter_ui::SCREEN_H) &&
            tp.x >= (latter_ui::SCREEN_W - 64)) {
          latter_ui::cam_landscape_mode() = !latter_ui::cam_landscape_mode();
          latter_ui::cam_intercom_status() = latter_ui::cam_landscape_mode() ? "90° LANDSCAPE" : "PORTRAIT VIEW";
          *rt_.cam_intercom_status_ms = now_rel;
          rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
          rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
          rt_.schedule_disp();
        } else if (c_total > 1 && tp.x < latter_ui::SCREEN_CX - latter_ui::scale_x(55)) {
          latter_ui::cam_carousel_idx() = latter_ui::step_wrap(latter_ui::cam_carousel_idx(), -1, c_total);
          rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
          rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
          rt_.schedule_disp();
        } else if (c_total > 1 && tp.x > latter_ui::SCREEN_CX + latter_ui::scale_x(55)) {
          latter_ui::cam_carousel_idx() = latter_ui::step_wrap(latter_ui::cam_carousel_idx(), 1, c_total);
          rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
          rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
          rt_.schedule_disp();
        } else {
          rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
          rt_.schedule_disp();
        }
      } else if (tp.y >= latter_ui::scale_y(342) && tp.y <= latter_ui::scale_y(398)) {
        if (tp.x < latter_ui::SCREEN_CX - latter_ui::scale_x(48)) {
          if (latter_ui::cam_call_active()) {
            rt_.cmd(UiHostCmd::STOP_DOORBELL_CALL);
          } else {
            latter_ui::cam_listen_active() = !latter_ui::cam_listen_active();
            *rt_.cam_intercom_action = latter_ui::cam_listen_active() ? "listen_start" : "listen_stop";
            latter_ui::cam_intercom_status() = latter_ui::cam_listen_active() ? "DOOR AUDIO ON" : "AUDIO MUTED";
            *rt_.cam_intercom_status_ms = now_rel;
            rt_.cmd(UiHostCmd::SEND_DOORBELL_INTERCOM_CMD);
            if (!latter_ui::cam_listen_active()) {
              rt_.cmd(UiHostCmd::STOP_DOORBELL_AUDIO);
            }
            rt_.schedule_disp();
          }
        } else if (tp.x <= latter_ui::SCREEN_CX + latter_ui::scale_x(48)) {
          if (!latter_ui::cam_call_active()) {
            rt_.cmd(UiHostCmd::START_DOORBELL_CALL);
          } else if (latter_ui::cam_intercom_ptt_active()) {
            rt_.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
            rt_.cmd(UiHostCmd::RESUME_DOORBELL_LISTEN);
          } else {
            rt_.cmd(UiHostCmd::START_DOORBELL_PTT);
          }
        } else {
          latter_ui::cam_quick_reply_open() = true;
          rt_.schedule_disp();
        }
      } else {
        rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
        rt_.schedule_disp();
      }
    } else if (!gest.swipe_committed && *rt_.gui_page == 8 && *rt_.tools_subpage == 9 &&
               latter_ui::cal_detail_mode() == 0 && !latter_ui::cal_quick_add_open() &&
               gest.max_travel < latter_ui::layout().tap_max() &&
               gest.last_y >= latter_ui::scale_y(80) && gest.last_y < latter_ui::scale_y(404)) {
      const int slot_idx = latter_ui::layout().hit_slot5(tp, 75, 385);
      if (slot_idx >= 0 && slot_idx < 5) {
        const std::string s_name = *rt_.cal_slot_names[slot_idx];
        const std::string s_id = *rt_.cal_slot_ids[slot_idx];
        if (!s_name.empty()) {
          latter_ui::parse_calendar_slot_meta(
              s_name, s_id,
              latter_ui::cal_detail_time(),
              latter_ui::cal_detail_title(),
              latter_ui::cal_detail_location(),
              latter_ui::cal_detail_map_url());
          latter_ui::cal_detail_mode() = 1;
          rt_.schedule_disp();
        }
      }
    }
  }

  if (rel_t0_us > 0ULL) {
    const uint32_t rel_exec_us = static_cast<uint32_t>(latter_ui::prof_now_us() - rel_t0_us);
    const int end_ctx = latter_ui::compute_touch_context(
        *rt_.gui_page, *rt_.home_subpage, *rt_.music_overlay_mode, *rt_.active_game,
        *rt_.system_menu_mode, *rt_.sky_weather_subpage, *rt_.area_subpage,
        *rt_.remote_subpage, *rt_.tools_subpage, latter_ui::whole_home_tab());
    const int dx_p = gest.last_x - gest.start_x;
    const int dy_p = gest.last_y - gest.start_y;
    const uint32_t dur_p = (now_rel >= gest.start_ms) ? (now_rel - gest.start_ms) : 0U;
    LATTER_PROF_LOGI(
        "[PROF:TOUCH_UP] start=(%d,%d) end=(%d,%d) dx=%d dy=%d travel=%d samples=%d dur=%lums swipe=%d ctx=%d->%d exec=%luus",
        gest.start_x, gest.start_y, gest.last_x, gest.last_y,
        dx_p, dy_p, gest.max_travel, gest.sample_count,
        static_cast<unsigned long>(dur_p), static_cast<int>(gest.swipe_committed),
        gest.start_context, end_ctx, static_cast<unsigned long>(rel_exec_us));
  }
  gest.active = false;
  ESP_LOGD("touch", "<<< Touch Released (page=%d)", *rt_.gui_page);
}

void TouchGestureEngine::on_input_event(const std::string &action, int delta, uint32_t now_in) {
  if (!rt_.bound) return;
  if (*rt_.display_power_state != 0 || !rt_.panel_is_on) {
    latter_ui::note_wake_reason(latter_ui::WakeReason::TOUCH);
    rt_.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    return;
  }
  rt_.cmd(UiHostCmd::SCREEN_SLEEP_TIMER);
  latter_ui::activate_ui_focus(now_in);
  if (*rt_.va_active || !rt_.va_status_text->empty()) {
    rt_.cmd(UiHostCmd::CANCEL_VOICE_ASSISTANT);
    return;
  }
  if (*rt_.notif_active) {
    rt_.cmd(UiHostCmd::DISMISS_NOTIFICATION);
    return;
  }
  if (*rt_.alarm_ringing || *rt_.timer_ringing || *rt_.find_my_active) {
    *rt_.alarm_ringing = false;
    *rt_.timer_ringing = false;
    *rt_.find_my_active = false;
    rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
    rt_.schedule_disp();
    return;
  }

  std::string act = action;
  for (char &c : act) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  int step = delta;
  if (step == 0) {
    if (act == "next" || act == "right" || act == "down" || act == "cw") step = 1;
    else if (act == "prev" || act == "left" || act == "up" || act == "ccw") step = -1;
  }
  if (act == "home") {
    latter_ui::ui_focus_index() = 0;
    navigate_home(now_in);
    return;
  }
  if (act == "back") {
    if (ui::pin_pad_state().open) {
      ui::pin_pad_scrub();
      latter_ui::ui_focus_index() = 0;
      mark_dirty_full();
      rt_.schedule_disp();
      return;
    }
    if (*rt_.gui_page == 8 && *rt_.tools_subpage > 0) {
      if (*rt_.tools_subpage == 12) {
        if (latter_ui::cam_call_active() || latter_ui::cam_listen_active()) {
          rt_.cmd(UiHostCmd::STOP_DOORBELL_CALL);
        }
        rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
        if (rt_.current_loaded_camera_url != nullptr) rt_.current_loaded_camera_url->clear();
      }
      if (latter_ui::tools_direct_home()) {
        latter_ui::tools_direct_home() = false;
        *rt_.tools_subpage = 0;
        *rt_.gui_page = 1;
      } else {
        *rt_.tools_subpage = 0;
      }
    } else if (*rt_.gui_page == 3 && *rt_.active_game > 0) {
      *rt_.active_game = 0;
    } else if (*rt_.gui_page == 2 && *rt_.music_overlay_mode > 0) {
      *rt_.music_overlay_mode = 0;
    } else if (*rt_.gui_page == 4 && *rt_.system_menu_mode > 0) {
      *rt_.system_menu_mode = 0;
    } else if (*rt_.gui_page == 6 && *rt_.area_subpage > 0) {
      if (*rt_.area_subpage == 1 && rt_.active_area_id->empty()) {
        *rt_.area_subpage = 0;
        *rt_.gui_page = 1;
        *rt_.home_subpage = 0;
      } else {
        *rt_.area_subpage = 0;
      }
    } else if (*rt_.gui_page == 7 && *rt_.remote_subpage > 0) {
      *rt_.remote_subpage = 0;
    } else {
      *rt_.gui_page = 1;
      *rt_.home_subpage = 0;
    }
    latter_ui::ui_focus_index() = 0;
    rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
    rt_.schedule_disp();
    return;
  }
  if (step != 0) {
    int &f_idx = latter_ui::ui_focus_index();
    f_idx = std::max(0, f_idx + step);
    if (!ui::pin_pad_state().open) {
      if (*rt_.gui_page == 5) {
        *rt_.sky_weather_subpage = f_idx % 4;
      } else if (*rt_.gui_page == 9) {
        latter_ui::whole_home_tab() = f_idx % 4;
      } else if (*rt_.gui_page == 8 && *rt_.tools_subpage == 12) {
        std::string cam_ids[latter_ui::MAX_CAMERA_SLOTS], cam_names[latter_ui::MAX_CAMERA_SLOTS];
        const int c_cnt = std::max(1, latter_ui::parse_cameras_summary_list(latter_ui::area_ctx_state().cameras_summary, cam_ids, cam_names));
        latter_ui::cam_carousel_idx() = (latter_ui::cam_carousel_idx() + step + c_cnt) % c_cnt;
        rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
      }
    }
    rt_.schedule_disp();
    return;
  }
  if (act == "select" || act == "ok" || act == "click") {
    const int f_idx = std::max(0, latter_ui::ui_focus_index());
    const int hit_focus_cnt = frame_hits().focus_count();
    if (hit_focus_cnt > 0 &&
        (ui::pin_pad_state().open ||
         (*rt_.gui_page == 4 && (*rt_.system_menu_mode == 4 || *rt_.system_menu_mode == 6)) ||
         (*rt_.gui_page == 3 && *rt_.active_game > 0) ||
         (*rt_.gui_page == 7 && *rt_.remote_subpage == 1) ||
         (*rt_.gui_page == 8 && (*rt_.tools_subpage == 1 || *rt_.tools_subpage == 2)))) {
      const HitRegion *foc = frame_hits().focus_at(f_idx);
      if (foc != nullptr) {
        on_touch_down(foc->cx(), foc->cy(), now_in);
        if (latter_ui::touch_gesture().active) {
          on_touch_up(now_in);
        }
        latter_ui::activate_ui_focus(now_in);
        return;
      }
    }

    if (*rt_.gui_page == 1) {
      int dock_apps[8] = {0, 0, 0, 0, 0, 0, 0, 0};
      const int dock_count = latter_ui::collect_home_dock_apps(dock_apps);
      if (dock_count > 0) {
        const int start_idx = latter_ui::get_home_dock_start_index(dock_count, latter_ui::home_dock_page());
        const int vis_count = std::min(4, dock_count - start_idx);
        const int chosen_app = dock_apps[start_idx + (f_idx % std::max(1, vis_count))];
        if (chosen_app == 12) {
          *rt_.gui_page = 8;
          *rt_.tools_subpage = 12;
          latter_ui::tools_direct_home() = true;
          rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
        } else {
          *rt_.gui_page = chosen_app;
        }
      }
      latter_ui::ui_focus_index() = 0;
      if (*rt_.gui_page == 6) {
        if (rt_.active_area_id->empty()) {
          *rt_.area_subpage = 1;
          *rt_.area_list_page = 1;
          rt_.cmd(UiHostCmd::FETCH_AREA_LIST_BROWSE);
        } else {
          rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
        }
      }
    } else if (*rt_.gui_page == 2) {
      if (*rt_.music_overlay_mode == 0) {
        const int m_act = f_idx % 5;
        if (m_act == 0) rt_.cmd(UiHostCmd::MUSIC_PREV_TRACK);
        else if (m_act == 1) rt_.cmd(UiHostCmd::TOGGLE_HA_MUSIC);
        else if (m_act == 2) rt_.cmd(UiHostCmd::MUSIC_NEXT_TRACK);
        else if (m_act == 3) { *rt_.music_overlay_mode = 2; *rt_.selected_category_mode = "favorites"; rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE); }
        else { *rt_.music_overlay_mode = 1; *rt_.selected_category_mode = "speakers"; rt_.cmd(UiHostCmd::FETCH_MUSIC_BROWSE); }
      } else {
        rt_.cmd(UiHostCmd::PLAY_MUSIC_SLOT, (f_idx % 5) + 1);
      }
    } else if (*rt_.gui_page == 3 && *rt_.active_game == 0) {
      *rt_.active_game = (f_idx % 6) + 1;
    } else if (*rt_.gui_page == 6 && *rt_.area_subpage == 0) {
      const int slot = f_idx % 7;
      if (slot < 3 && !rt_.area_slot_ids[slot]->empty()) {
        const std::string &s_type = *rt_.area_slot_types[slot];
        bool &s_act = *rt_.area_slot_actives[slot];
        const bool prev_act = s_act;
        s_act = !s_act;
        std::string next_act = "toggle";
        if (s_type == "lock") next_act = s_act ? "lock" : "unlock";
        else if (s_type == "alarm_control_panel") next_act = s_act ? "arm_home" : "disarm";
        else if (s_type == "cover") next_act = s_act ? "turn_on" : "turn_off";
        *rt_.area_target_entity_id = *rt_.area_slot_ids[slot];
        if (!gate_or_dispatch_area_action(s_type, next_act, s_act ? 100 : 0, "ENTER SECURITY PIN", now_in)) {
          s_act = prev_act;
        }
        return;
      }
    } else if (*rt_.gui_page == 7) {
      static constexpr int kKeyMap[8] = {9, 0, 2, 4, 3, 1, 5, 6};
      rt_.cmd(UiHostCmd::REMOTE_SEND_KEY, kKeyMap[f_idx % 8], *rt_.active_remote_slot);
    } else if (*rt_.gui_page == 8 && *rt_.tools_subpage == 0) {
#if LATTER_HAS_IMU
      static constexpr int kToolsP0[6] = {1, 2, 3, 7, 5, 6};
      static constexpr int kToolsP1[5] = {4, 8, 9, 10, 12};
      *rt_.tools_subpage = (*rt_.tools_menu_page == 0) ? kToolsP0[f_idx % 6] : kToolsP1[f_idx % 5];
#else
      static constexpr int kToolsP0[6] = {1, 2, 3, 5, 6, 9};
      static constexpr int kToolsP1[4] = {10, 12, 4, 8};
      *rt_.tools_subpage = (*rt_.tools_menu_page == 0) ? kToolsP0[f_idx % 6] : kToolsP1[f_idx % 4];
#endif
      if (*rt_.tools_subpage == 12) rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
    }
    rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
    rt_.schedule_disp();
  }
}

}  // namespace latter_ui
