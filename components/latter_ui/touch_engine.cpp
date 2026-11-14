// ==============================================================================
// Latter Compiled On-Device Touch, Gesture & Rotary Input Engine
// (components/latter_ui/touch_engine.cpp)
// FrameHitTable-driven tap, horizontal slider, and rotary focus dispatch paired
// with UiLayout millimetre swipe thresholds (docs/device-ui-design.md).
// ==============================================================================

#include "ui_touch_engine.h"
#include "ui_pages.h"

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#if __has_include("esphome/core/hal.h")
#include "esphome/core/hal.h"
#endif
#if __has_include("esphome/core/log.h")
#include "esphome/core/log.h"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef USE_ESP32
#include <esp_random.h>
#endif

namespace latter_ui {

namespace {

inline uint32_t next_rng_u32() {
#ifdef USE_ESP32
  return esp_random();
#else
  return static_cast<uint32_t>(std::rand());
#endif
}

inline void reset_music_loading_slots(DeviceRuntimeState &rs, const char *msg) {
  auto *slots = rs.music_slots();
  slots[0].name = msg;
  slots[0].id.clear();
  slots[0].type = "text";
  slots[0].active = false;
  for (size_t i = 1; i < DeviceRuntimeState::kSlotsPerPage; ++i) {
    slots[i] = BrowseSlotItem{};
    slots[i].type = "player";
  }
}

inline void launch_dock_app(int chosen_app, DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms) {
  tools_direct_home() = false;
  home_dock_page() = 0;

  if (chosen_app == 9) {
    rs.nav.gui_page = PageId::WHOLE_HOME;
    rs.nav.home_subpage = 0;
    int wh_tabs[4] = {0, 0, 0, 0};
    const int wh_cnt = collect_home_hub_tabs(wh_tabs);
    if (wh_cnt > 0) {
      bool cur_ok = false;
      for (int i = 0; i < wh_cnt; i++) {
        if (wh_tabs[i] == whole_home_tab()) {
          cur_ok = true;
          break;
        }
      }
      if (!cur_ok) whole_home_tab() = wh_tabs[0];
    }
  } else if (chosen_app == 12) {
    tools_direct_home() = true;
    cam_quick_reply_open() = false;
    rs.nav.gui_page = PageId::TOOLS;
    rs.nav.tools_subpage = ToolsSubpage::CAMERAS;
    rs.nav.home_subpage = 0;
    rt.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
  } else {
    rs.nav.gui_page = chosen_app;
    rs.nav.home_subpage = 0;
  }

  if (chosen_app == PageId::MUSIC) {
    rs.music.overlay_mode = 0;
    rs.music.hud_active = true;
    rs.music.hud_timer = now_ms;
    const auto &ms = media_state();
    if (rt.select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE) != "None" &&
        (!ms.is_local || rt.album_art_w == 0) && ms.art_url != rs.music.loaded_art_url) {
      rt.cmd(UiHostCmd::FETCH_REMOTE_ART_DEBOUNCED);
    }
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
      rt.cmd(UiHostCmd::FETCH_AREA_LIST_BROWSE);
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
        rt.cmd(UiHostCmd::FETCH_AREA_BROWSE);
      }
    }
  }

  if (chosen_app != 12) {
    rs.nav.tools_subpage = 0;
  }
  rs.nav.remote_subpage = 0;
  rs.nav.active_game = 0;
  mark_dirty_full();
  rt.schedule_disp();
}

inline void handle_home_activity_action(const HomeActivityItem *items, int count, int item_idx,
                                        bool quick_btn_tap, uint32_t now_ms,
                                        DeviceRuntimeState &rs, PersistedDevicePrefs &prefs,
                                        const UiTouchRuntime &rt) {
  if (item_idx < 0 || item_idx >= count) return;
  const HomeActivityItem &act = items[item_idx];
  const int kind = act.kind;
  const int s_idx = act.slot_idx;

  if (quick_btn_tap) {
    if (kind == 1 && s_idx >= 0 && s_idx < 3) {
      auto &s = timer_slots()[s_idx];
      if (s.running) {
        s.running = false;
        s.paused = true;
        s.remaining_sec = (s.target_ms > now_ms)
                              ? static_cast<int>((s.target_ms - now_ms + 999U) / 1000U)
                              : 0;
      } else if (s.paused) {
        s.running = true;
        s.paused = false;
        s.target_ms = now_ms + static_cast<uint32_t>(s.remaining_sec) * 1000U;
      }
      sync_multi_timer_state();
      rt.apply_power_policy();
    } else if (kind == 2) {
      if (rs.stopwatch.running) {
        rs.stopwatch.running = false;
        rs.stopwatch.elapsed_ms = now_ms - rs.stopwatch.start_time_ms;
      } else {
        rs.stopwatch.running = true;
        rs.stopwatch.start_time_ms = now_ms - rs.stopwatch.elapsed_ms;
      }
    } else if (kind == 3) {
      const uint32_t intent_ts = (esphome::millis() != 0U) ? esphome::millis() : now_ms;
      audio_session().set_play_intent(MusicPlayIntent::HomeQuickToggle, intent_ts);
      rt.cmd(UiHostCmd::TOGGLE_HA_MUSIC);
    } else if (kind == 4) {
      if (s_idx == 0) {
        rs.alarms.snooze_until = 0;
      } else if (s_idx >= 1 && s_idx <= 3) {
        prefs.alarms[s_idx - 1].enabled = false;
        if (rs.alarms.ringing_idx == s_idx) rs.alarms.snooze_until = 0;
        save_persisted_prefs();
      }
    } else if (kind == 6) {
      auto &ac = area_ctx_state();
      if (cam_call_active() || cam_intercom_ptt_active()) {
        intercom_session().stop_call_with_hw(now_ms);
      }
      if (cam_listen_active()) {
        cam_listen_active() = false;
        rs.intercom.cam_action = "listen_stop";
        intercom_session().emit_current_cmd();
        rt.cmd(UiHostCmd::STOP_DOORBELL_AUDIO);
      }
      if (!ac.doorbell_talk_state.empty() && ac.doorbell_talk_state != "idle" &&
          ac.doorbell_talk_state != "off" && ac.doorbell_talk_state != "none") {
        rs.intercom.cam_action = "hangup";
        intercom_session().emit_current_cmd();
        intercom_session().stop_call_with_hw(now_ms);
        ac.doorbell_talk_state = "idle";
      }
      if (rs.voice.intercom_turn) {
        rs.voice.intercom_turn = false;
        rt.cmd(UiHostCmd::CANCEL_VOICE_ASSISTANT);
      }
    }
    rt.schedule_disp();
    return;
  }

  rs.nav.home_subpage = 0;
  home_dock_page() = 0;
  if (kind == 1) {
    tools_direct_home() = true;
    rs.nav.gui_page = PageId::TOOLS;
    rs.nav.tools_subpage = ToolsSubpage::TIMERS;
    timer_view_slot() = (s_idx >= 0 && s_idx < 3) ? (s_idx + 1) : 0;
  } else if (kind == 2) {
    tools_direct_home() = true;
    rs.nav.gui_page = PageId::TOOLS;
    rs.nav.tools_subpage = ToolsSubpage::STOPWATCH;
  } else if (kind == 3) {
    tools_direct_home() = false;
    rs.nav.gui_page = PageId::MUSIC;
    rs.music.overlay_mode = 0;
    rs.music.hud_active = true;
    rs.music.hud_timer = now_ms;
    const auto &ms = media_state();
    if (rt.select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE) != "None" &&
        (!ms.is_local || rt.album_art_w == 0) && ms.art_url != rs.music.loaded_art_url) {
      rt.cmd(UiHostCmd::FETCH_REMOTE_ART_DEBOUNCED);
    }
  } else if (kind == 4) {
    tools_direct_home() = true;
    rs.nav.gui_page = PageId::TOOLS;
    rs.nav.tools_subpage = ToolsSubpage::ALARMS;
    rs.alarms.edit_idx = (s_idx >= 1 && s_idx <= 3) ? s_idx : 0;
  } else if (kind == 5) {
    tools_direct_home() = true;
    rs.nav.gui_page = PageId::TOOLS;
    rs.nav.tools_subpage = ToolsSubpage::CALENDAR;
    cal_detail_mode() = 0;
    cal_quick_add_open() = false;
    rs.slot_page_for(latter_rpc::SlotChannel::CALENDAR) = 1;
    rt.cmd(UiHostCmd::FETCH_CALENDAR_BROWSE);
  } else if (kind == 6) {
    const auto &ac = area_ctx_state();
    const bool db_live = !ac.doorbell_talk_state.empty() &&
                         ac.doorbell_talk_state != "idle" &&
                         ac.doorbell_talk_state != "off" &&
                         ac.doorbell_talk_state != "none" &&
                         ac.doorbell_talk_state != "unavailable" &&
                         ac.doorbell_talk_state != "unknown";
    tools_direct_home() = true;
    cam_quick_reply_open() = false;
    rs.nav.gui_page = PageId::TOOLS;
    if (db_live || cam_call_active() || cam_listen_active()) {
      rs.nav.tools_subpage = ToolsSubpage::CAMERAS;
      rt.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
    } else {
      rs.nav.tools_subpage = ToolsSubpage::INTERCOM;
    }
  }
  mark_dirty_full();
  rt.schedule_disp();
}

inline void toggle_timer_slot_run(MultiTimerSlot &s, uint32_t now_ms, const UiTouchRuntime &rt) {
  if (!s.running && !s.paused) {
    s.running = true;
    s.paused = false;
    s.remaining_sec = s.duration_sec;
    s.target_ms = now_ms + static_cast<uint32_t>(s.duration_sec) * 1000U;
  } else if (s.running) {
    s.running = false;
    s.paused = true;
    s.remaining_sec = (s.target_ms > now_ms)
                          ? static_cast<int>((s.target_ms - now_ms + 999U) / 1000U)
                          : 0;
  } else {
    s.running = true;
    s.paused = false;
    s.target_ms = now_ms + static_cast<uint32_t>(s.remaining_sec) * 1000U;
  }
  sync_multi_timer_state();
  rt.apply_power_policy();
  rt.schedule_disp();
}

inline void update_light_inspector_hex(float hue_deg, float sat) {
  const esphome::Color rgb = hsv_to_rgb(hue_deg / 360.0f, sat, 1.0f);
  char hex_buf[12];
  std::snprintf(hex_buf, sizeof(hex_buf), "#%02X%02X%02X", rgb.r, rgb.g, rgb.b);
  area_color_hex_param() = hex_buf;
}

}  // namespace

TouchGestureEngine &touch_engine() {
  static TouchGestureEngine s_engine;
  return s_engine;
}

void TouchGestureEngine::navigate_to_start_screen(uint32_t now_ms) {
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const int anchor = resolve_anchor_dock_app(prefs);
  if (rs.nav.active_game > 0) {
    rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
  }
  if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
    intercom_session().stop_call_with_hw(now_ms);
  }
  const bool was_cam_page = (rs.nav.gui_page == PageId::TOOLS &&
                             rs.nav.tools_subpage == ToolsSubpage::CAMERAS);
  ui::pin_pad_scrub();
  tools_direct_home() = false;
  timer_view_slot() = 0;
  rs.alarms.edit_idx = 0;
  cal_detail_mode() = 0;
  cal_quick_add_open() = false;
  cam_quick_reply_open() = false;
  rs.music.overlay_mode = 0;
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
  rs.nav.sky_weather_subpage = 0;
  if (was_cam_page && anchor != 12) {
    rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
    rs.alert.loaded_camera_url.clear();
    rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
  }
  if (anchor == PageId::HOME) {
    rs.nav.gui_page = PageId::HOME;
    rs.nav.home_subpage = 0;
    mark_dirty_full();
    rt_.schedule_disp();
    return;
  }
  if (anchor == PageId::CLOCK) {
    rs.nav.gui_page = PageId::CLOCK;
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
  launch_dock_app(anchor, rs, rt_, now_ms);
}

void TouchGestureEngine::navigate_home(uint32_t now_ms) {
  if (!persisted_prefs().home_screen_enabled) {
    navigate_to_start_screen(now_ms);
    return;
  }
  auto &rs = runtime_state();
  if (rs.nav.active_game > 0) {
    rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
  }
  if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
    intercom_session().stop_call_with_hw(now_ms);
  }
  const bool was_cam_page = (rs.nav.gui_page == PageId::TOOLS &&
                             rs.nav.tools_subpage == ToolsSubpage::CAMERAS);
  ui::pin_pad_scrub();
  tools_direct_home() = false;
  timer_view_slot() = 0;
  rs.alarms.edit_idx = 0;
  cal_detail_mode() = 0;
  cal_quick_add_open() = false;
  cam_quick_reply_open() = false;
  rs.nav.gui_page = PageId::HOME;
  rs.nav.home_subpage = 0;
  rs.music.overlay_mode = 0;
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
  rs.nav.sky_weather_subpage = 0;
  if (was_cam_page) {
    rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
    rs.alert.loaded_camera_url.clear();
    rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
  }
  mark_dirty_full();
  rt_.schedule_disp();
}

bool TouchGestureEngine::handle_pin_pad_tap(int tx, int ty, uint32_t now_ms) {
  auto &st = ui::pin_pad_state();
  if (!st.open) return false;

  auto &rs = runtime_state();
  const HitRegion *hit = frame_hits().hit_test(tx, ty, kHitTap);
  if (hit != nullptr && (hit->widget_id == WidgetId::PIN_PAD_CANCEL ||
                         hit->widget_id == WidgetId::BACK_BUTTON)) {
    if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      ui::pin_pad_scrub();
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return true;
  }
  if (hit == nullptr) {
    return true;
  }
  if (hit->widget_id >= WidgetId::PIN_PAD_KEY_BASE &&
      hit->widget_id <= WidgetId::PIN_PAD_KEY_BASE + 9) {
    if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID)) {
      ui::pin_pad_press_digit(static_cast<uint8_t>(hit->widget_id - WidgetId::PIN_PAD_KEY_BASE));
      rt_.schedule_disp();
    }
    return true;
  }
  if (hit->widget_id == WidgetId::PIN_PAD_BACKSPACE) {
    if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID)) {
      ui::pin_pad_backspace();
      rt_.schedule_disp();
    }
    return true;
  }
  if (hit->widget_id == WidgetId::PIN_PAD_SUBMIT) {
    if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
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
        rs.area.target_code = pin_str;
        rs.area.feedback_timer = now_ms;
        rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
      } else if (target == ui::PinPadTargetKind::ALARM_ACTION) {
        rs.area.target_code = pin_str;
        rs.area.feedback_timer = now_ms;
        rt_.cmd(UiHostCmd::EXECUTE_ALARM_ACTION);
      } else if (target == ui::PinPadTargetKind::DOORBELL_UNLOCK) {
        rs.intercom.cam_code = pin_str;
        rs.intercom.cam_action = "unlock_door";
        rs.intercom.cam_message.clear();
        rs.intercom.broadcast_feedback = "Unlocked Door / Gate";
        rs.intercom.broadcast_feedback_ms = now_ms;
        cam_intercom_status() = "DOOR / GATE UNLOCKED";
        rs.intercom.cam_status_ms = now_ms;
        intercom_session().emit_current_cmd();
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
  auto &rs = runtime_state();
  const std::string &pol = area_ctx_state().security_pin_policy;
  const bool has_alarm = !rs.area.alarm_entity.empty() || !area_ctx_state().alarm_entity.empty();
  if (ui::is_perimeter_action_blocked(pol, domain_or_entity, action, level)) {
    rs.area.action_feedback = "Blocked by Security Policy";
    rs.area.feedback_timer = now_ms;
    rt_.schedule_disp();
    return false;
  }
  rs.area.target_action = action;
  rs.area.target_level = level;
  if (ui::requires_perimeter_pin_prompt(pol, domain_or_entity, action, level, has_alarm)) {
    ui::pin_pad_open(ui::PinPadTargetKind::AREA_ACTION, pin_prompt);
    mark_dirty_full();
    rt_.schedule_disp();
    return false;
  }
  rs.area.feedback_timer = now_ms;
  rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
  rt_.schedule_disp();
  return true;
}

bool TouchGestureEngine::gate_or_dispatch_alarm_action(const std::string &action, uint32_t now_ms) {
  auto &rs = runtime_state();
  const std::string &pol = area_ctx_state().security_pin_policy;
  const bool has_alarm = !rs.area.alarm_entity.empty() || !area_ctx_state().alarm_entity.empty();
  if (ui::is_perimeter_action_blocked(pol, "alarm_control_panel", action, 100)) {
    rs.area.action_feedback = "Blocked by Security Policy";
    rs.area.feedback_timer = now_ms;
    rt_.schedule_disp();
    return false;
  }
  rs.area.alarm_service = action;
  if (ui::requires_perimeter_pin_prompt(pol, "alarm_control_panel", action, 100, has_alarm)) {
    ui::pin_pad_open(ui::PinPadTargetKind::ALARM_ACTION, "ENTER ALARM PIN");
    mark_dirty_full();
    rt_.schedule_disp();
    return false;
  }
  rs.area.feedback_timer = now_ms;
  rt_.cmd(UiHostCmd::EXECUTE_ALARM_ACTION);
  rt_.schedule_disp();
  return true;
}

