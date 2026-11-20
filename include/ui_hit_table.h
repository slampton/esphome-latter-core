#pragma once

// ==============================================================================
// Latter UI Co-Located Frame Hit Table & Pure-C++20 Geometry (include/ui_hit_table.h)
// Zero ESPHome dependencies so host tests compile standalone in <1s.
// Every interactive widget registers its exact rendered pixel bounds as it draws;
// TouchGestureEngine and render_active_focus_overlay read frame_hits() directly.
// ==============================================================================

#include <algorithm>
#include <cstdint>

#include "ui_touch_recognizer.h"

namespace latter_ui {

static constexpr uint8_t kHitTap         = 0x01;
static constexpr uint8_t kHitHorizSlider = 0x02;
static constexpr uint8_t kHitFocusSlot   = 0x04;
static constexpr uint8_t kHitFocusCircle = 0x08;
static constexpr uint8_t kHitLongPress   = 0x10;
static constexpr uint8_t kHitVertSlider  = 0x20;

namespace WidgetId {
  static constexpr uint16_t NONE               = 0;

  // Universal navigation & list/grid primitives
  static constexpr uint16_t BACK_BUTTON        = 10;
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
  static constexpr uint16_t NOTIF_ANSWER_BTN   = 56;
  static constexpr uint16_t NOTIF_VIEW_BTN     = 57;
  static constexpr uint16_t NOTIF_TALK_BTN     = 58;
  static constexpr uint16_t NOTIF_REPLY_BTN    = 59;
  static constexpr uint16_t NOTIF_UNLOCK_BTN   = 60;
  static constexpr uint16_t NOTIF_EXPAND_BTN   = 61;
  static constexpr uint16_t CAM_PTT_BANNER_STOP = 62;

  static constexpr uint16_t CONFIRM_CANCEL     = 100;
  static constexpr uint16_t CONFIRM_ACCEPT     = 101;

  // Games launcher & modals
  static constexpr uint16_t GAME_OVER_CTA      = 110;
  static constexpr uint16_t GAMES_PLAY_BTN     = 111;
  static constexpr uint16_t GAMES_SOUND_BTN    = 112;
  static constexpr uint16_t GAMES_PREV_BTN     = 113;
  static constexpr uint16_t GAMES_NEXT_BTN     = 114;
  static constexpr uint16_t GAMES_HERO_CARD    = 115;

  // Music App (Page 2) — core transport, scrubber & sliders
  static constexpr uint16_t MUSIC_QUEUE_CLEAR  = 120;
  static constexpr uint16_t MUSIC_PREV_BTN     = 121;
  static constexpr uint16_t MUSIC_PLAY_PAUSE   = 122;
  static constexpr uint16_t MUSIC_NEXT_BTN     = 123;
  static constexpr uint16_t MUSIC_SCRUB_BAR    = 124;
  static constexpr uint16_t MUSIC_SHUFFLE_BTN  = 127;
  static constexpr uint16_t MUSIC_REPEAT_BTN   = 128;
  static constexpr uint16_t MUSIC_ART_TAP      = 129;
  static constexpr uint16_t MUSIC_VOL_SLIDER   = 135;

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
  static constexpr uint16_t HOME_AREA_ICON     = 506;
  static constexpr uint16_t HOME_DATE_HERO     = 507;
  static constexpr uint16_t HOME_BRI_SLIDER    = 510;
  static constexpr uint16_t HOME_VOL_SLIDER    = 511;
  static constexpr uint16_t HOME_QUICK_TILE    = 520;  // 520..525
  static constexpr uint16_t HOME_ACT_ROW_0     = 530;  // param = rendered act_items slot index
  static constexpr uint16_t HOME_ACT_ACTION_0  = 531;  // param = rendered act_items slot index
  static constexpr uint16_t HOME_ACT_ROW_1     = 532;  // param = rendered act_items slot index
  static constexpr uint16_t HOME_ACT_ACTION_1  = 533;  // param = rendered act_items slot index
  static constexpr uint16_t HOME_ACT_EXPAND    = 534;  // opens Active Items overlay (home_subpage = 2)

  // Area Control & Home Hub (Page 6 / Page 9)
  static constexpr uint16_t AREA_PICKER_PILL   = 600;
  static constexpr uint16_t AREA_CARD_TAP      = 610;  // 610..612 (param = slot_idx)
  static constexpr uint16_t AREA_CARD_TOGGLE   = 615;  // 615..617 (param = slot_idx)
  static constexpr uint16_t AREA_CARD_SLIDER   = 620;  // 620..622 (param = slot_idx)
  static constexpr uint16_t LIGHT_ORB_BTN      = 630;
  static constexpr uint16_t LIGHT_BRI_SLIDER   = 631;
  static constexpr uint16_t LIGHT_TEMP_SLIDER  = 632;
  static constexpr uint16_t LIGHT_COLOR_BTN    = 633;
  static constexpr uint16_t ENTITY_POWER_BTN   = 634;
  static constexpr uint16_t ENTITY_PRESET_BASE = 635;  // 635..638 (0..3 presets: 25%/50%/75%/100%)
  static constexpr uint16_t CLIMATE_MINUS_BTN  = 640;
  static constexpr uint16_t CLIMATE_PLUS_BTN   = 641;
  static constexpr uint16_t CLIMATE_MODE_BASE  = 642;  // 642..647 (0=heat, 1=cool, 2=heat_cool, 3=off, 4=fan_only, 5=dry)
  static constexpr uint16_t THERMOSTAT_PREV_BTN = 648;
  static constexpr uint16_t THERMOSTAT_NEXT_BTN = 649;
  static constexpr uint16_t COVER_OPEN_BTN     = 650;
  static constexpr uint16_t COVER_STOP_BTN     = 651;
  static constexpr uint16_t COVER_CLOSE_BTN    = 652;
  static constexpr uint16_t COVER_POS_SLIDER   = 653;
  static constexpr uint16_t ENTRY_CARD_BASE    = 656;  // 656..657 (visible slot 0..1, param = global entry idx)
  static constexpr uint16_t ENTRY_ACTION_BASE  = 658;  // 658..659 (visible slot 0..1, param = global entry idx)
  static constexpr uint16_t LOCK_ACTION_BTN    = 660;
  static constexpr uint16_t ENTRY_LOCK_CARD    = 661;
  static constexpr uint16_t ENTRY_GARAGE_CARD  = 662;
  static constexpr uint16_t ALARM_MODE_BASE    = 665;  // 665..669 (0=disarm, 1=arm_home, 2=arm_away, 3=arm_night, 4=arm_vacation)
  static constexpr uint16_t VACUUM_ACTION_BASE = 670;  // 670..675 (0=start, 1=pause, 2=return_to_base, 3=stop, 4=clean_spot, 5=locate)
  static constexpr uint16_t VACUUM_PREV_BTN    = 676;
  static constexpr uint16_t VACUUM_NEXT_BTN    = 677;
  static constexpr uint16_t HUB_DOMAIN_CARD    = 680;  // 680..685 (param = domain_idx)
  static constexpr uint16_t HUB_ROW_ACTION     = 690;  // 690..694 (param = item_idx)

