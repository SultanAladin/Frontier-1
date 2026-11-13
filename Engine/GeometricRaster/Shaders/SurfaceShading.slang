//============================================================================================================================================
//                                                            SURFACESHADING.SLANG
//============================================================================================================================================
// 3D PolygonNetwork surface rasterizer outputting HDR-tonemapped RGBA8 + simultaneous R32G32B32A32_UINT GPU Visibility Pick IDs

import MatCapEvaluation;

struct SurfaceVertexRecord
{
    float4 PositionAndU;       // xyz = World Position, w = UV.U
    float4 NormalAndV;         // xyz = World Normal,   w = UV.V
    float4 AlbedoAndMetallic;  // rgb = AlbedoLinear,   a = Metallic
    float4 ShadingParams;      // x = Roughness, y = Specular, z = MatCapBlend, w = StretchMetric
    uint4  NetworkPickIDs;     // x = InstanceSlot, y = FaceIndex, z = DirectedEdgeIndex, w = SurfaceMask (bits 0..7 MatCap, 8..23 Layer, 24 Seam, 25 Border)
    float4 BarycentricAndPad;  // xyz = Triangle Barycentric (1,0,0)/(0,1,0)/(0,0,1), w = EdgeSeamMask
};

struct ViewportUniformBlock
{
    float4x4 ViewProjection;
    float4   CameraPositionAndMode;   // xyz = EyePos, w = DisplayMode (0=SurfaceMaterial, 1=LayerTint, 2=Stretch, 3=CheckerUV, 4=MatCap, 5=Material+MatCap)
    float4   CameraRightAndWire;      // xyz = Right,  w = WireframeEnabled (0/1)
    float4   CameraUpAndSeam;         // xyz = Up,     w = SeamOverlayEnabled (0/1, default 0)
    float4   CameraForwardAndLayer;   // xyz = Fwd,    w = ActiveLayerOrdinal (0 = none)
    uint4    PickHighlightIDs;        // x = HoveredFaceIndex (0xFFFFFFFF = none), y = HoveredEdgeIndex, z = GroundGridPass, w = Reserved
};

[[vk::binding(0, 0)]] ConstantBuffer<ViewportUniformBlock> ViewportUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<SurfaceVertexRecord> VertexRecords;
[[vk::binding(2, 0)]] StructuredBuffer<uint>                FaceSelectionBitmask;

struct VertexInterpolators
{
    float4 ClipPosition       : SV_Position;
    float3 WorldPosition      : TEXCOORD0;
    float3 WorldNormal        : TEXCOORD1;
    float2 ParameterUV        : TEXCOORD2;
    float4 AlbedoAndMetallic  : TEXCOORD3;
    float4 ShadingParams      : TEXCOORD4;
    float3 Barycentric        : TEXCOORD5;
    nointerpolation uint4 IDs : TEXCOORD6;
};

struct FragmentAttachmentOutput
{
    float4 ColorRGBA : SV_Target0;
    uint4  PickIDs   : SV_Target1;
};

float3 ResolvePriorityLayerSwatch(uint LayerOrdinal)
{
    if (LayerOrdinal == 1u) return float3(0.937, 0.957, 0.812); // #EFF4CF Ivory
    if (LayerOrdinal == 2u) return float3(0.620, 0.812, 1.000); // #9ECFFF Sky
    if (LayerOrdinal == 3u) return float3(0.608, 0.855, 0.773); // #9BDAC5 Mint
    if (LayerOrdinal == 4u) return float3(0.910, 0.760, 0.478); // #E8C27A Amber
    return float3(0.42, 0.45, 0.51);
}

[shader("vertex")]
VertexInterpolators VertexEntry(uint VertexID : SV_VertexID)
{
    SurfaceVertexRecord Rec = VertexRecords[VertexID];
    VertexInterpolators   Out;
    Out.ClipPosition      = mul(ViewportUniforms.ViewProjection, float4(Rec.PositionAndU.xyz, 1.0));
    Out.WorldPosition     = Rec.PositionAndU.xyz;
    Out.WorldNormal       = Rec.NormalAndV.xyz;
    Out.ParameterUV       = float2(Rec.PositionAndU.w, Rec.NormalAndV.w);
    Out.AlbedoAndMetallic = Rec.AlbedoAndMetallic;
    Out.ShadingParams     = Rec.ShadingParams;
    Out.Barycentric       = Rec.BarycentricAndPad.xyz;
    Out.IDs               = Rec.NetworkPickIDs;
    return Out;
}

