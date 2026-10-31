# Upstream ESPHome PR Specification: Public `VoiceAssistant` Session Control Hooks

## Target Component
- **Repository**: `esphome/esphome`
- **Files**:
  - `esphome/components/voice_assistant/voice_assistant.h`
  - `esphome/components/voice_assistant/voice_assistant.cpp`

## Summary / Motivation
External voice satellite integrations (particularly battery-powered and half-duplex I2S devices sharing `BCLK`/`WS`/`MCLK` between an ES8311 DAC and ES7210/ES8311 ADC, or devices using PSRAM-buffered `mixer` speakers with long `buffer_duration` windows) need four lifecycle operations on `esphome::voice_assistant::VoiceAssistant` that currently touch `private:` members:

1. **Immediate Microphone Stop (`stop_microphone()`)**:
   - On half-duplex I2S buses, the microphone (`mic_source_`) must stop before routing I2S clocks to the speaker when Home Assistant sends `STT_END` + `TTS_END` in a single TCP frame without an intermediate `STT_VAD_END`.
2. **Continue-Conversation Suppression (`set_continue_conversation(bool)`)**:
   - When TTS audio is decoded into a large PSRAM `mixer` buffer (`15s`), `media_player` transitions to `IDLE` as soon as decoding completes while the speaker ring buffer is still playing out. Clearing `continue_conversation_` prevents `VoiceAssistant` from re-opening the microphone mid-sentence while the mixer finishes draining.
3. **Synchronous Session Abort (`abort_session()`)**:
   - When a high-priority local hardware event occurs (local countdown timer alarm, physical cancel tap, or entering deep sleep / hibernate), the firmware needs to synchronously stop the microphone, clear internal stream/TTS flags (`wait_for_stream_end_`, `stream_ended_`, `started_streaming_tts_`, `continuous_`, `continue_conversation_`), clear buffers (`clear_buffers_()`), and return the state machine to `State::IDLE`.
4. **Stale API Connection Release (`release_api_connection()`)**:
   - When a battery satellite wakes from deep sleep / hibernate or reconnects over Wi-Fi before the server's previous TCP keepalive expires, `api_client_` can remain bound to the dead `APIConnection *`, rejecting the newly authenticated Home Assistant connection.

## Proposed Public API Additions (`voice_assistant.h`)

```cpp
namespace esphome::voice_assistant {

class VoiceAssistant : public Component, ... {
 public:
  // Stop active MicrophoneSource immediately (safe on shared half-duplex I2S buses)
  void stop_microphone() {
#ifdef USE_MICROPHONE
    if (this->mic_source_ != nullptr && this->mic_source_->is_running()) {
      this->mic_source_->stop();
    }
#endif
  }

  // Override continue_conversation flag for buffered external media_player/mixer pipelines
  void set_continue_conversation(bool continue_conversation) {
    this->continue_conversation_ = continue_conversation;
  }

  // Release current APIConnection pointer on reconnect or prior to deep sleep
  void release_api_connection() {
    this->api_client_ = nullptr;
  }

  // Synchronously abort any active STT/TTS session and reset state to IDLE
  void abort_session() {
    this->stop_microphone();
    this->request_stop();
    this->reset_conversation_id();
#ifdef USE_SPEAKER
    this->wait_for_stream_end_ = false;
    this->stream_ended_ = false;
#endif
#ifdef USE_MEDIA_PLAYER
    this->started_streaming_tts_ = false;
#endif
    this->continuous_ = false;
    this->continue_conversation_ = false;
    this->clear_buffers_();
    this->set_state_(State::IDLE, State::IDLE);
  }
};

}  // namespace esphome::voice_assistant
```

## Migration Plan in `esphome-latter-core`
Until this PR is merged upstream, `include/va_cancel_helper.h` fences the remaining 9 `Rob<>` tags behind:
```cpp
static_assert(ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 10, 0) &&
              ESPHOME_VERSION_CODE < VERSION_CODE(2027, 1, 0), ...);
```
Once merged into ESPHome, all remaining `Rob<>` instantiations in `include/va_cancel_helper.h` can be deleted in favor of `va->stop_microphone()`, `va->set_continue_conversation(false)`, `va->release_api_connection()`, and `va->abort_session()`.
