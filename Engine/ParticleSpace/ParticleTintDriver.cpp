/*============================================================================================================================================
                                                            PARTICLETINTDRIVER.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleTintDriver routing all 15 particle scalar channels directly through Frontier::ColourSpace::SampleColourSource

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleTintDriver.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr float TwoPi = 6.28318530717958647692f;
constexpr float Pi    = 3.14159265358979323846f;

struct ChannelDescriptor
{
    const char* Key;
    const char* Label;
};

constexpr std::array<ChannelDescriptor, static_cast<std::size_t>(ParticleScalarChannel::Count)> ChannelTable = {{
    { "modeDefault",  "Mode default" },
    { "lifetime",     "Normalized lifetime" },
    { "age",          "Particle age (s)" },
    { "speed",        "Velocity speed (m/s)" },
    { "velY",         "Vertical velocity (m/s)" },
    { "angle",        "Origin azimuth angle" },
    { "velAngle",     "Velocity heading angle" },
    { "distOrigin",   "Distance from origin (m)" },
    { "height",       "World elevation (m)" },
    { "noise3D",      "Spatial 3D noise" },
    { "random",       "Per-particle random seed" },
    { "spinAngle",    "Spin orientation angle" },
    { "distCamera",   "Distance to camera (m)" },
    { "particleSize", "Particle size (m)" },
    { "lifeDuration", "Assigned lifespan (s)" },
}};

[[nodiscard]] float HashLattice3(int Ix, int Iy, int Iz) noexcept
{
    uint32_t H = static_cast<uint32_t>(Ix) * 374761393u
               + static_cast<uint32_t>(Iy) * 668265263u
               + static_cast<uint32_t>(Iz) * 2147483647u;
    H = (H ^ (H >> 13u)) * 1274126177u;
    H ^= (H >> 16u);
    return static_cast<float>(H & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

[[nodiscard]] float SampleValueNoise3D(float X, float Y, float Z) noexcept
{
    const int Ix = static_cast<int>(std::floor(X));
    const int Iy = static_cast<int>(std::floor(Y));
    const int Iz = static_cast<int>(std::floor(Z));
    float Fx = X - static_cast<float>(Ix);
    float Fy = Y - static_cast<float>(Iy);
    float Fz = Z - static_cast<float>(Iz);
    Fx = Fx * Fx * (3.0f - 2.0f * Fx);
    Fy = Fy * Fy * (3.0f - 2.0f * Fy);
    Fz = Fz * Fz * (3.0f - 2.0f * Fz);

    auto Lerp = [](float A, float B, float T) noexcept { return A + (B - A) * T; };
    const float C000 = HashLattice3(Ix,     Iy,     Iz);
    const float C100 = HashLattice3(Ix + 1, Iy,     Iz);
    const float C010 = HashLattice3(Ix,     Iy + 1, Iz);
    const float C110 = HashLattice3(Ix + 1, Iy + 1, Iz);
    const float C001 = HashLattice3(Ix,     Iy,     Iz + 1);
    const float C101 = HashLattice3(Ix + 1, Iy,     Iz + 1);
    const float C011 = HashLattice3(Ix,     Iy + 1, Iz + 1);
    const float C111 = HashLattice3(Ix + 1, Iy + 1, Iz + 1);

    const float C00 = Lerp(C000, C100, Fx);
    const float C10 = Lerp(C010, C110, Fx);
    const float C01 = Lerp(C001, C101, Fx);
    const float C11 = Lerp(C011, C111, Fx);
    return Lerp(Lerp(C00, C10, Fy), Lerp(C01, C11, Fy), Fz);
}

} // namespace

std::string_view QueryScalarChannelKey(ParticleScalarChannel Channel) noexcept
{
    const auto Idx = static_cast<std::size_t>(Channel);
    if (Idx >= ChannelTable.size()) return ChannelTable[0].Key;
    return ChannelTable[Idx].Key;
}

std::string_view QueryScalarChannelLabel(ParticleScalarChannel Channel) noexcept
{
    const auto Idx = static_cast<std::size_t>(Channel);
    if (Idx >= ChannelTable.size()) return ChannelTable[0].Label;
    return ChannelTable[Idx].Label;
}

ParticleScalarChannel ParseScalarChannelKey(std::string_view Key) noexcept
{
    for (std::size_t I = 0u; I < ChannelTable.size(); ++I)
    {
        if (Key == ChannelTable[I].Key)
        {
            return static_cast<ParticleScalarChannel>(I);
        }
    }
    return ParticleScalarChannel::ModeDefault;
}

float ExtractParticleChannelScalar(
    ParticleScalarChannel                Channel,
    const ParticleSlot&                  Slot,
    const ParticleTintInput& Context) noexcept
{
    const float NormLife = (Slot.LifespanSeconds > 1e-5f)
        ? std::clamp(Slot.AgeSeconds / Slot.LifespanSeconds, 0.0f, 1.0f)
        : 0.0f;

    switch (Channel)
    {
        case ParticleScalarChannel::ModeDefault:
            return (Context.ModeOverrideScalar >= 0.0f) ? Context.ModeOverrideScalar : NormLife;

        case ParticleScalarChannel::NormalizedLifetime:
            return NormLife;

        case ParticleScalarChannel::ParticleAgeSeconds:
            return std::max(0.0f, Slot.AgeSeconds);

        case ParticleScalarChannel::VelocitySpeed:
            return std::sqrt(
                Slot.VelocityMps[0] * Slot.VelocityMps[0]
              + Slot.VelocityMps[1] * Slot.VelocityMps[1]
              + Slot.VelocityMps[2] * Slot.VelocityMps[2]);

        case ParticleScalarChannel::VerticalVelocity:
            return Slot.VelocityMps[1];

        case ParticleScalarChannel::OriginAzimuthAngle:
        {
            const float Dx = Slot.PositionMeters[0] - Context.OriginMeters[0];
            const float Dz = Slot.PositionMeters[2] - Context.OriginMeters[2];
            const float Angle = std::atan2(Dz, Dx);
            return (Angle + Pi) / TwoPi;
        }

        case ParticleScalarChannel::VelocityHeadingAngle:
        {
            const float Angle = std::atan2(Slot.VelocityMps[2], Slot.VelocityMps[0]);
            return (Angle + Pi) / TwoPi;
        }

        case ParticleScalarChannel::DistanceFromOrigin:
        {
            const float Dx = Slot.PositionMeters[0] - Context.OriginMeters[0];
            const float Dy = Slot.PositionMeters[1] - Context.OriginMeters[1];
            const float Dz = Slot.PositionMeters[2] - Context.OriginMeters[2];
            return std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
        }

        case ParticleScalarChannel::WorldElevation:
            return Slot.PositionMeters[1];

        case ParticleScalarChannel::SpatialNoise3D:
            return SampleValueNoise3D(
                Slot.PositionMeters[0] * 0.75f + Context.ElapsedSeconds * 0.12f,
                Slot.PositionMeters[1] * 0.75f,
                Slot.PositionMeters[2] * 0.75f);

        case ParticleScalarChannel::PerParticleRandomSeed:
            return std::clamp(Slot.SeedScalar, 0.0f, 1.0f);

        case ParticleScalarChannel::SpinOrientationAngle:
        {
            float Wrapped = std::fmod(Slot.SpinRadians, TwoPi);
            if (Wrapped < 0.0f) Wrapped += TwoPi;
            return Wrapped / TwoPi;
        }

        case ParticleScalarChannel::DistanceToCamera:
        {
            const float Dx = Slot.PositionMeters[0] - Context.CameraMeters[0];
            const float Dy = Slot.PositionMeters[1] - Context.CameraMeters[1];
            const float Dz = Slot.PositionMeters[2] - Context.CameraMeters[2];
            return std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
        }

        case ParticleScalarChannel::ParticleSizeMeters:
            return std::max(0.0f, Slot.SizeMeters);

        case ParticleScalarChannel::AssignedLifespanSeconds:
            return std::max(0.0f, Slot.LifespanSeconds);

        default:
            return NormLife;
    }
}

ColourSpace::LinearRgba EvaluateParticleTint(
    const ParticleTintDriverSpec&           Driver,
    const ColourSpace::ColourSourceProfile& ColourProfile,
    const ParticleSlot&                     Slot,
    const ParticleTintInput&    Context) noexcept
{
    const float RawScalar  = ExtractParticleChannelScalar(Driver.Channel, Slot, Context);
    const float Normalized = ColourSpace::NormalizeScalarToRange(RawScalar, ColourProfile.Range);

    ColourSpace::SpatialColourSampleInput Query {};
    Query.NormalizedCoordinate = Normalized;
    Query.PositionX            = Slot.PositionMeters[0];
    Query.PositionY            = Slot.PositionMeters[1];
    Query.PositionZ            = Slot.PositionMeters[2];
    Query.ElapsedSeconds       = Context.ElapsedSeconds;
    Query.DiscreteOrdinal      = static_cast<uint32_t>(std::max(0.0f, Slot.SeedScalar) * 1024.0f);

    return ColourSpace::SampleColourSource(ColourProfile, Query);
}

} // namespace Frontier::ParticleSpace
