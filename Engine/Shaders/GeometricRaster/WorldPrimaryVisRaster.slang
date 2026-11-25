/*============================================================================================================================================
                                                        WORLDPRIMARYVISRASTER.SLANG
============================================================================================================================================*/
// 🧩 Hardware Vulkan graphics rasterization VisBuffer pass (vertex-pulls ShadedTriangles + ActiveClusterMask, writes R32_UINT TriIdx+1 + Reversed-Z D32)

struct GpuShadedTriangle96
{
    float4 Corner0Pad;
    float4 Edge01Pad;
    float4 Edge02Pad;
    float4 Normal0Pad;
    float4 Normal1Pad;
    float4 Normal2Slot;
};

struct VisRasterPushConstants
{
    float4 CamOriginTanX;   // xyz = CameraOrigin,  w = TanHalfFovX
    float4 CamRightTanY;    // xyz = CameraRight,   w = TanHalfFovY
    float4 CamUpNearZ;      // xyz = CameraUp,      w = NearZ (0.08)
    float4 CamForwardFarZ;  // xyz = CameraForward, w = FarZ  (PrimaryMaxT)
    uint4  TriangleMeta;    // x = TriangleCount, y = ClusterSlotCount (0 => ignore mask), z = 0, w = 0
};

[[vk::push_constant]]
ConstantBuffer<VisRasterPushConstants> VisPush;

[[vk::binding(0, 0)]] StructuredBuffer<GpuShadedTriangle96> SceneTriangles;
[[vk::binding(1, 0)]] StructuredBuffer<uint>                ActiveClusterBitmask;

struct VisVertexOut
{
    float4             ClipPos    : SV_Position;
    nointerpolation uint TriIdPlus1 : TEXCOORD0;
};

[shader("vertex")]
VisVertexOut PrimaryVisVertexEntry(uint VertexID : SV_VertexID)
{
    VisVertexOut Out;
    Out.ClipPos    = float4(0.0, 0.0, -1.0, 1.0);
    Out.TriIdPlus1 = 0u;

    uint TriIdx = VertexID / 3u;
    if (TriIdx >= VisPush.TriangleMeta.x)
    {
        return Out;
    }

    GpuShadedTriangle96 Tri = SceneTriangles[TriIdx];

    uint ClusterSlotCount = VisPush.TriangleMeta.y;
    if (ClusterSlotCount > 0u)
    {
        uint ClusterOrd = uint(Tri.Edge01Pad.w + 0.5);
        if (ClusterOrd < ClusterSlotCount)
        {
            uint WordIdx = ClusterOrd >> 5u;
            uint BitMask = 1u << (ClusterOrd & 31u);
            if ((ActiveClusterBitmask[WordIdx] & BitMask) == 0u)
            {
                return Out;
            }
        }
    }

    uint   Corner   = VertexID - TriIdx * 3u;
    float3 WorldPos = Tri.Corner0Pad.xyz;
    if (Corner == 1u)
    {
        WorldPos += Tri.Edge01Pad.xyz;
    }
    else if (Corner == 2u)
    {
        WorldPos += Tri.Edge02Pad.xyz;
    }

    float3 Rel   = WorldPos - VisPush.CamOriginTanX.xyz;
    float  Vx    = dot(Rel, VisPush.CamRightTanY.xyz);
    float  Vy    = dot(Rel, VisPush.CamUpNearZ.xyz);
    float  Vz    = dot(Rel, VisPush.CamForwardFarZ.xyz);
    float  TanX  = max(1.0e-5, VisPush.CamOriginTanX.w);
    float  TanY  = max(1.0e-5, VisPush.CamRightTanY.w);
    float  ZNear = max(1.0e-3, VisPush.CamUpNearZ.w);
    float  ZFar  = max(ZNear + 1.0, VisPush.CamForwardFarZ.w);

    // Vulkan clip-space with Reversed-Z [1 at ZNear, 0 at ZFar] matching ComputeEntry primary rays pixel-for-pixel
    float ClipX =  Vx / TanX;
    float ClipY = -Vy / TanY;
    float ClipZ = (ZNear * (ZFar - Vz)) / (ZFar - ZNear);
    float ClipW = Vz;

    Out.ClipPos    = float4(ClipX, ClipY, ClipZ, ClipW);
    Out.TriIdPlus1 = TriIdx + 1u;
    return Out;
}

[shader("fragment")]
uint PrimaryVisFragmentEntry(VisVertexOut In) : SV_Target0
{
    return In.TriIdPlus1;
}
