#pragma once

// ==============================================================================
// Isolated ESPHome Private-Member Bridge Declarations (esphome_bridge.h)
// ==============================================================================
// Declares the VoiceAssistant session/wake-word helpers and MipiDsi framebuffer
// getter. All Rob<> private-member accessors live exclusively inside
// components/latter_hal/esphome_bridge.cpp behind ESPHOME_VERSION_CODE guards.
// ==============================================================================

#include <cstdint>
#include <string>

#include "esphome/core/defines.h"

namespace esphome {
namespace voice_assistant {
class VoiceAssistant;
}  // namespace voice_assistant
namespace micro_wake_word {
class MicroWakeWord;
}  // namespace micro_wake_word
namespace mipi_dsi {
class MipiDsi;
}  // namespace mipi_dsi
}  // namespace esphome

namespace esphome::voice_assistant {

#ifdef USE_VOICE_ASSISTANT
void stop_va_microphone(VoiceAssistant *va);
void abort_voice_assistant(VoiceAssistant *va);
void on_api_client_authenticated(VoiceAssistant *va, const std::string &client_info,
                                 const std::string &client_address);
void clean_disconnect_api_before_hibernate(VoiceAssistant *va);
#else
inline void stop_va_microphone(VoiceAssistant *) {}
inline void abort_voice_assistant(VoiceAssistant *) {}
inline void on_api_client_authenticated(VoiceAssistant *, const std::string &, const std::string &) {}
inline void clean_disconnect_api_before_hibernate(VoiceAssistant *) {}
#endif

#if defined(USE_VOICE_ASSISTANT) && defined(USE_MICRO_WAKE_WORD)
std::string primary_wake_word_name(micro_wake_word::MicroWakeWord *mww);
void cycle_primary_wake_word(VoiceAssistant *va, micro_wake_word::MicroWakeWord *mww);
#else
inline std::string primary_wake_word_name(micro_wake_word::MicroWakeWord *) { return "None"; }
inline void cycle_primary_wake_word(VoiceAssistant *, micro_wake_word::MicroWakeWord *) {}
#endif

}  // namespace esphome::voice_assistant

namespace latter_hal {
#if defined(USE_ESP32_VARIANT_ESP32P4) && __has_include("esphome/components/mipi_dsi/mipi_dsi.h")
uint8_t *get_mipi_dsi_buffer(esphome::mipi_dsi::MipiDsi *d);
#else
inline uint8_t *get_mipi_dsi_buffer(esphome::mipi_dsi::MipiDsi *) { return nullptr; }
#endif
}  // namespace latter_hal
