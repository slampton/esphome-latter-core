#pragma once

// ==============================================================================
// Latter Core UI Module: Watch Faces, Home Hub, Live Activities & Dock (ui_clock_home.h)
// ==============================================================================

#include "ui_layout.h"
#include "ui_tools.h"
#include "ui_weather_sky.h"

namespace pocket_ui {

// =============================================================================
// SECTION 16: CLOCK PAGE ENGINE (DIGITAL & ANALOG WATCH FACES & BATTERY GAUGE)
// =============================================================================

template <typename DisplayT, typename FontT>
inline void render_battery_indicator(
    DisplayT &it, int cx, int by, float batt, bool is_charging,
    FontT *font_small, const esphome::Color &theme_primary,
    const esphome::Color &col_gold, const esphome::Color &col_green) {
  if (std::isnan(batt)) return;
  char batt_str[16];
  snprintf(batt_str, sizeof(batt_str), "%.0f%%", batt);
  int text_x = cx + 8;
  it.print(text_x,     by, font_small, theme_primary, esphome::display::TextAlign::CENTER, batt_str);
  it.print(text_x + 1, by, font_small, theme_primary, esphome::display::TextAlign::CENTER, batt_str);
  int bx = text_x - (batt >= 100.0f ? 36 : 28);
  if (is_charging) {
    it.filled_triangle(bx + 3, by - 9, bx - 3, by, bx + 1, by, col_gold);
    it.filled_triangle(bx - 1, by, bx + 2, by, bx - 2, by + 9, col_gold);
  } else {
    it.filled_rectangle(bx - 2, by - 9, 4, 2, col_green);
    it.rectangle(bx - 5, by - 7, 10, 16, col_green);
    int fill_h = static_cast<int>(std::max(1.0f, std::min(12.0f, (batt / 100.0f) * 12.0f)));
    it.filled_rectangle(bx - 3, (by + 7) - fill_h, 6, fill_h, col_green);
  }
}

template <typename DisplayT, typename TimeT, typename FontT>
inline void render_clock_page(
    DisplayT &it, int cx, int cy, const TimeT &now,
    bool clock_face_digital, bool is_24h_format,
    float batt, bool is_charging,
    FontT *font_time, FontT *font_date, FontT *font_small,
    const esphome::Color &theme_primary, const esphome::Color &theme_secondary,
    const esphome::Color &col_white, const esphome::Color &col_slate,
    const esphome::Color &col_gray, const esphome::Color &col_dark_gray,
    const esphome::Color &col_red, const esphome::Color &col_gold,
    const esphome::Color &col_green) {
  // 1. Always render the watch bezel rings, 60-tick perimeter chapter ring, and live battery gauge
  //    immediately on boot/wake so the watch face looks complete even before Home Assistant time syncs.
  it.circle(cx, cy, 230, col_dark_gray);
  it.circle(cx, cy, 228, col_slate);
  it.circle(cx, cy, 222, col_dark_gray);

  for (int i = 0; i < 60; i++) {
    if (!clock_face_digital && i == 30) continue;  // 6 o'clock pip sanctuary on Analog dial
    float s = DIAL_SIN[i];
    float c = DIAL_COS[i];
    int r_in = (i % 5 == 0) ? 212 : 216;
    const esphome::Color &tick_col = (i % 5 == 0) ? col_slate : col_gray;
    it.line(cx + static_cast<int>(r_in * s), cy - static_cast<int>(r_in * c),
            cx + static_cast<int>(220 * s), cy - static_cast<int>(220 * c), tick_col);
  }

  render_battery_indicator(it, cx, 348, batt, is_charging, font_small,
                           theme_primary, col_gold, col_green);

  // 2. Pre-sync state: minimalist "--:--" placeholder without text clutter until HA time syncs
  if (!now.is_valid()) {
    if (clock_face_digital) {
      it.print(cx, 205, font_time, col_slate, esphome::display::TextAlign::CENTER, "--:--");
    } else {
      it.print(cx, 305, font_date, col_slate, esphome::display::TextAlign::CENTER, "--:--");
      it.filled_circle(cx, cy, 7, col_white);
      it.filled_circle(cx, cy, 4, col_red);
      it.filled_circle(cx, cy, 2, col_dark_gray);
    }
    return;
  }

  // 3. Synchronized live watch face rendering
  int hour = now.hour;
  int min = now.minute;
  int sec = now.second;
  int sec_idx = ((sec % 60) + 60) % 60;

  if (clock_face_digital) {
    // Highlight active second tick in theme primary (enlarged +25% in length & width)
    float s_sec = DIAL_SIN[sec_idx];
    float c_sec = DIAL_COS[sec_idx];
    float perp_x = -c_sec;
    float perp_y = -s_sec;
    for (int w = -1; w <= 1; w++) {
      int x1 = cx + static_cast<int>(205.0f * s_sec + w * perp_x);
      int y1 = cy - static_cast<int>(205.0f * c_sec - w * perp_y);
      int x2 = cx + static_cast<int>(220.0f * s_sec + w * perp_x);
      int y2 = cy - static_cast<int>(220.0f * c_sec - w * perp_y);
      it.line(x1, y1, x2, y2, theme_primary);
    }

    // Date Header: "MON, SEP 28" (Theme Primary)
    it.strftime(cx, 130, font_date, theme_primary, esphome::display::TextAlign::CENTER, "%a, %b %d", now);

    // Giant Bold Digital Time: "10:45" (12h) or "14:45" (24h)
    char dig_buf[16];
    char ap_str[8] = "";
    format_time_hhmm(hour, min, is_24h_format, dig_buf, sizeof(dig_buf), ap_str);
    it.print(cx, 205, font_time, col_white, esphome::display::TextAlign::CENTER, dig_buf);

    // AM/PM & Seconds Pill: (Theme Secondary)
    char sec_buf[32];
    if (is_24h_format) {
      snprintf(sec_buf, sizeof(sec_buf), "SEC  •  %02ds", sec);
    } else {
      snprintf(sec_buf, sizeof(sec_buf), "%s  •  %02ds", ap_str, sec);
    }
    it.print(cx, 262, font_small, theme_secondary, esphome::display::TextAlign::CENTER, sec_buf);
  } else {
    // Formatted date (Theme Primary) and digital time complication (uniform slate gray)
    it.strftime(cx, 104, font_date, theme_primary, esphome::display::TextAlign::CENTER, "%a, %b %d", now);
    char time_buf[32];
    if (is_24h_format) {
      snprintf(time_buf, sizeof(time_buf), "%02d:%02d", hour, min);
    } else {
      int h12 = hour % 12;
      if (h12 == 0) h12 = 12;
      snprintf(time_buf, sizeof(time_buf), "%d:%02d %s", h12, min, hour >= 12 ? "pm" : "am");
    }
    it.print(cx, 305, font_date, col_slate, esphome::display::TextAlign::CENTER, time_buf);

    // Zero-trig LUT angles for Hour, Minute, and Second hands
    float h_angle = ((hour % 12) + min / 60.0f + sec / 3600.0f) * (2.0f * PI_F / 12.0f);
    float m_angle = (min + sec / 60.0f) * (2.0f * PI_F / 60.0f);

    // Hour Hand: Bold White Sword Hand (base width 14px, length 88px)
    {
      float sin_h = 0.0f, cos_h = 1.0f;
      fast_sincos(h_angle, sin_h, cos_h);
      it.filled_triangle(cx - static_cast<int>(7.0f * cos_h), cy - static_cast<int>(7.0f * sin_h),
                         cx + static_cast<int>(7.0f * cos_h), cy + static_cast<int>(7.0f * sin_h),
                         cx + static_cast<int>(88.0f * sin_h), cy - static_cast<int>(88.0f * cos_h),
                         col_white);
    }

    // Minute Hand: Slender Slate Gray Sword Hand (base width 8px, length 148px)
    {
      float sin_m = 0.0f, cos_m = 1.0f;
      fast_sincos(m_angle, sin_m, cos_m);
      it.filled_triangle(cx - static_cast<int>(4.0f * cos_m), cy - static_cast<int>(4.0f * sin_m),
                         cx + static_cast<int>(4.0f * cos_m), cy + static_cast<int>(4.0f * sin_m),
                         cx + static_cast<int>(148.0f * sin_m), cy - static_cast<int>(148.0f * cos_m),
                         col_slate);
    }

    // Second Hand: Bold Red Needle (length 168px) with 4px circular counterweight via O(1) DIAL_SIN/COS
    {
      float sin_s = DIAL_SIN[sec_idx];
      float cos_s = DIAL_COS[sec_idx];
      int tip_x = cx + static_cast<int>(168.0f * sin_s);
      int tip_y = cy - static_cast<int>(168.0f * cos_s);
      int tail_x = cx - static_cast<int>(30.0f * sin_s);
      int tail_y = cy + static_cast<int>(30.0f * cos_s);
      it.line(tail_x, tail_y, tip_x, tip_y, col_red);
      it.filled_circle(tail_x, tail_y, 4, col_red);
    }

    // Center Pinion Cap (White ring, red center pip, dark gray arbor)
    it.filled_circle(cx, cy, 7, col_white);
    it.filled_circle(cx, cy, 4, col_red);
    it.filled_circle(cx, cy, 2, col_dark_gray);
  }
}

// =============================================================================
// SECTION 16B: INTERACTIVE HOME PAGE ENGINE (PAGE 1 — 20 FPS WEATHER + SMART STACK + DOCK)
// =============================================================================

inline bool &music_queue_cleared() {
  static bool cleared = false;
  return cleared;
}

inline int &home_activity_overlay_page() {
  static int pg = 0;
  return pg;
}

struct HomeActivityItem {
  int kind;        // 1 = Timer, 2 = Stopwatch, 3 = Music, 4 = Alarm
  int slot_idx;    // Timer slot (0..2) or Alarm slot (0 = Snoozed, 1..3 = Alarm 1..3)
  bool is_active;  // true = running / playing / snoozed, false = paused / armed
  bool urgent_pin; // true = locks top slot & pauses rotation (ONLY running Stopwatch or running Timer <= 60s!)
  bool in_deck;    // true = rotates on front Home card; false = distant alarm (>4h away), shown only in dropdown overlay!
  std::string label;
};

inline int collect_home_activities(
    uint32_t now_ms, bool is_24h_format,
    bool stopwatch_running, uint32_t stopwatch_start_time, uint32_t stopwatch_elapsed_ms,
    bool music_playing, const std::string &music_title, const std::string &music_artist,
    uint32_t alarm_snooze_until,
    int alarm1_h, int alarm1_m, bool alarm1_en,
    int alarm2_h, int alarm2_m, bool alarm2_en,
    int alarm3_h, int alarm3_m, bool alarm3_en,
    HomeActivityItem out_items[8],
    int cur_hour = -1, int cur_min = -1,
    const std::string &calendar_agenda_raw = "") {
  int count = 0;

  // 1. Active or Paused Countdown Timers (locks top slot ONLY if running with <= 60 seconds left!)
  auto *t_slots = timer_slots();
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < 3 && count < 8; i++) {
      bool match = (pass == 0) ? t_slots[i].running : (!t_slots[i].running && t_slots[i].paused);
      if (match) {
        const auto &s = t_slots[i];
        std::string t_name = s.name.empty() ? ("Timer " + std::to_string(i + 1)) : s.name;
        char buf[64];
        snprintf(buf, sizeof(buf), "%s • %02d:%02d",
                 t_name.c_str(), s.remaining_sec / 60, s.remaining_sec % 60);
        bool is_urgent = s.running && (s.remaining_sec <= 60);
        out_items[count++] = {1, i, s.running, is_urgent, true, std::string(buf)};
      }
    }
  }

  // 2. Active or Paused Stopwatch (locks top slot whenever actively running!)
  uint32_t sw_ms = stopwatch_running ? (now_ms - stopwatch_start_time) : stopwatch_elapsed_ms;
  if ((stopwatch_running || sw_ms > 0) && count < 8) {
    int mins = (sw_ms / 60000U) % 60U;
    int secs = (sw_ms / 1000U) % 60U;
    int tenths = (sw_ms / 100U) % 10U;
    char buf[48];
    snprintf(buf, sizeof(buf), "Stopwatch • %02d:%02d.%d", mins, secs, tenths);
    out_items[count++] = {2, 0, stopwatch_running, stopwatch_running, true, std::string(buf)};
  }

  // 3. Active or Paused Music Queue (automatically displays on Hibernate wake / boot whenever
  //    the active speaker has a track or podcast in its queue, until Clear Queue is pressed!)
  bool has_valid_track = (!music_title.empty() && music_title != "None" &&
                          music_title != "Idle" && music_title != "unknown" &&
                          music_title != "unavailable");
  if (music_playing) {
    music_queue_cleared() = false;
  }
  if ((music_playing || (has_valid_track && !music_queue_cleared())) && count < 8) {
    std::string m_txt = music_title;
    if (!has_valid_track) m_txt = "Now Playing";
    else if (!music_artist.empty() && music_artist != "None" && music_artist != "unknown") {
      m_txt = music_artist + " • " + m_txt;
    }
    out_items[count++] = {3, 0, music_playing, false, true, m_txt};
  }

  // 4. Armed or Snoozed Alarms:
  //    - Always included in the dropdown overlay list (out_items)
  //    - Only included in front-card rotation (in_deck = true) when Snoozed or within <= 4 hours (240 min)!
  //    - Never locks the top slot (urgent_pin = false) so it never steals focus from Music/Timers!
  if (alarm_snooze_until > 0 && count < 8) {
    out_items[count++] = {4, 0, true, false, true, "Alarm • Snoozed"};
  } else {
    int a_hrs[3] = {alarm1_h, alarm2_h, alarm3_h};
    int a_mns[3] = {alarm1_m, alarm2_m, alarm3_m};
    bool a_ens[3] = {alarm1_en, alarm2_en, alarm3_en};
    int now_total_m = (cur_hour >= 0 && cur_min >= 0) ? (cur_hour * 60 + cur_min) : -1;
    for (int i = 0; i < 3 && count < 8; i++) {
      if (!a_ens[i]) continue;
      char t_buf[16], ap_buf[8] = "";
      format_time_hhmm(a_hrs[i], a_mns[i], is_24h_format, t_buf, sizeof(t_buf), ap_buf);
      char buf[48];
      if (!is_24h_format && ap_buf[0] != '\0') {
        snprintf(buf, sizeof(buf), "Alarm • %s %s", t_buf, ap_buf);
      } else {
        snprintf(buf, sizeof(buf), "Alarm • %s", t_buf);
      }
      bool within_4h = false;
      if (now_total_m >= 0) {
        int a_total_m = a_hrs[i] * 60 + a_mns[i];
        int diff_m = (a_total_m - now_total_m + 1440) % 1440;
        if (diff_m > 0 && diff_m <= 240) {
          within_4h = true;
        }
      }
      out_items[count++] = {4, i + 1, false, false, within_4h, std::string(buf)};
    }
  }

  // 5. Upcoming Calendar Event (from sensor.pocket_assistant_calendar_agenda "TODAY 14:30|Dentist;...")
  if (!calendar_agenda_raw.empty() && calendar_agenda_raw != "NONE" &&
      calendar_agenda_raw != "unknown" && calendar_agenda_raw != "unavailable" && count < 8) {
    size_t semi = calendar_agenda_raw.find(';');
    std::string first_ev = (semi == std::string::npos) ? calendar_agenda_raw : calendar_agenda_raw.substr(0, semi);
    size_t bar = first_ev.find('|');
    if (bar != std::string::npos) {
      first_ev = first_ev.substr(0, bar) + " • " + first_ev.substr(bar + 1);
    }
    bool is_today = (first_ev.rfind("TODAY", 0) == 0 || first_ev.rfind("NOW", 0) == 0);
    out_items[count++] = {5, 0, false, false, is_today, first_ev};
  }

  return count;
}

