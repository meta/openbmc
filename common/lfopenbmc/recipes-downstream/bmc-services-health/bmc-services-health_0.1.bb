SUMMARY = "BMC services health metrics"
DESCRIPTION = "Publishes failed, restarting and boot-stuck systemd service counts as Meta OEM metrics (bmc-oem-metric)"
SECTION = "base"
PR = "r1"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

inherit systemd pkgconfig meson ptest-meson
S = "${UNPACKDIR}"

LOCAL_URI = " \
    file://meson.build \
    file://meson.options \
    file://services_health.hpp \
    file://services_health.cpp \
    file://checks.hpp \
    file://checks.cpp \
    file://metric_writer.hpp \
    file://metric_writer.cpp \
    file://systemd_dbus.hpp \
    file://systemd_dbus.cpp \
    file://main.cpp \
    file://test \
    file://bmc-services-health.service \
    file://exclude \
    "

DEPENDS += " \
    phosphor-logging \
    sdbusplus \
    systemd \
    ${@bb.utils.contains('PTEST_ENABLED', '1', 'gtest', '', d)} \
    "

# bmc-oem-metric, the only path from this daemon to Redfish, ships with
# adhoc-sensor. An image that removes adhoc-sensor must remove this too, or
# this line pulls adhoc-sensor back in.
RDEPENDS:${PN} += "adhoc-sensor"

EXTRA_OEMESON += "-Dtests=${@bb.utils.contains('PTEST_ENABLED', '1', 'enabled', 'disabled', d)}"

SYSTEMD_SERVICE:${PN} = "bmc-services-health.service"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${UNPACKDIR}/bmc-services-health.service \
        ${D}${systemd_system_unitdir}/bmc-services-health.service

    install -d ${D}${datadir}/bmc-services-health
    install -m 0644 ${UNPACKDIR}/exclude ${D}${datadir}/bmc-services-health/exclude
}

FILES:${PN} += " \
    ${systemd_system_unitdir}/bmc-services-health.service \
    ${datadir}/bmc-services-health \
    "
