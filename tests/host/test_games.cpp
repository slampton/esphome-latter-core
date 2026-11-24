#include "test_harness.h"

namespace latter_test {

namespace {

void render_games_frame(HostSurfaceFixture &fix, uint32_t now_ms) {
  using namespace latter_ui;
  using namespace latter_core;
  fix.ctx.now_ms = now_ms;
  fix.fill(0U);
  reset_clip_y_band();
  dirty_band() = DirtyBandTracker{};
  (void)dirty_band().begin_frame(PageId::GAMES, compute_active_subpage_id(), now_ms, true);
  frame_hits().clear();
  render_games_page(fix.surf, fix.ctx);
}

void assert_all_hits_valid_7mm(const HostSurfaceFixture &fix) {
  using namespace latter_ui;
  using namespace latter_core;
  const auto &ht = frame_hits();
  LATTER_CHECK(ht.dropped_count == 0U);
  LATTER_CHECK(ht.count > 0U && ht.count <= FrameHitTable::kMaxRegions);
  const int min_7mm = mm_to_px(7.0f);
  for (uint8_t i = 0; i < ht.count; ++i) {
    const auto &r = ht.regions[i];
    LATTER_CHECK(r.x0 >= 0 && r.y0 >= 0 && r.x1 < SCREEN_W && r.y1 < SCREEN_H);
    LATTER_CHECK(r.x0 <= r.x1 && r.y0 <= r.y1);
    LATTER_CHECK(is_inside_bezel_for(r.cx(), r.cy(), SCREEN_W, SCREEN_H,
                                     LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 1, LATTER_CORNER_R));
    if (r.widget_id != WidgetId::GAMES_PREV_BTN &&
        r.widget_id != WidgetId::GAMES_NEXT_BTN &&
        r.widget_id != WidgetId::GAMES_HERO_CARD) {
      const int pw = r.width() + 2 * static_cast<int>(r.hit_pad);
      const int ph = r.height() + 2 * static_cast<int>(r.hit_pad);
      LATTER_CHECK(pw >= min_7mm && ph >= min_7mm);
    }
  }
  size_t lit = 0;
  for (uint16_t px : fix.pixels) {
    if (px != 0U) ++lit;
  }
  LATTER_CHECK(lit > 0U);
}

void setup_touch_runtime_for_games(uint32_t now_ms) {
  using namespace latter_ui;
  using namespace latter_core;
  auto &eng = touch_engine();
  eng.runtime() = UiTouchRuntime{};
  eng.runtime().panel_is_on = true;
  touch_gesture() = TouchGestureState{};
  touch_router() = TouchRouter{};
  last_user_touch_ms() = now_ms;
  auto &rs = runtime_state();
  rs.nav.display_power_state = 0;
  rs.nav.is_hibernating = false;
  rs.nav.gui_page = PageId::GAMES;
  rs.nav.quick_actions_open = false;
  rs.nav.ota_in_progress = false;
}

void test_game_input_queue() {
  using namespace latter_ui;
  using namespace latter_core;
  auto &q = game_input();
  q.clear();
  LATTER_CHECK(q.count == 0U && q.held_cmd == GameInputCmd::NONE);
  LATTER_CHECK(!q.push(GameInputCmd::NONE));

  LATTER_CHECK(q.push(GameInputCmd::LEFT));
  LATTER_CHECK(q.push(GameInputCmd::COL_TAP, 3));
  q.set_held(GameInputCmd::RIGHT, 1000U);
  LATTER_CHECK(q.held_cmd == GameInputCmd::RIGHT && q.held_since_ms == 1000U);

  GameInputEvent ev{};
  LATTER_CHECK(q.pop(ev) && ev.cmd == GameInputCmd::LEFT && ev.param == 0);
  LATTER_CHECK(q.pop(ev) && ev.cmd == GameInputCmd::COL_TAP && ev.param == 3);
  LATTER_CHECK(!q.pop(ev));

  // Ring buffer drops oldest on overflow cleanly
  for (int i = 0; i < 10; ++i) {
    LATTER_CHECK(q.push(GameInputCmd::COL_TAP, static_cast<int8_t>(i)));
  }
  LATTER_CHECK(q.count == GameInputQueue::kCap);
  LATTER_CHECK(q.pop(ev) && ev.param == 2);

  q.release_held();
  LATTER_CHECK(q.held_cmd == GameInputCmd::NONE);
  q.clear();
  LATTER_CHECK(q.count == 0U);
}

void test_launcher_and_marbles_universal() {
  using namespace latter_ui;
  using namespace latter_core;
  reset_runtime_state();
  game_arena_drop();
  HostSurfaceFixture fix{};
  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  prefs = PersistedDevicePrefs{};

  int enabled[kMaxGamesLauncherItems]{};
  const int cnt = collect_enabled_games(enabled, prefs);
  LATTER_CHECK(cnt == 4);
#if LATTER_HAS_IMU
  LATTER_CHECK(enabled[0] == 1 && enabled[1] == 2 && enabled[2] == 3 && enabled[3] == 4);
#else
  LATTER_CHECK(enabled[0] == 5 && enabled[1] == 6 && enabled[2] == 7 && enabled[3] == 1);
#endif

  // Render every enabled launcher carousel card on this geometry
  rs.nav.gui_page = PageId::GAMES;
  rs.nav.active_game = 0;
  for (int i = 0; i < cnt; ++i) {
    rs.nav.game_carousel_idx = i;
    render_games_frame(fix, 10000U + static_cast<uint32_t>(i) * 50U);
    assert_all_hits_valid_7mm(fix);
    const HitRegion *play_btn = frame_hits().find_by_id(WidgetId::GAMES_PLAY_BTN);
    LATTER_CHECK(play_btn != nullptr && play_btn->param == enabled[i]);
    LATTER_CHECK(frame_hits().find_by_id(WidgetId::GAMES_SOUND_BTN) != nullptr);
  }

  // Marbles (universal across all 5 geometries): entry, tilt step, tap impulse wave, drag-without-spam, cleanup
  setup_touch_runtime_for_games(12000U);
  rs.nav.active_game = 1;
  render_games_frame(fix, 12000U);
  assert_all_hits_valid_7mm(fix);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::BACK_BUTTON) != nullptr);

