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

SUMMARY = "Generate CPTRA authorization flash image with cptra_imgtool"
HOMEPAGE = "https://github.com/AspeedTech-BMC/cptra_imgtool"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

BRANCH = "master"
SRC_URI = "git://github.com/AspeedTech-BMC/cptra_imgtool;protocol=https;branch=${BRANCH}"

# Tag for v00.01.06
SRCREV = "4bfcd71ecdeab7c1743abdb69a3b4a36e6c619d5"

DEPENDS += "caliptra-mcu-sw"

inherit cargo cargo-update-recipe-crates perlnative

# crates.io may reject the API download endpoint used by BitBake's crate
# fetcher; prefer the static crate tarball location first.
PREMIRRORS:prepend = " \
    https://crates.io/api/v1/crates/([^/]*)/[^/]*/download https://static.crates.io/crates/\1/ \n \
"

do_compile() {
    cd ${S}

    export PERL=perl

    # openssl-sys needs the vendored OpenSSL 3.x,
    # but oe_cargo_fix_env forces OPENSSL_NO_VENDOR=1
    unset OPENSSL_NO_VENDOR

    # Cargo does not cap lints for vendored path crates; prevent new rustc
    # warnings from failing ufmt-macros, which uses #![deny(warnings)].
    export RUSTFLAGS="--cap-lints=warn"

    # Build caliptra-auth-manifest-app
    cargo build --offline -p ${CALIPTRA_AUTH_MANIFEST_APP} --release

    # Build cptra_imgtool
    cargo build --offline -p cptra-imgtool --release
    cd -
}

do_install() {
    install -d ${D}${bindir}

    install -m 0755 ${B}/target/release/${CALIPTRA_AUTH_MANIFEST_APP} ${D}${bindir}
    install -m 0755 ${B}/target/release/cptra-imgtool ${D}${bindir}
}

BBCLASSEXTEND = "native nativesdk"
