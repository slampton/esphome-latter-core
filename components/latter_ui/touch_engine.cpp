// ==============================================================================
// Latter Compiled On-Device Touch, Gesture, Rotary, PIN & Lifecycle Engine
// (components/latter_ui/touch_engine.cpp)
// ==============================================================================

#include "ui_touch_engine.h"
#include "ui_pages.h"
#include "ui_touch_recognizer.h"
#include "audio_router.h"
#include "hal_display_lifecycle.h"
#include "hal_panel_power.h"
#include "hal_sleep_pm.h"
#include "latter_actions.h"
#include "latter_prof.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace latter_ui {

namespace {

inline void update_light_inspector_hex(float hue_deg, float sat) {
  const UiColor rgb = hsv_to_rgb(hue_deg / 360.0f, sat, 1.0f);
  char hex_buf[12];
  std::snprintf(hex_buf, sizeof(hex_buf), "#%02X%02X%02X", rgb.r, rgb.g, rgb.b);
  area_color_hex_param() = hex_buf;
}

void do_execute_alarm_action() {
  auto &ar = runtime_state().area;
  latter_actions::emit_alarm_action(ar.alarm_service, ar.target_code);
  latter_rpc::secure_zero_string(ar.target_code);
  ar.target_code.clear();
}

void apply_optimistic_area_entity_dispatch(
    const std::string &entity_id, const std::string &action, DeviceRuntimeState &rs) {
  if (entity_id.empty()) return;
  auto &hub = home_hub_state();
  for (int i = 0; i < hub.entry_count; ++i) {
    auto &item = hub.entries[i];
    if (item.entity_id != entity_id) continue;
    if (item.is_lock) {
      if (action == "lock") {
        item.lock_state = LockState::LOCKED;
        ++hub.local_mutation_seq;
      } else if (action == "unlock") {
        item.lock_state = LockState::UNLOCKED;
        ++hub.local_mutation_seq;
      }
    } else {
      if (action == "open_cover") {
        item.garage_state = GarageState::OPEN;
        ++hub.local_mutation_seq;
      } else if (action == "close_cover") {
        item.garage_state = GarageState::CLOSED;
        ++hub.local_mutation_seq;
      }
    }
  }
  auto *slots = rs.area_entity_slots();
  for (size_t i = 0; i < LATTER_CARD_SLOT_COUNT; ++i) {
    if (slots[i].id != entity_id) continue;
    if (action == "turn_on" || action == "open_cover" || action == "unlock") {
      slots[i].active = true;
    } else if (action == "turn_off" || action == "close_cover" || action == "lock") {
      slots[i].active = false;
    }
  }
}

}  // namespace

void TouchGestureEngine::do_execute_area_action_() {
  auto &ar = runtime_state().area;
  latter_actions::emit_area_action(
      ar.target_entity_id, ar.target_action, ar.target_level, ar.active_area_id, ar.target_code);
  latter_rpc::secure_zero_string(ar.target_code);
  ar.target_code.clear();
}

void TouchGestureEngine::apply_optimistic_shortcut_feedback_(const std::string &entity_id,
                                        const std::string &action,
                                        const std::string &label,
                                        DeviceRuntimeState &rs,
                                        uint32_t now_ms) {
  rs.nav.pinned_feedback = label.empty() ? entity_id : label;
  rs.nav.pinned_feedback_ms = (now_ms == 0U) ? 1U : now_ms;
  auto flip_list = [&](std::vector<ShortcutItem> &vec) {
    for (auto &it : vec) {
      if (it.kind != ShortcutKind::ENTITY || it.target != entity_id) continue;
      if (it.state == 0 || it.state == 1) {
        if (action == "toggle" || action == "media_play_pause") {
          it.state = (it.state == 1) ? 0 : 1;
        } else if (action == "turn_on" || action == "open_cover" || action == "open_valve" ||
                   action == "unlock" || action == "open" || action == "start") {
          it.state = 1;
        } else if (action == "turn_off" || action == "close_cover" || action == "close_valve" ||
                   action == "lock" || action == "stop" || action == "return_to_base") {
          it.state = 0;
        }
      }
    }
  };
  flip_list(shortcuts_state().dock_items);
  flip_list(shortcuts_state().pinned_items);
}

