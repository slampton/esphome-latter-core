#include "ui_compositor.h"
#include "ui_pages.h"
#include "hal_video_stream.h"

#include <algorithm>
#include <string>

namespace latter_ui {
using namespace latter_core;

void render_notification_overlay(Rgb565Surface &it, const UiFrameContext &ctx,
                                 const SystemTakeoverState &st) {
  auto &rs = runtime_state();
  const auto &ac = area_ctx_state();
  const auto &ly = layout();
  const int cx = st.cx;
  const int cy = st.cy;
  const uint32_t now_ms = st.now_ms;
  const bool is_crit = (st.notif_priority == "critical");
  const bool is_warn = (st.notif_priority == "warning");
  const bool pulse = ((now_ms / 250U) % 2U) != 0;
  const UiColor accent_c = is_crit ? st.col_red : (is_warn ? st.col_gold : st.theme_primary);

  const bool has_cam_url = !st.notif_camera_url.empty();
  const bool has_cam_ent = !st.notif_camera_entity.empty();
  const bool is_cam_notif = has_cam_url || has_cam_ent;
  const bool live_popup = rs.cameras.doorbell_live_popup_active && is_cam_notif;

  // Lookup matching CameraRecord for per-camera action gating
  CameraRecord *cams = camera_records_scratch();
  const int cam_cnt = parse_cameras_records(ac.cameras_summary, cams, MAX_CAMERA_SLOTS);
  const int cam_idx = has_cam_ent ? find_camera_index_by_entity(cams, cam_cnt, st.notif_camera_entity.sv()) : -1;
  const CameraRecord *cam_rec = (cam_idx >= 0) ? &cams[cam_idx] : nullptr;
  const bool can_audio = (LATTER_HAS_SPEAKER != 0) && (cam_rec != nullptr) && cam_rec->has_audio();
  const bool can_talk = (LATTER_HAS_MIC && LATTER_HAS_SPEAKER) && (cam_rec != nullptr) && cam_rec->has_talkback();
  const bool can_reply = (cam_rec != nullptr) && cam_rec->show_reply();
  const bool can_unlock = (cam_rec != nullptr) && cam_rec->has_unlock();

  const int wrap_max_w = std::min(ly.max_content_w(), dp_px(280));
  const std::string fit_title = fit_text_to_width_font(
      st.notif_title.empty() ? std::string("Notification") : st.notif_title.str(), wrap_max_w, ctx.font_date);

  // --------------------------------------------------------------------------
  // Path A: ESP32-P4 Live Video Doorbell Popup (D-066)
  // --------------------------------------------------------------------------
  if (live_popup) {
    const CameraRect vp = compute_uncropped_video_viewport(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, false);
    rs.cameras.video_y0 = vp.y0;
    rs.cameras.video_y1 = vp.y1();
    // Paint only strips outside vp (zero double-paint on live video pixels)
    if (vp.y0 > 0) {
      fill_rect_row_major(it, 0, 0, SCREEN_W, vp.y0, st.col_black);
    }
    if (vp.y1() + 1 < SCREEN_H) {
      fill_rect_row_major(it, 0, vp.y1() + 1, SCREEN_W, SCREEN_H - (vp.y1() + 1), st.col_black);
    }
    if (vp.x0 > 0) {
      fill_rect_row_major(it, 0, vp.y0, vp.x0, vp.h, st.col_black);
    }
    if (vp.x1() + 1 < SCREEN_W) {
      fill_rect_row_major(it, vp.x1() + 1, vp.y0, SCREEN_W - (vp.x1() + 1), vp.h, st.col_black);
    }

    bool drew_live = false;
    const uint16_t *v_px = nullptr;
    int v_w = 0, v_h = 0;
    if (video_client().acquire_frame(v_px, v_w, v_h)) {
      drew_live = blit_rgb565_fitted_frame(it, v_px, v_w, v_h, vp, /*fill=*/false, false);
      video_client().release_frame();
    }
    if (!drew_live && ctx.camera_snapshot_art.valid()) {
      drew_live = blit_rgb565_fitted_frame(it, ctx.camera_snapshot_art.data,
                                           ctx.camera_snapshot_art.width, ctx.camera_snapshot_art.height,
                                           vp, /*fill=*/false, false);
    }
    if (!drew_live) {
      render_camera_video_placeholder(it, ctx, vp, nullptr, "Connecting live video...", accent_c);
    }

    const UiColor ring_c = (is_crit && !pulse) ? ui::chrome_danger_bg() : accent_c;
    render_bezel_alert_border(it, cx, cy, ring_c);

    // Top title over live video (D-060 / D-070 canonical font_small col_white header)
    render_page_header(it, cx, fit_title, ctx.font_small, st.col_white);

    // Live PTT talk banner keeps video 100% visible during talk!
    if (rs.voice.intercom_turn) {
      const int ptt_y = SCREEN_H - dp_px(LATTER_ROUND_DISPLAY ? 76 : 62);
      const int ptt_top = std::max(1, ptt_y - ly.min_hit_px() / 2 - dp_px(8));
      frame_hits().add_rect(0, 0, SCREEN_W, ptt_top, WidgetId::NOTIF_EXPAND_BTN, kHitTap, 1);
      render_doorbell_ptt_banner(it, ctx, ptt_y);
      return;
    }

    // Interactive action rows directly on P4 Live Popup (D-070 canonical action_pill_row)
    const int btn_h = std::max(38, dp_px(40));
    const int row2_y = SCREEN_H - dp_px(LATTER_ROUND_DISPLAY ? 54 : 38);
    const int row1_y = row2_y - btn_h - dp_px(12);
    const int gap = dp_px(8);

    ui::ActionPillSpec r1[4]{};
    int r1_cnt = 0;
    if (can_audio) {
      const bool act = cam_listen_active() || cam_call_active();
      r1[r1_cnt++] = {act ? "Mute" : "Listen", WidgetId::NOTIF_ANSWER_BTN,
                      act ? ctx.col_green : ui::chrome_card_brd_on(),
                      act ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                      st.col_white, 0, nullptr};
    }
    if (can_talk) {
      r1[r1_cnt++] = {"Talk", WidgetId::NOTIF_TALK_BTN, ctx.theme_primary,
                      ui::chrome_card_bg_on(), st.col_white, 0, nullptr};
    }
    if (can_reply && r1_cnt < 4) {
      r1[r1_cnt++] = {"Reply", WidgetId::NOTIF_REPLY_BTN, ui::chrome_card_brd_on(),
                      ui::chrome_card_bg_off(), st.col_white, 0, nullptr};
    }
    if (can_unlock && r1_cnt < 4) {
      r1[r1_cnt++] = {cam_rec->unlock_is_lock() ? "Unlock" : "Open", WidgetId::NOTIF_UNLOCK_BTN,
                      st.col_gold, ui::chrome_card_bg_on(), st.col_white, 0, nullptr};
    }
    // Tap video area -> Expand into full Cameras app (param=1 so Expand pill param=0 never
    // overwrites this entry, and clamped above action-pill Pass-2 >= 7.0 mm halos!).
    const int top_btn_y = (r1_cnt > 0) ? row1_y : row2_y;
    const int btn_pad = std::max(0, (ly.min_hit_px() - btn_h + 1) / 2);
    const int media_tap_h = std::max(1, top_btn_y - btn_h / 2 - btn_pad - 1);
    frame_hits().add_rect(0, 0, SCREEN_W, media_tap_h, WidgetId::NOTIF_EXPAND_BTN, kHitTap, 1);
    const int stack_row_w = ui::compute_uniform_stack_w(
        (r1_cnt > 0) ? row1_y : row2_y, row2_y, btn_h, std::min(ly.max_content_w(), dp_px(376)), 14);
    if (r1_cnt > 0) {
      ui::action_pill_row(it, row1_y, btn_h, stack_row_w, dp_px(104), gap,
                          r1, r1_cnt, ctx.font_small, 14);
    }

    const ui::ActionPillSpec r2[2] = {
        {"Expand", WidgetId::NOTIF_EXPAND_BTN, ctx.theme_primary,
         ui::chrome_card_bg_on(), st.col_white, 0, nullptr},
        {"Dismiss", WidgetId::NOTIF_DISMISS_BTN, accent_c,
         ui::chrome_card_bg_off(), st.col_white, 0, nullptr},
    };
    ui::action_pill_row(it, row2_y, btn_h, std::min(stack_row_w, dp_px(276)),
                        dp_px(132), gap, r2, 2, ctx.font_small, 16);
    return;
  }

  // --------------------------------------------------------------------------
  // Path B: Universal Default Snapshot Doorbell Popup (Uncropped) / Alert Card
  // --------------------------------------------------------------------------
  const bool cam_ready = (has_cam_url && st.notif_camera_url == st.current_loaded_camera_url &&
                          ctx.camera_snapshot_art.valid());
  const UiColor ring_c = (is_crit && !pulse) ? ui::chrome_danger_bg() : accent_c;

  std::string msg_lines[2];
  const int msg_line_cnt = ui::wrap_text_lines(
      st.notif_message.str(), std::min(ly.max_content_w(), wrap_max_w), ctx.font_small, msg_lines, 2);
  if (msg_line_cnt < 1) msg_lines[0].clear();
  if (msg_line_cnt < 2) msg_lines[1].clear();
  const std::string &line1 = msg_lines[0];
  const std::string &line2 = msg_lines[1];

  if (is_cam_notif) {
    // Uncropped snapshot viewport above action pills (never crops visitor!)
    const int btn_h = std::max(38, dp_px(40));
    const int btn_y = SCREEN_H - dp_px(LATTER_ROUND_DISPLAY ? 64 : 46);
    const int snap_top = ly.hdr_y + dp_px(24);
    const int msg_reserve = line1.empty() ? dp_px(16) : (line2.empty() ? dp_px(30) : dp_px(52));
    const int snap_bot = btn_y - btn_h / 2 - msg_reserve;
    const int snap_h = std::max(80, snap_bot - snap_top);
    int snap_w = SCREEN_W - dp_px(24);
    if (LATTER_ROUND_DISPLAY) {
      snap_w = std::min(ly.safe_span(snap_top, 14), ly.safe_span(snap_bot, 14));
      snap_w = std::clamp(snap_w, 160, SCREEN_W - 24);
    }
    const CameraRect snap_vp{cx - snap_w / 2, snap_top, snap_w, snap_h};

    if (cam_ready) {
      blit_rgb565_fitted_frame(it, ctx.camera_snapshot_art.data,
                               ctx.camera_snapshot_art.width, ctx.camera_snapshot_art.height,
                               snap_vp, /*fill=*/false, false);
    } else {
      render_camera_video_placeholder(it, ctx, snap_vp, nullptr, "Loading camera snapshot...", accent_c);
    }

    render_bezel_alert_border(it, cx, cy, ring_c);
    render_page_header(it, cx, fit_title, ctx.font_small, st.col_white);

    if (!line1.empty()) {
      const int msg_y = snap_bot + dp_px(14);
      ui::shadow_print(it, cx, msg_y, ctx.font_small, st.col_white, UiTextAlign::CENTER, line1.c_str());
      if (!line2.empty()) {
        ui::shadow_print(it, cx, msg_y + dp_px(22), ctx.font_small, st.theme_secondary,
                         UiTextAlign::CENTER, line2.c_str());
      }
    }

    // Tap snapshot -> jump directly into Cameras Single live video (param=1 so View pill param=0
    // keeps its own entry, and clamped above bottom pill Pass-2 >= 7.0 mm halo!).
    const int snap_btn_pad = std::max(0, (ly.min_hit_px() - btn_h + 1) / 2);
    const int max_snap_y1 = btn_y - btn_h / 2 - snap_btn_pad - 2;
    const int snap_tap_h = std::max(1, std::min(snap_vp.h, max_snap_y1 - snap_vp.y0 + 1));
    frame_hits().add_rect(snap_vp.x0, snap_vp.y0, snap_vp.w, snap_tap_h,
                          WidgetId::NOTIF_VIEW_BTN, kHitTap, 1);

    // Bottom buttons: Answer (if audio/talkback) | View | Dismiss (D-070 canonical action_pill_row)
    ui::ActionPillSpec sb[3]{};
    int sb_cnt = 0;
    if (can_audio) {
      sb[sb_cnt++] = {"Answer", WidgetId::NOTIF_ANSWER_BTN, ctx.col_green,
                      ui::chrome_card_bg_on(), st.col_white, 0, nullptr};
    }
    sb[sb_cnt++] = {"View", WidgetId::NOTIF_VIEW_BTN, ctx.theme_primary,
                    ui::chrome_card_bg_on(), st.col_white, 0, nullptr};
    sb[sb_cnt++] = {"Dismiss", WidgetId::NOTIF_DISMISS_BTN, accent_c,
                    ui::chrome_card_bg_off(), st.col_white, 0, nullptr};

    ui::action_pill_row(it, btn_y, btn_h, std::min(ly.max_content_w(), dp_px(348)),
                        dp_px(112), dp_px(8), sb, sb_cnt, ctx.font_small, 14);
    return;
  }

  // Regular non-camera push notification
  render_bezel_alert_border(it, cx, cy, ring_c);
  const char *hdr_lbl = is_crit ? "CRITICAL ALERT" : (is_warn ? "WARNING" : "NOTIFICATION");
  render_page_header(it, cx, hdr_lbl, ctx.font_small, st.col_white);
  const int dis_h = std::max(44, dp_px(48));
  const int dis_y = std::min(ly.back_y - dp_px(32), (SCREEN_H * 72) / 100);
  const int bell_y = ly.hdr_y + std::max(dp_px(48), (dis_y - ly.hdr_y) * 24 / 100);
  const int title_y = bell_y + dp_px(46);
  const int line1_y = title_y + std::max(kAutoFonts.date, dp_px(36));
  const int line2_y = line1_y + std::max(kAutoFonts.small + dp_px(4), dp_px(24));

  ui::draw_icon_bell(it, cx, bell_y, ui::hero_icon_r(dp_px(84)), accent_c, true);
  ui::shadow_print(it, cx, title_y, ctx.font_date, st.col_white, UiTextAlign::CENTER, fit_title.c_str());

  if (!line1.empty()) {
    ui::shadow_print(it, cx, line1_y, ctx.font_small,
                     st.theme_secondary, UiTextAlign::CENTER, line1.c_str());
  }
  if (!line2.empty()) {
    ui::shadow_print(it, cx, line2_y, ctx.font_small, st.theme_secondary,
                     UiTextAlign::CENTER, line2.c_str());
  }

  const int dis_safe = ly.band_safe_span(dis_y, dis_h, dp_px(16));
  const int dis_w = std::min({ly.max_content_w(), dp_px(216), dis_safe});
  ui::action_pill_centered(it, cx, dis_w, dis_y, dis_h,
                           accent_c, ui::chrome_card_bg_off(), "Dismiss", st.col_white,
                           ctx.font_small, WidgetId::NOTIF_DISMISS_BTN, kHitTap | kHitFocusSlot);
}

}  // namespace latter_ui
