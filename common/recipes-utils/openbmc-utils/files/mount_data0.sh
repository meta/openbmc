#!/bin/sh
#
# Copyright 2014-present Facebook. All Rights Reserved.
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

### BEGIN INIT INFO
# Provides:          mount_data0
# Required-Start:    mountvirtfs
# Required-Stop:
# Default-Start:     S
# Default-Stop:
# Short-Description: Mount data0 partition from flash chip.
# Description:
### END INIT INFO

# shellcheck disable=SC1091
if [ -f /etc/default/rcS ]; then
    . /etc/default/rcS
fi

#
# The default filesystem type is jffs2, but people can override it by
# adding "mtd-ubifs" to MACHINE_FEATURES.
#
FLASH_FS_TYPE=jffs2

MOUNT_POINT="/mnt/data"

mtd_lookup_data_partition() {
    dev_data0=$(awk '{ if ($4 == "\"data0\"") print $1 }' /proc/mtd |
                cut -d ':' -f 1)
    if [ -n "$dev_data0" ]; then
        echo "$dev_data0"
    else
        dev_dataro=$(awk '{ if ($4 == "\"dataro\"") print $1 }' /proc/mtd |
                     cut -d ':' -f 1)
        echo "$dev_dataro"
    fi
}

#
# Sanity check if directories and files are corrupted after mount. The
# function can be called regardless of fs type.
#
mnt_point_health_check() {
    mnt_error=0
    mnt_point="$1"

    #
    # Failing to list root directory is definitely not acceptable.
    #
    if ! ls "$mnt_point" > /dev/null 2>&1; then
        echo "$FLASH_FS_TYPE health check error: unable to list root directory!"
        mnt_error=$((mnt_error+1))
    fi

    #
    # We've seen several instances of corrupted /mnt/data/etc on Minipack
    # and CMM BMC: somehow /mnt/data/etc is interpreted as a regular file
    # instead of a directory, and people lost connection to BMC in this
    # condition because sshd cannot be started.
    #
    persist_etc="${mnt_point}/etc"
    if [ -e "$persist_etc" ] && [ ! -d "$persist_etc" ] ; then
        echo "$FLASH_FS_TYPE health check error: $persist_etc corrupted (not a directory)!"
        mnt_error=$((mnt_error+1))
    fi

    return "$mnt_error"
}

get_reboot_cause_addr() {
    data_type=$1
    soc_model=$(cat /etc/soc_model)

    if [ "$soc_model" = "SOC_MODEL_ASPEED_G6" ]; then
        # ast_g6
        # sram reboot base:     0x10015000
        # sram reboot offset:   0xC00
        # cmd offset:           0x4
        # sig offset:           0x8
        if [ "$data_type" = "cmd" ]; then
            echo "0x10015C04"
        elif [ "$data_type" = "sig" ]; then
            echo "0x10015C08"
        fi
    else
        # ast_g5 & ast_g4
        # sram reboot base:     0x1E721000
        # sram reboot offset:   0x200
        # reboot cmd offset:    0x4
        # sig offset:           0x8
        if [ "$data_type" = "cmd" ]; then
            echo "0x1E721204"
        elif [ "$data_type" = "sig" ]; then
            echo "0x1E721208"
        fi
    fi
}

is_ubifs_error_set() {
    BOOT_MAGIC="0xFB420054"
    soc_model=$(cat /etc/soc_model 2>/dev/null)
    if [ "$soc_model" = "SOC_MODEL_ASPEED_G7" ]; then
        # No AST2700 mapping for the SRAM reboot-cause handoff is
        # established.
        return 1
    fi

    reboot_sig_addr=$(get_reboot_cause_addr sig)
    reboot_sig_val=$(devmem "$reboot_sig_addr" 2>/dev/null)
    if [ "$reboot_sig_val" != "$BOOT_MAGIC" ]; then
        # Power on reset
        return 1
    fi

    # Read reboot command flags
    reboot_cmd_addr=$(get_reboot_cause_addr cmd)
    reboot_cmd_flags=$(devmem "$reboot_cmd_addr" 2>/dev/null)
    if [ "$((reboot_cmd_flags & 0x4))" -eq 0 ]; then
        # UBIFS_ERROR not set
        return 1
    fi

    # UBIFS_ERROR is set
    return 0
}

ubifs_format() {
    mtd_chardev="$1"
    ubi_dev="$2"

    if ubinfo "$ubi_dev" > /dev/null; then
        if ! ubidetach -p "$mtd_chardev"; then
            return 1
        fi
    fi

    if ! ubiformat -y "$mtd_chardev"; then
        return 1
    fi

    if ! ubiattach -p "$mtd_chardev"; then
        return 1
    fi

    if ! ubimkvol "$ubi_dev" -N data0 -m; then
        return 1
    fi
}

ubifs_mount() {
    ubi_vol="$1"
    mnt_point="$2"

    echo "ubifs_mount $ubi_vol to $mnt_point.."
    mount -t ubifs "$ubi_vol" "$mnt_point" -o sync,compr=zstd
}

