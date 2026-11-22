#include "home_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace latter_ui {
namespace {

HomeActivityItem s_rendered_act_items[kMaxHomeActivity]{};
int s_rendered_act_count = 0;
int s_act_band_y0 = 0;
int s_act_band_y1 = 0;
bool s_act_deck_rotates = false;

void store_rendered_activity_snapshot(const HomeActivityItem *items, int count) {
  const int n = std::clamp(count, 0, kMaxHomeActivity);
  for (int i = 0; i < n; ++i) s_rendered_act_items[i] = items[i];
  s_rendered_act_count = n;
}

struct HomeActivitiesMemoKey {
  uint32_t sw_tick{0xFFFFFFFFU};
  std::array<int, 3> t_rem{-1, -1, -1};
  uint8_t t_flags{0xFFU};
  std::array<std::string, 3> t_names{};
  bool is_24h{false}, music_on{false}, music_cleared{false}, music_vis{false};
  std::string m_title{}, m_artist{};
  uint32_t snooze{0xFFFFFFFFU};
  int snooze_slot{-99};
  std::array<uint8_t, 3> a_h{0, 0, 0}, a_m{0, 0, 0}, a_rec{0xFFU, 0xFFU, 0xFFU};
  uint8_t a_en{0xFFU};
  int cur_h{-2}, cur_m{-2}, cur_dow{-2};
  uint8_t src_mask{0xFFU};
  std::string cal_raw{};
  bool intercom_on{false};
  std::string doorbell_st{};
  bool operator==(const HomeActivitiesMemoKey &) const = default;
};

}  // namespace

HomeActivityAnimState &home_act_state() noexcept { static HomeActivityAnimState st; return st; }

void reset_home_activity_anim_state(bool on_home_main) noexcept {
  auto &st = home_act_state();
  st.cur_rows = st.from_rows = 0;
  st.cur_top_item = st.cur_bot_item = st.from_top_item = st.from_bot_item = HomeActivityItem{};
  st.from_h = st.last_drawn_h = 0;
  st.from_alpha = 1.0f; st.last_drawn_alpha = 0.0f;
  st.anim_start_ms = st.fade_out_start_ms = 0U;
  st.was_visible = false; st.was_on_home_main = on_home_main;
}

void sync_home_activity_away_sentinel() noexcept {
  if (home_activity_rendered_rows() < 0) {
    s_act_deck_rotates = false;
    reset_home_activity_anim_state(false);
    home_activity_rendered_rows() = 0;
  }
}

void set_home_activity_deck_rotates(bool rotates) noexcept { s_act_deck_rotates = rotates; }

