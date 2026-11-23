#!/usr/bin/env python3
"""Validation for manifest-declared external include paths in Construct.ps1."""
from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    construct = read("Build/Construct.ps1")
    require("ExternalInclude" in construct, "Construct.ps1 must carry [external].include entries from Module.toml")
    require("Resolve-ManifestPath" in construct, "Construct.ps1 must resolve manifest paths before passing /I")
    require("$UnitEntry.ExternalInclude" in construct, "Get-IncludePath must append manifest external includes")
    require("Select-Object -Unique" in construct, "include path emission should deduplicate manifest and default paths")

    slate_compute = read("Engine/SlateCompute/Module.toml")
    require("ExternalPackages/earcut/include" in slate_compute, "SlateCompute must declare the earcut include root")

    slate_shape = read("Engine/SlateShape/Module.toml")
    require("ExternalPackages/earcut/include" in slate_shape, "SlateShape must declare the earcut include root for profile area fills")
    require("ExternalPackages/clipper2/CPP/Clipper2Lib/include" in slate_shape, "SlateShape must declare the clipper2 include root for robust profile area operations")

    vertex = read("Engine/SlateVulkan/Device/WorkspaceScenePass/Shader/WorkspaceSceneVertex.slang")
    fragment = read("Engine/SlateVulkan/Device/WorkspaceScenePass/Shader/WorkspaceSceneFragment.slang")
    require("[[vk::binding(0, 0)]] StructuredBuffer<SceneTriangleRecord> SceneTriangles" in vertex,
            "scene triangle buffer must declare its Vulkan binding")
    require("[[vk::binding(1, 0)]] StructuredBuffer<SceneMaterialRecord> SceneMaterials" in vertex,
            "scene material buffer must declare its Vulkan binding in the vertex shader")
    require("[[vk::binding(1, 0)]] StructuredBuffer<SceneMaterialRecord> SceneMaterials" in fragment,
            "scene material buffer must declare its Vulkan binding in the fragment shader")

    shader_text = "\n".join(path.read_text(encoding="utf-8") for path in (ROOT / "Engine").rglob("*.slang"))
    require(not re.search(r"register\s*\(", shader_text), "shader register declarations must not bypass Vulkan binding declarations")

    print("[BuildExternalIncludes] manifest external includes and shader Vulkan bindings hold")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[BuildExternalIncludes] failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
