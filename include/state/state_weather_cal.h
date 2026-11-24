#pragma once

// ==============================================================================
// Latter Core SDK: Weather Forecast, Civil Date Math & PSRAM Calendar Store
// (include/state/state_weather_cal.h)
// ==============================================================================

#include "state/state_common.h"

#if defined(ESP_PLATFORM) && !defined(LATTER_HOST_TEST) && __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#ifndef LATTER_HAS_SPIRAM_ALLOC
#define LATTER_HAS_SPIRAM_ALLOC 1
#endif
#else
#ifndef LATTER_HAS_SPIRAM_ALLOC
#define LATTER_HAS_SPIRAM_ALLOC 0
#endif
#endif

namespace latter_core {

struct WeatherState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string condition = "";
  float temperature = NAN;
  float humidity = NAN;
  float wind_speed = NAN;
  float wind_bearing = NAN;
  float high = NAN;
  float low = NAN;
  std::string forecast_5d = "";
  std::string sun_state = "";
  float sun_elevation = NAN;
  float sun_azimuth = NAN;
  std::string sun_next_rising = "";
  std::string sun_next_setting = "";
  std::string moon_phase = "";
  std::string temp_unit = "F";
};

// Protocol v3 calendar_agenda state.
struct CalendarState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string summary = "";
  int count = 0;
  std::string calendars_meta = "";
  std::string locations_catalog = "";
};


inline WeatherState &weather_state() {
  static WeatherState st;
  return st;
}

inline CalendarState &calendar_state() {
  static CalendarState st;
  return st;
}

struct CalendarUiState {
  int subpage = CalendarSubpage::OVERVIEW;
  int drilldown = CalDrilldown::NONE;
  int return_subpage = CalendarSubpage::OVERVIEW;
  int picker_return_drilldown = CalDrilldown::ADD_STEP1;
  int32_t selected_ymd = 0;  // YYYYMMDD (0 = seed from current local date)
  int overview_page = 0;
  int day_page = 0;
  int week_page = 0;
  int picker_page = 0;
  int selected_event_idx = -1;
  bool delete_confirm_open = false;
  uint8_t dictation_field = 0;  // 0 = none, 1 = Title, 2 = Location
  uint32_t dictation_started_ms = 0;
  uint32_t next_req_id = 1;
  std::string feedback_text = "";
  uint32_t feedback_ms = 0;

  // + Add Event composer draft state
  std::string draft_title = "Reminder";
  std::string draft_location = "";
  uint8_t draft_cal_idx = 0;
  uint8_t draft_rrule_idx = 0;   // 0=Once, 1=Daily, 2=Weekdays, 3=Weekly, 4=Biweekly, 5=Monthly, 6=Yearly
  int32_t draft_ymd = 0;         // YYYYMMDD
  int16_t draft_start_min = 540; // 09:00
  int16_t draft_end_min = 600;   // 10:00
  bool draft_all_day = false;
  uint8_t draft_time_field = 0;  // 0 = Start focused, 1 = End focused
};


inline constexpr const char *normalize_weather_temp_unit(std::string_view sv) noexcept {
  while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
  while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
  if (sv == "C" || sv == "c" || sv == "\xC2\xB0" "C" || sv == "\xC2\xB0" "c" ||
      sv == "celsius" || sv == "CELSIUS") {
    return "C";
  }
  return "F";
}


bool parse_v3_weather_payload(std::string_view payload, WeatherState &st = weather_state());
bool parse_v3_calendar_payload(std::string_view payload, CalendarState &st = calendar_state());

struct CalendarRecord {
  std::string_view time;
  std::string_view title;
  std::string_view location;
  bool all_day = false;
};

inline size_t parse_calendar_records(std::string_view summary, CalendarRecord *out, size_t max_records) {
  if (summary.empty() || out == nullptr || max_records == 0) return 0;
  constexpr size_t kMaxRecords = 8;
  std::string_view recs[kMaxRecords];
  const size_t n = v3_split_fields(summary, recs, max_records < kMaxRecords ? max_records : kMaxRecords,
                                   kProtocolRecordSep);
  size_t count = 0;
  for (size_t i = 0; i < n; i++) {
    std::string_view f[4];
    if (v3_split_fields(recs[i], f, 4, '|') != 4) continue;
    out[count].time = f[0];
    out[count].title = f[1];
    out[count].location = f[2];
    out[count].all_day = (f[3] == "1");
    count++;
  }
  return count;
}

