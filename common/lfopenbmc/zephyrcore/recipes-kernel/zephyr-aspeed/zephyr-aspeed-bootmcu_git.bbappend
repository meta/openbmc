FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://ast2700_facebook_bootmcu.overlay \
    file://0001-mcu-runtime-spi-Set-eDAF-mode-only-when-the-bridge-i.patch;patchdir=aspeed-zephyr-project \
"

BOOTMCU_EXTRA_DTC_OVERLAY_FILE ?= "${UNPACKDIR}/ast2700_facebook_bootmcu.overlay"

EXTRA_OECMAKE:append = " \
    -DEXTRA_DTC_OVERLAY_FILE="${BOOTMCU_EXTRA_DTC_OVERLAY_FILE}" \
"
