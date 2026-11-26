// ==============================================================================
// Latter Compiled On-Device System, Remote & Tools Touch Dispatch
// (components/latter_ui/touch_dispatch_system_tools.cpp)
// ==============================================================================

#include "touch_dispatch_internal.h"
#include "ui_pages.h"
#include "ui_touch_recognizer.h"
#include "hal_display_lifecycle.h"
#include "hal_panel_power.h"
#include "latter_actions.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef USE_ESP32
#include <esp_random.h>
#endif

namespace latter_ui {
using namespace latter_core;
using namespace touch_detail;

namespace {

inline void do_save_custom_color_to_latter() {
  save_persisted_prefs();
  latter_core::emit_save_custom_color_rgb(persisted_prefs().custom_theme_rgb);
}

inline void do_remote_select_slot(int slot_num) {
  if (!remote_state().slots_summary.empty()) {
    latter_core::emit_remote_action(slot_num, "select_slot");
  }
}

inline uint32_t next_rng_u32() {
#ifdef USE_ESP32
  return esp_random();
#else
  return static_cast<uint32_t>(std::rand());
#endif
}

}  // namespace

namespace touch_detail {

int step_brightness_tap_10pct(int cur_pct) noexcept {
  if (cur_pct < 10 || cur_pct >= 100) return 10;
  return std::min(100, ((cur_pct / 10) + 1) * 10);
}

}  // namespace touch_detail

int TouchGestureEngine::remote_key_for_widget_(uint16_t wid) noexcept {
  switch (wid) {
    case WidgetId::REMOTE_DPAD_UP:    return 0;
    case WidgetId::REMOTE_DPAD_DOWN:  return 1;
    case WidgetId::REMOTE_DPAD_LEFT:  return 2;
    case WidgetId::REMOTE_DPAD_RIGHT: return 3;
    case WidgetId::REMOTE_DPAD_OK:    return 4;
    case WidgetId::REMOTE_NAV_BACK:   return 5;
    case WidgetId::REMOTE_NAV_HOME:   return 6;
    case WidgetId::REMOTE_VOL_UP:     return 7;
    case WidgetId::REMOTE_VOL_DOWN:   return 8;
    default: return -1;
  }
}

void TouchGestureEngine::do_remote_send_key_(int key_idx, int target_slot, bool long_press, uint32_t now_override_ms) {
  auto &rs = runtime_state();
  const int res_slot = (target_slot == -1) ? resolve_active_remote_slot(0)
                                           : resolve_active_remote_slot(target_slot);
  const int slot_1based = res_slot + 1;

  static constexpr const char *kCanonicalCmds[10] = {
      "up", "down", "left", "right", "select",
      "back", "home", "volume_up", "volume_down", "toggle"};
  static constexpr const char *kFeedback[10] = {
      "Up", "Down", "Left", "Right", "Select",
      "Back", "Home", "Vol +", "Vol -", "Toggle Power"};
  static constexpr const char *kHoldFeedback[10] = {
      "Hold Up", "Hold Down", "Hold Left", "Hold Right", "Hold OK",
      "Hold Back", "Hold Home", "Vol +", "Vol -", "Toggle Power"};
  const int k = (key_idx >= 0 && key_idx < 10) ? key_idx : 0;
  const char *act = long_press
                        ? "long_press"
                        : ((k == 7) ? "volume_up"
                                    : ((k == 8) ? "volume_down"
                                                : ((k == 9) ? "toggle" : "command")));
  const char *cmd = kCanonicalCmds[k];
  const std::string *t_names = remote_slots_name();
  if (k == 9 && !t_names[res_slot].empty()) {
    rs.remote.action_feedback = "Power: " + t_names[res_slot];
  } else {
    rs.remote.action_feedback = long_press ? kHoldFeedback[k] : kFeedback[k];
  }
  rs.remote.feedback_timer = TouchGestureEngine::now_or_hal_ms_(now_override_ms);
  if (!remote_state().slots_summary.empty()) {
    latter_core::emit_remote_action(slot_1based, act, cmd);
  }
}

bool TouchGestureEngine::handle_down_system_tools_(TouchGestureEngine &eng, int tx, int ty,
                                                 uint32_t now_ms, const HitRegion *hit,
                                                 uint16_t wid, uint32_t tkey) {
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  auto &rt = eng.rt_;
  auto &gest = touch_gesture();
  const auto &ly = layout();

  auto back_to_sys_root = [&]() {
    rs.nav.system_menu_mode = 0;
    rs.nav.menu_scroll_active = false;
    if (TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms)) return;
    mark_dirty_full();
    rt.schedule_disp();
  };

