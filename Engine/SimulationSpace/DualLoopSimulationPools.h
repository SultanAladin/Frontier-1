/*============================================================================================================================================
                                                   DUALLOOPSIMULATIONPOOLS.H
============================================================================================================================================*/
// 🧩 Merge-Prep Phase U04: Modular Contiguous Structure-of-Arrays (SoA) Simulation Pools + Tight Dual-Loop
//    Functions (`Execute<Subsystem>Loop(Pool, Params)`) with 0.00 ms Edit-mode/Idle Fast-Skip & Dual
//    `#if FRONTIER_PROFILE_EDITOR` Constructors/Destructors (ZERO Editor headers compiled in Shipping builds).
// Reference: InstanceAndDualLoopArchitecturePlan.md

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceModelTypes.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#ifndef FRONTIER_PROFILE_EDITOR
#define FRONTIER_PROFILE_EDITOR 0
#endif

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                            DUAL-LOOP EXECUTION PARAMETERS
//------------------------------------------------------------------------------------------------------------------------

struct DomainLoopParameters
{
    bool                 ShouldUpdate       = false;                   // [-] - False in Edit/Idle mode -> 0.00 ms fast-skip
    float                DeltaTimeSeconds   = 1.0f / 60.0f;            // [s] - Fixed simulation step
    float                SimulationTimeSec  = 0.0f;                    // [s] - Accumulated simulation clock
    std::array<float, 3> CameraWorldPos     { 0.0f, 2.0f, 6.0f };      // [m] - Viewer camera for LOD culling
    uint32_t             ActiveSubstepIndex = 0u;                      // [-] - Substep ordinal
};

enum class SimulationDomainOrdinal : uint8_t
{
    ForceField = 0u,
    Physics    = 1u,
    Trigger    = 2u,
    Fluid      = 3u,
    Particle   = 4u,
    Deformable = 5u,
};

//------------------------------------------------------------------------------------------------------------------------
//                      DUAL #if FRONTIER_PROFILE_EDITOR INSTANCE REGISTRATION HOOK (ZERO SHIPPING OVERHEAD)
//------------------------------------------------------------------------------------------------------------------------

#if FRONTIER_PROFILE_EDITOR
struct EditorSimulationRegistrationHooks
{
    void (*OnRegisterOutlinerRow)(SimulationDomainOrdinal domain, uint32_t slotIndex, const char* label)   = nullptr;
    void (*OnUnregisterOutlinerRow)(SimulationDomainOrdinal domain, uint32_t slotIndex)                    = nullptr;
    void (*OnRegisterInspectorGizmo)(SimulationDomainOrdinal domain, uint32_t slotIndex)                   = nullptr;
    void (*OnUnregisterInspectorGizmo)(SimulationDomainOrdinal domain, uint32_t slotIndex)                 = nullptr;
};

/// Installs/reads process-local Editor registration callbacks (compiled ONLY when FRONTIER_PROFILE_EDITOR == 1).
void AssignEditorSimulationHooks(const EditorSimulationRegistrationHooks& hooks) noexcept;
[[nodiscard]] const EditorSimulationRegistrationHooks& InspectEditorSimulationHooks() noexcept;
#endif

/// Lightweight RAII instance token with dual Editor/Shipping constructors & destructors.
/// In FRONTIER_PROFILE=Shipping (FRONTIER_PROFILE_EDITOR == 0), compiles to trivial POD with zero Editor symbols.
class SimulationDomainInstanceHandle
{
public:
    SimulationDomainInstanceHandle() noexcept = default;
    SimulationDomainInstanceHandle(SimulationDomainOrdinal domain, uint32_t slotIndex, const char* editorLabel = nullptr) noexcept;
    ~SimulationDomainInstanceHandle() noexcept;

    SimulationDomainInstanceHandle(const SimulationDomainInstanceHandle&)            = delete;
    SimulationDomainInstanceHandle& operator=(const SimulationDomainInstanceHandle&) = delete;

    SimulationDomainInstanceHandle(SimulationDomainInstanceHandle&& other) noexcept;
    SimulationDomainInstanceHandle& operator=(SimulationDomainInstanceHandle&& other) noexcept;

    [[nodiscard]] bool                    Valid() const noexcept { return Bound_; }
    [[nodiscard]] SimulationDomainOrdinal QueryDomain() const noexcept { return Domain_; }
    [[nodiscard]] uint32_t                QuerySlotIndex() const noexcept { return SlotIndex_; }

private:
    SimulationDomainOrdinal Domain_    = SimulationDomainOrdinal::ForceField;
    uint32_t                SlotIndex_ = 0u;
    bool                    Bound_     = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                    6 MODULAR CONTIGUOUS SoA SIMULATION DOMAIN POOLS
//------------------------------------------------------------------------------------------------------------------------

/// 1. ForceField contiguous SoA pool (tightly packed 3D origins, directions, radii, strengths & GPU records).
struct ForceFieldDomainPool
{
    uint32_t                                      ActiveCount = 0u;
    std::vector<std::array<float, 3>>             Origins     {};
    std::vector<std::array<float, 3>>             Directions  {};
    std::vector<float>                            Strengths   {};
    std::vector<float>                            Radii       {};
    std::vector<uint8_t>                          ModelCodes  {};
    std::vector<ForceFields::ForceModelGpuRecord> Evaluated   {};

