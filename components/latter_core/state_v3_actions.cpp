#if __has_include("state/device_runtime_state.h")
#include "state/device_runtime_state.h"
#include "latter_rpc.h"
#else
#include "../../include/state/device_runtime_state.h"
#include "../../include/latter_rpc.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace latter_core {

int parse_calendar_sources_meta(std::string_view raw,
                                CalendarSourceMeta *out,
                                int max_out) noexcept {
  if (raw.empty() || out == nullptr || max_out <= 0) return 0;
  const int cap = std::min(max_out, kMaxCalendarSources);
  std::string_view recs[kMaxCalendarSources]{};
  const size_t n = v3_split_fields(raw, recs, static_cast<size_t>(cap), kProtocolRecordSep);
  int count = 0;
  for (size_t i = 0; i < n; ++i) {
    std::string_view cols[6]{};
    const size_t cn = v3_split_fields(recs[i], cols, 6, '|');
    if (cn != 5U || cols[0].empty()) continue;
    CalendarSourceMeta item{};
    copy_bounded_cstr(item.entity_id, v3_unescape_subfield(cols[0]));
    copy_bounded_cstr(item.label, v3_unescape_subfield(cols[1].empty() ? cols[0] : cols[1]));
    item.color_idx = static_cast<uint8_t>(std::clamp(v3_parse_int(cols[2], count), 0, 5));
    item.can_create = v3_parse_bool(cols[3]);
    item.can_delete = v3_parse_bool(cols[4]);
    out[count++] = item;
  }
  return count;
}

int collect_writable_calendar_sources(std::string_view raw,
                                      CalendarSourceMeta *out,
                                      int *out_source_indices,
                                      int max_out) noexcept {
  if (raw.empty() || max_out <= 0) return 0;
  std::string_view recs[kMaxCalendarSources]{};
  const size_t n = v3_split_fields(raw, recs, static_cast<size_t>(kMaxCalendarSources), kProtocolRecordSep);
  int src_idx = 0;
  int wcnt = 0;
  for (size_t i = 0; i < n; ++i) {
    std::string_view cols[6]{};
    const size_t cn = v3_split_fields(recs[i], cols, 6, '|');
    if (cn != 5U || cols[0].empty()) continue;
    const bool can_create = (cols[3] == "1" || cols[3] == "true");
    if (can_create && wcnt < max_out) {
      if (out != nullptr) {
        CalendarSourceMeta item{};
        copy_bounded_cstr(item.entity_id, v3_unescape_subfield(cols[0]));
        copy_bounded_cstr(item.label, v3_unescape_subfield(cols[1].empty() ? cols[0] : cols[1]));
        item.color_idx = static_cast<uint8_t>(std::clamp(v3_parse_int(cols[2], src_idx), 0, 5));
        item.can_create = true;
        item.can_delete = (cols[4] == "1" || cols[4] == "true");
        out[wcnt] = item;
      }
      if (out_source_indices != nullptr) out_source_indices[wcnt] = src_idx;
      ++wcnt;
    }
    ++src_idx;
  }
  return wcnt;
}

int parse_calendar_locations_catalog(std::string_view raw,
                                     std::string *out,
                                     int max_out) {
  if (raw.empty() || out == nullptr || max_out <= 0) return 0;
  const int cap = std::min(max_out, kMaxCalendarLocations);
  std::string_view recs[kMaxCalendarLocations]{};
  const size_t n = v3_split_fields(raw, recs, static_cast<size_t>(cap), kProtocolRecordSep);
  int count = 0;
  for (size_t i = 0; i < n; ++i) {
    const std::string_view sv = trim_ascii_ws(recs[i]);
    if (sv.empty()) continue;
    const std::string unescaped = v3_unescape_subfield(sv);
    const std::string_view cleaned = trim_ascii_ws(unescaped);
    if (!cleaned.empty()) {
      out[count++] = std::string(cleaned);
    }
  }
  return count;
}

