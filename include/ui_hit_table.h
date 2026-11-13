#pragma once

// ==============================================================================
// Latter UI Co-Located Frame Hit Table & Pure-C++20 Geometry (include/ui_hit_table.h)
// Zero ESPHome dependencies so host tests compile standalone in <1s.
// Every interactive widget registers its exact rendered pixel bounds as it draws;
// TouchGestureEngine and render_active_focus_overlay read frame_hits() directly.
// ==============================================================================

#include <cstdint>

namespace latter_ui {

static constexpr uint8_t kHitTap         = 0x01;
static constexpr uint8_t kHitHorizSlider = 0x02;
static constexpr uint8_t kHitFocusSlot   = 0x04;
static constexpr uint8_t kHitFocusCircle = 0x08;

namespace WidgetId {
  static constexpr uint16_t NONE               = 0;

  // Universal navigation & list/grid primitives
  static constexpr uint16_t BACK_BUTTON        = 10;
  static constexpr uint16_t HOME_BAR           = 11;
  static constexpr uint16_t SUBHEADER_PREV     = 12;
  static constexpr uint16_t SUBHEADER_NEXT     = 13;
  static constexpr uint16_t SUBHEADER_ACTION   = 14;
  static constexpr uint16_t CHEVRON_UP         = 15;
  static constexpr uint16_t CHEVRON_DOWN       = 16;
  static constexpr uint16_t MODE_BAR_LEFT      = 17;
  static constexpr uint16_t MODE_BAR_RIGHT     = 18;
  static constexpr uint16_t LIST_SLOT_BASE     = 20;   // 20..24 (param = item index)
  static constexpr uint16_t GRID_CARD_BASE     = 30;   // 30..35 (param = card index)
  static constexpr uint16_t DOCK_SLOT_BASE     = 40;   // 40..43 (param = dock index)
  static constexpr uint16_t MODE_BAR_SLOT_BASE = 44;   // 44..47 (param = mode global index)

  // System takeover & modal buttons
  static constexpr uint16_t ALERT_SNOOZE_BTN   = 50;
  static constexpr uint16_t ALERT_DISMISS_BTN  = 51;
  static constexpr uint16_t NOTIF_OPEN_CAM_BTN = 52;
  static constexpr uint16_t NOTIF_DISMISS_BTN  = 53;
  static constexpr uint16_t VA_CANCEL_BTN      = 54;
  static constexpr uint16_t HIBERNATE_CANCEL   = 55;

  static constexpr uint16_t CONFIRM_CANCEL     = 100;
  static constexpr uint16_t CONFIRM_ACCEPT     = 101;

  // Games launcher & modals
  static constexpr uint16_t GAME_OVER_CTA      = 110;
  static constexpr uint16_t GAMES_PLAY_BTN     = 111;
  static constexpr uint16_t GAMES_SOUND_BTN    = 112;
  static constexpr uint16_t GAMES_PREV_BTN     = 113;
  static constexpr uint16_t GAMES_NEXT_BTN     = 114;
  static constexpr uint16_t GAMES_HERO_CARD    = 115;

  // Music Now Playing & overlays
  static constexpr uint16_t MUSIC_QUEUE_CLEAR  = 120;
  static constexpr uint16_t MUSIC_PREV_BTN     = 121;
  static constexpr uint16_t MUSIC_PLAY_PAUSE   = 122;
  static constexpr uint16_t MUSIC_NEXT_BTN     = 123;
  static constexpr uint16_t MUSIC_SCRUB_BAR    = 124;
  static constexpr uint16_t MUSIC_BROWSE_BTN   = 125;
  static constexpr uint16_t MUSIC_SPEAKER_BTN  = 126;
  static constexpr uint16_t MUSIC_SHUFFLE_BTN  = 127;
  static constexpr uint16_t MUSIC_REPEAT_BTN   = 128;
  static constexpr uint16_t MUSIC_ART_TAP      = 129;
  static constexpr uint16_t MUSIC_VOL_SLIDER   = 135;
  static constexpr uint16_t MUSIC_FAV_BTN      = 136;
  static constexpr uint16_t MUSIC_QUEUE_BTN    = 137;
  static constexpr uint16_t MUSIC_SPK_GROUP    = 150;  // + row (0..4), param = spk_idx

  // Shared HSV wheel & 4-column stepper
  static constexpr uint16_t COLOR_WHEEL_RING   = 130;
  static constexpr uint16_t COLOR_WHEEL_SAVE   = 131;
  static constexpr uint16_t COLOR_SWATCH_BASE  = 160;  // 160..169 (param = swatch index)
  static constexpr uint16_t STEPPER_BASE       = 140;  // 140..143 (col 0..3)

