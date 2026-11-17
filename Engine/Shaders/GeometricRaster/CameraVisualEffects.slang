/*============================================================================================================================================
                                                          CAMERAVISUALEFFECTS.SLANG
============================================================================================================================================*/
// 🧩 GPU compute optics unit evaluating Thin-Lens Depth of Field, Chromatic Aberration, Baked 3-Archetype Lens Flare, Vignette & Guides

#define FRONTIER_ATMOSPHERE_EMBEDDED 1
#include "../Atmosphere/LensFlareCompute.slang"

struct GpuScreenDistortionAnchor
{
    float4 PosRadKind;         // x=ScreenU, y=ScreenV, z=RadiusUv, w=Kind(0=Heat,1=BlackHole,2=Glitch)
    float4 DepthStrengthParam; // x=LinearDepthM, y=Strength, z=SecondaryParam, w=Active(0/1)
};

struct CameraOpticsUniforms
{
    uint4  ResolutionMaskPacked;      // x = WidthPx, y = HeightPx, z = EffectMask, w = GhostCountShapePacked
    float4 LensFocusPacked;           // x = FocalLengthMm, y = ApertureFStop, z = FocusDistanceM, w = SensorHeightMm
    float4 FlarePositionGain;         // x = FlareU, y = FlareV, z = FlareIntensity, w = FlareSpread
    float4 GhostAberrationPacked;     // x = GhostIntensity, y = GhostSpacing, z = StarburstRotationRad, w = ChromaticStrength
    float4 VignetteBlurPacked;        // x = VignetteIntensity, y = VignetteRoundness, z = MaxBokehRadiusPx, w = GuideStyleOrdinal
    float4 HaloTintPacked;            // x = HaloIntensity, yzw = SolarTintRGB
    float4 MotionBlurPacked0;         // x = Style(0..2), y = Intensity, z = SampleCount, w = MaxBlurRadiusPx
    float4 MotionBlurPacked1;         // x = VelU, y = VelV, z = ShutterNorm, w = DirAngleRad
    float4 MotionBlurPacked2;         // x = ZoomCenterU, y = ZoomCenterV, z = RadialSpinAmount, w = AspectRatio
    float4 PrevViewClipRow0;
    float4 PrevViewClipRow1;
    float4 PrevViewClipRow2;
    float4 PrevViewClipRow3;
    float4 CameraOriginTanX;          // xyz = Eye, w = TanHalfFovX
    float4 CameraForwardTanY;         // xyz = Forward, w = TanHalfFovY
    float4 CameraRightClock;          // xyz = Right, w = EffectClockSeconds
    float4 CameraUpReprojValid;       // xyz = Up, w = HasPrevReprojection(0/1)
    float4 HeatShimmerPacked;         // x = Intensity, y = Frequency, z = RiseSpeed, w = ChromaticDispersion
    float4 BlackHolePacked0;          // x = LensingStrength, y = EventHorizonScale, z = AccretionGlow, w = ChromaticSplit
    float4 BlackHolePacked1;          // x = SwirlTwist, yzw = AccretionTintRGB
    float4 GlitchPacked;              // x = Intensity, y = BlockJitter, z = ScanlineTear, w = RgbSplit
    float4 DistortionMetaPacked;      // x = ActiveAnchorCount, y = HeatUseWorldAnchors, z = GlitchLocalizeToAnchors, w = GlitchRateHz
    GpuScreenDistortionAnchor Anchors[8];
};

// EffectMask bits (ResolutionMaskPacked.z):
// bit 0  (1u)    = DepthOfFieldEnabled
// bit 1  (2u)    = ChromaticAberrationEnabled
// bit 2  (4u)    = LensFlareEnabled
// bit 3  (8u)    = FlareAnamorphicEnabled
// bit 4  (16u)   = FlareStarburstEnabled
// bit 5  (32u)   = FlareGhostsEnabled
// bit 6  (64u)   = VignetteEnabled
// bit 7  (128u)  = FocusPlaneHighlightEnabled
// bit 8  (256u)  = MotionBlurEnabled
// bit 9  (512u)  = HeatShimmerEnabled
// bit 10 (1024u) = BlackHoleLensEnabled
// bit 11 (2048u) = GlitchEnabled

