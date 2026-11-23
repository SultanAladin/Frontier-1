#!/usr/bin/env python3
"""CornerOperationProof — compiles, links and RUNS the 2D fillet and chamfer.

Usage: python3 Tools/CornerOperationProof/CornerOperationProof.py

Executes engine code rather than parsing it. What it proves, from `CornerOperationProof.cpp`:

  1. A fillet is an ARC TANGENT TO BOTH LEGS, checked against that definition: the arc's centre is
     exactly one radius from each leg's infinite line, its ends are exactly the two tangent points,
     and the sharp corner is cut away by exactly R*(sqrt2 - 1) at a right angle.
  2. A chamfer is the chord between the fillet's own tangent points, so the two operations are proven
     to shorten each leg to the very same point. If they ever disagree, one of them is wrong.
  3. A corner states its own LIMIT — half the shorter leg, so two corners of one short edge both fit —
     and refuses for reasons a caller can tell apart: not-positive, beyond-limit, no-shared-endpoint,
     collinear, unsupported-geometry. A refusal changes nothing.
  4. Filleting a corner of a closed loop keeps the legs' NAMES, so the loop still traverses them; and
     stitching the new arc into the traversal closes and refills it.
  5. Corners are found by pointing at them, in any drawing order — including two lines drawn outward
     from a shared point, which meet origin-to-origin and which the retired loop-walking corner finder
     could not see at all.
  6. The whole gesture: hover, press, drag, clamp, release to the popup, type an exact figure, Apply.
     The release commits NOTHING; only Apply writes. A typed figure and a dragged one are one number.

Negative-tested: reverting the tangent reach to `Radius * tan(theta/2)`, dropping the half-leg clamp,
or applying on release rather than on Apply each refute a section above. A gate that has never been
seen to fail proves nothing.
"""

import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

SOURCES = [
    ROOT / "Tools/CornerOperationProof/CornerOperationProof.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/CornerDragSession/Source/CornerDragSession.cpp",
    ROOT / "Engine/SlateShape/World/WorldSketchCorner/Source/WorldSketchCorner.cpp",
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
            print(f"CornerOperationProof: missing {Source.relative_to(ROOT)}")
            raise SystemExit(1)

    with tempfile.TemporaryDirectory() as Scratch:
        Binary = pathlib.Path(Scratch) / "CornerOperationProof"

        Command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror"]
        for Include in INCLUDES:
            Command += ["-I", str(Include)]
        Command += [str(Source) for Source in SOURCES]
        Command += ["-o", str(Binary)]

        Built = subprocess.run(Command, capture_output=True, text=True)
        if Built.returncode != 0:
            print("CornerOperationProof: the corner geometry does not compile clean")
            print(Built.stderr.strip()[:4000])
            raise SystemExit(1)

        Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
        sys.stdout.write(Ran.stdout)
        if Ran.stderr.strip():
            sys.stderr.write(Ran.stderr)

        if Ran.returncode != 0:
            print("CornerOperationProof: REFUTED")
            raise SystemExit(1)

    print("CornerOperationProof: the corner operations stand")
    raise SystemExit(0)


if __name__ == "__main__":
    Main()
