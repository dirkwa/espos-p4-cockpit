#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Regenerates main/jlp/fonts/jlp_font_<px>.c: Montserrat Medium with
# Latin-1 and the common typographic marks, which LVGL's built-in
# Montserrat (ASCII only) lacks, so labels such as "Kühlwasser" or
# "Bank 12 · 1" render instead of showing boxes. Each font falls back to
# the built-in one of its size for the LV_SYMBOL_* icons.
#
# Needs Node and a firmware build (managed_components/ holds LVGL's copy
# of the font). Keep the sizes in step with main/jlp/fonts/jlp_fonts.h.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

ttf=managed_components/lvgl__lvgl/scripts/generators/built_in_font/Montserrat-Medium.ttf
[ -f "$ttf" ] || { echo "missing $ttf: build the firmware once first" >&2; exit 1; }

# ASCII, Latin-1, – —, ‘ ’ ‚ “ ” „, •, …, €, minus sign.
range=0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2018-0x201E,0x2022,0x2026,0x20AC,0x2212

for px in 14 16 20 28 36; do
  npx --yes lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 \
    --size "$px" --font "$ttf" -r "$range" \
    --format lvgl --lv-include lvgl.h \
    --lv-fallback "lv_font_montserrat_$px" --force-fast-kern-format \
    -o "main/jlp/fonts/jlp_font_$px.c"
  sed -i -e '1i /* SPDX-License-Identifier: OFL-1.1 */' \
    -e '1a /* Montserrat, Copyright 2011 The Montserrat Project Authors; see OFL.txt. */' \
    "main/jlp/fonts/jlp_font_$px.c"
done
