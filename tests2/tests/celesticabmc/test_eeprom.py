#!/usr/bin/env python3
#
# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
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
import unittest

from common.base_eeprom_test import EepromV5Test
from tests.celesticabmc.helper.utils import BoardRevision, get_platform_id


class ChassisEepromTest(EepromV5Test, unittest.TestCase):
    """
    Test for CHASSIS EEPROM
    """

    def set_eeprom_cmd(self):
        self.eeprom_cmd = ["weutil"]

    def set_product_name(self):
        platform_to_product_name_map = {
            BoardRevision.BRD_TYPE_TAHANSB800BC: ["TAHANSB800BC", "TAHANSB800BCM"],
            BoardRevision.BRD_TYPE_ICECUBE: ["ICECUBE", "ICECUBEM"],
            BoardRevision.BRD_TYPE_ICETEA: ["ICETEA", "ICETEAM"],
            BoardRevision.BRD_TYPE_LADAKH800BCLS: ["LADAKH800BCLS", "LADAKH800BCLSM"],
            BoardRevision.BRD_TYPE_LEH800BCLS: ["LEH800BCLS", "LEH800BCLSM"],
            BoardRevision.BRD_TYPE_M4061CLSCL: ["M4061CLSCL"],
        }
        self.product_name = platform_to_product_name_map.get(get_platform_id())
        if self.product_name is None:
            raise ValueError(f"Unsupported platform: {get_platform_id()}")

    def set_location_on_fabric(self):
        self.location_on_fabric = ["MCB"]
