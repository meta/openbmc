require conf/distro/openbmc-pin-rocko.inc

# Keep the modern flashrom append out of legacy builds.
BBMASK_append = " common/recipes-utils/flashrom/flashrom_%\.bbappend$"

# Prior to Honister, the licenses like GPL-2.0-only didn't exist and only
# the non-timed versions did, but these non-timed versions are now deprecated
# in Kirkstone.  We need our recipes to have the timed versions so that they
# are compatible with Kirkstone, but we need to provide mappings for old
# distros in order to avoid missing license warnings.
SPDXLICENSEMAP[GPL-2.0-only] = "GPL-2.0"
SPDXLICENSEMAP[GPL-2.0-or-later] = "GPL-2.0"
SPDXLICENSEMAP[LGPL-2.1-only] = "LGPL-2.1"
SPDXLICENSEMAP[LGPL-2.1-or-later] = "LGPL-2.1"

# This variable is removed in master but we need it in Rocko.
BB_DANGLINGAPPENDS_WARNONLY = "1"

# Master eliminated the virtual/${TARGET_PREFIX} variables, so we needed to
# adjust some recipes to use `${VIRTUAL_CROSS_CC}` and populate it here.
VIRTUAL_CROSS_CC ?= "virtual/${TARGET_PREFIX}gcc"
