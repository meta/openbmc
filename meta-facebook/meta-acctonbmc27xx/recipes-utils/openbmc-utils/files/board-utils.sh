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

wedge_board_type() {
    echo 'acctonbmc27xx'
}

wedge_board_rev() {
    # FIXME if needed.
    return 1
}

userver_power_is_on() {
    echo "FIXME: feature not implemented!!"
    return 1
}

userver_power_on() {
    echo "FIXME: feature not implemented!!"
    return 1
}

userver_power_off() {
    echo "FIXME: feature not implemented!!"
    return 1
}

userver_reset() {
    return 1
    echo "FIXME: feature not implemented!!"
}

chassis_power_cycle() {
    echo "FIXME: feature not implemented!!"
    return 1
}

bmc_mac_addr() {
    echo "FIXME: feature not implemented!!"
    return 1
}

userver_mac_addr() {
    echo "FIXME: feature not implemented!!"
    return 1
}
