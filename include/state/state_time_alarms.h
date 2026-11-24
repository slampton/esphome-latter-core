#pragma once

// ==============================================================================
// Latter Core SDK: Timers, Stopwatch, Alarms & World Clock Catalog/Codec
// (include/state/state_time_alarms.h)
// ==============================================================================

#include "state/state_common.h"
#include "state/state_prefs.h"
#include "state/state_areas.h"

namespace latter_core {

struct MultiTimerSlot {
  std::string id = "";
  std::string name = "";
  int duration_sec = 300;
  int remaining_sec = 300;
  uint32_t target_ms = 0;
  bool running = false;
  bool paused = false;
};

struct TimerRuntimeState {
  MultiTimerSlot slots[3] = {
      {"", "", 300, 300, 0, false, false},
      {"", "", 600, 600, 0, false, false},
      {"", "", 900, 900, 0, false, false},
  };
  int view_slot = 0;  // 0 = 3-Timer Landing Page, 1..3 = Single-Timer Detail/Editor
  int duration_sec = 300;
  int remaining_sec = 300;
  uint32_t target_ms = 0;
  bool running = false;
  bool paused = false;
  bool ringing = false;
  std::string va_timer_name = "";
};

struct StopwatchRuntimeState {
  static constexpr uint8_t kMaxLapSplits = 8;
  bool running = false;
  bool started_in_analog = false;
  uint32_t start_time_ms = 0;
  uint32_t elapsed_ms = 0;
  uint32_t lap_ms = 0;
  uint32_t lap_splits_ms[kMaxLapSplits]{};
  uint8_t lap_count = 0;

  void record_lap_elapsed(uint32_t cur_elapsed_ms) {
    const uint32_t split_ms = (cur_elapsed_ms >= lap_ms) ? (cur_elapsed_ms - lap_ms) : cur_elapsed_ms;
    lap_splits_ms[lap_count % kMaxLapSplits] = split_ms;
    if (lap_count < 255U) {
      ++lap_count;
    } else {
      lap_count = static_cast<uint8_t>((lap_count % kMaxLapSplits) + kMaxLapSplits);
    }
    lap_ms = cur_elapsed_ms;
  }

