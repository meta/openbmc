#!/bin/bash
#
# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
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
# Read, write, verify or erase the CPU BIOS SPI flash with flashrom over
# MTD. The flash is on SPI1 CS1 behind a mux in the CPU FPGA: assert
# FPGA_GRAB, point the mux at the BMC, bind spi1.1 to spi-nor, run
# flashrom, then undo in reverse. The x86 must be powered on (the flash is
# on its power domain) and idle on the flash. Leaving the mux at the BMC
# hangs the CPU on its next BIOS fetch; cleanup always restores it.
#
# "write" first patches the unit's SMBIOS strings into the image with
# DmiPatch.py, from the chassis EEPROM via weutil, carrying over the current
# flash's $DMI records where the EEPROM cannot supply them.

# shellcheck disable=SC1091
. /usr/local/bin/openbmc-utils.sh

PIDFILE="/var/run/bios_update.pid"

SPI_NOR_DRIVER_PATH="/sys/bus/spi/drivers/spi-nor"
BIOS_SPI_DEV="spi1.1"
BIOS_MTD_LABEL="bios"

FPGA_UTILS="/usr/local/bin/fpga_utils.py"
FPGA_REG_DEV="/dev/fpga_reg"
# CPU FPGA register and bit that point the BIOS SPI mux at the BMC.
FPGA_SPI_MUX_REG=0xc
FPGA_SPI_MUX_BIT=0x1
MUX_SETTLE_SEC=1

TMP_IMAGE="/tmp/bios_update_tmp_image"
TMP_DMI_IMAGE="/tmp/bios_update_dmi_image"
TMP_OLD_IMAGE="/tmp/bios_update_old_image"

# The vendor BIOS image ships with an empty $DMI store; a plain write would
# wipe the serial numbers programmed at manufacturing.
DMI_PATCH="/usr/local/bin/DmiPatch.py"
DMI_SYSTEM_EEPROM="chassis_eeprom"
# Units without a chassis EEPROM alias chassis_eeprom to this one, which
# describes the BMC card rather than the unit.
DMI_ALIAS_EEPROM="bmc_eeprom"
SKIP_DMI=0
DMI_EEPROM_COMPLETE=0
DMI_EEPROM_ALIASED=0
MUX_VAL_ORIG=""
GRABBED=0
FLASH_DIRTY=""

usage() {
    program=$(basename "$0")
    cat <<USAGE
Usage:
  $program write  <file>   patch SMBIOS DMI strings from weutil into <file>,
                           program it (padded with 0xff) and verify
  $program write --no-dmi <file>
                           program <file> as-is (no DMI patch)
  $program dmi    <file>   write the DMI-patched copy of <file> to <file>.dmi.bin
                           without flashing (does not touch the flash)
  $program read   <file>   dump the whole flash to <file>
  $program verify <file>   compare the flash against <file>
  $program erase           erase the whole flash
  $program id              print the flash JEDEC id and MTD size

The x86 must be powered on (the BIOS flash is on its power domain) and idle
on the flash: booted into its OS, never sitting in BIOS or mid-boot. Power
it on with "wedge_power.sh on" and wait for the OS before running this.
USAGE
}

log()  { echo "[bios_update] $*"; }
warn() { echo "[bios_update] WARNING: $*" >&2; }
die()  { echo "[bios_update] ERROR: $*" >&2; exit 1; }

check_duplicate_process() {
    exec 9>"$PIDFILE"
    flock -n 9 || die "another BIOS update process is running"
    echo $$ 1>&9
}

fpga_read_reg() {
    "$FPGA_UTILS" --fpga -r "$1" | awk '{print $2}'
}

fpga_write_reg() {
    "$FPGA_UTILS" --fpga -w "$1" "$(printf 0x%08X "$2")" > /dev/null
}

x86_power_check() {
    userver_power_is_on || \
        die "x86 is powered off; the BIOS flash is on the x86 power domain. Run \"wedge_power.sh on\", wait for the OS to boot, then retry."
    log "x86 is powered on; it must be idle on the flash (booted into its OS)"
}

