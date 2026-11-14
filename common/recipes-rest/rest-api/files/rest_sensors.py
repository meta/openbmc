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

import functools
import re
import subprocess

import rest_pal_legacy
from rest_utils import DEFAULT_TIMEOUT_SEC

# Platforms whose sensor names were published with the older Aspeed I2C
# device-tree node name ("<addr>.i2c-bus"). Adapter names are "<addr>.<node>"
# and newer kernels renamed the node, which would rename every I2C sensor and
# break ODS history, so pin these platforms to "<addr>.i2c-bus" by address.
LEGACY_ASPEED_I2C_ADAPTER_PLATFORMS = {"wedge100"}


@functools.lru_cache(maxsize=64)
def legacy_adapter_name(adapter_name: str) -> str:
    if (
        rest_pal_legacy.pal_get_platform_name()
        not in LEGACY_ASPEED_I2C_ADAPTER_PLATFORMS
    ):
        return adapter_name
    addr, sep, _ = adapter_name.partition(".")
    if sep and addr.startswith("1e78a"):
        return addr + ".i2c-bus"
    return adapter_name


# Handler for sensors resource endpoint
def get_sensors():
    result = []
    proc = subprocess.Popen(["sensors"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        data, _ = proc.communicate(timeout=DEFAULT_TIMEOUT_SEC)
        data = data.decode()
    except subprocess.TimeoutExpired:
        proc.kill()
        data, _ = proc.communicate()
        data = data.decode()

    data = re.sub(r"\(.+?\)", "", data)  # noqa: W605
    for edata in data.split("\n\n"):
        adata = edata.split("\n", 1)
        sresult = {}
        if len(adata) < 2:
            break
        sresult["name"] = adata[0]
        for sdata in adata[1].split("\n"):
            tdata = sdata.split(":")
            if len(tdata) < 2:
                continue
            key = tdata[0].strip()
            value = tdata[1].strip()
            if key == "Adapter":
                value = legacy_adapter_name(value)
            sresult[key] = value
        result.append(sresult)

    fresult = {"Information": result, "Actions": [], "Resources": []}
    return fresult


# Handler for sensors-full resource endpoint

name_adapter_re = re.compile(r"(\S+)\nAdapter:\s*(\S.*?)\s*\n")
label_re = re.compile(r"(\S.*):\n")
value_re = re.compile(r"\s+(\S.*?):\s*(\S.*?)\s*\n")
skipline_re = re.compile(r".*\n?")


def get_sensors_full():  # noqa: C901
    proc = subprocess.Popen(
        ["sensors", "-u"], stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    try:
        data, _ = proc.communicate(timeout=DEFAULT_TIMEOUT_SEC)
    except subprocess.TimeoutExpired:
        proc.kill()
        data, _ = proc.communicate()

    # The output of sensors -u is a series of sections separated
    # by blank lines.  Each section looks like this:
    #   coretemp-isa-0000
    #   Adapter: ISA adapter
    #   Core 0:
    #     temp2_input: 40.000
    #     temp2_max: 110.000
    #     temp2_crit: 110.000
    #     temp2_crit_alarm: 0.000
    #   Core 1:
    #     temp3_input: 39.000
    #     temp3_max: 110.000
    #     temp3_crit: 110.000
    #     temp3_crit_alarm: 0.000
    #   ...
    #
    # We skip over malformed data silently.

    result = []
    pos = 0
    while pos < len(data):
        if data[pos] == "\n":
            pos += 1
            continue

        sresult = {}

        # match the first two lines of a section
        m = name_adapter_re.match(data, pos)
        if not m:
            # bad input, skip a line and try again
            pos = skipline_re.match(data, pos).end()
            continue
        sresult["name"] = m.group(1)
        sresult["adapter"] = legacy_adapter_name(m.group(2))
        pos = m.end()

        # match the sensors
        while True:
            # each starts with a label line
            m = label_re.match(data, pos)
            if not m:
                break
            label = m.group(1)
            pos = m.end()

            # the following lines are name-value pairs
            values = {}
            while True:
                m = value_re.match(data, pos)
                if not m:
                    break
                values[m.group(1)] = m.group(2)
                pos = m.end()

            if len(values) > 0:
                sresult[label] = values

        result.append(sresult)

    fresult = {"Information": result, "Actions": [], "Resources": []}
    return fresult