    void Reserve(uint32_t capacity);
    [[nodiscard]] uint32_t Emplace(
        const std::array<float, 3>& origin,
        const std::array<float, 3>& direction,
        float strength,
        float radiusMeters,
        uint8_t modelCode = 0u);
    void Clear() noexcept;
};

/// 2. Physics rigid-body contiguous SoA pool (positions, linear velocities, inverse masses, accumulated impulses).
struct PhysicsDomainPool
{
    uint32_t                          ActiveCount   = 0u;
    std::vector<std::array<float, 3>> Positions     {};
    std::vector<std::array<float, 3>> Velocities    {};
    std::vector<std::array<float, 3>> PendingImpulses {};
    std::vector<float>                InverseMasses {};
    std::vector<float>                RadiiMeters   {};

    void Reserve(uint32_t capacity);
    [[nodiscard]] uint32_t Emplace(
        const std::array<float, 3>& position,
        const std::array<float, 3>& velocity,
        float inverseMass,
        float radiusMeters);
    void Clear() noexcept;
};

/// 3. Trigger overlap volume contiguous SoA pool (bounding sphere volumes + live overlap counts).
struct TriggerDomainPool
{
    uint32_t                          ActiveCount   = 0u;
    std::vector<std::array<float, 3>> Centres       {};
    std::vector<float>                RadiiMeters   {};
    std::vector<uint32_t>             OverlapCounts {};
    std::vector<uint8_t>              ActiveFlags   {};

    void Reserve(uint32_t capacity);
    [[nodiscard]] uint32_t Emplace(const std::array<float, 3>& centre, float radiusMeters);
    void Clear() noexcept;
};

/// 4. Fluid volume region contiguous SoA pool (volume centres, cell sizes, dissipation & GPU step counters).
struct FluidDomainPool
{
    uint32_t                          ActiveCount   = 0u;
    std::vector<std::array<float, 3>> VolumeCentres {};
    std::vector<float>                CellSizes     {};
    std::vector<float>                PeakDensities {};
    std::vector<uint64_t>             StepCounters  {};

    void Reserve(uint32_t capacity);
    [[nodiscard]] uint32_t Emplace(const std::array<float, 3>& centre, float cellSizeMeters);
    void Clear() noexcept;
};

/// 5. Particle cloud contiguous SoA pool (emitter origins, budgets, release rates & active counts).
struct ParticleDomainPool
{
    uint32_t                          ActiveCount    = 0u;
    std::vector<std::array<float, 3>> EmitterOrigins {};
    std::vector<uint32_t>             Budgets        {};
    std::vector<float>                ReleaseRates   {};
    std::vector<uint32_t>             LiveCounts     {};
    std::vector<uint64_t>             StepCounters   {};

    void Reserve(uint32_t capacity);
    [[nodiscard]] uint32_t Emplace(const std::array<float, 3>& origin, uint32_t budget, float releaseRate);
    void Clear() noexcept;
};

/// 6. Deformable body contiguous SoA pool (world origins, sleep radii, camera distances, activity tiers & step counts).
struct DeformableDomainPool
{
    uint32_t                          ActiveCount        = 0u;
    std::vector<std::array<float, 3>> WorldOrigins       {};
    std::vector<float>                SleepDistances     {};
    std::vector<float>                LastCameraDist     {};
    std::vector<uint8_t>              ActivityTiers      {}; // 0=Full, 1=Half, 2=Asleep
    std::vector<uint64_t>             StepCounters       {};

    void Reserve(uint32_t capacity);
    [[nodiscard]] uint32_t Emplace(const std::array<float, 3>& worldOrigin, float sleepDistanceMeters);
    void Clear() noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                           6 TIGHT DUAL-LOOP FUNCTIONS (0.00 ms FAST-SKIP, 0 HEAP ALLOCS)
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint32_t ExecuteForceFieldLoop(ForceFieldDomainPool& pool, const DomainLoopParameters& params) noexcept;
[[nodiscard]] uint32_t ExecutePhysicsLoop(PhysicsDomainPool& pool, const DomainLoopParameters& params) noexcept;
[[nodiscard]] uint32_t ExecuteTriggerLoop(TriggerDomainPool& pool, const PhysicsDomainPool& physicsPool, const DomainLoopParameters& params) noexcept;
[[nodiscard]] uint32_t ExecuteFluidLoop(FluidDomainPool& pool, const DomainLoopParameters& params) noexcept;
[[nodiscard]] uint32_t ExecuteParticleLoop(ParticleDomainPool& pool, const DomainLoopParameters& params) noexcept;
[[nodiscard]] uint32_t ExecuteDeformableLoop(DeformableDomainPool& pool, const DomainLoopParameters& params) noexcept;

} // namespace Frontier::SimulationSpace
