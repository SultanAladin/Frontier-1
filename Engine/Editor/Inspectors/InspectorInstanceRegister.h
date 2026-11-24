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
    bool     TripodMounted       = true;  // [-]    - true = On tripod (fixed), false = Floating (crane/drone/rig)
    float    MountHeightMeters   = 1.45f; // [m]    - Camera mount height above ground
    float    CameraRollDegrees   = 0.0f;  // [deg]  - Dutch roll angle (Floating mount)
    bool     AimAtShowcase       = true;  // [-]    - Auto-aim pitch & yaw at scene focal subject
    bool     MotionBlurEnabled   = false;
    float    ShutterAngleDegrees = 180.0f;
    GeometricRaster::LensFlareConfiguration LensFlare {};
};

// Camera Settings (F77/F78): linked end-to-end to ViewportDisplaySettings -> WorldRenderConfiguration -> WorldViewportRadiometricCompute.slang
struct CameraSettingsData
{
    uint32_t RendererIndex           = 0u;     // [0..3] - 0=PBR (OpenPbrDirect), 1=SDF (RealTimeGiGtxLowEnd), 2=Ray (BVH4/PathTracer), 3=MatCap
    uint32_t QualityTierIndex        = 3u;     // [0..4] - Low, Medium, Standard, High, Ultra (ViewportDisplaySettings::QualityTier)

    // PBR raster settings (OpenPbrDirect)
    uint32_t PbrShadowIndex          = 0u;     // [0..4] - 0=Contact PCSS, 1=Plane PCSS, 2=SMRT, 3=PCF, 4=BVH RayQuery
    float    SmrtShadowRays          = 4.0f;   // [cnt]  - SMRT shadow rays [1..8] (.slang EvaluateSolarShadow Alg 2)
    float    SmrtMarchSteps          = 10.0f;  // [cnt]  - SMRT march steps per ray [4..16] (.slang EvaluateSolarShadow Alg 2)
    bool     PbrReflectionsEnabled   = true;   // [-]    - Specular reflections toggle
    uint32_t ReflectionSourceIndex   = 0u;     // [0..1] - 0=BVH4 Raytrace, 1=Sky Environment
    float    PbrFillGain             = 1.00f;  // [x]    - Sky dome fill gain [0..3] (.slang EvaluateSkyDome)

    // Real XeGTAO screen-space ambient occlusion (.slang EvaluateGroundTruthAmbientOcclusion)
    bool     XeGtaoEnabled           = false;  // [-]    - Enable XeGTAO screen-space AO
    float    XeGtaoStrength          = 1.00f;  // [x]    - XeGTAO power exponent [0..2]
    float    XeGtaoSliceCount        = 4.0f;   // [cnt]  - XeGTAO directional slices [2..8]
    float    XeGtaoStepsPerSide      = 4.0f;   // [cnt]  - XeGTAO horizon march steps per side [2..8]
    float    XeGtaoSearchRadiusM     = 0.85f;  // [m]    - XeGTAO world-space search radius [0.25..3.0]

    // SDF tracing settings (RealTimeGiGtxLowEnd)
    uint32_t SdfPenumbraIndex        = 3u;     // [0..5] - 0=PointSdf, 1=AnalyticCone, 2=Clipmap, 3=ConeGi, 4=HybridSobol, 5=McRef96
    uint32_t GiCandidateIndex        = 1u;     // [0..1] - 0=StokesContour, 1=SurfelRadiosity
    float    SdfConeSteps            = 6.0f;   // [cnt]  - GDF voxel cone GI march steps [3..12] (.slang EvaluateGdfVoxelConeGi)
    float    SdfGiIntensity          = 1.00f;  // [x]    - GDF voxel cone GI intensity [0..2.5]
    float    SdfAoRadiusM            = 1.55f;  // [m]    - SDF UDF ambient occlusion reach [0.35..3.5]
    float    SdfAoContrast           = 1.50f;  // [x]    - SDF UDF ambient occlusion contrast [0.40..3.5]
    float    SdfFillGain             = 1.00f;  // [x]    - Sky dome ambient fill [0..3]

    // Ray traced & Path Tracer settings (RealTimeGiHighEnd=3, PathTracerGameRealtime=4, PathTracerSceneFinalRender=5)
    uint32_t RayTracerIndex          = 1u;     // [0..2] - 0=BVH4 High-End GI, 1=Path Tracer Realtime, 2=Path Tracer Final
    bool     RaytracingEnabled       = true;   // [-]    - Hardware/BVH4 raytracing toggle
    float    PathTraceBatchSpp       = 1.0f;   // [spp]  - Samples accumulated per frame [1..8]
    float    PathTraceTargetSpp      = 256.0f; // [spp]  - Convergence target [0..1024] (0 = continuous)
    float    PathTraceMaxBounces     = 3.0f;   // [cnt]  - Path tracer bounce depth [1..8] (.slang EvaluateProgressiveViewportPath)
    uint32_t RayShadowIndex          = 4u;     // [0..4] - Shadow algorithm for BVH4 High-End GI
    bool     RayReflectionsEnabled   = true;   // [-]    - Traced specular reflections toggle