// ==============================================================================
// Calendar App: Civil Date Math, PSRAM Window Store, Metadata & STT Dictation
// ==============================================================================
inline constexpr bool is_leap_year(int y) noexcept {
  return (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0));
}

inline constexpr int days_in_month(int y, int m) noexcept {
  switch (m) {
    case 1: case 3: case 5: case 7: case 8: case 10: case 12: return 31;
    case 4: case 6: case 9: case 11: return 30;
    case 2: return is_leap_year(y) ? 29 : 28;
    default: return 30;
  }
}

inline constexpr int32_t make_ymd(int y, int m, int d) noexcept {
  return static_cast<int32_t>(y) * 10000 + static_cast<int32_t>(m) * 100 + static_cast<int32_t>(d);
}

inline constexpr bool split_ymd(int32_t ymd, int &y, int &m, int &d) noexcept {
  if (ymd < 19700101 || ymd > 21991231) return false;
  y = static_cast<int>(ymd / 10000);
  m = static_cast<int>((ymd / 100) % 100);
  d = static_cast<int>(ymd % 100);
  if (m < 1 || m > 12) return false;
  if (d < 1 || d > days_in_month(y, m)) return false;
  return true;
}

// Howard Hinnant civil-from-days / days-from-civil (1970-01-01 == 0, TZ-free).
inline constexpr int32_t days_from_civil(int y, int m, int d) noexcept {
  y -= (m <= 2) ? 1 : 0;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153U * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2U) / 5U +
                       static_cast<unsigned>(d) - 1U;
  const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

inline constexpr void civil_from_days(int32_t z, int &y, int &m, int &d) noexcept {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
  y = static_cast<int>(yoe) + era * 400;
  const unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
  const unsigned mp = (5U * doy + 2U) / 153U;
  d = static_cast<int>(doy - (153U * mp + 2U) / 5U + 1U);
  m = static_cast<int>(mp < 10U ? mp + 3U : mp - 9U);
  y += (m <= 2) ? 1 : 0;
}

// Returns 0=Sunday .. 6=Saturday.
inline constexpr int weekday_sun0_from_ymd(int32_t ymd) noexcept {
  int y = 0, m = 0, d = 0;
  if (!split_ymd(ymd, y, m, d)) return 0;
  const int32_t z = days_from_civil(y, m, d);
  return static_cast<int>(z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6);
}

inline constexpr int32_t add_days_ymd(int32_t ymd, int delta_days) noexcept {
  int y = 0, m = 0, d = 0;
  if (!split_ymd(ymd, y, m, d)) return ymd;
  const int32_t z = days_from_civil(y, m, d) + static_cast<int32_t>(delta_days);
  int ny = 0, nm = 0, nd = 0;
  civil_from_days(z, ny, nm, nd);
  return make_ymd(ny, nm, nd);
}

inline constexpr int32_t add_months_ymd(int32_t ymd, int delta_months) noexcept {
  int y = 0, m = 0, d = 0;
  if (!split_ymd(ymd, y, m, d)) return ymd;
  const int total_m = (y * 12 + (m - 1)) + delta_months;
  int ny = total_m / 12;
  int nm = (total_m % 12) + 1;
  if (nm <= 0) {
    nm += 12;
    ny -= 1;
  }
  ny = std::clamp(ny, 1970, 2199);
  const int nd = std::min(d, days_in_month(ny, nm));
  return make_ymd(ny, nm, nd);
}

inline constexpr int32_t start_of_week_ymd(int32_t ymd, bool monday_first) noexcept {
  const int wd = weekday_sun0_from_ymd(ymd);
  const int back = monday_first ? ((wd + 6) % 7) : wd;
  return add_days_ymd(ymd, -back);
}

