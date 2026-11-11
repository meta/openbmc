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

MCB_FPGA_I2C_BUS_NUM=12 # i2c-12 for Main Control Board FPGA
MCB_FPGA_I2C_BUS_OFFSET=0x72 # Address offset in the bus for Main Control Board FPGA

# Divide the register address value on FPGA spec by 4 to get the actual address
# I2C_BOARD_VERSION_REGISTER_ADDR=0x8
I2C_PWR_CTRL_REGISTER_ADDR=0xc
I2C_PWR_FAULT_STATUS_REGISTER_ADDR=0xe
I2C_PWR_GOOD_STATUS_REGISTER_ADDR=0xf

I2C_SEQ_IN_PROGRESS_BIT_MASK=0x80        # bit 7 (seq_inprog)
I2C_PWR_FAULT_FOR_CPU_DP_BIT_MASK=0x1c   # bit 2,3,4 (zone2b_fault,zone3a_fault,zone3b_fault)
I2C_PWR_GOOD_FOR_CPU_DP_BIT_MASK=0x1c    # bit 2,3,4 (zone2b_pwrgood,zone3a_pwrgood,zone3b_pwrgood)
# I2C_BOARD_VER_BIT_MASK=0xf               # bit 0~3 (brd_ver)

I2C_CPU_DP_PWR_OFF_VALUE=0x0     # 0b00000000, set 0 on bit cpu_datapath_pwr
I2C_CPU_DP_PWR_ON_VALUE=0x3      # 0b00000011, set 3 on bit cpu_datapath_pwr and cpu_pwr
I2C_CHASSIS_PWR_CYCLE_VALUE=0x4  # 0b00000100, set 1 on bit chassis_pwr_cycle

SEQ_TIMEOUT=15 # in seconds

# Standard Linux error code
EBUSY_ERR=16       # Device or resource busy
ETIME_ERR=62       # Timer expired
ENODEV_ERR=19      # No such device (wrong SCM card for this chassis)

WEUTIL_CMD="weutil -e"

# The SCM card variant is identified by a signature register in the SCM FPGA,
# which is reachable through the indirect address/data window on i2c-2. Only the
# SCM card designed for the MORGAN800CCX chassis returns SCM_AMD_FPGA_SIGNATURE;
# the MORGAN800CC SCM card does not.
SCM_FPGA_I2C_BUS_NUM=2         # i2c-2 for System Controller Module FPGA
SCM_FPGA_ID_REG_ADDR=0x10bc    # SCM variant signature register
SCM_AMD_FPGA_SIGNATURE=0x14f00000

lock_file_path_configure() {
    local lock_dir="/run/lock"
    local link_path="/var/lock"

    if [ ! -d "$lock_dir" ]; then
        if ! output=$(mkdir -p "$lock_dir"); then
            echo "Error: failed to create $lock_dir:"
            echo "$output"
            exit 1
        fi
    fi
    if [ ! -L "$link_path" ]; then
        if ! output=$(ln -s "$lock_dir" "$link_path"); then
            echo "Error: failed to create symlink $link_path:"
            echo "$output"
            exit 1
        fi
    fi
}

# the script may be triggered when "/var/lock" is not populated
lock_file_path_configure

LOCK_FILE="/var/lock/lockfile_for_board_util"

# Allocate file descriptor dynamically
exec {LOCK_FD}>"$LOCK_FILE"

write_i2c_register() {
    i2cset -f -y "$MCB_FPGA_I2C_BUS_NUM" "$MCB_FPGA_I2C_BUS_OFFSET" "$@"
}

write_i2c_pwr_ctrl_register() {
    write_i2c_register "$I2C_PWR_CTRL_REGISTER_ADDR" "$@"
}

read_i2c_register() {
    i2cget -f -y "$MCB_FPGA_I2C_BUS_NUM" "$MCB_FPGA_I2C_BUS_OFFSET" "$@"
}

# Function to acquire the file lock
acquire_lock() {
    local calling_func=$1  # The name of the calling function is passed as an argument

    flock -n $LOCK_FD || { log_err_message "$calling_func" "Another instance is running, exiting"; exit $EBUSY_ERR; }
}

# Function to release the file lock
release_lock() {
    flock -u $LOCK_FD
}

# Function to log error messages to syslog
log_err_message() {
    local func_name=$1
    local message=$2

    logger -p user.err "$func_name: $message"
    echo "$func_name: $message"
}

log_pwr_fault_if_exists() {
    local calling_func=$1
    local pwr_fault_status
    local pwr_good_status

    pwr_fault_status=$(read_i2c_register "$I2C_PWR_FAULT_STATUS_REGISTER_ADDR")

    if [ $((pwr_fault_status & I2C_PWR_FAULT_FOR_CPU_DP_BIT_MASK)) -ne 0 ]; then
        pwr_good_status=$(read_i2c_register "$I2C_PWR_GOOD_STATUS_REGISTER_ADDR")
        log_err_message "$calling_func" "Power fault happened, pwr_fault: $pwr_fault_status, pwr_good: $pwr_good_status"
    fi
}

