/*============================================================================================================================================
                                                         VOLUMETRICSPACECODEC.CPP
============================================================================================================================================*/
// 🧩 Implementation of .VolumetricSpace Two-Tier Hybrid encoder & decoder (Sky, Celestials, Static Clouds, Fog & 64B Condensate Volumes)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/VolumetricSpaceCodec.h"

#include <cstring>

namespace Frontier::Codecs
{

namespace
{

[[nodiscard]] toml::array PackVec3(float X, float Y, float Z)
{
    return toml::array{ static_cast<double>(X), static_cast<double>(Y), static_cast<double>(Z) };
}

void UnpackVec3(const toml::node_view<const toml::node>& Node, float& OutX, float& OutY, float& OutZ) noexcept
{
    if (const auto* Arr = Node.as_array(); Arr != nullptr && Arr->size() >= 3u)
    {
        OutX = static_cast<float>((*Arr)[0u].value_or(static_cast<double>(OutX)));
        OutY = static_cast<float>((*Arr)[1u].value_or(static_cast<double>(OutY)));
        OutZ = static_cast<float>((*Arr)[2u].value_or(static_cast<double>(OutZ)));
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 VOLUMETRIC SPACE ENCODER
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload VolumetricSpaceEncoder::BuildPayload(
    const VolumetricSpaceDocumentModel& Model,
    const Workspaces::RevisionTracker*  OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.SuffixCategory     = SpaceSuffixCategory::VolumetricSpace;
    Payload.SchemaVersion      = 1u;
    Payload.DocumentTitle      = Model.DocumentTitle.empty() ? "ClearSkyDaylight" : Model.DocumentTitle;
    Payload.AuthoringWorkspace = "Frontier.VolumetricWorkspace";

    const auto& Atmo = Model.Atmosphere;

    toml::table MediumTbl{};
    MediumTbl.insert_or_assign("AtmosphereEnabled", Atmo.Medium.AtmosphereEnabled);
    MediumTbl.insert_or_assign("RayleighStrength", static_cast<double>(Atmo.Medium.RayleighStrength));
    MediumTbl.insert_or_assign("MieStrength", static_cast<double>(Atmo.Medium.MieStrength));
    MediumTbl.insert_or_assign("MieAnisotropy", static_cast<double>(Atmo.Medium.MieAnisotropy));
    MediumTbl.insert_or_assign("OzoneStrength", static_cast<double>(Atmo.Medium.OzoneStrength));
    MediumTbl.insert_or_assign("RayleighScaleHeightMeters", static_cast<double>(Atmo.Medium.RayleighScaleHeightMeters));
    MediumTbl.insert_or_assign("MieScaleHeightMeters", static_cast<double>(Atmo.Medium.MieScaleHeightMeters));
    MediumTbl.insert_or_assign("AtmosphereThicknessMeters", static_cast<double>(Atmo.Medium.AtmosphereThicknessMeters));
    MediumTbl.insert_or_assign("PlanetRadiusMeters", static_cast<double>(Atmo.Medium.PlanetRadiusMeters));
    MediumTbl.insert_or_assign("LimbGlowIntensity", static_cast<double>(Atmo.Medium.LimbGlowIntensity));
    MediumTbl.insert_or_assign("SkyBrightnessMultiplier", static_cast<double>(Atmo.Medium.SkyBrightnessMultiplier));
    MediumTbl.insert_or_assign("SkyTint", PackVec3(Atmo.Medium.SkyTint.R, Atmo.Medium.SkyTint.G, Atmo.Medium.SkyTint.B));
    MediumTbl.insert_or_assign("GroundAlbedo", PackVec3(Atmo.Medium.GroundAlbedo.R, Atmo.Medium.GroundAlbedo.G, Atmo.Medium.GroundAlbedo.B));
    MediumTbl.insert_or_assign("FallbackSkyRadiance", PackVec3(Model.FallbackSkyRadiance.R, Model.FallbackSkyRadiance.G, Model.FallbackSkyRadiance.B));
    MediumTbl.insert_or_assign("ObserverAltitudeMeters", static_cast<double>(Atmo.Medium.ObserverAltitudeMeters));
    MediumTbl.insert_or_assign("StarfieldEnabled", Atmo.Stars.StarfieldEnabled);
    MediumTbl.insert_or_assign("StarBrightnessMultiplier", static_cast<double>(Atmo.Stars.BrightnessMultiplier));
    MediumTbl.insert_or_assign("MoonPrimaryEnabled", Atmo.Moons[0u].Visible);
    MediumTbl.insert_or_assign("MoonPhaseFraction", static_cast<double>(Atmo.Moons[0u].SynodicPhaseFraction));
    MediumTbl.insert_or_assign("MoonAngularDiameterDegrees", static_cast<double>(Atmo.Moons[0u].AngularDiameterDegrees));
    MediumTbl.insert_or_assign("MoonAzimuthDegrees", static_cast<double>(Atmo.Moons[0u].AzimuthDegrees));
    MediumTbl.insert_or_assign("MoonElevationDegrees", static_cast<double>(Atmo.Moons[0u].ElevationDegrees));
    MediumTbl.insert_or_assign("GlobalWindSpeedMetersPerSecond", static_cast<double>(Atmo.Wind.SpeedMetersPerSecond));
    MediumTbl.insert_or_assign("GlobalWindBearingDegrees", static_cast<double>(Atmo.Wind.BearingDegrees));
    MediumTbl.insert_or_assign("HeightFogEnabled", Atmo.Fog.HeightFog.Enabled);
    MediumTbl.insert_or_assign("HeightFogDensity", static_cast<double>(Atmo.Fog.HeightFog.Density));
    MediumTbl.insert_or_assign("HeightFogFalloffHeightMeters", static_cast<double>(Atmo.Fog.HeightFog.FalloffHeightMeters));
    Payload.DomainManifest.insert_or_assign(".AtmosphericMedium", std::move(MediumTbl));

    toml::table CloudTbl{};
    CloudTbl.insert_or_assign("CloudsEnabled", Atmo.Clouds.CloudsEnabled);
    CloudTbl.insert_or_assign("FollowWindEnabled", Atmo.Clouds.FollowWindEnabled);
    CloudTbl.insert_or_assign("CategoryOrdinal", static_cast<int64_t>(Atmo.Clouds.Category));
    CloudTbl.insert_or_assign("CloudFraction", static_cast<double>(Atmo.Clouds.CloudFraction));
    CloudTbl.insert_or_assign("ExtinctionMultiplier", static_cast<double>(Atmo.Clouds.ExtinctionMultiplier));
    CloudTbl.insert_or_assign("FeatureScaleMultiplier", static_cast<double>(Atmo.Clouds.FeatureScaleMultiplier));
    CloudTbl.insert_or_assign("CloudFloorMeters", static_cast<double>(Atmo.Clouds.CloudFloorMeters));
    CloudTbl.insert_or_assign("LayerThicknessMeters", static_cast<double>(Atmo.Clouds.LayerThicknessMeters));
    CloudTbl.insert_or_assign("DeckCeilingMeters", static_cast<double>(Atmo.Clouds.DeckCeilingMeters));
    CloudTbl.insert_or_assign("AnvilTopSpread", static_cast<double>(Atmo.Clouds.AnvilTopSpread));
    CloudTbl.insert_or_assign("ForwardScatterAnisotropy", static_cast<double>(Atmo.Clouds.ForwardScatterAnisotropy));
    CloudTbl.insert_or_assign("WindSpeedMetersPerSecond", static_cast<double>(Atmo.Clouds.WindSpeedMetersPerSecond));
    CloudTbl.insert_or_assign("WindBearingDegrees", static_cast<double>(Atmo.Clouds.WindBearingDegrees));
    CloudTbl.insert_or_assign("CloudRenderScale", static_cast<double>(Atmo.Clouds.CloudRenderScale));
    CloudTbl.insert_or_assign("CloudMarchSteps", static_cast<int64_t>(Atmo.Clouds.CloudMarchSteps));
    CloudTbl.insert_or_assign("CloudShadowEnabled", Atmo.Clouds.CloudShadow.ShadowEnabled);
    CloudTbl.insert_or_assign("LocalCloudEnabled", Atmo.LocalCloud.LocalEnabled);
    CloudTbl.insert_or_assign("ShapeVolumeDimension", static_cast<int64_t>(Model.ShapeVolumeDimension));
    CloudTbl.insert_or_assign("ErosionVolumeDimension", static_cast<int64_t>(Model.ErosionVolumeDimension));
    Payload.DomainManifest.insert_or_assign(".CloudDeck", std::move(CloudTbl));

    if (!Model.ShapeNoiseTexels.empty())
    {
        BinarySectionBlock ShapeBlock{};
        ShapeBlock.SectionIdentifier = "ShapeCondensateVolume";
        ShapeBlock.SubFormatTag      = ".CondensateVolume";
        ShapeBlock.PayloadBytes.resize(Model.ShapeNoiseTexels.size());
        std::memcpy(ShapeBlock.PayloadBytes.data(), Model.ShapeNoiseTexels.data(), Model.ShapeNoiseTexels.size());
        Payload.BinarySections.push_back(std::move(ShapeBlock));
    }

    if (!Model.ErosionNoiseTexels.empty())
    {
        BinarySectionBlock ErosionBlock{};
        ErosionBlock.SectionIdentifier = "ErosionCondensateVolume";
        ErosionBlock.SubFormatTag      = ".CondensateVolume";
        ErosionBlock.PayloadBytes.resize(Model.ErosionNoiseTexels.size());
        std::memcpy(ErosionBlock.PayloadBytes.data(), Model.ErosionNoiseTexels.data(), Model.ErosionNoiseTexels.size());
        Payload.BinarySections.push_back(std::move(ErosionBlock));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool VolumetricSpaceEncoder::EncodeToBytes(
    const VolumetricSpaceDocumentModel& Model,
    std::vector<std::byte>&             OutContainerBytes,
    std::string&                        OutDiagnostic,
    const Workspaces::RevisionTracker*  OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToBytes(BuildPayload(Model, OptionalRevision), OutContainerBytes, OutDiagnostic);
}

bool VolumetricSpaceEncoder::EncodeToFile(
    const std::filesystem::path&        TargetPath,
    const VolumetricSpaceDocumentModel& Model,
    std::string&                        OutDiagnostic,
    const Workspaces::RevisionTracker*  OptionalRevision)
{
    SpaceContainerPayload Payload = BuildPayload(Model, OptionalRevision);
    SpaceSuffixCategory   PathCat{};
    std::string           CanonExt{};
    std::string           IgnoreDiag{};
    if (SpaceContainerDecoder::ResolveSuffixCaseInsensitive(TargetPath.extension().string(), PathCat, CanonExt, IgnoreDiag) &&
        (PathCat == SpaceSuffixCategory::VolumetricSpace || PathCat == SpaceSuffixCategory::InstanceSpace))
    {
        Payload.SuffixCategory = PathCat;
    }
    return SpaceContainerEncoder::EncodeToFile(TargetPath, Payload, OutDiagnostic);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 VOLUMETRIC SPACE DECODER
//------------------------------------------------------------------------------------------------------------------------

bool VolumetricSpaceDecoder::ExtractModel(
    const SpaceContainerPayload&  Payload,
    VolumetricSpaceDocumentModel& OutModel,
    std::string&                  OutDiagnostic,
    Workspaces::RevisionTracker*  OptionalOutRevision)
{
    if (Payload.SuffixCategory != SpaceSuffixCategory::VolumetricSpace &&
        Payload.SuffixCategory != SpaceSuffixCategory::InstanceSpace)
    {
        OutDiagnostic = "Expected .VolumetricSpace container category.";
        return false;
    }

    OutModel               = VolumetricSpaceDocumentModel{};
    OutModel.DocumentTitle = Payload.DocumentTitle;
    auto& Atmo             = OutModel.Atmosphere;

    if (const auto* MediumTbl = Payload.DomainManifest[".AtmosphericMedium"].as_table())
    {
        Atmo.Medium.AtmosphereEnabled         = (*MediumTbl)["AtmosphereEnabled"].value_or(true);
        Atmo.Medium.RayleighStrength          = static_cast<float>((*MediumTbl)["RayleighStrength"].value_or(1.0));
        Atmo.Medium.MieStrength               = static_cast<float>((*MediumTbl)["MieStrength"].value_or(1.0));
        Atmo.Medium.MieAnisotropy             = static_cast<float>((*MediumTbl)["MieAnisotropy"].value_or(0.78));
        Atmo.Medium.OzoneStrength             = static_cast<float>((*MediumTbl)["OzoneStrength"].value_or(1.2));
        Atmo.Medium.RayleighScaleHeightMeters = static_cast<float>((*MediumTbl)["RayleighScaleHeightMeters"].value_or(8000.0));
        Atmo.Medium.MieScaleHeightMeters      = static_cast<float>((*MediumTbl)["MieScaleHeightMeters"].value_or(1200.0));
        Atmo.Medium.AtmosphereThicknessMeters = static_cast<float>((*MediumTbl)["AtmosphereThicknessMeters"].value_or(60000.0));
        Atmo.Medium.PlanetRadiusMeters        = static_cast<float>((*MediumTbl)["PlanetRadiusMeters"].value_or(6360000.0));
        Atmo.Medium.LimbGlowIntensity         = static_cast<float>((*MediumTbl)["LimbGlowIntensity"].value_or(1.0));
        Atmo.Medium.SkyBrightnessMultiplier   = static_cast<float>((*MediumTbl)["SkyBrightnessMultiplier"].value_or(1.0));
        UnpackVec3((*MediumTbl)["SkyTint"], Atmo.Medium.SkyTint.R, Atmo.Medium.SkyTint.G, Atmo.Medium.SkyTint.B);
        UnpackVec3((*MediumTbl)["GroundAlbedo"], Atmo.Medium.GroundAlbedo.R, Atmo.Medium.GroundAlbedo.G, Atmo.Medium.GroundAlbedo.B);
        UnpackVec3((*MediumTbl)["FallbackSkyRadiance"], OutModel.FallbackSkyRadiance.R, OutModel.FallbackSkyRadiance.G, OutModel.FallbackSkyRadiance.B);
        Atmo.Medium.ObserverAltitudeMeters    = static_cast<float>((*MediumTbl)["ObserverAltitudeMeters"].value_or(100.0));
        Atmo.Stars.StarfieldEnabled              = (*MediumTbl)["StarfieldEnabled"].value_or(false);
        Atmo.Stars.BrightnessMultiplier          = static_cast<float>((*MediumTbl)["StarBrightnessMultiplier"].value_or(1.0));
        Atmo.Moons[0u].Visible                   = (*MediumTbl)["MoonPrimaryEnabled"].value_or(false);
        Atmo.Moons[0u].SynodicPhaseFraction      = static_cast<float>((*MediumTbl)["MoonPhaseFraction"].value_or(0.62));
        Atmo.Moons[0u].AngularDiameterDegrees    = static_cast<float>((*MediumTbl)["MoonAngularDiameterDegrees"].value_or(1.60));
        Atmo.Moons[0u].AzimuthDegrees            = static_cast<float>((*MediumTbl)["MoonAzimuthDegrees"].value_or(300.0));
        Atmo.Moons[0u].ElevationDegrees          = static_cast<float>((*MediumTbl)["MoonElevationDegrees"].value_or(28.0));
        Atmo.Wind.SpeedMetersPerSecond           = static_cast<float>((*MediumTbl)["GlobalWindSpeedMetersPerSecond"].value_or(0.0));
        Atmo.Wind.BearingDegrees                 = static_cast<float>((*MediumTbl)["GlobalWindBearingDegrees"].value_or(250.0));
        Atmo.Fog.HeightFog.Enabled               = (*MediumTbl)["HeightFogEnabled"].value_or(false);
        Atmo.Fog.HeightFog.Density               = static_cast<float>((*MediumTbl)["HeightFogDensity"].value_or(0.020));
        Atmo.Fog.HeightFog.FalloffHeightMeters   = static_cast<float>((*MediumTbl)["HeightFogFalloffHeightMeters"].value_or(400.0));
    }

    if (const auto* CloudTbl = Payload.DomainManifest[".CloudDeck"].as_table())
    {
        Atmo.Clouds.CloudsEnabled            = (*CloudTbl)["CloudsEnabled"].value_or(false);
        Atmo.Clouds.FollowWindEnabled        = (*CloudTbl)["FollowWindEnabled"].value_or(false);
        Atmo.Clouds.Category                 = static_cast<GeometricRaster::CloudCategoryType>((*CloudTbl)["CategoryOrdinal"].value_or(int64_t{ 2 }));
        Atmo.Clouds.CloudFraction            = static_cast<float>((*CloudTbl)["CloudFraction"].value_or(0.34));
        Atmo.Clouds.ExtinctionMultiplier     = static_cast<float>((*CloudTbl)["ExtinctionMultiplier"].value_or(2.40));
        Atmo.Clouds.FeatureScaleMultiplier   = static_cast<float>((*CloudTbl)["FeatureScaleMultiplier"].value_or(0.35));
        Atmo.Clouds.CloudFloorMeters         = static_cast<float>((*CloudTbl)["CloudFloorMeters"].value_or(1400.0));
        Atmo.Clouds.LayerThicknessMeters     = static_cast<float>((*CloudTbl)["LayerThicknessMeters"].value_or(1100.0));
        Atmo.Clouds.DeckCeilingMeters        = static_cast<float>((*CloudTbl)["DeckCeilingMeters"].value_or(16000.0));
        Atmo.Clouds.AnvilTopSpread           = static_cast<float>((*CloudTbl)["AnvilTopSpread"].value_or(0.50));
        Atmo.Clouds.ForwardScatterAnisotropy = static_cast<float>((*CloudTbl)["ForwardScatterAnisotropy"].value_or(0.45));
        Atmo.Clouds.WindSpeedMetersPerSecond = static_cast<float>((*CloudTbl)["WindSpeedMetersPerSecond"].value_or(0.0));
        Atmo.Clouds.WindBearingDegrees       = static_cast<float>((*CloudTbl)["WindBearingDegrees"].value_or(250.0));
        Atmo.Clouds.CloudRenderScale         = static_cast<float>((*CloudTbl)["CloudRenderScale"].value_or(1.0));
        Atmo.Clouds.CloudMarchSteps          = static_cast<uint32_t>((*CloudTbl)["CloudMarchSteps"].value_or(int64_t{ 64 }));
        Atmo.Clouds.CloudShadow.ShadowEnabled = (*CloudTbl)["CloudShadowEnabled"].value_or(false);
        Atmo.LocalCloud.LocalEnabled         = (*CloudTbl)["LocalCloudEnabled"].value_or(false);
        OutModel.ShapeVolumeDimension        = static_cast<uint32_t>((*CloudTbl)["ShapeVolumeDimension"].value_or(int64_t{ 0 }));
        OutModel.ErosionVolumeDimension      = static_cast<uint32_t>((*CloudTbl)["ErosionVolumeDimension"].value_or(int64_t{ 0 }));
    }

    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SectionIdentifier == "ShapeCondensateVolume" && Section.SubFormatTag == ".CondensateVolume")
        {
            OutModel.ShapeNoiseTexels.resize(Section.PayloadBytes.size());
            std::memcpy(OutModel.ShapeNoiseTexels.data(), Section.PayloadBytes.data(), Section.PayloadBytes.size());
        }
        else if (Section.SectionIdentifier == "ErosionCondensateVolume" && Section.SubFormatTag == ".CondensateVolume")
        {
            OutModel.ErosionNoiseTexels.resize(Section.PayloadBytes.size());
            std::memcpy(OutModel.ErosionNoiseTexels.data(), Section.PayloadBytes.data(), Section.PayloadBytes.size());
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

bool VolumetricSpaceDecoder::DecodeFromBytes(
    std::span<const std::byte>    ContainerBytes,
    VolumetricSpaceDocumentModel& OutModel,
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

bool VolumetricSpaceDecoder::DecodeFromFile(
    const std::filesystem::path&  SourcePath,
    VolumetricSpaceDocumentModel& OutModel,
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
