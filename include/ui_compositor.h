#pragma once

// ==============================================================================
// Latter UI Compositor, Frame Context & 2px-Aligned Dirty-Band Flush
// (include/ui_compositor.h)
// 1. Pure C++20 DirtyBandTracker + subpage/takeover signature helpers (compiles
//    standalone on host with zero ESPHome dependencies for tests/host/test_all.cpp).
// 2. Concrete UiFrameContext & SystemTakeoverState for compiled page_*.cpp TUs.
// 3. DisplayUpdateScheduler (both DispT* and DispT*const* overloads) and
//    FrameHitTable-driven render_active_focus_overlay().
// ==============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#include <cmath>
#include <type_traits>
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/time.h"
#include "hal_panel_power.h"
#include "hal_power_pm.h"
#include "latter_state.h"
#include "ui_types.h"
#include "ui_gfx.h"
#include "ui_hit_table.h"
#include "ui_layout.h"
#include "ui_widgets.h"
#else
#include "latter_state.h"
#include "ui_hit_table.h"
#ifndef LATTER_SCREEN_H
#define LATTER_SCREEN_H 466
#endif
namespace latter_ui {
static constexpr int SCREEN_H = LATTER_SCREEN_H;
}  // namespace latter_ui
#endif

namespace latter_ui {

// Zero-allocation null-terminated string view for UiFrameContext & SystemTakeoverState (D-034(2)).
// Binds only to string literals, const char * singletons, or lvalue std::string members;
// rvalue std::string temporaries are deleted to prevent dangling views at compile time.
struct UiStrView {
  const char *ptr_{""};
  size_t len_{0};

  constexpr UiStrView() noexcept = default;
  constexpr UiStrView(const char *s) noexcept
      : ptr_(s != nullptr ? s : ""),
        len_(s != nullptr ? std::char_traits<char>::length(s) : 0U) {}
  constexpr UiStrView(const char *s, size_t n) noexcept
      : ptr_(s != nullptr ? s : ""),
        len_(s != nullptr ? n : 0U) {}
  UiStrView(const std::string &s) noexcept : ptr_(s.c_str()), len_(s.size()) {}
  UiStrView(std::string &&) = delete;

  constexpr const char *c_str() const noexcept { return ptr_; }
  constexpr const char *data() const noexcept { return ptr_; }
  constexpr size_t size() const noexcept { return len_; }
  constexpr size_t length() const noexcept { return len_; }
  constexpr bool empty() const noexcept { return len_ == 0U; }
  constexpr char operator[](size_t idx) const noexcept { return ptr_[idx]; }

  constexpr std::string_view sv() const noexcept { return std::string_view(ptr_, len_); }
  constexpr operator std::string_view() const noexcept { return std::string_view(ptr_, len_); }
  inline std::string str() const { return std::string(ptr_, len_); }

  constexpr size_t find(std::string_view needle, size_t pos = 0U) const noexcept {
    return sv().find(needle, pos);
  }
  constexpr size_t find(char ch, size_t pos = 0U) const noexcept {
    return sv().find(ch, pos);
  }
  constexpr size_t rfind(std::string_view needle, size_t pos = std::string_view::npos) const noexcept {
    return sv().rfind(needle, pos);
  }
  constexpr std::string_view substr(size_t pos = 0U, size_t count = std::string_view::npos) const noexcept {
    return sv().substr(pos, count);
  }

  friend constexpr bool operator==(UiStrView a, UiStrView b) noexcept { return a.sv() == b.sv(); }
  friend constexpr bool operator!=(UiStrView a, UiStrView b) noexcept { return a.sv() != b.sv(); }
  friend constexpr bool operator==(UiStrView a, std::string_view b) noexcept { return a.sv() == b; }
  friend constexpr bool operator!=(UiStrView a, std::string_view b) noexcept { return a.sv() != b; }
  friend constexpr bool operator==(std::string_view a, UiStrView b) noexcept { return a == b.sv(); }
  friend constexpr bool operator!=(std::string_view a, UiStrView b) noexcept { return a != b.sv(); }
  friend constexpr bool operator==(UiStrView a, const char *b) noexcept {
    return a.sv() == (b != nullptr ? std::string_view(b) : std::string_view{});
  }
  friend constexpr bool operator!=(UiStrView a, const char *b) noexcept { return !(a == b); }
  friend constexpr bool operator==(const char *a, UiStrView b) noexcept { return b == a; }
  friend constexpr bool operator!=(const char *a, UiStrView b) noexcept { return !(b == a); }
  friend inline bool operator==(UiStrView a, const std::string &b) noexcept {
    return a.sv() == std::string_view(b);
  }
  friend inline bool operator!=(UiStrView a, const std::string &b) noexcept {
    return a.sv() != std::string_view(b);
  }
  friend inline bool operator==(const std::string &a, UiStrView b) noexcept {
    return std::string_view(a) == b.sv();
  }
  friend inline bool operator!=(const std::string &a, UiStrView b) noexcept {
    return std::string_view(a) != b.sv();
  }
};

enum class BackdropMode : uint8_t {
  SolidBlack = 0,
  CustomWallpaper = 1,
  LiveWeatherSky = 2,
  DayNightSky = 3,
};

// Single authoritative backdrop precedence shared across gui.yaml, timers_and_fetchers.yaml,
// page_clock_home.cpp, and page_weather.cpp (D-038, D-067):
// - Home root (gui_page == 1 && home_subpage == 0):
//     If home_weather_bg is ON (persisted pref OR active weather simulation): LiveWeatherSky.
//     Otherwise: DayNightSky (native "latter://day_night_sky") > CustomWallpaper > SolidBlack.
// - Sky & Weather (gui_page == 5):
//     If weather_app_bg is ON (persisted pref OR active weather simulation): LiveWeatherSky.
//     Otherwise: DayNightSky > CustomWallpaper > SolidBlack.
// - Home overlays (subpage 1 or 2), System Settings (4), Area (6), Remote (7),
//   Tools/Home Hub/Calendar/Cameras non-full-bleed (8/9/12/13), Time (10), Pinned (11),
//   and Music with Art Mode "None" (2):
//     DayNightSky > CustomWallpaper > SolidBlack.
// - Clock (0), Games (3), Music with Album Art enabled (2), and full-bleed tools/maps/cameras: SolidBlack.
constexpr BackdropMode resolve_backdrop_mode(int gui_page, int home_subpage,
                                             bool home_weather_bg,
                                             bool weather_app_bg,
                                             bool weather_sim_active,
                                             bool has_custom_wallpaper,
                                             bool has_day_night_sky = false,
                                             bool tools_full_bleed = false,
                                             bool music_art_none = false) noexcept {
  const BackdropMode fallback_wall =
      has_day_night_sky
          ? BackdropMode::DayNightSky
          : (has_custom_wallpaper ? BackdropMode::CustomWallpaper : BackdropMode::SolidBlack);
  if (gui_page == 1 && home_subpage == 0) {
    if (home_weather_bg || weather_sim_active) {
      return BackdropMode::LiveWeatherSky;
    }
    return fallback_wall;
  }
  if (gui_page == 5) {
    if (weather_app_bg || weather_sim_active) {
      return BackdropMode::LiveWeatherSky;
    }
    return fallback_wall;
  }
  if (gui_page == 1) {
    return fallback_wall;
  }
  if (gui_page == 2) {
    return music_art_none ? fallback_wall : BackdropMode::SolidBlack;
  }
  if (gui_page == 4 || gui_page == 6 || gui_page == 7 ||
      ((gui_page == 8 || gui_page == 9 || gui_page == 12 || gui_page == 13) && !tools_full_bleed) ||
      gui_page == 10 || gui_page == 11) {
    return fallback_wall;
  }
  return BackdropMode::SolidBlack;
}

struct DirtyBandTracker {
  int pending_y0{0};
  int pending_y1{SCREEN_H - 1};
  bool has_pending_band{false};
  bool force_full{true};

