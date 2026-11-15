/*============================================================================================================================================
                                                          OPENPBRSURFACESCATTERING.H
============================================================================================================================================*/
// 🧩 Master 10-Lobe OpenPBR 1.1 + Constant-Time Glinty BSDF evaluation, VNDF/Cosine mixture PDF, & White-Furnace hemisphere integrator

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/EnergyCompensation/DirectionalAlbedoIntegral.h"
#include "Engine/MaterialSpace/EnergyCompensation/SheenCosineIntegral.h"
#include "Engine/MaterialSpace/Scattering/ClearcoatAbsorption.h"
#include "Engine/MaterialSpace/Scattering/DielectricTransmission.h"
#include "Engine/MaterialSpace/Scattering/EnergyPreservingOrenNayar.h"
#include "Engine/MaterialSpace/Scattering/FiberScattering.h"
#include "Engine/MaterialSpace/Scattering/GlintMicroflakeScattering.h"
#include "Engine/MaterialSpace/Scattering/LinearlyTransformedSheen.h"
#include "Engine/MaterialSpace/Scattering/MetallicFresnelTint.h"
#include "Engine/MaterialSpace/Scattering/PorosityScattering.h"
#include "Engine/MaterialSpace/Scattering/BlackbodyEmission.h"
#include "Engine/MaterialSpace/Scattering/SubsurfaceDiffusion.h"
#include "Engine/MaterialSpace/Scattering/ThinFilmInterference.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace::Scattering
{

//------------------------------------------------------------------------------------------------------------------------
//                                        SHADING CONTEXT & BSDF SAMPLE STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceShadingFrame
{
    Mathematics::Axis3D TangentX   { 1.0f, 0.0f, 0.0f }; // [-] - Orthonormal surface tangent (+X in local BSDF frame)
    Mathematics::Axis3D BitangentY { 0.0f, 1.0f, 0.0f }; // [-] - Orthonormal surface bitangent (+Y in local BSDF frame)
    Mathematics::Axis3D NormalZ    { 0.0f, 0.0f, 1.0f }; // [-] - Outward shading normal (+Z in local BSDF frame)

    [[nodiscard]] Mathematics::Axis3D WorldToLocal(Mathematics::Axis3D WorldVec) const noexcept
    {
        return {
            Mathematics::EvaluateDot(WorldVec, TangentX),
            Mathematics::EvaluateDot(WorldVec, BitangentY),
            Mathematics::EvaluateDot(WorldVec, NormalZ),
        };
    }

    [[nodiscard]] Mathematics::Axis3D LocalToWorld(Mathematics::Axis3D LocalVec) const noexcept
    {
        return TangentX * LocalVec.X + BitangentY * LocalVec.Y + NormalZ * LocalVec.Z;
    }
};

struct BsdfDirectionSample
{
    Mathematics::Axis3D    IncidentDirectionWorld { 0.0f, 0.0f, 1.0f }; // [-]    - Sampled incident light direction
    Mathematics::LinearRGB ThroughputWeight       { 0.0f, 0.0f, 0.0f }; // [-]    - f(wo, wi) * |cos(theta_i)| / pdf(wi)
    float                  PdfSolidAngle          = 0.0f;               // [1/sr] - Mixture solid-angle probability density
    bool                   SampledTransmission    = false;              // [-]    - True if refracted into lower hemisphere
};

struct WhiteFurnaceAuditReport
{
    Mathematics::LinearRGB ReflectedDirectionalAlbedo   { 0.0f, 0.0f, 0.0f }; // [-] - Upper-hemisphere integral f*cos dwi
    Mathematics::LinearRGB TransmittedDirectionalAlbedo { 0.0f, 0.0f, 0.0f }; // [-] - Lower-hemisphere integral f*|cos| dwi
    float                  MaxChannelTotalAlbedo        = 0.0f;               // [-] - max_{R,G,B}(Reflected + Transmitted) <= 1.0
};

//------------------------------------------------------------------------------------------------------------------------
//                                        MASTER 10-LOBE OPENPBR 1.1 BSDF EVALUATOR
//------------------------------------------------------------------------------------------------------------------------

// 📝 Evaluate exact reciprocal 10-lobe BSDF f(wo, wi) [1/sr] in local Z-up frame (wo.Z > 0; wi.Z > 0 reflection, wi.Z < 0 transmission)
[[nodiscard]] inline Mathematics::LinearRGB EvaluateOpenPbrLocalBsdf(
    const SurfaceMaterial&        Mat,
    uint32_t                      ActiveLobeMask,
    Mathematics::Axis3D           LocalWo,
    Mathematics::Axis3D           LocalWi,
    TextureFootprintJacobian      Footprint   = {},
    const DirectionalAlbedoSheet& AlbedoSheet = InspectGlobalDirectionalAlbedoSheet(),
    const SheenLinearCosineSheet& SheenSheet  = InspectGlobalSheenLinearCosineSheet()) noexcept
{
    if (LocalWo.Z <= 1.0e-6f || std::fabs(LocalWi.Z) <= 1.0e-6f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    // Rotate tangent plane by Specular.RotationRadians if authored
    if (std::fabs(Mat.Specular.RotationRadians) > 1.0e-5f)
    {
        const float CosR = std::cos(Mat.Specular.RotationRadians);
        const float SinR = std::sin(Mat.Specular.RotationRadians);
        LocalWo = { LocalWo.X * CosR + LocalWo.Y * SinR, -LocalWo.X * SinR + LocalWo.Y * CosR, LocalWo.Z };
        LocalWi = { LocalWi.X * CosR + LocalWi.Y * SinR, -LocalWi.X * SinR + LocalWi.Y * CosR, LocalWi.Z };
    }

    //====================================================================================================================
    // LOWER HEMISPHERE (LocalWi.Z < 0): Lobe 6 Dielectric Transmission + Lobe 7 Thin Subsurface Translucency
    //====================================================================================================================
    if (LocalWi.Z < 0.0f)
    {
        Mathematics::LinearRGB LowerTotal { 0.0f, 0.0f, 0.0f };

        if ((ActiveLobeMask & LobeDielectricTransmission) != 0u)
        {
            const float EffectiveRoughness = ((ActiveLobeMask & LobeClearcoat) != 0u)
                ? std::pow(std::min(1.0f, std::pow(Mat.Specular.Roughness, 4.0f) + 2.0f * std::pow(Mat.Clearcoat.CoatRoughness, 4.0f)), 0.25f)
                : Mat.Specular.Roughness;
            const auto Btdf = EvaluateDielectricTransmissionBtdf(
                LocalWo, LocalWi, EffectiveRoughness, Mat.Specular.Anisotropy, Mat.Specular.DielectricIOR, Mat.Transmission);
            const float NonMetalScale = 1.0f - Mat.Specular.Metalness;
            LowerTotal.R += Btdf.R * NonMetalScale;
            LowerTotal.G += Btdf.G * NonMetalScale;
            LowerTotal.B += Btdf.B * NonMetalScale;
        }

        if ((ActiveLobeMask & LobeSubsurfaceDiffusion) != 0u)
        {
            const float SubMask = (1.0f - Mat.Specular.Metalness) * (1.0f - Mat.Transmission.TransmissionWeight);
            const auto TransSss = EvaluateSubsurfaceLocalScattering(LocalWo, LocalWi, Mat.Specular.DielectricIOR, Mat.Subsurface);
            LowerTotal.R += TransSss.R * SubMask;
            LowerTotal.G += TransSss.G * SubMask;
            LowerTotal.B += TransSss.B * SubMask;
        }

        return LowerTotal;
    }

    //====================================================================================================================
    // UPPER HEMISPHERE (LocalWi.Z > 0): Ordered Top-Down Optical Stack (L10 Wetness -> L3 Coat -> L8 Sheen -> L9/L5/L4/L2/L7/L1)
    //====================================================================================================================

    // Lobe 10a: Porosity & adsorbed liquid water film
    PorosityWetnessAdjustment WetAdj {};
    WetAdj.DarkenedDiffuseAlbedo = Mat.Diffuse.BaseColor;
    WetAdj.ModifiedRelativeIOR   = Mat.Specular.DielectricIOR;
    if ((ActiveLobeMask & LobePorosityWetness) != 0u)
    {
        WetAdj = EvaluatePorosityWetness(
            LocalWo,
            LocalWi,
            Mat.Diffuse.BaseColor,
            Mat.Specular.DielectricIOR,
            Mat.PorosityLight.PorosityFraction,
            Mat.PorosityLight.SurfaceWetnessFraction);
    }

    // Lobe 3: Clearcoat reflection, underlying roughness widening, & Beer-Lambert medium transmittance
    ClearcoatInteractionResult CoatRes {};
    CoatRes.WidenedBaseRoughness = Mat.Specular.Roughness;
    CoatRes.ModifiedRelativeIOR  = WetAdj.ModifiedRelativeIOR;
    if ((ActiveLobeMask & LobeClearcoat) != 0u)
    {
        CoatRes = EvaluateClearcoatLayer(
            LocalWo,
            LocalWi,
            Mat.Clearcoat.CoatWeight,
            Mat.Clearcoat.CoatRoughness,
            Mat.Clearcoat.CoatAnisotropy,
            Mat.Clearcoat.CoatIOR,
            Mat.Clearcoat.CoatAbsorptionTint,
            Mat.Clearcoat.CoatThicknessMillimeter,
            Mat.Specular.Roughness,
            WetAdj.ModifiedRelativeIOR);
    }

    // Lobe 8: Zeltner 2022 LTC multiple-scattering cloth/velvet sheen
    SheenInteractionResult SheenRes {};
    if ((ActiveLobeMask & LobeClothSheenLTC) != 0u)
    {
        SheenRes = EvaluateClothSheenLayer(LocalWo, LocalWi, Mat.Sheen, SheenSheet);
    }

    // Lobe 9: Chiang/Marschner dual-scattering hair & technical fiber BCSDF
    Mathematics::LinearRGB FiberBrdf { 0.0f, 0.0f, 0.0f };
    const float FiberBlend = ((ActiveLobeMask & LobeFiberBCSDF) != 0u) ? Mat.Fiber.FiberWeight : 0.0f;
    if (FiberBlend > 1.0e-5f)
    {
        FiberBrdf = EvaluateFiberStrandScattering(
            LocalWo, LocalWi, CoatRes.ModifiedRelativeIOR, WetAdj.DarkenedDiffuseAlbedo, Mat.Fiber);
    }

    const float SurfaceBlend = 1.0f - FiberBlend;
    Mathematics::LinearRGB CoreBrdf { 0.0f, 0.0f, 0.0f };

    if (SurfaceBlend > 1.0e-5f)
    {
        const float FlakeCov = ((ActiveLobeMask & LobeGlintMicroflakes) != 0u) ? Mat.GlintFlakes.FlakeCoverage : 0.0f;
        const float NonFlakeScale = 1.0f - FlakeCov;

        // Lobe 5: Kemppinen 2025 Constant-Time 4D Multiscale Glinty Microflakes
        Mathematics::LinearRGB GlintBrdf { 0.0f, 0.0f, 0.0f };
        if (FlakeCov > 1.0e-5f)
        {
            GlintBrdf = EvaluateGlintMicroflakeBrdf(LocalWo, LocalWi, Mat.GlintFlakes, Footprint);
        }

        // Lobe 2 + Lobe 4: Anisotropic VNDF GGX + F0/F82 Conductor + Optional Belcour-Barla Nanometer Thin-Film Airy Interference
        Mathematics::LinearRGB SpecBrdf { 0.0f, 0.0f, 0.0f };
        if ((ActiveLobeMask & LobeSpecularMetalGGX) != 0u)
        {
            Mathematics::LinearRGB CustomIridescentFresnel {};
            Mathematics::LinearRGB* FresnelOverride = nullptr;

            if ((ActiveLobeMask & LobeThinFilmAiry) != 0u)
            {
                const Mathematics::Axis3D HalfVec = Mathematics::NormalizeAxis(LocalWo + LocalWi, { 0.0f, 0.0f, 1.0f });
                const float VoDotH  = std::clamp(Mathematics::EvaluateDot(LocalWo, HalfVec), 0.0f, 1.0f);
                const float DielF   = EvaluateExactDielectricFresnel(VoDotH, CoatRes.ModifiedRelativeIOR) * Mat.Specular.SpecularWeight;
                const auto  CondF   = EvaluateConductorEdgeTintFresnel(VoDotH, WetAdj.DarkenedDiffuseAlbedo, Mat.Specular.EdgeTintF82);
                const Mathematics::LinearRGB BaseFresnel {
                    DielF * (1.0f - Mat.Specular.Metalness) + CondF.R * Mat.Specular.Metalness,
                    DielF * (1.0f - Mat.Specular.Metalness) + CondF.G * Mat.Specular.Metalness,
                    DielF * (1.0f - Mat.Specular.Metalness) + CondF.B * Mat.Specular.Metalness,
                };
                const float ExteriorIor = (Mat.Clearcoat.CoatWeight > 0.5f) ? Mat.Clearcoat.CoatIOR : 1.0f;
                CustomIridescentFresnel = EvaluateSpectralThinFilmFresnel(
                    VoDotH,
                    Mat.ThinFilm.ThinFilmThicknessNanometers,
                    Mat.ThinFilm.ThinFilmIOR,
                    ExteriorIor,
                    BaseFresnel,
                    Mat.ThinFilm.ThinFilmWeight,
                    false);
                FresnelOverride = &CustomIridescentFresnel;
            }

            SpecBrdf = EvaluateConductorDielectricSpecular(
                LocalWo,
                LocalWi,
                WetAdj.DarkenedDiffuseAlbedo,
                Mat.Specular.EdgeTintF82,
                Mat.Specular.Metalness,
                Mat.Specular.SpecularWeight,
                CoatRes.WidenedBaseRoughness,
                Mat.Specular.Anisotropy,
                CoatRes.ModifiedRelativeIOR,
                AlbedoSheet,
                FresnelOverride);
        }

        // Reciprocal dielectric specular energy window for underlying diffuse & subsurface lobes
        const float DielF0Base =
            ((CoatRes.ModifiedRelativeIOR - 1.0f) / (CoatRes.ModifiedRelativeIOR + 1.0f)) *
            ((CoatRes.ModifiedRelativeIOR - 1.0f) / (CoatRes.ModifiedRelativeIOR + 1.0f));
        const float SpecWeight = Mat.Specular.SpecularWeight;
        const auto  AlbO       = SampleDirectionalAlbedo(AlbedoSheet, LocalWo.Z, CoatRes.WidenedBaseRoughness);
        const auto  AlbI       = SampleDirectionalAlbedo(AlbedoSheet, LocalWi.Z, CoatRes.WidenedBaseRoughness);
        const float SpecReflO  = SpecWeight * (DielF0Base + (1.0f - DielF0Base) * std::pow(1.0f - LocalWo.Z, 5.0f)) * AlbO.DirectionalAlbedoE;
        const float SpecReflI  = SpecWeight * (DielF0Base + (1.0f - DielF0Base) * std::pow(1.0f - LocalWi.Z, 5.0f)) * AlbI.DirectionalAlbedoE;
        const float DielectricSubScale =
            (1.0f - Mat.Specular.Metalness) *
            (1.0f - Mat.Transmission.TransmissionWeight) *
            std::clamp(1.0f - std::max(SpecReflO, SpecReflI), 0.0f, 1.0f);

        // Lobe 1: Portsmouth 2025 Energy-Preserving Oren-Nayar Diffuse
        Mathematics::LinearRGB DiffBrdf { 0.0f, 0.0f, 0.0f };
        if ((ActiveLobeMask & LobeDiffuseEON) != 0u && DielectricSubScale > 1.0e-5f)
        {
            const float DiffShare = DielectricSubScale * (1.0f - Mat.Subsurface.SubsurfaceWeight) * Mat.Diffuse.BaseWeight;
            const auto RawEon = EvaluateEnergyPreservingOrenNayar(
                LocalWo, LocalWi, WetAdj.DarkenedDiffuseAlbedo, Mat.Diffuse.DiffuseRoughness);
            DiffBrdf = { RawEon.R * DiffShare, RawEon.G * DiffShare, RawEon.B * DiffShare };
        }

        // Lobe 7: Christensen-Burley Subsurface Scattering
        Mathematics::LinearRGB SssBrdf { 0.0f, 0.0f, 0.0f };
        if ((ActiveLobeMask & LobeSubsurfaceDiffusion) != 0u && DielectricSubScale > 1.0e-5f)
        {
            const auto RawSss = EvaluateSubsurfaceLocalScattering(
                LocalWo, LocalWi, CoatRes.ModifiedRelativeIOR, Mat.Subsurface);
            SssBrdf = { RawSss.R * DielectricSubScale, RawSss.G * DielectricSubScale, RawSss.B * DielectricSubScale };
        }

        CoreBrdf = {
            GlintBrdf.R + NonFlakeScale * (SpecBrdf.R + DiffBrdf.R + SssBrdf.R),
            GlintBrdf.G + NonFlakeScale * (SpecBrdf.G + DiffBrdf.G + SssBrdf.G),
            GlintBrdf.B + NonFlakeScale * (SpecBrdf.B + DiffBrdf.B + SssBrdf.B),
        };
    }

    // Combine Sheen + Fiber/Surface beneath Clearcoat & Water Film windows
    const Mathematics::LinearRGB UnderCoat {
        SheenRes.SheenBrdf.R + SheenRes.SubstrateEnergyScale * (FiberBrdf.R + SurfaceBlend * CoreBrdf.R),
        SheenRes.SheenBrdf.G + SheenRes.SubstrateEnergyScale * (FiberBrdf.G + SurfaceBlend * CoreBrdf.G),
        SheenRes.SheenBrdf.B + SheenRes.SubstrateEnergyScale * (FiberBrdf.B + SurfaceBlend * CoreBrdf.B),
    };

    const Mathematics::LinearRGB UnderWater {
        CoatRes.CoatSpecularBrdf.R + CoatRes.SubstrateTransmittance.R * UnderCoat.R,
        CoatRes.CoatSpecularBrdf.G + CoatRes.SubstrateTransmittance.G * UnderCoat.G,
        CoatRes.CoatSpecularBrdf.B + CoatRes.SubstrateTransmittance.B * UnderCoat.B,
    };

    return {
        WetAdj.WaterFilmSpecularBrdf + WetAdj.SubstrateWindowScale * UnderWater.R,
        WetAdj.WaterFilmSpecularBrdf + WetAdj.SubstrateWindowScale * UnderWater.G,
        WetAdj.WaterFilmSpecularBrdf + WetAdj.SubstrateWindowScale * UnderWater.B,
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                        MIXTURE PDF & IMPORTANCE SAMPLING
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline float EvaluateOpenPbrMixturePdf(
    const SurfaceMaterial& Mat,
    Mathematics::Axis3D    LocalWo,
    Mathematics::Axis3D    LocalWi) noexcept
{
    if (LocalWo.Z <= 1.0e-6f || std::fabs(LocalWi.Z) <= 1.0e-6f)
    {
        return 0.0f;
    }

    const float TransProb = (1.0f - Mat.Specular.Metalness) * Mat.Transmission.TransmissionWeight * 0.85f;
    if (LocalWi.Z < 0.0f)
    {
        if (TransProb <= 1.0e-5f)
        {
            return 0.0f;
        }
        return TransProb * (std::fabs(LocalWi.Z) / Constants::PiFloat);
    }

    const float UpperProb = 1.0f - TransProb;
    const float CoatProb  = 0.25f * Mat.Clearcoat.CoatWeight;
    const float SpecProb  = (1.0f - CoatProb) * std::clamp(0.25f + 0.65f * Mat.Specular.Metalness + 0.35f * Mat.GlintFlakes.FlakeCoverage, 0.15f, 0.90f);
    const float DiffProb  = std::max(0.0f, 1.0f - CoatProb - SpecProb);

    const Mathematics::Axis3D LocalHalf = Mathematics::NormalizeAxis(LocalWo + LocalWi, { 0.0f, 0.0f, 1.0f });
    const auto SpecAlpha = ComputeAnisotropicAlpha(Mat.Specular.Roughness, Mat.Specular.Anisotropy);
    const auto CoatAlpha = ComputeAnisotropicAlpha(Mat.Clearcoat.CoatRoughness, Mat.Clearcoat.CoatAnisotropy);

    const float PdfDiff = EvaluateCosineHemispherePdf(LocalWi);
    const float PdfSpec = EvaluateSphericalCapVndfReflectionPdf(LocalWo, LocalHalf, SpecAlpha);
    const float PdfCoat = EvaluateSphericalCapVndfReflectionPdf(LocalWo, LocalHalf, CoatAlpha);

    return UpperProb * (DiffProb * PdfDiff + SpecProb * PdfSpec + CoatProb * PdfCoat);
}

[[nodiscard]] inline BsdfDirectionSample SampleOpenPbrScattering(
    const SurfaceMaterial&   Mat,
    uint32_t                 ActiveLobeMask,
    const SurfaceShadingFrame& Frame,
    Mathematics::Axis3D      OutgoingWorld,
    float                    LobeSelectorU0,
    float                    SampleU1,
    float                    SampleU2,
    TextureFootprintJacobian Footprint = {}) noexcept
{
    BsdfDirectionSample Result {};
    const Mathematics::Axis3D LocalWo = Frame.WorldToLocal(OutgoingWorld);
    if (LocalWo.Z <= 1.0e-6f)
    {
        return Result;
    }

    const float TransProb = (1.0f - Mat.Specular.Metalness) * Mat.Transmission.TransmissionWeight * 0.85f;
    Mathematics::Axis3D LocalWi {};

    if (LobeSelectorU0 < TransProb)
    {
        const Mathematics::Axis3D CosineLower = SampleCosineHemisphereDirection(SampleU1, SampleU2);
        LocalWi = { CosineLower.X, CosineLower.Y, -CosineLower.Z };
        Result.SampledTransmission = true;
    }
    else
    {
        const float RemappedU = (LobeSelectorU0 - TransProb) / std::max(1.0e-5f, 1.0f - TransProb);
        const float CoatProb  = 0.25f * Mat.Clearcoat.CoatWeight;
        const float SpecProb  = (1.0f - CoatProb) * std::clamp(0.25f + 0.65f * Mat.Specular.Metalness + 0.35f * Mat.GlintFlakes.FlakeCoverage, 0.15f, 0.90f);

        if (RemappedU < CoatProb)
        {
            const auto CoatAlpha = ComputeAnisotropicAlpha(Mat.Clearcoat.CoatRoughness, Mat.Clearcoat.CoatAnisotropy);
            const auto HalfVec   = SampleAnisotropicSphericalCapVndf(LocalWo, CoatAlpha, SampleU1, SampleU2);
            const float VoDotH   = Mathematics::EvaluateDot(LocalWo, HalfVec);
            LocalWi              = HalfVec * (2.0f * VoDotH) - LocalWo;
        }
        else if (RemappedU < CoatProb + SpecProb)
        {
            const auto SpecAlpha = ComputeAnisotropicAlpha(Mat.Specular.Roughness, Mat.Specular.Anisotropy);
            const auto HalfVec   = SampleAnisotropicSphericalCapVndf(LocalWo, SpecAlpha, SampleU1, SampleU2);
            const float VoDotH   = Mathematics::EvaluateDot(LocalWo, HalfVec);
            LocalWi              = HalfVec * (2.0f * VoDotH) - LocalWo;
        }
        else
        {
            LocalWi = SampleCosineHemisphereDirection(SampleU1, SampleU2);
        }
    }

    if (std::fabs(LocalWi.Z) <= 1.0e-5f)
    {
        return Result;
    }

    const float Pdf = EvaluateOpenPbrMixturePdf(Mat, LocalWo, LocalWi);
    if (Pdf <= 1.0e-7f)
    {
        return Result;
    }

    const auto BsdfVal = EvaluateOpenPbrLocalBsdf(Mat, ActiveLobeMask, LocalWo, LocalWi, Footprint);
    const float Factor = std::fabs(LocalWi.Z) / Pdf;

    Result.IncidentDirectionWorld = Frame.LocalToWorld(LocalWi);
    Result.PdfSolidAngle          = Pdf;
    Result.ThroughputWeight       = { BsdfVal.R * Factor, BsdfVal.G * Factor, BsdfVal.B * Factor };
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      DETERMINISTIC WHITE-FURNACE HEMISPHERE AUDIT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Integrate upper & lower hemispheres via Multiple Importance Sampling (VNDF Reflection + Snell Refraction + Cosine)
[[nodiscard]] inline WhiteFurnaceAuditReport IntegrateDirectionalHemisphereAlbedo(
    const SurfaceMaterial& Mat,
    float                  CosThetaOut,
    uint32_t               ZenithRings  = 32u,
    uint32_t               AzimuthSlices = 64u) noexcept
{
    WhiteFurnaceAuditReport Report {};
    const float MuO  = std::clamp(CosThetaOut, 0.02f, 1.0f);
    const float SinO = std::sqrt(std::max(0.0f, 1.0f - MuO * MuO));
    const Mathematics::Axis3D LocalWo { SinO, 0.0f, MuO };
    const uint32_t ActiveMask = DetermineActiveLobeMask(Mat);

    // Far-field footprint for macroscopic energy integration
    TextureFootprintJacobian MacroFootprint {};
    MacroFootprint.DerivativeDuDx = { 0.05f, 0.0f };
    MacroFootprint.DerivativeDuDy = { 0.0f, 0.05f };

    const uint32_t SampleCount = std::max(256u, ZenithRings * AzimuthSlices);
    const auto     SpecAlpha   = ComputeAnisotropicAlpha(Mat.Specular.Roughness, Mat.Specular.Anisotropy);
    const float    EtaT        = std::max(1.01f, Mat.Specular.DielectricIOR);
    const float    InvEtaT     = 1.0f / EtaT;

    const auto RadicalInverseVdC = [](uint32_t Bits) noexcept -> float
    {
        Bits = (Bits << 16u) | (Bits >> 16u);
        Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
        Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
        Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
        Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
        return static_cast<float>(Bits) * 2.3283064365386963e-10f;
    };

    const auto EvalUpperMixturePdf = [&](Mathematics::Axis3D Wi) noexcept -> float
    {
        if (Wi.Z <= 1.0e-6f) return 0.0f;
        const auto HalfVec = Mathematics::NormalizeAxis(LocalWo + Wi, { 0.0f, 0.0f, 1.0f });
        const float PdfCos  = EvaluateCosineHemispherePdf(Wi);
        const float PdfVndf = EvaluateSphericalCapVndfReflectionPdf(LocalWo, HalfVec, SpecAlpha);
        return 0.5f * (PdfCos + PdfVndf);
    };

    const auto EvalLowerMixturePdf = [&](Mathematics::Axis3D Wi) noexcept -> float
    {
        if (Wi.Z >= -1.0e-6f) return 0.0f;
        const float PdfCos = std::fabs(Wi.Z) / Constants::PiFloat;
        Mathematics::Axis3D HalfT = Mathematics::NormalizeAxis(
            { -(LocalWo.X + EtaT * Wi.X), -(LocalWo.Y + EtaT * Wi.Y), -(LocalWo.Z + EtaT * Wi.Z) },
            { 0.0f, 0.0f, 1.0f });
        if (HalfT.Z < 0.0f) HalfT = HalfT * -1.0f;
        const float WoDotH = Mathematics::EvaluateDot(LocalWo, HalfT);
        const float WiDotH = Mathematics::EvaluateDot(Wi, HalfT);
        float PdfRefr = 0.0f;
        if (WoDotH > 1.0e-6f && WiDotH < -1.0e-6f)
        {
            const float SqrtD = WoDotH + EtaT * WiDotH;
            if (std::fabs(SqrtD) > 1.0e-5f)
            {
                const float NdfD  = EvaluateAnisotropicGgxNdf(HalfT, SpecAlpha);
                const float G1O   = 1.0f / (1.0f + EvaluateAnisotropicSmithLambda(LocalWo, SpecAlpha));
                const float VndfH = (G1O * NdfD * WoDotH) / std::max(1.0e-6f, LocalWo.Z);
                const float Jac   = (EtaT * EtaT * std::fabs(WiDotH)) / (SqrtD * SqrtD);
                PdfRefr = VndfH * Jac;
            }
        }
        return 0.5f * (PdfCos + PdfRefr);
    };

    double SumReflR = 0.0, SumReflG = 0.0, SumReflB = 0.0;
    double SumTranR = 0.0, SumTranG = 0.0, SumTranB = 0.0;
    const double InvN = 1.0 / static_cast<double>(SampleCount);

    for (uint32_t SIdx = 0u; SIdx < SampleCount; ++SIdx)
    {
        const float U1 = (static_cast<float>(SIdx) + 0.5f) / static_cast<float>(SampleCount);
        const float U2 = RadicalInverseVdC(SIdx);

        // Upper hemisphere MIS: 50% Cosine + 50% VNDF Reflection (Balance Heuristic)
        {
            const Mathematics::Axis3D WiCos = SampleCosineHemisphereDirection(U1, U2);
            const float PdfMixCos = EvalUpperMixturePdf(WiCos);
            if (PdfMixCos > 1.0e-7f)
            {
                const auto F = EvaluateOpenPbrLocalBsdf(Mat, ActiveMask, LocalWo, WiCos, MacroFootprint);
                const double W = static_cast<double>(0.5f * WiCos.Z / PdfMixCos) * InvN;
                SumReflR += static_cast<double>(F.R) * W;
                SumReflG += static_cast<double>(F.G) * W;
                SumReflB += static_cast<double>(F.B) * W;
            }

            const auto HalfVec = SampleAnisotropicSphericalCapVndf(LocalWo, SpecAlpha, U1, U2);
            const float VoDotH = Mathematics::EvaluateDot(LocalWo, HalfVec);
            const Mathematics::Axis3D WiVndf = HalfVec * (2.0f * VoDotH) - LocalWo;
            if (WiVndf.Z > 1.0e-6f)
            {
                const float PdfMixVndf = EvalUpperMixturePdf(WiVndf);
                if (PdfMixVndf > 1.0e-7f)
                {
                    const auto F = EvaluateOpenPbrLocalBsdf(Mat, ActiveMask, LocalWo, WiVndf, MacroFootprint);
                    const double W = static_cast<double>(0.5f * WiVndf.Z / PdfMixVndf) * InvN;
                    SumReflR += static_cast<double>(F.R) * W;
                    SumReflG += static_cast<double>(F.G) * W;
                    SumReflB += static_cast<double>(F.B) * W;
                }
            }
        }

        // Lower hemisphere MIS: 50% Cosine + 50% VNDF Snell Refraction
        if ((ActiveMask & (LobeDielectricTransmission | LobeSubsurfaceDiffusion)) != 0u)
        {
            const Mathematics::Axis3D CosDir = SampleCosineHemisphereDirection(U1, U2);
            const Mathematics::Axis3D WiLowerCos { CosDir.X, CosDir.Y, -CosDir.Z };
            const float PdfMixLowCos = EvalLowerMixturePdf(WiLowerCos);
            if (PdfMixLowCos > 1.0e-7f)
            {
                const auto F = EvaluateOpenPbrLocalBsdf(Mat, ActiveMask, LocalWo, WiLowerCos, MacroFootprint);
                const double W = static_cast<double>(0.5f * std::fabs(WiLowerCos.Z) / PdfMixLowCos) * InvN;
                SumTranR += static_cast<double>(F.R) * W;
                SumTranG += static_cast<double>(F.G) * W;
                SumTranB += static_cast<double>(F.B) * W;
            }

            const auto HalfT   = SampleAnisotropicSphericalCapVndf(LocalWo, SpecAlpha, U1, U2);
            const float WoDotH = Mathematics::EvaluateDot(LocalWo, HalfT);
            const float Sin2T  = InvEtaT * InvEtaT * std::max(0.0f, 1.0f - WoDotH * WoDotH);
            if (WoDotH > 1.0e-6f && Sin2T < 1.0f)
            {
                const float CosT = std::sqrt(1.0f - Sin2T);
                const Mathematics::Axis3D WiRefr = HalfT * (InvEtaT * WoDotH - CosT) - LocalWo * InvEtaT;
                if (WiRefr.Z < -1.0e-6f)
                {
                    const float PdfMixRefr = EvalLowerMixturePdf(WiRefr);
                    if (PdfMixRefr > 1.0e-7f)
                    {
                        const auto F = EvaluateOpenPbrLocalBsdf(Mat, ActiveMask, LocalWo, WiRefr, MacroFootprint);
                        const double W = static_cast<double>(0.5f * std::fabs(WiRefr.Z) / PdfMixRefr) * InvN;
                        SumTranR += static_cast<double>(F.R) * W;
                        SumTranG += static_cast<double>(F.G) * W;
                        SumTranB += static_cast<double>(F.B) * W;
                    }
                }
            }
        }
    }

    Report.ReflectedDirectionalAlbedo = {
        static_cast<float>(SumReflR),
        static_cast<float>(SumReflG),
        static_cast<float>(SumReflB),
    };
    Report.TransmittedDirectionalAlbedo = {
        static_cast<float>(SumTranR),
        static_cast<float>(SumTranG),
        static_cast<float>(SumTranB),
    };
    Report.MaxChannelTotalAlbedo = std::max({
        Report.ReflectedDirectionalAlbedo.R + Report.TransmittedDirectionalAlbedo.R,
        Report.ReflectedDirectionalAlbedo.G + Report.TransmittedDirectionalAlbedo.G,
        Report.ReflectedDirectionalAlbedo.B + Report.TransmittedDirectionalAlbedo.B,
    });

    return Report;
}

} // namespace Frontier::MaterialSpace::Scattering