HomeActivityStepResult step_home_activity_card(
    uint32_t now_ms, int home_subpage, bool greeting_active, int weather_h,
    const HomeActivityView &view, const HomeActivityItem *act_items, int act_count,
    int &out_nav_idx) {
  HomeActivityStepResult res{};
  const auto &ly = layout();
  const bool two = ly.home_activity_two_line();
  const int act_card_cy = ly.home_activity_cy(weather_h), max_card_h = ly.home_activity_h();
  const int max_card_y1 = ly.is_round ? (act_card_cy + max_card_h / 2) : ly.home_activity_bottom();
  const int max_band_y0 = std::max(0, max_card_y1 - max_card_h - 2);
  const int max_band_y1 = std::min(SCREEN_H - 1, max_card_y1 + 2);
  const int h1 = ly.home_activity_h_for_rows(1);

  const int target_rows = (home_subpage == 0 && !greeting_active) ? view.visible_rows : 0;
  home_activity_card_shown() = (target_rows > 0);
  home_activity_rendered_rows() = target_rows;

  auto &st = home_act_state();
  if (target_rows > 0) {
    const int target_h = ly.home_activity_h_for_rows(target_rows);
    const int target_y1 = ly.is_round ? (ly.home_activity_cy_for_h(weather_h, target_h) + target_h / 2) : ly.home_activity_bottom();
    const int target_y0 = target_y1 - target_h;
    const bool has_drop = ly.home_activity_has_drop(view.front_count);
    out_nav_idx = view.top;
    s_act_deck_rotates = two ? (view.front_count > 2) : (!view.pinned && view.front_count > 1);

    const HomeActivityItem top_item = (view.top >= 0 && view.top < act_count) ? act_items[view.top] : HomeActivityItem{};
    const HomeActivityItem bot_item = (target_rows >= 2 && view.bottom >= 0 && view.bottom < act_count) ? act_items[view.bottom] : HomeActivityItem{};
    const bool fresh_home_entry = !st.was_on_home_main;
    st.was_on_home_main = true;

    const bool had_active_anim = (st.anim_start_ms != 0U);
    if (st.anim_start_ms != 0U && now_ms >= st.anim_start_ms && (now_ms - st.anim_start_ms) >= kMotionMs) st.anim_start_ms = 0U;

    if (fresh_home_entry) {
      st.from_rows = st.cur_rows = target_rows;
      st.from_top_item = st.cur_top_item = top_item;
      st.from_bot_item = st.cur_bot_item = bot_item;
      st.from_has_drop = st.cur_has_drop = has_drop;
      st.from_h = target_h; st.from_alpha = 1.0f; st.anim_start_ms = 0U;
    } else if (!st.was_visible || st.fade_out_start_ms != 0U) {
      st.from_rows = 0; st.from_top_item = st.from_bot_item = HomeActivityItem{}; st.from_has_drop = has_drop;
      st.from_h = (st.last_drawn_h > 0) ? st.last_drawn_h : std::max(scale_y(26), (h1 * 3) / 5);
      st.from_alpha = (st.last_drawn_alpha > 0.02f) ? st.last_drawn_alpha : 0.0f;
      st.cur_rows = target_rows; st.cur_top_item = top_item; st.cur_bot_item = bot_item; st.cur_has_drop = has_drop;
      st.anim_start_ms = (now_ms == 0U) ? 1U : now_ms;
    } else if (target_rows != st.cur_rows || !same_activity_id(top_item, st.cur_top_item) ||
               (target_rows >= 2 && !same_activity_id(bot_item, st.cur_bot_item))) {
      st.from_rows = st.cur_rows; st.from_top_item = st.cur_top_item; st.from_bot_item = st.cur_bot_item; st.from_has_drop = st.cur_has_drop;
      st.from_h = (st.last_drawn_h > 0) ? st.last_drawn_h : ly.home_activity_h_for_rows(st.cur_rows);
      st.from_alpha = (st.last_drawn_alpha > 0.02f) ? st.last_drawn_alpha : 1.0f;
      st.cur_rows = target_rows; st.cur_top_item = top_item; st.cur_bot_item = bot_item; st.cur_has_drop = has_drop;
      st.anim_start_ms = (now_ms == 0U) ? 1U : now_ms;
    } else {
      st.cur_top_item = top_item; st.cur_bot_item = bot_item; st.cur_has_drop = has_drop;
    }
    st.was_visible = true; st.fade_out_start_ms = 0U;

    float e = 1.0f;
    if (st.anim_start_ms != 0U) {
      const uint32_t el = (now_ms >= st.anim_start_ms) ? (now_ms - st.anim_start_ms) : 0U;
      if (el < kMotionMs) e = ease_out_cubic(static_cast<float>(el) / static_cast<float>(kMotionMs));
      else st.anim_start_ms = 0U;
    }
    const bool animating = (e < 1.0f);
    s_act_band_y0 = (animating || had_active_anim) ? max_band_y0 : std::max(0, target_y0 - 2);
    s_act_band_y1 = (animating || had_active_anim) ? max_band_y1 : std::min(SCREEN_H - 1, target_y1 + 2);

    res.draw_active = true; res.animating = animating; res.has_drop = has_drop;
    res.target_rows = target_rows; res.target_h = target_h; res.target_cy = target_y0 + target_h / 2;
    res.e = e;
    res.card_h = animating ? static_cast<int>(std::lround(static_cast<float>(st.from_h) + static_cast<float>(target_h - st.from_h) * e)) : target_h;
    res.surf_alpha = animating ? std::clamp(st.from_alpha + (1.0f - st.from_alpha) * e, 0.0f, 1.0f) : 1.0f;
    st.last_drawn_h = res.card_h; st.last_drawn_alpha = res.surf_alpha;
    return res;
  }

  s_act_deck_rotates = false;
  if (home_subpage != 0 || greeting_active) { reset_home_activity_anim_state(false); return res; }
  st.was_on_home_main = true;
  if (st.was_visible) {
    st.was_visible = false;
    st.fade_out_rows = (st.cur_rows >= 2) ? 2 : 1; st.fade_out_has_drop = st.cur_has_drop;
    st.fade_out_top_item = st.cur_top_item; st.fade_out_bot_item = st.cur_bot_item;
    st.fade_out_from_h = (st.last_drawn_h > 0) ? st.last_drawn_h : ly.home_activity_h_for_rows(st.fade_out_rows);
    st.fade_out_from_alpha = (st.last_drawn_alpha > 0.02f) ? st.last_drawn_alpha : 1.0f;
    st.cur_rows = st.from_rows = 0; st.anim_start_ms = 0U;
    st.fade_out_start_ms = (now_ms == 0U) ? 1U : now_ms;
  }
  if (st.fade_out_start_ms != 0U) {
    s_act_band_y0 = max_band_y0; s_act_band_y1 = max_band_y1;
    const uint32_t el = (now_ms >= st.fade_out_start_ms) ? (now_ms - st.fade_out_start_ms) : 0U;
    if (el >= kMotionMs) {
      st.fade_out_start_ms = 0U; st.last_drawn_h = 0; st.last_drawn_alpha = 0.0f;
    } else {
      const float e = ease_out_cubic(static_cast<float>(el) / static_cast<float>(kMotionMs));
      const int min_collapse_h = std::max(scale_y(26), (h1 * 3) / 5);
      res.draw_fade_out = true; res.e = e;
      res.surf_alpha = std::clamp(st.fade_out_from_alpha * (1.0f - e), 0.0f, 1.0f);
      res.card_h = static_cast<int>(std::lround(static_cast<float>(st.fade_out_from_h) + static_cast<float>(min_collapse_h - st.fade_out_from_h) * e));
      st.last_drawn_h = res.card_h; st.last_drawn_alpha = res.surf_alpha;
    }
  }
  return res;
}

