#!/usr/bin/env python3
#
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
#

import subprocess

from rest_utils import DEFAULT_TIMEOUT_SEC


# Handler for FRUID resource endpoint
def get_fruid(cmd=["weutil"]):  # noqa: B006
    result = {}
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        data, err = proc.communicate(timeout=DEFAULT_TIMEOUT_SEC)
        data = data.decode(errors="ignore")
    except subprocess.TimeoutExpired:
        proc.kill()
        data, _ = proc.communicate()
        data = data.decode(errors="ignore")

    for sdata in data.split("\n"):
        tdata = sdata.split(":", 1)
        if len(tdata) < 2:
            continue
        # need to ignore Header for weutil V3
        # Wedge EEPROM (.*):
        if tdata[0].startswith("Wedge EEPROM"):
            continue

        result[tdata[0].strip()] = tdata[1].strip()
    fresult = {"Information": result, "Actions": [], "Resources": []}
    return fresult
