#!/usr/bin/env python3
"""SpatialArithmeticProof — pins the arithmetic folded out of 21 translation units.

Usage: python3 Tools/SpatialArithmeticProof/SpatialArithmeticProof.py

Step 10 found nine vector functions copied byte-for-byte into the anonymous namespace of 21
files — 119 definitions in all — and folded them into one definition beside the types they
operate on. That is a large blast radius: a wrong sign is now wrong in the whole CAD kernel
rather than in one file. This gate is the arithmetic written down independently of the
implementation, so the fold stays honest.
"""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Tools/SpatialArithmeticProof/SpatialArithmeticProof.cpp"

with tempfile.TemporaryDirectory() as Scratch:
    Binary = Path(Scratch) / "SpatialArithmeticProof"
    Built = subprocess.run(
        ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
         "-I", str(ROOT), "-I", str(ROOT / "Engine"), str(SOURCE), "-o", str(Binary)],
        capture_output=True, text=True)
    if Built.returncode != 0:
        print("SpatialArithmeticProof: does not compile")
        print(Built.stderr.strip()[:3000])
        raise SystemExit(1)
    Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
    sys.stdout.write(Ran.stdout)
    raise SystemExit(0 if Ran.returncode == 0 else 1)
