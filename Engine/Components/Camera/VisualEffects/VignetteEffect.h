/*============================================================================================================================================
                                                            VIGNETTEEFFECT.H
============================================================================================================================================*/
// 🧩 Natural cos^4 optical falloff parameter validation and GPU bitmask packing

#pragma once

#include "Engine/Components/Camera/CameraConfiguration.h"

#include <algorithm>
#include <cstdint>

namespace Frontier::Components::VisualEffects
{

[[nodiscard]] constexpr uint32_t PackVignetteMask(const VignetteParameters& Params) noexcept
{
    return (Params.Enabled && Params.Intensity > 1.0e-4f) ? 64u : 0u;
}

[[nodiscard]] constexpr float ResolveVignetteIntensity(const VignetteParameters& Params) noexcept
{
    return Params.Enabled ? std::clamp(Params.Intensity, 0.0f, 1.0f) : 0.0f;
}

} // namespace Frontier::Components::VisualEffects
