#pragma once

// ==============================================================================
// Latter Concrete Compiled Page & Camera Video Declarations (include/ui_pages.h)
// Pure C++20 L3 header — zero ESPHome includes; compiles cleanly on host via
// Rgb565Surface across all 5 canonical panel geometries.
// ==============================================================================

#include "audio_router.h"
#include "latter_protocol.h"
#include "state/device_runtime_state.h"
#include "ui_compositor.h"
#include "ui_gfx.h"
#include "ui_hit_table.h"
#include "ui_layout.h"
#include "ui_types.h"
#include "ui_widgets.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace latter_ui {

// ------------------------------------------------------------------------------
// 1. Top-Level Page Dispatcher & Concrete Page Renderers
// ------------------------------------------------------------------------------
void render_active_page(Rgb565Surface &it, const UiFrameContext &ctx);

void render_clock_page(Rgb565Surface &it, const UiFrameContext &ctx);
void render_home_page(Rgb565Surface &it, const UiFrameContext &ctx);
void render_music_page(Rgb565Surface &it, const UiFrameContext &ctx);
void render_weather_page(Rgb565Surface &it, const UiFrameContext &ctx);
void render_area_control_page(Rgb565Surface &it, const UiFrameContext &ctx);
void render_home_hub_page(Rgb565Surface &it, const UiFrameContext &ctx);
void render_tv_remote_page(Rgb565Surface &it, const UiFrameContext &ctx);
void render_system_page(Rgb565Surface &it, const UiFrameContext &ctx);
void render_tools_page(Rgb565Surface &it, const UiFrameContext &ctx);
void render_time_page(Rgb565Surface &it, const UiFrameContext &ctx);
void render_games_page(Rgb565Surface &it, const UiFrameContext &ctx);
void render_pinned_page(Rgb565Surface &it, const UiFrameContext &ctx);
void render_calendar_page(Rgb565Surface &it, const UiFrameContext &ctx);
void render_cameras_page(Rgb565Surface &it, const UiFrameContext &ctx);

// Shared live-video blitter & translucent doorbell PTT overlay
bool blit_rgb565_fitted_frame(Rgb565Surface &it, const uint16_t *src,
                              int src_w, int src_h, const CameraRect &dst_rect,
                              bool fill, bool clip_round_bezel = false);
void render_doorbell_ptt_banner(Rgb565Surface &it, const UiFrameContext &ctx,
                                int center_y);
void render_camera_video_placeholder(Rgb565Surface &it, const UiFrameContext &ctx,
                                     const CameraRect &vp, const char *title, const char *subtitle,
                                     UiColor icon_col);

// ------------------------------------------------------------------------------
// 2. Shared Home Activity Collector & Weather / Area / Tools / Games Helpers
// ------------------------------------------------------------------------------
int collect_home_activities(
    uint32_t now_ms, bool is_24h_format,
    bool stopwatch_running, uint32_t stopwatch_start_time, uint32_t stopwatch_elapsed_ms,
    bool music_playing, const std::string &music_title, const std::string &music_artist,
    uint32_t alarm_snooze_until,
    int alarm1_h, int alarm1_m, bool alarm1_en,
    int alarm2_h, int alarm2_m, bool alarm2_en,
    int alarm3_h, int alarm3_m, bool alarm3_en,
    HomeActivityItem out_items[kMaxHomeActivity],
    int cur_hour = -1, int cur_min = -1,
    const std::string &calendar_agenda_raw = "",
    int cur_day_of_week = -1,
    int snoozed_slot = -1);

int get_activity_overlay_card_y(int vis_count, int row_idx);

void resolve_weather_sim_view(std::string_view sim_opt, std::string_view &cond, bool &is_night) noexcept;
void apply_weather_sim_override(std::string_view sim_opt, std::string &cond, bool &is_night);
std::string format_weather_title(const std::string &c_in);
bool &night_meteor_epoch_initialized();
bool has_weather_motion_sv(std::string_view cond, bool is_night, uint32_t now_ms = 0);
bool has_weather_motion(const std::string &cond, bool is_night, uint32_t now_ms = 0);
float weather_precip_alpha_scale();
void render_weather_motion_wallpaper(Rgb565Surface &it, int cx, int cy, uint32_t now_ms,
                                     const std::string &cond, bool is_night,
                                     double moon_cycle = 0.5,
                                     int cel_cx = 341, int cel_cy = 91,
                                     bool mask_hero_temp = true,
                                     float accel_x = 0.0f, float accel_y = 0.0f,
                                     bool show_celestial = true);
void render_static_day_night_sky_base(Rgb565Surface &it, int cx, int cy, bool is_night);
void format_iso_time_to(const std::string &iso_str, bool is_24h, char *out, size_t out_sz);
std::string format_iso_time(const std::string &iso_str);

std::string strip_trailing_paren_state(const std::string &s);

void render_climate_controls_deck(Rgb565Surface &it,
                                  const UiFrameContext &ctx,
                                  const DeviceRuntimeState &rs,
                                  const ThermostatZoneItem &clim,
                                  int content_top,
                                  int content_bot);

void reset_calculator_state();
void handle_calculator_keypad_tap(int tx, int ty);

void compute_3d_game_tilt(float raw_ax, float raw_ay, float raw_az,
                          float euler_roll_deg, float euler_pitch_deg,
                          bool game_just_entered,
                          float &out_roll_delta, float &out_pitch_delta);

}  // namespace latter_ui
