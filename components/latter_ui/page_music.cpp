// ==============================================================================
// Clean-Sheet Music App v3 Renderer (components/latter_ui/page_music.cpp)
// PageId::MUSIC (2) — Unified 9-Tab Peer Mode-Bar Architecture (D-059 / D-060):
//   - Peer Tabs 0..8 (MusicDrilldown::NONE):
//       0: NOW_PLAYING  · 1: LIBRARY (2x3 Bento) · 2: SPEAKERS · 3: VOLUME
//       4: QUEUE        · 5: LYRICS (Ambient)    · 6: ALBUM    · 7: INFO
//       8: SETTINGS (Music Settings — Peer Tab 8, 10 s idle spring-back)
//   - Deep Drilldowns (MusicDrilldown != NONE, Mode Bar hidden, < Back shown):
//       LIB_LIST (5 stack-uniform rows) · SPEAKER_ACTIONS
//   - Floating Takeovers (MusicTakeover::SCRUB / ACTION_SHEET):
//       Zero outer passive glass containers & strict hit isolation (D-043 / D-048).
// ==============================================================================

#include "esphome/core/defines.h"
#include "latter_actions.h"
#include "ui_pages.h"
#include "ui_touch_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace latter_ui {

namespace {

constexpr bool is_speech_media_type(std::string_view mt) noexcept {
  return mt == "podcast" || mt == "podcast_episode" || mt == "audiobook";
}

void format_mmss(int total_sec, char *out, size_t cap) noexcept {
  if (total_sec < 0) {
    std::snprintf(out, cap, "%s", kUnknownGlyph);
    return;
  }
  const int hrs = total_sec / 3600;
  const int mins = (total_sec % 3600) / 60;
  const int secs = total_sec % 60;
  if (hrs > 0) {
    std::snprintf(out, cap, "%d:%02d:%02d", hrs, mins, secs);
  } else {
    std::snprintf(out, cap, "%d:%02d", mins, secs);
  }
}

void draw_shadow_print(esphome::display::Display &it, int x, int y,
                       esphome::display::BaseFont *font, esphome::Color col,
                       esphome::display::TextAlign align, const char *txt,
                       bool dual_shadow = false, int alpha_q8 = 256) {
  if (font == nullptr || txt == nullptr || txt[0] == '\0' || alpha_q8 <= 0) return;
  if (alpha_q8 >= 256) {
    if (dual_shadow || backdrop_active()) {
      if (dual_shadow) {
        fast_print(it, x + 2, y + 2, font, kChromeShadow, align, txt);
      }
      fast_print(it, x + 1, y + 1, font, kChromeShadow, align, txt);
    }
    fast_print(it, x, y, font, col, align, txt);
  } else {
    const int sh_a = (alpha_q8 * 210) >> 8;
    if (dual_shadow) {
      fast_print_alpha(it, x + 2, y + 2, font, kChromeShadow, align, txt, (sh_a * 170) >> 8);
    }
    fast_print_alpha(it, x + 1, y + 1, font, kChromeShadow, align, txt, sh_a);
    fast_print_alpha(it, x, y, font, col, align, txt, alpha_q8);
  }
}

void draw_music_mdi(esphome::display::Display &it, std::string_view slug,
                    int cx, int cy, int size_px, esphome::Color col,
                    bool shadow = true, std::string_view fallback = "mdi:music") {
  const std::string_view chosen = has_mdi_icon(slug) ? slug : fallback;
  if (shadow && backdrop_active()) {
    rasterize_mdi_icon_fn(chosen, cx + 1, cy + 1, size_px, [&](int x0, int x1, int y, uint8_t a) {
      if (!row_in_clip_band(y) || a < 24U) return;
      const int sa = (static_cast<int>(a) * 185) >> 8;
      for (int x = x0; x < x1; ++x) fast_pixel_blend(it, x, y, kChromeShadow, sa);
    });
  }
  rasterize_mdi_icon_fn(chosen, cx, cy, size_px, [&](int x0, int x1, int y, uint8_t a) {
    if (!row_in_clip_band(y) || a == 0U) return;
    if (a >= 250U) {
      fast_hline(it, x0, y, x1 - x0, col);
    } else {
      for (int x = x0; x < x1; ++x) fast_pixel_blend(it, x, y, col, a);
    }
  });
}

int wrap_music_text_lines(std::string_view raw, int max_w, esphome::display::BaseFont *font,
                          std::string *out_lines, int max_lines) {
  if (font == nullptr || out_lines == nullptr || max_lines <= 0 || max_w <= 16) return 0;
  const std::string clean = sanitize_utf8_for_font(std::string(raw));
  if (clean.empty()) return 0;

  int count = 0;
  size_t pos = 0;
  while (pos < clean.size() && count < max_lines) {
    while (pos < clean.size() && (clean[pos] == ' ' || clean[pos] == '\r')) ++pos;
    if (pos >= clean.size()) break;
    if (clean[pos] == '\n') {
      ++pos;
      continue;
    }
    const size_t nl = clean.find('\n', pos);
    const size_t seg_end = (nl == std::string::npos) ? clean.size() : nl;
    if (count == max_lines - 1) {
      out_lines[count++] = fit_text_to_width_font(clean.substr(pos, seg_end - pos), max_w, font);
      break;
    }

    size_t best_end = pos;
    size_t scan = pos;
    while (scan < seg_end) {
      const size_t sp = clean.find(' ', scan);
      const size_t word_end = (sp == std::string::npos || sp > seg_end) ? seg_end : sp;
      const std::string cand = clean.substr(pos, word_end - pos);
      int w = 0, xo = 0, b = 0, h = 0;
      font->measure(cand.c_str(), &w, &xo, &b, &h);
      if (w <= max_w) {
        best_end = word_end;
        scan = (word_end >= seg_end) ? seg_end : (word_end + 1U);
      } else {
        break;
      }
    }

    if (best_end == pos) {
      out_lines[count++] = fit_text_to_width_font(clean.substr(pos, seg_end - pos), max_w, font);
      pos = (nl == std::string::npos) ? clean.size() : (nl + 1U);
    } else {
      out_lines[count++] = clean.substr(pos, best_end - pos);
      pos = best_end;
      if (pos < clean.size() && clean[pos] == '\n') ++pos;
    }
  }
  return count;
}

struct ResolvedTrackStrings {
  std::string title{};
  std::string artist{};
  std::string album{};
  std::string speaker_name{};
  std::string speaker_entity{};
  bool is_playing{false};
  bool is_speech{false};
};

ResolvedTrackStrings resolve_track_strings(const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const auto &ms = media_state();
  ResolvedTrackStrings out{};
  out.is_playing = is_music_playing_effective(!ms.is_local, ctx.local_media_playing, ms.state, ctx.now_ms);

  if (!ms.is_local) {
    out.title = ms.title;
    out.artist = ms.artist;
    out.album = ms.album;
  } else {
    out.title = !ctx.track_title.empty() ? ctx.track_title.str() : ms.title;
    out.artist = !ctx.track_artist.empty() ? ctx.track_artist.str() : ms.artist;
    out.album = !ctx.track_album.empty() ? ctx.track_album.str() : ms.album;
  }

  out.speaker_entity = !ms.active_speaker.empty() ? ms.active_speaker : rs.music.active_speaker_entity;
  if (!ms.speaker_name.empty()) {
    out.speaker_name = ms.speaker_name;
  } else if (!rs.music.active_speaker_name.empty()) {
    out.speaker_name = rs.music.active_speaker_name;
  } else if (ms.is_local && ctx.friendly_name != nullptr && ctx.friendly_name[0] != '\0') {
    out.speaker_name = ctx.friendly_name;
  } else {
    out.speaker_name = "This Device";
  }

  const std::string_view mt = !ms.media_type.empty()
                                  ? std::string_view(ms.media_type)
                                  : std::string_view(rs.music.active_playback_media_type);
  out.is_speech = is_speech_media_type(mt);
  return out;
}

ArtImageView select_music_art(const UiFrameContext &ctx) {
  const auto &ms = media_state();
  if (!ms.is_local && ctx.remote_album_art.valid()) return ctx.remote_album_art;
  if (ms.is_local && ctx.album_art.valid()) return ctx.album_art;
  return ctx.remote_album_art.valid() ? ctx.remote_album_art : ctx.album_art;
}

void render_music_art_or_wallpaper_base(esphome::display::Display &it, const UiFrameContext &ctx,
                                        bool has_controls_scrim, bool has_metadata_scrim,
                                        int dim_level) {
  const auto &rs = runtime_state();
  const auto &ac = area_ctx_state();
  const UiStrView art_mode = ctx.music_fullscreen_art_opt;
  if (art_mode == "None") {
    const bool has_wall = (!ac.wallpaper_url.empty() &&
                           ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                           ctx.home_wallpaper_art.valid());
    render_page_wallpaper_base(it, ctx.home_wallpaper_art, has_wall, ctx.cx, ctx.col_bg, dim_level);
  } else {
    const bool is_standard = (art_mode == "Standard") && (dim_level == 0);
    const bool is_letterbox = (art_mode == "Letterbox") && (dim_level == 0);
    render_album_art_canvas(it, select_music_art(ctx), ctx.cx, is_standard, is_letterbox,
                            has_controls_scrim, has_metadata_scrim, dim_level);
    frame_base(it, ctx.col_bg);
  }
}

void draw_glass_pill_in_lane(esphome::display::Display &it,
                             int lane_x0, int lane_y0, int lane_w, int lane_h, int vis_h,
                             esphome::Color bg_c, esphome::Color brd_c, esphome::Color txt_c,
                             esphome::display::BaseFont *font, const char *label,
                             std::string_view leading_mdi = {}) {
  const int eh = std::clamp(vis_h, 26, lane_h);
  const int ey0 = lane_y0 + (lane_h - eh) / 2;
  const int r = eh / 2;
  render_card_box(it, lane_x0, ey0, lane_w, eh, bg_c, brd_c, r, GlassVariant::Clear, txt_c);
  const int cy = ey0 + eh / 2;
  const int cx = lane_x0 + lane_w / 2;
  if (!leading_mdi.empty()) {
    const int icon_px = std::clamp(scale_s(17), 14, eh - 10);
    if (label == nullptr || label[0] == '\0') {
      draw_music_mdi(it, leading_mdi, cx, cy, icon_px, txt_c, true);
    } else {
      const int max_txt_w = std::max(20, lane_w - icon_px - 18);
      const std::string fit = fit_text_to_width_font(std::string(label), max_txt_w, font);
      int tw = 0, xo = 0, b = 0, th = 0;
      if (font != nullptr) font->measure(fit.c_str(), &tw, &xo, &b, &th);
      const int total_w = icon_px + 6 + tw;
      const int start_x = cx - total_w / 2;
      draw_music_mdi(it, leading_mdi, start_x + icon_px / 2, cy, icon_px, txt_c, true);
      draw_shadow_print(it, start_x + icon_px + 6, cy, font, txt_c,
                        esphome::display::TextAlign::CENTER_LEFT, fit.c_str());
    }
  } else if (label != nullptr && label[0] != '\0' && font != nullptr) {
    const std::string fit = fit_text_to_width_font(std::string(label), std::max(20, lane_w - 14), font);
    draw_shadow_print(it, cx, cy, font, txt_c, esphome::display::TextAlign::CENTER, fit.c_str());
  }
}

void render_music_vertical_pips(esphome::display::Display &it, bool show_up, bool show_down,
                                bool above_mode_bar, const UiFrameContext &ctx) {
  if (!show_up && !show_down) return;
  if (!above_mode_bar) {
    render_vertical_page_pips(it, show_up, show_down, ctx.col_bg, ctx.theme_primary, ctx.col_dark_gray);
    return;
  }

  const auto &ly = layout();
  int mask_x = 0, chev_x = 0, hit_x0 = 0, hit_w = 0, min_hit_h = 0;
  compute_vertical_page_pips_x(SCREEN_W, LATTER_ROUND_DISPLAY != 0, LATTER_PANEL_PPI,
                               mask_x, chev_x, hit_x0, hit_w, min_hit_h);
  const int up_y = scale_y(134);
  const int dn_y = scale_y(236);
  if (!backdrop_active() && mask_x < SCREEN_W) {
    fill_rect_row_major(it, mask_x, up_y - scale_y(20), SCREEN_W - mask_x,
                        (dn_y - up_y) + scale_y(40), ctx.col_bg);
  }

  const int top_y0 = scale_y(68);
  const int mode_hit_top = std::min(ly.mode_bar_zone_top(), ly.app_dock_y - ly.dock_r - scale_y(12));
  const int bot_y1 = std::max(top_y0 + 2 * min_hit_h, mode_hit_top - 2);
  const int mid_y = (top_y0 + bot_y1) / 2;

  if (show_up) {
    render_scroll_chevron(it, chev_x, up_y, true, ctx.theme_primary, kChromeShadow, backdrop_active());
    const int uh = std::max(min_hit_h, mid_y - top_y0);
    frame_hits().add_rect(hit_x0, mid_y - uh, hit_w, uh, WidgetId::CHEVRON_UP, kHitTap);
  }
  if (show_down) {
    render_scroll_chevron(it, chev_x, dn_y, false, ctx.theme_primary, kChromeShadow, backdrop_active());
    const int dh = std::max(min_hit_h, bot_y1 - mid_y);
    frame_hits().add_rect(hit_x0, mid_y, hit_w, std::min(mode_hit_top - mid_y, dh),
                          WidgetId::CHEVRON_DOWN, kHitTap);
  }
}

// ------------------------------------------------------------------------------
// Unified 9-Slot Peer Stadium Mode Bar Conveyor (0..8, 4-Slot Sliding Window — D-059 / D-060)
//   0 Now Playing (Play/Pause) · 1 Library (Hamburger) · 2 Speakers · 3 Volume
//   4 Queue                    · 5 Lyrics              · 6 Album    · 7 Info
//   8 Settings (Music Settings)
// ------------------------------------------------------------------------------
void render_music_stadium_mode_bar(esphome::display::Display &it, const UiFrameContext &ctx,
                                   bool is_speech_media) {
  using esphome::Color;
  auto &m = runtime_state().music;
  const auto &ms = media_state();
  const int cx = ctx.cx;

  static constexpr int kMusicModeSlotCount = kMusicPeerTabCount;
  m.sync_carousel_for_view();
  const int start_idx = m.mode_bar_win_start;
  const int vis_count = std::min(4, kMusicModeSlotCount - start_idx);

  int win_xs[4] = {0, 0, 0, 0};
  bool has_l = false;
  bool has_r = false;
  get_mode_bar_window_xs(kMusicModeSlotCount, start_idx, cx, win_xs, has_l, has_r);

  const int active_global = std::clamp(static_cast<int>(m.view), 0, kMusicModeSlotCount - 1);
  const int active_vis = (active_global >= start_idx && active_global < start_idx + vis_count)
                             ? (active_global - start_idx)
                             : -1;
  render_in_app_mode_bar_track(it, cx, win_xs, vis_count, active_vis,
                               has_l, has_r, kMusicModeSlotCount, active_global,
                               ctx.theme_primary, ctx.col_white, start_idx);

  const int by = layout().app_dock_y;
  const int ir = ui::dock_icon_r(layout().dock_r);
  const int mdi_sz = std::max(16, 2 * ir);
  const int left_cx = win_xs[0];
  const int right_cx = win_xs[vis_count - 1];
  const int stride = get_dock_slot_stride(win_xs, vis_count);
  const float anim_start = mode_bar_anim().cur_start;

  for (int g_idx = 0; g_idx < kMusicModeSlotCount; ++g_idx) {
    const float rel_slot = static_cast<float>(g_idx) - anim_start;
    if (rel_slot < -0.55f || rel_slot > static_cast<float>(vis_count - 1) + 0.55f) continue;
    const int bx = left_cx + static_cast<int>(std::roundf(rel_slot * static_cast<float>(stride)));
    if (bx < left_cx - scale_x(16) || bx > right_cx + scale_x(16)) continue;

    const Color raw_c = (g_idx == active_global) ? ctx.theme_primary : ctx.col_white;
    float alpha = 1.0f;
    if (rel_slot < 0.0f) {
      alpha = std::max(0.25f, 1.0f + rel_slot * 1.4f);
    } else if (rel_slot > static_cast<float>(vis_count - 1)) {
      alpha = std::max(0.25f, 1.0f - (rel_slot - static_cast<float>(vis_count - 1)) * 1.4f);
    }
    const Color fg_c(static_cast<uint8_t>(16 + (raw_c.r - 16) * alpha),
                     static_cast<uint8_t>(20 + (raw_c.g - 20) * alpha),
                     static_cast<uint8_t>(27 + (raw_c.b - 27) * alpha));
    const bool sh = (alpha > 0.65f);

    switch (g_idx) {
      case 0:  // Now Playing — Combo Play/Pause SDF vector icon
        ui::draw_icon_play_pause(it, bx, by, ir, fg_c, sh);
        break;
      case 1:  // Library — Standard Hamburger SDF vector icon
        ui::draw_icon_hamburger(it, bx, by, ir, fg_c, sh);
        break;
      case 2:  // Speakers
        draw_music_mdi(it, "mdi:speaker-multiple", bx, by, mdi_sz, fg_c, sh);
        break;
      case 3:  // Volume
        draw_music_mdi(it, ms.muted ? "mdi:volume-off" : "mdi:volume-high", bx, by, mdi_sz, fg_c, sh);
        break;
      case 4:  // Queue
        draw_music_mdi(it, "mdi:playlist-play", bx, by, mdi_sz, fg_c, sh);
        break;
      case 5:  // Lyrics
        draw_music_mdi(it, "mdi:script-text", bx, by, mdi_sz, fg_c, sh);
        break;
      case 6:  // Album (or Episodes on podcast/audiobook)
        draw_music_mdi(it, is_speech_media ? "mdi:podcast" : "mdi:album", bx, by, mdi_sz, fg_c, sh);
        break;
      case 7:  // Info
        draw_music_mdi(it, "mdi:information-outline", bx, by, mdi_sz, fg_c, sh);
        break;
      case 8:  // Settings
      default:
        draw_music_mdi(it, "mdi:cog-outline", bx, by, mdi_sz, fg_c, sh, "mdi:tune-variant");
        break;
    }
  }
}

// ------------------------------------------------------------------------------
// Floating Takeover 1: Track Scrubber (MusicTakeover::SCRUB — Zero Outer Box)
// ------------------------------------------------------------------------------
void render_scrub_takeover(esphome::display::Display &it, const UiFrameContext &ctx,
                           const ResolvedTrackStrings &trk, float live_pos_sec, float dur_sec) {
  using esphome::display::TextAlign;

  const auto &ms = media_state();
  const MusicScrubGeom g = compute_music_scrub_geom(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);
  register_music_scrub_hits(frame_hits(), g);

  const std::string title_lbl = trk.title.empty() ? std::string("Track Scrubber") : trk.title;
  const std::string fit_t = fit_text_to_width_font(title_lbl, g.hdr_max_w, ctx.font_small);
  draw_shadow_print(it, g.cx, g.hdr_title_y, ctx.font_small, ctx.col_white,
                    TextAlign::CENTER, fit_t.c_str(), true);

  char el_buf[16], du_buf[16], time_lbl[40];
  format_mmss((live_pos_sec >= 0.0f) ? static_cast<int>(live_pos_sec) : 0, el_buf, sizeof(el_buf));
  format_mmss((dur_sec > 0.5f) ? static_cast<int>(dur_sec + 0.5f) : -1, du_buf, sizeof(du_buf));
  std::snprintf(time_lbl, sizeof(time_lbl), "%s  /  %s", el_buf, du_buf);
  draw_shadow_print(it, g.cx, g.hdr_time_y, ctx.font_small, ctx.col_white,
                    TextAlign::CENTER, time_lbl, true);

  // Wide horizontal seek rail with glowing AA thumb over scrimmed Album Art
  const int track_h = (SCREEN_W >= 640) ? 12 : 8;
  const int sl_cy = g.slider_y0 + g.slider_h / 2;
  const float prog = (dur_sec > 0.5f) ? std::clamp(live_pos_sec / dur_sec, 0.0f, 1.0f) : 0.0f;
  fill_round_rect_aa(it, g.slider_x0, sl_cy - track_h / 2, g.slider_w, track_h,
                     static_cast<float>(track_h / 2), kChromeTrack, kChromeTrack);
  const int fill_w = static_cast<int>(std::lround(prog * static_cast<float>(g.slider_w)));
  if (fill_w > 2) {
    fill_round_rect_aa(it, g.slider_x0, sl_cy - track_h / 2, fill_w, track_h,
                       static_cast<float>(track_h / 2), ctx.theme_primary, ctx.theme_secondary);
  }
  const int thumb_r = std::clamp(scale_s(11), 9, 16);
  const int thumb_cx = std::clamp(g.slider_x0 + fill_w, g.slider_x0 + thumb_r, g.slider_x0 + g.slider_w - thumb_r);
  ui::draw_aa_disc(it, static_cast<float>(thumb_cx), static_cast<float>(sl_cy),
                   static_cast<float>(thumb_r + 2), kChromeShadow, 155);
  ui::draw_aa_disc(it, static_cast<float>(thumb_cx), static_cast<float>(sl_cy),
                   static_cast<float>(thumb_r), ctx.col_white, 255);
  ui::draw_aa_disc(it, static_cast<float>(thumb_cx), static_cast<float>(sl_cy),
                   static_cast<float>(std::max(3, thumb_r - 4)), ctx.theme_primary, 255);

  // Individual frosted-glass -15s / +30s jump pills
  draw_glass_pill_in_lane(it, g.step_back_x0, g.step_y0, g.step_btn_w, g.step_h, g.step_vis_h,
                          ui::chrome_card_bg_off(), ui::chrome_card_brd_off(), ctx.col_white,
                          ctx.font_small, "-15s", "mdi:rewind-15");
  draw_glass_pill_in_lane(it, g.step_fwd_x0, g.step_y0, g.step_btn_w, g.step_h, g.step_vis_h,
                          ui::chrome_card_bg_off(), ui::chrome_card_brd_off(), ctx.col_white,
                          ctx.font_small, "+30s", "mdi:fast-forward-30");

  // Individual Shuffle / Repeat / Like frosted-glass pills in crisp font_small
  const bool shuf_on = ms.shuffle;
  const bool rep_on = (ms.repeat_mode != static_cast<uint8_t>(MusicRepeatMode::OFF));
  const bool fav_on = ms.favorite;
  const char *rep_lbl = music_repeat_mode_label(ms.repeat_mode);
  const std::string_view rep_icon =
      (ms.repeat_mode == static_cast<uint8_t>(MusicRepeatMode::ONE)) ? "mdi:repeat-once" : "mdi:repeat";

  const bool compact_pills = (g.mode_btn_w < 132);
  draw_glass_pill_in_lane(it, g.shuf_x0, g.mode_y0, g.mode_btn_w, g.mode_h, g.mode_vis_h,
                          shuf_on ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                          shuf_on ? ctx.theme_primary : ui::chrome_card_brd_off(),
                          shuf_on ? ctx.theme_primary : ctx.col_white,
                          ctx.font_small, compact_pills ? (shuf_on ? "On" : "Shuf") : "Shuffle",
                          "mdi:shuffle-variant");
  draw_glass_pill_in_lane(it, g.rep_x0, g.mode_y0, g.mode_btn_w, g.mode_h, g.mode_vis_h,
                          rep_on ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                          rep_on ? ctx.theme_primary : ui::chrome_card_brd_off(),
                          rep_on ? ctx.theme_primary : ctx.col_white,
                          ctx.font_small, rep_lbl, rep_icon);
  draw_glass_pill_in_lane(it, g.fav_x0, g.mode_y0, g.mode_btn_w, g.mode_h, g.mode_vis_h,
                          fav_on ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                          fav_on ? ctx.col_red : ui::chrome_card_brd_off(),
                          fav_on ? ctx.col_red : ctx.col_white,
                          ctx.font_small, (g.mode_btn_w < 112) ? "Like" : (fav_on ? "Saved" : "Like"),
                          fav_on ? "mdi:heart" : "mdi:heart-outline");
}

// ------------------------------------------------------------------------------
// Floating Takeover 2: Enqueue / Queue Action Sheet (MusicTakeover::ACTION_SHEET)
// Zero outer square glass container & zero underlying list buttons rendered!
// ------------------------------------------------------------------------------
void render_action_sheet_takeover(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::display::TextAlign;
  const auto &m = runtime_state().music;
  const auto &ly = layout();

  static constexpr uint16_t kLibWids[5] = {
      WidgetId::MUSIC_ACTION_PLAY_NOW,
      WidgetId::MUSIC_ACTION_PLAY_NEXT,
      WidgetId::MUSIC_ACTION_ADD_QUEUE,
      WidgetId::MUSIC_ACTION_REPLACE_QUEUE,
      WidgetId::MUSIC_ACTION_START_RADIO,
  };
  static constexpr const char *kLibLabels[5] = {
      "Play Now", "Play Next", "Add to Queue", "Replace Queue", "Start Radio"};
  static constexpr const char *kLibIcons[5] = {
      "mdi:play", "mdi:playlist-play", "mdi:playlist-plus", "mdi:playlist-edit", "mdi:radio-tower"};

  static constexpr uint16_t kQueueWids[5] = {
      WidgetId::MUSIC_ACTION_PLAY_NOW,
      WidgetId::MUSIC_ACTION_PLAY_NEXT,
      WidgetId::MUSIC_ACTION_MOVE_UP,
      WidgetId::MUSIC_ACTION_MOVE_DOWN,
      WidgetId::MUSIC_ACTION_REMOVE_QUEUE,
  };
  static constexpr const char *kQueueLabels[5] = {
      "Play Now", "Play Next", "Move Up", "Move Down", "Remove"};
  static constexpr const char *kQueueIcons[5] = {
      "mdi:play", "mdi:skip-next-circle-outline", "mdi:arrow-up", "mdi:arrow-down", "mdi:playlist-remove"};

  const bool is_q = m.action_sheet_is_queue;
  const uint16_t *wids = is_q ? kQueueWids : kLibWids;
  const char *const *labels = is_q ? kQueueLabels : kLibLabels;
  const char *const *icons = is_q ? kQueueIcons : kLibIcons;

  const MusicActionSheetGeom g = compute_music_action_sheet_geom(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI, 5);
  register_music_action_sheet_hits(frame_hits(), g, wids);

  const std::string hdr_raw = m.action_sheet_name.empty()
                                  ? std::string(is_q ? "Queue Item" : "Enqueue Options")
                                  : m.action_sheet_name;
  const std::string fit_hdr = fit_text_to_width_font(hdr_raw, g.hdr_max_w, ctx.font_small);
  draw_shadow_print(it, g.cx, g.hdr_title_y, ctx.font_small,
                    ctx.col_white, TextAlign::CENTER, fit_hdr.c_str(), true);

  for (int i = 0; i < g.item_count; ++i) {
    const bool is_danger = (wids[i] == WidgetId::MUSIC_ACTION_REMOVE_QUEUE);
    const bool is_primary = (i == 0);
    const esphome::Color bg = is_danger ? ui::chrome_danger_bg()
                                        : (is_primary ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off());
    const esphome::Color brd = is_danger ? ctx.col_red
                                         : (is_primary ? ctx.theme_primary : ui::chrome_card_brd_off());
    const esphome::Color fg = is_danger ? ctx.col_red : ctx.col_white;
    draw_glass_pill_in_lane(it, g.item_x0[i], g.item_y0[i], g.item_w[i], g.item_h[i], g.item_vis_h,
                            bg, brd, fg, ctx.font_small, labels[i], icons[i]);
  }

  render_back_button(it, ctx.cx, ctx.font_small, ly.back_y, "< Cancel");
}

// ------------------------------------------------------------------------------
// Peer Tab 0: Now Playing (MusicView::NOW_PLAYING)
// ------------------------------------------------------------------------------
void render_now_playing_view(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  auto &rs = runtime_state();
  const auto &ms = media_state();
  const ResolvedTrackStrings trk = resolve_track_strings(ctx);

  const bool always_vis = (ctx.music_overlay_style_opt == "Always Visible");
  const bool scrub_open = (rs.music.takeover == MusicTakeover::SCRUB);
  const bool show_controls = always_vis || rs.music.hud_active;
  const bool show_metadata = always_vis || (ctx.music_overlay_style_opt == "Art + Metadata") || rs.music.hud_active;

  // Normal HUD-scrimmed Album Art (dim_level = 0 even when SCRUB is open — D-043 / D-048 / D-059 / D-060)
  render_music_art_or_wallpaper_base(it, ctx, show_controls || scrub_open,
                                     show_metadata || scrub_open, /*dim_level=*/0);

  const float dur_sec = (!std::isnan(ms.duration) && ms.duration > 0.0f) ? ms.duration : 0.0f;
  float live_pos_sec = static_cast<float>(ms.interpolated_position_ms(ctx.now_ms)) * 0.001f;
  if (scrub_open && rs.music.pos_sync_time != 0U) {
    live_pos_sec = rs.music.pos_base;
    if (trk.is_playing && ctx.now_ms > rs.music.pos_sync_time) {
      live_pos_sec += static_cast<float>(ctx.now_ms - rs.music.pos_sync_time) * 0.001f;
    }
    if (dur_sec > 0.0f && live_pos_sec > dur_sec) live_pos_sec = dur_sec;
  }

  if (scrub_open) {
    render_scrub_takeover(it, ctx, trk, live_pos_sec, dur_sec);
    return;
  }

  const MusicNowPlayingGeom g = compute_music_now_playing_geom(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);

  // 1. Top Third: Metadata + Slim Capsule Progress Bar + MM:SS Timestamps (all crisp font_small — D-060)
  if (show_metadata) {
    const int title_span = safe_span(g.title_y, 20);
    const std::string raw_title = !trk.title.empty()
                                      ? trk.title
                                      : (trk.speaker_entity.empty() ? std::string("Select Speaker")
                                                                    : std::string("Ready to Play"));
    const std::string fit_title = fit_text_to_width_font(raw_title, title_span, ctx.font_small);
    draw_shadow_print(it, ctx.cx, g.title_y, ctx.font_small, ctx.col_white,
                      TextAlign::CENTER, fit_title.c_str());

    std::string sub_line;
    if (!trk.artist.empty() && !trk.album.empty()) {
      sub_line = trk.artist + " • " + trk.album;
    } else if (!trk.artist.empty()) {
      sub_line = trk.artist;
    } else if (!trk.album.empty()) {
      sub_line = trk.album;
    } else {
      sub_line = kUnknownGlyph;
    }
    const int sub_span = safe_span(g.sub_y, 20);
    const std::string fit_sub = fit_text_to_width_font(sub_line, sub_span, ctx.font_small);
    const Color sub_col = (!trk.title.empty() && trk.is_playing) ? ctx.theme_secondary : ctx.col_slate;
    draw_shadow_print(it, ctx.cx, g.sub_y, ctx.font_small, sub_col,
                      TextAlign::CENTER, fit_sub.c_str());

    // Slim horizontal capsule progress bar + MM:SS elapsed / duration across ALL geometries
    const float prog = (dur_sec > 0.5f) ? std::clamp(live_pos_sec / dur_sec, 0.0f, 1.0f) : 0.0f;
    const float cap_r = static_cast<float>(g.prog_h) * 0.5f;
    fill_round_rect_aa(it, g.prog_x0, g.prog_y, g.prog_w, g.prog_h, cap_r,
                       kChromeBarTrack, kChromeTrack);
    const int fill_w = static_cast<int>(std::lround(prog * static_cast<float>(g.prog_w)));
    if (fill_w > 1) {
      fill_round_rect_aa(it, g.prog_x0, g.prog_y, fill_w, g.prog_h, cap_r,
                         ctx.theme_primary, ctx.theme_secondary);
    }

    char el_buf[16], du_buf[16];
    format_mmss((!trk.title.empty() || live_pos_sec > 0.5f) ? static_cast<int>(live_pos_sec) : 0,
                el_buf, sizeof(el_buf));
    format_mmss((dur_sec > 0.5f) ? static_cast<int>(dur_sec + 0.5f) : -1, du_buf, sizeof(du_buf));
    draw_shadow_print(it, g.prog_x0, g.time_y, ctx.font_small, ctx.col_slate,
                      TextAlign::CENTER_LEFT, el_buf);
    draw_shadow_print(it, g.prog_x0 + g.prog_w, g.time_y, ctx.font_small, ctx.col_slate,
                      TextAlign::CENTER_RIGHT, du_buf);
  }

  // 2. Center: Frosted-Glass Transport Controls + Bottom Speaker Caption & 9-Slot Mode Bar
  if (show_controls) {
    render_glass_circle(it, g.prev_cx, g.transport_cy, g.side_r,
                        ui::chrome_card_bg_off(), ui::chrome_card_brd_off(),
                        GlassVariant::Clear, ctx.col_white);
    render_glass_circle(it, g.play_cx, g.transport_cy, g.play_r,
                        trk.is_playing ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                        trk.is_playing ? ctx.theme_primary : ctx.col_white,
                        GlassVariant::Clear, ctx.col_white);
    render_glass_circle(it, g.next_cx, g.transport_cy, g.side_r,
                        ui::chrome_card_bg_off(), ui::chrome_card_brd_off(),
                        GlassVariant::Clear, ctx.col_white);

    const int side_ic_sz = std::max(18, (g.side_r * 11) / 10);
    const int hero_ic_sz = std::max(24, (g.play_r * 12) / 10);
    draw_music_mdi(it, trk.is_speech ? "mdi:rewind-10" : "mdi:skip-previous",
                   g.prev_cx, g.transport_cy, side_ic_sz, ctx.col_white, true);
    draw_music_mdi(it, trk.is_playing ? "mdi:pause" : "mdi:play",
                   g.play_cx, g.transport_cy, hero_ic_sz, ctx.col_white, true);
    draw_music_mdi(it, trk.is_speech ? "mdi:fast-forward-30" : "mdi:skip-next",
                   g.next_cx, g.transport_cy, side_ic_sz, ctx.col_white, true);

    // Readable Active Speaker caption line above Mode Bar (doubles as 2.5 s feedback toast; zero passive glass box)
    const bool show_toast = !rs.music.action_feedback.empty() &&
                            (ctx.now_ms >= rs.music.feedback_timer) &&
                            ((ctx.now_ms - rs.music.feedback_timer) < 2500U);
    const int cap_span = safe_span(g.spk_caption_y, 24);
    if (show_toast) {
      const std::string fit_fb = fit_text_to_width_font(rs.music.action_feedback, cap_span, ctx.font_small);
      draw_shadow_print(it, ctx.cx, g.spk_caption_y, ctx.font_small, ctx.col_gold,
                        TextAlign::CENTER, fit_fb.c_str());
    } else {
      const Color spk_col = ms.is_local ? ctx.col_slate : ctx.theme_secondary;
      const int spk_ic = std::clamp(scale_s(17), 14, 26);
      const std::string fit_spk = fit_text_to_width_font(trk.speaker_name, std::max(40, cap_span - spk_ic - 8),
                                                         ctx.font_small);
      int tw = 0, xo = 0, b = 0, th = 0;
      if (ctx.font_small != nullptr) ctx.font_small->measure(fit_spk.c_str(), &tw, &xo, &b, &th);
      const int tot_w = spk_ic + 6 + tw;
      const int sx0 = ctx.cx - tot_w / 2;
      draw_music_mdi(it, ms.is_local ? "mdi:speaker" : "mdi:speaker-wireless",
                     sx0 + spk_ic / 2, g.spk_caption_y, spk_ic, spk_col, true);
      draw_shadow_print(it, sx0 + spk_ic + 6, g.spk_caption_y, ctx.font_small, spk_col,
                        TextAlign::CENTER_LEFT, fit_spk.c_str());
    }

    rs.music.sync_carousel_for_view();
    register_music_now_playing_hits(frame_hits(), g, show_metadata, show_controls,
                                    rs.music.takeover, rs.music.mode_bar_win_start, false);
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
    return;
  }

  register_music_now_playing_hits(frame_hits(), g, show_metadata, false, rs.music.takeover, 0, false);
}

// ------------------------------------------------------------------------------
// Peer Tab 3: Volume Peer Tab (MusicView::VOLUME — Zero Outer Glass Box & dim=0)
// ------------------------------------------------------------------------------
void render_volume_peer_tab(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &rs = runtime_state();
  const auto &ms = media_state();
  const ResolvedTrackStrings trk = resolve_track_strings(ctx);

  // Same normal HUD-scrimmed Album Art as Now Playing (dim_level = 0, zero extra darkening!)
  render_music_art_or_wallpaper_base(it, ctx, /*has_controls_scrim=*/true,
                                     /*has_metadata_scrim=*/true, /*dim_level=*/0);

  const MusicVolumeGeom g = compute_music_volume_geom(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);
  register_music_volume_hits(frame_hits(), g);

  const int vol_pct = std::clamp(ms.is_local ? ctx.spk_vol_pct : rs.music.remote_vol_pct, 0, 100);
  const std::string spk_fit = fit_text_to_width_font(trk.speaker_name, g.hdr_max_w, ctx.font_small);
  draw_shadow_print(it, g.cx, g.hdr_title_y, ctx.font_small, ctx.col_white,
                    TextAlign::CENTER, spk_fit.c_str(), true);

  char pct_buf[24];
  if (ms.muted) {
    std::snprintf(pct_buf, sizeof(pct_buf), "Muted (%d%%)", vol_pct);
  } else {
    std::snprintf(pct_buf, sizeof(pct_buf), "Volume %d%%", vol_pct);
  }
  draw_shadow_print(it, g.cx, g.hdr_pct_y, ctx.font_small,
                    ms.muted ? ctx.col_gold : ctx.col_white, TextAlign::CENTER, pct_buf, true);

  // Tall vertical frosted-glass volume slider rail (only interactive controls are glass!)
  const int sl_r = std::min(g.slider_w / 2, scale_s(24));
  render_card_box(it, g.slider_x0, g.slider_y0, g.slider_w, g.slider_h,
                  kChromeTrack, kChromeTrackBorder, sl_r, GlassVariant::Clear, ctx.col_white);
  const float frac = ms.muted ? 0.0f : (static_cast<float>(vol_pct) * 0.01f);
  const int fill_h = static_cast<int>(std::lround(frac * static_cast<float>(g.slider_h - 8)));
  if (fill_h >= 6) {
    const int fy0 = g.slider_y0 + g.slider_h - 4 - fill_h;
    const Color fill_c = mix_color_q8(ctx.theme_primary, ctx.col_white, 48);
    fill_round_rect_aa(it, g.slider_x0 + 4, fy0, g.slider_w - 8, fill_h,
                       static_cast<float>(std::min((g.slider_w - 8) / 2, sl_r - 2)),
                       fill_c, ctx.theme_primary);
  }
  const int thumb_y = std::clamp(
      g.slider_y0 + g.slider_h - 6 - static_cast<int>(std::lround(frac * static_cast<float>(g.slider_h - 12))),
      g.slider_y0 + 8, g.slider_y0 + g.slider_h - 8);
  fill_round_rect_aa(it, g.slider_x0 + 8, thumb_y - 3, g.slider_w - 16, 6, 3.0f,
                     ctx.col_white, ctx.col_white);
  draw_music_mdi(it, ms.muted ? "mdi:volume-off" : "mdi:volume-high",
                 g.slider_x0 + g.slider_w / 2, g.slider_y0 + g.slider_h - scale_y(20),
                 scale_s(18), ctx.col_white, true);

  // Individual frosted-glass +X%, -X%, and Mute pills in consistent font_small
  const int step_v = std::clamp(ctx.vol_step_pct, 1, 25);
  char up_lbl[16], dn_lbl[16];
  std::snprintf(up_lbl, sizeof(up_lbl), "+%d%%", step_v);
  std::snprintf(dn_lbl, sizeof(dn_lbl), "-%d%%", step_v);

  draw_glass_pill_in_lane(it, g.btn_x0, g.up_y0, g.btn_w, g.btn_h, g.btn_vis_h,
                          ui::chrome_card_bg_off(), ui::chrome_card_brd_off(), ctx.col_white,
                          ctx.font_small, up_lbl, "mdi:volume-plus");
  draw_glass_pill_in_lane(it, g.btn_x0, g.down_y0, g.btn_w, g.btn_h, g.btn_vis_h,
                          ui::chrome_card_bg_off(), ui::chrome_card_brd_off(), ctx.col_white,
                          ctx.font_small, dn_lbl, "mdi:volume-minus");
  draw_glass_pill_in_lane(it, g.btn_x0, g.mute_y0, g.btn_w, g.btn_h, g.btn_vis_h,
                          ms.muted ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                          ms.muted ? ctx.col_gold : ui::chrome_card_brd_off(),
                          ms.muted ? ctx.col_gold : ctx.col_white,
                          ctx.font_small, ms.muted ? "Unmute" : "Mute",
                          ms.muted ? "mdi:volume-off" : "mdi:volume-mute");

  render_music_stadium_mode_bar(it, ctx, trk.is_speech);
}

// ------------------------------------------------------------------------------
// Peer Tab 5: Ambient Full-Screen Synced Lyrics (MusicView::LYRICS)
// ------------------------------------------------------------------------------
void render_lyrics_view(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  auto &rs = runtime_state();
  auto &lyr = rs.music.lyrics;
  const auto &ms = media_state();
  const auto &ac = area_ctx_state();
  const auto &ly = layout();
  const ResolvedTrackStrings trk = resolve_track_strings(ctx);

  // Full-bleed dimmed cover art backdrop — ZERO passive glass card behind lyrics text (D-043 / D-048)!
  if (ctx.music_fullscreen_art_opt == "None") {
    const bool has_wall = (!ac.wallpaper_url.empty() &&
                           ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                           ctx.home_wallpaper_art.valid());
    render_page_wallpaper_base(it, ctx.home_wallpaper_art, has_wall, ctx.cx, ctx.col_bg, kArtDimLyrics);
  } else {
    render_lyrics_art_backdrop(it, select_music_art(ctx), ctx.cx, ctx.theme_primary, ctx.col_bg);
  }

  // Auto-fetch / track-advance refresh when in LYRICS view
  const std::string cur_key = build_music_track_key(trk.title, trk.artist);
  static uint32_t s_last_auto_lyrics_ms = 0U;
  if (!cur_key.empty() && lyr.track_key != cur_key && !lyr.loading &&
      (s_last_auto_lyrics_ms == 0U || (ctx.now_ms - s_last_auto_lyrics_ms) > 1500U)) {
    s_last_auto_lyrics_ms = (ctx.now_ms == 0U) ? 1U : ctx.now_ms;
    lyr.track_key = cur_key;
    lyr.loading = true;
    lyr.lines.clear();
    lyr.active_line_idx = -1;
    lyr.scroll_px = 0;
    lyr.target_scroll_px = 0;
    lyr.scroll_anim_start_ms = 0U;
    latter_actions::emit_music_lyrics_fetch(cur_key, trk.speaker_entity, ctx.now_ms);
  }

  const bool show_hud = rs.music.hud_active;
  const int min_hit = mm_to_px(7.0f);
  int vp_top = LATTER_ROUND_DISPLAY ? scale_y(28) : scale_y(20);
  int vp_bot = SCREEN_H - (LATTER_ROUND_DISPLAY ? scale_y(28) : scale_y(20));

  if (show_hud) {
    std::string hdr_txt = trk.title.empty() ? std::string("Lyrics") : trk.title;
    if (!trk.artist.empty()) hdr_txt += " • " + trk.artist;
    const std::string fit_hdr = fit_text_to_width_font(hdr_txt, safe_span(ly.hdr_y, 24), ctx.font_small);
    draw_shadow_print(it, ctx.cx, ly.hdr_y, ctx.font_small, ctx.col_white,
                      TextAlign::CENTER, fit_hdr.c_str(), true);

    const bool manual_override = lyr.synced && !lyr.lines.empty() && (ctx.now_ms < lyr.manual_override_until_ms);
    int sync_hit_bottom = 0;
    if (manual_override) {
      const int sync_vis_h = std::clamp(scale_y(32), 28, 42);
      const int sync_hit_h = std::max(sync_vis_h, std::min(min_hit, scale_y(54)));
      const int sync_cy = ly.hdr_y + scale_y(30);
      const int span_w = safe_span(sync_cy, 16);
      const int sync_vis_w = std::clamp(scale_x(86), 72, 124);
      const int sync_hit_w = std::max(sync_vis_w, std::min(min_hit, span_w / 2));
      const int right_edge = ctx.cx + span_w / 2;
      const int sync_x0 = right_edge - sync_vis_w;
      render_pill_button(it, sync_x0, sync_x0 + sync_vis_w, sync_cy, sync_vis_h / 2,
                         ctx.theme_primary, ui::chrome_card_bg_on(), "Sync",
                         ctx.theme_primary, ctx.font_small, GlassVariant::Clear);
      frame_hits().add_rect(right_edge - sync_hit_w, sync_cy - sync_hit_h / 2,
                            sync_hit_w, sync_hit_h, WidgetId::MUSIC_LYRIC_SYNC_BTN,
                            kHitTap | kHitFocusSlot);
      sync_hit_bottom = sync_cy + sync_hit_h / 2 + 2;
    }
    vp_top = std::max(ly.hdr_y + scale_y(24), sync_hit_bottom);
    vp_bot = std::max(vp_top + 60, ly.mode_bar_zone_top() - scale_y(6));
  }

  const int vp_h = std::max(60, vp_bot - vp_top);

  auto finish_lyrics_chrome = [&]() {
    if (show_hud) {
      render_music_stadium_mode_bar(it, ctx, trk.is_speech);
    } else {
      frame_hits().add_rect(0, 0, SCREEN_W, SCREEN_H, WidgetId::MUSIC_ART_TAP, kHitTap);
    }
  };

  if (cur_key.empty()) {
    draw_shadow_print(it, ctx.cx, (vp_top + vp_bot) / 2, ctx.font_small, ctx.col_slate,
                      TextAlign::CENTER, "Play a track to view lyrics", true);
    finish_lyrics_chrome();
    return;
  }
  if (lyr.loading) {
    draw_shadow_print(it, ctx.cx, (vp_top + vp_bot) / 2, ctx.font_small, ctx.col_slate,
                      TextAlign::CENTER, "Loading lyrics...", true);
    finish_lyrics_chrome();
    return;
  }
  if (lyr.lines.empty()) {
    draw_shadow_print(it, ctx.cx, (vp_top + vp_bot) / 2, ctx.font_small, ctx.col_slate,
                      TextAlign::CENTER, "No lyrics available for this track", true);
    finish_lyrics_chrome();
    return;
  }

  // Fonts & line metrics
  esphome::display::BaseFont *active_font =
      (SCREEN_W >= 640) ? ctx.font_date
                        : ((SCREEN_W >= 440) ? ctx.font_btn_bold : ctx.font_small);
  esphome::display::BaseFont *body_font = (SCREEN_W >= 640) ? ctx.font_btn_bold : ctx.font_small;
  const int active_lh = (SCREEN_W >= 640) ? 36 : ((SCREEN_W >= 440) ? 26 : 22);
  const int body_lh = (SCREEN_W >= 640) ? 28 : 21;
  const int line_gap = scale_y(12);

  // Conservative chord-safe wrap width across the active lyrical viewport
  const int anchor_screen_y = std::clamp((SCREEN_H * 38) / 100, vp_top + 28, vp_bot - 28);
  const int wrap_w = std::max(120, std::min(safe_span(anchor_screen_y, 26),
                                            safe_span(vp_top + scale_y(28), 26)));

  const uint32_t pos_ms = ms.interpolated_position_ms(ctx.now_ms);
  const int live_act_idx = lyr.synced ? lyr.find_active_line(pos_ms) : -1;

  const int n_lines = static_cast<int>(std::min<size_t>(lyr.lines.size(), kMaxMusicLyricLines));
  int doc_y0[kMaxMusicLyricLines]{};
  int doc_y1[kMaxMusicLyricLines]{};
  int doc_cy[kMaxMusicLyricLines]{};
  int cur_doc_y = scale_y(16);

  for (int i = 0; i < n_lines; ++i) {
    const bool is_act = lyr.synced && (i == live_act_idx);
    esphome::display::BaseFont *f = is_act ? active_font : body_font;
    const int lh = is_act ? active_lh : body_lh;
    std::string sub[4];
    const std::string_view txt = lyr.lines[static_cast<size_t>(i)].text.empty()
                                     ? std::string_view("♪")
                                     : std::string_view(lyr.lines[static_cast<size_t>(i)].text);
    const int sc = std::max(1, wrap_music_text_lines(txt, wrap_w, f, sub, 4));
    doc_y0[i] = cur_doc_y;
    doc_y1[i] = cur_doc_y + sc * lh;
    doc_cy[i] = (doc_y0[i] + doc_y1[i]) / 2;
    cur_doc_y = doc_y1[i] + line_gap;
  }

  const int total_doc_h = cur_doc_y + scale_y(24);
  const int anchor_in_vp = anchor_screen_y - vp_top;
  const int max_scroll = std::max(0, total_doc_h - vp_h / 2);
  step_music_lyrics_auto_scroll(lyr, pos_ms, ctx.now_ms, anchor_in_vp, doc_cy, n_lines, max_scroll);

  const int fade_band = std::max(16, scale_y(28));
  int hit_slot = 0;
  int last_hit_y1 = vp_top - 1;

  for (int i = 0; i < n_lines; ++i) {
    const int scr_y0 = vp_top + doc_y0[i] - lyr.scroll_px;
    const int scr_y1 = vp_top + doc_y1[i] - lyr.scroll_px;
    if (scr_y1 < vp_top + 4 || scr_y0 > vp_bot - 4) continue;

    const bool is_act = lyr.synced && (i == lyr.active_line_idx);
    esphome::display::BaseFont *f = is_act ? active_font : body_font;
    const int lh = is_act ? active_lh : body_lh;
    Color fg_c = ctx.col_white;
    if (lyr.synced && !is_act) {
      fg_c = (i < lyr.active_line_idx) ? mix_color_q8(ctx.col_white, ctx.col_slate, 88)
                                       : mix_color_q8(ctx.col_white, ctx.col_slate, 124);
    }

    std::string sub[4];
    const std::string_view txt = lyr.lines[static_cast<size_t>(i)].text.empty()
                                     ? std::string_view("♪")
                                     : std::string_view(lyr.lines[static_cast<size_t>(i)].text);
    const int sc = std::max(1, wrap_music_text_lines(txt, wrap_w, f, sub, 4));

    for (int s = 0; s < sc; ++s) {
      const int draw_cy = scr_y0 + s * lh + lh / 2;
      if (draw_cy < vp_top + 4 || draw_cy > vp_bot - 4) continue;
      int alpha_q8 = 256;
      if (!is_act) {
        if (draw_cy - vp_top < fade_band) {
          alpha_q8 = std::clamp(((draw_cy - vp_top) * 256) / fade_band, 56, 256);
        } else if (vp_bot - draw_cy < fade_band) {
          alpha_q8 = std::clamp(((vp_bot - draw_cy) * 256) / fade_band, 56, 256);
        }
      }
      const int row_span = safe_span(draw_cy, 20);
      const std::string clamped_line = fit_text_to_width_font(sub[s], row_span, f);
      draw_shadow_print(it, ctx.cx, draw_cy, f, fg_c, TextAlign::CENTER,
                        clamped_line.c_str(), true, alpha_q8);
    }

    if (show_hud && lyr.synced && lyr.lines[static_cast<size_t>(i)].t_ms >= 0 && hit_slot < 8) {
      const int hy0 = std::max(scr_y0 - 2, last_hit_y1 + 1);
      const int hy1 = std::min(scr_y1 + 2, vp_bot - 2);
      if (hy1 - hy0 >= 16) {
        const int hw = std::max(80, band_safe_span_for((hy0 + hy1) / 2, hy1 - hy0,
                                                       SCREEN_W, SCREEN_H,
                                                       LATTER_ROUND_DISPLAY != 0,
                                                       LATTER_IS_WEARABLE != 0, 20));
        frame_hits().add_rect(ctx.cx - hw / 2, hy0, hw, hy1 - hy0,
                              static_cast<uint16_t>(WidgetId::MUSIC_LYRIC_LINE_BASE + hit_slot),
                              kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
        last_hit_y1 = hy1;
        ++hit_slot;
      }
    }
  }

  finish_lyrics_chrome();
}

// ------------------------------------------------------------------------------
// Peer Tab 8: Music Settings (MusicView::SETTINGS — 10 s Idle Spring-Back, Zero < Back)
// ------------------------------------------------------------------------------
void render_music_settings_view(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::display::TextAlign;

  auto &rs = runtime_state();
  const auto &ly = layout();
  const ResolvedTrackStrings trk = resolve_track_strings(ctx);

  render_music_art_or_wallpaper_base(it, ctx, /*has_controls_scrim=*/true,
                                     /*has_metadata_scrim=*/true, kArtDimMenu);
  render_page_header(it, ctx.cx, "MUSIC SETTINGS", ctx.font_small, ctx.col_white, true);
  const int pg = std::clamp(rs.music.settings_page, 1, 2);

  struct SettingItem {
    const char *title;
    std::string_view value;
  };
  SettingItem items[3]{};
  int count = 0;
  if (pg == 1) {
    items[0] = {"Album Art", ctx.music_fullscreen_art_opt.sv()};
    items[1] = {"Now Playing HUD", ctx.music_overlay_style_opt.sv()};
    items[2] = {"Album Sort", ctx.music_album_sort_opt.sv()};
    count = 3;
  } else {
    items[0] = {"Artist Albums", ctx.music_artist_album_sort_opt.sv()};
    items[1] = {"Podcast Sort", ctx.music_podcast_sort_opt.sv()};
    count = 2;
  }

  const int content_top = ly.hdr_y + scale_y(24);
  const int mode_bar_bot_limit = ly.mode_bar_zone_top() - scale_y(8);
  for (int i = 0; i < count; ++i) {
    const MusicListRowGeom g = compute_music_list_row_geom(
        i, /*visible_rows=*/3, content_top, mode_bar_bot_limit,
        /*has_leading_play=*/false, /*has_pips=*/true,
        SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);
    render_card_box(it, g.card_x0, g.card_y0, g.card_w, g.card_h,
                    ui::chrome_card_bg_off(), ui::chrome_card_brd_off(),
                    scale_s(15), GlassVariant::Clear, ctx.col_white);
    const int txt_max_w = std::max(40, g.card_w - scale_x(30));
    const std::string title_fit = fit_text_to_width_font(std::string(items[i].title), txt_max_w, ctx.font_small);
    const std::string val_fit = fit_text_to_width_font(std::string(items[i].value), txt_max_w, ctx.font_small);
    const int off_y = std::max(9, scale_y(11));
    draw_shadow_print(it, g.card_x0 + scale_x(15), g.cy_row - off_y, ctx.font_small, ctx.col_slate,
                      TextAlign::CENTER_LEFT, title_fit.c_str());
    draw_shadow_print(it, g.card_x0 + scale_x(15), g.cy_row + off_y, ctx.font_small, ctx.theme_primary,
                      TextAlign::CENTER_LEFT, val_fit.c_str());
    register_music_list_row_hits(frame_hits(), g, i, /*enable_long_press=*/false);
  }

  render_music_vertical_pips(it, pg > 1, pg < 2, /*above_mode_bar=*/true, ctx);
  render_music_stadium_mode_bar(it, ctx, trk.is_speech);
}

// ------------------------------------------------------------------------------
// Peer Tab 7: Track & Artist Info (MusicView::INFO — Zero Passive Glass Box D-048 / D-060)
// ------------------------------------------------------------------------------
void render_info_view(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::display::TextAlign;

  auto &rs = runtime_state();
  const auto &ms = media_state();
  const auto &ac = area_ctx_state();
  const ResolvedTrackStrings trk = resolve_track_strings(ctx);

  if (ctx.music_fullscreen_art_opt == "None") {
    const bool has_wall = (!ac.wallpaper_url.empty() &&
                           ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                           ctx.home_wallpaper_art.valid());
    render_page_wallpaper_base(it, ctx.home_wallpaper_art, has_wall, ctx.cx, ctx.col_bg, kArtDimLyrics);
  } else {
    render_lyrics_art_backdrop(it, select_music_art(ctx), ctx.cx, ctx.theme_primary, ctx.col_bg);
  }

  const bool show_hud = rs.music.hud_active;
  const MusicInfoGeom g = compute_music_info_geom(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);

  int vp_top = LATTER_ROUND_DISPLAY ? scale_y(30) : scale_y(22);
  int vp_bot = g.vp_bot_ambient;

  if (show_hud) {
    render_page_header(it, ctx.cx, "TRACK INFO", ctx.font_small, ctx.col_white, true);
    vp_top = g.vp_top;
    vp_bot = g.vp_bot_hud;
  }

  const int wrap_w = std::max(120, std::min(safe_span(ctx.cy, 24), safe_span(vp_top + 24, 24)));

  std::string body;
  body += "Track: " + (trk.title.empty() ? std::string(kUnknownGlyph) : trk.title) + "\n";
  body += "Artist: " + (trk.artist.empty() ? std::string(kUnknownGlyph) : trk.artist) + "\n";
  body += "Album: " + (trk.album.empty() ? std::string(kUnknownGlyph) : trk.album) + "\n";
  body += "Speaker: " + trk.speaker_name;
  if (!ms.media_type.empty()) {
    body += " (" + ms.media_type + ")";
  }
  const BrowseSlotItem *slots = rs.music_slots();
  if (rs.music.selected_category_mode == "bio") {
    for (int i = 0; i < LATTER_SLOT_COUNT; ++i) {
      if (!slots[i].name.empty() && slots[i].name != "Loading...") {
        body += "\n" + slots[i].name;
      }
    }
  }

  std::string lines[24];
  const int lc = wrap_music_text_lines(body, wrap_w, ctx.font_small, lines, 24);
  const int lh = (SCREEN_W >= 640) ? 28 : 22;
  const int max_scroll = std::max(0, lc * lh - std::max(40, vp_bot - vp_top));
  rs.music.info_scroll_px = std::clamp(rs.music.info_scroll_px, 0, max_scroll);

  for (int i = 0; i < lc; ++i) {
    const int dy = vp_top + 14 + i * lh - rs.music.info_scroll_px;
    if (dy < vp_top + 6 || dy > vp_bot - 6) continue;
    const std::string fit_l = fit_text_to_width_font(lines[i], safe_span(dy, 20), ctx.font_small);
    const esphome::Color c = (i == 0) ? ctx.col_white
                                      : ((i <= 2) ? ctx.theme_secondary
                                                  : mix_color_q8(ctx.col_white, ctx.col_slate, 72));
    draw_shadow_print(it, ctx.cx, dy, ctx.font_small, c, TextAlign::CENTER, fit_l.c_str(), true);
  }

  if (show_hud) {
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
  } else {
    frame_hits().add_rect(0, 0, SCREEN_W, SCREEN_H, WidgetId::MUSIC_ART_TAP, kHitTap);
  }
}

// ------------------------------------------------------------------------------
// Stack-Uniform Bezel-Safe List Row Helper (480 px Desk Cap, font_small — D-060)
// ------------------------------------------------------------------------------
void render_slot_list_row(esphome::display::Display &it, const UiFrameContext &ctx,
                          int row_idx, int visible_rows, int content_top, int content_bot,
                          const char *primary_txt, const char *badge_txt,
                          bool is_highlighted, bool has_leading_play,
                          bool enable_long_press, bool has_pips, bool register_hits) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const MusicListRowGeom g = compute_music_list_row_geom(
      row_idx, visible_rows, content_top, content_bot, has_leading_play, has_pips,
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);

  const Color bg = is_highlighted ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off();
  const Color brd = is_highlighted ? ctx.theme_primary : ui::chrome_card_brd_off();
  const Color fg = is_highlighted ? ctx.theme_primary : ctx.col_white;
  render_card_box(it, g.card_x0, g.card_y0, g.card_w, g.card_h,
                  bg, brd, scale_s(14), GlassVariant::Clear, fg);

  int text_x0 = g.card_x0 + scale_x(14);
  if (g.play_vis_w > 0) {
    const int play_cx = g.card_x0 + g.play_vis_w / 2;
    draw_music_mdi(it, "mdi:play", play_cx, g.cy_row,
                   std::clamp(scale_s(18), 15, g.card_h - 14), ctx.theme_primary, true);
    fast_vline(it, g.card_x0 + g.play_vis_w, g.card_y0 + 8, std::max(8, g.card_h - 16), kChromeCardBorder);
    text_x0 = g.card_x0 + g.play_vis_w + scale_x(12);
  }

  int badge_reserve_w = 0;
  const bool has_badge = (badge_txt != nullptr && badge_txt[0] != '\0');
  if (has_badge) {
    int bw = 0, xo = 0, b = 0, bh = 0;
    if (ctx.font_small != nullptr) ctx.font_small->measure(badge_txt, &bw, &xo, &b, &bh);
    badge_reserve_w = std::max(bw, scale_x(28)) + scale_x(16);
    const int bx = g.card_x0 + g.card_w - scale_x(12);
    draw_shadow_print(it, bx, g.cy_row, ctx.font_small,
                      is_highlighted ? ctx.theme_primary : ctx.col_slate,
                      TextAlign::CENTER_RIGHT, badge_txt);
  }

  const int text_right = g.card_x0 + g.card_w - scale_x(12) - badge_reserve_w;
  const int text_max_w = std::max(40, text_right - text_x0);
  const std::string clean_primary = sanitize_utf8_for_font(std::string(primary_txt != nullptr ? primary_txt : ""));

  int sw = 0, xo = 0, b = 0, sh = 0;
  if (ctx.font_small != nullptr && !clean_primary.empty()) {
    ctx.font_small->measure(clean_primary.c_str(), &sw, &xo, &b, &sh);
  }

  if (sw <= text_max_w || g.card_h < scale_y(56)) {
    const std::string fit_p = fit_text_to_width_font(clean_primary, text_max_w, ctx.font_small);
    draw_shadow_print(it, text_x0, g.cy_row, ctx.font_small, fg,
                      TextAlign::CENTER_LEFT, fit_p.c_str());
  } else {
    std::string two_lines[2];
    const int lc = wrap_music_text_lines(clean_primary, text_max_w, ctx.font_small, two_lines, 2);
    if (lc <= 1) {
      draw_shadow_print(it, text_x0, g.cy_row, ctx.font_small, fg,
                        TextAlign::CENTER_LEFT, two_lines[0].c_str());
    } else {
      const int off_y = std::max(9, scale_y(10));
      draw_shadow_print(it, text_x0, g.cy_row - off_y, ctx.font_small, fg,
                        TextAlign::CENTER_LEFT, two_lines[0].c_str());
      draw_shadow_print(it, text_x0, g.cy_row + off_y, ctx.font_small,
                        mix_color_q8(fg, ctx.col_slate, 56),
                        TextAlign::CENTER_LEFT, two_lines[1].c_str());
    }
  }

  if (register_hits) {
    register_music_list_row_hits(frame_hits(), g, row_idx, enable_long_press);
  }
}

// ------------------------------------------------------------------------------
// Peer Tab 1 (LIBRARY Bento + LIB_LIST), Tab 2 (SPEAKERS + SPEAKER_ACTIONS),
// Peer Tab 4 (QUEUE), and Peer Tab 6 (ALBUM_TRACKS)
// ------------------------------------------------------------------------------
void render_music_browse_views(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::display::TextAlign;

  auto &rs = runtime_state();
  auto &m = rs.music;
  const auto &ms = media_state();
  const auto &ly = layout();
  const ResolvedTrackStrings trk = resolve_track_strings(ctx);
  const int min_hit = mm_to_px(7.0f);
  const int mode_bar_bot_limit = ly.mode_bar_zone_top() - scale_y(8);
  const int full_list_bot_limit = (LATTER_ROUND_DISPLAY ? scale_min(404) : (SCREEN_H - scale_y(64))) - scale_y(6);

  render_music_art_or_wallpaper_base(it, ctx, /*has_controls_scrim=*/true,
                                     /*has_metadata_scrim=*/true, kArtDimMenu);

  // 1. Speakers Peer Tab (MusicView::SPEAKERS) & Speaker Actions Drilldown
  if (m.view == MusicView::SPEAKERS) {
    if (m.drilldown == MusicDrilldown::SPEAKER_ACTIONS && !m.target_speaker_id.empty()) {
      render_page_header(it, ctx.cx, m.target_speaker_name.empty() ? "SPEAKER" : m.target_speaker_name,
                         ctx.font_small, ctx.col_white, true);
      const char *labels[3] = {
          "Control Speaker",
          "Transfer Queue Here",
          (m.target_speaker_id == trk.speaker_entity) ? "Leave Speaker Group" : "Join Active Group",
      };
      const char *icons[3] = {"mdi:speaker-wireless", "mdi:swap-horizontal", "mdi:speaker-multiple"};
      const uint16_t wids[3] = {
          WidgetId::MUSIC_SPK_CONTROL_BTN,
          WidgetId::MUSIC_SPK_TRANSFER_BTN,
          WidgetId::MUSIC_SPK_GROUP_BTN,
      };
      const int safe_top = band_safe_span_for(
          ly.tool_card3_y(0), ly.tool_card3_h, SCREEN_W, SCREEN_H,
          LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 16);
      const int safe_bot = band_safe_span_for(
          ly.tool_card3_y(2), ly.tool_card3_h, SCREEN_W, SCREEN_H,
          LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 16);
      const int stack_safe_w = std::min(safe_top, safe_bot);
      const int max_w = (SCREEN_W > 520) ? 480 : 384;
      const int cw = std::clamp(std::min(max_w, stack_safe_w), std::max(140, min_hit), SCREEN_W - 24);
      const int x0 = ctx.cx - cw / 2;
      for (int i = 0; i < 3; ++i) {
        const int ym = ly.tool_card3_y(i);
        const int y0 = ym - ly.tool_card3_h / 2;
        draw_glass_pill_in_lane(it, x0, y0, cw, ly.tool_card3_h, ly.tool_card3_h,
                                (i == 0) ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                                (i == 0) ? ctx.theme_primary : ui::chrome_card_brd_off(),
                                ctx.col_white, ctx.font_small, labels[i], icons[i]);
        frame_hits().add_rect(x0, y0, cw, ly.tool_card3_h, wids[i], kHitTap | kHitFocusSlot);
      }
      render_back_button(it, ctx.cx, ctx.font_small, ly.back_y);
      return;
    }

    render_page_header(it, ctx.cx, "SPEAKERS", ctx.font_small, ctx.col_white, true);
    const int content_top = ly.hdr_y + scale_y(24);
    const int pg = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
    const int tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC);
    const bool has_pips = (pg > 1) || (pg < tot);
    const BrowseSlotItem *slots = rs.music_slots();
    int drawn = 0;
    for (int r = 0; r < kMusicPeerListPageSize; ++r) {
      if (slots[r].name.empty()) continue;
      char lvl_buf[16] = "";
      if (slots[r].level >= 0) std::snprintf(lvl_buf, sizeof(lvl_buf), "%d%%", slots[r].level);
      const bool is_cur = slots[r].active || (!slots[r].id.empty() && slots[r].id == trk.speaker_entity);
      render_slot_list_row(it, ctx, r, kMusicPeerListPageSize, content_top, mode_bar_bot_limit,
                           slots[r].name.c_str(), lvl_buf, is_cur,
                           /*has_leading_play=*/false, /*enable_long_press=*/false,
                           has_pips, /*register_hits=*/true);
      ++drawn;
    }
    if (drawn == 0) {
      draw_shadow_print(it, ctx.cx, (content_top + mode_bar_bot_limit) / 2, ctx.font_small, ctx.col_slate,
                        TextAlign::CENTER, "No speakers found", true);
    }
    render_music_vertical_pips(it, pg > 1, pg < tot, /*above_mode_bar=*/true, ctx);
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
    return;
  }

