/*==========================================================================================================================================
                                                   WEATHER EFFECTS DEFINITION
==========================================================================================================================================*/

#include "WeatherEffects.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

SpatialVector Add(SpatialVector A, SpatialVector B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
SpatialVector Sub(SpatialVector A, SpatialVector B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
SpatialVector Mul(SpatialVector A, double S) noexcept { return {A.X * S, A.Y * S, A.Z * S}; }
double        Length(SpatialVector A) noexcept { return std::sqrt(A.X * A.X + A.Y * A.Y + A.Z * A.Z); }

double Wrap(double Value, double Extent) noexcept
{
    double R = std::fmod(Value, Extent);
    return R < 0.0 ? R + Extent : R;
}

uint32_t Hash32(uint64_t Seed, uint32_t Index, uint32_t Salt) noexcept
{
    uint64_t H = Seed + static_cast<uint64_t>(Index) * 0x9E3779B97F4A7C15ull +
                 static_cast<uint64_t>(Salt) * 0x85EBCA77C2B2AE63ull;
    H ^= H >> 29u;
    H *= 0xBF58476D1CE4E5B9ull;
    H ^= H >> 32u;
    return static_cast<uint32_t>(H >> 32u);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     PRECIPITATION
//------------------------------------------------------------------------------------------------------------------------

void PrecipitationField::Hashes(uint32_t Index, double& U0, double& U1, double& U2, double& U3) const noexcept
{
    U0 = Hash32(P_.Seed, Index, 0u) * (1.0 / 4294967296.0);
    U1 = Hash32(P_.Seed, Index, 1u) * (1.0 / 4294967296.0);
    U2 = Hash32(P_.Seed, Index, 2u) * (1.0 / 4294967296.0);
    U3 = Hash32(P_.Seed, Index, 3u) * (1.0 / 4294967296.0);
}

SpatialVector PrecipitationField::ParticleVelocity(uint32_t Index) const noexcept
{
    double U0, U1, U2, U3;
    Hashes(Index, U0, U1, U2, U3);
    const double Fall = P_.FallSpeedMps * (0.8 + 0.4 * U3);
    return {P_.WindXMps, P_.WindYMps, -Fall};
}

double PrecipitationField::VerticalPeriod(uint32_t Index) const noexcept
{
    const SpatialVector V = ParticleVelocity(Index);
    return V.Z < 0.0 ? P_.VolumeHeightM / -V.Z : 0.0;
}

SpatialVector PrecipitationField::ParticleAt(uint32_t Index, double TimeS, SpatialVector Anchor) const noexcept
{
    double U0, U1, U2, U3;
    Hashes(Index, U0, U1, U2, U3);
    const SpatialVector Velocity = ParticleVelocity(Index);
    const double E = P_.VolumeExtentM, H = P_.VolumeHeightM;
    // Base cell position + analytic advection, wrapped about the anchor.
    const double X = Anchor.X - 0.5 * E + Wrap(U0 * E + Velocity.X * TimeS - Anchor.X + 0.5 * E, E) ;
    const double Y = Anchor.Y - 0.5 * E + Wrap(U1 * E + Velocity.Y * TimeS - Anchor.Y + 0.5 * E, E);
    const double Z = Anchor.Z + Wrap(U2 * H + Velocity.Z * TimeS, H);
    return {X, Y, Z};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIGHTNING
//------------------------------------------------------------------------------------------------------------------------

namespace
{

void Displace(std::vector<SpatialVector>& Points, uint32_t Depth, double Roughness, DeterministicRandom& Random)
{
    for (uint32_t Level = 0; Level < Depth; ++Level)
    {
        std::vector<SpatialVector> Next;
        Next.reserve(Points.size() * 2 - 1);
        for (size_t I = 0; I + 1 < Points.size(); ++I)
        {
            Next.push_back(Points[I]);
            const SpatialVector A = Points[I], B = Points[I + 1];
            const SpatialVector Mid = Mul(Add(A, B), 0.5);
            const double SegLen = Length(Sub(B, A));
            const double Scale = Roughness * SegLen;
            SpatialVector Offset{(Random.NextUnit() - 0.5) * 2.0 * Scale, (Random.NextUnit() - 0.5) * 2.0 * Scale,
                                 (Random.NextUnit() - 0.5) * 2.0 * Scale};
            // Suppress the along-segment component so the channel never folds back on itself:
            // project out the component parallel to the segment (keeps it lightning, not scribble).
            if (SegLen > 0.0)
            {
                const SpatialVector Axis = Mul(Sub(B, A), 1.0 / SegLen);
                const double Along = Offset.X * Axis.X + Offset.Y * Axis.Y + Offset.Z * Axis.Z;
                Offset = Sub(Offset, Mul(Axis, Along));
            }
            Next.push_back(Add(Mid, Offset));
        }
        Next.push_back(Points.back());
        Points.swap(Next);
    }
}

} // namespace

LightningBolt GenerateLightningBolt(SpatialVector From, SpatialVector To, uint32_t Depth, double Roughness,
                                    uint64_t Seed, uint32_t MaxBranches)
{
    DeterministicRandom Random(Seed, 0xB017ull);
    LightningBolt Bolt;
    Bolt.MainChannel = {From, To};
    Displace(Bolt.MainChannel, std::min(Depth, 10u), Roughness, Random);

    const uint32_t BranchCount = MaxBranches > 0 ? Random.NextBelow(MaxBranches + 1) : 0;
    for (uint32_t B = 0; B < BranchCount && Bolt.MainChannel.size() > 4; ++B)
    {
        const size_t Root = 1 + Random.NextBelow(static_cast<uint32_t>(Bolt.MainChannel.size() - 2));
        const SpatialVector RootPoint = Bolt.MainChannel[Root];
        // Branch target: continue mostly toward To, skewed sideways and shortened.
        const SpatialVector ToTarget = Sub(To, RootPoint);
        const SpatialVector Skew{(Random.NextUnit() - 0.5) * 0.8, (Random.NextUnit() - 0.5) * 0.8,
                                 -0.3 * Random.NextUnit()};
        const SpatialVector End = Add(RootPoint, Add(Mul(ToTarget, 0.35), Mul(Skew, Length(ToTarget) * 0.35)));
        std::vector<SpatialVector> Branch{RootPoint, End};
        Displace(Branch, std::min(Depth, 7u), Roughness, Random);
        Bolt.Branches.push_back(std::move(Branch));
    }
    return Bolt;
}

double LightningIntensity(double TimeS, uint64_t Seed) noexcept
{
    if (TimeS < 0.0)
    {
        return 0.0;
    }
    DeterministicRandom Random(Seed ^ 0xF1A5ull, 0x57B0ull);
    double Intensity = std::exp(-TimeS / 0.045);   // main stroke, fast decay
    double RestrikeAt = 0.05;
    for (uint32_t Stroke = 0; Stroke < 3; ++Stroke)
    {
        RestrikeAt += 0.03 + 0.08 * Random.NextUnit();
        const double Strength = 0.35 + 0.45 * Random.NextUnit();
        const double Tau = 0.03 + 0.03 * Random.NextUnit();
        if (TimeS >= RestrikeAt)
        {
            Intensity += Strength * std::exp(-(TimeS - RestrikeAt) / Tau);
        }
    }
    return std::clamp(Intensity, 0.0, 1.0);
}

} // namespace Frontier
