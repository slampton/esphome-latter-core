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

  const std::string ui_color_types = read_repo_files({
      "../../include/ui_color.h",
      "../../include/ui_types.h",
  });
  const std::string ui_gfx_h = read_repo_file("../../include/ui_gfx.h");
  const std::string home_cpp = read_repo_files({
      "../../components/latter_ui/page_clock.cpp",
      "../../components/latter_ui/page_home.cpp",
  });
  const std::string tools_cpp = read_repo_file("../../components/latter_ui/page_tools.cpp");
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
  using namespace latter_core;

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
  using namespace latter_core;

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
  const std::string w_chrome = read_repo_file("../../components/latter_ui/ui_widgets.cpp");
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
  using namespace latter_core;

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
      read_repo_file("../../components/latter_ui/page_calendar.cpp"),
      read_repo_file("../../components/latter_ui/page_music.cpp"),
      read_repo_file("../../components/latter_ui/page_cameras.cpp"),
  };
  for (const auto &src : mode_bar_families) {
    LATTER_CHECK(src.find("ui::mode_bar(") != std::string::npos);
    LATTER_CHECK(src.find("render_in_app_mode_bar_track(") == std::string::npos);
  }

  for (std::string_view slug : {
           "mdi:pin", "mdi:music", "mdi:speaker-multiple", "mdi:home", "mdi:shield-home",
           "mdi:bullhorn", "mdi:thermostat", "mdi:lock", "mdi:garage", "mdi:robot-vacuum",
           "mdi:cctv", "mdi:view-grid", "mdi:video", "mdi:view-carousel", "mdi:cog",
           "mdi:sofa", "mdi:lightbulb", "mdi:remote-tv", "mdi:weather-partly-cloudy",
           "mdi:clock", "mdi:calendar", "mdi:controller", "mdi:play-pause", "mdi:menu"}) {
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
  using namespace latter_core;

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
  static_assert(!has_known_day_night_phase("", false, -1));
  static_assert(has_known_day_night_phase("", true, 22));
  static_assert(has_known_day_night_phase("below_horizon", false, -1));
  static_assert(resolve_day_night_sky_is_night("below_horizon", true, 12));
  static_assert(!resolve_day_night_sky_is_night("above_horizon", true, 23));
  static_assert(resolve_day_night_sky_is_night("", true, 22));
  static_assert(!resolve_day_night_sky_is_night("", true, 12));

  const uint32_t kSsSig = (1U << 3);
  const std::string url = "http://ha.local/api/latter/wallpaper/p1?mode=screensaver";
  LATTER_CHECK(is_photo_frame_initial_load_pending(kSsSig, "Photo Frame", url, false, false, 10000U, 12499U));
  LATTER_CHECK(!is_photo_frame_initial_load_pending(kSsSig, "Photo Frame", url, true, false, 10000U, 10150U));
  LATTER_CHECK(can_reuse_wallpaper_for_photo_frame(url, url, true));
}

bool is_valid_utf8_str(const std::string &s) noexcept {
  size_t i = 0;
  const size_t n = s.size();
  while (i < n) {
    const unsigned char b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80U) {
      ++i;
      continue;
    }
    size_t len = 0;
    uint32_t cp = 0;
    if ((b0 & 0xE0U) == 0xC0U && i + 1 < n) {
      const unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
      if ((b1 & 0xC0U) != 0x80U) return false;
      cp = (static_cast<uint32_t>(b0 & 0x1FU) << 6) | static_cast<uint32_t>(b1 & 0x3FU);
      if (cp < 0x80U) return false;
      len = 2;
    } else if ((b0 & 0xF0U) == 0xE0U && i + 2 < n) {
      const unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
      const unsigned char b2 = static_cast<unsigned char>(s[i + 2]);
      if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U) return false;
      cp = (static_cast<uint32_t>(b0 & 0x0FU) << 12) |
           (static_cast<uint32_t>(b1 & 0x3FU) << 6) |
           static_cast<uint32_t>(b2 & 0x3FU);
      if (cp < 0x800U || (cp >= 0xD800U && cp <= 0xDFFFU)) return false;
      len = 3;
    } else if ((b0 & 0xF8U) == 0xF0U && i + 3 < n) {
      const unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
      const unsigned char b2 = static_cast<unsigned char>(s[i + 2]);
      const unsigned char b3 = static_cast<unsigned char>(s[i + 3]);
      if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U || (b3 & 0xC0U) != 0x80U) return false;
      cp = (static_cast<uint32_t>(b0 & 0x07U) << 18) |
           (static_cast<uint32_t>(b1 & 0x3FU) << 12) |
           (static_cast<uint32_t>(b2 & 0x3FU) << 6) |
           static_cast<uint32_t>(b3 & 0x3FU);
      if (cp < 0x10000U || cp > 0x10FFFFU) return false;
      len = 4;
    } else {
      return false;
    }
    i += len;
  }
  return true;
}

