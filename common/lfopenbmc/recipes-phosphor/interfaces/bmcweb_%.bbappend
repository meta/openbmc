FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://0100-Set-request-timeout-to-40s-on-bmcweb-http-http_conne.patch \
    file://0101-Up-the-max-connections-from-200-to-1000.patch \
    file://0102-parsing-add-parseTrustedStringAsJson-for-internal-pa.patch \
    file://0103-Add-option-to-disable-password-authentication.patch \
"

EXTRA_OEMESON:append = " -Dredfish-updateservice-use-dbus=enabled"

# Disable password authentication in hardened builds.
EXTRA_OEMESON:append = "${@bb.utils.contains('DISTRO_FEATURES', 'fb-hardened', ' -Dpassword-auth=disabled', '', d)}"

# S546922: Disable HTTP2 as it's not working with cert auth
EXTRA_OEMESON:append = " -Dhttp2=disabled"

# Enable PDI-generated Redfish Message Registries
EXTRA_OEMESON:append = " -Dredfish-allow-dbus-messages-mapping=enabled"
# Patches for PDI-generated Redfish Message Registries
SRC_URI:append = " \
    file://0200-Add-generation-for-Vendor-registries-and-a-DBus-to-R.patch \
    file://0201-registries-generate-from-phosphor-dbus-interfaces.patch \
    file://0202-Add-required-MemberId-to-the-EventRecord-definition.patch \
    file://0203-Map-DBus-event-to-RF-MessageID-and-parse-args.patch \
    file://0204-Show-mapped-and-raw-Dbus-messages-in-Log-services.patch \
"

# Dependency variables for PDI-generated Redfish Message Registries
DEPENDS += " \
    ${PYTHON_PN}-requests-native \
    phosphor-dbus-interfaces \
"
inherit python3native

SRC_URI:append:fb-compute-multihost = " \
    file://0300-Multi-host-HW-inventory-add-processor-subtree.patch \
    file://0301-Multi-host-HW-inventory-add-memory-subtree.patch \
"

SRC_URI:append = " \
    file://0350-Store-Log-Entry-URI-for-Redfish-event.patch \
"

# Drive state management - Redfish Drive.Reset action
SRC_URI:append = " \
    file://0400-storage-Add-Drive.Reset-action-with-association-base.patch \
"

# CPER support
SRC_URI:append = " \
    file://0410-LogEntry-Add-support-for-parsing-and-presenting-CPER.patch \
    file://0411-Event-carry-the-CPER-section-onto-the-pushed-EventRe.patch \
"

# Oem/Meta/Metrics in ManagerDiagnosticData, from Metric.Value objects under
# /xyz/openbmc_project/metric/bmc/oem (adhoc-sensor's /run/openbmc/metrics)
SRC_URI:append = " \
    file://0420-ManagerDiagnosticData-report-Oem-Meta-Metrics-from-m.patch \
"

# Collect PortMetrics (Metric.Value) properties into MetricReports.
# 0430 is upstream https://gerrit.openbmc.org/c/openbmc/bmcweb/+/93495
# (unmerged); 0440 builds on its nicPortMetrics table.
SRC_URI:append = " \
    file://0430-port-look-up-port-metrics-through-a-table.patch \
    file://0431-MetricReportDefinition-accept-PortMetrics-properties.patch \
"

# Carry Software.Update events into the update task's messages, so a task
# reports what the update did as Redfish Update registry messages.
SRC_URI:append = " \
    file://0440-Add-firmware-update-events-to-task-messages.patch \
    file://0441-Catch-update-events-committed-before-the-task-exists.patch \
    file://0442-Add-ResetRequired-event-to-task-messages.patch \
"
