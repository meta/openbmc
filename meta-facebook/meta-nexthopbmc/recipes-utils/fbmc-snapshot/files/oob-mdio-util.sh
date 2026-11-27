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

# The OOB switch hangs off MDIO bus 4 on the AST2600 and bus 1 on the
# AST2700.
if [ "$(cat /etc/soc_model 2>/dev/null)" = "SOC_MODEL_ASPEED_G7" ]; then
    OOB_MDIO_BUS="${OOB_MDIO_BUS:-1}"
else
    OOB_MDIO_BUS="${OOB_MDIO_BUS:-4}"
fi
# SMI device address of the switch and the internal SMI device address of
# port 0. Both are board straps that differ by switch family:
#   88E6191X (Amethyst family): PHY 0x1e, ports based at 0x0
#   88E6341  (Topaz family):    PHY 0xf,  ports based at 0x10
# When neither is set in the environment the switch is auto-detected by
# probing the Switch Identifier register of each known (PHY, base) pair.
OOB_PHY_ADDR="${OOB_PHY_ADDR:-}"
OOB_PORT_BASE="${OOB_PORT_BASE:-}"

# "<model> <product id> <phy addr> <port base> <max port>" per known switch.
OOB_KNOWN_SWITCHES="88E6191X 0x1920 0x1e 0x0 10
88E6341 0x3410 0xf 0x10 5"

mode=$1
port=$2
reg_addr=$3

usage() {
    echo "OOB MDIO Utility to access the Marvell OOB switch"
    echo "(88E6341 / 88E6191X, multi-chip SMI addressing)"
    echo "Usage:"
    echo "$0 read <port> <reg_addr> "
    echo "$0 write <port> <reg_addr> <data>"
    echo "$0 detect"
    echo "   prints '<model> <phy_addr> <port_base> <max_port>' for the"
    echo "   detected switch, e.g. '88E6191X 0x1e 0x0 10'"
    echo ""
    echo "Environment overrides (both must be set to skip auto-detection):"
    echo "  OOB_MDIO_BUS  (default 4, 1 on AST2700)  MDIO master bus"
    echo "  OOB_PHY_ADDR  switch SMI device address (0x1e or 0xf)"
    echo "  OOB_PORT_BASE internal SMI base addr of port 0 (0x0 or 0x10)"
}

