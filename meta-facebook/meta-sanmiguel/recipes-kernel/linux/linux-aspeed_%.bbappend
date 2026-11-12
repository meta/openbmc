FILESEXTRAPATHS:prepend := "${THISDIR}/linux-aspeed:"

SRC_URI:append = " \
    file://sanmiguel-local.cfg \
    file://1001-Add-RTL8211F-RMII-to-SGMII-bridge-support.patch \
    file://1002-i2c-muxes-introduce-virtual-mux-host.patch \
    file://1003-Squash-of-the-work-in-progress-series-on-this-branch.patch \
    file://1004-virtual-mux-fail-fast-when-downstream-device-is-abse.patch \
"



SRC_URI:append = " \
    file://2001-ARM-dts-aspeed-sanmiguel-Add-SSIF-DTS-properties.patch \
    file://2002-ARM-dts-aspeed-sanmiguel-Add-CP2112-and-downstream-I.patch \
    file://2003-ARM-dts-aspeed-sanmiguel-add-virtual-mux-host-node.patch \
    file://2004-ARM-dts-aspeed-sanmiguel-Disable-HSC-nodes.patch \
    file://2005-ARM-dts-aspeed-sanmiguel-enable-i2c-slave-timeout.patch \
    "
