#!/usr/bin/env python3
"""Parse every host and unit source against a stand-in Vulkan header.

Why this exists
---------------
`Build/Construct.ps1` is Windows-only and needs cl.exe, the Vulkan SDK and populated submodules, so no
build of any kind was reachable in a Linux checkout. That left large edits to the hosts verifiable only by
reading them -- and reading is what let a stale `subject` name and an unread feature seam survive several
steps of the refactor.

`Tools/VulkanParseStub/vulkan/vulkan.h` declares just enough of the vendor's surface -- handles, enums and
the structures Slate actually names -- for g++ to PARSE the engine. That is a syntax and type check, not a
build: nothing links, no shader is lowered, and no vendor behaviour is exercised. It catches what reading
misses -- a signature that no longer matches, a member that moved, a host left referring to something a
refactor removed -- and it catches it in seconds.

⚠️ A clean run here does NOT mean the Windows build passes. It means every translation unit below is
internally consistent. Treat it as the fast gate, never as the verdict.
"""
from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUB = ROOT / "Tools" / "VulkanParseStub"

# 📝 One entry per translation unit that must parse, with the product macro it is compiled under. A host
#    named by [product] in Engine/Application/Module.toml is listed once per product, because the whole
#    point of the arrangement is that one source compiles several ways.
TRANSLATION = [
    ("Engine/SlateRuntime/Session/SessionSequence/Source/SessionSequence.cpp", None),
    ("Engine/Application/EditorHost/Source/EditorHost.cpp", "SLATE_TEXTURE_AUTHORING"),
    ("Engine/Application/EditorHost/Source/EditorHost.cpp", "SLATE_PARAMETRIC_AUTHORING"),
    ("Engine/Application/EditorHost/Source/EditorHost.cpp", "SLATE_COMBINED_AUTHORING"),
    ("Engine/Application/InterfaceValidationHost/Source/InterfaceValidationHost.cpp", "SLATE_COMBINED_AUTHORING"),
    ("Engine/Application/ConsoleHost/Source/ConsoleHost.cpp", "SLATE_COMBINED_AUTHORING"),
]


# 📝 Hosts examined for abandoned file-local functions. One product macro each, deliberately -- see the
#    note in the loop below.
ABANDONMENT = [
]


def parse(source: str, define: str | None) -> tuple[bool, str]:
    command = [
        "g++", "-std=c++20", "-fsyntax-only", "-Wall", "-Wextra",
        "-I", str(ROOT), "-I", str(ROOT / "Engine"), "-I", str(STUB),
    ]
    if define:
        command.append(f"-D{define}")
    command.append(str(ROOT / source))

    finished = subprocess.run(command, capture_output=True, text=True)
    return finished.returncode == 0, finished.stderr


def abandoned(source: str, define: str | None) -> tuple[bool, str]:
    """Report file-local functions that nothing calls.

    🔴 This must NOT use `-fsyntax-only`. That flag stops the compiler before the analysis that
       finds unreachable definitions, so `-Wunused-function` under it reports nothing at all --
       silently, and on a file with known-dead functions in it. Step 10 hit exactly that: the
       report came back empty on a host that held five abandoned functions. Codegen has to run,
       which is why this compiles to an object file and throws it away.

    ⚠️ Only functions in the anonymous namespace can be judged here. Anything with external
       linkage might be called from a translation unit this gate never sees.
    """
    with tempfile.TemporaryDirectory() as scratch:
        command = [
            "g++", "-std=c++20", "-c", "-Wunused-function",
            "-I", str(ROOT), "-I", str(ROOT / "Engine"), "-I", str(STUB),
        ]
        if define:
            command.append(f"-D{define}")
        command += [str(ROOT / source), "-o", str(Path(scratch) / "abandoned.o")]

        finished = subprocess.run(command, capture_output=True, text=True)

    named = [
        line for line in finished.stderr.splitlines()
        if "defined but not used" in line
    ]
    return not named, "\n".join(named)


def main() -> int:
    if not (STUB / "vulkan" / "vulkan.h").exists():
        print(f"[ParseHosts] the stand-in header is missing at {STUB}", file=sys.stderr)
        return 1

    failures = 0

    for source, define in TRANSLATION:
        if not (ROOT / source).exists():
            print(f"[ParseHosts] {source} does not exist", file=sys.stderr)
            failures += 1
            continue

        parsed, diagnostics = parse(source, define)
        leaf = Path(source).name
        print(f"  {leaf:<42} {define or '(no product macro)':<30} {'PARSES' if parsed else 'FAILED'}")

        if not parsed:
            failures += 1
            for line in [entry for entry in diagnostics.splitlines() if "error" in entry][:8]:
                print(f"      {line}", file=sys.stderr)

    # 🔴 Abandoned file-local functions, reported by the compiler rather than by grep. Step 10 removed
    #    195 lines of them from ParametricSketchHost; this keeps them from growing back. Only the hosts
    #    are examined, and only under one product macro each -- a function used solely by the OTHER
    #    product's branch is live, not abandoned, so checking every macro would report false deaths.
    for source, define in ABANDONMENT:
        clean, named = abandoned(source, define)
        leaf = Path(source).name
        if clean:
            print(f"  {leaf:<42} {'nothing abandoned':<30} CLEAN")
            continue
        failures += 1
        print(f"  {leaf:<42} {'abandoned functions':<30} {len(named.splitlines())} FOUND")
        for line in named.splitlines()[:10]:
            print(f"      {line}", file=sys.stderr)

    # 🔴 The one product arrangement that must NOT compile. HostFeature.h refuses a build with no product
    #    macro, and a header that stopped refusing would let a featureless host ship in silence -- which is
    #    the arrangement the [product] table was introduced to end.
    probe = ROOT / "Tools" / "VulkanParseStub" / "FeatureRefusalProbe.cpp"
    probe.write_text('#include "Application/Api/HostFeature.h"\nint main() { return 0; }\n', encoding="utf-8")
    refused, diagnostics = parse(str(probe.relative_to(ROOT)), None)
    probe.unlink()

    if refused or "No product macro" not in diagnostics:
        print("  HostFeature.h with no product macro        EXPECTED A REFUSAL, GOT NONE")
        failures += 1
    else:
        print(f"  {'HostFeature.h':<42} {'(no product macro)':<30} REFUSES, as declared")

    if failures:
        print(f"[ParseHosts] {failures} translation units did not parse", file=sys.stderr)
        return 1

    print("[ParseHosts] every host parses under every product macro")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
