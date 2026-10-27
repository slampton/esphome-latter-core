#pragma once

// ==============================================================================
// Latter Core UI Module: Universal 4-Slot TV & Media Remote (ui_remote.h)
// ==============================================================================

#include "ui_layout.h"

namespace pocket_ui {

// =============================================================================
// SECTION 17: UNIVERSAL TV & MEDIA REMOTE ENGINE (LATTER v1.0.3 ZERO-YAML SYNC)
// =============================================================================

inline bool is_configured_remote_slot(const std::string &ent) {
  if (ent.empty()) return false;
  return ent != "none" && ent != "remote.none" && ent != "media_player.none";
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
  // Uppercase common AV acronyms (TV, LG, AVR)
  auto replace_word = [&](const std::string &from, const std::string &to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
      bool left_ok = (pos == 0 || s[pos - 1] == ' ');
      bool right_ok = (pos + from.length() == s.length() || s[pos + from.length()] == ' ');
      if (left_ok && right_ok) {
        s.replace(pos, from.length(), to);
      }
      pos += to.length();
    }
  };
  replace_word("Tv", "TV");
  replace_word("Lg", "LG");
  replace_word("Avr", "AVR");
  return s;
}

inline const char *detect_remote_profile(const std::string &ent, const std::string &name,
                                         const char *default_profile = "Android TV / Fire TV / Bravia") {
  std::string s = ent + " " + name;
  for (char &c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
  if (s.find("marantz") != std::string::npos || s.find("denon") != std::string::npos ||
      s.find("cinema") != std::string::npos || s.find("avr") != std::string::npos) {
    return "Denon / Marantz AVR";
  }
  if (s.find("webos") != std::string::npos || s.find("lg_tv") != std::string::npos ||
      s.find("lg tv") != std::string::npos || s.find("lgtv") != std::string::npos) {
    return "LG webOS";
  }
  if (s.find("apple") != std::string::npos) {
    return "Apple TV";
  }
  if (s.find("roku") != std::string::npos) {
    return "Roku";
  }
  if (s.find("samsung") != std::string::npos || s.find("tizen") != std::string::npos) {
    return "Samsung TV";
  }
  if (s.find("shield") != std::string::npos || s.find("android") != std::string::npos ||
      s.find("fire") != std::string::npos || s.find("bravia") != std::string::npos ||
      s.find("chromecast") != std::string::npos || s.find("google") != std::string::npos) {
    return "Android TV / Fire TV / Bravia";
  }
  if (ent.rfind("media_player.", 0) == 0) {
    return "Denon / Marantz AVR";
  }
  return default_profile;
}

// Parses Latter v1.0.3's `slots_summary` attribute ("1:Name:remote_ent:mp_ent:state;2:...")
// into the 4 slot entity and friendly-name globals.
inline bool *remote_slot_states_on() {
  static bool states[4] = {false, false, false, false};
  return states;
}

inline void parse_latter_remote_slots_summary(
    const std::string &summary,
    std::string &s0_ent, std::string &s0_name,
    std::string &s1_ent, std::string &s1_name,
    std::string &s2_ent, std::string &s2_name,
    std::string &s3_ent, std::string &s3_name) {
  std::string *ents[4]  = {&s0_ent,  &s1_ent,  &s2_ent,  &s3_ent};
  std::string *names[4] = {&s0_name, &s1_name, &s2_name, &s3_name};
  bool *st_on = remote_slot_states_on();
  for (int i = 0; i < 4; i++) {
    *ents[i] = "remote.none";
    names[i]->clear();
    st_on[i] = false;
  }
  if (summary.empty() || summary == "unknown" || summary == "unavailable" || summary == "none") {
    return;
  }
  size_t pos = 0;
  while (pos < summary.size()) {
    size_t semi = summary.find(';', pos);
    std::string entry = (semi == std::string::npos) ? summary.substr(pos) : summary.substr(pos, semi - pos);
    pos = (semi == std::string::npos) ? summary.size() : (semi + 1);
    if (entry.empty()) continue;

    // Format: slot_num:Name:remote_ent:mp_ent:state
    size_t c1 = entry.find(':');
    if (c1 == std::string::npos) continue;
    int slot_num = std::atoi(entry.substr(0, c1).c_str());
    if (slot_num < 1 || slot_num > 4) continue;
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

    st_on[idx] = (st_str == "on" || st_str == "playing" || st_str == "paused" || st_str == "buffering" || st_str == "idle");

    // Prefer remote.* entity if present; otherwise media_player.* entity
    std::string best_ent = (!rem_e.empty() && rem_e != "none" && rem_e != "None") ? rem_e : mp_e;
    if (!best_ent.empty() && best_ent != "none" && best_ent != "None") {
      *ents[idx] = best_ent;
      *names[idx] = name.empty() ? format_remote_entity_fallback(best_ent) : name;
    }
  }
}

inline int collect_configured_remote_slots(const std::string ents[4], int out_slots[4]) {
  int count = 0;
  for (int i = 0; i < 4; i++) {
    if (is_configured_remote_slot(ents[i])) {
      out_slots[count++] = i;
    }
  }
  return count;
}

inline int resolve_active_remote_slot(int active_slot, const std::string ents[4]) {
  if (active_slot >= 0 && active_slot < 4 && is_configured_remote_slot(ents[active_slot])) {
    return active_slot;
  }
  for (int i = 0; i < 4; i++) {
    if (is_configured_remote_slot(ents[i])) return i;
  }
  return 0;
}

inline bool &avr_setup_menu_open() {
  static bool open = false;
  return open;
}

struct RemoteCustomCmds {
  const char *up;
  const char *down;
  const char *left;
  const char *right;
  const char *select;
  const char *back;
  const char *home;
  const char *vol_up;
  const char *vol_down;
  const char *power;
};

inline void resolve_remote_dispatch(
    int key_idx, int target_slot,
    const std::string slot_profiles[4],
    const std::string ents[4], const std::string names[4],
    const RemoteCustomCmds &custom_cmds,
    std::string &out_ent, std::string &out_prof,
    std::string &out_cmd, std::string &out_feedback) {
  static constexpr const char *kFeedback[10] = {
    "Up", "Down", "Left", "Right", "Select",
    "Back", "Home", "Vol +", "Vol -", "Toggle Power"
  };
  int idx = (key_idx >= 0 && key_idx < 10) ? key_idx : 0;
  int resolved_slot = 0;
  std::string dev_name;
  if (target_slot == -1) {
    resolved_slot = resolve_active_remote_slot(0, ents);
    out_ent = ents[resolved_slot];
    dev_name = names[resolved_slot].empty() ? format_remote_entity_fallback(out_ent) : names[resolved_slot];
    out_feedback = "Power: " + dev_name;
  } else {
    resolved_slot = resolve_active_remote_slot(target_slot, ents);
    out_ent = ents[resolved_slot];
    dev_name = names[resolved_slot].empty() ? format_remote_entity_fallback(out_ent) : names[resolved_slot];
    out_feedback = (idx == 9) ? ("Power: " + dev_name) : kFeedback[idx];
  }

  const std::string &cfg_prof = slot_profiles[resolved_slot];
  if (!cfg_prof.empty() && cfg_prof != "Auto-Detect") {
    out_prof = cfg_prof;
  } else {
    out_prof = detect_remote_profile(out_ent, dev_name, "Android TV / Fire TV / Bravia");
  }

  if (out_prof == "Denon / Marantz AVR") {
    static constexpr const char *kAvrCmds[10] = {
      "/goform/formiPhoneAppDirect.xml?MNCUP",
      "/goform/formiPhoneAppDirect.xml?MNCDN",
      "/goform/formiPhoneAppDirect.xml?MNCLT",
      "/goform/formiPhoneAppDirect.xml?MNCRT",
      "/goform/formiPhoneAppDirect.xml?MNENT",
      "/goform/formiPhoneAppDirect.xml?MNRTN",
      "/goform/formiPhoneAppDirect.xml?MNMEN%20ON",
      "VOLUME_UP", "VOLUME_DOWN", "POWER"
    };
    if (idx == 6) {
      bool &open = avr_setup_menu_open();
      open = !open;
      out_cmd = open ? "/goform/formiPhoneAppDirect.xml?MNMEN%20ON"
                     : "/goform/formiPhoneAppDirect.xml?MNMEN%20OFF";
    } else {
      out_cmd = kAvrCmds[idx];
    }
    return;
  }

  if (out_prof == "LG webOS") {
    static constexpr const char *kWebosBtns[10] = {
      "UP", "DOWN", "LEFT", "RIGHT", "ENTER",
      "BACK", "HOME", "VOLUMEUP", "VOLUMEDOWN", "POWER"
    };
    out_cmd = kWebosBtns[idx];
    return;
  }

  struct RemoteCmdRow {
    const char *apple_cmd;
    const char *roku_cmd;
    const char *samsung_cmd;
    const char *custom_cmd;
    const char *android_cmd;
  };
  const RemoteCmdRow kCmds[10] = {
    {"up",          "up",          "KEY_UP",      custom_cmds.up,       "DPAD_UP"},
    {"down",        "down",        "KEY_DOWN",    custom_cmds.down,     "DPAD_DOWN"},
    {"left",        "left",        "KEY_LEFT",    custom_cmds.left,     "DPAD_LEFT"},
    {"right",       "right",       "KEY_RIGHT",   custom_cmds.right,    "DPAD_RIGHT"},
    {"select",      "select",      "KEY_ENTER",   custom_cmds.select,   "DPAD_CENTER"},
    {"menu",        "back",        "KEY_RETURN",  custom_cmds.back,     "BACK"},
    {"top_menu",    "home",        "KEY_HOME",    custom_cmds.home,     "HOME"},
    {"volume_up",   "volume_up",   "KEY_VOLUP",   custom_cmds.vol_up,   "VOLUME_UP"},
    {"volume_down", "volume_down", "KEY_VOLDOWN", custom_cmds.vol_down, "VOLUME_DOWN"},
    {"wakeup",      "power",       "KEY_POWER",   custom_cmds.power,    "POWER"}
  };
  if (out_prof == "Apple TV") out_cmd = kCmds[idx].apple_cmd;
  else if (out_prof == "Roku") out_cmd = kCmds[idx].roku_cmd;
  else if (out_prof == "Samsung TV") out_cmd = kCmds[idx].samsung_cmd;
  else if (out_prof == "Custom") out_cmd = kCmds[idx].custom_cmd;
  else out_cmd = kCmds[idx].android_cmd;
}

template <typename DisplayT, typename FontT>
inline void render_tv_remote_page(
    DisplayT &it, int cx, int cy, uint32_t now_ms,
    int remote_subpage, int &active_remote_slot,
    const std::string ents[4], const std::string names[4],
    const std::string &remote_action_feedback, uint32_t remote_feedback_timer,
    FontT *font_date, FontT *font_small,
    const esphome::Color &theme_primary, const esphome::Color &theme_secondary,
    const esphome::Color &col_white, const esphome::Color &col_slate,
    const esphome::Color &col_gray, const esphome::Color &col_dark_gray,
    const esphome::Color &col_red) {
  int slot = resolve_active_remote_slot(active_remote_slot, ents);
  active_remote_slot = slot;

  std::string rem_name = names[slot];
  if (rem_name.empty()) {
    rem_name = format_remote_entity_fallback(ents[slot]);
    if (rem_name.empty()) rem_name = "TV Remote";
  }

  bool is_fb = (now_ms - remote_feedback_timer < 1500U && !remote_action_feedback.empty());

  if (remote_subpage == 1) {
    // -------------------------------------------------------------------------
    // SUBPAGE 1: CONTROL (MASTER POWER + UP TO 4 PRECONFIGURED DEVICES)
    // -------------------------------------------------------------------------
    if (is_fb) {
      std::string fit_fb = fit_text_to_width_font(remote_action_feedback, 280, font_small);
      it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, fit_fb.c_str());
    } else {
      it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, "CONTROL");
    }

    const esphome::Color card_bg(22, 27, 36);
    const esphome::Color card_brd(52, 62, 78);

    // Slot 0 (y = 110): Master System Power Card (w = 276, h = 44, r = 12 -> outer edges x = 95..371)
    render_modern_card(it, cx, 110, 276, 44, 12, esphome::Color(28, 18, 22), col_red,
                       "Toggle Power", col_white, font_small);

    // Slots 1..4 (y = 173, 236, 299, 362):
    //   - Row 0 (Slot 1 Primary Device): Full 276px width (cx_card = 233, w = 276 -> outer edges x = 95..371)
    //   - Rows 1..3 (Secondary Devices): 226px width (cx_card = 208, w = 226 -> outer edges x = 95..321) + Power Circle at 353
    int valid_slots[4] = {0, 0, 0, 0};
    int valid_count = collect_configured_remote_slots(ents, valid_slots);
    for (int row = 0; row < valid_count; row++) {
      int s_idx = valid_slots[row];
      int y_mid = 173 + row * 63;
      bool is_sel = (s_idx == slot);

      std::string d_name = names[s_idx];
      if (d_name.empty()) d_name = format_remote_entity_fallback(ents[s_idx]);

      esphome::Color border_c = is_sel ? theme_primary : card_brd;
      esphome::Color text_c = is_sel ? theme_primary : col_white;

      int card_cx = (row == 0) ? cx  : 208;
      int card_w  = (row == 0) ? 276 : 226;
      int max_txt = (row == 0) ? 228 : 182;

      render_modern_card(it, card_cx, y_mid, card_w, 44, 12, card_bg, border_c, nullptr, text_c, font_small);
      if (is_sel) {
        it.filled_circle(114, y_mid, 6, theme_primary);
      } else {
        it.circle(114, y_mid, 5, col_slate);
      }
      std::string fit_lbl = fit_text_to_width_font(d_name, max_txt, font_small);
      it.print(130, y_mid, font_small, text_c, esphome::display::TextAlign::CENTER_LEFT, fit_lbl.c_str());

      if (row > 0) {
        // Secondary Devices (Slots 2..4): Individual Override Power Button right-aligned with Toggle Power
        // Top Toggle Power right edge = 371; p_cx = 353 (r = 18) aligns right outer edge at 371 with a 14px gap!
        const int p_cx = 353;
        const bool is_dev_on = remote_slot_states_on()[s_idx];
        it.filled_circle(p_cx, y_mid, 18, card_bg);
        it.circle(p_cx, y_mid, 18, is_dev_on ? theme_secondary : col_red);
        for (int i = 6; i < 54; i += 2) {
          int x1 = p_cx + static_cast<int>(7.0f * DIAL_SIN[i]);
          int y1 = y_mid - static_cast<int>(7.0f * DIAL_COS[i]);
          int x2 = p_cx + static_cast<int>(7.0f * DIAL_SIN[i + 2]);
          int y2 = y_mid - static_cast<int>(7.0f * DIAL_COS[i + 2]);
          it.line(x1, y1, x2, y2, col_white);
        }
        it.filled_rectangle(p_cx - 1, y_mid - 9, 3, 8, col_white);
      }
    }

    // Standardized Centered < BACK Button at y = 410
    render_back_button(it, cx, font_small, 426, "< BACK");
    return;
  }

  // ---------------------------------------------------------------------------
  // SUBPAGE 0: MAIN REMOTE CONTROL INTERFACE (Title-Case Device Name & Feedback)
  // ---------------------------------------------------------------------------
  if (is_fb) {
    std::string fit_fb = fit_text_to_width_font(remote_action_feedback, 280, font_small);
    it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, fit_fb.c_str());
  } else {
    std::string disp_title = fit_text_to_width_font(rem_name, 280, font_small);
    it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, disp_title.c_str());
  }

  // Flank Buttons Symmetrical Coordinates (x = 64 / 402, y = 134 / 332, r_dist = 196px from center)
  // Positioned cleanly outside the enlarged r_out = 144 D-pad ring!
  const int left_fx = POCKET_UI_ROUND_DISPLAY ? 64 : 46;
  const int right_fx = SCREEN_W - left_fx;
  const int top_fy = 134;
  const int btm_fy = 332;
  const int f_r = 19;

  // 1. HOME: Top-Left (64, 134)
  it.filled_circle(left_fx, top_fy, f_r, col_dark_gray);
  it.circle(left_fx, top_fy, f_r, col_gray);
  it.filled_triangle(left_fx, top_fy - 8, left_fx - 9, top_fy - 1, left_fx + 9, top_fy - 1, col_white);
  it.filled_rectangle(left_fx - 6, top_fy - 1, 13, 8, col_white);
  it.filled_rectangle(left_fx - 2, top_fy + 2, 5, 5, col_dark_gray);

  // 2. VOL UP: Top-Right (402, 134)
  it.filled_circle(right_fx, top_fy, f_r, col_dark_gray);
  it.circle(right_fx, top_fy, f_r, col_gray);
  it.filled_rectangle(right_fx - 7, top_fy - 1, 15, 3, col_white);
  it.filled_rectangle(right_fx - 1, top_fy - 7, 3, 15, col_white);

  // 3. BACK: Bottom-Left (64, 332)
  it.filled_circle(left_fx, btm_fy, f_r, col_dark_gray);
  it.circle(left_fx, btm_fy, f_r, col_gray);
  it.line(left_fx + 3, btm_fy - 7, left_fx - 4, btm_fy, col_white);
  it.line(left_fx + 4, btm_fy - 7, left_fx - 3, btm_fy, col_white);
  it.line(left_fx - 4, btm_fy, left_fx + 3, btm_fy + 7, col_white);
  it.line(left_fx - 3, btm_fy, left_fx + 4, btm_fy + 7, col_white);

  // 4. VOL DOWN: Bottom-Right (402, 332)
  it.filled_circle(right_fx, btm_fy, f_r, col_dark_gray);
  it.circle(right_fx, btm_fy, f_r, col_gray);
  it.filled_rectangle(right_fx - 7, btm_fy - 1, 15, 3, col_white);

  // Enlarged Concentric D-Pad (r_out = 144 -> 288px diameter, r_in = 50)
  const int dpad_r_in = 50;
  const int dpad_r_out = 144;
  it.circle(cx, cy, dpad_r_out, col_gray);
  it.circle(cx, cy, dpad_r_in, col_gray);

  // 45-degree sector dividers (35px .. 102px)
  const int d_in = 35;
  const int d_out = 102;
  it.line(cx + d_in, cy + d_in, cx + d_out, cy + d_out, col_gray);
  it.line(cx - d_in, cy + d_in, cx - d_out, cy + d_out, col_gray);
  it.line(cx + d_in, cy - d_in, cx + d_out, cy - d_out, col_gray);
  it.line(cx - d_in, cy - d_in, cx - d_out, cy - d_out, col_gray);

  // Directional Arrows (shifted out to +/- 105px)
  it.filled_triangle(cx, cy - 105, cx - 11, cy - 89, cx + 11, cy - 89, col_white);
  it.filled_triangle(cx, cy + 105, cx - 11, cy + 89, cx + 11, cy + 89, col_white);
  it.filled_triangle(cx - 105, cy, cx - 89, cy - 11, cx - 89, cy + 11, col_white);
  it.filled_triangle(cx + 105, cy, cx + 89, cy - 11, cx + 89, cy + 11, col_white);

  // Center OK Button (Radius 42)
  it.filled_circle(cx, cy, 42, col_dark_gray);
  it.circle(cx, cy, 42, theme_primary);
  it.print(cx, cy, font_date, col_white, esphome::display::TextAlign::CENTER, "OK");
}

}  // namespace pocket_ui
