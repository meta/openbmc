FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

STATE_MGR_PACKAGES += " \
    ${PN}-drive \
"

EXTRA_OEMESON:append = " \
                         -Donly-allow-boot-when-bmc-ready=false \
                       "

FILES:${PN}-drive = "${bindir}/phosphor-drive-state-manager"
DBUS_SERVICE:${PN}-drive += "xyz.openbmc_project.State.Drive.service"

# Drive action targets
DRIVE_ACTION_TARGETS = "poweron poweroff powercycle powered-on powered-off reboot hard-reboot"

DRIVE_ACTION_FMT = "obmc-drive-{0}@.target"

SYSTEMD_SERVICE:${PN}-obmc-targets += "${@compose_list(d, 'DRIVE_ACTION_FMT', 'DRIVE_ACTION_TARGETS')}"

SRC_URI += " \
    file://0001-Also-allow-power-policy-when-watchdog-flag-is-raised.patch \
    file://0002-sub-device-manager-Add-shared-entity-manager-discove.patch \
    file://0003-Add-drive-state-manager-daemon.patch \
    file://0004-systemd-target-monitor-add-important-services.patch \
"
