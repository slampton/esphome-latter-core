#pragma once

// ==============================================================================
// Latter Core: Typed Outbound Protocol v3 RPC Action Helpers (latter_actions.h)
// ==============================================================================
// Provides typed C++ wrappers around latter_rpc::emit(...) for all 20 outbound
// RPC methods declared in protocol/latter_v3.json so C++ touch/UI handlers and
// YAML scripts emit RPC calls directly without scratch YAML globals.
// ==============================================================================

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "latter_rpc.h"
#include "state/device_runtime_state.h"

namespace latter_core {

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

TimerAlarmSyncTracker &timer_alarm_sync_tracker();

struct DeviceCommandEffects {
  bool stop_sound = false;
  bool sync_brightness = false;
  bool apply_power = false;
  bool mark_dirty_full = false;
};

namespace detail {

template <typename OutboxT>
inline void bind_outbox_if_present(OutboxT outbox) {
  if constexpr (!std::is_same_v<std::decay_t<OutboxT>, std::nullptr_t>) {
    latter_rpc::bind_sensor_sink(outbox);
  }
}

void emit_device_hello_impl();
void emit_device_telemetry_impl(std::string_view link_json,
                                int int_ram_free_kb,
                                int int_ram_min_kb,
                                int int_ram_largest_kb,
                                int psram_min_kb,
                                bool ble_active,
                                bool profiler_on);
void emit_display_state_impl(int power_state, int brightness_pct, bool screensaver_active);
int emit_music_browse_impl(std::string_view mode,
                           int page,
                           std::string_view search,
                           std::string_view speaker,
                           std::string_view last_speaker,
                           std::string_view album_sort,
                           std::string_view artist_album_sort,
                           std::string_view podcast_sort,
                           int page_size,
                           uint32_t now_ms);
int emit_music_lyrics_fetch_impl(std::string_view track_key,
                                 std::string_view speaker,
                                 uint32_t now_ms);
void emit_set_active_speaker_impl(std::string_view entity_id, bool transfer_queue);
void emit_media_command_impl(std::string_view command);
void emit_media_seek_impl(int seek_seconds);
void emit_media_play_item_impl(std::string_view entity_id,
                               std::string_view media_id,
                               std::string_view media_type,
                               std::string_view enqueue,
                               bool radio_mode);
void emit_media_queue_play_impl(std::string_view queue_item_id, std::string_view entity_id);
void emit_media_queue_move_impl(std::string_view queue_item_id,
                                std::string_view pos_shift,
                                std::string_view entity_id);
void emit_media_queue_move_int_impl(std::string_view queue_item_id,
                                    int pos_shift,
                                    std::string_view entity_id);
void emit_media_queue_delete_impl(std::string_view queue_item_id, std::string_view entity_id);
void emit_media_mute_toggle_impl(std::string_view entity_id);
void emit_media_shuffle_toggle_impl(std::string_view entity_id);
void emit_media_repeat_cycle_impl(std::string_view entity_id);
void emit_media_favorite_toggle_impl(std::string_view media_id, std::string_view entity_id);
void emit_speaker_join_impl(std::string_view active_speaker_entity,
                            std::string_view group_member_entity);
void emit_speaker_unjoin_impl(std::string_view target_speaker_entity);
void emit_media_volume_impl(std::string_view entity_id, std::string_view action, int volume_pct);
int emit_area_browse_entities_impl(std::string_view area_id,
                                   std::string_view domain_filter,
                                   int page,
                                   int page_size);
int emit_area_browse_list_impl(std::string_view area_id, int page, int page_size);
void emit_area_action_impl(std::string_view entity_id,
                           std::string_view action,
                           int level,
                           std::string_view area_id,
                           std::string_view code);
void emit_alarm_action_impl(std::string_view action, std::string_view code);
void emit_set_active_area_impl(std::string_view area_id);
void emit_remote_action_impl(int slot_1based,
                             std::string_view action,
                             std::string_view command,
                             float hold_secs);
int emit_calendar_fetch_window_impl(std::string_view start_date,
                                    std::string_view end_date,
                                    int days);
void emit_calendar_create_event_impl(std::string_view calendar_entity,
                                     std::string_view summary,
                                     bool all_day,
                                     std::string_view start_val,
                                     std::string_view end_val,
                                     std::string_view location,
                                     std::string_view rrule);
void emit_calendar_delete_event_impl(std::string_view calendar_entity,
                                     std::string_view uid,
                                     std::string_view recurrence_id);
void emit_intercom_broadcast_impl(std::string_view message);
void emit_doorbell_intercom_impl(std::string_view action,
                                 std::string_view camera_entity,
                                 int camera_index,
                                 std::string_view message,
                                 std::string_view noise_reduction);
void emit_save_custom_color_impl(std::string_view hex_color);
void emit_save_custom_color_rgb_impl(uint32_t rgb24);
void emit_shortcut_action_impl(std::string_view entity_id,
                               std::string_view action,
                               std::string_view code);
void emit_set_device_prefs_impl(uint32_t base_seq, std::string_view changes);
bool record_local_pref_edit_impl(PrefKey key,
                                 std::string_view val,
                                 uint32_t now_ms,
                                 uint32_t &out_effects);
void emit_timer_alarm_state_impl(bool timer_running,
                                 bool timer_paused,
                                 int remaining_sec,
                                 bool ringing,
                                 bool stopwatch_running,
                                 bool stopwatch_paused,
                                 int stopwatch_elapsed_sec);
bool check_and_emit_timer_alarm_state_impl(uint32_t now_ms, bool force_if_active, bool force_emit);
void sync_local_pref_key_edit_impl(PrefKey key);
DeviceCommandEffects handle_device_cmd_dismiss_ringing_impl(uint32_t now_ms);
DeviceCommandEffects handle_device_cmd_cancel_timer_impl(uint32_t now_ms);
DeviceCommandEffects handle_device_cmd_reset_stopwatch_impl(uint32_t now_ms);

}  // namespace detail

// 1. device_hello
template <typename OutboxT = std::nullptr_t>
inline void emit_device_hello(OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_device_hello_impl();
}

// 1b. device_telemetry (consolidated heap, link, BLE, and profiler telemetry)
template <typename OutboxT = std::nullptr_t>
inline void emit_device_telemetry(std::string_view link_json,
                                  int int_ram_free_kb,
                                  int int_ram_min_kb,
                                  int int_ram_largest_kb,
                                  int psram_min_kb,
                                  bool ble_active,
                                  bool profiler_on,
                                  OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_device_telemetry_impl(
      link_json, int_ram_free_kb, int_ram_min_kb, int_ram_largest_kb,
      psram_min_kb, ble_active, profiler_on);
}

// 1c. display_state (screen light & screensaver state sync)
template <typename OutboxT = std::nullptr_t>
inline void emit_display_state(int power_state,
                               int brightness_pct,
                               bool screensaver_active,
                               OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_display_state_impl(power_state, brightness_pct, screensaver_active);
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
  detail::bind_outbox_if_present(outbox);
  return detail::emit_music_browse_impl(
      mode, page, search, speaker, last_speaker, album_sort,
      artist_album_sort, podcast_sort, page_size, now_ms);
}

// Emits `music_browse` (mode=lyrics_fetch). `track_key` must use `build_music_track_key()`
// escaping (`\`/`|`/; -> `\\`/`\p`/`\s`), symmetric with HA `media/lyrics.py` decoding.
template <typename OutboxT = std::nullptr_t>
inline int emit_music_lyrics_fetch(std::string_view track_key = "",
                                   std::string_view speaker = "",
                                   uint32_t now_ms = 0,
                                   OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  return detail::emit_music_lyrics_fetch_impl(track_key, speaker, now_ms);
}

// 3. set_active_speaker
template <typename OutboxT = std::nullptr_t>
inline void emit_set_active_speaker(std::string_view entity_id,
                                    bool transfer_queue = false,
                                    OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_set_active_speaker_impl(entity_id, transfer_queue);
}

// 4. media_command (transport, seek, MA enqueue/radio, queue actions, toggles & grouping)
template <typename OutboxT = std::nullptr_t>
inline void emit_media_command(std::string_view command, OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_media_command_impl(command);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_seek(int seek_seconds, OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_media_seek_impl(seek_seconds);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_play_item(std::string_view entity_id,
                                 std::string_view media_id,
                                 std::string_view media_type,
                                 std::string_view enqueue = "play",
                                 bool radio_mode = false,
                                 OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_media_play_item_impl(entity_id, media_id, media_type, enqueue, radio_mode);
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
  detail::bind_outbox_if_present(outbox);
  detail::emit_media_queue_play_impl(queue_item_id, entity_id);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_queue_move(std::string_view queue_item_id,
                                  std::string_view pos_shift,
                                  std::string_view entity_id = "",
                                  OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_media_queue_move_impl(queue_item_id, pos_shift, entity_id);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_queue_move(std::string_view queue_item_id,
                                  int pos_shift,
                                  std::string_view entity_id = "",
                                  OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_media_queue_move_int_impl(queue_item_id, pos_shift, entity_id);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_queue_delete(std::string_view queue_item_id,
                                    std::string_view entity_id = "",
                                    OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_media_queue_delete_impl(queue_item_id, entity_id);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_mute_toggle(std::string_view entity_id = "", OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_media_mute_toggle_impl(entity_id);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_shuffle_toggle(std::string_view entity_id = "", OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_media_shuffle_toggle_impl(entity_id);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_repeat_cycle(std::string_view entity_id = "", OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_media_repeat_cycle_impl(entity_id);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_media_favorite_toggle(std::string_view media_id = "",
                                       std::string_view entity_id = "",
                                       OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_media_favorite_toggle_impl(media_id, entity_id);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_speaker_join(std::string_view active_speaker_entity,
                              std::string_view group_member_entity,
                              OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_speaker_join_impl(active_speaker_entity, group_member_entity);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_speaker_unjoin(std::string_view target_speaker_entity, OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_speaker_unjoin_impl(target_speaker_entity);
}

// 5. media_volume
template <typename OutboxT = std::nullptr_t>
inline void emit_media_volume(std::string_view entity_id,
                              std::string_view action,
                              int volume_pct = -1,
                              OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_media_volume_impl(entity_id, action, volume_pct);
}

// 6. area_browse (entities & areas list)
template <typename OutboxT = std::nullptr_t>
inline int emit_area_browse_entities(std::string_view area_id,
                                     std::string_view domain_filter,
                                     int page = 1,
                                     int page_size = LATTER_CARD_SLOT_COUNT,
                                     OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  return detail::emit_area_browse_entities_impl(area_id, domain_filter, page, page_size);
}

template <typename OutboxT = std::nullptr_t>
inline int emit_area_browse_list(std::string_view area_id,
                                 int page = 1,
                                 int page_size = LATTER_SLOT_COUNT,
                                 OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  return detail::emit_area_browse_list_impl(area_id, page, page_size);
}

// 7. area_action (entity control + alarm panel service)
template <typename OutboxT = std::nullptr_t>
inline void emit_area_action(std::string_view entity_id,
                             std::string_view action,
                             int level = 0,
                             std::string_view area_id = "",
                             std::string_view code = "",
                             OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_area_action_impl(entity_id, action, level, area_id, code);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_alarm_action(std::string_view action,
                              std::string_view code = "",
                              OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_alarm_action_impl(action, code);
}

// 8. set_active_area
template <typename OutboxT = std::nullptr_t>
inline void emit_set_active_area(std::string_view area_id, OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_set_active_area_impl(area_id);
}

// 9. remote_action
template <typename OutboxT = std::nullptr_t>
inline void emit_remote_action(int slot_1based,
                               std::string_view action,
                               std::string_view command = "",
                               OutboxT outbox = nullptr,
                               float hold_secs = 0.0f) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_remote_action_impl(slot_1based, action, command, hold_secs);
}

// 10. calendar_fetch (windowed events)
template <typename OutboxT = std::nullptr_t>
inline int emit_calendar_fetch_window(std::string_view start_date,
                                      std::string_view end_date,
                                      int days = 30,
                                      OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  return detail::emit_calendar_fetch_window_impl(start_date, end_date, days);
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
  detail::bind_outbox_if_present(outbox);
  detail::emit_calendar_create_event_impl(
      calendar_entity, summary, all_day, start_val, end_val, location, rrule);
}

// 11b. calendar_delete_event
template <typename OutboxT = std::nullptr_t>
inline void emit_calendar_delete_event(std::string_view calendar_entity,
                                       std::string_view uid,
                                       std::string_view recurrence_id = "",
                                       OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_calendar_delete_event_impl(calendar_entity, uid, recurrence_id);
}

// 12. intercom_broadcast
template <typename OutboxT = std::nullptr_t>
inline void emit_intercom_broadcast(std::string_view message, OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_intercom_broadcast_impl(message);
}

// 13. doorbell_intercom
template <typename OutboxT = std::nullptr_t>
inline void emit_doorbell_intercom(std::string_view action,
                                   std::string_view camera_entity,
                                   int camera_index = 0,
                                   std::string_view message = "",
                                   std::string_view noise_reduction = "strong",
                                   OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_doorbell_intercom_impl(
      action, camera_entity, camera_index, message, noise_reduction);
}

// 14. save_custom_color
template <typename OutboxT = std::nullptr_t>
inline void emit_save_custom_color(std::string_view hex_color, OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_save_custom_color_impl(hex_color);
}

template <typename OutboxT = std::nullptr_t>
inline void emit_save_custom_color_rgb(uint32_t rgb24, OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_save_custom_color_rgb_impl(rgb24);
}

// 15. shortcut_action (App Dock & Pinned App entity actions)
template <typename OutboxT = std::nullptr_t>
inline void emit_shortcut_action(std::string_view entity_id,
                                 std::string_view action,
                                 std::string_view code = "",
                                 OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_shortcut_action_impl(entity_id, action, code);
}

// 16. set_device_prefs (outbound local touchscreen/rotary preference edits)
template <typename OutboxT = std::nullptr_t>
inline void emit_set_device_prefs(uint32_t base_seq, std::string_view changes, OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_set_device_prefs_impl(base_seq, changes);
}

template <typename OutboxT = std::nullptr_t>
inline bool record_local_pref_edit(PrefKey key, std::string_view val,
                                   uint32_t now_ms, uint32_t &out_effects,
                                   OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  return detail::record_local_pref_edit_impl(key, val, now_ms, out_effects);
}

// 17. timer_alarm_state (event-driven timer, alarm & stopwatch transition sync to Tier 2 HA entities)
template <typename OutboxT = std::nullptr_t>
inline void emit_timer_alarm_state(bool timer_running, bool timer_paused,
                                   int remaining_sec, bool ringing,
                                   bool stopwatch_running = false,
                                   bool stopwatch_paused = false,
                                   int stopwatch_elapsed_sec = 0,
                                   OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::emit_timer_alarm_state_impl(
      timer_running, timer_paused, remaining_sec, ringing,
      stopwatch_running, stopwatch_paused, stopwatch_elapsed_sec);
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

template <typename OutboxT = std::nullptr_t>
inline bool check_and_emit_timer_alarm_state(uint32_t now_ms, bool force_if_active = false,
                                             OutboxT outbox = nullptr, bool force_emit = false) {
  detail::bind_outbox_if_present(outbox);
  return detail::check_and_emit_timer_alarm_state_impl(now_ms, force_if_active, force_emit);
}

// Persist local alarm mutations (Time App toggle/editor, Home quick-disable, Once auto-clear)
// and push alarms_summary upstream via Protocol v3 set_device_prefs when HA is connected.
template <typename OutboxT = std::nullptr_t>
inline void sync_local_pref_key_edit(PrefKey key, OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  detail::sync_local_pref_key_edit_impl(key);
}

template <typename OutboxT = std::nullptr_t>
inline void sync_local_alarms_edit(uint32_t now_ms = 0, OutboxT outbox = nullptr) {
  (void)now_ms;
  sync_local_pref_key_edit(PrefKey::ALARMS_SUMMARY, outbox);
}

template <typename OutboxT = std::nullptr_t>
inline DeviceCommandEffects handle_device_cmd_dismiss_ringing(uint32_t now_ms,
                                                              OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  return detail::handle_device_cmd_dismiss_ringing_impl(now_ms);
}

template <typename OutboxT = std::nullptr_t>
inline DeviceCommandEffects handle_device_cmd_cancel_timer(uint32_t now_ms,
                                                           OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  return detail::handle_device_cmd_cancel_timer_impl(now_ms);
}

template <typename OutboxT = std::nullptr_t>
inline DeviceCommandEffects handle_device_cmd_reset_stopwatch(uint32_t now_ms,
                                                              OutboxT outbox = nullptr) {
  detail::bind_outbox_if_present(outbox);
  return detail::handle_device_cmd_reset_stopwatch_impl(now_ms);
}

}  // namespace latter_core
