#!/bin/bash
#
# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program in a file named COPYING; if not, write to the
# Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA
#

### BEGIN INIT INFO
# Provides:          gpio-setup
# Required-Start:
# Required-Stop:
# Default-Start:     S
# Default-Stop:
# Short-Description:  Set up GPIO pins as appropriate
### END INIT INFO

# shellcheck disable=SC1091
. /usr/local/bin/openbmc-utils.sh

# Leakage related GPIOs
gpio_export_by_offset i2c-io-expander 1 LEAK_BMC_L

gpio_export_by_offset i2c-io-expander 2 LEAK_DISABLE_PWR_L
gpio_set_value LEAK_DISABLE_PWR_L 1

# Set DEBUG_PRSNT low to route CPU UART to BMC in SCMCPLD
gpio_export_by_offset i2c-io-expander 9 DEBUG_PRSNT
gpio_set_value DEBUG_PRSNT 0

# BMC ready event to COME
gpio_export_by_offset i2c-io-expander 8 FM_BMC_READY_R_L
gpio_set_value FM_BMC_READY_R_L 0

# COME platform reset signal
setup_gpio RST_PLTRST_L                     GPIOF2 in

# Enable I2C buffer for MCB CPLD
setup_gpio BMC_MCB_I2C13_BUF_EN        GPIOL1 out 1
