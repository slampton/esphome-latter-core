#include "esphome/core/defines.h"
#include "ui_pages.h"
#include "hal_video_stream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace latter_ui {

namespace {

// Draws a solid black backdrop for rows outside `inner` (strictly honoring active clip band).
void fill_black_around_rect(esphome::display::Display &it, const CameraRect &inner, esphome::Color col_black) {
  const int cy0 = std::max(0, clip_y0());
  const int cy1 = std::min(SCREEN_H - 1, clip_y1());
  if (cy0 > cy1) return;

  if (inner.w <= 0 || inner.h <= 0) {
    fill_rect_row_major(it, 0, cy0, SCREEN_W, cy1 - cy0 + 1, col_black);
    return;
  }
  const int iy0 = std::clamp(inner.y0, 0, SCREEN_H);
  const int iy1 = std::clamp(inner.y0 + inner.h - 1, -1, SCREEN_H - 1);
  if (cy0 < iy0) {
    const int top_end = std::min(cy1, iy0 - 1);
    if (top_end >= cy0) {
      fill_rect_row_major(it, 0, cy0, SCREEN_W, top_end - cy0 + 1, col_black);
    }
  }
  if (cy1 > iy1) {
    const int bot_start = std::max(cy0, iy1 + 1);
    if (cy1 >= bot_start) {
      fill_rect_row_major(it, 0, bot_start, SCREEN_W, cy1 - bot_start + 1, col_black);
    }
  }
  const int mid_y0 = std::max(cy0, iy0);
  const int mid_y1 = std::min(cy1, iy1);
  if (mid_y1 >= mid_y0) {
    const int mh = mid_y1 - mid_y0 + 1;
    if (inner.x0 > 0) {
      fill_rect_row_major(it, 0, mid_y0, std::min(inner.x0, SCREEN_W), mh, col_black);
    }
    const int rx0 = inner.x0 + inner.w;
    if (rx0 < SCREEN_W) {
      fill_rect_row_major(it, std::max(0, rx0), mid_y0, SCREEN_W - std::max(0, rx0), mh, col_black);
    }
  }
}

void draw_live_dot(esphome::display::Display &it, const UiFrameContext &ctx,
                   bool streaming, bool paused) {
  const int r = std::max(4, scale_s(5));
  int dx = SCREEN_W - scale_x(26);
  int dy = scale_y(26);
#if LATTER_ROUND_DISPLAY
  dx = ctx.cx + scale_x(72);
  dy = layout().hdr_y;
#endif
  const esphome::Color dot_col = paused ? ctx.col_gold : (streaming ? ctx.col_green : ctx.col_slate);
  fast_filled_circle(it, dx, dy, r + 2, ctx.col_black);
  fast_filled_circle(it, dx, dy, r, dot_col);
}

void render_cameras_mode_bar(esphome::display::Display &it, const UiFrameContext &ctx, int active_tab) {
  const auto &ly = layout();
  const int bar_cy = ly.app_dock_y;
  const int dock_r = ly.dock_r;
  const int icon_r = ui::dock_icon_r(dock_r);
  const int dock_step = (SCREEN_W < 380)
                            ? ((SCREEN_W - 64) / 3)
                            : ((SCREEN_W > 520) ? ((SCREEN_W * 74) / 466)
                                                : (LATTER_ROUND_DISPLAY ? scale_x(60) : scale_x(72)));
  const int xs[4] = {
      ctx.cx - (3 * dock_step) / 2,
      ctx.cx - dock_step / 2,
      ctx.cx + dock_step / 2,
      ctx.cx + (3 * dock_step) / 2,
  };
  const int min_r = (ly.min_hit_px() + 1) / 2;
  const int hit_pad = std::max(scale_s(8), min_r - dock_r);
  const int pill_pad_x = dock_r + scale_x(10);
  const int pill_x0 = xs[0] - pill_pad_x;
  const int pill_w = (xs[3] + pill_pad_x) - pill_x0;
  const int pill_h = 2 * dock_r + scale_y(12);
  const int pill_y0 = bar_cy - pill_h / 2;
  render_card_box(it, pill_x0, pill_y0, pill_w, pill_h,
                  kChromeGlassTint, kChromeGlassSheen, pill_h / 2, GlassVariant::Clear);

  const float anim_slot = mode_bar_anim().step_active(std::clamp(active_tab, 0, 3), ctx.now_ms);
  const float hl_cx_f = static_cast<float>(xs[0]) + anim_slot * static_cast<float>(dock_step);
  const int hl_r = dock_r - scale_s(2);
  render_card_box(it, static_cast<int>(std::lround(hl_cx_f)) - hl_r, bar_cy - hl_r,
                  2 * hl_r + 1, 2 * hl_r + 1,
                  mix_color_q8(kChromeGlassTint, ctx.theme_primary, 92),
                  ctx.theme_primary, hl_r, GlassVariant::Clear);

  for (int i = 0; i < 4; ++i) {
    const bool is_act = (i == active_tab);
    const esphome::Color fg = is_act ? ctx.col_white : ctx.col_slate;
    if (i == CamerasTab::GRID) {
      ui::draw_icon_grid_2x2(it, xs[i], bar_cy, icon_r, fg, true);
    } else if (i == CamerasTab::SINGLE) {
      ui::draw_icon_camera(it, xs[i], bar_cy, icon_r, fg, true);
    } else if (i == CamerasTab::CAROUSEL) {
      ui::draw_icon_carousel(it, xs[i], bar_cy, icon_r, fg, true);
    } else {
      ui::draw_icon_gear(it, xs[i], bar_cy, icon_r, fg, true);
    }
    frame_hits().add_circle(xs[i], bar_cy, dock_r,
                            static_cast<uint16_t>(WidgetId::MODE_BAR_SLOT_BASE + i),
                            kHitTap | kHitFocusSlot | kHitFocusCircle,
                            static_cast<int16_t>(i), hit_pad);
  }
}

void render_camera_quick_reply_sheet(esphome::display::Display &it, const UiFrameContext &ctx,
                                     const CameraRecord *cam) {
  using esphome::display::TextAlign;
  const auto &ly = layout();
  const auto &rs = runtime_state();
  const auto &ac = area_ctx_state();

  render_page_header(it, ctx.cx, "QUICK REPLY", ctx.font_date, ctx.col_white);

  const std::string area_id = (cam != nullptr) ? cam->area_id : ac.area_id;
  const EntryControlItem *unlock_target = resolve_doorbell_quick_unlock_target(area_id, home_hub_state());
  static constexpr const char *kReplies[3] = {
      "On my way!",
      "Leave it at the door.",
      "One moment, please!",
  };
  const int row_cnt = (unlock_target != nullptr) ? 4 : 3;
  const int top_y = ly.hdr_y + scale_y(36);
  const int bot_y = ly.back_y - scale_y(30);
  const int gap = scale_y(10);
  const int row_h = std::clamp((bot_y - top_y - (row_cnt - 1) * gap) / row_cnt, scale_y(44), scale_y(66));
  const int total_h = row_cnt * row_h + (row_cnt - 1) * gap;
  const int start_y = top_y + std::max(0, (bot_y - top_y - total_h) / 2);

  for (int i = 0; i < 3; ++i) {
    const int ry = start_y + i * (row_h + gap);
    const int cy = ry + row_h / 2;
    const int rw = std::min(scale_w(330), ly.band_safe_span(cy, row_h, 18));
    ui::pill_button(it, ctx.cx - rw / 2, ctx.cx + rw / 2, cy, row_h / 2,
                    kChromeGlassSheen, kChromeGlassTint, kReplies[i], ctx.col_white,
                    ctx.font_small, static_cast<uint16_t>(WidgetId::CAM_REPLY_BASE + i),
                    kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
  }
  if (unlock_target != nullptr) {
    const int ry = start_y + 3 * (row_h + gap);
    const int cy = ry + row_h / 2;
    const int rw = std::min(scale_w(330), ly.band_safe_span(cy, row_h, 18));
    const char *lbl = unlock_target->is_lock ? "Unlock Door" : "Open Garage";
    ui::pill_button(it, ctx.cx - rw / 2, ctx.cx + rw / 2, cy, row_h / 2,
                    ctx.col_gold, mix_color_q8(kChromeGlassTint, ctx.col_gold, 64),
                    lbl, ctx.col_white, ctx.font_small, WidgetId::CAM_UNLOCK_BTN,
                    kHitTap | kHitFocusSlot);
  }

  if (!rs.intercom.cam_intercom_status.empty() &&
      (ctx.now_ms - rs.intercom.cam_status_ms < 3200U)) {
    fast_print(it, ctx.cx, ly.back_y - scale_y(28), ctx.font_micro, ctx.col_gold,
               TextAlign::CENTER, rs.intercom.cam_intercom_status.c_str());
  }
  render_back_button(it, ctx.cx, ctx.font_small, ly.back_y);
}

void render_camera_video_placeholder(esphome::display::Display &it, const UiFrameContext &ctx,
                                     const CameraRect &vp, const char *title, const char *subtitle) {
  using esphome::display::TextAlign;
  fill_rect_row_major(it, vp.x0, vp.y0, vp.w, vp.h, ctx.col_black);
  const int cy = vp.cy();
  ui::draw_icon_camera(it, vp.cx(), cy - scale_y(24), scale_s(24), ctx.col_slate, false);
  if (title != nullptr && title[0] != '\0') {
    fast_print(it, vp.cx(), cy + scale_y(16), ctx.font_small, ctx.col_white, TextAlign::CENTER, title);
  }
  if (subtitle != nullptr && subtitle[0] != '\0') {
    fast_print(it, vp.cx(), cy + scale_y(40), ctx.font_micro, ctx.col_slate, TextAlign::CENTER, subtitle);
  }
}

void render_cameras_grid_tab(esphome::display::Display &it, const UiFrameContext &ctx,
                             const CameraRecord *cams, int cam_count) {
  using esphome::display::TextAlign;
  auto &rs = runtime_state();
  const auto &ly = layout();
  const CameraGridGeom g = compute_camera_grid_geom(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);

  rs.cameras.video_y0 = g.frame.y0;
  rs.cameras.video_y1 = g.frame.y1();

  fill_black_around_rect(it, g.frame, ctx.col_black);

  const int total_pages = std::max(1, (cam_count + 3) / 4);
  rs.cameras.grid_page = std::clamp(rs.cameras.grid_page, 0, total_pages - 1);
  const int page_start = rs.cameras.grid_page * 4;
  const int vis_cnt = std::min(4, std::max(0, cam_count - page_start));

  // Header + optional pagination arrows when > 4 cameras
  render_page_header(it, ctx.cx, "CAMERAS", ctx.font_date, ctx.col_white);
  if (total_pages > 1) {
    char pg_buf[16];
    std::snprintf(pg_buf, sizeof(pg_buf), "%d/%d", rs.cameras.grid_page + 1, total_pages);
    const int btn_r = std::max(scale_s(16), ly.min_hit_px() / 2);
    const int span = std::min(scale_w(140), ly.safe_span(g.title_y, 20) / 2 - btn_r);
    const int lx = ctx.cx - std::max(scale_x(84), span);
    const int rx = ctx.cx + std::max(scale_x(84), span);
    ui::pill_button(it, lx - scale_x(20), lx + scale_x(20), g.title_y, scale_y(16),
                    kChromeGlassSheen, kChromeGlassTint, "<", ctx.col_white, ctx.font_small,
                    WidgetId::CAM_GRID_PREV_BTN, kHitTap | kHitFocusSlot);
    ui::pill_button(it, rx - scale_x(20), rx + scale_x(20), g.title_y, scale_y(16),
                    kChromeGlassSheen, kChromeGlassTint, ">", ctx.col_white, ctx.font_small,
                    WidgetId::CAM_GRID_NEXT_BTN, kHitTap | kHitFocusSlot);
    fast_print(it, ctx.cx, g.frame.y0 - scale_y(8), ctx.font_micro, ctx.col_slate, TextAlign::CENTER, pg_buf);
  }

  // Blit live 2x2 server-composited MJPEG grid frame (or clean placeholder while connecting)
  bool drew_live = false;
  const uint16_t *v_px = nullptr;
  int v_w = 0, v_h = 0;
  if (video_client().acquire_frame(v_px, v_w, v_h)) {
    drew_live = blit_rgb565_fitted_frame(it, v_px, v_w, v_h, g.frame, /*fill=*/false, false);
    video_client().release_frame();
  }
  if (!drew_live) {
    fill_rect_row_major(it, g.frame.x0, g.frame.y0, g.frame.w, g.frame.h, esphome::Color(14, 18, 24));
    const int mid_x = g.frame.x0 + g.frame.w / 2;
    const int mid_y = g.frame.y0 + g.frame.h / 2;
    fast_vline(it, mid_x, g.frame.y0, g.frame.h, esphome::Color(28, 34, 46));
    fast_hline(it, g.frame.x0, mid_y, g.frame.w, esphome::Color(28, 34, 46));
    const char *st_lbl = (video_client().state() == VideoStreamState::ERROR) ? "Stream unavailable" : "Connecting...";
    for (int q = 0; q < vis_cnt; ++q) {
      const auto &tr = g.tiles[q];
      ui::draw_icon_camera(it, tr.cx(), tr.cy() - scale_y(8), scale_s(14), ctx.col_slate, false);
      fast_print(it, tr.cx(), tr.cy() + scale_y(14), ctx.font_micro, ctx.col_slate, TextAlign::CENTER, st_lbl);
    }
  }

  // Subtle divider lines + crisp quadrant name overlays + 4 exact FrameHitTable targets
  const esphome::Color grid_brd(44, 52, 68);
  fast_hline(it, g.frame.x0, g.frame.y0, g.frame.w, grid_brd);
  fast_hline(it, g.frame.x0, g.frame.y1(), g.frame.w, grid_brd);
  fast_vline(it, g.frame.x0, g.frame.y0, g.frame.h, grid_brd);
  fast_vline(it, g.frame.x1(), g.frame.y0, g.frame.h, grid_brd);
  for (int q = 0; q < vis_cnt; ++q) {
    const int cam_idx = page_start + q;
    const auto &tr = g.tiles[q];
    const int lbl_h = std::max(18, scale_y(20));
    const int lbl_y0 = tr.y1() - lbl_h + 1;
    fill_rect_row_major(it, tr.x0 + 1, lbl_y0, std::max(1, tr.w - 2), lbl_h - 1, esphome::Color(10, 14, 20));
    const std::string fit_name = fit_text_to_width_font(cams[cam_idx].name, std::max(32, tr.w - 10), ctx.font_micro);
    fast_print(it, tr.cx(), lbl_y0 + lbl_h / 2, ctx.font_micro, ctx.col_white, TextAlign::CENTER, fit_name.c_str());

    frame_hits().add_rect(tr.x0, tr.y0, tr.w, tr.h,
                          static_cast<uint16_t>(WidgetId::CAM_GRID_TILE_BASE + q),
                          kHitTap | kHitFocusSlot, static_cast<int16_t>(cam_idx));
  }

  draw_live_dot(it, ctx, drew_live, false);
  render_cameras_mode_bar(it, ctx, CamerasTab::GRID);
}

void render_cameras_single_tab(esphome::display::Display &it, const UiFrameContext &ctx,
                               const CameraRecord *cams, int cam_count) {
  using esphome::display::TextAlign;
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &ly = layout();
  const auto &ac = area_ctx_state();

  if (cam_count > 0) {
    rs.cameras.selected_idx = std::clamp(rs.cameras.selected_idx, 0, cam_count - 1);
  } else {
    rs.cameras.selected_idx = 0;
  }
  const CameraRecord *cam = (cam_count > 0) ? &cams[rs.cameras.selected_idx] : nullptr;

  if (rs.cameras.quick_reply_open) {
    render_camera_quick_reply_sheet(it, ctx, cam);
    return;
  }

  const bool want_fill = rs.cameras.effective_fill(prefs);
  const CameraRect vp = compute_uncropped_video_viewport(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, want_fill);
  rs.cameras.video_y0 = vp.y0;
  rs.cameras.video_y1 = vp.y1();

  fill_black_around_rect(it, vp, ctx.col_black);

  bool drew_frame = false;
  bool is_live_stream = false;
  const bool snapshot_mode = (prefs.camera_stream_mode == 2U) || ac.camera_stream_url.empty();
  if (!snapshot_mode) {
    const uint16_t *v_px = nullptr;
    int v_w = 0, v_h = 0;
    if (video_client().acquire_frame(v_px, v_w, v_h)) {
      drew_frame = blit_rgb565_fitted_frame(it, v_px, v_w, v_h, vp, want_fill, want_fill);
      is_live_stream = drew_frame;
      video_client().release_frame();
    }
  }
  if (!drew_frame && ctx.camera_snapshot_art.valid()) {
    drew_frame = blit_rgb565_fitted_frame(
        it, ctx.camera_snapshot_art.data, ctx.camera_snapshot_art.width,
        ctx.camera_snapshot_art.height, vp, want_fill, want_fill);
  }
  if (!drew_frame) {
    const char *cam_title = (cam != nullptr) ? cam->name.c_str() : "Camera";
    const char *sub = (video_client().state() == VideoStreamState::ERROR)
                          ? "Reconnecting..."
                          : "Connecting to live feed...";
    render_camera_video_placeholder(it, ctx, vp, cam_title, sub);
  }

  // Register full-video tap BEFORE HUD buttons so tapping empty video wakes/toggles HUD
  frame_hits().add_rect(0, 0, SCREEN_W, SCREEN_H, WidgetId::CAM_VIDEO_TAP, kHitTap);
  draw_live_dot(it, ctx, is_live_stream, false);

  // Live Doorbell PTT Banner (D-066): NEVER hide live video during talk!
  if (rs.voice.intercom_turn) {
    const int banner_y = SCREEN_H - scale_y(LATTER_ROUND_DISPLAY ? 76 : 60);
    render_doorbell_ptt_banner(it, ctx, banner_y);
    return;
  }

  // 6 s auto-hiding HUD (keep HUD visible while talking/listening)
  const bool show_hud = rs.cameras.hud_active || cam_call_active() || cam_listen_active();
  if (!show_hud) {
    return;
  }

  // Top Title Pill + Prev/Next arrows when > 1 camera
  const int top_y = ly.hdr_y + scale_y(LATTER_ROUND_DISPLAY ? 4 : 6);
  const std::string title_raw = (cam != nullptr) ? cam->name : std::string("Camera");
  const int safe_top_w = ly.safe_span(top_y, 18);
  const int arrow_w = (cam_count > 1) ? scale_x(38) : 0;
  const int pill_max_w = std::max(80, safe_top_w - (cam_count > 1 ? (2 * arrow_w + scale_x(16)) : 0));
  const std::string fit_title = fit_text_to_width_font(title_raw, std::min(scale_w(210), pill_max_w - 20), ctx.font_small);
  const int title_w = std::min(pill_max_w, scale_w(220));
  const int title_r = scale_y(17);
  render_card_box(it, ctx.cx - title_w / 2, top_y - title_r, title_w, 2 * title_r,
                  kChromeGlassTint, kChromeGlassSheen, title_r, GlassVariant::Clear);
  fast_print(it, ctx.cx, top_y, ctx.font_small, ctx.col_white, TextAlign::CENTER, fit_title.c_str());

  if (cam_count > 1) {
    const int lx2 = ctx.cx - title_w / 2 - scale_x(6);
    const int lx1 = lx2 - arrow_w;
    const int rx1 = ctx.cx + title_w / 2 + scale_x(6);
    const int rx2 = rx1 + arrow_w;
    ui::pill_button(it, lx1, lx2, top_y, title_r, kChromeGlassSheen, kChromeGlassTint,
                    "<", ctx.col_white, ctx.font_small, WidgetId::CAM_PREV_BTN, kHitTap | kHitFocusSlot);
    ui::pill_button(it, rx1, rx2, top_y, title_r, kChromeGlassSheen, kChromeGlassTint,
                    ">", ctx.col_white, ctx.font_small, WidgetId::CAM_NEXT_BTN, kHitTap | kHitFocusSlot);
  }

  // Secondary framing & rotate pills row below top bar
  const int sub_y = top_y + title_r + scale_y(20);
  const int sub_r = scale_y(14);
  const bool show_rot = (SCREEN_W != SCREEN_H);
  const int fit_w = scale_x(68);
  if (show_rot) {
    const int gap = scale_x(10);
    ui::pill_button(it, ctx.cx - fit_w - gap / 2, ctx.cx - gap / 2, sub_y, sub_r,
                    want_fill ? ctx.theme_primary : kChromeGlassSheen,
                    want_fill ? mix_color_q8(kChromeGlassTint, ctx.theme_primary, 72) : kChromeGlassTint,
                    want_fill ? "Fill" : "Fit", ctx.col_white, ctx.font_micro,
                    WidgetId::CAM_FRAMING_BTN, kHitTap | kHitFocusSlot);
    ui::pill_button(it, ctx.cx + gap / 2, ctx.cx + fit_w + gap / 2, sub_y, sub_r,
                    rs.cameras.landscape_mode ? ctx.theme_primary : kChromeGlassSheen,
                    rs.cameras.landscape_mode ? mix_color_q8(kChromeGlassTint, ctx.theme_primary, 72) : kChromeGlassTint,
                    "90\xC2\xB0", ctx.col_white, ctx.font_micro,
                    WidgetId::CAM_ORIENT_BTN, kHitTap | kHitFocusSlot);
  } else {
    ui::pill_button(it, ctx.cx - fit_w / 2, ctx.cx + fit_w / 2, sub_y, sub_r,
                    want_fill ? ctx.theme_primary : kChromeGlassSheen,
                    want_fill ? mix_color_q8(kChromeGlassTint, ctx.theme_primary, 72) : kChromeGlassTint,
                    want_fill ? "Fill" : "Fit", ctx.col_white, ctx.font_micro,
                    WidgetId::CAM_FRAMING_BTN, kHitTap | kHitFocusSlot);
  }

  // Capability-gated bottom action pills row above the 4-slot mode bar
  const bool can_listen = (cam == nullptr) ? true : (cam->has_audio() || cam->has_talkback());
  const bool can_talk = (LATTER_HAS_MIC && LATTER_HAS_SPEAKER) &&
                        ((cam == nullptr) ? false : cam->has_talkback());
  const std::string cam_area = (cam != nullptr) ? cam->area_id : ac.area_id;
  const EntryControlItem *unlock_t = resolve_doorbell_quick_unlock_target(cam_area, home_hub_state());
  const bool can_unlock = (unlock_t != nullptr);

  struct ActionBtn {
    const char *label;
    uint16_t wid;
    esphome::Color brd;
    esphome::Color bg;
  };
  ActionBtn btns[4]{};
  int btn_cnt = 0;
  if (can_listen) {
    const bool act = cam_listen_active() || cam_call_active();
    btns[btn_cnt++] = {
        act ? "Mute" : "Listen", WidgetId::CAM_LISTEN_BTN,
        act ? ctx.col_green : kChromeGlassSheen,
        act ? mix_color_q8(kChromeGlassTint, ctx.col_green, 72) : kChromeGlassTint};
  }
  if (can_talk) {
    const bool act = cam_intercom_ptt_active();
    btns[btn_cnt++] = {
        act ? "Stop" : "Talk", WidgetId::CAM_PTT_BTN,
        act ? ctx.col_red : ctx.theme_primary,
        act ? mix_color_q8(kChromeGlassTint, ctx.col_red, 80) : mix_color_q8(kChromeGlassTint, ctx.theme_primary, 64)};
    btns[btn_cnt++] = {"Reply", WidgetId::CAM_REPLY_MENU_BTN, kChromeGlassSheen, kChromeGlassTint};
  }
  if (can_unlock && btn_cnt < 4) {
    btns[btn_cnt++] = {
        unlock_t->is_lock ? "Unlock" : "Open", WidgetId::CAM_UNLOCK_BTN,
        ctx.col_gold, mix_color_q8(kChromeGlassTint, ctx.col_gold, 60)};
  }

  const int act_y = ly.app_dock_y - ly.dock_r - scale_y(28);
  if (btn_cnt > 0) {
    const int act_r = scale_y(18);
    const int act_gap = scale_x(8);
    const int avail_w = std::min(scale_w(360), ly.band_safe_span(act_y, 2 * act_r, 14));
    const int bw = std::max(scale_x(62), (avail_w - (btn_cnt - 1) * act_gap) / btn_cnt);
    const int tot_w = btn_cnt * bw + (btn_cnt - 1) * act_gap;
    int x = ctx.cx - tot_w / 2;
    for (int i = 0; i < btn_cnt; ++i) {
      ui::pill_button(it, x, x + bw, act_y, act_r, btns[i].brd, btns[i].bg,
                      btns[i].label, ctx.col_white, ctx.font_micro, btns[i].wid,
                      kHitTap | kHitFocusSlot);
      x += bw + act_gap;
    }
  }

  if (!rs.intercom.cam_intercom_status.empty() &&
      (ctx.now_ms - rs.intercom.cam_status_ms < 3200U)) {
    fast_print(it, ctx.cx, act_y - scale_y(28), ctx.font_micro, ctx.col_gold,
               TextAlign::CENTER, rs.intercom.cam_intercom_status.c_str());
  }

  render_cameras_mode_bar(it, ctx, CamerasTab::SINGLE);
}

void render_cameras_carousel_tab(esphome::display::Display &it, const UiFrameContext &ctx,
                                 const CameraRecord *cams, int cam_count) {
  using esphome::display::TextAlign;
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &ly = layout();
  const auto &ac = area_ctx_state();

  int car_indices[MAX_CAMERA_SLOTS]{};
  const int car_cnt = collect_carousel_camera_indices(cams, cam_count, car_indices, MAX_CAMERA_SLOTS);
  const CameraRecord *cam = nullptr;
  if (car_cnt > 0) {
    rs.cameras.carousel_idx = std::clamp(rs.cameras.carousel_idx, 0, car_cnt - 1);
    cam = &cams[car_indices[rs.cameras.carousel_idx]];
  }

  const bool want_fill = rs.cameras.effective_fill(prefs);
  const CameraRect vp = compute_uncropped_video_viewport(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, want_fill);
  rs.cameras.video_y0 = vp.y0;
  rs.cameras.video_y1 = vp.y1();

  fill_black_around_rect(it, vp, ctx.col_black);

  bool drew_frame = false;
  bool is_live = false;
  const bool snapshot_mode = (prefs.camera_stream_mode == 2U) || ac.camera_stream_url.empty();
  if (!snapshot_mode) {
    const uint16_t *v_px = nullptr;
    int v_w = 0, v_h = 0;
    if (video_client().acquire_frame(v_px, v_w, v_h)) {
      drew_frame = blit_rgb565_fitted_frame(it, v_px, v_w, v_h, vp, want_fill, want_fill);
      is_live = drew_frame;
      video_client().release_frame();
    }
  }
  if (!drew_frame && ctx.camera_snapshot_art.valid()) {
    drew_frame = blit_rgb565_fitted_frame(
        it, ctx.camera_snapshot_art.data, ctx.camera_snapshot_art.width,
        ctx.camera_snapshot_art.height, vp, want_fill, want_fill);
  }
  if (!drew_frame) {
    render_camera_video_placeholder(it, ctx, vp,
                                    (cam != nullptr) ? cam->name.c_str() : "Carousel",
                                    "Connecting to live feed...");
  }

  frame_hits().add_rect(0, 0, SCREEN_W, SCREEN_H, WidgetId::CAM_VIDEO_TAP, kHitTap);
  draw_live_dot(it, ctx, is_live, rs.cameras.carousel_paused);

  // Subtle camera name pill + thin dwell progress bar (always visible in Carousel)
  const int cap_y = ly.hdr_y + scale_y(LATTER_ROUND_DISPLAY ? 4 : 6);
  std::string cap_txt = (cam != nullptr) ? cam->name : std::string("Cameras");
  if (car_cnt > 1) {
    char idx_buf[20];
    std::snprintf(idx_buf, sizeof(idx_buf), " (%d/%d)", rs.cameras.carousel_idx + 1, car_cnt);
    cap_txt += idx_buf;
  }
  const int cap_w = std::min(scale_w(240), ly.safe_span(cap_y, 24));
  const std::string fit_cap = fit_text_to_width_font(cap_txt, cap_w - 18, ctx.font_small);
  fast_print(it, ctx.cx + 1, cap_y + 1, ctx.font_small, ctx.col_black, TextAlign::CENTER, fit_cap.c_str());
  fast_print(it, ctx.cx, cap_y, ctx.font_small, ctx.col_white, TextAlign::CENTER, fit_cap.c_str());

  if (car_cnt > 1) {
    const uint32_t dwell_ms = prefs.camera_carousel_interval_ms();
    const uint32_t elapsed = (rs.cameras.carousel_last_advance_ms > 0U &&
                              ctx.now_ms >= rs.cameras.carousel_last_advance_ms)
                                 ? (ctx.now_ms - rs.cameras.carousel_last_advance_ms)
                                 : 0U;
    const float frac = rs.cameras.carousel_paused
                           ? 0.0f
                           : std::clamp(static_cast<float>(elapsed) / static_cast<float>(std::max<uint32_t>(1000U, dwell_ms)),
                                        0.0f, 1.0f);
    const int bar_w = std::min(scale_w(180), ly.safe_span(cap_y + scale_y(16), 28));
    const int bar_x = ctx.cx - bar_w / 2;
    const int bar_y = cap_y + scale_y(16);
    fill_rect_row_major(it, bar_x, bar_y, bar_w, 3, esphome::Color(48, 56, 72));
    const int fill_w = static_cast<int>(std::lround(static_cast<float>(bar_w) * frac));
    if (fill_w > 0) {
      fill_rect_row_major(it, bar_x, bar_y, fill_w, 3, ctx.theme_primary);
    }
  }

  if (!rs.cameras.hud_active) {
    return;
  }

  // HUD controls: < | Pause/Resume | > above 4-slot mode bar
  const int ctrl_y = ly.app_dock_y - ly.dock_r - scale_y(28);
  const int ctrl_r = scale_y(18);
  const int pause_w = scale_x(108);
  ui::pill_button(it, ctx.cx - pause_w / 2, ctx.cx + pause_w / 2, ctrl_y, ctrl_r,
                  rs.cameras.carousel_paused ? ctx.col_gold : kChromeGlassSheen,
                  rs.cameras.carousel_paused ? mix_color_q8(kChromeGlassTint, ctx.col_gold, 70) : kChromeGlassTint,
                  rs.cameras.carousel_paused ? "Resume" : "Pause", ctx.col_white, ctx.font_micro,
                  WidgetId::CAM_CAROUSEL_PAUSE_BTN, kHitTap | kHitFocusSlot);
  if (car_cnt > 1) {
    const int aw = scale_x(44);
    const int gap = scale_x(10);
    ui::pill_button(it, ctx.cx - pause_w / 2 - gap - aw, ctx.cx - pause_w / 2 - gap, ctrl_y, ctrl_r,
                    kChromeGlassSheen, kChromeGlassTint, "<", ctx.col_white, ctx.font_small,
                    WidgetId::CAM_PREV_BTN, kHitTap | kHitFocusSlot);
    ui::pill_button(it, ctx.cx + pause_w / 2 + gap, ctx.cx + pause_w / 2 + gap + aw, ctrl_y, ctrl_r,
                    kChromeGlassSheen, kChromeGlassTint, ">", ctx.col_white, ctx.font_small,
                    WidgetId::CAM_NEXT_BTN, kHitTap | kHitFocusSlot);
  }

  render_cameras_mode_bar(it, ctx, CamerasTab::CAROUSEL);
}

void render_cameras_settings_tab(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::display::TextAlign;
  const auto &ly = layout();
  const auto &prefs = persisted_prefs();

  render_page_header(it, ctx.cx, "CAMERA SETTINGS", ctx.font_date, ctx.col_white);

  struct SettingRow {
    const char *label;
    const char *value;
    int idx;
  };
  SettingRow rows[5]{};
  int row_cnt = 0;
  rows[row_cnt++] = {"Default View", pref_camera_default_view_cstr(prefs), 0};
  rows[row_cnt++] = {"Carousel Timer", pref_camera_carousel_interval_cstr(prefs), 1};
  rows[row_cnt++] = {"Framing", pref_camera_framing_cstr(prefs), 2};
  rows[row_cnt++] = {"Stream Mode", pref_camera_stream_mode_cstr(prefs), 3};
#if LATTER_CHIP_P4
  rows[row_cnt++] = {"Doorbell Popup", pref_camera_doorbell_popup_cstr(prefs), 4};
#endif

  const int top_y = ly.hdr_y + scale_y(30);
  const int bot_y = ly.app_dock_y - ly.dock_r - scale_y(16);
  const int gap = scale_y(8);
  const int row_h = std::clamp((bot_y - top_y - (row_cnt - 1) * gap) / row_cnt, scale_y(38), scale_y(56));
  const int total_h = row_cnt * row_h + (row_cnt - 1) * gap;
  const int start_y = top_y + std::max(0, (bot_y - top_y - total_h) / 2);

  for (int i = 0; i < row_cnt; ++i) {
    const int ry = start_y + i * (row_h + gap);
    const int cy = ry + row_h / 2;
    const int rw = std::min(scale_w(350), ly.band_safe_span(cy, row_h, 16));
    const int rx0 = ctx.cx - rw / 2;
    render_card_box(it, rx0, ry, rw, row_h, kChromeGlassTint, kChromeGlassSheen,
                    std::max(10, (row_h * 9) / 20), GlassVariant::Clear);
    const int pad_x = scale_x(16);
    fast_print(it, rx0 + pad_x, cy, ctx.font_small, ctx.col_white, TextAlign::CENTER_LEFT, rows[i].label);
    fast_print(it, rx0 + rw - pad_x, cy, ctx.font_small, ctx.theme_secondary,
               TextAlign::CENTER_RIGHT, rows[i].value);
    frame_hits().add_rect(rx0, ry, rw, row_h,
                          static_cast<uint16_t>(WidgetId::CAM_SETTINGS_ROW_BASE + rows[i].idx),
                          kHitTap | kHitFocusSlot, static_cast<int16_t>(rows[i].idx));
  }

  render_cameras_mode_bar(it, ctx, CamerasTab::SETTINGS);
}

}  // namespace

// ==============================================================================
// Shared RGB565 Fitted Scanline Blitter & Live Doorbell PTT Overlay Banner
// ==============================================================================

bool blit_rgb565_fitted_frame(esphome::display::Display &it, const uint16_t *src,
                              int src_w, int src_h, const CameraRect &dst_rect,
                              bool fill, bool clip_round_bezel) {
  if (src == nullptr || src_w <= 0 || src_h <= 0 || dst_rect.w <= 0 || dst_rect.h <= 0) {
    return false;
  }

  const CameraRect fit_r = compute_video_display_rect(dst_rect, src_w, src_h, fill);
  // Paint black letterbox strips inside dst_rect outside fit_r
  if (!fill && (fit_r.x0 > dst_rect.x0 || fit_r.y0 > dst_rect.y0 ||
                fit_r.w < dst_rect.w || fit_r.h < dst_rect.h)) {
    const esphome::Color black(0, 0, 0);
    if (fit_r.y0 > dst_rect.y0) {
      fill_rect_row_major(it, dst_rect.x0, dst_rect.y0, dst_rect.w, fit_r.y0 - dst_rect.y0, black);
    }
    const int fit_y_end = fit_r.y0 + fit_r.h;
    const int dst_y_end = dst_rect.y0 + dst_rect.h;
    if (dst_y_end > fit_y_end) {
      fill_rect_row_major(it, dst_rect.x0, fit_y_end, dst_rect.w, dst_y_end - fit_y_end, black);
    }
    if (fit_r.x0 > dst_rect.x0) {
      fill_rect_row_major(it, dst_rect.x0, fit_r.y0, fit_r.x0 - dst_rect.x0, fit_r.h, black);
    }
    const int fit_x_end = fit_r.x0 + fit_r.w;
    const int dst_x_end = dst_rect.x0 + dst_rect.w;
    if (dst_x_end > fit_x_end) {
      fill_rect_row_major(it, fit_x_end, fit_r.y0, dst_x_end - fit_x_end, fit_r.h, black);
    }
  }

  // Compute source crop region for `fill == true` (center-crop preserving aspect)
  int crop_x0 = 0;
  int crop_y0 = 0;
  int crop_w = src_w;
  int crop_h = src_h;
  if (fill) {
    const int64_t lhs = static_cast<int64_t>(src_w) * static_cast<int64_t>(fit_r.h);
    const int64_t rhs = static_cast<int64_t>(src_h) * static_cast<int64_t>(fit_r.w);
    if (lhs > rhs) {
      crop_w = std::max(1, static_cast<int>((rhs + fit_r.h / 2) / fit_r.h));
      crop_x0 = (src_w - crop_w) / 2;
    } else if (rhs > lhs) {
      crop_h = std::max(1, static_cast<int>((lhs + fit_r.w / 2) / fit_r.w));
      crop_y0 = (src_h - crop_h) / 2;
    }
  }

  const int y_start = std::max(fit_r.y0, std::max(0, clip_y0()));
  const int y_end = std::min(fit_r.y0 + fit_r.h - 1, std::min(SCREEN_H - 1, clip_y1()));
  if (y_start > y_end) return true;

  const bool one_to_one = (crop_w == fit_r.w && crop_h == fit_r.h && crop_x0 == 0 && crop_y0 == 0);
  const int span_w = std::min(fit_r.w, 1024);
  static uint16_t s_row_buf[1024];

  const int cx = SCREEN_W / 2;
  const int cy = SCREEN_H / 2;
  const int r_lim = std::min(cx, cy) - 2;
  const int r2_lim = r_lim * r_lim;

  for (int y = y_start; y <= y_end; ++y) {
    int row_x0 = fit_r.x0;
    int row_w = span_w;
    int src_x_skip = 0;
#if LATTER_ROUND_DISPLAY
    if (clip_round_bezel) {
      const int dy = y - cy;
      const int rem = r2_lim - dy * dy;
      if (rem <= 0) continue;
      const int half_c = static_cast<int>(std::sqrt(static_cast<float>(rem)));
      const int cx0 = std::max(fit_r.x0, cx - half_c);
      const int cx1 = std::min(fit_r.x0 + span_w - 1, cx + half_c);
      if (cx0 > cx1) continue;
      src_x_skip = cx0 - fit_r.x0;
      row_x0 = cx0;
      row_w = cx1 - cx0 + 1;
    }
#else
    (void)clip_round_bezel;
    (void)cx;
    (void)cy;
    (void)r2_lim;
#endif
    const int sy = crop_y0 + ((y - fit_r.y0) * crop_h) / fit_r.h;
    const uint16_t *src_row = src + std::clamp(sy, 0, src_h - 1) * src_w;
    if (one_to_one) {
      blit_rgb565_scanline(it, row_x0, y, row_w, src_row + src_x_skip, false);
    } else {
      const uint32_t step_fp = (static_cast<uint32_t>(crop_w) << 16) / static_cast<uint32_t>(fit_r.w);
      uint32_t sx_fp = (static_cast<uint32_t>(crop_x0) << 16) + static_cast<uint32_t>(src_x_skip) * step_fp;
      for (int i = 0; i < row_w; ++i) {
        const int sx = std::min(src_w - 1, static_cast<int>(sx_fp >> 16));
        s_row_buf[i] = src_row[sx];
        sx_fp += step_fp;
      }
      blit_rgb565_scanline(it, row_x0, y, row_w, s_row_buf, false);
    }
  }
  return true;
}

void render_doorbell_ptt_banner(esphome::display::Display &it, const UiFrameContext &ctx,
                                int center_y) {
  using esphome::display::TextAlign;
  const auto &rs = runtime_state();
  const auto &ly = layout();
  const int bh = std::max(scale_y(44), ly.min_hit_px());
  const int bw = std::min(scale_w(340), ly.band_safe_span(center_y, bh, 16));
  const int bx0 = ctx.cx - bw / 2;
  const int by0 = center_y - bh / 2;
  const esphome::Color brd = rs.voice.vad_active ? ctx.col_green : ctx.col_gold;
  const esphome::Color tint = mix_color_q8(kChromeGlassTint, brd, 64);

  render_card_box(it, bx0, by0, bw, bh, tint, brd, bh / 2, GlassVariant::Clear);

  // Live VAD pulse indicator on left
  const int dot_x = bx0 + scale_x(20);
  const bool pulse_hi = ((ctx.now_ms / 240U) % 2U) == 0U;
  const int dot_r = std::max(4, scale_s(rs.voice.vad_active && pulse_hi ? 7 : 5));
  fast_filled_circle(it, dot_x, center_y, dot_r, brd);

  const char *msg = (SCREEN_W < 380) ? "Listening... Tap to send"
                                     : "Listening to reply... (Tap to send)";
  const std::string fit_msg = fit_text_to_width_font(msg, std::max(40, bw - scale_x(44)), ctx.font_micro);
  fast_print(it, bx0 + scale_x(34), center_y, ctx.font_micro, ctx.col_white,
             TextAlign::CENTER_LEFT, fit_msg.c_str());

  frame_hits().add_rect(bx0, by0, bw, bh, WidgetId::CAM_PTT_BANNER_STOP, kHitTap | kHitFocusSlot);
}

// ==============================================================================
// Page 13: Dedicated 4-Tab Cameras App (Grid / Single / Carousel / Settings)
// ==============================================================================

void render_cameras_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  auto &rs = runtime_state();
  const auto &ac = area_ctx_state();

