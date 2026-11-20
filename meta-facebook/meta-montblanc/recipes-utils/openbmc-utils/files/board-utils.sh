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

#shellcheck disable=SC1091,SC2034
# Do not change this line to openbmc-utils.sh, or it will generate a source loop.
. /usr/local/bin/i2c-utils.sh

MCBCPLD_SYSFS_DIR=$(i2c_device_sysfs_abspath 12-0060)
SCMCPLD_SYSFS_DIR=$(i2c_device_sysfs_abspath 1-0035)

PWR_FORCE_OFF="${MCBCPLD_SYSFS_DIR}/pwr_force_off"
PWR_COME_EN="${MCBCPLD_SYSFS_DIR}/pwr_come_en"
PWR_COME_CYCLE_N="${MCBCPLD_SYSFS_DIR}/pwr_come_cycle_n"
PWR_COME_CYCLE_DEV_N="${MCBCPLD_SYSFS_DIR}/pwr_come_cycle_dev_n"

TIMER_BASE_10S="${MCBCPLD_SYSFS_DIR}/timer_base_10s"
TIMER_BASE_1S="${MCBCPLD_SYSFS_DIR}/timer_base_1s"
TIMER_BASE_100MS="${MCBCPLD_SYSFS_DIR}/timer_base_100ms"
TIMER_BASE_10MS="${MCBCPLD_SYSFS_DIR}/timer_base_10ms"
TIMER_COUNTER_SETTING="${MCBCPLD_SYSFS_DIR}/timer_counter_setting"
TIMER_COUNTER_SETTING_UPDATE="${MCBCPLD_SYSFS_DIR}/timer_counter_setting_update"
POWER_CYCLE_GO="${MCBCPLD_SYSFS_DIR}/power_cycle_go"

BOARD_ID="${MCBCPLD_SYSFS_DIR}/board_id"
VERSION_ID="${MCBCPLD_SYSFS_DIR}/version_id"

wedge_board_type() {
    echo 'MONTBLANC'
}

wedge_board_type_from_idprom() {
    product_name=$(weutil | grep 'Product Name' | cut -d ' ' -f3)
    product_name=${product_name%_*}

    echo "$product_name"
}

# read board type from chassis EEPROM and
# hardware revision from MCB CPLD register
wedge_board_rev() {
    board_type=$(wedge_board_type_from_idprom)
    if [ -z "$board_type" ]; then
        board_id=$(head -n 1 < "$BOARD_ID" 2> /dev/null)
        echo "Board type: unknown value [$board_id]"
    else
        echo "Board type: $board_type Switching System"
    fi

    version_id=$(head -n 1 < "$VERSION_ID" 2> /dev/null)
    case "$((version_id))" in
        0)
            echo "Revision: EVT-1"
            ;;
        1)
            echo "Revision: EVT-2"
            ;;
        2)
            echo "Revision: EVT-3"
            ;;
        3)
            echo "Revision: DVT-1"
            ;;
        4)
            echo "Revision: DVT-2"
            ;;
        5)
            echo "Revision: PVT"
            ;;
        6)
            echo "Revision: MP"
            ;;
        *) 
            echo "Revision: unknown value [$version_id]"
            ;;
    esac

    return 0
}

wedge_board_type_rev() {
    # Get the board type and revision
    output=$(wedge_board_rev)

    # Get board type from Board type: field
    board_type=$(echo "$output" | sed -nE 's/.*Board type: (\S+).*/\1/p')
    if [ -z "$board_type" ]; then
        echo "Error: failed to get board type"
        return 1
    fi

    # Get revision from Revision: field
    rev=$(echo "$output" | sed -nE 's/.*Revision: (.*)/\1/p')
    if [ -z "$rev" ]; then
        echo "Error: failed to get board revision"
        return 1
    fi

    echo "${board_type}_${rev}"
}

