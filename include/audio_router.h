#pragma once

// ==============================================================================
// Latter Core Audio Router & Wake-Word Arbitrator (audio_router.h)
// ==============================================================================

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

// Zero-cost fast return outside an active VA reply echo-gate window.
inline void echo_meter_feed(const uint8_t *data, size_t len) {
  if (!echo_meter_active().load(std::memory_order_relaxed)) return;
  const size_t n = len / 2;
  if (n == 0) return;
  const int16_t *s = reinterpret_cast<const int16_t *>(data);
  int64_t acc = 0;
  for (size_t i = 0; i < n; i++) acc += static_cast<int32_t>(s[i]) * s[i];
  if (acc / static_cast<int64_t>(n) > kEchoLoudMeanSquare) {
    echo_last_loud_ms().store(hal_millis_now(), std::memory_order_relaxed);
  }
}

inline bool wake_word_should_run(const std::string &ww_mode,
                                 bool usb_on,
                                 bool va_busy,
                                 bool media_busy,
                                 bool alert_or_tool_busy,
                                 bool sys_busy) {
  const bool mode_allowed = (ww_mode == "Always On") ||
                            (ww_mode == "Docked Only" && usb_on);
  return mode_allowed && !va_busy && !media_busy && !alert_or_tool_busy && !sys_busy;
}

// Must match volume_min / volume_max of latter_media_player in modules/audio_voice.yaml.
inline constexpr float kMediaVolumeMin = 0.10f;
inline constexpr float kMediaVolumeMax = 1.00f;

inline float media_dac_volume(float player_volume) {
  if (std::isnan(player_volume)) player_volume = 0.60f;
  if (player_volume < 0.001f) return 0.0f;
  const float v = std::clamp(player_volume, 0.0f, 1.0f);
  return kMediaVolumeMin + v * (kMediaVolumeMax - kMediaVolumeMin);
}

inline float alarm_output_volume(float alert_vol_pct, float media_volume) {
  float a = alert_vol_pct;
  if (std::isnan(a) || a <= 0.0f) a = 80.0f;
  a = std::clamp(a, 20.0f, 100.0f);
  const float level = 0.90f - ((100.0f - a) * 0.18f * 2.0f / 255.0f);
  return std::clamp(std::max(level, media_dac_volume(media_volume)), 0.10f, 1.0f);
}