// Deterministic 3-step upward carousel reel state for the Home Live Activity Card
inline int &home_act_anim_step() {
  static int step = 0; // 0 = settled, 1..3 = upward reel transition frames
  return step;
}

inline int &home_act_prev_idx() {
  static int idx = 0;
  return idx;
}

inline int &home_act_cur_idx() {
  static int idx = 0;
  return idx;
}

inline bool is_home_activity_carousel_animating(uint32_t /*now_ms*/) {
  return (home_act_anim_step() > 0);
}

// Computes an adaptive, slightly lighter 3D card palette that complements whichever
// weather sky background is currently active on the Home screen!
inline void resolve_home_card_palette(
    bool show_weather_bg, bool is_night, const std::string &cond,
    esphome::Color &out_card_bg, esphome::Color &out_card_brd, esphome::Color &out_btn_bg) {
  using esphome::Color;
  if (!show_weather_bg) {
    out_card_bg  = Color(22, 27, 36);
    out_card_brd = Color(52, 62, 78);
    out_btn_bg   = Color(32, 39, 52);
    return;
  }
  const bool is_fog = (cond.find("fog") != std::string::npos ||
                       cond.find("mist") != std::string::npos ||
                       cond.find("haze") != std::string::npos);
  if (is_night) {
    if (is_fog) {
      // Night Fog sky is (10, 15, 22)
      out_card_bg  = Color(28, 36, 48);
      out_card_brd = Color(58, 70, 88);
      out_btn_bg   = Color(38, 48, 62);
    } else {
      // Clear / Cloudy / Rainy Night sky is (0, 0, 0)
      out_card_bg  = Color(22, 27, 36);
      out_card_brd = Color(52, 62, 78);
      out_btn_bg   = Color(32, 39, 52);
    }
    return;
  }

  // Daytime Sky Palettes:
  const bool is_partly = (cond.find("partly") != std::string::npos ||
                          cond.find("mostlysunny") != std::string::npos ||
                          cond.find("partlysunny") != std::string::npos);
  const bool is_storm_or_rain = (cond.find("rain") != std::string::npos ||
                                 cond.find("pouring") != std::string::npos ||
                                 cond.find("drizzle") != std::string::npos ||
                                 cond.find("lightning") != std::string::npos ||
                                 cond.find("thunder") != std::string::npos);
  if (is_fog) {
    // Daytime Fog sky is (32, 46, 60) -> Misty Slate Blue 3D Card
    out_card_bg  = Color(50, 68, 88);
    out_card_brd = Color(84, 106, 132);
    out_btn_bg   = Color(62, 82, 104);
  } else if (is_storm_or_rain) {
    // Daytime Rain / Storm sky is (18, 30, 46) -> Storm Slate 3D Card
    out_card_bg  = Color(34, 50, 74);
    out_card_brd = Color(64, 86, 116);
    out_btn_bg   = Color(44, 62, 90);
  } else if (cond.find("cloud") != std::string::npos && !is_partly) {
    // Daytime Overcast sky is (22, 38, 56) -> Overcast Steel 3D Card
    out_card_bg  = Color(38, 58, 84);
    out_card_brd = Color(70, 96, 128);
    out_btn_bg   = Color(48, 72, 100);
  } else {
    // Clear / Sunny / Partly Cloudy / Windy Daytime sky is (28, 54, 84) -> Rich Steel Azure 3D Card
    out_card_bg  = Color(44, 76, 114);
    out_card_brd = Color(78, 116, 160);
    out_btn_bg   = Color(56, 92, 134);
  }
}

