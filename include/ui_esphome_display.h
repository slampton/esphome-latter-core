#pragma once

// ==============================================================================
// Latter ESPHome Display & Cadence Scheduler Bridge (include/ui_esphome_display.h)
// ESPHome Display Bridge — SOLE UI header permitted to include
// esphome/components/display/display.h and DispBufferAccessor.
// ==============================================================================

#if __has_include("esphome/components/display/display.h")

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/display/display.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) && __has_include("esphome/components/mipi_dsi/mipi_dsi.h")
#include "esphome/components/mipi_dsi/mipi_dsi.h"
#endif

#ifdef USE_API
#if __has_include("esphome/components/api/api_server.h")
#include "esphome/components/api/api_server.h"
#endif
#endif

#include "esphome_bridge.h"
#include "hal_display_lifecycle.h"
#include "hal_link_report.h"
#include "hal_panel_power.h"
#include "ui_compositor.h"
#include "ui_pages.h"
#include "ui_touch_engine.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace latter_ui {

inline bool disp_render_in_flight() { return disp_render_in_flight(esphome::millis()); }

template <typename DispT, bool IsFinal = std::is_final_v<DispT>>
struct DispBufferAccessor {
  static uint8_t *get(DispT *) { return nullptr; }
};

template <typename DispT>
struct DispBufferAccessor<DispT, false> : DispT {
  static uint8_t *get(DispT *d) {
    if constexpr (requires { &DispBufferAccessor::buffer_; }) {
      return reinterpret_cast<uint8_t *>(d->*(&DispBufferAccessor::buffer_));
    } else {
      return nullptr;
    }
  }
};

#if defined(USE_ESP32_VARIANT_ESP32P4) && __has_include("esphome/components/mipi_dsi/mipi_dsi.h")
template <>
struct DispBufferAccessor<esphome::mipi_dsi::MipiDsi, true> {
  static uint8_t *get(esphome::mipi_dsi::MipiDsi *d) {
    return ::latter_hal::get_mipi_dsi_buffer(d);
  }
};
#endif

inline void probe_frame_buffer_endian(esphome::display::Display &it, uint16_t *buf16) {
  if (buf16 == nullptr) {
    active_frame_endian_mode() = 0;
    return;
  }
  const uint16_t orig_px0 = buf16[0];
  it.draw_pixel_at(0, 0, esphome::Color(255, 0, 0));
  const uint16_t red_probe = buf16[0];
  buf16[0] = orig_px0;
  if (red_probe == 0xF800U) {
    active_frame_endian_mode() = 1;
  } else if (red_probe == 0x00F8U) {
    active_frame_endian_mode() = 2;
  } else if (red_probe == 0x001FU) {
    active_frame_endian_mode() = 3;
  } else if (red_probe == 0x1F00U) {
    active_frame_endian_mode() = 4;
  } else {
    active_frame_endian_mode() = 0;
  }
  ESP_LOGI("disp_fb", "Bound direct RGB565 framebuffer: ptr=%p endian_mode=%d",
           static_cast<void *>(buf16), active_frame_endian_mode());
}

struct DisplayUpdateScheduler {
  static constexpr uint32_t kTimeoutAsyncDraw = kTimeoutAsyncDrawId;
  static constexpr uint32_t kTimeoutFbClear = kTimeoutFbClearId;
  static constexpr uint32_t kTimeoutWeatherStep = kTimeoutWeatherStepId;
  static constexpr uint32_t kTimeoutActivityRotate = kTimeoutActivityRotateId;

  static inline uint32_t &weather_due_ms_ref() {
    return weather_step_due_ms_ref();
  }

  static inline bool weather_step_pending(uint32_t now_ms) {
    return is_weather_step_pending(now_ms);
  }

  static void request_post_frame(const esphome::Component *comp, void (*fn)()) {
    if (comp == nullptr || fn == nullptr) return;
    schedule_core_timeout(kTimeoutFbClear, 0U, [fn]() { fn(); });
  }

