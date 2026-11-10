#!/usr/bin/python3
#
# Copyright (c) Nexthop Systems Inc.
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
# Access the CPU FPGA registers through /dev/fpga_reg, and the FPGA
# configuration flash through the AXI Quad SPI master inside the FPGA.
# FPGA_GRAB must be asserted for the register bridge to respond; the
# bios_update.sh and fpga_update.sh wrappers take care of that.

import argparse
import logging
import time
from dataclasses import dataclass, field
from enum import Enum

logging.basicConfig(
    level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s"
)
logger = logging.getLogger(__name__)

REG_DEV = "/dev/fpga_reg"
REG_SIZE = 4
REG_ENDIAN = "little"


class FpgaRegFile:
    """Register-level access to the FPGA through the nh_fpga_reg device."""

    _instance = None

    def __new__(cls):
        if cls._instance is None:
            instance = super().__new__(cls)
            instance._file = open(REG_DEV, "r+b", buffering=0)
            cls._instance = instance
        return cls._instance

    def __del__(self):
        self._file.close()

    def read_reg(self, reg: int) -> int:
        self._file.seek(reg)
        value = int.from_bytes(self._file.read(REG_SIZE), REG_ENDIAN)
        logger.debug(f"READ  0x{reg:08X}: 0x{value:08X}")
        return value

    def write_reg(self, reg: int, value: int) -> None:
        self._file.seek(reg)
        self._file.write(value.to_bytes(REG_SIZE, REG_ENDIAN))
        logger.debug(f"WRITE 0x{reg:08X}: 0x{value:08X}")


class FpgaReg:
    """One FPGA register. twice_read works around a stale first read on some registers."""

    def __init__(self, addr: int, twice_read: bool = False):
        self._addr = addr
        self._regs = FpgaRegFile()
        self._twice_read = twice_read

    def read(self) -> int:
        if self._twice_read:
            self._regs.read_reg(self._addr)
        return self._regs.read_reg(self._addr)

    def write(self, value: int) -> None:
        self._regs.write_reg(self._addr, value)

    def write_bits(self, bits: dict[int, int]) -> None:
        mask = 0
        value = 0
        for bit, bit_val in bits.items():
            mask |= 1 << bit
            if bit_val:
                value |= 1 << bit
        self.write((self.read() & ~mask) | (value & mask))


# SPI-NOR opcodes and device configuration.


class FourByteAddrMode(Enum):
    NONE = "none"  # 3-byte addressing
    EN4B = "en4b"  # send EN4B at init, keep 3-byte opcodes
    WREN_EN4B = "wren_en4b"  # WREN then EN4B (Micron/ST)
    NATIVE_4B = "native_4b"  # 4-byte opcodes, no mode switch


@dataclass
class SpiOpCodes:
    read: int = 0x03
    write: int = 0x02  # PAGE_PROGRAM
    write_enable: int = 0x06  # WREN
    write_disable: int = 0x04  # WRDI
    read_status: int = 0x05  # RDSR
    read_id: int = 0x9F  # RDID
    sector_erase: int | None = 0x20  # derived from sector_size in SpiDeviceConfig
    chip_erase: int | None = 0xC7
    enter_4byte_mode: int | None = 0xB7  # EN4B
    exit_4byte_mode: int | None = 0xE9  # EX4B


@dataclass
class SpiOpCodes4B(SpiOpCodes):
    read: int = 0x13
    write: int = 0x12
    sector_erase: int | None = 0xDC


# Sector size in bytes to erase opcode, 3-byte and 4-byte address variants.
_SECTOR_ERASE_OPCODES: dict[int, int] = {
    4 * 1024: 0x20,
    32 * 1024: 0x52,
    64 * 1024: 0xD8,
}
_SECTOR_ERASE_OPCODES_4B: dict[int, int] = {
    4 * 1024: 0x21,
    32 * 1024: 0x5C,
    64 * 1024: 0xDC,
    128 * 1024: 0xDC,
}


