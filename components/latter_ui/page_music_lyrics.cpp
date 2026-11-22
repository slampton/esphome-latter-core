#include "page_music_shared.h"

#include <algorithm>
#include <string>
#include <vector>

namespace latter_ui {

namespace {

void draw_lyrics_alpha_print(Rgb565Surface &it, int x, int y,
                             UiFontView font, UiColor col,
                             UiTextAlign align, const char *txt,
                             int alpha_q8) {
  if (font == nullptr || txt == nullptr || txt[0] == '\0' || alpha_q8 <= 0) return;
  if (alpha_q8 >= 256) {
    ui::shadow_print(it, x, y, font, col, align, txt, true);
  } else {
    const int sh_a = (alpha_q8 * 210) >> 8;
    fast_print_alpha(it, x + 2, y + 2, font, kChromeShadow, align, txt, (sh_a * 170) >> 8);
    fast_print_alpha(it, x + 1, y + 1, font, kChromeShadow, align, txt, sh_a);
    fast_print_alpha(it, x, y, font, col, align, txt, alpha_q8);
  }
}

}  // namespace

void render_music_lyrics_view(Rgb565Surface &it, const UiFrameContext &ctx) {
  auto &rs = runtime_state();
  auto &lyr = rs.music.lyrics;
  const auto &ms = media_state();
  const auto &ac = area_ctx_state();
  const auto &ly = layout();
  const ResolvedTrackStrings trk = resolve_track_strings(ctx);

  if (ctx.music_fullscreen_art_opt == "None") {
    if (is_day_night_sky_wallpaper_url(ac.wallpaper_url)) {
      render_static_day_night_sky_base(
          it, ctx.cx, ctx.cy,
          resolve_day_night_sky_is_night(weather_state().sun_state, ctx.now.is_valid(), ctx.now.hour));
    } else {
      const bool has_wall = (!ac.wallpaper_url.empty() &&
                             ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                             ctx.home_wallpaper_art.valid());
      render_page_wallpaper_base(it, ctx.home_wallpaper_art, has_wall, ctx.cx, ctx.col_bg, kArtDimLyrics);
    }
  } else {
    render_lyrics_art_backdrop(it, select_music_art(ctx), ctx.cx, ctx.theme_primary, ctx.col_bg);
  }

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
    request_music_lyrics_refresh(cur_key, trk.speaker_entity, ctx.now_ms);
  }

  const bool show_hud = rs.music.hud_active;
  const int min_hit = mm_to_px(7.0f);
  int vp_top = LATTER_ROUND_DISPLAY ? scale_y(28) : scale_y(20);
  int vp_bot = SCREEN_H - (LATTER_ROUND_DISPLAY ? scale_y(28) : scale_y(20));

