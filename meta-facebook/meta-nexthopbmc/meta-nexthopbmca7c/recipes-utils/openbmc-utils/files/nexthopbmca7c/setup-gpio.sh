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
gpio_export_by_name "${ASPEED_GPIO}" GPIOE2 CPE_CTRL

# Write protect for EEPROM
setup_gpio EEPROM_WP        GPIOE4  out 1
# Alert from CPU
setup_gpio CPU_ALERT_L      GPIOF1  in
# Reset from CPU
setup_gpio CPU_RST_L        GPIOF2  in
# Power cycle request from BMC
setup_gpio BMC_PWR_CYC_REQ  GPIOF0  out 0
# BMC_GPIO_0
setup_gpio BMC_GPIO_0       GPIOAA6 in
# BMC_GPIO_1
setup_gpio BMC_GPIO_1       GPIOAA7 in
# BMC_GPIO_2
setup_gpio BMC_GPIO_2       GPIOH2  in
# BMC_GPIO_3
setup_gpio BMC_GPIO_3       GPIOH3  in
# SVI VR Alert
setup_gpio SVI_VR_ALERT_L   GPIOE0  in
# BMC Mode Latch
setup_gpio BMC_MODE_LATCH   GPIOE1  in
# BMC FPGA Prog
setup_gpio BMC_FPGA_PROG    GPIOE3  out 0
# COME Boot Okay
setup_gpio BOOT_OK          GPIOAA4 in
# TPM PIRQ
setup_gpio TPM_PIRQ_L       GPIOG5  in
# USB2B VBUS Sense
setup_gpio USB2B_VBUS_SNS   GPIOAA3 in
# TPM_GPIO_0
setup_gpio TPM_GPIO_0       GPIOG6  in
# TPM_GPIO_1
setup_gpio TPM_GPIO_1       GPIOG7  in
# TPM_GPIO_2
setup_gpio TPM_GPIO_2       GPIOH0  in
# FPGA_GRAB
setup_gpio FPGA_GRAB        GPIOD2  out 0
# BMC_GPIO_5
setup_gpio BMC_GPIO_5       GPIOD3  in
# BMC_GPIO_6
setup_gpio BMC_GPIO_6       GPIOD4  in
# PCB ID strap 0
setup_gpio PCB_ID_0         GPIOC6  in
# PCB ID strap 1
setup_gpio PCB_ID_1         GPIOC7  in
