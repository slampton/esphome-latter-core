// ==============================================================================
// Latter Compiled On-Device Home, Clock, Pinned, Area & Home Hub Touch Dispatch
// (components/latter_ui/touch_dispatch_home_area.cpp)
// ==============================================================================

#include "touch_dispatch_internal.h"
#include "ui_pages.h"
#include "ui_touch_recognizer.h"
#include "audio_router.h"
#include "latter_actions.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace latter_ui {
using namespace latter_core;
using namespace touch_detail;

namespace {

inline void do_send_intercom_broadcast() {
  latter_core::emit_intercom_broadcast(runtime_state().intercom.broadcast_message);
}

}  // namespace

namespace touch_detail {

void do_select_active_area_latter() {
  latter_core::emit_set_active_area(runtime_state().area.active_area_id);
}

void stop_broadcast_dictation_if_active(DeviceRuntimeState &rs,
                                        const UiTouchRuntime &rt,
                                        uint32_t now_ms,
                                        const char *msg) {
  if (!broadcast_dictation_active(rs)) return;
  cancel_broadcast_dictation(msg, now_ms, rs);
  rs.voice.intercom_turn = false;
  rt.cmd(UiHostCmd::ABORT_VOICE_ASSISTANT);
  rt.apply_power_policy();
  rt.cmd(UiHostCmd::SYNC_WAKE_WORD_STATE);
}

}  // namespace touch_detail

void TouchGestureEngine::handle_home_activity_action_(const HomeActivityItem *items, int count, int item_idx,
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
      latter_core::check_and_emit_timer_alarm_state(now_ms);
      rt.apply_power_policy();
    } else if (kind == 2) {
      if (rs.stopwatch.running) {
        rs.stopwatch.running = false;
        rs.stopwatch.elapsed_ms = now_ms - rs.stopwatch.start_time_ms;
      } else {
        rs.stopwatch.running = true;
        rs.stopwatch.start_time_ms = now_ms - rs.stopwatch.elapsed_ms;
      }
      latter_core::check_and_emit_timer_alarm_state(now_ms);
    } else if (kind == 3) {
      const uint32_t intent_ts = TouchGestureEngine::now_or_hal_ms_(now_ms);
      audio_session().set_play_intent(MusicPlayIntent::HomeQuickToggle, intent_ts);
      rt.cmd(UiHostCmd::TOGGLE_HA_MUSIC);
    } else if (kind == 4) {
      if (s_idx == 0) {
        rs.alarms.snooze_until = 0;
        snoozed_alarm_slot() = 0;
      } else if (s_idx >= 1 && s_idx <= 3) {
        prefs.alarms[s_idx - 1].enabled = false;
        if (rs.alarms.ringing_idx == s_idx || snoozed_alarm_slot() == s_idx) {
          rs.alarms.snooze_until = 0;
          snoozed_alarm_slot() = 0;
        }
        latter_core::sync_local_pref_key_edit(latter_core::PrefKey::ALARMS_SUMMARY);
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
          ac.doorbell_talk_state != "off") {
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
    rs.nav.shortcut_origin = ShortcutOrigin::NONE;
    rs.nav.gui_page = PageId::TIME;
    rs.nav.time_subpage = TimeSubpage::TIMERS;
    rs.alarms.edit_idx = 0;
    timer_view_slot() = (s_idx >= 0 && s_idx < 3) ? (s_idx + 1) : 0;
  } else if (kind == 2) {
#if LATTER_ROUND_DISPLAY
    if (rs.stopwatch.started_in_analog) {
      rs.nav.shortcut_origin = ShortcutOrigin::HOME_DOCK;
      rs.nav.gui_page = PageId::TOOLS;
      rs.nav.tools_subpage = ToolsSubpage::STOPWATCH;
    } else
#endif
    {
      rs.nav.shortcut_origin = ShortcutOrigin::NONE;
      rs.nav.gui_page = PageId::TIME;
      rs.nav.time_subpage = TimeSubpage::STOPWATCH;
      timer_view_slot() = 0;
      rs.alarms.edit_idx = 0;
    }
  } else if (kind == 3) {
    rs.nav.shortcut_origin = ShortcutOrigin::NONE;
    rs.nav.gui_page = PageId::MUSIC;
    reset_music_to_now_playing(rs, now_ms, true);
    const auto &ms = media_state();
    if (rt.select_opt(UiSelectId::MUSIC_FULLSCREEN_ART_MODE) != "None" &&
        (!ms.is_local || rt.album_art_w == 0) && ms.art_url != rs.music.loaded_art_url) {
      rt.cmd(UiHostCmd::FETCH_REMOTE_ART_DEBOUNCED);
    }
  } else if (kind == 4) {
    rs.nav.shortcut_origin = ShortcutOrigin::NONE;
    rs.nav.gui_page = PageId::TIME;
    rs.nav.time_subpage = TimeSubpage::ALARMS;
    timer_view_slot() = 0;
    rs.alarms.edit_idx = (s_idx >= 1 && s_idx <= 3) ? s_idx : 0;
    rs.nav.alarm_edit_field = 1;
  } else if (kind == 5) {
    TouchGestureEngine::launch_dock_app_(PageId::CALENDAR, rs, rt, now_ms);
    rs.cal.subpage = CalendarSubpage::DAY;
    return;
  } else if (kind == 6) {
    const auto &ac = area_ctx_state();
    const bool db_live = !ac.doorbell_talk_state.empty() &&
                         ac.doorbell_talk_state != "idle" &&
                         ac.doorbell_talk_state != "off" &&
                         ac.doorbell_talk_state != "unavailable" &&
                         ac.doorbell_talk_state != "unknown";
    rs.nav.shortcut_origin = ShortcutOrigin::NONE;
    if (db_live || cam_call_active() || cam_listen_active()) {
      rs.nav.gui_page = PageId::CAMERAS;
      rs.cameras.tab = CamerasTab::SINGLE;
      rs.cameras.quick_reply_open = false;
      rs.cameras.hud_active = true;
      rs.cameras.hud_timer = now_ms;
      rs.cameras.doorbell_live_popup_active = false;
      mode_bar_anim().snap(CamerasTab::SINGLE);
      rt.cmd(UiHostCmd::SYNC_CAMERA_STREAM);
    } else {
      TouchGestureEngine::enter_home_hub_tab_(HomeHubTab::INTERCOM, rs, rt, now_ms);
    }
  }
  mark_dirty_full();
  rt.schedule_disp();
}

