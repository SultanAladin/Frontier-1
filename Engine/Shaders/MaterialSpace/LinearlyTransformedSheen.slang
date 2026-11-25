/*============================================================================================================================================
                                                        LINEARLYTRANSFORMEDSHEEN.SLANG
============================================================================================================================================*/
// 🧩 Lobe 8: Zeltner, Burley & Chiang (SIGGRAPH 2022) Linearly Transformed Cosine (LTC) multiple-scattering cloth/velvet sheen

module LinearlyTransformedSheen;

float3 SampleAnalyticSheenFit(float CosTheta, float SheenRoughness)
{
    float Mu = clamp(CosTheta, 1.0e-3, 1.0);
    float R  = clamp(SheenRoughness, 0.05, 1.0);
    float AlphaInv = (2.58126 * Mu + 0.813703 * R) / (1.0 + 0.310327 * Mu * Mu + 2.60994 * Mu * R);
    float OneMinusR = R - 1.0;
    float ShearB   = sqrt(max(0.0, 1.0 - Mu * Mu)) * (OneMinusR * OneMinusR * OneMinusR) /
                     (0.0000254053 + 1.71228 * Mu - 1.71506 * Mu * R + 1.34174 * R * R);
    float Grazing  = pow(1.0 - Mu, 2.6 - 1.4 * R);
    float BaseTerm = 0.045 + 0.30 * R * R;
    float SheenR   = clamp(BaseTerm + (0.82 - BaseTerm) * Grazing, 0.01, 0.92);
    return float3(clamp(AlphaInv, 0.08, 8.0), clamp(ShearB, -4.0, 4.0), SheenR);
}

float EvaluateDirectedSheenLtc(float3 LocalWo, float3 LocalWi, float SheenRoughness, out float OutReflectance)
{
    float3 Coeffs  = SampleAnalyticSheenFit(LocalWo.z, SheenRoughness);
    OutReflectance = Coeffs.z;
    if (LocalWo.z <= 1.0e-6 || LocalWi.z <= 1.0e-6)
    {
        return 0.0;
    }

    float SinLen  = length(LocalWo.xy);
    float2 ViewT1 = (SinLen > 1.0e-5) ? (LocalWo.xy / SinLen) : float2(1.0, 0.0);
    float2 ViewT2 = float2(-ViewT1.y, ViewT1.x);

    float WiX   = dot(LocalWi.xy, ViewT1);
    float WiY   = dot(LocalWi.xy, ViewT2);
    float OrigX = Coeffs.x * WiX + Coeffs.y * LocalWi.z;
    float OrigY = Coeffs.x * WiY;
    float OrigZ = LocalWi.z;

    float LenSq = OrigX * OrigX + OrigY * OrigY + OrigZ * OrigZ;
    if (LenSq <= 1.0e-8)
    {
        return 0.0;
    }
    float DetInvM = Coeffs.x * Coeffs.x;
    return Coeffs.z * (DetInvM / (LenSq * LenSq * 3.14159265359));
}

public float4 EvaluateClothSheenLobe(
    float3 LocalWo,
    float3 LocalWi,
    float  SheenWeight,
    float  SheenRoughness,
    float3 SheenColor)
{
    float Weight = saturate(SheenWeight);
    if (Weight <= 1.0e-5 || LocalWo.z <= 1.0e-6 || LocalWi.z <= 1.0e-6)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float ReflO = 0.0;
    float ReflI = 0.0;
    float Fwd   = EvaluateDirectedSheenLtc(LocalWo, LocalWi, SheenRoughness, ReflO);
    float Rev   = EvaluateDirectedSheenLtc(LocalWi, LocalWo, SheenRoughness, ReflI);
    float Sym   = Weight * 0.5 * (Fwd + Rev);
    float Scale = saturate(1.0 - Weight * max(ReflO, ReflI));
    return float4(SheenColor * Sym, Scale);
}
