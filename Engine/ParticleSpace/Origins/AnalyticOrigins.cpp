/*============================================================================================================================================
                                                             ANALYTICORIGINS.CPP
============================================================================================================================================*/
// 🧩 Implementation of deterministic PCG PRNG, analytic release-origin samplers & structural anchor generators

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Origins/AnalyticOrigins.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr float TwoPi = 6.28318530717958647692f;

void Normalize3(float V[3]) noexcept
{
    const float LenSq = V[0] * V[0] + V[1] * V[1] + V[2] * V[2];
    if (LenSq > 1e-12f)
    {
        const float Inv = 1.0f / std::sqrt(LenSq);
        V[0] *= Inv;
        V[1] *= Inv;
        V[2] *= Inv;
    }
    else
    {
        V[0] = 0.0f;
        V[1] = 1.0f;
        V[2] = 0.0f;
    }
}

void BuildOrthonormalBasis(const float N[3], float U[3], float V[3]) noexcept
{
    const std::array<float, 3> Ref = (std::fabs(N[1]) < 0.92f)
        ? std::array<float, 3> { 0.0f, 1.0f, 0.0f }
        : std::array<float, 3> { 1.0f, 0.0f, 0.0f };
    // U = normalize(cross(Ref, N))
    U[0] = Ref[1] * N[2] - Ref[2] * N[1];
    U[1] = Ref[2] * N[0] - Ref[0] * N[2];
    U[2] = Ref[0] * N[1] - Ref[1] * N[0];
    Normalize3(U);
    // V = cross(N, U)
    V[0] = N[1] * U[2] - N[2] * U[1];
    V[1] = N[2] * U[0] - N[0] * U[2];
    V[2] = N[0] * U[1] - N[1] * U[0];
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 DETERMINISTIC PCG RNG
//------------------------------------------------------------------------------------------------------------------------

uint32_t DeterministicPcgRng::NextUint32() noexcept
{
    Cursor = Cursor * 747796405u + 2891336453u;
    const uint32_t Word = ((Cursor >> ((Cursor >> 28u) + 4u)) ^ Cursor) * 277803737u;
    return (Word >> 22u) ^ Word;
}

float DeterministicPcgRng::NextUnitFloat() noexcept
{
    return static_cast<float>(NextUint32() & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float DeterministicPcgRng::NextSignedFloat() noexcept
{
    return NextUnitFloat() * 2.0f - 1.0f;
}

float DeterministicPcgRng::NextRange(float MinVal, float MaxVal) noexcept
{
    return MinVal + (MaxVal - MinVal) * NextUnitFloat();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                ANALYTIC ORIGIN SAMPLER
//------------------------------------------------------------------------------------------------------------------------

OriginSpawnPoint SampleAnalyticOrigin(
    const ReleaseOriginSpec& Origin,
    DeterministicPcgRng&     Rng) noexcept
{
    OriginSpawnPoint Out {};

    if (const auto* Pt = std::get_if<AnalyticPointOrigin>(&Origin))
    {
        Out.PositionMeters[0] = Pt->CenterMeters[0];
        Out.PositionMeters[1] = Pt->CenterMeters[1];
        Out.PositionMeters[2] = Pt->CenterMeters[2];
        return Out;
    }

    if (const auto* Sph = std::get_if<AnalyticSphereOrigin>(&Origin))
    {
        const float Z     = Rng.NextSignedFloat();
        const float Phi   = Rng.NextUnitFloat() * TwoPi;
        const float SinTh = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
        const float Nx    = SinTh * std::cos(Phi);
        const float Ny    = Z;
        const float Nz    = SinTh * std::sin(Phi);
        const float Rad   = Sph->ShellOnly
            ? Sph->RadiusMeters
            : Sph->RadiusMeters * std::cbrt(Rng.NextUnitFloat());
        Out.PositionMeters[0] = Sph->CenterMeters[0] + Nx * Rad;
        Out.PositionMeters[1] = Sph->CenterMeters[1] + Ny * Rad;
        Out.PositionMeters[2] = Sph->CenterMeters[2] + Nz * Rad;
        Out.NormalUnit[0]     = Nx;
        Out.NormalUnit[1]     = Ny;
        Out.NormalUnit[2]     = Nz;
        return Out;
    }

    if (const auto* Disc = std::get_if<AnalyticDiscOrigin>(&Origin))
    {
        float N[3] = { Disc->NormalAxis[0], Disc->NormalAxis[1], Disc->NormalAxis[2] };
        Normalize3(N);
        float U[3], V[3];
        BuildOrthonormalBasis(N, U, V);
        const float R   = Disc->RadiusMeters * std::sqrt(Rng.NextUnitFloat());
        const float Ang = Rng.NextUnitFloat() * TwoPi;
        const float Cu  = R * std::cos(Ang);
        const float Cv  = R * std::sin(Ang);
        for (int K = 0; K < 3; ++K)
        {
            Out.PositionMeters[K] = Disc->CenterMeters[K] + U[K] * Cu + V[K] * Cv;
            Out.NormalUnit[K]     = N[K];
        }
        return Out;
    }

    if (const auto* Box = std::get_if<AnalyticBoxOrigin>(&Origin))
    {
        for (int K = 0; K < 3; ++K)
        {
            Out.PositionMeters[K] = Box->CenterMeters[K] + Rng.NextSignedFloat() * Box->HalfExtentsMeters[K];
        }
        return Out;
    }

    if (const auto* Elec = std::get_if<DualElectrodeOrigin>(&Origin))
    {
        const float T = Rng.NextUnitFloat();
        for (int K = 0; K < 3; ++K)
        {
            Out.PositionMeters[K] = Elec->CathodeMeters[K] + (Elec->AnodeMeters[K] - Elec->CathodeMeters[K]) * T;
            Out.NormalUnit[K]     = Elec->AnodeMeters[K] - Elec->CathodeMeters[K];
        }
        Normalize3(Out.NormalUnit);
        return Out;
    }

    if (const auto* Surf = std::get_if<SurfaceAnchorOrigin>(&Origin))
    {
        const float Z     = Rng.NextSignedFloat();
        const float Phi   = Rng.NextUnitFloat() * TwoPi;
        const float SinTh = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
        Out.NormalUnit[0]     = SinTh * std::cos(Phi);
        Out.NormalUnit[1]     = Z;
        Out.NormalUnit[2]     = SinTh * std::sin(Phi);
        Out.PositionMeters[0] = Surf->CenterMeters[0] + Out.NormalUnit[0] * Surf->ScaleMeters[0];
        Out.PositionMeters[1] = Surf->CenterMeters[1] + Out.NormalUnit[1] * Surf->ScaleMeters[1];
        Out.PositionMeters[2] = Surf->CenterMeters[2] + Out.NormalUnit[2] * Surf->ScaleMeters[2];
        return Out;
    }

    if (const auto* Fw = std::get_if<FireworkOrigin>(&Origin))
    {
        Out.PositionMeters[0] = Fw->ShellOrigin[0];
        Out.PositionMeters[1] = Fw->ShellOrigin[1];
        Out.PositionMeters[2] = Fw->ShellOrigin[2];
        return Out;
    }

    return Out;
}

void SampleConeVelocity(
    const float          LaunchDir[3],
    float                SpreadRadians,
    float                SpeedMinMps,
    float                SpeedMaxMps,
    DeterministicPcgRng& Rng,
    float                OutVelocityMps[3]) noexcept
{
    float Axis[3] = { LaunchDir[0], LaunchDir[1], LaunchDir[2] };
    Normalize3(Axis);
    float U[3], V[3];
    BuildOrthonormalBasis(Axis, U, V);

    const float ClampedSpread = std::clamp(SpreadRadians, 0.0f, 3.14159265f);
    const float CosMax        = std::cos(ClampedSpread);
    const float CosTheta      = CosMax + (1.0f - CosMax) * Rng.NextUnitFloat();
    const float SinTheta      = std::sqrt(std::max(0.0f, 1.0f - CosTheta * CosTheta));
    const float Phi           = Rng.NextUnitFloat() * TwoPi;
    const float Speed         = Rng.NextRange(SpeedMinMps, std::max(SpeedMinMps, SpeedMaxMps));

    const float DirU = SinTheta * std::cos(Phi);
    const float DirV = SinTheta * std::sin(Phi);
    for (int K = 0; K < 3; ++K)
    {
        OutVelocityMps[K] = (Axis[K] * CosTheta + U[K] * DirU + V[K] * DirV) * Speed;
    }
}

std::vector<ParticleAnchorSample> BuildStructuralOriginAnchors(
    const ReleaseOriginSpec& Origin,
    uint32_t                 MaxSampleCount,
    uint32_t                 Seed)
{
    std::vector<ParticleAnchorSample> Samples;
    DeterministicPcgRng Rng(Seed);

    if (const auto* Lat = std::get_if<SurfaceLatticeOrigin>(&Origin))
    {
        const uint32_t Cols = std::max(1u, Lat->Columns);
        const uint32_t Rows = std::max(1u, Lat->Rows);
        Samples.reserve(std::min(MaxSampleCount, Cols * Rows));
        for (uint32_t R = 0u; R < Rows && Samples.size() < MaxSampleCount; ++R)
        {
            const float V = (Rows > 1u) ? (static_cast<float>(R) / static_cast<float>(Rows - 1u) * 2.0f - 1.0f) : 0.0f;
            for (uint32_t C = 0u; C < Cols && Samples.size() < MaxSampleCount; ++C)
            {
                const float U = (Cols > 1u) ? (static_cast<float>(C) / static_cast<float>(Cols - 1u) * 2.0f - 1.0f) : 0.0f;
                ParticleAnchorSample S {};
                S.PositionMeters[0] = Lat->CenterMeters[0] + U * Lat->HalfExtentsMeters[0];
                S.PositionMeters[1] = Lat->CenterMeters[1] + V * Lat->HalfExtentsMeters[1];
                S.PositionMeters[2] = Lat->CenterMeters[2] + V * Lat->HalfExtentsMeters[2];
                S.NormalUnit[0]     = 0.0f;
                S.NormalUnit[1]     = 1.0f;
                S.NormalUnit[2]     = 0.0f;
                S.CurvatureWeight   = 0.5f;
                S.GroupIndex        = R;
                Samples.push_back(S);
            }
        }
        return Samples;
    }

    if (const auto* Web = std::get_if<SpiderwebOrigin>(&Origin))
    {
        const uint32_t Spokes = std::max(4u, Web->Spokes);
        const uint32_t Rings  = std::max(2u, Web->SpiralRings);
        Samples.reserve(std::min(MaxSampleCount, Spokes * Rings));
        for (uint32_t R = 1u; R <= Rings && Samples.size() < MaxSampleCount; ++R)
        {
            const float Frac = static_cast<float>(R) / static_cast<float>(Rings);
            for (uint32_t S = 0u; S < Spokes && Samples.size() < MaxSampleCount; ++S)
            {
                const float BaseAng = TwoPi * (static_cast<float>(S) / static_cast<float>(Spokes));
                const float Jitter  = Web->MessyCobweb ? Rng.NextSignedFloat() * 0.28f : 0.0f;
                const float Rad     = Web->RadiusMeters * Frac * (Web->MessyCobweb ? (0.85f + 0.30f * Rng.NextUnitFloat()) : 1.0f);
                ParticleAnchorSample Sample {};
                Sample.PositionMeters[0] = Web->CenterMeters[0] + std::cos(BaseAng + Jitter) * Rad;
                Sample.PositionMeters[1] = Web->CenterMeters[1] + std::sin(BaseAng + Jitter) * Rad;
                Sample.PositionMeters[2] = Web->CenterMeters[2] + (Web->MessyCobweb ? Rng.NextSignedFloat() * 0.35f : 0.0f);
                Sample.NormalUnit[0]     = 0.0f;
                Sample.NormalUnit[1]     = 0.0f;
                Sample.NormalUnit[2]     = 1.0f;
                Sample.CurvatureWeight   = Frac;
                Sample.GroupIndex        = S;
                Samples.push_back(Sample);
            }
        }
        return Samples;
    }

    if (const auto* Cloth = std::get_if<ClothGridOrigin>(&Origin))
    {
        const uint32_t Rows = std::max(2u, Cloth->Strands);
        const uint32_t Segs = std::max(2u, Cloth->SegmentsPerRow);
        Samples.reserve(std::min(MaxSampleCount, Rows * Segs));
        for (uint32_t R = 0u; R < Rows && Samples.size() < MaxSampleCount; ++R)
        {
            const float U = static_cast<float>(R) / static_cast<float>(Rows - 1u) - 0.5f;
            for (uint32_t K = 0u; K < Segs && Samples.size() < MaxSampleCount; ++K)
            {
                const float V = static_cast<float>(K) / static_cast<float>(Segs - 1u);
                ParticleAnchorSample Sample {};
                Sample.PositionMeters[0] = Cloth->OriginMeters[0] + U * Cloth->SpanMeters;
                Sample.PositionMeters[1] = Cloth->OriginMeters[1] - V * Cloth->DropMeters;
                Sample.PositionMeters[2] = Cloth->OriginMeters[2];
                Sample.NormalUnit[0]     = 0.0f;
                Sample.NormalUnit[1]     = 0.0f;
                Sample.NormalUnit[2]     = 1.0f;
                Sample.CurvatureWeight   = V;
                Sample.GroupIndex        = R;
                Samples.push_back(Sample);
            }
        }
        return Samples;
    }

    if (const auto* Flora = std::get_if<BotanicalRootOrigin>(&Origin))
    {
        const uint32_t Count = std::min(MaxSampleCount, std::max(1u, Flora->RootCount));
        Samples.reserve(Count);
        for (uint32_t I = 0u; I < Count; ++I)
        {
            const float R   = Flora->PatchRadius * std::sqrt(Rng.NextUnitFloat());
            const float Ang = Rng.NextUnitFloat() * TwoPi;
            ParticleAnchorSample Sample {};
            Sample.PositionMeters[0] = Flora->CenterMeters[0] + std::cos(Ang) * R;
            Sample.PositionMeters[1] = Flora->CenterMeters[1];
            Sample.PositionMeters[2] = Flora->CenterMeters[2] + std::sin(Ang) * R;
            Sample.NormalUnit[0]     = 0.0f;
            Sample.NormalUnit[1]     = 1.0f;
            Sample.NormalUnit[2]     = 0.0f;
            Sample.CurvatureWeight   = Rng.NextUnitFloat();
            Sample.GroupIndex        = I;
            Samples.push_back(Sample);
        }
        return Samples;
    }

    return Samples;
}

} // namespace Frontier::ParticleSpace
