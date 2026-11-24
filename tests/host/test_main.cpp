#include "test_harness.h"

#include <iostream>

namespace latter_test {

namespace {

void test_music_app_v3_d059() {
  using namespace latter_ui;
  using namespace latter_core;
  reset_runtime_state();
  reset_all_v3_rev_guards();

  struct FlagshipPanel {
    int w;
    int h;
    bool round;
    bool wearable;
    int ppi;
  };
  static constexpr FlagshipPanel kPanels[5] = {
      {466, 466, true, false, 266},
      {480, 480, false, false, 313},
      {410, 502, false, true, 315},
      {320, 480, false, false, 165},
      {720, 720, false, false, 254},
  };

  auto check_zero_aabb_overlaps = [](const FrameHitTable &ht, int w, int h) {
    for (uint8_t i = 0; i < ht.count; ++i) {
      const auto &a = ht.regions[i];
      LATTER_CHECK(a.x0 >= 0 && a.y0 >= 0 && a.x1 < w && a.y1 < h && a.x0 <= a.x1 && a.y0 <= a.y1);
      for (uint8_t j = static_cast<uint8_t>(i + 1U); j < ht.count; ++j) {
        const auto &b = ht.regions[j];
        const bool x_overlap = (a.x0 <= b.x1) && (b.x0 <= a.x1);
        const bool y_overlap = (a.y0 <= b.y1) && (b.y0 <= a.y1);
        LATTER_CHECK(!(x_overlap && y_overlap));
      }
    }
  };

  static constexpr uint16_t kLibSheetWids[5] = {
      WidgetId::MUSIC_ACTION_PLAY_NOW,
      WidgetId::MUSIC_ACTION_PLAY_NEXT,
      WidgetId::MUSIC_ACTION_ADD_QUEUE,
      WidgetId::MUSIC_ACTION_REPLACE_QUEUE,
      WidgetId::MUSIC_ACTION_START_RADIO,
  };
  static constexpr uint16_t kQueueSheetWids[5] = {
      WidgetId::MUSIC_ACTION_PLAY_NOW,
      WidgetId::MUSIC_ACTION_PLAY_NEXT,
      WidgetId::MUSIC_ACTION_MOVE_UP,
      WidgetId::MUSIC_ACTION_MOVE_DOWN,
      WidgetId::MUSIC_ACTION_REMOVE_QUEUE,
  };

  auto pad_w = [](const HitRegion *r) { return r->width() + 2 * static_cast<int>(r->hit_pad); };
  auto pad_h = [](const HitRegion *r) { return r->height() + 2 * static_cast<int>(r->hit_pad); };

  for (const auto &p : kPanels) {
    const int min_hit = mm_to_px_for(7.0f, p.ppi);
    const auto ly = compute_panel_layout(p.w, p.h, p.round, p.wearable, p.ppi);
    const auto np = compute_music_now_playing_geom(p.w, p.h, p.round, p.wearable, p.ppi);
    LATTER_CHECK(np.prog_h == ((p.w >= 640) ? 6 : 4));

    for (int win_start = 0; win_start <= 5; ++win_start) {
      FrameHitTable ht{};
      register_music_now_playing_hits(ht, np, true, true, MusicTakeover::NONE, win_start, true);
      check_zero_aabb_overlaps(ht, p.w, p.h);

      for (uint16_t wid : {WidgetId::MUSIC_SCRUB_OPEN_BTN, WidgetId::MUSIC_PREV_BTN,
                           WidgetId::MUSIC_PLAY_PAUSE, WidgetId::MUSIC_NEXT_BTN}) {
        const HitRegion *r = ht.find_by_id(wid);
        LATTER_CHECK(r != nullptr && pad_w(r) >= min_hit && pad_h(r) >= min_hit);
      }
      for (int i = 0; i < 4; ++i) {
        const HitRegion *slot_r = ht.find_by_id(static_cast<uint16_t>(WidgetId::MODE_BAR_SLOT_BASE + i));
        LATTER_CHECK(slot_r != nullptr && slot_r->param == win_start + i);
        LATTER_CHECK(pad_w(slot_r) >= min_hit && pad_h(slot_r) >= min_hit);
      }
    }

    {
      FrameHitTable ht{};
      const auto lg = compute_music_lyrics_transport_geom(p.w, p.h, p.round, p.wearable, p.ppi);
      register_music_lyrics_transport_hits(ht, lg);
      for (int i = 0; i < 4; ++i) {
        ht.add_circle(np.mode_slot_xs[i], np.mode_bar_cy, np.mode_bar_r,
                      static_cast<uint16_t>(WidgetId::MODE_BAR_SLOT_BASE + i),
                      kHitTap | kHitFocusSlot | kHitFocusCircle,
                      static_cast<int16_t>(i), np.mode_bar_pad);
      }
      check_zero_aabb_overlaps(ht, p.w, p.h);
      for (uint16_t wid : {WidgetId::MUSIC_PREV_BTN, WidgetId::MUSIC_PLAY_PAUSE, WidgetId::MUSIC_NEXT_BTN}) {
        const HitRegion *r = ht.find_by_id(wid);
        LATTER_CHECK(r != nullptr && pad_w(r) >= min_hit && pad_h(r) >= min_hit);
        LATTER_CHECK(is_inside_bezel_for(r->cx(), r->cy(), p.w, p.h, p.round, p.wearable, 1));
        LATTER_CHECK(lg.vp_bot < (r->y0 - static_cast<int>(r->hit_pad)));
        LATTER_CHECK((r->y1 + static_cast<int>(r->hit_pad)) < (np.mode_bar_cy - np.mode_bar_r - np.mode_bar_pad));
      }
      LATTER_CHECK((lg.prev_cx + lg.side_hit_r) < (lg.play_cx - lg.play_hit_r));
      LATTER_CHECK((lg.play_cx + lg.play_hit_r) < (lg.next_cx - lg.side_hit_r));
      const int anchor_y = (p.h * 38) / 100;
      LATTER_CHECK(anchor_y <= lg.vp_bot - 28);
      if (p.w > 520) {
        LATTER_CHECK((lg.next_cx + lg.side_r) - (lg.prev_cx - lg.side_r) <= kMaxContentColW);
      }
    }

    {
      FrameHitTable ht{};
      const auto vg = compute_music_volume_geom(p.w, p.h, p.round, p.wearable, p.ppi);
      register_music_volume_hits(ht, vg);
      check_zero_aabb_overlaps(ht, p.w, p.h);
      const HitRegion *vsl = ht.find_by_id(WidgetId::MUSIC_VOL_SLIDER);
      LATTER_CHECK(vsl != nullptr && (vsl->flags & kHitVertSlider) != 0U);
      LATTER_CHECK(vsl->width() >= min_hit && vsl->height() >= min_hit);
    }

    {
      FrameHitTable ht{};
      const auto sg = compute_music_scrub_geom(p.w, p.h, p.round, p.wearable, p.ppi);
      register_music_scrub_hits(ht, sg);
      check_zero_aabb_overlaps(ht, p.w, p.h);
      for (uint16_t wid : {WidgetId::MUSIC_SCRUB_BAR, WidgetId::MUSIC_SCRUB_STEP_BACK,
                           WidgetId::MUSIC_SCRUB_STEP_FWD, WidgetId::MUSIC_SHUFFLE_BTN,
                           WidgetId::MUSIC_REPEAT_BTN, WidgetId::MUSIC_FAVORITE_BTN}) {
        const HitRegion *r = ht.find_by_id(wid);
        LATTER_CHECK(r != nullptr && r->width() >= min_hit && r->height() >= min_hit);
      }
    }

    for (const uint16_t *wids : {kLibSheetWids, kQueueSheetWids}) {
      FrameHitTable ht{};
      const auto ag = compute_music_action_sheet_geom(p.w, p.h, p.round, p.wearable, p.ppi, 5);
      register_music_action_sheet_hits(ht, ag, wids);
      ht.add_rect(p.w / 2 - 80, ag.back_hit_y0, 160, p.h - ag.back_hit_y0, WidgetId::BACK_BUTTON, kHitTap);
      check_zero_aabb_overlaps(ht, p.w, p.h);
      // 5-row action sheets keep the compact slot5 vertical stride on smaller high-PPI panels.
      const int max_gap_pad = std::max(0, (ly.slot5_stride - ly.slot5_h - 1) / 2);
      const int expected_row_hit_h =
          (p.h <= 520 && p.ppi >= 266) ? (ly.slot5_h + 2 * max_gap_pad) : min_hit;
      for (int i = 0; i < 5; ++i) {
        const HitRegion *r = ht.find_by_id(wids[i]);
        LATTER_CHECK(r != nullptr && pad_w(r) >= min_hit && pad_h(r) >= expected_row_hit_h);
      }
    }

    {
      FrameHitTable ht{};
      const int c_top = ly.hdr_y + scale_y_for(24, p.h);
      const int c_bot = (p.round ? scale_min_for(404, p.h) : (p.h - scale_y_for(64, p.h))) - scale_y_for(6, p.h);
      for (int r = 0; r < kMusicFullListPageSize; ++r) {
        const auto rg = compute_music_list_row_geom(
            r, kMusicFullListPageSize, c_top, c_bot, true, false,
            p.w, p.h, p.round, p.wearable, p.ppi);
        LATTER_CHECK(rg.card_w == ly.list_card_w && rg.card_h == ly.slot5_h);
        register_music_list_row_hits(ht, rg, r, true);
      }
      check_zero_aabb_overlaps(ht, p.w, p.h);
    }
  }

  {
    static_assert(kMusicPeerTabCount == 9);
    static_assert(static_cast<int>(MusicView::SETTINGS) == 8);
    LATTER_CHECK(step_music_peer_tab(MusicView::NOW_PLAYING, +1) == MusicView::LIBRARY);
    LATTER_CHECK(step_music_peer_tab(MusicView::NOW_PLAYING, -1) == MusicView::SETTINGS);

    MusicRuntimeState m{};
    m.view = MusicView::VOLUME;
    m.hud_active = true;
    m.hud_timer = 1000U;
    LATTER_CHECK(step_music_hud_auto_fade(m, 6000U, "Art Only").sprung_back_to_now_playing);
  }

  {
    auto &rs = runtime_state();
    auto &ms = media_state();
    auto &lyr = rs.music.lyrics;
    ms.title = "Midnight City";
    ms.artist = "M83";
    ms.playing = true;
    ms.state = "playing";
    ms.duration = 240.0f;
    ms.position = 10.0f;
    ms.position_updated_ms = 5000U;
    const std::string tkey = build_music_track_key(ms.title, ms.artist);
    const std::string c0 = std::string("0") + kProtocolFieldSep + "Waiting in a car" +
                           kProtocolRecordSep + "10000" + kProtocolFieldSep + "Waiting for a ride";
    const std::string c1 = std::string("20000") + kProtocolFieldSep + "The night city grows";
    LATTER_CHECK(!apply_music_lyrics_action(4, 0, 2, tkey, true, c0, lyr));
    LATTER_CHECK(apply_music_lyrics_action(4, 1, 2, tkey, true, c1, lyr));
    LATTER_CHECK(lyr.synced && lyr.lines.size() == 3U);

    const int doc_cys[3] = {40, 180, 320};
    step_music_lyrics_auto_scroll(lyr, 11500U, 6500U, 110, doc_cys, 3, 500);
    LATTER_CHECK(lyr.active_line_idx == 1 && lyr.scroll_px == 70);
  }

  {
    MockRpcOutbox mbox{};
    latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
    (void)latter_core::emit_music_lyrics_fetch("Midnight City|M83", "media_player.office", 1000U, &mbox);
    latter_core::emit_media_play_item("media_player.office", "library://track/42", "track", "next", false, &mbox);
    latter_core::emit_media_queue_play("q_item_7", "media_player.office", &mbox);
    latter_core::emit_media_seek(20, &mbox);

    uint32_t pump_ms = 1100U;
    while (latter_rpc::has_pending_frames()) {
      latter_rpc::step_outbox_loop(pump_ms += 40U);
    }
    std::string all_rpc;
    for (const auto &f : mbox.published) all_rpc += f;
    LATTER_CHECK(all_rpc.find("\"mode\":\"lyrics_fetch\"") != std::string::npos);
    LATTER_CHECK(all_rpc.find("\"enqueue\":\"next\"") != std::string::npos);
    LATTER_CHECK(all_rpc.find("\"command\":\"queue_play\"") != std::string::npos);
    LATTER_CHECK(all_rpc.find("\"seek_position\":\"20\"") != std::string::npos);

    // Slim music_browse / lyrics_fetch payloads must omit empty/default/irrelevant keys so typical
    // requests fit in a single 1/1 RPC outbox frame while preserving non-default mode-relevant sorts.
    auto check_single_chunk = [](auto fn, const char *must_have, const char *must_omit) {
      MockRpcOutbox box{};
      latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
      fn(&box);
      uint32_t t = 2000U;
      while (latter_rpc::has_pending_frames()) {
        latter_rpc::step_outbox_loop(t += 40U);
      }
      LATTER_CHECK(box.published.size() == 1U);
      LATTER_CHECK(box.published[0].find(":1/1|") != std::string::npos);
      if (must_have != nullptr) LATTER_CHECK(box.published[0].find(must_have) != std::string::npos);
      if (must_omit != nullptr) LATTER_CHECK(box.published[0].find(must_omit) == std::string::npos);
    };
    check_single_chunk(
        [](MockRpcOutbox *b) {
          (void)latter_core::emit_music_browse(
              "speakers", 1, "", "media_player.pocket_assistant_music", "media_player.garage_hifi",
              "name", "year", "newest", 4, 2000U, b);
        },
        "\"last_speaker\":\"media_player.garage_hifi\"", "\"album_sort\"");
    check_single_chunk(
        [](MockRpcOutbox *b) {
          (void)latter_core::emit_music_browse(
              "active_queue", 1, "", "media_player.pocket_assistant_music", "media_player.garage_hifi",
              "name", "year", "newest", 4, 2000U, b);
        },
        "\"mode\":\"active_queue\"", "\"last_speaker\"");
    check_single_chunk(
        [](MockRpcOutbox *b) {
          (void)latter_core::emit_music_browse(
              "album", 1, "", "media_player.pocket_assistant_music", "media_player.garage_hifi",
              "year_desc", "year", "newest", 5, 2000U, b);
        },
        "\"album_sort\":\"year_desc\"", "\"podcast_sort\"");
    check_single_chunk(
        [](MockRpcOutbox *b) {
          (void)latter_core::emit_music_browse(
              "artist_albums", 1, "Fugazi", "media_player.pocket_assistant_music", "",
              "name", "name", "newest", 5, 2000U, b);
        },
        "\"artist_album_sort\":\"name\"", "\"album_sort\"");
    check_single_chunk(
        [](MockRpcOutbox *b) {
          (void)latter_core::emit_music_browse(
              "podcast_episodes", 1, "library://podcast/9", "media_player.pocket_assistant_music", "",
              "name", "year", "oldest", 5, 2000U, b);
        },
        "\"podcast_sort\":\"oldest\"", "\"artist_album_sort\"");
    check_single_chunk(
        [](MockRpcOutbox *b) {
          (void)latter_core::emit_music_lyrics_fetch(
              "Waiting Room|Fugazi", "media_player.pocket_assistant_music", 2000U, b);
        },
        "\"search\":\"Waiting Room|Fugazi\"", "\"album_sort\"");
  }

  // Zero-legacy & D-043 / D-048 / D-059 / D-060 source-level invariants across Music & UI TUs
  {
    const std::string mu_src = read_repo_file("../../components/latter_ui/page_music.cpp");

    const std::string te_src = read_repo_glob("../../components/latter_ui", "touch_", ".cpp");
    const std::string comp_src = read_repo_file("../../components/latter_ui/ui_compositor.cpp");
    const std::string gfx_art_src = read_repo_file("../../components/latter_ui/ui_gfx.cpp");
    const std::string wid_chr_src = read_repo_file("../../components/latter_ui/ui_widgets.cpp");
    const std::string &wid_txt_src = wid_chr_src;
    const std::string lay_src = read_repo_file("../../include/ui_layout.h");
    const std::string sys_yaml = read_repo_files({
        "../../modules/system_core.yaml",
        "../../modules/system_ui_sync.yaml",
    });

    LATTER_CHECK(mu_src.find("render_progress_arc_r224") == std::string::npos);
    LATTER_CHECK(mu_src.find("overlay_mode") == std::string::npos);
    LATTER_CHECK(mu_src.find("GlassVariant::ReadableDark") == std::string::npos);
    LATTER_CHECK(mu_src.find("MUSIC_ROW_MORE_BASE") == std::string::npos);
    LATTER_CHECK(mu_src.find("MusicDrilldown::SETTINGS") == std::string::npos);
    LATTER_CHECK(mu_src.find("ui::draw_icon_play_pause(") != std::string::npos);
    LATTER_CHECK(mu_src.find("ui::draw_icon_hamburger(") != std::string::npos);
    LATTER_CHECK(mu_src.find("ui::draw_icon_gear(") != std::string::npos);
    LATTER_CHECK(mu_src.find("render_lyrics_art_backdrop(") != std::string::npos);
    LATTER_CHECK(mu_src.find("register_music_list_row_hits(") != std::string::npos);
    LATTER_CHECK(mu_src.find("case MusicView::SETTINGS:") != std::string::npos);

    LATTER_CHECK(te_src.find("MUSIC_ROW_MORE_BASE") == std::string::npos);
    LATTER_CHECK(te_src.find("MusicDrilldown::SETTINGS") == std::string::npos);
    LATTER_CHECK(te_src.find("activate_music_peer_tab(") != std::string::npos);
    LATTER_CHECK(te_src.find("MUSIC_ROW_PLAY_BASE") != std::string::npos);

    LATTER_CHECK(lay_src.find("std::abs(new_start - target_start) > 9") != std::string::npos);
    LATTER_CHECK(sys_yaml.find("rs.music.view == latter_core::MusicView::SETTINGS") != std::string::npos);
    LATTER_CHECK(wid_chr_src.find("draw_icon_play_pause(") != std::string::npos);
    LATTER_CHECK(wid_chr_src.find("draw_icon_hamburger(") != std::string::npos);

    const size_t ph_s = wid_txt_src.find("void render_page_header(Rgb565Surface &it, int cx, const char *title");
    const size_t ph_e = wid_txt_src.find("void render_sub_header(", ph_s);
    LATTER_CHECK(ph_s != std::string::npos && ph_e != std::string::npos);
    LATTER_CHECK(wid_txt_src.substr(ph_s, ph_e - ph_s).find("UiColor::WHITE") != std::string::npos);

    const size_t art_s = gfx_art_src.find("compute_art_row_scale_q8(");
    const size_t art_e = gfx_art_src.find("void art_letterbox_bounds(", art_s);
    LATTER_CHECK(art_s != std::string::npos && art_e != std::string::npos);
    const std::string art_fn = gfx_art_src.substr(art_s, art_e - art_s);
    LATTER_CHECK(art_fn.find("return 140;") != std::string::npos);
    LATTER_CHECK(art_fn.find("ramp(y, scale_y(104), 140, scale_y(148), 256)") != std::string::npos);

    LATTER_CHECK(comp_src.find("if (!show_metadata && !show_controls)") != std::string::npos);
    LATTER_CHECK(comp_src.find("mark_music_progress_dirty_band(") != std::string::npos);

    auto slice_in = [](const std::string &src, std::string_view sig, std::string_view next_sig) -> std::string {
      const size_t s = src.find(sig);
      const size_t e = next_sig.empty() ? src.size() : src.find(next_sig, s);
      LATTER_CHECK(s != std::string::npos && e != std::string::npos && e > s);
      return src.substr(s, e - s);
    };

    const std::string lyr_fn = slice_in(mu_src, "void render_music_lyrics_view(", "void render_music_info_view(");
    const std::string info_fn = slice_in(mu_src, "void render_music_info_view(", "int compute_two_line_half_gap(");
    const std::string sheet_fn = slice_in(mu_src, "void render_music_action_sheet_takeover(", "void render_music_settings_view(");
    const std::string set_fn = slice_in(mu_src, "void render_music_settings_view(", "void render_music_browse_views(");
    const std::string scrub_fn = slice_in(mu_src, "void render_scrub_takeover(", "void render_now_playing_view(");
    const std::string np_fn = slice_in(mu_src, "void render_now_playing_view(", "void render_volume_peer_tab(");
    const std::string vol_fn = slice_in(mu_src, "void render_volume_peer_tab(", "void render_music_page(");
    const std::string page_fn = slice_in(mu_src, "void render_music_page(", "");

    LATTER_CHECK(scrub_fn.find("render_card_box(") == std::string::npos);
    const size_t first_sheet_card = sheet_fn.find("render_card_box(");
    LATTER_CHECK(first_sheet_card != std::string::npos &&
                 sheet_fn.find("render_card_box(", first_sheet_card + 1U) == std::string::npos);
    LATTER_CHECK(lyr_fn.find("render_card_box(") == std::string::npos);
    LATTER_CHECK(info_fn.find("render_card_box(") == std::string::npos);
    LATTER_CHECK(set_fn.find("render_music_stadium_mode_bar(") != std::string::npos);
    LATTER_CHECK(set_fn.find("render_back_button(") == std::string::npos);
    LATTER_CHECK(np_fn.find("/*dim_level=*/0") != std::string::npos);
    LATTER_CHECK(vol_fn.find("/*dim_level=*/0") != std::string::npos);

    const size_t sheet_guard = page_fn.find("m.takeover == MusicTakeover::ACTION_SHEET");
    const size_t browse_call = page_fn.find("render_music_browse_views(");
    LATTER_CHECK(sheet_guard != std::string::npos && browse_call != std::string::npos &&
                 sheet_guard < browse_call);
  }
}

void append_utf8_codepoint(uint32_t cp, std::string &out) {
  if (cp <= 0x7FU) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FFU) {
    out.push_back(static_cast<char>(0xC0U | (cp >> 6)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if (cp <= 0xFFFFU) {
    out.push_back(static_cast<char>(0xE0U | (cp >> 12)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if (cp <= 0x10FFFFU) {
    out.push_back(static_cast<char>(0xF0U | (cp >> 18)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 12) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  }
}

bool parse_hex4(std::string_view sv, size_t pos, uint32_t &out) noexcept {
  if (pos + 4U > sv.size()) return false;
  uint32_t v = 0U;
  for (size_t k = 0; k < 4U; ++k) {
    const char c = sv[pos + k];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
    else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
    else return false;
  }
  out = v;
  return true;
}

bool json_extract_raw_value(std::string_view line, std::string_view key, size_t &val_pos) noexcept {
  std::string needle = "\"";
  needle.append(key);
  needle.push_back('"');
  size_t pos = 0U;
  while (pos < line.size()) {
    const size_t hit = line.find(needle, pos);
    if (hit == std::string_view::npos) return false;
    size_t p = hit + needle.size();
    while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
    if (p < line.size() && line[p] == ':') {
      ++p;
      while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
      val_pos = p;
      return p < line.size();
    }
    pos = hit + 1U;
  }
  return false;
}

bool json_get_string(std::string_view line, std::string_view key, std::string &out) {
  size_t p = 0U;
  if (!json_extract_raw_value(line, key, p) || line[p] != '"') return false;
  ++p;
  out.clear();
  while (p < line.size()) {
    const char ch = line[p++];
    if (ch == '"') return true;
    if (ch != '\\') {
      out.push_back(ch);
      continue;
    }
    if (p >= line.size()) return false;
    const char esc = line[p++];
    switch (esc) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u': {
        uint32_t u1 = 0U;
        if (!parse_hex4(line, p, u1)) return false;
        p += 4U;
        if (u1 >= 0xD800U && u1 <= 0xDBFFU && p + 6U <= line.size() &&
            line[p] == '\\' && line[p + 1U] == 'u') {
          uint32_t u2 = 0U;
          if (parse_hex4(line, p + 2U, u2) && u2 >= 0xDC00U && u2 <= 0xDFFFU) {
            p += 6U;
            const uint32_t cp = 0x10000U + (((u1 - 0xD800U) << 10) | (u2 - 0xDC00U));
            append_utf8_codepoint(cp, out);
            break;
          }
        }
        append_utf8_codepoint(u1, out);
        break;
      }
      default:
        out.push_back(esc);
        break;
    }
  }
  return false;
}

int64_t json_get_int(std::string_view line, std::string_view key, int64_t def_val) noexcept {
  size_t p = 0U;
  if (!json_extract_raw_value(line, key, p)) return def_val;
  bool neg = false;
  if (line[p] == '-') {
    neg = true;
    ++p;
  }
  if (p >= line.size() || line[p] < '0' || line[p] > '9') return def_val;
  int64_t acc = 0;
  while (p < line.size() && line[p] >= '0' && line[p] <= '9') {
    acc = acc * 10 + (line[p] - '0');
    ++p;
  }
  return neg ? -acc : acc;
}

bool json_get_bool(std::string_view line, std::string_view key, bool def_val) noexcept {
  size_t p = 0U;
  if (!json_extract_raw_value(line, key, p)) return def_val;
  if (line.substr(p, 4U) == "true") return true;
  if (line.substr(p, 5U) == "false") return false;
  return def_val;
}

void json_append_escaped(std::string &dst, std::string_view sv) {
  dst.push_back('"');
  for (unsigned char b : sv) {
    switch (b) {
      case '"': dst += "\\\""; break;
      case '\\': dst += "\\\\"; break;
      case '\b': dst += "\\b"; break;
      case '\f': dst += "\\f"; break;
      case '\n': dst += "\\n"; break;
      case '\r': dst += "\\r"; break;
      case '\t': dst += "\\t"; break;
      default:
        if (b < 0x20U) {
          char ubuf[7];
          std::snprintf(ubuf, sizeof(ubuf), "\\u%04x", static_cast<unsigned>(b));
          dst += ubuf;
        } else {
          dst.push_back(static_cast<char>(b));
        }
        break;
    }
  }
  dst.push_back('"');
}

size_t count_non_empty_records(std::string_view sv, char sep) noexcept {
  if (sv.empty()) return 0U;
  size_t count = 0U;
  size_t pos = 0U;
  while (pos <= sv.size()) {
    const size_t next = sv.find(sep, pos);
    const std::string_view tok =
        (next == std::string_view::npos) ? sv.substr(pos) : sv.substr(pos, next - pos);
    if (!tok.empty()) ++count;
    if (next == std::string_view::npos) break;
    pos = next + 1U;
  }
  return count;
}

std::string build_verify_wire_dump() {
  using namespace latter_ui;
  using namespace latter_core;
  const auto &prefs = persisted_prefs();
  const auto &ms = media_state();
  const auto &wx = weather_state();
  const auto &ac = area_ctx_state();
  const auto &cal = calendar_state();
  const auto &cwin = calendar_window_store();
  const auto &rem = remote_state();
  const auto &hh = home_hub_state();
  const auto &sc = shortcuts_state();
  const auto &rs = runtime_state();

  int alarms_enabled = 0;
  for (const auto &a : prefs.alarms) {
    if (a.enabled) ++alarms_enabled;
  }

  std::string dock_csv;
  for (size_t i = 0; i < sc.dock_items.size(); ++i) {
    if (i > 0U) dock_csv.push_back(',');
    dock_csv += sc.dock_items[i].target;
  }

  int area_slot_count = 0;
  const BrowseSlotItem *aslots = rs.area_entity_slots();
  for (size_t i = 0; i < DeviceRuntimeState::kCardSlotsPerPage; ++i) {
    if (!aslots[i].id.empty() || (!aslots[i].name.empty() && aslots[i].type != "text")) {
      ++area_slot_count;
    }
  }

  CalendarRecord cal_recs[8]{};
  const size_t cal_rec_n = parse_calendar_records(cal.summary, cal_recs, 8U);
  const std::string_view first_cal_title =
      (cwin.count > 0) ? std::string_view(cwin.events[0].title)
                       : (cal_rec_n > 0U ? cal_recs[0].title : std::string_view{});
  const std::string_view first_cal_loc =
      (cwin.count > 0) ? std::string_view(cwin.events[0].location)
                       : (cal_rec_n > 0U ? cal_recs[0].location : std::string_view{});

  std::vector<std::string> phrases;
  const int phrase_cnt = parse_broadcast_phrases(ac.broadcast_phrases, phrases);

  std::string out;
  out.reserve(4096U);
  out += "{\"ok\":true,\"core_rev\":";
  out += std::to_string(LATTER_CORE_REV);

  out += ",\"prefs\":{\"prefs_seq\":";
  out += std::to_string(prefs.prefs_seq);
  out += ",\"wake_word_mode\":";
  json_append_escaped(out, get_pref_value_str(PrefKey::WAKE_WORD_MODE, prefs));
  out += ",\"auto_brightness_mode\":";
  json_append_escaped(out, get_pref_value_str(PrefKey::AUTO_BRIGHTNESS_MODE, prefs));
  out += ",\"do_not_disturb\":";
  out += prefs.do_not_disturb ? "true" : "false";
  out += ",\"bedtime_active\":";
  out += prefs.bedtime_active ? "true" : "false";
  out += ",\"manual_brightness\":";
  out += std::to_string(prefs.manual_brightness_pct());
  out += ",\"day_brightness\":";
  out += std::to_string(prefs.day_brightness_pct());
  out += ",\"night_brightness\":";
  out += std::to_string(prefs.night_brightness_pct());
  out += ",\"bedtime_brightness\":";
  out += std::to_string(prefs.bedtime_brightness_pct());
  out += ",\"theme_color_hex\":";
  json_append_escaped(out, format_hex_rgb(prefs.custom_theme_rgb));
  out += ",\"dock_count\":";
  out += std::to_string(sc.dock_items.size());
  out += ",\"dock_items\":";
  json_append_escaped(out, dock_csv);
  out += ",\"world_clocks\":";
  json_append_escaped(out, prefs.world_clocks_spec);
  out += ",\"alarms_count\":";
  out += std::to_string(alarms_enabled);
  out += ",\"alarms_summary\":";
  json_append_escaped(out, format_alarms_summary(prefs));
  out += ",\"dirty_prefs\":";
  json_append_escaped(out, format_dirty_prefs(prefs));
  out += ",\"values\":{";
  for (uint8_t k = 0; k < static_cast<uint8_t>(PrefKey::COUNT); ++k) {
    if (k > 0U) out.push_back(',');
    const auto pk = static_cast<PrefKey>(k);
    json_append_escaped(out, pref_key_name(pk));
    out.push_back(':');
    json_append_escaped(out, get_pref_value_str(pk, prefs));
  }
  out += ",\"weather_sim_condition\":";
  json_append_escaped(out, pref_weather_sim_cstr());
  out += "}}";

  out += ",\"media\":{\"title\":";
  json_append_escaped(out, ms.title);
  out += ",\"artist\":";
  json_append_escaped(out, ms.artist);
  out += ",\"album\":";
  json_append_escaped(out, ms.album);
  out += ",\"speaker_entity\":";
  json_append_escaped(out, ms.active_speaker);
  out += ",\"speaker_name\":";
  json_append_escaped(out, ms.speaker_name);
  out += ",\"caps\":";
  out += std::to_string(ms.media_caps);
  out += ",\"is_playing\":";
  out += ms.playing ? "true" : "false";
  out += ",\"state\":";
  json_append_escaped(out, ms.state);
  out += ",\"has_volume\":";
  out += ms.has_volume ? "true" : "false";
  out += ",\"volume_pct\":";
  out += std::to_string(static_cast<int>(std::lround(ms.volume * 100.0f)));
  out += ",\"queue_count\":";
  out += std::to_string(ms.queue_count);
  out += ",\"lyrics_count\":";
  out += std::to_string(rs.music.lyrics.lines.size());
  out += "}";

  char fbuf[32];
  out += ",\"weather\":{\"temp\":";
  if (std::isnan(wx.temperature)) out += "null";
  else {
    std::snprintf(fbuf, sizeof(fbuf), "%.2f", static_cast<double>(wx.temperature));
    out += fbuf;
  }
  out += ",\"wind_bearing\":";
  if (std::isnan(wx.wind_bearing)) out += "null";
  else {
    std::snprintf(fbuf, sizeof(fbuf), "%.2f", static_cast<double>(wx.wind_bearing));
    out += fbuf;
  }
  out += ",\"condition\":";
  json_append_escaped(out, wx.condition);
  out += ",\"temp_unit\":";
  json_append_escaped(out, wx.temp_unit);
  out += ",\"sun_state\":";
  json_append_escaped(out, wx.sun_state);
  out += ",\"moon_phase\":";
  json_append_escaped(out, wx.moon_phase);
  out += ",\"forecast_count\":";
  out += std::to_string(count_non_empty_records(wx.forecast_5d, kProtocolRecordSep));
  out += "}";

  out += ",\"area\":{\"area_id\":";
  json_append_escaped(out, ac.area_id);
  out += ",\"area_name\":";
  json_append_escaped(out, ac.area_name);
  out += ",\"site_id\":";
  json_append_escaped(out, ac.site_id);
  out += ",\"ambient_light_pct\":";
  out += std::to_string(static_cast<int>(ac.area_ambient_pct));
  out += ",\"ambient_lux\":";
  json_append_escaped(out, ac.area_lux_display);
  out += ",\"security_pin_policy\":";
  json_append_escaped(out, ac.security_pin_policy);
  out += ",\"alarm_entity\":";
  json_append_escaped(out, ac.alarm_entity);
  out += ",\"alarm_caps\":";
  out += std::to_string(ac.alarm_caps);
  out += ",\"broadcast_phrase_count\":";
  out += std::to_string(phrase_cnt);
  out += ",\"slot_count\":";
  out += std::to_string(area_slot_count);
  out += ",\"slot0_id\":";
  json_append_escaped(out, aslots[0].id);
  out += ",\"slot0_name\":";
  json_append_escaped(out, aslots[0].name);
  out += ",\"slot0_level\":";
  out += std::to_string(aslots[0].level);
  out += ",\"area_list_slot0_name\":";
  json_append_escaped(out, rs.area_list_slots()[0].name);
  out += ",\"music_slot0_name\":";
  json_append_escaped(out, rs.music_slots()[0].name);
  out += "}";

  out += ",\"calendar\":{\"event_count\":";
  out += std::to_string(cal.count);
  out += ",\"window_event_count\":";
  out += std::to_string(cwin.count);
  out += ",\"source_count\":";
  out += std::to_string(count_non_empty_records(cal.calendars_meta, kProtocolRecordSep));
  out += ",\"location_count\":";
  out += std::to_string(count_non_empty_records(cal.locations_catalog, kProtocolRecordSep));
  out += ",\"first_title\":";
  json_append_escaped(out, first_cal_title);
  out += ",\"first_location\":";
  json_append_escaped(out, first_cal_loc);
  out += "}";

  out += ",\"remote\":{\"active_slot\":";
  out += std::to_string(rem.active_slot);
  out += ",\"configured_count\":";
  out += std::to_string(rem.configured_count);
  out += ",\"slot0_name\":";
  json_append_escaped(out, rs.remote.slot_names[0]);
  out += ",\"slot0_entity\":";
  json_append_escaped(out, rs.remote.slot_ents[0]);
  out += "}";

  out += ",\"home_hub\":{\"thermostat_count\":";
  out += std::to_string(hh.thermostat_count);
  out += ",\"first_thermostat_name\":";
  json_append_escaped(out, hh.thermostat_count > 0 ? hh.thermostats[0].name : std::string_view{});
  out += ",\"entry_count\":";
  out += std::to_string(hh.entry_count);
  out += ",\"first_entry_name\":";
  json_append_escaped(out, hh.entry_count > 0 ? hh.entries[0].name : std::string_view{});
  out += ",\"vacuum_count\":";
  out += std::to_string(hh.vacuum_count);
  out += ",\"first_vacuum_name\":";
  json_append_escaped(out, hh.vacuum_count > 0 ? hh.vacuums[0].name : std::string_view{});
  out += ",\"camera_count\":";
  out += std::to_string(count_non_empty_records(ac.cameras_summary, kProtocolRecordSep));
  out += "}";

  out += ",\"shortcuts\":{\"dock_count\":";
  out += std::to_string(sc.dock_items.size());
  out += ",\"pinned_count\":";
  out += std::to_string(sc.pinned_items.size());
  out += ",\"first_pinned_label\":";
  json_append_escaped(out, sc.pinned_items.empty() ? std::string_view{} : std::string_view(sc.pinned_items[0].label));
  out += ",\"first_dock_badge_style\":";
  out += std::to_string(sc.dock_items.empty() ? 0 : static_cast<int>(sc.dock_items[0].badge_style));
  out += ",\"first_dock_accent\":";
  out += std::to_string(sc.dock_items.empty() ? 0 : static_cast<int>(sc.dock_items[0].accent));
  out += ",\"first_pinned_badge_style\":";
  out += std::to_string(sc.pinned_items.empty() ? 0 : static_cast<int>(sc.pinned_items[0].badge_style));
  out += ",\"first_pinned_accent\":";
  out += std::to_string(sc.pinned_items.empty() ? 0 : static_cast<int>(sc.pinned_items[0].accent));
  out += ",\"activity_text\":";
  json_append_escaped(out, ac.activity_sources);
  out += ",\"activity_source_mask\":";
  out += std::to_string(activity_source_mask());
  out += "}}";
  return out;
}

int run_verify_wire_cli() {
  using namespace latter_ui;
  using namespace latter_core;
  reset_runtime_state();
  reset_all_v3_rev_guards();
  reset_calendar_window_store();
  persisted_prefs() = PersistedDevicePrefs{};
  device_prefs_state() = DevicePrefsState{};
  v3_domain_reducer().reset();
  latter_rpc::clear_pin_session_key();
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};

  std::string line;
  while (std::getline(std::cin, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
      line.pop_back();
    }
    if (line.empty()) continue;

    std::string op;
    if (!json_get_string(line, "op", op)) {
      std::cout << "{\"ok\":false,\"error\":\"missing_op\"}\n" << std::flush;
      continue;
    }

    if (op == "reset") {
      reset_runtime_state();
      reset_all_v3_rev_guards();
      reset_calendar_window_store();
      persisted_prefs() = PersistedDevicePrefs{};
      device_prefs_state() = DevicePrefsState{};
      v3_domain_reducer().reset();
      latter_rpc::clear_pin_session_key();
      latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
      std::cout << "{\"ok\":true}\n" << std::flush;
    } else if (op == "push_v3") {
      std::string domain;
      std::string payload;
      (void)json_get_string(line, "domain", domain);
      (void)json_get_string(line, "payload", payload);
      const uint32_t now_ms = static_cast<uint32_t>(json_get_int(line, "now_ms", 1000));
      bool parsed = false;
      if (domain == "area_context") {
        parsed = parse_v3_area_ctx_payload(payload, area_ctx_state(), home_hub_state());
      } else if (domain == "active_media") {
        parsed = parse_v3_media_payload(payload, media_state());
      } else if (domain == "remote_config") {
        parsed = parse_v3_remote_payload(payload, remote_state());
        if (parsed) parse_latter_remote_slots_summary(remote_state().slots_summary);
      } else if (domain == "weather_forecast") {
        parsed = parse_v3_weather_payload(payload, weather_state());
      } else if (domain == "calendar_agenda") {
        parsed = parse_v3_calendar_payload(payload, calendar_state());
      } else if (domain == "device_prefs") {
        uint32_t eff_mask = 0U;
        parsed = parse_v3_device_prefs_payload(payload, eff_mask, now_ms);
        device_prefs_state().last_effects = eff_mask;
      } else if (domain == "shortcuts") {
        parsed = parse_v3_shortcuts_payload(payload, shortcuts_state());
      }
      V3DomainEffects eff{};
      if (parsed) {
        V3ReduceContext rctx{};
        rctx.now_ms = now_ms;
        eff = v3_domain_reducer().reduce(domain, rctx);
      }
      std::cout << "{\"ok\":true,\"parsed\":" << (parsed ? "true" : "false")
                << ",\"area_changed\":" << (eff.area_changed ? "true" : "false")
                << ",\"sync_brightness\":" << (eff.sync_display_brightness ? "true" : "false")
                << ",\"mismatch_warns\":" << v3_mismatch_warn_count() << "}\n"
                << std::flush;
    } else if (op == "set_slots") {
      std::string mode_str;
      std::string channel_str;
      (void)json_get_string(line, "channel", channel_str);
      if (!json_get_string(line, "mode", mode_str)) {
        if (channel_str == "area_list") mode_str = "areas";
        else if (channel_str == "music") mode_str = "favorites";
        else if (channel_str == "calendar") mode_str = "calendar_agenda";
        else mode_str = "entities";
      }
      std::string slots_blob;
      if (!json_get_string(line, "slots", slots_blob)) {
        for (int i = 0; i < 5; ++i) {
          const std::string skey = "s" + std::to_string(i);
          std::string sv;
          if (!json_get_string(line, skey, sv)) break;
          if (i > 0) slots_blob.push_back(kProtocolRecordSep);
          slots_blob += sv;
        }
      }
      const int req_id = static_cast<int>(json_get_int(line, "req_id", 0));
      const int page = static_cast<int>(json_get_int(line, "page", 1));
      const int total = static_cast<int>(
          json_get_int(line, "total_pages", json_get_int(line, "total", 1)));
      const bool default_area_slots =
          (channel_str == "area_entities" || mode_str == "entities" ||
           mode_str.rfind("area_", 0) == 0U);
      const bool is_area_slots = json_get_bool(line, "is_area_slots", default_area_slots);
      const bool accepted = apply_slots_action(
          req_id, mode_str, page, total, slots_blob, 1000U, is_area_slots);
      std::cout << "{\"ok\":true,\"accepted\":" << (accepted ? "true" : "false") << "}\n"
                << std::flush;
    } else if (op == "set_calendar_view") {
      std::string win_start;
      std::string win_end;
      std::string events;
      (void)json_get_string(line, "window_start", win_start);
      (void)json_get_string(line, "window_end", win_end);
      (void)json_get_string(line, "events", events);
      const int req_id = static_cast<int>(json_get_int(line, "req_id", 1));
      const int cidx = static_cast<int>(json_get_int(line, "chunk_index", 0));
      const int ccnt = static_cast<int>(json_get_int(line, "chunk_count", 1));
      const bool committed = apply_calendar_view_action(req_id, cidx, ccnt, win_start, win_end, events);
      std::cout << "{\"ok\":true,\"committed\":" << (committed ? "true" : "false")
                << ",\"count\":" << calendar_window_store().count << "}\n"
                << std::flush;
    } else if (op == "set_music_lyrics") {
      std::string track_key;
      std::string lines_blob;
      (void)json_get_string(line, "track_key", track_key);
      (void)json_get_string(line, "lines", lines_blob);
      const int req_id = static_cast<int>(json_get_int(line, "req_id", 1));
      const int cidx = static_cast<int>(json_get_int(line, "chunk_index", 0));
      const int ccnt = static_cast<int>(json_get_int(line, "chunk_count", 1));
      const bool synced = json_get_bool(line, "synced", true);
      const bool committed = apply_music_lyrics_action(req_id, cidx, ccnt, track_key, synced, lines_blob);
      std::cout << "{\"ok\":true,\"committed\":" << (committed ? "true" : "false")
                << ",\"line_count\":" << runtime_state().music.lyrics.lines.size() << "}\n"
                << std::flush;
    } else if (op == "local_pref_edit") {
      std::string key_name;
      std::string val_str;
      (void)json_get_string(line, "key", key_name);
      (void)json_get_string(line, "value", val_str);
      bool applied = false;
      for (uint8_t k = 0; k < static_cast<uint8_t>(PrefKey::COUNT); ++k) {
        const auto pk = static_cast<PrefKey>(k);
        if (key_name == pref_key_name(pk)) {
          PrefMask chg{};
          uint32_t eff = 0U;
          applied = set_pref_by_key_str(persisted_prefs(), pk, val_str, chg, eff);
          persisted_prefs().dirty_mask |= chg;
          break;
        }
      }
      std::string resp = "{\"ok\":true,\"applied\":";
      resp += applied ? "true" : "false";
      resp += ",\"dirty_prefs\":";
      json_append_escaped(resp, build_hello_dirty_prefs(persisted_prefs()));
      resp += "}\n";
      std::cout << resp << std::flush;
    } else if (op == "wrap_pin") {
      std::string key_id;
      std::string key_hex;
      std::string entity_id = "lock.front_door";
      std::string action = "unlock";
      std::string pin;
      const bool has_key = json_get_string(line, "key_id", key_id) &&
                           json_get_string(line, "key_hex", key_hex);
      (void)json_get_string(line, "entity_id", entity_id);
      (void)json_get_string(line, "action", action);
      (void)json_get_string(line, "pin", pin);
      bool key_ok = latter_rpc::has_pin_session_key();
      if (has_key) {
        key_ok = latter_rpc::set_pin_session_key(key_id, key_hex);
      }
      const int64_t nonce_arg = json_get_int(line, "nonce", -1);
      if (key_ok && nonce_arg >= 1) {
        latter_rpc::detail::pin_session_state().nonce = static_cast<uint32_t>(nonce_arg - 1);
      }
      const std::string wrapped = key_ok ? latter_rpc::wrap_pin_code(entity_id, action, pin) : std::string{};
      std::string resp = "{\"ok\":true,\"key_ok\":";
      resp += key_ok ? "true" : "false";
      resp += ",\"wrapped\":";
      json_append_escaped(resp, wrapped);
      resp += "}\n";
      std::cout << resp << std::flush;
    } else if (op == "dump") {
      std::cout << build_verify_wire_dump() << "\n" << std::flush;
    } else {
      std::cout << "{\"ok\":false,\"error\":\"unknown_op\"}\n" << std::flush;
    }
  }
  return 0;
}

}  // namespace

}  // namespace latter_test

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--verify-wire") {
      return latter_test::run_verify_wire_cli();
    }
  }
  latter_test::run_protocol_rpc_tests();
  latter_test::run_state_prefs_nvs_tests();
  latter_test::run_power_audio_hal_tests();
  latter_test::run_ui_layout_compositor_tests();
  latter_test::run_ui_rgb565_5geom_tests();
  latter_test::run_touch_engine_tests();
  latter_test::run_pages_apps_tests();
  latter_test::run_showcase_nav_tests();
  latter_test::run_mdi_plus_tests();
  latter_test::run_games_tests();
  latter_test::test_music_app_v3_d059();

  std::printf(
      "[HOST TEST PASS] %dx%d (round=%d, R=%d, ppi=%d, p4=%d) — "
      "Protocol v3 + 14 Pages + System Takeovers + Scissor + HitTable + TouchEngine verified.\n",
      latter_ui::SCREEN_W, latter_ui::SCREEN_H,
      LATTER_ROUND_DISPLAY, LATTER_CORNER_R, LATTER_PANEL_PPI, LATTER_CHIP_P4);
  return 0;
}
