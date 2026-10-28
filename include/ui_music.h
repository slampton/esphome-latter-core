#pragma once

// ==============================================================================
// Latter Core UI Module: Music Player, Transport HUD, Inspector & Menus (ui_music.h)
// ==============================================================================

#include "ui_layout.h"
#include "ui_ambient_va.h"

namespace pocket_ui {

// ============================================================================
// SECTION 15: MUSIC PLAYER ENGINE — NOW PLAYING, TRANSPORT HUD & LIBRARY MENUS
// ============================================================================

inline int &remote_vol_pct() {
  static int pct = 70;
  return pct;
}

inline uint32_t &remote_play_override_ms() {
  static uint32_t ms = 0;
  return ms;
}

inline bool &remote_play_override_state() {
  static bool st = false;
  return st;
}

inline void set_remote_play_override(bool playing, uint32_t now_ms) {
  remote_play_override_state() = playing;
  remote_play_override_ms() = now_ms;
}

inline bool is_music_playing_effective(bool is_remote, bool local_playing,
                                       const std::string &ha_state, uint32_t now_ms) {
  if (!is_remote) {
    return local_playing;
  }
  if (remote_play_override_ms() > 0 && (now_ms - remote_play_override_ms() < 2500U)) {
    return remote_play_override_state();
  }
  return (ha_state == "playing");
}

template<typename DisplayT, typename FontHeaderT>
inline void render_menu_header(DisplayT &it, int cx, int y_pos, const std::string &title,
                               FontHeaderT *font_hdr, esphome::Color theme_primary) {
  if (title.empty() || font_hdr == nullptr) return;
  int eff_y = (y_pos == 50 || y_pos == 46) ? 48 : y_pos;
  std::string disp_title = fit_text_to_width_font(title, 280, font_hdr);
  it.print(cx, eff_y, font_hdr, theme_primary, esphome::display::TextAlign::CENTER, disp_title.c_str());
}

template<typename DisplayT, typename LocalImageT, typename RemoteImageT,
         typename FontSmallT, typename FontMicroT>
inline void render_music_now_playing(
    DisplayT &it, int cx, int cy, uint32_t now_ms,
    const char *art_mode_opt, const char *overlay_mode_opt, bool music_hud_active,
    bool is_remote, bool is_playing,
    LocalImageT *local_art, RemoteImageT *remote_art,
    const std::string &ha_target_art_url, const std::string &current_loaded_art_url,
    const std::string &active_speaker_name, const char *friendly_name,
    const std::string &music_action_feedback, uint32_t music_feedback_timer,
    const std::string &active_playback_media_type,
    bool show_spk_shortcut, bool show_lib_shortcut,
    float dur, float music_pos_base, uint32_t music_pos_sync_time,
    const std::string &local_title, const std::string &local_artist, const std::string &local_album,
    const std::string &ha_title, const std::string &ha_artist, const std::string &ha_album,
    FontSmallT *font_small, FontMicroT *font_micro,
    esphome::Color theme_primary, esphome::Color theme_secondary,
    esphome::Color col_bg, esphome::Color col_black, esphome::Color col_white,
    esphome::Color col_gray, esphome::Color col_dark_gray) {
  std::string art_mode = (art_mode_opt && art_mode_opt[0]) ? art_mode_opt : "Full Screen";
  bool is_standard = (art_mode == "Standard");
  bool is_letterbox = (art_mode == "Letterbox");

  std::string overlay_mode = (overlay_mode_opt && overlay_mode_opt[0]) ? overlay_mode_opt : "Art Only";
  bool always_visible = (overlay_mode == "Always Visible");
  bool show_meta_always = always_visible || (overlay_mode == "Art + Metadata");
  bool show_controls = always_visible || music_hud_active;
  bool show_metadata = show_meta_always || music_hud_active;

  const int art_center_y = 233;
  bool apply_scrim = always_visible || music_hud_active;
  bool has_art = false;
  bool rendered_art = false;
  if (is_remote) {
    if (!ha_target_art_url.empty() && ha_target_art_url != "none" &&
        current_loaded_art_url == ha_target_art_url && remote_art && remote_art->get_width() > 0) {
      has_art = true;
      rendered_art = render_album_art_canvas(
          it, remote_art, cx, is_standard, is_letterbox, apply_scrim, show_metadata);
    }
  } else {
    if (local_art && local_art->get_width() > 0) {
      has_art = true;
      rendered_art = render_album_art_canvas(
          it, local_art, cx, is_standard, is_letterbox, apply_scrim, show_metadata);
    }
  }

  if (!rendered_art) {
    it.filled_circle(cx, art_center_y, 80, col_dark_gray);
    it.circle(cx, art_center_y, 80, is_remote ? theme_secondary : theme_primary);
    it.circle(cx, art_center_y, 64, col_bg);
    it.circle(cx, art_center_y, 48, col_dark_gray);
    it.circle(cx, art_center_y, 32, col_bg);
    it.filled_circle(cx, art_center_y, 16, is_remote ? theme_secondary : theme_primary);
  }

  std::string spk_title = active_speaker_name;
  if (spk_title.empty() || spk_title == "None") spk_title = friendly_name;
  bool is_feedback = (now_ms - music_feedback_timer < 1800U && !music_action_feedback.empty());
  if (is_feedback) {
    spk_title = music_action_feedback;
  } else if (now_ms < 4000U) {
    spk_title = "Connecting...";
  }
  spk_title = fit_text_to_width_font(spk_title, 280, font_small);

  if (show_controls || music_hud_active) {
    if (!is_standard) {
      it.print(cx + 1, 51, font_small, col_black, esphome::display::TextAlign::CENTER, spk_title.c_str());
    }
    it.print(cx, 50, font_small, col_white, esphome::display::TextAlign::CENTER, spk_title.c_str());
  }

  if (show_controls) {
    const int prev_btn_x = cx - (POCKET_UI_ROUND_DISPLAY ? 109 : 98);
    const int next_btn_x = cx + (POCKET_UI_ROUND_DISPLAY ? 109 : 98);
    const int left_flank_x = POCKET_UI_ROUND_DISPLAY ? 71 : 46;
    const int right_flank_x = SCREEN_W - left_flank_x;

    bool is_speech_media = (active_playback_media_type == "podcast" ||
                            active_playback_media_type == "podcast_episode" ||
                            active_playback_media_type == "audiobook");

    // Zero-trig LUT circular skip glyph (replaces 256 sincosf calls with .rodata DIAL_SIN/DIAL_COS)
    auto draw_skip_glyph_lut = [&](int bx, int by, int dir_sign, const char *lbl, esphome::Color c) {
      if (dir_sign < 0) {
        it.filled_triangle(bx - 10, by - 15, bx - 1, by - 21, bx - 1, by - 9, c);
      } else {
        it.filled_triangle(bx + 10, by - 15, bx + 1, by - 21, bx + 1, by - 9, c);
      }
      for (int idx = 3; idx < 50; idx += 2) {
        int next_idx = std::min(50, idx + 2);
        float s1 = DIAL_SIN[idx] * (float)(-dir_sign);
        float c1 = DIAL_COS[idx];
        float s2 = DIAL_SIN[next_idx] * (float)(-dir_sign);
        float c2 = DIAL_COS[next_idx];
        it.line(bx + (int)(15.0f * s1), by - (int)(15.0f * c1),
                bx + (int)(15.0f * s2), by - (int)(15.0f * c2), c);
        it.line(bx + (int)(16.0f * s1), by - (int)(16.0f * c1),
                bx + (int)(16.0f * s2), by - (int)(16.0f * c2), c);
      }
      it.print(bx, by + 1, font_micro, c, esphome::display::TextAlign::CENTER, lbl);
      it.print(bx + 1, by + 1, font_micro, c, esphome::display::TextAlign::CENTER, lbl);
    };

    auto draw_prev_glyph = [&](int ox, int oy, esphome::Color c) {
      int px = prev_btn_x + ox;
      int py = art_center_y + oy;
      it.filled_rectangle(px - 14, py - 10, 3, 21, c);
      it.filled_triangle(px - 8, py, px + 6, py - 10, px + 6, py + 10, c);
    };

    auto draw_next_glyph = [&](int ox, int oy, esphome::Color c) {
      int nx = next_btn_x + ox;
      int ny = art_center_y + oy;
      it.filled_triangle(nx + 8, ny, nx - 6, ny - 10, nx - 6, ny + 10, c);
      it.filled_rectangle(nx + 11, ny - 10, 3, 21, c);
    };

    if (is_speech_media) {
      draw_skip_glyph_lut(prev_btn_x + 1, art_center_y + 1, -1, "10", col_black);
      draw_skip_glyph_lut(prev_btn_x, art_center_y, -1, "10", col_white);
      draw_skip_glyph_lut(next_btn_x + 1, art_center_y + 1, 1, "30", col_black);
      draw_skip_glyph_lut(next_btn_x, art_center_y, 1, "30", col_white);
    } else {
      draw_prev_glyph(1, 1, col_black);
      draw_prev_glyph(0, 0, col_white);
      draw_next_glyph(1, 1, col_black);
      draw_next_glyph(0, 0, col_white);
    }

    // Right Flank Volume Up (+) and Volume Down (-)
    it.filled_rectangle(right_flank_x - 6, 140, 15, 3, col_black);
    it.filled_rectangle(right_flank_x, 134, 3, 15, col_black);
    it.filled_rectangle(right_flank_x - 7, 139, 15, 3, col_white);
    it.filled_rectangle(right_flank_x - 1, 133, 3, 15, col_white);

    it.filled_rectangle(right_flank_x - 6, 326, 15, 3, col_black);
    it.filled_rectangle(right_flank_x - 7, 325, 15, 3, col_white);

    // Optional Left Flank Shortcuts (Matched 22x19px footprint for both Speaker at y=140 and Library at y=326!)
    if (show_spk_shortcut) {
      auto draw_spk = [&](int ox, int oy, esphome::Color c) {
        int sx = left_flank_x + ox;
        int sy = 140 + oy;
        // Balanced rear speaker box (5x9px) + flared cone (width 6px, height 19px: sy - 9 .. sy + 9)
        it.filled_rectangle(sx - 9, sy - 4, 5, 9, c);
        it.filled_triangle(sx - 4, sy - 4, sx - 4, sy + 4, sx + 2, sy + 9, c);
        it.filled_triangle(sx - 4, sy - 4, sx + 2, sy + 9, sx + 2, sy - 9, c);
        // Inner 2px-thick acoustic wave arc
        it.line(sx + 5, sy - 4, sx + 7, sy - 2, c);
        it.line(sx + 6, sy - 4, sx + 8, sy - 2, c);
        it.line(sx + 7, sy - 2, sx + 7, sy + 2, c);
        it.line(sx + 8, sy - 2, sx + 8, sy + 2, c);
        it.line(sx + 7, sy + 2, sx + 5, sy + 4, c);
        it.line(sx + 8, sy + 2, sx + 6, sy + 4, c);
        // Outer 2px-thick acoustic wave arc
        it.line(sx + 8, sy - 7, sx + 11, sy - 3, c);
        it.line(sx + 9, sy - 7, sx + 12, sy - 3, c);
        it.line(sx + 11, sy - 3, sx + 11, sy + 3, c);
        it.line(sx + 12, sy - 3, sx + 12, sy + 3, c);
        it.line(sx + 11, sy + 3, sx + 8, sy + 7, c);
        it.line(sx + 12, sy + 3, sx + 9, sy + 7, c);
      };
      draw_spk(1, 1, col_black);
      draw_spk(0, 0, col_white);
    }

    if (show_lib_shortcut) {
      auto draw_lib = [&](int ox, int oy, esphome::Color c) {
        int lx = left_flank_x + ox;
        int ly = 326 + oy;
        // Classic 4-Element Bookshelf (16px tall: two 3x16px upright spines + two 2px-thin leaning spines)
        it.filled_rectangle(lx - 9, ly - 8, 3, 16, c);
        it.filled_rectangle(lx - 4, ly - 8, 3, 16, c);
        it.line(lx + 2, ly + 7, lx + 6, ly - 8, c);
        it.line(lx + 3, ly + 7, lx + 7, ly - 8, c);
        it.line(lx + 6, ly + 7, lx + 10, ly - 7, c);
        it.line(lx + 7, ly + 7, lx + 11, ly - 7, c);
      };
      draw_lib(1, 1, col_black);
      draw_lib(0, 0, col_white);
    }

    // Center Hero Play/Pause Capsule (r = 34)
    it.filled_circle(cx, art_center_y, 34, col_dark_gray);
    it.circle(cx, art_center_y, 34, col_black);
    it.circle(cx, art_center_y, 33, col_gray);

    if (!is_playing) {
      it.filled_triangle(cx - 7, art_center_y - 15, cx - 7, art_center_y + 15, cx + 15, art_center_y, theme_primary);
    } else {
      it.filled_rectangle(cx - 9, art_center_y - 13, 6, 26, theme_primary);
      it.filled_rectangle(cx + 3, art_center_y - 13, 6, 26, theme_primary);
    }
  }

  if (show_metadata) {
    float current_pos = music_pos_base;
    if (is_playing && music_pos_sync_time > 0) {
      current_pos += (float)(now_ms - music_pos_sync_time) / 1000.0f;
    }
    float prog = 0.0f;
    if (!std::isnan(dur) && dur > 0.0f) {
      prog = std::max(0.0f, std::min(1.0f, current_pos / dur));
    }

    render_progress_arc_r224(it, cx, cy, prog, theme_secondary);

    std::string title = is_remote ? ha_title : ((local_title.empty() || local_title == "None") ? ha_title : local_title);
    std::string artist = is_remote ? ha_artist : ((local_artist.empty() || local_artist == "None") ? ha_artist : local_artist);
    std::string album = is_remote ? ha_album : ((local_album.empty() || local_album == "None") ? ha_album : local_album);

    if (title.empty() || title == "None") title = is_playing ? "Playing Audio" : "Idle";
    title = fit_text_to_width_font(title, 280, font_small);

    bool has_shadow = has_art;
    if (has_shadow) {
      it.print(cx + 1, 353, font_small, col_black, esphome::display::TextAlign::CENTER, title.c_str());
    }
    it.print(cx, 352, font_small, col_white, esphome::display::TextAlign::CENTER, title.c_str());

    if (!artist.empty() && artist != "None") {
      artist = fit_text_to_width_font(artist, 260, font_small);
      if (has_shadow) {
        it.print(cx + 1, 374, font_small, col_black, esphome::display::TextAlign::CENTER, artist.c_str());
      }
      it.print(cx, 373, font_small, col_white, esphome::display::TextAlign::CENTER, artist.c_str());
    }
    if (!album.empty() && album != "None") {
      album = fit_text_to_width_font(album, 240, font_small);
      if (has_shadow) {
        it.print(cx + 1, 395, font_small, col_black, esphome::display::TextAlign::CENTER, album.c_str());
      }
      it.print(cx, 394, font_small, col_white, esphome::display::TextAlign::CENTER, album.c_str());
    }
  }
}

template<typename DisplayT, typename FontDateT, typename FontSmallT, typename FontMicroT>
inline void render_music_menus(
    DisplayT &it, int cx, int music_overlay_mode,
    const std::string &active_speaker_entity, const std::string &active_speaker_name,
    const char *local_player_id,
    const std::string &selected_category_mode, const std::string &selected_category_title,
    int music_category_page, int music_browse_page, int music_browse_total_pages,
    bool menu_scroll_active, int menu_scroll_step, int menu_scroll_dir, int menu_scroll_target_page,
    const std::string &slot_1_name, const std::string &slot_1_id, const std::string &slot_1_type, bool slot_1_active,
    const std::string &slot_2_name, const std::string &slot_2_id, const std::string &slot_2_type, bool slot_2_active,
    const std::string &slot_3_name, const std::string &slot_3_id, const std::string &slot_3_type, bool slot_3_active,
    const std::string &slot_4_name, const std::string &slot_4_id, const std::string &slot_4_type, bool slot_4_active,
    const std::string &slot_5_name, const std::string &slot_5_id, const std::string &slot_5_type, bool slot_5_active,
    const std::string &target_speaker_id, const std::string &target_speaker_name,
    FontDateT *font_date, FontSmallT *font_small, FontMicroT *font_micro,
    esphome::Color theme_primary, esphome::Color theme_secondary,
    esphome::Color col_bg, esphome::Color col_black, esphome::Color col_white,
    esphome::Color col_slate, esphome::Color col_dark_gray,
    uint32_t now_ms = 0, bool is_playing = false, float dur = 0.0f,
    float music_pos_base = 0.0f, uint32_t music_pos_sync_time = 0,
    const std::string &cur_title = "", const std::string &cur_artist = "",
    const std::string &cur_album = "") {
  auto draw_right_chevrons = [&](bool show_up, bool show_down) {
    render_vertical_page_pips(it, show_up, show_down, col_bg, theme_secondary, col_slate);
  };

  (void)font_date;
  const esphome::Color card_bg(22, 27, 36);
  const esphome::Color card_brd(52, 62, 78);

  if (music_overlay_mode == 5) {
    // SUBPAGE 5: MUSIC MENU (SPEAKERS, LIBRARY, CLEAR QUEUE, SETTINGS)
    it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, "MUSIC");
    std::string spk_sub = fit_text_to_width_font(active_speaker_name, 260, font_small);
    it.print(cx, 76, font_small, col_white, esphome::display::TextAlign::CENTER, spk_sub.c_str());

    render_modern_card(it, cx, 122, 276, 44, 12, card_bg, theme_primary, "Speakers",    col_white, font_small);
    render_modern_card(it, cx, 185, 276, 44, 12, card_bg, theme_primary, "Library",     col_white, font_small);
    render_modern_card(it, cx, 248, 276, 44, 12, card_bg, theme_primary, "Queue",       col_white, font_small);
    render_modern_card(it, cx, 311, 276, 44, 12, card_bg, card_brd,      "Settings",    col_white, font_small);

  } else if (music_overlay_mode == 1) {
    // LEVEL 1: SPEAKERS BROWSER (5-Slot Speaker Inventory — 63px Stride Modern Cards + Animated Slide)
    int list_shift = 0;
    if (menu_scroll_active) {
      int stp = std::max(0, std::min(4, menu_scroll_step));
      list_shift = menu_scroll_dir * (116 - SHARED_SCROLL_EASE_116[stp]);
    }

    auto draw_speaker_card = [&](int y_mid, const std::string &name, const std::string &id_str, bool is_act) {
      if (name.empty() || y_mid < 65 || y_mid > 395) return;
      bool is_current = (is_act || id_str == active_speaker_entity);
      esphome::Color border_c = is_current ? theme_primary : card_brd;
      esphome::Color txt_c = is_current ? theme_primary : col_white;

      const int c_left = layout().list_card_left;
      render_modern_card(it, cx, y_mid, 276, 44, 12, card_bg, border_c, nullptr, txt_c, font_small);
      if (is_current) {
        it.filled_circle(c_left + 19, y_mid, 6, theme_primary);
      } else {
        it.circle(c_left + 19, y_mid, 5, col_slate);
      }
      std::string disp_name = fit_text_to_width_font(name, layout().list_card_w - 56, font_small);
      it.print(c_left + 35, y_mid, font_small, txt_c, esphome::display::TextAlign::CENTER_LEFT, disp_name.c_str());
    };

    draw_speaker_card(110 + list_shift, slot_1_name, slot_1_id, slot_1_active);
    draw_speaker_card(173 + list_shift, slot_2_name, slot_2_id, slot_2_active);
    draw_speaker_card(236 + list_shift, slot_3_name, slot_3_id, slot_3_active);
    draw_speaker_card(299 + list_shift, slot_4_name, slot_4_id, slot_4_active);
    draw_speaker_card(362 + list_shift, slot_5_name, slot_5_id, slot_5_active);

    if (menu_scroll_active) {
      it.filled_rectangle(0, 0, 466, 82, col_bg);
      it.filled_rectangle(0, 388, 466, 78, col_bg);
    }
    if (music_browse_total_pages > 1) {
      char spk_hdr[40];
      snprintf(spk_hdr, sizeof(spk_hdr), "SPEAKERS  %d/%d", music_browse_page, music_browse_total_pages);
      render_menu_header(it, cx, 48, spk_hdr, font_small, theme_primary);
      draw_right_chevrons(music_browse_page > 1, music_browse_page < music_browse_total_pages);
    } else {
      render_menu_header(it, cx, 48, "SPEAKERS", font_small, theme_primary);
    }

  } else if (music_overlay_mode == 2) {
    // LEVEL 2: LIBRARY CATEGORIES SUBMENU (Compact 202px Width Modern Cards, 63px Stride, 126px 2-Row Scroll)
    static constexpr int16_t SCROLL_EASE_126[5] = {0, 27, 68, 108, 126};
    int current_offset = 0;
    if (menu_scroll_active) {
      int cur_step = std::max(0, std::min(4, menu_scroll_step));
      int shift = SCROLL_EASE_126[cur_step];
      current_offset = (menu_scroll_dir == 1) ? shift : (126 - shift);
    } else {
      current_offset = (music_category_page == 2) ? 126 : 0;
    }

    static constexpr const char *CAT_LABELS[7] = {
        "Favorites", "Playlists", "Artists", "Albums", "Podcasts", "Radio", "Audiobooks"};
    for (int i = 0; i < 7; i++) {
      int y = 110 + i * 63 - current_offset;
      if (menu_scroll_active ? (y >= 60 && y <= 395) : (y >= 100 && y <= 370)) {
        render_modern_card(it, cx, y, 276, 44, 12, card_bg, card_brd,
                           CAT_LABELS[i], col_white, font_small);
      }
    }

    if (menu_scroll_active) {
      it.filled_rectangle(0, 0, 466, 82, col_bg);
      it.filled_rectangle(0, 388, 466, 78, col_bg);
    }
    render_menu_header(it, cx, 48, "LIBRARY", font_small, theme_primary);
    int cur_dot_page = menu_scroll_active ? menu_scroll_target_page : music_category_page;

    draw_right_chevrons(cur_dot_page == 2, cur_dot_page == 1);

  } else if (music_overlay_mode == 3) {
    // LEVEL 3: ITEM BROWSER (5 Slots at 63px Stride + Animated Vertical Slide + Page Counter Header)
    bool is_podcast_list = (selected_category_mode == "podcast");
    bool is_drill = (selected_category_mode == "artist" || selected_category_mode == "album" ||
                     selected_category_mode == "artist_albums");

    int list_shift = 0;
    if (menu_scroll_active) {
      int stp = std::max(0, std::min(4, menu_scroll_step));
      list_shift = menu_scroll_dir * (116 - SHARED_SCROLL_EASE_116[stp]);
    }

    auto draw_drilldown_icon = [&](int x_pos, int y_mid) {
      for (int dy : {-4, 0, 4}) it.filled_rectangle(x_pos, y_mid + dy, 13, 2, theme_secondary);
    };

    auto draw_slot_card = [&](int y_mid, const std::string &name, const std::string &mtype, bool is_act) {
      if (name.empty() || y_mid < 65 || y_mid > 395) return;
      const int c_left  = layout().list_card_left;
      const int c_right = cx + (layout().list_card_w / 2);
      if (is_drill) {
        const int play_cx = (SCREEN_W < 380) ? 28 : 82;
        const int dr_left = play_cx + 26;
        const int dr_w    = c_right - dr_left;
        const int dr_cx   = dr_left + (dr_w / 2);
        it.filled_circle(play_cx, y_mid, 18, card_bg);
        it.circle(play_cx, y_mid, 18, theme_primary);
        it.filled_triangle(play_cx - 3, y_mid - 6, play_cx - 3, y_mid + 6, play_cx + 6, y_mid, theme_primary);

        render_modern_card(it, dr_cx, y_mid, dr_w, 44, 12, card_bg, card_brd, nullptr, col_white, font_small);

        std::string disp_name = fit_text_to_width_font(name, dr_w - 48, font_small);
        it.print(dr_left + 14, y_mid, font_small, col_white, esphome::display::TextAlign::CENTER_LEFT, disp_name.c_str());
        draw_drilldown_icon(c_right - 24, y_mid);
      } else if (is_podcast_list) {
        render_modern_card(it, cx, y_mid, 276, 44, 12, card_bg, card_brd, nullptr, col_white, font_small);
        std::string disp_name = fit_text_to_width_font(name, layout().list_card_w - 58, font_small);
        it.print(c_left + 19, y_mid, font_small, col_white, esphome::display::TextAlign::CENTER_LEFT, disp_name.c_str());
        draw_drilldown_icon(c_right - 26, y_mid);
      } else if (mtype == "text") {
        std::string disp_name = fit_text_to_width_font(name, layout().list_card_w - 24, font_small);
        render_modern_card(it, cx, y_mid, 276, 44, 12, esphome::Color(15, 18, 24), esphome::Color(34, 40, 50),
                           disp_name.c_str(), col_slate, font_small);
      } else {
        esphome::Color border_c = is_act ? theme_primary : card_brd;
        esphome::Color txt_c = is_act ? theme_primary : col_white;
        render_modern_card(it, cx, y_mid, 276, 44, 12, card_bg, border_c, nullptr, txt_c, font_small);

        esphome::Color icon_c = (mtype == "radio") ? theme_secondary : theme_primary;
        it.filled_triangle(c_left + 15, y_mid - 6, c_left + 15, y_mid + 6, c_left + 25, y_mid, icon_c);

        std::string disp_name = fit_text_to_width_font(name, layout().list_card_w - 50, font_small);
        it.print(c_left + 35, y_mid, font_small, txt_c, esphome::display::TextAlign::CENTER_LEFT, disp_name.c_str());
      }
    };

    draw_slot_card(110 + list_shift, slot_1_name, slot_1_type, slot_1_active);
    draw_slot_card(173 + list_shift, slot_2_name, slot_2_type, slot_2_active);
    draw_slot_card(236 + list_shift, slot_3_name, slot_3_type, slot_3_active);
    draw_slot_card(299 + list_shift, slot_4_name, slot_4_type, slot_4_active);
    draw_slot_card(362 + list_shift, slot_5_name, slot_5_type, slot_5_active);

    if (menu_scroll_active) {
      it.filled_rectangle(0, 0, 466, 82, col_bg);
      it.filled_rectangle(0, 388, 466, 78, col_bg);
    }
    if (selected_category_mode == "active_queue") {
      // Queue View: Render Header + 1-Tap "Clear" Pill at (x = 342, y = 48)
      char q_hdr[36];
      if (music_browse_total_pages > 1) {
        snprintf(q_hdr, sizeof(q_hdr), "QUEUE %d/%d", music_browse_page, music_browse_total_pages);
      } else {
        snprintf(q_hdr, sizeof(q_hdr), "QUEUE");
      }
      it.print(185, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, q_hdr);
      render_modern_card(it, 326, 48, 84, 30, 8, card_bg, theme_primary, "Clear", col_white, font_micro);
      if (music_browse_total_pages > 1) {
        draw_right_chevrons(music_browse_page > 1, music_browse_page < music_browse_total_pages);
      }
    } else if (music_browse_total_pages > 1) {
      char pg_buf[24];
      snprintf(pg_buf, sizeof(pg_buf), "  %d/%d", music_browse_page, music_browse_total_pages);
      std::string base_hdr = fit_text_to_width_font(selected_category_title, 185, font_small);
      render_menu_header(it, cx, 48, base_hdr + pg_buf, font_small, theme_primary);
      bool wrap_list = (selected_category_mode == "artist" || selected_category_mode == "album");
      draw_right_chevrons(wrap_list || music_browse_page > 1,
                          wrap_list || music_browse_page < music_browse_total_pages);
    } else {
      render_menu_header(it, cx, 48, selected_category_title, font_small, theme_primary);
    }

  } else if (music_overlay_mode == 4) {
    // LEVEL 4: SPEAKER ACTION SHEET (Uppercase Page Title + Title-Case Subtitle & Actions)
    std::string spk_title = target_speaker_name.empty() ? "SPEAKER" : target_speaker_name;
    for (char &c : spk_title) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
    render_menu_header(it, cx, 48, spk_title, font_small, theme_primary);

    bool is_current = (target_speaker_id == active_speaker_entity);
    if (is_current) {
      it.print(cx, 76, font_small, theme_secondary, esphome::display::TextAlign::CENTER, "Currently Controlling");
      bool is_remote_target = (target_speaker_id != local_player_id);
      if (is_remote_target) {
        render_modern_card(it, cx, 142, 276, 44, 12, card_bg, card_brd, "Transfer to Local", col_white, font_small);
        render_modern_card(it, cx, 205, 276, 44, 12, card_bg, card_brd, "Unjoin Audio",      col_white, font_small);
        render_modern_card(it, cx, 268, 276, 44, 12, card_bg, card_brd, "Stop Playback",     col_white, font_small);
      } else {
        render_modern_card(it, cx, 160, 276, 44, 12, card_bg, card_brd, "Unjoin Audio",  col_white, font_small);
        render_modern_card(it, cx, 230, 276, 44, 12, card_bg, card_brd, "Stop Playback", col_white, font_small);
      }
    } else {
      it.print(cx, 76, font_small, col_slate, esphome::display::TextAlign::CENTER, "Speaker Actions");
      render_modern_card(it, cx, 130, 276, 44, 12, card_bg, card_brd, "Transfer Queue",  col_white, font_small);
      render_modern_card(it, cx, 190, 276, 44, 12, card_bg, card_brd, "Control Only",    col_white, font_small);
      render_modern_card(it, cx, 250, 276, 44, 12, card_bg, card_brd, "Join Audio",      col_white, font_small);
      render_modern_card(it, cx, 310, 276, 44, 12, card_bg, card_brd, "Unjoin / Detach", col_white, font_small);
    }
  } else if (music_overlay_mode == 6) {
    // LEVEL 6: NOW PLAYING TRACK & ALBUM INSPECTOR
    std::string disp_t = cur_title.empty() ? "NOW PLAYING" : fit_text_to_width_font(cur_title, 280, font_small);
    it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, disp_t.c_str());

