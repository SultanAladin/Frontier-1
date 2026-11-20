/*============================================================================================================================================
                                                      LENSFLAREBAKECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 GPU Compute Baker generating the 512 x 768 RGBA8 Multi-Archetype Lens Flare Kernel Atlas (Anamorphic, Streaks, Halos)

struct LensFlareBakeUniforms
{
    uint4  Dimensions; // x = TileWidthPx, y = TileHeightPx, z = StyleCount, w = Pad
    float4 Params;     // x = HdrScale, y..w = Pad
};

[[vk::binding(0, 0)]] ConstantBuffer<LensFlareBakeUniforms> BakeUniforms;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint>              OutAtlasRgba8;

uint PackFlareLinearToSrgb8(float3 LinearRGB, float HdrScale)
{
    float3 Norm = saturate(LinearRGB / max(HdrScale, 0.1));
    float3 Srgb = select(Norm <= float3(0.0031308), Norm * 12.92, 1.055 * pow(Norm, float3(1.0 / 2.2)) - 0.055);
    uint R = uint(clamp(Srgb.r * 255.0 + 0.5, 0.0, 255.0));
    uint G = uint(clamp(Srgb.g * 255.0 + 0.5, 0.0, 255.0));
    uint B = uint(clamp(Srgb.b * 255.0 + 0.5, 0.0, 255.0));
    return R | (G << 8u) | (B << 16u) | (0xFFu << 24u);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint TileW = max(1u, BakeUniforms.Dimensions.x);
    uint TileH = max(1u, BakeUniforms.Dimensions.y);
    uint Count = max(1u, BakeUniforms.Dimensions.z);
    uint TotalH = TileH * Count;
    if (DispatchThreadID.x >= TileW || DispatchThreadID.y >= TotalH) return;

    uint StyleIdx = DispatchThreadID.y / TileH;
    uint LocalY   = DispatchThreadID.y - StyleIdx * TileH;
    float U = (float(DispatchThreadID.x) + 0.5) / float(TileW);
    float V = (float(LocalY) + 0.5) / float(TileH);

    float KX = (U - 0.5) * 2.0;
    float KY = (V - 0.5) * 2.0;
    float CX = KX * 2.0;
    float CY = KY;
    float RC = sqrt(CX * CX + CY * CY);
    float R  = sqrt(KX * KX + KY * KY);

    float RectFade = (1.0 - smoothstep(0.84, 0.99, abs(KX))) * (1.0 - smoothstep(0.82, 0.99, abs(KY)));
    float CircFade = 1.0 - smoothstep(0.78, 0.98, R);
    float3 Col = float3(0.0, 0.0, 0.0);

    if (StyleIdx == 0u) // Anamorphic
    {
        float Core      = exp(-RC * RC / 0.0042);
        float StreakFin = exp(-abs(KY) / 0.009) * exp(-abs(KX) / 0.52);
        float StreakMed = exp(-abs(KY) / 0.032) * exp(-abs(KX) / 0.72);
        float WingPeak  = exp(-abs(KY) / 0.018) * exp(-pow(abs(KX) - 0.34, 2.0) / 0.016);
        float AnamAura  = exp(-RC * RC / 0.140) * 0.16;

        Col.r = (Core * 1.45 + StreakFin * 0.38 + StreakMed * 0.10 + WingPeak * 0.16 + AnamAura * 0.35) * RectFade;
        Col.g = (Core * 1.55 + StreakFin * 0.95 + StreakMed * 0.34 + WingPeak * 0.42 + AnamAura * 0.65) * RectFade;
        Col.b = (Core * 1.70 + StreakFin * 1.55 + StreakMed * 0.78 + WingPeak * 0.82 + AnamAura * 1.10) * RectFade;
    }
    else if (StyleIdx == 1u) // Streaks
    {
        float Theta     = atan2(KY, KX);
        float Primary   = pow(abs(cos(Theta * 6.0)), 84.0) * exp(-R / 0.38);
        float Secondary = pow(abs(cos((Theta + 0.261799) * 6.0)), 128.0) * exp(-R / 0.24);
        float Needle    = pow(abs(cos(Theta * 3.0)), 140.0) * exp(-R / 0.52) * 0.60;
        float Core      = exp(-R * R / 0.0038);
        float Aureole   = exp(-R / 0.095);
        float HaloRing  = exp(-pow(R - 0.54, 2.0) / 0.0024) * 0.14;

        float SpecPhase = R * 20.0;
        float3 Spec = float3(
            0.90 + 0.16 * sin(SpecPhase),
            0.88 + 0.14 * sin(SpecPhase + 2.094),
            0.85 + 0.16 * sin(SpecPhase + 4.188));
        float SpokeSum = Primary * 1.35 + Secondary * 0.95 + Needle;
        Col = (float3(Core * 1.55, Core * 1.48, Core * 1.35) +
               float3(Aureole * 0.42, Aureole * 0.34, Aureole * 0.24) +
               Spec * SpokeSum +
               float3(HaloRing * 0.95, HaloRing * 0.85, HaloRing * 0.72)) * CircFade;
    }
    else // Halos
    {
        float Theta   = atan2(KY, KX);
        float Core    = exp(-R * R / 0.0036);
        float Aureole = exp(-R / 0.11);

        float R1 = exp(-pow(R - 0.370, 2.0) / 0.00075);
        float G1 = exp(-pow(R - 0.342, 2.0) / 0.00075);
        float B1 = exp(-pow(R - 0.314, 2.0) / 0.00075);

        float R2 = exp(-pow(R - 0.640, 2.0) / 0.0012);
        float G2 = exp(-pow(R - 0.608, 2.0) / 0.0012);
        float B2 = exp(-pow(R - 0.576, 2.0) / 0.0012);

        float Sparkle = pow(abs(cos(Theta * 4.0)), 64.0) * exp(-R / 0.20) * 0.24;
        Col.r = (Core * 1.45 + Aureole * 0.38 + R1 * 0.58 + R2 * 0.36 + Sparkle * 1.00) * CircFade;
        Col.g = (Core * 1.40 + Aureole * 0.31 + G1 * 0.54 + G2 * 0.33 + Sparkle * 0.88) * CircFade;
        Col.b = (Core * 1.32 + Aureole * 0.24 + B1 * 0.62 + B2 * 0.38 + Sparkle * 0.78) * CircFade;
    }

    OutAtlasRgba8[DispatchThreadID.y * TileW + DispatchThreadID.x] =
        PackFlareLinearToSrgb8(Col, BakeUniforms.Params.x);
}
