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

# Wedge400/Wedge400C have one of two OOB switches on AST2500 MAC1's MDIO
# (mdio-util bus 1, enabled by setup_mgmt.sh). The BCM5389 is probed first:
# its pseudo-PHY does not respond, so status comes from the per-port SerDes
# MII blocks (0x0-0x7) and the front MGMT port's BCM54616S PHY (0xe). Port
# map per the Wedge400 system spec (2.5.3.2): 0 SCM/microserver (SGMII),
# 4 BMC (1000BASE-X), 5 front RJ45 (SGMII), 6/7 Rackmon, others unused.
# Otherwise fall back to the 88E6321, read through its SMI registers (0x12)
# like the other platforms' oob-status.sh and oob-mdio-util.sh.

# shellcheck disable=SC1091
. /usr/local/bin/openbmc-utils.sh

trap 'echo "Exiting because of signal" >&2; exit 1' INT TERM QUIT

OOB_MDIO_BUS=1
MV_SMI_ADDR=0x12
BCM_MGMT_PHY=0xe

SPEEDS=("10   Mb/s" "100  Mb/s" "1000 Mb/s" "unknown  ")
DUPLEX=("half-duplex" "full-duplex")
# BCM54616S Auxiliary Status Summary (reg 0x19) bits 10:8, autoneg result
AUX_HCD=("speed unresolved" "10   Mb/s half-duplex" "10   Mb/s full-duplex"
         "100  Mb/s half-duplex" "100  Mb/s half-duplex" "100  Mb/s full-duplex"
         "1000 Mb/s half-duplex" "1000 Mb/s full-duplex")

usage() {
    echo "Usage: oob-status.sh"
    echo "Print link status and speed of all OOB switch (BCM5389 or 88E6321) ports"
    echo "BCM5389 ports 0-7: 0 microserver, 3 TH3 (unused), 4 BMC, 5 front MGMT port"
    echo "88E6321 ports 0-6: 3 front MGMT port, 4 rackmon"
}

# mdio-util prints "Read from PHY 0x12.0x1, value is 0x4e09"
mdio_read() {
    mdio-util -m "$OOB_MDIO_BUS" -p "$(printf "0x%x" "$1")" -r "$2" \
        | sed -nE 's/^Read .* value is (0x[a-fA-F0-9]*)$/\1/p'
}

# 88E6321: read a port register (device 0x10 + port) through SMI command/data
mv_port_reg() {
    mdio-util -m "$OOB_MDIO_BUS" -p "$MV_SMI_ADDR" -w 0x0 \
        -d "$(printf "0x%04x" $(( 0x9800 | ((0x10 + $1) << 5) | $2 )))" > /dev/null
    mdio_read "$MV_SMI_ADDR" 0x1
}

bcm5389_present() {
    # Port SerDes MII blocks report Broadcom ID 0x0143/0xbd7x
    local id2
    [ "$(mdio_read 0x0 0x2)" = "0x143" ] && id2=$(mdio_read 0x0 0x3) &&
        [ $(( ${id2:-0} & 0xfff0 )) -eq $(( 0xbd70 )) ]
}

port_link() {
    local val
    if [ "$chip" = "BCM5389" ]; then
        mdio_read "$1" 0x1 > /dev/null   # BMSR link status latches low
        val=$(mdio_read "$1" 0x1)
        echo $(( (val >> 2) & 0x1 ))
    else
        echo $(( ($(mv_port_reg "$1" 0x0) >> 11) & 0x1 ))   # Port Status
    fi
}

port_speed() {
    local val spd dpx mode=""
    if [ "$chip" = "BCM5389" ]; then
        # SerDes link partner ability: bit 0 set is an SGMII word (speed in
        # bits 11:10, duplex in bit 12), otherwise 1000BASE-X (duplex bit 5)
        val=$(mdio_read "$1" 0x5)
        if [ $(( val & 0x1 )) -eq 1 ]; then
            spd=$(( (val >> 10) & 0x3 )); dpx=$(( (val >> 12) & 0x1 )); mode=" (SGMII)"
        else
            spd=2; dpx=$(( (val >> 5) & 0x1 )); mode=" (1000BASE-X)"
        fi
    else
        val=$(mv_port_reg "$1" 0x0)      # Port Status
        spd=$(( (val >> 8) & 0x3 )); dpx=$(( (val >> 10) & 0x1 ))
    fi
    echo "${SPEEDS[$spd]} ${DUPLEX[$dpx]}$mode"
}

# $1: status or speed
do_read() {
    local port out
    printf "\tLink %s:\n" "$1"
    for port in $(seq 0 "$max_port"); do
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

do_read_mgmt_phy() {
    local val status="Link Down"
    val=$(mdio_read "$BCM_MGMT_PHY" 0x19)
    [ $(( (val >> 2) & 0x1 )) -eq 1 ] && status="Link Up   - ${AUX_HCD[$(( (val >> 8) & 0x7 ))]}"
    printf "\tMGMT port PHY (%s): %s\n" "$BCM_MGMT_PHY" "$status"
}

[ $# -gt 0 ] && { usage; exit 1; }

# Only allow one instance of script to run at a time.
exec 100< "$(realpath "$0")"
flock -n 100 || { echo "ERROR: $0 already running" && exit 1; }

chip="88E6321"; max_port=6
bcm5389_present && { chip="BCM5389"; max_port=7; }

printf "OOB switch: %s\nStatus:\n" "$chip"
[ "$chip" = "BCM5389" ] && do_read_mgmt_phy
do_read status
do_read speed
