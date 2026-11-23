#!/usr/bin/env python3
"""WorkspaceDeclarationProof — builds every declared workspace into a real PanelStructure.

Usage: python3 Tools/WorkspaceDeclarationProof/WorkspaceDeclarationProof.py

The second gate in the repository that executes engine code. It links the REAL
`SlateUI/Interface/PanelStructure` rather than a stand-in, because the whole risk of the
workspace declaration is in the slot ordinals: a declaration says "seat the directory at slot
1", and slot 1 is only correct if `PanelStructure::Divide` allocates the way the declaration
assumes. That is unprovable by reading — it depends on an allocator this unit does not own.

What it proves:

  1. The declared sketching workspace produces a partition IDENTICAL, slot for slot, to the
     `ConstructParametricLayout` procedure it replaced — same panels, same divisions, same
     0.27 and 0.33 proportions. The procedure is transcribed verbatim into the proof.
  2. Every declaration applies without refusal, seats a viewport, and seats the panel its
     tools are chosen from.
  3. The disciplines differ, and the combined product reuses the sketching arrangement rather
     than carrying its own — the plan's "third product at zero additional code" made checkable.
  4. A declaration naming a slot that no division created REFUSES rather than silently
     skipping, which is how the retired procedure behaved since every call was `Discard`ed.

Negative-tested: swapping two seated panels, dropping a proportion, and pointing a step at the
wrong slot each make it fail and name the slot.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

SOURCES = [
    ROOT / "Tools/WorkspaceDeclarationProof/WorkspaceDeclarationProof.cpp",
    ROOT / "Engine/SlateWorkspace/Discipline/WorkspaceDeclaration/Source/WorkspaceDeclaration.cpp",
    ROOT / "Engine/SlateUI/Interface/PanelStructure/Source/PanelStructure.cpp",
]

INCLUDES = [ROOT / "Engine", ROOT / "Tools/VulkanParseStub"]


def Main():
    for Source in SOURCES:
        if not Source.exists():
            print(f"WorkspaceDeclarationProof: missing {Source.relative_to(ROOT)}")
            raise SystemExit(1)

    with tempfile.TemporaryDirectory() as Scratch:
        Binary = Path(Scratch) / "WorkspaceDeclarationProof"

        Command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror"]
        for Include in INCLUDES:
            Command += ["-I", str(Include)]
        Command += [str(Source) for Source in SOURCES]
        Command += ["-o", str(Binary)]

        Built = subprocess.run(Command, capture_output=True, text=True)
        if Built.returncode != 0:
            print("WorkspaceDeclarationProof: the workspace unit does not compile clean")
            print(Built.stderr.strip()[:4000])
            raise SystemExit(1)

        Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
        sys.stdout.write(Ran.stdout)
        if Ran.stderr.strip():
            sys.stderr.write(Ran.stderr)

        if Ran.returncode != 0:
            print("WorkspaceDeclarationProof: REFUTED")
            raise SystemExit(1)

    print("WorkspaceDeclarationProof: every declared workspace stands")
    raise SystemExit(0)


if __name__ == "__main__":
    Main()
