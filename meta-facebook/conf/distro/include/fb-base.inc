include conf/distro/openbmc-yocto-${DISTRO_CODENAME}.inc
require conf/distro/include/fb/network.inc
require conf/distro/include/fb/packaging.inc
require conf/distro/include/fb/parallel.inc
require conf/distro/include/fb/sstate.inc
require conf/distro/include/fb/versioning.inc

DEFAULT_TIMEZONE ?= "America/Los_Angeles"

# Opt in to hardened builds with BMC_HARDENED=1. Import the environment variable
# through BB_ENV_PASSTHROUGH_ADDITIONS (BB_ENV_EXTRAWHITE before Kirkstone):
# BB_ENV_PASSTHROUGH_ADDITIONS="$BB_ENV_PASSTHROUGH_ADDITIONS BMC_HARDENED" \
#     BMC_HARDENED=1 bitbake <image-target>
DISTRO_FEATURES:append = "${@' fb-hardened' if d.getVar('BMC_HARDENED') == '1' else ''}"

# By default disable interactive patch resolution (tasks will just fail instead):
PATCHRESOLVE = "noop"

def yocto_halt_taskname(d):
    distro = d.getVar('DISTRO_CODENAME', True)
    if distro in [ 'rocko', 'dunfell' ]:
        return "ABORT"
    return "HALT"

# Disk Space Monitoring during the build
#   This comes from the Yocto default in their local.inc.sample.
BB_DISKMON_DIRS ??= "\
    STOPTASKS,${TMPDIR},1G,100K \
    STOPTASKS,${DL_DIR},1G,100K \
    STOPTASKS,${SSTATE_DIR},1G,100K \
    STOPTASKS,/tmp,100M,100K \
    ${@yocto_halt_taskname(d)},${TMPDIR},100M,1K \
    ${@yocto_halt_taskname(d)},${DL_DIR},100M,1K \
    ${@yocto_halt_taskname(d)},${SSTATE_DIR},100M,1K \
    ${@yocto_halt_taskname(d)},/tmp,10M,1K"

# We have a lot of patches that don't have an Upstream-Status, so disable this
# QA rather than modifying all the patch files. A recipe can opt back in with
# FB_ERROR_QA_REMOVE:remove = "patch-status".
FB_ERROR_QA_REMOVE ?= "patch-status"
ERROR_QA:remove = "${FB_ERROR_QA_REMOVE}"

# Pick up defaults for sstate hashing from poky.conf
BB_SIGNATURE_HANDLER ?= "OEEquivHash"
BB_HASHSERVE ??= "auto"
