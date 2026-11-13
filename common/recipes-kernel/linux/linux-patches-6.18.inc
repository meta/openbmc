FILESEXTRAPATHS:prepend := "${THISDIR}/6.18:"

#
# Patches to be upstreamed soon.
#
SRC_URI:append = " \
    file://0001-ARM-dts-aspeed-Add-Facebook-fblite-r1-BMC.patch \
    file://0002-ARM-dts-aspeed-Add-device-tree-for-Facebook-meru-AST.patch \
"

#
# JTAG patches.
# Used by multiple OpenBMC platforms for firmware upgrade.
#
SRC_URI:append = " \
    file://0100-drivers-jtag-Add-JTAG-core-driver.patch \
    file://0101-dt-binding-jtag-Aspeed-2400-and-2500-series.patch \
    file://0102-Add-Aspeed-SoC-24xx-and-25xx-families-JTAG-master-dr.patch \
    file://0103-jtag-aspeed-support-sysfs-interface.patch \
    file://0104-jtag-support-driver-specific-ioctl-commands.patch \
    file://0105-jtag-aspeed-support-JTAG_RUN_CYCLE-ioctl-command.patch \
    file://0106-jtag-aspeed-add-AST2600-support.patch \
    file://0107-Documentation-jtag-Add-ABI-documentation.patch \
    file://0108-Documentation-jtag-Add-JTAG-core-driver-ioctl-number.patch \
    file://0109-drivers-jtag-Add-JTAG-core-driver-Maintainers.patch \
    file://0110-fixup-jtag-patch-series.patch \
    file://0111-ARM-dts-aspeed-g6-add-jtag-controllers.patch \
    file://0112-ARM-dts-aspeed-g5-add-jtag-controller.patch \
"

#
# ASPEED controller related patches.
#
SRC_URI:append = " \
    file://0200-spi-aspeed-add-shutdown-path-for-AST25XX-SPI-control.patch \
    file://0201-spi-add-user-mode-aspeed-spi-driver.patch \
    file://0202-aspeed-i2c-add-clock-duty-cycle-property.patch \
    file://0203-dt-bindings-aspeed-i2c-add-properties-for-setting-i2.patch \
    file://0204-i2c-aspeed-update-ast2400-timing-settings.patch \
    file://0205-i2c-add-a-slave-backend-to-receive-and-queue-message.patch \
    file://0206-i2c-aspeed-add-i2c-slave-inactive-timeout-support.patch \
    file://0207-Add-ASPEED-AST2600-I2Cv2-controller-driver.patch \
    file://0208-ARM-dts-aspeed-g6-Add-AST2600-I3Cs.patch \
    file://0209-dt-bindings-i3c-hub-Add-Renesas-RG3MxxB12A1-I3C-HUB.patch \
    file://0210-i3c-i3c-hub-Add-Renesas-RG3MxxB12A1-I3C-HUB-driver.patch \
    file://0211-ARM-dts-aspeed-g6-add-pcc-config-in-aspeed-g6.dtsi.patch \
    file://0212-dmaengine-aspeed-Add-AST2600-UART-DMA-driver.patch \
    file://0213-serial-8250-Add-AST2600-UART-driver.patch \
    file://0214-drivers-misc-Add-Aspeed-OTP-control-register.patch \
    file://0215-soc-aspeed-lpc-pcc-Add-PCC-controller-support.patch \
    file://0216-i2c-ast2600-support-ssif.patch \
    file://0217-ARM-dts-aspeed-g6-Add-UDMA-node.patch \
    file://0218-net-ftgmac100-reset-MAC-by-SCU-on-RMII.patch \
    file://0219-net-ncsi-check-for-netlink-driven-responses-before-r.patch \
    file://0220-i3c-ast2600-Validate-AST2600-I3C-for-MCTP-over-I3C.patch \
    file://0221-i3c-dw-Add-err-messages-for-ibi_fifo_clear-handling.patch \
    file://0222-i3c-dw-handle-transfer-abort-errors-distinctly.patch \
    file://0223-i3c-dw-Handle-unexpected-responses-caused-by-SDA-noi.patch \
    file://0224-i3c-handle-null-transfers-during-response-processing.patch \
    file://0225-i3c-master-ast2600-Fix-missing-of_node-for-virtual-I.patch \
    file://0226-i3c-dw-Improve-IBI-queue-handling-when-encountering-.patch \
    file://0227-i3c-master-dw-i3c-master-Fix-race-condition-in-halt-.patch \
    file://0228-gpio-aspeed-sgpio-Create-llops-to-handle-hardware-ac.patch \
    file://0229-gpio-aspeed-sgpio-Convert-IRQ-functions-to-use-llops.patch \
    file://0230-gpio-aspeed-sgpio-Support-G7-Aspeed-sgpiom-controlle.patch \
    file://0231-iio-adc-aspeed-Add-AST2700-ADC-support.patch \
    file://0232-iio-adc-aspeed-Disable-the-last-channel-by-default.patch \
    file://0233-net-ftgmac100-add-support-for-AST2700.patch \
    file://0234-aspeed-i2c-add-i2c-clk-high-min-percent-property.patch \
    file://0235-iio-adc-aspeed-Support-deglitch-feature.patch \
    file://0236-dt-bindings-iio-adc-ti-ads112c04-Add-binding-for-ADS.patch \
    file://0237-iio-adc-ti-ads112c04-Add-support-for-TI-ADS112C04.patch \
    file://0238-ipmi-ssif_bmc-Prevent-pending-response-from-being-lo.patch \
    file://0239-aspeed-i2c-fix-AST2700-target-buffer-mode-NULL-deref.patch \
    file://0240-i2c-ast2600-reset-more-ISR-status.patch \
    file://0241-ipmi-ssif_bmc-add-GPIO-based-alert-mechanism.patch \
    file://0242-bindings-ipmi-ssif-bmc-Add-property-to-adjust-respon.patch \
    file://0243-ipmi-ssif_bmc-Add-support-for-adjustable-response-ti.patch \
"

