#pragma once

// ==============================================================================
// Latter Core: Area Context, Home Hub, Remotes & Perimeter Policy PODs
// (include/state/state_areas.h)
// ==============================================================================

#include "state/state_common.h"

namespace latter_core {

inline constexpr uint16_t kAlarmCapCodeDisarm  = 0x01U;
inline constexpr uint16_t kAlarmCapCodeArm     = 0x02U;
inline constexpr uint16_t kAlarmCapArmHome     = 0x04U;
inline constexpr uint16_t kAlarmCapArmAway     = 0x08U;
inline constexpr uint16_t kAlarmCapArmNight    = 0x10U;
inline constexpr uint16_t kAlarmCapArmVacation = 0x20U;
inline constexpr uint16_t kAlarmCapDefault     = kAlarmCapArmHome | kAlarmCapArmAway;

inline constexpr uint16_t kClimateCapOff        = 0x01U;
inline constexpr uint16_t kClimateCapHeat       = 0x02U;
inline constexpr uint16_t kClimateCapCool       = 0x04U;
inline constexpr uint16_t kClimateCapAuto       = 0x08U;
inline constexpr uint16_t kClimateCapFan        = 0x10U;
inline constexpr uint16_t kClimateCapDry        = 0x20U;
inline constexpr uint16_t kClimateCapTargetTemp = 0x40U;
inline constexpr uint16_t kClimateCapCelsius    = 0x80U;
inline constexpr uint16_t kClimateCapDefault    =
    kClimateCapOff | kClimateCapHeat | kClimateCapCool | kClimateCapAuto | kClimateCapTargetTemp;

inline constexpr uint16_t kVacuumCapStart      = 0x01U;
inline constexpr uint16_t kVacuumCapPause      = 0x02U;
inline constexpr uint16_t kVacuumCapStop       = 0x04U;
inline constexpr uint16_t kVacuumCapReturnHome = 0x08U;
inline constexpr uint16_t kVacuumCapCleanSpot  = 0x10U;
inline constexpr uint16_t kVacuumCapLocate     = 0x20U;
inline constexpr uint16_t kVacuumCapDefault    =
    kVacuumCapStart | kVacuumCapPause | kVacuumCapReturnHome;


inline constexpr int kMaxThermostatZones  = 8;
inline constexpr int kMaxEntryLockItems   = 8;
inline constexpr int kMaxEntryGarageItems = 6;
inline constexpr int kMaxEntryItems       = kMaxEntryLockItems + kMaxEntryGarageItems;
inline constexpr int kMaxVacuumUnits      = 6;
inline constexpr int kMaxAreaClimates     = 6;

struct ThermostatZoneItem {
  std::string entity_id = "";
  std::string name = "";
  std::string area_id = "";
  std::string mode = "";
  int target_temp = -1;
  float current_temp = NAN;
  uint16_t caps = 0;
};

struct EntryControlItem {
  std::string entity_id = "";
  std::string name = "";
  std::string area_id = "";
  bool is_lock = true;
  LockState lock_state = LockState::UNKNOWN;
  GarageState garage_state = GarageState::UNKNOWN;
};

struct VacuumUnitItem {
  std::string entity_id = "";
  std::string name = "";
  std::string area_id = "";
  std::string state = "";
  uint16_t caps = 0;
};

struct HomeHubState {
  int tab = 0;
  int mode_bar_page = 0;
  int thermostat_idx = 0;
  int thermostat_count = 0;
  ThermostatZoneItem thermostats[kMaxThermostatZones]{};
  int entry_page = 0;
  int entry_count = 0;
  int broadcast_page = 0;
  EntryControlItem entries[kMaxEntryItems]{};
  int vacuum_idx = 0;
  int vacuum_count = 0;
  VacuumUnitItem vacuums[kMaxVacuumUnits]{};
  int area_climate_count = 0;
  ThermostatZoneItem area_climates[kMaxAreaClimates]{};
  uint32_t local_mutation_seq = 0;
};

// Protocol v3 area_context state.
struct AreaContextState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string area_id = "";
  std::string area_name = "";
  std::string site_id = "";
  std::string ble_closest_proxy = "";
  std::string wallpaper_url = "";
  std::string available_domains = "";
  float area_temp = NAN;
  float area_humidity = NAN;
  int lights_on = 0;
  int covers_open = 0;
  std::string alarm_entity = "";
  std::string alarm_state = "";
  uint16_t alarm_caps = 0;
  std::string thermostats_summary = "";
  std::string locks_summary = "";
  std::string garages_summary = "";
  std::string vacuums_summary = "";
  std::string area_climates_summary = "";
  std::string camera_snapshot_url = "";
  std::string camera_stream_url = "";
  std::string cameras_summary = "";
  std::string doorbell_talk_state = "";
  std::string broadcast_phrases = "";
  int alert_id = -1;
  std::string alert_title = "";
  std::string alert_message = "";
  std::string alert_priority = "normal";
  AlertPriority alert_priority_level = AlertPriority::Normal;
  int alert_duration = 10;
  bool alert_chime = true;
  std::string alert_camera_url = "";
  std::string alert_camera_entity = "";
  std::string activity_card = "";        // "1" / "0"; empty = not set -> kDefaultActivityCard
  std::string activity_sources = "";     // csv of source keys; empty = not set -> kDefaultActivitySources
  std::string security_pin_policy = "";  // "allow" | "require_pin" | "block"
  std::string home_categories = "";      // ordered available Home Hub category tokens ("alarm,intercom,thermostat,entry,vacuum")
  std::string area_icon = "";            // Home Assistant AreaEntry icon (e.g. "mdi:sofa") or inferred archetype icon
  std::string local_tz_info = "";        // "<iana>|<city>|<abbr>|<utc_offset_min>|<is_dst_0_or_1>"
  std::string world_clocks_summary = ""; // ";"-delimited "<iana>|<city>|<abbr>|<utc_offset_min>|<is_dst_0_or_1>"
  std::string screensaver_photo_url = "";
  std::string area_ambient_light = "";   // "<norm_pct_0_100>|<formatted_lux>" (empty when unavailable)
  int8_t area_ambient_pct = -1;          // 0..100 parsed calibrated ambient light; -1 when unavailable
  std::string area_lux_display = "";     // formatted lux string (e.g. "5.6") for UI readouts
};


