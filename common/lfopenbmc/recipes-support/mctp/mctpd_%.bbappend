require mctp.inc

SRC_URI += "file://10-run-as-root.conf"

FILES:${PN}:append = " ${systemd_system_unitdir}/mctpd.service.d"

do_install:append () {
    install -d ${D}${systemd_system_unitdir}/mctpd.service.d
    install -m 0644 ${UNPACKDIR}/10-run-as-root.conf \
            ${D}${systemd_system_unitdir}/mctpd.service.d/
}
