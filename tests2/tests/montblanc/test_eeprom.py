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


class CHASSISEepromTest(EepromV5Test, unittest.TestCase):
    """
    Test for CHASSIS EEPROM
    """

    def set_eeprom_cmd(self):
        self.eeprom_cmd = ["/usr/bin/weutil -e chassis_eeprom"]

    def set_product_name(self):
        self.product_name = ["MINIPACK3", "MINIPACK3M", "M4052CLSI", "M4052CLSM"]

    def set_location_on_fabric(self):
        self.location_on_fabric = ["FCB"]

    """
    Montblanc Chassis EEPROM have only BMC and ASIC switch
    need to ignore x86
    """

    def test_x86_mac(self):
        pass


class SCMEepromTest(EepromV5Test, unittest.TestCase):
    """
    Test for SCM EEPROM
    """

    def set_eeprom_cmd(self):
        self.eeprom_cmd = ["/usr/bin/weutil -e scm_eeprom"]

    def set_product_name(self):
        self.product_name = ["MINIPACK3"]

    def set_location_on_fabric(self):
        self.location_on_fabric = ["SCM"]

    """
    Montblanc SCM EEPROM have only x86
    need to ignore BMC and ASIC switch
    """

    def test_bmc_mac(self):
        pass

    def test_switch_asic_mac(self):
        pass
