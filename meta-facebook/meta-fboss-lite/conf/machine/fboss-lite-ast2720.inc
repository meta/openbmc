#@TYPE: Machine
#@NAME: FBOSS-Lite
#@DESCRIPTION: Machine configuration for Facebook FBOSS-Lite

require conf/machine/include/ast2700.inc
require conf/machine/include/fb-fboss.inc
require conf/machine/include/mtd-ubifs.inc
require conf/machine/include/tpm2.inc

PREFERRED_PROVIDER_virtual/bootloader = "u-boot-aspeed-sdk"
PREFERRED_PROVIDER_u-boot = "u-boot-aspeed-sdk"
PREFERRED_VERSION_u-boot-aspeed-sdk = "v2023.10%"
PREFERRED_PROVIDER_u-boot-fw-utils = "libubootenv"

BBMASK:append = " common/lfopenbmc/zephyrcore/"

ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
CALIPTRA_MANIFEST_FLASH_IMAGE = "ast2700-manifest-flash.bin"

PREFERRED_VERSION_wedge-eeprom = "0.2"
PREFERRED_VERSION_libwedge-eeprom = "0.2"

EXTRA_IMAGE_FEATURES:append = " image-meta"

MACHINE_ESSENTIAL_EXTRA_RDEPENDS += "kernel-module-i2c-dev-sysfs"

IMAGE_CLASSES:append = " image_types_phosphor_aspeed_g7"
