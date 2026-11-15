/*============================================================================================================================================
                                                            SUBSURFACEDIFFUSION.H
============================================================================================================================================*/
// 🧩 Lobe 7: Christensen-Burley (2015) Normalized Diffusion BSSRDF radial profile, thin translucency, & Owen-Sobol volumetric walk

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/MaterialSpace/Scattering/MetallicFresnelTint.h"
#include "Engine/MaterialSpace/SurfaceMaterial.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace::Scattering
{

//------------------------------------------------------------------------------------------------------------------------
//                                      CHRISTENSEN-BURLEY NORMALIZED DIFFUSION PROFILE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Burley (2015) scaling factor s(A) ensuring 2*pi * integral_0^inf r * R_d(r) dr == A identically
[[nodiscard]] inline float EvaluateBurleyScalingFactor(float EquilibriumAlbedo) noexcept
{
    const float A    = std::clamp(EquilibriumAlbedo, 0.0f, 1.0f);
    const float Diff = A - 0.8f;
    return 1.9f - A + 3.5f * Diff * Diff;
}

// 📝 Normalized diffusion radial kernel R_d(r) [1/mm^2] at surface separation rMillimeters
[[nodiscard]] inline Mathematics::LinearRGB EvaluateBurleyRadialDiffusionProfile(
    float                         RadiusMillimeters,
    Mathematics::LinearRGB        SubsurfaceAlbedo,
    Mathematics::Axis3D           MeanFreePathMillimeters) noexcept
{
    const float SafeR = std::max(1.0e-3f, RadiusMillimeters);
    const auto EvalChannel = [&](float Albedo, float MfpMm) noexcept -> float
    {
        if (Albedo <= 1.0e-5f)
        {
            return 0.0f;
        }
        const float ScaleS = EvaluateBurleyScalingFactor(Albedo);
        const float DistD  = std::max(1.0e-3f, MfpMm / ScaleS);
        const float Exp1   = std::exp(-SafeR / DistD);
        const float Exp3   = std::exp(-SafeR / (3.0f * DistD));
        return Albedo * (Exp1 + Exp3) / std::max(1.0e-6f, 8.0f * Constants::PiFloat * DistD * SafeR);
    };

    return {
        EvalChannel(SubsurfaceAlbedo.R, MeanFreePathMillimeters.X),
        EvalChannel(SubsurfaceAlbedo.G, MeanFreePathMillimeters.Y),
        EvalChannel(SubsurfaceAlbedo.B, MeanFreePathMillimeters.Z),
    };
}

// 📝 Directional subsurface local scattering closure (upper-hemisphere Hanrahan-Krueger / Burley multiple-scatter + thin back-scatter)
[[nodiscard]] inline Mathematics::LinearRGB EvaluateSubsurfaceLocalScattering(
    Mathematics::Axis3D                  LocalWo,
    Mathematics::Axis3D                  LocalWi,
    float                                RelativeIOR,
    const SubsurfaceDiffusionParameters& Params,
    float                                SlabThicknessMillimeters = 6.0f) noexcept
{
    if (Params.SubsurfaceWeight <= 1.0e-5f || LocalWo.Z <= 1.0e-6f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float InvPi = 1.0f / Constants::PiFloat;
    const float FoEntry = EvaluateExactDielectricFresnel(LocalWo.Z, RelativeIOR);

    if (LocalWi.Z > 1.0e-6f)
    {
        // Upper-hemisphere exitance: coupled Fresnel entry/exit window + Henyey-Greenstein single-scatter + multiple-scatter diffusion
        const float FiExit     = EvaluateExactDielectricFresnel(LocalWi.Z, RelativeIOR);
        const float FAvgWindow = 1.0f - 0.5f * ((RelativeIOR - 1.0f) / (RelativeIOR + 1.0f));
        const float EntryExit  = ((1.0f - FoEntry) * (1.0f - FiExit)) / std::max(0.25f, FAvgWindow * FAvgWindow);

        // Single-scattering Henyey-Greenstein phase contribution inside medium
        const float CosScatter = -(LocalWo.X * LocalWi.X + LocalWo.Y * LocalWi.Y + LocalWo.Z * LocalWi.Z);
        const float G          = std::clamp(Params.PhaseAnisotropyG, -0.85f, 0.85f);
        const float PhaseDenom = std::pow(1.0f + G * G - 2.0f * G * CosScatter, 1.5f);
        const float PhaseHG    = (1.0f - G * G) / std::max(1.0e-4f, 4.0f * Constants::PiFloat * PhaseDenom);
        const float SingleTerm = PhaseHG / std::max(0.15f, LocalWo.Z + LocalWi.Z);

        // Blend 82% normalized multiple-scatter Lambertian + 18% single-scatter HG, strictly clamped so hemisphere <= SubsurfaceColor!
        const float DirectionalShape = std::min(1.05f * InvPi, 0.82f * InvPi * std::min(1.15f, EntryExit) + 0.18f * SingleTerm);
        return {
            Params.SubsurfaceWeight * Params.SubsurfaceColor.R * DirectionalShape,
            Params.SubsurfaceWeight * Params.SubsurfaceColor.G * DirectionalShape,
            Params.SubsurfaceWeight * Params.SubsurfaceColor.B * DirectionalShape,
        };
    }

    if (LocalWi.Z < -1.0e-6f)
    {
        // Thin-sheet back-lit diffuse translucency attenuated by slab thickness / mean free path
        const float EffectiveSlab = std::max(0.1f, SlabThicknessMillimeters) / std::max(0.25f, std::fabs(LocalWi.Z));
        const Mathematics::LinearRGB Transmitted {
            std::exp(-EffectiveSlab / std::max(0.05f, Params.MeanFreePathMillimeters.X)),
            std::exp(-EffectiveSlab / std::max(0.05f, Params.MeanFreePathMillimeters.Y)),
            std::exp(-EffectiveSlab / std::max(0.05f, Params.MeanFreePathMillimeters.Z)),
        };
        const float Scale = Params.SubsurfaceWeight * (1.0f - FoEntry) * InvPi * 0.5f;
        return {
            Params.SubsurfaceColor.R * Transmitted.R * Scale,
            Params.SubsurfaceColor.G * Transmitted.G * Scale,
            Params.SubsurfaceColor.B * Transmitted.B * Scale,
        };
    }

    return { 0.0f, 0.0f, 0.0f };
}

} // namespace Frontier::MaterialSpace::Scattering