@dataclass
class SpiDeviceConfig:
    device_name: str = "Unknown"
    full_size: int | None = None
    sector_size: int | None = None
    page_size: int = 256
    address_width: int = 3
    word_size: int = 1
    chip_select: int = 0
    opcodes: SpiOpCodes = field(default_factory=SpiOpCodes)
    four_byte_addr_mode: FourByteAddrMode = FourByteAddrMode.NONE
    clock_phase_read: bool = False
    clock_phase_write: bool = False
    clock_polarity: bool = False
    needs_write_enable: bool = True
    id_length: int = 9

    def __post_init__(self) -> None:
        # Derive the 4-byte addressing mode and the sector erase opcode.
        if self.address_width > 3 and self.four_byte_addr_mode is FourByteAddrMode.NONE:
            if isinstance(self.opcodes, SpiOpCodes4B):
                self.four_byte_addr_mode = FourByteAddrMode.NATIVE_4B
            else:
                self.four_byte_addr_mode = FourByteAddrMode.EN4B
        if (
            self.four_byte_addr_mode is FourByteAddrMode.NATIVE_4B
            and type(self.opcodes) is SpiOpCodes
        ):
            self.opcodes = SpiOpCodes4B()
        if self.sector_size is None:
            self.opcodes.sector_erase = None
        else:
            lookup = (
                _SECTOR_ERASE_OPCODES_4B
                if isinstance(self.opcodes, SpiOpCodes4B)
                else _SECTOR_ERASE_OPCODES
            )
            opcode = lookup.get(self.sector_size)
            if opcode is not None:
                self.opcodes.sector_erase = opcode


class _ProgressReporter:
    """Log bulk-transfer progress at most every _INTERVAL_SECONDS."""

    _INTERVAL_SECONDS = 15.0

    def __init__(self, label: str, total_bytes: int):
        self._label = label
        self._total = total_bytes
        self._start = time.monotonic()
        self._next_report = self._start + self._INTERVAL_SECONDS

    def update(self, done_bytes: int) -> None:
        now = time.monotonic()
        if now < self._next_report or done_bytes <= 0:
            return
        self._next_report = now + self._INTERVAL_SECONDS
        elapsed = now - self._start
        rate = done_bytes / elapsed
        eta = (self._total - done_bytes) / rate if rate > 0 else 0
        logger.info(
            f"{self._label}: {done_bytes}/{self._total} bytes "
            f"({100 * done_bytes / self._total:.1f}%), "
            f"{rate / 1024:.1f} KiB/s, ETA {int(eta) // 60}m{int(eta) % 60:02d}s"
        )


