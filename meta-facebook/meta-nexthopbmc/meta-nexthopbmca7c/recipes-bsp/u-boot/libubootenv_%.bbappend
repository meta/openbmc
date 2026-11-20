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

# fw_printenv/fw_setenv configuration.  The image leaves the environment
# blank, so U-Boot boots on its compiled-in defaults; u-boot-initial-env
# seeds the first fw_setenv with what the SPI boot needs (fdtspiaddr is set
# by U-Boot at run time; baudrate and bootdelay fall back to the config).
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:nexthopbmca7c = " \
    file://fw_env.config \
    file://u-boot-initial-env \
    "

do_install:append:nexthopbmca7c() {
    install -d ${D}${sysconfdir}
    install -m 0644 ${UNPACKDIR}/fw_env.config ${D}${sysconfdir}/fw_env.config
    install -m 0644 ${UNPACKDIR}/u-boot-initial-env ${D}${sysconfdir}/u-boot-initial-env
}

FILES:${PN}:append:nexthopbmca7c = " ${sysconfdir}/fw_env.config ${sysconfdir}/u-boot-initial-env"
