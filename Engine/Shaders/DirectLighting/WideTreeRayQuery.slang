/*============================================================================================================================================
                                                            WIDETREERAYQUERY.SLANG
============================================================================================================================================*/
// 🧩 Shared 128-byte 4-Wide BVH4 ray-box & Moller-Trumbore intersection routines for GTX compute & RTX software/hybrid fallback

module WideTreeRayQuery;

public struct GpuWideTreeEntry128
{
    public float4 BranchBoundsMinX;
    public float4 BranchBoundsMaxX;
    public float4 BranchBoundsMinY;
    public float4 BranchBoundsMaxY;
    public float4 BranchBoundsMinZ;
    public float4 BranchBoundsMaxZ;
    public uint4  BranchReference;
    public uint4  BranchPrimitiveCount;
};

public struct GpuRayTrianglePrimitive48
{
    public float3 Corner0;
    public uint   SourceFaceIndex;
    public float3 Edge01;
    public uint   PackedMaterialOpaque;
    public float3 Edge02;
    public uint   PrimitiveOrdinal;
};

public struct GpuRayHitSummary
{
    public bool   HitConfirmed;
    public float  HitDistanceMeters;
    public float2 BarycentricsUV;
    public uint   MaterialOrdinal;
    public float3 GeometricNormal;
    public uint   SourceFaceIndex;
    public bool   HitFrontFace;
};

public float IntersectTriangleMollerTrumbore(
    float3         Origin,
    float3         Direction,
    float          MinT,
    float          MaxT,
    GpuRayTrianglePrimitive48 Prim,
    out float2     OutUV)
{
    OutUV = float2(0.0);
    float3 PVec = cross(Direction, Prim.Edge02);
    float  Det  = dot(Prim.Edge01, PVec);
    if (abs(Det) <= 1.0e-8) return -1.0;

    float  InvDet = 1.0 / Det;
    float3 TVec   = Origin - Prim.Corner0;
    float  U      = dot(TVec, PVec) * InvDet;
    if (U < 0.0 || U > 1.0) return -1.0;

    float3 QVec = cross(TVec, Prim.Edge01);
    float  V    = dot(Direction, QVec) * InvDet;
    if (V < 0.0 || (U + V) > 1.0) return -1.0;

    float HitT = dot(Prim.Edge02, QVec) * InvDet;
    if (HitT < MinT || HitT > MaxT) return -1.0;

    OutUV = float2(U, V);
    return HitT;
}

public float4 IntersectFourBoxesSlab(
    float3             Origin,
    float3             InvDir,
    float              MinT,
    float              MaxT,
    GpuWideTreeEntry128 Entry)
{
    float4 TX0 = (Entry.BranchBoundsMinX - Origin.xxxx) * InvDir.xxxx;
    float4 TX1 = (Entry.BranchBoundsMaxX - Origin.xxxx) * InvDir.xxxx;
    float4 TY0 = (Entry.BranchBoundsMinY - Origin.yyyy) * InvDir.yyyy;
    float4 TY1 = (Entry.BranchBoundsMaxY - Origin.yyyy) * InvDir.yyyy;
    float4 TZ0 = (Entry.BranchBoundsMinZ - Origin.zzzz) * InvDir.zzzz;
    float4 TZ1 = (Entry.BranchBoundsMaxZ - Origin.zzzz) * InvDir.zzzz;

    float4 NearT = max(max(min(TX0, TX1), min(TY0, TY1)), max(min(TZ0, TZ1), float4(MinT)));
    float4 FarT  = min(min(max(TX0, TX1), max(TY0, TY1)), min(max(TZ0, TZ1), float4(MaxT)));

    return select(NearT <= FarT, NearT, float4(1.0e30));
}

