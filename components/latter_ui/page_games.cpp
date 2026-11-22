#include "games_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace latter_ui {

namespace {

GameArena *&game_arena_slot() noexcept {
  static GameArena *s_ptr = nullptr;
  return s_ptr;
}

void draw_games_speaker_icon(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow, void *user_ctx) {
  const bool sound_on = (user_ctx != nullptr);
  ui::draw_icon_speaker(it, cx, cy, r, col, sound_on, shadow, !sound_on);
}

void render_games_launcher(Rgb565Surface &it, const UiFrameContext &ctx,
                           int &game_carousel_idx, bool menu_scroll_active,
                           int menu_scroll_dir, int menu_scroll_target_page) {
  const int cx = ctx.cx;
  const auto &prefs = device_prefs();
  const bool game_sound_on = ctx.game_sound_enabled;
  const bool compact_w = (SCREEN_W < 380);

  int enabled_ids[kMaxGamesLauncherItems]{};
  const int game_count = collect_enabled_games(enabled_ids, prefs);
  if (game_count <= 0) {
    game_carousel_idx = 0;
    const int hdr_y = compact_w ? 64 : scale_y(84);
    ui::shadow_print(it, cx, hdr_y, ctx.font_games_title, ctx.col_white, UiTextAlign::CENTER, "GAMES");
    render_empty_state(it, cx, ctx.cy, "No games enabled", ctx.font_small, ctx.col_slate);
    return;
  }

  if (game_carousel_idx < 0 || game_carousel_idx >= game_count) {
    game_carousel_idx = std::clamp(game_carousel_idx, 0, game_count - 1);
  }

  auto gs = [](int v) constexpr -> int { return scale_min(v); };

  auto draw_game_icon = [&](int g_idx, int icon_x, int icon_y) {
    if (g_idx == 0) {
      fast_filled_circle(it, icon_x, icon_y, gs(52), UiColor(18, 24, 28));
      fast_circle(it, icon_x, icon_y, gs(52), ctx.col_slate);
      fast_circle(it, icon_x, icon_y, gs(51), ctx.col_slate);
      fast_circle(it, icon_x, icon_y, gs(46), UiColor(30, 40, 48));

      fast_filled_circle(it, icon_x, icon_y, gs(15), ctx.col_black);
      fast_circle(it, icon_x, icon_y, gs(15), UiColor(45, 55, 65));
      fast_circle(it, icon_x, icon_y, gs(14), UiColor(45, 55, 65));

      for (int a = 0; a < 4; a++) {
        const float sa = static_cast<float>(a) * 1.570796f;
        fast_line(it, icon_x + static_cast<int>(fast_cos(sa) * scale_min_f(18.0f)),
                  icon_y + static_cast<int>(fast_sin(sa) * scale_min_f(18.0f)),
                  icon_x + static_cast<int>(fast_cos(sa + 0.8f) * scale_min_f(40.0f)),
                  icon_y + static_cast<int>(fast_sin(sa + 0.8f) * scale_min_f(40.0f)),
                  UiColor(35, 45, 55));
      }
      render_glass_marble(it, icon_x - gs(24), icon_y - gs(18), gs(13), ctx.col_red, UiColor(255, 138, 128));
      render_glass_marble(it, icon_x + gs(22), icon_y - gs(22), gs(12), ctx.col_cyan, UiColor(180, 255, 255));
      render_glass_marble(it, icon_x - gs(20), icon_y + gs(22), gs(11), ctx.col_green, UiColor(185, 246, 202));
      render_glass_marble(it, icon_x + gs(24), icon_y + gs(18), gs(13), ctx.col_gold, UiColor(255, 245, 157));
      render_glass_marble(it, icon_x + gs(2), icon_y - gs(31), gs(10), ctx.col_magenta, UiColor(248, 187, 208));

    } else if (g_idx == 1) {
      render_archery_target(it, icon_x, icon_y, gs(46), ctx.col_white, ctx.col_black,
                            ctx.col_cyan, ctx.col_red, ctx.col_gold, ctx.col_slate, ctx.col_dark_gray);
      const int tip_x = icon_x + gs(2);
      const int tip_y = icon_y + gs(2);
      const int tail_x = icon_x + gs(56);
      const int tail_y = icon_y - gs(36);
      fast_line(it, tip_x, tip_y + gs(5), tail_x - gs(8), tail_y + gs(10), UiColor(15, 18, 24));
      fast_line(it, tip_x, tip_y + gs(6), tail_x - gs(8), tail_y + gs(11), UiColor(15, 18, 24));
      fast_circle(it, tip_x, tip_y, gs(5), UiColor(180, 110, 0));
      fast_filled_triangle(it, tip_x - gs(3), tip_y + gs(2), tip_x + gs(9), tip_y - gs(9),
                           tip_x + gs(12), tip_y - gs(2), ctx.col_white);
      fast_line(it, tip_x, tip_y, tail_x, tail_y, UiColor(238, 206, 148));
      fast_line(it, tip_x, tip_y - 1, tail_x, tail_y - 1, UiColor(250, 228, 178));
      fast_line(it, tip_x, tip_y + 1, tail_x, tail_y + 1, UiColor(182, 138, 76));
      for (int f = 0; f < 9; f++) {
        const int fx = tail_x - gs(3 + f);
        const int fy = tail_y + gs(2 + (f * 2) / 3);
        fast_line(it, fx, fy - 1, fx + gs(3), fy - gs(11), UiColor(255, 52, 52));
        fast_line(it, fx, fy + 1, fx + gs(10), fy + gs(4), UiColor(220, 32, 32));
      }
      fast_filled_circle(it, tail_x + 1, tail_y - 1, std::max(2, gs(2)), ctx.col_white);

    } else if (g_idx == 2) {
      const int b_w = gs(26), b_h = gs(11), gap_x = gs(4), gap_y = gs(5);
      const int start_bx = icon_x - static_cast<int>(2.0f * static_cast<float>(b_w) + 1.5f * static_cast<float>(gap_x)) + b_w / 2;
      const int start_by = icon_y - gs(36);
      const UiColor row_c[3] = {ctx.col_gold, ctx.col_cyan, ctx.col_orange};
      const UiColor row_hi[3] = {UiColor(255, 245, 165), UiColor(185, 255, 255), UiColor(255, 195, 100)};
      for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
          const int bx = start_bx + c * (b_w + gap_x);
          const int by = start_by + r * (b_h + gap_y);
          if (r == 2 && c == 2) {
            fill_rect_row_major(it, bx - gs(10), by - gs(5), gs(5), gs(4), row_c[r]);
            fill_rect_row_major(it, bx + gs(4), by - gs(7), gs(5), gs(4), row_hi[r]);
            fill_rect_row_major(it, bx - gs(6), by + gs(3), gs(4), gs(4), row_c[r]);
            fill_rect_row_major(it, bx + gs(5), by + gs(2), gs(5), gs(4), row_hi[r]);
            fast_filled_circle(it, bx, by, std::max(2, gs(2)), ctx.col_white);
            continue;
          }
          const int hp = (r == 0 && (c == 1 || c == 2)) ? 2 : 1;
          const UiColor bc = (hp == 2) ? UiColor(145, 162, 182) : row_c[r];
          const UiColor hc = (hp == 2) ? UiColor(230, 240, 252) : row_hi[r];
          render_brick_tile(it, bx, by, b_w, b_h, bc, hc, hp, hp, ctx.col_white);
        }
      }
      const int pad_x = icon_x - gs(6), pad_y = icon_y + gs(42), pad_w = gs(44), pad_h = gs(8);
      render_brick_paddle(it, pad_x, pad_y, pad_w, pad_h, ctx.theme_primary, ctx.col_white);

      const int bx = icon_x + gs(10), by = icon_y + gs(7);
      fast_filled_circle(it, pad_x + gs(3), pad_y - gs(7), std::max(2, gs(2)), UiColor(50, 40, 14));
      fast_filled_circle(it, pad_x + gs(6), pad_y - gs(14), std::max(2, gs(2)), UiColor(85, 68, 20));
      fast_filled_circle(it, pad_x + gs(9), pad_y - gs(21), std::max(3, gs(3)), UiColor(130, 102, 28));
      fast_filled_circle(it, bx - gs(3), by + gs(7), std::max(3, gs(4)), UiColor(185, 145, 35));
      fast_filled_circle(it, bx, by, gs(5), ctx.col_gold);
      fast_circle(it, bx, by, gs(5), ctx.col_white);
      fast_filled_circle(it, bx - gs(2), by - gs(2), std::max(1, gs(1)), ctx.col_white);

    } else if (g_idx == 3) {
      render_chomp_snack(it, icon_x + gs(30), icon_y - gs(22), 0,
                         ctx.col_white, ctx.col_red, ctx.col_gold, ctx.col_cyan,
                         ctx.col_green, ctx.col_magenta, ctx.col_bg);
      render_chomp_snack(it, icon_x + gs(38), icon_y + gs(18), 2,
                         ctx.col_white, ctx.col_red, ctx.col_gold, ctx.col_cyan,
                         ctx.col_green, ctx.col_magenta, ctx.col_bg);
      render_chomp_snack(it, icon_x - gs(34), icon_y - gs(26), 1,
                         ctx.col_white, ctx.col_red, ctx.col_gold, ctx.col_cyan,
                         ctx.col_green, ctx.col_magenta, ctx.col_bg);
      render_chomp_character(it, icon_x - gs(8), icon_y + gs(6), gs(32), 0.85f, -0.25f,
                             true, false, 0U, ctx.col_white, ctx.col_black, ctx.col_dark_gray);

    } else if (g_idx == 4) {
      // Decorative 2048 preview (scaled with scale_min, non-glass flat tiles per D-048)
      const int bsz = gs(96), tsz = gs(40), half_b = bsz / 2;
      const int off = gs(42), gap2 = gs(2), mid_t = gs(22);
      const float br = static_cast<float>(gs(12));
      const float tr = static_cast<float>(gs(7));
      fill_round_rect_aa(it, icon_x - half_b, icon_y - half_b, bsz, bsz, br, kChromeTrack);
      stroke_round_rect_aa(it, icon_x - half_b, icon_y - half_b, bsz, bsz, br, kChromeCardBorder, kChromeCardBorder, 1);

      fill_round_rect_aa(it, icon_x - off, icon_y - off, tsz, tsz, tr, kChromeTrackBorder);
      ui::shadow_print(it, icon_x - mid_t, icon_y - mid_t, ctx.font_small, ctx.col_white, UiTextAlign::CENTER, "2");
      fill_round_rect_aa(it, icon_x + gap2, icon_y - off, tsz, tsz, tr, scale_color(ctx.col_orange, 0.85f));
      ui::shadow_print(it, icon_x + mid_t, icon_y - mid_t, ctx.font_small, ctx.col_white, UiTextAlign::CENTER, "16");
      fill_round_rect_aa(it, icon_x - off, icon_y + gap2, tsz, tsz, tr, ctx.col_red);
      ui::shadow_print(it, icon_x - mid_t, icon_y + mid_t, ctx.font_small, ctx.col_white, UiTextAlign::CENTER, "64");
      fill_round_rect_aa(it, icon_x + gap2, icon_y + gap2, tsz, tsz, tr, scale_color(ctx.col_gold, 0.62f));
      ui::shadow_print(it, icon_x + mid_t, icon_y + mid_t, ctx.font_small, ctx.col_white, UiTextAlign::CENTER, "2K");

    } else {
      // Decorative Memory Match preview (scaled with scale_min, non-glass flat cards per D-048)
      const int csz = gs(40), off = gs(44), gap4 = gs(4);
      const float cr = static_cast<float>(gs(8));
      auto draw_mini_card = [&](int x0, int y0, UiColor brd, int dot_r, UiColor dot_c) {
        fill_round_rect_aa(it, x0, y0, csz, csz, cr, kChromeTrack);
        stroke_round_rect_aa(it, x0, y0, csz, csz, cr, brd, brd, 1);
        fast_filled_circle(it, x0 + csz / 2, y0 + csz / 2, dot_r, dot_c);
      };
      draw_mini_card(icon_x - off, icon_y - off, ctx.col_gold, gs(9), ctx.col_gold);
      draw_mini_card(icon_x + gap4, icon_y - off, kChromeCardBorder, gs(5), ctx.theme_primary);
      draw_mini_card(icon_x - off, icon_y + gap4, kChromeCardBorder, gs(5), ctx.theme_primary);
      draw_mini_card(icon_x + gap4, icon_y + gap4, ctx.col_gold, gs(9), ctx.col_gold);
    }
  };

  auto draw_game_card = [&](int g_idx, int icon_x, int icon_y, int text_center_x, int title_y, int score_y) {
    const char *title = "MARBLES";
    int score = prefs.high_score_marbles;
    if (g_idx == 1) {
      title = "ARCHERY";
      score = prefs.high_score_archery;
    } else if (g_idx == 2) {
      title = "BRICKS";
      score = prefs.high_score_bricks;
    } else if (g_idx == 3) {
      title = "CHOMP";
      score = prefs.high_score_chomp;
    } else if (g_idx == 4) {
      title = "2048";
      score = prefs.high_score_2048;
    } else if (g_idx >= 5) {
      title = "MEMORY MATCH";
      score = prefs.high_score_memory;
    }

    draw_game_icon(g_idx, icon_x, icon_y);
    ui::shadow_print(it, text_center_x, title_y, ctx.font_btn_bold, ctx.col_white, UiTextAlign::CENTER, title);

    const char *lbl_str = "HIGH SCORE:";
    char val_str[20];
    if (score <= 0) {
      std::snprintf(val_str, sizeof(val_str), "%s", kUnknownGlyph);
    } else if (score >= 1000000) {
      std::snprintf(val_str, sizeof(val_str), "%d,%03d,%03d", score / 1000000, (score / 1000) % 1000, score % 1000);
    } else if (score >= 1000) {
      std::snprintf(val_str, sizeof(val_str), "%d,%03d", score / 1000, score % 1000);
    } else {
      std::snprintf(val_str, sizeof(val_str), "%d", score);
    }

    int w_lbl = 0, w_val = 0, x_off = 0, base = 0, th = 0;
    ctx.font_small->measure(lbl_str, &w_lbl, &x_off, &base, &th);
    ctx.font_small->measure(val_str, &w_val, &x_off, &base, &th);
    const int gap_px = scale_x(8);
    const int start_x = text_center_x - (w_lbl + gap_px + w_val) / 2;

    ui::shadow_print(it, start_x, score_y, ctx.font_small, ctx.col_gold, UiTextAlign::CENTER_LEFT, lbl_str);
    ui::shadow_print(it, start_x + w_lbl + gap_px, score_y, ctx.font_small, ctx.col_white, UiTextAlign::CENTER_LEFT, val_str);
  };

  const int icon_cx = compact_w ? cx : (cx - scale_x(118));
  const int card_cy = compact_w ? 158 : scale_y(194);
  const int text_cx = compact_w ? cx : (cx + scale_x(57));
  const int base_title_y = compact_w ? 242 : scale_y(176);
  const int base_score_y = compact_w ? 274 : scale_y(216);
  const int pips_y = compact_w ? 314 : scale_y(282);
  const int btn_row_y = layout().app_dock_y;
  const int cur_game_id = enabled_ids[game_carousel_idx];
  const int cur_g_idx = cur_game_id - 1;

  if (menu_scroll_active && game_count > 1) {
    const int full_span = scale_x(252);
    const int offset = menu_scroll_shift(full_span, ctx.now_ms);
    const int out_dx = -menu_scroll_dir * offset;
    const int in_dx = menu_scroll_dir * (full_span - offset);
    const int tgt_slot = std::clamp(menu_scroll_target_page, 0, game_count - 1);
    const int tgt_g_idx = enabled_ids[tgt_slot] - 1;

    draw_game_card(cur_g_idx, icon_cx + out_dx, card_cy, text_cx + out_dx, base_title_y, base_score_y);
    draw_game_card(tgt_g_idx, icon_cx + in_dx, card_cy, text_cx + in_dx, base_title_y, base_score_y);
  } else {
    const int bounce_x = edge_bounce().offset_px(
        PageId::GAMES, GestureAxisLock::Horizontal, ctx.now_ms, LATTER_PANEL_PPI);
    draw_game_card(cur_g_idx, icon_cx + bounce_x, card_cy, text_cx + bounce_x, base_title_y, base_score_y);

    // Co-register carousel flank tap zones (when > 1 game enabled) + center hero card tap zone
    const int flank_w = scale_x(72);
    const int hero_top = scale_y(112);
    const int hero_h = std::max(80, pips_y - hero_top - scale_y(8));
    if (game_count > 1) {
      frame_hits().add_rect(0, hero_top, flank_w, hero_h, WidgetId::GAMES_PREV_BTN, kHitTap, -1);
      frame_hits().add_rect(SCREEN_W - flank_w, hero_top, flank_w, hero_h, WidgetId::GAMES_NEXT_BTN, kHitTap, 1);
      frame_hits().add_rect(flank_w, hero_top, SCREEN_W - 2 * flank_w, hero_h,
                            WidgetId::GAMES_HERO_CARD, kHitTap, static_cast<int16_t>(cur_game_id));
    } else {
      frame_hits().add_rect(scale_x(24), hero_top, SCREEN_W - 2 * scale_x(24), hero_h,
                            WidgetId::GAMES_HERO_CARD, kHitTap, static_cast<int16_t>(cur_game_id));
    }
  }

  // Documented arcade carve-out (D-070): Orbitron "GAMES" display header
  const int hdr_y = compact_w ? 64 : scale_y(84);
  ui::shadow_print(it, cx, hdr_y, ctx.font_games_title, ctx.col_white, UiTextAlign::CENTER, "GAMES");

  const int active_pip = (menu_scroll_active && game_count > 1)
                             ? std::clamp(menu_scroll_target_page, 0, game_count - 1)
                             : game_carousel_idx;
  render_dock_page_pips(it, cx, pips_y, game_count, active_pip, ctx.col_white, kChromePipInactive);

  const int play_h = std::max(44, scale_s(44));
  const int play_w = std::min(scale_x(154), safe_span(btn_row_y, 24) - scale_x(72));
  ui::action_pill_centered(it, cx + scale_x(36), play_w, btn_row_y, play_h,
                           ctx.theme_primary, kChromeGlassTint, "PLAY", ctx.col_white, ctx.font_btn_bold,
                           WidgetId::GAMES_PLAY_BTN, kHitTap | kHitFocusSlot, static_cast<int16_t>(cur_game_id));

  const int spk_x = cx - scale_x(91);
  const int spk_r = scale_s(22);
  ui::icon_circle_button(it, spk_x, btn_row_y, spk_r, kChromeGlassTint,
                         game_sound_on ? ctx.theme_secondary : kChromeCardBorder,
                         game_sound_on ? ctx.col_white : ctx.col_slate,
                         draw_games_speaker_icon,
                         game_sound_on ? reinterpret_cast<void *>(1) : nullptr,
                         WidgetId::GAMES_SOUND_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle, 1, 6);
}

}  // namespace

