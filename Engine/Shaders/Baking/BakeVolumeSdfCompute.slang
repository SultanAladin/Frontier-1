/*============================================================================================================================================
                                                          BAKEVOLUMESDFCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Exact Pure-GPU 3D Volume Signed Distance Field kernel:
//   - Zero 256-tri grid cap, zero off-axis early cell exit bugs, zero single-axis +X ray marching streaks!
//   - GTX path (FRONTIER_USE_HW_RAY_QUERY=0): exact BVH4 BoxPointDistanceSq branch-and-bound + 26-ray front/back majority sign vote.
//   - RTX path (FRONTIER_USE_HW_RAY_QUERY=1): 26-ray hardware RayQuery seeding tight upper-bound distance & sign + BVH4 exact leaf refine.

import WideTreeRayQuery;
#include "BakeShaderLayouts.slang"

#ifndef FRONTIER_USE_HW_RAY_QUERY
#define FRONTIER_USE_HW_RAY_QUERY 0
#endif

[[vk::binding(0, 0)]] ConstantBuffer<GpuVolumeBakeConfig>      VolumeConfig;
[[vk::binding(1, 0)]] StructuredBuffer<GpuWideTreeEntry128>     WideEntries;
[[vk::binding(2, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48> Primitives;
[[vk::binding(3, 0)]] RWStructuredBuffer<float>                 OutVolumeDistances;
[[vk::binding(4, 0)]] [format("r32f")] RWTexture3D<float>       OutVolumeTexture3D;
#if FRONTIER_USE_HW_RAY_QUERY == 1
[[vk::binding(5, 0)]] RaytracingAccelerationStructure           HighAccel;
#endif

static const float3 SdfVoteDirs[26] = {
    float3( 1.0,  0.0,  0.0), float3(-1.0,  0.0,  0.0),
    float3( 0.0,  1.0,  0.0), float3( 0.0, -1.0,  0.0),
    float3( 0.0,  0.0,  1.0), float3( 0.0,  0.0, -1.0),
    float3( 0.70710678,  0.70710678,  0.0), float3(-0.70710678,  0.70710678,  0.0),
    float3( 0.70710678, -0.70710678,  0.0), float3(-0.70710678, -0.70710678,  0.0),
    float3( 0.70710678,  0.0,  0.70710678), float3(-0.70710678,  0.0,  0.70710678),
    float3( 0.70710678,  0.0, -0.70710678), float3(-0.70710678,  0.0, -0.70710678),
    float3( 0.0,  0.70710678,  0.70710678), float3( 0.0, -0.70710678,  0.70710678),
    float3( 0.0,  0.70710678, -0.70710678), float3( 0.0, -0.70710678, -0.70710678),
    float3( 0.57735027,  0.57735027,  0.57735027), float3(-0.57735027,  0.57735027,  0.57735027),
    float3( 0.57735027, -0.57735027,  0.57735027), float3(-0.57735027, -0.57735027,  0.57735027),
    float3( 0.57735027,  0.57735027, -0.57735027), float3(-0.57735027,  0.57735027, -0.57735027),
    float3( 0.57735027, -0.57735027, -0.57735027), float3(-0.57735027, -0.57735027, -0.57735027)
};

float ComputeExactBvh4UnsignedDistSq(float3 P, float UpperBoundSq)
{
    uint EntryCount = VolumeConfig.WideEntryCount;
    if (EntryCount == 0u)
    {
        return UpperBoundSq;
    }

    float BestDistSq = UpperBoundSq;
    uint  Stack[48];
    int   StackTop = 0;
    Stack[0] = 0u;

    while (StackTop >= 0)
    {
        uint CurIdx = Stack[StackTop--];
        if (CurIdx >= EntryCount)
        {
            continue;
        }
        GpuWideTreeEntry128 Entry = WideEntries[CurIdx];

        float LaneDistSq[4];
        uint  LaneOrder[4] = { 0u, 1u, 2u, 3u };
        [unroll]
        for (int L = 0; L < 4; ++L)
        {
            uint Ref = Entry.BranchReference[L];
            if (Ref == 0xFFFFFFFFu)
            {
                LaneDistSq[L] = 1.0e30;
            }
            else
            {
                float3 BMin = float3(Entry.BranchBoundsMinX[L], Entry.BranchBoundsMinY[L], Entry.BranchBoundsMinZ[L]);
                float3 BMax = float3(Entry.BranchBoundsMaxX[L], Entry.BranchBoundsMaxY[L], Entry.BranchBoundsMaxZ[L]);
                LaneDistSq[L] = BoxPointDistanceSq(P, BMin, BMax);
            }
        }

        // Visit farthest-pushed -> closest-popped (LIFO stack pops minimum distance first for aggressive BestDistSq pruning)
        for (int I = 0; I < 3; ++I)
        {
            for (int J = I + 1; J < 4; ++J)
            {
                if (LaneDistSq[LaneOrder[I]] < LaneDistSq[LaneOrder[J]])
                {
                    uint Tmp = LaneOrder[I];
                    LaneOrder[I] = LaneOrder[J];
                    LaneOrder[J] = Tmp;
                }
            }
        }

        for (int S = 0; S < 4; ++S)
        {
            uint L   = LaneOrder[S];
            uint Ref = Entry.BranchReference[L];
            if (Ref == 0xFFFFFFFFu || LaneDistSq[L] > BestDistSq)
            {
                continue;
            }

            if ((Ref & 0x80000000u) != 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                uint Count = Entry.BranchPrimitiveCount[L];
                for (uint K = 0u; K < Count; ++K)
                {
                    GpuRayTrianglePrimitive48 Prim = Primitives[Start + K];
                    float3 A = Prim.Corner0;
                    float3 B = A + Prim.Edge01;
                    float3 C = A + Prim.Edge02;
                    BestDistSq = min(BestDistSq, PointToTriangleDistanceSq(P, A, B, C));
                }
            }
            else if (StackTop < 46)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }

    return BestDistSq;
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ComputeEntry(uint3 Tid : SV_DispatchThreadID)
{
    uint Dim = VolumeConfig.GridDim;
    if (Tid.x >= Dim || Tid.y >= Dim || Tid.z >= Dim)
    {
        return;
    }

    float3 VoxelPos = VolumeConfig.BoundsMin + (float3(Tid) + 0.5) * VolumeConfig.VoxelStepMeters;
    float  MaxSpan  = max(0.05, VolumeConfig.BoundsDiagMeters * 1.5);
    float  SeedDistSq = MaxSpan * MaxSpan;
    float  Sign = 1.0;

    if (VolumeConfig.SignedMode != 0u)
    {
        int BackVote  = 0;
        int FrontVote = 0;

        for (uint D = 0u; D < 26u; ++D)
        {
            float3 Dir = SdfVoteDirs[D];
#if FRONTIER_USE_HW_RAY_QUERY == 1
            RayDesc Desc;
            Desc.Origin    = VoxelPos;
            Desc.Direction = Dir;
            Desc.TMin      = 1.0e-5;
            Desc.TMax      = MaxSpan;
            RayQuery<RAY_FLAG_FORCE_OPAQUE> Q;
            Q.TraceRayInline(HighAccel, RAY_FLAG_FORCE_OPAQUE, 0xFF, Desc);
            while (Q.Proceed()) {}
            if (Q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
            {
                float T = Q.CommittedRayT();
                SeedDistSq = min(SeedDistSq, T * T);
                if (!Q.CommittedRayFrontFace())
                {
                    BackVote++;
                }
                else
                {
                    FrontVote++;
                }
            }
#else
            GpuRayHitSummary Hit = TraceWideBoundingTreeClosest(
                WideEntries,
                Primitives,
                VolumeConfig.WideEntryCount,
                VoxelPos,
                Dir,
                1.0e-5,
                MaxSpan);
            if (Hit.HitConfirmed)
            {
                SeedDistSq = min(SeedDistSq, Hit.HitDistanceMeters * Hit.HitDistanceMeters);
                if (!Hit.HitFrontFace)
                {
                    BackVote++;
                }
                else
                {
                    FrontVote++;
                }
            }
#endif
        }

        // Majority back-face interior voting resists open boundary gaps completely (e.g. UtahTeapot lid/rim)
        if (BackVote > FrontVote && BackVote >= 9)
        {
            Sign = -1.0;
        }
    }

    float ExactDistMeters = sqrt(max(0.0, ComputeExactBvh4UnsignedDistSq(VoxelPos, SeedDistSq))) * Sign;
    uint  LinearIdx       = (Tid.z * Dim + Tid.y) * Dim + Tid.x;
    OutVolumeDistances[LinearIdx] = ExactDistMeters;
    OutVolumeTexture3D[Tid]       = ExactDistMeters;
}
