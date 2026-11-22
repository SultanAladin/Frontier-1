/*============================================================================================================================================
                                                        CAMERALAGCOMPONENT.CPP
============================================================================================================================================*/
// 🧩 Decoupled position + unit-sphere S^2 orientation exponential camera damping (immune to backward-flight target flips)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraLagComponent.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Components
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 CAMERA LAG COMPONENT
//------------------------------------------------------------------------------------------------------------------------

void CameraLagComponent::SnapTo(Mathematics::Axis3D Position, Mathematics::Axis3D Target) noexcept
{
    const Mathematics::Axis3D Offset = Target - Position;
    const float Dist = Mathematics::EvaluateLength(Offset);
    const Mathematics::Axis3D UnitFwd = (Dist > 1.0e-5f) ? (Offset * (1.0f / Dist)) : Mathematics::Axis3D{ 0.0f, 1.0f, 0.0f };
    SnapPose(Position, UnitFwd, std::max(0.05f, Dist));
}

void CameraLagComponent::SnapPose(
    Mathematics::Axis3D Position,
    Mathematics::Axis3D Forward,
    float               FocusDistanceMeters) noexcept
{
    SmoothedPosition      = Position;
    SmoothedForward       = Mathematics::NormalizeAxis(Forward, { 0.0f, 1.0f, 0.0f });
    SmoothedFocusDistance = std::max(0.05f, FocusDistanceMeters);
    Primed                = true;
}

bool CameraLagComponent::AdvanceLag(
    Mathematics::Axis3D DesiredPosition,
    Mathematics::Axis3D DesiredTarget,
    float               DeltaSeconds) noexcept
{
    const Mathematics::Axis3D Offset = DesiredTarget - DesiredPosition;
    const float Dist = Mathematics::EvaluateLength(Offset);
    const Mathematics::Axis3D DesiredFwd = (Dist > 1.0e-5f) ? (Offset * (1.0f / Dist)) : SmoothedForward;
    return AdvancePose(DesiredPosition, DesiredFwd, std::max(0.05f, Dist), DeltaSeconds);
}

bool CameraLagComponent::AdvancePose(
    Mathematics::Axis3D DesiredPosition,
    Mathematics::Axis3D DesiredForward,
    float               DesiredFocusDistanceMeters,
    float               DeltaSeconds) noexcept
{
    const Mathematics::Axis3D UnitDesiredFwd = Mathematics::NormalizeAxis(DesiredForward, SmoothedForward);
    const float SafeFocusDist                = std::max(0.05f, DesiredFocusDistanceMeters);

    if (!Primed || DeltaSeconds <= 0.0f || DeltaSeconds > 0.5f)
    {
        SnapPose(DesiredPosition, UnitDesiredFwd, SafeFocusDist);
        return false;
    }

    if (Settings.LocationLagEnabled && Settings.LocationLagSpeed > 0.0f)
    {
        const float AlphaPos = 1.0f - std::exp(-Settings.LocationLagSpeed * DeltaSeconds);
        SmoothedPosition     = SmoothedPosition + (DesiredPosition - SmoothedPosition) * AlphaPos;
        SmoothedFocusDistance += (SafeFocusDist - SmoothedFocusDistance) * AlphaPos;

        if (Settings.MaximumLagDistanceMeters > 0.0f)
        {
            const Mathematics::Axis3D Trail = SmoothedPosition - DesiredPosition;
            const float TrailLen            = Mathematics::EvaluateLength(Trail);
            if (TrailLen > Settings.MaximumLagDistanceMeters)
            {
                SmoothedPosition = DesiredPosition + Trail * (Settings.MaximumLagDistanceMeters / TrailLen);
            }
        }
    }
    else
    {
        SmoothedPosition      = DesiredPosition;
        SmoothedFocusDistance = SafeFocusDist;
    }

    if (Settings.RotationLagEnabled && Settings.RotationLagSpeed > 0.0f)
    {
        const float AlphaRot = 1.0f - std::exp(-Settings.RotationLagSpeed * DeltaSeconds);
        Mathematics::Axis3D Blended = SmoothedForward * (1.0f - AlphaRot) + UnitDesiredFwd * AlphaRot;
        if (Mathematics::EvaluateLengthSquared(Blended) < 1.0e-6f)
        {
            const Mathematics::Axis3D Ortho = (std::fabs(SmoothedForward.Z) < 0.9f)
                ? Mathematics::EvaluateCross(SmoothedForward, { 0.0f, 0.0f, 1.0f })
                : Mathematics::EvaluateCross(SmoothedForward, { 1.0f, 0.0f, 0.0f });
            Blended = SmoothedForward * (1.0f - AlphaRot) + Mathematics::NormalizeAxis(Ortho) * 0.25f + UnitDesiredFwd * AlphaRot;
        }
        SmoothedForward = Mathematics::NormalizeAxis(Blended, UnitDesiredFwd);
    }
    else
    {
        SmoothedForward = UnitDesiredFwd;
    }

    const float PosDeltaSq = Mathematics::EvaluateLengthSquared(DesiredPosition - SmoothedPosition);
    const float FwdDot     = Mathematics::EvaluateDot(SmoothedForward, UnitDesiredFwd);
    return (PosDeltaSq > 1.0e-6f) || (FwdDot < 0.999995f);
}

} // namespace Frontier::Components
