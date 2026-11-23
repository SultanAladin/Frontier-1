//============================================================================================================================================
//                                                          RESERVOIRSPATIAL.WGSL
//============================================================================================================================================
// 🧩 ReSTIR GI spatial reuse with the reconnection-shift Jacobian, 1/Z bias correction, a visibility test, and the resolve.

// 📝 Per resampling pixel r with primary surface (x_v, n_v):
//    ① its own temporal reservoir seeds the spatial reservoir;
//    ② SpatialCount nearby reservoirs q are proposed in a disk; a proposal is refused unless its surface agrees in
//       normal and depth. Its sample y = (x_s, n_s, L) is reconnected to x_v, so its weight carries the solid-angle
//       Jacobian |∂ω_r/∂ω_q| = (cos φ_r / cos φ_q)·(‖x_q − x_s‖² / ‖x_v − x_s‖²); escaped samples are directional
//       and carry 1. Extreme Jacobians are refused rather than clamped;
//    ③ Z sums the tallies of every domain whose own surface sees the chosen y above its horizon, so W = Σw/(Z·p̂(y));
//    ④ an adopted y is traced for visibility from x_v and, if occluded, the pixel falls back to its own reservoir;
//    ⑤ resolve: E = L(y) · max(0, n_v·ω_y) · W, the demodulated irradiance the variance integrator receives.

//! include "CardSampling.wgsl"
//! include "ReservoirRecords.wgsl"
//! include "DiscrepancySequence.wgsl"

@group(0) @binding(1) var Guide                            : texture_2d<f32>;
@group(0) @binding(2) var SurfaceDepth                     : texture_depth_2d;
@group(0) @binding(3) var<storage, read> CurrentReservoirs : array<ReservoirRecord>;
@group(0) @binding(4) var IrradianceTarget                 : texture_storage_2d<rgba16float, write>;

const ProposalCapacity : u32 = 8u;

fn ResolveTracePosition(TracePixel: vec2i) -> vec3f
{
    let ScreenPixel = min(TracePixel * i32(Frame.TraceScale), vec2i(Frame.ScreenExtent) - vec2i(1));
    return ReconstructWorldPosition(vec2f(ScreenPixel) + 0.5, textureLoad(SurfaceDepth, ScreenPixel, 0));
}

fn ResolveReconnectionJacobian(Sample: ReservoirSample, Receiver: vec3f, Donor: vec3f) -> f32
{
    if (Sample.Identity == SkyIdentity)
    {
        return 1.0;
    }
    let ToDonor    = Donor - Sample.Position;
    let ToReceiver = Receiver - Sample.Position;
    let DonorSquared    = max(dot(ToDonor, ToDonor), 1.0e-6);
    let ReceiverSquared = max(dot(ToReceiver, ToReceiver), 1.0e-6);
    let CosineDonor    = abs(dot(Sample.Normal, ToDonor)) * inverseSqrt(DonorSquared);
    let CosineReceiver = abs(dot(Sample.Normal, ToReceiver)) * inverseSqrt(ReceiverSquared);
    return (CosineReceiver / max(CosineDonor, 1.0e-4)) * (DonorSquared / ReceiverSquared);
}

