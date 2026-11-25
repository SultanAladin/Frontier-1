/*============================================================================================================================================
                                                       FORCEFIELDPHYSICSCOUPLING.H
============================================================================================================================================*/
// 🧩 Wait-free ForceFieldRegister -> JoltPhysics rigid body & vehicle aerodynamic coupling + Stage F (Slice S24) two-way GPU collision readback
// Reference: UnifiedFluidParticleForceFieldPortPlan.md §1.4 & §2.2A (Slices S06 & S24), BroadphaseSolver.{h,cpp}

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/ParticleSpace/ParticleGpuSimulation.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldRegister.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldSampler.h"
#include "Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.h"
#include "Engine/SimulationSpace/PhysicsSpace/RigidBodyDynamics.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::SimulationSpace::PhysicsSpace
{

class PhysicsExtension;

//------------------------------------------------------------------------------------------------------------------------
//                                                     DATA STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

inline constexpr float    kRigidImpulseFixedScale  = 100000.0f; // [1/(N*s)] - Fixed-point scale for GPU InterlockedAdd
inline constexpr uint32_t kCompoundHullShapeCode   = 3u;        // [enum]    - RigidColliderProxy::ShapeCode for baked 3D SDF hull

enum class ForceCouplingMode : uint8_t
{
    MassProportionalAcceleration = 0u, // [N] - F = m * a_field * CouplingScale (gravity / wind / tractor beam parity)
    AerodynamicQuadraticDrag     = 1u, // [N] - F = 0.5 * rho * Cd * A * |v_rel| * v_rel using field vector as air velocity
};

struct CoupledRigidBodyEntry
{
    RigidBodyToken    BodyToken             {};                                               // [id]     - Live Jolt rigid body token
    ForceCouplingMode CouplingMode          = ForceCouplingMode::MassProportionalAcceleration; // [-]     - Force integration mode
    float             CouplingScale         = 1.0f;                                           // [-]      - Scalar strength multiplier
    float             DragAreaM2            = 1.0f;                                           // [m^2]    - Reference frontal/lateral area for Aero mode
    float             DragCoefficient       = 0.8f;                                           // [-]      - Dimensionless Cd for Aero mode
    float             AirDensityKgPerM3     = 1.225f;                                         // [kg/m^3] - Ambient fluid density (ISA sea level)
    float             MaximumForceNewtons   = 250000.0f;                                      // [N]      - Safety clamp against extreme singularities
    uint32_t          ChannelMaskBits       = static_cast<uint32_t>(ForceFields::ForceTargetChannelBits::Rigid);
};

struct ForceCouplingStepSummary
{
    uint32_t CoupledBodyCount    = 0u;   // [count] - Live dynamic bodies actuated during this tick
    float    PeakForceNewtons    = 0.0f; // [N]     - Largest force magnitude applied this tick
    float    TotalForceNewtons   = 0.0f; // [N]     - Sum of force magnitudes applied this tick
};

/// 64-byte GPU->CPU impulse readback entry (shared contract for Stage F Slice S24 two-way particle-rigid coupling).
struct alignas(16) RigidImpulseAccumulatorEntry
{
    RigidBodyToken BodyToken       {};                   // [id]    - Target dynamic Jolt body
    uint32_t       ContactCount    = 0u;                 // [count] - Particle collisions accumulated this step
    uint32_t       ReservedPad     = 0u;                 // [-]     - 16-byte alignment padding
    float          LinearImpulse[4]= { 0.0f, 0.0f, 0.0f, 0.0f };  // [N*s]   - xyz = net linear impulse on rigid body
    float          AngularImpulse[4]= { 0.0f, 0.0f, 0.0f, 0.0f }; // [N*m*s] - xyz = net angular impulse about COM
    float          ContactPoint[4] = { 0.0f, 0.0f, 0.0f, 0.0f };  // [m]     - xyz = weighted average contact point
};
static_assert(sizeof(RigidImpulseAccumulatorEntry) == 64u, "RigidImpulseAccumulatorEntry must remain 64 bytes.");

/// Matches CompoundHullSdfHeaderGpu in ParticleCollisionCompute.slang byte-for-byte (48 bytes).
struct alignas(16) CompoundHullSdfHeaderGpu
{
    std::array<float, 4>    BoundsMinCellSize { 0.0f, 0.0f, 0.0f, 0.1f }; // xyz = local AABB min, w = voxel cell size
    std::array<uint32_t, 4> GridDimOffset     { 0u, 0u, 0u, 0u };         // xyz = dims, w = offset in voxel table
    std::array<uint32_t, 4> BodySlotPad       { 0u, 0u, 0u, 0u };         // x = matching BodySlotIndex
};
static_assert(sizeof(CompoundHullSdfHeaderGpu) == 48u, "CompoundHullSdfHeaderGpu must be 48 bytes.");

/// Matches RigidImpulseAtomicGpu in ParticleCollisionCompute.slang byte-for-byte (48 bytes).
struct alignas(16) RigidImpulseAtomicGpu
{
    int32_t  LinearImpulseFixed[3]   { 0, 0, 0 };
    uint32_t ContactCount            = 0u;
    int32_t  AngularImpulseFixed[3]  { 0, 0, 0 };
    uint32_t BodySlotIndex           = 0u;
    int32_t  ContactPointSumFixed[3] { 0, 0, 0 };
    uint32_t ReservedPad             = 0u;
};
static_assert(sizeof(RigidImpulseAtomicGpu) == 48u, "RigidImpulseAtomicGpu must be 48 bytes.");

/// Matches ParticleCollisionUniformBlock in ParticleCollisionCompute.slang byte-for-byte (128 bytes).
struct alignas(16) ParticleCollisionUniformBlock
{
    std::array<std::array<float, 4>, 4> AnalyticColliders {};
    std::array<float, 4>                GroundHeightFlags { 0.0f, 0.0f, 0.0f, 0.0f }; // groundY, groundEnabled, analyticCount, particleCount
    std::array<float, 4>                ParticleMaterial  { 0.05f, 0.1f, 0.35f, 0.4f }; // radius, mass, restitution, friction
    std::array<uint32_t, 4>             CountsMasks       { 0u, 0u, 0xFFFFFFFFu, 1u }; // proxyCount, hullCount, channelMask, twoWayEnabled
    std::array<float, 4>                ScaleLifetime     { kRigidImpulseFixedScale, 0.0f, 500.0f, 0.0f }; // fixedScale, lifetime, maxImpulse, pad
};
static_assert(sizeof(ParticleCollisionUniformBlock) == 128u, "ParticleCollisionUniformBlock must be 128 bytes.");

struct CompoundHullSubSphere
{
    std::array<float, 3> LocalCenterMeters { 0.0f, 0.0f, 0.0f };
    float                RadiusMeters      = 0.25f;
};

struct CompoundHullSdfBank
{
    std::vector<CompoundHullSdfHeaderGpu> Headers {};
    std::vector<float>                    Voxels  {};

    /// Bakes a union of sub-spheres into a local-space 3D signed-distance field appended to this bank.
    void AppendSphereUnionHull(
        uint32_t bodySlotIndex,
        std::span<const CompoundHullSubSphere> subSpheres,
        float cellSizeMeters = 0.08f,
        float paddingMeters  = 0.20f);
};

//------------------------------------------------------------------------------------------------------------------------
//                                           PURE FORCE COUPLING COMPUTATION
//------------------------------------------------------------------------------------------------------------------------

/// Evaluates the net ForceField force vector [N] acting on a single RigidBodyReading from an immutable PublishedForceFieldReading.
[[nodiscard]] Mathematics::Axis3D ComputeCoupledRigidForceNewtons(
    const CoupledRigidBodyEntry& entry,
    const ForceFields::PublishedForceFieldReading& reading,
    const RigidBodyReading& bodyReading,
    double simulationSeconds) noexcept;

/// Evaluates the local aerodynamic crosswind force [N] on a vehicle chassis from ForceFields (using lateral/frontal drag areas).
[[nodiscard]] Mathematics::Axis3D ComputeVehicleWindForceNewtons(
    const ForceFields::PublishedForceFieldReading& reading,
    const RigidBodyReading& chassisReading,
    float frontalAreaM2,
    float sideAreaM2,
    float dragCoefficient,
    float airDensityKgPerM3,
    double simulationSeconds) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                 SLICE S24 CPU COLLISION MIRROR & READBACK DECODER
//------------------------------------------------------------------------------------------------------------------------

/// Exact CPU mirror of ParticleCollisionCompute.slang::ComputeEntry (analytic + 80-byte RigidColliderProxy + CompoundHull SDF + atomic readback).
void ResolveParticleRigidCollisionsCpu(
    const ParticleCollisionUniformBlock& uniforms,
    std::span<Frontier::ParticleSpace::ParticleStateGpuRecord> particles,
    std::span<const RigidColliderProxy> proxies,
    std::span<const std::array<float, 4>> proxyAngularVelocities,
    const CompoundHullSdfBank& hullBank,
    std::vector<RigidImpulseAtomicGpu>& outAtomics);

/// Decodes GPU fixed-point RigidImpulseAtomicGpu readback entries into floating-point RigidImpulseAccumulatorEntry records.
void DecodeGpuImpulseReadback(
    std::span<const RigidImpulseAtomicGpu> atomics,
    std::span<const RigidBodyToken> proxyBodyTokens,
    float fixedPointScale,
    std::vector<RigidImpulseAccumulatorEntry>& outEntries);

//------------------------------------------------------------------------------------------------------------------------
//                                               FORCEFIELDPHYSICSCOUPLING
//------------------------------------------------------------------------------------------------------------------------

class ForceFieldPhysicsCoupling
{
public:
    ForceFieldPhysicsCoupling() noexcept = default;
    ~ForceFieldPhysicsCoupling() noexcept = default;

    void Clear() noexcept { CoupledBodies.clear(); }
    void RegisterRigidBody(const CoupledRigidBodyEntry& entry);
    bool UnregisterRigidBody(RigidBodyToken bodyToken) noexcept;
    [[nodiscard]] uint32_t QueryCoupledBodyCount() const noexcept { return static_cast<uint32_t>(CoupledBodies.size()); }

    /// Wait-free reads the published ForceFieldRegister and applies resulting forces to all registered dynamic bodies in PhysicsExtension.
    [[nodiscard]] ForceCouplingStepSummary ApplyPublishedForceFields(
        PhysicsExtension& physics,
        const ForceFields::ForceFieldRegister& forceRegister,
        double simulationSeconds) const noexcept;

    /// Wait-free reads the published ForceFieldRegister and applies aerodynamic wind/gust forces to a vehicle chassis body.
    [[nodiscard]] Mathematics::Axis3D ApplyPublishedWindToVehicleChassis(
        PhysicsExtension& physics,
        RigidBodyToken chassisToken,
        const ForceFields::ForceFieldRegister& forceRegister,
        float frontalAreaM2,
        float sideAreaM2,
        float dragCoefficient,
        double simulationSeconds,
        float airDensityKgPerM3 = 1.225f) const noexcept;

    /// Applies GPU-readback particle collision impulses (Stage F Slice S24) onto live dynamic Jolt rigid bodies.
    [[nodiscard]] uint32_t ApplyGpuImpulseReadback(
        PhysicsExtension& physics,
        std::span<const RigidImpulseAccumulatorEntry> readbackEntries) const noexcept;

private:
    std::vector<CoupledRigidBodyEntry> CoupledBodies {}; // [-] - Registered dynamic rigid bodies receiving force fields
};

} // namespace Frontier::SimulationSpace::PhysicsSpace
