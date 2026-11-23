//============================================================================================================================================
//                                                    MATERIALLAYERPROJECTION.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/MaterialLayerProjection/Api/MaterialLayerProjection.h"

namespace Slate
{


ChannelSubject TextureChannelToMaterialChannel(std::uint32_t ChannelIndex)
{
    switch (ChannelIndex)
    {
        case 0u:  return ChannelSubject::AlbedoColour;
        case 1u:  return ChannelSubject::Metallic;
        case 2u:  return ChannelSubject::Roughness;
        case 3u:  return ChannelSubject::SurfaceOrientation;
        case 4u:  return ChannelSubject::Displacement;
        case 5u:  return ChannelSubject::AmbientOcclusion;
        case 6u:  return ChannelSubject::Emission;
        case 7u:  return ChannelSubject::Opacity;
        case 8u:  return ChannelSubject::Anisotropy;
        case 9u:  return ChannelSubject::AnisotropyDirection;
        case 10u: return ChannelSubject::ClearCoat;
        case 11u: return ChannelSubject::RefractionRatio;
        case 12u: return ChannelSubject::SheenColour;
        case 13u: return ChannelSubject::SubsurfaceColour;
        default:  return ChannelSubject::ChannelCount;
    }
}

std::uint32_t MaterialChannelBit(ChannelSubject Channel)
{
    return Channel == ChannelSubject::ChannelCount ? 0u : (1u << static_cast<std::uint32_t>(Channel));
}

std::uint32_t TextureRowChannelMask(const TexturingContext& Context,
                                           std::uint32_t RowIndex,
                                           const TextureLayerRow& Row)
{
    std::uint32_t Mask = 0u;
    if (RowIndex < TextureLayerLimit)
    {
        for (std::uint32_t ChannelIndex = 0u; ChannelIndex < TextureChannelLimit; ++ChannelIndex)
        {
            if (Context.ChannelOn[RowIndex][ChannelIndex])
                Mask |= MaterialChannelBit(TextureChannelToMaterialChannel(ChannelIndex));
        }
    }

    if (Mask != 0u) return Mask;

    for (std::uint32_t ChannelIndex = 0u; ChannelIndex < Row.ChannelCount && ChannelIndex < TextureChannelLimit; ++ChannelIndex)
        Mask |= MaterialChannelBit(TextureChannelToMaterialChannel(ChannelIndex));

    return Mask == 0u ? MaterialChannelBit(ChannelSubject::AlbedoColour) : Mask;
}

std::uint32_t TextureRowMaskChannelMask(const TexturingContext& Context,
                                               std::uint32_t RowIndex,
                                               std::uint32_t LayerChannelMask)
{
    std::uint32_t Mask = 0u;
    if (RowIndex < TextureLayerLimit)
    {
        for (std::uint32_t ChannelIndex = 0u; ChannelIndex < TextureChannelLimit; ++ChannelIndex)
        {
            if (Context.MaskChannel[RowIndex][ChannelIndex])
                Mask |= MaterialChannelBit(TextureChannelToMaterialChannel(ChannelIndex));
        }
    }
    return Mask == 0u ? LayerChannelMask : Mask;
}

LayerContentSource TextureLayerSource(TextureLayerClassification Classified)
{
    switch (Classified)
    {
        case TextureLayerClassification::Material:
        case TextureLayerClassification::Fill:
            return LayerContentSource::MaterialConstants;
        case TextureLayerClassification::Brushed:
            return LayerContentSource::TexturedImpressions;
        case TextureLayerClassification::Folder:
            return LayerContentSource::NestedSequence;
        case TextureLayerClassification::Pattern:
        case TextureLayerClassification::Generator:
        case TextureLayerClassification::Adjustment:
        case TextureLayerClassification::Filter:
        case TextureLayerClassification::Decal:
        default:
            return LayerContentSource::AnalyticResolution;
    }
}

CoverageSpecification TextureRowCoverage(const TexturingContext& Context,
                                                std::uint32_t RowIndex,
                                                const TextureLayerRow& Row,
                                                std::uint32_t LayerChannelMask)
{
    CoverageSpecification Coverage;
    const bool Attached = Row.MaskDeclared || (RowIndex < TextureLayerLimit && Context.MaskAttached[RowIndex]);
    if (!Attached) return Coverage;

    Coverage.CoverageDeclared = true;
    Coverage.Source = LayerContentSource::AnalyticResolution;
    Coverage.UniformStrength = static_cast<double>(std::min<std::uint32_t>(Context.MaskDensity[RowIndex], 100u)) / 100.0;
    Coverage.ChannelMask = TextureRowMaskChannelMask(Context, RowIndex, LayerChannelMask);
    Coverage.Inverted = Row.MaskInverted || (RowIndex < TextureLayerLimit && Context.MaskInverted[RowIndex]);
    return Coverage;
}

MaterialLayerProjectionReport ProjectMaterialLayersFromTextureStack(
    MaterialSpecification& Material,
    SurfaceLayerSequence& Layers,
    const TexturingStack& Stack,
    const TexturingContext& Context,
    const MaterialProcessingExchange& Exchange,
    const MaterialProcessingSnapshot* PreviousSnapshot)
{
    MaterialLayerProjectionReport Report;
    Layers = SurfaceLayerSequence{};
    const Deliver<LayerIdentity> Base = Exchange.InitialiseDielectric(Material, Layers);
    if (Base.Resolved) Report.BaseLayer = Base.Resolve();

    for (std::uint32_t RowIndex = 0u; RowIndex < Stack.Count && RowIndex < TextureLayerLimit; ++RowIndex)
    {
        const TextureLayerRow& Row = Stack.Rows[RowIndex];
        if (Row.Classified == TextureLayerClassification::Folder)
            continue;

        LayerSpecification Layer;
        Layer.Name = Row.Naming ? Row.Naming : "Layer";
        Layer.Source = TextureLayerSource(Row.Classified);
        Layer.ChannelMask = TextureRowChannelMask(Context, RowIndex, Row);
        Layer.PresenceEnabled = Row.Opacity > 0u;
        Layer.Coverage = TextureRowCoverage(Context, RowIndex, Row, Layer.ChannelMask);

        const Deliver<LayerIdentity> Added = Layers.Append(Layer);
        if (!Added.Resolved) continue;

        ++Report.MirroredLayerCount;
        if (Layer.Coverage.CoverageDeclared) ++Report.MaskedLayerCount;
    }

    Report.Snapshot = Exchange.Capture(Material, Layers);
    if (PreviousSnapshot != nullptr)
    {
        Report.HadPreviousSnapshot = true;
        Report.Dirty = Exchange.Compare(*PreviousSnapshot, Report.Snapshot);
    }
    return Report;
}


}   // namespace Slate
