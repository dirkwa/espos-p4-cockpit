#pragma once

// Process-lifetime hub for the panel's voice + audio controls, so JLP widgets
// (mic button, mute/volume) can drive them without each widget depending on
// the satellite / audio-driver types. Mirrors chime().

#include <atomic>
#include <cstdint>

namespace sensesp_wyoming {
class WyomingSatellite;
}
namespace sensesp_cockpit_display {
class AudioDriver;
}

namespace jlp {

class VoiceControl {
 public:
  void init(sensesp_wyoming::WyomingSatellite* sat,
            sensesp_cockpit_display::AudioDriver* audio) {
    sat_ = sat;
    audio_ = audio;
  }

  // --- Voice (Wyoming satellite) ---

  // True if a satellite is wired and an orchestrator is connected.
  bool available() const;

  // Push-to-talk, press-and-hold: held=true on button press, false on
  // release. No-op if unavailable or the mic is muted.
  void set_ptt_held(bool held);

  // 0 disconnected, 1 idle, 2 listening, 3 speaking — for a UI indicator.
  int state_code() const;

  // --- Speaker (TTS/alert output) ---

  // Mute/unmute the panel speaker output (holds the amp disabled). Panel-
  // local; does not touch SignalK.
  void set_speaker_muted(bool muted);
  bool speaker_muted() const { return speaker_muted_; }

  // Output volume 0-100 (applied at the codec). get_volume returns the last
  // set value (or a default) for a slider's initial position.
  void set_volume(uint8_t pct);
  uint8_t volume() const { return volume_; }

  // --- Microphone ---

  // Mute/unmute the mic: while muted, push-to-talk (and future always-on
  // listening) is suppressed — the privacy switch. Panel-local.
  void set_mic_muted(bool muted);
  // Atomic: written on the event_loop (widget callback) but read from the
  // httpd task (the /mic_probe privacy gate + the satellite mute predicate).
  bool mic_muted() const { return mic_muted_.load(); }

 private:
  sensesp_wyoming::WyomingSatellite* sat_ = nullptr;
  sensesp_cockpit_display::AudioDriver* audio_ = nullptr;
  bool speaker_muted_ = false;
  std::atomic<bool> mic_muted_{false};
  uint8_t volume_ = 50;  // matches WaveshareAudio's default
};

VoiceControl& voice();

}  // namespace jlp
