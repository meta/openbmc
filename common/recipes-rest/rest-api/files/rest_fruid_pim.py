#!/usr/bin/env python3
#
# Copyright 2014-present Facebook. All Rights Reserved.
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

# Import helper functions for Minipack PIM information fetching
from rest_piminfo import prepare_piminfo
from rest_utils import DEFAULT_TIMEOUT_SEC


# Minipack's FRUID handler is a bit different from other FRUID handler in general,
# in that it will include some of the inforation regarding all PIMs
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

    # First, need to remove the first info line from weutil
    adata = data.split("\n", 1)
    for sdata in adata[1].split("\n"):
        tdata = sdata.split(":", 1)
        if len(tdata) < 2:
            continue
        result[tdata[0].strip()] = tdata[1].strip()

    # Then, for Minipack, also add PIM info
    pim_type, pim_fpga_ver = prepare_piminfo()
    for pim_number in range(1, 9):
        subsection = {}
        subsection["type"] = pim_type[str(pim_number)]
        subsection["FPGA version"] = pim_fpga_ver[str(pim_number)]
        result["pim" + str(pim_number)] = subsection

    fresult = {"Information": result, "Actions": [], "Resources": []}
    return fresult
