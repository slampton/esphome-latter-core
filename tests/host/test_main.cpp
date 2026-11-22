#include "test_harness.h"

namespace latter_test {

namespace {

void test_music_app_v3_d059() {
  using namespace latter_ui;
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
      const int min_row_hit_h = std::min(min_hit, ly.slot5_stride - 2);
      for (int i = 0; i < 5; ++i) {
        const HitRegion *r = ht.find_by_id(wids[i]);
        LATTER_CHECK(r != nullptr && pad_w(r) >= min_hit && pad_h(r) >= min_row_hit_h);
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
    (void)latter_actions::emit_music_lyrics_fetch("Midnight City|M83", "media_player.office", 1000U, &mbox);
    latter_actions::emit_media_play_item("media_player.office", "library://track/42", "track", "next", false, &mbox);
    latter_actions::emit_media_queue_play("q_item_7", "media_player.office", &mbox);
    latter_actions::emit_media_seek(20, &mbox);

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
  }

  // Zero-legacy & D-043 / D-048 / D-059 / D-060 source-level invariants across split Music & UI TUs
  {
    const std::string mu_shared = read_repo_file("../../components/latter_ui/page_music_shared.h");
    const std::string mu_np = read_repo_file("../../components/latter_ui/page_music_now_playing.cpp");
    const std::string mu_lyr = read_repo_file("../../components/latter_ui/page_music_lyrics.cpp");
    const std::string mu_brw = read_repo_file("../../components/latter_ui/page_music_browse.cpp");
    const std::string mu_src = mu_shared + "\n" + mu_np + "\n" + mu_lyr + "\n" + mu_brw;

    const std::string te_src = read_repo_glob("../../components/latter_ui", "touch_", ".cpp");
    const std::string comp_src = read_repo_files({
        "../../components/latter_ui/compositor_dirty_band.cpp",
        "../../components/latter_ui/compositor_cadence.cpp",
    });
    const std::string gfx_art_src = read_repo_file("../../components/latter_ui/gfx_art_blit.cpp");
    const std::string wid_chr_src = read_repo_file("../../components/latter_ui/widgets_chrome.cpp");
    const std::string wid_txt_src = read_repo_file("../../components/latter_ui/widgets_text_layout.cpp");
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
    LATTER_CHECK(sys_yaml.find("rs.music.view == latter_ui::MusicView::SETTINGS") != std::string::npos);
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

    const std::string scrub_fn = slice_in(mu_np, "void render_scrub_takeover(", "void render_now_playing_view(");
    const std::string np_fn = slice_in(mu_np, "void render_now_playing_view(", "void render_volume_peer_tab(");
    const std::string vol_fn = slice_in(mu_np, "void render_volume_peer_tab(", "void render_music_page(");
    const std::string page_fn = slice_in(mu_np, "void render_music_page(", "");
    const std::string lyr_fn = slice_in(mu_lyr, "void render_music_lyrics_view(", "void render_music_info_view(");
    const std::string info_fn = slice_in(mu_lyr, "void render_music_info_view(", "");
    const std::string sheet_fn = slice_in(mu_brw, "void render_music_action_sheet_takeover(", "void render_music_settings_view(");
    const std::string set_fn = slice_in(mu_brw, "void render_music_settings_view(", "void render_music_browse_views(");

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

}  // namespace

}  // namespace latter_test

int main() {
  latter_test::run_protocol_rpc_tests();
  latter_test::run_state_prefs_nvs_tests();
  latter_test::run_power_audio_hal_tests();
  latter_test::run_ui_layout_compositor_tests();
  latter_test::run_ui_rgb565_5geom_tests();
  latter_test::run_touch_engine_tests();
  latter_test::run_pages_apps_tests();
  latter_test::test_music_app_v3_d059();

  std::printf(
      "[HOST TEST PASS] %dx%d (round=%d, R=%d, ppi=%d, p4=%d) — "
      "Protocol v3 + 14 Pages + System Takeovers + Scissor + HitTable + TouchEngine verified.\n",
      latter_ui::SCREEN_W, latter_ui::SCREEN_H,
      LATTER_ROUND_DISPLAY, LATTER_CORNER_R, LATTER_PANEL_PPI, LATTER_CHIP_P4);
  return 0;
}
