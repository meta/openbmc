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

# This utility prints the meta information for specified boot flash
# mtd partition
START_OFFSET_KB=960
LEN_KB=64

usage() {
    echo "Usage $0 [flash0|flash1]"
}

case "$1" in
    flash0|flash1)
        # Match the quoted name: an unquoted match also picks up
        # partitions such as "flash1-data0".
        mtd="$(grep "\"$1\"" /proc/mtd | awk '{print $1}' | tr -d ':')"
        ;;
    *)
        usage
        exit 1
        ;;
esac

if [ -z "$mtd" ]; then
    echo "No $1 partition in /proc/mtd!"
    exit 1
fi

if ! dd if="/dev/$mtd" of="/tmp/.${1}_meta" \
    bs=1K skip="$START_OFFSET_KB" count="$LEN_KB"; then
    echo "Failed to read metadata from /dev/$mtd" >&2
    exit 1
fi
# Images without an image-meta partition (e.g. yamp) have unrelated data
# here, which strings would print as noise.
if ! grep -q FBOBMC_IMAGE_META_VER /tmp/."$1"_meta; then
    echo "No image meta on $1"
    exit 0
fi
strings /tmp/."$1"_meta