  int active_y0{0};
  int active_y1{SCREEN_H - 1};
  bool active_full{true};
  bool active_idle_skip{false};

  int last_page{-1};
  int last_subpage{-1};
  uint32_t last_overlay_sig{0xFFFFFFFFU};
  int last_hhmm{-2};
  uint32_t last_idle_state_sig{0xFFFFFFFFU};

  // Aligns any vertical span to 2px-aligned full-width rows:
  //   y0 -> even row (y0 & ~1), y1 -> odd row (min(SCREEN_H - 1, y1 | 1))
  static void align_band(int raw_y0, int raw_y1, int screen_h, int &out_y0, int &out_y1);

  void mark_band(int y0, int y1);
  void mark_full();
  void begin_frame(int page, int subpage, uint32_t overlay_sig, bool anim_or_scroll_active = false);
  bool begin_frame(int page, int subpage, uint32_t overlay_sig, bool anim_or_scroll_active,
                   int hhmm_key, uint32_t idle_state_sig, int home_clock_band_y1,
                   bool live_sec_active = false);

  constexpr int y0() const { return active_y0; }
  constexpr int y1() const { return active_y1; }
  constexpr int band_y0() const { return active_y0; }
  constexpr int band_y1() const { return active_y1; }
  constexpr bool is_full() const { return active_full; }
  constexpr bool is_idle_skip() const { return active_idle_skip; }
};

DirtyBandTracker &dirty_band();
void mark_dirty_band(int y0, int y1);
void mark_dirty_full();

inline constexpr int music_metadata_band_y0_for(int /*screen_h*/) noexcept { return 0; }
inline constexpr int music_metadata_band_y1_for(int screen_h) noexcept {
  return scale_y_for(136, screen_h);
}
inline constexpr int music_progress_band_y0_for(int screen_h) noexcept {
  return scale_y_for(74, screen_h);
}
inline constexpr int music_progress_band_y1_for(int screen_h) noexcept {
  return scale_y_for(136, screen_h);
}
inline constexpr int music_metadata_band_y0() noexcept { return 0; }
inline constexpr int music_metadata_band_y1() noexcept { return music_metadata_band_y1_for(SCREEN_H); }
inline constexpr int music_progress_band_y0() noexcept { return music_progress_band_y0_for(SCREEN_H); }
inline constexpr int music_progress_band_y1() noexcept { return music_progress_band_y1_for(SCREEN_H); }
void mark_music_progress_dirty_band(int y0 = 0, int y1 = music_metadata_band_y1_for(SCREEN_H));

// Folds all secondary subpage dimensions (home_subpage + home_dock_page,
// tools_subpage + tools_menu_page + cal_detail_mode + timer_view_slot,
// system_menu_mode + system_dashboard_page + sleep_menu_page + display_menu_page,
// home_hub_tab) into one unambiguous sub_id for DirtyBandTracker::begin_frame.
constexpr int compute_subpage_id(int gui_page, int home_subpage, int home_dock_pg,
                                 int music_mode, int active_game,
                                 int sys_mode, int sys_dash_pg, int sleep_pg, int disp_pg,
                                 int weather_sub, int area_sub, int rem_sub,
                                 int tools_sub, int tools_menu_pg, int cal_detail, int timer_slot,
                                 int hub_tab,
                                 int time_sub = 0, int wc_pg = 0,
                                 int alarm_edit = 0, int home_set_pg = 1,
                                 int hub_mode_pg = 0, int hub_drill = 0,
                                 int pinned_pg = 0, int cal_sub_id = 0,
                                 int cam_sub_id = 0) {
  switch (gui_page) {
    case 1:
      return (home_subpage & 0xFF) | ((home_dock_pg & 0xFF) << 8) | ((home_set_pg & 0x0F) << 16);
    case 2:
      return music_mode;
    case 3:
      return active_game;
    case 4:
      return (sys_mode & 0xFF) | ((sys_dash_pg & 0x0F) << 8) |
             ((sleep_pg & 0x0F) << 12) | ((disp_pg & 0x0F) << 16);
    case 5:
      return weather_sub;
    case 6:
      return area_sub;
    case 7:
      return rem_sub;
    case 8:
      return (tools_sub & 0xFF) | ((tools_menu_pg & 0xFF) << 8) |
             ((cal_detail & 0x0F) << 16) | ((timer_slot & 0x0F) << 20);
    case 9:
      return (hub_tab & 0x0F) | ((hub_mode_pg & 0x0F) << 4) | ((hub_drill & 0x0F) << 8);
    case 10:
      return (time_sub & 0x0F) | ((wc_pg & 0x0F) << 4) |
             ((timer_slot & 0x0F) << 8) | ((alarm_edit & 0x0F) << 12);
    case 11:
      return pinned_pg & 0xFFFF;
    case 12:
      return cal_sub_id;
    case 13:
      return cam_sub_id;
    default:
      return 0;
  }
}

// Pure-C++20 VA takeover precedence check (D-066): during doorbell PTT talk
// (`intercom_turn == true`), keep live camera video visible instead of hiding
// it behind the full-screen Voice Assistant orb!
inline constexpr bool should_va_takeover_preempt(bool va_active, bool has_status_text,
                                                 bool intercom_turn, int gui_page,
                                                 bool live_popup_active) noexcept {
  if (!va_active && !has_status_text) return false;
  if (intercom_turn && (gui_page == PageId::CAMERAS || live_popup_active)) {
    return false;
  }
  return true;
}

uint32_t compute_takeover_signature(bool ota_in_progress, bool is_hibernating, bool panel_is_on,
                                    int display_power_state, bool alert_ringing, bool va_active,
                                    bool pin_pad_open, bool notif_active,
                                    bool shortcut_confirm_open = false);

// ==============================================================================
// Pure C++20 FrameCadenceController (20 ms Active, 250 ms Wake, 20 ms Screensaver)
// Replaces ~395 lines of YAML tick state machines across timers_and_fetchers.yaml
// and power_manager.yaml; compiles standalone in tests/host/test_all.cpp.
// ==============================================================================
struct Cadence20msInputs {
  uint32_t now_ms = 0U;
  bool panel_on = true;
  int display_power_state = 0;
  int gui_page = 1;
  int home_subpage = 0;
  int active_game = 0;
  bool game_sfx_enabled = true;
  int pending_game_sfx = 0;
  bool tools_anim_active = false;
  bool time_anim_active = false;
  bool clock_smooth_sweep = false;
  bool interactive_slide = false;
  bool dock_anim_active = false;
  int dock_y0 = 0;
  int dock_y1 = 0;
  bool find_my_active = false;
  bool ringing_or_critical = false;
  bool va_visible = false;
  bool va_running = false;
  bool tts_received = false;
  bool tts_audio_active = false;
  bool touch_active = false;
  bool menu_scroll_active = false;
  bool mww_running = false;
  bool is_chip_p4 = false;
  bool speaker_audio_busy = false;
  bool render_in_flight = false;
  uint32_t last_ambient_ms = 0U;
  uint32_t last_disp_end_ms = 0U;
  bool weather_step_pending = false;
  bool sky_enabled = false;
  bool sky_stepping = false;
  bool sim_just_changed = false;
  float motion_mag = 0.0f;
  uint32_t home_rotate_due_ms = 0U;
  uint32_t rotate_period_ms = 8000U;
  uint32_t pause_exp_due_ms = 0U;
  bool has_act_band = false;
  int act_y0 = 0;
  int act_y1 = 0;
  bool greet_fading = false;
  bool act_sliding = false;
  bool act_fade_settle = false;
  bool video_stream_active = false;
};

struct Cadence20msActions {
  bool enable_game_audio_path = false;
  int play_game_sfx_id = 0;
  bool clear_pending_game_sfx = false;
  bool va_transition_holdoff = false;
  bool bump_user_touch_ms = false;
  bool record_ambient_ms = false;
  bool schedule_anim_full = false;
  bool schedule_anim_band = false;
  bool schedule_activity_rotate = false;
  uint32_t activity_rotate_delay_ms = 0U;
  bool schedule_band_now = false;
  bool schedule_disp_now = false;
  int band_y0 = 0;
  int band_y1 = 0;
  bool sync_disp_poller = false;
  bool sync_disp_poller_val = false;
  bool stop_anim_poller = false;
  bool anim_driving_disp = false;
};

struct Cadence250msInputs {
  uint32_t now_ms = 0U;
  int display_power_state = 0;
  bool is_hibernating = false;
  int gui_page = 1;
  int home_subpage = 0;
  int system_menu_mode = 0;
  int active_game = 0;
  uint8_t activity_mask = 0U;
  uint32_t rotate_period_ms = 8000U;
  uint32_t home_rotate_due_ms = 0U;
  uint32_t pause_exp_due_ms = 0U;
  bool has_act_band = false;
  int act_y0 = 0;
  int act_y1 = 0;
  bool sky_canvas_active = false;
  float motion_mag = 0.0f;
  bool anim_poller_running = false;
  bool sky_motion_due = false;
  bool greet_fade_due = false;
  bool tool_anim_due = false;
  bool time_anim_due = false;
  bool clock_sweep_due = false;
  bool va_busy = false;
  bool alert_or_critical_busy = false;
  bool home_overlay_motion_due = false;
  bool nav_scroll_or_sim_dirty = false;
  bool pending_wallpaper_fetch = false;
  bool video_stream_active = false;
  std::string_view latest_fw_version{};
};

struct Cadence250msActions {
  bool ensure_anim_poller = false;
  bool schedule_disp_now = false;
  bool schedule_band_now = false;
  bool schedule_activity_rotate = false;
  uint32_t activity_rotate_delay_ms = 0U;
  int band_y0 = 0;
  int band_y1 = 0;
  bool bump_user_touch_ms = false;
  bool fetch_wallpaper = false;
  bool fw_version_changed = false;
};

struct CadenceScreensaverInputs {
  uint32_t now_ms = 0U;
  int display_power_state = 0;
  bool is_hibernating = false;
  bool va_visible = false;
  std::string_view screensaver_style{};
  bool speaker_audio_busy = false;
  bool alert_ringing = false;
  bool mww_running = false;
  bool is_chip_p4 = false;
  bool render_in_flight = false;
  uint32_t last_ambient_ms = 0U;
  uint32_t last_disp_end_ms = 0U;
};

struct CadenceScreensaverActions {
  bool record_ambient_ms = false;
  bool schedule_anim_full = false;
  bool sync_disp_poller = false;
  bool sync_disp_poller_val = false;
};

// Pure-C++20 Home Activity Card model, recurrence-aware alarm windowing, and pinning/carousel
// resolver (D-028 / D-039 / D-045). Shared across firmware TUs and host unit tests.
static constexpr uint32_t kHomeActivityRotateMs = 8000U;
static constexpr int kMaxHomeActivity = 12;
static constexpr int kUpcomingAlarmDeckMinutes = 360;  // 6h front-card upcoming window (D-045)
static constexpr int kUpcomingAlarmPinMinutes = 120;   // 2h imminent top-pin window (Android At-a-Glance baseline)

struct HomeActivityItem {
  int kind = 0;        // 1 = Timer, 2 = Stopwatch, 3 = Music, 4 = Alarm, 5 = Calendar, 6 = Intercom/Doorbell
  int slot_idx = 0;    // Timer slot (0..2), Alarm slot (0 = Snoozed, 1..3 = Alarm 1..3), or Calendar slot
  bool is_active = false;
  bool urgent_pin = false;
  bool in_deck = true;
  std::string label{};
};

struct HomeActivityView {
  int top = -1;
  int bottom = -1;
  bool pinned = false;
  int front_count = 0;
  int visible_rows = 0;
};

inline int next_alarm_minutes_until(uint8_t alarm_h, uint8_t alarm_m, bool enabled,
                                    uint8_t recurrence, int cur_hour, int cur_min,
                                    int cur_day_of_week = -1) {
  if (!enabled || cur_hour < 0 || cur_hour > 23 || cur_min < 0 || cur_min > 59 ||
      alarm_h > 23 || alarm_m > 59) {
    return -1;
  }
  const int now_total_m = cur_hour * 60 + cur_min;
  const int a_total_m = static_cast<int>(alarm_h) * 60 + static_cast<int>(alarm_m);
  const int start_day = (a_total_m > now_total_m) ? 0 : 1;
  if (cur_day_of_week < 1 || cur_day_of_week > 7 || recurrence == 0 || recurrence == 3) {
    return start_day * 1440 + (a_total_m - now_total_m);
  }
  // ESPTime::day_of_week: 1=SUN, 2=MON, ..., 6=FRI, 7=SAT
  for (int d = start_day; d <= start_day + 7; ++d) {
    const int cand_dow = ((cur_day_of_week - 1 + d) % 7) + 1;
    const bool matches = (recurrence == 1)
                             ? (cand_dow >= 2 && cand_dow <= 6)
                             : ((recurrence == 2) ? (cand_dow == 1 || cand_dow == 7) : true);
    if (matches) {
      return d * 1440 + (a_total_m - now_total_m);
    }
  }
  return -1;
}

inline HomeActivityView resolve_home_activity_view(const HomeActivityItem items[], int count,
                                                   uint32_t now_ms, bool two_line) {
  HomeActivityView v;
  if (count <= 0) return v;

  // Top-pin priority hierarchy (D-045):
  //   1. Live Intercom / Doorbell (kind == 6)
  //   2. Running Countdown Timer  (kind == 1, sorted shortest remaining first)
  //   3. Snoozed Alarm            (kind == 4, slot_idx == 0)
  //   4. Running Stopwatch        (kind == 2)
  //   5. Imminent Armed Alarm     (kind == 4, slot_idx >= 1, <= 2h, sorted soonest first)
  static constexpr int kPinKindOrder[5] = {6, 1, 4, 2, 4};
  int pin = -1;
  for (int pass = 0; pass < 5 && pin < 0; ++pass) {
    const int target_kind = kPinKindOrder[pass];
    for (int i = 0; i < count; ++i) {
      if (!items[i].urgent_pin || items[i].kind != target_kind) continue;
      if (pass == 2 && items[i].slot_idx != 0) continue;
      if (pass == 4 && items[i].slot_idx == 0) continue;
      pin = i;
      break;
    }
  }

  int deck[kMaxHomeActivity];
  int n = 0;
  for (int i = 0; i < count && n < kMaxHomeActivity; ++i) {
    if ((items[i].in_deck || items[i].urgent_pin) && i != pin) {
      deck[n++] = i;
    }
  }

  v.front_count = (pin >= 0 ? 1 : 0) + n;
  if (v.front_count <= 0) {
    return v;
  }

  const uint32_t slot = now_ms / kHomeActivityRotateMs;
  if (pin >= 0) {
    v.top = pin;
    v.pinned = true;
    if (two_line && n > 0) {
      v.bottom = (n == 1) ? deck[0] : deck[slot % static_cast<uint32_t>(n)];
    }
    v.visible_rows = (v.bottom >= 0) ? 2 : 1;
    return v;
  }

  const int max_lines = two_line ? 2 : 1;
  if (n <= max_lines) {
    v.top = deck[0];
    if (two_line && n > 1) v.bottom = deck[1];
    v.visible_rows = (v.bottom >= 0) ? 2 : 1;
    return v;
  }

  v.top = deck[slot % static_cast<uint32_t>(n)];
  if (two_line) v.bottom = deck[(slot + 1U) % static_cast<uint32_t>(n)];
  v.visible_rows = (v.bottom >= 0) ? 2 : 1;
  return v;
}

class FrameCadenceController {
 public:
  static constexpr uint32_t interactive_gap_for(bool is_chip_p4, bool mww_running) noexcept {
    return is_chip_p4 ? 36U : (mww_running ? 54U : 36U);
  }