// Selects which activity item is featured on the single Home Live Activity Card:
//   - Returns -1 if ONLY distant alarms (>4h away, in_deck == false) are active (so distant alarms
//     don't clutter the front Home screen, or if other deck items exist, rotates only through in_deck items!).
//   - Priority 1 (Running Stopwatch): Locks top slot & pauses rotation
//   - Priority 2 (Running Timer <= 60s): Locks top slot & pauses rotation
//   - Otherwise: Calmly rotates every 8.0 seconds across all `in_deck` items!
inline int resolve_home_featured_activity_idx(const HomeActivityItem items[8], int count, uint32_t now_ms) {
  if (count <= 0) return -1;
  // 1. Check Priority 1: Running Stopwatch
  for (int i = 0; i < count; i++) {
    if (items[i].kind == 2 && items[i].urgent_pin) return i;
  }
  // 2. Check Priority 2: Running Timer <= 60s
  for (int i = 0; i < count; i++) {
    if (items[i].kind == 1 && items[i].urgent_pin) return i;
  }
  // 3. Collect all items eligible for front-deck rotation (in_deck == true)
  int deck_indices[8];
  int deck_cnt = 0;
  for (int i = 0; i < count; i++) {
    if (items[i].in_deck) {
      deck_indices[deck_cnt++] = i;
    }
  }
  // If only distant (>4h) alarms are armed, still show the first alarm card on Home so you have
  // access to the card/submenu, or if deck_cnt > 0, rotate calmly every 8.0 seconds across deck_indices!
  if (deck_cnt == 0) return 0;
  if (deck_cnt == 1) return deck_indices[0];
  int rot_slot = static_cast<int>((now_ms / 8000U) % static_cast<uint32_t>(deck_cnt));
  return deck_indices[rot_slot];
}

// Helper to compute auto-centered y_mid coordinates for 1..5 overlay cards using our exact
// System Menu standard: h = 44px, r = 12, STRIDE = 63px (19px gap!), centered at CENTER_Y = 236
// (so 5 items sit at {110, 173, 236, 299, 362} — 100% identical to System Menu!).
inline int get_activity_overlay_card_y(int vis_count, int row_idx) {
  static constexpr int STRIDE = 63;
  static constexpr int CENTER_Y = 236;
  int total_span = (vis_count - 1) * STRIDE;
  int start_y = CENTER_Y - (total_span / 2);
  return start_y + row_idx * STRIDE;
}

template <typename DisplayT, typename FontSmallT>
inline void draw_home_activity_row_contents(
    DisplayT &it, int row_y, const HomeActivityItem &cur_act,
    bool has_dropdown_ext, bool is_list_row, FontSmallT *font_small,
    esphome::Color card_bg, esphome::Color btn_bg,
    esphome::Color theme_secondary, esphome::Color col_white,
    float alpha = 1.0f) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const Color fg_w   = (alpha >= 0.99f) ? col_white       : lerp_color(card_bg, col_white, alpha);
  const Color fg_sec = (alpha >= 0.99f) ? theme_secondary : lerp_color(card_bg, theme_secondary, alpha);
  const Color fg_btn = (alpha >= 0.99f) ? btn_bg          : lerp_color(card_bg, btn_bg, alpha);

  // On Home (is_list_row == false), the card spans w = 380px (x = 43..423, ~30px inside the bezel):
  //   - When 2+ active (has_dropdown_ext): Left body x=43..295 (gx=72, txt_x=94, max_w=194), Play/Pause at 328, Dropdown '▼' at 391
  //   - When 1 active (!has_dropdown_ext): Left body x=43..355 (gx=72, txt_x=94, max_w=252), Play/Pause at 389
  // On the translucent overlay (is_list_row == true), cards use the exact System Menu standard:
  //   - w = 276, h = 44, r = 12 (x = 95..371, 63px stride = 19px clear gap!)
  //   - Icon at gx = 113, Text at txt_x = 128 (max_w = 186), Divider at 320, Action Circle at act_bx = 346 (r = 13)
  const int cx_ref  = SCREEN_CX;
  const int gx      = is_list_row ? (cx_ref - 120) : (cx_ref - 161);
  const int txt_x   = is_list_row ? (cx_ref - 105) : (cx_ref - 139);
  const int max_txt = is_list_row ? 186 : (has_dropdown_ext ? 194 : 252);
  const int act_bx  = is_list_row ? (cx_ref + 113) : (has_dropdown_ext ? (cx_ref + 95) : (cx_ref + 156));
  const int btn_r   = is_list_row ?  13 : 19;

  int gy = row_y;
  if (is_list_row) {
    if (cur_act.kind == 1) {
      it.filled_rectangle(gx - 6, gy - 9, 13, 2, fg_w);
      it.filled_rectangle(gx - 6, gy + 8, 13, 2, fg_w);
      it.line(gx - 5, gy - 7, gx - 1, gy, fg_w);
      it.line(gx + 5, gy - 7, gx + 1, gy, fg_w);
      it.line(gx - 1, gy, gx - 5, gy + 7, fg_w);
      it.line(gx + 1, gy, gx + 5, gy + 7, fg_w);
      it.filled_triangle(gx - 3, gy - 4, gx + 3, gy - 4, gx, gy - 1, fg_w);
      it.filled_triangle(gx - 4, gy + 7, gx + 4, gy + 7, gx, gy + 2, fg_w);
    } else if (cur_act.kind == 2) {
      it.filled_rectangle(gx - 2, gy - 10, 5, 2, fg_w);
      it.line(gx + 5, gy - 6, gx + 7, gy - 8, fg_w);
      it.circle(gx, gy + 1, 8, fg_w);
      it.circle(gx, gy + 1, 7, fg_sec);
      it.line(gx, gy + 1, gx + 4, gy - 3, fg_w);
      it.draw_pixel_at(gx, gy + 1, fg_w);
    } else if (cur_act.kind == 3) {
      it.filled_circle(gx - 4, gy + 5, 3, fg_w);
      it.filled_circle(gx + 4, gy + 3, 3, fg_w);
      it.filled_rectangle(gx - 2, gy - 6, 2, 11, fg_w);
      it.filled_rectangle(gx + 5, gy - 8, 2, 11, fg_w);
      it.line(gx - 2, gy - 6, gx + 6, gy - 8, fg_w);
      it.line(gx - 2, gy - 5, gx + 6, gy - 7, fg_w);
    } else if (cur_act.kind == 5) {
      it.rectangle(gx - 7, gy - 6, 15, 14, fg_w);
      it.filled_rectangle(gx - 7, gy - 6, 15, 4, fg_w);
      it.filled_rectangle(gx - 4, gy - 8, 2, 3, fg_w);
      it.filled_rectangle(gx + 3, gy - 8, 2, 3, fg_w);
      it.filled_rectangle(gx - 4, gy + 1, 2, 2, fg_sec);
      it.filled_rectangle(gx,     gy + 1, 2, 2, fg_w);
      it.filled_rectangle(gx + 4, gy + 1, 2, 2, fg_w);
    } else {
      it.filled_circle(gx - 6, gy - 6, 2, fg_w);
      it.filled_circle(gx + 6, gy - 6, 2, fg_w);
      it.filled_circle(gx, gy + 1, 9, card_bg);
      it.circle(gx, gy + 1, 8, fg_w);
      it.line(gx - 5, gy + 8, gx - 7, gy + 10, fg_w);
      it.line(gx + 5, gy + 8, gx + 7, gy + 10, fg_w);
      it.line(gx, gy + 1, gx,     gy - 4, fg_w);
      it.line(gx, gy + 1, gx + 4, gy + 3, fg_w);
    }
  } else {
    // Enlarged (+35%, ~22x24px) Monochromatic Tools-Style Icons for the 68px-tall Home Live Activity Card!
    if (cur_act.kind == 1) {
      it.filled_rectangle(gx - 9, gy - 12, 19, 2, fg_w);
      it.filled_rectangle(gx - 9, gy + 11, 19, 2, fg_w);
      it.line(gx - 7, gy - 10, gx - 1, gy, fg_w);
      it.line(gx - 6, gy - 10, gx,     gy, fg_w);
      it.line(gx + 7, gy - 10, gx + 1, gy, fg_w);
      it.line(gx + 6, gy - 10, gx,     gy, fg_w);
      it.line(gx - 1, gy, gx - 7, gy + 10, fg_w);
      it.line(gx,     gy, gx - 6, gy + 10, fg_w);
      it.line(gx + 1, gy, gx + 7, gy + 10, fg_w);
      it.line(gx,     gy, gx + 6, gy + 10, fg_w);
      it.filled_triangle(gx - 4, gy - 6, gx + 4, gy - 6, gx, gy - 1, fg_w);
      it.filled_triangle(gx - 5, gy + 9, gx + 5, gy + 9, gx, gy + 3, fg_w);
    } else if (cur_act.kind == 2) {
      it.filled_rectangle(gx - 3, gy - 13, 7, 2, fg_w);
      it.filled_rectangle(gx - 1, gy - 11, 3, 2, fg_w);
      it.line(gx + 7, gy - 7, gx + 10, gy - 10, fg_w);
      it.line(gx + 8, gy - 6, gx + 11, gy - 9,  fg_w);
      it.circle(gx, gy + 2, 10, fg_w);
      it.circle(gx, gy + 2, 9,  fg_w);
      it.line(gx,     gy + 2, gx + 5, gy - 3, fg_w);
      it.line(gx + 1, gy + 2, gx + 5, gy - 2, fg_w);
      it.filled_circle(gx, gy + 2, 2, fg_w);
    } else if (cur_act.kind == 3) {
      it.filled_circle(gx - 6, gy + 6, 4, fg_w);
      it.filled_circle(gx + 5, gy + 4, 4, fg_w);
      it.filled_rectangle(gx - 3, gy - 9,  2, 15, fg_w);
      it.filled_rectangle(gx + 8, gy - 11, 2, 15, fg_w);
      it.line(gx - 3, gy - 9, gx + 9, gy - 11, fg_w);
      it.line(gx - 3, gy - 8, gx + 9, gy - 10, fg_w);
      it.line(gx - 3, gy - 7, gx + 9, gy - 9,  fg_w);
    } else if (cur_act.kind == 5) {
      it.rectangle(gx - 10, gy - 9, 21, 20, fg_w);
      it.rectangle(gx - 9,  gy - 8, 19, 18, fg_w);
      it.filled_rectangle(gx - 9, gy - 8, 19, 5, fg_w);
      it.filled_rectangle(gx - 6, gy - 12, 3, 4, fg_w);
      it.filled_rectangle(gx + 4, gy - 12, 3, 4, fg_w);
      for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 3; c++) {
          it.filled_rectangle(gx - 6 + c * 5, gy - 1 + r * 5, 3, 3, (r == 0 && c == 1) ? fg_sec : fg_w);
        }
      }
    } else {
      it.filled_circle(gx - 8, gy - 7, 3, fg_w);
      it.filled_circle(gx + 8, gy - 7, 3, fg_w);
      it.filled_circle(gx, gy + 2, 12, card_bg);
      it.circle(gx, gy + 2, 10, fg_w);
      it.circle(gx, gy + 2, 9,  fg_w);
      it.line(gx - 6, gy + 10, gx - 9, gy + 13, fg_w);
      it.line(gx - 5, gy + 10, gx - 8, gy + 13, fg_w);
      it.line(gx + 6, gy + 10, gx + 9, gy + 13, fg_w);
      it.line(gx + 5, gy + 10, gx + 8, gy + 13, fg_w);
      it.line(gx, gy + 2, gx,     gy - 4, fg_w);
      it.line(gx, gy + 2, gx + 4, gy + 4, fg_w);
      it.filled_circle(gx, gy + 2, 2, fg_w);
    }
  }

  std::string fit_act = fit_text_to_width_font(cur_act.label, max_txt, font_small);
  it.print(txt_x, gy, font_small, fg_w, TextAlign::CENTER_LEFT, fit_act.c_str());

  // Inline Action Button (Play / Pause / Disarm / Open Calendar, r = btn_r at act_bx)
  it.filled_circle(act_bx, gy, btn_r, fg_btn);
  it.circle(act_bx, gy, btn_r, fg_sec);

  int sym_s = is_list_row ? 5 : 6;
  if (cur_act.kind == 5) {
    for (int w = -1; w <= 0; w++) {
      it.line(act_bx - 3 + w, gy - sym_s, act_bx + 3 + w, gy, fg_w);
      it.line(act_bx + 3 + w, gy, act_bx - 3 + w, gy + sym_s, fg_w);
    }
  } else if (cur_act.kind == 4) {
    for (int w = -1; w <= 0; w++) {
      it.line(act_bx - sym_s + w, gy - sym_s, act_bx + sym_s + w, gy + sym_s, fg_w);
      it.line(act_bx - sym_s + w, gy + sym_s, act_bx + sym_s + w, gy - sym_s, fg_w);
    }
  } else if (cur_act.is_active) {
    it.filled_rectangle(act_bx - 5, gy - sym_s, 3, sym_s * 2 + 1, fg_w);
    it.filled_rectangle(act_bx + 3, gy - sym_s, 3, sym_s * 2 + 1, fg_w);
  } else {
    it.filled_triangle(act_bx - 4, gy - sym_s, act_bx - 4, gy + sym_s, act_bx + sym_s + 1, gy, fg_w);
  }
}

