ROOTFS_POSTPROCESS_COMMAND += "openbmc_rootfs_fixup;"

OPENBMC_HOSTNAME ?= "bmc"

openbmc_rootfs_fixup() {
    # hostname
    if [ "${OPENBMC_HOSTNAME}" != "" ]; then
        echo ${OPENBMC_HOSTNAME} > ${IMAGE_ROOTFS}/etc/hostname
    else
        echo ${MACHINE} > ${IMAGE_ROOTFS}/etc/hostname
    fi

    # version
    echo "OpenBMC Release ${OPENBMC_VERSION}" > ${IMAGE_ROOTFS}/etc/issue
    echo >> ${IMAGE_ROOTFS}/etc/issue
    echo "OpenBMC Release ${OPENBMC_VERSION} %h" > ${IMAGE_ROOTFS}/etc/issue.net
    echo >> ${IMAGE_ROOTFS}/etc/issue.net

    # Remove all *.pyc files
    find ${IMAGE_ROOTFS} -type f -name "*.pyc" -exec rm -f {} \;

    # Remove all C files
    find ${IMAGE_ROOTFS} -type f -name "*.[ch]" -exec rm -f {} \;

    # Runtime hwdb lookups use the compiled database. Drop the text sources
    # after the package postinstall has compiled them, keeping hwdb.bin intact.
    if [ -f "${IMAGE_ROOTFS}${sysconfdir}/udev/hwdb.bin" ] || \
       [ -f "${IMAGE_ROOTFS}${nonarch_base_libdir}/udev/hwdb.bin" ] || \
       [ -f "${IMAGE_ROOTFS}${nonarch_libdir}/udev/hwdb.bin" ]; then
        rm -rf "${IMAGE_ROOTFS}${sysconfdir}/udev/hwdb.d" \
               "${IMAGE_ROOTFS}${nonarch_base_libdir}/udev/hwdb.d" \
               "${IMAGE_ROOTFS}${nonarch_libdir}/udev/hwdb.d"
    fi

}
