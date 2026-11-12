#pragma once

// ==============================================================================
// Latter Compiled On-Device Touch, Rotary & Hit-Table Input Engine
// (include/ui_touch_engine.h)
// Unifies TouchRouter (TOUCH_DEBOUNCE_*, allow_tap) + TouchGestureEngine
// (FrameHitTable tap/slider/focus dispatch + mm swipe gestures).
// ==============================================================================

#include <cstdint>
#include <string>

#include "hal_power_pm.h"
#include "latter_actions.h"
#include "latter_state.h"
#include "ui_compositor.h"
#include "ui_hit_table.h"
#include "ui_layout.h"
#include "ui_types.h"
#include "ui_widgets.h"

namespace latter_ui {

// ------------------------------------------------------------------------------
// 1. Standardized Touch Timing Constants & Single Tap Debounce Gate
// ------------------------------------------------------------------------------
static constexpr uint32_t TOUCH_DEBOUNCE_RAPID     = 100;  // ms: Rapid steppers
static constexpr uint32_t TOUCH_DEBOUNCE_BUTTON    = 150;  // ms: Standard buttons & pills
static constexpr uint32_t TOUCH_DEBOUNCE_WAKE      = 180;  // ms: Post-wake settling lockout
static constexpr uint32_t TOUCH_DEBOUNCE_DISMISS   = 200;  // ms: Overlay/HUD dismissal
static constexpr uint32_t TOUCH_DEBOUNCE_SCROLL    = 250;  // ms: Page transitions & chevrons
static constexpr uint32_t TOUCH_GUARD_DISMISS      = 250;  // ms: Minimum post-wake delay
static constexpr uint32_t TOUCH_POST_SWIPE_LOCKOUT = 200;  // ms: Ignore moves right after swipe

inline bool touch_allow_tap(uint32_t now_ms, uint32_t cooldown_ms = TOUCH_DEBOUNCE_BUTTON) {
  static uint32_t s_last_tap_ms = 0;
  if (now_ms - s_last_tap_ms <= cooldown_ms) return false;
  s_last_tap_ms = now_ms;
  return true;
}

struct TouchPt {
  int x = 0;
  int y = 0;
};

struct TouchGestureState {
  int start_x = 0;
  int start_y = 0;
  int last_x = 0;
  int last_y = 0;
  int max_travel = 0;
  int sample_count = 0;
  uint32_t start_ms = 0;
  uint32_t last_touch_ms = 0;
  uint32_t last_swipe_ms = 0;
  int start_page = 0;
  int start_context = 0;
  bool active = false;
  bool swipe_committed = false;
};

inline TouchGestureState &touch_gesture() {
  static TouchGestureState st;
  return st;
}

struct TouchRouter {
  inline bool allow_tap(uint32_t now_ms, uint32_t cooldown_ms = TOUCH_DEBOUNCE_BUTTON) const {
    return touch_allow_tap(now_ms, cooldown_ms);
  }

  template <typename TouchT, typename TouchesT, typename HandlerFn>
  inline void dispatch_press(const TouchT &touch, const TouchesT & /*touches*/, HandlerFn &&handler) {
    handler(static_cast<int>(touch.x), static_cast<int>(touch.y));
  }

  template <typename TouchesT, typename HandlerFn>
  inline void dispatch_move(const TouchesT &touches, HandlerFn &&handler) {
    if (touches.empty()) return;
    handler(static_cast<int>(touches[0].x), static_cast<int>(touches[0].y));
  }

