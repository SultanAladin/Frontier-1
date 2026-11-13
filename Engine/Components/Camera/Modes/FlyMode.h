/*============================================================================================================================================
                                                                FLYMODE.H
============================================================================================================================================*/
// 🧩 Free 6-DOF spatial flight camera translating along Forward/Right/Up axes with smoothed look & position

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"
#include "Engine/Components/Camera/Constraints/BoundsConstraint.h"
#include "Engine/Components/Camera/Constraints/PitchConstraint.h"
#include "Engine/Components/Camera/Kinematics/InterpolationCurves.h"

namespace Frontier::Components::Modes
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline Mathematics::Axis3D EvaluateForwardFromLookAngles(
    float                             YawRadians,
    float                             PitchRadians,
    Mathematics::WorldCoordinateBasis Basis) noexcept
{
    const Mathematics::Axis3D BackOffset = Mathematics::EvaluateOrbitOffsetFromAngles(1.0f, YawRadians, PitchRadians, Basis);
    return Mathematics::NormalizeAxis(-BackOffset, Mathematics::QueryWorldForwardAxis(Basis));
}

inline void AdvanceFlyKinematics(
    const CameraConfiguration& Config,
    CameraPosture&             Posture,
    float                      DeltaSeconds) noexcept
{
    Posture.TargetPitchRadians = Constraints::ClampElevationPitchRadians(Posture.TargetPitchRadians, Config.Kinematics.PitchLimitRadians);
    Posture.TargetEyePosition  = Constraints::ClampSpatialPositionToBounds(Posture.TargetEyePosition, Config.Bounds);

    const float LookBlend = Kinematics::EvaluateExponentialBlendFactor(Config.Kinematics.OrbitSmoothingRate, DeltaSeconds);
    const float MoveBlend = Kinematics::EvaluateExponentialBlendFactor(Config.Kinematics.PanSmoothingRate, DeltaSeconds);

    Posture.CurrentYawRadians   = Kinematics::InterpolateScalarSmooth(Posture.CurrentYawRadians, Posture.TargetYawRadians, LookBlend);
    Posture.CurrentPitchRadians = Kinematics::InterpolateScalarSmooth(Posture.CurrentPitchRadians, Posture.TargetPitchRadians, LookBlend);
    Posture.CurrentRollRadians  = Kinematics::InterpolateScalarSmooth(Posture.CurrentRollRadians, Posture.TargetRollRadians, LookBlend);
    Posture.CurrentEyePosition  = Kinematics::InterpolateAxisSmooth(Posture.CurrentEyePosition, Posture.TargetEyePosition, MoveBlend);

    const Mathematics::Axis3D Forward = EvaluateForwardFromLookAngles(
        Posture.CurrentYawRadians,
        Posture.CurrentPitchRadians,
        Config.WorldBasis);
    Posture.CurrentPivot = Posture.CurrentEyePosition + Forward * Posture.CurrentDistanceMeters;

    Mathematics::EvaluateOrthonormalCameraBasis(
        Posture.CurrentEyePosition,
        Posture.CurrentPivot,
        Config.WorldBasis,
        Posture.RightAxis,
        Posture.UpAxis,
        Posture.ForwardAxis);
}

} // namespace Frontier::Components::Modes