class SpiFlashMaster:
    """SPI-NOR access through the AXI Quad SPI master in the FPGA."""

    _SOFT_RESET_KEY: int = 0x0000_000A
    _CS_ALL_DEASSERTED: int = 0xFFFF_FFFF
    _FIFO_DEPTH: int = 16

    # Control register bits.
    _CTRL_INIT: int = 0x000_01E6  # inhibit, manual CS, FIFO resets, master, enable
    _CTRL_RX_FIFO_RESET_BIT: int = 5
    _CTRL_TX_FIFO_RESET_BIT: int = 6
    _CTRL_INHIBIT_BIT: int = 8
    # Status register bits.
    _STATUS_RX_EMPTY: int = 0x1
    _STATUS_TX_EMPTY: int = 0x4

    _POLL_TIMEOUT_SECONDS: float = 1.0
    _POLL_INTERVAL_SECONDS: float = 0.0001
    _WRITE_COMPLETE_TIMEOUT_SECONDS: float = 5.0
    _CHIP_ERASE_TIMEOUT_SECONDS: float = 300.0

    def __init__(self, offset: int, device_config: SpiDeviceConfig):
        self.name = "SPI master"
        self.device_config = device_config
        self._init_direction: bool | None = None

        def reg(rel, twice=False):
            return FpgaReg(offset + rel, twice)

        self.software_reset = reg(0x040, True)
        self.control = reg(0x060, True)
        self.status = reg(0x064, True)
        self.transmit_fifo = reg(0x068)
        self.receive_fifo = reg(0x06C)
        self.slave_select = reg(0x070, True)
        self.tx_fifo_occupancy = reg(0x074, True)
        self.rx_fifo_occupancy = reg(0x078, True)

    def __del__(self):
        # Leave CS deasserted and the master disabled.
        self.deselect_slave()
        self.control.write(0)

    def reset(self) -> None:
        logger.debug(f"{self.name}: soft reset")
        self.software_reset.write(self._SOFT_RESET_KEY)
        self._init_direction = None

    def init_master(self, read_not_write: bool = True, force: bool = False) -> None:
        # Skip when already initialised for this direction, or when the clock
        # phase is the same either way.
        if not force and self._init_direction is not None:
            if (
                self._init_direction == read_not_write
                or self.device_config.clock_phase_read
                == self.device_config.clock_phase_write
            ):
                return

        self.reset()
        self.control.write(self._CTRL_INIT)
        self.control.write_bits(
            {self._CTRL_RX_FIFO_RESET_BIT: 0, self._CTRL_TX_FIFO_RESET_BIT: 0}
        )
        logger.debug(
            f"{self.name}: initialised: status=0x{self.status.read():08X} "
            f"control=0x{self.control.read():08X} "
            f"slave_select=0x{self.slave_select.read():08X}"
        )
        self._init_direction = read_not_write

    def select_slave(self) -> None:
        self.slave_select.write(
            self._CS_ALL_DEASSERTED & ~(1 << self.device_config.chip_select)
        )

    def deselect_slave(self) -> None:
        self.slave_select.write(self._CS_ALL_DEASSERTED)

    def poll_tx_empty(self, timeout_seconds: float | None = None) -> None:
        timeout_seconds = timeout_seconds or self._POLL_TIMEOUT_SECONDS
        deadline = time.monotonic() + timeout_seconds
        while not self.status.read() & self._STATUS_TX_EMPTY:
            if time.monotonic() > deadline:
                raise Exception(
                    f"{self.name}: TX FIFO not empty after {timeout_seconds}s "
                    f"(status=0x{self.status.read():08X}, control=0x{self.control.read():08X}, "
                    f"tx_occupancy={self.tx_fifo_occupancy.read()})"
                )
            time.sleep(self._POLL_INTERVAL_SECONDS)

    def transfer(self, tx_data: bytes) -> bytes:
        """Full-duplex transfer with CS held for the whole of tx_data."""
        logger.debug(
            f"{self.name}: transfer cs={self.device_config.chip_select} tx={tx_data[:16].hex()}"
        )
        rx_data = bytearray()
        offset = 0
        self.select_slave()
        while offset < len(tx_data):
            chunk_size = min(self._FIFO_DEPTH, len(tx_data) - offset)

            self.control.write_bits({self._CTRL_INHIBIT_BIT: 1})
            for i in range(chunk_size):
                self.transmit_fifo.write(tx_data[offset + i])
            self.control.write_bits({self._CTRL_INHIBIT_BIT: 0})
            self.poll_tx_empty()
            self.control.write_bits({self._CTRL_INHIBIT_BIT: 1})

            # Drain by occupancy: some FPGA revisions leave a stale leading
            # byte in the RX FIFO. The occupancy register reports count - 1.
            if self.status.read() & self._STATUS_RX_EMPTY:
                raise Exception(
                    f"{self.name}: RX FIFO empty after clocking {chunk_size} bytes"
                )
            avail = self.rx_fifo_occupancy.read() + 1
            if avail < chunk_size:
                raise Exception(
                    f"{self.name}: RX FIFO has {avail} bytes, expected at least {chunk_size}"
                )
            chunk_rx = [self.receive_fifo.read() & 0xFF for _ in range(avail)]
            rx_data.extend(chunk_rx[-chunk_size:])

            offset += chunk_size
        self.deselect_slave()
        return bytes(rx_data)

    def read_identification(self) -> bytes:
        self.init_master(read_not_write=True)
        tx = bytes([self.device_config.opcodes.read_id]) + bytes(
            self.device_config.id_length
        )
        return self.transfer(tx)[1:]

    def send_write_enable(self) -> None:
        self.transfer(bytes([self.device_config.opcodes.write_enable]))

    def send_write_disable(self) -> None:
        self.transfer(bytes([self.device_config.opcodes.write_disable]))

    def read_status_register(self) -> int:
        self.init_master(read_not_write=True)
        return self.transfer(bytes([self.device_config.opcodes.read_status, 0x00]))[1]

    def enter_4byte_mode(self, wren_first: bool = False) -> None:
        opcode = self.device_config.opcodes.enter_4byte_mode
        if opcode is None:
            raise Exception(f"{self.name}: device does not support EN4B")
        self.init_master(read_not_write=False)
        if wren_first:
            self.send_write_enable()
        self.transfer(bytes([opcode]))

    def wait_write_complete(self, timeout_seconds: float | None = None) -> None:
        timeout_seconds = timeout_seconds or self._WRITE_COMPLETE_TIMEOUT_SECONDS
        deadline = time.monotonic() + timeout_seconds
        while True:
            status = self.read_status_register()
            if not status & 0x01:  # WIP
                return
            if time.monotonic() > deadline:
                raise Exception(
                    f"{self.name}: write still in progress after {timeout_seconds}s (status=0x{status:02X})"
                )
            time.sleep(self._POLL_INTERVAL_SECONDS)

    def bulk_erase(self) -> None:
        opcode = self.device_config.opcodes.chip_erase
        if opcode is None:
            raise Exception(f"{self.name}: bulk erase not supported on this device")
        self.init_master(read_not_write=False)
        if self.device_config.needs_write_enable:
            self.send_write_enable()
        self.transfer(bytes([opcode]))
        self.wait_write_complete(timeout_seconds=self._CHIP_ERASE_TIMEOUT_SECONDS)

    def _address_bytes(self, address: int) -> bytes:
        return address.to_bytes(self.device_config.address_width, byteorder="big")

    def read_bytes(self, address: int, length: int) -> bytes:
        self.init_master(read_not_write=True)
        header = bytes([self.device_config.opcodes.read]) + self._address_bytes(address)
        return self.transfer(header + bytes(length))[len(header) :]

    def write_bytes(self, address: int, data: bytes) -> None:
        """Page program, split at page boundaries."""
        self.init_master(read_not_write=False)
        page_size = self.device_config.page_size
        offset = 0
        while offset < len(data):
            chunk_addr = address + offset
            chunk_size = min(page_size - chunk_addr % page_size, len(data) - offset)
            chunk = data[offset : offset + chunk_size]
            if self.device_config.needs_write_enable:
                self.send_write_enable()
            self.transfer(
                bytes([self.device_config.opcodes.write])
                + self._address_bytes(chunk_addr)
                + chunk
            )
            self.wait_write_complete()
            offset += chunk_size


