/*============================================================================================================================================
                                                        CAMERALAGCOMPONENT.CPP
============================================================================================================================================*/
// 🧩 Reusable camera lag: exponentially-damped follow of a desired camera pose (Unreal-style lag speed)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraLagComponent.h"

#include "Engine/Components/Camera/Kinematics/InterpolationCurves.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Components
{

namespace
{

[[nodiscard]] Mathematics::Axis3D SlerpUnitDirection(
    Mathematics::Axis3D FromUnit,
    Mathematics::Axis3D ToUnit,
    float               Blend) noexcept
{
    const float ClampedBlend = std::clamp(Blend, 0.0f, 1.0f);
    if (ClampedBlend <= 1.0e-6f)
    {
        return FromUnit;
    }
    if (ClampedBlend >= 1.0f - 1.0e-6f)
    {
        return ToUnit;
    }

    const float DotVal = std::clamp(Mathematics::EvaluateDot(FromUnit, ToUnit), -1.0f, 1.0f);
    if (DotVal > 0.9995f)
    {
        return Mathematics::NormalizeAxis(
            Kinematics::InterpolateAxisSmooth(FromUnit, ToUnit, ClampedBlend),
            ToUnit);
    }
    if (DotVal < -0.9995f)
    {
        const Mathematics::Axis3D RefAxis =
            (std::fabs(FromUnit.Z) < 0.9f) ? Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f } : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
        const Mathematics::Axis3D Ortho = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(FromUnit, RefAxis), { 1.0f, 0.0f, 0.0f });
        const float Angle = 3.14159265358979323846f * ClampedBlend;
        return Mathematics::NormalizeAxis(FromUnit * std::cos(Angle) + Ortho * std::sin(Angle), ToUnit);
    }

    const float Omega    = std::acos(DotVal);
    const float InvSin   = 1.0f / std::sin(Omega);
    const float WeightA  = std::sin((1.0f - ClampedBlend) * Omega) * InvSin;
    const float WeightB  = std::sin(ClampedBlend * Omega) * InvSin;
    return Mathematics::NormalizeAxis(FromUnit * WeightA + ToUnit * WeightB, ToUnit);
}

} // namespace

void CameraLagComponent::SnapTo(Mathematics::Axis3D Position, Mathematics::Axis3D Target) noexcept
{
    SmoothedPosition               = Position;
    SmoothedTarget                 = Target;
    const Mathematics::Axis3D Span = Target - Position;
    const float SpanLen            = std::sqrt(Mathematics::EvaluateDot(Span, Span));
    SmoothedDistance               = std::max(0.05f, SpanLen);
    SmoothedForward                = Mathematics::NormalizeAxis(Span, { 0.0f, 1.0f, 0.0f });
    Primed                         = true;
}

bool CameraLagComponent::AdvanceLag(
    Mathematics::Axis3D DesiredPosition,
    Mathematics::Axis3D DesiredTarget,
    float               DeltaSeconds) noexcept
{
    if (!Primed)
    {
        SnapTo(DesiredPosition, DesiredTarget);
        return false;
    }

    const Mathematics::Axis3D PreviousPosition = SmoothedPosition;
    const Mathematics::Axis3D PreviousForward  = SmoothedForward;
    const float               PreviousDistance = SmoothedDistance;

    const Mathematics::Axis3D DesiredSpan     = DesiredTarget - DesiredPosition;
    const float               DesiredDistance = std::max(0.05f, std::sqrt(Mathematics::EvaluateDot(DesiredSpan, DesiredSpan)));
    const Mathematics::Axis3D DesiredForward  = Mathematics::NormalizeAxis(DesiredSpan, SmoothedForward);

    if (Settings.LocationLagEnabled && DeltaSeconds > 0.0f)
    {
        const float LocBlend = Kinematics::EvaluateExponentialBlendFactor(Settings.LocationLagSpeed, DeltaSeconds);
        SmoothedPosition     = Kinematics::InterpolateAxisSmooth(SmoothedPosition, DesiredPosition, LocBlend);

        // Log-space distance damping keeps zoom/dolly scale-invariant across 0.35 m .. 2500 m
        const float LogCurr  = std::log(std::max(0.05f, SmoothedDistance));
        const float LogGoal  = std::log(DesiredDistance);
        SmoothedDistance     = std::exp(LogCurr + (LogGoal - LogCurr) * LocBlend);
    }
    else if (!Settings.LocationLagEnabled)
    {
        SmoothedPosition = DesiredPosition;
        SmoothedDistance = DesiredDistance;
    }

    if (Settings.MaximumLagDistanceMeters > 0.0f)
    {
        const Mathematics::Axis3D TrailOffset = SmoothedPosition - DesiredPosition;
        const float TrailLength = std::sqrt(Mathematics::EvaluateDot(TrailOffset, TrailOffset));
        if (TrailLength > Settings.MaximumLagDistanceMeters && TrailLength > 1.0e-6f)
        {
            SmoothedPosition = DesiredPosition + TrailOffset * (Settings.MaximumLagDistanceMeters / TrailLength);
        }
    }

    if (Settings.RotationLagEnabled && DeltaSeconds > 0.0f)
    {
        const float RotBlend = Kinematics::EvaluateExponentialBlendFactor(Settings.RotationLagSpeed, DeltaSeconds);
        SmoothedForward      = SlerpUnitDirection(SmoothedForward, DesiredForward, RotBlend);
    }
    else if (!Settings.RotationLagEnabled)
    {
        SmoothedForward = DesiredForward;
    }

    SmoothedTarget = SmoothedPosition + SmoothedForward * SmoothedDistance;

    const Mathematics::Axis3D PositionError = DesiredPosition - SmoothedPosition;
    const Mathematics::Axis3D PositionStep  = SmoothedPosition - PreviousPosition;
    const Mathematics::Axis3D ForwardError  = DesiredForward - SmoothedForward;
    const Mathematics::Axis3D ForwardStep   = SmoothedForward - PreviousForward;
    const float               DistRelError  = std::fabs(DesiredDistance - SmoothedDistance) / std::max(1.0f, DesiredDistance);

    const float PosErrSq  = Mathematics::EvaluateDot(PositionError, PositionError);
    const float PosStepSq = Mathematics::EvaluateDot(PositionStep, PositionStep);
    const float FwdErrSq  = Mathematics::EvaluateDot(ForwardError, ForwardError);
    const float FwdStepSq = Mathematics::EvaluateDot(ForwardStep, ForwardStep);

    const bool Settled =
        (PosErrSq < 1.0e-8f) &&
        (FwdErrSq < 1.0e-9f) &&
        (DistRelError < 1.0e-5f);
    if (Settled)
    {
        SmoothedPosition = DesiredPosition;
        SmoothedForward  = DesiredForward;
        SmoothedDistance = DesiredDistance;
        SmoothedTarget   = DesiredTarget;
    }

    return (PosStepSq > 1.0e-10f) || (FwdStepSq > 1.0e-11f) || ( std::fabs(SmoothedDistance - PreviousDistance) > 1.0e-5f );
}

} // namespace Frontier::Components