  static void request_post_frame_raw(void *comp, void (*fn)()) {
    request_post_frame(static_cast<const esphome::Component *>(comp), fn);
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
inline Rgb565Surface bind_frame_buffer(DispT *disp, esphome::display::Display &it) {
  set_fb_post_frame_hook(&DisplayUpdateScheduler::request_post_frame_raw);
  fb_clear_bind_raw(nullptr, nullptr);
  if (disp == nullptr || it.get_rotation() != esphome::display::DISPLAY_ROTATION_0_DEGREES) {
    active_frame_buf16() = nullptr;
    return Rgb565Surface(it);
  }
  uint8_t *raw = DispBufferAccessor<DispT>::get(disp);
  if (raw == nullptr) {
    active_frame_buf16() = nullptr;
    return Rgb565Surface(it);
  }
  uint16_t *buf16 = reinterpret_cast<uint16_t *>(raw);
  if (active_frame_endian_mode() < 0) {
    probe_frame_buffer_endian(it, buf16);
  }
  active_frame_buf16() = (active_frame_endian_mode() > 0) ? buf16 : nullptr;
  fb_clear_bind_raw(static_cast<void *>(static_cast<esphome::Component *>(disp)), active_frame_buf16());
  return Rgb565Surface(it);
}

template <typename DispT>
inline Rgb565Surface bind_frame_buffer(DispT *const *disp_ptr, esphome::display::Display &it) {
  if (disp_ptr != nullptr) return bind_frame_buffer(*disp_ptr, it);
  return Rgb565Surface(it);
}

inline void sync_ctx_link_health(UiFrameContext &ctx) {
  auto &ls = link_state();
#ifdef USE_API
  if (esphome::api::global_api_server != nullptr &&
      esphome::api::global_api_server->is_connected_with_state_subscription()) {
    if (!ls.ha_connected) {
      ls.ha_connected = true;
      ls.last_ha_connect_ms = ctx.now_ms;
    }
    ctx.ha_connected = true;
    ctx.ha_disconnect_ms = 0U;
    return;
  }
#endif
  if (link_report().ha_clients > 0) {
    ctx.ha_connected = true;
    ctx.ha_disconnect_ms = 0U;
    return;
  }
  ctx.ha_connected = ls.ha_connected;
  ctx.ha_disconnect_ms = ls.last_ha_disconnect_ms;
}

// Thin Display& convenience overloads forwarding to Rgb565Surface
inline bool render_system_takeovers(esphome::display::Display &it, const UiFrameContext &ctx,
                                    const SystemTakeoverState &st) {
  Rgb565Surface surf(it);
  return render_system_takeovers(surf, ctx, st);
}

inline void render_active_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  Rgb565Surface surf(it);
  render_active_page(surf, ctx);
}

inline void render_active_focus_overlay(esphome::display::Display &it, uint32_t now_ms,
                                        UiColor theme_primary, UiColor col_white) {
  Rgb565Surface surf(it);
  render_active_focus_overlay(surf, now_ms, theme_primary, col_white);
}

inline void render_home_indicator_bar(esphome::display::Display &it, int cx, int y_pos = kHomeBarY,
                                      UiColor bar_col = kChromeHomePill) {
  Rgb565Surface surf(it);
  render_home_indicator_bar(surf, cx, y_pos, bar_col);
}

inline void render_offline_pill(esphome::display::Display &it, const UiFrameContext &ctx) {
  Rgb565Surface surf(it);
  render_offline_pill(surf, ctx);
}

inline void render_offline_pill(esphome::display::Display &it, int cx, uint32_t now_ms,
                                UiFontView font, UiColor text_c) {
  UiFrameContext tmp{};
  tmp.now_ms = now_ms;
  sync_ctx_link_health(tmp);
  Rgb565Surface surf(it);
  render_offline_pill(surf, cx, now_ms, font, text_c, tmp.ha_connected, tmp.ha_disconnect_ms);
}

inline void render_static_day_night_sky_base(esphome::display::Display &it, int cx, int cy, bool is_night) {
  Rgb565Surface surf(it);
  render_static_day_night_sky_base(surf, cx, cy, is_night);
}

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

}  // namespace latter_ui

#endif  // __has_include("esphome/components/display/display.h")
