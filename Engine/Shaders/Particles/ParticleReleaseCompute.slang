/*============================================================================================================================================
                                                         PARTICLERELEASECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 GPU deterministic PCG particle release compute kernel sampling analytic origins or pre-baked ParticleAnchorSampleGpu buffers

import ParticleBuffers;
import ColourRampSampling;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 1)]] RWStructuredBuffer<ParticleSlotGpu>        ParticleSlots;
[[vk::binding(1, 1)]] ConstantBuffer<ParticleGlobalUniformGpu>   GlobalParams;
[[vk::binding(2, 1)]] ConstantBuffer<ParticleRigUniformGpu>      RigParams;
[[vk::binding(3, 1)]] StructuredBuffer<ParticleAnchorSampleGpu>  AnchorSamples;
[[vk::binding(4, 1)]] StructuredBuffer<float4>                   ColourLutBuffer;

//------------------------------------------------------------------------------------------------------------------------
//                                                   DETERMINISTIC PCG HASH
//------------------------------------------------------------------------------------------------------------------------

uint PcgHash(uint State)
{
    uint Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

float PcgUnit(inout uint State)
{
    State = PcgHash(State * 747796405u + 2891336453u);
    return float(State >> 8u) * (1.0 / 16777216.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    COMPUTE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void ParticleReleaseComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint LocalIndex = DispatchThreadId.x;
    if (LocalIndex >= RigParams.SlotCapacity)
    {
        return;
    }

    uint SlotIdx = RigParams.SlotOffset + LocalIndex;
    ParticleSlotGpu Slot = ParticleSlots[SlotIdx];

    // Respawn only inactive slots deterministically budgeted by frame & release rate
    if ((Slot.PackedTraits & ParticleTraitAliveBit) != 0u)
    {
        return;
    }

    uint Rng = PcgHash(LocalIndex * 1973u + GlobalParams.FrameIndex * 9277u + RigParams.SlotOffset * 37u + 1337u);
    float SpawnProb = saturate((RigParams.ReleaseRatePerSec * max(GlobalParams.DeltaSeconds, 0.001)) / max(1.0, float(RigParams.SlotCapacity)));
    if (PcgUnit(Rng) > SpawnProb)
    {
        return;
    }

    // Select spawn point from AnchorSamples (surface/firework/structural) or analytic fallback
    float3 Pos = RigParams.OriginCenter;
    if (RigParams.AnchorBufferCount > 0u)
    {
        uint Pick = RigParams.AnchorBufferOffset + (PcgHash(Rng) % RigParams.AnchorBufferCount);
        Pos = AnchorSamples[Pick].PositionMeters;
    }
    else if (RigParams.ReleaseOriginCode == 1u) // SphereVolume
    {
        float U = PcgUnit(Rng) * 2.0 - 1.0;
        float Phi = PcgUnit(Rng) * 6.2831853;
        float Rad = pow(PcgUnit(Rng), 1.0 / 3.0) * RigParams.OriginRadius;
        float SinT = sqrt(max(0.0, 1.0 - U * U));
        Pos += float3(SinT * cos(Phi), U, SinT * sin(Phi)) * Rad;
    }
    else if (RigParams.ReleaseOriginCode == 3u) // BoxVolume
    {
        Pos += float3(
            (PcgUnit(Rng) * 2.0 - 1.0) * RigParams.OriginHalfExtents.x,
            (PcgUnit(Rng) * 2.0 - 1.0) * RigParams.OriginHalfExtents.y,
            (PcgUnit(Rng) * 2.0 - 1.0) * RigParams.OriginHalfExtents.z);
    }

    // Launch velocity cone around LaunchDirection
    float3 Axis = normalize(RigParams.LaunchDirection + float3(0.0, 1e-5, 0.0));
    float3 Ref  = (abs(Axis.y) < 0.92) ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 Tang = normalize(cross(Ref, Axis));
    float3 Bino = cross(Axis, Tang);

    float CosMax = cos(clamp(RigParams.SpreadRadians, 0.0, 3.14159265));
    float CosT   = lerp(CosMax, 1.0, PcgUnit(Rng));
    float SinT   = sqrt(max(0.0, 1.0 - CosT * CosT));
    float Az     = PcgUnit(Rng) * 6.2831853;
    float3 Dir   = normalize(Tang * (SinT * cos(Az)) + Bino * (SinT * sin(Az)) + Axis * CosT);
    float Speed  = lerp(RigParams.SpeedMinMps, max(RigParams.SpeedMinMps, RigParams.SpeedMaxMps), PcgUnit(Rng));

    Slot.PositionMeters  = Pos;
    Slot.OriginAnchor    = Pos;
    Slot.VelocityMps     = Dir * Speed;
    Slot.AgeSeconds      = 0.0;
    Slot.LifespanSeconds = max(0.05, lerp(RigParams.LifeMinSeconds, max(RigParams.LifeMinSeconds, RigParams.LifeMaxSeconds), PcgUnit(Rng)));
    Slot.SizeMeters      = RigParams.SizeBirthMeters * RigParams.SizeMultiplier;
    Slot.SeedScalar      = PcgUnit(Rng);
    Slot.SpinRadians     = PcgUnit(Rng) * 6.2831853;
    Slot.AuxScalar       = 0.0;
    Slot.PackedTraits    = ParticleTraitAliveBit;

    ColourRangeUniform RangeU;
    RangeU.RangeMin           = RigParams.ColourRangeMin;
    RangeU.RangeMax           = RigParams.ColourRangeMax;
    RangeU.RepeatCount        = RigParams.ColourRepeat;
    RangeU.PhaseOffset        = RigParams.ColourPhase;
    RangeU.WrapBehaviour      = RigParams.ColourWrapCode;
    RangeU.BlendCurve         = RigParams.ColourCurveCode;
    RangeU.InterpolationSpace = ColourSpaceOklab;
    RangeU.ReservedPad        = 0u;

    Slot.ColourLinear = SampleColourRampLut64(ColourLutBuffer, 0u, 0.0, RangeU);
    ParticleSlots[SlotIdx] = Slot;
}