const char *resolve_wake_greeting(int hour, int minute, int day_of_year,
                                  const std::string &cond, float temp_f) {
  const int pick = (day_of_year + hour * 3 + (minute / 10)) % 3;
  if (!cond.empty()) {
    if (cond.find("lightning") != std::string::npos || cond.find("thunder") != std::string::npos) return "Stay dry out there";
    if (cond.find("rain") != std::string::npos || cond.find("pouring") != std::string::npos ||
        cond.find("drizzle") != std::string::npos) {
      return (pick == 0) ? "Rainy out there today" : "Don't forget an umbrella";
    }
    if (cond.find("snow") != std::string::npos || cond.find("sleet") != std::string::npos) return "Bundle up out there";
    if (cond.find("wind") != std::string::npos || cond.find("breez") != std::string::npos) {
      return (pick == 0) ? "Breezy out there today" : "Hold onto your hat today";
    }
    if ((cond == "sunny" || cond == "clear-day" || cond == "clearday") &&
        hour >= 9 && hour < 17 && !std::isnan(temp_f) && temp_f >= 64.0f && temp_f <= 85.0f && pick == 0) {
      return "It's a beautiful day";
    }
  }
  if (hour >= 5 && hour < 12) return (pick == 1) ? "Rise and shine" : ((pick == 2) ? "Have a great day" : "Good morning");
  if (hour >= 12 && hour < 17) return (pick == 1) ? "Hope your day is going well" : "Good afternoon";
  if (hour >= 17 && hour < 22) return (pick == 1) ? "Time to unwind" : "Good evening";
  return (pick == 1) ? "Up late tonight?" : ((pick == 2) ? "Rest easy tonight" : "Sleep well");
}

int &home_activity_overlay_page() { static int pg = 0; return pg; }
int &snoozed_alarm_slot() noexcept { return runtime_state().alarms.snoozed_alarm_slot; }
uint32_t &wake_greeting_start_ms() { static uint32_t ts = 0; return ts; }
bool &wake_greeting_latched() { static bool latched = false; return latched; }

void dismiss_wake_greeting() {
  wake_greeting_latched() = false;
  runtime_state().nav.wake_greeting_pending = false;
}

bool is_wake_greeting_visible(uint32_t now_ms, bool enabled) {
  return enabled && wake_greeting_latched() && now_ms >= wake_greeting_start_ms() &&
         (now_ms - wake_greeting_start_ms()) < kWakeGreetingTotalMs;
}

bool is_wake_greeting_fading(uint32_t now_ms, bool enabled) {
  if (!enabled || !wake_greeting_latched() || now_ms < wake_greeting_start_ms()) return false;
  const uint32_t el = now_ms - wake_greeting_start_ms();
  return (el >= (kWakeGreetingHoldMs - 60U) && el <= (kWakeGreetingTotalMs + 60U));
}

