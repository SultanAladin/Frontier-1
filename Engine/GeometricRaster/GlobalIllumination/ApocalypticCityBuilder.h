/*============================================================================================================================================
                                                        APOCALYPTICCITYBUILDER.H
============================================================================================================================================*/
// 🧩 Procedural ruined city racing world generator with convex-fractured skyscrapers, multi-floor & skybridge ramps, wrecks, flora, deer & destruction

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"

#include <cstdint>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ApocalypticFallingFragment
{
    uint32_t            AssetSlotIndex      = 0u;                      // [idx] - Index in ShowcaseWorldSceneBundle::Objects
    Mathematics::Axis3D InitialCenterWorld  {};                        // [m]   - Authored pre-collapse world coordinate
    Mathematics::Axis3D CurrentCenterWorld  {};                        // [m]   - Active simulated world coordinate
    Mathematics::Axis3D VelocityMetersSec   {};                        // [m/s] - Linear velocity under gravity & blast impulse
    float               InitialYawDegrees   = 0.0f;                    // [deg] - Authored Z-axis yaw angle
    float               CurrentYawDegrees   = 0.0f;                    // [deg] - Simulated Z-axis yaw angle
    float               YawRateDegPerSec    = 0.0f;                    // [deg/s]- Angular spin rate during fall
    float               RestElevationZ      = 0.25f;                   // [m]   - Landing floor or street elevation
    float               NoiseSeedOffset     = 0.0f;                    // [-]   - Spatial-temporal coherent noise offset
    float               StructuralThreshold = 1.0f;                    // [-]   - Cumulative noise integral required to detach
    float               AccumulatedFatigue  = 0.0f;                    // [-]   - Accumulated seismic/wind structural fatigue
    float               Restitution         = 0.22f;                   // [0-1] - Damped impact bounce coefficient
    bool                AwaitingImpact      = false;                   // [-]   - True when held until vehicle/shockwave trigger
    bool                HiddenUntilShatter  = false;                   // [-]   - True for glass shards spawned when panel breaks
    bool                Detached            = false;                   // [-]   - True once falling under gravity
    bool                Settled             = false;                   // [-]   - True once resting on landing floor/street
};

struct ApocalypticSmashableBarrier
{
    uint32_t              IntactSlotIndex     = 0xFFFFFFFFu;           // [idx] - Intact pane hidden on shatter (0xFFFFFFFF = pre-split wall)
    Mathematics::Axis3D   TriggerCenterWorld  {};                      // [m]   - Impact detection center in world coordinates
    Mathematics::Axis3D   OutwardImpulseDir   { 0.0f, 1.0f, 0.0f };    // [-]   - Primary outward burst direction on breach
    float                 TriggerRadiusMeters = 4.2f;                  // [m]   - Proximity/impact radius for vehicle or blast
    float                 AutoBreachTimeSec   = 999.0f;                // [s]   - Scheduled shockwave breach time during simulation
    float                 BurstSpeedMetersSec = 8.5f;                  // [m/s] - Outward shard/masonry ejection speed
    bool                  GlassCurtain        = false;                 // [-]   - True for Abbe glass curtain, false for masonry wall
    bool                  Triggered           = false;                 // [-]   - True once shattered/breached
    std::vector<uint32_t> FragmentIndices     {};                      // [idx] - Linked indices in DynamicFragments
};

struct ApocalypticDeerWanderer
{
    uint32_t            AssetSlotIndex       = 0u;                     // [idx] - Index in ShowcaseWorldSceneBundle::Objects
    Mathematics::Axis3D PositionWorld        {};                       // [m]   - Grounded world center of deer actor
    float               RestElevationZ       = 0.72f;                  // [m]   - Standing center elevation above Z = 0 datum
    float               HeadingDegrees       = 0.0f;                   // [deg] - Forward travel heading around +Z
    float               CruiseSpeedMetersSec = 2.35f;                  // [m/s] - Natural trotting/foraging speed
    float               StridePhaseRadians   = 0.0f;                   // [rad] - Gait cycle phase for vertical stride bob
    float               WanderSeed           = 0.0f;                   // [-]   - Per-animal coherent steering noise seed
};

struct ApocalypticCitySimulationRegister
{
    bool                                     ActiveCityLevel       = false;        // [-]   - True when Apocalyptic City scene is loaded
    bool                                     HeroAutoDriveEnabled  = true;         // [-]   - Drives Hero ControlVehicle along multi-floor course
    uint32_t                                 HeroVehicleIndex      = 0xFFFFFFFFu;  // [idx] - Hero ControlVehicle index in Bundle.Objects
    double                                   ElapsedSeconds        = 0.0;          // [s]   - Total simulated warzone time
    float                                    HeroSplineProgress    = 0.0f;         // [0-1] - Normalized progress along multi-building spline
    uint32_t                                 DetachedFragmentCount = 0u;           // [cnt] - Count of collapsed building/floor/debris chunks
    uint32_t                                 ShatteredGlassCount   = 0u;           // [cnt] - Count of shattered Abbe glass curtain walls
    uint32_t                                 BreachedWallCount     = 0u;           // [cnt] - Count of smashed masonry breach walls
    std::vector<Mathematics::Axis3D>         HeroCourseWaypoints   {};             // [m]   - Street -> Overpass -> Tower A F2/F3 -> Skybridge -> Tower B -> Street
    std::vector<ApocalypticFallingFragment>  DynamicFragments      {};             // [-]   - Noise-collapsing slabs, debris, shards & wall blocks
    std::vector<ApocalypticSmashableBarrier> SmashableBarriers     {};             // [-]   - Smashable glass curtains & masonry breach walls
    std::vector<ApocalypticDeerWanderer>     RoamingDeer           {};             // [-]   - Autonomous obstacle-avoiding Woodland Deer herd
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

void BuildApocalypticCityRacingScene(
    ShowcaseWorldSceneBundle&          OutBundle,
    ApocalypticCitySimulationRegister& OutSimulation,
    uint32_t                           ViewportWidthPx  = 1280u,
    uint32_t                           ViewportHeightPx = 720u,
    uint32_t                           RandomSeed       = 20261122u);

void RegisterApocalypticCityInOutliner(
    OutlinerPanel&            TargetOutliner,
    ShowcaseWorldSceneBundle& InOutBundle,
    bool                      PreserveConstructTemplates = true);

[[nodiscard]] bool StepApocalypticCitySimulation(
    ShowcaseWorldSceneBundle&          InOutBundle,
    ApocalypticCitySimulationRegister& InOutSimulation,
    OutlinerPanel*                     OptionalOutliner,
    double                             DeltaSeconds);

} // namespace Frontier::GeometricRaster
