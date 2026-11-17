/*============================================================================================================================================
                                                           FLUIDDESCRIPTORS.H
============================================================================================================================================*/
// 🧩 Authoritative +Z-Up descriptors for Eulerian Fire/Smoke, Continuous World Drift Grid, ForceFields, Particles & GI Coupling

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::SimulationSpace::FluidSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class FluidColorPalette : uint32_t
{
    PlanckianBlackbody    = 0u, // [-] - Physical Planckian flame (black -> crimson -> orange -> amber -> white)
    CinemaNapalm          = 1u, // [-] - Rich saturated hydrocarbon fireball
    ThermobaricWhiteHot   = 2u, // [-] - High-energy white-hot ordnance core
    WildfireCopperEmerald = 3u, // [-] - Copper/boron green-emerald wildfire
    CherenkovBluePlasma   = 4u, // [-] - High-voltage blue-violet plasma core
    ArcaneVoidMagenta     = 5u, // [-] - Stylized violet/magenta corruption flame
};

enum class FluidObstacleType : uint32_t
{
    None               = 0u,
    Sphere             = 1u,
    VerticalCylinder   = 2u, // Aligned along +Z
    HorizontalCylinder = 3u, // Aligned along +X
    DeflectorSlab      = 4u,
    TyreRing           = 5u, // Torus lying in XY or tilted
};

enum class FluidRenderChannel : uint32_t
{
    LitVolume          = 0u,
    VoxelDda           = 1u,
    SmokeDensity       = 2u,
    Temperature        = 3u,
    Fuel               = 4u,
    Velocity           = 5u,
    Vorticity          = 6u,
    PressureIrradiance = 7u,
    CrossSection       = 8u,
};

enum class FluidAtlasMinimapField : uint32_t
{
    SmokeTemperature   = 0u,
    Velocity           = 1u,
    Vorticity          = 2u,
    PressureIrradiance = 3u,
};

