/*============================================================================================================================================
                                                    REFITWIDEBOUNDINGTREECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Order 2 HighEnd (< 0.08 ms) bottom-up binary AABB refit compute shader for skinned / deforming character triangle hierarchies

struct GpuRayTrianglePrimitive48
{
    float3 Corner0;
    uint   SourceFaceIndex;
    float3 Edge01;
    uint   PackedMaterialOpaque;
    float3 Edge02;
    uint   PrimitiveOrdinal;
};

struct GpuRefittableBinaryEntry32
{
    float3 BoundsMin;
    uint   LeftOrFirst;
    float3 BoundsMax;
    uint   PrimitiveCount;
};

struct RefitPushUniforms
{
    uint TotalBinaryEntries;
    uint DispatchPhase;      // 0u = parallel leaf AABB refit, 1u = level/post-order internal union
    uint InternalBatchStart;
    uint InternalBatchCount;
};

[[vk::binding(0, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48>    DeformedPrimitives;
[[vk::binding(1, 0)]] StructuredBuffer<uint>                         BottomUpInternalOrder;
[[vk::binding(2, 0)]] RWStructuredBuffer<GpuRefittableBinaryEntry32> BinaryHierarchy;
[[vk::binding(3, 0)]] ConstantBuffer<RefitPushUniforms>              RefitUniforms;

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint ThreadIdx = DispatchThreadId.x;

    if (RefitUniforms.DispatchPhase == 0u)
    {
        if (ThreadIdx >= RefitUniforms.TotalBinaryEntries) return;
        GpuRefittableBinaryEntry32 Entry = BinaryHierarchy[ThreadIdx];
        if (Entry.PrimitiveCount == 0u) return;

        float3 BMin = float3( 1.0e30);
        float3 BMax = float3(-1.0e30);
        for (uint K = 0u; K < Entry.PrimitiveCount; ++K)
        {
            GpuRayTrianglePrimitive48 Tri = DeformedPrimitives[Entry.LeftOrFirst + K];
            float3 V0 = Tri.Corner0;
            float3 V1 = Tri.Corner0 + Tri.Edge01;
            float3 V2 = Tri.Corner0 + Tri.Edge02;
            BMin = min(BMin, min(V0, min(V1, V2)));
            BMax = max(BMax, max(V0, max(V1, V2)));
        }
        float3 Pad = select((BMax - BMin) < float3(1.0e-6), float3(5.0e-7), float3(0.0));
        Entry.BoundsMin = BMin - Pad;
        Entry.BoundsMax = BMax + Pad;
        BinaryHierarchy[ThreadIdx] = Entry;
    }
    else
    {
        if (ThreadIdx >= RefitUniforms.InternalBatchCount) return;
        uint InternalIdx = BottomUpInternalOrder[RefitUniforms.InternalBatchStart + ThreadIdx];
        if (InternalIdx >= RefitUniforms.TotalBinaryEntries) return;

        GpuRefittableBinaryEntry32 Cur   = BinaryHierarchy[InternalIdx];
        GpuRefittableBinaryEntry32 Left  = BinaryHierarchy[Cur.LeftOrFirst];
        GpuRefittableBinaryEntry32 Right = BinaryHierarchy[Cur.LeftOrFirst + 1u];
        Cur.BoundsMin = min(Left.BoundsMin, Right.BoundsMin);
        Cur.BoundsMax = max(Left.BoundsMax, Right.BoundsMax);
        BinaryHierarchy[InternalIdx] = Cur;
    }
}
