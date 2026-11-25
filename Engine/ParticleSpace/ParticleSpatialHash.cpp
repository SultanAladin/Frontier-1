/*============================================================================================================================================
                                                       PARTICLESPATIALHASH.CPP
============================================================================================================================================*/
// 🧩 Stage F (Slice S25) Uniform 3D spatial-hash 2-level Blelloch exclusive scan & 27-neighbor self-collision CPU mirror
// Reference: UnifiedFluidParticleForceFieldPortPlan.md §2.2A (Slice S25)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleSpatialHash.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier::ParticleSpace
{

namespace
{

[[nodiscard]] bool IsAliveCpu(const ParticleStateGpuRecord& rec, const float lifetime) noexcept
{
    return !(lifetime > 0.0f && rec.PositionAge[3] >= lifetime);
}

void BlellochScanBlock256(std::array<uint32_t, kBlellochBlockSize>& buf, uint32_t* outBlockTotal) noexcept
{
    // Up-sweep
    for (uint32_t stride = 1u; stride < kBlellochBlockSize; stride <<= 1u)
    {
        const uint32_t step = stride << 1u;
        for (uint32_t idx = step - 1u; idx < kBlellochBlockSize; idx += step)
        {
            buf[idx] += buf[idx - stride];
        }
    }

    if (outBlockTotal != nullptr)
    {
        *outBlockTotal = buf[kBlellochBlockSize - 1u];
    }
    buf[kBlellochBlockSize - 1u] = 0u;

    // Down-sweep
    for (uint32_t stride = kBlellochBlockSize >> 1u; stride >= 1u; stride >>= 1u)
    {
        const uint32_t step = stride << 1u;
        for (uint32_t idx = step - 1u; idx < kBlellochBlockSize; idx += step)
        {
            const uint32_t left = buf[idx - stride];
            buf[idx - stride]   = buf[idx];
            buf[idx]           += left;
        }
    }
}

} // namespace

uint32_t HashSpatialCell3D(const int32_t cx, const int32_t cy, const int32_t cz) noexcept
{
    const uint32_t hx = static_cast<uint32_t>(cx) * 73856093u;
    const uint32_t hy = static_cast<uint32_t>(cy) * 19349663u;
    const uint32_t hz = static_cast<uint32_t>(cz) * 83492791u;
    return (hx ^ hy ^ hz) & kSpatialHashBucketMask;
}

void RunBlellochPrefixSum65536Cpu(
    const std::span<const uint32_t> cellCounts,
    std::vector<uint32_t>& outOffsets,
    std::vector<uint32_t>& outBlockSums)
{
    outOffsets.assign(kSpatialHashBucketCount, 0u);
    outBlockSums.assign(kBlellochBlockCount, 0u);

    // 1. Local exclusive scan per 256-bucket workgroup
    std::array<uint32_t, kBlellochBlockSize> localBuf {};
    for (uint32_t block = 0u; block < kBlellochBlockCount; ++block)
    {
        const uint32_t base = block * kBlellochBlockSize;
        for (uint32_t t = 0u; t < kBlellochBlockSize; ++t)
        {
            const uint32_t gid = base + t;
            localBuf[t] = (gid < cellCounts.size()) ? cellCounts[gid] : 0u;
        }
        uint32_t blockTotal = 0u;
        BlellochScanBlock256(localBuf, &blockTotal);
        outBlockSums[block] = blockTotal;
        for (uint32_t t = 0u; t < kBlellochBlockSize; ++t)
        {
            outOffsets[base + t] = localBuf[t];
        }
    }

    // 2. Exclusive scan over the 256 block sums
    std::array<uint32_t, kBlellochBlockSize> blockBuf {};
    for (uint32_t b = 0u; b < kBlellochBlockCount; ++b)
    {
        blockBuf[b] = outBlockSums[b];
    }
    BlellochScanBlock256(blockBuf, nullptr);
    for (uint32_t b = 0u; b < kBlellochBlockCount; ++b)
    {
        outBlockSums[b] = blockBuf[b];
    }

    // 3. Add scanned block sum to every bucket offset
    for (uint32_t block = 0u; block < kBlellochBlockCount; ++block)
    {
        const uint32_t addVal = outBlockSums[block];
        const uint32_t base   = block * kBlellochBlockSize;
        for (uint32_t t = 0u; t < kBlellochBlockSize; ++t)
        {
            outOffsets[base + t] += addVal;
        }
    }
}

void BuildParticleSpatialHashCpu(
    const ParticleSpatialHashUniformBlock& uniforms,
    const std::span<const ParticleStateGpuRecord> readStates,
    ParticleSpatialHashBuffers& buffers)
{
    const uint32_t count = std::min(uniforms.CountsMode[0], static_cast<uint32_t>(readStates.size()));
    const float invCell  = 1.0f / std::max(uniforms.GridMaterial[0], 1.0e-4f);
    const float lifetime = uniforms.LifetimeBounds[0];

    buffers.CellCounts.assign(kSpatialHashBucketCount, 0u);
    buffers.ParticleBuckets.assign(count, 0xFFFFFFFFu);

    // CountCells
    for (uint32_t i = 0u; i < count; ++i)
    {
        const auto& rec = readStates[i];
        if (!IsAliveCpu(rec, lifetime)) continue;
        const int32_t cx = static_cast<int32_t>(std::floor(rec.PositionAge[0] * invCell));
        const int32_t cy = static_cast<int32_t>(std::floor(rec.PositionAge[1] * invCell));
        const int32_t cz = static_cast<int32_t>(std::floor(rec.PositionAge[2] * invCell));
        const uint32_t bucket = HashSpatialCell3D(cx, cy, cz);
        buffers.ParticleBuckets[i] = bucket;
        buffers.CellCounts[bucket] += 1u;
    }

    // Blelloch 2-level PrefixSum
    RunBlellochPrefixSum65536Cpu(buffers.CellCounts, buffers.CellOffsets, buffers.BlockSums);
    buffers.CellWriteOffsets = buffers.CellOffsets;

    // ScatterIndices
    buffers.SortedIndices.assign(count, 0u);
    for (uint32_t i = 0u; i < count; ++i)
    {
        const uint32_t bucket = buffers.ParticleBuckets[i];
        if (bucket >= kSpatialHashBucketCount) continue;
        const uint32_t slot = buffers.CellWriteOffsets[bucket]++;
        if (slot < buffers.SortedIndices.size())
        {
            buffers.SortedIndices[slot] = i;
        }
    }
}

void ResolveParticleSelfCollisionsCpu(
    const ParticleSpatialHashUniformBlock& uniforms,
    const std::span<const ParticleStateGpuRecord> readStates,
    ParticleSpatialHashBuffers& buffers)
{
    const uint32_t count = std::min(uniforms.CountsMode[0], static_cast<uint32_t>(readStates.size()));
    buffers.WriteStates.resize(readStates.size());

    const float cellSize   = std::max(uniforms.GridMaterial[0], 1.0e-4f);
    const float invCell    = 1.0f / cellSize;
    const float radius     = std::max(uniforms.GridMaterial[1], 1.0e-4f);
    const float minDist    = 2.0f * radius;
    const float minDistSq  = minDist * minDist;
    const float rest       = std::clamp(uniforms.GridMaterial[2], 0.0f, 1.0f);
    const float fric       = std::clamp(uniforms.GridMaterial[3], 0.0f, 2.0f);
    const float posRelax   = std::clamp(uniforms.SolverTuning[0], 0.05f, 1.8f);
    const float cohesion   = std::max(uniforms.SolverTuning[1], 0.0f);
    const float cohRange   = std::max(uniforms.SolverTuning[2], 0.0f);
    const float dt         = std::max(uniforms.SolverTuning[3], 1.0e-5f);
    const float pbdBias    = std::clamp(uniforms.LifetimeBounds[2], 0.0f, 1.0f);
    const float cohMaxDist = minDist + cohRange;
    const float cohMaxSq   = cohMaxDist * cohMaxDist;
    const float lifetime   = uniforms.LifetimeBounds[0];
    const uint32_t maxCell = std::max(uniforms.CountsMode[2], 1u);

    for (uint32_t idx = 0u; idx < count; ++idx)
    {
        ParticleStateGpuRecord self = readStates[idx];
        if (!IsAliveCpu(self, lifetime))
        {
            buffers.WriteStates[idx] = self;
            continue;
        }

        float px = self.PositionAge[0],  py = self.PositionAge[1],  pz = self.PositionAge[2];
        float vx = self.VelocitySeed[0], vy = self.VelocitySeed[1], vz = self.VelocitySeed[2];

        const int32_t bcx = static_cast<int32_t>(std::floor(px * invCell));
        const int32_t bcy = static_cast<int32_t>(std::floor(py * invCell));
        const int32_t bcz = static_cast<int32_t>(std::floor(pz * invCell));

        float corrX = 0.0f, corrY = 0.0f, corrZ = 0.0f;
        float dvX   = 0.0f, dvY   = 0.0f, dvZ   = 0.0f;
        float cohX  = 0.0f, cohY  = 0.0f, cohZ  = 0.0f;
        uint32_t contactCount = 0u;
        uint32_t impulseCount = 0u;

        for (int32_t dz = -1; dz <= 1; ++dz)
        {
            for (int32_t dy = -1; dy <= 1; ++dy)
            {
                for (int32_t dx = -1; dx <= 1; ++dx)
                {
                    const uint32_t bucket = HashSpatialCell3D(bcx + dx, bcy + dy, bcz + dz);
                    const uint32_t start  = buffers.CellOffsets[bucket];
                    const uint32_t nCnt   = std::min(buffers.CellCounts[bucket], maxCell);

                    for (uint32_t k = 0u; k < nCnt; ++k)
                    {
                        const uint32_t otherIdx = buffers.SortedIndices[start + k];
                        if (otherIdx == idx) continue;

                        const auto& other = readStates[otherIdx];
                        const float dX = px - other.PositionAge[0];
                        const float dY = py - other.PositionAge[1];
                        const float dZ = pz - other.PositionAge[2];
                        const float distSq = dX * dX + dY * dY + dZ * dZ;

                        if (distSq < minDistSq)
                        {
                            const float dist = std::sqrt(std::max(distSq, 1.0e-12f));
                            float nx, ny, nz;
                            if (dist > 1.0e-5f)
                            {
                                const float invD = 1.0f / dist;
                                nx = dX * invD; ny = dY * invD; nz = dZ * invD;
                            }
                            else
                            {
                                const float fx = static_cast<float>(static_cast<int32_t>(idx) - static_cast<int32_t>(otherIdx));
                                const float invL = 1.0f / std::sqrt(fx * fx + 1.0f + 0.37f * 0.37f);
                                nx = fx * invL; ny = 1.0f * invL; nz = 0.37f * invL;
                            }

                            const float overlap = minDist - dist;
                            corrX += nx * (0.5f * overlap);
                            corrY += ny * (0.5f * overlap);
                            corrZ += nz * (0.5f * overlap);
                            ++contactCount;

                            const float rvx = vx - other.VelocitySeed[0];
                            const float rvy = vy - other.VelocitySeed[1];
                            const float rvz = vz - other.VelocitySeed[2];
                            const float relVn = rvx * nx + rvy * ny + rvz * nz;
                            if (relVn < 0.0f)
                            {
                                const float normalDelta = -0.5f * (1.0f + rest) * relVn;
                                float pdvx = nx * normalDelta;
                                float pdvy = ny * normalDelta;
                                float pdvz = nz * normalDelta;

                                const float tvx = rvx - nx * relVn;
                                const float tvy = rvy - ny * relVn;
                                const float tvz = rvz - nz * relVn;
                                const float tanLenSq = tvx * tvx + tvy * tvy + tvz * tvz;
                                if (tanLenSq > 1.0e-10f && fric > 0.0f)
                                {
                                    const float tanLen = std::sqrt(tanLenSq);
                                    const float maxF   = std::min(0.5f * tanLen, fric * normalDelta);
                                    const float s      = maxF / tanLen;
                                    pdvx -= tvx * s;
                                    pdvy -= tvy * s;
                                    pdvz -= tvz * s;
                                }
                                dvX += pdvx; dvY += pdvy; dvZ += pdvz;
                                ++impulseCount;
                            }
                        }
                        else if (cohesion > 0.0f && cohRange > 0.0f && distSq < cohMaxSq)
                        {
                            const float dist = std::sqrt(distSq);
                            const float t = 1.0f - (dist - minDist) / cohRange;
                            const float s = (cohesion * t * dt) / dist;
                            cohX -= dX * s; cohY -= dY * s; cohZ -= dZ * s;
                        }
                    }
                }
            }
        }

        if (contactCount > 0u)
        {
            const float weight = (contactCount == 1u)
                ? std::min(posRelax, 1.0f)
                : std::min(1.0f, posRelax / std::max(1.0f, 0.5f * static_cast<float>(contactCount)));
            const float pdx = corrX * weight;
            const float pdy = corrY * weight;
            const float pdz = corrZ * weight;
            px += pdx; py += pdy; pz += pdz;
            if (pbdBias > 0.0f)
            {
                const float invDt = pbdBias / dt;
                vx += pdx * invDt;
                vy += pdy * invDt;
                vz += pdz * invDt;
            }
        }
        if (impulseCount > 0u)
        {
            const float invI = 1.0f / static_cast<float>(impulseCount);
            vx += dvX * invI;
            vy += dvY * invI;
            vz += dvZ * invI;
        }
        vx += cohX; vy += cohY; vz += cohZ;

        self.PositionAge[0]  = px; self.PositionAge[1]  = py; self.PositionAge[2]  = pz;
        self.VelocitySeed[0] = vx; self.VelocitySeed[1] = vy; self.VelocitySeed[2] = vz;
        buffers.WriteStates[idx] = self;
    }
}

void EnforceParticleBoxBoundsCpu(
    const std::span<ParticleStateGpuRecord> particles,
    const std::array<float, 3>& boxMinMeters,
    const std::array<float, 3>& boxMaxMeters,
    const float radiusMeters,
    const float restitution,
    const float friction) noexcept
{
    const float r = std::max(radiusMeters, 1.0e-4f);
    const float rest = std::clamp(restitution, 0.0f, 1.0f);
    const float fric = std::clamp(friction, 0.0f, 2.0f);

    for (auto& p : particles)
    {
        for (size_t axis = 0u; axis < 3u; ++axis)
        {
            const float lo = boxMinMeters[axis] + r;
            const float hi = boxMaxMeters[axis] - r;
            if (p.PositionAge[axis] < lo)
            {
                p.PositionAge[axis] = lo;
                if (p.VelocitySeed[axis] < 0.0f)
                {
                    const float normalDelta = -(1.0f + rest) * p.VelocitySeed[axis];
                    p.VelocitySeed[axis] += normalDelta;
                    const size_t a1 = (axis + 1u) % 3u;
                    const size_t a2 = (axis + 2u) % 3u;
                    const float tanLen = std::sqrt(p.VelocitySeed[a1] * p.VelocitySeed[a1] + p.VelocitySeed[a2] * p.VelocitySeed[a2]);
                    if (tanLen > 1.0e-6f && fric > 0.0f)
                    {
                        const float maxF = std::min(tanLen, fric * normalDelta);
                        p.VelocitySeed[a1] -= (p.VelocitySeed[a1] / tanLen) * maxF;
                        p.VelocitySeed[a2] -= (p.VelocitySeed[a2] / tanLen) * maxF;
                    }
                }
            }
            else if (p.PositionAge[axis] > hi)
            {
                p.PositionAge[axis] = hi;
                if (p.VelocitySeed[axis] > 0.0f)
                {
                    const float normalDelta = -(1.0f + rest) * p.VelocitySeed[axis];
                    p.VelocitySeed[axis] += normalDelta;
                    const size_t a1 = (axis + 1u) % 3u;
                    const size_t a2 = (axis + 2u) % 3u;
                    const float tanLen = std::sqrt(p.VelocitySeed[a1] * p.VelocitySeed[a1] + p.VelocitySeed[a2] * p.VelocitySeed[a2]);
                    if (tanLen > 1.0e-6f && fric > 0.0f)
                    {
                        const float maxF = std::min(tanLen, fric * std::fabs(normalDelta));
                        p.VelocitySeed[a1] -= (p.VelocitySeed[a1] / tanLen) * maxF;
                        p.VelocitySeed[a2] -= (p.VelocitySeed[a2] / tanLen) * maxF;
                    }
                }
            }
        }
    }
}

ParticleOverlapMetrics MeasureParticleOverlapMetricsCpu(
    const ParticleSpatialHashUniformBlock& uniforms,
    const std::span<const ParticleStateGpuRecord> states,
    ParticleSpatialHashBuffers& buffers)
{
    BuildParticleSpatialHashCpu(uniforms, states, buffers);

    ParticleOverlapMetrics m {};
    const uint32_t count = std::min(uniforms.CountsMode[0], static_cast<uint32_t>(states.size()));
    m.TotalParticles = count;

    const float invCell = 1.0f / std::max(uniforms.GridMaterial[0], 1.0e-4f);
    const float radius  = std::max(uniforms.GridMaterial[1], 1.0e-4f);
    const float diam    = 2.0f * radius;
    const float diamSq  = diam * diam;
    double sumPairRatio = 0.0;

    for (uint32_t idx = 0u; idx < count; ++idx)
    {
        const auto& self = states[idx];
        if (!std::isfinite(self.PositionAge[0]) || !std::isfinite(self.PositionAge[1]) || !std::isfinite(self.PositionAge[2])
         || !std::isfinite(self.VelocitySeed[0]) || !std::isfinite(self.VelocitySeed[1]) || !std::isfinite(self.VelocitySeed[2]))
        {
            ++m.NonFiniteCount;
            continue;
        }
        const int32_t bcx = static_cast<int32_t>(std::floor(self.PositionAge[0] * invCell));
        const int32_t bcy = static_cast<int32_t>(std::floor(self.PositionAge[1] * invCell));
        const int32_t bcz = static_cast<int32_t>(std::floor(self.PositionAge[2] * invCell));

        for (int32_t dz = -1; dz <= 1; ++dz)
        {
            for (int32_t dy = -1; dy <= 1; ++dy)
            {
                for (int32_t dx = -1; dx <= 1; ++dx)
                {
                    const uint32_t bucket = HashSpatialCell3D(bcx + dx, bcy + dy, bcz + dz);
                    const uint32_t start  = buffers.CellOffsets[bucket];
                    const uint32_t nCnt   = buffers.CellCounts[bucket];
                    for (uint32_t k = 0u; k < nCnt; ++k)
                    {
                        const uint32_t otherIdx = buffers.SortedIndices[start + k];
                        if (otherIdx <= idx) continue; // count each pair once
                        const auto& other = states[otherIdx];
                        const float dX = self.PositionAge[0] - other.PositionAge[0];
                        const float dY = self.PositionAge[1] - other.PositionAge[1];
                        const float dZ = self.PositionAge[2] - other.PositionAge[2];
                        const float distSq = dX * dX + dY * dY + dZ * dZ;
                        if (distSq < diamSq)
                        {
                            const float ratio = (diam - std::sqrt(std::max(distSq, 0.0f))) / diam;
                            ++m.OverlappingPairs;
                            m.MaxOverlapRatio = std::max(m.MaxOverlapRatio, ratio);
                            sumPairRatio += static_cast<double>(ratio);
                        }
                    }
                }
            }
        }
    }

    if (m.OverlappingPairs > 0u)
    {
        m.MeanOverlapRatio = static_cast<float>(sumPairRatio / static_cast<double>(m.OverlappingPairs));
    }
    if (count > 0u)
    {
        m.GlobalMeanOverlap = static_cast<float>(sumPairRatio / static_cast<double>(count));
    }
    return m;
}

} // namespace Frontier::ParticleSpace