    // MatCap shading settings
    uint32_t MatcapShadowIndex       = 1u;     // [0..2] - None, Contact, Soft
    float    MatcapLightAngleDeg     = 38.0f;  // [deg]  - Key light angle
    float    MatcapRimGain           = 0.35f;  // [0-1]  - Grazing silhouette rim boost
    uint32_t MatcapChannelIndex      = 0u;     // [0..3] - Clay, Ceramic, Metallic, Normal

    // Output & Cluster-LOD settings (shared across all 4 renderers -> ViewportDisplaySettings)
    float    RenderScalePercent      = 100.0f; // [%]    - Internal resolution scale [50..200]
    uint32_t TraceResolutionIndex    = 0u;     // [0..2] - 0=Full, 1=1/2 Trace, 2=1/4 Trace
    float    ExposureGain            = 1.00f;  // [x]    - ACES display exposure gain [0.1..4.0] (.slang ApplyAcesFilmicGpu)
    bool     SpatialAntiAliasing     = true;   // [-]    - Spatial bilateral shadow/AA filter
    bool     ClusterLodEnabled       = true;   // [-]    - View-dependent Cluster-LOD BVH cut
    float    ClusterLodPixelError    = 1.25f;  // [px]   - Screen-space LOD error budget [0.25..8.0]
    int32_t  ClusterForcedLodLevel   = -1;     // [idx]  - -1=Auto, 0..3=Forced LOD tier
    bool     ClusterWireframeOverlay = false;  // [-]    - Analytical triangle edge wireframe overlay
    uint32_t DebugChannelIndex       = 0u;     // [0..22]- Shader debug inspection channel
};

struct LensFlareInstanceData
{
    GeometricRaster::LensFlareConfiguration Flare {};
};

struct MotionBlurInstanceData
{
    bool     Enabled              = true;
    uint32_t StyleIndex           = 0u;     // [idx] - 0=CameraVelocity, 1=DirectionalShutter, 2=RadialZoom
    float    ShutterAngleDegrees  = 180.0f; // [deg] - Rotary shutter open angle [1..360]
    float    VelocityScale        = 1.0f;   // [x]   - Blur velocity intensity [0..2.5]
    float    MaxSampleRadiusPx    = 24.0f;  // [px]  - Maximum blur radius [2..48]
    uint32_t SampleCount          = 12u;    // [cnt] - Blur gather samples [4..16]
    float    DirectionalAngleDeg  = 0.0f;   // [deg] - Directional shutter angle [-180..180]
    bool     ObjectMotionEnabled  = true;   // [-]   - Object motion blur toggle
    bool     CameraMotionEnabled  = true;   // [-]   - Camera motion blur toggle
    uint32_t FrameRateIndex       = 0u;     // [idx] - 0=24fps, 1=30fps, 2=60fps
};

struct ExposureInstanceData
{
    bool  Enabled          = true;
    float ExposureEv       = 0.0f;  // [EV]  - Photographic stop compensation [-4..+4]
    float BiasMultiplier   = 1.0f;  // [x]   - Direct linear gain multiplier [0.25..4.0]
    float SaturationGain   = 1.0f;  // [x]   - Colour saturation [0..2.0]
    float ContrastGain     = 1.0f;  // [x]   - Tonal contrast [0.5..2.0]
    float VignetteStrength = 0.0f;  // [0-1] - Optical vignette falloff [0..1.0]
};

// Luminaire 1: Omnidirectional Point Light
struct PointLightData
{
    float                  IntensityCandela   = 22.0f;    // [cd]  - Authored luminous intensity
    float                  KelvinTemperature  = 5600.0f;  // [K]   - Blackbody colour temperature
    Mathematics::LinearRGB RadiantTint       { 1.00f, 0.96f, 0.90f };
    float                  ReachMeters        = 26.0f;    // [m]   - Smooth attenuation reach
    float                  DecayExponent      = 2.00f;    // [-]   - Distance falloff exponent (2.0 = inverse square)
    float                  SourceRadiusMeters = 0.08f;    // [m]   - Emitter sphere radius
    bool                   ShowGlow           = true;     // [-]   - Show emitter glow helper in viewport
};