  auto &eng = touch_engine();
  eng.on_touch_down(SCREEN_CX, SCREEN_CY, 12020U);
  LATTER_CHECK(rs.nav.marble_touch_x >= 0.0f && rs.nav.game_btn_pressed);
  render_games_frame(fix, 12045U);
  LATTER_CHECK(rs.nav.marble_touch_x < 0.0f && !rs.nav.game_btn_pressed);
  // Dragging must tilt via touch_gesture().last_x/last_y without re-arming marble_touch_x shockwaves
  eng.on_touch_move(SCREEN_CX + 30, SCREEN_CY - 20, 12070U);
  LATTER_CHECK(rs.nav.marble_touch_x < 0.0f && !rs.nav.game_btn_pressed);
  LATTER_CHECK(touch_gesture().last_x == SCREEN_CX + 30 && touch_gesture().last_y == SCREEN_CY - 20);
  eng.on_touch_up(12095U);

  for (int step = 2; step <= 8; ++step) {
    render_games_frame(fix, 12000U + static_cast<uint32_t>(step) * 45U);
  }
  assert_all_hits_valid_7mm(fix);
  LATTER_CHECK(!rs.nav.game_btn_pressed);

  rs.nav.active_game = 0;
  game_arena_drop();
}

#if !LATTER_HAS_IMU
void test_blocks_logic_and_e2e() {
  using namespace latter_ui;
  using namespace latter_core;

  // 1. Pure SRS placement, wall kicks, line clears & Guideline scoring
  int8_t board[20][10]{};
  LATTER_CHECK(blocks_can_place(board, 0, 0, 3, 0));
  LATTER_CHECK(!blocks_can_place(board, 0, 0, -1, 0));
  LATTER_CHECK(!blocks_can_place(board, 0, 0, 7, 0));
  LATTER_CHECK(!blocks_can_place(board, 0, 0, 3, 19));

  // T-piece against left wall (rot=1 cells at x=px+1,px+2 -> px=-1 puts it at cols 0,1): CW kick shifts right
  int rot = 1, px = -1, py = 5;
  LATTER_CHECK(blocks_can_place(board, 2, rot, px, py));
  LATTER_CHECK(blocks_try_srs_rotate(board, 2, rot, px, py));
  LATTER_CHECK(rot == 2 && px == 0 && py == 5);  // kicked right from -1 to 0!

  // I-piece vertical at right wall (rot=1, cells at x=px+2 -> px=7 puts it at col 9): CW kick shifts left
  int i_rot = 1, i_px = 7, i_py = 6;
  LATTER_CHECK(blocks_can_place(board, 0, i_rot, i_px, i_py));
  LATTER_CHECK(blocks_try_srs_rotate(board, 0, i_rot, i_px, i_py));
  LATTER_CHECK(i_rot == 2 && i_px <= 6 && blocks_can_place(board, 0, i_rot, i_px, i_py));

  // Quad line clear & row collapse
  std::memset(board, 0, sizeof(board));
  board[15][4] = 3;
  for (int r = 16; r < 20; ++r) {
    for (int c = 0; c < 10; ++c) board[r][c] = 1;
  }
  uint32_t mask = 0U;
  const int cleared = blocks_clear_full_rows(board, mask);
  LATTER_CHECK(cleared == 4);
  LATTER_CHECK(mask == ((1U << 16U) | (1U << 17U) | (1U << 18U) | (1U << 19U)));
  LATTER_CHECK(board[19][4] == 3);
  LATTER_CHECK(board[15][4] == 0);
  LATTER_CHECK(blocks_guideline_clear_score(1, 2) == 200);
  LATTER_CHECK(blocks_guideline_clear_score(2, 2) == 600);
  LATTER_CHECK(blocks_guideline_clear_score(3, 2) == 1000);
  LATTER_CHECK(blocks_guideline_clear_score(4, 3) == 2400);

  // 2. End-to-end Blocks render, hit targets, touch controls, DAS/ARR, hard drop & game over
  reset_runtime_state();
  game_arena_drop();
  auto &prefs = persisted_prefs();
  prefs = PersistedDevicePrefs{};
  HostSurfaceFixture fix{};
  auto &rs = runtime_state();
  setup_touch_runtime_for_games(20000U);
  rs.nav.active_game = 5;

  render_games_frame(fix, 20000U);
  assert_all_hits_valid_7mm(fix);

  const HitRegion *r_board = frame_hits().find_by_id(WidgetId::GAME_BOARD_TAP);
  const HitRegion *r_left = frame_hits().find_by_id(WidgetId::GAME_CTRL_LEFT);
  const HitRegion *r_right = frame_hits().find_by_id(WidgetId::GAME_CTRL_RIGHT);
  const HitRegion *r_rot = frame_hits().find_by_id(WidgetId::GAME_CTRL_ROTATE);
  const HitRegion *r_drop = frame_hits().find_by_id(WidgetId::GAME_CTRL_DROP);
  const HitRegion *r_hold = frame_hits().find_by_id(WidgetId::GAME_CTRL_HOLD);
  LATTER_CHECK(r_board != nullptr && r_left != nullptr && r_right != nullptr &&
               r_rot != nullptr && r_drop != nullptr && r_hold != nullptr);

  auto &eng = touch_engine();
  // Press LEFT -> pushes LEFT + arms held DAS; release clears held
  eng.on_touch_down(r_left->cx(), r_left->cy(), 20050U);
  LATTER_CHECK(game_input().held_cmd == GameInputCmd::LEFT);
  render_games_frame(fix, 20250U);  // DAS elapsed (>= 175 ms)
  eng.on_touch_up(20280U);
  LATTER_CHECK(game_input().held_cmd == GameInputCmd::NONE);

  // Tap HOLD
  eng.on_touch_down(r_hold->cx(), r_hold->cy(), 20350U);
  eng.on_touch_up(20390U);
  render_games_frame(fix, 20400U);

  // Tap board -> rotates piece
  eng.on_touch_down(r_board->cx(), r_board->cy(), 20460U);
  eng.on_touch_up(20500U);
  GameInputEvent ev{};
  LATTER_CHECK(game_input().pop(ev) && ev.cmd == GameInputCmd::ROTATE);
  game_input().push(GameInputCmd::ROTATE);
  render_games_frame(fix, 20520U);

  // Swipe down on board -> HARD_DROP (scores 2 * drop_rows, updates high_score_blocks & schedules NVS save)
  device_prefs_state().nvs_save_pending = false;
  eng.on_touch_down(r_board->cx(), r_board->cy() - 40, 20600U);
  eng.on_touch_move(r_board->cx(), r_board->cy() + 40, 20640U);
  eng.on_touch_up(20660U);
  render_games_frame(fix, 20680U);
  LATTER_CHECK(prefs.high_score_blocks > 0);
  LATTER_CHECK(device_prefs_state().nvs_save_pending);

  // Hard-drop repeatedly until stack reaches top -> game over + PLAY_AGAIN resets
  uint32_t t = 20750U;
  bool reached_over = false;
  for (int drops = 0; drops < 30; ++drops) {
    game_input().push(GameInputCmd::HARD_DROP);
    t += 50U;
    render_games_frame(fix, t);
    t += 220U;  // allow any 180 ms line-clear flash to complete
    render_games_frame(fix, t);
    if (frame_hits().find_by_id(WidgetId::GAME_OVER_CTA) != nullptr) {
      reached_over = true;
      break;
    }
  }
  LATTER_CHECK(reached_over);
  assert_all_hits_valid_7mm(fix);

  const HitRegion *cta = frame_hits().find_by_id(WidgetId::GAME_OVER_CTA);
  LATTER_CHECK(cta != nullptr);
  eng.on_touch_down(cta->cx(), cta->cy(), t + 100U);
  eng.on_touch_up(t + 140U);
  render_games_frame(fix, t + 160U);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::GAME_OVER_CTA) == nullptr);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::GAME_BOARD_TAP) != nullptr);

  rs.nav.active_game = 0;
  game_arena_drop();
}

