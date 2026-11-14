//============================================================================================================================================
//                                                          UVTILERASTERIZATION.SLANG
//============================================================================================================================================
// 2D UDIM UVTile parameter canvas shader rendering checker/custom backdrops, authored-polygon UV edges, GPU seam/pin/selection & 2D Pick IDs

struct UVCanvasVertexRecord
{
    float4 UVAndBaryXY;    // xy = ParameterUV (U, V), zw = Barycentric.xy
    float4 BaryZAndStats;  // x = Barycentric.z, y = StretchMetric, z = SignedAreaSign, w = OverlapFlag
    uint4  NetworkPickIDs; // x = InstanceSlot, y = FaceIndex, z = CornerEdgeIndex, w = LayerOrd | (Pins<<16) | (Seams<<19) | (Authored<<22)
    uint4  TriEdgeIDs;     // x = EdgeOppV0 (Bary.x==0), y = EdgeOppV1 (Bary.y==0), z = EdgeOppV2 (Bary.z==0), w = Reserved
    uint4  TriVertexIDs;   // x = Vert0, y = Vert1, z = Vert2, w = Reserved
};

struct UVCanvasUniformBlock
{
    float4 CanvasMinMaxUV;       // xy = VisibleMinUV, zw = VisibleMaxUV
    uint4  OverlayAndPick;       // x = DisplayMode, y = Checker|(Seam<<1)|(Pin<<2)|(Backdrop<<8), z = ActiveLayerOrd, w = HoveredFaceIdx
    uint4  ComponentHighlightIDs;// x = HoveredEdgeIdx, y = HoveredOppositeEdgeIdx, z = HoveredVertexIdx, w = SelectionModeOrdinal
    float4 ViewportSizeAndPad;   // xy = Framebuffer (WidthPx, HeightPx), zw = Reserved
    float4 LayerSwatches[16];    // rgb = priority layer tint colour per layer ordinal [0..15]
};

[[vk::binding(0, 0)]] ConstantBuffer<UVCanvasUniformBlock>  CanvasUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<UVCanvasVertexRecord> CanvasVertices;
[[vk::binding(2, 0)]] StructuredBuffer<uint>                 FaceSelectionBitmask;
[[vk::binding(3, 0)]] StructuredBuffer<uint>                 EdgeSelectionBitmask;
[[vk::binding(4, 0)]] StructuredBuffer<uint>                 VertexSelectionBitmask;

struct UVVertexOutput
{
    float4 ClipPosition                 : SV_Position;
    float2 ParameterUV                  : TEXCOORD0;
    float3 Barycentric                  : TEXCOORD1;
    float3 Diagnostics                  : TEXCOORD2;
    nointerpolation uint4 IDs           : TEXCOORD3;
    nointerpolation uint4 TriEdgeIDs    : TEXCOORD4;
    nointerpolation uint4 TriVertexIDs  : TEXCOORD5;
    nointerpolation float4 TriScreen01  : TEXCOORD6;
    nointerpolation float2 TriScreen2   : TEXCOORD7;
};

struct UVFragmentOutput
{
    float4 ColorRGBA : SV_Target0;
    uint4  PickIDs   : SV_Target1;
};

float3 ResolveUVLayerColor(uint LayerOrdinal)
{
    if (LayerOrdinal < 16u)
    {
        return CanvasUniforms.LayerSwatches[LayerOrdinal].rgb;
    }
    return float3(0.52, 0.56, 0.64);
}

float2 ProjectUVToScreenPx(float2 UV)
{
    float2 MinUV = CanvasUniforms.CanvasMinMaxUV.xy;
    float2 MaxUV = CanvasUniforms.CanvasMinMaxUV.zw;
    float2 Norm  = (UV - MinUV) / max(MaxUV - MinUV, float2(1.0e-5, 1.0e-5));
    return float2(Norm.x, 1.0 - Norm.y) * CanvasUniforms.ViewportSizeAndPad.xy;
}

bool QueryBitSet2D(StructuredBuffer<uint> Bitmask, uint Index)
{
    if (Index == 0xFFFFFFFFu)
    {
        return false;
    }
    return (Bitmask[Index >> 5u] & (1u << (Index & 31u))) != 0u;
}

