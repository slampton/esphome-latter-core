#pragma once
#include "esphome.h"
#include "esphome/core/version.h"

#if defined(ESPHOME_VERSION_CODE) && defined(VERSION_CODE)
static_assert(ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 10, 0) &&
              ESPHOME_VERSION_CODE < VERSION_CODE(2027, 1, 0),
              "va_cancel_helper.h Rob<> private-member bridge is validated for ESPHome 2026.10.x-2026.12.x; "
              "verify VoiceAssistant private member layout before bumping the upper version bound.");
#endif
#ifdef USE_ESP32
#include <esp_sleep.h>
#endif

// ==============================================================================
// Voice Assistant Session, Mixer & Wake Word Helpers (va_cancel_helper.h)
// ==============================================================================
// Provides clean, encapsulated lifecycle helpers for ESPHome's final VoiceAssistant
// component, its PSRAM MixerSpeaker announcement channel, and on-board
// micro_wake_word configuration + NVS flash persistence:
//   1. disarm_va_speaker_timeout(va)
//   2. reset_va_session_state(va)
//   3. abort_voice_assistant(va)
//   4. ensure_wake_words_restored(va) & poll_and_persist_wake_words(va)
//   5. on_api_client_authenticated(va, client_info, client_address)
//   6. clean_disconnect_api_before_hibernate(va)
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

struct VA_ContinueConversation_Tag {
  typedef bool esphome::voice_assistant::VoiceAssistant::*type;
  friend type get(VA_ContinueConversation_Tag);
};
template struct Rob<VA_ContinueConversation_Tag, &esphome::voice_assistant::VoiceAssistant::continue_conversation_>;

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

// Clears continue_conversation_ so LLM responses buffered in announcement_mixing_input's
// 15s PSRAM ring buffer never re-open the microphone mid-sentence while playing out.
inline void disarm_va_speaker_timeout(VoiceAssistant *va) {
  if (va != nullptr) {
    va->*get(VA_ContinueConversation_Tag()) = false;
  }
}

// Resets per-utterance continuation state on VoiceAssistant.
inline void reset_va_session_state(VoiceAssistant *va) {
  if (va != nullptr) {
    disarm_va_speaker_timeout(va);
  }
}


// Explicitly stops VoiceAssistant's internal MicrophoneSource so shared I2S buses unlock immediately
// even when Home Assistant sends STT_END + TTS_END in a single burst without STT_VAD_END.
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
  if (va != nullptr) {
    stop_va_microphone(va);
    va->request_stop();
    va->reset_conversation_id();
    reset_va_session_state(va);
    (va->*get(VA_SetState_Tag()))(State::IDLE, State::IDLE);
  }

  if (api::global_api_server != nullptr) {
    for (auto &client : api::global_api_server->active_clients()) {
      api::VoiceAssistantRequest msg;
      msg.start = false;
      bool sent = client->send_message(msg);
      (void)sent;
    }
  }
}

// ==============================================================================
// Wake Word NVS Persistence (Dual-Slot ww1 & ww2), Model State Sync & API Handoff
// ==============================================================================
struct SavedWakeWordsNVS {
  uint32_t magic;
  char ww1[28];
  char ww2[28];
};

static constexpr uint32_t WAKE_WORDS_NVS_KEY = 0x50415757UL;
static constexpr uint32_t WAKE_WORDS_NVS_MAGIC = 0x50415734UL;

inline bool &wake_words_restored_flag() {
  static bool restored = false;
  return restored;
}

inline std::string &last_persisted_ww1() {
  static std::string s;
  return s;
}

inline std::string &last_persisted_ww2() {
  static std::string s;
  return s;
}

inline void save_wake_words_to_nvs(const std::string &w1, const std::string &w2 = "") {
  last_persisted_ww1() = w1;
  last_persisted_ww2() = w2;
  if (global_preferences == nullptr) return;
  SavedWakeWordsNVS data{};
  data.magic = WAKE_WORDS_NVS_MAGIC;
  std::snprintf(data.ww1, sizeof(data.ww1), "%s", w1.c_str());
  std::snprintf(data.ww2, sizeof(data.ww2), "%s", w2.c_str());
  auto pref = global_preferences->make_preference<SavedWakeWordsNVS>(WAKE_WORDS_NVS_KEY);
  pref.save(&data);
  global_preferences->sync();
  ESP_LOGI("va_nvs", "Saved wake words to NVS: ww1='%s', ww2='%s'", w1.c_str(), w2.c_str());
}