// Populates up to `kMaxCalendarSummaryPreviewEvents` preview cards from `calendar_state().summary`
// into PSRAM/heap scratch (`< 256 B` stack, zero mutation of `calendar_window_store()`).
int populate_calendar_summary_preview(
    std::string_view summary,
    int32_t today_ymd,
    CalendarWindowEvent *out_events,
    int max_out) noexcept {
  if (summary.empty() || out_events == nullptr || max_out <= 0) return 0;
  const int cap = std::min(max_out, kMaxCalendarSummaryPreviewEvents);
  std::string_view recs[kMaxCalendarSummaryPreviewEvents]{};
  const size_t n = v3_split_fields(summary, recs, static_cast<size_t>(cap), kProtocolRecordSep);
  int count = 0;
  for (size_t i = 0; i < n && count < cap; ++i) {
    std::string_view f[4]{};
    if (v3_split_fields(recs[i], f, 4, '|') != 4) continue;
    auto &dst = out_events[count];
    dst.reset_in_place();
    dst.start_ymd = today_ymd;
    dst.end_ymd = today_ymd;
    dst.all_day = (f[3] == "1") ? 1U : 0U;
    copy_bounded_cstr(dst.title, f[1].empty() ? std::string("Event") : v3_unescape_subfield(f[1]));
    copy_bounded_cstr(dst.location, v3_unescape_subfield(f[2]));
    ++count;
  }
  return count;
}

bool parse_calendar_window_event_record(std::string_view rec, CalendarWindowEvent &out) noexcept {
  if (rec.empty()) return false;
  std::string_view f[12]{};
  const size_t n = v3_split_fields(rec, f, 12, kProtocolFieldSep);
  if (n != 11U) return false;
  const int32_t s_ymd = parse_ymd_iso(f[0]);
  if (s_ymd == 0) return false;
  int32_t e_ymd = parse_ymd_iso(f[1]);
  if (e_ymd < s_ymd) e_ymd = s_ymd;

  out.reset_in_place();
  out.start_ymd = s_ymd;
  out.end_ymd = e_ymd;
  out.start_min = static_cast<int16_t>(std::clamp(v3_parse_int(f[2], -1), -1, 1439));
  out.end_min = static_cast<int16_t>(std::clamp(v3_parse_int(f[3], -1), -1, 1440));
  out.all_day = v3_parse_bool(f[4]) ? 1U : 0U;
  out.cal_idx = static_cast<uint8_t>(std::clamp(v3_parse_int(f[5], 0), 0, 7));
  copy_bounded_cstr(out.uid, f[6]);
  copy_bounded_cstr(out.recurrence_id, f[7]);
  copy_bounded_cstr(out.title, f[8].empty() ? std::string_view("Event") : f[8]);
  copy_bounded_cstr(out.location, f[9]);
  copy_bounded_cstr(out.description, f[10]);
  return true;
}