class CpuFpgaConfigFlash:
    """The CPU FPGA's configuration flash, behind the SPI master at 0x50600."""

    _SPI_MASTER_OFFSET = 0x50600
    # JEDEC id 20 BA 19: Micron N25Q256 / MT25QL256, 32 MiB, 64 KiB sectors.
    _DEVICE = SpiDeviceConfig(
        device_name="Micron N25Q256",
        full_size=32 * 1024 * 1024,
        sector_size=64 * 1024,
        address_width=4,
        four_byte_addr_mode=FourByteAddrMode.NATIVE_4B,
    )

    def __init__(self):
        self.device = self._DEVICE
        self.spi = SpiFlashMaster(self._SPI_MASTER_OFFSET, self.device)

        jedec = self.jedec_id
        capacity_code = jedec["mem_type"] << 8 | jedec["capacity"]
        size_str = (
            f"{(1 << jedec['capacity']) // (1024 * 1024)} MiB"
            if 20 <= jedec["capacity"] <= 32
            else "unknown size"
        )
        logger.info(
            f"Flash: JEDEC ID manufacturer=0x{jedec['manufacturer_id']:02X} "
            f"device=0x{capacity_code:04X} ({size_str}), configured as {self.device.device_name}"
        )
        if jedec["manufacturer_id"] in (0x00, 0xFF):
            logger.warning(
                "Flash: JEDEC ID looks invalid (bus floating or device not responding)"
            )

        mode = self.device.four_byte_addr_mode
        if mode is FourByteAddrMode.EN4B:
            self.spi.enter_4byte_mode()
        elif mode is FourByteAddrMode.WREN_EN4B:
            self.spi.enter_4byte_mode(wren_first=True)

    @property
    def jedec_id(self) -> dict[str, int]:
        raw_id = self.spi.read_identification()
        # Skip 0x7F continuation bytes; then manufacturer, memory type, capacity.
        continuation = 0
        manufacturer_id = 0
        index = 0
        for index, byte_value in enumerate(raw_id):
            if byte_value != 0x7F:
                manufacturer_id = byte_value
                break
            continuation += 1
        device_id = raw_id[index + 1 :]
        return {
            "manufacturer_id": manufacturer_id,
            "continuation_bytes": continuation,
            "mem_type": device_id[0] if len(device_id) > 0 else 0,
            "capacity": device_id[1] if len(device_id) > 1 else 0,
        }

    def read_flash(self, length: int | None = None) -> bytes:
        if length is None:
            length = self.device.full_size
        page_size = self.device.page_size
        num_pages = (length + page_size - 1) // page_size
        progress = _ProgressReporter("Flash read", num_pages * page_size)
        data = bytearray()
        for page in range(num_pages):
            data += self.spi.read_bytes(page * page_size, page_size)
            progress.update(len(data))
        return bytes(data[:length])

    def erase_flash(self) -> None:
        logger.info("Flash: erasing entire device")
        self.spi.bulk_erase()

    def write_flash(self, data: bytes) -> None:
        if len(data) > self.device.full_size:
            raise ValueError(
                f"Flash: data size {len(data)} bytes exceeds device size {self.device.full_size} bytes"
            )
        logger.info(f"Flash: writing {len(data)} bytes")
        page_size = self.device.page_size
        progress = _ProgressReporter("Flash write", len(data))
        for address in range(0, len(data), page_size):
            self.spi.write_bytes(address, data[address : address + page_size])
            progress.update(min(address + page_size, len(data)))

    def verify_flash(self, file_path: str) -> None:
        with open(file_path, "rb") as f:
            expected = f.read()
        if len(expected) > self.device.full_size:
            raise ValueError(
                f"Flash: file size {len(expected)} bytes exceeds device size {self.device.full_size}"
            )
        logger.info(f"Flash: reading back {len(expected)} bytes for verification")
        actual = self.read_flash(length=len(expected))
        if actual != expected:
            for offset, (a, e) in enumerate(zip(actual, expected)):
                if a != e:
                    raise ValueError(
                        f"Flash: verify failed at offset 0x{offset:06X}: read 0x{a:02X}, expected 0x{e:02X}"
                    )
        logger.info("Flash: verify passed")

    def program_device(self, file_path: str) -> None:
        with open(file_path, "rb") as f:
            image = f.read()
        if len(image) > self.device.full_size:
            raise ValueError(
                f"Flash: file size {len(image)} bytes is too large for device size {self.device.full_size}"
            )
        self.erase_flash()
        self.write_flash(image)
        self.verify_flash(file_path)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="CPU FPGA register and configuration flash utility"
    )
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--fpga", action="store_true", help="access FPGA registers")
    target.add_argument(
        "--flash", action="store_true", help="access the FPGA configuration flash"
    )

    op = parser.add_mutually_exclusive_group(required=False)
    op.add_argument(
        "-r", metavar="ADDRESS", type=lambda x: int(x, 0), help="read ADDRESS"
    )
    op.add_argument(
        "-w", nargs=2, metavar=("ADDRESS", "VALUE"), help="write VALUE to ADDRESS"
    )
    op.add_argument("--jedec", action="store_true", help="print the flash JEDEC ID")
    op.add_argument("--program", metavar="FILE", help="erase, program and verify FILE")
    op.add_argument("--verify", metavar="FILE", help="verify the flash against FILE")
    op.add_argument("--dump", metavar="FILE", help="dump the flash to FILE")
    parser.add_argument(
        "--length",
        type=lambda x: int(x, 0),
        help="bytes to read with --dump (default: whole flash)",
    )
    parser.add_argument(
        "--loglevel",
        choices=["trace", "debug", "info"],
        default="info",
        help="trace is an alias of debug",
    )
    args = parser.parse_args()

    logger.setLevel(
        logging.DEBUG if args.loglevel in ("trace", "debug") else logging.INFO
    )

    if args.fpga:
        regs = FpgaRegFile()
        if args.r is not None:
            print(f"0x{args.r:08X}: 0x{regs.read_reg(args.r):08X}")
        elif args.w is not None:
            address = int(args.w[0], 0)
            value = int(args.w[1], 0)
            regs.write_reg(address, value)
            print(f"Wrote 0x{value:08X} to 0x{address:08X}")
            print(f"Read back 0x{regs.read_reg(address):08X} from 0x{address:08X}")

    if args.flash:
        flash = CpuFpgaConfigFlash()
        if args.jedec:
            print(flash.jedec_id)
        elif args.r is not None:
            print(f"0x{args.r:08X}: 0x{flash.spi.read_bytes(args.r, 1)[0]:02X}")
        elif args.w is not None:
            address = int(args.w[0], 0)
            value = int(args.w[1], 0)
            num_bytes = max(1, (value.bit_length() + 7) // 8)
            flash.spi.write_bytes(address, value.to_bytes(num_bytes, byteorder="big"))
            print(f"Wrote 0x{value:0{num_bytes * 2}X} to 0x{address:08X}")
            read_back = int.from_bytes(
                flash.spi.read_bytes(address, num_bytes), byteorder="big"
            )
            print(f"Read back 0x{read_back:0{num_bytes * 2}X} from 0x{address:08X}")
        elif args.program is not None:
            flash.program_device(args.program)
        elif args.verify is not None:
            flash.verify_flash(args.verify)
        elif args.dump is not None:
            data = flash.read_flash(length=args.length)
            with open(args.dump, "wb") as f:
                f.write(data)
            print(f"Dumped flash contents to {args.dump}")


if __name__ == "__main__":
    main()
