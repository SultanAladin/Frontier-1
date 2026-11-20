/*============================================================================================================================================
                                                           VECTORGUIDEOVERLAY.SLANG
============================================================================================================================================*/
// 🧩 Resolves 2x SSAA SurfacePreviewImage + StackedPick into FinalColorImage (RGBA8) & FinalPickImage (RGBA32UI), plus GPU vector guides/knots/pins/marquee.

struct VectorOverlayUniforms
{
    float4 ViewportCanvasPacked;   // x = ViewportW, y = ViewportH, z = PanOffsetX, w = PanOffsetY
    float4 TileRectPx;          // x = TileMinX, y = TileMinY, z = TileSpanPx, w = DocExtentPx
    float4 SelectedBoundsPx;    // x = CenterX, y = CenterY, z = WidthPx, w = HeightPx
    float4 SelectedRotationSnap;  // x = RotationRad, y = SnapStepPx, z = ZoomFraction, w = Reserved
    uint4  OverlayOptionBits;        // x = ShowGrid|(ShowAxes<<1)|(GhostBorders<<2)|(Seamless2x2<<3)|(StampMode<<4), y = SelectedLayerId, z = ActiveToolOrd, w = HoveredLayerId
    uint4  CountsActivePacked;     // x = KnotCount, y = PinCount, z = ActiveKnotIdx, w = ActivePinIdx
    float4 MarqueeRectPx;       // xy = MinCorner, zw = MaxCorner (active when w > y && z > x)
    float4 KnotPositions[64];   // xy = DocPx anchor, zw = DocPx outgoing tangent handle (or incoming if mirrored)
    float4 KnotInHandles[64];   // xy = DocPx incoming tangent handle, z = SelectedOrSmooth, w = SubpathBreak
    float4 PinPoints[12];       // xy = NormDocPos [0..1], z = ActiveMask, w = HoverMask
    float4 PinColors[12];       // rgb = Pin tint
};

[[vk::binding(0, 0)]] ConstantBuffer<VectorOverlayUniforms> OverlayUniforms;
[[vk::binding(1, 0)]] SamplerState                          SurfaceSampler;
[[vk::binding(2, 0)]] Texture2D<float4>                     SurfacePreviewTexture;
[[vk::binding(3, 0)]] Texture2D<uint4>                      StackedPickTexture;

struct OverlayVSOutput
{
    float4 ClipPosition : SV_Position;
    float2 ScreenPx     : TEXCOORD0;
};

struct OverlayFSOutput
{
    float4 ColorRGBA : SV_Target0;
    uint4  PickIDs   : SV_Target1;
};

[shader("vertex")]
OverlayVSOutput VertexEntry(uint VertexID : SV_VulkanVertexID)
{
    static const float2 Quad[6] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
        float2(-1.0, -1.0), float2(1.0,  1.0), float2(-1.0, 1.0)
    };
    float2 Ndc = Quad[VertexID % 6u];
    OverlayVSOutput Out;
    Out.ClipPosition = float4(Ndc, 0.5, 1.0);
    Out.ScreenPx     = float2(Ndc.x * 0.5 + 0.5, Ndc.y * 0.5 + 0.5) * OverlayUniforms.ViewportCanvasPacked.xy;
    return Out;
}

float2 DocToScreen(float2 DocPx)
{
    float2 TileOrigin = OverlayUniforms.TileRectPx.xy;
    float  TileSpan   = max(1.0, OverlayUniforms.TileRectPx.z);
    float  DocExtent  = max(1.0, OverlayUniforms.TileRectPx.w);
    return TileOrigin + (DocPx / DocExtent) * TileSpan;
}

float DistToSegment(float2 P, float2 A, float2 B)
{
    float2 AB = B - A;
    float Den = dot(AB, AB);
    if (Den <= 1.0e-5) return length(P - A);
    float T = saturate(dot(P - A, AB) / Den);
    return length(P - (A + AB * T));
}

