/*============================================================================================================================================
                                                         SUBSURFACETRANSMISSION.SLANG
============================================================================================================================================*/
// 🧩 Lobe 6 & Lobe 7: Rough Microfacet GGX Dielectric Transmission + Abbe Dispersion + Christensen-Burley Subsurface Diffusion

module SubsurfaceTransmission;

float EvaluateExactFresnelScalar(float CosThetaI, float RelativeIOR)
{
    float CosI   = saturate(abs(CosThetaI));
    float Eta    = max(1.0e-4, RelativeIOR);
    float SinTSq = (1.0 - CosI * CosI) / (Eta * Eta);
    if (SinTSq >= 1.0)
    {
        return 1.0;
    }
    float CosT = sqrt(max(0.0, 1.0 - SinTSq));
    float Rs   = (CosI - Eta * CosT) / max(1.0e-6, CosI + Eta * CosT);
    float Rp   = (Eta * CosI - CosT) / max(1.0e-6, Eta * CosI + CosT);
    return saturate(0.5 * (Rs * Rs + Rp * Rp));
}

public float3 ComputeAbbeDispersedIor(float BaseIor, float AbbeNumber)
{
    float SafeIor  = max(1.001, BaseIor);
    float DeltaFC  = (SafeIor - 1.0) / clamp(AbbeNumber, 10.0, 120.0);
    return float3(max(1.001, SafeIor - 0.30 * DeltaFC), SafeIor, SafeIor + 0.70 * DeltaFC);
}

public float3 EvaluateDielectricTransmission(
    float3 LocalWo,
    float3 LocalWi,
    float  PerceptualRoughness,
    float  RelativeIOR,
    float  TransmissionWeight,
    float  AbbeNumber,
    bool   ThinWalledSlab,
    float3 AttenuationColor,
    float  AttenuationDistanceMeters)
{
    if (TransmissionWeight <= 1.0e-5 || LocalWo.z <= 1.0e-6 || LocalWi.z >= -1.0e-6)
    {
        return float3(0.0);
    }

    float Alpha   = max(1.0e-3, PerceptualRoughness * PerceptualRoughness);
    float AlphaSq = Alpha * Alpha;
    float PathRat = (ThinWalledSlab ? 0.015 : 0.08) / max(1.0e-3, AttenuationDistanceMeters * abs(LocalWi.z));
    float3 Medium = pow(clamp(AttenuationColor, float3(1.0e-4), float3(1.0)), float3(PathRat));

    if (ThinWalledSlab)
    {
        float3 Mirrored = float3(LocalWi.x, LocalWi.y, -LocalWi.z);
        float3 HalfVec  = normalize(LocalWo + Mirrored);
        float FSingle   = EvaluateExactFresnelScalar(dot(LocalWo, HalfVec), RelativeIOR);
        float TSlab     = (1.0 - FSingle) / max(1.0e-4, 1.0 + FSingle);
        float Denom     = HalfVec.z * HalfVec.z * (AlphaSq - 1.0) + 1.0;
        float NdfD      = AlphaSq / max(1.0e-6, 3.14159265359 * Denom * Denom);
        float TanO2     = (1.0 - LocalWo.z * LocalWo.z) / max(1.0e-6, LocalWo.z * LocalWo.z);
        float TanI2     = (1.0 - Mirrored.z * Mirrored.z) / max(1.0e-6, Mirrored.z * Mirrored.z);
        float G2        = 1.0 / (1.0 + 0.5 * (sqrt(1.0 + AlphaSq * TanO2) - 1.0) + 0.5 * (sqrt(1.0 + AlphaSq * TanI2) - 1.0));
        return Medium * (TransmissionWeight * TSlab * NdfD * G2 / max(1.0e-6, 4.0 * LocalWo.z * Mirrored.z));
    }

    float3 DispIor = ComputeAbbeDispersedIor(RelativeIOR, AbbeNumber);
    float3 OutBtdf = float3(0.0);
    [unroll]
    for (int Ch = 0; Ch < 3; ++Ch)
    {
        float EtaT   = DispIor[Ch];
        float3 HalfT = normalize(-(LocalWo + EtaT * LocalWi));
        if (HalfT.z < 0.0) HalfT = -HalfT;
        float WoDotH = dot(LocalWo, HalfT);
        float WiDotH = dot(LocalWi, HalfT);
        if (WoDotH > 1.0e-6 && WiDotH < -1.0e-6)
        {
            float SqrtD = WoDotH + EtaT * WiDotH;
            float Fres  = EvaluateExactFresnelScalar(WoDotH, EtaT);
            float Denom = HalfT.z * HalfT.z * (AlphaSq - 1.0) + 1.0;
            float NdfD  = AlphaSq / max(1.0e-6, 3.14159265359 * Denom * Denom);
            float TanO2 = (1.0 - LocalWo.z * LocalWo.z) / max(1.0e-6, LocalWo.z * LocalWo.z);
            float TanI2 = (1.0 - LocalWi.z * LocalWi.z) / max(1.0e-6, LocalWi.z * LocalWi.z);
            float G2    = 1.0 / (1.0 + 0.5 * (sqrt(1.0 + AlphaSq * TanO2) - 1.0) + 0.5 * (sqrt(1.0 + AlphaSq * TanI2) - 1.0));
            float Jac   = (EtaT * EtaT * WoDotH * abs(WiDotH)) / max(1.0e-6, LocalWo.z * abs(LocalWi.z) * SqrtD * SqrtD);
            OutBtdf[Ch] = TransmissionWeight * (1.0 - Fres) * NdfD * G2 * Jac * Medium[Ch];
        }
    }
    return OutBtdf;
}

public float3 EvaluateSubsurfaceLocalClosure(
    float3 LocalWo,
    float3 LocalWi,
    float  RelativeIOR,
    float  SubsurfaceWeight,
    float3 SubsurfaceColor,
    float3 MeanFreePathMm,
    float  PhaseAnisotropyG)
{
    if (SubsurfaceWeight <= 1.0e-5 || LocalWo.z <= 1.0e-6)
    {
        return float3(0.0);
    }

    float FoEntry = EvaluateExactFresnelScalar(LocalWo.z, RelativeIOR);
    if (LocalWi.z > 1.0e-6)
    {
        float FiExit    = EvaluateExactFresnelScalar(LocalWi.z, RelativeIOR);
        float FAvgWin   = 1.0 - 0.5 * ((RelativeIOR - 1.0) / (RelativeIOR + 1.0));
        float EntryExit = ((1.0 - FoEntry) * (1.0 - FiExit)) / max(0.25, FAvgWin * FAvgWin);
        float CosScat   = -dot(LocalWo, LocalWi);
        float G         = clamp(PhaseAnisotropyG, -0.85, 0.85);
        float PhaseHG   = (1.0 - G * G) / max(1.0e-4, 12.5663706 * pow(1.0 + G * G - 2.0 * G * CosScat, 1.5));
        float Shape     = min(0.334225, 0.261014 * min(1.15, EntryExit) + 0.18 * PhaseHG / max(0.15, LocalWo.z + LocalWi.z));
        return SubsurfaceWeight * SubsurfaceColor * Shape;
    }

    if (LocalWi.z < -1.0e-6)
    {
        float SlabPath = 6.0 / max(0.25, abs(LocalWi.z));
        float3 Trans   = exp(-SlabPath / max(float3(0.05), MeanFreePathMm));
        return SubsurfaceColor * Trans * (SubsurfaceWeight * (1.0 - FoEntry) * 0.15915494);
    }
    return float3(0.0);
}