  // Universal Remote (Page 7)
  static constexpr uint16_t REMOTE_PRIMARY_PWR = 200;
  static constexpr uint16_t REMOTE_SLOT_CARD   = 210;  // + row (0..3), param = slot_idx
  static constexpr uint16_t REMOTE_SEC_PWR     = 220;  // + row (0..3), param = slot_idx
  static constexpr uint16_t REMOTE_SLOT_PILL   = 230;
  static constexpr uint16_t REMOTE_DPAD_UP     = 231;
  static constexpr uint16_t REMOTE_DPAD_DOWN   = 232;
  static constexpr uint16_t REMOTE_DPAD_LEFT   = 233;
  static constexpr uint16_t REMOTE_DPAD_RIGHT  = 234;
  static constexpr uint16_t REMOTE_DPAD_OK     = 235;
  static constexpr uint16_t REMOTE_NAV_BACK    = 236;
  static constexpr uint16_t REMOTE_NAV_HOME    = 237;
  static constexpr uint16_t REMOTE_PLAY_PAUSE  = 238;
  static constexpr uint16_t REMOTE_MUTE        = 239;
  static constexpr uint16_t REMOTE_VOL_DOWN    = 240;
  static constexpr uint16_t REMOTE_VOL_UP      = 241;
  static constexpr uint16_t REMOTE_MORE_BTN    = 242;
  static constexpr uint16_t REMOTE_EXTRA_BASE  = 250;  // 250..255 (param = cmd index)

  // Security PIN pad modal
  static constexpr uint16_t PIN_PAD_KEY_BASE   = 300;  // 300..309 digits 0..9
  static constexpr uint16_t PIN_PAD_BACKSPACE  = 310;
  static constexpr uint16_t PIN_PAD_SUBMIT     = 311;
  static constexpr uint16_t PIN_PAD_CANCEL     = 312;

  // System Settings (Page 4)
  static constexpr uint16_t SYS_TOGGLE_ROW     = 400;  // + slot_idx (0..4)
  static constexpr uint16_t SYS_BRI_SLIDER     = 410;
  static constexpr uint16_t SYS_VOL_SLIDER     = 411;
  static constexpr uint16_t SYS_PRIMARY_BTN    = 412;
  static constexpr uint16_t SYS_SECONDARY_BTN  = 413;

  // Clock & Home Dashboard (Page 0 / Page 1)
  static constexpr uint16_t HOME_WEATHER_HERO  = 500;
  static constexpr uint16_t HOME_TIME_READOUT  = 501;
  static constexpr uint16_t HOME_ACTIVITY_CARD = 502;
  static constexpr uint16_t HOME_ACTIVITY_PREV = 503;
  static constexpr uint16_t HOME_ACTIVITY_NEXT = 504;
  static constexpr uint16_t HOME_QUICK_BTN     = 505;
  static constexpr uint16_t HOME_BRI_SLIDER    = 510;
  static constexpr uint16_t HOME_VOL_SLIDER    = 511;
  static constexpr uint16_t HOME_QUICK_TILE    = 520;  // 520..525

  // Weather & Forecast (Page 5)
  static constexpr uint16_t WEATHER_MODE_BTN   = 550;

  // Area Control & Whole-Home Hub (Page 6 / Page 9)
  static constexpr uint16_t AREA_PICKER_PILL   = 600;
  static constexpr uint16_t AREA_CARD_TAP      = 610;  // 610..612 (param = slot_idx)
  static constexpr uint16_t AREA_CARD_TOGGLE   = 615;  // 615..617 (param = slot_idx)
  static constexpr uint16_t AREA_CARD_SLIDER   = 620;  // 620..622 (param = slot_idx)
  static constexpr uint16_t LIGHT_ORB_BTN      = 630;
  static constexpr uint16_t LIGHT_BRI_SLIDER   = 631;
  static constexpr uint16_t LIGHT_TEMP_SLIDER  = 632;
  static constexpr uint16_t LIGHT_COLOR_BTN    = 633;
  static constexpr uint16_t CLIMATE_MINUS_BTN  = 640;
  static constexpr uint16_t CLIMATE_PLUS_BTN   = 641;
  static constexpr uint16_t CLIMATE_MODE_BASE  = 642;  // 642..646
  static constexpr uint16_t COVER_OPEN_BTN     = 650;
  static constexpr uint16_t COVER_STOP_BTN     = 651;
  static constexpr uint16_t COVER_CLOSE_BTN    = 652;
  static constexpr uint16_t COVER_POS_SLIDER   = 653;
  static constexpr uint16_t LOCK_ACTION_BTN    = 660;
  static constexpr uint16_t ALARM_MODE_BASE    = 665;  // 665..669
  static constexpr uint16_t HUB_DOMAIN_CARD    = 680;  // 680..685 (param = domain_idx)
  static constexpr uint16_t HUB_ROW_ACTION     = 690;  // 690..694 (param = item_idx)

