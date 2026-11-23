//============================================================================================================================================
//                                                        MATERIALPAINTEXCHANGE.H
//============================================================================================================================================
// 🧩 Material texturing command seam: textured layer creation, committed-stroke dirty regions, and snapshot dirtiness.
//    UI tools stay outside this component; this is document/compute plumbing only.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateCompute/Compute/ImpressionSequence/Api/ImpressionSequence.h"
#include "SlateCompute/Compute/MaterialProcessingExchange/Api/MaterialProcessingExchange.h"
#include "SlateDocument/Document/BrushSpecification/Api/BrushSpecification.h"
#include "SlateDocument/Document/RevisionSequence/Api/RevisionSequence.h"
#include "SlateDocument/Document/SurfaceLayerSequence/Api/SurfaceLayerSequence.h"

#include <cstdint>
#include <vector>

namespace Slate
{

struct MaterialTextureLayerDeclaration
{
    std::string Name = "Texture Layer";
    std::uint32_t ChannelMask = 0u;
    std::uint32_t WorkingExtent = MaximumWorkingEdge;
    std::uint32_t ComponentCount = 4u;
};

struct MaterialTextureDirtyTile
{
    std::uint32_t CellIndex = 0u;
    std::uint32_t Level = 0u;
    std::uint32_t OriginX = 0u;
    std::uint32_t OriginY = 0u;
    std::uint32_t Extent = CoverageTileTexels;
    std::uint32_t ChannelMask = 0u;
};

struct MaterialTextureCommitReport
{
    SealedStroke Stroke = {};
    MaterialProcessingDirtySet Dirty = {};
    std::vector<MaterialTextureDirtyTile> DirtyTiles = {};
    std::uint32_t DirtyChannelMask = 0u;
    std::uint64_t BeforeFingerprint = 0u;
    std::uint64_t AfterFingerprint = 0u;
};

class MaterialTextureExchange
{
public:
    /// 🧩 Creates a document-owned textured layer with a full authored texel span and stable layer identity.
    Deliver<LayerIdentity> CreateTexturedLayer(SurfaceLayerSequence& Layers,
                                              const MaterialTextureLayerDeclaration& Declaring) const;

    /// 🧩 Builds the stroke declaration for a textured material layer without reading UI state.
    Deliver<StrokeDeclaration> DeclareStroke(const SurfaceLayerSequence& Layers,
                                             LayerIdentity Subject,
                                             const BrushSpecification& Brush,
                                             std::uint32_t WorkingExtent,
                                             std::uint32_t ComponentCount,
                                             std::uint32_t StrokeSeed,
                                             bool Speculative = false) const;

    /// 🧩 Commits an already-resolved stroke and reports precise tile/channel dirtiness.
    Deliver<MaterialTextureCommitReport> CommitResolvedStroke(ImpressionSequence& Stroke,
                                                            MaterialSpecification& Material,
                                                            SurfaceLayerSequence& Layers,
                                                            RevisionSequence& Revisions,
                                                            SurfaceTileSpace& Residency,
                                                            std::uint64_t SealedAt,
                                                            const MaterialProcessingSnapshot* Previous = nullptr) const;

    /// 🧩 Converts a sealed stroke into dirty tile records without replaying the stroke.
    std::vector<MaterialTextureDirtyTile> DirtyTilesOf(const SealedStroke& Stroke,
                                                     std::uint32_t ChannelMask) const;
};

} // namespace Slate
