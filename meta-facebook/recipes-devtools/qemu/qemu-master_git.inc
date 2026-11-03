# QEMU recipe that builds from git
# This recipe is based on poky's qemu recipe but uses git

# Recipe renamed from qemu-git to qemu-master to avoid pn-overrides QA error
# (having "git" in PN conflicts with BitBake overrides)

# Point to the qemu-git directory for SRC_URI files
FILESEXTRAPATHS:prepend := "${THISDIR}/qemu-git:"
SUMMARY = "Fast open source processor emulator (git master branch)"
DESCRIPTION = "QEMU is a hosted virtual machine monitor: it emulates the \
machine's processor through dynamic binary translation and provides a set \
of different hardware and device models for the machine, enabling it to run \
a variety of guest operating systems. This recipe builds from git master."
HOMEPAGE = "http://qemu.org"
LICENSE = "GPL-2.0-only AND LGPL-2.1-only"

DEPENDS += "bison-native libusb1 meson-native ninja-native"

require ${COREBASE}/meta/recipes-devtools/qemu/qemu-targets.inc

inherit pkgconfig update-rc.d systemd python3native

LIC_FILES_CHKSUM = "file://COPYING;md5=a3b50d8b88dcc0eb3d7d39b760b9e821 \
                    file://COPYING.LIB;endline=24;md5=8a8178c06478747a771588adec965232"

# QEMU reference - commit SHA for reproducible builds
# To update to latest master, run:
#   git ls-remote https://github.com/qemu/qemu.git refs/heads/master | cut -f1
# This is normally updated by the daily yocto sync job when
# --refresh-patches is enabled
QEMU_REF ?= "3a202dab30470b3ee61a694350370513e916b4f3"

# Use git protocol (required by BitBake QA)
SRC_URI = "git://github.com/qemu/qemu.git;protocol=https;branch=master;lfs=0 \
           file://powerpc_rom.bin \
           file://qemu-guest-agent.init \
           file://qemu-guest-agent.udev \
           "

SRC_URI:append = " \
           file://0500-net-slirp-Add-mfr-id-and-oob-eth-addr-parameters.patch \
           file://0501-aspeed-Zero-extend-flash-files-to-128MB.patch \
           file://0502-hw-aspeed_vic-Add-heartbeat-LED-registers.patch \
           file://0503-hw-misc-Add-i2c-netdev-device.patch \
           file://0504-hw-misc-Add-byte-by-byte-i2c-network-device.patch \
           file://0505-hw-intc-Add-a-property-to-allow-GIC-to-reset-into-no.patch \
           file://0506-hw-m25p80-Add-BP-and-TB-bits-to-n25q00.patch \
           file://0507-hw-i2c-pca954x-Add-method-to-get-channels.patch \
           file://0508-hw-arm-aspeed-Add-fb_machine_class_init.patch \
           file://0509-fbttn-Add-I2C-SCL-timeout-property.patch \
           file://0510-hw-i2c-aspeed-Add-bus-ID-to-all-trace-events.patch \
           file://0511-hw-i2c-aspeed-Add-slave-event-traces.patch \
           file://0512-hw-i2c-aspeed-Fix-bus-derivation-for-slave-events.patch \
           \
           file://0513-aspeed-Re-add-hw-arm-fby35.c-dual-SoC-fby35-machine-.patch \
           file://0514-hw-i2c-aspeed-Fix-interrupt-status-flag-names.patch \
           file://0515-tests-Create-qtest-for-Aspeed-I2C-controller.patch \
           file://0516-scripts-Add-redact-util.patch \
           file://0517-aspeed-Expose-i2c-buses-to-machine.patch \
           file://0518-aspeed-fby35-Add-bmc-bb-and-nic-FRU-s.patch \
           file://0519-hw-misc-aspeed-Add-fby35-sb-cpld.patch \
           file://0520-hw-misc-aspeed-Add-intel-me.patch \
           file://0521-hw-misc-aspeed-Add-fby35-server-board-bridge-IC.patch \
           file://0522-hw-arm-aspeed-Switch-fby35-grandcanyon-to-n25q00.patch \
           \
           file://0524-fby35-Setup-I2C-devices-and-GPIO-s.patch \
           file://0525-fby35-Add-motherboard-fru-EEPROM-to-BIC.patch \
           file://0526-qemu-Add-i2c-devices-to-oby35-cl.patch \
           file://0527-hw-arm-aspeed-Don-t-initialize-fby35-bmc-GPIO-s.patch \
           file://0528-aspeed-Add-grandteton-bmc.patch \
           file://0529-aspeed-Add-greatlakes-bmc.patch \
           file://0530-aspeed-Add-Sandia.patch \
           file://0531-aspeed-add-montblanc-bmc-reference-from-fuji.patch \
           file://0532-aspeed-add-janga-bmc.patch \
           file://0533-hw-ssi-aspeed_smc-don-t-bus-fault-on-flash-window-ac.patch \
           file://0534-hw-i2c-aspeed-mask-I2CS-slave-address-to-7-bits.patch \
           file://0535-add-usbredir-subproject-for-QEMU-usb-redir.patch \
           file://0536-hw-i2c-aspeed-add-target-buffer-mode-RX-support.patch \
           "