float3 EvaluateBackdropTexture(uint BackdropPreset, float2 LocalUV)
{
    if (BackdropPreset == 1u)
    {
        int2   Patch  = int2(floor(LocalUV * float2(6.0, 4.0)));
        uint   Idx    = uint((Patch.y * 6 + Patch.x) & 7);
        float3 Swatch = float3(0.24, 0.26, 0.30);
        if (Idx == 0u) Swatch = float3(0.45, 0.32, 0.27);
        if (Idx == 1u) Swatch = float3(0.76, 0.58, 0.51);
        if (Idx == 2u) Swatch = float3(0.36, 0.48, 0.62);
        if (Idx == 3u) Swatch = float3(0.35, 0.43, 0.26);
        if (Idx == 4u) Swatch = float3(0.51, 0.50, 0.69);
        if (Idx == 5u) Swatch = float3(0.40, 0.74, 0.67);
        if (Idx == 6u) Swatch = float3(0.84, 0.49, 0.18);
        if (Idx == 7u) Swatch = float3(0.78, 0.22, 0.24);
        float2 Inner        = frac(LocalUV * float2(6.0, 4.0));
        float  CellInterior = step(0.06, Inner.x) * step(Inner.x, 0.94) * step(0.08, Inner.y) * step(Inner.y, 0.92);
        return lerp(float3(0.09, 0.09, 0.11), Swatch * 0.55, CellInterior);
    }
    if (BackdropPreset == 2u)
    {
        float  Band   = floor(LocalUV.y * 6.0);
        float  Groove = smoothstep(0.0, 0.06, frac(LocalUV.y * 6.0)) * smoothstep(1.0, 0.94, frac(LocalUV.y * 6.0));
        float3 Tint   = ((uint(Band) & 1u) != 0u) ? float3(0.20, 0.26, 0.34) : float3(0.28, 0.25, 0.19);
        return Tint * (0.55 + 0.45 * Groove);
    }
    if (BackdropPreset == 3u)
    {
        float Brush = 0.5 + 0.5 * sin(LocalUV.x * 180.0 + sin(LocalUV.y * 24.0) * 1.5);
        return lerp(float3(0.14, 0.16, 0.20), float3(0.28, 0.31, 0.38), Brush * 0.65);
    }
    if (BackdropPreset == 5u)
    {
        float2 Grid      = abs(frac(LocalUV * 8.0 - 0.5) - 0.5);
        float  Line      = 1.0 - smoothstep(0.44, 0.49, max(Grid.x, Grid.y));
        float3 CodeColor = float3(LocalUV.x, LocalUV.y, 1.0 - LocalUV.x) * 0.28 + 0.08;
        return lerp(CodeColor, float3(0.36, 0.42, 0.48), Line * 0.65);
    }
    if (BackdropPreset == 6u)
    {
        float Parity = float((int(floor(LocalUV.x * 8.0)) + int(floor(LocalUV.y * 8.0))) & 1);
        return lerp(float3(0.10, 0.11, 0.13), float3(0.18, 0.19, 0.21), Parity);
    }
    if (BackdropPreset == 7u)
    {
        return float3(0.11, 0.12, 0.14);
    }
    return float3(0.072, 0.076, 0.088);
}

