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
import typing as t

import rest_fruid
from rest_helper import read_gpio_by_shadow


WEDGE40 = ["Wedge-DC-F", "Wedge-AC-F"]


def read_wedge_back_ports(legacy=False):
    if legacy:
        bhinfo = {
            "port_1": {
                "pin_1": read_gpio_by_shadow("BLOODHOUND_GPIOP0"),
                "pin_2": read_gpio_by_shadow("BLOODHOUND_GPIOP1"),
                "pin_3": read_gpio_by_shadow("BLOODHOUND_GPIOP2"),
                "pin_4": read_gpio_by_shadow("BLOODHOUND_GPIOP3"),
            },
            "port_2": {
                "pin_1": read_gpio_by_shadow("BLOODHOUND_GPIOP4"),
                "pin_2": read_gpio_by_shadow("BLOODHOUND_GPIOP5"),
            },
        }
    else:
        bhinfo = {
            "port_1": {
                "pin_1": read_gpio_by_shadow("RMON1_PF"),
                "pin_2": read_gpio_by_shadow("RMON1_RF"),
                "pin_3": read_gpio_by_shadow("RMON2_PF"),
                "pin_4": read_gpio_by_shadow("RMON2_RF"),
            },
            "port_2": {
                "pin_1": read_gpio_by_shadow("RMON3_PF"),
                "pin_2": read_gpio_by_shadow("RMON3_RF"),
            },
        }
    return bhinfo


@functools.lru_cache(maxsize=1)
def _check_wedge() -> t.Tuple[bool, bool]:
    """
    Returns if a given BMC unit is a wedge RSW and whether it's a wedge40
    -> (is_wedge, is_wedge40)
    """

    # Check if this is a wedge40 from fruinfo
    fruinfo = rest_fruid.get_fruid()
    product_name = fruinfo["Information"].get("Product Name", "")
    if re.match("WEDGE.*", product_name, re.IGNORECASE):
        return True, product_name in WEDGE40

    # Not a wedge
    return False, False


def get_gpios():
    is_wedge, is_wedge40 = _check_wedge()
    if is_wedge:
        return {"back_ports": read_wedge_back_ports(is_wedge40)}
    return {}
