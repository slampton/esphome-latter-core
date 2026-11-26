#pragma once

// ==============================================================================
// Latter Core: Active Media, Music Peer Views & Synced Lyrics State PODs
// (include/state/state_media.h)
// ==============================================================================

#include "state/state_common.h"

namespace latter_core {

enum class MusicView : uint8_t {
  NOW_PLAYING  = 0,
  LIBRARY      = 1,
  SPEAKERS     = 2,
  VOLUME       = 3,
  QUEUE        = 4,
  LYRICS       = 5,
  ALBUM_TRACKS = 6,
  INFO         = 7,
  SETTINGS     = 8,
};

enum class MusicDrilldown : uint8_t {
  NONE            = 0,
  LIB_LIST        = 1,
  SPEAKER_ACTIONS = 2,
};

enum class MusicTakeover : uint8_t {
  NONE         = 0,
  SCRUB        = 1,
  ACTION_SHEET = 2,
};

inline constexpr int kMusicPeerTabCount = 9;

inline constexpr bool is_music_ambient_tab(MusicView v) noexcept {
  return v == MusicView::NOW_PLAYING || v == MusicView::LYRICS || v == MusicView::INFO;
}

inline constexpr bool resolve_music_route_target(
    RouteId route_id, MusicView &out_view, MusicDrilldown &out_drilldown) noexcept {
  out_drilldown = MusicDrilldown::NONE;
  switch (route_id) {
    case RouteId::APP_MUSIC:
      out_view = MusicView::NOW_PLAYING;
      return true;
    case RouteId::MUSIC_LIBRARY:
      out_view = MusicView::LIBRARY;
      return true;
    case RouteId::MUSIC_SPEAKERS:
      out_view = MusicView::SPEAKERS;
      return true;
    case RouteId::MUSIC_QUEUE:
      out_view = MusicView::QUEUE;
      return true;
    case RouteId::MUSIC_LYRICS:
      out_view = MusicView::LYRICS;
      return true;
    case RouteId::MUSIC_ALBUM_TRACKS:
      out_view = MusicView::ALBUM_TRACKS;
      return true;
    case RouteId::MUSIC_INFO:
      out_view = MusicView::INFO;
      return true;
    case RouteId::MUSIC_FAVORITES:
    case RouteId::MUSIC_PLAYLISTS:
      out_view = MusicView::LIBRARY;
      out_drilldown = MusicDrilldown::LIB_LIST;
      return true;
    default:
      return false;
  }
}

enum class MusicRepeatMode : uint8_t {
  OFF = 0,
  ALL = 1,
  ONE = 2,
};

inline constexpr uint8_t parse_music_repeat_mode(std::string_view sv) noexcept {
  if (sv == "one" || sv == "ONE" || sv == "repeat_one" || sv == "2") {
    return static_cast<uint8_t>(MusicRepeatMode::ONE);
  }
  if (sv == "all" || sv == "ALL" || sv == "repeat_all" || sv == "1") {
    return static_cast<uint8_t>(MusicRepeatMode::ALL);
  }
  return static_cast<uint8_t>(MusicRepeatMode::OFF);
}

inline constexpr const char *music_repeat_mode_cstr(uint8_t mode) noexcept {
  if (mode == static_cast<uint8_t>(MusicRepeatMode::ONE)) return "one";
  if (mode == static_cast<uint8_t>(MusicRepeatMode::ALL)) return "all";
  return "off";
}

inline constexpr const char *music_repeat_mode_label(uint8_t mode) noexcept {
  if (mode == static_cast<uint8_t>(MusicRepeatMode::ONE)) return "One";
  if (mode == static_cast<uint8_t>(MusicRepeatMode::ALL)) return "All";
  return "Off";
}

inline std::string build_music_track_key(std::string_view title, std::string_view artist) {
  if (title.empty()) return "";
  const std::string esc_title = v3_escape_subfield(title);
  const std::string esc_artist = v3_escape_subfield(artist);
  std::string out;
  out.reserve(esc_title.size() + 1U + esc_artist.size());
  out.append(esc_title);
  out.push_back('|');
  out.append(esc_artist);
  return out;
}

inline constexpr bool is_speech_media_type(std::string_view mt) noexcept {
  return mt == "podcast" || mt == "podcast_episode" || mt == "audiobook";
}


inline constexpr uint16_t kMediaCapPause          = 0x0001U;
inline constexpr uint16_t kMediaCapSeek           = 0x0002U;
inline constexpr uint16_t kMediaCapVolumeSet      = 0x0004U;
inline constexpr uint16_t kMediaCapVolumeMute     = 0x0008U;
inline constexpr uint16_t kMediaCapPreviousTrack  = 0x0010U;
inline constexpr uint16_t kMediaCapNextTrack      = 0x0020U;
inline constexpr uint16_t kMediaCapShuffleSet     = 0x0040U;
inline constexpr uint16_t kMediaCapRepeatSet      = 0x0080U;
inline constexpr uint16_t kMediaCapGrouping       = 0x0100U;
inline constexpr uint16_t kMediaCapMusicAssistant = 0x0200U;
inline constexpr uint16_t kMediaCapFavorite       = 0x0400U;
inline constexpr uint16_t kMediaCapDefault        =
    kMediaCapPause | kMediaCapSeek | kMediaCapVolumeSet | kMediaCapVolumeMute |
    kMediaCapPreviousTrack | kMediaCapNextTrack;


// Protocol v3 active_media state.
struct MediaState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string state = "";
  MediaPlaybackState playback_state = MediaPlaybackState::Idle;
  bool playing = false;
  std::string title = "";
  std::string artist = "";
  std::string album = "";
  float duration = 0.0f;
  float position = 0.0f;
  uint32_t position_updated_ms = 0;
  float volume = 0.5f;
  bool has_volume = false;
  std::string media_type = "";
  std::string art_url = "";
  std::string active_speaker = "";
  std::string speaker_name = "";
  std::string last_speaker = "";
  std::string local_speaker = "";
  bool is_local = false;
  bool is_default_speaker = false;
  bool shuffle = false;
  uint8_t repeat_mode = 0;  // 0 = off, 1 = all, 2 = one
  std::string repeat = "off";
  bool muted = false;
  bool favorite = false;
  int queue_count = 0;
  uint16_t media_caps = 0;

