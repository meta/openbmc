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

from time import sleep

from utils.shell_util import run_shell_cmd


class UnknownSocError(Exception):
    """Raised when the Aspeed SoC cannot be determined from the device tree."""


class WatchdogUtils:
    """Watchdog utility functions."""

    _DEVMEM_CMD = "/sbin/devmem"
    _WDTCLI_CMD = "/usr/local/bin/wdtcli"
    _DT_COMPATIBLE_PATH = "/proc/device-tree/compatible"

    # Registers for the watchdog exposed as /dev/watchdog.
    _WATCHDOG_REGISTERS = {
        "ast2400": ("0x1E78500C", "0x1E785000"),
        "ast2500": ("0x1E78500C", "0x1E785000"),
        "ast2600": ("0x1E78500C", "0x1E785000"),
        "ast2700": ("0x14C3700C", "0x14C37000"),
    }

    # Boot (second) watchdog enable register per SoC; keep in sync with
    # common/recipes-utils/openbmc-utils/files/aspeed-g*/disable_watchdog.sh
    _SECOND_WDT_ENABLE_REG = {
        "ast2400": "0x1E78502C",  # WDT2 control
        "ast2500": "0x1E78502C",  # WDT2 control
        "ast2600": "0x1E620064",  # FMC_WDT2
        "ast2700": "0x14C3740C",  # WDTA control
    }

    def _watchdog_registers(self):
        return self._WATCHDOG_REGISTERS[self.get_soc()]

    def _read_register(self, register):
        cmd_out = run_shell_cmd(f"{self._DEVMEM_CMD} {register}")
        return int(cmd_out, 16)

    def start_watchdog(self):
        """Start the watchdog."""
        cmd = self._WDTCLI_CMD + " start"
        run_shell_cmd(cmd)

    def stop_watchdog(self):
        """Stop the watchdog."""
        cmd = self._WDTCLI_CMD + " stop"
        run_shell_cmd(cmd)

    def kick_watchdog(self):
        """Kick the watchdog."""
        cmd = self._WDTCLI_CMD + " kick"
        run_shell_cmd(cmd)

    def _read_ctrl_register(self):
        """Read watchdog control register."""
        ctrl_reg, _ = self._watchdog_registers()
        return self._read_register(ctrl_reg)

    def _read_status_register(self):
        """Read watchdog status register."""
        _, status_reg = self._watchdog_registers()
        return self._read_register(status_reg)

    def get_soc(self):
        """Return the Aspeed SoC (e.g. "ast2500") from the device tree.

        Raises UnknownSocError if it cannot be determined.
        """
        try:
            with open(self._DT_COMPATIBLE_PATH, "rb") as f:
                entries = f.read().decode("ascii", "ignore").rstrip("\0").split("\0")
        except OSError as e:
            raise UnknownSocError(f"cannot read {self._DT_COMPATIBLE_PATH}: {e}") from e
        for entry in entries:
            vendor, _, model = entry.partition(",")
            if vendor == "aspeed" and model in self._WATCHDOG_REGISTERS:
                return model
        raise UnknownSocError(f"no supported Aspeed SoC in compatible: {entries}")

    def _watchdog_is_ticking(self):
        """Check if watchdog timer is ticking."""
        # NOTE: please make sure the watchdog is not touched by other
        # apps when this function is called; otherwise, the function
        # may return incorrect result because the watchdog might be
        # restarted by other apps.
        count_1 = self._read_status_register()

        # Sleep for a short while so the watchdog controller gets chance
        # to refresh the status register.
        sleep(0.01)  # 10 milliseconds

        count_2 = self._read_status_register()

        return count_2 < count_1

    def watchdog_is_running(self, check_counter=False):
        """Check if watchdog is running."""
        # Check if the last 2 bits of control register are set.
        #  - bit 0: set to enable watchdog.
        #  - bit 1: set to reboot system when timer expires.
        reg_val = self._read_ctrl_register()
        if reg_val & 3 != 3:
            return False

        if check_counter and not self._watchdog_is_ticking():
            return False

        return True

    def second_watchdog_is_running(self):
        """Check if second watchdog is running."""
        reg = self._SECOND_WDT_ENABLE_REG[self.get_soc()]
        return (self._read_register(reg) & 1) == 1
