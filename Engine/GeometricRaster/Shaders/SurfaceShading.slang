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
    uint4  NetworkPickIDs;     // x = InstanceSlot, y = FaceIndex, z = CornerEdgeIndex, w = SurfaceMask (bits 0..7 MatCap, 8..23 Layer, 24 Seam)
    float4 BarycentricAndPad;  // xyz = Triangle Barycentric (1,0,0)/(0,1,0)/(0,0,1), w = Reserved
    float4 MatCapTintAndPad;   // rgb = MatCapTintLinearRGB, a = 1.0
    uint4  TriEdgeIDs;         // x = EdgeOppV0 (Bary.x==0), y = EdgeOppV1 (Bary.y==0), z = EdgeOppV2 (Bary.z==0), w = Hovered/Pad
    uint4  TriVertexIDs;       // x = Vert0, y = Vert1, z = Vert2, w = AuthoredMask(0..2) | (SeamMask(0..2) << 3)
};

struct ViewportUniformBlock
{
    float4x4 ViewProjection;
    float4   CameraPositionAndMode;   // xyz = EyePos, w = DisplayMode
    float4   CameraRightAndWire;      // xyz = Right,  w = WireframeEnabled (0/1)
    float4   CameraUpAndSeam;         // xyz = Up,     w = SeamOverlayEnabled (0/1)
    float4   CameraForwardAndLayer;   // xyz = Fwd,    w = ActiveLayerOrdinal (0 = none)
    uint4    PickHighlightIDs;        // x = HoveredFaceIdx, y = HoveredEdgeIdx, z = HoveredOppositeEdgeIdx, w = WorldBasis
    uint4    ComponentHighlightIDs;   // x = HoveredVertexIdx, y = SelectionModeOrdinal (0=Vert,1=Edge,2=Face,3=Island,4=Layer), z = Reserved, w = Reserved
    float4   ViewportSizeAndPad;      // xy = Framebuffer (WidthPx, HeightPx), zw = Reserved
    float4   LayerSwatches[16];       // rgb = linear layer tint colour per layer ordinal [0..15]
};

[[vk::binding(0, 0)]] ConstantBuffer<ViewportUniformBlock> ViewportUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<SurfaceVertexRecord> VertexRecords;
[[vk::binding(2, 0)]] StructuredBuffer<uint>                FaceSelectionBitmask;
[[vk::binding(5, 0)]] StructuredBuffer<uint>                EdgeSelectionBitmask;
[[vk::binding(6, 0)]] StructuredBuffer<uint>                VertexSelectionBitmask;

struct VertexInterpolators
{
    float4 ClipPosition                 : SV_Position;
    float3 WorldPosition                : TEXCOORD0;
    float3 WorldNormal                  : TEXCOORD1;
    float2 ParameterUV                  : TEXCOORD2;
    float4 AlbedoAndMetallic            : TEXCOORD3;
    float4 ShadingParams                : TEXCOORD4;
    float3 Barycentric                  : TEXCOORD5;
    nointerpolation uint4 IDs           : TEXCOORD6;
    float3 MatCapTintLinear             : TEXCOORD7;
    nointerpolation uint4 TriEdgeIDs    : TEXCOORD8;
    nointerpolation uint4 TriVertexIDs  : TEXCOORD9;
    nointerpolation float4 TriScreen01  : TEXCOORD10;
    nointerpolation float2 TriScreen2   : TEXCOORD11;
};

struct FragmentAttachmentOutput
{
    float4 ColorRGBA : SV_Target0;
    uint4  PickIDs   : SV_Target1;
};

float3 ResolvePriorityLayerSwatch(uint LayerOrdinal)
{
    if (LayerOrdinal < 16u)
    {
        return ViewportUniforms.LayerSwatches[LayerOrdinal].rgb;
    }
    return float3(0.42, 0.45, 0.51);
}

float2 ProjectWorldToScreenPx(float3 WorldPos)
{
    float4 Clip = mul(ViewportUniforms.ViewProjection, float4(WorldPos, 1.0));
    if (Clip.w <= 1.0e-4)
    {
        return float2(-1.0e5, -1.0e5);
    }
    float2 Ndc = Clip.xy / Clip.w;
    return (Ndc * 0.5 + 0.5) * ViewportUniforms.ViewportSizeAndPad.xy;
}

