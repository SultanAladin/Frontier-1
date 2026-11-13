/*============================================================================================================================================
                                                          INTERPOLATIONCURVES.H
============================================================================================================================================*/
// 🧩 Step-rate-independent exponential smoothing 1 - exp(-lambda * dt) and logarithmic zoom interpolation

#pragma once

#include "Engine/Mathematics/AxisCoordinates.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Components::Kinematics
{

[[nodiscard]] inline float EvaluateExponentialBlendFactor(
    float RateLambdaPerSecond,
    float DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f)
    {
        return 0.0f;
    }
    if (RateLambdaPerSecond <= 0.0f)
    {
        return 1.0f;
    }
    const float Exponent = std::clamp(RateLambdaPerSecond * DeltaSeconds, 0.0f, 40.0f);
    return 1.0f - std::exp(-Exponent);
}

[[nodiscard]] constexpr float InterpolateScalarSmooth(
    float CurrentReading,
    float TargetReading,
    float BlendFactor) noexcept
{
    return CurrentReading + (TargetReading - CurrentReading) * BlendFactor;
}

[[nodiscard]] constexpr Mathematics::Axis3D InterpolateAxisSmooth(
    Mathematics::Axis3D CurrentReading,
    Mathematics::Axis3D TargetReading,
    float               BlendFactor) noexcept
{
    return Mathematics::InterpolateLinear(CurrentReading, TargetReading, BlendFactor);
}

[[nodiscard]] constexpr Mathematics::UVAxes InterpolatePlanarSmooth(
    Mathematics::UVAxes CurrentReading,
    Mathematics::UVAxes TargetReading,
    float               BlendFactor) noexcept
{
    return Mathematics::InterpolateLinear(CurrentReading, TargetReading, BlendFactor);
}

// Logarithmic distance interpolation ensures perceptually uniform zoom speed across 0.2m .. 250m
[[nodiscard]] inline float InterpolateLogarithmicDistance(
    float CurrentDistance,
    float TargetDistance,
    float BlendFactor) noexcept
{
    const float SafeCurrent = std::max(1.0e-3f, CurrentDistance);
    const float SafeTarget  = std::max(1.0e-3f, TargetDistance);
    const float LogBlended  = InterpolateScalarSmooth(std::log(SafeCurrent), std::log(SafeTarget), BlendFactor);
    return std::exp(LogBlended);
}

} // namespace Frontier::Components::Kinematics
