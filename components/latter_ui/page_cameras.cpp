#include "ui_pages.h"
#include "hal_video_stream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace latter_ui {
using namespace latter_core;

namespace {

// Draws a solid black backdrop for rows outside `inner` (strictly honoring active clip band).
void fill_black_around_rect(Rgb565Surface &it, const CameraRect &inner, UiColor col_black) {
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

void draw_live_dot(Rgb565Surface &it, const UiFrameContext &ctx,
                   bool streaming, bool paused) {
  const int r = std::max(4, dp_px(5));
  int dy = std::max(dp_px(26), r + 8);
  const int corner_in = bezel_x_inset_for(dy, SCREEN_W, SCREEN_H, LATTER_CORNER_R, false, r + 6);
  int dx = SCREEN_W - std::max(dp_px(26), corner_in);
#if LATTER_ROUND_DISPLAY
  dx = ctx.cx + dp_px(72);
  dy = layout().hdr_y;
#endif
  const UiColor dot_col = paused ? ctx.col_gold : (streaming ? ctx.col_green : ctx.col_slate);
  fast_filled_circle(it, dx, dy, r + 2, ctx.col_black);
  fast_filled_circle(it, dx, dy, r, dot_col);
}

void draw_cameras_mode_icon(Rgb565Surface &it, int global_idx,
                            int bx, int by, int icon_r,
                            UiColor fg_col, bool shadow, void *) {
  if (global_idx == CamerasTab::GRID) {
    ui::draw_icon_grid_2x2(it, bx, by, icon_r, fg_col, shadow);
  } else if (global_idx == CamerasTab::SINGLE) {
    ui::draw_icon_camera(it, bx, by, icon_r, fg_col, shadow);
  } else if (global_idx == CamerasTab::CAROUSEL) {
    ui::draw_icon_carousel(it, bx, by, icon_r, fg_col, shadow);
  } else {
    ui::draw_icon_gear(it, bx, by, icon_r, fg_col, shadow);
  }
}

void draw_cam_chev_left(Rgb565Surface &it, int cx, int cy, int r,
                        UiColor col, bool shadow, void *) {
  ui::draw_icon_chevron(it, cx, cy, r, false, col, shadow);
}

void draw_cam_chev_right(Rgb565Surface &it, int cx, int cy, int r,
                         UiColor col, bool shadow, void *) {
  ui::draw_icon_chevron(it, cx, cy, r, true, col, shadow);
}

void render_cameras_mode_bar(Rgb565Surface &it, const UiFrameContext &ctx, int active_tab) {
  ui::mode_bar(it, ctx.cx, 4, std::clamp(active_tab, 0, 3), 0,
               ctx.theme_primary, ctx.col_white, draw_cameras_mode_icon);
}

void render_camera_quick_reply_sheet(Rgb565Surface &it, const UiFrameContext &ctx,
                                     const CameraRecord *cam) {
  const auto &ly = layout();
  const auto &rs = runtime_state();

  render_page_header(it, ctx.cx, "QUICK REPLY", ctx.font_small);
  if (!rs.intercom.cam_intercom_status.empty() &&
      (ctx.now_ms - rs.intercom.cam_status_ms < 3200U)) {
    render_sub_header(it, ctx.cx, rs.intercom.cam_intercom_status, ctx.font_small, ctx.theme_secondary);
  }

  const bool can_unlock = (cam != nullptr) && cam->has_unlock();
  static constexpr const char *kReplies[3] = {
      "On my way!",
      "Leave it at the door.",
      "One moment, please!",
  };
  // D-072: Standard slot5_* list geometry (list_card_w x slot5_h, slot5_stride)
  for (int i = 0; i < 3; ++i) {
    ui::list_row(it, ctx.cx, ly.slot5_y(i), ly.list_card_w, ly.slot5_h, kReplies[i], nullptr,
                 ui::chrome_card_bg_off(), ui::chrome_card_brd_on(),
                 ctx.col_white, ctx.col_slate, ctx.font_small,
                 static_cast<uint16_t>(WidgetId::CAM_REPLY_BASE + i),
                 kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
  }
  if (can_unlock) {
    const char *lbl = cam->unlock_is_lock() ? "Unlock Door" : "Open Garage";
    ui::list_row(it, ctx.cx, ly.slot5_y(3), ly.list_card_w, ly.slot5_h, lbl, nullptr,
                 ui::chrome_card_bg_on(), ctx.col_gold,
                 ctx.col_white, ctx.col_slate, ctx.font_small,
                 WidgetId::CAM_UNLOCK_BTN, kHitTap | kHitFocusSlot, 0);
  }

  render_back_button(it, ctx.cx, ctx.font_small, ly.back_y);
}

void render_cameras_grid_tab(Rgb565Surface &it, const UiFrameContext &ctx,
                             const CameraRecord *cams, int cam_count) {
  auto &rs = runtime_state();
  const auto &ly = layout();
  CameraGridGeom g = compute_camera_grid_geom(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);
  const int bounce_y = edge_bounce().offset_px(PageId::CAMERAS, GestureAxisLock::Vertical,
                                               ctx.now_ms, LATTER_PANEL_PPI);
  if (bounce_y != 0) {
    g.frame.y0 = std::clamp(g.frame.y0 + bounce_y, 0, std::max(0, SCREEN_H - g.frame.h));
    for (int q = 0; q < 4; ++q) {
      g.tiles[q].y0 = g.frame.y0 + (q / 2) * g.tiles[q].h;
    }
  }

  rs.cameras.video_y0 = g.frame.y0;
  rs.cameras.video_y1 = g.frame.y1();

  fill_black_around_rect(it, g.frame, ctx.col_black);

  const int total_pages = std::max(1, (cam_count + 3) / 4);
  rs.cameras.grid_page = std::clamp(rs.cameras.grid_page, 0, total_pages - 1);
  const int page_start = rs.cameras.grid_page * 4;
  const int vis_cnt = std::min(4, std::max(0, cam_count - page_start));

  // Header + optional pagination arrows when > 4 cameras (zero glass behind title text)
  if (total_pages > 1) {
    char hdr_buf[24];
    std::snprintf(hdr_buf, sizeof(hdr_buf), "CAMERAS %d/%d", rs.cameras.grid_page + 1, total_pages);
    ui::header_pager_bar(it, ctx.cx, ly.hdr_y, hdr_buf, ctx.font_small, ctx.col_white, true,
                         WidgetId::CAM_GRID_PREV_BTN, WidgetId::CAM_GRID_NEXT_BTN);
  } else {
    render_page_header(it, ctx.cx, "CAMERAS", ctx.font_small);
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
    fill_rect_row_major(it, g.frame.x0, g.frame.y0, g.frame.w, g.frame.h, kChromeGridBg);
    const int mid_x = g.frame.x0 + g.frame.w / 2;
    const int mid_y = g.frame.y0 + g.frame.h / 2;
    fast_vline(it, mid_x, g.frame.y0, g.frame.h, kChromeGridLine);
    fast_hline(it, g.frame.x0, mid_y, g.frame.w, kChromeGridLine);
    const char *st_lbl = (video_client().state() == VideoStreamState::ERROR) ? "Stream unavailable" : "Connecting...";
    for (int q = 0; q < vis_cnt; ++q) {
      const auto &tr = g.tiles[q];
      ui::draw_icon_camera(it, tr.cx(), tr.cy() - dp_px(8), dp_px(14), ctx.col_slate, false);
      fast_print(it, tr.cx(), tr.cy() + dp_px(14), ctx.font_micro, ctx.col_slate, TextAlign::CENTER, st_lbl);
    }
  }

  // Subtle divider lines + crisp quadrant name overlays + 4 exact FrameHitTable targets
  fast_hline(it, g.frame.x0, g.frame.y0, g.frame.w, kChromeGridDivider);
  fast_hline(it, g.frame.x0, g.frame.y1(), g.frame.w, kChromeGridDivider);
  fast_vline(it, g.frame.x0, g.frame.y0, g.frame.h, kChromeGridDivider);
  fast_vline(it, g.frame.x1(), g.frame.y0, g.frame.h, kChromeGridDivider);
  for (int q = 0; q < vis_cnt; ++q) {
    const int cam_idx = page_start + q;
    const auto &tr = g.tiles[q];
    const int lbl_h = std::max(18, dp_px(20));
    const int lbl_y0 = tr.y1() - lbl_h + 1;
    fill_rect_row_major(it, tr.x0 + 1, lbl_y0, std::max(1, tr.w - 2), lbl_h - 1, kChromeCaptionBg);
    const std::string fit_name = fit_text_to_width_font(cams[cam_idx].name, std::max(32, tr.w - 10), ctx.font_micro);
    fast_print(it, tr.cx(), lbl_y0 + lbl_h / 2, ctx.font_micro, ctx.col_white, TextAlign::CENTER, fit_name.c_str());

    frame_hits().add_rect(tr.x0, tr.y0, tr.w, tr.h,
                          static_cast<uint16_t>(WidgetId::CAM_GRID_TILE_BASE + q),
                          kHitTap | kHitFocusSlot, static_cast<int16_t>(cam_idx));
  }

  draw_live_dot(it, ctx, drew_live, false);
  render_cameras_mode_bar(it, ctx, CamerasTab::GRID);
}

void render_cameras_single_tab(Rgb565Surface &it, const UiFrameContext &ctx,
                               const CameraRecord *cams, int cam_count) {
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
    render_camera_video_placeholder(it, ctx, vp, cam_title, sub, ctx.col_slate);
  }

  draw_live_dot(it, ctx, is_live_stream, false);

  // Live Doorbell PTT Banner (D-066): NEVER hide live video during talk!
  if (rs.voice.intercom_turn) {
    const int banner_y = SCREEN_H - dp_px(LATTER_ROUND_DISPLAY ? 76 : 60);
    const int tap_h = std::max(1, banner_y - ly.min_hit_px() / 2 - dp_px(8));
    frame_hits().add_rect(0, 0, SCREEN_W, tap_h, WidgetId::CAM_VIDEO_TAP, kHitTap);
    render_doorbell_ptt_banner(it, ctx, banner_y);
    return;
  }

  // 6 s auto-hiding HUD (keep HUD visible while talking/listening)
  const bool show_hud = rs.cameras.hud_active || cam_call_active() || cam_listen_active();
  if (!show_hud) {
    frame_hits().add_rect(0, 0, SCREEN_W, SCREEN_H, WidgetId::CAM_VIDEO_TAP, kHitTap);
    return;
  }

  // D-048 / D-070: Bare shadowed camera title (zero glass behind title text) + [‹]/[›] when > 1 camera.
  const std::string title_raw = (cam != nullptr) ? cam->name : std::string("Camera");
  ui::header_pager_bar(it, ctx.cx, ly.hdr_y, title_raw, ctx.font_small, ctx.col_white,
                       cam_count > 1, WidgetId::CAM_PREV_BTN, WidgetId::CAM_NEXT_BTN);

  // Portrait-only 90° rotate pill (Framing Fit/Fill lives exclusively in Settings so Single & Carousel match)
  int top_hud_bot = (cam_count > 1) ? (ly.hdr_y + ly.min_hit_px() / 2 + 1) : 0;
  if (SCREEN_H > SCREEN_W) {
    const int sub_y = ly.sub_hdr_y + dp_px(10);
    const int rot_h = std::max(34, dp_px(36));
    const int rot_w = std::max(68, dp_px(76));
    const int rot_pad = std::max(0, (ly.min_hit_px() - rot_h + 1) / 2);
    top_hud_bot = std::max(top_hud_bot, sub_y + rot_h / 2 + rot_pad + 1);
    ui::action_pill_centered(it, ctx.cx, rot_w, sub_y, rot_h,
                             rs.cameras.landscape_mode ? ctx.theme_primary : ui::chrome_card_brd_on(),
                             rs.cameras.landscape_mode ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                             "90\xC2\xB0", ctx.col_white, ctx.font_small,
                             WidgetId::CAM_ORIENT_BTN, kHitTap | kHitFocusSlot);
  }

  // Per-camera configured action pills row above the 4-slot mode bar (D-070 canonical action_pill_row)
  const bool can_listen = (LATTER_HAS_SPEAKER != 0) && (cam != nullptr) && cam->has_audio();
  const bool can_talk = (LATTER_HAS_MIC && LATTER_HAS_SPEAKER) && (cam != nullptr) && cam->has_talkback();
  const bool can_reply = (cam != nullptr) && cam->show_reply();
  const bool can_unlock = (cam != nullptr) && cam->has_unlock();

  ui::ActionPillSpec btns[4]{};
  int btn_cnt = 0;
  if (can_listen) {
    const bool act = cam_listen_active() || cam_call_active();
    btns[btn_cnt++] = {
        act ? "Mute" : "Listen", WidgetId::CAM_LISTEN_BTN,
        act ? ctx.col_green : ui::chrome_card_brd_on(),
        act ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
        ctx.col_white, 0, nullptr};
  }
  if (can_talk) {
    const bool act = cam_intercom_ptt_active();
    btns[btn_cnt++] = {
        act ? "Stop" : "Talk", WidgetId::CAM_PTT_BTN,
        act ? ctx.col_red : ctx.theme_primary,
        act ? ui::chrome_danger_bg() : ui::chrome_card_bg_on(),
        ctx.col_white, 0, nullptr};
  }
  if (can_reply && btn_cnt < 4) {
    btns[btn_cnt++] = {"Reply", WidgetId::CAM_REPLY_MENU_BTN,
                       ui::chrome_card_brd_on(), ui::chrome_card_bg_off(),
                       ctx.col_white, 0, nullptr};
  }
  if (can_unlock && btn_cnt < 4) {
    btns[btn_cnt++] = {
        cam->unlock_is_lock() ? "Unlock" : "Open", WidgetId::CAM_UNLOCK_BTN,
        ctx.col_gold, ui::chrome_card_bg_on(), ctx.col_white, 0, nullptr};
  }

  const int act_h = std::max(38, dp_px(40));
  const int mode_bar_top = ly.app_dock_y - ly.dock_r - std::max(2, dp_px(3));
  const int act_y = mode_bar_top - dp_px(14) - act_h / 2;
  const int act_pad = std::max(0, (ly.min_hit_px() - act_h + 1) / 2);
  const int bot_hud_top = (btn_cnt > 0)
      ? (act_y - act_h / 2 - act_pad - 1)
      : (ly.app_dock_y - ly.dock_hit_r() - 1);
  if (bot_hud_top > top_hud_bot) {
    frame_hits().add_rect(0, top_hud_bot, SCREEN_W, bot_hud_top - top_hud_bot,
                          WidgetId::CAM_VIDEO_TAP, kHitTap);
  }
  if (btn_cnt > 0) {
    ui::action_pill_row(it, act_y, act_h, std::min(ly.max_content_w(380), dp_px(376)),
                        dp_px(104), dp_px(8), btns, btn_cnt, ctx.font_small, dp_px(12));
  }

  if (!rs.intercom.cam_intercom_status.empty() &&
      (ctx.now_ms - rs.intercom.cam_status_ms < 3200U)) {
    ui::shadow_print(it, ctx.cx, act_y - act_h / 2 - dp_px(14), ctx.font_small,
                     ctx.theme_secondary, TextAlign::CENTER,
                     rs.intercom.cam_intercom_status.c_str());
  }

  render_cameras_mode_bar(it, ctx, CamerasTab::SINGLE);
}

void render_cameras_carousel_tab(Rgb565Surface &it, const UiFrameContext &ctx,
                                 const CameraRecord *cams, int cam_count) {
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
                                    "Connecting to live feed...", ctx.col_slate);
  }

