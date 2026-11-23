/*============================================================================================================================================
                                                  DISPLAYRECONSTRUCTIONFILTER.SLANG
============================================================================================================================================*/
// 🧩 Deterministic display reconstruction: guided reduced-trace fill, clamped temporal history, and 5-step edge-aware a-trous filter
//    Reads raw Accumulation and writes ONLY Estimates — raw IEEE-754 float accumulation and digests remain untouched.

struct ReconstructionUniformBlock
{
    uint4  Extent;   // x = width, y = height, z = a-trous step [px], w = mode bits (1 history, 2 final, 4 gather)
    uint4  Routing;  // x = read offset, y = write offset (Estimates), z = history offset (Accumulation), w = sample count
    float4 Tuning;   // x = neighbourhood clamp gamma (0 = off), y = history span [spp], z = filter strength [0,1], w = luminance sigma / n
    float4 Geometry; // x = pixel footprint per metre, y = normal exponent, z = plane sigma, w = trace scale [px]
};

[[vk::binding(0, 0)]] ConstantBuffer<ReconstructionUniformBlock> Reconstruction;
[[vk::binding(1, 0)]] StructuredBuffer<float4>                   Accumulation;
[[vk::binding(2, 0)]] StructuredBuffer<float4>                   SurfaceFeatures;
[[vk::binding(3, 0)]] RWStructuredBuffer<float4>                 Estimates;

static const uint   ModeHistoryBit   = 1u;
static const uint   ModeFinalBit     = 2u;
static const uint   BackgroundSlotId = 0xffffu;
static const float  SharpRoughness   = 0.1f;
static const float3 LuminanceWeights = float3(0.2126f, 0.7152f, 0.0722f);
static const float  HoleWeight       = 0.25f;
static const float  SpatialWeight       = 3.0f;
static const float  SpatialFadedAt      = 16.0f;

uint TotalPixelCount()
{
    return Reconstruction.Extent.x * Reconstruction.Extent.y;
}

float3 ExactPixelMean(uint PixelIndex)
{
    float4 Sum = Accumulation[PixelIndex];
    return Sum.rgb / max(Sum.w, 1.0f);
}

uint PackedMaterialSlot(uint PixelIndex)
{
    return asuint(SurfaceFeatures[TotalPixelCount() + PixelIndex].w) & 0xffffu;
}

float PackedSurfaceRoughness(uint PixelIndex)
{
    return float(asuint(SurfaceFeatures[TotalPixelCount() + PixelIndex].w) >> 16u) / 65535.0f;
}

void WriteReconstructionResult(uint PixelIndex, float3 Radiance, float Variance)
{
    bool FinalStep = (Reconstruction.Extent.w & ModeFinalBit) != 0u;
    Estimates[Reconstruction.Routing.y + PixelIndex] = FinalStep ? float4(Radiance, 1.0f) : float4(Radiance, Variance);
}

