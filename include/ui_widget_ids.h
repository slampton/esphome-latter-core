#pragma once

// ==============================================================================
// Latter UI Hit Flags & Canonical Widget IDs (include/ui_widget_ids.h)
// Pure C++20 UI leaf — zero ESPHome, HAL, or runtime-state includes.
// Every WidgetId enumerator below has verified live render and touch callers.
// ==============================================================================

#include <cstdint>

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
  static constexpr uint16_t CHEVRON_LEFT       = 13;
  static constexpr uint16_t CHEVRON_RIGHT      = 14;
  static constexpr uint16_t CHEVRON_UP         = 15;
  static constexpr uint16_t CHEVRON_DOWN       = 16;
  static constexpr uint16_t MODE_BAR_LEFT      = 17;
  static constexpr uint16_t MODE_BAR_RIGHT     = 18;
  static constexpr int kMaxDockSlotWidgets = 6;
  static constexpr int kMaxListSlotWidgets = 8;

  static constexpr uint16_t LIST_SLOT_BASE     = 20;   // 20..27 (param = item index; up to 8 dynamic rows)
  static constexpr uint16_t GRID_CARD_BASE     = 30;   // 30..35 (param = card index)
  static constexpr uint16_t DOCK_SLOT_BASE     = 40;   // 40..45 (param = dock index; up to 6 dynamic slots)
  static constexpr uint16_t MODE_BAR_SLOT_BASE = 80;   // 80..85 (param = mode global index; up to 6 dynamic slots)

  // System takeover & modal buttons
  static constexpr uint16_t ALERT_SNOOZE_BTN    = 50;
  static constexpr uint16_t ALERT_DISMISS_BTN   = 51;
  static constexpr uint16_t NOTIF_OPEN_CAM_BTN  = 52;
  static constexpr uint16_t NOTIF_DISMISS_BTN   = 53;
  static constexpr uint16_t VA_CANCEL_BTN       = 54;
  static constexpr uint16_t NOTIF_ANSWER_BTN    = 56;
  static constexpr uint16_t NOTIF_VIEW_BTN      = 57;
  static constexpr uint16_t NOTIF_TALK_BTN      = 58;
  static constexpr uint16_t NOTIF_REPLY_BTN     = 59;
  static constexpr uint16_t NOTIF_UNLOCK_BTN    = 60;
  static constexpr uint16_t NOTIF_EXPAND_BTN    = 61;
  static constexpr uint16_t CAM_PTT_BANNER_STOP = 62;

  // Quick Actions Overlay (Priority 9 Takeover — D-074)
  static constexpr uint16_t QUICK_VOL_SLIDER      = 70;
  static constexpr uint16_t QUICK_BRI_SLIDER      = 71;
  static constexpr uint16_t QUICK_BEDTIME_TOGGLE  = 72;
  static constexpr uint16_t QUICK_HIBERNATE_BTN   = 73;
  static constexpr uint16_t QUICK_SCREEN_OFF_BTN  = 74;
  static constexpr uint16_t QUICK_SCREENSAVER_BTN = 75;
  static constexpr uint16_t QUICK_DND_TOGGLE      = 76;

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
  static constexpr uint16_t REMOTE_VOL_DOWN    = 240;
  static constexpr uint16_t REMOTE_VOL_UP      = 241;

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
  static constexpr uint16_t HOME_ACTIVITY_PREV = 503;
  static constexpr uint16_t HOME_ACTIVITY_NEXT = 504;
  static constexpr uint16_t HOME_QUICK_BTN     = 505;
  static constexpr uint16_t HOME_DATE_HERO     = 507;
  static constexpr uint16_t HOME_ACT_ROW_0     = 530;  // param = rendered act_items slot index
  static constexpr uint16_t HOME_ACT_ACTION_0  = 531;  // param = rendered act_items slot index
  static constexpr uint16_t HOME_ACT_ROW_1     = 532;  // param = rendered act_items slot index
  static constexpr uint16_t HOME_ACT_ACTION_1  = 533;  // param = rendered act_items slot index
  static constexpr uint16_t HOME_ACT_EXPAND    = 534;  // opens Active Items overlay (home_subpage = 2)
  static constexpr uint16_t HOME_ACT_OVERLAY_ACTION_BASE = 540;  // 540..544 (right quick-action pill in subpage 2)

  // Area Control & Home Hub (Page 6 / Page 9)
  static constexpr uint16_t AREA_PICKER_PILL    = 600;
  static constexpr uint16_t AREA_CARD_TAP       = 610;  // 610..612 (param = slot_idx)
  static constexpr uint16_t AREA_CARD_TOGGLE    = 615;  // 615..617 (param = slot_idx)
  static constexpr uint16_t LIGHT_ORB_BTN       = 630;
  static constexpr uint16_t LIGHT_BRI_SLIDER    = 631;
  static constexpr uint16_t LIGHT_TEMP_SLIDER   = 632;
  static constexpr uint16_t LIGHT_COLOR_BTN     = 633;
  static constexpr uint16_t ENTITY_PRESET_BASE  = 635;  // 635..638 (0..3 presets: 25%/50%/75%/100%)
  static constexpr uint16_t CLIMATE_MINUS_BTN   = 640;
  static constexpr uint16_t CLIMATE_PLUS_BTN    = 641;
  static constexpr uint16_t CLIMATE_MODE_BASE   = 642;  // 642..647 (0=heat..5=dry)
  static constexpr uint16_t THERMOSTAT_PREV_BTN = 648;
  static constexpr uint16_t THERMOSTAT_NEXT_BTN = 649;
  static constexpr uint16_t ENTRY_ACTION_BASE   = 658;  // 658..659 (visible slot 0..1, param = global entry idx)
  static constexpr uint16_t ALARM_MODE_BASE     = 665;  // 665..669 (0=disarm..4=arm_vacation)
  static constexpr uint16_t VACUUM_ACTION_BASE  = 670;  // 670..675 (0=start..5=locate)
  static constexpr uint16_t VACUUM_PREV_BTN     = 676;
  static constexpr uint16_t VACUUM_NEXT_BTN     = 677;

  // Tools, Time & Home Hub Comms (Page 8 / Page 9 / Page 10)
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
  static constexpr uint16_t ALARM_HH_BTN        = 742;
  static constexpr uint16_t ALARM_MM_BTN        = 743;
  static constexpr uint16_t ALARM_SNOOZE_CANCEL = 744;
  static constexpr uint16_t CALC_KEY_BASE       = 750;  // 750..769 (param = key code)
  static constexpr uint16_t DICE_ROLL_BTN       = 770;
  static constexpr uint16_t COIN_FLIP_BTN       = 775;
  static constexpr uint16_t CAL_EVENT_BASE      = 790;  // 790..794 (param = global event idx)
  static constexpr uint16_t CAL_NAV_PREV        = 795;
  static constexpr uint16_t CAL_NAV_NEXT        = 796;
  static constexpr uint16_t CAL_TODAY_BTN       = 797;
  static constexpr uint16_t CAL_ADD_BTN         = 798;
  static constexpr uint16_t CAL_DELETE_BTN      = 800;
  static constexpr uint16_t INTERCOM_ROW_BASE   = 810;  // 810..817 (param = phrase idx)
  static constexpr uint16_t INTERCOM_SPEAK_BTN  = 818;
  static constexpr uint16_t CAM_PREV_BTN        = 820;
  static constexpr uint16_t CAM_NEXT_BTN        = 821;
  static constexpr uint16_t CAM_LISTEN_BTN      = 822;
  static constexpr uint16_t CAM_PTT_BTN         = 823;
  static constexpr uint16_t CAM_REPLY_MENU_BTN  = 824;
  static constexpr uint16_t CAM_ORIENT_BTN      = 826;
  static constexpr uint16_t CAM_UNLOCK_BTN      = 828;
  static constexpr uint16_t CAM_REPLY_BASE      = 830;  // 830..833 (param = reply idx)

  // Dedicated Calendar App (Page 12)
  static constexpr uint16_t CAL_WEEK_DAY_BASE    = 850;  // 850..856 (param = day offset 0..6)
  static constexpr uint16_t CAL_MONTH_WEEK_BASE  = 860;  // 860..865 (param = week row 0..5)
  static constexpr uint16_t CAL_MONTH_CELL_BASE  = 870;  // 870..911 (param = cell 0..41)
  static constexpr uint16_t CAL_ADD_TITLE_BTN    = 920;
  static constexpr uint16_t CAL_ADD_TITLE_MIC    = 921;
  static constexpr uint16_t CAL_ADD_LOC_BTN      = 922;
  static constexpr uint16_t CAL_ADD_LOC_MIC      = 923;
  static constexpr uint16_t CAL_ADD_CAL_BTN      = 924;
  static constexpr uint16_t CAL_ADD_REPEAT_BTN   = 925;
  static constexpr uint16_t CAL_ADD_NEXT_BTN     = 926;
  static constexpr uint16_t CAL_ADD_DATE_PREV    = 927;
  static constexpr uint16_t CAL_ADD_DATE_NEXT    = 928;
  static constexpr uint16_t CAL_ADD_ALLDAY_BTN   = 929;
  static constexpr uint16_t CAL_ADD_START_BTN    = 930;
  static constexpr uint16_t CAL_ADD_END_BTN      = 931;
  static constexpr uint16_t CAL_ADD_SAVE_BTN     = 932;
  static constexpr uint16_t CAL_PICKER_SLOT_BASE = 935;  // 935..939 (param = option idx)

  // Music App clean-sheet takeovers, MA action sheet, synced lyrics & speakers (Page 2)
  static constexpr uint16_t MUSIC_SCRUB_OPEN_BTN      = 950;
  static constexpr uint16_t MUSIC_TAKEOVER_BACKDROP   = 951;
  static constexpr uint16_t MUSIC_VOL_DOWN_STEP       = 952;
  static constexpr uint16_t MUSIC_VOL_UP_STEP         = 953;
  static constexpr uint16_t MUSIC_VOL_MUTE_BTN        = 954;
  static constexpr uint16_t MUSIC_SCRUB_STEP_BACK     = 955;
  static constexpr uint16_t MUSIC_SCRUB_STEP_FWD      = 956;
  static constexpr uint16_t MUSIC_FAVORITE_BTN        = 957;
  static constexpr uint16_t MUSIC_ROW_PLAY_BASE       = 1080; // 1080..1087 (leading 1-tap Play Now orb, param = slot 0..7)

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

  // Dedicated 4-Tab Cameras App (Page 13)
  static constexpr uint16_t CAM_GRID_TILE_BASE         = 990;  // 990..993 (param = global camera idx)
  static constexpr uint16_t CAM_GRID_PREV_BTN          = 994;
  static constexpr uint16_t CAM_GRID_NEXT_BTN          = 995;
  static constexpr uint16_t CAM_CAROUSEL_PAUSE_BTN     = 997;
  static constexpr uint16_t CAM_VIDEO_TAP              = 998;
  static constexpr uint16_t CAM_SETTINGS_ROW_BASE      = 1000; // 1000..1004 (param = setting index)

  // Non-IMU touch arcade controls (Blocks, Snake, Drop Four — D-081)
  static constexpr uint16_t GAME_CTRL_LEFT             = 1010;
  static constexpr uint16_t GAME_CTRL_RIGHT            = 1011;
  static constexpr uint16_t GAME_CTRL_ROTATE           = 1012;
  static constexpr uint16_t GAME_CTRL_DROP             = 1013;
  static constexpr uint16_t GAME_CTRL_HOLD             = 1014;
  static constexpr uint16_t GAME_BOARD_TAP             = 1015;
  static constexpr uint16_t GAME_MODE_AI               = 1016;
  static constexpr uint16_t GAME_MODE_2P               = 1017;
  static constexpr uint16_t GAME_COL_BASE              = 1020; // 1020..1026 (param = col 0..6)

  // Unified Variable-Span Tile-Grid Engine (Pinned, Tools Launcher & System Root — D-086)
  static constexpr uint16_t PINNED_CARD_BASE           = 1040; // 1040..1055 (16 slots; param = global pinned item idx)
  static constexpr uint16_t TOOLS_CARD_BASE            = 1060; // 1060..1075 (16 slots; param = tool subpage)
  static constexpr uint16_t SYSTEM_CARD_BASE           = 1090; // 1090..1105 (16 slots; param = system menu item idx 0..7)
}  // namespace WidgetId

}  // namespace latter_ui
