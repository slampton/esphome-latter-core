#pragma once

// ==============================================================================
// Latter UI Co-Located Frame Hit Table (include/ui_hit_table.h)
// Every interactive widget registers its exact rendered pixel bounds as it draws.
// TouchGestureEngine and render_active_focus_overlay read frame_hits() directly,
// eliminating draw-vs-touch coordinate drift across all 5 display geometries.
// ==============================================================================

#include <cstdint>

namespace latter_ui {

enum class TouchEventKind : uint8_t {
  TAP = 0,
  SLIDER_DRAG = 1,
  FOCUS_ACTIVATE = 2,
};

static constexpr uint8_t kHitTap         = 0x01;
static constexpr uint8_t kHitHorizSlider = 0x02;
static constexpr uint8_t kHitFocusSlot   = 0x04;
static constexpr uint8_t kHitFocusCircle = 0x08;

using UiActionFn = void (*)(TouchEventKind kind, float norm_val, int16_t param, void *ctx);

namespace WidgetId {
  static constexpr uint16_t NONE               = 0;
  static constexpr uint16_t CONFIRM_CANCEL     = 100;
  static constexpr uint16_t CONFIRM_ACCEPT     = 101;
  static constexpr uint16_t GAME_OVER_CTA      = 110;
  static constexpr uint16_t GAMES_PLAY_BTN     = 111;
  static constexpr uint16_t GAMES_SOUND_BTN    = 112;
  static constexpr uint16_t MUSIC_QUEUE_CLEAR  = 120;
  static constexpr uint16_t COLOR_WHEEL_RING   = 130;
  static constexpr uint16_t COLOR_WHEEL_SAVE   = 131;
  static constexpr uint16_t STEPPER_BASE       = 140;  // 140..143
  static constexpr uint16_t REMOTE_PRIMARY_PWR = 200;
  static constexpr uint16_t REMOTE_SLOT_CARD   = 210;  // + row (0..3), param = slot_idx
  static constexpr uint16_t REMOTE_SEC_PWR     = 220;  // + row (0..3), param = slot_idx
  static constexpr uint16_t PIN_PAD_KEY_BASE   = 300;  // 300..309 digits 0..9, 310 back, 311 submit, 312 cancel
  static constexpr uint16_t PIN_PAD_BACKSPACE  = 310;
  static constexpr uint16_t PIN_PAD_SUBMIT     = 311;
  static constexpr uint16_t PIN_PAD_CANCEL     = 312;
  static constexpr uint16_t SYS_TOGGLE_ROW     = 400;  // + slot_idx
}  // namespace WidgetId

struct HitRegion {
  int16_t x0{0};
  int16_t y0{0};
  int16_t x1{0};
  int16_t y1{0};
  uint16_t widget_id{0};
  uint8_t flags{0};
  int16_t param{0};
  UiActionFn on_event{nullptr};
  void *ctx{nullptr};

  constexpr int cx() const { return (static_cast<int>(x0) + static_cast<int>(x1)) / 2; }
  constexpr int cy() const { return (static_cast<int>(y0) + static_cast<int>(y1)) / 2; }
  constexpr int width() const { return static_cast<int>(x1) - static_cast<int>(x0) + 1; }
  constexpr int height() const { return static_cast<int>(y1) - static_cast<int>(y0) + 1; }
  constexpr int radius() const {
    const int w = width();
    const int h = height();
    return ((w < h) ? w : h) / 2;
  }
  constexpr bool contains(int tx, int ty) const {
    return tx >= x0 && tx <= x1 && ty >= y0 && ty <= y1;
  }
  float norm_x(int tx) const {
    const int span = static_cast<int>(x1) - static_cast<int>(x0);
    if (span <= 0) return 0.0f;
    if (tx <= x0) return 0.0f;
    if (tx >= x1) return 1.0f;
    return static_cast<float>(tx - x0) / static_cast<float>(span);
  }
};

struct FrameHitTable {
  static constexpr uint8_t kMaxRegions = 48;
  HitRegion regions[kMaxRegions]{};
  uint8_t count{0};

  void clear() { count = 0; }

  void add_rect(int x0, int y0, int w, int h, uint16_t widget_id,
                uint8_t flags = kHitTap, int16_t param = 0,
                UiActionFn on_event = nullptr, void *ctx = nullptr);

  void add_centered_rect(int cx, int cy, int w, int h, uint16_t widget_id,
                         uint8_t flags = kHitTap, int16_t param = 0,
                         UiActionFn on_event = nullptr, void *ctx = nullptr) {
    add_rect(cx - w / 2, cy - h / 2, w, h, widget_id, flags, param, on_event, ctx);
  }

  void add_circle(int cx, int cy, int r, uint16_t widget_id,
                  uint8_t flags = kHitTap, int16_t param = 0,
                  UiActionFn on_event = nullptr, void *ctx = nullptr);