  draw_live_dot(it, ctx, is_live, rs.cameras.carousel_paused);

  // Bare shadowed camera name + thin dwell progress bar (always visible in Carousel)
  const int cap_y = ly.hdr_y;
  std::string cap_txt = (cam != nullptr) ? cam->name : std::string("Cameras");
  if (car_cnt > 1) {
    char idx_buf[20];
    std::snprintf(idx_buf, sizeof(idx_buf), " (%d/%d)", rs.cameras.carousel_idx + 1, car_cnt);
    cap_txt += idx_buf;
  }
  const int cap_w = std::min(ly.max_content_w(), ly.safe_span(cap_y, dp_px(24)));
  const std::string fit_cap = fit_text_to_width_font(cap_txt, cap_w - 18, ctx.font_small);
  ui::shadow_print(it, ctx.cx, cap_y, ctx.font_small, ctx.col_white,
                   TextAlign::CENTER, fit_cap.c_str());

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
    const int bar_y = cap_y + dp_px(16);
    const int bar_w = std::min(dp_px(180), ly.safe_span(bar_y, dp_px(28)));
    const int bar_x = ctx.cx - bar_w / 2;
    fill_rect_row_major(it, bar_x, bar_y, bar_w, 3, kChromeDwellTrack);
    const int fill_w = static_cast<int>(std::lround(static_cast<float>(bar_w) * frac));
    if (fill_w > 0) {
      fill_rect_row_major(it, bar_x, bar_y, fill_w, 3, ctx.theme_primary);
    }
  }

  if (!rs.cameras.hud_active) {
    frame_hits().add_rect(0, 0, SCREEN_W, SCREEN_H, WidgetId::CAM_VIDEO_TAP, kHitTap);
    return;
  }

  // HUD controls: [‹] | Pause/Resume | [›] above 4-slot mode bar
  const int ctrl_h = std::max(38, dp_px(40));
  const int mode_bar_top = ly.app_dock_y - ly.dock_r - std::max(2, dp_px(3));
  const int ctrl_y = mode_bar_top - dp_px(14) - ctrl_h / 2;
  const int ctrl_pad = std::max(0, (ly.min_hit_px() - ctrl_h + 1) / 2);
  const int bot_hud_top = std::max(1, ctrl_y - ctrl_h / 2 - ctrl_pad - 1);
  frame_hits().add_rect(0, 0, SCREEN_W, bot_hud_top, WidgetId::CAM_VIDEO_TAP, kHitTap);
  const int pause_w = std::max(96, dp_px(118));
  ui::action_pill_centered(it, ctx.cx, pause_w, ctrl_y, ctrl_h,
                           rs.cameras.carousel_paused ? ctx.col_gold : ui::chrome_card_brd_on(),
                           rs.cameras.carousel_paused ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                           rs.cameras.carousel_paused ? "Resume" : "Pause",
                           ctx.col_white, ctx.font_small,
                           WidgetId::CAM_CAROUSEL_PAUSE_BTN, kHitTap | kHitFocusSlot);
  if (car_cnt > 1) {
    const int btn_r = ctrl_h / 2;
    const int gap = dp_px(12);
    ui::icon_circle_button(it, ctx.cx - pause_w / 2 - gap - btn_r, ctrl_y, btn_r,
                           ui::chrome_card_bg_off(), ui::chrome_card_brd_on(), ctx.col_white,
                           draw_cam_chev_left, nullptr, WidgetId::CAM_PREV_BTN);
    ui::icon_circle_button(it, ctx.cx + pause_w / 2 + gap + btn_r, ctrl_y, btn_r,
                           ui::chrome_card_bg_off(), ui::chrome_card_brd_on(), ctx.col_white,
                           draw_cam_chev_right, nullptr, WidgetId::CAM_NEXT_BTN);
  }

  render_cameras_mode_bar(it, ctx, CamerasTab::CAROUSEL);
}