@compute @workgroup_size(8, 8)
fn ResampleSpatial(@builtin(global_invocation_id) Invocation: vec3u)
{
    let TracePixel = vec2i(Invocation.xy);
    let Extent = vec2i(Frame.TraceExtent);
    if (any(TracePixel >= Extent))
    {
        return;
    }
    let GuideTexel = textureLoad(Guide, TracePixel, 0);
    if (GuideTexel.w < 0.5)
    {
        textureStore(IrradianceTarget, TracePixel, vec4f(0.0));
        return;
    }
    let Normal    = DecodeOctahedral(GuideTexel.xy);
    let ViewDepth = GuideTexel.z;
    let Position  = ResolveTracePosition(TracePixel);
    var Seed = ConstructPixelSeed(vec2u(TracePixel), Frame.FrameIndex, 23u);

    // ① own reservoir
    let Own = UnpackReservoir(CurrentReservoirs[u32(TracePixel.y * Extent.x + TracePixel.x)]);
    var Spatial = ConstructEmptyReservoir();
    MergeReservoir(&Spatial, Own.Sample, ResolveReservoirTarget(Own.Sample, Position, Normal) * Own.Weight * Own.Tally, Own.Tally,
                   AdvanceUniform(&Seed));

    // ② proposals
    var DonorPositions : array<vec3f, 8>;
    var DonorNormals   : array<vec3f, 8>;
    var DonorTallies   : array<f32, 8>;
    var DonorCount = 0u;
    if (FeatureEnabled(Frame, FeatureSpatialReuse))
    {
        let Count = min(Frame.SpatialCount, ProposalCapacity);
        for (var Proposal = 0u; Proposal < Count; Proposal++)
        {
            let Offset = ProjectUniformDisk(vec2f(AdvanceUniform(&Seed), AdvanceUniform(&Seed))) * Frame.SpatialRadius;
            let Donor = clamp(TracePixel + vec2i(round(Offset)), vec2i(0), Extent - vec2i(1));
            if (all(Donor == TracePixel))
            {
                continue;
            }
            let DonorGuide = textureLoad(Guide, Donor, 0);
            let DonorNormal = DecodeOctahedral(DonorGuide.xy);
            if (DonorGuide.w < 0.5 || dot(DonorNormal, Normal) < 0.9 || abs(DonorGuide.z - ViewDepth) > 0.1 * ViewDepth)
            {
                continue;
            }
            let Reservoir = UnpackReservoir(CurrentReservoirs[u32(Donor.y * Extent.x + Donor.x)]);
            if (Reservoir.Tally <= 0.0 || Reservoir.Weight <= 0.0)
            {
                continue;
            }
            let DonorPosition = ResolveTracePosition(Donor);
            let Jacobian = ResolveReconnectionJacobian(Reservoir.Sample, Position, DonorPosition);
            if (Jacobian > 8.0 || Jacobian < 0.125 || Jacobian != Jacobian)
            {
                continue;
            }
            let Target = ResolveReservoirTarget(Reservoir.Sample, Position, Normal);
            if (MergeReservoir(&Spatial, Reservoir.Sample, Target * Reservoir.Weight * Reservoir.Tally * Jacobian, Reservoir.Tally,
                               AdvanceUniform(&Seed)))
            {
                Spatial.Adopted = true;
            }
            DonorPositions[DonorCount] = DonorPosition;
            DonorNormals[DonorCount]   = DonorNormal;
            DonorTallies[DonorCount]   = Reservoir.Tally;
            DonorCount++;
        }
    }

    // ④ visibility of an adopted sample from this surface
    if (Spatial.Adopted && FeatureEnabled(Frame, FeatureVisibilityReuse))
    {
        let Toward = Spatial.Sample.Position - Position;
        let Length = length(Toward);
        let Bias = ResolveSurfaceBias(Position);
        let Reach = select(Length - 2.0 * ResolveSurfaceBias(Spatial.Sample.Position), 300.0, Spatial.Sample.Identity == SkyIdentity);
        if (TraverseSceneVisibility(Position + Normal * Bias, Toward / Length, 0.5 * Bias, max(Reach, 0.0)) < 0.5)
        {
            Spatial = Own;
            Spatial.WeightSum = ResolveReservoirTarget(Own.Sample, Position, Normal) * Own.Weight * Own.Tally;
            Spatial.Adopted = false;
            DonorCount = 0u;
        }
    }

    // ③ 1/Z bias correction over the domains that could have produced y
    let Chosen = Spatial.Sample;
    var Z = select(0.0, Own.Tally, ResolveReservoirTarget(Chosen, Position, Normal) > 0.0);
    for (var Donor = 0u; Donor < DonorCount; Donor++)
    {
        if (ResolveReservoirTarget(Chosen, DonorPositions[Donor], DonorNormals[Donor]) > 0.0)
        {
            Z += DonorTallies[Donor];
        }
    }
    let Target = ResolveReservoirTarget(Chosen, Position, Normal);
    let Weight = select(0.0, Spatial.WeightSum / (Z * Target), Target > 0.0 && Z > 0.0);

    // ⑤ resolve the demodulated irradiance
    let Cosine = max(dot(Normal, ResolveSampleDirection(Chosen, Position)), 0.0);
    var Irradiance = Chosen.Radiance * Cosine * Weight;
    let Luminance = ResolveLuminance(Irradiance);
    let Limit = 40.0 * max(ResolveLuminance(Frame.SunIlluminance), 0.5);
    if (Luminance > Limit)
    {
        Irradiance *= Limit / Luminance;
    }
    textureStore(IrradianceTarget, TracePixel, vec4f(Irradiance, 1.0));
}
