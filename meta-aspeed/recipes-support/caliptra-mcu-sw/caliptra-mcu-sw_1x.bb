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

SUMMARY = "Caliptra MCU firmware and software"
HOMEPAGE = "https://github.com/chipsalliance/caliptra-mcu-sw"

LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

BRANCH = "main"
SRC_URI = "gitsm://github.com/chipsalliance/caliptra-mcu-sw;protocol=https;branch=${BRANCH}"

SRCREV = "2b7837402328ab611968d40243075082469df7ae"

inherit cargo cargo-update-recipe-crates perlnative

require caliptra-mcu-sw_1x-crates.inc
require caliptra-mcu-sw_1x-git-crates.inc

# crates.io may reject the API download endpoint used by BitBake's crate
# fetcher; prefer the static crate tarball location first.
PREMIRRORS:prepend = " \
    https://crates.io/api/v1/crates/([^/]*)/[^/]*/download https://static.crates.io/crates/\1/ \n \
"

# After cargo_common_do_patch_paths strips git sources from Cargo.lock,
# remove stale git qualifiers and package entries made identical by that change.
python caliptra_mcu_fixup_lockfile() {
    import re

    lockfile = d.getVar("CARGO_LOCK_PATH")
    with open(lockfile, "r") as f:
        content = f.read()

    content = re.sub(r' \(git\+[^)]*\)"', '"', content)

    header, *packages = content.split("[[package]]\n")
    seen = set()
    unique = []
    for pkg in packages:
        if pkg not in seen:
            seen.add(pkg)
            unique.append(pkg)

    with open(lockfile, "w") as f:
        f.write("[[package]]\n".join([header] + unique))
}
do_configure[postfuncs] += "caliptra_mcu_fixup_lockfile"

do_compile() {
    cd ${S}

    export PERL=perl

    # openssl-sys needs the vendored OpenSSL 3.x,
    # but oe_cargo_fix_env forces OPENSSL_NO_VENDOR=1
    unset OPENSSL_NO_VENDOR

    # Cargo does not cap lints for vendored path crates; prevent new rustc
    # warnings from failing ufmt-macros, which uses #![deny(warnings)].
    export RUSTFLAGS="--cap-lints=warn"

    # Build xtask
    cargo build --offline -p xtask --release
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/target/release/xtask ${D}${bindir}
}

BBCLASSEXTEND = "native nativesdk"