void render_cameras_settings_tab(Rgb565Surface &it, const UiFrameContext &ctx) {
  const auto &ly = layout();
  const auto &prefs = persisted_prefs();

  render_page_header(it, ctx.cx, "CAMERA SETTINGS", ctx.font_small);

  char labels[5][44]{};
  int ids[5]{};
  int row_cnt = 0;
  std::snprintf(labels[row_cnt], sizeof(labels[row_cnt]), "Default View: %s", pref_camera_default_view_cstr(prefs));
  ids[row_cnt++] = 0;
  std::snprintf(labels[row_cnt], sizeof(labels[row_cnt]), "Carousel: %s", pref_camera_carousel_interval_cstr(prefs));
  ids[row_cnt++] = 1;
  std::snprintf(labels[row_cnt], sizeof(labels[row_cnt]), "Framing: %s", pref_camera_framing_cstr(prefs));
  ids[row_cnt++] = 2;
  std::snprintf(labels[row_cnt], sizeof(labels[row_cnt]), "Stream: %s", pref_camera_stream_mode_cstr(prefs));
  ids[row_cnt++] = 3;
#if LATTER_HAS_HW_JPEG
  std::snprintf(labels[row_cnt], sizeof(labels[row_cnt]), "Doorbell: %s", pref_camera_doorbell_popup_cstr(prefs));
  ids[row_cnt++] = 4;
#endif

  // Dynamic viewport-solved list above Mode Bar (fits all 5 settings on 1 page on Desk/Remote/Wrist!)
  auto &rs = runtime_state();
  const int per_pg = std::clamp(ly.peer_list_rows, 4, kMaxListSlots);
  const int total_pages = std::max(1, (row_cnt + per_pg - 1) / per_pg);
  rs.cameras.settings_page = std::clamp(rs.cameras.settings_page, 0, total_pages - 1);
  const int start_i = rs.cameras.settings_page * per_pg;
  const int vis_cnt = std::min(per_pg, row_cnt - start_i);

  const int bounce_y = edge_bounce().offset_px(PageId::CAMERAS, GestureAxisLock::Vertical,
                                               ctx.now_ms, LATTER_PANEL_PPI);
  for (int i = 0; i < vis_cnt; ++i) {
    const int idx = start_i + i;
    ui::setting_cycle_row(it, ctx.cx, ly.peer_list_row_y(i) + bounce_y, ly.list_card_w, ly.list_h, labels[idx], true,
                          ctx.font_small, ctx.col_white, ctx.col_slate, ctx.theme_secondary,
                          static_cast<uint16_t>(WidgetId::CAM_SETTINGS_ROW_BASE + ids[idx]),
                          kHitTap | kHitFocusSlot, static_cast<int16_t>(ids[idx]));
  }
  if (total_pages > 1) {
    render_vertical_page_pips(it, rs.cameras.settings_page > 0,
                              rs.cameras.settings_page + 1 < total_pages,
                              ctx.col_bg, ctx.theme_primary,
                              ly.mode_bar_zone_top());
  }

  render_cameras_mode_bar(it, ctx, CamerasTab::SETTINGS);
}

}  // namespace

