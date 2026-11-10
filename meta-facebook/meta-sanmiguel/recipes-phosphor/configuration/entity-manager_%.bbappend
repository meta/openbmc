FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://0001-configurations-nvidia-p3809-hmc-add-SMBPBI-sensors-f.patch \
    file://0002-configurations-nvidia-add-VR-NVL72-configuration.patch \
    file://0003-configurations-nvidia-add-Clover-CX9-module-configur.patch \
    file://0004-configurations-nvidia-vr_nvl72-add-GPIO-leak-detecto.patch \
    file://0005-configurations-nvidia-cx9_mezzanine_module-add-NIC-s.patch \
    file://0006-configurations-nvidia-clover_cx9-add-NIC-sensor-conf.patch \
    file://0007-configurations-nvidia-p3809-hmc-Add-NvidiaMctpVdm-co.patch \
    file://0008-configurations-nvidia-cx9_mezzanine_module-add-data-.patch \
    file://0009-configurations-nvidia-cable_cartridge-change-cable-c.patch \
"
