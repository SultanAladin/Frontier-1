/*============================================================================================================================================
                                                           VEHICLEDISSOLVESOLVER.H
============================================================================================================================================*/
// 🧩 Controlled Integrator (ControlVehicle) 16^3 lattice Derez, Reverse-Rez, Dissolve-to-Nothing, and Dissolve-to-Butterfly solver + Z-buffered two-way GI renderer

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Origins/ControlVehicleSurfaceLoader.h"

#include <cstdint>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class VehicleDissolveMode : uint8_t
{
    DerezCubes        = 0, // [-] - 16^3 lattice dissolve with white-hot->cyan rim, ballistic cubes, and 8-sub-cube floor shatter
    ReverseRezCubes   = 1, // [-] - Scattered luminous cubes spiral inward and lock cleanly into intact obsidian ControlVehicle
    DissolveToNothing = 2, // [-] - Sublimation front vaporizes bodywork into rising fading embers and micro-vapor wisps
    DissolveToButterfly = 3, // [-] - Iridescent dissolve wave transforms car panels into articulated 3D flapping butterfly swarm
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct VehicleDissolveParams
{
    VehicleDissolveMode Mode              = VehicleDissolveMode::DerezCubes;
    float               Progress          = 0.52f; // [0-1] - Master normalized dissolution wave clock
    uint32_t            LatticeResolution = 16u;   // [cnt] - Per-axis cubic lattice cell count (default 16^3)
    uint32_t            Seed              = 4201u; // [-]   - Deterministic jitter and fragment seed
    float               LeadingRimWidth   = 0.115f;// [0-1] - Incandescent leading-edge band thickness before discard
    float               LinearDrag        = 0.62f; // [1/s] - Closed-form air drag coefficient for detached cubes
    float               GravityY          = -8.8f; // [m/s2]- Vertical gravitational acceleration
    float               ShatterRestitution = 0.42f;// [0-1] - Floor bounce restitution for 8-way shattered sub-cubes
    float               BounceLightGain   = 1.15f; // [-]   - Two-way GI emissive floor and bodywork bounce strength
    bool                EnableTwoWayGi    = true;  // [-]   - Inject glowing fragment / rim bounce light onto car + studio floor
};

struct VehicleDissolveCubePiece
{
    float    PosX          = 0.0f;
    float    PosY          = 0.0f;
    float    PosZ          = 0.0f;
    float    VelX          = 0.0f;
    float    VelY          = 0.0f;
    float    VelZ          = 0.0f;
    float    HalfExtent    = 0.06f;
    float    RotX          = 0.0f;
    float    RotY          = 0.0f;
    float    RotZ          = 0.0f;
    float    GlowHeat      = 0.0f;  // [0-1] - Incandescent leading-edge -> cyan heat remaining
    float    Opacity       = 1.0f;  // [0-1] - Alpha fade
    uint8_t  MaterialSlot  = 6u;    // [6=BodyCoat, 7=Glass, 8=Plastic, 9=Tyre, 10=Hub, 11=Brake, 12=Trim, 13=Headlight, 14=Taillight]
    uint8_t  IsSubShatter  = 0u;    // [0/1] - 1 when spawned as one of the 8 floor-impact sub-cubes
};

struct VehicleDissolveButterflyPiece
{
    float PosX       = 0.0f;
    float PosY       = 0.0f;
    float PosZ       = 0.0f;
    float HeadingYaw = 0.0f;
    float PitchTilt  = 0.0f;
    float WingSpan   = 0.14f;
    float FlapAngle  = 0.4f;  // [rad] - Instantaneous left/right dihedral hinge flap angle
    float BotanicalGrowthMix  = 0.8f;  // [0-1] - 1 = electric BotanicalGrowth cyan, 0 = warm Monarch amber/gold
    float Shimmer    = 0.8f;  // [0-1] - Iridescent wing glow
    float Opacity    = 1.0f;
};

struct VehicleDissolveVaporPiece
{
    float PosX    = 0.0f;
    float PosY    = 0.0f;
    float PosZ    = 0.0f;
    float Radius  = 0.08f;
    float Heat    = 0.0f; // [0-1] - 1 = ember spark, 0 = cool fading vapor
    float Opacity = 0.0f;
};

struct VehicleDissolveSnapshot
{
    VehicleDissolveMode                        Mode                = VehicleDissolveMode::DerezCubes;
    float                                      Clock               = 0.0f;
    uint32_t                                   IntactAnchorCount   = 0u;
    uint32_t                                   ReleasedCellCount   = 0u;
    uint32_t                                   ShatteredCubeCount  = 0u;
    std::vector<VehicleDissolveCubePiece>      Cubes;
    std::vector<VehicleDissolveButterflyPiece> Butterflies;
    std::vector<VehicleDissolveVaporPiece>     Vapors;
};

struct VehicleDissolveRenderCamera
{
    float EyeX       = 4.55f;
    float EyeY       = 1.92f;
    float EyeZ       = 4.35f;
    float TargetX    = 0.0f;
    float TargetY    = 0.88f;
    float TargetZ    = -0.05f;
    float VerticalFovDeg = 35.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Shared deterministic 16^3 lattice release clock so intact car pixel discard and detached fragment release stay 100% locked.
[[nodiscard]] float EvaluateVehicleDissolveReleaseTime(
    float                       WorldX,
    float                       WorldY,
    float                       WorldZ,
    const ControlVehicleGeometrySet& Vehicle,
    const VehicleDissolveParams& Params) noexcept;

// Builds the deterministic snapshot of detached cubes, 8-way shattered sub-cubes, vapor motes, or articulated 3D butterflies.
[[nodiscard]] VehicleDissolveSnapshot BuildVehicleDissolveSnapshot(
    const ControlVehicleGeometrySet& Vehicle,
    const VehicleDissolveParams&     Params);

// Unified depth-buffered PBR + emissive rim + two-way GI studio rasterizer for all four ControlVehicle HMI dissolve modes.
void RenderVehicleDissolveStudioRgba8(
    const ControlVehicleGeometrySet& Vehicle,
    const VehicleDissolveParams&     Params,
    const VehicleDissolveSnapshot&   Snapshot,
    const VehicleDissolveRenderCamera& Camera,
    uint32_t                         Width,
    uint32_t                         Height,
    std::vector<uint8_t>&            OutRgba8);

} // namespace Frontier::ParticleSpace
