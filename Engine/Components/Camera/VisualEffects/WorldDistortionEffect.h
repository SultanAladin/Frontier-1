/*============================================================================================================================================
                                                           WORLDDISTORTIONEFFECT.H
============================================================================================================================================*/
// 🧩 World-placed camera distortion anchors (Heat Shimmer, Black Hole Lensing, Glitch) + 3D->Screen projection

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Frontier::Components::VisualEffects
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ProjectedScreenDistortionAnchor
{
    WorldDistortionCategory Kind               = WorldDistortionCategory::HeatShimmer;
    float               ScreenU            = 0.5f;
    float               ScreenV            = 0.5f;
    float               ScreenRadiusUv     = 0.12f;
    float               LinearDepthMeters  = 4.0f;
    float               Strength           = 1.0f;
    float               SecondaryParam     = 0.0f;
    bool                Visible            = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr uint32_t PackHeatShimmerMask(const HeatShimmerParameters& Params) noexcept
{
    return (Params.Enabled && Params.Intensity > 1.0e-4f) ? 512u : 0u;
}

[[nodiscard]] constexpr uint32_t PackBlackHoleLensMask(const BlackHoleLensParameters& Params) noexcept
{
    return (Params.Enabled && (Params.LensingStrength > 1.0e-4f || Params.AccretionGlowIntensity > 1.0e-4f)) ? 1024u : 0u;
}

[[nodiscard]] constexpr uint32_t PackGlitchEffectMask(const GlitchEffectParameters& Params) noexcept
{
    return (Params.Enabled && Params.Intensity > 1.0e-4f) ? 2048u : 0u;
}

[[nodiscard]] inline ProjectedScreenDistortionAnchor ProjectWorldDistortionAnchor(
    const CameraConfiguration&         Config,
    const CameraPosture&               Posture,
    const CameraWorldDistortionAnchor& Anchor,
    float                              AspectRatio) noexcept
{
    ProjectedScreenDistortionAnchor Out {};
    Out.Kind           = Anchor.Kind;
    Out.Strength       = std::clamp(Anchor.Strength, 0.0f, 4.0f);
    Out.SecondaryParam = Anchor.SecondaryParam;
    if (!Anchor.Active || Out.Strength <= 1.0e-4f)
    {
        return Out;
    }

    const Mathematics::Axis3D Rel = Anchor.WorldPosition - Posture.CurrentEyePosition;
    const float ForwardDepth      = Mathematics::EvaluateDot(Rel, Posture.ForwardAxis);
    if (ForwardDepth <= std::max(0.05f, Config.Optics.NearClipMeters * 0.5f))
    {
        return Out;
    }

    const float RightOffset = Mathematics::EvaluateDot(Rel, Posture.RightAxis);
    const float UpOffset    = Mathematics::EvaluateDot(Rel, Posture.UpAxis);

    float NdcX = 0.0f;
    float NdcY = 0.0f;
    float RadV = 0.1f;

    if (Config.ProjectionMode == CameraProjectionMode::Orthographic)
    {
        const float HalfH = std::max(0.05f, 0.5f * Config.Optics.OrthographicSpanMeters);
        const float HalfW = HalfH * std::max(0.25f, AspectRatio);
        NdcX = RightOffset / HalfW;
        NdcY = UpOffset / HalfH;
        RadV = Anchor.RadiusMeters / (2.0f * HalfH);
    }
    else
    {
        const float TanY = std::max(1.0e-3f, std::tan(0.5f * Posture.VerticalFovDegrees * Constants::DegToRadFloat));
        const float TanX = TanY * std::max(0.25f, AspectRatio);
        NdcX = RightOffset / (ForwardDepth * TanX);
        NdcY = UpOffset    / (ForwardDepth * TanY);
        RadV = Anchor.RadiusMeters / (2.0f * ForwardDepth * TanY);
    }

    Out.ScreenU           = 0.5f + 0.5f * NdcX;
    Out.ScreenV           = 0.5f - 0.5f * NdcY;
    Out.ScreenRadiusUv    = std::clamp(RadV, 0.01f, 0.95f);
    Out.LinearDepthMeters = ForwardDepth;
    Out.Visible           = (Out.ScreenU + Out.ScreenRadiusUv * 1.5f >= -0.25f) &&
                            (Out.ScreenU - Out.ScreenRadiusUv * 1.5f <=  1.25f) &&
                            (Out.ScreenV + Out.ScreenRadiusUv * 1.5f >= -0.25f) &&
                            (Out.ScreenV - Out.ScreenRadiusUv * 1.5f <=  1.25f);
    return Out;
}

} // namespace Frontier::Components::VisualEffects
