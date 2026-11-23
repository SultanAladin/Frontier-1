//============================================================================================================================================
//                                                     MATERIALLAYERPROJECTION.H
//============================================================================================================================================
// 🧩 Turns the texture layer stack the artist sees into the material layers a document stores.
//
// 🔴 THIS WAS `Engine/Application/Api/MaterialLayerStackBridge.h` — 175 `inline` lines in the APPLICATION
//    layer, described by its own header as an "editor-only bridge". It was neither editor-only nor a
//    bridge: it is the rule for how a layer stack becomes material layers, and that rule is the same
//    wherever it is asked. Living in `Application/` meant exactly one host could reach it, and any second
//    caller would have had to copy it — which is how the four defects in `HostDebtPlan.md` §4 all began.
//
// 📝 `SlateWorkspace` is the layer that may see BOTH `SlateUI`'s presentation and `SlateDocument`'s
//    records, which is precisely what this transformation reads and writes. "Bridge" named the file, not
//    the behaviour, so the names say what it does now: it PROJECTS a stack onto material layers.

#pragma once

#include "SlateCompute/Compute/MaterialProcessingExchange/Api/MaterialProcessingExchange.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateDocument/Document/SurfaceLayerSequence/Api/SurfaceLayerSequence.h"
#include "SlateUI/Interface/TexturingPanel/Api/TexturingPanel.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace Slate
{

struct MaterialLayerProjectionReport
{
    std::uint32_t MirroredLayerCount = 0u;
    std::uint32_t MaskedLayerCount = 0u;
    LayerIdentity BaseLayer = {};
    MaterialProcessingSnapshot Snapshot = {};
    MaterialProcessingDirtySet Dirty = {};
    bool HadPreviousSnapshot = false;
};

ChannelSubject TextureChannelToMaterialChannel(std::uint32_t ChannelIndex);

std::uint32_t MaterialChannelBit(ChannelSubject Channel);

std::uint32_t TextureRowChannelMask(const TexturingContext& Context,
                                           std::uint32_t RowIndex,
                                           const TextureLayerRow& Row);

std::uint32_t TextureRowMaskChannelMask(const TexturingContext& Context,
                                               std::uint32_t RowIndex,
                                               std::uint32_t LayerChannelMask);

LayerContentSource TextureLayerSource(TextureLayerClassification Classified);

CoverageSpecification TextureRowCoverage(const TexturingContext& Context,
                                                std::uint32_t RowIndex,
                                                const TextureLayerRow& Row,
                                                std::uint32_t LayerChannelMask);

MaterialLayerProjectionReport ProjectMaterialLayersFromTextureStack(
    MaterialSpecification& Material,
    SurfaceLayerSequence& Layers,
    const TexturingStack& Stack,
    const TexturingContext& Context,
    const MaterialProcessingExchange& Exchange,
    const MaterialProcessingSnapshot* PreviousSnapshot);

}   // namespace Slate
