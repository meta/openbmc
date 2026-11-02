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
import mmap
from collections import namedtuple
from ctypes import CDLL
from enum import IntEnum
from typing import Dict, List  # noqa: F401


misc_lib_hndl = CDLL("libmisc-utils.so.0")


class SocModelId(IntEnum):
    SOC_MODEL_INVALID = -1
    SOC_MODEL_ASPEED_G4 = 0
    SOC_MODEL_ASPEED_G5 = 1
    SOC_MODEL_ASPEED_G6 = 2
    SOC_MODEL_ASPEED_G7 = 3
    SOC_MODEL_MAX = 4


class SocModelError(Exception):
    pass


wdt_reg = namedtuple("WDT_REG", "addr boot_source_bit")  # type: ignore

# CPU Model : [(WDT timeout status reg address,
# bit that indicates boot source), ...]
# check all WDTs to make sure
# For netboot scenario, the WDT status tells the boot source of uboot
register_map = {
    SocModelId.SOC_MODEL_ASPEED_G4: [
        wdt_reg(0x1E785010, 1),
        wdt_reg(0x1E785030, 1),
    ],
    SocModelId.SOC_MODEL_ASPEED_G5: [
        wdt_reg(0x1E785010, 1),
        wdt_reg(0x1E785030, 1),
        wdt_reg(0x1E785050, 1),
    ],
    SocModelId.SOC_MODEL_ASPEED_G6: [wdt_reg(0x1E620064, 4)],
    # AST2700 WDTA boot-source status register.  Bit 1 is set when the BMC
    # is booted from the slave flash, as used by boot_info.sh.
    SocModelId.SOC_MODEL_ASPEED_G7: [wdt_reg(0x14C3744C, 1)],
}  # type: Dict[int, List[wdt_reg]]


@functools.lru_cache(maxsize=1)
def get_soc_model() -> int:
    soc_model_id = misc_lib_hndl.get_soc_model()

    if soc_model_id in register_map.keys():
        return soc_model_id
    elif soc_model_id == SocModelId.SOC_MODEL_INVALID:
        raise SocModelError("Invalid SoC model ID")
    else:
        raise SocModelError("Unknown SoC model ID")


@functools.lru_cache(maxsize=1)
def is_boot_from_secondary() -> bool:
    wdt_regs = register_map.get(get_soc_model())
    max_offset = max(reg.addr for reg in wdt_regs)  # type: ignore

    with open("/dev/mem", "rb") as f:
        with mmap.mmap(
            f.fileno(),
            (max_offset // mmap.PAGESIZE + 1) * mmap.PAGESIZE,
            mmap.MAP_SHARED,
            mmap.PROT_READ,
        ) as reg_map:
            for reg in wdt_regs:  # type: ignore
                if reg_map[reg.addr] >> reg.boot_source_bit & 0x1 == 1:
                    return True
    return False


def main():
    soc_model = get_soc_model()

    print("SoC model: {}".format(soc_model))

    source = is_boot_from_secondary()
    print("Boot from secondary: {}".format(source))


if __name__ == "__main__":
    main()
