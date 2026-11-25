#include "esphome_bridge.h"

#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"

#include <vector>

#if defined(ESPHOME_VERSION_CODE) && defined(VERSION_CODE)
static_assert(ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 11, 1),
              "Latter requires ESPHome 2026.11.1 or newer (devices/*.yaml min_version).");
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

// Standard C++ explicit-instantiation private-member bridge, isolated to this single TU.
// Awaits two upstream ESPHome public APIs so these tags can be deleted:
//   1. VoiceAssistant public session-abort & API-connection-release hooks
//      (replacing set_state_, mic_source_, and api_client_; see components/latter_core/README.md).
//   2. MipiDsi public framebuffer pointer getter
//      (replacing MipiDsi::buffer_ for zero-copy RGB565 scanlines on ESP32-P4 MIPI-DSI panels).
// Guarded by the ESPHOME_VERSION_CODE >= 2026.11.1 static_assert above and the weekly
// .github/workflows/esphome-canary.yml build against upstream esphome:dev (validated through 2026.12).
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

namespace latter_core {

#ifdef USE_VOICE_ASSISTANT
void stop_va_microphone(esphome::voice_assistant::VoiceAssistant *va) {
  if (va == nullptr) return;
#ifdef USE_MICROPHONE
  auto *ms = va->*bridge_get(VA_MicSource_Tag{});
  if (ms != nullptr && ms->is_running()) {
    ms->stop();
  }
#endif
}

void abort_voice_assistant(esphome::voice_assistant::VoiceAssistant *va) {
#ifdef USE_API
  esphome::api::APIConnection *conn = nullptr;
  if (va != nullptr) {
    conn = va->get_api_connection();
    stop_va_microphone(va);
    va->request_stop();
    va->reset_conversation_id();
    (va->*bridge_get(VA_SetState_Tag{}))(esphome::voice_assistant::State::IDLE,
                                         esphome::voice_assistant::State::IDLE);
  }
  if (conn != nullptr) {
    esphome::api::VoiceAssistantRequest msg;
    msg.start = false;
    bool sent = conn->send_message(msg);
    (void)sent;
  }
#else
  if (va != nullptr) {
    stop_va_microphone(va);
    va->request_stop();
    va->reset_conversation_id();
    (va->*bridge_get(VA_SetState_Tag{}))(esphome::voice_assistant::State::IDLE,
                                         esphome::voice_assistant::State::IDLE);
  }
#endif
}

void on_api_client_authenticated(esphome::voice_assistant::VoiceAssistant *va,
                                 std::string_view client_info,
                                 std::string_view client_address) {
#ifdef USE_API
  if (va == nullptr) return;
  if (client_info.find("Home Assistant") != std::string_view::npos) {
    if (va->get_api_connection() != nullptr) {
      ESP_LOGD("va_api", "Releasing stale VoiceAssistant API client for new Home Assistant connection (%.*s)",
               static_cast<int>(client_address.size()), client_address.empty() ? "" : client_address.data());
      va->*bridge_get(VA_ApiClient_Tag{}) = nullptr;
    }
  }
#else
  (void)va;
  (void)client_info;
  (void)client_address;
#endif
}

void clean_disconnect_api_before_hibernate(esphome::voice_assistant::VoiceAssistant *va) {
#ifdef USE_API
  if (va != nullptr) {
    va->*bridge_get(VA_ApiClient_Tag{}) = nullptr;
  }
  if (esphome::api::global_api_server != nullptr) {
    ESP_LOGD("power", "Sending clean API DisconnectRequest to Home Assistant before Hibernate rail cut");
    esphome::api::global_api_server->on_shutdown();
  }
#else
  (void)va;
#endif
}
#endif  // USE_VOICE_ASSISTANT

#if defined(USE_VOICE_ASSISTANT) && defined(USE_MICRO_WAKE_WORD)
std::string primary_wake_word_name(esphome::micro_wake_word::MicroWakeWord *mww) {
  if (mww != nullptr) {
    for (auto *model : mww->get_wake_words()) {
      if (model->is_enabled()) return model->get_wake_word();
    }
  }
  return "None";
}

void cycle_primary_wake_word(esphome::voice_assistant::VoiceAssistant *va,
                             esphome::micro_wake_word::MicroWakeWord *mww) {
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

#if defined(USE_ESP32_VARIANT_ESP32P4) && __has_include("esphome/components/mipi_dsi/mipi_dsi.h")
uint8_t *get_mipi_dsi_buffer(esphome::mipi_dsi::MipiDsi *d) {
  return (d != nullptr) ? (d->*bridge_get(MipiDsiBufferTag{})) : nullptr;
}
#endif

}  // namespace latter_core