struct RemoteState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  int active_slot = 0;
  int configured_count = 0;
  std::string slots_summary = "";
};


struct AreaRuntimeState {
  std::string browse_mode = "entities";
  std::string domain_filter = "scene";
  std::string active_area_id = "";
  std::string active_area_name = "Area Control";
  std::string area_label = "Home";
  std::string site_id = "primary";
  std::string ble_proxy = "";
  std::string available_domains_csv = "";
  int cat_dock_page = 0;
  std::string alarm_entity = "";
  std::string alarm_state = "";
  std::string alarm_service = "alarm_control_panel.alarm_disarm";
  std::string target_entity_id = "";
  std::string target_action = "toggle";
  int target_level = -1;
  std::string target_code = "";
  std::string action_feedback = "";
  uint32_t feedback_timer = 0;
  std::string light_insp_entity_id = "";
  std::string light_insp_name = "Light";
  std::string light_insp_domain = "light";
  int light_insp_mode = 0;
  int light_insp_slot = -1;
  bool light_insp_active = true;
  float light_insp_hue_deg = 35.0f;
  float light_insp_sat = 0.85f;
  int light_insp_kelvin = 3500;
  int light_insp_bri_pct = 80;
  std::string color_hex_param = "#FFAA55";
};

struct RemoteRuntimeState {
  static constexpr int kMaxSlots = 10;
  int active_slot = 0;
  int control_page = 0;
  std::string slot_ents[kMaxSlots]{};
  std::string slot_names[kMaxSlots]{};
  bool slot_states_on[kMaxSlots] = {false};
  std::string action_feedback = "";
  uint32_t feedback_timer = 0;
};


inline constexpr int MAX_REMOTE_SLOTS = RemoteRuntimeState::kMaxSlots;

inline HomeHubState &home_hub_state() {
  static HomeHubState st;
  return st;
}

inline AreaContextState &area_ctx_state() {
  static AreaContextState st;
  return st;
}

inline RemoteState &remote_state() {
  static RemoteState st;
  return st;
}

