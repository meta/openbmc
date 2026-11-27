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

require recipes-kernel/zephyr-kernel/zephyr-image.inc
require zephyr-aspeed-src.inc
require zephyr-aspeed-project-src.inc

SUMMARY = "BootMCU runtime firmware"
PACKAGE_ARCH = "${MACHINE_ARCH}"

PROVIDES += "virtual/bootmcu"
PV = "1.0+git"

ZEPHYR_BOARD_BOOTMCU ??= "ast2700_evb/ast2700/bootmcu"
ZEPHYR_BOARD = "${ZEPHYR_BOARD_BOOTMCU}"
ZEPHYR_SRC_DIR ??= "${S}/aspeed-zephyr-project/apps/mcu-runtime"
ASPEED_ZEPHYR_PROJECT_SUBMODULE_DTC = "1"
ZEPHYR_ASPEED_OUTPUT = "${BOOTMCU_FMC_BINARY} ${BOOTMCU_FW_BINARY}"

DEPENDS += "${@bb.utils.contains('MACHINE_FEATURES', 'ast-secure', 'aspeed-secure-config-native', '', d)}"

# Use fmc-imgtool to create fmc image since A1
do_create_fmc_image() {
    export OPENSSL_MODULES="${STAGING_LIBDIR_NATIVE}/ossl-modules"

    local ecc_key=""
    local ecc_key_index=""
    local lms_key=""
    local lms_key_index=""
    local sign_args=""

    if [ "${FMC_IMAGE_ENABLE}" != "1" ]; then
        install -m 0644 ${B}/zephyr/zephyr.bin ${B}/zephyr/${BOOTMCU_FW_BINARY}
        return
    fi

    if [ -f "${FMC_ECC_KEY}" ]; then
        ecc_key="--ecc-key ${FMC_ECC_KEY}"
    fi

    if [ -n "${FMC_ECC_KEY_INDEX}" ]; then
        ecc_key_index="--ecc-key-index ${FMC_ECC_KEY_INDEX}"
    fi

    if [ -f "${FMC_LMS_KEY}" ]; then
        lms_key="--lms-key ${FMC_LMS_KEY}"
    fi

    if [ -n "${FMC_LMS_KEY_INDEX}" ]; then
        lms_key_index="--lms-key-index ${FMC_LMS_KEY_INDEX}"
    fi

    if [ "${FMC_SIGN_ENABLE}" = "1" ]; then
        sign_args="${ecc_key} ${ecc_key_index} ${lms_key} ${lms_key_index}"
    fi

    echo "sign_args=${sign_args}"

    fmc-imgtool \
        --verbose \
        --version 2 \
        --input ${B}/zephyr/zephyr.bin \
        --output ${B}/zephyr/${BOOTMCU_FMC_BINARY} \
        --prebuilt-dir ${DEPLOY_DIR_IMAGE}/ \
        ${sign_args}
}

addtask create_fmc_image before do_install after do_compile

do_create_fmc_image[depends] += " \
    bmc-pb:do_deploy \
    "

# Deploy all files defined in ZEPHYR_ASPEED_OUTPUT
do_deploy:append() {
    for file in ${ZEPHYR_ASPEED_OUTPUT}; do
        if [ -f "${B}/zephyr/${file}" ]; then
            install -m 0644 ${B}/zephyr/${file} ${DEPLOYDIR}
        fi
    done
}