  void reset() {
    running = false;
    started_in_analog = false;
    start_time_ms = 0;
    elapsed_ms = 0;
    lap_ms = 0;
    for (uint32_t &sp : lap_splits_ms) sp = 0;
    lap_count = 0;
  }
};

struct AlarmRuntimeState {
  int edit_idx = 0;
  bool ringing = false;
  int ringing_idx = 0;
  int snoozed_alarm_slot = 0;
  uint32_t snooze_until = 0;
  uint32_t last_fired_epoch_min = 0;
  bool find_my_active = false;
  uint32_t find_my_start_ms = 0;
};


inline std::string format_agenda_time_label(std::string_view raw_lbl, bool is_24h) {
  if (is_24h || raw_lbl.size() < 6) return std::string(raw_lbl);
  const size_t t_pos = raw_lbl.size() - 5;
  if (raw_lbl[t_pos - 1] != ' ' || raw_lbl[t_pos + 2] != ':') return std::string(raw_lbl);
  auto is_dig = [](char c) { return c >= '0' && c <= '9'; };
  if (!is_dig(raw_lbl[t_pos]) || !is_dig(raw_lbl[t_pos + 1]) ||
      !is_dig(raw_lbl[t_pos + 3]) || !is_dig(raw_lbl[t_pos + 4])) {
    return std::string(raw_lbl);
  }
  const int hh = (raw_lbl[t_pos] - '0') * 10 + (raw_lbl[t_pos + 1] - '0');
  const int mm = (raw_lbl[t_pos + 3] - '0') * 10 + (raw_lbl[t_pos + 4] - '0');
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return std::string(raw_lbl);
  const int h12 = (hh % 12 == 0) ? 12 : (hh % 12);
  const char *ampm = (hh >= 12) ? "PM" : "AM";
  char buf[24];
  std::snprintf(buf, sizeof(buf), " %d:%02d %s", h12, mm, ampm);
  std::string out(raw_lbl.substr(0, t_pos - 1));
  out += buf;
  return out;
}

inline void format_schedule_range_label(int start_hr, int end_hr, bool is_24h, char *out, size_t cap) {
  if (!out || cap == 0) return;
  const int sh = ((start_hr % 24) + 24) % 24;
  const int eh = ((end_hr % 24) + 24) % 24;
  if (is_24h) {
    std::snprintf(out, cap, "Schedule: %02d:00-%02d:00", sh, eh);
  } else {
    const int sh12 = (sh % 12 == 0) ? 12 : (sh % 12);
    const int eh12 = (eh % 12 == 0) ? 12 : (eh % 12);
    std::snprintf(out, cap, "Schedule: %d%s-%d%s", sh12, (sh >= 12) ? "PM" : "AM",
                  eh12, (eh >= 12) ? "PM" : "AM");
  }
}


// ==============================================================================
// Time App: World Clock Catalog, TZ Metadata Parsers & Alarm Summary Codec
// ==============================================================================
static constexpr int kMaxWorldClocks = 4;

struct WorldClockCatalogItem {
  const char *iana;
  const char *city;
  int16_t std_offset_min;
};

inline constexpr WorldClockCatalogItem kWorldClockCatalog[24] = {
    {"America/Los_Angeles", "Los Angeles", -480},
    {"America/Denver", "Denver", -420},
    {"America/Chicago", "Chicago", -360},
    {"America/New_York", "New York", -300},
    {"America/Toronto", "Toronto", -300},
    {"America/Sao_Paulo", "Sao Paulo", -180},
    {"America/Argentina/Buenos_Aires", "Buenos Aires", -180},
    {"Pacific/Honolulu", "Honolulu", -600},
    {"America/Anchorage", "Anchorage", -540},
    {"Europe/London", "London", 0},
    {"Europe/Paris", "Paris", 60},
    {"Europe/Berlin", "Berlin", 60},
    {"Europe/Zurich", "Zurich", 60},
    {"Africa/Cairo", "Cairo", 120},
    {"Africa/Johannesburg", "Johannesburg", 120},
    {"Asia/Dubai", "Dubai", 240},
    {"Asia/Kolkata", "Kolkata", 330},
    {"Asia/Bangkok", "Bangkok", 420},
    {"Asia/Singapore", "Singapore", 480},
    {"Asia/Hong_Kong", "Hong Kong", 480},
    {"Asia/Shanghai", "Shanghai", 480},
    {"Asia/Tokyo", "Tokyo", 540},
    {"Asia/Seoul", "Seoul", 540},
    {"Australia/Sydney", "Sydney", 600},
};
inline constexpr size_t kWorldClockCatalogCount =
    sizeof(kWorldClockCatalog) / sizeof(kWorldClockCatalog[0]);

struct LocalTzInfo {
  std::string iana = "";
  std::string city = "";
  std::string abbr = "";
  int utc_offset_min = 0;
  bool is_dst = false;
  bool valid = false;
};

struct WorldClockEntry {
  std::string iana = "";
  std::string city = "";
  std::string abbr = "";
  int utc_offset_min = 0;
  bool is_dst = false;
  bool is_pinned_utc = false;
};

inline std::string format_utc_offset_str(int offset_min) {
  if (offset_min == 0) return "UTC+0";
  const char sign = (offset_min >= 0) ? '+' : '-';
  const int abs_m = std::abs(offset_min);
  const int h = abs_m / 60;
  const int m = abs_m % 60;
  char buf[16];
  if (m == 0) {
    std::snprintf(buf, sizeof(buf), "UTC%c%d", sign, h);
  } else {
    std::snprintf(buf, sizeof(buf), "UTC%c%d:%02d", sign, h, m);
  }
  return std::string(buf);
}

inline std::string city_from_iana_fallback(std::string_view iana) {
  for (const auto &item : kWorldClockCatalog) {
    if (iana == item.iana) return item.city;
  }
  const size_t slash = iana.rfind('/');
  std::string city(slash == std::string_view::npos ? iana : iana.substr(slash + 1));
  for (char &c : city) {
    if (c == '_') c = ' ';
  }
  return city;
}

inline int catalog_std_offset_min_for_iana(std::string_view iana, bool *found = nullptr) {
  for (const auto &item : kWorldClockCatalog) {
    if (iana == item.iana) {
      if (found != nullptr) *found = true;
      return item.std_offset_min;
    }
  }
  if (found != nullptr) *found = false;
  return 0;
}

inline bool is_valid_iana_zone_token(std::string_view sv) noexcept {
  if (sv.empty() || sv.size() > 48) return false;
  for (char c : sv) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '+';
    if (!ok) return false;
  }
  return true;
}

