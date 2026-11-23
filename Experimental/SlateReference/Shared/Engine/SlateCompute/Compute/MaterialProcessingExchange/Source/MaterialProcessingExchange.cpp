//============================================================================================================================================
//                                                MATERIALPROCESSINGEXCHANGE.CPP
//============================================================================================================================================

#include "SlateCompute/Compute/MaterialProcessingExchange/Api/MaterialProcessingExchange.h"

#include <algorithm>
#include <cstring>

namespace Slate
{
namespace
{
constexpr std::uint32_t ChannelBit(ChannelSubject Channel)
{
    return 1u << static_cast<std::uint32_t>(Channel);
}

ChannelSpecification Scalar(double Value, double Default)
{
    ChannelSpecification Declared;
    Declared.Source = ChannelSource::Constant;
    Declared.Measured = ChannelMeasure::Scalar;
    Declared.ConstantScalar = Value;
    Declared.DefaultScalar = Default;
    Declared.LowerMagnitude = 0.0;
    Declared.UpperMagnitude = 1.0;
    return Declared;
}

ChannelSpecification Colour(double Red, double Green, double Blue)
{
    ChannelSpecification Declared;
    Declared.Source = ChannelSource::Constant;
    Declared.Measured = ChannelMeasure::Reflectance;
    Declared.ConstantColour = { Red, Green, Blue, WorkingSpaceIdentity };
    Declared.DefaultColour = Declared.ConstantColour;
    Declared.LowerMagnitude = 0.0;
    Declared.UpperMagnitude = 1.0;
    return Declared;
}

constexpr std::uint64_t HashSeed = 1469598103934665603ull;
constexpr std::uint64_t HashPrime = 1099511628211ull;

void HashBytes(std::uint64_t& Hash, const void* Data, std::size_t Span)
{
    const auto* Bytes = static_cast<const unsigned char*>(Data);
    for (std::size_t ByteIndex = 0u; ByteIndex < Span; ++ByteIndex)
    {
        Hash ^= Bytes[ByteIndex];
        Hash *= HashPrime;
    }
}

template <typename ValueType>
void HashValue(std::uint64_t& Hash, const ValueType& Value)
{
    HashBytes(Hash, &Value, sizeof(Value));
}

void HashColour(std::uint64_t& Hash, const ColourSpecification& Colour)
{
    HashValue(Hash, Colour.RedCoordinate);
    HashValue(Hash, Colour.GreenCoordinate);
    HashValue(Hash, Colour.BlueCoordinate);
    HashValue(Hash, Colour.SpaceIdentity);
}

std::uint64_t HashChannel(const ChannelSpecification& Channel)
{
    std::uint64_t Hash = HashSeed;
    HashValue(Hash, Channel.Source);
    HashValue(Hash, Channel.Measured);
    HashValue(Hash, Channel.SourceIndex);
    HashValue(Hash, Channel.ConstantScalar);
    HashColour(Hash, Channel.ConstantColour);
    HashValue(Hash, Channel.DefaultScalar);
    HashColour(Hash, Channel.DefaultColour);
    HashValue(Hash, Channel.LowerMagnitude);
    HashValue(Hash, Channel.UpperMagnitude);
    HashValue(Hash, Channel.ChannelDeclared);
    return Hash;
}

void HashPhysicalDeclaration(std::uint64_t& Hash, const PhysicalSurfaceDeclaration& Declaration)
{
    HashValue(Hash, Declaration.Closure);
    HashValue(Hash, Declaration.Features);
    HashValue(Hash, Declaration.Coverage);
    HashValue(Hash, Declaration.Wall);
    HashValue(Hash, Declaration.Interface);
    HashValue(Hash, Declaration.TwoSided);
}

void HashTextured(std::uint64_t& Hash, const TexturedContent& Textured)
{
    HashValue(Hash, Textured.ExtentTexels);
    HashValue(Hash, Textured.ComponentCount);
    const std::uint64_t TexelCount = static_cast<std::uint64_t>(Textured.Texels.size());
    HashValue(Hash, TexelCount);
    if (!Textured.Texels.empty())
        HashBytes(Hash, Textured.Texels.data(), Textured.Texels.size() * sizeof(float));
}

void HashLayer(std::uint64_t& Hash, const MaterialProcessingLayerSnapshot& Snapshot)
{
    const LayerSpecification& Layer = Snapshot.Layer;
    HashValue(Hash, Snapshot.Depth);
    HashValue(Hash, Snapshot.Position);
    HashValue(Hash, Layer.Identity.SlotIndex);
    HashValue(Hash, Layer.Identity.SlotGeneration);
    HashValue(Hash, Layer.Source);
    HashValue(Hash, Layer.SourceIndex);
    HashValue(Hash, Layer.NestedIndex);
    HashValue(Hash, Layer.ChannelMask);
    HashValue(Hash, Layer.Combination);
    HashValue(Hash, Layer.Coverage.Source);
    HashValue(Hash, Layer.Coverage.SourceIndex);
    HashTextured(Hash, Layer.Coverage.Textured);
    HashValue(Hash, Layer.Coverage.UniformStrength);
    HashValue(Hash, Layer.Coverage.ChannelMask);
    HashValue(Hash, Layer.Coverage.Inverted);
    HashValue(Hash, Layer.Coverage.CoverageDeclared);
    HashTextured(Hash, Layer.Textured);
    const std::uint64_t NameLength = static_cast<std::uint64_t>(Layer.Name.size());
    HashValue(Hash, NameLength);
    HashBytes(Hash, Layer.Name.data(), Layer.Name.size());
    HashValue(Hash, Layer.PresenceEnabled);
    HashValue(Hash, Layer.Mandatory);
    HashValue(Hash, Layer.ResampleOwed);
}

void CaptureLayers(const SurfaceLayerSequence& Sequence,
                   std::uint32_t Depth,
                   std::vector<MaterialProcessingLayerSnapshot>& Captured)
{
    const std::vector<LayerSpecification>& Entries = Sequence.Entries();
    for (std::size_t Position = 0u; Position < Entries.size(); ++Position)
    {
        Captured.push_back({ Entries[Position], Depth, static_cast<std::uint32_t>(Position) });
        if (Entries[Position].Source != LayerContentSource::NestedSequence) continue;

        const Deliver<const SurfaceLayerSequence*> Nested = Sequence.Nested(Entries[Position].NestedIndex);
        if (Nested.Resolved) CaptureLayers(*Nested.Resolve(), Depth + 1u, Captured);
    }
}
}

Deliver<LayerIdentity> MaterialProcessingExchange::InitialiseDielectric(MaterialSpecification& Material,
                                                                        SurfaceLayerSequence& Layers) const
{
    if (Layers.EntryCount() != 0u)
        return Deliver<LayerIdentity>::Refuse(
            { RefusalReason::HostDenied, "a material layer sequence already stands" });

    Material.DeclareReflectance(ReflectanceSelection::Standard);
    const struct ChannelDeclaration
    {
        ChannelSubject Subject;
        ChannelSpecification Specification;
    } Declared[] = {
        { ChannelSubject::AlbedoColour, Colour(1.0, 1.0, 1.0) },
        { ChannelSubject::Metallic, Scalar(0.0, 0.0) },
        { ChannelSubject::Roughness, Scalar(0.5, 0.5) },
        { ChannelSubject::NormalIncidenceReflectance, Scalar(0.04, 0.04) },
        { ChannelSubject::AmbientOcclusion, Scalar(1.0, 1.0) },
        { ChannelSubject::Emission, Colour(0.0, 0.0, 0.0) },
        { ChannelSubject::Opacity, Scalar(1.0, 1.0) }
    };

    std::uint32_t Mask = 0u;
    for (const ChannelDeclaration& Channel : Declared)
    {
        const Deliver<bool> Accepted = Material.DeclareChannel(Channel.Subject, Channel.Specification);
        if (!Accepted.Resolved)
            return Deliver<LayerIdentity>::Refuse(Accepted.Error);
        Mask |= ChannelBit(Channel.Subject);
    }

    LayerSpecification Base;
    Base.Source = LayerContentSource::MaterialConstants;
    Base.ChannelMask = Mask;
    Base.Combination = CombineSpecification::Over;
    Base.Name = "Base Material";
    Base.PresenceEnabled = true;
    Base.Mandatory = true;
    return Layers.Append(Base);
}

Deliver<const LayerSpecification*> MaterialProcessingExchange::BaseLayer(const SurfaceLayerSequence& Layers,
                                                                         LayerIdentity Layer) const
{
    const Deliver<const LayerSpecification*> Resolved = Layers.Resolve(Layer);
    if (!Resolved.Resolved) return Resolved;
    if (!Resolved.Resolve()->Mandatory ||
        Resolved.Resolve()->Source != LayerContentSource::MaterialConstants)
    {
        return Deliver<const LayerSpecification*>::Refuse(
            { RefusalReason::HostDenied, "material constants may only be edited through the base material layer" });
    }
    return Resolved;
}

Deliver<bool> MaterialProcessingExchange::DeclareScalar(MaterialSpecification& Material,
                                                        const SurfaceLayerSequence& Layers,
                                                        LayerIdentity Layer, ChannelSubject Channel,
                                                        double Value) const
{
    const Deliver<const LayerSpecification*> Base = BaseLayer(Layers, Layer);
    if (!Base.Resolved) return Deliver<bool>::Refuse(Base.Error);
    if (!EntryWritesChannel(*Base.Resolve(), Channel))
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the base layer does not declare this channel" });

    ChannelSpecification Amended = Material.Channel(Channel);
    if (Amended.Measured != ChannelMeasure::Scalar)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the channel is not scalar" });
    Amended.Source = ChannelSource::Constant;
    Amended.ConstantScalar = std::clamp(Value, Amended.LowerMagnitude, Amended.UpperMagnitude);
    return Material.DeclareChannel(Channel, Amended);
}

Deliver<bool> MaterialProcessingExchange::DeclareColour(MaterialSpecification& Material,
                                                        const SurfaceLayerSequence& Layers,
                                                        LayerIdentity Layer, ChannelSubject Channel,
                                                        ColourSpecification Value) const
{
    const Deliver<const LayerSpecification*> Base = BaseLayer(Layers, Layer);
    if (!Base.Resolved) return Deliver<bool>::Refuse(Base.Error);
    if (!EntryWritesChannel(*Base.Resolve(), Channel))
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the base layer does not declare this channel" });
    if (!Value.ColourDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the colour declares no space" });

    ChannelSpecification Amended = Material.Channel(Channel);
    if (!MeasureCarriesColour(Amended.Measured))
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the channel does not carry colour" });
    Amended.Source = ChannelSource::Constant;
    Amended.ConstantColour = Value;
    return Material.DeclareChannel(Channel, Amended);
}

Deliver<MaterialLayerCommandResult> MaterialProcessingExchange::ApplyLayerCommand(
    SurfaceLayerSequence& Layers,
    const MaterialLayerCommand& Command) const
{
    MaterialLayerCommandResult Result;
    Result.Action = Command.Action;

    switch (Command.Action)
    {
        case MaterialLayerCommandAction::AddLayer:
        {
            LayerSpecification Layer = Command.Layer;
            if (Layer.Name.empty()) Layer.Name = "Layer";
            if (Layer.Source == LayerContentSource::TexturedImpressions && Layer.Textured.Texels.empty())
                Layer.Source = LayerContentSource::MaterialConstants;
            const Deliver<LayerIdentity> Added = Layers.Append(Layer);
            if (!Added.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Added.Error);
            Result.Identity = Added.Resolve();
            Result.StructuralChange = true;
            return Deliver<MaterialLayerCommandResult>::Result(Result);
        }
        case MaterialLayerCommandAction::AddFolder:
        {
            const Deliver<std::uint32_t> Nested = Layers.Nest();
            if (!Nested.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Nested.Error);
            LayerSpecification Folder = Command.Layer;
            Folder.Name = Folder.Name.empty() ? "Folder" : Folder.Name;
            Folder.Source = LayerContentSource::NestedSequence;
            Folder.NestedIndex = Nested.Resolve();
            const Deliver<LayerIdentity> Added = Layers.Append(Folder);
            if (!Added.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Added.Error);
            Result.Identity = Added.Resolve();
            Result.NestedIndex = Nested.Resolve();
            Result.StructuralChange = true;
            return Deliver<MaterialLayerCommandResult>::Result(Result);
        }
        case MaterialLayerCommandAction::Remove:
        {
            const Deliver<std::uint32_t> Position = Layers.PositionOf(Command.Subject);
            const Deliver<LayerSpecification> Removed = Layers.Withdraw(Command.Subject);
            if (!Removed.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Removed.Error);
            Result.Identity = Command.Subject;
            Result.PreviousPosition = Position.Resolved ? Position.Resolve() : 0u;
            Result.StructuralChange = true;
            return Deliver<MaterialLayerCommandResult>::Result(Result);
        }
        case MaterialLayerCommandAction::Duplicate:
        {
            const Deliver<const LayerSpecification*> Source = Layers.Resolve(Command.Subject);
            if (!Source.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Source.Error);
            LayerSpecification Copy = *Source.Resolve();
            Copy.Identity = {};
            Copy.Mandatory = false;
            if (!Command.Name.empty()) Copy.Name = Command.Name;
            const Deliver<LayerIdentity> Added = Layers.Append(Copy);
            if (!Added.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Added.Error);
            Result.Identity = Added.Resolve();
            Result.StructuralChange = true;
            return Deliver<MaterialLayerCommandResult>::Result(Result);
        }
        case MaterialLayerCommandAction::Reorder:
        {
            const Deliver<std::uint32_t> Previous = Layers.Reorder(Command.Subject, Command.Position);
            if (!Previous.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Previous.Error);
            Result.Identity = Command.Subject;
            Result.PreviousPosition = Previous.Resolve();
            Result.StructuralChange = true;
            return Deliver<MaterialLayerCommandResult>::Result(Result);
        }
        case MaterialLayerCommandAction::MoveToFolder:
        {
            const Deliver<SurfaceLayerSequence*> Nested = Layers.AmendNested(Command.NestedIndex);
            if (!Nested.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Nested.Error);
            const Deliver<LayerSpecification> Moving = Layers.Withdraw(Command.Subject);
            if (!Moving.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Moving.Error);
            LayerSpecification Layer = Moving.Resolve();
            Layer.Identity = {};
            Layer.Mandatory = false;
            const Deliver<LayerIdentity> Added = Nested.Resolve()->Append(Layer);
            if (!Added.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Added.Error);
            Result.Identity = Added.Resolve();
            Result.StructuralChange = true;
            return Deliver<MaterialLayerCommandResult>::Result(Result);
        }
        case MaterialLayerCommandAction::Rename:
        {
            const Deliver<std::string> Renamed = Layers.DeclareName(Command.Subject, Command.Name);
            if (!Renamed.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Renamed.Error);
            Result.Identity = Command.Subject;
            return Deliver<MaterialLayerCommandResult>::Result(Result);
        }
        case MaterialLayerCommandAction::Presence:
        {
            const Deliver<bool> Presence = Layers.DeclarePresence(Command.Subject, Command.PresenceEnabled);
            if (!Presence.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Presence.Error);
            Result.Identity = Command.Subject;
            return Deliver<MaterialLayerCommandResult>::Result(Result);
        }
        case MaterialLayerCommandAction::Source:
        {
            const Deliver<LayerContentSource> Source = Layers.DeclareSource(Command.Subject, Command.Source);
            if (!Source.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Source.Error);
            Result.Identity = Command.Subject;
            return Deliver<MaterialLayerCommandResult>::Result(Result);
        }
        case MaterialLayerCommandAction::ChannelMask:
        {
            const Deliver<std::uint32_t> Mask = Layers.DeclareChannelMask(Command.Subject, Command.ChannelMask);
            if (!Mask.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Mask.Error);
            Result.Identity = Command.Subject;
            return Deliver<MaterialLayerCommandResult>::Result(Result);
        }
        case MaterialLayerCommandAction::Coverage:
        {
            const Deliver<CoverageSpecification> Coverage = Layers.DeclareCoverage(Command.Subject, Command.Coverage);
            if (!Coverage.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Coverage.Error);
            Result.Identity = Command.Subject;
            return Deliver<MaterialLayerCommandResult>::Result(Result);
        }
        case MaterialLayerCommandAction::Combination:
        {
            const Deliver<CombineSpecification> Combination = Layers.DeclareCombination(Command.Subject, Command.Combination);
            if (!Combination.Resolved) return Deliver<MaterialLayerCommandResult>::Refuse(Combination.Error);
            Result.Identity = Command.Subject;
            return Deliver<MaterialLayerCommandResult>::Result(Result);
        }
    }

    return Deliver<MaterialLayerCommandResult>::Refuse(
        { RefusalReason::ContentUnsupported, "no such material layer command" });
}

MaterialProcessingSnapshot MaterialProcessingExchange::Capture(const MaterialSpecification& Material,
                                                               const SurfaceLayerSequence& Layers,
                                                               const PhysicalSurfaceDeclaration& PhysicalDeclaration) const
{
    MaterialProcessingSnapshot Captured;
    Captured.Material = Material;
    Captured.PhysicalDeclaration = PhysicalDeclaration;
    const Deliver<CompiledPhysicalSurface> Physical = PhysicalSurfaceExchange().Compile(Material, PhysicalDeclaration);
    if (Physical.Resolved)
    {
        Captured.PhysicalSurface = Physical.Resolve();
        Captured.PhysicalSurfaceResolved = true;
        const Deliver<PhysicalSurfacePacket> Binding = BindPhysicalSurface(Captured.PhysicalSurface);
        if (Binding.Resolved)
        {
            Captured.PhysicalPacket = Binding.Resolve();
            Captured.PhysicalPacketResolved = true;
        }
    }
    CaptureLayers(Layers, 0u, Captured.Layers);

    Captured.DirtyKey.Reflectance = HashSeed;
    const ReflectanceSelection Selected = Material.Reflectance();
    HashValue(Captured.DirtyKey.Reflectance, Selected);

    Captured.DirtyKey.PhysicalSurface = HashSeed;
    HashPhysicalDeclaration(Captured.DirtyKey.PhysicalSurface, PhysicalDeclaration);
    HashValue(Captured.DirtyKey.PhysicalSurface, Captured.PhysicalSurfaceResolved);
    HashValue(Captured.DirtyKey.PhysicalSurface, Captured.PhysicalPacketResolved);

    for (std::size_t ChannelIndex = 0u; ChannelIndex < MaterialProcessingDirtyKey::ChannelSpan; ++ChannelIndex)
    {
        const ChannelSubject Subject = static_cast<ChannelSubject>(ChannelIndex);
        Captured.DirtyKey.Channels[ChannelIndex] = HashChannel(Material.Channel(Subject));
    }

    Captured.DirtyKey.Layers = HashSeed;
    for (const MaterialProcessingLayerSnapshot& Layer : Captured.Layers)
        HashLayer(Captured.DirtyKey.Layers, Layer);

    Captured.DirtyKey.Combined = HashSeed;
    HashValue(Captured.DirtyKey.Combined, Captured.DirtyKey.Reflectance);
    HashValue(Captured.DirtyKey.Combined, Captured.DirtyKey.PhysicalSurface);
    for (std::uint64_t ChannelKey : Captured.DirtyKey.Channels)
        HashValue(Captured.DirtyKey.Combined, ChannelKey);
    HashValue(Captured.DirtyKey.Combined, Captured.DirtyKey.Layers);
    return Captured;
}

MaterialProcessingDirtySet MaterialProcessingExchange::Compare(const MaterialProcessingSnapshot& Previous,
                                                               const MaterialProcessingSnapshot& Current) const
{
    MaterialProcessingDirtySet Dirty;
    Dirty.ReflectanceChanged = Previous.DirtyKey.Reflectance != Current.DirtyKey.Reflectance;
    Dirty.PhysicalSurfaceChanged = Previous.DirtyKey.PhysicalSurface != Current.DirtyKey.PhysicalSurface;
    Dirty.LayersChanged = Previous.DirtyKey.Layers != Current.DirtyKey.Layers;
    for (std::size_t ChannelIndex = 0u; ChannelIndex < MaterialProcessingDirtyKey::ChannelSpan; ++ChannelIndex)
    {
        if (Previous.DirtyKey.Channels[ChannelIndex] != Current.DirtyKey.Channels[ChannelIndex])
            Dirty.ChannelMask |= 1u << static_cast<std::uint32_t>(ChannelIndex);
    }
    return Dirty;
}

Deliver<MaterialLiveChannelPreview> MaterialProcessingExchange::ResolveLiveChannelPreview(
    const MaterialProcessingSnapshot& Snapshot,
    const PreviewProjection& Preview,
    const SurfaceLayerSequence& Layers,
    ChannelSubject Channel,
    double PositionX,
    double PositionY,
    std::uint32_t Level) const
{
    if (Channel >= ChannelSubject::ChannelCount)
        return Deliver<MaterialLiveChannelPreview>::Refuse(
            { RefusalReason::ContentUnsupported, "the closed channel count is not a material channel" });

    const ChannelSpecification& Declared = Snapshot.Material.Channel(Channel);
    if (!Declared.ChannelDeclared)
        return Deliver<MaterialLiveChannelPreview>::Refuse(
            { RefusalReason::ContentUnsupported, "the requested material channel is undeclared" });

    ChannelPlacement Placement;
    Placement.Channel = Channel;
    Placement.ComponentSpan = MeasureCarriesColour(Declared.Measured) ? 3u : 1u;
    const std::vector<ChannelPlacement> Placements = { Placement };
    const Deliver<ResolvedSample> Resolved = Preview.ProjectContentAt(
        Layers, Placements, PositionX, PositionY, Level, Placement.ComponentSpan);
    if (!Resolved.Resolved)
        return Deliver<MaterialLiveChannelPreview>::Refuse(Resolved.Error);

    MaterialLiveChannelPreview Produced;
    Produced.Channel = Channel;
    Produced.Sample = Resolved.Resolve();
    Produced.PhysicalSurface = Snapshot.PhysicalSurface;
    Produced.MaterialFingerprint = Snapshot.DirtyKey.Combined;
    Produced.PhysicalSurfaceResolved = Snapshot.PhysicalSurfaceResolved;
    return Deliver<MaterialLiveChannelPreview>::Result(Produced);
}

Deliver<MaterialLayerCoveragePreview> MaterialProcessingExchange::ResolveLayerCoveragePreview(
    const MaterialProcessingSnapshot& Snapshot,
    LayerIdentity Layer,
    ChannelSubject Channel) const
{
    if (Channel >= ChannelSubject::ChannelCount)
        return Deliver<MaterialLayerCoveragePreview>::Refuse(
            { RefusalReason::ContentUnsupported, "the closed channel count is not a material channel" });

    for (const MaterialProcessingLayerSnapshot& Captured : Snapshot.Layers)
    {
        const LayerSpecification& Entry = Captured.Layer;
        if (Entry.Identity.SlotIndex != Layer.SlotIndex || Entry.Identity.SlotGeneration != Layer.SlotGeneration)
            continue;

        MaterialLayerCoveragePreview Produced;
        Produced.Identity = Entry.Identity;
        Produced.Channel = Channel;
        Produced.CoverageDeclared = Entry.Coverage.CoverageDeclared;
        Produced.AffectedChannelMask = Entry.Coverage.ChannelMask;

        const std::uint32_t TargetMask = Entry.Coverage.ChannelMask == 0u ? Entry.ChannelMask : Entry.Coverage.ChannelMask;
        if (TargetMask != 0u && (TargetMask & ChannelBit(Channel)) == 0u)
        {
            Produced.Coverage = 1.0;
        }
        else if (!Entry.Coverage.CoverageDeclared)
        {
            Produced.Coverage = 1.0;
        }
        else
        {
            Produced.Coverage = std::clamp(Entry.Coverage.UniformStrength, 0.0, 1.0);
            if (Entry.Coverage.Inverted) Produced.Coverage = 1.0 - Produced.Coverage;
        }

        Produced.LayerFingerprint = HashSeed;
        HashLayer(Produced.LayerFingerprint, Captured);
        return Deliver<MaterialLayerCoveragePreview>::Result(Produced);
    }

    return Deliver<MaterialLayerCoveragePreview>::Refuse(
        { RefusalReason::IdentityStale, "the layer no longer resolves in the material snapshot" });
}

MaterialProcessingCapabilities MaterialProcessingExchange::Capabilities() const
{
    MaterialProcessingCapabilities Declared;
    Declared.AnalyticResolution = true;
    Declared.ImportedImageResolution = true;
    return Declared;
}

} // namespace Slate
