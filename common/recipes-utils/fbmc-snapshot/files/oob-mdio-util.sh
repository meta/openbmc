#!/bin/sh
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

OOB_MDIO_BUS=2
OOB_PHY_ADDR=0x12

mode=$1
port=$2
reg_addr=$3

usage() {
    echo "OOB MDIO Utility to access Marvell 88E6321 OOB Switch"
    echo "Usage:"
    echo "$0 read <port> <reg_addr> "
    echo "$0 write <port> <reg_addr> <data>"
}

if [ $# -lt 3 ]; then
    usage
    exit 1;
fi

if [ "$mode" = "write" ]; then
    if [ $# -ne 4 ]; then
        usage
        exit 1;
    fi
    ctrl_cmd=$(( 0x9000 | (0x1 << 10) | ( (0x10 + port) << 5) | reg_addr ))
    ctrl_cmd=$(printf "0x%08X" "$ctrl_cmd")
    data=$4
    mdio-util -m "$OOB_MDIO_BUS" -p "$OOB_PHY_ADDR" -w 0x1 -d "$data"
    mdio-util -m "$OOB_MDIO_BUS" -p "$OOB_PHY_ADDR" -w 0x0 -d "$ctrl_cmd"
elif [ "$mode" = "read" ]; then
    if [ $# -ne 3 ]; then
        usage
        exit 1;
    fi
    ctrl_cmd=$(( 0x9000 | (0x2 << 10) | ( (0x10 + port) << 5) | reg_addr ))
    ctrl_cmd=$(printf "0x%08X" "$ctrl_cmd")
    mdio-util -m "$OOB_MDIO_BUS" -p "$OOB_PHY_ADDR" -w 0x0 -d "$ctrl_cmd"
    mdio-util -m "$OOB_MDIO_BUS" -p "$OOB_PHY_ADDR" -r 0x1
else
    usage
    exit 1;
fi