bool apply_calendar_view_action(int req_id,
                                int chunk_index,
                                int chunk_count,
                                std::string_view window_start,
                                std::string_view window_end,
                                std::string_view events,
                                CalendarWindowStore &store,
                                DeviceRuntimeState &rs) {
  if (!latter_rpc::accept_req_id(latter_rpc::SlotChannel::CALENDAR, req_id)) {
    return false;
  }
  if (req_id > 0 && store.last_applied_req_id > 0 && req_id < store.last_applied_req_id) {
    return false;
  }
  if (chunk_index < 0 || chunk_count <= 0 || chunk_index >= chunk_count || chunk_count > 16) {
    return false;
  }
  if (chunk_index == 0) {
    store.staging_req_id = req_id;
    store.staging_expected_chunks = chunk_count;
    store.staging_next_chunk = 0;
    store.staging_start_ymd = parse_ymd_iso(window_start);
    store.staging_end_ymd = parse_ymd_iso(window_end);
    store.staging_count = 0;
  } else if (req_id != store.staging_req_id ||
             chunk_index != store.staging_next_chunk ||
             chunk_count != store.staging_expected_chunks) {
    store.staging_expected_chunks = 0;
    store.staging_next_chunk = 0;
    store.staging_count = 0;
    return false;
  }

  if (!events.empty()) {
    size_t pos = 0;
    while (pos <= events.size() && store.staging_count < kMaxCalendarWindowEvents) {
      const size_t sep = events.find(kProtocolRecordSep, pos);
      const std::string_view rec =
          (sep == std::string_view::npos) ? events.substr(pos) : events.substr(pos, sep - pos);
      if (parse_calendar_window_event_record(rec, store.staging_events[store.staging_count])) {
        store.staging_count++;
      }
      if (sep == std::string_view::npos) break;
      pos = sep + 1;
    }
  }

  store.staging_next_chunk++;
  if (store.staging_next_chunk < store.staging_expected_chunks) {
    return false;
  }

  if (req_id > store.last_applied_req_id) {
    store.last_applied_req_id = req_id;
  }
  store.count = store.staging_count;
  for (int i = 0; i < store.count; ++i) {
    store.events[i] = store.staging_events[i];
  }
  if (store.staging_start_ymd > 0) store.window_start_ymd = store.staging_start_ymd;
  if (store.staging_end_ymd > 0) store.window_end_ymd = store.staging_end_ymd;
  store.loading = false;
  store.staging_expected_chunks = 0;
  store.staging_next_chunk = 0;
  if (rs.cal.selected_event_idx >= store.count) {
    rs.cal.selected_event_idx = -1;
    if (rs.cal.drilldown == CalDrilldown::DETAIL) {
      rs.cal.drilldown = CalDrilldown::NONE;
      rs.cal.delete_confirm_open = false;
    }
  }
  store.rev++;
  return true;
}

namespace {

// Parse packed browse/area slots directly into out_slots without stack-allocating a full page array.
bool parse_v3_slots_payload_into(std::string_view slots_blob, BrowseSlotItem *out_slots,
                                 size_t max_slots, size_t *out_valid_cnt = nullptr) {
  if (out_slots == nullptr || max_slots == 0U) {
    if (out_valid_cnt != nullptr) *out_valid_cnt = 0U;
    return false;
  }
  bool changed = false;
  size_t valid_cnt = 0U;
  if (!slots_blob.empty()) {
    std::string_view recs[16];
    const size_t rec_cnt = v3_split_fields(slots_blob, recs, (max_slots < 16U) ? max_slots : 16U, '\x1e');
    for (size_t i = 0; i < rec_cnt && valid_cnt < max_slots; ++i) {
      if (recs[i].empty()) continue;
      std::string_view cols[8];
      const size_t col_cnt = v3_split_fields(recs[i], cols, 8, '\x1f');
      if (col_cnt < 2U) continue;
      const std::string_view name = cols[0];
      const std::string_view id = cols[1];
      const std::string_view type = (col_cnt >= 3U) ? cols[2] : std::string_view{};
      const bool active = (col_cnt >= 4U) ? v3_parse_bool(cols[3]) : false;
      const int level = (col_cnt >= 5U) ? v3_parse_int(cols[4], -1) : -1;
      const std::string_view queue_item_id = (col_cnt >= 6U) ? cols[5] : std::string_view{};
      const bool is_current = (col_cnt >= 7U) ? v3_parse_bool(cols[6]) : false;

      BrowseSlotItem &dst = out_slots[valid_cnt];
      if (dst.active != active) {
        dst.active = active;
        changed = true;
      }
      if (dst.level != level) {
        dst.level = level;
        changed = true;
      }
      if (dst.is_current != is_current) {
        dst.is_current = is_current;
        changed = true;
      }
      if (dst.name != name) {
        dst.name.assign(name);
        changed = true;
      }
      if (dst.id != id) {
        dst.id.assign(id);
        changed = true;
      }
      if (dst.type != type) {
        dst.type.assign(type);
        changed = true;
      }
      if (dst.queue_item_id != queue_item_id) {
        dst.queue_item_id.assign(queue_item_id);
        changed = true;
      }
      ++valid_cnt;
    }
  }
  for (size_t i = valid_cnt; i < max_slots; ++i) {
    BrowseSlotItem &dst = out_slots[i];
    if (dst.active || dst.level != -1 || dst.is_current ||
        !dst.name.empty() || !dst.id.empty() || !dst.type.empty() || !dst.queue_item_id.empty()) {
      dst = BrowseSlotItem{};
      changed = true;
    }
  }
  if (out_valid_cnt != nullptr) *out_valid_cnt = valid_cnt;
  return changed;
}

}  // namespace