  if (rs.nav.gui_page == PageId::SYSTEM) {
    const int mode = rs.nav.system_menu_mode;
    if (mode == SystemMenuMode::SLEEP_WAKE || mode == SystemMenuMode::DISPLAY ||
        mode == SystemMenuMode::SYSTEM_INFO) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        back_to_sys_root();
      }
      return true;
    }
    if (mode == SystemMenuMode::ACTIONS) {
      if (wid == WidgetId::BACK_BUTTON) {
        if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) back_to_sys_root();
        return true;
      }
      if (wid == WidgetId::SYS_SECONDARY_BTN) {
        if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          rs.nav.system_menu_mode = SystemMenuMode::CONFIRM_RESTART;
          mark_dirty_full();
          rt.schedule_disp();
        }
        return true;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5 &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int slot = hit->param;
        if (slot == 0) {
          rt.cmd(UiHostCmd::TRIGGER_SCREENSAVER_NOW);
        } else if (slot == 1) {
#if LATTER_HAS_BATTERY
          rt.cmd(UiHostCmd::START_HIBERNATION_COUNTDOWN);
#else
          rt.cmd(UiHostCmd::TURN_SCREEN_OFF);
#endif
        } else if (slot == 2) {
          rs.nav.system_menu_mode = SystemMenuMode::UPDATE;
          mark_dirty_full();
          rt.schedule_disp();
        } else if (slot == 3) {
          rt.sw_toggle(UiSwitchId::BLE_AREA_BEACON);
          rt.schedule_disp();
        }
      }
      return true;
    }
    if (mode == SystemMenuMode::CONFIRM_RESTART || mode == SystemMenuMode::CONFIRM_UPDATE) {
      return true;
    }
    if (mode == SystemMenuMode::UPDATE) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.nav.update_check_state = 0;
        back_to_sys_root();
        return true;
      }
      if (wid == WidgetId::SYS_PRIMARY_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const bool has_update = is_newer_semver(
            rt.fw_latest_version, (rt.fw_version != nullptr) ? rt.fw_version : "0.0.0");
        if (has_update) {
          rs.nav.system_menu_mode = SystemMenuMode::CONFIRM_UPDATE;
          mark_dirty_full();
          rt.schedule_disp();
        } else if (rs.nav.update_check_state == 0) {
          rs.nav.update_check_state = 1;
          rs.nav.update_check_timer = now_ms;
          rt.cmd(UiHostCmd::CHECK_FIRMWARE_UPDATE);
          rt.schedule_disp();
        }
      }
      return true;
    }
    if (mode == SystemMenuMode::SOUND) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        back_to_sys_root();
        return true;
      }
      if (wid == WidgetId::SYS_VOL_SLIDER) return true;
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5) {
        const int slot = hit->param;
        if (slot == 1 && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
          const float cur_a_f = rt.num_val(UiNumberId::ALERT_VOLUME);
          const int cur_a = (std::isnan(cur_a_f) || cur_a_f < 20.0f) ? 70 : static_cast<int>(std::round(cur_a_f));
          const int nxt_a = static_cast<int>(step_wrap_range(static_cast<float>(cur_a), 10.0f, 50.0f, 100.0f));
          rt.set_num_val(UiNumberId::ALERT_VOLUME, static_cast<float>(nxt_a));
          rt.schedule_disp();
        } else if (slot == 2 && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          const int cur_sz = static_cast<int>(std::round(rt.num_val(UiNumberId::VOLUME_STEP_SIZE)));
          const int nxt_sz = (cur_sz < 5) ? 5 : ((cur_sz < 10) ? 10 : 2);
          rt.set_num_val(UiNumberId::VOLUME_STEP_SIZE, static_cast<float>(nxt_sz));
          rt.schedule_disp();
        } else if (slot == 3 && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          rt.sw_toggle(UiSwitchId::GAME_SOUND);
          rt.schedule_disp();
        }
      }
      return true;
    }
    if (mode == SystemMenuMode::MUSIC) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        back_to_sys_root();
        return true;
      }
      const int per_pg = std::clamp(layout().list_rows, 4, kMaxListSlots);
      const int max_pg = std::max(1, (6 + per_pg - 1) / per_pg);
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
        rs.music.settings_page = std::clamp(
            rs.music.settings_page + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, max_pg);
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + kMaxListSlots &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        cycle_music_pref_slot(rt, hit->param);
        rt.schedule_disp();
      }
      return true;
    }
    if (mode == SystemMenuMode::VOICE) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        back_to_sys_root();
        return true;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5 &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int slot = hit->param;
        if (slot == 0) {
          rt.set_select_opt(UiSelectId::VA_WAKE_WORD_MODE,
                            cycle_option(rt.select_opt(UiSelectId::VA_WAKE_WORD_MODE),
                                         {"Docked Only", "Always On", "Disabled"}));
          rt.schedule_disp();
        } else if (slot == 1) {
          if (rt.select_opt(UiSelectId::VA_WAKE_WORD_MODE) != "Disabled") {
            rt.cmd(UiHostCmd::CYCLE_PRIMARY_WAKE_WORD);
            rt.schedule_disp();
          }
        } else if (slot == 2) {
          if (rt.select_opt(UiSelectId::VA_WAKE_WORD_MODE) != "Disabled") {
            rs.nav.system_menu_mode = SystemMenuMode::WW_SENSITIVITY;
            mark_dirty_full();
            rt.schedule_disp();
          }
        } else if (slot == 3) {
          rt.set_select_opt(UiSelectId::VA_VISUAL_PROFILE,
                            cycle_option(rt.select_opt(UiSelectId::VA_VISUAL_PROFILE),
                                         {"Nebula", "Solaris", "Rings", "Light Bar", "Dot Wave"}));
          rt.schedule_disp();
        } else if (slot == 4) {
          rt.sw_toggle(UiSwitchId::VA_WAKE_SCREEN);
          rt.schedule_disp();
        }
      }
      return true;
    }
    if (mode == SystemMenuMode::WW_SENSITIVITY) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.nav.system_menu_mode = SystemMenuMode::VOICE;
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + 5 &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int slot = std::clamp(static_cast<int>(hit->param), 0, 4);
        static constexpr UiSelectId kSensIds[5] = {
            UiSelectId::WW_SENS_OKAY_NABU, UiSelectId::WW_SENS_HEY_LUNA,
            UiSelectId::WW_SENS_HEY_JARVIS, UiSelectId::WW_SENS_HEY_MYCROFT,
            UiSelectId::WW_SENS_ALEXA};
        rt.set_select_opt(kSensIds[slot],
                          cycle_option(rt.select_opt(kSensIds[slot]),
                                       {"Slightly sensitive", "Moderately sensitive", "Very sensitive"}));
        rt.schedule_disp();
      }
      return true;
    }
    if (mode == SystemMenuMode::COLOR_WHEEL) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.nav.system_menu_mode = SystemMenuMode::DISPLAY;
        rt.cmd(UiHostCmd::APPLY_THEME_PRESET);
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      if (wid == WidgetId::COLOR_WHEEL_SAVE && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        save_persisted_prefs();
        rt.set_select_opt(UiSelectId::THEME_PALETTE, "Custom");
        do_save_custom_color_to_latter();
        rs.nav.system_menu_mode = SystemMenuMode::DISPLAY;
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      if (wid == WidgetId::COLOR_WHEEL_RING) {
        float hue = 0.0f, sat = 1.0f;
        if (ui::hsv_wheel_pick(tx, ty, ui::system_color_wheel_geom(), hue, sat)) {
          gest.axis_lock = GestureAxisLock::Slider;
          gest.tap_cancelled = true;
          color_wheel_hue_deg() = hue;
          color_wheel_sat() = sat;
          const UiColor c = hsv_to_rgb(hue / 360.0f, sat, 1.0f);
          prefs.custom_theme_rgb = (static_cast<uint32_t>(c.r) << 16) |
                                   (static_cast<uint32_t>(c.g) << 8) |
                                   static_cast<uint32_t>(c.b);
          if (rt.theme_primary != nullptr && rt.theme_secondary != nullptr) {
            apply_custom_rgb_harmony_slots(prefs.custom_theme_rgb, rt.theme_primary, rt.theme_secondary);
          }
          rt.schedule_disp();
        }
      }
      return true;
    }
    return true;
  }

  if (rs.nav.gui_page == PageId::REMOTE) {
    if (rs.nav.remote_subpage == 1) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.remote.action_feedback.clear();
        rs.nav.remote_subpage = 0;
        if (TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms)) return true;
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
        gest.active = false;
        int valid_slots[MAX_REMOTE_SLOTS] = {0};
        const int valid_count = collect_configured_remote_slots(valid_slots);
        const int total_pages = std::max(1, (valid_count + 3) / 4);
        const int next_pg = std::clamp(
            remote_control_page() + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 0, total_pages - 1);
        if (next_pg != remote_control_page()) {
          remote_control_page() = next_pg;
          rt.schedule_disp();
        }
      }
      return true;
    }

    const int act_slot = resolve_active_remote_slot(rs.remote.active_slot);
    rs.remote.active_slot = act_slot;

    if (wid == WidgetId::REMOTE_SLOT_PILL) {
      if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        gest.active = false;
        rs.remote.action_feedback.clear();
        rs.nav.remote_subpage = 1;
        mark_dirty_full();
        rt.schedule_disp();
      }
      return true;
    }

    if (hit != nullptr && (hit->flags & kHitLongPress) != 0U) {
      gest.arm_long_press(wid, hit->param);
      schedule_core_timeout(kTimeoutTouchLongPressId, kinematics::kLongPressHoldMs, []() {
        touch_engine().tick(TouchGestureEngine::now_or_hal_ms_(last_user_touch_ms()));
      });
      rt.schedule_band_disp(std::max(0, static_cast<int>(hit->y0) - 6),
                            std::min(SCREEN_H - 1, static_cast<int>(hit->y1) + 6));
      return true;
    }

    uint16_t eff_wid = wid;
    uint32_t eff_tkey = tkey;
    if (eff_wid == WidgetId::NONE) {
      const RemoteDpadGeom dp = compute_remote_dpad_geom(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0);
      eff_wid = resolve_remote_dpad_widget(tx, ty, dp);
      if (eff_wid != WidgetId::NONE) {
        eff_tkey = make_tap_target_key(rs.nav.gui_page, eff_wid, 0);
      }
    }

    const int remote_key = TouchGestureEngine::remote_key_for_widget_(eff_wid);
    if (remote_key >= 0 && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, eff_tkey)) {
      gest.active = false;
      TouchGestureEngine::do_remote_send_key_(remote_key, act_slot, false, now_ms);
      rt.schedule_band_disp(0, std::min(SCREEN_H - 1, ly.hdr_y + dp_px(24)));
    }
    return true;
  }

  if (rs.nav.gui_page != PageId::TOOLS) return false;

  const int sub = rs.nav.tools_subpage;
  if (sub == ToolsSubpage::LAUNCHER) return true;

  auto exit_tool_subpage = [&](bool sync_policy) {
    rs.nav.tools_subpage = 0;
    if (sync_policy) rt.apply_power_policy();
    if (TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms)) return;
    mark_dirty_full();
    rt.schedule_disp();
  };

