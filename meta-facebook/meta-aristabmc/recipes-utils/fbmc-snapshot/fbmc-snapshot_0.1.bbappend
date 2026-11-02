# Copyright 2026-present Facebook. All Rights Reserved.
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

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# fboss-lite keeps the common helpers out of fbmc-snapshot while a platform
# still ships show-tech.  Aristabmc migrates atomically, so it needs the
# helpers from fbmc-snapshot instead.
SHOWTECH_INSTALL_UTILS = "1"

LOCAL_URI += " \
    file://900_system_info.sh \
    file://901_platform_debug.sh \
    file://902_boot_info.sh \
    file://903_network_detail.sh \
    file://904_systemd.sh \
    file://905_dump_cpld.sh \
    "

SHOWTECH_RULES_FILES:append = " \
    900_system_info.sh \
    901_platform_debug.sh \
    902_boot_info.sh \
    903_network_detail.sh \
    904_systemd.sh \
    905_dump_cpld.sh \
    "

# mtd_metadata.sh overrides the common utility: Aristabmc stores image metadata
# at a platform-specific location.  oob-status.sh likewise overrides the
# fboss-lite version for the BCM53134 OOB switch.

# openbmc-utils owns the Aristabmc-specific MDIO helper used by oob-status.sh.
# Do not install the common Marvell helper as well, or rootfs construction sees
# two packages claim /usr/local/bin/oob-mdio-util.sh.
SHOWTECH_UTILS_FILES:remove = "oob-mdio-util.sh"
