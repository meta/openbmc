FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://0001-CPER-add-generic-event-for-CPER-errors.patch \
    file://0002-Control-Port-Add-interface-for-monitoring-control.patch \
    file://0003-State.Drive-Add-On-Off-transitions-and-namespace-pat.patch \
    file://0004-State.Drive-Add-NotAllowed-error-to-RequestedDriveTr.patch \
    file://0005-Add-xyz.openbmc_project.PLDM.File-interface.patch \
    file://0006-sensor-events-update-FatalThreshold-redfish-mapping.patch \
"

do_write_config[depends] = ""
