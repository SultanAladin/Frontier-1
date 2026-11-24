/*============================================================================================================================================
                                                    PARTICLEXPBDNETWORKBUILDER.H
============================================================================================================================================*/
// 🧩 Stage F (S26) Conflict-colored zero-atomic Orthotropic XPBD constraint network builder, GPU table packer & CPU reference solver
// Reference: Experimental/ClothEditor/Engine/WebGpuClothAccelerator.js L9-L100, L201-L435, L609-L653 & PatternDraftNetwork.js L75-L120, L431-L530

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceFieldSampler.h"
#include "Engine/SimulationSpace/ForceFields/ForceModelTypes.h"
#include "Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                              CONFLICT-COLORED TOPOLOGY TABLES
//------------------------------------------------------------------------------------------------------------------------

struct ConflictColorBatchTable
{
    uint32_t              ColorCount   = 0u;
    std::vector<uint32_t> Offsets      {}; // size = ColorCount + 1
    std::vector<uint32_t> OrderedItems {}; // permutation of [0..itemCount-1] grouped by color
};

struct XpbdConstraintNetwork
{
    uint32_t                           PointCount         = 0u;
    std::vector<std::array<float, 3>>  RestPositions      {};
    std::vector<std::array<float, 2>>  MaterialUV         {};
    std::vector<float>                 InverseMass        {}; // 0 = pinned vertex
    std::vector<float>                 PointRestArea      {};

    // Distance / Strand / Cable constraints
    uint32_t                           DistanceCount      = 0u;
    std::vector<uint32_t>              DistanceIndices    {}; // 2 * DistanceCount
    std::vector<float>                 DistanceRestLength {}; // DistanceCount
    std::vector<float>                 DistanceStiffness  {}; // DistanceCount [N/m]
    std::vector<float>                 DistanceBreakRatio {}; // DistanceCount (0 = unbreakable, >1 = tear ratio)
    ConflictColorBatchTable            DistanceBatches    {};

    // Orthotropic Triangle Strain constraints (Warp, Weft, Shear)
    uint32_t                           FaceCount          = 0u;
    std::vector<uint32_t>              FaceIndices        {}; // 3 * FaceCount
    std::vector<float>                 FaceDmInv          {}; // 4 * FaceCount (q00, q01, q10, q11)
    std::vector<float>                 FaceRestArea       {}; // FaceCount
    ConflictColorBatchTable            FaceBatches        {};

    // Quadratic Isometric Hinge Bending constraints
    uint32_t                           HingeCount         = 0u;
    std::vector<uint32_t>              HingeIndices       {}; // 4 * HingeCount (v0, v1, oppA, oppB)
    std::vector<float>                 HingeWeights       {}; // 4 * HingeCount (c0, c1, c2, c3)
    std::vector<float>                 HingeArea          {}; // HingeCount
    ConflictColorBatchTable            HingeBatches       {};

    // Seam Stitch constraints
    uint32_t                           SeamCount          = 0u;
    std::vector<uint32_t>              SeamIndices        {}; // 2 * SeamCount
    std::vector<float>                 SeamInitialGap     {}; // SeamCount
    ConflictColorBatchTable            SeamBatches        {};
};

struct XpbdPackedGpuTables
{
    static constexpr uint32_t kHeaderWords = 24u;
    std::vector<uint32_t> U32Topology {};
    std::vector<float>    F32Rest     {};
    uint32_t              TotalDuals  = 0u;
    uint32_t              HingeDualOffset    = 0u;
    uint32_t              SeamDualOffset     = 0u;
    uint32_t              DistanceDualOffset = 0u;
};

/// Exact 128-byte uniform block matching ParticleXpbdCompute.slang::XpbdUniformBlock.
struct alignas(16) ParticleXpbdUniformBlock
{
    std::array<float, 4>    TimeControl     { 1.0f / 240.0f, 240.0f, 57600.0f, 1.0f }; // dt, invDt, invDt2, zipAlpha
    std::array<float, 4>    GravityDampWind { -9.81f, 0.996f, 0.0f, 0.0f };            // gravityY, damping, windX, windZ
    std::array<float, 4>    MembraneModuli  { 1600.0f, 1600.0f, 250.0f, 14000.0f };    // warp, weft, shear, wall
    std::array<float, 4>    CrimpCompShear  { 0.02f, 0.02f, 0.15f, 0.35f };            // warpCrimp, weftCrimp, compRatio, maxShearS
    std::array<float, 4>    BendFriction    { 0.12f, 0.45f, 0.002f, 6.0f };            // bendStiff, frictionMu, groundY, maxSpeed
    std::array<uint32_t, 4> CountsBatch     { 0u, 0u, 0u, 0u };                        // pointCount, batchStart, batchEnd, proxyCount
    std::array<uint32_t, 4> DualOffsets     { 0u, 0u, 0u, 0u };                        // faceCount, hingeDualOff, seamDualOff, distDualOff
    std::array<float, 4>    WindAeroFlags   { 0.0f, 0.35f, 0.01f, 0.0f };              // windEnabled, aeroDrag, contactRadius, windY
};
static_assert(sizeof(ParticleXpbdUniformBlock) == 128u, "ParticleXpbdUniformBlock must be 128 bytes.");

