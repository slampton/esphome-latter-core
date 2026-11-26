#include "state/device_runtime_state.h"
#include "state/state_areas.h"
#include "state/state_common.h"
#include "state/state_media.h"
#include "state/state_prefs.h"
#include "state/state_shortcuts.h"
#include "state/state_weather_cal.h"

#include <cstddef>

namespace latter_core {

// GCC/Clang on ESP-IDF support init_priority(200) to construct the 8
// std::string/vector-bearing singletons before default-priority (65535)
// global constructors. ESP-IDF calls heap_caps_init() before do_global_ctors()
// and preserves SORT_BY_INIT_PRIORITY(.init_array.*) in linker scripts.
#if defined(__GNUC__)
#define LATTER_CORE_INIT_EARLY __attribute__((init_priority(200)))
#else
#define LATTER_CORE_INIT_EARLY
#endif

// ---------------------------------------------------------------------------
// 5 Pure-POD domain globals: zero-initialized in .bss via constinit
// ---------------------------------------------------------------------------
constinit LinkState g_link_state{};
constinit uint32_t g_v3_mismatch_warn_count{0};
constinit DevicePrefsState g_device_prefs_state{};
constinit PersistedDevicePrefs g_persisted_prefs{};
constinit BedtimeScheduleTracker g_bedtime_schedule_tracker{};

// ---------------------------------------------------------------------------
// 8 Dynamic (std::string / std::vector-bearing) domain globals
// ---------------------------------------------------------------------------
DeviceRuntimeState g_runtime_state LATTER_CORE_INIT_EARLY{};
MediaState g_media_state LATTER_CORE_INIT_EARLY{};
HomeHubState g_home_hub_state LATTER_CORE_INIT_EARLY{};
AreaContextState g_area_ctx_state LATTER_CORE_INIT_EARLY{};
RemoteState g_remote_state LATTER_CORE_INIT_EARLY{};
WeatherState g_weather_state LATTER_CORE_INIT_EARLY{};
CalendarState g_calendar_state LATTER_CORE_INIT_EARLY{};
ShortcutsState g_shortcuts_state LATTER_CORE_INIT_EARLY{};

#undef LATTER_CORE_INIT_EARLY

CoreStoreFootprint core_store_footprint() noexcept {
  constexpr size_t pod = sizeof(g_link_state) + sizeof(g_v3_mismatch_warn_count) +
                       sizeof(g_device_prefs_state) + sizeof(g_persisted_prefs) +
                       sizeof(g_bedtime_schedule_tracker);
  constexpr size_t dyn = sizeof(g_runtime_state) + sizeof(g_media_state) +
                       sizeof(g_home_hub_state) + sizeof(g_area_ctx_state) +
                       sizeof(g_remote_state) + sizeof(g_weather_state) +
                       sizeof(g_calendar_state) + sizeof(g_shortcuts_state);
  return CoreStoreFootprint{pod, dyn, pod + dyn};
}

}  // namespace latter_core
