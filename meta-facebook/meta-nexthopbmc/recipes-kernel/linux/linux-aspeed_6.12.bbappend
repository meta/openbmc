# Copyright 2020-present Facebook. All Rights Reserved.
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

FILESEXTRAPATHS:prepend := "${THISDIR}/patches_6.12:"

#
# Include patches from nexthopbmc machine layer.
#
SRC_URI:append = " \
    file://1001-ARM-dts-aspeed-fblite-r1-fixup-device-settings.patch \
    file://1002-ARM-dts-aspeed-fblite-r1-add-TPM-reset-hog.patch \
    file://1003-ARM-dts-aspeed-fblite-r1-fix-usb-device.patch \
    file://1004-ARM-dts-aspeed-fblite-r1-Add-ssif.patch \
    file://1005-i2c-aspeed-Acknowledge-Tx-ack-late-when-in-SLAVE_REA.patch \
    file://1007-ARM-dts-aspeed-fblite-r1-add-FPGA-on-SPI1-CS0.patch \
    file://1008-ARM-dts-aspeed-fblite-r1-add-BIOS-flash-on-SPI1-CS1.patch \
"

# Backported from upstream (v6.19): the SPI controller programs the CE
# address decoding ranges itself at probe instead of relying on U-Boot to
# have opened them. Needed for the BIOS flash on SPI1 CS1, whose decode
# range is closed at boot on the AST2620.
SRC_URI:append = " \
    file://1009-spi-aspeed-Force-default-address-decoding-range-assi.patch \
    file://1010-spi-aspeed-Centralize-address-decoding-region-manage.patch \
    file://1011-spi-aspeed-Add-per-platform-adjust_window-callback-f.patch \
    file://1012-spi-aspeed-Only-map-necessary-address-window-region.patch \
    file://1013-spi-aspeed-fix-spelling-mistake-triming-trimming.patch \
    file://1014-spi-aspeed-Fix-an-IS_ERR-vs-NULL-bug-in-probe.patch \
    file://1015-spi-aspeed-Use-devm_iounmap-to-unmap-devm_ioremap-me.patch \
"