  template <typename HandlerFn>
  inline void dispatch_release(HandlerFn &&handler) {
    handler();
  }
};

inline TouchRouter &touch_router() {
  static TouchRouter s_router;
  return s_router;
}

// ------------------------------------------------------------------------------
// 2. Host Hardware / Entity Bridge Enums (implemented in modules/system_core.yaml)
// ------------------------------------------------------------------------------
enum class UiHostCmd : uint8_t {
  SCHEDULE_DISP_UPDATE = 0,
  SCHEDULE_BAND_DISP_UPDATE,
  WAKE_TO_ACTIVE,
  SCREEN_SLEEP_TIMER,
  APPLY_POWER_POLICY,
  CANCEL_VOICE_ASSISTANT,
  ABORT_VOICE_ASSISTANT,
  CYCLE_PRIMARY_WAKE_WORD,
  SYNC_WAKE_WORD_STATE,
  DISMISS_NOTIFICATION,
  STOP_ALARM_SOUND,
  SYNC_DISPLAY_BRIGHTNESS,
  FLASHLIGHT_FULL_BRIGHTNESS,
  PAUSE_LOCAL_MEDIA_PLAYER,
  STOP_MP_IDLE_DEBOUNCE,
  TOGGLE_HA_MUSIC,
  STOP_HA_MUSIC,
  CLEAR_HA_MUSIC_QUEUE,
  MUSIC_VOL_UP,
  MUSIC_VOL_DOWN,
  MUSIC_NEXT_TRACK,
  MUSIC_PREV_TRACK,
  MUSIC_SEEK_RELATIVE,
  MUSIC_SEEK_ABSOLUTE,
  FETCH_MUSIC_BROWSE,
  PLAY_MUSIC_SLOT,
  FETCH_REMOTE_ART_DEBOUNCED,
  SPEAKER_TRANSFER_TO_TARGET,
  SPEAKER_CONTROL_TARGET,
  SPEAKER_JOIN_TARGET,
  SPEAKER_UNJOIN_TARGET,
  REQUEST_DOCK_APPS,
  TRIGGER_SCREENSAVER_NOW,
  START_HIBERNATION_COUNTDOWN,
  TURN_SCREEN_OFF,
  RESTART_DEVICE,
  CHECK_FIRMWARE_UPDATE,
  TRIGGER_FIRMWARE_UPDATE,
  APPLY_THEME_PRESET,
  SAVE_CUSTOM_COLOR_TO_LATTER,
  MENU_SCROLL_TRANSITION,
  FETCH_AREA_BROWSE,
  FETCH_AREA_LIST_BROWSE,
  SELECT_ACTIVE_AREA_LATTER,
  EXECUTE_AREA_ACTION,
  EXECUTE_ALARM_ACTION,
  REMOTE_SEND_KEY,
  REMOTE_SELECT_SLOT_LATTER,
  FETCH_CALENDAR_BROWSE,
  CREATE_CALENDAR_QUICK_EVENT,
  FETCH_CALENDAR_MAP_SNAPSHOT,
  SEND_INTERCOM_BROADCAST,
  FETCH_ON_DEMAND_CAMERA,
  RELEASE_CAMERA_ART,
  START_DOORBELL_CALL,
  STOP_DOORBELL_CALL,
  STOP_DOORBELL_AUDIO,
  START_DOORBELL_PTT,
  RESUME_DOORBELL_LISTEN,
  SEND_DOORBELL_INTERCOM_CMD,
};

enum class UiSelectId : uint8_t {
  TAP_WAKE_MODE = 0,
  WEATHER_SIM_CONDITION,
  MUSIC_OVERLAY_STYLE,
  MUSIC_FULLSCREEN_ART_MODE,
  MUSIC_ALBUM_SORT_ORDER,
  MUSIC_ARTIST_ALBUM_SORT_ORDER,
  MUSIC_PODCAST_SORT_ORDER,
  VA_WAKE_WORD_MODE,
  VA_VISUAL_PROFILE,
  WW_SENS_OKAY_NABU,
  WW_SENS_HEY_LUNA,
  WW_SENS_HEY_JARVIS,
  WW_SENS_HEY_MYCROFT,
  WW_SENS_ALEXA,
  THEME_PALETTE,
  POWER_PROFILE,
  PICKUP_WAKE_MODE,
  DISPLAY_OFF_TIMEOUT,
  SCREENSAVER_STYLE,
  SCREENSAVER_DOCKED_TIMEOUT,
  HIBERNATE_TIMEOUT_MODE,
  DEFAULT_START_APP,
  CLOCK_FACE_STYLE,
  TIME_FORMAT,
  AUTO_BRIGHTNESS_MODE,
  DISPLAY_BRIGHTNESS_PRESET,
  DAY_BRIGHTNESS_PRESET,
  NIGHT_BRIGHTNESS_PRESET,
  AREA_DEFAULT_CATEGORY,
};

enum class UiSwitchId : uint8_t {
  HOME_WEATHER_BG = 0,
  HOME_WAKE_GREETING,
  MUSIC_SHOW_SPEAKER_SHORTCUT,
  MUSIC_SHOW_LIBRARY_SHORTCUT,
  BLE_AREA_BEACON,
  GAME_SOUND,
  VA_WAKE_SCREEN,
  DOCKED_SCREENSAVER,
  INSTANT_CROWN_HIBERNATE,
  AREA_SHOW_ALL_CATEGORY,
};

enum class UiNumberId : uint8_t {
  VOLUME_STEP_SIZE = 0,
  SPEAKER_VOLUME,
  ALERT_VOLUME,
  DAY_START_HOUR,
  NIGHT_START_HOUR,
};

struct UiTouchRuntime {
  using ExecCmdFn = void (*)(UiHostCmd cmd, int arg0, int arg1);
  using GetSelectFn = std::string (*)(UiSelectId id);
  using SetSelectFn = void (*)(UiSelectId id, const std::string &opt, bool publish_also);
  using GetSwitchFn = bool (*)(UiSwitchId id);
  using ToggleSwitchFn = void (*)(UiSwitchId id);
  using GetNumberFn = float (*)(UiNumberId id);
  using SetNumberFn = void (*)(UiNumberId id, float val);

