#!/bin/bash
#
# Copyright (c) Meta Platforms, Inc. and affiliates.
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
# Identify NL1.0 or NL2.0 through COMe FPGA reg.
# COMe FPGA Bus:0 SLA:0x1F reg:0x01 Description:Bit7: System_NL2_Flag (0=NL1.0, 1=NL2.0)
#
netlake_identify() {
    local bus=13 addr=0x1f reg=0x01
    local val=0
    local ret=2
    echo "Identifying board type..."
    # Enable I2C channel 1
    setup_gpio BMC_I2C1_EN GPIOL0 out 1
    sleep 3
    if ! val=$(i2cget -f -y "$bus" "$addr" "$reg" 2>/dev/null); then
        echo "Error: Failed to read I2C register." >&2
        ret=2
    elif [ -z "$val" ]; then
        echo "Error: I2C returned empty value." >&2
        ret=2
    elif [ $((val & 0x80)) -ne 0 ]; then
        echo "Netlake 2.0 detected (Value: $val)"
        ret=1
    else
        echo "Netlake 1.0 detected (Value: $val)"
        ret=0
    fi

    # Disable I2C channel 1
    setup_gpio BMC_I2C1_EN GPIOL0 out 0
    return "$ret"
}