  // 2. Queue Peer Tab (MusicView::QUEUE — 4 Stack-Uniform Rows Above Mode Bar)
  if (m.view == MusicView::QUEUE) {
    render_page_header(it, ctx.cx, "PLAYBACK QUEUE", ctx.font_small, ctx.col_white, true);

    const int strip_h = std::clamp(scale_y(32), 28, 44);
    const int sy0 = ly.hdr_y + scale_y(16);
    const int strip_cy = sy0 + strip_h / 2;
    const int strip_span = band_safe_span_for(
        strip_cy, strip_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 16);
    const int max_strip_w = (SCREEN_W > 520) ? 480 : 384;
    const int strip_w = std::clamp(std::min(max_strip_w, strip_span), 180, SCREEN_W - 24);
    const int gap = scale_x(6);
    const int pw = (strip_w - 2 * gap) / 3;
    const int sx0 = ctx.cx - strip_w / 2;

    const bool shuf_on = ms.shuffle;
    const bool rep_on = (ms.repeat_mode != static_cast<uint8_t>(MusicRepeatMode::OFF));
    const bool compact_q = (pw < 118);
    char rep_txt[20];
    if (compact_q) {
      std::snprintf(rep_txt, sizeof(rep_txt), "%s", music_repeat_mode_label(ms.repeat_mode));
    } else {
      std::snprintf(rep_txt, sizeof(rep_txt), "Rep: %s", music_repeat_mode_label(ms.repeat_mode));
    }

    draw_glass_pill_in_lane(it, sx0, sy0, pw, strip_h, strip_h,
                            shuf_on ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                            shuf_on ? ctx.theme_primary : ui::chrome_card_brd_off(),
                            shuf_on ? ctx.theme_primary : ctx.col_white,
                            ctx.font_small, compact_q ? (shuf_on ? "On" : "Shuf") : (shuf_on ? "Shuf On" : "Shuffle"));
    draw_glass_pill_in_lane(it, sx0 + pw + gap, sy0, pw, strip_h, strip_h,
                            rep_on ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                            rep_on ? ctx.theme_primary : ui::chrome_card_brd_off(),
                            rep_on ? ctx.theme_primary : ctx.col_white,
                            ctx.font_small, rep_txt);
    draw_glass_pill_in_lane(it, sx0 + 2 * (pw + gap), sy0, pw, strip_h, strip_h,
                            ui::chrome_card_bg_off(), ui::chrome_card_brd_off(),
                            ctx.col_slate, ctx.font_small, "Clear");

    frame_hits().add_rect(sx0, sy0, pw, strip_h, WidgetId::MUSIC_SHUFFLE_BTN, kHitTap | kHitFocusSlot);
    frame_hits().add_rect(sx0 + pw + gap, sy0, pw, strip_h, WidgetId::MUSIC_REPEAT_BTN, kHitTap | kHitFocusSlot);
    frame_hits().add_rect(sx0 + 2 * (pw + gap), sy0, pw, strip_h, WidgetId::MUSIC_QUEUE_CLEAR, kHitTap | kHitFocusSlot);

    const int content_top = sy0 + strip_h + scale_y(6);
    const int pg = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
    const int tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC);
    const bool has_pips = (pg > 1) || (pg < tot);
    const BrowseSlotItem *slots = rs.music_slots();
    int drawn = 0;
    for (int r = 0; r < kMusicPeerListPageSize; ++r) {
      if (slots[r].name.empty()) continue;
      const bool is_now = slots[r].is_current || slots[r].active;
      const bool actionable = !slots[r].id.empty() || !slots[r].queue_item_id.empty();
      render_slot_list_row(it, ctx, r, kMusicPeerListPageSize, content_top, mode_bar_bot_limit,
                           slots[r].name.c_str(), is_now ? "NOW" : "",
                           is_now, /*has_leading_play=*/false, actionable, has_pips, actionable);
      ++drawn;
    }
    if (drawn == 0) {
      draw_shadow_print(it, ctx.cx, (content_top + mode_bar_bot_limit) / 2, ctx.font_small, ctx.col_slate,
                        TextAlign::CENTER, "Queue is empty", true);
    }
    render_music_vertical_pips(it, pg > 1, pg < tot, /*above_mode_bar=*/true, ctx);
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
    return;
  }

  // 3. Library Root Peer Tab (MusicView::LIBRARY && m.drilldown == MusicDrilldown::NONE)
  //    2x3 Frosted-Glass Bento Category Grid (480 px Desk Cap, Stacked Icon + font_small — D-060)
  if (m.view == MusicView::LIBRARY && m.drilldown == MusicDrilldown::NONE) {
    render_page_header(it, ctx.cx, "LIBRARY", ctx.font_small, ctx.col_white, true);

    static constexpr const char *kCatLabels[7] = {
        "Favorites", "Playlists", "Artists", "Albums", "Podcasts", "Radio", "Audiobooks"};
    static constexpr const char *kCatIcons[7] = {
        "mdi:heart", "mdi:playlist-music", "mdi:account-music",
        "mdi:album", "mdi:podcast", "mdi:radio-tower", "mdi:book-open-variant"};

    const int cat_pg = std::clamp(m.category_page, 1, 2);
    const int start_i = (cat_pg == 1) ? 0 : 6;
    const int page_cnt = (cat_pg == 1) ? 6 : 1;
    const int content_top = ly.hdr_y + scale_y(24);

    for (int local_i = 0; local_i < page_cnt; ++local_i) {
      const int cat_idx = start_i + local_i;
      const MusicBentoTileGeom tg = compute_music_bento_tile_geom(
          local_i, page_cnt, content_top, mode_bar_bot_limit,
          SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);
      render_card_box(it, tg.tile_x0, tg.tile_y0, tg.tile_w, tg.tile_h,
                      ui::chrome_card_bg_off(), ui::chrome_card_brd_off(),
                      scale_s(15), GlassVariant::Clear, ctx.col_white);

      const int tcx = tg.tile_x0 + tg.tile_w / 2;
      const int ic_sz = std::clamp(scale_s(22), 18, std::max(18, (tg.tile_h * 42) / 100));
      const int ic_cy = tg.tile_y0 + (tg.tile_h * 34) / 100;
      const int txt_cy = tg.tile_y0 + (tg.tile_h * 73) / 100;
      draw_music_mdi(it, kCatIcons[cat_idx], tcx, ic_cy, ic_sz, ctx.theme_primary, true);

      const int txt_max_w = std::max(40, tg.tile_w - scale_x(16));
      const std::string fit_lbl = fit_text_to_width_font(std::string(kCatLabels[cat_idx]), txt_max_w, ctx.font_small);
      draw_shadow_print(it, tcx, txt_cy, ctx.font_small, ctx.col_white, TextAlign::CENTER, fit_lbl.c_str());

      frame_hits().add_rect(tg.tile_x0, tg.tile_y0, tg.tile_w, tg.tile_h,
                            static_cast<uint16_t>(WidgetId::GRID_CARD_BASE + local_i),
                            kHitTap | kHitFocusSlot, static_cast<int16_t>(cat_idx));
    }

    render_music_vertical_pips(it, cat_pg > 1, cat_pg < 2, /*above_mode_bar=*/true, ctx);
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
    return;
  }

  // 4. Library Full-Screen Drilldown (MusicDrilldown::LIB_LIST -> 5 Stack-Uniform Rows + < Back)
  //    OR Album/Episodes Peer Tab (MusicView::ALBUM_TRACKS -> 4 Stack-Uniform Rows + Mode Bar)
  const bool is_peer_album_tab = (m.view == MusicView::ALBUM_TRACKS && m.drilldown == MusicDrilldown::NONE);
  const int vis_rows = is_peer_album_tab ? kMusicPeerListPageSize : kMusicFullListPageSize;
  const int content_bot = is_peer_album_tab ? mode_bar_bot_limit : full_list_bot_limit;

  std::string hdr_title = m.selected_category_title;
  if (m.view == MusicView::ALBUM_TRACKS) {
    hdr_title = trk.is_speech ? "EPISODES" : "ALBUM TRACKS";
  } else if (hdr_title.empty()) {
    hdr_title = "LIBRARY";
  }
  render_page_header(it, ctx.cx, hdr_title, ctx.font_small, ctx.col_white, true);

  const bool has_header_container =
      (m.view == MusicView::ALBUM_TRACKS && (!trk.album.empty() || !m.drilldown_album_id.empty())) ||
      (m.selected_category_mode == "album_tracks" && !m.drilldown_album_id.empty()) ||
      (m.selected_category_mode == "artist_albums" && !m.drilldown_artist_name.empty());

  int content_top = ly.hdr_y + scale_y(24);
  if (has_header_container) {
    const int strip_h = std::clamp(scale_y(32), 28, 44);
    const int sy0 = ly.hdr_y + scale_y(16);
    const int pw = std::clamp(scale_x(156), 124, 220);
    const int sx0 = ctx.cx - pw / 2;
    draw_glass_pill_in_lane(it, sx0, sy0, pw, strip_h, strip_h,
                            ui::chrome_card_bg_off(), ctx.theme_primary, ctx.theme_primary,
                            ctx.font_small, "Enqueue…", "mdi:playlist-plus");
    frame_hits().add_rect(sx0, sy0, pw, strip_h, WidgetId::MUSIC_HEADER_ENQUEUE_BTN, kHitTap | kHitFocusSlot);
    content_top = sy0 + strip_h + scale_y(6);
  }

  const int pg = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
  const int tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC);
  const bool has_pips = (pg > 1) || (pg < tot);
  const BrowseSlotItem *slots = rs.music_slots();
  int drawn = 0;
  for (int r = 0; r < vis_rows; ++r) {
    if (slots[r].name.empty()) continue;
    const bool actionable = !slots[r].id.empty() && slots[r].type != "text";
    const bool has_leading_play = actionable && is_music_playable_container_type(slots[r].type);
    const bool is_drilldown_only_podcast = (slots[r].type == "podcast");
    render_slot_list_row(it, ctx, r, vis_rows, content_top, content_bot,
                         slots[r].name.c_str(), is_drilldown_only_podcast ? ">" : "",
                         slots[r].active || slots[r].is_current,
                         has_leading_play, actionable, has_pips, actionable);
    ++drawn;
  }
  if (drawn == 0) {
    draw_shadow_print(it, ctx.cx, (content_top + content_bot) / 2, ctx.font_small, ctx.col_slate,
                      TextAlign::CENTER, "No items available", true);
  }

  if (is_peer_album_tab) {
    render_music_vertical_pips(it, pg > 1, pg < tot, /*above_mode_bar=*/true, ctx);
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
  } else {
    render_music_vertical_pips(it, pg > 1, pg < tot, /*above_mode_bar=*/false, ctx);
    render_back_button(it, ctx.cx, ctx.font_small, ly.back_y);
  }
}

}  // namespace

void render_music_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  const auto &m = runtime_state().music;

  // Strict ACTION_SHEET modal isolation: NEVER render underlying list buttons!
  if (m.takeover == MusicTakeover::ACTION_SHEET) {
    render_music_art_or_wallpaper_base(it, ctx, /*has_controls_scrim=*/true,
                                       /*has_metadata_scrim=*/true, kArtDimMenu);
    render_action_sheet_takeover(it, ctx);
    return;
  }

  switch (m.view) {
    case MusicView::NOW_PLAYING:
      render_now_playing_view(it, ctx);
      return;
    case MusicView::VOLUME:
      render_volume_peer_tab(it, ctx);
      return;
    case MusicView::LYRICS:
      render_lyrics_view(it, ctx);
      return;
    case MusicView::INFO:
      render_info_view(it, ctx);
      return;
    case MusicView::SETTINGS:
      render_music_settings_view(it, ctx);
      return;
    case MusicView::LIBRARY:
    case MusicView::SPEAKERS:
    case MusicView::QUEUE:
    case MusicView::ALBUM_TRACKS:
    default:
      render_music_browse_views(it, ctx);
      return;
  }
}

}  // namespace latter_ui
