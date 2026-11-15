/*============================================================================================================================================
                                                        CONTROLVEHICLESURFACELOADER.H
============================================================================================================================================*/
// 🧩 Decodes Frontier's Controlled Integrator (ControlVehicle) FCT1 binary surfaces (Body + 4 Rims + 4 Tyres) and samples blue-noise anchors

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ControlVehiclePartCategory : uint8_t
{
    BodyChassis  = 0u,
    RimFrontLeft = 1u,
    RimFrontRight = 2u,
    RimRearLeft  = 3u,
    RimRearRight = 4u,
    TyreFrontLeft = 5u,
    TyreFrontRight = 6u,
    TyreRearLeft = 7u,
    TyreRearRight = 8u
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

/// Single oriented triangle of the assembled Controlled Integrator vehicle in studio coordinates (+X forward, +Y up, +Z right).
struct ControlVehicleTriangle
{
    float                  PositionA[3]     = { 0.0f, 0.0f, 0.0f }; // [m]   - Corner A world/studio coordinate
    float                  PositionB[3]     = { 0.0f, 0.0f, 0.0f }; // [m]   - Corner B world/studio coordinate
    float                  PositionC[3]     = { 0.0f, 0.0f, 0.0f }; // [m]   - Corner C world/studio coordinate
    float                  NormalA[3]       = { 0.0f, 1.0f, 0.0f }; // [-]   - Creased unit normal at A
    float                  NormalB[3]       = { 0.0f, 1.0f, 0.0f }; // [-]   - Creased unit normal at B
    float                  NormalC[3]       = { 0.0f, 1.0f, 0.0f }; // [-]   - Creased unit normal at C
    float                  TriangleAreaM2   = 0.0f;                 // [m^2] - Surface area for uniform CDF sampling
    uint32_t               MaterialSlotCode = 6u;                   // [idx] - 6 BodyCoat, 7 Glass, 8 Plastic, 9 Tyre, 10 Hub, 11 Brake, 12 Trim, 13 Headlight, 14 Taillight
    ControlVehiclePartCategory PartCategory         = ControlVehiclePartCategory::BodyChassis;
};

/// Cached full Controlled Integrator vehicle triangle collection (Body + 4 Rims + 4 Tyres).
struct ControlVehicleGeometrySet
{
    std::vector<ControlVehicleTriangle> Triangles       {};
    float                               BoundsMin[3]    = { -3.11f, 0.0f, -1.25f }; // [m] - Studio AABB lower corner
    float                               BoundsMax[3]    = {  3.11f, 1.8f,  1.25f }; // [m] - Studio AABB upper corner
    float                               CenterMeters[3] = {  0.0f,  0.9f,  0.0f  }; // [m] - Studio AABB midpoint
    float                               HalfLengthM     = 3.11f;                    // [m] - Half longitudinal span
    uint32_t                            BodyTriCount    = 0u;                       // [cnt] - Leading triangles belonging to BodyChassis
    bool                                LoadedFromDisk  = false;                    // [-] - True when real FCT1 binary files loaded
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

/// Returns the cached Controlled Integrator (ControlVehicle) studio-space triangle set (loaded once from FCT1 files).
[[nodiscard]] const ControlVehicleGeometrySet& QueryControlVehicleGeometrySet();

/// Samples evenly spaced area-weighted blue-noise surface anchors across the Controlled Integrator vehicle.
/// CurvatureWeight stores normalized vertical elevation in [-1, 1] (matching HTML ParticleEditor localY convention).
/// GroupIndex stores the originating triangle MaterialSlotCode (6..15).
[[nodiscard]] std::vector<ParticleAnchorSample> SampleControlVehicleAnchors(
    uint32_t SampleCount,
    uint32_t Seed                  = 1337u,
    bool     IncludeWheels         = true,
    bool     NormalizeToUnitSpan   = false);

} // namespace Frontier::ParticleSpace
