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

# meta-nexthopbmca7c (AST2720-A2) nests inside this layer and sees these
# appends too, so keep the nexthopbmc settings behind the machine override.
LINUX_VERSION_EXTENSION:nexthopbmc = "-nexthopbmc"

COMPATIBLE_MACHINE:nexthopbmc = "nexthopbmc"

FILESEXTRAPATHS:prepend := "${THISDIR}/board_config:"

SRC_URI:append:nexthopbmc = " file://nexthopbmc.cfg"