inline constexpr int days_between_ymd(int32_t start_ymd, int32_t end_ymd) noexcept {
  int sy = 0, sm = 0, sd = 0;
  int ey = 0, em = 0, ed = 0;
  if (!split_ymd(start_ymd, sy, sm, sd) || !split_ymd(end_ymd, ey, em, ed)) return 0;
  return static_cast<int>(days_from_civil(ey, em, ed) - days_from_civil(sy, sm, sd));
}

inline void format_ymd_iso(int32_t ymd, char (&out)[12]) noexcept {
  int y = 2026, m = 1, d = 1;
  (void)split_ymd(ymd, y, m, d);
  std::snprintf(out, sizeof(out), "%04d-%02d-%02d", y, m, d);
}

inline int32_t parse_ymd_iso(std::string_view sv) noexcept {
  sv = trim_ascii_ws(sv);
  if (sv.size() == 8) {
    const int parsed = v3_parse_int(sv, 0);
    int y = 0, m = 0, d = 0;
    return split_ymd(parsed, y, m, d) ? parsed : 0;
  }
  if (sv.size() >= 10 && sv[4] == '-' && sv[7] == '-') {
    const int y = v3_parse_int(sv.substr(0, 4), 0);
    const int m = v3_parse_int(sv.substr(5, 2), 0);
    const int d = v3_parse_int(sv.substr(8, 2), 0);
    const int32_t ymd = make_ymd(y, m, d);
    int cy = 0, cm = 0, cd = 0;
    return split_ymd(ymd, cy, cm, cd) ? ymd : 0;
  }
  return 0;
}

inline constexpr int kMaxCalendarSources = 8;
inline constexpr int kMaxCalendarLocations = 12;
inline constexpr int kMaxCalendarWindowEvents = 64;
inline constexpr int kMaxCalendarSummaryPreviewEvents = 5;

inline constexpr const char *const kCalTitlePresets[10] = {
    "Reminder", "Meeting", "Appointment", "Errand", "Dinner",
    "Workout", "Call", "Travel", "Family", "Service"};

inline constexpr const char *const kCalRepeatLabels[7] = {
    "Once", "Daily", "Weekdays", "Weekly", "Biweekly", "Monthly", "Yearly"};

inline constexpr const char *const kCalRepeatRrules[7] = {
    "",
    "FREQ=DAILY",
    "FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR",
    "FREQ=WEEKLY",
    "FREQ=WEEKLY;INTERVAL=2",
    "FREQ=MONTHLY",
    "FREQ=YEARLY"};

struct CalendarSourceMeta {
  char entity_id[64]{};
  char label[32]{};
  uint8_t color_idx = 0;
  bool can_create = false;
  bool can_delete = false;
};


int parse_calendar_sources_meta(std::string_view raw,
                                CalendarSourceMeta *out,
                                int max_out = kMaxCalendarSources) noexcept;
int collect_writable_calendar_sources(std::string_view raw,
                                      CalendarSourceMeta *out,
                                      int *out_source_indices = nullptr,
                                      int max_out = kMaxCalendarSources) noexcept;
int parse_calendar_locations_catalog(std::string_view raw,
                                     std::string_view *out,
                                     int max_out = kMaxCalendarLocations) noexcept;

struct CalendarWindowEvent {
  int32_t start_ymd = 0;    // YYYYMMDD local start date
  int32_t end_ymd = 0;      // YYYYMMDD inclusive local end date
  int16_t start_min = -1;   // 0..1439 local minutes from midnight (-1 when all_day)
  int16_t end_min = -1;     // 0..1440 local minutes from midnight (-1 when all_day)
  uint8_t all_day = 0;
  uint8_t cal_idx = 0;
  char uid[72]{};
  char recurrence_id[40]{};
  char title[60]{};
  char location[68]{};
  char description[144]{};