void TouchGestureEngine::activate_shortcut_item_(const ShortcutItem &item, ShortcutOrigin origin,
                            DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                            uint32_t now_ms) {
  if (item.kind == ShortcutKind::ROUTE) {
    TouchGestureEngine::launch_shortcut_route_(item.route_id, origin, rs, rt, now_ms);
    return;
  }

  if (item.target.empty() || item.action.empty()) return;
  if (item.state == 2) {
    rs.nav.pinned_feedback = "Unavailable: " + item.label;
    rs.nav.pinned_feedback_ms = (now_ms == 0U) ? 1U : now_ms;
    rt.schedule_disp();
    return;
  }

  const std::string &pol = area_ctx_state().security_pin_policy;
  if (ui::is_perimeter_action_blocked(pol, item.target, item.action, 100)) {
    rs.nav.pinned_feedback = "Blocked by Security Policy";
    rs.nav.pinned_feedback_ms = (now_ms == 0U) ? 1U : now_ms;
    rt.schedule_disp();
    return;
  }
  if (ui::requires_perimeter_pin_prompt(pol, item.target, item.action, 100, effective_alarm_caps())) {
    rs.nav.pending_shortcut_entity_id = item.target;
    rs.nav.pending_shortcut_action = item.action;
    rs.nav.pending_shortcut_label = item.label;
    ui::pin_pad_open(ui::PinPadTargetKind::SHORTCUT_ACTION, "SECURITY PIN");
    ui::pin_pad_state().last_activity_ms = (now_ms == 0U) ? 1U : now_ms;
    mark_dirty_full();
    rt.schedule_disp();
    return;
  }

  if (item.confirm) {
    auto &sc = shortcut_confirm_state();
    sc.open = true;
    sc.opened_ms = (now_ms == 0U) ? 1U : now_ms;
    sc.entity_id = item.target;
    sc.action = item.action;
    build_shortcut_confirm_copy(item, sc.title, sc.line1, sc.line2, sc.verb_btn, sc.is_danger);
    mark_dirty_full();
    rt.schedule_disp();
    return;
  }

  latter_actions::emit_shortcut_action(item.target, item.action);
  TouchGestureEngine::apply_optimistic_shortcut_feedback_(item.target, item.action, item.label, rs, now_ms);
  rt.schedule_disp();
}

void TouchGestureEngine::scrub_pin_and_pending_(TouchGestureEngine &eng, DeviceRuntimeState &rs,
                                              bool clear_feedback_and_shortcut) {
  ui::pin_pad_scrub();
  latter_rpc::secure_zero_string(rs.area.target_code);
  rs.area.target_code.clear();
  eng.pending_pin_feedback_.clear();
  eng.pending_doorbell_unlock_ = false;
  if (clear_feedback_and_shortcut) {
    rs.area.action_feedback.clear();
    rs.nav.pending_shortcut_entity_id.clear();
    rs.nav.pending_shortcut_action.clear();
    rs.nav.pending_shortcut_label.clear();
  }
}

bool TouchGestureEngine::handle_pin_pad_tap(int tx, int ty, uint32_t now_ms) {
  auto &st = ui::pin_pad_state();
  if (!st.open) return false;

  auto &rs = runtime_state();
  const HitRegion *hit = frame_hits().hit_test(tx, ty, kHitTap);
  if (hit != nullptr && (hit->widget_id == WidgetId::PIN_PAD_CANCEL ||
                         hit->widget_id == WidgetId::BACK_BUTTON)) {
    if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      TouchGestureEngine::scrub_pin_and_pending_(*this, rs, true);
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
      st.last_activity_ms = now_ms;
      rt_.schedule_disp();
    }
    return true;
  }
  if (hit->widget_id == WidgetId::PIN_PAD_BACKSPACE) {
    if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID)) {
      ui::pin_pad_backspace();
      st.last_activity_ms = now_ms;
      rt_.schedule_disp();
    }
    return true;
  }
  if (hit->widget_id == WidgetId::PIN_PAD_SUBMIT) {
    if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON)) {
      if (st.len < ui::PinPadModalState::kMinDigits || st.len > ui::PinPadModalState::kMaxDigits) {
        std::snprintf(st.error_msg, sizeof(st.error_msg), "Enter 4-12 digits");
        st.error_ms = now_ms;
        st.last_activity_ms = now_ms;
        rt_.schedule_disp();
        return true;
      }
      std::string pin_str(st.digits, st.len);
      const auto target = st.target;
      const std::string ok_fb = std::move(pending_pin_feedback_);
      const bool db_unlock = pending_doorbell_unlock_;
      pending_pin_feedback_.clear();
      pending_doorbell_unlock_ = false;
      ui::pin_pad_scrub();
      if (target == ui::PinPadTargetKind::AREA_ACTION) {
        rs.area.target_code = pin_str;
        if (!ok_fb.empty()) rs.area.action_feedback = ok_fb;
        if (db_unlock) {
          cam_intercom_status() = "UNLOCKING...";
          rs.intercom.cam_status_ms = now_ms;
        }
        apply_optimistic_area_entity_dispatch(
            rs.area.target_entity_id, rs.area.target_action, rs);
        rs.area.feedback_timer = now_ms;
        TouchGestureEngine::do_execute_area_action_();
      } else if (target == ui::PinPadTargetKind::ALARM_ACTION) {
        rs.area.target_code = pin_str;
        if (!ok_fb.empty()) rs.area.action_feedback = ok_fb;
        rs.area.feedback_timer = now_ms;
        do_execute_alarm_action();
      } else if (target == ui::PinPadTargetKind::SHORTCUT_ACTION) {
        if (!rs.nav.pending_shortcut_entity_id.empty() && !rs.nav.pending_shortcut_action.empty()) {
          latter_actions::emit_shortcut_action(
              rs.nav.pending_shortcut_entity_id, rs.nav.pending_shortcut_action, pin_str);
          TouchGestureEngine::apply_optimistic_shortcut_feedback_(
              rs.nav.pending_shortcut_entity_id, rs.nav.pending_shortcut_action,
              rs.nav.pending_shortcut_label, rs, now_ms);
        }
        rs.nav.pending_shortcut_entity_id.clear();
        rs.nav.pending_shortcut_action.clear();
        rs.nav.pending_shortcut_label.clear();
      }
      latter_rpc::secure_zero_string(pin_str);
      pin_str.clear();
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return true;
  }
  return true;
}