inline std::string sanitize_clock_label(std::string_view sv, size_t max_bytes = 32) {
  std::string cleaned;
  cleaned.reserve(sv.size() < max_bytes ? sv.size() : max_bytes);
  bool prev_space = true;
  for (unsigned char uc : sv) {
    if (uc < 0x20U || uc == 0x7FU || uc == '|' || uc == ';' || uc == 0x1EU || uc == 0x1FU ||
        uc == ' ' || uc == '\t' || uc == '\r' || uc == '\n') {
      if (!prev_space) {
        cleaned.push_back(' ');
        prev_space = true;
      }
      continue;
    }
    cleaned.push_back(static_cast<char>(uc));
    prev_space = false;
  }
  while (!cleaned.empty() && cleaned.back() == ' ') {
    cleaned.pop_back();
  }
  if (cleaned.size() > max_bytes) {
    size_t cut = max_bytes;
    while (cut > 0 && (static_cast<unsigned char>(cleaned[cut]) & 0xC0U) == 0x80U) {
      --cut;
    }
    cleaned.resize(cut);
    while (!cleaned.empty() && cleaned.back() == ' ') {
      cleaned.pop_back();
    }
  }
  return cleaned;
}

struct WorldClockSpecItem {
  std::string label = "";
  std::string iana = "";
};

inline int parse_world_clocks_spec(std::string_view spec, WorldClockSpecItem out_items[kMaxWorldClocks]) {
  auto eq_ascii_ci = [](std::string_view a, std::string_view b) noexcept -> bool {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      unsigned char ca = static_cast<unsigned char>(a[i]);
      unsigned char cb = static_cast<unsigned char>(b[i]);
      if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + ('a' - 'A'));
      if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + ('a' - 'A'));
      if (ca != cb) return false;
    }
    return true;
  };
  int count = 0;
  size_t start = 0;
  while (start <= spec.size() && count < kMaxWorldClocks) {
    size_t pos = spec.find(';', start);
    if (pos == std::string_view::npos) pos = spec.size();
    std::string_view rec = trim_ascii_ws(spec.substr(start, pos - start));
    start = pos + 1;
    if (rec.empty()) continue;
    const size_t pipe = rec.find('|');
    if (pipe == std::string_view::npos) continue;
    const std::string_view raw_lbl = rec.substr(0, pipe);
    const std::string_view iana = trim_ascii_ws(rec.substr(pipe + 1));
    if (!is_valid_iana_zone_token(iana)) continue;
    if (iana == "UTC" || iana == "Etc/UTC" || iana == "Etc/GMT" || iana == "GMT") continue;
    std::string lbl = sanitize_clock_label(raw_lbl);
    if (lbl.empty()) lbl = city_from_iana_fallback(iana);
    bool dup = false;
    for (int i = 0; i < count; ++i) {
      if (out_items[i].iana == iana && eq_ascii_ci(out_items[i].label, lbl)) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      out_items[count++] = WorldClockSpecItem{std::move(lbl), std::string(iana)};
    }
  }
  return count;
}

