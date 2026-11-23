#if __has_include("audio_router.h")
#include "audio_router.h"
#include "hal_display_lifecycle.h"
#include "hal_radio_policy.h"
#include "hal_sleep_pm.h"
#include "state/state_nvs_snapshot.h"
#include "latter_rpc.h"
#include "latter_actions.h"
#else
#include "../../include/audio_router.h"
#include "../../include/hal_display_lifecycle.h"
#include "../../include/hal_radio_policy.h"
#include "../../include/hal_sleep_pm.h"
#include "../../include/state/state_nvs_snapshot.h"
#include "../../include/latter_rpc.h"
#include "../../include/latter_actions.h"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>

namespace latter_ui {

// ==============================================================================
// 1. V3DomainReducer — Deterministic Protocol v3 Inbound Domain State Reducer
// ==============================================================================

void V3DomainReducer::reset() {
  *this = V3DomainReducer{};
}

V3DomainEffects V3DomainReducer::reduce(std::string_view sv_dom,
                                        const V3ReduceContext &ctx) {
  V3DomainEffects eff{};
  auto &r = runtime_state();
  auto &prefs = persisted_prefs();

  if (sv_dom == "area_context") {
    const auto &st = area_ctx_state();
    const bool area_icon_changed = (st.area_icon != prev_area_icon_);
    prev_area_icon_ = st.area_icon;
    const bool ambient_light_changed = (st.area_ambient_light != prev_area_ambient_light_);
    prev_area_ambient_light_ = st.area_ambient_light;

    r.area.active_area_name = st.area_name;
    latter_area_label() = st.area_name;

    const bool was_unassigned = r.area.active_area_id.empty();
    const bool area_changed = !st.area_id.empty() && st.area_id != r.area.active_area_id;
    eff.area_changed = area_changed;

    if (area_changed) {
      r.area.active_area_id = st.area_id;
      r.nav.ambient_bri_override_pct = -1;
      if (was_unassigned && r.nav.gui_page == 6 && r.nav.area_subpage == 1) {
        r.nav.area_subpage = 0;
        r.area.browse_mode = "entities";
      }
      r.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
      area_cat_dock_page() = 0;
      eff.mark_dirty_full = true;
    } else if (st.area_id.empty() && !r.area.active_area_id.empty()) {
      r.area.active_area_id.clear();
      r.nav.ambient_bri_override_pct = -1;
      eff.mark_dirty_full = true;
    } else if (area_icon_changed && r.nav.gui_page == 1) {
      eff.mark_dirty_full = true;
    }

    if ((ambient_light_changed || area_changed) &&
        std::string_view(pref_auto_brightness_mode_cstr(prefs)) == "Ambient Light" &&
        !prefs.bedtime_active) {
      eff.sync_display_brightness = true;
      eff.sync_display_brightness_smooth = (r.nav.display_power_state == 0);
      if (r.nav.quick_actions_open && r.nav.display_power_state == 0 && !r.nav.is_hibernating) {
        eff.schedule_disp_update = true;
      }
    }

    sync_audio_session_media_observation(ctx.local_mp_playing, ctx.local_mp_paused, ctx.now_ms);

    {
      const bool domains_changed = (st.available_domains != area_available_domains_csv());
      area_available_domains_csv() = st.available_domains;
      std::string cats[8];
      const int cnt = collect_area_categories(cats);
      if (cnt <= 0) {
        r.area.domain_filter.clear();
        auto *eslots = r.slots_for(latter_rpc::SlotChannel::AREA_ENTITIES);
        for (size_t si = 0; si < DeviceRuntimeState::kSlotsPerPage; ++si) {
          eslots[si] = BrowseSlotItem{};
        }
        r.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
        r.slot_total_pages_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
        area_cat_dock_page() = 0;
        if (domains_changed && r.nav.gui_page == 6 && r.nav.area_subpage == 0) {
          eff.mark_dirty_full = true;
        }
      } else {
        bool cur_ok = false;
        int act_idx = 0;
        for (int i = 0; i < cnt; i++) {
          if (cats[i] == r.area.domain_filter) {
            cur_ok = true;
            act_idx = i;
            break;
          }
        }
        if (area_changed || !cur_ok) {
          r.area.domain_filter = cats[0];
          act_idx = 0;
        }
        if ((area_changed || !cur_ok) && r.nav.gui_page == 6 && r.nav.area_subpage == 0) {
          r.slot_page_for(latter_rpc::SlotChannel::AREA_ENTITIES) = 1;
          eff.fetch_area_browse = true;
        }
        if (domains_changed && r.nav.gui_page == 6 && r.nav.area_subpage == 0) {
          eff.mark_dirty_full = true;
        }
        sync_mode_bar_window(area_cat_dock_page(), act_idx, cnt);
      }
    }

    {
      int hh_tabs[kMaxHomeHubTabs]{};
      const int hh_cnt = collect_home_hub_tabs(hh_tabs, st);
      if (hh_cnt > 0) {
        int active_slot = -1;
        for (int i = 0; i < hh_cnt; i++) {
          if (hh_tabs[i] == home_hub_tab()) {
            active_slot = i;
            break;
          }
        }
        if (active_slot < 0) {
          home_hub_tab() = hh_tabs[0];
          home_hub_mode_bar_page() = 0;
          if (r.nav.gui_page == PageId::HOME_HUB) eff.mark_dirty_full = true;
        } else {
          sync_mode_bar_window(home_hub_mode_bar_page(), active_slot, hh_cnt);
        }
      } else {
        home_hub_mode_bar_page() = 0;
        if (r.nav.gui_page == PageId::HOME_HUB) {
          eff.mark_dirty_full = true;
        }
      }
    }

    if (is_camera_view_active(r) &&
        prefs.camera_stream_mode == 2U &&
        r.alert.loaded_camera_url.empty() &&
        (!st.camera_snapshot_url.empty() || !st.cameras_summary.empty())) {
      eff.fetch_on_demand_camera = true;
    }

    r.area.alarm_entity = st.alarm_entity;
    r.area.alarm_state = st.alarm_state;
    if (!st.site_id.empty()) {
      latter_site_id() = st.site_id;
    }
    latter_ble_proxy() = st.ble_closest_proxy;
    cam_listen_active() = (st.doorbell_talk_state == "listening");

    if (trigger_alert_banner(st.alert_title, st.alert_message, st.alert_priority, st.alert_duration,
                             st.alert_camera_url, st.alert_id, ctx.now_ms, true, r.alert,
                             st.alert_camera_entity)) {
      const bool is_doorbell = !r.alert.camera_entity.empty() || !r.alert.camera_url.empty();
      const bool dnd_suppressed = should_suppress_alert_in_dnd(
          prefs.do_not_disturb, static_cast<int>(r.alert.priority_level), is_doorbell);
      const bool bedtime_suppressed =
          should_suppress_alert_in_bedtime(prefs.bedtime_active, r.alert.priority_level);
      if (dnd_suppressed || bedtime_suppressed) {
        if (dnd_suppressed || r.nav.display_power_state != 0) {
          r.alert.active = false;
        }
      } else {
        eff.alert_triggered = true;
        const bool chime_suppressed = should_suppress_alert_chime_in_dnd(
            prefs.do_not_disturb, static_cast<int>(r.alert.priority_level), is_doorbell);
        if (st.alert_chime && !chime_suppressed && !r.voice.active && !r.voice.tts_received) {
          eff.alert_sfx_id =
              (r.alert.priority == "critical" || r.alert.priority == "warning") ? 9 : 6;
        }
      }
      if (r.alert.active && is_p4_live_doorbell_popup_enabled(prefs, st, r.alert)) {
        r.cameras.doorbell_live_popup_active = true;
      } else {
        r.cameras.doorbell_live_popup_active = false;
      }
      if (r.alert.active && !r.alert.camera_url.empty() && !r.cameras.doorbell_live_popup_active) {
        eff.fetch_alert_camera = true;
      } else if (!r.alert.active) {
        r.alert.loaded_camera_url.clear();
        eff.release_alert_camera_art = true;
      }
    }

    bool wallpaper_changed = (st.wallpaper_url != prev_wallpaper_url_);
    prev_wallpaper_url_ = st.wallpaper_url;
    bool wallpaper_cleared = false;
    if (st.wallpaper_url.empty() || is_day_night_sky_wallpaper_url(st.wallpaper_url)) {
      if (!r.music.loaded_wallpaper_url.empty()) {
        r.music.loaded_wallpaper_url.clear();
        eff.stop_wallpaper_fetch = true;
        eff.release_wallpaper_art = true;
        eff.weather_sim_dirty = true;
        eff.ensure_anim_poller = true;
        wallpaper_cleared = true;
      } else if (wallpaper_changed) {
        eff.stop_wallpaper_fetch = true;
        eff.weather_sim_dirty = true;
        wallpaper_cleared = true;
      }
    } else if (r.nav.display_power_state == 0 && st.wallpaper_url != r.music.loaded_wallpaper_url) {
      eff.fetch_wallpaper = true;
    }

    if (st.screensaver_photo_url.empty()) {
      if (!r.music.loaded_screensaver_photo_url.empty()) {
        r.music.loaded_screensaver_photo_url.clear();
        eff.stop_screensaver_photo_fetch = true;
        eff.release_screensaver_photo_art = true;
        if (r.nav.display_power_state == 1 && !r.nav.is_hibernating) {
          eff.mark_dirty_full = true;
          eff.schedule_disp_update = true;
        }
      }
    } else if (r.nav.display_power_state == 1 && !r.nav.is_hibernating &&
               std::string_view(effective_screensaver_style_cstr(prefs, r, ctx.now_ms)) == "Photo Frame" &&
               st.screensaver_photo_url != r.music.loaded_screensaver_photo_url) {
      eff.fetch_screensaver_photo = true;
    }

    if (r.nav.display_power_state == 0 && !r.nav.is_hibernating) {
      const int gp = r.nav.gui_page;
      if (wallpaper_cleared || wallpaper_changed) {
        eff.mark_dirty_full = true;
      }
      if (wallpaper_cleared || wallpaper_changed || r.alert.active || gp == 1 || gp == 4 || gp == 6 || gp == 7 || gp == 9 || gp == 10 ||
          gp == PageId::PINNED || gp == PageId::CALENDAR || gp == PageId::CAMERAS || gp == PageId::WEATHER) {
        if (gp == 1 && r.nav.home_subpage == 0) {
          eff.ensure_anim_poller = true;
        }
        eff.schedule_disp_update = true;
      }
    }
  } else if (sv_dom == "active_media") {
    auto &ms = media_state();
    float prev_interp_pos = r.music.pos_base;
    if (r.music.pos_sync_time > 0 && prev_state_ == "playing") {
      prev_interp_pos += static_cast<float>(ctx.now_ms - r.music.pos_sync_time) / 1000.0f;
    }

    ms.playing = (ms.playback_state == MediaPlaybackState::Playing || ms.state == "playing");
    r.music.active_playback_media_type = ms.media_type;
    if (ms.position >= 0.0f) {
      r.music.pos_base = ms.position;
      r.music.pos_sync_time = ctx.now_ms;
      ms.position_updated_ms = ctx.now_ms;
    }
    if (!ms.active_speaker.empty()) {
      if (!r.music.active_speaker_entity.empty() &&
          r.music.active_speaker_entity != ms.active_speaker &&
          media_state().last_speaker.empty()) {
        media_state().last_speaker = r.music.active_speaker_entity;
      }
      r.music.active_speaker_entity = ms.active_speaker;
    }

    sync_audio_session_media_observation(ctx.local_mp_playing, ctx.local_mp_paused, ctx.now_ms);

    if (!ms.speaker_name.empty()) {
      r.music.active_speaker_name = ms.speaker_name;
    } else if (ms.is_local) {
      r.music.active_speaker_name = (ctx.friendly_name != nullptr) ? ctx.friendly_name : "";
    } else if (!ms.active_speaker.empty()) {
      std::string s = ms.active_speaker;
      const size_t dot = s.find('.');
      if (dot != std::string::npos) s = s.substr(dot + 1);
      bool cap = true;
      for (size_t i = 0; i < s.length(); i++) {
        if (s[i] == '_') {
          s[i] = ' ';
          cap = true;
        } else if (cap) {
          s[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
          cap = false;
        }
      }
      r.music.active_speaker_name = s;
    }

    if (ms.has_volume && ms.volume >= 0.0f && ms.volume <= 1.0f) {
      if (ctx.now_ms - last_local_vol_tap_ms() > 3000U || ms.active_speaker != prev_spk_) {
        remote_vol_pct() = static_cast<int>(std::round(ms.volume * 100.0f));
        if (ms.is_local) {
          eff.sync_local_volume = true;
          eff.local_volume_0_to_1 = ms.volume;
        }
      }
    }

    const bool track_changed = (ms.title != prev_title_) || (ms.artist != prev_artist_);
    if (ms.title != prev_title_) {
      r.music.action_feedback.clear();
      if (!ms.title.empty()) {
        music_queue_cleared() = false;
      }
    }
    if (track_changed && r.nav.display_power_state == 0 &&
        r.nav.gui_page == PageId::MUSIC && r.music.view == MusicView::LYRICS &&
        !ms.title.empty()) {
      eff.fetch_music_lyrics = true;
    }

    if (ms.art_url.empty()) {
      if (!r.music.loaded_art_url.empty()) {
        r.music.loaded_art_url.clear();
        eff.stop_remote_art_fetch = true;
        eff.release_remote_art = true;
      }
    } else if (r.nav.display_power_state == 0 && r.nav.gui_page == 2 &&
               (r.music.view == MusicView::LYRICS ||
                std::string_view(pref_album_art_style_cstr(prefs)) != "None") &&
               (!ms.is_local || ctx.local_album_art_w == 0) &&
               ms.art_url != r.music.loaded_art_url) {
      if (ms.art_url != prev_art_ || !ctx.remote_art_fetch_running) {
        eff.fetch_remote_art = true;
      }
    }

    const bool pos_jumped = (ms.position >= 0.0f) && (std::fabs(ms.position - prev_interp_pos) > 2.5f);
    const bool media_ui_changed =
        (ms.title != prev_title_) ||
        (ms.artist != prev_artist_) ||
        (ms.album != prev_album_) ||
        (ms.state != prev_state_) ||
        (ms.active_speaker != prev_spk_) ||
        (ms.speaker_name != prev_spk_name_) ||
        (ms.art_url != prev_art_) ||
        (ms.is_local != prev_is_local_) ||
        (remote_vol_pct() != prev_vol_pct_) ||
        (ms.shuffle != prev_shuffle_) ||
        (ms.repeat_mode != prev_repeat_mode_) ||
        (ms.muted != prev_muted_) ||
        (ms.favorite != prev_favorite_) ||
        (ms.queue_count != prev_queue_count_) ||
        (ms.media_caps != prev_media_caps_) ||
        pos_jumped;

    prev_title_ = ms.title;
    prev_artist_ = ms.artist;
    prev_album_ = ms.album;
    prev_state_ = ms.state;
    prev_spk_ = ms.active_speaker;
    prev_spk_name_ = ms.speaker_name;
    prev_art_ = ms.art_url;
    prev_is_local_ = ms.is_local;
    prev_vol_pct_ = remote_vol_pct();
    prev_shuffle_ = ms.shuffle;
    prev_repeat_mode_ = ms.repeat_mode;
    prev_muted_ = ms.muted;
    prev_favorite_ = ms.favorite;
    prev_queue_count_ = ms.queue_count;
    prev_media_caps_ = ms.media_caps;

    if (media_ui_changed && r.nav.display_power_state == 0 &&
        (r.nav.gui_page == 1 || r.nav.gui_page == 2)) {
      if (r.nav.gui_page == 1 && r.nav.home_subpage == 0) {
        eff.ensure_anim_poller = true;
      }
      eff.throttled_disp_update = true;
    }
  } else if (sv_dom == "remote_config") {
    const auto &rem = remote_state();
    parse_latter_remote_slots_summary(rem.slots_summary);
    r.remote.active_slot = resolve_active_remote_slot(rem.active_slot);
    home_dock_page() = 0;

    if (!has_any_configured_remote() && r.nav.gui_page == 7) {
      eff.navigate_home_from_remote = true;
    } else if (has_any_configured_remote() &&
               std::string_view(pref_start_screen_cstr(prefs)) == "Remote" &&
               ctx.last_touch_ms == 0U &&
               r.nav.gui_page != 7) {
      eff.navigate_start_screen_remote = true;
    }

    if (r.nav.display_power_state == 0 &&
        (r.nav.gui_page == 1 || r.nav.gui_page == 7) &&
        ctx.now_ms > 3000U) {
      eff.schedule_disp_update = true;
    }
  } else if (sv_dom == "weather_forecast") {
    if (weather_state().sun_state != prev_sun_) {
      prev_sun_ = weather_state().sun_state;
      eff.sync_display_brightness = true;
      eff.mark_dirty_full = true;
    }
    eff.weather_sim_dirty = true;
    eff.reset_ambient_frame_ms = true;
    if (r.nav.display_power_state == 0 && !r.nav.is_hibernating) {
      eff.ensure_anim_poller = true;
      eff.schedule_disp_update = true;
    }
  } else if (sv_dom == "calendar_agenda") {
    if (r.nav.display_power_state == 0 &&
        (r.nav.gui_page == 1 || r.nav.gui_page == PageId::CALENDAR)) {
      if (r.nav.gui_page == 1 && r.nav.home_subpage == 0) {
        eff.ensure_anim_poller = true;
      }
      eff.schedule_disp_update = true;
    }
  } else if (sv_dom == "device_prefs") {
    const int tot_tools_pg = std::max(1, active_tools_launcher_page_count(prefs));
    if (r.nav.tools_menu_page < 0 || r.nav.tools_menu_page >= tot_tools_pg) {
      r.nav.tools_menu_page = std::max(0, tot_tools_pg - 1);
    }
    const int tot_games = active_games_launcher_count(prefs);
    if (tot_games <= 0) {
      r.nav.game_carousel_idx = 0;
    } else if (r.nav.game_carousel_idx < 0 || r.nav.game_carousel_idx >= tot_games) {
      r.nav.game_carousel_idx = std::max(0, tot_games - 1);
    }
    eff.apply_device_prefs_effects = true;
  } else if (sv_dom == "shortcuts") {
    ShortcutItem *dock_buf = dock_items_scratch();
    const int dock_cnt = collect_effective_dock_items(dock_buf, kMaxEffectiveDockItems);
    const int max_dock_pages = std::max(1, (dock_cnt + 3) / 4);
    if (home_dock_page() < 0 || home_dock_page() >= max_dock_pages) {
      home_dock_page() = std::max(0, max_dock_pages - 1);
    }
    const int tot_pinned_pages = pinned_total_pages();
    if (r.nav.pinned_menu_page < 0 || r.nav.pinned_menu_page >= tot_pinned_pages) {
      r.nav.pinned_menu_page = std::max(0, tot_pinned_pages - 1);
    }
    if (r.nav.gui_page == PageId::HOME || r.nav.gui_page == PageId::PINNED) {
      eff.mark_dirty_full = true;
      if (r.nav.display_power_state == 0 && !r.nav.is_hibernating) {
        if (r.nav.gui_page == PageId::HOME && r.nav.home_subpage == 0) {
          eff.ensure_anim_poller = true;
        }
        eff.schedule_disp_update = true;
      }
    }
  }

  return eff;
}

V3DomainReducer &v3_domain_reducer() noexcept {
  static V3DomainReducer inst;
  return inst;
}

// ==============================================================================
// 2. TimerAlarmController — Unified Multi-Timer, VA Timer & Scheduled Alarm Engine
// ==============================================================================

bool TimerAlarmController::check_auto_silence_(uint32_t now_ms) noexcept {
  auto &rs = runtime_state();
  if (!rs.alarms.ringing && !rs.timers.ringing) {
    ringing_started_ms_ = 0U;
    return false;
  }
  if (ringing_started_ms_ == 0U) {
    stamp_ringing_start_(now_ms);
    return false;
  }
  if (static_cast<uint32_t>(now_ms - ringing_started_ms_) >= kAutoSilenceMs) {
    rs.alarms.ringing = false;
    rs.timers.ringing = false;
    ringing_started_ms_ = 0U;
    return true;
  }
  return false;
}

bool TimerAlarmController::should_redraw_va_timer_(const DeviceRuntimeState &rs) noexcept {
  if (rs.voice.active || !rs.voice.status_text.empty() || rs.nav.display_power_state != 0) {
    return false;
  }
  return (rs.nav.gui_page == 10 && rs.nav.time_subpage == 1) ||
         (rs.nav.gui_page == 1 && (rs.nav.home_subpage == 0 || rs.nav.home_subpage == 2));
}

TimerTickEffects TimerAlarmController::tick_timers_250ms(uint32_t now_ms) {
  TimerTickEffects eff{};
  auto &rs = runtime_state();

  if (rs.timers.running) {
    auto *slots = timer_slots();
    bool any_expired = false;
    std::string exp_name;
    bool sec_changed = false;

    for (int i = 0; i < 3; i++) {
      if (!slots[i].running) continue;
      if (now_ms >= slots[i].target_ms) {
        if (i == 0 && rs.showcase.is_active(now_ms) && rs.showcase.timer_owned) {
          const int dur_s = std::clamp(slots[0].duration_sec, 5, 3600);
          slots[0].duration_sec = dur_s;
          slots[0].remaining_sec = dur_s;
          slots[0].target_ms = now_ms + static_cast<uint32_t>(dur_s) * 1000U;
          sec_changed = true;
        } else {
          slots[i].running = false;
          slots[i].paused = false;
          slots[i].remaining_sec = slots[i].duration_sec;  // Preserve duration for 1-tap reuse!
          exp_name = slots[i].name;
          any_expired = true;
        }
      } else {
        const int rem = static_cast<int>((slots[i].target_ms - now_ms + 999U) / 1000U);
        if (rem != slots[i].remaining_sec) {
          slots[i].remaining_sec = rem;
          sec_changed = true;
        }
      }
    }

    sync_multi_timer_state();

    if (any_expired) {
      if (rs.nav.home_subpage == 2) {
        rs.nav.home_subpage = 0;
      }
      rs.timers.va_timer_name = exp_name;
      rs.timers.ringing = true;
      stamp_ringing_start_(now_ms);
      eff.any_expired = true;
      return eff;
    }

    eff.sec_changed = sec_changed;
  }

  eff.auto_silenced = check_auto_silence_(now_ms);
  return eff;
}

AlarmTickEffects TimerAlarmController::tick_alarms_1s(bool time_valid, uint32_t now_epoch,
                                                      int hour, int minute, int second,
                                                      int day_of_week, uint32_t now_ms) {
  AlarmTickEffects eff{};
  eff.auto_silenced = check_auto_silence_(now_ms);
  if (eff.auto_silenced || !time_valid) {
    return eff;
  }

  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();

  // 1. Snooze expiration check
  if (rs.alarms.snooze_until > 0 && now_epoch >= rs.alarms.snooze_until) {
    if (rs.nav.home_subpage == 2) {
      rs.nav.home_subpage = 0;
    }
    rs.alarms.snooze_until = 0;
    if (rs.alarms.snoozed_alarm_slot >= 1 && rs.alarms.snoozed_alarm_slot <= 3) {
      rs.alarms.ringing_idx = rs.alarms.snoozed_alarm_slot;
    }
    rs.alarms.snoozed_alarm_slot = 0;
    rs.alarms.ringing = true;
    stamp_ringing_start_(now_ms);
    eff.snooze_fired = true;
    return eff;
  }

  // 2. Edge-triggered scheduled alarms check (45s window within target minute)
  const uint32_t em = now_epoch / 60U;
  if (second >= 0 && second < 45 && em != rs.alarms.last_fired_epoch_min) {
    auto check_alarm = [&](const PersistedAlarmPref &ap) -> bool {
      if (!ap.enabled) return false;
      if (hour != static_cast<int>(ap.hour) || minute != static_cast<int>(ap.minute)) return false;
      // Recurrence: 0=Daily, 1=Weekdays, 2=Weekends, 3=Once
      // ESPHome ESPTime::day_of_week: 1=SUN, 2=MON, ..., 6=FRI, 7=SAT
      if (ap.recurrence == 1) {
        if (day_of_week < 2 || day_of_week > 6) return false;
      } else if (ap.recurrence == 2) {
        if (day_of_week != 1 && day_of_week != 7) return false;
      }
      return true;
    };

    int trig_idx = 0;
    bool need_save = false;
    for (int i = 0; i < 3; i++) {
      if (check_alarm(prefs.alarms[i])) {
        trig_idx = i + 1;
        if (prefs.alarms[i].recurrence == 3) {
          prefs.alarms[i].enabled = false;  // Once: auto-disable
          need_save = true;
        }
        break;
      }
    }

    if (trig_idx > 0) {
      if (need_save) {
        latter_actions::sync_local_alarms_edit(now_ms);
        eff.prefs_saved = true;
      }
      if (rs.nav.home_subpage == 2) {
        rs.nav.home_subpage = 0;
      }
      rs.alarms.last_fired_epoch_min = em;
      rs.alarms.ringing = true;
      rs.alarms.ringing_idx = trig_idx;
      if (rs.alarms.snoozed_alarm_slot == trig_idx) {
        rs.alarms.snooze_until = 0;
        rs.alarms.snoozed_alarm_slot = 0;
      }
      stamp_ringing_start_(now_ms);
      eff.triggered_idx = trig_idx;
    }
  }

  return eff;
}

VaTimerEffects TimerAlarmController::on_va_timer_started(const std::string &id,
                                                         const std::string &name,
                                                         int total_sec, int left_sec,
                                                         bool is_active, uint32_t now_ms) {
  VaTimerEffects eff{};
  auto &rs = runtime_state();
  upsert_voice_timer_slot(id, name, total_sec, left_sec, is_active, now_ms);
  rs.timers.va_timer_name = name;
  sync_multi_timer_state();
  rs.timers.ringing = false;
  if (!rs.alarms.ringing) ringing_started_ms_ = 0U;
  latter_actions::check_and_emit_timer_alarm_state(now_ms);
  eff.schedule_disp = should_redraw_va_timer_(rs);
  return eff;
}

VaTimerEffects TimerAlarmController::on_va_timer_updated(const std::string &id,
                                                         const std::string &name,
                                                         int total_sec, int left_sec,
                                                         bool is_active, uint32_t now_ms) {
  VaTimerEffects eff{};
  const auto &rs = runtime_state();
  upsert_voice_timer_slot(id, name, total_sec, left_sec, is_active, now_ms);
  sync_multi_timer_state();
  latter_actions::check_and_emit_timer_alarm_state(now_ms);
  eff.schedule_disp = should_redraw_va_timer_(rs);
  return eff;
}

VaTimerEffects TimerAlarmController::on_va_timer_cancelled(const std::string &id,
                                                           const std::string &name,
                                                           uint32_t now_ms) {
  VaTimerEffects eff{};
  auto &rs = runtime_state();
  auto *slots = timer_slots();
  for (int i = 0; i < 3; i++) {
    if ((!id.empty() && slots[i].id == id) || (!name.empty() && slots[i].name == name)) {
      slots[i].running = false;
      slots[i].paused = false;
      slots[i].remaining_sec = slots[i].duration_sec;
    }
  }
  sync_multi_timer_state();
  if (rs.nav.home_subpage == 2 && !rs.timers.running) {
    rs.nav.home_subpage = 0;
  }
  const bool was_ringing = rs.timers.ringing;
  rs.timers.ringing = false;
  if (!rs.alarms.ringing) ringing_started_ms_ = 0U;
  latter_actions::check_and_emit_timer_alarm_state(now_ms);
  eff.stop_sound = was_ringing;
  eff.schedule_disp = should_redraw_va_timer_(rs);
  return eff;
}

VaTimerEffects TimerAlarmController::on_va_timer_finished(const std::string &id,
                                                          const std::string &name,
                                                          uint32_t now_ms) {
  VaTimerEffects eff{};
  auto &rs = runtime_state();
  auto *slots = timer_slots();
  std::string fired_name = name;
  for (int i = 0; i < 3; i++) {
    if ((!id.empty() && slots[i].id == id) || (!name.empty() && slots[i].name == name)) {
      if (fired_name.empty()) fired_name = slots[i].name;
      slots[i].running = false;
      slots[i].paused = false;
      slots[i].remaining_sec = slots[i].duration_sec;  // Keep duration & name saved as a reusable preset!
    }
  }
  sync_multi_timer_state();
  if (rs.nav.home_subpage == 2) {
    rs.nav.home_subpage = 0;
  }
  if (!rs.timers.ringing) {
    rs.timers.va_timer_name = fired_name;
    rs.timers.ringing = true;
    stamp_ringing_start_(now_ms);
    eff.fire_alert = true;
  }
  latter_actions::check_and_emit_timer_alarm_state(now_ms);
  return eff;
}

bool TimerAlarmController::snooze_active_alarm(bool time_valid, uint32_t now_epoch,
                                               uint32_t now_ms) noexcept {
  auto &rs = runtime_state();
  if (!rs.alarms.ringing) return false;
  rs.alarms.snoozed_alarm_slot = rs.alarms.ringing_idx;
  rs.alarms.snooze_until = (time_valid ? now_epoch : (now_ms / 1000U)) + kSnoozeSeconds;
  rs.alarms.ringing = false;
  if (!rs.timers.ringing) ringing_started_ms_ = 0U;
  return true;
}

bool TimerAlarmController::dismiss_ringing() noexcept {
  auto &rs = runtime_state();
  if (!rs.timers.ringing && !rs.alarms.ringing && !rs.alarms.find_my_active) {
    return false;
  }
  rs.timers.ringing = false;
  rs.alarms.ringing = false;
  rs.alarms.snoozed_alarm_slot = 0;
  rs.alarms.find_my_active = false;
  ringing_started_ms_ = 0U;
  return true;
}

TimerAlarmController &timer_alarm_controller() noexcept {
  static TimerAlarmController inst;
  return inst;
}

// ==============================================================================
// 3. Deterministic Runtime State & Controller Reset
// ==============================================================================

void reset_runtime_state(bool preserve_hooks) {
  auto &rt = runtime_state();
  const auto saved_boot_chk = rt.hw_boot_battery_check;
  const auto saved_init_pmu = rt.hw_init_pmu;
  const auto saved_poll_pmu = rt.hw_poll_pmu_1s;
  const auto saved_wait_key = rt.hw_wait_pwr_key_release;
  const auto saved_key_pressed = rt.hw_pwr_key_pressed;
  const auto saved_upd_batt = rt.hw_update_batt_level;
  const auto saved_imu_pm = rt.hw_set_imu_power_mode;
  const auto saved_on_conn = rt.on_ha_client_connected;
  const auto saved_on_disc = rt.on_ha_client_disconnected;
  const auto saved_on_push = rt.on_v3_domain_pushed;
  const auto saved_on_cmd = rt.on_device_command;
  const auto saved_on_hello = rt.on_hello_sent;

  rt = DeviceRuntimeState{};
  if (preserve_hooks) {
    rt.hw_boot_battery_check = saved_boot_chk;
    rt.hw_init_pmu = saved_init_pmu;
    rt.hw_poll_pmu_1s = saved_poll_pmu;
    rt.hw_wait_pwr_key_release = saved_wait_key;
    rt.hw_pwr_key_pressed = saved_key_pressed;
    rt.hw_update_batt_level = saved_upd_batt;
    rt.hw_set_imu_power_mode = saved_imu_pm;
    rt.on_ha_client_connected = saved_on_conn;
    rt.on_ha_client_disconnected = saved_on_disc;
    rt.on_v3_domain_pushed = saved_on_push;
    rt.on_device_command = saved_on_cmd;
    rt.on_hello_sent = saved_on_hello;
  }

  link_state() = LinkState{};
  home_hub_state() = HomeHubState{};
  area_ctx_state() = AreaContextState{};
  media_state() = MediaState{};
  remote_state() = RemoteState{};
  weather_state() = WeatherState{};
  calendar_state() = CalendarState{};
  reset_calendar_window_store();
  shortcuts_state() = ShortcutsState{};
  device_prefs_state() = DevicePrefsState{};
  persisted_prefs() = PersistedDevicePrefs{};
  clamp_prefs_to_capabilities(persisted_prefs());
  v3_mismatch_warn_count() = 0;

  va_display_restore() = VaDisplayRestore{};
  radio_boost_until_ms() = 0U;
  screen_off_tap_state().reset();
  bedtime_schedule_tracker().reset();
  latter_actions::timer_alarm_sync_tracker() = latter_actions::TimerAlarmSyncTracker{};
  latter_rpc::clear_pin_session_key();

  audio_session().reset();
  audio_arbitrator().reset();
  intercom_session().reset();
  display_lifecycle().reset();
  v3_domain_reducer().reset();
  timer_alarm_controller().reset();

  auto &rpc = latter_rpc::rpc_state();
  void *saved_sink_ctx = rpc.sink_ctx;
  auto saved_sink_fn = rpc.sink_fn;
  rpc = latter_rpc::RpcQueueState{};
  if (preserve_hooks) {
    rpc.sink_ctx = saved_sink_ctx;
    rpc.sink_fn = saved_sink_fn;
  }
  (void)latter_rpc::rpc_state();  // Re-seed boot_id

#if !LATTER_HAS_ESP_PREFS
  reset_hibernate_snapshot_for_test();
#endif
}

}  // namespace latter_ui
