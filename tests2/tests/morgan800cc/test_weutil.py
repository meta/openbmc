#!/usr/bin/env python3
# Copyright (c) Facebook, Inc. and its affiliates.

# This software may be used and distributed according to the terms of the
# GNU General Public License version 2.

import unittest
import json

from utils.test_utils import qemu_check
from utils.shell_util import run_shell_cmd


@unittest.skipIf(qemu_check(), "test env is QEMU, skipped")
class Weutil(unittest.TestCase):
    def setUp(self):
        weutil_json_string = run_shell_cmd ("weutil --json")
        self.weutil_json = json.loads(weutil_json_string)
        self.weutil_version = int(self.weutil_json["Version"])
        pass

    def tearDown(self):
        pass

    def weutil_check(self, keyToCheck):
        result = False
        if keyToCheck in self.weutil_json and self.weutil_json[keyToCheck]:
            result = True

        return result

    def test_weutil_crc(self):
        self.assertEqual(
            self.weutil_check("CRC16"),
            True,
            "weutil does not contain a valid CRC",
        )

    def test_weutil_product_serial(self):
        self.assertEqual(
            self.weutil_check("Product Serial Number"),
            True,
            "weutil does not contain a valid Product Serial Number",
        )

    def test_weutil_local_mac(self):
        self.assertEqual(
            self.weutil_check("BMC MAC Base"),
            True,
            "weutil does not contain a valid local mac",
        )

    def test_weutil_product_name(self):
        self.assertEqual(
            self.weutil_check("Product Name"),
            True,
            "weutil does not contain a valid CRC",
        )

    def test_weutil_local_asset_tag(self):
        if self.weutil_version >= 6:
            self.assertIn("System Assembly Part Number", self.weutil_json)
            return

        self.assertEqual(
            self.weutil_check("System Assembly Part Number"),
            True,
            "weutil does not contain a valid Asset Tag",
        )

    def test_weutil_part_number(self):
        self.assertEqual(
            self.weutil_check("Product Part Number"),
            True,
            "weutil does not contain a valid Product Part Number",
        )

    def test_weutil_product_version(self):
        version_field = (
            "Production State" if self.weutil_version >= 6 else "Product Version"
        )
        self.assertEqual(
            self.weutil_check(version_field),
            True,
            "weutil does not contain a valid {}".format(version_field),
        )

    def test_weutil_product_sub_version(self):
        sub_version_field = (
            "Production Sub-State"
            if self.weutil_version >= 6
            else "Product Sub-Version"
        )
        self.assertEqual(
            self.weutil_check(sub_version_field),
            True,
            "weutil does not contain a valid {}".format(sub_version_field),
        )
        if self.weutil_version >= 6:
            self.assertEqual(
                self.weutil_check("Re-Spin/Variant Indicator"),
                True,
                "weutil does not contain a valid Re-Spin/Variant Indicator",
            )

