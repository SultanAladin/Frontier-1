#!/usr/bin/env python3
"""WorkplaneStandingProof — the surface a sketch is drawn on.

Usage: python3 Tools/WorkplaneStandingProof/WorkplaneStandingProof.py

A workplane is an origin plus an orientation. That is the same thing as putting an empty somewhere
and drawing on the grid through it, which is not a workaround — it IS what a workplane is. This unit
gives the pairing a name, the three planes the world always has, offsets, and a plane named by
pointing at the viewport.

A SKETCH WITHOUT A WORKPLANE STILL DRAWS. The ground plane through the world origin is the standing
default, so the artist can start immediately; demanding a declared plane before anything can be
tried is the single most common complaint about parametric sketchers, and section 1 pins that the
default is declared and is the plane the grid lies on.

A PLACED PLANE FACES THE VIEWER, which is what makes drawing in screen space work: a plane seen
edge-on projects to a line and anything drawn on it lands nowhere near where it was drawn. Choosing
which direction is "along" matters too — the world axis that survives projection best, so the grid
does not arrive rolled at an arbitrary angle and does not swing wildly as the view turns.

NEGATIVE TESTING FOUND A HOLE IN THE PROOF. Seven sabotages, and the first six were caught. The
survivor reversed the cross product deriving Across, and all 286 claims stayed green — because a
round trip uses the same flipped axis in both directions and cannot see the flip, even though the
sketch would be MIRRORED against the world. Handedness has to be asserted against a direction
written out by hand, not against the implementation's own expression. With that added the sabotage
is caught, and the ground plane's across direction is pinned concretely as world -Z.
"""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [
    ROOT / "Tools/WorkplaneStandingProof/WorkplaneStandingProof.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/WorkplaneStanding/Source/WorkplaneStanding.cpp",
]

for Source in SOURCES:
    if not Source.exists():
        print(f"WorkplaneStandingProof: missing {Source.relative_to(ROOT)}")
        raise SystemExit(1)

with tempfile.TemporaryDirectory() as Scratch:
    Binary = Path(Scratch) / "WorkplaneStandingProof"
    Command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
               "-I", str(ROOT), "-I", str(ROOT / "Engine"), "-I", str(ROOT / "Tools/VulkanParseStub")]
    Command += [str(Source) for Source in SOURCES] + ["-o", str(Binary)]

    Built = subprocess.run(Command, capture_output=True, text=True)
    if Built.returncode != 0:
        print("WorkplaneStandingProof: does not compile")
        print(Built.stderr.strip()[:3000])
        raise SystemExit(1)

    Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
    sys.stdout.write(Ran.stdout)
    raise SystemExit(0 if Ran.returncode == 0 else 1)
