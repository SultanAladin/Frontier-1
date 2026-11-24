/*============================================================================================================================================
                                                      INSPECTORINSTANCEREGISTER.CPP
============================================================================================================================================*/
// 🧩 Implementation of per-entry multi-instance parameter register & generic multi-source scene combination

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace Frontier
{

namespace
{
constexpr float PiF = 3.14159265359f;
} // namespace

void InspectorInstanceRegister::Clear() noexcept
{
    Atmospheres.clear();
    Suns.clear();
    Clouds.clear();
    LocalClouds.clear();
    Moons.clear();
    Starfields.clear();
    Winds.clear();
    HeightFogs.clear();
    AtmosphericFogs.clear();
    LocalFogs.clear();
    GodRays.clear();
    Precipitations.clear();
    Rainbows.clear();
    Cameras.clear();
    CameraSettingsEntries.clear();
    LensFlares.clear();
    MotionBlurs.clear();
    PointLights.clear();
    SpotLights.clear();
    IesLights.clear();
    AreaLights.clear();
    TubeLights.clear();
    LedLights.clear();
    LedStrips.clear();
    EnvironmentLights.clear();
    PostProcesses.clear();
    AnalyticPrimitives.clear();
    NativeGeometries.clear();
    ImportedGeometries.clear();
    ProceduralGeometries.clear();
}

void InspectorInstanceRegister::EraseEntry(OutlinerSlotToken Token) noexcept
{
    const uint64_t Key = Token.Identifier;
    Atmospheres.erase(Key);
    Suns.erase(Key);
    Clouds.erase(Key);
    LocalClouds.erase(Key);
    Moons.erase(Key);
    Starfields.erase(Key);
    Winds.erase(Key);
    HeightFogs.erase(Key);
    AtmosphericFogs.erase(Key);
    LocalFogs.erase(Key);
    GodRays.erase(Key);
    Precipitations.erase(Key);
    Rainbows.erase(Key);
    Cameras.erase(Key);
    CameraSettingsEntries.erase(Key);
    LensFlares.erase(Key);
    MotionBlurs.erase(Key);
    PointLights.erase(Key);
    SpotLights.erase(Key);
    IesLights.erase(Key);
    AreaLights.erase(Key);
    TubeLights.erase(Key);
    LedLights.erase(Key);
    LedStrips.erase(Key);
    EnvironmentLights.erase(Key);
    PostProcesses.erase(Key);
    AnalyticPrimitives.erase(Key);
    NativeGeometries.erase(Key);
    ImportedGeometries.erase(Key);
    ProceduralGeometries.erase(Key);
}

AtmosphereInstanceData& InspectorInstanceRegister::AccessAtmosphere(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = Atmospheres.try_emplace(Token.Identifier);
    if (Inserted) It->second.Medium = Scene.Atmosphere.Medium;
    return It->second;
}

SolarInstanceData& InspectorInstanceRegister::AccessSolar(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = Suns.try_emplace(Token.Identifier);
    if (Inserted) It->second.Solar = Scene.Atmosphere.Solar;
    return It->second;
}

CloudInstanceData& InspectorInstanceRegister::AccessCloud(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = Clouds.try_emplace(Token.Identifier);
    if (Inserted) It->second.Clouds = Scene.Atmosphere.Clouds;
    return It->second;
}

LocalCloudInstanceData& InspectorInstanceRegister::AccessLocalCloud(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = LocalClouds.try_emplace(Token.Identifier);
    if (Inserted) It->second.Volume = Scene.Atmosphere.LocalCloud;
    return It->second;
}

LunarInstanceData& InspectorInstanceRegister::AccessLunar(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene, uint32_t SeedSlot)
{
    auto [It, Inserted] = Moons.try_emplace(Token.Identifier);
    if (Inserted)
    {
        It->second.Moon = Scene.Atmosphere.Moons[std::min<uint32_t>(SeedSlot, 5u)];
        It->second.Moon.Visible = true;
    }
    return It->second;
}

StarfieldInstanceData& InspectorInstanceRegister::AccessStarfield(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = Starfields.try_emplace(Token.Identifier);
    if (Inserted) It->second.Stars = Scene.Atmosphere.Stars;
    return It->second;
}

WindInstanceData& InspectorInstanceRegister::AccessWind(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = Winds.try_emplace(Token.Identifier);
    if (Inserted) It->second.Wind = Scene.Atmosphere.Wind;
    return It->second;
}

HeightFogInstanceData& InspectorInstanceRegister::AccessHeightFog(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = HeightFogs.try_emplace(Token.Identifier);
    if (Inserted) It->second.HeightFog = Scene.Atmosphere.Fog.HeightFog;
    return It->second;
}

AtmosphericFogInstanceData& InspectorInstanceRegister::AccessAtmosphericFog(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = AtmosphericFogs.try_emplace(Token.Identifier);
    if (Inserted) It->second.AerialFog = Scene.Atmosphere.Fog.AerialFog;
    return It->second;
}

LocalFogInstanceData& InspectorInstanceRegister::AccessLocalFog(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = LocalFogs.try_emplace(Token.Identifier);
    if (Inserted && !Scene.Atmosphere.Fog.LocalFogs.empty())
    {
        It->second.LocalFog = Scene.Atmosphere.Fog.LocalFogs.front();
    }
    return It->second;
}

GodRaysInstanceData& InspectorInstanceRegister::AccessGodRays(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = GodRays.try_emplace(Token.Identifier);
    if (Inserted) It->second.GodRays = Scene.Atmosphere.Fog.GodRays;
    return It->second;
}

PrecipitationInstanceData& InspectorInstanceRegister::AccessPrecipitation(OutlinerSlotToken Token)
{
    return Precipitations[Token.Identifier];
}

RainbowInstanceData& InspectorInstanceRegister::AccessRainbow(OutlinerSlotToken Token)
{
    return Rainbows[Token.Identifier];
}

CameraInstanceData& InspectorInstanceRegister::AccessCamera(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = Cameras.try_emplace(Token.Identifier);
    if (Inserted) It->second.LensFlare = Scene.Atmosphere.LensFlare;
    return It->second;
}

CameraSettingsData& InspectorInstanceRegister::AccessCameraSettings(OutlinerSlotToken Token)
{
    return CameraSettingsEntries[Token.Identifier];
}

LensFlareInstanceData& InspectorInstanceRegister::AccessLensFlare(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = LensFlares.try_emplace(Token.Identifier);
    if (Inserted) It->second.Flare = Scene.Atmosphere.LensFlare;
    return It->second;
}

MotionBlurInstanceData& InspectorInstanceRegister::AccessMotionBlur(OutlinerSlotToken Token)
{
    return MotionBlurs[Token.Identifier];
}

namespace
{
[[nodiscard]] bool ContainsLower(std::string_view Haystack, std::string_view Needle) noexcept
{
    if (Needle.empty() || Haystack.size() < Needle.size()) return false;
    for (std::size_t I = 0u; I + Needle.size() <= Haystack.size(); ++I)
    {
        bool Match = true;
        for (std::size_t J = 0u; J < Needle.size(); ++J)
        {
            const unsigned char A = static_cast<unsigned char>(Haystack[I + J]);
            const unsigned char B = static_cast<unsigned char>(Needle[J]);
            if (std::tolower(A) != std::tolower(B)) { Match = false; break; }
        }
        if (Match) return true;
    }
    return false;
}

void SeedSurfaceFromScene(GeometrySurfaceBinding& Binding, const GeometricRaster::WorldSceneSpecification& Scene) noexcept
{
    if (Scene.MaterialRegister.QuerySlotCount() > 0u)
    {
        const uint16_t Slot = std::min<uint16_t>(Binding.MaterialSlotOrdinal, static_cast<uint16_t>(Scene.MaterialRegister.QuerySlotCount() - 1u));
        const auto& Authored = Scene.MaterialRegister.InspectAuthoredMaterial(Slot);
        Binding.AlbedoTint = Authored.Diffuse.BaseColor;
        Binding.Roughness  = Authored.Specular.Roughness;
        Binding.Metallic   = Authored.Specular.Metalness;
    }
}
} // namespace

PointLightData& InspectorInstanceRegister::AccessPointLight(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = PointLights.try_emplace(Token.Identifier);
    if (Inserted && !Scene.PointLights.empty())
    {
        It->second.RadiantTint       = Scene.PointLights.front().Color;
        It->second.IntensityCandela   = std::clamp(Scene.PointLights.front().Intensity * 12.0f, 1.0f, 60.0f);
        It->second.ReachMeters        = std::max(4.0f, Scene.PointLights.front().RangeMeters);
        It->second.SourceRadiusMeters = std::max(0.02f, Scene.PointLights.front().RadiusMeters);
    }
    return It->second;
}

SpotLightData& InspectorInstanceRegister::AccessSpotLight(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = SpotLights.try_emplace(Token.Identifier);
    if (Inserted && !Scene.PointLights.empty())
    {
        It->second.RadiantTint = Scene.PointLights.front().Color;
    }
    return It->second;
}

IesLightData& InspectorInstanceRegister::AccessIesLight(
    OutlinerSlotToken Token, const OutlinerRecordEntry& Entry)
{
    auto [It, Inserted] = IesLights.try_emplace(Token.Identifier);
    if (Inserted)
    {
        const std::string Combined = Entry.Title + " " + Entry.Subtitle + " " + Entry.DomainTag;
        if (ContainsLower(Combined, "wall"))        It->second.ProfilePresetIndex = 1u;
        else if (ContainsLower(Combined, "batwing"))It->second.ProfilePresetIndex = 2u;
        else if (ContainsLower(Combined, "ece"))    { It->second.ProfilePresetIndex = 3u; It->second.FieldAngleDegrees = 58.0f; It->second.RangeMeters = 120.0f; }
        else if (ContainsLower(Combined, "sae"))    { It->second.ProfilePresetIndex = 4u; It->second.FieldAngleDegrees = 54.0f; It->second.RangeMeters = 110.0f; }
        else if (ContainsLower(Combined, "high"))   { It->second.ProfilePresetIndex = 5u; It->second.FieldAngleDegrees = 36.0f; It->second.RangeMeters = 180.0f; }
        else if (ContainsLower(Combined, "fog"))    { It->second.ProfilePresetIndex = 6u; It->second.FieldAngleDegrees = 82.0f; }
        else if (ContainsLower(Combined, "parking"))It->second.ProfilePresetIndex = 7u;
        else                                        It->second.ProfilePresetIndex = 0u;
    }
    return It->second;
}

AreaLightData& InspectorInstanceRegister::AccessAreaLight(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = AreaLights.try_emplace(Token.Identifier);
    if (Inserted && !Scene.AreaLights.empty())
    {
        const auto& First = Scene.AreaLights.front();
        It->second.WidthMeters      = std::clamp(First.HalfWidthMeters * 2.0f, 0.2f, 20.0f);
        It->second.HeightMeters     = std::clamp(First.HalfHeightMeters * 2.0f, 0.2f, 20.0f);
        It->second.TwoSidedRadiator = First.TwoSidedEmission;
    }
    return It->second;
}

TubeLightData& InspectorInstanceRegister::AccessTubeLight(OutlinerSlotToken Token)
{
    return TubeLights[Token.Identifier];
}

LedLightData& InspectorInstanceRegister::AccessLedLight(OutlinerSlotToken Token)
{
    return LedLights[Token.Identifier];
}

LedStripData& InspectorInstanceRegister::AccessLedStrip(OutlinerSlotToken Token)
{
    return LedStrips[Token.Identifier];
}

EnvironmentLightInstanceData& InspectorInstanceRegister::AccessEnvironmentLight(
    OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = EnvironmentLights.try_emplace(Token.Identifier);
    if (Inserted) It->second.SkyRadiance = Scene.SkyRadiance;
    return It->second;
}

PostProcessInstanceData& InspectorInstanceRegister::AccessPostProcess(OutlinerSlotToken Token)
{
    return PostProcesses[Token.Identifier];
}

AnalyticPrimitiveData& InspectorInstanceRegister::AccessAnalyticPrimitive(
    OutlinerSlotToken Token, const OutlinerRecordEntry& Entry, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = AnalyticPrimitives.try_emplace(Token.Identifier);
    if (Inserted)
    {
        SeedSurfaceFromScene(It->second.Surface, Scene);
        const std::string Combined = Entry.DomainTag + " " + Entry.Title + " " + Entry.Subtitle;
        if (ContainsLower(Combined, "box") || ContainsLower(Combined, "cube"))          It->second.FormIndex = 1u;
        else if (ContainsLower(Combined, "cylinder"))                                    It->second.FormIndex = 2u;
        else if (ContainsLower(Combined, "cone"))                                        It->second.FormIndex = 3u;
        else if (ContainsLower(Combined, "torus"))                                       It->second.FormIndex = 4u;
        else if (ContainsLower(Combined, "capsule"))                                     It->second.FormIndex = 5u;
        else if (ContainsLower(Combined, "plane") || ContainsLower(Combined, "ground")) It->second.FormIndex = 6u;
        else                                                                             It->second.FormIndex = 0u;
    }
    return It->second;
}

NativeGeometryData& InspectorInstanceRegister::AccessNativeGeometry(
    OutlinerSlotToken Token, const OutlinerRecordEntry& Entry, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = NativeGeometries.try_emplace(Token.Identifier);
    if (Inserted)
    {
        SeedSurfaceFromScene(It->second.Surface, Scene);
        if (!Entry.Title.empty())
        {
            It->second.PackageReference = "EngineContent/Geometry/" + Entry.Title + ".GeometrySpace";
        }
        if (!Scene.ClusterInstances.empty())
        {
            It->second.ClusterCount = std::max<uint32_t>(16u, static_cast<uint32_t>(Scene.ClusterInstances.size()) * 24u);
        }
    }
    return It->second;
}

ImportedGeometryData& InspectorInstanceRegister::AccessImportedGeometry(
    OutlinerSlotToken Token, const OutlinerRecordEntry& Entry, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = ImportedGeometries.try_emplace(Token.Identifier);
    if (Inserted)
    {
        SeedSurfaceFromScene(It->second.Surface, Scene);
        const auto WithExt = [&](std::string_view Ext) -> std::string
        {
            return ContainsLower(Entry.Title, Ext) ? ("Imports/" + Entry.Title) : ("Imports/" + Entry.Title + std::string(Ext));
        };
        const std::string Combined = Entry.DomainTag + " " + Entry.Title + " " + Entry.Subtitle;
        if (ContainsLower(Combined, "fbx"))                                          { It->second.ExchangeFormatIndex = 1u; It->second.SourcePath = WithExt(".fbx"); }
        else if (ContainsLower(Combined, "obj"))                                     { It->second.ExchangeFormatIndex = 2u; It->second.SourcePath = WithExt(".obj"); }
        else if (ContainsLower(Combined, "usd"))                                     { It->second.ExchangeFormatIndex = 3u; It->second.SourcePath = WithExt(".usdc"); }
        else if (ContainsLower(Combined, "step") || ContainsLower(Combined, ".stp")) { It->second.ExchangeFormatIndex = 4u; It->second.SourcePath = WithExt(".step"); It->second.SourceUnitIndex = 0u; }
        else if (ContainsLower(Combined, "stl"))                                     { It->second.ExchangeFormatIndex = 5u; It->second.SourcePath = WithExt(".stl"); It->second.SourceUnitIndex = 0u; }
        else                                                                         { It->second.ExchangeFormatIndex = 0u; It->second.SourcePath = WithExt(".gltf"); }
    }
    return It->second;
}

ProceduralGeometryData& InspectorInstanceRegister::AccessProceduralGeometry(
    OutlinerSlotToken Token, const OutlinerRecordEntry& Entry, const GeometricRaster::WorldSceneSpecification& Scene)
{
    auto [It, Inserted] = ProceduralGeometries.try_emplace(Token.Identifier);
    if (Inserted)
    {
        SeedSurfaceFromScene(It->second.Surface, Scene);
        const std::string Combined = Entry.DomainTag + " " + Entry.Title + " " + Entry.Subtitle;
        if (ContainsLower(Combined, "terrain") || ContainsLower(Combined, "height")) It->second.GeneratorIndex = 1u;
        else if (ContainsLower(Combined, "boolean") || ContainsLower(Combined, "csg")) It->second.GeneratorIndex = 2u;
        else if (ContainsLower(Combined, "spline") || ContainsLower(Combined, "loft")) It->second.GeneratorIndex = 3u;
        else if (ContainsLower(Combined, "fracture") || ContainsLower(Combined, "voronoi")) It->second.GeneratorIndex = 4u;
        else It->second.GeneratorIndex = 0u;
    }
    return It->second;
}

void InspectorInstanceRegister::CombineIntoScene(
    const OutlinerPanel&                      Outliner,
    GeometricRaster::WorldSceneSpecification& Scene,
    ViewportDisplaySettings*                  DisplaySettings) const
{
    uint32_t ActiveAtmosCount  = 0u;
    uint32_t ActiveSunCount    = 0u;
    uint32_t ActiveCloudCount  = 0u;
    uint32_t ActiveMoonCount   = 0u;
    uint32_t ActiveStarCount   = 0u;
    uint32_t ActiveWindCount   = 0u;
    uint32_t ActiveHeightFogCount = 0u;
    bool     FoundAnyHeightFog    = false;
    GeometricRaster::HeightFogConfiguration CombinedHeightFog {};

    bool     FoundAnyAtmo      = false;
    bool     FoundAnySun       = false;
    bool     FoundAnyCloud     = false;
    bool     FoundAnyMoon      = false;
    bool     FoundAnyStar      = false;
    bool     FoundAnyWind      = false;

    float    CombinedSunGain   = 0.0f;
    Mathematics::LinearRGB CombinedSunIrradiance { 0.0f, 0.0f, 0.0f };

    float    WindVelX          = 0.0f;
    float    WindVelY          = 0.0f;
    float    WindShearSum      = 0.0f;
    float    WindTurbMax       = 0.0f;
    bool     WindShearOn       = false;

    GeometricRaster::StarfieldConfiguration CombinedStars {};
    CombinedStars.StarfieldEnabled        = false;
    CombinedStars.LimitingMagnitude       = 0.0f;
    CombinedStars.BrightnessMultiplier    = 0.0f;
    CombinedStars.EnableBrightStarCatalog = false;
    CombinedStars.EnableGalacticBand      = false;
    CombinedStars.ZodiacalLightEnabled    = false;
    CombinedStars.ScintillationEnabled    = false;

    Scene.Atmosphere.LocalClouds.clear();
    Scene.Atmosphere.Fog.LocalFogs.clear();

    for (const auto& Entry : Outliner.QueryEntries())
    {
        const uint64_t Key = Entry.Token.Identifier;
        const bool Visible = Outliner.QueryEffectiveVisibility(Entry.Token);
        const std::string_view Tag = Entry.DomainTag;

        if (Tag == "env:atmosphere")
        {
            FoundAnyAtmo = true;
            if (Visible)
            {
                if (const auto It = Atmospheres.find(Key); It != Atmospheres.end())
                {
                    if (ActiveAtmosCount == 0u) Scene.Atmosphere.Medium = It->second.Medium;
                    else
                    {
                        Scene.Atmosphere.Medium.RayleighStrength += It->second.Medium.RayleighStrength * 0.5f;
                        Scene.Atmosphere.Medium.MieStrength      += It->second.Medium.MieStrength * 0.5f;
                        Scene.Atmosphere.Medium.OzoneStrength    += It->second.Medium.OzoneStrength * 0.5f;
                    }
                }
                Scene.Atmosphere.Medium.AtmosphereEnabled = true;
                ++ActiveAtmosCount;
            }
        }
        else if (Tag == "env:sun")
        {
            FoundAnySun = true;
            if (Visible)
            {
                const auto It = Suns.find(Key);
                const auto& SunConfiguration = (It != Suns.end()) ? It->second.Solar : Scene.Atmosphere.Solar;
                if (ActiveSunCount == 0u)
                {
                    Scene.Atmosphere.Solar = SunConfiguration;
                    Scene.Atmosphere.Solar.SolarEnabled = true;
                }
                const auto Ephem = GeometricRaster::CelestialEphemerisSolver::SolveSolarEphemeris(SunConfiguration);
                CombinedSunGain += SunConfiguration.SolarIntensityMultiplier;
                CombinedSunIrradiance.R += Ephem.EffectiveSolarTint.R * SunConfiguration.SolarIntensityMultiplier;
                CombinedSunIrradiance.G += Ephem.EffectiveSolarTint.G * SunConfiguration.SolarIntensityMultiplier;
                CombinedSunIrradiance.B += Ephem.EffectiveSolarTint.B * SunConfiguration.SolarIntensityMultiplier;
                ++ActiveSunCount;
            }
        }
        else if (Tag == "env:clouds")
        {
            FoundAnyCloud = true;
            if (Visible)
            {
                if (const auto It = Clouds.find(Key); It != Clouds.end())
                {
                    if (ActiveCloudCount == 0u) Scene.Atmosphere.Clouds = It->second.Clouds;
                    else
                    {
                        Scene.Atmosphere.Clouds.CloudFraction = std::clamp(Scene.Atmosphere.Clouds.CloudFraction + It->second.Clouds.CloudFraction * 0.5f, 0.0f, 1.0f);
                        Scene.Atmosphere.Clouds.ExtinctionMultiplier = std::max(Scene.Atmosphere.Clouds.ExtinctionMultiplier, It->second.Clouds.ExtinctionMultiplier);
                    }
                }
                Scene.Atmosphere.Clouds.CloudsEnabled = true;
                ++ActiveCloudCount;
            }
        }
        else if (Tag == "env:local-cloud")
        {
            if (Visible)
            {
                if (const auto It = LocalClouds.find(Key); It != LocalClouds.end())
                {
                    auto Vol = It->second.Volume;
                    Vol.LocalEnabled = true;
                    Scene.Atmosphere.LocalClouds.push_back(Vol);
                }
            }
        }
        else if (Tag == "env:moon")
        {
            FoundAnyMoon = true;
            if (Visible && ActiveMoonCount < 6u)
            {
                if (const auto It = Moons.find(Key); It != Moons.end())
                {
                    Scene.Atmosphere.Moons[ActiveMoonCount] = It->second.Moon;
                }
                Scene.Atmosphere.Moons[ActiveMoonCount].Visible = true;
                ++ActiveMoonCount;
            }
        }
        else if (Tag == "env:stars")
        {
            FoundAnyStar = true;
            if (Visible)
            {
                const auto It = Starfields.find(Key);
                const auto& StarConfiguration = (It != Starfields.end()) ? It->second.Stars : Scene.Atmosphere.Stars;
                if (ActiveStarCount == 0u) CombinedStars = StarConfiguration;
                else
                {
                    CombinedStars.LimitingMagnitude = std::max(CombinedStars.LimitingMagnitude, StarConfiguration.LimitingMagnitude);
                    CombinedStars.BrightnessMultiplier = std::min(6.0f, CombinedStars.BrightnessMultiplier + StarConfiguration.BrightnessMultiplier);
                    CombinedStars.EnableBrightStarCatalog = CombinedStars.EnableBrightStarCatalog || StarConfiguration.EnableBrightStarCatalog;
                    CombinedStars.EnableGalacticBand      = CombinedStars.EnableGalacticBand      || StarConfiguration.EnableGalacticBand;
                    CombinedStars.ZodiacalLightEnabled    = CombinedStars.ZodiacalLightEnabled    || StarConfiguration.ZodiacalLightEnabled;
                    CombinedStars.ScintillationEnabled    = CombinedStars.ScintillationEnabled    || StarConfiguration.ScintillationEnabled;
                }
                CombinedStars.StarfieldEnabled = true;
                ++ActiveStarCount;
            }
        }
        else if (Tag == "env:wind")
        {
            FoundAnyWind = true;
            if (Visible)
            {
                if (const auto It = Winds.find(Key); It != Winds.end())
                {
                    const auto& W = It->second.Wind;
                    const float Rad = W.BearingDegrees * (PiF / 180.0f);
                    WindVelX     += W.SpeedMetersPerSecond * std::sin(Rad);
                    WindVelY     += W.SpeedMetersPerSecond * std::cos(Rad);
                    WindShearSum += W.ShearPerKilometer;
                    WindTurbMax   = std::max(WindTurbMax, W.Turbulence);
                    WindShearOn   = WindShearOn || W.AirShearAdvection;
                }
                ++ActiveWindCount;
            }
        }
        else if (Tag == "env:height-fog")
        {
            FoundAnyHeightFog = true;
            if (Visible)
            {
                if (const auto It = HeightFogs.find(Key); It != HeightFogs.end())
                {
                    if (ActiveHeightFogCount == 0u)
                    {
                        CombinedHeightFog = It->second.HeightFog;
                    }
                    else
                    {
                        CombinedHeightFog.Density = std::clamp(CombinedHeightFog.Density + It->second.HeightFog.Density, 0.0f, 0.25f);
                        CombinedHeightFog.FalloffHeightMeters = std::max(CombinedHeightFog.FalloffHeightMeters, It->second.HeightFog.FalloffHeightMeters);
                        CombinedHeightFog.SunScatterIntensity = std::max(CombinedHeightFog.SunScatterIntensity, It->second.HeightFog.SunScatterIntensity);
                    }
                    CombinedHeightFog.Enabled = true;
                    ++ActiveHeightFogCount;
                }
            }
        }
        else if (Tag == "env:aerial-fog")
        {
            if (const auto It = AtmosphericFogs.find(Key); It != AtmosphericFogs.end())
            {
                Scene.Atmosphere.Fog.AerialFog = It->second.AerialFog;
                Scene.Atmosphere.Fog.AerialFog.Enabled = Visible;
            }
        }
        else if (Tag == "env:local-fog")
        {
            if (Visible)
            {
                if (const auto It = LocalFogs.find(Key); It != LocalFogs.end())
                {
                    auto F = It->second.LocalFog;
                    F.Enabled = true;
                    Scene.Atmosphere.Fog.LocalFogs.push_back(F);
                }
            }
        }
        else if (Tag == "env:god-rays")
        {
            if (const auto It = GodRays.find(Key); It != GodRays.end())
            {
                Scene.Atmosphere.Fog.GodRays = It->second.GodRays;
                Scene.Atmosphere.Fog.GodRays.Enabled = Visible;
            }
        }
        else if (Tag == "env:lens-flare")
        {
            if (const auto It = LensFlares.find(Key); It != LensFlares.end())
            {
                Scene.Atmosphere.LensFlare = It->second.Flare;
                Scene.Atmosphere.LensFlare.Enabled = Visible && It->second.Flare.Enabled;
            }
        }
        else if (Tag == "env:environment-light")
        {
            if (Visible)
            {
                if (const auto It = EnvironmentLights.find(Key); It != EnvironmentLights.end())
                {
                    const float EvGain = It->second.GainScale * std::pow(2.0f, It->second.ExposureEv);
                    Scene.SkyRadiance = {
                        It->second.SkyRadiance.R * EvGain,
                        It->second.SkyRadiance.G * EvGain,
                        It->second.SkyRadiance.B * EvGain,
                    };
                }
            }
        }
    }

    // Sync active multi-instance luminaires into WorldSceneSpecification (PointLights & Heitz LTC AreaLights)
    const bool AuthoredLuminairesPresent = !PointLights.empty() || !SpotLights.empty() || !IesLights.empty() ||
                                           !AreaLights.empty()  || !TubeLights.empty() || !LedLights.empty() || !LedStrips.empty();
    if (AuthoredLuminairesPresent)
    {
        std::vector<GeometricRaster::DirectPointLightSpec>   CombinedPointLights {};
        std::vector<GeometricRaster::PolygonalAreaQuadLight> CombinedAreaLights  {};
        for (const auto& E : Outliner.QueryEntries())
        {
            if (E.Category == ConstructSlotCategory::Folder || !Outliner.QueryEffectiveVisibility(E.Token)) continue;
            const uint64_t Key = E.Token.Identifier;
            const Mathematics::Axis3D PosWorld { E.PositionX, E.PositionY, E.PositionZ };
            const auto CombineTint = [](const Mathematics::LinearRGB& SourceRgb, float Kelvin) noexcept -> Mathematics::LinearRGB
            {
                const auto K = InspectorPrimitives::ComputeKelvinRgb(Kelvin);
                return { SourceRgb.R * K.R, SourceRgb.G * K.G, SourceRgb.B * K.B };
            };
            if (const auto It = PointLights.find(Key); It != PointLights.end())
            {
                const auto& P = It->second;
                GeometricRaster::DirectPointLightSpec Spec {};
                Spec.PositionWorld = PosWorld; Spec.RangeMeters = P.ReachMeters;
                Spec.Color = CombineTint(P.RadiantTint, P.KelvinTemperature);
                Spec.Intensity = P.IntensityCandela; Spec.RadiusMeters = P.SourceRadiusMeters;
                CombinedPointLights.push_back(Spec);
            }
            else if (const auto It = SpotLights.find(Key); It != SpotLights.end())
            {
                const auto& S = It->second;
                GeometricRaster::DirectPointLightSpec Spec {};
                Spec.PositionWorld = PosWorld; Spec.RangeMeters = S.ReachMeters;
                Spec.Color = CombineTint(S.RadiantTint, S.KelvinTemperature);
                Spec.Intensity = S.IntensityCandela; Spec.RadiusMeters = S.SourceRadiusMeters;
                CombinedPointLights.push_back(Spec);
            }
            else if (const auto It = IesLights.find(Key); It != IesLights.end())
            {
                const auto& I = It->second;
                GeometricRaster::DirectPointLightSpec Spec {};
                Spec.PositionWorld = PosWorld; Spec.RangeMeters = I.RangeMeters;
                Spec.Color = CombineTint(I.RadiantTint, I.KelvinTemperature);
                Spec.Intensity = (I.LuminousFlux * I.ProfileMultiplier) / (4.0f * PiF); Spec.RadiusMeters = 0.08f;
                CombinedPointLights.push_back(Spec);
            }
            else if (const auto It = LedLights.find(Key); It != LedLights.end())
            {
                const auto& L = It->second;
                GeometricRaster::DirectPointLightSpec Spec {};
                Spec.PositionWorld = PosWorld; Spec.RangeMeters = 18.0f;
                Spec.Color = CombineTint(L.RadiantTint, L.KelvinTemperature);
                Spec.Intensity = (L.DriverWatts * L.EfficacyLmPerW * L.Dimmer) / (2.0f * PiF);
                Spec.RadiusMeters = L.PackageDiameterMm * 0.0005f;
                CombinedPointLights.push_back(Spec);
            }
            else if (const auto It = AreaLights.find(Key); It != AreaLights.end())
            {
                const auto& A = It->second;
                const float AreaM2 = std::max(0.01f, A.WidthMeters * A.HeightMeters);
                const float RadScale = (A.LuminousFlux / AreaM2) * 0.004f;
                const auto  C = CombineTint(A.RadiantTint, A.KelvinTemperature);
                GeometricRaster::PolygonalAreaQuadLight Quad {};
                Quad.CenterWorld      = PosWorld;
                Quad.HalfWidthMeters  = A.WidthMeters * 0.5f;
                Quad.HalfHeightMeters = A.HeightMeters * 0.5f;
                Quad.EmittedRadiance  = { C.R * RadScale, C.G * RadScale, C.B * RadScale };
                Quad.TwoSidedEmission = A.TwoSidedRadiator;
                CombinedAreaLights.push_back(Quad);
            }
            else if (const auto It = TubeLights.find(Key); It != TubeLights.end())
            {
                const auto& T = It->second;
                const float AreaM2   = std::max(0.01f, T.LengthMeters * T.TubeRadiusMeters * 2.0f);
                const float RadScale = (T.LuminousFlux / AreaM2) * 0.003f;
                const auto  C = CombineTint(T.RadiantTint, T.KelvinTemperature);
                GeometricRaster::PolygonalAreaQuadLight Quad {};
                Quad.CenterWorld      = PosWorld;
                Quad.HalfWidthMeters  = T.LengthMeters * 0.5f;
                Quad.HalfHeightMeters = T.TubeRadiusMeters;
                Quad.EmittedRadiance  = { C.R * RadScale, C.G * RadScale, C.B * RadScale };
                Quad.TwoSidedEmission = true;
                CombinedAreaLights.push_back(Quad);
            }
            else if (const auto It = LedStrips.find(Key); It != LedStrips.end())
            {
                const auto& S = It->second;
                const float TotalLm  = S.LumensPerMetre * S.LengthMeters * S.Dimmer;
                const float AreaM2   = std::max(0.01f, S.LengthMeters * 0.02f);
                const float RadScale = (TotalLm / AreaM2) * 0.002f;
                const auto  C = CombineTint(S.RadiantTint, S.KelvinTemperature);
                GeometricRaster::PolygonalAreaQuadLight Quad {};
                Quad.CenterWorld      = PosWorld;
                Quad.HalfWidthMeters  = S.LengthMeters * 0.5f;
                Quad.HalfHeightMeters = 0.015f;
                Quad.EmittedRadiance  = { C.R * RadScale, C.G * RadScale, C.B * RadScale };
                Quad.TwoSidedEmission = false;
                CombinedAreaLights.push_back(Quad);
            }
        }
        if (!CombinedPointLights.empty()) Scene.PointLights = std::move(CombinedPointLights);
        if (!CombinedAreaLights.empty())  Scene.AreaLights  = std::move(CombinedAreaLights);
    }

    if (FoundAnyAtmo)
    {
        Scene.Atmosphere.Medium.AtmosphereEnabled = (ActiveAtmosCount > 0u);
        if (DisplaySettings) DisplaySettings->AtmosphereSkyEnabled = (ActiveAtmosCount > 0u);
    }
    if (FoundAnySun)
    {
        Scene.Atmosphere.Solar.SolarEnabled = (ActiveSunCount > 0u);
        if (ActiveSunCount > 0u)
        {
            Scene.Atmosphere.Solar.SolarIntensityMultiplier = CombinedSunGain;
            Scene.Sun.SolarIrradiance = CombinedSunIrradiance;
        }
        else
        {
            Scene.Sun.SolarIrradiance = { 0.0f, 0.0f, 0.0f };
        }
    }
    if (FoundAnyCloud)
    {
        Scene.Atmosphere.Clouds.CloudsEnabled = (ActiveCloudCount > 0u);
        if (DisplaySettings) DisplaySettings->VolumetricCloudsEnabled = (ActiveCloudCount > 0u);
    }
    if (FoundAnyMoon)
    {
        for (uint32_t Slot = ActiveMoonCount; Slot < 6u; ++Slot)
        {
            Scene.Atmosphere.Moons[Slot].Visible = false;
        }
    }
    if (FoundAnyStar)
    {
        Scene.Atmosphere.Stars = CombinedStars;
        if (DisplaySettings) DisplaySettings->CelestialStarsEnabled = (ActiveStarCount > 0u);
    }
    if (FoundAnyHeightFog)
    {
        if (ActiveHeightFogCount > 0u)
        {
            Scene.Atmosphere.Fog.HeightFog = CombinedHeightFog;
        }
        else
        {
            Scene.Atmosphere.Fog.HeightFog.Enabled = false;
        }
    }

    if (FoundAnyWind)
    {
        Scene.Atmosphere.Wind.Enabled = (ActiveWindCount > 0u);
        if (ActiveWindCount > 0u)
        {
            const float CombinedSpeed = std::sqrt(WindVelX * WindVelX + WindVelY * WindVelY);
            float BearingDeg = std::atan2(WindVelX, WindVelY) * (180.0f / PiF);
            if (BearingDeg < 0.0f) BearingDeg += 360.0f;
            Scene.Atmosphere.Wind.SpeedMetersPerSecond = CombinedSpeed;
            Scene.Atmosphere.Wind.BearingDegrees       = BearingDeg;
            Scene.Atmosphere.Wind.ShearPerKilometer    = WindShearSum;
            Scene.Atmosphere.Wind.Turbulence           = WindTurbMax;
            Scene.Atmosphere.Wind.AirShearAdvection    = WindShearOn;
        }
        else
        {
            Scene.Atmosphere.Wind.SpeedMetersPerSecond = 0.0f;
        }
    }

    // Sync active Camera Settings entry & active Camera optics into live ViewportDisplaySettings -> GPU uniforms
    if (DisplaySettings != nullptr)
    {
        for (const auto& Entry : Outliner.QueryEntries())
        {
            if (!Outliner.QueryEffectiveVisibility(Entry.Token)) continue;
            if (Entry.DomainTag == "env:camera-settings" ||
                Entry.DomainTag == "env:render-engine" ||
                Entry.DomainTag == "camera:settings" ||
                Entry.DomainTag == "render:settings")
            {
                if (const auto It = CameraSettingsEntries.find(Entry.Token.Identifier); It != CameraSettingsEntries.end())
                {
                    SyncCameraSettingsToDisplay(It->second, DisplaySettings);
                    break;
                }
            }
        }
        for (const auto& Entry : Outliner.QueryEntries())
        {
            if (!Outliner.QueryEffectiveVisibility(Entry.Token)) continue;
            if (Entry.DomainTag == "camera:perspective" ||
                Entry.DomainTag == "camera:orthographic" ||
                Entry.DomainTag == "camera:cinematic" ||
                Entry.DomainTag == "camera:primary" ||
                Entry.DomainTag == "scene:camera")
            {
                if (const auto It = Cameras.find(Entry.Token.Identifier); It != Cameras.end())
                {
                    DisplaySettings->CameraFocalLengthMm  = std::clamp(It->second.FocalLengthMm, 12.0f, 200.0f);
                    DisplaySettings->CameraSensorWidthMm  = std::clamp(It->second.SensorWidthMm, 12.0f, 70.0f);
                    DisplaySettings->CameraApertureFStop  = std::clamp(It->second.ApertureFStop, 1.2f, 22.0f);
                    DisplaySettings->CameraFocusDistanceM = std::clamp(It->second.FocusDistanceMeters, 0.3f, 100.0f);
                    DisplaySettings->CameraRollDegrees    = std::clamp(It->second.CameraRollDegrees, -45.0f, 45.0f);
                    break;
                }
            }
        }
    }
}

void InspectorInstanceRegister::SyncCameraSettingsToDisplay(
    const CameraSettingsData& Data,
    ViewportDisplaySettings*  DisplaySettings) noexcept
{
    if (DisplaySettings == nullptr) return;

    switch (Data.RendererIndex)
    {
        case 0u: DisplaySettings->RenderEngineIndex = 1u; break; // WorldRenderEngine::OpenPbrDirect
        case 1u: DisplaySettings->RenderEngineIndex = 2u; break; // WorldRenderEngine::RealTimeGiGtxLowEnd
        case 2u: DisplaySettings->RenderEngineIndex = 3u + std::min<uint32_t>(Data.RayTracerIndex, 2u); break; // 3=BVH4, 4=PT Realtime, 5=PT Final
        default: DisplaySettings->RenderEngineIndex = 0u; break; // WorldRenderEngine::MatCap
    }

    DisplaySettings->QualityTier             = std::min<uint32_t>(Data.QualityTierIndex, 4u);
    DisplaySettings->ShadowAlgorithmIndex    = (Data.RendererIndex == 2u) ? std::min<uint32_t>(Data.RayShadowIndex, 4u) : std::min<uint32_t>(Data.PbrShadowIndex, 4u);
    DisplaySettings->SmrtShadowRays          = static_cast<uint32_t>(std::clamp(std::lround(Data.SmrtShadowRays), 1l, 8l));
    DisplaySettings->SmrtMarchSteps          = static_cast<uint32_t>(std::clamp(std::lround(Data.SmrtMarchSteps), 4l, 16l));
    DisplaySettings->SdfShadowPenumbraIndex  = std::min<uint32_t>(Data.SdfPenumbraIndex, 5u);
    DisplaySettings->GiCandidateIndex        = std::min<uint32_t>(Data.GiCandidateIndex, 1u);
    DisplaySettings->SdfConeSteps            = static_cast<uint32_t>(std::clamp(std::lround(Data.SdfConeSteps), 3l, 12l));
    DisplaySettings->SdfGiIntensity          = std::clamp(Data.SdfGiIntensity, 0.0f, 2.5f);
    DisplaySettings->SdfAoRadiusM            = std::clamp(Data.SdfAoRadiusM, 0.35f, 3.5f);
    DisplaySettings->SdfAoContrast           = std::clamp(Data.SdfAoContrast, 0.40f, 3.5f);
    DisplaySettings->MatcapChannelIndex      = std::min<uint32_t>(Data.MatcapChannelIndex, 3u);
    DisplaySettings->MatcapRimGain           = std::clamp(Data.MatcapRimGain, 0.0f, 1.0f);
    DisplaySettings->ReflectionsEnabled      = (Data.RendererIndex == 2u) ? Data.RayReflectionsEnabled : Data.PbrReflectionsEnabled;
    DisplaySettings->ReflectionSourceIndex   = std::min<uint32_t>(Data.ReflectionSourceIndex, 1u);
    DisplaySettings->RaytracingEnabled       = Data.RaytracingEnabled;
    DisplaySettings->XeGtaoEnabled           = (Data.RendererIndex == 1u) ? false : Data.XeGtaoEnabled;
    DisplaySettings->XeGtaoStrength          = std::clamp(Data.XeGtaoStrength, 0.0f, 2.0f);
    DisplaySettings->XeGtaoSliceCount        = static_cast<uint32_t>(std::clamp(std::lround(Data.XeGtaoSliceCount), 2l, 8l));
    DisplaySettings->XeGtaoStepsPerSide      = static_cast<uint32_t>(std::clamp(std::lround(Data.XeGtaoStepsPerSide), 2l, 8l));
    DisplaySettings->XeGtaoSearchRadiusM     = std::clamp(Data.XeGtaoSearchRadiusM, 0.25f, 3.0f);
    DisplaySettings->PathTraceBatchSpp       = static_cast<uint32_t>(std::clamp(std::lround(Data.PathTraceBatchSpp), 1l, 8l));
    DisplaySettings->PathTraceTargetSpp      = static_cast<uint32_t>(std::clamp(std::lround(Data.PathTraceTargetSpp), 0l, 2048l));
    DisplaySettings->PathTraceMaxBounces     = static_cast<uint32_t>(std::clamp(std::lround(Data.PathTraceMaxBounces), 1l, 8l));
    DisplaySettings->RenderScalePercent      = std::clamp(Data.RenderScalePercent, 50.0f, 200.0f);
    DisplaySettings->TraceResolutionIndex    = std::min<uint32_t>(Data.TraceResolutionIndex, 2u);
    DisplaySettings->SkyFillGain             = std::clamp((Data.RendererIndex == 1u) ? Data.SdfFillGain : Data.PbrFillGain, 0.0f, 3.0f);
    DisplaySettings->ExposureGain            = std::clamp(Data.ExposureGain, 0.1f, 4.0f);
    DisplaySettings->AntiAliasingEnabled     = Data.SpatialAntiAliasing;
    DisplaySettings->ClusterLodEnabled       = Data.ClusterLodEnabled;
    DisplaySettings->ClusterLodPixelError    = std::clamp(Data.ClusterLodPixelError, 0.25f, 8.0f);
    DisplaySettings->ClusterForcedLodLevel   = std::clamp(Data.ClusterForcedLodLevel, -1, 3);
    DisplaySettings->ClusterWireframeOverlay = Data.ClusterWireframeOverlay;
    DisplaySettings->DebugChannelIndex       = std::min<uint32_t>(Data.DebugChannelIndex, 22u);
}

std::string InspectorInstanceRegister::ResolveEnclosingBreadcrumb(
    const OutlinerPanel& Outliner,
    OutlinerSlotToken    EntryToken)
{
    const OutlinerRecordEntry* Current = Outliner.FindEntry(EntryToken);
    if (Current == nullptr) return "Inspector / Scene";

    std::vector<std::string> Chain;
    OutlinerSlotToken Walk = Current->EnclosingToken;
    for (uint32_t Guard = 0u; Guard < 12u && Walk.Valid(); ++Guard)
    {
        const OutlinerRecordEntry* Enclosing = Outliner.FindEntry(Walk);
        if (Enclosing == nullptr) break;
        Chain.push_back(Enclosing->Title);
        Walk = Enclosing->EnclosingToken;
    }

    if (Chain.empty()) return "Inspector / Scene";
    std::string Breadcrumb = "Inspector";
    for (auto It = Chain.rbegin(); It != Chain.rend(); ++It)
    {
        Breadcrumb += " / ";
        Breadcrumb += *It;
    }
    return Breadcrumb;
}

} // namespace Frontier
