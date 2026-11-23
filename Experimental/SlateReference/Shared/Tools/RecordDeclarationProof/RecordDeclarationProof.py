#!/usr/bin/env python3
"""RecordDeclarationProof — writing a sketched thing into the workspace directory, lifted at step 10f.

Usage: python3 Tools/RecordDeclarationProof/RecordDeclarationProof.py

Five near-identical functions in ParametricSketchHost differed in exactly three places: the subject
the record carries, the category folder it files under, and the payload member that gets set.
Everything else — the folder lookup and the naming — was written out five times. They are now one
unit, SlateWorkspace/Discipline/RecordDeclaration, and these are the claims that were owed.

WHAT THE PROOF CORRECTED WAS MY OWN UNDERSTANDING, TWICE.

First, the bench declared no sketch plane, so every area query REFUSED rather than returning an
empty list, and seven claims failed for a reason that had nothing to do with what they were
testing. A SketchStructure is not Declared() until a plane is standing.

Second, and more usefully: I asserted that a square standing upright in Left/Up would declare no
profile, reasoning that area is measured on the Left/Forward ground plane and an upright square is
edge-on to it. That is wrong. CLOSURE IS TOPOLOGICAL, NOT AREAL — curves chain end to end and the
loop is closed when the chain returns to its start. Signed area is computed afterwards, and is used
to sort loops and decide which are holes, not to decide whether a loop exists at all. The upright
square declares a profile with zero area. The claim now pins the real behaviour.

The same round corrected a comment I had just written: the 0.05 handed to AutoDeclareClosedAreaProfiles
is a CLOSURE tolerance — the largest gap two curve ends may leave and still be treated as joined —
not, as I had documented it, a minimum area. Two claims pin it from both sides: a square left 0.03
units open closes, one left 0.5 units open does not.

The claim worth keeping is the undo cost. Closing a rectangle is ONE action and seals ONE revision
listing every profile it wrote, however many areas the closure enclosed. Sealing one revision per
profile would make the artist press undo four times to walk back one action.
"""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# The proof and the unit under test: built with -Werror, because a warning here is mine.
OWNED = [
    ROOT / "Tools/RecordDeclarationProof/RecordDeclarationProof.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/RecordDeclaration/Source/RecordDeclaration.cpp",
]

# The sketch kernel this links against. Built without -Werror: these files are not under test and
# some carry warnings that predate this work (ProfileArea has a dead Cross2, ProfileReshape sets an
# EndDirection it never reads). Turning those into errors here would block a proof about something
# else entirely.
SUPPORTING = [ROOT / Relative for Relative in [
    "Engine/SlateShape/Sketch/ProfileArea/Source/ProfileArea.cpp",
    "Engine/SlateShape/Sketch/ConstraintSolver/Source/ConstraintSolver.cpp",
    "Engine/SlateShape/Sketch/DimensionSolver/Source/DimensionSolver.cpp",
    "Engine/SlateShape/Sketch/SketchAnalysis/Source/SketchAnalysis.cpp",
    "Engine/SlateShape/Sketch/ProfileBoolean/Source/ProfileBoolean.cpp",
    "Engine/SlateShape/Sketch/ProfileReshape/Source/ProfileReshape.cpp",
    "Engine/SlateShape/Sketch/SketchPolyline/Source/SketchPolyline.cpp",
    "Engine/SlateShape/Sketch/ConstraintSpecification/Source/ConstraintSpecification.cpp",
    "Engine/SlateShape/Geometry/CurveSpecification/Source/CurveSpecification.cpp",
    "Engine/SlateShape/Sketch/DimensionSpecification/Source/DimensionSpecification.cpp",
    "Engine/SlateShape/Sketch/SketchEditing/Source/SketchEditing.cpp",
    "Engine/SlateShape/Geometry/ProfileSpecification/Source/ProfileSpecification.cpp",
    "Engine/SlateShape/Reference/PickClassifier/Source/PickClassifier.cpp",
    "Engine/SlateShape/Reference/ReferenceSpecification/Source/ReferenceSpecification.cpp",
    "Engine/SlateShape/Sketch/SketchSelection/Source/SketchSelection.cpp",
    "Engine/SlateShape/Sketch/ProfileClosure/Source/ProfileClosure.cpp",
    "Engine/SlateShape/Sketch/ProfilePattern/Source/ProfilePattern.cpp",
    "Engine/SlateShape/Sketch/ProfileSolver/Source/ProfileSolver.cpp",
    "Engine/SlateShape/Sketch/SketchCreation/Source/SketchCreation.cpp",
    "Engine/SlateShape/Sketch/SketchRenderingProjection/Source/SketchRenderingProjection.cpp",
    "Engine/SlateShape/Sketch/SketchSnap/Source/SketchSnap.cpp",
    "Engine/SlateShape/Sketch/SketchSolve/Source/SketchSolve.cpp",
    "Engine/SlateShape/Sketch/SketchStructure/Source/SketchStructure.cpp",
    "Engine/SlateShape/Record/WorkspaceNameIndex/Source/WorkspaceNameIndex.cpp",
    "Engine/SlateShape/Record/WorkspaceDirectoryProjection/Source/WorkspaceDirectoryProjection.cpp",
    "Engine/SlateShape/Record/WorkspaceRecordStructure/Source/WorkspaceRecordStructure.cpp",
    "Engine/SlateShape/Record/WorkspacePropertyProjection/Source/WorkspacePropertyProjection.cpp",
    "Engine/SlateShape/Record/WorkspaceRevisionSequence/Source/WorkspaceRevisionSequence.cpp",
]]

for Source in OWNED + SUPPORTING:
    if not Source.exists():
        print(f"RecordDeclarationProof: missing {Source.relative_to(ROOT)}")
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
            print(f"RecordDeclarationProof: {Source.relative_to(ROOT)} does not compile")
            print(Compiled.stderr.strip()[:3000])
            raise SystemExit(1)
        Objects.append(str(Object))

    Binary = Path(Scratch) / "RecordDeclarationProof"
    Linked = subprocess.run(["g++", *Objects, "-o", str(Binary)], capture_output=True, text=True)
    if Linked.returncode != 0:
        print("RecordDeclarationProof: does not link")
        print(Linked.stderr.strip()[:3000])
        raise SystemExit(1)

    Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
    sys.stdout.write(Ran.stdout)
    raise SystemExit(0 if Ran.returncode == 0 else 1)
