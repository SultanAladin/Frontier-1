#!/usr/bin/env python3
"""SketchOperationProof — compiles, links and RUNS Cut, Trim, Extend, Offset and Fill.

Usage: python3 Tools/SketchOperationProof/SketchOperationProof.py

Executes engine code rather than parsing it. What it proves, from `SketchOperationProof.cpp`:

  1. Cut divides and LOSES NOTHING — the pieces still sum to the original length — and keeps the subject
     as the leading piece, so loops, constraints and selections naming it stay valid. Cutting at an
     endpoint or off the curve refuses, and the refusal changes nothing.
  2. Cutting at every crossing divides in the right PLACES, proven by a line crossed twice whose three
     pieces must be 25, 35 and 40 in order — the lengths a front-to-back cut gets wrong.
  3. Trim removes what was pointed at and keeps the rest, and an INTERIOR trim leaves TWO curves. The
     tempting implementation shortens the subject and silently loses everything past the far crossing.
  4. Extend grows the nearer end to the FIRST curve it meets, not the furthest, proven against a fixture
     with two walls. With nothing to meet it refuses rather than inventing a length.
  5. An offset holds its distance EVERYWHERE, sampled along its whole length including across a corner —
     the test a corner-pushing offset fails, and which checking only endpoints would let through.
  6. The offset states the distance at which it would collapse, proven from both sides: just inside the
     limit works, just beyond it refuses.
  7. A circle inside a circle is a TUBE. Nesting depth decides fill: odd is a hole, even is material, so
     a third ring inside the hole fills again. Rings on different planes never nest. Fill itself is the
     artist's WISH, which overrides fillable geometry and cannot make an open loop fillable.
  8. The gestures: the click operations perform on release and raise no readout; Offset drags, clamps its
     MAGNITUDE while keeping its sign, hands the figure to the readout on release without writing, takes
     a typed figure through the same clamp, and writes only on Apply.

Negative-tested: cutting forwards rather than backwards, clamping the offset's signed value, returning
one curve from an interior trim, and deciding fill from geometry alone each refute a section above.
A gate that has never been seen to fail proves nothing.
"""

import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

SOURCES = [
    ROOT / "Tools/SketchOperationProof/SketchOperationProof.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/SketchOperationSession/Source/SketchOperationSession.cpp",
    ROOT / "Engine/SlateShape/World/WorldSketchOperations/Source/WorldSketchOperations.cpp",
    ROOT / "Engine/SlateShape/World/WorldSketchStructure/Source/WorldSketchStructure.cpp",
    ROOT / "Engine/SlateShape/World/WorldSketchAnalysis/Source/WorldSketchAnalysis.cpp",
    ROOT / "Engine/SlateShape/Sketch/SketchPolyline/Source/SketchPolyline.cpp",
    ROOT / "Engine/SlateShape/Geometry/CurveSpecification/Source/CurveSpecification.cpp",
    ROOT / "Engine/SlateShape/Geometry/ProfileSpecification/Source/ProfileSpecification.cpp",
]

INCLUDES = [ROOT / "Engine", ROOT / "Tools/VulkanParseStub"]


def Main():
    for Source in SOURCES:
        if not Source.exists():
            print(f"SketchOperationProof: missing {Source.relative_to(ROOT)}")
            raise SystemExit(1)

    with tempfile.TemporaryDirectory() as Scratch:
        Binary = pathlib.Path(Scratch) / "SketchOperationProof"

        Command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror"]
        for Include in INCLUDES:
            Command += ["-I", str(Include)]
        Command += [str(Source) for Source in SOURCES]
        Command += ["-o", str(Binary)]

        Built = subprocess.run(Command, capture_output=True, text=True)
        if Built.returncode != 0:
            print("SketchOperationProof: the operation geometry does not compile clean")
            print(Built.stderr.strip()[:4000])
            raise SystemExit(1)

        Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
        sys.stdout.write(Ran.stdout)
        if Ran.stderr.strip():
            sys.stderr.write(Ran.stderr)

        if Ran.returncode != 0:
            print("SketchOperationProof: REFUTED")
            raise SystemExit(1)

    print("SketchOperationProof: the sketch operations stand")
    raise SystemExit(0)


if __name__ == "__main__":
    Main()
