#include "chime.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "esp_log.h"

#include "jlp/layout/store.h"

static const char* TAG = "jlp.chime";

namespace jlp {

// NVS key for the persisted chime mute (keys cap at 15 chars).
static constexpr const char* kChimeMutedKey = "chime_muted";

void Chime::init(espos_audio::AudioDriver* audio) {
  audio_ = audio;
  // Restore before any notification can fire, so a muted helm stays quiet
  // through the boot chime as well.
  muted_.store(store_flag_get(kChimeMutedKey, false));
}

void Chime::set_muted(bool m) {
  if (!muted_.exchange(m) && m) ++mutes_;
  store_flag_set(kChimeMutedKey, m);
}

namespace {

// A single tone segment: frequency and duration. A zero-frequency
// segment is a rest (silence between beeps).
struct Seg {
  float hz;
  uint16_t ms;
};

// Append a tone (or a rest, when hz==0) to `out` at `rate`, with a
// short linear fade in/out so the speaker doesn't click on each edge.
void append_seg(std::vector<int16_t>& out, uint32_t rate, const Seg& seg,
                float amp) {
  size_t n = (size_t)((uint32_t)seg.ms * rate / 1000);
  if (n == 0) return;
  size_t fade = n / 20;  // ~5% ramp on each end
  if (fade == 0) fade = 1;
  const float two_pi_f = 2.0f * (float)M_PI * seg.hz / (float)rate;
  size_t base = out.size();
  out.resize(base + n);
  for (size_t i = 0; i < n; ++i) {
    float env = 1.0f;
    if (i < fade)
      env = (float)i / (float)fade;
    else if (i >= n - fade)
      env = (float)(n - i) / (float)fade;
    float s = (seg.hz > 0.0f) ? sinf(two_pi_f * (float)i) : 0.0f;
    out[base + i] = (int16_t)(s * env * amp * 32767.0f);
  }
}

// Severity -> tone pattern. Higher severity = more insistent (more
// beeps, tighter spacing, higher pitch). Amplitudes stay below full
// scale to leave the codec headroom and avoid amp clipping.
void pattern_for(NotState state, std::vector<Seg>& segs, float& amp) {
  switch (state) {
    case NotState::Emergency:
      amp = 0.85f;
      segs = {{1100, 90}, {0, 60}, {1100, 90}, {0, 60}, {1100, 90}};
      break;
    case NotState::Alarm:
      amp = 0.80f;
      segs = {{880, 150}, {0, 90}, {880, 150}};
      break;
    case NotState::Warn:
      amp = 0.65f;
      segs = {{660, 220}};
      break;
    case NotState::Alert:
      amp = 0.55f;
      segs = {{540, 180}};
      break;
    default:  // Nominal / Normal — silent
      break;
  }
}

}  // namespace

void Chime::play(NotState state, bool ignore_mute) {
  if ((muted_ && !ignore_mute) || !audio_ || !audio_->ready()) return;

  std::vector<Seg> segs;
  float amp = 0.0f;
  pattern_for(state, segs, amp);
  if (segs.empty()) return;

  // Trailing silence so the codec's I2S DMA fully drains the last tone
  // before the driver closes the device and drops PA_CTRL — otherwise
  // the final few ms get clipped as the amp cuts.
  segs.push_back({0, 40});

  const uint32_t rate = audio_->sample_rate();
  std::vector<int16_t> pcm;
  pcm.reserve(rate / 2);  // ~half a second worst case
  for (const Seg& seg : segs) append_seg(pcm, rate, seg, amp);

  ESP_LOGI(TAG, "chime %s (%u frames)", not_state_name(state),
           (unsigned)pcm.size());
  audio_->play_pcm(pcm.data(), pcm.size());
}

Chime& chime() {
  static Chime c;
  return c;
}

}  // namespace jlp
