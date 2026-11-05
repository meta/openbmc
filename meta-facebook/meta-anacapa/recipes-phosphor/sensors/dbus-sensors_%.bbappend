FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://1001-hwmontempsensor-discard-out-of-range-readings.patch \
    file://1002-hwmon-temp-Support-software-calibration-offset-and-s.patch \
"
