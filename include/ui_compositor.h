#pragma once

// ==============================================================================
// Latter UI Compositor & 2px-Aligned Horizontal Band Dirty Flush (include/ui_compositor.h)
// 1. Tracks full-width 2px-aligned horizontal dirty bands [band_y0, band_y1] so
//    MipiSpi keeps its contiguous stride == SCREEN_W DMA fast path while cutting
//    SPI/QSPI pixel bus traffic 5-12x on localized updates (clock ticks, media
//    progress, marquee, slider drags).
// 2. Owns all system-level display takeovers in strict priority order:
//    OTA -> Hibernation -> Panel Off -> Alarm/Timer/Find-My -> Voice Assistant ->
//    Security PIN Pad Modal -> Push Notification -> Docked Screensaver.
// ==============================================================================

#if __has_include("esphome.h")
#include "ui_ambient_va.h"
#include "ui_core.h"
#include "ui_hit_table.h"
#include "ui_tools.h"
#include "ui_widgets.h"
#else
#include <cstdint>
#include "ui_hit_table.h"
#ifndef LATTER_SCREEN_H
#define LATTER_SCREEN_H 466
#endif
namespace latter_ui {
static constexpr int SCREEN_H = LATTER_SCREEN_H;
}  // namespace latter_ui
#endif

namespace latter_ui {

struct DirtyBandTracker {
  int pending_y0{0};
  int pending_y1{SCREEN_H - 1};
  bool has_pending_band{false};
  bool force_full{true};

  int active_y0{0};
  int active_y1{SCREEN_H - 1};
  bool active_full{true};

  int last_page{-1};
  int last_subpage{-1};
  uint32_t last_overlay_sig{0xFFFFFFFFU};

  // Aligns any vertical span to 2px-aligned full-width rows:
  //   y0 -> even row (y0 & ~1), y1 -> odd row (min(SCREEN_H - 1, y1 | 1))
  static void align_band(int raw_y0, int raw_y1, int screen_h, int &out_y0, int &out_y1);

  void mark_band(int y0, int y1);
  void mark_full();
  void begin_frame(int page, int subpage, uint32_t overlay_sig, bool anim_or_scroll_active = false);

  constexpr int y0() const { return active_y0; }
  constexpr int y1() const { return active_y1; }
  constexpr bool is_full() const { return active_full; }
};

DirtyBandTracker &dirty_band();
void mark_dirty_band(int y0, int y1);
void mark_dirty_full();

uint32_t compute_takeover_signature(bool ota_in_progress, bool is_hibernating, bool panel_is_on,
                                    int display_power_state, bool alert_ringing, bool va_active,
                                    bool pin_pad_open, bool notif_active);

#if __has_include("esphome.h")

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
  std::string va_timer_name{};
  bool va_active{false};
  std::string va_status_text{};
  bool va_tts_received{false};
  bool va_vad_active{false};
  std::string va_visual_profile_opt{};
  bool notif_active{false};
  std::string notif_title{};
  std::string notif_message{};
  std::string notif_priority{};
  std::string notif_camera_url{};
  std::string current_loaded_camera_url{};
  std::string screensaver_style_opt{};
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
};

inline uint32_t compute_takeover_signature(const SystemTakeoverState &st) {
  return compute_takeover_signature(
      st.ota_in_progress, st.is_hibernating, st.panel_is_on,
      st.display_power_state,
      st.alarm_ringing || st.timer_ringing || st.find_my_active,
      st.va_active || !st.va_status_text.empty(),
      ui::pin_pad_state().open, st.notif_active);
}

// Renders all system-owned global takeovers in strict priority order.
// Returns true when a takeover owned the frame (caller returns immediately).
template <typename DisplayT, typename TimeT, typename CamImgT,
          typename FontTimeT, typename FontChronoT, typename FontDateT,
          typename FontSmallT, typename FontMicroT>
