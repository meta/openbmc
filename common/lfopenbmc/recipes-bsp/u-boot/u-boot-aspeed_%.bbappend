FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

require u-boot-version-override.inc

SRC_URI:append:openbmc-fb-lf = " \
    file://0100-ARM-uboot-dts-add-facebook-common-dts.patch \
    file://0500-ARM64-Aspeed2700-add-a-config-for-WDTA-timer-reload-.patch \
    file://wdta-timer.cfg \
"