do_mount_ubifs() {
    mtd_chardev="$1"
    mnt_point="$2"
    ubi_dev="/dev/ubi0"
    ubi_vol="${ubi_dev}_0"
    need_recovery=0

    #
    # Blindly load ubi module just in case it's compiled as a module.
    # Ignore modprobe failures and we will run more check later.
    #
    modprobe ubi mtd=data0 > /dev/null 2>&1

    #
    # Attached ubi device manually if ubi is compiled into kernel but
    # "ubi.mtd=data0" bootargs is not supplied.
    #
    if ! [ -e "$ubi_dev" ]; then
        ubiattach -p "$mtd_chardev"
        # Fall through regardless of attach results: we will start
        # filesystem recovery if the ubi volume doesn't show up after
        # attach.
    fi

    #
    # ubifs "recovery" is needed when:
    #   1) the mtd partition is never formatted, for example, when people
    #      migrate a BMC from jffs2 to ubifs for the first time.
    #   2) the mtd partition or ubi volume is corrupted for some reasons.
    #
    if is_ubifs_error_set; then
        echo "UBIFS ERROR flag is set, start recovery.."
        need_recovery=1
    elif [ -e "$ubi_vol" ]; then
        device_size=$(ubinfo "$ubi_dev" | sed -n '/^Total amount of logical eraseblocks:/s/.*(\([0-9]*\) bytes.*/\1/p')
        volume_size=$(ubinfo "$ubi_vol" | sed -n '/^Size:/s/.*(\([0-9]*\) bytes.*/\1/p')
        if [ -n "$device_size" ] && [ -n "$volume_size" ] && [ $((volume_size+4194304)) -lt "$device_size" ]; then
            # If UBI device is larger than volume (+4MB to account for overhead), recreate the volume
            echo "$ubi_vol has size $volume_size, but device $ubi_dev has size $device_size. Recreating volume..."
            need_recovery=1
        elif ubifs_mount "$ubi_vol" "$mnt_point"; then
            echo "Check ubifs filesystem health on $ubi_vol.."

            if ! mnt_point_health_check "$mnt_point"; then
                need_recovery=$((need_recovery+1))
            fi

            if [ "$need_recovery" -gt 0 ]; then
                echo "ubifs health check failed. Unmount $ubi_vol and start recovery.."
                umount "$mnt_point"
                ubidetach -p "$mtd_chardev"
            else
                echo "ubifs health check passed: no error found."
            fi
        else
            echo "unable to mount $ubi_vol. Start recovery soon.."
            need_recovery=1
        fi
    else
        echo "$ubi_vol does not exist. Start recovery soon.."
        need_recovery=1
    fi

    if [ "$need_recovery" -gt 0 ]; then
        if ! ubifs_format "$mtd_chardev" "$ubi_dev"; then
            echo "ubifs_format failed. Exiting!"
            exit 1
        fi

        if ! ubifs_mount "$ubi_vol" "$mnt_point"; then
            echo "ubifs_mount failed after recovery. Exiting!"
            exit 1
        fi
    fi
}

jffs2_format() {
    mtd_chardev="$1"

    echo "Create jffs2 filesystem on $mtd_chardev.."
    flash_eraseall -j "$mtd_chardev"
}

jffs2_mount() {
    mtd_blkdev="$1"
    mnt_point="$2"

    echo "jffs2_mount $mtd_blkdev to $mnt_point.."
    mount -t jffs2 "$mtd_blkdev" "$mnt_point"
}

jffs2_health_check() {
    #
    # If JFFS2 prints below messages at mount time, then probably the
    # filesystem is corrupted. Although mount may still return "success",
    # the file structure/content can be corrupted in this case.
    # For details, please refer to:
    # http://www.linux-mtd.infradead.org/faq/jffs2.html
    #
    if dmesg | grep "jffs2.*: Magic bitmask.*not found" > /dev/null 2>&1; then
        echo "jffs2 health check error: <Magic bitmask not found> reported!"
        return 1
    fi

    return 0
}

do_mount_jffs2() {
    mtd_chardev="$1"
    mnt_point="$2"
    need_recovery=0

    #
    # "/proc/mtd" lists partitions using mtd#, but we need to use the
    # according "block" device (/dev/mtdblock#) due to mount.busybox
    # limitations.
    #
    mtd_blkdev="/dev/mtdblock${mtd_chardev#/dev/mtd}"

    if jffs2_mount "$mtd_blkdev" "$mnt_point"; then
        #
        # Wait a second to make sure filesystem is fully ready.
        #
        sleep 1
        echo "Check jffs2 filesystem health on $mtd_blkdev.."

        if ! jffs2_health_check; then
            need_recovery=$((need_recovery+1))
        fi
        if ! mnt_point_health_check "$mnt_point"; then
            need_recovery=$((need_recovery+1))
        fi

        if [ "$need_recovery" -gt 0 ]; then
            echo "jffs2 health check failed. Unmount $mtd_blkdev and start recovery.."
            umount "$mtd_blkdev"
        else
            echo "jffs2 health check passed: no error found."
        fi
    else
        echo "jffs2_mount $mtd_blkdev failed. Start recovery.."
        need_recovery=1
    fi

    if [ "$need_recovery" -gt 0 ]; then
        if ! jffs2_format "$mtd_chardev"; then
            echo "jffs2_format failed. Exiting!"
            exit 1
        fi

        if ! jffs2_mount "$mtd_blkdev" "$mnt_point"; then
            echo "jffs2_mount failed after recovery. Exiting!"
            exit 1
        fi
    fi
}

is_vboot_recovery_boot(){
   [ -x /usr/local/bin/vboot-util ] || return 1
   recv_boot=$(/usr/local/bin/vboot-util | awk -F: -e '/recovery_boot/{ print $2 }')
   test $(( "$recv_boot" )) -ne 0
}

if is_vboot_recovery_boot; then
    echo "Recovery boot, skip mounting /mnt/data"
    exit 1
fi

MTD_DATA_DEV=$(mtd_lookup_data_partition)
if [ -z "$MTD_DATA_DEV" ]; then
    echo "No data0/dataro partition found. $MOUNT_POINT not mounted."
    exit 1
fi

MTD_DATA_PATH="/dev/$MTD_DATA_DEV"
[ -d $MOUNT_POINT ] || mkdir -p $MOUNT_POINT

do_mount_$FLASH_FS_TYPE "$MTD_DATA_PATH" "$MOUNT_POINT"

: exit 0
