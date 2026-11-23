//============================================================================================================================================
//                                                         WORKSPACECODEX.CPP
//============================================================================================================================================
// 🧩 Workspace section inscription and recovery without coupling document storage to any interface implementation.

#include "SlateDocument/Format/CodexInterchange/Api/WorkspaceCodex.h"

#include <cstring>
#include <limits>
#include <utility>

namespace Slate
{

namespace
{

constexpr std::uint32_t NamingSection      = 0x4D414E57u;   // [-] - `WNAM`
constexpr std::uint32_t EnvironmentSection = 0x564E4557u;   // [-] - `WENV`
constexpr std::uint32_t SceneSection       = 0x454E4353u;   // [-] - `SCNE`
constexpr std::uint32_t MeshSection        = 0x4853454Du;   // [-] - `MESH`
constexpr std::uint32_t MaterialSection    = 0x5354414Du;   // [-] - `MATS`
constexpr std::uint32_t EmbeddedSection    = 0x44424D45u;   // [-] - `EMBD`

void Inscribe32(std::vector<std::uint8_t>& Content, std::uint32_t Held)
{
    for (std::uint32_t Shift = 0u; Shift < 32u; Shift += 8u)
        Content.push_back(static_cast<std::uint8_t>(Held >> Shift));
}

void Inscribe64(std::vector<std::uint8_t>& Content, std::uint64_t Held)
{
    for (std::uint32_t Shift = 0u; Shift < 64u; Shift += 8u)
        Content.push_back(static_cast<std::uint8_t>(Held >> Shift));
}

void InscribeReal(std::vector<std::uint8_t>& Content, double Held)
{
    std::uint64_t Bits = 0u;
    static_assert(sizeof(Bits) == sizeof(Held));
    std::memcpy(&Bits, &Held, sizeof(Bits));
    Inscribe64(Content, Bits);
}

void InscribeBool(std::vector<std::uint8_t>& Content, bool Held)
{
    Content.push_back(Held ? 1u : 0u);
}

void InscribeRun(std::vector<std::uint8_t>& Content, const std::string& Held)
{
    Inscribe32(Content, static_cast<std::uint32_t>(Held.size()));
    Content.insert(Content.end(), Held.begin(), Held.end());
}

bool Extract32(const std::vector<std::uint8_t>& Content, std::size_t& Position, std::uint32_t& Held)
{
    if (Position > Content.size() || Content.size() - Position < 4u)
        return false;

    Held = 0u;
    for (std::uint32_t Shift = 0u; Shift < 32u; Shift += 8u)
        Held |= static_cast<std::uint32_t>(Content[Position++]) << Shift;
    return true;
}

bool Extract64(const std::vector<std::uint8_t>& Content, std::size_t& Position, std::uint64_t& Held)
{
    if (Position > Content.size() || Content.size() - Position < 8u)
        return false;

    Held = 0u;
    for (std::uint32_t Shift = 0u; Shift < 64u; Shift += 8u)
        Held |= static_cast<std::uint64_t>(Content[Position++]) << Shift;
    return true;
}

bool ExtractReal(const std::vector<std::uint8_t>& Content, std::size_t& Position, double& Held)
{
    std::uint64_t Bits = 0u;
    if (!Extract64(Content, Position, Bits))
        return false;

    std::memcpy(&Held, &Bits, sizeof(Held));
    return true;
}

bool ExtractBool(const std::vector<std::uint8_t>& Content, std::size_t& Position, bool& Held)
{
    if (Position >= Content.size())
        return false;
    Held = Content[Position++] != 0u;
    return true;
}

bool ExtractRun(const std::vector<std::uint8_t>& Content, std::size_t& Position, std::string& Held)
{
    std::uint32_t ByteCount = 0u;
    if (!Extract32(Content, Position, ByteCount) || ByteCount > Content.size() - Position)
        return false;

    Held.assign(reinterpret_cast<const char*>(Content.data() + Position), ByteCount);
    Position += ByteCount;
    return true;
}

const CodexSection* SectionOf(const CodexDocument& Document, std::uint32_t Code)
{
    for (const CodexSection& Current : Document.Sections)
    {
        if (Current.Code == Code)
            return &Current;
    }

    return nullptr;
}

CodexSection Section(std::uint32_t Code, std::uint64_t Revision, std::vector<std::uint8_t>&& Content)
{
    CodexSection Produced;
    Produced.Code = Code;
    Produced.MajorVersion = 1u;
    Produced.MinorVersion = 0u;
    Produced.Revision = Revision;
    Produced.Content = std::move(Content);
    return Produced;
}

ChannelSpecification ScalarChannel(double Value, double Default)
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

ChannelSpecification ColourChannel(double Red, double Green, double Blue)
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

std::uint32_t ChannelBit(ChannelSubject Channel)
{
    return Channel == ChannelSubject::ChannelCount ? 0u : (1u << static_cast<std::uint32_t>(Channel));
}

std::uint32_t DefaultMaterialChannelMask()
{
    return ChannelBit(ChannelSubject::AlbedoColour)
         | ChannelBit(ChannelSubject::Metallic)
         | ChannelBit(ChannelSubject::Roughness)
         | ChannelBit(ChannelSubject::NormalIncidenceReflectance)
         | ChannelBit(ChannelSubject::AmbientOcclusion)
         | ChannelBit(ChannelSubject::Emission)
         | ChannelBit(ChannelSubject::Opacity);
}

void InscribeColour(std::vector<std::uint8_t>& Content, const ColourSpecification& Colour)
{
    InscribeReal(Content, Colour.RedCoordinate);
    InscribeReal(Content, Colour.GreenCoordinate);
    InscribeReal(Content, Colour.BlueCoordinate);
    Inscribe32(Content, Colour.SpaceIdentity);
}

bool ExtractColour(const std::vector<std::uint8_t>& Content, std::size_t& Position, ColourSpecification& Colour)
{
    return ExtractReal(Content, Position, Colour.RedCoordinate)
        && ExtractReal(Content, Position, Colour.GreenCoordinate)
        && ExtractReal(Content, Position, Colour.BlueCoordinate)
        && Extract32(Content, Position, Colour.SpaceIdentity);
}

void InscribeTextured(std::vector<std::uint8_t>& Content, const TexturedContent& Textured)
{
    Inscribe32(Content, Textured.ExtentTexels);
    Inscribe32(Content, Textured.ComponentCount);
    Inscribe32(Content, static_cast<std::uint32_t>(Textured.Texels.size()));
    for (float Texel : Textured.Texels)
    {
        std::uint32_t Bits = 0u;
        static_assert(sizeof(Bits) == sizeof(Texel));
        std::memcpy(&Bits, &Texel, sizeof(Bits));
        Inscribe32(Content, Bits);
    }
}

bool ExtractTextured(const std::vector<std::uint8_t>& Content, std::size_t& Position, TexturedContent& Textured)
{
    std::uint32_t TexelCount = 0u;
    if (!Extract32(Content, Position, Textured.ExtentTexels)
     || !Extract32(Content, Position, Textured.ComponentCount)
     || !Extract32(Content, Position, TexelCount))
        return false;

    if (TexelCount > (Content.size() - Position) / 4u)
        return false;

    Textured.Texels.clear();
    Textured.Texels.reserve(TexelCount);
    for (std::uint32_t Index = 0u; Index < TexelCount; ++Index)
    {
        std::uint32_t Bits = 0u;
        float Texel = 0.0f;
        if (!Extract32(Content, Position, Bits))
            return false;
        std::memcpy(&Texel, &Bits, sizeof(Texel));
        Textured.Texels.push_back(Texel);
    }
    return true;
}

void InscribeChannel(std::vector<std::uint8_t>& Content, const ChannelSpecification& Channel)
{
    Inscribe32(Content, static_cast<std::uint32_t>(Channel.Source));
    Inscribe32(Content, static_cast<std::uint32_t>(Channel.Measured));
    Inscribe32(Content, Channel.SourceIndex);
    InscribeReal(Content, Channel.ConstantScalar);
    InscribeColour(Content, Channel.ConstantColour);
    InscribeReal(Content, Channel.DefaultScalar);
    InscribeColour(Content, Channel.DefaultColour);
    InscribeReal(Content, Channel.LowerMagnitude);
    InscribeReal(Content, Channel.UpperMagnitude);
    InscribeBool(Content, Channel.ChannelDeclared);
}

bool ExtractChannel(const std::vector<std::uint8_t>& Content, std::size_t& Position, ChannelSpecification& Channel)
{
    std::uint32_t Source = 0u;
    std::uint32_t Measure = 0u;
    if (!Extract32(Content, Position, Source) || Source > static_cast<std::uint32_t>(ChannelSource::Absent)
     || !Extract32(Content, Position, Measure) || Measure >= static_cast<std::uint32_t>(ChannelMeasure::MeasureCount)
     || !Extract32(Content, Position, Channel.SourceIndex)
     || !ExtractReal(Content, Position, Channel.ConstantScalar)
     || !ExtractColour(Content, Position, Channel.ConstantColour)
     || !ExtractReal(Content, Position, Channel.DefaultScalar)
     || !ExtractColour(Content, Position, Channel.DefaultColour)
     || !ExtractReal(Content, Position, Channel.LowerMagnitude)
     || !ExtractReal(Content, Position, Channel.UpperMagnitude)
     || !ExtractBool(Content, Position, Channel.ChannelDeclared))
        return false;

    Channel.Source = static_cast<ChannelSource>(Source);
    Channel.Measured = static_cast<ChannelMeasure>(Measure);
    return true;
}

void InscribeCoverage(std::vector<std::uint8_t>& Content, const CoverageSpecification& Coverage)
{
    Inscribe32(Content, static_cast<std::uint32_t>(Coverage.Source));
    Inscribe32(Content, Coverage.SourceIndex);
    InscribeTextured(Content, Coverage.Textured);
    InscribeReal(Content, Coverage.UniformStrength);
    Inscribe32(Content, Coverage.ChannelMask);
    InscribeBool(Content, Coverage.Inverted);
    InscribeBool(Content, Coverage.CoverageDeclared);
}

bool ExtractCoverage(const std::vector<std::uint8_t>& Content, std::size_t& Position, CoverageSpecification& Coverage)
{
    std::uint32_t Source = 0u;
    if (!Extract32(Content, Position, Source) || Source >= static_cast<std::uint32_t>(LayerContentSource::SourceCount)
     || !Extract32(Content, Position, Coverage.SourceIndex)
     || !ExtractTextured(Content, Position, Coverage.Textured)
     || !ExtractReal(Content, Position, Coverage.UniformStrength)
     || !Extract32(Content, Position, Coverage.ChannelMask)
     || !ExtractBool(Content, Position, Coverage.Inverted)
     || !ExtractBool(Content, Position, Coverage.CoverageDeclared))
        return false;

    Coverage.Source = static_cast<LayerContentSource>(Source);
    return true;
}

void InscribeLayer(std::vector<std::uint8_t>& Content, const LayerSpecification& Layer)
{
    Inscribe32(Content, Layer.Identity.SlotIndex);
    Inscribe32(Content, Layer.Identity.SlotGeneration);
    Inscribe32(Content, static_cast<std::uint32_t>(Layer.Source));
    Inscribe32(Content, Layer.SourceIndex);
    Inscribe32(Content, Layer.NestedIndex);
    Inscribe32(Content, Layer.ChannelMask);
    Inscribe32(Content, static_cast<std::uint32_t>(Layer.Combination));
    InscribeCoverage(Content, Layer.Coverage);
    InscribeTextured(Content, Layer.Textured);
    InscribeRun(Content, Layer.Name);
    InscribeBool(Content, Layer.PresenceEnabled);
    InscribeBool(Content, Layer.Mandatory);
    InscribeBool(Content, Layer.ResampleOwed);
}

bool ExtractLayer(const std::vector<std::uint8_t>& Content, std::size_t& Position, LayerSpecification& Layer)
{
    std::uint32_t Source = 0u;
    std::uint32_t Combination = 0u;
    if (!Extract32(Content, Position, Layer.Identity.SlotIndex)
     || !Extract32(Content, Position, Layer.Identity.SlotGeneration)
     || !Extract32(Content, Position, Source) || Source >= static_cast<std::uint32_t>(LayerContentSource::SourceCount)
     || !Extract32(Content, Position, Layer.SourceIndex)
     || !Extract32(Content, Position, Layer.NestedIndex)
     || !Extract32(Content, Position, Layer.ChannelMask)
     || !Extract32(Content, Position, Combination) || Combination >= static_cast<std::uint32_t>(CombineSpecification::CombineCount)
     || !ExtractCoverage(Content, Position, Layer.Coverage)
     || !ExtractTextured(Content, Position, Layer.Textured)
     || !ExtractRun(Content, Position, Layer.Name)
     || !ExtractBool(Content, Position, Layer.PresenceEnabled)
     || !ExtractBool(Content, Position, Layer.Mandatory)
     || !ExtractBool(Content, Position, Layer.ResampleOwed))
        return false;

    Layer.Source = static_cast<LayerContentSource>(Source);
    Layer.Combination = static_cast<CombineSpecification>(Combination);
    return true;
}

}

WorkspaceMaterialRecord DefaultWorkspaceMaterialRecord(const std::string& Reference)
{
    WorkspaceMaterialRecord Record;
    Record.Reference = Reference.empty() ? "DefaultMaterial" : Reference;
    Record.Material.DeclareReflectance(ReflectanceSelection::Standard);
    Discard(Record.Material.DeclareChannel(ChannelSubject::AlbedoColour, ColourChannel(1.0, 1.0, 1.0)));
    Discard(Record.Material.DeclareChannel(ChannelSubject::Metallic, ScalarChannel(0.0, 0.0)));
    Discard(Record.Material.DeclareChannel(ChannelSubject::Roughness, ScalarChannel(0.5, 0.5)));
    Discard(Record.Material.DeclareChannel(ChannelSubject::NormalIncidenceReflectance, ScalarChannel(0.04, 0.04)));
    Discard(Record.Material.DeclareChannel(ChannelSubject::AmbientOcclusion, ScalarChannel(1.0, 1.0)));
    Discard(Record.Material.DeclareChannel(ChannelSubject::Emission, ColourChannel(0.0, 0.0, 0.0)));
    Discard(Record.Material.DeclareChannel(ChannelSubject::Opacity, ScalarChannel(1.0, 1.0)));

    LayerSpecification Base;
    Base.Name = "Base Material";
    Base.Source = LayerContentSource::MaterialConstants;
    Base.ChannelMask = DefaultMaterialChannelMask();
    Base.Mandatory = true;
    Base.PresenceEnabled = true;
    Discard(Record.Layers.Append(Base));
    return Record;
}

void EnsureWorkspaceMaterialRecords(WorkspaceCodex& Workspace)
{
    for (CodexSceneEntry& Entry : Workspace.Scene)
    {
        if (Entry.Subject != CodexSceneSubject::Geometry)
            continue;
        if (Entry.MaterialReference.empty())
            Entry.MaterialReference = Entry.Naming.empty() ? "DefaultMaterial" : Entry.Naming + "Material";

        bool Found = false;
        for (const WorkspaceMaterialRecord& Material : Workspace.Materials)
        {
            if (Material.Reference == Entry.MaterialReference)
            {
                Found = true;
                break;
            }
        }
        if (!Found)
            Workspace.Materials.push_back(DefaultWorkspaceMaterialRecord(Entry.MaterialReference));
    }
}

Deliver<bool> AssignWorkspaceMaterial(WorkspaceCodex& Workspace,
                                      std::uint32_t SceneIndex,
                                      const std::string& MaterialReference)
{
    if (SceneIndex >= Workspace.Scene.size() || Workspace.Scene[SceneIndex].Subject != CodexSceneSubject::Geometry)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the scene entry is not a geometry material owner" });
    if (MaterialReference.empty())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the material reference is empty" });

    bool Found = false;
    for (const WorkspaceMaterialRecord& Material : Workspace.Materials)
        if (Material.Reference == MaterialReference)
        {
            Found = true;
            break;
        }

    if (!Found)
        Workspace.Materials.push_back(DefaultWorkspaceMaterialRecord(MaterialReference));

    Workspace.Scene[SceneIndex].MaterialReference = MaterialReference;
    return Deliver<bool>::Result(true);
}

Deliver<std::uint32_t> BindWorkspaceMaterialImage(WorkspaceMaterialRecord& Material,
                                                  ChannelSubject Channel,
                                                  const WorkspaceMaterialImageReference& Image)
{
    if (Channel == ChannelSubject::ChannelCount)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "the closed channel count is not bindable" });
    if (Image.OriginPath.empty() || Image.Width == 0u || Image.Height == 0u || Image.ComponentCount == 0u)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "the imported image declaration is incomplete" });

    Material.Images.push_back(Image);
    const std::uint32_t SourceIndex = static_cast<std::uint32_t>(Material.Images.size() - 1u);
    ChannelSpecification Declared = Material.Material.Channel(Channel);
    Declared.Source = ChannelSource::Imported;
    Declared.SourceIndex = SourceIndex;
    Declared.Measured = Image.ColourData ? ChannelMeasure::Reflectance : ChannelMeasure::Scalar;
    if (Image.ColourData && !Declared.DefaultColour.ColourDeclared())
        Declared.DefaultColour = { 1.0, 1.0, 1.0, WorkingSpaceIdentity };
    if (!Image.ColourData)
    {
        Declared.DefaultScalar = Channel == ChannelSubject::Opacity || Channel == ChannelSubject::AmbientOcclusion ? 1.0 : 0.0;
        Declared.LowerMagnitude = 0.0;
        Declared.UpperMagnitude = 1.0;
    }

    const Deliver<bool> ChannelDeclared = Material.Material.DeclareChannel(Channel, Declared);
    if (!ChannelDeclared.Resolved)
    {
        Material.Images.pop_back();
        return Deliver<std::uint32_t>::Refuse(ChannelDeclared.Error);
    }
    return Deliver<std::uint32_t>::Result(SourceIndex);
}