  if (show_hud) {
    std::string hdr_txt = trk.title.empty() ? std::string("Lyrics") : trk.title;
    if (!trk.artist.empty()) hdr_txt += " • " + trk.artist;
    const std::string fit_hdr = fit_text_to_width_font(hdr_txt, safe_span(ly.hdr_y, 24), ctx.font_small);
    ui::shadow_print(it, ctx.cx, ly.hdr_y, ctx.font_small, ctx.col_white,
                     TextAlign::CENTER, fit_hdr.c_str(), true);

    const bool manual_override = lyr.synced && !lyr.lines.empty() && (ctx.now_ms < lyr.manual_override_until_ms);
    int sync_hit_bottom = 0;
    if (manual_override) {
      const int sync_vis_h = std::clamp(scale_y(32), 28, 42);
      const int sync_hit_h = std::max(sync_vis_h, std::min(min_hit, scale_y(54)));
      const int sync_cy = ly.hdr_y + scale_y(30);
      const int span_w = std::min(kMaxContentColW, safe_span(sync_cy, 16));
      const int sync_vis_w = std::clamp(scale_x(86), 72, 124);
      const int right_edge = ctx.cx + span_w / 2;
      const int sync_x0 = right_edge - sync_vis_w;
      ui::action_pill(it, sync_x0, sync_vis_w, sync_cy, sync_vis_h,
                      ctx.theme_primary, ui::chrome_card_bg_on(), "Sync",
                      ctx.theme_primary, ctx.font_small,
                      WidgetId::MUSIC_LYRIC_SYNC_BTN, kHitTap | kHitFocusSlot, 0, sync_hit_h);
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
    render_empty_state(it, ctx.cx, (vp_top + vp_bot) / 2, "Play a track to view lyrics", ctx.font_small, ctx.col_slate);
    finish_lyrics_chrome();
    return;
  }
  if (lyr.loading) {
    render_empty_state(it, ctx.cx, (vp_top + vp_bot) / 2, "Loading lyrics...", ctx.font_small, ctx.col_slate);
    finish_lyrics_chrome();
    return;
  }
  if (lyr.lines.empty()) {
    render_empty_state(it, ctx.cx, (vp_top + vp_bot) / 2, "No lyrics available for this track", ctx.font_small, ctx.col_slate);
    finish_lyrics_chrome();
    return;
  }

  const UiFontView active_font =
      (SCREEN_W >= 640) ? ctx.font_date
                        : ((SCREEN_W >= 440) ? ctx.font_btn_bold : ctx.font_small);
  const UiFontView body_font = ctx.font_small;
  const int active_lh = (SCREEN_W >= 640) ? 36 : ((SCREEN_W >= 440) ? 26 : 22);
  const int body_lh = (SCREEN_W >= 640) ? 28 : 21;
  const int line_gap = scale_y(12);

  const int anchor_screen_y = std::clamp((SCREEN_H * 38) / 100, vp_top + 28, vp_bot - 28);
  const int wrap_w = std::max(120, std::min({kMaxContentColW,
                                             safe_span(anchor_screen_y, 26),
                                             safe_span(vp_top + scale_y(28), 26)}));

  const uint32_t pos_ms = ms.interpolated_position_ms(ctx.now_ms);
  const int live_act_idx = lyr.synced ? lyr.find_active_line(pos_ms) : -1;

  const int n_lines = static_cast<int>(std::min<size_t>(lyr.lines.size(), kMaxMusicLyricLines));
  struct LyricWrapEntry {
    uint8_t body_cnt{1};
    uint8_t act_cnt{1};
    std::string body_sub[4]{};
    std::string act_sub[4]{};
  };
  struct LyricWrapCache {
    std::string track_key{};
    uint32_t rev{0xFFFFFFFFU};
    int line_count{-1};
    int wrap_w{-1};
    const void *active_font{nullptr};
    const void *body_font{nullptr};
    int16_t active_h{0};
    int16_t body_h{0};
    std::vector<LyricWrapEntry> entries{kMaxMusicLyricLines};
  };
  static LyricWrapCache s_wrap_cache{};
  const bool same_fonts = (active_font.handle == body_font.handle && active_font.height == body_font.height);
  if (s_wrap_cache.track_key != cur_key ||
      s_wrap_cache.rev != lyr.rev ||
      s_wrap_cache.line_count != n_lines ||
      s_wrap_cache.wrap_w != wrap_w ||
      s_wrap_cache.active_font != active_font.handle ||
      s_wrap_cache.body_font != body_font.handle ||
      s_wrap_cache.active_h != active_font.height ||
      s_wrap_cache.body_h != body_font.height) {
    s_wrap_cache.track_key = cur_key;
    s_wrap_cache.rev = lyr.rev;
    s_wrap_cache.line_count = n_lines;
    s_wrap_cache.wrap_w = wrap_w;
    s_wrap_cache.active_font = active_font.handle;
    s_wrap_cache.body_font = body_font.handle;
    s_wrap_cache.active_h = active_font.height;
    s_wrap_cache.body_h = body_font.height;
    if (s_wrap_cache.entries.size() < kMaxMusicLyricLines) {
      s_wrap_cache.entries.resize(kMaxMusicLyricLines);
    }
    for (int i = 0; i < n_lines; ++i) {
      auto &ent = s_wrap_cache.entries[static_cast<size_t>(i)];
      const std::string txt = lyr.lines[static_cast<size_t>(i)].text.empty()
                                  ? std::string("♪")
                                  : lyr.lines[static_cast<size_t>(i)].text;
      ent.body_cnt = static_cast<uint8_t>(
          std::max(1, ui::wrap_text_lines(txt, wrap_w, body_font, ent.body_sub, 4)));
      if (!same_fonts) {
        ent.act_cnt = static_cast<uint8_t>(
            std::max(1, ui::wrap_text_lines(txt, wrap_w, active_font, ent.act_sub, 4)));
      } else {
        ent.act_cnt = ent.body_cnt;
        for (int s = 0; s < static_cast<int>(ent.body_cnt); ++s) {
          ent.act_sub[s] = ent.body_sub[s];
        }
      }
    }
  }

  static std::vector<int> s_doc_buf(kMaxMusicLyricLines * 3U);
  int *const doc_y0 = s_doc_buf.data();
  int *const doc_y1 = doc_y0 + kMaxMusicLyricLines;
  int *const doc_cy = doc_y0 + (kMaxMusicLyricLines * 2U);
  int cur_doc_y = scale_y(16);

  for (int i = 0; i < n_lines; ++i) {
    const bool is_act = lyr.synced && (i == live_act_idx);
    const int lh = is_act ? active_lh : body_lh;
    const auto &ent = s_wrap_cache.entries[static_cast<size_t>(i)];
    const int sc = static_cast<int>(is_act ? ent.act_cnt : ent.body_cnt);
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
    const UiFontView f = is_act ? active_font : body_font;
    const int lh = is_act ? active_lh : body_lh;
    UiColor fg_c = ctx.col_white;
    if (lyr.synced && !is_act) {
      fg_c = (i < lyr.active_line_idx) ? mix_color_q8(ctx.col_white, ctx.col_slate, 88)
                                       : mix_color_q8(ctx.col_white, ctx.col_slate, 124);
    }

    const auto &ent = s_wrap_cache.entries[static_cast<size_t>(i)];
    const int sc = static_cast<int>(is_act ? ent.act_cnt : ent.body_cnt);
    const std::string *sub = is_act ? ent.act_sub : ent.body_sub;

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
      const int row_span = std::min(kMaxContentColW, safe_span(draw_cy, 20));
      const std::string clamped_line = fit_text_to_width_font(sub[s], row_span, f);
      draw_lyrics_alpha_print(it, ctx.cx, draw_cy, f, fg_c, TextAlign::CENTER,
                              clamped_line.c_str(), alpha_q8);
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

void render_music_info_view(Rgb565Surface &it, const UiFrameContext &ctx) {
  auto &rs = runtime_state();
  const auto &ms = media_state();
  const auto &ac = area_ctx_state();
  const ResolvedTrackStrings trk = resolve_track_strings(ctx);

  if (ctx.music_fullscreen_art_opt == "None") {
    if (is_day_night_sky_wallpaper_url(ac.wallpaper_url)) {
      render_static_day_night_sky_base(
          it, ctx.cx, ctx.cy,
          resolve_day_night_sky_is_night(weather_state().sun_state, ctx.now.is_valid(), ctx.now.hour));
    } else {
      const bool has_wall = (!ac.wallpaper_url.empty() &&
                             ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                             ctx.home_wallpaper_art.valid());
      render_page_wallpaper_base(it, ctx.home_wallpaper_art, has_wall, ctx.cx, ctx.col_bg, kArtDimLyrics);
    }
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

  const int wrap_w = std::max(120, std::min({kMaxContentColW, safe_span(ctx.cy, 24), safe_span(vp_top + 24, 24)}));

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
  const int lc = ui::wrap_text_lines(body, wrap_w, ctx.font_small, lines, 24);
  const int lh = (SCREEN_W >= 640) ? 28 : 22;
  const int max_scroll = std::max(0, lc * lh - std::max(40, vp_bot - vp_top));
  rs.music.info_scroll_px = std::clamp(rs.music.info_scroll_px, 0, max_scroll);

  for (int i = 0; i < lc; ++i) {
    const int dy = vp_top + 14 + i * lh - rs.music.info_scroll_px;
    if (dy < vp_top + 6 || dy > vp_bot - 6) continue;
    const std::string fit_l = fit_text_to_width_font(lines[i], std::min(kMaxContentColW, safe_span(dy, 20)), ctx.font_small);
    const UiColor c = (i == 0) ? ctx.col_white
                               : ((i <= 2) ? ctx.theme_secondary
                                           : mix_color_q8(ctx.col_white, ctx.col_slate, 72));
    ui::shadow_print(it, ctx.cx, dy, ctx.font_small, c, TextAlign::CENTER, fit_l.c_str(), true);
  }

  if (show_hud) {
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
  } else {
    frame_hits().add_rect(0, 0, SCREEN_W, SCREEN_H, WidgetId::MUSIC_ART_TAP, kHitTap);
  }
}

}  // namespace latter_ui
