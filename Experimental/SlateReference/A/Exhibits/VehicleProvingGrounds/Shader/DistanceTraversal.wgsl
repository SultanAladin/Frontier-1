//============================================================================================================================================
//                                                          DISTANCETRAVERSAL.WGSL
//============================================================================================================================================
// 🧩 Sphere traversal of the ONE scene distance field — four nested toroidal cascades, never a walk over occupants.

// 📝 Group 1 is the scene distance field and nothing else. Every ray in the exhibit — indirect candidates, reuse
//    visibility, card shadows, card radiosity, reflections, the inspection march — is answered by this file.
//
//    A cascade is a Resolution³ lattice of rgba16float texels addressed toroidally: world voxel v lives at texel
//    v mod Resolution, so the window scrolls with the camera by rewriting only the slabs it newly covers. Because the
//    sampler repeats, uvw = position / (Resolution · VoxelExtent) lands on the right texel with no indirection.
//    Texel .x is the truncated signed distance [m]; .y is the identity of the occupant nearest the voxel centre.

//! include "DistanceRecords.wgsl"

@group(1) @binding(0) var<uniform> Distance : DistanceRecord;
@group(1) @binding(1) var SceneClip0        : texture_3d<f32>;
@group(1) @binding(2) var SceneClip1        : texture_3d<f32>;
@group(1) @binding(3) var SceneClip2        : texture_3d<f32>;
@group(1) @binding(4) var SceneClip3        : texture_3d<f32>;
@group(1) @binding(5) var SceneSampler      : sampler;
@group(1) @binding(6) var<storage, read> BrickEpochs : array<u32>;

//------------------------------------------------------------------------------------------------------------------------
//                                                  CASCADE ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

fn SampleClipTexel(Level: u32, Uvw: vec3f) -> vec4f
{
    switch (Level)
    {
        case 0u:      { return textureSampleLevel(SceneClip0, SceneSampler, Uvw, 0.0); }
        case 1u:      { return textureSampleLevel(SceneClip1, SceneSampler, Uvw, 0.0); }
        case 2u:      { return textureSampleLevel(SceneClip2, SceneSampler, Uvw, 0.0); }
        default:      { return textureSampleLevel(SceneClip3, SceneSampler, Uvw, 0.0); }
    }
}

fn LoadClipTexel(Level: u32, Texel: vec3i) -> vec4f
{
    switch (Level)
    {
        case 0u:      { return textureLoad(SceneClip0, Texel, 0); }
        case 1u:      { return textureLoad(SceneClip1, Texel, 0); }
        case 2u:      { return textureLoad(SceneClip2, Texel, 0); }
        default:      { return textureLoad(SceneClip3, Texel, 0); }
    }
}

fn ClipContains(Level: u32, Position: vec3f, MarginVoxels: f32) -> bool
{
    let Clip  = Distance.Clips[Level];
    let Voxel = Position / Clip.VoxelExtent;
    let Low   = vec3f(Clip.OriginVoxel) + vec3f(MarginVoxels);
    let High  = vec3f(Clip.OriginVoxel) + vec3f(f32(Clip.Resolution) - MarginVoxels);
    return all(Voxel >= Low) && all(Voxel <= High);
}

fn ResolveFinestClip(Position: vec3f, MarginVoxels: f32) -> u32
{
    for (var Level = 0u; Level < Distance.ClipCount; Level++)
    {
        if (ClipContains(Level, Position, MarginVoxels))
        {
            return Level;
        }
    }
    return Distance.ClipCount;
}

fn SampleClipDistance(Level: u32, Position: vec3f) -> f32
{
    return SampleClipTexel(Level, Position * Distance.Clips[Level].InverseWorldExtent).x;
}

fn ResolveClipIdentity(Level: u32, Position: vec3f) -> u32
{
    let Clip       = Distance.Clips[Level];
    let Voxel      = vec3i(floor(Position / Clip.VoxelExtent));
    let Resolution = i32(Clip.Resolution);
    let Texel      = ((Voxel % Resolution) + Resolution) % Resolution;
    return u32(LoadClipTexel(Level, Texel).y + 0.5);
}

