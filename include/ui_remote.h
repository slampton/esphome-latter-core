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

inline void sync_legacy_remote_slots_into_table(const std::string legacy_ents[4],
                                                const std::string legacy_names[4]) {
  std::string *t_ents = remote_slots_ent();
  std::string *t_names = remote_slots_name();
  bool any_table = false;
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    if (is_configured_remote_slot(t_ents[i])) {
      any_table = true;
      break;
    }
  }
  if (!any_table && legacy_ents != nullptr) {
    for (int i = 0; i < 4; i++) {
      t_ents[i] = legacy_ents[i];
      if (legacy_names != nullptr) t_names[i] = legacy_names[i];
    }
  }
}

// Parses Latter's `slots_summary` attribute ("1:Name:remote_ent:mp_ent:state;2:...")
// into all 10 slot entries (while syncing the first 4 legacy globals).
inline void parse_latter_remote_slots_summary(
    const std::string &summary,
    std::string &s0_ent, std::string &s0_name,
    std::string &s1_ent, std::string &s1_name,
    std::string &s2_ent, std::string &s2_name,
    std::string &s3_ent, std::string &s3_name) {
  std::string *t_ents = remote_slots_ent();
  std::string *t_names = remote_slots_name();
  bool *st_on = remote_slot_states_on();
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    t_ents[i] = "remote.none";
    t_names[i].clear();
    st_on[i] = false;
  }

  if (!summary.empty() && summary != "unknown" && summary != "unavailable" && summary != "none") {
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

      st_on[idx] = (st_str == "on" || st_str == "playing" || st_str == "paused" || st_str == "buffering" || st_str == "idle");

      std::string best_ent = (!rem_e.empty() && rem_e != "none" && rem_e != "None") ? rem_e : mp_e;
      if (!best_ent.empty() && best_ent != "none" && best_ent != "None") {
        t_ents[idx] = best_ent;
        t_names[idx] = name.empty() ? format_remote_entity_fallback(best_ent) : name;
      }
    }
  }

  s0_ent = t_ents[0]; s0_name = t_names[0];
  s1_ent = t_ents[1]; s1_name = t_names[1];
  s2_ent = t_ents[2]; s2_name = t_names[2];
  s3_ent = t_ents[3]; s3_name = t_names[3];
  remote_control_page() = 0;
}

inline int collect_configured_remote_slots(const std::string legacy_ents[4], int out_slots[MAX_REMOTE_SLOTS]) {
  sync_legacy_remote_slots_into_table(legacy_ents, nullptr);
  const std::string *t_ents = remote_slots_ent();
  int count = 0;
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    if (is_configured_remote_slot(t_ents[i])) {
      out_slots[count++] = i;
    }
  }
  return count;
}

