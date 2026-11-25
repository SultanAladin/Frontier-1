/*============================================================================================================================================
                                                          FORMATSPECIFICATIONS.H
============================================================================================================================================*/
// 🧩 Strongly-typed C++20 document structures for Frontier configuration, WorldSpace, CameraOptics, Atmosphere, FractureNetwork & pillars

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Container/SuffixRegistry.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 CONFIGURATION FAMILY
//------------------------------------------------------------------------------------------------------------------------

struct EngineSettingsDocument
{
    uint32_t    SchemaVersion               = 1u;                             // [ver]   - Document schema version
    std::string ActiveProjectIndexPath      { "Content/Project/Frontier.ProjectIndex" }; // [-] - Active project index path
    std::string DefaultStartupWorldPath     { "Content/Worlds/VehicleClearSkyDefault.WorldSpace" }; // [-] - Startup WorldSpace path
    std::string DefaultGraphicsSettingsPath { "Configuration/GraphicsDefault.GraphicsSettings" };   // [-] - Default graphics settings path
    std::string CompiledRepositoryDirectory { "Build/CompiledRepository" };   // [-]     - Persistent compiled disk repository path
    uint64_t    RepositoryQuotaMegabytes    = 2048u;                          // [MiB]   - Maximum compiled disk repository quota
    uint32_t    DocumentRevisionLimitMiB    = 215u;                           // [MiB]   - Per-document parametric revision budget
    uint32_t    GlobalRevisionLimitMiB      = 750u;                           // [MiB]   - Global parametric revision budget
};

struct RuntimeConfigurationDocument
{
    uint32_t    SchemaVersion          = 1u;                                  // [ver]   - Document schema version
    std::string ApplicationTitle       { "Frontier Engine" };                 // [-]     - Window & process title
    std::string StartupWorldPath       { "Content/Worlds/VehicleClearSkyDefault.WorldSpace" }; // [-] - Boot WorldSpace override
    uint32_t    WindowWidthPixels      = 1920u;                               // [px]    - Initial viewport width
    uint32_t    WindowHeightPixels     = 1080u;                               // [px]    - Initial viewport height
    uint32_t    TargetFramesPerSecond  = 60u;                                 // [Hz]    - Target frame pacing cadence
    bool        EnableVulkanValidation = true;                                // [-]     - Enable Khronos validation layers
    bool        EnableVerticalSync     = true;                                // [-]     - Synchronize presentation to display refresh
};

struct PlatformSettingsDocument
{
    uint32_t    SchemaVersion           = 1u;                                 // [ver]   - Document schema version
    std::string TargetPlatformName      { "Linux-x86_64" };                   // [-]     - Operating platform & CPU architecture
    std::string PreferredGraphicsApi    { "Vulkan-1.3" };                     // [-]     - Graphics backend identifier
    std::string PreferredPresentMode    { "Mailbox" };                        // [-]     - Swapchain presentation mode
    uint32_t    WorkerThreadCap         = 0u;                                 // [count] - 0 means auto-detect hardware threads
    bool        PreferDedicatedTransfer = true;                               // [-]     - Use dedicated DMA transfer queue when available
};

struct ProjectIndexDocument
{
    uint32_t                 SchemaVersion       = 1u;                        // [ver]   - Document schema version
    std::string              ProjectName         { "Frontier" };              // [-]     - Project display name
    std::string              ProjectIdentifier   { "0193b5c0-7a1e-7000-8000-000000000100" }; // [-] - Persistent project UUID
    std::string              StartupWorldPath    { "Content/Worlds/VehicleClearSkyDefault.WorldSpace" }; // [-] - Default startup world
    std::vector<std::string> ContentSearchRoots  { "Content", "EngineContent" }; // [-] - Ordered content root directories
    std::vector<std::string> RegisteredWorlds    { "Content/Worlds/VehicleClearSkyDefault.WorldSpace" }; // [-] - Indexed WorldSpace paths
};

