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

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

PACKAGECONFIG += "disable-watchdog"
PACKAGECONFIG += "boot-info"

LOCAL_URI += " \
    file://setup-gpio.sh \
    file://board-utils.sh \
    file://fpga_ver.sh \
    file://setup_board.sh \
    file://setup_i2c.sh \
    file://setup_i2c.service \
    file://sol.sh \
    file://cpld_update.sh \
    file://wedge_power.sh \
    file://feutil \
    file://seutil \
    file://peutil \
    file://power-on.sh \
    file://presence_util.sh \
    file://read_sled.sh \
    file://setup_avs.sh \
    file://setup_bic.sh \
    file://setup_mgmt.sh \
    file://spi_util.sh \
    file://smbutil \
    file://beutil \
    file://set_sled.sh \
    file://parallel_update_pims.sh \
    file://pim_upgrade.sh \
    file://dump_pim_serials.sh \
    file://switch_pim_mux_to_fpga.sh \
    file://reinit_all_pim.sh \
    file://wedge_us_mac.sh \
    file://eth0_mac_fixup.sh \
    file://mount_data1.service \
    file://setup_gpio.service \
    file://i2c_sensor_fixup.sh \
    file://reset_pim_i2c_mux.sh \
    file://improve_aura_pll.sh \
    file://oob-status.sh \
    "

OPENBMC_UTILS_FILES += " \
    board-utils.sh \
    fpga_ver.sh \
    sol.sh \
    cpld_update.sh \
    wedge_power.sh \
    feutil \
    seutil \
    peutil \
    presence_util.sh\
    read_sled.sh \
    setup_avs.sh \
    setup_bic.sh \
    setup_mgmt.sh \
    spi_util.sh \
    smbutil \
    beutil \
    set_sled.sh \
    parallel_update_pims.sh \
    pim_upgrade.sh \
    dump_pim_serials.sh \
    switch_pim_mux_to_fpga.sh \
    reinit_all_pim.sh \
    wedge_us_mac.sh \
    i2c_sensor_fixup.sh \
    reset_pim_i2c_mux.sh \
    improve_aura_pll.sh \
    oob-status.sh \
    "
DEPENDS:append = " update-rc.d-native"
inherit systemd

do_work_sysv() {
    # the script to mount /mnt/data
    install -m 0755 ${UNPACKDIR}/mount_data0.sh ${D}${sysconfdir}/init.d/mount_data0.sh
    update-rc.d -r ${D} mount_data0.sh start 03 S .
    install -m 0755 ${UNPACKDIR}/rc.early ${D}${sysconfdir}/init.d/rc.early
    update-rc.d -r ${D} rc.early start 04 S .

    # mount secondary storage (emmc) to /mnt/data1
    install -m 0755 ${UNPACKDIR}/mount_data1.sh ${D}${sysconfdir}/init.d/mount_data1.sh
    update-rc.d -r ${D} mount_data1.sh start 05 S .

    install -m 755 ${UNPACKDIR}/power-on.sh ${D}${sysconfdir}/init.d/power-on.sh
    update-rc.d -r ${D} power-on.sh start 85 S .
    install -m 755 ${UNPACKDIR}/setup_i2c.sh ${D}${sysconfdir}/init.d/setup_i2c.sh
    update-rc.d -r ${D} setup_i2c.sh start 59 S .
    # Export GPIO pins and set initial directions/values.
    install -m 755 ${UNPACKDIR}/setup-gpio.sh ${D}${sysconfdir}/init.d/setup-gpio.sh
    update-rc.d -r ${D} setup-gpio.sh start 60 S .

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
    install -m 755 create_vlan_intf ${D}${sysconfdir}/network/if-up.d/create_vlan_intf

    install -m 0755 ${UNPACKDIR}/rc.local ${D}${sysconfdir}/init.d/rc.local
    update-rc.d -r ${D} rc.local start 99 2 3 4 5 .
}

do_work_systemd() {
  # TODO: We'd want to run all the logic here that is run in mound_data0.sh
  install -d ${D}/usr/local/bin
  install -d ${D}${systemd_system_unitdir}

  # mount secondary storage (emmc) to /mnt/data1
  install -m 755 ${UNPACKDIR}/mount_data1.sh ${D}/usr/local/bin/mount_data1.sh

  install -m 755 ${UNPACKDIR}/setup-gpio.sh ${D}/usr/local/bin/setup-gpio.sh

  install -m 755 ${UNPACKDIR}/setup_i2c.sh ${D}/usr/local/bin/setup_i2c.sh

  # networking is done after rcS, any start level within rcS
  # for mac fixup should work
  install -m 755 ${UNPACKDIR}/eth0_mac_fixup.sh ${D}/usr/local/bin/eth0_mac_fixup.sh

  install -m 755 ${UNPACKDIR}/setup_board.sh ${D}/usr/local/bin/setup_board.sh

  install -m 755 ${UNPACKDIR}/power-on.sh ${D}/usr/local/bin/power-on.sh

  install -m 755 ${UNPACKDIR}/setup_avs.sh ${D}/usr/local/bin/setup_avs.sh

  install -m 0644 ${UNPACKDIR}/mount_data1.service ${D}${systemd_system_unitdir}

  install -m 0644 ${UNPACKDIR}/setup_gpio.service ${D}${systemd_system_unitdir}

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


do_install:append() {
  do_install_board
}

FILES:${PN} += "${sysconfdir}"

SYSTEMD_SERVICE:${PN} += "mount_data1.service setup_gpio.service"
SYSTEMD_SERVICE:${PN} += "setup_i2c.service"

# oob-status.sh reads the OOB switch and its PHYs through the kernel MDIO bus
RDEPENDS:${PN} += "mii-read"

#Not needed for fuji
SYSTEMD_SERVICE:${PN}:remove = "enable_watchdog_ext_signal.service"
