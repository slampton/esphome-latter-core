#include "ui_pages.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace latter_ui {
using namespace latter_core;

namespace {

inline bool same_activity_id(const HomeActivityItem &a, const HomeActivityItem &b) {
  if (a.kind == 0 || b.kind == 0 || a.kind != b.kind) return false;
  if (a.kind == 5) return a.label == b.label;
  return a.slot_idx == b.slot_idx;
}

struct HomeActivityAnimState {
  int cur_rows = 0;
  int from_rows = 0;
  HomeActivityItem cur_top_item{};
  HomeActivityItem cur_bot_item{};
  HomeActivityItem from_top_item{};
  HomeActivityItem from_bot_item{};
  int from_h = 0;
  float from_alpha = 1.0f;
  int last_drawn_h = 0;
  float last_drawn_alpha = 0.0f;
  bool from_has_drop = false;
  bool cur_has_drop = false;
  uint32_t anim_start_ms = 0;
  bool was_visible = false;
  bool was_on_home_main = false;
  uint32_t fade_out_start_ms = 0;
  int fade_out_rows = 1;
  bool fade_out_has_drop = false;
  HomeActivityItem fade_out_top_item{};
  HomeActivityItem fade_out_bot_item{};
  int fade_out_from_h = 0;
  float fade_out_from_alpha = 1.0f;
};

struct HomeActivityStepResult {
  bool draw_active = false;
  bool draw_fade_out = false;
  bool animating = false;
  bool has_drop = false;
  int target_rows = 0;
  int target_h = 0;
  int target_cy = 0;
  int card_h = 0;
  float surf_alpha = 1.0f;
  float e = 1.0f;
};

HomeActivityAnimState &home_act_state() noexcept;
void reset_home_activity_anim_state(bool on_home_main) noexcept;
void sync_home_activity_away_sentinel() noexcept;
void set_home_activity_deck_rotates(bool rotates) noexcept;

HomeActivityStepResult step_home_activity_card(
    uint32_t now_ms, int home_subpage, bool greeting_active, int weather_h,
    const HomeActivityView &view, const HomeActivityItem *act_items, int act_count);

const HomeActivityItem *collect_home_activities_cached(
    const UiFrameContext &ctx, bool music_playing,
    const std::string &music_title, const std::string &music_artist,
    int &out_count);

const char *resolve_wake_greeting(
    int hour, int minute, int day_of_year,
    const std::string &cond, float temp_f);



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
    const HomeActivityView &view, const HomeActivityItem *act_items, int act_count) {
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

}  // namespace

int &home_activity_overlay_page() { static int pg = 0; return pg; }
int &snoozed_alarm_slot() noexcept { return runtime_state().alarms.snoozed_alarm_slot; }
uint32_t &wake_greeting_start_ms() { static uint32_t ts = 0; return ts; }
bool &wake_greeting_latched() { static bool latched = false; return latched; }
const char *&wake_greeting_latched_text() { static const char *txt = nullptr; return txt; }