void TouchGestureEngine::do_fetch_area_browse_() {
  const auto &rs = runtime_state();
  const int page = rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES);
  latter_core::emit_area_browse_entities(
      rs.area.active_area_id, rs.area.domain_filter, page, LATTER_CARD_SLOT_COUNT);
}

void TouchGestureEngine::do_fetch_area_list_browse_() {
  const auto &rs = runtime_state();
  const int page = rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST);
  latter_core::emit_area_browse_list(rs.area.active_area_id, page, std::clamp(layout().list_rows, 4, kMaxListSlots));
}

void TouchGestureEngine::enter_home_hub_tab_(int target_tab, DeviceRuntimeState &rs,
                        const UiTouchRuntime &rt, uint32_t now_ms) {
  const bool was_hub = (rs.nav.gui_page == PageId::HOME_HUB);
  stop_broadcast_dictation_if_active(rs, rt, now_ms);
  if (is_camera_view_active(rs)) {
    leave_home_hub_cameras_if_active(rs, rt, now_ms);
  }
  rs.nav.gui_page = PageId::HOME_HUB;
  rs.nav.home_subpage = 0;
  rs.nav.tools_subpage = 0;
  if (!was_hub || target_tab == HomeHubTab::INTERCOM) {
    home_hub_broadcast_page() = 0;
  }
  if (!was_hub) {
    sync_home_hub_for_active_area(area_ctx_state().area_id, home_hub_state());
  }

  int hh_tabs[kMaxHomeHubTabs]{};
  const int hh_cnt = collect_home_hub_tabs(hh_tabs);
  int slot = -1;
  if (target_tab >= 0) {
    for (int i = 0; i < hh_cnt; ++i) {
      if (hh_tabs[i] == target_tab) { slot = i; break; }
    }
  }
  if (slot >= 0) {
    home_hub_tab() = target_tab;
    sync_mode_bar_window(home_hub_mode_bar_page(), slot, hh_cnt, layout().dock_slot_count);
  } else if (hh_cnt > 0) {
    slot = 0;
    for (int i = 0; i < hh_cnt; ++i) {
      if (hh_tabs[i] == home_hub_tab()) { slot = i; break; }
    }
    home_hub_tab() = hh_tabs[slot];
    sync_mode_bar_window(home_hub_mode_bar_page(), slot, hh_cnt, layout().dock_slot_count);
  } else {
    home_hub_mode_bar_page() = 0;
    home_hub_tab() = (target_tab >= 0) ? target_tab : HomeHubTab::ALARM;
  }
}

const HomeActivityItem *TouchGestureEngine::resolve_home_activities_(uint32_t now_ms, const DeviceRuntimeState &rs,
                                                const PersistedDevicePrefs &prefs,
                                                const UiTouchRuntime &rt, int &out_count,
                                                std::vector<HomeActivityItem> &fallback_buf) {
  out_count = 0;
  const HomeActivityItem *act_items = home_activity_rendered_items(out_count);
  if (out_count > 0) return act_items;
  fallback_buf.resize(static_cast<size_t>(kMaxHomeActivity));
  HomeActivityItem *fallback_items = fallback_buf.data();
  const auto &ms_hm = media_state();
  const bool m_play = is_music_playing_effective(
      !ms_hm.is_local, rt.local_mp_playing, ms_hm.state, now_ms);
  const std::string &m_t = (!ms_hm.is_local || rt.track_title.empty()) ? ms_hm.title : rt.track_title;
  const std::string &m_a = (!ms_hm.is_local || rt.track_artist.empty()) ? ms_hm.artist : rt.track_artist;
  out_count = collect_home_activities(
      now_ms, prefs.is_24h_format,
      rs.stopwatch.running, rs.stopwatch.start_time_ms, rs.stopwatch.elapsed_ms,
      m_play, m_t, m_a, rs.alarms.snooze_until,
      prefs.alarms[0].hour, prefs.alarms[0].minute, prefs.alarms[0].enabled,
      prefs.alarms[1].hour, prefs.alarms[1].minute, prefs.alarms[1].enabled,
      prefs.alarms[2].hour, prefs.alarms[2].minute, prefs.alarms[2].enabled,
      fallback_items, rt.time_hour, rt.time_minute, calendar_state().summary,
      rt.time_day_of_week);
  return fallback_items;
}

