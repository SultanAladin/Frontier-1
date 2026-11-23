/*============================================================================================================================================
                                                         PHOTOMETRYSPACECODEC.CPP
============================================================================================================================================*/
// 🧩 Implementation of .PhotometrySpace Two-Tier Hybrid encoder & decoder (TOML manifest + 64B-aligned 128x32 float32 CandelaDistribution)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/PhotometrySpaceCodec.h"

#include <cstring>

namespace Frontier::Codecs
{

namespace
{

[[nodiscard]] toml::array PackFloat3(float X, float Y, float Z)
{
    return toml::array{ static_cast<double>(X), static_cast<double>(Y), static_cast<double>(Z) };
}

void UnpackFloat3(const toml::node_view<const toml::node>& Node, float& OutX, float& OutY, float& OutZ) noexcept
{
    if (const auto* Arr = Node.as_array(); Arr != nullptr && Arr->size() >= 3u)
    {
        OutX = static_cast<float>((*Arr)[0u].value_or(static_cast<double>(OutX)));
        OutY = static_cast<float>((*Arr)[1u].value_or(static_cast<double>(OutY)));
        OutZ = static_cast<float>((*Arr)[2u].value_or(static_cast<double>(OutZ)));
    }
}

[[nodiscard]] std::string_view CategoryToString(PhotometryLuminaireCategory Category) noexcept
{
    switch (Category)
    {
        case PhotometryLuminaireCategory::SolarEphemeris:    return "SolarEphemeris";
        case PhotometryLuminaireCategory::AreaQuadLuminaire: return "AreaQuadLuminaire";
        case PhotometryLuminaireCategory::MeasuredCandela:   return "MeasuredCandela";
    }
    return "SolarEphemeris";
}

[[nodiscard]] PhotometryLuminaireCategory ParseCategoryString(std::string_view Text) noexcept
{
    if (Text == "AreaQuadLuminaire")
    {
        return PhotometryLuminaireCategory::AreaQuadLuminaire;
    }
    if (Text == "MeasuredCandela")
    {
        return PhotometryLuminaireCategory::MeasuredCandela;
    }
    return PhotometryLuminaireCategory::SolarEphemeris;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 PHOTOMETRY SPACE ENCODER
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload PhotometrySpaceEncoder::BuildPayload(
    const PhotometrySpaceDocumentModel& Model,
    const Workspaces::RevisionTracker*  OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.SuffixCategory     = SpaceSuffixCategory::PhotometrySpace;
    Payload.SchemaVersion      = 1u;
    Payload.DocumentTitle      = Model.DocumentTitle.empty() ? "UntitledLuminaire" : Model.DocumentTitle;
    Payload.AuthoringWorkspace = "Frontier.PhotometryWorkspace";

    Payload.DomainManifest.insert_or_assign("LuminaireCategory", std::string(CategoryToString(Model.LuminaireCategory)));

    if (Model.LuminaireCategory == PhotometryLuminaireCategory::SolarEphemeris)
    {
        toml::table SolarTbl{};
        SolarTbl.insert_or_assign("DirectionToSun", PackFloat3(Model.SolarSource.DirectionToSun.X, Model.SolarSource.DirectionToSun.Y, Model.SolarSource.DirectionToSun.Z));
        SolarTbl.insert_or_assign("SolarIrradiance", PackFloat3(Model.SolarSource.SolarIrradiance.R, Model.SolarSource.SolarIrradiance.G, Model.SolarSource.SolarIrradiance.B));
        SolarTbl.insert_or_assign("SunAngularRadiusRadians", static_cast<double>(Model.SolarSource.SunAngularRadiusRadians));
        SolarTbl.insert_or_assign("RtxSamplesPerPixel", static_cast<int64_t>(Model.SolarSource.RtxSamplesPerPixel));
        SolarTbl.insert_or_assign("SolarEnabled", Model.SolarEphemeris.SolarEnabled);
        SolarTbl.insert_or_assign("AngularDiameterDegrees", static_cast<double>(Model.SolarEphemeris.AngularDiameterDegrees));
        SolarTbl.insert_or_assign("TintSource", static_cast<int64_t>(Model.SolarEphemeris.TintSource));
        SolarTbl.insert_or_assign("ManualRgbTint", PackFloat3(Model.SolarEphemeris.ManualRgbTint.R, Model.SolarEphemeris.ManualRgbTint.G, Model.SolarEphemeris.ManualRgbTint.B));
        SolarTbl.insert_or_assign("ColorTemperatureKelvin", static_cast<double>(Model.SolarEphemeris.ColorTemperatureKelvin));
        SolarTbl.insert_or_assign("SolarIntensityMultiplier", static_cast<double>(Model.SolarEphemeris.SolarIntensityMultiplier));
        SolarTbl.insert_or_assign("DirectGainMultiplier", static_cast<double>(Model.SolarEphemeris.DirectGainMultiplier));
        SolarTbl.insert_or_assign("LocalHours", static_cast<double>(Model.SolarEphemeris.LocalHours));
        SolarTbl.insert_or_assign("CycleAnimationEnabled", Model.SolarEphemeris.CycleAnimationEnabled);
        SolarTbl.insert_or_assign("SiteLatitudeDegrees", static_cast<double>(Model.SolarEphemeris.SiteLatitudeDegrees));
        SolarTbl.insert_or_assign("SiteLongitudeDegrees", static_cast<double>(Model.SolarEphemeris.SiteLongitudeDegrees));
        SolarTbl.insert_or_assign("DayOfMonth", static_cast<int64_t>(Model.SolarEphemeris.DayOfMonth));
        SolarTbl.insert_or_assign("Month", static_cast<int64_t>(Model.SolarEphemeris.Month));
        SolarTbl.insert_or_assign("Year", static_cast<int64_t>(Model.SolarEphemeris.Year));
        SolarTbl.insert_or_assign("UtcOffsetHours", static_cast<double>(Model.SolarEphemeris.UtcOffsetHours));
        SolarTbl.insert_or_assign("ManualDirectionOverride", Model.SolarEphemeris.ManualDirectionOverride);
        SolarTbl.insert_or_assign("ManualAzimuthDegrees", static_cast<double>(Model.SolarEphemeris.ManualAzimuthDegrees));
        SolarTbl.insert_or_assign("ManualElevationDegrees", static_cast<double>(Model.SolarEphemeris.ManualElevationDegrees));
        SolarTbl.insert_or_assign("AppearanceMode", static_cast<int64_t>(Model.SolarEphemeris.AppearanceMode));
        SolarTbl.insert_or_assign("SunGlareStrength", static_cast<double>(Model.SolarEphemeris.SunGlareStrength));
        Payload.DomainManifest.insert_or_assign(".SolarEphemeris", std::move(SolarTbl));
    }
    else if (Model.LuminaireCategory == PhotometryLuminaireCategory::AreaQuadLuminaire)
    {
        toml::table AreaTbl{};
        AreaTbl.insert_or_assign("CenterWorld", PackFloat3(Model.AreaQuad.CenterWorld.X, Model.AreaQuad.CenterWorld.Y, Model.AreaQuad.CenterWorld.Z));
        AreaTbl.insert_or_assign("RightTangent", PackFloat3(Model.AreaQuad.RightTangent.X, Model.AreaQuad.RightTangent.Y, Model.AreaQuad.RightTangent.Z));
        AreaTbl.insert_or_assign("UpBitangent", PackFloat3(Model.AreaQuad.UpBitangent.X, Model.AreaQuad.UpBitangent.Y, Model.AreaQuad.UpBitangent.Z));
        AreaTbl.insert_or_assign("HalfWidthMeters", static_cast<double>(Model.AreaQuad.HalfWidthMeters));
        AreaTbl.insert_or_assign("HalfHeightMeters", static_cast<double>(Model.AreaQuad.HalfHeightMeters));
        AreaTbl.insert_or_assign("EmittedRadiance", PackFloat3(Model.AreaQuad.EmittedRadiance.R, Model.AreaQuad.EmittedRadiance.G, Model.AreaQuad.EmittedRadiance.B));
        AreaTbl.insert_or_assign("TwoSidedEmission", Model.AreaQuad.TwoSidedEmission);
        Payload.DomainManifest.insert_or_assign(".AreaLuminaire", std::move(AreaTbl));
    }
    else
    {
        toml::table CandelaTbl{};
        CandelaTbl.insert_or_assign("ProfileLabel", Model.CandelaProfile.ProfileLabel.empty() ? Model.DocumentTitle : Model.CandelaProfile.ProfileLabel);
        CandelaTbl.insert_or_assign("VerticalResolution", static_cast<int64_t>(PhotometricVerticalCount));
        CandelaTbl.insert_or_assign("HorizontalResolution", static_cast<int64_t>(PhotometricHorizontalCount));
        CandelaTbl.insert_or_assign("PeakCandela", static_cast<double>(Model.CandelaProfile.PeakCandela));
        CandelaTbl.insert_or_assign("NormalizedFluxSteradians", static_cast<double>(Model.CandelaProfile.NormalizedFluxSr));
        CandelaTbl.insert_or_assign("CutoffHalfAngleRadians", static_cast<double>(Model.CandelaProfile.CutoffHalfAngleRad));
        CandelaTbl.insert_or_assign("PositionWorld", PackFloat3(Model.RadiatorPlacement.PositionWorld.X, Model.RadiatorPlacement.PositionWorld.Y, Model.RadiatorPlacement.PositionWorld.Z));
        CandelaTbl.insert_or_assign("BeamAxis", PackFloat3(Model.RadiatorPlacement.BeamAxis.X, Model.RadiatorPlacement.BeamAxis.Y, Model.RadiatorPlacement.BeamAxis.Z));
        CandelaTbl.insert_or_assign("ZeroAzimuthAxis", PackFloat3(Model.RadiatorPlacement.ZeroAzimuthAxis.X, Model.RadiatorPlacement.ZeroAzimuthAxis.Y, Model.RadiatorPlacement.ZeroAzimuthAxis.Z));
        CandelaTbl.insert_or_assign("PeakIntensityRgb", PackFloat3(Model.RadiatorPlacement.PeakIntensityRgb.R, Model.RadiatorPlacement.PeakIntensityRgb.G, Model.RadiatorPlacement.PeakIntensityRgb.B));
        CandelaTbl.insert_or_assign("RangeMeters", static_cast<double>(Model.RadiatorPlacement.RangeMeters));
        CandelaTbl.insert_or_assign("RadiusMeters", static_cast<double>(Model.RadiatorPlacement.RadiusMeters));
        Payload.DomainManifest.insert_or_assign(".CandelaDistribution", std::move(CandelaTbl));

        if (Model.CandelaIncluded)
        {
            BinarySectionBlock GridSection{};
            GridSection.SectionIdentifier = "CandelaGrid128x32";
            GridSection.SubFormatTag      = ".CandelaDistribution";
            const std::size_t ByteLength  = Model.CandelaProfile.NormalizedCandela.size() * sizeof(float);
            GridSection.PayloadBytes.resize(ByteLength);
            std::memcpy(GridSection.PayloadBytes.data(), Model.CandelaProfile.NormalizedCandela.data(), ByteLength);
            Payload.BinarySections.push_back(std::move(GridSection));
        }
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool PhotometrySpaceEncoder::EncodeToBytes(
    const PhotometrySpaceDocumentModel& Model,
    std::vector<std::byte>&             OutContainerBytes,
    std::string&                        OutDiagnostic,
    const Workspaces::RevisionTracker*  OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToBytes(BuildPayload(Model, OptionalRevision), OutContainerBytes, OutDiagnostic);
}

bool PhotometrySpaceEncoder::EncodeToFile(
    const std::filesystem::path&        TargetPath,
    const PhotometrySpaceDocumentModel& Model,
    std::string&                        OutDiagnostic,
    const Workspaces::RevisionTracker*  OptionalRevision)
{
    SpaceContainerPayload Payload = BuildPayload(Model, OptionalRevision);
    SpaceSuffixCategory   PathCat{};
    std::string           CanonExt{};
    std::string           IgnoreDiag{};
    if (SpaceContainerDecoder::ResolveSuffixCaseInsensitive(TargetPath.extension().string(), PathCat, CanonExt, IgnoreDiag) &&
        (PathCat == SpaceSuffixCategory::PhotometrySpace || PathCat == SpaceSuffixCategory::Photometry || PathCat == SpaceSuffixCategory::InstanceSpace))
    {
        Payload.SuffixCategory = PathCat;
    }
    return SpaceContainerEncoder::EncodeToFile(TargetPath, Payload, OutDiagnostic);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 PHOTOMETRY SPACE DECODER
//------------------------------------------------------------------------------------------------------------------------

bool PhotometrySpaceDecoder::ExtractModel(
    const SpaceContainerPayload&  Payload,
    PhotometrySpaceDocumentModel& OutModel,
    std::string&                  OutDiagnostic,
    Workspaces::RevisionTracker*  OptionalOutRevision)
{
    if (Payload.SuffixCategory != SpaceSuffixCategory::PhotometrySpace &&
        Payload.SuffixCategory != SpaceSuffixCategory::Photometry &&
        Payload.SuffixCategory != SpaceSuffixCategory::InstanceSpace)
    {
        OutDiagnostic = "Expected .PhotometrySpace container category.";
        return false;
    }

    OutModel                   = PhotometrySpaceDocumentModel{};
    OutModel.DocumentTitle     = Payload.DocumentTitle;
    OutModel.LuminaireCategory = ParseCategoryString(Payload.DomainManifest["LuminaireCategory"].value_or(std::string_view{ "SolarEphemeris" }));

    if (const auto* SolarTbl = Payload.DomainManifest[".SolarEphemeris"].as_table())
    {
        UnpackFloat3((*SolarTbl)["DirectionToSun"], OutModel.SolarSource.DirectionToSun.X, OutModel.SolarSource.DirectionToSun.Y, OutModel.SolarSource.DirectionToSun.Z);
        UnpackFloat3((*SolarTbl)["SolarIrradiance"], OutModel.SolarSource.SolarIrradiance.R, OutModel.SolarSource.SolarIrradiance.G, OutModel.SolarSource.SolarIrradiance.B);
        OutModel.SolarSource.SunAngularRadiusRadians     = static_cast<float>((*SolarTbl)["SunAngularRadiusRadians"].value_or(0.028));
        OutModel.SolarSource.RtxSamplesPerPixel          = static_cast<uint32_t>((*SolarTbl)["RtxSamplesPerPixel"].value_or(int64_t{ 8 }));
        OutModel.SolarEphemeris.SolarEnabled             = (*SolarTbl)["SolarEnabled"].value_or(true);
        OutModel.SolarEphemeris.AngularDiameterDegrees   = static_cast<float>((*SolarTbl)["AngularDiameterDegrees"].value_or(0.533));
        OutModel.SolarEphemeris.TintSource               = static_cast<GeometricRaster::SolarTintSource>((*SolarTbl)["TintSource"].value_or(int64_t{ 0 }));
        UnpackFloat3((*SolarTbl)["ManualRgbTint"], OutModel.SolarEphemeris.ManualRgbTint.R, OutModel.SolarEphemeris.ManualRgbTint.G, OutModel.SolarEphemeris.ManualRgbTint.B);
        OutModel.SolarEphemeris.ColorTemperatureKelvin   = static_cast<float>((*SolarTbl)["ColorTemperatureKelvin"].value_or(6500.0));
        OutModel.SolarEphemeris.SolarIntensityMultiplier = static_cast<float>((*SolarTbl)["SolarIntensityMultiplier"].value_or(22.0));
        OutModel.SolarEphemeris.DirectGainMultiplier     = static_cast<float>((*SolarTbl)["DirectGainMultiplier"].value_or(2.50));
        OutModel.SolarEphemeris.LocalHours               = static_cast<float>((*SolarTbl)["LocalHours"].value_or(15.50));
        OutModel.SolarEphemeris.CycleAnimationEnabled    = (*SolarTbl)["CycleAnimationEnabled"].value_or(false);
        OutModel.SolarEphemeris.SiteLatitudeDegrees      = static_cast<float>((*SolarTbl)["SiteLatitudeDegrees"].value_or(-26.19));
        OutModel.SolarEphemeris.SiteLongitudeDegrees     = static_cast<float>((*SolarTbl)["SiteLongitudeDegrees"].value_or(28.32));
        OutModel.SolarEphemeris.DayOfMonth               = static_cast<uint32_t>((*SolarTbl)["DayOfMonth"].value_or(int64_t{ 10 }));
        OutModel.SolarEphemeris.Month                    = static_cast<uint32_t>((*SolarTbl)["Month"].value_or(int64_t{ 9 }));
        OutModel.SolarEphemeris.Year                     = static_cast<int32_t>((*SolarTbl)["Year"].value_or(int64_t{ 2026 }));
        OutModel.SolarEphemeris.UtcOffsetHours           = static_cast<float>((*SolarTbl)["UtcOffsetHours"].value_or(2.0));
        OutModel.SolarEphemeris.ManualDirectionOverride  = (*SolarTbl)["ManualDirectionOverride"].value_or(false);
        OutModel.SolarEphemeris.ManualAzimuthDegrees     = static_cast<float>((*SolarTbl)["ManualAzimuthDegrees"].value_or(295.0));
        OutModel.SolarEphemeris.ManualElevationDegrees   = static_cast<float>((*SolarTbl)["ManualElevationDegrees"].value_or(38.0));
        OutModel.SolarEphemeris.AppearanceMode           = static_cast<GeometricRaster::SolarAppearanceMode>((*SolarTbl)["AppearanceMode"].value_or(int64_t{ 1 }));
        OutModel.SolarEphemeris.SunGlareStrength         = static_cast<float>((*SolarTbl)["SunGlareStrength"].value_or(0.25));
    }

    if (const auto* AreaTbl = Payload.DomainManifest[".AreaLuminaire"].as_table())
    {
        UnpackFloat3((*AreaTbl)["CenterWorld"], OutModel.AreaQuad.CenterWorld.X, OutModel.AreaQuad.CenterWorld.Y, OutModel.AreaQuad.CenterWorld.Z);
        UnpackFloat3((*AreaTbl)["RightTangent"], OutModel.AreaQuad.RightTangent.X, OutModel.AreaQuad.RightTangent.Y, OutModel.AreaQuad.RightTangent.Z);
        UnpackFloat3((*AreaTbl)["UpBitangent"], OutModel.AreaQuad.UpBitangent.X, OutModel.AreaQuad.UpBitangent.Y, OutModel.AreaQuad.UpBitangent.Z);
        OutModel.AreaQuad.HalfWidthMeters  = static_cast<float>((*AreaTbl)["HalfWidthMeters"].value_or(0.85));
        OutModel.AreaQuad.HalfHeightMeters = static_cast<float>((*AreaTbl)["HalfHeightMeters"].value_or(0.45));
        UnpackFloat3((*AreaTbl)["EmittedRadiance"], OutModel.AreaQuad.EmittedRadiance.R, OutModel.AreaQuad.EmittedRadiance.G, OutModel.AreaQuad.EmittedRadiance.B);
        OutModel.AreaQuad.TwoSidedEmission = (*AreaTbl)["TwoSidedEmission"].value_or(false);
    }

    if (const auto* CandelaTbl = Payload.DomainManifest[".CandelaDistribution"].as_table())
    {
        OutModel.CandelaProfile.ProfileLabel       = (*CandelaTbl)["ProfileLabel"].value_or(Payload.DocumentTitle);
        OutModel.CandelaProfile.PeakCandela        = static_cast<float>((*CandelaTbl)["PeakCandela"].value_or(1.0));
        OutModel.CandelaProfile.NormalizedFluxSr   = static_cast<float>((*CandelaTbl)["NormalizedFluxSteradians"].value_or(1.0));
        OutModel.CandelaProfile.CutoffHalfAngleRad = static_cast<float>((*CandelaTbl)["CutoffHalfAngleRadians"].value_or(3.14159265));
        UnpackFloat3((*CandelaTbl)["PositionWorld"], OutModel.RadiatorPlacement.PositionWorld.X, OutModel.RadiatorPlacement.PositionWorld.Y, OutModel.RadiatorPlacement.PositionWorld.Z);
        UnpackFloat3((*CandelaTbl)["BeamAxis"], OutModel.RadiatorPlacement.BeamAxis.X, OutModel.RadiatorPlacement.BeamAxis.Y, OutModel.RadiatorPlacement.BeamAxis.Z);
        UnpackFloat3((*CandelaTbl)["ZeroAzimuthAxis"], OutModel.RadiatorPlacement.ZeroAzimuthAxis.X, OutModel.RadiatorPlacement.ZeroAzimuthAxis.Y, OutModel.RadiatorPlacement.ZeroAzimuthAxis.Z);
        UnpackFloat3((*CandelaTbl)["PeakIntensityRgb"], OutModel.RadiatorPlacement.PeakIntensityRgb.R, OutModel.RadiatorPlacement.PeakIntensityRgb.G, OutModel.RadiatorPlacement.PeakIntensityRgb.B);
        OutModel.RadiatorPlacement.RangeMeters  = static_cast<float>((*CandelaTbl)["RangeMeters"].value_or(24.0));
        OutModel.RadiatorPlacement.RadiusMeters = static_cast<float>((*CandelaTbl)["RadiusMeters"].value_or(0.06));
    }

    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SectionIdentifier == "CandelaGrid128x32" && Section.SubFormatTag == ".CandelaDistribution")
        {
            const std::size_t ExpectedBytes = OutModel.CandelaProfile.NormalizedCandela.size() * sizeof(float);
            if (Section.PayloadBytes.size() != ExpectedBytes)
            {
                OutDiagnostic = "Corrupt CandelaGrid128x32 byte length inside .PhotometrySpace.";
                return false;
            }
            std::memcpy(OutModel.CandelaProfile.NormalizedCandela.data(), Section.PayloadBytes.data(), ExpectedBytes);
            OutModel.CandelaIncluded = true;
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

bool PhotometrySpaceDecoder::DecodeFromBytes(
    std::span<const std::byte>    ContainerBytes,
    PhotometrySpaceDocumentModel& OutModel,
    std::string&                  OutDiagnostic,
    Workspaces::RevisionTracker*  OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromBytes(ContainerBytes, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractModel(Payload, OutModel, OutDiagnostic, OptionalOutRevision);
}

bool PhotometrySpaceDecoder::DecodeFromFile(
    const std::filesystem::path&  SourcePath,
    PhotometrySpaceDocumentModel& OutModel,
    std::string&                  OutDiagnostic,
    Workspaces::RevisionTracker*  OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractModel(Payload, OutModel, OutDiagnostic, OptionalOutRevision);
}

} // namespace Frontier::Codecs
