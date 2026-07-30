#include "voice_control.h"

#include "sensesp_cockpit_display/hal/audio_driver.h"
#include "sensesp_wyoming_satellite/wyoming_satellite.h"

namespace jlp {

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
    case sensesp_wyoming::SatState::Idle:
      return 1;
    case sensesp_wyoming::SatState::Listening:
      return 2;
    case sensesp_wyoming::SatState::Speaking:
      return 3;
    case sensesp_wyoming::SatState::Disconnected:
    default:
      return 0;
  }
}

void VoiceControl::set_speaker_muted(bool muted) {
  speaker_muted_ = muted;
  // set_enabled(false) holds the amp disabled so a quiet helm stays quiet;
  // set_enabled(true) re-arms it. Also mute the chime so a mute is total.
  if (audio_) audio_->set_enabled(!muted);
}

void VoiceControl::set_volume(uint8_t pct) {
  if (pct > 100) pct = 100;
  volume_ = pct;
  if (audio_) audio_->set_volume(pct);
}

void VoiceControl::set_mic_muted(bool muted) {
  mic_muted_ = muted;
  // If we're muted mid-utterance, drop the current PTT hold immediately.
  if (muted && sat_) sat_->set_ptt_held(false);
}

VoiceControl& voice() {
  static VoiceControl v;
  return v;
}

}  // namespace jlp
