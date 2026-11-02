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
import json
import mmap
import os
import struct

from measure_func import (
    measure_keystore,
    measure_os,
    measure_rcv_uboot,
    measure_spl,
    measure_uboot,
    measure_vbs,
    Pcr,
)
from vboot_common import get_soc_model


class MalformedTPMEventLog(Exception):
    pass


class InvalidTPMEventLog(Exception):
    pass


EVENT_LOG_SIZE = 2 * 1024
EVENT_LOG_LOCATION = {
    # TPM event log SRAM mapping: (addr , size, log_offset, log_size)
    "SOC_MODEL_ASPEED_G5": (0x1E728000, 4 * 1024, 0x800, EVENT_LOG_SIZE),
    "SOC_MODEL_ASPEED_G6": (0x10015000, 4 * 1024, 0x000, EVENT_LOG_SIZE),
}

EVENT_LOG_MAX_LEN = EVENT_LOG_SIZE - 8
EVENT_LOG_FMT_VER = 1
EVENT_LOG_MAGIC_CODE = 0xFBBE
# The log start with uint32_t length of data
EVENT_LOG_START_FMT = "=L"

# Represent the TPM event log record preamble as following c strcutre
# struct tpm_event_log_record {
# 	uint16_t measurement; /* enumeration of measurements */
# 	uint8_t pcrid; /* enumeration of ast_tpm_pcrs */
# 	uint8_t algo; /* enumeration of tpm2_algorithms */
# 	uint32_t index;
# 	uint8_t digest[0]; /* hash value, variable length depedents on algo */
# };
EVENT_LOG_RECORD_FMT = "=HBBL"

# Represent the log end mark as following c structure
# union tpm_event_log_end_mark {
# 	uint32_t endmark;
# 	struct {
# 		uint16_t magic;
# 		uint16_t fmtver;
# 	};
# };
EVENT_LOG_ENDMARK_FMT = "=HH"

measure_name = {
    1: "spl",
    2: "key-store",
    3: "u-boot",
    4: "rec-u-boot",
    5: "u-boot-env",
    6: "vbs",
    7: "os:kernel",
    8: "os:rootfs",
    9: "os:dtb",
    10: "recovery-os:kernel",
    11: "recovery-os:rootfs",
    12: "recovery-os:dtb",
}