inline std::string normalize_world_clocks_spec(std::string_view spec) {
  WorldClockSpecItem items[kMaxWorldClocks];
  const int count = parse_world_clocks_spec(spec, items);
  std::string out;
  for (int i = 0; i < count; ++i) {
    if (i > 0) out.push_back(';');
    out.append(items[i].label);
    out.push_back('|');
    out.append(items[i].iana);
  }
  return out;
}

inline LocalTzInfo parse_local_tz_info(std::string_view raw) {
  LocalTzInfo info{};
  if (raw.empty()) return info;
  std::string_view cols[5];
  if (v3_split_fields(raw, cols, 5, '|') != 5 || cols[0].empty()) return info;
  info.iana = v3_unescape_subfield(cols[0]);
  info.city = cols[1].empty() ? city_from_iana_fallback(info.iana) : v3_unescape_subfield(cols[1]);
  info.utc_offset_min = std::clamp(v3_parse_int(cols[3], 0), -840, 840);
  info.abbr = cols[2].empty() ? format_utc_offset_str(info.utc_offset_min) : v3_unescape_subfield(cols[2]);
  info.is_dst = v3_parse_bool(cols[4]);
  info.valid = true;
  return info;
}

inline int collect_active_world_clocks(const PersistedDevicePrefs &p,
                                       const AreaContextState &ac,
                                       WorldClockEntry out_entries[kMaxWorldClocks + 1]) {
  int total = 0;
  if (p.show_utc_clock) {
    out_entries[total++] = WorldClockEntry{"UTC", "UTC", "UTC", 0, false, true};
  }
  WorldClockSpecItem items[kMaxWorldClocks];
  const int z_cnt = parse_world_clocks_spec(p.world_clocks_spec, items);
  if (z_cnt == 0) return total;

  std::string_view summary_recs[8];
  const size_t s_cnt = ac.world_clocks_summary.empty()
                           ? 0
                           : v3_split_fields(ac.world_clocks_summary, summary_recs, 8, ';');
  bool used_rec[8]{};

  for (int i = 0; i < z_cnt && total < (kMaxWorldClocks + 1); ++i) {
    const std::string &iana = items[i].iana;
    const std::string &custom_lbl = items[i].label;
    int match_r = -1;
    std::string_view matched_cols[5];
    for (size_t r = 0; r < s_cnt; ++r) {
      if (used_rec[r]) continue;
      std::string_view cols[5];
      if (v3_split_fields(summary_recs[r], cols, 5, '|') == 5 && cols[0] == iana) {
        if (cols[1] == custom_lbl) {
          for (int c = 0; c < 5; ++c) matched_cols[c] = cols[c];
          match_r = static_cast<int>(r);
          break;
        }
        if (match_r < 0) {
          for (int c = 0; c < 5; ++c) matched_cols[c] = cols[c];
          match_r = static_cast<int>(r);
        }
      }
    }
    if (match_r >= 0) {
      used_rec[match_r] = true;
      const int off = std::clamp(v3_parse_int(matched_cols[3], 0), -840, 840);
      out_entries[total++] = WorldClockEntry{
          iana,
          custom_lbl.empty() ? city_from_iana_fallback(iana) : custom_lbl,
          matched_cols[2].empty() ? format_utc_offset_str(off) : v3_unescape_subfield(matched_cols[2]),
          off,
          v3_parse_bool(matched_cols[4]),
          false,
      };
    } else {
      const int off = catalog_std_offset_min_for_iana(iana);
      out_entries[total++] = WorldClockEntry{
          iana,
          custom_lbl.empty() ? city_from_iana_fallback(iana) : custom_lbl,
          format_utc_offset_str(off),
          off,
          false,
          false,
      };
    }
  }
  return total;
}

