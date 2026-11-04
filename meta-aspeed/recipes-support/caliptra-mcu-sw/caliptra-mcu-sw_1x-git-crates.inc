# Hand-maintained glue generated from Cargo.lock git sources.
# This file keeps BitBake's fetch graph aligned with the workspace's git deps.

SRC_URI += " \
    gitsm://github.com/chipsalliance/caliptra-sw;protocol=https;nobranch=1;name=caliptra_sw;destsuffix=caliptra-sw;type=git-dependency \
    git://github.com/chipsalliance/caliptra-cfi.git;protocol=https;nobranch=1;name=caliptra_cfi;destsuffix=caliptra-cfi;type=git-dependency \
    git://github.com/tock/tock.git;protocol=https;nobranch=1;name=tock;destsuffix=tock;type=git-dependency \
    git://github.com/teythoon/rust-openssl.git;protocol=https;branch=justus/pqc;nobranch=1;name=openssl;destsuffix=rust-openssl;type=git-dependency \
    git://github.com/korran/ufmt.git;protocol=https;nobranch=1;name=ufmt;destsuffix=ufmt;type=git-dependency \
"

SRCREV_FORMAT = "default_caliptra_sw_caliptra_cfi_tock_openssl_ufmt"
SRCREV_caliptra_sw = "977a21dfbe129836fb930b00cf87803275255611"
SRCREV_caliptra_cfi = "a98e499d279e81ae85881991b1e9eee354151189"
SRCREV_tock = "b128ae817b86706c8c4e39d27fae5c54b98659f1"
SRCREV_openssl = "4ada1c1d7c264e052e7bb71ecddbd196a5c2a0c7"
SRCREV_ufmt = "1d0743c1ffffc68bc05ca8eeb81c166192863f33"

