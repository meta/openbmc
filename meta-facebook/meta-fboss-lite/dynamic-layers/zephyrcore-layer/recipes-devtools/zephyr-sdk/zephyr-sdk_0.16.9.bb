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

SUMMARY = "Zephyr SDK Bundle"
DESCRIPTION = "Official SDK built using crosstool-ng, distributed by the \
Zephyr project"
COMPATIBLE_HOST = "(x86_64|aarch64).*-linux"

LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

INHIBIT_DEFAULT_DEPS = "1"
# CMake is required by the setup script
DEPENDS += "cmake"

SDK_ARCHIVE = "zephyr-sdk-${PV}_linux-${BUILD_ARCH}.tar.xz"
SDK_NAME = "${BUILD_ARCH}"
SRC_URI = "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${PV}/${SDK_ARCHIVE};name=${SDK_NAME}"

SRC_URI[x86_64.sha256sum] = "9b4b96f8df594801a84fce1aa112a84cdfcd430efc2a74229907b6421fb859a8"
SRC_URI[aarch64.sha256sum] = "a8c1731ad602553304e539a7ade9cdb34caabc1850d581585cbba0372a7493b2"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

ZEPHYR_SDK_DIR = "${prefix}/zephyr-sdk"

do_install() {
    install -d ${D}${prefix}
    cp -r ${UNPACKDIR}/zephyr-sdk-${PV} ${D}${ZEPHYR_SDK_DIR}
}

SYSROOT_DIRS += "${ZEPHYR_SDK_DIR}"
INHIBIT_SYSROOT_STRIP = "1"
BBCLASSEXTEND = "native"