    std::string sub_meta = cur_artist;
    if (!cur_album.empty() && cur_album != "None") {
      sub_meta = sub_meta.empty() ? cur_album : (sub_meta + " • " + cur_album);
    }
    if (!sub_meta.empty() && sub_meta != "None") {
      sub_meta = fit_text_to_width_font(sub_meta, 275, font_small);
      it.print(cx, 76, font_small, col_slate, esphome::display::TextAlign::CENTER, sub_meta.c_str());
    }

    // 1. Top Modern Card: Album Tracks dropped to y_mid = 132 (y = 110..154 -> 24px buffer below artist/album!)
    render_modern_card(it, cx, 132, 276, 44, 12, card_bg, card_brd, "Album Tracks", col_white, font_small);
    for (int dy : {-4, 0, 4}) {
      it.filled_rectangle(342, 132 + dy, 13, 2, theme_secondary);
    }

    // 2. Track Position Scrubber dropped just below center line (Time at y = 196, Scrubber at y = 242)
    float cur_pos = music_pos_base;
    if (is_playing && music_pos_sync_time > 0 && now_ms >= music_pos_sync_time) {
      cur_pos += static_cast<float>(now_ms - music_pos_sync_time) * 0.001f;
    }
    float valid_dur = (!std::isnan(dur) && dur > 0.0f) ? dur : 0.0f;
    if (valid_dur > 0.0f && cur_pos > valid_dur) cur_pos = valid_dur;
    if (cur_pos < 0.0f) cur_pos = 0.0f;
    float prog = (valid_dur > 0.0f) ? (cur_pos / valid_dur) : 0.0f;

