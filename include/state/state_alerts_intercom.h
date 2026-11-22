#pragma once

// ==============================================================================
// Latter Core SDK: Notification Banner, Intercom, Camera Records & Stream URLs
// (include/state/state_alerts_intercom.h)
// ==============================================================================

#include "state/state_common.h"
#include "state/state_prefs.h"
#include "state/state_areas.h"

#if defined(ESP_PLATFORM) && !defined(LATTER_HOST_TEST) && __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#endif

namespace latter_ui {

struct AlertBannerState {
  bool active = false;
  std::string title = "";
  std::string message = "";
  std::string priority = "normal";
  AlertPriority priority_level = AlertPriority::Normal;
  std::string camera_url = "";
  std::string camera_entity = "";
  std::string loaded_camera_url = "";
  uint32_t start_ms = 0;
  uint32_t duration_ms = 10000U;
  int last_seen_alert_id = -1;

  bool is_critical() const noexcept {
    return priority_level == AlertPriority::Critical || priority == "critical";
  }
};


struct IntercomRuntimeState {
  std::string broadcast_message = "Dinner is ready";
  std::string broadcast_feedback = "";
  uint32_t broadcast_feedback_ms = 0;
  std::string cam_action = "listen_start";
  std::string cam_message = "Please leave it at the door.";
  std::string cam_noise_reduction = "strong";
  std::string cam_code = "";
  uint32_t cam_status_ms = 0;
  bool broadcast_dictation_active = false;
  uint32_t broadcast_dictation_started_ms = 0;
  bool cam_call_active = false;
  bool cam_listen_active = false;
  bool cam_intercom_ptt_active = false;
  std::string cam_intercom_status = "";
  bool cam_expanded_fullscreen = false;
  uint32_t last_cam_live_fetch_ms = 0;
  uint32_t cam_error_backoff_until_ms = 0;
  bool cam_download_in_progress = false;
};


struct CamerasRuntimeState {
  int tab = CamerasTab::GRID;
  int selected_idx = 0;
  int grid_page = 0;
  int settings_page = 0;
  int carousel_idx = 0;
  uint32_t carousel_last_advance_ms = 0;
  bool carousel_paused = false;
  bool hud_active = true;
  uint32_t hud_timer = 0;
  bool quick_reply_open = false;
  bool landscape_mode = false;
  bool doorbell_live_popup_active = false;
  int video_y0 = 0;
  int video_y1 = LATTER_SCREEN_H - 1;
  uint32_t last_stream_sync_ms = 0;

  bool effective_fill(const PersistedDevicePrefs &p = persisted_prefs()) const noexcept {
    return p.camera_framing == 1;
  }
};


inline constexpr int MAX_CAMERA_SLOTS = 10;

struct CameraRecord {
  std::string entity_id = "";
  std::string name = "";
  std::string area_id = "";
  uint8_t caps = 0;
  std::string unlock_entity = "";

