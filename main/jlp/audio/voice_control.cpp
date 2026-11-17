#include "voice_control.h"

#include "espos_audio/audio_driver.h"
#include "espos_voice/wyoming_satellite.h"

#include "esp_log.h"

#include "jlp/layout/store.h"

namespace jlp {

// NVS keys for the persisted panel-local audio state. Short: NVS keys cap
// at 15 chars.
static const char* kTag = "jlp.voice";

static constexpr const char* kSpeakerMutedKey = "spk_muted";
static constexpr const char* kMicMutedKey = "mic_muted";
static constexpr const char* kVolumeKey = "volume";

void VoiceControl::init(espos_voice::WyomingSatellite* sat,
                        espos_audio::AudioDriver* audio) {
  sat_ = sat;
  audio_ = audio;
  // Restore the persisted mute state. Defaults to unmuted on a fresh
  // device, so a first boot behaves as before; thereafter the helm keeps
  // whatever the last person set instead of re-arming audio by surprise.
  speaker_muted_.store(store_flag_get(kSpeakerMutedKey, false));
  mic_muted_.store(store_flag_get(kMicMutedKey, false));
  volume_.store(store_u8_get(kVolumeKey, volume_.load()));
  if (volume_.load() > 100) volume_.store(100);  // guard an out-of-range read
  if (audio_) {
    audio_->set_enabled(!speaker_muted_.load());
    // Push the restored level at the codec, otherwise the slider shows the
    // stored value while the amp still runs at the driver default.
    audio_->set_volume(volume_.load());
  }
  // Log what was restored: the state is otherwise invisible until someone
  // touches a switch, so a persistence failure looks identical to a fresh
  // device and would go unnoticed.
  ESP_LOGI(kTag, "restored audio: speaker=%s mic=%s volume=%u",
           speaker_muted_.load() ? "muted" : "on",
           mic_muted_.load() ? "muted" : "on", (unsigned)volume_.load());
}

bool VoiceControl::available() const {
  return sat_ && sat_->running() && sat_->client_connected();
}

void VoiceControl::set_ptt_held(bool held) {
  // A muted mic ignores a press so the privacy switch is honoured.
  if (held && mic_muted_) return;
  if (sat_) sat_->set_ptt_held(held);
}

int VoiceControl::state_code() const {
  if (!sat_) return 0;
  switch (sat_->state()) {
    case espos_voice::SatState::Idle:
      return 1;
    case espos_voice::SatState::Listening:
      return 2;
    case espos_voice::SatState::Speaking:
      return 3;
    case espos_voice::SatState::Disconnected:
    default:
      return 0;
  }
}

void VoiceControl::set_speaker_muted(bool muted) {
  if (!speaker_muted_.exchange(muted) && muted) ++speaker_mutes_;
  // set_enabled(false) holds the amp disabled so a quiet helm stays quiet;
  // set_enabled(true) re-arms it. Also mute the chime so a mute is total.
  if (audio_) audio_->set_enabled(!muted);
  // Persist: a panel that unmutes itself on every power cycle overrides
  // whoever silenced it, and a helm that beeps after a reboot is exactly
  // the surprise a mute switch exists to prevent.
  store_flag_set(kSpeakerMutedKey, muted);
}

void VoiceControl::set_volume(uint8_t pct, bool persist) {
  if (pct > 100) pct = 100;
  volume_.store(pct);
  if (audio_) audio_->set_volume(pct);
  // Persisted like the mute flags: a helm turned down before a reboot should
  // not come back at the driver default. Only on release, though — see the
  // header: a drag emits a value change per pixel.
  if (persist) store_u8_set(kVolumeKey, pct);
}

void VoiceControl::set_mic_muted(bool muted) {
  // Publish the muted state FIRST, then purge, so any concurrent /mic_probe
  // sees "muted" and the satellite's own probe kill switch (set by
  // wake_pcm_clear) blocks the snapshot regardless of timing — the two gates
  // together close the check-then-read window.
  mic_muted_.store(muted);
  if (muted && sat_) {
    sat_->set_ptt_held(false);
    sat_->wake_pcm_clear();  // sets the lock-free probe-disable kill switch
  }
  // Persist for the same reason as the speaker — a privacy switch that
  // silently re-opens the mic at boot is worse than one that never existed.
  store_flag_set(kMicMutedKey, muted);
}

VoiceControl& voice() {
  static VoiceControl v;
  return v;
}

}  // namespace jlp