    int el_s = static_cast<int>(cur_pos);
    int tot_s = static_cast<int>(valid_dur);
    char time_buf[40];
    if (tot_s > 0) {
      snprintf(time_buf, sizeof(time_buf), "%02d:%02d  /  %02d:%02d   (%d%%)",
               el_s / 60, el_s % 60, tot_s / 60, tot_s % 60, static_cast<int>(prog * 100.0f));
    } else {
      snprintf(time_buf, sizeof(time_buf), "%02d:%02d  /  --:--", el_s / 60, el_s % 60);
    }
    it.print(cx, 196, font_small, col_white, esphome::display::TextAlign::CENTER, time_buf);

    // Wide Scrubber Bar + Vector Seek Buttons (adaptive to screen width)
    const int scrub_y  = 242;
    const int l_seek_x = POCKET_UI_ROUND_DISPLAY ? 50 : 34;
    const int r_seek_x = SCREEN_W - l_seek_x;
    const int bar_x0   = POCKET_UI_ROUND_DISPLAY ? 95 : 72;
    const int bar_x1   = SCREEN_W - bar_x0;
    const int bar_w    = bar_x1 - bar_x0;

    // Left -15s Seek Button
    esphome::Color btn_bg(22, 27, 36);
    esphome::Color btn_brd(58, 68, 84);
    it.filled_circle(l_seek_x, scrub_y, 20, btn_bg);
    it.circle(l_seek_x, scrub_y, 20, btn_brd);
    it.line(l_seek_x + 4, scrub_y - 6, l_seek_x - 2, scrub_y, col_white);
    it.line(l_seek_x - 2, scrub_y,     l_seek_x + 4, scrub_y + 6, col_white);
    it.line(l_seek_x - 2, scrub_y - 6, l_seek_x - 8, scrub_y, theme_secondary);
    it.line(l_seek_x - 8, scrub_y,     l_seek_x - 2, scrub_y + 6, theme_secondary);

