#if __has_include("audio_router.h")
#include "audio_router.h"
#include "hal_power_pm.h"
#include "latter_state.h"
#include "latter_rpc.h"
#include "latter_actions.h"
#else
#include "../../include/audio_router.h"
#include "../../include/hal_power_pm.h"
#include "../../include/latter_state.h"
#include "../../include/latter_rpc.h"
#include "../../include/latter_actions.h"
#endif

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#if __has_include("esphome/core/log.h")
#include "esphome/core/log.h"
#endif

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace latter_ui {

// ==============================================================================
// 1. AudioSessionController
// ==============================================================================

void AudioSessionController::reset() {
  *this = AudioSessionController{};
}

void AudioSessionController::set_play_intent(MusicPlayIntent intent, uint32_t now_ms) noexcept {
  play_intent_ = intent;
  intent_set_ms_ = (now_ms == 0U) ? 1U : now_ms;
}

MusicPlayIntent AudioSessionController::active_play_intent(uint32_t now_ms) const noexcept {
  if (intent_set_ms_ != 0U &&
      static_cast<uint32_t>(now_ms - intent_set_ms_) <= kPlayIntentWindowMs) {
    return play_intent_;
  }
  return MusicPlayIntent::ExternalOrUserExplicit;
}

void AudioSessionController::note_local_playback_stopped() noexcept {
  session_active_playback_ = false;
}

bool AudioSessionController::should_auto_open_music_page(int current_gui_page,
                                                         bool home_screen_enabled,
                                                         int start_screen_page,
                                                         bool alert_or_ringing,
                                                         uint32_t now_ms,
                                                         bool ota_or_hibernating) noexcept {
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

void AudioSessionController::note_media_state_observation(bool is_effectively_playing,
                                                          bool is_paused_state,
                                                          std::string_view speaker_id,
                                                          std::string_view area_id,
                                                          uint32_t now_ms) {
  if (!initialized_ || speaker_id != last_speaker_id_ ||
      (!area_id.empty() && !last_area_id_.empty() && area_id != last_area_id_)) {
    initialized_ = true;
    last_speaker_id_.assign(speaker_id.data(), speaker_id.size());
    if (!area_id.empty()) last_area_id_.assign(area_id.data(), area_id.size());
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

  if (!area_id.empty()) last_area_id_.assign(area_id.data(), area_id.size());

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

void AudioSessionController::set_paused_for_va(bool v, uint32_t now_ms) noexcept {
  paused_for_va_ = v;
  if (v && (was_playing_ || last_live_pause_ms_ != 0U)) {
    last_live_pause_ms_ = (now_ms == 0U) ? 1U : now_ms;
    was_playing_ = false;
  }
}

void AudioSessionController::clear_paused_music() noexcept {
  was_playing_ = false;
  last_live_pause_ms_ = 0U;
  paused_for_va_ = false;
  if (phase_ == AudioSessionPhase::MusicPausedRecent) {
    phase_ = AudioSessionPhase::Idle;
  }
}

bool AudioSessionController::is_music_visible_on_home_activity(bool is_effectively_playing,
                                                               bool is_paused_state,
                                                               bool has_title,
                                                               uint32_t now_ms) const noexcept {
  if (!has_title) return false;
  if (is_effectively_playing) return true;
  if (paused_for_va_) return true;
  if (is_paused_state && last_live_pause_ms_ != 0U) {
    return static_cast<uint32_t>(now_ms - last_live_pause_ms_) < kRecentPausedMusicTimeoutMs;
  }
  return false;
}

uint32_t AudioSessionController::paused_music_expiry_due_ms(uint32_t now_ms) const noexcept {
  if (paused_for_va_ || last_live_pause_ms_ == 0U) return 0U;
  const uint32_t el = static_cast<uint32_t>(now_ms - last_live_pause_ms_);
  return (el < kRecentPausedMusicTimeoutMs) ? (kRecentPausedMusicTimeoutMs - el) : 0U;
}

void AudioSessionController::preempt_for_alert(uint32_t now_ms) noexcept {
  (void)now_ms;
  phase_ = AudioSessionPhase::AlertRinging;
}

AudioSessionController &audio_session() noexcept {
  static AudioSessionController ctrl{};
  return ctrl;
}

// ==============================================================================
// 2. AudioPathArbitrator
// ==============================================================================

void AudioPathArbitrator::reset() noexcept {
  const HostDriverFn saved_driver = host_driver_;
  *this = AudioPathArbitrator{};
  host_driver_ = saved_driver;
  echo_meter_active().store(false, std::memory_order_relaxed);
}

void AudioPathArbitrator::arm_echo_gate(uint32_t now_ms, uint32_t source_timeout_ms) noexcept {
  const uint32_t ts = non_zero_ms_(now_ms);
  echo_stage_ = EchoGateStage::DrainSources;
  echo_stage_start_ms_ = ts;
  echo_source_timeout_ms_ = (source_timeout_ms == 0U) ? 1500U : source_timeout_ms;
  echo_last_loud_ms().store(now_ms, std::memory_order_relaxed);
  echo_meter_active().store(true, std::memory_order_relaxed);
  audio_session().note_va_phase(AudioSessionPhase::VaEchoCooldown);
}

void AudioPathArbitrator::cancel_echo_gate() noexcept {
  echo_stage_ = EchoGateStage::Inactive;
  echo_stage_start_ms_ = 0U;
  echo_meter_active().store(false, std::memory_order_relaxed);
}

void AudioPathArbitrator::arm_media_idle_debounce(uint32_t now_ms, uint32_t delay_ms) noexcept {
  idle_debounce_start_ms_ = non_zero_ms_(now_ms);
  idle_debounce_delay_ms_ = (delay_ms == 0U) ? kDefaultMediaIdleDebounceMs : delay_ms;
}

void AudioPathArbitrator::cancel_media_idle_debounce() noexcept {
  idle_debounce_start_ms_ = 0U;
  pause_settle_start_ms_ = 0U;
}

void AudioPathArbitrator::arm_media_pause_settle(uint32_t now_ms) noexcept {
  idle_debounce_start_ms_ = 0U;
  pause_settle_start_ms_ = non_zero_ms_(now_ms);
}

void AudioPathArbitrator::arm_announce_stall_check(uint32_t now_ms) noexcept {
  stall_check_start_ms_ = non_zero_ms_(now_ms);
}

void AudioPathArbitrator::cancel_announce_stall_check() noexcept {
  stall_check_start_ms_ = 0U;
}

AudioArbitratorActions AudioPathArbitrator::step(uint32_t now_ms,
                                                 const AudioArbitratorInputs &in) noexcept {
  AudioArbitratorActions out{};

  // 1. Acoustic Echo Gate multi-stage drain -> speaker mute -> 30ms settle -> mic unmute
  if (echo_stage_ == EchoGateStage::DrainSources) {
    const uint32_t elapsed = static_cast<uint32_t>(now_ms - echo_stage_start_ms_);
    if ((!in.ann_has_buffered && !in.snd_has_buffered) || elapsed >= echo_source_timeout_ms_) {
      echo_stage_ = EchoGateStage::QuietSpeaker;
      echo_stage_start_ms_ = non_zero_ms_(now_ms);
    }
  }
  if (echo_stage_ == EchoGateStage::QuietSpeaker) {
    const uint32_t elapsed = static_cast<uint32_t>(now_ms - echo_stage_start_ms_);
    const uint32_t last_loud = echo_last_loud_ms().load(std::memory_order_relaxed);
    const uint32_t quiet_ms = (now_ms >= last_loud) ? (now_ms - last_loud) : 0U;
    if (!in.phys_spk_has_buffered ||
        quiet_ms > kEchoQuietSilenceGapMs ||
        elapsed >= kEchoQuietSpeakerTimeoutMs) {
      out.mute_speaker_path = true;
      echo_stage_ = EchoGateStage::SettleUnmute;
      echo_stage_start_ms_ = non_zero_ms_(now_ms);
    }
  } else if (echo_stage_ == EchoGateStage::SettleUnmute) {
    const uint32_t elapsed = static_cast<uint32_t>(now_ms - echo_stage_start_ms_);
    if (elapsed >= kEchoSettleUnmuteMs) {
      echo_stage_ = EchoGateStage::Inactive;
      echo_stage_start_ms_ = 0U;
      echo_meter_active().store(false, std::memory_order_relaxed);
      out.unmute_mic = true;
    }
  }

  // 2. Media Pause Settle (<=500ms mixer drain -> media_idle_finish)
  if (pause_settle_start_ms_ != 0U) {
    const uint32_t elapsed = static_cast<uint32_t>(now_ms - pause_settle_start_ms_);
    if (!in.media_mixer_running || elapsed >= kPauseSettleTimeoutMs) {
      pause_settle_start_ms_ = 0U;
      out.run_media_idle_finish = true;
    }
  }

  // 3. Media Idle Debounce (3000ms -> media_idle_finish)
  if (idle_debounce_start_ms_ != 0U) {
    const uint32_t elapsed = static_cast<uint32_t>(now_ms - idle_debounce_start_ms_);
    if (elapsed >= idle_debounce_delay_ms_) {
      idle_debounce_start_ms_ = 0U;
      out.run_media_idle_finish = true;
    }
  }

  // 4. Announcement 6s Stall Watchdog ([PROF:AUDIO_STALL])
  if (stall_check_start_ms_ != 0U) {
    const uint32_t elapsed = static_cast<uint32_t>(now_ms - stall_check_start_ms_);
    if (elapsed >= kAnnounceStallTimeoutMs) {
      stall_check_start_ms_ = 0U;
      if (in.mp_announcing && !in.ann_has_buffered) {
        out.log_announce_stall = true;
      }
    }
  }

  return out;
}

AudioPathArbitrator &audio_arbitrator() noexcept {
  static AudioPathArbitrator arb{};
  return arb;
}

// ==============================================================================
// 3. IntercomSessionController
// ==============================================================================

void IntercomSessionController::reset() noexcept {
  const HostStopHwFn saved_stop = host_stop_hw_;
  *this = IntercomSessionController{};
  host_stop_hw_ = saved_stop;
}

void IntercomSessionController::emit_current_cmd() {
  auto &ic = runtime_state().intercom;
  const auto &ac = area_ctx_state();
  const std::string cam_ent = resolve_active_carousel_camera_id(
      ac.cameras_summary, ac.camera_snapshot_url);
  latter_actions::emit_doorbell_intercom(
      ic.cam_action, cam_ent, cam_carousel_idx(),
      ic.cam_message, ic.cam_noise_reduction, ic.cam_code);
  ic.cam_code.clear();
}

void IntercomSessionController::start_call(uint32_t now_ms) {
  auto &ic = runtime_state().intercom;
  phase_ = IntercomPhase::TwoWayListening;
  cam_call_active() = true;
  cam_listen_active() = true;
  cam_intercom_ptt_active() = false;
  cam_intercom_status() = "DOORBELL CALL";
  ic.cam_status_ms = now_ms;
  ic.cam_action = "listen_start";
  emit_current_cmd();
}

void IntercomSessionController::stop_call(uint32_t now_ms) {
  auto &rs = runtime_state();
  phase_ = IntercomPhase::Idle;
  cam_call_active() = false;
  cam_intercom_ptt_active() = false;
  cam_listen_active() = false;
  rs.voice.intercom_turn = false;
  audio_arbitrator().cancel_echo_gate();
  cam_intercom_status() = "CALL ENDED";
  rs.intercom.cam_status_ms = now_ms;
  rs.intercom.cam_action = "listen_stop";
  emit_current_cmd();
  audio_arbitrator().arm_media_idle_debounce(now_ms);
}

void IntercomSessionController::stop_call_with_hw(uint32_t now_ms) {
  stop_call(now_ms);
  if (host_stop_hw_ != nullptr) {
    host_stop_hw_(now_ms);
  }
}

bool IntercomSessionController::resume_listen(uint32_t now_ms) {
  cam_intercom_ptt_active() = false;
  radio_boost_until_ms() = 0;
  if (cam_call_active()) {
    phase_ = IntercomPhase::TwoWayListening;
    auto &ic = runtime_state().intercom;
    cam_listen_active() = true;
    cam_intercom_status() = "DOORBELL CALL";
    ic.cam_status_ms = now_ms;
    ic.cam_action = "listen_start";
    emit_current_cmd();
    return true;
  }
  phase_ = cam_listen_active() ? IntercomPhase::LiveListenOnly : IntercomPhase::Idle;
  return false;
}

bool IntercomSessionController::begin_ptt(uint32_t now_ms, bool api_connected) {
  auto &ic = runtime_state().intercom;
  if (!api_connected) {
    cam_intercom_status() = "NOT CONNECTED";
    ic.cam_status_ms = now_ms;
    return false;
  }
  audio_arbitrator().cancel_echo_gate();
  audio_arbitrator().cancel_announce_stall_check();
  phase_ = IntercomPhase::TwoWayPttTx;
  cam_intercom_ptt_active() = true;
  cam_intercom_status() = "CALL • SPEAK NOW";
  ic.cam_status_ms = now_ms;
  audio_session().note_va_phase(AudioSessionPhase::DoorbellIntercom);
  return true;
}

bool IntercomSessionController::finish_ptt_stt(std::string_view stt_text, uint32_t now_ms) {
  auto &rs = runtime_state();
  audio_arbitrator().cancel_echo_gate();
  rs.voice.intercom_turn = true;
  rs.voice.active = false;
  rs.voice.status_text.clear();
  if (stt_text.empty()) {
    resume_listen(now_ms);
    return false;
  }
  cam_intercom_ptt_active() = false;
  phase_ = cam_call_active() ? IntercomPhase::TwoWayListening : IntercomPhase::Idle;
  rs.intercom.cam_action = "talk_message";
  rs.intercom.cam_message.assign(stt_text.data(), stt_text.size());
  cam_intercom_status() = "SENT TO DOORBELL";
  rs.intercom.cam_status_ms = now_ms;
  emit_current_cmd();
  radio_boost_until_ms() = 0;
  return true;
}

IntercomSessionController &intercom_session() noexcept {
  static IntercomSessionController ctrl{};
  return ctrl;
}

// ==============================================================================
// 4. Compiled Media Transport, Browse & Speaker Actions
// ==============================================================================

float media_step_volume(int dir,
                        float current_local_vol,
                        uint32_t now_ms,
                        bool step_already_recorded) {
  auto &m = runtime_state().music;
  const auto &ms = media_state();
  const float step_pct = static_cast<float>(persisted_prefs().volume_step_size);
  const int step_dir = (dir >= 0) ? 1 : -1;

  if (!step_already_recorded) {
    step_music_volume_feedback(step_dir, step_pct, ms.is_local, current_local_vol, now_ms, m);
  }

  if (ms.is_local) {
    float step = step_pct / 100.0f;
    if (std::isnan(step) || step <= 0.0f) step = 0.05f;
    last_local_vol_tap_ms() = now_ms;
    float cur = current_local_vol;
    if (std::isnan(cur) || cur <= 0.0f) cur = 0.70f;
    const float new_vol = (step_dir > 0) ? std::min(1.00f, cur + step)
                                         : std::max(0.10f, cur - step);
    m.action_feedback =
        "Volume: " + std::to_string(static_cast<int>(std::lround(new_vol * 100.0f))) + "%";
    m.feedback_timer = now_ms;
    return new_vol;
  }

  latter_actions::emit_media_volume(
      m.active_speaker_entity, (step_dir > 0) ? "up" : "down", remote_vol_pct());
  return -1.0f;
}

LocalMediaHwAction media_toggle_playback(bool local_mp_playing, uint32_t now_ms) {
  auto &ms = media_state();
  const bool is_remote = !ms.is_local;
  if (is_music_playing_effective(is_remote, local_mp_playing, ms.state, now_ms)) {
    set_remote_play_override(false, now_ms);
    if (!ms.title.empty()) {
      ms.state = "paused";
    }
    sync_audio_session_media_observation(false, true, now_ms);
    latter_actions::emit_media_command("pause");
    return ms.is_local ? LocalMediaHwAction::PauseLocalPlayer : LocalMediaHwAction::None;
  }

  set_remote_play_override(true, now_ms);
  latter_actions::emit_media_command("play");
  return ms.is_local ? LocalMediaHwAction::StopLocalMww : LocalMediaHwAction::None;
}

bool media_stop_playback(uint32_t now_ms) {
  auto &m = runtime_state().music;
  auto &ms = media_state();
  set_remote_play_override(false, now_ms);
  if (!ms.title.empty()) {
    ms.state = "paused";
  }
  sync_audio_session_media_observation(false, true, now_ms);
  m.action_feedback = "Playback Stopped";
  m.feedback_timer = now_ms;
  m.overlay_mode = 0;
  m.hud_active = true;
  m.hud_timer = now_ms;
  latter_actions::emit_media_command("pause");
  return ms.is_local;
}

bool media_clear_queue(uint32_t now_ms) {
  auto &m = runtime_state().music;
  auto &ms = media_state();
  const bool was_local = ms.is_local;
  set_remote_play_override(false, now_ms);
  audio_session().clear_paused_music();
  audio_session().note_local_playback_stopped();
  music_queue_cleared() = true;
  ms.title.clear();
  ms.artist.clear();
  ms.album.clear();
  ms.state = "idle";
  m.pos_base = 0.0f;
  m.pos_sync_time = 0U;
  m.loaded_art_url.clear();
  m.action_feedback = "Queue Cleared";
  m.feedback_timer = now_ms;
  m.overlay_mode = 0;
  m.hud_active = true;
  m.hud_timer = now_ms;
  latter_actions::emit_media_command("clear_queue");
  return was_local;
}

void media_next_track(uint32_t now_ms) {
  auto &m = runtime_state().music;
  audio_session().set_play_intent(MusicPlayIntent::TrackAdvanceOrSeek, now_ms);
  m.action_feedback = "Next Track >>";
  m.feedback_timer = now_ms;
  m.pos_base = 0.0f;
  m.pos_sync_time = now_ms;
  latter_actions::emit_media_command("next");
}

void media_prev_track(uint32_t now_ms) {
  auto &m = runtime_state().music;
  audio_session().set_play_intent(MusicPlayIntent::TrackAdvanceOrSeek, now_ms);
  m.action_feedback = "<< Prev Track";
  m.feedback_timer = now_ms;
  m.pos_base = 0.0f;
  m.pos_sync_time = now_ms;
  latter_actions::emit_media_command("previous");
}

void media_seek_relative(int dir, uint32_t now_ms) {
  auto &m = runtime_state().music;
  audio_session().set_play_intent(MusicPlayIntent::TrackAdvanceOrSeek, now_ms);
  if (now_ms - m.skip_last_tap_ms < 1000U) {
    m.skip_burst_count++;
  } else {
    m.skip_burst_count = 1;
  }
  m.skip_last_tap_ms = now_ms;

  float step_sec = 10.0f;
  const char *label = "-10s Replay";
  if (dir < 0) {
    switch (m.skip_burst_count) {
      case 1: step_sec = 10.0f; label = "-10s Replay"; break;
      case 2: step_sec = 30.0f; label = "-30s Replay"; break;
      case 3: step_sec = 60.0f; label = "-1m Replay"; break;
      case 4: step_sec = 120.0f; label = "-2m Replay"; break;
      default: step_sec = 300.0f; label = "-5m Replay"; break;
    }
  } else {
    switch (m.skip_burst_count) {
      case 1: step_sec = 30.0f; label = "+30s Skip"; break;
      case 2: step_sec = 60.0f; label = "+1m Skip"; break;
      case 3: step_sec = 120.0f; label = "+2m Skip"; break;
      case 4: step_sec = 300.0f; label = "+5m Skip"; break;
      default: step_sec = 600.0f; label = "+10m Skip"; break;
    }
  }

  m.action_feedback = label;
  m.feedback_timer = now_ms;

  float cur_pos = m.pos_base;
  if (m.pos_sync_time > 0U) {
    cur_pos += static_cast<float>(now_ms - m.pos_sync_time) / 1000.0f;
  }
  float new_pos = 0.0f;
  if (dir < 0) {
    new_pos = std::max(0.0f, cur_pos - step_sec);
  } else {
    const float dur = media_state().duration;
    new_pos = (!std::isnan(dur) && dur > 0.0f) ? std::min(dur, cur_pos + step_sec)
                                               : (cur_pos + step_sec);
  }
  m.pos_base = new_pos;
  m.pos_sync_time = now_ms;
  latter_actions::emit_media_seek(static_cast<int>(std::max(0.0f, new_pos)));
}

void media_seek_absolute(uint32_t now_ms) {
  audio_session().set_play_intent(MusicPlayIntent::TrackAdvanceOrSeek, now_ms);
  latter_actions::emit_media_seek(
      static_cast<int>(std::max(0.0f, runtime_state().music.pos_base)));
}

int media_fetch_browse(uint32_t now_ms) {
  const auto &rs = runtime_state();
  const std::string &mode_s = rs.music.selected_category_mode;
  const int page = rs.slot_page_for(latter_rpc::SlotChannel::MUSIC);
  std::string_view search_sv{};
  if (mode_s == "artist_albums") {
    search_sv = rs.music.drilldown_artist_name;
  } else if (mode_s == "current_album_tracks" || mode_s == "album_tracks" ||
             mode_s == "podcast_episodes" || mode_s == "lyrics" || mode_s == "bio") {
    search_sv = rs.music.drilldown_album_id;
  }
  const std::string_view alb_opt(pref_album_sort_order_cstr());
  const char *alb_sort =
      (alb_opt == "Newest on Top") ? "year_desc" : ((alb_opt == "Oldest on Top") ? "year" : "name");
  const std::string_view art_opt(pref_artist_album_sort_order_cstr());
  const char *art_sort =
      (art_opt == "Alphabetical") ? "name" : ((art_opt == "Newest on Top") ? "year_desc" : "year");
  const std::string_view pod_opt(pref_podcast_sort_order_cstr());
  const char *pod_sort = (pod_opt == "Oldest on Top") ? "oldest" : "newest";
  return latter_actions::emit_music_browse(
      mode_s, page, search_sv, rs.music.active_speaker_entity,
      media_state().last_speaker, alb_sort, art_sort, pod_sort, LATTER_SLOT_COUNT, now_ms);
}

bool media_play_slot(int slot_1based, uint32_t now_ms) {
  auto &rs = runtime_state();
  auto &m = rs.music;
  auto &ms = media_state();
  const int idx = std::clamp(slot_1based - 1, 0, 4);
  const auto &item = rs.music_slots()[idx];
  m.selected_slot_name = item.name;
  m.selected_slot_id = item.id;
  m.selected_slot_type = item.type;

  if (m.active_speaker_entity.empty()) {
    m.active_speaker_entity = ms.local_speaker;
    ms.is_local = !ms.local_speaker.empty();
  }
  if (m.selected_category_mode == "artist") {
    m.selected_slot_type = "artist";
  } else if (m.selected_category_mode == "album" || m.selected_category_mode == "artist_albums") {
    m.selected_slot_type = "album";
  } else if (m.selected_category_mode == "podcast_episodes") {
    m.selected_slot_type = "podcast_episode";
  } else if (m.selected_slot_type.empty()) {
    m.selected_slot_type = "music";
  }

  m.action_feedback = "Playing: " + m.selected_slot_name;
  m.feedback_timer = now_ms;
  audio_session().set_play_intent(MusicPlayIntent::ExternalOrUserExplicit, now_ms);
  set_remote_play_override(true, now_ms);
  if (m.selected_category_mode == "podcast_episodes") {
    m.active_playback_media_type = "podcast";
  } else if (m.selected_category_mode == "audiobook") {
    m.active_playback_media_type = "audiobook";
  } else {
    m.active_playback_media_type = "music";
  }
  m.overlay_mode = 0;
  m.hud_active = true;
  m.hud_timer = now_ms;
  latter_actions::emit_media_play_item(
      m.active_speaker_entity, m.selected_slot_id, m.selected_slot_type, "replace");
  return ms.is_local;
}

bool speaker_activate_selected(bool transfer_queue,
                               bool local_mp_playing,
                               bool local_mp_paused,
                               uint32_t now_ms) {
  auto &m = runtime_state().music;
  auto &ms = media_state();
  const bool was_playing =
      is_music_playing_effective(!ms.is_local, local_mp_playing, ms.state, now_ms);
  const char *prefix = transfer_queue ? "Transferring -> " : "Controlling: ";
  if (m.target_speaker_id.rfind("media_player.", 0) == 0) {
    if (m.active_speaker_entity != m.target_speaker_id && !m.active_speaker_entity.empty()) {
      ms.last_speaker = m.active_speaker_entity;
    }
    m.active_speaker_entity = m.target_speaker_id;
    m.active_speaker_name = m.target_speaker_name;
    m.action_feedback = std::string(prefix) + m.target_speaker_name;
  } else {
#ifdef ESP_LOGW
    ESP_LOGW("ui", "Rejecting non-media_player target: %s", m.target_speaker_id.c_str());
#endif
    m.action_feedback = "Invalid Speaker";
  }
  m.feedback_timer = now_ms;
  const std::string &loc_spk = ms.local_speaker;
  const bool new_is_local = (!loc_spk.empty() && m.active_speaker_entity == loc_spk);
  ms.active_speaker = m.active_speaker_entity;
  ms.speaker_name = m.active_speaker_name;
  ms.is_local = new_is_local;
  if (transfer_queue && was_playing && !new_is_local) {
    ms.state = "playing";
    set_remote_play_override(true, now_ms);
  } else {
    clear_remote_play_override();
  }
  sync_audio_session_media_observation(local_mp_playing && new_is_local, local_mp_paused, now_ms);
  m.overlay_mode = 0;
  m.direct_nav = false;
  m.hud_active = true;
  m.hud_timer = now_ms;
  latter_actions::emit_set_active_speaker(m.active_speaker_entity, transfer_queue);
  return !new_is_local;
}

bool speaker_join_target(uint32_t now_ms) {
  auto &m = runtime_state().music;
  m.action_feedback = "Joining -> " + m.target_speaker_name;
  m.feedback_timer = now_ms;
  m.overlay_mode = 0;
  m.hud_active = true;
  m.hud_timer = now_ms;
  latter_actions::emit_speaker_join(m.active_speaker_entity, m.target_speaker_id);
  return media_state().is_local;
}

void speaker_unjoin_target(std::string_view friendly_name, uint32_t now_ms) {
  auto &m = runtime_state().music;
  auto &ms = media_state();
  m.action_feedback = "Unjoining -> " + m.target_speaker_name;
  m.feedback_timer = now_ms;
  latter_actions::emit_speaker_unjoin(m.target_speaker_id);
  const std::string &loc_spk = ms.local_speaker;
  if (m.target_speaker_id == m.active_speaker_entity ||
      (!loc_spk.empty() && m.target_speaker_id == loc_spk)) {
    m.active_speaker_entity = loc_spk;
    m.active_speaker_name.assign(friendly_name.data(), friendly_name.size());
    ms.is_local = !loc_spk.empty();
    m.loaded_art_url.clear();
  }
  m.overlay_mode = 0;
  latter_actions::emit_set_active_speaker(m.active_speaker_entity, false);
}

}  // namespace latter_ui
