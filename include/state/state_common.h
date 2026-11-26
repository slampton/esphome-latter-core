#pragma once

// ==============================================================================
// Latter Core: Pure C++20 State Enums, Capabilities & Protocol Primitives
// (include/state/state_common.h)
// ==============================================================================
// Thread affinity: main-loop only (ESPHome loop() / API / script context).
// ==============================================================================

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

#include "latter_traits.h"
#if __has_include("esphome/core/log.h")
#include "esphome/core/log.h"
#endif

#include "latter_protocol.h"

namespace latter_rpc {
#ifndef LATTER_RPC_SLOT_CHANNEL_DEFINED
#define LATTER_RPC_SLOT_CHANNEL_DEFINED 1
enum class SlotChannel : uint8_t {
  MUSIC         = 0,
  AREA_LIST     = 1,
  AREA_ENTITIES = 2,
  CALENDAR      = 3,
  COUNT         = 4,
};
#endif
}  // namespace latter_rpc

namespace latter_core {

// Canonical top-level page IDs.
namespace PageId {
  static constexpr int CLOCK      = 0;
  static constexpr int HOME       = 1;
  static constexpr int MUSIC      = 2;
  static constexpr int GAMES      = 3;
  static constexpr int SYSTEM     = 4;
  static constexpr int WEATHER    = 5;
  static constexpr int AREA       = 6;
  static constexpr int REMOTE     = 7;
  static constexpr int TOOLS      = 8;
  static constexpr int HOME_HUB   = 9;
  static constexpr int TIME       = 10;
  static constexpr int PINNED     = 11;
  static constexpr int CALENDAR   = 12;
  static constexpr int CAMERAS    = 13;
}  // namespace PageId

namespace CamerasTab {
  static constexpr int GRID     = 0;
  static constexpr int SINGLE   = 1;
  static constexpr int CAROUSEL = 2;
  static constexpr int SETTINGS = 3;
}  // namespace CamerasTab

inline constexpr uint8_t kCamCapAudio    = 0x01U;
inline constexpr uint8_t kCamCapTalkback = 0x02U;
inline constexpr uint8_t kCamCapDoorbell = 0x04U;
inline constexpr uint8_t kCamCapCarousel = 0x08U;
inline constexpr uint8_t kCamCapReply    = 0x10U;
inline constexpr uint8_t kCamCapUnlock   = 0x20U;

namespace CalendarSubpage {
  static constexpr int OVERVIEW = 0;
  static constexpr int DAY      = 1;
  static constexpr int WEEK     = 2;
  static constexpr int MONTH    = 3;
}  // namespace CalendarSubpage

namespace CalDrilldown {
  static constexpr int NONE          = 0;
  static constexpr int DETAIL        = 1;
  static constexpr int ADD_STEP1     = 3;
  static constexpr int ADD_STEP2     = 4;
  static constexpr int PICK_TITLE    = 5;
  static constexpr int PICK_LOCATION = 6;
  static constexpr int PICK_CALENDAR = 7;
  static constexpr int PICK_REPEAT   = 8;
}  // namespace CalDrilldown


// Em-dash placeholder rendered when Home Assistant / Protocol v3 state is not yet populated.
inline constexpr const char *kUnknownGlyph = "—";

enum class LockState : uint8_t {
  UNKNOWN  = 0,
  UNLOCKED = 1,
  LOCKED   = 2,
};

enum class GarageState : uint8_t {
  UNKNOWN = 0,
  CLOSED  = 1,
  OPEN    = 2,
};

enum class WakeWordMode : uint8_t {
  DockedOnly = 0,
  AlwaysOn   = 1,
  Disabled   = 2,
};

enum class WakePolicyMode : uint8_t {
  AlwaysOn   = 0,
  DockedOnly = 1,
  Disabled   = 2,
};

enum class TapWakeMode : uint8_t {
  SingleTap  = 0,
  DoubleTap  = 1,
  DockedOnly = 2,
  Disabled   = 3,
};

enum class ScreensaverMode : uint8_t {
  DockedOnly = 0,
  Always     = 1,
  Never      = 2,
};

enum class ScreensaverStyle : uint8_t {
  AmbientClock = 0,
  PhotoFrame   = 1,
  Aurora       = 2,
  Starfield    = 3,
  DigitalRain  = 4,
  Fireworks    = 5,
};

inline constexpr std::string_view kDayNightSkyWallpaperUrl = "latter://day_night_sky";

inline constexpr bool is_day_night_sky_wallpaper_url(std::string_view url) noexcept {
  return url == kDayNightSkyWallpaperUrl;
}

inline constexpr bool has_known_day_night_phase(
    std::string_view sun_state, bool time_valid, int hour) noexcept {
  if (sun_state == "below_horizon" || sun_state == "above_horizon") return true;
  return time_valid && hour >= 0 && hour <= 23;
}

inline constexpr bool resolve_day_night_sky_is_night(
    std::string_view sun_state, bool time_valid, int hour) noexcept {
  if (sun_state == "below_horizon") return true;
  if (sun_state == "above_horizon") return false;
  if (time_valid && hour >= 0 && hour <= 23) {
    return (hour < 6 || hour >= 20);
  }
  return false;
}

enum class AutoBrightnessMode : uint8_t {
  Disabled        = 0,
  SunSynchronized = 1,
  TimeSchedule    = 2,
  AmbientLight    = 3,
};

enum class ColorProfilePref : uint8_t {
  SteelBlue = 0,
  Pink      = 1,
  Blue      = 2,
  Green     = 3,
  Custom    = 4,
};

enum class VaVisualStyle : uint8_t {
  Nebula   = 0,
  Solaris  = 1,
  Rings    = 2,
  LightBar = 3,
  DotWave  = 4,
};

enum class ClockFaceStyle : uint8_t {
  MinimalDigital = 0,
  ModernAnalog   = 1,
  HybridDigital  = 2,
};

enum class GlassStyle : uint8_t {
  Auto  = 0,
  Clear = 1,
  Dark  = 2,
};

enum class MediaPlaybackState : uint8_t {
  Idle       = 0,
  Playing    = 1,
  Paused     = 2,
  Buffering  = 3,
  Announcing = 4,
};

enum class AlertPriority : uint8_t {
  Normal   = 0,
  Critical = 1,
};

enum class UpdateCheckState : uint8_t {
  Idle     = 0,
  Checking = 1,
  Checked  = 2,
};

inline constexpr MediaPlaybackState parse_media_playback_state(std::string_view sv) noexcept {
  if (sv == "playing") return MediaPlaybackState::Playing;
  if (sv == "paused") return MediaPlaybackState::Paused;
  if (sv == "buffering") return MediaPlaybackState::Buffering;
  if (sv == "announcing") return MediaPlaybackState::Announcing;
  return MediaPlaybackState::Idle;
}

inline constexpr AlertPriority parse_alert_priority(std::string_view sv) noexcept {
  return (sv == "critical") ? AlertPriority::Critical : AlertPriority::Normal;
}

struct LinkState {
  bool ha_connected = false;
  bool wifi_connected = false;
  uint32_t last_ha_connect_ms = 0;
  uint32_t last_ha_disconnect_ms = 0;
  uint32_t last_wifi_connect_ms = 0;
};

namespace HomeHubTab {
  static constexpr int ALARM      = 0;
  static constexpr int INTERCOM   = 1;
  static constexpr int THERMOSTAT = 2;
  static constexpr int ENTRY      = 3;
  static constexpr int VACUUM     = 4;
}  // namespace HomeHubTab

namespace ToolsSubpage {
  static constexpr int LAUNCHER   = 0;
  static constexpr int STOPWATCH  = 3;
  static constexpr int DICE       = 4;
  static constexpr int FLASHLIGHT = 5;
  static constexpr int CALCULATOR = 6;
  static constexpr int LEVEL      = 7;
  static constexpr int COIN_FLIP  = 8;
}  // namespace ToolsSubpage

inline constexpr int kMaxHomeHubTabs = 5;


struct BrowseSlotItem {
  std::string name = "";
  std::string id = "";
  std::string type = "";
  bool active = false;
  int level = -1;
  std::string queue_item_id = "";
  bool is_current = false;
};

using MusicBrowseSlot = BrowseSlotItem;


extern constinit LinkState g_link_state;

inline LinkState &link_state() noexcept {
  return g_link_state;
}

inline constexpr std::string_view utf8_safe_prefix(std::string_view sv, size_t max_bytes) noexcept {
  if (sv.size() <= max_bytes) return sv;
  if (max_bytes == 0U) return {};
  size_t end = max_bytes;
  while (end > 0U && (static_cast<unsigned char>(sv[end]) & 0xC0U) == 0x80U) {
    --end;
  }
  if (end > 0U) {
    size_t lead = end - 1U;
    while (lead > 0U && (static_cast<unsigned char>(sv[lead]) & 0xC0U) == 0x80U) {
      --lead;
    }
    const unsigned char lb = static_cast<unsigned char>(sv[lead]);
    size_t seq_len = 1U;
    if ((lb & 0x80U) == 0x00U) seq_len = 1U;
    else if ((lb & 0xE0U) == 0xC0U) seq_len = 2U;
    else if ((lb & 0xF0U) == 0xE0U) seq_len = 3U;
    else if ((lb & 0xF8U) == 0xF0U) seq_len = 4U;
    else return sv.substr(0U, lead);
    if (lead + seq_len > end) {
      end = lead;
    }
  }
  return sv.substr(0U, end);
}

inline std::string v3_escape_subfield(std::string_view sv) {
  std::string out;
  out.reserve(sv.size());
  for (char ch : sv) {
    if (ch == '\\') {
      out += "\\\\";
    } else if (ch == '|') {
      out += "\\p";
    } else if (ch == ';') {
      out += "\\s";
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

inline std::string v3_unescape_subfield(std::string_view sv) {
  if (sv.find('\\') == std::string_view::npos) {
    return std::string(sv);
  }
  std::string out;
  out.reserve(sv.size());
  for (size_t i = 0; i < sv.size(); ++i) {
    if (sv[i] == '\\' && (i + 1U) < sv.size()) {
      const char nxt = sv[i + 1U];
      if (nxt == '\\') {
        out.push_back('\\');
        ++i;
        continue;
      }
      if (nxt == 'p') {
        out.push_back('|');
        ++i;
        continue;
      }
      if (nxt == 's') {
        out.push_back(';');
        ++i;
        continue;
      }
    }
    out.push_back(sv[i]);
  }
  return out;
}


template <size_t N>
inline void copy_bounded_cstr(char (&dst)[N], std::string_view src) {
  static_assert(N > 0, "Destination buffer must be non-empty");
  const std::string_view safe = utf8_safe_prefix(src, N - 1U);
  if (!safe.empty()) std::memcpy(dst, safe.data(), safe.size());
  dst[safe.size()] = '\0';
}

inline std::string_view trim_ascii_ws(std::string_view sv) noexcept {
  while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
    sv.remove_suffix(1);
  }
  return sv;
}


// Zero-allocation tokenizer for \x1f-delimited Protocol v3 payloads
inline size_t v3_split_fields(std::string_view payload, std::string_view *out, size_t max_fields, char sep = '\x1f') {
  if (payload.empty() || max_fields == 0) return 0;
  size_t count = 0;
  size_t start = 0;
  while (count < max_fields) {
    size_t pos = payload.find(sep, start);
    if (pos == std::string_view::npos) {
      out[count++] = payload.substr(start);
      break;
    }
    out[count++] = payload.substr(start, pos - start);
    start = pos + 1;
  }
  return count;
}

inline std::string_view trim_v3_num_sv(std::string_view sv) noexcept {
  while (!sv.empty() &&
         (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' ||
          sv.front() == '\n' || sv.front() == '\v' || sv.front() == '\f')) {
    sv.remove_prefix(1);
  }
  return sv;
}

inline int v3_parse_int(std::string_view sv, int fallback = 0) noexcept {
  sv = trim_v3_num_sv(sv);
  if (sv.size() >= 2 && sv.front() == '+' && sv[1] != '-' && sv[1] != '+') {
    sv.remove_prefix(1);
  }
  if (sv.empty()) return fallback;
  int val = 0;
  const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val, 10);
  if (ec != std::errc{} || ptr == sv.data()) return fallback;
  return val;
}

inline uint32_t v3_parse_u32(std::string_view sv, uint32_t fallback = 0) noexcept {
  sv = trim_v3_num_sv(sv);
  if (sv.empty() || sv.front() == '-') return fallback;
  if (sv.size() >= 2 && sv.front() == '+' && sv[1] != '-' && sv[1] != '+') {
    sv.remove_prefix(1);
  }
  uint32_t val = 0;
  const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val, 10);
  if (ec != std::errc{} || ptr == sv.data()) return fallback;
  return val;
}

inline float v3_parse_float(std::string_view sv, float fallback = NAN) noexcept {
  sv = trim_v3_num_sv(sv);
  if (sv.size() >= 2 && sv.front() == '+' && sv[1] != '-' && sv[1] != '+') {
    sv.remove_prefix(1);
  }
  if (sv.empty()) return fallback;
  float val = 0.0f;
  const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
  if (ec != std::errc{} || ptr == sv.data()) return fallback;
  return val;
}

inline bool v3_parse_bool(std::string_view sv) {
  return (sv == "1" || sv == "true" || sv == "True" || sv == "on");
}


inline bool apply_if_newer(uint32_t &cur_epoch, uint32_t &cur_rev, uint32_t in_epoch, uint32_t in_rev) {
  if (in_epoch == 0 || in_rev == 0) return false;
  if (in_epoch != cur_epoch) {
    cur_epoch = in_epoch;
    cur_rev = in_rev;
    return true;
  }
  if (in_rev > cur_rev) {
    cur_rev = in_rev;
    return true;
  }
  return false;
}

extern constinit uint32_t g_v3_mismatch_warn_count;

inline uint32_t &v3_mismatch_warn_count() noexcept {
  return g_v3_mismatch_warn_count;
}


inline void v3_check_field_count(const char *domain, std::string_view payload, size_t expected) {
  const size_t got = payload.empty() ? 0 : (1U + static_cast<size_t>(
      std::count(payload.begin(), payload.end(), kProtocolFieldSep)));
  if (got != expected) {
    const uint32_t count = ++v3_mismatch_warn_count();
#if defined(USE_ESP32) && defined(ESP_LOGW)
    if (count <= 8U || (count % 64U) == 0U) {
      ESP_LOGW("latter_v3", "Domain '%s' field count mismatch: got=%u expected=%u (total=%lu)",
               domain != nullptr ? domain : "?",
               static_cast<unsigned>(got),
               static_cast<unsigned>(expected),
               static_cast<unsigned long>(count));
    }
#else
    (void)domain;
    (void)count;
#endif
  }
}

inline constexpr bool eq_ascii_ci_sv(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    unsigned char ca = static_cast<unsigned char>(a[i]);
    unsigned char cb = static_cast<unsigned char>(b[i]);
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + ('a' - 'A'));
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + ('a' - 'A'));
    if (ca != cb) return false;
  }
  return true;
}


inline uint32_t parse_hex_rgb_u24(std::string_view sv, uint32_t fallback) {
  if (sv.size() != 7 || sv[0] != '#') return fallback;
  auto hex_nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  };
  uint32_t rgb = 0;
  for (size_t i = 1; i < 7; ++i) {
    int n = hex_nib(sv[i]);
    if (n < 0) return fallback;
    rgb = (rgb << 4) | static_cast<uint32_t>(n);
  }
  return rgb;
}


}  // namespace latter_core
