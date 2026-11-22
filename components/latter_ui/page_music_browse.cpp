#include "page_music_shared.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

namespace latter_ui {

namespace {

int compute_two_line_half_gap(UiFontView font, int card_h) {
  int fw = 0, fxo = 0, fb = 0, fh = 0;
  if (font != nullptr) font->measure("Ag", &fw, &fxo, &fb, &fh);
  const int line_h = (fh > 0) ? fh : 18;
  const int desired = std::max(scale_y(11), (line_h / 2) + 2);
  const int max_safe = std::max(desired, (card_h - line_h) / 2 - 2);
  return std::min(desired, std::max(line_h / 2 + 1, max_safe));
}

void render_slot_list_row(Rgb565Surface &it, const UiFrameContext &ctx,
                          int row_idx, int visible_rows, int content_top, int content_bot,
                          const char *primary_txt, const char *badge_txt,
                          bool is_highlighted, bool has_leading_play,
                          bool enable_long_press, bool has_pips, bool register_hits) {
  const MusicListRowGeom g = compute_music_list_row_geom(
      row_idx, visible_rows, content_top, content_bot, has_leading_play, has_pips,
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);

  const std::string clean_primary = sanitize_utf8_for_font(std::string(primary_txt != nullptr ? primary_txt : ""));

  if (!register_hits) {
    const std::string fit_p = fit_text_to_width_font(clean_primary, std::max(40, g.card_w - scale_x(24)), ctx.font_small);
    ui::shadow_print(it, g.card_x0 + g.card_w / 2, g.cy_row, ctx.font_small, ctx.col_slate,
                     TextAlign::CENTER, fit_p.c_str());
    return;
  }

  const UiColor bg = is_highlighted ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off();
  const UiColor brd = is_highlighted ? ctx.theme_primary : ui::chrome_card_brd_off();
  const UiColor fg = is_highlighted ? ctx.theme_primary : ctx.col_white;
  render_card_box(it, g.card_x0, g.card_y0, g.card_w, g.card_h,
                  bg, brd, kRadiusCardPx(), GlassVariant::Clear, fg);

  int text_x0 = g.card_x0 + scale_x(14);
  if (g.play_vis_w > 0) {
    const int play_cx = g.card_x0 + g.play_vis_w / 2;
    render_mdi_icon(it, "mdi:play", play_cx, g.cy_row,
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
    ui::shadow_print(it, bx, g.cy_row, ctx.font_small,
                     is_highlighted ? ctx.theme_primary : ctx.col_slate,
                     TextAlign::CENTER_RIGHT, badge_txt);
  }

  const int text_right = g.card_x0 + g.card_w - scale_x(12) - badge_reserve_w;
  const int text_max_w = std::max(40, text_right - text_x0);

  int sw = 0, xo = 0, b = 0, sh = 0;
  if (ctx.font_small != nullptr && !clean_primary.empty()) {
    ctx.font_small->measure(clean_primary.c_str(), &sw, &xo, &b, &sh);
  }

  if (sw <= text_max_w || g.card_h < scale_y(56)) {
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

}  // namespace

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
    const int icon_px = std::clamp(scale_s(17), 14, eh - 10);
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
  render_page_header(it, ctx.cx, "MUSIC SETTINGS", ctx.font_small, ctx.col_white, true);
  const int pg = std::clamp(rs.music.settings_page, 1, 2);

  struct SettingItem {
    const char *prefix;
    std::string_view value;
  };
  SettingItem items[4]{};
  int count = 0;
  if (pg == 1) {
    items[0] = {"Art", ctx.music_fullscreen_art_opt.sv()};
    items[1] = {"HUD", ctx.music_overlay_style_opt.sv()};
    items[2] = {"Albums", ctx.music_album_sort_opt.sv()};
    items[3] = {"Artist", ctx.music_artist_album_sort_opt.sv()};
    count = 4;
  } else {
    items[0] = {"Podcasts", ctx.music_podcast_sort_opt.sv()};
    count = 1;
  }

  for (int i = 0; i < count; ++i) {
    char buf[72];
    std::snprintf(buf, sizeof(buf), "%s: %.*s", items[i].prefix,
                  static_cast<int>(items[i].value.size()), items[i].value.data());
    ui::setting_cycle_row(it, ctx.cx, ly.slot5_y(i), ly.list_card_w, ly.slot5_h,
                          buf, true, ctx.font_small, ctx.col_white, ctx.col_slate,
                          ctx.theme_secondary,
                          static_cast<uint16_t>(WidgetId::LIST_SLOT_BASE + i),
                          kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
  }

  render_vertical_page_pips(it, pg > 1, pg < 2, ctx.col_bg, ctx.theme_primary, ctx.col_dark_gray,
                            ly.mode_bar_zone_top());
  render_music_stadium_mode_bar(it, ctx, trk.is_speech);
}

void render_music_browse_views(Rgb565Surface &it, const UiFrameContext &ctx) {
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
      render_empty_state(it, ctx.cx, (content_top + mode_bar_bot_limit) / 2, "No speakers found",
                         ctx.font_small, ctx.col_slate);
    }
    render_vertical_page_pips(it, pg > 1, pg < tot, ctx.col_bg, ctx.theme_primary, ctx.col_dark_gray,
                              ly.mode_bar_zone_top());
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
    return;
  }

  if (m.view == MusicView::QUEUE) {
    render_page_header(it, ctx.cx, "PLAYBACK QUEUE", ctx.font_small, ctx.col_white, true);

    const int strip_h = std::clamp(scale_y(32), 28, 44);
    const int sy0 = ly.hdr_y + scale_y(16);
    const int strip_cy = sy0 + strip_h / 2;
    const int strip_span = band_safe_span_for(
        strip_cy, strip_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 16);
    const int strip_w = std::clamp(std::min(ly.list_card_w, strip_span), 180, SCREEN_W - 24);
    const int gap = scale_x(6);
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

    const int q_hit_h = std::max(strip_h, std::min(min_hit, scale_y(44)));
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
      render_empty_state(it, ctx.cx, (content_top + mode_bar_bot_limit) / 2, "Queue is empty",
                         ctx.font_small, ctx.col_slate);
    }
    render_vertical_page_pips(it, pg > 1, pg < tot, ctx.col_bg, ctx.theme_primary, ctx.col_dark_gray,
                              ly.mode_bar_zone_top());
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
    return;
  }