bool TouchGestureEngine::gate_or_dispatch_area_action(const std::string &domain_or_entity,
                                                      const std::string &action, int level,
                                                      const char *pin_prompt, uint32_t now_ms,
                                                      std::string_view success_feedback) {
  auto &rs = runtime_state();
  const std::string &pol = area_ctx_state().security_pin_policy;
  if (ui::is_perimeter_action_blocked(pol, domain_or_entity, action, level)) {
    pending_pin_feedback_.clear();
    pending_doorbell_unlock_ = false;
    rs.area.action_feedback = "Blocked by Security Policy";
    rs.area.feedback_timer = now_ms;
    rt_.schedule_disp();
    return false;
  }
  rs.area.target_action = action;
  rs.area.target_level = level;
  if (ui::requires_perimeter_pin_prompt(pol, domain_or_entity, action, level)) {
    pending_pin_feedback_ = std::string(success_feedback);
    rs.area.action_feedback.clear();
    ui::pin_pad_open(ui::PinPadTargetKind::AREA_ACTION, pin_prompt);
    ui::pin_pad_state().last_activity_ms = (now_ms == 0U) ? 1U : now_ms;
    mark_dirty_full();
    rt_.schedule_disp();
    return false;
  }
  pending_pin_feedback_.clear();
  if (!success_feedback.empty()) {
    rs.area.action_feedback = std::string(success_feedback);
  }
  if (pending_doorbell_unlock_) {
    cam_intercom_status() = "UNLOCKING...";
    rs.intercom.cam_status_ms = now_ms;
    pending_doorbell_unlock_ = false;
  }
  apply_optimistic_area_entity_dispatch(rs.area.target_entity_id, action, rs);
  rs.area.feedback_timer = now_ms;
  TouchGestureEngine::do_execute_area_action_();
  rt_.schedule_disp();
  return true;
}

bool TouchGestureEngine::gate_or_dispatch_alarm_action(const std::string &action, uint32_t now_ms,
                                                       std::string_view success_feedback) {
  auto &rs = runtime_state();
  const std::string &pol = area_ctx_state().security_pin_policy;
  if (ui::is_perimeter_action_blocked(pol, "home_hub.alarm", action, 100)) {
    pending_pin_feedback_.clear();
    rs.area.action_feedback = "Blocked by Security Policy";
    rs.area.feedback_timer = now_ms;
    rt_.schedule_disp();
    return false;
  }
  rs.area.alarm_service = action;
  if (ui::requires_perimeter_pin_prompt(pol, "home_hub.alarm", action, 100)) {
    pending_pin_feedback_ = std::string(success_feedback);
    rs.area.action_feedback.clear();
    ui::pin_pad_open(ui::PinPadTargetKind::ALARM_ACTION,
                     (action == "disarm") ? "PIN TO DISARM" : "PIN TO ARM");
    ui::pin_pad_state().last_activity_ms = (now_ms == 0U) ? 1U : now_ms;
    mark_dirty_full();
    rt_.schedule_disp();
    return false;
  }
  pending_pin_feedback_.clear();
  if (!success_feedback.empty()) {
    rs.area.action_feedback = std::string(success_feedback);
  }
  rs.area.feedback_timer = now_ms;
  mark_dirty_full();
  do_execute_alarm_action();
  rt_.schedule_disp();
  return true;
}

