#pragma once

// ==============================================================================
// Latter Core SDK: Typed Outbound Protocol v3 RPC Action Helpers (latter_actions.h)
// ==============================================================================
// Provides typed C++ wrappers around latter_rpc::emit(...) for all 20 outbound
// RPC methods declared in protocol/latter_v3.json so C++ touch/UI handlers and
// YAML scripts emit RPC calls directly without scratch YAML globals.
// ==============================================================================

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "hal_manifest.h"
#include "latter_rpc.h"
#include "state/device_runtime_state.h"

namespace latter_actions {

// 1. device_hello
template <typename OutboxT = std::nullptr_t>
inline void emit_device_hello(OutboxT outbox = nullptr) {
  latter_ui::ensure_persisted_prefs_loaded();
  const auto &prefs = latter_ui::persisted_prefs();
  const std::string boot_str = std::to_string(latter_ui::device_boot_id());
  const std::string manifest_str = latter_ui::build_hardware_manifest_json();
  const std::string seq_str = std::to_string(prefs.prefs_seq);
  const std::string dirty_str = latter_ui::build_hello_dirty_prefs(prefs);
  latter_rpc::emit(outbox, "device_hello", {
    {"contract", "3"},
    {"boot_id", boot_str},
    {"manifest", manifest_str},
    {"prefs_seq", seq_str},
    {"dirty_prefs", dirty_str},
  });
}

// 1b. device_telemetry (Pillar 3 internal telemetry consolidation)
template <typename OutboxT = std::nullptr_t>
inline void emit_device_telemetry(std::string_view link_json,
                                  int int_ram_free_kb,
                                  int int_ram_min_kb,
                                  int int_ram_largest_kb,
                                  int psram_min_kb,
                                  bool ble_active,
                                  bool profiler_on,
                                  OutboxT outbox = nullptr) {
  const std::string free_str = std::to_string(std::max(0, int_ram_free_kb));
  const std::string min_str = std::to_string(std::max(0, int_ram_min_kb));
  const std::string blk_str = std::to_string(std::max(0, int_ram_largest_kb));
  const std::string ps_str = std::to_string(std::max(0, psram_min_kb));
  latter_rpc::emit(outbox, "device_telemetry", {
    {"link_json", link_json},
    {"int_ram_free_kb", free_str},
    {"int_ram_min_kb", min_str},
    {"int_ram_largest_kb", blk_str},
    {"psram_min_kb", ps_str},
    {"ble_active", ble_active ? "1" : "0"},
    {"profiler_on", profiler_on ? "1" : "0"},
  });
}

// 1c. display_state (Pillar 6 screen light & screensaver state sync)
template <typename OutboxT = std::nullptr_t>
inline void emit_display_state(int power_state,
                               int brightness_pct,
                               bool screensaver_active,
                               OutboxT outbox = nullptr) {
  const std::string pwr_str = std::to_string(std::clamp(power_state, 0, 2));
  const std::string bri_str = std::to_string(std::clamp(brightness_pct, 1, 100));
  latter_rpc::emit(outbox, "display_state", {
    {"power_state", pwr_str},
    {"brightness_pct", bri_str},
    {"screensaver_active", screensaver_active ? "1" : "0"},
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
                             int page_size = LATTER_SLOT_COUNT,
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

template <typename OutboxT = std::nullptr_t>
inline int emit_music_lyrics_fetch(std::string_view track_key = "",
                                   std::string_view speaker = "",
                                   uint32_t now_ms = 0,
                                   OutboxT outbox = nullptr) {
  auto &rt = latter_ui::runtime_state();
  const auto &ms = latter_ui::media_state();
  if (now_ms != 0U) rt.last_music_page_req_ms = now_ms;
  auto &lyr = rt.music.lyrics;
  lyr.latest_req_id = (lyr.latest_req_id >= 1000000) ? 1 : (lyr.latest_req_id + 1);
  lyr.loading = true;
  const int req_id = lyr.latest_req_id;
  const std::string req_str = std::to_string(req_id);
  const std::string fallback_key = latter_ui::build_music_track_key(ms.title, ms.artist);
  const std::string_view eff_key = !track_key.empty() ? track_key : std::string_view(fallback_key);
  const std::string_view eff_spk =
      !speaker.empty() ? speaker : std::string_view(rt.music.active_speaker_entity);
  const std::string_view eff_last_spk(ms.last_speaker);

  latter_rpc::emit(outbox, "music_browse", {
    {"req_id", req_str},
    {"mode", "lyrics_fetch"},
    {"page", "1"},
    {"page_size", "128"},
    {"search", eff_key},
    {"speaker", eff_spk},
    {"last_speaker", eff_last_spk},
    {"album_sort", "name"},
    {"artist_album_sort", "year"},
    {"podcast_sort", "newest"},
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

// 4. media_command (transport, seek, MA enqueue/radio, queue actions, toggles & grouping)
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
                                 std::string_view enqueue = "play",
                                 bool radio_mode = false,
                                 OutboxT outbox = nullptr) {
  const std::string_view eff_spk =
      !entity_id.empty() ? entity_id
                         : std::string_view(latter_ui::runtime_state().music.active_speaker_entity);
  const std::string_view eff_enq = enqueue.empty() ? std::string_view("play") : enqueue;
  latter_rpc::emit(outbox, "media_command", {
    {"command", "play_media"},
    {"entity_id", eff_spk},
    {"media_id", media_id},
    {"media_type", media_type},
    {"enqueue", eff_enq},
    {"radio_mode", radio_mode ? "true" : "false"},
  });
}

template <typename OutboxT>
  requires (!std::is_same_v<std::decay_t<OutboxT>, bool> &&
            !std::is_same_v<std::decay_t<OutboxT>, std::nullptr_t>)
inline void emit_media_play_item(std::string_view entity_id,
                                 std::string_view media_id,
                                 std::string_view media_type,
                                 std::string_view enqueue,
                                 OutboxT outbox) {
  emit_media_play_item(entity_id, media_id, media_type, enqueue, false, outbox);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_queue_play(std::string_view queue_item_id,
                                  std::string_view entity_id = "",
                                  OutboxT outbox = nullptr) {
  const std::string_view eff_spk =
      !entity_id.empty() ? entity_id
                         : std::string_view(latter_ui::runtime_state().music.active_speaker_entity);
  latter_rpc::emit(outbox, "media_command", {
    {"command", "queue_play"},
    {"entity_id", eff_spk},
    {"queue_item_id", queue_item_id},
  });
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_queue_move(std::string_view queue_item_id,
                                  std::string_view pos_shift,
                                  std::string_view entity_id = "",
                                  OutboxT outbox = nullptr) {
  const std::string_view eff_spk =
      !entity_id.empty() ? entity_id
                         : std::string_view(latter_ui::runtime_state().music.active_speaker_entity);
  latter_rpc::emit(outbox, "media_command", {
    {"command", "queue_move"},
    {"entity_id", eff_spk},
    {"queue_item_id", queue_item_id},
    {"pos_shift", pos_shift},
  });
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_queue_move(std::string_view queue_item_id,
                                  int pos_shift,
                                  std::string_view entity_id = "",
                                  OutboxT outbox = nullptr) {
  const std::string shift_str = std::to_string(pos_shift);
  emit_media_queue_move(queue_item_id, shift_str, entity_id, outbox);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_queue_delete(std::string_view queue_item_id,
                                    std::string_view entity_id = "",
                                    OutboxT outbox = nullptr) {
  const std::string_view eff_spk =
      !entity_id.empty() ? entity_id
                         : std::string_view(latter_ui::runtime_state().music.active_speaker_entity);
  latter_rpc::emit(outbox, "media_command", {
    {"command", "queue_delete"},
    {"entity_id", eff_spk},
    {"queue_item_id", queue_item_id},
  });
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_mute_toggle(std::string_view entity_id = "", OutboxT outbox = nullptr) {
  const std::string_view eff_spk =
      !entity_id.empty() ? entity_id
                         : std::string_view(latter_ui::runtime_state().music.active_speaker_entity);
  latter_rpc::emit(outbox, "media_command", {
    {"command", "mute_toggle"},
    {"entity_id", eff_spk},
  });
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_shuffle_toggle(std::string_view entity_id = "", OutboxT outbox = nullptr) {
  const std::string_view eff_spk =
      !entity_id.empty() ? entity_id
                         : std::string_view(latter_ui::runtime_state().music.active_speaker_entity);
  latter_rpc::emit(outbox, "media_command", {
    {"command", "shuffle_toggle"},
    {"entity_id", eff_spk},
  });
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_repeat_cycle(std::string_view entity_id = "", OutboxT outbox = nullptr) {
  const std::string_view eff_spk =
      !entity_id.empty() ? entity_id
                         : std::string_view(latter_ui::runtime_state().music.active_speaker_entity);
  latter_rpc::emit(outbox, "media_command", {
    {"command", "repeat_cycle"},
    {"entity_id", eff_spk},
  });
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_favorite_toggle(std::string_view media_id = "",
                                       std::string_view entity_id = "",
                                       OutboxT outbox = nullptr) {
  const std::string_view eff_spk =
      !entity_id.empty() ? entity_id
                         : std::string_view(latter_ui::runtime_state().music.active_speaker_entity);
  latter_rpc::emit(outbox, "media_command", {
    {"command", "favorite_toggle"},
    {"entity_id", eff_spk},
    {"media_id", media_id},
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
                                     int page_size = LATTER_CARD_SLOT_COUNT,
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
                                 int page_size = LATTER_SLOT_COUNT,
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
  if (code.empty()) {
    latter_rpc::emit(outbox, "area_action", {
      {"entity_id", entity_id},
      {"action", action},
      {"level", lvl_str},
      {"area_id", eff_area},
      {"code", ""},
    });
    return;
  }
  std::string wrapped = latter_rpc::wrap_pin_code(entity_id, action, code);
  if (wrapped.empty()) {
#ifdef ESP_LOGW
    ESP_LOGW("latter_rpc", "Blocked area_action PIN dispatch: session key unavailable or invalid PIN");
#endif
    return;
  }
  latter_rpc::emit(outbox, "area_action", {
    {"entity_id", entity_id},
    {"action", action},
    {"level", lvl_str},
    {"area_id", eff_area},
    {"code", wrapped},
  });
  latter_rpc::secure_zero_string(wrapped);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_alarm_action(std::string_view action,
                              std::string_view code = "",
                              OutboxT outbox = nullptr) {
  if (code.empty()) {
    latter_rpc::emit(outbox, "area_action", {
      {"entity_id", "home_hub.alarm"},
      {"action", action},
    });
    return;
  }
  std::string wrapped = latter_rpc::wrap_pin_code("home_hub.alarm", action, code);
  if (wrapped.empty()) {
#ifdef ESP_LOGW
    ESP_LOGW("latter_rpc", "Blocked alarm_action PIN dispatch: session key unavailable or invalid PIN");
#endif
    return;
  }
  latter_rpc::emit(outbox, "area_action", {
    {"entity_id", "home_hub.alarm"},
    {"action", action},
    {"code", wrapped},
  });
  latter_rpc::secure_zero_string(wrapped);
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
                               OutboxT outbox = nullptr,
                               float hold_secs = 0.0f) {
  const std::string slot_str = std::to_string(std::max(1, slot_1based));
  if (hold_secs > 0.0f) {
    char hold_buf[16];
    std::snprintf(hold_buf, sizeof(hold_buf), "%.2f", static_cast<double>(hold_secs));
    latter_rpc::emit(outbox, "remote_action", {
      {"slot", slot_str},
      {"action", action},
      {"command", command},
      {"hold_secs", hold_buf},
    });
    return;
  }
  latter_rpc::emit(outbox, "remote_action", {
    {"slot", slot_str},
    {"action", action},
    {"command", command},
  });
}

// 10. calendar_fetch (windowed events)
template <typename OutboxT = std::nullptr_t>
inline int emit_calendar_fetch_window(std::string_view start_date,
                                      std::string_view end_date,
                                      int days = 30,
                                      OutboxT outbox = nullptr) {
  const int req_id = latter_rpc::issue_req_id(latter_rpc::SlotChannel::CALENDAR);
  const std::string req_str = std::to_string(req_id);
  const std::string days_str = std::to_string(std::max(1, days));
  latter_rpc::emit(outbox, "calendar_fetch", {
    {"req_id", req_str},
    {"mode", "window"},
    {"start_date", start_date},
    {"end_date", end_date},
    {"days", days_str},
  });
  return req_id;
}

// 11. calendar_create_event
template <typename OutboxT = std::nullptr_t>
inline void emit_calendar_create_event(std::string_view calendar_entity,
                                       std::string_view summary,
                                       bool all_day,
                                       std::string_view start_val,
                                       std::string_view end_val,
                                       std::string_view location = "",
                                       std::string_view rrule = "",
                                       OutboxT outbox = nullptr) {
  if (all_day) {
    latter_rpc::emit(outbox, "calendar_create_event", {
      {"calendar_entity", calendar_entity},
      {"summary", summary},
      {"all_day", "1"},
      {"start_date", start_val},
      {"end_date", end_val},
      {"location", location},
      {"rrule", rrule},
    });
  } else {
    latter_rpc::emit(outbox, "calendar_create_event", {
      {"calendar_entity", calendar_entity},
      {"summary", summary},
      {"all_day", "0"},
      {"start_date_time", start_val},
      {"end_date_time", end_val},
      {"location", location},
      {"rrule", rrule},
    });
  }
}

// 11b. calendar_delete_event
template <typename OutboxT = std::nullptr_t>
inline void emit_calendar_delete_event(std::string_view calendar_entity,
                                       std::string_view uid,
                                       std::string_view recurrence_id = "",
                                       OutboxT outbox = nullptr) {
  latter_rpc::emit(outbox, "calendar_delete_event", {
    {"calendar_entity", calendar_entity},
    {"uid", uid},
    {"recurrence_id", recurrence_id},
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
                                   OutboxT outbox = nullptr) {
  const std::string idx_str = std::to_string(std::max(0, camera_index));
  latter_rpc::emit(outbox, "doorbell_intercom", {
    {"action", action},
    {"camera_entity", camera_entity},
    {"camera_index", idx_str},
    {"message", message},
    {"noise_reduction", noise_reduction},
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

// 15. shortcut_action (App Dock & Pinned App entity actions)
template <typename OutboxT = std::nullptr_t>
inline void emit_shortcut_action(std::string_view entity_id,
                                 std::string_view action,
                                 std::string_view code = "",
                                 OutboxT outbox = nullptr) {
  if (code.empty()) {
    latter_rpc::emit(outbox, "shortcut_action", {
      {"entity_id", entity_id},
      {"action", action},
      {"code", ""},
    });
    return;
  }
  std::string wrapped = latter_rpc::wrap_pin_code(entity_id, action, code);
  if (wrapped.empty()) {
#ifdef ESP_LOGW
    ESP_LOGW("latter_rpc", "Blocked shortcut_action PIN dispatch: session key unavailable or invalid PIN");
#endif
    return;
  }
  latter_rpc::emit(outbox, "shortcut_action", {
    {"entity_id", entity_id},
    {"action", action},
    {"code", wrapped},
  });
  latter_rpc::secure_zero_string(wrapped);
}

// 16. set_device_prefs (outbound local touchscreen/rotary preference edits)
template <typename OutboxT = std::nullptr_t>
inline void emit_set_device_prefs(uint32_t base_seq, std::string_view changes, OutboxT outbox = nullptr) {
  if (changes.empty()) return;
  const std::string seq_str = std::to_string(base_seq);
  latter_rpc::emit(outbox, "set_device_prefs", {
    {"base_seq", seq_str},
    {"changes", changes},
  });
}

template <typename OutboxT = std::nullptr_t>
inline bool record_local_pref_edit(latter_ui::PrefKey key, std::string_view val,
                                   uint32_t now_ms, uint32_t &out_effects,
                                   OutboxT outbox = nullptr) {
  out_effects = 0;
  auto &p = latter_ui::persisted_prefs();
  latter_ui::PrefMask changed_mask{};
  if (!latter_ui::set_pref_by_key_str(p, key, val, true, changed_mask, out_effects)) {
    return false;
  }
  p.dirty_mask |= changed_mask;
  latter_ui::schedule_persisted_prefs_save(now_ms);
  if (latter_ui::link_state().ha_connected) {
    const std::string changes = latter_ui::encode_prefs_mask(changed_mask, p);
    emit_set_device_prefs(p.prefs_seq, changes, outbox);
  }
  return true;
}

// 17. timer_alarm_state (event-driven timer, alarm & stopwatch transition sync to Tier 2 HA entities)
template <typename OutboxT = std::nullptr_t>
inline void emit_timer_alarm_state(bool timer_running, bool timer_paused,
                                   int remaining_sec, bool ringing,
                                   bool stopwatch_running = false,
                                   bool stopwatch_paused = false,
                                   int stopwatch_elapsed_sec = 0,
                                   OutboxT outbox = nullptr) {
  const std::string rem_str = std::to_string(std::max(0, remaining_sec));
  const std::string sw_el_str = std::to_string(std::max(0, stopwatch_elapsed_sec));
  latter_rpc::emit(outbox, "timer_alarm_state", {
    {"timer_running", timer_running ? "1" : "0"},
    {"timer_paused", timer_paused ? "1" : "0"},
    {"remaining_sec", rem_str},
    {"ringing", ringing ? "1" : "0"},
    {"stopwatch_running", stopwatch_running ? "1" : "0"},
    {"stopwatch_paused", stopwatch_paused ? "1" : "0"},
    {"stopwatch_elapsed_sec", sw_el_str},
  });
}

template <typename OutboxT>
  requires (!std::is_same_v<std::decay_t<OutboxT>, bool> &&
            !std::is_same_v<std::decay_t<OutboxT>, std::nullptr_t>)
inline void emit_timer_alarm_state(bool timer_running, bool timer_paused,
                                   int remaining_sec, bool ringing,
                                   OutboxT outbox) {
  emit_timer_alarm_state(timer_running, timer_paused, remaining_sec, ringing,
                         false, false, 0, outbox);
}

struct TimerAlarmSyncTracker {
  bool inited = false;
  bool pending_sync = false;
  bool running = false;
  bool paused = false;
  bool ringing = false;
  int last_emitted_rem = 0;
  bool sw_running = false;
  bool sw_paused = false;
  int last_emitted_sw_sec = 0;
  uint32_t last_emitted_ms = 0;
};

inline TimerAlarmSyncTracker &timer_alarm_sync_tracker() {
  static TimerAlarmSyncTracker tr{};
  return tr;
}

template <typename OutboxT = std::nullptr_t>
inline bool check_and_emit_timer_alarm_state(uint32_t now_ms, bool force_if_active = false,
                                             OutboxT outbox = nullptr, bool force_emit = false) {
  const auto &rs = latter_ui::runtime_state();
  const bool cur_run = rs.timers.running;
  const bool cur_pau = !cur_run && rs.timers.paused;
  const int cur_rem = (cur_run || cur_pau) ? std::max(0, rs.timers.remaining_sec) : 0;
  const bool cur_ring = (rs.alarms.ringing || rs.timers.ringing);
  const bool cur_sw_run = rs.stopwatch.running;
  const bool cur_sw_pau = !cur_sw_run && (rs.stopwatch.elapsed_ms > 0U);
  const uint32_t raw_sw_ms =
      cur_sw_run
          ? ((now_ms >= rs.stopwatch.start_time_ms) ? (now_ms - rs.stopwatch.start_time_ms)
                                                    : rs.stopwatch.elapsed_ms)
          : rs.stopwatch.elapsed_ms;
  const int cur_sw_sec = (cur_sw_run || cur_sw_pau) ? std::max(0, static_cast<int>(raw_sw_ms / 1000U)) : 0;

  auto &tr = timer_alarm_sync_tracker();
  const bool any_active = (cur_run || cur_pau || cur_ring || cur_sw_run || cur_sw_pau);
  bool should_emit = false;
  if (force_emit || tr.pending_sync) {
    should_emit = true;
  } else if (force_if_active && any_active) {
    should_emit = true;
  } else if (!tr.inited) {
    if (!any_active && latter_ui::link_state().ha_connected) {
      tr.inited = true;
    }
    should_emit = any_active;
  } else if (cur_run != tr.running || cur_pau != tr.paused || cur_ring != tr.ringing ||
             cur_sw_run != tr.sw_running || cur_sw_pau != tr.sw_paused) {
    should_emit = true;
  } else {
    const int elapsed_s = static_cast<int>((now_ms - tr.last_emitted_ms) / 1000U);
    if (cur_run) {
      const int expected_rem = std::max(0, tr.last_emitted_rem - elapsed_s);
      if (std::abs(cur_rem - expected_rem) > 3) {
        should_emit = true;
      }
    } else if (cur_pau && cur_rem != tr.last_emitted_rem) {
      should_emit = true;
    }
    if (!should_emit) {
      if (cur_sw_run) {
        const int expected_sw = tr.last_emitted_sw_sec + elapsed_s;
        if (std::abs(cur_sw_sec - expected_sw) > 3) {
          should_emit = true;
        }
      } else if (cur_sw_sec != tr.last_emitted_sw_sec) {
        should_emit = true;
      }
    }
  }

  if (!should_emit) return false;
  if (!latter_ui::link_state().ha_connected) {
    tr.pending_sync = true;
    return false;
  }
  tr.inited = true;
  tr.pending_sync = false;
  tr.running = cur_run;
  tr.paused = cur_pau;
  tr.ringing = cur_ring;
  tr.last_emitted_rem = cur_rem;
  tr.sw_running = cur_sw_run;
  tr.sw_paused = cur_sw_pau;
  tr.last_emitted_sw_sec = cur_sw_sec;
  tr.last_emitted_ms = now_ms;
  emit_timer_alarm_state(cur_run, cur_pau, cur_rem, cur_ring,
                         cur_sw_run, cur_sw_pau, cur_sw_sec, outbox);
  return true;
}

// Persist local alarm mutations (Time App toggle/editor, Home quick-disable, Once auto-clear)
// and push alarms_summary upstream via Protocol v3 set_device_prefs when HA is connected.
template <typename OutboxT = std::nullptr_t>
inline void sync_local_pref_key_edit(latter_ui::PrefKey key, OutboxT outbox = nullptr) {
  auto &p = latter_ui::persisted_prefs();
  const latter_ui::PrefMask bit = latter_ui::pref_key_bit(key);
  p.dirty_mask |= bit;
  latter_ui::save_persisted_prefs();
  if (latter_ui::link_state().ha_connected) {
    const std::string changes = latter_ui::encode_prefs_mask(bit, p);
    emit_set_device_prefs(p.prefs_seq, changes, outbox);
  }
}

template <typename OutboxT = std::nullptr_t>
inline void sync_local_alarms_edit(uint32_t now_ms = 0, OutboxT outbox = nullptr) {
  (void)now_ms;
  sync_local_pref_key_edit(latter_ui::PrefKey::ALARMS_SUMMARY, outbox);
}

struct DeviceCommandEffects {
  bool stop_sound = false;
  bool sync_brightness = false;
  bool apply_power = false;
  bool mark_dirty_full = false;
};

template <typename OutboxT = std::nullptr_t>
inline DeviceCommandEffects handle_device_cmd_dismiss_ringing(uint32_t now_ms,
                                                              OutboxT outbox = nullptr) {
  auto &rs = latter_ui::runtime_state();
  auto &prefs = latter_ui::persisted_prefs();
  const bool was_any = (rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active);
  if (rs.alarms.ringing) {
    if (rs.alarms.ringing_idx >= 1 && rs.alarms.ringing_idx <= 3) {
      auto &ap = prefs.alarms[rs.alarms.ringing_idx - 1];
      if (ap.recurrence == 3) {
        ap.enabled = false;
        sync_local_alarms_edit(now_ms, outbox);
      }
    }
    rs.alarms.snooze_until = 0;
    rs.alarms.snoozed_alarm_slot = 0;
  } else if (rs.alarms.snooze_until != 0U) {
    rs.alarms.snooze_until = 0;
    rs.alarms.snoozed_alarm_slot = 0;
  }
  if (rs.timers.ringing) {
    rs.timers.va_timer_name.clear();
    latter_ui::sync_multi_timer_state();
  }
  (void)latter_ui::timer_alarm_controller().dismiss_ringing();
  rs.nav.ui_dirty_full = true;
  (void)check_and_emit_timer_alarm_state(now_ms, false, outbox, /*force_emit=*/true);
  return DeviceCommandEffects{was_any, was_any, true, true};
}

template <typename OutboxT = std::nullptr_t>
inline DeviceCommandEffects handle_device_cmd_cancel_timer(uint32_t now_ms,
                                                           OutboxT outbox = nullptr) {
  auto &rs = latter_ui::runtime_state();
  auto *slots = latter_ui::timer_slots();
  for (int i = 0; i < 3; ++i) {
    slots[i].running = false;
    slots[i].paused = false;
    slots[i].remaining_sec = slots[i].duration_sec;
    slots[i].target_ms = 0;
  }
  rs.timers.va_timer_name.clear();
  const bool was_ringing = rs.timers.ringing;
  rs.timers.ringing = false;
  if (was_ringing && !rs.alarms.ringing && !rs.alarms.find_my_active) {
    latter_ui::timer_alarm_controller().reset();
  }
  latter_ui::sync_multi_timer_state();
  if (rs.nav.home_subpage == 2 && !rs.timers.running && !rs.stopwatch.running) {
    rs.nav.home_subpage = 0;
  }
  rs.nav.ui_dirty_full = true;
  (void)check_and_emit_timer_alarm_state(now_ms, false, outbox, /*force_emit=*/true);
  return DeviceCommandEffects{was_ringing, was_ringing, true, true};
}

template <typename OutboxT = std::nullptr_t>
inline DeviceCommandEffects handle_device_cmd_reset_stopwatch(uint32_t now_ms,
                                                              OutboxT outbox = nullptr) {
  auto &rs = latter_ui::runtime_state();
  rs.stopwatch.reset();
  if (rs.nav.home_subpage == 2 && !rs.timers.running && !rs.stopwatch.running) {
    rs.nav.home_subpage = 0;
  }
  rs.nav.ui_dirty_full = true;
  (void)check_and_emit_timer_alarm_state(now_ms, false, outbox, /*force_emit=*/true);
  return DeviceCommandEffects{false, false, true, true};
}

}  // namespace latter_actions

namespace latter_ui {
using namespace latter_actions;
}  // namespace latter_ui
