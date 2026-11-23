#!/usr/bin/env python3
"""Static validation for Material System Pass 1 plumbing."""
# ⚠️ Strings quoted from documents under References/ are matched VERBATIM against files this
#    gate does not own. A vocabulary sweep must not rewrite them: renaming the quotation only
#    stops the gate finding the line, it does not rename the document.

from __future__ import annotations

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_text(relative: str, *needles: str) -> str:
    text = read(relative)
    for needle in needles:
        require(needle in text, f"{relative} missing {needle!r}")
    return text


def main() -> int:
    require_text(
        "References/MaterialSystemPass1Plan.md",
        "document-backed commands",
        "mandatory Base Material layer",
        "No painting and no UV editor",
        "Lit/Matcap/source-wire/triangulated-wire/points",
    )

    surface_header = require_text(
        "Engine/SlateDocument/Document/SurfaceLayerSequence/Api/SurfaceLayerSequence.h",
        "Deliver<std::uint32_t> DeclareChannelMask",
        "Deliver<LayerContentSource> DeclareSource",
        "Deliver<CoverageSpecification> DeclareCoverage",
        "std::uint32_t       ChannelMask",
        "bool                Inverted",
    )
    require("Mandatory" in surface_header and "base material cannot be removed" in surface_header,
            "mandatory base-layer protection was lost")

    require_text(
        "Engine/SlateDocument/Document/SurfaceLayerSequence/Source/SurfaceLayerSequence.cpp",
        "SurfaceLayerSequence::DeclareChannelMask",
        "SurfaceLayerSequence::DeclareSource",
        "SurfaceLayerSequence::DeclareCoverage",
        "no such coverage source",
    )

    compute_header = require_text(
        "Engine/SlateCompute/Compute/MaterialProcessingExchange/Api/MaterialProcessingExchange.h",
        "MaterialLayerCoveragePreview",
        "MaterialLayerCommandAction",
        "ApplyLayerCommand",
        "ResolveLayerCoveragePreview",
        "LayerSequenceResolution        = true",
        "AnalyticResolution             = true",
    )
    require("ImportedImageResolution        = true" in compute_header,
            "imported image resolution should stay enabled after pass 4")

    compute_source = require_text(
        "Engine/SlateCompute/Compute/MaterialProcessingExchange/Source/MaterialProcessingExchange.cpp",
        "HashValue(Hash, Layer.Coverage.ChannelMask)",
        "HashValue(Hash, Layer.Coverage.Inverted)",
        "ResolveLayerCoveragePreview",
        "ApplyLayerCommand",
        "MaterialLayerCommandAction::Duplicate",
        "MaterialLayerCommandAction::MoveToFolder",
        "std::clamp(Entry.Coverage.UniformStrength",
    )
    require("1.0 - Produced.Coverage" in compute_source,
            "coverage inversion is not part of mask evaluation")

    require_text(
        "Engine/SlateWorkspace/Discipline/MaterialLayerProjection/Source/MaterialLayerProjection.cpp",
        "ProjectMaterialLayersFromTextureStack",
        "InitialiseDielectric(Material, Layers)",
        "TextureRowCoverage",
        "MaskChannel",
        "Exchange.Capture(Material, Layers)",
        "Exchange.Compare(*PreviousSnapshot, Report.Snapshot)",
    )

    # 🔴 The third and last `*Bridge*` header. It called itself an "editor-only bridge"; it was neither.
    #    Living in `Application/` meant one host could reach it and a second would have had to copy it.
    if (ROOT / "Engine/Application/Api/MaterialLayerStackBridge.h").exists():
        raise SystemExit("MaterialLayerStackBridge.h is deleted; the projection lives in SlateWorkspace")

    require_text(
        "Engine/Application/EditorHost/Source/EditorHost.cpp",
        "SlateWorkspace/Discipline/MaterialLayerProjection/Api/MaterialLayerProjection.h",
        "MaterialSpecification     EditorMaterialDocument",
        "SurfaceLayerSequence      EditorMaterialLayers",
        "ProjectMaterialLayersFromTextureStack",
    )

    require_text(
        "Engine/SlateDocument/Format/CodexInterchange/Api/WorkspaceCodex.h",
        "WorkspaceMaterialRecord",
        "std::vector<WorkspaceMaterialRecord> Materials",
        "DefaultWorkspaceMaterialRecord",
        "EnsureWorkspaceMaterialRecords",
    )

    require_text(
        "Engine/SlateDocument/Format/CodexInterchange/Source/WorkspaceCodex.cpp",
        "MaterialSection    = 0x5354414D",
        "DefaultWorkspaceMaterialRecord",
        "Base.Name = \"Base Material\"",
        "Base.Mandatory = true",
        "EnsureWorkspaceMaterialRecords",
        "InscribeLayer(Materials, Layer)",
        "ExtractLayer(Materials->Content",
    )

    require_text(
        "Engine/SlateDocument/Format/SceneMeshImport/Api/SceneMeshImport.h",
        "std::vector<WorkspaceMaterialRecord> MaterialRecords",
    )
    require_text(
        "Engine/SlateDocument/Format/SceneMeshImport/Source/SceneMeshImport.cpp",
        "DefaultWorkspaceMaterialRecord(Slot)",
    )
    require_text(
        "Engine/SlateWorkspace/Discipline/ContentImportCommit/Source/ContentImportCommit.cpp",
        "OpenedScene.Materials.push_back(Material)",
        "EnsureWorkspaceMaterialRecords(OpenedScene)",
    )
    require_text(
        "Engine/SlateDocument/Format/WorkspaceSceneActivation/Source/WorkspaceSceneActivation.cpp",
        "MandatoryBaseLayerDeclared",
        "references a missing material",
        "Activated.Materials = Activated.Workspace.Materials",
    )

    editor_panel = require_text(
        "Engine/SlateUI/Interface/EditorPanel/Api/EditorPanel.h",
        "Lit",
        "SourceWire",
        "TriangulatedWire",
        "Points",
        "Wireframe          = TriangulatedWire",
    )
    require("ShadingCount       = 8u" in editor_panel, "view-mode count was not extended")

    require_text(
        "Engine/SlateUI/Interface/EditorPanel/Source/EditorPanel.cpp",
        "source wire",
        "tri wire",
        "points",
        "return \"lit\"",
    )

    subprocess.run(
        [
            "g++", "-std=c++20", "-fsyntax-only", "-IEngine",
            "Engine/SlateDocument/Document/SurfaceLayerSequence/Source/SurfaceLayerSequence.cpp",
            "Engine/SlateDocument/Format/CodexInterchange/Source/WorkspaceCodex.cpp",
            "Engine/SlateDocument/Format/WorkspaceSceneActivation/Source/WorkspaceSceneActivation.cpp",
            "Engine/SlateDocument/Format/SceneMeshImport/Source/SceneMeshImport.cpp",
            "Engine/SlateCompute/Compute/MaterialProcessingExchange/Source/MaterialProcessingExchange.cpp",
        ],
        cwd=ROOT,
        check=True,
    )

    print("[MaterialSystemPass1] plan, layer commands, masks, dirty keys and view routing hold")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover - validation entry point
        print(f"[MaterialSystemPass1] failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