bool is_doorbell_or_intercom_active() {
  const auto &ac = area_ctx_state();
  const auto &rs = runtime_state();
  const bool db_live = !ac.doorbell_talk_state.empty() && ac.doorbell_talk_state != "idle" &&
                       ac.doorbell_talk_state != "off" && ac.doorbell_talk_state != "unavailable" &&
                       ac.doorbell_talk_state != "unknown";
  return db_live || rs.voice.intercom_turn || cam_call_active() || cam_listen_active();
}

bool is_home_activity_carousel_animating(uint32_t now_ms) {
  sync_home_activity_away_sentinel();
  const uint32_t t0 = home_act_state().anim_start_ms;
  return t0 != 0U && (now_ms < t0 || (now_ms - t0) < kMotionMs);
}

bool is_home_activity_fading_out(uint32_t now_ms) {
  sync_home_activity_away_sentinel();
  const uint32_t t0 = home_act_state().fade_out_start_ms;
  return t0 != 0U && (now_ms < t0 || (now_ms - t0) < kMotionMs);
}

bool has_pending_home_activity_fade_settle(uint32_t now_ms) {
  sync_home_activity_away_sentinel();
  const auto &rs = runtime_state();
  if (rs.nav.gui_page != PageId::HOME || rs.nav.home_subpage != 0) return false;
  const auto &st = home_act_state();
  if (st.fade_out_start_ms != 0U && now_ms >= st.fade_out_start_ms) {
    const uint32_t el = now_ms - st.fade_out_start_ms;
    if (el >= kMotionMs && el < (kMotionMs + 250U)) return true;
  }
  if (st.anim_start_ms != 0U && now_ms >= st.anim_start_ms) {
    const uint32_t el = now_ms - st.anim_start_ms;
    if (el >= kMotionMs && el < (kMotionMs + 250U)) return true;
  }
  return false;
}

bool home_activity_slide_active(uint32_t now_ms) {
  sync_home_activity_away_sentinel();
  const auto &rs = runtime_state();
  if (rs.nav.gui_page != PageId::HOME || rs.nav.home_subpage != 0) return false;
  return (home_activity_card_shown() && is_home_activity_carousel_animating(now_ms)) ||
         is_home_activity_fading_out(now_ms);
}

uint32_t home_activity_next_rotate_due_ms(uint32_t now_ms) {
  sync_home_activity_away_sentinel();
  const auto &rs = runtime_state();
  if (rs.nav.gui_page != PageId::HOME || rs.nav.home_subpage != 0 ||
      !home_activity_card_shown() || !s_act_deck_rotates) {
    return 0U;
  }
  return std::max<uint32_t>(1U, kHomeActivityRotateMs - (now_ms % kHomeActivityRotateMs));
}

bool home_activity_band(int &out_y0, int &out_y1) {
  sync_home_activity_away_sentinel();
  const auto &rs = runtime_state();
  if (rs.nav.gui_page != PageId::HOME || rs.nav.home_subpage != 0) return false;
  if ((!home_activity_card_shown() && home_act_state().fade_out_start_ms == 0U) ||
      s_act_band_y1 <= s_act_band_y0) {
    return false;
  }
  out_y0 = s_act_band_y0;
  out_y1 = s_act_band_y1;
  return true;
}

const HomeActivityItem *home_activity_rendered_items(int &out_count) {
  out_count = s_rendered_act_count;
  return s_rendered_act_items;
}

