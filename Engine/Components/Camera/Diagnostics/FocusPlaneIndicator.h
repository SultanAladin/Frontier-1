/*============================================================================================================================================
                                                          FOCUSPLANEINDICATOR.H
============================================================================================================================================*/
// 🧩 Non-Shipping world-space thin-lens focus plane quad and near/far acceptable sharpness plane markers

#pragma once

#include "Engine/Configuration/BuildConfiguration.h"

#if !FRONTIER_PROFILE_SHIPPING

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"
#include "Engine/Constants/MathConstants.h"

#include <array>
#include <cmath>

namespace Frontier::Components::Diagnostics
{

struct FocusPlaneQuad
{
    std::array<Mathematics::Axis3D, 4u> Corners {}; // World-space focus rectangle (BL, BR, TR, TL)
    float                               FocusDistanceMeters = 4.6f;
    float                               NearSharpMeters     = 3.3f;
    float                               FarSharpMeters      = 7.5f;
};

[[nodiscard]] inline FocusPlaneQuad ConstructFocusPlaneIndicator(
    const CameraConfiguration& Config,
    const CameraPosture&       Posture,
    float                      AspectRatio) noexcept
{
    FocusPlaneQuad Out {};
    Out.FocusDistanceMeters = std::max(Config.Optics.NearClipMeters, Config.Optics.FocusDistanceMeters);
    Out.NearSharpMeters     = Posture.NearSharpLimitMeters;
    Out.FarSharpMeters      = Posture.FarSharpLimitMeters;

    const float HalfH = (Config.ProjectionMode == CameraProjectionMode::Orthographic)
                            ? 0.5f * std::max(0.10f, Config.Optics.OrthographicSpanMeters)
                            : Out.FocusDistanceMeters * std::tan(0.5f * Posture.VerticalFovDegrees * Constants::DegToRadFloat);
    const float HalfW = HalfH * std::max(0.1f, AspectRatio);

    const Mathematics::Axis3D Center = Posture.CurrentEyePosition + Posture.ForwardAxis * Out.FocusDistanceMeters;
    Out.Corners[0u] = Center - Posture.RightAxis * HalfW - Posture.UpAxis * HalfH;
    Out.Corners[1u] = Center + Posture.RightAxis * HalfW - Posture.UpAxis * HalfH;
    Out.Corners[2u] = Center + Posture.RightAxis * HalfW + Posture.UpAxis * HalfH;
    Out.Corners[3u] = Center - Posture.RightAxis * HalfW + Posture.UpAxis * HalfH;
    return Out;
}

} // namespace Frontier::Components::Diagnostics

#endif // !FRONTIER_PROFILE_SHIPPING