// 📐 Central-difference gradient over ±¾ voxel; the field is a distance, so its gradient is the surface normal.
fn ResolveClipNormal(Level: u32, Position: vec3f) -> vec3f
{
    let Step = Distance.Clips[Level].VoxelExtent * 0.75;
    let Gradient = vec3f(SampleClipDistance(Level, Position + vec3f(Step, 0.0, 0.0)) - SampleClipDistance(Level, Position - vec3f(Step, 0.0, 0.0)),
                         SampleClipDistance(Level, Position + vec3f(0.0, Step, 0.0)) - SampleClipDistance(Level, Position - vec3f(0.0, Step, 0.0)),
                         SampleClipDistance(Level, Position + vec3f(0.0, 0.0, Step)) - SampleClipDistance(Level, Position - vec3f(0.0, 0.0, Step)));
    let Length = length(Gradient);
    return select(vec3f(0.0, 0.0, 1.0), Gradient / Length, Length > 1.0e-6);
}

// 📝 Distance at a point from the finest cascade holding it; the analytic plane answers outside every cascade.
fn SampleSceneDistance(Position: vec3f) -> vec2f
{
    let Level = ResolveFinestClip(Position, 1.0);
    if (Level >= Distance.ClipCount)
    {
        return vec2f(Position.z - Distance.GroundHeight, f32(Distance.ClipCount));
    }
    return vec2f(SampleClipDistance(Level, Position), f32(Level));
}