    // Right +15s Seek Button
    it.filled_circle(r_seek_x, scrub_y, 20, btn_bg);
    it.circle(r_seek_x, scrub_y, 20, btn_brd);
    it.line(r_seek_x - 4, scrub_y - 6, r_seek_x + 2, scrub_y, col_white);
    it.line(r_seek_x + 2, scrub_y,     r_seek_x - 4, scrub_y + 6, col_white);
    it.line(r_seek_x + 2, scrub_y - 6, r_seek_x + 8, scrub_y, theme_secondary);
    it.line(r_seek_x + 8, scrub_y,     r_seek_x + 2, scrub_y + 6, theme_secondary);

    // Scrubber Track Groove (r=5 capsule) + 25%/50%/75% ticks
    it.filled_circle(bar_x0, scrub_y, 5, col_dark_gray);
    it.filled_circle(bar_x1, scrub_y, 5, col_dark_gray);
    it.filled_rectangle(bar_x0, scrub_y - 5, bar_w, 11, col_dark_gray);
    it.line(bar_x0, scrub_y - 5, bar_x1, scrub_y - 5, btn_brd);
    it.line(bar_x0, scrub_y + 5, bar_x1, scrub_y + 5, btn_brd);
    for (int q = 1; q <= 3; q++) {
      int tx = bar_x0 + (bar_w * q) / 4;
      it.line(tx, scrub_y - 8, tx, scrub_y - 6, col_slate);
      it.line(tx, scrub_y + 6, tx, scrub_y + 8, col_slate);
    }

