#!/usr/bin/env python3
"""EncodeProof — one marked raw dump to one compressed PNG, stdlib only.

Usage: python3 Tools/EncodeProof.py <input.rgba> <output.png>
The dump carries the marker RIFTRAW1, the along extent and the across extent
before its RGBA ordinates.
"""

import struct
import sys
import zlib


def Encode(SourcePath, DestinationPath):
    with open(SourcePath, "rb") as Stream:
        Marker = Stream.read(8)
        if Marker != b"RIFTRAW1":
            raise SystemExit(f"{SourcePath}: not a marked raw dump")
        XExtent, YExtent = struct.unpack("<II", Stream.read(8))
        Configuration = Stream.read(XExtent * YExtent * 4)
        if len(Configuration) != XExtent * YExtent * 4:
            raise SystemExit(f"{SourcePath}: ordinates short of the declared extent")

    # ① Filter 0 per scanline, zlib-compressed — the smallest honest PNG.
    Stride = XExtent * 4
    Scanlines = bytearray()
    for Y in range(YExtent):
        Scanlines.append(0)
        Scanlines.extend(Configuration[Y * Stride:(Y + 1) * Stride])
    Compressed = zlib.compress(bytes(Scanlines), 9)

    def Chunk(Tag, Body):
        CRC = zlib.crc32(Tag + Body) & 0xFFFFFFFF
        return struct.pack(">I", len(Body)) + Tag + Body + struct.pack(">I", CRC)

    Png = bytearray()
    Png += b"\x89PNG\r\n\x1a\n"
    Png += Chunk(b"IHDR", struct.pack(">IIBBBBB", XExtent, YExtent, 8, 6, 0, 0, 0))
    Png += Chunk(b"IDAT", Compressed)
    Png += Chunk(b"IEND", b"")

    with open(DestinationPath, "wb") as Stream:
        Stream.write(Png)
    print(f"EncodeProof: {DestinationPath} ({XExtent}x{YExtent}, {len(Png)} bytes)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    Encode(sys.argv[1], sys.argv[2])
