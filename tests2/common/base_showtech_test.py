#!/usr/bin/env python3
#
# Copyright 2026-present Facebook. All Rights Reserved.
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program in a file named COPYING; if not, write to the
# Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA
#

import unittest

from utils.shell_util import run_shell_cmd
from utils.test_utils import qemu_check


@unittest.skipIf(qemu_check(), "test env is QEMU, skipped")
class BaseShowtechTest:
    """Check real common diagnostics; platforms retain their hardware assertions."""

    COMMON_SECTIONS = (
        "dmesg log",
        "/var/log/messages log",
        "journal log",
        "CPU Usage",
        "Memory Info",
        "Disk Usage",
        "Network Info",
        "Uboot Environment",
        "Network interface status",
        "I2C DETECT",
        "GPIO DUMP",
        "DEBUG LOGS",
        "HOST (uServer) CPU LOGS",
        "CURRENTLY RUNNING PROCESSES",
        "x86 mTerm Logs",
        "x86 mTerm Rotated Logs",
        "BMC BOOT INFO",
        "FLASH0 META_INFO",
        "FLASH1 META_INFO",
    )

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.show_tech_output = run_shell_cmd(
            "/usr/local/bin/showtech", ignore_err=True, errors="replace"
        )

    def _section(self, title):
        if not hasattr(self, "_showtech_sections"):
            sections = {}
            current = None
            for line in self.show_tech_output.splitlines():
                if line.startswith("####") and line.endswith("####"):
                    heading = line.strip("# \t")
                    # Ignore separator-only lines around diagnostic headings.
                    if heading:
                        current = sections.setdefault(heading, [])
                elif current is not None:
                    current.append(line)
            self._showtech_sections = sections
        self.assertIn(
            title, self._showtech_sections, f"Missing showtech section: {title}"
        )
        return "\n".join(self._showtech_sections[title]).strip()

    def test_bmc_information(self):
        self.assertRegex(self._section("BMC HOSTNAME"), r"^\S+$")
        self.assertRegex(self._section("BMC UPTIME"), r"\bup\b")
        self.assertRegex(self._section("BMC VERSION"), r"(?m)^built[ \t]*:[ \t]*\S+")
        self.assertRegex(self._section("BMC OS VERSION"), r"(?m)^Linux[ \t]+\S+")

    def test_shared_sections_present(self):
        # Require headings, not optional log files or image metadata.
        for section in self.COMMON_SECTIONS:
            with self.subTest(section=section):
                self._section(section)

    def test_system_diagnostic_content(self):
        self.assertRegex(
            self._section("Memory Info"), r"(?m)^MemTotal:[ \t]+\d+[ \t]+kB"
        )
        self.assertRegex(
            self._section("Network interface status"), r"(?m)^[ \t]*\d+:[ \t]+\S+:"
        )
        self.assertRegex(
            self._section("CURRENTLY RUNNING PROCESSES"), r"(?m)^[ \t]*\d+[ \t]+\S+"
        )
