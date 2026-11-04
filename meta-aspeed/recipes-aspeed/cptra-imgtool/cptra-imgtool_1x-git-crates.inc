# Hand-maintained glue generated from Cargo.lock git sources.
# This file keeps BitBake's fetch graph aligned with the workspace's git deps.

SRC_URI += " \
    git://github.com/chipsalliance/caliptra-sw;protocol=https;nobranch=1;name=caliptra_sw;destsuffix=caliptra-sw;type=git-dependency \
    git://github.com/clundin25/rust-openssl.git;protocol=https;nobranch=1;name=openssl;destsuffix=rust-openssl;type=git-dependency \
    git://github.com/korran/ufmt.git;protocol=https;nobranch=1;name=ufmt;destsuffix=ufmt;type=git-dependency \
"

SRCREV_FORMAT = "default_caliptra_sw_openssl_ufmt"

SRCREV_caliptra_sw = "951209a31c4373658f89e4b1211f3a557731c7d9"
SRCREV_openssl = "4ada1c1d7c264e052e7bb71ecddbd196a5c2a0c7"
SRCREV_ufmt = "1d0743c1ffffc68bc05ca8eeb81c166192863f33"

python cargo_common_do_patch_paths() {
    import os
    import shutil

    unpackdir = d.getVar("UNPACKDIR")

    def restore_lockfile(lock_path):
        lock_orig = lock_path + ".orig"
        if os.path.exists(lock_path):
            if not os.path.exists(lock_orig):
                shutil.copy(lock_path, lock_orig)
        elif not os.path.exists(lock_orig):
            return

        shutil.copy(lock_orig, lock_path)

    restore_lockfile(d.getVar("CARGO_LOCK_PATH"))

    caliptra_sw_lockfile = os.path.join(unpackdir, "caliptra-sw/Cargo.lock")
    restore_lockfile(caliptra_sw_lockfile)

    # Write patch config to ${S}/.cargo/config.toml so it takes priority over
    # ${CARGO_HOME}/config.toml per Cargo's directory hierarchy rules, without
    # modifying the shared CARGO_HOME config.
    s_dir = d.getVar("S")
    cargo_dir = os.path.join(s_dir, ".cargo")
    os.makedirs(cargo_dir, exist_ok=True)
    override_config = os.path.join(cargo_dir, "config.toml")

    patchsets = [
        (
            "https://github.com/chipsalliance/caliptra-sw",
            [
                ("caliptra-api", "caliptra-sw/api"),
                ("caliptra-api-types", "caliptra-sw/api/types"),
                ("caliptra-auth-man-gen", "caliptra-sw/auth-manifest/gen"),
                ("caliptra-auth-man-types", "caliptra-sw/auth-manifest/types"),
                ("caliptra-cfi-derive", "caliptra-sw/cfi/derive"),
                ("caliptra-cfi-lib", "caliptra-sw/cfi/lib"),
                ("caliptra-drivers", "caliptra-sw/drivers"),
                ("caliptra-error", "caliptra-sw/error"),
                ("caliptra-image-crypto", "caliptra-sw/image/crypto"),
                ("caliptra-image-elf", "caliptra-sw/image/elf"),
                ("caliptra-image-fake-keys", "caliptra-sw/image/fake-keys"),
                ("caliptra-image-gen", "caliptra-sw/image/gen"),
                ("caliptra-image-serde", "caliptra-sw/image/serde"),
                ("caliptra-image-types", "caliptra-sw/image/types"),
                ("caliptra-lms-types", "caliptra-sw/lms-types"),
                ("caliptra-registers", "caliptra-sw/registers"),
                ("caliptra-registers-latest", "caliptra-sw/hw/latest/registers"),
                ("ureg", "caliptra-sw/ureg"),
            ],
        ),
        (
            "https://github.com/chipsalliance/caliptra-cfi.git",
            [
                ("caliptra-cfi-derive", "caliptra-sw/cfi/derive"),
                ("caliptra-cfi-lib", "caliptra-sw/cfi/lib"),
            ],
        ),
        (
            "https://github.com/clundin25/rust-openssl.git",
            [
                ("openssl", "rust-openssl/openssl"),
                ("openssl-macros", "rust-openssl/openssl-macros"),
                ("openssl-sys", "rust-openssl/openssl-sys"),
            ],
        ),
        (
            "https://github.com/korran/ufmt.git",
            [
                ("ufmt", "ufmt"),
                ("ufmt-macros", "ufmt/macros"),
                ("ufmt-write", "ufmt/write"),
            ],
        ),
    ]

    with open(override_config, "w") as config:
        for repo, entries in patchsets:
            print('\n[patch."%s"]' % repo, file=config)
            for name, relpath in entries:
                print('%s = { path = "%s" }' % (name, os.path.join(unpackdir, relpath)), file=config)

        # Redirect [patch.crates-io] openssl to the locally fetched path.
        print('\n[patch.crates-io]', file=config)
        print('openssl = { path = "%s" }' % os.path.join(unpackdir, "rust-openssl/openssl"), file=config)
}
