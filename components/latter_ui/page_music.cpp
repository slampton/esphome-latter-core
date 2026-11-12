#include "esphome/core/defines.h"
#include "ui_pages.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace latter_ui {

namespace {

void render_music_now_playing(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &rs = runtime_state();
  const auto &ms = media_state();
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;

  const std::string art_mode = ctx.music_fullscreen_art_opt.empty()
                                   ? std::string("Full Screen")
                                   : ctx.music_fullscreen_art_opt;
  const bool is_standard  = (art_mode == "Standard");
  const bool is_letterbox = (art_mode == "Letterbox");

  const std::string overlay_mode = ctx.music_overlay_style_opt.empty()
                                       ? std::string("Art Only")
                                       : ctx.music_overlay_style_opt;
  const bool always_visible   = (overlay_mode == "Always Visible");
  const bool show_meta_always = always_visible || (overlay_mode == "Art + Metadata");
  const bool music_hud_active = rs.music.hud_active;
  const bool show_controls    = always_visible || music_hud_active;
  const bool show_metadata    = show_meta_always || music_hud_active;

  const bool is_remote  = !ms.is_local;
  const bool is_playing = is_music_playing_effective(is_remote, ctx.local_media_playing, ms.state, now_ms);

  const int art_center_y = layout().music_art_cy;
  const bool apply_scrim = always_visible || music_hud_active;
  bool has_art = false;
  bool rendered_art = false;
  bool used_remote_art = false;

  const bool remote_art_ready = (!ms.art_url.empty() && ctx.remote_album_art.get_width() > 0);
  const bool local_art_ready  = (ctx.album_art.get_width() > 0);

  if (is_remote) {
    if (remote_art_ready) {
      has_art = true;
      used_remote_art = true;
      rendered_art = render_album_art_canvas(
          it, ctx.remote_album_art, cx, is_standard, is_letterbox, apply_scrim, show_metadata);
    }
  } else {
    if (local_art_ready) {
      has_art = true;
      rendered_art = render_album_art_canvas(
          it, ctx.album_art, cx, is_standard, is_letterbox, apply_scrim, show_metadata);
    } else if (remote_art_ready) {
      has_art = true;
      used_remote_art = true;
      rendered_art = render_album_art_canvas(
          it, ctx.remote_album_art, cx, is_standard, is_letterbox, apply_scrim, show_metadata);
    }
  }

  frame_base(it, ctx.col_bg);

  const Color glass_bg = rendered_art
      ? (used_remote_art ? sample_image_ambient_tint(ctx.remote_album_art, Color(20, 25, 34))
                         : sample_image_ambient_tint(ctx.album_art, Color(20, 25, 34)))
      : kChromeGlassTint;
  const Color glass_brd(62, 74, 92);

  std::string spk_title = rs.music.active_speaker_name.empty() ? ms.speaker_name : rs.music.active_speaker_name;
  if (spk_title.empty()) {
    spk_title = (ctx.friendly_name != nullptr) ? ctx.friendly_name : "Speaker";
  }
  const bool is_feedback = (now_ms - rs.music.feedback_timer < 1800U && !rs.music.action_feedback.empty());
  if (is_feedback) {
    spk_title = rs.music.action_feedback;
  } else if (now_ms < 4000U) {
    spk_title = "Connecting...";
  }
  spk_title = fit_text_to_width_font(spk_title, scale_x(260), ctx.font_small);

  if (show_controls || music_hud_active) {
    const int hdr_y = scale_y(50);
    const int pill_h = scale_y(30);
    const int pill_w = std::min(scale_x(268),
                                std::max(scale_x(136), static_cast<int>(spk_title.size()) * scale_x(11) + scale_x(34)));
    render_card_box(it, cx - pill_w / 2, hdr_y - pill_h / 2, pill_w, pill_h, glass_bg,
                    is_feedback ? ctx.theme_primary : glass_brd, scale_s(15));
    fast_print(it, cx + 1, hdr_y + 1, ctx.font_small, ctx.col_black, TextAlign::CENTER, spk_title.c_str());
    fast_print(it, cx, hdr_y, ctx.font_small,
               is_feedback ? ctx.theme_secondary : ctx.col_white, TextAlign::CENTER, spk_title.c_str());
    frame_hits().add_centered_rect(cx, hdr_y, pill_w, pill_h + scale_y(8),
                                   WidgetId::MUSIC_QUEUE_BTN, kHitTap | kHitFocusSlot, 0, scale_s(10));
  } else {
    frame_hits().add_centered_rect(cx, art_center_y, SCREEN_W - 32, scale_y(240),
                                   WidgetId::MUSIC_ART_TAP, kHitTap);
  }

  if (show_controls) {
    const int prev_btn_x    = layout().music_prev_x();
    const int next_btn_x    = layout().music_next_x();
    const int left_flank_x  = layout().music_left_flank_x();
    const int right_flank_x = layout().music_right_flank_x();
    const int top_fy  = scale_y(140);
    const int btm_fy  = scale_y(326);
    const int nav_r   = scale_s(22);
    const int flank_r = scale_s(18);
    const int pp_r    = scale_s(34);

    render_glass_circle(it, prev_btn_x, art_center_y, nav_r, glass_bg, glass_brd);
    render_glass_circle(it, next_btn_x, art_center_y, nav_r, glass_bg, glass_brd);
    render_glass_circle(it, right_flank_x, top_fy, flank_r, glass_bg, glass_brd);
    render_glass_circle(it, right_flank_x, btm_fy, flank_r, glass_bg, glass_brd);
    if (ctx.music_show_speaker_shortcut) {
      render_glass_circle(it, left_flank_x, top_fy, flank_r, glass_bg, glass_brd);
    }
    if (ctx.music_show_library_shortcut) {
      render_glass_circle(it, left_flank_x, btm_fy, flank_r, glass_bg, glass_brd);
    }

    const std::string &media_type =
        rs.music.active_playback_media_type.empty() ? ms.media_type : rs.music.active_playback_media_type;
    const bool is_speech_media = (media_type == "podcast" ||
                                  media_type == "podcast_episode" ||
                                  media_type == "audiobook");

    auto draw_skip_glyph_lut = [&](int bx, int by, int dir_sign, const char *lbl, Color c) {
      const int r1 = scale_s(14), r2 = scale_s(15);
      if (dir_sign < 0) {
        fast_filled_triangle(it, bx - scale_s(9), by - r1, bx - 1, by - scale_s(19), bx - 1, by - scale_s(9), c);
      } else {
        fast_filled_triangle(it, bx + scale_s(9), by - r1, bx + 1, by - scale_s(19), bx + 1, by - scale_s(9), c);
      }
      for (int idx = 3; idx < 50; idx += 2) {
        const int next_idx = std::min(50, idx + 2);
        const float s1 = DIAL_SIN[idx] * static_cast<float>(-dir_sign);
        const float c1 = DIAL_COS[idx];
        const float s2 = DIAL_SIN[next_idx] * static_cast<float>(-dir_sign);
        const float c2 = DIAL_COS[next_idx];
        fast_line(it, bx + static_cast<int>(r1 * s1), by - static_cast<int>(r1 * c1),
                  bx + static_cast<int>(r1 * s2), by - static_cast<int>(r1 * c2), c);
        fast_line(it, bx + static_cast<int>(r2 * s1), by - static_cast<int>(r2 * c1),
                  bx + static_cast<int>(r2 * s2), by - static_cast<int>(r2 * c2), c);
      }
      fast_print(it, bx, by + 1, ctx.font_micro, c, TextAlign::CENTER, lbl);
    };

    auto draw_prev_glyph = [&](int ox, int oy, Color c) {
      const int px = prev_btn_x + ox;
      const int py = art_center_y + oy;
      const int h = scale_s(9);
      fill_rect_row_major(it, px - scale_s(9), py - h, scale_s(3), h * 2 + 1, c);
      fast_filled_triangle(it, px - scale_s(4), py, px + scale_s(8), py - h, px + scale_s(8), py + h, c);
    };

    auto draw_next_glyph = [&](int ox, int oy, Color c) {
      const int nx = next_btn_x + ox;
      const int ny = art_center_y + oy;
      const int h = scale_s(9);
      fast_filled_triangle(it, nx + scale_s(4), ny, nx - scale_s(8), ny - h, nx - scale_s(8), ny + h, c);
      fill_rect_row_major(it, nx + scale_s(6), ny - h, scale_s(3), h * 2 + 1, c);
    };

    if (is_speech_media) {
      draw_skip_glyph_lut(prev_btn_x + 1, art_center_y + 1, -1, "10", ctx.col_black);
      draw_skip_glyph_lut(prev_btn_x, art_center_y, -1, "10", ctx.col_white);
      draw_skip_glyph_lut(next_btn_x + 1, art_center_y + 1, 1, "30", ctx.col_black);
      draw_skip_glyph_lut(next_btn_x, art_center_y, 1, "30", ctx.col_white);
    } else {
      draw_prev_glyph(1, 1, ctx.col_black);
      draw_prev_glyph(0, 0, ctx.col_white);
      draw_next_glyph(1, 1, ctx.col_black);
      draw_next_glyph(0, 0, ctx.col_white);
    }

    const int pls = scale_s(6);
    const int pth = scale_s(2);
    fill_rect_row_major(it, right_flank_x - pls + 1, top_fy + 1, pls * 2 + 1, pth, ctx.col_black);
    fill_rect_row_major(it, right_flank_x,           top_fy - pls + 1, pth, pls * 2 + 1, ctx.col_black);
    fill_rect_row_major(it, right_flank_x - pls,     top_fy,     pls * 2 + 1, pth, ctx.col_white);
    fill_rect_row_major(it, right_flank_x - 1,       top_fy - pls, pth, pls * 2 + 1, ctx.col_white);

    fill_rect_row_major(it, right_flank_x - pls + 1, btm_fy + 1, pls * 2 + 1, pth, ctx.col_black);
    fill_rect_row_major(it, right_flank_x - pls,     btm_fy,     pls * 2 + 1, pth, ctx.col_white);

    if (ctx.music_show_speaker_shortcut) {
      ui::draw_icon_speaker(it, left_flank_x + 1, top_fy + 1, ctx.col_black, true);
      ui::draw_icon_speaker(it, left_flank_x,     top_fy,     ctx.col_white, true);
      frame_hits().add_circle(left_flank_x, top_fy, flank_r,
                              WidgetId::MUSIC_SPEAKER_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle,
                              0, scale_s(26));
    }

    if (ctx.music_show_library_shortcut) {
      auto draw_lib = [&](int ox, int oy, Color c) {
        const int lx = left_flank_x + ox;
        const int ly = btm_fy + oy;
        fill_rect_row_major(it, lx - scale_s(8), ly - scale_s(7), scale_s(3), scale_s(14), c);
        fill_rect_row_major(it, lx - scale_s(3), ly - scale_s(7), scale_s(3), scale_s(14), c);
        fast_line(it, lx + scale_s(2), ly + scale_s(6), lx + scale_s(5), ly - scale_s(7), c);
        fast_line(it, lx + scale_s(3), ly + scale_s(6), lx + scale_s(6), ly - scale_s(7), c);
        fast_line(it, lx + scale_s(5), ly + scale_s(6), lx + scale_s(8), ly - scale_s(6), c);
        fast_line(it, lx + scale_s(6), ly + scale_s(6), lx + scale_s(9), ly - scale_s(6), c);
      };
      draw_lib(1, 1, ctx.col_black);
      draw_lib(0, 0, ctx.col_white);
      frame_hits().add_circle(left_flank_x, btm_fy, flank_r,
                              WidgetId::MUSIC_BROWSE_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle,
                              0, scale_s(26));
    }

    render_glass_circle(it, cx, art_center_y, pp_r, glass_bg, ctx.theme_primary);
    if (!is_playing) {
      fast_filled_triangle(it, cx - scale_s(7), art_center_y - scale_s(14),
                           cx - scale_s(7), art_center_y + scale_s(14),
                           cx + scale_s(14), art_center_y, ctx.col_white);
    } else {
      fill_rect_row_major(it, cx - scale_s(9), art_center_y - scale_s(12), scale_s(6), scale_s(24), ctx.col_white);
      fill_rect_row_major(it, cx + scale_s(3), art_center_y - scale_s(12), scale_s(6), scale_s(24), ctx.col_white);
    }

    frame_hits().add_circle(prev_btn_x, art_center_y, nav_r,
                            WidgetId::MUSIC_PREV_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle,
                            0, scale_s(18));
    frame_hits().add_circle(cx, art_center_y, pp_r,
                            WidgetId::MUSIC_PLAY_PAUSE, kHitTap | kHitFocusSlot | kHitFocusCircle,
                            0, scale_s(12));
    frame_hits().add_circle(next_btn_x, art_center_y, nav_r,
                            WidgetId::MUSIC_NEXT_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle,
                            0, scale_s(18));
    frame_hits().add_circle(right_flank_x, top_fy, flank_r,
                            WidgetId::MUSIC_VOL_SLIDER, kHitTap | kHitFocusSlot | kHitFocusCircle,
                            1, scale_s(26));
    frame_hits().add_circle(right_flank_x, btm_fy, flank_r,
                            WidgetId::MUSIC_VOL_SLIDER, kHitTap | kHitFocusSlot | kHitFocusCircle,
                            -1, scale_s(26));
  }

  if (show_metadata) {
    float current_pos = rs.music.pos_base;
    if (is_playing && rs.music.pos_sync_time > 0 && now_ms >= rs.music.pos_sync_time) {
      current_pos += static_cast<float>(now_ms - rs.music.pos_sync_time) * 0.001f;
    }
    float prog = 0.0f;
    if (!std::isnan(ms.duration) && ms.duration > 0.0f) {
      prog = std::max(0.0f, std::min(1.0f, current_pos / ms.duration));
    }

    render_progress_arc_r224(it, cx, cy, prog, ctx.theme_secondary);

    std::string title  = is_remote ? ms.title  : (ctx.track_title.empty()  ? ms.title  : ctx.track_title);
    std::string artist = is_remote ? ms.artist : (ctx.track_artist.empty() ? ms.artist : ctx.track_artist);
    std::string album  = is_remote ? ms.album  : (ctx.track_album.empty()  ? ms.album  : ctx.track_album);

    if (title.empty()) title = is_playing ? "Playing Audio" : "Idle";
    title = fit_text_to_width_font(title, scale_x(280), ctx.font_small);

    const bool has_shadow = has_art;
    const int y_t  = scale_y(352);
    const int y_ar = scale_y(374);
    const int y_al = scale_y(395);
    if (has_shadow) {
      fast_print(it, cx + 1, y_t + 1, ctx.font_small, ctx.col_black, TextAlign::CENTER, title.c_str());
    }
    fast_print(it, cx, y_t, ctx.font_small, ctx.col_white, TextAlign::CENTER, title.c_str());

    if (!artist.empty()) {
      artist = fit_text_to_width_font(artist, scale_x(260), ctx.font_small);
      if (has_shadow) {
        fast_print(it, cx + 1, y_ar + 1, ctx.font_small, ctx.col_black, TextAlign::CENTER, artist.c_str());
      }
      fast_print(it, cx, y_ar, ctx.font_small, ctx.theme_secondary, TextAlign::CENTER, artist.c_str());
    }
    if (!album.empty()) {
      album = fit_text_to_width_font(album, scale_x(240), ctx.font_small);
      if (has_shadow) {
        fast_print(it, cx + 1, y_al + 1, ctx.font_small, ctx.col_black, TextAlign::CENTER, album.c_str());
      }
      fast_print(it, cx, y_al, ctx.font_small, Color(154, 166, 182), TextAlign::CENTER, album.c_str());
    }

    frame_hits().add_centered_rect(cx, y_ar, scale_x(240), scale_y(62),
                                   WidgetId::MUSIC_ART_TAP, kHitTap | kHitFocusSlot, 6);
  }
}

void render_music_menus(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &rs = runtime_state();
  const auto &ms = media_state();
  const int cx = ctx.cx;
  const uint32_t now_ms = ctx.now_ms;
  const int music_overlay_mode = rs.music.overlay_mode;
  const bool is_rem = !ms.is_local;
  const bool is_playing = is_music_playing_effective(is_rem, ctx.local_media_playing, ms.state, now_ms);
  const BrowseSlotItem *slots = rs.slots_for(latter_rpc::SlotChannel::MUSIC);
  const int music_browse_page = rs.slot_page[static_cast<size_t>(latter_rpc::SlotChannel::MUSIC)];
  const int music_browse_total_pages = rs.slot_total_pages[static_cast<size_t>(latter_rpc::SlotChannel::MUSIC)];

  auto draw_right_chevrons = [&](bool show_up, bool show_down) {
    render_vertical_page_pips(it, show_up, show_down, ctx.col_bg, ctx.theme_secondary, ctx.col_slate);
  };

  Color card_bg = kChromeGlassTint;
  const Color card_brd(56, 68, 84);

  if (!rs.nav.menu_scroll_active) {
    const bool rem_ready = (!ms.art_url.empty() && ctx.remote_album_art.get_width() > 0);
    const bool loc_ready = (ctx.album_art.get_width() > 0);
    if (is_rem) {
      if (rem_ready) {
        render_album_art_canvas(it, ctx.remote_album_art, cx, false, false, true, false, 2);
        card_bg = sample_image_ambient_tint(ctx.remote_album_art, Color(20, 25, 34));
      }
    } else if (loc_ready) {
      render_album_art_canvas(it, ctx.album_art, cx, false, false, true, false, 2);
      card_bg = sample_image_ambient_tint(ctx.album_art, Color(20, 25, 34));
    } else if (rem_ready) {
      render_album_art_canvas(it, ctx.remote_album_art, cx, false, false, true, false, 2);
      card_bg = sample_image_ambient_tint(ctx.remote_album_art, Color(20, 25, 34));
    }
  }
  frame_base(it, ctx.col_bg);

  const int row0_y = layout().slot5_y(0);
  const int row_s  = layout().slot5_stride;
  const int mask_top_h = scale_y(82);
  const int mask_btm_y = scale_y(388);
  const int min_vis_y  = scale_y(65);
  const int max_vis_y  = scale_y(395);

  const std::string &cur_title  = (is_rem || ctx.track_title.empty())  ? ms.title  : ctx.track_title;
  const std::string &cur_artist = (is_rem || ctx.track_artist.empty()) ? ms.artist : ctx.track_artist;
  const std::string &cur_album  = (is_rem || ctx.track_album.empty())  ? ms.album  : ctx.track_album;
  const std::string &active_spk_name =
      rs.music.active_speaker_name.empty() ? ms.speaker_name : rs.music.active_speaker_name;

  if (music_overlay_mode == 5) {
    render_page_header(it, cx, "MUSIC", ctx.font_small, ctx.theme_primary);
    const std::string spk_sub = fit_text_to_width_font(active_spk_name, scale_x(260), ctx.font_small);
    render_sub_header(it, cx, spk_sub.c_str(), ctx.font_small, ctx.col_white);

    static constexpr int kHubYs[4] = {122, 185, 248, 311};
    static constexpr const char *kHubLabels[4] = {"Speakers", "Library", "Queue", "Settings"};
    for (int i = 0; i < 4; i++) {
      const int ym = scale_y(kHubYs[i]);
      const Color brd = (i < 3) ? ctx.theme_primary : card_brd;
      render_modern_card(it, cx, ym, 276, 44, 12, card_bg, brd, kHubLabels[i], ctx.col_white, ctx.font_small);
      if (i == 1) {
        ui::draw_icon_eighth_notes(it, layout().list_card_left + scale_x(22), ym, ctx.theme_primary);
      }
      frame_hits().add_centered_rect(cx, ym, layout().list_card_w, layout().slot5_h,
                                     WidgetId::LIST_SLOT_BASE + i, kHitTap | kHitFocusSlot,
                                     static_cast<int16_t>(i));
    }

  } else if (music_overlay_mode == 1) {
    int list_shift = 0;
    if (rs.nav.menu_scroll_active) {
      list_shift = rs.nav.menu_scroll_dir * (scale_y(116) - menu_scroll_shift(scale_y(116)));
    }

    for (int i = 0; i < 5; i++) {
      const int y_mid = layout().slot5_y(i) + list_shift;
      const auto &sl = slots[i];
      if (sl.name.empty() || y_mid < min_vis_y || y_mid > max_vis_y) continue;
      const bool is_current = (sl.active || (!sl.id.empty() && sl.id == rs.music.active_speaker_entity));
      const Color border_c = is_current ? ctx.theme_primary : card_brd;
      const Color txt_c    = is_current ? ctx.theme_primary : ctx.col_white;

      const int c_left = layout().list_card_left;
      render_modern_card(it, cx, y_mid, 276, 44, 12, card_bg, border_c, nullptr, txt_c, ctx.font_small);
      if (is_current) {
        fast_filled_circle(it, c_left + scale_x(19), y_mid, scale_s(6), ctx.theme_primary);
      } else {
        fast_circle(it, c_left + scale_x(19), y_mid, scale_s(5), ctx.col_slate);
      }
      const std::string disp_name = fit_text_to_width_font(sl.name, layout().list_card_w - scale_x(56), ctx.font_small);
      fast_print(it, c_left + scale_x(35), y_mid, ctx.font_small, txt_c, TextAlign::CENTER_LEFT, disp_name.c_str());

      if (!rs.nav.menu_scroll_active) {
        frame_hits().add_centered_rect(cx, y_mid, layout().list_card_w, layout().slot5_h,
                                       WidgetId::LIST_SLOT_BASE + i, kHitTap | kHitFocusSlot,
                                       static_cast<int16_t>(i));
        frame_hits().add_circle(c_left + scale_x(19), y_mid, scale_s(18),
                                WidgetId::MUSIC_SPK_GROUP + i, kHitTap | kHitFocusCircle,
                                static_cast<int16_t>(i));
      }
    }

    if (rs.nav.menu_scroll_active) {
      fill_rect_row_major(it, 0, 0, SCREEN_W, mask_top_h, ctx.col_bg);
      fill_rect_row_major(it, 0, mask_btm_y, SCREEN_W, SCREEN_H - mask_btm_y, ctx.col_bg);
    }
    if (music_browse_total_pages > 1) {
      char spk_hdr[40];
      std::snprintf(spk_hdr, sizeof(spk_hdr), "SPEAKERS  %d/%d", music_browse_page, music_browse_total_pages);
      render_page_header(it, cx, spk_hdr, ctx.font_small, ctx.theme_primary);
      draw_right_chevrons(music_browse_page > 1, music_browse_page < music_browse_total_pages);
    } else {
      render_page_header(it, cx, "SPEAKERS", ctx.font_small, ctx.theme_primary);
    }

  } else if (music_overlay_mode == 2) {
    const int max_shift = scale_y(126);
    int current_offset = 0;
    if (rs.nav.menu_scroll_active) {
      const int shift = menu_scroll_shift(max_shift);
      current_offset = (rs.nav.menu_scroll_dir == 1) ? shift : (max_shift - shift);
    } else {
      current_offset = (rs.music.category_page == 2) ? max_shift : 0;
    }

    static constexpr const char *CAT_LABELS[7] = {
        "Favorites", "Playlists", "Artists", "Albums", "Podcasts", "Radio", "Audiobooks"};
    int vis_slot = 0;
    for (int i = 0; i < 7; i++) {
      const int y = row0_y + i * row_s - current_offset;
      const bool in_view = rs.nav.menu_scroll_active
                               ? (y >= min_vis_y && y <= max_vis_y)
                               : (y >= row0_y - 10 && y <= layout().slot5_y(4) + 10);
      if (in_view) {
        render_modern_card(it, cx, y, 276, 44, 12, card_bg, card_brd,
                           CAT_LABELS[i], ctx.col_white, ctx.font_small);
        if (!rs.nav.menu_scroll_active && vis_slot < 5) {
          frame_hits().add_centered_rect(cx, y, layout().list_card_w, layout().slot5_h,
                                         WidgetId::LIST_SLOT_BASE + vis_slot, kHitTap | kHitFocusSlot,
                                         static_cast<int16_t>(i));
          vis_slot++;
        }
      }
    }

    if (rs.nav.menu_scroll_active) {
      fill_rect_row_major(it, 0, 0, SCREEN_W, mask_top_h, ctx.col_bg);
      fill_rect_row_major(it, 0, mask_btm_y, SCREEN_W, SCREEN_H - mask_btm_y, ctx.col_bg);
    }
    render_page_header(it, cx, "LIBRARY", ctx.font_small, ctx.theme_primary);
    const int cur_dot_page = rs.nav.menu_scroll_active ? rs.nav.menu_scroll_target_page : rs.music.category_page;
    draw_right_chevrons(cur_dot_page == 2, cur_dot_page == 1);

  } else if (music_overlay_mode == 3) {
    const std::string &cat_mode = rs.music.selected_category_mode;
    const bool is_podcast_list = (cat_mode == "podcast");
    const bool is_drill = (cat_mode == "artist" || cat_mode == "album" || cat_mode == "artist_albums");

    int list_shift = 0;
    if (rs.nav.menu_scroll_active) {
      list_shift = rs.nav.menu_scroll_dir * (scale_y(116) - menu_scroll_shift(scale_y(116)));
    }

    auto draw_drilldown_icon = [&](int x_pos, int y_mid) {
      for (int dy : {-4, 0, 4}) {
        fill_rect_row_major(it, x_pos, y_mid + scale_y(dy), scale_x(13), 2, ctx.theme_secondary);
      }
    };

    for (int i = 0; i < 5; i++) {
      const int y_mid = layout().slot5_y(i) + list_shift;
      const auto &sl = slots[i];
      if (sl.name.empty() || y_mid < min_vis_y || y_mid > max_vis_y) continue;
      const int c_left  = layout().list_card_left;
      const int c_right = cx + (layout().list_card_w / 2);

      if (is_drill) {
        const int play_cx = layout().music_drill_play_cx();
        const int dr_left = layout().music_drill_split_x();
        const int dr_w    = c_right - dr_left;
        const int dr_cx   = dr_left + (dr_w / 2);
        const int play_r  = scale_s(18);
        render_glass_circle(it, play_cx, y_mid, play_r, card_bg, ctx.theme_primary);
        fast_filled_triangle(it, play_cx - scale_s(3), y_mid - scale_s(6),
                             play_cx - scale_s(3), y_mid + scale_s(6),
                             play_cx + scale_s(6), y_mid, ctx.theme_primary);

        render_modern_card(it, dr_cx, y_mid, dr_w, 44, 12, card_bg, card_brd, nullptr, ctx.col_white, ctx.font_small);
        const std::string disp_name = fit_text_to_width_font(sl.name, dr_w - scale_x(48), ctx.font_small);
        fast_print(it, dr_left + scale_x(14), y_mid, ctx.font_small, ctx.col_white,
                   TextAlign::CENTER_LEFT, disp_name.c_str());
        draw_drilldown_icon(c_right - scale_x(24), y_mid);

        if (!rs.nav.menu_scroll_active) {
          frame_hits().add_circle(play_cx, y_mid, play_r,
                                  WidgetId::MUSIC_PLAY_PAUSE, kHitTap | kHitFocusSlot | kHitFocusCircle,
                                  static_cast<int16_t>(i), scale_s(12));
          frame_hits().add_centered_rect(dr_cx, y_mid, dr_w, layout().slot5_h,
                                         WidgetId::LIST_SLOT_BASE + i, kHitTap | kHitFocusSlot,
                                         static_cast<int16_t>(i));
        }
      } else if (is_podcast_list) {
        render_modern_card(it, cx, y_mid, 276, 44, 12, card_bg, card_brd, nullptr, ctx.col_white, ctx.font_small);
        const std::string disp_name = fit_text_to_width_font(sl.name, layout().list_card_w - 58, ctx.font_small);
        fast_print(it, c_left + 19, y_mid, ctx.font_small, ctx.col_white,
                   TextAlign::CENTER_LEFT, disp_name.c_str());
        draw_drilldown_icon(c_right - 26, y_mid);
        if (!rs.nav.menu_scroll_active) {
          frame_hits().add_centered_rect(cx, y_mid, layout().list_card_w, layout().slot5_h,
                                         WidgetId::LIST_SLOT_BASE + i, kHitTap | kHitFocusSlot,
                                         static_cast<int16_t>(i));
        }
      } else if (sl.type == "text") {
        const std::string disp_name = fit_text_to_width_font(sl.name, layout().list_card_w - 24, ctx.font_small);
        render_modern_card(it, cx, y_mid, 276, 44, 12, kChromeCardBgOff, kChromeCardBrdOff,
                           disp_name.c_str(), ctx.col_slate, ctx.font_small);
      } else {
        const Color border_c = sl.active ? ctx.theme_primary : card_brd;
        const Color txt_c    = sl.active ? ctx.theme_primary : ctx.col_white;
        render_modern_card(it, cx, y_mid, 276, 44, 12, card_bg, border_c, nullptr, txt_c, ctx.font_small);

        const Color icon_c = (sl.type == "radio") ? ctx.theme_secondary : ctx.theme_primary;
        fast_filled_triangle(it, c_left + 15, y_mid - 6, c_left + 15, y_mid + 6, c_left + 25, y_mid, icon_c);

        const std::string disp_name = fit_text_to_width_font(sl.name, layout().list_card_w - 50, ctx.font_small);
        fast_print(it, c_left + 35, y_mid, ctx.font_small, txt_c, TextAlign::CENTER_LEFT, disp_name.c_str());
        if (!rs.nav.menu_scroll_active) {
          frame_hits().add_centered_rect(cx, y_mid, layout().list_card_w, layout().slot5_h,
                                         WidgetId::LIST_SLOT_BASE + i, kHitTap | kHitFocusSlot,
                                         static_cast<int16_t>(i));
        }
      }
    }

    if (rs.nav.menu_scroll_active) {
      fill_rect_row_major(it, 0, 0, SCREEN_W, mask_top_h, ctx.col_bg);
      fill_rect_row_major(it, 0, mask_btm_y, SCREEN_W, SCREEN_H - mask_btm_y, ctx.col_bg);
    }
    if (cat_mode == "active_queue") {
      char q_hdr[36];
      if (music_browse_total_pages > 1) {
        std::snprintf(q_hdr, sizeof(q_hdr), "QUEUE  %d/%d", music_browse_page, music_browse_total_pages);
      } else {
        std::snprintf(q_hdr, sizeof(q_hdr), "QUEUE");
      }
      render_page_header(it, cx, q_hdr, ctx.font_small, ctx.theme_primary);
      const int clear_r = scale_s(12);
      const int clear_hw = scale_x(34);
      const int top_slot_top = layout().slot5_y(0) - scale_y(22);
      const int clear_y = (layout().hdr_y + scale_y(10) + top_slot_top) / 2;
      ui::pill_button(it, cx - clear_hw, cx + clear_hw, clear_y, clear_r,
                      ctx.theme_primary, card_bg, "Clear", ctx.col_white, ctx.font_micro,
                      WidgetId::MUSIC_QUEUE_CLEAR, kHitTap | kHitFocusSlot);
      if (music_browse_total_pages > 1) {
        draw_right_chevrons(music_browse_page > 1, music_browse_page < music_browse_total_pages);
      }
    } else if (music_browse_total_pages > 1) {
      char pg_buf[24];
      std::snprintf(pg_buf, sizeof(pg_buf), "  %d/%d", music_browse_page, music_browse_total_pages);
      const std::string base_hdr =
          fit_text_to_width_font(rs.music.selected_category_title, scale_x(185), ctx.font_small);
      render_page_header(it, cx, base_hdr + pg_buf, ctx.font_small, ctx.theme_primary);
      const bool wrap_list = (cat_mode == "artist" || cat_mode == "album");
      draw_right_chevrons(wrap_list || music_browse_page > 1,
                          wrap_list || music_browse_page < music_browse_total_pages);
    } else {
      render_page_header(it, cx, rs.music.selected_category_title, ctx.font_small, ctx.theme_primary);
    }

  } else if (music_overlay_mode == 4) {
    std::string spk_title = rs.music.target_speaker_name.empty() ? "SPEAKER" : rs.music.target_speaker_name;
    for (char &c : spk_title) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    render_page_header(it, cx, spk_title, ctx.font_small, ctx.theme_primary);

    const bool is_current = (rs.music.target_speaker_id == rs.music.active_speaker_entity);
    if (is_current) {
      render_sub_header(it, cx, "Currently Controlling", ctx.font_small, ctx.theme_secondary);
      const bool is_remote_target = is_rem && !ms.local_speaker.empty();
      if (is_remote_target) {
        static constexpr int kRemYs[3] = {142, 205, 268};
        static constexpr const char *kRemLbls[3] = {"Transfer to Local", "Unjoin Audio", "Stop Playback"};
        for (int r = 0; r < 3; r++) {
          const int ym = scale_y(kRemYs[r]);
          render_modern_card(it, cx, ym, 276, 44, 12, card_bg, card_brd, kRemLbls[r], ctx.col_white, ctx.font_small);
          frame_hits().add_centered_rect(cx, ym, layout().list_card_w, layout().slot5_h,
                                         WidgetId::LIST_SLOT_BASE + r, kHitTap | kHitFocusSlot,
                                         static_cast<int16_t>(r));
        }
      } else {
        static constexpr int kLocYs[2] = {160, 230};
        static constexpr const char *kLocLbls[2] = {"Unjoin Audio", "Stop Playback"};
        for (int r = 0; r < 2; r++) {
          const int ym = scale_y(kLocYs[r]);
          render_modern_card(it, cx, ym, 276, 44, 12, card_bg, card_brd, kLocLbls[r], ctx.col_white, ctx.font_small);
          frame_hits().add_centered_rect(cx, ym, layout().list_card_w, layout().slot5_h,
                                         WidgetId::LIST_SLOT_BASE + r, kHitTap | kHitFocusSlot,
                                         static_cast<int16_t>(r));
        }
      }
    } else {
      render_sub_header(it, cx, "Speaker Actions", ctx.font_small, ctx.col_slate);
      static constexpr int kActYs[4] = {130, 190, 250, 310};
      static constexpr const char *kActLbls[4] = {
          "Transfer Queue", "Control Only", "Join Audio", "Unjoin / Detach"};
      for (int r = 0; r < 4; r++) {
        const int ym = scale_y(kActYs[r]);
        render_modern_card(it, cx, ym, 276, 44, 12, card_bg, card_brd, kActLbls[r], ctx.col_white, ctx.font_small);
        frame_hits().add_centered_rect(cx, ym, layout().list_card_w, layout().slot5_h,
                                       WidgetId::LIST_SLOT_BASE + r, kHitTap | kHitFocusSlot,
                                       static_cast<int16_t>(r));
      }
    }

  } else if (music_overlay_mode == 6) {
    const std::string disp_t = cur_title.empty()
                                   ? std::string("NOW PLAYING")
                                   : fit_text_to_width_font(cur_title, scale_x(280), ctx.font_small);
    render_page_header(it, cx, disp_t.c_str(), ctx.font_small, ctx.theme_primary);

    std::string sub_meta = cur_artist;
    if (!cur_album.empty()) {
      sub_meta = sub_meta.empty() ? cur_album : (sub_meta + " • " + cur_album);
    }
    if (!sub_meta.empty()) {
      sub_meta = fit_text_to_width_font(sub_meta, scale_x(275), ctx.font_small);
      render_sub_header(it, cx, sub_meta.c_str(), ctx.font_small, ctx.col_slate);
    }

    const int trk_card_y = scale_y(132);
    render_modern_card(it, cx, trk_card_y, 276, 44, 12, card_bg, card_brd, "Album Tracks", ctx.col_white, ctx.font_small);
    for (int dy : {-4, 0, 4}) {
      fill_rect_row_major(it, cx + scale_x(109), trk_card_y + scale_y(dy), scale_x(13), 2, ctx.theme_secondary);
    }
    frame_hits().add_centered_rect(cx, trk_card_y, layout().list_card_w, layout().slot5_h,
                                   WidgetId::LIST_SLOT_BASE + 0, kHitTap | kHitFocusSlot, 0);

    float cur_pos = rs.music.pos_base;
    if (is_playing && rs.music.pos_sync_time > 0 && now_ms >= rs.music.pos_sync_time) {
      cur_pos += static_cast<float>(now_ms - rs.music.pos_sync_time) * 0.001f;
    }
    const float valid_dur = (!std::isnan(ms.duration) && ms.duration > 0.0f) ? ms.duration : 0.0f;
    if (valid_dur > 0.0f && cur_pos > valid_dur) cur_pos = valid_dur;
    if (cur_pos < 0.0f) cur_pos = 0.0f;
    const float prog = (valid_dur > 0.0f) ? (cur_pos / valid_dur) : 0.0f;

    const int el_s  = static_cast<int>(cur_pos);
    const int tot_s = static_cast<int>(valid_dur);
    char time_buf[40];
    if (tot_s > 0) {
      std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d  /  %02d:%02d   (%d%%)",
                    el_s / 60, el_s % 60, tot_s / 60, tot_s % 60, static_cast<int>(prog * 100.0f));
    } else {
      std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d  /  %s", el_s / 60, el_s % 60, kUnknownGlyph);
    }
    fast_print(it, cx, scale_y(196), ctx.font_small, ctx.col_white, TextAlign::CENTER, time_buf);

    const int scrub_y  = layout().music_scrub_y;
    const int l_seek_x = layout().music_seek_left_x();
    const int r_seek_x = layout().music_seek_right_x();
    const int bar_x0   = layout().music_scrub_x0();
    const int bar_x1   = layout().music_scrub_x1();
    const int bar_w    = layout().music_scrub_w();

    const Color btn_brd(58, 68, 84);
    render_glass_circle(it, l_seek_x, scrub_y, scale_s(20), card_bg, btn_brd);
    fast_line(it, l_seek_x + 4, scrub_y - 6, l_seek_x - 2, scrub_y,     ctx.col_white);
    fast_line(it, l_seek_x - 2, scrub_y,     l_seek_x + 4, scrub_y + 6, ctx.col_white);
    fast_line(it, l_seek_x - 2, scrub_y - 6, l_seek_x - 8, scrub_y,     ctx.theme_secondary);
    fast_line(it, l_seek_x - 8, scrub_y,     l_seek_x - 2, scrub_y + 6, ctx.theme_secondary);
    frame_hits().add_circle(l_seek_x, scrub_y, scale_s(20),
                            WidgetId::MUSIC_PREV_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle,
                            0, scale_s(16));

    render_glass_circle(it, r_seek_x, scrub_y, scale_s(20), card_bg, btn_brd);
    fast_line(it, r_seek_x - 4, scrub_y - 6, r_seek_x + 2, scrub_y,     ctx.col_white);
    fast_line(it, r_seek_x + 2, scrub_y,     r_seek_x - 4, scrub_y + 6, ctx.col_white);
    fast_line(it, r_seek_x + 2, scrub_y - 6, r_seek_x + 8, scrub_y,     ctx.theme_secondary);
    fast_line(it, r_seek_x + 8, scrub_y,     r_seek_x + 2, scrub_y + 6, ctx.theme_secondary);
    frame_hits().add_circle(r_seek_x, scrub_y, scale_s(20),
                            WidgetId::MUSIC_NEXT_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle,
                            0, scale_s(16));

    fast_filled_circle(it, bar_x0, scrub_y, 5, ctx.col_dark_gray);
    fast_filled_circle(it, bar_x1, scrub_y, 5, ctx.col_dark_gray);
    fill_rect_row_major(it, bar_x0, scrub_y - 5, bar_w, 11, ctx.col_dark_gray);
    fast_line(it, bar_x0, scrub_y - 5, bar_x1, scrub_y - 5, btn_brd);
    fast_line(it, bar_x0, scrub_y + 5, bar_x1, scrub_y + 5, btn_brd);
    for (int q = 1; q <= 3; q++) {
      const int tx = bar_x0 + (bar_w * q) / 4;
      fast_line(it, tx, scrub_y - 8, tx, scrub_y - 6, ctx.col_slate);
      fast_line(it, tx, scrub_y + 6, tx, scrub_y + 8, ctx.col_slate);
    }

    const int knob_x = bar_x0 + static_cast<int>(prog * static_cast<float>(bar_w));
    if (knob_x > bar_x0) {
      fast_filled_circle(it, bar_x0, scrub_y, 4, ctx.theme_primary);
      fill_rect_row_major(it, bar_x0, scrub_y - 4, knob_x - bar_x0, 9, ctx.theme_primary);
      fast_hline(it, bar_x0, scrub_y - 3, knob_x - bar_x0, ctx.col_white);
    }
    fast_filled_circle(it, knob_x, scrub_y, 9, ctx.col_white);
    fast_filled_circle(it, knob_x, scrub_y, 6, ctx.theme_secondary);
    fast_filled_circle(it, knob_x, scrub_y, 2, ctx.col_black);
    frame_hits().add_rect(bar_x0, scrub_y - scale_y(20), bar_w, scale_y(40),
                          WidgetId::MUSIC_SCRUB_BAR, kHitTap | kHitHorizSlider | kHitFocusSlot);

    auto draw_inspector_tool_card = [&](int card_cx, int card_cy, bool is_bio, const char *label, int slot_i) {
      const Color t_bg = kChromeGlassTint;
      const Color t_brd(48, 56, 70);
      render_modern_card(it, card_cx, card_cy, 122, 86, 14, t_bg, t_brd, nullptr, ctx.col_white, ctx.font_small);

      const int ix = card_cx;
      const int iy = card_cy - 13;
      if (!is_bio) {
        fast_filled_circle(it, ix - 10, iy + 4, 4, ctx.theme_primary);
        fill_rect_row_major(it, ix - 7, iy - 9, 2, 13, ctx.theme_primary);
        fast_line(it, ix - 7, iy - 9, ix - 2, iy - 7, ctx.theme_primary);
        fast_line(it, ix - 7, iy - 8, ix - 2, iy - 6, ctx.theme_primary);
        fill_rect_row_major(it, ix + 1, iy - 8, 14, 2, ctx.col_white);
        fill_rect_row_major(it, ix + 1, iy - 2, 14, 2, ctx.col_white);
        fill_rect_row_major(it, ix - 4, iy + 5, 19, 2, ctx.theme_secondary);
      } else {
        fast_circle(it, ix, iy, 13, ctx.col_white);
        fast_circle(it, ix, iy, 12, ctx.col_white);
        fast_filled_circle(it, ix, iy - 6, 2, ctx.theme_primary);
        fill_rect_row_major(it, ix - 1, iy - 1, 3, 9, ctx.col_white);
      }

      fast_print(it, card_cx, card_cy + scale_y(22), ctx.font_small, ctx.col_white, TextAlign::CENTER, label);
      frame_hits().add_centered_rect(card_cx, card_cy, scale_x(122), scale_y(86),
                                     WidgetId::LIST_SLOT_BASE + slot_i, kHitTap | kHitFocusSlot,
                                     static_cast<int16_t>(slot_i));
    };

    draw_inspector_tool_card(cx - scale_x(68), scale_y(341), false, "Lyrics", 1);
    draw_inspector_tool_card(cx + scale_x(68), scale_y(341), true,  "Bio",    2);

  } else if (music_overlay_mode == 7 || music_overlay_mode == 8) {
    const bool is_lyrics = (music_overlay_mode == 7);
    char hdr_buf[44];
    if (music_browse_total_pages > 1) {
      std::snprintf(hdr_buf, sizeof(hdr_buf), "%s  %d/%d",
                    is_lyrics ? "LYRICS" : "BIO", music_browse_page, music_browse_total_pages);
    } else {
      std::snprintf(hdr_buf, sizeof(hdr_buf), "%s", is_lyrics ? "LYRICS" : "ARTIST & ALBUM BIO");
    }
    render_page_header(it, cx, hdr_buf, ctx.font_small, ctx.theme_primary);

    std::string sub_h = cur_title.empty() ? cur_artist : (cur_title + " • " + cur_artist);
    sub_h = fit_text_to_width_font(sub_h, scale_x(270), ctx.font_small);
    render_sub_header(it, cx, sub_h.c_str(), ctx.font_small, ctx.col_slate);

    render_modern_card(it, cx, scale_y(236), 324, scale_y(274), 16,
                       card_bg, card_brd, nullptr, ctx.col_white, ctx.font_small);

    for (int r = 0; r < 5; r++) {
      if (!slots[r].name.empty()) {
        const int ly = scale_y(132 + r * 50);
        const std::string fit_ln = fit_text_to_width_font(slots[r].name, scale_x(300), ctx.font_small);
        const Color lc = slots[r].active ? ctx.theme_primary : ctx.col_white;
        fast_print(it, cx, ly, ctx.font_small, lc, TextAlign::CENTER, fit_ln.c_str());
      }
    }

    if (music_browse_total_pages > 1) {
      draw_right_chevrons(music_browse_page > 1, music_browse_page < music_browse_total_pages);
    }
  }

  render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back");
}

}  // namespace

// ==============================================================================
// Page 2 Entry Point
// ==============================================================================

void render_music_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  if (runtime_state().music.overlay_mode == 0) {
    render_music_now_playing(it, ctx);
  } else {
    render_music_menus(it, ctx);
  }
}

}  // namespace latter_ui