#if LATTER_ROUND_DISPLAY
  if (sub == ToolsSubpage::STOPWATCH) {
    if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      exit_tool_subpage(false);
      return true;
    }
    if (wid == WidgetId::SW_PRIMARY_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      if (!rs.stopwatch.running) {
        rs.stopwatch.running = true;
        rs.stopwatch.started_in_analog = true;
        rs.stopwatch.start_time_ms = now_ms - rs.stopwatch.elapsed_ms;
        rs.nav.sleep_start_time = 0;
      } else {
        rs.stopwatch.running = false;
        rs.stopwatch.elapsed_ms = now_ms - rs.stopwatch.start_time_ms;
      }
      latter_core::check_and_emit_timer_alarm_state(now_ms);
      rt.schedule_disp();
      return true;
    }
    if (wid == WidgetId::SW_LAP_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      if (rs.stopwatch.running) {
        record_stopwatch_lap(rs.stopwatch, now_ms - rs.stopwatch.start_time_ms);
      } else if (rs.stopwatch.elapsed_ms > 0) {
        rs.stopwatch.lap_ms = rs.stopwatch.elapsed_ms;
      }
      rt.schedule_disp();
      return true;
    }
    if (wid == WidgetId::SW_RESET_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.stopwatch.reset();
      latter_core::check_and_emit_timer_alarm_state(now_ms);
      rt.schedule_disp();
    }
    return true;
  }