  static constexpr bool ambient_step_ready(uint32_t now_ms, uint32_t last_ambient_ms,
                                           uint32_t last_disp_end_ms, bool render_in_flight,
                                           bool is_chip_p4, bool speaker_audio_busy,
                                           bool va_busy, bool alert_busy,
                                           bool mww_running) noexcept {
    if (va_busy || render_in_flight) return false;
    uint32_t min_post_idle = 18U;
    uint32_t min_period = 54U;
    if (is_chip_p4) {
      min_post_idle = speaker_audio_busy ? 26U : 16U;
      min_period = alert_busy ? 120U : (speaker_audio_busy ? 74U : 54U);
    } else {
      const bool audio_or_mww = speaker_audio_busy || mww_running;
      min_post_idle = audio_or_mww ? 32U : 18U;
      min_period = alert_busy ? 120U : (audio_or_mww ? 74U : 54U);
    }
    if (now_ms - last_disp_end_ms < min_post_idle) return false;
    return (now_ms - last_ambient_ms >= min_period);
  }

  void reset();
  Cadence20msActions evaluate_20ms(const Cadence20msInputs &in);
  Cadence250msActions evaluate_250ms(const Cadence250msInputs &in);
  CadenceScreensaverActions evaluate_screensaver_20ms(const CadenceScreensaverInputs &in) const;

#if __has_include("esphome/core/defines.h")
  Cadence20msActions step_20ms(uint32_t now_ms, bool panel_on, bool wallpaper_loaded,
                               bool mww_running, bool va_running, bool tts_audio_active,
                               bool speaker_audio_busy, uint32_t rotate_period_ms);
  Cadence250msActions step_250ms(uint32_t now_ms, bool wallpaper_loaded, bool va_running,
                                 std::string_view latest_fw_version, uint32_t rotate_period_ms);
  CadenceScreensaverActions step_screensaver_20ms(uint32_t now_ms, bool speaker_audio_busy,
                                                  bool mww_running) const;
#endif

