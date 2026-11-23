#!/usr/bin/env python3
"""SketchSlotProof — compiles, links and RUNS the slot tool.

Usage: python3 Tools/SketchSlotProof/SketchSlotProof.py

Like SketchPlacementProof, this executes engine code rather than parsing it. The slot outline
lives in `SlateShape` and the placement machine in `SketchToolset`; neither names a device, a
window or a vendor header, so both link and run on any toolchain.

What it proves, from `SketchSlotProof.cpp`:

  1. A straight two-point slot closes, does not cross itself, and every boundary point is
     exactly the slot radius from the spine.
  2. An L-shaped spine does the same — and its corner is a QUARTER-TURN ARC about the spine
     vertex, not the straight chord that used to cut through the slot body.
  3. A zig-zag, a sharp turn and a near fold-back all close. The fold-back genuinely overlaps
     itself, because its two legs run back over each other closer than the slot is thick; that
     is the spine the artist drew, not a corner defect, so it is stated rather than asserted
     away.
  4. The whole gesture: click, click, ENTER, drag, click. Before ENTER the slot previews as a
     polyline with no thickness; ENTER locks the spine without placing a stray anchor; after it
     the pointer thickens the slot; the confirming click completes.
  5. The thickness is the PERPENDICULAR distance to the spine, not the distance to its last
     point — the measure that made a pointer 20 out from the middle of a spine commit 53.852.

The proof has been negative-tested: reinstating the straight-chord corner makes section 2 report
a self-intersection and the wrong arc count, and reverting the thickness measure makes section 5
fail. A gate that has never been seen to fail proves nothing.
"""

import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

SOURCES = [
    ROOT / "Tools/SketchSlotProof/SketchSlotProof.cpp",
    ROOT / "Engine/SketchToolset/SketchTool/SketchPlacement/Source/SketchPlacement.cpp",
    ROOT / "Engine/SlateShape/Sketch/SketchStructure/Source/SketchStructure.cpp",
    ROOT / "Engine/SlateShape/Sketch/SketchPolyline/Source/SketchPolyline.cpp",
    ROOT / "Engine/SlateShape/Sketch/ConstraintSpecification/Source/ConstraintSpecification.cpp",
    ROOT / "Engine/SlateShape/Sketch/DimensionSpecification/Source/DimensionSpecification.cpp",
    ROOT / "Engine/SlateShape/Reference/ReferenceSpecification/Source/ReferenceSpecification.cpp",
    ROOT / "Engine/SlateShape/Geometry/CurveSpecification/Source/CurveSpecification.cpp",
    ROOT / "Engine/SlateShape/Geometry/ProfileSpecification/Source/ProfileSpecification.cpp",
]

INCLUDES = [ROOT / "Engine", ROOT / "Tools/VulkanParseStub"]


def Main():
    for Source in SOURCES:
        if not Source.exists():
            print(f"SketchSlotProof: missing {Source.relative_to(ROOT)}")
            raise SystemExit(1)

    with tempfile.TemporaryDirectory() as Scratch:
        Binary = pathlib.Path(Scratch) / "SketchSlotProof"

        Command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror"]
        for Include in INCLUDES:
            Command += ["-I", str(Include)]
        Command += [str(Source) for Source in SOURCES]
        Command += ["-o", str(Binary)]

        Built = subprocess.run(Command, capture_output=True, text=True)
        if Built.returncode != 0:
            print("SketchSlotProof: the slot geometry does not compile clean")
            print(Built.stderr.strip()[:4000])
            raise SystemExit(1)

        Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
        sys.stdout.write(Ran.stdout)
        if Ran.stderr.strip():
            sys.stderr.write(Ran.stderr)

        if Ran.returncode != 0:
            print("SketchSlotProof: REFUTED")
            raise SystemExit(1)

    print("SketchSlotProof: the slot tool stands")
    raise SystemExit(0)


if __name__ == "__main__":
    Main()