// Luminaire 2: Conical Spot Light
struct SpotLightData
{
    float                  IntensityCandela   = 90.0f;    // [cd]  - Authored axial luminous intensity
    float                  KelvinTemperature  = 5400.0f;  // [K]   - Blackbody colour temperature
    Mathematics::LinearRGB RadiantTint       { 1.00f, 0.97f, 0.92f };
    float                  FullConeDegrees    = 26.0f;    // [deg] - Outer full beam cone angle
    float                  Penumbra           = 0.40f;    // [0-1] - Soft outer edge transition ratio
    float                  ReachMeters        = 35.0f;    // [m]   - Smooth attenuation cutoff distance
    float                  SourceRadiusMeters = 0.14f;    // [m]   - Lens aperture radius
    bool                   ShowCone           = true;     // [-]   - Show beam cone helper in viewport
};

// Luminaire 3: IES Photometric Distribution Light (LM-63 / PhotometricPointRadiator)
struct IesLightData
{
    float                  LuminousFlux       = 1600.0f;  // [lm]  - Profile source luminous flux
    float                  ProfileMultiplier  = 1.00f;    // [x]   - Output scale multiplier
    float                  KelvinTemperature  = 4000.0f;  // [K]   - Blackbody colour temperature
    Mathematics::LinearRGB RadiantTint       { 1.00f, 0.98f, 0.94f };
    uint32_t               ProfilePresetIndex = 0u;       // [0..7]- Downlight, Wall wash, Batwing, ECE Low, SAE Low, High Beam, Fog, Parking
    float                  FieldAngleDegrees  = 60.0f;    // [deg] - Photometric field beam angle
    float                  CutoffPitchDegrees = 0.0f;     // [deg] - Cut-off pitch tilt [-5..+5 deg]
    float                  RangeMeters        = 30.0f;    // [m]   - Photometric range cutoff
    bool                   ShowDistribution   = true;     // [-]   - Show polar distribution wire in viewport
};

// Luminaire 4: Polygonal Area / Softbox Light (Heitz LTC PolygonalAreaQuadLight)
struct AreaLightData
{
    float                  LuminousFlux       = 2400.0f;  // [lm]  - Authored luminous flux
    float                  KelvinTemperature  = 5600.0f;  // [K]   - Blackbody colour temperature
    Mathematics::LinearRGB RadiantTint       { 1.00f, 0.98f, 0.95f };
    uint32_t               ApertureFormIndex  = 0u;       // [idx] - 0=Rectangle, 1=Disk
    float                  WidthMeters        = 2.00f;    // [m]   - Aperture horizontal width
    float                  HeightMeters       = 1.00f;    // [m]   - Aperture vertical height
    float                  SpreadDegrees      = 120.0f;   // [deg] - Beam spread angle
    bool                   TwoSidedRadiator   = false;    // [-]   - Emit from both front & back faces
    bool                   ShowFixture        = true;     // [-]   - Show glowing aperture face in viewport
};

// Luminaire 5: Linear Tube Light
struct TubeLightData
{
    float                  LuminousFlux       = 1800.0f;  // [lm]  - Total linear luminous flux
    float                  KelvinTemperature  = 4600.0f;  // [K]   - Blackbody colour temperature
    Mathematics::LinearRGB RadiantTint       { 0.96f, 0.99f, 1.00f };
    float                  LengthMeters       = 1.50f;    // [m]   - Linear emitter length
    float                  TubeRadiusMeters   = 0.04f;    // [m]   - Cylindrical glass radius
    float                  ReachMeters        = 24.0f;    // [m]   - Smooth attenuation cutoff distance
    bool                   ShowFixture        = true;     // [-]   - Show glowing tube body in viewport
};

// Luminaire 6: Solid-State LED Emitter (COB / Star Board)
struct LedLightData
{
    float                  DriverWatts        = 10.0f;    // [W]   - Electrical driver input power
    float                  EfficacyLmPerW     = 110.0f;   // [lm/W]- Luminous efficacy target
    float                  Dimmer             = 1.00f;    // [0-1] - Dimmer output level
    float                  KelvinTemperature  = 4000.0f;  // [K]   - Phosphor colour temperature
    Mathematics::LinearRGB RadiantTint       { 1.00f, 1.00f, 1.00f };
    float                  PackageDiameterMm  = 40.0f;    // [mm]  - Emitter package diameter
    float                  OpticAngleDeg   = 120.0f;   // [deg] - Primary lens optic angle
    bool                   ShowFixture        = true;     // [-]   - Show emitter fixture in viewport
};