digest_algo_info = {
    0x0B: ("sha256", 256 // 8),
    0x0C: ("sha384", 384 // 8),
    0x0D: ("sha512", 512 // 8),
}


def get_location_size(soc_model):
    SRAM_OFFSET, _, LOG_OFFSET, log_data_size = get_event_log_location(soc_model)
    return (SRAM_OFFSET + LOG_OFFSET, log_data_size)


def get_event_log_location(soc_model=None):
    soc_model = get_soc_model() if soc_model is None else soc_model
    if soc_model not in EVENT_LOG_LOCATION:
        raise RuntimeError("TPM event log is not supported on {}".format(soc_model))
    return EVENT_LOG_LOCATION[soc_model]


def read_log_data_from_sram():
    memfn = None
    try:
        (
            SRAM_OFFSET,
            SRAM_SIZE,
            LOG_OFFSET,
            log_data_size,
        ) = get_event_log_location()
        memfn = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
        with mmap.mmap(
            memfn,
            SRAM_SIZE,
            mmap.MAP_SHARED,
            mmap.PROT_READ,
            offset=SRAM_OFFSET,
        ) as sram:
            sram.seek(LOG_OFFSET)
            return sram.read(log_data_size)
    finally:
        if memfn is not None:
            os.close(memfn)


def read_log_data_from_file(fname):
    with open(fname, "br") as f:
        return f.read()


def print_tpm_event_log_debug_suggestion():
    tpm_log_loc_size = {}
    try:
        soc_model = get_soc_model()
        tpm_log_loc_size[soc_model] = get_location_size(soc_model)
    except Exception:
        print("cannot detect BMC SOC model automatically")
        for soc_model in EVENT_LOG_LOCATION:
            tpm_log_loc_size[soc_model] = get_location_size(soc_model)

    print("run command to dump the raw event log to debug")
    for soc_model in tpm_log_loc_size:
        log_loc, log_size = tpm_log_loc_size[soc_model]
        print(
            f"""
    {soc_model}:
    $> phymemdump {log_loc:#08x} {log_size:#x} -b | hexdump -e '4/4 "%08x " "\\n"'
    or
    $> phymemdump {log_loc:#08x} {log_size:#x} -b > /tmp/event-log.bin
    and download event-log.bin
            """
        )


class TPMEventRecord(object):
    def __init__(
        self,
        mid: int,
        measure: str,
        pcrid: int,
        algo: str,
        index: int,
        digest: bytearray,
    ):
        self.mid = mid
        self.measure = measure
        self.pcrid = pcrid
        self.algo = algo
        self.index = index
        self.digest = digest


class TPMEventLog(object):
    def __init__(self, flash0_meta, flash1_meta, eventfile=None):
        self.flash0_meta = flash0_meta
        self.flash1_meta = flash1_meta
        self.eventfile = eventfile
        self._os_measure_cache = None
        self._rec_os_measure_cache = None
        self.tpm_events = self._load_tpm_events()

    def _load_tpm_events(self) -> "list[TPMEventRecord]":
        tpm_events = []
        if self.eventfile:
            log_data = read_log_data_from_file(self.eventfile)
        else:
            log_data = read_log_data_from_sram()
        # sanity check len
        log_len = struct.unpack_from(EVENT_LOG_START_FMT, log_data, 0)[0]
        if log_len > EVENT_LOG_MAX_LEN:
            raise MalformedTPMEventLog(
                f"Malformed TPM Log, len {log_len} > {self.VENT_LOG_MAX_LEN}"
            )
        # sanity check end mark
        end_mark_magic, end_mark_fmt_ver = struct.unpack_from(
            EVENT_LOG_ENDMARK_FMT,
            log_data,
            struct.calcsize(EVENT_LOG_START_FMT) + log_len,
        )
        if end_mark_magic != EVENT_LOG_MAGIC_CODE:
            raise MalformedTPMEventLog(
                "Malformed TPM log, magic code 0x%04x != 0x%04x"
                % (
                    end_mark_magic,
                    EVENT_LOG_MAGIC_CODE,
                )
            )
        if end_mark_fmt_ver not in [EVENT_LOG_FMT_VER]:
            raise MalformedTPMEventLog(
                "TPM log format ver-%d not supported, only support %s"
                % (
                    end_mark_fmt_ver,
                    [EVENT_LOG_FMT_VER],
                )
            )
        # parser the log records
        log_data = log_data[
            struct.calcsize(EVENT_LOG_START_FMT) : -struct.calcsize(
                EVENT_LOG_ENDMARK_FMT
            )
        ]
        cur = 0
        while cur < log_len:
            mid, pcrid, algo_id, index = struct.unpack_from(
                EVENT_LOG_RECORD_FMT, log_data, cur
            )
            cur += struct.calcsize(EVENT_LOG_RECORD_FMT)
            algo, digest_len = digest_algo_info[algo_id]
            tpm_events.append(
                TPMEventRecord(
                    mid,
                    measure_name[mid],
                    pcrid,
                    algo,
                    index,
                    log_data[cur : cur + digest_len],
                )
            )
            cur += digest_len
        if cur != log_len:
            raise MalformedTPMEventLog(
                f"Malformed event log, length {cur} != {log_len}"
            )

        return tpm_events

    def print_events(self, asjosn=False):
        class EventLogEncoder(json.JSONEncoder):
            def default(self, obj):
                if hasattr(obj, "hex"):
                    return obj.hex()
                return json.JSONEncoder.default(self, obj)

        if asjosn:
            print(json.dumps(self.tpm_events, cls=EventLogEncoder))
            return

        # normal print
        header_str = "".join(
            [
                f"{'ID':<4}",
                f"{'MID':^4}",
                f"{'Measure-Name':^20}",
                f"{'PCR':^4}",
                f"{'Index':^6}",
                f"{'Algo':^8}",
                f"{'Digest':^64}",
            ]
        )
        print("=" * len(header_str))
        print(header_str)
        print("-" * len(header_str))
        for i, event in enumerate(self.tpm_events, start=1):
            print(
                "".join(
                    [
                        f"{i:<4d}",
                        f"{event.mid:^4d}",
                        f"{event.measure:^20}",
                        f"{event.pcrid:^4d}",
                        f"{event.index:^6d}",
                        f"{event.algo:^8}",
                        f"{event.digest.hex():>64}",
                    ]
                )
            )
        print("=" * len(header_str))
        return

    def _measure_os_cached(self, algo, recal):
        key = (algo, recal)
        cached = self._os_measure_cache
        if cached is None or cached[0] != key:
            result = measure_os(algo, self.flash1_meta, recal, True)
            cached = (key, result)
            self._os_measure_cache = cached
        return cached[1]

    def _measure_rec_os_cached(self, algo, recal):
        key = (algo, recal)
        cached = self._rec_os_measure_cache
        if cached is None or cached[0] != key:
            result = measure_os(algo, self.flash0_meta, recal, True)
            cached = (key, result)
            self._rec_os_measure_cache = cached
        return cached[1]

    def _check_digest(self, measure, algo, digest, recal):
        expect_digest = None
        if measure == "spl":
            expect_digest = measure_spl(algo, self.flash0_meta, True)
        elif measure == "key-store":
            expect_digest = measure_keystore(algo, self.flash1_meta, True)
        elif measure == "u-boot":
            expect_digest = measure_uboot(algo, self.flash1_meta, recal, True)
        elif measure == "rec-u-boot":
            expect_digest = measure_rcv_uboot(algo, self.flash0_meta, True)
        elif measure == "os:kernel":
            expect_digest = self._measure_os_cached(algo, recal)[0]
        elif measure == "os:rootfs":
            expect_digest = self._measure_os_cached(algo, recal)[1]
        elif measure == "os:dtb":
            expect_digest = self._measure_os_cached(algo, recal)[2]
        elif measure == "recovery-os:kernel":
            expect_digest = self._measure_rec_os_cached(algo, recal)[0]
        elif measure == "recovery-os:rootfs":
            expect_digest = self._measure_rec_os_cached(algo, recal)[1]
        elif measure == "recovery-os:dtb":
            expect_digest = self._measure_rec_os_cached(algo, recal)[2]
        elif measure == "u-boot-env":  # u-boot-env can change skip checking
            expect_digest = digest
        elif measure == "vbs":
            try:
                expect_digest = measure_vbs(algo, True)
            except Exception:
                # not on BMC cannot measure vbs by-pass
                expect_digest = digest

        if expect_digest is None:
            raise InvalidTPMEventLog(f"Unknown meausre({measure})")

        if digest != expect_digest:
            raise InvalidTPMEventLog(
                f"{measure} digest does't match\n\t[%s](event)\n\t[%s](expect)"
                % (digest.hex(), expect_digest.hex())
            )

    def _build_pcr_replay(self, chkdigest, recal):
        pcr_replay = {}
        # build up PCR replay data from event
        for event in self.tpm_events:
            replay_target = f"{event.pcrid}.{event.algo}"
            if replay_target not in pcr_replay:
                pcr_replay[replay_target] = []
            # check the digest
            if chkdigest:
                self._check_digest(event.measure, event.algo, event.digest, recal)
            # add the digest into replay target's replay list
            pcr_replay[replay_target].append((event.index, event.measure, event.digest))

        # check each replay target's replay extension index start with 0, no gap
        for pcr_target, replay_list in pcr_replay.items():
            replay_list.sort(key=lambda e: e[0])
            if replay_list[0][0] != 0 or replay_list[-1][0] + 1 != len(replay_list):
                raise InvalidTPMEventLog(
                    "%s extension event in wrong sequence %s"
                    % (
                        pcr_target,
                        [e[0] for e in replay_list],
                    )
                )
        return pcr_replay

    def replay_measure(self, chkdigest, recal):
        replayed_measures = []
        pcr_replay = self._build_pcr_replay(chkdigest, recal)
        for replay_target, replay_list in pcr_replay.items():
            [pcrid, algo] = replay_target.split(".")
            pcr = Pcr(algo)
            major_measure = set()
            for _, measure, digest in replay_list:
                pcr.extend(digest)
                major_measure.add(measure.split(":")[0])

            if len(major_measure) != 1:
                raise InvalidTPMEventLog(
                    "Multiple major measure {set} extend in same PCR{pcrid}"
                )
            replayed_measures.append(
                {
                    "component": major_measure.pop(),
                    "pcr_id": int(pcrid),
                    "algo": algo,
                    "expect": pcr.value.hex(),
                    "measure": "NA",
                }
            )
        return replayed_measures