GameArena *acquire_game_arena() noexcept {
  auto *&ptr = game_arena_slot();
  if (ptr == nullptr) {
    ptr = static_cast<GameArena *>(ui_psram_calloc(1, sizeof(GameArena)));
  }
  return ptr;
}

void game_arena_drop() noexcept {
  auto *&ptr = game_arena_slot();
  if (ptr != nullptr) {
    ui_psram_free(ptr);
    ptr = nullptr;
  }
}

bool &game_imu_retare_flag() noexcept {
  static bool flag = false;
  return flag;
}

void compute_3d_game_tilt(float raw_ax, float raw_ay, float raw_az,
                          float euler_roll_deg, float euler_pitch_deg,
                          bool game_just_entered,
                          float &out_roll_delta, float &out_pitch_delta) {
  constexpr float DEG_TO_RAD = 3.14159265f / 180.0f;
  constexpr float RAD_TO_DEG = 180.0f / 3.14159265f;

  const float roll = std::isnan(euler_roll_deg) ? 0.0f : euler_roll_deg;
  const float raw_pitch = std::isnan(euler_pitch_deg) ? -45.0f : euler_pitch_deg;

  const float r_rad = roll * DEG_TO_RAD;
  const float p_rad = raw_pitch * DEG_TO_RAD;
  const float gr = std::clamp(std::sin(r_rad) * std::cos(p_rad), -1.0f, 1.0f);
  const float clean_roll = std::asin(gr) * RAD_TO_DEG;
  const float clean_pitch = raw_pitch;

  const float gp = std::sin(p_rad);
  float gz = std::cos(p_rad);
  if (!std::isnan(raw_ax) && !std::isnan(raw_ay) && !std::isnan(raw_az)) {
    const float g_norm = std::sqrt(raw_ax * raw_ax + raw_ay * raw_ay + raw_az * raw_az);
    if (g_norm > 0.05f) {
      gz = raw_az / g_norm;
    }
  }

  static float zero_roll = 0.0f;
  static float zero_pitch = -45.0f;
  static float zero_gp = -0.7071f;
  static float zero_gz = 0.7071f;
  static bool vertical_pitch_mode = false;
  static bool tare_initialized = false;

  if (!tare_initialized || game_just_entered || game_imu_retare_flag()) {
    tare_initialized = true;
    game_imu_retare_flag() = false;
    zero_roll = clean_roll;
    zero_pitch = clean_pitch;
    const float pz_norm = std::sqrt(gp * gp + gz * gz);
    if (pz_norm > 0.05f) {
      zero_gp = gp / pz_norm;
      zero_gz = gz / pz_norm;
    } else {
      zero_gp = gp;
      zero_gz = gz;
    }
    vertical_pitch_mode = (std::fabs(gz) < 0.38f);
  }

  out_roll_delta = clean_roll - zero_roll;
  if (vertical_pitch_mode) {
    const float cross_pz = gp * zero_gz - gz * zero_gp;
    const float dot_pz = gp * zero_gp + gz * zero_gz;
    out_pitch_delta = std::atan2(cross_pz, dot_pz) * RAD_TO_DEG;
  } else {
    out_pitch_delta = clean_pitch - zero_pitch;
  }

  if (touch_gesture().active && touch_gesture().last_y < (layout().back_y - scale_y(50)) &&
      (LATTER_HAS_IMU == 0 || touch_gesture().max_travel >= scale_s(10))) {
    const float span = static_cast<float>(std::max(110, scale_s(160)));
    out_roll_delta = std::clamp((static_cast<float>(touch_gesture().last_x - SCREEN_CX) / span) * 28.0f, -35.0f, 35.0f);
    out_pitch_delta = std::clamp((static_cast<float>(SCREEN_CY - touch_gesture().last_y) / span) * 28.0f, -35.0f, 35.0f);
  }
}