void test_snake_logic_and_e2e() {
  using namespace latter_ui;
  using namespace latter_core;

  // 1. Pure 2-deep turn queue + 180-degree reversal guard
  int8_t tq[2]{0, 0};
  int tq_len = 0;
  LATTER_CHECK(!snake_enqueue_turn(tq, tq_len, 1, 1));  // same dir ignored
  LATTER_CHECK(!snake_enqueue_turn(tq, tq_len, 1, 3));  // 180° opposite RIGHT->LEFT blocked!
  LATTER_CHECK(snake_enqueue_turn(tq, tq_len, 1, 0));   // RIGHT -> UP accepted
  LATTER_CHECK(!snake_enqueue_turn(tq, tq_len, 1, 2));  // opposite of queued UP -> DOWN blocked!
  LATTER_CHECK(snake_enqueue_turn(tq, tq_len, 1, 3));   // rapid corner UP -> LEFT accepted!
  LATTER_CHECK(tq_len == 2 && tq[0] == 0 && tq[1] == 3);
  LATTER_CHECK(!snake_enqueue_turn(tq, tq_len, 1, 2));  // queue full (cap 2)

  // 2. End-to-end Snake continuous-drag steering + wall crash + game over reset
  reset_runtime_state();
  game_arena_drop();
  HostSurfaceFixture fix{};
  auto &rs = runtime_state();
  setup_touch_runtime_for_games(30000U);
  rs.nav.active_game = 6;

  render_games_frame(fix, 30000U);
  assert_all_hits_valid_7mm(fix);

  auto &eng = touch_engine();
  const int step_px = layout().flick_min() + 6;
  // Continuous drag UP then LEFT without lifting finger!
  eng.on_touch_down(SCREEN_CX, SCREEN_CY, 30040U);
  eng.on_touch_move(SCREEN_CX, SCREEN_CY - step_px, 30070U);
  eng.on_touch_move(SCREEN_CX - step_px, SCREEN_CY - step_px, 30100U);
  eng.on_touch_up(30130U);

  GameInputEvent e1{}, e2{};
  LATTER_CHECK(game_input().pop(e1) && e1.cmd == GameInputCmd::UP);
  LATTER_CHECK(game_input().pop(e2) && e2.cmd == GameInputCmd::LEFT);
  // Re-push UP so snake steers into the top wall cleanly
  game_input().push(GameInputCmd::UP);

  uint32_t t = 30150U;
  bool crashed = false;
  for (int ticks = 0; ticks < 25; ++ticks) {
    t += 230U;
    render_games_frame(fix, t);
    if (frame_hits().find_by_id(WidgetId::GAME_OVER_CTA) != nullptr) {
      crashed = true;
      break;
    }
  }
  LATTER_CHECK(crashed);
  assert_all_hits_valid_7mm(fix);

  const HitRegion *cta = frame_hits().find_by_id(WidgetId::GAME_OVER_CTA);
  LATTER_CHECK(cta != nullptr);
  eng.on_touch_down(cta->cx(), cta->cy(), t + 100U);
  eng.on_touch_up(t + 140U);
  render_games_frame(fix, t + 160U);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::GAME_OVER_CTA) == nullptr);

  rs.nav.active_game = 0;
  game_arena_drop();
}

