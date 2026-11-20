/*====================================================================================================================================
                                                    DISPLAYRECONSTRUCTION.WGSL
====================================================================================================================================*/
// 🧩 Deterministic display reconstruction: clamped temporal history + edge-aware à-trous denoiser over the exact sums

// 📝 Reads the exact accumulation and never writes it, so digests, convergence and the converged image are untouched.
//    Every weight is a fixed function of the sums and the guide buffers — no random numbers, same input → same pixels.
//    Sequence per displayed frame (TracingDivision.EncodeReconstruction):
//      ① GatherEstimate   sums (+ reprojected history, clamped to the current neighbourhood) → estimate + variance
//      ② FilterAtrous ×5   steps 1, 2, 4, 8, 16 of a 5 × 5 B3-spline wavelet, stopped at normal, plane, material and
//                          luminance edges (variance-scaled); the last step mixes with ① by the sample-count strength
//    Regions of Reconstruction (N = pixels): [0,N) estimate, [N,3N) ping-pong, [3N,4N) display radiance (rgb, 1).

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ReconstructionUniforms
{
    Extent   : vec4u,   // width, height, à-trous step [px], mode bits (1 history, 2 final, 4 gather)
    Routing  : vec4u,   // read offset, write offset (Reconstruction), history offset (Accumulation), sample count
    Tuning   : vec4f,   // neighbourhood clamp γ (0 = off), history span [spp], filter strength [0,1], luminance σ (already ÷ n)
    Geometry : vec4f,   // pixel footprint per metre of distance, normal exponent, plane σ, trace scale [px]
}

@group(0) @binding(0) var<uniform>             Reconstruction   : ReconstructionUniforms;
@group(0) @binding(1) var<storage, read>       Accumulation     : array<vec4f>;
@group(0) @binding(2) var<storage, read>       SurfaceFeatures  : array<vec4f>;   // [0,N) position + distance, [N,2N) normal + packed slot/roughness
@group(0) @binding(3) var<storage, read_write> Estimates        : array<vec4f>;

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const ModeHistory    : u32 = 1u;
const ModeFinal      : u32 = 2u;
const BackgroundSlot : u32 = 0xffffu;
const SharpRoughness : f32 = 0.1;    // [-] - Sharper lobes keep reflection detail: only the two finest steps apply
const LuminanceWeights = vec3f(0.2126, 0.7152, 0.0722);
const HoleWeight     : f32 = 0.25;   // [samples] - Weight of an upsampled estimate against reprojected history
const PoolWeight     : f32 = 3.0;    // [samples] - Neighbourhood prior of a sparsely sampled pixel (reduced trace) …
const PoolFadedAt    : f32 = 16.0;   // [spp]     - … fading as (1 − n / 16)², gone at 16 samples per pixel


//------------------------------------------------------------------------------------------------------------------------
//                                                    SHARED
//------------------------------------------------------------------------------------------------------------------------

fn PixelCount() -> u32
{
    return Reconstruction.Extent.x * Reconstruction.Extent.y;
}

fn ExactMean(PixelIndex: u32) -> vec3f
{
    let Sum = Accumulation[PixelIndex];
    return Sum.rgb / max(Sum.w, 1.0);
}

fn PackedSlot(PixelIndex: u32) -> u32
{
    return bitcast<u32>(SurfaceFeatures[PixelCount() + PixelIndex].w) & 0xffffu;
}

fn PackedRoughness(PixelIndex: u32) -> f32
{
    return f32(bitcast<u32>(SurfaceFeatures[PixelCount() + PixelIndex].w) >> 16u) / 65535.0;
}

