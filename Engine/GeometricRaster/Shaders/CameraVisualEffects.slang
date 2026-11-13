//============================================================================================================================================
//                                                        CAMERAVISUALEFFECTS.SLANG
//============================================================================================================================================
// GPU compute optics unit evaluating Thin-Lens Depth of Field, Chromatic Aberration, Multi-Layer Lens Flare, Vignette & Composition Guides

struct CameraOpticsUniforms
{
    uint4  ResolutionAndMask;      // x = WidthPx, y = HeightPx, z = EffectMask, w = GhostCountAndShape (low8=Count, mid8=Sides, high8=RayPairs)
    float4 LensAndFocus;           // x = FocalLengthMm, y = ApertureFStop, z = FocusDistanceM, w = SensorHeightMm
    float4 FlarePositionAndGain;   // x = FlareU, y = FlareV, z = FlareIntensity, w = FlareSpread
    float4 GhostAndAberration;     // x = GhostIntensity, y = GhostSpacing, z = StarburstRotationRad, w = ChromaticStrength
    float4 VignetteAndBlur;        // x = VignetteIntensity, y = VignetteRoundness, z = MaxBokehRadiusPx, w = GuideStyleOrdinal
};

// EffectMask bits (ResolutionAndMask.z):
// bit 0 (1u)   = DepthOfFieldEnabled
// bit 1 (2u)   = ChromaticAberrationEnabled
// bit 2 (4u)   = LensFlareEnabled
// bit 3 (8u)   = FlareAnamorphicEnabled
// bit 4 (16u)  = FlareStarburstEnabled
// bit 5 (32u)  = FlareGhostsEnabled
// bit 6 (64u)  = VignetteEnabled
// bit 7 (128u) = FocusPlaneHighlightEnabled

[[vk::binding(0, 0)]] ConstantBuffer<CameraOpticsUniforms> Optics;
[[vk::binding(1, 0)]] StructuredBuffer<uint>               SourcePixelsRGBA8;
[[vk::binding(2, 0)]] StructuredBuffer<float>              LinearDepthMeters;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint>             OutputPixelsRGBA8;

float4 UnpackRGBA8Linear(uint Packed)
{
    float4 Srgb = float4(
        float((Packed >>  0u) & 0xFFu) / 255.0,
        float((Packed >>  8u) & 0xFFu) / 255.0,
        float((Packed >> 16u) & 0xFFu) / 255.0,
        float((Packed >> 24u) & 0xFFu) / 255.0);
    return float4(pow(max(Srgb.rgb, float3(0.0)), float3(2.2)), Srgb.a);
}

uint PackLinearRGBA8(float3 LinearRGB, float Alpha)
{
    float3 Srgb = pow(saturate(LinearRGB), float3(1.0 / 2.2));
    uint R = uint(clamp(Srgb.r * 255.0 + 0.5, 0.0, 255.0));
    uint G = uint(clamp(Srgb.g * 255.0 + 0.5, 0.0, 255.0));
    uint B = uint(clamp(Srgb.b * 255.0 + 0.5, 0.0, 255.0));
    uint A = uint(clamp(Alpha  * 255.0 + 0.5, 0.0, 255.0));
    return R | (G << 8u) | (B << 16u) | (A << 24u);
}

float4 SampleBilinearLinear(float2 UV, uint WidthPx, uint HeightPx)
{
    float2 ClampedUV = clamp(UV, float2(0.0), float2(1.0));
    float2 PixelPos  = ClampedUV * float2(float(WidthPx - 1u), float(HeightPx - 1u));
    uint   X0 = uint(floor(PixelPos.x));
    uint   Y0 = uint(floor(PixelPos.y));
    uint   X1 = min(X0 + 1u, WidthPx - 1u);
    uint   Y1 = min(Y0 + 1u, HeightPx - 1u);
    float2 Fr = frac(PixelPos);

    float4 C00 = UnpackRGBA8Linear(SourcePixelsRGBA8[Y0 * WidthPx + X0]);
    float4 C10 = UnpackRGBA8Linear(SourcePixelsRGBA8[Y0 * WidthPx + X1]);
    float4 C01 = UnpackRGBA8Linear(SourcePixelsRGBA8[Y1 * WidthPx + X0]);
    float4 C11 = UnpackRGBA8Linear(SourcePixelsRGBA8[Y1 * WidthPx + X1]);
    return lerp(lerp(C00, C10, Fr.x), lerp(C01, C11, Fr.x), Fr.y);
}

