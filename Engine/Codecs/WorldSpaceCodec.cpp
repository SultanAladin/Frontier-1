/*============================================================================================================================================
                                                           WORLDSPACECODEC.CPP
============================================================================================================================================*/
// 🧩 Implementation of .WorldSpace Two-Tier Hybrid encoder & decoder (TOML world hierarchy + 64B-aligned packed placement transforms)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/WorldSpaceCodec.h"

#include <cstring>

namespace Frontier::Codecs
{

namespace
{

[[nodiscard]] toml::array PackWorldVec3(float X, float Y, float Z)
{
    return toml::array{ static_cast<double>(X), static_cast<double>(Y), static_cast<double>(Z) };
}

void UnpackWorldVec3(const toml::node_view<const toml::node>& Node, float& OutX, float& OutY, float& OutZ) noexcept
{
    if (const auto* Arr = Node.as_array(); Arr != nullptr && Arr->size() >= 3u)
    {
        OutX = static_cast<float>((*Arr)[0u].value_or(static_cast<double>(OutX)));
        OutY = static_cast<float>((*Arr)[1u].value_or(static_cast<double>(OutY)));
        OutZ = static_cast<float>((*Arr)[2u].value_or(static_cast<double>(OutZ)));
    }
}

struct PackedWorldPlacementBinaryRow
{
    float    CenterX          = 0.0f;
    float    CenterY          = 0.0f;
    float    CenterZ          = 0.0f;
    float    RotationZDegrees = 0.0f;
    float    ExtentX          = 1.0f;
    float    ExtentY          = 1.0f;
    float    ExtentZ          = 1.0f;
    uint32_t FlagsMask        = 0u;
};
static_assert(sizeof(PackedWorldPlacementBinaryRow) == 32u, "PackedWorldPlacementBinaryRow must equal 32 bytes.");

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  WORLD SPACE ENCODER
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload WorldSpaceEncoder::BuildPayload(
    const WorldSpaceDocumentModel&     Model,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.SuffixCategory     = SpaceSuffixCategory::WorldSpace;
    Payload.SchemaVersion      = 1u;
    Payload.DocumentTitle      = Model.DocumentTitle.empty() ? "VehicleClearSkyDefault" : Model.DocumentTitle;
    Payload.AuthoringWorkspace = "Frontier.WorldWorkspace";

    toml::table ReferencesSection{};
    ReferencesSection.insert_or_assign("SolarPhotometryPath", Model.SolarPhotometryPath);
    ReferencesSection.insert_or_assign("EnvironmentVolumetricPath", Model.EnvironmentVolumetricPath);
    ReferencesSection.insert_or_assign("RenderSettingsPath", Model.RenderSettingsPath);
    ReferencesSection.insert_or_assign("GeometryPlacementCount", static_cast<int64_t>(Model.GeometryPlacements.size()));
    ReferencesSection.insert_or_assign("AreaLuminaireCount", static_cast<int64_t>(Model.AreaLuminairePlacements.size()));
    ReferencesSection.insert_or_assign("MeasuredLuminaireCount", static_cast<int64_t>(Model.MeasuredLuminairePlacements.size()));
    Payload.DomainManifest.insert_or_assign(".WorldReferences", std::move(ReferencesSection));

    toml::table CameraTbl{};
    CameraTbl.insert_or_assign("CameraOrigin", PackWorldVec3(Model.EditorCamera.CameraOrigin.X, Model.EditorCamera.CameraOrigin.Y, Model.EditorCamera.CameraOrigin.Z));
    CameraTbl.insert_or_assign("TargetPoint", PackWorldVec3(Model.EditorCamera.TargetPoint.X, Model.EditorCamera.TargetPoint.Y, Model.EditorCamera.TargetPoint.Z));
    CameraTbl.insert_or_assign("WorldUpAxis", PackWorldVec3(Model.EditorCamera.WorldUpAxis.X, Model.EditorCamera.WorldUpAxis.Y, Model.EditorCamera.WorldUpAxis.Z));
    CameraTbl.insert_or_assign("VerticalFovRadians", static_cast<double>(Model.EditorCamera.VerticalFovRadians));
    CameraTbl.insert_or_assign("NearClipMeters", static_cast<double>(Model.EditorCamera.NearClipMeters));
    CameraTbl.insert_or_assign("FarClipMeters", static_cast<double>(Model.EditorCamera.FarClipMeters));
    Payload.DomainManifest.insert_or_assign(".EditorCamera", std::move(CameraTbl));

    if (!Model.GeometryPlacements.empty())
    {
        toml::array PlacementArr{};
        std::vector<PackedWorldPlacementBinaryRow> BinaryRows;
        BinaryRows.reserve(Model.GeometryPlacements.size());

        for (const WorldGeometryPlacementEntry& Entry : Model.GeometryPlacements)
        {
            toml::table Tbl{};
            Tbl.insert_or_assign("PlacementTitle", Entry.PlacementTitle);
            Tbl.insert_or_assign("StableIdentifier", Entry.StableIdentifier);
            Tbl.insert_or_assign("Subtitle", Entry.Subtitle);
            Tbl.insert_or_assign("AuthorNotes", Entry.AuthorNotes);
            Tbl.insert_or_assign("GeometrySpacePath", Entry.GeometrySpacePath);
            if (!Entry.ComponentSpacePath.empty())
            {
                Tbl.insert_or_assign("ComponentSpacePath", Entry.ComponentSpacePath);
            }
            Tbl.insert_or_assign("PrimaryMaterialPath", Entry.PrimaryMaterialPath);
            Tbl.insert_or_assign("CenterWorld", PackWorldVec3(Entry.CenterWorld.X, Entry.CenterWorld.Y, Entry.CenterWorld.Z));
            Tbl.insert_or_assign("DimensionsMeters", PackWorldVec3(Entry.DimensionsMeters.X, Entry.DimensionsMeters.Y, Entry.DimensionsMeters.Z));
            Tbl.insert_or_assign("RotationZDegrees", static_cast<double>(Entry.RotationZDegrees));
            Tbl.insert_or_assign("PrimitiveShapeOrdinal", static_cast<int64_t>(Entry.PrimitiveShapeOrdinal));
            Tbl.insert_or_assign("SphericalPrimitive", Entry.SphericalPrimitive);
            Tbl.insert_or_assign("GlassShell", Entry.GlassShell);
            Tbl.insert_or_assign("VisibilityEnabled", Entry.VisibilityEnabled);
            Tbl.insert_or_assign("CastShadows", Entry.CastShadows);
            PlacementArr.push_back(std::move(Tbl));

            PackedWorldPlacementBinaryRow Row{};
            Row.CenterX          = Entry.CenterWorld.X;
            Row.CenterY          = Entry.CenterWorld.Y;
            Row.CenterZ          = Entry.CenterWorld.Z;
            Row.RotationZDegrees = Entry.RotationZDegrees;
            Row.ExtentX          = Entry.DimensionsMeters.X;
            Row.ExtentY          = Entry.DimensionsMeters.Y;
            Row.ExtentZ          = Entry.DimensionsMeters.Z;
            Row.FlagsMask        = (Entry.VisibilityEnabled ? 1u : 0u) |
                                   (Entry.CastShadows ? 2u : 0u) |
                                   (Entry.SphericalPrimitive ? 4u : 0u) |
                                   (Entry.GlassShell ? 8u : 0u);
            BinaryRows.push_back(Row);
        }
        Payload.DomainManifest.insert_or_assign(".WorldPlacement", std::move(PlacementArr));

        BinarySectionBlock TransformBlock{};
        TransformBlock.SectionIdentifier = "WorldPlacementTransforms";
        TransformBlock.SubFormatTag      = ".WorldPlacement";
        const std::size_t ByteLength     = BinaryRows.size() * sizeof(PackedWorldPlacementBinaryRow);
        TransformBlock.PayloadBytes.resize(ByteLength);
        std::memcpy(TransformBlock.PayloadBytes.data(), BinaryRows.data(), ByteLength);
        Payload.BinarySections.push_back(std::move(TransformBlock));
    }

    if (!Model.AreaLuminairePlacements.empty())
    {
        toml::array AreaArr{};
        for (const WorldAreaLuminairePlacement& Area : Model.AreaLuminairePlacements)
        {
            toml::table Tbl{};
            Tbl.insert_or_assign("PlacementTitle", Area.PlacementTitle);
            Tbl.insert_or_assign("PhotometrySpacePath", Area.PhotometrySpacePath);
            Tbl.insert_or_assign("CenterWorld", PackWorldVec3(Area.CenterWorld.X, Area.CenterWorld.Y, Area.CenterWorld.Z));
            Tbl.insert_or_assign("VisibilityEnabled", Area.VisibilityEnabled);
            AreaArr.push_back(std::move(Tbl));
        }
        Payload.DomainManifest.insert_or_assign("AreaLuminaires", std::move(AreaArr));
    }

    if (!Model.MeasuredLuminairePlacements.empty())
    {
        toml::array MeasArr{};
        for (const WorldMeasuredLuminairePlacement& Lum : Model.MeasuredLuminairePlacements)
        {
            toml::table Tbl{};
            Tbl.insert_or_assign("PlacementTitle", Lum.PlacementTitle);
            Tbl.insert_or_assign("PhotometrySpacePath", Lum.PhotometrySpacePath);
            Tbl.insert_or_assign("PositionWorld", PackWorldVec3(Lum.PositionWorld.X, Lum.PositionWorld.Y, Lum.PositionWorld.Z));
            Tbl.insert_or_assign("BeamAxis", PackWorldVec3(Lum.BeamAxis.X, Lum.BeamAxis.Y, Lum.BeamAxis.Z));
            Tbl.insert_or_assign("ZeroAzimuthAxis", PackWorldVec3(Lum.ZeroAzimuthAxis.X, Lum.ZeroAzimuthAxis.Y, Lum.ZeroAzimuthAxis.Z));
            Tbl.insert_or_assign("PeakIntensityRgb", PackWorldVec3(Lum.PeakIntensityRgb.R, Lum.PeakIntensityRgb.G, Lum.PeakIntensityRgb.B));
            Tbl.insert_or_assign("RangeMeters", static_cast<double>(Lum.RangeMeters));
            Tbl.insert_or_assign("RadiusMeters", static_cast<double>(Lum.RadiusMeters));
            MeasArr.push_back(std::move(Tbl));
        }
        Payload.DomainManifest.insert_or_assign("MeasuredLuminaires", std::move(MeasArr));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool WorldSpaceEncoder::EncodeToBytes(
    const WorldSpaceDocumentModel&     Model,
    std::vector<std::byte>&            OutContainerBytes,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToBytes(BuildPayload(Model, OptionalRevision), OutContainerBytes, OutDiagnostic);
}

bool WorldSpaceEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const WorldSpaceDocumentModel&     Model,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload = BuildPayload(Model, OptionalRevision);
    SpaceSuffixCategory   PathCat{};
    std::string           CanonExt{};
    std::string           IgnoreDiag{};
    if (SpaceContainerDecoder::ResolveSuffixCaseInsensitive(TargetPath.extension().string(), PathCat, CanonExt, IgnoreDiag) &&
        (PathCat == SpaceSuffixCategory::WorldSpace || PathCat == SpaceSuffixCategory::InstanceSpace))
    {
        Payload.SuffixCategory = PathCat;
    }
    return SpaceContainerEncoder::EncodeToFile(TargetPath, Payload, OutDiagnostic);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  WORLD SPACE DECODER
//------------------------------------------------------------------------------------------------------------------------

bool WorldSpaceDecoder::ExtractModel(
    const SpaceContainerPayload& Payload,
    WorldSpaceDocumentModel&     OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.SuffixCategory != SpaceSuffixCategory::WorldSpace &&
        Payload.SuffixCategory != SpaceSuffixCategory::InstanceSpace)
    {
        OutDiagnostic = "Expected .WorldSpace container category.";
        return false;
    }

    OutModel               = WorldSpaceDocumentModel{};
    OutModel.DocumentTitle = Payload.DocumentTitle;

    const auto* ReferencesSection = Payload.DomainManifest[".WorldReferences"].as_table();
    if (ReferencesSection == nullptr)
    {
        ReferencesSection = Payload.DomainManifest[".WorldHierarchy"].as_table();
    }
    if (ReferencesSection != nullptr)
    {
        OutModel.SolarPhotometryPath       = (*ReferencesSection)["SolarPhotometryPath"].value_or(std::string{});
        OutModel.EnvironmentVolumetricPath = (*ReferencesSection)["EnvironmentVolumetricPath"].value_or(std::string{});
        OutModel.RenderSettingsPath        = (*ReferencesSection)["RenderSettingsPath"].value_or(std::string{});
    }

    if (const auto* CameraTbl = Payload.DomainManifest[".EditorCamera"].as_table())
    {
        UnpackWorldVec3((*CameraTbl)["CameraOrigin"], OutModel.EditorCamera.CameraOrigin.X, OutModel.EditorCamera.CameraOrigin.Y, OutModel.EditorCamera.CameraOrigin.Z);
        UnpackWorldVec3((*CameraTbl)["TargetPoint"], OutModel.EditorCamera.TargetPoint.X, OutModel.EditorCamera.TargetPoint.Y, OutModel.EditorCamera.TargetPoint.Z);
        UnpackWorldVec3((*CameraTbl)["WorldUpAxis"], OutModel.EditorCamera.WorldUpAxis.X, OutModel.EditorCamera.WorldUpAxis.Y, OutModel.EditorCamera.WorldUpAxis.Z);
        OutModel.EditorCamera.VerticalFovRadians = static_cast<float>((*CameraTbl)["VerticalFovRadians"].value_or(0.78539816));
        OutModel.EditorCamera.NearClipMeters     = static_cast<float>((*CameraTbl)["NearClipMeters"].value_or(0.10));
        OutModel.EditorCamera.FarClipMeters      = static_cast<float>((*CameraTbl)["FarClipMeters"].value_or(5000.0));
    }

    if (const auto* PlacementArr = Payload.DomainManifest[".WorldPlacement"].as_array())
    {
        for (const toml::node& Elem : *PlacementArr)
        {
            const auto* Tbl = Elem.as_table();
            if (Tbl == nullptr)
            {
                continue;
            }
            WorldGeometryPlacementEntry Entry{};
            Entry.PlacementTitle        = (*Tbl)["PlacementTitle"].value_or(std::string{ "Placement" });
            Entry.StableIdentifier      = (*Tbl)["StableIdentifier"].value_or(std::string{});
            Entry.Subtitle              = (*Tbl)["Subtitle"].value_or(std::string{});
            Entry.AuthorNotes           = (*Tbl)["AuthorNotes"].value_or(std::string{});
            Entry.GeometrySpacePath     = (*Tbl)["GeometrySpacePath"].value_or(std::string{});
            Entry.ComponentSpacePath    = (*Tbl)["ComponentSpacePath"].value_or(std::string{});
            Entry.PrimaryMaterialPath   = (*Tbl)["PrimaryMaterialPath"].value_or(std::string{});
            UnpackWorldVec3((*Tbl)["CenterWorld"], Entry.CenterWorld.X, Entry.CenterWorld.Y, Entry.CenterWorld.Z);
            UnpackWorldVec3((*Tbl)["DimensionsMeters"], Entry.DimensionsMeters.X, Entry.DimensionsMeters.Y, Entry.DimensionsMeters.Z);
            Entry.RotationZDegrees      = static_cast<float>((*Tbl)["RotationZDegrees"].value_or(0.0));
            Entry.PrimitiveShapeOrdinal = static_cast<uint32_t>((*Tbl)["PrimitiveShapeOrdinal"].value_or(int64_t{ 0 }));
            Entry.SphericalPrimitive    = (*Tbl)["SphericalPrimitive"].value_or(false);
            Entry.GlassShell            = (*Tbl)["GlassShell"].value_or(false);
            Entry.VisibilityEnabled     = (*Tbl)["VisibilityEnabled"].value_or(true);
            Entry.CastShadows           = (*Tbl)["CastShadows"].value_or(true);
            OutModel.GeometryPlacements.push_back(std::move(Entry));
        }
    }

    if (const auto* AreaArr = Payload.DomainManifest["AreaLuminaires"].as_array())
    {
        for (const toml::node& Elem : *AreaArr)
        {
            if (const auto* Tbl = Elem.as_table())
            {
                WorldAreaLuminairePlacement Area{};
                Area.PlacementTitle      = (*Tbl)["PlacementTitle"].value_or(std::string{ "AreaLuminaire" });
                Area.PhotometrySpacePath = (*Tbl)["PhotometrySpacePath"].value_or(std::string{});
                UnpackWorldVec3((*Tbl)["CenterWorld"], Area.CenterWorld.X, Area.CenterWorld.Y, Area.CenterWorld.Z);
                Area.VisibilityEnabled   = (*Tbl)["VisibilityEnabled"].value_or(true);
                OutModel.AreaLuminairePlacements.push_back(std::move(Area));
            }
        }
    }

    if (const auto* MeasArr = Payload.DomainManifest["MeasuredLuminaires"].as_array())
    {
        for (const toml::node& Elem : *MeasArr)
        {
            if (const auto* Tbl = Elem.as_table())
            {
                WorldMeasuredLuminairePlacement Lum{};
                Lum.PlacementTitle      = (*Tbl)["PlacementTitle"].value_or(std::string{ "MeasuredLuminaire" });
                Lum.PhotometrySpacePath = (*Tbl)["PhotometrySpacePath"].value_or(std::string{});
                UnpackWorldVec3((*Tbl)["PositionWorld"], Lum.PositionWorld.X, Lum.PositionWorld.Y, Lum.PositionWorld.Z);
                UnpackWorldVec3((*Tbl)["BeamAxis"], Lum.BeamAxis.X, Lum.BeamAxis.Y, Lum.BeamAxis.Z);
                UnpackWorldVec3((*Tbl)["ZeroAzimuthAxis"], Lum.ZeroAzimuthAxis.X, Lum.ZeroAzimuthAxis.Y, Lum.ZeroAzimuthAxis.Z);
                UnpackWorldVec3((*Tbl)["PeakIntensityRgb"], Lum.PeakIntensityRgb.R, Lum.PeakIntensityRgb.G, Lum.PeakIntensityRgb.B);
                Lum.RangeMeters  = static_cast<float>((*Tbl)["RangeMeters"].value_or(24.0));
                Lum.RadiusMeters = static_cast<float>((*Tbl)["RadiusMeters"].value_or(0.06));
                OutModel.MeasuredLuminairePlacements.push_back(std::move(Lum));
            }
        }
    }

    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SectionIdentifier == "WorldPlacementTransforms" && Section.SubFormatTag == ".WorldPlacement")
        {
            if (Section.PayloadBytes.size() != OutModel.GeometryPlacements.size() * sizeof(PackedWorldPlacementBinaryRow))
            {
                OutDiagnostic = "WorldPlacementTransforms binary row count mismatch inside .WorldSpace.";
                return false;
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        if (!RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic))
        {
            return false;
        }
    }

    OutDiagnostic.clear();
    return true;
}

bool WorldSpaceDecoder::DecodeFromBytes(
    std::span<const std::byte>   ContainerBytes,
    WorldSpaceDocumentModel&     OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromBytes(ContainerBytes, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractModel(Payload, OutModel, OutDiagnostic, OptionalOutRevision);
}

bool WorldSpaceDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    WorldSpaceDocumentModel&     OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractModel(Payload, OutModel, OutDiagnostic, OptionalOutRevision);
}

} // namespace Frontier::Codecs