bool QueryBitSet(StructuredBuffer<uint> Bitmask, uint Index)
{
    if (Index == 0xFFFFFFFFu)
    {
        return false;
    }
    return (Bitmask[Index >> 5u] & (1u << (Index & 31u))) != 0u;
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
    Out.MatCapTintLinear  = Rec.MatCapTintAndPad.rgb;
    Out.TriEdgeIDs        = Rec.TriEdgeIDs;
    Out.TriVertexIDs      = Rec.TriVertexIDs;

    if (Rec.NetworkPickIDs.x != 0u)
    {
        uint   TriStartVertex = VertexID - (VertexID % 3u);
        float2 S0             = ProjectWorldToScreenPx(VertexRecords[TriStartVertex + 0u].PositionAndU.xyz);
        float2 S1             = ProjectWorldToScreenPx(VertexRecords[TriStartVertex + 1u].PositionAndU.xyz);
        float2 S2             = ProjectWorldToScreenPx(VertexRecords[TriStartVertex + 2u].PositionAndU.xyz);
        Out.TriScreen01 = float4(S0, S1);
        Out.TriScreen2  = S2;
    }
    else
    {
        Out.TriScreen01 = float4(-1.0e5, -1.0e5, -1.0e5, -1.0e5);
        Out.TriScreen2  = float2(-1.0e5, -1.0e5);
    }
    return Out;
}

