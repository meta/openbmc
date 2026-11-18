#Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
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

SUMMARY = "fbmc Snapshot Utility"
DESCRIPTION = "Dump debug information"
SECTION = "base"
PR = "r1"
LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://showtech.sh;beginline=5;endline=18;md5=0b1ee7d6f844d472fa306b2fee2167e0"

S = "${UNPACKDIR}"
LOCAL_URI = " \
    file://showtech.sh \
    file://000_collect_log_messages.sh \
    file://001_collect_system_state.sh \
    file://002_collect_uboot_info.sh \
    file://003_network_status.sh \
    file://004_sensors.sh \
    file://005_i2c_detect.sh \
    file://006_dump_gpio.sh \
    file://007_debug_logs.sh \
    file://008_host_postcode.sh \
    file://009_running_processes.sh \
    file://010_mterm_logs.sh \
    file://011_boot_info.sh \
    file://dump_gpios.sh \
    file://i2c_scan.sh \
    file://mtd_metadata.sh \
    file://oob-mdio-util.sh \
    "

SHOWTECH_RULES_FILES = " \
    000_collect_log_messages.sh \
    001_collect_system_state.sh \
    002_collect_uboot_info.sh \
    003_network_status.sh \
    004_sensors.sh \
    005_i2c_detect.sh \
    006_dump_gpio.sh \
    007_debug_logs.sh \
    008_host_postcode.sh \
    009_running_processes.sh \
    010_mterm_logs.sh \
    011_boot_info.sh \
    "

# Debug utilities the rules call, also usable interactively.
SHOWTECH_UTILS_FILES = " \
    dump_gpios.sh \
    i2c_scan.sh \
    mtd_metadata.sh \
    oob-mdio-util.sh \
    "

do_install() {
    localbindir="${D}/usr/local/bin"
    showtech_rules_dir="${D}/etc/showtech/rules/"
    install -d ${localbindir}
    install -d ${showtech_rules_dir}

    install -m 755 showtech.sh ${localbindir}/showtech.sh
    ln -s showtech.sh ${localbindir}/showtech
    for f in ${SHOWTECH_RULES_FILES}; do
        install -m 755 $f ${showtech_rules_dir}/${f}
    done

    for f in ${SHOWTECH_UTILS_FILES}; do
        install -m 755 $f ${localbindir}/${f}
    done
}

# showtech uses GNU timeout's kill-after option to bound collector execution.
RDEPENDS:${PN} += "bash coreutils-timeout"
FILES:${PN} = "/usr/local/bin /etc/showtech/rules/"
