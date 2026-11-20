FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append:aspeed-g7 = " \
    file://wdta-timer.cfg \
"
