# Copyright 2018-present Facebook. All Rights Reserved.

# Keep the zstd ramdisk. On the AST2400 the kernel takes ~20s to unpack an
# LZMA ramdisk, and those builds reset in a loop during early boot. The
# dbus bbappend drops X11 support to keep the zstd image under 28 MiB.
require recipes-core/images/fb-zstd-rootfs.inc
inherit kernel_fitimage

require recipes-core/images/fb-openbmc-image.bb

# Remove debug tools (strace, mtrace) to save flash space on 16MB wedge100
IMAGE_FEATURES:remove = "tools-debug"

# Include modules in rootfs
IMAGE_INSTALL += " \
  packagegroup-openbmc-base \
  packagegroup-openbmc-net \
  packagegroup-openbmc-python3 \
  packagegroup-openbmc-rest3 \
  openbmc-utils \
  at93cx6-util \
  bcm5396-util \
  ast-mdio \
  openbmc-gpio \
  rackmon \
  psu-update \
  sensor-setup \
  usb-console \
  lldp-util \
  wedge-eeprom \
  weutil-dhcp-id \
  po-eeprom \
  bitbang \
  jbi \
  flashrom \
  psumuxmon \
  mterm \
  fscd  \
  recover-from-secondary \
  udev-rules \
  "

remove_python_config_dir() {
    rm -rf ${IMAGE_ROOTFS}${libdir}/python*/config-*
}
ROOTFS_POSTPROCESS_COMMAND:append = " remove_python_config_dir;"

#
# kcsd and ipmid are not installed because IPMI is not enabled in uServer
# BIOS.
# To enable IPMI in wedge100, we will need to:
#   1. Upgrade wedge100s BIOS with IPMI support.
#   2. Enable AST2400 LPC/KCS support (adding kcs nodes in aspeed-g4.dtsi
#      and potentially kcs driver changes).
#   3. Enable KCS devices in wedge100 dts.
#   4. Uncomment below lines to include kcsd and ipmid in wedge100 image.
#
# IMAGE_INSTALL += " \
#   kcsd \
#   ipmid \
#  "
