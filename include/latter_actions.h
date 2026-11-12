#pragma once

// ==============================================================================
// Latter Core SDK: Typed Outbound Protocol v3 RPC Action Helpers (latter_actions.h)
// ==============================================================================
// Provides typed C++ wrappers around latter_rpc::emit(...) for all 15 outbound
// RPC services declared in protocol/latter_v3.json so C++ touch/UI handlers and
// YAML scripts emit RPC calls directly without scratch YAML globals.
// ==============================================================================

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "hal_manifest.h"
#include "latter_rpc.h"
#include "latter_state.h"

namespace latter_actions {

// 1. device_hello
template <typename OutboxT = std::nullptr_t>
inline void emit_device_hello(OutboxT outbox = nullptr) {
  const std::string boot_str = std::to_string(latter_ui::device_boot_id());
  const std::string manifest_str = latter_ui::build_hardware_manifest_json();
  latter_rpc::emit(outbox, "device_hello", {
    {"contract", "3"},
    {"boot_id", boot_str},
    {"manifest", manifest_str},
  });
}

// 2. music_browse (wires MediaState::last_speaker by default when last_speaker is omitted)
template <typename OutboxT = std::nullptr_t>
inline int emit_music_browse(std::string_view mode,
                             int page,
                             std::string_view search = "",
                             std::string_view speaker = "",
                             std::string_view last_speaker = "",
                             std::string_view album_sort = "name",
                             std::string_view artist_album_sort = "year",
                             std::string_view podcast_sort = "newest",
                             int page_size = 5,
                             uint32_t now_ms = 0,
                             OutboxT outbox = nullptr) {
  auto &rt = latter_ui::runtime_state();
  if (now_ms != 0) rt.last_music_page_req_ms = now_ms;
  const int req_id = latter_rpc::issue_req_id(latter_rpc::classify_browse_mode(mode));
  const std::string req_str = std::to_string(req_id);
  const std::string page_str = std::to_string(std::max(1, page));
  const std::string size_str = std::to_string(std::max(1, page_size));
  const std::string_view eff_spk =
      !speaker.empty() ? speaker : std::string_view(rt.music.active_speaker_entity);
  const std::string_view eff_last_spk =
      !last_speaker.empty() ? last_speaker : std::string_view(latter_ui::media_state().last_speaker);

  latter_rpc::emit(outbox, "music_browse", {
    {"req_id", req_str},
    {"mode", mode},
    {"page", page_str},
    {"page_size", size_str},
    {"search", search},
    {"speaker", eff_spk},
    {"last_speaker", eff_last_spk},
    {"album_sort", album_sort},
    {"artist_album_sort", artist_album_sort},
    {"podcast_sort", podcast_sort},
  });
  return req_id;
}

// 3. set_active_speaker
template <typename OutboxT = std::nullptr_t>
inline void emit_set_active_speaker(std::string_view entity_id,
                                    bool transfer_queue = false,
                                    OutboxT outbox = nullptr) {
  latter_rpc::emit(outbox, "set_active_speaker", {
    {"entity_id", entity_id},
    {"transfer_queue", transfer_queue ? "true" : "false"},
  });
}

// 4. media_command (simple transport command: play, pause, next, previous, clear_queue)
template <typename OutboxT = std::nullptr_t>
inline void emit_media_command(std::string_view command, OutboxT outbox = nullptr) {
  latter_rpc::emit(outbox, "media_command", {
    {"command", command},
  });
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_seek(int seek_seconds, OutboxT outbox = nullptr) {
  const std::string seek_str = std::to_string(std::max(0, seek_seconds));
  latter_rpc::emit(outbox, "media_command", {
    {"command", "seek"},
    {"seek_position", seek_str},
  });
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_play_item(std::string_view entity_id,
                                 std::string_view media_id,
                                 std::string_view media_type,
                                 std::string_view enqueue = "replace",
                                 OutboxT outbox = nullptr) {
  latter_rpc::emit(outbox, "media_command", {
    {"command", "play_media"},
    {"entity_id", entity_id},
    {"media_id", media_id},
    {"media_type", media_type},
    {"enqueue", enqueue},
  });
}

template <typename OutboxT = std::nullptr_t>
inline void emit_speaker_join(std::string_view active_speaker_entity,
                              std::string_view group_member_entity,
                              OutboxT outbox = nullptr) {
  latter_rpc::emit(outbox, "media_command", {
    {"command", "join"},
    {"entity_id", active_speaker_entity},
    {"group_members", group_member_entity},
  });
}

template <typename OutboxT = std::nullptr_t>
inline void emit_speaker_unjoin(std::string_view target_speaker_entity, OutboxT outbox = nullptr) {
  latter_rpc::emit(outbox, "media_command", {
    {"command", "unjoin"},
    {"entity_id", target_speaker_entity},
  });
}

// 5. media_volume
template <typename OutboxT = std::nullptr_t>
inline void emit_media_volume(std::string_view entity_id,
                              std::string_view action,
                              int volume_pct = -1,
                              OutboxT outbox = nullptr) {
  const std::string vol_str = (volume_pct >= 0) ? std::to_string(volume_pct) : std::string{};
  latter_rpc::emit(outbox, "media_volume", {
    {"entity_id", entity_id},
    {"action", action},
    {"volume_level", vol_str},
  });
}

// 6. area_browse (entities & areas list)
template <typename OutboxT = std::nullptr_t>
inline int emit_area_browse_entities(std::string_view area_id,
                                     std::string_view domain_filter,
                                     int page = 1,
                                     int page_size = 3,
                                     OutboxT outbox = nullptr) {
  const int req_id = latter_rpc::issue_req_id(latter_rpc::SlotChannel::AREA_ENTITIES);
  const std::string req_str = std::to_string(req_id);
  const std::string page_str = std::to_string(std::max(1, page));
  const std::string size_str = std::to_string(std::max(1, page_size));
  latter_rpc::emit(outbox, "area_browse", {
    {"req_id", req_str},
    {"mode", "entities"},
    {"area_id", area_id},
    {"domain_filter", domain_filter},
    {"page", page_str},
    {"page_size", size_str},
  });
  return req_id;
}

template <typename OutboxT = std::nullptr_t>
inline int emit_area_browse_list(std::string_view area_id,
                                 int page = 1,
                                 int page_size = 5,
                                 OutboxT outbox = nullptr) {
  const int req_id = latter_rpc::issue_req_id(latter_rpc::SlotChannel::AREA_LIST);
  const std::string req_str = std::to_string(req_id);
  const std::string page_str = std::to_string(std::max(1, page));
  const std::string size_str = std::to_string(std::max(1, page_size));
  latter_rpc::emit(outbox, "area_browse", {
    {"req_id", req_str},
    {"mode", "areas"},
    {"area_id", area_id},
    {"page", page_str},
    {"page_size", size_str},
  });
  return req_id;
}

// 7. area_action (entity control + alarm panel service)
template <typename OutboxT = std::nullptr_t>
inline void emit_area_action(std::string_view entity_id,
                             std::string_view action,
                             int level = 0,
                             std::string_view area_id = "",
                             std::string_view code = "",
                             OutboxT outbox = nullptr) {
  const std::string lvl_str = std::to_string(level);
  const std::string_view eff_area =
      !area_id.empty() ? area_id : std::string_view(latter_ui::runtime_state().area.active_area_id);
  latter_rpc::emit(outbox, "area_action", {
    {"entity_id", entity_id},
    {"action", action},
    {"level", lvl_str},
    {"area_id", eff_area},
    {"code", code},
  });
}

template <typename OutboxT = std::nullptr_t>
inline void emit_alarm_action(std::string_view alarm_service,
                              std::string_view code = "",
                              OutboxT outbox = nullptr) {
  latter_rpc::emit(outbox, "area_action", {
    {"action", alarm_service},
    {"code", code},
  });
}

// 8. set_active_area
template <typename OutboxT = std::nullptr_t>
inline void emit_set_active_area(std::string_view area_id, OutboxT outbox = nullptr) {
  latter_rpc::emit(outbox, "set_active_area", {
    {"area_id", area_id},
  });
}

// 9. remote_action
template <typename OutboxT = std::nullptr_t>
inline void emit_remote_action(int slot_1based,
                               std::string_view action,
                               std::string_view command = "",
                               OutboxT outbox = nullptr) {
  const std::string slot_str = std::to_string(std::max(1, slot_1based));
  latter_rpc::emit(outbox, "remote_action", {
    {"slot", slot_str},
    {"action", action},
    {"command", command},
  });
}

// 10. calendar_fetch
template <typename OutboxT = std::nullptr_t>
inline int emit_calendar_fetch(int page = 1,
                               int days = 30,
                               int page_size = 5,
                               OutboxT outbox = nullptr) {
  const int req_id = latter_rpc::issue_req_id(latter_rpc::SlotChannel::CALENDAR);
  const std::string req_str = std::to_string(req_id);
  const std::string days_str = std::to_string(std::max(1, days));
  const std::string page_str = std::to_string(std::max(1, page));
  const std::string size_str = std::to_string(std::max(1, page_size));
  latter_rpc::emit(outbox, "calendar_fetch", {
    {"req_id", req_str},
    {"days", days_str},
    {"page", page_str},
    {"page_size", size_str},
  });
  return req_id;
}

// 11. calendar_create_event
template <typename OutboxT = std::nullptr_t>
inline void emit_calendar_create_event(std::string_view summary,
                                       int in_minutes = 15,
                                       int duration_minutes = 30,
                                       OutboxT outbox = nullptr) {
  const std::string in_min_str = std::to_string(std::max(0, in_minutes));
  const std::string dur_min_str = std::to_string(std::max(1, duration_minutes));
  latter_rpc::emit(outbox, "calendar_create_event", {
    {"summary", summary},
    {"in_minutes", in_min_str},
    {"duration_minutes", dur_min_str},
  });
}

// 12. intercom_broadcast
template <typename OutboxT = std::nullptr_t>
inline void emit_intercom_broadcast(std::string_view message, OutboxT outbox = nullptr) {
  latter_rpc::emit(outbox, "intercom_broadcast", {
    {"message", message},
  });
}

// 13. doorbell_intercom
template <typename OutboxT = std::nullptr_t>
inline void emit_doorbell_intercom(std::string_view action,
                                   std::string_view camera_entity,
                                   int camera_index = 0,
                                   std::string_view message = "",
                                   std::string_view noise_reduction = "strong",
                                   std::string_view code = "",
                                   OutboxT outbox = nullptr) {
  const std::string idx_str = std::to_string(std::max(0, camera_index));
  latter_rpc::emit(outbox, "doorbell_intercom", {
    {"action", action},
    {"camera_entity", camera_entity},
    {"camera_index", idx_str},
    {"message", message},
    {"noise_reduction", noise_reduction},
    {"code", code},
  });
}

// 14. save_custom_color
template <typename OutboxT = std::nullptr_t>
inline void emit_save_custom_color(std::string_view hex_color, OutboxT outbox = nullptr) {
  latter_rpc::emit(outbox, "save_custom_color", {
    {"hex_color", hex_color},
  });
}

template <typename OutboxT = std::nullptr_t>
inline void emit_save_custom_color_rgb(uint32_t rgb24, OutboxT outbox = nullptr) {
  char buf[12];
  std::snprintf(buf, sizeof(buf), "#%06lX", static_cast<unsigned long>(rgb24 & 0xFFFFFFUL));
  emit_save_custom_color(buf, outbox);
}

// 15. set_dock_apps
template <typename OutboxT = std::nullptr_t>
inline void emit_set_dock_apps(std::string_view dock_apps_csv, OutboxT outbox = nullptr) {
  latter_rpc::emit(outbox, "set_dock_apps", {
    {"dock_apps", dock_apps_csv},
  });
}

}  // namespace latter_actions

namespace latter_ui {
using namespace latter_actions;
}  // namespace latter_ui
