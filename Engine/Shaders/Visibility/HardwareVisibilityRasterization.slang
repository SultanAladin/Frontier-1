/*============================================================================================================================================
                                                    HARDWAREVISIBILITYRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 >=4 px Indirect Hardware Visibility Rasterizer emitting exact 32-bit VisibilityToken32 + Reversed-Z hardware depth

module HardwareVisibilityRasterization;

struct VisibilityCameraConstants
{
    float3 CameraOrigin;
    float  NearClip;
    float3 RightAxis;
    float  FarClip;
    float3 UpAxis;
    float  TanHalfFovX;
    float3 ForwardAxis;
    float  TanHalfFovY;
    float2 JitterNdc;
    uint   InstanceOrdinal;
    uint   ClusterOrdinal;
};

struct GpuClusterTriangle
{
    uint   VertexIndex0;
    uint   VertexIndex1;
    uint   VertexIndex2;
    uint   SourceFaceIndex;
    float2 CornerUV0;
    float2 CornerUV1;
    float2 CornerUV2;
    uint   PackedNormal0;
    uint   PackedNormal1;
    uint   PackedNormal2;
    uint   PackedTangent0;
    uint   PackedMaterialTangentSign;
};

[[vk::binding(0, 0)]] ConstantBuffer<VisibilityCameraConstants> CamConsts;
[[vk::binding(1, 0)]] StructuredBuffer<float3>                  SharedPositions;
[[vk::binding(2, 0)]] StructuredBuffer<GpuClusterTriangle>      ClusterTriangles;

struct VisibilityVertexToFragment
{
    float4 ClipPosition               : SV_Position;
    nointerpolation uint VisibilityId : VISIBILITY_TOKEN;
};

[shader("vertex")]
VisibilityVertexToFragment VisibilityVertexEntry(uint VertexID : SV_VertexID)
{
    uint LocalTri = VertexID / 3u;
    uint Corner   = VertexID % 3u;

    GpuClusterTriangle Tri = ClusterTriangles[LocalTri];
    uint VIdx = (Corner == 0u) ? Tri.VertexIndex0 : ((Corner == 1u) ? Tri.VertexIndex1 : Tri.VertexIndex2);
    float3 WorldPos = SharedPositions[VIdx];

    float3 Rel   = WorldPos - CamConsts.CameraOrigin;
    float  ViewX = dot(Rel, CamConsts.RightAxis);
    float  ViewY = dot(Rel, CamConsts.UpAxis);
    float  ViewZ = max(1.0e-4, dot(Rel, CamConsts.ForwardAxis));

    float ClipX = (ViewX / CamConsts.TanHalfFovX) + CamConsts.JitterNdc.x * ViewZ;
    float ClipY = (ViewY / CamConsts.TanHalfFovY) + CamConsts.JitterNdc.y * ViewZ;
    // Reversed-Z clip Z: z=Near -> ViewZ (NDC=1), z=Far -> 0 (NDC=0)
    float ClipZ = (CamConsts.NearClip * (CamConsts.FarClip - ViewZ)) / max(1.0e-6, CamConsts.FarClip - CamConsts.NearClip);

    VisibilityVertexToFragment Out;
    Out.ClipPosition = float4(ClipX, ClipY, ClipZ, ViewZ);
    Out.VisibilityId = ((min(2047u, CamConsts.InstanceOrdinal + 1u)) << 21u) |
                       ((CamConsts.ClusterOrdinal & 0x3FFFu) << 7u) |
                       (LocalTri & 0x7Fu);
    return Out;
}

[shader("fragment")]
uint VisibilityFragmentEntry(VisibilityVertexToFragment Input) : SV_Target0
{
    return Input.VisibilityId;
}