void test_landscape_geoms_psram_budget_and_utf8_probing() {
  using namespace latter_ui;
  using namespace latter_core;

  // 1. Landscape panel layout & Remote D-Pad bounds (800x480, 1024x600, 1280x800, 800x400)
  struct LandscapeCase {
    int w;
    int h;
    int ppi;
  };
  static constexpr LandscapeCase kLandscapes[] = {
      {800, 480, 187},
      {1024, 600, 170},
      {1280, 800, 149},
      {800, 400, 170},
  };
  for (const auto &lc : kLandscapes) {
    const auto ly = compute_panel_layout(lc.w, lc.h, false, false, lc.ppi);
    LATTER_CHECK(ly.dock_y + ly.dock_r < ly.dock_pips_y);
    LATTER_CHECK(ly.list_card_w <= kMaxContentColW);
    int xs[4] = {0, 0, 0, 0};
    compute_mode_bar_slot_xs(4, lc.w, false, xs);
    LATTER_CHECK(xs[0] - ly.dock_r >= 0);
    LATTER_CHECK(xs[3] + ly.dock_r < lc.w);
    LATTER_CHECK((xs[1] - xs[0]) > 2 * ly.dock_r);

    const auto dp = compute_remote_dpad_geom(lc.w, lc.h, false);
    LATTER_CHECK(dp.cy - dp.r_out - dp.outer_pad > 0);
    LATTER_CHECK(dp.cy + dp.r_out + dp.outer_pad < lc.h);
    LATTER_CHECK(dp.r_out > dp.r_in && dp.r_in >= 36);

    // Tablet landscape Activity Card internals & hit rects use scale_s (zero 64 px text crush on 1024x600 / 1280x800).
    const int min_dim = (lc.w < lc.h) ? lc.w : lc.h;
    const int act_card_w = std::min(kMaxContentColW, scale_x_for(380, lc.w));
    const int act_card_h = scale_y_for(60, lc.h) + scale_y_for(56, lc.h);
    const int act_col_w = scale_s_for(56, min_dim);
    const int drop_col_w = scale_s_for(52, min_dim);
    const int card_left = lc.w / 2 - act_card_w / 2;
    const int card_right = lc.w / 2 + act_card_w / 2;
    const int txt_x = card_left + scale_s_for(48, min_dim);
    const int div1_x = card_right - drop_col_w - act_col_w;
    const int max_txt = div1_x - txt_x - scale_s_for(8, min_dim);
    LATTER_CHECK(max_txt >= 190);
    FrameHitTable act_ht{};
    const int act_cy = ly.dock_y - ly.dock_r - mm_to_px_for(3.0f, lc.ppi) - act_card_h / 2;
    register_home_activity_hit_regions(act_ht, lc.w / 2, act_cy, act_card_w, act_card_h,
                                       true, true, 0, 1, act_col_w, drop_col_w);
    const HitRegion *hr_row = act_ht.hit_test(txt_x + 4, act_cy - act_card_h / 4, kHitTap);
    const HitRegion *hr_act = act_ht.hit_test(card_right - drop_col_w - act_col_w / 2, act_cy - act_card_h / 4, kHitTap);
    const HitRegion *hr_exp = act_ht.hit_test(card_right - drop_col_w / 2, act_cy, kHitTap);
    LATTER_CHECK(hr_row != nullptr && hr_row->widget_id == WidgetId::HOME_ACT_ROW_0);
    LATTER_CHECK(hr_act != nullptr && hr_act->widget_id == WidgetId::HOME_ACT_ACTION_0);
    LATTER_CHECK(hr_exp != nullptr && hr_exp->widget_id == WidgetId::HOME_ACT_EXPAND);
    LATTER_CHECK(hr_row->x1 + 1 == div1_x && hr_act->x0 == div1_x && hr_act->x1 + 1 == card_right - drop_col_w);

    // Camera Single 4-pill & 2-pill action rows stay proportional and non-overlapping on 1024x600 / 1280x800.
    const int cam_h = std::max(38, scale_y_for(40, lc.h));
    const int cam_y = (ly.app_dock_y - ly.dock_r - scale_s_for(3, min_dim)) - scale_y_for(14, lc.h) - cam_h / 2;
    HomeHubPillGeom cam_geoms[4]{};
    const int n4 = compute_action_pill_row(
        4, cam_y, cam_h, std::min(kMaxContentColW, scale_s_for(376, min_dim)),
        scale_s_for(104, min_dim), scale_s_for(8, min_dim),
        lc.w, lc.h, false, false, lc.ppi, cam_geoms, 12);
    LATTER_CHECK(n4 == 4);
    for (int i = 0; i < 4; ++i) {
      LATTER_CHECK(cam_geoms[i].w >= ((min_dim >= 600) ? 105 : 80));
      LATTER_CHECK(cam_geoms[i].hit_h >= mm_to_px_for(7.0f, lc.ppi));
      if (i > 0) {
        LATTER_CHECK(cam_geoms[i - 1].cx + cam_geoms[i - 1].w / 2 < cam_geoms[i].cx - cam_geoms[i].w / 2);
      }
    }
    const int row_span = (cam_geoms[3].cx + cam_geoms[3].w / 2) - (cam_geoms[0].cx - cam_geoms[0].w / 2);
    LATTER_CHECK(row_span <= kMaxContentColW);
  }

  // Isotropic toggle_switch aspect ratio across all active geometries.
  {
    const int sw_h = std::max(18, scale_s(22));
    const int sw_w = (sw_h * 40 + 11) / 22;
    const float aspect = static_cast<float>(sw_w) / static_cast<float>(sw_h);
    LATTER_CHECK(aspect >= 1.70f && aspect <= 1.95f);
  }

  // 2. Byte-budgeted PSRAM cache sizing (ArtCanvasCache & SkyBaseCache)
  static_assert(compute_art_cache_slot_budget(720, 720, 32, 3) == 3);
  static_assert(compute_art_cache_slot_budget(1024, 600, 32, 3) == 2);
  static_assert(compute_art_cache_slot_budget(1280, 800, 32, 3) == 1);
  static_assert(compute_art_cache_slot_budget(466, 466, 8, 1) == 1);
  static_assert(compute_art_cache_slot_budget(800, 480, 8, 1) == 1);
  static_assert(compute_art_cache_slot_budget(1280, 800, 8, 1) == 0);
  static_assert(sky_base_cache_allowed(720, 720, 32));
  static_assert(sky_base_cache_allowed(1024, 600, 32));
  static_assert(sky_base_cache_allowed(1280, 800, 32));
  static_assert(sky_base_cache_allowed(800, 480, 8));
  static_assert(!sky_base_cache_allowed(1280, 800, 8));

  // 3. UTF-8 glyph probing via font.find_glyph() + multibyte pixel-width truncation (no 32-byte cliff)
  static int s_probe_font_tag = 42;
  UiFontView probe_font{};
  probe_font.handle = &s_probe_font_tag;
  probe_font.height = 20;
  probe_font.baseline = 16;
  probe_font.bpp = 1;
  probe_font.measure_fn = +[](const void *, const char *s, int *w, int *xo, int *bs, int *h) {
    int cps = 0;
    if (s != nullptr) {
      for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p != 0; ++p) {
        if ((*p & 0xC0U) != 0x80U) ++cps;
      }
    }
    if (w != nullptr) *w = cps * 10;
    if (xo != nullptr) *xo = 0;
    if (bs != nullptr) *bs = 16;
    if (h != nullptr) *h = 20;
  };
  probe_font.find_glyph_fn = +[](const void *, uint32_t cp, UiGlyphData &out_g) -> bool {
    // Allow printable ASCII + Cyrillic 'Ж' (0x0416), Greek 'Ω' (0x03A9), CJK '音' (0x97F3),
    // and 4-byte emoji '🎵' (0x1F3B5); explicitly reject '€' (0x20AC) and '漢' (0x6F22).
    const bool ok = (cp >= 0x20U && cp <= 0x7EU) ||
                    cp == 0x0416U || cp == 0x03A9U || cp == 0x97F3U || cp == 0x1F3B5U;
    if (!ok) return false;
    out_g.advance = 10;
    out_g.width = 8;
    out_g.height = 14;
    return true;
  };

  const std::string mixed = "Hi \xD0\x96\xCE\xA9\xE9\x9F\xB3\xF0\x9F\x8E\xB5\xE2\x82\xAC\xE6\xBC\xA2!";
  const std::string clean = sanitize_utf8_for_font(mixed, probe_font);
  LATTER_CHECK(clean == "Hi \xD0\x96\xCE\xA9\xE9\x9F\xB3\xF0\x9F\x8E\xB5!");
  LATTER_CHECK(is_valid_utf8_str(clean));

  // Missing curly quotes / dashes / ellipsis fall back cleanly to ASCII when absent from font.
  const std::string punct = "A\xE2\x80\x98" "B\xE2\x80\x99\xE2\x80\x94" "C\xE2\x80\xA6";
  LATTER_CHECK(sanitize_utf8_for_font(punct, probe_font) == "A'B'-C...");

  // Verify no 32-byte truncation cliff on long ASCII titles with wide max_w.
  const std::string long_ascii(64, 'A');
  const std::string fit_wide = fit_text_to_width_font(long_ascii, 500, probe_font);
  LATTER_CHECK(fit_wide.size() == 50U);  // 48 'A's + ".." = 500 px exactly!
  LATTER_CHECK(fit_wide.substr(fit_wide.size() - 2U) == "..");

  // Verify multibyte UTF-8 truncation at every width produces valid UTF-8 and fits max_w.
  std::string long_utf8;
  for (int k = 0; k < 8; ++k) {
    long_utf8 += "\xD0\x96\xCE\xA9\xE9\x9F\xB3\xF0\x9F\x8E\xB5";
  }
  for (int mw = 20; mw <= 280; mw += 15) {
    const std::string fitted = fit_text_to_width_font(long_utf8, mw, probe_font);
    LATTER_CHECK(is_valid_utf8_str(fitted));
    int tw = 0, xo = 0, bs = 0, th = 0;
    probe_font.measure(fitted.c_str(), &tw, &xo, &bs, &th);
    LATTER_CHECK(tw <= mw);
    LATTER_CHECK(fitted.size() >= 2U && fitted.substr(fitted.size() - 2U) == "..");
  }
}