void render_camera_video_placeholder(Rgb565Surface &it, const UiFrameContext &ctx,
                                     const CameraRect &vp, const char *title, const char *subtitle,
                                     UiColor icon_col) {
  fill_rect_row_major(it, vp.x0, vp.y0, vp.w, vp.h, kChromeGridBg);
  const int cy = vp.cy();
  ui::draw_icon_camera(it, vp.cx(), cy - dp_px(22), dp_px(22), icon_col, true);
  if (title != nullptr && title[0] != '\0') {
    const std::string fit = fit_text_to_width_font(std::string(title), std::max(60, vp.w - dp_px(24)), ctx.font_small);
    ui::shadow_print(it, vp.cx(), cy + dp_px(16), ctx.font_small, ctx.col_white,
                     TextAlign::CENTER, fit.c_str());
  }
  if (subtitle != nullptr && subtitle[0] != '\0') {
    const std::string fit = fit_text_to_width_font(std::string(subtitle), std::max(60, vp.w - dp_px(24)), ctx.font_small);
    ui::shadow_print(it, vp.cx(), cy + dp_px(42), ctx.font_small, ctx.col_slate,
                     TextAlign::CENTER, fit.c_str());
  }
}

// ==============================================================================
// Shared RGB565 Fitted Scanline Blitter & Live Doorbell PTT Overlay Banner
// ==============================================================================