handle_error_and_cleanup() {
    local calling_func=$1
    local error_message=$2
    local exit_code=$3

    log_err_message "$calling_func" "$error_message"
    log_pwr_fault_if_exists "$calling_func"
    release_lock
    exit "$exit_code"
}

# Check for power sequencing in progress and handle error if busy
handle_error_and_cleanup_if_busy() {
    local func_name=$1

    if is_sequencing; then
        handle_error_and_cleanup "$func_name" "Sequencing is in progress, exiting" $EBUSY_ERR
    fi
}

# Wait for sequencing to be done and handle error if timeout
handle_error_and_cleanup_if_timeout() {
    local calling_func=$1

    if ! wait_for_sequencing; then
        handle_error_and_cleanup "$calling_func" "Sequencing timeout, exiting" $ETIME_ERR
    else
        # Fault may happen even if sequencing is done within SEQ_TIMEOUT
        log_pwr_fault_if_exists "$calling_func"
    fi
}

# Check if it's in the process of power sequencing for either chassis, CPU or datapath
is_sequencing() {
    local state
    state=$(read_i2c_register "$I2C_PWR_CTRL_REGISTER_ADDR")

    if [ $((state & I2C_SEQ_IN_PROGRESS_BIT_MASK)) -eq $((I2C_SEQ_IN_PROGRESS_BIT_MASK)) ]; then
        return 0 # true
    else
        return 1 # false
    fi
}

# Wait for power sequencing to be done
# Return 0 if sequencing is done within SEQ_TIMEOUT, 1 if timeout
wait_for_sequencing() {
    for ((count=0; count < SEQ_TIMEOUT; count++)); do
        if is_sequencing; then
            sleep 1
        else
            return 0 # sequencing is done within SEQ_TIMEOUT
        fi
    done

    # Not able to finish sequencing within SEQ_TIMEOUT
    return 1 # timeout
}

wedge_board_type() {
    echo "morgan800cc"
}

wedge_board_rev() {
    board_rev=$($WEUTIL_CMD scm_eeprom|grep "Product Production State"|awk '{print $4}')
    case "$((board_rev))" in
        1)
            echo "EVT"
            ;;
        2)
            echo "DVT"
            ;;
        3)
            echo "PVT"
            ;;
        4)
            echo "MP"
            ;;
        *)
            echo "Revision: unknown value [$board_rev]"
            ;;
    esac
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

userver_power_is_on_helper() {
    local state
    state=$(read_i2c_register "$I2C_PWR_GOOD_STATUS_REGISTER_ADDR")

    if [ $((state & I2C_PWR_GOOD_FOR_CPU_DP_BIT_MASK)) -eq $((I2C_PWR_GOOD_FOR_CPU_DP_BIT_MASK)) ]; then
        return 0 # power is on
    else
        return 1 # power is off
    fi
}

userver_power_is_on() {
    if ! wait_for_sequencing; then
        log_err_message "${FUNCNAME[0]}" "Sequencing timeout, exiting"
        log_pwr_fault_if_exists "${FUNCNAME[0]}"
        exit $ETIME_ERR
    else
        # Fault may happen even if sequencing is done within SEQ_TIMEOUT
        log_pwr_fault_if_exists "${FUNCNAME[0]}"
    fi

    userver_power_is_on_helper
    return $?
}

wait_for_status_on() {
    # Note: In MCB FPGA (>= 0.16), there is a delay between power-good register
    # and the status register to reflect the power on status. To avoid this,
    # we increase upper bound of timeout to 5 seconds.
    for ((count=0; count < 50; count++)); do
        if ! userver_power_is_on_helper; then
            sleep 0.1
        else
            break
        fi
    done
}

userver_power_on() {
    acquire_lock "${FUNCNAME[0]}"

    # Refuse to power on if the SCM card is fitted into the wrong chassis.
    if ! check_scm_chassis_compatibility; then
        handle_error_and_cleanup "${FUNCNAME[0]}" \
            "SCM card is not compatible with this chassis, aborting power-on" $ENODEV_ERR
    fi

    handle_error_and_cleanup_if_busy "${FUNCNAME[0]}"

    # Power on CPU and Datapath
    write_i2c_pwr_ctrl_register "$I2C_CPU_DP_PWR_ON_VALUE"

    handle_error_and_cleanup_if_timeout "${FUNCNAME[0]}"

    wait_for_status_on

    release_lock
    return 0
}

