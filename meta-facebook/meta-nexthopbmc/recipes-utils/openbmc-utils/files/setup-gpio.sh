#!/bin/bash
#
# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
# Copyright (c) Nexthop Systems Inc.
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

# COME Power on
# Note, don't actually set a value here, let it keep the value it has
gpio_export_by_name "${ASPEED_GPIO}" GPIOR2 CPE_CTRL

# Write protect for EEPROM
setup_gpio EEPROM_WP  GPIOQ0 out 0
# Alert from CPU
setup_gpio CPU_ALERT_L  GPIOQ1 in 
# Reset from CPU
setup_gpio CPU_RST_L  GPIOQ2 in 
# Power cycle request from BMC
setup_gpio BMC_PWR_CYC_REQ  GPIOQ3 out 0
# BMC_GPIO_0
setup_gpio BMC_GPIO_0  GPIOQ4 in 
# BMC_GPIO_1
setup_gpio BMC_GPIO_1  GPIOQ5 in 
# BMC_GPIO_2
setup_gpio BMC_GPIO_2  GPIOQ6 in 
# BMC_GPIO_3
setup_gpio BMC_GPIO_3  GPIOQ7 in 
# SVI VR Alert
setup_gpio SVI_VR_ALERT_L  GPIOR0 in 
# BMC Mode Latch
setup_gpio BMC_MODE_LATCH  GPIOR1 in 
# BMC FPAG Prog
setup_gpio BMC_FPGA_PROG  GPIOR3 out 0
# COME Boot Okay
setup_gpio BOOT_OK  GPIOR4 in 
# TPM PIRQ
setup_gpio TPM_PIRQ_L  GPIOR6 in 
# USB2B VBUS Sense
setup_gpio USB2B_VBUS_SNS  GPIOR7 in 
# TPM_GPIO_0
setup_gpio TPM_GPIO_0  GPIOO0 in 
# TPM_GPIO_1
setup_gpio TPM_GPIO_1  GPIOO1 in 
# TPM_GPIO_2
setup_gpio TPM_GPIO_2  GPIOO2 in 
# FPGA_GRAB
setup_gpio FPGA_GRAB  GPIOO3 out 0
# BMC_GPIO_5
setup_gpio BMC_GPIO_5  GPIOO4 in 
# BMC_GPIO_6
setup_gpio BMC_GPIO_6  GPIOO5 in 
