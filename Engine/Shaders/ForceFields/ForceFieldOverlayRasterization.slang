/*============================================================================================================================================
                                                    FORCEFIELDOVERLAYRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 100% GPU Vulkan .slang overlay shader for analytic ForceField gizmo lines and 3D slice/volume force-vector arrows
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js RampChunk (L1077-L1122), ForceVectorVertex (L1423-L1480), ColourFragment (L1481-L1487)

import ForceModelEvaluation;

//------------------------------------------------------------------------------------------------------------------------
//                                                    GPU BUFFER LAYOUTS
//------------------------------------------------------------------------------------------------------------------------

struct ForceGizmoVertexInput
{
    float4 PositionPad; // [m] - xyz = world position, w = unused padding
    float4 Colour;      // [-] - RGBA line tint
};

struct ForceOverlayConstants
{
    float4x4 ViewProjection;  // [-] - Row-major camera view-projection matrix
    float4   GridOriginCell;  // [m] - xyz = region origin, w = cellSize
    uint4    GridExtentMode;  // [-] - xyz = grid voxel counts, w = mode (0 slice, 1 sparse volume)
    uint4    AxisSliceStride; // [-] - x = axis, y = slice, z = stride, w = rampMode (0..3)
    float4   ScaleMaxTimeCnt; // [-] - x = arrowScale, y = maxForce, z = simulationTimeSeconds, w = activeForceCount
    uint4    ChannelHeadsPad; // [-] - x = channelMaskBits, y = drawHeads (0 lines, 1 heads), z/w = reserved
};

[[vk::push_constant]] ConstantBuffer<ForceOverlayConstants> OverlayPush;

[[vk::binding(0, 0)]] StructuredBuffer<ForceModelGpuRecord> ForceRecords;
[[vk::binding(1, 0)]] StructuredBuffer<ForceGizmoVertexInput> GizmoVertices;
[[vk::binding(2, 0)]] Texture3D<float4> VelocityVolume;

struct ForceOverlayVarying
{
    float4 ClipPosition : SV_Position;
    float4 Colour       : COLOR0;
    float  PointSize    : SV_PointSize;
};

struct ForceOverlayFragmentInput
{
    float4 ClipPosition : SV_Position;
    float4 Colour       : COLOR0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  SCIENTIFIC COLOUR RAMPS
//------------------------------------------------------------------------------------------------------------------------

float3 SampleTurboRamp(float t)
{
    t = saturate(t);
    const float4 kR  = float4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
    const float4 kG  = float4(0.09140261, 2.19418839, 4.84296658, -14.18503333);
    const float4 kB  = float4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
    const float2 kR2 = float2(-152.94239396, 59.28637943);
    const float2 kG2 = float2(4.27729857, 2.82956604);
    const float2 kB2 = float2(-89.90310912, 27.34824973);
    float4 v4 = float4(1.0, t, t * t, t * t * t);
    float2 v2 = v4.zw * v4.z;
    return float3(dot(v4, kR) + dot(v2, kR2), dot(v4, kG) + dot(v2, kG2), dot(v4, kB) + dot(v2, kB2));
}

float3 SamplePlasmaRamp(float t)
{
    t = saturate(t);
    float3 c0 = float3(0.0509, 0.0298, 0.5279);
    float3 c1 = float3(2.1763, 0.2383, 0.7539);
    float3 c2 = float3(-2.6895, -7.4559, 3.1107);
    float3 c3 = float3(6.1303, 42.3461, -28.5188);
    float3 c4 = float3(-11.1074, -82.6663, 60.1398);
    float3 c5 = float3(10.0231, 71.4136, -54.0721);
    float3 c6 = float3(-3.6587, -22.9315, 18.1919);
    return c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
}

float3 SampleEngineeringRamp(float t)
{
    t = saturate(t) * 6.0;
    static const float3 stops[7] = {
        float3(0.043, 0.122, 0.227), float3(0.071, 0.318, 0.541), float3(0.102, 0.627, 0.784),
        float3(0.373, 0.816, 0.627), float3(0.898, 0.890, 0.416), float3(0.949, 0.545, 0.176),
        float3(0.851, 0.157, 0.110)
    };
    int idx = int(min(floor(t), 5.0));
    return lerp(stops[idx], stops[idx + 1], frac(t));
}

float3 SampleDivergingRamp(float t)
{
    t = saturate(t) * 6.0;
    static const float3 stops[7] = {
        float3(0.129, 0.400, 0.675), float3(0.404, 0.663, 0.812), float3(0.820, 0.898, 0.941),
        float3(0.969, 0.969, 0.969), float3(0.992, 0.859, 0.780), float3(0.937, 0.541, 0.384),
        float3(0.698, 0.094, 0.169)
    };
    int idx = int(min(floor(t), 5.0));
    return lerp(stops[idx], stops[idx + 1], frac(t));
}

float3 SampleScientificRamp(uint mode, float t)
{
    if (mode == 1u) return SampleEngineeringRamp(t);
    if (mode == 2u) return SamplePlasmaRamp(t);
    if (mode == 3u) return SampleDivergingRamp(t);
    return SampleTurboRamp(t);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              GPU GIZMO LINE VERTEX ENTRY
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
ForceOverlayVarying ForceGizmoVertexEntry(uint vertexId : SV_VulkanVertexID)
{
    ForceGizmoVertexInput v = GizmoVertices[vertexId];
    ForceOverlayVarying output;
    output.ClipPosition = mul(OverlayPush.ViewProjection, float4(v.PositionPad.xyz, 1.0));
    output.Colour       = v.Colour;
    output.PointSize    = 1.0;
    return output;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         GPU FORCE VECTOR ARROW VERTEX ENTRY
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
ForceOverlayVarying ForceVectorVertexEntry(uint vertexId : SV_VulkanVertexID)
{
    // Reference: Experimental/FluidEditor/Engine/FluidShaders.js L1423-L1480
    const uint drawHeads = OverlayPush.ChannelHeadsPad.y;
    const int vertexInArrow = (drawHeads == 1u) ? 0 : int(vertexId & 1u);
    const int arrow = (drawHeads == 1u) ? int(vertexId) : int(vertexId >> 1u);

    const int3 gridDims = int3(OverlayPush.GridExtentMode.xyz);
    const uint mode     = OverlayPush.GridExtentMode.w;
    const uint axis     = OverlayPush.AxisSliceStride.x;
    const int  slice    = int(OverlayPush.AxisSliceStride.y);
    const int  stride   = max(1, int(OverlayPush.AxisSliceStride.z));
    const uint rampMode = OverlayPush.AxisSliceStride.w;

    int3 cell = int3(0, 0, 0);
    bool valid = true;
    if (mode == 0u)
    {
        int2 planeSize = (axis == 0u) ? gridDims.yz : ((axis == 1u) ? gridDims.xz : gridDims.xy);
        int columns = (planeSize.x + stride - 1) / stride;
        int uCoord = (arrow - (arrow / columns) * columns) * stride + stride / 2;
        int vCoord = (arrow / columns) * stride + stride / 2;
        cell = (axis == 0u) ? int3(slice, uCoord, vCoord)
             : ((axis == 1u) ? int3(uCoord, slice, vCoord) : int3(uCoord, vCoord, slice));
        valid = vCoord < planeSize.y;
    }
    else
    {
        int3 counts = (gridDims + stride - 1) / stride;
        int x = arrow - (arrow / counts.x) * counts.x;
        int rest = arrow / counts.x;
        int y = rest - (rest / counts.y) * counts.y;
        int z = rest / counts.y;
        cell = int3(x, y, z) * stride + stride / 2;
        valid = z < counts.z;
    }

    const bool cellInside = all(cell >= int3(0, 0, 0)) && all(cell < gridDims);
    ForceOverlayVarying output;
    output.PointSize = 3.0;

    if (!valid || !cellInside)
    {
        output.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        output.Colour       = float4(0.0, 0.0, 0.0, 0.0);
        return output;
    }

    const float  cellSize   = OverlayPush.GridOriginCell.w;
    const float3 origin     = OverlayPush.GridOriginCell.xyz + (float3(cell) + 0.5) * cellSize;
    const float3 velocity   = VelocityVolume.Load(int4(cell, 0)).xyz;
    const uint   forceCount = uint(OverlayPush.ScaleMaxTimeCnt.w);
    const float  simTime    = OverlayPush.ScaleMaxTimeCnt.z;
    const uint   chanMask   = OverlayPush.ChannelHeadsPad.x;

    float3 force = float3(0.0, 0.0, 0.0);
    for (uint i = 0u; i < forceCount; ++i)
    {
        ForceModelGpuRecord rec = ForceRecords[i];
        uint recMask = uint(rec.TimingChannelMask.z);
        if ((recMask & chanMask) == 0u) continue;
        force += EvaluateSingleForceModel(rec, origin, velocity, simTime);
    }

    const float magnitude  = length(force);
    const float arrowScale = OverlayPush.ScaleMaxTimeCnt.x;
    const float maxForce   = max(OverlayPush.ScaleMaxTimeCnt.y, 1.0e-3);
    const float cap        = float(stride) * cellSize * 1.5;
    const float3 tip       = origin + force * arrowScale * min(1.0, cap / max(magnitude * arrowScale, 1.0e-4));
    const float3 position  = (vertexInArrow == 1 || drawHeads == 1u) ? tip : origin;

    const float t     = saturate(magnitude / maxForce);
    const float alpha = smoothstep(0.0, 0.04, t) * (0.3 + 0.7 * t);
    output.Colour       = float4(SampleScientificRamp(rampMode, t), alpha);
    output.ClipPosition = mul(OverlayPush.ViewProjection, float4(position, 1.0));
    return output;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SHARED OVERLAY FRAGMENT ENTRY
//------------------------------------------------------------------------------------------------------------------------

[shader("fragment")]
float4 ForceOverlayFragmentEntry(ForceOverlayFragmentInput input) : SV_Target0
{
    if (input.Colour.a <= 0.0)
    {
        discard;
    }
    return input.Colour;
}