// 📝 Normal-and-stride bias that lifts a ray origin off the surface it leaves, scaled by the cascade there.
fn ResolveSurfaceBias(Position: vec3f) -> f32
{
    let Level = min(ResolveFinestClip(Position, 2.0), Distance.ClipCount - 1u);
    return Distance.Clips[Level].VoxelExtent;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SPHERE TRAVERSAL
//------------------------------------------------------------------------------------------------------------------------

struct TraversalHit
{
    Position : vec3f,   // [m]
    Travel   : f32,     // [m]   ray parameter at the surface
    Normal   : vec3f,   // [-]   distance gradient
    Identity : u32,     // [-]   occupant, GroundIdentity, or SkyIdentity on a miss
    Level    : u32,     // [-]   cascade that answered; ClipCount for the far-field plane
    Steps    : u32,     // [-]
    Hit      : bool,
}

// 📝 ① sample the finest cascade holding the point; ② a saturated sample (no surface within that cascade's band)
//    asks the next coarser cascade, whose band is twice as long, for a longer conservative stride; ③ the surface is
//    declared within a voxel fraction, widened along the ray by an optional cone; ④ past the outermost cascade the
//    analytic course plane is the far field. A traversal that exhausts its steps reports a hit rather than leaking.
fn TraverseSceneDistance(Origin: vec3f, Direction: vec3f, MinimumTravel: f32, MaximumTravel: f32, ConeSlope: f32) -> TraversalHit
{
    var Result: TraversalHit;
    Result.Hit      = false;
    Result.Identity = SkyIdentity;
    Result.Level    = Distance.ClipCount;
    Result.Steps    = 0u;
    Result.Normal   = -Direction;
    Result.Travel   = MaximumTravel;
    Result.Position = Origin + Direction * MaximumTravel;

    var Travel  = MinimumTravel;
    var Escaped = false;
    var Exhausted = true;
    for (var Step = 0u; Step < MaximumTraversalSteps; Step++)
    {
        let Position = Origin + Direction * Travel;
        let Level = ResolveFinestClip(Position, 2.0);
        if (Level >= Distance.ClipCount)
        {
            Escaped = true;
            Exhausted = false;
            break;
        }
        let Clip = Distance.Clips[Level];
        var Sampled = SampleClipDistance(Level, Position);
        var Saturation = Clip.Band * 0.9;
        var Coarser = Level + 1u;
        while (Sampled >= Saturation && Coarser < Distance.ClipCount)
        {
            if (!ClipContains(Coarser, Position, 2.0))
            {
                break;
            }
            let CoarseClip = Distance.Clips[Coarser];
            Sampled = max(Sampled, SampleClipDistance(Coarser, Position) - 0.5 * CoarseClip.VoxelExtent);
            Saturation = CoarseClip.Band * 0.9;
            Coarser++;
        }
        Result.Steps = Step + 1u;
        let Threshold = max(Clip.VoxelExtent * Distance.HitFraction, Travel * ConeSlope);
        if (Sampled < Threshold)
        {
            Result.Hit      = true;
            Result.Travel   = Travel;
            Result.Position = Position;
            Result.Level    = Level;
            Exhausted = false;
            break;
        }
        Travel += max(Sampled, Clip.VoxelExtent * 0.125);
        if (Travel > MaximumTravel)
        {
            Exhausted = false;
            break;
        }
    }

    if (Result.Hit)
    {
        Result.Normal   = ResolveClipNormal(Result.Level, Result.Position);
        let Inward      = Result.Position - Result.Normal * (0.5 * Distance.Clips[Result.Level].VoxelExtent);
        Result.Identity = ResolveClipIdentity(Result.Level, Inward);
        return Result;
    }
    if (Exhausted)
    {
        let Position = Origin + Direction * Travel;
        let Level = min(ResolveFinestClip(Position, 2.0), Distance.ClipCount - 1u);
        Result.Hit      = true;
        Result.Travel   = Travel;
        Result.Position = Position;
        Result.Level    = Level;
        Result.Normal   = ResolveClipNormal(Level, Position);
        Result.Identity = ResolveClipIdentity(Level, Position);
        return Result;
    }
    if (Escaped && Direction.z < -1.0e-4)
    {
        let GroundTravel = (Distance.GroundHeight - Origin.z) / Direction.z;
        if (GroundTravel >= MinimumTravel && GroundTravel <= MaximumTravel)
        {
            Result.Hit      = true;
            Result.Travel   = GroundTravel;
            Result.Position = Origin + Direction * GroundTravel;
            Result.Normal   = vec3f(0.0, 0.0, 1.0);
            Result.Identity = GroundIdentity;
            Result.Level    = Distance.ClipCount;
        }
    }
    return Result;
}

// 📝 Binary visibility along a segment, for reuse validation: 1 when nothing intervenes before MaximumTravel.
fn TraverseSceneVisibility(Origin: vec3f, Direction: vec3f, MinimumTravel: f32, MaximumTravel: f32) -> f32
{
    let Hit = TraverseSceneDistance(Origin, Direction, MinimumTravel, MaximumTravel, 0.0);
    return select(1.0, 0.0, Hit.Hit && Hit.Travel < MaximumTravel);
}

// 📐 Penumbra-estimating visibility toward a distant light: min over the march of k·d/t (Quílez), clamped to [0, 1].
//    Used for card shadows, where a soft cascade-resolution penumbra is the honest answer.
fn TraverseSceneShadow(Origin: vec3f, Direction: vec3f, MinimumTravel: f32, MaximumTravel: f32, Hardness: f32) -> f32
{
    var Travel = MinimumTravel;
    var Visibility = 1.0;
    for (var Step = 0u; Step < MaximumTraversalSteps; Step++)
    {
        let Position = Origin + Direction * Travel;
        let Level = ResolveFinestClip(Position, 2.0);
        if (Level >= Distance.ClipCount)
        {
            if (Direction.z < -1.0e-4)
            {
                return 0.0;
            }
            return Visibility;
        }
        let Clip = Distance.Clips[Level];
        var Sampled = SampleClipDistance(Level, Position);
        var Saturation = Clip.Band * 0.9;
        var Coarser = Level + 1u;
        while (Sampled >= Saturation && Coarser < Distance.ClipCount)
        {
            if (!ClipContains(Coarser, Position, 2.0))
            {
                break;
            }
            let CoarseClip = Distance.Clips[Coarser];
            Sampled = max(Sampled, SampleClipDistance(Coarser, Position) - 0.5 * CoarseClip.VoxelExtent);
            Saturation = CoarseClip.Band * 0.9;
            Coarser++;
        }
        if (Sampled < Clip.VoxelExtent * Distance.HitFraction)
        {
            return 0.0;
        }
        Visibility = min(Visibility, Hardness * Sampled / max(Travel, 1.0e-3));
        Travel += max(Sampled, Clip.VoxelExtent * 0.125);
        if (Travel > MaximumTravel)
        {
            break;
        }
    }
    return clamp(Visibility, 0.0, 1.0);
}
