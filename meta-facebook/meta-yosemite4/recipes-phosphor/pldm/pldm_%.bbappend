FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += " \
    file://yosemite4.conf \
    file://pldmd-exit-handler.service \
    file://pldmd-exit-handler \
"

FILES:${PN}:append = " \
    ${systemd_system_unitdir}/pldmd.service.d/*.conf \
    ${systemd_system_unitdir}/pldmd-exit-handler \
"

SYSTEMD_SERVICE:${PN} += " \
    pldmd-exit-handler.service \
"


EXTRA_OEMESON:append = " \
  -Dtransport-implementation=af-mctp \
  -Dmaximum-transfer-size=245 \
  -Dsensor-polling-time=2000 \
  -Ddbus-timeout-value=60 \
  -Dinstance-id-expiration-interval=6 \
  -Dmctp-recovery=disabled \
  -Ddiscovery-fru-data-from-terminus=disabled \
  -Dterminus-discovery-retry-count=2 \
  -Denable-fallback-terminus-name=disabled \
  -Dfw-update-pkg-inotify=enabled \
"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}/pldmd.service.d
    install -m 0644 ${UNPACKDIR}/yosemite4.conf ${D}${systemd_system_unitdir}/pldmd.service.d/

    install -m 0755 ${UNPACKDIR}/pldmd-exit-handler ${D}${libexecdir}/${PN}/
    install -m 0644 ${UNPACKDIR}/pldmd-exit-handler.service ${D}${systemd_system_unitdir}
}

# Keep other assertions enabled and leave other stdexec consumers unchanged.
CXXFLAGS:append:yosemite4 = " -DSTDEXEC_DISABLE_POLYMORPHIC_DOWNCAST_ASSERT"