Deliver<CodexDocument> WorkspaceCodexInterchange::EncodeWorkspace(const WorkspaceCodex& Workspace,
                                                                   std::uint64_t          Identity,
                                                                   std::uint64_t          Revision) const
{
    if (Workspace.Scene.size() > std::numeric_limits<std::uint32_t>::max() ||
        Workspace.SceneMeshes.size() > std::numeric_limits<std::uint32_t>::max() ||
        Workspace.Materials.size() > std::numeric_limits<std::uint32_t>::max() ||
        Workspace.Embedded.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return Deliver<CodexDocument>::Refuse(
            { RefusalReason::ExtentExhausted, "the workspace carries too many scene, mesh, material, or embedded documents" });
    }

    std::vector<std::uint8_t> Naming;
    InscribeRun(Naming, Workspace.Naming);

    std::vector<std::uint8_t> Environment;
    InscribeReal(Environment, Workspace.Environment.SunElevation);
    InscribeReal(Environment, Workspace.Environment.SunAzimuth);
    InscribeReal(Environment, Workspace.Environment.SunIntensity);
    InscribeReal(Environment, Workspace.Environment.SunTemperature);
    InscribeReal(Environment, Workspace.Environment.SkyIntensity);
    InscribeReal(Environment, Workspace.Environment.AtmosphereDensity);
    InscribeReal(Environment, Workspace.Environment.AtmosphereScaleHeight);

    std::vector<std::uint8_t> Scene;
    Inscribe32(Scene, static_cast<std::uint32_t>(Workspace.Scene.size()));
    for (const CodexSceneEntry& Current : Workspace.Scene)
    {
        Inscribe32(Scene, static_cast<std::uint32_t>(Current.Subject));
        InscribeRun(Scene, Current.Naming);
        InscribeRun(Scene, Current.GeometryReference);
        InscribeRun(Scene, Current.MaterialReference);
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
            InscribeReal(Scene, Current.Position[Axis]);
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
            InscribeReal(Scene, Current.Rotation[Axis]);
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
            InscribeReal(Scene, Current.Scale[Axis]);
    }

    std::vector<std::uint8_t> Meshes;
    Inscribe32(Meshes, static_cast<std::uint32_t>(Workspace.SceneMeshes.size()));
    for (const CodexSceneMesh& Current : Workspace.SceneMeshes)
    {
        if (Current.Positions.size() % 3u != 0u ||
            Current.Positions.size() / 3u > std::numeric_limits<std::uint32_t>::max() ||
            Current.Indices.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return Deliver<CodexDocument>::Refuse(
                { RefusalReason::ContentUnsupported, "a workspace scene mesh has inconsistent extents" });
        }
        InscribeRun(Meshes, Current.Naming);
        Inscribe32(Meshes, static_cast<std::uint32_t>(Current.Positions.size() / 3u));
        for (double Position : Current.Positions)
            InscribeReal(Meshes, Position);
        Inscribe32(Meshes, static_cast<std::uint32_t>(Current.Indices.size()));
        for (std::uint32_t Index : Current.Indices)
            Inscribe32(Meshes, Index);
    }

    std::vector<std::uint8_t> Materials;
    Inscribe32(Materials, static_cast<std::uint32_t>(Workspace.Materials.size()));
    for (const WorkspaceMaterialRecord& Current : Workspace.Materials)
    {
        InscribeRun(Materials, Current.Reference);
        Inscribe32(Materials, static_cast<std::uint32_t>(Current.Material.Reflectance()));
        InscribeReal(Materials, Current.Material.CutoutThreshold());
        InscribeBool(Materials, Current.Material.CutoutRegistered());
        for (std::uint32_t ChannelIndex = 0u; ChannelIndex < static_cast<std::uint32_t>(ChannelSubject::ChannelCount); ++ChannelIndex)
            InscribeChannel(Materials, Current.Material.Channel(static_cast<ChannelSubject>(ChannelIndex)));
        Inscribe32(Materials, static_cast<std::uint32_t>(Current.Images.size()));
        for (const WorkspaceMaterialImageReference& Image : Current.Images)
        {
            InscribeRun(Materials, Image.ReferenceName);
            InscribeRun(Materials, Image.OriginPath);
            Inscribe32(Materials, Image.Width);
            Inscribe32(Materials, Image.Height);
            Inscribe32(Materials, Image.ComponentCount);
            Inscribe32(Materials, Image.BitsPerComponent);
            InscribeBool(Materials, Image.ColourData);
        }
        Inscribe32(Materials, Current.Layers.EntryCount());
        for (const LayerSpecification& Layer : Current.Layers.Entries())
            InscribeLayer(Materials, Layer);
    }

    CodexInterchange Codex;
    std::vector<std::uint8_t> Embedded;
    Inscribe32(Embedded, static_cast<std::uint32_t>(Workspace.Embedded.size()));
    for (const CodexDocument& Current : Workspace.Embedded)
    {
        const Deliver<std::vector<std::uint8_t>> Encoded = Codex.Encode(Current);
        if (!Encoded.Resolved || Encoded.Resolve().size() > std::numeric_limits<std::uint32_t>::max())
        {
            return Deliver<CodexDocument>::Refuse(
                { RefusalReason::ContentUnsupported, "an embedded Codex document could not be represented" });
        }

        Inscribe32(Embedded, static_cast<std::uint32_t>(Encoded.Resolve().size()));
        Embedded.insert(Embedded.end(), Encoded.Resolve().begin(), Encoded.Resolve().end());
    }

    CodexDocument Produced;
    Produced.Profile = CodexProfile::Workspace;
    Produced.Identity = Identity;
    Produced.CurrentRevision = Revision;
    Produced.Sections.push_back(Section(NamingSection, Revision, std::move(Naming)));
    Produced.Sections.push_back(Section(EnvironmentSection, Revision, std::move(Environment)));
    Produced.Sections.push_back(Section(SceneSection, Revision, std::move(Scene)));
    Produced.Sections.push_back(Section(MeshSection, Revision, std::move(Meshes)));
    Produced.Sections.push_back(Section(MaterialSection, Revision, std::move(Materials)));
    Produced.Sections.push_back(Section(EmbeddedSection, Revision, std::move(Embedded)));
    return Deliver<CodexDocument>::Result(std::move(Produced));
}