inline int resolve_active_remote_slot(int active_slot, const std::string legacy_ents[4]) {
  sync_legacy_remote_slots_into_table(legacy_ents, nullptr);
  const std::string *t_ents = remote_slots_ent();
  if (active_slot >= 0 && active_slot < MAX_REMOTE_SLOTS && is_configured_remote_slot(t_ents[active_slot])) {
    return active_slot;
  }
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    if (is_configured_remote_slot(t_ents[i])) return i;
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
    const std::string legacy_ents[4], const std::string legacy_names[4],
    const RemoteCustomCmds &custom_cmds,
    std::string &out_ent, std::string &out_prof,
    std::string &out_cmd, std::string &out_feedback) {
  sync_legacy_remote_slots_into_table(legacy_ents, legacy_names);
  const std::string *t_ents = remote_slots_ent();
  const std::string *t_names = remote_slots_name();

  static constexpr const char *kFeedback[10] = {
    "Up", "Down", "Left", "Right", "Select",
    "Back", "Home", "Vol +", "Vol -", "Toggle Power"
  };
  int idx = (key_idx >= 0 && key_idx < 10) ? key_idx : 0;
  int resolved_slot = 0;
  std::string dev_name;
  if (target_slot == -1) {
    resolved_slot = resolve_active_remote_slot(0, legacy_ents);
    out_ent = t_ents[resolved_slot];
    dev_name = t_names[resolved_slot].empty() ? format_remote_entity_fallback(out_ent) : t_names[resolved_slot];
    out_feedback = "Power: " + dev_name;
  } else {
    resolved_slot = resolve_active_remote_slot(target_slot, legacy_ents);
    out_ent = t_ents[resolved_slot];
    dev_name = t_names[resolved_slot].empty() ? format_remote_entity_fallback(out_ent) : t_names[resolved_slot];
    out_feedback = (idx == 9) ? ("Power: " + dev_name) : kFeedback[idx];
  }

  const std::string cfg_prof = (resolved_slot < 4 && slot_profiles != nullptr) ? slot_profiles[resolved_slot] : "Auto-Detect";
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
    const std::string legacy_ents[4], const std::string legacy_names[4],
    const std::string &remote_action_feedback, uint32_t remote_feedback_timer,
    FontT *font_date, FontT *font_small,
    const esphome::Color &theme_primary, const esphome::Color &theme_secondary,
    const esphome::Color &col_white, const esphome::Color &col_slate,
    const esphome::Color &col_gray, const esphome::Color &col_dark_gray,
    const esphome::Color &col_red) {
  sync_legacy_remote_slots_into_table(legacy_ents, legacy_names);
  const std::string *t_ents = remote_slots_ent();
  const std::string *t_names = remote_slots_name();

  int slot = resolve_active_remote_slot(active_remote_slot, legacy_ents);
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
    int valid_count = collect_configured_remote_slots(legacy_ents, valid_slots);
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

    // Row 0 (y = 110): Pinned Master System Power Card (targets Slot 1)
    render_modern_card(it, cx, 110, 276, 44, 12, esphome::Color(28, 18, 22), col_red,
                       "Toggle Power", col_white, font_small);

    // Rows 1..4 (y = 173, 236, 299, 362): 4 Devices Per Page
    int start_idx = cur_page * 4;
    int vis_count = std::min(4, std::max(0, valid_count - start_idx));
    for (int row = 0; row < vis_count; row++) {
      int s_idx = valid_slots[start_idx + row];
      int y_mid = 173 + row * 63;
      bool is_sel = (s_idx == slot);
      bool is_primary_slot = (s_idx == valid_slots[0]);

      std::string d_name = t_names[s_idx];
      if (d_name.empty()) d_name = format_remote_entity_fallback(t_ents[s_idx]);

      esphome::Color border_c = is_sel ? theme_primary : card_brd;
      esphome::Color text_c = is_sel ? theme_primary : col_white;

      int card_cx = is_primary_slot ? cx  : (cx - 25);
      int card_w  = is_primary_slot ? 276 : 226;
      int max_txt = is_primary_slot ? 228 : 182;

      render_modern_card(it, card_cx, y_mid, card_w, 44, 12, card_bg, border_c, nullptr, text_c, font_small);
      if (is_sel) {
        it.filled_circle(cx - 119, y_mid, 6, theme_primary);
      } else {
        it.circle(cx - 119, y_mid, 5, col_slate);
      }
      std::string fit_lbl = fit_text_to_width_font(d_name, max_txt, font_small);
      it.print(cx - 103, y_mid, font_small, text_c, esphome::display::TextAlign::CENTER_LEFT, fit_lbl.c_str());

      if (!is_primary_slot) {
        const int p_cx = cx + 120;
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
    std::string fit_fb = fit_text_to_width_font(remote_action_feedback, 280, font_small);
    it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, fit_fb.c_str());
  } else {
    std::string disp_title = fit_text_to_width_font(rem_name, 280, font_small);
    it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, disp_title.c_str());
  }

  const int left_fx  = LATTER_ROUND_DISPLAY ? 64 : ((SCREEN_W < 380) ? 38 : 48);
  const int right_fx = SCREEN_W - left_fx;
  const int flank_dy = LATTER_ROUND_DISPLAY ? 99 : ((SCREEN_W < 380) ? 108 : ((SCREEN_W >= 480) ? 128 : 114));
  const int top_fy   = cy - flank_dy;
  const int btm_fy   = cy + flank_dy;
  const int f_r      = 19;

  // 1. HOME: Top-Left (strictly symmetric with Bottom-Left BACK around cy!)
  it.filled_circle(left_fx, top_fy, f_r, col_dark_gray);
  it.circle(left_fx, top_fy, f_r, col_gray);
  it.filled_triangle(left_fx, top_fy - 8, left_fx - 9, top_fy - 1, left_fx + 9, top_fy - 1, col_white);
  it.filled_rectangle(left_fx - 6, top_fy - 1, 13, 8, col_white);
  it.filled_rectangle(left_fx - 2, top_fy + 2, 5, 5, col_dark_gray);

  // 2. VOL UP: Top-Right
  it.filled_circle(right_fx, top_fy, f_r, col_dark_gray);
  it.circle(right_fx, top_fy, f_r, col_gray);
  it.filled_rectangle(right_fx - 7, top_fy - 1, 15, 3, col_white);
  it.filled_rectangle(right_fx - 1, top_fy - 7, 3, 15, col_white);

  // 3. BACK: Bottom-Left
  it.filled_circle(left_fx, btm_fy, f_r, col_dark_gray);
  it.circle(left_fx, btm_fy, f_r, col_gray);
  it.line(left_fx + 3, btm_fy - 7, left_fx - 4, btm_fy, col_white);
  it.line(left_fx + 4, btm_fy - 7, left_fx - 3, btm_fy, col_white);
  it.line(left_fx - 4, btm_fy, left_fx + 3, btm_fy + 7, col_white);
  it.line(left_fx - 3, btm_fy, left_fx + 4, btm_fy + 7, col_white);

  // 4. VOL DOWN: Bottom-Right
  it.filled_circle(right_fx, btm_fy, f_r, col_dark_gray);
  it.circle(right_fx, btm_fy, f_r, col_gray);
  it.filled_rectangle(right_fx - 7, btm_fy - 1, 15, 3, col_white);

  // Adaptive Concentric D-Pad:
  //   - 320x480 Remote Assistant: r_out = 108, r_in = 42 (zero overlap with corner buttons!)
  //   - 410x502 Wrist Assist & 466x466 Pocket Assistant: r_out = 144, r_in = 50
  //   - 480x480 Mobile Assistant & 720x720 Desk Assistant: r_out = 160, r_in = 54 (expanded control ring!)
  const int dpad_r_out = (SCREEN_W < 380) ? 108 : ((!LATTER_ROUND_DISPLAY && SCREEN_W >= 480) ? 160 : 144);
  const int dpad_r_in  = (SCREEN_W < 380) ?  42 : ((!LATTER_ROUND_DISPLAY && SCREEN_W >= 480) ?  54 :  50);
  const int ok_r       = dpad_r_in - 8;
  it.circle(cx, cy, dpad_r_out, col_gray);
  it.circle(cx, cy, dpad_r_in, col_gray);

  const int d_in  = (dpad_r_in * 707) / 1000;
  const int d_out = (dpad_r_out * 707) / 1000;
  it.line(cx + d_in, cy + d_in, cx + d_out, cy + d_out, col_gray);
  it.line(cx - d_in, cy + d_in, cx - d_out, cy + d_out, col_gray);
  it.line(cx + d_in, cy - d_in, cx + d_out, cy - d_out, col_gray);
  it.line(cx - d_in, cy - d_in, cx - d_out, cy - d_out, col_gray);

  const int arr_mid = (dpad_r_in + dpad_r_out) / 2;
  it.filled_triangle(cx, cy - arr_mid - 8, cx - 10, cy - arr_mid + 8, cx + 10, cy - arr_mid + 8, col_white);
  it.filled_triangle(cx, cy + arr_mid + 8, cx - 10, cy + arr_mid - 8, cx + 10, cy + arr_mid - 8, col_white);
  it.filled_triangle(cx - arr_mid - 8, cy, cx - arr_mid + 8, cy - 10, cx - arr_mid + 8, cy + 10, col_white);
  it.filled_triangle(cx + arr_mid + 8, cy, cx + arr_mid - 8, cy - 10, cx + arr_mid - 8, cy + 10, col_white);

  it.filled_circle(cx, cy, ok_r, col_dark_gray);
  it.circle(cx, cy, ok_r, theme_primary);
  it.print(cx, cy, font_date, col_white, esphome::display::TextAlign::CENTER, "OK");
}

}  // namespace latter_ui