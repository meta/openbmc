FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://0001-configuration-schema-extend-MCTP-target-members.patch \
    file://0002-configuration-schema-mctp-target-add-powerstate-prop.patch \
    file://0003-configurations-santabarbara-add-MB-ADI-VR-sensors.patch \
    file://0004-fru-device-fix-redundant-rescans-on-dbus-property-ch.patch \
"

do_install:append() {
    rm -f ${D}${datadir}/${PN}/configurations/mtjade.json
    rm -f ${D}${datadir}/${PN}/configurations/mtjefferson_*.json
    rm -f ${D}${datadir}/${PN}/configurations/mtmitchell_*.json
}