userver_power_is_on() {
    if [ ! -e "$PWR_FORCE_OFF" ] || [ ! -e "$PWR_COME_EN" ]; then
        echo "Error: $PWR_FORCE_OFF does not exist! Is mcbcpld ready??"
        echo "Assuming uServer is off!"
        return 1
    fi

    val1=$(head -n 1 < "$PWR_FORCE_OFF" 2> /dev/null)
    val2=$(head -n 1 < "$PWR_COME_EN" 2> /dev/null)

    if [ $((val1)) -eq $((0x1)) ] && [ $((val2)) -eq $((0x1)) ] ; then
        return 0            # powered on
    else
        return 1
    fi
}

userver_power_on() {
    if ! sysfs_write "$PWR_FORCE_OFF" 1; then
        return 1
    fi
    if ! sysfs_write "$PWR_COME_EN" 1; then
        return 1
    fi

    return 0
}

userver_power_off() {
    if ! sysfs_write "$PWR_FORCE_OFF" 0; then
        return 1
    fi

    return 0
}

userver_reset() {
    #
    # write 0 to trigger CPLD power cycling COMe
    # then this bit will auto set to 1 after Power cycle finish
    if ! sysfs_write "$PWR_COME_CYCLE_N" 0; then
        return 1
    fi
    
    timeout=10 #timeout 10 second
    until [[ $(head -n 1 < "$PWR_COME_CYCLE_N" 2>/dev/null) -eq 1 ]] \
        || [ $((timeout)) -lt 0 ]; do
        sleep 1
        timeout=$((timeout-1))
    done

    if [[ $(head -n 1 < "$PWR_COME_CYCLE_N" 2>/dev/null) -eq 0 ]]; then
        echo "Reset timed out" >&2
        return 1
    fi
}

userver_dev_reset(){
    #
    # write 0 to trigger CPLD power cycling COMe + I210 + SSD
    # then this bit will auto set to 1 after Power cycle finish
    #
    if ! sysfs_write "$PWR_COME_CYCLE_DEV_N" 0; then
        return 1
    fi

    timeout=10 #timeout 10 second
    until [[ $(head -n 1 < "$PWR_COME_CYCLE_DEV_N" 2>/dev/null) -eq 1 ]] \
        || [ $((timeout)) -lt 0 ]; do
        sleep 1
        timeout=$((timeout-1))
    done

    if [[ $(head -n 1 < "$PWR_COME_CYCLE_DEV_N" 2>/dev/null) -eq 0 ]]; then
        echo "Reset timed out" >&2
        return 1
    fi

    return 0
}

chassis_power_cycle() {
    # TIME_COUTER will be calculated 
    # based on register TIMER_BASE (10S,1S,100MS,10MS)
    #
    # eg. TIMER_BASE set as 100MS
    #     TIMER_COUNTER_SETTING set as 50
    #     timer is 50 x 100ms = 5 sec.
    #
    # Fall through on failures.
    #
    sysfs_write "$TIMER_BASE_10S" 0
    sysfs_write "$TIMER_BASE_1S" 0
    sysfs_write "$TIMER_BASE_100MS" 1
    sysfs_write "$TIMER_BASE_10MS" 0
    sysfs_write "$TIMER_COUNTER_SETTING" 50
    sysfs_write "$TIMER_COUNTER_SETTING_UPDATE" 1

    if ! sysfs_write "$POWER_CYCLE_GO" 1; then
        return 1
    fi

    return 0
}

bmc_mac_addr() {
    # Fetch mac addr supporting v4 and v5 format.
    weutil_output=$(weutil -e chassis_eeprom | sed -nE 's/((Local MAC)|(BMC MAC Base)): (.*)/\4/p')

    if [ -n "$weutil_output" ]; then
        # Mac address: xx:xx:xx:xx:xx:xx
        echo "$weutil_output" 
        return 0
    else
        echo "Cannot find out the BMC MAC" 1>&2
        return 1
    fi
}

userver_mac_addr() {
    # Fetch mac addr supporting v4 and v5 format.
    weutil_output=$(weutil -e scm_eeprom | sed -nE 's/((Local MAC)|(X86 CPU MAC Base)): (.*)/\4/p')

    if [ -n "$weutil_output" ]; then
        # Mac address: xx:xx:xx:xx:xx:xx
        echo "$weutil_output" 
        return 0
    else
        echo "Cannot find out the microserver MAC" 1>&2
        return 1
    fi
}
