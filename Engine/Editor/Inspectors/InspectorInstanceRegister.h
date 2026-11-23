/*============================================================================================================================================
                                                       INSPECTORINSTANCEREGISTER.H
============================================================================================================================================*/
// 🧩 Per-entry multi-instance parameter register keyed by OutlinerSlotToken + scene combination pass (zero hardcoded singletons or folders)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Frontier
{

struct AtmosphereInstanceData
{
    GeometricRaster::AtmosphericMediumConfiguration Medium          {};
    float                                           SunAtLimbDeg    = 6.0f;
    bool                                            LineAtCivilOnly = true;
};

struct SolarInstanceData
{
    GeometricRaster::SolarCelestialConfiguration Solar       {};
    uint32_t                                     DesignIndex = 0u;    // [0..2] - Sun now A / B / C visual selector
    bool                                         RealTime    = false;
};

struct CloudInstanceData
{
    GeometricRaster::CloudDeckConfiguration Clouds {};
};

struct LocalCloudInstanceData
{
    GeometricRaster::BoundedCloudVolume Volume {};
};

struct LunarInstanceData
{
    GeometricRaster::LunarBodyConfiguration Moon {};
};

struct StarfieldInstanceData
{
    GeometricRaster::StarfieldConfiguration Stars {};
};

struct WindInstanceData
{
    GeometricRaster::AtmosphericWindParameters Wind {};
};

struct HeightFogInstanceData
{
    GeometricRaster::HeightFogConfiguration HeightFog {};
};

struct AtmosphericFogInstanceData
{
    GeometricRaster::AerialFogConfiguration AerialFog {};
};

struct LocalFogInstanceData
{
    GeometricRaster::LocalFogVolume LocalFog {};
};

struct GodRaysInstanceData
{
    GeometricRaster::GodRayConfiguration GodRays {};
};

struct PrecipitationInstanceData
{
    bool     Enabled          = true;
    uint32_t ModeIndex        = 0u;    // [0..2] - Rain, Snow, Hail
    float    RateMultiplier   = 0.45f; // [0..2] - Fall rate density
    float    DropSizeMm       = 1.8f;  // [mm]   - Hydrometeor diameter
    float    WindSlantScale   = 0.65f; // [0..2] - Advection deflection
};

struct RainbowInstanceData
{
    bool  Enabled             = true;
    float PrimaryIntensity    = 0.72f; // [0..2] - Primary 42 deg bow gain
    float SecondaryIntensity  = 0.28f; // [0..1] - Secondary 51 deg bow gain
    float SupernumeraryWidth  = 0.35f; // [0..1] - Drop dispersion softness
    float DropletRadiusUm     = 500.0f;// [um]   - Mean water droplet radius
};

struct CameraInstanceData
{
    float    FocalLengthMm       = 35.0f;
    float    SensorWidthMm       = 36.0f;
    uint32_t SensorGateIndex     = 0u;    // [0..3] - Full 36mm, Super 35, Med 44mm, APS-C
    float    ApertureFStop       = 2.8f;
    float    FocusDistanceMeters = 6.0f;
    uint32_t RigPresetIndex      = 0u;    // [0..2] - Cinema, Rig, Compact
    bool     TripodMounted       = true;
    bool     MotionBlurEnabled   = false;
    float    ShutterAngleDegrees = 180.0f;
    GeometricRaster::LensFlareConfiguration LensFlare {};
};

struct LensFlareInstanceData
{
    GeometricRaster::LensFlareConfiguration Flare {};
};

struct MotionBlurInstanceData
{
    bool  Enabled             = true;
    float ShutterAngleDegrees = 180.0f;
    float VelocityScale       = 1.0f;
    float MaxSampleRadiusPx   = 24.0f;
};

struct LightInstanceData
{
    uint32_t               LuminaireFormIndex = 0u;   // [0..4] - Point, Spot, Area, Rect, Tube
    float                  LuminousPower      = 1200.0f;
    float                  KelvinTemperature = 5600.0f;
    Mathematics::LinearRGB LightTint         { 1.00f, 0.96f, 0.90f };
    float                  ConeDegrees       = 45.0f;
    float                  SourceRadiusMeters = 0.25f;
};

struct EnvironmentLightInstanceData
{
    Mathematics::LinearRGB SkyRadiance { 0.42f, 0.56f, 0.78f };
    float                  GainScale   = 1.0f;
    bool                   IndirectGi  = true;
};

struct PostProcessInstanceData
{
    bool  Enabled          = true;
    float ExposureEv       = 0.0f;
    float ContrastGain     = 1.0f;
    float VignetteStrength = 0.18f;
    float SaturationGain   = 1.0f;
    float ChromaticShift   = 0.0f;
};

struct GeometrySpecimenData
{
    Mathematics::LinearRGB AlbedoTint  { 0.78f, 0.76f, 0.72f };
    float                  Roughness   = 0.36f;
    float                  Metallic    = 0.10f;
    float                  SelfGlow    = 0.0f;
};

class InspectorInstanceRegister
{
public:
    InspectorInstanceRegister() noexcept = default;

    void Clear() noexcept;
    void EraseEntry(OutlinerSlotToken Token) noexcept;

    // Access or lazily initialize per-entry instance data (seeding from Scene on first access)
    AtmosphereInstanceData&       AccessAtmosphere(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    SolarInstanceData&            AccessSolar(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    CloudInstanceData&            AccessCloud(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    LocalCloudInstanceData&       AccessLocalCloud(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    LunarInstanceData&            AccessLunar(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene, uint32_t SeedSlot = 0u);
    StarfieldInstanceData&        AccessStarfield(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    WindInstanceData&             AccessWind(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    HeightFogInstanceData&        AccessHeightFog(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    AtmosphericFogInstanceData&   AccessAtmosphericFog(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    LocalFogInstanceData&         AccessLocalFog(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    GodRaysInstanceData&          AccessGodRays(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    PrecipitationInstanceData&    AccessPrecipitation(OutlinerSlotToken Token);
    RainbowInstanceData&          AccessRainbow(OutlinerSlotToken Token);
    CameraInstanceData&           AccessCamera(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    LensFlareInstanceData&        AccessLensFlare(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    MotionBlurInstanceData&       AccessMotionBlur(OutlinerSlotToken Token);
    LightInstanceData&            AccessLight(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    EnvironmentLightInstanceData& AccessEnvironmentLight(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    PostProcessInstanceData&      AccessPostProcess(OutlinerSlotToken Token);
    GeometrySpecimenData&         AccessGeometrySpecimen(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);

    // Combines ALL visible Outliner instances across ALL folders into WorldSceneSpecification & ViewportDisplaySettings
    void CombineIntoScene(
        const OutlinerPanel&                      Outliner,
        GeometricRaster::WorldSceneSpecification& Scene,
        ViewportDisplaySettings*                  DisplaySettings = nullptr) const;

    // Builds an uppercase breadcrumb dynamically from the enclosing folder hierarchy in Outliner (never hardcoded)
    [[nodiscard]] static std::string ResolveEnclosingBreadcrumb(
        const OutlinerPanel& Outliner,
        OutlinerSlotToken    EntryToken);

private:
    std::unordered_map<uint64_t, AtmosphereInstanceData>       Atmospheres       {};
    std::unordered_map<uint64_t, SolarInstanceData>            Suns              {};
    std::unordered_map<uint64_t, CloudInstanceData>            Clouds            {};
    std::unordered_map<uint64_t, LocalCloudInstanceData>       LocalClouds       {};
    std::unordered_map<uint64_t, LunarInstanceData>            Moons             {};
    std::unordered_map<uint64_t, StarfieldInstanceData>        Starfields        {};
    std::unordered_map<uint64_t, WindInstanceData>             Winds             {};
    std::unordered_map<uint64_t, HeightFogInstanceData>        HeightFogs        {};
    std::unordered_map<uint64_t, AtmosphericFogInstanceData>   AtmosphericFogs   {};
    std::unordered_map<uint64_t, LocalFogInstanceData>         LocalFogs         {};
    std::unordered_map<uint64_t, GodRaysInstanceData>          GodRays           {};
    std::unordered_map<uint64_t, PrecipitationInstanceData>    Precipitations    {};
    std::unordered_map<uint64_t, RainbowInstanceData>          Rainbows          {};
    std::unordered_map<uint64_t, CameraInstanceData>           Cameras           {};
    std::unordered_map<uint64_t, LensFlareInstanceData>        LensFlares        {};
    std::unordered_map<uint64_t, MotionBlurInstanceData>       MotionBlurs       {};
    std::unordered_map<uint64_t, LightInstanceData>            Lights            {};
    std::unordered_map<uint64_t, EnvironmentLightInstanceData> EnvironmentLights {};
    std::unordered_map<uint64_t, PostProcessInstanceData>      PostProcesses     {};
    std::unordered_map<uint64_t, GeometrySpecimenData>         Specimens         {};
};

} // namespace Frontier
