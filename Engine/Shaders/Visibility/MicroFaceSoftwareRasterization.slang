/*============================================================================================================================================
                                                     MICROFACESOFTWARERASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 <4 px Sub-Pixel Compute Visibility Rasterizer packing (ReversedDepthBits32 << 32 | VisibilityToken32) via 64-bit InterlockedMax

module MicroFaceSoftwareRasterization;

struct MicroRasterUniforms
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
    uint   MicroTriangleCount;
    uint   InstanceOrdinal;
};

struct GpuMicroTriangleItem
{
    float3 WorldP0;
    uint   ClusterOrdinal;
    float3 WorldP1;
    uint   LocalTriangleOrdinal;
    float3 WorldP2;
    uint   ReservedPad;
};

[[vk::binding(0, 0)]] ConstantBuffer<MicroRasterUniforms>      MicroCam;
[[vk::binding(1, 0)]] StructuredBuffer<GpuMicroTriangleItem>   MicroTriangles;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint64_t>             VisibilityBuffer64;

bool ProjectMicroVertex(float3 WorldPos, out float2 OutScreen, out float OutReversedZ)
{
    float3 Rel   = WorldPos - MicroCam.CameraOrigin;
    float  ViewZ = dot(Rel, MicroCam.ForwardAxis);
    if (ViewZ <= MicroCam.NearClip * 0.5)
    {
        OutScreen    = float2(0.0);
        OutReversedZ = 0.0;
        return false;
    }
    float ViewX = dot(Rel, MicroCam.RightAxis);
    float ViewY = dot(Rel, MicroCam.UpAxis);
    float NdcX  = (ViewX / ViewZ) / max(1.0e-5, MicroCam.TanHalfFovX);
    float NdcY  = (ViewY / ViewZ) / max(1.0e-5, MicroCam.TanHalfFovY);

    OutScreen    = float2((NdcX * 0.5 + 0.5) * float(MicroCam.ViewportWidth),
                          (0.5 - NdcY * 0.5) * float(MicroCam.ViewportHeight));
    OutReversedZ = saturate((MicroCam.NearClip * (MicroCam.FarClip - ViewZ)) /
                            max(1.0e-7, ViewZ * (MicroCam.FarClip - MicroCam.NearClip)));
    return true;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void MicroFaceComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint TriIdx = DispatchThreadID.x;
    if (TriIdx >= MicroCam.MicroTriangleCount)
    {
        return;
    }

    GpuMicroTriangleItem Item = MicroTriangles[TriIdx];
    float2 S0, S1, S2;
    float  Z0, Z1, Z2;
    if (!ProjectMicroVertex(Item.WorldP0, S0, Z0) ||
        !ProjectMicroVertex(Item.WorldP1, S1, Z1) ||
        !ProjectMicroVertex(Item.WorldP2, S2, Z2))
    {
        return;
    }

    float Area2 = (S1.x - S0.x) * (S2.y - S0.y) - (S1.y - S0.y) * (S2.x - S0.x);
    if (Area2 >= -1.0e-6)
    {
        return; // Backface or zero-area micro-triangle
    }

    int2 MinPx = max(int2(0, 0), int2(floor(min(S0, min(S1, S2)))));
    int2 MaxPx = min(int2(int(MicroCam.ViewportWidth) - 1, int(MicroCam.ViewportHeight) - 1),
                     int2(ceil(max(S0, max(S1, S2)))));

    uint Token32 = ((min(2047u, MicroCam.InstanceOrdinal + 1u)) << 21u) |
                   ((Item.ClusterOrdinal & 0x3FFFu) << 7u) |
                   (Item.LocalTriangleOrdinal & 0x7Fu);
    float InvArea2 = 1.0 / Area2;

    for (int PY = MinPx.y; PY <= MaxPx.y; ++PY)
    {
        for (int PX = MinPx.x; PX <= MaxPx.x; ++PX)
        {
            float2 Sample = float2(float(PX) + 0.5, float(PY) + 0.5);
            float W0 = (S1.x - Sample.x) * (S2.y - Sample.y) - (S1.y - Sample.y) * (S2.x - Sample.x);
            float W1 = (S2.x - Sample.x) * (S0.y - Sample.y) - (S2.y - Sample.y) * (S0.x - Sample.x);
            float W2 = (S0.x - Sample.x) * (S1.y - Sample.y) - (S0.y - Sample.y) * (S1.x - Sample.x);
            if (W0 <= 1.0e-6 && W1 <= 1.0e-6 && W2 <= 1.0e-6)
            {
                float B0   = W0 * InvArea2;
                float B1   = W1 * InvArea2;
                float B2   = 1.0 - B0 - B1;
                float RevZ = saturate(B0 * Z0 + B1 * Z1 + B2 * Z2);
                uint64_t Packed64 = (uint64_t(asuint(RevZ)) << 32) | uint64_t(Token32);
                uint LinearIdx    = uint(PY) * MicroCam.ViewportWidth + uint(PX);
                InterlockedMax(VisibilityBuffer64[LinearIdx], Packed64);
            }
        }
    }
}