void dismiss_wake_greeting() {
  wake_greeting_latched() = false;
  wake_greeting_latched_text() = nullptr;
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

namespace {

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

#if LATTER_HAS_BATTERY
void render_battery_indicator(
    Rgb565Surface &it, int cx, int by, float batt, bool is_charging,
    UiFontView font_batt, const UiColor &theme_primary,
    const UiColor &col_gold, const UiColor &col_green) {
  if (std::isnan(batt) || font_batt == nullptr) return;
  char batt_str[16];
  std::snprintf(batt_str, sizeof(batt_str), "%.0f%%", batt);
  const bool compact = (SCREEN_W < 380);
  const int text_x = cx + (compact ? 6 : 8);
  ui::shadow_print(it, text_x, by, font_batt, theme_primary, UiTextAlign::CENTER, batt_str);
  if (!compact) fast_print(it, text_x + 1, by, font_batt, theme_primary, UiTextAlign::CENTER, batt_str);
  const int bx = text_x - (compact ? (batt >= 100.0f ? 24 : 20) : (batt >= 100.0f ? 36 : 28));
  if (is_charging) {
    const int dx = compact ? 2 : 3, dy = compact ? 6 : 9;
    fast_filled_triangle(it, bx + dx, by - dy, bx - dx, by, bx + 1, by, col_gold);
    fast_filled_triangle(it, bx - 1, by, bx + 2, by, bx - dx, by + dy, col_gold);
  } else if (compact) {
    fill_rect_row_major(it, bx - 1, by - 6, 3, 1, col_green);
    fast_rect_outline(it, bx - 4, by - 5, 8, 11, col_green);
    const int fill_h = static_cast<int>(std::clamp((batt / 100.0f) * 7.0f, 1.0f, 7.0f));
    fill_rect_row_major(it, bx - 2, (by + 4) - fill_h, 4, fill_h, col_green);
  } else {
    fill_rect_row_major(it, bx - 2, by - 9, 4, 2, col_green);
    fast_rect_outline(it, bx - 5, by - 7, 10, 16, col_green);
    const int fill_h = static_cast<int>(std::clamp((batt / 100.0f) * 12.0f, 1.0f, 12.0f));
    fill_rect_row_major(it, bx - 3, (by + 7) - fill_h, 6, fill_h, col_green);
  }
}
#endif

void draw_home_activity_row_contents(
    Rgb565Surface &it, int row_y, const HomeActivityItem &cur_act,
    bool has_dropdown_ext, bool is_list_row, UiFontView font_small,
    UiColor card_bg, UiColor btn_bg, UiColor theme_secondary, UiColor col_white,
    float alpha = 1.0f, bool is_two_row_card = false) {
  const UiColor fg_w   = (alpha >= 0.99f) ? col_white       : lerp_color(card_bg, col_white, alpha);
  const UiColor fg_sh  = (alpha >= 0.99f) ? kChromeShadow   : lerp_color(card_bg, kChromeShadow, alpha);
  const UiColor fg_sec = (alpha >= 0.99f) ? theme_secondary : lerp_color(card_bg, theme_secondary, alpha);
  const UiColor fg_btn = (alpha >= 0.99f) ? btn_bg          : lerp_color(card_bg, btn_bg, alpha);

  const int card_w     = is_list_row ? layout().list_card_w : layout().home_activity_w();
  const int card_left  = SCREEN_CX - (card_w / 2), card_right = SCREEN_CX + (card_w / 2);
  const int act_col_w  = scale_s(56);
  const int drop_col_w = scale_s(52);
  const int gx         = is_list_row ? (card_left + scale_s(18)) : (card_left + scale_s(28));
  const int txt_x      = is_list_row ? (card_left + scale_s(33)) : (card_left + scale_s(48));
  const int div1_x     = is_list_row ? (card_right - scale_s(44))
                                     : (has_dropdown_ext ? (card_right - drop_col_w - act_col_w) : (card_right - act_col_w));
  const int act_bx     = is_list_row ? (card_right - scale_s(22))
                                     : (has_dropdown_ext ? (card_right - drop_col_w - act_col_w / 2) : (card_right - act_col_w / 2));
  const int max_txt    = std::max(64, div1_x - txt_x - scale_s(8));
  const int btn_r      = is_list_row ? scale_s(13)
                                     : (is_two_row_card ? ((SCREEN_W < 380) ? 15 : scale_s(17))
                                                        : ((SCREEN_W < 380) ? 16 : scale_s(18)));
  const int icon_r = is_list_row ? scale_s(8) : scale_s(10);
  const bool icon_sh = (alpha >= 0.85f);
  if (cur_act.kind == 1) ui::draw_icon_hourglass(it, gx, row_y, icon_r, fg_w, icon_sh);
  else if (cur_act.kind == 2) ui::draw_icon_stopwatch(it, gx, row_y, icon_r, fg_w, icon_sh);
  else if (cur_act.kind == 3) ui::draw_icon_eighth_notes(it, gx, row_y, icon_r, fg_w, icon_sh);
  else if (cur_act.kind == 5) ui::draw_icon_calendar(it, gx, row_y, icon_r, fg_w, icon_sh);
  else if (cur_act.kind == 6) ui::draw_icon_intercom_handset(it, gx, row_y, icon_r, fg_w, icon_sh);
  else ui::draw_icon_alarm_clock(it, gx, row_y, icon_r, fg_w, icon_sh);

  struct FitActMemo { std::string raw{}, fitted{}; int max_w{-1}; const void *font{nullptr}; };
  static FitActMemo fit_memos[4];
  static uint8_t fit_memo_rr = 0;
  const std::string *fit_ptr = nullptr;
  for (auto &m : fit_memos) {
    if (m.max_w == max_txt && m.font == font_small.handle && m.raw == cur_act.label) {
      fit_ptr = &m.fitted;
      break;
    }
  }
  if (fit_ptr == nullptr) {
    auto &slot = fit_memos[fit_memo_rr++ & 3U];
    slot = {cur_act.label, fit_text_to_width_font(cur_act.label, max_txt, font_small), max_txt, font_small.handle};
    fit_ptr = &slot.fitted;
  }
  fast_print(it, txt_x + 1, row_y + 1, font_small, fg_sh, UiTextAlign::CENTER_LEFT, fit_ptr->c_str());
  fast_print(it, txt_x, row_y, font_small, fg_w, UiTextAlign::CENTER_LEFT, fit_ptr->c_str());

  if (alpha >= 0.99f) {
    render_glass_circle(it, act_bx, row_y, btn_r, fg_btn, fg_sec);
  } else if (alpha > 0.01f && btn_r > 1) {
    const int d = 2 * btn_r + 1;
    GlassBackdrop btn_bd{};
    UiColor btn_eff{}, btn_rim{};
    int raw_at = 0, raw_ab = 0, raw_rt = 0, raw_rb = 0;
    resolve_styled_glass(act_bx - btn_r, row_y - btn_r, d, d, fg_btn, fg_sec,
                         GlassVariant::Clear, kChromeGlassSheen, -1, false,
                         btn_bd, btn_eff, btn_rim, raw_at, raw_ab, raw_rt, raw_rb);
    const int at = static_cast<int>(std::lround(static_cast<float>(raw_at) * alpha));
    const int ab = static_cast<int>(std::lround(static_cast<float>(raw_ab) * alpha));
    const int rt_a = static_cast<int>(std::lround(static_cast<float>(raw_rt) * alpha));
    const int rb_a = static_cast<int>(std::lround(static_cast<float>(raw_rb) * alpha));
    if (rt_a > 0 || rb_a > 0) {
      fill_round_rect_glass(it, act_bx - btn_r, row_y - btn_r, d, d, static_cast<float>(btn_r) + 0.5f, btn_rim, rt_a, rb_a, 1);
    }
    if ((at > 0 || ab > 0) && d > 2) {
      fill_round_rect_glass(it, act_bx - btn_r + 1, row_y - btn_r + 1, d - 2, d - 2,
                            std::max(0.5f, static_cast<float>(btn_r) - 0.5f), btn_eff, at, ab);
    }
  }

  const char *act_slug = (cur_act.kind == 5) ? "mdi:chevron-right"
                       : (cur_act.kind == 4) ? "mdi:close"
                       : (cur_act.kind == 6) ? "mdi:stop"
                       : (cur_act.is_active ? "mdi:pause" : "mdi:play");
  render_mdi_icon(it, act_slug, act_bx, row_y, std::max(14, (btn_r * 13) / 10), fg_w, icon_sh);
}

void render_home_activity_list_row(
    Rgb565Surface &it, int cx, int pill_y, const HomeActivityItem &cur_act,
    UiFontView font_small, UiColor theme_secondary, UiColor col_white,
    UiColor card_bg, UiColor card_brd, UiColor btn_bg) {
  const int card_w = layout().list_card_w;
  const int card_h = layout().slot5_h;
  const int div1_x = cx + (card_w / 2) - scale_s(44);
  const int div_h = scale_y(14);
  const UiColor surf_bg = render_card_box(it, cx - card_w / 2, pill_y - card_h / 2, card_w, card_h,
                                          card_bg, card_brd, kRadiusCardPx(), GlassVariant::Clear, col_white, true);
  draw_home_activity_row_contents(it, pill_y, cur_act, false, true, font_small,
                                  surf_bg, btn_bg, theme_secondary, col_white, 1.0f);
  fast_line(it, div1_x, pill_y - div_h, div1_x, pill_y + div_h, card_brd);
}

void draw_activity_card_chrome(
    Rgb565Surface &it, int cx, int card_x0, int card_w, int card_h, int cur_cy,
    int y_top_slot, int y_bot_slot, int rows, bool has_drop, bool show_bot_div,
    float surf_alpha, float row_sep_a, UiColor card_bg, UiColor col_white) {
  const UiColor div_col = (surf_alpha >= 0.99f) ? kChromeCardBrdOn : lerp_color(card_bg, kChromeCardBrdOn, surf_alpha);
  const int card_right = cx + card_w / 2;
  const int act_col_w = scale_s(56);
  const int drop_col_w = scale_s(52);
  const int div2_x = card_right - drop_col_w;
  const int div1_x = has_drop ? (card_right - drop_col_w - act_col_w) : (card_right - act_col_w);

  if (row_sep_a > 0.05f) {
    const int sep_x0 = card_x0 + scale_s(18), sep_x1 = (has_drop ? div2_x : card_right) - scale_s(12);
    if (sep_x1 > sep_x0) {
      const UiColor sep_col = (row_sep_a >= 0.99f) ? kChromeCardBrdOn : lerp_color(card_bg, kChromeCardBrdOn, row_sep_a);
      fast_hline(it, sep_x0, cur_cy, sep_x1 - sep_x0 + 1, sep_col);
    }
  }
  if (rows == 2) {
    const int div_h = scale_y(16);
    fast_line(it, div1_x, y_top_slot - div_h, div1_x, y_top_slot + div_h, div_col);
    if (show_bot_div) fast_line(it, div1_x, y_bot_slot - div_h, div1_x, y_bot_slot + div_h, div_col);
  } else {
    const int div_h = scale_y(18);
    fast_line(it, div1_x, cur_cy - div_h, div1_x, cur_cy + div_h, div_col);
  }
  if (has_drop) {
    const int col_h = std::max(scale_y(12), card_h / 2 - scale_y(10));
    fast_line(it, div2_x, cur_cy - col_h, div2_x, cur_cy + col_h, div_col);
    const UiColor chev_col = (surf_alpha >= 0.99f) ? col_white : lerp_color(card_bg, col_white, surf_alpha);
    const int d_bx = card_right - drop_col_w / 2, ch_w = scale_s(8), ch_h = scale_s(4);
    for (int w = -1; w <= 1; w++) {
      fast_line(it, d_bx - ch_w, cur_cy - ch_h + w, d_bx, cur_cy + ch_h + w, chev_col);
      fast_line(it, d_bx + ch_w, cur_cy - ch_h + w, d_bx, cur_cy + ch_h + w, chev_col);
    }
  }
}

bool draw_glass_activity_surface(
    Rgb565Surface &it, int card_x0, int card_y0, int card_w, int box_h, float surf_alpha,
    bool sample_bd, UiColor col_bg, UiColor col_white, UiColor &out_card_bg) {
  if (box_h <= 4 || surf_alpha <= 0.01f) return false;
  if ((card_y0 + box_h) <= clip_y0() || card_y0 > clip_y1()) return false;
  const UiColor tint = kChromeGlassTint;
  const int card_r = std::min(box_h / 2, (SCREEN_W > 520) ? scale_s(16) : 16);
  const bool full_opaque = (surf_alpha >= 0.995f);

  const int prev_clip_y0 = clip_y0(), prev_clip_y1 = clip_y1();
  if (!full_opaque) {
    const int sc_y0 = std::max(prev_clip_y0, card_y0), sc_y1 = std::min(prev_clip_y1, card_y0 + box_h - 1);
    if (sc_y0 > sc_y1) return false;
    set_clip_y_band(sc_y0, sc_y1);
  }

  GlassBackdrop bd{};
  UiColor eff_tint{}, rim_col{};
  int raw_a_top = 0, raw_a_bot = 0, raw_rim_top = 0, raw_rim_bot = 0;
  const bool want_sample = full_opaque ? sample_bd : true;
  resolve_styled_glass(card_x0, card_y0, card_w, box_h, tint, kChromeCardBorder,
                       GlassVariant::Clear, col_white, -1, want_sample,
                       bd, eff_tint, rim_col, raw_a_top, raw_a_bot, raw_rim_top, raw_rim_bot);
  const int a_top = full_opaque ? raw_a_top : static_cast<int>(std::lround(static_cast<float>(raw_a_top) * surf_alpha));
  const int a_bot = full_opaque ? raw_a_bot : static_cast<int>(std::lround(static_cast<float>(raw_a_bot) * surf_alpha));
  const int rim_top = full_opaque ? raw_rim_top : static_cast<int>(std::lround(static_cast<float>(raw_rim_top) * surf_alpha));
  const int rim_bot = full_opaque ? raw_rim_bot : static_cast<int>(std::lround(static_cast<float>(raw_rim_bot) * surf_alpha));
  if (full_opaque || rim_top > 0 || rim_bot > 0) {
    fill_round_rect_glass(it, card_x0, card_y0, card_w, box_h, static_cast<float>(card_r), rim_col, rim_top, rim_bot, 1);
  }
  if ((full_opaque || a_top > 0 || a_bot > 0) && card_w > 2 && box_h > 2) {
    fill_round_rect_glass(it, card_x0 + 1, card_y0 + 1, card_w - 2, box_h - 2,
                          std::max(0.5f, static_cast<float>(card_r) - 1.0f), eff_tint, a_top, a_bot);
  }
  if (full_opaque) {
    out_card_bg = (sample_bd && bd.valid) ? mix_color_q8(bd.mean, eff_tint, (a_top + a_bot) / 2) : eff_tint;
  } else {
    out_card_bg = mix_color_q8(bd.valid ? bd.mean : col_bg, eff_tint, (a_top + a_bot) / 2);
    set_clip_y_band(prev_clip_y0, prev_clip_y1);
  }
  return true;
}

}  // namespace

int collect_home_settings_items(const PersistedDevicePrefs &prefs,
                                HomeSettingsItemId out[12]) noexcept {
  int n = 0;
  out[n++] = HomeSettingsItemId::SHOW_TIME;
  if (prefs.home_show_time) out[n++] = HomeSettingsItemId::TIME_TAP;
  out[n++] = HomeSettingsItemId::SHOW_DATE;
  if (prefs.home_show_date) out[n++] = HomeSettingsItemId::DATE_TAP;
  out[n++] = HomeSettingsItemId::SHOW_WEATHER;
  if (prefs.home_show_weather) out[n++] = HomeSettingsItemId::WEATHER_TAP;
#if LATTER_HAS_BATTERY
  out[n++] = HomeSettingsItemId::SHOW_BATTERY;
#endif
  out[n++] = HomeSettingsItemId::WAKE_GREETING;
  out[n++] = HomeSettingsItemId::WEATHER_BG;
  if (prefs.weather_background) {
    out[n++] = HomeSettingsItemId::HOME_SUN;
    out[n++] = HomeSettingsItemId::HOME_MOON;
  }
  out[n++] = HomeSettingsItemId::WEATHER_APP_BG;
  return n;
}

int home_settings_page_count(const PersistedDevicePrefs &prefs) noexcept {
  HomeSettingsItemId items[12]{};
  const int n = collect_home_settings_items(prefs, items);
  return std::max(1, (n + 4) / 5);
}

void render_home_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  sync_home_activity_away_sentinel();

  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &ac = area_ctx_state();
  const auto &ws = weather_state();
  const auto &ms = media_state();

  const int cx = ctx.cx, cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  const int home_subpage = rs.nav.home_subpage;
  const bool is_24h_format = prefs.is_24h_format;
  const bool show_weather_bg = ctx.home_weather_bg;
  const bool show_wake_greeting = ctx.home_wake_greeting;

  const bool is_rem_media = !ms.is_local;
  const bool music_playing = is_music_playing_effective(is_rem_media, ctx.local_media_playing, ms.state, now_ms);
  const std::string music_title = (is_rem_media || ctx.track_title.empty()) ? ms.title : ctx.track_title.str();
  const std::string music_artist = (is_rem_media || ctx.track_artist.empty()) ? ms.artist : ctx.track_artist.str();

  const bool sim_override_on = (!ctx.weather_sim_opt.empty() && ctx.weather_sim_opt != "Live (Home Assistant)");
  const bool has_day_night_sky = is_day_night_sky_wallpaper_url(ac.wallpaper_url);
  const bool has_custom_wallpaper = (!ac.wallpaper_url.empty() && !has_day_night_sky &&
                                     ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                                     ctx.home_wallpaper_art.get_width() > 0);
  const bool dns_known = has_known_day_night_phase(ws.sun_state, ctx.now.is_valid(), ctx.now.hour);
  const bool dns_night = dns_known && resolve_day_night_sky_is_night(ws.sun_state, ctx.now.is_valid(), ctx.now.hour);
  const BackdropMode bd_mode = resolve_backdrop_mode(
      PageId::HOME, home_subpage, show_weather_bg, prefs.weather_app_background,
      sim_override_on, has_custom_wallpaper, has_day_night_sky);

  if (home_subpage == 1) {
    set_home_activity_deck_rotates(false);
    home_activity_card_shown() = false;
    home_act_state().was_visible = false;
    home_act_state().fade_out_start_ms = 0U;
    if (bd_mode == BackdropMode::DayNightSky) {
      if (dns_known) render_static_day_night_sky_base(it, cx, cy, dns_night);
      else frame_base(it, ctx.col_bg);
    } else {
      render_page_wallpaper_base(it, ctx.home_wallpaper_art, bd_mode == BackdropMode::CustomWallpaper, cx, ctx.col_bg);
    }
    render_page_header(it, cx, "HOME SETTINGS", ctx.font_small, ctx.col_white);

    HomeSettingsItemId items[12]{};
    const int item_count = collect_home_settings_items(prefs, items);
    const int total_pages = std::max(1, (item_count + 4) / 5);
    const int hs_pg = std::clamp(rs.nav.home_settings_page, 1, total_pages);
    rs.nav.home_settings_page = hs_pg;
    const int hs_w = layout().list_card_w, hs_h = layout().slot5_h;
    const int start_idx = (hs_pg - 1) * 5;
    const int vis_cnt = std::min(5, std::max(0, item_count - start_idx));
    for (int row = 0; row < vis_cnt; ++row) {
      const HomeSettingsItemId item = items[start_idx + row];
      const int row_y = layout().slot5_y(row);
      const uint16_t wid = static_cast<uint16_t>(WidgetId::LIST_SLOT_BASE + row);
      const int16_t param = static_cast<int16_t>(item);
      char buf[48];
      switch (item) {
        case HomeSettingsItemId::SHOW_TIME:
          ui::toggle_card_row(it, cx, row_y, hs_w, hs_h, "Show Time", prefs.home_show_time,
                              ctx.font_small, ctx.theme_primary, ctx.col_white, ctx.col_slate, wid, param);
          break;
        case HomeSettingsItemId::TIME_TAP:
          std::snprintf(buf, sizeof(buf), "Time Tap: %s", pref_home_clock_tap_cstr(prefs));
          ui::setting_cycle_row(it, cx, row_y, hs_w, hs_h, buf, true,
                                ctx.font_small, ctx.col_white, ctx.col_slate, ctx.theme_secondary,
                                wid, kHitTap | kHitFocusSlot, param);
          break;
        case HomeSettingsItemId::SHOW_DATE:
          ui::toggle_card_row(it, cx, row_y, hs_w, hs_h, "Show Date", prefs.home_show_date,
                              ctx.font_small, ctx.theme_primary, ctx.col_white, ctx.col_slate, wid, param);
          break;
        case HomeSettingsItemId::DATE_TAP:
          std::snprintf(buf, sizeof(buf), "Date Tap: %s", pref_home_date_tap_cstr(prefs));
          ui::setting_cycle_row(it, cx, row_y, hs_w, hs_h, buf, true,
                                ctx.font_small, ctx.col_white, ctx.col_slate, ctx.theme_secondary,
                                wid, kHitTap | kHitFocusSlot, param);
          break;
        case HomeSettingsItemId::SHOW_WEATHER:
          ui::toggle_card_row(it, cx, row_y, hs_w, hs_h, "Show Weather", prefs.home_show_weather,
                              ctx.font_small, ctx.theme_primary, ctx.col_white, ctx.col_slate, wid, param);
          break;
        case HomeSettingsItemId::WEATHER_TAP: {
          const char *wt_short = (prefs.home_weather_tap_action == 0U)
                                     ? "Current"
                                     : ((prefs.home_weather_tap_action == 1U) ? "Forecast" : "None");
          std::snprintf(buf, sizeof(buf), "Weather Tap: %s", wt_short);
          ui::setting_cycle_row(it, cx, row_y, hs_w, hs_h, buf, true,
                                ctx.font_small, ctx.col_white, ctx.col_slate, ctx.theme_secondary,
                                wid, kHitTap | kHitFocusSlot, param);
          break;
        }
        case HomeSettingsItemId::SHOW_BATTERY:
          ui::toggle_card_row(it, cx, row_y, hs_w, hs_h, "Show Battery", prefs.home_show_battery,
                              ctx.font_small, ctx.theme_primary, ctx.col_white, ctx.col_slate, wid, param);
          break;
        case HomeSettingsItemId::WAKE_GREETING:
          ui::toggle_card_row(it, cx, row_y, hs_w, hs_h, "Wake Greeting", prefs.wake_greeting,
                              ctx.font_small, ctx.theme_primary, ctx.col_white, ctx.col_slate, wid, param);
          break;
        case HomeSettingsItemId::WEATHER_BG:
          ui::toggle_card_row(it, cx, row_y, hs_w, hs_h, "Weather Sky", prefs.weather_background,
                              ctx.font_small, ctx.theme_primary, ctx.col_white, ctx.col_slate, wid, param);
          break;
        case HomeSettingsItemId::HOME_SUN:
          ui::toggle_card_row(it, cx, row_y, hs_w, hs_h, "Sun Indicator", prefs.home_sun,
                              ctx.font_small, ctx.theme_primary, ctx.col_white, ctx.col_slate, wid, param);
          break;
        case HomeSettingsItemId::HOME_MOON:
          ui::toggle_card_row(it, cx, row_y, hs_w, hs_h, "Moon Indicator", prefs.home_moon,
                              ctx.font_small, ctx.theme_primary, ctx.col_white, ctx.col_slate, wid, param);
          break;
        case HomeSettingsItemId::WEATHER_APP_BG:
          ui::toggle_card_row(it, cx, row_y, hs_w, hs_h, "Weather App Sky", prefs.weather_app_background,
                              ctx.font_small, ctx.theme_primary, ctx.col_white, ctx.col_slate, wid, param);
          break;
      }
    }
    if (total_pages > 1) {
      render_vertical_page_pips(it, hs_pg > 1, hs_pg < total_pages, ctx.col_bg, ctx.theme_secondary, ctx.col_slate);
    }
    render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back");
    return;
  }

  int act_count = 0;
  const HomeActivityItem *act_items = collect_home_activities_cached(ctx, music_playing, music_title, music_artist, act_count);

  std::string cond = ws.condition;
  bool is_night = (ws.sun_state == "below_horizon" || cond == "clear-night" ||
                   cond == "clearnight" || (cond.find("night") != std::string::npos));
  apply_weather_sim_override(ctx.weather_sim_opt, cond, is_night);

  static std::string s_memo_cond_in, s_memo_cond_title = "Unavailable";
  if (cond != s_memo_cond_in) {
    s_memo_cond_in = cond;
    s_memo_cond_title = format_weather_title(cond);
  }
  const std::string &cond_title = s_memo_cond_title;
  const bool sun_ready = (!ws.sun_state.empty() && ws.sun_state != "unknown" && ws.sun_state != "unavailable");
  const bool weather_ready = ((!std::isnan(ws.temperature) && sun_ready) || sim_override_on) && (cond_title != "Unavailable");
  const bool live_sky_drawn = (bd_mode == BackdropMode::LiveWeatherSky && weather_ready);
  const int cel_x = layout().celestial_x, cel_y = layout().celestial_y;
  const bool show_celestial = is_night ? ctx.home_moon_phase : ctx.home_sun_arc;
  double moon_cycle = 0.5;
  bool moon_known = true;
  if (weather_ready && is_night) {
    moon_known = compute_synodic_moon_phase(
        ctx.now.is_valid(), ctx.now.is_valid() ? ctx.now.timestamp : 0LL,
        ws.moon_phase, moon_cycle, nullptr, nullptr);
  }
  const bool celestial_visible =
      weather_ready && show_celestial &&
      weather_condition_shows_celestial(cond, is_night) &&
      (!is_night || moon_known);

  if (home_subpage == 2) {
    set_home_activity_deck_rotates(false);
    home_activity_card_shown() = false;
    home_activity_rendered_rows() = 0;
    reset_home_activity_anim_state(false);
    if (bd_mode == BackdropMode::DayNightSky) {
      if (dns_known) render_static_day_night_sky_base(it, cx, cy, dns_night);
      else frame_base(it, ctx.col_bg);
    } else if (bd_mode == BackdropMode::CustomWallpaper) {
      render_album_art_canvas(it, ctx.home_wallpaper_art, cx, false, false, true, false, 2);
    } else if ((show_weather_bg || sim_override_on) && (weather_ready || dns_known)) {
      backdrop_active() = true;
      const bool eff_night = weather_ready ? is_night : dns_night;
#if LATTER_ROUND_DISPLAY
      frame_base(it, ctx.col_bg);
      if (!eff_night) {
        fast_filled_circle(it, cx, cy, SCREEN_MIN_R - 5, UiColor(7, 13, 22));
      }
#else
      frame_base(it, eff_night ? ctx.col_bg : UiColor(7, 13, 22));
#endif
      if (celestial_visible) {
        fast_filled_circle(it, cel_x, cel_y, LATTER_ROUND_DISPLAY ? 23 : scale_min(27),
                           is_night ? UiColor(22, 26, 34) : UiColor(42, 36, 12));
      }
    } else {
      frame_base(it, ctx.col_bg);
    }
  } else if (live_sky_drawn) {
    backdrop_active() = true;
    render_weather_motion_wallpaper(it, cx, cy, now_ms, cond, is_night, moon_cycle, cel_x, cel_y,
                                    rs.hw_accel_x, rs.hw_accel_y, celestial_visible);
  } else if (bd_mode == BackdropMode::LiveWeatherSky) {
    if (dns_known) {
      render_neutral_day_night_sky_base(it, cx, cy, dns_night);
    } else {
      frame_base(it, ctx.col_bg);
    }
  } else if (bd_mode == BackdropMode::DayNightSky) {
    if (dns_known) {
      render_static_day_night_sky_base(it, cx, cy, dns_night);
    } else {
      frame_base(it, ctx.col_bg);
    }
  } else if (bd_mode == BackdropMode::CustomWallpaper) {
    render_album_art_canvas(it, ctx.home_wallpaper_art, cx, false, false, true, false, 0);
  } else {
    frame_base(it, ctx.col_bg);
  }
  frame_base(it, ctx.col_bg);

  if (home_subpage == 2) {
    char hdr_buf[36] = "ACTIVE ITEMS";
    if (ctx.now.is_valid()) {
      char t_sub[16];
      format_time_hhmm(ctx.now.hour, ctx.now.minute, is_24h_format, t_sub, sizeof(t_sub));
      std::snprintf(hdr_buf, sizeof(hdr_buf), "ACTIVE  •  %s", t_sub);
    }
    render_page_header(it, cx, hdr_buf, ctx.font_small, ctx.col_white, true);

    if (act_count <= 0) {
      render_empty_state(it, cx, cy, "No active activities", ctx.font_small, ctx.col_slate);
    } else {
      const int total_pages = std::max(1, (act_count + 4) / 5);
      if (home_activity_overlay_page() >= total_pages || home_activity_overlay_page() < 0) home_activity_overlay_page() = 0;
      const int start_i = home_activity_overlay_page() * 5;
      const int vis_cnt = std::min(5, act_count - start_i);
      for (int row = 0; row < vis_cnt; row++) {
        const int y_mid = get_activity_overlay_card_y(vis_cnt, row);
        render_home_activity_list_row(it, cx, y_mid, act_items[start_i + row], ctx.font_small,
                                      ctx.theme_secondary, ctx.col_white,
                                      kChromeGlassTint, kChromeCardBorder, kChromeGlassTint);
        frame_hits().add_centered_rect(cx, y_mid, layout().list_card_w, layout().slot5_h,
                                       WidgetId::LIST_SLOT_BASE + static_cast<uint16_t>(row),
                                       kHitTap | kHitFocusSlot, static_cast<int16_t>(start_i + row));
      }
      if (total_pages > 1) {
        render_vertical_page_pips(it, home_activity_overlay_page() > 0, home_activity_overlay_page() + 1 < total_pages,
                                  ctx.col_bg, ctx.theme_secondary, ctx.col_slate);
      }
    }
    render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back");
    return;
  }

  if (live_sky_drawn && celestial_visible) {
    frame_hits().add_circle(cel_x, cel_y, LATTER_ROUND_DISPLAY ? scale_min(30) : scale_min(34),
                            WidgetId::HOME_QUICK_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle,
                            static_cast<int16_t>(is_night ? 1 : 0), scale_s(10));
  }

  const UiColor fg_home_cond = (!live_sky_drawn || is_night) ? ctx.theme_secondary : kChromeDayGold;
  const int z_date_y = layout().home_date_y, z_clock_y = layout().home_clock_y, z_weath_y = layout().home_weather_y;

  ProfLap home_lap;
  if (prefs.home_show_date && ctx.now.is_valid()) {
    char date_bar[32];
    ctx.now.strftime(date_bar, sizeof(date_bar), "%a, %b %d");
    ui::shadow_print(it, cx, z_date_y, ctx.font_date, ctx.col_white, UiTextAlign::CENTER, date_bar);
    if (prefs.home_date_tap_action != 2U) {
      const int date_hit_w = std::min(scale_x(230), safe_span(z_date_y, 16));
      const int date_hit_h = std::min(scale_y(34), std::max(24, (z_clock_y - scale_y(38) - z_date_y) * 2));
      frame_hits().add_centered_rect(cx, z_date_y, date_hit_w, date_hit_h, WidgetId::HOME_DATE_HERO, kHitTap | kHitFocusSlot);
    }
  }

  if (prefs.home_show_time) {
    char time_str[16];
    std::snprintf(time_str, sizeof(time_str), "%s", kUnknownGlyph);
    if (ctx.now.is_valid()) format_time_hhmm(ctx.now.hour, ctx.now.minute, is_24h_format, time_str, sizeof(time_str));
    ui::shadow_print(it, cx, z_clock_y, ctx.font_time, ctx.col_white, UiTextAlign::CENTER, time_str);
    if (prefs.home_clock_tap_action != 2U) {
      frame_hits().add_centered_rect(cx, z_clock_y, scale_x(260), scale_y(72), WidgetId::HOME_TIME_READOUT, kHitTap | kHitFocusSlot);
    }
  }

  if (prefs.home_show_weather && weather_ready) {
    char w_line[64];
    if (!std::isnan(ws.temperature)) std::snprintf(w_line, sizeof(w_line), "%.0f° • %s", ws.temperature, cond_title.c_str());
    else std::snprintf(w_line, sizeof(w_line), "%s", cond_title.c_str());
    const int max_w_span = std::min(scale_x(340), safe_span(z_weath_y));
    const std::string fit_w = fit_text_to_width_font(std::string(w_line), max_w_span, ctx.font_date);
    fast_print(it, cx + 1, z_weath_y + 1, ctx.font_date, ctx.col_black, UiTextAlign::CENTER, fit_w.c_str());
    fast_print(it, cx, z_weath_y, ctx.font_date, fg_home_cond, UiTextAlign::CENTER, fit_w.c_str());
    if (prefs.home_weather_tap_action != 2U) {
      frame_hits().add_centered_rect(cx, z_weath_y, std::min(scale_x(300), max_w_span), scale_y(36),
                                     WidgetId::HOME_WEATHER_HERO, kHitTap | kHitFocusSlot);
    }
  }

  const auto &ly = layout();
  const HomeActivityView view = resolve_home_activity_view(act_items, act_count, now_ms, ly.home_activity_two_line());

  if (rs.nav.wake_greeting_pending) {
    if (!show_wake_greeting || view.pinned) {
      rs.nav.wake_greeting_pending = false;
    } else if (ctx.now.is_valid()) {
      rs.nav.wake_greeting_pending = false;
      wake_greeting_latched() = true;
      wake_greeting_start_ms() = (now_ms == 0U) ? 1U : now_ms;
      const std::string greet_cond = weather_ready ? cond : std::string{};
      const float greet_temp_f = weather_ready ? weather_temp_to_fahrenheit(ws.temperature, ws.temp_unit) : NAN;
      wake_greeting_latched_text() =
          resolve_wake_greeting(ctx.now.hour, ctx.now.minute, ctx.now.day_of_year, greet_cond, greet_temp_f);
    } else if (now_ms >= 15000U) {
      // Non-RTC cold-boot grace expired without valid time; avoid firing a late greeting.
      rs.nav.wake_greeting_pending = false;
    }
  }
  if (wake_greeting_latched()) {
    const uint32_t el_chk = (now_ms >= wake_greeting_start_ms()) ? (now_ms - wake_greeting_start_ms()) : 0U;
    if (!show_wake_greeting || view.pinned || !ctx.now.is_valid() || el_chk >= kWakeGreetingTotalMs) {
      wake_greeting_latched() = false;
      wake_greeting_latched_text() = nullptr;
    }
  }
  const uint32_t greet_el = (wake_greeting_latched() && now_ms >= wake_greeting_start_ms())
                                ? (now_ms - wake_greeting_start_ms()) : 999999U;
  const bool greeting_active = wake_greeting_latched() && (greet_el < kWakeGreetingTotalMs);

  if (ctx.font_date != nullptr) {
    static bool weather_h_measured = false;
    if (!weather_h_measured) {
      int m_w = 0, m_xo = 0, m_bl = 0, m_h = 0;
      ctx.font_date->measure("Ag", &m_w, &m_xo, &m_bl, &m_h);
      if (m_h > 0) home_weather_text_h() = m_h;
      weather_h_measured = true;
    }
  }
  const int weather_h = home_weather_text_h();
  const int act_card_cy = ly.home_activity_cy(weather_h);
  const int h1 = ly.home_activity_h_for_rows(1), card_w = ly.home_activity_w(), pitch = ly.home_activity_pitch();

  const HomeActivityStepResult step = step_home_activity_card(
      now_ms, home_subpage, greeting_active, weather_h, view, act_items, act_count);
  const auto &st = home_act_state();

  if (step.draw_active) {
    const int card_h = step.card_h;
    const int card_y1 = ly.is_round ? (ly.home_activity_cy_for_h(weather_h, card_h) + card_h / 2) : ly.home_activity_bottom();
    const int card_y0 = card_y1 - card_h, cur_cy = card_y0 + card_h / 2, card_x0 = cx - card_w / 2;
    const float e = step.e, surf_alpha = step.surf_alpha;

    UiColor card_bg = kChromeGlassTint;
    if (draw_glass_activity_surface(it, card_x0, card_y0, card_w, card_h, surf_alpha,
                                    step.animating, ctx.col_bg, ctx.col_white, card_bg)) {
      const int prev_clip_y0 = clip_y0(), prev_clip_y1 = clip_y1();
      const int inner_y0 = std::max(prev_clip_y0, card_y0 + 1), inner_y1 = std::min(prev_clip_y1, card_y0 + card_h - 2);
      const int y_top_slot = card_y0 + h1 / 2, y_bot_slot = card_y1 - h1 / 2;
      const float shrink_out_a = std::clamp(1.0f - e * 1.65f, 0.0f, 1.0f);

      if (inner_y0 <= inner_y1) {
        set_clip_y_band(inner_y0, inner_y1);
        const int rise = scale_y(16);
        auto draw_item = [&](const HomeActivityItem &it_act, int y, float alpha, bool two_row_style) {
          if (it_act.kind == 0 || alpha <= 0.04f) return;
          draw_home_activity_row_contents(it, y, it_act, step.has_drop, false, ctx.font_small,
                                          card_bg, kChromeGlassTint, ctx.theme_secondary, ctx.col_white,
                                          std::min(1.0f, alpha), two_row_style);
        };
        auto change_in_place = [&](const HomeActivityItem &from_it, const HomeActivityItem &to_it, int y, bool two_row_style) {
          if (!step.animating || from_it.kind == 0 || same_activity_id(from_it, to_it)) {
            draw_item(to_it, y, surf_alpha, two_row_style);
            return;
          }
          draw_item(from_it, y - static_cast<int>(static_cast<float>(rise) * e), (1.0f - e) * surf_alpha, two_row_style);
          draw_item(to_it, y + static_cast<int>(static_cast<float>(rise) * (1.0f - e)), e * surf_alpha, two_row_style);
        };

        if (step.animating && st.from_rows == 0) {
          const int rise_in = static_cast<int>(static_cast<float>(scale_y(10)) * (1.0f - e));
          if (step.target_rows == 1) {
            draw_item(st.cur_top_item, cur_cy + rise_in, surf_alpha, false);
          } else {
            draw_item(st.cur_top_item, y_top_slot + rise_in, surf_alpha, true);
            draw_item(st.cur_bot_item, y_bot_slot + rise_in, surf_alpha, true);
          }
        } else if (step.animating && st.from_rows == 1 && step.target_rows == 2) {
          if (same_activity_id(st.cur_top_item, st.from_top_item)) {
            draw_item(st.cur_top_item, y_top_slot, 1.0f, true);
            draw_item(st.cur_bot_item, y_bot_slot + static_cast<int>(static_cast<float>(rise) * (1.0f - e)), e, true);
          } else if (same_activity_id(st.cur_bot_item, st.from_top_item)) {
            draw_item(st.cur_top_item, y_top_slot - static_cast<int>(static_cast<float>(scale_y(10)) * (1.0f - e)), e, true);
            draw_item(st.cur_bot_item, y_bot_slot, 1.0f, true);
          } else {
            draw_item(st.from_top_item, y_bot_slot - static_cast<int>(static_cast<float>(rise) * e), 1.0f - e, true);
            draw_item(st.cur_top_item, y_top_slot, e, true);
            draw_item(st.cur_bot_item, y_bot_slot, e, true);
          }
        } else if (step.animating && st.from_rows == 2 && step.target_rows == 1) {
          if (same_activity_id(st.cur_top_item, st.from_bot_item)) {
            draw_item(st.from_top_item, y_top_slot, shrink_out_a, true);
            draw_item(st.cur_top_item, y_bot_slot, 1.0f, false);
          } else if (same_activity_id(st.cur_top_item, st.from_top_item)) {
            draw_item(st.from_bot_item, y_bot_slot, shrink_out_a, true);
            draw_item(st.cur_top_item, y_top_slot, 1.0f, false);
          } else {
            draw_item(st.from_top_item, y_top_slot, shrink_out_a, true);
            draw_item(st.cur_top_item, y_bot_slot, e, false);
          }
        } else if (step.target_rows == 1) {
          change_in_place(st.from_top_item, st.cur_top_item, cur_cy, false);
        } else if (step.animating && st.from_rows == 2 && !view.pinned && same_activity_id(st.cur_top_item, st.from_bot_item)) {
          draw_item(st.from_top_item, y_top_slot - static_cast<int>(static_cast<float>(rise) * e), 1.0f - e, true);
          draw_item(st.cur_top_item, y_bot_slot - static_cast<int>(static_cast<float>(pitch) * e), 1.0f, true);
          draw_item(st.cur_bot_item, y_bot_slot + static_cast<int>(static_cast<float>(rise) * (1.0f - e)), e, true);
        } else if (step.animating) {
          change_in_place(st.from_top_item, st.cur_top_item, y_top_slot, true);
          change_in_place(st.from_bot_item, st.cur_bot_item, y_bot_slot, true);
        } else {
          draw_item(st.cur_top_item, y_top_slot, 1.0f, true);
          draw_item(st.cur_bot_item, y_bot_slot, 1.0f, true);
        }

        float row_sep_a = 0.0f;
        if (step.target_rows == 2 && (st.from_rows == 2 || st.from_rows == 0)) row_sep_a = surf_alpha;
        else if (step.target_rows == 2 && st.from_rows == 1) row_sep_a = step.animating ? e : 1.0f;
        else if (step.target_rows == 1 && st.from_rows == 2 && step.animating) row_sep_a = shrink_out_a;

        draw_activity_card_chrome(it, cx, card_x0, card_w, card_h, cur_cy, y_top_slot, y_bot_slot,
                                  step.target_rows, step.has_drop, (!step.animating || st.from_rows != 1 || e > 0.25f),
                                  surf_alpha, row_sep_a, card_bg, ctx.col_white);
        set_clip_y_band(prev_clip_y0, prev_clip_y1);
      }
    }
    register_home_activity_hit_regions(frame_hits(), cx, step.target_cy, card_w, step.target_h,
                                       (step.target_rows >= 2) && (view.bottom >= 0), step.has_drop,
                                       view.top, view.bottom, scale_s(56), scale_s(52));
  } else if (step.draw_fade_out) {
    const int fade_h = step.card_h;
    const float fade_alpha = step.surf_alpha;
    const int card_y1 = ly.is_round ? (ly.home_activity_cy_for_h(weather_h, fade_h) + fade_h / 2) : ly.home_activity_bottom();
    const int card_y0 = card_y1 - fade_h, cur_cy = card_y0 + fade_h / 2, card_x0 = cx - card_w / 2;

    UiColor card_bg = kChromeGlassTint;
    if (draw_glass_activity_surface(it, card_x0, card_y0, card_w, fade_h, fade_alpha, true, ctx.col_bg, ctx.col_white, card_bg)) {
      const int prev_clip_y0 = clip_y0(), prev_clip_y1 = clip_y1();
      const int inner_y0 = std::max(prev_clip_y0, card_y0 + 1), inner_y1 = std::min(prev_clip_y1, card_y1 - 2);
      if (inner_y0 <= inner_y1) {
        set_clip_y_band(inner_y0, inner_y1);
        const int y_top_slot = card_y0 + h1 / 2, y_bot_slot = card_y1 - h1 / 2;
        if (st.fade_out_rows >= 2) {
          if (st.fade_out_top_item.kind != 0) {
            draw_home_activity_row_contents(it, y_top_slot, st.fade_out_top_item, st.fade_out_has_drop, false, ctx.font_small,
                                            card_bg, kChromeGlassTint, ctx.theme_secondary, ctx.col_white, fade_alpha, true);
          }
          if (st.fade_out_bot_item.kind != 0) {
            draw_home_activity_row_contents(it, y_bot_slot, st.fade_out_bot_item, st.fade_out_has_drop, false, ctx.font_small,
                                            card_bg, kChromeGlassTint, ctx.theme_secondary, ctx.col_white, fade_alpha, true);
          }
        } else if (st.fade_out_top_item.kind != 0) {
          draw_home_activity_row_contents(it, cur_cy, st.fade_out_top_item, st.fade_out_has_drop, false, ctx.font_small,
                                          card_bg, kChromeGlassTint, ctx.theme_secondary, ctx.col_white, fade_alpha, false);
        }
        draw_activity_card_chrome(it, cx, card_x0, card_w, fade_h, cur_cy, y_top_slot, y_bot_slot,
                                  st.fade_out_rows >= 2 ? 2 : 1, st.fade_out_has_drop, true,
                                  fade_alpha, st.fade_out_rows >= 2 ? fade_alpha : 0.0f, card_bg, ctx.col_white);
        set_clip_y_band(prev_clip_y0, prev_clip_y1);
      }
    }
  }

  if (greeting_active) {
    const char *raw_greet = wake_greeting_latched_text();
    if (raw_greet == nullptr) {
      const std::string greet_cond = weather_ready ? cond : std::string{};
      const float greet_temp_f = weather_ready ? weather_temp_to_fahrenheit(ws.temperature, ws.temp_unit) : NAN;
      raw_greet = resolve_wake_greeting(ctx.now.hour, ctx.now.minute, ctx.now.day_of_year, greet_cond, greet_temp_f);
      wake_greeting_latched_text() = raw_greet;
    }
    const std::string fit_greet = fit_text_to_width_font(
        std::string(raw_greet), std::min(scale_x(340), safe_span(act_card_cy)), ctx.font_date);
    const int greet_y = act_card_cy - 2;
    float alpha = 1.0f;
    if (greet_el > kWakeGreetingHoldMs) {
      const float u = std::clamp(
          1.0f - static_cast<float>(greet_el - kWakeGreetingHoldMs) / static_cast<float>(kWakeGreetingFadeMs), 0.0f, 1.0f);
      alpha = u * u * (3.0f - 2.0f * u);
    }
    const int alpha_q8 = std::clamp(static_cast<int>(std::lround(alpha * 256.0f)), 0, 256);
    if (alpha_q8 > 0) {
      const int shadow_q8 = (alpha_q8 * 192) >> 8;
      if (shadow_q8 > 8) fast_print_alpha(it, cx + 1, greet_y + 1, ctx.font_date, kChromeShadow, UiTextAlign::CENTER, fit_greet.c_str(), shadow_q8);
      fast_print_alpha(it, cx, greet_y, ctx.font_date, ctx.col_white, UiTextAlign::CENTER, fit_greet.c_str(), alpha_q8);
    }
  }

  home_lap.mark(ProfSect::HOME_TEXT);

  ShortcutItem *dock_items = dock_items_scratch();
  const int dock_count = collect_effective_dock_items(dock_items, kMaxEffectiveDockItems);
  if (dock_count > 0) {
    const int total_dock_pages = (dock_count + 3) / 4;
    if (home_dock_page() < 0 || home_dock_page() >= total_dock_pages) home_dock_page() = 0;
    int win_xs[4] = {0, 0, 0, 0};
    get_home_dock_window_xs(dock_count, home_dock_page(), win_xs);
    const int start_idx = get_home_dock_start_index(dock_count, home_dock_page());
    const int vis_count = std::min(4, dock_count - start_idx);
    const int dock_y = ly.dock_y, dock_r = ly.dock_r, dock_pad = std::max(0, ly.dock_hit_r() - dock_r);
    const int dock_gutter_w = std::max(12, scale_s(26));
    if (total_dock_pages > 1 && home_dock_page() > 0 && vis_count > 0) {
      const int lx1 = win_xs[0] - dock_r - 6;
      const int lx0 = std::max(0, lx1 - dock_gutter_w);
      if (lx1 - lx0 >= 8) {
        frame_hits().add_rect(lx0, dock_y - dock_r, lx1 - lx0, 2 * dock_r,
                              WidgetId::MODE_BAR_LEFT, kHitTap | kHitFocusSlot);
      }
    }
    for (int v = 0; v < vis_count; v++) {
      frame_hits().add_circle(win_xs[v], dock_y, dock_r, WidgetId::DOCK_SLOT_BASE + static_cast<uint16_t>(v),
                              kHitTap | kHitFocusSlot | kHitFocusCircle, static_cast<int16_t>(start_idx + v), dock_pad);
    }
    if (total_dock_pages > 1 && home_dock_page() + 1 < total_dock_pages && vis_count > 0) {
      const int rx0 = win_xs[vis_count - 1] + dock_r + 6;
      const int rx1 = std::min(SCREEN_W - 1, rx0 + dock_gutter_w);
      if (rx1 - rx0 >= 8) {
        frame_hits().add_rect(rx0, dock_y - dock_r, rx1 - rx0, 2 * dock_r,
                              WidgetId::MODE_BAR_RIGHT, kHitTap | kHitFocusSlot);
      }
    }

    const float anim_start = home_dock_anim().step_window(start_idx, now_ms);
    // Keep Dock hit-region registration above unconditional (DirtyBandTracker clears frame_hits()
    // every frame), and skip the Dock pixel-draw pass when rendering an upper-band clip above it.
    if (clip_y1() >= dock_dirty_band_y0() && clip_y0() < SCREEN_H) {
      const int dock_bounce_x = edge_bounce().offset_px(PageId::HOME, GestureAxisLock::Horizontal, now_ms, LATTER_PANEL_PPI);
      const int stride = get_dock_slot_stride(win_xs, vis_count);
      const int left_cx = win_xs[0], right_cx = win_xs[vis_count - 1], clip_pad = stride / 2 + dock_r, icon_r = ui::dock_icon_r(dock_r);

      for (int g_idx = 0; g_idx < dock_count; g_idx++) {
        const float rel_slot = static_cast<float>(g_idx) - anim_start;
        if (rel_slot < -0.85f || rel_slot > static_cast<float>(vis_count - 1) + 0.85f) continue;
        const int bx = left_cx + static_cast<int>(std::roundf(rel_slot * static_cast<float>(stride))) + dock_bounce_x;
        if (bx < left_cx - clip_pad || bx > right_cx + clip_pad) continue;

        float alpha = 1.0f;
        if (rel_slot < 0.0f) alpha = std::max(0.15f, 1.0f + rel_slot * 1.15f);
        else if (rel_slot > static_cast<float>(vis_count - 1)) alpha = std::max(0.15f, 1.0f - (rel_slot - static_cast<float>(vis_count - 1)) * 1.15f);

        const ShortcutItem &item = dock_items[g_idx];
        const bool is_entity = (item.kind == ShortcutKind::ENTITY);
        const bool item_active = is_entity && (item.state == 1);
        const bool item_off = is_entity && (item.state == 0);
        const bool item_unavail = is_entity && (item.state == 2);
        const bool is_inverted = (item.badge_style == ShortcutBadgeStyle::INVERTED);
        const bool has_accent = (item.accent != ShortcutAccent::DEFAULT);
        const UiColor accent_col = ui::resolve_shortcut_accent_color(item.accent, ctx, ctx.theme_primary);
        const int d = 2 * dock_r + 1;

        UiColor slot_fg_c = ctx.col_white;
        UiColor cut_c = kChromeGlassTint;
        bool icon_shadow = (alpha > 0.55f);

        if (is_inverted) {
          const UiColor base_orb = item_unavail ? ctx.col_gray : (has_accent ? accent_col : ctx.col_white);
          const int rim_top = static_cast<int>(std::roundf(88.0f * alpha));
          const int rim_bot = static_cast<int>(std::roundf(24.0f * alpha));
          if (alpha >= 0.99f) {
            fill_round_rect_aa(it, bx - dock_r, dock_y - dock_r, d, d,
                               static_cast<float>(dock_r) + 0.5f, base_orb, base_orb, 0);
          } else {
            const int orb_a = std::clamp(static_cast<int>(std::roundf(256.0f * alpha)), 0, 256);
            fill_round_rect_glass(it, bx - dock_r, dock_y - dock_r, d, d,
                                  static_cast<float>(dock_r) + 0.5f, base_orb, orb_a, orb_a);
          }
          if (rim_top > 0 || rim_bot > 0) {
            fill_round_rect_glass(it, bx - dock_r, dock_y - dock_r, d, d,
                                  static_cast<float>(dock_r) + 0.5f, kChromeGlassSheen, rim_top, rim_bot, 1);
          }
          slot_fg_c = (alpha >= 0.99f) ? kChromeShadow : lerp_color(base_orb, kChromeShadow, alpha);
          cut_c = base_orb;
          icon_shadow = false;
        } else {
          const bool show_tint = !item_unavail && (item_active || (has_accent && !item_off));
          const UiColor raw_bg = item_active ? ui::chrome_card_bg_on() : kChromeGlassTint;
          const UiColor raw_brd = show_tint ? accent_col : kChromeCardBorder;
          GlassBackdrop dock_bd{};
          UiColor eff_tint{}, rim_col{};
          int raw_a_top = 0, raw_a_bot = 0, raw_rim_top = 0, raw_rim_bot = 0;
          resolve_styled_glass(bx - dock_r, dock_y - dock_r, d, d, raw_bg, raw_brd,
                               GlassVariant::Clear, kChromeGlassSheen, -1, false,
                               dock_bd, eff_tint, rim_col, raw_a_top, raw_a_bot, raw_rim_top, raw_rim_bot);
          const int a_top = static_cast<int>(std::roundf(static_cast<float>(raw_a_top) * alpha));
          const int a_bot = static_cast<int>(std::roundf(static_cast<float>(raw_a_bot) * alpha));
          const int rim_boost_top = item_active ? 48 : (show_tint ? 32 : 0);
          const int rim_boost_bot = item_active ? 36 : (show_tint ? 24 : 0);
          const int sheen_top = static_cast<int>(std::roundf(static_cast<float>(std::min(255, raw_rim_top + rim_boost_top)) * alpha));
          const int sheen_bot = static_cast<int>(std::roundf(static_cast<float>(std::min(255, raw_rim_bot + rim_boost_bot)) * alpha));
          fill_round_rect_glass(it, bx - dock_r, dock_y - dock_r, d, d, static_cast<float>(dock_r) + 0.5f, rim_col, sheen_top, sheen_bot, 1);
          fill_round_rect_glass(it, bx - dock_r + 1, dock_y - dock_r + 1, d - 2, d - 2, static_cast<float>(dock_r) - 0.5f, eff_tint, a_top, a_bot);

          const UiColor base_fg = item_unavail
                                      ? ctx.col_gray
                                      : (item_off
                                             ? (has_accent ? ctx.col_slate : ctx.col_white)
                                             : (has_accent ? accent_col : ctx.col_white));
          slot_fg_c = scale_color(base_fg, alpha);
          cut_c = eff_tint;
        }

        if (item.kind == ShortcutKind::ROUTE && item.icon.empty()) {
          ui::draw_route_icon(it, item.route_id, bx, dock_y, icon_r, slot_fg_c, cut_c, icon_shadow);
        } else {
          char state_slug_buf[96];
          const std::string_view eff_slug =
              is_entity ? resolve_state_mdi_slug(item.icon, item.state, state_slug_buf, sizeof(state_slug_buf))
                        : std::string_view(item.icon);
          render_mdi_icon(it, eff_slug, bx, dock_y, std::max(16, 2 * icon_r), slot_fg_c, icon_shadow,
                          (item.kind == ShortcutKind::ROUTE) ? "mdi:apps" : "mdi:gesture-tap-button");
        }
      }
      render_viewport_scrollbar(it, cx, ly.dock_pips_y, dock_count, anim_start, ctx.theme_primary, false);
    }
  }

#if LATTER_HAS_BATTERY
  if (prefs.home_show_battery) {
    render_battery_indicator(it, ly.battery_cx, ly.battery_y, rs.hw_batt_level, rs.hw_batt_charging,
                             (SCREEN_W < 380 && ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small,
                             ctx.theme_secondary, ctx.col_gold, ctx.col_green);
  }
#endif
  home_lap.mark(ProfSect::HOME_DOCK);
}

}  // namespace latter_ui
