/*============================================================================================================================================
                                                          BAKEDILATIONCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 GPU UV edge gutter padding kernel extending covered texel borders smoothly outward across all 17 surface channel array layers.

#include "BakeShaderLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<GpuSurfaceBakeConfig> BakeConfig;
[[vk::binding(1, 0)]] Texture2DArray<float4>                SourceSurfaceChannels;
[[vk::binding(2, 0)]] [format("rgba8")] RWTexture2DArray<float4> OutSurfaceChannels;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 Tid : SV_DispatchThreadID)
{
    int ResPx = int(BakeConfig.ResolutionPx);
    if (int(Tid.x) >= ResPx || int(Tid.y) >= ResPx || Tid.z >= 17u)
    {
        return;
    }

    int4 Coord = int4(int(Tid.x), int(Tid.y), int(Tid.z), 0);
    float4 Center = SourceSurfaceChannels.Load(Coord);
    if (Center.a > 0.5)
    {
        OutSurfaceChannels[Tid] = Center;
        return;
    }

    float3 ColorSum  = float3(0.0, 0.0, 0.0);
    float  WeightSum = 0.0;
    for (int Dy = -1; Dy <= 1; ++Dy)
    {
        for (int Dx = -1; Dx <= 1; ++Dx)
        {
            if (Dx == 0 && Dy == 0)
            {
                continue;
            }
            int Nx = int(Tid.x) + Dx;
            int Ny = int(Tid.y) + Dy;
            if (Nx < 0 || Ny < 0 || Nx >= ResPx || Ny >= ResPx)
            {
                continue;
            }
            float4 Neighbor = SourceSurfaceChannels.Load(int4(Nx, Ny, int(Tid.z), 0));
            if (Neighbor.a > 0.5)
            {
                float W = (Dx == 0 || Dy == 0) ? 1.0 : 0.70710678;
                ColorSum  += Neighbor.rgb * W;
                WeightSum += W;
            }
        }
    }

    if (WeightSum > 0.0)
    {
        OutSurfaceChannels[Tid] = float4(ColorSum / WeightSum, 1.0);
    }
    else
    {
        OutSurfaceChannels[Tid] = float4(0.0, 0.0, 0.0, 0.0);
    }
}
