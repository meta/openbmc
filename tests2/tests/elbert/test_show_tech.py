#!/usr/bin/env python3
#
# Copyright 2021-present Facebook. All Rights Reserved.
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
import re
import unittest

from common.base_showtech_test import BaseShowtechTest
from utils.cit_logger import Logger
from utils.shell_util import run_shell_cmd
from utils.test_utils import qemu_check


def collect_show_tech():
    show_tech_cmd = "/usr/local/bin/showtech"
    # showtech.sh lets rule stderr through; show_tech.py used to capture it.
    # It also passes raw log bytes (e.g. mTerm console noise) through, which
    # show_tech.py stripped as non-ASCII.
    return run_shell_cmd(show_tech_cmd, ignore_err=True, errors="replace")


@unittest.skipIf(qemu_check(), "test env is QEMU, skipped")
class ShowTechTest(BaseShowtechTest, unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.show_tech_output = collect_show_tech()

    def setUp(self):
        Logger.start(name=self._testMethodName)

    def tearDown(self):
        Logger.info("Finished logging for {}".format(self._testMethodName))

    def test_smb_powergood_status(self):
        powergood_status = "0x1"
        status_match = re.search(
            r"##### SWITCHCARD POWERGOOD STATUS #####\s*{}".format(powergood_status),
            self.show_tech_output,
        )
        self.assertTrue(status_match, "Switchcard powergood status is bad")

    def test_fpga_version_detected(self):
        fpgas = [
            "SCM_FPGA",
            "SMB_FPGA",
            "SMB_CPLD",
            "FAN_FPGA",
        ]
        for fpga in fpgas:
            driver_not_detected_match = re.search(
                r"{}:\s*FPGA_DRIVER_NOT_DETECTED".format(fpga), self.show_tech_output
            )
            self.assertFalse(
                driver_not_detected_match, "{} driver not detected".format(fpga)
            )

            no_version_match = re.search(
                r"{}:\s*0.0".format(fpga), self.show_tech_output
            )
            self.assertFalse(no_version_match, "{} version not detected".format(fpga))

    def test_pim_fpga_version_detected(self):
        for pim in range(2, 10):
            version_not_detected_match = re.search(
                r"PIM\s*{}:\s*VERSION_NOT_DETECTED".format(pim), self.show_tech_output
            )
            self.assertFalse(
                version_not_detected_match,
                "PIM{} FPGA version not detected".format(pim),
            )
