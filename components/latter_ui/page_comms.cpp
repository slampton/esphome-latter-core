#include "esphome/core/defines.h"
#include "ui_pages.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>

namespace latter_ui {

// ==============================================================================
// Shared Intercom & Camera Carousel State (declared in include/ui_pages.h)
// ==============================================================================

bool &intercom_live_call_mode() {
  static bool live_mode = true;
  return live_mode;
}

// intercom_targets_summary (Protocol v3 area_context): records joined by kProtocolRecordSep,
// each "target_id|label|kind". kind is "all" (listed first when another Device exists) or
// "device" (target_id = the Device's slug, label = its title). Empty = no other Device to call.
int parse_intercom_targets_list(const std::string &raw, std::string out_ids[5],
                                std::string out_labels[5], bool out_all[5]) {
  std::string_view recs[5];
  const size_t n = v3_split_fields(raw, recs, 5, kProtocolRecordSep);
  int idx = 0;
  for (size_t r = 0; r < n; r++) {
    std::string_view f[3];
    if (v3_split_fields(recs[r], f, 3, '|') != 3 || f[0].empty()) continue;
    out_ids[idx] = std::string(f[0]);
    out_labels[idx] = std::string(f[1]);
    if (out_all != nullptr) out_all[idx] = (f[2] == "all");
    idx++;
  }
  return idx;
}

int &cam_carousel_idx() {
  static int idx = 0;
  return idx;
}

bool &cam_call_active() {
  static bool active = false;
  return active;
}

bool &cam_listen_active() {
  static bool active = false;
  return active;
}

bool &cam_intercom_ptt_active() {
  static bool active = false;
  return active;
}

std::string &cam_intercom_status() {
  static std::string st;
  return st;
}

int &cam_noise_idx() {
  // Noise filter preset applied by Home Assistant: 0 = strong, 1 = light, 2 = off.
  static int m = 0;
  return m;
}

bool &cam_landscape_mode() {
  static bool landscape = false;
  return landscape;
}

uint32_t &last_cam_live_fetch_ms() {
  static uint32_t ts = 0;
  return ts;
}

bool &cam_download_in_progress() {
  static bool busy = false;
  return busy;
}

// cameras_summary (Protocol v3 area_context): records joined by kProtocolRecordSep, each
// "entity_id|name|area_id|snapshot_url" (snapshot_url is the signed Latter URL). Empty = no cameras.
int parse_cameras_summary_list(const std::string &raw,
                               std::string out_ids[MAX_CAMERA_SLOTS],
                               std::string out_names[MAX_CAMERA_SLOTS],
                               std::string out_urls[MAX_CAMERA_SLOTS]) {
  std::string_view recs[MAX_CAMERA_SLOTS];
  const size_t n = v3_split_fields(raw, recs, MAX_CAMERA_SLOTS, kProtocolRecordSep);
  int idx = 0;
  for (size_t r = 0; r < n; r++) {
    std::string_view f[4];
    if (v3_split_fields(recs[r], f, 4, '|') != 4 || f[0].empty()) continue;
    out_ids[idx] = std::string(f[0]);
    out_names[idx] = std::string(f[1]);
    if (out_urls != nullptr) out_urls[idx] = std::string(f[3]);
    idx++;
  }
  return idx;
}

std::string resolve_active_carousel_camera_id(const std::string &cameras_summary,
                                              const std::string &fallback_cam_url) {
  std::string ids[MAX_CAMERA_SLOTS], names[MAX_CAMERA_SLOTS];
  const int cnt = parse_cameras_summary_list(cameras_summary, ids, names);
  if (cnt > 0) {
    if (cam_carousel_idx() < 0 || cam_carousel_idx() >= cnt) {
      cam_carousel_idx() = 0;
    }
    return ids[cam_carousel_idx()];
  }
  const size_t epos = fallback_cam_url.find("entity_id=");
  if (epos != std::string::npos) {
    const size_t start = epos + 10;
    const size_t amp = fallback_cam_url.find('&', start);
    return (amp == std::string::npos)
               ? fallback_cam_url.substr(start)
               : fallback_cam_url.substr(start, amp - start);
  }
  return "";
}

std::string build_carousel_camera_url(const std::string &base_cam_url,
                                      const std::string &cameras_summary,
                                      uint32_t now_ms) {
  std::string ids[MAX_CAMERA_SLOTS], names[MAX_CAMERA_SLOTS], urls[MAX_CAMERA_SLOTS];
  const int cnt = parse_cameras_summary_list(cameras_summary, ids, names, urls);
  std::string chosen_url;
  if (cnt > 0) {
    if (cam_carousel_idx() < 0 || cam_carousel_idx() >= cnt) {
      cam_carousel_idx() = 0;
    }
    chosen_url = urls[cam_carousel_idx()];
  }
  if (chosen_url.empty() && !base_cam_url.empty()) {
    chosen_url = base_cam_url;
  }
  if (chosen_url.empty()) {
    return "";
  }
  if (cam_landscape_mode() && chosen_url.find("&rot=") == std::string::npos) {
    chosen_url += "&rot=90";
  }
  chosen_url += "&ts=" + std::to_string(now_ms);
  return chosen_url;
}

namespace {

void render_intercom_subpage(esphome::display::Display &it, const UiFrameContext &ctx,
                             const DeviceRuntimeState &rs, const AreaContextState &ac) {
  using esphome::Color;
  using esphome::display::TextAlign;

  if (!frame_base_state().painted) {
    frame_base(it, ctx.col_bg);
  }

  const int cx = ctx.cx;
  const uint32_t now_ms = ctx.now_ms;
  auto *font_small = ctx.font_small;
  const std::string &feedback_str = rs.intercom.broadcast_feedback;
  const uint32_t feedback_ms = rs.intercom.broadcast_feedback_ms;

  const bool show_fb = (!feedback_str.empty() && (now_ms - feedback_ms < 2200U));
  if (show_fb) {
    const std::string fit_fb = fit_text_to_width_font(feedback_str, scale_x(280), font_small);
    render_page_header(it, cx, fit_fb, font_small, ctx.theme_secondary);
  } else {
    render_modern_card(it, cx, scale_y(44), 232, 34, 10, kChromeGlassTint, ctx.theme_primary,
                       intercom_live_call_mode() ? "Mode: Talk >" : "Mode: Broadcast >",
                       ctx.col_white, font_small);
  }
  frame_hits().add_centered_rect(cx, scale_y(44), scale_x(244), scale_y(42),
                                 WidgetId::INTERCOM_MODE_BTN, kHitTap | kHitFocusSlot, 0);

  const Color card_bg = kChromeGlassTint;
  const Color card_brd = kChromeCardBrdOn;
  const int c_left = layout().list_card_left;
  const int hit_w = scale_x(296);
  const int hit_h = scale_y(48);

  if (intercom_live_call_mode()) {
    std::string t_ids[5], t_lbls[5];
    bool t_all[5] = {false, false, false, false, false};
    const int cnt = parse_intercom_targets_list(ac.intercom_targets_summary, t_ids, t_lbls, t_all);
    if (cnt == 0) {
      render_empty_state(it, cx, layout().slot5_y(0), "No other devices to call", font_small, ctx.col_slate);
    }
    for (int i = 0; i < cnt; i++) {
      const int y_mid = layout().slot5_y(i);
      const Color brd_c = t_all[i] ? ctx.theme_secondary : ctx.col_green;
      render_modern_card(it, cx, y_mid, 290, 44, 12, card_bg, brd_c, nullptr, ctx.col_white, font_small);
      fast_filled_circle(it, c_left + scale_x(18), y_mid, scale_s(6), brd_c);
      const std::string fit_l = fit_text_to_width_font(t_lbls[i], layout().list_card_w - scale_x(46), font_small);
      fast_print(it, c_left + scale_x(34), y_mid, font_small, ctx.col_white, TextAlign::CENTER_LEFT, fit_l.c_str());
      frame_hits().add_centered_rect(cx, y_mid, hit_w, hit_h,
                                     static_cast<uint16_t>(WidgetId::INTERCOM_ROW_BASE + i),
                                     kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
    }
    if (cnt < 5) {
      const int unlock_y = layout().slot5_y(4);
      render_modern_card(it, cx, unlock_y, 290, 44, 12, card_bg, ctx.col_green,
                         "Unlock Gate / Door", ctx.col_white, font_small);
      frame_hits().add_centered_rect(cx, unlock_y, hit_w, hit_h,
                                     static_cast<uint16_t>(WidgetId::INTERCOM_ROW_BASE + 4),
                                     kHitTap | kHitFocusSlot, -1);
    }
  } else {
    static constexpr const char *kPresets[5] = {
        "Dinner is ready!",
        "Time to head out!",
        "Kids, come downstairs!",
        "Need a hand in here!",
        "Movie night is starting!"};
    for (int i = 0; i < 5; i++) {
      const int y_mid = layout().slot5_y(i);
      render_modern_card(it, cx, y_mid, 290, 44, 12, card_bg, card_brd, nullptr, ctx.col_white, font_small);
      fast_filled_triangle(it, c_left + scale_x(14), y_mid - 4,
                           c_left + scale_x(14), y_mid + 4,
                           c_left + scale_x(21), y_mid, ctx.theme_primary);
      fast_print(it, c_left + scale_x(32), y_mid, font_small, ctx.col_white, TextAlign::CENTER_LEFT, kPresets[i]);
      frame_hits().add_centered_rect(cx, y_mid, hit_w, hit_h,
                                     static_cast<uint16_t>(WidgetId::INTERCOM_ROW_BASE + i),
                                     kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
    }
  }
  render_back_button(it, cx, font_small, layout().back_y, "< Back");
}

void render_cameras_subpage(esphome::display::Display &it, const UiFrameContext &ctx,
                            const DeviceRuntimeState &rs, const AreaContextState &ac) {
  using esphome::Color;
  using esphome::display::TextAlign;

  if (!frame_base_state().painted) {
    frame_base(it, ctx.col_bg);
  }

  const int cx = ctx.cx;
  const uint32_t now_ms = ctx.now_ms;
  auto *font_small = ctx.font_small;

  std::string ids[MAX_CAMERA_SLOTS], names[MAX_CAMERA_SLOTS];
  const int cam_count = parse_cameras_summary_list(ac.cameras_summary, ids, names);
  if (cam_count > 0 && (cam_carousel_idx() < 0 || cam_carousel_idx() >= cam_count)) {
    cam_carousel_idx() = 0;
  }

  static constexpr const char *kNoiseNames[3] = {
      "Noise Filter: Strong", "Noise Filter: Light", "Noise Filter: Off"};
  const int noise_idx = std::max(0, std::min(2, cam_noise_idx()));

  if (cam_quick_reply_open()) {
    render_page_header(it, cx, "DOORBELL ACTIONS", font_small, ctx.theme_primary);
    const Color card_bg = kChromeGlassTint;
    const Color card_brd = kChromeCardBrdOn;
    const int hit_w = scale_x(302);
    const int hit_h = scale_y(48);

    render_modern_card(it, cx, layout().slot5_y(0), 296, 44, 12, card_bg, ctx.col_green,
                       "Unlock Door / Gate", ctx.col_white, font_small);
    frame_hits().add_centered_rect(cx, layout().slot5_y(0), hit_w, hit_h,
                                   static_cast<uint16_t>(WidgetId::CAM_REPLY_BASE + 0),
                                   kHitTap | kHitFocusSlot, 0);

    render_modern_card(it, cx, layout().slot5_y(1), 296, 44, 12, card_bg, ctx.theme_primary,
                       "Leave package at door", ctx.col_white, font_small);
    frame_hits().add_centered_rect(cx, layout().slot5_y(1), hit_w, hit_h,
                                   static_cast<uint16_t>(WidgetId::CAM_REPLY_BASE + 1),
                                   kHitTap | kHitFocusSlot, 1);

    render_modern_card(it, cx, layout().slot5_y(2), 296, 44, 12, card_bg, ctx.theme_primary,
                       "We'll be right there!", ctx.col_white, font_small);
    frame_hits().add_centered_rect(cx, layout().slot5_y(2), hit_w, hit_h,
                                   static_cast<uint16_t>(WidgetId::CAM_REPLY_BASE + 2),
                                   kHitTap | kHitFocusSlot, 2);

    render_modern_card(it, cx, layout().slot5_y(3), 296, 44, 12, card_bg, card_brd,
                       "No soliciting, thank you", ctx.col_white, font_small);
    frame_hits().add_centered_rect(cx, layout().slot5_y(3), hit_w, hit_h,
                                   static_cast<uint16_t>(WidgetId::CAM_REPLY_BASE + 3),
                                   kHitTap | kHitFocusSlot, 3);

    char view_lbl[56];
    std::snprintf(view_lbl, sizeof(view_lbl), "%s • %s",
                  cam_landscape_mode() ? "90° View" : "Portrait", kNoiseNames[noise_idx]);
    const std::string fit_view = fit_text_to_width_font(view_lbl, layout().list_card_w - scale_x(16), font_small);
    render_modern_card(it, cx, layout().slot5_y(4), 296, 44, 12, card_bg, ctx.theme_secondary,
                       fit_view.c_str(), ctx.col_white, font_small);
    frame_hits().add_centered_rect(cx, layout().slot5_y(4), hit_w, hit_h,
                                   WidgetId::CAM_NOISE_BTN, kHitTap | kHitFocusSlot, 4);

    render_back_button(it, cx, font_small, layout().back_y, "< Back", ctx.col_white);
    return;
  }

  const bool has_url = !ac.camera_snapshot_url.empty() || (cam_count > 0);
  const bool has_frame = (has_url && ctx.camera_snapshot_art.valid() &&
                          ctx.camera_snapshot_art.width > 16 && ctx.camera_snapshot_art.height > 16);

  if (has_frame) {
    render_album_art_canvas(it, ctx.camera_snapshot_art, cx, false, false, false, false);
  } else {
    const char *msg = has_url ? "Loading camera snapshot..." : "Configure camera in Latter Studio";
    render_empty_state(it, cx, scale_y(212), msg, font_small,
                       has_url ? ctx.theme_secondary : ctx.col_slate);
  }

  // 1. Top Translucent Camera Carousel Header Pill (y = 40)
  std::string cur_cam_title = (cam_count > 0) ? names[cam_carousel_idx()] : "CAMERA";
  for (char &c : cur_cam_title) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (cam_count > 1) {
    cur_cam_title += " (" + std::to_string(cam_carousel_idx() + 1) + "/" + std::to_string(cam_count) + ")";
  }
  if (!cam_intercom_status().empty()) {
    cur_cam_title = cam_intercom_status();
  } else if (cam_call_active()) {
    cur_cam_title = "DOORBELL SESSION";
  }
  cur_cam_title = fit_text_to_width_font(cur_cam_title, scale_x(210), font_small);

  const Color hdr_brd = cam_intercom_ptt_active()
                            ? ctx.col_red
                            : (cam_call_active() ? ctx.col_green : ctx.theme_primary);
  const int hdr_w = (!LATTER_ROUND_DISPLAY && SCREEN_W != SCREEN_H) ? 216 : 256;
  const int hdr_cx = (!LATTER_ROUND_DISPLAY && SCREEN_W != SCREEN_H) ? (cx - 24) : cx;
  const int hdr_cy = scale_y(40);
  render_modern_card(it, hdr_cx, hdr_cy, hdr_w, 34, 10, kChromeModalBg, hdr_brd,
                     cur_cam_title.c_str(), ctx.col_white, font_small);
  if (cam_count > 1 && cam_intercom_status().empty()) {
    const int eff_hdr_w = scale_x(hdr_w);
    fast_print(it, hdr_cx - (eff_hdr_w / 2) + 12, hdr_cy, font_small, ctx.theme_secondary, TextAlign::CENTER, "<");
    fast_print(it, hdr_cx + (eff_hdr_w / 2) - 12, hdr_cy, font_small, ctx.theme_secondary, TextAlign::CENTER, ">");
    render_dock_page_pips(it, cx, scale_y(66), cam_count, cam_carousel_idx(), ctx.col_white, ctx.col_slate);
    frame_hits().add_rect(hdr_cx - (eff_hdr_w / 2) - 8, hdr_cy - scale_y(24),
                          (eff_hdr_w / 2) + 8, scale_y(48),
                          WidgetId::CAM_PREV_BTN, kHitTap | kHitFocusSlot, -1);
    frame_hits().add_rect(hdr_cx, hdr_cy - scale_y(24),
                          (eff_hdr_w / 2) + 8, scale_y(48),
                          WidgetId::CAM_NEXT_BTN, kHitTap | kHitFocusSlot, 1);
  }
#if !LATTER_ROUND_DISPLAY
  if (SCREEN_W != SCREEN_H) {
    render_modern_card(it, SCREEN_W - 34, hdr_cy, 48, 34, 9,
                       kChromeModalBg,
                       cam_landscape_mode() ? ctx.theme_primary : kChromeCardBorder,
                       "90°", cam_landscape_mode() ? ctx.col_white : ctx.theme_secondary, font_small);
    frame_hits().add_centered_rect(SCREEN_W - 34, hdr_cy, 58, scale_y(46),
                                   WidgetId::CAM_ORIENT_BTN, kHitTap | kHitFocusSlot, 0);
  }
#endif

  // 2. Bottom Translucent Doorbell Audio & Push-to-Talk Control Deck (y = 368)
  if (has_url) {
    const bool in_call  = cam_call_active();
    const bool speaking = cam_intercom_ptt_active();
    const bool pulse    = ((now_ms / 300U) % 2U) != 0;
    const int deck_y    = scale_y(368);

    if (in_call) {
      render_modern_card(it, cx - scale_x(98), deck_y, 82, 38, 11,
                         kChromeModalBg, ctx.col_red, "End", ctx.col_white, font_small);
    } else {
      const bool is_listening = cam_listen_active();
      render_modern_card(it, cx - scale_x(98), deck_y, 82, 38, 11,
                         kChromeModalBg,
                         is_listening ? ctx.theme_primary : kChromeCardBorder,
                         is_listening ? "Mute" : "Audio",
                         is_listening ? ctx.col_white : ctx.theme_secondary, font_small);
    }
    frame_hits().add_centered_rect(cx - scale_x(98), deck_y, scale_x(90), scale_y(46),
                                   WidgetId::CAM_LISTEN_BTN, kHitTap | kHitFocusSlot, 0);

    const Color call_brd = speaking ? (pulse ? ctx.col_white : ctx.col_red) : ctx.col_green;
    render_modern_card(it, cx, deck_y, 102, 40, 12,
                       kChromeModalBg, call_brd,
                       !in_call ? "Listen+Talk" : (speaking ? "Cancel" : "Talk"),
                       ctx.col_white, font_small);
    frame_hits().add_centered_rect(cx, deck_y, scale_x(108), scale_y(48),
                                   WidgetId::CAM_PTT_BTN, kHitTap | kHitFocusSlot, 0);

    render_modern_card(it, cx + scale_x(98), deck_y, 82, 38, 11,
                       kChromeModalBg, kChromeCardBorder,
                       "Reply", ctx.theme_secondary, font_small);
    frame_hits().add_centered_rect(cx + scale_x(98), deck_y, scale_x(90), scale_y(46),
                                   WidgetId::CAM_REPLY_MENU_BTN, kHitTap | kHitFocusSlot, 0);
  }

  const bool hide_single_app_back = tools_direct_home() &&
                                    !persisted_prefs().home_screen_enabled &&
                                    is_on_anchor_root(rs, persisted_prefs());
  if (!hide_single_app_back) {
    if (has_frame && (!tools_direct_home() || !persisted_prefs().home_screen_enabled)) {
      render_glass_card(it, cx - scale_x(64), layout().back_y - scale_y(16), scale_x(128), scale_y(32),
                        scale_s(16), kChromeModalBg, -1, GlassVariant::ReadableDark, ctx.col_white);
    }
    const char *back_lbl =
        (tools_direct_home() && persisted_prefs().home_screen_enabled) ? "Home" : "< Back";
    render_back_button(it, cx, font_small, layout().back_y, back_lbl, ctx.col_white);
  }
}

}  // namespace

void render_comms_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const auto &ac = area_ctx_state();
  if (rs.nav.tools_subpage == ToolsSubpage::INTERCOM) {
    render_intercom_subpage(it, ctx, rs, ac);
  } else {
    render_cameras_subpage(it, ctx, rs, ac);
  }
}

}  // namespace latter_ui
