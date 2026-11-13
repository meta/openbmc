FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

require ../../../../../common/recipes-bsp/u-boot/u-boot-version-override.inc

SRC_URI:append = " file://0100-cmd-mem-set-test-result-of-mtest-to-enviroment-varia.patch"

