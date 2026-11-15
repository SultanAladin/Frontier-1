/*============================================================================================================================================
                                                           SPLINEENSEMBLESOLVER.CPP
============================================================================================================================================*/
// 🧩 Exact 1:1 C++20 implementation of HTML Particle Studio Curl/Bokeh Stream Ribbons, Spiderweb Catenary Splines, and Dielectric Arcs

#include "Engine/ParticleSpace/Dynamics/SplineEnsembleSolver.h"
#include "Engine/ParticleSpace/ParticleVectorMath.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

using Vec3 = Vector3f;

inline float SmoothClamp(float A, float B, float X) noexcept
{
    const float T = std::clamp((X - A) / std::max(1e-5f, B - A), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

inline float Hash2(float Seed, float K) noexcept
{
    const float S = std::sin(Seed * 12.9898f + K * 78.233f) * 43758.5453123f;
    return S - std::floor(S);
}

inline float ArcHash(float N, float S) noexcept
{
    const float X = std::sin(N * 127.1f + S * 311.7f) * 43758.5453123f;
    return X - std::floor(X);
}

inline float SrgbToLinear(float C) noexcept
{
    return std::pow(std::clamp(C, 0.0f, 1.0f), 2.2f);
}

inline void BuildOrthonormalBasis(Vec3 Dir, Vec3& OutU, Vec3& OutV, Vec3& OutW) noexcept
{
    OutW = Norm3(Dir);
    const Vec3 Up = (std::fabs(OutW.Y) < 0.99f) ? Vec3 { 0.0f, 1.0f, 0.0f } : Vec3 { 1.0f, 0.0f, 0.0f };
    OutU = Norm3(Cross3(Up, OutW));
    OutV = Cross3(OutW, OutU);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  EXACT ANALYTIC CURL & STREAM / SURFACE-POUR VELOCITY FIELDS
//------------------------------------------------------------------------------------------------------------------------

Vec3 AnalyticCurl3(float X, float Y, float Z) noexcept
{
    const float dAz_dy = 1.15f * std::cos(X * 1.1f + Y * 1.15f) - 1.1f * std::sin(Y * 2.2f - Z * 1.6f);
    const float dAy_dz = 1.2f  * std::cos(Z * 1.2f + X * 0.95f);
    const float dAx_dz = 0.9f  * std::cos(Y * 1.3f + Z * 0.9f) - 1.2f * std::sin(Z * 2.4f - X * 1.7f);
    const float dAz_dx = 1.1f  * std::cos(X * 1.1f + Y * 1.15f);
    const float dAy_dx = 0.95f * std::cos(Z * 1.2f + X * 0.95f) - 1.15f * std::sin(X * 2.3f - Y * 1.8f);
    const float dAx_dy = 1.3f  * std::cos(Y * 1.3f + Z * 0.9f);
    return { dAz_dy - dAy_dz, dAx_dz - dAz_dx, dAy_dx - dAx_dy };
}

Vec3 StreamRibbonVel(
    Vec3 Pos, float T, float Freq, float Amp, Vec3 Org, Vec3 Ext) noexcept
{
    const Vec3 Rel = Pos - Org;
    const float F  = std::max(0.08f, Freq);
    const float DrX =  T * 0.22f;
    const float DrY = -T * 0.14f;
    const float DrZ =  T * 0.18f;
    const Vec3 C1 = AnalyticCurl3(Pos.X * F * 0.52f + DrX, Pos.Y * F * 0.52f + DrY, Pos.Z * F * 0.52f + DrZ);
    const Vec3 C2 = AnalyticCurl3(
        Pos.X * F * 1.12f - DrX * 0.75f + 2.4f,
        Pos.Y * F * 1.12f - DrY * 0.75f - 1.3f,
        Pos.Z * F * 1.12f - DrZ * 0.75f + 0.9f);
    const float Sx = 0.45f * std::cos(Rel.Z * F * 0.75f + 0.5f * std::sin(Rel.Y * F + T * 0.25f));
    const float Sy = 0.34f * std::sin(Rel.X * F * 0.85f - Rel.Z * F * 0.45f + T * 0.18f);
    const float Sz = 0.45f * std::sin(Rel.X * F * 0.75f + 0.5f * std::cos(Rel.Z * F - T * 0.22f));
    const float Pull = Amp * 0.38f;
    return {
        (C1.X * 0.85f + C2.X * 0.32f + Sx * 0.68f) * Amp - (Rel.X / std::max(Ext.X * Ext.X, 1.2f)) * Pull,
        (C1.Y * 0.85f + C2.Y * 0.32f + Sy * 0.68f) * Amp - (Rel.Y / std::max(Ext.Y * Ext.Y, 1.2f)) * Pull,
        (C1.Z * 0.85f + C2.Z * 0.32f + Sz * 0.68f) * Amp - (Rel.Z / std::max(Ext.Z * Ext.Z, 1.2f)) * Pull
    };
}

Vec3 ProjectObjSurface(Vec3 Rel, uint8_t ObjSh, Vec3 Ext) noexcept
{
    if (ObjSh == 1u)
    {
        const float R = std::max(Ext.X, 0.35f) * 1.02f;
        const float D = std::max(1e-4f, Len3(Rel));
        return Rel * (R / D);
    }
    if (ObjSh == 2u)
    {
        const float R   = std::max(Ext.X, 0.35f) * 1.02f;
        const float Hy  = std::max(Ext.Y, 0.35f);
        const float Rxz = std::hypot(Rel.X, Rel.Z);
        if (Rel.Y >= Hy * 0.98f && Rxz < R * 0.96f) return { Rel.X, Hy * 1.01f, Rel.Z };
        const float Dx = (Rxz > 1e-4f) ? (Rel.X / Rxz) : 1.0f;
        const float Dz = (Rxz > 1e-4f) ? (Rel.Z / Rxz) : 0.0f;
        return { Dx * R, std::clamp(Rel.Y, -Hy, Hy), Dz * R };
    }
    const float Cx = std::clamp(Rel.X, -Ext.X, Ext.X);
    const float Cy = std::clamp(Rel.Y, -Ext.Y, Ext.Y);
    const float Cz = std::clamp(Rel.Z, -Ext.Z, Ext.Z);
    const float Qx = std::fabs(Cx / std::max(Ext.X, 0.1f));
    const float Qy = std::fabs(Cy / std::max(Ext.Y, 0.1f));
    const float Qz = std::fabs(Cz / std::max(Ext.Z, 0.1f));
    if (Qy >= Qx && Qy >= Qz) return { Cx, (Cy >= 0.0f ? Ext.Y : -Ext.Y) * 1.015f, Cz };
    if (Qx >= Qz)             return { (Cx >= 0.0f ? Ext.X : -Ext.X) * 1.015f, Cy, Cz };
    return { Cx, Cy, (Cz >= 0.0f ? Ext.Z : -Ext.Z) * 1.015f };
}

Vec3 ObjSurfaceNormal(Vec3 Rel, uint8_t ObjSh, Vec3 Ext) noexcept
{
    if (ObjSh == 1u)
    {
        return Norm3(Rel);
    }
    if (ObjSh == 2u)
    {
        const float Rxz = std::hypot(Rel.X, Rel.Z);
        if (Rel.Y >= Ext.Y * 0.98f && Rxz < std::max(Ext.X, 0.35f) * 0.96f) return { 0.0f, 1.0f, 0.0f };
        const float D = std::max(1e-4f, Rxz);
        return { Rel.X / D, 0.0f, Rel.Z / D };
    }
    const float Qx = std::fabs(Rel.X / std::max(Ext.X, 0.1f));
    const float Qy = std::fabs(Rel.Y / std::max(Ext.Y, 0.1f));
    const float Qz = std::fabs(Rel.Z / std::max(Ext.Z, 0.1f));
    if (Qy >= Qx && Qy >= Qz) return { 0.0f, Rel.Y >= 0.0f ? 1.0f : -1.0f, 0.0f };
    if (Qx >= Qz)             return { Rel.X >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f };
    return { 0.0f, 0.0f, Rel.Z >= 0.0f ? 1.0f : -1.0f };
}

Vec3 SurfacePourVel(
    Vec3 Pos, Vec3 Org, Vec3 Ext, uint8_t ObjSh, uint8_t AxIdx,
    float T, float Freq, float Amp, float Seed) noexcept
{
    const Vec3 PD  = (AxIdx == 0u) ? Vec3 { 1.0f, 0.0f, 0.0f } : ((AxIdx == 2u) ? Vec3 { 0.0f, 0.0f, 1.0f } : Vec3 { 0.0f, -1.0f, 0.0f });
    const Vec3 Rel = Pos - Org;
    const Vec3 N   = ObjSurfaceNormal(Rel, ObjSh, Ext);
    const float DrX =  T * 0.20f;
    const float DrY = -T * 0.16f;
    const float DrZ =  T * 0.18f;
    const float F   = std::max(0.1f, Freq);
    const Vec3 C    = AnalyticCurl3(Pos.X * F + DrX, Pos.Y * F + DrY, Pos.Z * F + DrZ);

    const float DotRP = Dot3(Rel, PD);
    Vec3 Rad = Rel - PD * DotRP;
    const float RL = Len3(Rad);
    if (RL < 0.04f)
    {
        const float A = Seed * 0.0173f;
        Vec3 BU {}, BV {}, BW {};
        BuildOrthonormalBasis(PD, BU, BV, BW);
        Rad = BU * std::cos(A) + BV * std::sin(A);
    }
    else
    {
        Rad = Rad * (1.0f / RL);
    }

    const float DotNP    = Dot3(N, PD * -1.0f);
    const float NearTop  = std::clamp((DotNP - 0.65f) / 0.34f, 0.0f, 1.0f);
    const float RadBoost = Amp * (0.35f + 0.95f * NearTop * NearTop * (3.0f - 2.0f * NearTop));
    const Vec3  Drive    = PD * Amp + C * (Amp * 0.45f) + Rad * RadBoost;
    return Drive - N * Dot3(Drive, N);
}

inline void MarkSlotAlive(ParticleSlot& Slot, bool Alive) noexcept
{
    if (Alive) Slot.PackedTraits |= ParticleTraitAliveBit;
    else       Slot.PackedTraits &= ~ParticleTraitAliveBit;
}

inline void SampleFieldVelocityAccel(
    const Frontier::SimulationSpace::ForceFields::ForceFieldAccumulator& Fields,
    Vec3 Pos,
    const float AmbientWindMps[3],
    Vec3& OutVel,
    Vec3& OutAccel,
    float& OutDamp) noexcept
{
    OutVel   = { AmbientWindMps[0], AmbientWindMps[1], AmbientWindMps[2] };
    OutAccel = { 0.0f, 0.0f, 0.0f };
    OutDamp  = 0.0f;
    const Frontier::SimulationSpace::ForceFields::SpatialVector3 Pt  { Pos.X, Pos.Y, Pos.Z };
    const Frontier::SimulationSpace::ForceFields::SpatialVector3 Vel { 0.0f, 0.0f, 0.0f };
    const auto Eval = Fields.QueryForceFieldSample(Pt, Vel, 0.0);
    OutVel.X   += Eval.AdvectionVelocity.X;
    OutVel.Y   += Eval.AdvectionVelocity.Y;
    OutVel.Z   += Eval.AdvectionVelocity.Z;
    OutAccel.X  = Eval.LinearAcceleration.X;
    OutAccel.Y  = Eval.LinearAcceleration.Y;
    OutAccel.Z  = Eval.LinearAcceleration.Z;
    OutDamp     = Eval.DragDecayRate;
}

void SeedCurlParticle(
    uint32_t                SlotIdx,
    uint32_t                RestartCount,
    bool                    PreseedCurtain,
    const ParticleRig&      Rig,
    const CurlManifoldSpec& Spec,
    Vec3                    Org,
    float                   ElapsedSeconds,
    ParticleSlot&           Slot,
    std::span<StrandVertex> Strands,
    uint32_t                VertsPerStrand) noexcept
{
    const float Sd   = static_cast<float>(SlotIdx) * 17.31f + static_cast<float>(RestartCount) * 73.19f + 1.0f;
    const float U0   = Hash2(Sd, 1.1f);
    const float U1   = Hash2(Sd, 2.3f);
    const float U2   = Hash2(Sd, 3.7f);
    const float U3   = Hash2(Sd, 4.9f);
    const float U4   = Hash2(Sd, 6.1f);
    const float U5   = Hash2(Sd, 7.3f);
    const float U6   = Hash2(Sd, 8.9f);

    const float LifeMin = std::max(0.4f, Rig.Kinematics.LifeMinSeconds);
    const float LifeMax = std::max(LifeMin, Rig.Kinematics.LifeMaxSeconds);
    const float Life    = LifeMin + (LifeMax - LifeMin) * U3;
    const float Freq    = std::max(0.08f, Spec.FrequencyPerM);
    const float Amp     = Spec.DriveSpeedMps;
    const float Rate    = Spec.EvolutionRateHz;
    const float FR      = ElapsedSeconds * Rate;
    const Vec3  Ext     {
        std::max(0.45f, Spec.BoxHalfMeters[0]),
        std::max(0.45f, Spec.BoxHalfMeters[1]),
        std::max(0.45f, Spec.BoxHalfMeters[2])
    };

    Vec3 Pos = Org;
    Vec3 Vel { 0.0f, 0.0f, 0.0f };
    float SpawnAge = 0.0f;
    float Crest    = 0.5f;
    float Wake     = 1.0f;
    float Pulse    = 1.0f;

    const size_t StartIdx = static_cast<size_t>(SlotIdx) * static_cast<size_t>(VertsPerStrand);
    const bool HasTrailBuf = (VertsPerStrand >= 3u) && (StartIdx + VertsPerStrand <= Strands.size());

    if (Spec.ManifoldMode == 3u)
    {
        if (Spec.PulseMode > 0u)
        {
            const uint8_t AxI = static_cast<uint8_t>(Spec.PulseAxis % 3u);
            const Vec3 PD     = (AxI == 0u) ? Vec3 { 1.0f, 0.0f, 0.0f } : ((AxI == 2u) ? Vec3 { 0.0f, 0.0f, 1.0f } : Vec3 { 0.0f, -1.0f, 0.0f });
            Vec3 BU {}, BV {}, BW {};
            BuildOrthonormalBasis(PD, BU, BV, BW);
            const float Phi   = U0 * kTwoPi;
            const float UFrac = PreseedCurtain ? (0.04f + U1 * 0.84f) : (0.04f + U1 * 0.16f);
            Vec3 Rel { 0.0f, 0.0f, 0.0f };
            if (Spec.ObjectShape == 1u)
            {
                const float R     = std::max(Ext.X, 0.35f) * 1.02f;
                const float Theta = std::clamp(UFrac * 2.95f + 0.08f, 0.08f, 3.04f);
                const float Ct    = std::cos(Theta);
                const float St    = std::sin(Theta);
                const float Cp    = std::cos(Phi);
                const float Sp    = std::sin(Phi);
                Rel = (PD * -Ct + (BU * Cp + BV * Sp) * St) * R;
            }
            else if (Spec.ObjectShape == 2u)
            {
                const float R  = std::max(Ext.X, 0.35f) * 1.02f;
                const float Hy = std::max(Ext.Y, 0.35f);
                if (PreseedCurtain && UFrac > 0.2f)
                    Rel = { std::cos(Phi) * R, Hy - ((UFrac - 0.2f) / 0.8f) * 1.85f * Hy, std::sin(Phi) * R };
                else
                {
                    const float Rr = R * (0.12f + 0.86f * std::sqrt(U2));
                    Rel = { std::cos(Phi) * Rr, Hy * 1.01f, std::sin(Phi) * Rr };
                }
            }
            else
            {
                const float R = std::max(Ext.X, 0.35f);
                Rel = ProjectObjSurface({ (U0 - 0.5f) * 2.0f * R, (U1 - 0.5f) * 2.0f * Ext.Y, (U2 - 0.5f) * 2.0f * Ext.Z }, 0u, Ext);
            }
            Pos   = Org + Rel;
            Vel   = SurfacePourVel(Pos, Org, Ext, Spec.ObjectShape, AxI, FR, Freq, Amp, Sd * 9.7f);
            Crest = std::min(1.0f, Len3(Vel) / std::max(Amp * 1.25f, 0.2f));
            if (PreseedCurtain) SpawnAge = U4 * Life * 0.72f;
            if (HasTrailBuf)
            {
                Vec3 TPos = Pos;
                constexpr float StepDt = 0.036f;
                for (uint32_t K = 0u; K < VertsPerStrand; ++K)
                {
                    const Vec3 Prj = ProjectObjSurface(TPos - Org, Spec.ObjectShape, Ext);
                    TPos = Org + Prj;
                    Strands[StartIdx + K].PositionMeters[0] = TPos.X;
                    Strands[StartIdx + K].PositionMeters[1] = TPos.Y;
                    Strands[StartIdx + K].PositionMeters[2] = TPos.Z;
                    Strands[StartIdx + K].NormalisedSpanU   = static_cast<float>(K) / static_cast<float>(std::max<uint32_t>(1u, VertsPerStrand - 1u));
                    const Vec3 Bv = SurfacePourVel(TPos, Org, Ext, Spec.ObjectShape, AxI, FR - static_cast<float>(K) * StepDt * Rate, Freq, Amp, Sd * 9.7f);
                    TPos = TPos - Bv * StepDt;
                }
            }
        }
        else
        {
            Pos = {
                Org.X + (U0 * 2.0f - 1.0f) * Ext.X,
                std::max(0.08f, Org.Y + (U1 * 2.0f - 1.0f) * Ext.Y),
                Org.Z + (U2 * 2.0f - 1.0f) * Ext.Z
            };
            Vel   = StreamRibbonVel(Pos, FR, Freq, Amp, Org, Ext);
            Crest = std::min(1.0f, Len3(Vel) / std::max(Amp * 1.4f, 0.2f));
            if (PreseedCurtain) SpawnAge = U4 * Life * 0.78f;
            if (HasTrailBuf)
            {
                Vec3 TPos = Pos;
                constexpr float StepDt = 0.044f;
                for (uint32_t K = 0u; K < VertsPerStrand; ++K)
                {
                    Strands[StartIdx + K].PositionMeters[0] = TPos.X;
                    Strands[StartIdx + K].PositionMeters[1] = TPos.Y;
                    Strands[StartIdx + K].PositionMeters[2] = TPos.Z;
                    Strands[StartIdx + K].NormalisedSpanU   = static_cast<float>(K) / static_cast<float>(std::max<uint32_t>(1u, VertsPerStrand - 1u));
                    const Vec3 Bv = StreamRibbonVel(TPos, FR - static_cast<float>(K) * StepDt * Rate, Freq, Amp, Org, Ext);
                    TPos.X -= Bv.X * StepDt;
                    TPos.Y  = std::max(0.08f, TPos.Y - Bv.Y * StepDt);
                    TPos.Z -= Bv.Z * StepDt;
                }
            }
        }
    }
    else if (Spec.ManifoldMode == 2u)
    {
        // Spherical Curl Plasma shell
        const float Rad   = std::max(0.35f, Spec.ShellRadiusM);
        const float CosT  = U0 * 2.0f - 1.0f;
        const float SinT  = std::sqrt(std::max(0.0f, 1.0f - CosT * CosT));
        const float Phi   = U1 * kTwoPi;
        const Vec3  Nrm   { SinT * std::cos(Phi), CosT, SinT * std::sin(Phi) };
        Pos = Org + Nrm * Rad;
        const float DriftX =  ElapsedSeconds * Rate * 1.45f;
        const float DriftY = -ElapsedSeconds * Rate * 0.85f;
        const float DriftZ =  ElapsedSeconds * Rate * 1.15f;
        const Vec3 Q  { Pos.X * Freq + DriftX, Pos.Y * Freq + DriftY, Pos.Z * Freq + DriftZ };
        const Vec3 C1 = AnalyticCurl3(Q.X, Q.Y, Q.Z);
        const Vec3 C2 = AnalyticCurl3(Q.X * 2.2f - DriftX, Q.Y * 2.2f + DriftY, Q.Z * 2.2f - DriftZ);
        Vec3 Cv = (C1 + C2 * 0.45f) * (Amp * 0.55f);
        Cv = Cv - Nrm * Dot3(Cv, Nrm);
        Vel   = Cv;
        Crest = std::min(1.0f, Len3(Vel) / std::max(Amp * 1.15f, 0.2f));
        if (PreseedCurtain) SpawnAge = U4 * Life * 0.82f;
        if (HasTrailBuf)
        {
            for (uint32_t K = 0u; K < VertsPerStrand; ++K)
            {
                const Vec3 Back = Org + Norm3((Pos - Vel * (0.025f * static_cast<float>(K))) - Org) * Rad;
                Strands[StartIdx + K].PositionMeters[0] = Back.X;
                Strands[StartIdx + K].PositionMeters[1] = Back.Y;
                Strands[StartIdx + K].PositionMeters[2] = Back.Z;
                Strands[StartIdx + K].NormalisedSpanU   = static_cast<float>(K) / static_cast<float>(std::max<uint32_t>(1u, VertsPerStrand - 1u));
            }
        }
    }
    else
    {
        // Planar Curl Wave Field (ManifoldMode == 1)
        const Vec3 BoxH {
            std::max(0.25f, Spec.BoxHalfMeters[0]),
            std::max(0.10f, Spec.BoxHalfMeters[1]),
            std::max(0.25f, Spec.BoxHalfMeters[2])
        };
        Pos = {
            Org.X + (U0 * 2.0f - 1.0f) * BoxH.X,
            std::max(0.06f, Org.Y + (U1 * 2.0f - 1.0f) * BoxH.Y),
            Org.Z + (U2 * 2.0f - 1.0f) * BoxH.Z
        };
        const float DriftX =  ElapsedSeconds * Rate * 1.45f;
        const float DriftY = -ElapsedSeconds * Rate * 0.85f;
        const float DriftZ =  ElapsedSeconds * Rate * 1.15f;
        const Vec3 Q  { Pos.X * Freq + DriftX, Pos.Y * Freq + DriftY, Pos.Z * Freq + DriftZ };
        const Vec3 C1 = AnalyticCurl3(Q.X, Q.Y, Q.Z);
        const Vec3 C2 = AnalyticCurl3(Q.X * 2.2f - DriftX, Q.Y * 2.2f + DriftY, Q.Z * 2.2f - DriftZ);
        Vel   = (C1 + C2 * 0.45f) * (Amp * 0.55f);
        Crest = std::min(1.0f, Len3(Vel) / std::max(Amp * 1.15f, 0.2f));
        if (PreseedCurtain) SpawnAge = U4 * Life * 0.82f;
        if (HasTrailBuf)
        {
            for (uint32_t K = 0u; K < VertsPerStrand; ++K)
            {
                const Vec3 Back = Pos - Vel * (0.025f * static_cast<float>(K));
                Strands[StartIdx + K].PositionMeters[0] = Back.X;
                Strands[StartIdx + K].PositionMeters[1] = std::max(0.04f, Back.Y);
                Strands[StartIdx + K].PositionMeters[2] = Back.Z;
                Strands[StartIdx + K].NormalisedSpanU   = static_cast<float>(K) / static_cast<float>(std::max<uint32_t>(1u, VertsPerStrand - 1u));
            }
        }
    }

    Slot.PositionMeters[0] = Pos.X;
    Slot.PositionMeters[1] = Pos.Y;
    Slot.PositionMeters[2] = Pos.Z;
    Slot.VelocityMps[0]    = Vel.X;
    Slot.VelocityMps[1]    = Vel.Y;
    Slot.VelocityMps[2]    = Vel.Z;
    Slot.AgeSeconds        = SpawnAge;
    Slot.LifespanSeconds   = Life;
    Slot.SizeMeters        = Rig.Kinematics.SizeBirthMeters;
    Slot.SeedScalar        = U5;
    Slot.SpinRadians       = static_cast<float>(RestartCount);
    Slot.AuxScalar         = Crest;
    Slot.OriginAnchor[0]   = U6;    // Per-particle bokeh DoF [0..1]
    Slot.OriginAnchor[1]   = Pulse; // Object-pour wave boost
    Slot.OriginAnchor[2]   = Wake;  // Object-pour edge fade
    MarkSlotAlive(Slot, true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  EXACT SPIDERWEB CATENARY SPLINE HELPERS
//------------------------------------------------------------------------------------------------------------------------

constexpr float kOrbAnchorAngles[6] = { 0.14f, 1.12f, 2.28f, 3.32f, 4.34f, 5.42f };
constexpr float kOrbAnchorRadii[6]  = { 1.28f, 1.14f, 1.32f, 1.16f, 1.26f, 1.18f };

Vec3 OrbBridgeEdgeLocal(float Theta, float R) noexcept
{
    const float Dx = std::cos(Theta);
    const float Dy = std::sin(Theta);
    float BestT    = R * 1.15f;
    for (int G = 0; G < 6; ++G)
    {
        const int   G1  = (G + 1) % 6;
        const float Ax  = std::cos(kOrbAnchorAngles[G])  * R * kOrbAnchorRadii[G];
        const float Ay  = std::sin(kOrbAnchorAngles[G])  * R * kOrbAnchorRadii[G];
        const float Bx  = std::cos(kOrbAnchorAngles[G1]) * R * kOrbAnchorRadii[G1];
        const float By  = std::sin(kOrbAnchorAngles[G1]) * R * kOrbAnchorRadii[G1];
        const float Ex  = Bx - Ax;
        const float Ey  = By - Ay;
        const float Den = Dx * Ey - Dy * Ex;
        if (std::fabs(Den) > 1e-4f)
        {
            const float TRay  = (Ax * Ey - Ay * Ex) / Den;
            const float UEdge = (Ax * Dy - Ay * Dx) / Den;
            if (TRay > 0.0f && UEdge >= -0.02f && UEdge <= 1.02f && TRay < BestT) BestT = TRay;
        }
    }
    return { Dx * BestT, Dy * BestT, 0.0f };
}

Vec3 CobwebPocketPoint(float SeedVal, int Pocket, int Role, Vec3 Org, Vec3 BR, Vec3 BU, Vec3 BF, Vec3 Ext) noexcept
{
    float Px = 0.0f, Py = 0.0f, Pz = 0.0f;
    if (Pocket == 0)      { Px = -Ext.X * 0.52f; Py = Ext.Y * 0.36f; Pz = -Ext.Z * 0.28f; }
    else if (Pocket == 1) { Px =  Ext.X * 0.08f; Py = Ext.Y * 0.46f; Pz =  Ext.Z * 0.08f; }
    else                  { Px =  Ext.X * 0.56f; Py = Ext.Y * 0.22f; Pz = -Ext.Z * 0.20f; }

    const float U0 = Hash2(SeedVal, 1.0f);
    const float U1 = Hash2(SeedVal, 2.0f);
    const float U2 = Hash2(SeedVal, 3.0f);
    float Lx = 0.0f, Ly = 0.0f, Lz = 0.0f;
    if (Role == 0)
    {
        Lx = Px + (U0 - 0.5f) * Ext.X * 0.88f;
        Ly = Py + Ext.Y * (0.42f + 0.28f * U1);
        Lz = Pz + (U2 - 0.5f) * Ext.Z * 0.65f;
    }
    else if (Role == 1)
    {
        Lx = Px - Ext.X * (0.38f + 0.38f * U0);
        Ly = Py + (U1 * 1.15f - 0.68f) * Ext.Y;
        Lz = Pz + (U2 - 0.5f) * Ext.Z * 0.72f;
    }
    else
    {
        Lx = Px + Ext.X * (0.28f + 0.44f * U0);
        Ly = Py + (U1 * 1.05f - 0.58f) * Ext.Y;
        Lz = Pz + (U2 - 0.5f) * Ext.Z * 0.72f;
    }
    return Org + BR * Lx + BU * Ly + BF * Lz;
}

struct CobwebChord
{
    Vec3  PointA {};
    Vec3  PointB {};
    float Sag    = 0.16f;
};

CobwebChord EvalCobwebChord(uint32_t CIdx, Vec3 Org, Vec3 BR, Vec3 BU, Vec3 BF, Vec3 Ext, float SagVal) noexcept
{
    const float Sd     = static_cast<float>(CIdx) * 37.0f + 13.0f;
    const int   Pocket = static_cast<int>(CIdx % 3u);
    const int   RA     = static_cast<int>(std::floor(Hash2(Sd, 5.0f) * 3.0f)) % 3;
    const int   RB     = (RA + 1 + static_cast<int>(std::floor(Hash2(Sd, 6.0f) * 2.0f))) % 3;
    const Vec3  A      = CobwebPocketPoint(Sd + 1.1f, Pocket, RA, Org, BR, BU, BF, Ext);
    const Vec3  B      = CobwebPocketPoint(Sd + 7.9f, Pocket, RB, Org, BR, BU, BF, Ext);
    const float Len    = Len3(B - A);
    const float Sag    = SagVal * (0.55f + 1.25f * Hash2(Sd, 8.0f)) * std::clamp(Len / 2.2f, 0.45f, 1.55f);
    return { A, B, Sag };
}

Vec3 EvalWebSway(
    Vec3 Q, float WindT, float WindScale, float Amp, float Freq, float WindCoupling,
    const float AmbientWindMps[3],
    const Frontier::SimulationSpace::ForceFields::ForceFieldAccumulator& Fields) noexcept
{
    Vec3 FVel {}, FAcc {};
    float Damp = 0.0f;
    SampleFieldVelocityAccel(Fields, Q, AmbientWindMps, FVel, FAcc, Damp);
    const Vec3 WindPart = FVel * (WindCoupling * WindScale * 0.045f) + FAcc * (0.025f * 0.42f);
    const Vec3 CurlPart = AnalyticCurl3(
        Q.X * Freq * 0.45f + WindT * 0.58f,
        Q.Y * Freq * 0.45f - WindT * 0.36f,
        Q.Z * Freq * 0.45f + WindT * 0.46f) * (Amp * 0.16f * WindScale);
    Vec3 Out = WindPart + CurlPart;
    const float Mag = Len3(Out);
    if (Mag > 0.38f) Out = Out * (0.38f / Mag);
    return Out;
}

struct WebStrandDesc
{
    bool  Visible   = false;
    Vec3  PointA    {};
    Vec3  PointB    {};
    Vec3  Tip       {};
    float Sag       = 0.0f;
    float WindScale = 0.12f;
    float TipWhip   = 0.0f;
    float Grow      = 1.0f;
    float Bead      = 0.0f;
    float Role      = 0.5f;
};

WebStrandDesc EvalSpiderwebStrand(
    const SpiderwebWeaveSpec& Spec,
    uint32_t                  SlotIdx,
    uint32_t                  Cap,
    Vec3                      Org,
    float                     TWeb,
    float                     WindT,
    const float               AmbientWindMps[3],
    const Frontier::SimulationSpace::ForceFields::ForceFieldAccumulator& Fields) noexcept
{
    Vec3 BR {}, BU {}, BF {};
    BuildOrthonormalBasis({ Spec.FacingDir[0], Spec.FacingDir[1], Spec.FacingDir[2] }, BR, BU, BF);
    const float Amp     = Spec.CurlAmpMps;
    const float Freq    = std::max(0.2f, Spec.CurlFreqPerM);
    const float WindC   = Spec.WindCoupling;
    const uint32_t Spokes = std::clamp<uint32_t>(Spec.SpokeCount, 8u, 32u);
    const float RingsT  = std::clamp(static_cast<float>(Spec.RingCount), 6.0f, 22.0f);
    const float SagVal  = std::max(0.0f, Spec.SilkSagMeters);

    auto Sway = [&](Vec3 Q, float Ws, float A = -1.0f, float F = -1.0f) -> Vec3 {
        return EvalWebSway(Q, WindT, Ws, A > 0.0f ? A : Amp, F > 0.0f ? F : Freq, WindC, AmbientWindMps, Fields);
    };

    if (!Spec.MessyAtticCobweb)
    {
        const float R      = std::max(0.45f, Spec.WebRadiusMeters);
        const Vec3 HubRest = Org + BU * (-0.05f * R);
        const Vec3 Hub     = HubRest + Sway(HubRest, 0.24f);

        if (SlotIdx < 6u)
        {
            const float T0  = 0.03f + static_cast<float>(SlotIdx) * 0.06f;
            const float Dur = 0.24f;
            if (TWeb < T0) return {};
            const uint32_t S1 = (SlotIdx + 1u) % 6u;
            const float Ax = std::cos(kOrbAnchorAngles[SlotIdx]) * R * kOrbAnchorRadii[SlotIdx];
            const float Ay = std::sin(kOrbAnchorAngles[SlotIdx]) * R * kOrbAnchorRadii[SlotIdx];
            const float Bx = std::cos(kOrbAnchorAngles[S1])      * R * kOrbAnchorRadii[S1];
            const float By = std::sin(kOrbAnchorAngles[S1])      * R * kOrbAnchorRadii[S1];
            const Vec3 RawA = Org + BR * Ax + BU * Ay;
            const Vec3 RawB = Org + BR * Bx + BU * By;
            const Vec3 PA   = RawA + Sway(RawA, 0.08f);
            const Vec3 PB   = RawB + Sway(RawB, 0.08f);
            const float Grow = std::clamp((TWeb - T0) / Dur, 0.0f, 1.0f);
            const Vec3 Tip   = Mix3(PA, PB, Grow);
            const float Len  = std::max(1e-4f, Len3(PB - PA));
            const float HFr  = std::clamp(std::hypot(PB.X - PA.X, PB.Z - PA.Z) / Len, 0.15f, 1.0f);
            return { true, PA, Tip, Tip, SagVal * 0.42f * HFr * Grow, 0.12f, 0.0f, Grow, Grow < 0.99f ? 1.0f : 0.0f, 0.12f };
        }
        if (SlotIdx < 12u)
        {
            const uint32_t G = SlotIdx - 6u;
            const float T0   = 0.25f + static_cast<float>(G) * 0.04f;
            const float Dur  = 0.18f;
            if (TWeb < T0) return {};
            const float Ax  = std::cos(kOrbAnchorAngles[G]) * R * kOrbAnchorRadii[G];
            const float Ay  = std::sin(kOrbAnchorAngles[G]) * R * kOrbAnchorRadii[G];
            const Vec3 RawA = Org + BR * Ax + BU * Ay;
            const Vec3 PA   = RawA + Sway(RawA, 0.08f);
            const Vec3 PB   = Org + BR * (Ax * 1.18f) + BU * (Ay * 1.18f);
            const float Grow = std::clamp((TWeb - T0) / Dur, 0.0f, 1.0f);
            const Vec3 Tip   = Mix3(PA, PB, Grow);
            return { true, PA, Tip, Tip, SagVal * 0.15f * Grow, 0.08f, 0.0f, Grow, Grow < 0.99f ? 1.0f : 0.0f, 0.08f };
        }
        if (SlotIdx < 12u + Spokes)
        {
            const uint32_t SIdx = SlotIdx - 12u;
            const float T0  = 0.42f + static_cast<float>(SIdx) * 0.030f;
            const float Dur = 0.22f;
            if (TWeb < T0) return {};
            const float Jit   = (Hash2(static_cast<float>(SIdx) * 19.0f + 3.0f, 1.0f) - 0.5f) * 0.032f;
            const float Theta = (static_cast<float>(SIdx) / static_cast<float>(Spokes)) * kTwoPi + Jit;
            const Vec3  Loc   = OrbBridgeEdgeLocal(Theta, R);
            const Vec3  RawB  = Org + BR * Loc.X + BU * Loc.Y;
            const Vec3  PB    = RawB + Sway(RawB, 0.08f);
            const float Grow  = std::clamp((TWeb - T0) / Dur, 0.0f, 1.0f);
            const Vec3  Tip   = Mix3(Hub, PB, Grow);
            const float Len   = std::max(1e-4f, Len3(PB - Hub));
            const float HFr   = std::clamp(std::hypot(PB.X - Hub.X, PB.Z - Hub.Z) / Len, 0.12f, 1.0f);
            return { true, Hub, Tip, Tip, SagVal * 0.20f * HFr * Grow, 0.16f, 0.0f, Grow, Grow < 0.99f ? 1.0f : 0.0f, 0.22f };
        }

        const uint32_t SpiralIdx = SlotIdx - 12u - Spokes;
        const uint32_t MaxAvail  = (Cap > 12u + Spokes) ? (Cap - 12u - Spokes) : 1u;
        const uint32_t GoalSegs  = std::min<uint32_t>(MaxAvail, Spokes * static_cast<uint32_t>(std::round(RingsT)));
        if (SpiralIdx >= GoalSegs) return {};
        const float T0  = 1.05f + static_cast<float>(SpiralIdx) * (6.2f / static_cast<float>(std::max<uint32_t>(1u, GoalSegs)));
        const float Dur = 0.028f;
        if (TWeb < T0) return {};

        const uint32_t S0      = SpiralIdx % Spokes;
        const uint32_t S1      = (SpiralIdx + 1u) % Spokes;
        const uint32_t HubSegs = Spokes * 2u;
        const float UTotal0    = std::min(1.0f, static_cast<float>(SpiralIdx) / static_cast<float>(GoalSegs));
        auto RadialFrac = [&](uint32_t Idx) -> float {
            if (Idx < HubSegs) return 0.035f + 0.07f * (static_cast<float>(Idx) / static_cast<float>(std::max<uint32_t>(1u, HubSegs)));
            const float Den = static_cast<float>(std::max<uint32_t>(1u, GoalSegs - HubSegs));
            return 0.20f + 0.72f * std::pow(std::max(0.0f, static_cast<float>(Idx - HubSegs) / Den), 0.92f);
        };
        const float RF0 = RadialFrac(SpiralIdx);
        const float RF1 = RadialFrac(SpiralIdx + 1u);
        const float Th0 = (static_cast<float>(S0) / static_cast<float>(Spokes)) * kTwoPi + (Hash2(static_cast<float>(S0) * 19.0f + 3.0f, 1.0f) - 0.5f) * 0.032f;
        const float Th1 = (static_cast<float>(S1) / static_cast<float>(Spokes)) * kTwoPi + (Hash2(static_cast<float>(S1) * 19.0f + 3.0f, 1.0f) - 0.5f) * 0.032f;
        const Vec3  L0  = OrbBridgeEdgeLocal(Th0, R);
        const Vec3  L1  = OrbBridgeEdgeLocal(Th1, R);
        const Vec3  R0  = Org + BR * L0.X + BU * L0.Y;
        const Vec3  R1  = Org + BR * L1.X + BU * L1.Y;
        const Vec3  E0  = R0 + Sway(R0, 0.08f);
        const Vec3  E1  = R1 + Sway(R1, 0.08f);
        const float SpH0 = std::clamp(std::hypot(E0.X - Hub.X, E0.Z - Hub.Z) / std::max(1e-4f, Len3(E0 - Hub)), 0.12f, 1.0f);
        const float SpH1 = std::clamp(std::hypot(E1.X - Hub.X, E1.Z - Hub.Z) / std::max(1e-4f, Len3(E1 - Hub)), 0.12f, 1.0f);
        Vec3 PA = Mix3(Hub, E0, RF0); PA.Y -= 4.0f * SagVal * 0.20f * SpH0 * RF0 * (1.0f - RF0);
        Vec3 PB = Mix3(Hub, E1, RF1); PB.Y -= 4.0f * SagVal * 0.20f * SpH1 * RF1 * (1.0f - RF1);
        const float Grow   = std::clamp((TWeb - T0) / Dur, 0.0f, 1.0f);
        const Vec3  Tip    = Mix3(PA, PB, Grow);
        const float LenSeg = std::max(1e-4f, Len3(PB - PA));
        const float HSeg   = std::clamp(std::hypot(PB.X - PA.X, PB.Z - PA.Z) / LenSeg, 0.12f, 1.0f);
        const float SagSeg = SagVal * (0.22f + 0.55f * UTotal0) * HSeg * std::clamp(LenSeg / std::max(0.28f, R * 0.36f), 0.35f, 1.25f) * Grow;
        const float Bead   = (Grow < 0.99f) ? 1.0f : (((SpiralIdx % 3u) == 0u) ? 0.42f : 0.0f);
        return { true, PA, Tip, Tip, SagSeg, 0.07f, 0.0f, Grow, Bead, 0.45f + 0.50f * UTotal0 };
    }

    // Messy Attic Cobwebs (MessyAtticCobweb == true)
    const Vec3 Ext {
        std::max(0.65f, Spec.BoxHalfMeters[0]),
        std::max(0.65f, Spec.BoxHalfMeters[1]),
        std::max(0.65f, Spec.BoxHalfMeters[2])
    };
    const uint32_t NChords = std::min<uint32_t>(Cap / 2u, std::max<uint32_t>(16u, static_cast<uint32_t>(static_cast<float>(Cap) * std::clamp(static_cast<float>(Spokes) * 0.020f, 0.24f, 0.38f))));
    const uint32_t NTangle = std::min<uint32_t>(Cap > 16u ? Cap - 16u : Cap, std::max<uint32_t>(NChords + 1u, NChords + static_cast<uint32_t>(static_cast<float>(Cap) * std::clamp(RingsT * 0.032f, 0.28f, 0.42f))));

    if (SlotIdx < NChords)
    {
        const float T0  = 0.04f + (static_cast<float>(SlotIdx) / static_cast<float>(NChords)) * 1.45f;
        const float Dur = 0.28f;
        if (TWeb < T0) return {};
        const CobwebChord Ch = EvalCobwebChord(SlotIdx, Org, BR, BU, BF, Ext, SagVal);
        const float Grow = std::clamp((TWeb - T0) / Dur, 0.0f, 1.0f);
        const Vec3  Tip  = Mix3(Ch.PointA, Ch.PointB, Grow);
        return { true, Ch.PointA, Tip, Tip, Ch.Sag * Grow, 0.22f, 0.0f, Grow, Grow < 0.99f ? 0.85f : 0.0f, 0.20f };
    }
    if (SlotIdx < NTangle)
    {
        const uint32_t IdxT  = SlotIdx - NChords;
        const uint32_t SpanT = std::max<uint32_t>(1u, NTangle - NChords);
        const float T0  = 1.25f + (static_cast<float>(IdxT) / static_cast<float>(SpanT)) * 2.85f;
        const float Dur = 0.16f;
        if (TWeb < T0) return {};
        const float Sd       = static_cast<float>(SlotIdx) * 53.0f + 19.0f;
        const uint32_t CA    = static_cast<uint32_t>(std::floor(Hash2(Sd, 1.0f) * static_cast<float>(NChords))) % NChords;
        const uint32_t Pkt   = CA % 3u;
        const uint32_t Step3 = 1u + static_cast<uint32_t>(std::floor(Hash2(Sd, 2.0f) * std::max(2.0f, static_cast<float>(NChords) * 0.25f)));
        const uint32_t CB    = (Pkt + ((CA - Pkt) / 3u + Step3) * 3u) % NChords;
        const float UA       = 0.10f + 0.80f * Hash2(Sd, 3.0f);
        const float UB       = 0.10f + 0.80f * Hash2(Sd, 4.0f);
        const CobwebChord ChA = EvalCobwebChord(CA, Org, BR, BU, BF, Ext, SagVal);
        const CobwebChord ChB = EvalCobwebChord(CB, Org, BR, BU, BF, Ext, SagVal);
        Vec3 RawA = Mix3(ChA.PointA, ChA.PointB, UA); RawA.Y -= 4.0f * ChA.Sag * UA * (1.0f - UA);
        Vec3 RawB = Mix3(ChB.PointA, ChB.PointB, UB); RawB.Y -= 4.0f * ChB.Sag * UB * (1.0f - UB);
        const Vec3 PA   = RawA + Sway(RawA, 0.24f * 4.0f * UA * (1.0f - UA));
        const Vec3 PB   = RawB + Sway(RawB, 0.24f * 4.0f * UB * (1.0f - UB));
        const float Grow = std::clamp((TWeb - T0) / Dur, 0.0f, 1.0f);
        const Vec3  Tip  = Mix3(PA, PB, Grow);
        const float Len  = Len3(PB - PA);
        const float Sag  = SagVal * (0.45f + 0.95f * Hash2(Sd, 5.0f)) * std::clamp(Len / 1.8f, 0.3f, 1.4f) * Grow;
        return { true, PA, Tip, Tip, Sag, 0.38f, 0.0f, Grow, Grow < 0.99f ? 0.85f : (((IdxT % 4u) == 0u) ? 0.30f : 0.0f), 0.55f };
    }

    const uint32_t IdxD  = SlotIdx - NTangle;
    const uint32_t SpanD = std::max<uint32_t>(1u, Cap - NTangle);
    const float T0  = 3.15f + (static_cast<float>(IdxD) / static_cast<float>(SpanD)) * 1.95f;
    const float Dur = 0.22f;
    if (TWeb < T0) return {};
    const float Sd    = static_cast<float>(SlotIdx) * 71.0f + 41.0f;
    const uint32_t CP = static_cast<uint32_t>(std::floor(Hash2(Sd, 1.0f) * static_cast<float>(NChords))) % NChords;
    const float UP    = 0.16f + 0.68f * Hash2(Sd, 2.0f);
    const CobwebChord ChP = EvalCobwebChord(CP, Org, BR, BU, BF, Ext, SagVal);
    Vec3 RawP = Mix3(ChP.PointA, ChP.PointB, UP); RawP.Y -= 4.0f * ChP.Sag * UP * (1.0f - UP);
    const Vec3 PA    = RawP + Sway(RawP, 0.24f * 4.0f * UP * (1.0f - UP));
    const float DLen = 0.55f + 0.90f * Hash2(Sd, 3.0f);
    const Vec3  Dr   { (Hash2(Sd, 4.0f) - 0.5f) * 0.26f, -DLen, (Hash2(Sd, 5.0f) - 0.5f) * 0.26f };
    const Vec3  PB   = PA + Dr + Sway(PA + Dr, 0.85f, Amp * 1.25f, Freq * 1.2f);
    const float Grow = std::clamp((TWeb - T0) / Dur, 0.0f, 1.0f);
    const Vec3  Tip  = Mix3(PA, PB, Grow);
    return { true, PA, Tip, Tip, 0.05f * DLen * Grow, 0.22f, 1.15f, Grow, Grow < 0.99f ? 0.85f : 0.24f, 0.85f };
}

//------------------------------------------------------------------------------------------------------------------------
//                                  EXACT MIDPOINT-SUBDIVIDED DIELECTRIC LIGHTNING SOLVER
//------------------------------------------------------------------------------------------------------------------------

void SubdivideSparkRecursive(
    Vec3                   A,
    Vec3                   B,
    Vec3                   UDir,
    Vec3                   VDir,
    float                  Amp,
    float                  StrokeW,
    int                    Depth,
    float                  SeedStart,
    const float            CoolCol[3],
    float                  Inten,
    float                  ThermalUp,
    int                    ForkQuota,
    bool                   MainChain,
    DielectricArcGeometry& Out) noexcept
{
    const int Steps = 1 << std::clamp(Depth, 1, 6);
    std::vector<Vec3> Pts(static_cast<size_t>(Steps + 1));
    Pts[0]                    = A;
    Pts[static_cast<size_t>(Steps)] = B;

    int   Stride = Steps;
    float Scale  = Amp;
    int   Rng    = 1;
    for (int D = 0; D < Depth; ++D)
    {
        const int Half = Stride >> 1;
        for (int I = 0; I < Steps; I += Stride)
        {
            const Vec3 P0 = Pts[static_cast<size_t>(I)];
            const Vec3 P1 = Pts[static_cast<size_t>(I + Stride)];
            const Vec3 M  = (P0 + P1) * 0.5f;
            const float R1   = ArcHash(SeedStart + static_cast<float>(Rng) * 1.73f, 11.3f) - 0.5f;
            const float R2   = ArcHash(SeedStart + static_cast<float>(Rng) * 2.41f, 23.7f) - 0.5f;
            const float Kink = 0.65f + ArcHash(SeedStart + static_cast<float>(Rng) * 3.19f, 37.1f) * 0.85f;
            const float Du   = R1 * Scale * Kink;
            const float Dv   = R2 * Scale * Kink;
            const float MidT = static_cast<float>(I + Half) / static_cast<float>(Steps);
            const float Lift = std::sin(kPi * MidT) * (ThermalUp / static_cast<float>(std::max(1, Depth)));
            Pts[static_cast<size_t>(I + Half)] = {
                M.X + UDir.X * Du + VDir.X * Dv,
                M.Y + UDir.Y * Du + VDir.Y * Dv + Lift,
                M.Z + UDir.Z * Du + VDir.Z * Dv
            };
            ++Rng;
        }
        Stride = Half;
        Scale *= 0.54f;
    }

    for (int I = 0; I < Steps; ++I)
    {
        const float T0     = static_cast<float>(I) / static_cast<float>(Steps);
        const float T1     = static_cast<float>(I + 1) / static_cast<float>(Steps);
        const float Taper0 = MainChain ? (0.85f + 0.30f * std::sin(kPi * T0)) : std::max(0.18f, 1.0f - T0 * 0.82f);
        const float Taper1 = MainChain ? (0.85f + 0.30f * std::sin(kPi * T1)) : std::max(0.16f, 1.0f - T1 * 0.84f);
        const float Fade   = MainChain ? 1.0f : std::max(0.14f, 1.0f - T0 * 0.78f);
        const Vec3  P0     = Pts[static_cast<size_t>(I)];
        const Vec3  P1     = Pts[static_cast<size_t>(I + 1)];
        Out.Segments.push_back({
            { P0.X, P0.Y, P0.Z }, StrokeW * Taper0,
            { P1.X, P1.Y, P1.Z }, StrokeW * Taper1,
            { CoolCol[0], CoolCol[1], CoolCol[2] },
            Inten * Fade
        });
    }

    if (ForkQuota > 0 && Steps >= 8)
    {
        const Vec3  GapVec    = Norm3(B - A);
        const float SpanTotal = Len3(B - A);
        for (int F = 0; F < ForkQuota; ++F)
        {
            const float Ratio    = 0.18f + ((static_cast<float>(F) + 0.45f * ArcHash(SeedStart, 71.0f + static_cast<float>(F))) / static_cast<float>(ForkQuota)) * 0.64f;
            const int   Idx      = std::clamp(static_cast<int>(std::floor(Ratio * static_cast<float>(Steps))), 2, Steps - 2);
            const Vec3  Root     = Pts[static_cast<size_t>(Idx)];
            const float SideSign = (ArcHash(SeedStart, 91.0f + static_cast<float>(F)) > 0.5f ? 1.0f : -1.0f) * (0.48f + ArcHash(SeedStart, 95.0f + static_cast<float>(F)) * 0.62f);
            const float VertSign = (ArcHash(SeedStart, 103.0f + static_cast<float>(F)) - 0.44f) * 0.85f;
            const Vec3  FDir     = Norm3(GapVec * 0.78f + UDir * SideSign + VDir * VertSign);
            const float FLen     = std::min(1.6f, SpanTotal * (0.20f + ArcHash(SeedStart, 113.0f + static_cast<float>(F)) * 0.24f));
            const Vec3  Tip      = Root + FDir * FLen;
            Vec3 FU {}, FV {}, FW {};
            BuildOrthonormalBasis(FDir, FU, FV, FW);
            const int SubForks = ((F % 2 == 0) && Depth >= 5) ? 1 : 0;
            SubdivideSparkRecursive(
                Root, Tip, FU, FV, FLen * 0.24f, StrokeW * 0.52f, std::max(2, Depth - 2),
                SeedStart + 409.0f + static_cast<float>(F) * 67.0f,
                CoolCol, Inten * 0.68f, ThermalUp * 0.25f, SubForks, false, Out);
        }
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  PUBLIC SOLVER FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void StepCurlStreamRibbonEnsemble(
    const ParticleRig&                                          Rig,
    const CurlManifoldSpec&                                     Spec,
    const float                                                 OriginMeters[3],
    const float                                                 AmbientWindMps[3],
    const Frontier::SimulationSpace::ForceFields::ForceFieldAccumulator& Fields,
    std::span<ParticleSlot>                                     Slots,
    std::span<StrandVertex>                                     Strands,
    float                                                       ElapsedSeconds,
    float                                                       DeltaSeconds) noexcept
{
    const uint32_t Cap = static_cast<uint32_t>(Slots.size());
    if (Cap == 0u) return;

    uint32_t VertsPerStrand = 3u;
    if (const auto* Rib = std::get_if<RibbonStrandCarrier>(&Rig.Carrier))
        VertsPerStrand = std::max<uint32_t>(3u, Rib->VerticesPerStrand);

    const Vec3 Org  { OriginMeters[0], OriginMeters[1], OriginMeters[2] };
    const Vec3 Ext  {
        std::max(0.45f, Spec.BoxHalfMeters[0]),
        std::max(0.45f, Spec.BoxHalfMeters[1]),
        std::max(0.45f, Spec.BoxHalfMeters[2])
    };
    const float Dt   = std::clamp(DeltaSeconds, 1e-4f, 0.05f);
    const float Freq = std::max(0.08f, Spec.FrequencyPerM);
    const float Amp  = Spec.DriveSpeedMps;
    const float Rate = Spec.EvolutionRateHz;
    const float WindCoupling = Rig.Dynamics.ForceCouple.WindCouplingRate;
    const float DriftX =  ElapsedSeconds * Rate * 1.45f;
    const float DriftY = -ElapsedSeconds * Rate * 0.85f;
    const float DriftZ =  ElapsedSeconds * Rate * 1.15f;

    // Self-seed full live steady-state curtain on first step (matching HTML preseedCurtain)
    bool AnyAlive = false;
    for (uint32_t I = 0u; I < std::min<uint32_t>(Cap, 8u); ++I)
    {
        if (QueryParticleSlotAlive(Slots[I])) { AnyAlive = true; break; }
    }
    if (!AnyAlive)
    {
        for (uint32_t I = 0u; I < Cap; ++I)
        {
            SeedCurlParticle(I, 0u, true, Rig, Spec, Org, ElapsedSeconds, Slots[I], Strands, VertsPerStrand);
        }
    }

    for (uint32_t I = 0u; I < Cap; ++I)
    {
        ParticleSlot& Slot = Slots[I];
        if (!QueryParticleSlotAlive(Slot))
        {
            SeedCurlParticle(I, static_cast<uint32_t>(Slot.SpinRadians) + 1u, false, Rig, Spec, Org, ElapsedSeconds, Slot, Strands, VertsPerStrand);
            continue;
        }

        Slot.AgeSeconds += Dt;
        if (Slot.AgeSeconds >= Slot.LifespanSeconds)
        {
            SeedCurlParticle(I, static_cast<uint32_t>(Slot.SpinRadians) + 1u, false, Rig, Spec, Org, ElapsedSeconds, Slot, Strands, VertsPerStrand);
            continue;
        }

        Vec3 Pos { Slot.PositionMeters[0], Slot.PositionMeters[1], Slot.PositionMeters[2] };
        Vec3 Vel { Slot.VelocityMps[0],    Slot.VelocityMps[1],    Slot.VelocityMps[2]    };
        const size_t StartIdx = static_cast<size_t>(I) * static_cast<size_t>(VertsPerStrand);
        const bool HasTrail   = (StartIdx + VertsPerStrand <= Strands.size());

        Vec3 FVel {}, FAcc {};
        float ExtraDamp = 0.0f;
        SampleFieldVelocityAccel(Fields, Pos, AmbientWindMps, FVel, FAcc, ExtraDamp);

        if (Spec.ManifoldMode == 3u)
        {
            if (Spec.PulseMode > 0u)
            {
                const uint8_t AxI = static_cast<uint8_t>(Spec.PulseAxis % 3u);
                const Vec3 PD     = (AxI == 0u) ? Vec3 { 1.0f, 0.0f, 0.0f } : ((AxI == 2u) ? Vec3 { 0.0f, 0.0f, 1.0f } : Vec3 { 0.0f, -1.0f, 0.0f });
                Vec3 CvS = SurfacePourVel(Pos, Org, Ext, Spec.ObjectShape, AxI, ElapsedSeconds * Rate, Freq, Amp, Slot.SeedScalar * 65536.0f);
                CvS = CvS + FVel * (WindCoupling * 0.12f) + FAcc * 0.18f;
                const Vec3 Nrm = ObjSurfaceNormal(Pos - Org, Spec.ObjectShape, Ext);
                CvS = CvS - Nrm * Dot3(CvS, Nrm);
                const float Blend = std::min(1.0f, 8.0f * Dt);
                const float Keep  = std::exp(-ExtraDamp * Dt);
                Vel = (Vel + (CvS - Vel) * Blend) * Keep;
                Vel = Vel - Nrm * Dot3(Vel, Nrm);
                const Vec3 NextRel = ProjectObjSurface((Pos + Vel * Dt) - Org, Spec.ObjectShape, Ext);
                Pos = Org + NextRel;

                const float HalfSpan = std::max(0.35f, Spec.ObjectShape == 1u ? std::max(Ext.X, 0.35f) : (AxI == 0u ? Ext.X : (AxI == 1u ? Ext.Y : Ext.Z)));
                const float USurf    = std::clamp(0.5f + Dot3(NextRel, PD) / (2.0f * HalfSpan), 0.0f, 1.0f);
                if (USurf >= 0.97f)
                {
                    SeedCurlParticle(I, static_cast<uint32_t>(Slot.SpinRadians) + 1u, false, Rig, Spec, Org, ElapsedSeconds, Slot, Strands, VertsPerStrand);
                    continue;
                }

                float PulseB = 1.0f;
                if (Spec.PulseMode == 2u)
                {
                    const float Ph    = ElapsedSeconds * std::max(0.04f, Spec.PulseSpeedHz);
                    const float WaveU = (Ph - std::floor(Ph)) * 1.24f - 0.12f;
                    const float Dw    = (USurf - WaveU) / std::max(0.06f, Spec.PulseWidth);
                    PulseB = std::clamp(0.42f + 0.78f * std::exp(-Dw * Dw * 1.55f), 0.34f, 1.20f);
                }
                const float EdgeFade = SmoothClamp(0.0f, 0.08f, USurf) * (1.0f - SmoothClamp(0.86f, 1.0f, USurf));
                Slot.OriginAnchor[1] = PulseB;
                Slot.OriginAnchor[2] = std::clamp(PulseB * EdgeFade, 0.12f, 1.15f);
                Slot.AuxScalar       = std::min(1.0f, Len3(Vel) / std::max(Amp * 1.25f, 0.2f));

                if (HasTrail)
                {
                    const float KLerp = std::min(1.0f, 15.0f * Dt);
                    for (uint32_t K = VertsPerStrand - 1u; K >= 1u; --K)
                    {
                        const Vec3 Prev { Strands[StartIdx + K - 1u].PositionMeters[0], Strands[StartIdx + K - 1u].PositionMeters[1], Strands[StartIdx + K - 1u].PositionMeters[2] };
                        const Vec3 Cur  { Strands[StartIdx + K].PositionMeters[0],      Strands[StartIdx + K].PositionMeters[1],      Strands[StartIdx + K].PositionMeters[2]      };
                        const Vec3 Prj  = ProjectObjSurface(Mix3(Cur, Prev, KLerp) - Org, Spec.ObjectShape, Ext);
                        Strands[StartIdx + K].PositionMeters[0] = Org.X + Prj.X;
                        Strands[StartIdx + K].PositionMeters[1] = Org.Y + Prj.Y;
                        Strands[StartIdx + K].PositionMeters[2] = Org.Z + Prj.Z;
                    }
                    Strands[StartIdx].PositionMeters[0] = Pos.X;
                    Strands[StartIdx].PositionMeters[1] = Pos.Y;
                    Strands[StartIdx].PositionMeters[2] = Pos.Z;
                }
            }
            else
            {
                Vec3 Cv = StreamRibbonVel(Pos, ElapsedSeconds * Rate, Freq, Amp, Org, Ext);
                Cv = Cv + FVel * (WindCoupling * 0.14f) + FAcc * 0.18f;
                const float Blend = std::min(1.0f, 5.5f * Dt);
                const float Keep  = std::exp(-ExtraDamp * Dt);
                Vel = (Vel + (Cv - Vel) * Blend) * Keep;
                Pos.X += Vel.X * Dt;
                Pos.Y  = std::max(0.08f, Pos.Y + Vel.Y * Dt);
                Pos.Z += Vel.Z * Dt;
                Slot.OriginAnchor[1] = 1.0f;
                Slot.OriginAnchor[2] = 1.0f;
                Slot.AuxScalar       = std::min(1.0f, Len3(Vel) / std::max(Amp * 1.4f, 0.2f));

                if (HasTrail)
                {
                    const float KLerp = std::min(1.0f, 11.5f * Dt);
                    for (uint32_t K = VertsPerStrand - 1u; K >= 1u; --K)
                    {
                        const Vec3 Prev { Strands[StartIdx + K - 1u].PositionMeters[0], Strands[StartIdx + K - 1u].PositionMeters[1], Strands[StartIdx + K - 1u].PositionMeters[2] };
                        const Vec3 Cur  { Strands[StartIdx + K].PositionMeters[0],      Strands[StartIdx + K].PositionMeters[1],      Strands[StartIdx + K].PositionMeters[2]      };
                        const Vec3 Nx   = Mix3(Cur, Prev, KLerp);
                        Strands[StartIdx + K].PositionMeters[0] = Nx.X;
                        Strands[StartIdx + K].PositionMeters[1] = Nx.Y;
                        Strands[StartIdx + K].PositionMeters[2] = Nx.Z;
                    }
                    Strands[StartIdx].PositionMeters[0] = Pos.X;
                    Strands[StartIdx].PositionMeters[1] = Pos.Y;
                    Strands[StartIdx].PositionMeters[2] = Pos.Z;
                }
            }
        }
        else
        {
            // Framerate-independent curved 3-vertex trail follow so Curl 1/2 streaks sweep gracefully
            if (HasTrail)
            {
                const float KLerp = std::min(1.0f, (Spec.ManifoldMode == 2u ? 10.5f : 12.5f) * Dt);
                for (uint32_t K = VertsPerStrand - 1u; K >= 1u; --K)
                {
                    const Vec3 Prev { Strands[StartIdx + K - 1u].PositionMeters[0], Strands[StartIdx + K - 1u].PositionMeters[1], Strands[StartIdx + K - 1u].PositionMeters[2] };
                    const Vec3 Cur  { Strands[StartIdx + K].PositionMeters[0],      Strands[StartIdx + K].PositionMeters[1],      Strands[StartIdx + K].PositionMeters[2]      };
                    Vec3 Nx = Mix3(Cur, Prev, KLerp);
                    if (Spec.ManifoldMode == 2u)
                    {
                        const float Rad = std::max(0.35f, Spec.ShellRadiusM);
                        Nx = Org + Norm3(Nx - Org) * Rad;
                    }
                    Strands[StartIdx + K].PositionMeters[0] = Nx.X;
                    Strands[StartIdx + K].PositionMeters[1] = Nx.Y;
                    Strands[StartIdx + K].PositionMeters[2] = Nx.Z;
                }
            }

            const Vec3 Q  { Pos.X * Freq + DriftX, Pos.Y * Freq + DriftY, Pos.Z * Freq + DriftZ };
            const Vec3 C1 = AnalyticCurl3(Q.X, Q.Y, Q.Z);
            const Vec3 C2 = AnalyticCurl3(Q.X * 2.25f - DriftX * 1.35f, Q.Y * 2.25f + DriftY, Q.Z * 2.25f - DriftZ * 1.2f);
            Vec3 Cv = (C1 + C2 * 0.48f) * (Amp * 0.60f) + FVel * (WindCoupling * 0.12f) + FAcc * 0.14f;

            if (Spec.ManifoldMode == 1u)
            {
                const Vec3 Rel = Pos - Org;
                Cv.Y -= Rel.Y * 1.4f;
                Cv.X -= (Rel.X / std::max(Spec.BoxHalfMeters[0] * Spec.BoxHalfMeters[0], 0.25f)) * 0.55f;
                Cv.Z -= (Rel.Z / std::max(Spec.BoxHalfMeters[2] * Spec.BoxHalfMeters[2], 0.25f)) * 0.55f;
                const float Blend = std::min(1.0f, 6.5f * Dt);
                const float Keep  = std::exp(-ExtraDamp * Dt);
                Vel = (Vel + (Cv - Vel) * Blend) * Keep;
                Pos.X += Vel.X * Dt;
                Pos.Y  = std::max(0.04f, Pos.Y + Vel.Y * Dt);
                Pos.Z += Vel.Z * Dt;
                const float WaveB = 0.5f + 0.5f * std::sin(Pos.X * Freq * 1.4f + Pos.Z * Freq * 1.1f + ElapsedSeconds * 0.9f);
                Slot.AuxScalar = std::clamp((Len3(Vel) / std::max(Amp * 1.15f, 0.2f)) * 0.65f + WaveB * 0.35f, 0.0f, 1.0f);
            }
            else
            {
                const float Rad = std::max(0.35f, Spec.ShellRadiusM);
                const Vec3  Nrm = Norm3(Pos - Org);
                const Vec3  Spin {
                    (0.93f * Nrm.Z - 0.22f * Nrm.Y) * Amp * 0.34f,
                    (0.22f * Nrm.X - 0.26f * Nrm.Z) * Amp * 0.34f,
                    (0.26f * Nrm.Y - 0.93f * Nrm.X) * Amp * 0.34f
                };
                Vec3 Tan = Cv + Spin;
                Tan = Tan - Nrm * Dot3(Tan, Nrm);
                const float Blend = std::min(1.0f, 7.0f * Dt);
                const float Keep  = std::exp(-ExtraDamp * Dt);
                Vel = (Vel + (Tan - Vel) * Blend) * Keep;
                Vel = Vel - Nrm * Dot3(Vel, Nrm);
                Pos = Org + Norm3((Pos + Vel * Dt) - Org) * Rad;
                Slot.AuxScalar = std::clamp(Len3(Vel) / std::max(Amp * 1.15f, 0.2f), 0.0f, 1.0f);
            }

            if (HasTrail)
            {
                Strands[StartIdx].PositionMeters[0] = Pos.X;
                Strands[StartIdx].PositionMeters[1] = Pos.Y;
                Strands[StartIdx].PositionMeters[2] = Pos.Z;
            }
        }

        const float AgeFrac = std::clamp(Slot.AgeSeconds / std::max(0.01f, Slot.LifespanSeconds), 0.0f, 1.0f);
        Slot.PositionMeters[0] = Pos.X;
        Slot.PositionMeters[1] = Pos.Y;
        Slot.PositionMeters[2] = Pos.Z;
        Slot.VelocityMps[0]    = Vel.X;
        Slot.VelocityMps[1]    = Vel.Y;
        Slot.VelocityMps[2]    = Vel.Z;
        Slot.SizeMeters        = Rig.Kinematics.SizeBirthMeters * (1.0f - AgeFrac) + Rig.Kinematics.SizeDeathMeters * AgeFrac;
    }
}

void StepSpiderwebSplineEnsemble(
    const ParticleRig&                                          Rig,
    const SpiderwebWeaveSpec&                                   Spec,
    const float                                                 OriginMeters[3],
    const float                                                 AmbientWindMps[3],
    const Frontier::SimulationSpace::ForceFields::ForceFieldAccumulator& Fields,
    const InteractiveColliderSpec&                              Collider,
    std::span<ParticleSlot>                                     Slots,
    std::span<StrandVertex>                                     Strands,
    float                                                       ElapsedSeconds,
    float                                                       /*DeltaSeconds*/) noexcept
{
    const uint32_t Cap = static_cast<uint32_t>(Slots.size());
    if (Cap == 0u) return;

    uint32_t VertsPerStrand = kWebSplineVertexCount;
    if (const auto* Rib = std::get_if<RibbonStrandCarrier>(&Rig.Carrier))
        VertsPerStrand = std::max<uint32_t>(4u, Rib->VerticesPerStrand);

    const Vec3  Org   { OriginMeters[0], OriginMeters[1], OriginMeters[2] };
    const float TWeb  = ElapsedSeconds * std::max(0.15f, Spec.SpinRateScale);
    const float WindT = ElapsedSeconds;

    for (uint32_t I = 0u; I < Cap; ++I)
    {
        ParticleSlot& Slot = Slots[I];
        const WebStrandDesc Desc = EvalSpiderwebStrand(Spec, I, Cap, Org, TWeb, WindT, AmbientWindMps, Fields);
        if (!Desc.Visible)
        {
            Slot.AuxScalar = 0.0f;
            MarkSlotAlive(Slot, false);
            continue;
        }

        MarkSlotAlive(Slot, true);
        Slot.AgeSeconds      = 1.0f;
        Slot.LifespanSeconds = 9999.0f;
        Slot.SizeMeters      = Spec.SilkWidthMeters;
        Slot.AuxScalar       = 1.0f;
        Slot.OriginAnchor[0] = Desc.Role;
        Slot.OriginAnchor[1] = Desc.Grow;
        Slot.OriginAnchor[2] = Desc.Bead;
        Slot.PositionMeters[0] = Desc.Tip.X;
        Slot.PositionMeters[1] = Desc.Tip.Y;
        Slot.PositionMeters[2] = Desc.Tip.Z;

        const size_t StartIdx = static_cast<size_t>(I) * static_cast<size_t>(VertsPerStrand);
        if (StartIdx + VertsPerStrand > Strands.size()) continue;

        const Vec3 Mid = Mix3(Desc.PointA, Desc.PointB, 0.5f);
        const Vec3 MidSway = EvalWebSway(
            Mid, WindT, Desc.WindScale, Spec.CurlAmpMps, Spec.CurlFreqPerM,
            Spec.WindCoupling, AmbientWindMps, Fields);

        for (uint32_t K = 0u; K < VertsPerStrand; ++K)
        {
            const float U     = static_cast<float>(K) / static_cast<float>(VertsPerStrand - 1u);
            const float Belly = 4.0f * U * (1.0f - U);
            const float Whip  = Desc.TipWhip * U * U;
            Vec3 Pt = Mix3(Desc.PointA, Desc.PointB, U);
            Pt.Y   -= Desc.Sag * Belly;
            Pt      = Pt + MidSway * (Belly + Whip * 0.65f);

            if (Collider.BallActive)
            {
                const Vec3 BPos { Collider.BallCenterM[0], Collider.BallCenterM[1], Collider.BallCenterM[2] };
                const Vec3 D    = Pt - BPos;
                const float Dist = Len3(D);
                const float MinR = Collider.BallRadiusMeters + 0.04f;
                if (Dist < MinR && Dist > 1e-4f)
                {
                    Pt = BPos + D * (MinR / Dist);
                }
            }

            Strands[StartIdx + K].PositionMeters[0] = Pt.X;
            Strands[StartIdx + K].PositionMeters[1] = Pt.Y;
            Strands[StartIdx + K].PositionMeters[2] = Pt.Z;
            Strands[StartIdx + K].NormalisedSpanU   = U;
        }
    }
}

void StepDielectricSparkEnsemble(
    const ParticleRig&       Rig,
    const DielectricArcSpec& Spec,
    const float              CathodeMeters[3],
    const float              AnodeMeters[3],
    std::span<ParticleSlot>  Slots,
    float                    ElapsedSeconds,
    float                    DeltaSeconds) noexcept
{
    const uint32_t Cap = static_cast<uint32_t>(Slots.size());
    if (Cap == 0u) return;

    const float Dt   = std::clamp(DeltaSeconds, 1e-4f, 0.05f);
    const Vec3  PosA { CathodeMeters[0], CathodeMeters[1], CathodeMeters[2] };
    const Vec3  PosB { AnodeMeters[0],   AnodeMeters[1],   AnodeMeters[2]   };
    const uint32_t ActiveSparks = std::min<uint32_t>(Cap, Spec.ArcVariant >= 2u ? 42u : 28u);

    for (uint32_t I = 0u; I < Cap; ++I)
    {
        ParticleSlot& Slot = Slots[I];
        if (I >= ActiveSparks)
        {
            MarkSlotAlive(Slot, false);
            continue;
        }
        const bool NeedsSeed = !QueryParticleSlotAlive(Slot) || (Slot.AgeSeconds + Dt >= Slot.LifespanSeconds);
        if (NeedsSeed)
        {
            const float Cycle = std::floor(ElapsedSeconds * 6.0f + static_cast<float>(I) * 0.37f);
            const float Sd    = static_cast<float>(I) * 29.7f + Cycle * 11.3f;
            const float U0    = Hash2(Sd, 1.3f);
            const float U1    = Hash2(Sd, 2.9f);
            const float U2    = Hash2(Sd, 4.1f);
            const float U3    = Hash2(Sd, 5.7f);
            Vec3 Origin = PosA;
            if (Spec.ArcVariant >= 2u)
            {
                const float Ang = U0 * kTwoPi;
                const float Rad = (Spec.ArcVariant == 2u ? Spec.CageRadiusMeters : Spec.BoxHalfMeters[0]) * (0.25f + 0.75f * U1);
                const float Yo  = (Spec.ArcVariant == 3u) ? (U2 - 0.5f) * Spec.BoxHalfMeters[1] * 1.4f : (U2 - 0.5f) * 0.65f;
                Origin = { PosA.X + std::cos(Ang) * Rad, std::max(0.25f, PosA.Y + Yo), PosA.Z + std::sin(Ang) * Rad };
            }
            else
            {
                Origin = Mix3(PosA, PosB, U0);
                Origin.Y += (U1 - 0.5f) * 0.35f;
            }
            const float Az = U2 * kTwoPi;
            const float Sp = 0.8f + U3 * 2.6f;
            Slot.PositionMeters[0] = Origin.X;
            Slot.PositionMeters[1] = Origin.Y;
            Slot.PositionMeters[2] = Origin.Z;
            Slot.VelocityMps[0]    = std::cos(Az) * Sp;
            Slot.VelocityMps[1]    = (U1 - 0.3f) * Sp;
            Slot.VelocityMps[2]    = std::sin(Az) * Sp;
            Slot.AgeSeconds        = U0 * 0.12f;
            Slot.LifespanSeconds   = std::max(0.14f, Rig.Kinematics.LifeMinSeconds + U3 * (Rig.Kinematics.LifeMaxSeconds - Rig.Kinematics.LifeMinSeconds));
            Slot.SizeMeters        = std::max(0.018f, Rig.Kinematics.SizeBirthMeters * 0.65f);
            Slot.SeedScalar        = U3;
            MarkSlotAlive(Slot, true);
        }
        else
        {
            Slot.AgeSeconds += Dt;
            const float Drag = std::exp(-2.6f * Dt);
            Slot.VelocityMps[0] *= Drag;
            Slot.VelocityMps[1]  = Slot.VelocityMps[1] * Drag - 0.45f * Dt;
            Slot.VelocityMps[2] *= Drag;
            Slot.PositionMeters[0] += Slot.VelocityMps[0] * Dt;
            Slot.PositionMeters[1]  = std::max(0.05f, Slot.PositionMeters[1] + Slot.VelocityMps[1] * Dt);
            Slot.PositionMeters[2] += Slot.VelocityMps[2] * Dt;
        }
    }
}

DielectricArcGeometry BuildDielectricArcGeometry(
    const ParticleRig&             /*Rig*/,
    const DielectricArcSpec&       Spec,
    const float                    CathodeMeters[3],
    const float                    AnodeMeters[3],
    const InteractiveColliderSpec& Collider,
    float                          ElapsedSeconds,
    bool                           ForcePeakDischarge) noexcept
{
    DielectricArcGeometry Out {};
    const Vec3 PosA { CathodeMeters[0], CathodeMeters[1], CathodeMeters[2] };
    const Vec3 PosB { AnodeMeters[0],   AnodeMeters[1],   AnodeMeters[2]   };

    if (Spec.ArcVariant >= 2u)
    {
        // Plasma Cage Collector (2) & Dynamic Lightning Storm Web (3)
        const float CageR    = std::max(0.5f, Spec.CageRadiusMeters);
        const float HoldDur  = std::max(0.10f, Spec.ArcHoldSeconds);
        const float Interval = std::max(HoldDur + 0.05f, Spec.ArcIntervalSeconds);
        const float ModT     = std::fmod(std::max(0.0f, ElapsedSeconds), Interval);
        const float StrikeAge = ForcePeakDischarge ? 0.03f : ModT;
        const float Envelope = (StrikeAge >= 0.0f && StrikeAge <= HoldDur)
            ? std::max(0.45f, std::pow(1.0f - StrikeAge / HoldDur, 0.85f))
            : 0.48f;
        const float SeedStart = 97.0f + std::floor(ElapsedSeconds * 11.0f) * 13.7f;
        const std::array<float, 3> ColCage = (Spec.ArcVariant == 2u)
            ? std::array<float, 3> { 0.52f, 0.68f, 1.0f }
            : std::array<float, 3> { 0.46f, 0.82f, 1.0f };

        std::vector<Vec3> Terminals;
        if (Spec.ArcVariant == 2u)
        {
            const float YB     = PosA.Y + std::max(0.45f, CageR * 0.45f);
            constexpr int RingsN = 8;
            const float Rot    = ElapsedSeconds * 0.16f;
            const Vec3  CentreOrb { PosA.X, PosA.Y + 0.10f, PosA.Z };
            Terminals.push_back(CentreOrb);

            for (int N = 0; N < RingsN; ++N)
            {
                const float Ang = (static_cast<float>(N) / static_cast<float>(RingsN)) * kTwoPi + Rot;
                const Vec3  Term {
                    PosA.X + std::cos(Ang) * CageR,
                    YB + std::sin(Ang * 2.0f + ElapsedSeconds * 1.4f) * 0.22f,
                    PosA.Z + std::sin(Ang) * CageR
                };
                Terminals.push_back(Term);
                if (Spec.ShowElectrodes)
                {
                    Out.Electrodes.push_back({
                        { Term.X, Term.Y, Term.Z }, 0.11f,
                        { SrgbToLinear(0.16f), SrgbToLinear(0.20f), SrgbToLinear(0.28f) },
                        { 0.48f, 0.84f, 1.0f }, true, true
                    });
                    Out.Conductors.push_back({
                        { Term.X, 0.01f, Term.Z }, { Term.X, Term.Y, Term.Z },
                        { SrgbToLinear(0.32f), SrgbToLinear(0.38f), SrgbToLinear(0.48f) }, 0.72f
                    });
                    if (N > 0)
                    {
                        const Vec3 Prev = Terminals[static_cast<size_t>(N)];
                        Out.Conductors.push_back({
                            { Prev.X, Prev.Y, Prev.Z }, { Term.X, Term.Y, Term.Z },
                            { SrgbToLinear(0.36f), SrgbToLinear(0.42f), SrgbToLinear(0.54f) }, 0.75f
                        });
                    }
                }
            }
            if (Spec.ShowElectrodes && Terminals.size() > 2u)
            {
                const Vec3 First = Terminals[1];
                const Vec3 Last  = Terminals.back();
                Out.Conductors.push_back({
                    { Last.X, Last.Y, Last.Z }, { First.X, First.Y, First.Z },
                    { SrgbToLinear(0.36f), SrgbToLinear(0.42f), SrgbToLinear(0.54f) }, 0.75f
                });
                Out.Electrodes.push_back({
                    { CentreOrb.X, CentreOrb.Y, CentreOrb.Z }, 0.26f,
                    { SrgbToLinear(0.35f), SrgbToLinear(0.62f), SrgbToLinear(1.0f) },
                    { 0.75f, 0.92f, 1.0f }, true, false
                });
            }
            Out.Flashes.push_back({ { CentreOrb.X, CentreOrb.Y, CentreOrb.Z }, 0.42f, { 0.58f, 0.88f, 1.0f }, 1.15f * Envelope });
        }
        else
        {
            const float Ex = std::max(0.8f, Spec.BoxHalfMeters[0]);
            const float Ey = std::max(0.5f, Spec.BoxHalfMeters[1]);
            const float Ez = std::max(0.8f, Spec.BoxHalfMeters[2]);
            for (int N = 0; N < 9; ++N)
            {
                const float Ph = ElapsedSeconds * 0.55f + static_cast<float>(N) * 1.73f;
                const Vec3 Term {
                    PosA.X + std::sin(Ph * 0.9f + static_cast<float>(N)) * Ex * 0.85f,
                    std::max(0.32f, PosA.Y + std::cos(Ph * 1.15f + static_cast<float>(N) * 2.1f) * Ey * 0.78f),
                    PosA.Z + std::cos(Ph * 0.75f - static_cast<float>(N) * 1.3f) * Ez * 0.85f
                };
                Terminals.push_back(Term);
                if (Spec.ShowElectrodes)
                {
                    Out.Electrodes.push_back({
                        { Term.X, Term.Y, Term.Z }, 0.095f,
                        { SrgbToLinear(0.22f), SrgbToLinear(0.28f), SrgbToLinear(0.36f) },
                        { 0.52f, 0.90f, 1.0f }, true, false
                    });
                }
            }
        }

        const int LinkCount = (Spec.ArcVariant == 2u) ? 9 : 11;
        const size_t NTerm  = Terminals.size();
        for (int L = 0; L < LinkCount && NTerm >= 2u; ++L)
        {
            const size_t Ia = (Spec.ArcVariant == 2u)
                ? ((L % 2 == 0) ? 0u : (1u + static_cast<size_t>(L) % (NTerm - 1u)))
                : (static_cast<size_t>(L) % NTerm);
            const size_t Ib = (Spec.ArcVariant == 2u)
                ? (1u + (static_cast<size_t>(L) * 3u + static_cast<size_t>(std::floor(ArcHash(SeedStart, static_cast<float>(L)) * 3.0f))) % (NTerm - 1u))
                : ((static_cast<size_t>(L) + 2u + static_cast<size_t>(std::floor(ArcHash(SeedStart, static_cast<float>(L) * 3.0f) * 3.0f))) % NTerm);
            if (Ia == Ib) continue;

            const Vec3  A    = Terminals[Ia];
            const Vec3  B    = Terminals[Ib];
            const float Span = std::max(0.2f, Len3(B - A));
            Vec3 SU {}, SV {}, SW {};
            BuildOrthonormalBasis(B - A, SU, SV, SW);
            SubdivideSparkRecursive(
                A, B, SU, SV,
                std::min(0.75f, Span * 0.22f) * Spec.JitterAmplitudeM,
                0.024f, 4, SeedStart + static_cast<float>(L) * 31.7f,
                ColCage.data(), 1.45f * Envelope, 0.08f, (L % 2 == 0) ? 2 : 1, true, Out);
            Out.Flashes.push_back({ { A.X, A.Y, A.Z }, 0.16f, { ColCage[0], ColCage[1], ColCage[2] }, 0.95f * Envelope });
            Out.Flashes.push_back({ { B.X, B.Y, B.Z }, 0.18f, { ColCage[0], ColCage[1], ColCage[2] }, 1.05f * Envelope });
        }

        if (Collider.BallActive && !Terminals.empty())
        {
            const Vec3 BPos { Collider.BallCenterM[0], Collider.BallCenterM[1], Collider.BallCenterM[2] };
            if (Len3(BPos - PosA) < CageR + 2.8f)
            {
                const size_t CapN = std::min<size_t>(3u, Terminals.size());
                const float  CaptureCol[3] { 0.78f, 0.72f, 1.0f };
                for (size_t C = 0u; C < CapN; ++C)
                {
                    const Vec3 SrcN  = Terminals[(C * 3u) % Terminals.size()];
                    const Vec3 DN    = Norm3(BPos - SrcN);
                    const Vec3 HitPt = BPos - DN * Collider.BallRadiusMeters;
                    const float Span = std::max(0.2f, Len3(HitPt - SrcN));
                    Vec3 SU {}, SV {}, SW {};
                    BuildOrthonormalBasis(DN, SU, SV, SW);
                    SubdivideSparkRecursive(
                        SrcN, HitPt, SU, SV, std::min(0.65f, Span * 0.24f) * Spec.JitterAmplitudeM,
                        0.032f, 4, SeedStart + 901.0f + static_cast<float>(C) * 47.0f,
                        CaptureCol, 2.1f, 0.12f, 3, true, Out);
                    Out.Flashes.push_back({ { HitPt.X, HitPt.Y, HitPt.Z }, 0.28f, { 0.82f, 0.85f, 1.0f }, 1.8f });
                }
            }
        }
        return Out;
    }

    // Dual-Sphere Tesla Lightning (0) & Static Shock Arcs (1)
    const bool  StaticMode = (Spec.ArcVariant == 1u);
    const float RadA       = std::max(0.08f, Spec.CathodeRadiusMeters);
    const float RadB       = std::max(0.08f, Spec.AnodeRadiusMeters);

    if (Spec.ShowElectrodes)
    {
        const std::array<float, 3> RingA = StaticMode ? std::array<float, 3> { 0.42f, 0.88f, 1.0f } : std::array<float, 3> { 0.96f, 0.82f, 0.36f };
        const std::array<float, 3> RingB = StaticMode ? std::array<float, 3> { 0.76f, 0.56f, 1.0f } : std::array<float, 3> { 0.96f, 0.82f, 0.36f };
        Out.Electrodes.push_back({
            { PosA.X, PosA.Y, PosA.Z }, RadA,
            { SrgbToLinear(0.20f), SrgbToLinear(0.24f), SrgbToLinear(0.32f) },
            { RingA[0], RingA[1], RingA[2] }, true, false
        });
        Out.Electrodes.push_back({
            { PosB.X, PosB.Y, PosB.Z }, RadB,
            { SrgbToLinear(0.24f), SrgbToLinear(0.22f), SrgbToLinear(0.34f) },
            { RingB[0], RingB[1], RingB[2] }, true, false
        });
    }

    const Vec3  Delta = PosB - PosA;
    const Vec3  Dir   = Norm3(Delta);
    Vec3 SideU {}, UpPerp {}, WAxis {};
    BuildOrthonormalBasis(Dir, SideU, UpPerp, WAxis);

    const float HoldDur   = std::max(0.08f, Spec.ArcHoldSeconds);
    const float Interval  = std::max(HoldDur + 0.05f, Spec.ArcIntervalSeconds);
    const float StrikeAge = ForcePeakDischarge ? 0.03f : std::fmod(std::max(0.0f, ElapsedSeconds), Interval);
    if (!ForcePeakDischarge && StrikeAge > HoldDur)
    {
        if (StaticMode)
        {
            const float Flick = 0.22f + 0.16f * std::sin(ElapsedSeconds * 31.0f);
            const Vec3 FA = PosA + Dir * RadA;
            const Vec3 FB = PosB - Dir * RadB;
            Out.Flashes.push_back({ { FA.X, FA.Y, FA.Z }, 0.16f, { 0.45f, 0.82f, 1.0f }, Flick * 0.75f });
            Out.Flashes.push_back({ { FB.X, FB.Y, FB.Z }, 0.16f, { 0.68f, 0.60f, 1.0f }, Flick * 0.75f });
        }
        return Out;
    }

    const float UAge      = std::clamp(StrikeAge / HoldDur, 0.0f, 1.0f);
    const float Snap      = std::min(1.0f, StrikeAge / 0.012f);
    const float Decay     = std::pow(std::max(0.0f, 1.0f - UAge), StaticMode ? 1.25f : 1.45f);
    const float Restrike1 = std::exp(-std::pow((UAge - 0.38f) / 0.055f, 2.0f)) * 0.62f;
    const float Restrike2 = std::exp(-std::pow((UAge - 0.70f) / 0.045f, 2.0f)) * 0.34f;
    const float Envelope  = Snap * std::min(1.25f, Decay * 1.15f + Restrike1 + Restrike2);

    const std::array<float, 3> CoolCol = StaticMode
        ? std::array<float, 3> { 0.42f + 0.45f * Decay, 0.56f + 0.40f * Decay, 1.0f }
        : std::array<float, 3> { 0.36f + 0.58f * Decay, 0.58f + 0.40f * Decay, 1.0f };

    const uint32_t ArcCnt = std::clamp<uint32_t>(Spec.ArcCount, 1u, 6u);
    const float SeedStart = (StaticMode ? 211.0f : 97.0f) + std::floor(ElapsedSeconds * 4.5f) * 7.13f;

    for (uint32_t K = 0u; K < ArcCnt; ++K)
    {
        const float KSeed = SeedStart + static_cast<float>(K) * 43.17f;
        const float Lx    = (ArcHash(KSeed, 1.1f) - 0.5f) * 0.54f;
        const float Ly    = (ArcHash(KSeed, 2.2f) - 0.5f) * 0.44f + (Spec.ArcBow > 0.0f ? 0.10f : -0.10f);
        const float Rx    = (ArcHash(KSeed, 3.3f) - 0.5f) * 0.52f;
        const float Ry    = (ArcHash(KSeed, 4.4f) - 0.5f) * 0.42f + (Spec.ArcBow > 0.0f ? 0.10f : -0.10f);
        const Vec3  CapA  = Norm3(Dir + SideU * Lx + UpPerp * Ly);
        const Vec3  CapB  = Norm3(Dir * -1.0f + SideU * Rx + UpPerp * Ry);
        const Vec3  Start = PosA + CapA * RadA;
        const Vec3  End   = PosB + CapB * RadB;
        const float Span  = std::max(0.25f, Len3(End - Start));
        const float Amp   = std::min(1.15f, Span * 0.21f) * Spec.JitterAmplitudeM * (K == 0u ? 1.0f : 0.85f);
        const float W0    = (K == 0u ? 0.036f : 0.022f) * (0.65f + 0.55f * Envelope);
        const float Inten = (K == 0u ? 1.85f  : 1.05f)  * Envelope;
        const float Lift  = Spec.ArcBow + StrikeAge * 0.55f;
        const int   Depth = StaticMode ? 4 : 5;
        const int   Forks = (K == 0u) ? (StaticMode ? 2 : 4) : (StaticMode ? 1 : 2);

        SubdivideSparkRecursive(Start, End, SideU, UpPerp, Amp, W0, Depth, KSeed, CoolCol.data(), Inten, Lift, Forks, true, Out);
        Out.Flashes.push_back({ { Start.X, Start.Y, Start.Z }, (K == 0u ? 0.24f : 0.14f) * (0.55f + 0.65f * Envelope), { CoolCol[0], CoolCol[1], CoolCol[2] }, (K == 0u ? 1.55f : 0.85f) * Envelope });
        Out.Flashes.push_back({ { End.X,   End.Y,   End.Z   }, (K == 0u ? 0.30f : 0.17f) * (0.55f + 0.70f * Envelope), { CoolCol[0], CoolCol[1], CoolCol[2] }, (K == 0u ? 1.85f : 0.95f) * Envelope });
    }

    return Out;
}

} // namespace Frontier::ParticleSpace
