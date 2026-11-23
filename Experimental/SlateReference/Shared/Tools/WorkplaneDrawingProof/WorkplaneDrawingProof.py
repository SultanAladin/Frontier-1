#!/usr/bin/env python3
"""WorkplaneDrawingProof — compiles, links and RUNS every drawing tool on every workplane.

Usage: python3 Tools/WorkplaneDrawingProof/WorkplaneDrawingProof.py

Executes engine code rather than parsing it. What it proves, from `WorkplaneDrawingProof.cpp`:

  1. Every drawing subject, placed on each of five workplanes — Ground, Front, Right, an OFFSET
     plane and a TILTED one — lands entirely in the plane it was drawn on. Curves are
     tessellated, so a shape's interior is measured and not merely its endpoints.
  2. A circle's and an ellipse's own NORMAL is the plane's normal, and their start/major
     directions lie in the plane. This is what the artist sees as "the face does not face the
     camera": a hardcoded world-up normal on a shape drawn down the Z axis is a face turned
     ninety degrees away, edge-on, whose fill reads as a line.
  3. A polygon has the side count asked for and every vertex the full radius from its centre.
     Rotating a spoke about the wrong axis projects it, so the radius shrinks — which catches
     the defect even where the wrong axis happens to share a world axis with the right one.
  4. The plane-less overloads still assume the ground exactly as they did, and agree vertex for
     vertex with the plane-aware path when the plane handed in IS the ground — so nothing that
     already worked has moved.

Negative-tested: restoring the hardcoded `{0,1,0}` normal in either `ResolvePlacementCurveInPlane`
or `AppendPolygonSpans`, or routing the basis-aware plural back to the plane-less one, refutes
sections 1 to 3. A gate that has never been seen to fail proves nothing.
"""

import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

SOURCES = [
    ROOT / "Tools/WorkplaneDrawingProof/WorkplaneDrawingProof.cpp",
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
            print(f"WorkplaneDrawingProof: missing {Source.relative_to(ROOT)}")
            raise SystemExit(1)

    with tempfile.TemporaryDirectory() as Scratch:
        Binary = pathlib.Path(Scratch) / "WorkplaneDrawingProof"

        Command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror"]
        for Include in INCLUDES:
            Command += ["-I", str(Include)]
        Command += [str(Source) for Source in SOURCES]
        Command += ["-o", str(Binary)]

        Built = subprocess.run(Command, capture_output=True, text=True)
        if Built.returncode != 0:
            print("WorkplaneDrawingProof: the placement geometry does not compile clean")
            print(Built.stderr.strip()[:4000])
            raise SystemExit(1)

        Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
        sys.stdout.write(Ran.stdout)
        if Ran.stderr.strip():
            sys.stderr.write(Ran.stderr)

        if Ran.returncode != 0:
            print("WorkplaneDrawingProof: REFUTED")
            raise SystemExit(1)

    print("WorkplaneDrawingProof: every shape stands in the plane it was drawn on")
    raise SystemExit(0)


if __name__ == "__main__":
    Main()
