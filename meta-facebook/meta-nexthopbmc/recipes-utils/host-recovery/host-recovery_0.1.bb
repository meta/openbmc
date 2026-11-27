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

SUMMARY = "OpenBMC Host-Recovery utilies"
SECTION = "base"
PR = "r0"
LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://fpga_update.sh;beginline=5;endline=18;md5=0b1ee7d6f844d472fa306b2fee2167e0"

S = "${UNPACKDIR}"
LOCAL_URI = " \
    file://bios_update.sh \
    file://DmiPatch.py \
    file://fpga_update.sh \
    file://fpga_utils.py \
"

HOSTRECOVERY_UTILS_FILES = " \
    bios_update.sh \
    DmiPatch.py \
    fpga_update.sh \
    fpga_utils.py \
"

do_install() {
  localbindir="${D}/usr/local/bin"
  install -d ${localbindir}
  for f in ${HOSTRECOVERY_UTILS_FILES}; do
      install -m 755 ${f} ${localbindir}/${f}
  done
}

# python3-json: DmiPatch.py (used by bios_update.sh) imports json.
RDEPENDS:${PN} += " \
    bash \
    python3-core \
    python3-json \
    python3-logging \
"

FILES:${PN} += "/usr/local/bin"
