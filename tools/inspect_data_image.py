#!/usr/bin/env python3
"""Dump and validate a packed QSPI data image (the inverse of build_data_image.py).

Use it to confirm an image looks right before flashing, or to inspect one read
back from the device. Mirrors the layout in data_format.h.

Usage:  python3 inspect_data_image.py data_image.bin
"""

import struct
import sys

MAGIC = 0x444D414E
SECTOR_SIZE = 4096
TYPE_NAMES = {0: "model", 1: "IR", 2: "preset"}


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)

    with open(sys.argv[1], "rb") as f:
        img = f.read()

    magic, version, count = struct.unpack_from("<IHH", img, 0)
    ok = magic == MAGIC
    print(f"magic   0x{magic:08X}  {'OK' if ok else 'BAD (expected 0x444D414E)'}")
    print(f"version {version}")
    print(f"count   {count}")
    if not ok:
        sys.exit(1)

    print(f"\n{'type':<7} {'name':<24} {'offset':>10} {'length':>9} {'rate':>7}  align")
    pos = 8
    errors = 0
    for _ in range(count):
        t, name, off, length, rate, _res = struct.unpack_from("<B31sIIII", img, pos)
        pos += 48
        nm = name.split(b"\x00")[0].decode("ascii", "replace")
        aligned = (off % SECTOR_SIZE) == 0
        in_range = off + length <= len(img)
        if not aligned or not in_range:
            errors += 1
        flag = "ok" if (aligned and in_range) else "ERR"
        extra = ""
        if t == 1 and in_range:
            extra = f"  ({length // 4} taps)"
        print(
            f"{TYPE_NAMES.get(t, '?'):<7} {nm:<24} {off:>10} {length:>9} {rate:>7}  {flag}{extra}"
        )

    print(f"\nimage size: {len(img)} bytes ({len(img)//1024} KiB)")
    if errors:
        print(f"FOUND {errors} bad entr{'y' if errors == 1 else 'ies'}")
        sys.exit(1)
    print("all entries valid")


if __name__ == "__main__":
    main()
