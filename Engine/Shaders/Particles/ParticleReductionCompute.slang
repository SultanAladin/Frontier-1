/*============================================================================================================================================
                                                        PARTICLEREDUCTIONCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 GPU telemetry reduction compute kernel aggregating live count, fixed-point AABB bounds & kinetic energy per rig

import ParticleBuffers;

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleMetricsReductionGpu
{
    uint LiveCount;
    uint SumSpeedMilli;         // sum of |v| in mm/s
    uint MaxSpeedMilli;         // max |v| in mm/s
    uint SumKineticMilli;       // sum of 0.5*|v|^2 in mJ/kg
    int  MinXmm;
    int  MinYmm;
    int  MinZmm;
    int  MaxXmm;
    int  MaxYmm;
    int  MaxZmm;
    uint SpeciesACount;
    uint SpeciesBCount;
    uint SpeciesCCount;
    uint Reserved0;
    uint Reserved1;
    uint Reserved2;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 1)]] StructuredBuffer<ParticleSlotGpu>             ParticleSlots;
[[vk::binding(1, 1)]] ConstantBuffer<ParticleRigUniformGpu>         RigParams;
[[vk::binding(2, 1)]] RWStructuredBuffer<ParticleMetricsReductionGpu> MetricsOut;

//------------------------------------------------------------------------------------------------------------------------
//                                                    COMPUTE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void ParticleReductionComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint LocalIdx = DispatchThreadId.x;
    if (LocalIdx >= RigParams.SlotCapacity) return;

    uint SlotIdx = RigParams.SlotOffset + LocalIdx;
    ParticleSlotGpu Slot = ParticleSlots[SlotIdx];
    if ((Slot.PackedTraits & ParticleTraitAliveBit) == 0u) return;

    float Speed = length(Slot.VelocityMps);
    uint  SpeedMm = uint(clamp(Speed * 1000.0, 0.0, 2000000.0));
    uint  KinMm   = uint(clamp(0.5 * Speed * Speed * 1000.0, 0.0, 2000000.0));

    int3 PosMm = int3(clamp(Slot.PositionMeters * 1000.0, float3(-1000000.0), float3(1000000.0)));

    uint Prev = 0u;
    int  PrevI = 0;
    InterlockedAdd(MetricsOut[0].LiveCount, 1u, Prev);
    InterlockedAdd(MetricsOut[0].SumSpeedMilli, SpeedMm, Prev);
    InterlockedMax(MetricsOut[0].MaxSpeedMilli, SpeedMm, Prev);
    InterlockedAdd(MetricsOut[0].SumKineticMilli, KinMm, Prev);

    InterlockedMin(MetricsOut[0].MinXmm, PosMm.x, PrevI);
    InterlockedMin(MetricsOut[0].MinYmm, PosMm.y, PrevI);
    InterlockedMin(MetricsOut[0].MinZmm, PosMm.z, PrevI);
    InterlockedMax(MetricsOut[0].MaxXmm, PosMm.x, PrevI);
    InterlockedMax(MetricsOut[0].MaxYmm, PosMm.y, PrevI);
    InterlockedMax(MetricsOut[0].MaxZmm, PosMm.z, PrevI);

    uint Sp = (Slot.PackedTraits >> 16u) & 3u;
    if (Sp == 0u)      InterlockedAdd(MetricsOut[0].SpeciesACount, 1u, Prev);
    else if (Sp == 1u) InterlockedAdd(MetricsOut[0].SpeciesBCount, 1u, Prev);
    else               InterlockedAdd(MetricsOut[0].SpeciesCCount, 1u, Prev);
}
