#pragma once

// Process-lifetime hub for the panel's voice + audio controls, so JLP widgets
// (mic button, mute/volume) can drive them without each widget depending on
// the satellite / audio-driver types. Mirrors chime().

#include <atomic>
#include <cstdint>

namespace espos_voice {
class WyomingSatellite;
}
namespace espos_audio {
class AudioDriver;
}

namespace jlp {

class VoiceControl {
 public:
  // Wires the satellite + audio driver and restores the persisted
  // speaker/mic mute state, so the panel comes back the way it was left.
  // Call after store_init() — the flags live in the same NVS namespace.
  void init(espos_voice::WyomingSatellite* sat,
            espos_audio::AudioDriver* audio);

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
  //
  // Stored as "is muted" while the `speaker` widget reads ON = working. The
  // negation is deliberate: unmuted is the power-on default, so a false-means-
  // working field needs no migration and no NVS default to get right. It is
  // also what /hello publishes as audio_state.speaker_muted.
  void set_speaker_muted(bool muted);
  bool speaker_muted() const { return speaker_muted_.load(); }

  // Output volume 0-100 (applied at the codec). get_volume returns the last
  // set value (or a default) for a slider's initial position.
  // Applies immediately. `persist` writes the value to NVS — pass false
  // while a slider is being dragged (LV_EVENT_VALUE_CHANGED fires per pixel,
  // so persisting each one would burn NVS for a single sweep) and true once
  // on release.
  void set_volume(uint8_t pct, bool persist = true);
  uint8_t volume() const { return volume_.load(); }

  // --- Microphone ---

  // Mute/unmute the mic: while muted, push-to-talk and wake-word streaming
  // are suppressed — the privacy switch. Panel-local.
  //
  // Stored as "is muted" while the `mic` widget reads ON = live, for the
  // same reason set_speaker_muted above is: unmuted is the power-on default.
  void set_mic_muted(bool muted);
  // Atomic: written on the UI task (widget callback) but read from the
  // httpd task (the /mic_probe privacy gate + the satellite mute predicate).
  bool mic_muted() const { return mic_muted_.load(); }

 private:
  espos_voice::WyomingSatellite* sat_ = nullptr;
  espos_audio::AudioDriver* audio_ = nullptr;
  // All three are written on the UI task (widget callbacks) and read from
  // the httpd task (/hello's audio_state, the /mic_probe privacy gate, the
  // satellite mute predicate), so they are atomic for the same reason
  // mic_muted_ always was. Relaxed ordering is enough: each is an independent
  // scalar and no reader infers anything from the order of the others.
  std::atomic<bool> speaker_muted_{false};
  std::atomic<bool> mic_muted_{false};
  std::atomic<uint8_t> volume_{50};  // matches WaveshareAudio's default
};

VoiceControl& voice();

}  // namespace jlp