template <typename DisplayT, typename FontSmallT>
inline void render_home_activity_card_row(
    DisplayT &it, int cx, int pill_y, const HomeActivityItem &cur_act,
    bool has_dropdown_ext, FontSmallT *font_small,
    esphome::Color theme_secondary, esphome::Color col_white,
    bool is_list_row = false,
    esphome::Color card_bg = esphome::Color(22, 27, 36),
    esphome::Color card_brd = esphome::Color(52, 62, 78),
    esphome::Color btn_bg = esphome::Color(32, 39, 52)) {
  const int card_w  = is_list_row ? 276 : std::min(380, SCREEN_W - 26);
  const int card_h  = is_list_row ?  44 : 68;
  const int card_r  = is_list_row ?  12 : 16;
  const int div1_x  = is_list_row ? (cx + 87) : (has_dropdown_ext ? (cx + 63) : (cx + 123));
  const int div_h   = is_list_row ?  14 : 23;
  const int div2_x  = cx + 127;
  const int drop_bx = cx + 158;

  render_modern_card(it, cx, pill_y, card_w, card_h, card_r, card_bg, card_brd,
                     nullptr, col_white, font_small);

  draw_home_activity_row_contents(
      it, pill_y, cur_act, has_dropdown_ext, is_list_row, font_small,
      card_bg, btn_bg, theme_secondary, col_white, 1.0f);

  it.line(div1_x, pill_y - div_h, div1_x, pill_y + div_h, card_brd);

  if (has_dropdown_ext) {
    it.line(div2_x, pill_y - div_h, div2_x, pill_y + div_h, card_brd);
    for (int w = -1; w <= 1; w++) {
      it.line(drop_bx - 8, pill_y - 4 + w, drop_bx,     pill_y + 4 + w, col_white);
      it.line(drop_bx + 8, pill_y - 4 + w, drop_bx,     pill_y + 4 + w, col_white);
    }
  }
}

// Tracks the horizontal sliding-window offset of the Home Quick-Launch Dock (0 = first 3 icons: Music, Remote, Games)
inline int &home_dock_page() {
  static int pg = 0;
  return pg;
}

// ==============================================================================
// MODULAR WAKE GREETING ENGINE (8.0s Hold + 2.5s Smooth Fade after Boot / Wake)
// Easy to customize: edit or add phrases below by weather condition or hour!
// ==============================================================================
inline uint32_t &wake_greeting_start_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline bool &wake_greeting_latched() {
  static bool latched = false;
  return latched;
}

inline bool is_wake_greeting_fading(uint32_t now_ms, bool enabled) {
  if (!enabled || !wake_greeting_latched()) return false;
  uint32_t el = now_ms - wake_greeting_start_ms();
  return (el >= 7900U && el <= 10700U);
}

inline const char *resolve_wake_greeting(int hour, int minute, int day_of_year,
                                         const std::string &cond, float temp_f) {
  int pick = (day_of_year + hour * 3 + (minute / 10)) % 3;

  // 1. Helpful / Playful Weather-Aware Overrides (when weather condition is known)
  if (!cond.empty() && cond != "unknown" && cond != "unavailable") {
    if (cond.find("lightning") != std::string::npos || cond.find("thunder") != std::string::npos) {
      return "Stay dry out there";
    }
    if (cond.find("rain") != std::string::npos || cond.find("pouring") != std::string::npos ||
        cond.find("drizzle") != std::string::npos) {
      return (pick == 0) ? "Rainy out there today" : "Don't forget an umbrella";
    }
    if (cond.find("snow") != std::string::npos || cond.find("sleet") != std::string::npos) {
      return "Bundle up out there";
    }
    if (cond.find("wind") != std::string::npos || cond.find("breez") != std::string::npos) {
      return (pick == 0) ? "Breezy out there today" : "Hold onto your hat today";
    }
    if ((cond == "sunny" || cond == "clear-day" || cond == "clearday") &&
        hour >= 9 && hour < 17 && !std::isnan(temp_f) && temp_f >= 64.0f && temp_f <= 85.0f && pick == 0) {
      return "It's a beautiful day";
    }
  }

  // 2. Time-of-Day Personality Greetings
  if (hour >= 5 && hour < 12) {
    if (pick == 1) return "Rise and shine";
    if (pick == 2) return "Have a great day";
    return "Good morning";
  }
  if (hour >= 12 && hour < 17) {
    if (pick == 1) return "Hope your day is going well";
    return "Good afternoon";
  }
  if (hour >= 17 && hour < 22) {
    if (pick == 1) return "Time to unwind";
    return "Good evening";
  }
  // Late Night (10 PM .. 4:59 AM)
  if (pick == 1) return "Up late tonight?";
  if (pick == 2) return "Rest easy tonight";
  return "Sleep well";
}

