FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://0001-i2c-vr-add-preUpdateFirmware-hook-for-MPS-devices.patch \
    file://0002-i2cvr-pause-sensor-reads-during-CRC-retrieval.patch \
    file://0003-common-restore-VR-page-after-update-or-CRC-access.patch \
    file://0004-i2c-vr-add-support-for-MPS-MP2940x-VR-firmware-updat.patch \
    file://0005-i2c-vr-add-support-for-TI-TPS544X27-VR-firmware-upda.patch \
    file://0006-spi-Fix-output-pipe-creation-for-asyncSystem-call.patch \
"

SRC_URI:append:mf-fb-rainier = " \
    file://0100-i2cvr-cache-VR-version-to-allow-read-access-when-hos.patch \
"
