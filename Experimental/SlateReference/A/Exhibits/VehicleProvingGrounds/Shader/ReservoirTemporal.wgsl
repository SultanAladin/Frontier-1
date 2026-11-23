//============================================================================================================================================
//                                                          RESERVOIRTEMPORAL.WGSL
//============================================================================================================================================
// 🧩 ReSTIR GI initial candidate and temporal reuse: a cosine ray (screen, then scene distance field), then the prior reservoir.

// 📝 Per resampling pixel:
//    ① the primary surface (x_v, n_v) is read from the surface projection and written to this frame's guide;
//    ② one cosine-weighted ray is traced — first across the screen through the nearest-depth pyramid, then, where the
//       screen cannot settle it, through the ONE scene distance field from the last point the screen proved unoccluded;
//       its radiance is the sample exitance at the hit (or the sky on an escape) — this is the candidate, with source
//       pdf cosθ/π whichever tracer answered;
//    ③ the prior frame's reservoir is found by motion reprojection and validated against the prior guide; its
//       reconnection vertex is carried with its occupant's motion, its radiance re-resolved by the same exitance rule, and
//       (on a rotating quarter of pixels) its visibility re-traced, so a vehicle driving between x_v and x_s darkens
//       the reuse instead of leaving a ghost; M is clamped to TemporalTallyLimit;
//    ④ W = Σw / (M · p̂(y)) is stored for the spatial pass and for the next frame.

//! include "CardSampling.wgsl"
//! include "ScreenTraversal.wgsl"
//! include "ReservoirRecords.wgsl"
//! include "DiscrepancySequence.wgsl"

@group(0) @binding(1) var SurfaceAttributes                   : texture_2d<f32>;
@group(0) @binding(2) var SurfaceDepth                        : texture_depth_2d;
@group(0) @binding(3) var SurfaceMotion                       : texture_2d<f32>;
@group(0) @binding(4) var PreviousGuide                       : texture_2d<f32>;
@group(0) @binding(5) var GuideTarget                         : texture_storage_2d<rgba16float, write>;
@group(0) @binding(6) var<storage, read> PreviousReservoirs   : array<ReservoirRecord>;
@group(0) @binding(7) var<storage, read_write> CurrentReservoirs : array<ReservoirRecord>;

fn ResolveScreenPixel(TracePixel: vec2u) -> vec2i
{
    return min(vec2i(TracePixel * Frame.TraceScale), vec2i(Frame.ScreenExtent) - vec2i(1));
}

// 📝 Carries a reconnection vertex that lies on a moving occupant from where it was last frame to where it is now.
fn CarryReservoirSample(Sample: ReservoirSample) -> ReservoirSample
{
    var Carried = Sample;
    if (Sample.Identity < arrayLength(&Occupants))
    {
        let Occupant = Occupants[Sample.Identity];
        if ((Occupant.Roles & RoleDynamic) != 0u)
        {
            let Local = Occupant.PreviousWorldToLocal * vec4f(Sample.Position, 1.0);
            Carried.Position = (Occupant.LocalToWorld * Local).xyz;
            let LocalNormal = (Occupant.PreviousWorldToLocal * vec4f(Sample.Normal, 0.0)).xyz;
            Carried.Normal = normalize((Occupant.LocalToWorld * vec4f(LocalNormal, 0.0)).xyz);
        }
    }
    return Carried;
}

fn RefreshSampleRadiance(Sample: ReservoirSample) -> vec3f
{
    if (Sample.Identity == SkyIdentity)
    {
        return SampleSkyRadiance(-Sample.Normal);
    }
    let Level = ResolveFinestClip(Sample.Position, 1.0);
    let Exitance = ResolveSampleExitance(Sample.Position, Sample.Normal, Sample.Identity, ResolveCardTolerance(Level));
    return select(Sample.Radiance, Exitance.rgb, Exitance.w > 0.0);
}