SRC_URI:append = " \
           file://0001-qemu-Add-addition-environment-space-to-boot-loader-q.patch \
           file://0002-apic-fixup-fallthrough-to-PIC.patch \
           file://0003-qemu-Do-not-include-file-if-not-exists.patch \
           file://0004-qemu-Determinism-fixes.patch \
           file://0005-tests-meson.build-use-relative-path-to-refer-to-file.patch \
           file://0006-Define-MAP_SYNC-and-MAP_SHARED_VALIDATE-on-needed-li.patch \
           file://0007-Subject-PATCH-configure-lookup-meson-exutable-from-P.patch \
           file://0008-qemu-Ensure-pip-and-the-python-venv-aren-t-used-for-.patch \
           file://0009-Add-MAX31790-fan-controller-device-emulation-for-BMC.patch \
           file://0010-Add-PCF8563-Real-Time-Clock-device-emulation-for-BMC.patch \
           "

SRC_URI:append = " \
           file://0601-docs-system-arm-aspeed-add-the-catalina-bmc-machine.patch \
           file://0602-docs-system-arm-aspeed-add-the-sanmiguel-bmc-machine.patch \
           file://0603-hw-arm-sanmiguel-remove-the-second-SPI-flash-control.patch \
           file://0604-hw-i2c-pca954x-drop-unused-header-inclusions.patch \
           file://0605-hw-i2c-pca954x-add-PCA9545-4-channel-mux-model.patch \
           file://0606-hw-i2c-pca954x-migrate-mux-state.patch \
           file://0607-tests-qtest-pca9545-add-mux-test.patch \
           file://0608-hw-rtc-ds1338-move-TYPE_DS1338-to-a-public-header.patch \
           file://0609-hw-ds1338-use-the-TYPE_DS1338-constant-instead-of-th.patch \
           file://0610-hw-rtc-ds1338-implement-Resettable-reset.patch \
           file://0611-hw-rtc-ds1338-prefix-the-file-local-helpers-with-ds1.patch \
           file://0612-hw-rtc-ds1338-parameterise-the-register-map-via-a-de.patch \
           file://0613-hw-rtc-ds1338-program-the-clock-from-the-time-regist.patch \
           file://0614-hw-rtc-ds1338-implement-the-DS1338-clock-halt-bit.patch \
           file://0615-hw-rtc-ds1338-refresh-the-user-registers-on-a-read-r.patch \
           file://0616-hw-rtc-ds1338-add-a-persist-on-reset-property.patch \
           file://0617-tests-qtest-ds1338-cover-the-DS1338-specific-registe.patch \
           file://0618-tests-qtest-ds1338-cover-the-power-on-register-defau.patch \
           file://0619-tests-qtest-ds1338-cover-the-clock-halt-bit.patch \
           file://0620-tests-qtest-ds1338-cover-persist-on-reset.patch \
           file://0621-tests-qtest-ds1338-cover-the-reserved-bits-of-the-ti.patch \
           file://0622-hw-rtc-ds1338-add-DS1339-variant.patch \
           file://0623-tests-qtest-ds1339-add-RTC-test.patch \
           file://0624-tests-qtest-ds1339-cover-power-on-defaults-and-reser.patch \
           file://0625-tests-qtest-ds1339-cover-oscillator-stop-restart-and.patch \
           file://0626-tests-qtest-ds1339-cover-the-Century-bit.patch \
           file://0627-tests-qtest-ds1339-cover-the-end-of-month-rollover.patch \
           file://0628-tests-qtest-ds1339-cover-the-12-hour-format.patch \
           file://0629-tests-qtest-ds1339-cover-the-day-of-week-counter.patch \
           file://0630-tests-qtest-ds1339-cover-persist-on-reset.patch \
           file://0631-tests-qtest-ds1339-cover-the-migration-round-trip.patch \
           file://0632-hw-sensor-ina230-add-current-voltage-power-monitor.patch \
           file://0633-tests-qtest-ina230-add-current-voltage-power-monitor.patch \
           file://0634-tests-qtest-ina230-cover-voltage-injection-and-power.patch \
           file://0635-tests-qtest-ina230-cover-the-ALERT-limit-functions-a.patch \
           file://0636-tests-qtest-ina230-cover-the-injection-property-limi.patch \
           file://0637-tests-qtest-ina230-cover-operating-modes-and-trigger.patch \
           file://0638-tests-qtest-ina230-cover-ALERT-latch-persistence.patch \
           file://0639-tests-qtest-ina230-cover-a-cleared-calibration-per-c.patch \
           file://0640-hw-sensor-ina238-add-current-voltage-power-monitor.patch \
           file://0641-tests-qtest-ina238-add-current-voltage-power-monitor.patch \
           file://0642-tests-qtest-ina238-cover-voltage-temperature-injecti.patch \
           file://0643-tests-qtest-ina238-cover-the-ALERT-path-and-conversi.patch \
           file://0644-tests-qtest-ina238-cover-the-injection-property-limi.patch \
           file://0645-tests-qtest-ina238-cover-the-operating-modes.patch \
           file://0646-tests-qtest-ina238-cover-conversion-ready-and-alert-.patch \
           file://0647-tests-qtest-ina238-cover-a-cleared-calibration-per-c.patch \
           file://0648-hw-arm-minerva-add-Facebook-BMC-machine.patch \
           file://0649-hw-arm-minerva-populate-EEPROM-data.patch \
           file://0650-tests-functional-arm-minerva-add-BMC-boot-test.patch \
           file://0651-hw-sensor-tmp421-do-not-report-a-stale-errno-on-a-pa.patch \
           file://0652-hw-sensor-tmp421-name-the-parent-object-field-parent.patch \
           file://0653-hw-sensor-tmp421-implement-Resettable-reset.patch \
           file://0654-hw-sensor-tmp421-model-device-variants-with-a-class-.patch \
           file://0655-hw-sensor-tmp421-honor-per-variant-channels-and-chan.patch \
           file://0656-hw-sensor-tmp421-implement-register-write-masks-one-.patch \
           file://0657-hw-sensor-tmp421-describe-the-temperature-properties.patch \
           file://0658-hw-sensor-tmp421-keep-the-measurement-across-a-range.patch \
           file://0659-hw-sensor-tmp421-migrate-the-exact-measurement.patch \
           file://0660-hw-sensor-tmp421-publish-the-variant-type-names.patch \
           file://0661-hw-arm-aspeed-use-the-TMP421-type-name-macros.patch \
           file://0662-tests-qtest-tmp421-cover-device-IDs-and-reset-defaul.patch \
           file://0663-tests-qtest-tmp421-cover-temperature-encoding-and-ra.patch \
           file://0664-tests-qtest-tmp421-cover-per-variant-channels-and-en.patch \
           file://0665-tests-qtest-tmp421-cover-software-reset-and-N-factor.patch \
           file://0666-tests-qtest-tmp421-cover-the-register-form-of-the-mi.patch \
           file://0667-hw-sensor-tmp105-add-the-TMP1075-variant.patch \
           file://0668-tests-qtest-tmp105-cover-the-TMP1075-device-ID-and-r.patch \
           file://0669-tests-qtest-tmp105-cover-the-TMP1075-config-register.patch \
           file://0670-tests-qtest-tmp105-cover-the-Config-low-byte-across-.patch \
           file://0671-hw-arm-clemente-add-Facebook-Clemente-BMC-machine.patch \
           file://0672-hw-arm-clemente-populate-the-FRU-EEPROMs.patch \
           file://0673-hw-gpio-pca9552-restate-every-pin-level-on-reset.patch \
           file://0674-hw-gpio-pca9552-add-the-PCA9539-variant-with-a-hardw.patch \
           file://0675-hw-gpio-pca9552-restate-every-pin-level-after-a-migr.patch \
           file://0676-tests-qtest-pca9539-cover-the-PCA9539-I-O-expander.patch \
           file://0677-tests-qtest-pca9539-cover-the-output-levels-across-a.patch \
           file://0678-hw-arm-bletchley-match-the-device-tree.patch \
           file://0679-hw-arm-bletchley-select-the-board-revision-with-a-ma.patch \
           file://0680-hw-arm-bletchley-populate-the-v1.0-FRU-EEPROMs.patch \
           file://0681-hw-arm-bletchley-populate-the-v1.5-FRU-EEPROMs.patch \
           file://0682-tests-functional-arm-bletchley-cover-the-board-revis.patch \
           file://0683-tests-functional-add-a-pure-Python-device-locator-re.patch \
           file://0684-tests-functional-add-a-device-locator-self-check.patch \
           file://0685-tests-functional-aspeed-add-hwmon-sensor-read-helper.patch \
           file://0686-tests-functional-arm-sanmiguel-add-BMC-boot-test.patch \
           file://0687-tests-functional-arm-catalina-test-TMP75.patch \
           file://0688-tests-functional-arm-catalina-test-PCA9555-IO-expand.patch \
           file://0689-hw-arm-catalina-drive-pca9554-io-expander-input-pins.patch \
           file://0690-tests-functional-arm-catalina-test-PCA9554-IO-expand.patch \
           file://0691-hw-arm-santabarbara-add-Facebook-Santabarbara-BMC-ma.patch \
           file://0692-hw-arm-santabarbara-populate-the-FRU-EEPROMs.patch \
           file://0693-hw-arm-yosemite5-add-Facebook-Yosemite-5-BMC-machine.patch \
           file://0694-hw-arm-yosemite5-populate-the-FRU-EEPROMs.patch \
           file://0695-hw-arm-bletchley-correct-the-file-copyright-header.patch \
           file://0696-hw-arm-harma-add-Facebook-Harma-BMC-machine.patch \
           file://0697-hw-arm-harma-populate-the-FRU-EEPROMs.patch \
           file://0698-hw-arm-ventura-add-Facebook-Ventura-BMC-machine.patch \
           file://0699-hw-arm-ventura-populate-the-FRU-EEPROMs.patch \
           file://0700-tests-functional-arm-minerva-test-INA230.patch \
           file://0701-tests-functional-arm-minerva-test-INA238.patch \
           file://0702-tests-functional-arm-clemente-add-BMC-boot-test.patch \
           file://0703-tests-functional-arm-santabarbara-add-BMC-boot-test.patch \
           file://0704-tests-functional-arm-yosemite5-add-BMC-boot-test.patch \
           file://0705-tests-functional-arm-bletchley-exercise-the-I2C-sens.patch \
           file://0706-tests-functional-arm-harma-add-BMC-boot-test.patch \
           file://0707-tests-functional-arm-ventura-add-BMC-boot-test.patch \
           file://0708-hw-i2c-aspeed_i2c-only-raise-the-target-IRQ-for-enab.patch \
           file://0709-hw-i2c-aspeed_i2c-only-clear-target-status-the-ISR-h.patch \
           file://0710-hw-i2c-aspeed_i2c-model-AST2700-I2CC_SIRQ_LOG-target.patch \
           file://0711-hw-i2c-aspeed_i2c-fix-AST2700-target-buffer-mode-rec.patch \
           file://0712-hw-arm-bletchley-take-the-FRU-MAC-from-the-configure.patch \
           file://0713-slirp-pin-the-guest-address-with-a-static-DHCP-lease.patch \
           file://0714-hw-i2c-aspeed_i2c-hold-the-target-RX-ACK-until-the-I.patch \
           file://0715-tests-functional-arm-bletchley-expect-the-FRU-MAC-of-.patch \
           file://0716-tests-functional-arm-device_locator-drop-the-SanMigu.patch \
           file://0717-hw-usb-usb-net-derive-CDC-framing-from-the-endpoint-.patch \
           file://0718-hw-usb-usb-net-add-high-speed-descriptors.patch \
           file://0719-hw-arm-aspeed-add-a-machine-hook-for-USB-devices.patch \
           file://0720-hw-arm-catalina-attach-a-USB-Ethernet-device.patch \
           file://0721-tests-functional-arm-catalina-cover-the-USB-Ethernet.patch \
           "

