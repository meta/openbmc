#!/usr/bin/env python3
#
# Copyright 2018-present Facebook. All Rights Reserved.
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


import json
import re

from utils.cit_logger import Logger
from utils.shell_util import run_cmd, run_shell_cmd


class BaseDimmUtilTest(object):
    # Accept "NO DIMM" lines in --raw output (empty DIMM slots)
    RAW_ALLOW_NO_DIMM = False

    def setUp(self):
        Logger.start(name=self._testMethodName)
        self.dimm_info = self.extraceDimmNum()

    def extraceDimmNum(self):
        """
        To extract a dimm number and its matching label
        Raises:
            Exception: raise expection to fail the test
            if no dimm number and label is extracted
        Returns:
            dict: {dimm_num: str, dimm_label: str}
        """
        cmd = ["dimm-util"]
        out = run_shell_cmd(cmd, expected_return_code=254)
        pattern = (
            r"DIMM number\s+"
            + r"\((?P<dimm_num>\d+),.*\).+\n.+\n\s+"
            + r"DIMM Label\s+\((?P<dimm_label>[A-Z]+\d+),"
        )
        m = re.search(pattern, out)
        if m:
            return m.groupdict()
        else:
            raise Exception("unable to get dimm info, output:{}".format(out))

    def verifyLabel(self, label):
        """
        verify dimm label format, ex: A0, B1..etc
        """
        self.assertRegex(
            label, r"[A-Z]{1}[0-9]{1}", "incorrect label: {}".format(label)
        )

    def verifySerial(self, sn):
        """
        verify dimm serial format.
        should either be: NO DIMM or cap character and number only
        ex:
        DIMM A0 Serial Number: 73D6EFFC
        DIMM A1 Serial Number: NO DIMM
        """
        self.assertRegex(sn, r"NO DIMM|[0-9A-Z]+", "incorrect s/n: {}".format(sn))

    def verifyPart(self, part):
        """
        verify dimm part format.
        should either be: NO DIMM or cap character, number and dash(-) only
        ex:
        DIMM A0 Part Number: HMA84GR7JJR4N-VK
        DIMM A1 Part Number: NO DIMM
        """
        self.assertRegex(
            part, r"NO DIMM|[0-9A-Z]+-[A-Z0-9]+", "incorrect part: {}".format(part)
        )

    def testSerial(self):
        """
        verify output: dimm-util [FRU] --serial
        """
        cmd = ["dimm-util", self.FRU, "--serial"]
        out = run_cmd(cmd)
        pattern = r"DIMM (?P<label>.+) Serial Number: (?P<sn>.+)\n"
        m = re.search(pattern, out)
        if m:
            self.verifyLabel(m.group("label"))
            self.verifySerial(m.group("sn"))
        else:
            self.assertTrue(False, "unexpect output: {}".format(out))

    def testSerialDimmNumber(self):
        """
        verify output: dimm-util [FRU] --serial --dimm [dimm number]
        """
        cmd = ["dimm-util", self.FRU, "--serial", "--dimm", self.dimm_info["dimm_num"]]
        out = run_cmd(cmd)
        pattern = (
            r"DIMM " + self.dimm_info["dimm_label"] + r" Serial Number: (?P<sn>.+)\n"
        )
        m = re.search(pattern, out)
        if m:
            self.verifySerial(m.group("sn"))
        else:
            self.assertTrue(False, "unexpect output: {}".format(out))

    def testSerialDimmLabel(self):
        """
        verify output: dimm-util [FRU] --serial -dimm [dimm label]
        """
        cmd = [
            "dimm-util",
            self.FRU,
            "--serial",
            "--dimm",
            self.dimm_info["dimm_label"],
        ]
        out = run_cmd(cmd)
        pattern = (
            r"DIMM " + self.dimm_info["dimm_label"] + r" Serial Number: (?P<sn>.+)\n"
        )
        m = re.search(pattern, out)
        if m:
            self.verifySerial(m.group("sn"))
        else:
            self.assertTrue(False, "unexpect output: {}".format(out))

    def testSerialJson(self):
        """
        verify if `--json` give a valid json output
        """
        cmd = ["dimm-util", self.FRU, "--serial", "--json"]
        out = run_cmd(cmd)
        try:
            json.loads(out)
            self.assertTrue(True)
        except Exception:
            self.assertTrue(False, "output not json format: {}".format(out))

    def testPart(self):
        """
        verify output: dimm-util [FRU] --part
        """
        cmd = ["dimm-util", self.FRU, "--part"]
        out = run_cmd(cmd)
        pattern = r"DIMM (?P<label>.+) Part Number: (?P<part>.+)\n"
        m = re.search(pattern, out)
        if m:
            self.verifyLabel(m.group("label"))
            self.verifyPart(m.group("part"))
        else:
            self.assertTrue(False, "unexpect output: {}".format(out))

    def testPartDimmNumber(self):
        """
        verify output: dimm-util [FRU] --part --dimm [dimm number]
        """
        cmd = ["dimm-util", self.FRU, "--part", "--dimm", self.dimm_info["dimm_num"]]
        out = run_cmd(cmd)
        pattern = (
            r"DIMM " + self.dimm_info["dimm_label"] + r" Part Number: (?P<part>.+)\n"
        )
        m = re.search(pattern, out)
        if m:
            self.verifyPart(m.group("part"))
        else:
            self.assertTrue(False, "unexpect output: {}".format(out))

    def testPartDimmLabel(self):
        """
        verify output: dimm-util [FRU] --part -dimm [dimm label]
        """
        cmd = ["dimm-util", self.FRU, "--part", "--dimm", self.dimm_info["dimm_label"]]
        out = run_cmd(cmd)
        pattern = (
            r"DIMM " + self.dimm_info["dimm_label"] + r" Part Number: (?P<part>.+)\n"
        )
        m = re.search(pattern, out)
        if m:
            self.verifyPart(m.group("part"))
        else:
            self.assertTrue(False, "unexpect output: {}".format(out))

    def testRawDimmNumber(self):
        """
        verify output: dimm-util [FRU] --raw --dimm [dimm_num]
        we specify the dimm number to speed the test
        expected output:
        Fru: mb
        DIMM A0
        000: 23 12 0c 01 85 29 00 08 00 60 00 03 08 0b 80 00
        010: 00 00 06 0d f8 3f 00 00 6e 6e 6e 11 00 6e f0 0a
        """
        cmd = ["dimm-util", self.FRU, "--raw", "--dimm", self.dimm_info["dimm_num"]]
        out = run_cmd(cmd)
        pattern = (
            r"$|" + r"^DIMM [A-Z]{1}[0-9]{1}.*$|" + r"^[a-f0-9]{3}:( [0-9a-f]{2}){16}$"
        )
        if self.RAW_ALLOW_NO_DIMM:
            pattern += r"|^NO DIMM$"
        if self.FRU == "all":
            pattern = r"^[Ff][Rr][Uu]: [a-z0-9]+" + pattern
        else:
            pattern = r"^[Ff][Rr][Uu]: " + self.FRU + pattern
        if out:
            for output in [line for line in out.split("\n") if len(line) > 0]:
                self.assertRegex(
                    output.strip(), pattern, "unexpected output: {}".format(out)
                )
        else:
            self.assertTrue(False, "empty output")

    def testCacheDimmNumber(self):
        """
        verify output: dimm-util [FRU] --cache --dimm [dimm_num]
        we specify the dimm number to speed the test
        expected output:
        DIMM A0 Serial Number (cached): FCEFD673
        DIMM A0 Part Number   (cached): HMA84GR7JJR4N-VK
        """
        cmd = ["dimm-util", self.FRU, "--cache", "--dimm", self.dimm_info["dimm_num"]]
        out = run_cmd(cmd)
        pattern = (
            r"^DIMM "
            + re.escape(self.dimm_info["dimm_label"])
            + r" (Serial|Part) Number\s+\(cached\): "
            + r"(NO DIMM|[0-9A-Z]+(-[0-9A-Z]+)*)$"
        )
        if out:
            for output in [line for line in out.split("\n") if len(line) > 0]:
                self.assertRegex(
                    output.strip(), pattern, "unexpected output: {}".format(out)
                )
        else:
            self.assertTrue(False, "empty output")

    def testConfigDimmNumber(self):
        """
        verify output: dimm-util [FRU] --config --dimm [dimm_num]
        we specify the dimm number to speed the test
        expected output:
        FRU: mb
        DIMM A0:
                Size: 32768 MB
                Type: DDR4 SDRAM
                Speed: 2666
                Manufacturer: SK Hynix
                Serial Number: 73D6EFFC
                Part Number: HMA84GR7JJR4N-VK
                Manufacturing Date: 2020 Week21
        """
        cmd = ["dimm-util", self.FRU, "--config", "--dimm", self.dimm_info["dimm_num"]]
        out = run_cmd(cmd)
        pattern = (
            r"\nDIMM "
            + self.dimm_info["dimm_label"]
            + r":\n\t.*Size: \d+ MB\n\t"
            + r".*Type:.*\n\t"
            + r"Speed:.*\n\t"
            + r"Manufacturer:.*\n\t"
            + r"Serial Number:.*\n\t"
            + r"Part Number:.*\n\t"
            + r"Manufacturing Date:.*\n"
        )
        if self.FRU == "all":
            pattern = r"FRU: [a-z0-9]+" + pattern
        else:
            pattern = r"FRU: " + self.FRU + pattern
        if out:
            self.assertRegex(out, pattern, "unexpected output: {}".format(out))
        else:
            self.assertTrue(False, "empty output")
