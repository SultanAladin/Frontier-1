#!/usr/bin/env python3
"""Static validation for Material System Pass 4 imported-image sampling."""
from __future__ import annotations

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_text(path: str, *needles: str) -> str:
    content = read(path)
    for needle in needles:
        require(needle in content, f"{path} missing {needle!r}")
    return content


def main() -> int:
    require_text(
        "References/MaterialSystemPass4Plan.md",
        "CPU-side imported-image material sampling",
        "explicit U/V coordinates",
        "missing references as refusals",
    )

    header = require_text(
        "Engine/SlateCompute/Compute/MaterialImageSampling/Api/MaterialImageSampling.h",
        "MaterialImageAddressing",
        "MaterialImageRaster",
        "MaterialImageSampleRequest",
        "SampleMaterialChannel",
        "FingerprintMaterialImageReference",
    )
    require("Asset" not in header, "material image sampling API must avoid banned Asset naming")

    source = require_text(
        "Engine/SlateCompute/Compute/MaterialImageSampling/Source/MaterialImageSampling.cpp",
        "DecodeBmp",
        "DecodeTga",
        "MaterialImageAddressing::Mirror",
        "Channel.Source != ChannelSource::Imported",
        "Channel.SourceIndex >= Material.Images.size()",
        "the imported image reference is absent",
        "DecodeExternal",
    )
    require("std::floor" in source and "std::min<std::uint32_t>" in source,
            "UV addressing and texel selection are not wired")

    require_text(
        "Engine/SlateCompute/Compute/MaterialProcessingExchange/Api/MaterialProcessingExchange.h",
        "ImportedImageResolution        = true",
    )
    require_text(
        "Engine/SlateCompute/Compute/MaterialProcessingExchange/Source/MaterialProcessingExchange.cpp",
        "HashValue(Hash, Channel.SourceIndex)",
        "Declared.ImportedImageResolution = true",
    )

    subprocess.run([
        "g++", "-std=c++20", "-fsyntax-only", "-IEngine",
        "Engine/SlateCompute/Compute/MaterialImageSampling/Source/MaterialImageSampling.cpp",
        "Engine/SlateDocument/Format/CodexInterchange/Source/WorkspaceCodex.cpp",
        "Engine/SlateDocument/Format/MaterialImageImport/Source/MaterialImageImport.cpp",
        "Engine/SlateDocument/Document/SurfaceLayerSequence/Source/SurfaceLayerSequence.cpp",
        "Engine/SlateCompute/Compute/MaterialProcessingExchange/Source/MaterialProcessingExchange.cpp",
    ], cwd=ROOT, check=True)

    print("[MaterialSystemPass4] imported-image sampling, UV addressing and dirty source indices hold")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[MaterialSystemPass4] failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