inline std::string &latter_dock_order_csv() {
  static std::string csv = "";
  return csv;
}

// Canonical Dock Order (customizable via Latter `dock_apps` CSV attribute):
// Defaults to Music (2), Room Control (6), Remote (7), Games (3), Tools (8), Security Alarm (9), Settings (4)
inline int collect_home_dock_apps(bool show_music, bool show_remote, bool show_tools, bool show_games,
                                  int out_apps[8], bool show_room = true, bool show_alarm = true) {
  int n = 0;
  bool added[12] = {false};
  auto try_add = [&](int app_id) {
    if (n >= 7 || app_id < 0 || app_id > 10 || added[app_id]) return;
    bool en = (app_id == 2 && show_music) ||
              (app_id == 6 && show_room) ||
              (app_id == 7 && show_remote) ||
              (app_id == 3 && show_games) ||
              (app_id == 8 && show_tools) ||
              (app_id == 9 && show_alarm) ||
              (app_id == 4); // Settings is always available in the App Dock
    if (en) {
      added[app_id] = true;
      out_apps[n++] = app_id;
    }
  };

  const std::string &csv = latter_dock_order_csv();
  // Ignore legacy default CSVs so the new canonical order (Music, Home, Room, Remote, Games, Tools, Settings) takes effect automatically
  const bool is_legacy_default_csv = (csv == "music,room,remote,games,tools" ||
                                      csv == "music,room,remote,games,tools,alarm");
  if (!csv.empty() && csv != "unknown" && csv != "unavailable" && !is_legacy_default_csv) {
    size_t pos = 0;
    while (pos < csv.size()) {
      size_t comma = csv.find_first_of(",;| ", pos);
      std::string tok = (comma == std::string::npos) ? csv.substr(pos) : csv.substr(pos, comma - pos);
      pos = (comma == std::string::npos) ? csv.size() : (comma + 1);
      if (tok.empty()) continue;
      for (char &c : tok) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
      if (tok.find("music") != std::string::npos || tok == "2") try_add(2);
      else if (tok.find("alarm") != std::string::npos || tok.find("home") != std::string::npos || tok.find("sec") != std::string::npos || tok == "9") try_add(9);
      else if (tok.find("room") != std::string::npos || tok == "6") try_add(6);
      else if (tok.find("remote") != std::string::npos || tok.find("tv") != std::string::npos || tok == "7") try_add(7);
      else if (tok.find("game") != std::string::npos || tok == "3") try_add(3);
      else if (tok.find("tool") != std::string::npos || tok == "8") try_add(8);
      else if (tok.find("setting") != std::string::npos || tok.find("sys") != std::string::npos || tok == "4") try_add(4);
    }
  }

  // Canonical Default Home Dock Order:
  // 1. Music (2), 2. Home (9), 3. Room (6), 4. Remote (7), 5. Games (3), 6. Tools (8), 7. Settings (4)
  try_add(2); // 1. Music
  try_add(9); // 2. Home (Whole-Home Hub)
  try_add(6); // 3. Room Control
  try_add(7); // 4. Remote Control
  try_add(3); // 5. Games
  try_add(8); // 6. Tools
  try_add(4); // 7. Settings
  return n;
}

// Fluid 4-Icon Sliding Window Helper:
// When total_count > 4, Page 0 shows the first 4 icons (0..3) and Page 1 slides fluidly to show
// the last 4 icons (total_count - 4 .. total_count - 1) so the dock always displays 4 balanced icons!
inline int get_home_dock_start_index(int total_count, int page) {
  if (total_count <= 4 || page <= 0) return 0;
  int total_pages = (total_count + 3) / 4;
  int eff_page = (page >= total_pages) ? (total_pages - 1) : page;
  int raw_start = eff_page * 4;
  if (raw_start + 4 > total_count) {
    raw_start = total_count - 4;
  }
  return std::max(0, raw_start);
}

inline void get_home_dock_window_xs(int total_count, int page, int cx, int out_xs[4],
                                    bool &out_has_left_arrow, bool &out_has_right_arrow) {
  out_has_left_arrow = false;
  out_has_right_arrow = false;
  if (total_count <= 0) return;
  int start_idx = get_home_dock_start_index(total_count, page);
  int vis_count = std::min(4, total_count - start_idx);
  if (vis_count == 1) {
    out_xs[0] = cx;
  } else if (vis_count == 2) {
    out_xs[0] = cx - 44;
    out_xs[1] = cx + 44;
  } else if (vis_count == 3) {
    out_xs[0] = cx - 84;
    out_xs[1] = cx;
    out_xs[2] = cx + 84;
  } else {
#if POCKET_UI_ROUND_DISPLAY
    out_xs[0] = 112;
    out_xs[1] = 193;
    out_xs[2] = 274;
    out_xs[3] = 355;
#else
    out_xs[0] = cx - 147;
    out_xs[1] = cx - 49;
    out_xs[2] = cx + 49;
    out_xs[3] = cx + 147;
#endif
  }
}