[shader("fragment")]
OverlayFSOutput FragmentEntry(OverlayVSOutput In)
{
    float2 ScreenP    = In.ScreenPx;
    float2 TileOrigin = OverlayUniforms.TileRectPx.xy;
    float  TileSpan   = max(1.0, OverlayUniforms.TileRectPx.z);
    float  DocExtent  = max(1.0, OverlayUniforms.TileRectPx.w);
    uint OptionBits   = OverlayUniforms.OverlayOptionBits.x;

    bool ShowGrid     = (OptionBits & 1u) != 0u;
    bool ShowAxes     = (OptionBits & 2u) != 0u;
    bool GhostBorders = (OptionBits & 4u) != 0u;
    bool Seamless2x2  = (OptionBits & 8u) != 0u;

    float2 TileUV = (ScreenP - TileOrigin) / TileSpan;
    bool InsideTile = (TileUV.x >= 0.0 && TileUV.x <= 1.0 && TileUV.y >= 0.0 && TileUV.y <= 1.0);

    float3 ColorRGB = float3(0.062, 0.066, 0.078);
    uint4  OutPick  = uint4(0u, 0u, 0u, 0u);

    uint PickW = 1u, PickH = 1u;
    StackedPickTexture.GetDimensions(PickW, PickH);

    if (InsideTile)
    {
        // 2x2 SSAA box sample from SurfacePreviewTexture
        float2 TexelStep = float2(0.25 / max(1.0, float(PickW)), 0.25 / max(1.0, float(PickH)));
        float3 Sum = float3(0.0, 0.0, 0.0);
        Sum += SurfacePreviewTexture.SampleLevel(SurfaceSampler, TileUV + float2(-TexelStep.x, -TexelStep.y), 0.0).rgb;
        Sum += SurfacePreviewTexture.SampleLevel(SurfaceSampler, TileUV + float2( TexelStep.x, -TexelStep.y), 0.0).rgb;
        Sum += SurfacePreviewTexture.SampleLevel(SurfaceSampler, TileUV + float2(-TexelStep.x,  TexelStep.y), 0.0).rgb;
        Sum += SurfacePreviewTexture.SampleLevel(SurfaceSampler, TileUV + float2( TexelStep.x,  TexelStep.y), 0.0).rgb;
        ColorRGB = Sum * 0.25;

        int2 PickCoord = clamp(int2(TileUV * float2(PickW, PickH)), int2(0, 0), int2(int(PickW) - 1, int(PickH) - 1));
        if (Seamless2x2)
        {
            PickCoord = int2((PickCoord.x * 2) % int(PickW), (PickCoord.y * 2) % int(PickH));
        }
        OutPick = StackedPickTexture.Load(int3(PickCoord, 0));

        // Tile grid & axis overlays
        if (ShowGrid && OverlayUniforms.SelectedRotationSnap.y > 0.5)
        {
            float DocStep = max(4.0, OverlayUniforms.SelectedRotationSnap.y);
            float2 DocP   = TileUV * DocExtent;
            float2 GridD  = abs(frac(DocP / DocStep + 0.5) - 0.5) * (TileSpan / DocExtent * DocStep);
            float  LineAlpha = 1.0 - smoothstep(0.5, 1.3, min(GridD.x, GridD.y));
            ColorRGB = lerp(ColorRGB, float3(0.55, 0.60, 0.68), LineAlpha * 0.14);
        }

        if (ShowAxes)
        {
            float2 CenterDist = abs(ScreenP - (TileOrigin + TileSpan * 0.5));
            float  AxisAlpha  = 1.0 - smoothstep(0.6, 1.4, min(CenterDist.x, CenterDist.y));
            ColorRGB = lerp(ColorRGB, float3(0.94, 0.96, 0.81), AxisAlpha * 0.22);
        }

        if (Seamless2x2 && GhostBorders)
        {
            float2 HalfDist = abs(ScreenP - (TileOrigin + TileSpan * 0.5));
            float  SubBorder = 1.0 - smoothstep(0.6, 1.5, min(HalfDist.x, HalfDist.y));
            ColorRGB = lerp(ColorRGB, float3(0.46, 0.70, 0.85), SubBorder * 0.55);
        }

        // Hover & selected layer silhouette boundary highlight
        uint SelLayer = OverlayUniforms.OverlayOptionBits.y;
        uint HovLayer = OverlayUniforms.OverlayOptionBits.w;
        int2 PxR = int2(min(int(PickW) - 1, PickCoord.x + 1), PickCoord.y);
        int2 PxU = int2(PickCoord.x, min(int(PickH) - 1, PickCoord.y + 1));
        uint RightId = StackedPickTexture.Load(int3(PxR, 0)).x;
        uint UpId    = StackedPickTexture.Load(int3(PxU, 0)).x;

        if (SelLayer != 0u && ((OutPick.x == SelLayer) != (RightId == SelLayer) || (OutPick.x == SelLayer) != (UpId == SelLayer)))
        {
            ColorRGB = lerp(ColorRGB, float3(0.937, 0.957, 0.812), 0.78);
        }
        else if (HovLayer != 0u && HovLayer != SelLayer && ((OutPick.x == HovLayer) != (RightId == HovLayer) || (OutPick.x == HovLayer) != (UpId == HovLayer)))
        {
            ColorRGB = lerp(ColorRGB, float3(0.463, 0.698, 0.847), 0.75);
        }
    }
    else
    {
        // Ghosted 3x3 surrounding tile preview in Pattern mode
        if (GhostBorders)
        {
            float2 WrappedUV = frac(TileUV);
            bool InGhostBand = (TileUV.x >= -1.0 && TileUV.x <= 2.0 && TileUV.y >= -1.0 && TileUV.y <= 2.0);
            if (InGhostBand)
            {
                float3 GhostCol = SurfacePreviewTexture.SampleLevel(SurfaceSampler, WrappedUV, 0.0).rgb;
                ColorRGB = lerp(ColorRGB, GhostCol * 0.42, 0.36);
            }
        }
        // Subtle viewport dot grid
        float2 BgGrid = abs(frac(ScreenP / 24.0 + 0.5) - 0.5) * 24.0;
        if (length(BgGrid) < 1.2)
        {
            ColorRGB += float3(0.03, 0.035, 0.045);
        }
    }

    // Crisp outer tile border
    float2 BorderDist = min(abs(ScreenP - TileOrigin), abs(ScreenP - (TileOrigin + TileSpan)));
    float BorderEdge  = 1.0 - smoothstep(0.6, 1.5, min(BorderDist.x, BorderDist.y));
    if (ScreenP.x >= TileOrigin.x - 1.5 && ScreenP.x <= TileOrigin.x + TileSpan + 1.5 &&
        ScreenP.y >= TileOrigin.y - 1.5 && ScreenP.y <= TileOrigin.y + TileSpan + 1.5)
    {
        ColorRGB = lerp(ColorRGB, float3(0.937, 0.957, 0.812), BorderEdge * 0.48);
    }

    // Selected layer bounding frame
    if (OverlayUniforms.OverlayOptionBits.y != 0u && OverlayUniforms.SelectedBoundsPx.z > 2.0)
    {
        float2 BCenter = DocToScreen(OverlayUniforms.SelectedBoundsPx.xy);
        float2 BHalf   = 0.5 * (OverlayUniforms.SelectedBoundsPx.zw / DocExtent) * TileSpan;
        float  CosR    = cos(-OverlayUniforms.SelectedRotationSnap.x);
        float  SinR    = sin(-OverlayUniforms.SelectedRotationSnap.x);
        float2 RelS    = ScreenP - BCenter;
        float2 LocalB  = float2(RelS.x * CosR - RelS.y * SinR, RelS.x * SinR + RelS.y * CosR);
        float  BoxEdgeDist = max(abs(LocalB.x) - BHalf.x, abs(LocalB.y) - BHalf.y);
        if (abs(BoxEdgeDist) < 1.25 && abs(LocalB.x) <= BHalf.x + 2.0 && abs(LocalB.y) <= BHalf.y + 2.0)
        {
            ColorRGB = lerp(ColorRGB, float3(0.937, 0.957, 0.812), 0.72);
        }
    }

    // Bezier curve knots & tangent arms (EditPoints / Curve / Pen tools)
    uint KnotCount = min(OverlayUniforms.CountsActivePacked.x, 64u);
    uint ToolOrd   = OverlayUniforms.OverlayOptionBits.z;
    if (KnotCount > 0u && (ToolOrd == 1u || ToolOrd == 2u || ToolOrd == 3u))
    {
        for (uint K = 0u; K < KnotCount; ++K)
        {
            float2 AnchorS = DocToScreen(OverlayUniforms.KnotPositions[K].xy);
            float2 OutS    = DocToScreen(OverlayUniforms.KnotPositions[K].zw);
            float2 InS     = DocToScreen(OverlayUniforms.KnotInHandles[K].xy);

            float ArmD = min(DistToSegment(ScreenP, AnchorS, OutS), DistToSegment(ScreenP, AnchorS, InS));
            if (ArmD < 1.2)
            {
                ColorRGB = lerp(ColorRGB, float3(0.463, 0.698, 0.847), 0.85);
            }
            float AnchorD = length(ScreenP - AnchorS);
            bool ActiveKnotMatch = (K == OverlayUniforms.CountsActivePacked.z);
            if (AnchorD <= 5.5)
            {
                float3 KCol = ActiveKnotMatch ? float3(0.937, 0.957, 0.812) : float3(0.14, 0.16, 0.20);
                float3 Rim  = float3(0.937, 0.957, 0.812);
                ColorRGB = (AnchorD > 3.6) ? Rim : KCol;
                OutPick  = uint4(OverlayUniforms.OverlayOptionBits.y, K, 1u, 0u);
            }
            else if (min(length(ScreenP - OutS), length(ScreenP - InS)) <= 4.0)
            {
                ColorRGB = float3(0.463, 0.698, 0.847);
                OutPick  = uint4(OverlayUniforms.OverlayOptionBits.y, K, 1u, 0u);
            }
        }
    }

    // 12-Pin PointField handles (visible when FillSource == PointField or PointGradient tool active)
    uint PinCount = min(OverlayUniforms.CountsActivePacked.y, 12u);
    for (uint P = 0u; P < PinCount; ++P)
    {
        if (OverlayUniforms.PinPoints[P].z < 0.5) continue;
        float2 PinS = TileOrigin + OverlayUniforms.PinPoints[P].xy * TileSpan;
        float  Dist = length(ScreenP - PinS);
        bool ActivePinMatch = (P == OverlayUniforms.CountsActivePacked.w);
        if (Dist <= 8.5)
        {
            if (Dist > 6.0)
            {
                ColorRGB = ActivePinMatch ? float3(0.937, 0.957, 0.812) : float3(0.95, 0.96, 0.98);
            }
            else
            {
                ColorRGB = OverlayUniforms.PinColors[P].rgb;
            }
            OutPick = uint4(OverlayUniforms.OverlayOptionBits.y, P, 2u, 0u);
        }
    }

    // Live Marquee selection rectangle
    float4 Mq = OverlayUniforms.MarqueeRectPx;
    if (Mq.z > Mq.x + 2.0 && Mq.w > Mq.y + 2.0)
    {
        if (ScreenP.x >= Mq.x && ScreenP.x <= Mq.z && ScreenP.y >= Mq.y && ScreenP.y <= Mq.w)
        {
            ColorRGB = lerp(ColorRGB, float3(0.463, 0.698, 0.847), 0.14);
            float EdgeD = min(min(ScreenP.x - Mq.x, Mq.z - ScreenP.x), min(ScreenP.y - Mq.y, Mq.w - ScreenP.y));
            if (EdgeD <= 1.25)
            {
                ColorRGB = float3(0.463, 0.698, 0.847);
            }
        }
    }

    OverlayFSOutput Out;
    Out.ColorRGBA = float4(ColorRGB, 1.0);
    Out.PickIDs   = OutPick;
    return Out;
}
