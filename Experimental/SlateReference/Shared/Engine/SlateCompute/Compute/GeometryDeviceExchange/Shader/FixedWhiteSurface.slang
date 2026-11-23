//============================================================================================================================================
//                                                        FIXEDWHITESURFACE.SLANG
//============================================================================================================================================
// 🧩 Resolves visibility words into the initial transparent fixed-white dielectric radiance surface.

[[vk::binding(0, 0)]] RWTexture2D<uint2>   VisibilityInput;
[[vk::binding(1, 0)]] RWTexture2D<float4>  RadianceOutput;

static const uint AbsentPartition = 0xFFFFFFFFu;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 Dispatched : SV_DispatchThreadID)
{
    uint Width;
    uint Height;
    RadianceOutput.GetDimensions(Width, Height);

    if (Dispatched.x >= Width || Dispatched.y >= Height)
        return;

    const uint2 Resolved = VisibilityInput.Load(int2(Dispatched.xy));

    // The visibility raster's clear word is the sole unoccupied classification. An empty target is transparent,
    // so the viewport's atmosphere remains visible; every winning surface is initially the same white dielectric.
    RadianceOutput[Dispatched.xy] = Resolved.x == AbsentPartition
        ? float4(0.0f, 0.0f, 0.0f, 0.0f)
        : float4(0.82f, 0.82f, 0.82f, 1.0f);
}
