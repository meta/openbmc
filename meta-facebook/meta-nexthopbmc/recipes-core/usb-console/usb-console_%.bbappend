# usbcons is not needed on nexthopbmc, so mask the service.
SYSTEMD_SERVICE:${PN}:remove:nexthopbmc = "usbcons.service"

do_install:append:nexthopbmc() {
    ln -sf /dev/null "${D}${systemd_system_unitdir}/usbcons.service"
}
