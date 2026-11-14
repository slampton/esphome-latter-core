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
#include "hal_power_pm.h"
#include "latter_state.h"
#include "ui_types.h"
#include "ui_gfx.h"
#include "ui_hit_table.h"
#include "ui_layout.h"
#include "ui_widgets.h"
#else
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
};

// Single authoritative backdrop precedence shared across gui.yaml, timers_and_fetchers.yaml,
// page_clock_home.cpp, and page_weather.cpp (D-038):
// - Home root (gui_page == 1 && home_subpage == 0) and Sky & Weather (gui_page == 5):
//     If weather_background is ON (persisted pref OR active weather simulation): LiveWeatherSky
//     (even when a System Wallpaper is loaded — System Wallpaper still renders dimmed on Home
//     subpage 2 and on other wallpaper-backed views, and renders on Home/Weather when
//     weather_background is OFF).
//     If weather_background is OFF: CustomWallpaper when loaded, else SolidBlack.
// - Home overlays (subpage 1 or 2), System Settings (4), Area (6), Remote (7),
//   Tools non-full-bleed (8), Whole-Home (9), and Music with Art Mode "None" (2):
//     CustomWallpaper when loaded, else SolidBlack.
// - Clock (0), Games (3), Music with Album Art enabled (2), and Tools full-bleed (8): SolidBlack.
constexpr BackdropMode resolve_backdrop_mode(int gui_page, int home_subpage,
                                             bool weather_background_pref,
                                             bool weather_sim_active,
                                             bool has_custom_wallpaper,
                                             bool tools_full_bleed = false,
                                             bool music_art_none = false) noexcept {
  if ((gui_page == 1 && home_subpage == 0) || gui_page == 5) {
    if (weather_background_pref || weather_sim_active) {
      return BackdropMode::LiveWeatherSky;
    }
    return has_custom_wallpaper ? BackdropMode::CustomWallpaper : BackdropMode::SolidBlack;
  }
  if (gui_page == 1) {
    return has_custom_wallpaper ? BackdropMode::CustomWallpaper : BackdropMode::SolidBlack;
  }
  if (gui_page == 2) {
    return (music_art_none && has_custom_wallpaper) ? BackdropMode::CustomWallpaper : BackdropMode::SolidBlack;
  }
  if (gui_page == 4 || gui_page == 6 || gui_page == 7 ||
      (gui_page == 8 && !tools_full_bleed) || gui_page == 9) {
    return has_custom_wallpaper ? BackdropMode::CustomWallpaper : BackdropMode::SolidBlack;
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

// Folds all secondary subpage dimensions (home_subpage + home_dock_page,
// tools_subpage + tools_menu_page + cal_detail_mode + timer_view_slot,
// system_menu_mode + system_dashboard_page + sleep_menu_page + display_menu_page,
// whole_home_tab) into one unambiguous sub_id for DirtyBandTracker::begin_frame.
constexpr int compute_subpage_id(int gui_page, int home_subpage, int home_dock_pg,
                                 int music_mode, int active_game,
                                 int sys_mode, int sys_dash_pg, int sleep_pg, int disp_pg,
                                 int weather_sub, int area_sub, int rem_sub,
                                 int tools_sub, int tools_menu_pg, int cal_detail, int timer_slot,
                                 int wh_tab) {
  switch (gui_page) {
    case 1:
      return (home_subpage & 0xFF) | ((home_dock_pg & 0xFF) << 8);
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
      return wh_tab;
    default:
      return 0;
  }
}

uint32_t compute_takeover_signature(bool ota_in_progress, bool is_hibernating, bool panel_is_on,
                                    int display_power_state, bool alert_ringing, bool va_active,
                                    bool pin_pad_open, bool notif_active);

#if __has_include("esphome/core/defines.h")

// ------------------------------------------------------------------------------
// Display Update Scheduler (ESPHome 2026.11+ numeric timeout IDs)
// ------------------------------------------------------------------------------
struct DisplayUpdateScheduler : public esphome::Component {
  static constexpr uint32_t kTimeoutAsyncDraw = 0x4C410001U;
  static constexpr uint32_t kTimeoutFbClear = 0x4C410002U;
  static constexpr uint32_t kTimeoutWeatherStep = 0x4C410003U;
  static constexpr uint32_t kTimeoutActivityRotate = 0x4C410004U;

  static inline uint32_t &weather_due_ms_ref() {
    static uint32_t due_ms = 0U;
    return due_ms;
  }

  static inline bool weather_step_pending(uint32_t now_ms) {
    const uint32_t due = weather_due_ms_ref();
    return due != 0U && static_cast<int32_t>(due + 24U - now_ms) > 0;
  }

  // Post-frame hook: a 0 ms timeout runs after the current update() (and its panel transfer) returns.
  static void request_post_frame(esphome::Component *comp, void (*fn)()) {
    if (comp == nullptr || fn == nullptr) return;
    static_cast<DisplayUpdateScheduler *>(comp)->set_timeout(kTimeoutFbClear, 0, [fn]() { fn(); });
  }

  template <typename DispT>
  static void request_ptr(DispT *disp) {
    if (disp != nullptr && !screen_is_off_standby() && !disp_poller_held()) {
      const uint32_t delay_ms = disp_render_in_flight(esphome::millis()) ? 4U : 0U;
      auto *comp = static_cast<esphome::Component *>(disp);
      static_cast<DisplayUpdateScheduler *>(comp)->set_timeout(
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
      auto *comp = static_cast<esphome::Component *>(disp);
      static_cast<DisplayUpdateScheduler *>(comp)->set_timeout(
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
      auto *comp = static_cast<esphome::Component *>(disp);
      static_cast<DisplayUpdateScheduler *>(comp)->set_timeout(
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
      auto *comp = static_cast<esphome::Component *>(disp);
      static_cast<DisplayUpdateScheduler *>(comp)->set_timeout(
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
  UiStrView power_profile_opt{"Balanced"};
  UiStrView tap_wake_opt{"Always On"};
  UiStrView pickup_wake_opt{"Medium"};
  UiStrView display_off_timeout_opt{"30 Seconds"};
  UiStrView screensaver_style_opt{"Ambient Clock"};
  UiStrView screensaver_docked_timeout_opt{"5 Minutes"};
  UiStrView hibernate_timeout_opt{"30 Minutes"};
  UiStrView default_start_app_opt{"Home"};
  UiStrView auto_brightness_opt{"Time Schedule"};
  UiStrView display_brightness_opt{"High (80%)"};
  UiStrView day_brightness_opt{"High (80%)"};
  UiStrView night_brightness_opt{"Low (25%)"};
  UiStrView va_wake_word_mode_opt{"Always On"};
  UiStrView va_visual_profile_opt{"Nebula"};
  UiStrView ww_sens_okay_nabu_opt{"Medium"};
  UiStrView ww_sens_hey_luna_opt{"Off"};
  UiStrView ww_sens_hey_jarvis_opt{"Off"};
  UiStrView ww_sens_hey_mycroft_opt{"Off"};
  UiStrView ww_sens_alexa_opt{"Off"};

  // Numbers
  int day_start_hr{7};
  int night_start_hr{21};

  // Switches
  bool home_weather_bg{true};
  bool home_wake_greeting{true};
  bool home_sun_arc{true};
  bool home_moon_phase{true};
  bool music_show_speaker_shortcut{true};
  bool music_show_library_shortcut{true};
  bool game_sound_enabled{true};
  bool va_wake_screen{true};
  bool docked_screensaver_enabled{true};
  bool instant_crown_hibernate{false};
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
  UiStrView va_visual_profile_opt{};
  bool notif_active{false};
  UiStrView notif_title{};
  UiStrView notif_message{};
  UiStrView notif_priority{};
  UiStrView notif_camera_url{};
  UiStrView current_loaded_camera_url{};
  UiStrView screensaver_style_opt{};
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
    st.va_visual_profile_opt = ctx.va_visual_profile_opt;
    st.notif_active = rs.alert.active;
    st.notif_title = rs.alert.title;
    st.notif_message = rs.alert.message;
    st.notif_priority = rs.alert.priority;
    st.notif_camera_url = rs.alert.camera_url;
    st.current_loaded_camera_url = rs.alert.loaded_camera_url;
    st.screensaver_style_opt = ctx.screensaver_style_opt;
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

inline uint32_t compute_takeover_signature(const SystemTakeoverState &st) {
  return compute_takeover_signature(
      st.ota_in_progress, st.is_hibernating, st.panel_is_on,
      st.display_power_state,
      st.alarm_ringing || st.timer_ringing || st.find_my_active,
      st.va_active || !st.va_status_text.empty(),
      ui::pin_pad_state().open, st.notif_active);
}

inline int compute_active_subpage_id() {
  const auto &rs = runtime_state();
  return compute_subpage_id(
      rs.nav.gui_page, rs.nav.home_subpage, home_dock_page(),
      rs.music.overlay_mode, rs.nav.active_game,
      rs.nav.system_menu_mode, rs.nav.system_dashboard_page,
      rs.nav.sleep_menu_page, rs.nav.display_menu_page,
      rs.nav.sky_weather_subpage, rs.nav.area_subpage, rs.nav.remote_subpage,
      rs.nav.tools_subpage, rs.nav.tools_menu_page,
      cal_detail_mode(), timer_view_slot(), whole_home_tab());
}

inline int compute_hhmm_key(const esphome::ESPTime &now) {
  return now.is_valid()
      ? (static_cast<int>(now.day_of_year) * 1440 + static_cast<int>(now.hour) * 60 + static_cast<int>(now.minute))
      : -1;
}

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

#endif  // __has_include("esphome/core/defines.h")

}  // namespace latter_ui
