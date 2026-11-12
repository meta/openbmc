FILESEXTRAPATHS:prepend := "${THISDIR}/linux-aspeed:"

SRC_URI:append = " \
    file://1000-Arm64-aspeed-dts-add-yosemite5-dts.patch \
    file://1001-arm64-dts-aspeed-add-yosemite5-variants-support.patch \
    file://1002-arm64-dts-aspeed-yosemite5-add-CXL-BD-on-J32-and-MCT.patch \
    file://defconfig \
    file://yosemite5a7.cfg \
"
