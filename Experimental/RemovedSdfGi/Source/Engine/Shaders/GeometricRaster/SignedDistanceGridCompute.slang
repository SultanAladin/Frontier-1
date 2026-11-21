/*============================================================================================================================================
                                                      SIGNEDDISTANCEGRIDCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Hardware Vulkan GPU compute shader building the dense 3D narrow-band Signed Distance Grid directly from BVH4 + triangles

struct SignedDistanceGridUniformBlock
{
    float4 GridMinSpacing;   // xyz = GridMinCorner [m], w = VoxelSpacing [m]
    uint4  GridDimsBvhCount; // xyz = GridDims [count],  w = BvhEntryCount
    float4 CutoffConfig;     // x = UpperCutoffDistance [m], yzw = Pad
};

struct GpuWideTreeEntry128
{
    float4 BranchBoundsMinX;
    float4 BranchBoundsMaxX;
    float4 BranchBoundsMinY;
    float4 BranchBoundsMaxY;
    float4 BranchBoundsMinZ;
    float4 BranchBoundsMaxZ;
    uint4  BranchReference;
    uint4  BranchPrimitiveCount;
};

struct GpuShadedTriangle96
{
    float4 Corner0Pad;
    float4 Edge01Pad;
    float4 Edge02Pad;
    float4 Normal0Pad;
    float4 Normal1Pad;
    float4 Normal2Slot;
};

[[vk::binding(0, 0)]] ConstantBuffer<SignedDistanceGridUniformBlock> GridUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<GpuWideTreeEntry128>          SceneBvhEntries;
[[vk::binding(2, 0)]] StructuredBuffer<GpuShadedTriangle96>          SceneTriangles;
[[vk::binding(3, 0)]] RWStructuredBuffer<float>                      OutSignedDistanceGrid;

static const uint EmptyBranchSentinel  = 0xFFFFFFFFu;
static const uint WideLeafBitMask      = 0x80000000u;

float EvaluatePointTriangleDistanceSq(float3 Point, float3 CornerA, float3 EdgeAB, float3 EdgeAC)
{
    float3 DiffAP = Point - CornerA;
    float  Dot1   = dot(EdgeAB, DiffAP);
    float  Dot2   = dot(EdgeAC, DiffAP);
    if (Dot1 <= 0.0 && Dot2 <= 0.0)
    {
        return dot(DiffAP, DiffAP);
    }

    float3 CornerB = CornerA + EdgeAB;
    float3 DiffBP  = Point - CornerB;
    float  Dot3    = dot(EdgeAB, DiffBP);
    float  Dot4    = dot(EdgeAC, DiffBP);
    if (Dot3 >= 0.0 && Dot4 <= Dot3)
    {
        return dot(DiffBP, DiffBP);
    }

    float EdgeVC = Dot1 * Dot4 - Dot3 * Dot2;
    if (EdgeVC <= 0.0 && Dot1 >= 0.0 && Dot3 <= 0.0)
    {
        float  WeightV = Dot1 / max(1.0e-12, Dot1 - Dot3);
        float3 Delta   = Point - (CornerA + EdgeAB * WeightV);
        return dot(Delta, Delta);
    }

    float3 CornerC = CornerA + EdgeAC;
    float3 DiffCP  = Point - CornerC;
    float  Dot5    = dot(EdgeAB, DiffCP);
    float  Dot6    = dot(EdgeAC, DiffCP);
    if (Dot6 >= 0.0 && Dot5 <= Dot6)
    {
        return dot(DiffCP, DiffCP);
    }

    float EdgeVB = Dot5 * Dot2 - Dot1 * Dot6;
    if (EdgeVB <= 0.0 && Dot2 >= 0.0 && Dot6 <= 0.0)
    {
        float  WeightW = Dot2 / max(1.0e-12, Dot2 - Dot6);
        float3 Delta   = Point - (CornerA + EdgeAC * WeightW);
        return dot(Delta, Delta);
    }

    float EdgeVA = Dot3 * Dot6 - Dot5 * Dot4;
    if (EdgeVA <= 0.0 && (Dot4 - Dot3) >= 0.0 && (Dot5 - Dot6) >= 0.0)
    {
        float  WeightW = (Dot4 - Dot3) / max(1.0e-12, (Dot4 - Dot3) + (Dot5 - Dot6));
        float3 Delta   = Point - (CornerB + (CornerC - CornerB) * WeightW);
        return dot(Delta, Delta);
    }

    float  Denom   = 1.0 / max(1.0e-12, EdgeVA + EdgeVB + EdgeVC);
    float  WeightV = EdgeVB * Denom;
    float  WeightW = EdgeVC * Denom;
    float3 Closest = CornerA + EdgeAB * WeightV + EdgeAC * WeightW;
    float3 Delta   = Point - Closest;
    return dot(Delta, Delta);
}

float4 EvaluateFourBoxDistanceSq(float3 QueryPt, GpuWideTreeEntry128 Entry)
{
    float4 DX = max(max(float4(0.0), Entry.BranchBoundsMinX - QueryPt.xxxx), QueryPt.xxxx - Entry.BranchBoundsMaxX);
    float4 DY = max(max(float4(0.0), Entry.BranchBoundsMinY - QueryPt.yyyy), QueryPt.yyyy - Entry.BranchBoundsMaxY);
    float4 DZ = max(max(float4(0.0), Entry.BranchBoundsMinZ - QueryPt.zzzz), QueryPt.zzzz - Entry.BranchBoundsMaxZ);
    float4 DistSq = DX * DX + DY * DY + DZ * DZ;
    bool4  Empty  = (Entry.BranchBoundsMinX > Entry.BranchBoundsMaxX);
    return select(Empty, float4(1.0e30), DistSq);
}

float4 IntersectFourBoxesRay(float3 Origin, float3 InvDir, float MaxT, GpuWideTreeEntry128 Entry)
{
    float4 TX0 = (Entry.BranchBoundsMinX - Origin.xxxx) * InvDir.xxxx;
    float4 TX1 = (Entry.BranchBoundsMaxX - Origin.xxxx) * InvDir.xxxx;
    float4 TY0 = (Entry.BranchBoundsMinY - Origin.yyyy) * InvDir.yyyy;
    float4 TY1 = (Entry.BranchBoundsMaxY - Origin.yyyy) * InvDir.yyyy;
    float4 TZ0 = (Entry.BranchBoundsMinZ - Origin.zzzz) * InvDir.zzzz;
    float4 TZ1 = (Entry.BranchBoundsMaxZ - Origin.zzzz) * InvDir.zzzz;

    float4 NearT = max(max(min(TX0, TX1), min(TY0, TY1)), max(min(TZ0, TZ1), float4(0.0)));
    float4 FarT  = min(min(max(TX0, TX1), max(TY0, TY1)), min(max(TZ0, TZ1), float4(MaxT)));
    return select(NearT <= FarT, NearT, float4(1.0e30));
}

bool TraceSignRayInside(float3 Origin, float3 Dir, uint EntryCount)
{
    float3 SafeDir = select(abs(Dir) > float3(1.0e-8), Dir, float3(1.0e-8));
    float3 InvDir  = 1.0 / SafeDir;

    float  BestT      = 1.0e6;
    float3 BestNormal = float3(0.0, 0.0, 0.0);
    bool   HitAny     = false;

    uint Stack[32];
    int  StackTop = 0;
    Stack[0]      = 0u;

    while (StackTop >= 0)
    {
        uint CurIdx = Stack[StackTop--];
        if (CurIdx >= EntryCount) continue;

        GpuWideTreeEntry128 Entry = SceneBvhEntries[CurIdx];
        float4 BoxT = IntersectFourBoxesRay(Origin, InvDir, BestT, Entry);

        uint  Order[4] = { 0u, 1u, 2u, 3u };
        float Dists[4] = { BoxT.x, BoxT.y, BoxT.z, BoxT.w };
        for (int A = 0; A < 3; ++A)
        {
            for (int B = A + 1; B < 4; ++B)
            {
                if (Dists[A] < Dists[B])
                {
                    float TmpD = Dists[A]; Dists[A] = Dists[B]; Dists[B] = TmpD;
                    uint  TmpI = Order[A]; Order[A] = Order[B]; Order[B] = TmpI;
                }
            }
        }

        for (int K = 0; K < 4; ++K)
        {
            if (Dists[K] > BestT) continue;
            uint Lane = Order[K];
            uint Ref  = Entry.BranchReference[Lane];
            if (Ref == EmptyBranchSentinel) continue;

            if ((Ref & WideLeafBitMask) != 0u)
            {
                uint Start = Ref & ~WideLeafBitMask;
                uint Count = Entry.BranchPrimitiveCount[Lane];
                for (uint P = 0u; P < Count; ++P)
                {
                    GpuShadedTriangle96 Tri = SceneTriangles[Start + P];
                    float3 Edge01 = Tri.Edge01Pad.xyz;
                    float3 Edge02 = Tri.Edge02Pad.xyz;
                    float3 PVec   = cross(Dir, Edge02);
                    float  Det    = dot(Edge01, PVec);
                    if (abs(Det) <= 1.0e-8) continue;

                    float  InvDet = 1.0 / Det;
                    float3 TVec   = Origin - Tri.Corner0Pad.xyz;
                    float  U      = dot(TVec, PVec) * InvDet;
                    if (U < 0.0 || U > 1.0) continue;

                    float3 QVec = cross(TVec, Edge01);
                    float  V    = dot(Dir, QVec) * InvDet;
                    if (V < 0.0 || (U + V) > 1.0) continue;

                    float HitT = dot(Edge02, QVec) * InvDet;
                    if (HitT >= 0.0 && HitT < BestT)
                    {
                        BestT      = HitT;
                        BestNormal = cross(Edge01, Edge02);
                        HitAny     = true;
                    }
                }
            }
            else if (StackTop < 30)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }

    return HitAny && (dot(BestNormal, Dir) > 0.0);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint3 Dims = GridUniforms.GridDimsBvhCount.xyz;
    if (DispatchThreadId.x >= Dims.x || DispatchThreadId.y >= Dims.y || DispatchThreadId.z >= Dims.z)
    {
        return;
    }

    uint VoxelIndex = (DispatchThreadId.z * Dims.y + DispatchThreadId.y) * Dims.x + DispatchThreadId.x;
    uint EntryCount = GridUniforms.GridDimsBvhCount.w;
    float UpperCutoff = max(0.25, GridUniforms.CutoffConfig.x);
    if (EntryCount == 0u)
    {
        OutSignedDistanceGrid[VoxelIndex] = UpperCutoff;
        return;
    }

    float3 QueryPt = GridUniforms.GridMinSpacing.xyz + float3(DispatchThreadId) * GridUniforms.GridMinSpacing.w;

    float BestDistSq = UpperCutoff * UpperCutoff;
    bool  FoundNear  = false;

    uint Stack[32];
    int  StackTop = 0;
    Stack[0]      = 0u;

    while (StackTop >= 0)
    {
        uint CurIdx = Stack[StackTop--];
        if (CurIdx >= EntryCount) continue;

        GpuWideTreeEntry128 Entry  = SceneBvhEntries[CurIdx];
        float4              BoxSq  = EvaluateFourBoxDistanceSq(QueryPt, Entry);

        uint  Order[4] = { 0u, 1u, 2u, 3u };
        float Dists[4] = { BoxSq.x, BoxSq.y, BoxSq.z, BoxSq.w };
        for (int A = 0; A < 3; ++A)
        {
            for (int B = A + 1; B < 4; ++B)
            {
                if (Dists[A] < Dists[B])
                {
                    float TmpD = Dists[A]; Dists[A] = Dists[B]; Dists[B] = TmpD;
                    uint  TmpI = Order[A]; Order[A] = Order[B]; Order[B] = TmpI;
                }
            }
        }

        for (int K = 0; K < 4; ++K)
        {
            if (Dists[K] > BestDistSq) continue;
            uint Lane = Order[K];
            uint Ref  = Entry.BranchReference[Lane];
            if (Ref == EmptyBranchSentinel) continue;

            if ((Ref & WideLeafBitMask) != 0u)
            {
                uint Start = Ref & ~WideLeafBitMask;
                uint Count = Entry.BranchPrimitiveCount[Lane];
                for (uint P = 0u; P < Count; ++P)
                {
                    GpuShadedTriangle96 Tri = SceneTriangles[Start + P];
                    float DistSq = EvaluatePointTriangleDistanceSq(
                        QueryPt,
                        Tri.Corner0Pad.xyz,
                        Tri.Edge01Pad.xyz,
                        Tri.Edge02Pad.xyz);
                    if (DistSq < BestDistSq)
                    {
                        BestDistSq = DistSq;
                        FoundNear  = true;
                    }
                }
            }
            else if (StackTop < 30)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }

    float UnsignedDist = FoundNear ? sqrt(max(0.0, BestDistSq)) : UpperCutoff;

    uint InsideVotes = 0u;
    if (TraceSignRayInside(QueryPt, float3( 0.57735027,  0.57735027,  0.57735027), EntryCount)) ++InsideVotes;
    if (TraceSignRayInside(QueryPt, float3(-0.68599434,  0.51449576, -0.51449576), EntryCount)) ++InsideVotes;
    if (TraceSignRayInside(QueryPt, float3( 0.26726124, -0.80178373,  0.53452248), EntryCount)) ++InsideVotes;

    OutSignedDistanceGrid[VoxelIndex] = (InsideVotes >= 2u) ? -UnsignedDist : UnsignedDist;
}
