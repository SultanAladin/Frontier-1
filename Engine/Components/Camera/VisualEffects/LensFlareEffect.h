/*============================================================================================================================================
                                                            LENSFLAREEFFECT.H
============================================================================================================================================*/
// 🧩 Multi-layer physical lens flare & coated polygonal aperture ghost uniform packing for CameraVisualEffects.slang

#pragma once

#include "Engine/Components/Camera/CameraConfiguration.h"

#include <algorithm>
#include <cstdint>

namespace Frontier::Components::VisualEffects
{

[[nodiscard]] constexpr uint32_t PackLensFlareEffectMask(const LensFlareParameters& Params) noexcept
{
    if (!Params.Enabled || Params.FlareIntensity <= 1.0e-4f)
    {
        return 0u;
    }
    uint32_t Mask = 4u;
    if (Params.AnamorphicStreak)
    {
        Mask |= 8u;
    }
    if (Params.StarburstDiffraction)
    {
        Mask |= 16u;
    }
    if (Params.ApertureGhosts && Params.GhostReflectionCount > 0u && Params.GhostIntensity > 1.0e-4f)
    {
        Mask |= 32u;
    }
    return Mask;
}

[[nodiscard]] constexpr uint32_t PackGhostAndStarburstWord(const LensFlareParameters& Params) noexcept
{
    const uint32_t Count    = std::min<uint32_t>(Params.GhostReflectionCount, 24u) & 0xFFu;
    const uint32_t Sides    = static_cast<uint32_t>(Params.GhostShape) & 0xFFu;
    const uint32_t RayPairs = std::clamp<uint32_t>(Params.StarburstRayPairs, 2u, 12u) & 0xFFu;
    return Count | (Sides << 8u) | (RayPairs << 16u);
}

} // namespace Frontier::Components::VisualEffects
