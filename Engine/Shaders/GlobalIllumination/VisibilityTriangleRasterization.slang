/*============================================================================================================================================
                                                VISIBILITYTRIANGLERASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Subpixel-shifted primary triangle-ID rasterizer (r32uint) aligned bit-for-bit with DeterministicRadianceIntegration camera rays

#include "GlobalIllumination/QuadratureSequence.slang"

struct RasterUniformBlock
{
    float4 CameraOrigin;  // xyz [m]
    float4 CameraForward; // xyz unit forward
    float4 CameraRight;   // xyz right * tan(fov/2) * aspect
    float4 CameraUp;      // xyz up * tan(fov/2)
    uint4  Sampling;      // x = width, y = height, z = sample index, w = first instance placement [uint4]
};

struct RasterVertexOutput
{
    float4               ClipPosition : SV_Position;
    nointerpolation uint Identifier   : TEXCOORD0;
};

[[vk::binding(0, 0)]] ConstantBuffer<RasterUniformBlock> Raster;
[[vk::binding(1, 0)]] StructuredBuffer<float4>           TrianglePositions;
[[vk::binding(2, 0)]] StructuredBuffer<uint4>            BoundsEntries;

static const float RasterNearPlane = 0.02f;  // [m]
static const float RasterFarPlane  = 500.0f; // [m]

[shader("vertex")]
RasterVertexOutput VertexEntry(uint VertexIndex : SV_VertexID, uint InstanceIndex : SV_InstanceID)
{
    uint   TriangleIndex = VertexIndex / 3u;
    uint   CornerIndex   = VertexIndex % 3u;
    float3 Local         = TrianglePositions[TriangleIndex * 3u].xyz;
    if (CornerIndex > 0u)
    {
        Local += TrianglePositions[TriangleIndex * 3u + CornerIndex].xyz;
    }

    // 📝 Rigid 2D-yaw studio bay placement: (Cosine, Sine, Tx, Tz) in row 0 of the 2-uint4 instance placement entry.
    float4 Placement = asfloat(BoundsEntries[Raster.Sampling.w + InstanceIndex * 2u]);
    float3 WorldPos  = float3(
        Placement.x * Local.x + Placement.y * Local.z + Placement.z,
        Local.y,
        -Placement.y * Local.x + Placement.x * Local.z + Placement.w);

    // 📝 Vulkan clip-space Y points downward (+Y -> bottom row), matching Ndc.y = (Py + Subpixel.y)/H * 2 - 1 in IntegrateSample.
    float3 Offset   = WorldPos - Raster.CameraOrigin.xyz;
    float  Depth    = dot(Offset, Raster.CameraForward.xyz);
    float  Across   = dot(Offset, Raster.CameraRight.xyz) / max(dot(Raster.CameraRight.xyz, Raster.CameraRight.xyz), 1.0e-12f);
    float  Upward   = dot(Offset, Raster.CameraUp.xyz) / max(dot(Raster.CameraUp.xyz, Raster.CameraUp.xyz), 1.0e-12f);
    float2 Subpixel = ScrambledQuadraturePair(Raster.Sampling.z, 0u, 0u, uint2(0u, 0u));
    float2 Shift    = float2(
        (0.5f - Subpixel.x) * 2.0f / float(max(Raster.Sampling.x, 1u)),
        (0.5f - Subpixel.y) * 2.0f / float(max(Raster.Sampling.y, 1u)));

    RasterVertexOutput Output;
    Output.ClipPosition = float4(
        Across + Shift.x * Depth,
        -Upward + Shift.y * Depth,
        (Depth - RasterNearPlane) * RasterFarPlane / (RasterFarPlane - RasterNearPlane),
        Depth);
    Output.Identifier = ((InstanceIndex << 20u) | TriangleIndex) + 1u;
    return Output;
}

[shader("fragment")]
uint FragmentEntry(RasterVertexOutput Varying) : SV_Target0
{
    return Varying.Identifier;
}