  // Tools & Comms (Page 8)
  static constexpr uint16_t TOOLS_CARD_BASE    = 700;  // 700..705 (param = subpage)
  static constexpr uint16_t TIMER_TAB_BASE     = 710;  // 710..712 (param = slot 0..2)
  static constexpr uint16_t TIMER_PRESET_BASE  = 715;  // 715..718 (param = minutes/seconds)
  static constexpr uint16_t TIMER_PRIMARY_BTN  = 720;
  static constexpr uint16_t TIMER_RESET_BTN    = 721;
  static constexpr uint16_t SW_PRIMARY_BTN     = 725;
  static constexpr uint16_t SW_RESET_BTN       = 726;
  static constexpr uint16_t SW_STYLE_BTN       = 727;
  static constexpr uint16_t SW_LAP_BTN         = 728;
  static constexpr uint16_t ALARM_CARD_BASE    = 730;  // 730..732 (param = alarm 0..2)
  static constexpr uint16_t ALARM_TOGGLE_BASE  = 735;  // 735..737 (param = alarm 0..2)
  static constexpr uint16_t ALARM_RECUR_BTN    = 740;
  static constexpr uint16_t ALARM_SAVE_BTN     = 741;
  static constexpr uint16_t CALC_KEY_BASE      = 750;  // 750..769 (param = key code)
  static constexpr uint16_t DICE_ROLL_BTN      = 770;
  static constexpr uint16_t DICE_COUNT_BTN     = 771;
  static constexpr uint16_t COIN_FLIP_BTN      = 775;
  static constexpr uint16_t LEVEL_ZERO_BTN     = 781;
  static constexpr uint16_t CAL_EVENT_BASE     = 790;  // 790..794 (param = event idx)
  static constexpr uint16_t CAL_QUICK_ADD_BTN  = 795;
  static constexpr uint16_t CAL_PRESET_BASE    = 796;  // 796..800 (param = preset idx 0..4)
  static constexpr uint16_t CAL_ZOOM_IN_BTN    = 802;
  static constexpr uint16_t CAL_ZOOM_OUT_BTN   = 803;
  static constexpr uint16_t INTERCOM_ROW_BASE  = 810;  // 810..814 (param = target idx)
  static constexpr uint16_t INTERCOM_PTT_BTN   = 815;
  static constexpr uint16_t INTERCOM_MODE_BTN  = 816;
  static constexpr uint16_t CAM_PREV_BTN       = 820;
  static constexpr uint16_t CAM_NEXT_BTN       = 821;
  static constexpr uint16_t CAM_LISTEN_BTN     = 822;
  static constexpr uint16_t CAM_PTT_BTN        = 823;
  static constexpr uint16_t CAM_REPLY_MENU_BTN = 824;
  static constexpr uint16_t CAM_NOISE_BTN      = 825;
  static constexpr uint16_t CAM_ORIENT_BTN     = 826;
  static constexpr uint16_t CAM_REPLY_BASE     = 830;  // 830..833 (param = reply idx)
}  // namespace WidgetId

struct HitRegion {
  int16_t x0{0};
  int16_t y0{0};
  int16_t x1{0};
  int16_t y1{0};
  uint16_t widget_id{0};
  int16_t param{0};
  uint8_t flags{0};
  uint8_t hit_pad{0};

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
  constexpr bool contains_padded(int tx, int ty) const {
    const int pad = static_cast<int>(hit_pad);
    return tx >= (static_cast<int>(x0) - pad) && tx <= (static_cast<int>(x1) + pad) &&
           ty >= (static_cast<int>(y0) - pad) && ty <= (static_cast<int>(y1) + pad);
  }
  float norm_x(int tx) const {
    const int span = static_cast<int>(x1) - static_cast<int>(x0);
    if (span <= 0) return 0.0f;
    if (tx <= x0) return 0.0f;
    if (tx >= x1) return 1.0f;
    return static_cast<float>(tx - x0) / static_cast<float>(span);
  }
};
static_assert(sizeof(HitRegion) == 14, "HitRegion must stay compact at 14 bytes");

struct FrameHitTable {
  static constexpr uint8_t kMaxRegions = 48;
  HitRegion regions[kMaxRegions]{};
  uint8_t count{0};

  void clear() { count = 0; }