inline const char *resolve_area_dock_icon(const AreaContextState &ac) noexcept {
  const bool assigned =
      !ac.area_id.empty() ||
      (!ac.area_name.empty() && ac.area_name != "Unassigned" && ac.area_name != kUnknownGlyph);
  if (assigned && !ac.area_icon.empty()) {
    return ac.area_icon.c_str();
  }
  return "mdi:sofa";
}


inline constexpr const char *const kCanonicalAreaCategoryTokens[8] = {
    "scene", "light", "cover", "fan", "switch", "climate", "media", "all"};

inline bool is_canonical_area_category_token(std::string_view sv) {
  for (const char *tok : kCanonicalAreaCategoryTokens) {
    if (sv == tok) return true;
  }
  return false;
}

inline int parse_area_categories_csv(std::string_view csv, std::string *out_cats, int max_cats = 8) {
  if (csv.empty() || out_cats == nullptr || max_cats <= 0) return 0;
  int n = 0;
  size_t start = 0;
  while (start <= csv.size() && n < max_cats) {
    size_t pos = csv.find(',', start);
    std::string_view part =
        (pos == std::string_view::npos) ? csv.substr(start) : csv.substr(start, pos - start);
    while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) part.remove_prefix(1);
    while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.remove_suffix(1);
    if (!part.empty() && is_canonical_area_category_token(part)) {
      bool dup = false;
      for (int i = 0; i < n; ++i) {
        if (out_cats[i] == part) {
          dup = true;
          break;
        }
      }
      if (!dup) out_cats[n++] = std::string(part);
    }
    if (pos == std::string_view::npos) break;
    start = pos + 1;
  }
  return n;
}

inline int home_category_tab_for_token(std::string_view sv) noexcept {
  if (sv == "alarm") return HomeHubTab::ALARM;
  if (sv == "intercom") return HomeHubTab::INTERCOM;
  if (sv == "thermostat") return HomeHubTab::THERMOSTAT;
  if (sv == "entry") return HomeHubTab::ENTRY;
  if (sv == "vacuum") return HomeHubTab::VACUUM;
  return -1;
}

inline int parse_home_categories_csv(std::string_view csv, int *out_tabs, int max_tabs = kMaxHomeHubTabs) {
  if (csv.empty() || out_tabs == nullptr || max_tabs <= 0) return 0;
  int n = 0;
  size_t start = 0;
  while (start <= csv.size() && n < max_tabs) {
    size_t pos = csv.find(',', start);
    std::string_view part =
        (pos == std::string_view::npos) ? csv.substr(start) : csv.substr(start, pos - start);
    while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) part.remove_prefix(1);
    while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.remove_suffix(1);
    const int tab_id = home_category_tab_for_token(part);
    if (tab_id >= 0) {
      bool dup = false;
      for (int i = 0; i < n; ++i) {
        if (out_tabs[i] == tab_id) {
          dup = true;
          break;
        }
      }
      if (!dup) out_tabs[n++] = tab_id;
    }
    if (pos == std::string_view::npos) break;
    start = pos + 1;
  }
  return n;
}


inline LockState parse_lock_state_str(std::string_view sv) {
  if (sv == "locked") return LockState::LOCKED;
  if (sv == "unlocked" || sv == "unlocking" || sv == "open") return LockState::UNLOCKED;
  return LockState::UNKNOWN;
}

inline GarageState parse_garage_state_str(std::string_view sv) {
  if (sv == "open" || sv == "opening") return GarageState::OPEN;
  if (sv == "closed" || sv == "closing") return GarageState::CLOSED;
  return GarageState::UNKNOWN;
}

inline std::string normalize_vacuum_state_str(std::string_view sv) {
  if (sv.empty()) return "";
  if (sv == "cleaning" || sv == "Cleaning") return "Cleaning";
  if (sv == "paused" || sv == "Paused") return "Paused";
  if (sv == "idle" || sv == "Idle") return "Idle";
  if (sv == "returning" || sv == "Returning") return "Returning";
  if (sv == "docked" || sv == "Docked") return "Docked";
  if (sv == "error" || sv == "Error") return "Error";
  return std::string(sv);
}