fpga_grab_bios_flash() {
    [ -c "$FPGA_REG_DEV" ] || die "$FPGA_REG_DEV not found: is the nh_fpga_reg driver loaded?"
    [ -d "/sys/bus/spi/devices/$BIOS_SPI_DEV" ] || \
        die "$BIOS_SPI_DEV not registered: kernel lacks the SPI1 CS1 flash node or the spi-aspeed-smc decode range backport"

    log "asserting FPGA_GRAB"
    gpio_set_value FPGA_GRAB 1
    GRABBED=1

    MUX_VAL_ORIG=$(fpga_read_reg "$FPGA_SPI_MUX_REG")
    [ -n "$MUX_VAL_ORIG" ] || die "cannot read FPGA reg $FPGA_SPI_MUX_REG"
    log "pointing BIOS SPI mux at the BMC (FPGA reg $FPGA_SPI_MUX_REG: $MUX_VAL_ORIG)"
    fpga_write_reg "$FPGA_SPI_MUX_REG" $((MUX_VAL_ORIG | FPGA_SPI_MUX_BIT))
    rb=$(fpga_read_reg "$FPGA_SPI_MUX_REG")
    [ $((rb & FPGA_SPI_MUX_BIT)) -ne 0 ] || die "mux bit did not set (read $rb)"
    sleep "$MUX_SETTLE_SEC"
}

fpga_release_bios_flash() {
    [ "$GRABBED" = 1 ] || return
    if [ -c "$FPGA_REG_DEV" ] && [ -n "$MUX_VAL_ORIG" ]; then
        fpga_write_reg "$FPGA_SPI_MUX_REG" $((MUX_VAL_ORIG & ~FPGA_SPI_MUX_BIT))
        rb=$(fpga_read_reg "$FPGA_SPI_MUX_REG")
        # Empty readback would look like a cleared bit; report it as unknown.
        if [ -z "$rb" ]; then
            warn "cannot read back FPGA reg $FPGA_SPI_MUX_REG; mux state unknown. If the CPU hangs on BIOS access, recover with a PDU power cycle."
        elif [ $((rb & FPGA_SPI_MUX_BIT)) -ne 0 ]; then
            warn "mux still at BMC ($rb); CPU will hang on BIOS access. Recover with a PDU power cycle."
        else
            log "BIOS SPI mux returned to the CPU ($rb)"
        fi
    fi
    gpio_set_value FPGA_GRAB 0
    GRABBED=0
}

bind_bios_flash() {
    if [ -e "${SPI_NOR_DRIVER_PATH}/${BIOS_SPI_DEV}" ]; then
        echo "$BIOS_SPI_DEV" > "${SPI_NOR_DRIVER_PATH}/unbind"
    fi
    echo "$BIOS_SPI_DEV" > "${SPI_NOR_DRIVER_PATH}/bind" || die "spi-nor probe of $BIOS_SPI_DEV failed (see dmesg)"
    sleep 1
    dmesg | grep -E "spi-nor $BIOS_SPI_DEV:" | tail -n 1
}

unbind_bios_flash() {
    if [ -e "${SPI_NOR_DRIVER_PATH}/${BIOS_SPI_DEV}" ]; then
        echo "$BIOS_SPI_DEV" > "${SPI_NOR_DRIVER_PATH}/unbind" 2>/dev/null
    fi
}

on_signal() {
    if [ -n "$FLASH_DIRTY" ]; then
        warn "interrupted while $FLASH_DIRTY. Do not boot the x86 from it; re-run the write before releasing it."
    else
        warn "interrupted; flash contents untouched"
    fi
    exit 130
}

cleanup() {
    unbind_bios_flash
    fpga_release_bios_flash
    rm -f "$TMP_IMAGE" "$TMP_DMI_IMAGE" "$TMP_OLD_IMAGE"
}

bios_mtd_index() {
    grep "\"${BIOS_MTD_LABEL}\"" /proc/mtd | cut -d':' -f1 | sed 's/mtd//'
}

# Copy $1 to $TMP_IMAGE padded with 0xff to the size of mtd$2.
pad_image() {
    file=$1
    mtd_index=$2
    targetsize=$(flashrom -p "linux_mtd:dev=${mtd_index}" --flash-size | tail -n 1)
    case "$targetsize" in
        ''|*[!0-9]*) die "cannot determine flash size (flashrom --flash-size returned \"$targetsize\")" ;;
    esac
    filesize=$(stat -c%s "$file")
    [ "$filesize" -le "$targetsize" ] || die "image ($filesize bytes) is larger than flash ($targetsize bytes)"
    cp "$file" "$TMP_IMAGE" || die "cannot copy $file to $TMP_IMAGE"
    if [ "$filesize" -lt "$targetsize" ]; then
        dd if=/dev/zero bs=$((targetsize - filesize)) count=1 2>/dev/null \
            | tr "\000" "\377" >> "$TMP_IMAGE"
    fi
    [ "$(stat -c%s "$TMP_IMAGE")" -eq "$targetsize" ] || die "padded image is not $targetsize bytes"
}

