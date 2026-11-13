# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program in a file named COPYING; if not, write to the
# Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA

# Most BMC tools (showtech, wedge_power.sh, boot_info.sh, ...) live in
# /usr/local/bin, which only /etc/profile adds for login shells. Build sshd
# with /usr/local/{bin,sbin} in the default PATH so non-interactive
# "ssh root@bmc <tool>" works. They are appended after the system dirs, the
# same order a login shell gets from /etc/profile, so no command resolves to
# a different binary than it does today.
SSHD_DEFAULT_PATH = "/usr/bin:/bin:/usr/local/bin"
SSHD_SUPERUSER_PATH = "/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin:/usr/local/sbin"

EXTRA_OECONF += " \
    --with-default-path=${SSHD_DEFAULT_PATH} \
    --with-superuser-path=${SSHD_SUPERUSER_PATH} \
    "

do_install:append() {
    # Keep the informational comment in sshd_config in sync with the build.
    sed -i 's|^# This sshd was compiled with PATH=.*|# This sshd was compiled with PATH=${SSHD_SUPERUSER_PATH}|' \
        ${D}${sysconfdir}/ssh/sshd_config
}