  // Tools, Time & Home Hub Comms (Page 8 / Page 9 / Page 10)
  static constexpr uint16_t TOOLS_CARD_BASE     = 700;  // 700..705 (param = subpage)
  static constexpr uint16_t TIMER_TAB_BASE      = 710;  // 710..712 (param = slot 0..2)
  static constexpr uint16_t TIMER_PRESET_BASE   = 715;  // 715..718 (param = minutes/seconds)
  static constexpr uint16_t TIMER_PRIMARY_BTN   = 720;
  static constexpr uint16_t TIMER_RESET_BTN     = 721;
  static constexpr uint16_t TIMER_PLUS_1M_BTN   = 722;
  static constexpr uint16_t SW_PRIMARY_BTN      = 725;
  static constexpr uint16_t SW_RESET_BTN        = 726;
  static constexpr uint16_t SW_LAP_BTN          = 728;
  static constexpr uint16_t ALARM_CARD_BASE     = 730;  // 730..732 (param = alarm 0..2)
  static constexpr uint16_t ALARM_TOGGLE_BASE   = 735;  // 735..737 (param = alarm 0..2)
  static constexpr uint16_t ALARM_RECUR_BTN     = 740;
  static constexpr uint16_t ALARM_SAVE_BTN      = 741;
  static constexpr uint16_t ALARM_HH_BTN        = 742;
  static constexpr uint16_t ALARM_MM_BTN        = 743;
  static constexpr uint16_t ALARM_SNOOZE_CANCEL = 744;
  static constexpr uint16_t CALC_KEY_BASE       = 750;  // 750..769 (param = key code)
  static constexpr uint16_t DICE_ROLL_BTN       = 770;
  static constexpr uint16_t DICE_COUNT_BTN      = 771;
  static constexpr uint16_t COIN_FLIP_BTN       = 775;
  static constexpr uint16_t LEVEL_ZERO_BTN      = 781;
  static constexpr uint16_t CAL_EVENT_BASE      = 790;  // 790..794 (param = global event idx)
  static constexpr uint16_t CAL_NAV_PREV        = 795;
  static constexpr uint16_t CAL_NAV_NEXT        = 796;
  static constexpr uint16_t CAL_TODAY_BTN       = 797;
  static constexpr uint16_t CAL_ADD_BTN         = 798;
  static constexpr uint16_t CAL_VIEW_MAP_BTN    = 799;
  static constexpr uint16_t CAL_DELETE_BTN      = 800;
  static constexpr uint16_t CAL_ZOOM_IN_BTN     = 802;
  static constexpr uint16_t CAL_ZOOM_OUT_BTN    = 803;
  static constexpr uint16_t INTERCOM_ROW_BASE   = 810;  // 810..814 (param = target idx)
  static constexpr uint16_t INTERCOM_PTT_BTN    = 815;
  static constexpr uint16_t INTERCOM_MODE_BTN   = 816;
  static constexpr uint16_t INTERCOM_SEG_BROADCAST = 817;
  static constexpr uint16_t INTERCOM_SEG_CALL   = 818;
  static constexpr uint16_t INTERCOM_UNLOCK_BTN = 819;
  static constexpr uint16_t CAM_PREV_BTN        = 820;
  static constexpr uint16_t CAM_NEXT_BTN        = 821;
  static constexpr uint16_t CAM_LISTEN_BTN      = 822;
  static constexpr uint16_t CAM_PTT_BTN         = 823;
  static constexpr uint16_t CAM_REPLY_MENU_BTN  = 824;
  static constexpr uint16_t CAM_NOISE_BTN       = 825;
  static constexpr uint16_t CAM_ORIENT_BTN      = 826;
  static constexpr uint16_t CAM_SNAPSHOT_CARD   = 827;
  static constexpr uint16_t CAM_UNLOCK_BTN      = 828;
  static constexpr uint16_t CAM_REPLY_BASE      = 830;  // 830..833 (param = reply idx)
  static constexpr uint16_t PINNED_CARD_BASE    = 840;  // 840..845 (param = global pinned item idx)

  // Dedicated Calendar App (Page 12)
  static constexpr uint16_t CAL_WEEK_DAY_BASE   = 850;  // 850..856 (param = day offset 0..6)
  static constexpr uint16_t CAL_MONTH_WEEK_BASE = 860;  // 860..865 (param = week row 0..5)
  static constexpr uint16_t CAL_MONTH_CELL_BASE = 870;  // 870..911 (param = cell 0..41)
  static constexpr uint16_t CAL_ADD_TITLE_BTN   = 920;
  static constexpr uint16_t CAL_ADD_TITLE_MIC   = 921;
  static constexpr uint16_t CAL_ADD_LOC_BTN     = 922;
  static constexpr uint16_t CAL_ADD_LOC_MIC     = 923;
  static constexpr uint16_t CAL_ADD_CAL_BTN     = 924;
  static constexpr uint16_t CAL_ADD_REPEAT_BTN  = 925;
  static constexpr uint16_t CAL_ADD_NEXT_BTN    = 926;
  static constexpr uint16_t CAL_ADD_DATE_PREV   = 927;
  static constexpr uint16_t CAL_ADD_DATE_NEXT   = 928;
  static constexpr uint16_t CAL_ADD_ALLDAY_BTN  = 929;
  static constexpr uint16_t CAL_ADD_START_BTN   = 930;
  static constexpr uint16_t CAL_ADD_END_BTN     = 931;
  static constexpr uint16_t CAL_ADD_SAVE_BTN    = 932;
  static constexpr uint16_t CAL_PICKER_SLOT_BASE = 935; // 935..939 (param = option idx)

  // Music App clean-sheet mode bar, takeovers, MA action sheet, synced lyrics & speakers (Page 2)
  static constexpr uint16_t MUSIC_MODE_SLOT_BASE      = MODE_BAR_SLOT_BASE;  // 44..47 (param = global slot 0..8)
  static constexpr uint16_t MUSIC_MODE_SLOT_0         = MODE_BAR_SLOT_BASE + 0;
  static constexpr uint16_t MUSIC_MODE_SLOT_1         = MODE_BAR_SLOT_BASE + 1;
  static constexpr uint16_t MUSIC_MODE_SLOT_2         = MODE_BAR_SLOT_BASE + 2;
  static constexpr uint16_t MUSIC_MODE_SLOT_3         = MODE_BAR_SLOT_BASE + 3;

  static constexpr uint16_t MUSIC_SCRUB_OPEN_BTN      = 950;
  static constexpr uint16_t MUSIC_TAKEOVER_BACKDROP   = 951;
  static constexpr uint16_t MUSIC_VOL_DOWN_STEP       = 952;
  static constexpr uint16_t MUSIC_VOL_UP_STEP         = 953;
  static constexpr uint16_t MUSIC_VOL_MUTE_BTN        = 954;
  static constexpr uint16_t MUSIC_SCRUB_STEP_BACK     = 955;
  static constexpr uint16_t MUSIC_SCRUB_STEP_FWD      = 956;
  static constexpr uint16_t MUSIC_FAVORITE_BTN        = 957;
  static constexpr uint16_t MUSIC_HEADER_ENQUEUE_BTN  = 958;
  static constexpr uint16_t MUSIC_ROW_PLAY_BASE       = 960;  // 960..964 (leading 1-tap Play Now orb, param = slot 0..4)
  static constexpr uint16_t MUSIC_ROW_PLAY_0          = 960;
  static constexpr uint16_t MUSIC_ROW_PLAY_1          = 961;
  static constexpr uint16_t MUSIC_ROW_PLAY_2          = 962;
  static constexpr uint16_t MUSIC_ROW_PLAY_3          = 963;
  static constexpr uint16_t MUSIC_ROW_PLAY_4          = 964;

  static constexpr uint16_t MUSIC_ACTION_PLAY_NOW      = 965;
  static constexpr uint16_t MUSIC_ACTION_PLAY_NEXT     = 966;
  static constexpr uint16_t MUSIC_ACTION_ADD_QUEUE     = 967;
  static constexpr uint16_t MUSIC_ACTION_REPLACE_QUEUE = 968;
  static constexpr uint16_t MUSIC_ACTION_START_RADIO   = 969;
  static constexpr uint16_t MUSIC_ACTION_MOVE_UP       = 970;
  static constexpr uint16_t MUSIC_ACTION_MOVE_DOWN     = 971;
  static constexpr uint16_t MUSIC_ACTION_REMOVE_QUEUE  = 972;

  static constexpr uint16_t MUSIC_SPK_CONTROL_BTN      = 975;
  static constexpr uint16_t MUSIC_SPK_TRANSFER_BTN     = 976;
  static constexpr uint16_t MUSIC_SPK_GROUP_BTN        = 977;

  static constexpr uint16_t MUSIC_LYRIC_SYNC_BTN       = 979;
  static constexpr uint16_t MUSIC_LYRIC_LINE_BASE      = 980;  // 980..987 (param = lyric line index)
  static constexpr uint16_t MUSIC_LYRIC_LINE_0         = 980;
  static constexpr uint16_t MUSIC_LYRIC_LINE_1         = 981;
  static constexpr uint16_t MUSIC_LYRIC_LINE_2         = 982;
  static constexpr uint16_t MUSIC_LYRIC_LINE_3         = 983;
  static constexpr uint16_t MUSIC_LYRIC_LINE_4         = 984;
  static constexpr uint16_t MUSIC_LYRIC_LINE_5         = 985;
  static constexpr uint16_t MUSIC_LYRIC_LINE_6         = 986;
  static constexpr uint16_t MUSIC_LYRIC_LINE_7         = 987;