[[vk::binding(0, 0)]] ConstantBuffer<CameraOpticsUniforms> Optics;
[[vk::binding(1, 0)]] StructuredBuffer<uint>               SourcePixelsRGBA8;
[[vk::binding(2, 0)]] StructuredBuffer<float>              LinearDepthMeters;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint>             OutputPixelsRGBA8;
[[vk::binding(4, 0)]] StructuredBuffer<uint>               BakedLensFlareAtlasTexels;

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
    float2 PixelPos  = clamp(
        ClampedUV * float2(float(WidthPx), float(HeightPx)) - float2(0.5, 0.5),
        float2(0.0, 0.0),
        float2(float(WidthPx - 1u), float(HeightPx - 1u)));
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
    float FocalMm      = max(Optics.LensFocusPacked.x, 4.0);
    float FStop        = max(Optics.LensFocusPacked.y, 0.7);
    float FocusDistMm  = max(Optics.LensFocusPacked.z * 1000.0, FocalMm + 10.0);
    float SubjectMm    = max(DepthM * 1000.0, FocalMm + 10.0);
    float SensorHMm    = max(Optics.LensFocusPacked.w, 4.0);

    float PupilMm = FocalMm / FStop;
    float CoCMm   = abs(SubjectMm - FocusDistMm) / SubjectMm *
                    (PupilMm * FocalMm / max(FocusDistMm - FocalMm, 1.0));
    float CoCPx   = (CoCMm / SensorHMm) * float(HeightPx);
    return clamp(CoCPx, 0.0, max(Optics.VignetteBlurPacked.z, 1.0));
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
    float2 SourceUV = Optics.FlarePositionGain.xy;
    float  Gain     = max(Optics.FlarePositionGain.z, 0.0);
    float  Spread   = 0.35 + clamp(Optics.FlarePositionGain.w, 0.0, 1.5);
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
        uint  RayPairs = max(2u, (Optics.ResolutionMaskPacked.w >> 16u) & 0xFFu);
        float Angle    = atan2(Delta.y, Delta.x) - Optics.GhostAberrationPacked.z;
        float Spike    = pow(abs(cos(Angle * float(RayPairs))), 28.0) *
                         exp(-Dist / max(0.19 * Spread, 1.0e-3));
        Accum += float3(1.00, 0.84, 0.56) * (Spike * 0.65 * Gain);
    }

    if ((EffectMask & 32u) != 0u)
    {
        uint  GhostCount = Optics.ResolutionMaskPacked.w & 0xFFu;
        uint  Sides      = (Optics.ResolutionMaskPacked.w >> 8u) & 0xFFu;
        float GhostGain  = max(Optics.GhostAberrationPacked.x, 0.0);
        float Spacing    = 0.85 + clamp(Optics.GhostAberrationPacked.y, 0.0, 2.0) * 1.65;
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

float Hash11Gpu(float P)
{
    float X = frac(P * 0.1031);
    X *= X + 33.33;
    X *= X + X;
    return frac(X);
}

float Hash21Gpu(float2 P)
{
    float3 P3 = frac(float3(P.x, P.y, P.x) * 0.1031);
    P3 += dot(P3, float3(P3.y, P3.z, P3.x) + 33.33);
    return frac((P3.x + P3.y) * P3.z);
}

float ValueNoise2DGpu(float2 P)
{
    float2 I = floor(P);
    float2 F = frac(P);
    float2 U = F * F * (3.0 - 2.0 * F);
    float A = Hash21Gpu(I);
    float B = Hash21Gpu(I + float2(1.0, 0.0));
    float C = Hash21Gpu(I + float2(0.0, 1.0));
    float D = Hash21Gpu(I + float2(1.0, 1.0));
    return lerp(lerp(A, B, U.x), lerp(C, D, U.x), U.y);
}

void EvaluateSpatialDistortions(
    float2     UV,
    float      CenterDepth,
    float      Aspect,
    uint       EffectMask,
    out float2 OutSampleUV,
    out float2 OutChromaticOffsetUV,
    out float  OutBlackHoleCoreMask,
    out float3 OutAdditiveGlow)
{
    OutSampleUV           = UV;
    OutChromaticOffsetUV  = float2(0.0, 0.0);
    OutBlackHoleCoreMask  = 0.0;
    OutAdditiveGlow       = float3(0.0, 0.0, 0.0);

    float TimeSec      = Optics.CameraRightClock.w;
    uint  AnchorCount  = min(8u, uint(Optics.DistortionMetaPacked.x + 0.5));

    // 1. Heat Shimmer (world-linked convective plumes or fullscreen thermal mirage)
    if ((EffectMask & 512u) != 0u)
    {
        float HeatInt  = Optics.HeatShimmerPacked.x;
        float HeatFreq = Optics.HeatShimmerPacked.y;
        float RiseSpd  = Optics.HeatShimmerPacked.z;
        float HeatDisp = Optics.HeatShimmerPacked.w;

        float Weight = 0.0;
        if (Optics.DistortionMetaPacked.y > 0.5 && AnchorCount > 0u)
        {
            bool FoundHeatAnchor = false;
            for (uint I = 0u; I < AnchorCount; ++I)
            {
                if (uint(Optics.Anchors[I].PosRadKind.w + 0.5) == 0u)
                {
                    FoundHeatAnchor = true;
                    float2 CPos  = Optics.Anchors[I].PosRadKind.xy;
                    float  Rad   = max(0.02, Optics.Anchors[I].PosRadKind.z);
                    float  ADep  = Optics.Anchors[I].DepthStrengthParam.x;
                    float  Str   = Optics.Anchors[I].DepthStrengthParam.y;
                    if (ADep <= CenterDepth + 0.50)
                    {
                        float2 Rel = float2((UV.x - CPos.x) * Aspect, UV.y - CPos.y);
                        // Plume extends upward in screen space (negative Y)
                        float2 PlumeRel = float2(Rel.x * 1.35, (Rel.y + Rad * 0.25) * 0.72);
                        float  D = length(PlumeRel) / Rad;
                        float  LocalW = (1.0 - smoothstep(0.18, 1.0, D)) * Str;
                        Weight = max(Weight, LocalW);
                    }
                }
            }
            if (!FoundHeatAnchor)
            {
                Weight = 1.0;
            }
        }
        else
        {
            Weight = 1.0;
        }

        if (Weight > 1.0e-4)
        {
            float2 FlowUV = float2(UV.x * Aspect, UV.y) * HeatFreq + float2(0.0, TimeSec * RiseSpd);
            float  N1 = ValueNoise2DGpu(FlowUV) * 2.0 - 1.0;
            float  N2 = ValueNoise2DGpu(FlowUV * 2.15 + float2(5.2, TimeSec * RiseSpd * 0.85)) * 2.0 - 1.0;
            float  WaveX = sin(UV.y * HeatFreq * 3.1 + TimeSec * RiseSpd * 3.4) * 0.45 + N1 * 0.55;
            float  WaveY = cos(UV.x * Aspect * HeatFreq * 2.4 - TimeSec * RiseSpd * 2.7) * 0.35 + N2 * 0.65;
            float2 Offset = float2(WaveX, WaveY) * (HeatInt * Weight * 0.014);
            OutSampleUV += Offset;
            OutChromaticOffsetUV += Offset * (HeatDisp * 0.65);
        }
    }

    // 2. Black Hole Gravitational Lensing (world-linked Einstein rings + Kerr frame dragging)
    if ((EffectMask & 1024u) != 0u)
    {
        float LensStr    = Optics.BlackHolePacked0.x;
        float HorizonRat = Optics.BlackHolePacked0.y;
        float GlowInt    = Optics.BlackHolePacked0.z;
        float ChromSplit = Optics.BlackHolePacked0.w;
        float Swirl      = Optics.BlackHolePacked1.x;
        float3 Tint      = Optics.BlackHolePacked1.yzw;

        bool UsedWorldBH = false;
        for (uint I = 0u; I < AnchorCount; ++I)
        {
            if (uint(Optics.Anchors[I].PosRadKind.w + 0.5) != 1u)
            {
                continue;
            }
            UsedWorldBH = true;
            float2 CPos = Optics.Anchors[I].PosRadKind.xy;
            float  Rad  = max(0.025, Optics.Anchors[I].PosRadKind.z);
            float  ADep = Optics.Anchors[I].DepthStrengthParam.x;
            float  Str  = Optics.Anchors[I].DepthStrengthParam.y;
            if (ADep > CenterDepth + 0.45)
            {
                continue;
            }

            float2 Delta = float2((UV.x - CPos.x) * Aspect, UV.y - CPos.y);
            float  Dist  = length(Delta);
            float  OuterR = Rad * 2.35;
            if (Dist < OuterR)
            {
                float2 Dir   = Delta / max(Dist, 1.0e-4);
                float2 Tang  = float2(-Dir.y, Dir.x);
                float  HorR  = Rad * HorizonRat;
                float  EinR  = Rad * (HorizonRat + 0.24);
                float  Fall  = 1.0 - smoothstep(HorR * 0.8, OuterR, Dist);

                // Einstein gravitational deflection pulls rays radially outward around the shadow (sampling outer background inward)
                float  DeflectMag = LensStr * Str * (EinR * EinR) / max(Dist, HorR * 0.85) * Fall * 0.85;
                float  SwirlMag   = Swirl * Str * exp(-max(0.0, Dist - HorR) / max(Rad * 0.42, 0.01)) * 0.14 * Fall;
                float2 DeflectUV  = float2((-Dir.x * DeflectMag + Tang.x * SwirlMag) / Aspect,
                                            -Dir.y * DeflectMag + Tang.y * SwirlMag);
                OutSampleUV += DeflectUV;
                OutChromaticOffsetUV += float2(Dir.x, Dir.y) * (ChromSplit * DeflectMag * 0.28);

                // Dark event horizon core
                float CoreMask = 1.0 - smoothstep(HorR * 0.82, HorR * 1.06, Dist);
                OutBlackHoleCoreMask = max(OutBlackHoleCoreMask, CoreMask);

                // Relativistic Doppler-beamed photon ring & swirling accretion halo
                float PhotonRing = exp(-pow((Dist - HorR * 1.12) / max(Rad * 0.075, 0.004), 2.0));
                float DiscTheta  = atan2(Delta.y * 1.85, Delta.x) + TimeSec * 2.4 - (Rad / max(Dist, 0.02)) * 1.8;
                float SpiralBand = 0.65 + 0.35 * sin(DiscTheta * 3.0);
                float Doppler    = 0.72 + 0.48 * (-Dir.x); // Approaching left limb brighter
                float Accretion  = exp(-pow((Dist - EinR * 1.08) / max(Rad * 0.28, 0.01), 2.0)) * SpiralBand * Doppler;
                OutAdditiveGlow += Tint * ((PhotonRing * 1.65 + Accretion * 0.75) * GlowInt * Str);
                OutAdditiveGlow += float3(1.0, 0.94, 0.86) * (PhotonRing * 0.65 * GlowInt * Str);
            }
        }

        if (!UsedWorldBH)
        {
            float2 CPos  = float2(0.5, 0.5);
            float  Rad   = 0.22;
            float2 Delta = float2((UV.x - CPos.x) * Aspect, UV.y - CPos.y);
            float  Dist  = length(Delta);
            if (Dist < Rad * 2.35)
            {
                float2 Dir   = Delta / max(Dist, 1.0e-4);
                float2 Tang  = float2(-Dir.y, Dir.x);
                float  HorR  = Rad * HorizonRat;
                float  EinR  = Rad * (HorizonRat + 0.24);
                float  Fall  = 1.0 - smoothstep(HorR * 0.8, Rad * 2.35, Dist);
                float  DeflectMag = LensStr * (EinR * EinR) / max(Dist, HorR * 0.85) * Fall * 0.85;
                float  SwirlMag   = Swirl * exp(-max(0.0, Dist - HorR) / (Rad * 0.42)) * 0.14 * Fall;
                OutSampleUV += float2((-Dir.x * DeflectMag + Tang.x * SwirlMag) / Aspect,
                                       -Dir.y * DeflectMag + Tang.y * SwirlMag);
                OutChromaticOffsetUV += Dir * (ChromSplit * DeflectMag * 0.28);
                OutBlackHoleCoreMask = max(OutBlackHoleCoreMask, 1.0 - smoothstep(HorR * 0.82, HorR * 1.06, Dist));
                float PhotonRing = exp(-pow((Dist - HorR * 1.12) / (Rad * 0.075), 2.0));
                float Doppler    = 0.72 + 0.48 * (-Dir.x);
                float Accretion  = exp(-pow((Dist - EinR * 1.08) / (Rad * 0.28), 2.0)) * Doppler;
                OutAdditiveGlow += Tint * ((PhotonRing * 1.65 + Accretion * 0.75) * GlowInt);
                OutAdditiveGlow += float3(1.0, 0.94, 0.86) * (PhotonRing * 0.65 * GlowInt);
            }
        }
    }

    // 3. Digital / Holographic Glitch (world-localized or full-screen camera shear + RGB desync)
    if ((EffectMask & 2048u) != 0u)
    {
        float GInt   = Optics.GlitchPacked.x;
        float BlockJ = Optics.GlitchPacked.y;
        float Tear   = Optics.GlitchPacked.z;
        float Split  = Optics.GlitchPacked.w;
        float RateHz = max(1.0, Optics.DistortionMetaPacked.w);
        float QuantT = floor(TimeSec * RateHz);

        float GWeight = 1.0;
        if (Optics.DistortionMetaPacked.z > 0.5 && AnchorCount > 0u)
        {
            GWeight = 0.0;
            for (uint I = 0u; I < AnchorCount; ++I)
            {
                if (uint(Optics.Anchors[I].PosRadKind.w + 0.5) == 2u)
                {
                    float2 CPos = Optics.Anchors[I].PosRadKind.xy;
                    float  Rad  = max(0.02, Optics.Anchors[I].PosRadKind.z);
                    float  ADep = Optics.Anchors[I].DepthStrengthParam.x;
                    float  Str  = Optics.Anchors[I].DepthStrengthParam.y;
                    if (ADep <= CenterDepth + 0.50)
                    {
                        float D = length(float2((UV.x - CPos.x) * Aspect, UV.y - CPos.y)) / Rad;
                        GWeight = max(GWeight, (1.0 - smoothstep(0.45, 1.15, D)) * Str);
                    }
                }
            }
        }

        if (GWeight > 1.0e-4)
        {
            float BandRow   = floor(UV.y * 18.0);
            float BandNoise = Hash21Gpu(float2(BandRow, QuantT));
            float BlockGate = step(0.62, BandNoise);
            float BlockDisp = (Hash21Gpu(float2(BandRow + 17.0, QuantT * 1.37)) * 2.0 - 1.0) * BlockGate * BlockJ * 0.045;

            float FineRow   = floor(UV.y * 140.0);
            float FineNoise = Hash21Gpu(float2(FineRow, QuantT * 2.13));
            float TearGate  = step(0.78, FineNoise);
            float TearDisp  = (Hash11Gpu(FineRow * 0.31 + QuantT) * 2.0 - 1.0) * TearGate * Tear * 0.032;

            float TotalShear = (BlockDisp + TearDisp) * GInt * GWeight;
            OutSampleUV.x += TotalShear;
            OutChromaticOffsetUV.x += (Split * 0.012 + abs(TotalShear) * 0.45) * GInt * GWeight;

            float Scanline = 0.5 + 0.5 * sin(UV.y * float(Optics.ResolutionMaskPacked.y) * 1.57);
            float CyberTint = (BlockGate * 0.08 + TearGate * 0.10 * Scanline) * GInt * GWeight;
            OutAdditiveGlow += float3(0.08, 0.72, 0.96) * CyberTint;
        }
    }
}

float3 SampleDistortedColor(float2 UV, float2 ChromOffset, uint WidthPx, uint HeightPx)
{
    if (dot(ChromOffset, ChromOffset) > 1.0e-9)
    {
        float R = SampleBilinearLinear(UV + ChromOffset, WidthPx, HeightPx).r;
        float G = SampleBilinearLinear(UV, WidthPx, HeightPx).g;
        float B = SampleBilinearLinear(UV - ChromOffset, WidthPx, HeightPx).b;
        return float3(R, G, B);
    }
    return SampleBilinearLinear(UV, WidthPx, HeightPx).rgb;
}

float2 EvaluateMotionBlurVelocity(float2 UV, float CenterDepth, uint WidthPx, uint HeightPx)
{
    uint   Style      = uint(Optics.MotionBlurPacked0.x + 0.5);
    float  Intensity  = max(0.0, Optics.MotionBlurPacked0.y);
    float  MaxRadPx   = max(1.0, Optics.MotionBlurPacked0.w);
    float  Shutter    = clamp(Optics.MotionBlurPacked1.z, 0.05, 1.0);
    float  Aspect     = max(0.25, Optics.MotionBlurPacked2.w);
    float2 VelUV      = float2(0.0, 0.0);

    if (Style == 0u) // CameraVelocity: depth-aware 3D reprojection (fallback to packed UV delta)
    {
        if (Optics.CameraUpReprojValid.w > 0.5)
        {
            float NdcX = UV.x * 2.0 - 1.0;
            float NdcY = 1.0 - UV.y * 2.0;
            float3 RayDir = normalize(
                Optics.CameraForwardTanY.xyz +
                Optics.CameraRightClock.xyz * (NdcX * Optics.CameraOriginTanX.w) +
                Optics.CameraUpReprojValid.xyz * (NdcY * Optics.CameraForwardTanY.w));
            float  ForwardCos = max(0.05, dot(RayDir, Optics.CameraForwardTanY.xyz));
            float3 WorldPos = Optics.CameraOriginTanX.xyz + RayDir * (max(0.05, CenterDepth) / ForwardCos);
            float4 WorldH   = float4(WorldPos, 1.0);
            float4 PrevClip = float4(
                dot(Optics.PrevViewClipRow0, WorldH),
                dot(Optics.PrevViewClipRow1, WorldH),
                dot(Optics.PrevViewClipRow2, WorldH),
                dot(Optics.PrevViewClipRow3, WorldH));
            if (abs(PrevClip.w) > 1.0e-4)
            {
                float2 PrevNdc = PrevClip.xy / PrevClip.w;
                float2 PrevUV  = float2(0.5 + 0.5 * PrevNdc.x, 0.5 + 0.5 * PrevNdc.y);
                VelUV = (PrevUV - UV) * (Intensity * Shutter);
            }
        }
        else
        {
            VelUV = Optics.MotionBlurPacked1.xy * (Intensity * Shutter);
        }
    }
    else if (Style == 1u) // DirectionalShutter: cinema shutter-angle directional streak
    {
        float2 RawVel = Optics.MotionBlurPacked1.xy;
        if (dot(RawVel, RawVel) < 1.0e-8)
        {
            float Ang = Optics.MotionBlurPacked1.w;
            RawVel = float2(cos(Ang), sin(Ang)) * 0.030;
        }
        VelUV = RawVel * (Intensity * Shutter);
    }
    else // Style == 2u: RadialZoom + tangential speed swirl
    {
        float2 Center = Optics.MotionBlurPacked2.xy;
        float2 Delta  = UV - Center;
        float2 Tang   = float2(-Delta.y / Aspect, Delta.x * Aspect);
        float  Spin   = Optics.MotionBlurPacked2.z;
        VelUV = (Delta + Tang * Spin) * (0.14 * Intensity * Shutter);
    }

    // Clamp blur vector length to MaxBlurRadiusPx
    float2 PixelVel = VelUV * float2(float(WidthPx), float(HeightPx));
    float  PixLen   = length(PixelVel);
    if (PixLen > MaxRadPx && PixLen > 1.0e-5)
    {
        VelUV *= (MaxRadPx / PixLen);
    }
    return VelUV;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint WidthPx  = Optics.ResolutionMaskPacked.x;
    uint HeightPx = Optics.ResolutionMaskPacked.y;
    if (DispatchThreadID.x >= WidthPx || DispatchThreadID.y >= HeightPx)
    {
        return;
    }

    uint   PixelIdx   = DispatchThreadID.y * WidthPx + DispatchThreadID.x;
    uint   EffectMask = Optics.ResolutionMaskPacked.z;
    float2 UV         = (float2(DispatchThreadID.xy) + 0.5) / float2(float(WidthPx), float(HeightPx));
    float  Aspect     = float(WidthPx) / max(float(HeightPx), 1.0);

    float4 PrimarySample = UnpackRGBA8Linear(SourcePixelsRGBA8[PixelIdx]);
    float  CenterDepth   = LinearDepthMeters[PixelIdx];

    float2 SampleUV            = UV;
    float2 DistortChromOffset  = float2(0.0, 0.0);
    float  BlackHoleCoreMask   = 0.0;
    float3 DistortAdditiveGlow = float3(0.0, 0.0, 0.0);
    if ((EffectMask & (512u | 1024u | 2048u)) != 0u)
    {
        EvaluateSpatialDistortions(
            UV,
            CenterDepth,
            Aspect,
            EffectMask,
            SampleUV,
            DistortChromOffset,
            BlackHoleCoreMask,
            DistortAdditiveGlow);
    }

    float3 ColorLin = SampleDistortedColor(SampleUV, DistortChromOffset, WidthPx, HeightPx);

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
                float2 OffUV = SampleUV + float2(cos(Theta), sin(Theta)) * R * Texel;
                uint SX = clamp(uint(OffUV.x * float(WidthPx)),  0u, WidthPx - 1u);
                uint SY = clamp(uint(OffUV.y * float(HeightPx)), 0u, HeightPx - 1u);
                float SampleDepth = LinearDepthMeters[SY * WidthPx + SX];
                float SampleCoC   = EvaluateThinLensCoCPixels(SampleDepth, HeightPx);
                float Weight      = (SampleDepth >= CenterDepth - 0.05) ? 1.0 : saturate(SampleCoC / max(CoCPx, 0.5));
                Sum  += SampleDistortedColor(OffUV, DistortChromOffset, WidthPx, HeightPx) * Weight;
                WSum += Weight;
            }
            ColorLin = Sum / max(WSum, 1.0e-4);
        }
    }

    if ((EffectMask & 2u) != 0u)
    {
        float2 Centered = SampleUV - float2(0.5, 0.5);
        float  RadSq    = dot(Centered * float2(Aspect, 1.0), Centered * float2(Aspect, 1.0));
        float2 Shift    = Centered * (RadSq * Optics.GhostAberrationPacked.w * 0.035);
        float  RedCh    = SampleBilinearLinear(SampleUV + Shift + DistortChromOffset, WidthPx, HeightPx).r;
        float  BlueCh   = SampleBilinearLinear(SampleUV - Shift - DistortChromOffset, WidthPx, HeightPx).b;
        ColorLin        = float3(RedCh, ColorLin.g, BlueCh);
    }

    if ((EffectMask & 256u) != 0u)
    {
        float2 MotionVel = EvaluateMotionBlurVelocity(UV, CenterDepth, WidthPx, HeightPx);
        float  VelPix    = length(MotionVel * float2(float(WidthPx), float(HeightPx)));
        if (VelPix > 0.35)
        {
            uint   Taps = clamp(uint(Optics.MotionBlurPacked0.z + 0.5), 4u, 24u);
            float3 Accum = float3(0.0, 0.0, 0.0);
            float  WAcc  = 0.0;
            for (uint T = 0u; T < Taps; ++T)
            {
                float Frac = (float(T) / float(max(Taps - 1u, 1u))) - 0.5;
                float W    = 1.0 - abs(Frac) * 0.65;
                float2 TapUV = SampleUV + MotionVel * Frac;
                Accum += SampleDistortedColor(TapUV, DistortChromOffset, WidthPx, HeightPx) * W;
                WAcc  += W;
            }
            ColorLin = Accum / max(WAcc, 1.0e-4);
        }
    }

    // Apply black-hole event horizon photon sphere darkening & relativistic accretion / glitch additive luminescence
    if ((EffectMask & (512u | 1024u | 2048u)) != 0u)
    {
        ColorLin = lerp(ColorLin, float3(0.005, 0.006, 0.010), saturate(BlackHoleCoreMask));
        ColorLin += DistortAdditiveGlow;
    }

    if ((EffectMask & 4u) != 0u)
    {
        uint StyleIdx = (Optics.ResolutionMaskPacked.w >> 24u) & 0x0Fu;
        if (StyleIdx > 2u)
        {
            StyleIdx = ((EffectMask & 8u) != 0u) ? 0u : (((EffectMask & 16u) != 0u) ? 1u : 2u);
        }
        LensFlareUniformBlock LF;
        LF.SourceScreenGain = float4(
            Optics.FlarePositionGain.x,
            Optics.FlarePositionGain.y,
            max(Optics.FlarePositionGain.z, 0.0),
            max(0.25, Optics.FlarePositionGain.w));
        LF.OpticsStyleHalo = float4(
            float(StyleIdx),
            max(Optics.HaloTintPacked.x, 0.0),
            ((EffectMask & 32u) != 0u) ? max(Optics.GhostAberrationPacked.x, 0.0) : 0.0,
            max(0.2, Optics.GhostAberrationPacked.y));
        LF.GhostRotationTint = float4(
            ((EffectMask & 32u) != 0u) ? float(Optics.ResolutionMaskPacked.w & 0xFFu) : 0.0,
            Optics.GhostAberrationPacked.z,
            float((Optics.ResolutionMaskPacked.w >> 8u) & 0xFFu),
            1.0);
        LF.SourceTintAspect = float4(
            Optics.HaloTintPacked.yzw,
            Aspect);
        ColorLin += EvaluateBakedLensFlareGpu(LF, BakedLensFlareAtlasTexels, UV);
    }

    if ((EffectMask & 64u) != 0u)
    {
        float2 VigVec    = (UV - float2(0.5, 0.5)) * float2(lerp(1.0, Aspect, Optics.VignetteBlurPacked.y), 1.0) * 1.45;
        float  CosTheta2 = 1.0 / (1.0 + dot(VigVec, VigVec));
        float  Cos4      = CosTheta2 * CosTheta2;
        ColorLin *= lerp(1.0, Cos4, saturate(Optics.VignetteBlurPacked.x));
    }

    if ((EffectMask & 128u) != 0u)
    {
        float FocusDist = Optics.LensFocusPacked.z;
        float Band      = 1.0 - saturate(abs(CenterDepth - FocusDist) / max(0.035 * FocusDist, 0.02));
        ColorLin = lerp(ColorLin, float3(0.22, 0.92, 0.72), Band * 0.45);
    }

    uint GuideStyle = uint(Optics.VignetteBlurPacked.w + 0.5);
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