int collect_home_activities(
    uint32_t now_ms, bool is_24h_format,
    bool stopwatch_running, uint32_t stopwatch_start_time, uint32_t stopwatch_elapsed_ms,
    bool music_playing, const std::string &music_title, const std::string &music_artist,
    uint32_t alarm_snooze_until,
    int alarm1_h, int alarm1_m, bool alarm1_en,
    int alarm2_h, int alarm2_m, bool alarm2_en,
    int alarm3_h, int alarm3_m, bool alarm3_en,
    HomeActivityItem out_items[kMaxHomeActivity],
    int cur_hour, int cur_min,
    const std::string &calendar_agenda_raw,
    int cur_day_of_week,
    int snoozed_slot) {
  int count = 0;

  if (is_doorbell_or_intercom_active() && count < kMaxHomeActivity) {
    const auto &ac = area_ctx_state();
    std::string lbl = "Intercom • Live";
    if (ac.doorbell_talk_state == "ringing") {
      lbl = "Doorbell • Ringing";
    } else if (cam_call_active() || cam_listen_active() ||
               (!ac.doorbell_talk_state.empty() && ac.doorbell_talk_state != "idle" &&
                ac.doorbell_talk_state != "off" && ac.doorbell_talk_state != "unavailable" &&
                ac.doorbell_talk_state != "unknown")) {
      lbl = "Doorbell • Live";
    }
    out_items[count++] = {6, 0, true, true, true, lbl};
  }

  auto *t_slots = timer_slots();
  int run_order[3] = {0, 1, 2};
  if (t_slots[run_order[1]].remaining_sec < t_slots[run_order[0]].remaining_sec) std::swap(run_order[0], run_order[1]);
  if (t_slots[run_order[2]].remaining_sec < t_slots[run_order[1]].remaining_sec) std::swap(run_order[1], run_order[2]);
  if (t_slots[run_order[1]].remaining_sec < t_slots[run_order[0]].remaining_sec) std::swap(run_order[0], run_order[1]);
  for (int k = 0; k < 3 && count < kMaxHomeActivity; ++k) {
    const int i = run_order[k];
    if (!t_slots[i].running) continue;
    const auto &s = t_slots[i];
    const std::string t_name = s.name.empty() ? ("Timer " + std::to_string(i + 1)) : s.name;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s • %02d:%02d", t_name.c_str(), s.remaining_sec / 60, s.remaining_sec % 60);
    out_items[count++] = {1, i, true, true, true, std::string(buf)};
  }

  if (alarm_snooze_until > 0 && count < kMaxHomeActivity) {
    out_items[count++] = {4, 0, true, true, true, "Alarm • Snoozed"};
  }

  const uint32_t sw_ms = stopwatch_running ? (now_ms - stopwatch_start_time) : stopwatch_elapsed_ms;
  char sw_buf[48] = "";
  if (stopwatch_running || sw_ms > 0) {
    std::snprintf(sw_buf, sizeof(sw_buf), "Stopwatch • %02d:%02d.%d",
                  static_cast<int>((sw_ms / 60000U) % 60U), static_cast<int>((sw_ms / 1000U) % 60U),
                  static_cast<int>((sw_ms / 100U) % 10U));
    if (stopwatch_running && count < kMaxHomeActivity) {
      out_items[count++] = {2, 0, true, true, true, std::string(sw_buf)};
    }
  }

  struct PendingAlarmEntry { int slot_1based = 0, mins_until = -1; std::string label{}; };
  PendingAlarmEntry pend_alarms[3]{};
  int n_pend_alarms = 0;
  const int eff_snoozed = (alarm_snooze_until > 0) ? ((snoozed_slot >= 0) ? snoozed_slot : snoozed_alarm_slot()) : -1;
  if (alarm_snooze_until == 0 || eff_snoozed >= 1) {
    const auto &prefs = persisted_prefs();
    const int a_hrs[3] = {alarm1_h, alarm2_h, alarm3_h}, a_mns[3] = {alarm1_m, alarm2_m, alarm3_m};
    const bool a_ens[3] = {alarm1_en, alarm2_en, alarm3_en};
    for (int i = 0; i < 3; ++i) {
      if (alarm_snooze_until > 0 && (i + 1) == eff_snoozed) continue;
      if (!a_ens[i] || a_hrs[i] < 0 || a_hrs[i] > 23 || a_mns[i] < 0 || a_mns[i] > 59) continue;
      char t_buf[16], ap_buf[8] = "", buf[48];
      format_time_hhmm(a_hrs[i], a_mns[i], is_24h_format, t_buf, sizeof(t_buf), ap_buf);
      if (!is_24h_format && ap_buf[0] != '\0') std::snprintf(buf, sizeof(buf), "Alarm • %s %s", t_buf, ap_buf);
      else std::snprintf(buf, sizeof(buf), "Alarm • %s", t_buf);
      const int mins_u = next_alarm_minutes_until(
          static_cast<uint8_t>(a_hrs[i]), static_cast<uint8_t>(a_mns[i]), true,
          prefs.alarms[i].recurrence, cur_hour, cur_min, cur_day_of_week);
      pend_alarms[n_pend_alarms++] = {i + 1, mins_u, std::string(buf)};
    }
    for (int a = 0; a < n_pend_alarms - 1; ++a) {
      for (int b = a + 1; b < n_pend_alarms; ++b) {
        const int ma = (pend_alarms[a].mins_until > 0) ? pend_alarms[a].mins_until : 999999;
        const int mb = (pend_alarms[b].mins_until > 0) ? pend_alarms[b].mins_until : 999999;
        if (mb < ma) std::swap(pend_alarms[a], pend_alarms[b]);
      }
    }
  }

  for (int a = 0; a < n_pend_alarms && count < kMaxHomeActivity; ++a) {
    const int mu = pend_alarms[a].mins_until;
    if (mu > 0 && mu <= kUpcomingAlarmPinMinutes) {
      out_items[count++] = {4, pend_alarms[a].slot_1based, false, true, true, pend_alarms[a].label};
    }
  }

  const auto &ms = media_state();
  if (music_playing) music_queue_cleared() = false;
  const bool is_paused_st = is_music_paused_effective(music_playing, ms.is_local, ms.state);
  const bool has_valid_track = !music_title.empty() || !ms.title.empty();
  if ((music_playing || !music_queue_cleared()) &&
      audio_session().is_music_visible_on_home_activity(music_playing, is_paused_st, has_valid_track, now_ms) &&
      count < kMaxHomeActivity) {
    std::string m_txt = !music_title.empty() ? music_title : ms.title;
    if (m_txt.empty()) m_txt = "Now Playing";
    else if (!music_artist.empty()) m_txt = music_artist + " • " + m_txt;
    out_items[count++] = {3, 0, music_playing, false, true, m_txt};
  }

  for (int a = 0; a < n_pend_alarms && count < kMaxHomeActivity; ++a) {
    const int mu = pend_alarms[a].mins_until;
    if (mu > kUpcomingAlarmPinMinutes && mu <= kUpcomingAlarmDeckMinutes) {
      out_items[count++] = {4, pend_alarms[a].slot_1based, false, false, true, pend_alarms[a].label};
    }
  }

  for (int i = 0; i < 3 && count < kMaxHomeActivity; ++i) {
    if (!t_slots[i].running && t_slots[i].paused) {
      const auto &s = t_slots[i];
      const std::string t_name = s.name.empty() ? ("Timer " + std::to_string(i + 1)) : s.name;
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%s • %02d:%02d", t_name.c_str(), s.remaining_sec / 60, s.remaining_sec % 60);
      out_items[count++] = {1, i, false, false, true, std::string(buf)};
    }
  }
  if (!stopwatch_running && sw_ms > 0 && count < kMaxHomeActivity) {
    out_items[count++] = {2, 0, false, false, true, std::string(sw_buf)};
  }

  CalendarRecord cal_recs[8];
  const size_t n_cal = parse_calendar_records(calendar_agenda_raw, cal_recs, 8);
  int added_today = 0;
  for (size_t c = 0; c < n_cal && added_today < 3 && count < kMaxHomeActivity; ++c) {
    if (cal_recs[c].time.rfind("TODAY", 0) == 0) {
      out_items[count++] = {5, static_cast<int>(c), false, false, true,
                            format_agenda_time_label(cal_recs[c].time, is_24h_format) + " • " + std::string(cal_recs[c].title)};
      ++added_today;
    }
  }

  for (int a = 0; a < n_pend_alarms && count < kMaxHomeActivity; ++a) {
    const int mu = pend_alarms[a].mins_until;
    if (mu <= 0 || mu > kUpcomingAlarmDeckMinutes) {
      out_items[count++] = {4, pend_alarms[a].slot_1based, false, false, false, pend_alarms[a].label};
    }
  }
  if (added_today == 0 && n_cal > 0 && count < kMaxHomeActivity) {
    out_items[count++] = {5, 0, false, false, false,
                          format_agenda_time_label(cal_recs[0].time, is_24h_format) + " • " + std::string(cal_recs[0].title)};
  }

  const uint8_t mask = activity_source_mask();
  int kept = 0;
  for (int i = 0; i < count; i++) {
    if ((out_items[i].kind == 6) || ((mask & (1U << out_items[i].kind)) != 0U)) {
      if (kept != i) out_items[kept] = out_items[i];
      kept++;
    }
  }
  return kept;
}