#if defined(FRONTIER_PROFILE_EDITOR) && FRONTIER_PROFILE_EDITOR
struct WorkspaceLayoutDocument
{
    uint32_t                 SchemaVersion       = 1u;                        // [ver]   - Document schema version
    std::string              ActiveWorkspaceName { "ViewportAuthoring" };     // [-]     - Selected editor workspace preset
    std::string              DockingTopologyHash {};                          // [hash]  - Deterministic layout hash
    std::vector<std::string> VisiblePanelTitles  { "Viewport", "Outliner", "Inspector", "Atmosphere" }; // [-] - Open dock panels
};
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                       CAMERA OPTICS, ATMOSPHERE & FRACTURE NETWORK
//------------------------------------------------------------------------------------------------------------------------

struct CameraOpticsDocument
{
    uint32_t              SchemaVersion              = 1u;                    // [ver] - Document schema version
    std::string           OpticsTitle                { "CinePrime50mm" };     // [-]   - Lens assembly display title
    float                 FocalLengthMillimeters     = 50.0f;                 // [mm]  - Physical focal length
    float                 ApertureFStop              = 2.8f;                  // [f/#] - Iris f-number
    float                 ShutterSpeedSeconds        = 1.0f / 125.0f;         // [s]   - Exposure integration duration
    float                 ShutterAngleDegrees        = 180.0f;                // [deg] - Cinema rotary shutter angle
    float                 SensorIsoSensitivity       = 100.0f;                // [ISO] - Sensor exposure sensitivity
    float                 SensorWidthMillimeters     = 36.0f;                 // [mm]  - Full-frame sensor width
    float                 SensorHeightMillimeters    = 24.0f;                 // [mm]  - Full-frame sensor height
    float                 FocusDistanceMeters        = 6.5f;                  // [m]   - Focal plane distance
    float                 FocusBreathingRatio        = 0.015f;                // [-]   - Focal length shift across focus travel
    uint32_t              ApertureBladeCount         = 9u;                    // [count] - Iris diaphragm blade count
    float                 BladeCurvature             = 0.35f;                 // [-]   - Blade rounding factor [0..1]
    float                 BokehCatsEyeFactor         = 0.18f;                 // [-]   - Mechanical vignetting bokeh clipping
    float                 BokehSphericalAberration   = 0.12f;                 // [-]   - Edge-bright vs soft-disc bokeh bias
    std::array<float, 3u> RadialDistortionK          = { -0.012f, 0.002f, 0.0f }; // [-] - Brown-Conrady radial K1, K2, K3
    std::array<float, 2u> TangentialDistortionP      = { 0.0f, 0.0f };        // [-]   - Brown-Conrady tangential P1, P2
    float                 ChromaticDispersionRms     = 0.0008f;               // [-]   - Lateral axial colour dispersion
    float                 OpticalVignettingStrength  = 0.22f;                 // [-]   - Natural cos^4 + barrel falloff
};

struct AtmosphereDocument
{
    uint32_t              SchemaVersion              = 1u;                    // [ver]   - Document schema version
    std::string           AtmosphereTitle            { "ClearSkyDaylight" };  // [-]     - Atmosphere profile title
    bool                  SolarDiskEnabled           = true;                  // [-]     - Independent solar disc radiance toggle
    bool                  CelestialStarfieldEnabled  = false;                 // [-]     - Independent night starfield toggle
    float                 SolarElevationDegrees      = 42.0f;                 // [deg]   - Sun elevation above horizon
    float                 SolarAzimuthDegrees        = 135.0f;                // [deg]   - Sun compass azimuth
    float                 SolarAngularDiameterDeg    = 0.533f;                // [deg]   - Physical apparent solar diameter
    float                 SolarIlluminanceLux        = 105000.0f;             // [lx]    - Direct solar illuminance
    float                 AtmosphericTurbidity       = 2.1f;                  // [-]     - Aerosol haze turbidity
    float                 RayleighScaleHeightMeters  = 8000.0f;               // [m]     - Exponential Rayleigh scale height
    float                 MieScaleHeightMeters       = 1200.0f;               // [m]     - Exponential Mie aerosol scale height
    float                 MieAnisotropyG             = 0.78f;                 // [-]     - Henyey-Greenstein forward scattering
    float                 OzoneAbsorptionWeight      = 1.0f;                  // [-]     - Chappuis band twilight ozone absorption
    float                 GroundAlbedoReflectance    = 0.22f;                 // [-]     - Planet surface diffuse bounce albedo
    float                 CloudCoverageFraction      = 0.08f;                 // [-]     - Volumetric cloud layer fraction [0..1]
    std::array<float, 3u> ZenithTintLinearRgb        = { 0.16f, 0.36f, 0.82f }; // [-]   - Reference sky zenith radiance cue
};