# slirp is a subproject: its patches need a patchdir, or they hit QEMU's tree.
SRC_URI:append = " \
           file://slirp-0001-bootp-honour-static-DHCP-leases.patch;patchdir=subprojects/slirp \
           "

QEMU_CHECKOUT_DIR = "${BPN}-${PV}"
QEMU_CHECKOUT_DIR:openbmc-fb-dunfell = "git"
QEMU_CHECKOUT_DIR:openbmc-fb-kirkstone = "git"
QEMU_CHECKOUT_DIR:openbmc-fb-rocko = "git"

S:openbmc-fb-dunfell = "${UNPACKDIR}/${QEMU_CHECKOUT_DIR}"
S:openbmc-fb-kirkstone = "${UNPACKDIR}/${QEMU_CHECKOUT_DIR}"
S:openbmc-fb-rocko = "${UNPACKDIR}/${QEMU_CHECKOUT_DIR}"

SRCREV = "${QEMU_REF}"

# Meson wrap subprojects - these are fetched separately since meson wraps
# are not git submodules. destsuffix puts them in the right location.
# Note: rust crates are skipped since we do not need them.
SRC_URI:append = " \
    git://gitlab.com/qemu-project/keycodemapdb.git;protocol=https;nobranch=1;destsuffix=${QEMU_CHECKOUT_DIR}/subprojects/keycodemapdb;name=keycodemapdb \
    git://gitlab.com/qemu-project/berkeley-softfloat-3.git;protocol=https;nobranch=1;destsuffix=${QEMU_CHECKOUT_DIR}/subprojects/berkeley-softfloat-3;name=berkeley-softfloat-3 \
    git://gitlab.com/qemu-project/berkeley-testfloat-3.git;protocol=https;nobranch=1;destsuffix=${QEMU_CHECKOUT_DIR}/subprojects/berkeley-testfloat-3;name=berkeley-testfloat-3 \
    git://gitlab.com/qemu-project/dtc.git;protocol=https;nobranch=1;destsuffix=${QEMU_CHECKOUT_DIR}/subprojects/dtc;name=dtc \
    git://gitlab.freedesktop.org/slirp/libslirp.git;protocol=https;nobranch=1;destsuffix=${QEMU_CHECKOUT_DIR}/subprojects/slirp;name=slirp \
    git://gitlab.com/qemu-project/libvfio-user.git;protocol=https;nobranch=1;destsuffix=${QEMU_CHECKOUT_DIR}/subprojects/libvfio-user;name=libvfio-user \
    git://gitlab.com/libblkio/libblkio.git;protocol=https;nobranch=1;destsuffix=${QEMU_CHECKOUT_DIR}/subprojects/libblkio;name=libblkio \
    git://gitlab.freedesktop.org/spice/usbredir.git;protocol=https;nobranch=1;destsuffix=${QEMU_CHECKOUT_DIR}/subprojects/usbredir;name=usbredir \
    "

