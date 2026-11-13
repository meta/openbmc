#!/bin/bash
#
# Copyright (c) Nexthop Systems Inc.
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
# Read, write or verify the FPGA configuration flash. The flash sits behind
# an SPI master inside the FPGA, reached through /dev/fpga_reg; FPGA_GRAB
# must be asserted for the register bridge to respond. fpga_utils.py does
# the flash access, this script owns the grab and locking.
#
# No standalone erase: the flash is only reachable through the running FPGA
# image, so it is only ever erased as the first step of a write.

# shellcheck disable=SC1091
. /usr/local/bin/openbmc-utils.sh

PIDFILE="/var/run/fpga_update.pid"
BIOS_PIDFILE="/var/run/bios_update.pid"
FPGA_UTILS="/usr/local/bin/fpga_utils.py"
FPGA_REG_DEV="/dev/fpga_reg"

FLASH_DIRTY=""
GRABBED=0
CHILD=""

usage() {
    program=$(basename "$0")
    cat <<USAGE
Usage:
  $program write  <file>   erase the flash, program <file> and verify
  $program read   <file>   dump the whole flash to <file>
  $program verify <file>   compare the flash against <file>
  $program id              print the flash JEDEC id
  $program version         print the running FPGA image version (register 0)

The FPGA keeps running its current image until it is reconfigured
(power cycle). Do not reconfigure from a flash that failed verification.
USAGE
}

log()  { echo "[fpga_update] $*"; }
warn() { echo "[fpga_update] WARNING: $*" >&2; }
die()  { echo "[fpga_update] ERROR: $*" >&2; exit 1; }

check_duplicate_process() {
    exec 9>"$PIDFILE"
    flock -n 9 || die "another FPGA update process is running"
    echo $$ 1>&9
}

# bios_update.sh uses the same register bridge; exclude it both ways.
check_bios_update_idle() {
    exec 8>"$BIOS_PIDFILE"
    flock -n 8 || die "bios_update.sh is running; wait for it to finish before running $(basename "$0")"
}

# Run fpga_utils.py as a tracked child so on_signal can stop it.
run_utils() {
    "$FPGA_UTILS" "$@" &
    CHILD=$!
    wait "$CHILD"
    rc=$?
    CHILD=""
    return $rc
}

fpga_grab() {
    warn "the x86 loses access to the CPU FPGA (including its I2C buses) until this finishes; the x86 kernel may log I2C timeouts"
    log "asserting FPGA_GRAB"
    gpio_set_value FPGA_GRAB 1
    GRABBED=1
    sleep 0.2
}

cleanup() {
    [ "$GRABBED" = 1 ] || return
    gpio_set_value FPGA_GRAB 0
    GRABBED=0
    log "FPGA_GRAB released"
}

on_signal() {
    if [ -n "$CHILD" ]; then
        kill "$CHILD" 2>/dev/null
        wait "$CHILD" 2>/dev/null
    fi
    if [ -n "$FLASH_DIRTY" ]; then
        warn "interrupted while $FLASH_DIRTY. Do not reconfigure the FPGA from it; re-run the write first."
    else
        warn "interrupted; flash contents untouched"
    fi
    exit 130
}

do_write() {
    FLASH_DIRTY="writing the flash"
    run_utils --flash --program "$1" || die "program failed"
    FLASH_DIRTY=""
    log "write and verify OK"
}

do_verify() {
    run_utils --flash --verify "$1" || die "verify failed"
}

do_read() {
    run_utils --flash --dump "$1" || die "read failed"
}

do_version() {
    v=$(run_utils --fpga -r 0 | awk '{print $2}')
    [ -n "$v" ] || die "cannot read FPGA register 0"
    echo "$v"
}

do_id() {
    run_utils --flash --jedec || die "cannot read JEDEC id"
}

if [ $# -lt 1 ]; then
    usage
    exit 1
fi

op=$1
file=$2

case "$op" in
    write|verify)
        [ -f "$file" ] || die "image file '$file' not found"
        ;;
    read)
        [ -n "$file" ] || { usage; exit 1; }
        ;;
    id|version)
        ;;
    erase)
        die "erase is not supported: the flash is only reachable through the running FPGA image. Use \"write <file>\" to replace it."
        ;;
    *)
        usage
        exit 1
        ;;
esac

[ -c "$FPGA_REG_DEV" ] || die "$FPGA_REG_DEV not present (nh_fpga_reg driver not loaded?)"

check_duplicate_process
check_bios_update_idle

trap cleanup EXIT
trap on_signal INT TERM QUIT HUP

fpga_grab

case "$op" in
    write)   do_write "$file" ;;
    verify)  do_verify "$file" ;;
    read)    do_read "$file" ;;
    id)      do_id ;;
    version) do_version ;;
esac
