# Copyright 2015-present Facebook. All Rights Reserved.
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

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

PACKAGECONFIG += "disable-watchdog"
PACKAGECONFIG += " boot-info"

LOCAL_URI += " \
    file://enable_watchdog_ext_signal.sh \
    file://ec_version.sh \
    file://board-utils.sh \
    file://setup_board.sh \
    file://cpld_rev.sh \
    file://cpld_upgrade.sh \
    file://internal_switch_version.sh \
    file://oob-status.sh \
    file://cp2112_i2c_flush.sh \
    file://reset_qsfp_mux.sh \
    file://setup_i2c.sh \
    file://spi_util.sh \
    file://reset_fancpld.sh \
    file://reset_oob_switch.sh \
    file://setup_i2c.service \
    file://power-on.service \
    file://setup_board.service \
    file://mount_data0.sh \
    "

OPENBMC_UTILS_FILES += " \
    enable_watchdog_ext_signal.sh \
    cpld_upgrade.sh \
    cpld_rev.sh \
    internal_switch_version.sh \
    oob-status.sh \
    ec_version.sh \
    cp2112_i2c_flush.sh \
    reset_qsfp_mux.sh \
    spi_util.sh \
    reset_fancpld.sh \
    reset_oob_switch.sh \
    "

DEPENDS:append = " update-rc.d-native"

inherit systemd

do_work_sysv() {
    # the script to mount /mnt/data
    install -m 0755 ${UNPACKDIR}/mount_data0.sh ${D}${sysconfdir}/init.d/mount_data0.sh
    update-rc.d -r ${D} mount_data0.sh start 03 S .
    install -m 0755 ${UNPACKDIR}/rc.early ${D}${sysconfdir}/init.d/rc.early
    update-rc.d -r ${D} rc.early start 04 S .

    install -m 755 ${UNPACKDIR}/setup_i2c.sh ${D}${sysconfdir}/init.d/setup_i2c.sh
    update-rc.d -r ${D} setup_i2c.sh start 60 S .

    # networking is done after rcS, any start level within rcS
    # for mac fixup should work
    install -m 755 ${UNPACKDIR}/eth0_mac_fixup.sh ${D}${sysconfdir}/init.d/eth0_mac_fixup.sh
    update-rc.d -r ${D} eth0_mac_fixup.sh start 70 S .

    # create VLAN intf automatically
    install -d ${D}/${sysconfdir}/network/if-up.d
    install -m 755 ${UNPACKDIR}/create_vlan_intf ${D}${sysconfdir}/network/if-up.d/create_vlan_intf

    install -m 755 ${UNPACKDIR}/setup_board.sh ${D}${sysconfdir}/init.d/setup_board.sh
    update-rc.d -r ${D} setup_board.sh start 80 S .

    install -m 755 ${UNPACKDIR}/power-on.sh ${D}${sysconfdir}/init.d/power-on.sh
    update-rc.d -r ${D} power-on.sh start 85 S .

    install -m 0755 ${UNPACKDIR}/rc.local ${D}${sysconfdir}/init.d/rc.local
    update-rc.d -r ${D} rc.local start 99 2 3 4 5 .

    install -m 0755 ${UNPACKDIR}/enable_watchdog_ext_signal.sh ${D}${sysconfdir}/init.d/enable_watchdog_ext_signal.sh
    update-rc.d -r ${D} enable_watchdog_ext_signal.sh start 99 2 3 4 5 .
}

do_work_systemd() {
    # TODO: We'd want to run all the logic here that is run in mound_data0.sh
    install -d ${D}/usr/local/bin
    install -d ${D}${systemd_system_unitdir}

    install -m 0755 ${UNPACKDIR}/setup_i2c.sh ${D}/usr/local/bin/setup_i2c.sh

    # networking is done after rcS, any start level within rcS
    # for mac fixup should work
    install -m 755 ${UNPACKDIR}/eth0_mac_fixup.sh ${D}/usr/local/bin/eth0_mac_fixup.sh

    install -m 755 ${UNPACKDIR}/setup_board.sh ${D}/usr/local/bin/setup_board.sh

    install -m 755 ${UNPACKDIR}/power-on.sh ${D}/usr/local/bin/power-on.sh

    install -m 0755 ${UNPACKDIR}/enable_watchdog_ext_signal.sh ${D}/usr/local/bin/enable_watchdog_ext_signal.sh
}

do_install_board() {
    # for backward compatible, create /usr/local/fbpackages/utils/ast-functions
    olddir="/usr/local/fbpackages/utils"
    install -d ${D}${olddir}
    ln -s "/usr/local/bin/openbmc-utils.sh" "${D}${olddir}/ast-functions"

    # init
    install -d ${D}${sysconfdir}/init.d
    install -d ${D}${sysconfdir}/rcS.d
    if ${@bb.utils.contains('DISTRO_FEATURES', 'systemd', 'true', 'false', d)}; then
      do_work_systemd
    else
      do_work_sysv
    fi
}

FILES:${PN} += "${sysconfdir}"

# oob-status.sh: mdio-bb (bitbang) for the OOB switch, mii-read for the MGMT PHY
RDEPENDS:${PN} += "bitbang mii-read"

SYSTEMD_SERVICE:${PN} += "setup_i2c.service"
