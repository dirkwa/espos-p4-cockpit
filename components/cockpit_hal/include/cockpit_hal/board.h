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
#elif CONFIG_COCKPIT_BOARD_WAVESHARE_7B
using BoardDisplay = Waveshare7BDisplay;
using BoardTouch = Waveshare7BTouch;
/// The 7B is mounted upside down; the UI layer rotates each flushed strip.
#define COCKPIT_UI_ROTATE_180 1
#endif

}  // namespace cockpit_hal
