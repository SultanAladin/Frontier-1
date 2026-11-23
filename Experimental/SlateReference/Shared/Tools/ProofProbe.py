#!/usr/bin/env python3
"""ProofProbe — reads back a RIFT-encoded PNG and asserts applied inks, plus prints an ASCII reduction.

Usage: python3 Tools/ProofProbe.py <proof.png> [--probe along across expectedHex tolerance]...
The decoder is the mirror of EncodeProof (filter 0, stdlib only).
"""

import struct
import sys
import zlib


def Decode(Path):
    with open(Path, "rb") as Stream:
        Blob = Stream.read()
    assert Blob[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    Position = 8
    Width = Height = 0
    Scanlines = b""
    while Position < len(Blob):
        Length, = struct.unpack(">I", Blob[Position:Position + 4])
        Tag = Blob[Position + 4:Position + 8]
        Body = Blob[Position + 8:Position + 8 + Length]
        Position += 12 + Length
        if Tag == b"IHDR":
            Width, Height = struct.unpack(">II", Body[:8])
        elif Tag == b"IDAT":
            Scanlines += Body
    Raw = zlib.decompress(Scanlines)
    Stride = Width * 4
    Pixels = bytearray(Width * Height * 4)
    for Row in range(Height):
        Offset = Row * (Stride + 1)
        Filter = Raw[Offset]
        assert Filter == 0, f"unexpected filter {Filter}"
        Pixels[Row * Stride:(Row + 1) * Stride] = Raw[Offset + 1:Offset + 1 + Stride]
    return Width, Height, Pixels


def Sample(Pixels, Width, X, Y):
    Offset = (Y * Width + X) * 4
    return Pixels[Offset], Pixels[Offset + 1], Pixels[Offset + 2], Pixels[Offset + 3]


def Main():
    Path = sys.argv[1]
    Width, Height, Pixels = Decode(Path)
    print(f"ProofProbe: {Path} ({Width}x{Height})")

    Failures = 0
    Arguments = sys.argv[2:]
    Index = 0
    while Index + 6 <= len(Arguments):
        _, X, Y, ExpectedHex, Tolerance, Label = Arguments[Index:Index + 6]
        Index += 6
        X, Y = int(X), int(Y)
        Expected = int(ExpectedHex, 16)
        Delta = int(Tolerance)
        Red, Green, Blue, Alpha = Sample(Pixels, Width, X, Y)
        Passed = (abs(Red - ((Expected >> 16) & 0xFF)) <= Delta and
                  abs(Green - ((Expected >> 8) & 0xFF)) <= Delta and
                  abs(Blue - (Expected & 0xFF)) <= Delta)
        Mark = "PASS" if Passed else "FAIL"
        if not Passed:
            Failures += 1
        Got = (Red << 16) | (Green << 8) | Blue
        print(f"  [{Mark}] ({X:4d},{Y:4d}) {Label:<28} want #{ExpectedHex} got #{Got:06x} a={Alpha}")

    # ① The ASCII reduction — one character per 15x15 cell, brightness ramp.
    Ramp = " .:-=+*#%@"
    CellExtent = 15
    Columns = Width // CellExtent
    RowsOut = Height // CellExtent
    print("  ASCII reduction:")
    for Row in range(RowsOut):
        Line = ""
        for Column in range(Columns):
            Total = 0
            Count = 0
            for Y in range(Row * CellExtent, (Row + 1) * CellExtent, 3):
                for X in range(Column * CellExtent, (Column + 1) * CellExtent, 3):
                    Red, Green, Blue, _ = Sample(Pixels, Width, X, Y)
                    Total += (Red + Green + Blue) // 3
                    Count += 1
            Luminance = Total // max(Count, 1)
            Line += Ramp[min(Luminance * len(Ramp) // 256, len(Ramp) - 1)]
        print("  |" + Line + "|")

    raise SystemExit(1 if Failures else 0)


if __name__ == "__main__":
    Main()