 private:
  int prev_audio_game_ = 0;
  uint32_t last_va_anim_ms_ = 0U;
  bool prev_tts_active_ = false;
  bool prev_pause_exp_pending_20ms_ = false;
  uint32_t last_cond_eval_ms_ = 0U;
  bool last_sky_enabled_ = false;
  bool cached_has_motion_ = false;
  uint8_t idle_anim_ticks_ = 0U;

  uint32_t prev_home_slot_ = 0U;
  bool prev_pause_exp_pending_250ms_ = false;
  int prev_disp_pwr_ = -1;
  std::string prev_fw_ver_{};
};

FrameCadenceController &frame_cadence();

struct MusicHudAutoFadeResult {
  bool changed{false};
  bool sprung_back_to_now_playing{false};
};

// Evaluates Music App v3 Smart HUD Auto-Fade & Transient Control-Tab Spring-Back (D-059):
//   - Ambient tabs (NOW_PLAYING, LYRICS, INFO): fade hud_active -> false at 6000 ms while staying
//     on the ambient tab (except NOW_PLAYING when overlay style is "Always Visible").
//   - Transient tabs (VOLUME: 5000 ms; LIBRARY/SPEAKERS/QUEUE/ALBUM_TRACKS: 10000 ms): spring back
//     to NOW_PLAYING, carousel_page = 0, mode_bar_win_start = 0, hud_active = Always Visible.
MusicHudAutoFadeResult step_music_hud_auto_fade(
    MusicRuntimeState &m,
    uint32_t now_ms,
    std::string_view music_overlay_style_opt,
    bool touch_active = false) noexcept;

// Reserved disp1 Component numeric timeout IDs:
//   0x4C410001U..0x4C41000FU : UI compositor schedulers (below)
//   0x4C410010U..0x4C41001FU : HAL panel power & MIPI-DCS SLPOUT/DISPON state machine (include/hal_panel_power.h)
inline constexpr uint32_t kTimeoutAsyncDrawId = 0x4C410001U;
inline constexpr uint32_t kTimeoutFbClearId = 0x4C410002U;
inline constexpr uint32_t kTimeoutWeatherStepId = 0x4C410003U;
inline constexpr uint32_t kTimeoutActivityRotateId = 0x4C410004U;
inline constexpr uint32_t kTimeoutTouchLongPressId = 0x4C410005U;

#if __has_include("esphome/core/defines.h")

// ------------------------------------------------------------------------------
// Display Update Scheduler (Zero-UB CoreTimeoutBridge via LatterLoopProbe)
// ------------------------------------------------------------------------------
struct DisplayUpdateScheduler {
  static constexpr uint32_t kTimeoutAsyncDraw = kTimeoutAsyncDrawId;
  static constexpr uint32_t kTimeoutFbClear = kTimeoutFbClearId;
  static constexpr uint32_t kTimeoutWeatherStep = kTimeoutWeatherStepId;
  static constexpr uint32_t kTimeoutActivityRotate = kTimeoutActivityRotateId;

