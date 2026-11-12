# `latter_core` Component & Upstream ESPHome Bridge Notes

`components/latter_core` is the unified non-UI C++20 runtime and hardware abstraction component for `esphome-latter-core`. It stages the 13 non-UI headers (`latter_prof.h`, `hal_manifest.h`, `esphome_bridge.h`, `audio_router.h`, `hal_imu_qmi8658.h`, `hal_battery.h`, `hal_panel_power.h`, `hal_power_pm.h`, `hal_link_report.h`, `latter_protocol.h`, `latter_state.h`, `latter_rpc.h`, `latter_actions.h`), compiles the hardware drivers (`audio_es8311.cpp`, `imu_qmi8658.cpp`, `panel_power.cpp`, `power_battery.cpp`, `power_pm.cpp`, `esphome_bridge.cpp`), registers the paced RPC outbox loop probe (`LatterLoopProbe`), and auto-loads `latter_ui` unless `gui: "none"` is configured.

---

## Upstream ESPHome PR Specification: Public `VoiceAssistant` Session Control Hooks

### Target component
- **Repository:** `esphome/esphome`
- **Files:** `esphome/components/voice_assistant/voice_assistant.h` and `voice_assistant.cpp`

### Motivation
`components/latter_core/esphome_bridge.cpp` (`include/esphome_bridge.h`) reaches three private `VoiceAssistant` members through explicit template instantiation (`BridgeRob<>` tags): `set_state_`, `mic_source_`, and `api_client_` (alongside `MipiDsi::buffer_` for zero-copy DSI scanlines). Public hooks would let it drop the `VoiceAssistant` bridge tags completely.

1. **Synchronous session abort.** When the user cancels a voice turn (a tap on the overlay, an alarm or timer takeover, a doorbell talk turn whose reply must be dropped), the device needs to stop the microphone, tell Home Assistant to stop the pipeline, and return to `IDLE` *now*, not after the next event. Today that needs `mic_source_` and `set_state_`.
2. **API connection release before hibernate.** A battery device that hibernates or reconnects before the server's TCP keepalive expires can keep `api_client_` bound to a dead `APIConnection *` and reject the new session. The helper clears it before the AXP2101 cuts power.

Since ESPHome 2026.11 the component stops the microphone itself at `STT_END`, and when `STT_END` + `TTS_END` arrive in one burst (esphome#21380, #21755). Every Latter board runs a full-duplex I2S bus, so no bus hand-off needs the microphone stopped early. Continued conversation is never cleared (`continue_conversation_` must keep working natively), so no setter for it is needed.

### Proposed public API (`voice_assistant.h`)

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

### Migration in `esphome-latter-core`
Until this is merged, `components/latter_core/esphome_bridge.cpp` isolates the `BridgeRob<>` tags in a single translation unit guarded by a `static_assert` requiring ESPHome >= 2026.11.0 and a `#warning` from 2027.1 (`include/esphome_bridge.h`). Once merged, `abort_voice_assistant()` becomes `va->abort_session()`, the hibernate path calls `va->release_api_connection()`, and the `VoiceAssistant` `BridgeRob<>` instantiations are deleted.