struct FractureCellRecord
{
    uint32_t              CellIdentifier            = 0u;                     // [idx]   - Contiguous fracture shard identifier
    int32_t               ContainmentIndex          = -1;                     // [idx]   - Enclosing composite shard (-1 = root)
    std::array<float, 3u> CentroidMeters            = { 0.0f, 0.0f, 0.0f };   // [m]     - Local shard centroid
    float                 MassKilograms             = 0.25f;                  // [kg]    - Shard inertial mass
    float                 RuptureThresholdNewtons   = 1850.0f;                // [N]     - Stress impulse required to fracture bonds
};

struct FractureBondRecord
{
    uint32_t CellIndexA          = 0u;                                        // [idx]   - First connected shard index
    uint32_t CellIndexB          = 1u;                                        // [idx]   - Second connected shard index
    float    CohesiveAreaSqMeters = 0.015f;                                   // [m^2]   - Shared interface area
    float    TensileStrengthMpa   = 45.0f;                                    // [MPa]   - Maximum tensile bond strength
};

struct FractureNetworkDocument
{
    uint32_t                        SchemaVersion   = 1u;                     // [ver]   - Document schema version
    std::string                     NetworkTitle    { "TemperedGlassShatter" }; // [-]   - Fracture specification title
    std::string                     SurfaceMaterial { "TemperedAutomotiveGlass" }; // [-] - Assigned physical material label
    std::vector<FractureCellRecord> Cells           {};                       // [-]     - Voronoi fracture shard cells
    std::vector<FractureBondRecord> CohesiveBonds   {};                       // [-]     - Adjacency stress bonds between cells
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    WORLDSPACE
//------------------------------------------------------------------------------------------------------------------------

struct WorldPlacementEntry
{
    std::string           PlacementIdentifier  {};                            // [-]     - Persistent unique placement ID
    std::string           DisplayTitle         {};                            // [-]     - Human-readable outliner label
    int32_t               ContainmentIndex     = -1;                          // [idx]   - Enclosing placement index (-1 = world root)
    std::string           InstanceRelativePath {};                            // [-]     - Path to linked .Instance or pillar file
    std::array<float, 3u> PositionMeters       = { 0.0f, 0.0f, 0.0f };        // [m]     - World position (Z-up right-handed)
    std::array<float, 3u> RotationEulerDegrees = { 0.0f, 0.0f, 0.0f };        // [deg]   - Euler orientation (Pitch, Roll, Yaw)
    std::array<float, 3u> ScaleFactors         = { 1.0f, 1.0f, 1.0f };        // [-]     - Non-uniform scale factors
    bool                  VisibleInViewport    = true;                        // [-]     - Visibility switch
};

struct WorldSpaceDocument
{
    uint32_t                         SchemaVersion       = 1u;                // [ver]   - Document schema version
    std::string                      StableIdentifier    { "0193b5c0-7a1e-7000-8000-000000000200" }; // [-] - Persistent world UUID
    std::string                      WorldTitle          { "VehicleClearSkyDefault" }; // [-] - World display title
    CameraOpticsDocument             StartupCameraOptics {};                  // [-]     - Embedded startup camera lens optics
    std::array<float, 3u>            CameraPositionMeters{ 7.1f, -6.2f, 2.35f }; // [m]  - Startup camera position
    std::array<float, 3u>            CameraTargetMeters  { 0.0f, 0.0f, 0.95f };  // [m]  - Startup camera focus point
    AtmosphereDocument               Atmosphere          {};                  // [-]     - Embedded physical atmosphere specification
    std::vector<WorldPlacementEntry> Placements          {};                  // [-]     - Ordered flat placements (non-kinship)
};

} // namespace Frontier::Serialization
