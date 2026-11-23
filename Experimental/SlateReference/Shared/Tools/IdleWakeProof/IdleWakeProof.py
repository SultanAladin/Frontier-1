#!/usr/bin/env python3
"""Builds and runs the idle-wake proof.

The wake rule decides whether the editor presents an image at all. It is checked on its own, away
from the device, because the rule is the part that can be wrong in a way nobody notices: a rule that
always wakes costs 8 to 9% of a core at rest, and a rule that wrongly sleeps freezes the window.
"""

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

OWNED = [
    "Tools/IdleWakeProof/IdleWakeProof.cpp",
    "Engine/SlateUI/Interface/RedrawScheduler/Source/RedrawScheduler.cpp",
]

INCLUDES = ["-I", ".", "-I", "Engine", "-I", "Tools/VulkanParseStub"]


def Compile(Source, Objects):
    Object = ROOT / "Tools" / "IdleWakeProof" / (pathlib.Path(Source).stem + ".o")
    Command = ["g++", "-std=c++20", "-c", Source, "-o", str(Object)] + INCLUDES
    Command += ["-Wall", "-Wextra", "-Werror"]
    Result = subprocess.run(Command, cwd=ROOT, capture_output=True, text=True)
    if Result.returncode != 0:
        print(Result.stderr[:4000])
        return False
    Objects.append(str(Object))
    return True


def Main():
    Objects = []
    for Source in OWNED:
        if not Compile(Source, Objects):
            print("FAILED to compile " + Source)
            return 1

    Binary = ROOT / "Tools" / "IdleWakeProof" / "IdleWakeProof"
    Binary.unlink(missing_ok=True)
    Link = subprocess.run(["g++", "-o", str(Binary)] + Objects, cwd=ROOT,
                          capture_output=True, text=True)
    if Link.returncode != 0:
        print(Link.stderr[:4000])
        print("FAILED to link")
        return 1

    Ran = subprocess.run([str(Binary)], cwd=ROOT, capture_output=True, text=True)
    print(Ran.stdout, end="")
    if Ran.stderr:
        print(Ran.stderr[:2000], end="")

    for Object in Objects:
        pathlib.Path(Object).unlink(missing_ok=True)
    Binary.unlink(missing_ok=True)

    return Ran.returncode


if __name__ == "__main__":
    sys.exit(Main())