bool blit_rgb565_fitted_frame(Rgb565Surface &it, const uint16_t *src,
                              int src_w, int src_h, const CameraRect &dst_rect,
                              bool fill, bool clip_round_bezel) {
  if (src == nullptr || src_w <= 0 || src_h <= 0 || dst_rect.w <= 0 || dst_rect.h <= 0) {
    return false;
  }
  backdrop_active() = true;

  const CameraRect fit_r = compute_video_display_rect(dst_rect, src_w, src_h, fill);
  // Paint black letterbox strips inside dst_rect outside fit_r
  if (!fill && (fit_r.x0 > dst_rect.x0 || fit_r.y0 > dst_rect.y0 ||
                fit_r.w < dst_rect.w || fit_r.h < dst_rect.h)) {
    const UiColor black = kChromeShadow;
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

  // ESP32-P4 hardware bilinear PPA SRM scaling fast path when unclipped and rectangular.
  const bool need_round_clip = (LATTER_ROUND_DISPLAY != 0) && clip_round_bezel;
  uint16_t *const fb_direct = active_frame_buf16();
  if (!one_to_one && !need_round_clip && fb_direct != nullptr &&
      y_start == fit_r.y0 && y_end == (fit_r.y0 + fit_r.h - 1) &&
      fit_r.x0 >= 0 && (fit_r.x0 + fit_r.w) <= SCREEN_W &&
      ppa_srm_scale_rgb565(fb_direct, SCREEN_W, fit_r.x0, fit_r.y0, fit_r.w, fit_r.h,
                           src, src_w, src_h, crop_x0, crop_y0, crop_w, crop_h)) {
    return true;
  }
  // Dynamically sized PSRAM scanline buffer (evictable via global PSRAM cache evictor;
  // zero internal SRAM .bss and no 1024 px ceiling so 1280x800+ never clips).
  static uint16_t *s_dyn_row_buf = nullptr;
  static int s_dyn_row_cap = 0;
  static const bool s_cam_row_evictor_reg = register_psram_cache_evictor([]() noexcept {
    if (s_dyn_row_buf != nullptr) {
      ui_psram_free(s_dyn_row_buf);
      s_dyn_row_buf = nullptr;
      s_dyn_row_cap = 0;
    }
  });
  (void)s_cam_row_evictor_reg;

  uint16_t *row_buf = nullptr;
  int span_w = fit_r.w;
  if (!one_to_one) {
    if (s_dyn_row_cap < fit_r.w) {
      uint16_t *nb = static_cast<uint16_t *>(
          ui_psram_malloc(static_cast<size_t>(fit_r.w) * sizeof(uint16_t)));
      if (nb != nullptr) {
        if (s_dyn_row_buf != nullptr) ui_psram_free(s_dyn_row_buf);
        s_dyn_row_buf = nb;
        s_dyn_row_cap = fit_r.w;
      }
    }
    if (s_dyn_row_buf == nullptr || s_dyn_row_cap <= 0) {
      return false;
    }
    row_buf = s_dyn_row_buf;
    if (s_dyn_row_cap < fit_r.w) {
      span_w = s_dyn_row_cap;
    }
  }

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
      blit_rgb565_scanline(it, row_x0, y, row_w, src_row + src_x_skip);
    } else {
      const uint32_t step_fp = (static_cast<uint32_t>(crop_w) << 16) / static_cast<uint32_t>(fit_r.w);
      uint32_t sx_fp = (static_cast<uint32_t>(crop_x0) << 16) + static_cast<uint32_t>(src_x_skip) * step_fp;
      for (int i = 0; i < row_w; ++i) {
        const int sx = std::min(src_w - 1, static_cast<int>(sx_fp >> 16));
        row_buf[i] = src_row[sx];
        sx_fp += step_fp;
      }
      blit_rgb565_scanline(it, row_x0, y, row_w, row_buf);
    }
  }
  return true;
}