@compute @workgroup_size(8, 8)
fn ResampleTemporal(@builtin(global_invocation_id) Invocation: vec3u)
{
    let TracePixel = Invocation.xy;
    let Extent = vec2u(Frame.TraceExtent);
    if (any(TracePixel >= Extent))
    {
        return;
    }
    let Index = TracePixel.y * Extent.x + TracePixel.x;
    let ScreenPixel = ResolveScreenPixel(TracePixel);
    let Depth = textureLoad(SurfaceDepth, ScreenPixel, 0);
    if (Depth <= 0.0)
    {
        textureStore(GuideTarget, vec2i(TracePixel), vec4f(0.0));
        CurrentReservoirs[Index] = PackReservoir(ConstructEmptyReservoir());
        return;
    }

    // ① primary surface
    let Position  = ReconstructWorldPosition(vec2f(ScreenPixel) + 0.5, Depth);
    let Normal    = DecodeOctahedral(textureLoad(SurfaceAttributes, ScreenPixel, 0).xy);
    let ViewDepth = ResolveViewDepth(Position);
    let ExpectedDepth = ResolvePriorViewDepth(Position);
    // 📝 Guide: octahedral normal, view depth, and 1 + the depth this point had last frame (w ≥ 1 marks valid).
    textureStore(GuideTarget, vec2i(TracePixel), vec4f(EncodeOctahedral(Normal), ViewDepth, 1.0 + ExpectedDepth));
    var Seed = ConstructPixelSeed(TracePixel, Frame.FrameIndex, 11u);

    // ② initial candidate: the screen first, then the scene distance field from where the screen lost the ray
    let Pair      = ProjectTemporalPair(TracePixel, Frame.FrameIndex, 3u);
    let Direction = ConstructTangentFrame(Normal) * ProjectCosineHemisphere(Pair);
    let Bias      = ResolveSurfaceBias(Position);
    var Candidate: ReservoirSample;
    if (FeatureEnabled(Frame, FeatureScreenTraversal))
    {
        let Strike = TraverseScreenDepth(Position + Normal * ResolveScreenBias(ViewDepth), Direction, 300.0, ScreenTraversalSteps);
        if (Strike.Hit)
        {
            Candidate.Position = Strike.Position;
            Candidate.Normal   = Strike.Normal;
            Candidate.Identity = Strike.Identity;
            Candidate.Radiance = ResolveStrikeRadiance(Strike);
        }
        else
        {
            // 📝 Backed off by one bias so a surface the screen saw the ray reach is still found by the distance field.
            let Resume = max(0.5 * Bias, Strike.Travel - Bias);
            let Hit = TraverseSceneDistance(Position + Normal * Bias, Direction, Resume, 300.0, 0.0);
            if (Hit.Hit)
            {
                Candidate.Position = Hit.Position;
                Candidate.Normal   = Hit.Normal;
                Candidate.Identity = Hit.Identity;
                Candidate.Radiance = ResolveContinuedRadiance(Hit, Direction);
            }
            else
            {
                Candidate.Position = Position + Direction * SkyReach;
                Candidate.Normal   = -Direction;
                Candidate.Identity = SkyIdentity;
                Candidate.Radiance = SampleSkyRadiance(Direction);
            }
        }
    }
    else
    {
        let Hit = TraverseSceneDistance(Position + Normal * Bias, Direction, 0.5 * Bias, 300.0, 0.0);
        if (Hit.Hit)
        {
            Candidate.Position = Hit.Position;
            Candidate.Normal   = Hit.Normal;
            Candidate.Identity = Hit.Identity;
            Candidate.Radiance = ResolveTraversalRadiance(Hit, Direction);
        }
        else
        {
            Candidate.Position = Position + Direction * SkyReach;
            Candidate.Normal   = -Direction;
            Candidate.Identity = SkyIdentity;
            Candidate.Radiance = SampleSkyRadiance(Direction);
        }
    }
    let Cosine    = max(dot(Normal, Direction), 0.0);
    let SourcePdf = Cosine * InversePi;
    var Current   = ConstructEmptyReservoir();
    let InitialWeight = select(0.0, ResolveLuminance(Candidate.Radiance) * Cosine / SourcePdf, SourcePdf > 1.0e-6);
    MergeReservoir(&Current, Candidate, InitialWeight, 1.0, AdvanceUniform(&Seed));

    // ③ temporal reuse
    if (FeatureEnabled(Frame, FeatureTemporalReuse))
    {
        let Motion = textureLoad(SurfaceMotion, ScreenPixel, 0).xy;
        let PriorScreen = vec2f(ScreenPixel) + 0.5 - Motion;
        let PriorTrace = vec2i(floor((PriorScreen - 0.5) / f32(Frame.TraceScale) + 0.5));
        if (all(PriorTrace >= vec2i(0)) && all(PriorTrace < vec2i(Extent)))
        {
            let PriorGuide = textureLoad(PreviousGuide, PriorTrace, 0);
            let PriorNormal = DecodeOctahedral(PriorGuide.xy);
            let Similar = PriorGuide.w > 0.5 && dot(PriorNormal, Normal) > 0.85
                        && ClassifyPriorDepth(PriorGuide.z, ExpectedDepth, ViewDepth);
            if (Similar)
            {
                var Prior = UnpackReservoir(PreviousReservoirs[u32(PriorTrace.y) * Extent.x + u32(PriorTrace.x)]);
                if (Prior.Tally > 0.0)
                {
                    var Reused = CarryReservoirSample(Prior.Sample);
                    if (FeatureEnabled(Frame, FeatureRadianceRefresh))
                    {
                        Reused.Radiance = RefreshSampleRadiance(Reused);
                    }
                    var Target = ResolveReservoirTarget(Reused, Position, Normal);
                    let Validate = ((TracePixel.x + TracePixel.y * 3u + Frame.FrameIndex) & 3u) == 0u;
                    if (FeatureEnabled(Frame, FeatureVisibilityReuse) && Validate && Target > 0.0)
                    {
                        let Toward = Reused.Position - Position;
                        let Length = length(Toward);
                        let Reach = select(Length - 2.0 * ResolveSurfaceBias(Reused.Position), 300.0, Reused.Identity == SkyIdentity);
                        let Visible = TraverseSceneVisibility(Position + Normal * Bias, Toward / Length, 0.5 * Bias, max(Reach, 0.0));
                        Target *= Visible;
                    }
                    let Tally = min(Prior.Tally, Frame.TemporalTallyLimit);
                    MergeReservoir(&Current, Reused, Target * Prior.Weight * Tally, Tally, AdvanceUniform(&Seed));
                }
            }
        }
    }

    // ④ unbiased contribution weight
    let Selected = ResolveReservoirTarget(Current.Sample, Position, Normal);
    Current.Weight = select(0.0, Current.WeightSum / (Current.Tally * Selected), Selected > 0.0 && Current.Tally > 0.0);
    CurrentReservoirs[Index] = PackReservoir(Current);
}
