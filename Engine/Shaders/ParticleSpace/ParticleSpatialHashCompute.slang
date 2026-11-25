/*============================================================================================================================================
                                                  PARTICLESPATIALHASHCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Stage F (Slice S25) Uniform 3D Spatial-Hash Particle Self-Collision (65,536 buckets, 2-level Blelloch PrefixSum, 27-neighbor resolve)
// Reference: UnifiedFluidParticleForceFieldPortPlan.md §2.2A (Slice S25)

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS & ABI
//------------------------------------------------------------------------------------------------------------------------

static const uint kSpatialHashBucketCount = 65536u;
static const uint kSpatialHashBucketMask  = 65535u;
static const uint kBlellochBlockSize      = 256u;

struct ParticleStateRecord
{
    float4 PositionAge;  // xyz = position (m), w = age (s)
    float4 VelocitySeed; // xyz = velocity (m/s), w = seed [0..1]
};

/// Exact 64-byte uniform block shared with ParticleSpatialHash.h.
struct ParticleSpatialHashUniformBlock
{
    uint4  CountsMode;      // x = particleCount, y = bucketCount (65536), z = maxNeighborsPerCell (64), w = substeps
    float4 GridMaterial;    // x = cellSize (2*radius), y = particleRadius (m), z = restitution [0..1], w = friction [0..2]
    float4 SolverTuning;    // x = positionRelaxation [0.5..1.8], y = cohesionStrength, z = cohesionRange, w = deltaTime (s)
    float4 LifetimeBounds;  // x = lifetime (0 = Cloner), y = velocityDamping, z = pbdVelocityBias [0..1], w = pad
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    GPU BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]] ConstantBuffer<ParticleSpatialHashUniformBlock> U;
[[vk::binding(1, 0)]] StructuredBuffer<ParticleStateRecord>           ReadStates;
[[vk::binding(2, 0)]] RWStructuredBuffer<ParticleStateRecord>         WriteStates;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint>                        CellCounts;
[[vk::binding(4, 0)]] RWStructuredBuffer<uint>                        CellOffsets;
[[vk::binding(5, 0)]] RWStructuredBuffer<uint>                        BlockSums;
[[vk::binding(6, 0)]] RWStructuredBuffer<uint>                        CellWriteOffsets;
[[vk::binding(7, 0)]] RWStructuredBuffer<uint>                        ParticleBuckets;
[[vk::binding(8, 0)]] RWStructuredBuffer<uint>                        SortedIndices;

//------------------------------------------------------------------------------------------------------------------------
//                                                   HASHING HELPERS
//------------------------------------------------------------------------------------------------------------------------

int3 ComputeSpatialCell(float3 pos, float invCellSize)
{
    return int3(floor(pos * invCellSize));
}

uint HashSpatialCell(int3 cell)
{
    uint hx = asuint(cell.x) * 73856093u;
    uint hy = asuint(cell.y) * 19349663u;
    uint hz = asuint(cell.z) * 83492791u;
    return (hx ^ hy ^ hz) & kSpatialHashBucketMask;
}

bool IsParticleAlive(ParticleStateRecord rec, float lifetime)
{
    return !(lifetime > 0.0 && rec.PositionAge.w >= lifetime);
}

//------------------------------------------------------------------------------------------------------------------------
//                                            PASS 1: CLEAR & COUNT CELLS
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(256, 1, 1)]
void ClearBucketsComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint b = dispatchThreadId.x;
    if (b >= kSpatialHashBucketCount) return;
    CellCounts[b]       = 0u;
    CellOffsets[b]      = 0u;
    CellWriteOffsets[b] = 0u;
    if (b < kBlellochBlockSize)
    {
        BlockSums[b] = 0u;
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void CountCellsComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint idx = dispatchThreadId.x;
    if (idx >= U.CountsMode.x) return;

    ParticleStateRecord rec = ReadStates[idx];
    if (!IsParticleAlive(rec, U.LifetimeBounds.x))
    {
        ParticleBuckets[idx] = 0xFFFFFFFFu;
        return;
    }

    float invCellSize = 1.0 / max(U.GridMaterial.x, 1.0e-4);
    uint bucket = HashSpatialCell(ComputeSpatialCell(rec.PositionAge.xyz, invCellSize));
    ParticleBuckets[idx] = bucket;
    InterlockedAdd(CellCounts[bucket], 1u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                    PASS 2: BLELLOCH WORKGROUP PREFIX SUM (65,536)
//------------------------------------------------------------------------------------------------------------------------

groupshared uint SharedScan[kBlellochBlockSize];

[shader("compute")]
[numthreads(256, 1, 1)]
void PrefixSumLocalComputeEntry(
    uint3 groupThreadId : SV_GroupThreadID,
    uint3 groupId       : SV_GroupID)
{
    uint tid = groupThreadId.x;
    uint gid = groupId.x * kBlellochBlockSize + tid;
    SharedScan[tid] = (gid < kSpatialHashBucketCount) ? CellCounts[gid] : 0u;
    GroupMemoryBarrierWithGroupSync();

    // Up-sweep (reduction)
    for (uint stride = 1u; stride < kBlellochBlockSize; stride <<= 1u)
    {
        uint idx = (tid + 1u) * (stride << 1u) - 1u;
        if (idx < kBlellochBlockSize)
        {
            SharedScan[idx] += SharedScan[idx - stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (tid == kBlellochBlockSize - 1u)
    {
        BlockSums[groupId.x] = SharedScan[tid];
        SharedScan[tid] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    // Down-sweep (exclusive scan)
    for (uint stride = kBlellochBlockSize >> 1u; stride >= 1u; stride >>= 1u)
    {
        uint idx = (tid + 1u) * (stride << 1u) - 1u;
        if (idx < kBlellochBlockSize)
        {
            uint left = SharedScan[idx - stride];
            SharedScan[idx - stride] = SharedScan[idx];
            SharedScan[idx] += left;
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (gid < kSpatialHashBucketCount)
    {
        CellOffsets[gid] = SharedScan[tid];
    }
}

[shader("compute")]
[numthreads(256, 1, 1)]
void PrefixSumBlocksComputeEntry(uint3 groupThreadId : SV_GroupThreadID)
{
    uint tid = groupThreadId.x;
    SharedScan[tid] = BlockSums[tid];
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = 1u; stride < kBlellochBlockSize; stride <<= 1u)
    {
        uint idx = (tid + 1u) * (stride << 1u) - 1u;
        if (idx < kBlellochBlockSize)
        {
            SharedScan[idx] += SharedScan[idx - stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (tid == kBlellochBlockSize - 1u)
    {
        SharedScan[tid] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = kBlellochBlockSize >> 1u; stride >= 1u; stride >>= 1u)
    {
        uint idx = (tid + 1u) * (stride << 1u) - 1u;
        if (idx < kBlellochBlockSize)
        {
            uint left = SharedScan[idx - stride];
            SharedScan[idx - stride] = SharedScan[idx];
            SharedScan[idx] += left;
        }
        GroupMemoryBarrierWithGroupSync();
    }

    BlockSums[tid] = SharedScan[tid];
}

[shader("compute")]
[numthreads(256, 1, 1)]
void PrefixSumAddBlocksComputeEntry(
    uint3 groupThreadId : SV_GroupThreadID,
    uint3 groupId       : SV_GroupID)
{
    uint gid = groupId.x * kBlellochBlockSize + groupThreadId.x;
    if (gid >= kSpatialHashBucketCount) return;
    uint totalOffset = CellOffsets[gid] + BlockSums[groupId.x];
    CellOffsets[gid]      = totalOffset;
    CellWriteOffsets[gid] = totalOffset;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          PASS 3: SCATTER SORTED INDICES
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void ScatterIndicesComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint idx = dispatchThreadId.x;
    if (idx >= U.CountsMode.x) return;
    uint bucket = ParticleBuckets[idx];
    if (bucket >= kSpatialHashBucketCount) return;

    uint dstSlot = 0u;
    InterlockedAdd(CellWriteOffsets[bucket], 1u, dstSlot);
    SortedIndices[dstSlot] = idx;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      PASS 4: RESOLVE 3x3x3 = 27 NEIGHBOR CELLS
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void ResolveNeighborsComputeEntry(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint idx = dispatchThreadId.x;
    if (idx >= U.CountsMode.x) return;

    ParticleStateRecord self = ReadStates[idx];
    if (!IsParticleAlive(self, U.LifetimeBounds.x))
    {
        WriteStates[idx] = self;
        return;
    }

    float3 pos = self.PositionAge.xyz;
    float3 vel = self.VelocitySeed.xyz;
    float  cellSize   = max(U.GridMaterial.x, 1.0e-4);
    float  invCell    = 1.0 / cellSize;
    float  radius     = max(U.GridMaterial.y, 1.0e-4);
    float  minDist    = 2.0 * radius;
    float  minDistSq  = minDist * minDist;
    float  rest       = clamp(U.GridMaterial.z, 0.0, 1.0);
    float  fric       = clamp(U.GridMaterial.w, 0.0, 2.0);
    float  posRelax   = clamp(U.SolverTuning.x, 0.05, 1.8);
    float  cohesion   = max(U.SolverTuning.y, 0.0);
    float  cohRange   = max(U.SolverTuning.z, 0.0);
    float  dt         = max(U.SolverTuning.w, 1.0e-5);
    float  pbdBias    = clamp(U.LifetimeBounds.z, 0.0, 1.0);
    float  cohMaxDist = minDist + cohRange;
    uint   maxPerCell = max(U.CountsMode.z, 1u);

    int3 baseCell = ComputeSpatialCell(pos, invCell);

    float3 posCorrection = float3(0.0, 0.0, 0.0);
    float3 velDelta      = float3(0.0, 0.0, 0.0);
    float3 cohDelta      = float3(0.0, 0.0, 0.0);
    uint   contactCount  = 0u;
    uint   impulseCount  = 0u;

    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                int3 nCell = baseCell + int3(dx, dy, dz);
                uint bucket = HashSpatialCell(nCell);
                uint start  = CellOffsets[bucket];
                uint count  = min(CellCounts[bucket], maxPerCell);

                for (uint k = 0u; k < count; ++k)
                {
                    uint otherIdx = SortedIndices[start + k];
                    if (otherIdx == idx) continue;

                    ParticleStateRecord other = ReadStates[otherIdx];
                    float3 diff   = pos - other.PositionAge.xyz;
                    float  distSq = dot(diff, diff);

                    if (distSq < minDistSq)
                    {
                        float  dist = sqrt(max(distSq, 1.0e-12));
                        float3 n    = (dist > 1.0e-5)
                            ? (diff / dist)
                            : normalize(float3(float(int(idx) - int(otherIdx)), 1.0, 0.37));
                        float overlap = minDist - dist;

                        // Symmetric equal-mass separation (1/2 overlap per particle)
                        posCorrection += n * (0.5 * overlap);
                        contactCount  += 1u;

                        // Equal-mass restitution + Coulomb friction impulse
                        float3 relV = vel - other.VelocitySeed.xyz;
                        float  relVn = dot(relV, n);
                        if (relVn < 0.0)
                        {
                            float normalDelta = -0.5 * (1.0 + rest) * relVn;
                            float3 pairDeltaV = n * normalDelta;

                            float3 tanV = relV - n * relVn;
                            float tanLenSq = dot(tanV, tanV);
                            if (tanLenSq > 1.0e-10 && fric > 0.0)
                            {
                                float tanLen = sqrt(tanLenSq);
                                float maxF   = min(0.5 * tanLen, fric * normalDelta);
                                pairDeltaV  -= (tanV / tanLen) * maxF;
                            }
                            velDelta     += pairDeltaV;
                            impulseCount += 1u;
                        }
                    }
                    else if (cohesion > 0.0 && cohRange > 0.0 && distSq < cohMaxDist * cohMaxDist)
                    {
                        float dist = sqrt(distSq);
                        float t = 1.0 - (dist - minDist) / cohRange;
                        float3 n = diff / dist;
                        cohDelta -= n * (cohesion * t * dt);
                    }
                }
            }
        }
    }

    if (contactCount > 0u)
    {
        float weight = (contactCount == 1u)
            ? min(posRelax, 1.0)
            : min(1.0, posRelax / max(1.0, 0.5 * float(contactCount)));
        float3 posDelta = posCorrection * weight;
        pos += posDelta;
        if (pbdBias > 0.0)
        {
            vel += posDelta * (pbdBias / dt);
        }
    }
    if (impulseCount > 0u)
    {
        vel += velDelta / float(impulseCount);
    }
    vel += cohDelta;

    self.PositionAge.xyz  = pos;
    self.VelocitySeed.xyz = vel;
    WriteStates[idx] = self;
}
