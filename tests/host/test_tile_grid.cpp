#include "test_harness.h"

namespace latter_test {

namespace {

void test_tile_grid_traits_and_6geom_bezel_invariants() {
  using namespace latter_core;
  using namespace latter_ui;

  struct BoardGeomSpec {
    const char *name;
    int w;
    int h;
    bool round;
    bool wearable;
    int corner_r;
    int ppi;
    int view_scale_pct;
    bool stationary;
    int expected_cols;
    int expected_rows;
  };
  static constexpr BoardGeomSpec kBoards[9] = {
      {"320x480_rect",      320, 480, false, false,   0, 165, 145, false, 2, 3},
      {"410x502_wearable",  410, 502, false, true,   98, 315, 100, false, 2, 3},
      {"466x466_round",     466, 466, true,  false, 233, 266, 100, false, 2, 3},
      {"480x480_square",    480, 480, false, false,  76, 313, 100, false, 2, 3},
      {"720x720_square",    720, 720, false, false,   0, 254, 145, true,  3, 3},
      {"360x360_mini",      360, 360, true,  false, 180, 255, 100, false, 2, 2},
      {"800x480_wide",      800, 480, false, false,   0, 187, 135, true,  4, 2},
      {"1024x600_wide",    1024, 600, false, false,   0, 170, 145, true,  4, 3},
      {"1280x800_tablet",  1280, 800, false, false,   0, 149, 150, true,  4, 3},
  };

  // Extra edge-case trait checks
  static_assert(tile_grid_cols_for(800, 480, false) == 4);
  static_assert(tile_grid_rows_for(800, 480, false) == 2);
  static_assert(tile_grid_cols_for(360, 360, true) == 2);
  static_assert(tile_grid_rows_for(360, 360, true) == 2);
  static_assert(tile_grid_cols_for(1280, 800, false) == 4);
  static_assert(tile_grid_cols_for(800, 800, true) == 2);
  static_assert(tile_grid_rows_for(1024, 600, false) == 3);

  for (const auto &b : kBoards) {
    const int cols = tile_grid_cols_for(b.w, b.h, b.round, b.ppi, b.view_scale_pct, b.stationary);
    const int rows = tile_grid_rows_for(b.w, b.h, b.round, b.ppi, b.view_scale_pct, b.stationary);
    LATTER_CHECK(cols == b.expected_cols);
    LATTER_CHECK(rows == b.expected_rows);

    const auto geom = compute_tile_grid_geom(b.w, b.h, b.corner_r, b.round, cols, rows,
                                             b.ppi, b.view_scale_pct, b.stationary);
    const auto ly = compute_panel_layout(b.w, b.h, b.round, b.wearable, b.ppi, b.corner_r, b.view_scale_pct, b.stationary);
    const int min_hit = mm_to_px_for(7.0f, b.ppi);

    LATTER_CHECK(geom.cols == cols && geom.rows == rows);
    LATTER_CHECK(geom.cell_w >= min_hit);
    LATTER_CHECK(geom.cell_h >= min_hit);
    LATTER_CHECK(geom.grid_y > ly.sub_hdr_y);
    LATTER_CHECK(geom.grid_y + geom.grid_h < ly.home_bar_y);

    // Every single valid (col, row, col_span, row_span) tile must stay strictly inside
    // the bezel (inset >= 5 px) on all 4 corners and never overlap distinct cells.
    for (int r = 0; r < rows; ++r) {
      for (int c = 0; c < cols; ++c) {
        for (int rh = 1; rh <= rows - r; ++rh) {
          for (int cw = 1; cw <= cols - c; ++cw) {
            const TileRect tr = tile_grid_cell_rect(geom, c, r, cw, rh);
            LATTER_CHECK(tr.w >= min_hit && tr.h >= min_hit);
            LATTER_CHECK(is_inside_bezel_for(tr.x, tr.y, b.w, b.h, b.round, b.wearable, 5, b.corner_r));
            LATTER_CHECK(is_inside_bezel_for(tr.x + tr.w - 1, tr.y, b.w, b.h, b.round, b.wearable, 5, b.corner_r));
            LATTER_CHECK(is_inside_bezel_for(tr.x, tr.y + tr.h - 1, b.w, b.h, b.round, b.wearable, 5, b.corner_r));
            LATTER_CHECK(is_inside_bezel_for(tr.x + tr.w - 1, tr.y + tr.h - 1, b.w, b.h, b.round, b.wearable, 5, b.corner_r));
          }
        }
      }
    }
  }
}

void test_tile_grid_solver_reflow_and_compaction() {
  using namespace latter_core;

  // 1. Empty input -> 0 placements, 1 page, source page 0
  {
    int8_t src_pages[kTileGridMaxLogicalPages]{};
    const auto res = resolve_tile_grid_layout(nullptr, 0, 3, 3, nullptr, 0, src_pages, kTileGridMaxLogicalPages);
    LATTER_CHECK(res.placement_count == 0 && res.total_pages == 1 && src_pages[0] == 0);
  }

  // 2. Explicit multi-span + collision fallback + auto-flow on 3x3
  {
    TileGridInputItem in[6] = {
        {0, 0, 0, 2, 2, 10},  // Hero 2x2 at (0,0)
        {0, 1, 1, 1, 1, 11},  // Collides with Hero -> defers to first open slot (2,0)
        {0, 2, 1, 1, 2, 12},  // Tall 1x2 at (2,1)
        {-1, -1, -1, 2, 1, 13},  // Auto-flow wide 2x1 -> fits at (0,2)
        {0, 0, 0, 4, 1, 14},  // Oversized span 4x1 collides -> clamped to 3x1, overflows to page 1 (0,0)
        {-1, -1, -1, 1, 1, 15},  // Auto-flow 1x1 -> fits page 1 (0,1) since page 0 is full
    };
    TileGridPlacement out[16]{};
    int8_t src_pages[kTileGridMaxLogicalPages]{};
    const auto res = resolve_tile_grid_layout(in, 6, 3, 3, out, 16, src_pages, kTileGridMaxLogicalPages);
    LATTER_CHECK(res.placement_count == 6 && res.total_pages == 2);
    LATTER_CHECK(src_pages[0] == 0 && src_pages[1] == 0);  // Page 1 spawned by overflow from logical page 0

    auto find_pl = [&](int idx) -> const TileGridPlacement * {
      for (int i = 0; i < res.placement_count; ++i) {
        if (out[i].item_index == idx) return &out[i];
      }
      return nullptr;
    };
    const auto *p10 = find_pl(10);
    const auto *p11 = find_pl(11);
    const auto *p12 = find_pl(12);
    const auto *p13 = find_pl(13);
    const auto *p14 = find_pl(14);
    const auto *p15 = find_pl(15);
    LATTER_CHECK(p10 != nullptr && p10->visual_page == 0 && p10->col == 0 && p10->row == 0 && p10->col_span == 2 && p10->row_span == 2);
    LATTER_CHECK(p11 != nullptr && p11->visual_page == 0 && p11->col == 2 && p11->row == 0 && p11->col_span == 1 && p11->row_span == 1);
    LATTER_CHECK(p12 != nullptr && p12->visual_page == 0 && p12->col == 2 && p12->row == 1 && p12->col_span == 1 && p12->row_span == 2);
    LATTER_CHECK(p13 != nullptr && p13->visual_page == 0 && p13->col == 0 && p13->row == 2 && p13->col_span == 2 && p13->row_span == 1);
    LATTER_CHECK(p14 != nullptr && p14->visual_page == 1 && p14->col == 0 && p14->row == 0 && p14->col_span == 3 && p14->row_span == 1 && p14->source_page == 0);
    LATTER_CHECK(p15 != nullptr && p15->visual_page == 1 && p15->col == 0 && p15->row == 1 && p15->col_span == 1 && p15->row_span == 1);
  }

  // 3. Cross-device 3x3 -> 2x3 reflow + empty logical page compaction + custom title inheritance
  {
    TileGridInputItem in[11]{};
    int k = 0;
    // 9 items on logical page 0 (full 3x3)
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        in[k] = TileGridInputItem{0, static_cast<int8_t>(c), static_cast<int8_t>(r), 1, 1, static_cast<int16_t>(k)};
        ++k;
      }
    }
    // Logical pages 1 and 2 empty; logical page 3 has 2 items
    in[9] = TileGridInputItem{3, 0, 0, 2, 1, 9};
    in[10] = TileGridInputItem{3, 0, 1, 1, 1, 10};