bool TouchGestureEngine::gate_or_dispatch_doorbell_unlock(uint32_t now_ms, bool from_intercom) {
  auto &rs = runtime_state();
  const std::string &pol = area_ctx_state().security_pin_policy;
  const bool has_alarm = !rs.area.alarm_entity.empty() || !area_ctx_state().alarm_entity.empty();
  if (ui::is_perimeter_action_blocked(pol, "lock", "unlock", 100)) {
    if (from_intercom) {
      rs.intercom.broadcast_feedback = "Blocked by Security Policy";
      rs.intercom.broadcast_feedback_ms = now_ms;
    } else {
      cam_intercom_status() = "BLOCKED BY POLICY";
      rs.intercom.cam_status_ms = now_ms;
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
  rs.intercom.cam_action = "unlock_door";
  rs.intercom.cam_message.clear();
  if (from_intercom) {
    rs.intercom.broadcast_feedback = "Unlocked Door / Gate";
    rs.intercom.broadcast_feedback_ms = now_ms;
  } else {
    cam_intercom_status() = "DOOR / GATE UNLOCKED";
    rs.intercom.cam_status_ms = now_ms;
  }
  intercom_session().emit_current_cmd();
  rt_.schedule_disp();
  return true;
}

void TouchGestureEngine::on_touch_down(int tx, int ty, uint32_t now_touch) {
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  const TouchPt touch{tx, ty};

  // ----------------------------------------------------------------------------
  // 1. System Takeover Intercepts ([TAKEOVER ORDER] matches render_system_takeovers)
  //    OTA -> Hibernating -> Screen Off -> Alarm/Timer/FindMy -> VA -> PIN -> Notif
  // ----------------------------------------------------------------------------
  if (rs.nav.ota_in_progress) {
    return;
  }

  if (rs.nav.is_hibernating) {
    if (rs.nav.hibernate_countdown_val > 0) {
      rs.nav.is_hibernating = false;
      rs.nav.hibernate_countdown_val = 0;
      note_wake_reason(WakeReason::TOUCH);
      rt_.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    }
    return;
  }

  if (rs.nav.display_power_state != 0 || !rt_.panel_is_on) {
    const std::string_view tw_mode = rt_.select_opt(UiSelectId::TAP_WAKE_MODE);
    if (tw_mode == "Disabled") return;
    if (tw_mode == "Docked Only" && !rt_.usb_connected) return;
    if (!accept_screen_off_tap(rt_.usb_connected, now_touch)) return;
    note_wake_reason(WakeReason::TOUCH);
    rt_.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    return;
  }

  if (now_touch - last_screen_wake_ms() < TOUCH_DEBOUNCE_WAKE) {
    if (latter_ui::profiler_enabled()) {
      LATTER_PROF_LOGI("[PROF:TOUCH_BLOCKED] x=%d y=%d page=%d reason=WAKE_DEBOUNCE",
                       touch.x, touch.y, rs.nav.gui_page);
    }
    return;
  }

  if (now_touch - last_touch_activity_ms_ > 1000U) {
    last_touch_activity_ms_ = now_touch;
    display_lifecycle().note_activity(now_touch);
  }

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
  last_user_touch_ms() = now_touch;

  // Priority 1: Alarm / Timer / Find-My Ringing Takeover (above Voice Assistant!)
  if (rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active) {
    gest.active = false;
    const HitRegion *t_hit = frame_hits().hit_test(touch.x, touch.y, kHitTap);
    if (rs.alarms.find_my_active) {
      rs.alarms.find_my_active = false;
      last_screen_wake_ms() = now_touch;
      rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
      rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
      mark_dirty_full();
      rt_.schedule_disp();
      return;
    }
    if (rs.alarms.ringing) {
      if (t_hit != nullptr && t_hit->widget_id == WidgetId::ALERT_SNOOZE_BTN) {
        rs.alarms.snooze_until = (rt_.time_epoch > 0) ? (rt_.time_epoch + 300U)
                                                      : ((now_touch / 1000U) + 300U);
        rs.alarms.ringing = false;
        last_screen_wake_ms() = now_touch;
        rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (t_hit != nullptr && t_hit->widget_id == WidgetId::ALERT_DISMISS_BTN) {
        if (rs.alarms.ringing_idx >= 1 && rs.alarms.ringing_idx <= 3) {
          auto &ap = prefs.alarms[rs.alarms.ringing_idx - 1];
          if (ap.recurrence == 3) {
            ap.enabled = false;
            save_persisted_prefs();
          }
        }
        rs.alarms.ringing = false;
        rs.alarms.snooze_until = 0;
        last_screen_wake_ms() = now_touch;
        rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
        rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      return;
    }
    if (rs.timers.ringing) {
      if (t_hit != nullptr && t_hit->widget_id == WidgetId::ALERT_DISMISS_BTN) {
        rs.timers.ringing = false;
        rs.timers.va_timer_name.clear();
        sync_multi_timer_state();
        last_screen_wake_ms() = now_touch;
        rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
        rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
        rt_.apply_power_policy();
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
  }

  // Priority 2: Voice Assistant Takeover
  if (rs.voice.active || !rs.voice.status_text.empty()) {
    gest.active = false;
    last_screen_wake_ms() = now_touch;
    rt_.cmd(UiHostCmd::CANCEL_VOICE_ASSISTANT);
    return;
  }

  // Priority 3: Security PIN Pad Modal
  if (ui::pin_pad_state().open) {
    gest.active = false;
    handle_pin_pad_tap(touch.x, touch.y, now_touch);
    return;
  }

  // Priority 4: Push Notification Banner / Doorbell Camera Popup
  if (rs.alert.active) {
    gest.active = false;
    last_screen_wake_ms() = now_touch;
    const HitRegion *n_hit = frame_hits().hit_test(touch.x, touch.y, kHitTap);
    if (!rs.alert.camera_url.empty() && n_hit != nullptr &&
        n_hit->widget_id == WidgetId::NOTIF_OPEN_CAM_BTN) {
      rs.alert.active = false;
      tools_direct_home() = true;
      cam_quick_reply_open() = false;
      rs.nav.gui_page = PageId::TOOLS;
      rs.nav.tools_subpage = ToolsSubpage::CAMERAS;
      rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
      intercom_session().start_call(now_touch);
      mark_dirty_full();
      rt_.schedule_disp();
      return;
    }
    rt_.cmd(UiHostCmd::DISMISS_NOTIFICATION);
    return;
  }

  // ----------------------------------------------------------------------------
  // 2. Active Page Touch-Down Initialization & Immediate Hit-Table Controls
  // ----------------------------------------------------------------------------
  gest.start_page = rs.nav.gui_page;
  gest.start_context = compute_touch_context(
      rs.nav.gui_page, rs.nav.home_subpage, rs.music.overlay_mode, rs.nav.active_game,
      rs.nav.system_menu_mode, rs.nav.sky_weather_subpage, rs.nav.area_subpage,
      rs.nav.remote_subpage, rs.nav.tools_subpage, whole_home_tab());
  gest.active = true;
  gest.swipe_committed = false;

  const auto &ly = layout();
  const HitRegion *hit = frame_hits().hit_test(touch.x, touch.y, kHitTap);
  const uint16_t wid = (hit != nullptr) ? hit->widget_id : WidgetId::NONE;

  // Universal bottom-edge swipe-up home guard: defer chin touches to on_touch_move / on_touch_up
  // without swallowing direct taps on an explicit < Back button near the bottom bezel.
  if (can_go_home(rs) && wid != WidgetId::BACK_BUTTON && touch.y >= ly.chin_guard_y()) {
    return;
  }

  // Active arcade canvas games (Marbles=1, Archery=2, Bricks=3, Chomp=4, Memory=6)
  if (rs.nav.gui_page == PageId::GAMES && rs.nav.active_game > 0) {
    if (wid == WidgetId::BACK_BUTTON) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.active_game = 0;
        rs.nav.game_btn_pressed = false;
        rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
    if (rs.nav.active_game != 5) {
      rs.nav.marble_touch_x = static_cast<float>(touch.x);
      rs.nav.marble_touch_y = static_cast<float>(touch.y);
      rs.nav.game_btn_pressed = true;
      rt_.schedule_disp();
      return;
    }
    return;  // 2048 (active_game == 5) resolves flick vs tap in on_touch_up
  }

  // Page 0: Standalone Clock Face
  if (rs.nav.gui_page == PageId::CLOCK) {
    if (prefs.home_screen_enabled && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
      rs.nav.gui_page = PageId::HOME;
      rs.nav.home_subpage = 0;
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return;
  }

  // Page 1: Home Dashboard & Dock Settings / Activity Overlay
  if (rs.nav.gui_page == PageId::HOME) {
    if (wake_greeting_latched() || rs.nav.wake_greeting_pending) {
      dismiss_wake_greeting();
      rt_.schedule_disp();
    }

    if (rs.nav.home_subpage == 1) {
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rs.nav.home_subpage = 0;
          rs.nav.gui_page = PageId::SYSTEM;
          rs.nav.system_menu_mode = 0;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 5) {
        const int slot = hit->param;
        if (slot >= 0 && slot < 5 && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          latter_dock_order_csv() = toggled_dock_csv(kHomeSettingsDockTokens[slot]);
          rt_.cmd(UiHostCmd::REQUEST_DOCK_APPS);
          rt_.schedule_disp();
        }
      }
      return;
    }

    int act_count = 0;
    const HomeActivityItem *act_items = home_activity_rendered_items(act_count);
    HomeActivityItem fallback_items[kMaxHomeActivity];
    if (act_count <= 0) {
      const auto &ms_hm = media_state();
      const bool m_play = is_music_playing_effective(
          !ms_hm.is_local, rt_.local_mp_playing, ms_hm.state, now_touch);
      const std::string &m_t = (!ms_hm.is_local || rt_.track_title.empty()) ? ms_hm.title : rt_.track_title;
      const std::string &m_a = (!ms_hm.is_local || rt_.track_artist.empty()) ? ms_hm.artist : rt_.track_artist;
      act_count = collect_home_activities(
          now_touch, prefs.is_24h_format,
          rs.stopwatch.running, rs.stopwatch.start_time_ms, rs.stopwatch.elapsed_ms,
          m_play, m_t, m_a, rs.alarms.snooze_until,
          prefs.alarms[0].hour, prefs.alarms[0].minute, prefs.alarms[0].enabled,
          prefs.alarms[1].hour, prefs.alarms[1].minute, prefs.alarms[1].enabled,
          prefs.alarms[2].hour, prefs.alarms[2].minute, prefs.alarms[2].enabled,
          fallback_items, rt_.time_hour, rt_.time_minute, calendar_state().summary);
      act_items = fallback_items;
    }

    if (rs.nav.home_subpage == 2) {
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rs.nav.home_subpage = 0;
          home_activity_overlay_page() = 0;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::HOME_ACTIVITY_PREV || wid == WidgetId::CHEVRON_UP) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_SCROLL)) {
          home_activity_overlay_page() = std::max(0, home_activity_overlay_page() - 1);
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::HOME_ACTIVITY_NEXT || wid == WidgetId::CHEVRON_DOWN) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_SCROLL)) {
          const int max_pg = std::max(0, (act_count - 1) / 5);
          home_activity_overlay_page() = std::min(max_pg, home_activity_overlay_page() + 1);
          rt_.schedule_disp();
        }
        return;
      }
      if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 5) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const bool quick_tap = (touch.x >= hit->x1 - scale_x(56));
          handle_home_activity_action(act_items, act_count, hit->param, quick_tap,
                                      now_touch, rs, prefs, rt_);
        }
        return;
      }
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_DISMISS)) {
        rs.nav.home_subpage = 0;
        home_activity_overlay_page() = 0;
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }

    // Home subpage 0: Hero zones, Roaming Area icon & FrameHitTable Activity sub-regions
    // (Dock slots resolve on_touch_up so dock swipe works!)
    if (wid == WidgetId::HOME_QUICK_BTN) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        const bool is_night = (weather_state().sun_state == "below_horizon");
        rs.nav.gui_page = PageId::WEATHER;
        rs.nav.sky_weather_subpage = is_night ? 3 : 2;
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
    if (wid == WidgetId::HOME_AREA_ICON) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        const auto &ac = area_ctx_state();
        if (!ac.area_id.empty()) {
          rs.area.active_area_id = ac.area_id;
          rs.area.active_area_name = ac.area_name.empty() ? latter_area_label() : ac.area_name;
        }
        launch_dock_app(PageId::AREA, rs, rt_, now_touch);
      }
      return;
    }
    if (wid == WidgetId::HOME_TIME_READOUT) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.gui_page = PageId::CLOCK;
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
    if (wid == WidgetId::HOME_WEATHER_HERO) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.gui_page = PageId::WEATHER;
        rs.nav.sky_weather_subpage = 0;
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
    if (wid == WidgetId::HOME_ACT_EXPAND && act_count > 0) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        home_activity_overlay_page() = 0;
        rs.nav.home_subpage = 2;
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
    if ((wid == WidgetId::HOME_ACT_ROW_0 || wid == WidgetId::HOME_ACT_ROW_1 ||
         wid == WidgetId::HOME_ACT_ACTION_0 || wid == WidgetId::HOME_ACT_ACTION_1) &&
        act_count > 0) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        const bool quick_tap =
            (wid == WidgetId::HOME_ACT_ACTION_0 || wid == WidgetId::HOME_ACT_ACTION_1);
        handle_home_activity_action(act_items, act_count, hit->param, quick_tap,
                                    now_touch, rs, prefs, rt_);
      }
      return;
    }
    return;
  }

  // Page 2: Music Now Playing & Overlays
  if (rs.nav.gui_page == PageId::MUSIC) {
    if (rs.music.overlay_mode == 0) {
      const std::string_view ov_style = rt_.select_opt(UiSelectId::MUSIC_OVERLAY_STYLE);
      const bool always_vis = (ov_style == "Always Visible");
      const bool controls_shown = always_vis || rs.music.hud_active;

      if (!controls_shown) {
        rs.music.hud_active = true;
        rs.music.hud_timer = now_touch;
        rs.nav.hud_wake_finger_released = false;
        hud_just_woken_ms_ = now_touch;
        touch_router().note_tap(now_touch);
        rt_.schedule_disp();
        return;
      }

      if (wid == WidgetId::MUSIC_QUEUE_BTN) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          music_scroll_tap_lockout_until_ms() = now_touch + 280U;
          rs.music.direct_nav = false;
          rs.music.overlay_mode = 5;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::MUSIC_SPEAKER_BTN) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rs.music.selected_category_mode = "speakers";
          rs.music.selected_category_title = "SPEAKERS";
          reset_music_loading_slots(rs, "Loading...");
          rs.slot_page_for(latter_rpc::SlotChannel::MUSIC) = 1;
          rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
          rs.music.direct_nav = true;
          rs.music.overlay_mode = 1;
          media_fetch_browse(now_touch);
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::MUSIC_BROWSE_BTN) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rs.music.category_page = 1;
          rs.nav.menu_scroll_active = false;
          rs.music.direct_nav = true;
          rs.music.overlay_mode = 2;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::MUSIC_PLAY_PAUSE) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const auto &ms = media_state();
          const bool was_playing = is_music_playing_effective(
              !ms.is_local, rt_.local_mp_playing, ms.state, now_touch);
          rs.music.action_feedback = was_playing ? "Pausing..." : "Playing...";
          rs.music.feedback_timer = now_touch;
          rs.music.hud_timer = now_touch;
          rt_.cmd(UiHostCmd::TOGGLE_HA_MUSIC);
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::MUSIC_PREV_BTN || wid == WidgetId::MUSIC_NEXT_BTN) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
          const bool is_next = (wid == WidgetId::MUSIC_NEXT_BTN);
          const bool is_speech = (rs.music.active_playback_media_type == "podcast" ||
                                  rs.music.active_playback_media_type == "audiobook");
          if (is_speech) {
            media_seek_relative(is_next ? 1 : -1, now_touch);
          } else if (is_next) {
            media_next_track(now_touch);
          } else {
            media_prev_track(now_touch);
          }
          rs.music.feedback_timer = now_touch;
          rs.music.hud_timer = now_touch;
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::MUSIC_VOL_SLIDER) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
          step_music_volume_feedback(
              hit->param, rt_.num_val(UiNumberId::VOLUME_STEP_SIZE),
              media_state().is_local, rt_.local_mp_volume, now_touch, rs.music);
          rs.music.hud_timer = now_touch;
          rt_.cmd(UiHostCmd::MUSIC_STEP_VOLUME, (hit->param > 0) ? 1 : -1);
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::MUSIC_ART_TAP) {
        if (hit->param == 6) {
          if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
            rs.music.overlay_mode = 6;
            mark_dirty_full();
            rt_.schedule_disp();
          }
          return;
        }
      }
      if (wid == WidgetId::NONE && !always_vis && rs.music.hud_active &&
          (now_touch - rs.music.hud_timer >= TOUCH_GUARD_DISMISS) &&
          (now_touch - hud_just_woken_ms_ >= TOUCH_GUARD_DISMISS)) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_DISMISS)) {
          rs.music.hud_active = false;
          rt_.schedule_disp();
        }
      }
      return;
    }

    // Music Mode 5: Music Hub
    if (rs.music.overlay_mode == 5) {
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rs.music.overlay_mode = 0;
          rs.music.hud_active = true;
          rs.music.hud_timer = now_touch;
          rs.nav.system_menu_mode = 0;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 4) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const int row = hit->param;
          if (row == 0) {
            rs.music.selected_category_mode = "speakers";
            rs.music.selected_category_title = "SPEAKERS";
            reset_music_loading_slots(rs, "Loading...");
            rs.slot_page_for(latter_rpc::SlotChannel::MUSIC) = 1;
            rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
            rs.music.direct_nav = false;
            rs.music.overlay_mode = 1;
            media_fetch_browse(now_touch);
          } else if (row == 1) {
            rs.music.category_page = 1;
            rs.nav.menu_scroll_active = false;
            rs.music.direct_nav = false;
            rs.music.overlay_mode = 2;
          } else if (row == 2) {
            rs.music.selected_category_mode = "active_queue";
            rs.music.selected_category_title = "QUEUE";
            rs.music.drilldown_parent_mode = "music_hub";
            reset_music_loading_slots(rs, "Loading queue...");
            rs.slot_page_for(latter_rpc::SlotChannel::MUSIC) = 1;
            rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
            rs.music.overlay_mode = 3;
            media_fetch_browse(now_touch);
          } else if (row == 3) {
            rs.nav.gui_page = PageId::SYSTEM;
            rs.nav.system_menu_mode = 9;
            rs.nav.audio_menu_parent_page = 2;
            rs.nav.menu_scroll_active = false;
          }
          mark_dirty_full();
          rt_.schedule_disp();
        }
      }
      return;
    }

    // Music Mode 1, 2, 3: Back & Queue Clear (scrollable list rows resolve in on_touch_up)
    if (rs.music.overlay_mode == 1 || rs.music.overlay_mode == 2) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (rs.music.direct_nav) {
          rs.music.overlay_mode = 0;
          rs.music.direct_nav = false;
        } else {
          rs.music.overlay_mode = 5;
        }
        rs.music.category_page = 1;
        rs.nav.menu_scroll_active = false;
        rs.music.hud_active = true;
        rs.music.hud_timer = now_touch;
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }

    if (rs.music.overlay_mode == 3) {
      if (wid == WidgetId::MUSIC_QUEUE_CLEAR && rs.music.selected_category_mode == "active_queue") {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rt_.cmd(UiHostCmd::CLEAR_HA_MUSIC_QUEUE);
          reset_music_loading_slots(rs, "Queue Cleared");
          rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
          music_queue_cleared() = true;
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        int &b_page = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
        if (rs.music.selected_category_mode == "active_queue" &&
            rs.music.drilldown_parent_mode == "music_hub") {
          rs.music.drilldown_parent_mode.clear();
          rs.music.overlay_mode = 5;
        } else if (rs.music.selected_category_mode == "current_album_tracks" ||
                   rs.music.selected_category_mode == "active_queue" ||
                   (rs.music.selected_category_mode == "artist_albums" &&
                    rs.music.drilldown_parent_mode == "track_inspector")) {
          rs.music.drilldown_parent_mode.clear();
          rs.music.overlay_mode = 6;
        } else if (rs.music.selected_category_mode == "album_tracks" ||
                   rs.music.selected_category_mode == "podcast_episodes") {
          rs.music.selected_category_mode =
              rs.music.drilldown_parent_mode.empty() ? "album" : rs.music.drilldown_parent_mode;
          rs.music.selected_category_title =
              (rs.music.selected_category_mode == "podcast")
                  ? "PODCASTS"
                  : ((rs.music.selected_category_mode == "artist_albums")
                         ? rs.music.drilldown_artist_name
                         : "ALBUMS");
          b_page = std::max(1, rs.music.drilldown_album_page);
          media_fetch_browse(now_touch);
        } else if (rs.music.selected_category_mode == "artist_albums") {
          rs.music.selected_category_mode = "artist";
          rs.music.selected_category_title = "ARTISTS";
          b_page = std::max(1, rs.music.drilldown_artist_page);
          media_fetch_browse(now_touch);
        } else {
          rs.music.drilldown_artist_page = 1;
          rs.music.drilldown_album_page = 1;
          rs.nav.menu_scroll_active = false;
          rs.music.overlay_mode = 2;
        }
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }

    // Music Mode 4: Speaker Action Sheet
    if (rs.music.overlay_mode == 4) {
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rs.music.overlay_mode = 1;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 4) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const int r = hit->param;
          const bool is_current = (rs.music.target_speaker_id == rs.music.active_speaker_entity);
          if (!is_current) {
            if (r == 0) rt_.cmd(UiHostCmd::SPEAKER_TRANSFER_TO_TARGET);
            else if (r == 1) rt_.cmd(UiHostCmd::SPEAKER_CONTROL_TARGET);
            else if (r == 2) rt_.cmd(UiHostCmd::SPEAKER_JOIN_TARGET);
            else if (r == 3) rt_.cmd(UiHostCmd::SPEAKER_UNJOIN_TARGET);
          } else {
            const auto &ms = media_state();
            const bool is_remote_target = !ms.is_local && !ms.local_speaker.empty();
            if (is_remote_target) {
              if (r == 0) {
                rs.music.target_speaker_id = ms.local_speaker;
                rs.music.target_speaker_name =
                    (rt_.friendly_name != nullptr && rt_.friendly_name[0] != '\0')
                        ? rt_.friendly_name
                        : "This Device";
                rt_.cmd(UiHostCmd::SPEAKER_TRANSFER_TO_TARGET);
              } else if (r == 1) {
                rt_.cmd(UiHostCmd::SPEAKER_UNJOIN_TARGET);
              } else if (r == 2) {
                rt_.cmd(UiHostCmd::STOP_HA_MUSIC);
              }
            } else {
              if (r == 0) rt_.cmd(UiHostCmd::SPEAKER_UNJOIN_TARGET);
              else if (r == 1) rt_.cmd(UiHostCmd::STOP_HA_MUSIC);
            }
          }
        }
      }
      return;
    }

    // Music Mode 6: Track Inspector
    if (rs.music.overlay_mode == 6) {
      const auto &ms = media_state();
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rs.music.overlay_mode = 0;
          rs.music.hud_active = true;
          rs.music.hud_timer = now_touch;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::LIST_SLOT_BASE + 0) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const bool is_rem = !ms.is_local;
          const std::string alb = (is_rem || rt_.track_album.empty()) ? ms.album : rt_.track_album;
          rs.music.selected_category_mode = "current_album_tracks";
          rs.music.selected_category_title = alb.empty() ? "ALBUM TRACKS" : alb;
          rs.music.drilldown_album_id = alb;
          rs.slot_page_for(latter_rpc::SlotChannel::MUSIC) = 1;
          rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
          reset_music_loading_slots(rs, "Loading album tracks...");
          rs.music.overlay_mode = 3;
          media_fetch_browse(now_touch);
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::MUSIC_PREV_BTN || wid == WidgetId::MUSIC_NEXT_BTN) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
          float cur_pos = rs.music.pos_base;
          if (rs.music.pos_sync_time > 0) {
            cur_pos += static_cast<float>(now_touch - rs.music.pos_sync_time) * 0.001f;
          }
          const float delta_s = (wid == WidgetId::MUSIC_NEXT_BTN) ? 15.0f : -15.0f;
          float new_p = std::max(0.0f, cur_pos + delta_s);
          if (!std::isnan(ms.duration) && ms.duration > 0.0f) {
            new_p = std::min(ms.duration, new_p);
          }
          rs.music.pos_base = new_p;
          rs.music.pos_sync_time = now_touch;
          media_seek_absolute(now_touch);
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::MUSIC_SCRUB_BAR) {
        if (!std::isnan(ms.duration) && ms.duration > 0.0f &&
            touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
          rs.music.pos_base = hit->norm_x(touch.x) * ms.duration;
          rs.music.pos_sync_time = now_touch;
          media_seek_absolute(now_touch);
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::LIST_SLOT_BASE + 1 || wid == WidgetId::LIST_SLOT_BASE + 2) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const bool is_bio = (wid == WidgetId::LIST_SLOT_BASE + 2);
          const bool is_rem = !ms.is_local;
          const std::string t_name = (is_rem || rt_.track_title.empty()) ? ms.title : rt_.track_title;
          const std::string ar_name = (is_rem || rt_.track_artist.empty()) ? ms.artist : rt_.track_artist;
          const std::string al_name = (is_rem || rt_.track_album.empty()) ? ms.album : rt_.track_album;
          rs.music.drilldown_album_id = t_name + "|" + ar_name + "|" + al_name;
          rs.music.selected_category_mode = is_bio ? "bio" : "lyrics";
          rs.music.selected_category_title = is_bio ? "BIO" : "LYRICS";
          rs.slot_page_for(latter_rpc::SlotChannel::MUSIC) = 1;
          rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
          reset_music_loading_slots(rs, is_bio ? "Loading artist & album bio..." : "Loading lyrics...");
          rs.music.overlay_mode = is_bio ? 8 : 7;
          media_fetch_browse(now_touch);
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      return;
    }

    // Music Mode 7 / 8: Lyrics & Bio
    if (rs.music.overlay_mode == 7 || rs.music.overlay_mode == 8) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.music.overlay_mode = 6;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
        int &pg = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
        const int tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC);
        const int next_pg = std::clamp(pg + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, std::max(1, tot));
        if (next_pg != pg) {
          pg = next_pg;
          media_fetch_browse(now_touch);
          rt_.schedule_disp();
        }
      }
      return;
    }
  }

  // Page 3: Games Launcher (active_game == 0; carousel swipes & Play/Sound taps resolve on_touch_up)
  if (rs.nav.gui_page == PageId::GAMES) {
    return;
  }

  // Page 4: System Settings & Modals
  if (rs.nav.gui_page == PageId::SYSTEM) {
    if (rs.nav.system_menu_mode == 1 || rs.nav.system_menu_mode == 2) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = 0;
        rs.nav.menu_scroll_active = false;
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;  // Scrollable list rows resolve in on_touch_up
    }

    if (rs.nav.system_menu_mode == 3) {
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rs.nav.system_menu_mode = 0;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::SYS_SECONDARY_BTN) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rs.nav.system_menu_mode = 4;
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const int slot = hit->param;
          if (slot == 0) {
            rt_.cmd(UiHostCmd::TRIGGER_SCREENSAVER_NOW);
          } else if (slot == 1) {
#if LATTER_HAS_BATTERY
            rt_.cmd(UiHostCmd::START_HIBERNATION_COUNTDOWN);
#else
            rt_.cmd(UiHostCmd::TURN_SCREEN_OFF);
#endif
          } else if (slot == 2) {
            rs.nav.system_menu_mode = 5;
            mark_dirty_full();
            rt_.schedule_disp();
          } else if (slot == 3) {
            rt_.sw_toggle(UiSwitchId::BLE_AREA_BEACON);
            rt_.schedule_disp();
          }
        }
      }
      return;
    }

    if (rs.nav.system_menu_mode == 4) {
      return;  // Confirm modal resolves in on_touch_up so finger is off glass before restart
    }

    if (rs.nav.system_menu_mode == 5) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.update_check_state = 0;
        rs.nav.system_menu_mode = 0;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::SYS_PRIMARY_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        const bool has_update = is_newer_semver(
            rt_.fw_latest_version, (rt_.fw_version != nullptr) ? rt_.fw_version : "0.0.0");
        if (has_update) {
          rs.nav.system_menu_mode = 6;
          mark_dirty_full();
          rt_.schedule_disp();
        } else if (rs.nav.update_check_state == 0) {
          rs.nav.update_check_state = 1;
          rs.nav.update_check_timer = now_touch;
          rt_.cmd(UiHostCmd::CHECK_FIRMWARE_UPDATE);
          rt_.schedule_disp();
        }
      }
      return;
    }

    if (rs.nav.system_menu_mode == 6) {
      return;  // Confirm modal resolves in on_touch_up
    }

    if (rs.nav.system_menu_mode == 7) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = 0;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::SYS_VOL_SLIDER) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
          const float new_vol = step_wrap_range(rt_.local_mp_volume, 0.10f, 0.10f, 1.00f);
          rt_.set_num_val(UiNumberId::SPEAKER_VOLUME, std::round(new_vol * 100.0f));
          rt_.schedule_disp();
        }
        return;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
        const int slot = hit->param;
        if (slot == 1 && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
          const float cur_a_f = rt_.num_val(UiNumberId::ALERT_VOLUME);
          const int cur_a = (std::isnan(cur_a_f) || cur_a_f < 20.0f) ? 70 : static_cast<int>(std::round(cur_a_f));
          const int nxt_a = static_cast<int>(step_wrap_range(static_cast<float>(cur_a), 10.0f, 50.0f, 100.0f));
          rt_.set_num_val(UiNumberId::ALERT_VOLUME, static_cast<float>(nxt_a));
          rt_.schedule_disp();
        } else if (slot == 2 && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const int cur_sz = static_cast<int>(std::round(rt_.num_val(UiNumberId::VOLUME_STEP_SIZE)));
          const int nxt_sz = (cur_sz < 5) ? 5 : ((cur_sz < 10) ? 10 : 2);
          rt_.set_num_val(UiNumberId::VOLUME_STEP_SIZE, static_cast<float>(nxt_sz));
          rt_.schedule_disp();
        } else if (slot == 3 && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          rt_.sw_toggle(UiSwitchId::GAME_SOUND);
          rt_.schedule_disp();
        }
      }
      return;
    }

    if (rs.nav.system_menu_mode == 8) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = 0;
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }

    if (rs.nav.system_menu_mode == 9) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (rs.nav.audio_menu_parent_page == PageId::MUSIC) {
          rs.nav.gui_page = PageId::MUSIC;
          rs.music.overlay_mode = 5;
          rs.nav.audio_menu_parent_page = PageId::SYSTEM;
          rs.nav.system_menu_mode = 0;
          const auto &ms = media_state();
          if (rt_.select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE) != "None" &&
              (!ms.is_local || rt_.album_art_w == 0) &&
              !ms.art_url.empty() && ms.art_url != rs.music.loaded_art_url) {
            rt_.cmd(UiHostCmd::FETCH_REMOTE_ART_DEBOUNCED);
          }
        } else {
          rs.nav.system_menu_mode = 0;
        }
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const int slot = hit->param;
          if (slot == 0) {
            rt_.set_select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE),
                                            {"Full Screen", "Letterbox", "Standard", "None"}));
          } else if (slot == 1) {
            rt_.set_select_opt(UiSelectId::MUSIC_OVERLAY_STYLE,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_OVERLAY_STYLE),
                                            {"Art Only", "Art + Metadata", "Always Visible"}));
          } else if (slot == 2) {
            rt_.set_select_opt(UiSelectId::MUSIC_ALBUM_SORT_ORDER,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_ALBUM_SORT_ORDER),
                                            {"Alphabetical", "Newest on Top", "Oldest on Top"}));
          } else if (slot == 3) {
            rt_.set_select_opt(UiSelectId::MUSIC_ARTIST_ALBUM_SORT_ORDER,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_ARTIST_ALBUM_SORT_ORDER),
                                            {"Oldest on Top", "Newest on Top", "Alphabetical"}));
          } else if (slot == 4) {
            rt_.set_select_opt(UiSelectId::MUSIC_PODCAST_SORT_ORDER,
                               cycle_option(rt_.select_opt(UiSelectId::MUSIC_PODCAST_SORT_ORDER),
                                            {"Newest on Top", "Oldest on Top"}));
          }
          rt_.schedule_disp();
        }
      }
      return;
    }

    if (rs.nav.system_menu_mode == 11) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = 0;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const int slot = hit->param;
          if (slot == 0) {
            rt_.set_select_opt(UiSelectId::VA_WAKE_WORD_MODE,
                               cycle_option(rt_.select_opt(UiSelectId::VA_WAKE_WORD_MODE),
                                            {"Docked Only", "Always On", "Disabled"}));
            rt_.schedule_disp();
          } else if (slot == 1) {
            if (rt_.select_opt(UiSelectId::VA_WAKE_WORD_MODE) != "Disabled") {
              rt_.cmd(UiHostCmd::CYCLE_PRIMARY_WAKE_WORD);
              rt_.schedule_disp();
            }
          } else if (slot == 2) {
            if (rt_.select_opt(UiSelectId::VA_WAKE_WORD_MODE) != "Disabled") {
              rs.nav.system_menu_mode = 12;
              mark_dirty_full();
              rt_.schedule_disp();
            }
          } else if (slot == 3) {
            rt_.set_select_opt(UiSelectId::VA_VISUAL_PROFILE,
                               cycle_option(rt_.select_opt(UiSelectId::VA_VISUAL_PROFILE),
                                            {"Nebula", "Solaris", "Rings", "Light Bar", "Dot Wave"}));
            rt_.schedule_disp();
          } else if (slot == 4) {
            rt_.sw_toggle(UiSwitchId::VA_WAKE_SCREEN);
            rt_.schedule_disp();
          }
        }
      }
      return;
    }

    if (rs.nav.system_menu_mode == 12) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = 11;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const int slot = std::clamp(static_cast<int>(hit->param), 0, 4);
          static constexpr UiSelectId kSensIds[5] = {
              UiSelectId::WW_SENS_OKAY_NABU,
              UiSelectId::WW_SENS_HEY_LUNA,
              UiSelectId::WW_SENS_HEY_JARVIS,
              UiSelectId::WW_SENS_HEY_MYCROFT,
              UiSelectId::WW_SENS_ALEXA,
          };
          rt_.set_select_opt(kSensIds[slot],
                             cycle_option(rt_.select_opt(kSensIds[slot]),
                                          {"Slightly sensitive", "Moderately sensitive", "Very sensitive"}));
          rt_.schedule_disp();
        }
      }
      return;
    }

    if (rs.nav.system_menu_mode == 13) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = 2;
        rt_.cmd(UiHostCmd::APPLY_THEME_PRESET);
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::COLOR_WHEEL_SAVE && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        save_persisted_prefs();
        rt_.set_select_opt(UiSelectId::THEME_PALETTE, "Custom");
        rt_.cmd(UiHostCmd::SAVE_CUSTOM_COLOR_TO_LATTER);
        rs.nav.system_menu_mode = 2;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::COLOR_WHEEL_RING) {
        float hue = 0.0f, sat = 1.0f;
        if (ui::hsv_wheel_pick(touch.x, touch.y, SCREEN_CX, scale_y(180), 156, 56, hue, sat)) {
          color_wheel_hue_deg() = hue;
          color_wheel_sat() = sat;
          const esphome::Color c = hsv_to_rgb(hue / 360.0f, sat, 1.0f);
          prefs.custom_theme_rgb = (static_cast<uint32_t>(c.r) << 16) |
                                   (static_cast<uint32_t>(c.g) << 8) |
                                   static_cast<uint32_t>(c.b);
          if (rt_.theme_primary != nullptr && rt_.theme_secondary != nullptr) {
            apply_custom_rgb_harmony(prefs.custom_theme_rgb, *rt_.theme_primary, *rt_.theme_secondary);
          }
          rt_.schedule_disp();
        }
      }
      return;
    }
    return;
  }

  // Page 5: Weather & Sky (mode bar & horizontal swipes resolve in on_touch_move / on_touch_up)
  if (rs.nav.gui_page == PageId::WEATHER) {
    return;
  }

  // Page 6: Area Control
  if (rs.nav.gui_page == PageId::AREA) {
    if (rs.nav.area_subpage > 0 && wid == WidgetId::BACK_BUTTON) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (rs.nav.area_subpage == AreaSubpage::PICKER && rs.area.active_area_id.empty()) {
          if (!prefs.home_screen_enabled && is_on_anchor_root(rs, prefs)) return;
          rs.nav.area_subpage = AreaSubpage::OVERVIEW;
          navigate_home(now_touch);
          return;
        }
        rs.nav.area_subpage = AreaSubpage::OVERVIEW;
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
          rt_.cmd(UiHostCmd::FETCH_AREA_LIST_BROWSE);
        }
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }
    if (rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
      if (wid == WidgetId::LIGHT_COLOR_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        light_insp_mode() = (light_insp_mode() == 0) ? 1 : 0;
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::COLOR_WHEEL_RING) {
        float hue = 0.0f, sat = 1.0f;
        if (ui::hsv_wheel_pick(touch.x, touch.y, SCREEN_CX, scale_y(176), 140, 62, hue, sat)) {
          light_insp_hue_deg() = hue;
          light_insp_sat() = sat;
          update_light_inspector_hex(hue, sat);
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::LIGHT_TEMP_SLIDER) {
        const float f = hit->norm_x(touch.x);
        const int raw_k = 2000 + static_cast<int>(f * 4500.0f);
        light_insp_kelvin() = std::clamp(((raw_k + 50) / 100) * 100, 2000, 6500);
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::LIGHT_BRI_SLIDER) {
        const float f = hit->norm_x(touch.x);
        const int raw_b = static_cast<int>(f * 100.0f);
        light_insp_bri_pct() = std::clamp(((raw_b + 2) / 5) * 5, 5, 100);
        rt_.schedule_disp();
        return;
      }
    }
    return;
  }

  // Page 7: Universal TV Remote
  if (rs.nav.gui_page == PageId::REMOTE) {
    if (rs.nav.remote_subpage == 1) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.remote.action_feedback.clear();
        rs.nav.remote_subpage = 0;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
        int valid_slots[MAX_REMOTE_SLOTS] = {0};
        const int valid_count = collect_configured_remote_slots(valid_slots);
        const int total_pages = std::max(1, (valid_count + 3) / 4);
        const int next_pg = std::clamp(
            remote_control_page() + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 0, total_pages - 1);
        if (next_pg != remote_control_page()) {
          remote_control_page() = next_pg;
          rt_.schedule_disp();
        }
      }
      return;  // Slot cards & power buttons resolve in on_touch_up
    }

    const int act_slot = resolve_active_remote_slot(rs.remote.active_slot);
    rs.remote.active_slot = act_slot;

    if (wid == WidgetId::REMOTE_SLOT_PILL) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.remote.action_feedback.clear();
        rs.nav.remote_subpage = 1;
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }

    int remote_key = -1;
    uint32_t debounce_ms = TOUCH_DEBOUNCE_RAPID;
    switch (wid) {
      case WidgetId::REMOTE_DPAD_UP:    remote_key = 0; break;
      case WidgetId::REMOTE_DPAD_DOWN:  remote_key = 1; break;
      case WidgetId::REMOTE_DPAD_LEFT:  remote_key = 2; break;
      case WidgetId::REMOTE_DPAD_RIGHT: remote_key = 3; break;
      case WidgetId::REMOTE_DPAD_OK:    remote_key = 4; debounce_ms = TOUCH_DEBOUNCE_BUTTON; break;
      case WidgetId::REMOTE_NAV_BACK:   remote_key = 5; debounce_ms = TOUCH_DEBOUNCE_BUTTON; break;
      case WidgetId::REMOTE_NAV_HOME:   remote_key = 6; debounce_ms = TOUCH_DEBOUNCE_BUTTON; break;
      case WidgetId::REMOTE_VOL_UP:     remote_key = 7; break;
      case WidgetId::REMOTE_VOL_DOWN:   remote_key = 8; break;
      default: break;
    }
    if (remote_key >= 0 && touch_router().allow_tap(now_touch, debounce_ms)) {
      rt_.cmd(UiHostCmd::REMOTE_SEND_KEY, remote_key, act_slot);
      rt_.schedule_disp();
    }
    return;
  }

  // Page 8: Tools & Communications
  if (rs.nav.gui_page == PageId::TOOLS) {
    const int sub = rs.nav.tools_subpage;
    if (sub == ToolsSubpage::LAUNCHER) {
      return;  // Grid cards & vertical page swipes resolve in on_touch_up
    }

    if (sub == ToolsSubpage::TIMERS) {
      auto *slots = timer_slots();
      int &view_idx = timer_view_slot();

      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (tools_direct_home()) {
          navigate_home(now_touch);
          return;
        }
        if (view_idx > 0) {
          view_idx = 0;
        } else {
          rs.nav.tools_subpage = 0;
        }
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }

      if (view_idx == 0) {
        if (wid >= WidgetId::TIMER_TAB_BASE && wid < WidgetId::TIMER_TAB_BASE + 3) {
          if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
            view_idx = std::clamp(static_cast<int>(hit->param), 0, 2) + 1;
            mark_dirty_full();
            rt_.schedule_disp();
          }
        } else if (wid == WidgetId::TIMER_PRIMARY_BTN) {
          if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
            const int idx = std::clamp(static_cast<int>(hit->param), 0, 2);
            toggle_timer_slot_run(slots[idx], now_touch, rt_);
          }
        }
        return;
      }

      const int s_idx = std::clamp(view_idx - 1, 0, 2);
      auto &s = slots[s_idx];

      if (wid == WidgetId::TIMER_PRIMARY_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        toggle_timer_slot_run(s, now_touch, rt_);
        return;
      }
      if (wid >= WidgetId::STEPPER_BASE && wid < WidgetId::STEPPER_BASE + 4) {
        if (!s.running && !s.paused && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
          const int col = static_cast<int>(wid - WidgetId::STEPPER_BASE);
          if (col == 0) s.duration_sec = std::clamp(s.duration_sec - 300, 60, 3600);
          else if (col == 1) s.duration_sec = std::clamp(s.duration_sec - 60, 60, 3600);
          else if (col == 2) s.duration_sec = std::clamp(s.duration_sec + 60, 60, 3600);
          else if (col == 3) s.duration_sec = std::clamp(s.duration_sec + 300, 60, 3600);
          s.remaining_sec = s.duration_sec;
          sync_multi_timer_state();
          rt_.apply_power_policy();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::TIMER_RESET_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (s.running || s.paused) {
          s.running = false;
          s.paused = false;
          s.remaining_sec = s.duration_sec;
        } else {
          s.name.clear();
          s.id.clear();
          s.duration_sec = (s_idx + 1) * 300;
          s.remaining_sec = s.duration_sec;
        }
        sync_multi_timer_state();
        rt_.apply_power_policy();
        rt_.schedule_disp();
      }
      return;
    }

    if (sub == ToolsSubpage::ALARMS) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (tools_direct_home()) {
          navigate_home(now_touch);
          return;
        }
        if (rs.alarms.edit_idx > 0) {
          rs.alarms.edit_idx = 0;
        } else {
          rs.nav.tools_subpage = 0;
        }
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }

      if (rs.alarms.edit_idx == 0 &&
          wid >= WidgetId::ALARM_TOGGLE_BASE && wid < WidgetId::ALARM_TOGGLE_BASE + 3) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const int a_idx = std::clamp(static_cast<int>(wid - WidgetId::ALARM_TOGGLE_BASE), 0, 2);
          if (rs.alarms.snooze_until > 0 &&
              (rs.alarms.ringing_idx == a_idx + 1 || rs.alarms.ringing_idx == 0)) {
            rs.alarms.snooze_until = 0;
          } else {
            prefs.alarms[a_idx].enabled = !prefs.alarms[a_idx].enabled;
            if (!prefs.alarms[a_idx].enabled && rs.alarms.ringing_idx == a_idx + 1) {
              rs.alarms.snooze_until = 0;
            }
            save_persisted_prefs();
          }
          rt_.schedule_disp();
        }
        return;
      }

      if (rs.alarms.edit_idx == 0) {
        if (wid >= WidgetId::ALARM_CARD_BASE && wid < WidgetId::ALARM_CARD_BASE + 3) {
          if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
            const int a_idx = std::clamp(static_cast<int>(wid - WidgetId::ALARM_CARD_BASE), 0, 2);
            if (rs.alarms.snooze_until > 0 &&
                (rs.alarms.ringing_idx == a_idx + 1 || rs.alarms.ringing_idx == 0)) {
              rs.alarms.snooze_until = 0;
            } else {
              rs.alarms.edit_idx = a_idx + 1;
              mark_dirty_full();
            }
            rt_.schedule_disp();
          }
        }
        return;
      }

      const int a_idx = std::clamp(rs.alarms.edit_idx - 1, 0, 2);
      auto &ap = prefs.alarms[a_idx];
      if (wid >= WidgetId::STEPPER_BASE && wid < WidgetId::STEPPER_BASE + 4) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
          const int col = static_cast<int>(wid - WidgetId::STEPPER_BASE);
          if (col == 0) ap.hour = static_cast<uint8_t>((ap.hour + 23) % 24);
          else if (col == 1) ap.hour = static_cast<uint8_t>((ap.hour + 1) % 24);
          else if (col == 2) ap.minute = static_cast<uint8_t>((ap.minute + 55) % 60);
          else if (col == 3) ap.minute = static_cast<uint8_t>((ap.minute + 5) % 60);
          save_persisted_prefs();
          rt_.schedule_disp();
        }
        return;
      }
      if (wid == WidgetId::ALARM_RECUR_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        ap.recurrence = static_cast<uint8_t>((ap.recurrence + 1) % 4);
        save_persisted_prefs();
        rt_.schedule_disp();
      }
      return;
    }

    if (sub == ToolsSubpage::STOPWATCH) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (tools_direct_home()) {
          navigate_home(now_touch);
          return;
        }
        rs.nav.tools_subpage = 0;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::SW_STYLE_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        stopwatch_modern_digital() = !stopwatch_modern_digital();
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::SW_PRIMARY_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (!rs.stopwatch.running) {
          rs.stopwatch.running = true;
          rs.stopwatch.start_time_ms = now_touch - rs.stopwatch.elapsed_ms;
          rs.nav.sleep_start_time = 0;
        } else {
          rs.stopwatch.running = false;
          rs.stopwatch.elapsed_ms = now_touch - rs.stopwatch.start_time_ms;
        }
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::SW_LAP_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (rs.stopwatch.running) {
          rs.stopwatch.lap_ms = now_touch - rs.stopwatch.start_time_ms;
        } else if (rs.stopwatch.elapsed_ms > 0) {
          rs.stopwatch.lap_ms = rs.stopwatch.elapsed_ms;
        }
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::SW_RESET_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.stopwatch.running = false;
        rs.stopwatch.elapsed_ms = 0;
        rs.stopwatch.lap_ms = 0;
        rt_.schedule_disp();
      }
      return;
    }

    if (sub == ToolsSubpage::DICE) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.tools_subpage = 0;
        rt_.apply_power_policy();
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::DICE_ROLL_BTN && !dice_state().anim_active &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        trigger_dice_roll(now_touch, next_rng_u32(), next_rng_u32());
        rt_.schedule_disp();
      }
      return;
    }

    if (sub == ToolsSubpage::FLASHLIGHT) {
      if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.tools_subpage = 0;
        rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }

    if (sub == ToolsSubpage::CALCULATOR) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.tools_subpage = 0;
        reset_calculator_state();
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid >= WidgetId::CALC_KEY_BASE && wid < WidgetId::CALC_KEY_BASE + 20) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
          handle_calculator_keypad_tap(touch.x, touch.y);
          rt_.schedule_disp();
        }
      }
      return;
    }

    if (sub == ToolsSubpage::LEVEL) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.tools_subpage = 0;
        rt_.apply_power_policy();
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;
    }

    if (sub == ToolsSubpage::COIN_FLIP) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.tools_subpage = 0;
        rt_.apply_power_policy();
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::COIN_FLIP_BTN && !coin_state().anim_active &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        trigger_coin_flip(now_touch, next_rng_u32());
        rt_.schedule_disp();
      }
      return;
    }

    if (sub == ToolsSubpage::CALENDAR) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (cal_detail_mode() == 2) {
          cal_detail_mode() = 1;
          rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
          rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
        } else if (cal_detail_mode() == 1) {
          cal_detail_mode() = 0;
        } else if (cal_quick_add_open()) {
          cal_quick_add_open() = false;
        } else if (tools_direct_home()) {
          navigate_home(now_touch);
          return;
        } else {
          rs.nav.tools_subpage = 0;
        }
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }

      if (cal_detail_mode() == 2) {
        return;  // Map pan & zoom resolve in on_touch_up
      }
      if (cal_detail_mode() == 1) {
        if (wid == WidgetId::CAL_EVENT_BASE && !cal_detail_location().empty() &&
            touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          cal_map_zoom() = 15;
          cal_map_pan_x() = 0;
          cal_map_pan_y() = 0;
          cal_detail_mode() = 2;
          rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
          rt_.cmd(UiHostCmd::FETCH_CALENDAR_MAP_SNAPSHOT);
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return;
      }
      if (cal_quick_add_open()) {
        if (wid >= WidgetId::CAL_PRESET_BASE && wid < WidgetId::CAL_PRESET_BASE + 5) {
          if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
            const int q_idx = std::clamp(static_cast<int>(wid - WidgetId::CAL_PRESET_BASE), 0, 4);
            static constexpr const char *kQuickSummaries[5] = {
                "Quick Reminder", "Check-In / Task", "Follow-Up Call",
                "Errand / Pickup", "Morning Reminder"};
            static constexpr int kQuickInMins[5] = {15, 30, 60, 180, 720};
            rs.cal.quick_summary = kQuickSummaries[q_idx];
            rs.cal.quick_in_minutes = kQuickInMins[q_idx];
            rs.cal.quick_duration_minutes = 30;
            cal_quick_add_open() = false;
            cal_feedback_text() = std::string("Added: ") + kQuickSummaries[q_idx];
            cal_feedback_ms() = now_touch;
            rt_.cmd(UiHostCmd::CREATE_CALENDAR_QUICK_EVENT);
            mark_dirty_full();
            rt_.schedule_disp();
          }
        }
        return;
      }
      if (wid == WidgetId::CAL_QUICK_ADD_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        cal_quick_add_open() = true;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_RAPID)) {
        int &pg = rs.slot_page_for(latter_rpc::SlotChannel::CALENDAR);
        const int tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::CALENDAR);
        const int next_pg = std::clamp(pg + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, std::max(1, tot));
        if (next_pg != pg) {
          pg = next_pg;
          rt_.cmd(UiHostCmd::FETCH_CALENDAR_BROWSE);
          rt_.schedule_disp();
        }
      }
      return;  // Calendar event rows resolve in on_touch_up
    }

    if (sub == ToolsSubpage::INTERCOM) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.tools_subpage = 0;
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (wid == WidgetId::INTERCOM_MODE_BTN && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        intercom_live_call_mode() = !intercom_live_call_mode();
        rt_.schedule_disp();
        return;
      }
      if (wid >= WidgetId::INTERCOM_ROW_BASE && wid < WidgetId::INTERCOM_ROW_BASE + 5) {
        if (touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
          const int slot_idx = static_cast<int>(wid - WidgetId::INTERCOM_ROW_BASE);
          if (intercom_live_call_mode()) {
            std::string t_ids[5], t_lbls[5];
            const int cnt = parse_intercom_targets_list(
                area_ctx_state().intercom_targets_summary, t_ids, t_lbls);
            if (hit->param < 0 || (slot_idx == 4 && cnt < 5)) {
              gate_or_dispatch_doorbell_unlock(now_touch, true);
              return;
            }
            if (slot_idx < cnt) {
              rs.intercom.cam_action = "call_target";
              rs.intercom.cam_message = t_ids[slot_idx];
              rs.intercom.broadcast_feedback = "Call: " + t_lbls[slot_idx];
              rs.intercom.broadcast_feedback_ms = now_touch;
              intercom_session().emit_current_cmd();
              cam_quick_reply_open() = false;
              rs.nav.tools_subpage = ToolsSubpage::CAMERAS;
              rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
              intercom_session().start_call(now_touch);
              mark_dirty_full();
              rt_.schedule_disp();
            }
          } else {
            static constexpr const char *kIntercomPresets[5] = {
                "Dinner is ready!",
                "Time to head out!",
                "Everyone, please come downstairs!",
                "Could use a hand in here!",
                "Movie night is starting!",
            };
            rs.intercom.broadcast_message = kIntercomPresets[slot_idx];
            rs.intercom.broadcast_feedback = std::string("Sent: ") + kIntercomPresets[slot_idx];
            rs.intercom.broadcast_feedback_ms = now_touch;
            rt_.cmd(UiHostCmd::SEND_INTERCOM_BROADCAST);
            rt_.schedule_disp();
          }
        }
      }
      return;
    }

    if (sub == ToolsSubpage::CAMERAS) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_touch, TOUCH_DEBOUNCE_BUTTON)) {
        if (cam_quick_reply_open()) {
          cam_quick_reply_open() = false;
          mark_dirty_full();
          rt_.schedule_disp();
          return;
        }
        if (tools_direct_home()) {
          if (!prefs.home_screen_enabled && is_on_anchor_root(rs, prefs)) return;
          navigate_home(now_touch);
          rt_.apply_power_policy();
          return;
        }
        if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
          intercom_session().stop_call_with_hw(now_touch);
        }
        rs.nav.tools_subpage = 0;
        rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
        rt_.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
        rt_.apply_power_policy();
        mark_dirty_full();
        rt_.schedule_disp();
      }
      return;  // Camera deck & carousel swipes resolve in on_touch_up
    }
  }
}