inline std::string fallback_entity_label_sv(std::string_view ent_id) {
  const size_t dot = ent_id.find('.');
  std::string s(dot == std::string_view::npos ? ent_id : ent_id.substr(dot + 1U));
  bool cap = true;
  for (char &ch : s) {
    if (ch == '_' || ch == '-') {
      ch = ' ';
      cap = true;
    } else if (cap) {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      cap = false;
    }
  }
  return s;
}


int parse_thermostats_summary_records(std::string_view raw,
                                      ThermostatZoneItem *out_items,
                                      int max_items = kMaxThermostatZones,
                                      bool has_area_col = true);
int parse_entry_controls_records(std::string_view locks_raw,
                                 std::string_view garages_raw,
                                 EntryControlItem out_items[kMaxEntryItems]);
int parse_vacuums_summary_records(std::string_view raw,
                                  VacuumUnitItem out_items[kMaxVacuumUnits]);

inline void sync_home_hub_for_active_area(std::string_view active_area_id,
                                          HomeHubState &hub = home_hub_state()) noexcept {
  if (active_area_id.empty()) return;
  for (int i = 0; i < hub.thermostat_count; ++i) {
    if (hub.thermostats[i].area_id == active_area_id) {
      hub.thermostat_idx = i;
      break;
    }
  }
  for (int i = 0; i < hub.vacuum_count; ++i) {
    if (hub.vacuums[i].area_id == active_area_id) {
      hub.vacuum_idx = i;
      break;
    }
  }
}

inline ThermostatZoneItem *find_climate_item_by_id(std::string_view entity_id,
                                                   HomeHubState &hub = home_hub_state()) noexcept {
  if (entity_id.empty()) return nullptr;
  for (int i = 0; i < hub.area_climate_count; ++i) {
    if (hub.area_climates[i].entity_id == entity_id) return &hub.area_climates[i];
  }
  for (int i = 0; i < hub.thermostat_count; ++i) {
    if (hub.thermostats[i].entity_id == entity_id) return &hub.thermostats[i];
  }
  return nullptr;
}

inline const ThermostatZoneItem *find_climate_item_by_id_const(
    std::string_view entity_id,
    const HomeHubState &hub = home_hub_state()) noexcept {
  if (entity_id.empty()) return nullptr;
  for (int i = 0; i < hub.area_climate_count; ++i) {
    if (hub.area_climates[i].entity_id == entity_id) return &hub.area_climates[i];
  }
  for (int i = 0; i < hub.thermostat_count; ++i) {
    if (hub.thermostats[i].entity_id == entity_id) return &hub.thermostats[i];
  }
  return nullptr;
}


bool parse_v3_area_ctx_payload(std::string_view payload,
                               AreaContextState &st = area_ctx_state(),
                               HomeHubState &hub = home_hub_state());
bool parse_v3_remote_payload(std::string_view payload, RemoteState &st = remote_state());

inline uint16_t effective_alarm_caps(const AreaContextState &ac = area_ctx_state()) noexcept {
  if (ac.alarm_entity.empty()) return 0U;
  return (ac.alarm_caps != 0U) ? ac.alarm_caps : kAlarmCapDefault;
}

inline uint16_t effective_climate_caps(const HomeHubState &hub = home_hub_state()) noexcept {
  if (hub.thermostat_count <= 0) return 0U;
  const int idx = std::clamp(hub.thermostat_idx, 0, hub.thermostat_count - 1);
  const uint16_t caps = hub.thermostats[idx].caps;
  return (caps != 0U) ? caps : kClimateCapDefault;
}

inline uint16_t effective_vacuum_caps(const HomeHubState &hub = home_hub_state()) noexcept {
  if (hub.vacuum_count <= 0) return 0U;
  const int idx = std::clamp(hub.vacuum_idx, 0, hub.vacuum_count - 1);
  const uint16_t caps = hub.vacuums[idx].caps;
  return (caps != 0U) ? caps : kVacuumCapDefault;
}


struct HomeHubActionSpec {
  int code{0};
  const char *label{""};
  const char *short_label{""};
};

struct ThermostatModeSpec {
  int code{0};
  const char *mode_id{""};
  const char *label{""};
  bool active{false};
};

