/*============================================================================================================================================
                                                         FORCEFIELDSAMPLER.CPP
============================================================================================================================================*/
// 🧩 Exact C++20 implementation of NoiseChunk (L234-L293), ForceFieldChunk (L294-L493), and GasForce saturation (L700-L722)
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L234-L493 & L700-L722

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceFieldSampler.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::ForceFields
{

namespace
{

inline constexpr float kPi = 3.14159265f; // [rad] - Matches GLSL literal in FluidShaders.js L314 & L447

[[nodiscard]] inline float Fract(const float x) noexcept
{
    return x - std::floor(x);
}

[[nodiscard]] inline float Lerp(const float a, const float b, const float t) noexcept
{
    return a + (b - a) * t;
}

[[nodiscard]] inline float SmoothStep(const float edge0, const float edge1, const float x) noexcept
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] inline float Dot3(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

[[nodiscard]] inline float Length3(const std::array<float, 3>& a) noexcept
{
    return std::sqrt(Dot3(a, a));
}

[[nodiscard]] inline std::array<float, 3> Cross3(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    };
}

[[nodiscard]] inline std::array<float, 3> Add3(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
}

[[nodiscard]] inline std::array<float, 3> Sub3(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

[[nodiscard]] inline std::array<float, 3> Scale3(const std::array<float, 3>& a, const float s) noexcept
{
    return { a[0] * s, a[1] * s, a[2] * s };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    NOISE & FALLOFF MATH
//------------------------------------------------------------------------------------------------------------------------

float Hash31(const std::array<float, 3>& pIn) noexcept
{
    std::array<float, 3> p = {
        Fract(pIn[0] * 0.1031f),
        Fract(pIn[1] * 0.1030f),
        Fract(pIn[2] * 0.0973f)
    };
    const float d = p[0] * (p[1] + 33.33f) + p[1] * (p[0] + 33.33f) + p[2] * (p[2] + 33.33f);
    p[0] += d;
    p[1] += d;
    p[2] += d;
    return Fract((p[0] + p[1]) * p[2]);
}

std::array<float, 3> Hash33(const std::array<float, 3>& p) noexcept
{
    const float qx = Dot3(p, { 127.1f, 311.7f,  74.7f });
    const float qy = Dot3(p, { 269.5f, 183.3f, 246.1f });
    const float qz = Dot3(p, { 113.5f, 271.9f, 124.6f });
    const auto hashScalar = [](const float q) noexcept -> float
    {
        // Correctly-rounded float32 sin matches V8 Math.fround(Math.sin(q)) and SPIR-V OpExtInst Sin
        const float s = static_cast<float>(std::sin(static_cast<double>(q)));
        return Fract(s * 43758.5453123f) * 2.0f - 1.0f;
    };
    return { hashScalar(qx), hashScalar(qy), hashScalar(qz) };
}

float GradientNoise(const std::array<float, 3>& p) noexcept
{
    const std::array<float, 3> i = { std::floor(p[0]), std::floor(p[1]), std::floor(p[2]) };
    const std::array<float, 3> f = { p[0] - i[0], p[1] - i[1], p[2] - i[2] };
    const std::array<float, 3> u = {
        f[0] * f[0] * f[0] * (f[0] * (f[0] * 6.0f - 15.0f) + 10.0f),
        f[1] * f[1] * f[1] * (f[1] * (f[1] * 6.0f - 15.0f) + 10.0f),
        f[2] * f[2] * f[2] * (f[2] * (f[2] * 6.0f - 15.0f) + 10.0f)
    };

    const auto corner = [&](const float dx, const float dy, const float dz) noexcept -> float
    {
        return Dot3(Hash33({ i[0] + dx, i[1] + dy, i[2] + dz }), { f[0] - dx, f[1] - dy, f[2] - dz });
    };

    const float x00 = Lerp(corner(0.0f, 0.0f, 0.0f), corner(1.0f, 0.0f, 0.0f), u[0]);
    const float x10 = Lerp(corner(0.0f, 1.0f, 0.0f), corner(1.0f, 1.0f, 0.0f), u[0]);
    const float x01 = Lerp(corner(0.0f, 0.0f, 1.0f), corner(1.0f, 0.0f, 1.0f), u[0]);
    const float x11 = Lerp(corner(0.0f, 1.0f, 1.0f), corner(1.0f, 1.0f, 1.0f), u[0]);
    return Lerp(Lerp(x00, x10, u[1]), Lerp(x01, x11, u[1]), u[2]);
}

float ValueNoise(const std::array<float, 3>& pIn) noexcept
{
    const auto rotateOctave = [](const std::array<float, 3>& v) noexcept -> std::array<float, 3>
    {
        return {
             0.00f * v[0] - 0.80f * v[1] - 0.60f * v[2],
             0.80f * v[0] + 0.36f * v[1] - 0.48f * v[2],
             0.60f * v[0] - 0.48f * v[1] + 0.64f * v[2]
        };
    };

    std::array<float, 3> p = pIn;
    float sum = GradientNoise(p);
    p = Scale3(rotateOctave(p), 2.02f);
    sum += 0.5f * GradientNoise(p);
    p = Scale3(rotateOctave(p), 2.03f);
    sum += 0.25f * GradientNoise(p);
    return std::clamp(sum / 1.75f * 0.75f + 0.5f, 0.0f, 1.0f);
}

std::array<float, 3> PotentialNoise(const std::array<float, 3>& p) noexcept
{
    return {
        GradientNoise(p),
        GradientNoise({ p[0] + 31.7f, p[1] + 17.3f, p[2] + 9.1f }),
        GradientNoise({ p[0] - 12.9f, p[1] + 44.2f, p[2] + 23.6f })
    };
}

std::array<float, 3> CurlNoise(const std::array<float, 3>& p) noexcept
{
    constexpr float kStep = 0.03f;
    const auto dx = Sub3(PotentialNoise({ p[0] + kStep, p[1], p[2] }), PotentialNoise({ p[0] - kStep, p[1], p[2] }));
    const auto dy = Sub3(PotentialNoise({ p[0], p[1] + kStep, p[2] }), PotentialNoise({ p[0], p[1] - kStep, p[2] }));
    const auto dz = Sub3(PotentialNoise({ p[0], p[1], p[2] + kStep }), PotentialNoise({ p[0], p[1], p[2] - kStep }));
    return Scale3({ dy[2] - dz[1], dz[0] - dx[2], dx[1] - dy[0] }, 1.0f / (2.0f * kStep));
}

float EvaluateForceFalloff(const ForceFalloffOrdinal falloff, const float d) noexcept
{
    const uint32_t mode = static_cast<uint32_t>(falloff);
    if (mode == 3u)
    {
        return 1.0f;
    }
    if (d >= 1.0f)
    {
        return 0.0f;
    }
    if (mode == 0u)
    {
        return 1.0f - SmoothStep(0.0f, 1.0f, d);
    }
    if (mode == 1u)
    {
        return 1.0f - d;
    }
    if (mode == 4u)
    {
        return std::exp(-d * d * 5.0f) * (1.0f - SmoothStep(0.9f, 1.0f, d));
    }
    if (mode == 5u)
    {
        return 1.0f - SmoothStep(0.55f, 1.0f, d);
    }
    if (mode == 6u)
    {
        return 0.5f + 0.5f * std::cos(d * kPi);
    }
    return (1.0f / (1.0f + 16.0f * d * d)) * (1.0f - SmoothStep(0.85f, 1.0f, d));
}

float EvaluatePulseEnvelope(const float simulationTimeSeconds, const float periodSeconds, const float width, const float delaySeconds) noexcept
{
    const float localTime = simulationTimeSeconds - delaySeconds;
    if (localTime < 0.0f)
    {
        return 0.0f;
    }
    const float phase = Fract(localTime / std::max(periodSeconds, 0.05f));
    return std::exp(-phase / std::max(width, 0.01f)) * (1.0f - SmoothStep(0.8f, 1.0f, phase));
}

std::array<float, 3> SafeNormalizeVector(const std::array<float, 3>& v) noexcept
{
    const float len = Length3(v);
    return (len > 1.0e-5f) ? Scale3(v, 1.0f / len) : std::array<float, 3> { 0.0f, 1.0f, 0.0f };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FORCE FIELD SAMPLING
//------------------------------------------------------------------------------------------------------------------------

std::array<float, 3> EvaluateSingleForceModel(
    const ForceModelGpuRecord& record,
    const std::array<float, 3>& worldPositionMeters,
    const std::array<float, 3>& velocityMetersPerSec,
    const float simulationTimeSeconds) noexcept
{
    const int typeCode   = static_cast<int>(record.TypeFalloffParams[0] + 0.5f);
    const auto falloff   = static_cast<ForceFalloffOrdinal>(static_cast<uint32_t>(record.TypeFalloffParams[1] + 0.5f));
    const float frequency = record.TypeFalloffParams[2];
    const float secondary = record.TypeFalloffParams[3];
    const float tertiary  = record.TimingChannelMask[0];

    const std::array<float, 3> centre = { record.PositionRadius[0], record.PositionRadius[1], record.PositionRadius[2] };
    const float radius                = std::max(record.PositionRadius[3], 1.0e-3f);
    const std::array<float, 3> axis   = SafeNormalizeVector({ record.DirectionStrength[0], record.DirectionStrength[1], record.DirectionStrength[2] });
    const float strength              = record.DirectionStrength[3];

    const std::array<float, 3> r      = Sub3(worldPositionMeters, centre);
    const float distance              = Length3(r);
    const std::array<float, 3> radial = Sub3(r, Scale3(axis, Dot3(r, axis)));
    const float radialDistance        = Length3(radial);

    if (typeCode == 0)
    {
        const float w = EvaluateForceFalloff(falloff, distance / radius);
        const float gust = ValueNoise({
            worldPositionMeters[0] * 0.6f + simulationTimeSeconds * frequency,
            worldPositionMeters[1] * 0.6f,
            worldPositionMeters[2] * 0.6f + simulationTimeSeconds * frequency * 0.7f
        }) * 2.0f - 1.0f;
        return Scale3(axis, strength * (1.0f + secondary * gust) * w);
    }
    if (typeCode == 1 || typeCode == 2)
    {
        const float w = EvaluateForceFalloff(falloff, distance / radius);
        const float core = distance / (distance + std::max(secondary, 0.02f) * radius + 1.0e-4f);
        const std::array<float, 3> direction = (distance > 1.0e-4f) ? Scale3(r, 1.0f / distance) : std::array<float, 3> { 0.0f, 0.0f, 0.0f };
        return Scale3(direction, strength * w * core * ((typeCode == 1) ? -1.0f : 1.0f));
    }
    if (typeCode == 3)
    {
        const float w = EvaluateForceFalloff(falloff, radialDistance / radius);
        const std::array<float, 3> radialDirection = (radialDistance > 1.0e-4f) ? Scale3(radial, 1.0f / radialDistance) : std::array<float, 3> { 0.0f, 0.0f, 0.0f };
        const std::array<float, 3> tangent = Cross3(axis, radialDirection);
        const float core = radialDistance / (radialDistance + 0.12f * radius);
        const auto sum = Add3(Sub3(Scale3(tangent, strength * core), Scale3(radialDirection, strength * 0.3f * frequency)), Scale3(axis, secondary));
        return Scale3(sum, w);
    }
    if (typeCode == 4)
    {
        const float w = EvaluateForceFalloff(falloff, distance / radius);
        const auto curl = CurlNoise({
            worldPositionMeters[0] * frequency,
            worldPositionMeters[1] * frequency + simulationTimeSeconds * secondary,
            worldPositionMeters[2] * frequency + simulationTimeSeconds * secondary * 0.31f
        });
        return Scale3(curl, strength * w * 0.35f);
    }
    if (typeCode == 5)
    {
        const float w = EvaluateForceFalloff(falloff, distance / radius);
        return Scale3(velocityMetersPerSec, -strength * w);
    }
    if (typeCode == 6 || typeCode == 13)
    {
        const float pulse = EvaluatePulseEnvelope(simulationTimeSeconds, frequency, secondary, tertiary);
        const float w = EvaluateForceFalloff(falloff, distance / radius);
        const std::array<float, 3> direction = (distance > 1.0e-4f) ? Scale3(r, 1.0f / distance) : std::array<float, 3> { 0.0f, 0.0f, 0.0f };
        return Scale3(direction, strength * pulse * w * ((typeCode == 13) ? -1.0f : 1.0f));
    }
    if (typeCode == 14)
    {
        const float pulse = EvaluatePulseEnvelope(simulationTimeSeconds, frequency, secondary, tertiary);
        const float w = EvaluateForceFalloff(falloff, distance / radius);
        return Scale3(axis, strength * pulse * w);
    }
    if (typeCode == 12)
    {
        const float localTime = simulationTimeSeconds - tertiary;
        if (localTime < 0.0f)
        {
            return { 0.0f, 0.0f, 0.0f };
        }
        const float phase = Fract(localTime / std::max(frequency, 0.05f));
        const float shellRadius = phase * radius;
        const float thickness = std::max(secondary, 0.02f) * radius;
        const float delta = (distance - shellRadius) / thickness;
        const float shell = std::exp(-(delta * delta));
        const std::array<float, 3> direction = (distance > 1.0e-4f) ? Scale3(r, 1.0f / distance) : std::array<float, 3> { 0.0f, 0.0f, 0.0f };
        return Scale3(direction, strength * shell * (1.0f - phase * 0.7f));
    }
    if (typeCode == 10)
    {
        const float w = EvaluateForceFalloff(falloff, distance / radius);
        const std::array<float, 3> radialDirection = (radialDistance > 1.0e-4f) ? Scale3(radial, 1.0f / radialDistance) : std::array<float, 3> { 0.0f, 0.0f, 0.0f };
        const std::array<float, 3> tangent = Cross3(axis, radialDirection);
        const std::array<float, 3> toCentre = (distance > 1.0e-4f) ? Scale3(r, -1.0f / distance) : std::array<float, 3> { 0.0f, 0.0f, 0.0f };
        const float core = radialDistance / (radialDistance + 0.1f * radius);
        return Scale3(Add3(Scale3(tangent, core), Scale3(toCentre, secondary)), strength * w);
    }
    if (typeCode == 11)
    {
        const float overshoot = distance - radius * (1.0f - std::max(secondary, 0.02f));
        if (overshoot <= 0.0f)
        {
            return { 0.0f, 0.0f, 0.0f };
        }
        const float wall = std::clamp(overshoot / (std::max(secondary, 0.02f) * radius), 0.0f, 1.0f);
        const std::array<float, 3> direction = (distance > 1.0e-4f) ? Scale3(r, -1.0f / distance) : std::array<float, 3> { 0.0f, 0.0f, 0.0f };
        return Scale3(direction, strength * wall * wall);
    }
    if (typeCode == 15)
    {
        const float w = EvaluateForceFalloff(falloff, distance / radius);
        const float stepT = std::floor(simulationTimeSeconds * std::max(secondary, 0.1f)) * 17.0f;
        const auto kick = Hash33({
            std::floor(worldPositionMeters[0] * frequency) + stepT,
            std::floor(worldPositionMeters[1] * frequency) + stepT,
            std::floor(worldPositionMeters[2] * frequency) + stepT
        });
        return Scale3(kick, strength * w);
    }
    if (typeCode == 16)
    {
        const float w = EvaluateForceFalloff(falloff, distance / radius);
        return Scale3(Sub3(Scale3(axis, frequency), velocityMetersPerSec), strength * w);
    }
    if (typeCode == 17)
    {
        const float w = EvaluateForceFalloff(falloff, distance / radius);
        const float height = std::clamp(Dot3(r, axis) / radius, -1.0f, 1.0f);
        const std::array<float, 3> refUp = (std::fabs(axis[1]) < 0.9f) ? std::array<float, 3> { 0.0f, 1.0f, 0.0f } : std::array<float, 3> { 1.0f, 0.0f, 0.0f };
        const std::array<float, 3> u = SafeNormalizeVector(Cross3(axis, refUp));
        const std::array<float, 3> v = Cross3(axis, u);
        const float angle = height * secondary * kPi;
        return Scale3(Add3(Scale3(u, std::cos(angle)), Scale3(v, std::sin(angle))), strength * w);
    }
    if (typeCode == 18)
    {
        const float w = EvaluateForceFalloff(falloff, radialDistance / radius);
        return Scale3(Cross3(axis, radial), (strength * w) / radius);
    }
    if (typeCode == 19)
    {
        const float w = EvaluateForceFalloff(falloff, distance / radius);
        const float core = std::max(secondary, 0.05f) * radius;
        const float d = std::max(distance, core);
        const std::array<float, 3> n = Scale3(r, 1.0f / std::max(distance, 1.0e-4f));
        const std::array<float, 3> field = Sub3(Scale3(n, 3.0f * Dot3(axis, n)), axis);
        const float scale = (core * core * core) / (d * d * d);
        return Scale3(SafeNormalizeVector(field), strength * w * Lerp(scale, 1.0f, 0.35f));
    }
    if (typeCode == 7)
    {
        const float w = EvaluateForceFalloff(falloff, radialDistance / radius);
        return Scale3(axis, strength * w);
    }
    if (typeCode == 8)
    {
        const float w = EvaluateForceFalloff(falloff, radialDistance / radius);
        const std::array<float, 3> radialDirection = (radialDistance > 1.0e-4f) ? Scale3(radial, 1.0f / radialDistance) : std::array<float, 3> { 0.0f, 0.0f, 0.0f };
        return Scale3(radialDirection, -strength * w);
    }
    if (typeCode == 9)
    {
        const float tube = std::max(frequency, 0.05f);
        const std::array<float, 3> radialDirection = (radialDistance > 1.0e-4f) ? Scale3(radial, 1.0f / radialDistance) : std::array<float, 3> { 1.0f, 0.0f, 0.0f };
        const std::array<float, 3> ringPoint = Add3(centre, Scale3(radialDirection, radius));
        const std::array<float, 3> d = Sub3(worldPositionMeters, ringPoint);
        const float coreDistance = Length3(d);
        const float w = EvaluateForceFalloff(falloff, coreDistance / tube);
        const std::array<float, 3> ringTangent = Cross3(axis, radialDirection);
        const std::array<float, 3> circulation = Cross3(ringTangent, Scale3(d, 1.0f / std::max(coreDistance, 1.0e-4f)));
        return Scale3(circulation, strength * w);
    }
    return { 0.0f, 0.0f, 0.0f };
}

std::array<float, 3> EvaluateTotalForceField(
    const std::span<const ForceModelGpuRecord> records,
    const std::array<float, 3>& worldPositionMeters,
    const std::array<float, 3>& velocityMetersPerSec,
    const float simulationTimeSeconds,
    const uint32_t channelMaskBits) noexcept
{
    std::array<float, 3> total = { 0.0f, 0.0f, 0.0f };
    for (const ForceModelGpuRecord& rec : records)
    {
        if (rec.TimingChannelMask[3] < 0.5f)
        {
            continue;
        }
        const uint32_t recMask = static_cast<uint32_t>(rec.TimingChannelMask[2] + 0.5f);
        if ((recMask & channelMaskBits) == 0u)
        {
            continue;
        }
        total = Add3(total, EvaluateSingleForceModel(rec, worldPositionMeters, velocityMetersPerSec, simulationTimeSeconds));
    }
    return total;
}

std::array<float, 3> EvaluateSaturatedGasForceField(
    const std::span<const ForceModelGpuRecord> records,
    const std::array<float, 3>& worldPositionMeters,
    const std::array<float, 3>& velocityMetersPerSec,
    const float simulationTimeSeconds,
    const float forceResponseSeconds) noexcept
{
    std::array<float, 3> total = { 0.0f, 0.0f, 0.0f };
    constexpr uint32_t kGasMask = static_cast<uint32_t>(ForceTargetChannelBits::Gas);
    for (const ForceModelGpuRecord& rec : records)
    {
        if (rec.TimingChannelMask[3] < 0.5f)
        {
            continue;
        }
        const uint32_t recMask = static_cast<uint32_t>(rec.TimingChannelMask[2] + 0.5f);
        if ((recMask & kGasMask) == 0u)
        {
            continue;
        }
        std::array<float, 3> force = EvaluateSingleForceModel(rec, worldPositionMeters, velocityMetersPerSec, simulationTimeSeconds);
        const int typeCode = static_cast<int>(rec.TypeFalloffParams[0] + 0.5f);
        const bool limited = !(typeCode == 5 || typeCode == 6 || typeCode == 11 || typeCode == 12 || typeCode == 13 || typeCode == 14 || typeCode == 16);
        const float size = Length3(force);
        if (limited && forceResponseSeconds > 0.0f && size > 1.0e-5f)
        {
            const float along = Dot3(velocityMetersPerSec, Scale3(force, 1.0f / size));
            force = Scale3(force, std::clamp(1.0f - along / (size * forceResponseSeconds), 0.0f, 1.0f));
        }
        total = Add3(total, force);
    }
    return total;
}

} // namespace Frontier::SimulationSpace::ForceFields
