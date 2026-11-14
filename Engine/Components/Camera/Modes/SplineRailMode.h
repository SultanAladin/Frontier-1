/*============================================================================================================================================
                                                             SPLINERAILMODE.H
============================================================================================================================================*/
// 🧩 Smooth Catmull-Rom spline rail traversal with forward tangent orientation or explicit look-at pivot

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"
#include "Engine/Components/Camera/Kinematics/InterpolationCurves.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace Frontier::Components::Modes
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline Mathematics::Axis3D SampleCatmullRomRail(
    std::span<const Mathematics::Axis3D> ControlPoints,
    float                                NormalizedParameter) noexcept
{
    if (ControlPoints.empty())
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    if (ControlPoints.size() == 1u)
    {
        return ControlPoints[0u];
    }

    const float ClampedT     = std::clamp(NormalizedParameter, 0.0f, 1.0f);
    const std::size_t SegCnt = ControlPoints.size() - 1u;
    const float Scaled       = ClampedT * static_cast<float>(SegCnt);
    const std::size_t SegIdx = std::min(static_cast<std::size_t>( std::floor(Scaled) ), SegCnt - 1u);
    const float LocalT       = Scaled - static_cast<float>(SegIdx);

    const Mathematics::Axis3D P0 = ControlPoints[SegIdx > 0u ? SegIdx - 1u : 0u];
    const Mathematics::Axis3D P1 = ControlPoints[SegIdx];
    const Mathematics::Axis3D P2 = ControlPoints[SegIdx + 1u];
    const Mathematics::Axis3D P3 = ControlPoints[std::min(SegIdx + 2u, ControlPoints.size() - 1u)];

    const float T2 = LocalT * LocalT;
    const float T3 = T2 * LocalT;

    return (P1 * 2.0f +
            (P2 - P0) * LocalT +
            (P0 * 2.0f - P1 * 5.0f + P2 * 4.0f - P3) * T2 +
            (-P0 + P1 * 3.0f - P2 * 3.0f + P3) * T3) * 0.5f;
}

inline void AdvanceSplineRailKinematics(
    const CameraConfiguration& Config,
    CameraPosture&             Posture,
    float                      DeltaSeconds) noexcept
{
    Posture.TargetSplineParameter = std::clamp(Posture.TargetSplineParameter, 0.0f, 1.0f);
    const float RailBlend         = Kinematics::EvaluateExponentialBlendFactor(Config.Kinematics.PanSmoothingRate, DeltaSeconds);
    Posture.CurrentSplineParameter = Kinematics::InterpolateScalarSmooth(
        Posture.CurrentSplineParameter,
        Posture.TargetSplineParameter,
        RailBlend);

    if (Config.SplineControlPoints.size() >= 2u)
    {
        Posture.CurrentEyePosition = SampleCatmullRomRail(Config.SplineControlPoints, Posture.CurrentSplineParameter);
        const float AheadT         = std::min(1.0f, Posture.CurrentSplineParameter + 0.02f);
        const Mathematics::Axis3D AheadPos = SampleCatmullRomRail(Config.SplineControlPoints, AheadT);
        if (Mathematics::EvaluateLengthSquared(AheadPos - Posture.CurrentEyePosition) > 1.0e-6f)
        {
            Posture.CurrentPivot = AheadPos;
        }
    }

    Mathematics::EvaluateOrthonormalCameraBasis(
        Posture.CurrentEyePosition,
        Posture.CurrentPivot,
        Config.WorldBasis,
        Posture.RightAxis,
        Posture.UpAxis,
        Posture.ForwardAxis);
}

} // namespace Frontier::Components::Modes
