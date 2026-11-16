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

# shellcheck disable=SC1091
source /usr/local/bin/openbmc-utils.sh

VERSION="0.10"
COLLECTOR_TIMEOUT=180
failed_collectors=0

run_collector() {
    if timeout -k 5 "$COLLECTOR_TIMEOUT" "$@"; then
        return 0
    fi
    echo "ERROR: $* failed or timed out. Continuing..." >&2
    failed_collectors=$((failed_collectors + 1))
}

echo "#################################"
echo "##### SHOWTECH VERSION $VERSION #####"
echo "#################################"

echo -e "\n##### BMC SYSTEM TIME #####"
date

echo -e "\n##### BMC HOSTNAME #####"
hostname

echo -e "\n##### BMC UPTIME #####"
uptime

echo -e "\n##### BMC VERSION #####"
built=$(cat /etc/version)
echo "built : $built"

echo -e "\n##### BMC OS VERSION #####"
uname -a

bmc_version=$(cat /etc/issue)
echo "$bmc_version"

echo -e "\n##### All Firmware Versions #####"
run_collector fw-util all --version

echo -e "\n##### Aspeed Chip Revision #####"
aspeed_soc_chip_ver

echo -e "\n##### BMC Board Revision #####"
wedge_board_rev

echo -e  "\n################################"
echo "######## eMMC debug log ########"
echo "################################"
if [ ! -f /usr/local/bin/mmcraw ]; then
	echo "/usr/local/bin/mmcraw doesn't exist!"
else
	run_collector /usr/local/bin/mmcraw show-summary /dev/mmcblk0
	run_collector /usr/local/bin/mmcraw read-cid /dev/mmcblk0
fi

echo -e  "\n##### Executing plugins in /etc/showtech/rules/ #####"
for showtech_file in /etc/showtech/rules/*
do
	echo -e "\n##### Running $showtech_file #####"
	run_collector "$showtech_file"
done

echo "SHOWTECH_FAILED_COLLECTORS=$failed_collectors"
exit 0
