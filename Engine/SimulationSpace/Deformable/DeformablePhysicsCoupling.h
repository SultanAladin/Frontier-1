/*============================================================================================================================================
                                                     DEFORMABLEPHYSICSCOUPLING.H
============================================================================================================================================*/
// 🧩 Phase D03 Bidirectional coupling between DeformableBody, ForceFieldSampler & Jolt BroadphaseSolver RigidColliderProxies
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldSampler.h"
#include "Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.h"

#include <array>
#include <span>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PHYSICS COUPLING
//------------------------------------------------------------------------------------------------------------------------

struct DeformableExternalCoupling
{
    std::span<const ForceFields::ForceModelGpuRecord>  ForceRecords {};
    std::span<const PhysicsSpace::RigidColliderProxy>  RigidProxies {};
    uint32_t                                           ForceChannelMask = 0xFFFFFFFFu;
};

struct DeformableCouplingFeedback
{
    uint32_t RigidContactCount   = 0u;
    float    TotalNormalImpulse  = 0.0f; // [N*s]
};

class DeformablePhysicsCoupling
{
public:
    /// Evaluates studio curl-wind + registered ForceFields::ForceModelGpuRecord velocity at a point.
    [[nodiscard]] static std::array<float, 3> SampleWindForceVelocity(
        float x,
        float y,
        float z,
        float timeSec,
        const DeformableSimulationConfiguration& configuration,
        float windPulse,
        const DeformableExternalCoupling* externalCoupling = nullptr) noexcept;

    /// Projects all dynamic mass points against live Jolt RigidColliderProxies (Sphere, Box, Capsule).
    [[nodiscard]] static DeformableCouplingFeedback ProjectRigidProxies(
        DeformableBody& body,
        std::span<const PhysicsSpace::RigidColliderProxy> proxies) noexcept;

    /// Builds a 128-byte GPU DeformableStepUniforms block synchronized with the current substep.
    [[nodiscard]] static DeformableStepUniforms BuildStepUniforms(
        const DeformableBody& body,
        const DeformableSimulationConfiguration& configuration,
        float h,
        float simTimeSec,
        float windPulse,
        uint32_t batchStart = 0u,
        uint32_t batchEnd = 0u) noexcept;
};

} // namespace Frontier::SimulationSpace::Deformable
