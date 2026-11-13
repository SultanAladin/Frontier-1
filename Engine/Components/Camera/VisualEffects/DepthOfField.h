/*============================================================================================================================================
                                                             DEPTHOFFIELD.H
============================================================================================================================================*/
// 🧩 Thin-lens Circle-of-Confusion diameter evaluation (mm & screen pixels) and GPU bitmask packing

#pragma once

#include "Engine/Components/Camera/CameraConfiguration.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Frontier::Components::VisualEffects
{

[[nodiscard]] constexpr uint32_t PackDepthOfFieldMask(const DepthOfFieldParameters& Params) noexcept
{
    return (Params.Enabled && Params.MaxBokehRadiusPx > 0.5f) ? 1u : 0u;
}

[[nodiscard]] inline float EvaluateCircleOfConfusionPixels(
    const OpticalLensParameters&  Lens,
    const DepthOfFieldParameters& DoF,
    float                         SubjectDepthMeters,
    uint32_t                      ViewportHeightPx) noexcept
{
    if (!DoF.Enabled || ViewportHeightPx == 0u)
    {
        return 0.0f;
    }
    const float FocalMm   = std::max(4.0f, Lens.FocalLengthMillimeters);
    const float FStop     = std::max(0.7f, Lens.ApertureFStop);
    const float FocusMm   = std::max(FocalMm + 10.0f, Lens.FocusDistanceMeters * 1000.0f);
    const float SubjectMm = std::max(FocalMm + 10.0f, SubjectDepthMeters * 1000.0f);
    const float SensorHMm = std::max(4.0f, Lens.SensorHeightMillimeters);

    const float PupilMm = FocalMm / FStop;
    const float CoCMm   = (std::abs(SubjectMm - FocusMm) / SubjectMm) *
                          ((PupilMm * FocalMm) / std::max(1.0f, FocusMm - FocalMm));
    const float CoCPx   = (CoCMm / SensorHMm) * static_cast<float>(ViewportHeightPx);
    return std::clamp(CoCPx, 0.0f, std::max(1.0f, DoF.MaxBokehRadiusPx));
}

} // namespace Frontier::Components::VisualEffects
