#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_magic_header.py - XBIN firmware packing tool

PURPOSE:
    Pack a raw .bin firmware into a self-describing .xbin upgrade file.
    The header bakes in version / target address / length / CRC32 / hardware
    model / minimum-BL-version, so the BootLoader validates the header first
    before deciding to copy it into internal Flash.

    Output .xbin = 4096-byte header + bin data. The header layout MUST match
    magic_header_t in BootLoader/App/magic_header.h field-by-field.

USAGE:
    # pack a firmware pack (default address 0x08009000)
    python gen_magic_header.py --input app.bin --version v1.2.0

    # explicit address / model / min-BL-version
    python gen_magic_header.py --input app.bin --data-address 0x08009000 \
           --hw-model 1 --bl-min 1 --version v1.2.0 --output app_upgrade.xbin
"""

import argparse
import os
import sys
import time
import zlib

# ---------------- constants (keep in sync with magic_header.h) ----------------
MAGIC              = 0x4D414749   # "MAGI"
DATA_TYPE_APP      = 0            # firmware code
DATA_TYPE_RESOURCE = 1            # resource pack
HEADER_SIZE        = 4096         # header fixed at 4KB
DEFAULT_ADDRESS    = 0x08009000   # App payload start (XBIN header occupies 0x08008000..0x08008FFF, payload at +4096)
HW_MODEL           = 0x0001       # WeatherClock_F407VET6
BL_VERSION_MIN     = 1


def build_header(data_type, data_address, data_length, data_crc, version, hw_model, bl_min):
    """Build the 252-byte header core (fields before this_crc32), per magic_header_t layout"""
    h = b""
    h += MAGIC.to_bytes(4, "little")                      # 0x00 magic
    h += (0).to_bytes(4, "little")                        # 0x04 bitmask
    h += (0).to_bytes(4 * 6, "little")                    # 0x08 reserved1[6]
    h += data_type.to_bytes(4, "little")                  # 0x20 data_type
    h += HEADER_SIZE.to_bytes(4, "little")                # 0x24 data_offset
    h += data_address.to_bytes(4, "little")               # 0x28 data_address
    h += data_length.to_bytes(4, "little")                # 0x2C data_length
    h += data_crc.to_bytes(4, "little")                   # 0x30 data_crc32
    h += hw_model.to_bytes(4, "little")                   # 0x34 hw_model
    h += bl_min.to_bytes(4, "little")                     # 0x38 bl_version_min
    h += (0).to_bytes(4 * 9, "little")                    # 0x3C reserved2[9]
    h += version.encode("ascii").ljust(128, b"\x00")      # 0x60 version[128]
    h += (0).to_bytes(4 * 6, "little")                    # 0xE0 reserved3[6]
    h += (0).to_bytes(4, "little")                        # 0xF8 this_address
    return h                                              # exactly 252 bytes here


def main():
    p = argparse.ArgumentParser(description="XBIN firmware packing tool")
    p.add_argument("--input", required=True, help="input bin file")
    p.add_argument("--output", default=None, help="output .xbin (default: same dir + _upgrade.xbin)")
    p.add_argument("--data-address", default=DEFAULT_ADDRESS, type=lambda x: int(x, 0),
                   help="target address (default 0x08008000)")
    p.add_argument("--data-type", default=DATA_TYPE_APP, type=int,
                   help="pack type 0=firmware 1=resource")
    p.add_argument("--hw-model", default=HW_MODEL, type=int, help="hardware model (anti-misflash)")
    p.add_argument("--bl-min", default=BL_VERSION_MIN, type=int, help="minimum BL version")
    p.add_argument("--version", default=None, help="version string (default auto timestamp)")
    args = p.parse_args()

    if not os.path.isfile(args.input):
        print("Error: file not found: %s" % args.input)
        sys.exit(1)

    with open(args.input, "rb") as f:
        bin_data = f.read()
    bin_crc = zlib.crc32(bin_data) & 0xFFFFFFFF

    if args.version is None:
        args.version = "v1.0.0-%s-%s" % (time.strftime("%y%m%d"), time.strftime("%H%M"))

    # build 252-byte core, compute self-CRC, append this_crc32, pad to 4096
    header_core = build_header(args.data_type, args.data_address,
                               len(bin_data), bin_crc,
                               args.version, args.hw_model, args.bl_min)
    this_crc = zlib.crc32(header_core) & 0xFFFFFFFF
    header = header_core + this_crc.to_bytes(4, "little")  # 0xFC this_crc32
    header = header.ljust(HEADER_SIZE, b"\x00")            # pad to 4096

    if args.output is None:
        base = os.path.splitext(os.path.basename(args.input))[0]
        args.output = os.path.join(os.path.dirname(args.input), base + "_upgrade.xbin")

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "wb") as f:
        f.write(header + bin_data)

    print("Packed: %s" % args.output)
    print("  version     : %s" % args.version)
    print("  data address: 0x%08X" % args.data_address)
    print("  length      : %d bytes" % len(bin_data))
    print("  CRC32       : 0x%08X" % bin_crc)


if __name__ == "__main__":
    main()