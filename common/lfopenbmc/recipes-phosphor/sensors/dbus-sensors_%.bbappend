FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://0001-DeviceMgmt-fix-device-not-found.patch \
    file://0002-Add-structured-logging-for-Threshold-events.patch \
    file://0003-psusensor-fixed-not-activate-for-multiple-power-stat.patch \
    file://0004-psusensors-fixed-multiple-power-state-issue.patch \
    file://0005-psu-support-gpio-bridge.patch \
    file://0006-meta-facebook-ventura-add-SCM-sensor-offset.patch \
    file://0007-PWMSensor-synchronize-hardware-PWM-with-D-Bus-proper.patch \
    file://0008-leakdetector-check-event-before-event_read.patch \
    file://0009-Add-delay-before-leak-event-log-to-prevent-AC-OFF-gl.patch \
    file://0010-PSUSensor-add-property-to-sensorTable-and-labelMatch.patch \
    file://0011-hwmontemp-Remove-newly-created-sensor-from-sensorsCh.patch \
    file://0012-psusensor-skip-sensor-reads-during-firmware-updates.patch \
    file://0013-meta-facebook-yosemite4-Disable-in2_alarm-event.patch \
    file://0014-valve-monitor-request-for-directory.patch \
    file://0015-Implement-valve-monitor-service.patch \
    file://0016-psusensor-Add-support-for-per-sensor-PollRate-config.patch \
    file://0017-SmbpbiSensor-Fix-invalid-data-check-size-for-tempera.patch \
    file://0018-dbus-sensors-implement-in-place-threshold-updates-to.patch \
    file://0019-mctp-add-PowerState-check-in-MCTPReactor.patch \
    file://0020-CableMonitor-increase-reconcile-delay-to-60s.patch \
    file://0021-PSUSensor-add-ADI-MAX20912-and-MAX20916-support.patch \
    file://0022-nvmesensor-implement-in-place-threshold-updates-to-p.patch \
    file://0023-psusensor-skip-sensor-reads-during-firmware-updates.patch \
    file://0024-PSUSensor-add-MPQ82D00-PMBus-device-support.patch \
    file://0025-common-fix-missing-power-gated-sensors-after-BMC-res.patch \
    file://0026-hwmontempsensor-add-support-for-adt7461-temp-sensors.patch \
    file://0027-PSUSensor-add-MP9941-VR-sensor-support.patch \
    file://0028-dbus-sensors-preserve-state-on-external-sensor-thres.patch \
    file://0029-hwmontempsensor-add-support-for-KB9002.patch \
    file://0030-PSUSensor-add-TDA38725-and-TDA38740-support.patch \
    file://0031-PSUSensor-add-RAA229639-and-RAA229640-support.patch \
    file://0032-smbpbi-add-sensor-reading-retry-on-next-polling-when.patch \
"

SRC_URI:append:mf-fb-liquid-cooled = " \
    file://0101-valve-monitor-add-analog-valve-support.patch \
    file://0102-valve-monitor-delay-analog-valve-feedback-monitoring.patch \
    file://0103-valve-monitor-catch-event-resolve-exceptions.patch \
    file://0104-valve-monitor-defer-control-interface-until-state-is.patch \
    file://0105-Add-200ms-delay-before-creating-vt-status-log.patch \
    file://0106-valve-monitor-expose-ObjectManagers-by-namespace.patch \
    file://0107-valve-monitor-Improve-config-validation-and-inotify-.patch \
    file://0108-valve-monitor-always-apply-Open-and-Close-commands.patch \
"

SRC_URI:append:fb-compute-multihost = " \
    file://0200-Utils-support-powerState-for-multi-node-system.patch \
    file://0201-Avoid-recreating-hwmon-temp-when-blade-cycle.patch \
    file://0202-Add-retry-attempts-configuration-for-fan-sensors.patch \
"

SRC_URI:append:santabarbara = " \
    file://0301-Thresholds-Add-option-to-log-thresholds-on-second-hi.patch \
"

PACKAGECONFIG[valvemonitor] = "-Dvalve-monitor=enabled, -Dvalve-monitor=disabled"

SYSTEMD_SERVICE:${PN} += "${@bb.utils.contains('PACKAGECONFIG', 'valvemonitor', \
                                               'xyz.openbmc_project.valvemonitor.service', \
                                               '', d)}"
