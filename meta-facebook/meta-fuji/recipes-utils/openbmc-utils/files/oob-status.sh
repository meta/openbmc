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

# Fuji OOB switch is a 5-port BCM5387 on AST2600 MDIO1 (mdio-util bus 2).
# Like the Wedge100 BCM5387 and Wedge400 BCM5389, its pseudo-PHY (0x1e) does
# not respond, so port status comes from the per-port SerDes MII blocks
# (0x0-0x4). Port map per the Minipack2 OOB switch EEPROM config:
# 0 COMe (SGMII, BCM54616S 0xa), 1 front MGMT port (SGMII, BCM54616S 0xe),
# 2 BMC (1000BASE-X, BCM54616S 0xd), 3/4 unused. Ports 0/1 run SGMII, so
# their link partner word carries the copper side's negotiated speed.
# MDIO1 is owned by the kernel, which polls the BMC's own PHY (0xd), so all
# registers are read with mii-read (SIOCGMIIREG on eth0, under the kernel's
# MDIO bus lock) instead of mdio-util's raw PHYCR access.

# shellcheck disable=SC1091
. /usr/local/bin/openbmc-utils.sh

trap 'echo "Exiting because of signal" >&2; exit 1' INT TERM QUIT

MAX_PORT=4

SPEEDS=("10   Mb/s" "100  Mb/s" "1000 Mb/s" "unknown  ")
DUPLEX=("half-duplex" "full-duplex")

usage() {
    echo "Usage: oob-status.sh"
    echo "Print link status and speed of all OOB switch (BCM5387) ports"
    echo "BCM5387 ports 0-4: 0 COMe, 1 front MGMT port, 2 BMC, 3/4 unused"
}

# Read every register needed into $regs ("phy:reg" -> value)
read_regs() {
    local args=("0x0:0x2" "0x0:0x3") port arg
    # BMSR link status latches low, so read it twice
    for port in $(seq 0 "$MAX_PORT"); do
        args+=("$port:0x1" "$port:0x1" "$port:0x5")
    done
    declare -gA regs
    for arg in "${args[@]}"; do
        regs[$arg]=$(mii-read eth0 "${arg%:*}" "${arg#*:}") || return 1
    done
}

bcm5387_present() {
    # Port SerDes MII blocks report Broadcom ID 0x0143/0xbd7x
    [ "${regs[0x0:0x2]}" = "0x143" ] &&
        [ $(( ${regs[0x0:0x3]:-0} & 0xfff0 )) -eq $(( 0xbd70 )) ]
}

port_link() {
    echo $(( (${regs[$1:0x1]} >> 2) & 0x1 ))
}

port_speed() {
    local val=${regs[$1:0x5]} spd dpx mode
    # SerDes link partner ability: bit 0 set is an SGMII word (speed in
    # bits 11:10, duplex in bit 12), otherwise 1000BASE-X (duplex bit 5)
    if [ $(( val & 0x1 )) -eq 1 ]; then
        spd=$(( (val >> 10) & 0x3 )); dpx=$(( (val >> 12) & 0x1 )); mode=" (SGMII)"
    else
        spd=2; dpx=$(( (val >> 5) & 0x1 )); mode=" (1000BASE-X)"
    fi
    echo "${SPEEDS[$spd]} ${DUPLEX[$dpx]}$mode"
}

# $1: status or speed
do_read() {
    local port out
    printf "\tLink %s:\n" "$1"
    for port in $(seq 0 "$MAX_PORT"); do
        out="Link Down"
        if [ "$(port_link "$port")" -eq 1 ]; then
            case "$1" in
                status) out="Link Up  " ;;
                speed) out=$(port_speed "$port") ;;
            esac
        fi
        printf "\t\tPort: %s - %s\n" "$port" "$out"
    done
}

[ $# -gt 0 ] && { usage; exit 1; }

# Only allow one instance of script to run at a time.
exec 100< "$(realpath "$0")"
flock -n 100 || { echo "ERROR: $0 already running" && exit 1; }

read_regs || { echo "ERROR: mii-read on eth0 failed" >&2; exit 1; }
bcm5387_present || { echo "ERROR: BCM5387 OOB switch not found" >&2; exit 1; }

printf "Status:\n"
do_read status
do_read speed
