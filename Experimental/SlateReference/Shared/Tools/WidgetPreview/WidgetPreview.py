#!/usr/bin/env python3
"""Renders the Select widget and the construction popups to SVG.

🔴 THIS IS NOT A PROOF AND IS NOT IN THE GATE SWEEP. It draws a picture so a human can look at the
   widgets without running the editor. It earns its place by reading the ENGINE'S OWN CONSTANTS --
   panel width, row height, radii, the design tokens, the tolerance default -- so a measure changed
   in the headers changes the picture. A mock-up typed by hand would drift the first time one did.
"""

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = pathlib.Path("/tmp/preview/widgets.svg")


def Main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    Binary = ROOT / "Tools" / "WidgetPreview" / "WidgetPreview"
    Binary.unlink(missing_ok=True)

    Built = subprocess.run(
        ["g++", "-std=c++20", "-w", "-o", str(Binary),
         "Tools/WidgetPreview/WidgetPreview.cpp",
         "-I", ".", "-I", "Engine", "-I", "Tools/VulkanParseStub"],
        cwd=ROOT, capture_output=True, text=True)
    if Built.returncode != 0:
        print(Built.stderr[:4000])
        return 1

    Run = subprocess.run([str(Binary)], cwd=ROOT, capture_output=True, text=True)
    print(Run.stdout, end="")
    if Run.returncode != 0:
        print(Run.stderr[:2000])
    Binary.unlink(missing_ok=True)
    return Run.returncode


if __name__ == "__main__":
    sys.exit(Main())