  static inline uint32_t &weather_due_ms_ref() {
    static uint32_t due_ms = 0U;
    return due_ms;
  }

  static inline bool weather_step_pending(uint32_t now_ms) {
    const uint32_t due = weather_due_ms_ref();
    return due != 0U && static_cast<int32_t>(due + 24U - now_ms) > 0;
  }

  // Post-frame hook: a 0 ms timeout runs after the current update() (and its panel transfer) returns.
  static void request_post_frame(const esphome::Component *comp, void (*fn)()) {
    if (comp == nullptr || fn == nullptr) return;
    schedule_core_timeout(kTimeoutFbClear, 0U, [fn]() { fn(); });
  }

  template <typename DispT>
  static void request_ptr(DispT *disp) {
    if (disp != nullptr && !screen_is_off_standby() && !disp_poller_held()) {
      const uint32_t delay_ms = disp_render_in_flight(esphome::millis()) ? 4U : 0U;
      schedule_core_timeout(
          kTimeoutAsyncDraw, delay_ms, [disp]() {
            if (screen_is_off_standby() || disp_poller_held()) return;
            disp->update();
            fb_clear_after_update();
          });
    }
  }

  template <typename DispT>
  static void request_anim_ptr(DispT *disp) {
    if (disp != nullptr && !screen_is_off_standby() && !disp_poller_held()) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
      constexpr uint32_t kFrameMs = 18U;
      constexpr uint32_t kMinPostIdleMs = 6U;
#else
      constexpr uint32_t kFrameMs = 26U;
      constexpr uint32_t kMinPostIdleMs = 10U;
#endif
      const uint32_t now_m = esphome::millis();
      const bool inside_render = disp_render_in_flight(now_m);
      const uint32_t elapsed_start = now_m - last_disp_update_ms();
      const uint32_t elapsed_end = now_m - last_disp_update_end_ms();
      uint32_t delay_ms = (elapsed_start < kFrameMs) ? (kFrameMs - elapsed_start) : 0U;
      if (inside_render) {
        delay_ms = std::max<uint32_t>(delay_ms, 8U);
      } else if (elapsed_end < kMinPostIdleMs) {
        delay_ms = std::max<uint32_t>(delay_ms, kMinPostIdleMs - elapsed_end);
      }
      schedule_core_timeout(
          kTimeoutAsyncDraw, delay_ms, [disp]() {
            if (screen_is_off_standby() || disp_poller_held()) return;
            disp->update();
            fb_clear_after_update();
          });
    }
  }

  template <typename DispT>
  static void request_weather_ptr(DispT *disp, uint32_t target_period_ms) {
    if (disp != nullptr && !screen_is_off_standby() && !disp_poller_held()) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
      constexpr uint32_t kMinPostIdleMs = 8U;
#else
      constexpr uint32_t kMinPostIdleMs = 12U;
#endif
      const uint32_t now_m = esphome::millis();
      const bool inside_render = disp_render_in_flight(now_m);
      const uint32_t elapsed_start = now_m - last_disp_update_ms();
      uint32_t delay_ms = (elapsed_start < target_period_ms) ? (target_period_ms - elapsed_start) : 0U;
      if (inside_render) {
        delay_ms = std::max<uint32_t>(delay_ms, kMinPostIdleMs);
      } else {
        const uint32_t elapsed_end = now_m - last_disp_update_end_ms();
        if (elapsed_end < kMinPostIdleMs) {
          delay_ms = std::max<uint32_t>(delay_ms, kMinPostIdleMs - elapsed_end);
        }
      }
      weather_due_ms_ref() = (now_m + delay_ms == 0U) ? 1U : (now_m + delay_ms);
      schedule_core_timeout(
          kTimeoutWeatherStep, delay_ms, [disp]() {
            weather_due_ms_ref() = 0U;
            if (screen_is_off_standby() || disp_poller_held()) return;
            if (disp_render_in_flight(esphome::millis())) return;
            mark_dirty_full();
            disp->update();
            fb_clear_after_update();
          });
    }
  }

  template <typename DispT>
  static void request_activity_rotate_ptr(DispT *disp, uint32_t delay_ms, int y0, int y1) {
    if (disp != nullptr && !screen_is_off_standby() && !disp_poller_held()) {
      schedule_core_timeout(
          kTimeoutActivityRotate, delay_ms, [disp, y0, y1]() {
            if (screen_is_off_standby() || disp_poller_held()) return;
            if (disp_render_in_flight(esphome::millis())) return;
            mark_dirty_band(y0, y1);
            disp->update();
            fb_clear_after_update();
          });
    }
  }
};

template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void schedule_disp_update(DispT *disp) {
  mark_dirty_full();
  DisplayUpdateScheduler::request_ptr(disp);
}

template <typename DispT>
inline void schedule_disp_update(DispT *const *disp_ptr) {
  mark_dirty_full();
  if (disp_ptr != nullptr) DisplayUpdateScheduler::request_ptr(*disp_ptr);
}

template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void schedule_band_disp_update(DispT *disp, int y0, int y1) {
  mark_dirty_band(y0, y1);
  DisplayUpdateScheduler::request_ptr(disp);
}

template <typename DispT>
inline void schedule_band_disp_update(DispT *const *disp_ptr, int y0, int y1) {
  mark_dirty_band(y0, y1);
  if (disp_ptr != nullptr) DisplayUpdateScheduler::request_ptr(*disp_ptr);
}

