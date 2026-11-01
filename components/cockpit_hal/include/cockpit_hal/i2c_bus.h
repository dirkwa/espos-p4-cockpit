/* SPDX-License-Identifier: Apache-2.0
 *
 * The board's shared I2C bus (SDA 7 / SCL 8 on the Waveshare P4 boards):
 * created by the touch HAL, reused by the audio codecs.
 */
#pragma once

#include "driver/i2c_master.h"

namespace cockpit_hal {
i2c_master_bus_handle_t i2c_bus();
}
