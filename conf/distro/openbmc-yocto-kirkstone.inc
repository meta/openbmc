# Keep the modern flashrom append out of legacy builds.
BBMASK:append = " common/recipes-utils/flashrom/flashrom_%\.bbappend$"

# Master eliminated the virtual/${TARGET_PREFIX} variables, so we needed to
# adjust some recipes to use `${VIRTUAL_CROSS_CC}` and populate it here.
VIRTUAL_CROSS_CC ?= "virtual/${TARGET_PREFIX}gcc"