inline float voice_output_volume(float media_volume) {
  return std::clamp(media_dac_volume(media_volume) + (18.0f / 255.0f), 0.50f, 1.0f);
}

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

  void reset() {
    *this = AudioSessionController{};
  }

  void set_play_intent(MusicPlayIntent intent, uint32_t now_ms) {
    play_intent_ = intent;
    intent_set_ms_ = (now_ms == 0U) ? 1U : now_ms;
  }

  MusicPlayIntent active_play_intent(uint32_t now_ms) const {
    if (intent_set_ms_ != 0U &&
        static_cast<uint32_t>(now_ms - intent_set_ms_) <= kPlayIntentWindowMs) {
      return play_intent_;
    }
    return MusicPlayIntent::ExternalOrUserExplicit;
  }

  void note_local_playback_stopped() {
    session_active_playback_ = false;
  }

  bool should_auto_open_music_page(int current_gui_page,
                                   bool home_screen_enabled,
                                   int start_screen_page,
                                   bool alert_or_ringing,
                                   uint32_t now_ms,
                                   bool ota_or_hibernating = false) {
    const MusicPlayIntent eff_intent = active_play_intent(now_ms);
    intent_set_ms_ = 0U;
    play_intent_ = MusicPlayIntent::ExternalOrUserExplicit;

    const bool was_continuous_session = session_active_playback_;
    session_active_playback_ = true;
    phase_ = AudioSessionPhase::MusicPlaying;

    if (alert_or_ringing || ota_or_hibernating) return false;
    if (!home_screen_enabled && start_screen_page != 2) return false;
    if (eff_intent != MusicPlayIntent::ExternalOrUserExplicit) return false;
    if (was_continuous_session && current_gui_page != 2) return false;
    return true;
  }

  void note_media_state_observation(bool is_effectively_playing,
                                    bool is_paused_state,
                                    std::string_view speaker_id,
                                    std::string_view area_id,
                                    uint32_t now_ms) {
    if (!initialized_ || speaker_id != last_speaker_id_ ||
        (!area_id.empty() && !last_area_id_.empty() && area_id != last_area_id_)) {
      initialized_ = true;
      last_speaker_id_ = std::string(speaker_id);
      if (!area_id.empty()) last_area_id_ = std::string(area_id);
      if (!is_effectively_playing) {
        last_live_pause_ms_ = 0U;
        was_playing_ = false;
        if (phase_ == AudioSessionPhase::MusicPlaying ||
            phase_ == AudioSessionPhase::MusicPausedRecent) {
          phase_ = AudioSessionPhase::Idle;
        }
      } else {
        was_playing_ = true;
        last_live_pause_ms_ = 0U;
        phase_ = AudioSessionPhase::MusicPlaying;
      }
      return;
    }

    if (!area_id.empty()) last_area_id_ = std::string(area_id);

    if (is_effectively_playing) {
      was_playing_ = true;
      last_live_pause_ms_ = 0U;
      if (phase_ == AudioSessionPhase::Idle ||
          phase_ == AudioSessionPhase::MusicPausedRecent) {
        phase_ = AudioSessionPhase::MusicPlaying;
      }
    } else if (is_paused_state) {
      if (was_playing_) {
        last_live_pause_ms_ = (now_ms == 0U) ? 1U : now_ms;
        was_playing_ = false;
      }
      if (last_live_pause_ms_ != 0U && phase_ == AudioSessionPhase::MusicPlaying) {
        phase_ = AudioSessionPhase::MusicPausedRecent;
      }
    } else {
      if (!paused_for_va_) {
        was_playing_ = false;
        last_live_pause_ms_ = 0U;
        if (phase_ == AudioSessionPhase::MusicPlaying ||
            phase_ == AudioSessionPhase::MusicPausedRecent) {
          phase_ = AudioSessionPhase::Idle;
        }
      }
    }
  }

  void set_paused_for_va(bool v, uint32_t now_ms) {
    paused_for_va_ = v;
    if (v && (was_playing_ || last_live_pause_ms_ != 0U)) {
      last_live_pause_ms_ = (now_ms == 0U) ? 1U : now_ms;
      was_playing_ = false;
    }
  }

  void clear_paused_music() {
    was_playing_ = false;
    last_live_pause_ms_ = 0U;
    paused_for_va_ = false;
    if (phase_ == AudioSessionPhase::MusicPausedRecent) {
      phase_ = AudioSessionPhase::Idle;
    }
  }

  bool is_music_visible_on_home_activity(bool is_effectively_playing,
                                         bool is_paused_state,
                                         bool has_title,
                                         uint32_t now_ms) const {
    if (!has_title) return false;
    if (is_effectively_playing) return true;
    if (paused_for_va_) return true;
    if (is_paused_state && last_live_pause_ms_ != 0U) {
      return static_cast<uint32_t>(now_ms - last_live_pause_ms_) < kRecentPausedMusicTimeoutMs;
    }
    return false;
  }

  uint32_t paused_music_expiry_due_ms(uint32_t now_ms) const {
    if (paused_for_va_ || last_live_pause_ms_ == 0U) return 0U;
    const uint32_t el = static_cast<uint32_t>(now_ms - last_live_pause_ms_);
    return (el < kRecentPausedMusicTimeoutMs) ? (kRecentPausedMusicTimeoutMs - el) : 0U;
  }

  void note_va_phase(AudioSessionPhase p) {
    phase_ = p;
  }

  void preempt_for_alert(uint32_t now_ms) {
    (void)now_ms;
    phase_ = AudioSessionPhase::AlertRinging;
  }

  AudioSessionPhase phase() const {
    return phase_;
  }

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

inline AudioSessionController &audio_session() {
  static AudioSessionController ctrl{};
  return ctrl;
}

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
