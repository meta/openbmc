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

# nexthopbmca7c (AST2720-A2) counterpart of meta-nexthopbmc's setup_i2c.sh: same
# devices, AST2700 bus numbers.

# shellcheck disable=SC1091
. /usr/local/bin/openbmc-utils.sh

# Instantiate an i2c device only if the hardware responds, so EEPROMs which
# are not fitted on all hardware revisions don't trigger at24 probe failures.
i2c_device_add_if_present() {
    if i2cget -y -f "$1" "$2" > /dev/null 2>&1; then
        i2c_device_add "$1" "$2" "$3"
    fi
}

# FRU IDPROMS
i2c_device_add 4 0x50 24c64  # BMC IDPROM

# The chassis (switch card) and SCM (CPU card) EEPROMs are only wired on
# later CPU card revisions, so both are optional.
i2c_device_add_if_present 10 0x50 24c64  # Chassis EEPROM
i2c_device_add_if_present 0 0x50 24c64   # SCM EEPROM

# APML
i2c_device_add 11 0x4c sbtsi
i2c_device_add 11 0x3c sbrmi

modprobe apml_sbrmi
modprobe apml_sbtsi
