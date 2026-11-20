/*============================================================================================================================================
                                                         CLUSTERVISIBILITYCULL.SLANG
============================================================================================================================================*/
// 🧩 Two-Phase GPU Cluster Visibility Culler: Frustum + Normal-Cone + Continuous LOD Cut + Hi-Z Split (>=4 px HW vs <4 px SW)

module ClusterVisibilityCull;

struct GpuClusterHeader128
{
    float3 SphereCenter;
    float  SphereRadiusMeters;
    float3 BoundsMinimum;
    uint   TriangleStartOffset;
    float3 BoundsMaximum;
    uint   PackedTriCountLevelMaterial;
    float3 NormalConeDirection;
    float  NormalConeSinCutoff;
    float  FinerSelfErrorMeters;
    float  CoarserThresholdErrorMeters;
    uint4  ReservedPaddingA;
    uint4  ReservedPaddingB;
    uint2  ReservedPaddingC;
    uint   EnclosingGroupOrdinal;
    uint   ClusterOrdinal;
};

struct GpuCullCameraUniforms
{
    float3 CameraOrigin;
    float  NearClip;
    float3 RightAxis;
    float  FarClip;
    float3 UpAxis;
    float  TanHalfFovX;
    float3 ForwardAxis;
    float  TanHalfFovY;
    uint   ViewportWidth;
    uint   ViewportHeight;
    uint   HiZMipCount;
    uint   TotalClusterCount;
    float  MaxAllowedPixelError;
    float  MicroClusterThresholdPx;
    float2 ReservedPad;
};

struct GpuVisibleClusterWorkEntry
{
    uint  InstanceOrdinal;
    uint  ClusterOrdinal;
    float ProjectedSpanPx;
    float NearestReversedZ;
};

[[vk::binding(0, 0)]] ConstantBuffer<GpuCullCameraUniforms>       CameraCull;
[[vk::binding(1, 0)]] StructuredBuffer<GpuClusterHeader128>       ClusterHeaders;
[[vk::binding(2, 0)]] Texture2D<float>                            DepthPyramidTex;
[[vk::binding(3, 0)]] SamplerState                                PointClampSampler;
[[vk::binding(4, 0)]] RWStructuredBuffer<GpuVisibleClusterWorkEntry> HardwareQueue;
[[vk::binding(5, 0)]] RWStructuredBuffer<GpuVisibleClusterWorkEntry> MicroComputeQueue;
[[vk::binding(6, 0)]] RWStructuredBuffer<uint>                    QueueCounters; // [0]=HW, [1]=SW

float EncodeReversedZ(float ViewZ, float NearZ, float FarZ)
{
    if (ViewZ <= NearZ) return 1.0;
    if (ViewZ >= FarZ) return 0.0;
    return saturate((NearZ * (FarZ - ViewZ)) / max(1.0e-7, ViewZ * (FarZ - NearZ)));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ClusterCullComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint ClusterIdx = DispatchThreadID.x;
    if (ClusterIdx >= CameraCull.TotalClusterCount)
    {
        return;
    }

    GpuClusterHeader128 Cluster = ClusterHeaders[ClusterIdx];
    float3 Rel   = Cluster.SphereCenter - CameraCull.CameraOrigin;
    float  ViewX = dot(Rel, CameraCull.RightAxis);
    float  ViewY = dot(Rel, CameraCull.UpAxis);
    float  ViewZ = dot(Rel, CameraCull.ForwardAxis);
    float  Rad   = Cluster.SphereRadiusMeters;

    if (ViewZ + Rad <= CameraCull.NearClip || ViewZ - Rad >= CameraCull.FarClip)
    {
        return;
    }

    float SecX = sqrt(1.0 + CameraCull.TanHalfFovX * CameraCull.TanHalfFovX);
    float SecY = sqrt(1.0 + CameraCull.TanHalfFovY * CameraCull.TanHalfFovY);
    if (abs(ViewX) - CameraCull.TanHalfFovX * ViewZ > Rad * SecX ||
        abs(ViewY) - CameraCull.TanHalfFovY * ViewZ > Rad * SecY)
    {
        return;
    }

    // Continuous monotonic LOD cut condition: FinerSelfError <= AllowedMeters < CoarserThresholdError
    float Dist = max(CameraCull.NearClip, length(Rel) - Rad);
    float PixelsPerRadian = float(CameraCull.ViewportHeight) / max(1.0e-5, 2.0 * CameraCull.TanHalfFovY);
    float AllowedMeters   = (CameraCull.MaxAllowedPixelError / PixelsPerRadian) * Dist;
    if (Cluster.FinerSelfErrorMeters > AllowedMeters || AllowedMeters >= Cluster.CoarserThresholdErrorMeters)
    {
        return;
    }

    // Normal-cone backface cull
    float CenterDist = length(Rel);
    if (Cluster.NormalConeSinCutoff < 0.995 && CenterDist > Rad + 1.0e-5)
    {
        float3 ViewDir = Rel / CenterDist;
        if (dot(ViewDir, Cluster.NormalConeDirection) > Cluster.NormalConeSinCutoff + Rad / CenterDist + 0.02)
        {
            return;
        }
    }

    float SafeZ    = max(CameraCull.NearClip, ViewZ);
    float SpanPx   = (2.0 * Rad / SafeZ) * PixelsPerRadian;
    float NearestZ = EncodeReversedZ(max(CameraCull.NearClip, ViewZ - Rad), CameraCull.NearClip, CameraCull.FarClip);

    if (CameraCull.HiZMipCount > 0u && (ViewZ - Rad) > CameraCull.NearClip)
    {
        int   MipIdx   = clamp(int(ceil(log2(max(1.0, SpanPx)))), 0, int(CameraCull.HiZMipCount) - 1);
        float2 UvCent  = float2((ViewX / (SafeZ * CameraCull.TanHalfFovX)) * 0.5 + 0.5,
                                0.5 - (ViewY / (SafeZ * CameraCull.TanHalfFovY)) * 0.5);
        float HorizonZ = DepthPyramidTex.SampleLevel(PointClampSampler, saturate(UvCent), float(MipIdx));
        if (NearestZ < HorizonZ - 1.0e-5)
        {
            return;
        }
    }

    GpuVisibleClusterWorkEntry Item;
    Item.InstanceOrdinal  = 0u;
    Item.ClusterOrdinal   = ClusterIdx;
    Item.ProjectedSpanPx  = SpanPx;
    Item.NearestReversedZ = NearestZ;

    if (SpanPx < CameraCull.MicroClusterThresholdPx)
    {
        uint OutSlot = 0u;
        InterlockedAdd(QueueCounters[1], 1u, OutSlot);
        MicroComputeQueue[OutSlot] = Item;
    }
    else
    {
        uint OutSlot = 0u;
        InterlockedAdd(QueueCounters[0], 1u, OutSlot);
        HardwareQueue[OutSlot] = Item;
    }
}
