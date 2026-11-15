/*============================================================================================================================================
                                                         PARTICLEADVECTCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Vulkan 1.3 particle advection compute kernel importing ColourRampSampling & ForceFieldEvaluationShared (zero duplicated physics/colour)

import ParticleBuffers;
import ColourRampSampling;
#include "ForceFieldEvaluationShared.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR BINDINGS
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 1)]] RWStructuredBuffer<ParticleSlotGpu>        ParticleSlots;
[[vk::binding(1, 1)]] ConstantBuffer<ParticleGlobalUniformGpu>   GlobalParams;
[[vk::binding(2, 1)]] ConstantBuffer<ParticleRigUniformGpu>      RigParams;
[[vk::binding(3, 1)]] StructuredBuffer<ForceSourceShaderEntry>   ForceSources;
[[vk::binding(4, 1)]] StructuredBuffer<float4>                   ForceWaypoints;
[[vk::binding(5, 1)]] StructuredBuffer<float4>                   ColourLutBuffer;
[[vk::binding(6, 1)]] StructuredBuffer<RigidColliderProxyGpu>    RigidColliders;

//------------------------------------------------------------------------------------------------------------------------
//                                                       HELPER FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float3 RotateByQuatGpu(float4 Quat, float3 Vector)
{
    float3 T = 2.0 * cross(Quat.xyz, Vector);
    return Vector + Quat.w * T + cross(Quat.xyz, T);
}

float3 RotateByConjugateQuatGpu(float4 Quat, float3 Vector)
{
    return RotateByQuatGpu(float4(-Quat.xyz, Quat.w), Vector);
}

float3 EvaluateAnalyticCurlVelocity(float3 P, float Freq, float Phase)
{
    float Sx = P.x * Freq + Phase;
    float Sy = P.y * Freq - Phase * 0.7;
    float Sz = P.z * Freq + Phase * 0.5;
    return float3(
        cos(Sx) * sin(Sy) - sin(Sz) * cos(Sx) * 0.5,
        cos(Sy) * sin(Sz) - sin(Sx) * cos(Sy) * 0.5,
        cos(Sz) * sin(Sx) - sin(Sy) * cos(Sz) * 0.5);
}

float EvaluateParticleScalarChannel(uint ChannelCode, ParticleSlotGpu Slot, ParticleRigUniformGpu Rig, float ElapsedSec)
{
    float NormLife = saturate(Slot.AgeSeconds / max(1e-5, Slot.LifespanSeconds));
    float Speed    = length(Slot.VelocityMps);
    float Dist     = length(Slot.PositionMeters - Slot.OriginAnchor);
    float RawVal   = NormLife;

    if      (ChannelCode == 0u)  RawVal = NormLife;
    else if (ChannelCode == 1u)  RawVal = Speed;
    else if (ChannelCode == 2u)  RawVal = Slot.PositionMeters.y;
    else if (ChannelCode == 3u)  RawVal = Dist;
    else if (ChannelCode == 4u)  RawVal = Slot.SeedScalar;
    else if (ChannelCode == 5u)  RawVal = Slot.VelocityMps.y;
    else if (ChannelCode == 6u)  RawVal = Slot.PositionMeters.x;
    else if (ChannelCode == 7u)  RawVal = Slot.PositionMeters.z;
    else if (ChannelCode == 8u)  RawVal = Slot.AuxScalar;
    else if (ChannelCode == 9u)  RawVal = (atan2(Slot.PositionMeters.z, Slot.PositionMeters.x) + VfPi) / VfTau;
    else if (ChannelCode == 10u) RawVal = abs(Slot.VelocityMps.y) / max(0.01, Speed);
    else if (ChannelCode == 11u) RawVal = Slot.SizeMeters;
    else if (ChannelCode == 12u) RawVal = frac(Slot.SpinRadians / VfTau);
    else if (ChannelCode == 13u) RawVal = Slot.AgeSeconds;
    else if (ChannelCode == 14u) RawVal = frac(ElapsedSec * 0.15);

    float Span = max(1e-5, Rig.ColourRangeMax - Rig.ColourRangeMin);
    return (RawVal - Rig.ColourRangeMin) / Span;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    COMPUTE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void ParticleAdvectComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint LocalIndex = DispatchThreadId.x;
    if (LocalIndex >= RigParams.SlotCapacity)
    {
        return;
    }

    uint SlotIdx = RigParams.SlotOffset + LocalIndex;
    ParticleSlotGpu Slot = ParticleSlots[SlotIdx];

    if ((Slot.PackedTraits & ParticleTraitAliveBit) == 0u)
    {
        return;
    }

    float Dt = clamp(GlobalParams.DeltaSeconds, 0.0, 0.05);
    Slot.AgeSeconds += Dt;
    if (Slot.AgeSeconds >= Slot.LifespanSeconds)
    {
        Slot.PackedTraits &= ~ParticleTraitAliveBit;
        ParticleSlots[SlotIdx] = Slot;
        return;
    }

    // 1. Evaluate all active Vector Field ForceSources directly via ForceFieldEvaluationShared.slang
    float3 ForceAccel   = float3(0.0, 0.0, 0.0);
    float3 ForceFlowVel = float3(0.0, 0.0, 0.0);
    float  ForceDrag    = 0.0;

    for (uint FIdx = 0u; FIdx < GlobalParams.ActiveForceCount; ++FIdx)
    {
        ForceSampleOutput Sample = VfEvaluateSingleSource(
            ForceSources[FIdx],
            ForceWaypoints,
            Slot.PositionMeters,
            Slot.VelocityMps);
        ForceAccel   += Sample.LinearAcceleration;
        ForceFlowVel += Sample.AdvectionVelocity;
        ForceDrag    += Sample.DragDecayRate;
    }

    // 2. Gravity, buoyancy, and direct ForceFields body acceleration
    float GravityY = -9.81 * RigParams.GravityScale + RigParams.BuoyancyMps2;
    Slot.VelocityMps += (ForceAccel + float3(0.0, GravityY, 0.0)) * Dt;

    // 3. Ambient wind + ForceFields velocity flow relaxation
    if (RigParams.WindCoupling > 1e-4)
    {
        float  Blend  = 1.0 - exp(-RigParams.WindCoupling * Dt);
        float3 Target = GlobalParams.AmbientWindVector.xyz + ForceFlowVel;
        Slot.VelocityMps += (Target - Slot.VelocityMps) * Blend;
    }

    // 4. Optional analytic curl manifold drive
    if ((RigParams.DynamicsBitmask & ParticleDynCurlBit) != 0u)
    {
        float3 CurlV = EvaluateAnalyticCurlVelocity(
            Slot.PositionMeters,
            RigParams.CurlFrequency,
            GlobalParams.ElapsedSeconds * RigParams.CurlEvolutionRate);
        Slot.VelocityMps += CurlV * (RigParams.CurlAmplitude * Dt);
    }

    // 5. Aerodynamic drag + ForceFields damping
    float TotalDamp = exp(-max(0.0, RigParams.AerodynamicDrag + ForceDrag) * Dt);
    Slot.VelocityMps *= TotalDamp;

    // 6. Position advance
    Slot.PositionMeters += Slot.VelocityMps * Dt;

    // 7. Floor bounce
    if (RigParams.FloorBounce > 0.0 && Slot.PositionMeters.y < 0.0)
    {
        Slot.PositionMeters.y = 0.0;
        if (Slot.VelocityMps.y < 0.0)
        {
            Slot.VelocityMps.y = -Slot.VelocityMps.y * RigParams.FloorBounce;
            Slot.VelocityMps.xz *= 0.88;
        }
    }

    // 8. Interactive chrome sphere collider
    if ((GlobalParams.ColliderFlags & 1u) != 0u)
    {
        float3 Delta  = Slot.PositionMeters - GlobalParams.ColliderBallCenterM;
        float  DistSq = dot(Delta, Delta);
        float  MinR   = GlobalParams.ColliderBallRadiusM + Slot.SizeMeters;
        if (DistSq > 1e-8 && DistSq < MinR * MinR)
        {
            float  Dist = sqrt(DistSq);
            float3 Nrm  = Delta / Dist;
            Slot.PositionMeters = GlobalParams.ColliderBallCenterM + Nrm * MinR;
            float RelVn = dot(Slot.VelocityMps - GlobalParams.ColliderBallVelocity, Nrm);
            if (RelVn < 0.0)
            {
                Slot.VelocityMps += Nrm * (-1.75 * RelVn);
            }
        }
    }

    // 8b. PhysicsSpace rigid-body collision proxies (Sphere, Oriented Box, Oriented +Z Capsule)
    uint RigidColliderCount = (GlobalParams.ColliderFlags >> 16u) & 0xFFFFu;
    for (uint CIdx = 0u; CIdx < RigidColliderCount; ++CIdx)
    {
        RigidColliderProxyGpu Proxy = RigidColliders[CIdx];
        float  SafeParticleR = max(Slot.SizeMeters, 1e-4);
        float3 RelLocal      = RotateByConjugateQuatGpu(Proxy.OrientationQuat, Slot.PositionMeters - Proxy.CenterMeters);
        float3 LocalNormal   = float3(0.0, 0.0, 1.0);
        float  Penetration   = 0.0;
        bool   Hit           = false;

        if (Proxy.ShapeCode == 0u)
        {
            float MinDist = max(Proxy.RadiusMeters, 1e-4) + SafeParticleR;
            float DistSq  = dot(RelLocal, RelLocal);
            if (DistSq < MinDist * MinDist)
            {
                float Dist = sqrt(max(DistSq, 1e-12));
                LocalNormal = (Dist > 1e-5) ? (RelLocal / Dist) : float3(0.0, 0.0, 1.0);
                Penetration = MinDist - Dist;
                Hit = true;
            }
        }
        else if (Proxy.ShapeCode == 1u)
        {
            float3 HalfE   = max(Proxy.HalfExtentsMeters, float3(1e-4, 1e-4, 1e-4));
            float3 Closest = clamp(RelLocal, -HalfE, HalfE);
            float3 Diff    = RelLocal - Closest;
            float  DistSq  = dot(Diff, Diff);
            if (DistSq > 1e-10)
            {
                if (DistSq < SafeParticleR * SafeParticleR)
                {
                    float Dist = sqrt(DistSq);
                    LocalNormal = Diff / Dist;
                    Penetration = SafeParticleR - Dist;
                    Hit = true;
                }
            }
            else
            {
                float Dx = HalfE.x - abs(RelLocal.x);
                float Dy = HalfE.y - abs(RelLocal.y);
                float Dz = HalfE.z - abs(RelLocal.z);
                if (Dx <= Dy && Dx <= Dz)
                {
                    LocalNormal = float3(RelLocal.x >= 0.0 ? 1.0 : -1.0, 0.0, 0.0);
                    Penetration = Dx + SafeParticleR;
                }
                else if (Dy <= Dz)
                {
                    LocalNormal = float3(0.0, RelLocal.y >= 0.0 ? 1.0 : -1.0, 0.0);
                    Penetration = Dy + SafeParticleR;
                }
                else
                {
                    LocalNormal = float3(0.0, 0.0, RelLocal.z >= 0.0 ? 1.0 : -1.0);
                    Penetration = Dz + SafeParticleR;
                }
                Hit = true;
            }
        }
        else
        {
            float  HalfH    = max(Proxy.HalfExtentsMeters.z, 0.0);
            float  MinDist  = max(Proxy.RadiusMeters, 1e-4) + SafeParticleR;
            float3 SegPt    = float3(0.0, 0.0, clamp(RelLocal.z, -HalfH, HalfH));
            float3 Diff     = RelLocal - SegPt;
            float  DistSq   = dot(Diff, Diff);
            if (DistSq < MinDist * MinDist)
            {
                float Dist = sqrt(max(DistSq, 1e-12));
                LocalNormal = (Dist > 1e-5) ? (Diff / Dist) : float3(0.0, 0.0, 1.0);
                Penetration = MinDist - Dist;
                Hit = true;
            }
        }

        if (Hit)
        {
            float3 WorldNrm = RotateByQuatGpu(Proxy.OrientationQuat, LocalNormal);
            Slot.PositionMeters += WorldNrm * Penetration;
            float3 RelV  = Slot.VelocityMps - Proxy.VelocityMps;
            float  RelVn = dot(RelV, WorldNrm);
            if (RelVn < 0.0)
            {
                float  NormalDelta = -(1.0 + clamp(Proxy.Restitution, 0.0, 1.0)) * RelVn;
                float3 TanV        = RelV - RelVn * WorldNrm;
                float  TanLenSq    = dot(TanV, TanV);
                Slot.VelocityMps  += WorldNrm * NormalDelta;
                if (TanLenSq > 1e-10 && Proxy.Friction > 0.0)
                {
                    float TanLen  = sqrt(TanLenSq);
                    float MaxFric = min(TanLen, clamp(Proxy.Friction, 0.0, 2.0) * NormalDelta);
                    Slot.VelocityMps -= TanV * (MaxFric / TanLen);
                }
            }
        }
    }

    // 9. Size progression & ColourSpace 64-entry LUT evaluation
    float NormT = saturate(Slot.AgeSeconds / max(1e-5, Slot.LifespanSeconds));
    Slot.SizeMeters   = lerp(RigParams.SizeBirthMeters, RigParams.SizeDeathMeters, NormT) * RigParams.SizeMultiplier;
    Slot.SpinRadians += 1.4 * Dt;

    ColourRangeUniform RangeU;
    RangeU.RangeMin           = RigParams.ColourRangeMin;
    RangeU.RangeMax           = RigParams.ColourRangeMax;
    RangeU.RepeatCount        = RigParams.ColourRepeat;
    RangeU.PhaseOffset        = RigParams.ColourPhase;
    RangeU.WrapBehaviour      = RigParams.ColourWrapCode;
    RangeU.BlendCurve         = RigParams.ColourCurveCode;
    RangeU.InterpolationSpace = ColourSpaceOklab;
    RangeU.ReservedPad        = 0u;

    float ScalarT = EvaluateParticleScalarChannel(RigParams.ScalarChannelCode, Slot, RigParams, GlobalParams.ElapsedSeconds);
    Slot.ColourLinear = SampleColourRampLut64(ColourLutBuffer, 0u, ScalarT, RangeU);

    ParticleSlots[SlotIdx] = Slot;
}
