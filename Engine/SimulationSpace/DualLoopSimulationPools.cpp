/*============================================================================================================================================
                                                  DUALLOOPSIMULATIONPOOLS.CPP
============================================================================================================================================*/
// 🧩 Merge-Prep Phase U04: Contiguous SoA Simulation Pools + Tight Zero-Allocation Execute<Subsystem>Loop Implementations

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/DualLoopSimulationPools.h"

#include <cmath>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                        DUAL #if FRONTIER_PROFILE_EDITOR HOOKS
//------------------------------------------------------------------------------------------------------------------------

#if FRONTIER_PROFILE_EDITOR
namespace
{
EditorSimulationRegistrationHooks gEditorHooks {};
} // namespace

void AssignEditorSimulationHooks(const EditorSimulationRegistrationHooks& hooks) noexcept
{
    gEditorHooks = hooks;
}

const EditorSimulationRegistrationHooks& InspectEditorSimulationHooks() noexcept
{
    return gEditorHooks;
}
#endif

SimulationDomainInstanceHandle::SimulationDomainInstanceHandle(
    SimulationDomainOrdinal domain,
    uint32_t slotIndex,
    const char* editorLabel) noexcept
    : Domain_(domain)
    , SlotIndex_(slotIndex)
    , Bound_(true)
{
#if FRONTIER_PROFILE_EDITOR
    if (gEditorHooks.OnRegisterOutlinerRow != nullptr)
    {
        gEditorHooks.OnRegisterOutlinerRow(Domain_, SlotIndex_, editorLabel != nullptr ? editorLabel : "SimulationInstance");
    }
    if (gEditorHooks.OnRegisterInspectorGizmo != nullptr)
    {
        gEditorHooks.OnRegisterInspectorGizmo(Domain_, SlotIndex_);
    }
#else
    (void)editorLabel;
#endif
}

SimulationDomainInstanceHandle::~SimulationDomainInstanceHandle() noexcept
{
    if (!Bound_) return;
#if FRONTIER_PROFILE_EDITOR
    if (gEditorHooks.OnUnregisterInspectorGizmo != nullptr)
    {
        gEditorHooks.OnUnregisterInspectorGizmo(Domain_, SlotIndex_);
    }
    if (gEditorHooks.OnUnregisterOutlinerRow != nullptr)
    {
        gEditorHooks.OnUnregisterOutlinerRow(Domain_, SlotIndex_);
    }
#endif
    Bound_ = false;
}

SimulationDomainInstanceHandle::SimulationDomainInstanceHandle(SimulationDomainInstanceHandle&& other) noexcept
    : Domain_(other.Domain_)
    , SlotIndex_(other.SlotIndex_)
    , Bound_(other.Bound_)
{
    other.Bound_ = false;
}

