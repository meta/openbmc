FILESEXTRAPATHS:prepend:yosemite4 := "${THISDIR}/files:"

SRC_URI:append:yosemite4 = " file://0001-Allow-opting-out-of-the-runtime-downcast-assertion.patch"
