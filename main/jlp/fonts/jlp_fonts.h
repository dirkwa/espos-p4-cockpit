// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "lvgl.h"

// Montserrat with Latin-1 and common typographic marks, falling back to
// LVGL's built-in font of the same size for LV_SYMBOL_* icons. Generated
// by scripts/gen-fonts.sh; use these instead of lv_font_montserrat_*,
// whose ASCII-only glyphs draw umlauts and "·" as boxes.
LV_FONT_DECLARE(jlp_font_14)
LV_FONT_DECLARE(jlp_font_16)
LV_FONT_DECLARE(jlp_font_20)
LV_FONT_DECLARE(jlp_font_28)
LV_FONT_DECLARE(jlp_font_36)
