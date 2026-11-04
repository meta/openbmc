FILESEXTRAPATHS:prepend:openbmc-fb-lf := "${THISDIR}/files:"

SRC_URI:append:openbmc-fb-lf = " file://led-group-retry.conf"

SYSTEMD_OVERRIDE:${PN}:append:openbmc-fb-lf = " \
    led-group-retry.conf:obmc-led-group-start@.service.d/led-group-retry.conf \
    led-group-retry.conf:obmc-led-group-stop@.service.d/led-group-retry.conf \
"
