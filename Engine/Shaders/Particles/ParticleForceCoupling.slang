/*============================================================================================================================================
                                                         PARTICLEFORCECOUPLING.SLANG
============================================================================================================================================*/
// 🧩 Dedicated Vulkan 1.3 compute pass coupling ForceFieldEvaluationShared 19 ForceModel laws & ambient wind onto live ParticleSlots

import ParticleBuffers;
#include "ForceFieldEvaluationShared.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 1)]] RWStructuredBuffer<ParticleSlotGpu>      ParticleSlots;
[[vk::binding(1, 1)]] ConstantBuffer<ParticleGlobalUniformGpu> GlobalParams;
[[vk::binding(2, 1)]] ConstantBuffer<ParticleRigUniformGpu>    RigParams;
[[vk::binding(3, 1)]] StructuredBuffer<ForceSourceShaderEntry> ForceSources;
[[vk::binding(4, 1)]] StructuredBuffer<float4>                 ForceWaypoints;

//------------------------------------------------------------------------------------------------------------------------
//                                                    COMPUTE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void ParticleForceCouplingEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint LocalIndex = DispatchThreadId.x;
    if (LocalIndex >= RigParams.SlotCapacity) return;

    uint SlotIdx = RigParams.SlotOffset + LocalIndex;
    ParticleSlotGpu Slot = ParticleSlots[SlotIdx];
    if ((Slot.PackedTraits & ParticleTraitAliveBit) == 0u) return;

    float Dt = clamp(GlobalParams.DeltaSeconds, 0.0, 0.05);
    float WindCouple = max(0.0, RigParams.WindCoupling);

    // 1. Ambient wind advection
    Slot.VelocityMps += (GlobalParams.AmbientWindVector.xyz - Slot.VelocityMps) * saturate(WindCouple * Dt * 0.65);

    // 2. Shared Vector Field Forces domain evaluation (zero duplicated force math!)
    uint SourceCount = GlobalParams.ActiveForceCount;
    for (uint S = 0u; S < SourceCount; ++S)
    {
        ForceSampleOutput Sample = VfEvaluateSingleSource(
            ForceSources[S],
            ForceWaypoints,
            Slot.PositionMeters,
            Slot.VelocityMps);

        Slot.VelocityMps += Sample.LinearAcceleration * Dt;
        Slot.VelocityMps += (Sample.AdvectionVelocity - Slot.VelocityMps) * saturate(WindCouple * Dt);
        Slot.SpinRadians += Sample.AngularTorque.y * Dt;
        Slot.VelocityMps *= exp(-max(0.0, Sample.DragDecayRate) * Dt);
    }

    ParticleSlots[SlotIdx] = Slot;
}
