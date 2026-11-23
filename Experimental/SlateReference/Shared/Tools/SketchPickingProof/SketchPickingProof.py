#!/usr/bin/env python3
"""SketchPickingProof — what the artist just pointed at, lifted at step 10h.

Usage: python3 Tools/SketchPickingProof/SketchPickingProof.py

THE PRIORITY IS THE WHOLE DESIGN. A point, a spline control and a curve can all be within reach of
one cursor position, and picking the nearest of the three is WRONG: a curve passes through every one
of its own endpoints, so at an endpoint both are at distance zero and a distance comparison could
hand back the curve. The artist would never be able to grab the end they were aiming at. The order
is POINT, then CONTROL, then CURVE — smallest target first — each answered at its own full tolerance.

THE PROOF CORRECTED ME ON DEDUPLICATION. I claimed two lines meeting at a corner share that corner
and it must be collected once. Wrong: each curve names its OWN endpoints, so the corner is point 258
of one line and point 513 of the other. They are coincident but genuinely distinct subjects, and
both must move — otherwise dragging one line would silently drag its neighbour's end with it. The
uniqueness guard exists for a subject reachable by two ROUTES: a curve collected directly and again
as part of a profile that contains it. That is what section 4 now proves.

ONE GUARD WAS ADDED, NOT TRANSCRIBED. The shipped ResolveRecordForPoint and ResolveRecordForCurve
matched on IssuedIndex with no check that the incoming name was assigned. An unassigned name is
index zero, and every record carrying no point also has zero, so an unassigned name resolved to the
first folder or dimension in the directory — handing back a record as the "owner" of a thing that
does not exist. Both now refuse, and the refusal is pinned from both sides.

An eleventh duplicate turned up while rewiring: the host's ResolvePlanarCoordinates was
character-for-character the projection unit's ResolvePlaneCoordinates, differing only by returning
an unconditional true that no caller could use.

Seven sabotages, all caught, including inverting the pick priority.
"""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Built with -Werror: a warning in these is mine.
OWNED = [ROOT / Relative for Relative in ['Tools/SketchPickingProof/SketchPickingProof.cpp', 'Engine/SlateWorkspace/Discipline/SketchPicking/Source/SketchPicking.cpp']]

# The sketch kernel this links against. Built without -Werror: not under test, and some carry
# warnings that predate this work.
SUPPORTING = [ROOT / Relative for Relative in ['Engine/SlateShape/Sketch/SketchSelection/Source/SketchSelection.cpp', 'Engine/SlateShape/Sketch/ConstraintSolver/Source/ConstraintSolver.cpp', 'Engine/SlateShape/Sketch/DimensionSolver/Source/DimensionSolver.cpp', 'Engine/SlateShape/Sketch/SketchAnalysis/Source/SketchAnalysis.cpp', 'Engine/SlateShape/Sketch/ProfileArea/Source/ProfileArea.cpp', 'Engine/SlateShape/Sketch/ProfileBoolean/Source/ProfileBoolean.cpp', 'Engine/SlateShape/Sketch/ProfileReshape/Source/ProfileReshape.cpp', 'Engine/SlateShape/Sketch/SketchPolyline/Source/SketchPolyline.cpp', 'Engine/SlateShape/Sketch/ConstraintSpecification/Source/ConstraintSpecification.cpp', 'Engine/SlateShape/Geometry/CurveSpecification/Source/CurveSpecification.cpp', 'Engine/SlateShape/Sketch/DimensionSpecification/Source/DimensionSpecification.cpp', 'Engine/SlateShape/Sketch/SketchEditing/Source/SketchEditing.cpp', 'Engine/SlateShape/Geometry/ProfileSpecification/Source/ProfileSpecification.cpp', 'Engine/SlateShape/Reference/PickClassifier/Source/PickClassifier.cpp', 'Engine/SlateShape/Reference/ReferenceSpecification/Source/ReferenceSpecification.cpp', 'Engine/SlateShape/Sketch/ProfileClosure/Source/ProfileClosure.cpp', 'Engine/SlateShape/Sketch/ProfilePattern/Source/ProfilePattern.cpp', 'Engine/SlateShape/Sketch/ProfileSolver/Source/ProfileSolver.cpp', 'Engine/SlateShape/Sketch/SketchCreation/Source/SketchCreation.cpp', 'Engine/SlateShape/Sketch/SketchRenderingProjection/Source/SketchRenderingProjection.cpp', 'Engine/SlateShape/Sketch/SketchSnap/Source/SketchSnap.cpp', 'Engine/SlateShape/Sketch/SketchSolve/Source/SketchSolve.cpp', 'Engine/SlateShape/Sketch/SketchStructure/Source/SketchStructure.cpp', 'Engine/SlateShape/Record/WorkspaceDirectoryProjection/Source/WorkspaceDirectoryProjection.cpp', 'Engine/SlateShape/Record/WorkspaceRecordStructure/Source/WorkspaceRecordStructure.cpp']]

for Source in OWNED + SUPPORTING:
    if not Source.exists():
        print(f"SketchPickingProof: missing {Source.relative_to(ROOT)}")
        raise SystemExit(1)

Include = ["-I", str(ROOT), "-I", str(ROOT / "Engine"), "-I", str(ROOT / "Tools/VulkanParseStub")]

with tempfile.TemporaryDirectory() as Scratch:
    Objects = []
    for Source, Owned in [(S, True) for S in OWNED] + [(S, False) for S in SUPPORTING]:
        Object = Path(Scratch) / (Source.stem + ".o")
        Warnings = ["-Wall", "-Wextra", "-Werror"] if Owned else ["-w"]
        Compiled = subprocess.run(
            ["g++", "-std=c++20", *Warnings, *Include, "-c", str(Source), "-o", str(Object)],
            capture_output=True, text=True)
        if Compiled.returncode != 0:
            print(f"SketchPickingProof: {Source.relative_to(ROOT)} does not compile")
            print(Compiled.stderr.strip()[:3000])
            raise SystemExit(1)
        Objects.append(str(Object))

    Binary = Path(Scratch) / "SketchPickingProof"
    Linked = subprocess.run(["g++", *Objects, "-o", str(Binary)], capture_output=True, text=True)
    if Linked.returncode != 0:
        print("SketchPickingProof: does not link")
        print(Linked.stderr.strip()[:3000])
        raise SystemExit(1)

    Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
    sys.stdout.write(Ran.stdout)
    raise SystemExit(0 if Ran.returncode == 0 else 1)
