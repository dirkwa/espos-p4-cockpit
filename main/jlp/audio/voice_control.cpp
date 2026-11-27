#include "voice_control.h"

#include "espos_audio/audio_driver.h"
#include "espos_voice/wyoming_satellite.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

void VoiceControl::set_mode(bool enabled, bool wake) {
  const bool wake_was_wanted = wake_wanted();
  const bool enabled_changed = want_enabled_.exchange(enabled) != enabled;
  const bool wake_changed = want_wake_.exchange(wake) != wake;
  // dirty_ still set: a worker could not be created last time.
  if (enabled_changed || wake_changed || dirty_.load()) kick();
  if (!wake_was_wanted && wake_wanted() && on_wake_wanted_) on_wake_wanted_();
}

void VoiceControl::set_network_ready() {
  net_ready_.store(true);
  kick();
}

void VoiceControl::kick() {
  dirty_.store(true);
  bool idle = false;
  if (!worker_running_.compare_exchange_strong(idle, true)) return;  // it loops
  // The stack the satellite's start() had on the main task: bringing WakeNet
  // up runs esp-sr's AFE setup on the caller's stack.
  if (xTaskCreate(worker, "jlp_voice", 8192, this, 3, nullptr) != pdPASS) {
    worker_running_.store(false);
    ESP_LOGE(kTag, "no task to switch voice; the next layout applied retries");
  }
}

void VoiceControl::worker(void* arg) {
  auto* self = static_cast<VoiceControl*>(arg);
  // A wake word that did not come up, or a switch the satellite could not
  // finish in time, is retried by espOS on the next call. A few calls,
  // further apart: a word that never loads is not worth retrying for ever,
  // and push-to-talk works meanwhile.
  constexpr int kRetries = 3;
  int failures = 0;
  for (;;) {
    while (self->dirty_.exchange(false)) {
      if (self->reconcile()) {
        failures = 0;
        continue;
      }
      if (++failures > kRetries) {
        ESP_LOGW(kTag, "voice switch still incomplete; giving up until the next layout");
        failures = 0;
        continue;
      }
      const int wait_ms = 15000 << (failures - 1);
      ESP_LOGW(kTag, "voice switch incomplete; trying again in %d s", wait_ms / 1000);
      // A layout pushed meanwhile ends the wait: it is applied at once.
      for (int t = 0; t < wait_ms && !self->dirty_.load(); t += 500) {
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      self->dirty_.store(true);
    }
    self->worker_running_.store(false);
    // A kick between the last exchange and the store above saw a running
    // worker and left the change to it.
    bool idle = false;
    if (!self->dirty_.load() ||
        !self->worker_running_.compare_exchange_strong(idle, true)) {
      break;
    }
  }
  vTaskDelete(nullptr);
}

bool VoiceControl::reconcile() {
  if (!sat_) return true;
  if (!want_enabled_.load()) {
    if (sat_->running()) {
      ESP_LOGI(kTag, "voice off");
      sat_->stop();
    }
    return true;
  }
  // Before start() this is only recorded, so a panel that should not listen
  // never loads WakeNet.
  const bool wake = want_wake_.load();
  bool done = sat_->set_wake_enabled(wake);
  if (net_ready_.load() && !sat_->running()) {
    ESP_LOGI(kTag, "voice on, wake word %s", wake ? "on" : "off");
    sat_->start();
  }
  // start() does not say whether the wake word came up.
  if (sat_->running() && wake && !sat_->wake_enabled()) done = false;
  return done;
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
