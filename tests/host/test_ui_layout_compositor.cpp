#include "test_harness.h"

namespace latter_test {

namespace {

struct HostRgb {
  uint8_t r{0};
  uint8_t g{0};
  uint8_t b{0};
};

enum class HostGlassVariant : uint8_t {
  Clear = 0,
  ReadableDark = 1,
};

float host_srgb_chan(uint8_t v) {
  const float x = static_cast<float>(v) * (1.0f / 255.0f);
  return (x <= 0.04045f) ? (x * (1.0f / 12.92f)) : std::pow((x + 0.055f) * (1.0f / 1.055f), 2.4f);
}

float host_rel_lum(HostRgb c) {
  return 0.2126f * host_srgb_chan(c.r) + 0.7152f * host_srgb_chan(c.g) + 0.0722f * host_srgb_chan(c.b);
}

float host_contrast(float la, float lb) {
  return (la > lb) ? ((la + 0.05f) / (lb + 0.05f)) : ((lb + 0.05f) / (la + 0.05f));
}

HostRgb host_mix_q8(HostRgb under, HostRgb over, int a) {
  a = std::clamp(a, 0, 256);
  const int ia = 256 - a;
  return {
      static_cast<uint8_t>((static_cast<int>(over.r) * a + static_cast<int>(under.r) * ia + 128) >> 8),
      static_cast<uint8_t>((static_cast<int>(over.g) * a + static_cast<int>(under.g) * ia + 128) >> 8),
      static_cast<uint8_t>((static_cast<int>(over.b) * a + static_cast<int>(under.b) * ia + 128) >> 8),
  };
}

int host_glass_alpha_for_text(HostRgb tint, HostRgb backdrop, HostRgb text, int min_alpha_q8) {
  const float lt = host_rel_lum(text);
  const int start_a = std::clamp(min_alpha_q8, 0, 256);
  for (int a = start_a; a < 256; a += 8) {
    if (host_contrast(lt, host_rel_lum(host_mix_q8(backdrop, tint, a))) >= 5.0f) return a;
  }
  return 256;
}

int host_glass_alpha_smooth(int &state, int target, uint32_t dt_ms) {
  if (state <= 0 || target >= state) {
    state = target;
  } else {
    const uint32_t clamped_dt = std::min<uint32_t>(dt_ms, 250U);
    const int fall = (clamped_dt == 0U)
                         ? 0
                         : std::max(1, static_cast<int>((4U * clamped_dt + 8U) / 16U));
    state = std::max(target, state - fall);
  }
  return state;
}

void host_resolve_glass_fill(bool bd_valid, HostRgb bd_bright, HostRgb bd_mean,
                             HostRgb tint, HostRgb ref_text, HostGlassVariant variant,
                             HostRgb &out_tint, int &out_a_top, int &out_a_bot) {
  constexpr HostRgb kHomePill{175, 190, 210};
  constexpr HostRgb kWhite{255, 255, 255};
  constexpr int kAlphaClearBase = 104;
  constexpr int kAlphaReadableMin = 208;

  const bool dark_tint = (std::max({tint.r, tint.g, tint.b}) <= 160);
  const bool is_readable = (variant == HostGlassVariant::ReadableDark);

  if (!is_readable) {
    const int mx = std::max({static_cast<int>(tint.r), static_cast<int>(tint.g), static_cast<int>(tint.b)});
    const int mn = std::min({static_cast<int>(tint.r), static_cast<int>(tint.g), static_cast<int>(tint.b)});
    const int chroma = mx - mn;
    const bool sem_tint = (chroma > 12) &&
                          !(tint.r == kHomePill.r && tint.g == kHomePill.g && tint.b == kHomePill.b) &&
                          !(tint.r <= tint.g && tint.g <= tint.b && mx <= 96 && chroma <= 30);
    const int frost_q8 = dark_tint ? (sem_tint ? 32 : 54) : 0;
    out_tint = (frost_q8 > 0) ? host_mix_q8(tint, kHomePill, frost_q8) : tint;
    const int da = dark_tint ? 12 : 0;
    out_a_top = std::min(256, kAlphaClearBase + da);
    out_a_bot = std::max(0, kAlphaClearBase - (da * 2) / 3);
    return;
  }

  (void)bd_mean;
  out_tint = tint;
  const HostRgb contrast_fg = ((static_cast<int>(ref_text.r) + ref_text.g + ref_text.b) < 48) ? kWhite : ref_text;
  const float fg_lum = host_rel_lum(contrast_fg);

  const int base_a = bd_valid
      ? host_glass_alpha_for_text(out_tint, bd_bright, contrast_fg, kAlphaReadableMin)
      : kAlphaReadableMin;
  const int da = 8;
  out_a_top = std::min(256, base_a + da);
  out_a_bot = std::max(200, base_a - (da * 2) / 3);
  if (bd_valid && host_contrast(fg_lum, host_rel_lum(host_mix_q8(bd_bright, out_tint, out_a_bot))) < 4.5f) {
    out_a_bot = base_a;
  }
}

void test_ui_layout_hit_table_and_dirty_band() {
  const auto ref = latter_ui::compute_panel_layout(466, 466, true, false, 266);
  LATTER_CHECK(ref.drag_start == 7 && ref.tap_max == 17 && ref.swipe_min == 25);
  LATTER_CHECK(ref.dock_hit_r >= 43 && ref.back_y == 426 && ref.home_bar_y == 434);
  LATTER_CHECK(latter_ui::scale_x_for(233, 466) == 233);
  LATTER_CHECK(latter_ui::scale_y_for(233, 466) == 233);
  LATTER_CHECK(latter_ui::scale_s_for(100, 466) == 100);
  LATTER_CHECK(latter_ui::scale_min_for(100, 466) == 100);
  LATTER_CHECK(latter_ui::scale_w_for(276, 466) == 276);

  for (const auto &p : kFlagshipPanels) {
    const auto ly = latter_ui::compute_panel_layout(p.w, p.h, p.round, p.wearable, p.ppi);
    const int min_dim = (p.w < p.h) ? p.w : p.h;
    LATTER_CHECK(ly.drag_start >= 3 && ly.tap_max > ly.drag_start && ly.swipe_min > ly.tap_max);
    LATTER_CHECK(ly.dock_hit_r >= latter_ui::mm_to_px_for(3.6f, p.ppi));
    LATTER_CHECK(ly.hdr_y > 0 && ly.hdr_y < ly.sub_hdr_y && ly.sub_hdr_y < ly.dock_y);
    LATTER_CHECK(ly.dock_y < ly.back_y && ly.back_y < ly.home_bar_y && ly.home_bar_y < p.h);
    LATTER_CHECK(ly.list_card_w > 0 && ly.list_card_w <= p.w);
    LATTER_CHECK(ly.area_card3_w > 0 && ly.area_card3_w <= p.w);
    LATTER_CHECK(ly.tool_card3_w > 0 && ly.tool_card3_w <= p.w);
    LATTER_CHECK(latter_ui::scale_w_for(200, p.w) > 0);
    LATTER_CHECK(latter_ui::scale_min_for(200, min_dim) > 0);

    for (int frac : {18, 35, 50, 72, 85}) {
      const int y = (p.h * frac) / 100;
      const int inset = latter_ui::scale_min_for(12, min_dim);
      const int span = latter_ui::safe_span_for(y, p.w, p.h, p.round, p.wearable, inset);
      LATTER_CHECK(span > 0 && span <= p.w - 2 * inset);
      if (p.round) {
        const int r = p.w / 2;
        const int dy = std::abs(y - p.h / 2);
        const int chord = 2 * latter_ui::chord_half_span_constexpr(r, dy, false);
        LATTER_CHECK(span <= std::max(0, chord - 2 * inset));
        if (frac == 50) LATTER_CHECK(latter_ui::chord_half_span_constexpr(r, 0, false) == r);
      }
    }

    if (p.w == 480 && p.h == 480 && !p.round && !p.wearable) {
      constexpr int kMobileCornerR = 76;
      constexpr int kMobilePad = 3;
      constexpr int kMobileCrOut = kMobileCornerR - kMobilePad;
      const double diag_outer_px = static_cast<double>(kMobileCornerR) -
                                   static_cast<double>(kMobileCrOut) * 0.7071067811865475;
      LATTER_CHECK(diag_outer_px >= 24.0 && diag_outer_px <= 25.0);
      const int hdr_span_cr76 = latter_ui::safe_span_for(ly.hdr_y, 480, 480, false, false, 16, kMobileCornerR);
      const int hdr_span_cr20 = latter_ui::safe_span_for(ly.hdr_y, 480, 480, false, false, 16, 20);
      const int mid_span_cr76 = latter_ui::safe_span_for(240, 480, 480, false, false, 16, kMobileCornerR);
      LATTER_CHECK(hdr_span_cr76 < hdr_span_cr20 && hdr_span_cr76 == 412 && mid_span_cr76 == 448);
    }

    latter_ui::FrameHitTable tbl;
    tbl.clear();
    LATTER_CHECK(tbl.count == 0 && tbl.dropped_count == 0U && tbl.focus_count() == 0);

    const int cx = p.w / 2;
    const int card_w = ly.list_card_w;
    const int card_x = cx - card_w / 2;
    const int card_y = ly.sub_hdr_y + 12;
    const int card_h = 44;

    tbl.add_rect(card_x, card_y, card_w, card_h,
                 latter_ui::WidgetId::REMOTE_SLOT_CARD,
                 latter_ui::kHitTap | latter_ui::kHitFocusSlot, 0);
    tbl.add_rect(card_x, card_y, card_w, card_h + 2,
                 latter_ui::WidgetId::REMOTE_SLOT_CARD,
                 latter_ui::kHitTap | latter_ui::kHitFocusSlot, 0);
    LATTER_CHECK(tbl.count == 1 && tbl.find_by_id(latter_ui::WidgetId::REMOTE_SLOT_CARD) != nullptr);

    const int sl_y = card_y + card_h + 16;
    tbl.add_rect(card_x, sl_y, card_w, 24,
                 latter_ui::WidgetId::SYS_TOGGLE_ROW,
                 latter_ui::kHitHorizSlider, 1);
    tbl.add_circle(cx, ly.back_y, 24,
                   latter_ui::WidgetId::REMOTE_PRIMARY_PWR,
                   latter_ui::kHitTap | latter_ui::kHitFocusCircle, 0);
    tbl.add_centered_rect(cx, ly.back_y, 80, 40,
                          latter_ui::WidgetId::PIN_PAD_CANCEL,
                          latter_ui::kHitTap | latter_ui::kHitFocusSlot, 0);

    LATTER_CHECK(tbl.count == 4 && tbl.focus_count() == 3);
    LATTER_CHECK(tbl.focus_at(0)->widget_id == latter_ui::WidgetId::REMOTE_SLOT_CARD);
    LATTER_CHECK(tbl.focus_at(1)->widget_id == latter_ui::WidgetId::REMOTE_PRIMARY_PWR);
    LATTER_CHECK(tbl.focus_at(2)->widget_id == latter_ui::WidgetId::PIN_PAD_CANCEL);
    LATTER_CHECK(tbl.focus_at(-1)->widget_id == latter_ui::WidgetId::PIN_PAD_CANCEL);

    const auto *top_hit = tbl.hit_test(cx, ly.back_y, latter_ui::kHitTap);
    LATTER_CHECK(top_hit != nullptr && top_hit->widget_id == latter_ui::WidgetId::PIN_PAD_CANCEL);

    const auto *drag_hit = tbl.hit_test(cx, sl_y + 12, latter_ui::kHitHorizSlider);
    LATTER_CHECK(drag_hit != nullptr && drag_hit->widget_id == latter_ui::WidgetId::SYS_TOGGLE_ROW);
    LATTER_CHECK(drag_hit->norm_x(card_x - 50) == 0.0f);
    LATTER_CHECK(drag_hit->norm_x(card_x + card_w + 50) == 1.0f);
    LATTER_CHECK(std::fabs(drag_hit->norm_x(card_x + card_w / 2) - 0.5f) < 0.02f);

    int ay0 = -1, ay1 = -1;
    latter_ui::DirtyBandTracker::align_band(85, 210, p.h, ay0, ay1);
    LATTER_CHECK(ay0 == 84 && ay1 == 211 && ((ay1 - ay0 + 1) % 2) == 0);
    latter_ui::DirtyBandTracker::align_band(-15, p.h + 40, p.h, ay0, ay1);
    LATTER_CHECK(ay0 == 0 && ay1 == p.h - 1 && ((ay1 - ay0 + 1) % 2) == 0);
  }

  auto &db = latter_ui::dirty_band();
  db = latter_ui::DirtyBandTracker{};
  const uint32_t sig_idle = latter_ui::compute_takeover_signature(false, false, true, 0, false, false, false, false);
  const uint32_t sig_va   = latter_ui::compute_takeover_signature(false, false, true, 0, false, true,  false, false);
  LATTER_CHECK(sig_idle != sig_va);

  const int home_sub_dock0 = latter_ui::compute_subpage_id(1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0);
  const int home_sub_dock1 = latter_ui::compute_subpage_id(1, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0);
  LATTER_CHECK(home_sub_dock0 != home_sub_dock1);

  db.begin_frame(1, home_sub_dock0, sig_idle, false);
  LATTER_CHECK(db.is_full() && db.y0() == 0 && db.y1() == latter_ui::SCREEN_H - 1);

  latter_ui::mark_dirty_band(13, 60);
  latter_ui::mark_dirty_band(180, 232);
  db.begin_frame(1, home_sub_dock0, sig_idle, false);
  LATTER_CHECK(!db.is_full() && db.y0() == 12 && db.y1() == 233);

  latter_ui::mark_dirty_band(40, 80);
  db.begin_frame(1, home_sub_dock1, sig_idle, false);
  LATTER_CHECK(db.is_full());

  latter_ui::mark_dirty_band(40, 80);
  db.begin_frame(1, home_sub_dock1, sig_va, false);
  LATTER_CHECK(db.is_full());

  static_assert(sizeof(latter_ui::HitRegion) == 14, "HitRegion must stay 14 bytes");
  latter_ui::FrameHitTable pad_ht{};
  pad_ht.add_circle(638, 216, 28, latter_ui::WidgetId::MUSIC_VOL_SLIDER,
                    latter_ui::kHitTap | latter_ui::kHitFocusSlot | latter_ui::kHitFocusCircle, 1, 40);
  pad_ht.add_circle(540, 216, 34, latter_ui::WidgetId::MUSIC_NEXT_BTN,
                    latter_ui::kHitTap | latter_ui::kHitFocusSlot | latter_ui::kHitFocusCircle, 0, 28);
  LATTER_CHECK(pad_ht.hit_test(638, 216 - 52, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::MUSIC_VOL_SLIDER);
  LATTER_CHECK(pad_ht.hit_test(540 + 30, 216, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::MUSIC_NEXT_BTN);
  LATTER_CHECK(pad_ht.hit_test(602, 216, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::MUSIC_VOL_SLIDER);
}

void test_phase2_glass_and_scissor_compositor() {
  auto &db = latter_ui::dirty_band();
  db = latter_ui::DirtyBandTracker{};
  const uint32_t sig_idle = latter_ui::compute_takeover_signature(false, false, true, 0, false, false, false, false);
  const uint32_t sig_notif = latter_ui::compute_takeover_signature(false, false, true, 0, false, false, false, true);
  const int home_sub = latter_ui::compute_subpage_id(1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0);

  db.begin_frame(1, home_sub, sig_idle, false);
  LATTER_CHECK(db.is_full() && db.y0() == 0 && db.y1() == latter_ui::SCREEN_H - 1);

  latter_ui::mark_dirty_band(93, 168);
  db.begin_frame(1, home_sub, sig_idle, false);
  LATTER_CHECK(!db.is_full() && db.y0() == 92 && db.y1() == 169);

  db.begin_frame(1, home_sub, sig_idle, false);
  LATTER_CHECK(db.is_full());

  latter_ui::mark_dirty_band(93, 168);
  db.begin_frame(1, home_sub, sig_idle, true);
  LATTER_CHECK(db.is_full());

  latter_ui::mark_dirty_band(93, 168);
  latter_ui::mark_dirty_full();
  db.begin_frame(1, home_sub, sig_idle, false);
  LATTER_CHECK(db.is_full());

  latter_ui::mark_dirty_band(93, 168);
  db.begin_frame(0, 0, sig_notif, false);
  LATTER_CHECK(db.is_full());

  db = latter_ui::DirtyBandTracker{};
  LATTER_CHECK(db.begin_frame(1, home_sub, sig_idle, false, 720, 85U, 174, false));
  latter_ui::frame_hits().add_centered_rect(
      latter_ui::SCREEN_CX, latter_ui::layout().dock_y, 200, 48,
      latter_ui::WidgetId::DOCK_SLOT_BASE, latter_ui::kHitTap | latter_ui::kHitFocusSlot, 0);
  LATTER_CHECK(!db.begin_frame(1, home_sub, sig_idle, false, 720, 85U, 174, false));
  LATTER_CHECK(db.is_idle_skip() && latter_ui::frame_hits().count == 1);
  LATTER_CHECK(db.begin_frame(1, home_sub, sig_idle, false, 721, 85U, 174, false));
  LATTER_CHECK(!db.is_full() && db.band_y0() == 0 && db.band_y1() == 175);

  const int home_overlay_sub = latter_ui::compute_subpage_id(1, 2, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0);
  LATTER_CHECK(db.begin_frame(1, home_overlay_sub, sig_idle, false, 721, 85U, 174, false));
  LATTER_CHECK(db.is_full());
  LATTER_CHECK(!db.begin_frame(1, home_overlay_sub, sig_idle, false, 721, 85U, 174, false));
  LATTER_CHECK(db.begin_frame(1, home_overlay_sub, sig_idle, false, 722, 85U, 174, false));
  LATTER_CHECK(db.is_full());

  db.begin_frame(1, home_sub, sig_idle, false);
  latter_ui::mark_dirty_band(92, 169);
  db.begin_frame(1, home_sub, sig_idle, false);
  auto &ht = latter_ui::frame_hits();
  const int cx = latter_ui::SCREEN_CX;
  const int dock_y = latter_ui::layout().dock_y;
  const int back_y = latter_ui::layout().back_y;
  ht.add_centered_rect(cx, 42, 180, 36, latter_ui::WidgetId::REMOTE_SLOT_PILL,
                       latter_ui::kHitTap | latter_ui::kHitFocusSlot, 0);
  ht.add_centered_rect(cx, 130, 200, 64, latter_ui::WidgetId::GAMES_HERO_CARD,
                       latter_ui::kHitTap, 0);
  ht.add_centered_rect(cx, 260, 240, 56, latter_ui::WidgetId::LIST_SLOT_BASE,
                       latter_ui::kHitTap | latter_ui::kHitFocusSlot, 0);
  ht.add_circle(cx - 40, dock_y, 24, latter_ui::WidgetId::DOCK_SLOT_BASE,
                latter_ui::kHitTap | latter_ui::kHitFocusSlot | latter_ui::kHitFocusCircle, 0, 14);
  ht.add_centered_rect(cx, back_y, 120, 28, latter_ui::WidgetId::BACK_BUTTON,
                       latter_ui::kHitTap | latter_ui::kHitFocusSlot, 0);
  LATTER_CHECK(ht.count == 5 && ht.focus_count() == 4);
  LATTER_CHECK(ht.hit_test(cx, 42, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::REMOTE_SLOT_PILL);
  LATTER_CHECK(ht.hit_test(cx, 130, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::GAMES_HERO_CARD);
  LATTER_CHECK(ht.hit_test(cx, 260, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::LIST_SLOT_BASE);
  LATTER_CHECK(ht.hit_test(cx, back_y, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::BACK_BUTTON);

  constexpr HostRgb kClearTint{22, 27, 36};
  constexpr HostRgb kReadableTint{16, 20, 28};
  constexpr HostRgb kWhiteText{255, 255, 255};
  constexpr HostRgb kMutedText{158, 158, 158};
  constexpr HostRgb kBackdrops[5] = {
      {6, 8, 12}, {34, 42, 56}, {92, 126, 168}, {156, 210, 252}, {250, 252, 255},
  };

  HostRgb ref_clear_tint{};
  int ref_clear_top = 0, ref_clear_bot = 0;
  host_resolve_glass_fill(false, HostRgb{}, HostRgb{}, kClearTint, kWhiteText,
                          HostGlassVariant::Clear, ref_clear_tint, ref_clear_top, ref_clear_bot);
  LATTER_CHECK(ref_clear_top == 116 && ref_clear_bot == 96);

  for (const auto &bd : kBackdrops) {
    for (HostRgb fg : {kWhiteText, kMutedText}) {
      for (HostGlassVariant var : {HostGlassVariant::Clear, HostGlassVariant::ReadableDark}) {
        const HostRgb base_tint = (var == HostGlassVariant::ReadableDark) ? kReadableTint : kClearTint;
        HostRgb eff_tint{};
        int a_top = 0, a_bot = 0;
        host_resolve_glass_fill(true, bd, bd, base_tint, fg, var, eff_tint, a_top, a_bot);
        if (var == HostGlassVariant::Clear) {
          LATTER_CHECK(a_top == 116 && a_bot == 96);
        } else {
          const float fg_lum = host_rel_lum(fg);
          LATTER_CHECK(host_contrast(fg_lum, host_rel_lum(host_mix_q8(bd, eff_tint, a_top))) >= 4.5f);
          LATTER_CHECK(host_contrast(fg_lum, host_rel_lum(host_mix_q8(bd, eff_tint, a_bot))) >= 4.5f);
        }
      }
    }
  }

  int alpha_st = 0;
  LATTER_CHECK(host_glass_alpha_smooth(alpha_st, 220, 16U) == 220);
  LATTER_CHECK(host_glass_alpha_smooth(alpha_st, 240, 16U) == 240);
  LATTER_CHECK(host_glass_alpha_smooth(alpha_st, 160, 16U) == 236);
  LATTER_CHECK(host_glass_alpha_smooth(alpha_st, 160, 32U) == 228);

  const std::string ui_color_types = read_repo_files({
      "../../include/ui_color.h",
      "../../include/ui_types.h",
  });
  const std::string ui_gfx_h = read_repo_file("../../include/ui_gfx.h");
  const std::string home_cpp = read_repo_files({
      "../../components/latter_ui/page_clock.cpp",
      "../../components/latter_ui/home_activity_model.cpp",
      "../../components/latter_ui/page_home.cpp",
  });
  const std::string tools_cpp = read_repo_files({
      "../../components/latter_ui/page_tools.cpp",
      "../../components/latter_ui/page_tool_calc.cpp",
      "../../components/latter_ui/page_tool_dice_coin.cpp",
  });
  const std::string home_hub_cpp = read_repo_file("../../components/latter_ui/page_home_hub.cpp");

  LATTER_CHECK(ui_color_types.find("enum class GlassVariant : uint8_t") != std::string::npos);
  LATTER_CHECK(ui_color_types.find("kChromeModalBg(16, 20, 28)") != std::string::npos);
  LATTER_CHECK(ui_gfx_h.find("kGlassAlphaMinQ8 = 96") != std::string::npos);
  LATTER_CHECK(ui_gfx_h.find("kGlassClearAlphaQ8 = 104") != std::string::npos);
  LATTER_CHECK(ui_gfx_h.find("kGlassClearAlphaMaxQ8 = 116") != std::string::npos);
  LATTER_CHECK(ui_gfx_h.find("kGlassReadableAlphaMinQ8 = 208") != std::string::npos);
  LATTER_CHECK(home_cpp.find("resolve_home_card_palette") == std::string::npos);
  LATTER_CHECK(tools_cpp.find("GlassVariant::ReadableDark") == std::string::npos);
  LATTER_CHECK(home_hub_cpp.find("GlassVariant::ReadableDark") == std::string::npos);
}

void test_frame_cadence_and_typed_prefs() {
  using namespace latter_ui;

  constexpr PersistedDevicePrefs kDefPrefs{};
  static_assert(std::is_trivially_copyable_v<PersistedDevicePrefs>);
  static_assert(kDefPrefs.wake_word_mode_enum() == WakeWordMode::DockedOnly);
  static_assert(kDefPrefs.screensaver_style_enum() == ScreensaverStyle::AmbientClock);
  static_assert(kDefPrefs.auto_brightness_enum() == AutoBrightnessMode::SunSynchronized);
  static_assert(kDefPrefs.battery_screen_timeout_ms() == 30000U);
  static_assert(kDefPrefs.return_to_home_ms() == 120000U);

  PersistedDevicePrefs corrupt = kDefPrefs;
  corrupt.battery_screen_timeout = 99U;
  corrupt.return_to_home_timeout = 99U;
  LATTER_CHECK(corrupt.battery_screen_timeout_ms() == 30000U && corrupt.return_to_home_ms() == 120000U);

  static_assert(FrameCadenceController::interactive_gap_for(true, true) == 36U);
  static_assert(FrameCadenceController::interactive_gap_for(false, true) == 54U);

  LATTER_CHECK(!FrameCadenceController::ambient_step_ready(1060U, 1000U, 1020U, false, false, false, false, false, true));
  LATTER_CHECK(FrameCadenceController::ambient_step_ready(1080U, 1000U, 1020U, false, false, false, false, false, true));
  LATTER_CHECK(FrameCadenceController::ambient_step_ready(1060U, 1000U, 1020U, false, true, false, false, false, true));

  FrameCadenceController fc;
  fc.reset();
  Cadence20msInputs in20{};
  in20.now_ms = 2000U;
  in20.panel_on = true;
  in20.va_visible = true;
  in20.va_running = true;
  in20.tts_received = true;
  auto act20 = fc.evaluate_20ms(in20);
  LATTER_CHECK(act20.va_transition_holdoff && !act20.schedule_anim_full);
  in20.now_ms = 2100U;
  act20 = fc.evaluate_20ms(in20);
  LATTER_CHECK(!act20.va_transition_holdoff && act20.schedule_anim_full);
  in20 = Cadence20msInputs{};
  in20.now_ms = 2120U;
  LATTER_CHECK(!fc.evaluate_20ms(in20).stop_anim_poller);
  in20.now_ms = 2140U;
  LATTER_CHECK(fc.evaluate_20ms(in20).stop_anim_poller);

  Cadence250msInputs in250{};
  in250.now_ms = 8000U;
  in250.gui_page = 1;
  in250.activity_mask = 0x02U;
  in250.rotate_period_ms = 8000U;
  in250.pending_wallpaper_fetch = true;
  auto act250 = fc.evaluate_250ms(in250);
  LATTER_CHECK(act250.ensure_anim_poller && act250.schedule_disp_now && act250.fetch_wallpaper);

  const std::string sys_yaml = read_repo_files({
      "../../modules/system_core.yaml",
      "../../modules/system_ui_sync.yaml",
  });
  LATTER_CHECK(sys_yaml.find("rt.get_select") == std::string::npos);
  LATTER_CHECK(sys_yaml.find("rt.toggle_switch") == std::string::npos);
}

void test_custom_icon_tiers_and_d051() {
  using namespace latter_ui;

  static_assert(dock_icon_r_for(24) == 13);
  static_assert(dock_icon_r_for(27) == 15);
  static_assert(dock_icon_r_for(40) == 22);

  for (const auto &p : kFlagshipPanels) {
    const auto base = compute_panel_layout(p.w, p.h, p.round, p.wearable, p.ppi);
    const int ir = dock_icon_r_for(base.dock_r);
    const float fill = static_cast<float>(2 * ir + 1) / static_cast<float>(2 * base.dock_r + 1);
    LATTER_CHECK(fill >= 0.53f && fill <= 0.58f);
  }

  const std::string w_hdr = read_repo_file("../../include/ui_widgets.h");
  const std::string w_chrome = read_repo_file("../../components/latter_ui/widgets_chrome.cpp");
  const std::string home_src = read_repo_files({
      "../../components/latter_ui/page_clock.cpp",
      "../../components/latter_ui/page_home.cpp",
  });
  const std::string hub_src = read_repo_file("../../components/latter_ui/page_home_hub.cpp");
  const std::string time_src = read_repo_file("../../components/latter_ui/page_time.cpp");

  LATTER_CHECK(w_hdr.find("draw_icon_clock_face") == std::string::npos);
  LATTER_CHECK(w_hdr.find("draw_icon_clock_solid") != std::string::npos);
  LATTER_CHECK(w_hdr.find("draw_icon_shield_home") != std::string::npos);

  const size_t glyph_sec_start = w_chrome.find("void draw_icon_power(");
  const size_t glyph_sec_end = w_chrome.find("void draw_route_icon(");
  LATTER_CHECK(glyph_sec_start != std::string::npos && glyph_sec_end != std::string::npos);
  LATTER_CHECK(w_chrome.substr(glyph_sec_start, glyph_sec_end - glyph_sec_start).find("scale_s(") == std::string::npos);

  LATTER_CHECK(home_src.find("draw_route_icon(") != std::string::npos);
  LATTER_CHECK(hub_src.find("draw_icon_shield_home") != std::string::npos);
  LATTER_CHECK(time_src.find("draw_icon_clock_solid") != std::string::npos);

  TouchGestureState g{};
  g.begin(233, 233, 1000U, PageId::REMOTE, 70);
  g.arm_long_press(WidgetId::REMOTE_DPAD_OK, 0);
  LATTER_CHECK(!g.should_fire_long_press(1479U) && g.should_fire_long_press(1480U));
  g.mark_long_press_fired();
  LATTER_CHECK(g.long_press_fired && !g.long_press_armed && g.tap_cancelled);

  const RemoteDpadGeom pk = compute_remote_dpad_geom(466, 466, true);
  LATTER_CHECK(resolve_remote_dpad_widget(233, 233, pk) == WidgetId::NONE);
  LATTER_CHECK(resolve_remote_dpad_widget(233, 233 - 52, pk) == WidgetId::REMOTE_DPAD_UP);
  LATTER_CHECK(resolve_remote_dpad_widget(233 - 55, 233 + 115, pk) == WidgetId::REMOTE_DPAD_DOWN);
  LATTER_CHECK(resolve_remote_dpad_widget(233 - 140, 233 + 35, pk) == WidgetId::REMOTE_DPAD_LEFT);
  LATTER_CHECK(resolve_remote_dpad_widget(233 + 150, 233 - 20, pk) == WidgetId::REMOTE_DPAD_RIGHT);
}

void test_gui_standardization_d070() {
  using namespace latter_ui;

  struct BoardSpec { int w, h; bool round, wearable; int ppi; };
  constexpr BoardSpec kBoards[5] = {
      {320, 480, false, false, 165},
      {410, 502, false, true,  315},
      {466, 466, true,  false, 266},
      {480, 480, false, false, 313},
      {720, 720, false, false, 254},
  };

  for (const auto &b : kBoards) {
    const int min_dim = (b.w < b.h) ? b.w : b.h;
    const auto ly = compute_panel_layout(b.w, b.h, b.round, b.wearable, b.ppi);
    int mode_xs[4] = {0, 0, 0, 0};
    compute_mode_bar_slot_xs(4, b.w, b.round, mode_xs);
    const auto cam_g = compute_camera_grid_geom(b.w, b.h, b.round, b.wearable, b.ppi);
    for (int i = 0; i < 4; ++i) LATTER_CHECK(cam_g.mode_slot_xs[i] == mode_xs[i]);
    LATTER_CHECK(radius_card_for(min_dim) > radius_key_for(min_dim));
    const int stack_w = compute_uniform_stack_w_for(
        ly.slot5_y(0), ly.slot5_y(4), ly.slot5_h, b.w - 20, b.w, b.h, b.round, b.wearable, 14);
    if (b.w > 520) LATTER_CHECK(stack_w <= kMaxContentColW);
  }

  const std::string mode_bar_families[7] = {
      read_repo_file("../../components/latter_ui/page_area_hub.cpp"),
      read_repo_file("../../components/latter_ui/page_weather.cpp"),
      read_repo_file("../../components/latter_ui/page_home_hub.cpp"),
      read_repo_file("../../components/latter_ui/page_time.cpp"),
      read_repo_glob("../../components/latter_ui", "page_calendar_", ".cpp"),
      read_repo_file("../../components/latter_ui/page_music_shared.h") + "\n" +
          read_repo_glob("../../components/latter_ui", "page_music_", ".cpp"),
      read_repo_file("../../components/latter_ui/page_cameras.cpp"),
  };
  for (const auto &src : mode_bar_families) {
    LATTER_CHECK(src.find("ui::mode_bar(") != std::string::npos);
    LATTER_CHECK(src.find("render_in_app_mode_bar_track(") == std::string::npos);
  }

  for (std::string_view slug : {
           "mdi:pin", "mdi:music", "mdi:speaker-multiple", "mdi:home", "mdi:shield-home",
           "mdi:bullhorn", "mdi:thermostat", "mdi:lock", "mdi:garage", "mdi:robot-vacuum",
           "mdi:cctv", "mdi:view-grid", "mdi:video", "mdi:view-carousel", "mdi:cog-outline",
           "mdi:sofa", "mdi:lightbulb", "mdi:remote-tv", "mdi:weather-partly-cloudy",
           "mdi:clock-outline", "mdi:calendar", "mdi:controller", "mdi:play-pause", "mdi:menu"}) {
    LATTER_CHECK(has_mdi_icon(slug));
  }

  std::string raw_phrases;
  for (int i = 1; i <= 14; ++i) {
    if (i > 1) raw_phrases.push_back('\x1e');
    raw_phrases += "  Phrase " + std::to_string(i) + "  ";
  }
  std::vector<std::string> phrases;
  LATTER_CHECK(parse_broadcast_phrases(raw_phrases, phrases) == 12);

  DeviceRuntimeState brs{};
  start_broadcast_dictation(1000U, brs);
  LATTER_CHECK(broadcast_dictation_active(brs));
  LATTER_CHECK(finish_broadcast_dictation("  dinner is ready  ", 2000U, brs));
  LATTER_CHECK(!broadcast_dictation_active(brs) && brs.intercom.broadcast_message == "Dinner is ready");
}

void test_photo_frame_screensaver_day_night_sky_d067() {
  using namespace latter_ui;

  static_assert(screensaver_allowed_for_power(0U, true, false));
  static_assert(!screensaver_allowed_for_power(0U, false, false));
  static_assert(screensaver_allowed_for_power(1U, false, false));
  static_assert(!screensaver_allowed_for_power(2U, true, true));

  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(1.00f, true) - 0.70f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(1.00f, false) - 0.45f) < 1e-4f);

  FrameCadenceController fc{};
  CadenceScreensaverInputs in{};
  in.now_ms = 5000U;
  in.display_power_state = 1;
  in.screensaver_style = "Photo Frame";
  const auto pf_act = fc.evaluate_screensaver_20ms(in);
  LATTER_CHECK(!pf_act.schedule_anim_full && pf_act.sync_disp_poller && !pf_act.sync_disp_poller_val);

  static_assert(is_day_night_sky_wallpaper_url("latter://day_night_sky"));
  static_assert(!is_day_night_sky_wallpaper_url("http://ha.local/api/latter/wallpaper/1"));
  static_assert(resolve_day_night_sky_is_night("below_horizon", true, 12));
  static_assert(!resolve_day_night_sky_is_night("above_horizon", true, 23));

  const uint32_t kSsSig = (1U << 3);
  const std::string url = "http://ha.local/api/latter/wallpaper/p1?mode=screensaver";
  LATTER_CHECK(is_photo_frame_initial_load_pending(kSsSig, "Photo Frame", url, false, false, 10000U, 12499U));
  LATTER_CHECK(!is_photo_frame_initial_load_pending(kSsSig, "Photo Frame", url, true, false, 10000U, 10150U));
  LATTER_CHECK(can_reuse_wallpaper_for_photo_frame(url, url, true));
}

}  // namespace

void run_ui_layout_compositor_tests() {
  test_ui_layout_hit_table_and_dirty_band();
  test_phase2_glass_and_scissor_compositor();
  test_frame_cadence_and_typed_prefs();
  test_custom_icon_tiers_and_d051();
  test_gui_standardization_d070();
  test_photo_frame_screensaver_day_night_sky_d067();
}

}  // namespace latter_test
