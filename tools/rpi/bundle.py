#!/usr/bin/env python3
"""Package Phoenix and the Vali ramdisk as one firmware-loaded initramfs."""

import argparse
import pathlib
import struct

from pack import check_pe

HEADER = struct.Struct("<8sIIQQQQQQ")
MAGIC = b"VALIBND\0"


def bundle(phoenix, ramdisk):
    check_pe(phoenix)
    if len(phoenix) > 64 * 1024 * 1024:
        raise ValueError("Phoenix exceeds the loader image limit")
    if not ramdisk or len(ramdisk) > 0xffffffff:
        raise ValueError("ramdisk length does not fit VBoot")
    ramdisk_offset = (HEADER.size + len(phoenix) + 4095) & ~4095
    total = ramdisk_offset + len(ramdisk)
    header = HEADER.pack(MAGIC, 1, HEADER.size, total, HEADER.size,
                         len(phoenix), ramdisk_offset, len(ramdisk), 0)
    padding = bytes(ramdisk_offset - HEADER.size - len(phoenix))
    return header + phoenix + padding + ramdisk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phoenix", required=True, type=pathlib.Path)
    parser.add_argument("--ramdisk", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    try:
        if args.output.resolve() in (args.phoenix.resolve(), args.ramdisk.resolve()):
            raise ValueError("output must not overwrite an input")
        image = bundle(args.phoenix.read_bytes(), args.ramdisk.read_bytes())
        args.output.write_bytes(image)
    except (OSError, ValueError) as error:
        parser.exit(1, f"rpi-bundle: {error}\n")


if __name__ == "__main__":
    main()
