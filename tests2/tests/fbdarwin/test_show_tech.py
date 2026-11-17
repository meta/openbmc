#!/usr/bin/env python3
#
# Copyright 2022-present Facebook. All Rights Reserved.
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