// Luminaire 7: Linear LED Strip / Ribbon
struct LedStripData
{
    float                  LumensPerMetre     = 1000.0f;  // [lm/m]- Luminous flux per metre
    float                  WattsPerMetre      = 14.4f;    // [W/m] - Electrical load per metre
    float                  Dimmer             = 1.00f;    // [0-1] - Dimmer output level
    float                  KelvinTemperature  = 3000.0f;  // [K]   - Warm/cool phosphor temperature
    Mathematics::LinearRGB RadiantTint       { 1.00f, 0.98f, 0.94f };
    float                  LengthMeters       = 2.40f;    // [m]   - Continuous ribbon run length
    float                  DiodesPerMetre   = 60.0f;    // [/m]  - SMD LED density per metre
    float                  SupplyVolts        = 24.0f;    // [V]   - DC bus supply voltage
    bool                   OpalDiffuser       = false;    // [-]   - Opal polycarbonate diffuser channel
    bool                   ShowFixture        = true;     // [-]   - Show lit channel in viewport
};

// Luminaire 8: Environment Sky / HDRI Light
struct EnvironmentLightInstanceData
{
    uint32_t               SourcePresetIndex  = 0u;       // [0..4]- Sky atmosphere, HDRI Studio, HDRI Overcast, HDRI Sunset, Uniform colour
    Mathematics::LinearRGB SkyRadiance        { 0.42f, 0.56f, 0.78f };
    float                  RotationDegrees    = 0.0f;     // [deg] - Environment azimuth rotation
    float                  GainScale          = 1.0f;     // [x]   - Intensity multiplier
    float                  ExposureEv         = 0.0f;     // [EV]  - Exposure offset [-4..+4]
    float                  Saturation         = 1.0f;     // [x]   - Chromatic saturation [0..2]
    float                  GroundBounce       = 0.30f;    // [0-1] - Lower hemisphere bounce reflectance
    bool                   IndirectGi         = true;
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
    CameraSettingsData&           AccessCameraSettings(OutlinerSlotToken Token);
    LensFlareInstanceData&        AccessLensFlare(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    MotionBlurInstanceData&       AccessMotionBlur(OutlinerSlotToken Token);
    ExposureInstanceData&         AccessExposure(OutlinerSlotToken Token);
    [[nodiscard]] const LensFlareInstanceData*  FindLensFlare(OutlinerSlotToken Token) const noexcept;
    [[nodiscard]] const MotionBlurInstanceData* FindMotionBlur(OutlinerSlotToken Token) const noexcept;
    [[nodiscard]] const ExposureInstanceData*   FindExposure(OutlinerSlotToken Token) const noexcept;
    [[nodiscard]] const PostProcessInstanceData* FindPostProcess(OutlinerSlotToken Token) const noexcept;
    PointLightData&               AccessPointLight(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    SpotLightData&                AccessSpotLight(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    IesLightData&                 AccessIesLight(OutlinerSlotToken Token, const OutlinerRecordEntry& Entry);
    AreaLightData&                AccessAreaLight(OutlinerSlotToken Token, const GeometricRaster::WorldSceneSpecification& Scene);
    TubeLightData&                AccessTubeLight(OutlinerSlotToken Token);
    LedLightData&               AccessLedLight(OutlinerSlotToken Token);
    LedStripData&                 AccessLedStrip(OutlinerSlotToken Token);
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

    // Push active CameraSettingsData live into ViewportDisplaySettings -> WorldRenderConfiguration -> .slang GPU uniforms
    static void SyncCameraSettingsToDisplay(
        const CameraSettingsData& Data,
        ViewportDisplaySettings*  DisplaySettings) noexcept;

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
    std::unordered_map<uint64_t, CameraSettingsData>           CameraSettingsEntries {};
    std::unordered_map<uint64_t, LensFlareInstanceData>        LensFlares        {};
    std::unordered_map<uint64_t, MotionBlurInstanceData>       MotionBlurs       {};
    std::unordered_map<uint64_t, ExposureInstanceData>         Exposures         {};
    std::unordered_map<uint64_t, PointLightData>               PointLights       {};
    std::unordered_map<uint64_t, SpotLightData>                SpotLights        {};
    std::unordered_map<uint64_t, IesLightData>                 IesLights         {};
    std::unordered_map<uint64_t, AreaLightData>                AreaLights        {};
    std::unordered_map<uint64_t, TubeLightData>                TubeLights        {};
    std::unordered_map<uint64_t, LedLightData>               LedLights       {};
    std::unordered_map<uint64_t, LedStripData>                 LedStrips         {};
    std::unordered_map<uint64_t, EnvironmentLightInstanceData> EnvironmentLights {};
    std::unordered_map<uint64_t, PostProcessInstanceData>      PostProcesses     {};
    std::unordered_map<uint64_t, AnalyticPrimitiveData>        AnalyticPrimitives{};
    std::unordered_map<uint64_t, NativeGeometryData>           NativeGeometries  {};
    std::unordered_map<uint64_t, ImportedGeometryData>         ImportedGeometries{};
    std::unordered_map<uint64_t, ProceduralGeometryData>       ProceduralGeometries{};
};

} // namespace Frontier
