#pragma once

// ==============================================================================
// Latter Core UI Module: Universal 10-Slot TV & Media Remote (ui_remote.h)
// ==============================================================================

#include "ui_layout.h"

namespace latter_ui {

// =============================================================================
// SECTION 17: UNIVERSAL TV & MEDIA REMOTE ENGINE (UP TO 10 SCROLLABLE SLOTS)
// =============================================================================

static constexpr int MAX_REMOTE_SLOTS = 10;

inline std::string *remote_slots_ent() {
  static std::string ents[MAX_REMOTE_SLOTS] = {
      "remote.none", "remote.none", "remote.none", "remote.none", "remote.none",
      "remote.none", "remote.none", "remote.none", "remote.none", "remote.none"};
  return ents;
}

inline std::string *remote_slots_name() {
  static std::string names[MAX_REMOTE_SLOTS];
  return names;
}

inline bool *remote_slot_states_on() {
  static bool states[MAX_REMOTE_SLOTS] = {false};
  return states;
}

inline int &remote_control_page() {
  static int pg = 0;
  return pg;
}

inline bool is_configured_remote_slot(const std::string &ent) {
  if (ent.empty()) return false;
  return ent != "none" && ent != "NONE" && ent != "remote.none" && ent != "media_player.none";
}

inline bool has_any_configured_remote() {
  const std::string *t_ents = remote_slots_ent();
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    if (is_configured_remote_slot(t_ents[i])) return true;
  }
  return false;
}

inline std::string format_remote_entity_fallback(const std::string &ent) {
  if (!is_configured_remote_slot(ent)) return "";
  std::string s = ent;
  size_t dot = s.find('.');
  if (dot != std::string::npos) s = s.substr(dot + 1);
  bool cap = true;
  for (size_t i = 0; i < s.length(); i++) {
    if (s[i] == '_') {
      s[i] = ' ';
      cap = true;
    } else if (cap) {
      s[i] = static_cast<char>(::toupper(static_cast<unsigned char>(s[i])));
      cap = false;
    }
  }
  return s;
}

// Parses Latter's `slots_summary` attribute ("1:Name:remote_ent:mp_ent:state;2:...")
// directly into the 10-slot table.
inline void parse_latter_remote_slots_summary(const std::string &summary) {
  std::string *t_ents = remote_slots_ent();
  std::string *t_names = remote_slots_name();
  bool *st_on = remote_slot_states_on();
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    t_ents[i] = "remote.none";
    t_names[i].clear();
    st_on[i] = false;
  }

  if (!summary.empty() && summary != "unknown" && summary != "unavailable" &&
      summary != "none" && summary != "NONE") {
    size_t pos = 0;
    while (pos < summary.size()) {
      size_t semi = summary.find(';', pos);
      std::string entry = (semi == std::string::npos) ? summary.substr(pos) : summary.substr(pos, semi - pos);
      pos = (semi == std::string::npos) ? summary.size() : (semi + 1);
      if (entry.empty()) continue;

      size_t c1 = entry.find(':');
      if (c1 == std::string::npos) continue;
      int slot_num = std::atoi(entry.substr(0, c1).c_str());
      if (slot_num < 1 || slot_num > MAX_REMOTE_SLOTS) continue;
      int idx = slot_num - 1;

      size_t c2 = entry.find(':', c1 + 1);
      if (c2 == std::string::npos) continue;
      std::string name = entry.substr(c1 + 1, c2 - (c1 + 1));

      size_t c3 = entry.find(':', c2 + 1);
      std::string rem_e = (c3 == std::string::npos) ? entry.substr(c2 + 1) : entry.substr(c2 + 1, c3 - (c2 + 1));
      std::string mp_e = "";
      std::string st_str = "";
      if (c3 != std::string::npos) {
        size_t c4 = entry.find(':', c3 + 1);
        mp_e = (c4 == std::string::npos) ? entry.substr(c3 + 1) : entry.substr(c3 + 1, c4 - (c3 + 1));
        if (c4 != std::string::npos) {
          st_str = entry.substr(c4 + 1);
        }
      }

      st_on[idx] = (st_str == "on" || st_str == "playing" || st_str == "paused" ||
                    st_str == "buffering" || st_str == "idle");

      std::string best_ent = (!rem_e.empty() && rem_e != "none" && rem_e != "None") ? rem_e : mp_e;
      if (!best_ent.empty() && best_ent != "none" && best_ent != "None") {
        t_ents[idx] = best_ent;
        t_names[idx] = name.empty() ? format_remote_entity_fallback(best_ent) : name;
      }
    }
  }
  remote_control_page() = 0;
}