void TouchGestureEngine::on_touch_move(int tx, int ty, uint32_t now_u) {
  auto &gest = touch_gesture();
  if (!gest.active) return;

  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  gest.last_touch_ms = now_u;
  last_user_touch_ms() = now_u;

  if (rs.nav.display_power_state == 0 && !rs.voice.active &&
      (now_u - last_drag_sleep_rst_ms_ >= 2000U)) {
    last_drag_sleep_rst_ms_ = now_u;
    display_lifecycle().note_activity(now_u);
  }

  if (tx != gest.last_x || ty != gest.last_y) {
    gest.last_x = tx;
    gest.last_y = ty;
    gest.sample_count += 1;
    const int cur_dist = std::max(std::abs(tx - gest.start_x), std::abs(ty - gest.start_y));
    if (cur_dist > gest.max_travel) gest.max_travel = cur_dist;
  }

  // Active steering games receive continuous touch coordinates
  if (rs.nav.gui_page == PageId::GAMES && rs.nav.active_game > 0 && rs.nav.active_game != 5) {
    if (ty < layout().back_y - scale_y(30)) {
      rs.nav.marble_touch_x = static_cast<float>(tx);
      rs.nav.marble_touch_y = static_cast<float>(ty);
    }
    return;
  }

  const int cur_ctx = compute_touch_context(
      rs.nav.gui_page, rs.nav.home_subpage, rs.music.overlay_mode, rs.nav.active_game,
      rs.nav.system_menu_mode, rs.nav.sky_weather_subpage, rs.nav.area_subpage,
      rs.nav.remote_subpage, rs.nav.tools_subpage, whole_home_tab());
  if (cur_ctx != gest.start_context) return;

  // Custom Theme Color Wheel continuous drag (System Mode 13)
  if (rs.nav.gui_page == PageId::SYSTEM && rs.nav.system_menu_mode == 13 &&
      (now_u - last_disp_update_ms() >= 42U)) {
    float hue = 0.0f, sat = 1.0f;
    if (ui::hsv_wheel_pick(tx, ty, SCREEN_CX, scale_y(180), 156, 56, hue, sat)) {
      color_wheel_hue_deg() = hue;
      color_wheel_sat() = sat;
      const esphome::Color c = hsv_to_rgb(hue / 360.0f, sat, 1.0f);
      prefs.custom_theme_rgb = (static_cast<uint32_t>(c.r) << 16) |
                               (static_cast<uint32_t>(c.g) << 8) |
                               static_cast<uint32_t>(c.b);
      if (rt_.theme_primary != nullptr && rt_.theme_secondary != nullptr) {
        apply_custom_rgb_harmony(prefs.custom_theme_rgb, *rt_.theme_primary, *rt_.theme_secondary);
      }
      rt_.schedule_disp();
    }
    return;
  }

  if (now_u - gest.last_swipe_ms < TOUCH_POST_SWIPE_LOCKOUT) return;
  if (gest.swipe_committed) return;

  const auto &ly = layout();
  const int dx = tx - gest.start_x;
  const int dy = ty - gest.start_y;

  // 1. Universal bottom-edge swipe-up -> Home ([HOME PREDICATE])
  const bool can_swipe_home = can_go_home(rs);
  if (can_swipe_home && gest.start_y >= ly.home_zone_y() &&
      dy <= -ly.swipe_min() && std::abs(dy) > std::abs(dx)) {
    gest.swipe_committed = true;
    gest.last_swipe_ms = now_u;
    navigate_home(now_u);
    return;
  }

  // 2. Horizontal Slider Drags via FrameHitTable (kHitHorizSlider)
  const HitRegion *sl_hit = frame_hits().hit_test(gest.start_x, gest.start_y, kHitHorizSlider);
  if (sl_hit != nullptr && std::abs(dx) >= ly.drag_start() && std::abs(dx) >= std::abs(dy)) {
    if (sl_hit->widget_id >= WidgetId::AREA_CARD_SLIDER &&
        sl_hit->widget_id < WidgetId::AREA_CARD_SLIDER + 3 &&
        rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::OVERVIEW &&
        rs.area.browse_mode != "areas") {
      const int idx = std::clamp(static_cast<int>(sl_hit->param), 0, 2);
      auto &sl = rs.area_entity_slots()[idx];
      if (!sl.id.empty() && sl.type != "scene" && sl.type != "switch") {
        const float f = sl_hit->norm_x(tx);
        const int stepped = std::clamp(((static_cast<int>(f * 100.0f) + 2) / 5) * 5, 5, 100);
        if (stepped != sl.level && (now_u - last_area_slider_ms_ >= 36U)) {
          sl.level = stepped;
          sl.active = true;
          last_area_slider_ms_ = now_u;
          const int y_mid = ly.area_card3_y(idx);
          const int hh = ly.area_card3_h / 2 + 4;
          rt_.schedule_band_disp(y_mid - hh, y_mid + hh);
        }
        return;
      }
    } else if (sl_hit->widget_id == WidgetId::MUSIC_SCRUB_BAR &&
               rs.nav.gui_page == PageId::MUSIC && rs.music.overlay_mode == 6) {
      const float dur = media_state().duration;
      if (!std::isnan(dur) && dur > 0.0f && (now_u - last_disp_update_ms() >= 42U)) {
        rs.music.pos_base = sl_hit->norm_x(tx) * dur;
        rs.music.pos_sync_time = now_u;
        rt_.schedule_band_disp(scale_y(182), ly.music_scrub_y + scale_y(24));
      }
      return;
    } else if (sl_hit->widget_id == WidgetId::LIGHT_TEMP_SLIDER &&
               rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
      const int raw_k = 2000 + static_cast<int>(sl_hit->norm_x(tx) * 4500.0f);
      const int next_k = std::clamp(((raw_k + 50) / 100) * 100, 2000, 6500);
      if (next_k != light_insp_kelvin() && (now_u - last_disp_update_ms() >= 42U)) {
        light_insp_kelvin() = next_k;
        rt_.schedule_disp();
      }
      return;
    } else if (sl_hit->widget_id == WidgetId::LIGHT_BRI_SLIDER &&
               rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
      const int raw_b = static_cast<int>(sl_hit->norm_x(tx) * 100.0f);
      const int next_b = std::clamp(((raw_b + 2) / 5) * 5, 5, 100);
      if (next_b != light_insp_bri_pct() && (now_u - last_disp_update_ms() >= 42U)) {
        light_insp_bri_pct() = next_b;
        rt_.schedule_disp();
      }
      return;
    }
  }

  // Light Inspector HSV Wheel continuous drag (Area Subpage 2)
  if (rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL &&
      light_insp_mode() == 0 && (now_u - last_disp_update_ms() >= 42U)) {
    float hue = 0.0f, sat = 1.0f;
    if (ui::hsv_wheel_pick(tx, ty, SCREEN_CX, scale_y(176), 140, 62, hue, sat)) {
      light_insp_hue_deg() = hue;
      light_insp_sat() = sat;
      update_light_inspector_hex(hue, sat);
      rt_.schedule_disp();
      return;
    }
  }

  // 3. Home Dock Horizontal Window Swipe
  if (rs.nav.gui_page == PageId::HOME && rs.nav.home_subpage == 0 &&
      gest.start_y >= ly.home_dock_zone_top(home_weather_text_h(), home_activity_card_shown())) {
    if (std::abs(dx) >= ly.swipe_short() && std::abs(dx) > std::abs(dy)) {
      int dock_apps[kMaxDockApps];
      const int dock_cnt = collect_home_dock_apps(dock_apps);
      const int dock_pages = (dock_cnt + 3) / 4;
      if (dock_pages > 1) {
        const int target_pg = std::clamp(home_dock_page() + ((dx < 0) ? 1 : -1), 0, dock_pages - 1);
        if (target_pg != home_dock_page()) {
          gest.swipe_committed = true;
          gest.last_swipe_ms = now_u;
          home_dock_page() = target_pg;
          rt_.schedule_disp();
        }
      }
    }
    return;
  }

  // 4. Weather Page Horizontal Subpage Swipe
  if (rs.nav.gui_page == PageId::WEATHER && gest.start_y < ly.home_zone_y()) {
    if (std::abs(dx) >= ly.wide_swipe() && std::abs(dx) > std::abs(dy)) {
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_u;
      rs.nav.sky_weather_subpage = step_wrap(rs.nav.sky_weather_subpage, (dx < 0) ? 1 : -1, 4);
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return;
  }

  // 5. System Animated 2-Page Vertical Scroll (Modes 0, 1, 2)
  if (rs.nav.gui_page == PageId::SYSTEM && !rs.nav.menu_scroll_active &&
      (rs.nav.system_menu_mode == 0 || rs.nav.system_menu_mode == 1 || rs.nav.system_menu_mode == 2) &&
      gest.start_y < scale_y(404)) {
    if (std::abs(dy) >= ly.list_swipe() && std::abs(dy) > std::abs(dx)) {
      const int cur_pg = (rs.nav.system_menu_mode == 0)
                             ? rs.nav.system_dashboard_page
                             : ((rs.nav.system_menu_mode == 1) ? rs.nav.sleep_menu_page
                                                               : rs.nav.display_menu_page);
      const int target_pg = (dy < 0) ? 2 : 1;
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_u;
      if (target_pg != cur_pg) {
        rs.nav.menu_scroll_active = true;
        rs.nav.menu_scroll_dir = (target_pg == 2) ? 1 : -1;
        rs.nav.menu_scroll_target_page = target_pg;
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      }
    }
    return;
  }

  // 6. Games Launcher Horizontal Carousel Swipe
  if (rs.nav.gui_page == PageId::GAMES && rs.nav.active_game == 0 && !rs.nav.menu_scroll_active &&
      gest.start_y < ly.home_zone_y()) {
    if (std::abs(dx) >= ly.list_swipe() && std::abs(dx) > std::abs(dy)) {
      const int dir = (dx < 0) ? 1 : -1;
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_u;
      rs.nav.menu_scroll_active = true;
      rs.nav.menu_scroll_dir = dir;
      rs.nav.menu_scroll_target_page = step_wrap(rs.nav.game_carousel_idx, dir, 6);
      rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
    }
    return;
  }

  // 7. Tools Launcher Vertical Page Swipe
  if (rs.nav.gui_page == PageId::TOOLS && rs.nav.tools_subpage == ToolsSubpage::LAUNCHER &&
      !rs.nav.menu_scroll_active && gest.start_y < ly.home_zone_y()) {
    if (std::abs(dy) >= ly.list_swipe() && std::abs(dy) > std::abs(dx)) {
      const int target_pg = (dy < 0) ? 1 : 0;
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_u;
      if (target_pg != rs.nav.tools_menu_page) {
        rs.nav.menu_scroll_active = true;
        rs.nav.menu_scroll_dir = (target_pg == 1) ? 1 : -1;
        rs.nav.menu_scroll_target_page = target_pg;
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      }
    }
    return;
  }

  // 8. Music Library Category Vertical Scroll (Mode 2)
  if (rs.nav.gui_page == PageId::MUSIC && rs.music.overlay_mode == 2 &&
      !rs.nav.menu_scroll_active && gest.start_y < scale_y(404)) {
    if (std::abs(dy) >= ly.list_swipe() && std::abs(dy) > std::abs(dx)) {
      const int target_pg = (dy < 0) ? 2 : 1;
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_u;
      music_scroll_tap_lockout_until_ms() = now_u + 180U;
      if (target_pg != rs.music.category_page) {
        rs.nav.menu_scroll_active = true;
        rs.nav.menu_scroll_dir = (target_pg == 2) ? 1 : -1;
        rs.nav.menu_scroll_target_page = target_pg;
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      }
    }
    return;
  }

  // 9. Area Control Category Canvas / Mode-Bar Swipe & Vertical Entity Paging
  if (rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::OVERVIEW &&
      rs.area.browse_mode != "areas" && sl_hit == nullptr) {
    if (gest.start_y >= ly.mode_bar_zone_top() && gest.start_y < ly.edge_guard_y() &&
        std::abs(dx) >= ly.page_swipe() && std::abs(dx) > std::abs(dy)) {
      std::string cats[8];
      const int cat_cnt = collect_area_categories(cats);
      if (cat_cnt > 4) {
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_u;
        area_cat_dock_page() = std::clamp(area_cat_dock_page() + ((dx < 0) ? 4 : -4), 0, cat_cnt - 4);
        rt_.schedule_disp();
      }
      return;
    }
    if (gest.start_y < ly.mode_bar_zone_top() &&
        std::abs(dx) >= ly.canvas_swipe() && std::abs(dx) > std::abs(dy)) {
      std::string cats[8];
      const int cat_cnt = collect_area_categories(cats);
      if (cat_cnt > 1) {
        int cur_c = 0;
        for (int i = 0; i < cat_cnt; i++) {
          if (cats[i] == rs.area.domain_filter) {
            cur_c = i;
            break;
          }
        }
        const int next_c = step_wrap(cur_c, (dx < 0) ? 1 : -1, cat_cnt);
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_u;
        rs.area.domain_filter = cats[next_c];
        sync_mode_bar_window(area_cat_dock_page(), next_c, cat_cnt);
        rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
        rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
        rt_.schedule_disp();
      }
      return;
    }
    if (gest.start_y < ly.mode_bar_zone_top() &&
        std::abs(dy) >= ly.list_swipe() && std::abs(dy) > std::abs(dx)) {
      int &pg = rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES);
      const int tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::AREA_ENTITIES);
      if (tot > 1) {
        const int next_pg = std::clamp(pg + ((dy < 0) ? 1 : -1), 1, tot);
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_u;
        if (next_pg != pg) {
          pg = next_pg;
          rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
          rt_.schedule_disp();
        }
      }
      return;
    }
  }

  // 10. Whole-Home Hub Horizontal Tab Swipe
  if (rs.nav.gui_page == PageId::WHOLE_HOME && gest.start_y < ly.home_zone_y()) {
    if (std::abs(dx) >= ly.page_swipe() && std::abs(dx) > std::abs(dy)) {
      int wh_tabs[4] = {0, 0, 0, 0};
      const int wh_cnt = collect_home_hub_tabs(wh_tabs);
      if (wh_cnt > 1) {
        int cur_slot = 0;
        for (int i = 0; i < wh_cnt; i++) {
          if (wh_tabs[i] == whole_home_tab()) {
            cur_slot = i;
            break;
          }
        }
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_u;
        whole_home_tab() = wh_tabs[step_wrap(cur_slot, (dx < 0) ? 1 : -1, wh_cnt)];
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
    return;
  }
}

void TouchGestureEngine::on_touch_up(uint32_t now_rel) {
  auto &gest = touch_gesture();
  if (!gest.active) return;

  const uint64_t rel_t0_us = profiler_enabled() ? prof_now_us() : 0ULL;
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();

  if (!rs.nav.hud_wake_finger_released) {
    rs.nav.hud_wake_finger_released = true;
  }

  const auto &ly = layout();
  const int dx = gest.last_x - gest.start_x;
  const int dy = gest.last_y - gest.start_y;
  const TouchPt tp{gest.last_x, gest.last_y};
  const HitRegion *hit = frame_hits().hit_test(tp.x, tp.y, kHitTap);
  if (hit == nullptr && gest.max_travel <= ly.tap_max_loose()) {
    hit = frame_hits().hit_test(gest.start_x, gest.start_y, kHitTap);
  }
  const uint16_t wid = (hit != nullptr) ? hit->widget_id : WidgetId::NONE;
  const HitRegion *start_slider = frame_hits().hit_test(gest.start_x, gest.start_y, kHitHorizSlider);

  const int cur_ctx = compute_touch_context(
      rs.nav.gui_page, rs.nav.home_subpage, rs.music.overlay_mode, rs.nav.active_game,
      rs.nav.system_menu_mode, rs.nav.sky_weather_subpage, rs.nav.area_subpage,
      rs.nav.remote_subpage, rs.nav.tools_subpage, whole_home_tab());

  if (cur_ctx == gest.start_context) {
    // 1. Universal Home Pill Tap / Release Swipe ([HOME PREDICATE])
    const bool allow_home = can_go_home(rs);
    if (!gest.swipe_committed && allow_home && gest.start_y >= ly.home_zone_y() &&
        dy <= -ly.swipe_min() && std::abs(dy) > std::abs(dx)) {
      gest.swipe_committed = true;
      gest.last_swipe_ms = now_rel;
      navigate_home(now_rel);
    } else if (!gest.swipe_committed && allow_home &&
               gest.max_travel <= ly.swipe_min() &&
               (wid == WidgetId::HOME_BAR || ly.is_home_pill_tap(tp))) {
      navigate_home(now_rel);

    // 2. Commit Horizontal Slider Releases (Area Card Slider / Light Inspector / Music Scrubber)
    } else if (!gest.swipe_committed && start_slider != nullptr && gest.max_travel >= ly.drag_start()) {
      if (start_slider->widget_id >= WidgetId::AREA_CARD_SLIDER &&
          start_slider->widget_id < WidgetId::AREA_CARD_SLIDER + 3 &&
          rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::OVERVIEW) {
        const int idx = std::clamp(static_cast<int>(start_slider->param), 0, 2);
        auto &sl = rs.area_entity_slots()[idx];
        if (!sl.id.empty()) {
          const int stepped = std::clamp(
              ((static_cast<int>(start_slider->norm_x(tp.x) * 100.0f) + 2) / 5) * 5, 5, 100);
          sl.level = stepped;
          sl.active = true;
          rs.area.target_entity_id = sl.id;
          char fb[48];
          std::snprintf(fb, sizeof(fb), "%s -> %d%%",
                        strip_trailing_paren_state(sl.name).c_str(), stepped);
          rs.area.action_feedback = fb;
          gate_or_dispatch_area_action(sl.id, "set_level", stepped, "PERIMETER PIN", now_rel);
        }
      } else if (start_slider->widget_id == WidgetId::MUSIC_SCRUB_BAR &&
                 rs.nav.gui_page == PageId::MUSIC && rs.music.overlay_mode == 6) {
        const float dur = media_state().duration;
        if (!std::isnan(dur) && dur > 0.0f) {
          rs.music.pos_base = start_slider->norm_x(tp.x) * dur;
          rs.music.pos_sync_time = now_rel;
          media_seek_absolute(now_rel);
          rt_.schedule_disp();
        }
      } else if (start_slider->widget_id == WidgetId::LIGHT_TEMP_SLIDER &&
                 rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
        rs.area.target_entity_id = light_insp_entity_id();
        rs.area.target_action = "set_kelvin";
        rs.area.target_level = light_insp_kelvin();
        rs.area.feedback_timer = now_rel;
        rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
        rt_.schedule_disp();
      } else if (start_slider->widget_id == WidgetId::LIGHT_BRI_SLIDER &&
                 rs.nav.gui_page == PageId::AREA && rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL) {
        rs.area.target_entity_id = light_insp_entity_id();
        rs.area.target_action = "set_level";
        rs.area.target_level = light_insp_bri_pct();
        rs.area.feedback_timer = now_rel;
        rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
        rt_.schedule_disp();
      }

    // 3. Light Inspector HSV Color Wheel Release -> Dispatch Color Action
    } else if (!gest.swipe_committed && rs.nav.gui_page == PageId::AREA &&
               rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL && light_insp_mode() == 0) {
      float hue = 0.0f, sat = 1.0f;
      if (ui::hsv_wheel_pick(tp.x, tp.y, SCREEN_CX, scale_y(176), 140, 62, hue, sat)) {
        light_insp_hue_deg() = hue;
        light_insp_sat() = sat;
        update_light_inspector_hex(hue, sat);
        rs.area.target_entity_id = light_insp_entity_id();
        rs.area.target_action = "set_color";
        rs.area.target_level = light_insp_bri_pct();
        rs.area.feedback_timer = now_rel;
        rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
        rt_.schedule_disp();
      }

    // 4. Page 1 Home Dock Tap / Release Swipe
    } else if (!gest.swipe_committed && rs.nav.gui_page == PageId::HOME && rs.nav.home_subpage == 0) {
      if (gest.start_y >= ly.home_dock_zone_top(home_weather_text_h(), home_activity_card_shown()) &&
          std::abs(dx) >= ly.dock_release_swipe() && std::abs(dx) > std::abs(dy)) {
        int dock_apps[kMaxDockApps];
        const int dock_cnt = collect_home_dock_apps(dock_apps);
        const int dock_pages = (dock_cnt + 3) / 4;
        if (dock_pages > 1) {
          home_dock_page() = std::clamp(home_dock_page() + ((dx < 0) ? 1 : -1), 0, dock_pages - 1);
          rt_.schedule_disp();
        }
      } else if (gest.max_travel <= ly.tap_max() &&
                 wid >= WidgetId::DOCK_SLOT_BASE && wid < WidgetId::DOCK_SLOT_BASE + 4) {
        if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON)) {
          launch_dock_app(hit->param, rs, rt_, now_rel);
        }
      }

    // 5. Page 2 Music Browse Lists (Modes 1, 2, 3, 7, 8)
    } else if (!gest.swipe_committed && rs.nav.gui_page == PageId::MUSIC &&
               (rs.music.overlay_mode == 1 || rs.music.overlay_mode == 2 ||
                rs.music.overlay_mode == 3 || rs.music.overlay_mode == 7 ||
                rs.music.overlay_mode == 8)) {
      int &b_page = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
      const int b_tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC);

      if (rs.music.overlay_mode != 2 &&
          std::abs(dy) >= ly.swipe_short() && std::abs(dy) > std::abs(dx) && b_tot > 1) {
        gest.swipe_committed = true;
        gest.last_swipe_ms = now_rel;
        music_scroll_tap_lockout_until_ms() = now_rel + 180U;
        const int step_d = (dy < 0) ? 1 : -1;
        const bool wrap_list = (rs.music.overlay_mode == 3) &&
                               (rs.music.selected_category_mode == "artist" ||
                                rs.music.selected_category_mode == "album");
        const int next_pg = wrap_list ? step_wrap_1based(b_page, step_d, b_tot)
                                      : std::clamp(b_page + step_d, 1, b_tot);
        if (next_pg != b_page) {
          b_page = next_pg;
          last_music_page_req_ms() = now_rel;
          rs.nav.menu_scroll_active = false;
          media_fetch_browse(now_rel);
          rt_.schedule_disp();
        }
      } else if (!rs.nav.menu_scroll_active && gest.max_travel <= ly.tap_max_loose()) {
        if (wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) {
          if (rs.music.overlay_mode == 2) {
            const int target_pg = (rs.music.category_page == 1) ? 2 : 1;
            rs.nav.menu_scroll_active = true;
            rs.nav.menu_scroll_dir = (target_pg == 2) ? 1 : -1;
            rs.nav.menu_scroll_target_page = target_pg;
            rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
          } else if (b_tot > 1) {
            const int step_d = (wid == WidgetId::CHEVRON_UP) ? -1 : 1;
            const bool wrap_list = (rs.music.overlay_mode == 3) &&
                                   (rs.music.selected_category_mode == "artist" ||
                                    rs.music.selected_category_mode == "album");
            const int next_pg = wrap_list ? step_wrap_1based(b_page, step_d, b_tot)
                                          : std::clamp(b_page + step_d, 1, b_tot);
            if (next_pg != b_page) {
              b_page = next_pg;
              media_fetch_browse(now_rel);
              rt_.schedule_disp();
            }
          }
        } else if (now_rel >= music_scroll_tap_lockout_until_ms()) {
          if (rs.music.overlay_mode == 1) {
            int slot_idx = -1;
            if (wid >= WidgetId::MUSIC_SPK_GROUP && wid < WidgetId::MUSIC_SPK_GROUP + 5) {
              slot_idx = hit->param;
            } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 5) {
              slot_idx = hit->param;
            }
            if (slot_idx >= 0 && slot_idx < 5) {
              const auto &sl = rs.music_slots()[slot_idx];
              if (!sl.name.empty() && !sl.id.empty()) {
                rs.music.target_speaker_id = sl.id;
                rs.music.target_speaker_name = sl.name;
                rs.music.overlay_mode = 4;
                mark_dirty_full();
                rt_.schedule_disp();
              }
            }
          } else if (rs.music.overlay_mode == 2 &&
                     wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 5) {
            const int idx = hit->param;
            if (idx == 0) { rs.music.selected_category_mode = "favorites"; rs.music.selected_category_title = "FAVORITES"; }
            else if (idx == 1) { rs.music.selected_category_mode = "playlist"; rs.music.selected_category_title = "PLAYLISTS"; }
            else if (idx == 2) { rs.music.selected_category_mode = "artist"; rs.music.selected_category_title = "ARTISTS"; rs.music.drilldown_artist_page = 1; }
            else if (idx == 3) { rs.music.selected_category_mode = "album"; rs.music.selected_category_title = "ALBUMS"; rs.music.drilldown_album_page = 1; }
            else if (idx == 4) { rs.music.selected_category_mode = "podcast"; rs.music.selected_category_title = "PODCASTS"; }
            else if (idx == 5) { rs.music.selected_category_mode = "radio"; rs.music.selected_category_title = "RADIO"; }
            else if (idx == 6) { rs.music.selected_category_mode = "audiobook"; rs.music.selected_category_title = "AUDIOBOOKS"; }
            reset_music_loading_slots(rs, "Loading...");
            b_page = 1;
            rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC) = 1;
            rs.music.overlay_mode = 3;
            media_fetch_browse(now_rel);
            mark_dirty_full();
            rt_.schedule_disp();
          } else if (rs.music.overlay_mode == 3) {
            if (wid == WidgetId::MUSIC_PLAY_PAUSE) {
              const int slot_idx = std::clamp(static_cast<int>(hit->param), 0, 4);
              if (!rs.music_slots()[slot_idx].id.empty()) {
                rt_.cmd(UiHostCmd::PLAY_MUSIC_SLOT, slot_idx + 1);
              }
            } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 5) {
              const int slot_idx = std::clamp(static_cast<int>(hit->param), 0, 4);
              const std::string s_id = rs.music_slots()[slot_idx].id;
              const std::string s_name = rs.music_slots()[slot_idx].name;
              if (!s_id.empty()) {
                const bool is_podcast_list = (rs.music.selected_category_mode == "podcast");
                const bool is_drill = (rs.music.selected_category_mode == "artist" ||
                                       rs.music.selected_category_mode == "album" ||
                                       rs.music.selected_category_mode == "artist_albums");
                if (is_podcast_list) {
                  rs.music.drilldown_album_id = s_id;
                  rs.music.drilldown_album_page = b_page;
                  rs.music.drilldown_parent_mode = "podcast";
                  rs.music.selected_category_mode = "podcast_episodes";
                  rs.music.selected_category_title = s_name;
                  reset_music_loading_slots(rs, "Loading episodes...");
                  b_page = 1;
                  media_fetch_browse(now_rel);
                  rt_.schedule_disp();
                } else if (is_drill) {
                  if (rs.music.selected_category_mode == "artist") {
                    rs.music.drilldown_artist_name = s_name;
                    rs.music.drilldown_artist_page = b_page;
                    rs.music.selected_category_mode = "artist_albums";
                    rs.music.selected_category_title = s_name;
                    b_page = 1;
                    media_fetch_browse(now_rel);
                    rt_.schedule_disp();
                  } else {
                    rs.music.drilldown_album_id = s_id;
                    rs.music.drilldown_album_page = b_page;
                    rs.music.drilldown_parent_mode = rs.music.selected_category_mode;
                    rs.music.selected_category_mode = "album_tracks";
                    rs.music.selected_category_title = s_name;
                    b_page = 1;
                    media_fetch_browse(now_rel);
                    rt_.schedule_disp();
                  }
                } else {
                  rt_.cmd(UiHostCmd::PLAY_MUSIC_SLOT, slot_idx + 1);
                }
              }
            }
          }
        }
      }

    // 6. Page 3 Games (2048 Swipes & Launcher Play/Sound/Carousel Taps)
    } else if (!gest.swipe_committed && rs.nav.gui_page == PageId::GAMES && rs.nav.active_game == 5) {
      if (std::abs(dx) >= ly.flick_min() || std::abs(dy) >= ly.flick_min()) {
        if (std::abs(dx) > std::abs(dy)) {
          game_2048_pending_dir() = (dx > 0) ? 2 : 4;
        } else {
          game_2048_pending_dir() = (dy > 0) ? 3 : 1;
        }
      } else {
        rs.nav.marble_touch_x = static_cast<float>(tp.x);
        rs.nav.marble_touch_y = static_cast<float>(tp.y);
        rs.nav.game_btn_pressed = true;
      }
      rt_.schedule_disp();
    } else if (!gest.swipe_committed && rs.nav.gui_page == PageId::GAMES &&
               rs.nav.active_game == 0 && !rs.nav.menu_scroll_active &&
               gest.max_travel <= ly.tap_max()) {
      if (wid == WidgetId::GAMES_SOUND_BTN) {
        rt_.sw_toggle(UiSwitchId::GAME_SOUND);
        rt_.schedule_disp();
      } else if (wid == WidgetId::GAMES_PLAY_BTN || wid == WidgetId::GAMES_HERO_CARD) {
        rs.nav.active_game = rs.nav.game_carousel_idx + 1;
        audio_arbitrator().cancel_media_idle_debounce();
        if (rt_.local_mp_playing) {
          rt_.cmd(UiHostCmd::PAUSE_LOCAL_MEDIA_PLAYER);
          audio_arbitrator().cancel_media_idle_debounce();
        }
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (wid == WidgetId::GAMES_PREV_BTN || wid == WidgetId::GAMES_NEXT_BTN) {
        const int dir = (wid == WidgetId::GAMES_NEXT_BTN) ? 1 : -1;
        rs.nav.menu_scroll_active = true;
        rs.nav.menu_scroll_dir = dir;
        rs.nav.menu_scroll_target_page = step_wrap(rs.nav.game_carousel_idx, dir, 6);
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      }

    // 7. Page 4 System Scrollable Menus (Modes 0, 1, 2) via SYS_TOGGLE_ROW / SYS_BRI_SLIDER / COLOR_SWATCH_BASE
    } else if (!gest.swipe_committed && rs.nav.gui_page == PageId::SYSTEM &&
               !rs.nav.menu_scroll_active &&
               (rs.nav.system_menu_mode == 0 || rs.nav.system_menu_mode == 1 ||
                rs.nav.system_menu_mode == 2) &&
               gest.max_travel <= ly.tap_max()) {
      const int cur_pg = (rs.nav.system_menu_mode == 0)
                             ? rs.nav.system_dashboard_page
                             : ((rs.nav.system_menu_mode == 1) ? rs.nav.sleep_menu_page
                                                               : rs.nav.display_menu_page);
      if (wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) {
        const int target_pg = (cur_pg == 1) ? 2 : 1;
        rs.nav.menu_scroll_active = true;
        rs.nav.menu_scroll_dir = (target_pg == 2) ? 1 : -1;
        rs.nav.menu_scroll_target_page = target_pg;
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
      } else if (rs.nav.system_menu_mode == 0 &&
                 wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
        const int slot = hit->param;
        const int item_idx = (rs.nav.system_dashboard_page == 1) ? slot : (slot + 3);
        if (item_idx == 0) { rs.nav.system_menu_mode = 2; rs.nav.display_menu_page = 1; }
        else if (item_idx == 1) { rs.nav.system_menu_mode = 1; rs.nav.sleep_menu_page = 1; }
        else if (item_idx == 2) { rs.nav.system_menu_mode = 7; }
        else if (item_idx == 3) { rs.nav.system_menu_mode = 11; }
        else if (item_idx == 4) { rs.nav.system_menu_mode = 9; rs.nav.audio_menu_parent_page = PageId::SYSTEM; }
        else if (item_idx == 5) {
          if (prefs.home_screen_enabled) {
            rs.nav.gui_page = PageId::HOME;
            rs.nav.home_subpage = 1;
          }
        }
        else if (item_idx == 6) { rs.nav.system_menu_mode = 3; }
        else if (item_idx == 7) { rs.nav.system_menu_mode = 8; }
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (rs.nav.system_menu_mode == 1 &&
                 wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
        const int slot_idx = hit->param;
        const int idx = (rs.nav.sleep_menu_page == 1) ? slot_idx : (slot_idx + 5);
        if (idx == 0) {
          rt_.set_select_opt(UiSelectId::POWER_PROFILE,
                             cycle_option(rt_.select_opt(UiSelectId::POWER_PROFILE),
                                          {"Portable Carry", "Handheld Roaming", "Wearable Watch",
                                           "Tabletop Dock", "Always-On Wall / Desk"}));
        } else if (idx == 1) {
#if LATTER_HAS_IMU
          rt_.set_select_opt(UiSelectId::PICKUP_WAKE_MODE,
                             cycle_option(rt_.select_opt(UiSelectId::PICKUP_WAKE_MODE),
                                          {"Always On", "Docked Only", "Disabled"}));
#endif
        } else if (idx == 2) {
          rt_.set_select_opt(UiSelectId::TAP_WAKE_MODE,
                             cycle_option(rt_.select_opt(UiSelectId::TAP_WAKE_MODE),
                                          {"Always On", "Docked Only", "Disabled"}));
        } else if (idx == 3) {
#if LATTER_HAS_BATTERY
          rt_.set_select_opt(UiSelectId::DISPLAY_OFF_TIMEOUT,
                             cycle_option(rt_.select_opt(UiSelectId::DISPLAY_OFF_TIMEOUT),
                                          {"10s", "15s", "30s", "1 min", "2 min", "Never"}));
#endif
        } else if (idx == 4) {
          rt_.sw_toggle(UiSwitchId::DOCKED_SCREENSAVER);
        } else if (idx == 5) {
          if (rt_.sw_state(UiSwitchId::DOCKED_SCREENSAVER)) {
            rt_.set_select_opt(UiSelectId::SCREENSAVER_STYLE,
                               cycle_option(rt_.select_opt(UiSelectId::SCREENSAVER_STYLE),
                                            {"Ambient Clock", "Aurora", "Starfield",
                                             "Digital Rain", "Fireworks"}));
          }
        } else if (idx == 6) {
          rt_.set_select_opt(UiSelectId::SCREENSAVER_DOCKED_TIMEOUT,
                             cycle_option(rt_.select_opt(UiSelectId::SCREENSAVER_DOCKED_TIMEOUT),
                                          {"10s", "15s", "30s", "1 min", "2 min", "5 min", "Never"}));
        } else if (idx == 7) {
#if LATTER_HAS_BATTERY
          rt_.set_select_opt(UiSelectId::HIBERNATE_TIMEOUT_MODE,
                             cycle_option(rt_.select_opt(UiSelectId::HIBERNATE_TIMEOUT_MODE),
                                          {"5 min", "10 min", "15 min", "30 min", "Disabled"}));
#endif
        } else if (idx == 8) {
#if LATTER_HAS_BATTERY
          rt_.sw_toggle(UiSwitchId::INSTANT_CROWN_HIBERNATE);
#endif
        }
        rt_.schedule_disp();
      } else if (rs.nav.system_menu_mode == 2) {
        if (wid >= WidgetId::COLOR_SWATCH_BASE && wid < WidgetId::COLOR_SWATCH_BASE + 10) {
          rs.nav.system_menu_mode = 13;
          mark_dirty_full();
          rt_.schedule_disp();
        } else if (wid == WidgetId::SYS_BRI_SLIDER) {
          const std::string_view ab = rt_.select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE);
          if (ab == "Disabled") {
            const char *next_b = cycle_option(
                rt_.select_opt(UiSelectId::DISPLAY_BRIGHTNESS_PRESET),
                {"60%", "70%", "80%", "90%", "100%"});
            rt_.set_select_opt(UiSelectId::DISPLAY_BRIGHTNESS_PRESET, next_b, true);
            rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
            rt_.schedule_disp();
          }
        } else if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
          const int slot_idx = hit->param;
          const int item_idx = (rs.nav.display_menu_page == 1) ? slot_idx : (slot_idx + 4);
          const std::string_view ab = rt_.select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE);
          if (item_idx == 0) {
            if (prefs.home_screen_enabled) {
              rt_.set_select_opt(UiSelectId::DEFAULT_START_APP,
                                 cycle_option(rt_.select_opt(UiSelectId::DEFAULT_START_APP),
                                              {"Home", "Clock", "Media", "Home Hub", "Area Controls",
                                               "Remote", "Weather", "Cameras", "Games", "Tools"}));
            } else {
              rt_.set_select_opt(UiSelectId::DEFAULT_START_APP,
                                 cycle_option(rt_.select_opt(UiSelectId::DEFAULT_START_APP),
                                              {"Clock", "Media", "Home Hub", "Area Controls",
                                               "Remote", "Weather", "Cameras", "Games", "Tools"}));
            }
          } else if (item_idx == 1) {
#if LATTER_ROUND_DISPLAY
            const std::string next_cf = cycle_option(
                rt_.select_opt(UiSelectId::CLOCK_FACE_STYLE),
                {"Minimal Digital", "Modern Roman (Analog)", "Modern Digital (Bold)"});
#else
            const std::string next_cf = cycle_option(
                rt_.select_opt(UiSelectId::CLOCK_FACE_STYLE),
                {"Minimal Digital", "Modern Digital (Bold)"});
#endif
            rt_.set_select_opt(UiSelectId::CLOCK_FACE_STYLE, next_cf);
          } else if (item_idx == 2) {
            rt_.set_select_opt(UiSelectId::TIME_FORMAT,
                               prefs.is_24h_format ? "12-Hour (AM/PM)" : "24-Hour");
          } else if (item_idx == 3) {
            const std::string next_pal = cycle_option(
                rt_.select_opt(UiSelectId::THEME_PALETTE),
                {"Steel Blue", "Pink", "Blue", "Green", "Custom"});
            rt_.set_select_opt(UiSelectId::THEME_PALETTE, next_pal);
            if (next_pal == "Custom") {
              rs.nav.system_menu_mode = 13;
              mark_dirty_full();
            }
          } else if (item_idx == 4) {
            const std::string next_m =
                cycle_option(ab, {"Disabled", "Sun Synchronized", "Time Schedule"});
            rt_.set_select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE, next_m, true);
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
            const std::string next_db = cycle_option(
                rt_.select_opt(UiSelectId::DAY_BRIGHTNESS_PRESET),
                {"60%", "70%", "80%", "90%", "100%"});
            rt_.set_select_opt(UiSelectId::DAY_BRIGHTNESS_PRESET, next_db, true);
            rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
          } else if (item_idx == 8 && ab != "Disabled") {
            const std::string next_nb = cycle_option(
                rt_.select_opt(UiSelectId::NIGHT_BRIGHTNESS_PRESET),
                {"60%", "70%", "80%", "90%", "100%"});
            rt_.set_select_opt(UiSelectId::NIGHT_BRIGHTNESS_PRESET, next_nb, true);
            rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
          }
          rt_.schedule_disp();
        }
      }

    // 7b. Page 4 System Confirm Modals (Mode 4 Reboot / Mode 6 Firmware Update — release-triggered)
    } else if (!gest.swipe_committed && rs.nav.gui_page == PageId::SYSTEM &&
               !rs.nav.menu_scroll_active &&
               (rs.nav.system_menu_mode == 4 || rs.nav.system_menu_mode == 6) &&
               gest.max_travel <= ly.tap_max()) {
      if (wid == WidgetId::CONFIRM_CANCEL && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON)) {
        rs.nav.system_menu_mode = (rs.nav.system_menu_mode == 4) ? 3 : 5;
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (wid == WidgetId::CONFIRM_ACCEPT && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON)) {
        rt_.cmd((rs.nav.system_menu_mode == 4) ? UiHostCmd::RESTART_DEVICE
                                               : UiHostCmd::TRIGGER_FIRMWARE_UPDATE);
      }

    // 8. Page 5 Weather Mode Bar Taps
    } else if (!gest.swipe_committed && rs.nav.gui_page == PageId::WEATHER &&
               gest.max_travel <= ly.tap_max()) {
      if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4) {
        const int target_sub = std::clamp(static_cast<int>(hit->param), 0, 3);
        if (target_sub != rs.nav.sky_weather_subpage) {
          rs.nav.sky_weather_subpage = target_sub;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (wid == WidgetId::WEATHER_MODE_BTN) {
        rs.nav.sky_weather_subpage = (rs.nav.sky_weather_subpage + 1) % 4;
        mark_dirty_full();
        rt_.schedule_disp();
      }

    // 9. Page 6 Area Control Taps (Picker List & 3-Card Domain Controls)
    } else if (!gest.swipe_committed && rs.nav.gui_page == PageId::AREA) {
      if (rs.nav.area_subpage == AreaSubpage::PICKER || rs.area.browse_mode == "areas") {
        int &a_pg = rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST);
        const int a_tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::AREA_LIST);
        if (std::abs(dy) >= ly.list_swipe() && std::abs(dy) > std::abs(dx) && a_tot > 1) {
          const int next_pg = std::clamp(a_pg + ((dy < 0) ? 1 : -1), 1, a_tot);
          if (next_pg != a_pg) {
            a_pg = next_pg;
            rt_.cmd(UiHostCmd::FETCH_AREA_LIST_BROWSE);
            rt_.schedule_disp();
          }
        } else if (gest.max_travel <= ly.tap_max()) {
          if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) && a_tot > 1) {
            const int next_pg =
                std::clamp(a_pg + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, a_tot);
            if (next_pg != a_pg) {
              a_pg = next_pg;
              rt_.cmd(UiHostCmd::FETCH_AREA_LIST_BROWSE);
              rt_.schedule_disp();
            }
          } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + 5) {
            const int s_idx = std::clamp(static_cast<int>(hit->param), 0, 4);
            const auto &asl = rs.area_list_slots()[s_idx];
            if (!asl.id.empty()) {
              rs.area.active_area_id = asl.id;
              rs.area.active_area_name = strip_trailing_paren_state(asl.name);
              rs.nav.area_subpage = AreaSubpage::OVERVIEW;
              rs.area.browse_mode = "entities";
              rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
              rt_.cmd(UiHostCmd::SELECT_ACTIVE_AREA_LATTER);
              rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
              mark_dirty_full();
              rt_.schedule_disp();
            }
          }
        }
      } else if (rs.nav.area_subpage == AreaSubpage::OVERVIEW && gest.max_travel <= ly.tap_max()) {
        std::string cats[8];
        const int cat_cnt = collect_area_categories(cats);
        if (wid == WidgetId::MODE_BAR_LEFT || wid == WidgetId::MODE_BAR_RIGHT) {
          if (cat_cnt > 4) {
            area_cat_dock_page() = std::clamp(
                area_cat_dock_page() + ((wid == WidgetId::MODE_BAR_RIGHT) ? 4 : -4), 0, cat_cnt - 4);
            rt_.schedule_disp();
          }
        } else if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + 4) {
          if (cat_cnt > 0) {
            const int c_idx = std::clamp(static_cast<int>(hit->param), 0, cat_cnt - 1);
            if (cats[c_idx] != rs.area.domain_filter) {
              rs.area.domain_filter = cats[c_idx];
              rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
              rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
              rt_.schedule_disp();
            }
          }
        } else if (wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) {
          int &pg = rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES);
          const int tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::AREA_ENTITIES);
          const int next_pg = std::clamp(pg + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, std::max(1, tot));
          if (next_pg != pg) {
            pg = next_pg;
            rt_.cmd(UiHostCmd::FETCH_AREA_BROWSE);
            rt_.schedule_disp();
          }
        } else if (wid == WidgetId::LIGHT_ORB_BTN) {
          const int idx = std::clamp(static_cast<int>(hit->param), 0, 2);
          const auto &sl = rs.area_entity_slots()[idx];
          if (!sl.id.empty()) {
            light_insp_entity_id() = sl.id;
            light_insp_name() = strip_trailing_paren_state(sl.name);
            if (sl.level > 0) light_insp_bri_pct() = sl.level;
            rs.nav.area_subpage = AreaSubpage::LIGHT_DETAIL;
            mark_dirty_full();
            rt_.schedule_disp();
          }
        } else if ((wid >= WidgetId::AREA_CARD_TOGGLE && wid < WidgetId::AREA_CARD_TOGGLE + 3) ||
                   (wid >= WidgetId::AREA_CARD_TAP && wid < WidgetId::AREA_CARD_TAP + 3) ||
                   (wid >= WidgetId::AREA_CARD_SLIDER && wid < WidgetId::AREA_CARD_SLIDER + 3)) {
          const int idx = std::clamp(static_cast<int>(hit->param), 0, 2);
          auto &sl = rs.area_entity_slots()[idx];
          if (!sl.id.empty() && sl.type != "text") {
            const bool is_slider_zone =
                (wid >= WidgetId::AREA_CARD_SLIDER && wid < WidgetId::AREA_CARD_SLIDER + 3);
            rs.area.target_entity_id = sl.id;
            if (is_slider_zone && sl.type != "scene" && sl.type != "switch") {
              const int stepped = std::clamp(
                  ((static_cast<int>(hit->norm_x(tp.x) * 100.0f) + 2) / 5) * 5, 5, 100);
              sl.level = stepped;
              sl.active = true;
              char fb[48];
              std::snprintf(fb, sizeof(fb), "%s -> %d%%",
                            strip_trailing_paren_state(sl.name).c_str(), stepped);
              rs.area.action_feedback = fb;
              gate_or_dispatch_area_action(sl.id, "set_level", stepped, "PERIMETER PIN", now_rel);
            } else {
              const bool prev_act = sl.active;
              const bool next_act = (sl.type == "scene") ? true : !sl.active;
              sl.active = next_act;
              std::string verb = "toggle";
              if (sl.type == "scene") verb = "turn_on";
              else if (sl.type == "cover") verb = next_act ? "open_cover" : "close_cover";
              else if (sl.type == "lock" || sl.id.rfind("lock.", 0) == 0) verb = next_act ? "unlock" : "lock";
              else verb = next_act ? "turn_on" : "turn_off";
              rs.area.action_feedback = strip_trailing_paren_state(sl.name);
              if (!gate_or_dispatch_area_action(sl.id, verb, next_act ? 100 : 0,
                                                "PERIMETER PIN", now_rel)) {
                sl.active = prev_act;
              }
            }
          }
        }
      }

    // 10. Page 7 Remote Subpage 1 (Slot Selector & Power Actions)
    } else if (!gest.swipe_committed && rs.nav.gui_page == PageId::REMOTE &&
               rs.nav.remote_subpage == 1) {
      int valid_slots[MAX_REMOTE_SLOTS] = {0};
      const int valid_count = collect_configured_remote_slots(valid_slots);
      const int total_pages = std::max(1, (valid_count + 3) / 4);
      if (std::abs(dy) >= ly.list_swipe() && std::abs(dy) > std::abs(dx) && total_pages > 1) {
        remote_control_page() = std::clamp(
            remote_control_page() + ((dy < 0) ? 1 : -1), 0, total_pages - 1);
        rt_.schedule_disp();
      } else if (gest.max_travel <= ly.tap_max()) {
        if (wid == WidgetId::REMOTE_PRIMARY_PWR) {
          rt_.cmd(UiHostCmd::REMOTE_SEND_KEY, 9, -1);
          rt_.schedule_disp();
        } else if (wid >= WidgetId::REMOTE_SEC_PWR && wid < WidgetId::REMOTE_SEC_PWR + 4) {
          rt_.cmd(UiHostCmd::REMOTE_SEND_KEY, 9, hit->param);
          rt_.schedule_disp();
        } else if (wid >= WidgetId::REMOTE_SLOT_CARD && wid < WidgetId::REMOTE_SLOT_CARD + 4) {
          rs.remote.active_slot = hit->param;
          rs.remote.action_feedback.clear();
          rs.nav.remote_subpage = 0;
          rt_.cmd(UiHostCmd::REMOTE_SELECT_SLOT_LATTER, hit->param + 1);
          mark_dirty_full();
          rt_.schedule_disp();
        }
      }

    // 11. Page 8 Tools Launcher, Calendar & Cameras Release Taps/Swipes
    } else if (!gest.swipe_committed && rs.nav.gui_page == PageId::TOOLS) {
      if (rs.nav.tools_subpage == ToolsSubpage::LAUNCHER && !rs.nav.menu_scroll_active &&
          gest.max_travel <= ly.tap_max()) {
        if (wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) {
          const int target_pg = (rs.nav.tools_menu_page == 0) ? 1 : 0;
          rs.nav.menu_scroll_active = true;
          rs.nav.menu_scroll_dir = (target_pg == 1) ? 1 : -1;
          rs.nav.menu_scroll_target_page = target_pg;
          rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
        } else if (wid >= WidgetId::TOOLS_CARD_BASE && wid < WidgetId::TOOLS_CARD_BASE + 6) {
          const int target_sub = hit->param;
          if (target_sub > 0) {
            rs.nav.tools_subpage = target_sub;
            if (target_sub == ToolsSubpage::FLASHLIGHT) {
              rt_.cmd(UiHostCmd::FLASHLIGHT_FULL_BRIGHTNESS);
            } else if (target_sub == ToolsSubpage::CALCULATOR) {
              reset_calculator_state();
            } else if (target_sub == ToolsSubpage::CALENDAR) {
              cal_detail_mode() = 0;
              cal_quick_add_open() = false;
              rs.slot_page_for(latter_rpc::SlotChannel::CALENDAR) = 1;
              rt_.cmd(UiHostCmd::FETCH_CALENDAR_BROWSE);
            } else if (target_sub == ToolsSubpage::CAMERAS) {
              cam_quick_reply_open() = false;
              rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
            }
            rt_.apply_power_policy();
            mark_dirty_full();
            rt_.schedule_disp();
          }
        }
      } else if (rs.nav.tools_subpage == ToolsSubpage::CALENDAR) {
        if (cal_detail_mode() == 2) {
          if (std::abs(dx) >= ly.page_swipe() || std::abs(dy) >= ly.page_swipe()) {
            gest.swipe_committed = true;
            gest.last_swipe_ms = now_rel;
            if (std::abs(dx) >= std::abs(dy)) {
              const int step_x = (std::abs(dx) >= ly.fast_pan()) ? 2 : 1;
              cal_map_pan_x() = std::clamp(cal_map_pan_x() + ((dx < 0) ? step_x : -step_x), -4, 4);
            } else {
              const int step_y = (std::abs(dy) >= ly.fast_pan()) ? 2 : 1;
              cal_map_pan_y() = std::clamp(cal_map_pan_y() + ((dy < 0) ? step_y : -step_y), -4, 4);
            }
            rt_.cmd(UiHostCmd::FETCH_CALENDAR_MAP_SNAPSHOT);
          } else if (gest.max_travel <= ly.tap_max()) {
            if (wid == WidgetId::CAL_ZOOM_OUT_BTN && cal_map_zoom() > 10) {
              cal_map_zoom()--;
              cal_map_pan_x() /= 2;
              cal_map_pan_y() /= 2;
              rt_.schedule_disp();
              rt_.cmd(UiHostCmd::FETCH_CALENDAR_MAP_SNAPSHOT);
            } else if (wid == WidgetId::CAL_ZOOM_IN_BTN && cal_map_zoom() < 18) {
              cal_map_zoom()++;
              cal_map_pan_x() = std::clamp(cal_map_pan_x() * 2, -4, 4);
              cal_map_pan_y() = std::clamp(cal_map_pan_y() * 2, -4, 4);
              rt_.schedule_disp();
              rt_.cmd(UiHostCmd::FETCH_CALENDAR_MAP_SNAPSHOT);
            } else if (tp.y <= scale_y(68) && (cal_map_pan_x() != 0 || cal_map_pan_y() != 0)) {
              cal_map_pan_x() = 0;
              cal_map_pan_y() = 0;
              rt_.cmd(UiHostCmd::FETCH_CALENDAR_MAP_SNAPSHOT);
            }
          }
        } else if (cal_detail_mode() == 0 && !cal_quick_add_open()) {
          int &c_pg = rs.slot_page_for(latter_rpc::SlotChannel::CALENDAR);
          const int c_tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::CALENDAR);
          if (std::abs(dy) >= ly.list_swipe() && std::abs(dy) > std::abs(dx) && c_tot > 1) {
            const int next_pg = std::clamp(c_pg + ((dy < 0) ? 1 : -1), 1, c_tot);
            if (next_pg != c_pg) {
              c_pg = next_pg;
              rt_.cmd(UiHostCmd::FETCH_CALENDAR_BROWSE);
              rt_.schedule_disp();
            }
          } else if (gest.max_travel <= ly.tap_max() &&
                     wid >= WidgetId::CAL_EVENT_BASE && wid < WidgetId::CAL_EVENT_BASE + 5) {
            const int slot_idx = std::clamp(static_cast<int>(wid - WidgetId::CAL_EVENT_BASE), 0, 4);
            const auto &csl = rs.calendar_slots()[slot_idx];
            if (!csl.name.empty()) {
              parse_calendar_slot_meta(csl.name, csl.id, cal_detail_time(),
                                       cal_detail_title(), cal_detail_location(),
                                       cal_detail_map_url());
              cal_detail_mode() = 1;
              mark_dirty_full();
              rt_.schedule_disp();
            } else {
              CalendarRecord recs[5];
              const size_t n_rec = parse_calendar_records(calendar_state().summary, recs, 5);
              if (static_cast<size_t>(slot_idx) < n_rec) {
                cal_detail_time() = std::string(recs[slot_idx].time);
                cal_detail_title() = std::string(recs[slot_idx].title);
                cal_detail_location() = std::string(recs[slot_idx].location);
                cal_detail_map_url().clear();
                cal_detail_mode() = 1;
                mark_dirty_full();
                rt_.schedule_disp();
              }
            }
          }
        }
      } else if (rs.nav.tools_subpage == ToolsSubpage::CAMERAS) {
        std::string c_ids[MAX_CAMERA_SLOTS], c_names[MAX_CAMERA_SLOTS];
        const int c_total = parse_cameras_summary_list(area_ctx_state().cameras_summary, c_ids, c_names);
        if (!cam_quick_reply_open() && c_total > 1 &&
            std::abs(dx) >= ly.page_swipe() && std::abs(dx) > std::abs(dy)) {
          cam_carousel_idx() = step_wrap(cam_carousel_idx(), (dx < 0) ? 1 : -1, c_total);
          rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
          rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
          rt_.schedule_disp();
        } else if (gest.max_travel <= ly.tap_max()) {
          if (cam_quick_reply_open()) {
            if (wid == WidgetId::CAM_REPLY_BASE + 0) {
              cam_quick_reply_open() = false;
              gate_or_dispatch_doorbell_unlock(now_rel, false);
            } else if (wid >= WidgetId::CAM_REPLY_BASE + 1 && wid <= WidgetId::CAM_REPLY_BASE + 3) {
              static constexpr const char *kDoorbellReplies[3] = {
                  "Please leave the package at the door, thank you!",
                  "We'll be right there!",
                  "No soliciting, thank you!",
              };
              const int r_idx = static_cast<int>(wid - (WidgetId::CAM_REPLY_BASE + 1));
              cam_quick_reply_open() = false;
              rs.intercom.cam_action = "quick_reply";
              rs.intercom.cam_message = kDoorbellReplies[r_idx];
              cam_intercom_status() = "REPLY SENT";
              rs.intercom.cam_status_ms = now_rel;
              intercom_session().emit_current_cmd();
              mark_dirty_full();
              rt_.schedule_disp();
            } else if (wid == WidgetId::CAM_NOISE_BTN) {
              cam_noise_idx() = (cam_noise_idx() + 1) % 3;
              static constexpr const char *kNoiseCodes[3] = {"strong", "light", "off"};
              rs.intercom.cam_noise_reduction = kNoiseCodes[cam_noise_idx()];
              rs.intercom.cam_action = "set_noise_reduction";
              intercom_session().emit_current_cmd();
              rt_.schedule_disp();
            }
          } else if (wid == WidgetId::CAM_ORIENT_BTN) {
            cam_landscape_mode() = !cam_landscape_mode();
            cam_intercom_status() = cam_landscape_mode() ? "90° LANDSCAPE" : "PORTRAIT VIEW";
            rs.intercom.cam_status_ms = now_rel;
            rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
            rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
            mark_dirty_full();
            rt_.schedule_disp();
          } else if ((wid == WidgetId::CAM_PREV_BTN || wid == WidgetId::CAM_NEXT_BTN) && c_total > 1) {
            cam_carousel_idx() = step_wrap(
                cam_carousel_idx(), (wid == WidgetId::CAM_NEXT_BTN) ? 1 : -1, c_total);
            rt_.cmd(UiHostCmd::RELEASE_CAMERA_ART);
            rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
            rt_.schedule_disp();
          } else if (wid == WidgetId::CAM_LISTEN_BTN) {
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
              rt_.schedule_disp();
            }
          } else if (wid == WidgetId::CAM_PTT_BTN) {
            if (!cam_call_active()) {
              intercom_session().start_call(now_rel);
              rt_.schedule_disp();
            } else if (cam_intercom_ptt_active()) {
              rt_.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
              intercom_session().resume_listen(now_rel);
              rt_.apply_power_policy();
              rt_.schedule_disp();
            } else {
              rt_.cmd(UiHostCmd::START_DOORBELL_PTT);
            }
          } else if (wid == WidgetId::CAM_REPLY_MENU_BTN) {
            cam_quick_reply_open() = true;
            mark_dirty_full();
            rt_.schedule_disp();
          } else if (tp.y < scale_y(340)) {
            rt_.cmd(UiHostCmd::FETCH_ON_DEMAND_CAMERA);
            rt_.schedule_disp();
          }
        }
      }

    // 12. Page 9 Whole-Home Hub Controls (Security, Climate, Perimeter, Vacuum)
    } else if (!gest.swipe_committed && rs.nav.gui_page == PageId::WHOLE_HOME &&
               gest.max_travel <= ly.tap_max()) {
      int wh_tabs[4] = {0, 0, 0, 0};
      const int wh_tab_cnt = collect_home_hub_tabs(wh_tabs);
      if (wh_tab_cnt <= 0) {
        // No Home Controls configured: ignore taps on empty state.
      } else if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + wh_tab_cnt) {
        const int slot = std::clamp(static_cast<int>(hit->param), 0, wh_tab_cnt - 1);
        const int target_tab = wh_tabs[slot];
        if (target_tab != whole_home_tab()) {
          whole_home_tab() = target_tab;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (whole_home_tab() == 0 &&
                 wid >= WidgetId::ALARM_MODE_BASE && wid < WidgetId::ALARM_MODE_BASE + 3) {
        const int mode_i = static_cast<int>(wid - WidgetId::ALARM_MODE_BASE);
        static constexpr const char *kAlarmSvcs[3] = {
            "alarm_control_panel.alarm_disarm",
            "alarm_control_panel.alarm_arm_home",
            "alarm_control_panel.alarm_arm_away",
        };
        static constexpr const char *kAlarmFb[3] = {
            "Disarming System...", "Arming Home...", "Arming Away..."};
        rs.area.action_feedback = kAlarmFb[mode_i];
        gate_or_dispatch_alarm_action(kAlarmSvcs[mode_i], now_rel);
      } else if (whole_home_tab() == 1) {
        if (wid == WidgetId::CLIMATE_MINUS_BTN || wid == WidgetId::CLIMATE_PLUS_BTN) {
          int &tf = sim_thermostat_target_f();
          if (tf < 40 || tf > 100) tf = 70;
          tf = std::clamp(tf + ((wid == WidgetId::CLIMATE_PLUS_BTN) ? 1 : -1), 50, 90);
          rs.area.target_entity_id = "whole_home.thermostat";
          rs.area.target_action = "set_temperature";
          rs.area.target_level = tf;
          char fb[36];
          std::snprintf(fb, sizeof(fb), "Setpoint -> %dF", tf);
          rs.area.action_feedback = fb;
          rs.area.feedback_timer = now_rel;
          rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
          rt_.schedule_disp();
        } else if (wid >= WidgetId::CLIMATE_MODE_BASE && wid < WidgetId::CLIMATE_MODE_BASE + 4) {
          static constexpr const char *kClimModes[4] = {"heat", "cool", "heat_cool", "off"};
          static constexpr const char *kClimLabels[4] = {"Heat", "Cool", "Auto", "Off"};
          const int m_i = static_cast<int>(wid - WidgetId::CLIMATE_MODE_BASE);
          sim_thermostat_mode() = kClimModes[m_i];
          rs.area.target_entity_id = "whole_home.thermostat";
          rs.area.target_action = std::string("hvac_") + kClimModes[m_i];
          rs.area.target_level = sim_thermostat_target_f();
          rs.area.action_feedback = std::string("HVAC: ") + kClimLabels[m_i];
          rs.area.feedback_timer = now_rel;
          rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
          rt_.schedule_disp();
        }
      } else if (whole_home_tab() == 2) {
        if (wid == WidgetId::COVER_OPEN_BTN) {
          const GarageState prev_g = wh_garage_state();
          const bool want_open = (prev_g != GarageState::OPEN);
          wh_garage_state() = want_open ? GarageState::OPEN : GarageState::CLOSED;
          rs.area.target_entity_id = "whole_home.garage";
          rs.area.action_feedback = want_open ? "Opening Garage..." : "Closing Garage...";
          if (!gate_or_dispatch_area_action("cover", want_open ? "open_cover" : "close_cover",
                                            want_open ? 100 : 0, "GARAGE DOOR PIN", now_rel)) {
            wh_garage_state() = prev_g;
          }
        } else if (wid == WidgetId::LOCK_ACTION_BTN) {
          const LockState prev_l = wh_lock_state();
          const bool want_lock = (prev_l == LockState::UNLOCKED);
          wh_lock_state() = want_lock ? LockState::LOCKED : LockState::UNLOCKED;
          rs.area.target_entity_id = "whole_home.lock";
          rs.area.action_feedback = want_lock ? "Front Door Locked" : "Front Door Unlocked";
          if (!gate_or_dispatch_area_action("lock", want_lock ? "lock" : "unlock",
                                            want_lock ? 100 : 0, "UNLOCK DOOR PIN", now_rel)) {
            wh_lock_state() = prev_l;
          }
        }
      } else if (whole_home_tab() == 3 &&
                 wid >= WidgetId::HUB_ROW_ACTION && wid < WidgetId::HUB_ROW_ACTION + 3) {
        const int v_card = static_cast<int>(wid - WidgetId::HUB_ROW_ACTION);
        rs.area.target_entity_id = "whole_home.vacuum";
        if (v_card == 0) {
          sim_vacuum_state() = "Cleaning";
          rs.area.target_action = "start";
          rs.area.action_feedback = "Vacuum Started";
        } else if (v_card == 1) {
          sim_vacuum_state() = "Paused";
          rs.area.target_action = "pause";
          rs.area.action_feedback = "Vacuum Paused";
        } else {
          sim_vacuum_state() = "Docked";
          rs.area.target_action = "dock";
          rs.area.action_feedback = "Returning to Dock";
        }
        rs.area.feedback_timer = now_rel;
        rt_.cmd(UiHostCmd::EXECUTE_AREA_ACTION);
        rt_.schedule_disp();
      }
    }
  }

  if (rel_t0_us > 0ULL && profiler_enabled()) {
    const uint32_t rel_exec_us = static_cast<uint32_t>(prof_now_us() - rel_t0_us);
    const int end_ctx = compute_touch_context(
        rs.nav.gui_page, rs.nav.home_subpage, rs.music.overlay_mode, rs.nav.active_game,
        rs.nav.system_menu_mode, rs.nav.sky_weather_subpage, rs.nav.area_subpage,
        rs.nav.remote_subpage, rs.nav.tools_subpage, whole_home_tab());
    const uint32_t dur_p = (now_rel >= gest.start_ms) ? (now_rel - gest.start_ms) : 0U;
    LATTER_PROF_LOGI(
        "[PROF:TOUCH_UP] start=(%d,%d) end=(%d,%d) dx=%d dy=%d travel=%d samples=%d dur=%lums swipe=%d ctx=%d->%d exec=%luus",
        gest.start_x, gest.start_y, gest.last_x, gest.last_y,
        dx, dy, gest.max_travel, gest.sample_count,
        static_cast<unsigned long>(dur_p), static_cast<int>(gest.swipe_committed),
        gest.start_context, end_ctx, static_cast<unsigned long>(rel_exec_us));
  }
  gest.active = false;
}

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

  if (act == "home") {
    if (can_go_home(rs)) {
      ui_focus_index() = 0;
      navigate_home(now_in);
    }
    return;
  }

  if (act == "back") {
    if (ui::pin_pad_state().open) {
      ui::pin_pad_scrub();
      ui_focus_index() = 0;
      mark_dirty_full();
      rt_.schedule_disp();
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
    if (!ui::pin_pad_state().open && !rs.alarms.ringing && !rs.timers.ringing &&
        !rs.alarms.find_my_active && !rs.alert.active) {
      if (rs.nav.gui_page == PageId::WEATHER) {
        rs.nav.sky_weather_subpage = step_wrap(rs.nav.sky_weather_subpage, step, 4);
        mark_dirty_full();
        rt_.schedule_disp();
        return;
      }
      if (rs.nav.gui_page == PageId::GAMES && rs.nav.active_game == 0 && !rs.nav.menu_scroll_active) {
        rs.nav.menu_scroll_active = true;
        rs.nav.menu_scroll_dir = (step > 0) ? 1 : -1;
        rs.nav.menu_scroll_target_page = step_wrap(rs.nav.game_carousel_idx, step, 6);
        rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
        return;
      }
    }
    if (focus_cnt > 0) {
      ui_focus_index() = step_wrap(ui_focus_index(), step, focus_cnt);
      rt_.schedule_disp();
    }
    return;
  }

  if (act == "select" || act == "ok" || act == "click" || act == "enter") {
    if (rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active) {
      const HitRegion *foc = frame_hits().focus_at(ui_focus_index());
      if (foc != nullptr) {
        on_touch_down(foc->cx(), foc->cy(), now_in);
      } else {
        rs.alarms.ringing = false;
        rs.timers.ringing = false;
        rs.alarms.find_my_active = false;
        rt_.cmd(UiHostCmd::STOP_ALARM_SOUND);
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
    const HitRegion *foc = frame_hits().focus_at(ui_focus_index());
    if (foc != nullptr) {
      on_touch_down(foc->cx(), foc->cy(), now_in);
      if (touch_gesture().active) on_touch_up(now_in);
      activate_ui_focus(now_in);
    }
  }
}

// ==============================================================================
// Compiled UiTouchRuntime Preference Accessors & Mutators
// (Replaces ~195 lines of YAML function-pointer tables in modules/system_core.yaml)
// ==============================================================================
std::string_view UiTouchRuntime::select_opt(UiSelectId sid) const noexcept {
  using S = UiSelectId;
  const auto &p = persisted_prefs();
  switch (sid) {
    case S::TAP_WAKE_MODE: return pref_tap_to_wake_cstr(p);
    case S::WEATHER_SIM_CONDITION: return pref_weather_sim_cstr();
    case S::MUSIC_OVERLAY_STYLE: return pref_music_overlay_style_cstr(p);
    case S::MUSIC_FULLSCREEN_ART_MODE: return pref_album_art_style_cstr(p);
    case S::MUSIC_ALBUM_SORT_ORDER: return pref_album_sort_order_cstr(p);
    case S::MUSIC_ARTIST_ALBUM_SORT_ORDER: return pref_artist_album_sort_order_cstr(p);
    case S::MUSIC_PODCAST_SORT_ORDER: return pref_podcast_sort_order_cstr(p);
    case S::VA_WAKE_WORD_MODE: return pref_wake_word_mode_cstr(p);
    case S::VA_VISUAL_PROFILE: return pref_va_visual_profile_cstr(p);
    case S::WW_SENS_OKAY_NABU: return pref_ww_sens_cstr(p.ww_sens_okay_nabu);
    case S::WW_SENS_HEY_LUNA: return pref_ww_sens_cstr(p.ww_sens_hey_luna);
    case S::WW_SENS_HEY_JARVIS: return pref_ww_sens_cstr(p.ww_sens_hey_jarvis);
    case S::WW_SENS_HEY_MYCROFT: return pref_ww_sens_cstr(p.ww_sens_hey_mycroft);
    case S::WW_SENS_ALEXA: return pref_ww_sens_cstr(p.ww_sens_alexa);
    case S::THEME_PALETTE: return pref_color_profile_cstr(p);
    case S::POWER_PROFILE: return pref_power_profile_cstr(p);
    case S::PICKUP_WAKE_MODE: return pref_pickup_to_wake_cstr(p);
    case S::DISPLAY_OFF_TIMEOUT: return pref_battery_screen_timeout_cstr(p);
    case S::SCREENSAVER_STYLE: return pref_screensaver_style_cstr(p);
    case S::SCREENSAVER_DOCKED_TIMEOUT: return pref_docked_idle_timeout_cstr(p);
    case S::HIBERNATE_TIMEOUT_MODE: return pref_battery_hibernate_timeout_cstr(p);
    case S::DEFAULT_START_APP: return pref_start_screen_cstr(p);
    case S::CLOCK_FACE_STYLE: return pref_clock_face_cstr(p);
    case S::TIME_FORMAT: return pref_time_format_cstr(p);
    case S::AUTO_BRIGHTNESS_MODE: return pref_auto_brightness_mode_cstr(p);
    case S::DISPLAY_BRIGHTNESS_PRESET: return pref_manual_brightness_cstr(p);
    case S::DAY_BRIGHTNESS_PRESET: return pref_day_brightness_cstr(p);
    case S::NIGHT_BRIGHTNESS_PRESET: return pref_night_brightness_cstr(p);
  }
  return {};
}

void UiTouchRuntime::set_select_opt(UiSelectId sid, std::string_view opt, bool /*pub*/) const {
  using S = UiSelectId;
  using K = PrefKey;
  const uint32_t now_ms = esphome::millis();
  if (sid == S::WEATHER_SIM_CONDITION) {
    const int idx = find_opt_index(kWeatherSimOpts, opt);
    if (idx >= 0) {
      device_prefs_state().weather_sim_condition = static_cast<uint8_t>(idx);
      if (opt != "Live (Home Assistant)" && !persisted_prefs().weather_background) {
        uint32_t bg_eff = 0U;
        latter_actions::record_local_pref_edit(K::WEATHER_BACKGROUND, "1", now_ms, bg_eff);
      }
      if (link_state().ha_connected) {
        std::string chg = "weather_sim_condition=";
        chg.append(opt.data(), opt.size());
        latter_actions::emit_set_device_prefs(persisted_prefs().prefs_seq, chg);
      }
      night_meteor_epoch_initialized() = false;
      weather_sim_dirty() = true;
      record_ambient_frame_ms(0U);
      cmd(UiHostCmd::NOTIFY_WEATHER_SIM_CHANGED);
    }
    return;
  }
  K pk = K::COUNT;
  switch (sid) {
    case S::TAP_WAKE_MODE: pk = K::TAP_TO_WAKE; break;
    case S::WEATHER_SIM_CONDITION: break;
    case S::MUSIC_OVERLAY_STYLE: pk = K::MUSIC_OVERLAY_STYLE; break;
    case S::MUSIC_FULLSCREEN_ART_MODE: pk = K::ALBUM_ART_STYLE; break;
    case S::MUSIC_ALBUM_SORT_ORDER: pk = K::ALBUM_SORT_ORDER; break;
    case S::MUSIC_ARTIST_ALBUM_SORT_ORDER: pk = K::ARTIST_ALBUM_SORT_ORDER; break;
    case S::MUSIC_PODCAST_SORT_ORDER: pk = K::PODCAST_SORT_ORDER; break;
    case S::VA_WAKE_WORD_MODE: pk = K::WAKE_WORD_MODE; break;
    case S::VA_VISUAL_PROFILE: pk = K::VA_VISUAL_PROFILE; break;
    case S::WW_SENS_OKAY_NABU: pk = K::WW_SENS_OKAY_NABU; break;
    case S::WW_SENS_HEY_LUNA: pk = K::WW_SENS_HEY_LUNA; break;
    case S::WW_SENS_HEY_JARVIS: pk = K::WW_SENS_HEY_JARVIS; break;
    case S::WW_SENS_HEY_MYCROFT: pk = K::WW_SENS_HEY_MYCROFT; break;
    case S::WW_SENS_ALEXA: pk = K::WW_SENS_ALEXA; break;
    case S::THEME_PALETTE: pk = K::COLOR_PROFILE; break;
    case S::POWER_PROFILE: pk = K::POWER_PROFILE; break;
    case S::PICKUP_WAKE_MODE: pk = K::PICKUP_TO_WAKE; break;
    case S::DISPLAY_OFF_TIMEOUT: pk = K::BATTERY_SCREEN_TIMEOUT; break;
    case S::SCREENSAVER_STYLE: pk = K::SCREENSAVER_STYLE; break;
    case S::SCREENSAVER_DOCKED_TIMEOUT: pk = K::DOCKED_IDLE_TIMEOUT; break;
    case S::HIBERNATE_TIMEOUT_MODE: pk = K::BATTERY_HIBERNATE_TIMEOUT; break;
    case S::DEFAULT_START_APP: pk = K::START_SCREEN; break;
    case S::CLOCK_FACE_STYLE: pk = K::CLOCK_FACE; break;
    case S::TIME_FORMAT: pk = K::TIME_FORMAT; break;
    case S::AUTO_BRIGHTNESS_MODE: pk = K::AUTO_BRIGHTNESS_MODE; break;
    case S::DISPLAY_BRIGHTNESS_PRESET: pk = K::MANUAL_BRIGHTNESS; break;
    case S::DAY_BRIGHTNESS_PRESET: pk = K::DAY_BRIGHTNESS; break;
    case S::NIGHT_BRIGHTNESS_PRESET: pk = K::NIGHT_BRIGHTNESS; break;
  }
  if (pk == K::COUNT) return;
  uint32_t eff = 0U;
  if (latter_actions::record_local_pref_edit(pk, opt, now_ms, eff)) {
    device_prefs_state().last_effects = eff;
    cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
  }
}

bool UiTouchRuntime::sw_state(UiSwitchId swid) const noexcept {
  using W = UiSwitchId;
  const auto &p = persisted_prefs();
  switch (swid) {
    case W::HOME_WEATHER_BG: return p.weather_background;
    case W::HOME_WAKE_GREETING: return p.wake_greeting;
    case W::MUSIC_SHOW_SPEAKER_SHORTCUT: return p.show_speaker_shortcut;
    case W::MUSIC_SHOW_LIBRARY_SHORTCUT: return p.show_library_shortcut;
    case W::BLE_AREA_BEACON: return p.ble_beacon_enabled;
    case W::GAME_SOUND: return p.game_sound_effects;
    case W::VA_WAKE_SCREEN: return p.wake_screen_on_assist;
    case W::DOCKED_SCREENSAVER: return p.docked_screensaver;
    case W::INSTANT_CROWN_HIBERNATE: return p.hold_crown_power_off;
  }
  return false;
}

void UiTouchRuntime::sw_toggle(UiSwitchId swid) const {
  using W = UiSwitchId;
  using K = PrefKey;
  const auto &p = persisted_prefs();
  K pk = K::COUNT;
  bool next_val = false;
  switch (swid) {
    case W::HOME_WEATHER_BG: pk = K::WEATHER_BACKGROUND; next_val = !p.weather_background; break;
    case W::HOME_WAKE_GREETING: pk = K::WAKE_GREETING; next_val = !p.wake_greeting; break;
    case W::MUSIC_SHOW_SPEAKER_SHORTCUT: pk = K::SHOW_SPEAKER_SHORTCUT; next_val = !p.show_speaker_shortcut; break;
    case W::MUSIC_SHOW_LIBRARY_SHORTCUT: pk = K::SHOW_LIBRARY_SHORTCUT; next_val = !p.show_library_shortcut; break;
    case W::BLE_AREA_BEACON: pk = K::BLE_BEACON_ENABLED; next_val = !p.ble_beacon_enabled; break;
    case W::GAME_SOUND: pk = K::GAME_SOUND_EFFECTS; next_val = !p.game_sound_effects; break;
    case W::VA_WAKE_SCREEN: pk = K::WAKE_SCREEN_ON_ASSIST; next_val = !p.wake_screen_on_assist; break;
    case W::DOCKED_SCREENSAVER: pk = K::DOCKED_SCREENSAVER; next_val = !p.docked_screensaver; break;
    case W::INSTANT_CROWN_HIBERNATE: pk = K::HOLD_CROWN_POWER_OFF; next_val = !p.hold_crown_power_off; break;
  }
  if (pk == K::COUNT) return;
  uint32_t eff = 0U;
  if (latter_actions::record_local_pref_edit(pk, next_val ? "1" : "0", esphome::millis(), eff)) {
    device_prefs_state().last_effects = eff;
    cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
  }
}

float UiTouchRuntime::num_val(UiNumberId nid) const noexcept {
  using N = UiNumberId;
  const auto &p = persisted_prefs();
  switch (nid) {
    case N::VOLUME_STEP_SIZE: return static_cast<float>(p.volume_step_size);
    case N::SPEAKER_VOLUME:
      if (!std::isnan(local_mp_volume) && local_mp_volume >= 0.0f && local_mp_volume <= 1.0f) {
        return std::round(local_mp_volume * 100.0f);
      }
      return static_cast<float>(p.speaker_volume_pct);
    case N::ALERT_VOLUME: return static_cast<float>(p.alert_volume);
    case N::DAY_START_HOUR: return static_cast<float>(p.day_start_hour);
    case N::NIGHT_START_HOUR: return static_cast<float>(p.night_start_hour);
  }
  return 0.0f;
}

void UiTouchRuntime::set_num_val(UiNumberId nid, float val) const {
  using N = UiNumberId;
  using K = PrefKey;
  const uint32_t now_ms = esphome::millis();
  if (nid == N::SPEAKER_VOLUME) {
    const int cl_pct = std::clamp(static_cast<int>(std::lround(val)), 10, 100);
    auto &p = persisted_prefs();
    if (p.speaker_volume_pct != static_cast<uint8_t>(cl_pct)) {
      p.speaker_volume_pct = static_cast<uint8_t>(cl_pct);
      schedule_persisted_prefs_save(now_ms);
    }
    local_mp_volume = static_cast<float>(cl_pct) * 0.01f;
    cmd(UiHostCmd::SET_LOCAL_SPEAKER_VOL_PCT, cl_pct);
    return;
  }
  K pk = K::COUNT;
  int cl_val = static_cast<int>(std::lround(val));
  switch (nid) {
    case N::VOLUME_STEP_SIZE: pk = K::VOLUME_STEP_SIZE; cl_val = std::clamp(cl_val, 1, 10); break;
    case N::SPEAKER_VOLUME: break;
    case N::ALERT_VOLUME: pk = K::ALERT_VOLUME; cl_val = std::clamp(cl_val, 20, 100); break;
    case N::DAY_START_HOUR: pk = K::DAY_START_HOUR; cl_val = std::clamp(cl_val, 0, 23); break;
    case N::NIGHT_START_HOUR: pk = K::NIGHT_START_HOUR; cl_val = std::clamp(cl_val, 0, 23); break;
  }
  if (pk == K::COUNT) return;
  char buf[16];
  const int n = std::snprintf(buf, sizeof(buf), "%d", cl_val);
  if (n <= 0) return;
  uint32_t eff = 0U;
  if (latter_actions::record_local_pref_edit(pk, std::string_view(buf, static_cast<size_t>(n)), now_ms, eff)) {
    device_prefs_state().last_effects = eff;
    cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
  }
}

}  // namespace latter_ui