inline constexpr int resolve_alarm_actions(std::string_view alarm_state, uint16_t eff_alarm_caps,
                                           HomeHubActionSpec out_specs[4]) noexcept {
  if (out_specs == nullptr || eff_alarm_caps == 0U) return 0;
  if (!alarm_state.empty() && !eq_ascii_ci_sv(alarm_state, "disarmed")) {
    out_specs[0] = HomeHubActionSpec{0, "Disarm", "Disarm"};
    return 1;
  }
  uint16_t caps = eff_alarm_caps;
  if ((caps & 0x3CU) == 0U) caps |= kAlarmCapDefault;
  int cnt = 0;
  if ((caps & kAlarmCapArmHome) != 0U) {
    out_specs[cnt++] = HomeHubActionSpec{1, "Arm Home", "Home"};
  }
  if ((caps & kAlarmCapArmAway) != 0U) {
    out_specs[cnt++] = HomeHubActionSpec{2, "Arm Away", "Away"};
  }
  if ((caps & kAlarmCapArmNight) != 0U) {
    out_specs[cnt++] = HomeHubActionSpec{3, "Night", "Night"};
  }
  if ((caps & kAlarmCapArmVacation) != 0U) {
    out_specs[cnt++] = HomeHubActionSpec{4, "Vacation", "Vacay"};
  }
  return cnt;
}

inline constexpr int resolve_vacuum_actions(std::string_view vacuum_state, uint16_t eff_vacuum_caps,
                                            HomeHubActionSpec out_specs[4]) noexcept {
  if (out_specs == nullptr || eff_vacuum_caps == 0U) return 0;
  const uint16_t caps = eff_vacuum_caps;
  int cnt = 0;

  if (vacuum_state.empty() || eq_ascii_ci_sv(vacuum_state, "docked")) {
    if ((caps & kVacuumCapStart) != 0U) out_specs[cnt++] = HomeHubActionSpec{0, "Clean", "Clean"};
    if ((caps & kVacuumCapCleanSpot) != 0U) out_specs[cnt++] = HomeHubActionSpec{4, "Spot", "Spot"};
    if ((caps & kVacuumCapLocate) != 0U) out_specs[cnt++] = HomeHubActionSpec{5, "Locate", "Locate"};
  } else if (eq_ascii_ci_sv(vacuum_state, "cleaning")) {
    if ((caps & kVacuumCapPause) != 0U) out_specs[cnt++] = HomeHubActionSpec{1, "Pause", "Pause"};
    else if ((caps & kVacuumCapStop) != 0U) out_specs[cnt++] = HomeHubActionSpec{3, "Stop", "Stop"};
    if ((caps & kVacuumCapReturnHome) != 0U) out_specs[cnt++] = HomeHubActionSpec{2, "Dock", "Dock"};
    if ((caps & kVacuumCapLocate) != 0U) out_specs[cnt++] = HomeHubActionSpec{5, "Locate", "Locate"};
  } else if (eq_ascii_ci_sv(vacuum_state, "paused") || eq_ascii_ci_sv(vacuum_state, "idle")) {
    if ((caps & kVacuumCapStart) != 0U) out_specs[cnt++] = HomeHubActionSpec{0, "Resume", "Resume"};
    if ((caps & kVacuumCapReturnHome) != 0U) out_specs[cnt++] = HomeHubActionSpec{2, "Dock", "Dock"};
    else if ((caps & kVacuumCapStop) != 0U) out_specs[cnt++] = HomeHubActionSpec{3, "Stop", "Stop"};
    if ((caps & kVacuumCapLocate) != 0U) out_specs[cnt++] = HomeHubActionSpec{5, "Locate", "Locate"};
  } else if (eq_ascii_ci_sv(vacuum_state, "returning")) {
    if ((caps & kVacuumCapPause) != 0U) out_specs[cnt++] = HomeHubActionSpec{1, "Pause", "Pause"};
    else if ((caps & kVacuumCapStop) != 0U) out_specs[cnt++] = HomeHubActionSpec{3, "Stop", "Stop"};
    if ((caps & kVacuumCapStart) != 0U) out_specs[cnt++] = HomeHubActionSpec{0, "Clean", "Clean"};
  } else if (eq_ascii_ci_sv(vacuum_state, "error")) {
    if ((caps & kVacuumCapLocate) != 0U) out_specs[cnt++] = HomeHubActionSpec{5, "Locate", "Locate"};
    if ((caps & kVacuumCapStart) != 0U) out_specs[cnt++] = HomeHubActionSpec{0, "Resume", "Resume"};
    if ((caps & kVacuumCapReturnHome) != 0U) out_specs[cnt++] = HomeHubActionSpec{2, "Dock", "Dock"};
  } else {
    if ((caps & kVacuumCapStart) != 0U) out_specs[cnt++] = HomeHubActionSpec{0, "Clean", "Clean"};
    if ((caps & kVacuumCapReturnHome) != 0U) out_specs[cnt++] = HomeHubActionSpec{2, "Dock", "Dock"};
  }
  return cnt;
}