int &pending_game_sfx() {
  static int sfx = 0;
  return sfx;
}

int &game_2048_pending_dir() {
  static int dir = 0;
  return dir;
}

void render_games_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  if (!frame_base_state().painted) {
    frame_base(it, ctx.col_bg);
  }

  auto &rs = runtime_state();
  auto &prefs = persisted_prefs();
  static int s_prev_active_game = 0;

  if (rs.nav.active_game <= 0) {
    if (s_prev_active_game != 0) {
      s_prev_active_game = 0;
      flush_persisted_prefs_if_dirty();
    }
    render_games_launcher(it, ctx, rs.nav.game_carousel_idx,
                          rs.nav.menu_scroll_active, rs.nav.menu_scroll_dir,
                          rs.nav.menu_scroll_target_page);
    return;
  }

  GameArena *arena = acquire_game_arena();
  if (arena == nullptr) {
    render_empty_state(it, ctx.cx, ctx.cy, "Memory busy", ctx.font_small, ctx.col_slate);
    render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "Exit");
    return;
  }

  const bool game_just_entered =
      (rs.nav.active_game != s_prev_active_game) ||
      (arena->initialized_game_id != rs.nav.active_game);
  s_prev_active_game = rs.nav.active_game;
  arena->initialized_game_id = rs.nav.active_game;

  float roll_delta = 0.0f;
  float pitch_delta = 0.0f;
  compute_3d_game_tilt(rs.hw_accel_x, rs.hw_accel_y, rs.hw_accel_z,
                       rs.hw_imu_roll, rs.hw_imu_pitch,
                       game_just_entered, roll_delta, pitch_delta);

  switch (rs.nav.active_game) {
    case 1:
      render_game_marbles(it, ctx, *arena, game_just_entered, roll_delta, pitch_delta,
                          rs.nav.marble_touch_x, rs.nav.marble_touch_y,
                          rs.nav.game_btn_pressed, prefs.high_score_marbles);
      break;
    case 2:
      render_game_archery(it, ctx, *arena, game_just_entered, roll_delta, pitch_delta,
                          rs.nav.marble_touch_x, rs.nav.game_btn_pressed,
                          prefs.high_score_archery);
      break;
    case 3:
      render_game_bricks(it, ctx, *arena, game_just_entered, roll_delta,
                         rs.nav.marble_touch_x, rs.nav.game_btn_pressed,
                         prefs.high_score_bricks);
      break;
    case 4:
      render_game_chomp(it, ctx, *arena, game_just_entered, roll_delta, pitch_delta,
                        rs.nav.marble_touch_x, rs.nav.game_btn_pressed,
                        prefs.high_score_chomp);
      break;
    case 5:
      render_game_2048(it, ctx, *arena, game_just_entered,
                       rs.nav.marble_touch_x, rs.nav.marble_touch_y,
                       rs.nav.game_btn_pressed, prefs.high_score_2048);
      break;
    case 6:
    default:
      render_game_memory(it, ctx, *arena, game_just_entered,
                         rs.nav.marble_touch_x, rs.nav.marble_touch_y,
                         rs.nav.game_btn_pressed, prefs.high_score_memory);
      break;
  }
}

}  // namespace latter_ui
