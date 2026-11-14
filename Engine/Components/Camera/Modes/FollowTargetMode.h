/*============================================================================================================================================
                                                            FOLLOWTARGETMODE.H
============================================================================================================================================*/
// 🧩 Critically-damped spring pursuit tracking a moving target anchor with collision-guarded chase offset

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"
#include "Engine/Components/Camera/Constraints/CollisionAvoidance.h"
#include "Engine/Components/Camera/Constraints/PitchConstraint.h"
#include "Engine/Components/Camera/Kinematics/InterpolationCurves.h"
#include "Engine/Components/Camera/Kinematics/SpringDamper.h"

namespace Frontier::Components::Modes
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline void AdvanceFollowTargetKinematics(
    const CameraConfiguration& Config,
    CameraPosture&             Posture,
    float                      DeltaSeconds) noexcept
{
    Posture.TargetPitchRadians   = Constraints::ClampElevationPitchRadians(Posture.TargetPitchRadians, Config.Kinematics.PitchLimitRadians);
    Posture.TargetDistanceMeters = std::clamp(Posture.TargetDistanceMeters, Config.Kinematics.MinDistanceMeters, Config.Kinematics.MaxDistanceMeters);

    Kinematics::AdvanceCriticallyDampedSpring(
        Posture.CurrentPivot,
        Posture.FollowPivotVelocity,
        Posture.TargetPivot,
        Config.Kinematics.FollowSpringFrequency,
        DeltaSeconds);

    const float OrbitBlend = Kinematics::EvaluateExponentialBlendFactor(Config.Kinematics.OrbitSmoothingRate, DeltaSeconds);
    const float ZoomBlend  = Kinematics::EvaluateExponentialBlendFactor(Config.Kinematics.ZoomSmoothingRate, DeltaSeconds);

    Posture.CurrentYawRadians    = Kinematics::InterpolateScalarSmooth(Posture.CurrentYawRadians, Posture.TargetYawRadians, OrbitBlend);
    Posture.CurrentPitchRadians  = Kinematics::InterpolateScalarSmooth(Posture.CurrentPitchRadians, Posture.TargetPitchRadians, OrbitBlend);
    const float SmoothedDistance = Kinematics::InterpolateLogarithmicDistance(Posture.CurrentDistanceMeters, Posture.TargetDistanceMeters, ZoomBlend);

    const Mathematics::Axis3D DesiredEye = Posture.CurrentPivot + Mathematics::EvaluateOrbitOffsetFromAngles(
                                                                      SmoothedDistance,
                                                                      Posture.CurrentYawRadians,
                                                                      Posture.CurrentPitchRadians,
                                                                      Config.WorldBasis);
    const float SafeDist = Constraints::ResolveUnoccludedOrbitDistance(
        Posture.CurrentPivot,
        DesiredEye,
        Config.Kinematics.MinDistanceMeters,
        Config.Collision);

    Posture.CurrentDistanceMeters = SafeDist;
    Posture.CurrentEyePosition    = Posture.CurrentPivot + Mathematics::EvaluateOrbitOffsetFromAngles(
                                                            SafeDist,
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
