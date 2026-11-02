#!/bin/bash
#
# Copyright 2026-present Facebook. All Rights Reserved.
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

run_util() {
    if [ ! -x "$1" ]; then
        echo "$1 doesn't exist!"
    else
        "$@"
    fi
}

echo -e "\n##### USER PWR STATUS #####"
run_util /usr/local/bin/wedge_power.sh status

echo -e "\n##### BMC BOARD REVISION #####"
run_util /usr/local/bin/fboss-board-revision.sh -s

echo -e "\n##### BMC CMDLINE #####"
cat /proc/cmdline