# Pin subproject revisions (from .wrap files)
SRCREV_keycodemapdb = "f5772a62ec52591ff6870b7e8ef32482371f22c6"
SRCREV_berkeley-softfloat-3 = "b64af41c3276f97f0e181920400ee056b9c88037"
SRCREV_berkeley-testfloat-3 = "e7af9751d9f9fd3b47911f51a5cfd08af256a9ab"
SRCREV_dtc = "b6910bec11614980a21e46fbccc35934b671bd81"
SRCREV_slirp = "26be815b86e8d49add8c9a8b320239b9594ff03d"
SRCREV_libvfio-user = "4d9f663450fa80ff375612dbbafe073700e3d3d8"
SRCREV_libblkio = "f84cc963a444e4cb34813b2dcfc5bf8526947dc0"
# usbredir v0.13.0 (pins libusbredirparser for QEMU's usb-redir device)
SRCREV_usbredir = "5fc0e1c43194d948545941d408f4c10d084eb6ed"

SRCREV_FORMAT = "default_keycodemapdb_berkeley-softfloat-3_berkeley-testfloat-3_dtc_slirp_libvfio-user_libblkio_usbredir"

# Disable shallow git clone - QEMU repo requires full clone for checkout
BB_GIT_SHALLOW = "0"

PV = "${QEMU_REF}+git${SRCPV}"

