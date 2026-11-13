#!/usr/bin/env python3
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
"""Set SMBIOS DMI strings in a BIOS image.

The BIOS does not keep these strings in the SMBIOS table itself. It stores them
as records in an OEM DMI store (the '$DMI' flash region) and applies them to
the table on every boot. This script writes those records straight into an
image file, so system-specific values can be put back into a new BIOS before
it is flashed.

    python3 DmiPatch.py NEW.bin [--from OLD.bin] [field options] [-o OUT.bin]
    python3 DmiPatch.py IMAGE --show [--json]

Field options
-------------
  -SP STR    System product name       (Type 1, offset 0x05)
  -SV STR    System version            (Type 1, offset 0x06)
  -SS STR    System serial number      (Type 1, offset 0x07)
  -BP STR    Baseboard product name    (Type 2, offset 0x05)
  -CV STR    Chassis version           (Type 3, offset 0x06)
  -CS STR    Chassis serial number     (Type 3, offset 0x07)
  -CSKU STR  Chassis SKU number        (Type 3, offset 0x15)
  --set T:OFF=VALUE
             Any other field. VALUE is a string, or hex:<bytes> for a raw
             field such as the Type 1 UUID (1:0x08=hex:00112233...). Repeatable.

Other options
-------------
  --from OLD   Carry over every live record from OLD's $DMI store first (e.g. a
               flashrom dump of the chip about to be reflashed). Field options
               are applied on top, so they win.
  --show       Print the live records in IMAGE and exit. With --json, print them
               as {"T:0xOFF": "value"} for use by another script.
  -o PATH      Output image (default: <IMAGE>.dmi.bin). The input is never
               modified unless -o names it.
  --fdm-offset N
               Force the FDM location instead of scanning for 'HFDM'.

The rewritten store holds only the live records, compacted, so it never
needs the BIOS-side reclaim.

Record format: a 5-byte header (type, offset, valid flag, UINT16 length)
followed by the data with no NUL terminator; see REC_HDR_LEN below.
"""

import argparse
import json
import struct
import sys

FDM_SIG = b"HFDM"
DMI_SIG = b"$DMI"

FLASH_MAP_TYPE = "f078c1a0-fc52-4c3f-be1f-d688815a62c0"  # flash map region GUID
SMBIOS_UPD_TYPE = (
    "8964fedc-6fe7-4e1e-a55e-ff821d71ffcf"  # SMBIOS update ($DMI) region GUID
)

ATTRIB_MUTABLE = 0x1
ENTRY_LEN = 52  # flash device map entry without the trailing hash

REC_HDR_LEN = 5  # Type, Offset, Valid, Length(UINT16); data follows, no NUL
REC_VALID = 0xFF  # the BIOS clears this to 0x00 to retire a record

# The last 8 bytes of the store hold the '$REC' reclaim flag and are never
# used for records.
TAIL_RESERVED = 8

# field option -> (type, offset)
FLAGS = {
    "SP": (1, 0x05),
    "SV": (1, 0x06),
    "SS": (1, 0x07),
    "BP": (2, 0x05),
    "CV": (3, 0x06),
    "CS": (3, 0x07),
    "CSKU": (3, 0x15),
}

NAMES = {
    (1, 0x04): "System manufacturer",
    (1, 0x05): "System product",
    (1, 0x06): "System version",
    (1, 0x07): "System serial",
    (1, 0x08): "System UUID",
    (1, 0x19): "System SKU",
    (1, 0x1A): "System family",
    (2, 0x04): "Board manufacturer",
    (2, 0x05): "Board product",
    (2, 0x06): "Board version",
    (2, 0x07): "Board serial",
    (2, 0x08): "Board asset tag",
    (2, 0x0A): "Board location",
    (3, 0x04): "Chassis manufacturer",
    (3, 0x05): "Chassis type",
    (3, 0x06): "Chassis version",
    (3, 0x07): "Chassis serial",
    (3, 0x08): "Chassis asset tag",
    (3, 0x15): "Chassis SKU",
}