  if (m.view == MusicView::LIBRARY && m.drilldown == MusicDrilldown::NONE) {
    render_page_header(it, ctx.cx, "LIBRARY", ctx.font_small, ctx.col_white, true);

    static constexpr const char *kCatLabels[7] = {
        "Favorites", "Playlists", "Artists", "Albums", "Podcasts", "Radio", "Audiobooks"};
    static constexpr const char *kCatIcons[7] = {
        "mdi:heart", "mdi:playlist-music", "mdi:account-music",
        "mdi:album", "mdi:podcast", "mdi:radio-tower", "mdi:book-open-variant"};

    const int cat_pg = std::clamp(m.category_page, 1, 2);
    const int start_i = (cat_pg == 1) ? 0 : 4;
    const int page_cnt = (cat_pg == 1) ? 4 : 3;

    for (int local_i = 0; local_i < page_cnt; ++local_i) {
      const int cat_idx = start_i + local_i;
      ui::list_row(it, ctx.cx, ly.slot5_y(local_i), ly.list_card_w, ly.slot5_h,
                   kCatLabels[cat_idx], nullptr,
                   ui::chrome_card_bg_off(), ui::chrome_card_brd_off(),
                   ctx.col_white, ctx.col_slate, ctx.font_small,
                   static_cast<uint16_t>(WidgetId::LIST_SLOT_BASE + local_i),
                   kHitTap | kHitFocusSlot, static_cast<int16_t>(cat_idx),
                   kCatIcons[cat_idx], ">", ctx.col_slate);
    }

    render_vertical_page_pips(it, cat_pg > 1, cat_pg < 2, ctx.col_bg, ctx.theme_primary, ctx.col_dark_gray,
                              ly.mode_bar_zone_top());
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
    return;
  }

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

  const int content_top = ly.hdr_y + scale_y(24);
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
    render_vertical_page_pips(it, pg > 1, pg < tot, ctx.col_bg, ctx.theme_primary, ctx.col_dark_gray,
                              ly.mode_bar_zone_top());
    render_music_stadium_mode_bar(it, ctx, trk.is_speech);
  } else {
    render_vertical_page_pips(it, pg > 1, pg < tot, ctx.col_bg, ctx.theme_primary, ctx.col_dark_gray);
    render_back_button(it, ctx.cx, ctx.font_small, ly.back_y);
  }
}

}  // namespace latter_ui