// Parse Contract v3 packed browse/area slots ("name\x1fid\x1ftype\x1factive\x1flevel[\x1fqueue_item_id\x1fis_current]\x1e...")
size_t parse_v3_slots_payload(std::string_view slots_blob, BrowseSlotItem *out_slots, size_t max_slots) {
  size_t valid_cnt = 0U;
  parse_v3_slots_payload_into(slots_blob, out_slots, max_slots, &valid_cnt);
  return valid_cnt;
}

void append_music_lyrics_records_blob(std::string_view blob,
                                      std::vector<MusicLyricLine> &dst,
                                      size_t max_total) {
  const std::string_view trimmed = trim_ascii_ws(blob);
  if (trimmed.empty() || dst.size() >= max_total) return;

  // Canonical Protocol v3 record format: 0x1E-joined "<t_ms>\x1f<text>" lines.
  size_t pos = 0;
  while (pos <= blob.size() && dst.size() < max_total) {
    const size_t sep = blob.find(kProtocolRecordSep, pos);
    const std::string_view rec =
        (sep == std::string_view::npos) ? blob.substr(pos) : blob.substr(pos, sep - pos);
    if (!rec.empty()) {
      const size_t fsep = rec.find(kProtocolFieldSep);
      if (fsep != std::string_view::npos) {
        MusicLyricLine line{};
        line.t_ms = static_cast<int32_t>(v3_parse_int(rec.substr(0, fsep), -1));
        line.text = std::string(rec.substr(fsep + 1U));
        dst.push_back(std::move(line));
      }
    }
    if (sep == std::string_view::npos) break;
    pos = sep + 1U;
  }
}

bool apply_music_lyrics_action(int req_id,
                               int chunk_index,
                               int chunk_count,
                               std::string_view track_key,
                               bool synced,
                               std::string_view lines_blob,
                               MusicLyricsState &st) {
  if (req_id > 0 && st.latest_req_id > 0 && req_id < st.latest_req_id) {
    return false;
  }
  const int eff_chunk_count = (chunk_count <= 0) ? 1 : chunk_count;
  const int eff_chunk_index = (chunk_count <= 0) ? 0 : chunk_index;
  if (eff_chunk_index < 0 || eff_chunk_index >= eff_chunk_count || eff_chunk_count > 16) {
    return false;
  }

  if (eff_chunk_index == 0) {
    st.staging_req_id = req_id;
    st.staging_expected_chunks = eff_chunk_count;
    st.staging_next_chunk = 0;
    st.staging_track_key = std::string(track_key);
    st.staging_synced = synced;
    st.staging_lines.clear();
    st.staging_lines.reserve(std::min<size_t>(kMaxMusicLyricLines, 64U));
  } else if (req_id != st.staging_req_id ||
             eff_chunk_index != st.staging_next_chunk ||
             eff_chunk_count != st.staging_expected_chunks) {
    st.staging_expected_chunks = 0;
    st.staging_next_chunk = 0;
    st.staging_lines.clear();
    return false;
  }

  append_music_lyrics_records_blob(lines_blob, st.staging_lines, kMaxMusicLyricLines);
  st.staging_next_chunk = eff_chunk_index + 1;
  if (st.staging_next_chunk < st.staging_expected_chunks) {
    return false;
  }

  st.track_key = std::move(st.staging_track_key);
  st.synced = st.staging_synced;
  st.lines = std::move(st.staging_lines);
  if (st.synced && st.lines.size() > 1U) {
    std::stable_sort(st.lines.begin(), st.lines.end(),
                     [](const MusicLyricLine &a, const MusicLyricLine &b) {
                       return a.t_ms < b.t_ms;
                     });
  }
  st.loading = false;
  st.active_line_idx = -1;
  st.scroll_px = 0;
  st.target_scroll_px = 0;
  st.from_scroll_px = 0;
  st.scroll_anim_start_ms = 0;
  st.manual_override_until_ms = 0;
  st.staging_expected_chunks = 0;
  st.staging_next_chunk = 0;
  st.staging_lines.clear();
  st.rev++;
  return true;
}