fn WriteResult(PixelIndex: u32, Radiance: vec3f, Variance: f32)
{
    let Final = (Reconstruction.Extent.w & ModeFinal) != 0u;
    Estimates[Reconstruction.Routing.y + PixelIndex] = select(vec4f(Radiance, Variance), vec4f(Radiance, 1.0), Final);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Reduced trace resolution: a pixel without its own sample yet takes a tent-weighted mean of sampled pixels within
//    ±scale, weighted by the full-resolution guides (same material, normal agreement, plane distance) — joint-bilateral
//    upsampling. If every guided weight vanishes, the plain tent mean is used. Returns (rgb, total weight).
fn FillFromNeighbours(Pixel: vec2i, Width: u32, Height: u32) -> vec4f
{
    let Radius   = i32(Reconstruction.Geometry.w);
    let Index    = u32(Pixel.y) * Width + u32(Pixel.x);
    let Slot     = PackedSlot(Index);
    let Normal   = SurfaceFeatures[PixelCount() + Index].xyz;
    let Position = SurfaceFeatures[Index];
    let PlaneTolerance = Reconstruction.Geometry.x * max(Position.w, 1.0e-3) * f32(Radius) * 2.0;
    var Guided = vec4f(0.0);
    var Plain  = vec4f(0.0);
    for (var OffsetY = -Radius; OffsetY <= Radius; OffsetY++)
    {
        for (var OffsetX = -Radius; OffsetX <= Radius; OffsetX++)
        {
            let Tap = Pixel + vec2i(OffsetX, OffsetY);
            if (Tap.x < 0 || Tap.y < 0 || Tap.x >= i32(Width) || Tap.y >= i32(Height)) { continue; }
            let TapIndex = u32(Tap.y) * Width + u32(Tap.x);
            let TapSum   = Accumulation[TapIndex];
            if (TapSum.w <= 0.0) { continue; }
            let Tent    = f32((Radius + 1 - abs(OffsetX)) * (Radius + 1 - abs(OffsetY)));
            let TapMean = TapSum.rgb / TapSum.w;
            Plain += vec4f(TapMean, 1.0) * Tent;
            var Edge = select(0.0, 1.0, PackedSlot(TapIndex) == Slot);
            if (Slot != BackgroundSlot)
            {
                let TapPosition = SurfaceFeatures[TapIndex];
                Edge *= pow(max(dot(Normal, SurfaceFeatures[PixelCount() + TapIndex].xyz), 0.0), 8.0);
                Edge *= exp(-abs(dot(TapPosition.xyz - Position.xyz, Normal)) / max(PlaneTolerance, 1.0e-6));
            }
            Guided += vec4f(TapMean, 1.0) * Tent * Edge;
        }
    }
    if (Guided.w > 1.0e-3 * Plain.w) { return vec4f(Guided.rgb / Guided.w, Guided.w); }
    return vec4f(Plain.rgb / max(Plain.w, 1.0e-6), Plain.w);
}

// 📝 ① Estimate + luminance variance per pixel. Variance: temporal (second moment) from 4 samples on, before that the
//    3 × 3 spatial variance among neighbours of the same material. History (if active) keeps weight w·(1 − n/span)²,
//    gone after span exact samples, and is clamped to mean ± γσ of the current 3 × 3 neighbourhood — together they remove
//    the stale history that used to drag behind the exact sums.
@compute @workgroup_size(8, 8, 1)
fn GatherEstimate(@builtin(global_invocation_id) InvocationId: vec3u)
{
    let Width  = Reconstruction.Extent.x;
    let Height = Reconstruction.Extent.y;
    if (InvocationId.x >= Width || InvocationId.y >= Height) { return; }
    let PixelIndex = InvocationId.y * Width + InvocationId.x;
    var Sum        = Accumulation[PixelIndex];
    let Samples    = Sum.w;
    var Mean       = Sum.rgb / max(Samples, 1.0);
    let Slot       = PackedSlot(PixelIndex);
    // 📝 Hole (reduced trace, no own sample yet): upsampled estimate counted as a fraction of a sample, so valid
    //    reprojected history (when present) outweighs it.
    //    Sparse pixel: the guided neighbourhood mean joins as a prior worth PoolWeight·(1 − n/16)² samples, so the
    //    2-pixel dither of a reduced trace (neighbours hold different sample indices) is resolved at tile resolution
    //    first and the pixel's own samples take over as they arrive — the converged image is unchanged.
    let Hole = Samples <= 0.0 && Reconstruction.Geometry.w > 1.0;
    //    With the denoiser on, its edge-stopped filter does this job better (measured, TraceScale.json): the prior is
    //    scaled by (1 − filter strength) so the two never smooth twice.
    let Pool = PoolWeight * pow(clamp(1.0 - Samples / PoolFadedAt, 0.0, 1.0), 2.0) * (1.0 - Reconstruction.Tuning.z);
    if (Reconstruction.Geometry.w > 1.0 && (Hole || Pool > 0.0))
    {
        let Filled = FillFromNeighbours(vec2i(InvocationId.xy), Width, Height);
        if (Hole) { Sum = vec4f(Filled.rgb * HoleWeight, HoleWeight); }
        else      { Sum = Sum + vec4f(Filled.rgb * Pool, Pool); }
        Mean = Sum.rgb / Sum.w;
    }

    // Neighbourhood statistics of the current exact means.
    var Moment1 = vec3f(0.0);
    var Moment2 = vec3f(0.0);
    var Taps    = 0.0;
    var SlotLuminance1 = 0.0;
    var SlotLuminance2 = 0.0;
    var SlotTaps       = 0.0;
    for (var OffsetY = -1; OffsetY <= 1; OffsetY++)
    {
        for (var OffsetX = -1; OffsetX <= 1; OffsetX++)
        {
            let Tap       = vec2i(clamp(vec2i(InvocationId.xy) + vec2i(OffsetX, OffsetY), vec2i(0), vec2i(i32(Width) - 1, i32(Height) - 1)));
            let TapIndex  = u32(Tap.y) * Width + u32(Tap.x);
            // 📝 Unsampled neighbours (reduced trace) carry no information; full resolution always has samples here.
            if (Accumulation[TapIndex].w <= 0.0 && Reconstruction.Geometry.w > 1.0) { continue; }
            let TapMean   = ExactMean(TapIndex);
            Taps    += 1.0;
            Moment1 += TapMean;
            Moment2 += TapMean * TapMean;
            if (PackedSlot(TapIndex) == Slot)
            {
                let Luminance = dot(TapMean, LuminanceWeights);
                SlotLuminance1 += Luminance;
                SlotLuminance2 += Luminance * Luminance;
                SlotTaps       += 1.0;
            }
        }
    }
    let TapCount  = max(Taps, 1.0);
    Moment1 = Moment1 / TapCount;
    let Deviation = sqrt(max(Moment2 / TapCount - Moment1 * Moment1, vec3f(0.0)));
    let SlotCount = max(SlotTaps, 1.0);
    let Luminance       = dot(Mean, LuminanceWeights);
    // 📝 No same-material sampled neighbour (early reduced-trace frames): assume a large variance (filter freely).
    let SpatialVariance = select(Luminance * Luminance, max(SlotLuminance2 / SlotCount - (SlotLuminance1 / SlotCount) * (SlotLuminance1 / SlotCount), 0.0), SlotTaps > 0.0);
    let OwnLuminance    = dot(Accumulation[PixelIndex].rgb / max(Samples, 1.0), LuminanceWeights);
    let SampleVariance  = max(Accumulation[3u * PixelCount() + PixelIndex].x / max(Samples, 1.0) - OwnLuminance * OwnLuminance, 0.0);
    var Variance        = select(SpatialVariance, SampleVariance / max(Samples, 1.0), Samples >= 4.0);

    var Radiance = Mean;
    if ((Reconstruction.Extent.w & ModeHistory) != 0u)
    {
        let History       = Accumulation[Reconstruction.Routing.z + PixelIndex];
        let Remaining     = clamp(1.0 - Samples / max(Reconstruction.Tuning.y, 1.0), 0.0, 1.0);
        let HistoryWeight = History.w * Remaining * Remaining;
        var Carried       = History.rgb;
        if (Samples > 0.0 && Reconstruction.Tuning.x > 0.0)
        {
            let Gamma = Reconstruction.Tuning.x;
            Carried = clamp(Carried, Moment1 - Gamma * Deviation, Moment1 + Gamma * Deviation);
        }
        let Weight = Sum.w + HistoryWeight;
        if (Weight > 0.0)
        {
            Radiance = (Sum.rgb + Carried * HistoryWeight) / Weight;
            Variance = Variance * (Sum.w / Weight) * (Sum.w / Weight);
        }
    }
    WriteResult(PixelIndex, Radiance, Variance);
}

// 📝 ② One à-trous step. Weights = B3 spline × normal^p × exp(−plane distance / σ) × same material × exp(−|ΔL| / σ_L).
//    σ_L = (σ / n) × 3 × 3-blurred standard deviation: the host divides σ by the sample count, so the filter tightens
//    as exact samples arrive and highlights stop being smeared (measured best per sample count, Reconstruction.json).
@compute @workgroup_size(8, 8, 1)
fn FilterAtrous(@builtin(global_invocation_id) InvocationId: vec3u)
{
    let Width  = Reconstruction.Extent.x;
    let Height = Reconstruction.Extent.y;
    if (InvocationId.x >= Width || InvocationId.y >= Height) { return; }
    let PixelIndex = InvocationId.y * Width + InvocationId.x;
    let ReadOffset = Reconstruction.Routing.x;
    let Step       = i32(Reconstruction.Extent.z);
    let Centre     = Estimates[ReadOffset + PixelIndex];
    let Slot       = PackedSlot(PixelIndex);
    let Final      = (Reconstruction.Extent.w & ModeFinal) != 0u;

    var Filtered = Centre;
    let SharpSkipped = Slot != BackgroundSlot && PackedRoughness(PixelIndex) < SharpRoughness && Step > 2;
    if (!SharpSkipped)
    {
        // Blurred variance → luminance tolerance.
        var BlurredVariance = 0.0;
        let Gaussian = array<f32, 3>(0.25, 0.125, 0.0625);
        for (var OffsetY = -1; OffsetY <= 1; OffsetY++)
        {
            for (var OffsetX = -1; OffsetX <= 1; OffsetX++)
            {
                let Tap = vec2i(clamp(vec2i(InvocationId.xy) + vec2i(OffsetX, OffsetY), vec2i(0), vec2i(i32(Width) - 1, i32(Height) - 1)));
                BlurredVariance += Gaussian[abs(OffsetX) + abs(OffsetY)] * Estimates[ReadOffset + u32(Tap.y) * Width + u32(Tap.x)].w;
            }
        }
        let LuminanceTolerance = Reconstruction.Tuning.w * sqrt(max(BlurredVariance, 0.0)) + 1.0e-4;
        let CentreLuminance    = dot(Centre.rgb, LuminanceWeights);
        let CentrePosition     = SurfaceFeatures[PixelIndex];
        let CentreNormal       = SurfaceFeatures[PixelCount() + PixelIndex].xyz;
        let PlaneTolerance     = Reconstruction.Geometry.z * Reconstruction.Geometry.x * max(CentrePosition.w, 1.0e-3) * f32(Step);

        let Spline = array<f32, 3>(0.375, 0.25, 0.0625);
        var WeightSum   = 0.0;
        var RadianceSum = vec3f(0.0);
        var VarianceSum = 0.0;
        for (var OffsetY = -2; OffsetY <= 2; OffsetY++)
        {
            for (var OffsetX = -2; OffsetX <= 2; OffsetX++)
            {
                let Tap = vec2i(InvocationId.xy) + vec2i(OffsetX, OffsetY) * Step;
                if (any(Tap < vec2i(0)) || Tap.x >= i32(Width) || Tap.y >= i32(Height)) { continue; }
                let TapIndex = u32(Tap.y) * Width + u32(Tap.x);
                if (PackedSlot(TapIndex) != Slot) { continue; }
                let TapEstimate = Estimates[ReadOffset + TapIndex];
                var Weight = Spline[abs(OffsetX)] * Spline[abs(OffsetY)];
                Weight *= exp(-abs(dot(TapEstimate.rgb, LuminanceWeights) - CentreLuminance) / LuminanceTolerance);
                if (Slot != BackgroundSlot)
                {
                    let TapNormal   = SurfaceFeatures[PixelCount() + TapIndex].xyz;
                    let TapPosition = SurfaceFeatures[TapIndex].xyz;
                    Weight *= pow(max(dot(CentreNormal, TapNormal), 0.0), Reconstruction.Geometry.y);
                    Weight *= exp(-abs(dot(CentreNormal, TapPosition - CentrePosition.xyz)) / PlaneTolerance);
                }
                WeightSum   += Weight;
                RadianceSum += TapEstimate.rgb * Weight;
                VarianceSum += TapEstimate.w * Weight * Weight;
            }
        }
        if (WeightSum > 0.0) { Filtered = vec4f(RadianceSum / WeightSum, VarianceSum / (WeightSum * WeightSum)); }
    }

    if (Final)
    {
        let Unfiltered = Estimates[PixelIndex].rgb;
        WriteResult(PixelIndex, mix(Unfiltered, Filtered.rgb, Reconstruction.Tuning.z), 0.0);
        return;
    }
    WriteResult(PixelIndex, Filtered.rgb, Filtered.w);
}
