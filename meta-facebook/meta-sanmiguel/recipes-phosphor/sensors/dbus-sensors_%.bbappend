FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://1001-Add-NVMeMI-sensor-service.patch \
    file://1002-Fix-NVMeMI-sensor-build-error.patch \
    file://1003-nvidia-gpu-add-option-to-disable-PCIe-device-support.patch \
    file://1004-nvidia-gpu-add-State.Leak.Detector-support-for-SMA-s.patch \
    file://1005-nvidia-gpu-add-event-log-for-SMA-leak-sensor-state-c.patch \
    file://1006-nvidia-gpu-add-SMA-leak-detector-associations.patch \
    file://1007-nvidia-gpu-support-PATCH-for-leak-thresholds.patch \
    file://1008-nvidia-gpu-fix-first-set-threshold-drift-in-SMA-leak.patch \
    file://1009-nvidia-gpu-recover-from-MCTP-endpoint-removal.patch \
    file://1010-sanmiguel-dbus-sensors-nvmemi-poll-unavailable-senso.patch \
    file://1011-sanmiguel-dbus-sensors-nvmemi-fix-sensor-creation-ev.patch \
    file://1012-sanmiguel-dbus-sensors-nvmemi-fix-context-handling-d.patch \
    file://1013-sanmiguel-dbus-sensors-nvmemi-classify-sensors-by-co.patch \
    file://1014-sanmiguel-dbus-sensors-nvmemi-handle-NSS-Drive-Funct.patch \
"

DEPENDS += " libnvme"
PACKAGECONFIG[nvme-mi] = "-Dnvme-mi=enabled, -Dnvme=disabled"

PACKAGECONFIG:append = " \
    nvidia-gpu \
    nvme-mi \
"

EXTRA_OEMESON:append = " \
    -Dnvidia-gpu-pcie=disabled \
"

SYSTEMD_SERVICE:${PN} += "${@bb.utils.contains('PACKAGECONFIG', 'nvme-mi', \
                                               'xyz.openbmc_project.nvmemisensor.service', \
                                               '', d)}"
