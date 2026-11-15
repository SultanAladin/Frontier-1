/*============================================================================================================================================
                                                          LINEARLYTRANSFORMEDSHEEN.H
============================================================================================================================================*/
// 🧩 Lobe 8: Zeltner, Burley & Chiang (SIGGRAPH 2022) Linearly Transformed Cosine (LTC) multiple-scattering cloth & velvet sheen

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/MaterialSpace/EnergyCompensation/SheenCosineIntegral.h"
#include "Engine/MaterialSpace/SurfaceMaterial.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace::Scattering
{

//------------------------------------------------------------------------------------------------------------------------
//                                       ZELTNER 2022 LTC MULTIPLE-SCATTERING SHEEN
//------------------------------------------------------------------------------------------------------------------------

struct SheenInteractionResult
{
    Mathematics::LinearRGB SheenBrdf             { 0.0f, 0.0f, 0.0f }; // [1/sr] - Symmetrized reciprocal LTC cloth sheen BRDF
    float                  SubstrateEnergyScale  = 1.0f;               // [-]    - (1 - w_s * max(R(wo), R(wi))) substrate transmission
};

// 📝 Evaluate single-direction LTC clamped-cosine density D_LTC(wi | wo) where M^-1 aligns wo's azimuth along +X
[[nodiscard]] inline float EvaluateSingleDirectionSheenLtc(
    Mathematics::Axis3D           LocalWo,
    Mathematics::Axis3D           LocalWi,
    float                         SheenRoughness,
    const SheenLinearCosineSheet& SheenSheet,
    float&                        OutDirectionalReflectance) noexcept
{
    const auto Coeffs = SampleSheenLinearCosineCoefficients(SheenSheet, LocalWo.Z, SheenRoughness);
    OutDirectionalReflectance = Coeffs.DirectionalSheenR;

    if (LocalWo.Z <= 1.0e-6f || LocalWi.Z <= 1.0e-6f)
    {
        return 0.0f;
    }

    // Construct view-aligned azimuthal basis in tangent plane: T1 along projected LocalWo, T2 perpendicular
    const float SinLen = std::sqrt(LocalWo.X * LocalWo.X + LocalWo.Y * LocalWo.Y);
    const Mathematics::Axis2D ViewT1 = (SinLen > 1.0e-5f)
        ? Mathematics::Axis2D{ LocalWo.X / SinLen, LocalWo.Y / SinLen }
        : Mathematics::Axis2D{ 1.0f, 0.0f };
    const Mathematics::Axis2D ViewT2 { -ViewT1.Y, ViewT1.X };

    const float WiX = LocalWi.X * ViewT1.X + LocalWi.Y * ViewT1.Y;
    const float WiY = LocalWi.X * ViewT2.X + LocalWi.Y * ViewT2.Y;
    const float WiZ = LocalWi.Z;

    // Apply M^-1 = [[A_inv, 0, B_inv], [0, A_inv, 0], [0, 0, 1]]
    const float OrigX = Coeffs.InverseScaleA * WiX + Coeffs.InverseShearB * WiZ;
    const float OrigY = Coeffs.InverseScaleA * WiY;
    const float OrigZ = WiZ;

    const float LenSq = OrigX * OrigX + OrigY * OrigY + OrigZ * OrigZ;
    if (LenSq <= 1.0e-8f || OrigZ <= 0.0f)
    {
        return 0.0f;
    }

    // Exact LTC Jacobian: (det M^-1) / ||M^-1 wi||^3 * (OrigZ / ||M^-1 wi||) / pi; divides by cos(theta_i) = WiZ for BRDF f(wo, wi)!
    // Notice OrigZ == WiZ, so OrigZ / WiZ == 1.0 identically (zero grazing 1/cos singularity)!
    const float DetInvM   = Coeffs.InverseScaleA * Coeffs.InverseScaleA;
    const float InvNorm4  = 1.0f / (LenSq * LenSq);
    const float CosineLtc = (DetInvM * InvNorm4) / Constants::PiFloat;
    return Coeffs.DirectionalSheenR * CosineLtc;
}

// 📝 Evaluate reciprocal, White-Furnace-exact Zeltner LTC cloth sheen + substrate attenuation factor
[[nodiscard]] inline SheenInteractionResult EvaluateClothSheenLayer(
    Mathematics::Axis3D           LocalWo,
    Mathematics::Axis3D           LocalWi,
    const ClothSheenParameters&   Params,
    const SheenLinearCosineSheet& SheenSheet) noexcept
{
    SheenInteractionResult Result {};
    const float Weight = std::clamp(Params.SheenWeight, 0.0f, 1.0f);
    if (Weight <= 1.0e-5f || LocalWo.Z <= 1.0e-6f || LocalWi.Z <= 1.0e-6f)
    {
        return Result;
    }

    float ReflO = 0.0f, ReflI = 0.0f;
    const float ForwardLtc = EvaluateSingleDirectionSheenLtc(LocalWo, LocalWi, Params.SheenRoughness, SheenSheet, ReflO);
    const float ReverseLtc = EvaluateSingleDirectionSheenLtc(LocalWi, LocalWo, Params.SheenRoughness, SheenSheet, ReflI);

    // Symmetrize forward & adjoint LTC evaluations for exact Helmholtz reciprocity f(wo, wi) == f(wi, wo)
    const float SymmetricLtc = Weight * 0.5f * (ForwardLtc + ReverseLtc);
    Result.SheenBrdf = {
        SymmetricLtc * Params.SheenColor.R,
        SymmetricLtc * Params.SheenColor.G,
        SymmetricLtc * Params.SheenColor.B,
    };

    // Conservative reciprocal substrate energy window so sheen + substrate never exceeds 1.0 at grazing incidence
    Result.SubstrateEnergyScale = std::clamp(1.0f - Weight * std::max(ReflO, ReflI), 0.0f, 1.0f);
    return Result;
}

} // namespace Frontier::MaterialSpace::Scattering
