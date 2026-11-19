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

# Packages shared by the Nexthop BMC card images: nexthopbmc (AST2600) and
# nexthopbmca7c (AST2720-A2) sit on the same CPU cards.
require recipes-core/images/fboss-lite-image.inc

# The kernel unpacks a zstd initramfs several times faster than lzma.
# Kernels of images using this file need CONFIG_RD_ZSTD (on by default).
require recipes-core/images/fb-zstd-rootfs.inc

IMAGE_INSTALL += " \
  apml \
  ssifd \
  "