inline constexpr bool thermostat_shows_setpoint_buttons(std::string_view cur_mode,
                                                        uint16_t eff_climate_caps) noexcept {
  if (eff_climate_caps == 0U) return false;
  return ((eff_climate_caps & kClimateCapTargetTemp) != 0U) && !eq_ascii_ci_sv(cur_mode, "off");
}

inline constexpr int resolve_thermostat_modes(std::string_view cur_mode, uint16_t eff_climate_caps,
                                              ThermostatModeSpec out_specs[6]) noexcept {
  if (out_specs == nullptr || eff_climate_caps == 0U) return 0;
  uint16_t caps = eff_climate_caps;
  if ((caps & 0x3FU) == 0U) caps |= (kClimateCapDefault & 0x3FU);
  int cnt = 0;
  if ((caps & kClimateCapHeat) != 0U) {
    out_specs[cnt++] = ThermostatModeSpec{0, "heat", "Heat", eq_ascii_ci_sv(cur_mode, "heat")};
  }
  if ((caps & kClimateCapCool) != 0U) {
    out_specs[cnt++] = ThermostatModeSpec{1, "cool", "Cool", eq_ascii_ci_sv(cur_mode, "cool")};
  }
  if ((caps & kClimateCapAuto) != 0U) {
    out_specs[cnt++] = ThermostatModeSpec{
        2, "heat_cool", "Auto", eq_ascii_ci_sv(cur_mode, "heat_cool") || eq_ascii_ci_sv(cur_mode, "auto")};
  }
  if ((caps & kClimateCapFan) != 0U) {
    out_specs[cnt++] = ThermostatModeSpec{4, "fan_only", "Fan", eq_ascii_ci_sv(cur_mode, "fan_only")};
  }
  if ((caps & kClimateCapDry) != 0U) {
    out_specs[cnt++] = ThermostatModeSpec{5, "dry", "Dry", eq_ascii_ci_sv(cur_mode, "dry")};
  }
  if ((caps & kClimateCapOff) != 0U) {
    out_specs[cnt++] = ThermostatModeSpec{3, "off", "Off", eq_ascii_ci_sv(cur_mode, "off")};
  }
  return cnt;
}

inline constexpr int step_thermostat_setpoint(int cur_target, int delta,
                                              uint16_t eff_climate_caps) noexcept {
  const bool is_celsius = (eff_climate_caps & kClimateCapCelsius) != 0U;
  const int lo = is_celsius ? 10 : 50;
  const int hi = is_celsius ? 35 : 90;
  const int fallback = is_celsius ? 21 : 70;
  const int min_valid = is_celsius ? 4 : 35;
  const int max_valid = is_celsius ? 45 : 110;
  const int base = (cur_target < min_valid || cur_target > max_valid) ? fallback : cur_target;
  return std::clamp(base + delta, lo, hi);
}

static constexpr uint8_t kActivitySourceAll = 0x3E;

