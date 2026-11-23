#!/usr/bin/env python3
"""Static validation for Material System Pass 2 scene material routing."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_text(path: str, *needles: str) -> str:
    content = text(path)
    for needle in needles:
        require(needle in content, f"{path} missing {needle!r}")
    return content


def main() -> int:
    require_text(
        "References/MaterialSystemPass2Plan.md",
        "Scene pass material binding",
        "Lit/Matcap/Wire/Points",
        "CPU fallback material preview",
    )

    header = require_text(
        "Engine/SlateVulkan/Device/WorkspaceScenePass/Api/WorkspaceScenePass.h",
        "enum class WorkspaceSceneViewMode",
        "WorkspaceSceneMaterial",
        "UploadMaterials",
        "TriangleCapacity",
        "MaterialCapacity",
        "ScenePipeline",
    )
    require("SlateDocument" not in header and "SlateCompute" not in header,
            "SlateVulkan scene pass header must not depend on document/compute strata")

    source = require_text(
        "Engine/SlateVulkan/Device/WorkspaceScenePass/Source/WorkspaceScenePass.cpp",
        "Streams.Resolve(\"SlateVulkan\", \"WorkspaceSceneVertex\")",
        "Streams.Resolve(\"SlateVulkan\", \"WorkspaceSceneFragment\")",
        "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER",
        "vkCreateGraphicsPipelines",
        "WorkspaceScenePass::UploadMaterials",
        "ViewModeValue(ViewMode)",
        "WorkspaceSceneViewMode::Points ? 18u : 3u",
        "WorkspaceScenePass.GeometryAndMaterials",
    )

    require_text(
        "Engine/SlateVulkan/Device/WorkspaceScenePass/Shader/WorkspaceSceneVertex.slang",
        "SceneViewLit",
        "SceneViewMatcap",
        "SceneViewSourceWire",
        "SceneViewTriangulatedWire",
        "SceneViewPoints",
        "StructuredBuffer<SceneTriangleRecord>",
        "StructuredBuffer<SceneMaterialRecord>",
        "ProjectWorld",
    )

    require_text(
        "Engine/SlateVulkan/Device/WorkspaceScenePass/Shader/WorkspaceSceneFragment.slang",
        "Surface.Albedo",
        "Surface.Scalars",
        "SceneViewSourceWire",
        "SceneViewTriangulatedWire",
        "SceneViewNormal",
        "SceneViewMetallic",
        "SceneViewIllumination",
        "MatcapShade",
    )

    # 📝 The codex proxy left the host at step 10. The colour a proxy is tinted by is now the unit's
    #    question, and this checks it where it is answered.
    require_text(
        "Engine/SlateWorkspace/Discipline/CodexSceneProxy/Source/CodexSceneProxy.cpp",
        "CodexMaterialToken",
        "Material.Reference != Entry.MaterialReference",
        "ChannelSubject::AlbedoColour",
        "CodexMaterialToken(Scene, Entry",
    )

    subprocess.run(
        [
            "g++", "-std=c++20", "-fsyntax-only", "-IEngine",
            "Engine/SlateDocument/Format/CodexInterchange/Source/WorkspaceCodex.cpp",
            "Engine/SlateDocument/Format/WorkspaceSceneActivation/Source/WorkspaceSceneActivation.cpp",
            "Engine/SlateDocument/Format/SceneMeshImport/Source/SceneMeshImport.cpp",
            "Engine/SlateDocument/Document/SurfaceLayerSequence/Source/SurfaceLayerSequence.cpp",
            "Engine/SlateCompute/Compute/MaterialProcessingExchange/Source/MaterialProcessingExchange.cpp",
        ],
        cwd=ROOT,
        check=True,
    )

    print("[MaterialSystemPass2] scene material buffers, shaders, view modes and fallback material colour hold")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[MaterialSystemPass2] failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
