/*============================================================================================================================================
                                                       INSPECTORINSTANCEREGISTER.H
============================================================================================================================================*/
// 🧩 Per-entry multi-instance parameter register keyed by OutlinerSlotToken + scene combination pass (zero hardcoded singletons or folders)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
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

// Category 1: Analytical Primitive (Sphere, Box, Cylinder, Cone, Torus, Capsule, Plane)
struct AnalyticPrimitiveData
{
    GeometrySurfaceBinding Surface          {};
    uint32_t               FormIndex        = 0u;     // [idx] - 0=Sphere, 1=Box, 2=Cylinder, 3=Cone, 4=Torus, 5=Capsule, 6=Plane
    float                  RadiusMeters     = 0.50f;  // [m]   - Primary radius / half-extent
    float                  HeightMeters     = 1.00f;  // [m]   - Vertical span (Box/Cylinder/Cone/Capsule)
    float                  MinorRadiusMeters= 0.18f;  // [m]   - Tube radius (Torus) or bevel cap
    uint32_t               RadialSegments   = 32u;    // [cnt] - Analytic tessellation rings
    float                  BevelMillimeters = 2.0f;   // [mm]  - Edge fillet bevel
    bool                   Watertight       = true;   // [-]   - Closed watertight solid sewing
};

// Category 2: Native Engine Geometry (.GeometrySpace / ClusterPyramid / LocalSDF / OpenPBR)
struct NativeGeometryData
{
    GeometrySurfaceBinding Surface            {};
    std::string            PackageReference   { "EngineContent/Geometry/ShowcaseSpecimen.GeometrySpace" };
    uint32_t               TriangleCount      = 12480u;
    uint32_t               ClusterCount       = 98u;
    uint32_t               LodTierCount       = 3u;
    float                  LodPixelError      = 1.25f;  // [px]  - Per-instance cluster LOD cut budget
    float                  CreaseAngleDegrees = 49.0f;  // [deg] - Smooth normal crease threshold
    bool                   LocalSdfEnabled    = true;   // [-]   - Participates in local SDF occlusion/collision
    bool                   DoubleSided        = false;  // [-]   - Disable backface culling
};

// Category 3: Imported Geometry (STEP, STL, glTF, FBX, OBJ, USD exchange pipeline)
struct ImportedGeometryData
{
    GeometrySurfaceBinding Surface             {};
    std::string            SourcePath          { "Imports/ChassisAssembly.gltf" };
    uint32_t               ExchangeFormatIndex = 0u;     // [idx] - 0=glTF, 1=FBX, 2=OBJ, 3=USD, 4=STEP, 5=STL
    uint32_t               SourceUnitIndex     = 2u;     // [idx] - 0=mm, 1=cm, 2=m, 3=in
    uint32_t               UpAxisIndex         = 0u;     // [idx] - 0=Z-Up, 1=Y-Up
    float                  ImportScaleFactor   = 1.00f;  // [x]   - Multiplier applied on import
    float                  CreaseAngleDegrees  = 45.0f;  // [deg] - Imported normal split angle
    uint32_t               TriangleCount       = 48200u;
    uint32_t               UvChannelCount      = 2u;
    bool                   BuildClusterPyramid = true;
    bool                   BuildLocalSdf       = true;
    bool                   RecomputeNormals    = false;
    uint32_t               ImportRevision      = 1u;
};

// Category 4: Procedural Geometry (Parametric Sweep, Terrain Field, Boolean Solid, Spline Surface, Voronoi Partition)
struct ProceduralGeometryData
{
    GeometrySurfaceBinding Surface            {};
    uint32_t               GeneratorIndex     = 0u;     // [idx] - 0=Parametric Sweep, 1=Terrain Field, 2=Boolean Solid, 3=Spline Surface, 4=Voronoi Partition
    uint32_t               RandomSeed         = 42u;    // [id]  - Deterministic procedural seed
    uint32_t               SubdivisionLevel   = 4u;     // [lvl] - Generator tessellation level [1..8]
    float                  ChordToleranceMm   = 0.50f;  // [mm]  - Parametric curve/surface chord tolerance
    float                  FeatureScaleMeters = 2.40f;  // [m]   - Primary spatial wavelength / profile width
    float                  AmplitudeMeters    = 0.85f;  // [m]   - Extrusion depth / elevation relief
    bool                   AutoRebuild        = true;   // [-]   - Regenerate automatically on parameter change
    uint32_t               BuildRevision      = 1u;     // [rev] - Incremented on every procedural rebuild
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
    AnalyticPrimitiveData&        AccessAnalyticPrimitive(OutlinerSlotToken Token, const OutlinerRecordEntry& Entry, const GeometricRaster::WorldSceneSpecification& Scene);
    NativeGeometryData&           AccessNativeGeometry(OutlinerSlotToken Token, const OutlinerRecordEntry& Entry, const GeometricRaster::WorldSceneSpecification& Scene);
    ImportedGeometryData&         AccessImportedGeometry(OutlinerSlotToken Token, const OutlinerRecordEntry& Entry, const GeometricRaster::WorldSceneSpecification& Scene);
    ProceduralGeometryData&       AccessProceduralGeometry(OutlinerSlotToken Token, const OutlinerRecordEntry& Entry, const GeometricRaster::WorldSceneSpecification& Scene);

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
    std::unordered_map<uint64_t, AnalyticPrimitiveData>        AnalyticPrimitives{};
    std::unordered_map<uint64_t, NativeGeometryData>           NativeGeometries  {};
    std::unordered_map<uint64_t, ImportedGeometryData>         ImportedGeometries{};
    std::unordered_map<uint64_t, ProceduralGeometryData>       ProceduralGeometries{};
};

} // namespace Frontier
