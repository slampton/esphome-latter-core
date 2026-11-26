#include "ui_pages.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace latter_ui {
using namespace latter_core;

namespace {

bool is_curtain_entity(const std::string &name, const std::string &id_str) {
  std::string s = name + " " + id_str;
  for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return (s.find("curtain") != std::string::npos ||
          s.find("drape") != std::string::npos ||
          s.find("sheer") != std::string::npos);
}

struct AreaModeIconCtx {
  const std::string *cats{nullptr};
  int count{0};
};

void draw_area_mode_icon(Rgb565Surface &it, int g_idx,
                         int bx, int by, int ir,
                         UiColor fg_c, bool sh, void *user_ctx) {
  const auto *mc = static_cast<const AreaModeIconCtx *>(user_ctx);
  if (mc == nullptr || mc->cats == nullptr || g_idx < 0 || g_idx >= mc->count) return;
  const std::string &cat = mc->cats[g_idx];
  if (cat == "scene") {
    ui::draw_icon_sparkle(it, bx, by, ir, fg_c, sh);
  } else if (cat == "light") {
    ui::draw_icon_lightbulb(it, bx, by, ir, fg_c, sh);
  } else if (cat == "cover") {
    ui::draw_icon_blinds_slats(it, bx, by, ir, fg_c, true, sh);
  } else if (cat == "fan") {
    ui::draw_icon_ceiling_fan(it, bx, by, ir, fg_c, sh);
  } else if (cat == "switch") {
    ui::draw_icon_switch_pill(it, bx, by, ir, fg_c, true, sh);
  } else if (cat == "climate") {
    ui::draw_icon_thermometer(it, bx, by, ir, fg_c, sh);
  } else if (cat == "media") {
    render_mdi_icon(it, "mdi:television-speaker", bx, by, std::max(16, 2 * ir), fg_c, sh, "mdi:speaker");
  } else {
    ui::draw_icon_bento_grid(it, bx, by, ir, fg_c, sh);
  }
}

}  // namespace

