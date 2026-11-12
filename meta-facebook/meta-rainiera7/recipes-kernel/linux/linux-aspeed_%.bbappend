FILESEXTRAPATHS:prepend := "${THISDIR}/linux-aspeed:"

SRC_URI:append:rainiera7 = " \
    file://1002-arm64-dts-aspeed-Add-rainiera7-dts.patch \
    file://defconfig \
    file://rainiera7-local.cfg \
    file://rainiera7.cfg \
"
