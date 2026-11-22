FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += " \
    file://monitorrequested.conf \
"

FILES:${PN} += "${systemd_system_unitdir}/*"

# The SD/WF BIC does not report its own firmware (component identifier 0) in
# its GetFirmwareParameters component table. Keep the previous fw-update
# behavior so the BIC firmware update does not fail.
EXTRA_OEMESON:append = " \
    -Dfw-update-skip-missing-comp-info=disabled \
    -Dfw-update-abort-missing-comp-index=disabled \
"

do_install:append () {
    override_dir=${D}${systemd_system_unitdir}/pldmd.service.d
    install -d ${D}${systemd_system_unitdir}/pldmd.service.d

    install -m 0644 ${UNPACKDIR}/monitorrequested.conf ${override_dir}/monitorrequested.conf
}
