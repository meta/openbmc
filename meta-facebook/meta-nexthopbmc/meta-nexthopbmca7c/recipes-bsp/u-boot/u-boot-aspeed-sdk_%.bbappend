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

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append:nexthopbmca7c = " \
    file://nexthopbmca7c.cfg \
    file://0001-ARM-dts-aspeed-add-Nexthop-nexthopbmca7c-BMC.patch \
    "

# Put the image version in the U-Boot banner ("U-Boot 2023.10 <version>
# (<date>)"), which is where fbobmc-image reads fw_ver/uboot_ver for
# image-meta (fw-util bmc --version).
do_compile:prepend:nexthopbmca7c() {
    printf ' %s\n' "${OPENBMC_VERSION}" > "${S}/.scmversion"
    printf ' %s\n' "${OPENBMC_VERSION}" > "${B}/.scmversion"
}