  constexpr bool has_audio() const noexcept { return (caps & kCamCapAudio) != 0U; }
  constexpr bool has_talkback() const noexcept { return (caps & kCamCapTalkback) != 0U; }
  constexpr bool is_doorbell() const noexcept { return (caps & kCamCapDoorbell) != 0U; }
  constexpr bool in_carousel() const noexcept { return (caps & kCamCapCarousel) != 0U; }
  constexpr bool show_reply() const noexcept { return (caps & kCamCapReply) != 0U; }
  bool has_unlock() const noexcept { return ((caps & kCamCapUnlock) != 0U) && !unlock_entity.empty(); }
  bool unlock_is_lock() const noexcept { return unlock_entity.rfind("lock.", 0) == 0; }
};

inline int parse_cameras_records(std::string_view raw, CameraRecord *out, int max_slots = MAX_CAMERA_SLOTS) {
  if (raw.empty() || out == nullptr || max_slots <= 0) return 0;
  std::string_view recs[MAX_CAMERA_SLOTS];
  const int cap = (max_slots < MAX_CAMERA_SLOTS) ? max_slots : MAX_CAMERA_SLOTS;
  const size_t n = v3_split_fields(raw, recs, static_cast<size_t>(cap), kProtocolRecordSep);
  int idx = 0;
  for (size_t r = 0; r < n && idx < cap; ++r) {
    std::string_view f[5];
    const size_t fc = v3_split_fields(recs[r], f, 5, '|');
    if (fc < 2 || f[0].empty()) continue;
    out[idx].entity_id = v3_unescape_subfield(f[0]);
    out[idx].name = f[1].empty() ? out[idx].entity_id : v3_unescape_subfield(f[1]);
    out[idx].area_id = (fc >= 3) ? v3_unescape_subfield(f[2]) : "";
    uint8_t parsed_caps = 0;
    if (fc >= 4 && !f[3].empty()) {
      const std::string_view cv = f[3];
      if (cv.rfind("0x", 0) == 0 || cv.rfind("0X", 0) == 0) {
        uint32_t acc = 0;
        for (size_t i = 2; i < cv.size(); ++i) {
          const char ch = cv[i];
          if (ch >= '0' && ch <= '9') acc = (acc << 4) | static_cast<uint32_t>(ch - '0');
          else if (ch >= 'a' && ch <= 'f') acc = (acc << 4) | static_cast<uint32_t>(10 + ch - 'a');
          else if (ch >= 'A' && ch <= 'F') acc = (acc << 4) | static_cast<uint32_t>(10 + ch - 'A');
          else break;
        }
        parsed_caps = static_cast<uint8_t>(acc & 0xFFU);
      } else if (cv[0] >= '0' && cv[0] <= '9') {
        parsed_caps = static_cast<uint8_t>(v3_parse_u32(cv, 0U) & 0xFFU);
      }
    }
    out[idx].caps = parsed_caps;
    out[idx].unlock_entity = (fc >= 5) ? std::string(f[4]) : "";
    ++idx;
  }
  return idx;
}

inline CameraRecord *camera_records_scratch() {
  static std::vector<CameraRecord> s_buf(static_cast<size_t>(MAX_CAMERA_SLOTS));
  return s_buf.data();
}

inline int count_configured_cameras(const AreaContextState &ac = area_ctx_state()) noexcept {
  if (!ac.cameras_summary.empty()) {
    std::string_view recs[MAX_CAMERA_SLOTS];
    const size_t n = v3_split_fields(ac.cameras_summary, recs, static_cast<size_t>(MAX_CAMERA_SLOTS), kProtocolRecordSep);
    int valid = 0;
    for (size_t r = 0; r < n; ++r) {
      std::string_view f[2];
      const size_t fc = v3_split_fields(recs[r], f, 2, '|');
      if (fc >= 2 && !f[0].empty()) ++valid;
    }
    if (valid > 0) return valid;
  }
  return (!ac.camera_stream_url.empty() || !ac.camera_snapshot_url.empty()) ? 1 : 0;
}


inline bool can_start_online_art_fetch(bool has_existing_buffer = false) noexcept {
#if defined(ESP_PLATFORM)
  const size_t int_blk = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (int_blk > 0U && int_blk < 8192U) {
    return false;
  }
  const size_t need_psram = has_existing_buffer
                                ? 65536U
                                : (static_cast<size_t>(LATTER_SCREEN_W) *
                                       static_cast<size_t>(LATTER_SCREEN_H) * sizeof(uint16_t) +
                                   49152U);
  const size_t psram_blk = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (psram_blk < need_psram) {
    return false;
  }
#else
  (void)has_existing_buffer;
#endif
  return true;
}


inline int parse_broadcast_phrases(const std::string &raw, std::vector<std::string> &out) {
  out.clear();
  size_t start = 0;
  while (start < raw.size() && out.size() < 12U) {
    const size_t sep = raw.find(kProtocolRecordSep, start);
    const std::string_view rec =
        (sep == std::string::npos) ? std::string_view(raw).substr(start)
                                   : std::string_view(raw).substr(start, sep - start);
    const std::string_view trimmed = trim_ascii_ws(rec);
    if (!trimmed.empty()) {
      out.emplace_back(utf8_safe_prefix(trimmed, 64U));
    }
    if (sep == std::string::npos) break;
    start = sep + 1;
  }
  return static_cast<int>(out.size());
}


inline int find_camera_index_by_entity(const CameraRecord *cams, int cam_cnt,
                                       std::string_view entity_id) noexcept {
  if (cams == nullptr || cam_cnt <= 0 || entity_id.empty()) return -1;
  for (int i = 0; i < cam_cnt; ++i) {
    if (cams[i].entity_id == entity_id) return i;
  }
  return -1;
}

inline int collect_carousel_camera_indices(const CameraRecord *cams, int cam_cnt,
                                           int *out_indices, int max_out) noexcept {
  if (cams == nullptr || cam_cnt <= 0 || out_indices == nullptr || max_out <= 0) return 0;
  int n = 0;
  for (int i = 0; i < cam_cnt && n < max_out; ++i) {
    if (cams[i].in_carousel()) {
      out_indices[n++] = i;
    }
  }
  if (n == 0) {
    for (int i = 0; i < cam_cnt && n < max_out; ++i) {
      out_indices[n++] = i;
    }
  }
  return n;
}

inline constexpr int cam_align16_floor(int px) noexcept {
  return (px <= 16) ? 16 : (px & ~15);
}

inline void resolve_camera_stream_dims(int panel_w, int panel_h, bool is_round,
                                       bool is_grid, bool is_p4, bool low_bw,
                                       uint16_t &out_w, uint16_t &out_h,
                                       bool rot90 = false) noexcept {
  if (is_round) {
    if (is_grid) {
      out_w = low_bw ? 256U : 320U;
      out_h = low_bw ? 192U : 256U;
    } else {
      out_w = low_bw ? 320U : 384U;
      out_h = low_bw ? 176U : 224U;
    }
    return;
  }
  const int max_dim = is_p4 ? (low_bw ? 512 : 768) : (low_bw ? 320 : 480);
  if (rot90 && !is_grid && panel_h > panel_w) {
    const int h = std::min(panel_h, max_dim);
    int w = std::min(panel_w, (h * 9) / 16);
    if (w < 128) w = std::min(panel_w, 192);
    out_w = static_cast<uint16_t>(cam_align16_floor(w));
    out_h = static_cast<uint16_t>(cam_align16_floor(h));
    return;
  }
  int w = std::min(panel_w, max_dim);
  int h = is_grid ? std::min(panel_h - 96, (w * 3) / 4)
                  : std::min(panel_h, (w * 9) / 16);
  if (h < 128) h = std::min(panel_h, 192);
  out_w = static_cast<uint16_t>(cam_align16_floor(w));
  out_h = static_cast<uint16_t>(cam_align16_floor(h));
}

inline constexpr uint8_t resolve_camera_stream_fps(bool is_p4, bool is_grid,
                                                   bool low_bw, bool audio_or_ptt_active) noexcept {
  if (is_p4) {
    if (is_grid) return low_bw ? 4U : 6U;
    return low_bw ? 6U : 12U;
  }
  if (audio_or_ptt_active) return 2U;
  if (is_grid) return 2U;
  return low_bw ? 3U : 5U;
}

inline std::string strip_query_param(std::string_view url, std::string_view key) {
  std::string out;
  out.reserve(url.size());
  const size_t qpos = url.find('?');
  if (qpos == std::string_view::npos) return std::string(url);
  out.append(url.substr(0, qpos + 1));
  std::string_view qs = url.substr(qpos + 1);
  bool first = true;
  size_t pos = 0;
  while (pos <= qs.size()) {
    const size_t amp = qs.find('&', pos);
    const std::string_view seg =
        (amp == std::string_view::npos) ? qs.substr(pos) : qs.substr(pos, amp - pos);
    if (!seg.empty()) {
      const size_t eq = seg.find('=');
      const std::string_view k = (eq == std::string_view::npos) ? seg : seg.substr(0, eq);
      if (k != key) {
        if (!first) out.push_back('&');
        out.append(seg);
        first = false;
      }
    }
    if (amp == std::string_view::npos) break;
    pos = amp + 1;
  }
  return out;
}

inline std::string build_camera_single_stream_url(std::string_view base_stream_url,
                                                  std::string_view entity_id,
                                                  uint16_t w, uint16_t h, uint8_t fps,
                                                  bool fill, bool rot90 = false) {
  if (base_stream_url.empty()) return "";
  std::string out = strip_query_param(base_stream_url, "entity_id");
  const char sep = (out.find('?') == std::string::npos) ? '?' : '&';
  if (!entity_id.empty()) {
    out.push_back(sep);
    out.append("entity_id=");
    out.append(entity_id);
  }
  auto append_kv = [&](const char *k, const std::string &v) {
    out.push_back((out.find('?') == std::string::npos) ? '?' : '&');
    out.append(k);
    out.push_back('=');
    out.append(v);
  };
  append_kv("w", std::to_string(w));
  append_kv("h", std::to_string(h));
  append_kv("fps", std::to_string(fps));
  append_kv("fit", fill ? "fill" : "contain");
  if (rot90) append_kv("rot", "90");
  return out;
}

inline std::string build_camera_grid_stream_url(std::string_view base_stream_url,
                                                const std::string *entities, int count,
                                                uint16_t w, uint16_t h, uint8_t fps) {
  if (base_stream_url.empty() || entities == nullptr || count <= 0) return "";
  std::string base = strip_query_param(base_stream_url, "entity_id");
  const size_t pos = base.find("/camera_stream/");
  if (pos != std::string::npos) {
    base.replace(pos, 15U, "/camera_grid/");
  }
  std::string cams_csv;
  const int lim = std::min(count, 4);
  for (int i = 0; i < lim; ++i) {
    if (entities[i].empty()) continue;
    if (!cams_csv.empty()) cams_csv.push_back(',');
    cams_csv.append(entities[i]);
  }
  if (cams_csv.empty()) return "";
  auto append_kv = [&](const char *k, const std::string &v) {
    base.push_back((base.find('?') == std::string::npos) ? '?' : '&');
    base.append(k);
    base.push_back('=');
    base.append(v);
  };
  append_kv("cameras", cams_csv);
  append_kv("w", std::to_string(w));
  append_kv("h", std::to_string(h));
  append_kv("fps", std::to_string(fps));
  return base;
}

inline std::string build_camera_snapshot_url(std::string_view base_snap_url,
                                             std::string_view entity_id,
                                             bool fill, bool rot90, uint32_t ts_ms) {
  if (base_snap_url.empty()) return "";
  std::string out = strip_query_param(base_snap_url, "entity_id");
  out = strip_query_param(out, "fit");
  out = strip_query_param(out, "rot");
  out = strip_query_param(out, "ts");
  auto append_kv = [&](const char *k, std::string_view v) {
    out.push_back((out.find('?') == std::string::npos) ? '?' : '&');
    out.append(k);
    out.push_back('=');
    out.append(v);
  };
  if (!entity_id.empty()) append_kv("entity_id", entity_id);
  append_kv("fit", fill ? "fill" : "contain");
  if (rot90) append_kv("rot", "90");
  append_kv("ts", std::to_string(ts_ms));
  return out;
}


}  // namespace latter_ui
