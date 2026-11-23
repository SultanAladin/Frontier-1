/*==========================================================================================================================================
                                                  PATHREPLAYRESERVOIR.SLANG
==========================================================================================================================================*/
// 🧩 Reservoir sampling of replayable diffuse or rough-opaque random sequences in a common positive-support domain.
// 📝 Every reused sequence is fully retraced in the current triangle BVH. This is not vertex reconnection,
//    a hardware ray-query implementation, or a complete general-material ReSTIR GI renderer.
#include "LightInfluence.slang"
uint ReplayHistoryStart() { return Extent.Dimensions.x * 25u + 13889u + SolarSourceSampleCount; }
uint ReplaySlot(uint Slot, uint Pixel) { return ReplayHistoryStart() + Slot * Extent.Dimensions.x + Pixel; }
float2 EncodeReplaySeed(uint Seed) { return float2(float(Seed & 65535u), float(Seed >> 16u)); }
uint DecodeReplaySeed(float2 Packed) { return uint(Packed.x) | (uint(Packed.y) << 16u); }
// 📝 Keep float-to-int conversion defined even for extreme or non-finite reprojected coordinates.
bool ReplayMotionConvertible(float2 Pixel)
{
    return isfinite(Pixel.x) && isfinite(Pixel.y) && abs(Pixel.x) < 1073741824.0 && abs(Pixel.y) < 1073741824.0;
}
float3 ReplayDirection(float3 Normal, inout uint Bits)
{
    float Radius = sqrt(RandomUnit(Bits)), Azimuth = 6.283185307179586 * RandomUnit(Bits);
    float3 Tangent = Unit(cross(abs(Normal.z) < .999 ? float3(0,0,1) : float3(0,1,0), Normal));
    float3 Bitangent = cross(Normal, Tangent);
    return Unit(Tangent * (Radius * cos(Azimuth)) + Bitangent * (Radius * sin(Azimuth)) +
                Normal * sqrt(max(0, 1 - Radius * Radius)));
}
// 📝 The GGX NDF samples all microfacet normals, including ones whose reflections leave the upper hemisphere.
//    Such draws are zero-contribution events, not resampled. Cosine retains support over the whole hemisphere.
float ReplayImportancePdf(SurfacePoint Receiver, float3 Direction)
{
    float3 N = Unit(Receiver.NormalRoughness.xyz), V = Unit(Receiver.ViewMetallic.xyz);
    float NL = max(0, dot(N, Direction));
    if (NL <= 0) return 0;
    float3 H = Unit(V + Direction);
    float NH = max(0, dot(N, H)), VH = max(0, dot(V, H));
    float Ggx = VH > 0 ? EvaluateGgxDistribution(NH, Receiver.NormalRoughness.w) * NH / (4 * VH) : 0;
    return .5 * (NL / 3.141592653589793 + Ggx);
}
bool ReplayImportanceDirection(SurfacePoint Receiver, inout uint Bits, out float3 Direction, out float Pdf)
{
    float3 N = Unit(Receiver.NormalRoughness.xyz), V = Unit(Receiver.ViewMetallic.xyz);
    Direction = 0;
    Pdf = 0;
    if (RandomUnit(Bits) < .5)
        Direction = ReplayDirection(N, Bits);
    else
    {
        float U = RandomUnit(Bits), Azimuth = 6.283185307179586 * RandomUnit(Bits);
        float Roughness = Receiver.NormalRoughness.w, α = Roughness * Roughness, α2 = α * α;
        float Cosine = sqrt((1 - U) / (1 + (α2 - 1) * U));
        float Sine = sqrt(max(0, 1 - Cosine * Cosine));
        float3 Tangent = Unit(cross(abs(N.z) < .999 ? float3(0,0,1) : float3(0,1,0), N));
        float3 Half = Unit(Tangent * (Sine * cos(Azimuth)) + cross(N, Tangent) * (Sine * sin(Azimuth)) + N * Cosine);
        float ViewHalf = dot(V, Half);
        if (ViewHalf <= 0) return false;
        Direction = Unit(2 * ViewHalf * Half - V);
        if (dot(N, Direction) <= 0) return false;
    }
    Pdf = ReplayImportancePdf(Receiver, Direction);
    return Pdf > 0;
}
// 📝 Test the open segment and skip only the sampled endpoint triangle, rather than shortening the shadow ray.
bool ReplaySegmentOccluded(float3 Origin, float3 Direction, float Distance, uint Endpoint)
{
    if (Extent.Grid.w == 0 || Distance <= 0) return false;
    InterlockedAdd(Counters[2], 1);
    uint Stack[64], Count = 1;
    Stack[0] = 0;
    while (Count > 0)
    {
        FacetEnclosure Box = Enclosures[Stack[--Count]];
        if (!IntersectsBox(Box, Origin, Direction, Distance)) continue;
        if (Box.Links.z != 0)
        {
            for (uint I = 0; I < Box.Links.y; ++I)
            {
                uint Index = Box.Links.x + I;
                float3 Strike;
                if (Index != Endpoint && FacetStrike(Facets[Index], Origin, Direction, 0, Distance, Strike))
                    return true;
            }
        }
        else
        {
            if (Count + 2 > min(64u, max(1u, Extent.Sampling.w)))
            {
                InterlockedAdd(Counters[3], 1);
                for (uint I = 0; I < Extent.Sampling.z; ++I)
                {
                    float3 Strike;
                    if (I != Endpoint && FacetStrike(Facets[I], Origin, Direction, 0, Distance, Strike))
                        return true;
                }
                return false;
            }
            Stack[Count++] = Box.Links.x;
            Stack[Count++] = Box.Links.y;
        }
    }
    return false;
}
// 📝 A half-uniform mixture bounds each emitter's selection probability below by 0.5 / FacetCount.
float ReplayFacetProbability(uint Entry, uint FacetCount)
{
    float Probability = Angles[Entry + 1];
    if ((Extent.Primary.w & 16384u) != 0)
        Probability = .5 * Probability + .5 / float(FacetCount);
    return Probability;
}

