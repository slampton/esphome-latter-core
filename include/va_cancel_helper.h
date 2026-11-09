#pragma once
#include "esphome.h"
#include "esphome/core/version.h"

#if defined(ESPHOME_VERSION_CODE) && defined(VERSION_CODE)
static_assert(ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 11, 0),
              "Latter requires ESPHome 2026.11.0b2 or newer (devices/*.yaml min_version).");
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2027, 1, 0)
#warning "va_cancel_helper.h private-member bridge not validated on this ESPHome version; Phase 4 removes it"
#endif
#endif
#ifdef USE_ESP32
#include <esp_sleep.h>
#endif

// ==============================================================================
// Voice Assistant Session & Wake Word Helpers (va_cancel_helper.h)
// ==============================================================================
// Provides clean, encapsulated lifecycle helpers for ESPHome's final VoiceAssistant
// component and the on-device wake-word picker:
//   1. stop_va_microphone(va)
//   2. abort_voice_assistant(va)
//   3. primary_wake_word_name(mww) & cycle_primary_wake_word(va, mww)
//   4. on_api_client_authenticated(va, client_info, client_address)
//   5. clean_disconnect_api_before_hibernate(va)
// ==============================================================================

template<typename Tag, typename Tag::type M>
struct Rob {
  friend typename Tag::type get(Tag) { return M; }
};

struct VA_SetState_Tag {
  typedef void (esphome::voice_assistant::VoiceAssistant::*type)(esphome::voice_assistant::State, esphome::voice_assistant::State);
  friend type get(VA_SetState_Tag);
};
template struct Rob<VA_SetState_Tag, &esphome::voice_assistant::VoiceAssistant::set_state_>;

#ifdef USE_MICROPHONE
struct VA_MicSource_Tag {
  typedef esphome::microphone::MicrophoneSource *esphome::voice_assistant::VoiceAssistant::*type;
  friend type get(VA_MicSource_Tag);
};
template struct Rob<VA_MicSource_Tag, &esphome::voice_assistant::VoiceAssistant::mic_source_>;
#endif

struct VA_ApiClient_Tag {
  typedef esphome::api::APIConnection *esphome::voice_assistant::VoiceAssistant::*type;
  friend type get(VA_ApiClient_Tag);
};
template struct Rob<VA_ApiClient_Tag, &esphome::voice_assistant::VoiceAssistant::api_client_>;

namespace esphome {
namespace voice_assistant {

// Stops VoiceAssistant's internal MicrophoneSource right now, for a synchronous abort
// (abort_voice_assistant). Since ESPHome 2026.11 the component itself stops the microphone at STT_END
// and when STT_END + TTS_END arrive in one burst (esphome#21380, #21755), so the STT callbacks don't
// call this.
inline void stop_va_microphone(VoiceAssistant *va) {
  if (va == nullptr) return;
#ifdef USE_MICROPHONE
  auto *ms = va->*get(VA_MicSource_Tag());
  if (ms != nullptr && ms->is_running()) {
    ms->stop();
  }
#endif
}

// Performs a complete, synchronous client + server VoiceAssistant session abort.
inline void abort_voice_assistant(VoiceAssistant *va) {
  api::APIConnection *conn = nullptr;
  if (va != nullptr) {
    conn = va->get_api_connection();
    stop_va_microphone(va);
    va->request_stop();
    va->reset_conversation_id();
    (va->*get(VA_SetState_Tag()))(State::IDLE, State::IDLE);
  }

  if (conn != nullptr) {
    api::VoiceAssistantRequest msg;
    msg.start = false;
    bool sent = conn->send_message(msg);
    (void)sent;
  }
}

// ==============================================================================
// Wake Words: One Path With Home Assistant
// ==============================================================================
// Home Assistant's Assist satellite Wake Word selects are the one control. They arrive as
// VoiceAssistant::on_set_configuration(), which enables the listed micro_wake_word models. Every
// WakeWordModel saves its own enabled flag in preferences on enable() / disable() and loads it at boot
// (micro_wake_word/streaming_model.cpp), so the choice survives reboots with nothing stored by Latter,
// and the inference task loads and unloads models as their flags change, so nothing restarts it.
// The on-device Settings > Voice picker goes through the same on_set_configuration() call, and its
// options are the compiled models (id + wake_word name). Home Assistant's selects show a choice made on
// the device after their next configuration request (on reconnect): the native API has no
// device-initiated configuration push (VoiceAssistantConfigurationResponse only answers a request).
#ifdef USE_MICRO_WAKE_WORD
// The device's primary wake word: the first enabled model, which is also the first active wake word
// Home Assistant's configuration request reports (VoiceAssistant::get_configuration()).
inline std::string primary_wake_word_name(micro_wake_word::MicroWakeWord *mww) {
  if (mww != nullptr) {
    for (auto *model : mww->get_wake_words()) {
      if (model->is_enabled()) return model->get_wake_word();
    }
  }
  return "None";
}

// Settings > Voice picker: the next compiled model becomes the primary wake word; a second enabled
// model (Home Assistant's Wake Word 2) stays enabled.
inline void cycle_primary_wake_word(VoiceAssistant *va, micro_wake_word::MicroWakeWord *mww) {
  if (va == nullptr || mww == nullptr) return;
  const auto models = mww->get_wake_words();
  const size_t n = models.size();
  if (n == 0) return;
  size_t primary = n, second = n;
  for (size_t i = 0; i < n; i++) {
    if (!models[i]->is_enabled()) continue;
    if (primary == n) {
      primary = i;
    } else if (second == n) {
      second = i;
    }
  }
  size_t next = (primary == n) ? 0 : (primary + 1) % n;
  if (next == second) {
    if (n <= 2) return;  // every compiled model is already enabled
    next = (next + 1) % n;
  }
  std::vector<std::string> active{models[next]->get_id()};
  if (second != n && second != next) active.push_back(models[second]->get_id());
  va->on_set_configuration(active);
}
#endif

// Called synchronously from `api: on_client_connected:` inside APIConnection::complete_authentication_()
inline void on_api_client_authenticated(VoiceAssistant *va, const std::string &client_info,
                                        const std::string &client_address) {
  if (va == nullptr) return;
  if (client_info.find("Home Assistant") != std::string::npos) {
    if (va->get_api_connection() != nullptr) {
      ESP_LOGI("va_api", "Releasing stale VoiceAssistant API client for new Home Assistant connection (%s)",
               client_address.c_str());
      va->*get(VA_ApiClient_Tag()) = nullptr;
    }
  }
}

// Cleanly sends ESPHome DisconnectRequest + TCP FIN to all connected API clients
// before the AXP2101 cuts the 3.3V power rail in Hibernate (enter_deep_sleep).
// Prevents Home Assistant from staying stuck on a half-open ("zombie") TCP socket
// for 60-90s after power-off and ignoring mDNS wake announcements on the next boot.
inline void clean_disconnect_api_before_hibernate(VoiceAssistant *va) {
  if (va != nullptr) {
    va->*get(VA_ApiClient_Tag()) = nullptr;
  }
  if (api::global_api_server != nullptr) {
    ESP_LOGI("power", "Sending clean API DisconnectRequest to Home Assistant before Hibernate rail cut");
    api::global_api_server->on_shutdown();
  }
}

}  // namespace voice_assistant
}  // namespace esphome

