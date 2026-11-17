/*============================================================================================================================================
                                                         FLUIDLIGHTREDUCTION.SLANG
============================================================================================================================================*/
// 🧩 Reduces 8 octants of hot fire cells into FluidEmissiveLightGpu proxies for OpenPBR, DDGI & PathTrace illumination

#include "FluidSolverLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<FluidRenderUniformBlock>  RenderCfg;
[[vk::binding(1, 0)]] Texture3D<float4>                        HeroThermoTex;
[[vk::binding(2, 0)]] RWStructuredBuffer<FluidEmissiveLightGpu> OutEmissiveLights;

[shader("compute")]
[numthreads(8, 1, 1)]
void FluidLightReductionEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Oct = DispatchThreadId.x;
    if (Oct >= 8u) return;

    uint Res = uint(RenderCfg.HeroBoxMaxRes.w);
    uint Half = max(1u, Res / 2u);
    uint OX = (Oct & 1u) != 0u ? Half : 0u;
    uint OY = (Oct & 2u) != 0u ? Half : 0u;
    uint OZ = (Oct & 4u) != 0u ? Half : 0u;

    float3 BoxMin = RenderCfg.HeroBoxMinFade.xyz;
    float3 BoxMax = RenderCfg.HeroBoxMaxRes.xyz;
    float3 Span = BoxMax - BoxMin;
    float CellVol = (Span.x * Span.y * Span.z) / max(1.0, float(Res * Res * Res));
    uint Stride = (Res >= 64u) ? 2u : 1u;
    float WeightMul = CellVol * float(Stride * Stride * Stride);

    float SumW = 0.0;
    float3 SumPos = float3(0.0, 0.0, 0.0);
    float3 SumIrr = float3(0.0, 0.0, 0.0);

    for (uint Z = OZ; Z < min(Res, OZ + Half); Z += Stride)
    {
        for (uint Y = OY; Y < min(Res, OY + Half); Y += Stride)
        {
            for (uint X = OX; X < min(Res, OX + Half); X += Stride)
            {
                float Temp = HeroThermoTex.Load(int4(int(X), int(Y), int(Z), 0)).y;
                if (Temp <= 0.35) continue;
                float3 Em = EvaluateFluidBlackbodyPaletteGpu(Temp, RenderCfg.ShadingCore.w, int(RenderCfg.ShadingMode.x + 0.5));
                float Lum = dot(Em, float3(0.2126, 0.7152, 0.0722));
                if (Lum <= 0.02) continue;

                float3 UVW = (float3(float(X), float(Y), float(Z)) + 0.5) / float(Res);
                float3 WPos = lerp(BoxMin, BoxMax, UVW);
                float W = Lum * WeightMul;
                SumW += W;
                SumPos += WPos * W;
                SumIrr += Em * WeightMul;
            }
        }
    }

    FluidEmissiveLightGpu Proxy;
    if (SumW > 1.0e-5)
    {
        float Gain = RenderCfg.ShadingCore.z * 0.38;
        Proxy.PositionRadius  = float4(SumPos / SumW, clamp(0.45 * pow(SumW + 0.1, 1.0 / 3.0), 0.35, 3.5));
        Proxy.ColorIrradiance = float4(SumIrr * Gain, 1.0);
    }
    else
    {
        Proxy.PositionRadius  = float4(0.0, 0.0, 0.0, 0.5);
        Proxy.ColorIrradiance = float4(0.0, 0.0, 0.0, 0.0);
    }
    OutEmissiveLights[Oct] = Proxy;
}
