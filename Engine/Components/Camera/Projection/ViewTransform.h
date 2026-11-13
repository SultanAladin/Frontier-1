/*============================================================================================================================================
                                                             VIEWTRANSFORM.H
============================================================================================================================================*/
// 🧩 Unified camera View-Projection clip evaluation and viewport screen-to-world picking ray construction

#pragma once

#include "Engine/Components/Camera/Projection/OrthographicProjection.h"
#include "Engine/Components/Camera/Projection/PerspectiveProjection.h"

namespace Frontier::Components::Projection
{

struct SpatialPickingRay
{
    Mathematics::Axis3D Origin    { 0.0f, 0.0f, 0.0f }; // [m] - World-space ray origin
    Mathematics::Axis3D Direction { 0.0f, 1.0f, 0.0f }; // [-] - Normalized world-space ray direction
};

[[nodiscard]] inline Mathematics::ClipTransform4x4 EvaluateViewClipTransform(
    const CameraConfiguration& Config,
    const CameraPosture&       Posture,
    float                      ViewportAspectRatio) noexcept
{
    if (Config.ProjectionMode == CameraProjectionMode::Orthographic)
    {
        return EvaluateOrthographicClip(Config.Optics, Posture, ViewportAspectRatio);
    }
    return EvaluatePerspectiveClip(Config.Optics, Posture, ViewportAspectRatio);
}

[[nodiscard]] inline SpatialPickingRay ConstructSpatialPickingRay(
    const CameraConfiguration& Config,
    const CameraPosture&       Posture,
    uint32_t                   ViewportWidthPx,
    uint32_t                   ViewportHeightPx,
    float                      LocalPixelX,
    float                      LocalPixelY) noexcept
{
    const float SafeW  = static_cast<float>(std::max(1u, ViewportWidthPx));
    const float SafeH  = static_cast<float>(std::max(1u, ViewportHeightPx));
    const float Aspect = SafeW / SafeH;
    const float NdcX   = (LocalPixelX / SafeW) * 2.0f - 1.0f;
    const float NdcY   = 1.0f - (LocalPixelY / SafeH) * 2.0f;

    if (Config.ProjectionMode == CameraProjectionMode::Orthographic)
    {
        const float HalfH = std::max(0.05f, 0.5f * Config.Optics.OrthographicSpanMeters);
        const float HalfW = HalfH * Aspect;
        const Mathematics::Axis3D Origin =
            Posture.CurrentEyePosition +
            Posture.RightAxis * (NdcX * HalfW) +
            Posture.UpAxis * (NdcY * HalfH);
        return { Origin, Posture.ForwardAxis };
    }

    const float TanHalf = std::tan(0.5f * Posture.VerticalFovDegrees * Constants::DegToRadFloat);
    const Mathematics::Axis3D Dir = Mathematics::NormalizeAxis(
        Posture.ForwardAxis +
        Posture.RightAxis * (NdcX * Aspect * TanHalf) +
        Posture.UpAxis * (NdcY * TanHalf),
        Posture.ForwardAxis);
    return { Posture.CurrentEyePosition, Dir };
}

} // namespace Frontier::Components::Projection
