/*============================================================================================================================================
                                                       PARTICLESPATIALGRIDCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 GPU spatial hash grid builder for O(N) Boids, SPH Fluid, Granular DEM, & Molecular Lennard-Jones neighbor lookups

import ParticleBuffers;

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleGridUniformGpu
{
    float4 DomainMinInvCell;      // xyz = domain min [m], w = 1 / cellSize
    uint   TableMask;             // power-of-two hash table mask (e.g. 65535)
    uint   SlotOffset;
    uint   SlotCapacity;
    uint   Reserved0;
};

struct ParticleCellKeyGpu
{
    uint CellHash;
    uint SlotIndex;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 1)]] StructuredBuffer<ParticleSlotGpu>       ParticleSlots;
[[vk::binding(1, 1)]] ConstantBuffer<ParticleGridUniformGpu>  GridParams;
[[vk::binding(2, 1)]] RWStructuredBuffer<uint>                CellCounts;
[[vk::binding(3, 1)]] RWStructuredBuffer<ParticleCellKeyGpu>  CellEntries;

//------------------------------------------------------------------------------------------------------------------------
//                                                    COMPUTE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

uint HashGridCoord(int3 C, uint Mask)
{
    uint H = (uint(C.x) * 73856093u) ^ (uint(C.y) * 19349663u) ^ (uint(C.z) * 83492791u);
    return H & Mask;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ParticleSpatialGridComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint LocalIdx = DispatchThreadId.x;
    if (LocalIdx >= GridParams.SlotCapacity) return;

    uint SlotIdx = GridParams.SlotOffset + LocalIdx;
    ParticleSlotGpu Slot = ParticleSlots[SlotIdx];

    ParticleCellKeyGpu Entry;
    Entry.SlotIndex = SlotIdx;
    if ((Slot.PackedTraits & ParticleTraitAliveBit) == 0u)
    {
        Entry.CellHash = 0xFFFFFFFFu;
        CellEntries[LocalIdx] = Entry;
        return;
    }

    float3 Rel = (Slot.PositionMeters - GridParams.DomainMinInvCell.xyz) * GridParams.DomainMinInvCell.w;
    int3   Cell = int3(floor(Rel));
    uint   Bucket = HashGridCoord(Cell, GridParams.TableMask);

    uint IgnoredPrev = 0u;
    InterlockedAdd(CellCounts[Bucket], 1u, IgnoredPrev);

    Entry.CellHash = Bucket;
    CellEntries[LocalIdx] = Entry;
}
