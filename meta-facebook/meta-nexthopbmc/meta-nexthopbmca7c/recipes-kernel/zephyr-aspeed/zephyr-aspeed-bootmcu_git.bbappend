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

# 0001/0002 add the &sdrammc max-dram-size-mb cap the overlay sets: the
# BootMCU's wrap-around probe reports the board's 4 GiB as 8 GiB.
SRC_URI:append:nexthopbmca7c = " \
    file://ast2700_nexthopbmca7c_bootmcu.overlay \
    file://0001-dts-bindings-aspeed-ast27xx-sdrammc-add-max-dram-size-mb.patch;patchdir=zephyr \
    file://0002-mcu-runtime-sdram_ast2700-cap-the-detected-size.patch;patchdir=aspeed-zephyr-project \
"

EXTRA_OECMAKE:append:nexthopbmca7c = " -DEXTRA_DTC_OVERLAY_FILE=${UNPACKDIR}/ast2700_nexthopbmca7c_bootmcu.overlay"
