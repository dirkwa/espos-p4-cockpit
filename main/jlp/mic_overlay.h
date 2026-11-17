#pragma once

#include "lvgl.h"

namespace jlp {

/** Listening indicator shown while the voice pipeline has the mic. Visual
 *  rather than audible because mic and speaker share one I2S bus inches
 *  apart: a tone at wake would seed the orchestrator's endpointer noise
 *  floor from itself and stop the utterance ever ending early. */
class MicOverlay {
 public:
  void init();
  // Public for the poll timer in init().
  void set_visible(bool on);
  // Re-color from the active theme; init() and every theme change.
  void restyle();

 private:
  lv_obj_t* root_ = nullptr;
  // Drawn from primitives: the symbol font has no microphone
  // (LV_SYMBOL_AUDIO is a music note) and a bitmap needs an asset per density.
  lv_obj_t* capsule_ = nullptr;
  lv_obj_t* yoke_ = nullptr;
  lv_obj_t* yoke_cut_ = nullptr;
  lv_obj_t* stem_ = nullptr;
  lv_obj_t* base_ = nullptr;
  bool visible_ = false;
  uint8_t pulse_ = 0;
};

MicOverlay& mic_overlay();

}  // namespace jlp