# Read a 32-bit register from the SCM FPGA through its indirect
# address/data window (write the address, trigger, then read 4 data bytes).
# Prints the value as "0xXXXXXXXX".
scm_fpga_read32() {
    local addr=$1

    i2cset -f -y "$SCM_FPGA_I2C_BUS_NUM" 0x21 $(( addr        & 0xff ))
    i2cset -f -y "$SCM_FPGA_I2C_BUS_NUM" 0x22 $(( (addr >> 8)  & 0xff ))
    i2cset -f -y "$SCM_FPGA_I2C_BUS_NUM" 0x23 $(( (addr >> 16) & 0xff ))
    i2cset -f -y "$SCM_FPGA_I2C_BUS_NUM" 0x24 $(( (addr >> 24) & 0xff ))
    i2cset -f -y "$SCM_FPGA_I2C_BUS_NUM" 0x20 0x01

    printf "0x%02x%02x%02x%02x\n" \
        $(( $(i2cget -f -y "$SCM_FPGA_I2C_BUS_NUM" 0x28) )) \
        $(( $(i2cget -f -y "$SCM_FPGA_I2C_BUS_NUM" 0x27) )) \
        $(( $(i2cget -f -y "$SCM_FPGA_I2C_BUS_NUM" 0x26) )) \
        $(( $(i2cget -f -y "$SCM_FPGA_I2C_BUS_NUM" 0x25) ))
}

# Return the chassis Product Name (from the chassis EEPROM), e.g.
# "MORGAN800CCX" or "MORGAN800CC".
chassis_product_name() {
    weutil -e chassis_eeprom | awk -F': ' '/^Product Name:/ {print $2; exit}'
}

# Verify the fitted SCM card matches the chassis it is installed in.
# Return 0 if compatible, non-zero (and log) if the SCM card is wrongly fitted.
check_scm_chassis_compatibility() {
    local product_name scm_sig scm_is_amd

    product_name=$(chassis_product_name)
    scm_sig=$(scm_fpga_read32 "$SCM_FPGA_ID_REG_ADDR")

    if [ "$scm_sig" = "$SCM_AMD_FPGA_SIGNATURE" ]; then
        scm_is_amd=1
    else
        scm_is_amd=0
    fi

    case "$product_name" in
        MORGAN800CCX)
            if [ "$scm_is_amd" -ne 1 ]; then
                log_err_message "${FUNCNAME[0]}" \
                    "SCM card mismatch: MORGAN800CCX chassis with non-AMD SCM card (FPGA sig=$scm_sig)"
                return 1
            fi
            ;;
        MORGAN800CC)
            if [ "$scm_is_amd" -ne 0 ]; then
                log_err_message "${FUNCNAME[0]}" \
                    "SCM card mismatch: MORGAN800CC chassis with AMD SCM card (FPGA sig=$scm_sig)"
                return 1
            fi
            ;;
        *)
            log_err_message "${FUNCNAME[0]}" \
                "Unknown chassis Product Name '$product_name'; skipping SCM/chassis compatibility check"
            ;;
    esac

    return 0
}
userver_power_off() {
    acquire_lock "${FUNCNAME[0]}"

    handle_error_and_cleanup_if_busy "${FUNCNAME[0]}"

    # Power off CPU and Datapath
    write_i2c_pwr_ctrl_register "$I2C_CPU_DP_PWR_OFF_VALUE"

    handle_error_and_cleanup_if_timeout "${FUNCNAME[0]}"

    release_lock
    return 0
}

userver_reset() {
    acquire_lock "${FUNCNAME[0]}"

    handle_error_and_cleanup_if_busy "${FUNCNAME[0]}"

    # Power cycle CPU and Datapath by toggling the bit
    write_i2c_pwr_ctrl_register "$I2C_CPU_DP_PWR_OFF_VALUE"

    # Note: Setting upper bound power off timeout to 10 seconds
    # to be backward compatible to early MCB FPGA releases. Else the
    # sequencing is expected to complete within the value configured in
    # the by MCB FPGA.
    for ((count=0; count < 10; count++)); do
        if is_sequencing; then
            sleep 1
        else
            break
        fi
    done

    write_i2c_pwr_ctrl_register "$I2C_CPU_DP_PWR_ON_VALUE"

    handle_error_and_cleanup_if_timeout "${FUNCNAME[0]}"

    wait_for_status_on

    release_lock
    return 0
}

chassis_power_cycle() {
    acquire_lock "${FUNCNAME[0]}"

    handle_error_and_cleanup_if_busy "${FUNCNAME[0]}"

    # Power cycle chassis
    write_i2c_pwr_ctrl_register "$I2C_CHASSIS_PWR_CYCLE_VALUE"

    release_lock
    return 0
}

bmc_mac_addr() {
    weutil -e chassis_eeprom | grep 'BMC MAC Base:' | cut -d ' ' -f 4
}

userver_mac_addr() {
    weutil -e scm_eeprom | grep 'X86 CPU MAC Base:' | cut -d ' ' -f 5
}