  CameraRecord *cams = camera_records_scratch();
  const int cam_count = parse_cameras_records(ac.cameras_summary, cams, MAX_CAMERA_SLOTS);
  const bool has_fallback_src = !ac.camera_stream_url.empty() || !ac.camera_snapshot_url.empty();

  // Empty state (0 cameras configured) on non-Settings tabs
  if (cam_count == 0 && !has_fallback_src && rs.cameras.tab != CamerasTab::SETTINGS) {
    const CameraRect full_vp{0, 0, SCREEN_W, SCREEN_H};
    render_camera_video_placeholder(it, ctx, full_vp, "No Cameras Configured",
                                    "Add cameras in Latter Studio");
    render_page_header(it, ctx.cx, "CAMERAS", ctx.font_date, ctx.col_white);
    render_cameras_mode_bar(it, ctx, rs.cameras.tab);
    return;
  }

  // Effective view: Grid auto-falls back to Single when <= 1 camera configured
  const int tab = std::clamp(rs.cameras.tab, CamerasTab::GRID, CamerasTab::SETTINGS);
  if (tab == CamerasTab::SETTINGS) {
    render_cameras_settings_tab(it, ctx);
  } else if (tab == CamerasTab::CAROUSEL) {
    render_cameras_carousel_tab(it, ctx, cams, cam_count);
  } else if (tab == CamerasTab::GRID && cam_count > 1) {
    render_cameras_grid_tab(it, ctx, cams, cam_count);
  } else {
    render_cameras_single_tab(it, ctx, cams, cam_count);
  }
}

}  // namespace latter_ui
