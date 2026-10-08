#!/usr/bin/env python3
"""Writes the header checksum (0xBD) and pads the ROM to a 4-byte boundary."""
import sys

path = sys.argv[1]
data = bytearray(open(path, "rb").read())
data[0xBD] = (-(sum(data[0xA0:0xBD])) - 0x19) & 0xFF
while len(data) % 4:
    data.append(0)
open(path, "wb").write(data)
print(f"{path}: {len(data)} bytes, header checksum 0x{data[0xBD]:02X}")
