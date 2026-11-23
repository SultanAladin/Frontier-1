#!/usr/bin/env python3
"""SketchPlacementProof — compiles, links and RUNS the sketch placement machine.

Usage: python3 Tools/SketchPlacementProof/SketchPlacementProof.py

This is the only gate in the repository that executes engine code. `SketchToolset` names no
device, no window and no vendor header, so it links and runs on any toolchain — which is the
property that made the placement machine worth lifting out of a 5981-line host. Every other tool
here either parses source or samples a rendered image; this one calls the component.

What it proves, from `SketchPlacementProof.cpp`:

  1. The one declaration table answers exactly as the four switch statements it replaced,
     across all 22 retired subjects and all 61 catalogue tiles. The originals are transcribed
     verbatim into the proof so the comparison is against what shipped, not against a tidied
     restatement of it.
  2. Every subject completes at its declared anchor count and not one anchor before.
  3. A terminated curve grows past its minimum and ends only on a double-press.
  4. A dimension refuses contacts that landed on nothing.
  5. Sealing moves the anchors out and leaves the tool held.
  6. Restating the held tool keeps the anchors; changing it discards them.
  7. Contacts with no tool, and anchors with no hover, are refused.
  8. The anchor records exactly the snap the preview showed.

The proof has been negative-tested: four separate sabotages of the declaration table — a wrong
anchor count, a wrong closure, a misrouted catalogue tile and a mistyped name — each made it
fail and name the subject. A gate that has never been seen to fail proves nothing.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

SOURCES = [
    ROOT / "Tools/SketchPlacementProof/SketchPlacementProof.cpp",
    ROOT / "Engine/SketchToolset/SketchTool/SketchPlacement/Source/SketchPlacement.cpp",
    # 📝 `ResolvePlacementCurve` builds the curve an in-progress placement describes, so the placement
    #    unit now depends on the curve geometry it previews through.
    ROOT / "Engine/SlateShape/Geometry/CurveSpecification/Source/CurveSpecification.cpp",
]

INCLUDES = [ROOT / "Engine", ROOT / "Tools/VulkanParseStub"]


def Main():
    for Source in SOURCES:
        if not Source.exists():
            print(f"SketchPlacementProof: missing {Source.relative_to(ROOT)}")
            raise SystemExit(1)

    with tempfile.TemporaryDirectory() as Scratch:
        Binary = Path(Scratch) / "SketchPlacementProof"

        Command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror"]
        for Include in INCLUDES:
            Command += ["-I", str(Include)]
        Command += [str(Source) for Source in SOURCES]
        Command += ["-o", str(Binary)]

        Built = subprocess.run(Command, capture_output=True, text=True)
        if Built.returncode != 0:
            # -Werror is deliberate: an unhandled enumerator in any switch over SketchSubject is a
            # -Wswitch warning, and that warning is exactly how a newly added tool announces every
            # table it has not yet been added to. Letting it through as a warning would waste it.
            print("SketchPlacementProof: the toolset does not compile clean")
            print(Built.stderr.strip()[:4000])
            raise SystemExit(1)

        Ran = subprocess.run([str(Binary)], capture_output=True, text=True)
        sys.stdout.write(Ran.stdout)
        if Ran.stderr.strip():
            sys.stderr.write(Ran.stderr)

        if Ran.returncode != 0:
            print("SketchPlacementProof: REFUTED")
            raise SystemExit(1)

    print("SketchPlacementProof: the placement machine stands")
    raise SystemExit(0)


if __name__ == "__main__":
    Main()