inline uint8_t activity_source_mask(const AreaContextState &st = area_ctx_state()) {
  const std::string_view card = st.activity_card.empty() ? std::string_view(kDefaultActivityCard)
                                                         : std::string_view(st.activity_card);
  if (!v3_parse_bool(card)) return 0;
  const std::string_view csv = st.activity_sources.empty() ? std::string_view(kDefaultActivitySources)
                                                           : std::string_view(st.activity_sources);
  uint8_t mask = 0;
  size_t start = 0;
  while (start <= csv.size()) {
    size_t end = csv.find(',', start);
    if (end == std::string_view::npos) end = csv.size();
    const std::string_view key = csv.substr(start, end - start);
    if (key == "timers") mask |= 1U << 1;
    else if (key == "stopwatch") mask |= 1U << 2;
    else if (key == "music_queue") mask |= 1U << 3;
    else if (key == "alarms") mask |= 1U << 4;
    else if (key == "calendar") mask |= 1U << 5;
    start = end + 1;
  }
  return mask;
}


inline int collect_home_hub_tabs(int *out_tabs, const AreaContextState &ac = area_ctx_state(),
                                 int max_tabs = kMaxHomeHubTabs) {
  if (out_tabs == nullptr || max_tabs <= 0 || ac.epoch == 0U) return 0;
  return parse_home_categories_csv(ac.home_categories, out_tabs, max_tabs);
}


inline constexpr bool is_alarm_entity_target(std::string_view domain_or_entity) noexcept {
  return domain_or_entity == "home_hub.alarm" ||
         domain_or_entity == "alarm_control_panel" ||
         domain_or_entity.rfind("alarm_control_panel.", 0) == 0;
}

inline constexpr bool is_alarm_disarm_action(std::string_view action) noexcept {
  return action == "disarm" ||
         action == "alarm_disarm" ||
         action == "alarm_control_panel.alarm_disarm";
}

inline constexpr bool is_alarm_arm_action(std::string_view action) noexcept {
  return action == "arm_home" ||
         action == "arm_away" ||
         action == "arm_night" ||
         action == "arm_vacation" ||
         action.rfind("alarm_arm_", 0) == 0 ||
         action.rfind("alarm_control_panel.alarm_arm_", 0) == 0;
}

inline bool is_sensitive_perimeter_action_impl(const std::string &domain_or_entity,
                                               const std::string &action, int level = 100) {
  if (is_alarm_disarm_action(action)) {
    return true;
  }
  if (is_alarm_entity_target(domain_or_entity) && is_alarm_arm_action(action)) {
    return (effective_alarm_caps() & kAlarmCapCodeArm) != 0U;
  }
  if (action == "unlock" || action == "open_cover") {
    return true;
  }
  if (domain_or_entity == "lock" && action == "open") {
    return true;
  }
  if (domain_or_entity == "home_hub.garage" && action == "turn_on") {
    return true;
  }
  if (domain_or_entity.rfind("cover.", 0) == 0 &&
      (domain_or_entity.find("garage") != std::string::npos ||
       domain_or_entity.find("gate") != std::string::npos) &&
      (action == "turn_on" || action == "open" || action == "toggle" ||
       (action == "set_level" && level > 0))) {
    return true;
  }
  return false;
}

inline bool evaluate_perimeter_action_blocked(std::string_view policy,
                                              const std::string &domain_or_entity,
                                              const std::string &action,
                                              int level = 100) {
  if (policy != "block") return false;
  if (is_alarm_arm_action(action)) return false;
  return is_sensitive_perimeter_action_impl(domain_or_entity, action, level);
}

inline bool evaluate_perimeter_pin_prompt(std::string_view policy,
                                          const std::string &domain_or_entity,
                                          const std::string &action,
                                          int level,
                                          uint16_t alarm_caps,
                                          bool is_stationary) {
  if (is_alarm_entity_target(domain_or_entity) ||
      is_alarm_disarm_action(action) ||
      is_alarm_arm_action(action)) {
    if (is_alarm_disarm_action(action)) {
      if (policy == "block") return false;
      return ((alarm_caps & kAlarmCapCodeDisarm) != 0U) || (policy == "require_pin");
    }
    if (is_alarm_arm_action(action)) {
      return (alarm_caps & kAlarmCapCodeArm) != 0U;
    }
    return false;
  }
  if (!is_sensitive_perimeter_action_impl(domain_or_entity, action, level)) return false;
  if (policy == "allow" || policy == "block") return false;
  if (policy == "require_pin") return true;
  return is_stationary;
}

}  // namespace latter_core
