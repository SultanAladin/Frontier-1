//============================================================================================================================================
//                                                          RESERVOIRRECORDS.WGSL
//============================================================================================================================================
// 🧩 ReSTIR GI reservoirs (Ouyang et al. 2021): the reconnection vertex, its radiance, W and M, and weighted merging.

// 📝 A sample is the secondary vertex a cosine ray from a primary surface reached in the scene distance field: its
//    position, its distance-field normal, the card radiance it sends back and the occupant it belongs to. A ray that
//    escaped stores its direction pushed far away and SkyIdentity, and is treated directionally under every shift.
//
//    32 bytes per reservoir: position 12, packed normal 4, packed half radiance + identity 8, W 4, M 4.

//! include "PackingCodec.wgsl"
//! include "DistanceRecords.wgsl"

struct ReservoirRecord
{
    SamplePosition : vec3f,   // [m]   reconnection vertex x_s
    SampleNormal   : u32,     // [-]   octahedral n_s
    Radiance       : vec2u,   // [-]   half rgb radiance, identity in the fourth half
    Weight         : f32,     // [-]   unbiased contribution weight W
    Tally          : f32,     // [-]   M
}

struct ReservoirSample
{
    Position : vec3f,
    Normal   : vec3f,
    Radiance : vec3f,
    Identity : u32,
}

struct Reservoir
{
    Sample    : ReservoirSample,
    WeightSum : f32,
    Tally     : f32,
    Weight    : f32,
    Adopted   : bool,   // the selected sample came from another pixel's reservoir
}

const SkyReach : f32 = 10000.0;   // [m] - how far an escaped direction is pushed to become a position

fn ConstructEmptyReservoir() -> Reservoir
{
    var Empty: Reservoir;
    Empty.Sample.Position = vec3f(0.0);
    Empty.Sample.Normal   = vec3f(0.0, 0.0, 1.0);
    Empty.Sample.Radiance = vec3f(0.0);
    Empty.Sample.Identity = SkyIdentity;
    Empty.WeightSum = 0.0;
    Empty.Tally     = 0.0;
    Empty.Weight    = 0.0;
    Empty.Adopted   = false;
    return Empty;
}

fn PackReservoir(Source: Reservoir) -> ReservoirRecord
{
    var Record: ReservoirRecord;
    Record.SamplePosition = Source.Sample.Position;
    Record.SampleNormal   = PackOctahedral(Source.Sample.Normal);
    Record.Radiance       = PackHalfTriple(min(Source.Sample.Radiance, vec3f(60000.0)), f32(Source.Sample.Identity));
    Record.Weight         = Source.Weight;
    Record.Tally          = Source.Tally;
    return Record;
}

fn UnpackReservoir(Record: ReservoirRecord) -> Reservoir
{
    var Result = ConstructEmptyReservoir();
    let Radiance = UnpackHalfTriple(Record.Radiance);
    Result.Sample.Position = Record.SamplePosition;
    Result.Sample.Normal   = UnpackOctahedral(Record.SampleNormal);
    Result.Sample.Radiance = Radiance.rgb;
    Result.Sample.Identity = u32(Radiance.w + 0.5);
    Result.Weight = Record.Weight;
    Result.Tally  = Record.Tally;
    return Result;
}

// 📐 Weighted reservoir sampling: accumulate w into Σw and M into the tally, keep the candidate with probability w/Σw.
fn MergeReservoir(Target: ptr<function, Reservoir>, Candidate: ReservoirSample, Weight: f32, Tally: f32, Random: f32) -> bool
{
    (*Target).WeightSum += Weight;
    (*Target).Tally     += Tally;
    if (Weight > 0.0 && Random * (*Target).WeightSum < Weight)
    {
        (*Target).Sample = Candidate;
        return true;
    }
    return false;
}

fn ResolveSampleDirection(Sample: ReservoirSample, Position: vec3f) -> vec3f
{
    return normalize(Sample.Position - Position);
}

// 📐 Target function p̂(y) = luminance(L(y)) · max(0, n_v · ω) at the shading point (x_v, n_v): the integrand of the
//    irradiance without the visibility term, which the reuse passes test explicitly where they need it.
fn ResolveReservoirTarget(Sample: ReservoirSample, Position: vec3f, Normal: vec3f) -> f32
{
    let Cosine = dot(Normal, ResolveSampleDirection(Sample, Position));
    return select(0.0, ResolveLuminance(Sample.Radiance) * Cosine, Cosine > 0.0);
}