CVE_STATUS[CVE-2007-0998] = "not-applicable-config: The VNC server can expose host files uder some circumstances. We don't enable it by default."

# https://bugzilla.redhat.com/show_bug.cgi?id=1609015#c11
CVE_STATUS[CVE-2018-18438] = "disputed: The issues identified by this CVE were determined to not constitute a vulnerability."

# As per https://bugzilla.redhat.com/show_bug.cgi?id=2203387
CVE_STATUS[CVE-2023-2680] = "not-applicable-platform: RHEL specific issue."

# NVD DB has this CVE as version-less (with "-")
CVE_STATUS[CVE-2024-6505] = "fixed-version: this CVE is fixed since 9.1.0"
CVE_STATUS[CVE-2024-8354] = "fixed-version: this CVE is fixed since 10.0.5"

CVE_STATUS[CVE-2023-1386] = "disputed: not an issue as per https://bugzilla.redhat.com/show_bug.cgi?id=2223985"

DISABLE_STATIC = ""

COMPATIBLE_HOST:mipsarchn32 = "null"
COMPATIBLE_HOST:mipsarchn64 = "null"
COMPATIBLE_HOST:riscv32 = "null"

# Per https://lists.nongnu.org/archive/html/qemu-devel/2020-09/msg03873.html
# upstream states qemu doesn't work without optimization
DEBUG_BUILD = "0"

