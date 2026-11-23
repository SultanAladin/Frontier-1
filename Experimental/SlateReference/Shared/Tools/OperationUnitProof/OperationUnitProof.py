#!/usr/bin/env python3
"""OperationUnitProof — compiles, links and RUNS the operations readout's unit conversion.

Usage: python3 Tools/OperationUnitProof/OperationUnitProof.py

Executes engine code rather than parsing it. What it proves, from `OperationUnitProof.cpp`:

  1. The figure a session holds in MILLIMETRES is shown in whatever unit the artist chose — 500 mm
     reads as 0.5 with metres selected, and as exactly 20 with inches. It used to read 500 whatever
     the panel said.
  2. The unit cell beside the value names that same unit, instead of being hardcoded to "mm" and
     contradicting the number next to it.
  3. THE REPORTED DEFECT — the slider's RANGE converts along with the value. Left in millimetres while
     the reading was converted, a metre slider ran to 500 while its value could only reach 0.5, so the
     usable travel was under a thousandth of the control: "too conservative", exactly as reported.
     The top of travel is also proven to accept back to precisely the gesture's own clamp, in every
     unit, so the readout can never offer a figure the operation will refuse.
  4. A typed figure returns to millimetres exactly, in every unit — `ToDisplay` and `ToMillimetres` are
     inverses, so a value shown and accepted is the value stored and cannot creep.
  5. An Offset's negative side converts too, and its inward extreme accepts back to the full limit.
  6. The declared precision is enough to SEE the value: a 5 mm fillet in metres rendered as "0.01" at
     the old fixed two places and renders as "0.005" at the unit's declared places, while millimetres
     are untouched.

Negative-tested: publishing the figure without `ToDisplay`, leaving the limit unconverted, or fixing
the precision back at two places each refute a section above. A gate that has never been seen to fail
proves nothing.
"""

import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

SOURCES = [
    ROOT / "Tools/OperationUnitProof/OperationUnitProof.cpp",
]

INCLUDES = [ROOT / "Engine", ROOT / "Tools/VulkanParseStub"]


def Main():
    for Source in SOURCES:
        if not Source.exists():
            print(f"OperationUnitProof: missing {Source.relative_to(ROOT)}")
            raise SystemExit(1)

    with tempfile.TemporaryDirectory() as Scratch:
        Binary = pathlib.Path(Scratch) / "OperationUnitProof"

        Command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror"]
        for Include in INCLUDES:
            Command += ["-I", str(Include)]
        Command += [str(Source) for Source in SOURCES]
        Command += ["-o", str(Binary)]

        Built = subprocess.run(Command, capture_output=True, text=True)
        if Built.returncode != 0:
            print("OperationUnitProof: the readout conversion does not compile clean")
            print(Built.stderr.strip()[:4000])
            raise SystemExit(1)

        Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
        sys.stdout.write(Ran.stdout)
        if Ran.stderr.strip():
            sys.stderr.write(Ran.stderr)

        if Ran.returncode != 0:
            print("OperationUnitProof: REFUTED")
            raise SystemExit(1)

    print("OperationUnitProof: the readout speaks the artist's unit")
    raise SystemExit(0)


if __name__ == "__main__":
    Main()
