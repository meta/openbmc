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

# fw-util reads the image metadata partition at a fixed flash offset.
# Keep it in sync with FBOBMC_IMAGE_META_OFFSET in
# recipes-core/images/nexthopbmca7c-image-layout.inc.
CXXFLAGS:append:nexthopbmca7c = " -DFW_UTIL_IMAGE_META_OFFSET=0x003E8000"