public GpuRayHitSummary TraceWideBoundingTreeClosest(
    StructuredBuffer<GpuWideTreeEntry128>        WideEntries,
    StructuredBuffer<GpuRayTrianglePrimitive48>  Primitives,
    uint                                         EntryCount,
    float3                                       Origin,
    float3                                       UnitDir,
    float                                        MinT,
    float                                        MaxT)
{
    GpuRayHitSummary Result;
    Result.HitConfirmed      = false;
    Result.HitDistanceMeters = MaxT;
    Result.BarycentricsUV    = float2(0.0);
    Result.MaterialOrdinal   = 0u;
    Result.GeometricNormal   = float3(0.0, 0.0, 1.0);
    Result.SourceFaceIndex   = 0xFFFFFFFFu;
    Result.HitFrontFace      = true;

    if (EntryCount == 0u) return Result;

    float3 InvDir = 1.0 / select(abs(UnitDir) > float3(1.0e-8), UnitDir, float3(1.0e-8));
    uint   Stack[32];
    int    StackTop = 0;
    Stack[0] = 0u;

    while (StackTop >= 0)
    {
        uint CurrentIdx = Stack[StackTop--];
        if (CurrentIdx >= EntryCount) continue;

        GpuWideTreeEntry128 Entry = WideEntries[CurrentIdx];
        float4 BoxHits = IntersectFourBoxesSlab(Origin, InvDir, MinT, Result.HitDistanceMeters, Entry);

        [unroll]
        for (int Lane = 0; Lane < 4; ++Lane)
        {
            uint Ref = Entry.BranchReference[Lane];
            if (Ref == 0xFFFFFFFFu || BoxHits[Lane] > Result.HitDistanceMeters) continue;

            if ((Ref & 0x80000000u) != 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                uint Count = Entry.BranchPrimitiveCount[Lane];
                for (uint PIdx = 0u; PIdx < Count; ++PIdx)
                {
                    GpuRayTrianglePrimitive48 Prim = Primitives[Start + PIdx];
                    if ((Prim.PackedMaterialOpaque >> 16u) == 0u) continue;
                    float2 HitUV;
                    float HitT = IntersectTriangleMollerTrumbore(Origin, UnitDir, MinT, Result.HitDistanceMeters, Prim, HitUV);
                    if (HitT > 0.0)
                    {
                        float3 RawCross = cross(Prim.Edge01, Prim.Edge02);
                        float  CrossLen = length(RawCross);
                        float3 GeomN    = (CrossLen > 1.0e-12) ? (RawCross / CrossLen) : float3(0.0, 0.0, 1.0);
                        Result.HitConfirmed      = true;
                        Result.HitDistanceMeters = HitT;
                        Result.BarycentricsUV    = HitUV;
                        Result.MaterialOrdinal   = Prim.PackedMaterialOpaque & 0xFFFFu;
                        Result.GeometricNormal   = GeomN;
                        Result.SourceFaceIndex   = Prim.SourceFaceIndex;
                        Result.HitFrontFace      = (dot(UnitDir, GeomN) < 0.0);
                    }
                }
            }
            else if (StackTop < 30)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }

    return Result;
}

public bool TraceWideBoundingTreeAnyHit(
    StructuredBuffer<GpuWideTreeEntry128>        WideEntries,
    StructuredBuffer<GpuRayTrianglePrimitive48>  Primitives,
    uint                                         EntryCount,
    float3                                       Origin,
    float3                                       UnitDir,
    float                                        MinT,
    float                                        MaxT)
{
    if (EntryCount == 0u) return false;

    float3 InvDir = 1.0 / select(abs(UnitDir) > float3(1.0e-8), UnitDir, float3(1.0e-8));
    uint   Stack[32];
    int    StackTop = 0;
    Stack[0] = 0u;

    while (StackTop >= 0)
    {
        uint CurrentIdx = Stack[StackTop--];
        if (CurrentIdx >= EntryCount) continue;

        GpuWideTreeEntry128 Entry = WideEntries[CurrentIdx];
        float4 BoxHits = IntersectFourBoxesSlab(Origin, InvDir, MinT, MaxT, Entry);

        [unroll]
        for (int Lane = 0; Lane < 4; ++Lane)
        {
            uint Ref = Entry.BranchReference[Lane];
            if (Ref == 0xFFFFFFFFu || BoxHits[Lane] > MaxT) continue;

            if ((Ref & 0x80000000u) != 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                uint Count = Entry.BranchPrimitiveCount[Lane];
                for (uint PIdx = 0u; PIdx < Count; ++PIdx)
                {
                    GpuRayTrianglePrimitive48 Prim = Primitives[Start + PIdx];
                    if ((Prim.PackedMaterialOpaque >> 16u) == 0u) continue;
                    float2 HitUV;
                    float HitT = IntersectTriangleMollerTrumbore(Origin, UnitDir, MinT, MaxT, Prim, HitUV);
                    if (HitT > 0.0)
                    {
                        return true;
                    }
                }
            }
            else if (StackTop < 30)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  64-BYTE 8-BIT QUANTIZED 4-WIDE BVH4 RAY QUERY
//------------------------------------------------------------------------------------------------------------------------

public struct GpuCompressedWideEntry64
{
    public float OriginX;
    public float OriginY;
    public float OriginZ;
    public uint  PackedExponents;
    public uint  QuantizedBounds0;
    public uint  QuantizedBounds1;
    public uint  QuantizedBounds2;
    public uint  QuantizedBounds3;
    public uint  QuantizedBounds4;
    public uint  QuantizedBounds5;
    public uint  PackedCounts01;
    public uint  PackedCounts23;
    public uint  SubtreeLink0;
    public uint  SubtreeLink1;
    public uint  SubtreeLink2;
    public uint  SubtreeLink3;
};

public void UnpackCompressedWideBounds(
    GpuCompressedWideEntry64 Entry,
    out uint                OutActiveSlots,
    out float4              OutMinX,
    out float4              OutMaxX,
    out float4              OutMinY,
    out float4              OutMaxY,
    out float4              OutMinZ,
    out float4              OutMaxZ,
    out uint4               OutCounts,
    out uint4               OutLinks)
{
    OutActiveSlots = (Entry.PackedExponents >> 24u) & 0xFFu;
    int ExpX = int(Entry.PackedExponents & 0xFFu) - 127;
    int ExpY = int((Entry.PackedExponents >> 8u) & 0xFFu) - 127;
    int ExpZ = int((Entry.PackedExponents >> 16u) & 0xFFu) - 127;
    float ScaleX = ldexp(1.0, ExpX);
    float ScaleY = ldexp(1.0, ExpY);
    float ScaleZ = ldexp(1.0, ExpZ);

    uint4 Shifts = uint4(0u, 8u, 16u, 24u);
    OutMinX = float4(Entry.OriginX) + float4((uint4(Entry.QuantizedBounds0) >> Shifts) & 0xFFu) * ScaleX;
    OutMaxX = float4(Entry.OriginX) + float4((uint4(Entry.QuantizedBounds1) >> Shifts) & 0xFFu) * ScaleX;
    OutMinY = float4(Entry.OriginY) + float4((uint4(Entry.QuantizedBounds2) >> Shifts) & 0xFFu) * ScaleY;
    OutMaxY = float4(Entry.OriginY) + float4((uint4(Entry.QuantizedBounds3) >> Shifts) & 0xFFu) * ScaleY;
    OutMinZ = float4(Entry.OriginZ) + float4((uint4(Entry.QuantizedBounds4) >> Shifts) & 0xFFu) * ScaleZ;
    OutMaxZ = float4(Entry.OriginZ) + float4((uint4(Entry.QuantizedBounds5) >> Shifts) & 0xFFu) * ScaleZ;

    OutCounts = uint4(
        Entry.PackedCounts01 & 0xFFFFu,
        (Entry.PackedCounts01 >> 16u) & 0xFFFFu,
        Entry.PackedCounts23 & 0xFFFFu,
        (Entry.PackedCounts23 >> 16u) & 0xFFFFu);
    OutLinks = uint4(Entry.SubtreeLink0, Entry.SubtreeLink1, Entry.SubtreeLink2, Entry.SubtreeLink3);
}

public bool TraceCompressedWideTreeAnyHit(
    StructuredBuffer<GpuCompressedWideEntry64>    WideEntries,
    StructuredBuffer<GpuRayTrianglePrimitive48>  Primitives,
    uint                                         EntryCount,
    float3                                       Origin,
    float3                                       UnitDir,
    float                                        MinT,
    float                                        MaxT)
{
    if (EntryCount == 0u) return false;

    float3 InvDir = 1.0 / select(abs(UnitDir) > float3(1.0e-8), UnitDir, float3(1.0e-8));
    uint2  Stack[64];
    int    StackTop = 0;
    Stack[0] = uint2(0u, 0u);

    while (StackTop >= 0)
    {
        uint2 Cur = Stack[StackTop--];
        if (Cur.y > 0u)
        {
            for (uint K = 0u; K < Cur.y; ++K)
            {
                GpuRayTrianglePrimitive48 Prim = Primitives[Cur.x + K];
                if ((Prim.PackedMaterialOpaque >> 16u) == 0u) continue;
                float2 HitUV;
                if (IntersectTriangleMollerTrumbore(Origin, UnitDir, MinT, MaxT, Prim, HitUV) > 0.0)
                {
                    return true;
                }
            }
            continue;
        }
        if (Cur.x >= EntryCount) continue;

        uint   ActiveSlots;
        float4 MinX, MaxX, MinY, MaxY, MinZ, MaxZ;
        uint4  Counts, Links;
        UnpackCompressedWideBounds(WideEntries[Cur.x], ActiveSlots, MinX, MaxX, MinY, MaxY, MinZ, MaxZ, Counts, Links);

        float4 TX0 = (MinX - Origin.xxxx) * InvDir.xxxx;
        float4 TX1 = (MaxX - Origin.xxxx) * InvDir.xxxx;
        float4 TY0 = (MinY - Origin.yyyy) * InvDir.yyyy;
        float4 TY1 = (MaxY - Origin.yyyy) * InvDir.yyyy;
        float4 TZ0 = (MinZ - Origin.zzzz) * InvDir.zzzz;
        float4 TZ1 = (MaxZ - Origin.zzzz) * InvDir.zzzz;
        float4 NearT = max(max(min(TX0, TX1), min(TY0, TY1)), max(min(TZ0, TZ1), float4(MinT)));
        float4 FarT  = min(min(max(TX0, TX1), max(TY0, TY1)), min(max(TZ0, TZ1), float4(MaxT)));

        [unroll]
        for (uint S = 0u; S < 4u; ++S)
        {
            if (S < ActiveSlots && Links[S] != 0xFFFFFFFFu && NearT[S] <= FarT[S] && StackTop < 62)
            {
                Stack[++StackTop] = uint2(Links[S], Counts[S]);
            }
        }
    }
    return false;
}