# weutil field $2 of EEPROM $1, or nothing.
weutil_field() {
    weutil -e "$1" 2>/dev/null | sed -nE "s/^$2: *(.*[^ ]) *\$/\1/p" | head -n 1
}

# sysfs path weutil uses for EEPROM $1, or nothing.
weutil_sysfs_path() {
    weutil -l 2>/dev/null | awk -v n="$1" '$1 == n { print $2; exit }'
}

# Build the DmiPatch.py field options into the DMI_ARGS array. Leaves it
# empty with DMI_EEPROM_ALIASED=1 when the chassis EEPROM is really the BMC's.
dmi_collect_args() {
    DMI_ARGS=()
    DMI_EEPROM_COMPLETE=0
    DMI_EEPROM_ALIASED=0
    sys_eeprom=$DMI_SYSTEM_EEPROM
    sys_path=$(weutil_sysfs_path "$sys_eeprom")
    alias_path=$(weutil_sysfs_path "$DMI_ALIAS_EEPROM")
    if [ -n "$sys_path" ] && [ "$sys_path" = "$alias_path" ]; then
        log "$sys_eeprom is an alias of $DMI_ALIAS_EEPROM ($sys_path): no chassis EEPROM fitted, SMBIOS strings not derived from it"
        DMI_EEPROM_ALIASED=1
        return
    fi
    product=$(weutil_field "$sys_eeprom" "Product Name")
    [ -n "$product" ] || die "weutil -e $sys_eeprom gives no Product Name; cannot derive SMBIOS strings (use --no-dmi to flash anyway)"
    serial=$(weutil_field "$sys_eeprom" "Product Serial Number")
    [ -n "$serial" ] && DMI_EEPROM_COMPLETE=1
    # EEPROM format v6 has no Product Version; use Production Sub-State.
    version=$(weutil_field "$sys_eeprom" "Production Sub-State")

    # System, baseboard and chassis product are all the unit's product name;
    # the system SKU is left "Not Specified" on purpose.
    DMI_ARGS+=(-SP "$product" -BP "$product" -CSKU "$product")
    [ -n "$serial" ]  && DMI_ARGS+=(-SS "$serial" -CS "$serial")
    [ -n "$version" ] && DMI_ARGS+=(-SV "$version" -CV "$version")
}

# Write a DMI-patched copy of image $1 to $2. When $3 is an MTD index and
# the EEPROM lacks product or serial, the current flash's $DMI records are
# carried over; without a chassis EEPROM they are the only source.
dmi_patch_image() {
    src=$1
    out=$2
    mtd_index=$3
    [ -x "$DMI_PATCH" ] || die "$DMI_PATCH not found"
    dmi_collect_args
    [ "$DMI_EEPROM_ALIASED" = 1 ] || log "SMBIOS strings from weutil: ${DMI_ARGS[*]}"

    from_args=()
    if [ "$DMI_EEPROM_ALIASED" = 1 ]; then
        [ -n "$mtd_index" ] || die "no chassis EEPROM and no flash to carry records over from; nothing to write into $src"
        log "dumping current BIOS flash (mtd$mtd_index) to carry over its DMI records unchanged"
        flashrom -p "linux_mtd:dev=${mtd_index}" -r "$TMP_OLD_IMAGE" > /dev/null 2>&1 \
            || die "cannot read the current BIOS flash; the SMBIOS strings would be lost (use --no-dmi to flash anyway)"
        "$DMI_PATCH" "$TMP_OLD_IMAGE" --show > /dev/null 2>&1 \
            || die "current flash has no readable \$DMI store and there is no chassis EEPROM; the SMBIOS strings would be lost (use --no-dmi to flash anyway)"
        from_args=(--from "$TMP_OLD_IMAGE")
    elif [ -n "$mtd_index" ] && [ "$DMI_EEPROM_COMPLETE" = 1 ]; then
        log "EEPROM supplied product and serial; not carrying over records from the current flash"
    elif [ -n "$mtd_index" ]; then
        log "EEPROM lacks the serial; dumping current BIOS flash (mtd$mtd_index) to carry over its DMI records"
        if flashrom -p "linux_mtd:dev=${mtd_index}" -r "$TMP_OLD_IMAGE" > /dev/null 2>&1 \
            && "$DMI_PATCH" "$TMP_OLD_IMAGE" --show > /dev/null 2>&1; then
            from_args=(--from "$TMP_OLD_IMAGE")
        else
            warn "current flash has no readable \$DMI store (blank or foreign image); only the weutil fields will be set"
        fi
    fi

    "$DMI_PATCH" "$src" "${from_args[@]}" "${DMI_ARGS[@]}" -o "$out" \
        || die "DmiPatch.py failed on $src"
}

