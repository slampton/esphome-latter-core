#pragma once

// ==============================================================================
// Latter Core Audio Router, Path Arbitrator, Intercom & Session Controllers
// (include/audio_router.h + components/latter_core/audio_session.cpp)
// ==============================================================================

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#ifdef USE_I2C
#include "esphome/components/i2c/i2c_bus.h"
#endif

#include "hal_manifest.h"
#include "latter_prof.h"

namespace latter_ui {

#ifdef USE_ESP32
std::atomic<uint32_t> &echo_last_loud_ms();
std::atomic<bool> &echo_meter_active();
#else
inline std::atomic<uint32_t> &echo_last_loud_ms() {
  static std::atomic<uint32_t> last{0};
  return last;
}
inline std::atomic<bool> &echo_meter_active() {
  static std::atomic<bool> active{false};
  return active;
}
#endif

inline constexpr int64_t kEchoLoudMeanSquare = 33LL * 33LL;

// Alignment-safe, zero-UB PCM16 acoustic echo meter (fast no-op outside active echo-gate window).
inline void echo_meter_feed(std::span<const uint8_t> pcm_bytes) noexcept {
  if (!echo_meter_active().load(std::memory_order_relaxed)) return;
  const size_t n = pcm_bytes.size() / sizeof(int16_t);
  if (n == 0) return;
  int64_t acc = 0;
  for (size_t i = 0; i < n; ++i) {
    int16_t sample = 0;
    std::memcpy(&sample, pcm_bytes.data() + i * sizeof(int16_t), sizeof(int16_t));
    const int32_t s32 = static_cast<int32_t>(sample);
    acc += static_cast<int64_t>(s32) * static_cast<int64_t>(s32);
  }
  if (acc / static_cast<int64_t>(n) > kEchoLoudMeanSquare) {
    echo_last_loud_ms().store(hal_millis_now(), std::memory_order_relaxed);
  }
}

inline void echo_meter_feed(const uint8_t *data, size_t len) noexcept {
  if (data != nullptr && len > 0) {
    echo_meter_feed(std::span<const uint8_t>(data, len));
  }
}

// ------------------------------------------------------------------------------
// Zero-Allocation constexpr STT Local Media Stop/Pause Matcher
// ------------------------------------------------------------------------------
enum class LocalVoiceMediaCmd : uint8_t {
  None = 0,
  PauseOrStop = 1,
};

constexpr char norm_stt_ascii_char(char c) noexcept {
  if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
  if (c == '.' || c == ',' || c == '!' || c == '?' ||
      c == '\t' || c == '\r' || c == '\n') {
    return ' ';
  }
  return c;
}

constexpr std::string_view trim_stt_utterance(std::string_view sv) noexcept {
  while (!sv.empty() && norm_stt_ascii_char(sv.front()) == ' ') sv.remove_prefix(1);
  while (!sv.empty() && norm_stt_ascii_char(sv.back()) == ' ') sv.remove_suffix(1);
  return sv;
}

constexpr bool stt_equals_norm(std::string_view sv, std::string_view lower_target) noexcept {
  if (sv.size() != lower_target.size()) return false;
  for (size_t i = 0; i < sv.size(); ++i) {
    if (norm_stt_ascii_char(sv[i]) != lower_target[i]) return false;
  }
  return true;
}

constexpr bool stt_contains_norm(std::string_view hay, std::string_view lower_needle) noexcept {
  if (lower_needle.empty() || hay.size() < lower_needle.size()) return false;
  const size_t limit = hay.size() - lower_needle.size();
  for (size_t pos = 0; pos <= limit; ++pos) {
    bool ok = true;
    for (size_t j = 0; j < lower_needle.size(); ++j) {
      if (norm_stt_ascii_char(hay[pos + j]) != lower_needle[j]) {
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }
  return false;
}

constexpr LocalVoiceMediaCmd match_local_media_voice_cmd(std::string_view stt_text) noexcept {
  const std::string_view utt = trim_stt_utterance(stt_text);
  if (utt.empty()) return LocalVoiceMediaCmd::None;

  if (stt_equals_norm(utt, "pause") || stt_equals_norm(utt, "stop")) {
    return LocalVoiceMediaCmd::PauseOrStop;
  }

  const bool mentions_media = stt_contains_norm(utt, "music") ||
                              stt_contains_norm(utt, "speaker") ||
                              stt_contains_norm(utt, "audio") ||
                              stt_contains_norm(utt, "song") ||
                              stt_contains_norm(utt, "track") ||
                              stt_contains_norm(utt, "player") ||
                              stt_contains_norm(utt, "playing") ||
                              stt_contains_norm(utt, "playback");
  if (mentions_media &&
      (stt_contains_norm(utt, "pause") ||
       stt_contains_norm(utt, "stop") ||
       stt_contains_norm(utt, "turn off"))) {
    return LocalVoiceMediaCmd::PauseOrStop;
  }
  return LocalVoiceMediaCmd::None;
}

inline bool wake_word_should_run(std::string_view ww_mode,
                                 bool usb_on,
                                 bool va_busy,
                                 bool media_busy,
                                 bool alert_or_tool_busy,
                                 bool sys_busy) noexcept {
  const bool mode_allowed = (ww_mode == "Always On") ||
                            (ww_mode == "Docked Only" && usb_on);
  return mode_allowed && !va_busy && !media_busy && !alert_or_tool_busy && !sys_busy;
}

// Must match volume_min / volume_max of latter_media_player in modules/audio_voice.yaml.
inline constexpr float kMediaVolumeMin = 0.10f;
inline constexpr float kMediaVolumeMax = 1.00f;

inline float media_dac_volume(float player_volume) noexcept {
  if (std::isnan(player_volume)) player_volume = 0.60f;
  if (player_volume < 0.001f) return 0.0f;
  const float v = std::clamp(player_volume, 0.0f, 1.0f);
  return kMediaVolumeMin + v * (kMediaVolumeMax - kMediaVolumeMin);
}

inline float alarm_output_volume(float alert_vol_pct, float media_volume) noexcept {
  float a = alert_vol_pct;
  if (std::isnan(a) || a <= 0.0f) a = 80.0f;
  a = std::clamp(a, 20.0f, 100.0f);
  const float level = 0.90f - ((100.0f - a) * 0.18f * 2.0f / 255.0f);
  return std::clamp(std::max(level, media_dac_volume(media_volume)), 0.10f, 1.0f);
}

inline float voice_output_volume(float media_volume) noexcept {
  return std::clamp(media_dac_volume(media_volume) + (18.0f / 255.0f), 0.50f, 1.0f);
}

// ------------------------------------------------------------------------------
// AudioSessionController: VA/Music Play-Intent & 5-Minute Pause Visibility FSM
// ------------------------------------------------------------------------------
enum class AudioSessionPhase : uint8_t {
  Idle = 0,
  WakeWordArmed = 1,
  VaListening = 2,
  VaThinking = 3,
  VaSpeaking = 4,
  VaEchoCooldown = 5,
  MusicPlaying = 6,
  MusicPausedRecent = 7,
  AlertRinging = 8,
  DoorbellIntercom = 9,
};

enum class MusicPlayIntent : uint8_t {
  ExternalOrUserExplicit = 0,
  AutoResumeAfterVa = 1,
  HomeQuickToggle = 2,
  TrackAdvanceOrSeek = 3,
};

struct AudioSessionController {
  static constexpr uint32_t kRecentPausedMusicTimeoutMs = 300000U;
  static constexpr uint32_t kPlayIntentWindowMs = 10000U;

  void reset();
  void set_play_intent(MusicPlayIntent intent, uint32_t now_ms) noexcept;
  MusicPlayIntent active_play_intent(uint32_t now_ms) const noexcept;
  void note_local_playback_stopped() noexcept;

  bool should_auto_open_music_page(int current_gui_page,
                                   bool home_screen_enabled,
                                   int start_screen_page,
                                   bool alert_or_ringing,
                                   uint32_t now_ms,
                                   bool ota_or_hibernating = false) noexcept;

  void note_media_state_observation(bool is_effectively_playing,
                                    bool is_paused_state,
                                    std::string_view speaker_id,
                                    std::string_view area_id,
                                    uint32_t now_ms);

  void set_paused_for_va(bool v, uint32_t now_ms) noexcept;
  void clear_paused_music() noexcept;

  bool is_music_visible_on_home_activity(bool is_effectively_playing,
                                         bool is_paused_state,
                                         bool has_title,
                                         uint32_t now_ms) const noexcept;

  uint32_t paused_music_expiry_due_ms(uint32_t now_ms) const noexcept;
  void note_va_phase(AudioSessionPhase p) noexcept { phase_ = p; }
  void preempt_for_alert(uint32_t now_ms) noexcept;
  AudioSessionPhase phase() const noexcept { return phase_; }

 private:
  AudioSessionPhase phase_ = AudioSessionPhase::Idle;
  MusicPlayIntent play_intent_ = MusicPlayIntent::ExternalOrUserExplicit;
  uint32_t intent_set_ms_ = 0U;
  uint32_t last_live_pause_ms_ = 0U;
  std::string last_speaker_id_{};
  std::string last_area_id_{};
  bool initialized_ = false;
  bool was_playing_ = false;
  bool paused_for_va_ = false;
  bool session_active_playback_ = false;
};

AudioSessionController &audio_session() noexcept;

// ------------------------------------------------------------------------------
// AudioPathArbitrator: Deterministic Echo-Gate, Media Idle Debounce, Pause Settle
// & 6s Announcement Stall Watchdog (replaces mode: restart delay/wait_until YAML)
// ------------------------------------------------------------------------------
enum class EchoGateStage : uint8_t {
  Inactive = 0,
  DrainSources = 1,
  QuietSpeaker = 2,
  SettleUnmute = 3,
};

struct AudioArbitratorInputs {
  bool ann_has_buffered{false};
  bool snd_has_buffered{false};
  bool phys_spk_has_buffered{false};
  bool media_mixer_running{false};
  bool mp_announcing{false};
};

struct AudioArbitratorActions {
  bool mute_speaker_path{false};
  bool unmute_mic{false};
  bool run_media_idle_finish{false};
  bool log_announce_stall{false};
};

class AudioPathArbitrator {
 public:
  using HostDriverFn = void (*)(uint32_t now_ms);

  static constexpr uint32_t kDefaultMediaIdleDebounceMs = 3000U;
  static constexpr uint32_t kPauseSettleTimeoutMs = 500U;
  static constexpr uint32_t kAnnounceStallTimeoutMs = 6000U;
  static constexpr uint32_t kEchoQuietSpeakerTimeoutMs = 500U;
  static constexpr uint32_t kEchoQuietSilenceGapMs = 100U;
  static constexpr uint32_t kEchoSettleUnmuteMs = 30U;

  void reset() noexcept;
  void bind_host_driver(HostDriverFn fn) noexcept { host_driver_ = fn; }

  void arm_echo_gate(uint32_t now_ms, uint32_t source_timeout_ms = 1500U) noexcept;
  void cancel_echo_gate() noexcept;
  EchoGateStage echo_stage() const noexcept { return echo_stage_; }
  bool echo_gate_active() const noexcept { return echo_stage_ != EchoGateStage::Inactive; }

  void arm_media_idle_debounce(uint32_t now_ms,
                               uint32_t delay_ms = kDefaultMediaIdleDebounceMs) noexcept;
  void cancel_media_idle_debounce() noexcept;
  void arm_media_pause_settle(uint32_t now_ms) noexcept;
  bool media_idle_debounce_active() const noexcept {
    return idle_debounce_start_ms_ != 0U || pause_settle_start_ms_ != 0U;
  }

  void arm_announce_stall_check(uint32_t now_ms) noexcept;
  void cancel_announce_stall_check() noexcept;
  bool announce_stall_check_active() const noexcept { return stall_check_start_ms_ != 0U; }

  bool has_pending_work() const noexcept {
    return echo_stage_ != EchoGateStage::Inactive || idle_debounce_start_ms_ != 0U ||
           pause_settle_start_ms_ != 0U || stall_check_start_ms_ != 0U;
  }

  bool needs_fast_loop() const noexcept {
    return echo_stage_ != EchoGateStage::Inactive || pause_settle_start_ms_ != 0U;
  }

  AudioArbitratorActions step(uint32_t now_ms, const AudioArbitratorInputs &in) noexcept;

  void poll_loop(uint32_t now_ms) {
    if (has_pending_work() && host_driver_ != nullptr) {
      host_driver_(now_ms);
    }
  }

 private:
  static constexpr uint32_t non_zero_ms_(uint32_t t) noexcept { return (t == 0U) ? 1U : t; }

  HostDriverFn host_driver_{nullptr};
  EchoGateStage echo_stage_{EchoGateStage::Inactive};
  uint32_t echo_stage_start_ms_{0U};
  uint32_t echo_source_timeout_ms_{1500U};
  uint32_t idle_debounce_start_ms_{0U};
  uint32_t idle_debounce_delay_ms_{kDefaultMediaIdleDebounceMs};
  uint32_t pause_settle_start_ms_{0U};
  uint32_t stall_check_start_ms_{0U};
};

AudioPathArbitrator &audio_arbitrator() noexcept;

// ------------------------------------------------------------------------------
// IntercomSessionController: Deterministic Doorbell Call, Live Listen & PTT FSM
// ------------------------------------------------------------------------------
enum class IntercomPhase : uint8_t {
  Idle = 0,
  LiveListenOnly = 1,
  TwoWayListening = 2,
  TwoWayPttTx = 3,
};

class IntercomSessionController {
 public:
  using HostStopHwFn = void (*)(uint32_t now_ms);

  void reset() noexcept;
  void bind_host_stop_hw(HostStopHwFn fn) noexcept { host_stop_hw_ = fn; }

  IntercomPhase phase() const noexcept { return phase_; }

  void emit_current_cmd();
  void start_call(uint32_t now_ms);
  void stop_call(uint32_t now_ms);
  void stop_call_with_hw(uint32_t now_ms);
  bool resume_listen(uint32_t now_ms);
  bool begin_ptt(uint32_t now_ms, bool api_connected);
  // Returns true if stt_text was non-empty and talk_message was emitted; false if resumed listen.
  bool finish_ptt_stt(std::string_view stt_text, uint32_t now_ms);

 private:
  IntercomPhase phase_{IntercomPhase::Idle};
  HostStopHwFn host_stop_hw_{nullptr};
};

IntercomSessionController &intercom_session() noexcept;

// ------------------------------------------------------------------------------
// Compiled C++20 Media Transport, Browse & Speaker Group Actions
// ------------------------------------------------------------------------------
enum class LocalMediaHwAction : uint8_t {
  None = 0,
  PauseLocalPlayer = 1,
  StopLocalMww = 2,
};

// Applies volume step in direction dir (+1 / -1). When step_already_recorded is false,
// also updates remote_vol_pct() / action_feedback via step_music_volume_feedback.
// Returns new local player volume [0.10..1.00] when local, or -1.0f when remote RPC was emitted.
float media_step_volume(int dir,
                        float current_local_vol,
                        uint32_t now_ms,
                        bool step_already_recorded = false);

LocalMediaHwAction media_toggle_playback(bool local_mp_playing, uint32_t now_ms);
bool media_stop_playback(uint32_t now_ms);
bool media_clear_queue(uint32_t now_ms);
void media_next_track(uint32_t now_ms);
void media_prev_track(uint32_t now_ms);
void media_seek_relative(int dir, uint32_t now_ms);
void media_seek_absolute(uint32_t now_ms);
int media_fetch_browse(uint32_t now_ms);
bool media_play_slot(int slot_1based, uint32_t now_ms);

// Returns true when the newly selected speaker is remote (caller mutes local DAC & turns off speaker_enable).
bool speaker_activate_selected(bool transfer_queue,
                               bool local_mp_playing,
                               bool local_mp_paused,
                               uint32_t now_ms);
bool speaker_join_target(uint32_t now_ms);
void speaker_unjoin_target(std::string_view friendly_name, uint32_t now_ms);

#ifdef USE_I2C
void hal_set_es8311_es7210_standby(esphome::i2c::I2CBus *bus, bool standby, bool has_es7210 = true);
void hal_power_down_audio_and_imu_for_hibernate(esphome::i2c::I2CBus *bus, bool has_imu, bool has_es7210 = true);

template <typename I2cBusT>
inline void hal_set_es8311_es7210_standby(I2cBusT *const *bus_pp, bool standby, bool has_es7210 = true) {
  if (bus_pp != nullptr) hal_set_es8311_es7210_standby(*bus_pp, standby, has_es7210);
}

template <typename I2cBusT>
inline void hal_power_down_audio_and_imu_for_hibernate(I2cBusT *const *bus_pp, bool has_imu, bool has_es7210 = true) {
  if (bus_pp != nullptr) hal_power_down_audio_and_imu_for_hibernate(*bus_pp, has_imu, has_es7210);
}
#endif  // USE_I2C

}  // namespace latter_ui
