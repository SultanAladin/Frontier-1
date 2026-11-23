//============================================================================================================================================
//                                                           SCREENTRAVERSAL.WGSL
//============================================================================================================================================
// 🧩 Screen traces ahead of the scene distance field: a walk over the nearest-depth pyramid of this frame, as Lumen does.

// 📝 Lumen answers a gather or reflection ray in two parts. It first walks the ray across the screen through a pyramid of
//    nearest depths, which resolves contact detail far finer than any distance-field voxel. A ray the screen cannot
//    settle — it leaves the viewport, passes behind a surface, strikes the back of one, or exhausts its steps — goes on
//    through the ONE scene distance field, starting from the last point the screen proved unoccluded. That point is
//    sound: wherever the ray lies in front of the nearest depth of a tile, nothing rastered stands between it and the eye,
//    and everything inside the viewport is rastered. Group 3 carries what the walk reads.
//
//    The radiance a sample point sends back follows one rule for every consumer (ResolveSampleExitance): the diffuse
//    exitance the previous frame resolved at that point — the quantity a surface card stores, at screen resolution with
//    shadow-map visibility — where this frame's screen shows the point and the previous frame's depth confirms it was
//    visible there too; the surface cards everywhere else. Card radiance and screen radiance never differ in kind.

//! include "CardSampling.wgsl"
//! include "PackingCodec.wgsl"

@group(3) @binding(0) var NearestDepth      : texture_2d<f32>;   // [-] reversed depth; level L holds the nearest of each 2^L tile
@group(3) @binding(1) var PriorNearestDepth : texture_2d<f32>;   // [-] the same pyramid one frame earlier; level 0 is read
@group(3) @binding(2) var ScreenAttributes  : texture_2d<f32>;   // [-] octahedral normal in xy, material family in z
@group(3) @binding(4) var ScreenIdentity    : texture_2d<f32>;   // [-] occupant identity (GroundIdentity for the course plane), r16float
@group(3) @binding(3) var PriorExitance     : texture_2d<f32>;   // [-] previous frame's diffuse exitance per pixel

const ScreenSinkTolerance     : f32 = 0.0015;   // [-] relative depth a grazing ray may sink below a tile and still pass over it
const ScreenRelativeThickness : f32 = 0.02;     // [-] relative depth behind a surface that still counts as striking it
const ScreenAbsoluteThickness : f32 = 0.03;     // [m] floor of that thickness near the eye
const ScreenExitanceAgreement : f32 = 0.02;     // [-] relative depth agreement that confirms a point in a depth lattice
const ScreenUnreachedTravel   : f32 = 1.0e30;   // [-] parameter of a boundary the walk can never reach

struct ScreenHit
{
    Position : vec3f,   // [m]   on the ray, at the struck surface
    Travel   : f32,     // [m]   ray parameter at the strike, or at the last point the screen proved unoccluded
    Normal   : vec3f,   // [-]   shading normal of the struck pixel
    Identity : u32,     // [-]   occupant identity of the struck pixel (GroundIdentity for the course plane)
    Hit      : bool,
}

// 📝 World → (u, v, reversed depth) through this frame's jittered projection, the one the depth was rastered with.
fn ProjectScreenPoint(Position: vec3f) -> vec3f
{
    let Clip = Frame.ViewProjection * vec4f(Position, 1.0);
    let Ndc = Clip.xyz / Clip.w;
    return vec3f(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5, Ndc.z);
}

fn ReconstructScreenPoint(Point: vec3f) -> vec3f
{
    let Clip = Frame.InverseViewProjection * vec4f(Point.x * 2.0 - 1.0, 1.0 - Point.y * 2.0, Point.z, 1.0);
    return Clip.xyz / Clip.w;
}

