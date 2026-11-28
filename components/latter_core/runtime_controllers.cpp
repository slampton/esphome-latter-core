#if __has_include("audio_router.h")
#include "audio_router.h"
#include "hal_battery.h"
#include "hal_display_lifecycle.h"
#include "hal_link_report.h"
#include "hal_radio_policy.h"
#include "hal_sleep_pm.h"
#include "hal_video_stream.h"
#include "latter_prof.h"
#include "state/state_nvs_snapshot.h"
#include "latter_rpc.h"
#include "latter_actions.h"
#else
#include "../../include/audio_router.h"
#include "../../include/hal_battery.h"
#include "../../include/hal_display_lifecycle.h"
#include "../../include/hal_link_report.h"
#include "../../include/hal_radio_policy.h"
#include "../../include/hal_sleep_pm.h"
#include "../../include/hal_video_stream.h"
#include "../../include/latter_prof.h"
#include "../../include/state/state_nvs_snapshot.h"
#include "../../include/latter_rpc.h"
#include "../../include/latter_actions.h"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>

namespace latter_core {

// ==============================================================================
// 1. V3DomainReducer — Deterministic Protocol v3 Inbound Domain State Reducer
// ==============================================================================

void V3DomainReducer::reset() {
  *this = V3DomainReducer{};
}

MusicAutoOpenResult try_auto_open_music_on_playback_start(
    DeviceRuntimeState &rs,
    const MediaState &ms,
    const PersistedDevicePrefs &prefs,
    uint32_t now_ms,
    bool pin_pad_open,
    int local_album_art_w,
    bool is_local_play_event,
    uint32_t last_touch_ms) {
  MusicAutoOpenResult res{};
  const MusicAutoOpenMode mode = prefs.music_auto_open_enum();
  if (mode == MusicAutoOpenMode::Off) {
    return res;
  }

  if (is_local_play_event) {
    const bool local_is_default =
        ms.active_speaker.empty() || (ms.is_local && ms.is_default_speaker);
    if (!local_is_default) {
      return res;
    }
  } else if (!ms.is_default_speaker) {
    return res;
  }

  const bool alert_ring =
      rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active || rs.alert.active;
  const bool ota_or_hib = rs.nav.ota_in_progress || rs.nav.is_hibernating;
  const bool va_busy = rs.voice.active || rs.voice.tts_received;
  const bool overlay_busy =
      pin_pad_open || rs.nav.shortcut_confirm.open || rs.nav.quick_actions_open;
  const bool game_busy = (rs.nav.gui_page == PageId::GAMES && rs.nav.active_game > 0);
  const bool cam_busy =
      rs.intercom.cam_call_active || rs.intercom.cam_listen_active ||
      rs.intercom.cam_intercom_ptt_active;
  const bool cal_busy =
      (rs.nav.gui_page == PageId::CALENDAR &&
       (rs.cal.drilldown >= CalDrilldown::ADD_STEP1 || rs.cal.dictation_field != 0U ||
        rs.cal.delete_confirm_open));
  const bool showcase_busy = rs.showcase.is_active(now_ms);

  if (alert_ring || ota_or_hib || va_busy || overlay_busy || game_busy || cam_busy ||
      cal_busy || showcase_busy) {
    return res;
  }

  const int start_page = resolve_start_screen_page(prefs);
  if (!audio_session().should_auto_open_music_page(
          rs.nav.gui_page, prefs.home_screen_enabled, start_page, alert_ring, now_ms,
          ota_or_hib)) {
    return res;
  }

  // Respect active Music browsing/settings subviews when the screen is already awake.
  if (rs.nav.gui_page == PageId::MUSIC && rs.nav.display_power_state == 0) {
    if (rs.music.view != MusicView::NOW_PLAYING && rs.music.view != MusicView::LYRICS) {
      return res;
    }
    const bool recent_local_tap =
        (last_touch_ms != 0U) && (static_cast<uint32_t>(now_ms - last_touch_ms) < 3000U);
    if (rs.music.view == MusicView::NOW_PLAYING && mode == MusicAutoOpenMode::Lyrics &&
        recent_local_tap) {
      return res;
    }
  }

  const bool will_be_awake =
      (rs.nav.display_power_state == 0) ||
      (rs.nav.display_power_state == 1 && !prefs.bedtime_active);

  rs.nav.gui_page = PageId::MUSIC;
  rs.nav.home_subpage = 0;
  rs.nav.active_game = 0;
  rs.music.reset_overlays();
  rs.music.direct_nav = false;

  const std::string cur_key = build_music_track_key(ms.title, ms.artist);
  const bool is_speech = is_speech_media_type(ms.media_type);

  if (mode == MusicAutoOpenMode::Lyrics) {
    rs.music.lyrics_auto_managed = true;
    if (is_speech) {
      rs.music.view = MusicView::NOW_PLAYING;
      rs.music.lyrics_auto_fallback = true;
    } else if (!cur_key.empty() && rs.music.lyrics.track_key == cur_key &&
               !rs.music.lyrics.loading) {
      if (rs.music.lyrics.lines.empty()) {
        rs.music.view = MusicView::NOW_PLAYING;
        rs.music.lyrics_auto_fallback = true;
      } else {
        rs.music.view = MusicView::LYRICS;
        rs.music.lyrics_auto_fallback = false;
      }
    } else {
      rs.music.view = MusicView::LYRICS;
      rs.music.lyrics_auto_fallback = false;
    }
  } else {
    rs.music.view = MusicView::NOW_PLAYING;
    rs.music.clear_lyrics_auto_state();
  }
  rs.music.sync_carousel_for_view();
  res.opened = true;

  if (rs.nav.display_power_state == 1 && !prefs.bedtime_active) {
    res.wake_from_screensaver = true;
  }

  if (will_be_awake) {
    res.mark_dirty_full = true;
    if (!ms.art_url.empty() &&
        (rs.music.view == MusicView::LYRICS ||
         std::string_view(pref_album_art_style_cstr(prefs)) != "None") &&
        (!ms.is_local || local_album_art_w == 0) &&
        ms.art_url != rs.music.loaded_art_url) {
      res.fetch_remote_art = true;
    }
    if (rs.music.view == MusicView::LYRICS && !is_speech && !cur_key.empty() &&
        rs.music.lyrics.track_key != cur_key) {
      auto &lyr = rs.music.lyrics;
      lyr.track_key = cur_key;
      lyr.loading = true;
      lyr.lines.clear();
      lyr.active_line_idx = -1;
      lyr.scroll_px = 0;
      lyr.target_scroll_px = 0;
      lyr.scroll_anim_start_ms = 0U;
      res.fetch_music_lyrics = true;
    }
  }

  return res;
}

LyricsAutoViewResult reconcile_lyrics_auto_view(
    DeviceRuntimeState &rs,
    const MediaState &ms,
    const PersistedDevicePrefs &prefs) {
  LyricsAutoViewResult res{};
  if (rs.nav.gui_page != PageId::MUSIC || !rs.music.lyrics_auto_managed ||
      prefs.music_auto_open_enum() != MusicAutoOpenMode::Lyrics ||
      rs.music.drilldown != MusicDrilldown::NONE ||
      rs.music.takeover != MusicTakeover::NONE) {
    return res;
  }

  const std::string cur_key = build_music_track_key(ms.title, ms.artist);
  if (cur_key.empty() || rs.music.lyrics.track_key != cur_key || rs.music.lyrics.loading) {
    return res;
  }

  if (rs.music.view == MusicView::LYRICS && rs.music.lyrics.lines.empty()) {
    rs.music.view = MusicView::NOW_PLAYING;
    rs.music.lyrics_auto_fallback = true;
    rs.music.sync_carousel_for_view();
    res.view_changed = true;
    if (!ms.art_url.empty() &&
        std::string_view(pref_album_art_style_cstr(prefs)) != "None" &&
        ms.art_url != rs.music.loaded_art_url) {
      res.fetch_remote_art = true;
    }
  } else if (rs.music.view == MusicView::NOW_PLAYING && rs.music.lyrics_auto_fallback &&
             !rs.music.lyrics.lines.empty() && !is_speech_media_type(ms.media_type)) {
    rs.music.view = MusicView::LYRICS;
    rs.music.lyrics_auto_fallback = false;
    rs.music.sync_carousel_for_view();
    res.view_changed = true;
  }
  return res;
}

V3DomainEffects V3DomainReducer::reduce(std::string_view sv_dom,
                                        const V3ReduceContext &ctx) {
  V3DomainEffects eff{};
  auto &r = runtime_state();
  auto &prefs = persisted_prefs();

  if (sv_dom == "area_context") {
    const auto &st = area_ctx_state();
    const bool first_area_ctx = !area_ctx_primed_;
    area_ctx_primed_ = true;
    const bool area_icon_changed = (st.area_icon != prev_area_icon_);
    prev_area_icon_ = st.area_icon;
    const bool ambient_light_changed = (st.area_ambient_light != prev_area_ambient_light_);
    prev_area_ambient_light_ = st.area_ambient_light;

    r.area.active_area_name = st.area_name;
    latter_area_label() = st.area_name;

    const bool was_unassigned = r.area.active_area_id.empty();
    const bool area_changed = !st.area_id.empty() && st.area_id != r.area.active_area_id;
    const bool area_cleared = !area_changed && st.area_id.empty() && !r.area.active_area_id.empty();
    auto same_opt_float = [](float a, float b) noexcept -> bool {
      if (std::isnan(a) && std::isnan(b)) return true;
      if (std::isnan(a) || std::isnan(b)) return false;
      return std::fabs(a - b) < 0.05f;
    };
    const bool area_name_changed = (st.area_name != prev_area_name_);
    prev_area_name_ = st.area_name;
    const bool area_temp_changed = !same_opt_float(st.area_temp, prev_area_temp_);
    prev_area_temp_ = st.area_temp;
    const bool area_humidity_changed = !same_opt_float(st.area_humidity, prev_area_humidity_);
    prev_area_humidity_ = st.area_humidity;
    const bool climates_changed = (st.area_climates_summary != prev_climates_summary_);
    prev_climates_summary_ = st.area_climates_summary;

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
    } else if (area_cleared) {
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

    const bool domains_changed = (st.available_domains != area_available_domains_csv());
    {
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
      const bool area_page_changed =
          area_changed || area_cleared || domains_changed || area_name_changed ||
          area_temp_changed || area_humidity_changed || climates_changed;
      if (wallpaper_cleared || wallpaper_changed || first_area_ctx) {
        eff.mark_dirty_full = true;
      }
      if (wallpaper_cleared || wallpaper_changed || first_area_ctx || r.alert.active || gp == 1 || gp == 4 ||
          (gp == 6 && area_page_changed) || gp == 7 || gp == 9 ||
          (gp == 10 && r.nav.time_subpage == 0) ||
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

    const bool now_default_playing = ms.is_default_speaker && ms.playing;
    bool auto_opened_now = false;
    if (!active_media_primed_) {
      active_media_primed_ = true;
      prev_default_playing_ = now_default_playing;
    } else {
      if (prev_default_playing_ && !now_default_playing) {
        audio_session().note_local_playback_stopped();
      } else if (!prev_default_playing_ && now_default_playing) {
        const auto ao = try_auto_open_music_on_playback_start(
            r, ms, prefs, ctx.now_ms, ctx.pin_pad_open, ctx.local_album_art_w,
            /*is_local_play_event=*/false, ctx.last_touch_ms);
        if (ao.opened) {
          auto_opened_now = true;
          if (ao.wake_from_screensaver) eff.wake_from_screensaver = true;
          if (ao.mark_dirty_full) {
            eff.mark_dirty_full = true;
            eff.ensure_anim_poller = true;
            eff.schedule_disp_update = true;
          }
          if (ao.fetch_remote_art) eff.fetch_remote_art = true;
          if (ao.fetch_music_lyrics) eff.fetch_music_lyrics = true;
        }
      }
      prev_default_playing_ = now_default_playing;
    }

    if (!auto_opened_now && track_changed && r.nav.gui_page == PageId::MUSIC && !ms.title.empty()) {
      const bool is_speech = is_speech_media_type(ms.media_type);
      const std::string cur_key = build_music_track_key(ms.title, ms.artist);
      if (r.music.view == MusicView::LYRICS) {
        if (is_speech && r.music.lyrics_auto_managed &&
            prefs.music_auto_open_enum() == MusicAutoOpenMode::Lyrics) {
          r.music.view = MusicView::NOW_PLAYING;
          r.music.lyrics_auto_fallback = true;
          r.music.sync_carousel_for_view();
          eff.mark_dirty_full = true;
          if (r.nav.display_power_state == 0) {
            eff.schedule_disp_update = true;
          }
        } else if (!is_speech && r.music.lyrics.track_key == cur_key && !r.music.lyrics.loading) {
          if (r.music.lyrics.lines.empty() && r.music.lyrics_auto_managed &&
              prefs.music_auto_open_enum() == MusicAutoOpenMode::Lyrics) {
            r.music.view = MusicView::NOW_PLAYING;
            r.music.lyrics_auto_fallback = true;
            r.music.sync_carousel_for_view();
            eff.mark_dirty_full = true;
            if (r.nav.display_power_state == 0) {
              eff.schedule_disp_update = true;
            }
          }
        } else if (r.nav.display_power_state == 0 && !is_speech) {
          auto &lyr = r.music.lyrics;
          lyr.track_key = cur_key;
          lyr.loading = true;
          lyr.lines.clear();
          lyr.active_line_idx = -1;
          lyr.scroll_px = 0;
          lyr.target_scroll_px = 0;
          lyr.scroll_anim_start_ms = 0U;
          eff.fetch_music_lyrics = true;
        }
      } else if (r.music.view == MusicView::NOW_PLAYING &&
                 r.music.lyrics_auto_managed && r.music.lyrics_auto_fallback &&
                 prefs.music_auto_open_enum() == MusicAutoOpenMode::Lyrics &&
                 !is_speech) {
        if (r.music.lyrics.track_key == cur_key && !r.music.lyrics.loading) {
          if (!r.music.lyrics.lines.empty()) {
            r.music.view = MusicView::LYRICS;
            r.music.lyrics_auto_fallback = false;
            r.music.sync_carousel_for_view();
            eff.mark_dirty_full = true;
            if (r.nav.display_power_state == 0) {
              eff.ensure_anim_poller = true;
              eff.schedule_disp_update = true;
            }
          }
        } else if (r.nav.display_power_state == 0) {
          // Stay on Now Playing while checking whether the new track has lyrics;
          // reconcile_lyrics_auto_view() flips back to Lyrics iff non-empty lyrics arrive.
          eff.fetch_music_lyrics = true;
        }
      }
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
      eff.sync_display_brightness_smooth = (r.nav.display_power_state == 0);
      eff.mark_dirty_full = true;
    }
    eff.weather_sim_dirty = true;
    eff.reset_ambient_frame_ms = true;
    if (r.nav.display_power_state == 0 && !r.nav.is_hibernating) {
      eff.mark_dirty_full = true;
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
        latter_core::sync_local_pref_key_edit(latter_core::PrefKey::ALARMS_SUMMARY);
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
  latter_core::check_and_emit_timer_alarm_state(now_ms);
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
  latter_core::check_and_emit_timer_alarm_state(now_ms);
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
  latter_core::check_and_emit_timer_alarm_state(now_ms);
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
  latter_core::check_and_emit_timer_alarm_state(now_ms);
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
  const auto saved_key_pressed = rt.hw_pwr_key_pressed;
  const auto saved_upd_batt = rt.hw_update_batt_level;
  const auto saved_imu_pm = rt.hw_set_imu_power_mode;
  const auto saved_bl_lvl = rt.hw_set_backlight_level;
  const auto saved_pwr_press = rt.on_pwr_key_press;
  const auto saved_pwr_rel = rt.on_pwr_key_release;
  const auto saved_usb_chg = rt.on_usb_power_changed;
  const auto saved_batt_stat = rt.on_battery_status_updated;
  const auto saved_batt_samp = rt.on_battery_sample_updated;
  const auto saved_mot_poll = rt.on_motion_poller_changed;
  const auto saved_wom_wake = rt.on_imu_wom_wake;
  const auto saved_audio = rt.audio;

  rt = DeviceRuntimeState{};
  if (preserve_hooks) {
    rt.hw_boot_battery_check = saved_boot_chk;
    rt.hw_init_pmu = saved_init_pmu;
    rt.hw_poll_pmu_1s = saved_poll_pmu;
    rt.hw_pwr_key_pressed = saved_key_pressed;
    rt.hw_update_batt_level = saved_upd_batt;
    rt.hw_set_imu_power_mode = saved_imu_pm;
    rt.hw_set_backlight_level = saved_bl_lvl;
    rt.on_pwr_key_press = saved_pwr_press;
    rt.on_pwr_key_release = saved_pwr_rel;
    rt.on_usb_power_changed = saved_usb_chg;
    rt.on_battery_status_updated = saved_batt_stat;
    rt.on_battery_sample_updated = saved_batt_samp;
    rt.on_motion_poller_changed = saved_mot_poll;
    rt.on_imu_wom_wake = saved_wom_wake;
    rt.audio = saved_audio;
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
  latter_core::timer_alarm_sync_tracker() = latter_core::TimerAlarmSyncTracker{};
  latter_rpc::clear_pin_session_key();

  audio_session().reset();
  audio_arbitrator().reset();
  audio_1s_supervisor().reset();
  intercom_session().reset();
  display_lifecycle().reset();
  v3_domain_reducer().reset();
  timer_alarm_controller().reset();
  camera_supervisor_controller().reset();

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

// ==============================================================================
// 4. API Session Helpers, Remote Screen Brightness & Profiler Window Reset
// ==============================================================================

void apply_api_ha_connected_state(uint32_t now_ms) {
  auto &r = runtime_state();
  const bool was_connected = link_state().ha_connected;
  reset_all_v3_rev_guards();
  reset_hello_sync_on_connect();
  mark_hello_in_flight(true, now_ms);
  link_state().ha_connected = true;
  link_state().last_ha_connect_ms = now_ms;
  r.alert.last_seen_alert_id = -1;
  if (!was_connected) {
    r.nav.last_pub_display_power_state = -1;
    r.nav.last_pub_display_bri_pct = -1;
  }
}

void apply_api_ha_disconnected_state(bool is_ha, uint32_t now_ms) {
  if (is_ha) {
    mark_hello_in_flight(false);
  }
  link_report_api_disconnected(is_ha, now_ms);
  if (is_ha && link_report().ha_clients == 0U) {
    link_state().ha_connected = false;
    link_state().last_ha_disconnect_ms = now_ms;
  }
}

bool apply_remote_screen_brightness_cmd(std::string_view sv_cmd,
                                        uint32_t now_ms,
                                        bool panel_is_on,
                                        bool &out_need_wake) {
  out_need_wake = false;
  static constexpr std::string_view kPrefix = "set_screen_bri:";
  if (sv_cmd.size() <= kPrefix.size() || !sv_cmd.starts_with(kPrefix)) {
    return false;
  }
  const std::string_view pct_sv = sv_cmd.substr(kPrefix.size());
  if (pct_sv.empty()) {
    return false;
  }
  int parsed_pct = 0;
  for (char ch : pct_sv) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    if (parsed_pct <= 1000) {
      parsed_pct = parsed_pct * 10 + (ch - '0');
    }
  }
  const int pct = std::clamp(parsed_pct, 1, 100);
  auto &r = runtime_state();
  if (persisted_prefs().auto_brightness_enum() == AutoBrightnessMode::Disabled) {
    r.nav.ambient_bri_override_pct = -1;
    uint32_t bri_eff = 0U;
    (void)record_local_pref_edit(
        PrefKey::MANUAL_BRIGHTNESS, std::to_string(pct), now_ms, bri_eff);
  } else {
    r.nav.ambient_bri_override_pct = static_cast<int8_t>(pct);
  }
  r.nav.last_pub_display_power_state = -1;
  r.nav.last_pub_display_bri_pct = -1;
  out_need_wake = (r.nav.display_power_state != 0) || !panel_is_on;
  return true;
}

void reset_flight_recorder_window(uint32_t now_ms) noexcept {
  auto &fr = flight_recorder();
  fr.window_frames = 0U;
  fr.window_render_us = 0U;
  fr.max_render_us = 0U;
  fr.slow_frames = 0U;
  fr.last_summary_ms = now_ms;
}

// ==============================================================================
// 5. CameraSupervisorController — 250 ms Camera & Doorbell Popup Supervisor
// ==============================================================================

CameraSupervisorActions CameraSupervisorController::step_250ms(uint32_t now_ms,
                                                                 bool touch_active) {
  CameraSupervisorActions act{};
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &ac = area_ctx_state();
  const bool cam_active_now = is_camera_view_active(rs);
  const bool live_popup_now = rs.alert.active && rs.cameras.doorbell_live_popup_active;
  const bool screen_ok = (rs.nav.display_power_state == 0) && !rs.nav.is_hibernating &&
                         !rs.nav.ota_in_progress && !rs.alarms.ringing &&
                         !rs.timers.ringing && !rs.alarms.find_my_active;

  if (!screen_ok || (!cam_active_now && !live_popup_now)) {
    if (video_client().is_active()) {
      act.stop_video = true;
    }
    return act;
  }

  if (cam_active_now) {
    if (!cam_intercom_status().empty() && rs.intercom.cam_status_ms > 0U &&
        (now_ms - rs.intercom.cam_status_ms > 2500U)) {
      cam_intercom_status().clear();
      act.schedule_disp = true;
    }

    // 6 s auto-hide HUD on Single / Carousel (never hides while talking, listening, or quick-reply open)
    if ((rs.cameras.tab == CamerasTab::SINGLE ||
         rs.cameras.tab == CamerasTab::CAROUSEL) &&
        rs.cameras.hud_active && !rs.cameras.quick_reply_open &&
        !cam_call_active() && !cam_listen_active() &&
        !rs.voice.intercom_turn && !touch_active &&
        (now_ms - rs.cameras.hud_timer >= 6000U)) {
      rs.cameras.hud_active = false;
      act.mark_dirty_full = true;
      act.schedule_disp = true;
    }

    // Automatic Carousel dwell rotation
    if (rs.cameras.tab == CamerasTab::CAROUSEL &&
        !rs.cameras.carousel_paused && !rs.cameras.quick_reply_open) {
      CameraRecord cams[MAX_CAMERA_SLOTS]{};
      const int c_cnt = parse_cameras_records(ac.cameras_summary, cams, MAX_CAMERA_SLOTS);
      int car_idx[MAX_CAMERA_SLOTS]{};
      const int car_cnt = collect_carousel_camera_indices(cams, c_cnt, car_idx, MAX_CAMERA_SLOTS);
      const uint32_t dwell_ms = prefs.camera_carousel_interval_ms();
      if (car_cnt > 1 && (now_ms - rs.cameras.carousel_last_advance_ms >= dwell_ms)) {
        rs.cameras.carousel_idx = (rs.cameras.carousel_idx + 1) % car_cnt;
        rs.cameras.carousel_last_advance_ms = now_ms;
        act.sync_stream = true;
        if (prefs.camera_stream_mode == 2U || ac.camera_stream_url.empty()) {
          act.release_snapshot_art = true;
          rs.alert.loaded_camera_url.clear();
          act.fetch_snapshot = true;
        }
        act.mark_dirty_full = true;
        act.schedule_disp = true;
      }
    }

    // IMU landscape auto-rotate on non-square rectangular panels
    if constexpr (!LATTER_ROUND_DISPLAY && LATTER_HAS_IMU && (LATTER_SCREEN_W != LATTER_SCREEN_H)) {
      if (rs.cameras.tab == CamerasTab::SINGLE && !rs.cameras.quick_reply_open) {
        const float ax = rs.hw_accel_x;
        const float ay = rs.hw_accel_y;
        if (!std::isnan(ax) && !std::isnan(ay) && (std::abs(ax) > 0.15f || std::abs(ay) > 0.15f)) {
          const bool want_land = (std::abs(ax) > 0.68f && std::abs(ay) < 0.42f);
          const bool want_port = (std::abs(ay) > 0.68f && std::abs(ax) < 0.42f);
          if ((want_land && !rs.cameras.landscape_mode) ||
              (want_port && rs.cameras.landscape_mode)) {
            rs.cameras.landscape_mode = want_land;
            act.sync_stream = true;
            if (prefs.camera_stream_mode == 2U || ac.camera_stream_url.empty()) {
              act.release_snapshot_art = true;
              act.fetch_snapshot = true;
            }
            act.mark_dirty_full = true;
            act.schedule_disp = true;
          }
        }
      }
    }

    // Keep live video stream synced (or fall back to paced snapshots ONLY in Snapshots mode)
    const bool snap_only = (prefs.camera_stream_mode == 2U) || ac.camera_stream_url.empty();
    if (!snap_only && rs.cameras.tab != CamerasTab::SETTINGS && !rs.cameras.quick_reply_open) {
      if (!video_client().is_active() && (now_ms - rs.cameras.last_stream_sync_ms >= 1000U)) {
        act.sync_stream = true;
      }
    } else if (snap_only && rs.cameras.tab != CamerasTab::SETTINGS && !rs.cameras.quick_reply_open) {
#if LATTER_HAS_HW_JPEG
      constexpr uint32_t kCamRefreshMs = 500U;
#else
      constexpr uint32_t kCamRefreshMs = 900U;
#endif
      const uint32_t elapsed_cam_ms = now_ms - last_cam_live_fetch_ms();
      const uint32_t backoff_until = cam_error_backoff_until_ms();
      const bool in_backoff =
          (backoff_until != 0U) && (static_cast<int32_t>(now_ms - backoff_until) < 0);
      if (!in_backoff &&
          ((!cam_download_in_progress() && elapsed_cam_ms >= kCamRefreshMs) ||
           (elapsed_cam_ms >= 4500U))) {
        last_cam_live_fetch_ms() = now_ms;
        act.fetch_snapshot = true;
      }
    }
  } else if (live_popup_now && !video_client().is_active() &&
             (now_ms - rs.cameras.last_stream_sync_ms >= 1000U)) {
    act.sync_stream = true;
  }

  return act;
}

CameraSupervisorController &camera_supervisor_controller() noexcept {
  static CameraSupervisorController inst;
  return inst;
}

// ==============================================================================
// 6. 1 s Power Supervisor Evaluator (Games/Tools/RTH/DisplayLifecycle/Hibernate)
// ==============================================================================

PowerSupervisor1sActions evaluate_power_supervisor_1s(uint32_t now_ms,
                                                      const PowerSupervisor1sInputs &in) {
  PowerSupervisor1sActions act{};
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const bool showcase_active_1s = rs.showcase.is_active(now_ms);
  const uint32_t last_user_act_ms =
      std::max<uint32_t>({last_screen_wake_ms(), in.last_touch_ms, in.ui_focus_last_ms});

  if (rs.nav.display_power_state == 0 && !showcase_active_1s && (now_ms > last_user_act_ms)) {
    const uint32_t touch_idle_ms = now_ms - last_user_act_ms;
    if (rs.nav.active_game > 0 && touch_idle_ms >= 180000U) {
      ESP_LOGD("power", "Game untouched for 3 min: exiting to launcher so screen can sleep");
      rs.nav.active_game = 0;
      act.stop_alarm_sound = true;
      display_lifecycle().note_activity(now_ms);
      act.note_activity = true;
    }
    if (!rs.hw_usb_connected &&
        ((rs.nav.gui_page == 8 && (rs.nav.tools_subpage == 5 || rs.nav.tools_subpage == 7)) ||
         is_camera_view_active(rs)) &&
        touch_idle_ms >= 300000U) {
      ESP_LOGD("power", "Tool/Camera untouched on battery for 5 min: exiting to idle");
      if (is_camera_view_active(rs)) {
        if (cam_call_active() || cam_intercom_ptt_active() || cam_listen_active()) {
          intercom_session().stop_call_with_hw(now_ms);
        }
        cam_quick_reply_open() = false;
        video_client().stop();
        rs.alert.loaded_camera_url.clear();
        act.exit_camera_home = true;
      } else {
        rs.nav.tools_subpage = 0;
      }
      act.sync_brightness = true;
      display_lifecycle().note_activity(now_ms);
      act.note_activity = true;
    }
  }

  const uint32_t rth_ms = return_to_home_timeout_ms(prefs);
  const bool media_playing_1s =
      rs.audio.local_mp_playing() || (media_state().state == "playing");
  const bool cam_busy_1s =
      cam_call_active() || cam_listen_active() || cam_intercom_ptt_active();
  if (!act.exit_camera_home && rs.nav.display_power_state != 2 && rth_ms > 0U &&
      (now_ms > last_user_act_ms) &&
      (now_ms - last_user_act_ms >= rth_ms) &&
      should_return_to_home_on_idle(rs, prefs, media_playing_1s, cam_busy_1s)) {
    ESP_LOGD("power", "Idle exceeded Return to Home timeout (%s): returning to Home",
             pref_return_to_home_timeout_cstr(prefs));
    act.navigate_home = true;
    act.apply_power_policy = true;
  }

  const bool active_tool_awake =
      !act.exit_camera_home && !act.navigate_home &&
      ((rs.nav.gui_page == 8 && (rs.nav.tools_subpage == 5 || rs.nav.tools_subpage == 7)) ||
       is_camera_view_active(rs));
  const uint32_t since_touch_ms =
      now_ms - std::max<uint32_t>(last_screen_wake_ms(), in.last_touch_ms);
  const bool in_hand_hold =
      (since_touch_ms < 90000U) &&
      should_in_hand_block_sleep(
          !rs.hw_usb_connected && !rs.audio.speaker_enabled() && rs.hw_pickup_detected);
  const bool voice_or_announce_busy =
      rs.voice.active || rs.audio.va_running() || rs.voice.tts_received ||
      rs.audio.announcement_running();
  const bool alert_or_stopwatch_busy =
      rs.stopwatch.running || rs.timers.ringing || rs.alarms.ringing ||
      rs.alarms.find_my_active || rs.alert.active;
  const bool hibernate_blocked =
      rs.nav.is_hibernating || rs.hw_usb_connected || rs.nav.ota_in_progress ||
      showcase_active_1s ||
      rs.audio.local_mp_playing() ||
      rs.stopwatch.running || rs.timers.running || rs.timers.ringing ||
      rs.alarms.ringing || rs.alarms.find_my_active || voice_or_announce_busy ||
      cam_busy_1s || (rs.nav.active_game > 0);

  DisplaySupervisorInputs dl_in{};
  dl_in.is_stationary = (LATTER_IS_STATIONARY != 0);
  dl_in.has_battery = (LATTER_HAS_BATTERY != 0);
  dl_in.usb_connected = rs.hw_usb_connected;
  dl_in.screensaver_mode = prefs.screensaver_mode;
  dl_in.docked_timeout_opt = pref_docked_idle_timeout_cstr(prefs);
  dl_in.battery_timeout_opt = pref_battery_screen_timeout_cstr(prefs);
  dl_in.hibernate_timeout_opt = pref_battery_hibernate_timeout_cstr(prefs);
  dl_in.wake_greeting_enabled = prefs.wake_greeting && !prefs.bedtime_active;
  dl_in.bedtime_active = prefs.bedtime_active;
  dl_in.panel_brightness_on = in.panel_brightness_on;
  dl_in.is_hibernating = rs.nav.is_hibernating;
  dl_in.ota_in_progress = rs.nav.ota_in_progress;
  dl_in.voice_or_announce_busy = voice_or_announce_busy;
  dl_in.alert_or_stopwatch_busy = alert_or_stopwatch_busy;
  dl_in.in_hand_hold = in_hand_hold;
  dl_in.active_tool_awake = active_tool_awake;
  dl_in.active_game_running = (rs.nav.active_game != 0);
  dl_in.hibernate_blocked = hibernate_blocked;
  dl_in.showcase_active = showcase_active_1s;

  const auto dl_act = display_lifecycle().evaluate_1s(
      now_ms, dl_in, rs.nav.display_power_state, rs.nav.sleep_start_time,
      rs.nav.standby_entered_ms, rs.nav.wake_greeting_pending);
  if (dl_act.turn_screen_off_now) {
    act.turn_screen_off = true;
  } else if (dl_act.enter_standby_now) {
    act.enter_standby = true;
  }
#if LATTER_HAS_BATTERY
  if (dl_act.start_hibernate_countdown_now) {
    ESP_LOGD("power", "Inactivity with screen off exceeded %s: starting hibernation countdown",
             pref_battery_hibernate_timeout_cstr(prefs));
    battery_model().hib_reason = HibernateReason::IDLE;
    act.start_hibernate_countdown = true;
  }
#endif

  return act;
}

}  // namespace latter_core