# Wait for the SMI command register (reg 0) SMIBusy bit (bit 15) to clear
# before the data register is accessed. Reads are captured, not printed, so
# they do not pollute callers that parse this utility's output.
# $1 (optional): "quiet" suppresses the timeout error message.
oob_smi_wait_ready() {
    i=0
    while [ "$i" -lt 20 ]; do
        line=$(mdio-util -m "$OOB_MDIO_BUS" -p "$OOB_PHY_ADDR" -r 0x0 2>/dev/null)
        val=${line##* }
        if [ -n "$val" ] && [ $(( val & 0x8000 )) -eq 0 ]; then
            return 0
        fi
        i=$(( i + 1 ))
    done
    [ "$1" = "quiet" ] || echo "Error: timed out waiting for SMI ready" >&2
    return 1
}

# Read the Switch Identifier register (port 0, reg 0x3) at the given
# PHY address / port base and echo the product number (bits 15:4), or
# nothing if the device does not respond.
oob_probe_switch_id() {
    OOB_PHY_ADDR="$1"
    OOB_PORT_BASE="$2"
    # An absent device reads back all ones; bail out without burning the
    # SMI busy-wait on it.
    line=$(mdio-util -m "$OOB_MDIO_BUS" -p "$OOB_PHY_ADDR" -r 0x0 2>/dev/null)
    val=${line##* }
    [ -n "$val" ] && [ "$(( val ))" -ne 65535 ] || return 1
    ctrl_cmd=$(( 0x9000 | (0x2 << 10) | (OOB_PORT_BASE << 5) | 0x3 ))
    ctrl_cmd=$(printf "0x%08X" "$ctrl_cmd")
    mdio-util -m "$OOB_MDIO_BUS" -p "$OOB_PHY_ADDR" -w 0x0 -d "$ctrl_cmd" >/dev/null 2>&1
    oob_smi_wait_ready quiet || return 1
    line=$(mdio-util -m "$OOB_MDIO_BUS" -p "$OOB_PHY_ADDR" -r 0x1 2>/dev/null)
    val=${line##* }
    [ -n "$val" ] || return 1
    printf "0x%04x" "$(( val & 0xfff0 ))"
}

# Set OOB_SWITCH_MODEL, OOB_PHY_ADDR, OOB_PORT_BASE and OOB_MAX_PORT by
# probing each known switch. Returns 1 if no known switch answers.
oob_detect_switch() {
    echo "$OOB_KNOWN_SWITCHES" | while read -r model prod phy base max_port; do
        found=$(oob_probe_switch_id "$phy" "$base") || continue
        if [ "$found" = "$prod" ]; then
            echo "$model $phy $base $max_port"
            break
        fi
    done
}

oob_setup_switch() {
    if [ -n "$OOB_PHY_ADDR" ] && [ -n "$OOB_PORT_BASE" ]; then
        return 0
    fi
    if [ -n "$OOB_PHY_ADDR" ] || [ -n "$OOB_PORT_BASE" ]; then
        echo "Error: OOB_PHY_ADDR and OOB_PORT_BASE must be set together" >&2
        exit 1
    fi
    detected=$(oob_detect_switch)
    if [ -z "$detected" ]; then
        echo "Error: no known OOB switch found on MDIO bus $OOB_MDIO_BUS" >&2
        exit 1
    fi
    # Split "model addr base maxport" into positional parameters.
    # shellcheck disable=SC2086
    set -- $detected
    OOB_SWITCH_MODEL=$1
    OOB_PHY_ADDR=$2
    OOB_PORT_BASE=$3
    OOB_MAX_PORT=$4
}

if [ "$mode" = "detect" ]; then
    OOB_PHY_ADDR=""
    OOB_PORT_BASE=""
    oob_setup_switch
    echo "$OOB_SWITCH_MODEL $OOB_PHY_ADDR $OOB_PORT_BASE $OOB_MAX_PORT"
    exit 0
fi

if [ $# -lt 3 ]; then
    usage
    exit 1;
fi

if [ "$mode" = "write" ]; then
    if [ $# -ne 4 ]; then
        usage
        exit 1;
    fi
    oob_setup_switch
    ctrl_cmd=$(( 0x9000 | (0x1 << 10) | ( (OOB_PORT_BASE + port) << 5) | reg_addr ))
    ctrl_cmd=$(printf "0x%08X" "$ctrl_cmd")
    data=$4
    mdio-util -m "$OOB_MDIO_BUS" -p "$OOB_PHY_ADDR" -w 0x1 -d "$data"
    mdio-util -m "$OOB_MDIO_BUS" -p "$OOB_PHY_ADDR" -w 0x0 -d "$ctrl_cmd"
    oob_smi_wait_ready
elif [ "$mode" = "read" ]; then
    if [ $# -ne 3 ]; then
        usage
        exit 1;
    fi
    oob_setup_switch
    ctrl_cmd=$(( 0x9000 | (0x2 << 10) | ( (OOB_PORT_BASE + port) << 5) | reg_addr ))
    ctrl_cmd=$(printf "0x%08X" "$ctrl_cmd")
    mdio-util -m "$OOB_MDIO_BUS" -p "$OOB_PHY_ADDR" -w 0x0 -d "$ctrl_cmd"
    oob_smi_wait_ready
    mdio-util -m "$OOB_MDIO_BUS" -p "$OOB_PHY_ADDR" -r 0x1
else
    usage
    exit 1;
fi
