//==========================================================================================================================================
//                                                        DYNAMICMASKSEAM.SLANG
//==========================================================================================================================================
// Exact 256-byte GPU uniform block matching C++20 DynamicParameterBlock + spatial sweep, impact pulse, lidar scan & voxel cutout masks

module DynamicMaskSeam;

import ProceduralFieldGenerators;

// Exact 256-byte (16 x float4) GPU uniform block matching DynamicParameterBlock.h byte-for-byte
public struct GpuDynamicParameterBlock
{
    public float4 ClockSpeedProgressScale;      // x:ClockSeconds, y:AnimationSpeed, z:TransitionProgress, w:DetailScale
    public float4 ReliefOpacityRainSnow;        // x:ReliefStrength, y:OpacityScale, z:WeatherRainIntensity, w:WeatherSnowDepth
    public float4 PrimaryColorRgba;
    public float4 SecondaryColorRgba;
    public float4 TertiaryColorRgba;
    public float4 AccentRadianceRgba;
    public float4 ImpactOriginsAge0;
    public float4 ImpactOriginsAge1;
    public float4 ImpactOriginsAge2;
    public float4 ImpactOriginsAge3;
    public float4 AssetBoundsMinPad;
    public float4 AssetBoundsMaxPad;
    public float4 SweepAxisWidth;
    public float4 IorFilmRoughMetalBias;        // x:OpticalIor, y:ThinFilmNanometers, z:RoughnessBias, w:MetalnessBias
    public float4 SeamWaveTurbFilmSpread;       // x:SeamGlowIntensity, y:WaveAmplitude, z:TurbulenceAmount, w:ThinFilmSpread
    public uint4  PresetLayerChannelDriver;     // x:PresetOrdinal, y:ActiveLayerCount, z:ActiveChannelMask, w:PrimaryDriverOrdinal
};

public float3 NormalizeAssetCoordinateGpu(GpuDynamicParameterBlock Params, float3 LocalPos)
{
    float3 Center = 0.5 * (Params.AssetBoundsMinPad.xyz + Params.AssetBoundsMaxPad.xyz);
    float3 Half   = max(float3(1.0e-3), 0.5 * (Params.AssetBoundsMaxPad.xyz - Params.AssetBoundsMinPad.xyz));
    float  UniformHalf = max(Half.x, max(Half.y, Half.z));
    return (LocalPos - Center) / UniformHalf;
}

public float2 EvaluateGeodesicImpactPulsesGpu(GpuDynamicParameterBlock Params, float3 UnitNormal)
{
    float RippleSum = 0.0;
    float FlashSum  = 0.0;
    float4 Slots[4] = { Params.ImpactOriginsAge0, Params.ImpactOriginsAge1, Params.ImpactOriginsAge2, Params.ImpactOriginsAge3 };

    [unroll]
    for (int I = 0; I < 4; ++I)
    {
        float Age = Slots[I].w;
        if (Age < 0.0)
        {
            continue;
        }
        float3 OriginDir = normalize(Slots[I].xyz + float3(0.0, 0.0, 1.0e-5));
        float  CosAng    = clamp(dot(UnitNormal, OriginDir), -1.0, 1.0);
        float  GeoDist   = acos(CosAng);
        float  Cycle     = frac(Age * 0.46);
        RippleSum += exp(-pow((GeoDist - Cycle * 2.15) * 9.0, 2.0)) * (1.0 - Cycle);
        FlashSum  += exp(-GeoDist * GeoDist * 14.0) * exp(-Cycle * 4.2);
    }
    return float2(RippleSum, FlashSum);
}

public float3 EvaluateSweepTransitionMaskGpu(
    GpuDynamicParameterBlock Params,
    float3 NormPos,
    float2 SphUv)
{
    float Scale       = Params.ClockSpeedProgressScale.w;
    float SweepCenter = lerp(-0.85, 0.85, Params.ClockSpeedProgressScale.z);
    float Organic     = (Fbm2Gpu(SphUv * Scale * 1.45) - 0.5) * 0.14;
    HexCellGpuSample Hex = EvaluateHexLatticeGpu(float2(SphUv.x * Scale * 6.8, SphUv.y * Scale * 3.8));
    float HexJitter   = (Hex.CellHash - 0.5) * 0.08;
    float FrontDist   = dot(NormPos.xy, Params.SweepAxisWidth.xy) + Organic + HexJitter - SweepCenter;

    float BlendB      = smoothstep(-0.032, 0.032, FrontDist);
    float SeamLine    = exp(-pow(FrontDist * 28.0, 2.0));
    float HexEdge     = (1.0 - smoothstep(0.015, 0.085, Hex.EdgeDistance)) * exp(-pow(FrontDist * 9.5, 2.0));
    return float3(BlendB, SeamLine, HexEdge);
}

public float4 EvaluateLidarScanWakeGpu(
    GpuDynamicParameterBlock Params,
    float3 NormPos,
    float2 SphUv)
{
    float Scale      = Params.ClockSpeedProgressScale.w;
    float ScanY      = lerp(-0.78, 0.78, Params.ClockSpeedProgressScale.z);
    float BeamDistance = NormPos.y - ScanY;
    float LaserBeamPeak  = exp(-pow(BeamDistance * 26.0, 2.0));
    float WakeMask   = smoothstep(-0.85, 0.0, BeamDistance) * smoothstep(0.05, -0.01, BeamDistance);
    float LatLines   = 1.0 - smoothstep(0.03, 0.11, abs(frac(NormPos.y * Scale * 2.2) - 0.5) * 2.0);
    float LonLines   = 1.0 - smoothstep(0.03, 0.11, abs(frac(SphUv.x * Scale * 3.6) - 0.5) * 2.0);
    float GridWire   = max(LatLines, LonLines) * WakeMask;
    return float4(LaserBeamPeak, GridWire, WakeMask * 0.22, BeamDistance);
}