    TileGridPlacement out[16]{};
    int8_t src_pages[kTileGridMaxLogicalPages]{};
    const auto res = resolve_tile_grid_layout(in, 11, 2, 3, out, 16, src_pages, kTileGridMaxLogicalPages);
    // Page 0 keeps cols 0..1 (6 items), col 2 (3 items) overflows to logical page 1 (inheriting source_page=0),
    // logical page 2 stays empty and is compacted away, logical page 3 becomes visual_page 2 (source_page=3).
    LATTER_CHECK(res.placement_count == 11);
    LATTER_CHECK(res.total_pages == 3);
    LATTER_CHECK(src_pages[0] == 0);
    LATTER_CHECK(src_pages[1] == 0);
    LATTER_CHECK(src_pages[2] == 3);

    int vis0_cnt = 0, vis1_cnt = 0, vis2_cnt = 0;
    for (int i = 0; i < res.placement_count; ++i) {
      if (out[i].visual_page == 0) ++vis0_cnt;
      else if (out[i].visual_page == 1) ++vis1_cnt;
      else if (out[i].visual_page == 2) ++vis2_cnt;
    }
    LATTER_CHECK(vis0_cnt == 6 && vis1_cnt == 3 && vis2_cnt == 2);
  }
}

void test_protocol_v3_14col_titles_and_hibernate_v3() {
  using namespace latter_core;

  // 1. 14-column shortcut record parsing + partial coordinate auto-flow fallback + span clamping
  {
    ShortcutItem it{};
    LATTER_CHECK(parse_shortcut_record(
        "entity|light.kitchen|toggle|Kitchen|mdi:lightbulb|0|1|glass|gold|2|1|0|2|2", it));
    LATTER_CHECK(it.page == 2 && it.col == 1 && it.row == 0 && it.col_span == 2 && it.row_span == 2);

    // Missing row -> auto-flow (-1,-1,-1) while still honoring explicit span
    ShortcutItem partial{};
    LATTER_CHECK(parse_shortcut_record(
        "entity|switch.coffee|toggle|Coffee|mdi:coffee|0|0|inverted|green|1|0||2|1", partial));
    LATTER_CHECK(partial.page == -1 && partial.col == -1 && partial.row == -1);
    LATTER_CHECK(partial.col_span == 2 && partial.row_span == 1);

    // Legacy 9-column record -> auto-flow 1x1
    ShortcutItem legacy{};
    LATTER_CHECK(parse_shortcut_record(
        "route|app:music||Music||0|-1|glass|primary", legacy));
    LATTER_CHECK(legacy.page == -1 && legacy.col == -1 && legacy.row == -1 &&
                 legacy.col_span == 1 && legacy.row_span == 1);
  }

  // 2. Multi-chunk shortcuts commit with PINNED_PAGE_TITLES (index 8) + UTF-8 clamp
  {
    shortcuts_state().clear();
    const std::string p0 =
        std::string("entity|light.a|toggle|Lamp A|mdi:lamp|0|1|glass|gold|0|0|0|2|1") + kProtocolRecordSep +
        "entity|light.b|toggle|Lamp B|mdi:lamp|0|0|glass|default|0|0|1|1|2";
    const std::string p1 =
        std::string("entity|scene.movie|turn_on|Movie|mdi:movie|0|-1|inverted|primary|3|0|0|2|2");
    const std::string titles =
        std::string("0|Living Area") + kProtocolRecordSep +
        "3|Cinema Night Super Long Title Exceeding 24 Bytes" + kProtocolRecordSep +
        "99|InvalidPage" + kProtocolRecordSep +
        "1|   ";

    auto make_chunk = [&](int idx, int cnt, const std::string &pin_blob, const std::string &titles_blob) {
      std::vector<std::string> tok(static_cast<size_t>(ShortcutsField::COUNT), "");
      tok[static_cast<size_t>(ShortcutsField::CHUNK_INDEX)] = std::to_string(idx);
      tok[static_cast<size_t>(ShortcutsField::CHUNK_COUNT)] = std::to_string(cnt);
      tok[static_cast<size_t>(ShortcutsField::DOCK_ITEMS)] = (idx == 0) ? "route|app:pinned||Pinned||0|-1" : "";
      tok[static_cast<size_t>(ShortcutsField::PINNED_TOTAL)] = "3";
      tok[static_cast<size_t>(ShortcutsField::PINNED_ITEMS)] = pin_blob;
      tok[static_cast<size_t>(ShortcutsField::PINNED_PAGE_TITLES)] = titles_blob;
      return build_v3_packet(10, 1, tok);
    };

    // Chunk 0 stages titles without committing yet
    LATTER_CHECK(!parse_v3_shortcuts_payload(make_chunk(0, 2, p0, titles)));
    LATTER_CHECK(shortcuts_state().pinned_page_titles.empty());
    LATTER_CHECK(shortcuts_state().staged_pinned_page_titles.size() == 2U);

    // Chunk 1 commits atomically
    LATTER_CHECK(parse_v3_shortcuts_payload(make_chunk(1, 2, p1, "")));
    LATTER_CHECK(shortcuts_state().pinned_items.size() == 3U);
    LATTER_CHECK(shortcuts_state().pinned_page_titles.size() == 2U);
    LATTER_CHECK(resolve_pinned_page_title(shortcuts_state(), 0) == "Living Area");
    LATTER_CHECK(resolve_pinned_page_title(shortcuts_state(), 1).empty());
    const std::string_view t3 = resolve_pinned_page_title(shortcuts_state(), 3);
    LATTER_CHECK(!t3.empty() && t3.size() <= 24U && is_valid_utf8(t3));

    // Hardware manifest reports matching grid_cols / grid_rows
    const std::string manifest = build_hardware_manifest_json();
    const int exp_cols = tile_grid_cols_for(LATTER_SCREEN_W, LATTER_SCREEN_H, LATTER_ROUND_DISPLAY != 0,
                                            LATTER_PANEL_PPI, LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);
    const int exp_rows = tile_grid_rows_for(LATTER_SCREEN_W, LATTER_SCREEN_H, LATTER_ROUND_DISPLAY != 0,
                                            LATTER_PANEL_PPI, LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);
    LATTER_CHECK(manifest.find("\"grid_cols\":" + std::to_string(exp_cols)) != std::string::npos);
    LATTER_CHECK(manifest.find("\"grid_rows\":" + std::to_string(exp_rows)) != std::string::npos);
  }

  // 3. HibernateSnapshot v3 round-trip: up to 12 page-0 slots + spans + page-0 title
  {
    reset_hibernate_snapshot_for_test();
    auto &sc = shortcuts_state();
    sc.clear();
    sc.epoch = 20U;
    sc.rev = 2U;
    sc.dock_configured = true;
    sc.live_received = true;
    sc.pinned_page_titles.push_back(PinnedPageTitle{0U, "Desk Controls"});
    // Populate 12 auto-flow items so 4x3 boards store all 12 slots, 3x3 store 9, 2x3 store 6.
    // Give item 0 a 2x1 span on explicit (0,0) when testing spans.
    ShortcutItem wide{};
    wide.kind = ShortcutKind::ENTITY;
    wide.target = "light.main_desk";
    wide.action = "toggle";
    wide.label = "Main Desk";
    wide.icon = "mdi:desk-lamp";
    wide.state = 1;
    wide.page = 0;
    wide.col = 0;
    wide.row = 0;
    wide.col_span = 2;
    wide.row_span = 1;
    sc.pinned_items.push_back(wide);
    for (int i = 1; i < 12; ++i) {
      ShortcutItem it{};
      it.kind = ShortcutKind::ENTITY;
      it.target = "switch.slot_" + std::to_string(i);
      it.action = "toggle";
      it.label = "Slot " + std::to_string(i);
      it.icon = "mdi:toggle-switch";
      it.state = 1;
      sc.pinned_items.push_back(it);
    }

    LATTER_CHECK(save_hibernate_snapshot("PST8PDT,M3.2.0,M11.1.0/2"));
    sc.clear();
    LATTER_CHECK(load_hibernate_snapshot_if_cold());

    const int cols = tile_grid_cols_for(LATTER_SCREEN_W, LATTER_SCREEN_H, LATTER_ROUND_DISPLAY != 0);
    const int rows = tile_grid_rows_for(LATTER_SCREEN_W, LATTER_SCREEN_H, LATTER_ROUND_DISPLAY != 0);
    // Page 0 holds 1 wide (2 cells) + (cols*rows - 2) 1x1 items = cols*rows - 1 items!
    const size_t expected_pg0 = static_cast<size_t>(cols * rows - 1);
    LATTER_CHECK(sc.pinned_items.size() == expected_pg0);
    LATTER_CHECK(sc.pinned_items[0].target == "light.main_desk");
    LATTER_CHECK(sc.pinned_items[0].page == 0 && sc.pinned_items[0].col == 0 && sc.pinned_items[0].row == 0);
    LATTER_CHECK(sc.pinned_items[0].col_span == 2 && sc.pinned_items[0].row_span == 1);
    LATTER_CHECK(sc.pinned_items[0].state == -1);  // D-085 neutral cold-boot state preserved
    LATTER_CHECK(resolve_pinned_page_title(sc, 0) == "Desk Controls");
  }
}

