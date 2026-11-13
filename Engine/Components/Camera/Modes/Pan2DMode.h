/*============================================================================================================================================
                                                               PAN2DMODE.H
============================================================================================================================================*/
// 🧩 Smoothed 2D orthographic UDIM canvas pan and logarithmic zoom kinematics

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"
#include "Engine/Components/Camera/Constraints/BoundsConstraint.h"
#include "Engine/Components/Camera/Kinematics/InterpolationCurves.h"

namespace Frontier::Components::Modes
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline void AdvancePan2DKinematics(
    const CameraConfiguration& Config,
    CameraPosture&             Posture,
    float                      DeltaSeconds) noexcept
{
    Posture.TargetPlanarZoom   = std::clamp(Posture.TargetPlanarZoom, 0.15f, 32.0f);
    Posture.TargetPlanarCenter = Constraints::ClampPlanarCenterToBounds(Posture.TargetPlanarCenter, Config.Bounds);

    const float PanBlend  = Kinematics::EvaluateExponentialBlendFactor(Config.Kinematics.PanSmoothingRate, DeltaSeconds);
    const float ZoomBlend = Kinematics::EvaluateExponentialBlendFactor(Config.Kinematics.ZoomSmoothingRate, DeltaSeconds);

    Posture.CurrentPlanarCenter = Kinematics::InterpolatePlanarSmooth(Posture.CurrentPlanarCenter, Posture.TargetPlanarCenter, PanBlend);
    Posture.CurrentPlanarZoom   = Kinematics::InterpolateLogarithmicDistance(Posture.CurrentPlanarZoom, Posture.TargetPlanarZoom, ZoomBlend);
}

} // namespace Frontier::Components::Modes
