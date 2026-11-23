#!/usr/bin/env python3
"""Static validation for Material System Pass 3 image-reference material sources."""
# ⚠️ Strings quoted from documents under References/ are matched VERBATIM against files this
#    gate does not own. A vocabulary sweep must not rewrite them: renaming the quotation only
#    stops the gate finding the line, it does not rename the document.
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
        "References/MaterialSystemPass3Plan.md",
        "Imported image metadata intake",
        "SourceIndex",
        "BindWorkspaceMaterialImage",
        "no Texture Paint workspace logic",
    )

    material_header = require_text(
        "Engine/SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h",
        "std::uint32_t        SourceIndex",
        "Imported    = 3u",
    )

    require_text(
        "Engine/SlateCompute/Compute/MaterialProcessingExchange/Source/MaterialProcessingExchange.cpp",
        "HashValue(Hash, Channel.SourceIndex)",
    )

    codex_header = require_text(
        "Engine/SlateDocument/Format/CodexInterchange/Api/WorkspaceCodex.h",
        "WorkspaceMaterialImageReference",
        "std::vector<WorkspaceMaterialImageReference> Images",
        "AssignWorkspaceMaterial",
        "BindWorkspaceMaterialImage",
    )
    require("Asset" not in codex_header, "new material image API must not use banned Asset naming")

    require_text(
        "Engine/SlateDocument/Format/CodexInterchange/Source/WorkspaceCodex.cpp",
        "Inscribe32(Content, Channel.SourceIndex)",
        "Extract32(Content, Position, Channel.SourceIndex)",
        "Current.Images.reserve(ImageCount)",
        "BindWorkspaceMaterialImage",
        "Declared.Source = ChannelSource::Imported",
        "Declared.SourceIndex = SourceIndex",
    )

    require_text(
        "Engine/SlateDocument/Format/MaterialImageImport/Api/MaterialImageImport.h",
        "MaterialImageFormat",
        "ImportedMaterialImage",
        "SuggestMaterialImageChannel",
        "ImportMaterialImageReference",
    )

    require_text(
        "Engine/SlateDocument/Format/MaterialImageImport/Source/MaterialImageImport.cpp",
        "ParsePng",
        "ParseJpeg",
        "ParseBmp",
        "ParseTga",
        "ChannelSubject::Roughness",
        "ChannelSubject::SurfaceOrientation",
        "WorkspaceMaterialImageReference",
    )

    # 📝 Two different claims that used to sit in one host file. Listing a directory moved to
    #    `HostEnvironment` (shared by both hosts); acting on the chosen file is still the host's own.
    require_text(
        "Engine/SlateRuntime/Session/HostEnvironment/Source/HostEnvironment.cpp",
        "MaterialImageFormatSupported(Current.path().string())",
    )

    require_text(
        "Engine/SlateWorkspace/Discipline/ContentImportCommit/Source/ContentImportCommit.cpp",
        "ImportMaterialImageReference(ImportPath.string())",
        "BindWorkspaceMaterialImage",
    )

    subprocess.run(
        [
            "g++", "-std=c++20", "-fsyntax-only", "-IEngine",
            "Engine/SlateDocument/Document/SurfaceLayerSequence/Source/SurfaceLayerSequence.cpp",
            "Engine/SlateDocument/Format/CodexInterchange/Source/WorkspaceCodex.cpp",
            "Engine/SlateDocument/Format/MaterialImageImport/Source/MaterialImageImport.cpp",
            "Engine/SlateDocument/Format/WorkspaceSceneActivation/Source/WorkspaceSceneActivation.cpp",
            "Engine/SlateDocument/Format/SceneMeshImport/Source/SceneMeshImport.cpp",
            "Engine/SlateCompute/Compute/MaterialProcessingExchange/Source/MaterialProcessingExchange.cpp",
        ], cwd=ROOT, check=True)

    print("[MaterialSystemPass3] image reference import, source indices, binding and assignment commands hold")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[MaterialSystemPass3] failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