#
# CP2112 HID USB to SMBus/I2C Bridge patches.
#
SRC_URI:append = " \
    file://0300-dt-bindings-i2c-Add-CP2112-HID-USB-to-SMBus-Bridge.patch \
    file://0301-HID-cp2112-Configure-I2C-Bus-Speed-from-Firmware.patch \
    file://0302-Replace-cp2112-sync-xfer-with-async-workqueue.patch \
    file://0303-Rate-limit-cp2112-GPIO-poll-error-log.patch \
    file://0304-hid-cp2112-validate-data-length-byte.patch \
"

#
# NCSI patches used by multiple OpenBMC platforms.
#
SRC_URI:append = " \
    file://0500-NCSI-Add-propety-no-channel-monitor-and-start-redo-p.patch \
    file://0501-net-ncsi-specify-maximum-package-to-probe.patch \
    file://0502-net-usb-cdc_ether-add-NCSI-passthrough-support.patch \
    file://0503-net-ncsi-fix-use-after-free-in-ncsi_unregister_dev.patch \
"

#
# hwmon/sensor drivers
#
SRC_URI:append = " \
    file://0600-hwmon-max31790-support-to-config-PWM-as-TACH.patch \
    file://0601-hwmon-max31790-add-fanN_enable-for-all-fans.patch \
    file://0602-hwmon-pmbus-Add-xdpe132g5c-driver.patch \
    file://0603-hwmon-Add-net_brcm-driver.patch \
    file://0604-misc-amd-apml-support-APML-driver.patch \
    file://0605-hwmon-pmbus-tda38640-Add-identification-for-PMBUS_VO.patch \
    file://0606-iio-adc-ltc2309-explicitly-assign-hex-values-to-chan.patch \
    file://0607-iio-adc-ltc2309-add-support-for-ltc2305.patch \
    file://0608-nfc-nxp-nci-Add-ISO15693-support.patch \
    file://0609-dt-bindings-trivial-devices-Add-adi-max20912-and-max.patch \
    file://0610-hwmon-pmbus-Add-driver-for-Analog-Devices-MAX20912-a.patch \
    file://0611-nfc-nxp-nci-treat-ENXIO-in-IRQ-thread-as-no-data-ava.patch \
    file://0612-hwmon-kb9002-Add-driver-for-Kandou-KB9002-retimer.patch \
    file://0613-hwmon-pmbus-Add-MPQ82D00-support.patch \
    file://0614-hwmon-pmbus-ltc4286-Add-writable-shunt_resistor-sysf.patch \
    file://0615-dt-bindings-hwmon-pmbus-isl68137-Add-RAA229639-and-R.patch \
    file://0616-hwmon-pmbus-isl68137-Add-support-for-Renesas-RAA2296.patch \
    file://0617-dt-bindings-hwmon-pmbus-Add-Infineon-tda38740-and-td.patch \
    file://0618-hwmon-pmbus-tda38740-Add-driver-for-Infineon-TDA3874.patch \
"