[shader("vertex")]
UVVertexOutput VertexEntry(uint VertexID : SV_VertexID)
{
    UVCanvasVertexRecord Rec = CanvasVertices[VertexID];
    float2 MinUV = CanvasUniforms.CanvasMinMaxUV.xy;
    float2 MaxUV = CanvasUniforms.CanvasMinMaxUV.zw;
    float2 Norm  = (Rec.UVAndBaryXY.xy - MinUV) / max(MaxUV - MinUV, float2(1.0e-5, 1.0e-5));

    UVVertexOutput Out;
    Out.ClipPosition = float4(Norm.x * 2.0 - 1.0, 1.0 - Norm.y * 2.0, 0.5, 1.0);
    Out.ParameterUV  = Rec.UVAndBaryXY.xy;
    Out.Barycentric  = float3(Rec.UVAndBaryXY.zw, Rec.BaryZAndStats.x);
    Out.Diagnostics  = Rec.BaryZAndStats.yzw;
    Out.IDs          = Rec.NetworkPickIDs;
    Out.TriEdgeIDs   = Rec.TriEdgeIDs;
    Out.TriVertexIDs = Rec.TriVertexIDs;

    if (Rec.NetworkPickIDs.x != 0u)
    {
        uint   TriStartVertex = VertexID - (VertexID % 3u);
        float2 S0             = ProjectUVToScreenPx(CanvasVertices[TriStartVertex + 0u].UVAndBaryXY.xy);
        float2 S1             = ProjectUVToScreenPx(CanvasVertices[TriStartVertex + 1u].UVAndBaryXY.xy);
        float2 S2             = ProjectUVToScreenPx(CanvasVertices[TriStartVertex + 2u].UVAndBaryXY.xy);
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
UVFragmentOutput FragmentEntry(UVVertexOutput In)
{
    UVFragmentOutput Out;
    uint OverlayMask    = CanvasUniforms.OverlayAndPick.y;
    bool CheckerOn      = (OverlayMask & 1u) != 0u;
    bool SeamOverlayOn  = (OverlayMask & 2u) != 0u;
    bool PinOverlayOn   = (OverlayMask & 4u) != 0u;
    uint BackdropPreset = (OverlayMask >> 8u) & 0xFFu;

    // Background UDIM Checker / Backdrop & Grid (NetworkPickIDs.x == 0u)
    if (In.IDs.x == 0u)
    {
        float2 UV           = In.ParameterUV;
        bool   InTile       = (UV.x >= 0.0 && UV.y >= 0.0);
        float3 BackdropTone = InTile ? float3(0.072, 0.076, 0.088) : float3(0.050, 0.052, 0.060);

        if (InTile && BackdropPreset != 0u)
        {
            BackdropTone = EvaluateBackdropTexture(BackdropPreset, frac(UV));
        }
        else if (InTile && CheckerOn)
        {
            int2  Cell   = int2(floor(UV * 16.0));
            float Parity = float((Cell.x + Cell.y) & 1);
            BackdropTone = lerp(float3(0.068, 0.072, 0.084), float3(0.098, 0.104, 0.120), Parity);
        }

        float2 SubDeriv  = max(fwidth(UV * 4.0), float2(1.0e-4, 1.0e-4));
        float2 SubGrid   = abs(frac(UV * 4.0 - 0.5) - 0.5) / SubDeriv;
        float  SubLine   = 1.0 - saturate(min(SubGrid.x, SubGrid.y));

        float2 TileDeriv = max(fwidth(UV), float2(1.0e-4, 1.0e-4));
        float2 TileGrid  = abs(frac(UV - 0.5) - 0.5) / TileDeriv;
        float  TileLine  = 1.0 - smoothstep(0.0, 1.6, min(TileGrid.x, TileGrid.y));

        float3 FinalBg = lerp(BackdropTone, float3(0.16, 0.18, 0.22), SubLine * 0.55);
        FinalBg        = lerp(FinalBg, float3(0.36, 0.56, 0.86), TileLine * 0.85);
        Out.ColorRGBA  = float4(FinalBg, 1.0);
        Out.PickIDs    = uint4(0u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0u);
        return Out;
    }

    uint PackedMeta   = In.IDs.w;
    uint FaceIdx      = In.IDs.y;
    uint LayerOrdinal = PackedMeta & 0xFFFFu;
    uint PinMask      = (PackedMeta >> 16u) & 0x7u;
    uint SeamMask     = (PackedMeta >> 19u) & 0x7u;
    uint AuthMask     = (PackedMeta >> 22u) & 0x7u;
    if (AuthMask == 0u)
    {
        AuthMask = 7u;
    }

    uint SelectionModeOrdinal = CanvasUniforms.ComponentHighlightIDs.w;

    bool Selected    = QueryBitSet2D(FaceSelectionBitmask, FaceIdx);
    bool LayerActive = (CanvasUniforms.OverlayAndPick.z > 0u) && (LayerOrdinal == CanvasUniforms.OverlayAndPick.z);
    bool Hovered     = (CanvasUniforms.OverlayAndPick.w == FaceIdx);

    uint   DisplayMode = CanvasUniforms.OverlayAndPick.x;
    float3 FillColor   = ResolveUVLayerColor(LayerOrdinal) * 0.52;

    if (DisplayMode == 1u)
    {
        float Err = saturate(abs(In.Diagnostics.x - 1.0) * 2.2);
        FillColor = lerp(float3(0.16, 0.66, 0.42), float3(0.92, 0.24, 0.22), Err);
    }
    else if (DisplayMode == 2u)
    {
        bool Flipped  = In.Diagnostics.y <= 0.0;
        bool Overlaps = In.Diagnostics.z > 0.5;
        if (Flipped)       FillColor = float3(0.92, 0.18, 0.32);
        else if (Overlaps) FillColor = float3(0.96, 0.58, 0.16);
        else               FillColor = float3(0.18, 0.62, 0.42);
    }

    if (Selected || LayerActive)
    {
        FillColor = lerp(FillColor, float3(0.937, 0.957, 0.812), 0.42);
    }
    else if (Hovered && SelectionModeOrdinal >= 2u)
    {
        FillColor = lerp(FillColor, float3(0.62, 0.82, 1.00), 0.28);
    }

    float3 BaryDeriv = max(fwidth(In.Barycentric), float3(1.0e-4));
    float3 EdgeDists = In.Barycentric / BaryDeriv;
    float3 AuthDists = float3(
        ((AuthMask & 1u) != 0u) ? EdgeDists.x : 1.0e5,
        ((AuthMask & 2u) != 0u) ? EdgeDists.y : 1.0e5,
        ((AuthMask & 4u) != 0u) ? EdgeDists.z : 1.0e5);
    float  MinEdge   = min(AuthDists.x, min(AuthDists.y, AuthDists.z));
    float  EdgeMask  = 1.0 - smoothstep(0.4, 1.45, MinEdge);
    float3 EdgeColor = (Selected || LayerActive) ? float3(0.98, 0.99, 0.88) : ResolveUVLayerColor(LayerOrdinal);

    float3 FinalRGB = lerp(FillColor, EdgeColor, EdgeMask * 0.92);

    // GPU Seam Highlight (coral-orange border on marked seam edges when SeamOverlayOn == true)
    if (SeamOverlayOn && SeamMask != 0u)
    {
        float SeamDist = 1.0e5;
        if ((SeamMask & 1u) != 0u) SeamDist = min(SeamDist, AuthDists.x);
        if ((SeamMask & 2u) != 0u) SeamDist = min(SeamDist, AuthDists.y);
        if ((SeamMask & 4u) != 0u) SeamDist = min(SeamDist, AuthDists.z);
        float SeamStroke = 1.0 - smoothstep(0.6, 2.2, SeamDist);
        FinalRGB = lerp(FinalRGB, float3(1.00, 0.46, 0.18), SeamStroke);
    }

    // Closest Authored Edge Pick + Edge Selection / Hover Highlights
    uint  ClosestAuthEdge = In.IDs.z;
    float BestAuthDist    = 1.0e5;
    float MinSelEdgeDist  = 1.0e5;
    float MinHovEdgeDist  = 1.0e5;

    uint HovEdge    = CanvasUniforms.ComponentHighlightIDs.x;
    uint HovOppEdge = CanvasUniforms.ComponentHighlightIDs.y;

    if ((AuthMask & 1u) != 0u && In.TriEdgeIDs.x != 0xFFFFFFFFu)
    {
        if (AuthDists.x < BestAuthDist) { BestAuthDist = AuthDists.x; ClosestAuthEdge = In.TriEdgeIDs.x; }
        if (QueryBitSet2D(EdgeSelectionBitmask, In.TriEdgeIDs.x)) MinSelEdgeDist = min(MinSelEdgeDist, AuthDists.x);
        if (In.TriEdgeIDs.x == HovEdge || In.TriEdgeIDs.x == HovOppEdge) MinHovEdgeDist = min(MinHovEdgeDist, AuthDists.x);
    }
    if ((AuthMask & 2u) != 0u && In.TriEdgeIDs.y != 0xFFFFFFFFu)
    {
        if (AuthDists.y < BestAuthDist) { BestAuthDist = AuthDists.y; ClosestAuthEdge = In.TriEdgeIDs.y; }
        if (QueryBitSet2D(EdgeSelectionBitmask, In.TriEdgeIDs.y)) MinSelEdgeDist = min(MinSelEdgeDist, AuthDists.y);
        if (In.TriEdgeIDs.y == HovEdge || In.TriEdgeIDs.y == HovOppEdge) MinHovEdgeDist = min(MinHovEdgeDist, AuthDists.y);
    }
    if ((AuthMask & 4u) != 0u && In.TriEdgeIDs.z != 0xFFFFFFFFu)
    {
        if (AuthDists.z < BestAuthDist) { BestAuthDist = AuthDists.z; ClosestAuthEdge = In.TriEdgeIDs.z; }
        if (QueryBitSet2D(EdgeSelectionBitmask, In.TriEdgeIDs.z)) MinSelEdgeDist = min(MinSelEdgeDist, AuthDists.z);
        if (In.TriEdgeIDs.z == HovEdge || In.TriEdgeIDs.z == HovOppEdge) MinHovEdgeDist = min(MinHovEdgeDist, AuthDists.z);
    }

    if (SelectionModeOrdinal == 1u && MinHovEdgeDist < 3.0)
    {
        float HovFactor = 1.0 - smoothstep(0.9, 2.5, MinHovEdgeDist);
        FinalRGB        = lerp(FinalRGB, float3(0.45, 0.78, 1.00), HovFactor * 0.92);
    }
    if (MinSelEdgeDist < 3.4)
    {
        float SelEdgeFactor = 1.0 - smoothstep(1.1, 2.8, MinSelEdgeDist);
        FinalRGB            = lerp(FinalRGB, float3(1.00, 0.70, 0.20), SelEdgeFactor);
    }

    // Selected / Hovered / Active-Mode Vertex Dots (pixel-accurate screen discs)
    float2 FragPx = In.ClipPosition.xy;
    float  D0     = distance(FragPx, In.TriScreen01.xy);
    float  D1     = distance(FragPx, In.TriScreen01.zw);
    float  D2     = distance(FragPx, In.TriScreen2);

    bool  VertSel0     = QueryBitSet2D(VertexSelectionBitmask, In.TriVertexIDs.x);
    bool  VertSel1     = QueryBitSet2D(VertexSelectionBitmask, In.TriVertexIDs.y);
    bool  VertSel2     = QueryBitSet2D(VertexSelectionBitmask, In.TriVertexIDs.z);
    float MinSelVertPx = 1.0e5;
    if (VertSel0) MinSelVertPx = min(MinSelVertPx, D0);
    if (VertSel1) MinSelVertPx = min(MinSelVertPx, D1);
    if (VertSel2) MinSelVertPx = min(MinSelVertPx, D2);

    uint  HovVert      = CanvasUniforms.ComponentHighlightIDs.z;
    float MinHovVertPx = 1.0e5;
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
            FinalRGB        = lerp(FinalRGB, float3(0.06, 0.07, 0.09), DotBorder * 0.90);
            FinalRGB        = lerp(FinalRGB, float3(0.80, 0.82, 0.86), DotInner * 0.92);
        }
    }
    if (MinHovVertPx < 5.5)
    {
        float HovDot = 1.0 - smoothstep(3.0, 4.4, MinHovVertPx);
        FinalRGB     = lerp(FinalRGB, float3(0.45, 0.78, 1.00), HovDot);
    }
    if (MinSelVertPx < 5.8)
    {
        float SelOuter = 1.0 - smoothstep(3.2, 4.5, MinSelVertPx);
        float SelInner = 1.0 - smoothstep(1.4, 2.4, MinSelVertPx);
        FinalRGB       = lerp(FinalRGB, float3(1.00, 0.62, 0.14), SelOuter);
        FinalRGB       = lerp(FinalRGB, float3(1.00, 0.94, 0.76), SelInner);
    }

    // GPU Pin Beacon (crimson-gold marker at EnablePinLock corners when PinOverlayOn == true)
    if (PinOverlayOn && PinMask != 0u)
    {
        float  PinDist = 1.0e5;
        if ((PinMask & 1u) != 0u) PinDist = min(PinDist, D0);
        if ((PinMask & 2u) != 0u) PinDist = min(PinDist, D1);
        if ((PinMask & 4u) != 0u) PinDist = min(PinDist, D2);
        float PinDisc   = 1.0 - smoothstep(3.2, 4.8, PinDist);
        float PinCenter = 1.0 - smoothstep(1.4, 2.5, PinDist);
        FinalRGB = lerp(FinalRGB, float3(0.98, 0.24, 0.30), PinDisc);
        FinalRGB = lerp(FinalRGB, float3(1.00, 0.92, 0.56), PinCenter);
    }

    Out.ColorRGBA = float4(FinalRGB, 1.0);
    Out.PickIDs   = uint4(In.IDs.x, In.IDs.y, ClosestAuthEdge, 0u);
    return Out;
}