do_install:append() {
    # Prevent QA warnings about installed ${localstatedir}/run
    if [ -d ${D}${localstatedir}/run ]; then rmdir ${D}${localstatedir}/run; fi
}

# QEMU_TARGETS is overridable variable
QEMU_TARGETS ?= "arm aarch64 i386 loongarch64 mips mipsel mips64 mips64el ppc ppc64 ppc64le riscv32 riscv64 sh4 x86_64"

EXTRA_OECONF = " \
    --prefix=${prefix} \
    --bindir=${bindir} \
    --includedir=${includedir} \
    --libdir=${libdir} \
    --mandir=${mandir} \
    --datadir=${datadir} \
    --docdir=${docdir}/${BPN} \
    --sysconfdir=${sysconfdir} \
    --libexecdir=${libexecdir} \
    --localstatedir=${localstatedir} \
    --with-suffix=${BPN} \
    --disable-strip \
    --disable-werror \
    --extra-cflags='${CFLAGS}' \
    --extra-ldflags='${LDFLAGS}' \
    --disable-download \
    --disable-docs \
    --host-cc='${BUILD_CC}' \
    --disable-af-xdp \
    ${PACKAGECONFIG_CONFARGS} \
    "

EXTRA_OECONF:append:class-target = " --cross-prefix=${HOST_PREFIX}"
EXTRA_OECONF:append:class-nativesdk = " --cross-prefix=${HOST_PREFIX}"

B = "${WORKDIR}/build"

do_configure:prepend:class-native() {
    # Append build host pkg-config paths for native target since the host may provide sdl
    BHOST_PKGCONFIG_PATH=$(PATH=/usr/bin:/bin pkg-config --variable pc_path pkg-config || echo "")
    if [ ! -z "$BHOST_PKGCONFIG_PATH" ]; then
        export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:$BHOST_PKGCONFIG_PATH
    fi
}

do_configure() {
    export PKG_CONFIG=pkg-config
    ${S}/configure ${EXTRA_OECONF}
}
do_configure[cleandirs] += "${B}"

do_install () {
    export STRIP=""
    oe_runmake 'DESTDIR=${D}' install

    # If we built the guest agent, also install startup/udev rules
    if [ -e "${D}${bindir}/qemu-ga" ]; then
        install -d ${D}${sysconfdir}/init.d/
        install -m 0755 ${UNPACKDIR}/qemu-guest-agent.init ${D}${sysconfdir}/init.d/qemu-guest-agent
        sed -i 's:@bindir@:${bindir}:' ${D}${sysconfdir}/init.d/qemu-guest-agent

        install -d ${D}${sysconfdir}/udev/rules.d/
        install -m 0644 ${UNPACKDIR}/qemu-guest-agent.udev ${D}${sysconfdir}/udev/rules.d/60-qemu-guest-agent.rules

        install -d ${D}${systemd_unitdir}/system/
        install -m 0644 ${S}/contrib/systemd/qemu-guest-agent.service ${D}${systemd_unitdir}/system
        sed -i -e 's,-/usr/bin/,-${bindir}/,g' ${D}${systemd_unitdir}/system/qemu-guest-agent.service
    fi
    # ELF binary /usr/share/qemu/s390-netboot.img has relocations in .text
    rm ${D}${datadir}/qemu/s390-netboot.img -f
    # ELF binary /usr/share/qemu/s390-ccw.img has relocations in .text [textrel]
    rm ${D}${datadir}/qemu/s390-ccw.img -f
    # We don't support PARISC and these cause strip and SDK relocation errors
    rm ${D}${datadir}/qemu/hppa* -f
}

# Disable kvm/virgl/mesa on targets that do not support it
PACKAGECONFIG:remove:darwin = "kvm virglrenderer epoxy gtk+"
PACKAGECONFIG:remove:mingw32 = "kvm virglrenderer epoxy gtk+ pie"

