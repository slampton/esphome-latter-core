#pragma once

// ==============================================================================
// Latter UI Frame Cadence Controller, Home Activity Deck & HUD Auto-Fade
// (include/ui_cadence.h)
// Pure C++20 UI header — zero ESPHome or HAL includes.
// ==============================================================================

#include "state/device_runtime_state.h"
#include "ui_frame_context.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace latter_ui {

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

static constexpr uint32_t kHomeActivityRotateMs = 8000U;
static constexpr int kMaxHomeActivity = 12;
static constexpr int kUpcomingAlarmDeckMinutes = 360;
static constexpr int kUpcomingAlarmPinMinutes = 120;

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

inline uint32_t &weather_step_due_ms_ref() noexcept {
  static uint32_t due_ms = 0U;
  return due_ms;
}

inline bool is_weather_step_pending(uint32_t now_ms) noexcept {
  const uint32_t due = weather_step_due_ms_ref();
  return due != 0U && static_cast<int32_t>(due + 24U - now_ms) > 0;
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

  Cadence20msActions step_20ms(uint32_t now_ms, bool panel_on, bool wallpaper_loaded,
                               bool mww_running, bool va_running, bool tts_audio_active,
                               bool speaker_audio_busy, uint32_t rotate_period_ms);
  Cadence250msActions step_250ms(uint32_t now_ms, bool wallpaper_loaded, bool va_running,
                                 std::string_view latest_fw_version, uint32_t rotate_period_ms);
  CadenceScreensaverActions step_screensaver_20ms(uint32_t now_ms, bool speaker_audio_busy,
                                                  bool mww_running) const;

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

MusicHudAutoFadeResult step_music_hud_auto_fade(
    latter_core::MusicRuntimeState &m,
    uint32_t now_ms,
    std::string_view music_overlay_style_opt,
    bool touch_active = false) noexcept;

// Reserved disp1 Component numeric timeout IDs
inline constexpr uint32_t kTimeoutAsyncDrawId = 0x4C410001U;
inline constexpr uint32_t kTimeoutFbClearId = 0x4C410002U;
inline constexpr uint32_t kTimeoutWeatherStepId = 0x4C410003U;
inline constexpr uint32_t kTimeoutActivityRotateId = 0x4C410004U;
inline constexpr uint32_t kTimeoutTouchLongPressId = 0x4C410005U;
inline constexpr uint32_t kTimeoutMusicPeerBrowseId = 0x4C410006U;

// Shared cadence/dirty-band state hooks (implemented in page/fx TUs)
static constexpr uint32_t kWakeGreetingHoldMs = 2800U;
static constexpr uint32_t kWakeGreetingFadeMs = 1200U;
static constexpr uint32_t kWakeGreetingTotalMs = kWakeGreetingHoldMs + kWakeGreetingFadeMs;

int &home_activity_overlay_page();
int &snoozed_alarm_slot() noexcept;
uint32_t &wake_greeting_start_ms();
bool &wake_greeting_latched();
bool is_wake_greeting_visible(uint32_t now_ms, bool enabled);
bool is_wake_greeting_fading(uint32_t now_ms, bool enabled);
void dismiss_wake_greeting();
bool is_doorbell_or_intercom_active();
bool is_home_activity_carousel_animating(uint32_t now_ms);
bool is_home_activity_fading_out(uint32_t now_ms);
bool has_pending_home_activity_fade_settle(uint32_t now_ms);
bool home_activity_slide_active(uint32_t now_ms);
uint32_t home_activity_next_rotate_due_ms(uint32_t now_ms);
bool home_activity_band(int &out_y0, int &out_y1);
const HomeActivityItem *home_activity_rendered_items(int &out_count);

bool &weather_sim_dirty();
bool evaluate_sky_has_motion(std::string_view sim_opt, std::string_view raw_cond,
                             std::string_view sun_state, uint32_t now_ms);
bool weather_sky_motion_stepping(bool usb_connected, uint32_t now_ms, uint32_t latest_input_ms,
                                 bool has_motion_cond);

struct DiceRollState {
  int val_1 = 1;
  int val_2 = 6;
  bool anim_active = false;
  uint32_t anim_start_ms = 0;
};

DiceRollState &dice_state();
void trigger_dice_roll(uint32_t now_ms, uint32_t rand_a, uint32_t rand_b);

struct CoinFlipState {
  int side = 0;  // 0 = Heads, 1 = Tails
  bool anim_active = false;
  uint32_t anim_start_ms = 0;
};

CoinFlipState &coin_state();
void trigger_coin_flip(uint32_t now_ms, uint32_t rand_val);

enum class GameInputCmd : uint8_t {
  NONE = 0,
  LEFT = 1,
  RIGHT = 2,
  UP = 3,
  DOWN = 4,
  ROTATE = 5,
  HARD_DROP = 6,
  HOLD = 7,
  COL_TAP = 8,    // param = col 0..6
  MODE_AI = 9,
  MODE_2P = 10,
  PLAY_AGAIN = 11,
};

struct GameInputEvent {
  GameInputCmd cmd = GameInputCmd::NONE;
  int8_t param = 0;
};

struct GameInputQueue {
  static constexpr uint8_t kCap = 8U;
  GameInputEvent ring[kCap]{};
  uint8_t head = 0U;
  uint8_t tail = 0U;
  uint8_t count = 0U;
  GameInputCmd held_cmd = GameInputCmd::NONE;
  uint32_t held_since_ms = 0U;
  uint32_t held_last_rep_ms = 0U;

  void clear() noexcept {
    head = 0U;
    tail = 0U;
    count = 0U;
    held_cmd = GameInputCmd::NONE;
    held_since_ms = 0U;
    held_last_rep_ms = 0U;
  }

  bool push(GameInputCmd cmd, int8_t param = 0) noexcept {
    if (cmd == GameInputCmd::NONE) return false;
    if (count >= kCap) {
      head = static_cast<uint8_t>((head + 1U) % kCap);
      --count;
    }
    ring[tail] = {cmd, param};
    tail = static_cast<uint8_t>((tail + 1U) % kCap);
    ++count;
    return true;
  }

  bool pop(GameInputEvent &out) noexcept {
    if (count == 0U) return false;
    out = ring[head];
    head = static_cast<uint8_t>((head + 1U) % kCap);
    --count;
    return true;
  }

  void set_held(GameInputCmd cmd, uint32_t now_ms) noexcept {
    held_cmd = cmd;
    held_since_ms = now_ms;
    held_last_rep_ms = now_ms;
  }

  void release_held() noexcept {
    held_cmd = GameInputCmd::NONE;
  }
};

int &pending_game_sfx();
GameInputQueue &game_input();

}  // namespace latter_ui
