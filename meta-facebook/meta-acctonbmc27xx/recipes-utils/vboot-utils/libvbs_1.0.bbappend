# Copyright 2026-present Facebook. All Rights Reserved.
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

# AST2700 uses the Caliptra/BootMCU boot flow, which has no compatible VBS
# SRAM handoff.  Keep libvbs available for fw-util, but make its VBS API
# report unsupported without accessing /dev/mem.
CFLAGS:append:acctonbmc27xx = " -DLIBVBS_UNSUPPORTED"
