/*============================================================================================================================================
                                                          VECTORSHAREDLAYOUTS.SLANG
============================================================================================================================================*/
// 🧩 Shared GPU structures and procedural math for Frontier's Vulkan VectorGraphics & OpenPBR rasterization shaders.

#pragma once

struct VectorShapeVertex
{
    float4 PositionLocalUV; // xy = PositionPx in layer-local or document coords, zw = Normalized local UV [-0.5..0.5] or [0..1]
    uint4  Attributes;         // x = StrokeBit (0: fill, 1: stroke), y = ShapeOrdinal, z = LocalCoordBit (1: layer-local, 0: document-abs), w = ColorPackedRGBA
};

struct VectorLayerUniforms
{
    float4 GeometryBounds;     // x = CenterX, y = CenterY, z = WidthPx, w = HeightPx
    float4 StyleParams;        // x = RotationRad, y = Opacity, z = BlendOrdinal, w = FinishPresetOrdinal
    uint4  FillHeader;         // x = FillSourceMode (0:Solid,1:Linear,2:Radial,3:PointField), y = StopCount, z = StrokeSourceMode, w = LayerIdentity
    float4 FillAux;            // x = GradientAngleRad, y = StrokeWidthPx, z = FillEnabled (0/1), w = LayerOptionBits (bit0:MaskOn, bit1:MaskInvert, bit2:PatternMode, bit3:GroundMode)
    float4 FillTintA;
    float4 FillTintB;
    float4 StrokeTint;
    float4 GradientStops[8];   // xyz = Linear RGB, w = Offset [0..1]
    float4 PinCoords[12];      // xy = Normalized pin pos [0..1], z = InfluenceWeight, w = ActiveMask
    float4 PinColors[12];      // rgba = Pin tint
    float4 MaskParams;         // x = Mode (0:Noise,1:Radial,2:Stripe), y = Frequency, z = Threshold, w = Softness
    float4 MaskSeedOptions;   // x = float(Seed), y = BevelEdgeWearAmount, z = ActiveSelectionBit, w = Reserved
    float4 PbrScalars0;        // x = Roughness, y = Metalness, z = Height, w = NormalStr
    float4 PbrScalars1;        // x = Occlusion, y = Emissive, z = GlowPower, w = Clearcoat
    float4 PbrScalars2;        // x = CoatRough, y = Sheen, z = SheenRough, w = Specular
    float4 PbrScalars3;        // x = Anisotropy, y = Iridescence, z = IridFilm, w = Transmission
    float4 PbrScalars4;        // x = Subsurface, y = OpacityScalar, z = ExplicitMaskMaskBits, w = LayerExtentPx
    float4 SurfaceAlbedoTint;
    float4 SurfaceSpecTint;
    float4 SurfaceSheenTint;
    float4 SurfaceSubsurfTint;
    float4 ShapeLocalBounds;   // xy = minX/minY, zw = maxX/maxY in shape coordinate domain
};

struct VectorModifierEntry
{
    uint4 Header;       // x = Category (0..7), y = Enabled (0/1), z = SubMode (NoiseAlg/GridLayout/MirrorAxis/WarpMode), w = ModulateTarget
    float4 ScalarBlockA; // x = MinScale / Columns / RadialCount / EchoCopies / PosJitterPx / WarpAmp / ChipWear
    float4 ScalarBlockB; // x = MaxScale / Rows / RadiusPx / InsetStepPx / RotJitterDeg / Frequency
    float4 ScalarBlockC; // x = Rings / Dropout / SeedFloat / Reserved
    float4 TintBlock;    // rgba = TargetTintB or AltTint
};

struct VectorModifierDispatchParams
{
    uint4 CountsDimensionsPacked;   // x = ModifierCount, y = LayerIdentity, z = LayerIndex, w = MaxOutputInstances
    float4 CanvasMetrics;  // x = DocExtentPx (512.0), y = PatternTileWrap (1:Pattern, 0:Stamp), z = LayerCenterX, w = LayerCenterY
    float4 LayerExtentRotation; // x = WidthPx, y = HeightPx, z = RotationRad, w = Reserved
};

struct VectorInstanceEntry
{
    float4 TransformRow0;   // x = m00, y = m01, z = tx, w = scaleFactor
    float4 TransformRow1;   // x = m10, y = m11, z = ty, w = echoAlphaStep
    float4 ColorOpacityPacked; // rgb = modulated color or override, a = mixFactor (0 = keep base fill, 1 = full override)
    float4 ModParams;       // x = opacityMul, y = reliefMul, z = bevelWear, w = warpAmplitude
    uint4  PickMeta;        // x = LayerIdentity, y = InstanceOrdinal, z = WarpMode, w = OverrideTintBit
};

struct VectorSummaryMetrics
{
    uint InstanceCount;
    uint MinScaleFixed; // Fixed-point scale * 10000
    uint MaxScaleFixed; // Fixed-point scale * 10000
    uint ReservedMetric;
};

struct VectorRasterPushConstants
{
    uint4 Indices; // x = LayerIndex, y = InstanceIndex, z = ModeOptionBits (bit0:StrokeMode, bit1:PatternWrap), w = FirstVertexOffset
};

