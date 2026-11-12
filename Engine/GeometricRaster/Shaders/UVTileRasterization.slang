//============================================================================================================================================
//                                                          UVTILERASTERIZATION.SLANG
//============================================================================================================================================
// 2D UDIM UVTile parameter canvas shader rendering checker/custom backdrops, anti-aliased UV faces, GPU seam/pin beacons & 2D Pick IDs

struct UVCanvasVertexLayout
{
    float4 UVAndBaryXY;   // xy = ParameterUV (U, V), zw = Barycentric.xy
    float4 BaryZAndStats; // x = Barycentric.z, y = StretchMetric, z = SignedAreaSign, w = OverlapIndicator
    uint4  NetworkPickIDs;// x = InstanceSlot, y = FaceIndex, z = DirectedEdgeIndex, w = LayerOrd | (Pins<<16) | (Seams<<19)
};

struct UVCanvasUniformBlock
{
    float4 CanvasMinMaxUV;    // xy = VisibleMinUV, zw = VisibleMaxUV
    uint4  OverlayAndPick;    // x = DisplayMode, y = Checker|(Seam<<1)|(Pin<<2)|(Backdrop<<8), z = ActiveLayerOrd, w = HoveredFaceIdx
};

[[vk::binding(0, 0)]] ConstantBuffer<UVCanvasUniformBlock>  CanvasUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<UVCanvasVertexLayout> CanvasVertices;
[[vk::binding(2, 0)]] StructuredBuffer<uint>                 FaceSelectionBitmask;

struct UVVertexOutput
{
    float4 ClipPosition       : SV_Position;
    float2 ParameterUV        : TEXCOORD0;
    float3 Barycentric        : TEXCOORD1;
    float3 Diagnostics        : TEXCOORD2;
    nointerpolation uint4 IDs : TEXCOORD3;
};

struct UVFragmentOutput
{
    float4 ColorRGBA : SV_Target0;
    uint4  PickIDs   : SV_Target1;
};

float3 ResolveUVLayerColor(uint LayerOrdinal)
{
    if (LayerOrdinal == 1u) return float3(0.937, 0.957, 0.812); // #EFF4CF Ivory
    if (LayerOrdinal == 2u) return float3(0.620, 0.812, 1.000); // #9ECFFF Sky
    if (LayerOrdinal == 3u) return float3(0.608, 0.855, 0.773); // #9BDAC5 Mint
    if (LayerOrdinal == 4u) return float3(0.910, 0.760, 0.478); // #E8C27A Amber
    return float3(0.52, 0.56, 0.64);
}

float3 EvaluateBackdropTexture(uint BackdropPreset, float2 LocalUV)
{
    if (BackdropPreset == 1u) // ColorChecker Chart
    {
        int2 Patch = int2(floor(LocalUV * float2(6.0, 4.0)));
        uint Idx   = uint((Patch.y * 6 + Patch.x) & 7);
        float3 Swatch = float3(0.24, 0.26, 0.30);
        if (Idx == 0u) Swatch = float3(0.45, 0.32, 0.27);
        if (Idx == 1u) Swatch = float3(0.76, 0.58, 0.51);
        if (Idx == 2u) Swatch = float3(0.36, 0.48, 0.62);
        if (Idx == 3u) Swatch = float3(0.35, 0.43, 0.26);
        if (Idx == 4u) Swatch = float3(0.51, 0.50, 0.69);
        if (Idx == 5u) Swatch = float3(0.40, 0.74, 0.67);
        if (Idx == 6u) Swatch = float3(0.84, 0.49, 0.18);
        if (Idx == 7u) Swatch = float3(0.78, 0.22, 0.24);
        float2 Inner = frac(LocalUV * float2(6.0, 4.0));
        float  CellInterior = step(0.06, Inner.x) * step(Inner.x, 0.94) * step(0.08, Inner.y) * step(Inner.y, 0.92);
        return lerp(float3(0.09, 0.09, 0.11), Swatch * 0.55, CellInterior);
    }
    if (BackdropPreset == 2u) // TrimSheet Atlas
    {
        float Band = floor(LocalUV.y * 6.0);
        float Groove = smoothstep(0.0, 0.06, frac(LocalUV.y * 6.0)) * smoothstep(1.0, 0.94, frac(LocalUV.y * 6.0));
        float3 Tint = ((uint(Band) & 1u) != 0u) ? float3(0.20, 0.26, 0.34) : float3(0.28, 0.25, 0.19);
        return Tint * (0.55 + 0.45 * Groove);
    }
    if (BackdropPreset == 3u) // Brushed Aniso Pack
    {
        float Brush = 0.5 + 0.5 * sin(LocalUV.x * 180.0 + sin(LocalUV.y * 24.0) * 1.5);
        return lerp(float3(0.14, 0.16, 0.20), float3(0.28, 0.31, 0.38), Brush * 0.65);
    }
    return float3(0.072, 0.076, 0.088);
}

[shader("vertex")]
UVVertexOutput VertexEntry(uint VertexID : SV_VertexID)
{
    UVCanvasVertexLayout Rec = CanvasVertices[VertexID];
    float2 MinUV = CanvasUniforms.CanvasMinMaxUV.xy;
    float2 MaxUV = CanvasUniforms.CanvasMinMaxUV.zw;
    float2 Norm  = (Rec.UVAndBaryXY.xy - MinUV) / max(MaxUV - MinUV, float2(1.0e-5, 1.0e-5));

    UVVertexOutput Out;
    Out.ClipPosition = float4(Norm.x * 2.0 - 1.0, 1.0 - Norm.y * 2.0, 0.5, 1.0);
    Out.ParameterUV  = Rec.UVAndBaryXY.xy;
    Out.Barycentric  = float3(Rec.UVAndBaryXY.zw, Rec.BaryZAndStats.x);
    Out.Diagnostics  = Rec.BaryZAndStats.yzw;
    Out.IDs          = Rec.NetworkPickIDs;
    return Out;
}

