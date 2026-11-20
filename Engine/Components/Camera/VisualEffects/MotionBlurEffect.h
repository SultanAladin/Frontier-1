/*============================================================================================================================================
                                                             MOTIONBLUREFFECT.H
============================================================================================================================================*/
// 🧩 3-Mode Camera Motion Blur (CameraVelocity reprojection, DirectionalShutter 180-deg blur, RadialZoom speed blur)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Frontier::Components::VisualEffects
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr uint32_t PackMotionBlurMask(const MotionBlurParameters& Params) noexcept
{
    return (Params.Enabled && Params.Intensity > 1.0e-4f) ? 256u : 0u;
}

[[nodiscard]] inline float ResolveMotionBlurIntensity(const MotionBlurParameters& Params) noexcept
{
    if (!Params.Enabled)
    {
        return 0.0f;
    }
    return std::clamp(Params.Intensity, 0.0f, 2.0f);
}

[[nodiscard]] inline float ResolveMotionBlurShutterNorm(const MotionBlurParameters& Params) noexcept
{
    return std::clamp(Params.ShutterAngleDegrees / 360.0f, 0.05f, 1.0f);
}

[[nodiscard]] inline Mathematics::UVAxes ResolveMotionBlurVelocityUv(
    const CameraConfiguration& Config,
    const CameraPosture*       OptionalPosture,
    float                      AspectRatio) noexcept
{
    if (std::abs(Config.MotionBlur.VelocityUvOverrideX) > 1.0e-5f ||
        std::abs(Config.MotionBlur.VelocityUvOverrideY) > 1.0e-5f)
    {
        return { Config.MotionBlur.VelocityUvOverrideX, Config.MotionBlur.VelocityUvOverrideY };
    }

    if (Config.MotionBlur.Style == MotionBlurStyle::DirectionalShutter)
    {
        return {
            0.055f * std::cos(Config.MotionBlur.DirectionalAngleRad),
            0.055f * std::sin(Config.MotionBlur.DirectionalAngleRad)
        };
    }

    if (OptionalPosture == nullptr || !OptionalPosture->HasPreviousFrame)
    {
        return { 0.0f, 0.0f };
    }

    // Project reference focus point from previous frame into current screen vs current frame
    const float FocusDist = std::max(0.5f, Config.Optics.FocusDistanceMeters);
    const Mathematics::Axis3D WorldFocus =
        OptionalPosture->CurrentEyePosition + OptionalPosture->ForwardAxis * FocusDist;
    const Mathematics::Axis3D PrevRel = WorldFocus - OptionalPosture->PreviousEyePosition;
    const float PrevForward = std::max(0.1f, Mathematics::EvaluateDot(PrevRel, OptionalPosture->PreviousForwardAxis));
    const float PrevRight   = Mathematics::EvaluateDot(PrevRel, OptionalPosture->PreviousRightAxis);
    const float PrevUp      = Mathematics::EvaluateDot(PrevRel, OptionalPosture->PreviousUpAxis);

    const float TanY = std::max(1.0e-3f, std::tan(0.5f * OptionalPosture->VerticalFovDegrees * Constants::DegToRadFloat));
    const float TanX = TanY * std::max(0.25f, AspectRatio);

    const float PrevNdcX = PrevRight / (PrevForward * TanX);
    const float PrevNdcY = PrevUp    / (PrevForward * TanY);
    const float PrevU = 0.5f + 0.5f * PrevNdcX;
    const float PrevV = 0.5f - 0.5f * PrevNdcY;

    return { PrevU - 0.5f, PrevV - 0.5f };
}

} // namespace Frontier::Components::VisualEffects