float EvaluateThinLensCoCPixels(float DepthM, uint HeightPx)
{
    float FocalMm      = max(Optics.LensAndFocus.x, 4.0);
    float FStop        = max(Optics.LensAndFocus.y, 0.7);
    float FocusDistMm  = max(Optics.LensAndFocus.z * 1000.0, FocalMm + 10.0);
    float SubjectMm    = max(DepthM * 1000.0, FocalMm + 10.0);
    float SensorHMm    = max(Optics.LensAndFocus.w, 4.0);

    float PupilMm = FocalMm / FStop;
    float CoCMm   = abs(SubjectMm - FocusDistMm) / SubjectMm *
                    (PupilMm * FocalMm / max(FocusDistMm - FocalMm, 1.0));
    float CoCPx   = (CoCMm / SensorHMm) * float(HeightPx);
    return clamp(CoCPx, 0.0, max(Optics.VignetteAndBlur.z, 1.0));
}

float PolygonalDistance(float2 Local, uint Sides)
{
    if (Sides < 3u)
    {
        return length(Local);
    }
    float Angle   = atan2(Local.y, Local.x) + 0.5235988;
    float Sector  = 6.2831853 / float(Sides);
    float ModAng  = (Angle - Sector * floor(Angle / Sector)) - 0.5 * Sector;
    return length(Local) * cos(ModAng) / max(cos(0.5 * Sector), 0.25);
}

