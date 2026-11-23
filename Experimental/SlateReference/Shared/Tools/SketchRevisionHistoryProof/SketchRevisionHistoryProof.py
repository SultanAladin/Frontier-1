#!/usr/bin/env python3
"""Builds and runs the workspace revision-history proof."""

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

SOURCES = [
    "Tools/SketchRevisionHistoryProof/SketchRevisionHistoryProof.cpp",
    "Engine/SlateWorkspace/Discipline/SketchRevisionHistory/Source/SketchRevisionHistory.cpp",
    "Engine/SlateWorkspace/Discipline/WorkplaneCatalogue/Source/WorkplaneCatalogue.cpp",
    "Engine/SlateWorkspace/Discipline/WorkplaneStanding/Source/WorkplaneStanding.cpp",
    "Engine/SlateShape/World/WorldSketchStructure/Source/WorldSketchStructure.cpp",
    "Engine/SlateShape/Sketch/SketchStructure/Source/SketchStructure.cpp",
    "Engine/SlateShape/Geometry/CurveSpecification/Source/CurveSpecification.cpp",
    "Engine/SlateShape/Geometry/ProfileSpecification/Source/ProfileSpecification.cpp",
    "Engine/SlateShape/Sketch/ConstraintSpecification/Source/ConstraintSpecification.cpp",
    "Engine/SlateShape/Sketch/DimensionSpecification/Source/DimensionSpecification.cpp",
    "Engine/SlateShape/Sketch/SketchPolyline/Source/SketchPolyline.cpp",
    "Engine/SlateShape/Record/WorkspaceNameIndex/Source/WorkspaceNameIndex.cpp",
    "Engine/SlateShape/Record/WorkspaceRecordStructure/Source/WorkspaceRecordStructure.cpp",
    "Engine/SlateShape/Record/WorkspaceRevisionSequence/Source/WorkspaceRevisionSequence.cpp",
]

INCLUDES = ["-I", ".", "-I", "Engine", "-I", "Tools/VulkanParseStub"]


def main() -> int:
    objects = []
    for index, source in enumerate(SOURCES):
        object_path = ROOT / "Tools" / "SketchRevisionHistoryProof" / f"revision_{index}.o"
        command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-ffunction-sections", "-fdata-sections",
                   "-c", source, "-o", str(object_path)] + INCLUDES
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            print(result.stderr[:4000])
            print(f"SketchRevisionHistoryProof: {source} does not compile")
            for path in objects:
                pathlib.Path(path).unlink(missing_ok=True)
            return 1
        objects.append(str(object_path))

    binary = ROOT / "Tools" / "SketchRevisionHistoryProof" / "SketchRevisionHistoryProof"
    link = subprocess.run(["g++", "-Wl,--gc-sections", "-o", str(binary)] + objects,
                          cwd=ROOT, capture_output=True, text=True)
    if link.returncode:
        print(link.stderr[:4000])
        return 1

    run = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
    print(run.stdout)
    if run.returncode:
        print(run.stderr[:2000])

    for path in objects:
        pathlib.Path(path).unlink(missing_ok=True)
    binary.unlink(missing_ok=True)
    return run.returncode


if __name__ == "__main__":
    sys.exit(main())
