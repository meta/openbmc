# Keep GNU timeout available separately without replacing BusyBox commands.
# A standalone binary avoids pulling the coreutils multi-call binary into it.
PACKAGECONFIG:remove:class-target = "single-binary"
PACKAGE_BEFORE_PN:append:class-target = " ${PN}-timeout"
FILES:${PN}-timeout = "${bindir}/timeout.${BPN}"

# The main package still registers the timeout alternative and must retain its
# target, which update-alternatives renames to timeout.coreutils before splitting.
RDEPENDS:coreutils:append:class-target = " coreutils-timeout"
