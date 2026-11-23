#!/usr/bin/env python3
"""ViewportProjectionProof — the viewport projection lifted at step 10e.

Usage: python3 Tools/ViewportProjectionProof/ViewportProjectionProof.py

Where a point on the sketch plane lands on screen, and which point a screen position names.

A projection can only be checked properly against its own inverse. Screen coordinates are
meaningless on their own -- no reader can say whether 412.7 is the right pixel -- but "project
this point, unproject the result, get the point back" is checkable without knowing the formula,
and it fails the moment either direction disagrees with the other.

THAT ROUND TRIP FOUND TWO SHIPPED DEFECTS, both of which made the viewport unable to place a
point where the artist clicked:

  1. The ray-plane distance was negated. Difference(A, B) returns the direction FROM A TO B, so
     it already points from the eye towards the plane; the extra minus inverted every distance
     and a plane 240 units in FRONT was refused as being behind the viewer. Every perspective
     view was affected. It survived because the orthographic arm builds its ray origin from the
     focus, which lies ON the sketch plane, so the numerator is zero and the sign is invisible.

  2. An orthographic view was refused on a negative distance, but only a perspective view has an
     eye for something to be behind. A parallel projection's ray runs both ways. This rejected
     half the isometric orthographic viewport -- and was invisible in the six axis-aligned views
     for the same reason as above.

Both are pinned by section 6 so they cannot come back.
"""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [
    ROOT / "Tools/ViewportProjectionProof/ViewportProjectionProof.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/ViewportProjection/Source/ViewportProjection.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/OrientationCube/Source/OrientationStanding.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/ViewportProjection/Source/CadProjection.cpp",
]

for Source in SOURCES:
    if not Source.exists():
        print(f"ViewportProjectionProof: missing {Source.relative_to(ROOT)}")
        raise SystemExit(1)

with tempfile.TemporaryDirectory() as Scratch:
    Binary = Path(Scratch) / "ViewportProjectionProof"
    Command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
               "-I", str(ROOT), "-I", str(ROOT / "Engine"), "-I", str(ROOT / "Tools/VulkanParseStub")]
    Command += [str(Source) for Source in SOURCES] + ["-o", str(Binary)]

    Built = subprocess.run(Command, capture_output=True, text=True)
    if Built.returncode != 0:
        print("ViewportProjectionProof: does not compile")
        print(Built.stderr.strip()[:3000])
        raise SystemExit(1)

    Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
    sys.stdout.write(Ran.stdout)
    raise SystemExit(0 if Ran.returncode == 0 else 1)
