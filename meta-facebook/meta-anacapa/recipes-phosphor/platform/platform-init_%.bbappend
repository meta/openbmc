FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

DEPENDS:append = " phosphor-dbus-interfaces phosphor-logging"

SRC_URI:append = " \
    file://0001-Use-CLI11-callback-function-for-init-sub.patch \
    file://0002-Fix-platform_init-service-file-description.patch \
    file://0003-Add-phosphor-logging-and-PDI-deps.patch \
    file://0004-Add-initial-meta-catalina.patch \
    file://0005-cmos-reset-Add-cmos-reset-service.patch \
    file://0006-cmos-reset-Add-event-logs.patch \
    file://0007-cmos-reset-Add-power-control-capabilities.patch \
    file://0008-cmos-reset-Add-ActivationBlocksTransition.patch \
    file://0009-meta-add-Anacapa-init-support.patch \
"

SYSTEMD_SERVICE:${PN}:append = " xyz.openbmc_project.Software.Host.Updater0.service "