enum class FluidInteractionTool : uint8_t
{
    Orbit         = 0u,
    PyroBrush     = 1u,
    DetonateBurst = 2u,
    MoveCollider  = 3u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FluidDomainSettings
{
    uint32_t           GridResolution            = 64u;   // [vox]  - Per-axis Hero grid resolution [16..128]
    uint32_t           PressureIterations        = 22u;   // [iter] - Jacobi pressure solver iterations [4..64]
    float              TimeScale                 = 1.0f;  // [x]    - Simulation clock multiplier
    bool               Paused                    = false; // [-]    - Simulation pause state
    bool               MacCormackAdvection       = true;  // [-]    - 2nd-order MacCormack advection with extrema clamp
    bool               EnclosedBox               = false; // [-]    - Closed reflective walls vs open outflow
    float              VoxelQuantization         = 0.0f;  // [0..1] - Retro quantized voxel stepping blend
    float              BoundsWidthMeters         = 1.85f; // [m]    - Base X/Y domain width
    float              BoundsHeightMeters        = 2.10f; // [m]    - Base +Z domain height
    bool               DynamicBounds             = true;  // [-]    - Dynamic blast expansion enabled
    float              DynamicBoundsMax          = 1.55f; // [x]    - Base expansion multiplier (adaptive headroom can exceed for explosions)
    bool               AdaptiveCeilingEnabled    = false; // [-]    - Uncaps vertical explosion ceiling so rising mushroom plumes don't flatten
    float              MaxExplosionCeilingMeters = 28.0f; // [m]    - Hard safety cap for adaptive vertical headroom
    Mathematics::Axis3D OriginMeters             { 0.0f, 0.0f, 0.0f }; // [m] - Base floor anchor (+Z Up)

    // Continuous World-Space Sliding Smoke Grid (prevents horizontal wind-drift smoke clipping at box sides)
    bool               WorldGridEnabled          = true;  // [-]    - Enable sliding/toroidal world-space smoke drift grid
    uint32_t           WorldGridResolution       = 48u;   // [vox]  - Per-axis world drift grid resolution [16..64]
    float              WorldGridSpanMeters       = 24.0f; // [m]    - Horizontal & vertical span of sliding world smoke grid
};

struct FluidEmitterSettings
{
    bool  Enabled           = true;  // [-]    - Continuous plume emission toggle
    float Rate              = 1.0f;  // [x]    - Emission intensity multiplier
    float RadiusMeters      = 0.15f; // [m]    - Source nozzle radius
    float HeightMeters      = 0.08f; // [m]    - Source Z height above domain floor
    float UpwardVelocityMps = 4.2f;  // [m/s]  - +Z jet velocity
    float Swirl             = 1.65f; // [rad/s]- Azimuthal XY swirl around +Z
    float Temperature       = 4.2f;  // [K*]   - Normalized flame temperature injection
    float Fuel              = 3.4f;  // [-]    - Unburned hydrocarbon fuel injection
    float Smoke             = 1.15f; // [-]    - Direct soot/smoke injection
};

struct FluidBlastSpecification
{
    Mathematics::Axis3D CenterWorld          { 0.0f, 0.0f, 0.25f }; // [m]   - Detonation world point (+Z Up)
    float               RadiusMeters         = 0.14f;               // [m]   - Core fireball radius
    float               StrengthMps          = 12.5f;               // [m/s] - Radial shockwave impulse
    float               Temperature          = 6.2f;                // [K*]  - Initial blast temperature
    float               Fuel                 = 5.2f;                // [-]   - Initial unburned fireball fuel
    float               Smoke                = 2.8f;                // [-]   - Initial blast soot
    float               Lobes                = 6.0f;                // [-]   - Angular Rayleigh-Taylor lobe frequency
    float               Seed                 = 1.0f;                // [-]   - Deterministic lobe seed
    Mathematics::Axis3D DirectionalVelocity  { 0.0f, 0.0f, 0.0f };  // [m/s] - Optional directional blast bias
    bool                TriggerForceImpulse  = true;                // [-]   - Also injects transient RadialBurst into ForceFields
};

struct FluidCombustionSettings
{
    float                 VorticityConfinement = 8.8f;  // [-]     - Small-scale vortex re-injection
    float                 Buoyancy             = 6.0f;  // [m/s^2] - Thermal +Z lift per unit temperature
    float                 SmokeWeight          = 0.85f; // [m/s^2] - Cool soot downward settling
    float                 TurbulenceStrength   = 4.4f;  // [m/s^2] - Curl-noise sub-grid stirring
    float                 TurbulenceScale      = 3.8f;  // [1/m]   - Curl-noise spatial frequency
    float                 BurnRate             = 1.85f; // [1/s]   - Arrhenius fuel consumption rate
    float                 BurnHeat             = 2.75f; // [-]     - Heat released per unit fuel burned
    float                 SootGeneration       = 1.35f; // [-]     - Smoke produced per unit fuel burned
    float                 CombustionExpansion  = 3.8f;  // [-]     - Thermal gas expansion divergence source
    float                 CoolingRate          = 1.20f; // [1/s]   - Radiative + convective cooling
    float                 SmokeDissipation     = 0.16f; // [1/s]   - Exponential smoke decay
    float                 VelocityDamping      = 0.07f; // [1/s]   - Linear drag damping
    Mathematics::Scalar2D WindVelocityXY       { 0.18f, 0.04f }; // [m/s] - Baseline ambient wind (when no ForceFields override)
    bool                  CoupleForceFields    = true;  // [-]     - Sample SimulationSpace::ForceFields into fluid velocity
};

struct FluidObstacleSettings
{
    FluidObstacleType   Type                = FluidObstacleType::None;
    Mathematics::Axis3D CenterMeters        { 0.0f, 0.0f, 0.45f }; // [m] - World position (+Z Up)
    float               RadiusMeters        = 0.16f;               // [m] - Collider radius
    bool                AutoMove            = false;               // [-] - Oscillating collider animation
    float               MoveSpeed           = 1.25f;               // [rad/s]
    bool                VoxelizeSceneGiSdf  = true;                // [-] - Sample GI SignedDistanceVolume / WideBoundingTree as solid walls
};

struct FluidShadingSettings
{
    FluidRenderChannel     RenderChannel      = FluidRenderChannel::LitVolume;
    FluidColorPalette      ColorPalette       = FluidColorPalette::PlanckianBlackbody;
    uint32_t               RaymarchSteps      = 72u;
    uint32_t               ShadowSteps        = 6u;
    float                  RenderScale        = 1.0f;
    float                  FireIntensity      = 4.6f;
    float                  BloomIntensity     = 0.48f;
    float                  GodRaysIntensity   = 0.35f;
    float                  ShockwaveStrength  = 0.65f;
    float                  TemperatureScale   = 1.0f;
    float                  InternalScattering = 2.4f;
    float                  DensityExtinction  = 18.5f;
    float                  SmokeAlbedo        = 0.24f;
    float                  ShadowDensity      = 11.5f;
    float                  PhaseAnisotropy    = 0.38f;
    float                  SunElevationDegrees = 35.0f;
    float                  SunAzimuthDegrees   = 225.0f;
    float                  SunIntensity       = 2.6f;
    float                  AmbientIntensity   = 0.42f;
    float                  Exposure           = 1.15f;
    uint32_t               SliceAxis          = 2u;    // 0=X, 1=Y, 2=Z
    float                  SlicePosition      = 0.5f;  // [0..1]
};

struct FluidParticleCouplingSettings
{
    bool     ShowEmbers         = true;  // [-]    - Enable Fluid -> ParticleSpace ember/ash advection
    uint32_t EmberCount         = 360u;  // [cnt]  - Active ember/ash particles
    float    EmberSize          = 0.9f;  // [x]    - Particle scale multiplier
    float    EmberIntensity     = 1.05f; // [x]    - Blackbody ember glow intensity
    float    EmberLifetime      = 1.15f; // [s]    - Mean ember lifetime
    float    EmberAshiness      = 0.32f; // [0..1] - Dark cooled ash blend
    bool     ShrapnelEnabled    = true;  // [-]    - Enable ParticleSpace -> Fluid ballistic fuel/smoke streamers
    float    AdvectionCoupling  = 1.0f;  // [x]    - Fluid velocity drag weight on particles
    float    BuoyancyCoupling   = 0.75f; // [x]    - Thermal updraft lift weight on particles
};

struct FluidGiCouplingSettings
{
    bool     CollideWithGiScene     = true;  // [-]   - Collide fluid against WorldSceneSpecification::SceneSdf & SceneTree
    bool     CastGiShadows          = true;  // [-]   - Smoke casts volumetric shadows onto GI Sun/Area/DDGI rays
    bool     EmitGiLights           = true;  // [-]   - Extract hot fire voxels into dynamic GI point/area lights
    bool     InjectDdgiRadiance     = true;  // [-]   - Sample fluid emission/extinction along DDGI & PathTrace rays
    uint32_t MaxEmissiveProxyLights = 4u;    // [cnt] - Max reduced dynamic fire lights per domain [0..8]
    float    EmissiveLightIntensity = 1.45f; // [x]   - Fire GI illumination multiplier
    float    GiShadowStrength       = 1.0f;  // [x]   - Volumetric smoke shadow density multiplier on GI surfaces
};

struct FluidEmissiveLightProxy
{
    Mathematics::Axis3D    WorldPosition   {};
    float                  RadiusMeters    = 0.8f;
    Mathematics::LinearRGB ColorIrradiance {};
};

struct FluidSceneCameraState
{
    float               ThetaRadians   = -0.95f;
    float               PhiRadians     = 0.92f;
    float               DistanceMeters = 4.1f;
    Mathematics::Axis3D CenterWorld    { 0.0f, 0.0f, 0.35f };
};

struct FluidSceneNames
{
    std::string Domain   { "Fluid Domain" };
    std::string Emitter  { "Pyro Emitter" };
    std::string Collider { "Collider" };
    std::string Sun      { "Key Light" };
};

struct FluidViewportOverlaySettings
{
    bool                   ShowBoundingBox      = false;
    bool                   ShowVoxelGridLines   = false;
    bool                   ShowActiveVoxelCells = false;
    bool                   ShowFloorGrid        = true;
    bool                   ShowAtlasMinimap     = false;
    FluidAtlasMinimapField AtlasMinimapField    = FluidAtlasMinimapField::SmokeTemperature;
    bool                   AutoTurntable        = false;
    FluidInteractionTool   InteractionTool      = FluidInteractionTool::Orbit;
};

struct FluidSceneDocument
{
    std::string                   SceneName       { "Pyro plume" };
    std::string                   PresetKey       { "ue5_pyro_default" };
    FluidDomainSettings           Domain          {};
    FluidEmitterSettings          Emitter         {};
    FluidBlastSpecification       Blast           {};
    FluidCombustionSettings       Combustion      {};
    FluidObstacleSettings         Obstacle        {};
    FluidShadingSettings          Shading         {};
    FluidParticleCouplingSettings Particles       {};
    FluidGiCouplingSettings       GiCoupling      {};
    FluidSceneCameraState         Camera          {};
    FluidSceneNames               Names           {};
    FluidViewportOverlaySettings  Overlays        {};
};

} // namespace Frontier::SimulationSpace::FluidSpace