// Deterministic hash utilities for noise, scatter, and procedural masks
uint VectorHashU32(uint V)
{
    uint H = V ^ (V >> 16u);
    H *= 0x7FEB352Du;
    H ^= H >> 15u;
    H *= 0x846CA68Bu;
    H ^= H >> 16u;
    return H;
}

float VectorHashFloat(int2 Cell, uint Seed)
{
    uint H = VectorHashU32(uint(Cell.x * 73856093) ^ uint(Cell.y * 19349663) ^ Seed);
    return float(H & 0x00FFFFFFu) / 16777215.0;
}

float VectorLatticeNoise2D(float2 P, uint Seed)
{
    float2 I = floor(P);
    float2 F = frac(P);
    float2 U = F * F * (3.0 - 2.0 * F);

    float A = VectorHashFloat(int2(I), Seed);
    float B = VectorHashFloat(int2(I) + int2(1, 0), Seed);
    float C = VectorHashFloat(int2(I) + int2(0, 1), Seed);
    float D = VectorHashFloat(int2(I) + int2(1, 1), Seed);

    return lerp(lerp(A, B, U.x), lerp(C, D, U.x), U.y);
}

float VectorFbm2D(float2 P, uint Seed)
{
    float Sum = 0.0;
    float Amp = 0.5;
    float2 Pos = P;
    for (int Octave = 0; Octave < 4; ++Octave)
    {
        Sum += Amp * VectorLatticeNoise2D(Pos, Seed + uint(Octave) * 131u);
        Pos = Pos * 2.03 + float2(17.13, 9.41);
        Amp *= 0.5;
    }
    return Sum;
}

float VectorWorley2D(float2 P, uint Seed)
{
    float2 I = floor(P);
    float2 F = frac(P);
    float MinDist = 8.0;
    for (int Dy = -1; Dy <= 1; ++Dy)
    {
        for (int Dx = -1; Dx <= 1; ++Dx)
        {
            int2 Neighbor = int2(I) + int2(Dx, Dy);
            float Jx = VectorHashFloat(Neighbor, Seed + 11u);
            float Jy = VectorHashFloat(Neighbor, Seed + 37u);
            float2 Diff = float2(float(Dx) + Jx, float(Dy) + Jy) - F;
            MinDist = min(MinDist, dot(Diff, Diff));
        }
    }
    return saturate(1.0 - sqrt(MinDist));
}

float EvaluateNoiseAlgorithm(uint Alg, float2 NormCoord, float Frequency, uint Seed)
{
    float2 SampleP = NormCoord * max(0.25, Frequency);
    if (Alg == 1u) // Perlin-like smooth gradient
    {
        return VectorLatticeNoise2D(SampleP * 1.4, Seed);
    }
    if (Alg == 2u) // Worley cellular
    {
        return VectorWorley2D(SampleP * 1.2, Seed);
    }
    if (Alg == 3u) // Ridged multifractal
    {
        float N = VectorFbm2D(SampleP, Seed);
        return saturate(1.0 - abs(N * 2.0 - 1.0));
    }
    if (Alg == 4u) // Radial falloff
    {
        float R = length(NormCoord - float2(0.5, 0.5)) * 2.0;
        return saturate(1.0 - frac(R * max(0.5, Frequency * 0.35)));
    }
    return VectorFbm2D(SampleP, Seed);
}

float2 ApplyDomainWarpOffset(float2 PtPx, float Amplitude, uint WarpMode, float DocExtent)
{
    float2 Result = PtPx;
    if (abs(Amplitude) > 1.0e-4)
    {
        float SafeExtent = max(DocExtent, 1.0);
        float2 Norm = PtPx / SafeExtent;
        float2 Rel = Norm - float2(0.5, 0.5);
        float R = length(Rel);
        float SafeR = max(1.0e-4, R);

        float2 Delta = float2(0.0, 0.0);
        if (WarpMode == 0u) // Wave
        {
            Delta = float2(
                sin(Norm.y * 12.5663706) * Amplitude,
                cos(Norm.x * 12.5663706) * Amplitude * 0.65);
        }
        else if (WarpMode == 1u) // Swirl
        {
            float Ang = atan2(Rel.y, Rel.x + 1.0e-6) + (1.0 - saturate(R * 1.6)) * (Amplitude * 0.035);
            Delta = (float2(cos(Ang), sin(Ang)) * R - Rel) * SafeExtent;
        }
        else if (WarpMode == 2u) // Curl
        {
            Delta = float2(
                (VectorLatticeNoise2D(Norm * 4.0, 91u) - 0.5) * 2.0 * Amplitude,
                (VectorLatticeNoise2D(Norm * 4.0 + float2(5.2, 1.3), 173u) - 0.5) * 2.0 * Amplitude);
        }
        else // Pinch
        {
            float Factor = pow(max(1.0e-4, saturate(R * 2.0)), max(0.05, 1.0 + Amplitude * 0.015));
            Delta = ((Rel / SafeR) * Factor * 0.5 - Rel) * SafeExtent;
        }
        Result = PtPx + Delta;
    }
    return Result;
}
