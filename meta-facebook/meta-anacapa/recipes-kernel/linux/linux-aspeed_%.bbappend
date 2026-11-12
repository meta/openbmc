FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://anacapa-local.cfg \
    file://1000-ARM-dts-aspeed-anacapa-config-ncsi-device.patch \
    file://1001-ARM-dts-aspeed-anacapa-Add-eeprom-device-node.patch \
    file://1002-ARM-dts-aspeed-anacapa-enable-APML-I3C-bus.patch \
    file://1003-Enable-NIC-MCTP-over-I2C.patch \
    file://1004-dt-bindings-arm-aspeed-add-Anacapa-EVT1-EVT2-board.patch \
    file://1005-ARM-dts-aspeed-anacapa-add-EVT1-devicetree-and-point.patch \
    file://1006-ARM-dts-aspeed-anacapa-add-EVT2-devicetree-and-updat.patch \
    file://1007-ARM-dts-aspeed-anacapa-evt2-add-lpc_pcc-device.patch \
    file://1008-ARM-dts-aspeed-anacapa-evt2-config-ncsi-device.patch \
    file://1009-ARM-dts-aspeed-anacapa-evt2-enable-APML-I3C-bus.patch \
    file://1010-Enable-NIC-MCTP-over-I2C.patch \
    file://1011-net-ncsi-promote-debug-messages-to-info-for-runtime-.patch \
    file://1012-arm-dts-anacapa-Enable-JTAG1.patch \
    file://1013-arm-dts-anacapa-Modify-the-RMI-TSI-device-IDs.patch \
    file://1014-ARM-dts-aspeed-anacapa-add-interrupt-properties-for-.patch \
    file://1015-arm-dts-anacapa-Align-thermtrip-SGPIO-pin-names.patch \
    file://1016-arm-dts-anacapa-Rename-left-PDB-presence-pin.patch \
    file://1017-arm-dts-anacapa-Align-EDSFF-SGPIO-pin-names.patch \
    file://1018-arm-dts-anacapa-Align-PDB-fan-GPIO-numbering.patch \
    file://1019-arm-dts-anacapa-Align-leakage-SGPIO-pin-names.patch \
    file://1020-ARM-dts-aspeed-anacapa-Add-eeprom-device-node-for-NF.patch \
    file://1021-ARM-dts-aspeed-anacapa-add-JTAG-CPLD-TRST-pin-to-SGP.patch \
    file://1022-ARM-dts-aspeed-anacapa-Support-Venice-B0-CPU-APML.patch \
    file://1023-jtag-jtag-aspeed-Bring-changes-for-Aspeed-26XX.patch \
    file://1024-jtag-jtag-aspeed-Increase-the-wait-iteration-to-300.patch \
    file://1025-ARM-dts-aspeed-anacapa-Add-MCTP-I2C-controller-node.patch \
    file://1026-ARM-dts-aspeed-anacapa-add-NCT7363-fan-controllers.patch \
    file://1027-arm-dts-anacapa-Align-Cable-and-SSD-SGPIO-pin-names.patch \
    file://1028-ARM-dts-aspeed-anacapa-Add-LPDB-fuse-monitor-GPIO-ex.patch \
    file://1029-misc-amd-apml-Add-Venice-B0-I3C-ID-and-canonical-nod.patch \
    file://1030-jtag-increase-JTAG_MAX_XFER_DATA_LEN-to-0x1000000-2-.patch \
    file://1031-ARM-dts-aspeed-anacapa-Simplify-leakage-GPIO-line-na.patch \
    file://1032-ARM-dts-aspeed-anacapa-Rename-FM_CPU0_PWRGD-to-host0.patch \
    file://1033-ARM-dts-aspeed-anacapa-remove-unused-EEPROM.patch \
"
