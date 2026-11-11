FILESEXTRAPATHS:prepend := "${THISDIR}/linux-aspeed:"

SRC_URI:append = " \
    file://ventura2-local.cfg \
    file://0001-ARM-dts-aspeed-ventura2-Add-downstream-only-properti.patch \
    file://0002-ARM-dts-aspeed-ventura2-Add-ADCs-and-GPIO-line-names.patch \
"
