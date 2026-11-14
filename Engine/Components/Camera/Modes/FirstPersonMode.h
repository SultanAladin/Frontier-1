/*============================================================================================================================================
                                                            FIRSTPERSONMODE.H
============================================================================================================================================*/
// 🧩 Eye-anchored world-up-aligned first-person navigation translating on the horizontal ground plane

#pragma once

#include "Engine/Components/Camera/Modes/FlyMode.h"

namespace Frontier::Components::Modes
{

inline void ApplyFirstPersonHorizontalStride(
    const CameraConfiguration& Config,
    CameraPosture&             Posture,
    float                      ForwardInput,
    float                      RightInput,
    float                      VerticalInput,
    bool                       SprintBoost,
    float                      DeltaSeconds) noexcept
{
    const float Speed = Config.Kinematics.FlySpeedMetersPerSec *
                        (SprintBoost ? Config.Kinematics.FlyBoostMultiplier : 1.0f) *
                        std::max(0.0f, DeltaSeconds);
    const Mathematics::Axis3D WorldUp  = Mathematics::QueryWorldUpAxis(Config.WorldBasis);
    const Mathematics::Axis3D RawFwd   = EvaluateForwardFromLookAngles(Posture.TargetYawRadians, 0.0f, Config.WorldBasis);
    const Mathematics::Axis3D LevelFwd = Mathematics::NormalizeAxis(
        RawFwd - WorldUp * Mathematics::EvaluateDot(RawFwd, WorldUp),
        Mathematics::QueryWorldForwardAxis(Config.WorldBasis));
    const Mathematics::Axis3D LevelRight = Mathematics::NormalizeAxis(
        Mathematics::EvaluateCross(LevelFwd, WorldUp),
        { 1.0f, 0.0f, 0.0f });

    Posture.TargetEyePosition =
        Posture.TargetEyePosition +
        LevelFwd * (ForwardInput * Speed) +
        LevelRight * (RightInput * Speed) +
        WorldUp * (VerticalInput * Speed);
}

inline void AdvanceFirstPersonKinematics(
    const CameraConfiguration& Config,
    CameraPosture&             Posture,
    float                      DeltaSeconds) noexcept
{
    Posture.TargetRollRadians  = 0.0f;
    Posture.CurrentRollRadians = 0.0f;
    AdvanceFlyKinematics(Config, Posture, DeltaSeconds);
}

} // namespace Frontier::Components::Modes