template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void schedule_anim_disp_update(DispT *disp) {
  mark_dirty_full();
  DisplayUpdateScheduler::request_anim_ptr(disp);
}

template <typename DispT>
inline void schedule_anim_disp_update(DispT *const *disp_ptr) {
  mark_dirty_full();
  if (disp_ptr != nullptr) DisplayUpdateScheduler::request_anim_ptr(*disp_ptr);
}

template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void schedule_anim_band_disp_update(DispT *disp, int y0, int y1) {
  mark_dirty_band(y0, y1);
  DisplayUpdateScheduler::request_anim_ptr(disp);
}

template <typename DispT>
inline void schedule_anim_band_disp_update(DispT *const *disp_ptr, int y0, int y1) {
  mark_dirty_band(y0, y1);
  if (disp_ptr != nullptr) DisplayUpdateScheduler::request_anim_ptr(*disp_ptr);
}

template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void schedule_weather_disp_update(DispT *disp, uint32_t target_period_ms) {
  DisplayUpdateScheduler::request_weather_ptr(disp, target_period_ms);
}

template <typename DispT>
inline void schedule_weather_disp_update(DispT *const *disp_ptr, uint32_t target_period_ms) {
  if (disp_ptr != nullptr) DisplayUpdateScheduler::request_weather_ptr(*disp_ptr, target_period_ms);
}

template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void schedule_activity_rotate_disp_update(DispT *disp, uint32_t delay_ms, int y0, int y1) {
  DisplayUpdateScheduler::request_activity_rotate_ptr(disp, delay_ms, y0, y1);
}

template <typename DispT>
inline void schedule_activity_rotate_disp_update(DispT *const *disp_ptr, uint32_t delay_ms, int y0, int y1) {
  if (disp_ptr != nullptr) DisplayUpdateScheduler::request_activity_rotate_ptr(*disp_ptr, delay_ms, y0, y1);
}

// Pauses disp1's 1 Hz PollingComponent while a high-refresh animation loop actively owns frame
// cadence so the 1 s poller never fires a back-to-back double-render on top of an animation frame.
template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void sync_disp_poller_for_anim(DispT *disp, bool anim_active) {
  if (disp == nullptr) return;
  if (screen_is_off_standby() || disp_poller_held()) {
    anim_poller_suppressed() = false;
    return;
  }
  if (anim_active) {
    if (!anim_poller_suppressed()) {
      anim_poller_suppressed() = true;
      disp->stop_poller();
    }
  } else if (anim_poller_suppressed()) {
    anim_poller_suppressed() = false;
    disp->start_poller();
  }
}

template <typename DispT>
inline void sync_disp_poller_for_anim(DispT *const *disp_ptr, bool anim_active) {
  if (disp_ptr != nullptr) sync_disp_poller_for_anim(*disp_ptr, anim_active);
}

// ------------------------------------------------------------------------------
// Concrete Per-Frame Render Context (passed to all compiled page_*.cpp TUs)
// ------------------------------------------------------------------------------
struct UiFrameContext {
  esphome::ESPTime now{};
  uint32_t now_ms{0};
  int cx{SCREEN_CX};
  int cy{SCREEN_CY};
  const char *friendly_name{""};
  const char *fw_version{""};

  // Fonts (concrete BaseFont*)
  esphome::display::BaseFont *font_time{nullptr};
  esphome::display::BaseFont *font_chrono_time{nullptr};
  esphome::display::BaseFont *font_date{nullptr};
  esphome::display::BaseFont *font_btn_bold{nullptr};
  esphome::display::BaseFont *font_small{nullptr};
  esphome::display::BaseFont *font_micro{nullptr};
  esphome::display::BaseFont *font_games_title{nullptr};

  // Decoded RGB565 image views
  ArtImageView album_art{};
  ArtImageView remote_album_art{};
  ArtImageView home_wallpaper_art{};
  ArtImageView screensaver_photo_art{};
  ArtImageView camera_snapshot_art{};

  // Palette colors
  esphome::Color theme_primary{};
  esphome::Color theme_secondary{};
  esphome::Color col_bg{};
  esphome::Color col_black{};
  esphome::Color col_white{};
  esphome::Color col_slate{};
  esphome::Color col_gray{};
  esphome::Color col_dark_gray{};
  esphome::Color col_gold{};
  esphome::Color col_warm_gold{};
  esphome::Color col_solaris_gold{};
  esphome::Color col_cyan{};
  esphome::Color col_accent_cyan{};
  esphome::Color col_green{};
  esphome::Color col_orange{};
  esphome::Color col_red{};
  esphome::Color col_purple{};
  esphome::Color col_magenta{};

  // Live ESPHome component/entity snapshots (zero-copy UiStrView — zero heap allocs per frame)
  bool panel_is_on{true};
  bool local_media_playing{false};
  int spk_vol_pct{60};
  int alert_vol_pct{80};
  int vol_step_pct{5};
  int free_heap_kb{0};
  float wifi_rssi{NAN};
  UiStrView ip_addr{};
  bool ble_active{false};
  UiStrView track_title{};
  UiStrView track_artist{};
  UiStrView track_album{};
  UiStrView fw_latest_ver{};
  UiStrView fw_release_summary{};
  UiStrView primary_wake_word{"Okay Nabu"};

  // Select options
  UiStrView clock_face_style_opt{"Analog"};
  UiStrView weather_sim_opt{"Live (Home Assistant)"};
  UiStrView music_overlay_style_opt{"Art + Metadata"};
  UiStrView music_fullscreen_art_opt{"Full Screen"};
  UiStrView music_album_sort_opt{"A-Z"};
  UiStrView music_artist_album_sort_opt{"Release Year"};
  UiStrView music_podcast_sort_opt{"Latest Episode"};
  UiStrView theme_palette_opt{"Steel & Silver"};
  UiStrView tap_wake_opt{"Always On"};
  UiStrView pickup_wake_opt{"Medium"};
  UiStrView display_off_timeout_opt{"30 Seconds"};
  UiStrView screensaver_mode_opt{"Docked Only"};
  UiStrView screensaver_style_opt{"Ambient Clock"};
  UiStrView screensaver_docked_timeout_opt{"5 Minutes"};
  UiStrView hibernate_timeout_opt{"30 Minutes"};
  UiStrView bedtime_display_opt{"Screen Off"};
  UiStrView default_start_app_opt{"Home"};
  UiStrView auto_brightness_opt{"Time Schedule"};
  UiStrView va_wake_word_mode_opt{"Always On"};
  UiStrView va_visual_profile_opt{"Nebula"};
  UiStrView ww_sens_okay_nabu_opt{"Medium"};
  UiStrView ww_sens_hey_luna_opt{"Off"};
  UiStrView ww_sens_hey_jarvis_opt{"Off"};
  UiStrView ww_sens_hey_mycroft_opt{"Off"};
  UiStrView ww_sens_alexa_opt{"Off"};