[shader("fragment")]
UVFragmentOutput FragmentEntry(UVVertexOutput In)
{
    UVFragmentOutput Out;
    uint OverlayMask   = CanvasUniforms.OverlayAndPick.y;
    bool CheckerOn      = (OverlayMask & 1u) != 0u;
    bool SeamOverlayOn  = (OverlayMask & 2u) != 0u;
    bool PinOverlayOn   = (OverlayMask & 4u) != 0u;
    uint BackdropPreset = (OverlayMask >> 8u) & 0xFFu;

    // Background UDIM Checker / Backdrop & Grid Rasterization (NetworkPickIDs.x == 0u)
    if (In.IDs.x == 0u)
    {
        float2 UV     = In.ParameterUV;
        bool   InTile = (UV.x >= 0.0 && UV.y >= 0.0);
        float3 BackdropTone = InTile ? float3(0.072, 0.076, 0.088) : float3(0.050, 0.052, 0.060);

        if (InTile && BackdropPreset != 0u)
        {
            BackdropTone = EvaluateBackdropTexture(BackdropPreset, frac(UV));
        }
        else if (InTile && CheckerOn)
        {
            int2  Cell   = int2(floor(UV * 16.0));
            float Parity = float((Cell.x + Cell.y) & 1);
            BackdropTone       = lerp(float3(0.068, 0.072, 0.084), float3(0.098, 0.104, 0.120), Parity);
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

    bool Selected    = (FaceSelectionBitmask[FaceIdx >> 5u] & (1u << (FaceIdx & 31u))) != 0u;
    bool LayerActive = (CanvasUniforms.OverlayAndPick.z > 0u) && (LayerOrdinal == CanvasUniforms.OverlayAndPick.z);
    bool Hovered     = (CanvasUniforms.OverlayAndPick.w == FaceIdx);

    uint   DisplayMode = CanvasUniforms.OverlayAndPick.x;
    float3 FillColor   = ResolveUVLayerColor(LayerOrdinal) * 0.52;

    if (DisplayMode == 1u) // Stretch Heatmap
    {
        float Err = saturate(abs(In.Diagnostics.x - 1.0) * 2.2);
        FillColor = lerp(float3(0.16, 0.66, 0.42), float3(0.92, 0.24, 0.22), Err);
    }
    else if (DisplayMode == 2u) // Winding Flip / Overlap Check
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
    else if (Hovered)
    {
        FillColor = lerp(FillColor, float3(0.62, 0.82, 1.00), 0.28);
    }

    float3 BaryDeriv = max(fwidth(In.Barycentric), float3(1.0e-4));
    float3 EdgeDists = In.Barycentric / BaryDeriv;
    float  MinEdge   = min(EdgeDists.x, min(EdgeDists.y, EdgeDists.z));
    float  EdgeMask  = 1.0 - smoothstep(0.4, 1.45, MinEdge);
    float3 EdgeColor = (Selected || LayerActive) ? float3(0.98, 0.99, 0.88) : ResolveUVLayerColor(LayerOrdinal);

    float3 FinalRGB = lerp(FillColor, EdgeColor, EdgeMask * 0.92);

    // GPU Seam Highlight (coral-orange border on marked seam edges when SeamOverlayOn == true)
    if (SeamOverlayOn && SeamMask != 0u)
    {
        float SeamDist = 1.0e5;
        if ((SeamMask & 1u) != 0u) SeamDist = min(SeamDist, EdgeDists.x);
        if ((SeamMask & 2u) != 0u) SeamDist = min(SeamDist, EdgeDists.y);
        if ((SeamMask & 4u) != 0u) SeamDist = min(SeamDist, EdgeDists.z);
        float SeamStroke = 1.0 - smoothstep(0.6, 2.2, SeamDist);
        FinalRGB = lerp(FinalRGB, float3(1.00, 0.46, 0.18), SeamStroke);
    }

    // GPU Pin Beacon (crimson-gold marker at EnablePinLock corners when PinOverlayOn == true)
    if (PinOverlayOn && PinMask != 0u)
    {
        float3 CornerPx = (float3(1.0, 1.0, 1.0) - In.Barycentric) / BaryDeriv;
        float  PinDist  = 1.0e5;
        if ((PinMask & 1u) != 0u) PinDist = min(PinDist, CornerPx.x);
        if ((PinMask & 2u) != 0u) PinDist = min(PinDist, CornerPx.y);
        if ((PinMask & 4u) != 0u) PinDist = min(PinDist, CornerPx.z);
        float PinDisc = 1.0 - smoothstep(3.2, 4.8, PinDist);
        float PinCenter = 1.0 - smoothstep(1.4, 2.5, PinDist);
        FinalRGB = lerp(FinalRGB, float3(0.98, 0.24, 0.30), PinDisc);
        FinalRGB = lerp(FinalRGB, float3(1.00, 0.92, 0.56), PinCenter);
    }

    Out.ColorRGBA = float4(FinalRGB, 1.0);
    Out.PickIDs   = uint4(In.IDs.x, In.IDs.y, In.IDs.z, 0u);
    return Out;
}