void test_drop_four_logic_and_e2e() {
  using namespace latter_ui;
  using namespace latter_core;

  // 1. Pure 4-in-a-row win detection (H, V, Diag down-right, Diag up-right)
  uint8_t b[6][7]{};
  int cells[4][2]{};
  LATTER_CHECK(drop4_find_winner(b, cells) == 0);

  // Horizontal
  b[5][1] = b[5][2] = b[5][3] = b[5][4] = 1U;
  LATTER_CHECK(drop4_find_winner(b, cells) == 1);
  LATTER_CHECK(cells[0][0] == 5 && cells[0][1] == 1 && cells[3][0] == 5 && cells[3][1] == 4);

  // Vertical
  std::memset(b, 0, sizeof(b));
  b[2][3] = b[3][3] = b[4][3] = b[5][3] = 2U;
  LATTER_CHECK(drop4_find_winner(b, cells) == 2);
  LATTER_CHECK(cells[0][0] == 2 && cells[3][0] == 5 && cells[0][1] == 3);

  // Diagonal down-right
  std::memset(b, 0, sizeof(b));
  b[1][1] = b[2][2] = b[3][3] = b[4][4] = 1U;
  LATTER_CHECK(drop4_find_winner(b, cells) == 1);

  // Diagonal up-right
  std::memset(b, 0, sizeof(b));
  b[5][0] = b[4][1] = b[3][2] = b[2][3] = 2U;
  LATTER_CHECK(drop4_find_winner(b, cells) == 2);

  // 2. Alpha-beta AI takes immediate winning move & blocks immediate opponent 3-in-a-row
  uint32_t rng = 0x12345678U;
  // Immediate AI win in col 4
  std::memset(b, 0, sizeof(b));
  b[5][1] = b[5][2] = b[5][3] = 2U;
  b[5][0] = 1U;
  LATTER_CHECK(drop4_choose_ai_col(b, 0, rng) == 4);

  // Immediate human threat in col 2 (vertical 3-in-a-row): AI must block col 2 even at streak 0
  std::memset(b, 0, sizeof(b));
  b[5][2] = b[4][2] = b[3][2] = 1U;
  LATTER_CHECK(drop4_choose_ai_col(b, 0, rng) == 2);
  LATTER_CHECK(drop4_choose_ai_col(b, 1, rng) == 2);
  LATTER_CHECK(drop4_choose_ai_col(b, 2, rng) == 2);

  // Empty board: streak 2 depth 5 prefers center column 3
  std::memset(b, 0, sizeof(b));
  LATTER_CHECK(drop4_choose_ai_col(b, 2, rng) == 3);

  // 3. End-to-end Drop Four render, hit targets, 1P AI response & 2P non-inflation of high score
  reset_runtime_state();
  game_arena_drop();
  auto &prefs = persisted_prefs();
  prefs = PersistedDevicePrefs{};
  HostSurfaceFixture fix{};
  auto &rs = runtime_state();
  setup_touch_runtime_for_games(40000U);
  rs.nav.active_game = 7;

  render_games_frame(fix, 40000U);
  assert_all_hits_valid_7mm(fix);

  LATTER_CHECK(frame_hits().find_by_id(WidgetId::GAME_MODE_AI) != nullptr);
  LATTER_CHECK(frame_hits().find_by_id(WidgetId::GAME_MODE_2P) != nullptr);
  for (int c = 0; c < 7; ++c) {
    LATTER_CHECK(frame_hits().find_by_id(static_cast<uint16_t>(WidgetId::GAME_COL_BASE + c)) != nullptr);
  }

  auto &eng = touch_engine();
  uint32_t t = 40040U;
  const HitRegion *col3 = frame_hits().find_by_id(static_cast<uint16_t>(WidgetId::GAME_COL_BASE + 3));
  LATTER_CHECK(col3 != nullptr);
  eng.on_touch_down(col3->cx(), col3->cy(), t);
  eng.on_touch_up(t + 30U);
  // Step physics until human disc lands and AI completes its 260 ms thought + drop
  for (int i = 0; i < 45; ++i) {
    t += 45U;
    render_games_frame(fix, t);
  }
  assert_all_hits_valid_7mm(fix);

  // Switch to 2P mode and play a quick P1 vertical win in col 0 vs P2 col 1
  const HitRegion *mode_2p = frame_hits().find_by_id(WidgetId::GAME_MODE_2P);
  LATTER_CHECK(mode_2p != nullptr);
  eng.on_touch_down(mode_2p->cx(), mode_2p->cy(), t + 50U);
  eng.on_touch_up(t + 80U);
  t += 100U;
  render_games_frame(fix, t);

  auto drop_and_settle = [&](int col) {
    const HitRegion *cr = frame_hits().find_by_id(static_cast<uint16_t>(WidgetId::GAME_COL_BASE + col));
    LATTER_CHECK(cr != nullptr);
    t += 60U;
    eng.on_touch_down(cr->cx(), cr->cy(), t);
    eng.on_touch_up(t + 20U);
    for (int f = 0; f < 30; ++f) {
      t += 45U;
      render_games_frame(fix, t);
    }
  };
  drop_and_settle(0);  // P1
  drop_and_settle(1);  // P2
  drop_and_settle(0);  // P1
  drop_and_settle(1);  // P2
  drop_and_settle(0);  // P1
  drop_and_settle(1);  // P2
  drop_and_settle(0);  // P1 wins 4 vertically!

  LATTER_CHECK(frame_hits().find_by_id(WidgetId::GAME_OVER_CTA) != nullptr);
  // 2P win MUST NOT inflate AI high_score_drop_four!
  LATTER_CHECK(prefs.high_score_drop_four == 0);
  assert_all_hits_valid_7mm(fix);

  rs.nav.active_game = 0;
  game_arena_drop();
}
#endif  // !LATTER_HAS_IMU

}  // namespace

void run_games_tests() {
  test_game_input_queue();
  test_launcher_and_marbles_universal();
#if !LATTER_HAS_IMU
  test_blocks_logic_and_e2e();
  test_snake_logic_and_e2e();
  test_drop_four_logic_and_e2e();
#endif
}

}  // namespace latter_test
