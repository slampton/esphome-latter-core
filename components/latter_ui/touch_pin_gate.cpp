// ==============================================================================
// Latter Security PIN Modal, v1: Session-Key PIN Wrapping & Perimeter Action Gate
// (components/latter_ui/touch_pin_gate.cpp)
// ==============================================================================

#include "touch_internal.h"

namespace latter_ui {

namespace touch_detail {

void do_execute_area_action() {
  auto &ar = runtime_state().area;
  latter_actions::emit_area_action(
      ar.target_entity_id, ar.target_action, ar.target_level, ar.active_area_id, ar.target_code);
  latter_rpc::secure_zero_string(ar.target_code);
  ar.target_code.clear();
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

void apply_optimistic_shortcut_feedback(const std::string &entity_id,
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

void activate_shortcut_item(const ShortcutItem &item, ShortcutOrigin origin,
                            DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                            uint32_t now_ms) {
  if (item.kind == ShortcutKind::ROUTE) {
    launch_shortcut_route(item.route_id, origin, rs, rt, now_ms);
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
  apply_optimistic_shortcut_feedback(item.target, item.action, item.label, rs, now_ms);
  rt.schedule_disp();
}

}  // namespace touch_detail

void TouchEngineAccess::scrub_pin_and_pending(TouchGestureEngine &eng, DeviceRuntimeState &rs,
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
      TouchEngineAccess::scrub_pin_and_pending(*this, rs, true);
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
        touch_detail::apply_optimistic_area_entity_dispatch(
            rs.area.target_entity_id, rs.area.target_action, rs);
        rs.area.feedback_timer = now_ms;
        touch_detail::do_execute_area_action();
      } else if (target == ui::PinPadTargetKind::ALARM_ACTION) {
        rs.area.target_code = pin_str;
        if (!ok_fb.empty()) rs.area.action_feedback = ok_fb;
        rs.area.feedback_timer = now_ms;
        touch_detail::do_execute_alarm_action();
      } else if (target == ui::PinPadTargetKind::SHORTCUT_ACTION) {
        if (!rs.nav.pending_shortcut_entity_id.empty() && !rs.nav.pending_shortcut_action.empty()) {
          latter_actions::emit_shortcut_action(
              rs.nav.pending_shortcut_entity_id, rs.nav.pending_shortcut_action, pin_str);
          touch_detail::apply_optimistic_shortcut_feedback(
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
  touch_detail::apply_optimistic_area_entity_dispatch(rs.area.target_entity_id, action, rs);
  rs.area.feedback_timer = now_ms;
  touch_detail::do_execute_area_action();
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
  touch_detail::do_execute_alarm_action();
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

}  // namespace latter_ui
