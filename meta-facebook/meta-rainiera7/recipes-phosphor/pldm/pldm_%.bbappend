EXTRA_OEMESON:append = " -Dfw-update-pkg-inotify=enabled "
EXTRA_OEMESON:append = " -Dmctp-recovery=disabled "
EXTRA_OEMESON:append = " -Denable-fallback-terminus-name=disabled "
EXTRA_OEMESON:append = " -Ddiscovery-fru-data-from-terminus=disabled "
EXTRA_OEMESON:append = " -Ddbus-timeout-value=30 "
EXTRA_OEMESON:append = " -Dmaximum-transfer-size=128 "

# Follow upstream pldm commit dfb79e8893, "platform-mc: add InstanceNo
# to terminus name", we already updated Rainier's virtual sensor input
# to have the InstanceNo. i.e. PHX_1_*. Keep it for PVT Build.
EXTRA_OEMESON:remove = "-Dterminus-name-instance-suffix=disabled"
EXTRA_OEMESON:append = " -Dterminus-name-instance-suffix=enabled"

PACKAGECONFIG:append = " oem-arm"

do_install:append:openbmc-fb-lf() {
    rm -f ${D}/usr/share/pldm/host_eid
}
