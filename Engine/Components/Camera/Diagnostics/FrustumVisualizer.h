/*============================================================================================================================================
                                                          FRUSTUMVISUALIZER.H
============================================================================================================================================*/
// 🧩 Non-Shipping 8-corner / 12-segment world-space camera frustum outline generator

#pragma once

#include "Engine/Configuration/BuildConfiguration.h"

#if !FRONTIER_PROFILE_SHIPPING

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"
#include "Engine/Constants/MathConstants.h"

#include <array>
#include <cmath>
#include <utility>

namespace Frontier::Components::Diagnostics
{

struct FrustumOutlineGeometry
{
    std::array<Mathematics::Axis3D, 8u>                                   Corners {}; // [0..3] Near (BL,BR,TR,TL), [4..7] Far (BL,BR,TR,TL)
    std::array<std::pair<Mathematics::Axis3D, Mathematics::Axis3D>, 12u>  Edges   {};
};

[[nodiscard]] inline FrustumOutlineGeometry ConstructFrustumOutline(
    const CameraConfiguration& Config,
    const CameraPosture&       Posture,
    float                      AspectRatio,
    float                      MaxVisualFarMeters = 24.0f) noexcept
{
    FrustumOutlineGeometry Out {};
    const float NearD = std::max(0.05f, Config.Optics.NearClipMeters);
    const float FarD  = std::max(NearD + 0.25f, std::min(Config.Optics.FarClipMeters, MaxVisualFarMeters));

    float NearHalfH = 0.0f;
    float FarHalfH  = 0.0f;
    if (Config.ProjectionMode == CameraProjectionMode::Orthographic)
    {
        NearHalfH = FarHalfH = 0.5f * std::max(0.10f, Config.Optics.OrthographicSpanMeters);
    }
    else
    {
        const float TanHalf = std::tan(0.5f * Posture.VerticalFovDegrees * Constants::DegToRadFloat);
        NearHalfH = NearD * TanHalf;
        FarHalfH  = FarD * TanHalf;
    }
    const float SafeAspect = std::max(0.1f, AspectRatio);
    const float NearHalfW  = NearHalfH * SafeAspect;
    const float FarHalfW   = FarHalfH * SafeAspect;

    const Mathematics::Axis3D NearCenter = Posture.CurrentEyePosition + Posture.ForwardAxis * NearD;
    const Mathematics::Axis3D FarCenter  = Posture.CurrentEyePosition + Posture.ForwardAxis * FarD;

    Out.Corners[0u] = NearCenter - Posture.RightAxis * NearHalfW - Posture.UpAxis * NearHalfH;
    Out.Corners[1u] = NearCenter + Posture.RightAxis * NearHalfW - Posture.UpAxis * NearHalfH;
    Out.Corners[2u] = NearCenter + Posture.RightAxis * NearHalfW + Posture.UpAxis * NearHalfH;
    Out.Corners[3u] = NearCenter - Posture.RightAxis * NearHalfW + Posture.UpAxis * NearHalfH;

    Out.Corners[4u] = FarCenter - Posture.RightAxis * FarHalfW - Posture.UpAxis * FarHalfH;
    Out.Corners[5u] = FarCenter + Posture.RightAxis * FarHalfW - Posture.UpAxis * FarHalfH;
    Out.Corners[6u] = FarCenter + Posture.RightAxis * FarHalfW + Posture.UpAxis * FarHalfH;
    Out.Corners[7u] = FarCenter - Posture.RightAxis * FarHalfW + Posture.UpAxis * FarHalfH;

    constexpr std::array<std::pair<uint8_t, uint8_t>, 12u> Indices {{
        { 0u, 1u }, { 1u, 2u }, { 2u, 3u }, { 3u, 0u },
        { 4u, 5u }, { 5u, 6u }, { 6u, 7u }, { 7u, 4u },
        { 0u, 4u }, { 1u, 5u }, { 2u, 6u }, { 3u, 7u },
    }};
    for (std::size_t Idx = 0u; Idx < Indices.size(); ++Idx)
    {
        Out.Edges[Idx] = { Out.Corners[Indices[Idx].first], Out.Corners[Indices[Idx].second] };
    }
    return Out;
}

} // namespace Frontier::Components::Diagnostics

#endif // !FRONTIER_PROFILE_SHIPPING
