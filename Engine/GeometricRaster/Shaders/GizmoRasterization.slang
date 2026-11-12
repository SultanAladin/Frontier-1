//============================================================================================================================================
//                                                           GIZMORASTERIZATION.SLANG
//============================================================================================================================================
// Shared 3D & 2D Slate GizmoPRO handle rasterizer writing handle RGBA color + GizmoHandleOrdinal in PickIDs.w

struct GizmoVertexRecord
{
    float4 PositionAndAlpha;  // xyz = World or NDC Position, w = BaseAlpha (1.0 solid, 0.28 translucent plane quad)
    float4 NormalAndHandle;   // xyz = Shading Normal,        w = float(GizmoHandleOrdinal)
    float4 ColorRGBAndShaded; // rgb = Slate Handle Color,    w = LitShadingWeight (1.0 for 3D cones/cylinders, 0.0 for flat/arc/ring)
};

struct GizmoUniformBlock
{
    float4x4 ViewProjection;
    uint4    HoverAndSpace;   // x = HoveredHandleOrdinal (0 = none), y = ActiveDragOrdinal, z = DirectClipSpace (1 for 2D), w = Reserved
};

[[vk::binding(0, 0)]] ConstantBuffer<GizmoUniformBlock>    GizmoUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<GizmoVertexRecord>  GizmoVertices;

struct GizmoVSOutput
{
    float4 ClipPosition          : SV_Position;
    float3 Normal                : TEXCOORD0;
    float4 TintAndAlpha     : TEXCOORD1;
    float  ShadingWeight         : TEXCOORD2;
    nointerpolation uint Ordinal : TEXCOORD3;
};

struct GizmoFSOutput
{
    float4 ColorRGBA : SV_Target0;
    uint4  PickIDs   : SV_Target1;
};

[shader("vertex")]
GizmoVSOutput VertexEntry(uint VertexID : SV_VertexID)
{
    GizmoVertexRecord Rec = GizmoVertices[VertexID];
    GizmoVSOutput     Out;
    if (GizmoUniforms.HoverAndSpace.z != 0u)
    {
        Out.ClipPosition = float4(Rec.PositionAndAlpha.xyz, 1.0);
    }
    else
    {
        Out.ClipPosition = mul(GizmoUniforms.ViewProjection, float4(Rec.PositionAndAlpha.xyz, 1.0));
    }
    Out.Normal            = Rec.NormalAndHandle.xyz;
    Out.TintAndAlpha = float4(Rec.ColorRGBAndShaded.rgb, Rec.PositionAndAlpha.w);
    Out.ShadingWeight     = Rec.ColorRGBAndShaded.w;
    Out.Ordinal           = uint(Rec.NormalAndHandle.w + 0.5);
    return Out;
}

[shader("fragment")]
GizmoFSOutput FragmentEntry(GizmoVSOutput In)
{
    bool HoveredOrActive =
        (In.Ordinal != 0u) &&
        (In.Ordinal == GizmoUniforms.HoverAndSpace.x || In.Ordinal == GizmoUniforms.HoverAndSpace.y);

    float3 LightDir = normalize(float3(0.45, 0.72, 0.52));
    float  Lambert  = 0.62 + 0.38 * saturate(dot(normalize(In.Normal + float3(0.0, 0.0, 1.0e-5)), LightDir));
    float3 LitRGB   = lerp(In.TintAndAlpha.rgb, In.TintAndAlpha.rgb * Lambert, In.ShadingWeight);

    float  Alpha = In.TintAndAlpha.a;
    if (HoveredOrActive)
    {
        // Slate GizmoPRO hover rule: +0.33 emissive boost (#555555) and translucent plane alpha 0.28 -> 0.55
        LitRGB = saturate(LitRGB + float3(0.333, 0.333, 0.333));
        if (Alpha < 0.95)
        {
            Alpha = 0.55;
        }
    }

    // Blend translucent plane quads cleanly over dark viewport backdrop when alpha blending is disabled on integer MRT
    float3 BlendedRGB = lerp(float3(0.10, 0.11, 0.14), LitRGB, Alpha);

    GizmoFSOutput Out;
    Out.ColorRGBA = float4(BlendedRGB, Alpha);
    Out.PickIDs   = uint4(0u, 0xFFFFFFFFu, 0xFFFFFFFFu, In.Ordinal);
    return Out;
}