# Override the default cargo_common_do_patch_paths which generates wrong [patch.*]
# entries for workspace repos (points to workspace root instead of individual crate
# subdirectories). Strip git source lines from Cargo.lock and write all [patch.*]
# entries to ${S}/.cargo/config.toml (higher priority than ${CARGO_HOME}/config.toml
# per Cargo's directory hierarchy, without modifying the shared CARGO_HOME config).
python cargo_common_do_patch_paths() {
    import os
    import shutil

    lockfile = d.getVar("CARGO_LOCK_PATH")
    if not os.path.exists(lockfile):
        bb.fatal(f"{lockfile} file doesn't exist")

    lockfile_orig = lockfile + ".orig"
    if not os.path.exists(lockfile_orig):
        shutil.copy(lockfile, lockfile_orig)

    newlines = []
    with open(lockfile_orig, "r") as f:
        for line in f.readlines():
            if not line.startswith("source = \"git"):
                newlines.append(line)

    with open(lockfile, "w") as f:
        f.writelines(newlines)

    s_dir = d.getVar("S")
    cargo_dir = os.path.join(s_dir, ".cargo")
    os.makedirs(cargo_dir, exist_ok=True)
    override_config = os.path.join(cargo_dir, "config.toml")

    unpackdir = d.getVar("UNPACKDIR")
    patchsets = [
        (
            "https://github.com/chipsalliance/caliptra-sw",
            [
                ("caliptra-api",                  "caliptra-sw/api"),
                ("caliptra-api-types",             "caliptra-sw/api/types"),
                ("caliptra-auth-man-gen",          "caliptra-sw/auth-manifest/gen"),
                ("caliptra-auth-man-types",        "caliptra-sw/auth-manifest/types"),
                ("caliptra-builder",               "caliptra-sw/builder"),
                ("caliptra-cfi-derive",            "caliptra-sw/cfi/derive"),
                ("caliptra-cfi-lib",               "caliptra-sw/cfi/lib"),
                ("caliptra-coverage",              "caliptra-sw/coverage"),
                ("caliptra-cpu",                   "caliptra-sw/cpu"),
                ("caliptra-drivers",               "caliptra-sw/drivers"),
                ("caliptra-emu-bus",               "caliptra-sw/sw-emulator/lib/bus"),
                ("caliptra-emu-cpu",               "caliptra-sw/sw-emulator/lib/cpu"),
                ("caliptra-emu-crypto",            "caliptra-sw/sw-emulator/lib/crypto"),
                ("caliptra-emu-derive",            "caliptra-sw/sw-emulator/lib/derive"),
                ("caliptra-emu-periph",            "caliptra-sw/sw-emulator/lib/periph"),
                ("caliptra-emu-types",             "caliptra-sw/sw-emulator/lib/types"),
                ("caliptra-error",                 "caliptra-sw/error"),
                ("caliptra-gen-linker-scripts",    "caliptra-sw/cpu/gen"),
                ("caliptra-hw-model",              "caliptra-sw/hw-model"),
                ("caliptra-hw-model-types",        "caliptra-sw/hw-model/types"),
                ("caliptra-image-crypto",          "caliptra-sw/image/crypto"),
                ("caliptra-image-elf",             "caliptra-sw/image/elf"),
                ("caliptra-image-fake-keys",       "caliptra-sw/image/fake-keys"),
                ("caliptra-image-gen",             "caliptra-sw/image/gen"),
                ("caliptra-image-types",           "caliptra-sw/image/types"),
                ("caliptra-image-verify",          "caliptra-sw/image/verify"),
                ("caliptra-kat",                   "caliptra-sw/kat"),
                ("caliptra-lms-types",             "caliptra-sw/lms-types"),
                ("caliptra-registers",             "caliptra-sw/registers"),
                ("caliptra-registers-latest",      "caliptra-sw/hw/latest/registers"),
                ("caliptra-runtime",               "caliptra-sw/runtime"),
                ("caliptra-test",                  "caliptra-sw/test"),
                ("caliptra-test-harness-types",    "caliptra-sw/test-harness/types"),
                ("caliptra-x509",                  "caliptra-sw/x509"),
                ("caliptra_common",                "caliptra-sw/common"),
                ("crypto",                         "caliptra-sw/dpe/crypto"),
                ("dpe",                            "caliptra-sw/dpe/dpe"),
                ("platform",                       "caliptra-sw/dpe/platform"),
                ("ureg",                           "caliptra-sw/ureg"),
            ],
        ),
        (
            "https://github.com/chipsalliance/caliptra-cfi.git",
            [
                ("caliptra-cfi-derive-git", "caliptra-cfi/derive"),
                ("caliptra-cfi-lib-git",    "caliptra-cfi/lib"),
            ],
        ),
        (
            "https://github.com/tock/tock.git",
            [
                ("capsules-core",    "tock/capsules/core"),
                ("capsules-extra",   "tock/capsules/extra"),
                ("capsules-system",  "tock/capsules/system"),
                ("components",       "tock/boards/components"),
                ("enum_primitive",   "tock/libraries/enum_primitive"),
                ("kernel",           "tock/kernel"),
                ("riscv",            "tock/arch/riscv"),
                ("riscv-csr",        "tock/libraries/riscv-csr"),
                ("rv32i",            "tock/arch/rv32i"),
                ("tickv",            "tock/libraries/tickv"),
                ("tock-cells",       "tock/libraries/tock-cells"),
                ("tock-registers",   "tock/libraries/tock-register-interface"),
                ("tock-tbf",         "tock/libraries/tock-tbf"),
            ],
        ),
        (
            "https://github.com/teythoon/rust-openssl.git",
            [
                ("openssl",        "rust-openssl/openssl"),
                ("openssl-macros", "rust-openssl/openssl-macros"),
                ("openssl-sys",    "rust-openssl/openssl-sys"),
            ],
        ),
        (
            "https://github.com/korran/ufmt.git",
            [
                ("ufmt", "ufmt"),
            ],
        ),
    ]

    with open(override_config, "w") as config:
        print('[patch.crates-io]', file=config)
        print('openssl = { path = "%s" }' % os.path.join(unpackdir, "rust-openssl/openssl"), file=config)

        for repo, entries in patchsets:
            print('\n[patch."%s"]' % repo, file=config)
            for name, relpath in entries:
                print('%s = { path = "%s" }' % (name, os.path.join(unpackdir, relpath)), file=config)
}

do_configure:append() {
    # ufmt v0.2.0 (sources/ufmt) has `ufmt-write = { path = "write" }` which introduces
    # sources/ufmt/write as ufmt-write v0.1.0.  The workspace also has libtock/ufmt/write
    # as ufmt-write v0.1.0.  Two path deps at different directories with the same
    # name+version cause a lockfile collision.  Fix by redirecting ufmt's path dep for
    # ufmt-write to the workspace-internal copy so both resolve to the same directory.
    # Accept an already-patched file so do_configure can be re-run.
    grep -q -e 'ufmt-write = { path = "write"' \
            -e "ufmt-write = { path = \"${S}/runtime/userspace/libtock/ufmt/write\"" \
        "${UNPACKDIR}/ufmt/Cargo.toml" || \
        bbfatal "ufmt Cargo.toml format mismatch; update sed pattern in caliptra-mcu-sw-git-crates.inc"
    sed -i "s|ufmt-write = { path = \"write\", version = \"0.1.0\" }|ufmt-write = { path = \"${S}/runtime/userspace/libtock/ufmt/write\", version = \"0.1.0\" }|" \
        "${UNPACKDIR}/ufmt/Cargo.toml"
}