void render_doorbell_ptt_banner(Rgb565Surface &it, const UiFrameContext &ctx,
                                int center_y) {
  const auto &rs = runtime_state();
  const auto &ly = layout();
  const int bh = std::max(dp_px(44), ly.min_hit_px());
  const int bw = ui::compute_uniform_stack_w(center_y, center_y, bh, std::min(ly.max_content_w(340), dp_px(340)), dp_px(16));
  const int bx0 = ctx.cx - bw / 2;
  const int by0 = center_y - bh / 2;
  const UiColor brd = rs.voice.vad_active ? ctx.col_green : ctx.col_gold;

  render_card_box(it, bx0, by0, bw, bh, ui::chrome_card_bg_on(), brd, bh / 2, GlassVariant::Clear);

  // Live VAD pulse indicator on left
  const int dot_x = bx0 + dp_px(20);
  const bool pulse_hi = ((ctx.now_ms / 240U) % 2U) == 0U;
  const int dot_r = std::max(4, dp_px(rs.voice.vad_active && pulse_hi ? 7 : 5));
  fast_filled_circle_aa(it, dot_x, center_y, dot_r, brd);

  const char *msg = (ly.frame.w_dp < 380) ? "Listening... Tap"
                                          : "Listening... (Tap to send)";
  const std::string fit_msg = fit_text_to_width_font(msg, std::max(40, bw - dp_px(46)), ctx.font_small);
  ui::shadow_print(it, bx0 + dp_px(34), center_y, ctx.font_small, ctx.col_white,
                   TextAlign::CENTER_LEFT, fit_msg.c_str());

  frame_hits().add_rect(bx0, by0, bw, bh, WidgetId::CAM_PTT_BANNER_STOP, kHitTap | kHitFocusSlot);
}

// ==============================================================================
// Page 13: Dedicated 4-Tab Cameras App (Grid / Single / Carousel / Settings)
// ==============================================================================

void render_cameras_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  auto &rs = runtime_state();
  const auto &ac = area_ctx_state();

  CameraRecord *cams = camera_records_scratch();
  const int cam_count = parse_cameras_records(ac.cameras_summary, cams, MAX_CAMERA_SLOTS);
  const bool has_fallback_src = !ac.camera_stream_url.empty() || !ac.camera_snapshot_url.empty();

  // Empty state (0 cameras configured) on non-Settings tabs
  if (cam_count == 0 && !has_fallback_src && rs.cameras.tab != CamerasTab::SETTINGS) {
    fill_rect_row_major(it, 0, 0, SCREEN_W, SCREEN_H, ctx.col_black);
    render_page_header(it, ctx.cx, "CAMERAS", ctx.font_small);
    render_empty_state(it, ctx.cx, ctx.cy, "No cameras configured. Add cameras in Latter Studio.",
                       ctx.font_small, ctx.col_slate);
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
