#!/usr/bin/env python
#
# Copyright 2015-present Facebook. All Rights Reserved.
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

import asyncio
import datetime
import functools
import json
import logging
import mmap
import os
import os.path
import re
import subprocess
import threading
import time
import typing as t
from shlex import quote
from uuid import getnode as get_mac

import kv
import psutil
import rest_mmc
import rest_pal_legacy
from boot_source import SocModelId, get_soc_model, is_boot_from_secondary
from node import node
from vboot import get_vboot_status

PROC_MTD_PATH = "/proc/mtd"
CPU_PCT = psutil.cpu_times_percent()  # type: psutil._pslinux.scputimes


def update_cpu_pct_counter():
    global CPU_PCT
    while True:
        CPU_PCT = psutil.cpu_times_percent(interval=60)


percent_updater = threading.Thread(target=update_cpu_pct_counter)
percent_updater.daemon = True
percent_updater.start()

logger = logging.getLogger("restapi")


# Read all contents of file path specified
def read_file_contents(path):
    try:
        with open(path, "r") as proc_file:
            content = proc_file.readlines()
    except IOError:
        content = None

    return content


def cache_uboot_version():
    # Get U-boot Version
    logger.info("Caching uboot version")
    uboot_version = ""
    uboot_ver_regex = r"^U-Boot\W+(?P<uboot_ver>20\d{2}\.\d{2})\W+.*$"
    uboot_ver_re = re.compile(uboot_ver_regex)
    mtd_meta = getMTD("meta")
    if mtd_meta is None:
        stdout = subprocess.check_output(["/usr/bin/strings", "/dev/mtd0"])
        for line in stdout.splitlines():
            matched = uboot_ver_re.fullmatch(line.decode().strip())
            if matched:
                uboot_version = matched.group("uboot_ver")
                break
    else:
        try:
            mtd_dev = "/dev/" + mtd_meta
            with open(mtd_dev, "r") as f:
                raw_data = f.readline()
            uboot_version = json.loads(raw_data)["version_infos"]["uboot_ver"]
        except Exception:
            uboot_version = ""
    UBOOT_VER_KV_KEY = "u-boot-ver"
    kv.kv_set(UBOOT_VER_KV_KEY, uboot_version)
    logger.info("Cached uboot version to kv-store")
    logger.info("cached uboot version: %s" % (uboot_version))