void render_area_control_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const auto &ac = area_ctx_state();

  const int cx = ctx.cx;
  const uint32_t now_ms = ctx.now_ms;
  const int area_subpage = rs.nav.area_subpage;
  const std::string &area_browse_mode = rs.area.browse_mode;
  const std::string &area_domain_filter = rs.area.domain_filter;
  const std::string &active_area_name = rs.area.active_area_name;
  const float area_temp = ac.area_temp;
  const float area_humidity = ac.area_humidity;
  const std::string &area_action_feedback = rs.area.action_feedback;
  const uint32_t area_feedback_timer = rs.area.feedback_timer;

  const UiFontView font_small = ctx.font_small;
  const Color &theme_primary = ctx.theme_primary;
  const Color &theme_secondary = ctx.theme_secondary;
  const Color &col_bg = ctx.col_bg;
  const Color &col_black = ctx.col_black;
  const Color &col_white = ctx.col_white;
  const Color &col_slate = ctx.col_slate;
  const Color &col_gold = ctx.col_gold;

  const bool is_fb = (now_ms - area_feedback_timer < 1500U && !area_action_feedback.empty());
  const Color card_bg_off = ui::chrome_card_bg_off();
  const Color card_bg_on = ui::chrome_card_bg_on();
  const Color card_brd_off = ui::chrome_card_brd_off();

  // Helper: Draw Unified Area Category Mode Bar at layout().app_dock_y (1..4 Icon Conveyor Window)
  auto render_area_bottom_dock = [&](const std::string &active_domain) {
    std::string cats[8];
    const int cat_count = collect_area_categories(cats);
    if (cat_count <= 0) return;

    int active_global_idx = 0;
    for (int i = 0; i < cat_count; i++) {
      if (cats[i] == active_domain) {
        active_global_idx = i;
        break;
      }
    }
    area_cat_dock_page() = get_mode_bar_start_index(cat_count, area_cat_dock_page(), layout().dock_slot_count);
    AreaModeIconCtx ic{cats, cat_count};
    ui::mode_bar(it, cx, cat_count, active_global_idx, area_cat_dock_page(),
                 theme_primary, col_white, draw_area_mode_icon, &ic);
  };

  // ===========================================================================
  // SUBPAGE 2: ENTITY LEVEL, COLOR (HSV WHEEL) & WHITE KELVIN INSPECTOR (D-050)
  // ===========================================================================
  if (area_subpage == 2) {
    const auto ly = layout();
    const std::string &insp_dom = light_insp_domain();
    if (insp_dom == "climate") {
      std::string hdr_name = light_insp_name();
      if (hdr_name.empty()) hdr_name = "THERMOSTAT";
      else hdr_name = to_ascii_upper(hdr_name);
      std::string fit_ln = fit_text_to_width_font(hdr_name, std::min(dp_px(260), ly.safe_span(ly.hdr_y, dp_px(18))), font_small);
      render_page_header(it, cx, fit_ln.c_str(), font_small);

      ThermostatZoneItem fallback_clim{};
      const ThermostatZoneItem *clim_ptr = find_climate_item_by_id_const(light_insp_entity_id());
      if (clim_ptr == nullptr) {
        fallback_clim.entity_id = light_insp_entity_id();
        fallback_clim.name = light_insp_name();
        fallback_clim.mode = light_insp_active() ? "heat" : "off";
        fallback_clim.target_temp = (light_insp_bri_pct() > 0) ? light_insp_bri_pct() : 70;
        fallback_clim.caps = kClimateCapDefault;
        clim_ptr = &fallback_clim;
      }

      const int deck_top = ly.hdr_y + dp_px(24);
      const int deck_bot = ly.back_y - dp_px(22);
      render_climate_controls_deck(it, ctx, rs, *clim_ptr, deck_top, deck_bot);
      render_back_button(it, cx, font_small, ly.back_y, "< Back");
      return;
    }
    if (insp_dom == "cover" || insp_dom == "fan") {
      const bool is_cov = (insp_dom == "cover");
      const std::string hdr_name = to_ascii_upper(light_insp_name());
      std::string fit_ln = fit_text_to_width_font(hdr_name, std::min(dp_px(260), ly.safe_span(ly.hdr_y, dp_px(18))), font_small);
      render_page_header(it, cx, fit_ln.c_str(), font_small);

      const int cur_pct = std::clamp(light_insp_bri_pct(), 0, 100);
      const bool is_on_st = light_insp_active() && (cur_pct > 0);
      char sub_st[48];
      if (is_cov) {
        if (!is_on_st || cur_pct == 0) std::snprintf(sub_st, sizeof(sub_st), "Closed");
        else if (cur_pct >= 99) std::snprintf(sub_st, sizeof(sub_st), "Open  •  100%%");
        else std::snprintf(sub_st, sizeof(sub_st), "Open  •  %d%%", cur_pct);
      } else {
        if (!is_on_st || cur_pct == 0) std::snprintf(sub_st, sizeof(sub_st), "Off");
        else std::snprintf(sub_st, sizeof(sub_st), "Running  •  %d%%", cur_pct);
      }
      render_sub_header(it, cx, sub_st, font_small, is_on_st ? theme_secondary : col_slate);

      const int sl_card_w = ly.light_slider_card_w();
      const int b_h = std::max(44, dp_px(64));
      const int b_cy = ly.sub_hdr_y + dp_px(24) + b_h / 2;
      char b_buf[48];
      std::snprintf(b_buf, sizeof(b_buf), "%s: %d%%", is_cov ? "Position" : "Speed", cur_pct);
      ui::slider_card_row(it, cx, b_cy, sl_card_w, b_h,
                          b_buf, cur_pct, 0, 100, true,
                          is_cov ? kChromeCoverFill : kChromeFanFill,
                          is_on_st ? theme_primary : card_brd_off,
                          font_small, col_white, WidgetId::LIGHT_BRI_SLIDER);

      // 4 Quick-Preset Stadium Action Pills (0/25/50/100% for Cover; Off/33/66/100% for Fan)
      const int preset_vals[4] = {0, is_cov ? 25 : 33, is_cov ? 50 : 66, 100};
      const char *const cov_lbls[4] = {"Close", "25%", "50%", "Open"};
      const char *const fan_lbls[4] = {"Off", "33%", "66%", "100%"};
      HomeHubPillGeom p_geoms[4]{};
      const int deck_top = b_cy + b_h / 2 + dp_px(18);
      const int deck_bot = ly.back_y - dp_px(24);
      const bool two_r = home_hub_uses_two_rows(
          4, deck_top, deck_bot, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0,
          LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI, LATTER_CORNER_R,
          LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);
      const int n_p = compute_home_hub_pill_deck(
          4, deck_top, deck_bot, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0,
          LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI, two_r, p_geoms, LATTER_CORNER_R,
          LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);
      for (int p = 0; p < n_p; ++p) {
        const bool sel = (std::abs(cur_pct - preset_vals[p]) <= 4) &&
                         ((preset_vals[p] == 0) ? !is_on_st : is_on_st);
        const char *lbl = is_cov ? cov_lbls[p] : fan_lbls[p];
        ui::action_pill_centered(
            it, p_geoms[p].cx, p_geoms[p].w, p_geoms[p].cy, p_geoms[p].h,
            sel ? theme_primary : card_brd_off, sel ? card_bg_on : card_bg_off,
            lbl, sel ? theme_primary : col_white, font_small,
            static_cast<uint16_t>(WidgetId::ENTITY_PRESET_BASE + p),
            kHitTap | kHitFocusSlot, static_cast<int16_t>(preset_vals[p]),
            std::max(p_geoms[p].h, p_geoms[p].hit_h));
      }

      render_back_button(it, cx, font_small, ly.back_y, "< Back");
      return;
    }

    const bool is_kelvin_mode = (light_insp_mode() == 1);
    std::string light_hdr = light_insp_name();
    if (light_hdr.empty()) light_hdr = "LIGHT";
    else light_hdr = to_ascii_upper(light_hdr);
    std::string fit_lh = fit_text_to_width_font(light_hdr, safe_span(ly.hdr_y, 20), font_small);
    render_page_header(it, cx, fit_lh.c_str(), font_small);

    const int seg_h = std::max(28, dp_px(30));
    const int seg_cy = ly.sub_hdr_y + dp_px(8);
    const int seg_w = std::min({ly.max_content_w(300), ly.band_safe_span(seg_cy, seg_h, dp_px(14))});
    ui::segmented_toggle(it, cx, seg_cy, seg_w, seg_h,
                         "Color Wheel", "White Temp", is_kelvin_mode,
                         font_small, theme_primary, col_white, col_slate,
                         is_kelvin_mode ? WidgetId::LIGHT_COLOR_BTN : WidgetId::NONE,
                         !is_kelvin_mode ? WidgetId::LIGHT_COLOR_BTN : WidgetId::NONE);

    if (!is_kelvin_mode) {
      ui::hsv_color_wheel(it, ui::area_color_wheel_geom(), light_insp_hue_deg(), light_insp_sat(),
                          col_bg, col_white, WidgetId::COLOR_WHEEL_RING);
    } else {
      const int sl_card_w = ly.light_slider_card_w();
      const int k_bar_w   = ly.light_kelvin_bar_w();
      const int k_bar_x0  = ly.light_kelvin_bar_x0();
      const int top_y = seg_cy + seg_h / 2 + dp_px(16);
      const int bot_y = ly.back_y - dp_px(24);
      const int avail_h = std::max(100, bot_y - top_y);
      const int gap = dp_px(16);
      const int k_h = std::clamp((avail_h - gap) / 2, 46, dp_px(68));
      const int b_h = std::clamp((avail_h - gap) / 2, 44, dp_px(64));
      const int k_cy = top_y + (avail_h - (k_h + gap + b_h)) / 2 + k_h / 2;
      const int b_cy = k_cy + k_h / 2 + gap + b_h / 2;
      render_card_box(it, cx - sl_card_w / 2, k_cy - k_h / 2, sl_card_w, k_h,
                      card_bg_on, theme_primary, kRadiusCardPx());
      frame_hits().add_centered_rect(cx, k_cy, sl_card_w, k_h,
                                     WidgetId::LIGHT_TEMP_SLIDER, kHitTap | kHitHorizSlider | kHitFocusSlot);
      const int bar_h = std::max(12, dp_px(16));
      const int bar_y = k_cy + dp_px(6);
      for (int b = 0; b < 24; b++) {
        const float t = static_cast<float>(b) / 23.0f;
        const Color band_c = lerp_color(kChromeWarmKelvin, kChromeCoolKelvin, t);
        const int bx0 = k_bar_x0 + (b * k_bar_w) / 24;
        const int bx1 = k_bar_x0 + ((b + 1) * k_bar_w) / 24;
        fill_rect_row_major(it, bx0, bar_y, std::max(2, bx1 - bx0), bar_h, band_c);
      }
      const int k_clamped = std::max(kLightKelvinMin, std::min(kLightKelvinMax, light_insp_kelvin()));
      const int k_knob_x =
          k_bar_x0 + ((k_clamped - kLightKelvinMin) * k_bar_w) / (kLightKelvinMax - kLightKelvinMin);
      const int knob_r = std::max(8, dp_px(10));
      fast_filled_circle(it, k_knob_x, bar_y + bar_h / 2, knob_r, col_white);
      fast_circle(it, k_knob_x, bar_y + bar_h / 2, knob_r + 1, col_black);
      char k_buf[48];
      std::snprintf(k_buf, sizeof(k_buf), "White Temp: %dK", k_clamped);
      ui::shadow_print(it, cx, k_cy - dp_px(12), font_small, col_white, TextAlign::CENTER, k_buf);

      const int b_clamped = std::max(5, std::min(100, light_insp_bri_pct()));
      char b_buf[48];
      std::snprintf(b_buf, sizeof(b_buf), "Brightness: %d%%", b_clamped);
      ui::slider_card_row(it, cx, b_cy, sl_card_w, b_h,
                          b_buf, b_clamped, 0, 100, true,
                          kChromeLightFill, card_brd_off,
                          font_small, col_white, WidgetId::LIGHT_BRI_SLIDER);
    }

    render_back_button(it, cx, font_small, ly.back_y, "< Back");
    return;
  }

  // ===========================================================================
  // SUBPAGE 1 / AREAS MODE: 5-SLOT AREA SWITCHER
  // ===========================================================================
  if (area_subpage == 1 || area_browse_mode == "areas") {
    render_page_header(it, cx, "SELECT AREA", font_small);

    const auto *list_slots = rs.slots_for(latter_rpc::SlotChannel::AREA_LIST);
    const int list_page = rs.slot_page[static_cast<size_t>(latter_rpc::SlotChannel::AREA_LIST)];
    const int list_total_pages = rs.slot_total_pages[static_cast<size_t>(latter_rpc::SlotChannel::AREA_LIST)];
    const int bounce_y = edge_bounce().offset_px(
        PageId::AREA, GestureAxisLock::Vertical, now_ms, LATTER_PANEL_PPI);

    const int per_pg = std::clamp(layout().list_rows, 4, kMaxListSlots);
    for (int i = 0; i < per_pg; i++) {
      const auto &s = list_slots[i];
      if (s.name.empty() && s.id.empty()) continue;
      const int y_mid = layout().list_row_y(i) + bounce_y;
      const bool is_act = s.active || (!s.id.empty() && s.id == rs.area.active_area_id);
      const Color brd_c = is_act ? theme_primary : card_brd_off;
      const Color txt_c = is_act ? theme_primary : col_white;
      const int area_l = layout().list_card_left;
      const int area_w = layout().list_card_w;
      const int area_h = layout().list_h;
      render_card_box(it, area_l, y_mid - area_h / 2, area_w, area_h,
                      is_act ? card_bg_on : card_bg_off, brd_c, kRadiusCardPx());
      frame_hits().add_centered_rect(cx, y_mid, area_w, area_h,
                                     static_cast<uint16_t>(WidgetId::LIST_SLOT_BASE + i),
                                     kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
      const int dot_x = area_l + dp_px(19);
      if (is_act) {
        fast_filled_circle_aa(it, dot_x, y_mid, std::max(4, dp_px(6)), theme_primary);
      } else {
        fast_circle_aa(it, dot_x, y_mid, static_cast<float>(std::max(4, dp_px(5))), col_slate, 1.5f);
      }
      std::string fit_n = fit_text_to_width_font(s.name, std::max(48, area_w - dp_px(54)), font_small);
      ui::shadow_print(it, area_l + dp_px(37), y_mid, font_small, txt_c, TextAlign::CENTER_LEFT, fit_n.c_str());
    }

    if (list_total_pages > 1) {
      render_vertical_page_pips(it, list_page > 1, list_page < list_total_pages, col_bg, col_white);
    }

    const bool hide_picker_back = rs.area.active_area_id.empty() &&
                                  !persisted_prefs().home_screen_enabled &&
                                  is_on_anchor_root(rs, persisted_prefs());
    if (!hide_picker_back) {
      render_back_button(it, cx, font_small, layout().back_y, "< Back");
    }
    return;
  }

  // ===========================================================================
  // SUBPAGE 0: MAIN AREA CONTROL (HEADER + CLIMATE + 3 TALL SLIDER CARDS + DOCK)
  // ===========================================================================
  const auto ly = layout();
  const std::string hdr = active_area_name.empty() ? std::string("AREA CONTROL") : to_ascii_upper(active_area_name);
  std::string fit_hdr = fit_text_to_width_font(hdr, std::min(dp_px(250), ly.safe_span(ly.hdr_y, dp_px(18))), font_small);
  render_page_header(it, cx, fit_hdr.c_str(), font_small);
  const int hdr_hit_w = std::min(ly.safe_span(ly.hdr_y, dp_px(12)),
                                 std::max(dp_px(240), ly.min_hit_px() * 2));
  const int hdr_hit_h = dp_px(36);
  const int hdr_hit_pad = std::max(0, (ly.min_hit_px() - hdr_hit_h + 1) / 2);
  frame_hits().add_centered_rect(cx, ly.hdr_y, hdr_hit_w, hdr_hit_h,
                                 WidgetId::AREA_PICKER_PILL, kHitTap | kHitFocusSlot, 0, hdr_hit_pad);

  if (is_fb) {
    std::string fit_fb = fit_text_to_width_font(area_action_feedback, std::min(dp_px(280), ly.safe_span(ly.sub_hdr_y, dp_px(18))), font_small);
    render_sub_header(it, cx, fit_fb.c_str(), font_small, theme_secondary);
  } else if (!std::isnan(area_temp) || !std::isnan(area_humidity)) {
    const int tele_y = ly.sub_hdr_y;
    const Color icon_c = kChromeTeleIce;
    const bool tele_sh = backdrop_active();
    const int tr = std::max(6, dp_px(8));
    const int icon_slot = 2 * tr;
    const int txt_pad = dp_px(5);
    const int pair_gap = dp_px(22);

    char t_str[16] = "", h_str[16] = "";
    int w_t = 0, w_h = 0, x_off = 0, base = 0, th = 0;
    const bool has_t = !std::isnan(area_temp);
    const bool has_h = !std::isnan(area_humidity);
    if (has_t) {
      std::snprintf(t_str, sizeof(t_str), "%.0f°", area_temp);
      font_small->measure(t_str, &w_t, &x_off, &base, &th);
    }
    if (has_h) {
      std::snprintf(h_str, sizeof(h_str), "%.0f%%", area_humidity);
      font_small->measure(h_str, &w_h, &x_off, &base, &th);
    }

    if (has_t && has_h) {
      const int total_w = icon_slot + txt_pad + w_t + pair_gap + icon_slot + txt_pad + w_h;
      const int left_x = cx - (total_w / 2);
      ui::draw_icon_thermometer(it, left_x + tr, tele_y, tr, icon_c, tele_sh);
      ui::shadow_print(it, left_x + icon_slot + txt_pad, tele_y, font_small, theme_secondary, TextAlign::CENTER_LEFT, t_str);
      const int drop_x = left_x + icon_slot + txt_pad + w_t + pair_gap;
      ui::draw_icon_droplet(it, drop_x + tr, tele_y, tr, icon_c, tele_sh);
      ui::shadow_print(it, drop_x + icon_slot + txt_pad, tele_y, font_small, theme_secondary, TextAlign::CENTER_LEFT, h_str);
    } else if (has_t) {
      const int total_w = icon_slot + txt_pad + w_t;
      const int left_x = cx - (total_w / 2);
      ui::draw_icon_thermometer(it, left_x + tr, tele_y, tr, icon_c, tele_sh);
      ui::shadow_print(it, left_x + icon_slot + txt_pad, tele_y, font_small, theme_secondary, TextAlign::CENTER_LEFT, t_str);
    } else {
      const int total_w = icon_slot + txt_pad + w_h;
      const int left_x = cx - (total_w / 2);
      ui::draw_icon_droplet(it, left_x + tr, tele_y, tr, icon_c, tele_sh);
      ui::shadow_print(it, left_x + icon_slot + txt_pad, tele_y, font_small, theme_secondary, TextAlign::CENTER_LEFT, h_str);
    }
  } else {
    const char *sub_lbl = (area_domain_filter == "scene")   ? "Lighting Scenes"
                        : (area_domain_filter == "light")   ? "Lights"
                        : (area_domain_filter == "cover")   ? "Blinds & Curtains"
                        : (area_domain_filter == "fan")     ? "Fans"
                        : (area_domain_filter == "switch")  ? "Switches & Outlets"
                        : (area_domain_filter == "climate") ? "Climate & Thermostats"
                        : (area_domain_filter == "media")   ? "Area Media"
                        : "Tap Header to Switch Area";
    render_sub_header(it, cx, sub_lbl, font_small, col_slate);
  }

  const int card_ir = ui::card_icon_r(layout().area_card3_h);
  auto draw_tall_card_icon = [&](int gx, int gy, const std::string &dtype,
                                 const std::string &name, const std::string &id_str,
                                 bool is_on, Color fg_c) {
    if (dtype == "light") {
      ui::draw_icon_lightbulb(it, gx, gy, card_ir, is_on ? col_gold : fg_c, true, is_on);
    } else if (dtype == "cover") {
      const Color cov_c = is_on ? theme_primary : fg_c;
      if (is_curtain_entity(name, id_str)) {
        render_mdi_icon(it, is_on ? "mdi:curtains" : "mdi:curtains-closed",
                        gx, gy, std::max(16, 2 * card_ir), cov_c, true, "mdi:blinds-horizontal");
      } else {
        ui::draw_icon_blinds_slats(it, gx, gy, card_ir, cov_c, is_on, true);
      }
    } else if (dtype == "fan") {
      const Color fan_c = is_on ? kChromeFanAccent : fg_c;
      ui::draw_icon_ceiling_fan(it, gx, gy, card_ir, fan_c, true);
    } else if (dtype == "scene") {
      ui::draw_icon_sparkle(it, gx, gy, card_ir, col_gold, true);
    } else if (dtype == "climate") {
      ui::draw_icon_thermometer(it, gx, gy, card_ir, is_on ? col_gold : fg_c, true);
    } else if (dtype == "media_player") {
      ui::draw_icon_eighth_notes(it, gx, gy, card_ir, theme_primary, true);
    } else {
      const Color sw_c = is_on ? theme_primary : fg_c;
      ui::draw_icon_switch_pill(it, gx, gy, card_ir, sw_c, is_on, true);
    }
  };

  const auto *ent_slots = rs.slots_for(latter_rpc::SlotChannel::AREA_ENTITIES);
  const int browse_page = rs.slot_page[static_cast<size_t>(latter_rpc::SlotChannel::AREA_ENTITIES)];
  const int browse_total_pages = rs.slot_total_pages[static_cast<size_t>(latter_rpc::SlotChannel::AREA_ENTITIES)];
  const int card_slots = static_cast<int>(std::min<size_t>(DeviceRuntimeState::kCardSlotsPerPage, 3));
  const int card_bounce_y = edge_bounce().offset_px(
      PageId::AREA, GestureAxisLock::Vertical, now_ms, LATTER_PANEL_PPI);
  bool any_card = false;

  for (int idx = 0; idx < card_slots; idx++) {
    const auto &s = ent_slots[idx];
    const std::string &raw_name = s.name;
    const std::string &id_str = s.id;
    const std::string &dtype = s.type;
    const bool is_act = s.active;
    const int level = s.level;
    const int y_mid = layout().area_card3_y(idx) + card_bounce_y;

    if (raw_name.empty() && id_str.empty()) continue;
    any_card = true;

    if (dtype == "text" || id_str.empty()) {
      // D-048 / D-070: Non-interactive placeholder renders bare shadowed text (zero glass).
      std::string fit_msg = fit_text_to_width_font(raw_name, std::min(ly.max_content_w(), safe_span(y_mid, 24)), font_small);
      ui::shadow_print(it, cx, y_mid, font_small, col_slate, TextAlign::CENTER, fit_msg.c_str());
      continue;
    }

    const bool has_slider = (dtype == "light" || dtype == "cover" || dtype == "fan") && (level >= 0);
    const Color brd_c = is_act ? theme_primary : card_brd_off;
    const Color bg_c  = is_act ? card_bg_on    : card_bg_off;

    Color fill_c = scale_color(theme_primary, 0.48f);
    if (dtype == "light") fill_c = kChromeLightFill;
    else if (dtype == "cover") fill_c = kChromeCoverFill;
    else if (dtype == "fan") fill_c = kChromeFanFill;

    const int r_card_w   = ly.area_card3_w;
    const int r_card_h   = ly.area_card3_h;
    const int r_left     = ly.area_card_left();
    const int r_right    = ly.area_card_right();
    const int r_div_x    = ly.area_card_div_x();
    const int r_slider_w = (r_right - 5) - (r_div_x + 1);
    render_card_box(it, r_left, y_mid - r_card_h / 2, r_card_w, r_card_h, bg_c, brd_c, kRadiusCardPx());

    if (has_slider && is_act && level > 0 && r_slider_w > 10) {
      const int clamped_lvl = std::max(0, std::min(100, level));
      const int fill_w = (r_slider_w * clamped_lvl) / 100;
      if (fill_w > 4) {
        const int fh = r_card_h - 6;
        fill_round_rect_glass(it, r_div_x + 1, y_mid - fh / 2, fill_w, fh, 0.0f, fill_c, 152, 132);
        fast_vline(it, r_div_x + fill_w, y_mid - fh / 2 + 1, fh - 2, scale_color(col_white, 0.35f));
      }
    }

    draw_tall_card_icon((r_left + r_div_x) / 2, y_mid, dtype, raw_name, id_str, is_act, is_act ? col_white : col_slate);
    const int div_h = std::min(r_card_h / 2 - 4, dp_px(22));
    fast_line(it, r_div_x, y_mid - div_h, r_div_x, y_mid + div_h, brd_c);

    std::string clean_name = strip_trailing_paren_state(raw_name);
    const int max_t_w = (dtype == "light")
        ? (r_slider_w - dp_px(58))
        : ((has_slider || dtype == "climate") ? (r_slider_w - dp_px(28)) : (r_slider_w - dp_px(20)));
    std::string fit_title = fit_text_to_width_font(clean_name, std::max(80, max_t_w), font_small);
    ui::shadow_print(it, r_div_x + dp_px(13), y_mid - std::min(r_card_h / 4, dp_px(11)), font_small, is_act ? col_white : col_slate, TextAlign::CENTER_LEFT, fit_title.c_str());

    char sub_buf[40];
    if (dtype == "light") {
      if (!is_act) std::snprintf(sub_buf, sizeof(sub_buf), "Off");
      else if (level > 0) std::snprintf(sub_buf, sizeof(sub_buf), "On  •  %d%%", level);
      else std::snprintf(sub_buf, sizeof(sub_buf), "On");
    } else if (dtype == "cover") {
      const bool is_curt = is_curtain_entity(raw_name, id_str);
      const char *kind_s = is_curt ? "Curtains" : "Blinds";
      if (!is_act || level == 0) std::snprintf(sub_buf, sizeof(sub_buf), "%s  •  Closed", kind_s);
      else if (level >= 99) std::snprintf(sub_buf, sizeof(sub_buf), "%s  •  Open", kind_s);
      else std::snprintf(sub_buf, sizeof(sub_buf), "%s  •  %d%% Open", kind_s, level);
    } else if (dtype == "fan") {
      if (!is_act || level == 0) std::snprintf(sub_buf, sizeof(sub_buf), "Off");
      else std::snprintf(sub_buf, sizeof(sub_buf), "Running  •  %d%%", level);
    } else if (dtype == "climate") {
      if (const ThermostatZoneItem *ci = find_climate_item_by_id_const(id_str)) {
        const bool c_off = eq_ascii_ci_sv(ci->mode, "off");
        const char *m_s = eq_ascii_ci_sv(ci->mode, "heat") ? "Heating"
                        : (eq_ascii_ci_sv(ci->mode, "cool") ? "Cooling"
                        : ((eq_ascii_ci_sv(ci->mode, "auto") || eq_ascii_ci_sv(ci->mode, "heat_cool")) ? "Auto"
                        : (eq_ascii_ci_sv(ci->mode, "fan_only") ? "Fan"
                        : (eq_ascii_ci_sv(ci->mode, "dry") ? "Dry"
                        : (c_off ? "Off" : (is_act ? "Active" : "Idle"))))));
        if (!c_off && ci->target_temp > 0) {
          std::snprintf(sub_buf, sizeof(sub_buf), "%s  •  %d°", m_s, ci->target_temp);
        } else if (!std::isnan(ci->current_temp) && ci->current_temp > -40.0f && ci->current_temp < 150.0f) {
          std::snprintf(sub_buf, sizeof(sub_buf), "%s  •  %.0f°", m_s, ci->current_temp);
        } else {
          std::snprintf(sub_buf, sizeof(sub_buf), "%s", m_s);
        }
      } else if (level > 0) {
        std::snprintf(sub_buf, sizeof(sub_buf), "%s  •  %d°", is_act ? "Active" : "Idle", level);
      } else {
        std::snprintf(sub_buf, sizeof(sub_buf), is_act ? "Active" : "Off");
      }
    } else if (dtype == "scene") {
      std::snprintf(sub_buf, sizeof(sub_buf), "Tap to Activate");
    } else if (dtype == "media_player") {
      std::snprintf(sub_buf, sizeof(sub_buf), is_act ? "Playing  •  Open Music" : "Idle  •  Open Music");
    } else {
      std::snprintf(sub_buf, sizeof(sub_buf), is_act ? "On" : "Off");
    }
    const Color sub_c = is_act ? theme_secondary : kChromeMuted;
    ui::shadow_print(it, r_div_x + dp_px(13), y_mid + std::min(r_card_h / 4 + dp_px(2), dp_px(14)), font_small, sub_c, TextAlign::CENTER_LEFT, sub_buf);

    const int card_max_pad = std::max(0, (ly.area_card3_stride - r_card_h - 1) / 2);
    const int card_hit_pad = std::min(card_max_pad, std::max(0, (ly.min_hit_px() - r_card_h + 1) / 2));
    frame_hits().add_rect(r_left, y_mid - r_card_h / 2, r_div_x - r_left, r_card_h,
                          static_cast<uint16_t>(WidgetId::AREA_CARD_TOGGLE + idx),
                          kHitTap | kHitFocusSlot, static_cast<int16_t>(idx), card_hit_pad);

    int right_limit = r_right;
    if (dtype == "light") {
      const int ox = ly.area_card_orb_x();
      const int orb_r = std::min(r_card_h / 2 - 4, dp_px(16));
      const int dot_r = std::max(3, dp_px(4));
      right_limit = ox - orb_r - 4;
      render_glass_circle(it, ox, y_mid, orb_r, kChromeGlassTint, is_act ? theme_primary : card_brd_off);
      fast_filled_circle_aa(it, ox - dp_px(5), y_mid - dp_px(4), dot_r, kChromeRgbRed);
      fast_filled_circle_aa(it, ox + dp_px(5), y_mid - dp_px(4), dot_r, kChromeRgbGreen);
      fast_filled_circle_aa(it, ox,             y_mid + dp_px(5), dot_r, kChromeRgbBlue);
      const int orb_min_r = (ly.min_hit_px() + 1) / 2;
      const int orb_pad = std::max(dp_px(14), orb_min_r - orb_r);
      frame_hits().add_circle(ox, y_mid, orb_r,
                              WidgetId::LIGHT_ORB_BTN, kHitTap | kHitFocusCircle,
                              static_cast<int16_t>(idx), orb_pad);
    } else if (has_slider || dtype == "climate") {
      // Subtle right-chevron cue indicating tap-to-inspect Position / Speed (D-050).
      ui::draw_icon_chevron(it, r_right - dp_px(18), y_mid, std::max(5, dp_px(7)),
                            true, is_act ? col_white : col_slate, true);
    }

    // D-050: Overview cards register tap-only bodies (zero kHitHorizSlider) so horizontal swipes
    // anywhere on Area Control switch categories and vertical swipes always page entity slots.
    const int body_w = std::max(8, right_limit - (r_div_x + 1));
    frame_hits().add_rect(r_div_x + 1, y_mid - r_card_h / 2, body_w, r_card_h,
                          static_cast<uint16_t>(WidgetId::AREA_CARD_TAP + idx),
                          kHitTap | kHitFocusSlot, static_cast<int16_t>(idx), card_hit_pad);
  }

  if (!any_card) {
    render_empty_state(it, cx, layout().area_card3_y(1), "No controls in this Area", font_small, col_slate);
  }

  if (browse_total_pages > 1) {
    render_vertical_page_pips(it, browse_page > 1, browse_page < browse_total_pages,
                              col_bg, col_white, layout().mode_bar_zone_top());
  }

  render_area_bottom_dock(area_domain_filter);
}

}  // namespace latter_ui