#
# AST2700 I3C patches
#
SRC_URI:append = " \
    file://0700-i3c-mipi-i3c-hci-Fix-race-in-DMA-ring-enqueue-for-pa.patch \
    file://0701-i3c-mipi-i3c-hci-Fix-race-between-DMA-ring-dequeue-a.patch \
    file://0702-i3c-mipi-i3c-hci-Consolidate-common-xfer-processing-.patch \
    file://0703-i3c-mipi-i3c-hci-Fix-race-in-DMA-error-handling-in-i.patch \
    file://0704-i3c-mipi-i3c-hci-Add-DMA-ring-abort-reset-quirk-for-.patch \
    file://0705-dt-bindings-i3c-Document-the-AST2700-I3C-controller.patch \
    file://0706-i3c-mipi-i3c-hci-Support-address-indexed-DAT-slots.patch \
    file://0707-i3c-mipi-i3c-hci-Add-a-quirk-for-64-bit-DMA-addressi.patch \
    file://0708-i3c-mipi-i3c-hci-Add-a-quirk-to-clear-the-TX-start-t.patch \
    file://0709-i3c-mipi-i3c-hci-Add-support-for-the-AST2700-I3C-con.patch \
    file://0710-i3c-mipi-i3c-hci-Program-AST2700-IBI-termination-thr.patch \
    file://0711-i3c-mipi-i3c-hci-Improve-AST2700-PIO-TX-queue-utiliz.patch \
    file://0712-i3c-mipi-i3c-hci-Support-the-AST2700-internal-pull-u.patch \
"

#
# RTC drivers
#
SRC_URI:append = " \
    file://0800-rtc-Add-NXP-PCF85053-driver-support.patch \
"

#
# Hacks and workarounds
#
SRC_URI:append = " \
    file://0900-jffs2-kill-garbage-collect-thread-when-filesystem-is.patch \
    file://0901-jffs2-suppress-jffs2-messages-when-reading-inode.patch \
    file://0902-mmc-sdhci-of-aspeed-add-skip_probe-module-parameter.patch \
    file://0903-usb-gadget-f_ecm-disable-suspend-resume-feature.patch \
"

#
# AST2700 Patches at various stages of upstream. Since they are waiting
# for basic SOC to be merged they dont yet have LORE Links
#
SRC_URI:append = " \
    file://2707-arm64-dts-aspeed-add-peripheral-nodes-for-G7-platfor.patch \
    file://2708-pinctrl-aspeed-Fix-GPIO-mux-value-for-ADC-capable-ba.patch \
    file://2709-usb-host-ehci-platform-add-ASPEED-AST2700-support.patch \
    file://2710-arm64-dts-aspeed-add-address-cells-size-cells-to-G7-.patch \
    file://2711-usb-gadget-aspeed-vhub-Add-AST2700-support.patch \
    file://2712-arm64-dts-aspeed-Enable-TX-FIFO-retry-quirk-on-AST27.patch \
    file://2713-spi-aspeed-Add-AST2700-TX-RX-controller.patch \
    file://2714-arm64-dts-aspeed-Add-AST2700-128MB-flash-layout.patch \
    file://2715-pinctrl-aspeed-Split-TRST-out-of-the-AST2700-SoC1-JT.patch \
"

#
# AST2700 patches only FBOSS AST2700 builds take. 2716 changes the JTAG
# ioctl ABI that user space on other ASPEED SoCs still uses; 2717-2720
# change the 8250_aspeed UART driver, UART routing and AST2700 SoC device
# tree that lf-openbmc machines also build.
#
SRC_URI:append:openbmc-fb-fboss:aspeed-g7 = " \
    file://2716-jtag-aspeed-add-AST2700-support.patch \
    file://2717-serial-8250-aspeed-add-AST2700-UART-and-UDMA-support.patch \
    file://2718-soc-aspeed-add-AST2700-UART-routing-support.patch \
    file://2719-arm64-dts-aspeed-update-AST2700-platform-peripherals.patch \
    file://2720-pinctrl-aspeed-g7-add-split-UART-groups.patch \
"