// 📝 Normal offset of a screen-trace origin: about two pixels of footprint, far below the distance-field bias, so the
//    walk keeps the creases the distance field cannot resolve.
fn ResolveScreenBias(ViewDepth: f32) -> f32
{
    return 0.004 + 0.002 * ViewDepth;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  NEAREST-DEPTH WALK
//------------------------------------------------------------------------------------------------------------------------

// 📐 Stackless walk over the pyramid in (u, v, d) space, where a world segment stays a segment because the projection is
//    projective. At level L the ray either ① lies in front of the tile's nearest depth and crosses to the tile's exit
//    boundary — then it climbs a level — or ② meets the tile's nearest-depth plane first, or already lies behind it —
//    then it descends. Descending below level 0 means the ray has met the depth surface at one pixel, which is a strike
//    if the ray lies within the surface thickness, faces the surface, and has left its own neighbourhood.
fn TraverseScreenDepth(Origin: vec3f, Direction: vec3f, MaximumTravel: f32, StepLimit: u32) -> ScreenHit
{
    var Result: ScreenHit;
    Result.Hit      = false;
    Result.Travel   = 0.0;
    Result.Position = Origin;
    Result.Normal   = -Direction;
    Result.Identity = SkyIdentity;

    // ① keep the far end in front of the eye, then clip the projected segment to the viewport
    let OriginDepth = ResolveViewDepth(Origin);
    let DepthRate = -(Frame.View * vec4f(Direction, 0.0)).z;
    var Reach = MaximumTravel;
    if (DepthRate < 0.0)
    {
        Reach = min(Reach, (OriginDepth - 2.0 * Frame.CameraNear) / -DepthRate);
    }
    if (OriginDepth <= 2.0 * Frame.CameraNear || Reach <= 0.0)
    {
        return Result;
    }
    let Start = ProjectScreenPoint(Origin);
    if (any(Start.xy <= vec2f(0.0)) || any(Start.xy >= vec2f(1.0)))
    {
        return Result;
    }
    let Projected = ProjectScreenPoint(Origin + Direction * Reach) - Start;
    var Exit = 1.0;
    for (var Axis = 0u; Axis < 2u; Axis++)
    {
        if (Projected[Axis] > 0.0)
        {
            Exit = min(Exit, (1.0 - Start[Axis]) / Projected[Axis]);
        }
        else if (Projected[Axis] < 0.0)
        {
            Exit = min(Exit, -Start[Axis] / Projected[Axis]);
        }
    }
    // 📝 Re-parameterised over the visible part only, so one pixel is never a vanishing fraction of the parameter.
    let Delta = Projected * Exit;

    let BaseExtent = vec2f(textureDimensions(NearestDepth, 0));
    let LevelCount = i32(textureNumLevels(NearestDepth));
    let Moving     = abs(Delta) > vec3f(1.0e-12);
    let Inverse    = select(vec3f(ScreenUnreachedTravel), 1.0 / select(vec3f(1.0), Delta, Moving), Moving);
    // 📝 A component that does not move takes the forward side, so its boundary lies at +∞ rather than −∞.
    let Receding   = (Delta.xy < vec2f(0.0)) & Moving.xy;
    let ExitSide   = select(vec2f(1.0), vec2f(0.0), Receding);
    let Nudge      = select(vec2f(0.005), vec2f(-0.005), Receding) / BaseExtent;

    // ② leave the origin pixel before the first comparison: a surface never occludes itself
    let StartPixel = floor(Start.xy * BaseExtent);
    let FirstCrossing = ((StartPixel + ExitSide) / BaseExtent + Nudge - Start.xy) * Inverse.xy;
    var Travel = min(FirstCrossing.x, FirstCrossing.y);

    // ③ the walk
    var Level = 0;
    var Descended = false;
    for (var Iteration = 0u; Iteration < StepLimit; Iteration++)
    {
        if (Travel > 1.0)
        {
            break;
        }
        let Point      = Start + Delta * Travel;
        let Resolution = BaseExtent * exp2(-f32(Level));
        let Cell       = floor(Point.xy * Resolution);
        let Texel      = clamp(vec2i(Cell), vec2i(0), vec2i(textureDimensions(NearestDepth, Level)) - vec2i(1));
        let Tolerated  = textureLoad(NearestDepth, Texel, Level).r * (1.0 - ScreenSinkTolerance);
        let Crossing   = ((Cell + ExitSide) / Resolution + Nudge - Start.xy) * Inverse.xy;
        let Boundary   = min(Crossing.x, Crossing.y);
        var Sinking    = ScreenUnreachedTravel;
        if (Delta.z < 0.0 && Moving.z)
        {
            Sinking = (Tolerated - Start.z) * Inverse.z;
        }
        let InFront = Point.z > Tolerated;
        if (InFront)
        {
            Travel = min(Boundary, Sinking);
        }
        if (InFront && Boundary < Sinking)
        {
            Level = min(Level + 1, LevelCount - 1);
        }
        else
        {
            Level--;
            if (Level < 0)
            {
                Descended = true;
                break;
            }
        }
    }

    // ④ the last point the walk proved unoccluded is where the distance field takes over
    let Settled = Start + Delta * clamp(Travel, 0.0, 1.0);
    Result.Travel = max(dot(ReconstructScreenPoint(Settled) - Origin, Direction), 0.0);
    if (!Descended)
    {
        return Result;
    }

    // ⑤ validate the strike
    let Pixel = vec2i(floor(Settled.xy * BaseExtent));
    if (any(Pixel < vec2i(0)) || any(Pixel >= vec2i(BaseExtent)) || all(abs(Pixel - vec2i(StartPixel)) <= vec2i(1)))
    {
        return Result;
    }
    let SurfaceDepth = textureLoad(NearestDepth, Pixel, 0).r;
    if (SurfaceDepth <= 0.0)
    {
        return Result;
    }
    let Attributes = textureLoad(ScreenAttributes, Pixel, 0);
    let Normal = DecodeOctahedral(Attributes.xy);
    if (dot(Normal, Direction) >= 0.0)
    {
        return Result;
    }
    let SurfaceView = Frame.CameraNear / SurfaceDepth;
    let RayView     = Frame.CameraNear / max(Settled.z, 1.0e-12);
    if (RayView - SurfaceView > max(ScreenRelativeThickness * SurfaceView, ScreenAbsoluteThickness))
    {
        return Result;
    }
    Result.Hit      = true;
    Result.Position = Origin + Direction * Result.Travel;
    Result.Normal   = Normal;
    Result.Identity = u32(Attributes.w + 0.5);
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SAMPLE EXITANCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The previous frame's diffuse exitance at a world point, or w = 0 where it cannot be trusted. ① This frame's screen
//    must show the point at its pixel — same depth, same occupant — so a point hidden now never borrows the surface in
//    front of it. ② A point on a moving occupant is carried back by that occupant's previous transform. ③ The previous
//    frame's depth must agree at the reprojected pixel, which rejects every disocclusion.
fn ResolvePriorExitance(Position: vec3f, Identity: u32) -> vec4f
{
    let Point = ProjectScreenPoint(Position);
    if (any(Point.xy <= vec2f(0.0)) || any(Point.xy >= vec2f(1.0)) || Point.z <= 0.0)
    {
        return vec4f(0.0);
    }
    let Extent = vec2f(textureDimensions(NearestDepth, 0));
    let Pixel = vec2i(Point.xy * Extent);
    let Surface = textureLoad(NearestDepth, Pixel, 0).r;
    if (Surface <= 0.0 || abs(Point.z - Surface) > ScreenExitanceAgreement * Surface)
    {
        return vec4f(0.0);
    }
    if (u32(textureLoad(ScreenIdentity, Pixel, 0).r + 0.5) != Identity)
    {
        return vec4f(0.0);
    }

    var Prior = Position;
    if (Identity < arrayLength(&Occupants))
    {
        let Occupant = Occupants[Identity];
        if ((Occupant.Roles & RoleDynamic) != 0u)
        {
            Prior = (Occupant.PreviousLocalToWorld * (Occupant.WorldToLocal * vec4f(Position, 1.0))).xyz;
        }
    }
    let PriorClip = Frame.PreviousUnjitteredViewProjection * vec4f(Prior, 1.0);
    if (PriorClip.w <= Frame.CameraNear)
    {
        return vec4f(0.0);
    }
    let PriorNdc = PriorClip.xyz / PriorClip.w;
    let PriorPoint = vec2f(PriorNdc.x * 0.5 + 0.5, 0.5 - PriorNdc.y * 0.5);
    if (any(PriorPoint <= vec2f(0.0)) || any(PriorPoint >= vec2f(1.0)))
    {
        return vec4f(0.0);
    }
    let PriorPixel = vec2i(PriorPoint * Extent);
    let PriorSurface = textureLoad(PriorNearestDepth, PriorPixel, 0).r;
    if (PriorSurface <= 0.0 || abs(PriorNdc.z - PriorSurface) > ScreenExitanceAgreement * PriorSurface)
    {
        return vec4f(0.0);
    }
    return vec4f(textureLoad(PriorExitance, PriorPixel, 0).rgb, 1.0);
}

// 📝 Radiance leaving a sample point, by the one rule above. Returns (radiance, coverage) as SampleCardRadiance does.
//    A temporal restart (a lighting discontinuity, a resize) leaves the previous frame stale, so the cards answer alone.
fn ResolveSampleExitance(Position: vec3f, Normal: vec3f, Identity: u32, Tolerance: f32) -> vec4f
{
    if (FeatureEnabled(Frame, FeatureScreenTraversal) && !FeatureEnabled(Frame, FeatureTemporalRestart))
    {
        let Screen = ResolvePriorExitance(Position, Identity);
        if (Screen.w > 0.0)
        {
            return Screen;
        }
    }
    return SampleCardRadiance(Position, Normal, Identity, Tolerance);
}

// 📝 What a ray the distance field finished brings back: the sky on an escape, the sample exitance at a surface.
fn ResolveContinuedRadiance(Hit: TraversalHit, Direction: vec3f) -> vec3f
{
    if (!Hit.Hit)
    {
        return SampleSkyRadiance(Direction);
    }
    let Exitance = ResolveSampleExitance(Hit.Position, Hit.Normal, Hit.Identity, ResolveCardTolerance(Hit.Level));
    return select(vec3f(0.0), Exitance.rgb, Exitance.w > 0.0);
}

// 📝 What a screen strike brings back. The strike lies on the rastered surface itself, so the card tolerance is that of
//    the finest cascade holding the point.
fn ResolveStrikeRadiance(Strike: ScreenHit) -> vec3f
{
    let Tolerance = ResolveCardTolerance(ResolveFinestClip(Strike.Position, 1.0));
    let Exitance = ResolveSampleExitance(Strike.Position, Strike.Normal, Strike.Identity, Tolerance);
    return select(vec3f(0.0), Exitance.rgb, Exitance.w > 0.0);
}
