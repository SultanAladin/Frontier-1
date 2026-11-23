#!/usr/bin/env python3
"""TransformSequenceProof — the transform keyboard grammar lifted at step 10d.

Usage: python3 Tools/TransformSequenceProof/TransformSequenceProof.py

G, R and S start a move, rotate or scale; X and Z restrict it to an axis; digits type an exact
amount; backspace walks back out one step at a time; a second G slides along the curve.

This grammar shipped for the whole life of ParametricSketchHost without a single test, because
it was nine file-local functions interleaved with viewport drawing and could not be reached
without a Vulkan device. Lifting it into SlateWorkspace/Discipline/TransformSequence made it
testable; these are the claims that were owed.

Where the shipped behaviour is surprising it is PINNED rather than corrected — a minus sign is
taken mid-run, two G in one frame count as a double tap, and the reader sees characters rather
than words so a stray 'r' in ordinary text starts a rotate. Each is a claim carrying its reason,
so a later reader changes it deliberately or not at all.
"""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [
    ROOT / "Tools/TransformSequenceProof/TransformSequenceProof.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/TransformSequence/Source/TransformSequence.cpp",
]

for Source in SOURCES:
    if not Source.exists():
        print(f"TransformSequenceProof: missing {Source.relative_to(ROOT)}")
        raise SystemExit(1)

with tempfile.TemporaryDirectory() as Scratch:
    Binary = Path(Scratch) / "TransformSequenceProof"
    Command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
               "-I", str(ROOT), "-I", str(ROOT / "Engine"), "-I", str(ROOT / "Tools/VulkanParseStub")]
    Command += [str(Source) for Source in SOURCES] + ["-o", str(Binary)]

    Built = subprocess.run(Command, capture_output=True, text=True)
    if Built.returncode != 0:
        print("TransformSequenceProof: does not compile")
        print(Built.stderr.strip()[:3000])
        raise SystemExit(1)

    Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
    sys.stdout.write(Ran.stdout)
    raise SystemExit(0 if Ran.returncode == 0 else 1)
