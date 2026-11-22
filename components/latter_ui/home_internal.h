#pragma once

// ==============================================================================
// Internal shared declarations between home_activity_model.cpp & page_home.cpp
// Pure C++20 internal L3 header — zero ESPHome includes.
// ==============================================================================

#include "ui_pages.h"

#include <cstdint>
#include <string>

namespace latter_ui {

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
    const HomeActivityView &view, const HomeActivityItem *act_items, int act_count,
    int &out_nav_idx);

const HomeActivityItem *collect_home_activities_cached(
    const UiFrameContext &ctx, bool music_playing,
    const std::string &music_title, const std::string &music_artist,
    int &out_count);

const char *resolve_wake_greeting(
    int hour, int minute, int day_of_year,
    const std::string &cond, float temp_f);

}  // namespace latter_ui
