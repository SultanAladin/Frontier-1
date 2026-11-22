/*============================================================================================================================================
                                                         DEPTHPYRAMIDDOWNSAMPLE.SLANG
============================================================================================================================================*/
// 🧩 Conservative Reversed-Z Hi-Z mip chain downsampler with odd-dimension 3x3 floor guard (min reduction = farthest occluder horizon)

module DepthPyramidDownsample;

struct DepthDownsampleUniforms
{
    uint SrcWidth;
    uint SrcHeight;
    uint DstWidth;
    uint DstHeight;
};

[[vk::binding(0, 0)]] ConstantBuffer<DepthDownsampleUniforms> Uniforms;
[[vk::binding(1, 0)]] Texture2D<float>                        SourceDepthMip;
[[vk::binding(2, 0)]] RWTexture2D<float>                      TargetDepthMip;

float LoadClampedDepth(int2 Coord)
{
    int2 Safe = clamp(Coord, int2(0, 0), int2(int(Uniforms.SrcWidth) - 1, int(Uniforms.SrcHeight) - 1));
    return SourceDepthMip.Load(int3(Safe, 0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void DepthPyramidComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= Uniforms.DstWidth || DispatchThreadID.y >= Uniforms.DstHeight)
    {
        return;
    }

    int2 Base = int2(DispatchThreadID.xy) * 2;
    float MinHorizon = min(
        min(LoadClampedDepth(Base + int2(0, 0)), LoadClampedDepth(Base + int2(1, 0))),
        min(LoadClampedDepth(Base + int2(0, 1)), LoadClampedDepth(Base + int2(1, 1))));

    bool OddW = ((Uniforms.SrcWidth & 1u) != 0u) && (Uniforms.SrcWidth > 1u);
    bool OddH = ((Uniforms.SrcHeight & 1u) != 0u) && (Uniforms.SrcHeight > 1u);

    if (OddW)
    {
        MinHorizon = min(MinHorizon, min(LoadClampedDepth(Base + int2(2, 0)), LoadClampedDepth(Base + int2(2, 1))));
    }
    if (OddH)
    {
        MinHorizon = min(MinHorizon, min(LoadClampedDepth(Base + int2(0, 2)), LoadClampedDepth(Base + int2(1, 2))));
    }
    if (OddW && OddH)
    {
        MinHorizon = min(MinHorizon, LoadClampedDepth(Base + int2(2, 2)));
    }

    TargetDepthMip[DispatchThreadID.xy] = MinHorizon;
}
