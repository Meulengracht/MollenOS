#!/usr/bin/env python3
"""Preserve the kernel PE while adding an explicit native-wrapper payload boundary."""

import argparse
import pathlib
import struct

HEADER = struct.Struct("<8sIIQQQQQQ")
MAGIC = b"VALIRPI\0"
MAX_KERNEL = 64 * 1024 * 1024


def check_pe(kernel):
    # This is an artifact identity check, not the runtime PE loader's validation.
    if len(kernel) < 64 or kernel[:2] != b"MZ":
        raise ValueError("kernel is not a PE executable")
    pe = struct.unpack_from("<I", kernel, 60)[0]
    if pe > len(kernel) - 24 or kernel[pe:pe + 4] != b"PE\0\0":
        raise ValueError("invalid PE header bounds/signature")
    machine, count = struct.unpack_from("<HH", kernel, pe + 4)
    optional_size, flags = struct.unpack_from("<HH", kernel, pe + 20)
    optional = pe + 24
    sections = optional + optional_size
    if machine != 0xAA64 or not flags & 2:
        raise ValueError("kernel must be an ARM64 executable")
    if optional_size < 112 or sections > len(kernel) or not count:
        raise ValueError("invalid optional header or empty section table")
    if struct.unpack_from("<H", kernel, optional)[0] != 0x20B:
        raise ValueError("kernel must use PE32+")
    if count > (len(kernel) - sections) // 40:
        raise ValueError("truncated PE section table")
    for index in range(count):
        raw_size, raw_offset = struct.unpack_from("<II", kernel, sections + index * 40 + 16)
        if raw_size and (raw_offset > len(kernel) or raw_size > len(kernel) - raw_offset):
            raise ValueError("section contents lie outside kernel file")


def pack(loader, kernel):
    if len(loader) < HEADER.size or len(loader) % 4096:
        raise ValueError("wrapper must end on a 4 KiB boundary")
    expected = (MAGIC, 1, HEADER.size, len(loader), len(loader), 0, 0, 0, 0)
    if HEADER.unpack_from(loader, len(loader) - HEADER.size) != expected:
        raise ValueError("missing, incompatible, or already patched wrapper trailer")
    if not kernel or len(kernel) > MAX_KERNEL:
        raise ValueError("kernel exceeds the 64 MiB skeleton payload limit")
    check_pe(kernel)
    trailer = HEADER.pack(MAGIC, 1, HEADER.size, len(loader), len(loader),
                          len(kernel), len(loader) + len(kernel), 0, 0)
    return loader[:-HEADER.size] + trailer + kernel


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--loader", required=True, type=pathlib.Path)
    parser.add_argument("--kernel", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    try:
        if args.output.resolve() in (args.loader.resolve(), args.kernel.resolve()):
            raise ValueError("output must not overwrite either input")
        image = pack(args.loader.read_bytes(), args.kernel.read_bytes())
        args.output.write_bytes(image)
    except (OSError, ValueError) as error:
        parser.exit(1, f"rpi-pack: {error}\n")


if __name__ == "__main__":
    main()
