/*============================================================================================================================================
                                                          DYNAMICSTRANDSOLVER.CPP
============================================================================================================================================*/
// 🧩 Implementation of exact 20-node HTML Particle Studio strand solvers (cables/chains, vines+leaves, flags, ship sails, beaded curtains)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/DynamicStrandSolver.h"

#include "Engine/ParticleSpace/ParticleTintDriver.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

struct Vec3
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

inline Vec3 operator+(Vec3 A, Vec3 B) noexcept { return { A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
inline Vec3 operator-(Vec3 A, Vec3 B) noexcept { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
inline Vec3 operator*(Vec3 A, float S) noexcept { return { A.X * S, A.Y * S, A.Z * S }; }
inline Vec3 Mix3(Vec3 A, Vec3 B, float T) noexcept { return A + (B - A) * T; }
inline float Dot3(Vec3 A, Vec3 B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
inline Vec3 Cross3(Vec3 A, Vec3 B) noexcept { return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }
inline float Len3(Vec3 A) noexcept { return std::sqrt(Dot3(A, A)); }
inline Vec3 Norm3(Vec3 A) noexcept
{
    const float L = Len3(A);
    return (L > 1e-6f) ? (A * (1.0f / L)) : Vec3 { 0.0f, 1.0f, 0.0f };
}

// Exact WGSL hash / hashF / randSphere / simVNoise / simCurl parity from Experimental/ParticleEditor Final/Engine/shaders.js
uint32_t Hash32(uint32_t X) noexcept
{
    X ^= X >> 16u;
    X *= 0x7feb352du;
    X ^= X >> 15u;
    X *= 0x846ca68bu;
    X ^= X >> 16u;
    return X;
}

float HashF(float Seed, uint32_t K) noexcept
{
    const uint32_t H = Hash32(static_cast<uint32_t>(std::max(0.0f, Seed)) * 31u + K * 977u + 17u);
    return static_cast<float>(H & 0x00FFFFFFu) / 16777216.0f;
}

Vec3 RandSphere(float U, float V) noexcept
{
    const float Z = 2.0f * U - 1.0f;
    const float A = kTwoPi * V;
    const float R = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
    return { R * std::cos(A), Z, R * std::sin(A) };
}

float FractF(float X) noexcept { return X - std::floor(X); }

float SimH31(Vec3 P) noexcept
{
    return FractF(std::sin(Dot3(P, { 127.1f, 311.7f, 74.7f })) * 43758.5453f);
}

float SimVNoise(Vec3 P) noexcept
{
    const Vec3 I { std::floor(P.X), std::floor(P.Y), std::floor(P.Z) };
    const Vec3 F { FractF(P.X), FractF(P.Y), FractF(P.Z) };
    const Vec3 U { F.X * F.X * (3.0f - 2.0f * F.X), F.Y * F.Y * (3.0f - 2.0f * F.Y), F.Z * F.Z * (3.0f - 2.0f * F.Z) };

    const auto MixF = [](float A, float B, float T) noexcept { return A + (B - A) * T; };
    const float A = MixF(
        MixF(SimH31(I), SimH31(I + Vec3 { 1.0f, 0.0f, 0.0f }), U.X),
        MixF(SimH31(I + Vec3 { 0.0f, 1.0f, 0.0f }), SimH31(I + Vec3 { 1.0f, 1.0f, 0.0f }), U.X),
        U.Y);
    const float B = MixF(
        MixF(SimH31(I + Vec3 { 0.0f, 0.0f, 1.0f }), SimH31(I + Vec3 { 1.0f, 0.0f, 1.0f }), U.X),
        MixF(SimH31(I + Vec3 { 0.0f, 1.0f, 1.0f }), SimH31(I + Vec3 { 1.0f, 1.0f, 1.0f }), U.X),
        U.Y);
    return MixF(A, B, U.Z);
}

Vec3 SimPot(Vec3 Q) noexcept
{
    return {
        SimVNoise(Q) * 2.0f - 1.0f,
        SimVNoise(Q + Vec3 { 17.3f, 3.1f, 9.7f }) * 2.0f - 1.0f,
        SimVNoise(Q + Vec3 { 5.2f, 31.7f, 1.9f }) * 2.0f - 1.0f
    };
}

Vec3 SimCurl(Vec3 Q) noexcept
{
    constexpr float Eps = 0.06f;
    constexpr float Inv = 0.5f / Eps;
    const Vec3 DAdx = (SimPot(Q + Vec3 { Eps, 0.0f, 0.0f }) - SimPot(Q - Vec3 { Eps, 0.0f, 0.0f })) * Inv;
    const Vec3 DAdy = (SimPot(Q + Vec3 { 0.0f, Eps, 0.0f }) - SimPot(Q - Vec3 { 0.0f, Eps, 0.0f })) * Inv;
    const Vec3 DAdz = (SimPot(Q + Vec3 { 0.0f, 0.0f, Eps }) - SimPot(Q - Vec3 { 0.0f, 0.0f, Eps })) * Inv;
    return { DAdy.Z - DAdz.Y, DAdz.X - DAdx.Z, DAdx.Y - DAdy.X };
}

struct DynStrandRest
{
    Vec3  PointA     {};
    Vec3  PointB     {};
    float Sag        = 0.0f;
    float Width      = 0.04f;
    float ColourMix  = 0.0f;
    bool  PinEnd     = false;
    float LeafSize   = 0.0f;
    float SwayScale  = 1.0f;
    float LeafOnU    = 0.85f;
    bool  Visible    = false;
};

DynStrandRest EvalDynStrandRest(
    uint32_t                    SlotIdx,
    uint32_t                    Cap,
    const ParticleRig&          Rig,
    const StrandConstraintSpec& Spec,
    Vec3                        Org) noexcept
{
    const Vec3 Ext {
        std::max(0.35f, Spec.BoxHalfMeters[0]),
        std::max(0.25f, Spec.BoxHalfMeters[1]),
        std::max(0.35f, Spec.BoxHalfMeters[2])
    };
    const uint32_t Rows   = std::clamp<uint32_t>(Spec.RowCount, 8u, std::max<uint32_t>(8u, Cap));
    const float    DynH   = std::max(0.35f, Spec.HeightMeters);
    const float    DynSag = Spec.CatenarySagM;
    const float    Sd     = static_cast<float>(SlotIdx) * 41.0f + 19.0f;
    const float    SpanW  = std::max(0.005f, Rig.Kinematics.SizeBirthMeters * Rig.Kinematics.SizeMultiplier);

    if (Spec.StrandVariant == 1u)
    {
        // Cables & Industrial Chains suspended between tiered pylon spans (exact WGSL dynMode == 1)
        const uint32_t NCables = std::min(Cap, std::max<uint32_t>(12u, Rows * 2u));
        if (SlotIdx >= NCables) return {};
        const uint32_t Tier = SlotIdx % 3u;
        const float Lane = static_cast<float>(SlotIdx / 3u) / std::max(1.0f, static_cast<float>(NCables / 3u) - 1.0f);
        const float YTier = DynH * (0.56f + 0.19f * static_cast<float>(Tier));
        const float ZOff  = (-Ext.Z + 2.0f * Ext.Z * Lane) + (HashF(Sd, 1u) - 0.5f) * 0.22f;
        const float XJit  = (HashF(Sd, 2u) - 0.5f) * 0.18f;
        const Vec3 PA = Org + Vec3 { -Ext.X + XJit, YTier, ZOff };
        const Vec3 PB = Org + Vec3 {  Ext.X - XJit, YTier + (HashF(Sd, 3u) - 0.5f) * 0.18f, ZOff + (HashF(Sd, 4u) - 0.5f) * 0.16f };
        const float Sag = DynSag * (0.65f + 0.72f * HashF(Sd, 5u));
        return { PA, PB, Sag, SpanW * 1.35f, static_cast<float>(Tier) * 0.45f, true, 0.0f, 0.72f, 0.85f, true };
    }

    if (Spec.StrandVariant == 2u)
    {
        // Hanging Vines & Braided Overhead Ropes: slots 0..nVines-1 are hanging vines, remaining slots are ivy leaves
        const uint32_t NVines = std::min(Cap, std::max<uint32_t>(8u, Rows));
        if (SlotIdx < NVines)
        {
            const float UX   = HashF(Sd, 1u) * 2.0f - 1.0f;
            const float UZ   = HashF(Sd, 2u) * 2.0f - 1.0f;
            const float YTop = Org.Y + DynH * (0.86f + 0.18f * HashF(Sd, 3u));
            const float Len  = std::clamp(DynSag * (0.62f + 0.53f * HashF(Sd, 4u)), 0.75f, std::max(0.95f, YTop - 0.25f));
            const Vec3  PA   { Org.X + UX * Ext.X, YTop, Org.Z + UZ * Ext.Z };
            const Vec3  PB   = PA + Vec3 { (HashF(Sd, 5u) - 0.5f) * 0.28f, -Len, (HashF(Sd, 6u) - 0.5f) * 0.28f };
            return { PA, PB, 0.04f, SpanW * 1.25f, HashF(Sd, 7u) * 0.5f, false, 0.0f, 1.15f, 0.85f, true };
        }
        return { Org, Org, 0.0f, 0.0f, 0.85f, false, 0.16f, 1.25f, 0.5f, true };
    }

    if (Spec.StrandVariant == 3u)
    {
        // Wind Flag (SubMode == 0) & Square-Rigged Ship Sail (SubMode == 1) (exact WGSL dynMode == 3)
        const uint32_t NRows = std::clamp<uint32_t>(Rows, 10u, std::min<uint32_t>(Cap, 44u));
        if (SlotIdx >= NRows) return {};
        const float V = (static_cast<float>(SlotIdx) + 0.5f) / static_cast<float>(NRows);
        if (Spec.SubMode == 0u)
        {
            const float MastX  = Org.X - Ext.X;
            const float FH     = std::max(0.65f, Ext.Y * 1.75f);
            const float YTop   = Org.Y + DynH;
            const float YBot   = std::max(0.35f, YTop - FH);
            const float YRow   = YBot + (YTop - YBot) * V;
            const Vec3  FDir   = Norm3({ Spec.SpanDirection[0] + 1e-4f, 0.0f, Spec.SpanDirection[2] });
            const float FLen   = std::max(0.8f, Ext.X * 2.0f);
            const Vec3  PA     { MastX, YRow, Org.Z };
            const Vec3  PB     = PA + FDir * FLen;
            const float StripW = (FH / static_cast<float>(NRows)) * 0.64f;
            return { PA, PB, std::max(0.02f, DynSag * 0.28f), StripW, V, false, 0.0f, 1.15f, 0.85f, true };
        }
        else
        {
            const float SH       = std::max(0.8f, Ext.Y * 1.85f);
            const float YTop     = Org.Y + DynH;
            const float YBot     = std::max(0.45f, YTop - SH);
            const float YRow     = YBot + (YTop - YBot) * V;
            const float HalfW    = Ext.X * (1.06f + (0.90f - 1.06f) * V);
            const Vec3  PA       { Org.X - HalfW, YRow, Org.Z };
            const Vec3  PB       { Org.X + HalfW, YRow, Org.Z };
            const float StripW   = (SH / static_cast<float>(NRows)) * 0.64f;
            const float BellySag = DynSag * 0.04f * std::sin(V * kPi);
            return { PA, PB, BellySag, StripW, V, true, 0.0f, 1.05f, 0.85f, true };
        }
    }

    return {};
}

struct BeadRest
{
    Vec3  TopPos  {};
    float DropLen = 2.5f;
    float Width   = 0.072f;
    float Phase   = 0.0f;
    float IsSail  = 0.0f;
};

BeadRest EvalBeadRest(
    uint32_t                    SlotIdx,
    uint32_t                    Cap,
    const ParticleRig&          Rig,
    const StrandConstraintSpec& Spec,
    Vec3                        Org) noexcept
{
    const uint32_t BMode    = std::clamp<uint32_t>(Spec.SubMode, 1u, 3u);
    const float    DropH    = std::max(0.85f, Spec.HeightMeters);
    const float    WRef     = std::max(0.025f, Rig.Kinematics.SizeBirthMeters);
    const Vec3     Ext      { std::max(0.45f, Spec.BoxHalfMeters[0]), std::max(0.45f, Spec.BoxHalfMeters[1]), std::max(0.45f, Spec.BoxHalfMeters[2]) };
    const uint32_t NStrands = std::max<uint32_t>(6u, std::min<uint32_t>(Cap, Spec.RowCount));
    const uint32_t IStrand  = SlotIdx % NStrands;
    const float    U        = (static_cast<float>(IStrand) + 0.5f) / static_cast<float>(NStrands);
    const float    Sd       = static_cast<float>(SlotIdx) * 37.0f + 13.0f;

    Vec3  TopPos  = Org + Vec3 { (U - 0.5f) * Ext.X * 2.0f, DropH, (HashF(Sd, 1u) - 0.5f) * Ext.Z * 0.65f };
    float DropLen = DropH * (0.72f + 0.24f * std::cos((U * 2.0f - 1.0f) * 1.45f));
    float WSize   = WRef;
    float IsSail  = 0.0f;

    if (BMode == 2u)
    {
        const float CrownR = 0.56f;
        if (IStrand == 0u)
        {
            TopPos  = Org + Vec3 { 0.0f, DropH, 0.0f };
            DropLen = DropH * 0.86f;
            WSize   = WRef * 1.55f;
            IsSail  = 1.0f;
        }
        else
        {
            const float TubeU = static_cast<float>(IStrand - 1u) / std::max(1.0f, static_cast<float>(NStrands - 1u));
            const float Ang   = TubeU * kTwoPi;
            TopPos  = Org + Vec3 { std::cos(Ang) * CrownR, DropH, std::sin(Ang) * CrownR };
            DropLen = DropH * (0.30f + 0.42f * TubeU);
            WSize   = WRef * 1.05f;
        }
    }
    else if (BMode == 3u)
    {
        const float PleatZ = std::sin(static_cast<float>(IStrand) * 2.1f) * Ext.Z * 0.72f;
        TopPos  = Org + Vec3 { (U - 0.5f) * Ext.X * 2.05f, DropH, PleatZ };
        DropLen = std::max(0.65f, DropH - 0.08f);
        WSize   = std::max(WRef, (Ext.X * 2.25f) / static_cast<float>(NStrands) * 0.58f);
    }

    return { TopPos, DropLen, WSize, HashF(Sd, 2u) * kTwoPi, IsSail };
}

void SampleWindForce(
    Vec3                                                       Pos,
    Vec3                                                       AmbientWind,
    const SimulationSpace::ForceFields::ForceFieldAccumulator& ForceAccumulator,
    double                                                     ElapsedSec,
    Vec3&                                                      OutWind,
    Vec3&                                                      OutForce) noexcept
{
    const SimulationSpace::ForceFields::SpatialVector3 Pt { Pos.X, Pos.Y, Pos.Z };
    const SimulationSpace::ForceFields::SpatialVector3 ZeroV { 0.0f, 0.0f, 0.0f };
    const auto Sample = ForceAccumulator.QueryForceFieldSample(Pt, ZeroV, ElapsedSec);
    OutWind  = { AmbientWind.X + Sample.AdvectionVelocity.X, AmbientWind.Y + Sample.AdvectionVelocity.Y, AmbientWind.Z + Sample.AdvectionVelocity.Z };
    OutForce = { Sample.LinearAcceleration.X, Sample.LinearAcceleration.Y, Sample.LinearAcceleration.Z };
}

} // namespace

void StepDynamicStrandEnsemble(
    const ParticleRig&                                         Rig,
    const StrandConstraintSpec&                                Spec,
    const float                                                OriginMeters[3],
    const float                                                AmbientWindMps[3],
    const SimulationSpace::ForceFields::ForceFieldAccumulator& ForceAccumulator,
    const InteractiveColliderSpec&                             Collider,
    std::span<ParticleSlot>                                    Slots,
    std::span<StrandVertex>                                    StrandVertices,
    float                                                      ElapsedSeconds,
    float                                                      DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f || Slots.empty()) return;
    const uint32_t Cap = static_cast<uint32_t>(Slots.size());
    if (StrandVertices.size() < static_cast<std::size_t>(Cap) * kDynamicStrandVertexCount) return;

    const Vec3  Org         { OriginMeters[0], OriginMeters[1], OriginMeters[2] };
    const Vec3  AmbientWind { AmbientWindMps[0], AmbientWindMps[1], AmbientWindMps[2] };
    const float WindCoupling = std::max(0.0f, Rig.Dynamics.ForceCouple.WindCouplingRate);
    const float Stiff        = std::clamp(Spec.StiffnessRate, 0.25f, 4.0f);
    const float WaveSpd      = std::max(0.2f, Spec.WaveSpeedHz);
    const float Freq         = std::max(0.15f, Spec.CurlFreqPerM);
    const float CurlAmp      = Spec.CurlAmpMps;
    const float TearLim      = Spec.TearThreshold;
    const Vec3  BallPos      { Collider.BallCenterM[0], Collider.BallCenterM[1], Collider.BallCenterM[2] };
    const Vec3  BallVel      { Collider.BallVelocityMps[0], Collider.BallVelocityMps[1], Collider.BallVelocityMps[2] };
    const float BallRad      = Collider.BallActive ? Collider.BallRadiusMeters : 0.0f;
    const float BallSpd      = Len3(BallVel);

    ParticleTintEvaluationContext TintCtx {};
    TintCtx.ElapsedSeconds  = ElapsedSeconds;
    TintCtx.OriginMeters[0] = Org.X; TintCtx.OriginMeters[1] = Org.Y; TintCtx.OriginMeters[2] = Org.Z;

    // ----------------------------------------------------------------------------------------------------------------
    // VARIANT 4: Hanging Beaded Doorway Curtain, Bamboo Wind Chimes & Velvet Doorway Drapes (HTML beadsMode == 1..3)
    // ----------------------------------------------------------------------------------------------------------------
    if (Spec.StrandVariant == 4u)
    {
        const uint32_t NStrands = std::max<uint32_t>(6u, std::min<uint32_t>(Cap, Spec.RowCount));
        for (uint32_t I = 0u; I < Cap; ++I)
        {
            ParticleSlot& Slot = Slots[I];
            const std::size_t VertexOffset = static_cast<std::size_t>(I) * kDynamicStrandVertexCount;

            if (I >= NStrands)
            {
                Slot.AgeSeconds   = -1.0f;
                Slot.PackedTraits = 0u;
                for (uint32_t K = 0u; K < kDynamicStrandVertexCount; ++K)
                {
                    StrandVertices[VertexOffset + K].NormalisedSpanU = -1.0f;
                }
                continue;
            }
            if (!IsParticleSlotAlive(Slot))
            {
                Slot.AgeSeconds      = 0.0f;
                Slot.LifespanSeconds = 9999.0f;
                Slot.SeedScalar      = HashF(static_cast<float>(I) * 71.0f + 3.0f, 5u);
                Slot.SpinRadians     = HashF(static_cast<float>(I) * 31.0f + 11.0f, 7u) * kTwoPi;
                Slot.AuxScalar       = 0.0f;
                Slot.PackedTraits    = ParticleTraitAliveBit;
            }
            else
            {
                Slot.AgeSeconds += DeltaSeconds;
            }

            const BeadRest Rest = EvalBeadRest(I, Cap, Rig, Spec, Org);
            const bool FirstInit = ((Slot.PackedTraits & kParticleTraitStrandInitBit) == 0u);
            std::array<Vec3, kDynamicStrandVertexCount> CurN {};
            if (FirstInit)
            {
                for (uint32_t K = 0u; K < kDynamicStrandVertexCount; ++K)
                {
                    const float U = static_cast<float>(K) / 19.0f;
                    CurN[K] = Rest.TopPos - Vec3 { 0.0f, Rest.DropLen * U, 0.0f };
                }
                Slot.PackedTraits |= kParticleTraitStrandInitBit;
            }
            else
            {
                for (uint32_t K = 0u; K < kDynamicStrandVertexCount; ++K)
                {
                    const auto& V = StrandVertices[VertexOffset + K];
                    CurN[K] = { V.PositionMeters[0], V.PositionMeters[1], V.PositionMeters[2] };
                }
            }

            CurN[0] = Rest.TopPos;
            Vec3 WindAtTop {}, ForceAtTop {};
            SampleWindForce(Rest.TopPos, AmbientWind, ForceAccumulator, static_cast<double>(ElapsedSeconds), WindAtTop, ForceAtTop);

            const float StepLen   = Rest.DropLen / 19.0f;
            float       Flash     = Slot.AuxScalar * std::exp(-2.6f * DeltaSeconds);
            const float RelaxRate = std::min(1.0f, (1.6f + 2.8f * Stiff) * DeltaSeconds);
            const uint32_t BMode  = std::clamp<uint32_t>(Spec.SubMode, 1u, 3u);

            for (uint32_t K = 1u; K < kDynamicStrandVertexCount; ++K)
            {
                const float U = static_cast<float>(K) / 19.0f;
                const float SwayX = (WindAtTop.X * 0.22f + ForceAtTop.X * 0.08f + 0.14f * std::sin(ElapsedSeconds * WaveSpd + Rest.Phase + U * 2.2f)) * U * WindCoupling;
                const float SwayZ = (WindAtTop.Z * 0.22f + ForceAtTop.Z * 0.08f + 0.12f * std::cos(ElapsedSeconds * (WaveSpd * 0.85f) + Rest.Phase * 1.3f + U * 1.9f)) * U * WindCoupling;
                const Vec3 IdealP = Rest.TopPos + Vec3 { SwayX, -Rest.DropLen * U, SwayZ };
                Vec3 P = Mix3(CurN[K], IdealP, RelaxRate * (U < 0.25f ? 1.0f : 0.72f));

                if (BMode == 2u && Rest.IsSail < 0.5f && K >= 4u)
                {
                    const Vec3 TubeDir = Norm3(Mix3({ 0.0f, -1.0f, 0.0f }, P - Rest.TopPos, std::clamp(0.65f / Stiff, 0.2f, 0.95f)));
                    P = Mix3(P, Rest.TopPos + TubeDir * (Rest.DropLen * U), 0.72f);
                }

                if (BallRad > 0.04f)
                {
                    const Vec3 DVec = P - BallPos;
                    const float Dist = Len3(DVec);
                    const float Reach = BallRad + Rest.Width * 1.4f + 0.14f;
                    if (Dist < Reach && Dist > 1e-4f)
                    {
                        const Vec3 PushN = Norm3({ DVec.X + 1e-4f, std::max(-0.25f, DVec.Y * 0.35f), DVec.Z + 1e-4f });
                        P = BallPos + PushN * Reach + BallVel * (DeltaSeconds * 0.45f);
                        Flash = std::max(Flash, std::clamp(0.65f + BallSpd * 0.18f, 0.5f, 1.8f));
                    }
                }

                if (P.Y < 0.04f) P.Y = 0.04f;
                const Vec3 Prev = CurN[K - 1u];
                const Vec3 SegV = P - Prev;
                const float SLen = Len3(SegV);
                if (SLen > 1e-4f) P = Prev + SegV * (StepLen / SLen);
                CurN[K] = P;
            }

            const float TipU = std::clamp(Flash, 0.0f, 1.5f);
            for (uint32_t K = 0u; K < kDynamicStrandVertexCount; ++K)
            {
                StrandVertices[VertexOffset + K].PositionMeters[0] = CurN[K].X;
                StrandVertices[VertexOffset + K].PositionMeters[1] = CurN[K].Y;
                StrandVertices[VertexOffset + K].PositionMeters[2] = CurN[K].Z;
                StrandVertices[VertexOffset + K].NormalisedSpanU   = TipU;
            }

            const Vec3 GlintPos = Mix3(CurN[5], CurN[16], FractF(ElapsedSeconds * 0.42f + Rest.Phase * 0.159f));
            Slot.PositionMeters[0] = GlintPos.X;
            Slot.PositionMeters[1] = GlintPos.Y;
            Slot.PositionMeters[2] = GlintPos.Z;
            Slot.SizeMeters        = Rest.Width;
            Slot.AuxScalar         = Flash;
            Slot.PackedTraits      = (Slot.PackedTraits & ~kParticleTraitStrandLeafBit) | kParticleTraitStrandValidBit;

            TintCtx.ModeOverrideScalar = std::clamp(0.48f + Flash * 0.45f, 0.0f, 1.0f);
            const auto Col = EvaluateParticleTint(Rig.TintDriver, Rig.ColourProfile, Slot, TintCtx);
            Slot.ColourLinear[0] = Col.Red; Slot.ColourLinear[1] = Col.Green; Slot.ColourLinear[2] = Col.Blue; Slot.ColourLinear[3] = Col.Alpha;
        }
        return;
    }

    // ----------------------------------------------------------------------------------------------------------------
    // VARIANTS 1..3: Cables/Chains (1), Hanging Leafy Vines (2), Wind Flag & Ship Sail (3)
    // Pass 1: Update all primary strands first so vine leaves in Pass 2 read up-to-date vine node positions!
    // ----------------------------------------------------------------------------------------------------------------
    const uint32_t NRows = std::clamp<uint32_t>(Spec.RowCount, 8u, Cap);
    for (uint32_t I = 0u; I < Cap; ++I)
    {
        ParticleSlot& Slot = Slots[I];
        if (Spec.StrandVariant == 2u && I >= NRows) continue; // Vine ivy leaves updated in Pass 2

        const std::size_t VertexOffset = static_cast<std::size_t>(I) * kDynamicStrandVertexCount;
        const DynStrandRest St = EvalDynStrandRest(I, Cap, Rig, Spec, Org);
        if (!St.Visible)
        {
            Slot.AgeSeconds   = -1.0f;
            Slot.PackedTraits = 0u;
            for (uint32_t K = 0u; K < kDynamicStrandVertexCount; ++K)
            {
                StrandVertices[VertexOffset + K].NormalisedSpanU = -1.0f;
            }
            continue;
        }
        if (!IsParticleSlotAlive(Slot))
        {
            Slot.AgeSeconds      = 0.0f;
            Slot.LifespanSeconds = 9999.0f;
            Slot.SeedScalar      = HashF(static_cast<float>(I) * 71.0f + 3.0f, 5u);
            Slot.SpinRadians     = HashF(static_cast<float>(I) * 31.0f + 11.0f, 7u) * kTwoPi;
            Slot.AuxScalar       = 0.0f;
            Slot.PackedTraits    = ParticleTraitAliveBit;
        }
        else
        {
            Slot.AgeSeconds += DeltaSeconds;
        }

        const Vec3 MidRest = (St.PointA + St.PointB) * 0.5f;
        Vec3 WindSample {}, ForceSample {};
        SampleWindForce(MidRest, AmbientWind, ForceAccumulator, static_cast<double>(ElapsedSeconds), WindSample, ForceSample);
        WindSample = WindSample * WindCoupling;
        const float LenSpan = std::max(0.2f, Len3(St.PointB - St.PointA));
        const float Seed    = Slot.SeedScalar * 1000.0f + static_cast<float>(I) * 13.0f;

        std::array<Vec3, kDynamicStrandVertexCount> RestN {};
        for (uint32_t K = 0u; K < kDynamicStrandVertexCount; ++K)
        {
            const float UU        = static_cast<float>(K) / 19.0f;
            const float Belly     = 4.0f * UU * (1.0f - UU);
            const float TipWeight = St.PinEnd ? Belly : std::pow(UU, 1.35f);
            Vec3 PtR = Mix3(St.PointA, St.PointB, UU) - Vec3 { 0.0f, 4.0f * St.Sag * UU * (1.0f - UU), 0.0f };

            if (Spec.StrandVariant == 3u)
            {
                // Exact HTML SoftwarePreviewEngine.js dynMode == 3 billowing flag ripples & wind-filled square ship sail
                const float WPhase  = UU * 6.5f - ElapsedSeconds * WaveSpd + St.ColourMix * 1.5f;
                const float FlapAmp = (0.14f + 0.34f * UU) * (Spec.SubMode > 0u ? 0.85f : 1.0f);
                const float Mask    = St.PinEnd ? Belly : std::clamp(UU * 2.4f, 0.0f, 1.0f);
                const Vec3  Gust    = (WindSample + ForceSample * 0.16f) * (0.12f * TipWeight * St.SwayScale);
                const float WaveZ   = (std::sin(WPhase) * FlapAmp + std::cos(WPhase * 1.9f + ElapsedSeconds) * FlapAmp * 0.28f) * Mask;
                const float WaveY   = std::sin(WPhase * 0.8f + 0.5f) * FlapAmp * 0.22f * Mask;
                // Ship Sail (SubMode == 1): top yardarm (RowV=0) & bottom boom (RowV=1) stay attached while middle billows forward in +Z!
                const float RowBelly = (Spec.SubMode == 1u) ? (0.35f + 0.65f * std::sin(St.ColourMix * kPi)) : 1.0f;
                const Vec3 SailBelly = (Spec.SubMode == 1u)
                    ? Vec3 { 0.0f, 0.0f, Spec.CatenarySagM * 1.15f * Belly * RowBelly }
                    : Vec3 { 0.0f, 0.0f, 0.0f };
                PtR = PtR + Vec3 { 0.0f, WaveY * (Spec.SubMode == 1u ? 0.25f * RowBelly : 1.0f), WaveZ * RowBelly } + Gust + SailBelly;
            }
            else
            {
                // Travelling wind gusts + 3D curl sway for Cables/Chains & Hanging Vines (exact WGSL dynMode == 1, 2)
                const float GustPh = (PtR.X * 0.85f + PtR.Z * 0.52f) * Freq - ElapsedSeconds * WaveSpd;
                const float GustV  = (0.38f + 0.62f * std::sin(GustPh) + 0.25f * std::cos(GustPh * 1.8f + Seed * 0.01f)) * CurlAmp * 0.16f;
                const Vec3  CDisp  = SimCurl(PtR * (Freq * 0.6f) + Vec3 { ElapsedSeconds * 0.65f, 0.0f, ElapsedSeconds * 0.45f }) * (CurlAmp * 0.18f);
                Vec3 TotalW = (WindSample * 0.14f + ForceSample * 0.06f + Vec3 { GustV, 0.0f, GustV * 0.45f } + CDisp) * (TipWeight * St.SwayScale);
                const float WL = Len3(TotalW);
                if (WL > LenSpan * 0.72f) TotalW = TotalW * ((LenSpan * 0.72f) / WL);
                PtR = PtR + TotalW;
            }
            RestN[K] = PtR;
        }

        float SnapU = Slot.AuxScalar;
        std::array<Vec3, kDynamicStrandVertexCount> CurN {};
        const bool FirstInit = ((Slot.PackedTraits & kParticleTraitStrandInitBit) == 0u);
        if (FirstInit || Slot.AgeSeconds <= DeltaSeconds * 1.6f)
        {
            CurN = RestN;
            Slot.PackedTraits |= kParticleTraitStrandInitBit;
        }
        else
        {
            for (uint32_t K = 0u; K < kDynamicStrandVertexCount; ++K)
            {
                const auto& V = StrandVertices[VertexOffset + K];
                CurN[K] = { V.PositionMeters[0], V.PositionMeters[1], V.PositionMeters[2] };
            }

            if (SnapU > 0.05f)
            {
                const uint32_t BreakK  = std::clamp<uint32_t>(static_cast<uint32_t>(SnapU * 19.0f + 0.5f), 2u, 17u);
                const float    SegRest = std::max(0.015f, LenSpan / 19.0f);
                CurN[0] = RestN[0];
                for (uint32_t K = 1u; K <= BreakK; ++K)
                {
                    const Vec3 Tgt = CurN[K - 1u] + Vec3 { 0.0f, -SegRest * 0.94f, 0.0f } + WindSample * 0.03f;
                    CurN[K] = Mix3(CurN[K], Tgt, std::min(1.0f, DeltaSeconds * 8.5f));
                }
                if (St.PinEnd)
                {
                    CurN[19] = RestN[19];
                    for (uint32_t Kr = 19u; Kr > BreakK + 1u; --Kr)
                    {
                        const uint32_t K = Kr - 1u;
                        const Vec3 TgtB = CurN[K + 1u] + Vec3 { 0.0f, -SegRest * 0.94f, 0.0f } + WindSample * 0.03f;
                        CurN[K] = Mix3(CurN[K], TgtB, std::min(1.0f, DeltaSeconds * 8.5f));
                    }
                }
            }
            else
            {
                const float RecRate = std::clamp(DeltaSeconds * 4.8f * Stiff, 0.03f, 0.72f);
                CurN[0] = RestN[0];
                if (St.PinEnd) CurN[19] = RestN[19];
                for (uint32_t K = 1u; K < kDynamicStrandVertexCount; ++K)
                {
                    if (K == 19u && St.PinEnd) continue;
                    Vec3 Pt = Mix3(CurN[K], RestN[K], RecRate);
                    if (BallRad > 0.04f)
                    {
                        const Vec3 DVec = Pt - BallPos;
                        const float DLen = Len3(DVec);
                        const float PadR = BallRad + 0.04f;
                        if (DLen < PadR)
                        {
                            const Vec3 PDir = Norm3(DVec + Vec3 { 0.0f, 0.05f, 0.0f });
                            Pt = BallPos + PDir * PadR + BallVel * std::min(DeltaSeconds * 0.42f, 0.030f);
                            if (TearLim > 0.0f && BallSpd > TearLim && SnapU == 0.0f && K >= 2u && K <= 17u)
                            {
                                SnapU = static_cast<float>(K) / 19.0f;
                            }
                        }
                        else if (DLen < PadR * 2.35f && BallSpd > 0.5f)
                        {
                            const float Wf = std::pow(1.0f - DLen / (PadR * 2.35f), 1.5f);
                            Pt = Pt + (Norm3(DVec + Vec3 { 0.0f, 0.04f, 0.0f }) * 0.65f + Norm3(BallVel) * 0.35f) * (Wf * std::min(BallSpd, 10.0f) * DeltaSeconds * 0.42f);
                        }
                    }
                    Pt.Y    = std::max(0.015f, Pt.Y);
                    CurN[K] = Pt;
                }

                // 5-pass bilateral inextensibility constraint projection
                const float MaxSeg = std::max(0.018f, LenSpan / 19.0f) * 1.08f;
                for (uint32_t Sweep = 0u; Sweep < 5u; ++Sweep)
                {
                    CurN[0] = RestN[0];
                    for (uint32_t K = 0u; K < 19u; ++K)
                    {
                        const Vec3 Df = CurN[K + 1u] - CurN[K];
                        const float Dl = Len3(Df);
                        if (Dl > MaxSeg && !(K == 18u && St.PinEnd))
                        {
                            CurN[K + 1u] = CurN[K] + Df * (MaxSeg / std::max(Dl, 1e-4f));
                        }
                    }
                    if (St.PinEnd)
                    {
                        CurN[19] = RestN[19];
                        for (uint32_t Kr = 19u; Kr > 1u; --Kr)
                        {
                            const uint32_t K = Kr - 1u;
                            const Vec3 Db = CurN[K] - CurN[K + 1u];
                            const float Dl = Len3(Db);
                            if (Dl > MaxSeg)
                            {
                                CurN[K] = CurN[K + 1u] + Db * (MaxSeg / std::max(Dl, 1e-4f));
                            }
                        }
                    }
                }
            }
        }

        const uint32_t BreakKOut = std::clamp<uint32_t>(static_cast<uint32_t>(SnapU * 19.0f + 0.5f), 2u, 17u);
        for (uint32_t K = 0u; K < kDynamicStrandVertexCount; ++K)
        {
            float WMeta = std::clamp(0.65f + 0.35f * St.ColourMix, 0.0f, 1.0f);
            if (SnapU > 0.05f && (K == BreakKOut || (!St.PinEnd && K > BreakKOut))) WMeta = -1.0f;
            StrandVertices[VertexOffset + K].PositionMeters[0] = CurN[K].X;
            StrandVertices[VertexOffset + K].PositionMeters[1] = CurN[K].Y;
            StrandVertices[VertexOffset + K].PositionMeters[2] = CurN[K].Z;
            StrandVertices[VertexOffset + K].NormalisedSpanU   = WMeta;
        }

        const uint32_t TipK   = std::clamp<uint32_t>(static_cast<uint32_t>(St.LeafOnU * 19.0f + 0.5f), 1u, 19u);
        const Vec3     HeadPt = CurN[TipK];
        const Vec3     Tan    = Norm3(CurN[TipK] - CurN[TipK - 1u] + Vec3 { 0.0f, 0.01f, 0.0f });
        Slot.PositionMeters[0] = HeadPt.X; Slot.PositionMeters[1] = HeadPt.Y; Slot.PositionMeters[2] = HeadPt.Z;
        Slot.VelocityMps[0]    = Tan.X;    Slot.VelocityMps[1]    = Tan.Y;    Slot.VelocityMps[2]    = Tan.Z;
        Slot.SizeMeters        = St.Width;
        Slot.AuxScalar         = SnapU;
        Slot.PackedTraits      = (Slot.PackedTraits & ~kParticleTraitStrandLeafBit) | kParticleTraitStrandValidBit;

        TintCtx.ModeOverrideScalar = St.ColourMix;
        const auto Col = EvaluateParticleTint(Rig.TintDriver, Rig.ColourProfile, Slot, TintCtx);
        Slot.ColourLinear[0] = Col.Red; Slot.ColourLinear[1] = Col.Green; Slot.ColourLinear[2] = Col.Blue; Slot.ColourLinear[3] = Col.Alpha;
    }

    // Pass 2: Vine Ivy Leaves clinging along hanging vine nodes (exact WGSL dynMode == 2, slotIdx >= nRows)
    if (Spec.StrandVariant == 2u)
    {
        for (uint32_t I = NRows; I < Cap; ++I)
        {
            ParticleSlot& Slot = Slots[I];
            if (!IsParticleSlotAlive(Slot))
            {
                Slot.AgeSeconds      = 0.0f;
                Slot.LifespanSeconds = 9999.0f;
                Slot.SeedScalar      = HashF(static_cast<float>(I) * 71.0f + 3.0f, 5u);
                Slot.SpinRadians     = HashF(static_cast<float>(I) * 31.0f + 11.0f, 7u) * kTwoPi;
                Slot.AuxScalar       = 0.0f;
                Slot.PackedTraits    = ParticleTraitAliveBit;
            }
            else
            {
                Slot.AgeSeconds += DeltaSeconds;
            }
            const std::size_t VertexOffset = static_cast<std::size_t>(I) * kDynamicStrandVertexCount;
            for (uint32_t K = 0u; K < kDynamicStrandVertexCount; ++K)
            {
                StrandVertices[VertexOffset + K].NormalisedSpanU = -1.0f;
            }

            const float Seed   = static_cast<float>(I) * 53.0f + 7.0f;
            const uint32_t LNum  = I - NRows;
            const uint32_t PSlot = LNum % NRows;
            const uint32_t PVertex = std::clamp<uint32_t>(4u + ((LNum / NRows) * 5u + static_cast<uint32_t>(HashF(Seed, 9u) * 5.0f)) % 14u, 4u, 18u);
            const auto& AnchorV  = StrandVertices[static_cast<std::size_t>(PSlot) * kDynamicStrandVertexCount + PVertex];
            const Vec3  AnchorW  { AnchorV.PositionMeters[0], AnchorV.PositionMeters[1], AnchorV.PositionMeters[2] };

            const float LeafSz  = std::max(0.085f, Rig.Kinematics.SizeBirthMeters * 2.6f * (0.68f + 0.60f * HashF(Seed, 6u)));
            float Fallen        = Slot.AuxScalar;
            Vec3  LPos          { Slot.PositionMeters[0], Slot.PositionMeters[1], Slot.PositionMeters[2] };
            Vec3  LVel          { Slot.VelocityMps[0], Slot.VelocityMps[1], Slot.VelocityMps[2] };

            if (Fallen > 0.05f)
            {
                Vec3 WLocal {}, FLocal {};
                SampleWindForce(LPos, AmbientWind, ForceAccumulator, static_cast<double>(ElapsedSeconds), WLocal, FLocal);
                LVel = LVel + (WLocal - LVel) * std::min(WindCoupling * DeltaSeconds, 1.0f);
                LVel.Y -= std::max(0.15f, Rig.Dynamics.Ballistic.GravityScale) * 4.5f * DeltaSeconds;
                LVel = LVel + FLocal * DeltaSeconds;
                const float FlPh = Slot.AgeSeconds * 4.2f + Seed * 0.03f;
                LVel = LVel + Vec3 { std::sin(FlPh), 0.35f * std::cos(FlPh * 1.4f), std::cos(FlPh * 0.9f) } * (0.9f * DeltaSeconds);
                LVel = LVel * std::exp(-1.35f * DeltaSeconds);
                LPos = LPos + LVel * DeltaSeconds;
                if (LPos.Y <= 0.025f) { LPos.Y = 0.025f; LVel = { LVel.X * 0.72f, 0.0f, LVel.Z * 0.72f }; }
                else { Slot.SpinRadians += 1.6f * DeltaSeconds; }
                if (Slot.AgeSeconds - Fallen > 14.0f) { Fallen = 0.0f; LVel = {}; }
            }
            else
            {
                const Vec3 OffDir  = RandSphere(HashF(Seed, 2u), HashF(Seed, 3u));
                const Vec3 Flutter = Vec3 {
                    std::sin(ElapsedSeconds * 4.2f + Seed),
                    std::cos(ElapsedSeconds * 3.6f + Seed * 0.7f) * 0.6f,
                    std::cos(ElapsedSeconds * 4.5f + Seed)
                } * (0.04f + 0.03f * CurlAmp);
                LPos = AnchorW + OffDir * 0.11f + Flutter;
                Slot.SpinRadians += std::sin(ElapsedSeconds * 3.4f + Seed) * 1.8f * DeltaSeconds;
                if (BallRad > 0.04f && Len3(LPos - BallPos) < BallRad + 0.36f && BallSpd > std::max(1.8f, TearLim * 0.55f))
                {
                    Fallen = Slot.AgeSeconds + 0.01f;
                    LVel   = Norm3(LPos - BallPos + Vec3 { 0.0f, 0.3f, 0.0f }) * 1.8f + BallVel * 0.36f;
                }
            }

            Slot.PositionMeters[0] = LPos.X; Slot.PositionMeters[1] = LPos.Y; Slot.PositionMeters[2] = LPos.Z;
            Slot.VelocityMps[0]    = LVel.X; Slot.VelocityMps[1]    = LVel.Y; Slot.VelocityMps[2]    = LVel.Z;
            Slot.SizeMeters        = LeafSz;
            Slot.AuxScalar         = Fallen;
            Slot.PackedTraits     |= (kParticleTraitStrandLeafBit | kParticleTraitStrandValidBit);

            TintCtx.ModeOverrideScalar = 0.50f + 0.48f * HashF(Seed, 5u);
            const auto Col = EvaluateParticleTint(Rig.TintDriver, Rig.ColourProfile, Slot, TintCtx);
            Slot.ColourLinear[0] = Col.Red; Slot.ColourLinear[1] = Col.Green; Slot.ColourLinear[2] = Col.Blue; Slot.ColourLinear[3] = Col.Alpha;
        }
    }
}

StrandRiggingHardware BuildStrandHardwareProps(
    const StrandConstraintSpec& Spec,
    const float                 OriginMeters[3])
{
    StrandRiggingHardware Out {};
    if (!Spec.ShowHardwareProp) return Out;

    const float Ox   = OriginMeters[0];
    const float Oy   = OriginMeters[1];
    const float Oz   = OriginMeters[2];
    const float Ex   = std::max(0.5f, Spec.BoxHalfMeters[0]);
    const float Ey   = std::max(0.5f, Spec.BoxHalfMeters[1]);
    const float Ez   = std::max(0.5f, Spec.BoxHalfMeters[2]);
    const float DynH = std::max(0.8f, Spec.HeightMeters);

    if (Spec.StrandVariant == 1u)
    {
        // 4 tiered industrial dark-steel pylons + 2 slender top crossbars (linearized exact ParticleEditor.js:1875 colors)
        const float Py = Oy + DynH * 1.02f;
        for (float Sx : { -1.0f, 1.0f })
        {
            for (float Sz : { -1.0f, 1.0f })
            {
                const float Px = Ox + Sx * Ex * 1.04f;
                const float Pz = Oz + Sz * Ez;
                Out.Cylinders.push_back({ { Px, 0.0f, Pz }, { Px, Py, Pz }, 0.044f, { 0.095f, 0.114f, 0.148f, 0.96f } });
            }
            const float Px = Ox + Sx * Ex * 1.04f;
            Out.Cylinders.push_back({ { Px, Py * 0.96f, Oz - Ez }, { Px, Py * 0.96f, Oz + Ez }, 0.018f, { 0.175f, 0.228f, 0.292f, 0.92f } });
        }
    }
    else if (Spec.StrandVariant == 3u && Spec.SubMode == 0u)
    {
        // Metallic Flagpole + Golden Finial Sphere + Floor Pedestal Ring (linearized exact ParticleEditor.js:1864)
        const float Mx   = Ox - Ex;
        const float TopY = Oy + DynH + 0.16f;
        Out.Cylinders.push_back({ { Mx, 0.0f, Oz }, { Mx, TopY, Oz }, 0.044f, { 0.295f, 0.340f, 0.418f, 0.98f } });
        Out.Spheres.push_back({ { Mx, TopY + 0.055f, Oz }, 0.082f, { 0.870f, 0.545f, 0.062f, 1.0f } });
        Out.Rings.push_back({ { Mx, 0.02f, Oz }, 0.17f, 24u, { 0.380f, 0.445f, 0.535f, 0.72f } });
    }
    else if (Spec.StrandVariant == 3u && Spec.SubMode == 1u)
    {
        // Rich Dark-Oak Wooden Ship Mast + Slender Yardarm + Bottom Boom + Diagonal Rigging Stays (linearized exact ParticleEditor.js:1849)
        const float SH   = std::max(0.8f, Ey * 1.85f);
        const float YTop = Oy + DynH;
        const float YBot = std::max(0.45f, YTop - SH);
        const float Mx   = Ox;
        const float Mz   = Oz - 0.14f;
        const float TopY = YTop + 0.25f;
        Out.Cylinders.push_back({ { Mx, 0.0f, Mz }, { Mx, TopY, Mz }, 0.060f, { 0.175f, 0.082f, 0.030f, 0.98f } });
        Out.Cylinders.push_back({ { Ox - Ex * 0.98f, YTop, Oz }, { Ox + Ex * 0.98f, YTop, Oz }, 0.020f, { 0.275f, 0.132f, 0.052f, 0.96f } });
        Out.Cylinders.push_back({ { Ox - Ex * 1.12f, YBot, Oz }, { Ox + Ex * 1.12f, YBot, Oz }, 0.017f, { 0.275f, 0.132f, 0.052f, 0.92f } });
        Out.Cylinders.push_back({ { Mx, TopY - 0.05f, Mz }, { Ox - Ex * 0.90f, YTop, Oz }, 0.011f, { 0.445f, 0.365f, 0.235f, 0.80f } });
        Out.Cylinders.push_back({ { Mx, TopY - 0.05f, Mz }, { Ox + Ex * 0.90f, YTop, Oz }, 0.011f, { 0.445f, 0.365f, 0.235f, 0.80f } });
        Out.Rings.push_back({ { Mx, 0.02f, Mz }, 0.22f, 24u, { 0.275f, 0.148f, 0.062f, 0.72f } });
    }
    else if (Spec.StrandVariant == 4u && Spec.SubMode != 2u)
    {
        // Dark Walnut Doorway Lintel Header + Slender Left & Right Door Jamb Posts (linearized exact ParticleEditor.js:1836)
        const float YTop = Oy + DynH;
        Out.Cylinders.push_back({ { Ox - Ex * 1.08f, YTop, Oz }, { Ox + Ex * 1.08f, YTop, Oz }, 0.030f, { 0.105f, 0.044f, 0.018f, 0.98f } });
        Out.Cylinders.push_back({ { Ox - Ex * 1.06f, 0.0f, Oz }, { Ox - Ex * 1.06f, YTop, Oz }, 0.028f, { 0.072f, 0.029f, 0.012f, 0.92f } });
        Out.Cylinders.push_back({ { Ox + Ex * 1.06f, 0.0f, Oz }, { Ox + Ex * 1.06f, YTop, Oz }, 0.028f, { 0.072f, 0.029f, 0.012f, 0.92f } });
    }

    return Out;
}

} // namespace Frontier::ParticleSpace
