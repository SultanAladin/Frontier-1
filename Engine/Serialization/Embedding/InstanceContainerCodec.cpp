/*============================================================================================================================================
                                                       INSTANCECONTAINERCODEC.CPP
============================================================================================================================================*/
// 🧩 Implementation of multi-pillar embedded container encoding/decoding and VehicleDefault / ParticleSpace / InterfaceSpace examples

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Embedding/InstanceContainerCodec.h"

#include "Engine/Serialization/DependencyOrder/ContentHashCalculator.h"

#include <cstring>

namespace Frontier::Serialization
{

namespace
{

[[nodiscard]] std::vector<std::byte> PackFloatGridBytes(std::span<const float> Scalars)
{
    std::vector<std::byte> Bytes(Scalars.size() * sizeof(float));
    if (!Scalars.empty())
    {
        std::memcpy(Bytes.data(), Scalars.data(), Bytes.size());
    }
    return Bytes;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              MULTI-PILLAR CODEC METHODS
//------------------------------------------------------------------------------------------------------------------------

bool InstanceContainerCodec::EncodeToBytes(
    const MultiPillarInstanceDocument& Document,
    std::vector<std::byte>&            OutContainerBytes,
    std::string&                       OutDiagnostic)
{
    HybridContainerDocument Hybrid{};
    Hybrid.SuffixCategory     = Document.RootCategory;
    Hybrid.SchemaVersion      = Document.SchemaVersion;
    Hybrid.StableIdentifier   = Document.StableIdentifier;
    Hybrid.DocumentTitle      = Document.DocumentTitle;
    Hybrid.AuthoringWorkspace = Document.AuthoringWorkspace;
    Hybrid.ExternalLinks      = Document.ExternalLinks;

    toml::table EmbeddedSectionRoot{};
    for (const EmbeddedPillarPayload& Pillar : Document.EmbeddedPillars)
    {
        const FormatSuffixDescriptor& Desc = InspectFormatSuffixDescriptor(Pillar.PillarCategory);
        if (!Desc.SupportsEmbedding)
        {
            OutDiagnostic = "Format cannot be embedded inside a multi-pillar container: " + std::string(Desc.CanonicalSuffix);
            return false;
        }

        EmbeddedPillarEntry Decl{};
        Decl.PillarCategory    = Pillar.PillarCategory;
        Decl.SectionIdentifier = Pillar.SectionIdentifier;
        Decl.ResidencyMode     = Pillar.ResidencyMode;
        Decl.SummaryNotes      = Pillar.SummaryNotes;
        Hybrid.EmbeddedPillars.push_back(std::move(Decl));

        if (!Pillar.TomlSection.empty())
        {
            EmbeddedSectionRoot.insert(Pillar.SectionIdentifier, Pillar.TomlSection);
        }

        if (!Pillar.BinaryPayload.empty())
        {
            BinarySectionBlock Block{};
            Block.SectionIdentifier = Pillar.SectionIdentifier;
            Block.SubFormatTag      = std::string(Desc.CanonicalSuffix);
            Block.PayloadBytes      = Pillar.BinaryPayload;
            Hybrid.BinarySections.push_back(std::move(Block));
        }
    }

    if (!EmbeddedSectionRoot.empty())
    {
        Hybrid.TomlHeader.insert("EmbeddedSections", std::move(EmbeddedSectionRoot));
    }

    return HybridContainerEncoder::EncodeToBytes(Hybrid, OutContainerBytes, OutDiagnostic);
}

bool InstanceContainerCodec::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const MultiPillarInstanceDocument& Document,
    std::string&                       OutDiagnostic)
{
    std::vector<std::byte> Bytes{};
    if (!EncodeToBytes(Document, Bytes, OutDiagnostic))
    {
        return false;
    }
    return HybridContainerEncoder::EncodeToFile(
        TargetPath,
        [&]()
        {
            HybridContainerDocument Parsed{};
            std::string             Ignored{};
            (void)HybridContainerDecoder::DecodeFromBytes(Bytes, Parsed, Ignored);
            return Parsed;
        }(),
        OutDiagnostic);
}

bool InstanceContainerCodec::DecodeFromBytes(
    std::span<const std::byte>   InputBytes,
    MultiPillarInstanceDocument& OutDocument,
    std::string&                 OutDiagnostic)
{
    HybridContainerDocument Hybrid{};
    if (!HybridContainerDecoder::DecodeFromBytes(InputBytes, Hybrid, OutDiagnostic))
    {
        return false;
    }

    OutDocument = {};
    OutDocument.RootCategory       = Hybrid.SuffixCategory;
    OutDocument.SchemaVersion      = Hybrid.SchemaVersion;
    OutDocument.StableIdentifier   = Hybrid.StableIdentifier;
    OutDocument.DocumentTitle      = Hybrid.DocumentTitle;
    OutDocument.AuthoringWorkspace = Hybrid.AuthoringWorkspace;
    OutDocument.ExternalLinks      = Hybrid.ExternalLinks;

    const toml::table* EmbeddedSectionRoot = Hybrid.TomlHeader["EmbeddedSections"].as_table();

    for (const EmbeddedPillarEntry& Decl : Hybrid.EmbeddedPillars)
    {
        EmbeddedPillarPayload Payload{};
        Payload.PillarCategory    = Decl.PillarCategory;
        Payload.SectionIdentifier = Decl.SectionIdentifier;
        Payload.ResidencyMode     = Decl.ResidencyMode;
        Payload.SummaryNotes      = Decl.SummaryNotes;

        if (EmbeddedSectionRoot != nullptr)
        {
            if (const toml::table* SectionTbl = (*EmbeddedSectionRoot)[Decl.SectionIdentifier].as_table())
            {
                Payload.TomlSection = *SectionTbl;
            }
        }

        if (const BinarySectionBlock* Block = HybridContainerDecoder::FindBinarySection(Hybrid, Decl.SectionIdentifier))
        {
            Payload.BinaryPayload = Block->PayloadBytes;
        }

        OutDocument.EmbeddedPillars.push_back(std::move(Payload));
    }

    OutDiagnostic.clear();
    return true;
}

bool InstanceContainerCodec::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    MultiPillarInstanceDocument& OutDocument,
    std::string&                 OutDiagnostic)
{
    HybridContainerDocument Hybrid{};
    if (!HybridContainerDecoder::DecodeFromFile(SourcePath, Hybrid, OutDiagnostic))
    {
        return false;
    }
    std::vector<std::byte> RoundTripBytes{};
    if (!HybridContainerEncoder::EncodeToBytes(Hybrid, RoundTripBytes, OutDiagnostic))
    {
        return false;
    }
    return DecodeFromBytes(RoundTripBytes, OutDocument, OutDiagnostic);
}

const EmbeddedPillarPayload* InstanceContainerCodec::FindEmbeddedPillar(
    const MultiPillarInstanceDocument& Document,
    std::string_view                   SectionIdentifier) noexcept
{
    for (const EmbeddedPillarPayload& Pillar : Document.EmbeddedPillars)
    {
        if (Pillar.SectionIdentifier == SectionIdentifier)
        {
            return &Pillar;
        }
    }
    return nullptr;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              CANONICAL EMBEDDED EXAMPLES
//------------------------------------------------------------------------------------------------------------------------

MultiPillarInstanceDocument InstanceContainerCodec::ConstructVehicleDefaultExample()
{
    MultiPillarInstanceDocument Doc{};
    Doc.RootCategory       = FormatSuffixCategory::Instance;
    Doc.SchemaVersion      = 1u;
    Doc.StableIdentifier   = "0193b5c0-7a1e-7000-8000-000000000001";
    Doc.DocumentTitle      = "VehicleDefault";
    Doc.AuthoringWorkspace = "Frontier.ComponentWorkspace";

    // ① Embedded .GeometrySpace
    {
        EmbeddedPillarPayload Geom{};
        Geom.PillarCategory    = FormatSuffixCategory::GeometrySpace;
        Geom.SectionIdentifier = "ControlVehicleGeometry";
        Geom.ResidencyMode     = EmbeddedResidencyMode::HybridTwoTier;
        Geom.SummaryNotes      = "Body + 4 Rims + 4 Tyres PolygonNetwork, 38 deg creased normals, 11 surface slots.";
        Geom.TomlSection.insert("CreaseAngleDegrees", 38.0);
        Geom.TomlSection.insert("FaceCount",          static_cast<int64_t>(25068));
        Geom.TomlSection.insert("PartSlotCount",      static_cast<int64_t>(11));
        const std::array<float, 6u> BoundsMinMax = { -3.11f, -1.25f, 0.0f, 3.11f, 1.25f, 1.77f };
        Geom.BinaryPayload = PackFloatGridBytes(BoundsMinMax);
        Doc.EmbeddedPillars.push_back(std::move(Geom));
    }

    // ② Embedded .PixelSpace (dashboard gauge & icon sheet)
    {
        EmbeddedPillarPayload Pix{};
        Pix.PillarCategory    = FormatSuffixCategory::PixelSpace;
        Pix.SectionIdentifier = "DashboardPixelSheet";
        Pix.ResidencyMode     = EmbeddedResidencyMode::HybridTwoTier;
        Pix.SummaryNotes      = "Packed vehicle dashboard icon & decal coordinate sheet.";
        Pix.TomlSection.insert("SheetWidthPixels",  static_cast<int64_t>(1024));
        Pix.TomlSection.insert("SheetHeightPixels", static_cast<int64_t>(1024));
        Pix.TomlSection.insert("PackedRegionCount", static_cast<int64_t>(24));
        const std::array<float, 4u> SampleUvRect = { 0.0f, 0.0f, 0.125f, 0.125f };
        Pix.BinaryPayload = PackFloatGridBytes(SampleUvRect);
        Doc.EmbeddedPillars.push_back(std::move(Pix));
    }

    // ③ Embedded .TextureSpace (multi-UDIM channel pack)
    {
        EmbeddedPillarPayload Tex{};
        Tex.PillarCategory    = FormatSuffixCategory::TextureSpace;
        Tex.SectionIdentifier = "VehicleUdimMipChain";
        Tex.ResidencyMode     = EmbeddedResidencyMode::HybridTwoTier;
        Tex.SummaryNotes      = "Channel-packed BC7 albedo/roughness/normal UDIM 1001..1002 specification.";
        Tex.TomlSection.insert("BlockEncoding",   "BC7_UNORM");
        Tex.TomlSection.insert("MipLevelCount",   static_cast<int64_t>(11));
        Tex.TomlSection.insert("UdimTileCount",   static_cast<int64_t>(2));
        const std::array<float, 4u> AverageAlbedo = { 0.14f, 0.19f, 0.28f, 1.0f };
        Tex.BinaryPayload = PackFloatGridBytes(AverageAlbedo);
        Doc.EmbeddedPillars.push_back(std::move(Tex));
    }

    // ④ Embedded .MaterialSpace (11 OpenPBR 1.1 coatings)
    {
        EmbeddedPillarPayload Mat{};
        Mat.PillarCategory    = FormatSuffixCategory::MaterialSpace;
        Mat.SectionIdentifier = "ElevenSlotOpenPbr";
        Mat.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
        Mat.SummaryNotes      = "Metallic clearcoat paint, vulcanized tyre rubber, titanium alloy & smoked headlamp optics.";
        Mat.TomlSection.insert("MaterialSlotCount",  static_cast<int64_t>(11));
        Mat.TomlSection.insert("PrimaryCoatWeight",  1.0);
        Mat.TomlSection.insert("PrimaryRoughness",   0.18);
        Mat.TomlSection.insert("PrimaryMetalness",   0.85);
        Doc.EmbeddedPillars.push_back(std::move(Mat));
    }

    // ⑤ Embedded .PhysicsSpace (rigid chassis + wheel constraints)
    {
        EmbeddedPillarPayload Phys{};
        Phys.PillarCategory    = FormatSuffixCategory::PhysicsSpace;
        Phys.SectionIdentifier = "ChassisRigidDynamics";
        Phys.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
        Phys.SummaryNotes      = "1480 kg chassis mass, double-wishbone suspension & Jolt v5.6.0 wheeled drivetrain.";
        Phys.TomlSection.insert("ChassisMassKilograms", 1480.0);
        Phys.TomlSection.insert("SuspensionStiffness",  38000.0);
        Phys.TomlSection.insert("WheelCount",           static_cast<int64_t>(4));
        Doc.EmbeddedPillars.push_back(std::move(Phys));
    }

    // ⑥ Embedded .ForceField (aerodynamic wake & downforce)
    {
        EmbeddedPillarPayload Force{};
        Force.PillarCategory    = FormatSuffixCategory::ForceField;
        Force.SectionIdentifier = "AerodynamicWakeField";
        Force.ResidencyMode     = EmbeddedResidencyMode::HybridTwoTier;
        Force.SummaryNotes      = "3D velocity/downforce wake field generated by vehicle bodywork at speed.";
        Force.TomlSection.insert("FieldCategory",        "AerodynamicWake");
        Force.TomlSection.insert("GridResolutionX",      static_cast<int64_t>(4));
        Force.TomlSection.insert("GridResolutionY",      static_cast<int64_t>(2));
        Force.TomlSection.insert("GridResolutionZ",      static_cast<int64_t>(2));
        Force.TomlSection.insert("Units",                "m/s");
        const std::array<float, 12u> SampleVelocityVectors = {
            -12.0f, 0.0f, -1.5f,  -10.5f, 0.4f, -1.2f,
            -10.5f, -0.4f, -1.2f,  -8.0f, 0.0f, -0.6f
        };
        Force.BinaryPayload = PackFloatGridBytes(SampleVelocityVectors);
        Doc.EmbeddedPillars.push_back(std::move(Force));
    }

    // ⑦ Embedded .VolumetricSpace (exhaust plume participating medium)
    {
        EmbeddedPillarPayload Vol{};
        Vol.PillarCategory    = FormatSuffixCategory::VolumetricSpace;
        Vol.SectionIdentifier = "ExhaustPlumeVolume";
        Vol.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
        Vol.SummaryNotes      = "Warm exhaust vapour scattering & temperature dissipation medium.";
        Vol.TomlSection.insert("ExtinctionCoefficient", 0.35);
        Vol.TomlSection.insert("AnisotropyG",           0.62);
        Vol.TomlSection.insert("PlumeTemperatureKelvin", 420.0);
        Doc.EmbeddedPillars.push_back(std::move(Vol));
    }

    // ⑧ Embedded .AudioSpace (engine RPM stems & spatial acoustics)
    {
        EmbeddedPillarPayload Audio{};
        Audio.PillarCategory    = FormatSuffixCategory::AudioSpace;
        Audio.SectionIdentifier = "PowertrainAcoustics";
        Audio.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
        Audio.SummaryNotes      = "Idle-to-redline harmonic stems, turbo spool & tyre slip spatial bus.";
        Audio.TomlSection.insert("IdleRpm",       850.0);
        Audio.TomlSection.insert("RedlineRpm",    7800.0);
        Audio.TomlSection.insert("DopplerFactor", 1.0);
        Doc.EmbeddedPillars.push_back(std::move(Audio));
    }

    // ⑨ Embedded .BehaviorSpace (drivetrain, headlamp & damage response logic)
    {
        EmbeddedPillarPayload Logic{};
        Logic.PillarCategory    = FormatSuffixCategory::BehaviorSpace;
        Logic.SectionIdentifier = "VehicleDrivetrainLogic";
        Logic.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
        Logic.SummaryNotes      = "Deterministic gear-shift curves, automatic night headlamp switch & impact shatter trigger.";
        Logic.TomlSection.insert("RuleSetIdentifier",     "ControlVehicleStandard");
        Logic.TomlSection.insert("AutomaticHeadlamps",    true);
        Logic.TomlSection.insert("ImpactRuptureThreshold", 42000.0);
        Doc.EmbeddedPillars.push_back(std::move(Logic));
    }

    // Shared external link (zero duplication across vehicles)
    ExternalLinkEntry HeadlampLink{};
    HeadlampLink.LinkSlot          = "HeadlampPhotometry";
    HeadlampLink.TargetCategory    = FormatSuffixCategory::IesProfile;
    HeadlampLink.RelativePath      = "Content/Photometry/MeasuredLuminaires/LowBeamHeadlamp.IesProfile";
    HeadlampLink.StableIdentifier  = "0193b5c0-7a1e-7000-8000-000000000042";
    HeadlampLink.ExpectedSha256Hex = FormatSha256Hex(ComputeSha256String("LowBeamHeadlamp.IesProfile"));
    Doc.ExternalLinks.push_back(std::move(HeadlampLink));

    return Doc;
}

MultiPillarInstanceDocument InstanceContainerCodec::ConstructExhaustPlumeParticleExample()
{
    MultiPillarInstanceDocument Doc{};
    Doc.RootCategory       = FormatSuffixCategory::ParticleSpace;
    Doc.SchemaVersion      = 1u;
    Doc.StableIdentifier   = "0193b5c0-7a1e-7000-8000-000000000010";
    Doc.DocumentTitle      = "ExhaustPlume";
    Doc.AuthoringWorkspace = "Frontier.ParticleWorkspace";

    {
        EmbeddedPillarPayload Source{};
        Source.PillarCategory    = FormatSuffixCategory::SpawnSource;
        Source.SectionIdentifier = "TailpipeSpawnSource";
        Source.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
        Source.TomlSection.insert("SpawnRatePerSecond", 180.0);
        Source.TomlSection.insert("LifetimeSeconds",    2.4);
        Source.TomlSection.insert("InitialSpeedMps",    6.5);
        Doc.EmbeddedPillars.push_back(std::move(Source));
    }
    {
        EmbeddedPillarPayload Field{};
        Field.PillarCategory    = FormatSuffixCategory::ForceField;
        Field.SectionIdentifier = "CurlTurbulenceField";
        Field.ResidencyMode     = EmbeddedResidencyMode::HybridTwoTier;
        Field.TomlSection.insert("CurlFrequency", 1.85);
        Field.TomlSection.insert("DragStrength",  0.42);
        const std::array<float, 6u> VelocitySamples = { 0.2f, 0.5f, 1.1f, -0.3f, 0.4f, 0.9f };
        Field.BinaryPayload = PackFloatGridBytes(VelocitySamples);
        Doc.EmbeddedPillars.push_back(std::move(Field));
    }
    {
        EmbeddedPillarPayload Colour{};
        Colour.PillarCategory    = FormatSuffixCategory::ColourSpace;
        Colour.SectionIdentifier = "BlackbodySmokeRamp";
        Colour.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
        Colour.TomlSection.insert("WorkingPrimaries", "ACEScg");
        Colour.TomlSection.insert("StartKelvin",      1400.0);
        Colour.TomlSection.insert("EndOpacity",       0.0);
        Doc.EmbeddedPillars.push_back(std::move(Colour));
    }
    {
        EmbeddedPillarPayload SizeCurve{};
        SizeCurve.PillarCategory    = FormatSuffixCategory::Curve;
        SizeCurve.SectionIdentifier = "RadiusExpansionCurve";
        SizeCurve.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
        SizeCurve.TomlSection.insert("InterpolationMode", "CubicHermite");
        SizeCurve.TomlSection.insert("StartRadiusMeters", 0.04);
        SizeCurve.TomlSection.insert("EndRadiusMeters",   0.65);
        Doc.EmbeddedPillars.push_back(std::move(SizeCurve));
    }
    {
        EmbeddedPillarPayload Trail{};
        Trail.PillarCategory    = FormatSuffixCategory::RibbonTrail;
        Trail.SectionIdentifier = "HeatStreakRibbon";
        Trail.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
        Trail.TomlSection.insert("SegmentWidthMeters", 0.08);
        Trail.TomlSection.insert("MaxSegmentCount",    static_cast<int64_t>(64));
        Doc.EmbeddedPillars.push_back(std::move(Trail));
    }

    return Doc;
}

MultiPillarInstanceDocument InstanceContainerCodec::ConstructHudOverlayInterfaceExample()
{
    MultiPillarInstanceDocument Doc{};
    Doc.RootCategory       = FormatSuffixCategory::InterfaceSpace;
    Doc.SchemaVersion      = 1u;
    Doc.StableIdentifier   = "0193b5c0-7a1e-7000-8000-000000000020";
    Doc.DocumentTitle      = "HudOverlay";
    Doc.AuthoringWorkspace = "Frontier.InterfaceWorkspace";

    {
        EmbeddedPillarPayload Icons{};
        Icons.PillarCategory    = FormatSuffixCategory::PixelSpace;
        Icons.SectionIdentifier = "HudIconSheet";
        Icons.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
        Icons.TomlSection.insert("LogicalScaleDpi", 96.0);
        Icons.TomlSection.insert("IconCount",       static_cast<int64_t>(32));
        Doc.EmbeddedPillars.push_back(std::move(Icons));
    }
    {
        EmbeddedPillarPayload Vectors{};
        Vectors.PillarCategory    = FormatSuffixCategory::VectorSpace;
        Vectors.SectionIdentifier = "ReticleVectorContours";
        Vectors.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
        Vectors.TomlSection.insert("ContourCount",      static_cast<int64_t>(12));
        Vectors.TomlSection.insert("StrokeWidthPixels", 1.5);
        Doc.EmbeddedPillars.push_back(std::move(Vectors));
    }
    {
        EmbeddedPillarPayload Swatches{};
        Swatches.PillarCategory    = FormatSuffixCategory::ColourSpace;
        Swatches.SectionIdentifier = "HudThemeSwatches";
        Swatches.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
        Swatches.TomlSection.insert("AccentHex",  "#4F8CFF");
        Swatches.TomlSection.insert("SurfaceHex", "#181B22");
        Doc.EmbeddedPillars.push_back(std::move(Swatches));
    }
    {
        EmbeddedPillarPayload Hover{};
        Hover.PillarCategory    = FormatSuffixCategory::Tooltip;
        Hover.SectionIdentifier = "StatComparisonTooltip";
        Hover.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
        Hover.TomlSection.insert("DelayMilliseconds", 180.0);
        Hover.TomlSection.insert("MaxWidthPixels",    320.0);
        Doc.EmbeddedPillars.push_back(std::move(Hover));
    }

    ExternalLinkEntry FontLink{};
    FontLink.LinkSlot          = "PrimaryTypeface";
    FontLink.TargetCategory    = FormatSuffixCategory::FontSpace;
    FontLink.RelativePath      = "EngineContent/FontsArchives/FontRepository.FontSpace";
    FontLink.StableIdentifier  = "0193b5c0-7a1e-7000-8000-000000000099";
    FontLink.ExpectedSha256Hex = FormatSha256Hex(ComputeSha256String("FontRepository.FontSpace"));
    Doc.ExternalLinks.push_back(std::move(FontLink));

    return Doc;
}

} // namespace Frontier::Serialization