[shader("fragment")]
FragmentAttachmentOutput FragmentEntry(VertexInterpolators In)
{
    FragmentAttachmentOutput Out;

    // Ground Grid Analytic Rasterization (when NetworkPickIDs.x == 0u)
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

    uint SurfaceMask  = In.IDs.w;
    uint MatCapSlot   = SurfaceMask & 0xFFu;
    uint LayerOrdinal = (SurfaceMask >> 8u) & 0xFFFFu;

    uint FaceIdx      = In.IDs.y;
    bool FaceSelected = QueryBitSet(FaceSelectionBitmask, FaceIdx);
    uint ActiveLayer  = uint(ViewportUniforms.CameraForwardAndLayer.w + 0.5);
    bool LayerActive  = (ActiveLayer > 0u) && (LayerOrdinal == ActiveLayer);
    bool FaceHovered  = (ViewportUniforms.PickHighlightIDs.x == FaceIdx);

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
        In.MatCapTintLinear,
        In.AlbedoAndMetallic.a,
        In.ShadingParams.x,
        In.ShadingParams.y);

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

    if (Mode > 0.5 && Mode < 1.5)
    {
        float3 LayerTint = ResolvePriorityLayerSwatch(LayerOrdinal);
        FinalColor       = lerp(FinalColor, LayerTint * (0.45 + 0.55 * FinalColor.g), 0.72);
    }
    else if (Mode >= 1.5 && Mode < 2.5)
    {
        float  StretchErr = saturate(abs(In.ShadingParams.w - 1.0) * 2.2);
        float3 HeatColor  = lerp(float3(0.20, 0.78, 0.48), float3(0.94, 0.26, 0.24), StretchErr);
        FinalColor        = lerp(FinalColor, HeatColor, 0.78);
    }
    else if (Mode >= 2.5 && Mode < 3.5)
    {
        int2   CheckCell = int2(floor(In.ParameterUV * 16.0));
        float  Parity    = float((CheckCell.x + CheckCell.y) & 1);
        float3 CheckTint = lerp(float3(0.18, 0.20, 0.24), float3(0.82, 0.85, 0.90), Parity);
        FinalColor       = FinalColor * 0.45 + CheckTint * 0.55;
    }

    uint SelectionModeOrdinal = ViewportUniforms.ComponentHighlightIDs.y;

    // GPU O(1) Selection / Active Layer / Hover Highlight
    if (FaceSelected || (LayerActive && Mode > 0.5 && Mode < 1.5))
    {
        FinalColor = lerp(FinalColor, float3(0.937, 0.957, 0.812), 0.34);
    }
    else if (FaceHovered && SelectionModeOrdinal >= 2u)
    {
        FinalColor = lerp(FinalColor, float3(0.55, 0.78, 1.00), 0.22);
    }

    // Authored-polygon wireframe masking (hides internal triangulation fan diagonals!)
    float3 BaryDeriv = max(fwidth(In.Barycentric), float3(1.0e-4));
    float3 EdgeDists = In.Barycentric / BaryDeriv;
    uint   AuthMask  = In.TriVertexIDs.w & 7u;
    uint   SeamMask  = (In.TriVertexIDs.w >> 3u) & 7u;

    float3 AuthDists = float3(
        ((AuthMask & 1u) != 0u) ? EdgeDists.x : 1.0e5,
        ((AuthMask & 2u) != 0u) ? EdgeDists.y : 1.0e5,
        ((AuthMask & 4u) != 0u) ? EdgeDists.z : 1.0e5);
    float MinAuthEdge = min(AuthDists.x, min(AuthDists.y, AuthDists.z));

    if (ViewportUniforms.CameraRightAndWire.w > 0.5)
    {
        float WireFactor = 1.0 - smoothstep(0.35, 1.35, MinAuthEdge);
        float3 WireCol   = (FaceSelected || LayerActive) ? float3(0.96, 0.98, 0.84) : float3(0.09, 0.10, 0.13);
        FinalColor       = lerp(FinalColor, WireCol, WireFactor * 0.62);
    }

    // GPU Seam Highlight on all 3 authored triangle boundary edges
    if (ViewportUniforms.CameraUpAndSeam.w > 0.5 && SeamMask != 0u)
    {
        float SeamDist = 1.0e5;
        if ((SeamMask & 1u) != 0u) SeamDist = min(SeamDist, AuthDists.x);
        if ((SeamMask & 2u) != 0u) SeamDist = min(SeamDist, AuthDists.y);
        if ((SeamMask & 4u) != 0u) SeamDist = min(SeamDist, AuthDists.z);
        float SeamFactor = 1.0 - smoothstep(0.55, 2.0, SeamDist);
        FinalColor       = lerp(FinalColor, float3(0.98, 0.44, 0.22), SeamFactor * 0.94);
    }

    // Closest Authored Edge Pick + Edge Selection / Hover Highlights
    uint  ClosestAuthEdge = In.IDs.z;
    float BestAuthDist    = 1.0e5;
    float MinSelEdgeDist  = 1.0e5;
    float MinHovEdgeDist  = 1.0e5;

    uint  HovEdge    = ViewportUniforms.PickHighlightIDs.y;
    uint  HovOppEdge = ViewportUniforms.PickHighlightIDs.z;

    if ((AuthMask & 1u) != 0u && In.TriEdgeIDs.x != 0xFFFFFFFFu)
    {
        if (AuthDists.x < BestAuthDist) { BestAuthDist = AuthDists.x; ClosestAuthEdge = In.TriEdgeIDs.x; }
        if (QueryBitSet(EdgeSelectionBitmask, In.TriEdgeIDs.x)) MinSelEdgeDist = min(MinSelEdgeDist, AuthDists.x);
        if (In.TriEdgeIDs.x == HovEdge || In.TriEdgeIDs.x == HovOppEdge) MinHovEdgeDist = min(MinHovEdgeDist, AuthDists.x);
    }
    if ((AuthMask & 2u) != 0u && In.TriEdgeIDs.y != 0xFFFFFFFFu)
    {
        if (AuthDists.y < BestAuthDist) { BestAuthDist = AuthDists.y; ClosestAuthEdge = In.TriEdgeIDs.y; }
        if (QueryBitSet(EdgeSelectionBitmask, In.TriEdgeIDs.y)) MinSelEdgeDist = min(MinSelEdgeDist, AuthDists.y);
        if (In.TriEdgeIDs.y == HovEdge || In.TriEdgeIDs.y == HovOppEdge) MinHovEdgeDist = min(MinHovEdgeDist, AuthDists.y);
    }
    if ((AuthMask & 4u) != 0u && In.TriEdgeIDs.z != 0xFFFFFFFFu)
    {
        if (AuthDists.z < BestAuthDist) { BestAuthDist = AuthDists.z; ClosestAuthEdge = In.TriEdgeIDs.z; }
        if (QueryBitSet(EdgeSelectionBitmask, In.TriEdgeIDs.z)) MinSelEdgeDist = min(MinSelEdgeDist, AuthDists.z);
        if (In.TriEdgeIDs.z == HovEdge || In.TriEdgeIDs.z == HovOppEdge) MinHovEdgeDist = min(MinHovEdgeDist, AuthDists.z);
    }

    if (SelectionModeOrdinal == 1u && MinHovEdgeDist < 3.0)
    {
        float HovFactor = 1.0 - smoothstep(0.9, 2.5, MinHovEdgeDist);
        FinalColor      = lerp(FinalColor, float3(0.45, 0.78, 1.00), HovFactor * 0.90);
    }
    if (MinSelEdgeDist < 3.2)
    {
        float SelEdgeFactor = 1.0 - smoothstep(1.1, 2.7, MinSelEdgeDist);
        FinalColor          = lerp(FinalColor, float3(1.00, 0.68, 0.18), SelEdgeFactor);
    }

    // Selected / Hovered / Active-Mode Vertex Dots (pixel-accurate screen discs)
    float2 FragPx = In.ClipPosition.xy;
    float  D0     = distance(FragPx, In.TriScreen01.xy);
    float  D1     = distance(FragPx, In.TriScreen01.zw);
    float  D2     = distance(FragPx, In.TriScreen2);

    bool   VertSel0 = QueryBitSet(VertexSelectionBitmask, In.TriVertexIDs.x);
    bool   VertSel1 = QueryBitSet(VertexSelectionBitmask, In.TriVertexIDs.y);
    bool   VertSel2 = QueryBitSet(VertexSelectionBitmask, In.TriVertexIDs.z);
    float  MinSelVertPx = 1.0e5;
    if (VertSel0) MinSelVertPx = min(MinSelVertPx, D0);
    if (VertSel1) MinSelVertPx = min(MinSelVertPx, D1);
    if (VertSel2) MinSelVertPx = min(MinSelVertPx, D2);

    uint   HovVert = ViewportUniforms.ComponentHighlightIDs.x;
    float  MinHovVertPx = 1.0e5;
    if (SelectionModeOrdinal == 0u && HovVert != 0xFFFFFFFFu)
    {
        if (In.TriVertexIDs.x == HovVert) MinHovVertPx = min(MinHovVertPx, D0);
        if (In.TriVertexIDs.y == HovVert) MinHovVertPx = min(MinHovVertPx, D1);
        if (In.TriVertexIDs.z == HovVert) MinHovVertPx = min(MinHovVertPx, D2);
    }

    if (SelectionModeOrdinal == 0u)
    {
        float AnyVertPx = min(D0, min(D1, D2));
        if (AnyVertPx < 3.6)
        {
            float DotBorder = 1.0 - smoothstep(2.2, 3.3, AnyVertPx);
            float DotInner  = 1.0 - smoothstep(1.1, 2.0, AnyVertPx);
            FinalColor      = lerp(FinalColor, float3(0.06, 0.07, 0.09), DotBorder * 0.90);
            FinalColor      = lerp(FinalColor, float3(0.80, 0.82, 0.86), DotInner * 0.92);
        }
    }
    if (MinHovVertPx < 5.5)
    {
        float HovDot = 1.0 - smoothstep(3.0, 4.4, MinHovVertPx);
        FinalColor   = lerp(FinalColor, float3(0.45, 0.78, 1.00), HovDot);
    }
    if (MinSelVertPx < 5.8)
    {
        float SelOuter = 1.0 - smoothstep(3.2, 4.5, MinSelVertPx);
        float SelInner = 1.0 - smoothstep(1.4, 2.4, MinSelVertPx);
        FinalColor     = lerp(FinalColor, float3(1.00, 0.62, 0.14), SelOuter);
        FinalColor     = lerp(FinalColor, float3(1.00, 0.94, 0.76), SelInner);
    }

    Out.ColorRGBA = float4(FinalColor, 1.0);
    Out.PickIDs   = uint4(In.IDs.x, In.IDs.y, ClosestAuthEdge, 0u);
    return Out;
}
