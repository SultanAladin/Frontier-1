#!/usr/bin/env python3
"""Static validation for Material System Pass 6 export packing declarations."""
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
        "References/MaterialSystemPass6Plan.md",
        "export packing declarations",
        "Unreal/glTF ORM packing",
        "manifest generation",
    )

    header = require_text(
        "Engine/SlateDocument/Format/MaterialExport/Api/MaterialExport.h",
        "MaterialExportTarget",
        "MaterialExportOptions",
        "MaterialExportLaneDeclaration",
        "MaterialExportImageDeclaration",
        "MaterialExportPackage",
        "MaterialExportPreset",
        "BuildMaterialExportPackage",
        "EncodeMaterialExportManifest",
    )
    require("Asset" not in header and "Kind" not in header and "kind" not in header,
            "material export API must avoid banned naming")

    source = require_text(
        "Engine/SlateDocument/Format/MaterialExport/Source/MaterialExport.cpp",
        "MaterialExportTarget::Unreal",
        "MaterialExportTarget::Unity",
        "MaterialExportTarget::Gltf",
        "ChannelSubject::AmbientOcclusion",
        "ChannelSubject::Roughness",
        "ChannelSubject::Metallic",
        "MaterialExportNormalConvention::DirectX",
        "ExportsConsumedChannel",
        "FingerprintMaterial",
        "EncodeMaterialExportManifest",
        "\\\"lanes\\\"",
    )
    require("_ORM" in source and "_Mask" in source and "_Emission" in source,
            "expected DCC/engine packing outputs are missing")
    require("Asset" not in source and "Kind" not in source and "kind" not in source,
            "material export implementation must avoid banned naming")

    subprocess.run([
        "g++", "-std=c++20", "-fsyntax-only", "-IEngine",
        "Engine/SlateDocument/Format/MaterialExport/Source/MaterialExport.cpp",
        "Engine/SlateDocument/Format/CodexInterchange/Source/WorkspaceCodex.cpp",
        "Engine/SlateDocument/Document/SurfaceLayerSequence/Source/SurfaceLayerSequence.cpp",
    ], cwd=ROOT, check=True)

    print("[MaterialSystemPass6] export targets, channel packing presets and manifests hold")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[MaterialSystemPass6] failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
