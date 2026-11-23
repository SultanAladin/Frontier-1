#!/usr/bin/env python3
"""Builds and runs the codex unit-scale boundary proof.

The unit and its proof are compiled with every warning as an error; the engine translation units they
link against are compiled quietly, because this gate answers for what it owns and not for the rest of
the tree.
"""

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

OWNED = [
    "Tools/SketchDurabilityProof/SketchDurabilityProof.cpp",
    "Engine/SlateShape/Sketch/SketchRenderingProjection/Source/SketchRenderingProjection.cpp",
    "Engine/SlateShape/Sketch/SketchStructure/Source/SketchStructure.cpp",
]

SUPPORTING = [
    "Engine/SlateWorkspace/Discipline/SketchInteraction/Source/SketchInteraction.cpp",
    "Engine/SlateWorkspace/Discipline/WorkplaneCatalogue/Source/WorkplaneCatalogue.cpp",
    "Engine/SlateWorkspace/Discipline/WorkplaneStanding/Source/WorkplaneStanding.cpp",
    "Engine/SlateWorkspace/Discipline/RecordDeclaration/Source/RecordDeclaration.cpp",
    "Engine/SlateWorkspace/Discipline/PlacementCommit/Source/PlacementCommit.cpp",
    "Engine/SlateWorkspace/Discipline/ViewportProjection/Source/CadProjection.cpp",
    "Engine/SlateWorkspace/Discipline/ViewportProjection/Source/SketchBasis.cpp",
    "Engine/SlateWorkspace/Discipline/ViewportProjection/Source/ViewportProjection.cpp",
    "Engine/SlateWorkspace/Discipline/OrientationCube/Source/OrientationStanding.cpp",
    "Engine/SketchToolset/SketchTool/SketchPlacement/Source/SketchPlacement.cpp",
    "Engine/SlateShape/Sketch/ConstraintSolver/Source/ConstraintSolver.cpp",
    "Engine/SlateShape/Sketch/ConstraintSpecification/Source/ConstraintSpecification.cpp",
    "Engine/SlateShape/Sketch/DimensionSolver/Source/DimensionSolver.cpp",
    "Engine/SlateShape/Sketch/DimensionSpecification/Source/DimensionSpecification.cpp",
    "Engine/SlateShape/Sketch/ProfileArea/Source/ProfileArea.cpp",
    "Engine/SlateShape/Sketch/ProfileBoolean/Source/ProfileBoolean.cpp",
    "Engine/SlateShape/Sketch/ProfileClosure/Source/ProfileClosure.cpp",
    "Engine/SlateShape/Sketch/ProfilePattern/Source/ProfilePattern.cpp",
    "Engine/SlateShape/Sketch/ProfileReshape/Source/ProfileReshape.cpp",
    "Engine/SlateShape/Sketch/ProfileSolver/Source/ProfileSolver.cpp",
    "Engine/SlateShape/Sketch/SketchAnalysis/Source/SketchAnalysis.cpp",
    "Engine/SlateShape/Sketch/SketchCreation/Source/SketchCreation.cpp",
    "Engine/SlateShape/Sketch/SketchEditing/Source/SketchEditing.cpp",
    "Engine/SlateShape/Sketch/SketchPolyline/Source/SketchPolyline.cpp",
    "Engine/SlateShape/Sketch/SketchSelection/Source/SketchSelection.cpp",
    "Engine/SlateShape/Sketch/SketchSnap/Source/SketchSnap.cpp",
    "Engine/SlateShape/Sketch/SketchSolve/Source/SketchSolve.cpp",
    "Engine/SlateShape/Geometry/CurveSpecification/Source/CurveSpecification.cpp",
    "Engine/SlateShape/Geometry/ProfileSpecification/Source/ProfileSpecification.cpp",
    "Engine/SlateShape/Geometry/SurfaceSpecification/Source/SurfaceSpecification.cpp",
    "Engine/SlateShape/Record/WorkspaceDirectoryProjection/Source/WorkspaceDirectoryProjection.cpp",
    "Engine/SlateShape/Record/WorkspaceNameIndex/Source/WorkspaceNameIndex.cpp",
    "Engine/SlateShape/Record/WorkspacePropertyProjection/Source/WorkspacePropertyProjection.cpp",
    "Engine/SlateShape/Record/WorkspaceRecordStructure/Source/WorkspaceRecordStructure.cpp",
    "Engine/SlateShape/Record/WorkspaceRevisionSequence/Source/WorkspaceRevisionSequence.cpp",
    "Engine/SlateShape/Reference/OccurrenceSelection/Source/OccurrenceSelection.cpp",
    "Engine/SlateShape/Reference/PickClassifier/Source/PickClassifier.cpp",
    "Engine/SlateShape/Reference/ProvenanceIndex/Source/ProvenanceIndex.cpp",
    "Engine/SlateShape/Reference/ReferenceSpecification/Source/ReferenceSpecification.cpp",
    "Engine/SlateShape/Reference/TopologySelection/Source/TopologySelection.cpp",
    "Engine/SlateDocument/Format/CodexInterchange/Source/CodexInterchange.cpp",
    "Engine/SlateDocument/Format/CodexInterchange/Source/PigmentCodex.cpp",
    "Engine/SlateDocument/Format/CodexInterchange/Source/WorkspaceCodex.cpp",
    "Engine/SlateDocument/Document/MaterialSpecification/Source/MaterialSpecification.cpp",
    "Engine/SlateDocument/Document/MaterialSpecification/Source/PhysicalSurfaceSpecification.cpp",
]

INCLUDES = ["-I", ".", "-I", "Engine", "-I", "Tools/VulkanParseStub"]


def Compile(Source, Strict, Objects):
    Object = ROOT / "Tools" / "SketchDurabilityProof" / (pathlib.Path(Source).stem + ".o")
    Command = ["g++", "-std=c++20", "-ffunction-sections", "-fdata-sections",
               "-c", Source, "-o", str(Object)] + INCLUDES
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

    Binary = ROOT / "Tools" / "SketchDurabilityProof" / "SketchDurabilityProof"
    Binary.unlink(missing_ok=True)
    Link = subprocess.run(["g++", "-Wl,--gc-sections", "-o", str(Binary)] + Objects, cwd=ROOT,
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