# The BIOS only applies a $DMI record whose (type, offset) is in its table of
# updatable strings. This is that table, including Chassis SKU (3, 0x15),
# which this platform's BIOS supports (verified with dmidecode on the host).
UPDATABLE = {
    (1, 4),
    (1, 5),
    (1, 6),
    (1, 7),
    (1, 8),
    (2, 4),
    (2, 5),
    (2, 6),
    (2, 7),
    (3, 4),
    (3, 5),
    (3, 6),
    (3, 7),
    (3, 8),
    (3, 0x15),
    (11, 4),
    (11, 5),
    (11, 6),
    (11, 7),
    (11, 8),
    (11, 9),
}


def guid_str(raw):
    d1, d2, d3 = struct.unpack_from("<IHH", raw, 0)
    d4 = raw[8:16]
    return "%08x-%04x-%04x-%s-%s" % (d1, d2, d3, d4[:2].hex(), d4[2:].hex())


def find_fdm(blob, forced=None):
    if forced is not None:
        return forced
    pos = blob.find(FDM_SIG)
    while pos != -1:
        size, off, esize = struct.unpack_from("<III", blob, pos + 4)
        if 0 < off < size <= len(blob) - pos and ENTRY_LEN <= esize <= 256:
            return pos
        pos = blob.find(FDM_SIG, pos + 1)
    raise SystemExit("no valid 'HFDM' header found")