// Shared slot-response applier for both set_browse_slots and set_area_slots.
bool apply_slots_action(int req_id, std::string_view mode,
                        int page, int total_pages, std::string_view slots_blob,
                        uint32_t now_ms, bool force_area_entities,
                        DeviceRuntimeState &rt, bool *out_changed) {
  const latter_rpc::SlotChannel ch =
      force_area_entities ? latter_rpc::SlotChannel::AREA_ENTITIES
                          : latter_rpc::classify_browse_mode(mode);
  if (!latter_rpc::accept_req_id(ch, req_id)) {
    if (out_changed != nullptr) *out_changed = false;
    return false;
  }

  static_assert(DeviceRuntimeState::kListSlotsPerPage >= DeviceRuntimeState::kCardSlotsPerPage);
  const size_t max_items =
      (ch == latter_rpc::SlotChannel::AREA_ENTITIES) ? DeviceRuntimeState::kCardSlotsPerPage
                                                     : DeviceRuntimeState::kListSlotsPerPage;

  int new_page = rt.slot_page_for(ch);
  if (ch == latter_rpc::SlotChannel::MUSIC) {
    if (now_ms == 0 || (now_ms - rt.last_music_page_req_ms) > 350U || page == rt.slot_page_for(ch)) {
      new_page = page;
    }
  } else {
    new_page = page;
  }
  const std::string new_browse_mode =
      (ch == latter_rpc::SlotChannel::AREA_ENTITIES)
          ? (force_area_entities ? std::string(mode) : std::string("entities"))
          : rt.area.browse_mode;

  BrowseSlotItem *cur = rt.slots_for(ch);
  bool changed = (new_page != rt.slot_page_for(ch)) ||
                 (total_pages != rt.slot_total_pages_for(ch)) ||
                 ((ch == latter_rpc::SlotChannel::AREA_ENTITIES) && (new_browse_mode != rt.area.browse_mode));
  if (parse_v3_slots_payload_into(slots_blob, cur, max_items)) {
    changed = true;
  }
  rt.slot_page_for(ch) = new_page;
  rt.slot_total_pages_for(ch) = total_pages;
  if (ch == latter_rpc::SlotChannel::AREA_ENTITIES) {
    rt.area.browse_mode = new_browse_mode;
  }
  if (out_changed != nullptr) *out_changed = changed;
  return true;
}

void sync_multi_timer_globals(bool &out_any_running, bool &out_any_paused,
                              int &out_min_rem_sec, int &out_active_dur_sec,
                              uint32_t &out_min_target_ms, int view_slot) {
  auto *slots = timer_slots();
  const int eff_view = (view_slot >= 0) ? view_slot : runtime_state().timers.view_slot;
  bool any_run = false;
  bool any_pau = false;
  int min_rem = 0;
  int act_dur = slots[0].duration_sec;
  uint32_t min_t = 0;

  for (int i = 0; i < 3; i++) {
    if (slots[i].running) {
      if (!any_run || slots[i].remaining_sec < min_rem) {
        min_rem = slots[i].remaining_sec;
        act_dur = slots[i].duration_sec;
        min_t   = slots[i].target_ms;
      }
      any_run = true;
    } else if (slots[i].paused && !any_run) {
      if (!any_pau) {
        min_rem = slots[i].remaining_sec;
        act_dur = slots[i].duration_sec;
      }
      any_pau = true;
    }
  }
  if (!any_run && !any_pau) {
    int v_idx = (eff_view >= 1 && eff_view <= 3) ? (eff_view - 1) : 0;
    min_rem = slots[v_idx].duration_sec;
    act_dur = slots[v_idx].duration_sec;
  }
  out_any_running    = any_run;
  out_any_paused     = any_pau;
  out_min_rem_sec    = min_rem;
  out_active_dur_sec = act_dur;
  out_min_target_ms  = min_t;
}

