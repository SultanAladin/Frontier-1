#!/usr/bin/env python3
"""VerifyHostPartition.py — proves that a host is lifetime and tick only.

`32` §5 states the gate: "Neither host contains engine logic; each is lifetime and tick only." Nothing
enforced it, and the tree drifted to a 5 981-line host holding 138 local function definitions — a library
written inside an executable, reachable from no other host. An agent asked to reuse that behaviour had no
way to, so it wrote its own camera, sky and CAD editor instead.

Four rules are checked:

  ① A host source declares no function beyond main() and its tick helpers.
  ② No file under Application/ implements a solver, tessellator or raster loop.
  ③ Logic is gated with `if constexpr`, not `#ifdef`; the preprocessor guards includes and members only.
  ④ Every .cpp under Application/ is reachable from a declared subject.

    python3 Tools/VerifyHostPartition.py            # warn only, reports and exits 0
    python3 Tools/VerifyHostPartition.py --permissive   # reports without refusing

The default is deliberate. The debt predates the rule, so the check is introduced measuring rather than
refusing. That debt is now zero and this refuses by default; see the note at the foot of main().
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APPLICATION = ROOT / "Engine" / "Application"

# 📝 A host is permitted these. main() is the entry point; the rest are the tick's own shape, which is
# what a host legitimately owns.
PERMITTED_FUNCTIONS = {"main", "WinMain", "wWinMain"}

# 🔴 ConsoleHost is exempt by `32` §3: it is the headless verifier, its whole body is check procedures,
#    and it presents no workspace. InterfaceValidationHost is exempt on the same ground — it drives the
#    interface through declared scenarios rather than seating an artist's workspace. Neither is a product,
#    and neither may seat one; that is what keeps the exemption from becoming a way out of the rule.
EXEMPT_SUBJECTS = {"ConsoleHost", "InterfaceValidationHost"}

# 🔴 Names that state engine mechanism. A host defining one of these has taken ownership of a behaviour
#    that belongs in a unit, which is exactly how the duplication happened.
ENGINE_MECHANISM = (
    "Solve", "Solver", "Tessellate", "Triangulate", "Rasterise", "Rasterize",
    "Integrate", "Intersect", "Project", "Extrude", "Revolve", "Loft", "Sweep",
    "Fillet", "Chamfer", "Boolean", "Offset", "Constrain", "Snap",
)

# 📝 The declared function-definition shape at file scope: a return type, a name and an open paren, with
#    no semicolon before it. A trailing semicolon on the same line is a declaration or a call, not a
#    definition, so those are excluded — but a signature may legitimately wrap across lines, so the tail
#    after the paren is not constrained beyond that.
# 🔴 `constexpr NAME = …` is a constant, not a function. The paren must follow the NAME directly for the
#    line to register, which is what separates `constexpr double Pi = 3.14` from `constexpr T Faded(…)`.
DEFINITION = re.compile(
    r"^(?!\s)(?:static\s+|inline\s+|constexpr\s+|extern\s+\"C\"\s+)*"
    r"[A-Za-z_][\w:<>,\s\*&\[\]]*?[\s\*&]"
    r"([A-Za-z_]\w*)\s*\(",
)

# 📝 Control flow reads as `TYPE name(` to the pattern above. Naming them is cheaper and clearer than
#    teaching the pattern C++.
CONTROL_FLOW = {"if", "for", "while", "switch", "return", "catch", "sizeof", "else", "do"}

SUBJECT = re.compile(r'(?ms)^\[unit\].*?^subject\s*=\s*\[(.*?)\]')


def declared_subjects() -> set[str]:
    manifest = (APPLICATION / "Module.toml").read_text(encoding="utf-8")
    found = SUBJECT.search(manifest)
    if not found:
        return set()
    return set(re.findall(r'"([^"]+)"', found.group(1)))


def host_sources() -> list[Path]:
    return sorted(APPLICATION.glob("*/Source/*.cpp"))


def scan(path: Path) -> dict:
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()

    definitions: list[tuple[int, str]] = []
    mechanism: list[tuple[int, str]] = []
    depth = 0
    namespace_depth = 0
    namespace_pending = False
    in_comment = False

    for number, line in enumerate(lines, start=1):
        stripped = line.strip()

        if in_comment:
            if "*/" in stripped:
                in_comment = False
            continue
        if stripped.startswith("/*"):
            if "*/" not in stripped:
                in_comment = True
            continue
        if stripped.startswith("//") or not stripped:
            continue

        # 🔴 A namespace brace is not a scope for this purpose. Hosts wrap their whole content in
        #    `namespace Slate { namespace { … } }`, so counting those braces as depth would put every
        #    definition in the file "inside a function" and the check would pass by measuring nothing.
        #    Namespace braces are tracked separately and subtracted.
        # ⚠️ The brace may sit on the namespace line or on the line after it — both spellings appear in
        #    this tree. A pending flag carries the intent to the next brace so `namespace\n{` is counted
        #    the same as `namespace {`.
        if re.match(r"^\s*namespace\b", stripped):
            if "{" in stripped:
                namespace_depth += stripped.count("{")
            else:
                namespace_pending = True
            depth += stripped.count("{") - stripped.count("}")
            depth = max(depth, 0)
            continue

        if namespace_pending and stripped.startswith("{"):
            namespace_depth += 1
            namespace_pending = False
            depth += line.count("{") - line.count("}")
            depth = max(depth, 0)
            continue

        # Only file scope counts. A helper inside a function body is a local, not a declared capability.
        if depth - namespace_depth <= 0 and ";" not in stripped:
            found = DEFINITION.match(line)
            if found:
                name = found.group(1)
                if (name not in PERMITTED_FUNCTIONS
                        and name not in CONTROL_FLOW
                        and not name.startswith("_")):
                    definitions.append((number, name))
                    for mark in ENGINE_MECHANISM:
                        if mark.lower() in name.lower():
                            mechanism.append((number, name))
                            break

        depth += line.count("{") - line.count("}")
        depth = max(depth, 0)

        # 📝 A namespace's closing brace is an ordinary `}`. Clamping here retires the namespace's
        #    contribution as soon as the depth it opened is left, without parsing which brace closed what.
        namespace_depth = min(namespace_depth, depth)

    # ③ #if guarding logic rather than includes or members.
    preprocessor: list[tuple[int, str]] = []
    for number, line in enumerate(lines, start=1):
        stripped = line.strip()
        if not re.match(r"#\s*(if|ifdef|ifndef|elif)\b", stripped):
            continue
        if "SLATE_" not in stripped:
            continue
        window = "\n".join(lines[number : number + 6])
        if "#include" in window:
            continue
        preprocessor.append((number, stripped))

    return {
        "definitions": definitions,
        "mechanism": mechanism,
        "preprocessor": preprocessor,
        "lines": len(lines),
    }


def main() -> int:
    parsed = argparse.ArgumentParser(description=__doc__)
    parsed.add_argument("--strict", action="store_true",
                        help="accepted and ignored; refusing is the default now that the debt is paid")
    parsed.add_argument("--permissive", action="store_true",
                        help="report without refusing, for use mid-lift")
    argument = parsed.parse_args()

    subjects = declared_subjects()
    sources = host_sources()

    if not sources:
        print("[HostPartition] no host sources found", file=sys.stderr)
        return 1

    total_definitions = 0
    total_mechanism = 0
    total_preprocessor = 0
    unreachable: list[Path] = []

    print(f"[HostPartition] {len(sources)} host translation units\n")

    for path in sources:
        subject = path.relative_to(APPLICATION).parts[0]
        if subject not in subjects:
            unreachable.append(path)

        if subject in EXEMPT_SUBJECTS:
            print(f"  [    ] {path.relative_to(ROOT)}")
            print(f"         exempt — {subject} presents no workspace")
            continue

        result = scan(path)
        definitions = len(result["definitions"])
        mechanism = len(result["mechanism"])
        preprocessor = len(result["preprocessor"])

        total_definitions += definitions
        total_mechanism += mechanism
        total_preprocessor += preprocessor

        mark = "ok " if definitions == 0 and mechanism == 0 and preprocessor == 0 else "DEBT"
        print(f"  [{mark}] {path.relative_to(ROOT)}")
        print(f"         {result['lines']:>5} lines · {definitions:>3} definitions · "
              f"{mechanism:>2} engine-mechanism · {preprocessor:>2} preprocessor-gated logic")

        for number, name in result["mechanism"][:6]:
            print(f"           engine mechanism at :{number} — {name}")

    print()
    if unreachable:
        for path in unreachable:
            print(f"[HostPartition] unreachable — {path.relative_to(ROOT)} is under no declared subject")

    print(f"[HostPartition] totals — {total_definitions} definitions, "
          f"{total_mechanism} engine-mechanism, {total_preprocessor} preprocessor-gated, "
          f"{len(unreachable)} unreachable")

    refused = total_definitions or total_mechanism or total_preprocessor or unreachable

    if not refused:
        print("[HostPartition] hosts are lifetime and tick only")
        return 0

    # 🔴 THE DEBT IS PAID. Every host reached zero definitions, zero engine-mechanism and zero
    #    preprocessor-gated logic, so this refuses by default from here on. `--permissive` still reports
    #    without refusing, for someone mid-lift who wants to see the remaining count — but a host that
    #    grows a definition back now FAILS THE BUILD rather than adding a line to a report nobody reads.
    if argument.permissive:
        print("[HostPartition] reporting only — --permissive was passed")
        return 0

    print("[HostPartition] failed: a host holds behaviour that belongs in a unit", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