float3 EvaluateProceduralLensFlare(float2 UV, float AspectRatio, uint EffectMask)
{
    float2 SourceUV = Optics.FlarePositionAndGain.xy;
    float  Gain     = max(Optics.FlarePositionAndGain.z, 0.0);
    float  Spread   = 0.35 + clamp(Optics.FlarePositionAndGain.w, 0.0, 1.5);
    if (Gain <= 1.0e-4)
    {
        return float3(0.0);
    }

    float2 Delta = (UV - SourceUV) * float2(AspectRatio, 1.0);
    float  Dist  = length(Delta);
    float3 Accum = float3(0.0);

    float CenterGlow = exp(-Dist * Dist / max(0.00035 * Spread, 1.0e-6));
    float CoronaGlow = exp(-Dist / max(0.11 * Spread, 1.0e-4));
    Accum += float3(1.00, 0.96, 0.88) * (CenterGlow * 1.45 * Gain);
    Accum += float3(0.95, 0.68, 0.36) * (CoronaGlow * 0.42 * Gain);

    if ((EffectMask & 8u) != 0u)
    {
        float Horiz    = exp(-abs(Delta.y) / max(0.009 * Spread, 1.0e-4)) *
                         exp(-abs(Delta.x) / max(0.58 * Spread, 1.0e-3));
        float FineLine = exp(-abs(Delta.y) / 0.0025) * exp(-abs(Delta.x) / max(0.42 * Spread, 1.0e-3));
        Accum += float3(0.18, 0.48, 1.00) * (Horiz * 0.72 * Gain);
        Accum += float3(0.55, 0.82, 1.00) * (FineLine * 0.95 * Gain);
    }

    if ((EffectMask & 16u) != 0u)
    {
        uint  RayPairs = max(2u, (Optics.ResolutionAndMask.w >> 16u) & 0xFFu);
        float Angle    = atan2(Delta.y, Delta.x) - Optics.GhostAndAberration.z;
        float Spike    = pow(abs(cos(Angle * float(RayPairs))), 28.0) *
                         exp(-Dist / max(0.19 * Spread, 1.0e-3));
        Accum += float3(1.00, 0.84, 0.56) * (Spike * 0.65 * Gain);
    }

    if ((EffectMask & 32u) != 0u)
    {
        uint  GhostCount = Optics.ResolutionAndMask.w & 0xFFu;
        uint  Sides      = (Optics.ResolutionAndMask.w >> 8u) & 0xFFu;
        float GhostGain  = max(Optics.GhostAndAberration.x, 0.0);
        float Spacing    = 0.85 + clamp(Optics.GhostAndAberration.y, 0.0, 2.0) * 1.65;
        float2 AxisVec   = float2(0.5, 0.5) - SourceUV;

        const float3 CoatingTints[4] = {
            float3(0.32, 0.82, 0.56),
            float3(0.42, 0.58, 0.96),
            float3(0.95, 0.64, 0.34),
            float3(0.66, 0.44, 0.88)
        };

        for (uint Idx = 0u; Idx < min(GhostCount, 24u); ++Idx)
        {
            float  StepT   = 0.55 + (float(Idx + 1u) / float(max(GhostCount, 1u))) * Spacing;
            float2 GhostUV = SourceUV + AxisVec * StepT;
            float  Radius  = (0.024 + float((Idx * 7u) % 11u) * 0.0065) * (0.65 + 0.35 * Spread);
            float2 Local   = ((UV - GhostUV) * float2(AspectRatio, 1.0)) / max(Radius, 1.0e-4);
            float  ShapeD  = PolygonalDistance(Local, Sides);
            if (ShapeD <= 1.15)
            {
                float Rim  = smoothstep(0.58, 0.95, ShapeD) * (1.0 - smoothstep(0.96, 1.12, ShapeD));
                float Fill = (1.0 - smoothstep(0.0, 1.02, ShapeD)) * 0.24;
                Accum += CoatingTints[Idx & 3u] * ((Fill + Rim * 0.85) * GhostGain * 0.48);
            }
        }
    }

    return Accum;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint WidthPx  = Optics.ResolutionAndMask.x;
    uint HeightPx = Optics.ResolutionAndMask.y;
    if (DispatchThreadID.x >= WidthPx || DispatchThreadID.y >= HeightPx)
    {
        return;
    }

    uint   PixelIdx   = DispatchThreadID.y * WidthPx + DispatchThreadID.x;
    uint   EffectMask = Optics.ResolutionAndMask.z;
    float2 UV         = (float2(DispatchThreadID.xy) + 0.5) / float2(float(WidthPx), float(HeightPx));
    float  Aspect     = float(WidthPx) / max(float(HeightPx), 1.0);

    float4 PrimarySample = UnpackRGBA8Linear(SourcePixelsRGBA8[PixelIdx]);
    float3 ColorLin      = PrimarySample.rgb;
    float  CenterDepth   = LinearDepthMeters[PixelIdx];

    if ((EffectMask & 1u) != 0u)
    {
        float CoCPx = EvaluateThinLensCoCPixels(CenterDepth, HeightPx);
        if (CoCPx > 0.65)
        {
            float2 Texel = 1.0 / float2(float(WidthPx), float(HeightPx));
            float3 Sum   = ColorLin;
            float  WSum  = 1.0;
            const uint TapCount = 16u;
            for (uint Tap = 0u; Tap < TapCount; ++Tap)
            {
                float R     = sqrt((float(Tap) + 0.5) / float(TapCount)) * CoCPx;
                float Theta = float(Tap) * 2.39996323;
                float2 OffUV = UV + float2(cos(Theta), sin(Theta)) * R * Texel;
                uint SX = clamp(uint(OffUV.x * float(WidthPx)),  0u, WidthPx - 1u);
                uint SY = clamp(uint(OffUV.y * float(HeightPx)), 0u, HeightPx - 1u);
                float SampleDepth = LinearDepthMeters[SY * WidthPx + SX];
                float SampleCoC   = EvaluateThinLensCoCPixels(SampleDepth, HeightPx);
                float Weight      = (SampleDepth >= CenterDepth - 0.05) ? 1.0 : saturate(SampleCoC / max(CoCPx, 0.5));
                Sum  += SampleBilinearLinear(OffUV, WidthPx, HeightPx).rgb * Weight;
                WSum += Weight;
            }
            ColorLin = Sum / max(WSum, 1.0e-4);
        }
    }

    if ((EffectMask & 2u) != 0u)
    {
        float2 Centered = UV - float2(0.5, 0.5);
        float  RadSq    = dot(Centered * float2(Aspect, 1.0), Centered * float2(Aspect, 1.0));
        float2 Shift    = Centered * (RadSq * Optics.GhostAndAberration.w * 0.035);
        float  RedCh    = SampleBilinearLinear(UV + Shift, WidthPx, HeightPx).r;
        float  BlueCh   = SampleBilinearLinear(UV - Shift, WidthPx, HeightPx).b;
        ColorLin        = float3(RedCh, ColorLin.g, BlueCh);
    }

    if ((EffectMask & 4u) != 0u)
    {
        ColorLin += EvaluateProceduralLensFlare(UV, Aspect, EffectMask);
    }

    if ((EffectMask & 64u) != 0u)
    {
        float2 VigVec    = (UV - float2(0.5, 0.5)) * float2(lerp(1.0, Aspect, Optics.VignetteAndBlur.y), 1.0) * 1.45;
        float  CosTheta2 = 1.0 / (1.0 + dot(VigVec, VigVec));
        float  Cos4      = CosTheta2 * CosTheta2;
        ColorLin *= lerp(1.0, Cos4, saturate(Optics.VignetteAndBlur.x));
    }

    if ((EffectMask & 128u) != 0u)
    {
        float FocusDist = Optics.LensAndFocus.z;
        float Band      = 1.0 - saturate(abs(CenterDepth - FocusDist) / max(0.035 * FocusDist, 0.02));
        ColorLin = lerp(ColorLin, float3(0.22, 0.92, 0.72), Band * 0.45);
    }

    uint GuideStyle = uint(Optics.VignetteAndBlur.w + 0.5);
    if (GuideStyle > 0u)
    {
        float2 PxTol   = 1.15 / float2(float(WidthPx), float(HeightPx));
        bool   HitLine = false;
        if (GuideStyle == 1u || GuideStyle == 3u)
        {
            HitLine = HitLine ||
                      (abs(UV.x - 0.3333333) < PxTol.x) || (abs(UV.x - 0.6666667) < PxTol.x) ||
                      (abs(UV.y - 0.3333333) < PxTol.y) || (abs(UV.y - 0.6666667) < PxTol.y);
        }
        if (GuideStyle == 2u)
        {
            bool OuterGate = ((abs(UV.x - 0.05) < PxTol.x || abs(UV.x - 0.95) < PxTol.x) && UV.y >= 0.05 && UV.y <= 0.95) ||
                             ((abs(UV.y - 0.05) < PxTol.y || abs(UV.y - 0.95) < PxTol.y) && UV.x >= 0.05 && UV.x <= 0.95);
            bool InnerGate = ((abs(UV.x - 0.10) < PxTol.x || abs(UV.x - 0.90) < PxTol.x) && UV.y >= 0.10 && UV.y <= 0.90) ||
                             ((abs(UV.y - 0.10) < PxTol.y || abs(UV.y - 0.90) < PxTol.y) && UV.x >= 0.10 && UV.x <= 0.90);
            HitLine = HitLine || OuterGate || InnerGate;
        }
        if (GuideStyle == 3u)
        {
            bool Cross = (abs(UV.x - 0.5) < PxTol.x && abs(UV.y - 0.5) < 0.035) ||
                         (abs(UV.y - 0.5) < PxTol.y && abs(UV.x - 0.5) < 0.035 / Aspect);
            HitLine = HitLine || Cross;
        }
        if (HitLine)
        {
            ColorLin = lerp(ColorLin, float3(0.88, 0.92, 0.98), 0.38);
        }
    }

    OutputPixelsRGBA8[PixelIdx] = PackLinearRGBA8(ColorLin, PrimarySample.a);
}
