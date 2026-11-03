FILESEXTRAPATHS:prepend:openbmc-fb-lf := "${THISDIR}/files:"

SRC_URI:append:openbmc-fb-lf = "\
    file://rsyslog-remote.conf.in \
    "

# Fixed upstream in 8.2608.0; drop once meta-oe moves past 8.2606.0.
SRC_URI:append:openbmc-fb-lf = "${@' file://0001-imjournal-stop-invalidation-reopen-busy-loop.patch' if d.getVar('PV') == '8.2606.0' else ''}"

do_install:append:openbmc-fb-lf() {
  rsysconf="${D}${sysconfdir}/rsyslog.d"
  install -d ${rsysconf}
  remote_conf="${rsysconf}/remote.conf"
  install -m 644 ${UNPACKDIR}/rsyslog-remote.conf.in ${remote_conf}
  sed -i "s/__OPENBMC_VERSION__/${OPENBMC_VERSION}/g" ${remote_conf}
}
