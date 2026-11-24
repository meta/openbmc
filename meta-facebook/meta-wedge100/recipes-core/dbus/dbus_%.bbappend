# Copyright 2026-present Facebook. All Rights Reserved.

# The BMC has no X server. With x11 autolaunch enabled, dbus-launch is the
# only thing in the image that links libX11, which pulls in libX11, libxcb,
# libXau and libXdmcp (~1.3 MB uncompressed). Dropping it keeps the zstd
# image within the 28 MiB flash layout.
PACKAGECONFIG:remove = "x11"