inline int collect_configured_remote_slots(int out_slots[MAX_REMOTE_SLOTS]) {
  const std::string *t_ents = remote_slots_ent();
  int count = 0;
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    if (is_configured_remote_slot(t_ents[i])) {
      out_slots[count++] = i;
    }
  }
  return count;
}

inline int resolve_active_remote_slot(int active_slot) {
  const std::string *t_ents = remote_slots_ent();
  if (active_slot >= 0 && active_slot < MAX_REMOTE_SLOTS && is_configured_remote_slot(t_ents[active_slot])) {
    return active_slot;
  }
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    if (is_configured_remote_slot(t_ents[i])) return i;
  }
  return 0;
}

template <typename DisplayT, typename FontT>
inline void render_tv_remote_page(
    DisplayT &it, int cx, int cy, uint32_t now_ms,
    int remote_subpage, int &active_remote_slot,
    const std::string &remote_action_feedback, uint32_t remote_feedback_timer,
    FontT *font_date, FontT *font_small,
    const esphome::Color &theme_primary, const esphome::Color &theme_secondary,
    const esphome::Color &col_white, const esphome::Color &col_slate,
    const esphome::Color &col_gray, const esphome::Color &col_dark_gray,
    const esphome::Color &col_red) {
  const std::string *t_ents = remote_slots_ent();
  const std::string *t_names = remote_slots_name();

  int slot = resolve_active_remote_slot(active_remote_slot);
  active_remote_slot = slot;

  std::string rem_name = t_names[slot];
  if (rem_name.empty()) {
    rem_name = format_remote_entity_fallback(t_ents[slot]);
    if (rem_name.empty()) rem_name = "TV Remote";
  }

  bool is_fb = (now_ms - remote_feedback_timer < 1500U && !remote_action_feedback.empty());

  if (remote_subpage == 1) {
    // -------------------------------------------------------------------------
    // SUBPAGE 1: CONTROL (PINNED MASTER POWER + UP TO 10 SCROLLABLE DEVICES)
    // -------------------------------------------------------------------------
    int valid_slots[MAX_REMOTE_SLOTS] = {0};
    int valid_count = collect_configured_remote_slots(valid_slots);
    int total_pages = std::max(1, (valid_count + 3) / 4);
    if (remote_control_page() < 0 || remote_control_page() >= total_pages) {
      remote_control_page() = 0;
    }
    int cur_page = remote_control_page();

    if (is_fb) {
      std::string fit_fb = fit_text_to_width_font(remote_action_feedback, 280, font_small);
      it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, fit_fb.c_str());
    } else if (total_pages > 1) {
      char hdr_buf[32];
      snprintf(hdr_buf, sizeof(hdr_buf), "CONTROL  %d/%d", cur_page + 1, total_pages);
      it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, hdr_buf);
    } else {
      it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, "CONTROL");
    }

    const esphome::Color card_bg(22, 27, 36);
    const esphome::Color card_brd(52, 62, 78);

    render_modern_card(it, cx, layout().slot5_y(0), 276, 44, 12, esphome::Color(28, 18, 22), col_red,
                       "Toggle Power", col_white, font_small);

    int start_idx = cur_page * 4;
    int vis_count = std::min(4, std::max(0, valid_count - start_idx));
    for (int row = 0; row < vis_count; row++) {
      int s_idx = valid_slots[start_idx + row];
      int y_mid = layout().slot5_y(row + 1);
      bool is_sel = (s_idx == slot);
      bool is_primary_slot = (s_idx == valid_slots[0]);

      std::string d_name = t_names[s_idx];
      if (d_name.empty()) d_name = format_remote_entity_fallback(t_ents[s_idx]);

      esphome::Color border_c = is_sel ? theme_primary : card_brd;
      esphome::Color text_c = is_sel ? theme_primary : col_white;

      int card_cx = is_primary_slot ? cx  : (cx - scale_x(25));
      int card_w  = is_primary_slot ? 276 : 226;
      int max_txt = is_primary_slot ? scale_x(228) : scale_x(182);

      render_modern_card(it, card_cx, y_mid, card_w, 44, 12, card_bg, border_c, nullptr, text_c, font_small);
      if (is_sel) {
        it.filled_circle(cx - scale_x(119), y_mid, scale_s(6), theme_primary);
      } else {
        it.circle(cx - scale_x(119), y_mid, scale_s(5), col_slate);
      }
      std::string fit_lbl = fit_text_to_width_font(d_name, max_txt, font_small);
      it.print(cx - scale_x(103), y_mid, font_small, text_c, esphome::display::TextAlign::CENTER_LEFT, fit_lbl.c_str());

      if (!is_primary_slot) {
        const int p_cx = cx + scale_x(120);
        const int p_r  = scale_s(18);
        const bool is_dev_on = remote_slot_states_on()[s_idx];
        it.filled_circle(p_cx, y_mid, p_r, card_bg);
        it.circle(p_cx, y_mid, p_r, is_dev_on ? theme_secondary : col_red);
        for (int i = 6; i < 54; i += 2) {
          int x1 = p_cx + static_cast<int>(scale_s(7) * DIAL_SIN[i]);
          int y1 = y_mid - static_cast<int>(scale_s(7) * DIAL_COS[i]);
          int x2 = p_cx + static_cast<int>(scale_s(7) * DIAL_SIN[i + 2]);
          int y2 = y_mid - static_cast<int>(scale_s(7) * DIAL_COS[i + 2]);
          it.line(x1, y1, x2, y2, col_white);
        }
        it.filled_rectangle(p_cx - 1, y_mid - scale_s(9), 3, scale_s(8), col_white);
      }
    }

    if (total_pages > 1) {
      render_vertical_page_pips(it, cur_page > 0, cur_page + 1 < total_pages,
                                esphome::Color(0, 0, 0), theme_secondary, col_slate);
    }

    render_back_button(it, cx, font_small, 426, "< BACK");
    return;
  }

  // ---------------------------------------------------------------------------
  // SUBPAGE 0: MAIN REMOTE CONTROL INTERFACE
  // ---------------------------------------------------------------------------
  if (is_fb) {
    std::string fit_fb = fit_text_to_width_font(remote_action_feedback, scale_x(280), font_small);
    it.print(cx, layout().hdr_y, font_small, theme_primary, esphome::display::TextAlign::CENTER, fit_fb.c_str());
  } else {
    std::string disp_title = fit_text_to_width_font(rem_name, scale_x(280), font_small);
    it.print(cx, layout().hdr_y, font_small, theme_primary, esphome::display::TextAlign::CENTER, disp_title.c_str());
  }

  const int dpad_cy  = LATTER_ROUND_DISPLAY ? cy : (cy + scale_y(8));
  const int left_fx  = LATTER_ROUND_DISPLAY ? 64 : ((SCREEN_W < 380) ? 34 : ((SCREEN_W > 520) ? 104 : ((SCREEN_W >= 480) ? 44 : 48)));
  const int right_fx = SCREEN_W - left_fx;
  const int flank_dy = LATTER_ROUND_DISPLAY ? 99 : ((SCREEN_W < 380) ? 128 : ((SCREEN_W > 520) ? 196 : ((SCREEN_W >= 480) ? 148 : 116)));
  const int top_fy   = dpad_cy - flank_dy;
  const int btm_fy   = dpad_cy + flank_dy;
  const int f_r      = scale_s(19);

  // 1. HOME: Top-Left
  it.filled_circle(left_fx, top_fy, f_r, col_dark_gray);
  it.circle(left_fx, top_fy, f_r, col_gray);
  it.filled_triangle(left_fx, top_fy - scale_s(8), left_fx - scale_s(9), top_fy - 1, left_fx + scale_s(9), top_fy - 1, col_white);
  it.filled_rectangle(left_fx - scale_s(6), top_fy - 1, scale_s(13), scale_s(8), col_white);
  it.filled_rectangle(left_fx - scale_s(2), top_fy + scale_s(2), scale_s(5), scale_s(5), col_dark_gray);

  // 2. VOL UP: Top-Right
  const int pls = scale_s(7);
  const int pth = scale_s(3);
  it.filled_circle(right_fx, top_fy, f_r, col_dark_gray);
  it.circle(right_fx, top_fy, f_r, col_gray);
  it.filled_rectangle(right_fx - pls, top_fy - pth / 2, pls * 2 + 1, pth, col_white);
  it.filled_rectangle(right_fx - pth / 2, top_fy - pls, pth, pls * 2 + 1, col_white);

  // 3. BACK: Bottom-Left
  it.filled_circle(left_fx, btm_fy, f_r, col_dark_gray);
  it.circle(left_fx, btm_fy, f_r, col_gray);
  it.line(left_fx + scale_s(3), btm_fy - scale_s(7), left_fx - scale_s(4), btm_fy, col_white);
  it.line(left_fx + scale_s(4), btm_fy - scale_s(7), left_fx - scale_s(3), btm_fy, col_white);
  it.line(left_fx - scale_s(4), btm_fy, left_fx + scale_s(3), btm_fy + scale_s(7), col_white);
  it.line(left_fx - scale_s(3), btm_fy, left_fx + scale_s(4), btm_fy + scale_s(7), col_white);

  // 4. VOL DOWN: Bottom-Right
  it.filled_circle(right_fx, btm_fy, f_r, col_dark_gray);
  it.circle(right_fx, btm_fy, f_r, col_gray);
  it.filled_rectangle(right_fx - pls, btm_fy - pth / 2, pls * 2 + 1, pth, col_white);

  // Adaptive Concentric D-Pad
  const int dpad_r_out = (SCREEN_W < 380) ? 128 : ((SCREEN_W > 520) ? 236 : ((!LATTER_ROUND_DISPLAY && SCREEN_W >= 480) ? 180 : 144));
  const int dpad_r_in  = (SCREEN_W < 380) ?  46 : ((SCREEN_W > 520) ?  86 : ((!LATTER_ROUND_DISPLAY && SCREEN_W >= 480) ?  60 :  50));
  const int ok_r       = dpad_r_in - scale_s(8);
  it.circle(cx, dpad_cy, dpad_r_out, col_gray);
  it.circle(cx, dpad_cy, dpad_r_in, col_gray);

  const int d_in  = (dpad_r_in * 707) / 1000;
  const int d_out = (dpad_r_out * 707) / 1000;
  it.line(cx + d_in, dpad_cy + d_in, cx + d_out, dpad_cy + d_out, col_gray);
  it.line(cx - d_in, dpad_cy + d_in, cx - d_out, dpad_cy + d_out, col_gray);
  it.line(cx + d_in, dpad_cy - d_in, cx + d_out, dpad_cy - d_out, col_gray);
  it.line(cx - d_in, dpad_cy - d_in, cx - d_out, dpad_cy - d_out, col_gray);

  const int arr_mid = (dpad_r_in + dpad_r_out) / 2;
  it.filled_triangle(cx, dpad_cy - arr_mid - 8, cx - 10, dpad_cy - arr_mid + 8, cx + 10, dpad_cy - arr_mid + 8, col_white);
  it.filled_triangle(cx, dpad_cy + arr_mid + 8, cx - 10, dpad_cy + arr_mid - 8, cx + 10, dpad_cy + arr_mid - 8, col_white);
  it.filled_triangle(cx - arr_mid - 8, dpad_cy, cx - arr_mid + 8, dpad_cy - 10, cx - arr_mid + 8, dpad_cy + 10, col_white);
  it.filled_triangle(cx + arr_mid + 8, dpad_cy, cx + arr_mid - 8, dpad_cy - 10, cx + arr_mid - 8, dpad_cy + 10, col_white);

  it.filled_circle(cx, dpad_cy, ok_r, col_dark_gray);
  it.circle(cx, dpad_cy, ok_r, theme_primary);
  it.print(cx, dpad_cy, font_date, col_white, esphome::display::TextAlign::CENTER, "OK");
}

}  // namespace latter_ui