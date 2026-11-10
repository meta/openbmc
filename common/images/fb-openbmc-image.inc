OVERRIDES:append = ":pn-obmc-phosphor-image"
require recipes-phosphor/images/fb-phosphor-image.inc
require recipes-phosphor/images/obmc-phosphor-image.bb
require common/images/fb-openbmc-common.inc

# Include the package group that enables tests2 support.
IMAGE_INSTALL += "packagegroup-openbmc-tests2"

# Add the mfgtool.
IMAGE_INSTALL += "mfg-tool"

# Add overlay-helper for developers
IMAGE_INSTALL += "overlay-helper"

# Ad-hoc sensors and Meta OEM metrics (bmc-oem-metric)
IMAGE_INSTALL += "adhoc-sensor"

# BMC Health Services: failed, restarting and boot-stuck service counts (bmc-oem-metric)
IMAGE_INSTALL += "bmc-services-health"
