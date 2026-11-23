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

# Wedge100/Wedge100S OOB switch is a 5-port BCM5387. Its MDIO is on
# GPIOR6/R7 (SWITCH_MDC/SWITCH_MDIO), the AST2400's MDC1/MDIO1 pins, but MAC1
# is unused on this platform, so the switch is reached by bit-banging the
# GPIOs with mdio-bb. Nothing else drives these pins. Its pseudo-PHY (0x1e)
# does not respond, so port status comes from the per-port SerDes MII blocks.
#
# Port 2 faces the front MGMT port's BCM54616S (0x19), which runs in
# 1000BASE-X-to-copper mode, so the switch side always shows 1000BASE-X.
# Port 2 is therefore reported from the PHY's copper side. The PHY shares
# MAC2's MDIO bus with the BMC's own PHY (0x18), which the kernel polls, so
# it is read with mii-read (SIOCGMIIREG, under the kernel's MDIO bus lock).

# shellcheck disable=SC1091
. /usr/local/bin/openbmc-utils.sh

trap 'echo "Exiting because of signal" >&2; exit 1' INT TERM QUIT

MAX_PORT=4
MGMT_PORT=2
MGMT_PHY=0x19

SPEEDS=("10   Mb/s" "100  Mb/s" "1000 Mb/s" "unknown  ")
DUPLEX=("half-duplex" "full-duplex")
# BCM54616S Auxiliary Status Summary (reg 0x19) bits 10:8, autoneg result
AUX_HCD=("Unknown"
         "10   Mb/s half-duplex (10BASE-T)" "10   Mb/s full-duplex (10BASE-T)"
         "100  Mb/s half-duplex (100BASE-TX)" "100  Mb/s half-duplex (100BASE-T4)"
         "100  Mb/s full-duplex (100BASE-TX)"
         "1000 Mb/s half-duplex (1000BASE-T)" "1000 Mb/s full-duplex (1000BASE-T)")

usage() {
    echo "Usage: oob-status.sh"
    echo "Print link status and speed of all OOB switch (BCM5387) ports"
    echo "BCM5387 ports 0-4: 0/4 BMC and microserver, 2 front MGMT port"
}

# mdio-bb prints "Read: 0x16d"
switch_mdio_read() {
    local out
    out=$(mdio-bb -p -c "$mdc" -d "$mdio" read "$1" "$2") || return 1
    echo "${out#Read: }"
}

bcm5387_present() {
    # Port SerDes MII blocks report Broadcom ID 0x0143/0xbd7x. The ID is
    # shared with the BCM5389 (which also answers at 0x5-0x7); the BCM5387
    # model and ports 0-4 come from the platform's switch EEPROM config.
    local id2
    [ "$(switch_mdio_read 0x0 0x2)" = "0x143" ] && id2=$(switch_mdio_read 0x0 0x3) &&
        [ $(( ${id2:-0} & 0xfff0 )) -eq $(( 0xbd70 )) ]
}

# Sets link[$1] and speed[$1] from the port's SerDes MII block
read_switch_port() {
    local val spd dpx mode
    switch_mdio_read "$1" 0x1 > /dev/null   # BMSR link status latches low
    val=$(switch_mdio_read "$1" 0x1)
    if [ -z "$val" ]; then
        link[$1]="Unknown"; speed[$1]="Unknown"; return
    fi
    if [ $(( (val >> 2) & 0x1 )) -eq 0 ]; then
        link[$1]="Link Down"; speed[$1]="Link Down"; return
    fi
    link[$1]="Link Up  "
    # SerDes link partner ability: bit 0 set is an SGMII word (speed in
    # bits 11:10, duplex in bit 12), otherwise 1000BASE-X (duplex bit 5)
    val=$(switch_mdio_read "$1" 0x5)
    if [ $(( val & 0x1 )) -eq 1 ]; then
        spd=$(( (val >> 10) & 0x3 )); dpx=$(( (val >> 12) & 0x1 )); mode=" (SGMII)"
    else
        spd=2; dpx=$(( (val >> 5) & 0x1 )); mode=" (1000BASE-X)"
    fi
    speed[$1]="${SPEEDS[$spd]} ${DUPLEX[$dpx]}$mode"
}

# Sets link[] and speed[] for the MGMT port from the BCM54616S copper side
read_mgmt_port() {
    local val
    val=$(mii-read eth0 "$MGMT_PHY" 0x19 2> /dev/null)
    # An absent PHY reads back 0xffff
    if [ -z "$val" ] || [ "$val" = "0xffff" ]; then
        link[MGMT_PORT]="Unknown"; speed[MGMT_PORT]="Unknown"
    elif [ $(( (val >> 2) & 0x1 )) -eq 0 ]; then
        link[MGMT_PORT]="Link Down"; speed[MGMT_PORT]="Link Down"
    else
        link[MGMT_PORT]="Link Up  "
        speed[MGMT_PORT]="${AUX_HCD[$(( (val >> 8) & 0x7 ))]}"
    fi
}

read_ports() {
    local port
    for port in $(seq 0 "$MAX_PORT"); do
        if [ "$port" -eq "$MGMT_PORT" ]; then
            read_mgmt_port
        else
            read_switch_port "$port"
        fi
    done
}

# $1: status or speed
do_print() {
    local port val
    printf "\tLink %s:\n" "$1"
    for port in $(seq 0 "$MAX_PORT"); do
        if [ "$1" = "status" ]; then val=${link[port]}; else val=${speed[port]}; fi
        printf "\t\tPort: %s - %s\n" "$port" "$val"
    done
}

[ $# -gt 0 ] && { usage; exit 1; }

# Only allow one instance of script to run at a time.
exec 100< "$(realpath "$0")"
flock -n 100 || { echo "ERROR: $0 already running" && exit 1; }

mdc=$(gpio_name2value SWITCH_MDC)
mdio=$(gpio_name2value SWITCH_MDIO)

bcm5387_present || { echo "ERROR: BCM5387 OOB switch not found" >&2; exit 1; }

read_ports
printf "Status:\n"
do_print status
do_print speed
