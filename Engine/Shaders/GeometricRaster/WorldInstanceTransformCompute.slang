/*============================================================================================================================================
                                                    WORLDINSTANCETRANSFORMCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Pure GPU dirty-instance BVH4 + triangle world-space transform shader (zero CPU flattening / zero PCIe geometry re-upload)

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
    float4 Corner0Pad;  // xyz = Corner0, w = PackedMeta
    float4 Edge01Pad;   // xyz = Edge01,  w = GlobalClusterOrdinal
    float4 Edge02Pad;   // xyz = Edge02,  w = SelfErrorMeters
    float4 Normal0Pad;  // xyz = Normal0, w = PackedUV01
    float4 Normal1Pad;  // xyz = Normal1, w = PackedUV2
    float4 Normal2Slot; // xyz = Normal2, w = MaterialSlot
};

struct GpuInstanceTransformUniform
{
    float4 TranslationMaxScale; // xyz = WorldTranslation, w = MaxScaleAbs
    float4 ScaleCosYaw;         // xyz = ScaleXYZ,         w = CosYaw
    float4 InvScaleSinYaw;      // xyz = InvScaleXYZ,      w = SinYaw
    uint4  RangeMaterialFlags;  // x = FirstOffset, y = ElementCount, z = InstanceMaterialSlot, w = Flags (bit0=HasYaw, bit1=NonUni)
};

[[vk::binding(0, 0)]] ConstantBuffer<GpuInstanceTransformUniform> InstanceTransform;
[[vk::binding(1, 0)]] StructuredBuffer<GpuWideTreeEntry128>       LocalBvhEntries;
[[vk::binding(2, 0)]] RWStructuredBuffer<GpuWideTreeEntry128>     WorldBvhEntries;
[[vk::binding(3, 0)]] StructuredBuffer<GpuShadedTriangle96>       LocalTriangles;
[[vk::binding(4, 0)]] RWStructuredBuffer<GpuShadedTriangle96>     WorldTriangles;

static const uint EmptyBranchRef = 0xFFFFFFFFu;

float3 RotateZVec(float3 V, float CosZ, float SinZ)
{
    return float3(V.x * CosZ - V.y * SinZ, V.x * SinZ + V.y * CosZ, V.z);
}

void TransformAabbWorldGpu(
    float3 LMin,
    float3 LMax,
    float3 Scale,
    float  CosZ,
    float  SinZ,
    float3 Translation,
    out float3 WMin,
    out float3 WMax)
{
    float3 SMin = LMin * Scale;
    float3 SMax = LMax * Scale;
    float3 Center = 0.5 * (SMin + SMax);
    float3 Extent = 0.5 * (SMax - SMin);
    float  AbsCos = abs(CosZ);
    float  AbsSin = abs(SinZ);
    float3 RotCenter = float3(
        Center.x * CosZ - Center.y * SinZ,
        Center.x * SinZ + Center.y * CosZ,
        Center.z);
    float3 RotExtent = float3(
        Extent.x * AbsCos + Extent.y * AbsSin,
        Extent.x * AbsSin + Extent.y * AbsCos,
        Extent.z);
    WMin = RotCenter - RotExtent + Translation;
    WMax = RotCenter + RotExtent + Translation;
}

float3 TransformNormalGpu(float3 N, float3 InvScale, float CosZ, float SinZ, bool NonUni, bool HasYaw)
{
    float3 Res = N;
    if (NonUni)
    {
        float3 Scaled = N * InvScale;
        float  LenSq  = dot(Scaled, Scaled);
        Res = (LenSq > 1.0e-12) ? (Scaled * rsqrt(LenSq)) : float3(0.0, 0.0, 1.0);
    }
    if (HasYaw)
    {
        Res = RotateZVec(Res, CosZ, SinZ);
    }
    return Res;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void TransformBvhNodesEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint LocalIdx = DispatchThreadID.x;
    uint Count    = InstanceTransform.RangeMaterialFlags.y;
    if (LocalIdx >= Count)
    {
        return;
    }

    uint NodeIdx = InstanceTransform.RangeMaterialFlags.x + LocalIdx;
    GpuWideTreeEntry128 Node = LocalBvhEntries[NodeIdx];

    float3 Translation = InstanceTransform.TranslationMaxScale.xyz;
    float3 Scale       = InstanceTransform.ScaleCosYaw.xyz;
    float  CosZ        = InstanceTransform.ScaleCosYaw.w;
    float  SinZ        = InstanceTransform.InvScaleSinYaw.w;

    float4 OutMinX = Node.BranchBoundsMinX;
    float4 OutMaxX = Node.BranchBoundsMaxX;
    float4 OutMinY = Node.BranchBoundsMinY;
    float4 OutMaxY = Node.BranchBoundsMaxY;
    float4 OutMinZ = Node.BranchBoundsMinZ;
    float4 OutMaxZ = Node.BranchBoundsMaxZ;

    [unroll]
    for (uint Lane = 0u; Lane < 4u; ++Lane)
    {
        if (Node.BranchReference[Lane] == EmptyBranchRef)
        {
            continue;
        }
        float3 LMin = float3(Node.BranchBoundsMinX[Lane], Node.BranchBoundsMinY[Lane], Node.BranchBoundsMinZ[Lane]);
        float3 LMax = float3(Node.BranchBoundsMaxX[Lane], Node.BranchBoundsMaxY[Lane], Node.BranchBoundsMaxZ[Lane]);
        float3 WMin, WMax;
        TransformAabbWorldGpu(LMin, LMax, Scale, CosZ, SinZ, Translation, WMin, WMax);
        OutMinX[Lane] = WMin.x;
        OutMaxX[Lane] = WMax.x;
        OutMinY[Lane] = WMin.y;
        OutMaxY[Lane] = WMax.y;
        OutMinZ[Lane] = WMin.z;
        OutMaxZ[Lane] = WMax.z;
    }

    Node.BranchBoundsMinX = OutMinX;
    Node.BranchBoundsMaxX = OutMaxX;
    Node.BranchBoundsMinY = OutMinY;
    Node.BranchBoundsMaxY = OutMaxY;
    Node.BranchBoundsMinZ = OutMinZ;
    Node.BranchBoundsMaxZ = OutMaxZ;
    WorldBvhEntries[NodeIdx] = Node;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void TransformTrianglesEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint LocalIdx = DispatchThreadID.x;
    uint Count    = InstanceTransform.RangeMaterialFlags.y;
    if (LocalIdx >= Count)
    {
        return;
    }

    uint TriIdx = InstanceTransform.RangeMaterialFlags.x + LocalIdx;
    GpuShadedTriangle96 Tri = LocalTriangles[TriIdx];

    float3 Translation = InstanceTransform.TranslationMaxScale.xyz;
    float  MaxScaleAbs = InstanceTransform.TranslationMaxScale.w;
    float3 Scale       = InstanceTransform.ScaleCosYaw.xyz;
    float  CosZ        = InstanceTransform.ScaleCosYaw.w;
    float3 InvScale    = InstanceTransform.InvScaleSinYaw.xyz;
    float  SinZ        = InstanceTransform.InvScaleSinYaw.w;
    uint   InstMat     = InstanceTransform.RangeMaterialFlags.z;
    uint   Flags       = InstanceTransform.RangeMaterialFlags.w;
    bool   HasYaw      = (Flags & 1u) != 0u;
    bool   NonUni      = (Flags & 2u) != 0u;

    float3 LC0  = Tri.Corner0Pad.xyz * Scale;
    float3 LE01 = Tri.Edge01Pad.xyz  * Scale;
    float3 LE02 = Tri.Edge02Pad.xyz  * Scale;

    float3 WC0  = Translation + (HasYaw ? RotateZVec(LC0,  CosZ, SinZ) : LC0);
    float3 WE01 = HasYaw ? RotateZVec(LE01, CosZ, SinZ) : LE01;
    float3 WE02 = HasYaw ? RotateZVec(LE02, CosZ, SinZ) : LE02;

    float3 WN0 = TransformNormalGpu(Tri.Normal0Pad.xyz,  InvScale, CosZ, SinZ, NonUni, HasYaw);
    float3 WN1 = TransformNormalGpu(Tri.Normal1Pad.xyz,  InvScale, CosZ, SinZ, NonUni, HasYaw);
    float3 WN2 = TransformNormalGpu(Tri.Normal2Slot.xyz, InvScale, CosZ, SinZ, NonUni, HasYaw);

    uint LocalMat = uint(Tri.Normal2Slot.w + 0.5);
    uint FinalMat = (LocalMat != 0u) ? LocalMat : InstMat;

    Tri.Corner0Pad.xyz  = WC0;
    Tri.Edge01Pad.xyz   = WE01;
    Tri.Edge02Pad.xyz   = WE02;
    Tri.Edge02Pad.w     = Tri.Edge02Pad.w * MaxScaleAbs;
    Tri.Normal0Pad.xyz  = WN0;
    Tri.Normal1Pad.xyz  = WN1;
    Tri.Normal2Slot.xyz = WN2;
    Tri.Normal2Slot.w   = float(FinalMat);

    WorldTriangles[TriIdx] = Tri;
}
