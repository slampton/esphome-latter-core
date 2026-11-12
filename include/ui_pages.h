#pragma once

// ==============================================================================
// Latter Concrete Compiled Page & Overlay Declarations (include/ui_pages.h)
// Replaces 9 header-only template page files (~14,000L) with concrete C++20
// entry points compiled in 10 dedicated translation units under components/latter_ui/:
//   page_clock_home.cpp, page_music.cpp, page_weather.cpp, page_area_hub.cpp,
//   page_remote.cpp, page_system.cpp, page_ambient_va.cpp, page_tools.cpp,
//   page_comms.cpp, page_games.cpp.
// ==============================================================================

#include <cstdint>
#include <string>

#include "latter_protocol.h"
#include "latter_state.h"
#include "ui_compositor.h"
#include "ui_gfx.h"
#include "ui_hit_table.h"
#include "ui_layout.h"
#include "ui_types.h"
#include "ui_widgets.h"

namespace latter_ui {

// ------------------------------------------------------------------------------
// 1. Top-Level Page Dispatcher & Concrete Page Renderers
// ------------------------------------------------------------------------------
void render_active_page(esphome::display::Display &it, const UiFrameContext &ctx);

void render_clock_page(esphome::display::Display &it, const UiFrameContext &ctx);
void render_home_page(esphome::display::Display &it, const UiFrameContext &ctx);
void render_music_page(esphome::display::Display &it, const UiFrameContext &ctx);
void render_weather_page(esphome::display::Display &it, const UiFrameContext &ctx);
void render_area_control_page(esphome::display::Display &it, const UiFrameContext &ctx);
void render_whole_home_page(esphome::display::Display &it, const UiFrameContext &ctx);
void render_tv_remote_page(esphome::display::Display &it, const UiFrameContext &ctx);
void render_system_page(esphome::display::Display &it, const UiFrameContext &ctx);
void render_tools_page(esphome::display::Display &it, const UiFrameContext &ctx);
void render_comms_page(esphome::display::Display &it, const UiFrameContext &ctx);
void render_games_page(esphome::display::Display &it, const UiFrameContext &ctx);

// ------------------------------------------------------------------------------
// 2. Global System Takeover Overlays (called by render_system_takeovers)
// ------------------------------------------------------------------------------
void render_alert_ringing_overlay(esphome::display::Display &it, const UiFrameContext &ctx,
                                  const SystemTakeoverState &st);
void render_voice_assistant_overlay(esphome::display::Display &it, const UiFrameContext &ctx,
                                    const SystemTakeoverState &st);
void render_notification_overlay(esphome::display::Display &it, const UiFrameContext &ctx,
                                 const SystemTakeoverState &st);
void render_docked_screensaver(esphome::display::Display &it, const UiFrameContext &ctx,
                               const SystemTakeoverState &st);

// ------------------------------------------------------------------------------
// 3. Shared Home / Activity Deck State & Helpers (page_clock_home.cpp + touch_engine.cpp)
// ------------------------------------------------------------------------------
static constexpr uint32_t kHomeActivityRotateMs = 8000U;

struct HomeActivityItem {
  int kind = 0;        // 1 = Timer, 2 = Stopwatch, 3 = Music, 4 = Alarm, 5 = Calendar
  int slot_idx = 0;    // Timer slot (0..2) or Alarm slot (0 = Snoozed, 1..3 = Alarm 1..3)
  bool is_active = false;
  bool urgent_pin = false;
  bool in_deck = true;
  std::string label{};
};

struct HomeActivityView {
  int top = -1;
  int bottom = -1;
  bool pinned = false;
};

bool &music_queue_cleared();
int &home_activity_overlay_page();
uint32_t &wake_greeting_start_ms();
bool &wake_greeting_latched();
bool is_wake_greeting_fading(uint32_t now_ms, bool enabled);
bool is_home_activity_carousel_animating(uint32_t now_ms);

int collect_home_activities(
    uint32_t now_ms, bool is_24h_format,
    bool stopwatch_running, uint32_t stopwatch_start_time, uint32_t stopwatch_elapsed_ms,
    bool music_playing, const std::string &music_title, const std::string &music_artist,
    uint32_t alarm_snooze_until,
    int alarm1_h, int alarm1_m, bool alarm1_en,
    int alarm2_h, int alarm2_m, bool alarm2_en,
    int alarm3_h, int alarm3_m, bool alarm3_en,
    HomeActivityItem out_items[8],
    int cur_hour = -1, int cur_min = -1,
    const std::string &calendar_agenda_raw = "");

HomeActivityView resolve_home_activity_view(const HomeActivityItem items[8], int count,
                                            uint32_t now_ms, bool two_line);
int get_activity_overlay_card_y(int vis_count, int row_idx);

// ------------------------------------------------------------------------------
// 4. Shared Music & Weather Cross-Page Helpers (page_music.cpp / page_weather.cpp)
// ------------------------------------------------------------------------------
void set_remote_play_override(bool playing, uint32_t now_ms);
bool is_music_playing_effective(bool is_remote, bool local_playing,
                                const std::string &remote_state_str, uint32_t now_ms);

bool &weather_sim_dirty();
void apply_weather_sim_override(const std::string &sim_opt, std::string &cond, bool &is_night);
std::string format_weather_title(const std::string &c_in);
bool &night_meteor_epoch_initialized();
bool has_weather_motion(const std::string &cond, bool is_night, uint32_t now_ms = 0);
void render_weather_motion_wallpaper(esphome::display::Display &it, int cx, int cy, uint32_t now_ms,
                                     const std::string &cond, bool is_night,
                                     double moon_cycle = 0.5,
                                     int cel_cx = 341, int cel_cy = 91,
                                     bool mask_hero_temp = true,
                                     float accel_x = 0.0f, float accel_y = 0.0f,
                                     bool show_celestial = true);

// ------------------------------------------------------------------------------
// 5. Shared Area, Tools, Comms & Games State (page_area_hub/tools/comms/games.cpp)
// ------------------------------------------------------------------------------
std::string strip_trailing_paren_state(const std::string &s);

bool &stopwatch_modern_digital();
void reset_calculator_state();
void handle_calculator_keypad_tap(int tx, int ty);

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

std::string &cal_detail_time();
std::string &cal_detail_title();
std::string &cal_detail_location();
std::string &cal_detail_map_url();
int &cal_map_zoom();
int &cal_map_pan_x();
int &cal_map_pan_y();
std::string &cal_feedback_text();
uint32_t &cal_feedback_ms();
void parse_calendar_slot_meta(const std::string &slot_name, const std::string &slot_id,
                              std::string &out_time, std::string &out_title,
                              std::string &out_loc, std::string &out_map_url);

static constexpr int MAX_CAMERA_SLOTS = 10;

bool &intercom_live_call_mode();
int parse_intercom_targets_list(const std::string &raw, std::string out_ids[5],
                                std::string out_labels[5], bool out_all[5] = nullptr);

int &cam_carousel_idx();
bool &cam_call_active();
bool &cam_listen_active();
bool &cam_intercom_ptt_active();
std::string &cam_intercom_status();
int &cam_noise_idx();
bool &cam_landscape_mode();
uint32_t &last_cam_live_fetch_ms();
bool &cam_download_in_progress();
int parse_cameras_summary_list(const std::string &raw,
                               std::string out_ids[MAX_CAMERA_SLOTS],
                               std::string out_names[MAX_CAMERA_SLOTS],
                               std::string out_urls[MAX_CAMERA_SLOTS] = nullptr);
std::string resolve_active_carousel_camera_id(const std::string &cameras_summary,
                                              const std::string &fallback_cam_url);
std::string build_carousel_camera_url(const std::string &base_cam_url,
                                      const std::string &cameras_summary,
                                      uint32_t now_ms);

void compute_3d_game_tilt(float raw_ax, float raw_ay, float raw_az,
                          float euler_roll_deg, float euler_pitch_deg,
                          bool game_just_entered,
                          float &out_roll_delta, float &out_pitch_delta);
int &pending_game_sfx();
int &game_2048_pending_dir();

}  // namespace latter_ui
