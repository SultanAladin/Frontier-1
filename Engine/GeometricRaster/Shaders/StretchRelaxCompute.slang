//============================================================================================================================================
//                                                           STRETCHRELAXCOMPUTE.SLANG
//============================================================================================================================================
// Parallel Jacobi stretch-guided UV relaxation kernel preserving pinned vertices and seam boundaries on VRAM

struct RelaxCornerRecord
{
    float2 CurrentUV;        // Current corner UV
    float2 NeighborAvgUV;    // Area-weighted 1-ring interior UV centroid
    float2 IsometricTargetUV;// Local 3D-edge-length isometric target UV
    uint   PinnedOrSeamLock; // 1 = Pinned or seam border (locked or tangential-only), 0 = Free interior corner
    float  DampingWeight;    // Relaxation step weight [0..1]
};

struct RelaxDispatchParams
{
    uint  CornerCount;
    float StepScale;
    uint  PreserveSeams;
    uint  ReservedPad;
};

[[vk::binding(0, 0)]] ConstantBuffer<RelaxDispatchParams>   Params;
[[vk::binding(1, 0)]] StructuredBuffer<RelaxCornerRecord>   InputCorners;
[[vk::binding(2, 0)]] RWStructuredBuffer<float2>            OutputCornerUVs;

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Idx = DispatchThreadID.x;
    if (Idx >= Params.CornerCount)
    {
        return;
    }

    RelaxCornerRecord Rec = InputCorners[Idx];
    if ((Rec.PinnedOrSeamLock & 1u) != 0u)
    {
        OutputCornerUVs[Idx] = Rec.CurrentUV;
        return;
    }

    if ((Rec.PinnedOrSeamLock & 2u) != 0u && Params.PreserveSeams != 0u)
    {
        float2 GentleTarget  = lerp(Rec.CurrentUV, Rec.IsometricTargetUV, 0.25);
        OutputCornerUVs[Idx] = lerp(Rec.CurrentUV, GentleTarget, saturate(Params.StepScale * Rec.DampingWeight));
        return;
    }

    float2 BlendedTarget = lerp(Rec.NeighborAvgUV, Rec.IsometricTargetUV, 0.55);
    OutputCornerUVs[Idx] = lerp(Rec.CurrentUV, BlendedTarget, saturate(Params.StepScale * Rec.DampingWeight));
}
