#!/usr/bin/env python3
"""Builds and runs the sketch-snap proof.

The unit and its proof are compiled with every warning as an error; the engine translation units they
link against are compiled quietly, because this gate answers for what it owns and not for the rest of
the tree.
"""

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

OWNED = [
    "Tools/SketchSnapProof/SketchSnapProof.cpp",
    "Engine/SlateShape/Sketch/SketchSnap/Source/SketchSnap.cpp",
]

SUPPORTING = [
    "Engine/SlateShape/Geometry/CurveSpecification/Source/CurveSpecification.cpp",
    "Engine/SlateShape/Geometry/ProfileSpecification/Source/ProfileSpecification.cpp",
    "Engine/SlateShape/Reference/ReferenceSpecification/Source/ReferenceSpecification.cpp",
    "Engine/SlateShape/Sketch/ConstraintSpecification/Source/ConstraintSpecification.cpp",
    "Engine/SlateShape/Sketch/DimensionSpecification/Source/DimensionSpecification.cpp",
    "Engine/SlateShape/Sketch/SketchPolyline/Source/SketchPolyline.cpp",
    "Engine/SlateShape/Sketch/SketchSelection/Source/SketchSelection.cpp",
    "Engine/SlateShape/Sketch/SketchStructure/Source/SketchStructure.cpp",
]

INCLUDES = ["-I", ".", "-I", "Engine", "-I", "Tools/VulkanParseStub"]


def Compile(Source, Strict, Objects):
    Object = ROOT / "Tools" / "SketchSnapProof" / (pathlib.Path(Source).stem + ".o")
    Command = ["g++", "-std=c++20", "-c", Source, "-o", str(Object)] + INCLUDES
    Command += ["-Wall", "-Wextra", "-Werror"] if Strict else ["-w"]
    Result = subprocess.run(Command, cwd=ROOT, capture_output=True, text=True)
    if Result.returncode != 0:
        print(Result.stderr[:4000])
        return False
    Objects.append(str(Object))
    return True


def Main():
    Objects = []
    for Source in OWNED:
        if not Compile(Source, True, Objects):
            print("FAILED to compile " + Source)
            return 1
    for Source in SUPPORTING:
        if not Compile(Source, False, Objects):
            print("FAILED to compile " + Source)
            return 1

    Binary = ROOT / "Tools" / "SketchSnapProof" / "SketchSnapProof"
    Binary.unlink(missing_ok=True)
    Link = subprocess.run(["g++", "-o", str(Binary)] + Objects, cwd=ROOT,
                          capture_output=True, text=True)
    if Link.returncode != 0:
        print(Link.stderr[:4000])
        return 1

    Run = subprocess.run([str(Binary)], cwd=ROOT, capture_output=True, text=True)
    print(Run.stdout)
    if Run.returncode != 0:
        print(Run.stderr[:2000])

    for Object in Objects:
        pathlib.Path(Object).unlink(missing_ok=True)
    Binary.unlink(missing_ok=True)
    return Run.returncode


if __name__ == "__main__":
    sys.exit(Main())