float4 FillFromSampledNeighbours(int2 Pixel, uint Width, uint Height)
{
    int    Radius         = int(Reconstruction.Geometry.w);
    uint   Index          = uint(Pixel.y) * Width + uint(Pixel.x);
    uint   Slot           = PackedMaterialSlot(Index);
    float3 Normal         = SurfaceFeatures[TotalPixelCount() + Index].xyz;
    float4 Position       = SurfaceFeatures[Index];
    float  PlaneTolerance = Reconstruction.Geometry.x * max(Position.w, 1.0e-3f) * float(Radius) * 2.0f;

    float4 Guided = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 Plain  = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (int OffsetY = -Radius; OffsetY <= Radius; ++OffsetY)
    {
        for (int OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
        {
            int2 Tap = Pixel + int2(OffsetX, OffsetY);
            if (Tap.x < 0 || Tap.y < 0 || Tap.x >= int(Width) || Tap.y >= int(Height))
            {
                continue;
            }
            uint   TapIndex = uint(Tap.y) * Width + uint(Tap.x);
            float4 TapSum   = Accumulation[TapIndex];
            if (TapSum.w <= 0.0f)
            {
                continue;
            }
            float  Tent    = float((Radius + 1 - abs(OffsetX)) * (Radius + 1 - abs(OffsetY)));
            float3 TapMean = TapSum.rgb / TapSum.w;
            Plain += float4(TapMean, 1.0f) * Tent;

            float Edge = (PackedMaterialSlot(TapIndex) == Slot) ? 1.0f : 0.0f;
            if (Slot != BackgroundSlotId)
            {
                float4 TapPosition = SurfaceFeatures[TapIndex];
                Edge *= pow(max(dot(Normal, SurfaceFeatures[TotalPixelCount() + TapIndex].xyz), 0.0f), 8.0f);
                Edge *= exp(-abs(dot(TapPosition.xyz - Position.xyz, Normal)) / max(PlaneTolerance, 1.0e-6f));
            }
            Guided += float4(TapMean, 1.0f) * (Tent * Edge);
        }
    }
    if (Guided.w > 1.0e-3f * Plain.w)
    {
        return float4(Guided.rgb / Guided.w, Guided.w);
    }
    return float4(Plain.rgb / max(Plain.w, 1.0e-6f), Plain.w);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void GatherEstimate(uint3 InvocationId : SV_DispatchThreadID)
{
    uint Width  = Reconstruction.Extent.x;
    uint Height = Reconstruction.Extent.y;
    if (InvocationId.x >= Width || InvocationId.y >= Height)
    {
        return;
    }

    uint   PixelIndex = InvocationId.y * Width + InvocationId.x;
    float4 Sum        = Accumulation[PixelIndex];
    float  Samples    = Sum.w;
    float3 Mean       = Sum.rgb / max(Samples, 1.0f);
    uint   Slot       = PackedMaterialSlot(PixelIndex);

    bool  Hole     = (Samples <= 0.0f) && (Reconstruction.Geometry.w > 1.0f);
    float FadeTerm = clamp(1.0f - Samples / SpatialFadedAt, 0.0f, 1.0f);
    float Spatial     = SpatialWeight * (FadeTerm * FadeTerm) * (1.0f - Reconstruction.Tuning.z);
    if (Reconstruction.Geometry.w > 1.0f && (Hole || Spatial > 0.0f))
    {
        float4 Filled = FillFromSampledNeighbours(int2(InvocationId.xy), Width, Height);
        if (Hole)
        {
            Sum = float4(Filled.rgb * HoleWeight, HoleWeight);
        }
        else
        {
            Sum = Sum + float4(Filled.rgb * Spatial, Spatial);
        }
        Mean = Sum.rgb / Sum.w;
    }

    float3 Moment1        = float3(0.0f, 0.0f, 0.0f);
    float3 Moment2        = float3(0.0f, 0.0f, 0.0f);
    float  Taps           = 0.0f;
    float  SlotLuminance1 = 0.0f;
    float  SlotLuminance2 = 0.0f;
    float  SlotTaps       = 0.0f;

    for (int OffsetY = -1; OffsetY <= 1; ++OffsetY)
    {
        for (int OffsetX = -1; OffsetX <= 1; ++OffsetX)
        {
            int2 Tap = clamp(int2(InvocationId.xy) + int2(OffsetX, OffsetY), int2(0, 0), int2(int(Width) - 1, int(Height) - 1));
            uint TapIndex = uint(Tap.y) * Width + uint(Tap.x);
            if (Accumulation[TapIndex].w <= 0.0f && Reconstruction.Geometry.w > 1.0f)
            {
                continue;
            }
            float3 TapMean = ExactPixelMean(TapIndex);
            Taps    += 1.0f;
            Moment1 += TapMean;
            Moment2 += TapMean * TapMean;
            if (PackedMaterialSlot(TapIndex) == Slot)
            {
                float Lum = dot(TapMean, LuminanceWeights);
                SlotLuminance1 += Lum;
                SlotLuminance2 += Lum * Lum;
                SlotTaps       += 1.0f;
            }
        }
    }

    float  TapCount        = max(Taps, 1.0f);
    Moment1                = Moment1 / TapCount;
    float3 Deviation       = sqrt(max(Moment2 / TapCount - Moment1 * Moment1, float3(0.0f, 0.0f, 0.0f)));
    float  SlotCount       = max(SlotTaps, 1.0f);
    float  Luminance       = dot(Mean, LuminanceWeights);
    float  SlotMeanLum     = SlotLuminance1 / SlotCount;
    float  SpatialVariance = (SlotTaps > 0.0f) ? max(SlotLuminance2 / SlotCount - SlotMeanLum * SlotMeanLum, 0.0f) : (Luminance * Luminance);
    float  OwnLuminance    = dot(Accumulation[PixelIndex].rgb / max(Samples, 1.0f), LuminanceWeights);
    float  SampleVariance  = max(Accumulation[3u * TotalPixelCount() + PixelIndex].x / max(Samples, 1.0f) - OwnLuminance * OwnLuminance, 0.0f);
    float  Variance        = (Samples >= 4.0f) ? (SampleVariance / max(Samples, 1.0f)) : SpatialVariance;

    float3 Radiance = Mean;
    if ((Reconstruction.Extent.w & ModeHistoryBit) != 0u)
    {
        float4 History       = Accumulation[Reconstruction.Routing.z + PixelIndex];
        float  Remaining     = clamp(1.0f - Samples / max(Reconstruction.Tuning.y, 1.0f), 0.0f, 1.0f);
        float  HistoryWeight = History.w * Remaining * Remaining;
        float3 Carried       = History.rgb;
        if (Samples > 0.0f && Reconstruction.Tuning.x > 0.0f)
        {
            float Gamma = Reconstruction.Tuning.x;
            Carried = clamp(Carried, Moment1 - Gamma * Deviation, Moment1 + Gamma * Deviation);
        }
        float Weight = Sum.w + HistoryWeight;
        if (Weight > 0.0f)
        {
            Radiance = (Sum.rgb + Carried * HistoryWeight) / Weight;
            float Ratio = Sum.w / Weight;
            Variance = Variance * Ratio * Ratio;
        }
    }
    WriteReconstructionResult(PixelIndex, Radiance, Variance);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void FilterAtrous(uint3 InvocationId : SV_DispatchThreadID)
{
    uint Width  = Reconstruction.Extent.x;
    uint Height = Reconstruction.Extent.y;
    if (InvocationId.x >= Width || InvocationId.y >= Height)
    {
        return;
    }

    uint   PixelIndex = InvocationId.y * Width + InvocationId.x;
    uint   ReadOffset = Reconstruction.Routing.x;
    int    Step       = int(Reconstruction.Extent.z);
    float4 Centre     = Estimates[ReadOffset + PixelIndex];
    uint   Slot       = PackedMaterialSlot(PixelIndex);
    bool   FinalStep  = (Reconstruction.Extent.w & ModeFinalBit) != 0u;

    float4 Filtered     = Centre;
    bool   SharpSkipped = (Slot != BackgroundSlotId) && (PackedSurfaceRoughness(PixelIndex) < SharpRoughness) && (Step > 2);
    if (!SharpSkipped)
    {
        const float Gaussian[3] = { 0.25f, 0.125f, 0.0625f };
        float BlurredVariance = 0.0f;
        for (int OffsetY = -1; OffsetY <= 1; ++OffsetY)
        {
            for (int OffsetX = -1; OffsetX <= 1; ++OffsetX)
            {
                int2 Tap = clamp(int2(InvocationId.xy) + int2(OffsetX, OffsetY), int2(0, 0), int2(int(Width) - 1, int(Height) - 1));
                BlurredVariance += Gaussian[abs(OffsetX) + abs(OffsetY)] * Estimates[ReadOffset + uint(Tap.y) * Width + uint(Tap.x)].w;
            }
        }

        float  LuminanceTolerance = Reconstruction.Tuning.w * sqrt(max(BlurredVariance, 0.0f)) + 1.0e-4f;
        float  CentreLuminance    = dot(Centre.rgb, LuminanceWeights);
        float4 CentrePosition     = SurfaceFeatures[PixelIndex];
        float3 CentreNormal       = SurfaceFeatures[TotalPixelCount() + PixelIndex].xyz;
        float  PlaneTolerance     = Reconstruction.Geometry.z * Reconstruction.Geometry.x * max(CentrePosition.w, 1.0e-3f) * float(Step);

        const float Spline[3] = { 0.375f, 0.25f, 0.0625f };
        float  WeightSum   = 0.0f;
        float3 RadianceSum = float3(0.0f, 0.0f, 0.0f);
        float  VarianceSum = 0.0f;

        for (int OffsetY = -2; OffsetY <= 2; ++OffsetY)
        {
            for (int OffsetX = -2; OffsetX <= 2; ++OffsetX)
            {
                int2 Tap = int2(InvocationId.xy) + int2(OffsetX, OffsetY) * Step;
                if (Tap.x < 0 || Tap.y < 0 || Tap.x >= int(Width) || Tap.y >= int(Height))
                {
                    continue;
                }
                uint TapIndex = uint(Tap.y) * Width + uint(Tap.x);
                if (PackedMaterialSlot(TapIndex) != Slot)
                {
                    continue;
                }
                float4 TapEstimate = Estimates[ReadOffset + TapIndex];
                float  Weight      = Spline[abs(OffsetX)] * Spline[abs(OffsetY)];
                Weight *= exp(-abs(dot(TapEstimate.rgb, LuminanceWeights) - CentreLuminance) / LuminanceTolerance);
                if (Slot != BackgroundSlotId)
                {
                    float3 TapNormal   = SurfaceFeatures[TotalPixelCount() + TapIndex].xyz;
                    float3 TapPosition = SurfaceFeatures[TapIndex].xyz;
                    Weight *= pow(max(dot(CentreNormal, TapNormal), 0.0f), Reconstruction.Geometry.y);
                    Weight *= exp(-abs(dot(CentreNormal, TapPosition - CentrePosition.xyz)) / max(PlaneTolerance, 1.0e-6f));
                }
                WeightSum   += Weight;
                RadianceSum += TapEstimate.rgb * Weight;
                VarianceSum += TapEstimate.w * Weight * Weight;
            }
        }
        if (WeightSum > 0.0f)
        {
            Filtered = float4(RadianceSum / WeightSum, VarianceSum / (WeightSum * WeightSum));
        }
    }

    if (FinalStep)
    {
        float3 Unfiltered = Estimates[PixelIndex].rgb;
        WriteReconstructionResult(PixelIndex, lerp(Unfiltered, Filtered.rgb, Reconstruction.Tuning.z), 0.0f);
        return;
    }
    WriteReconstructionResult(PixelIndex, Filtered.rgb, Filtered.w);
}
