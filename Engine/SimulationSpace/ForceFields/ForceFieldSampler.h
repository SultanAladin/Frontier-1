/*============================================================================================================================================
                                                          FORCEFIELDSAMPLER.H
============================================================================================================================================*/
// 🧩 Bit-identical CPU C++20 mirror of ForceFieldNoise.slang and ForceModelEvaluation.slang (for PhysicsThread & validation)
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L234-L493 & L700-L722

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceModelTypes.h"

#include <array>
#include <cstdint>
#include <span>

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    NOISE & FALLOFF MATH
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] float Hash31(const std::array<float, 3>& p) noexcept;
[[nodiscard]] std::array<float, 3> Hash33(const std::array<float, 3>& p) noexcept;
[[nodiscard]] float GradientNoise(const std::array<float, 3>& p) noexcept;
[[nodiscard]] float ValueNoise(const std::array<float, 3>& p) noexcept;
[[nodiscard]] std::array<float, 3> PotentialNoise(const std::array<float, 3>& p) noexcept;
[[nodiscard]] std::array<float, 3> CurlNoise(const std::array<float, 3>& p) noexcept;

[[nodiscard]] float EvaluateForceFalloff(ForceFalloffOrdinal falloff, float normalizedDistance) noexcept;
[[nodiscard]] float EvaluatePulseEnvelope(float simulationTimeSeconds, float periodSeconds, float width, float delaySeconds) noexcept;
[[nodiscard]] std::array<float, 3> SafeNormalizeVector(const std::array<float, 3>& v) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                    FORCE FIELD SAMPLING
//------------------------------------------------------------------------------------------------------------------------

/// Evaluates a single 64-byte ForceModelGpuRecord at (worldPosition, velocity, simulationTimeSeconds).
[[nodiscard]] std::array<float, 3> EvaluateSingleForceModel(
    const ForceModelGpuRecord& record,
    const std::array<float, 3>& worldPositionMeters,
    const std::array<float, 3>& velocityMetersPerSec,
    float simulationTimeSeconds) noexcept;

/// Accumulates all enabled records matching channelMaskBits (unsaturated — used by particles & JoltPhysics bodies).
[[nodiscard]] std::array<float, 3> EvaluateTotalForceField(
    std::span<const ForceModelGpuRecord> records,
    const std::array<float, 3>& worldPositionMeters,
    const std::array<float, 3>& velocityMetersPerSec,
    float simulationTimeSeconds,
    uint32_t channelMaskBits = static_cast<uint32_t>(ForceTargetChannelBits::All)) noexcept;

/// Accumulates gas-channel records with FluidShaders.js L700-L722 continuous-force velocity saturation.
[[nodiscard]] std::array<float, 3> EvaluateSaturatedGasForceField(
    std::span<const ForceModelGpuRecord> records,
    const std::array<float, 3>& worldPositionMeters,
    const std::array<float, 3>& velocityMetersPerSec,
    float simulationTimeSeconds,
    float forceResponseSeconds) noexcept;

} // namespace Frontier::SimulationSpace::ForceFields
