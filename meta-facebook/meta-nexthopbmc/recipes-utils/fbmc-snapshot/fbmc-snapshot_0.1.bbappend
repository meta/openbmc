# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
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

# Override the layer's oob-mdio-util.sh and oob-status.sh with nexthopbmc
# specific versions (OOB MDIO bus per SoC, auto-detected Marvell 88E6341 /
# 88E6191X OOB switch), and i2c_scan.sh to skip the SSIF slave bus.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# oob-status.sh delegates its 'counters' subcommand to the compiled helper.
RDEPENDS:${PN} += "oob-switch-util"