    // Filled Elapsed Bar + 3D Playhead Knob
    int knob_x = bar_x0 + static_cast<int>(prog * static_cast<float>(bar_w));
    if (knob_x > bar_x0) {
      it.filled_circle(bar_x0, scrub_y, 4, theme_primary);
      it.filled_rectangle(bar_x0, scrub_y - 4, knob_x - bar_x0, 9, theme_primary);
      it.horizontal_line(bar_x0, scrub_y - 3, knob_x - bar_x0, col_white);
    }
    it.filled_circle(knob_x, scrub_y, 9, col_white);
    it.filled_circle(knob_x, scrub_y, 6, theme_secondary);
    it.filled_circle(knob_x, scrub_y, 2, col_black);

    // 3. Lower Row (y_mid = 341, x = 162 / 298): Lyrics & Bio Cards (122x86, r=14)
    auto draw_inspector_tool_card = [&](int card_cx, int card_cy, bool is_bio, const char *label) {
      esphome::Color t_bg(22, 27, 36);
      esphome::Color t_brd(48, 56, 70);
      render_modern_card(it, card_cx, card_cy, 122, 86, 14, t_bg, t_brd, nullptr, col_white, font_small);

      int ix = card_cx;
      int iy = card_cy - 13;
      if (!is_bio) {
        // Lyrics Icon: Musical Eighth Note + 3Synced Lyric Lines
        it.filled_circle(ix - 10, iy + 4, 4, theme_primary);
        it.filled_rectangle(ix - 7, iy - 9, 2, 13, theme_primary);
        it.line(ix - 7, iy - 9, ix - 2, iy - 7, theme_primary);
        it.line(ix - 7, iy - 8, ix - 2, iy - 6, theme_primary);
        it.filled_rectangle(ix + 1, iy - 8, 14, 2, col_white);
        it.filled_rectangle(ix + 1, iy - 2, 14, 2, col_white);
        it.filled_rectangle(ix - 4, iy + 5, 19, 2, theme_secondary);
      } else {
        // Bio / Info Icon: Sculpted Info Circle 'i' Badge
        it.circle(ix, iy, 13, col_white);
        it.circle(ix, iy, 12, col_white);
        it.filled_circle(ix, iy - 6, 2, theme_primary);
        it.filled_rectangle(ix - 1, iy - 1, 3, 9, col_white);
      }

      it.print(card_cx, card_cy + 22, font_small, col_white, esphome::display::TextAlign::CENTER, label);
    };

