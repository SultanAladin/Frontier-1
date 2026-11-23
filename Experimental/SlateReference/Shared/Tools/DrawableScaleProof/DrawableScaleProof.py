#!/usr/bin/env python3
"""DrawableScaleProof — the two kinds of pixel, and the defect that mixed them.

Usage: python3 Tools/DrawableScaleProof/DrawableScaleProof.py

A viewport lives in two units. LOGICAL POINTS are what ImGui reports — io.MousePos, every
PlaneExtent that comes back from a panel — and are the space the artist points in. PHYSICAL PIXELS
are what the swapchain is made of, and are the space the image is drawn in. On an unscaled display
the two are the same number and every confusion between them is invisible.

ResolveCadProjection built its screen mapping from a PlaneExtent in LOGICAL points and handed the
shader a DisplayWidth in PHYSICAL pixels. The shader divides one by the other to reach clip space,
so at any display scaling other than 100% the drawn geometry is out by exactly the scale factor. At
150% a point the picker placed at x=1200 was drawn at x=800 — four hundred points from the cursor
that placed it. THE ARTIST SEES GEOMETRY APPEAR SOMEWHERE OTHER THAN WHERE THEY CLICKED.

The scissor had the same mismatch from the other side: a logical rectangle clamped against a
physical width. The clamp never fires, so it silently leaves logical numbers in a physical field and
clips the wrong region of the image.

Section 3 replays the shipped arithmetic all the way through the shader and asserts the error, then
asserts that converting the extent first removes it at 100%, 125%, 150%, 200% and 300%.
"""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [
    ROOT / "Tools/DrawableScaleProof/DrawableScaleProof.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/ViewportProjection/Source/ViewportProjection.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/OrientationCube/Source/OrientationStanding.cpp",
]

for Source in SOURCES:
    if not Source.exists():
        print(f"DrawableScaleProof: missing {Source.relative_to(ROOT)}")
        raise SystemExit(1)

with tempfile.TemporaryDirectory() as Scratch:
    Binary = Path(Scratch) / "DrawableScaleProof"
    Command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
               "-I", str(ROOT), "-I", str(ROOT / "Engine"), "-I", str(ROOT / "Tools/VulkanParseStub")]
    Command += [str(Source) for Source in SOURCES] + ["-o", str(Binary)]

    Built = subprocess.run(Command, capture_output=True, text=True)
    if Built.returncode != 0:
        print("DrawableScaleProof: does not compile")
        print(Built.stderr.strip()[:3000])
        raise SystemExit(1)

    Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
    sys.stdout.write(Ran.stdout)
    raise SystemExit(0 if Ran.returncode == 0 else 1)
