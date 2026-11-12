#include "esphome_bridge.h"

#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"

#include <vector>

#if defined(ESPHOME_VERSION_CODE) && defined(VERSION_CODE)
static_assert(ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 11, 0),
              "Latter requires ESPHome 2026.11.0b4 or newer (devices/*.yaml min_version).");
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2027, 1, 0)
#warning "esphome_bridge.cpp private-member bridge not validated on this ESPHome version; verify upstream public API"
#endif
#endif

#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif
#ifdef USE_VOICE_ASSISTANT
#include "esphome/components/voice_assistant/voice_assistant.h"
#endif
#ifdef USE_MICRO_WAKE_WORD
#include "esphome/components/micro_wake_word/micro_wake_word.h"
#endif
#if defined(USE_ESP32_VARIANT_ESP32P4) && __has_include("esphome/components/mipi_dsi/mipi_dsi.h")
#include "esphome/components/mipi_dsi/mipi_dsi.h"
#endif

namespace {

template <typename Tag, typename Tag::type M>
struct BridgeRob {
  friend constexpr typename Tag::type bridge_get(Tag) { return M; }
};

#ifdef USE_VOICE_ASSISTANT
struct VA_SetState_Tag {
  using type = void (esphome::voice_assistant::VoiceAssistant::*)(esphome::voice_assistant::State,
                                                                  esphome::voice_assistant::State);
  friend constexpr type bridge_get(VA_SetState_Tag);
};
template struct BridgeRob<VA_SetState_Tag, &esphome::voice_assistant::VoiceAssistant::set_state_>;

#ifdef USE_MICROPHONE
struct VA_MicSource_Tag {
  using type = esphome::microphone::MicrophoneSource *esphome::voice_assistant::VoiceAssistant::*;
  friend constexpr type bridge_get(VA_MicSource_Tag);
};
template struct BridgeRob<VA_MicSource_Tag, &esphome::voice_assistant::VoiceAssistant::mic_source_>;
#endif

#ifdef USE_API
struct VA_ApiClient_Tag {
  using type = esphome::api::APIConnection *esphome::voice_assistant::VoiceAssistant::*;
  friend constexpr type bridge_get(VA_ApiClient_Tag);
};
template struct BridgeRob<VA_ApiClient_Tag, &esphome::voice_assistant::VoiceAssistant::api_client_>;
#endif
#endif  // USE_VOICE_ASSISTANT

#if defined(USE_ESP32_VARIANT_ESP32P4) && __has_include("esphome/components/mipi_dsi/mipi_dsi.h")
struct MipiDsiBufferTag {
  using type = uint8_t *(esphome::mipi_dsi::MipiDsi::*);
  friend constexpr type bridge_get(MipiDsiBufferTag);
};
template struct BridgeRob<MipiDsiBufferTag, &esphome::mipi_dsi::MipiDsi::buffer_>;
#endif

}  // namespace

namespace esphome::voice_assistant {

#ifdef USE_VOICE_ASSISTANT
void stop_va_microphone(VoiceAssistant *va) {
  if (va == nullptr) return;
#ifdef USE_MICROPHONE
  auto *ms = va->*bridge_get(VA_MicSource_Tag{});
  if (ms != nullptr && ms->is_running()) {
    ms->stop();
  }
#endif
}

void abort_voice_assistant(VoiceAssistant *va) {
#ifdef USE_API
  api::APIConnection *conn = nullptr;
  if (va != nullptr) {
    conn = va->get_api_connection();
    stop_va_microphone(va);
    va->request_stop();
    va->reset_conversation_id();
    (va->*bridge_get(VA_SetState_Tag{}))(State::IDLE, State::IDLE);
  }
  if (conn != nullptr) {
    api::VoiceAssistantRequest msg;
    msg.start = false;
    bool sent = conn->send_message(msg);
    (void)sent;
  }
#else
  if (va != nullptr) {
    stop_va_microphone(va);
    va->request_stop();
    va->reset_conversation_id();
    (va->*bridge_get(VA_SetState_Tag{}))(State::IDLE, State::IDLE);
  }
#endif
}

void on_api_client_authenticated(VoiceAssistant *va, const std::string &client_info,
                                 const std::string &client_address) {
#ifdef USE_API
  if (va == nullptr) return;
  if (client_info.find("Home Assistant") != std::string::npos) {
    if (va->get_api_connection() != nullptr) {
      ESP_LOGI("va_api", "Releasing stale VoiceAssistant API client for new Home Assistant connection (%s)",
               client_address.c_str());
      va->*bridge_get(VA_ApiClient_Tag{}) = nullptr;
    }
  }
#else
  (void)va;
  (void)client_info;
  (void)client_address;
#endif
}

void clean_disconnect_api_before_hibernate(VoiceAssistant *va) {
#ifdef USE_API
  if (va != nullptr) {
    va->*bridge_get(VA_ApiClient_Tag{}) = nullptr;
  }
  if (api::global_api_server != nullptr) {
    ESP_LOGI("power", "Sending clean API DisconnectRequest to Home Assistant before Hibernate rail cut");
    api::global_api_server->on_shutdown();
  }
#else
  (void)va;
#endif
}
#endif  // USE_VOICE_ASSISTANT

#if defined(USE_VOICE_ASSISTANT) && defined(USE_MICRO_WAKE_WORD)
std::string primary_wake_word_name(micro_wake_word::MicroWakeWord *mww) {
  if (mww != nullptr) {
    for (auto *model : mww->get_wake_words()) {
      if (model->is_enabled()) return model->get_wake_word();
    }
  }
  return "None";
}

void cycle_primary_wake_word(VoiceAssistant *va, micro_wake_word::MicroWakeWord *mww) {
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
    if (n <= 2) return;
    next = (next + 1) % n;
  }
  std::vector<std::string> active{models[next]->get_id()};
  if (second != n && second != next) active.push_back(models[second]->get_id());
  va->on_set_configuration(active);
}
#endif

}  // namespace esphome::voice_assistant

namespace latter_hal {
#if defined(USE_ESP32_VARIANT_ESP32P4) && __has_include("esphome/components/mipi_dsi/mipi_dsi.h")
uint8_t *get_mipi_dsi_buffer(esphome::mipi_dsi::MipiDsi *d) {
  return (d != nullptr) ? (d->*bridge_get(MipiDsiBufferTag{})) : nullptr;
}
#endif
}  // namespace latter_hal