  // Numbers
  int manual_brightness_pct{80};
  int day_brightness_pct{90};
  int night_brightness_pct{35};
  int day_start_hr{7};
  int night_start_hr{21};

  // Switches
  bool home_weather_bg{true};
  bool weather_app_bg{true};
  bool home_wake_greeting{true};
  bool home_sun_arc{true};
  bool home_moon_phase{true};
  bool game_sound_enabled{true};
  bool va_wake_screen{true};
  bool screensaver_photo_clock{false};
  bool instant_crown_hibernate{false};
  bool bedtime_active{false};
};

struct SystemTakeoverState {
  int cx{SCREEN_CX};
  int cy{SCREEN_CY};
  uint32_t now_ms{0};
  bool is_24h_format{false};
  bool ota_in_progress{false};
  int ota_progress_pct{0};
  bool is_hibernating{false};
  int hibernate_countdown_val{0};
  bool panel_is_on{true};
  int display_power_state{0};
  bool alarm_ringing{false};
  int alarm_ringing_idx{0};
  int alarm1_recurrence{1};
  int alarm2_recurrence{2};
  int alarm3_recurrence{2};
  bool timer_ringing{false};
  bool find_my_active{false};
  UiStrView va_timer_name{};
  bool va_active{false};
  UiStrView va_status_text{};
  bool va_tts_received{false};
  bool va_vad_active{false};
  bool va_intercom_turn{false};
  UiStrView va_visual_profile_opt{};
  bool notif_active{false};
  UiStrView notif_title{};
  UiStrView notif_message{};
  UiStrView notif_priority{};
  UiStrView notif_camera_url{};
  UiStrView notif_camera_entity{};
  UiStrView current_loaded_camera_url{};
  UiStrView screensaver_style_opt{};
  bool screensaver_photo_clock{false};
  esphome::Color theme_primary{};
  esphome::Color theme_secondary{};
  esphome::Color col_bg{};
  esphome::Color col_black{};
  esphome::Color col_white{};
  esphome::Color col_slate{};
  esphome::Color col_gray{};
  esphome::Color col_dark_gray{};
  esphome::Color col_gold{};
  esphome::Color col_red{};
  esphome::Color col_solaris_gold{};

  static inline SystemTakeoverState from_context(const UiFrameContext &ctx) {
    const auto &rs = runtime_state();
    const auto &prefs = persisted_prefs();
    SystemTakeoverState st{};
    st.cx = ctx.cx;
    st.cy = ctx.cy;
    st.now_ms = ctx.now_ms;
    st.is_24h_format = prefs.is_24h_format;
    st.ota_in_progress = rs.nav.ota_in_progress;
    st.ota_progress_pct = rs.nav.ota_progress_pct;
    st.is_hibernating = rs.nav.is_hibernating;
    st.hibernate_countdown_val = rs.nav.hibernate_countdown_val;
    st.panel_is_on = ctx.panel_is_on;
    st.display_power_state = rs.nav.display_power_state;
    st.alarm_ringing = rs.alarms.ringing;
    st.alarm_ringing_idx = rs.alarms.ringing_idx;
    st.alarm1_recurrence = prefs.alarms[0].recurrence;
    st.alarm2_recurrence = prefs.alarms[1].recurrence;
    st.alarm3_recurrence = prefs.alarms[2].recurrence;
    st.timer_ringing = rs.timers.ringing;
    st.find_my_active = rs.alarms.find_my_active;
    st.va_timer_name = rs.timers.va_timer_name;
    st.va_active = rs.voice.active;
    st.va_status_text = rs.voice.status_text;
    st.va_tts_received = rs.voice.tts_received;
    st.va_vad_active = rs.voice.vad_active;
    st.va_intercom_turn = rs.voice.intercom_turn;
    st.va_visual_profile_opt = ctx.va_visual_profile_opt;
    st.notif_active = rs.alert.active;
    st.notif_title = rs.alert.title;
    st.notif_message = rs.alert.message;
    st.notif_priority = rs.alert.priority;
    st.notif_camera_url = rs.alert.camera_url;
    st.notif_camera_entity = rs.alert.camera_entity;
    st.current_loaded_camera_url = rs.alert.loaded_camera_url;
    st.screensaver_style_opt =
        prefs.bedtime_active ? UiStrView("Ambient Clock") : ctx.screensaver_style_opt;
    st.screensaver_photo_clock = !prefs.bedtime_active && prefs.screensaver_photo_clock;
    st.theme_primary = ctx.theme_primary;
    st.theme_secondary = ctx.theme_secondary;
    st.col_bg = ctx.col_bg;
    st.col_black = ctx.col_black;
    st.col_white = ctx.col_white;
    st.col_slate = ctx.col_slate;
    st.col_gray = ctx.col_gray;
    st.col_dark_gray = ctx.col_dark_gray;
    st.col_gold = ctx.col_gold;
    st.col_red = ctx.col_red;
    st.col_solaris_gold = ctx.col_solaris_gold;
    return st;
  }
};

// PSRAM-backed per-frame scratch singletons (eliminates ~1.1 KB from disp1's lambda stack frame).
UiFrameContext &acquire_frame_context();
const SystemTakeoverState &acquire_takeover_state(const UiFrameContext &ctx);

inline uint32_t compute_takeover_signature(const SystemTakeoverState &st) {
  const auto &rs = runtime_state();
  const bool va_preempt = should_va_takeover_preempt(
      st.va_active, !st.va_status_text.empty(), st.va_intercom_turn,
      rs.nav.gui_page, rs.cameras.doorbell_live_popup_active);
  return compute_takeover_signature(
      st.ota_in_progress, st.is_hibernating, st.panel_is_on,
      st.display_power_state,
      st.alarm_ringing || st.timer_ringing || st.find_my_active,
      va_preempt,
      ui::pin_pad_state().open, st.notif_active,
      shortcut_confirm_state().open);
}

