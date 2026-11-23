#!/usr/bin/env python3
"""AnnotationProof — compiles, links and RUNS dimensions and constraints.

Usage: python3 Tools/AnnotationProof/AnnotationProof.py

Executes engine code rather than parsing it. What it proves, from `AnnotationProof.cpp`:

  1. A dimension stores NO coordinates, proven the only way that counts — by rewriting the geometry
     underneath it and re-asking. Nothing notifies the dimension; if it reports anything but the new
     length, it cached, and every drawing will eventually lie.
  2. The offset's SIGN is the side it draws on. Probes either side of an edge must return opposite
     signs and land the figure on opposite sides. Taking a magnitude passes every distance test ever
     written and still welds the dimension to one side forever.
  3. Linear, diameter and radial measure and draw differently: a diameter is exactly twice its radius
     and spans rim to rim THROUGH the centre, where a radius starts AT it.
  4. Horizontal and vertical measure a PROJECTION, not the span — proven on a 3-4-5 slope where the
     three subjects must read 40, 30 and 50 rather than three copies of the same number.
  5. A typed value goes through the solver, and a refusal leaves the drawing byte-for-byte unchanged.
     A dimension is born at its measured value, so creating one never moves anything; only typing does.
  6. Units convert at the edges and never reach the model. All four round-trip exactly, and composing
     labels in every unit leaves the stored millimetres untouched.
  7. All fourteen catalogue tiles resolve to distinct intents. The band existed with no band listing it
     AND no `ToolSubjectOf` case, so every tile reported `Select`; both halves are covered. The three
     unbuilt constraints declare themselves unsupported rather than applying the nearest thing.
  8. Each tool demands exactly the picks it needs — one for an edge, two for two points — and a
     constraint commits on its last pick with no readout.

Negative-tested: taking the offset's magnitude, caching a dimension's measured span, writing a typed
value straight into the target, and converting units on the way in as well as out each refute a section
above. A gate that has never been seen to fail proves nothing.
"""

import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

SOURCES = [
    ROOT / "Tools/AnnotationProof/AnnotationProof.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/AnnotationIntent/Source/AnnotationIntent.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/AnnotationSession/Source/AnnotationSession.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/WorldSketchConstraintAuthoring/Source/WorldSketchConstraintAuthoring.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/WorldSketchDimensionAuthoring/Source/WorldSketchDimensionAuthoring.cpp",
    ROOT / "Engine/SlateShape/World/WorldSketchDimensionGeometry/Source/WorldSketchDimensionGeometry.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/WorldSketchDimensionProjection/Source/WorldSketchDimensionProjection.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/ViewportProjection/Source/ViewportProjection.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/OrientationCube/Source/OrientationStanding.cpp",
    ROOT / "Engine/SlateShape/World/WorldSketchDimensionSolver/Source/WorldSketchDimensionSolver.cpp",
    ROOT / "Engine/SlateShape/World/WorldSketchAnnotationPriority/Source/WorldSketchAnnotationPriority.cpp",
    ROOT / "Engine/SlateShape/World/WorldSketchConstraintSolver/Source/WorldSketchConstraintSolver.cpp",
    ROOT / "Engine/SlateShape/World/WorldSketchPicking/Source/WorldSketchPicking.cpp",
    ROOT / "Engine/SlateShape/World/WorldSketchEditing/Source/WorldSketchEditing.cpp",
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
            print(f"AnnotationProof: missing {Source.relative_to(ROOT)}")
            raise SystemExit(1)

    with tempfile.TemporaryDirectory() as Scratch:
        Binary = pathlib.Path(Scratch) / "AnnotationProof"

        Command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror"]
        for Include in INCLUDES:
            Command += ["-I", str(Include)]
        Command += [str(Source) for Source in SOURCES]
        Command += ["-o", str(Binary)]

        Built = subprocess.run(Command, capture_output=True, text=True)
        if Built.returncode != 0:
            print("AnnotationProof: the annotation code does not compile clean")
            print(Built.stderr.strip()[:4000])
            raise SystemExit(1)

        Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
        sys.stdout.write(Ran.stdout)
        if Ran.stderr.strip():
            sys.stderr.write(Ran.stderr)

        if Ran.returncode != 0:
            print("AnnotationProof: REFUTED")
            raise SystemExit(1)

    print("AnnotationProof: dimensions and constraints stand")
    raise SystemExit(0)


if __name__ == "__main__":
    Main()