  // Dedicated 4-Tab Cameras App (Page 13)
  static constexpr uint16_t CAM_GRID_TILE_BASE         = 990;  // 990..993 (param = global camera idx)
  static constexpr uint16_t CAM_GRID_PREV_BTN          = 994;
  static constexpr uint16_t CAM_GRID_NEXT_BTN          = 995;
  static constexpr uint16_t CAM_CAROUSEL_PAUSE_BTN     = 997;
  static constexpr uint16_t CAM_VIDEO_TAP              = 998;
  static constexpr uint16_t CAM_SETTINGS_ROW_BASE      = 1000; // 1000..1004 (param = setting index)
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
  float norm_y(int ty) const {
    const int span = static_cast<int>(y1) - static_cast<int>(y0);
    if (span <= 0) return 0.0f;
    if (ty <= y0) return 0.0f;
    if (ty >= y1) return 1.0f;
    return static_cast<float>(ty - y0) / static_cast<float>(span);
  }
};
static_assert(sizeof(HitRegion) == 14, "HitRegion must stay compact at 14 bytes");

struct FrameHitTable {
  static constexpr uint8_t kMaxRegions = 56;
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

// Registers pixel-exact non-overlapping sub-regions for the Home Activity Card
// (body rows, per-row quick-action circles, and optional rightmost expand column),
// stamping each sub-region's param with the exact rendered act_items slot index.
inline void register_home_activity_hit_regions(FrameHitTable &hits, int cx, int card_cy,
                                               int card_w, int card_h,
                                               bool two_rows_rendered, bool has_drop,
                                               int top_slot, int bot_slot,
                                               int act_col_w, int drop_col_w) {
  if (card_w <= 0 || card_h <= 0 || top_slot < 0) return;
  const int card_x0 = cx - (card_w / 2);
  const int card_y0 = card_cy - (card_h / 2);
  const int eff_drop_w = (has_drop && drop_col_w > 0) ? drop_col_w : 0;
  const int eff_act_w = (act_col_w > 0) ? act_col_w : 0;
  const int body_w = (card_w - eff_drop_w - eff_act_w > 1) ? (card_w - eff_drop_w - eff_act_w) : 1;
  const int act_x0 = card_x0 + body_w;
  const int drop_x0 = card_x0 + card_w - eff_drop_w;
  constexpr uint8_t kFlags = kHitTap | kHitFocusSlot;

  if (two_rows_rendered && bot_slot >= 0) {
    const int row0_h = card_h / 2;
    const int row1_y0 = card_y0 + row0_h;
    const int row1_h = card_h - row0_h;
    hits.add_rect(card_x0, card_y0, body_w, row0_h, WidgetId::HOME_ACT_ROW_0, kFlags,
                  static_cast<int16_t>(top_slot));
    if (eff_act_w > 0) {
      hits.add_rect(act_x0, card_y0, eff_act_w, row0_h, WidgetId::HOME_ACT_ACTION_0, kFlags,
                    static_cast<int16_t>(top_slot));
    }
    hits.add_rect(card_x0, row1_y0, body_w, row1_h, WidgetId::HOME_ACT_ROW_1, kFlags,
                  static_cast<int16_t>(bot_slot));
    if (eff_act_w > 0) {
      hits.add_rect(act_x0, row1_y0, eff_act_w, row1_h, WidgetId::HOME_ACT_ACTION_1, kFlags,
                    static_cast<int16_t>(bot_slot));
    }
  } else {
    hits.add_rect(card_x0, card_y0, body_w, card_h, WidgetId::HOME_ACT_ROW_0, kFlags,
                  static_cast<int16_t>(top_slot));
    if (eff_act_w > 0) {
      hits.add_rect(act_x0, card_y0, eff_act_w, card_h, WidgetId::HOME_ACT_ACTION_0, kFlags,
                    static_cast<int16_t>(top_slot));
    }
  }
  if (eff_drop_w > 0) {
    hits.add_rect(drop_x0, card_y0, eff_drop_w, card_h, WidgetId::HOME_ACT_EXPAND, kFlags, 0);
  }
}

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

// Canonical container-proportional custom icon half-size tiers (D-051):
//   1. dock_icon_r_for: ~55-56% optical fill inside 2 * dock_r across Home Dock & all 4 Mode Bars
//   2. card_icon_r_for: ~45% optical fill inside tall 3-card Area / Entry rows
//   3. hero_icon_r_for: center-stage status hero glyph clamped cleanly inside hero_h
inline constexpr int dock_icon_r_for(int dock_r) {
  const int r = (dock_r * 15 + 13) / 27;
  return (r < 8) ? 8 : r;
}

inline constexpr int card_icon_r_for(int card_h) {
  const int r = (card_h * 22 + 50) / 100;
  return (r < 10) ? 10 : r;
}

inline constexpr int hero_icon_r_for(int hero_h, int screen_min_dim) {
  const int lo = scale_s_for(13, screen_min_dim);
  const int hi = scale_s_for(24, screen_min_dim);
  const int raw = (hero_h * 20 + 50) / 100;
  return (raw < lo) ? lo : ((raw > hi) ? hi : raw);
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

inline constexpr int default_corner_r_for(int screen_w, bool is_round, bool is_wearable) noexcept {
#if defined(LATTER_CORNER_R) && defined(LATTER_SCREEN_W) && defined(LATTER_ROUND_DISPLAY) && defined(LATTER_IS_WEARABLE)
  if (screen_w == LATTER_SCREEN_W && is_round == (LATTER_ROUND_DISPLAY != 0) &&
      is_wearable == (LATTER_IS_WEARABLE != 0)) {
    return LATTER_CORNER_R;
  }
#endif
  return is_round ? (screen_w / 2) : (is_wearable ? 98 : 20);
}

inline constexpr int safe_span_for(int y, int screen_w, int screen_h, bool is_round,
                                   bool is_wearable = false, int inset = 12,
                                   int corner_r = -1) {
  if (y < 0 || y >= screen_h || screen_w <= 0) return 0;
  if (is_round) {
    const int r = screen_w / 2;
    const int cy = screen_h / 2;
    const int dy = (y >= cy) ? (y - cy) : (cy - y);
    const int hw = chord_half_span_constexpr(r, dy, false) - inset;
    return (hw > 0) ? (2 * hw) : 0;
  }
  int half_w = (screen_w / 2) - inset;
  const int cr = (corner_r >= 0) ? corner_r : default_corner_r_for(screen_w, is_round, is_wearable);
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
  L.area_card3_w = (w < 380) ? (w - 68) : ((w > 520) ? std::min(480, (w * 316) / 466) : 316);
  L.area_card3_cy = (h > 520) ? ((h * 208) / 466) : 208;
  L.tool_card3_stride = (h > 520) ? ((h * 100) / 466) : 100;
  L.tool_card3_h = (h > 520) ? ((h * 74) / 466) : 74;
  L.tool_card3_w = (w < 380) ? (w - 36) : ((w > 520) ? std::min(480, (w * 316) / 466) : 316);
  L.tool_card3_cy = (h > 520) ? ((h * 222) / 466) : 222;
  L.tools_col_off = (w < 380) ? 61 : ((w > 520) ? ((w * 74) / 466) : 68);
  L.tools_row_stride = (h > 520) ? ((h * 105) / 466) : 105;
  L.tools_grid_cy = (h > 520) ? L.cy : 236;
  L.home_date_y = (h > 520) ? ((h * 52) / 466) : 52;
  L.home_clock_y = (h > 520) ? ((h * 128) / 466) : 128;
  L.home_weather_y = (h > 520) ? ((h * 200) / 466) : 200;
  const auto km = KinematicMetrics::for_ppi(ppi);
  L.drag_start = km.slider_start_px;
  L.tap_max = km.tap_slop_px;
  L.swipe_min = km.swipe_live_px;
  const int min_dock_hit = mm_to_px_for(3.6f, ppi);
  L.dock_hit_r = ((L.dock_r + 16) > min_dock_hit) ? (L.dock_r + 16) : min_dock_hit;
  return L;
}

// ==============================================================================
// Pure C++20 Standardized Touch Timing Constants, Gesture State & TouchRouter
// Shared between TouchGestureEngine (ui_touch_engine.h) and host unit tests.
// ==============================================================================
static constexpr uint32_t TOUCH_DEBOUNCE_RAPID     = 65;   // ms: Repeaters (Vol, Prev/Next, D-Pad, Steppers, Keypad)
static constexpr uint32_t TOUCH_DEBOUNCE_BUTTON    = 110;  // ms: Same-target stateful buttons & pills
static constexpr uint32_t TOUCH_DEBOUNCE_WAKE      = 150;  // ms: Post-wake settling lockout
static constexpr uint32_t TOUCH_DEBOUNCE_DISMISS   = 160;  // ms: Overlay/HUD dismissal
static constexpr uint32_t TOUCH_DEBOUNCE_SCROLL    = 160;  // ms: Page transitions & chevrons
static constexpr uint32_t TOUCH_GUARD_DISMISS      = 220;  // ms: Minimum post-wake delay
static constexpr uint32_t TOUCH_POST_SWIPE_LOCKOUT = 150;  // ms: Ignore moves right after swipe

inline uint32_t &touch_last_tap_ms() {
  static uint32_t s_last_tap_ms = 0;
  return s_last_tap_ms;
}

inline uint32_t &touch_last_target_key() {
  static uint32_t s_last_target_key = 0;
  return s_last_target_key;
}

inline bool touch_allow_tap(uint32_t now_ms, uint32_t cooldown_ms = TOUCH_DEBOUNCE_BUTTON,
                            uint32_t target_key = 0) {
  uint32_t &last = touch_last_tap_ms();
  uint32_t &last_key = touch_last_target_key();
  if (last != 0U) {
    if (target_key != 0U && last_key != 0U && target_key != last_key) {
      // Cross-target tap: zero UI lockout; only guard against <15 ms hardware contact bounce.
      if ((now_ms - last) < 15U) return false;
    } else {
      if ((now_ms - last) <= cooldown_ms) return false;
    }
  }
  last = (now_ms == 0U) ? 1U : now_ms;
  last_key = target_key;
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
  uint16_t long_press_wid = 0;
  int16_t long_press_param = 0;
  bool active = false;
  bool swipe_committed = false;
  bool tap_cancelled = false;
  bool long_press_armed = false;
  bool long_press_fired = false;
  GestureAxisLock axis_lock = GestureAxisLock::None;

  void begin(int tx, int ty, uint32_t now_ms, int page_id, int ctx_id) noexcept {
    start_x = tx;
    start_y = ty;
    last_x = tx;
    last_y = ty;
    max_travel = 0;
    sample_count = 1;
    start_ms = now_ms;
    last_touch_ms = now_ms;
    start_page = page_id;
    start_context = ctx_id;
    long_press_wid = 0;
    long_press_param = 0;
    active = true;
    swipe_committed = false;
    tap_cancelled = false;
    long_press_armed = false;
    long_press_fired = false;
    axis_lock = GestureAxisLock::None;
  }

  void arm_long_press(uint16_t wid, int16_t param = 0) noexcept {
    long_press_wid = wid;
    long_press_param = param;
    long_press_armed = (wid != 0U);
    long_press_fired = false;
  }

  bool should_fire_long_press(uint32_t now_ms) const noexcept {
    return active && long_press_armed && !long_press_fired && !tap_cancelled && !swipe_committed &&
           (now_ms >= start_ms) && ((now_ms - start_ms) >= kinematics::kLongPressHoldMs);
  }

  void mark_long_press_fired() noexcept {
    long_press_fired = true;
    long_press_armed = false;
    tap_cancelled = true;
  }

  void record_move(int tx, int ty, uint32_t now_ms, int ppi) noexcept {
    last_touch_ms = now_ms;
    if (tx == last_x && ty == last_y) return;
    last_x = tx;
    last_y = ty;
    sample_count += 1;
    const int dx = tx - start_x;
    const int dy = ty - start_y;
    const int adx = (dx < 0) ? -dx : dx;
    const int ady = (dy < 0) ? -dy : dy;
    const int cur_dist = (adx > ady) ? adx : ady;
    if (cur_dist > max_travel) max_travel = cur_dist;

    const KinematicMetrics km = KinematicMetrics::for_ppi(ppi);
    // Strict D-049 deadband: exceeding kTapSlopMm irreversibly cancels tap and long-press eligibility.
    if (max_travel > km.tap_slop_px) {
      tap_cancelled = true;
      long_press_armed = false;
    }
    if (axis_lock == GestureAxisLock::None && cur_dist >= km.axis_lock_px) {
      if (is_axis_dominant(dx, dy)) {
        axis_lock = GestureAxisLock::Horizontal;
      } else if (is_axis_dominant(dy, dx)) {
        axis_lock = GestureAxisLock::Vertical;
      }
    }
  }
};

inline TouchGestureState &touch_gesture() {
  static TouchGestureState st;
  return st;
}

struct TouchRouter {
  inline bool allow_tap(uint32_t now_ms, uint32_t cooldown_ms = TOUCH_DEBOUNCE_BUTTON,
                        uint32_t target_key = 0) const {
    return touch_allow_tap(now_ms, cooldown_ms, target_key);
  }

  inline void note_tap(uint32_t now_ms, uint32_t target_key = 0) const {
    touch_last_tap_ms() = (now_ms == 0U) ? 1U : now_ms;
    touch_last_target_key() = target_key;
  }

  inline void clear_tap() const {
    touch_last_tap_ms() = 0U;
    touch_last_target_key() = 0U;
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

inline constexpr int tools_launcher_item_count_for(bool round_display, bool has_imu) noexcept {
  return 4 + (round_display ? 1 : 0) + (has_imu ? 1 : 0);
}

inline constexpr int tools_launcher_page_count(int item_count) noexcept {
  return (item_count <= 6) ? 1 : ((item_count + 5) / 6);
}

inline constexpr void compute_vertical_page_pips_x(
    int screen_w, bool is_round, int ppi,
    int &out_mask_x, int &out_chev_x, int &out_hit_x0, int &out_hit_w, int &out_min_hit_h) noexcept {
  if (is_round) {
    out_mask_x = scale_w_for(395, screen_w);
    out_chev_x = scale_w_for(415, screen_w);
  } else {
    out_mask_x = (screen_w < 380) ? (screen_w - 30) : ((screen_w > 520) ? (screen_w - 88) : (screen_w - 42));
    out_chev_x = (screen_w < 380) ? (screen_w - 15) : ((screen_w > 520) ? (screen_w - 48) : (screen_w - 21));
  }
  const int min_hit = mm_to_px_for(7.0f, ppi);
  const int raw_x0 = out_mask_x - scale_x_for(12, screen_w);
  const int raw_w = screen_w - raw_x0;
  out_hit_w = (raw_w >= min_hit) ? raw_w : ((min_hit <= screen_w) ? min_hit : screen_w);
  out_hit_x0 = screen_w - out_hit_w;
  if (out_hit_x0 < 0) out_hit_x0 = 0;
  out_min_hit_h = min_hit;
}

// ==============================================================================
// Pure-C++20 Canonical Component Radii, Column Caps, Mode Bar & Pill Row Geometry (D-070)
// ==============================================================================
static constexpr int kMaxContentColW = 480;

inline constexpr int radius_card_for(int min_dim) noexcept {
  return scale_s_for(12, min_dim);
}

inline constexpr int radius_key_for(int min_dim) noexcept {
  return scale_s_for(10, min_dim);
}

inline constexpr void compute_mode_bar_slot_xs(int vis_count, int w, bool is_round,
                                               int out_xs[4]) noexcept {
  const int cx = w / 2;
  if (vis_count <= 0) return;
  if (vis_count == 1) {
    out_xs[0] = cx;
  } else if (vis_count == 2) {
    out_xs[0] = cx - scale_x_for(44, w);
    out_xs[1] = cx + scale_x_for(44, w);
  } else if (vis_count == 3) {
    out_xs[0] = cx - scale_x_for(84, w);
    out_xs[1] = cx;
    out_xs[2] = cx + scale_x_for(84, w);
  } else if (is_round) {
    out_xs[0] = scale_w_for(112, w);
    out_xs[1] = scale_w_for(193, w);
    out_xs[2] = scale_w_for(274, w);
    out_xs[3] = scale_w_for(355, w);
  } else {
    const int stride = (w < 380) ? 74 : scale_x_for(98, w);
    out_xs[0] = cx - (stride * 3) / 2;
    out_xs[1] = cx - (stride / 2);
    out_xs[2] = cx + (stride / 2);
    out_xs[3] = cx + (stride * 3) / 2;
  }
}

struct HomeHubPillGeom {
  int cx{0};
  int cy{0};
  int w{0};
  int h{0};
  int hit_w{0};
  int hit_h{0};
};

inline constexpr int band_safe_span_for(
    int cy_band, int h_band, int screen_w, int screen_h, bool is_round,
    bool is_wearable = false, int inset = 14, int corner_r = -1) noexcept {
  const int half_h = (h_band / 2 > 1) ? (h_band / 2) : 1;
  int y_top = cy_band - half_h;
  if (y_top < 0) y_top = 0;
  if (y_top >= screen_h) y_top = screen_h - 1;
  int y_bot = cy_band + half_h;
  if (y_bot < 0) y_bot = 0;
  if (y_bot >= screen_h) y_bot = screen_h - 1;
  const int s_top = safe_span_for(y_top, screen_w, screen_h, is_round, is_wearable, inset, corner_r);
  const int s_bot = safe_span_for(y_bot, screen_w, screen_h, is_round, is_wearable, inset, corner_r);
  return (s_top < s_bot) ? s_top : s_bot;
}

inline constexpr int compute_uniform_stack_w_for(
    int top_cy, int bot_cy, int row_h, int max_w,
    int screen_w, int screen_h, bool is_round, bool is_wearable = false,
    int inset = 14) noexcept {
  const int s_top = band_safe_span_for(top_cy, row_h, screen_w, screen_h, is_round, is_wearable, inset);
  const int s_bot = band_safe_span_for(bot_cy, row_h, screen_w, screen_h, is_round, is_wearable, inset);
  const int safe_w = (s_top < s_bot) ? s_top : s_bot;
  const int desk_cap = (screen_w > 520 && max_w > kMaxContentColW) ? kMaxContentColW : max_w;
  const int screen_cap = (screen_w > 2 * inset) ? (screen_w - 2 * inset) : screen_w;
  int w = safe_w;
  if (desk_cap < w) w = desk_cap;
  if (screen_cap < w) w = screen_cap;
  return (w > 64) ? w : 64;
}

inline constexpr int compute_action_pill_row(
    int count, int cy, int pill_h, int max_row_w, int max_btn_w, int gap,
    int screen_w, int screen_h, bool is_round, bool is_wearable, int ppi,
    HomeHubPillGeom *out_geoms, int inset = 14) noexcept {
  if (count <= 0 || out_geoms == nullptr) return 0;
  const int cx = screen_w / 2;
  const int min_hit = mm_to_px_for(7.0f, ppi);
  const int eff_h = (pill_h > 24) ? pill_h : 24;
  const int hit_h = (eff_h > min_hit) ? eff_h : min_hit;
  const int safe_w = band_safe_span_for(cy, eff_h, screen_w, screen_h, is_round, is_wearable, inset);
  const int desk_cap = (screen_w > 520 && max_row_w > kMaxContentColW) ? kMaxContentColW : max_row_w;
  const int avail_w = (desk_cap < safe_w) ? desk_cap : safe_w;
  int eff_gap = (gap > 2) ? gap : 4;
  int bw = (avail_w - (count - 1) * eff_gap) / count;
  if (bw < 48 && count > 1) {
    eff_gap = 4;
    bw = (avail_w - (count - 1) * eff_gap) / count;
  }
  if (bw < 40) bw = 40;
  if (max_btn_w > 0 && bw > max_btn_w) bw = max_btn_w;
  const int total_w = count * bw + (count - 1) * eff_gap;
  const int x0 = cx - total_w / 2;
  const int hit_w = (count == 1 && bw < min_hit) ? ((min_hit < safe_w) ? min_hit : safe_w) : bw;
  for (int i = 0; i < count; ++i) {
    const int bcx = x0 + i * (bw + eff_gap) + bw / 2;
    out_geoms[i] = HomeHubPillGeom{bcx, cy, bw, eff_h, hit_w, hit_h};
  }
  return count;
}

inline constexpr bool home_hub_uses_two_rows(
    int count, int content_top, int content_bot,
    int screen_w, int screen_h, bool is_round, bool is_wearable, int ppi) noexcept {
  if (count > 4) return true;
  if (count < 4) return false;
  const int avail_h = (content_bot - content_top > 140) ? (content_bot - content_top) : 140;
  const int min_hit = mm_to_px_for(7.0f, ppi);
  const int gap = (scale_x_for(8, screen_w) > 6) ? scale_x_for(8, screen_w) : 6;
  const int est_row_y = content_bot - avail_h / 4;
  const int est_h = (scale_y_for(46, screen_h) > 38) ? scale_y_for(46, screen_h) : 38;
  const int single_safe = band_safe_span_for(est_row_y, est_h, screen_w, screen_h, is_round, is_wearable, 14);
  const int thr = (min_hit < 72) ? min_hit : 72;
  return ((single_safe - 3 * gap) / 4) < thr;
}

inline constexpr int compute_home_hub_pill_deck(
    int count, int deck_top, int content_bot,
    int screen_w, int screen_h, bool is_round, bool is_wearable, int ppi,
    bool two_rows, HomeHubPillGeom *out_geoms) noexcept {
  if (count <= 0 || out_geoms == nullptr) return 0;
  const int cx = screen_w / 2;
  const int deck_h = (content_bot - deck_top > 40) ? (content_bot - deck_top) : 40;
  const int min_hit = mm_to_px_for(7.0f, ppi);
  const int gap = (scale_x_for(8, screen_w) > 6) ? scale_x_for(8, screen_w) : 6;

  if (two_rows && count >= 4) {
    const int row_stride = deck_h / 2;
    int pill_h = row_stride - 5;
    if (pill_h < 30) pill_h = 30;
    const int max_ph = scale_y_for(46, screen_h);
    if (pill_h > max_ph) pill_h = max_ph;
    const int cap_hit_h = (min_hit < row_stride) ? min_hit : row_stride;
    const int hit_h = (pill_h > cap_hit_h) ? pill_h : cap_hit_h;
    const int top_cnt = (count + 1) / 2;
    const int bot_cnt = count - top_cnt;
    const int row_cnts[2] = {top_cnt, bot_cnt};
    int idx = 0;
    for (int r = 0; r < 2; ++r) {
      const int rc = row_cnts[r];
      if (rc <= 0) continue;
      const int row_cy = deck_top + r * row_stride + row_stride / 2;
      const int safe_w = band_safe_span_for(row_cy, pill_h, screen_w, screen_h, is_round, is_wearable, 14);
      const int max_rw = (screen_w > 520) ? std::min(kMaxContentColW, scale_x_for(332, screen_w))
                                          : scale_x_for(332, screen_w);
      const int row_w = (max_rw < safe_w) ? max_rw : safe_w;
      const int raw_pw = (row_w - (rc - 1) * gap) / rc;
      const int pill_w = (raw_pw > 44) ? raw_pw : 44;
      const int total_w = rc * pill_w + (rc - 1) * gap;
      const int x_left = cx - total_w / 2;
      const int hit_w = (rc == 1 && pill_w < min_hit) ? min_hit : pill_w;
      for (int c = 0; c < rc; ++c) {
        const int pcx = x_left + c * (pill_w + gap) + pill_w / 2;
        out_geoms[idx++] = HomeHubPillGeom{pcx, row_cy, pill_w, pill_h, hit_w, hit_h};
      }
    }
    return idx;
  }

  const int row_cy = deck_top + deck_h / 2;
  int pill_h = deck_h - 8;
  const int min_ph = (count == 1) ? 40 : 36;
  const int max_ph = scale_y_for((count == 1) ? 54 : 50, screen_h);
  if (pill_h < min_ph) pill_h = min_ph;
  if (pill_h > max_ph) pill_h = max_ph;
  const int cap_hit_h = (min_hit < deck_h) ? min_hit : deck_h;
  const int hit_h = (pill_h > cap_hit_h) ? pill_h : cap_hit_h;
  const int safe_w = band_safe_span_for(row_cy, pill_h, screen_w, screen_h, is_round, is_wearable, 12);
  if (count == 1) {
    const int max_single_w = scale_x_for(204, screen_w);
    int pill_w = (max_single_w < safe_w) ? max_single_w : safe_w;
    if (pill_w < 64) pill_w = 64;
    const int hit_w = (pill_w > min_hit) ? pill_w : ((min_hit < safe_w) ? min_hit : pill_w);
    out_geoms[0] = HomeHubPillGeom{cx, row_cy, pill_w, pill_h, hit_w, hit_h};
    return 1;
  }

  const int max_rw = (screen_w > 520) ? std::min(kMaxContentColW, scale_x_for(336, screen_w))
                                      : scale_x_for(336, screen_w);
  const int row_w = (max_rw < safe_w) ? max_rw : safe_w;
  const int raw_pw = (row_w - (count - 1) * gap) / count;
  const int pill_w = (raw_pw > 44) ? raw_pw : 44;
  const int total_w = count * pill_w + (count - 1) * gap;
  const int x_left = cx - total_w / 2;
  for (int i = 0; i < count; ++i) {
    const int pcx = x_left + i * (pill_w + gap) + pill_w / 2;
    out_geoms[i] = HomeHubPillGeom{pcx, row_cy, pill_w, pill_h, pill_w, hit_h};
  }
  return count;
}

inline void register_home_hub_pill_deck_hits(
    FrameHitTable &hits, uint16_t base_widget_id,
    const int *codes, const HomeHubPillGeom *geoms, int count) {
  if (codes == nullptr || geoms == nullptr || count <= 0) return;
  for (int i = 0; i < count; ++i) {
    const int hw = (geoms[i].hit_w > geoms[i].w) ? geoms[i].hit_w : geoms[i].w;
    const int hh = (geoms[i].hit_h > geoms[i].h) ? geoms[i].hit_h : geoms[i].h;
    hits.add_centered_rect(geoms[i].cx, geoms[i].cy, hw, hh,
                           static_cast<uint16_t>(base_widget_id + codes[i]),
                           kHitTap | kHitFocusSlot, static_cast<int16_t>(codes[i]));
  }
}

inline void register_thermostat_stepper_hits(
    FrameHitTable &hits, bool show_setpoint_btns,
    int minus_cx, int plus_cx, int hero_cy, int btn_r, int btn_hit_r) {
  if (!show_setpoint_btns || btn_r <= 0) return;
  const int pad = (btn_hit_r > btn_r) ? (btn_hit_r - btn_r) : 0;
  hits.add_circle(minus_cx, hero_cy, btn_r, WidgetId::CLIMATE_MINUS_BTN,
                  kHitTap | kHitFocusCircle, -1, pad);
  hits.add_circle(plus_cx, hero_cy, btn_r, WidgetId::CLIMATE_PLUS_BTN,
                  kHitTap | kHitFocusCircle, 1, pad);
}

inline TouchRouter &touch_router() {
  static TouchRouter s_router;
  return s_router;
}

// ==============================================================================
// Pure-C++20 Music App v3 Zero-Container Geometries (D-043 / D-048 / D-059)
//   - MusicVolumeGeom: Peer Tab 3 (VOLUME) between hdr_y + scale_y(36) and
//     mode_bar_zone_top() - scale_y(10) so the Stadium Mode Bar stays active.
//   - MusicScrubGeom: MusicTakeover::SCRUB on NOW_PLAYING (individual controls,
//     zero outer glass box, MUSIC_TAKEOVER_BACKDROP outside).
//   - MusicActionSheetGeom: MusicTakeover::ACTION_SHEET full-viewport 5 wide
//     action pills between hdr_y and back_y (zero buttons-on-buttons, zero box).
// ==============================================================================
#define LATTER_MUSIC_V2_GEOM_IN_HIT_TABLE 1

inline void register_music_modal_backdrop_hits(
    FrameHitTable &ht, int w, int h, int bounds_x0, int bounds_y0, int bounds_w, int bounds_h) {
  const int x0 = std::clamp(bounds_x0, 0, w);
  const int y0 = std::clamp(bounds_y0, 0, h);
  const int x1 = std::clamp(bounds_x0 + bounds_w, x0, w);
  const int y1 = std::clamp(bounds_y0 + bounds_h, y0, h);
  if (y0 > 0) {
    ht.add_rect(0, 0, w, y0, WidgetId::MUSIC_TAKEOVER_BACKDROP, kHitTap, 0);
  }
  if (y1 < h) {
    ht.add_rect(0, y1, w, h - y1, WidgetId::MUSIC_TAKEOVER_BACKDROP, kHitTap, 1);
  }
  if (x0 > 0 && y1 > y0) {
    ht.add_rect(0, y0, x0, y1 - y0, WidgetId::MUSIC_TAKEOVER_BACKDROP, kHitTap, 2);
  }
  if (x1 < w && y1 > y0) {
    ht.add_rect(x1, y0, w - x1, y1 - y0, WidgetId::MUSIC_TAKEOVER_BACKDROP, kHitTap, 3);
  }
}

struct MusicVolumeGeom {
  int w{466};
  int h{466};
  int cx{233};
  int min_hit{73};
  int hdr_title_y{44};
  int hdr_pct_y{66};
  int hdr_max_w{258};
  int mode_bar_top{328};
  int slider_x0{325};
  int slider_y0{85};
  int slider_w{73};
  int slider_h{231};
  int btn_x0{68};
  int btn_w{243};
  int btn_h{73};
  int btn_vis_h{48};
  int up_y0{85};
  int down_y0{164};
  int mute_y0{243};
};

inline constexpr MusicVolumeGeom compute_music_volume_geom(
    int w, int h, bool round, bool wearable, int ppi) noexcept {
  MusicVolumeGeom g{};
  const auto ly = compute_panel_layout(w, h, round, wearable, ppi);
  const int min_s = (w < h) ? w : h;
  g.w = w;
  g.h = h;
  g.cx = ly.cx;
  g.min_hit = mm_to_px_for(7.0f, ppi);
  g.mode_bar_top = ly.app_dock_y - ly.dock_r - scale_s_for(14, min_s);

  g.hdr_title_y = std::max(18, ly.hdr_y - scale_y_for(4, h));
  g.hdr_pct_y = g.hdr_title_y + scale_y_for(23, h);
  g.hdr_max_w = std::max(120, safe_span_for(g.hdr_title_y, w, h, round, wearable, 18));

  const int top_limit = ly.hdr_y + scale_y_for(36, h);
  const int bot_limit = g.mode_bar_top - scale_y_for(10, h);
  const int avail_h = std::max(3 * g.min_hit + 12, bot_limit - top_limit);
  const int gap_y = std::clamp((avail_h - 3 * g.min_hit) / 4, 6, scale_y_for(14, h));
  const int max_btn_h = std::max(g.min_hit, scale_y_for(68, h));
  g.btn_h = std::clamp((avail_h - 2 * gap_y) / 3, g.min_hit, max_btn_h);
  g.btn_vis_h = std::clamp(scale_y_for(48, h), 38, g.btn_h);

  const int stack_h = 3 * g.btn_h + 2 * gap_y;
  const int body_y0 = top_limit + std::max(0, (bot_limit - top_limit - stack_h) / 2);
  g.slider_y0 = body_y0;
  g.slider_h = stack_h;
  g.up_y0 = body_y0;
  g.down_y0 = g.up_y0 + g.btn_h + gap_y;
  g.mute_y0 = g.down_y0 + g.btn_h + gap_y;

  const int body_cy = g.slider_y0 + g.slider_h / 2;
  const int safe_w = band_safe_span_for(body_cy, g.slider_h, w, h, round, wearable, 14);
  const int max_vol_w = (w > 520) ? 460 : 356;
  const int span_w = std::clamp(std::min(max_vol_w, safe_w), 220, w - 20);
  const int span_x0 = g.cx - span_w / 2;
  const int col_gap = std::max(10, scale_x_for(14, w));
  g.slider_w = (w >= 640) ? std::max(96, g.min_hit) : std::max(64, g.min_hit);
  g.slider_x0 = span_x0 + span_w - g.slider_w;
  g.btn_x0 = span_x0;
  g.btn_w = std::max(g.min_hit, g.slider_x0 - col_gap - g.btn_x0);
  return g;
}

inline void register_music_volume_hits(FrameHitTable &ht, const MusicVolumeGeom &g) {
  ht.add_rect(g.slider_x0, g.slider_y0, g.slider_w, g.slider_h,
              WidgetId::MUSIC_VOL_SLIDER, kHitTap | kHitVertSlider | kHitFocusSlot);
  ht.add_rect(g.btn_x0, g.up_y0, g.btn_w, g.btn_h,
              WidgetId::MUSIC_VOL_UP_STEP, kHitTap | kHitFocusSlot);
  ht.add_rect(g.btn_x0, g.down_y0, g.btn_w, g.btn_h,
              WidgetId::MUSIC_VOL_DOWN_STEP, kHitTap | kHitFocusSlot);
  ht.add_rect(g.btn_x0, g.mute_y0, g.btn_w, g.btn_h,
              WidgetId::MUSIC_VOL_MUTE_BTN, kHitTap | kHitFocusSlot);
}

struct MusicScrubGeom {
  int w{466};
  int h{466};
  int cx{233};
  int min_hit{73};
  int bounds_x0{53};
  int bounds_y0{127};
  int bounds_w{360};
  int bounds_h{235};
  int hdr_title_y{52};
  int hdr_time_y{76};
  int hdr_max_w{272};
  int slider_x0{53};
  int slider_y0{127};
  int slider_w{360};
  int slider_h{73};
  int step_y0{208};
  int step_h{73};
  int step_vis_h{44};
  int step_back_x0{53};
  int step_fwd_x0{237};
  int step_btn_w{176};
  int mode_y0{289};
  int mode_h{73};
  int mode_vis_h{44};
  int shuf_x0{53};
  int rep_x0{176};
  int fav_x0{299};
  int mode_btn_w{114};
};

inline constexpr MusicScrubGeom compute_music_scrub_geom(
    int w, int h, bool round, bool wearable, int ppi) noexcept {
  MusicScrubGeom g{};
  const auto ly = compute_panel_layout(w, h, round, wearable, ppi);
  g.w = w;
  g.h = h;
  g.cx = ly.cx;
  g.min_hit = mm_to_px_for(7.0f, ppi);

  g.hdr_title_y = ly.hdr_y;
  g.hdr_time_y = g.hdr_title_y + scale_y_for(24, h);
  g.hdr_max_w = std::max(120, safe_span_for(g.hdr_title_y, w, h, round, wearable, 18));

  const int gap_y = std::max(6, scale_y_for(8, h));
  const int gap_x = std::max(6, scale_x_for(8, w));
  const int row_h = std::max(g.min_hit, scale_y_for(64, h));
  const int body_h = 3 * row_h + 2 * gap_y;
  const int min_top = g.hdr_time_y + scale_y_for(22, h);
  const int max_top = std::max(min_top, h - body_h - scale_y_for(24, h));
  const int centered_top = (h - body_h) / 2 + scale_y_for(12, h);
  const int body_y0 = std::clamp(centered_top, min_top, max_top);

  const int min_span_w = 3 * g.min_hit + 2 * gap_x;
  const int safe_w = band_safe_span_for(body_y0 + body_h / 2, body_h, w, h, round, wearable, 14);
  const int max_scrub_w = (w > 520) ? 480 : 368;
  const int span_w = std::clamp(
      std::max(min_span_w, std::min(max_scrub_w, safe_w)),
      min_span_w, std::max(min_span_w, w - 16));
  const int span_x0 = g.cx - span_w / 2;

  g.slider_x0 = span_x0;
  g.slider_y0 = body_y0;
  g.slider_w = span_w;
  g.slider_h = row_h;

  g.step_y0 = g.slider_y0 + row_h + gap_y;
  g.step_h = row_h;
  g.step_vis_h = std::clamp(scale_y_for(44, h), 36, row_h);
  g.step_btn_w = std::max(g.min_hit, (span_w - gap_x) / 2);
  g.step_back_x0 = span_x0;
  g.step_fwd_x0 = span_x0 + span_w - g.step_btn_w;

  g.mode_y0 = g.step_y0 + row_h + gap_y;
  g.mode_h = row_h;
  g.mode_vis_h = std::clamp(scale_y_for(44, h), 36, row_h);
  g.mode_btn_w = std::max(g.min_hit, (span_w - 2 * gap_x) / 3);
  g.shuf_x0 = span_x0;
  g.rep_x0 = span_x0 + (span_w - g.mode_btn_w) / 2;
  g.fav_x0 = span_x0 + span_w - g.mode_btn_w;

  g.bounds_x0 = span_x0;
  g.bounds_y0 = g.slider_y0;
  g.bounds_w = span_w;
  g.bounds_h = (g.mode_y0 + g.mode_h) - g.slider_y0;
  return g;
}

inline void register_music_scrub_hits(FrameHitTable &ht, const MusicScrubGeom &g) {
  register_music_modal_backdrop_hits(ht, g.w, g.h, g.bounds_x0, g.bounds_y0, g.bounds_w, g.bounds_h);
  ht.add_rect(g.slider_x0, g.slider_y0, g.slider_w, g.slider_h,
              WidgetId::MUSIC_SCRUB_BAR, kHitTap | kHitHorizSlider | kHitFocusSlot);
  ht.add_rect(g.step_back_x0, g.step_y0, g.step_btn_w, g.step_h,
              WidgetId::MUSIC_SCRUB_STEP_BACK, kHitTap | kHitFocusSlot);
  ht.add_rect(g.step_fwd_x0, g.step_y0, g.step_btn_w, g.step_h,
              WidgetId::MUSIC_SCRUB_STEP_FWD, kHitTap | kHitFocusSlot);
  ht.add_rect(g.shuf_x0, g.mode_y0, g.mode_btn_w, g.mode_h,
              WidgetId::MUSIC_SHUFFLE_BTN, kHitTap | kHitFocusSlot);
  ht.add_rect(g.rep_x0, g.mode_y0, g.mode_btn_w, g.mode_h,
              WidgetId::MUSIC_REPEAT_BTN, kHitTap | kHitFocusSlot);
  ht.add_rect(g.fav_x0, g.mode_y0, g.mode_btn_w, g.mode_h,
              WidgetId::MUSIC_FAVORITE_BTN, kHitTap | kHitFocusSlot);
}

struct MusicActionSheetGeom {
  int w{466};
  int h{466};
  int cx{233};
  int min_hit{73};
  int hdr_title_y{48};
  int hdr_max_w{258};
  int bounds_y0{72};
  int back_y{426};
  int back_hit_y0{404};
  int item_x0[5]{};
  int item_y0[5]{};
  int item_w[5]{};
  int item_h[5]{};
  int item_hit_pad[5]{};
  int item_vis_h{46};
  int item_count{5};
};

inline constexpr MusicActionSheetGeom compute_music_action_sheet_geom(
    int w, int h, bool round, bool wearable, int ppi, int count = 5) noexcept {
  MusicActionSheetGeom g{};
  const auto ly = compute_panel_layout(w, h, round, wearable, ppi);
  g.w = w;
  g.h = h;
  g.cx = ly.cx;
  g.min_hit = mm_to_px_for(7.0f, ppi);
  g.item_count = std::clamp(count, 1, 5);

  g.hdr_title_y = ly.hdr_y;
  g.hdr_max_w = std::max(120, safe_span_for(g.hdr_title_y, w, h, round, wearable, 18));
  g.back_y = ly.back_y;
  g.back_hit_y0 = round ? scale_min_for(404, h) : (h - scale_y_for(64, h));

  const int top_limit = ly.hdr_y + scale_y_for(22, h);
  const int bot_limit = g.back_hit_y0 - scale_y_for(6, h);
  const int avail_h = std::max(g.item_count * 40, bot_limit - top_limit);
  const int gap_y = std::max(4, scale_y_for(6, h));
  const int raw_row_h = (avail_h - std::max(0, g.item_count - 1) * gap_y) / g.item_count;
  const int row_h = std::clamp(raw_row_h, 40, std::max(g.min_hit, scale_y_for(68, h)));
  g.item_vis_h = std::clamp(scale_y_for(46, h), 38, row_h);

  const int total_h = g.item_count * row_h + std::max(0, g.item_count - 1) * gap_y;
  const int start_y = top_limit + std::max(0, (avail_h - total_h) / 2);
  g.bounds_y0 = start_y;

  const int first_cy = start_y + row_h / 2;
  const int last_cy = start_y + (g.item_count - 1) * (row_h + gap_y) + row_h / 2;
  const int safe_top = band_safe_span_for(first_cy, g.item_vis_h, w, h, round, wearable, 14);
  const int safe_bot = band_safe_span_for(last_cy, g.item_vis_h, w, h, round, wearable, 14);
  const int stack_safe_w = std::min(safe_top, safe_bot);
  const int max_sheet_w = (w > 520) ? 480 : 384;
  const int uniform_w = std::clamp(
      std::min(max_sheet_w, stack_safe_w),
      std::max(140, g.min_hit), std::max(140, w - 24));
  const int uniform_x0 = g.cx - uniform_w / 2;

  for (int i = 0; i < g.item_count; ++i) {
    g.item_y0[i] = start_y + i * (row_h + gap_y);
    g.item_h[i] = row_h;
    g.item_hit_pad[i] = (g.min_hit > row_h) ? ((g.min_hit - row_h + 1) / 2) : 0;
    g.item_w[i] = uniform_w;
    g.item_x0[i] = uniform_x0;
  }
  return g;
}

inline void register_music_action_sheet_hits(
    FrameHitTable &ht, const MusicActionSheetGeom &g, const uint16_t *wids) {
  if (g.bounds_y0 > 0) {
    ht.add_rect(0, 0, g.w, g.bounds_y0, WidgetId::MUSIC_TAKEOVER_BACKDROP, kHitTap, 0);
  }
  for (int i = 0; i < g.item_count; ++i) {
    ht.add_rect(g.item_x0[i], g.item_y0[i], g.item_w[i], g.item_h[i],
                wids[i], kHitTap | kHitFocusSlot, static_cast<int16_t>(i), g.item_hit_pad[i]);
  }
}

struct RemoteDpadGeom {
  int cx{233};
  int cy{233};
  int r_in{50};
  int r_out{144};
  int ok_r{42};
  int arr_mid{97};
  int hit_radial{84};
  int hit_tangent{70};
  int outer_pad{10};
};

inline constexpr RemoteDpadGeom compute_remote_dpad_geom(int w, int h, bool round) noexcept {
  RemoteDpadGeom g{};
  const int min_s = (w < h) ? w : h;
  g.cx = w / 2;
  g.cy = round ? (h / 2) : (h / 2 + scale_y_for(8, h));
  g.r_out = (w < 380) ? 128 : ((w > 520) ? 236 : ((!round && w >= 480) ? 180 : 144));
  g.r_in  = (w < 380) ?  46 : ((w > 520) ?  86 : ((!round && w >= 480) ?  60 :  50));
  g.ok_r  = g.r_in - scale_s_for(8, min_s);
  g.arr_mid = (g.r_in + g.r_out) / 2;
  const int ring_t = g.r_out - g.r_in;
  g.hit_radial = (ring_t > 20) ? (ring_t - 10) : ring_t;
  // Inscribed chord at r_in is 2 * r_in * cos(45°) ≈ 1.414 * r_in (zero overlap across 45° diagonals or OK).
  g.hit_tangent = (g.r_in * 14) / 10;
  g.outer_pad = scale_s_for(10, min_s);
  return g;
}

// Polar quadrant resolver for the frosted-glass Remote D-Pad ring: maps 100% of the visual
// ring [r_in, r_out + outer_pad] across exact 45° diagonals matching the rendered dividers.
inline constexpr uint16_t resolve_remote_dpad_widget(int tx, int ty, const RemoteDpadGeom &g) noexcept {
  const int dx = tx - g.cx;
  const int dy = ty - g.cy;
  const int d2 = dx * dx + dy * dy;
  const int r_min = g.r_in;
  const int r_max = g.r_out + g.outer_pad;
  if (d2 < r_min * r_min || d2 > r_max * r_max) return WidgetId::NONE;
  const int adx = (dx < 0) ? -dx : dx;
  const int ady = (dy < 0) ? -dy : dy;
  if (adx >= ady) {
    return (dx < 0) ? WidgetId::REMOTE_DPAD_LEFT : WidgetId::REMOTE_DPAD_RIGHT;
  }
  return (dy < 0) ? WidgetId::REMOTE_DPAD_UP : WidgetId::REMOTE_DPAD_DOWN;
}

// ==============================================================================
// Pure-C++20 Dedicated Cameras App Layout & Uncropped Video Geometry (D-066)
// Shared by page_cameras.cpp, page_ambient_va.cpp & standalone host unit tests.
// ==============================================================================
struct CameraRect {
  int x0{0};
  int y0{0};
  int w{0};
  int h{0};
  constexpr int x1() const noexcept { return x0 + w - 1; }
  constexpr int y1() const noexcept { return y0 + h - 1; }
  constexpr int cx() const noexcept { return x0 + w / 2; }
  constexpr int cy() const noexcept { return y0 + h / 2; }
};

struct CameraGridGeom {
  CameraRect frame{};
  CameraRect tiles[4]{};
  int title_y{34};
  int pager_y{34};
  int mode_bar_cy{398};
  int mode_bar_r{28};
  int mode_slot_xs[4]{143, 203, 263, 323};
};

// Computes the centered destination rectangle for rendering a video/snapshot frame
// of size (src_w, src_h) inside a bounding viewport `bounds`.
// When `fill == false` (Uncropped default): preserves full aspect ratio with zero crop
// (and on round displays inscribes cleanly inside the safe circle radius!).
inline constexpr CameraRect compute_video_display_rect(
    const CameraRect &bounds, int src_w, int src_h, bool fill) noexcept {
  if (bounds.w <= 0 || bounds.h <= 0) return bounds;
  if (src_w <= 0 || src_h <= 0) return bounds;
  if (fill) return bounds;
  const int64_t lhs = static_cast<int64_t>(bounds.w) * static_cast<int64_t>(src_h);
  const int64_t rhs = static_cast<int64_t>(bounds.h) * static_cast<int64_t>(src_w);
  int dst_w = bounds.w;
  int dst_h = bounds.h;
  if (lhs > rhs) {
    dst_w = static_cast<int>((rhs + (src_h / 2)) / src_h);
  } else {
    dst_h = static_cast<int>((lhs + (src_w / 2)) / src_w);
  }
  if (dst_w < 1) dst_w = 1;
  if (dst_h < 1) dst_h = 1;
  CameraRect r{};
  r.w = dst_w;
  r.h = dst_h;
  r.x0 = bounds.x0 + (bounds.w - dst_w) / 2;
  r.y0 = bounds.y0 + (bounds.h - dst_h) / 2;
  return r;
}

// Computes the safe full-viewport uncropped bounding box for Single / Carousel video.
// On round panels (e.g. 466x466), inscribes 16:9 inside the circular bezel so all 4
// corners stay 100% visible with zero bezel clipping!
inline constexpr CameraRect compute_uncropped_video_viewport(
    int panel_w, int panel_h, bool round, bool fill) noexcept {
  if (round && !fill) {
    // Max 16:9 box inscribed in circle of radius R_safe = panel_w/2 - 8:
    // For 466x466: R_safe = 225 -> w = 384, h = 224 -> half-diag = sqrt(192^2 + 112^2) = 222.3 <= 225!
    const int r_safe = (panel_w < panel_h ? panel_w : panel_h) / 2 - 8;
    int w = (r_safe * 171) / 100;  // ~2 * R * 16/sqrt(337) = 1.743 * R
    if (w > panel_w - 16) w = panel_w - 16;
    w = (w > 16) ? (w & ~15) : 16;
    int h = (w * 9) / 16;
    h = (h > 16) ? ((h + 15) & ~15) : 16;
    if (h > panel_h - 32) h = panel_h - 32;
    return CameraRect{(panel_w - w) / 2, (panel_h - h) / 2, w, h};
  }
  return CameraRect{0, 0, panel_w, panel_h};
}

inline constexpr CameraGridGeom compute_camera_grid_geom(
    int w, int h, bool round, bool wearable, int ppi) noexcept {
  CameraGridGeom g{};
  const auto ly = compute_panel_layout(w, h, round, wearable, ppi);
  const int cx = w / 2;
  g.title_y = ly.hdr_y;
  g.mode_bar_cy = ly.app_dock_y;
  g.mode_bar_r = ly.dock_r;
  compute_mode_bar_slot_xs(4, w, round, g.mode_slot_xs);

  const int top_y = ly.hdr_y + scale_y_for(24, h);
  const int bot_y = ly.app_dock_y - ly.dock_r - scale_y_for(12, h);
  int avail_h = std::max(80, bot_y - top_y);
  int avail_w = w - scale_x_for(20, w);
  if (round) {
    // Inscribe 2x2 grid rectangle inside the safe circle chord at both top_y and bot_y
    const int s_top = safe_span_for(top_y, w, h, true, wearable, 14);
    const int s_bot = safe_span_for(bot_y, w, h, true, wearable, 14);
    const int chord = std::min(s_top, s_bot);
    avail_w = std::clamp(chord, 160, w - 36);
    // Keep roughly 4:3 or 5:4 aspect for crisp 2x2 quadrants
    if (avail_h > avail_w) avail_h = avail_w;
  }
  const int fx = cx - avail_w / 2;
  const int fy = top_y + (bot_y - top_y - avail_h) / 2;
  g.frame = CameraRect{fx, fy, avail_w, avail_h};

  const int half_w = avail_w / 2;
  const int half_h = avail_h / 2;
  g.tiles[0] = CameraRect{fx, fy, half_w, half_h};
  g.tiles[1] = CameraRect{fx + half_w, fy, avail_w - half_w, half_h};
  g.tiles[2] = CameraRect{fx, fy + half_h, half_w, avail_h - half_h};
  g.tiles[3] = CameraRect{fx + half_w, fy + half_h, avail_w - half_w, avail_h - half_h};
  g.pager_y = g.title_y;
  return g;
}

}  // namespace latter_ui
