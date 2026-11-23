#!/usr/bin/env python3
"""Builds and runs the tool-options widget proof.

The widget's own translation unit is compiled with every warning as an error, so a measure that stops
being used or a parameter that stops being read fails here rather than being noticed later. The proof
links only against what its claims touch: the arithmetic of the card, its rows and its pill.
"""

import pathlib
import subprocess
import tempfile
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

OWNED = [
    "Tools/ToolOptionsWidgetProof/ToolOptionsWidgetProof.cpp",
    "Engine/SlateUI/Interface/ToolOptionsWidget/Source/ToolOptionsWidget.cpp",
    "Engine/SlateUI/Interface/ToolContextMenu/Source/ToolContextMenu.cpp",
    "Engine/SlateUI/Interface/OptionControls/Source/OptionControls.cpp",
]

# 🔴 The repository root is NOT an include root. Adding `-I .` here once let a translation unit
#    reach a header by a spelling the real build cannot resolve, and the gate stayed green while
#    Windows failed with C1083. The include set must be no wider than Construct.ps1's.
INCLUDES = ["-I", "Engine", "-I", "Tools/VulkanParseStub"]


def Compile(Source, Strict, Objects, Scratch):
    Object = pathlib.Path(Scratch) / (pathlib.Path(Source).stem + ".o")
    Command = ["g++", "-std=c++20", "-c", Source, "-o", str(Object)] + INCLUDES
    Command += ["-Wall", "-Wextra", "-Werror"] if Strict else ["-w"]
    Result = subprocess.run(Command, cwd=ROOT, capture_output=True, text=True)
    if Result.returncode != 0:
        print(Result.stderr[:4000])
        return False
    Objects.append(str(Object))
    return True


def Main():
    # 📝 Objects and the linked binary are build output, not source. They are written to a scratch
    #    directory that is removed on the way out, so a proof run leaves the tree exactly as it
    #    found it -- an earlier version wrote them beside the source and they were committed.
    with tempfile.TemporaryDirectory(prefix="ToolOptionsWidgetProof-") as Scratch:
        return Run(Scratch)


def Run(Scratch):
    Objects = []

    # 📝 The widget compiles strictly on its own, which is the claim that the unit is warning-clean.
    #    The proof then runs against the parts of it that need no live surface.
    for Source in OWNED:
        if not Compile(Source, True, Objects, Scratch):
            print("[ToolOptionsWidgetProof] FAILED to compile", Source)
            return 1

    # 🔴 Only the proof is linked. The widget's recording path reaches a RecordingSurface, a
    #    MotionIntegrator and a ThemeProfile — the whole interface stack — and dragging that into a
    #    gate would test the stack rather than the widget. It is compiled above, so its warnings and
    #    its type errors are caught; what runs below is the arithmetic its behaviour rests on.
    # 🔴 `ControlIndex` and `MotionIntegrator` ARE linked, and deliberately. The press claims exercise the
    #    real grab-and-release rotation rather than a restatement of it -- the defect they exist to catch
    #    was `Advance` retiring a grab before the control asking about it ever ran, which a reimplementation
    #    of the rule inside the proof would have reproduced faithfully and passed.
    for Source in ["Engine/SlateUI/Interface/ControlIndex/Source/ControlIndex.cpp",
                   "Engine/SlateUI/Interface/MotionIntegrator/Source/MotionIntegrator.cpp"]:
        if not Compile(Source, False, Objects, Scratch):
            print("[ToolOptionsWidgetProof] FAILED to compile", Source)
            return 1

    # 🔴 Named, not "every object compiled above". The owned units are compiled for their warnings
    #    and their type errors; linking them too would drag in RecordingSurface and the rest of the
    #    interface stack, and the gate would then be testing the stack rather than the widget.
    Binary = pathlib.Path(Scratch) / "ToolOptionsWidgetProof"
    Linked = [str(pathlib.Path(Scratch) / Name) for Name in
              ("ToolOptionsWidgetProof.o", "ControlIndex.o", "MotionIntegrator.o")]
    Link = ["g++", "-std=c++20", "-o", str(Binary)] + Linked
    Result = subprocess.run(Link, cwd=ROOT, capture_output=True, text=True)
    if Result.returncode != 0:
        Reported = [Line for Line in Result.stderr.splitlines() if "undefined ref" in Line]
        print("\n".join(Reported[:20]) if Reported else Result.stderr[:3000])
        print("[ToolOptionsWidgetProof] FAILED to link")
        return 1

    Ran = subprocess.run([str(Binary)], cwd=ROOT, capture_output=True, text=True)
    print(Ran.stdout)
    if Ran.returncode != 0:
        print(Ran.stderr[:2000])
    return Ran.returncode


if __name__ == "__main__":
    sys.exit(Main())