struct XpbdSimulationSettings
{
    uint32_t             Substeps       = 4u;
    uint32_t             Iterations     = 16u;
    float                GravityY       = -9.81f;
    float                Damping        = 0.996f;
    float                WarpModulus    = 1600.0f;
    float                WeftModulus    = 1600.0f;
    float                ShearModulus   = 250.0f;
    float                WallModulus    = 14000.0f;
    float                WarpCrimp      = 0.02f;
    float                WeftCrimp      = 0.02f;
    float                CompRatio      = 0.15f;
    float                MaxShearS      = 0.35f;
    float                BendStiffness  = 0.12f;
    float                FrictionMu     = 0.45f;
    float                GroundY        = 0.002f;
    float                MaxSpeed       = 6.0f;
    float                ZipAlpha       = 1.0f;
    float                ContactRadius  = 0.012f;
    bool                 WindEnabled    = false;
    std::array<float, 3> WindVelocity   { 0.0f, 0.0f, 0.0f };
    float                AeroDragRate   = 0.35f;
};

struct XpbdSimulationState
{
    std::vector<std::array<float, 3>> Positions     {};
    std::vector<std::array<float, 3>> PrevPositions {};
    std::vector<std::array<float, 3>> Velocities    {};
    std::vector<float>                DualLambdas   {};
    std::vector<uint8_t>              BrokenEdges   {}; // 1 when a tearable distance constraint snapped
};

//------------------------------------------------------------------------------------------------------------------------
//                                          BUILDERS, PACKER & CPU SOLVER
//------------------------------------------------------------------------------------------------------------------------

/// Deterministic greedy vertex-disjoint conflict graph coloring (PatternDraftNetwork.js::computeConflictColorBatches).
[[nodiscard]] ConflictColorBatchTable ComputeConflictColorBatches(
    uint32_t itemCount,
    uint32_t itemArity,
    std::span<const uint32_t> flatVertices,
    uint32_t totalPointCount);

/// Verifies that no two constraints in the same color batch share a vertex (guaranteed zero GPU race condition).
[[nodiscard]] bool VerifyConflictColorTable(
    const ConflictColorBatchTable& batches,
    uint32_t itemArity,
    std::span<const uint32_t> flatVertices) noexcept;

/// Builds an orthotropic triangulated ClothGrid (e.g. 16x16) with Dm^-1, quadratic hinges, and conflict-colored batches.
[[nodiscard]] XpbdConstraintNetwork BuildOrthotropicClothGridNetwork(
    uint32_t cols,
    uint32_t rows,
    float widthMeters,
    float heightMeters,
    float arealDensityKgM2 = 0.22f,
    bool pinTopRow = true,
    const std::array<float, 3>& originMeters = { 0.0f, 2.0f, 0.0f });

/// Builds a Spiderweb / CablesChains / FloraBed radial + ring distance strand network with conflict-colored edge batches.
[[nodiscard]] XpbdConstraintNetwork BuildStrandWebNetwork(
    uint32_t radialSpokes,
    uint32_t concentricRings,
    float radiusMeters,
    float edgeStiffness = 1200.0f,
    float breakRatio = 0.0f,
    bool pinOuterRim = true,
    const std::array<float, 3>& centerMeters = { 0.0f, 2.0f, 0.0f });

/// Packs XpbdConstraintNetwork into flat U32Topology & F32Rest tables matching WebGpuClothAccelerator.js::packGpuNetworkTables.
[[nodiscard]] XpbdPackedGpuTables PackXpbdGpuTables(const XpbdConstraintNetwork& network);

/// Initializes simulation positions/velocities/duals from `network`.
void InitializeXpbdSimulationState(const XpbdConstraintNetwork& network, XpbdSimulationState& state);

/// Executes one frame (Substeps x Iterations) of conflict-colored zero-atomic Orthotropic XPBD + ForceFields + Jolt RigidColliderProxy unilateral contact.
void StepParticleXpbdCpu(
    const XpbdConstraintNetwork& network,
    const XpbdSimulationSettings& settings,
    XpbdSimulationState& state,
    float frameDeltaTimeSeconds,
    float simulationTimeSeconds = 0.0f,
    std::span<const SimulationSpace::ForceFields::ForceModelGpuRecord> forceRecords = {},
    std::span<const SimulationSpace::PhysicsSpace::RigidColliderProxy> rigidProxies = {});

} // namespace Frontier::ParticleSpace