Deliver<WorkspaceCodex> WorkspaceCodexInterchange::DecodeWorkspace(const CodexDocument& Document) const
{
    if (Document.Profile != CodexProfile::Workspace)
    {
        return Deliver<WorkspaceCodex>::Refuse(
            { RefusalReason::ContentUnsupported, "the Codex document is not a workspace profile" });
    }

    const CodexSection* Naming = SectionOf(Document, NamingSection);
    const CodexSection* Environment = SectionOf(Document, EnvironmentSection);
    const CodexSection* Scene = SectionOf(Document, SceneSection);
    const CodexSection* Meshes = SectionOf(Document, MeshSection);
    const CodexSection* Materials = SectionOf(Document, MaterialSection);
    const CodexSection* Embedded = SectionOf(Document, EmbeddedSection);
    if (Naming == nullptr || Environment == nullptr || Scene == nullptr || Embedded == nullptr)
    {
        return Deliver<WorkspaceCodex>::Refuse(
            { RefusalReason::ContentUnsupported, "the workspace is missing one required typed section" });
    }

    WorkspaceCodex Produced;
    std::size_t Position = 0u;
    if (!ExtractRun(Naming->Content, Position, Produced.Naming) || Position != Naming->Content.size())
    {
        return Deliver<WorkspaceCodex>::Refuse(
            { RefusalReason::ContentUnsupported, "the workspace naming section is inconsistent" });
    }

    Position = 0u;
    if (!ExtractReal(Environment->Content, Position, Produced.Environment.SunElevation) ||
        !ExtractReal(Environment->Content, Position, Produced.Environment.SunAzimuth) ||
        !ExtractReal(Environment->Content, Position, Produced.Environment.SunIntensity) ||
        !ExtractReal(Environment->Content, Position, Produced.Environment.SunTemperature) ||
        !ExtractReal(Environment->Content, Position, Produced.Environment.SkyIntensity) ||
        !ExtractReal(Environment->Content, Position, Produced.Environment.AtmosphereDensity) ||
        !ExtractReal(Environment->Content, Position, Produced.Environment.AtmosphereScaleHeight) ||
        Position != Environment->Content.size())
    {
        return Deliver<WorkspaceCodex>::Refuse(
            { RefusalReason::ContentUnsupported, "the workspace environment section is inconsistent" });
    }

    Position = 0u;
    std::uint32_t SceneCount = 0u;
    if (!Extract32(Scene->Content, Position, SceneCount))
    {
        return Deliver<WorkspaceCodex>::Refuse(
            { RefusalReason::ContentUnsupported, "the workspace scene section is incomplete" });
    }

    Produced.Scene.reserve(SceneCount);
    for (std::uint32_t Index = 0u; Index < SceneCount; ++Index)
    {
        std::uint32_t Subject = 0u;
        CodexSceneEntry Current;
        if (!Extract32(Scene->Content, Position, Subject) || Subject > static_cast<std::uint32_t>(CodexSceneSubject::Geometry) ||
            !ExtractRun(Scene->Content, Position, Current.Naming) ||
            !ExtractRun(Scene->Content, Position, Current.GeometryReference) ||
            !ExtractRun(Scene->Content, Position, Current.MaterialReference))
        {
            return Deliver<WorkspaceCodex>::Refuse(
                { RefusalReason::ContentUnsupported, "a workspace scene entry is inconsistent" });
        }

        Current.Subject = static_cast<CodexSceneSubject>(Subject);
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
            if (!ExtractReal(Scene->Content, Position, Current.Position[Axis]))
                return Deliver<WorkspaceCodex>::Refuse(
                    { RefusalReason::ContentUnsupported, "a workspace scene placement is incomplete" });
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
            if (!ExtractReal(Scene->Content, Position, Current.Rotation[Axis]))
                return Deliver<WorkspaceCodex>::Refuse(
                    { RefusalReason::ContentUnsupported, "a workspace scene rotation is incomplete" });
        for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
            if (!ExtractReal(Scene->Content, Position, Current.Scale[Axis]))
                return Deliver<WorkspaceCodex>::Refuse(
                    { RefusalReason::ContentUnsupported, "a workspace scene scale is incomplete" });

        Produced.Scene.push_back(std::move(Current));
    }

    if (Position != Scene->Content.size())
    {
        return Deliver<WorkspaceCodex>::Refuse(
            { RefusalReason::ContentUnsupported, "the workspace scene section has trailing content" });
    }

    if (Meshes != nullptr)
    {
        Position = 0u;
        std::uint32_t MeshCount = 0u;
        if (!Extract32(Meshes->Content, Position, MeshCount))
        {
            return Deliver<WorkspaceCodex>::Refuse(
                { RefusalReason::ContentUnsupported, "the workspace mesh section is incomplete" });
        }

        Produced.SceneMeshes.reserve(MeshCount);
        for (std::uint32_t MeshIndex = 0u; MeshIndex < MeshCount; ++MeshIndex)
        {
            CodexSceneMesh Current;
            std::uint32_t VertexCount = 0u;
            if (!ExtractRun(Meshes->Content, Position, Current.Naming) ||
                !Extract32(Meshes->Content, Position, VertexCount))
            {
                return Deliver<WorkspaceCodex>::Refuse(
                    { RefusalReason::ContentUnsupported, "a workspace mesh header is inconsistent" });
            }
            Current.Positions.reserve(static_cast<std::size_t>(VertexCount) * 3u);
            for (std::uint32_t Index = 0u; Index < VertexCount * 3u; ++Index)
            {
                double Held = 0.0;
                if (!ExtractReal(Meshes->Content, Position, Held))
                    return Deliver<WorkspaceCodex>::Refuse(
                        { RefusalReason::ContentUnsupported, "a workspace mesh vertex run is incomplete" });
                Current.Positions.push_back(Held);
            }
            std::uint32_t IndexCount = 0u;
            if (!Extract32(Meshes->Content, Position, IndexCount))
                return Deliver<WorkspaceCodex>::Refuse(
                    { RefusalReason::ContentUnsupported, "a workspace mesh index count is incomplete" });
            Current.Indices.reserve(IndexCount);
            for (std::uint32_t Index = 0u; Index < IndexCount; ++Index)
            {
                std::uint32_t Held = 0u;
                if (!Extract32(Meshes->Content, Position, Held) || Held >= VertexCount)
                    return Deliver<WorkspaceCodex>::Refuse(
                        { RefusalReason::ContentUnsupported, "a workspace mesh index is inconsistent" });
                Current.Indices.push_back(Held);
            }
            Produced.SceneMeshes.push_back(std::move(Current));
        }
        if (Position != Meshes->Content.size())
            return Deliver<WorkspaceCodex>::Refuse(
                { RefusalReason::ContentUnsupported, "the workspace mesh section has trailing content" });
    }

    if (Materials != nullptr)
    {
        Position = 0u;
        std::uint32_t MaterialCount = 0u;
        if (!Extract32(Materials->Content, Position, MaterialCount))
        {
            return Deliver<WorkspaceCodex>::Refuse(
                { RefusalReason::ContentUnsupported, "the workspace material section is incomplete" });
        }

        Produced.Materials.reserve(MaterialCount);
        for (std::uint32_t MaterialIndex = 0u; MaterialIndex < MaterialCount; ++MaterialIndex)
        {
            WorkspaceMaterialRecord Current;
            std::uint32_t Reflectance = 0u;
            bool CutoutRegistered = false;
            double CutoutThreshold = 0.5;
            if (!ExtractRun(Materials->Content, Position, Current.Reference)
             || !Extract32(Materials->Content, Position, Reflectance)
             || Reflectance >= static_cast<std::uint32_t>(ReflectanceSelection::ReflectanceCount)
             || !ExtractReal(Materials->Content, Position, CutoutThreshold)
             || !ExtractBool(Materials->Content, Position, CutoutRegistered))
            {
                return Deliver<WorkspaceCodex>::Refuse(
                    { RefusalReason::ContentUnsupported, "a workspace material header is inconsistent" });
            }
            Current.Material.DeclareReflectance(static_cast<ReflectanceSelection>(Reflectance));
            Current.Material.DeclareCutoutThreshold(CutoutThreshold);
            Current.Material.DeclareCutoutRegistration(CutoutRegistered);

            for (std::uint32_t ChannelIndex = 0u; ChannelIndex < static_cast<std::uint32_t>(ChannelSubject::ChannelCount); ++ChannelIndex)
            {
                ChannelSpecification Channel;
                if (!ExtractChannel(Materials->Content, Position, Channel))
                    return Deliver<WorkspaceCodex>::Refuse(
                        { RefusalReason::ContentUnsupported, "a workspace material channel is incomplete" });
                if (Channel.ChannelDeclared)
                {
                    const Deliver<bool> Declared = Current.Material.DeclareChannel(static_cast<ChannelSubject>(ChannelIndex), Channel);
                    if (!Declared.Resolved)
                        return Deliver<WorkspaceCodex>::Refuse(Declared.Error);
                }
            }

            std::uint32_t ImageCount = 0u;
            if (!Extract32(Materials->Content, Position, ImageCount))
                return Deliver<WorkspaceCodex>::Refuse(
                    { RefusalReason::ContentUnsupported, "a workspace material image count is incomplete" });
            Current.Images.reserve(ImageCount);
            for (std::uint32_t ImageIndex = 0u; ImageIndex < ImageCount; ++ImageIndex)
            {
                WorkspaceMaterialImageReference Image;
                if (!ExtractRun(Materials->Content, Position, Image.ReferenceName)
                 || !ExtractRun(Materials->Content, Position, Image.OriginPath)
                 || !Extract32(Materials->Content, Position, Image.Width)
                 || !Extract32(Materials->Content, Position, Image.Height)
                 || !Extract32(Materials->Content, Position, Image.ComponentCount)
                 || !Extract32(Materials->Content, Position, Image.BitsPerComponent)
                 || !ExtractBool(Materials->Content, Position, Image.ColourData))
                {
                    return Deliver<WorkspaceCodex>::Refuse(
                        { RefusalReason::ContentUnsupported, "a workspace material image is incomplete" });
                }
                Current.Images.push_back(std::move(Image));
            }

            std::uint32_t LayerCount = 0u;
            if (!Extract32(Materials->Content, Position, LayerCount))
                return Deliver<WorkspaceCodex>::Refuse(
                    { RefusalReason::ContentUnsupported, "a workspace material layer count is incomplete" });
            for (std::uint32_t LayerIndex = 0u; LayerIndex < LayerCount; ++LayerIndex)
            {
                LayerSpecification Layer;
                if (!ExtractLayer(Materials->Content, Position, Layer))
                    return Deliver<WorkspaceCodex>::Refuse(
                        { RefusalReason::ContentUnsupported, "a workspace material layer is incomplete" });
                if (Layer.Source == LayerContentSource::TexturedImpressions && Layer.Textured.Texels.empty())
                    Layer.Source = LayerContentSource::MaterialConstants;
                const Deliver<LayerIdentity> Added = Current.Layers.Append(Layer);
                if (!Added.Resolved)
                    return Deliver<WorkspaceCodex>::Refuse(Added.Error);
            }

            if (Current.Layers.EntryCount() == 0u)
                Current = DefaultWorkspaceMaterialRecord(Current.Reference);
            Produced.Materials.push_back(std::move(Current));
        }
        if (Position != Materials->Content.size())
            return Deliver<WorkspaceCodex>::Refuse(
                { RefusalReason::ContentUnsupported, "the workspace material section has trailing content" });
    }

    EnsureWorkspaceMaterialRecords(Produced);

    Position = 0u;
    std::uint32_t EmbeddedCount = 0u;
    if (!Extract32(Embedded->Content, Position, EmbeddedCount))
    {
        return Deliver<WorkspaceCodex>::Refuse(
            { RefusalReason::ContentUnsupported, "the workspace embedded section is incomplete" });
    }

    CodexInterchange Codex;
    Produced.Embedded.reserve(EmbeddedCount);
    for (std::uint32_t Index = 0u; Index < EmbeddedCount; ++Index)
    {
        std::uint32_t ByteCount = 0u;
        if (!Extract32(Embedded->Content, Position, ByteCount) || ByteCount > Embedded->Content.size() - Position)
        {
            return Deliver<WorkspaceCodex>::Refuse(
                { RefusalReason::ContentUnsupported, "an embedded Codex extent is inconsistent" });
        }

        std::vector<std::uint8_t> Stream(Embedded->Content.begin() + Position,
                                         Embedded->Content.begin() + Position + ByteCount);
        Position += ByteCount;

        const Deliver<CodexDocument> Decoded = Codex.Decode(Stream);
        if (!Decoded.Resolved)
            return Deliver<WorkspaceCodex>::Refuse(Decoded.Error);

        Produced.Embedded.push_back(Decoded.Resolve());
    }

    if (Position != Embedded->Content.size())
    {
        return Deliver<WorkspaceCodex>::Refuse(
            { RefusalReason::ContentUnsupported, "the workspace embedded section has trailing content" });
    }

    return Deliver<WorkspaceCodex>::Result(std::move(Produced));
}

}   // namespace Slate
