FILESEXTRAPATHS:prepend := "${THISDIR}/u-boot-aspeed-sdk:"

# The 32MB NOR, 64MB and 128MB NOR layouts use the same configuration
SRC_URI:append = " \
    file://fw_env_flash_nor.config \
    file://fw_env_ast2700_ufs.config \
    file://u-boot-initial-env \
"

ENV_CONFIG_FILE = "fw_env_flash_nor.config"
ENV_CONFIG_FILE:df-phosphor-mmc = "${@bb.utils.contains('MACHINE_FEATURES', 'mf-ufs', \
                'fw_env_ast2700_ufs.config', 'fw_env_ast2600_mmc.config', d)}"

# AST2700 gets the generated default environment from u-boot-aspeed-env.
LIBUBOOTENV_INSTALL_INITIAL_ENV = "1"
# UNCOMMENT IN NEXT DIFF
#LIBUBOOTENV_INSTALL_INITIAL_ENV:aspeed-g7 = "0"

do_install:append () {
    install -d ${D}${sysconfdir}
    install -m 644 ${UNPACKDIR}/${ENV_CONFIG_FILE} ${D}${sysconfdir}/fw_env.config
    if [ "${LIBUBOOTENV_INSTALL_INITIAL_ENV}" = "1" ]; then
        install -m 644 ${UNPACKDIR}/u-boot-initial-env ${D}${sysconfdir}/u-boot-initial-env
    fi

    # Compatibility links: meta-phosphor hardcodes /sbin/fw_printenv and
    # /sbin/fw_setenv (clear-once.service, phosphor-static-norootfs-init),
    # matching the historical u-boot-fw-utils install path. .
    install -d ${D}${base_sbindir}
    ln -sr ${D}${bindir}/fw_printenv ${D}${base_sbindir}/fw_printenv
    ln -sr ${D}${bindir}/fw_setenv ${D}${base_sbindir}/fw_setenv
}

FILES:${PN}-bin:append = " \
    ${base_sbindir}/fw_printenv \
    ${base_sbindir}/fw_setenv \
"

# Pull in the tools and environment config together
RDEPENDS:${PN}:append = " ${PN}-bin"

# fw_env.config is machine specific
PACKAGE_ARCH = "${MACHINE_ARCH}"