  ExecCmdFn exec_cmd{nullptr};
  GetSelectFn get_select{nullptr};
  SetSelectFn set_select{nullptr};
  GetSwitchFn get_switch{nullptr};
  ToggleSwitchFn toggle_switch{nullptr};
  GetNumberFn get_number{nullptr};
  SetNumberFn set_number{nullptr};

  // Per-event live hardware/ESPHome snapshot
  const char *friendly_name{""};
  const char *fw_version{""};
  std::string fw_latest_version{};
  bool usb_connected{false};
  bool panel_is_on{true};
  bool local_mp_playing{false};
  float local_mp_volume{0.6f};
  int album_art_w{0};
  std::string track_title{};
  std::string track_artist{};
  std::string track_album{};
  bool time_valid{false};
  int time_hour{-1};
  int time_minute{-1};
  uint32_t time_epoch{0};

  esphome::Color *theme_primary{nullptr};
  esphome::Color *theme_secondary{nullptr};

  void cmd(UiHostCmd c, int a = 0, int b = 0) const {
    if (exec_cmd != nullptr) exec_cmd(c, a, b);
  }
  void schedule_disp() const { cmd(UiHostCmd::SCHEDULE_DISP_UPDATE); }
  void schedule_band_disp(int y0, int y1) const { cmd(UiHostCmd::SCHEDULE_BAND_DISP_UPDATE, y0, y1); }
  void apply_power_policy() const { cmd(UiHostCmd::APPLY_POWER_POLICY); }
  std::string select_opt(UiSelectId id) const {
    return (get_select != nullptr) ? get_select(id) : std::string{};
  }
  void set_select_opt(UiSelectId id, const std::string &opt, bool pub = false) const {
    if (set_select != nullptr) set_select(id, opt, pub);
  }
  bool sw_state(UiSwitchId id) const {
    return (get_switch != nullptr) ? get_switch(id) : false;
  }
  void sw_toggle(UiSwitchId id) const {
    if (toggle_switch != nullptr) toggle_switch(id);
  }
  float num_val(UiNumberId id) const {
    return (get_number != nullptr) ? get_number(id) : 0.0f;
  }
  void set_num_val(UiNumberId id, float v) const {
    if (set_number != nullptr) set_number(id, v);
  }
};

class TouchGestureEngine {
 public:
  UiTouchRuntime &runtime() { return rt_; }
  const UiTouchRuntime &runtime() const { return rt_; }

  void on_touch_down(int tx, int ty, uint32_t now_ms);
  void on_touch_move(int tx, int ty, uint32_t now_ms);
  void on_touch_up(uint32_t now_ms);
  void on_touch(int tx, int ty, uint32_t now_ms) { on_touch_down(tx, ty, now_ms); }
  void on_update(int tx, int ty, uint32_t now_ms) { on_touch_move(tx, ty, now_ms); }
  void on_release(uint32_t now_ms) { on_touch_up(now_ms); }
  void on_input_event(const std::string &action, int delta, uint32_t now_ms);

 private:
  UiTouchRuntime rt_{};
  uint32_t last_touch_activity_ms_{0};
  uint32_t last_drag_sleep_rst_ms_{0};
  uint32_t last_rapid_chev_ms_{0};
  uint32_t hud_just_woken_ms_{0};
  uint32_t last_area_slider_ms_{0};

  void navigate_home(uint32_t now_ms);
  bool handle_pin_pad_tap(int tx, int ty, uint32_t now_ms);
  bool gate_or_dispatch_area_action(const std::string &domain_or_entity,
                                    const std::string &action, int level,
                                    const char *pin_prompt, uint32_t now_ms);
  bool gate_or_dispatch_alarm_action(const std::string &action, uint32_t now_ms);
  bool gate_or_dispatch_doorbell_unlock(uint32_t now_ms, bool from_intercom);
};

TouchGestureEngine &touch_engine();

}  // namespace latter_ui
