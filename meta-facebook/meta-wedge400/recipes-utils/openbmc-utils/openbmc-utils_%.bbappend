# Copyright 2019-present Facebook. All Rights Reserved.
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
    file://board-utils.sh \
    file://bsm-eutil \
    file://cpld_ver.sh \
    file://fcmcpld_update.sh \
    file://feutil \
    file://fpga_ver.sh \
    file://oob-status.sh \
    file://power-on.sh \
    file://presence_util.sh \
    file://pwrcpld_update.sh \
    file://rebind-driver.sh \
    file://reset_brcm.sh \
    file://reutil \
    file://scmcpld_update.sh \
    file://set_sled.sh \
    file://set_vdd.sh \
    file://setup_avs.sh \
    file://setup_bic.sh \
    file://setup_board.sh \
    file://setup_default_gpio.sh \
    file://setup_i2c.sh \
    file://setup_mgmt.sh \
    file://seutil \
    file://smbcpld_update.sh \
    file://sol.sh \
    file://spi_util.sh \
    file://switch_reset.sh \
    file://us_console.sh \
    file://wedge_power.sh \
    file://wedge_us_mac.sh \
    file://setup_default_gpio.service \
    file://setup_avs.service \
    file://mount_data1.service \
    "

OPENBMC_UTILS_FILES += " \
    board-utils.sh \
    bsm-eutil \
    cpld_ver.sh \
    fcmcpld_update.sh \
    feutil \
    fpga_ver.sh \
    oob-status.sh \
    presence_util.sh \
    pwrcpld_update.sh \
    rebind-driver.sh \
    reset_brcm.sh \
    reutil \
    scmcpld_update.sh \
    set_sled.sh \
    set_vdd.sh \
    setup_avs.sh \
    setup_bic.sh \
    setup_mgmt.sh \
    seutil \
    smbcpld_update.sh \
    sol.sh \
    spi_util.sh \
    switch_reset.sh \
    us_console.sh \
    wedge_power.sh \
    wedge_us_mac.sh \
    "

DEPENDS:append = " update-rc.d-native"

install_sysv:append() {
    install -m 755 ${UNPACKDIR}/power-on.sh ${D}${sysconfdir}/init.d/power-on.sh
    update-rc.d -r ${D} power-on.sh start 85 S .

    install -m 755 ${UNPACKDIR}/setup_default_gpio.sh ${D}${sysconfdir}/init.d/setup_default_gpio.sh
    update-rc.d -r ${D} setup_default_gpio.sh start 60 S .

    install -m 755 ${UNPACKDIR}/setup_i2c.sh ${D}${sysconfdir}/init.d/setup_i2c.sh
    update-rc.d -r ${D} setup_i2c.sh start 59 S .

    # AVS voltage setup on Wedge400 units, this should be after power-on.sh
    install -m 755 ${UNPACKDIR}/setup_avs.sh ${D}${sysconfdir}/init.d/setup_avs.sh
    update-rc.d -r ${D} setup_avs.sh start 86 S .

    # networking is done after rcS, any start level within rcS
    # for mac fixup should work
    install -m 755 ${UNPACKDIR}/eth0_mac_fixup.sh ${D}${sysconfdir}/init.d/eth0_mac_fixup.sh
    update-rc.d -r ${D} eth0_mac_fixup.sh start 70 S .

    install -m 755 ${UNPACKDIR}/setup_board.sh ${D}${sysconfdir}/init.d/setup_board.sh
    update-rc.d -r ${D} setup_board.sh start 80 S .

    # create VLAN intf automatically
    install -d ${D}/${sysconfdir}/network/if-up.d
    install -m 755 ${UNPACKDIR}/create_vlan_intf ${D}${sysconfdir}/network/if-up.d/create_vlan_intf

    install -m 0755 ${UNPACKDIR}/rc.local ${D}${sysconfdir}/init.d/rc.local
    update-rc.d -r ${D} rc.local start 99 2 3 4 5 .
}

install_systemd:append() {
    # Scripts to /usr/local/bin (wedge400-specific)
    install -m 0755 ${UNPACKDIR}/setup_i2c.sh ${D}/usr/local/bin/setup_i2c.sh
    install -m 0755 ${UNPACKDIR}/setup_default_gpio.sh ${D}/usr/local/bin/setup_default_gpio.sh
    install -m 755 ${UNPACKDIR}/eth0_mac_fixup.sh ${D}/usr/local/bin/eth0_mac_fixup.sh
    install -m 0755 ${UNPACKDIR}/setup_board.sh ${D}/usr/local/bin/setup_board.sh
    install -m 0755 ${UNPACKDIR}/power-on.sh ${D}/usr/local/bin/power-on.sh
    install -m 0755 ${UNPACKDIR}/setup_avs.sh ${D}/usr/local/bin/setup_avs.sh

    install -m 0644 ${UNPACKDIR}/setup_default_gpio.service ${D}${systemd_system_unitdir}
    install -m 0644 ${UNPACKDIR}/setup_avs.service ${D}${systemd_system_unitdir}
    install -m 0644 ${UNPACKDIR}/mount_data1.service ${D}${systemd_system_unitdir}
}

do_install:append() {
    # for backward compatible, create /usr/local/fbpackages/utils/ast-functions
    olddir="/usr/local/fbpackages/utils"
    install -d ${D}${olddir}
    ln -s "/usr/local/bin/openbmc-utils.sh" "${D}${olddir}/ast-functions"
}

FILES:${PN} += "${sysconfdir}"


# wedge400 does not implement enable_watchdog_ext_signal.sh — disable the service
SYSTEMD_SERVICE:${PN}:remove = "enable_watchdog_ext_signal.service"

SYSTEMD_SERVICE:${PN} += " \
    setup_default_gpio.service \
    setup_avs.service \
    mount_data1.service \
    "
