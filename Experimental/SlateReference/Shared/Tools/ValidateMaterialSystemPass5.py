#!/usr/bin/env python3
"""Static validation for Material System Pass 5 textured-layer and dirty-tile plumbing."""
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
        "References/MaterialSystemPass5Plan.md",
        "document/compute painting MVP",
        "No parametric paint UI",
        "dirty tiles/channels",
    )

    header = require_text(
        "Engine/SlateCompute/Compute/MaterialTextureExchange/Api/MaterialTextureExchange.h",
        "MaterialTextureLayerDeclaration",
        "MaterialTextureDirtyTile",
        "MaterialTextureCommitReport",
        "CreateTexturedLayer",
        "DeclareStroke",
        "CommitResolvedStroke",
        "DirtyTilesOf",
    )
    require("ParametricSketchHost" not in header and "TexturingPanel" not in header,
            "texture exchange must not depend on parametric host or texturing UI")
    require("Asset" not in header, "texture exchange must avoid banned Asset naming")

    source = require_text(
        "Engine/SlateCompute/Compute/MaterialTextureExchange/Source/MaterialTextureExchange.cpp",
        "LayerContentSource::TexturedImpressions",
        "Layer.Textured.Texels.assign",
        "ChannelPlacement Placement",
        "the brush writes a channel the layer does not own",
        "Stroke.Seal(Layers, Revisions, Residency, SealedAt)",
        "AddressOf(CellIndex)",
        "MaterialProcessingExchange Processing",
        "Processing.Compare(*Previous, Current)",
    )
    require("ParametricSketchHost" not in source and "TexturingPanel" not in source,
            "texture exchange source must stay host/UI independent")

    # The existing stroke path must still enforce one transaction, speculative refusal, and inverse extents.
    require_text(
        "Engine/SlateCompute/Compute/ImpressionSequence/Source/ImpressionSequence.cpp",
        "Revised.Open(\"\", \"TextureStroke\")",
        "a speculative extent never enters the revision sequence",
        "PriorTexels.assign",
        "Residency.DeclareUncommitted(CellIndex, false)",
    )

    subprocess.run([
        "g++", "-std=c++20", "-fsyntax-only", "-IEngine",
        "Engine/SlateCompute/Compute/MaterialTextureExchange/Source/MaterialTextureExchange.cpp",
        "Engine/SlateCompute/Compute/ImpressionSequence/Source/ImpressionSequence.cpp",
        "Engine/SlateCompute/Compute/StrokeSpace/Source/StrokeSpace.cpp",
        "Engine/SlateDocument/Document/SurfaceLayerSequence/Source/SurfaceLayerSequence.cpp",
        "Engine/SlateCompute/Compute/MaterialProcessingExchange/Source/MaterialProcessingExchange.cpp",
    ], cwd=ROOT, check=True)

    print("[MaterialSystemPass5] textured layers, stroke declarations, commit sealing and dirty tiles hold")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[MaterialSystemPass5] failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
