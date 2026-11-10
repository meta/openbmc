FILESEXTRAPATHS:prepend:openbmc-fb-lf := "${THISDIR}/files:"

# EXTRA_OEMESON:append:openbmc-fb-lf = " -Doem-meta=enabled"

PACKAGECONFIG[oem-arm] = "-Doem-arm=enabled, -Doem-arm=disabled"

SRC_URI:append:openbmc-fb-lf = " \
    file://host_eid \
    file://0001-pldm-Revise-image-path-for-update.patch \
    file://0002-platform-mc-Handle-get-PLDM-Commands-failure.patch \
    file://0003-platform-mc-Add-MCTP-recovery-option.patch \
    file://0004-platform-mc-Add-option-for-discovering-FRU-data.patch \
    file://0005-common-Add-blocking-instance-ID-allocation.patch \
    file://0006-platform-mc-Add-retries-for-terminus-discovery.patch \
    file://0007-platform-mc-Make-fallback-terminus-name-optional.patch \
    file://0008-oem-meta-Add-file-IO-responder-for-crashdump-from-BI.patch \
    file://0009-oem-meta-Add-APML-alert-handler.patch \
    file://0010-oem-meta-Add-MCTP-configuration-discovery.patch \
    file://0011-pldm-Increase-maximum-of-dbus-timeout-value.patch \
    file://0012-terminus-replace-inventory-and-sensor-configuration.patch \
    file://0013-terminus-support-firmware-parameter-refresh.patch \
    file://0014-requester-refresh-MCTP-endpoints-after-host-reaches-.patch \
    file://0015-oem-meta-Implement-special-event-handling.patch \
    file://0016-oem-meta-Add-retry-event-deduplication-for-unified-B.patch \
    file://0017-oem-meta-Add-call-to-the-fw-versions-sd-retimer-serv.patch \
    file://0018-Add-back-sensor-polling-time-configuration.patch \
    file://0019-Add-back-maximum-transfer-size-configuration.patch \
    file://0020-Add-back-instance-id-expiration-interval.patch \
    file://0021-fw_update-Reimplement-package-parser-to-use-new-libp.patch \
    file://0022-oem-meta-support-MCTP-I2C-and-I3C-target-configs.patch \
    file://0023-platform-mc-add-helper-to-get-terminus-name.patch \
    file://0024-oem-arm-handle-boot-progress-sensor-events.patch \
    file://0025-oem-arm-support-UINT64-boot-progress-events.patch \
    file://0026-platform-mc-use-tagged-numeric-sensor-decode.patch \
    file://0027-oem-meta-don-t-drop-OEM-event-during-MCTP-endpoint-r.patch \
    file://0028-oem-meta-bound-the-OEM-event-record-before-formattin.patch \
    file://0029-fw-update-Fix-redundant-inventory-entries-for-single.patch \
    file://0030-fw_update-stop-sensor-polling-during-update-when-con.patch \
    file://0031-requester-complete-queued-requests-on-endpoint-cance.patch \
    file://0032-platform-mc-drop-stale-discovery-work-on-endpoint-re.patch \
    file://0033-platform-mc-cper-submit-events-for-processing.patch \
    file://0034-platform-mc-Add-option-to-control-terminus-D-Bus-obj.patch \
    file://0035-oem-arm-accept-events-from-indexed-PHX-termini.patch \
"

# ARM crashlog support, PLDM file transfer dependencies, and compatibility fixes
SRC_URI:append:openbmc-fb-lf = " \
    file://0201-platform-mc-NumericSensor-Don-t-update-thresholds-fo.patch \
    file://0202-platform-mc-NumericSensor-Remove-useMetricInterface-.patch \
    file://0203-platform-mc-NumericSensor-Introduce-non-DBus-impleme.patch \
    file://0204-platform-mc-NumericSensor-Support-File-Size-Monitori.patch \
    file://0205-platform-mc-Receive-File-Size-Monitoring-sensor-from.patch \
    file://0206-platform-mc-Parse-File-Descriptor-PDR.patch \
    file://0207-platform-mc-Support-PLDM-File-Transfer-commands.patch \
    file://0208-platform-mc-Create-File-Descriptor-class.patch \
    file://0209-pldmtool-Add-pldm-file-parsing.patch \
    file://0210-platform-mc-Expose-internal-PLDM-file-transfer-API.patch \
    file://0211-platform-mc-Increase-file-transfer-socket-wait-toler.patch \
    file://0212-platform-mc-Fix-file-transfer-FD-lifetime.patch \
    file://0213-oem-arm-parse-PLDM-Device-File-state-events.patch \
    file://0214-oem-arm-collect-crashlog-files-as-a-system-dump.patch \
    file://0215-platform-mc-update-object_path-namespace-for-sdbuspl.patch \
    file://0216-platform-mc-NumericSensor-fix-rebase-of-useMetricInt.patch \
"

# Must apply after common/lfopenbmc pldm patches.
SRC_URI:append:yosemite4 = " \
    file://0100-yosemite4-pldm-remove-firmware-update-entry.patch \
"

SYSTEMD_AUTO_ENABLE:${PN}:openbmc-fb-lf = "enable"

do_install:append:openbmc-fb-lf() {
    install -d ${D}/usr/share/pldm
    install -m 0444 ${UNPACKDIR}/host_eid ${D}/usr/share/pldm
}