  void reset_in_place() noexcept {
    start_ymd = 0;
    end_ymd = 0;
    start_min = -1;
    end_min = -1;
    all_day = 0;
    cal_idx = 0;
    std::memset(uid, 0, sizeof(uid));
    std::memset(recurrence_id, 0, sizeof(recurrence_id));
    std::memset(title, 0, sizeof(title));
    std::memset(location, 0, sizeof(location));
    std::memset(description, 0, sizeof(description));
  }
};

static_assert(std::is_trivially_copyable_v<CalendarWindowEvent> &&
                  std::is_standard_layout_v<CalendarWindowEvent>,
              "CalendarWindowEvent must remain standard-layout and trivially copyable for PSRAM storage.");

struct CalendarWindowStore {
  uint32_t rev = 0;
  int32_t window_start_ymd = 0;
  int32_t window_end_ymd = 0;
  int count = 0;
  bool loading = false;
  CalendarWindowEvent events[kMaxCalendarWindowEvents]{};

  int last_applied_req_id = 0;
  int staging_req_id = -1;
  int staging_expected_chunks = 0;
  int staging_next_chunk = 0;
  int32_t staging_start_ymd = 0;
  int32_t staging_end_ymd = 0;
  int staging_count = 0;
  CalendarWindowEvent staging_events[kMaxCalendarWindowEvents]{};

  CalendarWindowStore(const CalendarWindowStore &) = delete;
  CalendarWindowStore &operator=(const CalendarWindowStore &) = delete;
  CalendarWindowStore(CalendarWindowStore &&) = delete;
  CalendarWindowStore &operator=(CalendarWindowStore &&) = delete;

  void reset_in_place() noexcept {
    rev = 0;
    window_start_ymd = 0;
    window_end_ymd = 0;
    count = 0;
    loading = false;
    for (auto &ev : events) {
      ev.reset_in_place();
    }
    last_applied_req_id = 0;
    staging_req_id = -1;
    staging_expected_chunks = 0;
    staging_next_chunk = 0;
    staging_start_ymd = 0;
    staging_end_ymd = 0;
    staging_count = 0;
    for (auto &ev : staging_events) {
      ev.reset_in_place();
    }
  }

 private:
  CalendarWindowStore() noexcept { reset_in_place(); }
  friend CalendarWindowStore &calendar_window_store() noexcept;
};

static_assert(std::is_standard_layout_v<CalendarWindowStore>,
              "CalendarWindowStore must remain standard-layout for PSRAM calloc.");
static_assert(!std::is_default_constructible_v<CalendarWindowStore> &&
                  !std::is_copy_constructible_v<CalendarWindowStore> &&
                  !std::is_copy_assignable_v<CalendarWindowStore> &&
                  !std::is_move_constructible_v<CalendarWindowStore> &&
                  !std::is_move_assignable_v<CalendarWindowStore>,
              "CalendarWindowStore (~51 KB) must never be constructed on the stack or copied by value.");