inline int compute_active_subpage_id() {
  const auto &rs = runtime_state();
  const int hub_drill = rs.intercom.live_call_mode ? 4 : 0;
  int cal_vert_pg = rs.cal.overview_page;
  if (rs.cal.drilldown >= CalDrilldown::PICK_TITLE) {
    cal_vert_pg = rs.cal.picker_page;
  } else if (rs.cal.subpage == CalendarSubpage::DAY) {
    cal_vert_pg = rs.cal.day_page;
  } else if (rs.cal.subpage == CalendarSubpage::WEEK) {
    cal_vert_pg = rs.cal.week_page;
  }
  const int cal_sub_id =
      (rs.cal.subpage & 0x0F) |
      ((rs.cal.drilldown & 0x0F) << 4) |
      ((cal_vert_pg & 0xFF) << 8) |
      (rs.cal.delete_confirm_open ? (1 << 16) : 0);
  const int music_sub_id =
      (static_cast<int>(rs.music.view) & 0x0F) |
      ((static_cast<int>(rs.music.drilldown) & 0x07) << 4) |
      ((static_cast<int>(rs.music.takeover) & 0x07) << 7) |
      ((rs.music.mode_bar_win_start & 0x07) << 10) |
      ((rs.music.overlay_mode & 0x0F) << 13);
  const int cam_sub_id =
      (rs.cameras.tab & 0x0F) |
      ((rs.cameras.grid_page & 0x0F) << 4) |
      (rs.cameras.hud_active ? (1 << 8) : 0) |
      (rs.cameras.quick_reply_open ? (1 << 9) : 0) |
      (rs.cameras.landscape_mode ? (1 << 10) : 0);
  return compute_subpage_id(
      rs.nav.gui_page, rs.nav.home_subpage, home_dock_page(),
      music_sub_id, rs.nav.active_game,
      rs.nav.system_menu_mode, rs.nav.system_dashboard_page,
      rs.nav.sleep_menu_page, rs.nav.display_menu_page,
      rs.nav.sky_weather_subpage, rs.nav.area_subpage, rs.nav.remote_subpage,
      rs.nav.tools_subpage, rs.nav.tools_menu_page,
      0, timer_view_slot(), home_hub_tab(),
      rs.nav.time_subpage, rs.nav.world_clock_page,
      rs.alarms.edit_idx, rs.nav.home_settings_page,
      home_hub_mode_bar_page(), hub_drill, rs.nav.pinned_menu_page,
      cal_sub_id, cam_sub_id);
}

inline int compute_hhmm_key(const esphome::ESPTime &now) {
  return now.is_valid()
      ? (static_cast<int>(now.day_of_year) * 1440 + static_cast<int>(now.hour) * 60 + static_cast<int>(now.minute))
      : -1;
}

bool is_music_lyrics_anim_active(uint32_t now_ms, bool local_media_playing = false);
uint32_t compute_idle_state_signature(const UiFrameContext &ctx);
bool compute_live_sec_active(const UiFrameContext &ctx);

// Draws rotary/touchless focus ring from FrameHitTable::focus_at(ui_focus_index()).
void render_active_focus_overlay(esphome::display::Display &it, uint32_t now_ms,
                                 esphome::Color theme_primary, esphome::Color col_white);

inline void render_active_focus_overlay(esphome::display::Display &it, int /*page*/, int /*subpage*/,
                                        uint32_t now_ms, esphome::Color theme_primary,
                                        esphome::Color col_white) {
  render_active_focus_overlay(it, now_ms, theme_primary, col_white);
}

// Renders all system-owned global takeovers in strict priority order:
//   OTA -> Hibernation -> Panel Off -> Alarm/Timer/Find-My -> Voice Assistant ->
//   PIN Pad Modal -> Push Notification -> Docked Screensaver.
// Returns true when a takeover owned the frame.
bool render_system_takeovers(esphome::display::Display &it, const UiFrameContext &ctx,
                             const SystemTakeoverState &st);

template <typename DispT, typename TickT, typename EnableSpkFn, typename PlaySfxFn>
inline void dispatch_cadence_20ms(uint32_t now_ms, DispT *disp, TickT *anim_tick,
                                  bool panel_on, bool wallpaper_loaded,
                                  bool mww_running, bool va_running,
                                  bool tts_audio_active, bool speaker_audio_busy,
                                  uint32_t rotate_period_ms,
                                  EnableSpkFn &&enable_spk_fn, PlaySfxFn &&play_sfx_fn) {
  const auto act = frame_cadence().step_20ms(
      now_ms, panel_on, wallpaper_loaded, mww_running, va_running,
      tts_audio_active, speaker_audio_busy, rotate_period_ms);
  if (act.enable_game_audio_path) {
    enable_spk_fn();
  }
  if (act.play_game_sfx_id > 0) {
    play_sfx_fn(act.play_game_sfx_id);
  }
  if (act.va_transition_holdoff) {
    sync_disp_poller_for_anim(disp, true);
    return;
  }
  if (act.schedule_activity_rotate) {
    schedule_activity_rotate_disp_update(disp, act.activity_rotate_delay_ms, act.band_y0, act.band_y1);
  } else if (act.schedule_band_now) {
    schedule_band_disp_update(disp, act.band_y0, act.band_y1);
  } else if (act.schedule_disp_now) {
    schedule_disp_update(disp);
  }
  if (act.record_ambient_ms) {
    record_ambient_frame_ms(now_ms);
  }
  if (act.schedule_anim_band) {
    schedule_anim_band_disp_update(disp, act.band_y0, act.band_y1);
  } else if (act.schedule_anim_full) {
    schedule_anim_disp_update(disp);
  }
  if (act.sync_disp_poller) {
    sync_disp_poller_for_anim(disp, act.sync_disp_poller_val);
  }
  if (act.stop_anim_poller) {
    sync_disp_poller_for_anim(disp, false);
    set_ui_anim_poller(anim_tick, false);
  }
}

template <typename DispT, typename TickT, typename FetchWallFn>
inline void dispatch_cadence_250ms(uint32_t now_ms, DispT *disp, TickT *anim_tick,
                                   bool wallpaper_loaded, bool va_running,
                                   std::string_view latest_fw_version,
                                   uint32_t rotate_period_ms,
                                   FetchWallFn &&fetch_wall_fn) {
  const auto act = frame_cadence().step_250ms(
      now_ms, wallpaper_loaded, va_running, latest_fw_version, rotate_period_ms);
  if (act.ensure_anim_poller) {
    ensure_ui_anim_poller(anim_tick);
  }
  if (act.schedule_activity_rotate) {
    schedule_activity_rotate_disp_update(disp, act.activity_rotate_delay_ms, act.band_y0, act.band_y1);
  } else if (act.schedule_band_now) {
    schedule_band_disp_update(disp, act.band_y0, act.band_y1);
  }
  if (act.schedule_disp_now || act.fw_version_changed) {
    schedule_disp_update(disp);
  }
  if (act.fetch_wallpaper) {
    fetch_wall_fn();
  }
}

template <typename DispT>
inline void dispatch_cadence_screensaver_20ms(uint32_t now_ms, DispT *disp,
                                              bool speaker_audio_busy, bool mww_running) {
  const auto act = frame_cadence().step_screensaver_20ms(now_ms, speaker_audio_busy, mww_running);
  if (act.record_ambient_ms) {
    record_ambient_frame_ms(now_ms);
  }
  if (act.schedule_anim_full) {
    schedule_anim_disp_update(disp);
  }
  if (act.sync_disp_poller) {
    sync_disp_poller_for_anim(disp, act.sync_disp_poller_val);
  }
}

#endif  // __has_include("esphome/core/defines.h")

}  // namespace latter_ui
