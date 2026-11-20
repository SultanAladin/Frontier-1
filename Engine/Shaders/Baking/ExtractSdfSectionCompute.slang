/*============================================================================================================================================
                                                        EXTRACTSDFSECTIONCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Sub-millisecond interactive GPU 3D->2D SDF cross-section extractor with trilinear sampling, signed interior coloring & iso-zero contour.

#include "BakeShaderLayouts.slang"

[[vk::binding(0, 0)]] ConstantBuffer<GpuVolumeBakeConfig> VolumeConfig;
[[vk::binding(1, 0)]] StructuredBuffer<float>           VolumeDistances;
[[vk::binding(2, 0)]] [format("rgba8")] RWTexture2D<float4> OutSectionImage;

float SampleVolumeTrilinear(float3 Uvw)
{
    int Dim = max(2, int(VolumeConfig.GridDim));
    float3 Scaled = clamp(Uvw, float3(0.0), float3(1.0)) * float(Dim - 1);
    int3 I0 = clamp(int3(floor(Scaled)), int3(0), int3(Dim - 1));
    int3 I1 = clamp(I0 + int3(1), int3(0), int3(Dim - 1));
    float3 F = Scaled - float3(I0);

    float D000 = VolumeDistances[(I0.z * Dim + I0.y) * Dim + I0.x];
    float D100 = VolumeDistances[(I0.z * Dim + I0.y) * Dim + I1.x];
    float D010 = VolumeDistances[(I0.z * Dim + I1.y) * Dim + I0.x];
    float D110 = VolumeDistances[(I0.z * Dim + I1.y) * Dim + I1.x];
    float D001 = VolumeDistances[(I1.z * Dim + I0.y) * Dim + I0.x];
    float D101 = VolumeDistances[(I1.z * Dim + I0.y) * Dim + I1.x];
    float D011 = VolumeDistances[(I1.z * Dim + I1.y) * Dim + I0.x];
    float D111 = VolumeDistances[(I1.z * Dim + I1.y) * Dim + I1.x];

    float Dx00 = lerp(D000, D100, F.x);
    float Dx10 = lerp(D010, D110, F.x);
    float Dx01 = lerp(D001, D101, F.x);
    float Dx11 = lerp(D011, D111, F.x);
    float Dxy0 = lerp(Dx00, Dx10, F.y);
    float Dxy1 = lerp(Dx01, Dx11, F.y);
    return lerp(Dxy0, Dxy1, F.z);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 Tid : SV_DispatchThreadID)
{
    uint SectionPx = VolumeConfig.SectionSizePx;
    if (Tid.x >= SectionPx || Tid.y >= SectionPx)
    {
        return;
    }

    float U = (float(Tid.x) + 0.5) / float(SectionPx);
    float V = 1.0 - (float(Tid.y) + 0.5) / float(SectionPx);
    float D = saturate(VolumeConfig.SectionDepthNormalized);

    float3 Uvw;
    if (VolumeConfig.SectionAxis == 0u)      // Z section (XY plane)
    {
        Uvw = float3(U, V, D);
    }
    else if (VolumeConfig.SectionAxis == 1u) // Y section (XZ plane)
    {
        Uvw = float3(U, D, V);
    }
    else                                     // X section (YZ plane)
    {
        Uvw = float3(D, U, V);
    }

    float DistMeters = SampleVolumeTrilinear(Uvw);
    float3 Col = ColorizeSdfDistance(
        DistMeters,
        max(0.05, VolumeConfig.BoundsDiagMeters * 0.42),
        VolumeConfig.SignedMode != 0u);

    // Subtle scientific coordinate hairlines on section quarter intervals
    float2 Grid = abs(frac(float2(U, V) * 4.0) - 0.5);
    float  Hair = 1.0 - smoothstep(0.0, 0.014, min(Grid.x, Grid.y));
    Col = lerp(Col, float3(0.45, 0.56, 0.72), Hair * 0.14);

    OutSectionImage[Tid.xy] = float4(saturate(Col), 1.0);
}
