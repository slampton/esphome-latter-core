#pragma once

// ==============================================================================
// Voice Assistant Session & Wake Word Helpers (va_cancel_helper.h)
// ==============================================================================
// Clean public header forwarding to components/latter_hal/esphome_bridge.cpp.
// Enforces the lockstep ESPHome minimum version across all builds.
// ==============================================================================

#include "esphome/core/defines.h"
#include "esphome/core/version.h"

#if defined(ESPHOME_VERSION_CODE) && defined(VERSION_CODE)
static_assert(ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 11, 0),
              "Latter requires ESPHome 2026.11.0b3 or newer (devices/*.yaml min_version).");
#endif

#ifdef USE_ESP32
#include <esp_sleep.h>
#endif

#include "esphome_bridge.h"