int upsert_voice_timer_slot(const std::string &id_str, const std::string &name_str,
                            int total_sec, int left_sec, bool is_act, uint32_t now_ms) {
  auto *slots = timer_slots();
  int target = -1;

  for (int i = 0; i < 3; i++) {
    if (!id_str.empty() && slots[i].id == id_str) { target = i; break; }
    if (!name_str.empty() && !slots[i].name.empty()) {
      std::string a = slots[i].name, b = name_str;
      for (char &c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      for (char &c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (a == b) { target = i; break; }
    }
  }

  if (target < 0) {
    if (!slots[0].running && !slots[0].paused) {
      if (!slots[1].running && !slots[1].paused && !slots[2].running && !slots[2].paused) {
        slots[2] = slots[1];
        slots[1] = slots[0];
      }
      target = 0;
    } else if (!slots[1].running && !slots[1].paused) {
      if (!slots[2].running && !slots[2].paused) {
        slots[2] = slots[1];
      }
      target = 1;
    } else {
      target = 2;
    }
  }

  if (total_sec <= 0) total_sec = std::max(60, left_sec);
  bool same_id = (!id_str.empty() && slots[target].id == id_str);
  slots[target].id = id_str;
  if (!name_str.empty() || !same_id) {
    slots[target].name = name_str;
  }
  slots[target].duration_sec  = total_sec;
  slots[target].remaining_sec = left_sec;
  slots[target].target_ms     = now_ms + static_cast<uint32_t>(left_sec) * 1000U;
  slots[target].running       = is_act;
  slots[target].paused        = !is_act;
  return target;
}

void parse_latter_remote_slots_summary(const std::string &summary) {
  std::string *t_ents = remote_slots_ent();
  std::string *t_names = remote_slots_name();
  bool *st_on = remote_slot_states_on();
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    t_ents[i].clear();
    t_names[i].clear();
    st_on[i] = false;
  }
  if (!summary.empty()) {
    std::string_view recs[MAX_REMOTE_SLOTS];
    const size_t n = v3_split_fields(summary, recs, MAX_REMOTE_SLOTS, kProtocolRecordSep);
    for (size_t r = 0; r < n; r++) {
      std::string_view f[6];
      if (v3_split_fields(recs[r], f, 6, '|') != 6 || f[2].empty()) continue;
      const int slot_num = v3_parse_int(f[0], 0);
      if (slot_num < 1 || slot_num > MAX_REMOTE_SLOTS) continue;
      const int idx = slot_num - 1;
      const std::string_view st = f[5];
      st_on[idx] = (st == "on" || st == "playing" || st == "paused" || st == "buffering" || st == "idle");
      t_ents[idx] = v3_unescape_subfield(f[2]);
      t_names[idx] = f[1].empty() ? format_remote_entity_fallback(t_ents[idx]) : v3_unescape_subfield(f[1]);
    }
  }
  remote_control_page() = 0;
}

int collect_configured_remote_slots(int out_slots[MAX_REMOTE_SLOTS]) {
  const std::string *t_ents = remote_slots_ent();
  int count = 0;
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    if (is_configured_remote_slot(t_ents[i])) {
      out_slots[count++] = i;
    }
  }
  return count;
}

