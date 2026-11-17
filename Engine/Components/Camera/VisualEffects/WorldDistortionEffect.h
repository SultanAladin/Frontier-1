/*============================================================================================================================================
                                                           WORLDDISTORTIONEFFECT.H
============================================================================================================================================*/
// 🧩 World-placed & particle-linked camera distortion anchors (Heat Shimmer, Black Hole Lensing, Glitch) + 3D->Screen projection

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Camera/CameraConfiguration.h"
#include "Engine/Components/Camera/CameraPosture.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/ParticleSimulationSession.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <variant>
#include <vector>

namespace Frontier::Components::VisualEffects
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ProjectedScreenDistortionAnchor
{
    WorldDistortionKind Kind               = WorldDistortionKind::HeatShimmer;
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

inline void ResolveReleaseOriginCenterAndRadius(
    const ParticleSpace::ReleaseOriginSpec& Origin,
    Mathematics::Axis3D&                    OutCenter,
    float&                                  OutRadiusMeters) noexcept
{
    std::visit([&](const auto& O)
    {
        using T = std::decay_t<decltype(O)>;
        if constexpr (std::is_same_v<T, ParticleSpace::AnalyticPointOrigin>)
        {
            OutCenter       = { O.CenterMeters[0], O.CenterMeters[1], O.CenterMeters[2] };
            OutRadiusMeters = 0.85f;
        }
        else if constexpr (std::is_same_v<T, ParticleSpace::AnalyticSphereOrigin>)
        {
            OutCenter       = { O.CenterMeters[0], O.CenterMeters[1], O.CenterMeters[2] };
            OutRadiusMeters = std::max(0.35f, O.RadiusMeters * 1.8f);
        }
        else if constexpr (std::is_same_v<T, ParticleSpace::AnalyticDiscOrigin>)
        {
            OutCenter       = { O.CenterMeters[0], O.CenterMeters[1], O.CenterMeters[2] };
            OutRadiusMeters = std::max(0.35f, O.RadiusMeters * 1.35f);
        }
        else if constexpr (std::is_same_v<T, ParticleSpace::AnalyticBoxOrigin>)
        {
            OutCenter       = { O.CenterMeters[0], O.CenterMeters[1], O.CenterMeters[2] };
            OutRadiusMeters = std::max({ 0.45f, O.HalfExtentsMeters[0], O.HalfExtentsMeters[1], O.HalfExtentsMeters[2] });
        }
        else if constexpr (std::is_same_v<T, ParticleSpace::DualElectrodeOrigin>)
        {
            OutCenter = {
                0.5f * (O.CathodeMeters[0] + O.AnodeMeters[0]),
                0.5f * (O.CathodeMeters[1] + O.AnodeMeters[1]),
                0.5f * (O.CathodeMeters[2] + O.AnodeMeters[2])
            };
            const float Dx = O.AnodeMeters[0] - O.CathodeMeters[0];
            const float Dy = O.AnodeMeters[1] - O.CathodeMeters[1];
            const float Dz = O.AnodeMeters[2] - O.CathodeMeters[2];
            OutRadiusMeters = std::max(0.5f, 0.55f * std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
        }
        else if constexpr (std::is_same_v<T, ParticleSpace::SurfaceLatticeOrigin>)
        {
            OutCenter       = { O.CenterMeters[0], O.CenterMeters[1], O.CenterMeters[2] };
            OutRadiusMeters = std::max({ 0.5f, O.HalfExtentsMeters[0], O.HalfExtentsMeters[1], O.HalfExtentsMeters[2] });
        }
        else if constexpr (std::is_same_v<T, ParticleSpace::SpiderwebOrigin>)
        {
            OutCenter       = { O.CenterMeters[0], O.CenterMeters[1], O.CenterMeters[2] };
            OutRadiusMeters = std::max(0.5f, O.RadiusMeters);
        }
        else if constexpr (std::is_same_v<T, ParticleSpace::ClothGridOrigin>)
        {
            OutCenter       = { O.OriginMeters[0], O.OriginMeters[1], O.OriginMeters[2] };
            OutRadiusMeters = std::max(0.5f, 0.6f * std::max(O.SpanMeters, O.DropMeters));
        }
        else if constexpr (std::is_same_v<T, ParticleSpace::BotanicalRootOrigin>)
        {
            OutCenter       = { O.CenterMeters[0], O.CenterMeters[1], O.CenterMeters[2] };
            OutRadiusMeters = std::max(0.5f, std::max(O.PatchRadius, O.CanopyHeight));
        }
        else if constexpr (std::is_same_v<T, ParticleSpace::SurfaceAnchorOrigin>)
        {
            OutCenter       = { O.CenterMeters[0], O.CenterMeters[1], O.CenterMeters[2] };
            OutRadiusMeters = std::max({ 0.45f, O.ScaleMeters[0], O.ScaleMeters[1], O.ScaleMeters[2] });
        }
        else if constexpr (std::is_same_v<T, ParticleSpace::FireworkOrigin>)
        {
            OutCenter       = { O.ShellOrigin[0], O.ShellOrigin[1], O.ShellOrigin[2] };
            OutRadiusMeters = std::max(0.5f, O.PatternRadius);
        }
    }, Origin);
}

inline void LinkParticleRigDistortionAnchor(
    const ParticleSpace::ParticleRig&          Rig,
    std::vector<CameraWorldDistortionAnchor>&  InOutAnchors)
{
    if (!Rig.Visible)
    {
        return;
    }

    Mathematics::Axis3D Center { 0.0f, 0.0f, 0.0f };
    float               Radius = 1.0f;
    ResolveReleaseOriginCenterAndRadius(Rig.Origin, Center, Radius);

    if (const auto* Lens = std::get_if<ParticleSpace::RefractionLensCarrier>(&Rig.Carrier))
    {
        CameraWorldDistortionAnchor Anchor {};
        Anchor.Kind           = Lens->GravitationalBlackHole
            ? WorldDistortionKind::BlackHole
            : WorldDistortionKind::HeatShimmer;
        Anchor.WorldPosition  = Center;
        Anchor.RadiusMeters   = std::max(0.45f, Radius * 1.25f);
        Anchor.Strength       = std::max(0.1f, Lens->DistortionStrength);
        Anchor.SecondaryParam = static_cast<float>(Rig.LocalSeed % 100u) * 0.0628f;
        Anchor.Active         = true;
        InOutAnchors.push_back(Anchor);
        return;
    }

    if (const auto* Sprite = std::get_if<ParticleSpace::SpriteBillboardCarrier>(&Rig.Carrier))
    {
        if (Sprite->Cutout == ParticleSpace::SpriteCutoutCategory::HologramGlitch)
        {
            CameraWorldDistortionAnchor Anchor {};
            Anchor.Kind           = WorldDistortionKind::Glitch;
            Anchor.WorldPosition  = Center;
            Anchor.RadiusMeters   = std::max(0.5f, Radius * 1.25f);
            Anchor.Strength       = 1.0f;
            Anchor.SecondaryParam = static_cast<float>(Rig.LocalSeed % 100u) * 0.0628f;
            Anchor.Active         = true;
            InOutAnchors.push_back(Anchor);
            return;
        }
    }

    if (Rig.Dynamics.Transition.has_value() && Rig.Dynamics.Transition->TransitionVariant == 7u)
    {
        CameraWorldDistortionAnchor Anchor {};
        Anchor.Kind           = WorldDistortionKind::Glitch;
        Anchor.WorldPosition  = Center;
        Anchor.RadiusMeters   = std::max(0.5f, Radius * 1.3f);
        Anchor.Strength       = 1.0f;
        Anchor.SecondaryParam = Rig.Dynamics.Transition->GlitchRateHz;
        Anchor.Active         = true;
        InOutAnchors.push_back(Anchor);
        return;
    }

    // Thermal buoyancy combustion rigs naturally emit convective heat shimmer above the flame plume
    if (Rig.Dynamics.Ballistic.BuoyancyMps2 > 2.2f || Rig.Dynamics.Thermal.has_value())
    {
        CameraWorldDistortionAnchor Anchor {};
        Anchor.Kind           = WorldDistortionKind::HeatShimmer;
        Anchor.WorldPosition  = { Center.X, Center.Y, Center.Z + Radius * 0.55f };
        Anchor.RadiusMeters   = std::max(0.55f, Radius * 1.25f);
        Anchor.Strength       = std::clamp(std::max(0.55f, Rig.Dynamics.Ballistic.BuoyancyMps2 * 0.22f), 0.35f, 1.5f);
        Anchor.SecondaryParam = static_cast<float>(Rig.LocalSeed % 100u) * 0.0628f;
        Anchor.Active         = true;
        InOutAnchors.push_back(Anchor);
    }
}

inline void LinkParticleDocumentDistortionAnchors(
    const ParticleSpace::ParticleDocument&     Doc,
    std::vector<CameraWorldDistortionAnchor>&  InOutAnchors)
{
    for (const auto& Rig : Doc.Rigs)
    {
        LinkParticleRigDistortionAnchor(Rig, InOutAnchors);
    }
}

inline void LinkParticleSessionDistortionAnchors(
    const ParticleSpace::ParticleSimulationSession& Session,
    std::vector<CameraWorldDistortionAnchor>&       InOutAnchors)
{
    LinkParticleDocumentDistortionAnchors(Session.InspectDocument(), InOutAnchors);
}

} // namespace Frontier::Components::VisualEffects