void test_tile_grid_render_and_horizontal_touch() {
  using namespace latter_core;
  using namespace latter_ui;

  HostSurfaceFixture fix{};
  reset_runtime_state();
  reset_all_v3_rev_guards();
  touch_router() = TouchRouter{};
  edge_bounce().clear();

  auto &eng = touch_engine();
  static std::vector<UiHostCmd> s_cmds;
  s_cmds.clear();
  eng.runtime() = UiTouchRuntime{};
  eng.runtime().panel_is_on = true;
  eng.runtime().exec_cmd = +[](UiHostCmd cmd, int, int) { s_cmds.push_back(cmd); };

  auto &rs = runtime_state();
  auto &sc = shortcuts_state();
  sc.clear();
  sc.epoch = 5U;
  sc.rev = 1U;
  sc.dock_configured = true;
  sc.live_received = true;
  sc.pinned_page_titles.push_back(PinnedPageTitle{0U, "Studio Grid"});
  sc.pinned_page_titles.push_back(PinnedPageTitle{1U, "Second Page"});

  // Page 0: mix of 1x1, 2x1 wide, 1x2 tall, plus extra items to verify >6 slot taps on 3x3/4x3
  // and multi-page horizontal navigation across all 6 boards.
  const int cols = tile_grid_cols_for(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0);
  const int rows = tile_grid_rows_for(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0);
  const int per_pg = cols * rows;
  for (int i = 0; i < per_pg + 4; ++i) {
    ShortcutItem it{};
    it.kind = ShortcutKind::ENTITY;
    it.target = "light.pin_" + std::to_string(i);
    it.action = "toggle";
    it.label = "Tile " + std::to_string(i);
    it.icon = "mdi:lightbulb";
    it.state = (i % 3 == 0) ? 1 : ((i % 3 == 1) ? 0 : 2);
    it.accent = (i % 2 == 0) ? ShortcutAccent::GOLD : ShortcutAccent::PRIMARY;
    it.badge_style = (i == 0) ? ShortcutBadgeStyle::INVERTED : ShortcutBadgeStyle::GLASS;
    sc.pinned_items.push_back(it);
  }
  LATTER_CHECK(pinned_total_pages() >= 2);

  // 1. Render PageId::PINNED page 0 and verify tiles + CHEVRON_RIGHT (CHEVRON_LEFT disabled on page 0)
  rs.nav.gui_page = PageId::PINNED;
  rs.nav.pinned_menu_page = 0;
  rs.nav.menu_scroll_active = false;
  dirty_band() = DirtyBandTracker{};
  (void)dirty_band().begin_frame(PageId::PINNED, 0, 0U, true);
  frame_hits().clear();
  render_active_page(fix.surf, fix.ctx);

  LATTER_CHECK(frame_hits().find_by_id(WidgetId::CHEVRON_LEFT) == nullptr);
  const HitRegion *chev_r = frame_hits().find_by_id(WidgetId::CHEVRON_RIGHT);
  LATTER_CHECK(chev_r != nullptr);
  const int min_7mm = mm_to_px(7.0f);
  LATTER_CHECK(chev_r->width() + 2 * static_cast<int>(chev_r->hit_pad) >= min_7mm);
  LATTER_CHECK(chev_r->height() + 2 * static_cast<int>(chev_r->hit_pad) >= min_7mm);

  // On 3x3 (9 slots) / 4x3 (12 slots), verify slots >= 6 exist and dispatch cleanly!
  MockRpcOutbox rpc_box{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_rpc::bind_sensor_sink(&rpc_box);
  link_state().ha_connected = true;

  if (per_pg > 6) {
    // Slot 7 has state = 7 % 3 == 1 (off, tappable!)
    const HitRegion *slot7 = frame_hits().find_by_id(static_cast<uint16_t>(WidgetId::PINNED_CARD_BASE + 7));
    LATTER_CHECK(slot7 != nullptr && slot7->param == 7);
    eng.on_touch_down(slot7->cx(), slot7->cy(), 10000U);
    eng.on_touch_up(10035U);
    uint32_t t_drain = 10050U;
    while (latter_rpc::has_pending_frames()) {
      latter_rpc::step_outbox_loop(t_drain);
      t_drain += 30U;
    }
    std::string wire;
    for (const auto &f : rpc_box.published) wire += f;
    LATTER_CHECK(wire.find("light.pin_7") != std::string::npos);
  }
  latter_rpc::bind_sensor_sink(static_cast<MockRpcOutbox *>(nullptr));

  // 2. Horizontal swipe RIGHT on page 0 -> boundary edge bounce (Horizontal) & full-frame bounce flag!
  const int swipe_dx = KinematicMetrics::for_ppi(LATTER_PANEL_PPI).swipe_live_px + 14;
  edge_bounce().clear();
  s_cmds.clear();
  eng.on_touch_down(SCREEN_CX - swipe_dx / 2, SCREEN_CY, 12000U);
  eng.on_touch_move(SCREEN_CX + swipe_dx / 2 + 4, SCREEN_CY, 12040U);
  eng.on_touch_up(12060U);
  LATTER_CHECK(!rs.nav.menu_scroll_active);
  LATTER_CHECK(edge_bounce().is_active(PageId::PINNED, GestureAxisLock::Horizontal, 12080U));
  LATTER_CHECK(any_page_horiz_bounce_active(PageId::PINNED, 0, 12080U));
  LATTER_CHECK(!any_dock_anim_active(PageId::PINNED, 0, 12080U));

  // 3. Horizontal swipe LEFT on page 0 -> advances to page 1 via MENU_SCROLL_TRANSITION!
  s_cmds.clear();
  eng.on_touch_down(SCREEN_CX + swipe_dx / 2, SCREEN_CY, 13000U);
  eng.on_touch_move(SCREEN_CX - swipe_dx / 2 - 4, SCREEN_CY, 13040U);
  eng.on_touch_up(13060U);
  LATTER_CHECK(rs.nav.menu_scroll_active);
  LATTER_CHECK(rs.nav.menu_scroll_dir == 1);
  LATTER_CHECK(rs.nav.menu_scroll_target_page == 1);
  LATTER_CHECK(!s_cmds.empty() && s_cmds.back() == UiHostCmd::MENU_SCROLL_TRANSITION);

  // Render mid-scroll horizontal transition cleanly without crashes
  rs.nav.menu_scroll_start_ms = 13060U;
  fix.ctx.now_ms = 13140U;
  dirty_band() = DirtyBandTracker{};
  (void)dirty_band().begin_frame(PageId::PINNED, 0, 0U, true);
  frame_hits().clear();
  render_active_page(fix.surf, fix.ctx);

  // Complete scroll to page 1 and tap CHEVRON_LEFT -> navigates back to page 0!
  rs.nav.menu_scroll_active = false;
  rs.nav.pinned_menu_page = 1;
  fix.ctx.now_ms = 14000U;
  dirty_band() = DirtyBandTracker{};
  (void)dirty_band().begin_frame(PageId::PINNED, 0, 0U, true);
  frame_hits().clear();
  render_active_page(fix.surf, fix.ctx);

  const HitRegion *chev_l = frame_hits().find_by_id(WidgetId::CHEVRON_LEFT);
  LATTER_CHECK(chev_l != nullptr);
  s_cmds.clear();
  eng.on_touch_down(chev_l->cx(), chev_l->cy(), 14100U);
  eng.on_touch_up(14135U);
  LATTER_CHECK(rs.nav.menu_scroll_active);
  LATTER_CHECK(rs.nav.menu_scroll_dir == -1);
  LATTER_CHECK(rs.nav.menu_scroll_target_page == 0);

  // 4. Multi-span aspect modes (Hero 2x2, Wide 2x1, Tall 1x2, 1x1) render cleanly with zero hit overlap
  rs.nav.menu_scroll_active = false;
  rs.nav.pinned_menu_page = 0;
  sc.pinned_items.clear();
  auto push_span = [&](const char *lbl, int8_t c, int8_t r, uint8_t cs, uint8_t rs_span, int8_t st) {
    ShortcutItem it{};
    it.kind = ShortcutKind::ENTITY;
    it.target = "light.span_test";
    it.action = "toggle";
    it.label = lbl;
    it.icon = "mdi:home-automation";
    it.state = st;
    it.page = 0;
    it.col = c;
    it.row = r;
    it.col_span = cs;
    it.row_span = rs_span;
    sc.pinned_items.push_back(it);
  };
  if (rows >= 3) {
    push_span("Hero Scene", 0, 0, 2, 2, 1);
    push_span("Wide Bar", 0, 2, 2, 1, 0);
    if (cols >= 3) {
      push_span("Tall Lamp", 2, 0, 1, 2, 1);
      push_span("Unit", 2, 2, 1, 1, 0);
    }
  } else if (cols >= 4) {
    push_span("Hero Scene", 0, 0, 2, 2, 1);
    push_span("Wide Bar", 2, 0, 2, 1, 0);
    push_span("Unit", 2, 1, 1, 1, 1);
  } else {
    push_span("Wide Bar", 0, 0, 2, 1, 1);
    push_span("Unit", 0, 1, 1, 1, 0);
  }
  dirty_band() = DirtyBandTracker{};
  (void)dirty_band().begin_frame(PageId::PINNED, 0, 0U, true);
  frame_hits().clear();
  render_active_page(fix.surf, fix.ctx);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::PINNED_CARD_BASE + 0) != nullptr);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::PINNED_CARD_BASE + 1) != nullptr);

  // 5. Tools Launcher shares the tile-grid renderer, horizontal edge bounce, and >= 16 slot range
  rs.nav.gui_page = PageId::TOOLS;
  rs.nav.tools_subpage = ToolsSubpage::LAUNCHER;
  rs.nav.tools_menu_page = 0;
  rs.nav.menu_scroll_active = false;
  persisted_prefs() = PersistedDevicePrefs{};
  dirty_band() = DirtyBandTracker{};
  (void)dirty_band().begin_frame(PageId::TOOLS, 0, 0U, true);
  frame_hits().clear();
  render_active_page(fix.surf, fix.ctx);

  const HitRegion *tool0 = frame_hits().find_by_id(WidgetId::TOOLS_CARD_BASE + 0);
  LATTER_CHECK(tool0 != nullptr && tool0->param > 0);
  edge_bounce().clear();
  eng.on_touch_down(SCREEN_CX - swipe_dx / 2, SCREEN_CY, 16000U);
  eng.on_touch_move(SCREEN_CX + swipe_dx / 2 + 4, SCREEN_CY, 16040U);
  eng.on_touch_up(16060U);
  LATTER_CHECK(edge_bounce().is_active(PageId::TOOLS, GestureAxisLock::Horizontal, 16080U));
  LATTER_CHECK(any_page_horiz_bounce_active(PageId::TOOLS, 0, 16080U));

  // Tapping a Tools launcher tile launches its subpage
  eng.on_touch_down(tool0->cx(), tool0->cy(), 17000U);
  eng.on_touch_up(17035U);
  LATTER_CHECK(rs.nav.tools_subpage == tool0->param);

  // 6. System Root Menu (system_menu_mode == 0) shares the adaptive tile-grid renderer & horizontal paging
  rs.nav.gui_page = PageId::SYSTEM;
  rs.nav.system_menu_mode = SystemMenuMode::DASHBOARD;
  rs.nav.system_dashboard_page = 0;
  rs.nav.menu_scroll_active = false;
  dirty_band() = DirtyBandTracker{};
  (void)dirty_band().begin_frame(PageId::SYSTEM, 0, 0U, true);
  frame_hits().clear();
  render_active_page(fix.surf, fix.ctx);

  const HitRegion *sys0 = frame_hits().find_by_id(WidgetId::SYSTEM_CARD_BASE + 0);
  LATTER_CHECK(sys0 != nullptr && sys0->param == 0);
  const int sys_total_pages = system_dashboard_page_count();
  if (cols * rows >= kSystemDashboardItemCount) {
    LATTER_CHECK(sys_total_pages == 1);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::CHEVRON_RIGHT) == nullptr);
  } else {
    LATTER_CHECK(sys_total_pages >= 2);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::CHEVRON_RIGHT) != nullptr);
  }

  // Horizontal swipe RIGHT on page 0 -> boundary edge bounce (Horizontal) & full-frame bounce flag
  edge_bounce().clear();
  s_cmds.clear();
  eng.on_touch_down(SCREEN_CX - swipe_dx / 2, SCREEN_CY, 18000U);
  eng.on_touch_move(SCREEN_CX + swipe_dx / 2 + 4, SCREEN_CY, 18040U);
  eng.on_touch_up(18060U);
  LATTER_CHECK(!rs.nav.menu_scroll_active);
  LATTER_CHECK(edge_bounce().is_active(PageId::SYSTEM, GestureAxisLock::Horizontal, 18080U));
  LATTER_CHECK(any_page_horiz_bounce_active(PageId::SYSTEM, 0, 18080U));

  if (sys_total_pages > 1) {
    // Horizontal swipe LEFT on page 0 -> advances to page 1 via MENU_SCROLL_TRANSITION
    s_cmds.clear();
    eng.on_touch_down(SCREEN_CX + swipe_dx / 2, SCREEN_CY, 19000U);
    eng.on_touch_move(SCREEN_CX - swipe_dx / 2 - 4, SCREEN_CY, 19040U);
    eng.on_touch_up(19060U);
    LATTER_CHECK(rs.nav.menu_scroll_active);
    LATTER_CHECK(rs.nav.menu_scroll_dir == 1);
    LATTER_CHECK(rs.nav.menu_scroll_target_page == 1);

    rs.nav.menu_scroll_active = false;
    rs.nav.system_dashboard_page = 1;
    dirty_band() = DirtyBandTracker{};
    (void)dirty_band().begin_frame(PageId::SYSTEM, 0, 0U, true);
    frame_hits().clear();
    render_active_page(fix.surf, fix.ctx);
    const HitRegion *sys_chev_l = frame_hits().find_by_id(WidgetId::CHEVRON_LEFT);
    LATTER_CHECK(sys_chev_l != nullptr);
    eng.on_touch_down(sys_chev_l->cx(), sys_chev_l->cy(), 19500U);
    eng.on_touch_up(19535U);
    LATTER_CHECK(rs.nav.menu_scroll_active && rs.nav.menu_scroll_dir == -1 && rs.nav.menu_scroll_target_page == 0);

    rs.nav.menu_scroll_active = false;
    rs.nav.system_dashboard_page = 0;
    dirty_band() = DirtyBandTracker{};
    (void)dirty_band().begin_frame(PageId::SYSTEM, 0, 0U, true);
    frame_hits().clear();
    render_active_page(fix.surf, fix.ctx);
    sys0 = frame_hits().find_by_id(WidgetId::SYSTEM_CARD_BASE + 0);
    LATTER_CHECK(sys0 != nullptr && sys0->param == 0);
  }

  // Tapping Display tile launches SystemMenuMode::DISPLAY
  eng.on_touch_down(sys0->cx(), sys0->cy(), 20000U);
  eng.on_touch_up(20035U);
  LATTER_CHECK(rs.nav.system_menu_mode == SystemMenuMode::DISPLAY);
}

}  // namespace

void run_tile_grid_tests() {
  test_tile_grid_traits_and_6geom_bezel_invariants();
  test_tile_grid_solver_reflow_and_compaction();
  test_protocol_v3_14col_titles_and_hibernate_v3();
  test_tile_grid_render_and_horizontal_touch();
}

}  // namespace latter_test