#endif

  if (sub == ToolsSubpage::DICE) {
    if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      exit_tool_subpage(true);
    } else if (wid == WidgetId::DICE_ROLL_BTN && !dice_state().anim_active &&
               touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      trigger_dice_roll(now_ms, next_rng_u32(), next_rng_u32());
      rt.schedule_disp();
    }
    return true;
  }

  if (sub == ToolsSubpage::FLASHLIGHT) {
    if (ty <= ly.top_edge_zone_y()) return true;
    if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.nav.tools_subpage = 0;
      rt.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
      if (!TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms)) {
        mark_dirty_full();
        rt.schedule_disp();
      }
    }
    return true;
  }

  if (sub == ToolsSubpage::CALCULATOR) {
    if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      reset_calculator_state();
      exit_tool_subpage(false);
    } else if (wid >= WidgetId::CALC_KEY_BASE && wid < WidgetId::CALC_KEY_BASE + 20 &&
               touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
      handle_calculator_key_index(static_cast<int>(wid - WidgetId::CALC_KEY_BASE));
      rt.schedule_disp();
    }
    return true;
  }

  if (sub == ToolsSubpage::LEVEL) {
    if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      exit_tool_subpage(true);
    }
    return true;
  }

  if (sub == ToolsSubpage::COIN_FLIP) {
    if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      exit_tool_subpage(true);
    } else if (wid == WidgetId::COIN_FLIP_BTN && !coin_state().anim_active &&
               touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      trigger_coin_flip(now_ms, next_rng_u32());
      rt.schedule_disp();
    }
  }
  return true;
}