def find_dmi_region(blob, path, forced_fdm=None):
    """Return (file_offset, size) of the $DMI store in blob."""
    base = find_fdm(blob, forced_fdm)
    size, off, esize = struct.unpack_from("<III", blob, base + 4)
    fdm_roff = dmi = None
    for i in range((size - off) // esize):
        e = base + off + i * esize
        rtype = guid_str(blob[e : e + 16])
        roff, rsize = struct.unpack_from("<QQ", blob, e + 32)
        attrs = struct.unpack_from("<I", blob, e + 48)[0]
        if rtype == FLASH_MAP_TYPE:
            fdm_roff = roff
        elif rtype == SMBIOS_UPD_TYPE and dmi is None:
            dmi = (roff, rsize, attrs)
    if fdm_roff is None or dmi is None:
        raise SystemExit(
            "%s: FDM has no %s row"
            % (path, "flash-map" if fdm_roff is None else "SMBIOS-update")
        )

    # Region offsets are relative to the start of the FD. Anchoring on the FDM's
    # own row makes this work for a bare .FD and for a whole-chip flashrom dump
    # that places the FD somewhere other than offset 0.
    fd_start = base - fdm_roff
    roff, rsize, attrs = dmi
    start = fd_start + roff
    if start < 0 or start + rsize > len(blob):
        raise SystemExit(
            "%s: $DMI region 0x%X+0x%X lies outside the file" % (path, start, rsize)
        )
    if not attrs & ATTRIB_MUTABLE:
        raise SystemExit(
            "%s: $DMI region is not MUTABLE in the FDM, so editing it "
            "would fail the FDM hash check" % path
        )
    if blob[start : start + 4] != DMI_SIG:
        raise SystemExit(
            "%s: no '$DMI' signature at 0x%X (found %s)"
            % (path, start, blob[start : start + 4].hex())
        )
    return start, rsize


def read_live(blob, start, size, path):
    """Return {(type, offset): bytes} - the first valid record per field, which
    is the one the BIOS applies at boot."""
    live = {}
    pos, end = start + 4, start + size - TAIL_RESERVED
    while pos + REC_HDR_LEN <= end and blob[pos] != 0xFF:
        rtype, roff, valid, length = struct.unpack_from("<BBBH", blob, pos)
        if length < REC_HDR_LEN or pos + length > end:
            raise SystemExit(
                "%s: corrupt $DMI record at 0x%X (length %d)" % (path, pos, length)
            )
        if valid == REC_VALID and (rtype, roff) not in live:
            live[(rtype, roff)] = bytes(blob[pos + REC_HDR_LEN : pos + length])
        pos += length
    return live


def build_region(records, size):
    out = bytearray(DMI_SIG)
    for (rtype, roff), data in sorted(records.items()):
        out += (
            struct.pack("<BBBH", rtype, roff, REC_VALID, REC_HDR_LEN + len(data)) + data
        )
    if len(out) > size - TAIL_RESERVED:
        raise SystemExit(
            "records need %d bytes; the $DMI store holds %d"
            % (len(out), size - TAIL_RESERVED)
        )
    return bytes(out) + b"\xff" * (size - len(out))


def encode(value, what):
    if value.lower().startswith("hex:"):
        try:
            return bytes.fromhex(value[4:])
        except ValueError:
            raise SystemExit("%s: bad hex value '%s'" % (what, value))
    try:
        data = value.encode("ascii")
    except UnicodeEncodeError:
        raise SystemExit("%s: '%s' is not ASCII" % (what, value))
    if b"\0" in data:
        raise SystemExit("%s: value contains a NUL" % what)
    return data


def parse_set(spec):
    try:
        key, value = spec.split("=", 1)
        t, o = key.split(":")
        return (int(t, 0), int(o, 0)), value
    except ValueError:
        raise SystemExit("--set expects T:OFF=VALUE, got '%s'" % spec)


def show_value(data):
    try:
        text = data.decode("ascii")
        if text.isprintable():
            return text
    except UnicodeDecodeError:
        pass
    return "hex:" + data.hex()


def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("image", nargs="?")
    ap.add_argument("-o", "--out")
    ap.add_argument("--from", dest="src")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--show", action="store_true")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--fdm-offset", type=lambda s: int(s, 0))
    ap.add_argument("-h", "--help", action="store_true")
    for flag in FLAGS:
        ap.add_argument("-" + flag, dest=flag)
    args = ap.parse_args()
    if args.help or not args.image:
        raise SystemExit(__doc__)

    blob = bytearray(open(args.image, "rb").read())
    start, size = find_dmi_region(blob, args.image, args.fdm_offset)
    records = read_live(blob, start, size, args.image)

    if args.show:
        if args.json:
            print(
                json.dumps(
                    {
                        "%d:0x%02X" % k: show_value(v)
                        for k, v in sorted(records.items())
                    },
                    indent=2,
                )
            )
        else:
            print(
                "$DMI @ 0x%X+0x%X of %s: %d live record(s)"
                % (start, size, args.image, len(records))
            )
            for (t, o), v in sorted(records.items()):
                print(
                    "  %2d:0x%02X  %-22s %s"
                    % (t, o, NAMES.get((t, o), ""), show_value(v))
                )
        return

    if args.src:
        old = open(args.src, "rb").read()
        s, n = find_dmi_region(old, args.src)
        carried = read_live(old, s, n, args.src)
        print("carrying %d record(s) from %s" % (len(carried), args.src))
        records.update(carried)

    changes = {}
    for flag, key in FLAGS.items():
        value = getattr(args, flag)
        if value is not None:
            changes[key] = encode(value, "-" + flag)
    for spec in args.set:
        key, value = parse_set(spec)
        changes[key] = encode(value, "--set " + spec)
    records.update(changes)

    if not args.src and not changes:
        raise SystemExit("nothing to do: give --from and/or field options (or --show)")

    for key in sorted(records):
        if key not in UPDATABLE:
            print(
                "WARNING: %d:0x%02X (%s) is not in the BIOS updatable-string table; "
                "it will be ignored at boot" % (key + (NAMES.get(key, "?"),)),
                file=sys.stderr,
            )

    blob[start : start + size] = build_region(records, size)

    out = args.out or args.image + ".dmi.bin"
    open(out, "wb").write(bytes(blob))
    print("$DMI @ 0x%X+0x%X: wrote %d record(s)" % (start, size, len(records)))
    for (t, o), v in sorted(records.items()):
        print("  %2d:0x%02X  %-22s %s" % (t, o, NAMES.get((t, o), ""), show_value(v)))
    print("wrote %s" % out)


if __name__ == "__main__":
    main()
