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

SUMMARY = "Read a PHY register through the kernel MDIO bus"
DESCRIPTION = "Reads a PHY register with SIOCGMIIREG, so the access takes \
the kernel's MDIO bus lock instead of racing with phylib. Used by \
oob-status.sh on platforms whose OOB switch or MGMT PHY shares an MDIO bus \
with the BMC's own kernel-managed PHY (Wedge100, Fuji)."
SECTION = "base"
PR = "r1"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://Makefile;md5=5999ad4c71b4cb72e16f70c403b8fa29"

S = "${UNPACKDIR}"
LOCAL_URI = " \
    file://Makefile \
    file://mii-read.c \
    "

pkgdir = "mii-read"
binfiles = "mii-read"

CFLAGS += "-Wall -Werror "

do_install() {
  dst="${D}/usr/local/fbpackages/${pkgdir}"
  bin="${D}/usr/local/bin"
  install -d $dst
  install -d $bin
  for f in ${binfiles}; do
    install -m 755 $f ${dst}/$f
    ln -snf ../fbpackages/${pkgdir}/$f ${bin}/$f
  done
}

FBPACKAGEDIR = "${prefix}/local/fbpackages"
FILES:${PN} = "${FBPACKAGEDIR}/mii-read ${prefix}/local/bin"