// 📝 The Engine CDF is ordered by facet index. Return zero for a source outside that distribution.
float ReplayFacetInverseProbability(uint FacetIndex)
{
    uint FacetCount = Extent.Reconstruction.z;
    if ((Extent.Primary.w & 8192u) == 0) return float(FacetCount);
    uint Offset = asuint(Extent.Maximum.y), Low = 0, High = FacetCount;
    while (Low < High)
    {
        uint Mid = (Low + High) / 2;
        if (uint(Angles[Offset + 1 + Mid * 4]) < FacetIndex) Low = Mid + 1;
        else High = Mid;
    }
    if (Low >= FacetCount || uint(Angles[Offset + 1 + Low * 4]) != FacetIndex) return 0;
    return 1 / ReplayFacetProbability(Offset + 1 + Low * 4, FacetCount);
}

// 📝 Select an emissive facet by the optional Engine CDF or uniformly, then a uniform point on its area.
float3 ReplayFacetRadiance(SurfacePoint Receiver, float3 Origin, bool RoughOpaque, inout uint Bits)
{
    uint FacetCount = Extent.Reconstruction.z;
    if (FacetCount == 0) return 0;
    uint Offset = asuint(Extent.Maximum.y), FacetIndex;
    float InverseProbability;
    if ((Extent.Primary.w & 8192u) != 0)
    {
        bool UniformDraw = false;
        if ((Extent.Primary.w & 16384u) != 0) UniformDraw = RandomUnit(Bits) < .5;
        uint Selected;
        if (UniformDraw)
        {
            uint Threshold = (0u - FacetCount) % FacetCount, SampleBits;
            do { SampleBits = NextRandom(Bits); } while (SampleBits < Threshold);
            Selected = SampleBits % FacetCount;
        }
        else
        {
            float Draw = RandomUnit(Bits);
            uint Low = 0, High = FacetCount - 1;
            while (Low < High)
            {
                uint Mid = (Low + High) / 2;
                if (Draw < Angles[Offset + 1 + Mid * 4 + 3]) High = Mid;
                else Low = Mid + 1;
            }
            Selected = Low;
        }
        uint Entry = Offset + 1 + Selected * 4;
        FacetIndex = uint(Angles[Entry]);
        InverseProbability = 1 / ReplayFacetProbability(Entry, FacetCount);
    }
    else
    {
        // 📝 Rejection removes modulo bias for a uniformly distributed 32-bit draw.
        uint Threshold = (0u - FacetCount) % FacetCount, SampleBits;
        do { SampleBits = NextRandom(Bits); } while (SampleBits < Threshold);
        FacetIndex = uint(Angles[Offset + SampleBits % FacetCount]);
        InverseProbability = float(FacetCount);
    }
    FacetCorners F = Facets[FacetIndex];
    float3 EdgeA = F.B.xyz - F.A.xyz, EdgeB = F.C.xyz - F.A.xyz;
    float3 AreaNormal = cross(EdgeA, EdgeB);
    float DoubleArea = length(AreaNormal);
    float Root = sqrt(RandomUnit(Bits)), Along = RandomUnit(Bits);
    float3 Point = F.A.xyz * (1 - Root) + F.B.xyz * (Root * (1 - Along)) + F.C.xyz * (Root * Along);
    float3 Delta = Point - Origin;
    float DistanceSquared = dot(Delta, Delta);
    if (DoubleArea <= 0 || DistanceSquared <= 1e-10) return 0;
    float Distance = sqrt(DistanceSquared), DirectionCosine;
    float3 Direction = Delta / Distance;
    DirectionCosine = max(0, dot(Unit(Receiver.NormalRoughness.xyz), Direction));
    float SourceCosine = abs(dot(AreaNormal / DoubleArea, Direction));
    if (DirectionCosine <= 0 || SourceCosine <= 0 ||
        ReplaySegmentOccluded(Origin, Direction, Distance, FacetIndex)) return 0;
    float3 Scattering = RoughOpaque ? Brdf(Receiver, Direction) : clamp(Receiver.AlbedoIor.xyz, 0, 1) / 3.141592653589793;
    float Measure = DirectionCosine * SourceCosine * (.5 * DoubleArea) * InverseProbability / DistanceSquared;
    // 📝 The balance ratio is p_bsdf / p_facet in solid angle, for the same proposal used below.
    float Balance = 1;
    if ((Extent.Primary.w & 1024u) != 0)
    {
        float Ratio = (Extent.Primary.w & 4096u) != 0
            ? ReplayImportancePdf(Receiver, Direction) * SourceCosine * (.5 * DoubleArea) * InverseProbability / DistanceSquared
            : Measure / 3.141592653589793;
        Balance = 1 / (1 + Ratio);
    }
    return Scattering * max(F.Emissive.xyz, 0) * (Measure * Balance);
}
float3 EvaluateReplayPath(SurfacePoint Receiver, uint Seed)
{
    uint Bits = Seed;
    float3 Normal = Unit(Receiver.NormalRoughness.xyz);
    float3 Origin = Receiver.PositionHit.xyz + Normal * .001;
    float3 Throughput = 1, Radiance = 0;
    bool RoughOpaque = (Extent.Primary.w & 64u) != 0, SampleFacets = (Extent.Primary.w & 128u) != 0;
    bool ImportanceRough = (Extent.Primary.w & 4096u) != 0;
    SurfacePoint Current = Receiver;
    for (uint Bounce = 0; Bounce < Extent.Primary.z; ++Bounce)
    {
        if (SampleFacets) Radiance += Throughput * ReplayFacetRadiance(Current, Origin, RoughOpaque, Bits);
        float3 Direction, Strike;
        if (ImportanceRough)
        {
            float Pdf;
            if (!ReplayImportanceDirection(Current, Bits, Direction, Pdf)) break;
            Throughput *= Brdf(Current, Direction) * (max(0, dot(Normal, Direction)) / Pdf);
        }
        else
        {
            Direction = ReplayDirection(Normal, Bits);
            // With cosine-weighted sampling, f*cos/pdf = pi*f. The original diffuse mode stays Lambertian.
            Throughput *= RoughOpaque ? 3.141592653589793 * Brdf(Current, Direction) : clamp(Current.AlbedoIor.xyz, 0, 1);
        }
        if (max(Throughput.x, max(Throughput.y, Throughput.z)) <= 0) break;
        uint Index = NearestFacet(Origin, Direction, .0001, 1e30, Strike);
        if (Index == 0xffffffffu)
        {
            Radiance += Throughput * max(Extent.Minimum.xyz, 0); // Uniform environment, not an importance map.
            break;
        }
        FacetCorners F = Facets[Index];
        // 📝 Without balance weighting, explicit facet sampling accounts for emissive hits; bit 256 is a wrong control.
        if (!SampleFacets || (Extent.Primary.w & 256u) != 0)
            Radiance += Throughput * max(F.Emissive.xyz, 0);
        else if ((Extent.Primary.w & 1024u) != 0 && any(F.Emissive.xyz > 0) && Extent.Reconstruction.z > 0)
        {
            float3 AreaNormal = cross(F.B.xyz - F.A.xyz, F.C.xyz - F.A.xyz);
            float DoubleArea = length(AreaNormal), DistanceSquared = Strike.x * Strike.x;
            if (DoubleArea > 0 && DistanceSquared > 0)
            {
                float ReceiverCosine = max(0, dot(Normal, Direction));
                float SourceCosine = abs(dot(AreaNormal / DoubleArea, Direction));
                float InverseProbability = ReplayFacetInverseProbability(Index);
                if (InverseProbability == 0)
                    Radiance += Throughput * max(F.Emissive.xyz, 0);
                else
                {
                    float Ratio = ImportanceRough
                        ? ReplayImportancePdf(Current, Direction) * SourceCosine * (.5 * DoubleArea) *
                          InverseProbability / DistanceSquared
                        : ReceiverCosine * SourceCosine * (.5 * DoubleArea) * InverseProbability /
                          (3.141592653589793 * DistanceSquared);
                    Radiance += Throughput * max(F.Emissive.xyz, 0) * (Ratio / (1 + Ratio));
                }
            }
        }
        Normal = Unit(F.NormalA.xyz * (1 - Strike.y - Strike.z) + F.NormalB.xyz * Strike.y + F.NormalC.xyz * Strike.z);
        if (dot(Normal, Direction) > 0) Normal = -Normal;
        float3 HitPosition = Origin + Direction * Strike.x;
        Current = (SurfacePoint)0;
        Current.PositionHit = float4(HitPosition, 1);
        Current.NormalRoughness = float4(Normal, F.AlbedoRoughness.w);
        Current.ViewMetallic = float4(-Direction, F.OpticalToken.x);
        Current.AlbedoIor = float4(F.AlbedoRoughness.xyz, F.OpticalToken.y);
        Current.ResidualSpecular.w = F.OpticalToken.z;
        // 📝 The host admits only guarded opaque single-stratum albedo maps. Rough mode uses the authored BRDF.
        if ((Extent.Primary.w & 2048u) != 0 && F.Emissive.w > 0)
            ApplyRayMaterial(Current, F, Strike.yz);
        Origin = HitPosition + Normal * .001;
    }
    return Radiance;
}
// 📝 Every half is an exactly representable uint16, avoiding float-to-uint conversion of corrupt history.
bool ReplaySeedHalfValid(float Value)
{
    return isfinite(Value) && Value >= 0 && Value <= 65535 && floor(Value) == Value;
}
bool ReplaySeedBankValid(float4 First, float4 Second)
{
    float Count = Second.z;
    return isfinite(Count) && Count >= 1 && Count <= 3 && floor(Count) == Count &&
        ReplaySeedHalfValid(First.x) && ReplaySeedHalfValid(First.y) &&
        (Count < 2 || (ReplaySeedHalfValid(First.z) && ReplaySeedHalfValid(First.w))) &&
        (Count < 3 || (ReplaySeedHalfValid(Second.x) && ReplaySeedHalfValid(Second.y)));
}
float ReplayTarget(float3 Radiance) { return max(0, Power(Radiance)) + Extent.Maximum.x; }
[shader("compute")][numthreads(64,1,1)] void GenerateReplayReservoirs(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    float4 Packed = 0, SelectedRadiance = 0;
    if (R.PositionHit.w != 0 && R.MotionPositionToken.w > 0 &&
        (R.ViewMetallic.w == 0 || (Extent.Primary.w & 64u) != 0))
    {
        uint SeedBits = Extent.Sampling.x ^ (Pixel * 747796405u + 2891336453u);
        uint SelectionBits = (Extent.Sampling.x * 277803737u + 1597334677u) ^ (Pixel * 2891336453u + 1u);
        uint Selected = NextRandom(SeedBits);
        float3 Radiance = EvaluateReplayPath(R, Selected);
        float Target = ReplayTarget(Radiance), Sum = Target, Count = 1;
        // 📝 Zero preserves the original one-candidate encoding and consumes no extra selection draws.
        for (uint CandidateIndex = 1; CandidateIndex < max(Extent.Sampling.y, 1u); ++CandidateIndex)
        {
            uint Candidate = NextRandom(SeedBits);
            float3 CandidateRadiance = EvaluateReplayPath(R, Candidate);
            float CandidateTarget = ReplayTarget(CandidateRadiance);
            Sum += CandidateTarget;
            Count += 1;
            if (RandomUnit(SelectionBits) * Sum < CandidateTarget)
            {
                Selected = Candidate;
                Radiance = CandidateRadiance;
                Target = CandidateTarget;
            }
        }
        bool TemporalReplayed = false;
        uint TemporalSeed = 0;
        if (Extent.Reconstruction.x != 0 && Extent.History.z != 0 && Extent.History.w == 0 &&
            R.MotionPixel.w != 0 && ReplayMotionConvertible(R.MotionPixel.xy))
        {
            int2 P = int2(floor(R.MotionPixel.xy + .5));
            if (all(P >= 0) && all(P < int2(Extent.History.xy)))
            {
                uint Previous = uint(P.y) * Extent.History.x + uint(P.x);
                float4 Source = OutputColour[ReplaySlot(0, Previous)];
                float4 Guide = OutputColour[ReplaySlot(1, Previous)];
                float3 PriorNormal = OutputColour[ReplaySlot(4, Previous)].xyz;
                float3 MotionNormal = R.MotionNormal.xyz;
                float PriorLength = dot(PriorNormal, PriorNormal), MotionLength = dot(MotionNormal, MotionNormal);
                bool MatchingPosition = all(Guide.xyz == R.MotionPositionToken.xyz);
                // Round-to-pixel reprojection generally lands on a nearby point, not the identical world position.
                // The audit flag retains the old exact test; missing normals also fall back to exact positions.
                if ((Extent.Primary.w & 512u) == 0 && PriorLength > 1e-12 && MotionLength > 1e-12)
                {
                    float3 Delta = Guide.xyz - R.MotionPositionToken.xyz;
                    float Radius = clamp(R.MotionPixel.z, 0.0, .25);
                    MatchingPosition = dot(Delta, Delta) <= Radius * Radius &&
                                       dot(PriorNormal, MotionNormal) >= .9 * sqrt(PriorLength * MotionLength);
                }
                bool Compatible = Source.w > 0 && Source.z > 0 && Guide.w == R.MotionPositionToken.w &&
                                  MatchingPosition;
                if (Compatible)
                {
                    uint Candidate = DecodeReplaySeed(Source.xy);
                    float3 Replayed = EvaluateReplayPath(R, Candidate);
                    // A domain-change cap scales represented count and weight together; W remains per sample.
                    float ReplayedTarget = ReplayTarget(Replayed);
                    float Retained = min(Source.w, float(max(1u, Extent.Reconstruction.w)));
                    float Weight = ReplayedTarget * Source.z * Retained;
                    Sum += Weight; Count += Retained;
                    if (RandomUnit(SelectionBits) * Sum < Weight)
                    {
                        Selected = Candidate; Radiance = Replayed; Target = ReplayedTarget;
                    }
                    TemporalReplayed = true;
                    TemporalSeed = Candidate;
                    InterlockedAdd(Counters[20], 1);
                }
                else InterlockedAdd(Counters[21], 1);
            }
        }
        // 📝 Retained unused-draw seeds are retraced here as ordinary one-sample reservoir candidates.
        //    Each enters with its current-surface target and count one, exactly like an extra fresh
        //    candidate; the resolve stage therefore keeps its usual reservoir estimator and no old
        //    radiance or visibility answer is reused. A seed already entered as the temporal candidate
        //    is skipped so the same path is never counted twice in one reservoir.
        if ((Extent.Primary.w & 262144u) != 0 && (Extent.Reconstruction.x != 0 || Extent.Reconstruction.y != 0))
        {
            float4 BankFirst = OutputColour[ReplaySlot(13, Pixel)];
            float4 BankSecond = OutputColour[ReplaySlot(14, Pixel)];
            if (ReplaySeedBankValid(BankFirst, BankSecond))
            {
                uint RetainedCount = uint(BankSecond.z);
                // 📝 With temporal reuse, only the newest banked seed may enter: it was drawn after the
                //    temporal reservoir was produced and never competed in it. Older banked seeds did
                //    compete in reservoirs whose summaries return as temporal candidates, which correlates
                //    their weights with the temporal weight and biases the merge (measured -25.6% stationary
                //    bias in Exhibits/LightCompute/ReplaySeedCorrelation.md; newest-only entry: +0.4%).
                uint RetainedEntries = Extent.Reconstruction.x != 0 ? min(RetainedCount, 1u) : RetainedCount;
                for (uint RetainedIndex = 0; RetainedIndex < RetainedEntries; ++RetainedIndex)
                {
                    float2 PackedSeed = RetainedIndex == 0 ? BankFirst.xy :
                                        (RetainedIndex == 1 ? BankFirst.zw : BankSecond.xy);
                    uint Candidate = DecodeReplaySeed(PackedSeed);
                    if (TemporalReplayed && Candidate == TemporalSeed) continue;
                    float3 CandidateRadiance = EvaluateReplayPath(R, Candidate);
                    float CandidateTarget = ReplayTarget(CandidateRadiance);
                    Sum += CandidateTarget;
                    Count += 1;
                    if (RandomUnit(SelectionBits) * Sum < CandidateTarget)
                    {
                        Selected = Candidate;
                        Radiance = CandidateRadiance;
                        Target = CandidateTarget;
                    }
                }
            }
        }
        Packed = float4(EncodeReplaySeed(Selected), Sum / (Count * Target), Count);
        SelectedRadiance = float4(Radiance, Target);
    }
    OutputColour[ReplaySlot(2, Pixel)] = Packed;
    OutputColour[ReplaySlot(3, Pixel)] = SelectedRadiance;
}
[shader("compute")][numthreads(64,1,1)] void ResolveReplayReservoirs(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    float4 Local = OutputColour[ReplaySlot(2, Pixel)];
    float3 Chosen = 0;
    float ChosenTarget = Extent.Maximum.x, Sum = 0, Count = 0;
    uint SelectionBits = (Extent.Sampling.x * 2891336453u + 1013904223u) ^ (Pixel * 277803737u + 13u);
    int2 Centre = int2(Pixel % Extent.History.x, Pixel / Extent.History.x);
    static const int2 Offsets[5] = {int2(0,0), int2(-1,0), int2(1,0), int2(0,-1), int2(0,1)};
    uint ProposalCount = Extent.Reconstruction.y != 0 ? 5u : 1u;
    if (Local.w > 0)
        for (uint I = 0; I < ProposalCount; ++I)
        {
            int2 P = Centre + Offsets[I];
            if (any(P < 0) || any(P >= int2(Extent.History.xy))) continue;
            uint Donor = uint(P.y) * Extent.History.x + uint(P.x);
            float4 Source = OutputColour[ReplaySlot(2, Donor)];
            if (Source.w <= 0 || Source.z <= 0) continue;
            float3 Radiance = I == 0 ? OutputColour[ReplaySlot(3, Pixel)].xyz :
                                       EvaluateReplayPath(R, DecodeReplaySeed(Source.xy));
            float Target = ReplayTarget(Radiance), Weight = Target * Source.z * Source.w;
            Sum += Weight; Count += Source.w;
            if (RandomUnit(SelectionBits) * Sum < Weight)
            {
                Chosen = Radiance; ChosenTarget = Target;
            }
            if (I != 0) InterlockedAdd(Counters[22], 1);
        }
    float3 Indirect = Count > 0 ? Chosen * (Sum / (Count * ChosenTarget)) : float3(0);
    OutputColour[Pixel] = float4(Indirect + R.ResidualSpecular.xyz, 1);
    // Persist only pre-spatial history; cross-pixel candidates are retraced, not assumed statistically independent.
    OutputColour[ReplaySlot(0, Pixel)] = Local;
    OutputColour[ReplaySlot(1, Pixel)] = float4(R.PositionHit.xyz, R.PositionHit.w != 0 ? R.MotionPositionToken.w : 0);
    OutputColour[ReplaySlot(4, Pixel)] = float4(R.NormalRoughness.xyz, 0);
}
