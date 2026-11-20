/*============================================================================================================================================
                                                        HYBRIDCONESHADOWSEQUENCE.H
============================================================================================================================================*/
// 🧩 Order 2 shadows: GtxLowEnd Hybrid Cone (0.28 m BVH4 contact + SmoothMin C0 SDF + Capsule Rig) vs HighEnd 0-SDF BVH4 Vogel Solar

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/ArticulatedCapsuleRig.h"
#include "Engine/DistanceFieldSpace/LocalSignedDistanceVolume.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SpatialAcceleration/CompressedWideTree.h"

#include <cstdint>
#include <span>

namespace Frontier::GeometricRaster::DirectLighting
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct HybridConeShadowSetup
{
    float    ContactRangeMeters = 0.28f;  // [m]   - Exact BVH4 short-range contact ray length before SDF handoff
    float    SmoothBlendMeters  = 0.08f;  // [m]   - C1 SmoothMin blend radius across asset SDF + static boxes + capsule rig
    float    ConeSoftness       = 11.0f;  // [-]   - Directional penumbra sharpness (higher = crisper solar disk)
    float    MaxTraceMeters     = 14.0f;  // [m]   - Maximum SDF directional march distance
    uint32_t MaxMarchSteps      = 40u;    // [cnt] - Maximum directional cone steps
    uint32_t VogelRaySamples    = 6u;     // [cnt] - HighEnd 0-SDF BVH4 Vogel solar disk ray count
    float    SunAngularRadius   = 0.032f; // [rad] - Apparent solar / studio key angular radius
};

struct HybridOccluderEnvironment
{
    const SpatialAcceleration::CompressedWideTree*               SceneBvh          = nullptr; // Shared BVH4 (contact on GtxLowEnd; full shadow on HighEnd)
    std::span<const DistanceFieldSpace::ClosedOccluderBox>       InteriorBoxes     {};        // GtxLowEnd-only analytic interior boxes
    DistanceFieldSpace::RigidLocalVolumePlacement                VehiclePlacement  {};        // GtxLowEnd-only 48^3 C0 local distance volume
    const DistanceFieldSpace::ArticulatedCapsuleRig*             CharacterRig      = nullptr; // GtxLowEnd-only live deforming capsule rig
    bool                                                         ForbidDistanceField = false; // HighEnd invariant: true => zero SDF reads
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 EVALUATION FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Evaluates C1-smooth combined distance across interior boxes + C0 vehicle SDF + live capsule rig (GtxLowEnd only)
[[nodiscard]] float EvaluateCombinedOccludersDistance(
    const HybridOccluderEnvironment& Env,
    Mathematics::Axis3D              WorldPos,
    float                            SmoothBlendMeters = 0.08f) noexcept;

// Candidate 1 (GtxLowEnd): 0.28 m BVH4 contact ray + SmoothMin C0 SDF + Capsule Rig directional cone visibility in [0, 1]
[[nodiscard]] float EvaluateGtxHybridDirectionalShadow(
    const HybridOccluderEnvironment& Env,
    Mathematics::Axis3D              ShadingPos,
    Mathematics::Axis3D              ShadingNormal,
    Mathematics::Axis3D              DirectionToLight,
    const HybridConeShadowSetup&     Setup = {}) noexcept;

// HighEnd (0 MB SDF): Pure BVH4 Vogel-disk jittered solar ray visibility in [0, 1]
[[nodiscard]] float EvaluateHighEndBvhDirectionalShadow(
    const SpatialAcceleration::CompressedWideTree& SceneBvh,
    Mathematics::Axis3D                            ShadingPos,
    Mathematics::Axis3D                            ShadingNormal,
    Mathematics::Axis3D                            DirectionToLight,
    uint32_t                                       PixelSeed,
    const HybridConeShadowSetup&                   Setup = {}) noexcept;

// GtxLowEnd finite-segment luminaire visibility: 0.28 m BVH4 contact + 32-step uniform optical-depth SDF cone
[[nodiscard]] float EvaluateGtxHybridLuminaireVisibility(
    const HybridOccluderEnvironment& Env,
    Mathematics::Axis3D              ShadingPos,
    Mathematics::Axis3D              ShadingNormal,
    Mathematics::Axis3D              LuminaireTargetPos,
    float                            LuminaireRadiusMeters) noexcept;

// HighEnd (0 MB SDF) finite-segment luminaire visibility: pure BVH4 ray occlusion
[[nodiscard]] float EvaluateHighEndBvhLuminaireVisibility(
    const SpatialAcceleration::CompressedWideTree& SceneBvh,
    Mathematics::Axis3D                            ShadingPos,
    Mathematics::Axis3D                            ShadingNormal,
    Mathematics::Axis3D                            LuminaireTargetPos) noexcept;

} // namespace Frontier::GeometricRaster::DirectLighting
