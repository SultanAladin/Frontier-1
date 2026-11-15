/*============================================================================================================================================
                                                         PLANEAWARESHADOWFILTER.SLANG
============================================================================================================================================*/
// 🧩 Coplanar Blocker-Dilated Continuous-Sigma Plane-Aware A-Trous Shadow Denoiser preserving razor-sharp 0 px contact edges

module PlaneAwareShadowFilter;

struct PlaneFilterUniforms
{
    uint  ViewportWidth;
    uint  ViewportHeight;
    int   StepStridePx;
    float TanSunAngularRadius;
};

[[vk::binding(0, 0)]] ConstantBuffer<PlaneFilterUniforms> FilterUniforms;
[[vk::binding(1, 0)]] Texture2D<float2>                   InputVisBlocker;
[[vk::binding(2, 0)]] Texture2D<float4>                   WorldPositionTex;
[[vk::binding(3, 0)]] Texture2D<float4>                   WorldNormalTex;
[[vk::binding(4, 0)]] RWTexture2D<float2>                 OutputVisBlocker;

[shader("compute")]
[numthreads(8, 8, 1)]
void PlaneFilterComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= FilterUniforms.ViewportWidth || DispatchThreadID.y >= FilterUniforms.ViewportHeight)
    {
        return;
    }

    int2   CenterPx  = int2(DispatchThreadID.xy);
    float2 CenterVal = InputVisBlocker.Load(int3(CenterPx, 0));
    float4 CenterPos = WorldPositionTex.Load(int3(CenterPx, 0));
    if (CenterPos.w <= 0.0)
    {
        OutputVisBlocker[DispatchThreadID.xy] = CenterVal;
        return;
    }

    float3 CenterNrm = normalize(WorldNormalTex.Load(int3(CenterPx, 0)).xyz);
    float  CamDist   = max(0.25, length(CenterPos.xyz));
    float  PenumbraM = CenterVal.y * FilterUniforms.TanSunAngularRadius;
    float  SigmaPx   = clamp(0.35 + (PenumbraM / CamDist) * float(FilterUniforms.ViewportHeight) * 1.4, 0.35, 9.0);
    float  InvTwoSig = 1.0 / (2.0 * SigmaPx * SigmaPx);

    float SumVis    = 0.0;
    float SumWeight = 0.0;
    float MaxBlock  = CenterVal.y;

    [unroll]
    for (int KY = -1; KY <= 1; ++KY)
    {
        [unroll]
        for (int KX = -1; KX <= 1; ++KX)
        {
            int2 SamplePx = clamp(CenterPx + int2(KX, KY) * FilterUniforms.StepStridePx,
                                  int2(0, 0),
                                  int2(int(FilterUniforms.ViewportWidth) - 1, int(FilterUniforms.ViewportHeight) - 1));
            float4 NeighPos = WorldPositionTex.Load(int3(SamplePx, 0));
            if (NeighPos.w <= 0.0) continue;

            float3 NeighNrm = normalize(WorldNormalTex.Load(int3(SamplePx, 0)).xyz);
            float2 NeighVal = InputVisBlocker.Load(int3(SamplePx, 0));

            float DistSq    = float((KX * FilterUniforms.StepStridePx) * (KX * FilterUniforms.StepStridePx) +
                                    (KY * FilterUniforms.StepStridePx) * (KY * FilterUniforms.StepStridePx));
            float WSpatial  = exp(-DistSq * InvTwoSig);
            float PlaneDist = abs(dot(CenterNrm, NeighPos.xyz - CenterPos.xyz));
            float WPlane    = exp(-PlaneDist * 65.0);
            float WNrm      = pow(saturate(dot(CenterNrm, NeighNrm)), 24.0);

            float W = WSpatial * WPlane * WNrm;
            SumVis    += NeighVal.x * W;
            SumWeight += W;
            if (PlaneDist < 0.03 && WNrm > 0.5) MaxBlock = max(MaxBlock, NeighVal.y);
        }
    }

    float Filtered = (SumWeight > 1.0e-6) ? (SumVis / SumWeight) : CenterVal.x;
    OutputVisBlocker[DispatchThreadID.xy] = float2(Filtered, MaxBlock);
}