PACKAGECONFIG[sdl] = "--enable-sdl,--disable-sdl,virtual/libsdl2"
PACKAGECONFIG[sdl-image] = "--enable-sdl-image,--disable-sdl-image,libsdl2-image"
PACKAGECONFIG[png] = "--enable-png,--disable-png,libpng"
PACKAGECONFIG[virtfs] = "--enable-virtfs --enable-attr --enable-cap-ng,--disable-virtfs,libcap-ng attr,"
PACKAGECONFIG[aio] = "--enable-linux-aio,--disable-linux-aio,libaio,"
PACKAGECONFIG[uring] = "--enable-linux-io-uring,--disable-linux-io-uring,liburing"
PACKAGECONFIG[xen] = "--enable-xen,--disable-xen,xen-tools,xen-tools-libxenstore xen-tools-libxenctrl xen-tools-libxenguest"
PACKAGECONFIG[vnc-sasl] = "--enable-vnc --enable-vnc-sasl,--disable-vnc-sasl,cyrus-sasl,"
PACKAGECONFIG[vnc-jpeg] = "--enable-vnc --enable-vnc-jpeg,--disable-vnc-jpeg,jpeg,"
PACKAGECONFIG[libcurl] = "--enable-curl,--disable-curl,curl,"
PACKAGECONFIG[nss] = "--enable-smartcard,--disable-smartcard,nss,"
PACKAGECONFIG[curses] = "--enable-curses,--disable-curses,ncurses,"
PACKAGECONFIG[pixman] = "--enable-pixman,--disable-pixman,pixman"
PACKAGECONFIG[gtk+] = "--enable-gtk,--disable-gtk,gtk+3 gettext-native"
PACKAGECONFIG[vte] = "--enable-vte,--disable-vte,vte gettext-native"
PACKAGECONFIG[libcap-ng] = "--enable-cap-ng,--disable-cap-ng,libcap-ng,"
PACKAGECONFIG[ssh] = "--enable-libssh,--disable-libssh,libssh,"
PACKAGECONFIG[gcrypt] = "--enable-gcrypt,--disable-gcrypt,libgcrypt,"
PACKAGECONFIG[nettle] = "--enable-nettle,--disable-nettle,nettle"
PACKAGECONFIG[libusb] = "--enable-libusb,--disable-libusb,libusb1"
PACKAGECONFIG[fdt] = "--enable-fdt,--disable-fdt,dtc"
PACKAGECONFIG[alsa] = "--audio-drv-list=default,,alsa-lib"
PACKAGECONFIG[epoxy] = "--enable-opengl,--disable-opengl,libepoxy"
PACKAGECONFIG[lzo] = "--enable-lzo,--disable-lzo,lzo"
PACKAGECONFIG[dax] = "--enable-libdaxctl,--disable-libdaxctl,ndctl"
PACKAGECONFIG[numa] = "--enable-numa,--disable-numa,numactl"
PACKAGECONFIG[gnutls] = "--enable-gnutls,--disable-gnutls,gnutls"
PACKAGECONFIG[bzip2] = "--enable-bzip2,--disable-bzip2,bzip2"
PACKAGECONFIG[libiscsi] = "--enable-libiscsi,--disable-libiscsi"
PACKAGECONFIG[kvm] = "--enable-kvm,--disable-kvm"
PACKAGECONFIG[virglrenderer] = "--enable-virglrenderer,--disable-virglrenderer,virglrenderer"
# spice will be in meta-networking layer
PACKAGECONFIG[spice] = "--enable-spice,--disable-spice,spice"
PACKAGECONFIG[dbus-display] = "--enable-dbus-display,--disable-dbus-display,glib-2.0-native,dbus"
# No usbredir dependency: libusbredirparser comes from the pinned
# subprojects/usbredir (SRCREV_usbredir, patch 0536), keeping the binary
# self-contained as facebook/qemu/build.sh expects.
PACKAGECONFIG[usb-redir] = "--enable-usb-redir,--disable-usb-redir,"
PACKAGECONFIG[snappy] = "--enable-snappy,--disable-snappy,snappy"
# No glusterfs: the meson option was removed by 3822df47c240 (v11.1.0-rc0).
PACKAGECONFIG[xkbcommon] = "--enable-xkbcommon,--disable-xkbcommon,libxkbcommon"
PACKAGECONFIG[libudev] = "--enable-libudev,--disable-libudev,udev"
PACKAGECONFIG[attr] = "--enable-attr,--disable-attr,attr,"
PACKAGECONFIG[rbd] = "--enable-rbd,--disable-rbd,ceph,ceph"
PACKAGECONFIG[vhost] = "--enable-vhost-net,--disable-vhost-net,,"
PACKAGECONFIG[ust] = "--enable-trace-backends=ust,,lttng-ust,"
PACKAGECONFIG[pie] = "--enable-pie,--disable-pie,,"
PACKAGECONFIG[seccomp] = "--enable-seccomp,--disable-seccomp,libseccomp"
# libnfs is currently provided by meta-kodi
PACKAGECONFIG[libnfs] = "--enable-libnfs,--disable-libnfs,libnfs"
PACKAGECONFIG[pmem] = "--enable-libpmem,--disable-libpmem,pmdk"
PACKAGECONFIG[pulseaudio] = "--enable-pa,--disable-pa,pulseaudio"
PACKAGECONFIG[selinux] = "--enable-selinux,--disable-selinux"
PACKAGECONFIG[bpf] = "--enable-bpf,--disable-bpf,libbpf"
PACKAGECONFIG[capstone] = "--enable-capstone,--disable-capstone"
PACKAGECONFIG[rdma] = "--enable-rdma,--disable-rdma"
PACKAGECONFIG[vde] = "--enable-vde,--disable-vde"
PACKAGECONFIG[fuse] = "--enable-fuse --enable-fuse-lseek,--disable-fuse --disable-fuse-lseek,fuse3"
PACKAGECONFIG[slirp] = "--enable-slirp,--disable-slirp,libslirp"
PACKAGECONFIG[brlapi] = "--enable-brlapi,--disable-brlapi"
PACKAGECONFIG[jack] = "--enable-jack,--disable-jack,jack,"
PACKAGECONFIG[debuginfo] = "--enable-libdw,--disable-libdw,elfutils"
PACKAGECONFIG[pipewire] = "--enable-pipewire,--disable-pipewire,pipewire"
PACKAGECONFIG[sndio] = "--enable-sndio,--disable-sndio,sndio"

