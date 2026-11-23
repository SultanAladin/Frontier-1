/*============================================================================================================================================
                                                           GIZMORASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Shared 3D & 2D Slate GizmoPRO handle rasterizer writing handle RGBA color + GizmoHandleOrdinal in PickIDs.w

struct GizmoVertexEntry
{
    float4 PositionAlpha;      // xyz = World or NDC Position, w = BaseAlpha (1.0 solid, 0.28 translucent plane quad)
    float4 NormalHandlePacked; // xyz = Shading Normal,        w = float(GizmoHandleOrdinal)
    float4 AccentLitWeight;    // rgb = Slate Handle Color,    w = LitShadingWeight (1.0 for 3D cones/cylinders, 0.0 for flat/arc/ring)
};

struct GizmoUniformBlock
{
    float4x4 ViewProjection;
    uint4    HoverSpacePacked;      // x = HoveredHandleOrdinal (0 = none), y = ActiveDragOrdinal, z = DirectClipSpace (1 for 2D), w = Reserved
    float4   HeadlightDirection; // xyz = Camera-relative key-light vector, w = 1.0
};

[[vk::binding(0, 0)]] ConstantBuffer<GizmoUniformBlock>    GizmoUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<GizmoVertexEntry>  GizmoVertices;

struct GizmoVSOutput
{
    float4 ClipPosition          : SV_Position;
    float3 Normal                : TEXCOORD0;
    float4 TintAlphaPacked     : TEXCOORD1;
    float  ShadingWeight         : TEXCOORD2;
    nointerpolation uint Ordinal : TEXCOORD3;
};

struct GizmoFSOutput
{
    float4 ColorRGBA : SV_Target0;
    uint4  PickIDs   : SV_Target1;
};

[shader("vertex")]
GizmoVSOutput VertexEntry(uint VertexID : SV_VulkanVertexID)
{
    GizmoVertexEntry Rec = GizmoVertices[VertexID];
    GizmoVSOutput     Out;
    if (GizmoUniforms.HoverSpacePacked.z != 0u)
    {
        Out.ClipPosition = float4(Rec.PositionAlpha.xyz, 1.0);
    }
    else
    {
        Out.ClipPosition = mul(GizmoUniforms.ViewProjection, float4(Rec.PositionAlpha.xyz, 1.0));
    }
    Out.Normal          = Rec.NormalHandlePacked.xyz;
    Out.TintAlphaPacked = float4(Rec.AccentLitWeight.rgb, Rec.PositionAlpha.w);
    Out.ShadingWeight   = Rec.AccentLitWeight.w;
    Out.Ordinal         = uint(Rec.NormalHandlePacked.w + 0.5);
    return Out;
}

[shader("fragment")]
GizmoFSOutput FragmentEntry(GizmoVSOutput In)
{
    bool HoveredOrActive =
        (In.Ordinal != 0u) &&
        (In.Ordinal == GizmoUniforms.HoverSpacePacked.x || In.Ordinal == GizmoUniforms.HoverSpacePacked.y);

    float3 LightDir = normalize(GizmoUniforms.HeadlightDirection.xyz + float3(0.0, 0.0, 1.0e-5));
    float3 SurfN    = normalize(In.Normal + float3(0.0, 0.0, 1.0e-5));
    float  KeyTerm  = saturate(dot(SurfN, LightDir));
    float  FillTerm = saturate(dot(SurfN, -LightDir)) * 0.25;
    float  Lambert  = 0.58 + 0.42 * KeyTerm + FillTerm;
    float3 LitRGB   = lerp(In.TintAlphaPacked.rgb, saturate(In.TintAlphaPacked.rgb * Lambert + float3(0.06) * pow(KeyTerm, 6.0)), In.ShadingWeight);

    float  Alpha = In.TintAlphaPacked.a;
    if (HoveredOrActive)
    {
        LitRGB = saturate(LitRGB + float3(0.333, 0.333, 0.333));
        if (Alpha < 0.95)
        {
            Alpha = 0.55;
        }
    }

    GizmoFSOutput Out;
    Out.ColorRGBA = float4(LitRGB, Alpha);
    Out.PickIDs   = uint4(0u, 0xFFFFFFFFu, 0xFFFFFFFFu, In.Ordinal);
    return Out;
}
