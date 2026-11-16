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

#shellcheck disable=SC1091
#shellcheck disable=SC2034

# Do not change this line to openbmc-utils.sh, or it will generate a source loop.
. /usr/local/bin/i2c-utils.sh

MCBCPLD_SYSFS_DIR=$(i2c_device_sysfs_abspath 12-0060)
SCMCPLD_SYSFS_DIR=$(i2c_device_sysfs_abspath 1-0035)

BOARD_ID="${SCMCPLD_SYSFS_DIR}/board_id"
VERSION_ID="${SCMCPLD_SYSFS_DIR}/version_id"
COME_POWER_EN="${SCMCPLD_SYSFS_DIR}/pwr_come_en"
COME_POWER_OFF="${SCMCPLD_SYSFS_DIR}/pwr_force_off"
COME_SYSTEM_WARM_RESET="${SCMCPLD_SYSFS_DIR}/cb_sys_reset"
XP5P0_COME_PG="${SCMCPLD_SYSFS_DIR}/xp5p0_come_pg"
XP12P0_COME_PG="${SCMCPLD_SYSFS_DIR}/xp12p0_come_pg"
#According to the CPLD specification: timer base supportted 10ms 100ms 1s 10s
TIMER_BASE="${MCBCPLD_SYSFS_DIR}/timer_base_lsb"
TIMER_COUNTER_SETTING="${MCBCPLD_SYSFS_DIR}/timer_counter_setting"
TIMER_COUNTER_SETTING_UPDATE="${MCBCPLD_SYSFS_DIR}/timer_counter_setting_update"
declare -g PWR_OFF_CHECK_TOLERANCE=15
declare -g PWR_CYCLE_DEF_INTERVAL=0
TIMER_BASE_1S=0x04
TIMER_BASE_10S=0x08
CHASSIS_POWER_CYCLE="${MCBCPLD_SYSFS_DIR}/power_cycle_go"

# New projects need to fill in this PRJ_BOARD_DATA
# Each board is defined by its ID, with 'name' and 'revs' (revisions) properties. 
# Revisions are stored as a semicolon-separated string.

declare -A PRJ_BOARD_DATA=(
    [7,name]="TAHANSB800BC"
    [7,revs]="EVT1;EVT2A-EVT2D;EVT2E;DVT-1A/1B;PPVT;MP;RSVD"

    [8,name]="ICECUBE800BC"
    [8,revs]="Pre-EVT & EVT-1;EVT-2;EVT-3;DVT-1;DVT-2;PPVT;PVT;MP"

    [9,name]="ICETEA"
    [9,revs]="Pre-EVT & EVT-1;EVT-2A;EVT-2B/C;DVT-1A;DVT-1B;PPVT;PVT;MP"

    [13,name]="LADAKH800BCLS"
    [13,revs]="Pre-EVT & EVT-1;EVT-2A;EVT-2B/C;DVT-1A;DVT-1B;PPVT;PVT;MP"

    [14,name]="LEH800BCLS"
    [14,revs]="EVT-1A & EVT-1B;EVT-2;PPVT;PVT;MP"

    [15,name]="M4061CLSCL"
    [15,revs]="EVT1;EVT2;DVT1;DVT2;PPVT;PVT;MP"
)

wedge_board_type() {
    #check if the sysfs exist
    if [[ ! -f "$BOARD_ID" ]]; then
        echo "Error: BOARD_ID sysfs missing" >&2
        return 1
    fi
    local board_id_raw
    board_id_raw=$(head -n 1 < "$BOARD_ID" 2> /dev/null)
    board_id=$((board_id_raw))
    #check if a valid number 0--9
    if ! [[ "$board_id" =~ ^[0-9]+$ ]]; then
        return 1
    fi  
    local prj_name=${PRJ_BOARD_DATA[$board_id,name]}
    if [[ -z "$prj_name" ]]; then
        echo "Unknow Board ID [$board_id]"
        return 1
    fi
    echo "${prj_name}"
    return 0
}