do_write() {
    if [ "$SKIP_DMI" = 1 ]; then
        log "--no-dmi: flashing $1 without patching SMBIOS strings"
        pad_image "$1" "$2"
    else
        dmi_patch_image "$1" "$TMP_DMI_IMAGE" "$2"
        pad_image "$TMP_DMI_IMAGE" "$2"
    fi
    log "writing $1 to BIOS flash (mtd$2) with verification"
    FLASH_DIRTY="writing $1; the BIOS flash may be partially programmed"
    flashrom -p "linux_mtd:dev=$2" -w "$TMP_IMAGE" || die "flashrom write/verify failed"
    FLASH_DIRTY=""
    log "write verified"
}

do_verify() {
    pad_image "$1" "$2"
    log "verifying BIOS flash (mtd$2) against $1"
    flashrom -p "linux_mtd:dev=$2" -v "$TMP_IMAGE" || die "flashrom verify failed"
    log "verify OK"
}

do_read() {
    log "reading BIOS flash (mtd$2) to $1"
    flashrom -p "linux_mtd:dev=$2" -r "$1" || die "flashrom read failed"
    log "read complete: $(sha256sum "$1")"
}

do_erase() {
    log "erasing BIOS flash (mtd$1)"
    FLASH_DIRTY="erasing; the BIOS flash may be partially erased"
    flashrom -p "linux_mtd:dev=$1" -E || die "flashrom erase failed"
    FLASH_DIRTY=""
}

do_id() {
    devdir="/sys/bus/spi/devices/$BIOS_SPI_DEV"
    # spi-nor publishes its attributes in a "spi-nor" group; manufacturer is
    # absent for parts identified from SFDP alone.
    jedec=$(cat "$devdir/spi-nor/jedec_id" 2>/dev/null)
    part=$(cat "$devdir/spi-nor/partname" 2>/dev/null)
    mfr=$(cat "$devdir/spi-nor/manufacturer" 2>/dev/null)
    size=$(cat "$devdir/mtd/mtd$1/size" 2>/dev/null)
    log "JEDEC id ${jedec:-unknown} (${mfr:+$mfr }${part:-unknown part})"
    log "mtd$1 \"$BIOS_MTD_LABEL\": ${size:-?} bytes"
}

op=$1
shift
if [ "$op" = write ] && [ "$1" = "--no-dmi" ]; then
    SKIP_DMI=1
    shift
fi
file=$1

case "$op" in
    dmi)
        [ -f "$file" ] || die "image file '$file' not found"
        dmi_patch_image "$file" "$file.dmi.bin" ""
        exit 0
        ;;
    write|verify)
        [ -f "$file" ] || die "image file '$file' not found"
        ;;
    read)
        [ -n "$file" ] || { usage; exit 1; }
        ;;
    erase|id)
        ;;
    *)
        usage
        exit 1
        ;;
esac

check_duplicate_process
trap cleanup EXIT
trap on_signal INT TERM QUIT HUP

x86_power_check
fpga_grab_bios_flash
bind_bios_flash

mtd_index=$(bios_mtd_index)
[ -n "$mtd_index" ] || die "BIOS flash MTD device (label \"$BIOS_MTD_LABEL\") not found"

case "$op" in
    write)  do_write "$file" "$mtd_index" ;;
    verify) do_verify "$file" "$mtd_index" ;;
    read)   do_read "$file" "$mtd_index" ;;
    erase)  do_erase "$mtd_index" ;;
    id)     do_id "$mtd_index" ;;
esac
