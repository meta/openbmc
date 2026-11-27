# Copyright (c) Nexthop Systems Inc
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

SUMMARY = "Marvell OOB Switch Diagnostics Utility"
DESCRIPTION = "Dump RMON counters from the Marvell OOB switch over MDIO"
SECTION = "base"
PR = "r1"
LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://oob-switch-util.c;beginline=11;endline=22;md5=92cb245af6c4a48a4efae48f2de9d5c0"

inherit meson pkgconfig

DEPENDS += "libmisc-utils"
RDEPENDS:${PN} += "libmisc-utils"
inherit legacy-packages

S = "${UNPACKDIR}"
LOCAL_URI = " \
    file://meson.build \
    file://oob-switch-util.c \
    "

pkgdir = "oob-switch-util"
