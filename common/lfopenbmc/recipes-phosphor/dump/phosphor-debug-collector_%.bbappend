FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

# Fail the build if any patch applied to this recipe lacks a valid Upstream-Status
inherit fb-patch-status

# ALL PATCHES SHOULD HAVE AN UPSTREAM STATUS
SRC_URI:append = " \
    file://0001-dump-Add-system-dump-entry-class.patch \
    file://0002-dump-Add-system-dump-Manager.patch \
    file://0003-dump-collectors-Add-system-dump-collector-hooks.patch \
    file://0004-dump-collectors-Add-PLDM-system-dump-collector.patch \
"