inline bool render_system_takeovers(
    DisplayT &it, int cx, int cy, uint32_t now_ms, const TimeT &now, bool is_24h_format,
    bool ota_in_progress, int ota_progress_pct,
    bool is_hibernating, int hibernate_countdown_val,
    bool panel_is_on, int display_power_state,
    bool alarm_ringing, int alarm_ringing_idx,
    int alarm1_recurrence, int alarm2_recurrence, int alarm3_recurrence,
    bool timer_ringing, bool find_my_active, const std::string &va_timer_name,
    bool va_active, const std::string &va_status_text, bool va_tts_received, bool va_vad_active,
    const std::string &va_visual_profile_opt,
    bool notif_active, const std::string &notif_title, const std::string &notif_message,
    const std::string &notif_priority, CamImgT *camera_snapshot_art,
    const std::string &notif_camera_url, const std::string &current_loaded_camera_url,
    const std::string &screensaver_style_opt,
    FontTimeT *font_time, FontChronoT *font_chrono_time, FontDateT *font_date,
    FontSmallT *font_small, FontMicroT *font_micro,
    esphome::Color theme_primary, esphome::Color theme_secondary,
    esphome::Color col_bg, esphome::Color col_black, esphome::Color col_white,
    esphome::Color col_slate, esphome::Color col_gray, esphome::Color col_dark_gray,
    esphome::Color col_gold, esphome::Color col_red, esphome::Color col_solaris_gold) {
  using esphome::display::TextAlign;

  // 1. OTA Firmware Update In Progress
  if (ota_in_progress) {
    frame_base(it, col_bg);
    int pct = ota_progress_pct;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (LATTER_ROUND_DISPLAY) {
      for (int i = 0; i <= pct; i++) {
        const float rad = (static_cast<float>(i) / 100.0f) * 2.0f * PI_F - HALF_PI_F;
        float s = 0.0f, c = 1.0f;
        fast_sincos(rad, s, c);
        fast_line(it, cx + static_cast<int>(220.0f * c), cy + static_cast<int>(220.0f * s),
                  cx + static_cast<int>(228.0f * c), cy + static_cast<int>(228.0f * s), theme_secondary);
      }
    } else {
      const int ota_bw = std::min(scale_w(280), SCREEN_W - 48);
      const int ota_bx = cx - (ota_bw / 2);
      const int ota_fill = (ota_bw * pct) / 100;
      fill_rect_row_major(it, ota_bx, cy + scale_y(104), ota_bw, scale_y(10), col_dark_gray);
      if (ota_fill > 0) {
        fill_rect_row_major(it, ota_bx, cy + scale_y(104), ota_fill, scale_y(10), theme_primary);
      }
    }
    fast_print(it, cx, cy - scale_y(60), font_date, theme_primary, TextAlign::CENTER, "FIRMWARE UPDATE");
    char prog_buf[32];
    snprintf(prog_buf, sizeof(prog_buf), "%d%%", pct);
    fast_print(it, cx, cy + scale_y(8), font_time, col_white, TextAlign::CENTER, prog_buf);
    const char *status_msg = "Preparing flash...";
    if (pct >= 99) status_msg = "Rebooting device...";
    else if (pct >= 85) status_msg = "Verifying image...";
    else if (pct >= 10) status_msg = "Writing flash memory...";
    fast_print(it, cx, cy + scale_y(65), font_small, col_slate, TextAlign::CENTER, status_msg);
    return true;
  }

  // 2. Hibernation Shutdown Countdown
  if (is_hibernating) {
    frame_base(it, col_bg);
    if (hibernate_countdown_val > 0) {
      fast_print(it, cx, cy - scale_y(65), font_date, theme_primary, TextAlign::CENTER, "HIBERNATING");
      char c_buf[16];
      snprintf(c_buf, sizeof(c_buf), "%d", hibernate_countdown_val);
      fast_print(it, cx, cy + scale_y(5), font_chrono_time, theme_secondary, TextAlign::CENTER, c_buf);
      fast_print(it, cx, cy + scale_y(65), font_small, col_slate, TextAlign::CENTER, "Tap screen to stay awake");
    } else {
      fast_print(it, cx, cy - scale_y(10), font_date, theme_primary, TextAlign::CENTER, "HIBERNATING");
      fast_print(it, cx, cy + scale_y(30), font_small, col_slate, TextAlign::CENTER, "Shutting down...");
    }
    return true;
  }

  // 3. Panel Off / Standby: skip buffer writes until wake frame
  if (!panel_is_on || display_power_state == 2) {
    frame_base_skip();
    return true;
  }

  // 4. ALARM / TIMER RINGING TAKEOVER (Highest Active Priority — always above Voice Assistant!)
  if (alarm_ringing || timer_ringing || find_my_active) {
    frame_base(it, col_bg);
    render_alert_ringing_overlay(
        it, cx, cy, now_ms, alarm_ringing, alarm_ringing_idx,
        alarm1_recurrence, alarm2_recurrence, alarm3_recurrence,
        now, is_24h_format,
        font_time, font_date, font_small, font_micro,
        theme_primary, theme_secondary, col_bg, col_black,
        col_white, col_gray, col_red,
        find_my_active, va_timer_name);
    return true;
  }

  // 5. VOICE ASSISTANT TAKEOVER
  if (va_active || !va_status_text.empty()) {
    frame_base(it, col_bg);
    const bool is_speaking = va_tts_received;
    const bool is_thinking = (!is_speaking && va_status_text == "Thinking...");
    render_voice_assistant_overlay(
        it, cx, cy, now_ms, is_speaking, is_thinking, va_vad_active,
        va_visual_profile_opt, col_white, col_solaris_gold);
    return true;
  }

  // 6. 3-TIER SECURITY PIN PAD MODAL
  if (ui::pin_pad_state().open) {
    frame_base(it, col_bg);
    ui::pin_pad_modal(it, cx, cy, now_ms, font_date, font_date, font_small,
                      theme_primary, col_white, col_slate, col_red);
    render_active_focus_overlay(it, 0, 0, now_ms, theme_primary, col_white);
    return true;
  }

  // 7. PUSH NOTIFICATION & CAMERA SNAPSHOT TAKEOVER
  if (notif_active) {
    frame_base(it, col_bg);
    render_notification_overlay(
        it, cx, cy, now_ms,
        notif_title, notif_message, notif_priority,
        camera_snapshot_art, notif_camera_url, current_loaded_camera_url,
        font_date, font_small,
        theme_primary, theme_secondary,
        col_bg, col_black, col_white,
        col_slate, col_gold, col_red);
    return true;
  }

  // 8. DOCKED SCREENSAVER (Mode 1)
  if (display_power_state == 1) {
    frame_base(it, col_bg);
    render_docked_screensaver(
        it, cx, cy, now_ms, now, is_24h_format,
        screensaver_style_opt,
        font_time, font_date, col_slate, col_dark_gray);
    return true;
  }

  return false;
}

