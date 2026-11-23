#!/usr/bin/env python3
"""Validation for requested final material completion slice."""
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
        "References/MaterialSystemPass7Plan.md",
        "Flattened texture writing",
        "GPU texture upload/sampling path",
        "Export UI wiring",
        "Full imported image decode path",
        "Stopped after the requested",
    )
    require_text(
        "References/MaterialSystemCompletionProgress.md",
        "completed through requested stop point",
        "Material System Pass 7",
        "No Texture Paint workspace logic was added to `ParametricSketchHost`",
    )

    texture_export_header = require_text(
        "Engine/SlateCompute/Compute/MaterialTextureExport/Api/MaterialTextureExport.h",
        "FlattenedMaterialTexture",
        "MaterialTextureExportReport",
        "FlattenImage",
        "WritePackage",
    )
    require("Asset" not in texture_export_header and "Kind" not in texture_export_header and "kind" not in texture_export_header,
            "texture export API must avoid banned naming")

    require_text(
        "Engine/SlateCompute/Compute/MaterialTextureExport/Source/MaterialTextureExport.cpp",
        "WriteTga",
        "ConvertTga",
        "ChannelLaneValue",
        "SampleMaterialChannel",
        "EncodeMaterialExportManifest",
        "MaterialExportFormatExtension",
    )

    upload_header = require_text(
        "Engine/SlateVulkan/Device/MaterialTextureUpload/Api/MaterialTextureUpload.h",
        "MaterialTextureUploadDeclaration",
        "MaterialTextureSamplerLink",
        "ConstructMaterialTextureUpload",
        "UploadRgbaFloat",
        "VkImageView View",
        "VkSampler Sampler",
    )
    require("SlateDocument" not in upload_header and "SlateCompute" not in upload_header,
            "SlateVulkan texture upload header must not depend on document/compute strata")

    require_text(
        "Engine/SlateVulkan/Device/MaterialTextureUpload/Source/MaterialTextureUpload.cpp",
        "vkCreateImage",
        "VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT",
        "vkCreateImageView",
        "vkCreateSampler",
        "vkCmdCopyBufferToImage",
        "VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL",
        "vkQueueSubmit",
        "MaterialTextureUpload.Sampler",
    )

    require_text(
        "Engine/SlateCompute/Compute/MaterialImageSampling/Source/MaterialImageSampling.cpp",
        "DecodeExternal",
        "convert ",
        "rgba:",
    )
    require_text(
        "Engine/SlateCompute/Compute/MaterialImageSampling/Api/MaterialImageSampling.h",
        "PngDecoded = true",
        "JpegDecoded = true",
        "WebpDecoded = true",
        "ExrDecoded = true",
    )
    require_text(
        "Engine/SlateDocument/Format/MaterialImageImport/Source/MaterialImageImport.cpp",
        "IdentifyExternal",
        "identify -format",
        "MaterialImageFormat::Webp",
        "MaterialImageFormat::Exr",
    )

    require_text(
        "Engine/Application/EditorHost/Source/EditorHost.cpp",
        "MaterialTextureExport/Api/MaterialTextureExport.h",
        "BuildMaterialExportPackage",
        "MaterialTextureExport().WritePackage",
        "TexturingApplied.ExportLocation",
    )

    subprocess.run([
        "g++", "-std=c++20", "-fsyntax-only", "-IEngine",
        "Engine/SlateCompute/Compute/MaterialTextureExport/Source/MaterialTextureExport.cpp",
        "Engine/SlateDocument/Format/MaterialExport/Source/MaterialExport.cpp",
        "Engine/SlateCompute/Compute/MaterialImageSampling/Source/MaterialImageSampling.cpp",
        "Engine/SlateDocument/Format/MaterialImageImport/Source/MaterialImageImport.cpp",
        "Engine/SlateDocument/Format/CodexInterchange/Source/WorkspaceCodex.cpp",
        "Engine/SlateDocument/Document/SurfaceLayerSequence/Source/SurfaceLayerSequence.cpp",
    ], cwd=ROOT, check=True)

    print("[MaterialSystemPass7] flattened writing, GPU upload path, UI export route and full decoder fallback hold")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[MaterialSystemPass7] failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