INSANE_SKIP:${PN}-common = "arch"

FILES:${PN} += "${datadir}/icons"

# For user who want to install all arch packages
PACKAGES =+ "${PN}-common"
RDEPENDS:${PN} += "${PN}-common"

ALLOW_EMPTY:${PN} = "1"
FILES:${PN} = ""

FILES:${PN}-common = "${bindir}/* ${includedir}/* ${libexecdir}/* ${datadir}/* ${localstatedir}"

PACKAGES_DYNAMIC += "^${PN}-user-.*  ^${PN}-system-.*"

PACKAGESPLITFUNCS =+ "split_qemu_packages"

python split_qemu_packages () {
    archdir = d.expand('${bindir}/')
    subpackages = do_split_packages(d, archdir, r'^qemu-system-(.*)$', '${PN}-system-%s', 'QEMU full system emulation binaries(%s)' , prepend=True, extra_depends='${PN}-common')

    subpackages += do_split_packages(d, archdir, r'^qemu-((?!system|edid|ga|img|io|nbd|pr-helper|storage-daemon).*)$', '${PN}-user-%s', 'QEMU full user emulation binaries(%s)' , prepend=True, extra_depends='${PN}-common')
    if subpackages:
        d.appendVar('RDEPENDS:' + d.getVar('PN'), ' ' + ' '.join(subpackages))
}

# Put the guest agent in a separate package
PACKAGES =+ "${PN}-guest-agent"
SUMMARY:${PN}-guest-agent = "QEMU guest agent"
FILES:${PN}-guest-agent += " \
    ${bindir}/qemu-ga \
    ${sysconfdir}/udev/rules.d/60-qemu-guest-agent.rules \
    ${sysconfdir}/init.d/qemu-guest-agent \
    ${systemd_unitdir}/system/qemu-guest-agent.service \
"

INITSCRIPT_PACKAGES = "${PN}-guest-agent"
INITSCRIPT_NAME:${PN}-guest-agent = "qemu-guest-agent"
INITSCRIPT_PARAMS:${PN}-guest-agent = "defaults"

SYSTEMD_PACKAGES = "${PN}-guest-agent"
SYSTEMD_SERVICE:${PN}-guest-agent = "qemu-guest-agent.service"

# Apply meson packagefiles to subprojects
# QEMU's subprojects/packagefiles/ contains meson.build and patches for each
# subproject that meson normally applies during download. Since we fetch
# subprojects via SRC_URI, we need to apply these manually.
do_apply_packagefiles() {
    for subdir in ${S}/subprojects/packagefiles/*; do
        subname=$(basename "$subdir")
        target="${S}/subprojects/${subname}"
        if [ -d "$target" ] && [ -d "$subdir" ]; then
            cp -rf "$subdir"/* "$target"/
            bbnote "Applied packagefiles to subproject: $subname"
        fi
    done
}
addtask apply_packagefiles after do_unpack before do_patch
