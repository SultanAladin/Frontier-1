/*============================================================================================================================================
                                                          COMPOSITIONOVERLAY.H
============================================================================================================================================*/
// 🧩 Non-Shipping normalized 2D viewport composition overlay segments (Rule of Thirds, Broadcast Safe Gate, Optical Reticle)

#pragma once

#include "Engine/Configuration/BuildConfiguration.h"

#if !FRONTIER_PROFILE_SHIPPING

#include "Engine/Components/Camera/CameraConfiguration.h"

#include <utility>
#include <vector>

namespace Frontier::Components::Diagnostics
{

using NormalizedLineSegment = std::pair<Mathematics::Axis2D, Mathematics::Axis2D>;

[[nodiscard]] inline std::vector<NormalizedLineSegment> BuildCompositionGuideSegments(CompositionGuideStyle Style)
{
    std::vector<NormalizedLineSegment> Segments;
    if (Style == CompositionGuideStyle::None)
    {
        return Segments;
    }

    if (Style == CompositionGuideStyle::RuleOfThirds || Style == CompositionGuideStyle::CrosshairAndThirds)
    {
        Segments.push_back({ { 1.0f / 3.0f, 0.0f }, { 1.0f / 3.0f, 1.0f } });
        Segments.push_back({ { 2.0f / 3.0f, 0.0f }, { 2.0f / 3.0f, 1.0f } });
        Segments.push_back({ { 0.0f, 1.0f / 3.0f }, { 1.0f, 1.0f / 3.0f } });
        Segments.push_back({ { 0.0f, 2.0f / 3.0f }, { 1.0f, 2.0f / 3.0f } });
    }

    if (Style == CompositionGuideStyle::BroadcastSafeGate)
    {
        for (const float Margin : { 0.05f, 0.10f })
        {
            const float Lo = Margin;
            const float Hi = 1.0f - Margin;
            Segments.push_back({ { Lo, Lo }, { Hi, Lo } });
            Segments.push_back({ { Hi, Lo }, { Hi, Hi } });
            Segments.push_back({ { Hi, Hi }, { Lo, Hi } });
            Segments.push_back({ { Lo, Hi }, { Lo, Lo } });
        }
    }

    if (Style == CompositionGuideStyle::CrosshairAndThirds)
    {
        Segments.push_back({ { 0.465f, 0.50f }, { 0.535f, 0.50f } });
        Segments.push_back({ { 0.50f, 0.465f }, { 0.50f, 0.535f } });
    }

    return Segments;
}

} // namespace Frontier::Components::Diagnostics

#endif // !FRONTIER_PROFILE_SHIPPING