wedge_board_rev() {
    #check if the sysfs exist
    if [[ ! -f "$BOARD_ID" || ! -f "$VERSION_ID" ]]; then
        echo "Error BOARD_ID or VERSION_ID sysfs missing" >&2
        return 1
    fi
    local board_id_raw
    local version_id_raw
    board_id_raw=$(head -n 1 < "$BOARD_ID" 2> /dev/null)
    version_id_raw=$(head -n 1 < "$VERSION_ID" 2> /dev/null)
    board_id=$((board_id_raw))
    version_id=$((version_id_raw))
    #check if a valid number 0--9
    if ! [[ "$board_id" =~ ^[0-9]+$ && "$version_id" =~ ^[0-9]+$ ]]; then
        return 1
    fi 
    local rev_string=${PRJ_BOARD_DATA[$board_id,revs]}
    if [[ -z "$rev_string" ]]; then
        echo "Error: Unknown Board ID [$board_id] or missing revision data" >&2
        return 1
    fi
    # Split the revision string into an array
    local IFS=';'
    read -r -a rev_array <<< "$rev_string"

    # Avoid array overflow
    if (( version_id < 0 || version_id >= ${#rev_array[@]} )); then
        local prj_name=${PRJ_BOARD_DATA[$board_id,name]}
        echo "Error: Unknown version ID [$version_id] for board [$prj_name ($board_id)]" >&2
        return 1
    fi
    echo "${rev_array[$version_id]}"
    return 0
}

wedge_board_type_rev() {
    board_type=$(wedge_board_type)
    board_rev=$(wedge_board_rev)

    if [ -z "$board_type" ] || [ -z "$board_rev" ]; then
        echo "Error: Unable to determine board type or revision!"
        return 1
    fi

    echo "${board_type}_${board_rev}"
}

userver_power_is_on() {
    if [ ! -e "$XP5P0_COME_PG" ] || [ ! -e "$XP12P0_COME_PG" ]; then
        echo "Error: $XP5P0_COME_PG or $XP12P0_COME_PG does not exist! Is scbcpld ready??"
        echo "Assuming uServer is off!"
        return 1
    fi

    xp5p0_sts=$(head -n 1 "$XP5P0_COME_PG" 2> /dev/null)
    xp12p0_sts=$(head -n 1 "$XP12P0_COME_PG" 2> /dev/null)

    if [ $((xp5p0_sts)) -eq $((0x1)) ] &&
       [ $((xp12p0_sts)) -eq $((0x1)) ] ; then
        return 0
    fi

    return 1
}

userver_power_on() {
    if ! sysfs_write "$COME_POWER_OFF" 1; then
        return 1
    fi
    if ! sysfs_write "$COME_POWER_EN" 1; then
        return 1
    fi

    # Wait for power good signal to be stable
    sleep 3
    return 0
}

userver_power_off() {
    if ! sysfs_write "$COME_POWER_OFF" 0; then
        return 1
    fi
}

userver_reset() {
    if ! sysfs_write "$COME_SYSTEM_WARM_RESET" 0; then
        return 1
    fi
    sleep 1
    if ! sysfs_write "$COME_SYSTEM_WARM_RESET" 1; then
        return 1
    fi

    return 0
}

chassis_power_cycle() {
    if ! sysfs_write "$CHASSIS_POWER_CYCLE" 1; then
        return 1
    fi

    return 0
}

bmc_mac_addr() {
    # Fetch mac addr supporting v5+ format.
    bmc_mac=$(weutil | sed -nE 's/BMC MAC Base: (.*)/\1/p')
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
    cpu_mac=$(weutil | sed -nE 's/X86 CPU MAC Base: (.*)/\1/p')
    if [ -z "$cpu_mac" ]; then
        echo "x86 CPU MAC Address Not Found !" 1>&2
        logger -p user.crit "x86 CPU MAC Address Not Found !"
        return 1
    else
        echo "$cpu_mac"
    fi
}

# This function is used to get PWR Cycle Timer settings from MCBCPLD
do_get_reset_timer_settings() {
    local reset_timer_base=0
    local reset_timer_counter_setting=0
    local timer_base=0
    reset_timer_base=$(head -n 1 < "$TIMER_BASE" 2> /dev/null)
    reset_timer_counter_setting=$(head -n 1 < "$TIMER_COUNTER_SETTING" 2> /dev/null)
    # hex to dec
    timer_base=$((timer_base))
    reset_timer_counter_setting=$((reset_timer_counter_setting))
    #10ms 100ms 1s 10s x100 avoid using float
    times=(1 10 100 1000)
    for i in 0 1 2 3; do
        # check if bit i == 1
        if (( (reset_timer_base >> i) & 0x01 )); then
            timer_base=$((timer_base + times[i]))
        fi
    done
    if [ "$timer_base" -eq 0 ] || [ "$reset_timer_counter_setting" -eq 0 ]; then
        return 1
    fi
    # ignore decimals
    PWR_CYCLE_DEF_INTERVAL=$(( (timer_base * reset_timer_counter_setting) / 100 + 1))
    export PWR_CYCLE_DEF_INTERVAL
    #echo "got pwr_cycle_def_interval: $PWR_CYCLE_DEF_INTERVAL"
    return 0
}

# This function is used to configure PWR Cycle Timer (0x20,0x21,0x22,0x23) of MCBCPLD to allow next power on with a time delay
do_config_reset_timer() {
    # get default time delay from CPLD
   if ! do_get_reset_timer_settings; then
        echo "Get reset timer setting of CPLD failed"
        logger -p user.crit "Get reset timer setting of CPLD failed"
   fi
    # Check numeric
    wake_t=$1
    echo "$wake_t" | grep -E -q '^[0-9]+$'
    ret=$?
    if [ $ret -ne 0 ]; then
        usage
        exit 1
    else
        if [ "$wake_t" -ge 0 ] && [ "$wake_t" -lt "$PWR_CYCLE_DEF_INTERVAL" ]; then
            # Handle delays less than the default CPLD interval
            if ! sysfs_write "$TIMER_BASE" "$TIMER_BASE_1S"; then
                return 1
            fi
            if ! sysfs_write "$TIMER_COUNTER_SETTING" "$wake_t"; then
                return 1
            fi
            echo "The default time delay in CPLD: $PWR_CYCLE_DEF_INTERVAL seconds, now overwritten"
        elif [ "$wake_t" -ge "$PWR_CYCLE_DEF_INTERVAL" ] && [ "$wake_t" -lt 250 ]; then
            # Handle delays less than 250 seconds
            if ! sysfs_write "$TIMER_BASE" "$TIMER_BASE_1S"; then
                return 1
            fi
            if ! sysfs_write "$TIMER_COUNTER_SETTING" "$wake_t"; then
                return 1
            fi
        elif [ "$wake_t" -ge 250 ] && [ "$wake_t" -le 2550 ]; then
            # Handle delays between 250 and 2550 seconds
            if ! sysfs_write "$TIMER_BASE" "$TIMER_BASE_10S"; then
                return 1
            fi
            local wake_t_div=$((wake_t/10))
            if ! sysfs_write "$TIMER_COUNTER_SETTING" "$wake_t_div"; then
                return 1
            fi
        else
            echo "The time delay parameter you entered: $wake_t is out of range (0--2550)"
            exit 1
        fi
        if ! sysfs_write "$TIMER_COUNTER_SETTING_UPDATE" 1; then
            return 1
        fi
    fi
    return 0
}
