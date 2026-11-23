#!/usr/bin/env python3
"""Builds and runs the world-sketch boolean proof."""

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

OWNED = [
    "Tools/WorldSketchBooleanProof/WorldSketchBooleanProof.cpp",
    "Engine/SlateShape/World/WorldSketchBoolean/Source/WorldSketchBoolean.cpp",
]

SUPPORTING = [
    "Engine/SlateShape/World/WorldSketchStructure/Source/WorldSketchStructure.cpp",
    "Engine/SlateShape/World/WorldSketchAnalysis/Source/WorldSketchAnalysis.cpp",
    "Engine/SlateShape/Geometry/CurveSpecification/Source/CurveSpecification.cpp",
    "Engine/SlateShape/Geometry/ProfileSpecification/Source/ProfileSpecification.cpp",
    "Engine/SlateShape/Sketch/SketchPolyline/Source/SketchPolyline.cpp",
]

CLIPPER = ROOT / "ExternalPackages/clipper2/CPP/Clipper2Lib/include"
CLIPPER_SRC = ROOT / "ExternalPackages/clipper2/CPP/Clipper2Lib/src"

# 🔴 Clipper2's engine, offset and rectclip translation units carry the symbols the header calls into, so
#    they are linked here exactly as the real SlateShape static library links every .cpp under it.
CLIPPER_SOURCES = [
    "ExternalPackages/clipper2/CPP/Clipper2Lib/src/clipper.engine.cpp",
    "ExternalPackages/clipper2/CPP/Clipper2Lib/src/clipper.offset.cpp",
    "ExternalPackages/clipper2/CPP/Clipper2Lib/src/clipper.rectclip.cpp",
] if CLIPPER_SRC.exists() else []

INCLUDES = ["-I", ".", "-I", "Engine", "-I", "Tools/VulkanParseStub"]
if CLIPPER.exists():
    INCLUDES += ["-I", str(CLIPPER)]


def compile_source(source: str, strict: bool, objects: list[str]) -> bool:
    obj = ROOT / "Tools" / "WorldSketchBooleanProof" / (pathlib.Path(source).stem + ".o")
    command = ["g++", "-std=c++20", "-ffunction-sections", "-fdata-sections",
               "-c", source, "-o", str(obj)] + INCLUDES
    command += ["-Wall", "-Wextra", "-Werror"] if strict else ["-w"]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr[:4000])
        return False
    objects.append(str(obj))
    return True


def main() -> int:
    if not CLIPPER.exists():
        print("[WorldSketchBooleanProof] Clipper2 headers are not vendored; "
              "the boolean backend cannot be exercised in this sandbox.")
        print("  git clone --depth 1 https://github.com/AngusJohnson/Clipper2.git "
              "ExternalPackages/clipper2")

    objects: list[str] = []
    for source in OWNED:
        if not compile_source(source, True, objects):
            print("FAILED to compile " + source)
            return 1
    for source in SUPPORTING + CLIPPER_SOURCES:
        if not compile_source(source, False, objects):
            print("FAILED to compile " + source)
            return 1

    binary = ROOT / "Tools" / "WorldSketchBooleanProof" / "WorldSketchBooleanProof"
    binary.unlink(missing_ok=True)
    link = subprocess.run(["g++", "-Wl,--gc-sections", "-o", str(binary)] + objects,
                          cwd=ROOT, capture_output=True, text=True)
    if link.returncode != 0:
        print(link.stderr[:4000])
        return 1

    run = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
    print(run.stdout)
    if run.returncode != 0:
        print(run.stderr[:2000])

    for obj in objects:
        pathlib.Path(obj).unlink(missing_ok=True)
    binary.unlink(missing_ok=True)
    return run.returncode


if __name__ == "__main__":
    sys.exit(main())
