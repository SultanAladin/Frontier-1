//============================================================================================================================================
//                                                       MATERIALPAINTEXCHANGE.CPP
//============================================================================================================================================

#include "SlateCompute/Compute/MaterialTextureExchange/Api/MaterialTextureExchange.h"

#include <algorithm>

namespace Slate
{
namespace
{

std::uint32_t ChannelBit(ChannelSubject Channel)
{
    return Channel == ChannelSubject::ChannelCount ? 0u : (1u << static_cast<std::uint32_t>(Channel));
}

std::uint32_t BrushChannelMask(const BrushSpecification& Brush)
{
    std::uint32_t Mask = 0u;
    for (const BrushChannelValue& Channel : Brush.Channels())
        Mask |= ChannelBit(Channel.Channel);
    return Mask;
}

std::uint32_t SpanFor(const BrushChannelValue& Channel)
{
    return Channel.ColourDeclared ? 3u : 1u;
}

} // namespace

Deliver<LayerIdentity> MaterialTextureExchange::CreateTexturedLayer(
    SurfaceLayerSequence& Layers,
    const MaterialTextureLayerDeclaration& Declaring) const
{
    if (Declaring.ChannelMask == 0u)
        return Deliver<LayerIdentity>::Refuse({ RefusalReason::ContentUnsupported, "a textured material layer writes no channel" });
    if (Declaring.WorkingExtent == 0u || Declaring.WorkingExtent > MaximumWorkingEdge)
        return Deliver<LayerIdentity>::Refuse({ RefusalReason::ExtentExhausted, "the textured layer extent is unsupported" });
    if (Declaring.ComponentCount == 0u)
        return Deliver<LayerIdentity>::Refuse({ RefusalReason::ContentUnsupported, "a textured material layer has no components" });

    LayerSpecification Layer;
    Layer.Name = Declaring.Name.empty() ? "Texture Layer" : Declaring.Name;
    Layer.Source = LayerContentSource::TexturedImpressions;
    Layer.ChannelMask = Declaring.ChannelMask;
    Layer.PresenceEnabled = true;
    Layer.Textured.ExtentTexels = Declaring.WorkingExtent;
    Layer.Textured.ComponentCount = Declaring.ComponentCount;
    Layer.Textured.Texels.assign(static_cast<std::size_t>(Declaring.WorkingExtent)
                              * Declaring.WorkingExtent
                              * Declaring.ComponentCount, 0.0f);

    return Layers.Append(Layer);
}

Deliver<StrokeDeclaration> MaterialTextureExchange::DeclareStroke(const SurfaceLayerSequence& Layers,
                                                                LayerIdentity Subject,
                                                                const BrushSpecification& Brush,
                                                                std::uint32_t WorkingExtent,
                                                                std::uint32_t ComponentCount,
                                                                std::uint32_t StrokeSeed,
                                                                bool Speculative) const
{
    const Deliver<const LayerSpecification*> Layer = Layers.Resolve(Subject);
    if (!Layer.Resolved) return Deliver<StrokeDeclaration>::Refuse(Layer.Error);
    if (Layer.Resolve()->Source != LayerContentSource::TexturedImpressions)
        return Deliver<StrokeDeclaration>::Refuse({ RefusalReason::ContentUnsupported, "the stroke target is not a textured layer" });
    if (Layer.Resolve()->Textured.ExtentTexels != WorkingExtent || Layer.Resolve()->Textured.ComponentCount != ComponentCount)
        return Deliver<StrokeDeclaration>::Refuse({ RefusalReason::ContentUnsupported, "the stroke declaration does not match the textured layer extent" });

    StrokeDeclaration Declared;
    Declared.Subject = Subject;
    Declared.WorkingExtent = WorkingExtent;
    Declared.ComponentCount = ComponentCount;
    Declared.StrokeSeed = StrokeSeed == 0u ? 1u : StrokeSeed;
    Declared.Speculative = Speculative;

    std::uint32_t Cursor = 0u;
    for (const BrushChannelValue& Channel : Brush.Channels())
    {
        if ((Layer.Resolve()->ChannelMask & ChannelBit(Channel.Channel)) == 0u)
            return Deliver<StrokeDeclaration>::Refuse({ RefusalReason::ContentUnsupported, "the brush writes a channel the layer does not own" });

        const std::uint32_t Span = SpanFor(Channel);
        if (Cursor + Span > ComponentCount)
            return Deliver<StrokeDeclaration>::Refuse({ RefusalReason::ContentUnsupported, "the brush channels do not fit the textured layer packing" });

        ChannelPlacement Placement;
        Placement.Channel = Channel.Channel;
        Placement.ComponentIndex = Cursor;
        Placement.ComponentSpan = Span;
        Declared.Placements.push_back(Placement);
        Cursor += Span;
    }

    if (Declared.Placements.empty())
        return Deliver<StrokeDeclaration>::Refuse({ RefusalReason::ContentUnsupported, "the brush writes no channel" });

    return Deliver<StrokeDeclaration>::Result(Declared);
}

std::vector<MaterialTextureDirtyTile> MaterialTextureExchange::DirtyTilesOf(const SealedStroke& Stroke,
                                                                        std::uint32_t ChannelMask) const
{
    std::vector<MaterialTextureDirtyTile> Tiles;
    Tiles.reserve(Stroke.TouchedCells.size());
    for (const std::uint32_t CellIndex : Stroke.TouchedCells)
    {
        const Deliver<CellAddress> Addressed = AddressOf(CellIndex);
        if (!Addressed.Resolved) continue;
        MaterialTextureDirtyTile Tile;
        Tile.CellIndex = CellIndex;
        Tile.Level = Stroke.TexturingLevel;
        Tile.OriginX = Addressed.Resolve().X * CoverageTileTexels;
        Tile.OriginY = Addressed.Resolve().Y * CoverageTileTexels;
        Tile.Extent = CoverageTileTexels;
        Tile.ChannelMask = ChannelMask;
        Tiles.push_back(Tile);
    }
    return Tiles;
}

Deliver<MaterialTextureCommitReport> MaterialTextureExchange::CommitResolvedStroke(
    ImpressionSequence& Stroke,
    MaterialSpecification& Material,
    SurfaceLayerSequence& Layers,
    RevisionSequence& Revisions,
    SurfaceTileSpace& Residency,
    std::uint64_t SealedAt,
    const MaterialProcessingSnapshot* Previous) const
{
    if (!Stroke.StrokeOpen())
        return Deliver<MaterialTextureCommitReport>::Refuse({ RefusalReason::HostDenied, "no material texture stroke is open" });

    const Deliver<SealedStroke> Sealed = Stroke.Seal(Layers, Revisions, Residency, SealedAt);
    if (!Sealed.Resolved) return Deliver<MaterialTextureCommitReport>::Refuse(Sealed.Error);

    MaterialTextureCommitReport Report;
    Report.Stroke = Sealed.Resolve();
    std::uint32_t DirtyChannels = 0u;
    for (const BrushChannelValue& Channel : Report.Stroke.Recorded.Channels)
        DirtyChannels |= ChannelBit(Channel.Channel);
    Report.DirtyChannelMask = DirtyChannels;
    Report.DirtyTiles = DirtyTilesOf(Report.Stroke, DirtyChannels);

    MaterialProcessingExchange Processing;
    const MaterialProcessingSnapshot Current = Processing.Capture(Material, Layers);
    Report.AfterFingerprint = Current.DirtyKey.Combined;
    if (Previous != nullptr)
    {
        Report.BeforeFingerprint = Previous->DirtyKey.Combined;
        Report.Dirty = Processing.Compare(*Previous, Current);
    }
    else
    {
        Report.Dirty.ChannelMask = DirtyChannels;
        Report.Dirty.LayersChanged = true;
    }

    return Deliver<MaterialTextureCommitReport>::Result(Report);
}

} // namespace Slate
