# Upstream ESPHome PR Specification: Public `VoiceAssistant` Session Control Hooks

## Target component
- **Repository:** `esphome/esphome`
- **Files:** `esphome/components/voice_assistant/voice_assistant.h` and `voice_assistant.cpp`

## Motivation
`include/va_cancel_helper.h` reaches three private `VoiceAssistant` members through explicit template instantiation (`Rob<>` tags): `set_state_`, `mic_source_` and `api_client_`. Public hooks would let it drop them.

1. **Synchronous session abort.** When the user cancels a voice turn (a tap on the overlay, an alarm or timer takeover, a doorbell talk turn whose reply must be dropped), the device needs to stop the microphone, tell Home Assistant to stop the pipeline, and return to `IDLE` *now*, not after the next event. Today that needs `mic_source_` and `set_state_`.
2. **API connection release before hibernate.** A battery device that hibernates or reconnects before the server's TCP keepalive expires can keep `api_client_` bound to a dead `APIConnection *` and reject the new session. The helper clears it before the AXP2101 cuts power.

Since ESPHome 2026.11 the component stops the microphone itself at `STT_END`, and when `STT_END` + `TTS_END` arrive in one burst (esphome#21380, #21755). And every Latter board now runs a full-duplex I2S bus, so no bus hand-off needs the microphone stopped early. Continued conversation is never cleared (`continue_conversation_` must keep working natively), so no setter for it is needed.

## Proposed public API (`voice_assistant.h`)

```cpp
namespace esphome::voice_assistant {

class VoiceAssistant : public Component, ... {
 public:
  // Synchronously abort any active session: stop the microphone, signal stop to Home Assistant,
  // reset the conversation id and return to IDLE.
  void abort_session();

  // Forget the current APIConnection, before deep sleep or hibernate.
  void release_api_connection() { this->api_client_ = nullptr; }
};

}  // namespace esphome::voice_assistant
```

## Migration in `esphome-latter-core`
Until this is merged, `include/va_cancel_helper.h` keeps the three `Rob<>` tags (`VA_SetState_Tag`, `VA_MicSource_Tag`, `VA_ApiClient_Tag`). They are guarded by a `static_assert` requiring ESPHome ≥ 2026.11.0 and a `#warning` from 2027.1. Once merged, `abort_voice_assistant()` becomes `va->abort_session()`, the hibernate path calls `va->release_api_connection()`, and all `Rob<>` instantiations are deleted.