  uint32_t interpolated_position_ms(uint32_t now_ms) const noexcept {
    const float base_s = (position > 0.0f && !std::isnan(position)) ? position : 0.0f;
    double pos_ms = static_cast<double>(base_s) * 1000.0;
    const bool is_playing =
        playing || playback_state == MediaPlaybackState::Playing || state == "playing";
    if (is_playing && position_updated_ms > 0U && now_ms > position_updated_ms) {
      pos_ms += static_cast<double>(now_ms - position_updated_ms);
    }
    if (duration > 0.0f && !std::isnan(duration)) {
      const double max_ms = static_cast<double>(duration) * 1000.0;
      if (pos_ms > max_ms) pos_ms = max_ms;
    }
    return (pos_ms > 0.0) ? static_cast<uint32_t>(pos_ms + 0.5) : 0U;
  }
};


inline MediaState &media_state() {
  static MediaState st;
  return st;
}

inline constexpr size_t kMaxMusicLyricLines = 128;

struct MusicLyricLine {
  int32_t t_ms = -1;  // >= 0 for LRC [mm:ss.xx] synced line; -1 for plain unsynced line
  std::string text = "";
};

struct MusicLyricsState {
  uint32_t rev = 0;
  int latest_req_id = 0;
  std::string track_key = "";
  bool synced = false;
  bool loading = false;
  std::vector<MusicLyricLine> lines{};
  int active_line_idx = -1;
  int scroll_px = 0;
  int target_scroll_px = 0;
  int from_scroll_px = 0;
  uint32_t scroll_anim_start_ms = 0;
  uint32_t manual_override_until_ms = 0;

  // Multi-chunk assembly buffer for latter_set_music_lyrics
  int staging_req_id = -1;
  int staging_expected_chunks = 0;
  int staging_next_chunk = 0;
  std::string staging_track_key = "";
  bool staging_synced = false;
  std::vector<MusicLyricLine> staging_lines{};

  void clear() {
    track_key.clear();
    synced = false;
    loading = false;
    lines.clear();
    active_line_idx = -1;
    scroll_px = 0;
    target_scroll_px = 0;
    from_scroll_px = 0;
    scroll_anim_start_ms = 0;
    manual_override_until_ms = 0;
    staging_req_id = -1;
    staging_expected_chunks = 0;
    staging_next_chunk = 0;
    staging_track_key.clear();
    staging_synced = false;
    staging_lines.clear();
  }

  int find_active_line(uint32_t pos_ms) const noexcept {
    if (!synced || lines.empty()) return -1;
    int best = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
      if (lines[i].t_ms < 0) continue;
      if (static_cast<uint32_t>(lines[i].t_ms) <= pos_ms) {
        best = static_cast<int>(i);
      } else {
        break;
      }
    }
    return best;
  }
};

struct MusicRuntimeState {
  MusicView view{MusicView::NOW_PLAYING};
  MusicDrilldown drilldown{MusicDrilldown::NONE};
  MusicTakeover takeover{MusicTakeover::NONE};
  uint32_t takeover_interacted_ms = 0;
  uint8_t carousel_page = 0;     // 0..5 sliding 4-slot window start across 9 peer tabs (0..8)
  int mode_bar_win_start = 0;    // 0..5 window start for render_in_app_mode_bar_track
  int8_t action_sheet_slot = -1; // 0..4 when opened from a slot row; -1 for header container
  bool action_sheet_is_queue = false;
  bool action_sheet_is_container = false;
  std::string action_sheet_id = "";
  std::string action_sheet_name = "";
  std::string action_sheet_type = "";
  std::string action_sheet_queue_item_id = "";
  int info_scroll_px = 0;
  int info_scroll_page = 1;
  int settings_page = 1;
  bool lyrics_auto_managed = false;
  bool lyrics_auto_fallback = false;
  MusicLyricsState lyrics{};

