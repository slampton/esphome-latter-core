// ==============================================================================
// Latter Compiled On-Device Time & Calendar Touch Dispatch
// (components/latter_ui/touch_dispatch_time_cal.cpp)
// ==============================================================================

#include "touch_dispatch_internal.h"
#include "ui_pages.h"
#include "ui_touch_recognizer.h"
#include "latter_actions.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace latter_ui {
using namespace latter_core;
using namespace touch_detail;

namespace {

inline CalendarSourceMeta *touch_cal_sources_scratch() {
  static_assert(std::is_trivially_copyable_v<CalendarSourceMeta>,
                "CalendarSourceMeta must stay POD for evictable PSRAM scratch");
  static CalendarSourceMeta *s_buf = nullptr;
  static const bool s_touch_cal_evictor_reg = register_psram_cache_evictor([]() noexcept {
    if (s_buf != nullptr) {
      ui_psram_free(s_buf);
      s_buf = nullptr;
    }
  });
  (void)s_touch_cal_evictor_reg;
  if (s_buf == nullptr) {
    s_buf = static_cast<CalendarSourceMeta *>(
        ui_psram_calloc(static_cast<size_t>(kMaxCalendarSources), sizeof(CalendarSourceMeta)));
  }
  return s_buf;
}

inline void open_calendar_composer(DeviceRuntimeState &rs,
                                   const UiTouchRuntime &rt,
                                   int32_t today_ymd,
                                   uint32_t now_ms) {
  stop_calendar_dictation_if_active(rs, rt, now_ms);
  rs.cal.drilldown = CalDrilldown::ADD_STEP1;
  rs.cal.delete_confirm_open = false;
  rs.cal.picker_page = 0;
  rs.cal.draft_ymd = (rs.cal.subpage == CalendarSubpage::OVERVIEW || rs.cal.selected_ymd <= 0)
                         ? today_ymd
                         : rs.cal.selected_ymd;
  if (rs.cal.draft_title.empty()) {
    rs.cal.draft_title = "Reminder";
  }
  const int next_h = (rt.time_valid && rt.time_hour >= 0) ? std::clamp(rt.time_hour + 1, 8, 22) : 9;
  rs.cal.draft_start_min = static_cast<int16_t>(next_h * 60);
  rs.cal.draft_end_min = static_cast<int16_t>(std::min(1425, (next_h + 1) * 60));
  rs.cal.draft_time_field = 0U;
}

inline void submit_calendar_draft_event(DeviceRuntimeState &rs,
                                        const PersistedDevicePrefs &prefs,
                                        uint32_t now_ms) {
  CalendarSourceMeta *writable_sources = touch_cal_sources_scratch();
  const int writable_cnt = (writable_sources != nullptr)
      ? collect_writable_calendar_sources(calendar_state().calendars_meta, writable_sources, nullptr)
      : 0;
  std::string_view cal_ent{};
  if (writable_cnt > 0) {
    const int w_idx = std::clamp<int>(rs.cal.draft_cal_idx, 0, writable_cnt - 1);
    cal_ent = writable_sources[w_idx].entity_id;
  }
  const std::string_view title_sv =
      rs.cal.draft_title.empty() ? std::string_view("Reminder") : std::string_view(rs.cal.draft_title);
  const int r_idx = std::clamp<int>(rs.cal.draft_rrule_idx, 0, 6);
  const std::string_view rrule_sv = kCalRepeatRrules[r_idx];
  const int32_t ev_ymd = (rs.cal.draft_ymd > 0) ? rs.cal.draft_ymd : 20260101;

  if (rs.cal.draft_all_day) {
    char s_date[12];
    char e_date[12];
    format_ymd_iso(ev_ymd, s_date);
    format_ymd_iso(add_days_ymd(ev_ymd, 1), e_date);
    latter_core::emit_calendar_create_event(
        cal_ent, title_sv, true, s_date, e_date, rs.cal.draft_location, rrule_sv);
  } else {
    const int s_min = std::clamp<int>(rs.cal.draft_start_min, 0, 1425);
    const int e_min = std::clamp<int>(std::max<int>(rs.cal.draft_end_min, s_min + 15), 15, 1440);
    char s_date[12];
    char e_date[12];
    format_ymd_iso(ev_ymd, s_date);
    const int32_t end_ymd = (e_min >= 1440) ? add_days_ymd(ev_ymd, 1) : ev_ymd;
    const int eff_e_min = (e_min >= 1440) ? 0 : e_min;
    format_ymd_iso(end_ymd, e_date);
    char s_dt[32];
    char e_dt[32];
    std::snprintf(s_dt, sizeof(s_dt), "%sT%02d:%02d:00", s_date, s_min / 60, s_min % 60);
    std::snprintf(e_dt, sizeof(e_dt), "%sT%02d:%02d:00", e_date, eff_e_min / 60, eff_e_min % 60);
    latter_core::emit_calendar_create_event(
        cal_ent, title_sv, false, s_dt, e_dt, rs.cal.draft_location, rrule_sv);
  }

  rs.cal.selected_ymd = ev_ymd;
  rs.cal.drilldown = CalDrilldown::NONE;
  rs.cal.delete_confirm_open = false;
  rs.cal.feedback_text = std::string("Added: ") + std::string(title_sv);
  rs.cal.feedback_ms = now_ms;
  ensure_calendar_window_for_ymd(ev_ymd, prefs, true);
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

}  // namespace

namespace touch_detail {

void record_stopwatch_lap(StopwatchRuntimeState &sw, uint32_t cur_ms) noexcept {
  sw.record_lap_elapsed(cur_ms);
}

void ensure_calendar_window_for_ymd(int32_t anchor_ymd,
                                    const PersistedDevicePrefs &prefs,
                                    bool force) {
  auto &store = calendar_window_store();
  const int lookahead = static_cast<int>(snap_calendar_lookahead_days(prefs.calendar_lookahead_days));
  const bool out_of_bounds =
      (store.window_start_ymd == 0) ||
      (anchor_ymd < add_days_ymd(store.window_start_ymd, 14)) ||
      (anchor_ymd > add_days_ymd(store.window_end_ymd, -21));
  if (!force && !out_of_bounds) return;

  const int32_t want_start = add_days_ymd(anchor_ymd, -35);
  const int32_t want_end = add_days_ymd(anchor_ymd, std::max(62, lookahead + 7));
  char s_iso[12];
  char e_iso[12];
  format_ymd_iso(want_start, s_iso);
  format_ymd_iso(want_end, e_iso);
  store.loading = true;
  latter_core::emit_calendar_fetch_window(s_iso, e_iso, lookahead);
}

void stop_calendar_dictation_if_active(DeviceRuntimeState &rs,
                                       const UiTouchRuntime &rt,
                                       uint32_t now_ms,
                                       const char *msg) {
  if (!cal_dictation_active(rs)) return;
  cancel_calendar_dictation(msg, now_ms, rs);
  rs.voice.intercom_turn = false;
  rt.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
  rt.apply_power_policy();
  rt.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
}

}  // namespace touch_detail

bool TouchGestureEngine::handle_down_time_cal_(TouchGestureEngine &eng, int /*tx*/, int /*ty*/,
                                             uint32_t now_ms, const HitRegion * /*hit*/,
                                             uint16_t wid, uint32_t tkey) {
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  auto &rt = eng.rt_;
  auto &gest = touch_gesture();

  if (rs.nav.gui_page == PageId::TIME) {
    const int sub = std::clamp(rs.nav.time_subpage, 0, 3);

    if (sub == TimeSubpage::TIMERS && timer_view_slot() > 0) {
      int &view_idx = timer_view_slot();
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        gest.active = false;
        view_idx = 0;
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      const int s_idx = std::clamp(view_idx - 1, 0, 2);
      auto &s = timer_slots()[s_idx];
      if (wid == WidgetId::TIMER_PRIMARY_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        gest.active = false;
        toggle_timer_slot_run(s, now_ms, rt);
        return true;
      }
      if (wid >= WidgetId::STEPPER_BASE && wid < WidgetId::STEPPER_BASE + 4) {
        gest.active = false;
        if (!s.running && !s.paused && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
          static constexpr int kStepSecs[4] = {-300, -60, 60, 300};
          const int col = static_cast<int>(wid - WidgetId::STEPPER_BASE);
          s.duration_sec = std::clamp(s.duration_sec + kStepSecs[col], 60, 3600);
          s.remaining_sec = s.duration_sec;
          sync_multi_timer_state();
          rt.apply_power_policy();
          rt.schedule_disp();
        }
        return true;
      }
      if (wid == WidgetId::TIMER_PLUS_1M_BTN && (s.running || s.paused) &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
        gest.active = false;
        if (s.running) {
          const int cur_rem = (s.target_ms > now_ms)
                                  ? static_cast<int>((s.target_ms - now_ms + 999U) / 1000U)
                                  : 0;
          const int next_rem = std::min(36000, cur_rem + 60);
          s.remaining_sec = next_rem;
          s.target_ms = now_ms + static_cast<uint32_t>(next_rem) * 1000U;
        } else {
          s.remaining_sec = std::min(36000, s.remaining_sec + 60);
        }
        s.duration_sec = std::max(s.duration_sec, s.remaining_sec);
        sync_multi_timer_state();
        rt.apply_power_policy();
        rt.schedule_disp();
        return true;
      }
      if (wid == WidgetId::TIMER_RESET_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        gest.active = false;
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
        rt.apply_power_policy();
        rt.schedule_disp();
      }
      return true;
    }

    if (sub == TimeSubpage::ALARMS && rs.alarms.edit_idx > 0) {
      if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        gest.active = false;
        rs.alarms.edit_idx = 0;
        mark_dirty_full();
        rt.schedule_disp();
        return true;
      }
      if ((wid == WidgetId::ALARM_HH_BTN || wid == WidgetId::ALARM_MM_BTN) &&
          touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
        gest.active = false;
        rs.nav.alarm_edit_field = (wid == WidgetId::ALARM_HH_BTN) ? 0 : 1;
        rt.schedule_disp();
        return true;
      }
      const int a_idx = std::clamp(rs.alarms.edit_idx - 1, 0, 2);
      auto &ap = prefs.alarms[a_idx];
      if (wid >= WidgetId::STEPPER_BASE && wid < WidgetId::STEPPER_BASE + 4) {
        gest.active = false;
        if (touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
          const int col = static_cast<int>(wid - WidgetId::STEPPER_BASE);
          if (rs.nav.alarm_edit_field == 0) {
            static constexpr int kHSteps[4] = {-6, -1, 1, 6};
            ap.hour = static_cast<uint8_t>((static_cast<int>(ap.hour) + 24 + kHSteps[col]) % 24);
          } else {
            static constexpr int kMSteps[4] = {-5, -1, 1, 5};
            ap.minute = static_cast<uint8_t>((static_cast<int>(ap.minute) + 60 + kMSteps[col]) % 60);
          }
          latter_core::sync_local_pref_key_edit(latter_core::PrefKey::ALARMS_SUMMARY);
          rt.schedule_disp();
        }
        return true;
      }
      if (wid == WidgetId::ALARM_RECUR_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        gest.active = false;
        ap.recurrence = static_cast<uint8_t>((ap.recurrence + 1) % 4);
        latter_core::sync_local_pref_key_edit(latter_core::PrefKey::ALARMS_SUMMARY);
        rt.schedule_disp();
      }
      return true;
    }

    if (sub == TimeSubpage::STOPWATCH) {
      if (wid == WidgetId::SW_PRIMARY_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        gest.active = false;
        if (!rs.stopwatch.running) {
          rs.stopwatch.running = true;
          rs.stopwatch.started_in_analog = false;
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
      if (wid == WidgetId::SW_LAP_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
        gest.active = false;
        if (rs.stopwatch.running) {
          record_stopwatch_lap(rs.stopwatch, now_ms - rs.stopwatch.start_time_ms);
          rt.schedule_disp();
        }
        return true;
      }
      if (wid == WidgetId::SW_RESET_BTN && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        gest.active = false;
        rs.stopwatch.reset();
        latter_core::check_and_emit_timer_alarm_state(now_ms);
        rt.schedule_disp();
      }
    }
    return true;
  }

  if (rs.nav.gui_page != PageId::CALENDAR) return false;

  const bool touch_time_ok = rt.time_valid && rt.time_year >= 2020;
  const int32_t today_ymd = TouchGestureEngine::resolve_touch_today_ymd_(rt);
  static bool s_touch_cal_anchored = false;
  if (!touch_time_ok) {
    s_touch_cal_anchored = false;
  }
  if (rs.cal.selected_ymd <= 0 ||
      (touch_time_ok && !s_touch_cal_anchored && rs.cal.selected_ymd == 20260101)) {
    rs.cal.selected_ymd = today_ymd;
  }
  if (rs.cal.draft_ymd <= 0 ||
      (touch_time_ok && !s_touch_cal_anchored && rs.cal.draft_ymd == 20260101)) {
    rs.cal.draft_ymd = today_ymd;
  }
  if (touch_time_ok) {
    s_touch_cal_anchored = true;
  }

  if (wid == WidgetId::BACK_BUTTON && touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    gest.active = false;
    if (rs.cal.delete_confirm_open) {
      rs.cal.delete_confirm_open = false;
    } else if (rs.cal.drilldown == CalDrilldown::DETAIL) {
      rs.cal.drilldown = CalDrilldown::NONE;
    } else if ((rs.cal.drilldown >= CalDrilldown::PICK_TITLE &&
                rs.cal.drilldown <= CalDrilldown::PICK_REPEAT) ||
               rs.cal.drilldown == CalDrilldown::ADD_STEP2) {
      rs.cal.drilldown = CalDrilldown::ADD_STEP1;
    } else if (rs.cal.drilldown == CalDrilldown::ADD_STEP1) {
      stop_calendar_dictation_if_active(rs, rt, now_ms);
      rs.cal.drilldown = CalDrilldown::NONE;
    } else if (TouchGestureEngine::return_from_shortcut_origin_(rs, rt, now_ms)) {
      return true;
    }
    mark_dirty_full();
    rt.schedule_disp();
    return true;
  }

  if (rs.cal.drilldown == CalDrilldown::NONE &&
      (wid == WidgetId::CAL_NAV_PREV || wid == WidgetId::CAL_NAV_NEXT) &&
      touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
    gest.active = false;
    const int dir = (wid == WidgetId::CAL_NAV_NEXT) ? 1 : -1;
    if (rs.cal.subpage == CalendarSubpage::OVERVIEW) {
      static constexpr uint8_t kHorizons[4] = {7U, 14U, 30U, 60U};
      const uint8_t cur_d = snap_calendar_lookahead_days(prefs.calendar_lookahead_days);
      int idx = 2;
      for (int i = 0; i < 4; ++i) {
        if (kHorizons[i] == cur_d) { idx = i; break; }
      }
      const uint8_t next_d = kHorizons[std::clamp(idx + dir, 0, 3)];
      if (next_d != cur_d) {
        char d_str[8];
        std::snprintf(d_str, sizeof(d_str), "%u", static_cast<unsigned>(next_d));
        uint32_t eff = 0U;
        if (latter_core::record_local_pref_edit(PrefKey::CALENDAR_LOOKAHEAD_DAYS, d_str, now_ms, eff)) {
          device_prefs_state().last_effects |= eff;
          rt.cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
        }
        rs.cal.overview_page = 0;
        ensure_calendar_window_for_ymd(today_ymd, prefs);
      }
    } else if (rs.cal.subpage == CalendarSubpage::DAY) {
      rs.cal.selected_ymd = add_days_ymd(rs.cal.selected_ymd, dir);
      rs.cal.day_page = 0;
      ensure_calendar_window_for_ymd(rs.cal.selected_ymd, prefs);
    } else if (rs.cal.subpage == CalendarSubpage::WEEK) {
      rs.cal.selected_ymd = add_days_ymd(rs.cal.selected_ymd, dir * 7);
      rs.cal.week_page = 0;
      ensure_calendar_window_for_ymd(rs.cal.selected_ymd, prefs);
    } else if (rs.cal.subpage == CalendarSubpage::MONTH) {
      rs.cal.selected_ymd = add_months_ymd(rs.cal.selected_ymd, dir);
      ensure_calendar_window_for_ymd(rs.cal.selected_ymd, prefs);
    }
    mark_dirty_full();
    rt.schedule_disp();
    return true;
  }

  if (rs.cal.drilldown == CalDrilldown::ADD_STEP2) {
    if ((wid == WidgetId::CAL_ADD_DATE_PREV || wid == WidgetId::CAL_ADD_DATE_NEXT) &&
        touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
      gest.active = false;
      rs.cal.draft_ymd = add_days_ymd(rs.cal.draft_ymd, (wid == WidgetId::CAL_ADD_DATE_NEXT) ? 1 : -1);
      rt.schedule_disp();
      return true;
    }
    if (!rs.cal.draft_all_day &&
        wid >= WidgetId::STEPPER_BASE && wid < WidgetId::STEPPER_BASE + 4 &&
        touch_router().allow_tap(now_ms, TOUCH_DEBOUNCE_RAPID, tkey)) {
      gest.active = false;
      static constexpr int kDeltas[4] = {-60, -15, 15, 60};
      const int delta = kDeltas[static_cast<int>(wid - WidgetId::STEPPER_BASE)];
      if (rs.cal.draft_time_field == 0U) {
        const int dur = std::max(15, static_cast<int>(rs.cal.draft_end_min) - static_cast<int>(rs.cal.draft_start_min));
        const int next_s = std::clamp(static_cast<int>(rs.cal.draft_start_min) + delta, 0, 1425);
        rs.cal.draft_start_min = static_cast<int16_t>(next_s);
        rs.cal.draft_end_min = static_cast<int16_t>(std::clamp(next_s + dur, next_s + 15, 1440));
      } else {
        const int min_e = static_cast<int>(rs.cal.draft_start_min) + 15;
        rs.cal.draft_end_min = static_cast<int16_t>(
            std::clamp(static_cast<int>(rs.cal.draft_end_min) + delta, min_e, 1440));
      }
      rt.schedule_disp();
    }
  }
  return true;
}

bool TouchGestureEngine::handle_tap_time_cal_(
    int /*tx*/, int /*ty*/, uint32_t now_rel, const HitRegion *hit, uint16_t wid) {
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  if (rs.nav.gui_page == PageId::TIME) {
    const int sub = std::clamp(rs.nav.time_subpage, 0, 3);
    if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + kMaxDockSlots &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int target_sub = std::clamp(static_cast<int>(hit->param), 0, 3);
      if (target_sub != sub) {
        rs.nav.time_subpage = target_sub;
        mark_dirty_full();
        rt_.schedule_disp();
      }
    } else if (sub == TimeSubpage::CLOCK) {
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        WorldClockEntry wc_entries[kMaxWorldClocks + 1]{};
        const int tot_wc = collect_active_world_clocks(prefs, area_ctx_state(), wc_entries);
        const int max_pg = std::max(0, (tot_wc - 1) / 2);
        rs.nav.world_clock_page = std::clamp(
            rs.nav.world_clock_page + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 0, max_pg);
        rt_.schedule_disp();
      }
    } else if (sub == TimeSubpage::TIMERS && timer_view_slot() == 0) {
      if (wid >= WidgetId::TIMER_TAB_BASE && wid < WidgetId::TIMER_TAB_BASE + 3 &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        timer_view_slot() = std::clamp(static_cast<int>(hit->param), 0, 2) + 1;
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (wid >= WidgetId::TIMER_PRESET_BASE && wid < WidgetId::TIMER_PRESET_BASE + 3 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int idx = std::clamp(static_cast<int>(hit->param), 0, 2);
        toggle_timer_slot_run(timer_slots()[idx], now_rel, rt_);
      }
    } else if (sub == TimeSubpage::ALARMS && rs.alarms.edit_idx == 0) {
      if (wid == WidgetId::ALARM_SNOOZE_CANCEL &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.alarms.snooze_until = 0;
        snoozed_alarm_slot() = 0;
        rt_.schedule_disp();
      } else if (wid >= WidgetId::ALARM_TOGGLE_BASE && wid < WidgetId::ALARM_TOGGLE_BASE + 3 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int a_idx = std::clamp(static_cast<int>(hit->param), 0, 2);
        prefs.alarms[a_idx].enabled = !prefs.alarms[a_idx].enabled;
        if (!prefs.alarms[a_idx].enabled &&
            (rs.alarms.ringing_idx == a_idx + 1 || snoozed_alarm_slot() == a_idx + 1)) {
          rs.alarms.snooze_until = 0;
          snoozed_alarm_slot() = 0;
        }
        latter_core::sync_local_pref_key_edit(latter_core::PrefKey::ALARMS_SUMMARY);
        rt_.schedule_disp();
      } else if (wid >= WidgetId::ALARM_CARD_BASE && wid < WidgetId::ALARM_CARD_BASE + 3 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int a_idx = std::clamp(static_cast<int>(hit->param), 0, 2);
        rs.alarms.edit_idx = a_idx + 1;
        rs.nav.alarm_edit_field = 1;
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
    return true;
  }

  if (rs.nav.gui_page != PageId::CALENDAR) return false;

  const int32_t today_ymd = TouchGestureEngine::resolve_touch_today_ymd_(rt_);
  const auto &store = calendar_window_store();

  if (rs.cal.delete_confirm_open) {
    if ((wid == WidgetId::CONFIRM_CANCEL || wid == WidgetId::NONE) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cal.delete_confirm_open = false;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if (wid == WidgetId::CONFIRM_ACCEPT &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      if (rs.cal.selected_event_idx >= 0 && rs.cal.selected_event_idx < store.count) {
        const auto &ev = store.events[rs.cal.selected_event_idx];
        CalendarSourceMeta *sources = touch_cal_sources_scratch();
        const int src_cnt = parse_calendar_sources_meta(calendar_state().calendars_meta, sources);
        const std::string_view ent_sv =
            (static_cast<int>(ev.cal_idx) < src_cnt) ? std::string_view(sources[ev.cal_idx].entity_id)
                                                     : std::string_view{};
        if (!ent_sv.empty() && ev.uid[0] != '\0') {
          latter_core::emit_calendar_delete_event(ent_sv, ev.uid, ev.recurrence_id);
          rs.cal.feedback_text = "Event deleted";
          rs.cal.feedback_ms = now_rel;
        }
      }
      rs.cal.delete_confirm_open = false;
      rs.cal.drilldown = CalDrilldown::NONE;
      ensure_calendar_window_for_ymd(rs.cal.selected_ymd, prefs, true);
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (rs.cal.drilldown == CalDrilldown::DETAIL) {
    if (wid == WidgetId::CAL_DELETE_BTN &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cal.delete_confirm_open = true;
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (rs.cal.drilldown == CalDrilldown::ADD_STEP1) {
    if ((wid == WidgetId::CAL_ADD_TITLE_MIC || wid == WidgetId::CAL_ADD_LOC_MIC) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const uint8_t target_f = (wid == WidgetId::CAL_ADD_TITLE_MIC) ? 1U : 2U;
      if (cal_dictation_active(rs)) {
        stop_calendar_dictation_if_active(rs, rt_, now_rel, "Cancelled");
      } else {
        start_calendar_dictation(target_f, now_rel, rs);
        rt_.cmd(UiHostCmd::START_CALENDAR_DICTATION);
      }
      rt_.schedule_disp();
    } else if ((wid == WidgetId::CAL_ADD_TITLE_BTN || wid == WidgetId::CAL_ADD_LOC_BTN ||
                wid == WidgetId::CAL_ADD_CAL_BTN || wid == WidgetId::CAL_ADD_REPEAT_BTN ||
                wid == WidgetId::CAL_ADD_NEXT_BTN) &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      stop_calendar_dictation_if_active(rs, rt_, now_rel);
      rs.cal.picker_page = 0;
      if (wid == WidgetId::CAL_ADD_TITLE_BTN) rs.cal.drilldown = CalDrilldown::PICK_TITLE;
      else if (wid == WidgetId::CAL_ADD_LOC_BTN) rs.cal.drilldown = CalDrilldown::PICK_LOCATION;
      else if (wid == WidgetId::CAL_ADD_CAL_BTN) rs.cal.drilldown = CalDrilldown::PICK_CALENDAR;
      else if (wid == WidgetId::CAL_ADD_REPEAT_BTN) rs.cal.drilldown = CalDrilldown::PICK_REPEAT;
      else rs.cal.drilldown = CalDrilldown::ADD_STEP2;
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (rs.cal.drilldown == CalDrilldown::ADD_STEP2) {
    if (wid == WidgetId::CAL_ADD_ALLDAY_BTN &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.cal.draft_all_day = !rs.cal.draft_all_day;
      mark_dirty_full();
      rt_.schedule_disp();
    } else if ((wid == WidgetId::CAL_ADD_START_BTN || wid == WidgetId::CAL_ADD_END_BTN) &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      rs.cal.draft_time_field = (wid == WidgetId::CAL_ADD_START_BTN) ? 0U : 1U;
      rt_.schedule_disp();
    } else if (wid == WidgetId::CAL_ADD_SAVE_BTN &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      submit_calendar_draft_event(rs, prefs, now_rel);
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (rs.cal.drilldown >= CalDrilldown::PICK_TITLE &&
             rs.cal.drilldown <= CalDrilldown::PICK_REPEAT) {
    if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      int total_opts = 10;
      if (rs.cal.drilldown == CalDrilldown::PICK_LOCATION) {
        std::string locs[kMaxCalendarLocations]{};
        total_opts = 1 + parse_calendar_locations_catalog(calendar_state().locations_catalog, locs);
      } else if (rs.cal.drilldown == CalDrilldown::PICK_CALENDAR) {
        total_opts = std::max(1, collect_writable_calendar_sources(calendar_state().calendars_meta, nullptr, nullptr));
      } else if (rs.cal.drilldown == CalDrilldown::PICK_REPEAT) {
        total_opts = 7;
      }
      const int max_pg = std::max(0, (total_opts - 1) / 5);
      rs.cal.picker_page = std::clamp(rs.cal.picker_page + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 0, max_pg);
      rt_.schedule_disp();
    } else if (wid >= WidgetId::CAL_PICKER_SLOT_BASE && wid < WidgetId::CAL_PICKER_SLOT_BASE + 5 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int opt_idx = std::max(0, static_cast<int>(hit->param));
      if (rs.cal.drilldown == CalDrilldown::PICK_TITLE && opt_idx < 10) {
        rs.cal.draft_title = kCalTitlePresets[opt_idx];
      } else if (rs.cal.drilldown == CalDrilldown::PICK_LOCATION) {
        if (opt_idx == 0) {
          rs.cal.draft_location.clear();
        } else {
          std::string locs[kMaxCalendarLocations]{};
          const int n_locs = parse_calendar_locations_catalog(calendar_state().locations_catalog, locs);
          if (opt_idx - 1 < n_locs) {
            rs.cal.draft_location = std::move(locs[opt_idx - 1]);
          }
        }
      } else if (rs.cal.drilldown == CalDrilldown::PICK_CALENDAR) {
        rs.cal.draft_cal_idx = static_cast<uint8_t>(opt_idx);
      } else if (rs.cal.drilldown == CalDrilldown::PICK_REPEAT && opt_idx < 7) {
        rs.cal.draft_rrule_idx = static_cast<uint8_t>(opt_idx);
      }
      rs.cal.drilldown = CalDrilldown::ADD_STEP1;
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + kMaxDockSlots &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    const int target_sub = std::clamp(static_cast<int>(hit->param), 0, 3);
    if (target_sub != rs.cal.subpage) {
      rs.cal.subpage = target_sub;
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (wid == WidgetId::CAL_ADD_BTN &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    open_calendar_composer(rs, rt_, today_ymd, now_rel);
    mark_dirty_full();
    rt_.schedule_disp();
  } else if (wid == WidgetId::CAL_TODAY_BTN &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    rs.cal.selected_ymd = today_ymd;
    rs.cal.day_page = 0;
    rs.cal.week_page = 0;
    ensure_calendar_window_for_ymd(today_ymd, prefs);
    mark_dirty_full();
    rt_.schedule_disp();
  } else if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
    const int step_d = (wid == WidgetId::CHEVRON_UP) ? -1 : 1;
    const int per_pg = (SCREEN_H > 520) ? 5 : (LATTER_ROUND_DISPLAY ? 3 : 4);
    const int max_ev_pg = std::max(0, (store.count - 1) / per_pg);
    if (rs.cal.subpage == CalendarSubpage::OVERVIEW) {
      rs.cal.overview_page = std::clamp(rs.cal.overview_page + step_d, 0, max_ev_pg);
    } else if (rs.cal.subpage == CalendarSubpage::DAY) {
      rs.cal.day_page = std::clamp(rs.cal.day_page + step_d, 0, max_ev_pg);
    } else if (rs.cal.subpage == CalendarSubpage::WEEK) {
      rs.cal.week_page = std::clamp(rs.cal.week_page + step_d, 0, max_ev_pg);
    }
    rt_.schedule_disp();
  } else if (wid >= WidgetId::CAL_EVENT_BASE && wid < WidgetId::CAL_EVENT_BASE + 5 &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    const int ev_idx = static_cast<int>(hit->param);
    if (ev_idx >= 0 && ev_idx < store.count) {
      rs.cal.selected_event_idx = ev_idx;
      rs.cal.drilldown = CalDrilldown::DETAIL;
      rs.cal.delete_confirm_open = false;
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (wid >= WidgetId::CAL_WEEK_DAY_BASE && wid < WidgetId::CAL_WEEK_DAY_BASE + 7 &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    const int32_t week_start = start_of_week_ymd(rs.cal.selected_ymd, prefs.calendar_week_start == 1U);
    rs.cal.selected_ymd = add_days_ymd(week_start, std::clamp(static_cast<int>(hit->param), 0, 6));
    rs.cal.subpage = CalendarSubpage::DAY;
    rs.cal.day_page = 0;
    ensure_calendar_window_for_ymd(rs.cal.selected_ymd, prefs);
    mark_dirty_full();
    rt_.schedule_disp();
  } else if (wid >= WidgetId::CAL_MONTH_CELL_BASE && wid < WidgetId::CAL_MONTH_CELL_BASE + 42 &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    int sy = 2026, sm = 1, sd = 1;
    (void)split_ymd(rs.cal.selected_ymd, sy, sm, sd);
    const int32_t grid_start = start_of_week_ymd(make_ymd(sy, sm, 1), prefs.calendar_week_start == 1U);
    rs.cal.selected_ymd = add_days_ymd(grid_start, std::clamp(static_cast<int>(hit->param), 0, 41));
    rs.cal.subpage = CalendarSubpage::DAY;
    rs.cal.day_page = 0;
    ensure_calendar_window_for_ymd(rs.cal.selected_ymd, prefs);
    mark_dirty_full();
    rt_.schedule_disp();
  } else if (wid >= WidgetId::CAL_MONTH_WEEK_BASE && wid < WidgetId::CAL_MONTH_WEEK_BASE + 6 &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    int sy = 2026, sm = 1, sd = 1;
    (void)split_ymd(rs.cal.selected_ymd, sy, sm, sd);
    const int32_t grid_start = start_of_week_ymd(make_ymd(sy, sm, 1), prefs.calendar_week_start == 1U);
    const int row = std::clamp(static_cast<int>(hit->param), 0, 5);
    int32_t target_ymd = add_days_ymd(grid_start, row * 7);
    for (int d = 0; d < 7; ++d) {
      const int32_t cand = add_days_ymd(target_ymd, d);
      int cy = 0, cm = 0, cd = 0;
      if (split_ymd(cand, cy, cm, cd) && cy == sy && cm == sm) {
        target_ymd = cand;
        break;
      }
    }
    rs.cal.selected_ymd = target_ymd;
    rs.cal.subpage = CalendarSubpage::WEEK;
    rs.cal.week_page = 0;
    ensure_calendar_window_for_ymd(rs.cal.selected_ymd, prefs);
    mark_dirty_full();
    rt_.schedule_disp();
  }

  return true;
}

}  // namespace latter_ui
