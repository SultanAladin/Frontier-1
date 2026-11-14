/*============================================================================================================================================
                                                               ORBITMODE.H
============================================================================================================================================*/
// 🧩 Pivot-centered right-handed spherical turntable navigation with exponential angular/pivot/log-distance smoothing

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"
#include "Engine/Components/Camera/Constraints/BoundsConstraint.h"
#include "Engine/Components/Camera/Constraints/CollisionAvoidance.h"
#include "Engine/Components/Camera/Constraints/PitchConstraint.h"
#include "Engine/Components/Camera/Kinematics/InterpolationCurves.h"

namespace Frontier::Components::Modes
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline void AdvanceOrbitKinematics(
    const CameraConfiguration& Config,
    CameraPosture&             Posture,
    float                      DeltaSeconds) noexcept
{
    Posture.TargetPitchRadians   = Constraints::ClampElevationPitchRadians(Posture.TargetPitchRadians, Config.Kinematics.PitchLimitRadians);
    Posture.TargetDistanceMeters = std::clamp(Posture.TargetDistanceMeters, Config.Kinematics.MinDistanceMeters, Config.Kinematics.MaxDistanceMeters);
    Posture.TargetPivot          = Constraints::ClampSpatialPositionToBounds(Posture.TargetPivot, Config.Bounds);

    const float OrbitBlend = Kinematics::EvaluateExponentialBlendFactor(Config.Kinematics.OrbitSmoothingRate, DeltaSeconds);
    const float PanBlend   = Kinematics::EvaluateExponentialBlendFactor(Config.Kinematics.PanSmoothingRate, DeltaSeconds);
    const float ZoomBlend  = Kinematics::EvaluateExponentialBlendFactor(Config.Kinematics.ZoomSmoothingRate, DeltaSeconds);

    Posture.CurrentYawRadians     = Kinematics::InterpolateScalarSmooth(Posture.CurrentYawRadians, Posture.TargetYawRadians, OrbitBlend);
    Posture.CurrentPitchRadians   = Kinematics::InterpolateScalarSmooth(Posture.CurrentPitchRadians, Posture.TargetPitchRadians, OrbitBlend);
    Posture.CurrentRollRadians    = Kinematics::InterpolateScalarSmooth(Posture.CurrentRollRadians, Posture.TargetRollRadians, OrbitBlend);
    Posture.CurrentPivot          = Kinematics::InterpolateAxisSmooth(Posture.CurrentPivot, Posture.TargetPivot, PanBlend);
    const float SmoothedDistance  = Kinematics::InterpolateLogarithmicDistance(Posture.CurrentDistanceMeters, Posture.TargetDistanceMeters, ZoomBlend);

    const Mathematics::Axis3D DesiredOffset = Mathematics::EvaluateOrbitOffsetFromAngles(
        SmoothedDistance,
        Posture.CurrentYawRadians,
        Posture.CurrentPitchRadians,
        Config.WorldBasis);
    const Mathematics::Axis3D DesiredEye = Posture.CurrentPivot + DesiredOffset;

    const float UnoccludedDist = Constraints::ResolveUnoccludedOrbitDistance(
        Posture.CurrentPivot,
        DesiredEye,
        Config.Kinematics.MinDistanceMeters,
        Config.Collision);
    Posture.CurrentDistanceMeters = UnoccludedDist;
    Posture.CurrentEyePosition    = Posture.CurrentPivot + Mathematics::EvaluateOrbitOffsetFromAngles(
                                                            UnoccludedDist,
                                                            Posture.CurrentYawRadians,
                                                            Posture.CurrentPitchRadians,
                                                            Config.WorldBasis);

    Mathematics::EvaluateOrthonormalCameraBasis(
        Posture.CurrentEyePosition,
        Posture.CurrentPivot,
        Config.WorldBasis,
        Posture.RightAxis,
        Posture.UpAxis,
        Posture.ForwardAxis);
}

} // namespace Frontier::Components::Modes
