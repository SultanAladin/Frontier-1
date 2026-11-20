#!/usr/bin/env python3
"""Restore the SDF / GI sources that commit 63db120 removed from Engine/ and Exhibits/.

Default is a dry run. Pass --apply to write the files.

    python3 Experimental/RemovedSdfGi/Restore.py           # list what would be restored
    python3 Experimental/RemovedSdfGi/Restore.py --apply   # materialise them

Files land at their original repository paths, relative to the repository root, so
`Source/Exhibits/CleanEmissiveGiValidation.cpp` becomes `Exhibits/CleanEmissiveGiValidation.cpp`.
Evidence PNGs are restored into `Exhibits/Images/<Domain>/`.
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

ARCHIVE_ROOT = Path(__file__).resolve().parent
REPO_ROOT = ARCHIVE_ROOT.parents[1]

SOURCE_PREFIXES = (
    ("Source/Exhibits/DeterministicPathTracer", "Exhibits/DeterministicPathTracer"),
    ("Source/Exhibits", "Exhibits"),
    ("Source/Engine", "Engine"),
)

EVIDENCE_PREFIX = "Evidence"


def digest(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()[:12]


def target_for(relative_path: Path) -> Path | None:
    posix = relative_path.as_posix()
    if posix.startswith(EVIDENCE_PREFIX + "/"):
        parts = relative_path.parts[1:]
        if len(parts) < 2:
            return None
        return Path("Exhibits/Images") / parts[0] / Path(*parts[1:])
    for archive_prefix, repo_prefix in SOURCE_PREFIXES:
        if posix == archive_prefix or posix.startswith(archive_prefix + "/"):
            tail = posix[len(archive_prefix):].lstrip("/")
            return Path(repo_prefix) / tail if tail else Path(repo_prefix)
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="write the files instead of listing them")
    parser.add_argument("--force", action="store_true", help="overwrite files whose content already differs")
    args = parser.parse_args()

    planned: list[tuple[Path, Path]] = []
    for path in sorted(ARCHIVE_ROOT.rglob("*")):
        if not path.is_file() or path.name == "Restore.py" or path.suffix == ".patch":
            continue
        relative = path.relative_to(ARCHIVE_ROOT)
        target = target_for(relative)
        if target is None:
            continue
        planned.append((path, REPO_ROOT / target))

    written = kept = conflicted = 0
    for source_path, target_path in planned:
        payload = source_path.read_bytes()
        marker = digest(payload)
        if target_path.exists():
            if digest(target_path.read_bytes()) == marker:
                kept += 1
                print(f"  same      {target_path.relative_to(REPO_ROOT)}")
                continue
            if not args.force:
                conflicted += 1
                print(f"  conflict  {target_path.relative_to(REPO_ROOT)} (differs; pass --force to overwrite)")
                continue
        if not args.apply:
            print(f"  restore   {target_path.relative_to(REPO_ROOT)}  [{marker}]")
            written += 1
            continue
        target_path.parent.mkdir(parents=True, exist_ok=True)
        target_path.write_bytes(payload)
        written += 1
        print(f"  restored  {target_path.relative_to(REPO_ROOT)}  [{marker}]")

    verb = "restored" if args.apply else "would restore"
    print(f"\n{verb}: {written}, already identical: {kept}, conflicting: {conflicted}, total: {len(planned)}")
    if not args.apply:
        print("Dry run only — re-run with --apply to write the files.")
    return 0 if conflicted == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
