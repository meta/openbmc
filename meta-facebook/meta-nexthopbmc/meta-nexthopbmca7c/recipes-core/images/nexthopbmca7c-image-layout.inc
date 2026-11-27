# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program in a file named COPYING; if not, write to the
# Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA

# AST2720-A2 128 MiB SPI NOR layout.
#
# The ASPEED AST2700-A2 boot firmware map (and the matching upstream
# phosphor flash-131072 layout) is:
#
#   0x00000000  manifest bundle   image info, Caliptra FW, SoC manifest,
#                                 mcu-runtime, DP FW, DDR PHY images,
#                                 UEFI VBIOS, u-boot, TF-A, TEE, SSP, TSP
#   0x003E8000  image-meta        Meta image metadata (96 KiB, see below)
#   0x00400000  u-boot-env        128 KiB
#   0x00420000  kernel / rofs     one FIT (kernel + dtb + rootfs) here
#   0x06000000  rwfs              "data0", 32 MiB
#
# fboss-lite images put the kernel and rootfs in a single FIT, so the
# upstream "kernel @0x420000 / rofs @0xD20000" split collapses into one
# os-fit partition.  The offsets below match facebook-bmc-flash-layout-128.dtsi
# (arm64) used by the kernel device tree.

# fboss-lite-image-layout.inc sets FBOBMC_IMAGE_META_OFFSET:aspeed-g7 for its
# own AST2700 layout; the machine override is needed to take priority over it.
FBOBMC_IMAGE_META_OFFSET:nexthopbmca7c = "0x003E8000"

def fbobmc_get_image_part_table(d):
    image_max_size = 0x08000000

    # The first 4000 KiB are reserved for the manifest bundle, so the image
    # metadata sits in the 96 KiB between it and the environment at 4 MiB.
    #
    # The NOR has 64 KiB erase blocks but 0x003E8000 is only 32 KiB aligned.
    # This value is the image generator's layout validation granularity, not
    # the NOR erase size.  Linux exposes "u-boot" and "meta" read-only for
    # that reason; whole-image updates go through the aligned flash0 device.
    erase_block_size = 32 * 1024

    part_table = (
        # offset,     name          max size (KiB)  type       source component
        (0x00000000, "u-boot",     -1,             "raw",     d.expand("${DEPLOY_DIR_IMAGE}/u-boot.${UBOOT_SUFFIX}")),
        (0x003E8000, "image-meta", 96,             "meta",    0),
        (0x00400000, "u-boot-env", 128,            "data",    0),
        (0x00420000, "os-fit",     -1,             "fit",     d.expand("${DEPLOY_DIR_IMAGE}/${FIT_LINK}")),
        (0x06000000, "data0",      32 * 1024,      "mtdonly", None),
    )

    return (image_max_size, erase_block_size, list(part_table))