bool TouchGestureEngine::gate_or_dispatch_doorbell_unlock(uint32_t now_ms) {
  auto &rs = runtime_state();
  const auto &ac = area_ctx_state();
  CameraRecord cams[MAX_CAMERA_SLOTS]{};
  const int cam_cnt = parse_cameras_records(ac.cameras_summary, cams, MAX_CAMERA_SLOTS);
  const std::string active_ent =
      (rs.alert.active && !rs.alert.camera_entity.empty())
          ? rs.alert.camera_entity
          : resolve_active_carousel_camera_id(ac.cameras_summary, ac.camera_snapshot_url);
  const int cam_idx = find_camera_index_by_entity(cams, cam_cnt, active_ent);
  const CameraRecord *cam = (cam_idx >= 0) ? &cams[cam_idx] : nullptr;
  if (cam == nullptr || !cam->has_unlock()) {
    return false;
  }
  rs.area.target_entity_id = cam->unlock_entity;
  if (!cam->unlock_is_lock()) {
    pending_doorbell_unlock_ = false;
    return gate_or_dispatch_area_action(
        cam->unlock_entity, "open_cover", 100, "GARAGE DOOR PIN", now_ms, "Opening Garage...");
  }
  pending_doorbell_unlock_ = true;
  return gate_or_dispatch_area_action(
      cam->unlock_entity, "unlock", 100, "UNLOCK DOOR PIN", now_ms, "Unlocking...");
}



TouchGestureEngine &touch_engine() {
  static TouchGestureEngine s_engine;
  return s_engine;
}