void test_p4_dma_blit_calm_sky_and_dock_clip() {
  using namespace latter_ui;
  using namespace latter_core;

  // 1. Calm vs fast weather motion classification
  for (const char *calm : {"sunny", "clear-day", "clear", "partlycloudy", "cloudy", "overcast", "fog", "mist", "haze"}) {
    LATTER_CHECK(evaluate_sky_has_motion("Live (Home Assistant)", calm, "above_horizon", 10000U));
    LATTER_CHECK(!evaluate_sky_has_fast_motion("Live (Home Assistant)", calm, "above_horizon", 10000U));
  }
  for (const char *calm_night : {"cloudy", "overcast", "fog", "mist", "haze"}) {
    LATTER_CHECK(evaluate_sky_has_motion("Live (Home Assistant)", calm_night, "below_horizon", 10000U));
    LATTER_CHECK(!evaluate_sky_has_fast_motion("Live (Home Assistant)", calm_night, "below_horizon", 10000U));
  }
  for (const char *fast : {"rainy", "pouring", "drizzle", "snowy", "sleet", "hail", "lightning", "thunderstorm", "windy"}) {
    LATTER_CHECK(evaluate_sky_has_motion("Live (Home Assistant)", fast, "above_horizon", 10000U));
    LATTER_CHECK(evaluate_sky_has_fast_motion("Live (Home Assistant)", fast, "above_horizon", 10000U));
  }
  LATTER_CHECK(evaluate_sky_has_fast_motion("Live (Home Assistant)", "partlycloudy", "below_horizon", 10000U));
  LATTER_CHECK(evaluate_sky_has_fast_motion("Live (Home Assistant)", "clear-night", "below_horizon", 10000U) ==
               evaluate_sky_has_motion("Live (Home Assistant)", "clear-night", "below_horizon", 10000U));

  // 2. Calm-sky cadence (240 ms) + Home dock band clipping vs fast full-screen weather
  const int dock_y0 = dock_dirty_band_y0();
  const int calm_y1 = calm_home_sky_band_y1(dock_y0);
  LATTER_CHECK(calm_y1 > 0 && calm_y1 < dock_y0 && (calm_y1 & 1) == 1);
  // Calm haze drift bands (3 bands max for fog: center_y <= scale_y(254) + 5, hh = scale_y(8))
  // stay strictly inside [0, calm_y1] across all compiled geometries.
  const int max_calm_haze_y = scale_y(118 + 2 * 68) + scale_y(8) + 5;
  LATTER_CHECK(max_calm_haze_y <= calm_y1);

  FrameCadenceController ctrl{};
  ctrl.reset();
  Cadence20msInputs in{};
  in.panel_on = true;
  in.display_power_state = 0;
  in.gui_page = 1;
  in.home_subpage = 0;
  in.is_chip_p4 = true;
  in.dock_y0 = dock_y0;
  in.dock_y1 = SCREEN_H - 1;
  in.sky_enabled = true;
  in.sky_stepping = true;
  in.sky_fast_motion = false;
  in.last_ambient_ms = 1000U;
  in.last_disp_end_ms = 1020U;

  // Before kCalmSkyFrameGapMs (240 ms): calm sky stays warm without triggering an early frame.
  in.now_ms = 1100U;
  const auto calm_early = ctrl.evaluate_20ms(in);
  LATTER_CHECK(calm_early.anim_driving_disp);
  LATTER_CHECK(!calm_early.schedule_anim_band && !calm_early.schedule_anim_full);

  // At >= kCalmSkyFrameGapMs: calm Home sky schedules upper-band [0, calm_y1] strictly above the Dock.
  in.now_ms = 1000U + kCalmSkyFrameGapMs;
  const auto calm_due = ctrl.evaluate_20ms(in);
  LATTER_CHECK(calm_due.record_ambient_ms);
  LATTER_CHECK(calm_due.schedule_anim_band && !calm_due.schedule_anim_full);
  LATTER_CHECK(calm_due.band_y0 == 0 && calm_due.band_y1 == calm_y1);

  // Weather app page (gui_page == 5) with calm sky still renders full frame (no Home Dock),
  // and its scheduled post-render cadence gap uses kCalmSkyFrameGapMs (240 ms) instead of 60-74 ms.
  in.gui_page = 5;
  const auto wx_app_due = ctrl.evaluate_20ms(in);
  LATTER_CHECK(wx_app_due.schedule_anim_full && !wx_app_due.schedule_anim_band);
  LATTER_CHECK(weather_sky_frame_gap_ms(74U, false) == kCalmSkyFrameGapMs);
  LATTER_CHECK(weather_sky_frame_gap_ms(300U, false) == 300U);
  LATTER_CHECK(weather_sky_frame_gap_ms(74U, true) == 74U);

  // Fast weather on Home (rain/snow/storm/wind/night-stars) renders full frame at standard ~60 ms cadence.
  in.gui_page = 1;
  in.sky_fast_motion = true;
  in.now_ms = 1080U;
  const auto fast_due = ctrl.evaluate_20ms(in);
  LATTER_CHECK(fast_due.schedule_anim_full && !fast_due.schedule_anim_band);

  // 3. Band-clipped Home render skips Dock pixel drawing while keeping Dock hit targets live.
  {
    HostSurfaceFixture fx{};
    constexpr uint16_t kSentinel = 0xA55AU;
    fx.fill(kSentinel);
    reset_runtime_state();
    auto &rs = runtime_state();
    rs.nav.gui_page = PageId::HOME;
    rs.nav.home_subpage = HomeSubpage::MAIN;
    auto &ws = weather_state();
    ws.temperature = 68.0f;
    ws.condition = "sunny";
    ws.sun_state = "above_horizon";
    fx.ctx.home_weather_bg = true;
    fx.ctx.weather_sim_opt = "Live (Home Assistant)";

    frame_hits().clear();
    set_clip_y_band(0, calm_y1);
    render_home_page(fx.surf, fx.ctx);
    reset_clip_y_band();

    // Every pixel in the untouched Dock band [dock_y0, SCREEN_H - 1] remains sentinel-intact.
    for (int y = dock_y0; y < SCREEN_H; ++y) {
      const size_t row = static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
      for (int x = 0; x < SCREEN_W; ++x) {
        LATTER_CHECK(fx.pixels[row + static_cast<size_t>(x)] == kSentinel);
      }
    }
    // Dock slot hit regions were still registered despite skipping Dock pixel rendering!
    bool found_dock_slot = false;
    for (uint8_t i = 0; i < frame_hits().count; ++i) {
      if (frame_hits().regions[i].widget_id == WidgetId::DOCK_SLOT_BASE) {
        found_dock_slot = true;
        break;
      }
    }
    LATTER_CHECK(found_dock_slot);
  }

  // 4. blit_psram_band_fast pixel-exact band copy with phase-matched PSRAM buffer
  {
    const size_t px_count = static_cast<size_t>(SCREEN_W) * static_cast<size_t>(SCREEN_H);
    const size_t bytes = px_count * sizeof(uint16_t);
    auto *dst = static_cast<uint16_t *>(ui_psram_aligned_calloc(px_count, sizeof(uint16_t)));
    PsramPhaseBuf src_buf = ui_psram_phase_alloc(bytes, dst, false);
    LATTER_CHECK(src_buf.px != nullptr && dst != nullptr);
    for (size_t i = 0; i < px_count; ++i) {
      src_buf.px[i] = static_cast<uint16_t>((i * 257U + 13U) & 0xFFFFU);
      dst[i] = 0x5AA5U;
    }
    ui_psram_cache_writeback(src_buf.px, bytes);
    const int y_lo = 11;
    const int y_hi = SCREEN_H - 19;
    blit_psram_band_fast(dst, src_buf.px, y_lo, y_hi);
    for (int y = 0; y < SCREEN_H; ++y) {
      const size_t row = static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
      const bool in_band = (y >= y_lo && y <= y_hi);
      for (int x = 0; x < SCREEN_W; ++x) {
        const size_t idx = row + static_cast<size_t>(x);
        LATTER_CHECK(dst[idx] == (in_band ? src_buf.px[idx] : 0x5AA5U));
      }
    }
    ui_psram_phase_free(src_buf);
    LATTER_CHECK(src_buf.px == nullptr && src_buf.raw == nullptr);
    ui_psram_free(dst);
  }

  // 5. Round/rect frame_base() memset fast path + half-scale album art edge widening regression test
  {
    HostSurfaceFixture fx{};
    constexpr uint16_t kSentinel = 0xA55AU;
    fx.fill(kSentinel);
    dirty_band().mark_full();
    dirty_band().begin_frame(4, 0, 0U, true);
    frame_base_begin();
    LATTER_CHECK(frame_base(fx.surf, UiColor{0, 0, 0}));
    // Center pixel and inner visible chord are cleared to pure black across all geometries.
    LATTER_CHECK(fx.pixels[static_cast<size_t>(SCREEN_CY) * SCREEN_W + static_cast<size_t>(SCREEN_CX)] == 0U);
#if LATTER_ROUND_DISPLAY
    // Hidden outer corner outside circular bezel is intentionally skipped on round panels.
    LATTER_CHECK(fx.pixels[0] == kSentinel);
#else
    LATTER_CHECK(fx.pixels[0] == 0U);
#endif

    // Half-scale album art (exact 2x upscale): verify upper-half round chord left/right edges
    // never show stale pixels when repeated source rows widen across consecutive scanlines.
    const int half_w = std::max(16, SCREEN_W / 2);
    const int half_h = std::max(16, SCREEN_H / 2);
    const uint16_t src_fill = encode_hw_rgb565(UiColor{200, 120, 64});
    std::vector<uint16_t> half_art(static_cast<size_t>(half_w) * static_cast<size_t>(half_h), src_fill);
    fx.fill(0U);
    dirty_band().mark_full();
    dirty_band().begin_frame(2, 0, 0U, true);
    frame_base_begin();
    art_canvas_drop_all();
    LATTER_CHECK(render_album_art_canvas_view(
        fx.surf, ArtImageView{half_art.data(), half_w, half_h}, SCREEN_CX,
        /*is_standard=*/false, /*is_letterbox=*/false, /*apply_scrim=*/false,
        /*show_metadata=*/false, /*dim_level=*/0));
    // Check both edges of upper-half scanlines where a circular chord widens between consecutive rows.
    for (int y = SCREEN_H / 8; y < SCREEN_H / 2; ++y) {
      const int hw = LATTER_ROUND_DISPLAY ? circle_half_span(y, 0) : (SCREEN_W / 2 - 1);
      const int lx = std::max(0, SCREEN_CX - hw);
      const int rx = std::min(SCREEN_W - 1, SCREEN_CX + hw);
      const size_t row = static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
      LATTER_CHECK(fx.pixels[row + static_cast<size_t>(lx)] != 0U);
      LATTER_CHECK(fx.pixels[row + static_cast<size_t>(rx)] != 0U);
    }
    art_canvas_drop_all();
  }

  // 6. Weather Moon subpage renders shaded lunar disc cleanly without integer division or limb sqrt regressions
  {
    HostSurfaceFixture fx{};
    fx.fill(0U);
    reset_runtime_state();
    auto &rs = runtime_state();
    rs.nav.gui_page = PageId::WEATHER;
    rs.nav.sky_weather_subpage = 3;
    auto &ws = weather_state();
    ws.temperature = 62.0f;
    ws.condition = "clear-night";
    ws.sun_state = "below_horizon";
    ws.moon_phase = "full_moon";
    fx.ctx.weather_app_bg = false;
    dirty_band().mark_full();
    dirty_band().begin_frame(5, 3, 0U, true);
    frame_base_begin();
    frame_base(fx.surf, UiColor{0, 0, 0});
    render_weather_page(fx.surf, fx.ctx);
    // Count illuminated lunar disc pixels in the upper-middle region where the moon disc sits.
    int lit_px = 0;
    for (int y = SCREEN_H / 8; y < (SCREEN_H * 3) / 4; ++y) {
      const size_t row = static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
      for (int x = SCREEN_CX - SCREEN_W / 4; x <= SCREEN_CX + SCREEN_W / 4; ++x) {
        if (fx.pixels[row + static_cast<size_t>(x)] != 0U) ++lit_px;
      }
    }
    LATTER_CHECK(lit_px > 4000);
    // Re-rendering after Music-style PSRAM eviction keeps the cached lunar disc intact and pixel-identical.
    const std::vector<uint16_t> first_moon_fb = fx.pixels;
    evict_registered_psram_caches();
    fx.fill(0U);
    dirty_band().mark_full();
    dirty_band().begin_frame(5, 3, 1U, true);
    frame_base_begin();
    frame_base(fx.surf, UiColor{0, 0, 0});
    render_weather_page(fx.surf, fx.ctx);
    LATTER_CHECK(fx.pixels == first_moon_fb);
    sky_base_cache_drop();
  }

  // 7. blend_psram_vertical_wash_fast & glass SWAR bit-exact Q8 parity across all 4 endian modes
  {
    const size_t px_count = static_cast<size_t>(SCREEN_W) * static_cast<size_t>(SCREEN_H);
    const int saved_endian = active_frame_endian_mode();
    static constexpr uint8_t kRefBayer4[4][4] = {
        {  0, 128,  32, 160},
        {192,  64, 224,  96},
        { 48, 176,  16, 144},
        {240, 112, 208,  80},
    };
    for (int mode : {1, 2, 3, 4}) {
      active_frame_endian_mode() = mode;
      const uint16_t bg_hw = encode_hw_rgb565(UiColor{16, 32, 48});
      const uint16_t sentinel = 0x5AA5U;
      std::vector<uint16_t> src(px_count, bg_hw);
      // Seed extreme RGB565 corner values in the first active row to verify zero SWAR channel carry.
      const int y_lo = 12;
      const int y_hi = SCREEN_H - 13;
      const int inset = LATTER_ROUND_DISPLAY ? 5 : 0;
      const uint16_t kExtremes[8] = {0x0000U, 0xFFFFU, 0xF800U, 0x07E0U, 0x001FU, 0xF81FU, 0x07FFU, 0xFFE0U};
      for (int x = 0; x < SCREEN_W; ++x) {
        src[static_cast<size_t>(y_lo + 4) * SCREEN_W + static_cast<size_t>(x)] =
            encode_hw_rgb565_raw(kExtremes[x & 7]);
      }
      std::vector<uint16_t> dst_fused(px_count, sentinel);
      std::vector<uint16_t> dst_inplace(px_count, sentinel);

      blend_psram_vertical_wash_fast(dst_fused.data(), src.data(), y_lo, y_hi,
                                     190U, 220U, 255U, 78, 152, inset, SCREEN_CX);
      // Verify untouched bands outside [y_lo, y_hi] remain sentinel-intact.
      for (int y = 0; y < y_lo; ++y) {
        LATTER_CHECK(dst_fused[static_cast<size_t>(y) * SCREEN_W + static_cast<size_t>(SCREEN_CX)] == sentinel);
      }
      for (int y = y_hi + 1; y < SCREEN_H; ++y) {
        LATTER_CHECK(dst_fused[static_cast<size_t>(y) * SCREEN_W + static_cast<size_t>(SCREEN_CX)] == sentinel);
      }

      // Verify fused src->dst matches copy-then-inplace pixel-for-pixel across the active band.
      for (int y = y_lo; y <= y_hi; ++y) {
        const size_t row = static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
        std::memcpy(dst_inplace.data() + row, src.data() + row, static_cast<size_t>(SCREEN_W) * sizeof(uint16_t));
      }
      blend_psram_vertical_wash_fast(dst_inplace.data(), dst_inplace.data(), y_lo, y_hi,
                                     190U, 220U, 255U, 78, 152, inset, SCREEN_CX);
      const int span_h = std::max(1, SCREEN_H - 1 - 2 * inset);
      const int check_y = y_lo + 4;
      const int falloff_q8 = 256 - (((check_y - inset) * 152) / span_h);
      const int check_a = (78 * std::clamp(falloff_q8, 0, 256)) >> 8;
      const int check_inv = 256 - check_a;
      const int sr5 = 190 >> 3, sg6 = 220 >> 2, sb5 = 255 >> 3;
      for (int y = y_lo; y <= y_hi; ++y) {
        const size_t row = static_cast<size_t>(y) * static_cast<size_t>(SCREEN_W);
        const int hw = LATTER_ROUND_DISPLAY ? circle_half_span(y, inset) : (SCREEN_W / 2 - 1);
        if (hw > 0) {
          const int lx = std::max(0, SCREEN_CX - hw);
          const int rx = std::min(SCREEN_W - 1, SCREEN_CX + hw);
          for (int x = lx; x <= rx; ++x) {
            LATTER_CHECK(dst_fused[row + static_cast<size_t>(x)] == dst_inplace[row + static_cast<size_t>(x)]);
            if (y == check_y) {
              const uint16_t p = decode_hw_rgb565_raw(src[row + static_cast<size_t>(x)]);
              const int dr = (p >> 11) & 0x1F, dg = (p >> 5) & 0x3F, db = p & 0x1F;
              const int d = kRefBayer4[y & 3][x & 3];
              const uint16_t ref_raw = static_cast<uint16_t>((((sr5 * check_a + dr * check_inv + d) >> 8) << 11) |
                                                             (((sg6 * check_a + dg * check_inv + d) >> 8) << 5) |
                                                             ((sb5 * check_a + db * check_inv + d) >> 8));
              LATTER_CHECK(dst_fused[row + static_cast<size_t>(x)] == encode_hw_rgb565_raw(ref_raw));
            }
          }
        }
      }

      // Verify top of wash is strictly brighter than bottom (vertical falloff) and brighter than unwashed sky.
      const uint16_t top_px = decode_hw_rgb565_raw(
          dst_fused[static_cast<size_t>(y_lo + 8) * SCREEN_W + static_cast<size_t>(SCREEN_CX)]);
      const uint16_t bot_px = decode_hw_rgb565_raw(
          dst_fused[static_cast<size_t>(y_hi - 4) * SCREEN_W + static_cast<size_t>(SCREEN_CX)]);
      const uint16_t raw_bg = decode_hw_rgb565_raw(bg_hw);
      LATTER_CHECK((top_px & 0x1FU) > (bot_px & 0x1FU));
      LATTER_CHECK((bot_px & 0x1FU) > (raw_bg & 0x1FU));
    }
    active_frame_endian_mode() = saved_endian;
  }
}

}  // namespace

void run_ui_layout_compositor_tests() {
  test_ui_layout_hit_table_and_dirty_band();
  test_phase2_glass_and_scissor_compositor();
  test_frame_cadence_and_typed_prefs();
  test_custom_icon_tiers_and_d051();
  test_gui_standardization_d070();
  test_photo_frame_screensaver_day_night_sky_d067();
  test_landscape_geoms_psram_budget_and_utf8_probing();
  test_p4_dma_blit_calm_sky_and_dock_clip();
}

}  // namespace latter_test
