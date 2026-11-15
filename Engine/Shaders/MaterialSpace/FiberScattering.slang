/*============================================================================================================================================
                                                            FIBERSCATTERING.SLANG
============================================================================================================================================*/
// 🧩 Lobe 9: Chiang et al. (2016) / Marschner Dual-Scattering Hair & Technical Fiber BCSDF (R + TT + TRT lobes)

module FiberScattering;

float FiberLogisticM(float DeltaTheta, float ScaleS)
{
    float SafeS  = max(0.015, ScaleS);
    float AbsVal = abs(DeltaTheta) / SafeS;
    if (AbsVal > 20.0) return 0.0;
    float ExpNeg = exp(-AbsVal);
    float OnePlus = 1.0 + ExpNeg;
    return ExpNeg / max(1.0e-6, OnePlus * OnePlus * SafeS);
}

float FiberAzimuthalN(float DeltaPhi, float ScaleS)
{
    float Wrapped = fmod(DeltaPhi + 3.14159265359, 6.28318530718);
    if (Wrapped < 0.0) Wrapped += 6.28318530718;
    Wrapped -= 3.14159265359;

    float SafeS    = max(0.03, ScaleS);
    float ExpNeg   = exp(-abs(Wrapped) / SafeS);
    float ExpPi    = exp(-3.14159265359 / SafeS);
    float TrimNorm = max(0.1, (1.0 - ExpPi) / (1.0 + ExpPi));
    float OnePlus  = 1.0 + ExpNeg;
    return ExpNeg / max(1.0e-6, SafeS * OnePlus * OnePlus * TrimNorm);
}

public float3 EvaluateFiberStrandBcsdf(
    float3 LocalWo,
    float3 LocalWi,
    float  FiberIOR,
    float3 DyeColor,
    float  FiberWeight,
    float  LongitudinalRoughness,
    float  AzimuthalRoughness,
    float  CuticleTiltRadians,
    float  MelaninConcentration,
    float  PheomelaninRatio)
{
    if (FiberWeight <= 1.0e-5 || LocalWo.z <= 1.0e-6 || LocalWi.z <= 1.0e-6)
    {
        return float3(0.0);
    }

    float ThetaO    = asin(clamp(LocalWo.x, -0.999, 0.999));
    float ThetaI    = asin(clamp(LocalWi.x, -0.999, 0.999));
    float ThetaH    = 0.5 * (ThetaO + ThetaI);
    float CosThetaD = max(0.10, cos(0.5 * (ThetaI - ThetaO)));
    float AzimPhi   = atan2(LocalWi.y, LocalWi.z) + atan2(LocalWo.y, LocalWo.z);

    float BetaM = clamp(LongitudinalRoughness, 0.03, 1.0) * 0.62;
    float BetaN = clamp(AzimuthalRoughness, 0.03, 1.0) * 0.78;
    float Alpha = CuticleTiltRadians;

    float LongR   = 0.5 * (FiberLogisticM(ThetaH - Alpha, BetaM) + FiberLogisticM(ThetaH + Alpha, BetaM));
    float LongTT  = FiberLogisticM(ThetaH, 0.65 * BetaM);
    float LongTRT = 0.5 * (FiberLogisticM(ThetaH - 1.5 * Alpha, 1.45 * BetaM) + FiberLogisticM(ThetaH + 1.5 * Alpha, 1.45 * BetaM));

    float AzimR   = FiberAzimuthalN(AzimPhi, BetaN);
    float AzimTT  = FiberAzimuthalN(AzimPhi, 1.65 * BetaN);
    float AzimTRT = 0.5 * (FiberAzimuthalN(AzimPhi - 0.55, 1.25 * BetaN) + FiberAzimuthalN(AzimPhi + 0.55, 1.25 * BetaN));

    float F0      = ((FiberIOR - 1.0) / (FiberIOR + 1.0));
    F0           *= F0;
    float CosHalf = max(0.05, CosThetaD * cos(0.5 * AzimPhi));
    float FCut    = F0 + (1.0 - F0) * pow(1.0 - saturate(CosHalf), 5.0);
    float TWin    = 1.0 - FCut;

    float Eu    = saturate(MelaninConcentration) * (1.0 - saturate(PheomelaninRatio)) * 4.0;
    float Pheo  = saturate(MelaninConcentration) * saturate(PheomelaninRatio) * 4.0;
    float3 SigA = Eu * float3(0.419, 0.697, 1.370) + Pheo * float3(0.187, 0.400, 1.050) - log(clamp(DyeColor, float3(0.02), float3(1.0))) * 0.35;
    float3 Tran = exp(-SigA * (1.15 / CosThetaD));

    float GeomNorm = FiberWeight * 0.62 / max(0.24, CosThetaD * CosThetaD);
    return GeomNorm * (float3(FCut * LongR * AzimR) + (TWin * TWin * LongTT * AzimTT) * Tran + (TWin * TWin * FCut * LongTRT * AzimTRT) * (Tran * Tran));
}
