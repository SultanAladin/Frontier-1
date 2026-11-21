/*============================================================================================================================================
                                                            BOUNCELAYERSUMMARY.CPP
============================================================================================================================================*/
// 🧩 Implementation of pre-integrated 32 B BounceSlabEntry compilation and fast secondary-bounce shading

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/MaterialSurfaceLayer/BounceLayerSummary.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/EnergyCompensation/DirectionalAlbedoIntegral.h"
#include "Engine/MaterialSpace/EnergyCompensation/SheenCosineIntegral.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace
{

namespace
{

[[nodiscard]] Mathematics::LinearRGB EvaluateApproximateKelvinChromaticity(float Kelvin) noexcept
{
    if (Kelvin < 500.0f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const float TempHecto = std::clamp(Kelvin, 800.0f, 15000.0f) * 0.01f;
    float Red = 1.0f, Green = 0.0f, Blue = 0.0f;

    if (TempHecto <= 66.0f)
    {
        Red   = 1.0f;
        Green = std::clamp(0.39008158f * std::log(std::max(1.0f, TempHecto)) - 0.63184144f, 0.0f, 1.0f);
        Blue  = (TempHecto <= 19.0f)
            ? 0.0f
            : std::clamp(0.54320679f * std::log(std::max(1.0f, TempHecto - 10.0f)) - 1.19625409f, 0.0f, 1.0f);
    }
    else
    {
        Red   = std::clamp(1.29293619f * std::pow(TempHecto - 60.0f, -0.13320476f), 0.0f, 1.0f);
        Green = std::clamp(1.12989086f * std::pow(TempHecto - 60.0f, -0.07551485f), 0.0f, 1.0f);
        Blue  = 1.0f;
    }

    // Convert perceptual sRGB curve approximation into linear Rec.709 chromaticity
    const float Intensity = std::min(25.0f, std::pow(Kelvin / 1500.0f, 3.0f));
    return {
        Red * Red * Intensity,
        Green * Green * Intensity,
        Blue * Blue * Intensity,
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           32 B BOUNCE SUMMARY COMPILER
//------------------------------------------------------------------------------------------------------------------------

BounceSlabEntry CompileBounceSlabEntry(const SurfaceMaterial& RawMaterial) noexcept
{
    SurfaceMaterial Mat = RawMaterial;
    SanitizeSurfaceMaterial(Mat);

    const float DielectricF0Scalar =
        ((Mat.Specular.DielectricIOR - 1.0f) / (Mat.Specular.DielectricIOR + 1.0f)) *
        ((Mat.Specular.DielectricIOR - 1.0f) / (Mat.Specular.DielectricIOR + 1.0f)) *
        Mat.Specular.SpecularWeight;

    // Porous wetness darkening on secondary bounces (relative IOR / 1.333 internal trapping)
    const float WetDarkening = 1.0f - 0.48f * Mat.PorosityLight.PorosityFraction * Mat.PorosityLight.SurfaceWetnessFraction;

    const float DielectricMask = (1.0f - Mat.Specular.Metalness) * (1.0f - Mat.Transmission.TransmissionWeight);
    const Mathematics::LinearRGB RawSub = {
        (Mat.Diffuse.BaseColor.R * (1.0f - Mat.Subsurface.SubsurfaceWeight) + Mat.Subsurface.SubsurfaceColor.R * Mat.Subsurface.SubsurfaceWeight) * WetDarkening,
        (Mat.Diffuse.BaseColor.G * (1.0f - Mat.Subsurface.SubsurfaceWeight) + Mat.Subsurface.SubsurfaceColor.G * Mat.Subsurface.SubsurfaceWeight) * WetDarkening,
        (Mat.Diffuse.BaseColor.B * (1.0f - Mat.Subsurface.SubsurfaceWeight) + Mat.Subsurface.SubsurfaceColor.B * Mat.Subsurface.SubsurfaceWeight) * WetDarkening,
    };

    // Fold sheen hemispherical contribution and coat Beer-Lambert tint into secondary-bounce diffuse albedo
    const auto SheenAvg = SampleSheenLinearCosineCoefficients(InspectGlobalSheenLinearCosineSheet(), 0.5f, Mat.Sheen.SheenRoughness);
    const float SheenScale = 1.0f - Mat.Sheen.SheenWeight * SheenAvg.DirectionalSheenR;

    const float CoatAttenR = 1.0f - Mat.Clearcoat.CoatWeight * (1.0f - Mat.Clearcoat.CoatAbsorptionTint.R);
    const float CoatAttenG = 1.0f - Mat.Clearcoat.CoatWeight * (1.0f - Mat.Clearcoat.CoatAbsorptionTint.G);
    const float CoatAttenB = 1.0f - Mat.Clearcoat.CoatWeight * (1.0f - Mat.Clearcoat.CoatAbsorptionTint.B);

    const Mathematics::LinearRGB DiffAlbedo {
        std::clamp((RawSub.R * DielectricMask * Mat.Diffuse.BaseWeight * SheenScale + Mat.Sheen.SheenWeight * SheenAvg.DirectionalSheenR * Mat.Sheen.SheenColor.R) * CoatAttenR, 0.0f, 1.0f),
        std::clamp((RawSub.G * DielectricMask * Mat.Diffuse.BaseWeight * SheenScale + Mat.Sheen.SheenWeight * SheenAvg.DirectionalSheenR * Mat.Sheen.SheenColor.G) * CoatAttenG, 0.0f, 1.0f),
        std::clamp((RawSub.B * DielectricMask * Mat.Diffuse.BaseWeight * SheenScale + Mat.Sheen.SheenWeight * SheenAvg.DirectionalSheenR * Mat.Sheen.SheenColor.B) * CoatAttenB, 0.0f, 1.0f),
    };

    Mathematics::LinearRGB SpecF0 {
        DielectricF0Scalar * (1.0f - Mat.Specular.Metalness) + Mat.Diffuse.BaseColor.R * Mat.Specular.Metalness,
        DielectricF0Scalar * (1.0f - Mat.Specular.Metalness) + Mat.Diffuse.BaseColor.G * Mat.Specular.Metalness,
        DielectricF0Scalar * (1.0f - Mat.Specular.Metalness) + Mat.Diffuse.BaseColor.B * Mat.Specular.Metalness,
    };
    if (Mat.GlintFlakes.FlakeCoverage > 1.0e-3f)
    {
        const float Cov = Mat.GlintFlakes.FlakeCoverage;
        SpecF0.R = SpecF0.R * (1.0f - Cov) + Mat.GlintFlakes.FlakePrimaryTint.R * Cov;
        SpecF0.G = SpecF0.G * (1.0f - Cov) + Mat.GlintFlakes.FlakePrimaryTint.G * Cov;
        SpecF0.B = SpecF0.B * (1.0f - Cov) + Mat.GlintFlakes.FlakePrimaryTint.B * Cov;
    }

    const float EffectiveRoughness = (Mat.Clearcoat.CoatWeight > 0.5f)
        ? (0.35f * Mat.Clearcoat.CoatRoughness + 0.65f * Mat.Specular.Roughness)
        : Mat.Specular.Roughness;

    const auto ThermalRgb = EvaluateApproximateKelvinChromaticity(Mat.PorosityLight.BlackbodyTemperatureKelvin);
    const float ExplicitScale = Mat.PorosityLight.EmissionLuminanceNits * (1.0f / 100.0f);
    const Mathematics::LinearRGB Emissive {
        (ThermalRgb.R + ExplicitScale) * Mat.PorosityLight.EmissionTint.R,
        (ThermalRgb.G + ExplicitScale) * Mat.PorosityLight.EmissionTint.G,
        (ThermalRgb.B + ExplicitScale) * Mat.PorosityLight.EmissionTint.B,
    };

    BounceSlabEntry Summary {};
    Summary.DiffuseAlbedoRgHalf            = PackHalf2x16(DiffAlbedo.R, DiffAlbedo.G);
    Summary.DiffuseAlbedoBAndRoughnessHalf = PackHalf2x16(DiffAlbedo.B, EffectiveRoughness);
    Summary.SpecularF0RgHalf               = PackHalf2x16(SpecF0.R, SpecF0.G);
    Summary.SpecularF0BAndMetalnessHalf    = PackHalf2x16(SpecF0.B, Mat.Specular.Metalness);
    Summary.EmissiveExitanceRgHalf         = PackHalf2x16(Emissive.R, Emissive.G);
    Summary.EmissiveExitanceBAndTransHalf  = PackHalf2x16(Emissive.B, Mat.Transmission.TransmissionWeight);

    const float MeanDiff = (DiffAlbedo.R + DiffAlbedo.G + DiffAlbedo.B) * (1.0f / 3.0f);
    const float MeanSpec = (SpecF0.R + SpecF0.G + SpecF0.B) * (1.0f / 3.0f);
    Summary.MeanHemisphericalReflectance = std::clamp(MeanDiff + MeanSpec * (1.0f - MeanDiff), 0.0f, 1.0f);
    Summary.SummaryFlags =
        ((Mat.Transmission.TransmissionWeight > 1.0e-3f) ? 1u : 0u) |
        (((Emissive.R + Emissive.G + Emissive.B) > 1.0e-4f) ? 2u : 0u);

    return Summary;
}

Mathematics::LinearRGB EvaluateBounceSlabRadiance(
    const BounceSlabEntry& Summary,
    Mathematics::Axis3D    SurfaceNormal,
    Mathematics::Axis3D    OutgoingDirection,
    Mathematics::Axis3D    IncidentLightDirection,
    Mathematics::LinearRGB IncidentRadiance) noexcept
{
    Mathematics::LinearRGB Diff {};
    Mathematics::LinearRGB SpecF0 {};
    Mathematics::LinearRGB Emissive {};
    float Roughness = 0.3f, Metalness = 0.0f, TransWeight = 0.0f;

    UnpackHalf2x16(Summary.DiffuseAlbedoRgHalf, Diff.R, Diff.G);
    UnpackHalf2x16(Summary.DiffuseAlbedoBAndRoughnessHalf, Diff.B, Roughness);
    UnpackHalf2x16(Summary.SpecularF0RgHalf, SpecF0.R, SpecF0.G);
    UnpackHalf2x16(Summary.SpecularF0BAndMetalnessHalf, SpecF0.B, Metalness);
    UnpackHalf2x16(Summary.EmissiveExitanceRgHalf, Emissive.R, Emissive.G);
    UnpackHalf2x16(Summary.EmissiveExitanceBAndTransHalf, Emissive.B, TransWeight);
    (void)Metalness;
    (void)TransWeight;

    const float NdotL = std::max(0.0f, Mathematics::EvaluateDot(SurfaceNormal, IncidentLightDirection));
    if (NdotL <= 1.0e-6f)
    {
        return Emissive;
    }

    const Mathematics::Axis3D HalfVec = Mathematics::NormalizeAxis(OutgoingDirection + IncidentLightDirection, SurfaceNormal);
    const float NdotH   = std::max(0.0f, Mathematics::EvaluateDot(SurfaceNormal, HalfVec));
    const float VdotH   = std::clamp(Mathematics::EvaluateDot(OutgoingDirection, HalfVec), 0.0f, 1.0f);
    const float AlphaSq = std::max(1.0e-3f, Roughness * Roughness * Roughness * Roughness);

    const float Denom    = NdotH * NdotH * (AlphaSq - 1.0f) + 1.0f;
    const float SpecLobe = AlphaSq / std::max(1.0e-5f, Constants::PiFloat * Denom * Denom * 4.0f);
    const float Schlick  = std::pow(1.0f - VdotH, 5.0f);
    const float InvPi    = 1.0f / Constants::PiFloat;

    const Mathematics::LinearRGB Fresnel {
        SpecF0.R + (1.0f - SpecF0.R) * Schlick,
        SpecF0.G + (1.0f - SpecF0.G) * Schlick,
        SpecF0.B + (1.0f - SpecF0.B) * Schlick,
    };

    return {
        Emissive.R + (Diff.R * InvPi * (1.0f - SpecF0.R) + Fresnel.R * SpecLobe) * IncidentRadiance.R * NdotL,
        Emissive.G + (Diff.G * InvPi * (1.0f - SpecF0.G) + Fresnel.G * SpecLobe) * IncidentRadiance.G * NdotL,
        Emissive.B + (Diff.B * InvPi * (1.0f - SpecF0.B) + Fresnel.B * SpecLobe) * IncidentRadiance.B * NdotL,
    };
}

} // namespace Frontier::MaterialSpace
