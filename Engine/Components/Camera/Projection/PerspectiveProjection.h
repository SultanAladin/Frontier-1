/*============================================================================================================================================
                                                         PERSPECTIVEPROJECTION.H
============================================================================================================================================*/
// 🧩 Physical thin-lens perspective optics: sensor gate + focal length -> FOV, entrance pupil, hyperfocal & Vulkan perspective clip

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/Mathematics/CoordinateSpace.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Components::Projection
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline float EvaluateVerticalFovDegrees(const OpticalLensParameters& Lens) noexcept
{
    const float SafeFocal  = std::clamp(Lens.FocalLengthMillimeters, 4.0f, 1200.0f);
    const float SafeHeight = std::max(2.0f, Lens.SensorHeightMillimeters);
    return 2.0f * std::atan(0.5f * SafeHeight / SafeFocal) * Constants::RadToDegFloat;
}

[[nodiscard]] inline float EvaluateHorizontalFovDegrees(const OpticalLensParameters& Lens) noexcept
{
    const float SafeFocal = std::clamp(Lens.FocalLengthMillimeters, 4.0f, 1200.0f);
    const float SafeWidth = std::max(2.0f, Lens.SensorWidthMillimeters);
    return 2.0f * std::atan(0.5f * SafeWidth / SafeFocal) * Constants::RadToDegFloat;
}

[[nodiscard]] inline float DeriveFocalLengthFromVerticalFov(
    float VerticalFovDegrees,
    float SensorHeightMillimeters) noexcept
{
    const float ClampedFov = std::clamp(VerticalFovDegrees, 5.0f, 160.0f) * Constants::DegToRadFloat;
    const float TanHalf    = std::max(1.0e-4f, std::tan(0.5f * ClampedFov));
    return std::clamp(0.5f * std::max(2.0f, SensorHeightMillimeters) / TanHalf, 4.0f, 1200.0f);
}

inline void RefreshOpticalTelemetry(
    const OpticalLensParameters& Lens,
    CameraPosture&               Posture) noexcept
{
    Posture.VerticalFovDegrees       = EvaluateVerticalFovDegrees(Lens);
    Posture.HorizontalFovDegrees     = EvaluateHorizontalFovDegrees(Lens);
    const float SafeFocalMm          = std::clamp(Lens.FocalLengthMillimeters, 4.0f, 1200.0f);
    const float SafeFStop            = std::clamp(Lens.ApertureFStop, 0.7f, 64.0f);
    Posture.EntrancePupilMillimeters = SafeFocalMm / SafeFStop;

    // Standard 35mm reference Circle of Confusion scaled proportionally to active sensor diagonal
    const float SensorDiagMm = std::sqrt(
        Lens.SensorWidthMillimeters * Lens.SensorWidthMillimeters +
        Lens.SensorHeightMillimeters * Lens.SensorHeightMillimeters);
    const float CoCLimitMm = std::max(0.005f, 0.029f * (SensorDiagMm / 43.2666f));

    const float HyperfocalMm = (SafeFocalMm * SafeFocalMm) / (SafeFStop * CoCLimitMm) + SafeFocalMm;
    Posture.HyperfocalMeters = HyperfocalMm * 0.001f;

    const float FocusMm = std::max(SafeFocalMm + 5.0f, Lens.FocusDistanceMeters * 1000.0f);
    const float NearMm  = (HyperfocalMm * FocusMm) / (HyperfocalMm + (FocusMm - SafeFocalMm));
    Posture.NearSharpLimitMeters = std::max(Lens.NearClipMeters, NearMm * 0.001f);

    if (FocusMm >= HyperfocalMm)
    {
        Posture.FarSharpLimitMeters = 1.0e5f;
    }
    else
    {
        const float FarMm = (HyperfocalMm * FocusMm) / std::max(1.0f, HyperfocalMm - (FocusMm - SafeFocalMm));
        Posture.FarSharpLimitMeters = std::min(1.0e5f, FarMm * 0.001f);
    }
}

[[nodiscard]] inline Mathematics::ClipTransform4x4 EvaluatePerspectiveClip(
    const OpticalLensParameters& Lens,
    const CameraPosture&         Posture,
    float                        ViewportAspectRatio) noexcept
{
    const float FarPlane = std::max(Lens.FarClipMeters, Posture.CurrentDistanceMeters * 6.0f);
    return Mathematics::ConstructVulkanPerspectiveViewClip(
        Posture.CurrentEyePosition,
        Posture.RightAxis,
        Posture.UpAxis,
        Posture.ForwardAxis,
        Posture.VerticalFovDegrees,
        ViewportAspectRatio,
        Lens.NearClipMeters,
        FarPlane);
}

} // namespace Frontier::Components::Projection