void TouchGestureEngine::on_touch_down(int tx, int ty, uint32_t now_touch) {
  cancel_core_timeout(kTimeoutTouchLongPressId);
  if (!TouchGestureEngine::allow_active_touch_down_(*this, tx, ty, now_touch)) {
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

  if (TouchGestureEngine::handle_down_takeovers_(*this, touch.x, touch.y, now_touch)) {
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
          if (TouchGestureEngine::return_from_shortcut_origin_(rs, rt_, now_touch)) return;
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
      (void)TouchGestureEngine::resolve_home_activities_(now_touch, rs, prefs, rt_, act_count);
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
          (void)TouchGestureEngine::handle_music_back_step_(rs, rt_, now_touch);
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
        (void)TouchGestureEngine::handle_music_back_step_(rs, rt_, now_touch);
      }
      return;
    }
    if (hit != nullptr && (hit->flags & kHitLongPress) != 0U) {
      gest.arm_long_press(wid, hit->param);
      schedule_core_timeout(kTimeoutTouchLongPressId, kinematics::kLongPressHoldMs, []() {
        touch_engine().tick(TouchGestureEngine::now_or_hal_ms_(last_user_touch_ms()));
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
        if (in_area_picker && TouchGestureEngine::return_from_shortcut_origin_(rs, rt_, now_touch)) return;
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
          TouchGestureEngine::do_fetch_area_list_browse_();
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
          TouchGestureEngine::do_execute_area_action_();
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
          update_light_inspector_hex(hue, sat);
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

  if (TouchGestureEngine::handle_down_cameras_games_(*this, touch.x, touch.y, now_touch, wid) ||
      TouchGestureEngine::handle_down_system_tools_(*this, touch.x, touch.y, now_touch, hit, wid, tkey) ||
      TouchGestureEngine::handle_down_time_cal_(*this, touch.x, touch.y, now_touch, hit, wid, tkey)) {
    return;
  }
}

void TouchGestureEngine::on_touch_up(uint32_t now_rel) {
  game_input().release_held();
  snake_drag_active_ = false;
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
    TouchGestureEngine::handle_quick_actions_release_(*this, now_rel, start_slider);
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
        latter_actions::emit_media_volume(TouchGestureEngine::resolve_music_speaker_entity_(rs), "set", next_pct);
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
        TouchGestureEngine::do_execute_area_action_();
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
        TouchGestureEngine::do_execute_area_action_();
        gest.tap_cancelled = true;
        rt_.schedule_disp();
      } else if (start_slider->widget_id == WidgetId::SYS_BRI_SLIDER &&
                 rs.nav.gui_page == PageId::SYSTEM && rs.nav.system_menu_mode == 2) {
        if (gest.axis_lock == GestureAxisLock::Slider) {
          UiNumberId nid = UiNumberId::MANUAL_BRIGHTNESS;
          if (TouchGestureEngine::resolve_sys_bri_slider_id_(
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
        update_light_inspector_hex(hue, sat);
      }
      if (picked || wheel_drag_dirty_) {
        wheel_drag_dirty_ = false;
        gest.tap_cancelled = true;
        rs.area.target_entity_id = light_insp_entity_id();
        rs.area.target_action = "set_color";
        rs.area.target_level = light_insp_bri_pct();
        rs.area.feedback_timer = now_rel;
        TouchGestureEngine::do_execute_area_action_();
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
        TouchGestureEngine::fetch_music_browse_for_current_view_(rs, now_ms);
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

  // 4. Page 3: Games Carousel & Blocks Board Swipes
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
    if (rs.nav.active_game == 5 &&
        game_input().held_cmd == GameInputCmd::NONE) {
      const HitRegion *start_hit = frame_hits().hit_test(gest.start_x, gest.start_y, kHitTap);
      const bool on_board = (start_hit == nullptr || start_hit->widget_id == WidgetId::GAME_BOARD_TAP);
      const int thr = is_release ? ly.flick_min() : ly.swipe_min();
      if (on_board && (std::abs(dx) >= thr || dy >= thr)) {
        if (std::abs(dx) > std::abs(dy) && std::abs(dx) >= thr) {
          commit_swipe();
          game_input().push((dx > 0) ? GameInputCmd::RIGHT : GameInputCmd::LEFT);
          rt_.schedule_disp();
          return true;
        }
        if (dy > 0 && dy > std::abs(dx) && dy >= thr) {
          commit_swipe();
          game_input().push(GameInputCmd::HARD_DROP);
          rt_.schedule_disp();
          return true;
        }
      }
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
                 ? step_area_chan(latter_rpc::SlotChannel::AREA_LIST, &TouchGestureEngine::do_fetch_area_list_browse_)
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
          TouchGestureEngine::do_fetch_area_browse_();
          mark_dirty_full();
          rt_.schedule_disp();
        }
        return true;
      }
      if (v_dir != 0 && gest.start_y < ly.mode_bar_zone_top()) {
        return step_area_chan(latter_rpc::SlotChannel::AREA_ENTITIES, &TouchGestureEngine::do_fetch_area_browse_);
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
        TouchGestureEngine::enter_home_hub_tab_(hh_tabs[step_wrap(cur_slot, h_dir, hh_cnt)], false, rs, rt_, now_ms);
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
      const int32_t today_ymd = TouchGestureEngine::resolve_touch_today_ymd_(rt_);
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

  if (rs.nav.gui_page == PageId::GAMES && rs.nav.active_game > 0) {
    if (rs.nav.active_game >= 1 && rs.nav.active_game <= 4) {
      return;
    }
    if (rs.nav.active_game == 6) {
      if (snake_drag_active_) {
        const int sdx = tx - snake_drag_x_;
        const int sdy = ty - snake_drag_y_;
        const int thr = layout().flick_min();
        if (std::abs(sdx) >= thr || std::abs(sdy) >= thr) {
          if (std::abs(sdx) >= std::abs(sdy)) {
            game_input().push((sdx > 0) ? GameInputCmd::RIGHT : GameInputCmd::LEFT);
          } else {
            game_input().push((sdy > 0) ? GameInputCmd::DOWN : GameInputCmd::UP);
          }
          snake_drag_x_ = tx;
          snake_drag_y_ = ty;
          gest.tap_cancelled = true;
          rt_.schedule_disp();
        }
      }
      return;
    }
    if (rs.nav.active_game == 7) {
      return;
    }
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
        drag_bri_num(TouchGestureEngine::brightness_number_id_for_kind_(bri.kind), qa_sl);
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
        latter_actions::emit_media_volume(TouchGestureEngine::resolve_music_speaker_entity_(rs), "set", next_pct);
      }
      if (now_u - last_disp_update_ms() >= 42U) rt_.schedule_disp();
    }
    return;
  }

  if (rs.nav.gui_page == PageId::MUSIC &&
      rs.music.drilldown == MusicDrilldown::NONE &&
      rs.music.takeover == MusicTakeover::NONE &&
      (rs.music.view == MusicView::LYRICS || rs.music.view == MusicView::INFO) &&
      (!rs.music.hud_active ||
       gest.start_y < ((rs.music.view == MusicView::LYRICS)
                           ? compute_music_lyrics_transport_geom(
                                 SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0,
                                 LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI)
                                 .vp_bot
                           : ly.mode_bar_zone_top())) &&
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
      if (TouchGestureEngine::resolve_sys_bri_slider_id_(
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
      update_light_inspector_hex(hue, sat);
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
    if (TouchGestureEngine::open_music_row_action_sheet_(gest.long_press_param, rs, now_ms)) {
      mark_dirty_full();
      rt_.schedule_disp();
      return true;
    }
  }

  if (rs.nav.gui_page == PageId::REMOTE && rs.nav.remote_subpage == 0) {
    const int remote_key = TouchGestureEngine::remote_key_for_widget_(gest.long_press_wid);
    if (remote_key >= 0) {
      const int act_slot = resolve_active_remote_slot(rs.remote.active_slot);
      rs.remote.active_slot = act_slot;
      const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, gest.long_press_wid, gest.long_press_param);
      if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        TouchGestureEngine::do_remote_send_key_(remote_key, act_slot, true, now_ms);
        rt_.schedule_disp();
        return true;
      }
    }
  }
  rt_.schedule_disp();
  return true;
}



void TouchGestureEngine::on_input_event(const std::string &action, int delta, uint32_t now_in) {
  auto &rs = runtime_state();

  if (rs.nav.ota_in_progress) return;

  if (rs.showcase.reel_active) {
    const bool was_dark = (rs.nav.display_power_state != 0 || !rt_.panel_is_on);
    (void)clear_showcase(now_in);
    rt_.cmd(UiHostCmd::APPLY_THEME_PRESET);
    rt_.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
    rt_.apply_power_policy();
    if (was_dark) {
      note_wake_reason(WakeReason::TOUCH);
      rt_.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    } else {
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return;
  }

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
  if (rs.showcase.has_va_preview(now_in)) {
    rs.showcase.va_stage = 0U;
    rt_.apply_power_policy();
    mark_dirty_full();
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
      TouchGestureEngine::scrub_pin_and_pending_(*this, rs, true);
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
        TouchGestureEngine::handle_music_back_step_(rs, rt_, now_in)) {
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



bool TouchGestureEngine::allow_active_touch_down_(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms) {
  auto &rs = runtime_state();
  auto &rt = eng.rt_;

  if (rs.nav.ota_in_progress) {
    return false;
  }

  if (rs.nav.is_hibernating) {
    if (rs.nav.hibernate_countdown_val > 0) {
      rs.nav.is_hibernating = false;
      rs.nav.hibernate_countdown_val = 0;
      note_wake_reason(WakeReason::TOUCH);
      rt.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    }
    return false;
  }

  // Standalone Showcase Reel: first physical touch immediately exits the reel and clears
  // volatile showcase overrides while keeping the user on the current screen (D-077).
  if (rs.showcase.reel_active) {
    const bool was_dark = (rs.nav.display_power_state != 0 || !rt.panel_is_on);
    (void)eng.clear_showcase(now_ms);
    rt.cmd(UiHostCmd::APPLY_THEME_PRESET);
    rt.cmd(UiHostCmd::SYNC_DISPLAY_BRIGHTNESS);
    rt.apply_power_policy();
    if (was_dark) {
      note_wake_reason(WakeReason::TOUCH);
      rt.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    } else {
      mark_dirty_full();
      rt.schedule_disp();
    }
    return false;
  }

  if (rs.nav.display_power_state != 0 || !rt.panel_is_on) {
    const std::string_view tw_mode = rt.select_opt(UiSelectId::TAP_WAKE_MODE);
    if (!accept_screen_off_tap(tw_mode, rt.usb_connected, now_ms)) return false;
    note_wake_reason(WakeReason::TOUCH);
    rt.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    return false;
  }

  if (now_ms - last_screen_wake_ms() < TOUCH_DEBOUNCE_WAKE) {
    if (latter_ui::profiler_enabled()) {
      LATTER_PROF_LOGI("[PROF:TOUCH_BLOCKED] x=%d y=%d page=%d reason=WAKE_DEBOUNCE",
                       tx, ty, rs.nav.gui_page);
    }
    return false;
  }

  if (now_ms - eng.last_touch_activity_ms_ > 1000U) {
    eng.last_touch_activity_ms_ = now_ms;
    display_lifecycle().note_activity(now_ms);
  }
  return true;
}

void TouchGestureEngine::tick(uint32_t now_ms) {
  auto &rs = runtime_state();
  auto &pp = ui::pin_pad_state();
  if (pp.open && pp.last_activity_ms > 0U &&
      static_cast<uint32_t>(now_ms - pp.last_activity_ms) >= ui::PinPadModalState::kAutoDismissMs) {
    TouchGestureEngine::scrub_pin_and_pending_(*this, rs, true);
    mark_dirty_full();
    rt_.schedule_disp();
  }
  auto &sc = shortcut_confirm_state();
  if (sc.open && sc.opened_ms > 0U &&
      static_cast<uint32_t>(now_ms - sc.opened_ms) >= ShortcutConfirmState::kAutoDismissMs) {
    sc.clear();
    mark_dirty_full();
    rt_.schedule_disp();
  }
  if (rs.nav.gui_page == PageId::MUSIC && !touch_gesture().active &&
      rs.music.takeover_interacted_ms > 0U) {
    const uint32_t idle_ms = (now_ms >= rs.music.takeover_interacted_ms)
                                 ? (now_ms - rs.music.takeover_interacted_ms)
                                 : 0U;
    if (rs.music.takeover == MusicTakeover::SCRUB && idle_ms >= 5000U) {
      rs.music.takeover = MusicTakeover::NONE;
      rs.music.hud_active = true;
      rs.music.hud_timer = now_ms;
      mark_dirty_full();
      rt_.schedule_disp();
    }
  }
  (void)try_commit_long_press_(now_ms);
}

std::string_view UiTouchRuntime::select_opt(UiSelectId sid) const noexcept {
  using S = UiSelectId;
  const auto &p = persisted_prefs();
  switch (sid) {
    case S::TAP_WAKE_MODE: return pref_tap_to_wake_cstr(p);
    case S::WEATHER_SIM_CONDITION: return pref_weather_sim_cstr();
    case S::MUSIC_OVERLAY_STYLE: return pref_music_overlay_style_cstr(p);
    case S::MUSIC_AUTO_OPEN: return pref_music_auto_open_cstr(p);
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
    case S::PICKUP_WAKE_MODE: return pref_pickup_to_wake_cstr(p);
    case S::DISPLAY_OFF_TIMEOUT: return pref_battery_screen_timeout_cstr(p);
    case S::SCREENSAVER_MODE: return pref_screensaver_mode_cstr(p);
    case S::SCREENSAVER_STYLE: return pref_screensaver_style_cstr(p);
    case S::SCREENSAVER_DOCKED_TIMEOUT: return pref_docked_idle_timeout_cstr(p);
    case S::HIBERNATE_TIMEOUT_MODE: return pref_battery_hibernate_timeout_cstr(p);
    case S::DEFAULT_START_APP: return pref_start_screen_cstr(p);
    case S::CLOCK_FACE_STYLE: return pref_clock_face_cstr(p);
    case S::TIME_FORMAT: return pref_time_format_cstr(p);
    case S::HOME_CLOCK_TAP_ACTION: return pref_home_clock_tap_cstr(p);
    case S::HOME_DATE_TAP_ACTION: return pref_home_date_tap_cstr(p);
    case S::AUTO_BRIGHTNESS_MODE: return pref_auto_brightness_mode_cstr(p);
    case S::BEDTIME_DISPLAY: return pref_bedtime_display_cstr(p);
    case S::CAMERA_DEFAULT_VIEW: return pref_camera_default_view_cstr(p);
    case S::CAMERA_CAROUSEL_INTERVAL: return pref_camera_carousel_interval_cstr(p);
    case S::CAMERA_FRAMING: return pref_camera_framing_cstr(p);
    case S::CAMERA_STREAM_MODE: return pref_camera_stream_mode_cstr(p);
    case S::CAMERA_DOORBELL_POPUP: return pref_camera_doorbell_popup_cstr(p);
  }
  return {};
}

void UiTouchRuntime::set_select_opt(UiSelectId sid, std::string_view opt, bool /*pub*/) const {
  using S = UiSelectId;
  using K = PrefKey;
  const uint32_t now_ms = TouchGestureEngine::now_or_hal_ms_(last_user_touch_ms());
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
    case S::MUSIC_AUTO_OPEN: pk = K::MUSIC_AUTO_OPEN; break;
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
    case S::PICKUP_WAKE_MODE: pk = K::PICKUP_TO_WAKE; break;
    case S::DISPLAY_OFF_TIMEOUT: pk = K::BATTERY_SCREEN_TIMEOUT; break;
    case S::SCREENSAVER_MODE: pk = K::SCREENSAVER_MODE; break;
    case S::SCREENSAVER_STYLE: pk = K::SCREENSAVER_STYLE; break;
    case S::SCREENSAVER_DOCKED_TIMEOUT: pk = K::DOCKED_IDLE_TIMEOUT; break;
    case S::HIBERNATE_TIMEOUT_MODE: pk = K::BATTERY_HIBERNATE_TIMEOUT; break;
    case S::DEFAULT_START_APP: pk = K::START_SCREEN; break;
    case S::CLOCK_FACE_STYLE: pk = K::CLOCK_FACE; break;
    case S::TIME_FORMAT: pk = K::TIME_FORMAT; break;
    case S::HOME_CLOCK_TAP_ACTION: pk = K::HOME_CLOCK_TAP_ACTION; break;
    case S::HOME_DATE_TAP_ACTION: pk = K::HOME_DATE_TAP_ACTION; break;
    case S::AUTO_BRIGHTNESS_MODE: pk = K::AUTO_BRIGHTNESS_MODE; break;
    case S::BEDTIME_DISPLAY: pk = K::BEDTIME_DISPLAY; break;
    case S::CAMERA_DEFAULT_VIEW: pk = K::CAMERA_DEFAULT_VIEW; break;
    case S::CAMERA_CAROUSEL_INTERVAL: pk = K::CAMERA_CAROUSEL_INTERVAL; break;
    case S::CAMERA_FRAMING: pk = K::CAMERA_FRAMING; break;
    case S::CAMERA_STREAM_MODE: pk = K::CAMERA_STREAM_MODE; break;
    case S::CAMERA_DOORBELL_POPUP: pk = K::CAMERA_DOORBELL_POPUP; break;
  }
  if (pk == K::COUNT) return;
  uint32_t eff = 0U;
  if (latter_actions::record_local_pref_edit(pk, opt, now_ms, eff)) {
    device_prefs_state().last_effects |= eff;
    cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
  }
}

bool UiTouchRuntime::sw_state(UiSwitchId swid) const noexcept {
  using W = UiSwitchId;
  const auto &p = persisted_prefs();
  switch (swid) {
    case W::HOME_WEATHER_BG: return p.weather_background;
    case W::WEATHER_APP_BG: return p.weather_app_background;
    case W::HOME_WAKE_GREETING: return p.wake_greeting;
    case W::BLE_AREA_BEACON: return p.ble_beacon_enabled;
    case W::GAME_SOUND: return p.game_sound_effects;
    case W::VA_WAKE_SCREEN: return p.wake_screen_on_assist;
    case W::SCREENSAVER_PHOTO_CLOCK: return p.screensaver_photo_clock;
    case W::BEDTIME_MODE: return p.bedtime_active;
    case W::DO_NOT_DISTURB: return p.do_not_disturb;
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
    case W::WEATHER_APP_BG: pk = K::WEATHER_APP_BACKGROUND; next_val = !p.weather_app_background; break;
    case W::HOME_WAKE_GREETING: pk = K::WAKE_GREETING; next_val = !p.wake_greeting; break;
    case W::BLE_AREA_BEACON: pk = K::BLE_BEACON_ENABLED; next_val = !p.ble_beacon_enabled; break;
    case W::GAME_SOUND: pk = K::GAME_SOUND_EFFECTS; next_val = !p.game_sound_effects; break;
    case W::VA_WAKE_SCREEN: pk = K::WAKE_SCREEN_ON_ASSIST; next_val = !p.wake_screen_on_assist; break;
    case W::SCREENSAVER_PHOTO_CLOCK: pk = K::SCREENSAVER_PHOTO_CLOCK; next_val = !p.screensaver_photo_clock; break;
    case W::BEDTIME_MODE: pk = K::BEDTIME_ACTIVE; next_val = !p.bedtime_active; break;
    case W::DO_NOT_DISTURB: pk = K::DO_NOT_DISTURB; next_val = !p.do_not_disturb; break;
  }
  if (pk == K::COUNT) return;
  const uint32_t now_ms = TouchGestureEngine::now_or_hal_ms_(last_user_touch_ms());
  uint32_t eff = 0U;
  if (latter_actions::record_local_pref_edit(pk, next_val ? "1" : "0", now_ms, eff)) {
    device_prefs_state().last_effects |= eff;
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
    case N::MANUAL_BRIGHTNESS: return static_cast<float>(p.manual_brightness_pct());
    case N::DAY_BRIGHTNESS: return static_cast<float>(p.day_brightness_pct());
    case N::NIGHT_BRIGHTNESS: return static_cast<float>(p.night_brightness_pct());
    case N::BEDTIME_BRIGHTNESS: return static_cast<float>(p.bedtime_brightness_pct());
    case N::DAY_START_HOUR: return static_cast<float>(p.day_start_hour);
    case N::NIGHT_START_HOUR: return static_cast<float>(p.night_start_hour);
  }
  return 0.0f;
}

void UiTouchRuntime::set_num_val(UiNumberId nid, float val) const {
  using N = UiNumberId;
  using K = PrefKey;
  const uint32_t now_ms = TouchGestureEngine::now_or_hal_ms_(last_user_touch_ms());
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
    case N::MANUAL_BRIGHTNESS: pk = K::MANUAL_BRIGHTNESS; cl_val = std::clamp(cl_val, 1, 100); break;
    case N::DAY_BRIGHTNESS: pk = K::DAY_BRIGHTNESS; cl_val = std::clamp(cl_val, 1, 100); break;
    case N::NIGHT_BRIGHTNESS: pk = K::NIGHT_BRIGHTNESS; cl_val = std::clamp(cl_val, 1, 100); break;
    case N::BEDTIME_BRIGHTNESS: pk = K::BEDTIME_BRIGHTNESS; cl_val = std::clamp(cl_val, 1, 100); break;
    case N::DAY_START_HOUR: pk = K::DAY_START_HOUR; cl_val = std::clamp(cl_val, 0, 23); break;
    case N::NIGHT_START_HOUR: pk = K::NIGHT_START_HOUR; cl_val = std::clamp(cl_val, 0, 23); break;
  }
  if (pk == K::COUNT) return;
  char buf[16];
  const int n = std::snprintf(buf, sizeof(buf), "%d", cl_val);
  if (n <= 0) return;
  uint32_t eff = 0U;
  if (latter_actions::record_local_pref_edit(pk, std::string_view(buf, static_cast<size_t>(n)), now_ms, eff)) {
    device_prefs_state().last_effects |= eff;
    cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
  }
}

}  // namespace latter_ui