  void add_rect(int x0, int y0, int w, int h, uint16_t widget_id,
                uint8_t flags = kHitTap, int16_t param = 0, int hit_pad = 0);

  void add_centered_rect(int cx, int cy, int w, int h, uint16_t widget_id,
                         uint8_t flags = kHitTap, int16_t param = 0, int hit_pad = 0) {
    add_rect(cx - w / 2, cy - h / 2, w, h, widget_id, flags, param, hit_pad);
  }

  void add_circle(int cx, int cy, int r, uint16_t widget_id,
                  uint8_t flags = kHitTap, int16_t param = 0, int hit_pad = 0);

  const HitRegion *hit_test(int tx, int ty, uint8_t required_flags = kHitTap) const;
  const HitRegion *find_by_id(uint16_t widget_id) const;
  const HitRegion *find_by_id_param(uint16_t widget_id, int16_t param) const;
  int focus_count() const;
  const HitRegion *focus_at(int focus_idx) const;
};

FrameHitTable &frame_hits();

// ==============================================================================
// Pure C++20 Multi-Geometry Scaling & Safe-Canvas Engine (320x480 .. 720x720)
// Shared by compile-time UiLayout constants (ui_layout.h) and host verification.
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
  int dock_pips_y{404};
  int app_dock_pips_y{408};
  int dock_r{27};
  int celestial_x{100};
  int celestial_y{92};
  int battery_cx{233};
  int battery_y{435};
  int list_card_w{276};
  int list_card_left{95};
  int slot5_stride{63};
  int slot5_h{44};
  int slot5_cy{236};
  int area_card3_stride{76};
  int area_card3_h{64};
  int area_card3_w{316};
  int area_card3_cy{208};
  int tool_card3_stride{100};
  int tool_card3_h{74};
  int tool_card3_w{316};
  int tool_card3_cy{222};
  int tools_col_off{68};
  int tools_row_stride{105};
  int tools_grid_cy{236};
  int home_date_y{52};
  int home_clock_y{128};
  int home_weather_y{200};
  int drag_start{6};
  int swipe_min{24};
  int tap_max{30};
  int dock_hit_r{43};

  constexpr int slot5_y(int idx_0_to_4) const {
    return slot5_cy + (idx_0_to_4 - 2) * slot5_stride;
  }
  constexpr int area_card3_y(int idx_0_to_2) const {
    return area_card3_cy + (idx_0_to_2 - 1) * area_card3_stride;
  }
  constexpr int tool_card3_y(int idx_0_to_2) const {
    return tool_card3_cy + (idx_0_to_2 - 1) * tool_card3_stride;
  }
  constexpr int tools_row_y(int row_0_to_2) const {
    return tools_grid_cy + (row_0_to_2 - 1) * tools_row_stride;
  }
};

using PanelLayoutMetrics = UiPanelLayout;

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
  L.dock_pips_y = is_round ? scale_min_for(404, h) : (h - scale_y_for(27, h));
  L.app_dock_pips_y = is_round ? scale_min_for(408, h) : (h - scale_y_for(27, h));
  L.dock_r = (w < 360) ? 24 : ((w > 520) ? 40 : 27);
  L.celestial_x = is_round ? scale_min_for(100, w) : ((w < 380) ? 44 : (is_wearable ? 68 : ((w > 520) ? 118 : 78)));
  L.celestial_y = is_round ? scale_min_for(92, h) : ((w < 380) ? 72 : ((h > 520) ? 118 : 78));
  L.battery_cx = is_round ? L.cx : (is_wearable ? (w - 68) : ((w < 380) ? (w - 36) : ((w > 520) ? (w - 86) : (w - 58))));
  L.battery_y = is_round ? scale_min_for(435, h) : (is_wearable ? 43 : ((w < 380) ? 24 : ((h > 520) ? 42 : 28)));
  L.list_card_w = (w < 380) ? (w - 88) : ((w > 520) ? ((w * 276) / 466) : 276);
  L.list_card_left = L.cx - (L.list_card_w / 2);
  L.slot5_stride = (h > 520) ? ((h * 63) / 466) : 63;
  L.slot5_h = (h > 520) ? ((h * 44) / 466) : 44;
  L.slot5_cy = (h > 520) ? L.cy : 236;
  L.area_card3_stride = (h > 520) ? ((h * 76) / 466) : 76;
  L.area_card3_h = (h > 520) ? ((h * 64) / 466) : 64;
  L.area_card3_w = (w < 380) ? (w - 68) : ((w > 520) ? ((w * 316) / 466) : 316);
  L.area_card3_cy = (h > 520) ? ((h * 208) / 466) : 208;
  L.tool_card3_stride = (h > 520) ? ((h * 100) / 466) : 100;
  L.tool_card3_h = (h > 520) ? ((h * 74) / 466) : 74;
  L.tool_card3_w = (w < 380) ? (w - 36) : ((w > 520) ? ((w * 316) / 466) : 316);
  L.tool_card3_cy = (h > 520) ? ((h * 222) / 466) : 222;
  L.tools_col_off = (w < 380) ? 61 : ((w > 520) ? ((w * 74) / 466) : 68);
  L.tools_row_stride = (h > 520) ? ((h * 105) / 466) : 105;
  L.tools_grid_cy = (h > 520) ? L.cy : 236;
  L.home_date_y = (h > 520) ? ((h * 52) / 466) : 52;
  L.home_clock_y = (h > 520) ? ((h * 128) / 466) : 128;
  L.home_weather_y = (h > 520) ? ((h * 200) / 466) : 200;
  L.drag_start = mm_to_px_for(0.6f, ppi);
  L.swipe_min = mm_to_px_for(2.3f, ppi);
  L.tap_max = mm_to_px_for(2.9f, ppi);
  const int min_dock_hit = mm_to_px_for(3.6f, ppi);
  L.dock_hit_r = ((L.dock_r + 16) > min_dock_hit) ? (L.dock_r + 16) : min_dock_hit;
  return L;
}

