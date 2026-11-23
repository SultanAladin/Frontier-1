#!/usr/bin/env python3
"""Builds and runs the menu-placement proof.

The proof owns header-only arithmetic, so it links nothing from the engine: `PlaceMenuClear` and its
neighbours are constexpr and live in Foundation. The translation unit is compiled with every warning
as an error.
"""

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

OWNED = ["Tools/MenuPlacementProof/MenuPlacementProof.cpp"]

INCLUDES = ["-I", ".", "-I", "Engine", "-I", "Tools/VulkanParseStub"]


def Main():
    Binary = ROOT / "Tools" / "MenuPlacementProof" / "MenuPlacementProof"
    Binary.unlink(missing_ok=True)

    Command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
               "-o", str(Binary)] + OWNED + INCLUDES
    Built = subprocess.run(Command, cwd=ROOT, capture_output=True, text=True)
    if Built.returncode != 0:
        print(Built.stderr[:4000])
        return 1

    Run = subprocess.run([str(Binary)], cwd=ROOT, capture_output=True, text=True)
    print(Run.stdout)
    if Run.returncode != 0:
        print(Run.stderr[:2000])

    Binary.unlink(missing_ok=True)
    return Run.returncode


if __name__ == "__main__":
    sys.exit(Main())
