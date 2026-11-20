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

. /usr/local/bin/gpio-utils.sh

# Serializes userver power operations (on/off/reset) so concurrent callers
# don't interleave the power sequence on the CPE_CTRL GPIO.
USERVER_PWR_LOCK="/run/userver_pwr.lock"

# Standard Linux error codes used as exit codes (per FBOSS wedge_power.sh spec).
EBUSY_ERR=16     # Device or resource busy (power sequencing in progress)
EINVAL_ERR=22    # Invalid argument (reset requested while userver is off)

# The power control is edge triggered for powering off. It needs 2 falling edges
# to trigger.
USERVER_PWR_OFF_PULSE_COUNT=2       # Falling edges required to power off
USERVER_PWR_OFF_PULSE_GAP_SEC=0.005 # Minimum gap between level changes

# Time to wait after the pulse train for the userver to power down (and, on
# reset, before it is powered back on).
USERVER_PWR_OFF_WAIT_SEC=6

# This file is shared by the meta-nexthopbmc machines; the board compatible in
# the device tree tells them apart.
wedge_is_nexthopbmca7c() {
    grep -qs "facebook,nexthopbmca7c-bmc" /proc/device-tree/compatible
}

wedge_board_type() {
    if wedge_is_nexthopbmca7c; then
        echo 'nexthopbmca7c'
    else
        echo 'nexthopbmc'
    fi
}

wedge_board_rev() {
    # FIXME if needed.
    return 1
}

userver_power_is_on() {
    [ "$(gpio_get_value CPE_CTRL)" = "1" ]
}

# Generate USERVER_PWR_OFF_PULSE_COUNT falling edges on CPE_CTRL
userver_power_off_pulse() {
    local pulse
    for pulse in $(seq 1 "$USERVER_PWR_OFF_PULSE_COUNT"); do
        gpio_set_value CPE_CTRL 1
        sleep "$USERVER_PWR_OFF_PULSE_GAP_SEC"
        gpio_set_value CPE_CTRL 0
        if [ "$pulse" -lt "$USERVER_PWR_OFF_PULSE_COUNT" ]; then
            sleep "$USERVER_PWR_OFF_PULSE_GAP_SEC"
        fi
    done
}

userver_power_on() {
    (
        # Bail out if another power operation is already in progress.
        if ! flock -n 9; then
            echo "userver_power_on: power sequencing in progress, try again later" >&2
            exit $EBUSY_ERR
        fi

        # No-op if the userver is already powered on.
        if userver_power_is_on; then
            exit 0
        fi

        gpio_set_value CPE_CTRL 1
    ) 9>"$USERVER_PWR_LOCK"
}

userver_power_off() {
    (
        # Bail out if another power operation is already in progress.
        if ! flock -n 9; then
            echo "userver_power_off: power sequencing in progress, try again later" >&2
            exit $EBUSY_ERR
        fi

        # No-op if the userver is already powered off.
        if ! userver_power_is_on; then
            exit 0
        fi

        userver_power_off_pulse

        sleep "$USERVER_PWR_OFF_WAIT_SEC"
    ) 9>"$USERVER_PWR_LOCK"
}

userver_reset() {
    (
        # Bail out if another power operation is already in progress.
        if ! flock -n 9; then
            echo "userver_reset: power sequencing in progress, try again later" >&2
            exit $EBUSY_ERR
        fi

        # Reset is invalid while the userver is powered off.
        if ! userver_power_is_on; then
            echo "userver is off, please run <wedge_power.sh on> to power on userver" >&2
            exit $EINVAL_ERR
        fi

        userver_power_off_pulse

        sleep "$USERVER_PWR_OFF_WAIT_SEC"

        gpio_set_value CPE_CTRL 1
    ) 9>"$USERVER_PWR_LOCK"
}

chassis_power_cycle() {
    gpio_set_value BMC_PWR_CYC_REQ 1
}

bmc_mac_addr() {
    # Fetch mac addr supporting v5+ format.
    bmc_mac=$(weutil -e bmc_eeprom | sed -nE 's/BMC MAC Base: (.*)/\1/p')
    if [ -z "$bmc_mac" ]; then
        echo "BMC MAC Address Not Found !" 1>&2
        logger -p user.crit "BMC MAC Address Not Found !"
        return 1
    else
        echo "$bmc_mac"
    fi
}

userver_mac_addr() {
    # Fetch mac addr supporting v5+ format.
    # Check SCM EEPROM first and fall back to BMC EEPROM for older revs
    cpu_mac=$(weutil -e scm_eeprom 2>/dev/null | sed -nE 's/X86 CPU MAC Base: (.*)/\1/p')
    if [ -z "$cpu_mac" ]; then
        cpu_mac=$(weutil -e bmc_eeprom 2>/dev/null | sed -nE 's/X86 CPU MAC Base: (.*)/\1/p')
    fi
    if [ -z "$cpu_mac" ]; then
        echo "x86 CPU MAC Address Not Found !" 1>&2
        logger -p user.crit "x86 CPU MAC Address Not Found !"
        return 1
    else
        echo "$cpu_mac"
    fi
}
