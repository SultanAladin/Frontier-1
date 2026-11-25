/*============================================================================================================================================
                                                    FLUIDOVERLAYRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Stage D (Slice S18) 1:1 Fluid diagnostic rasterization shaders:
//    Velocity Vector Arrows, Scalar Heat Slice (7 channels), Tracer Points/Trails, Debris Sparks/Rubble/Paper, and Gizmo Lines
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L1077-L1123, L1385-L1588, L1678-L1755

import FluidVolumeAccess;
import ForceFieldNoise;
import ForceModelEvaluation;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCIENTIFIC RAMPS (L1077-L1123)
//------------------------------------------------------------------------------------------------------------------------

float3 EvaluateTurboRamp(float t)
{
    t = clamp(t, 0.0, 1.0);
    const float4 kR  = float4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
    const float4 kG  = float4(0.09140261, 2.19418839,   4.84296658, -14.18503333);
    const float4 kB  = float4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
    const float2 kR2 = float2(-152.94239396, 59.28637943);
    const float2 kG2 = float2(   4.27729857,  2.82956604);
    const float2 kB2 = float2( -89.90310912, 27.34824973);
    float4 v4 = float4(1.0, t, t * t, t * t * t);
    float2 v2 = v4.zw * v4.z;
    return float3(dot(v4, kR) + dot(v2, kR2), dot(v4, kG) + dot(v2, kG2), dot(v4, kB) + dot(v2, kB2));
}

float3 EvaluatePlasmaRamp(float t)
{
    t = clamp(t, 0.0, 1.0);
    float3 c0 = float3( 0.0509,   0.0298,  0.5279);
    float3 c1 = float3( 2.1763,   0.2383,  0.7539);
    float3 c2 = float3(-2.6895,  -7.4559,  3.1107);
    float3 c3 = float3( 6.1303,  42.3461, -28.5188);
    float3 c4 = float3(-11.1074, -82.6663, 60.1398);
    float3 c5 = float3( 10.0231,  71.4136, -54.0721);
    float3 c6 = float3( -3.6587, -22.9315,  18.1919);
    return c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
}

float3 EvaluateEngineeringRamp(float t)
{
    t = clamp(t, 0.0, 1.0) * 6.0;
    float3 stops[7] = {
        float3(0.043, 0.122, 0.227), float3(0.071, 0.318, 0.541), float3(0.102, 0.627, 0.784),
        float3(0.373, 0.816, 0.627), float3(0.898, 0.890, 0.416), float3(0.949, 0.545, 0.176),
        float3(0.851, 0.157, 0.110)
    };
    int i = int(min(floor(t), 5.0));
    return lerp(stops[i], stops[i + 1], frac(t));
}

float3 EvaluateDivergingRamp(float t)
{
    t = clamp(t, 0.0, 1.0) * 6.0;
    float3 stops[7] = {
        float3(0.129, 0.400, 0.675), float3(0.404, 0.663, 0.812), float3(0.820, 0.898, 0.941),
        float3(0.969, 0.969, 0.969), float3(0.992, 0.859, 0.780), float3(0.937, 0.541, 0.384),
        float3(0.698, 0.094, 0.169)
    };
    int i = int(min(floor(t), 5.0));
    return lerp(stops[i], stops[i + 1], frac(t));
}

float3 EvaluateScientificRamp(int mode, float t)
{
    if (mode == 1) return EvaluateEngineeringRamp(t);
    if (mode == 2) return EvaluatePlasmaRamp(t);
    if (mode == 3) return EvaluateDivergingRamp(t);
    return EvaluateTurboRamp(t);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    UNIFORMS & BINDINGS
//------------------------------------------------------------------------------------------------------------------------

public struct FluidOverlayUniformBlock
{
    public FluidVolumeUniformBlock Volume;        // 160 B
    public float4x4                ViewProjection;//  64 B
    public float4                  Corner;        // xyz = slice corner (m), w = range
    public float4                  EdgeU;         // xyz = slice U edge (m), w = opacity
    public float4                  EdgeV;         // xyz = slice V edge (m), w = scale / pointSize
    public float4                  TimingLimits;  // x = maxSpeed, y = deltaTime, z = lifetime, w = glow/respawnDist
    public int4                    GridSliceMode; // x = axis, y = slice, z = stride, w = rampMode
    public int4                    Flags;         // x = heads/trail, y = channel, z = forceCount, w = simTimeBits
};

[[vk::binding(0, 0)]] ConstantBuffer<FluidOverlayUniformBlock> uOverlay;
[[vk::binding(1, 0)]] Texture3D<float4>                        gVelocity;
[[vk::binding(2, 0)]] Texture3D<float4>                        gState;
[[vk::binding(3, 0)]] Texture3D<float4>                        gPressure;
[[vk::binding(4, 0)]] Texture3D<float4>                        gCurl;
[[vk::binding(5, 0)]] Texture3D<float4>                        gDivergence;
[[vk::binding(6, 0)]] SamplerState                             gLinearClamp;
[[vk::binding(7, 0)]] StructuredBuffer<float4>                 gCurrentBuffer;
[[vk::binding(8, 0)]] StructuredBuffer<float4>                 gPreviousBuffer;
[[vk::binding(9, 0)]] StructuredBuffer<float4>                 gVelocityBuffer;
[[vk::binding(10, 0)]] StructuredBuffer<ForceModelGpuRecord>   gForceModels;

struct ColouredVertexOutput
{
    float4 Position  : SV_Position;
    float4 Colour    : COLOR0;
    float  PointSize : SV_PointSize;
};

struct ColouredFragmentInput
{
    float4 Position  : SV_Position;
    float4 Colour    : COLOR0;
};

struct SliceVertexOutput
{
    float4 Position : SV_Position;
    float3 WorldPos : TEXCOORD0;
};

struct GizmoVertexInput
{
    [[vk::location(0)]] float3 Position : POSITION;
    [[vk::location(1)]] float4 Colour   : COLOR0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                VELOCITY VECTOR ARROWS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
ColouredVertexOutput VelocityVectorVertexEntry(uint vertexId : SV_VertexID)
{
    ColouredVertexOutput outV;
    int heads  = uOverlay.Flags.x;
    int vid    = int(vertexId);
    int vInArr = (heads == 1) ? 0 : (vid - (vid / 2) * 2);
    int arrow  = (heads == 1) ? vid : (vid / 2);

    int axis   = uOverlay.GridSliceMode.x;
    int slice  = uOverlay.GridSliceMode.y;
    int stride = max(1, uOverlay.GridSliceMode.z);
    int ramp   = uOverlay.GridSliceMode.w;
    int3 grid  = uOverlay.Volume.GridBoundary.xyz;

    int2 planeSize = (axis == 0) ? grid.yz : ((axis == 1) ? grid.xz : grid.xy);
    int columns    = (planeSize.x + stride - 1) / stride;
    int u          = (arrow - (arrow / columns) * columns) * stride + stride / 2;
    int v          = (arrow / columns) * stride + stride / 2;
    int3 cell      = (axis == 0) ? int3(slice, u, v) : ((axis == 1) ? int3(u, slice, v) : int3(u, v, slice));

    float3 vel    = FetchCellDense(gVelocity, uOverlay.Volume, cell).xyz;
    float  speed  = length(vel);
    float3 origin = WorldFromCell(uOverlay.Volume, float3(cell) + 0.5);
    float  scale  = uOverlay.EdgeV.w;
    float  cap    = float(stride) * uOverlay.Volume.GridCentreCellSize.w * 1.6;
    float3 tip    = origin + vel * scale * min(1.0, cap / max(speed * scale, 1.0e-4));
    float3 pos    = (vInArr == 1 || heads == 1) ? tip : origin;

    float t     = clamp(speed / max(uOverlay.TimingLimits.x, 1.0e-3), 0.0, 1.0);
    float alpha = smoothstep(0.0, 0.05, t) * (0.35 + 0.65 * t);
    outV.Colour = float4(EvaluateScientificRamp(ramp, t), alpha);
    outV.PointSize = 3.0;

    if (!CellInsideDense(uOverlay.Volume, cell) || v >= planeSize.y)
    {
        outV.Position = float4(2.0, 2.0, 2.0, 1.0);
        outV.Colour.a = 0.0;
    }
    else
    {
        outV.Position = mul(uOverlay.ViewProjection, float4(pos, 1.0));
    }
    return outV;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 SCALAR HEAT SLICE
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
SliceVertexOutput ScalarSliceVertexEntry(uint vertexId : SV_VertexID)
{
    SliceVertexOutput outV;
    int id = int(vertexId);
    float2 uv = float2(
        (id == 1 || id == 2 || id == 4) ? 1.0 : 0.0,
        (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    outV.WorldPos = uOverlay.Corner.xyz + uOverlay.EdgeU.xyz * uv.x + uOverlay.EdgeV.xyz * uv.y;
    outV.Position = mul(uOverlay.ViewProjection, float4(outV.WorldPos, 1.0));
    return outV;
}

[shader("fragment")]
float4 ScalarSliceFragmentEntry(SliceVertexOutput input) : SV_Target0
{
    float3 coord   = CellFromWorld(uOverlay.Volume, input.WorldPos);
    int    channel = uOverlay.Flags.y;
    float  value   = 0.0;
    bool   isSigned = false;

    if (channel == 0)      value = length(SampleVolumeDense(gVelocity, gLinearClamp, uOverlay.Volume, coord).xyz);
    else if (channel == 1) { value = SampleVolumeDense(gPressure, gLinearClamp, uOverlay.Volume, coord).x; isSigned = true; }
    else if (channel == 2) value = SampleVolumeDense(gCurl, gLinearClamp, uOverlay.Volume, coord).w;
    else if (channel == 3) value = SampleVolumeDense(gState, gLinearClamp, uOverlay.Volume, coord).y;
    else if (channel == 4) value = SampleVolumeDense(gState, gLinearClamp, uOverlay.Volume, coord).x;
    else if (channel == 5) { value = SampleVolumeDense(gDivergence, gLinearClamp, uOverlay.Volume, coord).x; isSigned = true; }
    else
    {
        float3 vel = SampleVolumeDense(gVelocity, gLinearClamp, uOverlay.Volume, coord).xyz;
        float  simTime = asfloat(uOverlay.Flags.w);
        value = length(EvaluateTotalForceField(gForceModels, uOverlay.Flags.z, input.WorldPos, vel, simTime, 1u));
    }

    float range = max(uOverlay.Corner.w, 1.0e-5);
    float t     = isSigned ? clamp(value / range * 0.5 + 0.5, 0.0, 1.0) : clamp(value / range, 0.0, 1.0);
    float3 col  = EvaluateScientificRamp(uOverlay.GridSliceMode.w, t);
    float  mag  = isSigned ? abs(t - 0.5) * 2.0 : t;
    float  alpha = uOverlay.EdgeU.w * (0.18 + 0.82 * smoothstep(0.0, 0.3, mag));
    return float4(col, alpha);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              TRACER & DEBRIS VERTEX
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
ColouredVertexOutput TracerRenderVertexEntry(uint vertexId : SV_VertexID)
{
    ColouredVertexOutput outV;
    int trail    = uOverlay.Flags.x;
    int vid      = int(vertexId);
    int particle = (trail == 1) ? (vid / 2) : vid;
    int endPt    = (trail == 1) ? (vid - particle * 2) : 0;

    float4 cur  = gCurrentBuffer[particle];
    float4 prev = gPreviousBuffer[particle];
    float  respawnDist = uOverlay.TimingLimits.w;
    bool   respawned   = (cur.w < prev.w) || (distance(cur.xyz, prev.xyz) > respawnDist);
    float3 pos         = (endPt == 1 && !respawned) ? prev.xyz : cur.xyz;

    float speed = respawned ? 0.0 : (distance(cur.xyz, prev.xyz) / max(uOverlay.TimingLimits.y, 1.0e-4));
    float t     = clamp(speed / max(uOverlay.TimingLimits.x, 1.0e-3), 0.0, 1.0);
    float life  = cur.w / max(uOverlay.TimingLimits.z, 1.0e-3);
    float fade  = smoothstep(0.0, 0.08, life) * (1.0 - smoothstep(0.85, 1.0, life));
    float alpha = fade * (0.08 + 0.92 * smoothstep(0.0, 0.08, t));

    outV.Colour    = float4(EvaluateScientificRamp(uOverlay.GridSliceMode.w, t), alpha * ((endPt == 1) ? 0.25 : 1.0));
    outV.Position  = mul(uOverlay.ViewProjection, float4(pos, 1.0));
    outV.PointSize = uOverlay.EdgeV.w * (0.6 + 0.8 * t);
    return outV;
}

[shader("vertex")]
ColouredVertexOutput DebrisRenderVertexEntry(uint vertexId : SV_VertexID)
{
    ColouredVertexOutput outV;
    int trail    = uOverlay.Flags.x;
    int vid      = int(vertexId);
    int particle = (trail == 1) ? (vid / 2) : vid;
    int endPt    = (trail == 1) ? (vid - particle * 2) : 0;

    float4 cur  = gCurrentBuffer[particle];
    float4 prev = gPreviousBuffer[particle];
    float4 vel  = gVelocityBuffer[particle];

    bool   respawned = (cur.w < prev.w);
    float3 pos       = (endPt == 1 && !respawned) ? prev.xyz : cur.xyz;
    int    kind      = int(floor(vel.w));
    float  seed      = frac(vel.w);
    float  lifetime  = max(uOverlay.TimingLimits.z, 1.0e-3);
    float  alive     = (cur.w < lifetime) ? 1.0 : 0.0;
    float  life      = cur.w / lifetime;
    float  glow      = max(uOverlay.TimingLimits.w, 0.05);
    float  heat      = exp(-cur.w * (1.6 / glow)) * (0.6 + 0.4 * seed);

    float3 col;
    float  sizeScale = 1.0;
    if (kind == 1)
    {
        float flicker = 0.7 + 0.3 * sin(cur.w * (14.0 + 20.0 * seed) + seed * 40.0);
        heat = (1.0 - life) * flicker;
        col = lerp(float3(0.9, 0.18, 0.02), float3(1.0, 0.78, 0.35), seed * 0.6 + 0.4 * heat) * (1.2 + 1.8 * heat);
        sizeScale = 0.55 + 0.5 * heat;
    }
    else if (kind == 2)
    {
        float tumble = 0.55 + 0.45 * abs(sin(cur.w * (5.0 + 6.0 * seed) + seed * 20.0));
        col = lerp(float3(0.78, 0.74, 0.66), float3(0.96, 0.94, 0.88), seed) * tumble;
        sizeScale = 1.4 + 0.8 * seed;
        heat = 0.0;
    }
    else
    {
        float3 ember = lerp(float3(0.95, 0.22, 0.04), float3(1.0, 0.85, 0.45), seed);
        float3 ash   = lerp(float3(0.16, 0.14, 0.13), float3(0.32, 0.29, 0.26), seed);
        col = lerp(ash, ember * (1.0 + 1.5 * heat), smoothstep(0.0, 0.5, heat));
    }

    float speed = length(vel.xyz);
    float fade  = 1.0 - smoothstep(0.8, 1.0, life);
    float alpha = alive * fade * ((endPt == 1) ? ((kind == 2) ? 0.0 : 0.35 * smoothstep(0.3, 3.0, speed)) : 0.95);

    outV.Colour    = float4(col, alpha);
    outV.Position  = mul(uOverlay.ViewProjection, float4(pos, 1.0));
    outV.PointSize = uOverlay.EdgeV.w * sizeScale * (0.55 + 0.9 * seed) * (1.0 + 0.8 * heat);
    return outV;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              GIZMO LINE & COLOUR FRAGMENT
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
ColouredVertexOutput GizmoLineVertexEntry(GizmoVertexInput input)
{
    ColouredVertexOutput outV;
    outV.Colour    = input.Colour;
    outV.Position  = mul(uOverlay.ViewProjection, float4(input.Position, 1.0));
    outV.PointSize = 1.0;
    return outV;
}

[shader("fragment")]
float4 OverlayColourFragmentEntry(ColouredFragmentInput input) : SV_Target0
{
    if (input.Colour.a <= 0.0) discard;
    return input.Colour;
}
