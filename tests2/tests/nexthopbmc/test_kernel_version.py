#!/usr/bin/env python3
#
# Copyright (c) 2026 Nexthop Systems Inc.
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

import unittest

from common.base_kernel_version_test import BaseKernelVersionTest


class KernelVersionTest(BaseKernelVersionTest, unittest.TestCase):
    def set_kernel_version(self):
        self.kernel_version = "6.18"