  const HitRegion *hit_test(int tx, int ty, uint8_t required_flags = kHitTap) const;
  const HitRegion *find_by_id(uint16_t widget_id) const;
  int focus_count() const;
  const HitRegion *focus_at(int focus_idx) const;
};

FrameHitTable &frame_hits();

// ==============================================================================
// Pure C++20 Multi-Geometry Scaling & Safe-Canvas Engine (320x480 .. 720x720)
// Shared by compile-time LATTER_* constants (ui_core.h / ui_layout.h) and host
// multi-resolution verification tests without requiring ESPHome headers.
// ==============================================================================

inline constexpr int mm_to_px_for(float mm, int ppi) {
  return static_cast<int>((mm * static_cast<float>(ppi)) / 25.4f + 0.5f);
}

inline constexpr int scale_x_for(int v466, int screen_w) {
  return (screen_w > 520) ? ((v466 * screen_w + 233) / 466) : v466;
}

inline constexpr int scale_y_for(int v466, int screen_h) {
  return (screen_h > 520) ? ((v466 * screen_h + 233) / 466) : v466;
}

inline constexpr int scale_s_for(int v466, int screen_min_dim) {
  return (screen_min_dim > 520) ? ((v466 * screen_min_dim + 233) / 466) : v466;
}

inline constexpr int scale_min_for(int v466, int screen_min_dim) {
  return (v466 * screen_min_dim + 233) / 466;
}

inline constexpr int scale_w_for(int v466, int screen_w) {
  return (v466 * screen_w + 233) / 466;
}

inline constexpr int chord_half_span_constexpr(int r, int dy, bool round_nearest = false) {
  if (dy < 0) dy = -dy;
  if (dy == 0) return (r > 0) ? r : 0;
  if (dy >= r) return 0;
  const int n = r * r - dy * dy;
  int x = r - (dy * dy) / (2 * r);
  if (x <= 0) x = 1;
  x = (x + n / x) >> 1;
  x = (x + n / x) >> 1;
  while (x * x > n) x--;
  while ((x + 1) * (x + 1) <= n) x++;
  if (round_nearest && ((x + 1) * x < n)) x++;
  return x;
}

inline constexpr int safe_span_for(int y, int screen_w, int screen_h, bool is_round,
                                   bool is_wearable = false, int inset = 12) {
  if (y < 0 || y >= screen_h || screen_w <= 0) return 0;
  if (is_round) {
    const int r = screen_w / 2;
    const int cy = screen_h / 2;
    const int dy = (y >= cy) ? (y - cy) : (cy - y);
    const int hw = chord_half_span_constexpr(r, dy, false) - inset;
    return (hw > 0) ? (2 * hw) : 0;
  }
  int half_w = (screen_w / 2) - inset;
  const int cr = is_wearable ? 98 : 20;
  if (cr > 8) {
    int dy = 0;
    if (y < cr) dy = cr - y;
    else if (y >= screen_h - cr) dy = y - (screen_h - 1 - cr);
    if (dy > 0 && dy < cr) {
      const int dx_corner = chord_half_span_constexpr(cr, dy, false);
      const int corner_half_w = (screen_w / 2) - (cr - dx_corner) - inset;
      if (corner_half_w < half_w) half_w = corner_half_w;
    }
  }
  return (half_w > 0) ? (2 * half_w) : 0;
}

struct UiPanelLayout {
  int w{466};
  int h{466};
  int cx{233};
  int cy{233};
  bool is_round{true};
  int hdr_y{48};
  int sub_hdr_y{76};
  int back_y{426};
  int home_bar_y{434};
  int dock_y{359};
  int app_dock_y{369};
  int dock_r{27};
  int list_card_w{276};
  int area_card3_w{316};
  int tool_card3_w{316};
  int drag_start{6};
  int swipe_min{24};
  int tap_max{30};
  int dock_hit_r{43};
};

inline constexpr UiPanelLayout compute_panel_layout(int w, int h, bool is_round,
                                                    bool is_wearable, int ppi) {
  UiPanelLayout L{};
  L.w = w;
  L.h = h;
  L.cx = w / 2;
  L.cy = h / 2;
  L.is_round = is_round;
  L.hdr_y = is_round ? scale_min_for(48, h) : (is_wearable ? 36 : ((h > 520) ? 48 : 28));
  L.sub_hdr_y = is_round ? scale_min_for(76, h) : (is_wearable ? 64 : ((h > 520) ? 88 : 56));
  L.back_y = is_round ? scale_min_for(426, h) : (h - scale_y_for(28, h));
  L.home_bar_y = is_round ? scale_min_for(434, h) : (h - scale_y_for(11, h));
  L.dock_y = is_round ? scale_min_for(359, h) : (h - scale_y_for(66, h));
  L.app_dock_y = is_round ? scale_min_for(369, h) : (h - scale_y_for(66, h));
  L.dock_r = (w < 360) ? 24 : ((w > 520) ? 40 : 27);
  L.list_card_w = (w < 380) ? (w - 88) : ((w > 520) ? ((w * 276) / 466) : 276);
  L.area_card3_w = (w < 380) ? (w - 68) : ((w > 520) ? ((w * 316) / 466) : 316);
  L.tool_card3_w = (w < 380) ? (w - 36) : ((w > 520) ? ((w * 316) / 466) : 316);
  L.drag_start = mm_to_px_for(0.6f, ppi);
  L.swipe_min = mm_to_px_for(2.3f, ppi);
  L.tap_max = mm_to_px_for(2.9f, ppi);
  const int min_dock_hit = mm_to_px_for(3.6f, ppi);
  L.dock_hit_r = ((L.dock_r + 16) > min_dock_hit) ? (L.dock_r + 16) : min_dock_hit;
  return L;
}

}  // namespace latter_ui