// ==============================================================================
// Pure C++20 Standardized Touch Timing Constants, Gesture State & TouchRouter
// Shared between TouchGestureEngine (ui_touch_engine.h) and host unit tests.
// ==============================================================================
static constexpr uint32_t TOUCH_DEBOUNCE_RAPID     = 100;  // ms: Rapid steppers
static constexpr uint32_t TOUCH_DEBOUNCE_BUTTON    = 150;  // ms: Standard buttons & pills
static constexpr uint32_t TOUCH_DEBOUNCE_WAKE      = 180;  // ms: Post-wake settling lockout
static constexpr uint32_t TOUCH_DEBOUNCE_DISMISS   = 200;  // ms: Overlay/HUD dismissal
static constexpr uint32_t TOUCH_DEBOUNCE_SCROLL    = 250;  // ms: Page transitions & chevrons
static constexpr uint32_t TOUCH_GUARD_DISMISS      = 250;  // ms: Minimum post-wake delay
static constexpr uint32_t TOUCH_POST_SWIPE_LOCKOUT = 200;  // ms: Ignore moves right after swipe

inline uint32_t &touch_last_tap_ms() {
  static uint32_t s_last_tap_ms = 0;
  return s_last_tap_ms;
}

inline bool touch_allow_tap(uint32_t now_ms, uint32_t cooldown_ms = TOUCH_DEBOUNCE_BUTTON) {
  uint32_t &last = touch_last_tap_ms();
  if (now_ms - last <= cooldown_ms) return false;
  last = now_ms;
  return true;
}

struct TouchPt {
  int x = 0;
  int y = 0;
};

struct TouchGestureState {
  int start_x = 0;
  int start_y = 0;
  int last_x = 0;
  int last_y = 0;
  int max_travel = 0;
  int sample_count = 0;
  uint32_t start_ms = 0;
  uint32_t last_touch_ms = 0;
  uint32_t last_swipe_ms = 0;
  int start_page = 0;
  int start_context = 0;
  bool active = false;
  bool swipe_committed = false;
};

inline TouchGestureState &touch_gesture() {
  static TouchGestureState st;
  return st;
}

struct TouchRouter {
  inline bool allow_tap(uint32_t now_ms, uint32_t cooldown_ms = TOUCH_DEBOUNCE_BUTTON) const {
    return touch_allow_tap(now_ms, cooldown_ms);
  }

  inline void note_tap(uint32_t now_ms) const {
    touch_last_tap_ms() = now_ms;
  }

  template <typename TouchT, typename TouchesT, typename HandlerFn>
  inline void dispatch_press(const TouchT &touch, const TouchesT & /*touches*/, HandlerFn &&handler) {
    handler(static_cast<int>(touch.x), static_cast<int>(touch.y));
  }

  template <typename TouchesT, typename HandlerFn>
  inline void dispatch_move(const TouchesT &touches, HandlerFn &&handler) {
    if (touches.empty()) return;
    handler(static_cast<int>(touches[0].x), static_cast<int>(touches[0].y));
  }

  template <typename HandlerFn>
  inline void dispatch_release(HandlerFn &&handler) {
    handler();
  }
};

inline TouchRouter &touch_router() {
  static TouchRouter s_router;
  return s_router;
}

}  // namespace latter_ui