SimulationDomainInstanceHandle& SimulationDomainInstanceHandle::operator=(SimulationDomainInstanceHandle&& other) noexcept
{
    if (this != &other)
    {
        if (Bound_)
        {
#if FRONTIER_PROFILE_EDITOR
            if (gEditorHooks.OnUnregisterInspectorGizmo != nullptr) gEditorHooks.OnUnregisterInspectorGizmo(Domain_, SlotIndex_);
            if (gEditorHooks.OnUnregisterOutlinerRow != nullptr) gEditorHooks.OnUnregisterOutlinerRow(Domain_, SlotIndex_);
#endif
        }
        Domain_      = other.Domain_;
        SlotIndex_   = other.SlotIndex_;
        Bound_       = other.Bound_;
        other.Bound_ = false;
    }
    return *this;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              CONTIGUOUS SoA POOL STORAGE
//------------------------------------------------------------------------------------------------------------------------

void ForceFieldDomainPool::Reserve(uint32_t capacity)
{
    Origins.reserve(capacity);
    Directions.reserve(capacity);
    Strengths.reserve(capacity);
    Radii.reserve(capacity);
    ModelCodes.reserve(capacity);
    Evaluated.reserve(capacity);
}

uint32_t ForceFieldDomainPool::Emplace(
    const std::array<float, 3>& origin,
    const std::array<float, 3>& direction,
    float strength,
    float radiusMeters,
    uint8_t modelCode)
{
    const uint32_t slot = ActiveCount++;
    Origins.push_back(origin);
    Directions.push_back(direction);
    Strengths.push_back(strength);
    Radii.push_back(radiusMeters);
    ModelCodes.push_back(modelCode);
    Evaluated.emplace_back();
    return slot;
}

void ForceFieldDomainPool::Clear() noexcept
{
    ActiveCount = 0u;
    Origins.clear();
    Directions.clear();
    Strengths.clear();
    Radii.clear();
    ModelCodes.clear();
    Evaluated.clear();
}

void PhysicsDomainPool::Reserve(uint32_t capacity)
{
    Positions.reserve(capacity);
    Velocities.reserve(capacity);
    PendingImpulses.reserve(capacity);
    InverseMasses.reserve(capacity);
    RadiiMeters.reserve(capacity);
}

uint32_t PhysicsDomainPool::Emplace(
    const std::array<float, 3>& position,
    const std::array<float, 3>& velocity,
    float inverseMass,
    float radiusMeters)
{
    const uint32_t slot = ActiveCount++;
    Positions.push_back(position);
    Velocities.push_back(velocity);
    PendingImpulses.push_back({ 0.0f, 0.0f, 0.0f });
    InverseMasses.push_back(inverseMass);
    RadiiMeters.push_back(radiusMeters);
    return slot;
}

void PhysicsDomainPool::Clear() noexcept
{
    ActiveCount = 0u;
    Positions.clear();
    Velocities.clear();
    PendingImpulses.clear();
    InverseMasses.clear();
    RadiiMeters.clear();
}

void TriggerDomainPool::Reserve(uint32_t capacity)
{
    Centres.reserve(capacity);
    RadiiMeters.reserve(capacity);
    OverlapCounts.reserve(capacity);
    ActiveFlags.reserve(capacity);
}

uint32_t TriggerDomainPool::Emplace(const std::array<float, 3>& centre, float radiusMeters)
{
    const uint32_t slot = ActiveCount++;
    Centres.push_back(centre);
    RadiiMeters.push_back(radiusMeters);
    OverlapCounts.push_back(0u);
    ActiveFlags.push_back(1u);
    return slot;
}

void TriggerDomainPool::Clear() noexcept
{
    ActiveCount = 0u;
    Centres.clear();
    RadiiMeters.clear();
    OverlapCounts.clear();
    ActiveFlags.clear();
}

void FluidDomainPool::Reserve(uint32_t capacity)
{
    VolumeCentres.reserve(capacity);
    CellSizes.reserve(capacity);
    PeakDensities.reserve(capacity);
    StepCounters.reserve(capacity);
}

uint32_t FluidDomainPool::Emplace(const std::array<float, 3>& centre, float cellSizeMeters)
{
    const uint32_t slot = ActiveCount++;
    VolumeCentres.push_back(centre);
    CellSizes.push_back(cellSizeMeters);
    PeakDensities.push_back(0.0f);
    StepCounters.push_back(0ull);
    return slot;
}

void FluidDomainPool::Clear() noexcept
{
    ActiveCount = 0u;
    VolumeCentres.clear();
    CellSizes.clear();
    PeakDensities.clear();
    StepCounters.clear();
}

void ParticleDomainPool::Reserve(uint32_t capacity)
{
    EmitterOrigins.reserve(capacity);
    Budgets.reserve(capacity);
    ReleaseRates.reserve(capacity);
    LiveCounts.reserve(capacity);
    StepCounters.reserve(capacity);
}

uint32_t ParticleDomainPool::Emplace(const std::array<float, 3>& origin, uint32_t budget, float releaseRate)
{
    const uint32_t slot = ActiveCount++;
    EmitterOrigins.push_back(origin);
    Budgets.push_back(budget);
    ReleaseRates.push_back(releaseRate);
    LiveCounts.push_back(0u);
    StepCounters.push_back(0ull);
    return slot;
}

void ParticleDomainPool::Clear() noexcept
{
    ActiveCount = 0u;
    EmitterOrigins.clear();
    Budgets.clear();
    ReleaseRates.clear();
    LiveCounts.clear();
    StepCounters.clear();
}

void DeformableDomainPool::Reserve(uint32_t capacity)
{
    WorldOrigins.reserve(capacity);
    SleepDistances.reserve(capacity);
    LastCameraDist.reserve(capacity);
    ActivityTiers.reserve(capacity);
    StepCounters.reserve(capacity);
}

uint32_t DeformableDomainPool::Emplace(const std::array<float, 3>& worldOrigin, float sleepDistanceMeters)
{
    const uint32_t slot = ActiveCount++;
    WorldOrigins.push_back(worldOrigin);
    SleepDistances.push_back(sleepDistanceMeters);
    LastCameraDist.push_back(0.0f);
    ActivityTiers.push_back(0u);
    StepCounters.push_back(0ull);
    return slot;
}

void DeformableDomainPool::Clear() noexcept
{
    ActiveCount = 0u;
    WorldOrigins.clear();
    SleepDistances.clear();
    LastCameraDist.clear();
    ActivityTiers.clear();
    StepCounters.clear();
}

//------------------------------------------------------------------------------------------------------------------------
//                              TIGHT DUAL-LOOP FUNCTIONS (0.00 ms FAST-SKIP, 0 HEAP ALLOCS)
//------------------------------------------------------------------------------------------------------------------------

uint32_t ExecuteForceFieldLoop(ForceFieldDomainPool& pool, const DomainLoopParameters& params) noexcept
{
    if (!params.ShouldUpdate || pool.ActiveCount == 0u) return 0u;

    for (uint32_t i = 0u; i < pool.ActiveCount; ++i)
    {
        auto& rec = pool.Evaluated[i];
        rec.PositionRadius[0]    = pool.Origins[i][0];
        rec.PositionRadius[1]    = pool.Origins[i][1];
        rec.PositionRadius[2]    = pool.Origins[i][2];
        rec.PositionRadius[3]    = pool.Radii[i];
        rec.DirectionStrength[0] = pool.Directions[i][0];
        rec.DirectionStrength[1] = pool.Directions[i][1];
        rec.DirectionStrength[2] = pool.Directions[i][2];
        rec.DirectionStrength[3] = pool.Strengths[i];
        rec.TypeFalloffParams[0] = static_cast<float>(pool.ModelCodes[i]);
        rec.TypeFalloffParams[1] = 0.0f;
        rec.TypeFalloffParams[2] = 0.0f;
        rec.TypeFalloffParams[3] = 0.0f;
    }
    return pool.ActiveCount;
}

uint32_t ExecutePhysicsLoop(PhysicsDomainPool& pool, const DomainLoopParameters& params) noexcept
{
    if (!params.ShouldUpdate || pool.ActiveCount == 0u) return 0u;

    const float dt = params.DeltaTimeSeconds;
    for (uint32_t i = 0u; i < pool.ActiveCount; ++i)
    {
        const float invM = pool.InverseMasses[i];
        if (invM > 0.0f)
        {
            pool.Velocities[i][0] += pool.PendingImpulses[i][0] * invM;
            pool.Velocities[i][1] += pool.PendingImpulses[i][1] * invM;
            pool.Velocities[i][2] += pool.PendingImpulses[i][2] * invM;
            pool.Positions[i][0]  += pool.Velocities[i][0] * dt;
            pool.Positions[i][1]  += pool.Velocities[i][1] * dt;
            pool.Positions[i][2]  += pool.Velocities[i][2] * dt;
        }
        pool.PendingImpulses[i] = { 0.0f, 0.0f, 0.0f };
    }
    return pool.ActiveCount;
}

uint32_t ExecuteTriggerLoop(
    TriggerDomainPool& pool,
    const PhysicsDomainPool& physicsPool,
    const DomainLoopParameters& params) noexcept
{
    if (!params.ShouldUpdate || pool.ActiveCount == 0u) return 0u;

    for (uint32_t t = 0u; t < pool.ActiveCount; ++t)
    {
        if (pool.ActiveFlags[t] == 0u)
        {
            pool.OverlapCounts[t] = 0u;
            continue;
        }
        const auto& tc = pool.Centres[t];
        const float tr = pool.RadiiMeters[t];
        uint32_t hits  = 0u;
        for (uint32_t p = 0u; p < physicsPool.ActiveCount; ++p)
        {
            const float dx = physicsPool.Positions[p][0] - tc[0];
            const float dy = physicsPool.Positions[p][1] - tc[1];
            const float dz = physicsPool.Positions[p][2] - tc[2];
            const float sumR = tr + physicsPool.RadiiMeters[p];
            if ((dx * dx + dy * dy + dz * dz) <= (sumR * sumR))
            {
                ++hits;
            }
        }
        pool.OverlapCounts[t] = hits;
    }
    return pool.ActiveCount;
}

uint32_t ExecuteFluidLoop(FluidDomainPool& pool, const DomainLoopParameters& params) noexcept
{
    if (!params.ShouldUpdate || pool.ActiveCount == 0u) return 0u;

    for (uint32_t i = 0u; i < pool.ActiveCount; ++i)
    {
        pool.StepCounters[i] += 1ull;
    }
    return pool.ActiveCount;
}

uint32_t ExecuteParticleLoop(ParticleDomainPool& pool, const DomainLoopParameters& params) noexcept
{
    if (!params.ShouldUpdate || pool.ActiveCount == 0u) return 0u;

    const float dt = params.DeltaTimeSeconds;
    for (uint32_t i = 0u; i < pool.ActiveCount; ++i)
    {
        const uint32_t spawned = static_cast<uint32_t>(std::ceil(pool.ReleaseRates[i] * dt));
        const uint32_t nextLive = pool.LiveCounts[i] + spawned;
        pool.LiveCounts[i]   = (nextLive <= pool.Budgets[i]) ? nextLive : pool.Budgets[i];
        pool.StepCounters[i] += 1ull;
    }
    return pool.ActiveCount;
}

uint32_t ExecuteDeformableLoop(DeformableDomainPool& pool, const DomainLoopParameters& params) noexcept
{
    if (!params.ShouldUpdate || pool.ActiveCount == 0u) return 0u;

    uint32_t stepped = 0u;
    for (uint32_t i = 0u; i < pool.ActiveCount; ++i)
    {
        const float dx = pool.WorldOrigins[i][0] - params.CameraWorldPos[0];
        const float dy = pool.WorldOrigins[i][1] - params.CameraWorldPos[1];
        const float dz = pool.WorldOrigins[i][2] - params.CameraWorldPos[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        pool.LastCameraDist[i] = dist;

        if (dist >= pool.SleepDistances[i])
        {
            pool.ActivityTiers[i] = 2u; // Asleep
            continue;
        }
        pool.ActivityTiers[i] = (dist >= 0.5f * pool.SleepDistances[i]) ? 1u : 0u;
        pool.StepCounters[i] += 1ull;
        ++stepped;
    }
    return stepped;
}

} // namespace Frontier::SimulationSpace
