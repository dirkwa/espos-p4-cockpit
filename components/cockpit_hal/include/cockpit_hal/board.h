/* SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
 *
 * Selects the panel driver for the board this firmware is built for
 * (CONFIG_COCKPIT_BOARD_*, see the component's Kconfig) and exposes it under
 * one pair of names. Application code uses cockpit_hal::BoardDisplay and
 * cockpit_hal::BoardTouch and needs to know nothing else about the panel.
 *
 * The boards are NOT interchangeable at runtime -- different controller,
 * resolution, orientation and pins -- so this is a build-time choice.
 */
#pragma once

#include "sdkconfig.h"

#if CONFIG_COCKPIT_BOARD_WAVESHARE_LCD_X_7
#include "cockpit_hal/waveshare_lcd_x.h"
#elif CONFIG_COCKPIT_BOARD_WAVESHARE_7B
#include "cockpit_hal/waveshare_7b.h"
#else
#error "No panel board selected: set CONFIG_COCKPIT_BOARD_* (menuconfig -> Cockpit hardware -> Panel board)"
#endif

namespace cockpit_hal {

#if CONFIG_COCKPIT_BOARD_WAVESHARE_LCD_X_7
using BoardDisplay = WaveshareXDisplay;
using BoardTouch = WaveshareXTouch;
/// The X panel is portrait and is rotated to landscape inside the driver,
/// so the UI layer must not apply any rotation of its own.
#define COCKPIT_UI_ROTATE_180 0
/// Passed to espos_start() as opts.board, so /api/v1/system/info says which
/// panel this image was built for. Nothing can discover it at runtime -- IDF
/// knows the chip, not what it is soldered to -- and it is the fact a fleet
/// manager needs before it may offer an update: these boards are not
/// interchangeable, and the wrong image leaves the screen black. The size is
/// part of the name because each size in the X series drives a different
/// controller.
#define COCKPIT_BOARD_NAME "Waveshare ESP32-P4-WIFI6-Touch-LCD-X 7in"
#elif CONFIG_COCKPIT_BOARD_WAVESHARE_7B
using BoardDisplay = Waveshare7BDisplay;
using BoardTouch = Waveshare7BTouch;
/// The 7B is mounted upside down; the UI layer rotates each flushed strip.
#define COCKPIT_UI_ROTATE_180 1
#define COCKPIT_BOARD_NAME "Waveshare ESP32-P4-WIFI6-Touch-LCD-7B"
#endif

}  // namespace cockpit_hal
