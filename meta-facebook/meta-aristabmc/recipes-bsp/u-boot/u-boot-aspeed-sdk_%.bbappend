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

# AST2700-specific U-Boot configuration

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:aristabmc = " \
    file://aristabmc.cfg \
    file://u-boot-env-ast2700-spi.txt \
    file://0001-ARM-uboot-dts-add-Facebook-aristabmc-dts.patch \
    file://0002-net-ftgmac100-Use-calibrated-enable-delay-for-AST270.patch \
    "

do_compile:prepend:aristabmc() {
    printf ' %s\n' "${OPENBMC_VERSION}" > "${S}/.scmversion"
    printf ' %s\n' "${OPENBMC_VERSION}" > "${B}/.scmversion"
}

# Build the initial SPI-NOR environment image.  This supplies the console
# settings on a freshly programmed/reset environment instead of relying on
# U-Boot's compiled-in defaults alone.
UBOOT_ENV_SIZE:aristabmc = "0x20000"
UBOOT_ENV:aristabmc = "u-boot-env"
UBOOT_ENV_SUFFIX:aristabmc = "bin"
UBOOT_ENV_TXT:aristabmc = "u-boot-env-ast2700-spi.txt"
