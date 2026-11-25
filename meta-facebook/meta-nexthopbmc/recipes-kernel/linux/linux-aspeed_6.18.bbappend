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

FILESEXTRAPATHS:prepend := "${THISDIR}/patches_6.18:"

#
# Include patches from nexthopbmc machine layer.
#
SRC_URI:append = " \
    file://1001-ARM-dts-aspeed-fblite-r1-fixup-device-settings.patch \
    file://1002-ARM-dts-aspeed-fblite-r1-add-TPM-reset-hog.patch \
    file://1003-ARM-dts-aspeed-fblite-r1-fix-usb-device.patch \
    file://1004-ARM-dts-aspeed-fblite-r1-Add-ssif.patch \
    file://1005-i2c-aspeed-Acknowledge-Tx-ack-late-when-in-SLAVE_REA.patch \
"

# The host CPU's SB-RMI (rev 0x10) only answers 1-byte register addresses.
# The common APML driver probes with a 2-byte address first, after which
# the 1-byte fallback fails as well.
SRC_URI:append = " \
    file://1030-misc-amd-apml-sbrmi-probe-1-byte-register-address-si.patch \
"
