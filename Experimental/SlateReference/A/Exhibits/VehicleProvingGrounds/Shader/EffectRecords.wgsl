//============================================================================================================================================
//                                                            EFFECTRECORDS.WGSL
//============================================================================================================================================
// 🧩 The derez / rez effect as both the surface raster and the effect raster read it: emitters, the vehicle mask, the release clock.

// 📝 One clock decides everything. A point of the car (or of a cone's box) belongs to a cell of a 16³ lattice over its
//    bounds; the cell's release time is a wave across the bounds plus a hashed jitter. The surface raster discards a
//    car pixel once its cell is released (glowing white-hot, then cyan, just before), and the effect raster launches
//    a cube from every anchor of that cell at the same instant — so the car comes apart exactly where the cubes leave
//    it, with no state shared between the two but this function. Rez runs it backwards: cubes fly in and each cell
//    of the car appears as its cubes arrive.
//
//    Colour: with Colour.x set, every cube is the car — each anchor carries the material family of the facet it was
//    spread on, so the lime coat comes away as lime cubes, the glass as glass, the tyres as rubber — and glows in
//    that colour (G-buffer Attributes.w = 1 + glow: the recombination adds albedo × glow as emission). Otherwise the
//    Particle Editor's palette families (cyan glow, white-hot core, glitch pink) stand in.
//
//    Momentum: the car keeps moving while it comes apart, so a cell must leave from where the car is when that cell
//    lets go. EffectSequence samples the car's pose and velocity at 60 Hz from the derez on (Colour.z samples from
//    clock Colour.w); a cube launches from the pose at its release time with the car's velocity there (plus the spin
//    about the centre of mass) — the falling blocks carry on with the car.

struct EffectEmitter
{
    BodyToWorld : mat4x4f,   // the emitter's pose when it started (local → world)
    Velocity    : vec4f,     // xyz [m/s] velocity the cubes inherit, w [s] start on the effect clock
    Angular     : vec4f,     // xyz [rad/s] spin they inherit, w kind (EffectKind*)
    Extent      : vec4f,     // xyz [m] half extent of the local bounds, w particle count
    Centre      : vec4f,     // xyz [m] centre of the local bounds, w seed
}

struct EffectRecord
{
    Clock       : f32,       // [s]  effect clock now
    MaskMode    : u32,       // [-]  vehicle mask: 0 off, 1 derez, 2 rez, 3 hidden
    MaskStart   : f32,       // [s]  clock the mask started at
    Style       : u32,       // [-]  0 cubes, 1 glitch
    BodyMinimum : vec4f,     // xyz [m] the car's local bounds, w hold offset [s]
    BodyMaximum : vec4f,     // xyz [m], w frame seconds (motion vectors)
    Motion      : vec4f,     // x burst [m/s], y slowest launch [m/s], z fastest launch [m/s], w drag [1/s]
    Timing      : vec4f,     // x cube life [s], y cube size [m], z wave spread [s], w rez flight [s]
    Colour      : vec4f,     // x face colours (1) or palette (0), y glow strength [-], z pose samples, w first sample's clock [s]
    Emitters    : array<EffectEmitter, 8>,
}

// 📝 One sample of the car's motion during a derez (EffectSequence.RecordPose), 60 Hz on the effect clock.
struct EffectPose
{
    Position : vec4f,        // xyz [m] centre of mass, w [s] clock
    Rotation : vec4f,        // body → world quaternion (x, y, z, w)
    Velocity : vec4f,        // xyz [m/s]
    Angular  : vec4f,        // xyz [rad/s]
}

const EffectPoseSpacing : f32 = 1.0 / 60.0;   // [s]

fn RotateByQuaternion(Rotation: vec4f, Vector: vec3f) -> vec3f
{
    let T = 2.0 * cross(Rotation.xyz, Vector);
    return Vector + Rotation.w * T + cross(Rotation.xyz, T);
}

fn ResolveFaceGlow(Seconds: f32, Strength: f32) -> f32
{
    // 📝 A flash at release, then an afterglow a third of a second long.
    return Strength * (select(0.0, 2.5, Seconds < EffectHotSeconds) + exp(-max(Seconds, 0.0) / 0.35));
}

const EffectKindVehicleDerez : u32 = 0u;
const EffectKindVehicleRez   : u32 = 1u;
const EffectKindBoxDerez     : u32 = 2u;
const EffectKindBoxRez       : u32 = 3u;
const EffectKindDebris       : u32 = 4u;   // a burst tyre's rubber: released at once, thrown hard, never glowing
const EffectCellsPerAxis     : f32 = 16.0;
const EffectHotSeconds       : f32 = 0.07;   // [s] white-hot just before release / after arrival
const EffectGlowSeconds      : f32 = 0.22;   // [s] cyan glow before that

fn HashEffect(Value: u32) -> f32
{
    var State = Value * 747796405u + 2891336453u;
    State = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    State = (State >> 22u) ^ State;
    return f32(State) / 4294967296.0;
}

fn HashEffectCell(Cell: vec3u, Seed: u32) -> f32
{
    return HashEffect((Cell.x * 73856093u) ^ (Cell.y * 19349663u) ^ (Cell.z * 83492791u) ^ (Seed * 2654435761u));
}

// 📝 Unit: the point within the emitter's bounds, [0, 1]³. Glitch style uses coarser, uneven 7 × 5 × 7 cells and
//    rises from the floor; cubes style sweeps the diagonal of 16³ cells.
fn ResolveEffectCell(Unit: vec3f, Style: u32) -> vec3u
{
    let Divisions = select(vec3f(EffectCellsPerAxis), vec3f(7.0, 5.0, 7.0), Style == 1u);
    return vec3u(clamp(Unit, vec3f(0.0), vec3f(0.9999)) * Divisions);
}

fn ResolveEffectWave(Unit: vec3f, Style: u32) -> f32
{
    let Divisions = select(vec3f(EffectCellsPerAxis), vec3f(7.0, 5.0, 7.0), Style == 1u);
    let Centre = (vec3f(ResolveEffectCell(Unit, Style)) + 0.5) / Divisions;
    return select((Centre.x + Centre.y + Centre.z) / 3.0, 0.4 * Centre.z, Style == 1u);
}

// 📝 Seconds after the derez starts at which this cell lets go. Scale shortens it for small emitters (cones).
fn ResolveDerezRelease(Unit: vec3f, Seed: u32, Style: u32, Scale: f32) -> f32
{
    let Jitter = HashEffectCell(ResolveEffectCell(Unit, Style), Seed);
    let Spread = select(0.15 * Jitter, 1.0 * Jitter, Style == 1u);
    return Scale * (Effect.BodyMinimum.w + Effect.Timing.z * (ResolveEffectWave(Unit, Style) + Spread));
}

// 📝 Seconds after the rez starts at which this cell is complete again.
fn ResolveRezArrival(Unit: vec3f, Seed: u32, Style: u32, Scale: f32) -> f32
{
    let Jitter = HashEffectCell(ResolveEffectCell(Unit, Style), Seed ^ 0x9E3779B9u);
    return Scale * (0.3 + Effect.Timing.w * (ResolveEffectWave(Unit, Style) + 0.15 * Jitter));
}