inline int infer_local_utc_offset_min(uint32_t utc_epoch, int local_hour, int local_min, int local_sec) noexcept {
  const int utc_day_sec = static_cast<int>(utc_epoch % 86400U);
  const int loc_day_sec = std::clamp(local_hour, 0, 23) * 3600 +
                          std::clamp(local_min, 0, 59) * 60 +
                          std::clamp(local_sec, 0, 59);
  int diff_sec = loc_day_sec - utc_day_sec;
  if (diff_sec <= -43200) diff_sec += 86400;
  if (diff_sec > 50400) diff_sec -= 86400;
  const int rounded_min = (diff_sec >= 0) ? ((diff_sec + 30) / 60) : ((diff_sec - 30) / 60);
  return std::clamp(rounded_min, -720, 840);
}

inline void compute_world_clock_wall_time(uint32_t utc_epoch,
                                          int local_utc_offset_min,
                                          int target_utc_offset_min,
                                          int &out_hour24,
                                          int &out_min,
                                          int &out_day_delta) noexcept {
  auto floor_day = [](int64_t sec) -> int64_t {
    return (sec >= 0) ? (sec / 86400LL) : ((sec - 86399LL) / 86400LL);
  };
  const int64_t local_adj = static_cast<int64_t>(utc_epoch) + static_cast<int64_t>(local_utc_offset_min) * 60LL;
  const int64_t target_adj = static_cast<int64_t>(utc_epoch) + static_cast<int64_t>(target_utc_offset_min) * 60LL;
  const int64_t local_day = floor_day(local_adj);
  const int64_t target_day = floor_day(target_adj);
  const int rem = static_cast<int>(target_adj - target_day * 86400LL);
  out_hour24 = std::clamp(rem / 3600, 0, 23);
  out_min = std::clamp((rem % 3600) / 60, 0, 59);
  out_day_delta = std::clamp(static_cast<int>(target_day - local_day), -2, 2);
}

inline std::string format_alarms_summary(const PersistedDevicePrefs &p = persisted_prefs()) {
  char buf[64];
  std::snprintf(buf, sizeof(buf),
                "%02u:%02u|%u|%u;%02u:%02u|%u|%u;%02u:%02u|%u|%u",
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[0].hour, 23)),
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[0].minute, 59)),
                p.alarms[0].enabled ? 1U : 0U,
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[0].recurrence, 3)),
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[1].hour, 23)),
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[1].minute, 59)),
                p.alarms[1].enabled ? 1U : 0U,
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[1].recurrence, 3)),
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[2].hour, 23)),
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[2].minute, 59)),
                p.alarms[2].enabled ? 1U : 0U,
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[2].recurrence, 3)));
  return std::string(buf);
}

inline bool parse_alarms_summary(std::string_view sv, PersistedAlarmPref (&out)[3]) noexcept {
  std::string_view recs[4];
  if (v3_split_fields(sv, recs, 4, ';') != 3) return false;
  for (int i = 0; i < 3; ++i) {
    std::string_view cols[4];
    if (v3_split_fields(recs[i], cols, 4, '|') != 3) return false;
    const size_t colon = cols[0].find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= cols[0].size()) return false;
    const int hh = v3_parse_int(cols[0].substr(0, colon), -1);
    const int mm = v3_parse_int(cols[0].substr(colon + 1), -1);
    const int en = v3_parse_int(cols[1], -1);
    const int rec = v3_parse_int(cols[2], -1);
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || (en != 0 && en != 1) || rec < 0 || rec > 3) {
      return false;
    }
    out[i].hour = static_cast<uint8_t>(hh);
    out[i].minute = static_cast<uint8_t>(mm);
    out[i].enabled = (en == 1);
    out[i].recurrence = static_cast<uint8_t>(rec);
  }
  return true;
}


inline constexpr bool alarm_editor_blink_dim(uint32_t now_ms, uint32_t last_input_ms) noexcept {
  const uint32_t elapsed = (now_ms >= last_input_ms) ? (now_ms - last_input_ms) : 0U;
  if (elapsed < 500U) return false;
  return (((elapsed - 500U) / 500U) % 2U) == 0U;
}


}  // namespace latter_core