def get_wdt_counter() -> int:
    addr = (
        0x1E785010 if get_soc_model() != SocModelId.SOC_MODEL_ASPEED_G7 else 0x14C37410
    )
    with open("/dev/mem", "rb") as f:
        with mmap.mmap(
            f.fileno(),
            (addr // mmap.PAGESIZE + 1) * mmap.PAGESIZE,
            mmap.MAP_SHARED,
            mmap.PROT_READ,
        ) as reg_map:
            # The mmap conversion broke the former `devmem(addr) & 0xFF00`
            # behavior by reading bits 7:0. Read bits 15:8 at addr + 1 instead;
            # this was easy to miss because callers only check zero vs nonzero.
            return reg_map[addr + 1]


def SPIVendorID2Name(manufacturer_id):
    # Define Manufacturer ID
    MFID_WINBOND = "EF"  # Winbond
    MFID_MICRON = "20"  # Micron
    MFID_MACRONIX = "C2"  # Macronix

    vendor_name = {
        MFID_WINBOND: "Winbond",
        MFID_MICRON: "Micron",
        MFID_MACRONIX: "Macronix",
    }

    if manufacturer_id in vendor_name:
        return vendor_name[manufacturer_id]
    else:
        return "Unknown"


async def getSPIVendorLegacy(spi_id):
    vendor_dat_path = "/tmp/spi0.%d_vendor.dat" % (spi_id)
    manufacturer_id = ""
    with open(vendor_dat_path, "r") as vendor_dat:
        manufacturer_id = vendor_dat.read(2)
    return SPIVendorID2Name(manufacturer_id)


def getMTD(name):
    mtd_name = quote(name)
    with open(PROC_MTD_PATH) as f:
        lines = f.readlines()
        for line in lines:
            if mtd_name in line:
                return line.split(":")[0]
    return None


def getSPIVendorNew(spi_id, mtd_name):
    mtd = getMTD("%s%d" % (mtd_name, spi_id))
    if mtd is None:
        return "Unknown"
    debugfs_path = "/sys/kernel/debug/mtd/" + mtd + "/partid"
    try:
        with open(debugfs_path) as f:
            data = f.read().strip()
            # Example spi-nor:ef4019
            mfg_id = data.split(":")[-1][0:2].upper()
            return SPIVendorID2Name(mfg_id)
    except Exception:
        pass
    return "Unknown"


async def getSPIVendor(spi_id):
    # legacy way
    if os.path.isfile("/tmp/spi0.%d_vendor.dat" % (spi_id)):
        return await getSPIVendorLegacy(spi_id)

    ret = getSPIVendorNew(spi_id, "flash")
    if ret == "Unknown":
        ret = getSPIVendorNew(spi_id, "spi0.")

    return ret


@functools.lru_cache(maxsize=1)
def read_proc_mtd() -> t.List[str]:
    mtd_list = []
    with open(PROC_MTD_PATH) as f:
        # e.g. 'mtd5: 02000000 00010000 "flash0"' -> dev="mtd5", size="02000000", erasesize="00010000", name="flash0"   # noqa B950
        RE_MTD_INFO = re.compile(
            r"""^(?P<dev>[^:]+): \s+ (?P<size>[0-9a-f]+) \s+ (?P<erasesize>[0-9a-f]+) \s+ "(?P<name>[^"]+)"$""",  # noqa B950
            re.MULTILINE | re.VERBOSE,
        )
        for m in RE_MTD_INFO.finditer(f.read()):
            mtd_list.append(m.group("name"))  # e.g. "flash0"
    return mtd_list


cache_uboot_version()


class bmcNode(node):
    # Reads from TPM device files (e.g. /sys/class/tpm/tpm0/device/caps)
    # can hang on unhealthy systems. Cache version values here; the
    # background thread started by start_tpm_ver_updater() fills them.
    _TPM_VER_INFO = ("NA", "NA")  # (tpm_fw_version, tpm_tcg_version)
    _TPM_VER_UPDATER = None

    def __init__(self, info=None, actions=None):
        if info is None:
            self.info = {}
        else:
            self.info = info
        if actions is None:
            self.actions = []
        else:
            self.actions = actions

    @classmethod
    def start_tpm_ver_updater(cls) -> None:
        # Start the updater once; later calls do nothing.
        if cls._TPM_VER_UPDATER is None:
            cls._TPM_VER_UPDATER = threading.Thread(
                target=cls._update_tpm_ver_info, daemon=True
            )
            cls._TPM_VER_UPDATER.start()

    @classmethod
    def _update_tpm_ver_info(cls) -> None:
        # Retry until all TPM version info values are filled (the TPM may
        # not be ready yet), then exit.
        while "NA" in cls._TPM_VER_INFO:
            if os.path.exists("/sys/class/tpm/tpm0"):
                cls._TPM_VER_INFO = (cls.getTpmFwVer(), cls.getTpmTcgVer())
            if "NA" in cls._TPM_VER_INFO:
                time.sleep(30)

    async def getUbootVer(self):
        UBOOT_VER_KV_KEY = "u-boot-ver"
        try:
            uboot_version = kv.kv_get(UBOOT_VER_KV_KEY)
        except kv.KeyOperationFailure:
            uboot_version = None
        return uboot_version

    @staticmethod
    def getTpmTcgVer():
        out_str = "NA"
        tpm1_caps = "/sys/class/tpm/tpm0/device/caps"
        if os.path.isfile(tpm1_caps):
            with open(tpm1_caps) as f:
                for line in f:
                    if "TCG version:" in line:
                        out_str = line.strip("TCG version: ").strip("\n")  # noqa B005
        elif os.path.isfile("/usr/bin/tpm2_getcap"):
            cmd_list = []
            cmd_list.append(
                "/usr/bin/tpm2_getcap -c properties-fixed 2>/dev/null | grep -A2 TPM_PT_FAMILY_INDICATOR"  # noqa B950
            )
            cmd_list.append(
                "/usr/bin/tpm2_getcap properties-fixed 2>/dev/null | grep -A2 TPM2_PT_FAMILY_INDICATOR"  # noqa B950
            )
            for cmd in cmd_list:
                try:
                    stdout = subprocess.check_output(cmd, shell=True)  # noqa: P204
                    out_str = stdout.splitlines()[2].rstrip().decode().split('"')[1]
                    break
                except Exception:
                    pass
        return out_str

    @staticmethod
    def getTpmFwVer():
        out_str = "NA"
        tpm1_caps = "/sys/class/tpm/tpm0/device/caps"
        if os.path.isfile(tpm1_caps):
            with open(tpm1_caps) as f:
                for line in f:
                    if "Firmware version:" in line:
                        out_str = line.strip("Firmware version: ").strip(  # noqa B005
                            "\n"
                        )
        elif os.path.isfile("/usr/bin/tpm2_getcap"):
            cmd_list = []
            cmd_list.append(
                "/usr/bin/tpm2_getcap -c properties-fixed 2>/dev/null | grep TPM_PT_FIRMWARE_VERSION_1"  # noqa B950
            )
            cmd_list.append(
                "/usr/bin/tpm2_getcap properties-fixed 2>/dev/null | grep -A1 TPM2_PT_FIRMWARE_VERSION_1 | grep raw"  # noqa B950
            )
            for cmd in cmd_list:
                try:
                    stdout = subprocess.check_output(cmd, shell=True)  # noqa: P204
                    value = int(stdout.rstrip().decode().split(":")[1], 16)
                    out_str = "%d.%d" % (value >> 16, value & 0xFFFF)
                    break
                except Exception:
                    pass
        return out_str

    def getMemInfo(self):
        desired_keys = (
            "MemTotal",
            "MemAvailable",
            "MemFree",
            "Shmem",
            "Buffers",
            "Cached",
        )
        meminfo = {}
        with open("/proc/meminfo", "r") as mi:
            for line in mi:
                try:
                    key, value = line.split(":", 1)
                    key = key.strip()
                    if key not in desired_keys:
                        continue
                    memval, _ = value.strip().split(" ", 1)
                    meminfo[key] = int(memval)
                except ValueError:
                    pass
        return meminfo

    async def getInformation(self, param=None):
        loop = asyncio.get_event_loop()

        # Get Platform Name
        name = rest_pal_legacy.pal_get_platform_name()

        # Get MAC Address
        eth_intf = rest_pal_legacy.pal_get_eth_intf_name()
        mac_path = "/sys/class/net/%s/address" % (eth_intf)
        if os.path.isfile(mac_path):
            mac = open(mac_path).read()
            mac_addr = mac[0:17].upper()
        else:
            mac = get_mac()
            mac_addr = ":".join(("%012X" % mac)[i : i + 2] for i in range(0, 12, 2))

        # Get BMC Reset Reason
        wdt_counter = get_wdt_counter()
        if wdt_counter:
            por_flag = 0
        else:
            por_flag = 1

        if por_flag:
            reset_reason = "Power ON Reset"
        else:
            reset_reason = "User Initiated Reset or WDT Reset"

        # Use another method, ala /proc, but keep the old one for backwards
        # compat.
        # See http://man7.org/linux/man-pages/man5/proc.5.html for details
        # on full contents of proc endpoints.
        uptime_seconds = read_file_contents("/proc/uptime")[0].split()[0]

        # Pull load average directory from proc instead of processing it from
        # the contents of uptime command output later.
        load_avg = read_file_contents("/proc/loadavg")[0].split()[0:3]

        uptime_delta = datetime.timedelta(seconds=int(float(uptime_seconds)))
        # '07:44:38 up 144 days, 13:12,  load average: 5.03, 5.22, 5.42'
        # Construct uptime string for backaward compatibility reasons
        uptime = "{now} up {uptime}, load average: {load1}, {load5}, {load15}".format(
            now=datetime.datetime.now().strftime("%H:%M:%S"),
            uptime=str(uptime_delta),
            load1=load_avg[0],
            load5=load_avg[1],
            load15=load_avg[2],
        )
        cpu_usage = (
            "CPU:  {usr_pct:.0f}% usr  {sys_pct:.0f}% sys   {nice_pct:.0f}% nic"
            "   {idle_pct:.0f}% idle   {iowait_pct:.0f}% io   {irq_pct:.0f}% irq"
            "   {sirq_pct:.0f}% sirq"
        ).format(
            usr_pct=CPU_PCT.user,
            sys_pct=CPU_PCT.system,
            nice_pct=CPU_PCT.nice,
            idle_pct=CPU_PCT.idle,
            iowait_pct=CPU_PCT.iowait,
            irq_pct=CPU_PCT.irq,
            sirq_pct=CPU_PCT.softirq,
        )
        cpu = {
            "usr": CPU_PCT.user,
            "sys": CPU_PCT.system,
            "nic": CPU_PCT.nice,
            "idle": CPU_PCT.idle,
            "io": CPU_PCT.iowait,
            "irq": CPU_PCT.irq,
            "sirq": CPU_PCT.softirq,
        }
        memory_info = self.getMemInfo()

        # Mem: 175404K used, 260324K free, 21436K shrd, 0K buff, 112420K cached
        mem_usage = (
            "Mem: {used_mem}K used, {free_mem}K free, {shared_mem}K shrd,"
            " {buffer_mem}K buff, {cached_mem}K cached"
        ).format(
            used_mem=memory_info["MemTotal"] - memory_info["MemFree"],
            free_mem=memory_info["MemFree"],
            shared_mem=memory_info["Shmem"],
            buffer_mem=memory_info["Buffers"],
            cached_mem=memory_info["Cached"],
        )

        # Get OpenBMC version
        obc_version = ""
        with open("/etc/issue") as f:
            raw_version_str = f.read()
        # OpenBMC Version
        ver = re.search(r"-[v|V]([\w\d._-]*)\s", raw_version_str)
        if ver:
            obc_version = ver.group(1)

        # U-Boot version
        uboot_version = await self.getUbootVer()
        if uboot_version is None:
            uboot_version = "NA"

        # Get kernel release and kernel version
        uname = os.uname()
        kernel_release = uname.release
        kernel_version = uname.version

        # Get TPM version
        tpm_fw_version, tpm_tcg_version = self._TPM_VER_INFO

        spi0_vendor = await getSPIVendor(0)
        spi1_vendor = await getSPIVendor(1)

        # ASD status - check if ASD daemon/asd-test is currently running
        asd_status = await loop.run_in_executor(None, self._check_asd_status)

        boot_from_secondary = is_boot_from_secondary()

        vboot_info = await get_vboot_status()

        used_fd_count = read_file_contents("/proc/sys/fs/file-nr")[0].split()[0]

        info = {
            "Description": name + " BMC",
            "MAC Addr": mac_addr,
            "Reset Reason": reset_reason,
            # Upper case Uptime is for legacy
            # API support
            "Uptime": uptime,
            # Lower case Uptime is for simpler
            # more pass-through proxy
            "uptime": uptime_seconds,
            "Memory Usage": mem_usage,
            "memory": memory_info,
            "CPU Usage": cpu_usage,
            "cpu": cpu,
            "OpenBMC Version": obc_version,
            "u-boot version": uboot_version,
            "kernel version": kernel_release + " " + kernel_version,
            "TPM TCG version": tpm_tcg_version,
            "TPM FW version": tpm_fw_version,
            "SPI0 Vendor": spi0_vendor,
            "SPI1 Vendor": spi1_vendor,
            "At-Scale-Debug Running": asd_status,
            "Secondary Boot Triggered": boot_from_secondary,
            "vboot": vboot_info,
            "load-1": load_avg[0],
            "load-5": load_avg[1],
            "load-15": load_avg[2],
            "open-fds": used_fd_count,
            "MTD Parts": read_proc_mtd(),
            "mmc-info": await rest_mmc.get_mmc_info(),
        }

        return info

    async def doAction(self, data, param=None):
        if not t.TYPE_CHECKING:
            os.spawnvpe("sleep 5; /sbin/reboot", shell=True)
        return {"result": "success"}

    def _check_asd_status(self):
        # ASD status - check if ASD daemon/asd-test is currently running
        running_cmdlines = subprocess.run(
            "cat /proc/[0-9]*/cmdline",
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            shell=True,
        ).stdout

        return b"asd" in running_cmdlines or b"yaapd" in running_cmdlines


# Read the TPM version in the background from startup, before any request.
bmcNode.start_tpm_ver_updater()


def get_node_bmc():
    actions = ["reboot"]
    return bmcNode(actions=actions)
