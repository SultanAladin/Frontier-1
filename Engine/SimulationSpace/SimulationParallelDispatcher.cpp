/*============================================================================================================================================
                                              SIMULATIONPARALLELDISPATCHER.CPP
============================================================================================================================================*/

#include "Engine/SimulationSpace/SimulationParallelDispatcher.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace
{

namespace
{

constexpr uint64_t kFnvOffset64 = 14695981039346656037ull;
constexpr uint64_t kFnvPrime64  = 1099511628211ull;

inline void HashBytes(uint64_t& h, const void* ptr, size_t bytes) noexcept
{
    const auto* p = static_cast<const uint8_t*>(ptr);
    for (size_t i = 0u; i < bytes; ++i)
    {
        h ^= static_cast<uint64_t>(p[i]);
        h *= kFnvPrime64;
    }
}

} // namespace

void SimulationParallelDispatcher::ExecuteSlice(const WorkSlice& s) noexcept
{
    const DomainLoopParameters& p = *s.Params;
    switch (s.PoolKind)
    {
        case 0u: // ForceFieldDomainPool (matches ExecuteForceFieldLoop bit-for-bit)
        {
            auto& pool = *s.ForcePool;
            for (uint32_t i = s.BeginIndex; i < s.EndIndex; ++i)
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
            break;
        }
        case 1u: // PhysicsDomainPool (matches ExecutePhysicsLoop bit-for-bit)
        {
            auto& pool = *s.PhysicsPool;
            const float dt = p.DeltaTimeSeconds;
            for (uint32_t i = s.BeginIndex; i < s.EndIndex; ++i)
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
            break;
        }
        case 2u: // TriggerDomainPool (matches ExecuteTriggerLoop bit-for-bit)
        {
            auto& pool       = *s.TriggerPool;
            const auto& phys = *s.PhysicsPool;
            for (uint32_t t = s.BeginIndex; t < s.EndIndex; ++t)
            {
                if (pool.ActiveFlags[t] == 0u)
                {
                    pool.OverlapCounts[t] = 0u;
                    continue;
                }
                const auto& tc = pool.Centres[t];
                const float tr = pool.RadiiMeters[t];
                uint32_t overlaps = 0u;
                for (uint32_t b = 0u; b < phys.ActiveCount; ++b)
                {
                    const float dx   = phys.Positions[b][0] - tc[0];
                    const float dy   = phys.Positions[b][1] - tc[1];
                    const float dz   = phys.Positions[b][2] - tc[2];
                    const float rSum = tr + phys.RadiiMeters[b];
                    if ((dx * dx + dy * dy + dz * dz) <= (rSum * rSum))
                    {
                        ++overlaps;
                    }
                }
                pool.OverlapCounts[t] = overlaps;
            }
            break;
        }
        case 3u: // DeformableDomainPool (matches ExecuteDeformableLoop bit-for-bit)
        {
            auto& pool = *s.DeformablePool;
            for (uint32_t i = s.BeginIndex; i < s.EndIndex; ++i)
            {
                const float dx = pool.WorldOrigins[i][0] - p.CameraWorldPos[0];
                const float dy = pool.WorldOrigins[i][1] - p.CameraWorldPos[1];
                const float dz = pool.WorldOrigins[i][2] - p.CameraWorldPos[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                pool.LastCameraDist[i] = dist;

                if (dist >= pool.SleepDistances[i])
                {
                    pool.ActivityTiers[i] = 2u;
                    continue;
                }
                pool.ActivityTiers[i] = (dist >= 0.5f * pool.SleepDistances[i]) ? 1u : 0u;
                pool.StepCounters[i] += 1ull;
            }
            break;
        }
        default:
            break;
    }
}

uint32_t SimulationParallelDispatcher::DispatchPoolSliced(
    Threading::ExecutionChannels* channels,
    uint32_t activeCount,
    uint8_t poolKind,
    ForceFieldDomainPool* forcePool,
    PhysicsDomainPool* physicsPool,
    TriggerDomainPool* triggerPool,
    DeformableDomainPool* deformablePool,
    const DomainLoopParameters& params) noexcept
{
    if (activeCount == 0u) return 0u;

    const bool useParallel = (channels != nullptr && channels->QueryActive() && activeCount >= kMinItemsPerBatch * 2u);
    if (!useParallel)
    {
        WorkSlice single {};
        single.BeginIndex     = 0u;
        single.EndIndex       = activeCount;
        single.PoolKind       = poolKind;
        single.Params         = &params;
        single.ForcePool      = forcePool;
        single.PhysicsPool    = physicsPool;
        single.TriggerPool    = triggerPool;
        single.DeformablePool = deformablePool;
        ExecuteSlice(single);
        return 1u;
    }

    const uint32_t desiredBatches = std::clamp(
        std::max(channels->QueryLaneCount() * 2u, 2u),
        2u,
        std::min(kMaxBatchesPerPool, activeCount / kMinItemsPerBatch));
    const uint32_t chunkSize = (activeCount + desiredBatches - 1u) / desiredBatches;

    uint32_t batchCount = 0u;
    for (uint32_t start = 0u; start < activeCount && batchCount < kMaxBatchesPerPool; start += chunkSize)
    {
        WorkSlice& s      = SliceStorage_[batchCount];
        s.BeginIndex      = start;
        s.EndIndex        = std::min(start + chunkSize, activeCount);
        s.PoolKind        = poolKind;
        s.Params          = &params;
        s.ForcePool       = forcePool;
        s.PhysicsPool     = physicsPool;
        s.TriggerPool     = triggerPool;
        s.DeformablePool  = deformablePool;
        ++batchCount;
    }

    BatchLatch_.Reset(batchCount);
    for (uint32_t b = 0u; b < batchCount; ++b)
    {
        const WorkSlice* slicePtr = &SliceStorage_[b];
        Threading::ScheduledTaskUnit unit {};
        unit.TaskIdentifier      = (static_cast<uint32_t>(poolKind) << 8u) | b;
        unit.PriorityTier        = Threading::TaskPriorityTier::DeterministicTick;
        unit.EstimatedDurationUs = 25ull;
        unit.CompletionTarget    = &BatchLatch_;
        unit.Callable            = [slicePtr]() noexcept { ExecuteSlice(*slicePtr); }; // Single pointer capture -> SBO (0 heap allocs)

        if (!channels->EnqueueTask(std::move(unit)))
        {
            // Fallback inline if channel ring saturated
            ExecuteSlice(*slicePtr);
            BatchLatch_.Decrement();
        }
    }

    channels->AssistUntilComplete(BatchLatch_);
    return batchCount;
}

ParallelDispatchReport SimulationParallelDispatcher::DispatchAllPools(
    Threading::ExecutionChannels* channels,
    ForceFieldDomainPool& forcePool,
    PhysicsDomainPool& physicsPool,
    TriggerDomainPool& triggerPool,
    DeformableDomainPool& deformablePool,
    const DomainLoopParameters& params) noexcept
{
    ParallelDispatchReport rep {};
    if (!params.ShouldUpdate) return rep;

    rep.UsedParallelLanes = (channels != nullptr && channels->QueryActive());

    // Deterministic barrier order: ForceFields -> Physics -> Triggers (reads Physics) -> Deformables
    rep.BatchesSubmitted += DispatchPoolSliced(
        channels, forcePool.ActiveCount, 0u, &forcePool, &physicsPool, &triggerPool, &deformablePool, params);
    rep.ForceFieldEvaluated = forcePool.ActiveCount;

    rep.BatchesSubmitted += DispatchPoolSliced(
        channels, physicsPool.ActiveCount, 1u, &forcePool, &physicsPool, &triggerPool, &deformablePool, params);
    rep.PhysicsStepped = physicsPool.ActiveCount;

    rep.BatchesSubmitted += DispatchPoolSliced(
        channels, triggerPool.ActiveCount, 2u, &forcePool, &physicsPool, &triggerPool, &deformablePool, params);
    rep.TriggersEvaluated = triggerPool.ActiveCount;

    rep.BatchesSubmitted += DispatchPoolSliced(
        channels, deformablePool.ActiveCount, 3u, &forcePool, &physicsPool, &triggerPool, &deformablePool, params);
    rep.DeformablesUpdated = deformablePool.ActiveCount;

    return rep;
}

uint64_t SimulationParallelDispatcher::ComputePoolsStateChecksum(
    const ForceFieldDomainPool& forcePool,
    const PhysicsDomainPool& physicsPool,
    const TriggerDomainPool& triggerPool,
    const DeformableDomainPool& deformablePool) noexcept
{
    uint64_t h = kFnvOffset64;
    for (uint32_t i = 0u; i < forcePool.ActiveCount; ++i)
    {
        HashBytes(h, forcePool.Evaluated[i].PositionRadius, sizeof(forcePool.Evaluated[i].PositionRadius));
        HashBytes(h, forcePool.Evaluated[i].DirectionStrength, sizeof(forcePool.Evaluated[i].DirectionStrength));
        HashBytes(h, forcePool.Evaluated[i].TypeFalloffParams, sizeof(forcePool.Evaluated[i].TypeFalloffParams));
    }
    for (uint32_t i = 0u; i < physicsPool.ActiveCount; ++i)
    {
        HashBytes(h, physicsPool.Positions[i].data(), sizeof(physicsPool.Positions[i]));
        HashBytes(h, physicsPool.Velocities[i].data(), sizeof(physicsPool.Velocities[i]));
    }
    for (uint32_t i = 0u; i < triggerPool.ActiveCount; ++i)
    {
        HashBytes(h, &triggerPool.OverlapCounts[i], sizeof(triggerPool.OverlapCounts[i]));
    }
    for (uint32_t i = 0u; i < deformablePool.ActiveCount; ++i)
    {
        HashBytes(h, &deformablePool.ActivityTiers[i], sizeof(deformablePool.ActivityTiers[i]));
        HashBytes(h, &deformablePool.StepCounters[i], sizeof(deformablePool.StepCounters[i]));
    }
    return h;
}

} // namespace Frontier::SimulationSpace
