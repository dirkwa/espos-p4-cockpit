// SPDX-License-Identifier: Apache-2.0
//
// The widget kinds this player builds and the fields each one reads, as
// /hello reports them. A raw JSON literal, so /hello splices it in with
// ArduinoJson's serialized() instead of building dozens of JsonObjects per
// call. In a header because the WASM build reports the same catalog for a
// browser display. Keep it in step with build_widget() in
// widget_factory.cpp.
#pragma once

namespace jlp {

inline constexpr const char* kWidgetCatalogJson =
    "{"
      "\"label\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bind\",\"display\",\"show_description\",\"bg_color\",\"fg_color\"]},"
      "\"value\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bind\",\"display\",\"bg_color\",\"fg_color\"]},"
      "\"toggle\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bind\",\"bg_color\",\"fg_color\"]},"
      "\"arc\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bind\",\"display\",\"min\",\"max\",\"start_angle\",\"end_angle\",\"ticks\",\"tick_labels\",\"bands\",\"bg_color\",\"fg_color\"]},"
      "\"bar\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bind\",\"display\",\"min\",\"max\",\"vertical\",\"bg_color\",\"fg_color\"]},"
      "\"bargroup\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bars\",\"bg_color\",\"fg_color\"]},"
      "\"button\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bind\",\"press_value\",\"release_value\",\"hold_ms\",\"bg_color\",\"fg_color\"]},"
      "\"notifications\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"max_rows\",\"row_height\",\"columns\",\"row_color_field\",\"include_cleared\",\"bg_color\",\"fg_color\"]},"
      "\"anchor\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"display\",\"fg_color\"]},"
      "\"anchor_track\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"display\",\"fg_color\"]},"
      "\"voice\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bg_color\",\"fg_color\"]},"
      // The catalogue is what a designer should OFFER, not everything the
      // device accepts: it feeds the "Add widget" palette directly. Listing
      // the mute_speaker/mute_mic aliases here would put four buttons on it
      // for two controls and invite new layouts to use the deprecated name.
      // Acceptance lives in widget_factory's dispatch; JLP-PROTOCOL documents
      // the aliases for anyone reading an old layout.
      "\"speaker\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bg_color\",\"fg_color\"]},"
      "\"mic\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bg_color\",\"fg_color\"]},"
      "\"volume\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bg_color\",\"fg_color\"]},"
      "\"slider\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bind\",\"display\",\"min\",\"max\",\"bg_color\",\"fg_color\"]},"
      "\"stream\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"host\",\"port\",\"touch\",\"touch_port\",\"bg_color\",\"fg_color\"]}"
    "}";

}  // namespace jlp