  bool direct_nav = false;
  bool hud_active = false;
  uint32_t hud_timer = 0;
  int category_page = 1;
  std::string selected_category_mode = "speakers";
  std::string selected_category_title = "SPEAKERS";
  std::string drilldown_artist_name = "";
  std::string drilldown_album_id = "";
  std::string drilldown_parent_mode = "";
  int drilldown_artist_page = 1;
  int drilldown_album_page = 1;
  std::string active_playback_media_type = "music";
  std::string active_speaker_entity = "";
  std::string active_speaker_name = "";
  std::string target_speaker_id = "";
  std::string target_speaker_name = "";
  std::string selected_slot_name = "";
  std::string selected_slot_id = "";
  std::string selected_slot_type = "";
  std::string action_feedback = "";
  uint32_t feedback_timer = 0;
  float pos_base = 0.0f;
  uint32_t pos_sync_time = 0;
  int skip_burst_count = 0;
  uint32_t skip_last_tap_ms = 0;
  int remote_vol_pct = 70;
  uint32_t last_local_vol_tap_ms = 0;
  uint32_t remote_play_override_ms = 0;
  bool remote_play_override_state = false;
  bool queue_cleared = false;
  uint32_t scroll_tap_lockout_until_ms = 0;
  std::string last_sendspin_artist = "";
  std::string last_sendspin_title = "";
  std::string last_sendspin_album = "";
  std::string loaded_art_url = "";
  std::string loaded_wallpaper_url = "";
  std::string loaded_screensaver_photo_url = "";
  bool screensaver_photo_failed = false;

  void sync_carousel_for_view(int vis_slots = 4) noexcept {
    const int cap = std::clamp(vis_slots, 1, kMusicPeerTabCount);
    const int max_start = std::max(0, kMusicPeerTabCount - cap);
    const int idx = std::clamp(static_cast<int>(view), 0, kMusicPeerTabCount - 1);
    if (mode_bar_win_start < 0 || mode_bar_win_start > max_start) {
      mode_bar_win_start = 0;
    }
    if (idx < mode_bar_win_start) {
      mode_bar_win_start = idx;
    } else if (idx >= mode_bar_win_start + cap) {
      mode_bar_win_start = idx - (cap - 1);
    }
    mode_bar_win_start = std::clamp(mode_bar_win_start, 0, max_start);
    carousel_page = static_cast<uint8_t>(mode_bar_win_start);
  }

  void clear_lyrics_auto_state() noexcept {
    lyrics_auto_managed = false;
    lyrics_auto_fallback = false;
  }

  void reset_overlays() noexcept {
    drilldown = MusicDrilldown::NONE;
    takeover = MusicTakeover::NONE;
    takeover_interacted_ms = 0U;
    target_speaker_id.clear();
    target_speaker_name.clear();
    action_sheet_slot = -1;
    action_sheet_is_queue = false;
    action_sheet_is_container = false;
    action_sheet_id.clear();
    action_sheet_name.clear();
    action_sheet_type.clear();
    action_sheet_queue_item_id.clear();
  }

  bool apply_route(RouteId route_id) {
    MusicView next_view = MusicView::NOW_PLAYING;
    MusicDrilldown next_drill = MusicDrilldown::NONE;
    if (!resolve_music_route_target(route_id, next_view, next_drill)) {
      return false;
    }
    reset_overlays();
    clear_lyrics_auto_state();
    view = next_view;
    drilldown = next_drill;
    sync_carousel_for_view();
    if (route_id == RouteId::MUSIC_LIBRARY) {
      selected_category_mode = "categories";
      selected_category_title = "LIBRARY";
      category_page = 1;
      drilldown_parent_mode.clear();
    } else if (route_id == RouteId::MUSIC_FAVORITES) {
      selected_category_mode = "favorites";
      selected_category_title = "FAVORITES";
      drilldown_parent_mode.clear();
    } else if (route_id == RouteId::MUSIC_PLAYLISTS) {
      selected_category_mode = "playlist";
      selected_category_title = "PLAYLISTS";
      drilldown_parent_mode.clear();
    }
    return true;
  }
};


bool parse_v3_media_payload(std::string_view payload,
                            MediaState &st = media_state(),
                            uint32_t now_ms = 0);
void append_music_lyrics_records_blob(std::string_view blob,
                                      std::vector<MusicLyricLine> &dst,
                                      size_t max_total = kMaxMusicLyricLines);

}  // namespace latter_core