template <typename DisplayT, typename TimeT, typename CamImgT,
          typename FontTimeT, typename FontChronoT, typename FontDateT,
          typename FontSmallT, typename FontMicroT>
inline bool render_system_takeovers(
    DisplayT &it, const TimeT &now, const SystemTakeoverState &st,
    CamImgT *camera_snapshot_art,
    FontTimeT *font_time, FontChronoT *font_chrono_time, FontDateT *font_date,
    FontSmallT *font_small, FontMicroT *font_micro) {
  return render_system_takeovers(
      it, st.cx, st.cy, st.now_ms, now, st.is_24h_format,
      st.ota_in_progress, st.ota_progress_pct,
      st.is_hibernating, st.hibernate_countdown_val,
      st.panel_is_on, st.display_power_state,
      st.alarm_ringing, st.alarm_ringing_idx,
      st.alarm1_recurrence, st.alarm2_recurrence, st.alarm3_recurrence,
      st.timer_ringing, st.find_my_active, st.va_timer_name,
      st.va_active, st.va_status_text, st.va_tts_received, st.va_vad_active,
      st.va_visual_profile_opt,
      st.notif_active, st.notif_title, st.notif_message, st.notif_priority,
      camera_snapshot_art, st.notif_camera_url, st.current_loaded_camera_url,
      st.screensaver_style_opt,
      font_time, font_chrono_time, font_date, font_small, font_micro,
      st.theme_primary, st.theme_secondary,
      st.col_bg, st.col_black, st.col_white,
      st.col_slate, st.col_gray, st.col_dark_gray,
      st.col_gold, st.col_red, st.col_solaris_gold);
}

#endif  // __has_include("esphome.h")

}  // namespace latter_ui
