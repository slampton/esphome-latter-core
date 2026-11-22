#include "page_music_shared.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace latter_ui {

namespace {

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
    render_mdi_icon(it, trk.is_speech ? "mdi:rewind-10" : "mdi:skip-previous",
                    g.prev_cx, g.transport_cy, side_ic_sz, ctx.col_white, true);
    render_mdi_icon(it, trk.is_playing ? "mdi:pause" : "mdi:play",
                    g.play_cx, g.transport_cy, hero_ic_sz, ctx.col_white, true);
    render_mdi_icon(it, trk.is_speech ? "mdi:fast-forward-30" : "mdi:skip-next",
                    g.next_cx, g.transport_cy, side_ic_sz, ctx.col_white, true);

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
      const int spk_ic = std::clamp(scale_s(17), 14, 26);
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

    rs.music.sync_carousel_for_view();
    register_music_now_playing_hits(frame_hits(), g, show_metadata, show_controls,
                                    rs.music.takeover, rs.music.mode_bar_win_start, false);
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
    return;
  }

  register_music_now_playing_hits(frame_hits(), g, show_metadata, false, rs.music.takeover, 0, false);
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

  const int sl_r = std::min(g.slider_w / 2, scale_s(24));
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
  ui::draw_icon_speaker(it, g.slider_x0 + g.slider_w / 2, g.slider_y0 + g.slider_h - scale_y(20),
                        std::max(7, scale_s(9)), ctx.col_white, !ms.muted, true, ms.muted);

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