    draw_inspector_tool_card(cx - 68, 341, false, "Lyrics");
    draw_inspector_tool_card(cx + 68, 341, true,  "Bio");

  } else if (music_overlay_mode == 7 || music_overlay_mode == 8) {
    // LEVEL 7 (LYRICS READER) & LEVEL 8 (ARTIST & ALBUM BIO READER)
    // Powered by esphome.<slug>_set_browse_slots (5 wrapped lines per page, unlimited pages!)
    const bool is_lyrics = (music_overlay_mode == 7);
    char hdr_buf[44];
    if (music_browse_total_pages > 1) {
      snprintf(hdr_buf, sizeof(hdr_buf), "%s  %d/%d",
               is_lyrics ? "LYRICS" : "BIO", music_browse_page, music_browse_total_pages);
    } else {
      snprintf(hdr_buf, sizeof(hdr_buf), "%s", is_lyrics ? "LYRICS" : "ARTIST & ALBUM BIO");
    }
    render_menu_header(it, cx, 48, hdr_buf, font_small, theme_primary);

    std::string sub_h = cur_title.empty() ? cur_artist : (cur_title + " • " + cur_artist);
    sub_h = fit_text_to_width_font(sub_h, 270, font_micro);
    it.print(cx, 76, font_micro, col_slate, esphome::display::TextAlign::CENTER, sub_h.c_str());

    // Sleek dark reader card (y = 98..374) displaying 5 wrapped lines per page
    render_modern_card(it, cx, 236, 324, 274, 16, card_bg, card_brd, nullptr, col_white, font_small);

    const std::string *lines[5] = {&slot_1_name, &slot_2_name, &slot_3_name, &slot_4_name, &slot_5_name};
    const bool acts[5] = {slot_1_active, slot_2_active, slot_3_active, slot_4_active, slot_5_active};
    for (int r = 0; r < 5; r++) {
      if (!lines[r]->empty()) {
        int ly = 132 + r * 50;
        std::string fit_ln = fit_text_to_width_font(*lines[r], 300, font_small);
        esphome::Color lc = acts[r] ? theme_primary : col_white;
        it.print(cx, ly, font_small, lc, esphome::display::TextAlign::CENTER, fit_ln.c_str());
      }
    }

    if (music_browse_total_pages > 1) {
      draw_right_chevrons(music_browse_page > 1, music_browse_page < music_browse_total_pages);
    }
  }

  render_back_button(it, cx, font_small, 426, "< BACK");
}

}  // namespace pocket_ui
