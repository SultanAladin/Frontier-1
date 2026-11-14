/*============================================================================================================================================
                                                          CHROMATICABERRATION.H
============================================================================================================================================*/
// 🧩 Radial lateral chromatic dispersion uniform evaluation for CameraVisualEffects.slang

#pragma once

#include "Engine/Components/Camera/CameraConfiguration.h"

#include <algorithm>
#include <cstdint>

namespace Frontier::Components::VisualEffects
{

[[nodiscard]] constexpr uint32_t PackChromaticAberrationMask(const ChromaticAberrationParameters& Params) noexcept
{
    return (Params.Enabled && Params.DispersionStrength > 1.0e-4f) ? 2u : 0u;
}

[[nodiscard]] constexpr float ResolveChromaticDispersionStrength(const ChromaticAberrationParameters& Params) noexcept
{
    return Params.Enabled ? std::clamp(Params.DispersionStrength, 0.0f, 4.0f) : 0.0f;
}

} // namespace Frontier::Components::VisualEffects
