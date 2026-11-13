/*============================================================================================================================================
                                                        ORTHOGRAPHICPROJECTION.H
============================================================================================================================================*/
// 🧩 Aspect-locked parallel 3D & 2D planar orthographic projection transforms

#pragma once

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"
#include "Engine/Mathematics/CoordinateSpace.h"

#include <algorithm>

namespace Frontier::Components::Projection
{

[[nodiscard]] inline Mathematics::ClipTransform4x4 EvaluateOrthographicClip(
    const OpticalLensParameters& Lens,
    const CameraPosture&         Posture,
    float                        ViewportAspectRatio) noexcept
{
    const float FarPlane = std::max(Lens.FarClipMeters, Posture.CurrentDistanceMeters * 6.0f);
    const float Span     = std::max(0.10f, Lens.OrthographicSpanMeters);
    return Mathematics::ConstructVulkanOrthographicViewClip(
        Posture.CurrentEyePosition,
        Posture.RightAxis,
        Posture.UpAxis,
        Posture.ForwardAxis,
        Span,
        ViewportAspectRatio,
        -FarPlane * 0.5f,
        FarPlane);
}

inline void EvaluatePlanarViewportBounds(
    const CameraPosture& Posture,
    float                DomainSpanU,
    float                DomainSpanV,
    uint32_t             ViewportWidthPx,
    uint32_t             ViewportHeightPx,
    Mathematics::UVAxes& OutMinimumUV,
    Mathematics::UVAxes& OutMaximumUV) noexcept
{
    const float SafeZoom      = std::max(0.05f, Posture.CurrentPlanarZoom);
    const float Aspect        = static_cast<float>(std::max(1u, ViewportWidthPx)) /
                                static_cast<float>(std::max(1u, ViewportHeightPx));
    const float ReferenceSpan = std::max(DomainSpanU, DomainSpanV) * 1.12f / SafeZoom;

    float HalfU = 0.5f * ReferenceSpan;
    float HalfV = 0.5f * ReferenceSpan;
    if (Aspect >= 1.0f)
    {
        HalfU *= Aspect;
    }
    else
    {
        HalfV /= std::max(Aspect, 1.0e-4f);
    }

    OutMinimumUV = { Posture.CurrentPlanarCenter.U - HalfU, Posture.CurrentPlanarCenter.V - HalfV };
    OutMaximumUV = { Posture.CurrentPlanarCenter.U + HalfU, Posture.CurrentPlanarCenter.V + HalfV };
}

} // namespace Frontier::Components::Projection