#ifdef USE_MICRO_WAKE_WORD
// Restores both Wake Word 1 and Wake Word 2 from NVS on boot
inline void ensure_wake_words_restored(esphome::micro_wake_word::MicroWakeWord *mww, bool force = false) {
  if ((wake_words_restored_flag() && !force) || mww == nullptr) return;
  wake_words_restored_flag() = true;

  std::string w1 = "okay_nabu";
  std::string w2 = "";
  if (global_preferences != nullptr) {
    SavedWakeWordsNVS data{};
    auto pref = global_preferences->make_preference<SavedWakeWordsNVS>(WAKE_WORDS_NVS_KEY);
    if (pref.load(&data) && data.magic == WAKE_WORDS_NVS_MAGIC) {
      data.ww1[sizeof(data.ww1) - 1] = '\0';
      data.ww2[sizeof(data.ww2) - 1] = '\0';
      w1 = data.ww1;
      w2 = data.ww2;
    } else {
      save_wake_words_to_nvs(w1, w2);
    }
  }
  last_persisted_ww1() = w1;
  last_persisted_ww2() = w2;

  for (auto *model : mww->get_wake_words()) {
    model->disable();
  }
  int matched_count = 0;
  for (auto *model : mww->get_wake_words()) {
    const std::string id_str = model->get_id();
    if ((!w1.empty() && w1 != "no_wake_word" && id_str == w1) ||
        (!w2.empty() && w2 != "no_wake_word" && id_str == w2)) {
      model->enable();
      matched_count++;
    }
  }
  if (matched_count == 0 && w1 != "no_wake_word" && !mww->get_wake_words().empty()) {
    mww->get_wake_words().front()->enable();
    last_persisted_ww1() = mww->get_wake_words().front()->get_id();
  }
}

// Polls mww->get_wake_words() once per second to detect when Home Assistant updates
// Wake Word 1 or Wake Word 2 via on_set_configuration, persisting both slots to NVS.
inline bool poll_and_persist_wake_words(esphome::micro_wake_word::MicroWakeWord *mww) {
  if (mww == nullptr) return false;
  ensure_wake_words_restored(mww);

  std::string cur_w1 = "no_wake_word";
  std::string cur_w2 = "";
  for (auto *model : mww->get_wake_words()) {
    if (model->is_enabled()) {
      if (cur_w1 == "no_wake_word") {
        cur_w1 = model->get_id();
      } else if (cur_w2.empty()) {
        cur_w2 = model->get_id();
      } else {
        // Keep at most 2 active wake words to match Home Assistant's 2-slot Assist Satellite UI
        model->disable();
      }
    }
  }

  if (cur_w1 != last_persisted_ww1() || cur_w2 != last_persisted_ww2()) {
    save_wake_words_to_nvs(cur_w1, cur_w2);
    if (mww->is_running()) {
      mww->stop();
    }
    return true;
  }
  return false;
}
#endif

// Called synchronously from `api: on_client_connected:` inside APIConnection::complete_authentication_()
inline void on_api_client_authenticated(VoiceAssistant *va,
#ifdef USE_MICRO_WAKE_WORD
                                        esphome::micro_wake_word::MicroWakeWord *mww,
#endif
                                        const std::string &client_info,
                                        const std::string &client_address) {
  (void)client_address;
#ifdef USE_MICRO_WAKE_WORD
  ensure_wake_words_restored(mww);
#endif
  if (va == nullptr) return;
  if (client_info.find("Home Assistant") != std::string::npos) {
    if (va->get_api_connection() != nullptr) {
      ESP_LOGI("va_api", "Releasing stale VoiceAssistant API client for new Home Assistant connection (%s)",
               client_address.c_str());
      va->*get(VA_ApiClient_Tag()) = nullptr;
    }
  }
}

inline const char *wake_word_id_to_label(const std::string &id_str) {
  if (id_str == "hey_luna") return "Hey Luna";
  if (id_str == "hey_jarvis") return "Hey Jarvis";
  if (id_str == "hey_mycroft") return "Hey Mycroft";
  if (id_str == "alexa") return "Alexa";
  return "Okay Nabu";
}

inline const char *wake_word_label_to_id(const std::string &label) {
  if (label == "Hey Luna") return "hey_luna";
  if (label == "Hey Jarvis") return "hey_jarvis";
  if (label == "Hey Mycroft") return "hey_mycroft";
  if (label == "Alexa") return "alexa";
  return "okay_nabu";
}

#ifdef USE_MICRO_WAKE_WORD
inline void set_primary_wake_word_by_label(esphome::micro_wake_word::MicroWakeWord *mww, const std::string &label) {
  if (mww == nullptr) return;
  const std::string target_id = wake_word_label_to_id(label);
  std::string cur_w2 = last_persisted_ww2();
  if (cur_w2 == target_id) cur_w2 = "";

  for (auto *model : mww->get_wake_words()) {
    const std::string m_id = model->get_id();
    if (m_id == target_id || (!cur_w2.empty() && m_id == cur_w2)) {
      model->enable();
    } else {
      model->disable();
    }
  }
  save_wake_words_to_nvs(target_id, cur_w2);
  if (mww->is_running()) {
    mww->stop();
  }
}
#endif
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