inline CalendarWindowStore &calendar_window_store() noexcept {
#if LATTER_HAS_SPIRAM_ALLOC
  static CalendarWindowStore *ptr = nullptr;
  if (ptr == nullptr) {
    void *raw = heap_caps_calloc(1, sizeof(CalendarWindowStore), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (raw == nullptr) {
      raw = heap_caps_calloc(1, sizeof(CalendarWindowStore), MALLOC_CAP_8BIT);
    }
    if (raw != nullptr) {
      ptr = reinterpret_cast<CalendarWindowStore *>(raw);
      ptr->reset_in_place();
    } else {
      ptr = new CalendarWindowStore();
    }
  }
  return *ptr;
#else
  static CalendarWindowStore store;
  return store;
#endif
}

inline void reset_calendar_window_store() noexcept {
  calendar_window_store().reset_in_place();
}

inline CalendarWindowEvent *calendar_summary_preview_scratch() noexcept {
#if LATTER_HAS_SPIRAM_ALLOC
  static CalendarWindowEvent *ptr = nullptr;
  if (ptr == nullptr) {
    ptr = static_cast<CalendarWindowEvent *>(
        heap_caps_calloc(kMaxCalendarSummaryPreviewEvents, sizeof(CalendarWindowEvent),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (ptr == nullptr) {
      ptr = static_cast<CalendarWindowEvent *>(
          heap_caps_calloc(kMaxCalendarSummaryPreviewEvents, sizeof(CalendarWindowEvent), MALLOC_CAP_8BIT));
    }
    if (ptr != nullptr) {
      for (int i = 0; i < kMaxCalendarSummaryPreviewEvents; ++i) {
        ptr[i].reset_in_place();
      }
    } else {
      ptr = new CalendarWindowEvent[kMaxCalendarSummaryPreviewEvents]{};
    }
  }
  return ptr;
#else
  static CalendarWindowEvent buf[kMaxCalendarSummaryPreviewEvents]{};
  return buf;
#endif
}


int populate_calendar_summary_preview(
    std::string_view summary,
    int32_t today_ymd,
    CalendarWindowEvent *out_events = calendar_summary_preview_scratch(),
    int max_out = kMaxCalendarSummaryPreviewEvents) noexcept;
bool parse_calendar_window_event_record(std::string_view rec, CalendarWindowEvent &out) noexcept;

inline constexpr bool calendar_event_overlaps_ymd(const CalendarWindowEvent &ev, int32_t ymd) noexcept {
  return ymd >= ev.start_ymd && ymd <= ev.end_ymd;
}

inline constexpr bool calendar_event_is_upcoming(const CalendarWindowEvent &ev,
                                                 int32_t today_ymd,
                                                 int now_min_of_day,
                                                 int32_t horizon_ymd) noexcept {
  if (ev.start_ymd > horizon_ymd || ev.end_ymd < today_ymd) return false;
  if (ev.end_ymd > today_ymd || ev.all_day != 0U) return true;
  if (ev.end_min >= 0) return ev.end_min > now_min_of_day;
  if (ev.start_min >= 0) return (ev.start_min + 60) > now_min_of_day;
  return true;
}

inline int collect_calendar_day_events(int32_t ymd,
                                       int *out_indices,
                                       int max_out,
                                       const CalendarWindowStore &store = calendar_window_store()) noexcept {
  if (out_indices == nullptr || max_out <= 0) return 0;
  int n = 0;
  // All-day events first, then chronological timed events.
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < store.count && n < max_out; ++i) {
      const auto &ev = store.events[i];
      if (!calendar_event_overlaps_ymd(ev, ymd)) continue;
      const bool is_all_day = (ev.all_day != 0U);
      if ((pass == 0 && !is_all_day) || (pass == 1 && is_all_day)) continue;
      out_indices[n++] = i;
    }
  }
  return n;
}

inline int collect_calendar_overview_events(int32_t today_ymd,
                                            int now_min_of_day,
                                            int lookahead_days,
                                            int *out_indices,
                                            int max_out,
                                            const CalendarWindowStore &store = calendar_window_store()) noexcept {
  if (out_indices == nullptr || max_out <= 0) return 0;
  const int clamped_days = std::max(1, lookahead_days);
  const int32_t horizon_ymd = add_days_ymd(today_ymd, clamped_days);
  int n = 0;
  for (int i = 0; i < store.count && n < max_out; ++i) {
    if (calendar_event_is_upcoming(store.events[i], today_ymd, now_min_of_day, horizon_ymd)) {
      out_indices[n++] = i;
    }
  }
  return n;
}

// Collect up to 3 distinct source calendar indices (`cal_idx`) active on `ymd` for Month/Week dots.
inline int collect_calendar_day_dots(int32_t ymd,
                                     uint8_t (&out_cal_indices)[3],
                                     const CalendarWindowStore &store = calendar_window_store()) noexcept {
  int cnt = 0;
  for (int i = 0; i < store.count && cnt < 3; ++i) {
    const auto &ev = store.events[i];
    if (!calendar_event_overlaps_ymd(ev, ymd)) continue;
    bool seen = false;
    for (int j = 0; j < cnt; ++j) {
      if (out_cal_indices[j] == ev.cal_idx) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      out_cal_indices[cnt++] = ev.cal_idx;
    }
  }
  return cnt;
}


}  // namespace latter_core
