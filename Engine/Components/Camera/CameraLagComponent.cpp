/*============================================================================================================================================
                                                        CAMERALAGCOMPONENT.CPP
============================================================================================================================================*/
// 🧩 Reusable camera lag: exponentially-damped follow of a desired camera pose (Unreal-style lag speed)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraLagComponent.h"

#include "Engine/Components/Camera/Kinematics/InterpolationCurves.h"

#include <cmath>

namespace Frontier::Components
{

void CameraLagComponent::SnapTo(Mathematics::Axis3D Position, Mathematics::Axis3D Target) noexcept
{
    SmoothedPosition = Position;
    SmoothedTarget   = Target;
    Primed           = true;
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
    const Mathematics::Axis3D PreviousTarget   = SmoothedTarget;

    if (Settings.LocationLagEnabled && DeltaSeconds > 0.0f)
    {
        const float Blend = Kinematics::EvaluateExponentialBlendFactor(Settings.LocationLagSpeed, DeltaSeconds);
        SmoothedPosition  = Kinematics::InterpolateAxisSmooth(SmoothedPosition, DesiredPosition, Blend);
    }
    else if (!Settings.LocationLagEnabled)
    {
        SmoothedPosition = DesiredPosition;
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
        const float Blend = Kinematics::EvaluateExponentialBlendFactor(Settings.RotationLagSpeed, DeltaSeconds);
        SmoothedTarget    = Kinematics::InterpolateAxisSmooth(SmoothedTarget, DesiredTarget, Blend);
    }
    else if (!Settings.RotationLagEnabled)
    {
        SmoothedTarget = DesiredTarget;
    }

    const Mathematics::Axis3D PositionStep = SmoothedPosition - PreviousPosition;
    const Mathematics::Axis3D TargetStep   = SmoothedTarget - PreviousTarget;
    const float StepSq = Mathematics::EvaluateDot(PositionStep, PositionStep) + Mathematics::EvaluateDot(TargetStep, TargetStep);
    return StepSq > 1.0e-10f;
}

} // namespace Frontier::Components
