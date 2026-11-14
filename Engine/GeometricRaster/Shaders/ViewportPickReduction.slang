//============================================================================================================================================
//                                                          VIEWPORTPICKREDUCTION.SLANG
//============================================================================================================================================
// GPU marquee-box & circular-brush reduction scanning R32G32B32A32_UINT pick buffer and atomically setting VRAM SelectionBitmask bits

struct PickReductionParams
{
    uint2 ViewportDimensions; // Framebuffer width & height
    uint2 RegionMinPx;        // Top-left pixel of marquee or brush AABB
    uint2 RegionMaxPx;        // Bottom-right pixel (inclusive)
    uint2 BrushCenterPx;      // Brush center pixel (when BrushRadiusSq > 0)
    uint  BrushRadiusSq;      // 0 = rectangular marquee; > 0 = circular brush radius^2
    uint  MaxFaceCount;       // PolygonNetwork face ceiling
};

[[vk::binding(0, 0)]] ConstantBuffer<PickReductionParams> Params;
[[vk::binding(1, 0)]] StructuredBuffer<uint4>             PickPixelBuffer;  // Flattened Width * Height R32G32B32A32_UINT pick IDs
[[vk::binding(2, 0)]] RWStructuredBuffer<uint>            SelectionBitmask; // Atomic bitset output (1 bit per FaceIndex)
[[vk::binding(3, 0)]] RWStructuredBuffer<uint>            HitSummary;       // [0] = HitPixelCount, [1] = FirstInstanceSlot, [2] = FirstGizmoOrdinal

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint2 Pixel = Params.RegionMinPx + DispatchThreadID.xy;
    if (Pixel.x > Params.RegionMaxPx.x || Pixel.y > Params.RegionMaxPx.y ||
        Pixel.x >= Params.ViewportDimensions.x || Pixel.y >= Params.ViewportDimensions.y)
    {
        return;
    }

    if (Params.BrushRadiusSq > 0u)
    {
        int2 Delta  = int2(Pixel) - int2(Params.BrushCenterPx);
        uint DistSq = uint(Delta.x * Delta.x + Delta.y * Delta.y);
        if (DistSq > Params.BrushRadiusSq)
        {
            return;
        }
    }

    uint  FlatIndex = Pixel.y * Params.ViewportDimensions.x + Pixel.x;
    uint4 PickedKey = PickPixelBuffer[FlatIndex];

    if (PickedKey.w != 0u)
    {
        InterlockedMax(HitSummary[2u], PickedKey.w);
    }

    uint FaceIdx = PickedKey.y;
    if (PickedKey.x != 0u && FaceIdx < Params.MaxFaceCount)
    {
        InterlockedOr(SelectionBitmask[FaceIdx >> 5u], 1u << (FaceIdx & 31u));
        InterlockedAdd(HitSummary[0u], 1u);
        InterlockedMax(HitSummary[1u], PickedKey.x);
    }
}