const HomeActivityItem *collect_home_activities_cached(
    const UiFrameContext &ctx, bool music_playing,
    const std::string &music_title, const std::string &music_artist,
    int &out_count) {
  static HomeActivityItem s_memo_items[kMaxHomeActivity]{};
  static int s_memo_count = -1;
  static HomeActivitiesMemoKey s_memo_key{};

  const auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &ac = area_ctx_state();
  const auto &ms = media_state();
  const auto &cs = calendar_state();
  const auto *ts_chk = timer_slots();
  const uint32_t now_ms = ctx.now_ms;
  const bool is_24h = prefs.is_24h_format;
  const bool eff_cleared = music_playing ? false : music_queue_cleared();
  const bool eff_paused = !eff_cleared && is_music_paused_effective(music_playing, ms.is_local, ms.state);

  HomeActivitiesMemoKey cur_key{};
  cur_key.sw_tick = rs.stopwatch.running ? ((now_ms - rs.stopwatch.start_time_ms) / 100U)
                                         : (rs.stopwatch.elapsed_ms / 100U);
  cur_key.t_flags = static_cast<uint8_t>(
      (ts_chk[0].running ? 1U : 0U) | (ts_chk[0].paused ? 2U : 0U) |
      (ts_chk[1].running ? 4U : 0U) | (ts_chk[1].paused ? 8U : 0U) |
      (ts_chk[2].running ? 16U : 0U) | (ts_chk[2].paused ? 32U : 0U) |
      (rs.stopwatch.running ? 64U : 0U));
  for (size_t i = 0; i < 3; ++i) {
    cur_key.t_rem[i] = ts_chk[i].remaining_sec;
    cur_key.t_names[i] = ts_chk[i].name;
    cur_key.a_h[i] = prefs.alarms[i].hour;
    cur_key.a_m[i] = prefs.alarms[i].minute;
    cur_key.a_rec[i] = prefs.alarms[i].recurrence;
  }
  cur_key.is_24h = is_24h;
  cur_key.music_on = music_playing;
  cur_key.music_cleared = eff_cleared;
  cur_key.music_vis = (music_playing || !eff_cleared) &&
                      audio_session().is_music_visible_on_home_activity(
                          music_playing, eff_paused, !music_title.empty() || !ms.title.empty(), now_ms);
  cur_key.m_title = music_title;
  cur_key.m_artist = music_artist;
  cur_key.snooze = rs.alarms.snooze_until;
  cur_key.snooze_slot = (rs.alarms.snooze_until > 0) ? snoozed_alarm_slot() : -1;
  cur_key.a_en = static_cast<uint8_t>((prefs.alarms[0].enabled ? 1U : 0U) |
                                      (prefs.alarms[1].enabled ? 2U : 0U) |
                                      (prefs.alarms[2].enabled ? 4U : 0U));
  cur_key.cur_h = ctx.now.is_valid() ? ctx.now.hour : -1;
  cur_key.cur_m = ctx.now.is_valid() ? ctx.now.minute : -1;
  cur_key.cur_dow = ctx.now.is_valid() ? ctx.now.day_of_week : -1;
  cur_key.src_mask = activity_source_mask();
  cur_key.cal_raw = cs.summary;
  cur_key.intercom_on = is_doorbell_or_intercom_active();
  cur_key.doorbell_st = ac.doorbell_talk_state;

  if (s_memo_count < 0 || !(s_memo_key == cur_key)) {
    s_memo_count = collect_home_activities(
        now_ms, is_24h, rs.stopwatch.running, rs.stopwatch.start_time_ms, rs.stopwatch.elapsed_ms,
        music_playing, music_title, music_artist, rs.alarms.snooze_until,
        prefs.alarms[0].hour, prefs.alarms[0].minute, prefs.alarms[0].enabled,
        prefs.alarms[1].hour, prefs.alarms[1].minute, prefs.alarms[1].enabled,
        prefs.alarms[2].hour, prefs.alarms[2].minute, prefs.alarms[2].enabled,
        s_memo_items, cur_key.cur_h, cur_key.cur_m, cs.summary, cur_key.cur_dow, cur_key.snooze_slot);
    cur_key.music_cleared = music_queue_cleared();
    s_memo_key = std::move(cur_key);
  }
  out_count = s_memo_count;
  store_rendered_activity_snapshot(s_memo_items, s_memo_count);
  return s_memo_items;
}

int get_activity_overlay_card_y(int vis_count, int row_idx) {
  const int stride = layout().slot5_stride;
  return layout().slot5_cy - (((vis_count - 1) * stride) / 2) + row_idx * stride;
}

}  // namespace latter_ui