int resolve_active_remote_slot(int active_slot) {
  const std::string *t_ents = remote_slots_ent();
  if (active_slot >= 0 && active_slot < MAX_REMOTE_SLOTS && is_configured_remote_slot(t_ents[active_slot])) {
    return active_slot;
  }
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    if (is_configured_remote_slot(t_ents[i])) return i;
  }
  return 0;
}

std::string resolve_active_carousel_camera_id(const std::string &cameras_summary,
                                              const std::string &fallback_cam_url) {
  const auto &rs = runtime_state();
  if (rs.alert.active && !rs.alert.camera_entity.empty()) {
    return rs.alert.camera_entity;
  }
  CameraRecord *cams = camera_records_scratch();
  const int cnt = parse_cameras_records(cameras_summary, cams, MAX_CAMERA_SLOTS);
  if (cnt > 0) {
    int idx = cam_carousel_idx();
    if (rs.nav.gui_page == PageId::CAMERAS && rs.cameras.tab == CamerasTab::CAROUSEL) {
      int car[MAX_CAMERA_SLOTS]{};
      const int cc = collect_carousel_camera_indices(cams, cnt, car, MAX_CAMERA_SLOTS);
      if (cc > 0) idx = car[std::clamp(rs.cameras.carousel_idx, 0, cc - 1)];
    }
    if (idx < 0 || idx >= cnt) {
      cam_carousel_idx() = 0;
      idx = 0;
    }
    return cams[idx].entity_id;
  }
  const size_t epos = fallback_cam_url.find("entity_id=");
  if (epos != std::string::npos) {
    const size_t start = epos + 10;
    const size_t amp = fallback_cam_url.find('&', start);
    return (amp == std::string::npos)
               ? fallback_cam_url.substr(start)
               : fallback_cam_url.substr(start, amp - start);
  }
  return "";
}

std::string build_carousel_camera_url(const std::string &base_cam_url,
                                      const std::string &cameras_summary,
                                      uint32_t now_ms) {
  if (base_cam_url.empty()) return "";
  const auto &rs = runtime_state();
  const std::string ent = resolve_active_carousel_camera_id(cameras_summary, base_cam_url);
  return build_camera_snapshot_url(
      base_cam_url, ent, rs.cameras.effective_fill(persisted_prefs()), cam_landscape_mode(), now_ms);
}

bool dispatch_v3_domain_push(std::string_view domain, std::string_view payload, uint32_t now_ms) {
  if (domain == "area_context") {
    return parse_v3_area_ctx_payload(payload, area_ctx_state(), home_hub_state());
  }
  if (domain == "active_media") {
    return parse_v3_media_payload(payload, media_state(), now_ms);
  }
  if (domain == "remote_config") {
    return parse_v3_remote_payload(payload, remote_state());
  }
  if (domain == "weather_forecast") {
    return parse_v3_weather_payload(payload, weather_state());
  }
  if (domain == "calendar_agenda") {
    return parse_v3_calendar_payload(payload, calendar_state());
  }
  if (domain == "device_prefs") {
    uint32_t eff_mask = 0U;
    const bool parsed = parse_v3_device_prefs_payload(payload, eff_mask, now_ms);
    device_prefs_state().last_effects = eff_mask;
    return parsed;
  }
  if (domain == "shortcuts") {
    return parse_v3_shortcuts_payload(payload, shortcuts_state());
  }
  return false;
}

bool try_handle_pin_session_key_command(std::string_view cmd) {
  static constexpr std::string_view kPrefix = "set_pin_key:";
  if (cmd.size() <= kPrefix.size() || !cmd.starts_with(kPrefix)) {
    return false;
  }
  const std::string_view rest = cmd.substr(kPrefix.size());
  const size_t sep = rest.find(':');
  if (sep != std::string_view::npos) {
    (void)latter_rpc::set_pin_session_key(rest.substr(0U, sep), rest.substr(sep + 1U));
  }
  return true;
}

}  // namespace latter_core
