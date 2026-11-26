#pragma once

// ==============================================================================
// Latter UI Compositor, Takeover Precedence & Focus/Takeover Overlay Dispatch
// (include/ui_compositor.h)
// Pure C++20 UI umbrella header — zero ESPHome or HAL includes.
// ==============================================================================

#include "ui_cadence.h"
#include "ui_dirty_band.h"
#include "ui_frame_context.h"
#include "ui_widgets.h"

#include <cstdint>

namespace latter_ui {

// Pure-C++20 VA takeover precedence check (D-066): during doorbell PTT talk
// (`intercom_turn == true`), keep live camera video visible instead of hiding
// it behind the full-screen Voice Assistant orb!
inline constexpr bool should_va_takeover_preempt(bool va_active, bool has_status_text,
                                                 bool intercom_turn, int gui_page,
                                                 bool live_popup_active) noexcept {
  if (!va_active && !has_status_text) return false;
  if (intercom_turn && (gui_page == latter_core::PageId::CAMERAS ||
                        gui_page == latter_core::PageId::HOME_HUB ||
                        live_popup_active)) {
    return false;
  }
  return true;
}

uint32_t compute_takeover_signature(bool ota_in_progress, bool is_hibernating, bool panel_is_on,
                                    int display_power_state, bool alert_ringing, bool va_active,
                                    bool pin_pad_open, bool notif_active,
                                    bool shortcut_confirm_open = false,
                                    bool quick_actions_open = false);

inline uint32_t compute_takeover_signature(
    const SystemTakeoverState &st,
    const latter_core::DeviceRuntimeState &rs = latter_core::runtime_state()) {
  const bool va_preempt = should_va_takeover_preempt(
      st.va_active, !st.va_status_text.empty(), st.va_intercom_turn,
      rs.nav.gui_page, rs.cameras.doorbell_live_popup_active);
  return compute_takeover_signature(
      st.ota_in_progress, st.is_hibernating, st.panel_is_on,
      st.display_power_state,
      st.alarm_ringing || st.timer_ringing || st.find_my_active,
      va_preempt,
      ui::pin_pad_state().open, st.notif_active,
      rs.nav.shortcut_confirm.open,
      st.quick_actions_open);
}

inline int compute_active_subpage_id(
    const latter_core::DeviceRuntimeState &rs = latter_core::runtime_state()) {
  const int hub_drill = (rs.intercom.broadcast_dictation_active ? 4 : 0) |
                        ((latter_core::home_hub_tab() == latter_core::HomeHubTab::INTERCOM
                              ? latter_core::home_hub_state().broadcast_page
                              : latter_core::home_hub_state().entry_page) << 3);
  int cal_vert_pg = rs.cal.overview_page;
  if (rs.cal.drilldown >= latter_core::CalDrilldown::PICK_TITLE) {
    cal_vert_pg = rs.cal.picker_page;
  } else if (rs.cal.subpage == latter_core::CalendarSubpage::DAY) {
    cal_vert_pg = rs.cal.day_page;
  } else if (rs.cal.subpage == latter_core::CalendarSubpage::WEEK) {
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
      ((rs.music.mode_bar_win_start & 0x07) << 10);
  const int cam_sub_id =
      (rs.cameras.tab & 0x0F) |
      ((rs.cameras.grid_page & 0x0F) << 4) |
      (rs.cameras.hud_active ? (1 << 8) : 0) |
      (rs.cameras.quick_reply_open ? (1 << 9) : 0) |
      (rs.cameras.landscape_mode ? (1 << 10) : 0);
  return compute_subpage_id(
      rs.nav.gui_page, rs.nav.home_subpage, latter_core::home_dock_page(),
      music_sub_id, rs.nav.active_game,
      rs.nav.system_menu_mode, rs.nav.system_dashboard_page,
      rs.nav.sleep_menu_page, rs.nav.display_menu_page,
      rs.nav.sky_weather_subpage, rs.nav.area_subpage, rs.nav.remote_subpage,
      rs.nav.tools_subpage, rs.nav.tools_menu_page,
      latter_core::timer_view_slot(), latter_core::home_hub_tab(),
      rs.nav.time_subpage, rs.nav.world_clock_page,
      rs.alarms.edit_idx, rs.nav.home_settings_page,
      latter_core::home_hub_mode_bar_page(), hub_drill, rs.nav.pinned_menu_page,
      cal_sub_id, cam_sub_id);
}

bool is_music_lyrics_anim_active(uint32_t now_ms, bool local_media_playing = false);
uint32_t compute_idle_state_signature(const UiFrameContext &ctx);
bool compute_live_sec_active(const UiFrameContext &ctx);

// Draws rotary/touchless focus ring from FrameHitTable::focus_at(ui_focus_index()).
void render_active_focus_overlay(Rgb565Surface &it, uint32_t now_ms,
                                 UiColor theme_primary, UiColor col_white);

// System takeover overlay renderers (implemented in overlay_takeover.cpp)
void render_alert_ringing_overlay(Rgb565Surface &it, const UiFrameContext &ctx,
                                  const SystemTakeoverState &st);
void render_voice_assistant_overlay(Rgb565Surface &it, const SystemTakeoverState &st);
void render_notification_overlay(Rgb565Surface &it, const UiFrameContext &ctx,
                                 const SystemTakeoverState &st);
void render_quick_actions_overlay(Rgb565Surface &it, const UiFrameContext &ctx,
                                  const SystemTakeoverState &st);
void render_docked_screensaver(Rgb565Surface &it, const UiFrameContext &ctx,
                               const SystemTakeoverState &st);

// Renders all system-owned global takeovers in strict priority order:
//   OTA -> Hibernation -> Panel Off -> Alarm/Timer/Find-My -> Voice Assistant ->
//   PIN Pad Modal -> Push Notification -> Shortcut Confirm -> Quick Actions -> Docked Screensaver.
bool render_system_takeovers(Rgb565Surface &it, const UiFrameContext &ctx,
                             const SystemTakeoverState &st);

// Compiled per-frame pipeline (implemented in ui_compositor.cpp)
struct FrameRenderPlan {
  BackdropMode bd_mode{BackdropMode::SolidBlack};
  bool cam_live_canvas{false};
  bool weather_sky_active{false};
  bool weather_sky_stepping{false};
  bool w_has_fast_motion{false};
  bool on_home_main{false};
  bool dock_anim_now{false};
  bool page_horiz_bounce_now{false};
  bool vert_bounce_now{false};
  bool lyrics_anim_now{false};
  bool greet_fading_now{false};
  bool has_act_band{false};
  int act_y0{0};
  int act_y1{0};
  bool va_visible{false};
  bool alert_ringing{false};
  uint32_t takeover_sig{0};
  SystemTakeoverState takeover_st{};
  int band_y0{0};
  int band_y1{0};
};

struct PostFrameSchedule {
  bool anim_full{false};
  bool anim_band{false};
  int anim_band_y0{0};
  int anim_band_y1{0};
  bool record_ambient_ms{false};
  bool weather_full{false};
  bool weather_band{false};
  uint32_t weather_gap_ms{0};
  int weather_band_y0{0};
  int weather_band_y1{0};
  uint32_t wake_greeting_hold_ms{0};
  uint32_t activity_rotate_ms{0};
  int activity_rotate_y0{0};
  int activity_rotate_y1{0};
};

void populate_frame_context_state(UiFrameContext &ctx);
bool prepare_frame_plan(const UiFrameContext &ctx, FrameRenderPlan &out);
void render_composed_frame(Rgb565Surface &it, const UiFrameContext &ctx, const FrameRenderPlan &plan);
PostFrameSchedule compute_post_frame_schedule(const UiFrameContext &ctx, const FrameRenderPlan &plan);

}  // namespace latter_ui