template <typename DisplayT, typename TimeT, typename StrT, typename FontTimeT, typename FontDateT, typename FontSmallT, typename WallImgT = void>
inline void render_home_page(
    DisplayT &it, int cx, int cy, uint32_t now_ms, int home_subpage,
    int &home_activity_idx,
    const TimeT &now, bool is_24h_format,
    float batt, bool is_charging,
    bool show_weather_bg,
    bool dock_music, bool dock_remote, bool dock_tools, bool dock_games,
    const StrT &weather_sim_opt, float weather_temp,
    const std::string &weather_cond_raw, const std::string &sun_state,
    const std::string &moon_phase_raw, float accel_x, float accel_y,
    bool stopwatch_running, uint32_t stopwatch_start_time, uint32_t stopwatch_elapsed_ms,
    bool music_playing, const std::string &music_title, const std::string &music_artist,
    uint32_t alarm_snooze_until,
    int alarm1_h, int alarm1_m, bool alarm1_en,
    int alarm2_h, int alarm2_m, bool alarm2_en,
    int alarm3_h, int alarm3_m, bool alarm3_en,
    FontTimeT *font_time, FontDateT *font_date, FontSmallT *font_small,
    esphome::Color theme_primary, esphome::Color theme_secondary,
    esphome::Color col_bg, esphome::Color col_black, esphome::Color col_white,
    esphome::Color col_slate, esphome::Color col_dark_gray,
    esphome::Color col_gold, esphome::Color col_green,
    bool show_wake_greeting = true, bool dock_room = true,
    WallImgT *wallpaper_art = nullptr,
    const std::string &wallpaper_url = "",
    const std::string &loaded_wallpaper_url = "",
    const std::string &calendar_agenda_raw = "",
    bool dock_alarm = true,
    const std::string &home_alarm_state = "") {
  using esphome::Color;
  using esphome::display::TextAlign;

  if (home_subpage == 1) {
    // -------------------------------------------------------------------------
    // SUBPAGE 1: HOME SETTINGS (Canonical 5-Card Template: 30px Top/Bottom Buffer, 19px Gap)
    //   - Title at y = 48 (bottom at y = 58)
    //   - 5 cards (w = 276, h = 44, r = 12) at y_mid = {110, 173, 236, 299, 362} (63px stride)
    //   - Top of Slot 0 = y 88 (30px below title); Bottom of Slot 4 = y 384 (31px above < BACK)
    // -------------------------------------------------------------------------
    it.print(cx, 48, font_small, theme_primary, TextAlign::CENTER, "HOME SETTINGS");

    const Color card_bg_on(22, 27, 36);
    const Color card_bg_off(15, 18, 24);
    const Color card_brd_on(52, 62, 78);
    const Color card_brd_off(34, 40, 50);

    auto draw_setting_card = [&](int y_mid, const char *label, bool is_on) {
      Color bg_c  = is_on ? card_bg_on  : card_bg_off;
      Color brd_c = is_on ? card_brd_on : card_brd_off;
      Color txt_c = is_on ? col_white   : col_slate;
      render_modern_card(it, cx, y_mid, 276, 44, 12, bg_c, brd_c, label, txt_c, font_small);
    };

    char buf[48];
    snprintf(buf, sizeof(buf), "Music Icon: %s", dock_music ? "Shown" : "Hidden");
    draw_setting_card(110, buf, dock_music);

    snprintf(buf, sizeof(buf), "Room Icon: %s", dock_room ? "Shown" : "Hidden");
    draw_setting_card(173, buf, dock_room);

    snprintf(buf, sizeof(buf), "Remote Icon: %s", dock_remote ? "Shown" : "Hidden");
    draw_setting_card(236, buf, dock_remote);

    snprintf(buf, sizeof(buf), "Games Icon: %s", dock_games ? "Shown" : "Hidden");
    draw_setting_card(299, buf, dock_games);

    snprintf(buf, sizeof(buf), "Tools Icon: %s", dock_tools ? "Shown" : "Hidden");
    draw_setting_card(362, buf, dock_tools);

    render_back_button(it, cx, font_small, 426, "< Back");
    return;
  }

  HomeActivityItem act_items[8];
  int act_count = collect_home_activities(
      now_ms, is_24h_format,
      stopwatch_running, stopwatch_start_time, stopwatch_elapsed_ms,
      music_playing, music_title, music_artist,
      alarm_snooze_until,
      alarm1_h, alarm1_m, alarm1_en,
      alarm2_h, alarm2_m, alarm2_en,
      alarm3_h, alarm3_m, alarm3_en,
      act_items,
      now.is_valid() ? now.hour : -1,
      now.is_valid() ? now.minute : -1,
      calendar_agenda_raw);

  // ---------------------------------------------------------------------------
  // SUBPAGE 0 (MAIN HOME) & SUBPAGE 2 (TRANSLUCENT ACTIVITY OVERLAY BACKDROP)
  // ---------------------------------------------------------------------------
  std::string cond = weather_cond_raw;
  bool is_night = (sun_state == "below_horizon" || cond == "clear-night" ||
                   cond == "clearnight" || (cond.find("night") != std::string::npos));
  apply_weather_sim_override(weather_sim_opt, cond, is_night);

  const bool is_dim_backdrop = (home_subpage == 2);
  const bool has_custom_wallpaper = (!wallpaper_url.empty() && wallpaper_url != "none" &&
                                     wallpaper_url == loaded_wallpaper_url &&
                                     wallpaper_art != nullptr && wallpaper_art->get_width() > 0);

  if (has_custom_wallpaper && (!show_weather_bg || weather_sim_opt == "Live (Home Assistant)")) {
    render_album_art_canvas(it, wallpaper_art, cx, false, false, true, false);
  } else if (show_weather_bg) {
    if (is_dim_backdrop) {
      // Ultra-dim (~14% luminance) sky canvas when the Activity Overlay is open (zero interlace stripes!)
      if (!is_night) {
#if POCKET_UI_ROUND_DISPLAY
        it.filled_circle(cx, cy, 228, Color(4, 8, 13));
#else
        it.filled_rectangle(0, 0, SCREEN_W, SCREEN_H, Color(4, 8, 13));
#endif
      }
      // Subtle 14%-bright sun/moon silhouette at (360, 86)
      it.filled_circle(360, 86, 20, is_night ? Color(22, 24, 28) : Color(36, 30, 8));
    } else {
      double moon_cycle = 0.5;
      if (is_night) {
        compute_synodic_moon_phase(now.is_valid(), now.is_valid() ? static_cast<int64_t>(now.timestamp) : 0LL,
                                   moon_phase_raw, moon_cycle, nullptr, nullptr);
      }
      // Celestial Sun/Moon coordinates: (360, 86) on 466x466 Round, (342, 74) on 410x502 Watch
      render_weather_motion_wallpaper(it, cx, cy, now_ms, cond, is_night,
                                      moon_cycle, POCKET_UI_ROUND_DISPLAY ? 360 : 342,
                                      POCKET_UI_ROUND_DISPLAY ? 86 : 74, true, accel_x, accel_y);
    }
  }

  // Smooth 14%-luminance ghost colors when home_subpage == 2 (zero scanline/interlace artifacts!)
  const Color fg_home_white = is_dim_backdrop ? Color(34, 38, 44) : col_white;
  const Color fg_home_cond  = is_dim_backdrop
                                  ? Color(30, 34, 40)
                                  : ((!show_weather_bg || is_night) ? theme_secondary : Color(255, 225, 110));

  // ZONE 1 (y = 52): Centered Date Header in font_date (27pt, blank until HA time syncs!)
  if (now.is_valid()) {
    char date_bar[32];
    TimeT now_copy = now;
    now_copy.strftime(date_bar, sizeof(date_bar), "%a, %b %d");
    if (!is_dim_backdrop) {
      it.print(cx + 1, 53, font_date, col_black, TextAlign::CENTER, date_bar);
    }
    it.print(cx, 52, font_date, fg_home_white, TextAlign::CENTER, date_bar);
  }

  // ZONE 2 (y = 128): Hero Digital Clock (77pt Roboto, "--:--" until time syncs, Tap -> Clock)
  char time_str[16] = "--:--";
  if (now.is_valid()) {
    format_time_hhmm(now.hour, now.minute, is_24h_format, time_str, sizeof(time_str));
  }
  if (!is_dim_backdrop) {
    it.print(cx + 1, 129, font_time, col_black, TextAlign::CENTER, time_str);
  }
  it.print(cx, 128, font_time, fg_home_white, TextAlign::CENTER, time_str);

  // ZONE 3 (y = 200): Stepped-Up Weather Summary in font_date (27pt, blank until weather or simulation arrives!)
  std::string cond_title = format_weather_title(cond);
  const bool sim_override_on = (!weather_sim_opt.empty() && weather_sim_opt != "Live (Home Assistant)");
  const bool weather_ready = (!std::isnan(weather_temp) || sim_override_on) && (cond_title != "Unavailable");
  if (weather_ready) {
    char w_line[64];
    if (!std::isnan(weather_temp)) {
      snprintf(w_line, sizeof(w_line), "%.0f° • %s", weather_temp, cond_title.c_str());
    } else {
      snprintf(w_line, sizeof(w_line), "%s", cond_title.c_str());
    }
    std::string fit_w = fit_text_to_width_font(std::string(w_line), 340, font_date);
    if (!is_dim_backdrop) {
      it.print(cx + 1, 201, font_date, col_black, TextAlign::CENTER, fit_w.c_str());
    }
    it.print(cx, 200, font_date, fg_home_cond, TextAlign::CENTER, fit_w.c_str());
  }

  // TRANSIENT WAKE GREETING LATCH (8.0s Full Brightness + 2.5s Smooth Fade-Out = 10.5s Total)
  if (!wake_greeting_latched() && now.is_valid() && now_ms < 25000U) {
    wake_greeting_start_ms() = now_ms;
    wake_greeting_latched() = true;
  }
  uint32_t greet_el = (wake_greeting_latched() && now_ms >= wake_greeting_start_ms())
                          ? (now_ms - wake_greeting_start_ms())
                          : 999999U;
  const bool greeting_active = show_wake_greeting && wake_greeting_latched() &&
                               now.is_valid() && (greet_el < 10500U);

  // ZONE 4 (pill_y = 271, y = 237..305, h = 68px):
  //   - Adaptive sky-matched 3D card palette (resolve_home_card_palette)
  //   - Deterministic 3-step UPWARD carousel reel transition (outgoing slides UP & fades out,
  //     incoming rises UP from below & fades in, strictly inside the 68px card bounds!)
  if (home_subpage == 0 && !greeting_active && act_count > 0) {
    const int pill_y = 266;
    const bool has_drop = (act_count > 1);
    int feat_idx = resolve_home_featured_activity_idx(act_items, act_count, now_ms);
    if (home_act_cur_idx() >= act_count) home_act_cur_idx() = 0;
    if (home_act_prev_idx() >= act_count) home_act_prev_idx() = 0;

    if (feat_idx != home_act_cur_idx()) {
      home_act_prev_idx() = home_act_cur_idx();
      home_act_cur_idx()  = feat_idx;
      home_act_anim_step() = 1; // Start 3-step upward reel transition!
    }
    home_activity_idx = feat_idx;

    Color card_bg, card_brd, btn_bg;
    resolve_home_card_palette(show_weather_bg, is_night, cond, card_bg, card_brd, btn_bg);

    int step = home_act_anim_step();
    if (has_drop && step >= 1 && step <= 3 && home_act_prev_idx() != home_act_cur_idx()) {
      // Deterministic 3-frame upward reel slide strictly inside the card (zero dip, zero pop!):
      //   Step 1: Outgoing at pill_y - 9  (alpha 0.65), Incoming at pill_y + 15 (alpha 0.40)
      //   Step 2: Outgoing at pill_y - 16 (alpha 0.28), Incoming at pill_y + 8  (alpha 0.75)
      //   Step 3: Incoming at pill_y + 3  (alpha 0.92) -> then step resets to 0 (settled at pill_y)
      static constexpr int8_t OUT_DY[4]    = {0,  -9, -16, -20};
      static constexpr int8_t IN_DY[4]     = {0, +15,  +8,  +3};
      static constexpr float  OUT_ALPHA[4] = {0.0f, 0.65f, 0.25f, 0.0f};
      static constexpr float  IN_ALPHA[4]  = {1.0f, 0.42f, 0.76f, 0.94f};

      const int act_w = std::min(380, SCREEN_W - 26);
      const int act_x0 = cx - act_w / 2;
      render_modern_card(it, cx, pill_y, act_w, 68, 16, card_bg, card_brd, nullptr, col_white, font_small);

      if (OUT_ALPHA[step] > 0.05f) {
        draw_home_activity_row_contents(
            it, pill_y + OUT_DY[step], act_items[home_act_prev_idx()], true, false, font_small,
            card_bg, btn_bg, theme_secondary, col_white, OUT_ALPHA[step]);
      }
      draw_home_activity_row_contents(
          it, pill_y + IN_DY[step], act_items[home_act_cur_idx()], true, false, font_small,
          card_bg, btn_bg, theme_secondary, col_white, IN_ALPHA[step]);

      // Re-stamp top & bottom card border lines, dividers, and stationary '▼' dropdown chevron
      it.horizontal_line(act_x0 + 16, pill_y - 34, act_w - 32, card_brd);
      it.horizontal_line(act_x0 + 16, pill_y + 33, act_w - 32, card_brd);
      it.line(cx + 63,  pill_y - 23, cx + 63,  pill_y + 23, card_brd);
      it.line(cx + 127, pill_y - 23, cx + 127, pill_y + 23, card_brd);
      const int d_bx = cx + 158;
      for (int w = -1; w <= 1; w++) {
        it.line(d_bx - 8, pill_y - 4 + w, d_bx, pill_y + 4 + w, col_white);
        it.line(d_bx + 8, pill_y - 4 + w, d_bx, pill_y + 4 + w, col_white);
      }

      if (step < 3) {
        home_act_anim_step() = step + 1;
      } else {
        home_act_anim_step() = 0;
      }
    } else {
      home_act_anim_step() = 0;
      render_home_activity_card_row(
          it, cx, pill_y, act_items[feat_idx], has_drop, font_small,
          theme_secondary, col_white, false, card_bg, card_brd, btn_bg);
    }
  }

  if (greeting_active) {
    const char *raw_greet = resolve_wake_greeting(
        now.hour, now.minute, now.day_of_year, cond, weather_temp);
    std::string fit_greet = fit_text_to_width_font(std::string(raw_greet), 340, font_date);
    int greet_y = 264;

    // 0..8000ms = 1.0f full brightness; 8000..10500ms = smooth 2.5s Hermite fade into background sky color
    float alpha = 1.0f;
    if (greet_el > 8000U) {
      float u = 1.0f - static_cast<float>(greet_el - 8000U) * (1.0f / 2500.0f);
      if (u < 0.0f) u = 0.0f;
      alpha = u * u * (3.0f - 2.0f * u);
    }

    // Target background color so the text dissolves seamlessly into the daytime or nighttime sky
    Color bg_target(0, 0, 0);
    if (show_weather_bg && !is_night) {
      bg_target = Color(28, 54, 84);
      if (cond.find("fog") != std::string::npos || cond.find("mist") != std::string::npos) {
        bg_target = Color(32, 46, 60);
      } else if (cond.find("cloud") != std::string::npos && cond.find("partly") == std::string::npos) {
        bg_target = Color(22, 38, 56);
      } else if (cond.find("rain") != std::string::npos || cond.find("thunder") != std::string::npos ||
                 cond.find("lightning") != std::string::npos) {
        bg_target = Color(18, 30, 46);
      }
    }

    Color fg_c = lerp_color(bg_target, col_white, alpha);
    if (alpha > 0.35f) {
      Color sh_c = lerp_color(bg_target, col_black, alpha);
      it.print(cx + 1, greet_y + 1, font_date, sh_c, TextAlign::CENTER, fit_greet.c_str());
    }
    it.print(cx, greet_y, font_date, fg_c, TextAlign::CENTER, fit_greet.c_str());
  }

  // ZONE 5 (y = 359): Fluid 4-Icon Sliding App Dock (r = 27 / 54px diameter, 30px outer bezel buffer)
  int dock_apps[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  int dock_count = collect_home_dock_apps(dock_music, dock_remote, dock_tools, dock_games, dock_apps, dock_room, dock_alarm);
  if (dock_count > 0) {
    int total_dock_pages = (dock_count + 3) / 4;
    if (home_dock_page() < 0 || home_dock_page() >= total_dock_pages) {
      home_dock_page() = 0;
    }
    int win_xs[4] = {0, 0, 0, 0};
    bool has_left_arr = false, has_right_arr = false;
    get_home_dock_window_xs(dock_count, home_dock_page(), cx, win_xs, has_left_arr, has_right_arr);
    int start_idx = get_home_dock_start_index(dock_count, home_dock_page());
    int vis_count = std::min(4, dock_count - start_idx);
    const int dock_y = 359;
    const int dock_r = 27;

    auto draw_dock_glyph = [&](int app_id, int bx, int by, Color c, bool is_shadow) {
      if (app_id == 6) {
        // Room Control (Option B): Sculpted 2-Cushion Sofa + Tall Floor Lamp Silhouette
        // 1. Floor Lamp on right (x = bx + 11, lampshade at top y = by - 14..by - 7, pole to y = by + 12)
        it.filled_triangle(bx + 11, by - 15, bx + 6, by - 7, bx + 16, by - 7, c);
        it.filled_rectangle(bx + 8, by - 14, 7, 3, c);
        it.vertical_line(bx + 11, by - 7, 19, c);
        it.vertical_line(bx + 12, by - 7, 19, c);
        it.horizontal_line(bx + 8, by + 12, 8, c);
        // 2. Cozy 2-Cushion Sofa on left/center (bx - 15 .. bx + 5)
        // Backrest (bx - 13 .. bx + 3, y = by - 4 .. by + 4)
        it.filled_rectangle(bx - 12, by - 4, 15, 8, c);
        it.filled_circle(bx - 11, by - 2, 2, c);
        it.filled_circle(bx + 1,  by - 2, 2, c);
        // Left & Right Rolled Armrests + Seat Base
        it.filled_circle(bx - 14, by + 3, 3, c);
        it.filled_circle(bx + 4,  by + 3, 3, c);
        it.filled_rectangle(bx - 15, by + 3, 21, 7, c);
        // Sofa Feet
        it.filled_rectangle(bx - 14, by + 10, 3, 3, c);
        it.filled_rectangle(bx + 2,  by + 10, 3, 3, c);
        if (!is_shadow) {
          Color cut_c = (show_weather_bg && !is_night) ? Color(28, 54, 84) : col_black;
          // Negative-space cushion seam & armrest crease so the sofa reads crisply at 54px!
          it.vertical_line(bx - 5, by - 4, 8, cut_c);
          it.horizontal_line(bx - 12, by + 4, 15, cut_c);
        }
      } else if (app_id == 2) {
        // Music: Enlarged Beamed Eighth Notes (scaled for r = 29)
        it.filled_circle(bx - 9, by + 9, 5, c);
        it.filled_circle(bx + 8, by + 6, 5, c);
        it.filled_rectangle(bx - 5, by - 12, 3, 21, c);
        it.filled_rectangle(bx + 11, by - 15, 3, 21, c);
        it.line(bx - 5, by - 12, bx + 13, by - 15, c);
        it.line(bx - 5, by - 11, bx + 13, by - 14, c);
        it.line(bx - 5, by - 10, bx + 13, by - 13, c);
        it.line(bx - 5, by - 9,  bx + 13, by - 12, c);
      } else if (app_id == 7) {
        // Remote: Balanced 4-Segment Circular D-Pad + Center OK Dot (Slimmer 6px ring + wider moat & diagonal gaps)
        // Matches the open/filled visual weight of Music, Games, and Tools!
        auto is_remote_dpad = [](int dx, int dy) -> bool {
          int r2 = dx * dx + dy * dy;
          // 1. Solid filled center OK dot (r <= 3.8px -> r2 <= 15)
          if (r2 <= 15) return true;
          // 2. Generous 5.2px transparent moat around center OK dot (3.8px < r < 9.0px -> r2 < 81)
          //    and slimmer outer D-pad radius (r <= 15.2px -> r2 <= 232)
          if (r2 < 81 || r2 > 232) return false;
          // 3. Wider 45-degree X-shaped diagonal sector gaps (±2.25px -> 4.5px gap) splitting Up, Down, Left, Right arcs
          static constexpr float INV_SQRT2 = 0.70710678f;
          float fx = static_cast<float>(dx);
          float fy = static_cast<float>(dy);
          float d1 = std::fabs((fx - fy) * INV_SQRT2);
          float d2 = std::fabs((fx + fy) * INV_SQRT2);
          if (d1 <= 2.25f || d2 <= 2.25f) return false;
          return true;
        };

        if (!is_shadow) {
          for (int dy = -17; dy <= 17; dy++) {
            for (int dx = -17; dx <= 17; dx++) {
              if (is_remote_dpad(dx, dy)) {
                if (!is_remote_dpad(dx + 1, dy + 1)) {
                  it.draw_pixel_at(bx + dx + 1, by + dy + 1, col_black);
                }
                it.draw_pixel_at(bx + dx, by + dy, c);
              }
            }
          }
        }
      } else if (app_id == 8) {
        // Tools: Solid Filled Single-Head 45-Degree Wrench (Compact r=8.2px Head + Long Rounded Handle)
        // Matches the solid filled visual weight of the Music icon!
        auto is_wrench_filled = [](int dx, int dy) -> bool {
          static constexpr float INV_SQRT2 = 0.70710678f;
          float fx = static_cast<float>(dx);
          float fy = static_cast<float>(dy);
          float u = (fx - fy) * INV_SQRT2;
          float v = (fx + fy) * INV_SQRT2;

          // 1. Transparent Upper-Right Open U-Jaw Slot (u >= 6.8, |v| <= 3.15px)
          const float jaw_r  = 3.15f;
          const float jaw_u0 = 6.8f;
          if (u >= jaw_u0 && std::fabs(v) <= jaw_r) return false;
          float du_j = u - jaw_u0;
          if ((du_j * du_j + v * v) <= (jaw_r * jaw_r)) return false;

          // 2. Compact Upper-Right Wrench Head (centered at u = +8.5, v = 0, r = 8.2px)
          const float head_r = 8.2f;
          float du_h = u - 8.5f;
          if ((du_h * du_h + v * v) <= (head_r * head_r) && u <= 15.0f) {
            return true;
          }

          // 3. Long Diagonal Handle Capsule (from u = -14.2 to u = +4.5, half-width = 3.25px)
          const float shaft_r = 3.25f;
          if (u >= -14.2f && u <= 4.5f && std::fabs(v) <= shaft_r) return true;
          float du_cap = u + 14.2f;
          if ((du_cap * du_cap + v * v) <= (shaft_r * shaft_r)) return true;

          return false;
        };

        if (!is_shadow) {
          for (int dy = -18; dy <= 18; dy++) {
            for (int dx = -18; dx <= 18; dx++) {
              if (is_wrench_filled(dx, dy)) {
                if (!is_wrench_filled(dx + 1, dy + 1)) {
                  it.draw_pixel_at(bx + dx + 1, by + dy + 1, col_black);
                }
                it.draw_pixel_at(bx + dx, by + dy, c);
              }
            }
          }
        }
      } else if (app_id == 3) {
        // Games: Solid Filled Gamepad (Center bridge raised 2px to dy=+2 + larger negative-space
        // 11x11px D-pad '+' and 3x3px 4-dot diamond cutouts to strip away excess white bulk!)
        // dy spans -12 .. +12 (25 rows total: rows 0..14 = main body; rows 15..24 = lower grip wings)
        static constexpr int8_t GP_OUT_MAX[25] = {
           9, 11, 12, 13, 13, 14, 14, 15, 15, 15, 16, 16, 16, 17, 17, 17, 18, 18, 18, 18, 17, 17, 16, 15, 13
        };
        static constexpr int8_t GP_OUT_MIN[25] = {
           0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  6,  8,  9,  9, 10, 10, 11, 11, 12, 12
        };

        auto is_gamepad_filled = [](int dx, int dy) -> bool {
          if (dy < -12 || dy > 12) return false;
          int row = dy + 12;
          int ax = std::abs(dx);
          if (ax < GP_OUT_MIN[row] || ax > GP_OUT_MAX[row]) return false;

          // Enlarged Negative-Space Cutout 1: 11x11px Left D-Pad Cross (+) centered at (dx = -7, dy = -4)
          int dpx = dx + 7;
          int dpy = dy + 4;
          if ((std::abs(dpx) <= 1 && std::abs(dpy) <= 5) ||
              (std::abs(dpy) <= 1 && std::abs(dpx) <= 5)) {
            return false;
          }

          // Enlarged Negative-Space Cutout 2: Right 4-Dot Action Diamond centered at (dx = +7, dy = -4)
          // Each button dot is r^2 <= 2 (a crisp 5-pixel / 3x3 cross pip) spaced at ±4px!
          int abx = dx - 7;
          int aby = dy + 4;
          auto is_dot = [](int x, int y, int cx_d, int cy_d) -> bool {
            int rx = x - cx_d, ry = y - cy_d;
            return (rx * rx + ry * ry) <= 2;
          };
          if (is_dot(abx, aby, 0, -4) || is_dot(abx, aby, 0, 4) ||
              is_dot(abx, aby, -4, 0) || is_dot(abx, aby, 4, 0)) {
            return false;
          }

          return true;
        };

        if (!is_shadow) {
          for (int dy = -12; dy <= 12; dy++) {
            for (int dx = -19; dx <= 19; dx++) {
              if (is_gamepad_filled(dx, dy)) {
                if (!is_gamepad_filled(dx + 1, dy + 1)) {
                  it.draw_pixel_at(bx + dx + 1, by + dy + 1, col_black);
                }
                it.draw_pixel_at(bx + dx, by + dy, c);
              }
            }
          }
        }
      } else if (app_id == 4) {
        // System Settings: Enlarged 8-Sprocket Gear Icon scaled for r = 29 dock button
        auto is_dock_gear = [](int dx, int dy) -> bool {
          int r2 = dx * dx + dy * dy;
          if (r2 <= 24 || r2 > 232) return false;
          if (r2 <= 128) return true;
          float fx = static_cast<float>(dx);
          float fy = static_cast<float>(dy);
          static constexpr float INV_SQRT2 = 0.70710678f;
          float p_v  = std::fabs(fx),                      a_v  = std::fabs(fy);
          float p_h  = std::fabs(fy),                      a_h  = std::fabs(fx);
          float p_d1 = std::fabs((fx - fy) * INV_SQRT2),   a_d1 = std::fabs((fx + fy) * INV_SQRT2);
          float p_d2 = std::fabs((fx + fy) * INV_SQRT2),   a_d2 = std::fabs((fx - fy) * INV_SQRT2);
          auto in_tooth = [](float perp, float along) -> bool {
            if (along > 15.0f) return false;
            float max_w = 2.65f - (along - 11.0f) * 0.20f;
            return (perp <= max_w);
          };
          return in_tooth(p_v, a_v) || in_tooth(p_h, a_h) ||
                 in_tooth(p_d1, a_d1) || in_tooth(p_d2, a_d2);
        };
        if (!is_shadow) {
          for (int dy = -16; dy <= 16; dy++) {
            for (int dx = -16; dx <= 16; dx++) {
              if (is_dock_gear(dx, dy)) {
                if (!is_dock_gear(dx + 1, dy + 1)) {
                  it.draw_pixel_at(bx + dx + 1, by + dy + 1, col_black);
                }
                it.draw_pixel_at(bx + dx, by + dy, c);
              }
            }
          }
        }
      } else if (app_id == 9) {
        // Whole-Home Hub (Alarm, Thermostat, Garage, Vacuum): Solid Pitched-Roof House Silhouette
        it.filled_triangle(bx, by - 15, bx - 15, by - 2, bx + 15, by - 2, c);
        it.filled_rectangle(bx - 11, by - 2, 23, 15, c);
        if (!is_shadow) {
          Color cut_c = (show_weather_bg && !is_night) ? Color(28, 54, 84) : col_black;
          if (home_alarm_state.find("armed_away") != std::string::npos || home_alarm_state == "triggered") {
            cut_c = Color(235, 65, 65);
          } else if (home_alarm_state.find("armed") != std::string::npos || home_alarm_state == "arming") {
            cut_c = col_gold;
          }
          it.filled_circle(bx, by + 3, 4, cut_c);
          it.filled_rectangle(bx - 2, by + 7, 5, 3, cut_c);
        }
      }
    };

    const Color dock_ring_c = is_dim_backdrop ? Color(18, 22, 28) : Color(95, 118, 148);
    const Color dock_fg_c   = is_dim_backdrop ? Color(34, 38, 44) : col_white;
    for (int i = 0; i < vis_count; i++) {
      int bx = win_xs[i];
      int app_id = dock_apps[start_idx + i];
      if (!is_dim_backdrop) {
        it.circle(bx + 1, dock_y + 1, dock_r, col_black);
      }
      it.circle(bx, dock_y, dock_r, dock_ring_c);
      if (!is_dim_backdrop) {
        draw_dock_glyph(app_id, bx + 1, dock_y + 1, col_black, true);
      }
      draw_dock_glyph(app_id, bx, dock_y, dock_fg_c, false);
    }

    // Page Indicator Pips at y = 405 (17px clear gap below dock circles, 19px clear gap above battery)
    render_dock_page_pips(it, cx, 405, total_dock_pages, home_dock_page(),
                          is_dim_backdrop ? dock_fg_c : col_white,
                          is_dim_backdrop ? dock_ring_c : Color(95, 112, 134));
  }

  // ZONE 6 (chin_y = 435): Lowered Non-Clickable Battery Indicator in Standard Gray (theme_secondary)
  render_battery_indicator(it, cx, 435, batt, is_charging, font_small,
                           is_dim_backdrop ? Color(30, 34, 40) : theme_secondary, col_gold, col_green);

  // ---------------------------------------------------------------------------
  // SUBPAGE 2: TRANSLUCENT AUTO-CENTERED LIVE ACTIVITY OVERLAY
  //   - Background Home elements are already rendered in smooth ~14% ghost tones (zero interlace stripes!)
  //   - No title; auto-centers 1..5 standard System-sized cards (w = 276, h = 44, r = 12, 63px stride = 19px gap!)
  //   - Supports Page 1 / Page 2 via right-flank ▲ / ▼ chevron if >5 items are active!
  // ---------------------------------------------------------------------------
  if (home_subpage == 2) {
    // Clear bottom chin behind "< Back" so it replaces the battery/gear cleanly
    it.filled_rectangle(130, 404, 206, 44, show_weather_bg && !is_night ? Color(4, 8, 13) : col_black);

    Color ov_bg(22, 27, 36);
    Color ov_brd(52, 62, 78);
    Color ov_btn(32, 39, 52);

    if (act_count <= 0) {
      it.print(cx, 236, font_small, col_slate, TextAlign::CENTER, "No active activities");
    } else {
      int total_pages = (act_count > 5) ? 2 : 1;
      if (home_activity_overlay_page() >= total_pages) home_activity_overlay_page() = 0;
      if (home_activity_overlay_page() < 0) home_activity_overlay_page() = 0;

      int start_i = home_activity_overlay_page() * 5;
      int vis_cnt = std::min(5, act_count - start_i);

      for (int row = 0; row < vis_cnt; row++) {
        int y_mid = get_activity_overlay_card_y(vis_cnt, row);
        render_home_activity_card_row(
            it, cx, y_mid, act_items[start_i + row], false, font_small,
            theme_secondary, col_white, true, ov_bg, ov_brd, ov_btn);
      }

      if (total_pages > 1) {
        if (home_activity_overlay_page() > 0) {
          render_scroll_chevron(it, 415, 162, true, theme_secondary, col_black, true);
        }
        if (home_activity_overlay_page() + 1 < total_pages) {
          render_scroll_chevron(it, 415, 278, false, theme_secondary, col_black, true);
        }
      }
    }

    render_back_button(it, cx, font_small, 426, "< Back");
  }
}

}  // namespace pocket_ui
