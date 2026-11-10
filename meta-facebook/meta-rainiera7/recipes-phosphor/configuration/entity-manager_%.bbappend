FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://0001-configurations-rainier-tune-SCM-fan-control.patch \
"

SRC_URI:append = " \
    file://0001-configurations-revise-100G-BRCM-NIC-configuration.patch \
    file://0002-configurations-revise-Terminus-100G-NIC-configuratio.patch \
"