bool TouchGestureEngine::handle_tap_system_tools_(
    int /*tx*/, int /*ty*/, uint32_t now_rel, const HitRegion *hit, uint16_t wid) {
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  if (rs.nav.gui_page == PageId::SYSTEM) {
    const int mode = rs.nav.system_menu_mode;
    if (!rs.nav.menu_scroll_active && mode == 0) {
      const int total_pages = std::max(1, system_dashboard_page_count());
      if ((wid == WidgetId::CHEVRON_LEFT || wid == WidgetId::CHEVRON_RIGHT) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        if (total_pages > 1) {
          const int cur_pg = std::clamp(rs.nav.system_dashboard_page, 0, total_pages - 1);
          rs.nav.system_dashboard_page = cur_pg;
          const int target_pg =
              std::clamp(cur_pg + ((wid == WidgetId::CHEVRON_RIGHT) ? 1 : -1), 0, total_pages - 1);
          if (target_pg != cur_pg) {
            rs.nav.menu_scroll_active = true;
            rs.nav.menu_scroll_dir = (target_pg > cur_pg) ? 1 : -1;
            rs.nav.menu_scroll_target_page = target_pg;
            rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
          }
        }
      } else if (wid >= WidgetId::SYSTEM_CARD_BASE && wid < WidgetId::SYSTEM_CARD_BASE + 16 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int item_idx = hit->param;
        if (item_idx == 0) { rs.nav.system_menu_mode = SystemMenuMode::DISPLAY; rs.nav.display_menu_page = 1; }
        else if (item_idx == 1) { rs.nav.system_menu_mode = SystemMenuMode::SLEEP_WAKE; rs.nav.sleep_menu_page = 1; }
        else if (item_idx == 2) { rs.nav.system_menu_mode = SystemMenuMode::SOUND; }
        else if (item_idx == 3) { rs.nav.system_menu_mode = SystemMenuMode::VOICE; }
        else if (item_idx == 4) { rs.nav.system_menu_mode = SystemMenuMode::MUSIC; rs.music.settings_page = 1; }
        else if (item_idx == 5 && prefs.home_screen_enabled) {
          rs.nav.gui_page = PageId::HOME;
          rs.nav.home_subpage = 1;
          rs.nav.home_settings_page = 1;
        }
        else if (item_idx == 6) { rs.nav.system_menu_mode = SystemMenuMode::ACTIONS; }
        else if (item_idx == 7) { rs.nav.system_menu_mode = SystemMenuMode::SYSTEM_INFO; }
        mark_dirty_full();
        rt_.schedule_disp();
      }
    } else if (!rs.nav.menu_scroll_active &&
               (mode == SystemMenuMode::SLEEP_WAKE || mode == SystemMenuMode::DISPLAY)) {
      const int per_pg = std::clamp(layout().list_rows, 4, kMaxListSlots);
      const int cur_pg = (mode == SystemMenuMode::SLEEP_WAKE) ? rs.nav.sleep_menu_page : rs.nav.display_menu_page;
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        const int max_pg = (mode == SystemMenuMode::SLEEP_WAKE) ? std::max(1, (10 + per_pg - 1) / per_pg)
                                                                : std::max(1, (11 + per_pg - 1) / per_pg);
        const int target_pg = (wid == WidgetId::CHEVRON_DOWN)
                                  ? std::min(max_pg, cur_pg + 1)
                                  : std::max(1, cur_pg - 1);
        if (target_pg != cur_pg) {
          rs.nav.menu_scroll_active = true;
          rs.nav.menu_scroll_dir = (target_pg > cur_pg) ? 1 : -1;
          rs.nav.menu_scroll_target_page = target_pg;
          rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
        }
      } else if (mode == SystemMenuMode::SLEEP_WAKE &&
                 wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + kMaxListSlots &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int idx = hit->param;
        if (idx == 0) {
#if LATTER_HAS_IMU
          rt_.set_select_opt(UiSelectId::PICKUP_WAKE_MODE,
                             cycle_option(rt_.select_opt(UiSelectId::PICKUP_WAKE_MODE),
                                          {"Always On", "Docked Only", "Disabled"}));
#endif
        } else if (idx == 1) {
          rt_.set_select_opt(UiSelectId::TAP_WAKE_MODE,
                             cycle_option(rt_.select_opt(UiSelectId::TAP_WAKE_MODE),
                                          {"Single Tap", "Double Tap", "Docked Only", "Disabled"}));
        } else if (idx == 2) {
#if LATTER_HAS_BATTERY
          rt_.set_select_opt(UiSelectId::DISPLAY_OFF_TIMEOUT,
                             cycle_option(rt_.select_opt(UiSelectId::DISPLAY_OFF_TIMEOUT),
                                          {"10s", "15s", "30s", "1 min", "2 min", "Never"}));
#endif
        } else if (idx == 3) {
          rt_.set_select_opt(UiSelectId::SCREENSAVER_MODE,
                             cycle_option(rt_.select_opt(UiSelectId::SCREENSAVER_MODE),
                                          {"Docked Only", "Always", "Never"}));
        } else if (idx == 4 && rt_.select_opt(UiSelectId::SCREENSAVER_MODE) != "Never") {
          rt_.set_select_opt(UiSelectId::SCREENSAVER_STYLE,
                             cycle_option(rt_.select_opt(UiSelectId::SCREENSAVER_STYLE),
                                          {"Ambient Clock", "Photo Frame", "Aurora",
                                           "Starfield", "Digital Rain", "Fireworks"}));
        } else if (idx == 5) {
          rt_.set_select_opt(UiSelectId::SCREENSAVER_DOCKED_TIMEOUT,
                             cycle_option(rt_.select_opt(UiSelectId::SCREENSAVER_DOCKED_TIMEOUT),
                                          {"10s", "15s", "30s", "1 min", "2 min", "5 min", "Never"}));
        } else if (idx == 6) {
#if LATTER_HAS_BATTERY
          rt_.set_select_opt(UiSelectId::HIBERNATE_TIMEOUT_MODE,
                             cycle_option(rt_.select_opt(UiSelectId::HIBERNATE_TIMEOUT_MODE),
                                          {"5 min", "10 min", "15 min", "30 min", "Disabled"}));
#endif
        } else if (idx == 7) {
          rs.nav.ambient_bri_override_pct = -1;
          rt_.sw_toggle(UiSwitchId::BEDTIME_MODE);
        } else if (idx == 8) {
          rt_.set_select_opt(UiSelectId::BEDTIME_DISPLAY,
                             cycle_option(rt_.select_opt(UiSelectId::BEDTIME_DISPLAY),
                                          {"Screen Off", "Ambient Clock"}));
        } else if (idx == 9) {
          rt_.sw_toggle(UiSwitchId::DO_NOT_DISTURB);
        }
        rt_.schedule_disp();
      } else if (mode == SystemMenuMode::DISPLAY && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        if (wid >= WidgetId::COLOR_SWATCH_BASE && wid < WidgetId::COLOR_SWATCH_BASE + 10) {
          rs.nav.system_menu_mode = SystemMenuMode::COLOR_WHEEL;
          mark_dirty_full();
          rt_.schedule_disp();
        } else if (wid == WidgetId::SYS_BRI_SLIDER) {
          UiNumberId nid = UiNumberId::MANUAL_BRIGHTNESS;
          if (TouchGestureEngine::resolve_sys_bri_slider_id_(
                  hit->param, rt_.select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE), nid)) {
            const int cur_pct = static_cast<int>(std::lround(rt_.num_val(nid)));
            rt_.set_num_val(nid, static_cast<float>(step_brightness_tap_10pct(cur_pct)));
            rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
            rt_.schedule_disp();
          }
        } else if (wid >= WidgetId::SYS_TOGGLE_ROW && wid < WidgetId::SYS_TOGGLE_ROW + kMaxListSlots) {
          const int item_idx = hit->param;
          const std::string_view ab = rt_.select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE);
          if (item_idx == 0) {
            if (prefs.home_screen_enabled) {
              rt_.set_select_opt(UiSelectId::DEFAULT_START_APP,
                                 cycle_option(rt_.select_opt(UiSelectId::DEFAULT_START_APP),
                                              {"Home", "Pinned", "Time App", "Calendar", "Clock Face",
                                               "Media", "Home Hub", "Area Controls", "Remote",
                                               "Weather", "Cameras", "Games", "Tools"}));
            } else {
              rt_.set_select_opt(UiSelectId::DEFAULT_START_APP,
                                 cycle_option(rt_.select_opt(UiSelectId::DEFAULT_START_APP),
                                              {"Pinned", "Time App", "Calendar", "Clock Face",
                                               "Media", "Home Hub", "Area Controls", "Remote",
                                               "Weather", "Cameras", "Games", "Tools"}));
            }
          } else if (item_idx == 1) {
#if LATTER_ROUND_DISPLAY
            const std::string next_cf = cycle_option(
                rt_.select_opt(UiSelectId::CLOCK_FACE_STYLE),
                {"Minimal Digital", "Modern Analog", "Hybrid Digital"});
#else
            const std::string next_cf = cycle_option(
                rt_.select_opt(UiSelectId::CLOCK_FACE_STYLE),
                {"Minimal Digital", "Hybrid Digital"});
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
              rs.nav.system_menu_mode = SystemMenuMode::COLOR_WHEEL;
              mark_dirty_full();
            }
          } else if (item_idx == 4) {
            const std::string next_gs = cycle_option(
                rt_.select_opt(UiSelectId::GLASS_STYLE),
                {"Auto", "Clear", "Dark"});
            rt_.set_select_opt(UiSelectId::GLASS_STYLE, next_gs);
            mark_dirty_full();
          } else if (item_idx == 5) {
            const std::string next_m =
                cycle_option(ab, {"Disabled", "Sun Synchronized", "Time Schedule", "Ambient Light"});
            rs.nav.ambient_bri_override_pct = -1;
            rt_.set_select_opt(UiSelectId::AUTO_BRIGHTNESS_MODE, next_m, true);
            rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
          } else if (item_idx == 7 && ab == "Time Schedule") {
            const int dh = static_cast<int>(rt_.num_val(UiNumberId::DAY_START_HOUR));
            const int nh = static_cast<int>(rt_.num_val(UiNumberId::NIGHT_START_HOUR));
            int nxt_dh = 7, nxt_nh = 22;
            if (dh == 7 && nh == 22) { nxt_dh = 6; nxt_nh = 23; }
            else if (dh == 6 && nh == 23) { nxt_dh = 8; nxt_nh = 22; }
            else if (dh == 8 && nh == 22) { nxt_dh = 7; nxt_nh = 21; }
            rt_.set_num_val(UiNumberId::DAY_START_HOUR, static_cast<float>(nxt_dh));
            rt_.set_num_val(UiNumberId::NIGHT_START_HOUR, static_cast<float>(nxt_nh));
            rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
          } else if (item_idx == 10) {
            rt_.sw_toggle(UiSwitchId::SCREENSAVER_PHOTO_CLOCK);
          }
          rt_.schedule_disp();
        }
      }
    } else if (!rs.nav.menu_scroll_active && (mode == 4 || mode == 6)) {
      if (wid == WidgetId::CONFIRM_CANCEL && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.nav.system_menu_mode = (mode == 4) ? 3 : 5;
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (wid == WidgetId::CONFIRM_ACCEPT && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rt_.cmd((mode == 4) ? UiHostCmd::RESTART_DEVICE : UiHostCmd::TRIGGER_FIRMWARE_UPDATE);
      }
    } else if (mode == 7 && wid == WidgetId::SYS_VOL_SLIDER &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      const float new_vol = step_wrap_range(rt_.local_mp_volume, 0.10f, 0.10f, 1.00f);
      rt_.set_num_val(UiNumberId::SPEAKER_VOLUME, std::round(new_vol * 100.0f));
      rt_.schedule_disp();
    }
    return true;
  }

  if (rs.nav.gui_page == PageId::REMOTE) {
    if (rs.nav.remote_subpage == 0) {
      const int remote_key = TouchGestureEngine::remote_key_for_widget_(wid);
      if (remote_key >= 0 && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        const int act_slot = resolve_active_remote_slot(rs.remote.active_slot);
        rs.remote.active_slot = act_slot;
        TouchGestureEngine::do_remote_send_key_(remote_key, act_slot, false, now_rel);
        rt_.schedule_band_disp(0, std::min(SCREEN_H - 1, layout().hdr_y + dp_px(24)));
      }
    } else if (rs.nav.remote_subpage == 1) {
      if (wid == WidgetId::REMOTE_PRIMARY_PWR &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        TouchGestureEngine::do_remote_send_key_(9, -1, false, now_rel);
        rt_.schedule_disp();
      } else if (wid >= WidgetId::REMOTE_SEC_PWR && wid < WidgetId::REMOTE_SEC_PWR + 4 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        TouchGestureEngine::do_remote_send_key_(9, hit->param, false, now_rel);
        rt_.schedule_disp();
      } else if (wid >= WidgetId::REMOTE_SLOT_CARD && wid < WidgetId::REMOTE_SLOT_CARD + 4 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.remote.active_slot = hit->param;
        rs.remote.action_feedback.clear();
        rs.nav.remote_subpage = 0;
        do_remote_select_slot(hit->param + 1);
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
    return true;
  }

  if (rs.nav.gui_page != PageId::TOOLS) return false;

  if (rs.nav.tools_subpage == ToolsSubpage::LAUNCHER && !rs.nav.menu_scroll_active) {
    const int total_pages = active_tools_launcher_page_count(persisted_prefs());
    if ((wid == WidgetId::CHEVRON_LEFT || wid == WidgetId::CHEVRON_RIGHT) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      if (total_pages > 1) {
        const int cur_pg = std::clamp(rs.nav.tools_menu_page, 0, total_pages - 1);
        rs.nav.tools_menu_page = cur_pg;
        const int target_pg =
            std::clamp(cur_pg + ((wid == WidgetId::CHEVRON_RIGHT) ? 1 : -1), 0, total_pages - 1);
        if (target_pg != cur_pg) {
          rs.nav.menu_scroll_active = true;
          rs.nav.menu_scroll_dir = (target_pg > cur_pg) ? 1 : -1;
          rs.nav.menu_scroll_target_page = target_pg;
          rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
        }
      }
    } else if (wid >= WidgetId::TOOLS_CARD_BASE && wid < WidgetId::TOOLS_CARD_BASE + 16 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int target_sub = hit->param;
      if (target_sub > 0) {
        rs.nav.tools_subpage = target_sub;
        if (target_sub == ToolsSubpage::FLASHLIGHT) rt_.cmd(UiHostCmd::FLASHLIGHT_FULL_BRIGHTNESS);
        else if (target_sub == ToolsSubpage::CALCULATOR) reset_calculator_state();
        rt_.apply_power_policy();
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
  } else if (rs.nav.tools_subpage == ToolsSubpage::FLASHLIGHT &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    rs.nav.tools_subpage = 0;
    rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
    if (!TouchGestureEngine::return_from_shortcut_origin_(rs, rt_, now_rel)) {
      mark_dirty_full();
      rt_.schedule_disp();
    }
  }
  return true;
}

}  // namespace latter_ui
