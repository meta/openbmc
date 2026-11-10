FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://0001-lg2-commit-Allow-users-to-provide-additional-data.patch \
"

DEPENDS:append = " \
    libcper \
"

PACKAGECONFIG:append = "cper-processor"

PACKAGECONFIG[cper-processor] = " \
        -Dcper-processor=enabled, \
        -Dcper-processor=disabled, \
        libcper \
"
FILES:${PN}-base:append = " \
        ${@bb.utils.contains('PACKAGECONFIG', 'cper-processor', '${libexecdir}/phosphor-logging/phosphor-cper-processor', '', d)} \
"

DBUS_SERVICE:${PN}-base:append = " \
        ${@bb.utils.contains('PACKAGECONFIG', 'cper-processor', 'xyz.openbmc_project.Logging.CPER.Processor.service', '', d)} \
"

EXTRA_OEMESON:append = " \
  -Damd-event-log=enabled \
  -Dcper-processor=enabled \
  -Druntime-metadata-plugin=xyz.openbmc_project.Logging.Extension.CPER.Processed \
"
