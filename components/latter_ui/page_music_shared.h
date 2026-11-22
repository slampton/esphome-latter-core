#pragma once

#include "ui_pages.h"
#include "ui_touch_engine.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

namespace latter_ui {

inline constexpr bool is_speech_media_type(std::string_view mt) noexcept {
  return mt == "podcast" || mt == "podcast_episode" || mt == "audiobook";
}

inline void format_mmss(int total_sec, char *out, size_t cap) noexcept {
  if (total_sec < 0) {
    std::snprintf(out, cap, "%s", kUnknownGlyph);
    return;
  }
  const int hrs = total_sec / 3600;
  const int mins = (total_sec % 3600) / 60;
  const int secs = total_sec % 60;
  if (hrs > 0) {
    std::snprintf(out, cap, "%d:%02d:%02d", hrs, mins, secs);
  } else {
    std::snprintf(out, cap, "%d:%02d", mins, secs);
  }
}

struct ResolvedTrackStrings {
  std::string title{};
  std::string artist{};
  std::string album{};
  std::string speaker_name{};
  std::string speaker_entity{};
  bool is_playing{false};
  bool is_speech{false};
};

inline ResolvedTrackStrings resolve_track_strings(const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const auto &ms = media_state();
  ResolvedTrackStrings out{};
  out.is_playing = is_music_playing_effective(!ms.is_local, ctx.local_media_playing, ms.state, ctx.now_ms);

  if (!ms.is_local) {
    out.title = ms.title;
    out.artist = ms.artist;
    out.album = ms.album;
  } else {
    out.title = !ctx.track_title.empty() ? ctx.track_title.str() : ms.title;
    out.artist = !ctx.track_artist.empty() ? ctx.track_artist.str() : ms.artist;
    out.album = !ctx.track_album.empty() ? ctx.track_album.str() : ms.album;
  }

  out.speaker_entity = !ms.active_speaker.empty() ? ms.active_speaker : rs.music.active_speaker_entity;
  if (!ms.speaker_name.empty()) {
    out.speaker_name = ms.speaker_name;
  } else if (!rs.music.active_speaker_name.empty()) {
    out.speaker_name = rs.music.active_speaker_name;
  } else if (ms.is_local && ctx.friendly_name != nullptr && ctx.friendly_name[0] != '\0') {
    out.speaker_name = ctx.friendly_name;
  } else {
    out.speaker_name = "This Device";
  }

  const std::string_view mt = !ms.media_type.empty()
                                  ? std::string_view(ms.media_type)
                                  : std::string_view(rs.music.active_playback_media_type);
  out.is_speech = is_speech_media_type(mt);
  return out;
}

inline ArtImageView select_music_art(const UiFrameContext &ctx) {
  const auto &ms = media_state();
  if (!ms.is_local && ctx.remote_album_art.valid()) return ctx.remote_album_art;
  if (ms.is_local && ctx.album_art.valid()) return ctx.album_art;
  return ctx.remote_album_art.valid() ? ctx.remote_album_art : ctx.album_art;
}

inline void render_music_art_or_wallpaper_base(Rgb565Surface &it, const UiFrameContext &ctx,
                                               bool has_controls_scrim, bool has_metadata_scrim,
                                               int dim_level) {
  const auto &rs = runtime_state();
  const auto &ac = area_ctx_state();
  const UiStrView art_mode = ctx.music_fullscreen_art_opt;
  if (art_mode == "None") {
    if (is_day_night_sky_wallpaper_url(ac.wallpaper_url)) {
      render_static_day_night_sky_base(
          it, ctx.cx, ctx.cy,
          resolve_day_night_sky_is_night(weather_state().sun_state, ctx.now.is_valid(), ctx.now.hour));
    } else {
      const bool has_wall = (!ac.wallpaper_url.empty() &&
                             ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                             ctx.home_wallpaper_art.valid());
      render_page_wallpaper_base(it, ctx.home_wallpaper_art, has_wall, ctx.cx, ctx.col_bg, dim_level);
    }
  } else {
    const bool is_standard = (art_mode == "Standard") && (dim_level == 0);
    const bool is_letterbox = (art_mode == "Letterbox") && (dim_level == 0);
    render_album_art_canvas(it, select_music_art(ctx), ctx.cx, is_standard, is_letterbox,
                            has_controls_scrim, has_metadata_scrim, dim_level);
    frame_base(it, ctx.col_bg);
  }
}

struct MusicModeBarIconCtx {
  bool is_speech{false};
  bool muted{false};
};

inline void draw_music_mode_icon(Rgb565Surface &it, int g_idx, int bx, int by, int ir,
                                 UiColor fg_c, bool sh, void *user_ctx) {
  const auto *mc = static_cast<const MusicModeBarIconCtx *>(user_ctx);
  const bool is_speech = (mc != nullptr) ? mc->is_speech : false;
  const bool muted = (mc != nullptr) ? mc->muted : false;
  const int mdi_sz = std::max(16, 2 * ir);
  switch (g_idx) {
    case 0: ui::draw_icon_play_pause(it, bx, by, ir, fg_c, sh); break;
    case 1: ui::draw_icon_hamburger(it, bx, by, ir, fg_c, sh); break;
    case 2: render_mdi_icon(it, "mdi:speaker-multiple", bx, by, mdi_sz, fg_c, sh); break;
    case 3: ui::draw_icon_speaker(it, bx, by, ir, fg_c, !muted, sh, muted); break;
    case 4: render_mdi_icon(it, "mdi:playlist-play", bx, by, mdi_sz, fg_c, sh); break;
    case 5: render_mdi_icon(it, "mdi:script-text", bx, by, mdi_sz, fg_c, sh); break;
    case 6: render_mdi_icon(it, is_speech ? "mdi:podcast" : "mdi:album", bx, by, mdi_sz, fg_c, sh); break;
    case 7: render_mdi_icon(it, "mdi:information-outline", bx, by, mdi_sz, fg_c, sh); break;
    case 8:
    default: ui::draw_icon_gear(it, bx, by, ir, fg_c, sh); break;
  }
}

inline void render_music_stadium_mode_bar(Rgb565Surface &it, const UiFrameContext &ctx,
                                          bool is_speech_media) {
  auto &m = runtime_state().music;
  const auto &ms = media_state();
  m.sync_carousel_for_view();
  const int active_global = std::clamp(static_cast<int>(m.view), 0, kMusicPeerTabCount - 1);
  MusicModeBarIconCtx ic{is_speech_media, ms.muted};
  ui::mode_bar(it, ctx.cx, kMusicPeerTabCount, active_global, m.mode_bar_win_start,
               ctx.theme_primary, ctx.col_white, draw_music_mode_icon, &ic);
}

void render_music_lyrics_view(Rgb565Surface &it, const UiFrameContext &ctx);
void render_music_info_view(Rgb565Surface &it, const UiFrameContext &ctx);
void render_music_settings_view(Rgb565Surface &it, const UiFrameContext &ctx);
void render_music_action_sheet_takeover(Rgb565Surface &it, const UiFrameContext &ctx);
void render_music_browse_views(Rgb565Surface &it, const UiFrameContext &ctx);

}  // namespace latter_ui