[shader("fragment")]
FragmentAttachmentOutput FragmentEntry(VertexInterpolators In)
{
    FragmentAttachmentOutput Out;

    // Ground Grid Analytic Pass (when NetworkPickIDs.x == 0u)
    if (In.IDs.x == 0u)
    {
        float2 PlaneAxes  = (ViewportUniforms.PickHighlightIDs.w == 1u) ? In.WorldPosition.xz : In.WorldPosition.xy;
        float2 GridCoord  = PlaneAxes * 2.0;
        float2 Deriv      = max(fwidth(GridCoord), float2(1.0e-4, 1.0e-4));
        float2 GridDist   = abs(frac(GridCoord - 0.5) - 0.5) / Deriv;
        float  LineMask   = 1.0 - saturate(min(GridDist.x, GridDist.y));
        float  Fade       = exp(-0.065 * dot(PlaneAxes, PlaneAxes));
        float3 GroundTone = float3(0.068, 0.072, 0.082);
        float3 GridColor  = lerp(GroundTone, float3(0.24, 0.27, 0.33), LineMask * Fade * 0.75);
        Out.ColorRGBA     = float4(GridColor, 1.0);
        Out.PickIDs       = uint4(0u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0u);
        return Out;
    }

    uint   SurfaceMask  = In.IDs.w;
    uint   MatCapSlot   = SurfaceMask & 0xFFu;
    uint   LayerOrdinal = (SurfaceMask >> 8u) & 0xFFFFu;
    bool   EdgeIsSeam   = ((SurfaceMask >> 24u) & 1u) != 0u;

    uint   FaceIdx      = In.IDs.y;
    uint   WordIdx      = FaceIdx >> 5u;
    uint   BitMask      = 1u << (FaceIdx & 31u);
    bool   FaceSelected = (FaceSelectionBitmask[WordIdx] & BitMask) != 0u;
    uint   ActiveLayer  = uint(ViewportUniforms.CameraForwardAndLayer.w + 0.5);
    bool   LayerActive  = (ActiveLayer > 0u) && (LayerOrdinal == ActiveLayer);
    bool   FaceHovered  = (ViewportUniforms.PickHighlightIDs.x == FaceIdx);

    float3 ViewDir = ViewportUniforms.CameraPositionAndMode.xyz - In.WorldPosition;
    float  Mode    = ViewportUniforms.CameraPositionAndMode.w;

    float3 LitLinear = EvaluateStudioSurfaceMaterial(
        In.WorldNormal,
        ViewDir,
        In.AlbedoAndMetallic.rgb,
        In.AlbedoAndMetallic.a,
        In.ShadingParams.x,
        In.ShadingParams.y);
    float3 PbrSrgb = CompressRadiometricACESFilmic(LitLinear);
    float3 NormN   = normalize(In.WorldNormal);
    float3 ViewNormal = normalize(float3(
        dot(NormN, ViewportUniforms.CameraRightAndWire.xyz),
        dot(NormN, ViewportUniforms.CameraUpAndSeam.xyz),
        dot(NormN, -ViewportUniforms.CameraForwardAndLayer.xyz)));
    float3 MatCapSrgb = SampleStudioMatCapMaterial(
        MatCapSlot,
        ViewNormal,
        In.AlbedoAndMetallic.rgb,
        In.AlbedoAndMetallic.a,
        In.ShadingParams.x,
        In.ShadingParams.y);

    // Screen-space curvature bevel highlight & subtle cavity contrast
    float  BevelCurvature = saturate((length(ddx(NormN)) + length(ddy(NormN))) * 0.32);
    float3 FinalColor     = PbrSrgb;
    if (Mode > 3.5 && Mode < 4.5)
    {
        FinalColor = MatCapSrgb;
    }
    else if (Mode >= 4.5 && Mode < 5.5)
    {
        FinalColor = lerp(PbrSrgb, MatCapSrgb, saturate(In.ShadingParams.z));
    }
    FinalColor = saturate(FinalColor + float3(0.055 * BevelCurvature));

    if (Mode > 0.5 && Mode < 1.5) // Mode 1: Layer Allocation Tint
    {
        float3 LayerTint = ResolvePriorityLayerSwatch(LayerOrdinal);
        FinalColor       = lerp(FinalColor, LayerTint * (0.45 + 0.55 * FinalColor.g), 0.72);
    }
    else if (Mode >= 1.5 && Mode < 2.5) // Mode 2: L2 Stretch Heatmap
    {
        float  StretchErr = saturate(abs(In.ShadingParams.w - 1.0) * 2.2);
        float3 HeatColor  = lerp(float3(0.20, 0.78, 0.48), float3(0.94, 0.26, 0.24), StretchErr);
        FinalColor        = lerp(FinalColor, HeatColor, 0.78);
    }
    else if (Mode >= 2.5 && Mode < 3.5) // Mode 3: UDIM UV Checker Preview
    {
        int2   CheckCell = int2(floor(In.ParameterUV * 16.0));
        float  Parity    = float((CheckCell.x + CheckCell.y) & 1);
        float3 CheckTint = lerp(float3(0.18, 0.20, 0.24), float3(0.82, 0.85, 0.90), Parity);
        FinalColor       = FinalColor * 0.45 + CheckTint * 0.55;
    }

    // GPU O(1) Selection / Active Layer / Hover Highlight
    if (FaceSelected || (LayerActive && Mode > 0.5 && Mode < 1.5))
    {
        FinalColor = lerp(FinalColor, float3(0.937, 0.957, 0.812), 0.34);
    }
    else if (FaceHovered)
    {
        FinalColor = lerp(FinalColor, float3(0.55, 0.78, 1.00), 0.22);
    }

    // Screen-Space Anti-Aliased Wireframe & Optional Seam Overlay (Default Off)
    float3 BaryDeriv = max(fwidth(In.Barycentric), float3(1.0e-4));
    float3 EdgeDists = In.Barycentric / BaryDeriv;
    float  MinEdge   = min(EdgeDists.x, min(EdgeDists.y, EdgeDists.z));

    if (ViewportUniforms.CameraRightAndWire.w > 0.5)
    {
        float WireFactor = 1.0 - smoothstep(0.35, 1.35, MinEdge);
        float3 WireCol   = (FaceSelected || LayerActive) ? float3(0.96, 0.98, 0.84) : float3(0.09, 0.10, 0.13);
        FinalColor       = lerp(FinalColor, WireCol, WireFactor * 0.58);
    }

    if (ViewportUniforms.CameraUpAndSeam.w > 0.5 && EdgeIsSeam && EdgeDists.x < 1.8)
    {
        float SeamFactor = 1.0 - smoothstep(0.5, 1.8, EdgeDists.x);
        FinalColor       = lerp(FinalColor, float3(0.98, 0.44, 0.22), SeamFactor * 0.92);
    }

    Out.ColorRGBA = float4(FinalColor, 1.0);
    Out.PickIDs   = uint4(In.IDs.x, In.IDs.y, In.IDs.z, 0u);
    return Out;
}
