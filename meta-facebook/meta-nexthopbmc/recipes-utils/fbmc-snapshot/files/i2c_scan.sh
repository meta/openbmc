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

# shellcheck disable=SC1091
. /usr/local/bin/openbmc-utils.sh

# Buses showtech must not probe: the SSIF link, where the BMC is the slave
# (ssif_bmc) and the host CPU is the master, so i2cdetect both collides with
# host traffic and stalls until its 5 s timeout. Take the bus from the
# ssif_bmc driver's bindings (<bus>-<addr>) rather than hard-coding it; the
# driver registers as "ipmi-ssif-host".
ssif_buses() {
    local dev

    for dev in /sys/bus/i2c/drivers/ipmi-ssif-host/*-*; do
        [ -e "$dev" ] || continue
        dev=${dev##*/}
        echo "${dev%%-*}"
    done
}
SKIP_BUSES="$(ssif_buses)"

skip_bus() {
    local skip
    for skip in $SKIP_BUSES; do
        [ "$1" = "$skip" ] && return 0
    done
    return 1
}

list_i2cbus() {
    local dev

    # /sys/class/i2c-dev holds one entry per /dev/i2c-N the kernel created,
    # so this is exactly the set i2cdetect can open.
    for dev in /sys/class/i2c-dev/i2c-*; do
        [ -e "$dev" ] || continue
        echo "${dev##*/i2c-}"
    done | sort -n
}

scan_i2cbus() {
    local bus

    for bus in $(list_i2cbus); do
        echo -e "##### I2C Bus${bus} INFO #####"
        if skip_bus "$bus"; then
            echo "skipped (bus $bus is driven by the host; BMC is a slave)"
            echo ""
            continue
        fi
        # i2cdetect command
        timeout 5 i2cdetect -y "$bus"

        echo ""
    done
}
scan_i2cbus