bool TouchGestureEngine::handle_tap_home_area_(
    int /*tx*/, int /*ty*/, uint32_t now_rel, const HitRegion *hit, uint16_t wid) {
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  const uint32_t tkey = make_tap_target_key(rs.nav.gui_page, wid, (hit != nullptr) ? hit->param : 0);

  if (rs.nav.gui_page == PageId::CLOCK) {
    if (prefs.home_screen_enabled && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      rs.nav.gui_page = PageId::HOME;
      rs.nav.home_subpage = 0;
      mark_dirty_full();
      rt_.schedule_disp();
    }
    return true;
  }

  if (rs.nav.gui_page == PageId::HOME) {
    if (rs.nav.home_subpage == 0) {
      int act_count = 0;
      std::vector<HomeActivityItem> act_fallback;
      const HomeActivityItem *act_items =
          TouchGestureEngine::resolve_home_activities_(now_rel, rs, prefs, rt_, act_count, act_fallback);
      if (wid == WidgetId::HOME_QUICK_BTN && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.nav.gui_page = PageId::WEATHER;
        const bool tapped_moon = (hit != nullptr) ? (hit->param != 0)
                                                  : (weather_state().sun_state == "below_horizon");
        rs.nav.sky_weather_subpage = tapped_moon ? 3 : 2;
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (wid == WidgetId::HOME_DATE_HERO && prefs.home_show_date && prefs.home_date_tap_action != 2U &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        TouchGestureEngine::launch_dock_app_(PageId::CALENDAR, rs, rt_, now_rel);
        rs.cal.subpage = (prefs.home_date_tap_action == 0U) ? CalendarSubpage::DAY : CalendarSubpage::OVERVIEW;
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (wid == WidgetId::HOME_TIME_READOUT && prefs.home_show_time && prefs.home_clock_tap_action != 2U &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        if (prefs.home_clock_tap_action == 0U) {
          TouchGestureEngine::launch_dock_app_(PageId::TIME, rs, rt_, now_rel);
        } else {
          rs.nav.gui_page = PageId::CLOCK;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (wid == WidgetId::HOME_WEATHER_HERO && prefs.home_show_weather && prefs.home_weather_tap_action != 2U &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        rs.nav.gui_page = PageId::WEATHER;
        rs.nav.sky_weather_subpage = (prefs.home_weather_tap_action == 1U) ? 1 : 0;
        mark_dirty_full();
        rt_.schedule_disp();
      } else if (wid == WidgetId::HOME_ACT_EXPAND && act_count > 0 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        home_activity_overlay_page() = 0;
        rs.nav.home_subpage = 2;
        mark_dirty_full();
        rt_.schedule_disp();
      } else if ((wid == WidgetId::HOME_ACT_ROW_0 || wid == WidgetId::HOME_ACT_ROW_1 ||
                  wid == WidgetId::HOME_ACT_ACTION_0 || wid == WidgetId::HOME_ACT_ACTION_1) &&
                 act_count > 0 && touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const bool quick_tap = (wid == WidgetId::HOME_ACT_ACTION_0 || wid == WidgetId::HOME_ACT_ACTION_1);
        TouchGestureEngine::handle_home_activity_action_(act_items, act_count, hit->param, quick_tap, now_rel, rs, prefs, rt_);
      }
      return true;
    }

    if (rs.nav.home_subpage == 1) {
      const int total_pages = home_settings_page_count(prefs);
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        const int next_pg = std::clamp(
            rs.nav.home_settings_page + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, total_pages);
        rs.nav.home_settings_page = next_pg;
        rt_.schedule_disp();
      } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + kMaxListSlots &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const auto item = static_cast<HomeSettingsItemId>(hit->param);
        switch (item) {
          case HomeSettingsItemId::SHOW_TIME:
            rt_.sw_toggle(UiSwitchId::HOME_SHOW_TIME);
            break;
          case HomeSettingsItemId::TIME_TAP: {
            const size_t next_i = (static_cast<size_t>(prefs.home_clock_tap_action) + 1U) % kPrefOptsHomeClockTapAction.size();
            rt_.set_select_opt(UiSelectId::HOME_CLOCK_TAP_ACTION, kPrefOptsHomeClockTapAction[next_i]);
            break;
          }
          case HomeSettingsItemId::SHOW_DATE:
            rt_.sw_toggle(UiSwitchId::HOME_SHOW_DATE);
            break;
          case HomeSettingsItemId::DATE_TAP: {
            const size_t next_i = (static_cast<size_t>(prefs.home_date_tap_action) + 1U) % kPrefOptsHomeDateTapAction.size();
            rt_.set_select_opt(UiSelectId::HOME_DATE_TAP_ACTION, kPrefOptsHomeDateTapAction[next_i]);
            break;
          }
          case HomeSettingsItemId::SHOW_WEATHER:
            rt_.sw_toggle(UiSwitchId::HOME_SHOW_WEATHER);
            break;
          case HomeSettingsItemId::WEATHER_TAP: {
            const size_t next_i = (static_cast<size_t>(prefs.home_weather_tap_action) + 1U) % kPrefOptsHomeWeatherTapAction.size();
            rt_.set_select_opt(UiSelectId::HOME_WEATHER_TAP_ACTION, kPrefOptsHomeWeatherTapAction[next_i]);
            break;
          }
          case HomeSettingsItemId::SHOW_BATTERY:
            rt_.sw_toggle(UiSwitchId::HOME_SHOW_BATTERY);
            break;
          case HomeSettingsItemId::WAKE_GREETING:
            rt_.sw_toggle(UiSwitchId::HOME_WAKE_GREETING);
            break;
          case HomeSettingsItemId::WEATHER_BG:
            rt_.sw_toggle(UiSwitchId::HOME_WEATHER_BG);
            break;
          case HomeSettingsItemId::HOME_SUN:
          case HomeSettingsItemId::HOME_MOON: {
            const bool is_sun = (item == HomeSettingsItemId::HOME_SUN);
            const PrefKey pk = is_sun ? PrefKey::HOME_SUN : PrefKey::HOME_MOON;
            const bool next_v = is_sun ? !prefs.home_sun : !prefs.home_moon;
            uint32_t eff = 0U;
            if (latter_core::record_local_pref_edit(pk, next_v ? "1" : "0", now_rel, eff)) {
              device_prefs_state().last_effects |= eff;
              rt_.cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
            }
            break;
          }
          case HomeSettingsItemId::WEATHER_APP_BG:
            rt_.sw_toggle(UiSwitchId::WEATHER_APP_BG);
            break;
        }
        rs.nav.home_settings_page = std::clamp(rs.nav.home_settings_page, 1, home_settings_page_count(prefs));
        rt_.schedule_disp();
      }
      return true;
    }

    if (rs.nav.home_subpage == 2) {
      int act_count = 0;
      const HomeActivityItem *act_items = home_activity_rendered_items(act_count);
      const bool is_open_slot = (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + kMaxListSlots);
      const bool is_quick_slot = (wid >= WidgetId::HOME_ACT_OVERLAY_ACTION_BASE &&
                                  wid < WidgetId::HOME_ACT_OVERLAY_ACTION_BASE + kMaxListSlots);
      if ((is_open_slot || is_quick_slot) && act_count > 0) {
        if (touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
          TouchGestureEngine::handle_home_activity_action_(
              act_items, act_count, hit->param, is_quick_slot, now_rel, rs, prefs, rt_);
        }
      } else if (wid == WidgetId::NONE &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_DISMISS, tkey)) {
        rs.nav.home_subpage = 0;
        home_activity_overlay_page() = 0;
        mark_dirty_full();
        rt_.schedule_disp();
      }
    }
    return true;
  }

  if (rs.nav.gui_page == PageId::PINNED) {
    if (!rs.nav.menu_scroll_active) {
      const int total_pages = pinned_total_pages();
      if ((wid == WidgetId::CHEVRON_LEFT || wid == WidgetId::CHEVRON_RIGHT) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        if (total_pages > 1) {
          const int cur_pg = std::clamp(rs.nav.pinned_menu_page, 0, total_pages - 1);
          rs.nav.pinned_menu_page = cur_pg;
          const int target_pg =
              std::clamp(cur_pg + ((wid == WidgetId::CHEVRON_RIGHT) ? 1 : -1), 0, total_pages - 1);
          if (target_pg != cur_pg) {
            rs.nav.menu_scroll_active = true;
            rs.nav.menu_scroll_dir = (target_pg > cur_pg) ? 1 : -1;
            rs.nav.menu_scroll_target_page = target_pg;
            rt_.cmd(UiHostCmd::MENU_SCROLL_TRANSITION);
          }
        }
      } else if (wid >= WidgetId::PINNED_CARD_BASE && wid < WidgetId::PINNED_CARD_BASE + 16 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const auto &sc = shortcuts_state();
        const int idx = hit->param;
        if (idx >= 0 && static_cast<size_t>(idx) < sc.pinned_items.size()) {
          const ShortcutItem item_copy = sc.pinned_items[static_cast<size_t>(idx)];
          TouchGestureEngine::activate_shortcut_item_(item_copy, ShortcutOrigin::PINNED_APP, rs, rt_, now_rel);
        }
      }
    }
    return true;
  }

  static constexpr const char *kClimModes[6] = {"heat", "cool", "heat_cool", "off", "fan_only", "dry"};
  static constexpr const char *kClimLabels[6] = {"Heat", "Cool", "Auto", "Off", "Fan", "Dry"};

  if (rs.nav.gui_page == PageId::AREA) {
    if (rs.nav.area_subpage == AreaSubpage::PICKER || rs.area.browse_mode == "areas") {
      int &a_pg = rs.slot_page_for(latter_rpc::SlotChannel::AREA_LIST);
      const int a_tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::AREA_LIST);
      if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) && a_tot > 1 &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        const int next_pg = std::clamp(a_pg + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, a_tot);
        if (next_pg != a_pg) {
          a_pg = next_pg;
          TouchGestureEngine::do_fetch_area_list_browse_();
          rt_.schedule_disp();
        }
      } else if (wid >= WidgetId::LIST_SLOT_BASE && wid < WidgetId::LIST_SLOT_BASE + kMaxListSlots &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int s_idx = std::clamp(static_cast<int>(hit->param), 0, kMaxListSlots - 1);
        const auto &asl = rs.area_list_slots()[s_idx];
        if (!asl.id.empty()) {
          rs.area.active_area_id = asl.id;
          rs.area.active_area_name = strip_trailing_paren_state(asl.name);
          rs.nav.area_subpage = AreaSubpage::OVERVIEW;
          rs.area.browse_mode = "entities";
          rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
          sync_home_hub_for_active_area(asl.id, home_hub_state());
          do_select_active_area_latter();
          TouchGestureEngine::do_fetch_area_browse_();
          mark_dirty_full();
          rt_.schedule_disp();
        }
      }
      return true;
    }

    if (rs.nav.area_subpage == AreaSubpage::LIGHT_DETAIL && light_insp_domain() == "climate") {
      auto &hub = home_hub_state();
      ThermostatZoneItem *ci = find_climate_item_by_id(light_insp_entity_id(), hub);
      const uint16_t c_caps = (ci != nullptr && ci->caps != 0U) ? ci->caps : kClimateCapDefault;
      const bool is_c = (c_caps & kClimateCapCelsius) != 0U;
      const int sl_i = light_insp_slot();
      if ((wid == WidgetId::CLIMATE_MINUS_BTN || wid == WidgetId::CLIMATE_PLUS_BTN) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        const int cur_t = (ci != nullptr && ci->target_temp > 0)
                              ? ci->target_temp
                              : ((light_insp_bri_pct() > 0) ? light_insp_bri_pct() : (is_c ? 21 : 70));
        const int next_t = step_thermostat_setpoint(
            cur_t, (wid == WidgetId::CLIMATE_PLUS_BTN) ? 1 : -1, c_caps);
        if (ci != nullptr) {
          ci->target_temp = next_t;
          for (int ti = 0; ti < hub.thermostat_count; ++ti) {
            if (hub.thermostats[ti].entity_id == ci->entity_id) hub.thermostats[ti].target_temp = next_t;
          }
        }
        light_insp_bri_pct() = next_t;
        if (sl_i >= 0 && sl_i < 3) rs.area_entity_slots()[sl_i].level = next_t;
        ++hub.local_mutation_seq;
        rs.area.target_entity_id = light_insp_entity_id();
        rs.area.target_action = "set_temperature";
        rs.area.target_level = next_t;
        char fb[36];
        std::snprintf(fb, sizeof(fb), "Setpoint -> %d\xC2\xB0%c", next_t, is_c ? 'C' : 'F');
        rs.area.action_feedback = fb;
        rs.area.feedback_timer = now_rel;
        TouchGestureEngine::do_execute_area_action_();
        rt_.schedule_disp();
      } else if (wid >= WidgetId::CLIMATE_MODE_BASE && wid < WidgetId::CLIMATE_MODE_BASE + 6 &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int m_i = static_cast<int>(wid - WidgetId::CLIMATE_MODE_BASE);
        const bool next_on = (m_i != 3);
        if (ci != nullptr) {
          ci->mode = kClimModes[m_i];
          for (int ti = 0; ti < hub.thermostat_count; ++ti) {
            if (hub.thermostats[ti].entity_id == ci->entity_id) hub.thermostats[ti].mode = kClimModes[m_i];
          }
        }
        light_insp_active() = next_on;
        if (sl_i >= 0 && sl_i < 3) rs.area_entity_slots()[sl_i].active = next_on;
        ++hub.local_mutation_seq;
        rs.area.target_entity_id = light_insp_entity_id();
        rs.area.target_action = std::string("hvac_") + kClimModes[m_i];
        rs.area.target_level = (ci != nullptr && ci->target_temp > 0) ? ci->target_temp : light_insp_bri_pct();
        rs.area.action_feedback = std::string("HVAC: ") + kClimLabels[m_i];
        rs.area.feedback_timer = now_rel;
        mark_dirty_full();
        TouchGestureEngine::do_execute_area_action_();
        rt_.schedule_disp();
      }
      return true;
    }

    if (rs.nav.area_subpage == AreaSubpage::OVERVIEW) {
      std::string cats[8];
      const int cat_cnt = collect_area_categories(cats);
      const int vis_cap = std::clamp(layout().dock_slot_count, 1, kMaxDockSlots);
      if ((wid == WidgetId::MODE_BAR_LEFT || wid == WidgetId::MODE_BAR_RIGHT) &&
          touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        if (step_mode_bar_window_page(
                area_cat_dock_page(), (wid == WidgetId::MODE_BAR_RIGHT) ? 1 : -1,
                cat_cnt, vis_cap)) {
          rt_.schedule_disp();
        }
      } else if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + kMaxDockSlots &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        if (cat_cnt > 0) {
          const int c_idx = std::clamp(static_cast<int>(hit->param), 0, cat_cnt - 1);
          if (cats[c_idx] != rs.area.domain_filter) {
            rs.area.domain_filter = cats[c_idx];
            sync_mode_bar_window(area_cat_dock_page(), c_idx, cat_cnt, vis_cap);
            rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
            TouchGestureEngine::do_fetch_area_browse_();
            mark_dirty_full();
            rt_.schedule_disp();
          }
        }
      } else if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
        int &pg = rs.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES);
        const int tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::AREA_ENTITIES);
        const int next_pg = std::clamp(pg + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 1, std::max(1, tot));
        if (next_pg != pg) {
          pg = next_pg;
          TouchGestureEngine::do_fetch_area_browse_();
          rt_.schedule_disp();
        }
      } else if (wid == WidgetId::LIGHT_ORB_BTN &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int idx = std::clamp(static_cast<int>(hit->param), 0, 2);
        const auto &sl = rs.area_entity_slots()[idx];
        if (!sl.id.empty()) {
          light_insp_entity_id() = sl.id;
          light_insp_name() = strip_trailing_paren_state(sl.name);
          light_insp_domain() = "light";
          light_insp_slot() = idx;
          light_insp_active() = sl.active;
          light_insp_mode() = 0;
          if (sl.level > 0) light_insp_bri_pct() = sl.level;
          rs.nav.area_subpage = AreaSubpage::LIGHT_DETAIL;
          mark_dirty_full();
          rt_.schedule_disp();
        }
      } else if (((wid >= WidgetId::AREA_CARD_TOGGLE && wid < WidgetId::AREA_CARD_TOGGLE + 3) ||
                  (wid >= WidgetId::AREA_CARD_TAP && wid < WidgetId::AREA_CARD_TAP + 3)) &&
                 touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
        const int idx = std::clamp(static_cast<int>(hit->param), 0, 2);
        auto &sl = rs.area_entity_slots()[idx];
        if (!sl.id.empty() && sl.type != "text") {
          const bool is_card_body = (wid >= WidgetId::AREA_CARD_TAP && wid < WidgetId::AREA_CARD_TAP + 3);
          if (is_card_body &&
              (sl.type == "light" || sl.type == "cover" || sl.type == "fan" || sl.type == "climate")) {
            light_insp_entity_id() = sl.id;
            light_insp_name() = strip_trailing_paren_state(sl.name);
            light_insp_domain() = sl.type;
            light_insp_slot() = idx;
            light_insp_active() = sl.active;
            light_insp_mode() = 1;
            if (sl.type == "climate") {
              const ThermostatZoneItem *ci = find_climate_item_by_id_const(sl.id);
              light_insp_bri_pct() = (ci != nullptr && ci->target_temp > 0)
                                         ? ci->target_temp
                                         : ((sl.level > 0) ? sl.level : 70);
            } else if (sl.level >= 0) {
              light_insp_bri_pct() = sl.level;
            } else {
              light_insp_bri_pct() = sl.active ? 100 : ((sl.type == "light") ? 50 : 0);
            }
            rs.nav.area_subpage = AreaSubpage::LIGHT_DETAIL;
            mark_dirty_full();
            rt_.schedule_disp();
          } else if (is_card_body && sl.type == "media_player") {
            const std::string spk_id = sl.id;
            std::string spk_name = strip_trailing_paren_state(sl.name);
            if (spk_name.rfind("Speaker: ", 0) == 0 && spk_name.size() > 9U) {
              spk_name.erase(0, 9U);
            }
            if (spk_name.empty()) {
              spk_name = spk_id;
            }
            const std::string cur_spk = TouchGestureEngine::resolve_music_speaker_entity_(rs);
            TouchGestureEngine::launch_dock_app_(PageId::MUSIC, rs, rt_, now_rel);
            if (spk_id != cur_spk && spk_id.rfind("media_player.", 0) == 0) {
              rs.music.target_speaker_id = spk_id;
              rs.music.target_speaker_name = spk_name;
              rt_.cmd(UiHostCmd::SPEAKER_CONTROL_TARGET);
            }
          } else {
            rs.area.target_entity_id = sl.id;
            const bool next_act = (sl.type == "scene") ? true : !sl.active;
            std::string verb = "toggle";
            if (sl.type == "scene") verb = "turn_on";
            else if (sl.type == "cover") verb = next_act ? "open_cover" : "close_cover";
            else if (sl.type == "lock" || sl.id.rfind("lock.", 0) == 0) verb = next_act ? "unlock" : "lock";
            else if (sl.type == "media_player") verb = "toggle";
            else verb = next_act ? "turn_on" : "turn_off";
            const std::string ok_fb = strip_trailing_paren_state(sl.name);
            if (gate_or_dispatch_area_action(sl.id, verb, next_act ? 100 : 0,
                                             "PERIMETER PIN", now_rel, ok_fb)) {
              sl.active = next_act;
            }
          }
        }
      }
    }
    return true;
  }

  if (rs.nav.gui_page != PageId::HOME_HUB) return false;

  int hh_tabs[kMaxHomeHubTabs]{};
  const int hh_tab_cnt = collect_home_hub_tabs(hh_tabs);
  if (hh_tab_cnt <= 0) return true;

  const int hh_vis_cap = std::clamp(layout().dock_slot_count, 1, kMaxDockSlots);
  if ((wid == WidgetId::MODE_BAR_LEFT || wid == WidgetId::MODE_BAR_RIGHT) &&
      touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
    if (step_mode_bar_window_page(
            home_hub_mode_bar_page(), (wid == WidgetId::MODE_BAR_RIGHT) ? 1 : -1,
            hh_tab_cnt, hh_vis_cap)) {
      rt_.schedule_disp();
    }
  } else if (wid >= WidgetId::MODE_BAR_SLOT_BASE && wid < WidgetId::MODE_BAR_SLOT_BASE + kMaxDockSlots &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    const int slot = std::clamp(static_cast<int>(hit->param), 0, hh_tab_cnt - 1);
    const int target_tab = hh_tabs[slot];
    if (target_tab != home_hub_tab()) {
      TouchGestureEngine::enter_home_hub_tab_(target_tab, rs, rt_, now_rel);
      mark_dirty_full();
      rt_.schedule_disp();
    }
  } else if (home_hub_tab() == HomeHubTab::ALARM &&
             !area_ctx_state().alarm_entity.empty() &&
             wid >= WidgetId::ALARM_MODE_BASE && wid < WidgetId::ALARM_MODE_BASE + 5 &&
             touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
    static constexpr const char *kAlarmActs[5] = {
        "disarm", "arm_home", "arm_away", "arm_night", "arm_vacation"};
    static constexpr const char *kAlarmFb[5] = {
        "Disarming System...", "Arming Home...", "Arming Away...", "Arming Night...", "Arming Vacation..."};
    const int mode_i = static_cast<int>(wid - WidgetId::ALARM_MODE_BASE);
    gate_or_dispatch_alarm_action(kAlarmActs[mode_i], now_rel, kAlarmFb[mode_i]);
  } else if (home_hub_tab() == HomeHubTab::INTERCOM) {
    if (wid == WidgetId::INTERCOM_SPEAK_BTN &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      if (broadcast_dictation_active(rs)) {
        stop_broadcast_dictation_if_active(rs, rt_, now_rel, "Cancelled");
      } else {
        start_broadcast_dictation(now_rel, rs);
        rt_.cmd(UiHostCmd::START_BROADCAST_DICTATION);
      }
      mark_dirty_full();
      rt_.schedule_disp();
    } else if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
               !broadcast_dictation_active(rs) &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      std::vector<std::string> phrases;
      const int total_cnt = parse_broadcast_phrases(area_ctx_state().broadcast_phrases, phrases);
      const int max_pg = std::max(0, (total_cnt - 1) / 3);
      home_hub_broadcast_page() = std::clamp(
          home_hub_broadcast_page() + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 0, max_pg);
      rt_.schedule_disp();
    } else if (wid >= WidgetId::INTERCOM_ROW_BASE && wid < WidgetId::INTERCOM_ROW_BASE + 5 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      stop_broadcast_dictation_if_active(rs, rt_, now_rel);
      std::vector<std::string> phrases;
      const int cnt = parse_broadcast_phrases(area_ctx_state().broadcast_phrases, phrases);
      const int phrase_idx = (hit != nullptr) ? static_cast<int>(hit->param) : -1;
      if (phrase_idx >= 0 && phrase_idx < cnt) {
        const std::string &phrase = phrases[static_cast<size_t>(phrase_idx)];
        rs.intercom.broadcast_message = phrase;
        rs.intercom.broadcast_feedback = std::string("Sent: ") + phrase;
        rs.intercom.broadcast_feedback_ms = now_rel;
        do_send_intercom_broadcast();
        rt_.schedule_disp();
      }
    }
  } else if (home_hub_tab() == HomeHubTab::THERMOSTAT && home_hub_state().thermostat_count > 0) {
    auto &hub = home_hub_state();
    int &z_idx = hub.thermostat_idx;
    z_idx = std::clamp(z_idx, 0, hub.thermostat_count - 1);
    if ((wid == WidgetId::THERMOSTAT_PREV_BTN || wid == WidgetId::THERMOSTAT_NEXT_BTN) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      if (hub.thermostat_count > 1) {
        z_idx = step_wrap(z_idx, (wid == WidgetId::THERMOSTAT_NEXT_BTN) ? 1 : -1, hub.thermostat_count);
        ++hub.local_mutation_seq;
        mark_dirty_full();
        rt_.schedule_disp();
      }
    } else if ((wid == WidgetId::CLIMATE_MINUS_BTN || wid == WidgetId::CLIMATE_PLUS_BTN) &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      auto &z = hub.thermostats[z_idx];
      const uint16_t c_caps = (z.caps != 0U) ? z.caps : kClimateCapDefault;
      const bool is_c = (c_caps & kClimateCapCelsius) != 0U;
      z.target_temp = step_thermostat_setpoint(
          z.target_temp, (wid == WidgetId::CLIMATE_PLUS_BTN) ? 1 : -1, c_caps);
      for (int ai = 0; ai < hub.area_climate_count; ++ai) {
        if (hub.area_climates[ai].entity_id == z.entity_id) hub.area_climates[ai].target_temp = z.target_temp;
      }
      ++hub.local_mutation_seq;
      rs.area.target_entity_id = z.entity_id;
      rs.area.target_action = "set_temperature";
      rs.area.target_level = z.target_temp;
      char fb[36];
      std::snprintf(fb, sizeof(fb), "Setpoint -> %d\xC2\xB0%c", z.target_temp, is_c ? 'C' : 'F');
      rs.area.action_feedback = fb;
      rs.area.feedback_timer = now_rel;
      TouchGestureEngine::do_execute_area_action_();
      rt_.schedule_disp();
    } else if (wid >= WidgetId::CLIMATE_MODE_BASE && wid < WidgetId::CLIMATE_MODE_BASE + 6 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int m_i = static_cast<int>(wid - WidgetId::CLIMATE_MODE_BASE);
      auto &z = hub.thermostats[z_idx];
      z.mode = kClimModes[m_i];
      for (int ai = 0; ai < hub.area_climate_count; ++ai) {
        if (hub.area_climates[ai].entity_id == z.entity_id) hub.area_climates[ai].mode = kClimModes[m_i];
      }
      ++hub.local_mutation_seq;
      rs.area.target_entity_id = z.entity_id;
      rs.area.target_action = std::string("hvac_") + kClimModes[m_i];
      rs.area.target_level = z.target_temp;
      rs.area.action_feedback = std::string("HVAC: ") + kClimLabels[m_i];
      rs.area.feedback_timer = now_rel;
      mark_dirty_full();
      TouchGestureEngine::do_execute_area_action_();
      rt_.schedule_disp();
    }
  } else if (home_hub_tab() == HomeHubTab::ENTRY && home_hub_state().entry_count > 0) {
    auto &hub = home_hub_state();
    if ((wid == WidgetId::CHEVRON_UP || wid == WidgetId::CHEVRON_DOWN) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      const int max_pg = std::max(0, (hub.entry_count - 1) / 2);
      hub.entry_page = std::clamp(hub.entry_page + ((wid == WidgetId::CHEVRON_UP) ? -1 : 1), 0, max_pg);
      ++hub.local_mutation_seq;
      rt_.schedule_disp();
    } else if (wid >= WidgetId::ENTRY_ACTION_BASE && wid < WidgetId::ENTRY_ACTION_BASE + 2 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      const int e_idx = (hit != nullptr) ? static_cast<int>(hit->param) : -1;
      if (e_idx >= 0 && e_idx < hub.entry_count) {
        auto &item = hub.entries[e_idx];
        rs.area.target_entity_id = item.entity_id;
        if (item.is_lock) {
          const bool want_lock = (item.lock_state == LockState::UNLOCKED);
          const std::string ok_fb =
              (!item.name.empty() ? item.name : std::string("Door Lock")) +
              (want_lock ? ": Locked" : ": Unlocked");
          (void)gate_or_dispatch_area_action(
              item.entity_id, want_lock ? "lock" : "unlock",
              want_lock ? 100 : 0, "UNLOCK DOOR PIN", now_rel, ok_fb);
        } else {
          const bool want_open = (item.garage_state != GarageState::OPEN);
          const std::string g_lbl = !item.name.empty() ? item.name : std::string("Garage Door");
          const std::string ok_fb = (want_open ? "Opening " : "Closing ") + g_lbl + "...";
          (void)gate_or_dispatch_area_action(
              item.entity_id, want_open ? "open_cover" : "close_cover",
              want_open ? 100 : 0, "GARAGE DOOR PIN", now_rel, ok_fb);
        }
      }
    }
  } else if (home_hub_tab() == HomeHubTab::VACUUM && home_hub_state().vacuum_count > 0) {
    auto &hub = home_hub_state();
    int &v_idx = hub.vacuum_idx;
    v_idx = std::clamp(v_idx, 0, hub.vacuum_count - 1);
    if ((wid == WidgetId::VACUUM_PREV_BTN || wid == WidgetId::VACUUM_NEXT_BTN) &&
        touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_RAPID, tkey)) {
      if (hub.vacuum_count > 1) {
        v_idx = step_wrap(v_idx, (wid == WidgetId::VACUUM_NEXT_BTN) ? 1 : -1, hub.vacuum_count);
        ++hub.local_mutation_seq;
        mark_dirty_full();
        rt_.schedule_disp();
      }
    } else if (wid >= WidgetId::VACUUM_ACTION_BASE && wid < WidgetId::VACUUM_ACTION_BASE + 6 &&
               touch_router().allow_tap(now_rel, TOUCH_DEBOUNCE_BUTTON, tkey)) {
      static constexpr const char *kVacActs[6] = {
          "start", "pause", "return_to_base", "stop", "clean_spot", "locate"};
      static constexpr const char *kVacStates[6] = {
          "Cleaning", "Paused", "Returning", "Idle", "Cleaning", nullptr};
      static constexpr const char *kVacFb[6] = {
          "Vacuum Started", "Vacuum Paused", "Returning to Dock",
          "Vacuum Stopped", "Spot Cleaning", "Locating Vacuum..."};
      auto &v = hub.vacuums[v_idx];
      const int v_act = static_cast<int>(wid - WidgetId::VACUUM_ACTION_BASE);
      rs.area.target_entity_id = v.entity_id;
      if (kVacStates[v_act] != nullptr) v.state = kVacStates[v_act];
      rs.area.target_action = kVacActs[v_act];
      rs.area.action_feedback = kVacFb[v_act];
      ++hub.local_mutation_seq;
      rs.area.feedback_timer = now_rel;
      mark_dirty_full();
      TouchGestureEngine::do_execute_area_action_();
      rt_.schedule_disp();
    }
  }

  return true;
}

}  // namespace latter_ui
