#include "ui_pages.h"
#include "ui_touch_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace latter_ui {
using namespace latter_core;

namespace {

inline constexpr bool is_speech_media_type(std::string_view mt) noexcept {
  return mt == "podcast" || mt == "podcast_episode" || mt == "audiobook";
}

inline void format_mmss(int total_sec, char *out, size_t cap) noexcept {
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

struct ResolvedTrackStrings {
  std::string title{};
  std::string artist{};
  std::string album{};
  std::string speaker_name{};
  std::string speaker_entity{};
  bool is_playing{false};
  bool is_speech{false};
};

inline ResolvedTrackStrings resolve_track_strings(const UiFrameContext &ctx) {
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

inline ArtImageView select_music_art(const UiFrameContext &ctx) {
  const auto &ms = media_state();
  if (!ms.is_local && ctx.remote_album_art.valid()) return ctx.remote_album_art;
  if (ms.is_local && ctx.album_art.valid()) return ctx.album_art;
  return ctx.remote_album_art.valid() ? ctx.remote_album_art : ctx.album_art;
}

inline void render_music_art_or_wallpaper_base(Rgb565Surface &it, const UiFrameContext &ctx,
                                               bool has_controls_scrim, bool has_metadata_scrim,
                                               int dim_level) {
  const auto &rs = runtime_state();
  const auto &ac = area_ctx_state();
  const UiStrView art_mode = ctx.music_fullscreen_art_opt;
  if (art_mode == "None") {
    if (is_day_night_sky_wallpaper_url(ac.wallpaper_url)) {
      const auto &ws_m = weather_state();
      if (has_known_day_night_phase(ws_m.sun_state, ctx.now.is_valid(), ctx.now.hour)) {
        render_static_day_night_sky_base(
            it, ctx.cx, ctx.cy,
            resolve_day_night_sky_is_night(ws_m.sun_state, ctx.now.is_valid(), ctx.now.hour));
      } else {
        frame_base(it, ctx.col_bg);
      }
    } else {
      const bool has_wall = (!ac.wallpaper_url.empty() &&
                             ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                             ctx.home_wallpaper_art.valid());
      render_page_wallpaper_base(it, ctx.home_wallpaper_art, has_wall, ctx.cx, ctx.col_bg, dim_level);
    }
  } else {
    const bool is_standard = (art_mode == "Standard") && (dim_level == 0);
    const bool is_letterbox = (art_mode == "Letterbox") && (dim_level == 0);
    render_album_art_canvas(it, select_music_art(ctx), ctx.cx, is_standard, is_letterbox,
                            has_controls_scrim, has_metadata_scrim, dim_level);
    frame_base(it, ctx.col_bg);
  }
}

struct MusicModeBarIconCtx {
  bool is_speech{false};
  bool muted{false};
};

inline void draw_music_mode_icon(Rgb565Surface &it, int g_idx, int bx, int by, int ir,
                                 UiColor fg_c, bool sh, void *user_ctx) {
  const auto *mc = static_cast<const MusicModeBarIconCtx *>(user_ctx);
  const bool is_speech = (mc != nullptr) ? mc->is_speech : false;
  const bool muted = (mc != nullptr) ? mc->muted : false;
  const int mdi_sz = std::max(16, 2 * ir);
  switch (g_idx) {
    case 0: ui::draw_icon_play_pause(it, bx, by, ir, fg_c, sh); break;
    case 1: ui::draw_icon_hamburger(it, bx, by, ir, fg_c, sh); break;
    case 2: render_mdi_icon(it, "mdi:speaker-multiple", bx, by, mdi_sz, fg_c, sh); break;
    case 3: ui::draw_icon_speaker(it, bx, by, ir, fg_c, !muted, sh, muted); break;
    case 4: render_mdi_icon(it, "mdi:playlist-play", bx, by, mdi_sz, fg_c, sh); break;
    case 5: render_mdi_icon(it, "mdi:script-text", bx, by, mdi_sz, fg_c, sh); break;
    case 6: render_mdi_icon(it, is_speech ? "mdi:podcast" : "mdi:album", bx, by, mdi_sz, fg_c, sh); break;
    case 7: render_mdi_icon(it, "mdi:information", bx, by, mdi_sz, fg_c, sh); break;
    case 8:
    default: ui::draw_icon_gear(it, bx, by, ir, fg_c, sh); break;
  }
}

inline void render_music_stadium_mode_bar(Rgb565Surface &it, const UiFrameContext &ctx,
                                          bool is_speech_media) {
  auto &m = runtime_state().music;
  const auto &ms = media_state();
  m.sync_carousel_for_view(layout().dock_slot_count);
  const int active_global = std::clamp(static_cast<int>(m.view), 0, kMusicPeerTabCount - 1);
  MusicModeBarIconCtx ic{is_speech_media, ms.muted};
  ui::mode_bar(it, ctx.cx, kMusicPeerTabCount, active_global, m.mode_bar_win_start,
               ctx.theme_primary, ctx.col_white, draw_music_mode_icon, &ic);
}

inline void render_music_transport_row(Rgb565Surface &it, const UiFrameContext &ctx,
                                       const ResolvedTrackStrings &trk,
                                       int prev_cx, int play_cx, int next_cx,
                                       int transport_cy, int side_r, int play_r) {
  render_glass_circle(it, prev_cx, transport_cy, side_r,
                      ui::chrome_card_bg_off(), ui::chrome_card_brd_off(),
                      GlassVariant::Clear, ctx.col_white);
  render_glass_circle(it, play_cx, transport_cy, play_r,
                      trk.is_playing ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                      trk.is_playing ? ctx.theme_primary : ctx.col_white,
                      GlassVariant::Clear, ctx.col_white);
  render_glass_circle(it, next_cx, transport_cy, side_r,
                      ui::chrome_card_bg_off(), ui::chrome_card_brd_off(),
                      GlassVariant::Clear, ctx.col_white);

  const int side_ic_sz = std::max(18, (side_r * 11) / 10);
  const int hero_ic_sz = std::max(22, (play_r * 12) / 10);
  render_mdi_icon(it, trk.is_speech ? "mdi:rewind-10" : "mdi:skip-previous",
                  prev_cx, transport_cy, side_ic_sz, ctx.col_white, true);
  render_mdi_icon(it, trk.is_playing ? "mdi:pause" : "mdi:play",
                  play_cx, transport_cy, hero_ic_sz, ctx.col_white, true);
  render_mdi_icon(it, trk.is_speech ? "mdi:fast-forward-30" : "mdi:skip-next",
                  next_cx, transport_cy, side_ic_sz, ctx.col_white, true);
}

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


void render_music_lyrics_view(Rgb565Surface &it, const UiFrameContext &ctx) {
  auto &rs = runtime_state();
  auto &lyr = rs.music.lyrics;
  const auto &ms = media_state();
  const auto &ac = area_ctx_state();
  const auto &ly = layout();
  const ResolvedTrackStrings trk = resolve_track_strings(ctx);

  if (ctx.music_fullscreen_art_opt == "None") {
    if (is_day_night_sky_wallpaper_url(ac.wallpaper_url)) {
      const auto &ws_m = weather_state();
      if (has_known_day_night_phase(ws_m.sun_state, ctx.now.is_valid(), ctx.now.hour)) {
        render_static_day_night_sky_base(
            it, ctx.cx, ctx.cy,
            resolve_day_night_sky_is_night(ws_m.sun_state, ctx.now.is_valid(), ctx.now.hour));
      } else {
        frame_base(it, ctx.col_bg);
      }
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
  const MusicLyricsTransportGeom tg = compute_music_lyrics_transport_geom(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);
  const int min_hit = mm_to_px(7.0f);
  int vp_top = LATTER_ROUND_DISPLAY ? dp_px(28) : dp_px(20);
  int vp_bot = SCREEN_H - (LATTER_ROUND_DISPLAY ? dp_px(28) : dp_px(20));

  if (show_hud) {
    std::string hdr_txt = trk.title.empty() ? std::string("Lyrics") : trk.title;
    if (!trk.artist.empty()) hdr_txt += " • " + trk.artist;
    const std::string fit_hdr = fit_text_to_width_font(hdr_txt, safe_span(ly.hdr_y, 24), ctx.font_small);
    ui::shadow_print(it, ctx.cx, ly.hdr_y, ctx.font_small, ctx.col_white,
                     TextAlign::CENTER, fit_hdr.c_str(), true);

    const bool manual_override = lyr.synced && !lyr.lines.empty() && (ctx.now_ms < lyr.manual_override_until_ms);
    int sync_hit_bottom = 0;
    if (manual_override) {
      const int sync_vis_h = std::clamp(dp_px(32), 28, 46);
      const int sync_hit_h = std::max(sync_vis_h, std::min(min_hit, dp_px(54)));
      const int sync_cy = ly.hdr_y + dp_px(30);
      const int span_w = std::min(ly.max_content_w(), safe_span(sync_cy, 16));
      const int sync_vis_w = std::clamp(dp_px(86), 72, 132);
      const int right_edge = ctx.cx + span_w / 2;
      const int sync_x0 = right_edge - sync_vis_w;
      ui::action_pill(it, sync_x0, sync_vis_w, sync_cy, sync_vis_h,
                      ctx.theme_primary, ui::chrome_card_bg_on(), "Sync",
                      ctx.theme_primary, ctx.font_small,
                      WidgetId::MUSIC_LYRIC_SYNC_BTN, kHitTap | kHitFocusSlot, 0, sync_hit_h);
      sync_hit_bottom = sync_cy + sync_hit_h / 2 + 2;
    }
    vp_top = std::max(ly.hdr_y + dp_px(24), sync_hit_bottom);
    vp_bot = std::max(vp_top + 60, tg.vp_bot);
  }

  const int vp_h = std::max(60, vp_bot - vp_top);

  auto finish_lyrics_chrome = [&]() {
    if (show_hud) {
      render_music_transport_row(it, ctx, trk, tg.prev_cx, tg.play_cx, tg.next_cx,
                                 tg.transport_cy, tg.side_r, tg.play_r);
      register_music_lyrics_transport_hits(frame_hits(), tg);
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
      (ly.size_class == SizeClass::Expanded || kAutoFonts.date >= 36)
          ? ctx.font_date
          : ((ly.frame.w_dp >= 400) ? ctx.font_btn_bold : ctx.font_small);
  const UiFontView body_font = ctx.font_small;
  auto measure_font_lh = [](UiFontView f, int fallback_px) -> int {
    int mw = 0, mxo = 0, mbl = 0, mh = 0;
    if (f != nullptr) f->measure("Ag", &mw, &mxo, &mbl, &mh);
    return (mh > 0) ? mh : fallback_px;
  };
  const int active_lh = measure_font_lh(active_font, kAutoFonts.btn_bold) + dp_px(6);
  const int body_lh = measure_font_lh(body_font, kAutoFonts.small) + dp_px(5);
  const int line_gap = dp_px(12);

  const int anchor_screen_y = std::clamp((SCREEN_H * 38) / 100, vp_top + 28, vp_bot - 28);
  const int wrap_w = std::max(120, std::min({ly.max_content_w(),
                                             safe_span(anchor_screen_y, 26),
                                             safe_span(vp_top + dp_px(28), 26)}));

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
  int cur_doc_y = dp_px(16);

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

  const int total_doc_h = cur_doc_y + dp_px(24);
  const int anchor_in_vp = anchor_screen_y - vp_top;
  const int max_scroll = std::max(0, total_doc_h - vp_h / 2);
  step_music_lyrics_auto_scroll(lyr, pos_ms, ctx.now_ms, anchor_in_vp, doc_cy, n_lines, max_scroll);

  const int fade_band = std::max(16, dp_px(28));
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
      const int row_span = std::min(ly.max_content_w(), safe_span(draw_cy, 20));
      const std::string clamped_line = fit_text_to_width_font(sub[s], row_span, f);
      draw_lyrics_alpha_print(it, ctx.cx, draw_cy, f, fg_c, TextAlign::CENTER,
                              clamped_line.c_str(), alpha_q8);
    }

    if (show_hud && lyr.synced && lyr.lines[static_cast<size_t>(i)].t_ms >= 0 && hit_slot < 8) {
      const int hy0 = std::max(scr_y0 - 2, last_hit_y1 + 1);
      const int hy1 = std::min(scr_y1 + 2, vp_bot - 2);
      if (hy1 - hy0 >= 16) {
        const int hw = std::max(80, ly.band_safe_span((hy0 + hy1) / 2, hy1 - hy0, 20));
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
  const auto &ly = layout();
  const ResolvedTrackStrings trk = resolve_track_strings(ctx);

  if (ctx.music_fullscreen_art_opt == "None") {
    if (is_day_night_sky_wallpaper_url(ac.wallpaper_url)) {
      const auto &ws_m = weather_state();
      if (has_known_day_night_phase(ws_m.sun_state, ctx.now.is_valid(), ctx.now.hour)) {
        render_static_day_night_sky_base(
            it, ctx.cx, ctx.cy,
            resolve_day_night_sky_is_night(ws_m.sun_state, ctx.now.is_valid(), ctx.now.hour));
      } else {
        frame_base(it, ctx.col_bg);
      }
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

  int vp_top = LATTER_ROUND_DISPLAY ? dp_px(30) : dp_px(22);
  int vp_bot = g.vp_bot_ambient;

  if (show_hud) {
    render_page_header(it, ctx.cx, "TRACK INFO", ctx.font_small);
    vp_top = g.vp_top;
    vp_bot = g.vp_bot_hud;
  }

  const int wrap_w = std::max(120, std::min({ly.max_content_w(), safe_span(ctx.cy, 24), safe_span(vp_top + 24, 24)}));

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
  int imw = 0, imxo = 0, imbl = 0, imh = 0;
  if (ctx.font_small != nullptr) ctx.font_small->measure("Ag", &imw, &imxo, &imbl, &imh);
  const int lh = ((imh > 0) ? imh : kAutoFonts.small) + dp_px(6);
  const int max_scroll = std::max(0, lc * lh - std::max(40, vp_bot - vp_top));
  rs.music.info_scroll_px = std::clamp(rs.music.info_scroll_px, 0, max_scroll);

  for (int i = 0; i < lc; ++i) {
    const int dy = vp_top + 14 + i * lh - rs.music.info_scroll_px;
    if (dy < vp_top + 6 || dy > vp_bot - 6) continue;
    const std::string fit_l = fit_text_to_width_font(lines[i], std::min(ly.max_content_w(), safe_span(dy, 20)), ctx.font_small);
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



int compute_two_line_half_gap(UiFontView font, int card_h) {
  int fw = 0, fxo = 0, fb = 0, fh = 0;
  if (font != nullptr) font->measure("Ag", &fw, &fxo, &fb, &fh);
  const int line_h = (fh > 0) ? fh : 18;
  const int desired = std::max(dp_px(11), (line_h / 2) + 2);
  const int max_safe = std::max(desired, (card_h - line_h) / 2 - 2);
  return std::min(desired, std::max(line_h / 2 + 1, max_safe));
}

void render_slot_list_row(Rgb565Surface &it, const UiFrameContext &ctx,
                          int row_idx, int visible_rows, int content_top, int content_bot,
                          const char *primary_txt, const char *badge_txt,
                          bool is_highlighted, bool has_leading_play,
                          bool enable_long_press, bool has_pips, bool register_hits) {
  MusicListRowGeom g = compute_music_list_row_geom(
      row_idx, visible_rows, content_top, content_bot, has_leading_play, has_pips,
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);
  const int bounce_y = edge_bounce().offset_px(PageId::MUSIC, GestureAxisLock::Vertical,
                                               ctx.now_ms, LATTER_PANEL_PPI);
  g.card_y0 += bounce_y;
  g.cy_row += bounce_y;

  const std::string clean_primary = sanitize_utf8_for_font(
      std::string(primary_txt != nullptr ? primary_txt : ""), ctx.font_small);

  if (!register_hits) {
    const std::string fit_p = fit_text_to_width_font(clean_primary, std::max(40, g.card_w - dp_px(24)), ctx.font_small);
    ui::shadow_print(it, g.card_x0 + g.card_w / 2, g.cy_row, ctx.font_small, ctx.col_slate,
                     TextAlign::CENTER, fit_p.c_str());
    return;
  }

  const UiColor bg = is_highlighted ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off();
  const UiColor brd = is_highlighted ? ctx.theme_primary : ui::chrome_card_brd_off();
  const UiColor fg = is_highlighted ? ctx.theme_primary : ctx.col_white;
  render_card_box(it, g.card_x0, g.card_y0, g.card_w, g.card_h,
                  bg, brd, kRadiusCardPx(), GlassVariant::Clear, fg);

  int text_x0 = g.card_x0 + dp_px(14);
  if (g.play_vis_w > 0) {
    const int play_cx = g.card_x0 + g.play_vis_w / 2;
    render_mdi_icon(it, "mdi:play", play_cx, g.cy_row,
                    std::clamp(dp_px(18), 15, g.card_h - 14), ctx.theme_primary, true);
    fast_vline(it, g.card_x0 + g.play_vis_w, g.card_y0 + 8, std::max(8, g.card_h - 16), kChromeCardBorder);
    text_x0 = g.card_x0 + g.play_vis_w + dp_px(12);
  }

  int badge_reserve_w = 0;
  const bool has_badge = (badge_txt != nullptr && badge_txt[0] != '\0');
  if (has_badge) {
    int bw = 0, xo = 0, b = 0, bh = 0;
    if (ctx.font_small != nullptr) ctx.font_small->measure(badge_txt, &bw, &xo, &b, &bh);
    badge_reserve_w = std::max(bw, dp_px(28)) + dp_px(16);
    const int bx = g.card_x0 + g.card_w - dp_px(12);
    ui::shadow_print(it, bx, g.cy_row, ctx.font_small,
                     is_highlighted ? ctx.theme_primary : ctx.col_slate,
                     TextAlign::CENTER_RIGHT, badge_txt);
  }

  const int text_right = g.card_x0 + g.card_w - dp_px(12) - badge_reserve_w;
  const int text_max_w = std::max(40, text_right - text_x0);

  int sw = 0, xo = 0, b = 0, sh = 0;
  if (ctx.font_small != nullptr && !clean_primary.empty()) {
    ctx.font_small->measure(clean_primary.c_str(), &sw, &xo, &b, &sh);
  }

  if (sw <= text_max_w || g.card_h < dp_px(56)) {
    const std::string fit_p = fit_text_to_width_font(clean_primary, text_max_w, ctx.font_small);
    ui::shadow_print(it, text_x0, g.cy_row, ctx.font_small, fg,
                     TextAlign::CENTER_LEFT, fit_p.c_str());
  } else {
    std::string two_lines[2];
    const int lc = ui::wrap_text_lines(clean_primary, text_max_w, ctx.font_small, two_lines, 2);
    if (lc <= 1) {
      ui::shadow_print(it, text_x0, g.cy_row, ctx.font_small, fg,
                       TextAlign::CENTER_LEFT, two_lines[0].c_str());
    } else {
      const int off_y = compute_two_line_half_gap(ctx.font_small, g.card_h);
      ui::shadow_print(it, text_x0, g.cy_row - off_y, ctx.font_small, fg,
                       TextAlign::CENTER_LEFT, two_lines[0].c_str());
      ui::shadow_print(it, text_x0, g.cy_row + off_y, ctx.font_small,
                       mix_color_q8(fg, ctx.col_slate, 56),
                       TextAlign::CENTER_LEFT, two_lines[1].c_str());
    }
  }

  register_music_list_row_hits(frame_hits(), g, row_idx, enable_long_press);
}


void render_music_action_sheet_takeover(Rgb565Surface &it, const UiFrameContext &ctx) {
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
      "mdi:play", "mdi:skip-next-circle", "mdi:arrow-up", "mdi:arrow-down", "mdi:playlist-remove"};

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
  ui::shadow_print(it, g.cx, g.hdr_title_y, ctx.font_small,
                   ctx.col_white, TextAlign::CENTER, fit_hdr.c_str(), true);

  for (int i = 0; i < g.item_count; ++i) {
    const bool is_danger = (wids[i] == WidgetId::MUSIC_ACTION_REMOVE_QUEUE);
    const bool is_primary = (i == 0);
    const UiColor bg = is_danger ? ui::chrome_danger_bg()
                                 : (is_primary ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off());
    const UiColor brd = is_danger ? ctx.col_red
                                  : (is_primary ? ctx.theme_primary : ui::chrome_card_brd_off());
    const int eh = std::clamp(g.item_vis_h, 28, g.item_h[i]);
    const int row_cy = g.item_y0[i] + g.item_h[i] / 2;
    const int row_cx = g.item_x0[i] + g.item_w[i] / 2;
    render_card_box(it, g.item_x0[i], row_cy - eh / 2, g.item_w[i], eh,
                    bg, brd, kRadiusCardPx(), GlassVariant::Clear, ctx.col_white);
    const int icon_px = std::clamp(dp_px(17), 14, eh - 10);
    const int max_txt_w = std::max(24, g.item_w[i] - icon_px - 20);
    const std::string fit_l = fit_text_to_width_font(std::string(labels[i]), max_txt_w, ctx.font_small);
    int tw = 0, xo = 0, b = 0, th = 0;
    if (ctx.font_small != nullptr) ctx.font_small->measure(fit_l.c_str(), &tw, &xo, &b, &th);
    const int tot_w = icon_px + 6 + tw;
    const int start_x = row_cx - tot_w / 2;
    render_mdi_icon(it, icons[i], start_x + icon_px / 2, row_cy, icon_px, ctx.col_white, true);
    ui::shadow_print(it, start_x + icon_px + 6, row_cy, ctx.font_small, ctx.col_white,
                     TextAlign::CENTER_LEFT, fit_l.c_str());
  }

  render_back_button(it, ctx.cx, ctx.font_small, ly.back_y, "< Cancel");
}

void render_music_settings_view(Rgb565Surface &it, const UiFrameContext &ctx) {
  auto &rs = runtime_state();
  const auto &ly = layout();
  const ResolvedTrackStrings trk = resolve_track_strings(ctx);

  render_music_art_or_wallpaper_base(it, ctx, /*has_controls_scrim=*/true,
                                     /*has_metadata_scrim=*/true, kArtDimMenu);
  render_page_header(it, ctx.cx, "MUSIC SETTINGS", ctx.font_small);

  struct SettingItem {
    const char *prefix;
    std::string_view value;
  };
  const SettingItem all_items[6] = {
      {"Art", ctx.music_fullscreen_art_opt.sv()},
      {"HUD", ctx.music_overlay_style_opt.sv()},
      {"Auto-Open", ctx.music_auto_open_opt.sv()},
      {"Albums", ctx.music_album_sort_opt.sv()},
      {"Artist", ctx.music_artist_album_sort_opt.sv()},
      {"Podcasts", ctx.music_podcast_sort_opt.sv()},
  };
  const int per_pg = std::clamp(ly.peer_list_rows, 4, kMaxListSlots);
  const int total_pages = std::max(1, (6 + per_pg - 1) / per_pg);
  const int pg = std::clamp(rs.music.settings_page, 1, total_pages);
  rs.music.settings_page = pg;
  const int start_i = (pg - 1) * per_pg;
  const int count = std::min(per_pg, 6 - start_i);

  const int bounce_y = edge_bounce().offset_px(PageId::MUSIC, GestureAxisLock::Vertical,
                                               ctx.now_ms, LATTER_PANEL_PPI);
  for (int i = 0; i < count; ++i) {
    const int g_idx = start_i + i;
    char buf[72];
    std::snprintf(buf, sizeof(buf), "%s: %.*s", all_items[g_idx].prefix,
                  static_cast<int>(all_items[g_idx].value.size()), all_items[g_idx].value.data());
    ui::setting_cycle_row(it, ctx.cx, ly.peer_list_row_y(i) + bounce_y, ly.list_card_w, ly.list_h,
                          buf, true, ctx.font_small, ctx.col_white, ctx.col_slate,
                          ctx.theme_secondary,
                          static_cast<uint16_t>(WidgetId::LIST_SLOT_BASE + i),
                          kHitTap | kHitFocusSlot, static_cast<int16_t>(g_idx));
  }

  if (total_pages > 1) {
    render_vertical_page_pips(it, pg > 1, pg < total_pages, ctx.col_bg, ctx.theme_primary,
                              ly.mode_bar_zone_top());
  }
  render_music_stadium_mode_bar(it, ctx, trk.is_speech);
}

void render_music_browse_views(Rgb565Surface &it, const UiFrameContext &ctx) {
  auto &rs = runtime_state();
  auto &m = rs.music;
  const auto &ms = media_state();
  const auto &ly = layout();
  const ResolvedTrackStrings trk = resolve_track_strings(ctx);
  const int min_hit = mm_to_px(7.0f);
  const int mode_bar_bot_limit = ly.mode_bar_zone_top() - dp_px(8);
  const int full_list_bot_limit = ly.back_y - dp_px(24);

  render_music_art_or_wallpaper_base(it, ctx, /*has_controls_scrim=*/true,
                                     /*has_metadata_scrim=*/true, kArtDimMenu);

  if (m.view == MusicView::SPEAKERS) {
    if (m.drilldown == MusicDrilldown::SPEAKER_ACTIONS && !m.target_speaker_id.empty()) {
      render_page_header(it, ctx.cx, m.target_speaker_name.empty() ? "SPEAKER" : m.target_speaker_name,
                         ctx.font_small);
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
      for (int i = 0; i < 3; ++i) {
        const int ym = ly.slot5_y(i);
        ui::list_row(it, ctx.cx, ym, ly.list_card_w, ly.slot5_h, labels[i], nullptr,
                     (i == 0) ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                     (i == 0) ? ctx.theme_primary : ui::chrome_card_brd_off(),
                     ctx.col_white, ctx.col_slate, ctx.font_small,
                     wids[i], kHitTap | kHitFocusSlot, 0, icons[i]);
      }
      render_back_button(it, ctx.cx, ctx.font_small, ly.back_y);
      return;
    }

    render_page_header(it, ctx.cx, "SPEAKERS", ctx.font_small);
    const int content_top = ly.hdr_y + dp_px(24);
    const int vis_rows = std::clamp(ly.peer_list_rows, 4, kMaxListSlots);
    const int pg = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
    const int tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC);
    const bool has_pips = (pg > 1) || (pg < tot);
    const BrowseSlotItem *slots = rs.music_slots();
    int drawn = 0;
    for (int r = 0; r < vis_rows; ++r) {
      if (slots[r].name.empty()) continue;
      char lvl_buf[16] = "";
      if (slots[r].level >= 0) std::snprintf(lvl_buf, sizeof(lvl_buf), "%d%%", slots[r].level);
      const bool is_cur = slots[r].active || (!slots[r].id.empty() && slots[r].id == trk.speaker_entity);
      render_slot_list_row(it, ctx, r, vis_rows, content_top, mode_bar_bot_limit,
                           slots[r].name.c_str(), lvl_buf, is_cur,
                           /*has_leading_play=*/false, /*enable_long_press=*/false,
                           has_pips, /*register_hits=*/true);
      ++drawn;
    }
    if (drawn == 0) {
      render_empty_state(it, ctx.cx, (content_top + mode_bar_bot_limit) / 2, "No speakers found",
                         ctx.font_small, ctx.col_slate);
    }
    render_vertical_page_pips(it, pg > 1, pg < tot, ctx.col_bg, ctx.theme_primary,
                              ly.mode_bar_zone_top());
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
    return;
  }

  if (m.view == MusicView::QUEUE) {
    render_page_header(it, ctx.cx, "PLAYBACK QUEUE", ctx.font_small);

    const int strip_h = std::clamp(dp_px(32), 28, 46);
    const int sy0 = ly.hdr_y + dp_px(22);
    const int strip_cy = sy0 + strip_h / 2;
    const int strip_span = ly.band_safe_span(strip_cy, strip_h, dp_px(12));
    const int strip_w = std::clamp(std::min(ly.max_content_w(380), strip_span), 180, SCREEN_W - 24);
    const int gap = std::max(4, dp_px(5));
    const int pw = (strip_w - 2 * gap) / 3;
    const int sx0 = ctx.cx - strip_w / 2;

    const bool can_shuf = (ms.media_caps & kMediaCapShuffleSet) != 0U;
    const bool can_rep = (ms.media_caps & kMediaCapRepeatSet) != 0U;
    const bool shuf_on = ms.shuffle;
    const bool rep_on = (ms.repeat_mode != static_cast<uint8_t>(MusicRepeatMode::OFF));
    const bool compact_q = (pw < 118);
    char rep_txt[20];
    if (compact_q) {
      std::snprintf(rep_txt, sizeof(rep_txt), "%s", music_repeat_mode_label(ms.repeat_mode));
    } else {
      std::snprintf(rep_txt, sizeof(rep_txt), "Rep: %s", music_repeat_mode_label(ms.repeat_mode));
    }

    const int q_hit_h = std::max(strip_h, std::min(min_hit, ly.slot5_h));
    ui::action_pill(it, sx0, pw, strip_cy, strip_h,
                    (can_shuf && shuf_on) ? ctx.theme_primary : ui::chrome_card_brd_off(),
                    (can_shuf && shuf_on) ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                    compact_q ? (shuf_on ? "On" : "Shuf") : (shuf_on ? "Shuf On" : "Shuffle"),
                    !can_shuf ? ctx.col_gray : (shuf_on ? ctx.theme_primary : ctx.col_white),
                    ctx.font_small,
                    can_shuf ? WidgetId::MUSIC_SHUFFLE_BTN : WidgetId::NONE,
                    can_shuf ? (kHitTap | kHitFocusSlot) : 0, 0, q_hit_h);
    ui::action_pill(it, sx0 + pw + gap, pw, strip_cy, strip_h,
                    (can_rep && rep_on) ? ctx.theme_primary : ui::chrome_card_brd_off(),
                    (can_rep && rep_on) ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                    rep_txt,
                    !can_rep ? ctx.col_gray : (rep_on ? ctx.theme_primary : ctx.col_white),
                    ctx.font_small,
                    can_rep ? WidgetId::MUSIC_REPEAT_BTN : WidgetId::NONE,
                    can_rep ? (kHitTap | kHitFocusSlot) : 0, 0, q_hit_h);
    ui::action_pill(it, sx0 + 2 * (pw + gap), pw, strip_cy, strip_h,
                    ui::chrome_card_brd_off(), ui::chrome_card_bg_off(),
                    "Clear", ctx.col_slate, ctx.font_small,
                    WidgetId::MUSIC_QUEUE_CLEAR, kHitTap | kHitFocusSlot, 0, q_hit_h);

    const int content_top = sy0 + strip_h + dp_px(6);
    const int vis_rows = std::clamp(ly.queue_list_rows, 3, kMaxListSlots);
    const int pg = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
    const int tot = rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC);
    const bool has_pips = (pg > 1) || (pg < tot);
    const BrowseSlotItem *slots = rs.music_slots();
    int drawn = 0;
    for (int r = 0; r < vis_rows; ++r) {
      if (slots[r].name.empty()) continue;
      const bool is_now = slots[r].is_current || slots[r].active;
      const bool actionable = !slots[r].id.empty() || !slots[r].queue_item_id.empty();
      render_slot_list_row(it, ctx, r, vis_rows, content_top, mode_bar_bot_limit,
                           slots[r].name.c_str(), is_now ? "NOW" : "",
                           is_now, /*has_leading_play=*/false, actionable, has_pips, actionable);
      ++drawn;
    }
    if (drawn == 0) {
      render_empty_state(it, ctx.cx, (content_top + mode_bar_bot_limit) / 2, "Queue is empty",
                         ctx.font_small, ctx.col_slate);
    }
    render_vertical_page_pips(it, pg > 1, pg < tot, ctx.col_bg, ctx.theme_primary,
                              ly.mode_bar_zone_top());
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
    return;
  }

  if (m.view == MusicView::LIBRARY && m.drilldown == MusicDrilldown::NONE) {
    render_page_header(it, ctx.cx, "LIBRARY", ctx.font_small);

    static constexpr const char *kCatLabels[7] = {
        "Favorites", "Playlists", "Artists", "Albums", "Podcasts", "Radio", "Audiobooks"};
    static constexpr const char *kCatIcons[7] = {
        "mdi:heart", "mdi:playlist-music", "mdi:account-music",
        "mdi:album", "mdi:podcast", "mdi:radio-tower", "mdi:book-open-variant"};

    const int per_pg = std::clamp(ly.peer_list_rows, 4, kMaxListSlots);
    const int total_pages = std::max(1, (7 + per_pg - 1) / per_pg);
    const int cat_pg = std::clamp(m.category_page, 1, total_pages);
    m.category_page = cat_pg;
    const int start_i = (cat_pg - 1) * per_pg;
    const int page_cnt = std::min(per_pg, 7 - start_i);

    for (int local_i = 0; local_i < page_cnt; ++local_i) {
      const int cat_idx = start_i + local_i;
      ui::list_row(it, ctx.cx, ly.peer_list_row_y(local_i), ly.list_card_w, ly.list_h,
                   kCatLabels[cat_idx], nullptr,
                   ui::chrome_card_bg_off(), ui::chrome_card_brd_off(),
                   ctx.col_white, ctx.col_slate, ctx.font_small,
                   static_cast<uint16_t>(WidgetId::LIST_SLOT_BASE + local_i),
                   kHitTap | kHitFocusSlot, static_cast<int16_t>(cat_idx),
                   kCatIcons[cat_idx], ">", ctx.col_slate);
    }

    if (total_pages > 1) {
      render_vertical_page_pips(it, cat_pg > 1, cat_pg < total_pages, ctx.col_bg, ctx.theme_primary,
                                ly.mode_bar_zone_top());
    }
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
    return;
  }

  const bool is_peer_album_tab = (m.view == MusicView::ALBUM_TRACKS && m.drilldown == MusicDrilldown::NONE);
  const int vis_rows = is_peer_album_tab ? std::clamp(ly.peer_list_rows, 4, kMaxListSlots)
                                         : std::clamp(ly.list_rows, 4, kMaxListSlots);
  const int content_bot = is_peer_album_tab ? mode_bar_bot_limit : full_list_bot_limit;

  std::string hdr_title = m.selected_category_title;
  if (m.view == MusicView::ALBUM_TRACKS) {
    hdr_title = trk.is_speech ? "EPISODES" : "ALBUM TRACKS";
  } else if (hdr_title.empty()) {
    hdr_title = "LIBRARY";
  }
  render_page_header(it, ctx.cx, hdr_title, ctx.font_small);

  const int content_top = ly.hdr_y + dp_px(24);
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
    render_empty_state(it, ctx.cx, (content_top + content_bot) / 2, "No items available",
                       ctx.font_small, ctx.col_slate);
  }

  if (is_peer_album_tab) {
    render_vertical_page_pips(it, pg > 1, pg < tot, ctx.col_bg, ctx.theme_primary,
                              ly.mode_bar_zone_top());
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
  } else {
    render_vertical_page_pips(it, pg > 1, pg < tot, ctx.col_bg, ctx.theme_primary);
    render_back_button(it, ctx.cx, ctx.font_small, ly.back_y);
  }
}



void render_scrub_takeover(Rgb565Surface &it, const UiFrameContext &ctx,
                           const ResolvedTrackStrings &trk, float live_pos_sec, float dur_sec) {
  const auto &ms = media_state();
  const bool can_shuf = (ms.media_caps & kMediaCapShuffleSet) != 0U;
  const bool can_rep = (ms.media_caps & kMediaCapRepeatSet) != 0U;
  const MusicScrubGeom g = compute_music_scrub_geom(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);
  register_music_scrub_hits(frame_hits(), g, can_shuf, can_rep);

  const std::string title_lbl = trk.title.empty() ? std::string("Track Scrubber") : trk.title;
  const std::string fit_t = fit_text_to_width_font(title_lbl, g.hdr_max_w, ctx.font_small);
  ui::shadow_print(it, g.cx, g.hdr_title_y, ctx.font_small, ctx.col_white,
                   TextAlign::CENTER, fit_t.c_str(), true);

  char el_buf[16], du_buf[16], time_lbl[40];
  format_mmss((live_pos_sec >= 0.0f) ? static_cast<int>(live_pos_sec) : 0, el_buf, sizeof(el_buf));
  format_mmss((dur_sec > 0.5f) ? static_cast<int>(dur_sec + 0.5f) : -1, du_buf, sizeof(du_buf));
  std::snprintf(time_lbl, sizeof(time_lbl), "%s  /  %s", el_buf, du_buf);
  ui::shadow_print(it, g.cx, g.hdr_time_y, ctx.font_small, ctx.col_white,
                   TextAlign::CENTER, time_lbl, true);

  const int track_h = std::max(8, dp_px(8));
  const int sl_cy = g.slider_y0 + g.slider_h / 2;
  const float prog = (dur_sec > 0.5f) ? std::clamp(live_pos_sec / dur_sec, 0.0f, 1.0f) : 0.0f;
  fill_round_rect_aa(it, g.slider_x0, sl_cy - track_h / 2, g.slider_w, track_h,
                     static_cast<float>(track_h / 2), kChromeTrack, kChromeTrack);
  const int fill_w = static_cast<int>(std::lround(prog * static_cast<float>(g.slider_w)));
  if (fill_w > 2) {
    fill_round_rect_aa(it, g.slider_x0, sl_cy - track_h / 2, fill_w, track_h,
                       static_cast<float>(track_h / 2), ctx.theme_primary, ctx.theme_secondary);
  }
  const int thumb_r = std::clamp(dp_px(11), 9, 16);
  const int thumb_cx = std::clamp(g.slider_x0 + fill_w, g.slider_x0 + thumb_r, g.slider_x0 + g.slider_w - thumb_r);
  ui::draw_aa_disc(it, static_cast<float>(thumb_cx), static_cast<float>(sl_cy),
                   static_cast<float>(thumb_r + 2), kChromeShadow, 155);
  ui::draw_aa_disc(it, static_cast<float>(thumb_cx), static_cast<float>(sl_cy),
                   static_cast<float>(thumb_r), ctx.col_white, 255);
  ui::draw_aa_disc(it, static_cast<float>(thumb_cx), static_cast<float>(sl_cy),
                   static_cast<float>(std::max(3, thumb_r - 4)), ctx.theme_primary, 255);

  const int step_cy = g.step_y0 + g.step_h / 2;
  const int step_eh = std::clamp(g.step_vis_h, 26, g.step_h);
  ui::action_pill(it, g.step_back_x0, g.step_btn_w, step_cy, step_eh,
                  ui::chrome_card_brd_off(), ui::chrome_card_bg_off(),
                  "-15s", ctx.col_white, ctx.font_small, WidgetId::NONE, 0, 0, -1, "mdi:rewind-15");
  ui::action_pill(it, g.step_fwd_x0, g.step_btn_w, step_cy, step_eh,
                  ui::chrome_card_brd_off(), ui::chrome_card_bg_off(),
                  "+30s", ctx.col_white, ctx.font_small, WidgetId::NONE, 0, 0, -1, "mdi:fast-forward-30");

  const bool shuf_on = ms.shuffle;
  const bool rep_on = (ms.repeat_mode != static_cast<uint8_t>(MusicRepeatMode::OFF));
  const bool fav_on = ms.favorite;
  const char *rep_lbl = music_repeat_mode_label(ms.repeat_mode);
  const char *rep_icon =
      (ms.repeat_mode == static_cast<uint8_t>(MusicRepeatMode::ONE)) ? "mdi:repeat-once" : "mdi:repeat";

  const bool compact_pills = (g.mode_btn_w < 132);
  const int mode_cy = g.mode_y0 + g.mode_h / 2;
  const int mode_eh = std::clamp(g.mode_vis_h, 26, g.mode_h);
  ui::action_pill(it, g.shuf_x0, g.mode_btn_w, mode_cy, mode_eh,
                  (can_shuf && shuf_on) ? ctx.theme_primary : ui::chrome_card_brd_off(),
                  (can_shuf && shuf_on) ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                  compact_pills ? (shuf_on ? "On" : "Shuf") : "Shuffle",
                  !can_shuf ? ctx.col_gray : (shuf_on ? ctx.theme_primary : ctx.col_white),
                  ctx.font_small, WidgetId::NONE, 0, 0, -1, "mdi:shuffle-variant");
  ui::action_pill(it, g.rep_x0, g.mode_btn_w, mode_cy, mode_eh,
                  (can_rep && rep_on) ? ctx.theme_primary : ui::chrome_card_brd_off(),
                  (can_rep && rep_on) ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                  rep_lbl,
                  !can_rep ? ctx.col_gray : (rep_on ? ctx.theme_primary : ctx.col_white),
                  ctx.font_small, WidgetId::NONE, 0, 0, -1, rep_icon);
  ui::action_pill(it, g.fav_x0, g.mode_btn_w, mode_cy, mode_eh,
                  fav_on ? ctx.col_red : ui::chrome_card_brd_off(),
                  fav_on ? ui::chrome_danger_bg() : ui::chrome_card_bg_off(),
                  (g.mode_btn_w < 112) ? "Like" : (fav_on ? "Saved" : "Like"),
                  ctx.col_white, ctx.font_small,
                  WidgetId::NONE, 0, 0, -1, fav_on ? "mdi:heart" : "mdi:heart-outline");
}

void render_now_playing_view(Rgb565Surface &it, const UiFrameContext &ctx) {
  auto &rs = runtime_state();
  const auto &ms = media_state();
  const ResolvedTrackStrings trk = resolve_track_strings(ctx);

  const bool always_vis = (ctx.music_overlay_style_opt == "Always Visible");
  const bool scrub_open = (rs.music.takeover == MusicTakeover::SCRUB);
  const bool show_controls = always_vis || rs.music.hud_active;
  const bool show_metadata = always_vis || (ctx.music_overlay_style_opt == "Art + Metadata") || rs.music.hud_active;

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

  if (show_metadata) {
    const int title_span = safe_span(g.title_y, 20);
    const std::string raw_title = !trk.title.empty()
                                      ? trk.title
                                      : (trk.speaker_entity.empty() ? std::string("Select Speaker")
                                                                    : std::string("Ready to Play"));
    const std::string fit_title = fit_text_to_width_font(raw_title, title_span, ctx.font_small);
    ui::shadow_print(it, ctx.cx, g.title_y, ctx.font_small, ctx.col_white,
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
    const UiColor sub_col = (!trk.title.empty() && trk.is_playing) ? ctx.theme_secondary : ctx.col_slate;
    ui::shadow_print(it, ctx.cx, g.sub_y, ctx.font_small, sub_col,
                     TextAlign::CENTER, fit_sub.c_str());

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
    ui::shadow_print(it, g.prog_x0, g.time_y, ctx.font_small, ctx.col_slate,
                     TextAlign::CENTER_LEFT, el_buf);
    ui::shadow_print(it, g.prog_x0 + g.prog_w, g.time_y, ctx.font_small, ctx.col_slate,
                     TextAlign::CENTER_RIGHT, du_buf);
  }

  if (show_controls) {
    render_music_transport_row(it, ctx, trk, g.prev_cx, g.play_cx, g.next_cx,
                               g.transport_cy, g.side_r, g.play_r);

    const bool show_toast = !rs.music.action_feedback.empty() &&
                            (ctx.now_ms >= rs.music.feedback_timer) &&
                            ((ctx.now_ms - rs.music.feedback_timer) < 2500U);
    const int cap_span = safe_span(g.spk_caption_y, 24);
    if (show_toast) {
      const std::string fit_fb = fit_text_to_width_font(rs.music.action_feedback, cap_span, ctx.font_small);
      ui::shadow_print(it, ctx.cx, g.spk_caption_y, ctx.font_small, ctx.col_gold,
                       TextAlign::CENTER, fit_fb.c_str());
    } else {
      const UiColor spk_col = ms.is_local ? ctx.col_slate : ctx.theme_secondary;
      const int spk_ic = std::clamp(dp_px(17), 14, 26);
      const std::string fit_spk = fit_text_to_width_font(trk.speaker_name, std::max(40, cap_span - spk_ic - 8),
                                                         ctx.font_small);
      int tw = 0, xo = 0, b = 0, th = 0;
      if (ctx.font_small != nullptr) ctx.font_small->measure(fit_spk.c_str(), &tw, &xo, &b, &th);
      const int tot_w = spk_ic + 6 + tw;
      const int sx0 = ctx.cx - tot_w / 2;
      render_mdi_icon(it, ms.is_local ? "mdi:speaker" : "mdi:speaker-wireless",
                      sx0 + spk_ic / 2, g.spk_caption_y, spk_ic, spk_col, true);
      ui::shadow_print(it, sx0 + spk_ic + 6, g.spk_caption_y, ctx.font_small, spk_col,
                       TextAlign::CENTER_LEFT, fit_spk.c_str());
    }

    rs.music.sync_carousel_for_view(layout().dock_slot_count);
    register_music_now_playing_hits(frame_hits(), g, show_metadata, show_controls,
                                    rs.music.takeover);
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
    return;
  }

  register_music_now_playing_hits(frame_hits(), g, show_metadata, false, rs.music.takeover);
}

void render_volume_peer_tab(Rgb565Surface &it, const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const auto &ms = media_state();
  const ResolvedTrackStrings trk = resolve_track_strings(ctx);

  render_music_art_or_wallpaper_base(it, ctx, /*has_controls_scrim=*/true,
                                     /*has_metadata_scrim=*/true, /*dim_level=*/0);

  const MusicVolumeGeom g = compute_music_volume_geom(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);
  register_music_volume_hits(frame_hits(), g);

  const int vol_pct = std::clamp(ms.is_local ? ctx.spk_vol_pct : rs.music.remote_vol_pct, 0, 100);
  const std::string spk_fit = fit_text_to_width_font(trk.speaker_name, g.hdr_max_w, ctx.font_small);
  ui::shadow_print(it, g.cx, g.hdr_title_y, ctx.font_small, ctx.col_white,
                   TextAlign::CENTER, spk_fit.c_str(), true);

  char pct_buf[24];
  if (ms.muted) {
    std::snprintf(pct_buf, sizeof(pct_buf), "Muted (%d%%)", vol_pct);
  } else {
    std::snprintf(pct_buf, sizeof(pct_buf), "Volume %d%%", vol_pct);
  }
  ui::shadow_print(it, g.cx, g.hdr_pct_y, ctx.font_small,
                   ms.muted ? ctx.col_gold : ctx.col_white, TextAlign::CENTER, pct_buf, true);

  const int sl_r = std::min(g.slider_w / 2, dp_px(24));
  render_card_box(it, g.slider_x0, g.slider_y0, g.slider_w, g.slider_h,
                  kChromeTrack, kChromeTrackBorder, sl_r, GlassVariant::Clear, ctx.col_white);
  const float frac = ms.muted ? 0.0f : (static_cast<float>(vol_pct) * 0.01f);
  const int fill_h = static_cast<int>(std::lround(frac * static_cast<float>(g.slider_h - 8)));
  if (fill_h >= 6) {
    const int fy0 = g.slider_y0 + g.slider_h - 4 - fill_h;
    const UiColor fill_c = mix_color_q8(ctx.theme_primary, ctx.col_white, 48);
    fill_round_rect_aa(it, g.slider_x0 + 4, fy0, g.slider_w - 8, fill_h,
                       static_cast<float>(std::min((g.slider_w - 8) / 2, sl_r - 2)),
                       fill_c, ctx.theme_primary);
  }
  const int thumb_y = std::clamp(
      g.slider_y0 + g.slider_h - 6 - static_cast<int>(std::lround(frac * static_cast<float>(g.slider_h - 12))),
      g.slider_y0 + 8, g.slider_y0 + g.slider_h - 8);
  fill_round_rect_aa(it, g.slider_x0 + 8, thumb_y - 3, g.slider_w - 16, 6, 3.0f,
                     ctx.col_white, ctx.col_white);
  ui::draw_icon_speaker(it, g.slider_x0 + g.slider_w / 2, g.slider_y0 + g.slider_h - dp_px(20),
                        std::max(7, dp_px(9)), ctx.col_white, !ms.muted, true, ms.muted);

  const int step_v = std::clamp(ctx.vol_step_pct, 1, 25);
  char up_lbl[16], dn_lbl[16];
  std::snprintf(up_lbl, sizeof(up_lbl), "+%d%%", step_v);
  std::snprintf(dn_lbl, sizeof(dn_lbl), "-%d%%", step_v);
  const int btn_eh = std::clamp(g.btn_vis_h, 26, g.btn_h);

  ui::action_pill(it, g.btn_x0, g.btn_w, g.up_y0 + g.btn_h / 2, btn_eh,
                  ui::chrome_card_brd_off(), ui::chrome_card_bg_off(),
                  up_lbl, ctx.col_white, ctx.font_small, WidgetId::NONE, 0, 0, -1, "mdi:volume-plus");
  ui::action_pill(it, g.btn_x0, g.btn_w, g.down_y0 + g.btn_h / 2, btn_eh,
                  ui::chrome_card_brd_off(), ui::chrome_card_bg_off(),
                  dn_lbl, ctx.col_white, ctx.font_small, WidgetId::NONE, 0, 0, -1, "mdi:volume-minus");
  ui::action_pill(it, g.btn_x0, g.btn_w, g.mute_y0 + g.btn_h / 2, btn_eh,
                  ms.muted ? ctx.col_gold : ui::chrome_card_brd_off(),
                  ms.muted ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                  ms.muted ? "Unmute" : "Mute", ms.muted ? ctx.col_gold : ctx.col_white,
                  ctx.font_small, WidgetId::NONE, 0, 0, -1,
                  ms.muted ? "mdi:volume-off" : "mdi:volume-mute");

  render_music_stadium_mode_bar(it, ctx, trk.is_speech);
}


}  // namespace

void render_music_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  const auto &m = runtime_state().music;

  if (m.takeover == MusicTakeover::ACTION_SHEET) {
    render_music_art_or_wallpaper_base(it, ctx, /*has_controls_scrim=*/true,
                                       /*has_metadata_scrim=*/true, kArtDimMenu);
    render_music_action_sheet_takeover(it, ctx);
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
      render_music_lyrics_view(it, ctx);
      return;
    case MusicView::INFO:
      render_music_info_view(it, ctx);
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
