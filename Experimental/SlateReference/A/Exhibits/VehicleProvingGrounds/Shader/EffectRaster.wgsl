//============================================================================================================================================
//                                                             EFFECTRASTER.WGSL
//============================================================================================================================================
// 🧩 Derez and rez cubes, drawn into the surface projection like any surface — so they are lit, glow and reflect in the GI.

// 📝 No particle state lives anywhere. Each cube's whole life is a closed-form function of its emitter, its anchor
//    and the clock: it waits on the surface until its cell's release, launches with the emitter's velocity plus a
//    burst along the surface normal and a hashed scatter, flies under gravity and linear drag (v' = g − k·v, solved
//    exactly), and when it meets the floor it shatters into eight children that tumble out and shrink away. A rez
//    cube flies the other way, from a shell around the emitter onto its anchor, eased and arcing. The vertex program
//    evaluates that function for the frame's clock (and for the clock a frame ago, for motion vectors); nothing is
//    simulated, nothing is read back, and an emitter costs one instanced draw while it lasts.
//
//    Instance = particle (EmitterIndex · 4096 + index), vertex = 36 corners × (parent + eight children).

//! include "FrameDescriptor.wgsl"
//! include "PackingCodec.wgsl"
//! include "EffectRecords.wgsl"

@group(0) @binding(1) var<uniform> Effect             : EffectRecord;
@group(0) @binding(2) var<storage, read> VehicleCells : array<vec4f>;   // per cell: local anchor (w its facet's family), local normal
@group(0) @binding(3) var<storage, read> Poses        : array<EffectPose>;

const Gravity      : f32 = 9.81;
const ChildLife    : f32 = 1.2;    // [s]
const Shell        : f32 = 2.6;    // [m] rez cubes start this far out

struct EffectVarying
{
    @builtin(position) Position             : vec4f,
    @location(0) Normal                     : vec3f,
    @location(1) @interpolate(flat) Family  : u32,
    @location(2) CurrentClip                : vec4f,
    @location(3) PreviousClip               : vec4f,
    @location(4) WorldPosition              : vec3f,
    @location(5) @interpolate(flat) Glow    : f32,
}

struct CubeState
{
    Centre  : vec3f,
    Size    : f32,     // [m] edge; 0 hides the cube
    Spin    : vec4f,   // xyz axis, w angle
    Family  : u32,
    Glow    : f32,     // [-] emission as a multiple of the family's albedo (face colours)
}

// 📝 The car's pose at Clock, interpolated between the 60 Hz samples (EffectPose); clamped to the recorded span.
fn SamplePose(Clock: f32) -> EffectPose
{
    let Count = u32(Effect.Colour.z);
    let Cursor = clamp((Clock - Effect.Colour.w) / EffectPoseSpacing, 0.0, f32(Count - 1u));
    let Index = min(u32(floor(Cursor)), Count - 2u);
    let Fraction = clamp(Cursor - f32(Index), 0.0, 1.0);
    let A = Poses[Index];
    let B = Poses[Index + 1u];
    var Pose: EffectPose;
    Pose.Position = mix(A.Position, B.Position, Fraction);
    let Aligned = select(B.Rotation, -B.Rotation, dot(A.Rotation, B.Rotation) < 0.0);
    Pose.Rotation = normalize(mix(A.Rotation, Aligned, Fraction));
    Pose.Velocity = mix(A.Velocity, B.Velocity, Fraction);
    Pose.Angular = mix(A.Angular, B.Angular, Fraction);
    return Pose;
}

// 📝 The family a cube wears: the facet it was spread on (the car), the cone's band by height (a cone), else the
//    emitter's own (BodyToWorld[0].w, which the affine transform leaves free).
fn ResolveFaceFamily(Emitter: EffectEmitter, Kind: u32, Particle: u32, Unit: vec3f) -> u32
{
    if (Kind == EffectKindVehicleDerez || Kind == EffectKindVehicleRez)
    {
        return u32(VehicleCells[Particle * 2u].w + 0.5);
    }
    let Family = u32(Emitter.BodyToWorld[0].w + 0.5);
    if (Family == ConeOrangeFamily)
    {
        if (Unit.z < 0.12) { return ConeBaseFamily; }
        if (Unit.z > 0.45 && Unit.z < 0.66) { return ConeBandFamily; }
    }
    return Family;
}

fn RotateAxisAngle(Vector: vec3f, Axis: vec3f, Angle: f32) -> vec3f
{
    let C = cos(Angle);
    let S = sin(Angle);
    return Vector * C + cross(Axis, Vector) * S + Axis * dot(Axis, Vector) * (1.0 - C);
}

fn HashDirection(Seed: u32) -> vec3f
{
    let U = HashEffect(Seed);
    let V = HashEffect(Seed ^ 0x68E31DA4u);
    let Z = 2.0 * U - 1.0;
    let R = sqrt(max(0.0, 1.0 - Z * Z));
    let Phi = 6.2831853 * V;
    return vec3f(R * cos(Phi), R * sin(Phi), Z);
}

// 📐 Linear drag k under gravity: v(τ) = g/k + (v₀ − g/k)·e^(−kτ),  x(τ) = x₀ + (g/k)·τ + (v₀ − g/k)·(1 − e^(−kτ))/k.
fn Ballistic(Origin: vec3f, Velocity: vec3f, Drag: f32, Time: f32) -> vec3f
{
    let Terminal = vec3f(0.0, 0.0, -Gravity) / Drag;
    return Origin + Terminal * Time + (Velocity - Terminal) * (1.0 - exp(-Drag * Time)) / Drag;
}

fn BallisticVelocity(Velocity: vec3f, Drag: f32, Time: f32) -> vec3f
{
    let Terminal = vec3f(0.0, 0.0, -Gravity) / Drag;
    return Terminal + (Velocity - Terminal) * exp(-Drag * Time);
}

// 📝 First time the cube's bottom meets the floor (z = Floor), by bisection on the ballistic height; Limit if never.
fn ResolveImpact(Origin: vec3f, Velocity: vec3f, Drag: f32, Floor: f32, Limit: f32) -> f32
{
    if (Ballistic(Origin, Velocity, Drag, Limit).z > Floor)
    {
        return Limit;
    }
    // 📝 Height rises then falls, so step to the first sample below the floor, then bisect that bracket.
    var Low = 0.0;
    var High = Limit;
    for (var Step = 1; Step <= 8; Step++)
    {
        let Time = Limit * f32(Step) / 8.0;
        if (Ballistic(Origin, Velocity, Drag, Time).z <= Floor)
        {
            High = Time;
            break;
        }
        Low = Time;
    }
    for (var Iteration = 0; Iteration < 10; Iteration++)
    {
        let Middle = 0.5 * (Low + High);
        if (Ballistic(Origin, Velocity, Drag, Middle).z <= Floor) { High = Middle; } else { Low = Middle; }
    }
    return High;
}

fn ResolveAnchor(Emitter: EffectEmitter, Kind: u32, Particle: u32, Normal: ptr<function, vec3f>) -> vec3f
{
    if (Kind == EffectKindVehicleDerez || Kind == EffectKindVehicleRez)
    {
        *Normal = VehicleCells[Particle * 2u + 1u].xyz;
        return VehicleCells[Particle * 2u].xyz;
    }
    // 📝 A box emitter: a 6 × 6 grid on each of its six faces.
    let Face = min(Particle / 36u, 5u);
    let Cell = Particle % 36u;
    let Grid = (vec2f(f32(Cell % 6u), f32(Cell / 6u)) + 0.5) / 6.0 * 2.0 - 1.0;
    let Axis = Face >> 1u;
    let Sign = select(-1.0, 1.0, (Face & 1u) == 1u);
    var Local = vec3f(0.0);
    var Facing = vec3f(0.0);
    if (Axis == 0u)      { Local = vec3f(Sign, Grid.x, Grid.y); Facing = vec3f(Sign, 0.0, 0.0); }
    else if (Axis == 1u) { Local = vec3f(Grid.x, Sign, Grid.y); Facing = vec3f(0.0, Sign, 0.0); }
    else                 { Local = vec3f(Grid.x, Grid.y, Sign); Facing = vec3f(0.0, 0.0, Sign); }
    *Normal = Facing;
    return Emitter.Centre.xyz + Local * Emitter.Extent.xyz;
}

fn ResolveCube(Emitter: EffectEmitter, Particle: u32, Child: u32, Clock: f32) -> CubeState
{
    var Cube: CubeState;
    Cube.Size = 0.0;
    Cube.Family = DerezGlowFamily;
    Cube.Spin = vec4f(0.0, 0.0, 1.0, 0.0);
    Cube.Glow = 0.0;
    let Kind = u32(Emitter.Angular.w);
    let Count = u32(Emitter.Extent.w);
    if (Particle >= Count)
    {
        return Cube;
    }
    let Seed = u32(Emitter.Centre.w) ^ (Particle * 9781u);
    var LocalNormal = vec3f(0.0, 0.0, 1.0);
    let Anchor = ResolveAnchor(Emitter, Kind, Particle, &LocalNormal);
    let Vehicle = Kind == EffectKindVehicleDerez || Kind == EffectKindVehicleRez;
    let Minimum = select(Emitter.Centre.xyz - Emitter.Extent.xyz, Effect.BodyMinimum.xyz, Vehicle);
    let Maximum = select(Emitter.Centre.xyz + Emitter.Extent.xyz, Effect.BodyMaximum.xyz, Vehicle);
    let Unit = (Anchor - Minimum) / max(Maximum - Minimum, vec3f(1.0e-3));
    let Scale = select(0.45, 1.0, Vehicle);
    var World = (Emitter.BodyToWorld * vec4f(Anchor, 1.0)).xyz;
    var Normal = normalize((Emitter.BodyToWorld * vec4f(LocalNormal, 0.0)).xyz);
    let Elapsed = Clock - Emitter.Velocity.w;
    let Faces = Effect.Colour.x > 0.5;
    let FaceFamily = ResolveFaceFamily(Emitter, Kind, Particle, Unit);
    let Debris = Kind == EffectKindDebris;
    let Size = Effect.Timing.y * select(select(0.75, 0.6, Debris), 1.0, Vehicle) * (0.75 + 0.5 * HashEffect(Seed ^ 0x51u));
    let SpinAxis = HashDirection(Seed ^ 0x3Au);
    let SpinRate = 4.0 + 10.0 * HashEffect(Seed ^ 0x7Bu);

    if (Kind == EffectKindVehicleRez || Kind == EffectKindBoxRez)
    {
        // 📝 Rez: from the shell onto the anchor, arriving when the cell completes; only the parent flies.
        if (Child != 0u)
        {
            return Cube;
        }
        let Arrival = ResolveRezArrival(Unit, u32(Emitter.Centre.w), Effect.Style, Scale);
        if (Elapsed < 0.0 || Elapsed >= Arrival)
        {
            return Cube;
        }
        let Centre = (Emitter.BodyToWorld * vec4f(Emitter.Centre.xyz, 1.0)).xyz;
        let Outward = normalize(normalize(World - Centre) + 0.6 * HashDirection(Seed ^ 0x2Cu) + vec3f(0.0, 0.0, 0.35));
        let Start = World + Outward * Shell * Scale * (0.7 + 0.6 * HashEffect(Seed ^ 0x1Du));
        let Progress = clamp(Elapsed / Arrival, 0.0, 1.0);
        let Eased = 1.0 - pow(1.0 - Progress, 3.0);
        Cube.Centre = mix(Start, World, Eased) + vec3f(0.0, 0.0, 0.5 * Scale * sin(3.14159265 * Progress));
        Cube.Size = Size * (0.6 + 0.4 * Progress);
        Cube.Spin = vec4f(SpinAxis, SpinRate * (Arrival - Elapsed));
        Cube.Family = select(DerezGlowFamily, DerezHotFamily, Progress > 0.82);
        if (Effect.Style == 1u)
        {
            Cube.Family = select(DerezGlitchFamily, DerezHotFamily, Progress > 0.82);
            Cube.Centre += 0.06 * (vec3f(HashEffect(Seed ^ u32(Clock * 12.0)), HashEffect(Seed ^ u32(Clock * 12.0) ^ 7u), 0.5) - 0.5);
        }
        if (Faces)
        {
            // 📝 Assembling: each cube already wears its facet's colour, glowing brighter as it closes on its place.
            Cube.Family = FaceFamily;
            Cube.Glow = Effect.Colour.y * (0.35 + 2.0 * smoothstep(0.7, 1.0, Progress));
        }
        return Cube;
    }

    // 📝 Derez: wait for the release, launch, fall, shatter.
    let Release = select(ResolveDerezRelease(Unit, u32(Emitter.Centre.w), Effect.Style, Scale), 0.04 * HashEffect(Seed ^ 0x13u), Debris);
    let Flight = Elapsed - Release;
    let Life = select(Effect.Timing.x * (0.85 + 0.3 * HashEffect(Seed ^ 0x4Eu)), 2.2 + 0.6 * HashEffect(Seed ^ 0x4Eu), Debris);
    if (Flight < 0.0 || Flight > Life)
    {
        return Cube;
    }
    let Drag = select(max(Effect.Motion.w, 0.05), 0.5, Debris);
    let Launch = select(mix(Effect.Motion.y, Effect.Motion.z, HashEffect(Seed ^ 0x65u)), 2.0 + 5.0 * HashEffect(Seed ^ 0x65u), Debris);
    var Inherited = Emitter.Velocity.xyz + cross(Emitter.Angular.xyz, World - Emitter.BodyToWorld[3].xyz);
    if (Kind == EffectKindVehicleDerez && Effect.Colour.z >= 2.0)
    {
        // 📝 The car kept moving: leave from where it was at this cell's release, at its velocity there.
        let Pose = SamplePose(Emitter.Velocity.w + Release);
        World = Pose.Position.xyz + RotateByQuaternion(Pose.Rotation, Anchor);
        Normal = normalize(RotateByQuaternion(Pose.Rotation, LocalNormal));
        Inherited = Pose.Velocity.xyz + cross(Pose.Angular.xyz, World - Pose.Position.xyz);
    }
    let Velocity = Inherited + Normal * select(Effect.Motion.x, 3.0, Debris) + HashDirection(Seed ^ 0x99u) * Launch + vec3f(0.0, 0.0, 0.6 * Launch);
    let Floor = 0.5 * Size;
    let Impact = ResolveImpact(World, Velocity, Drag, Floor, Life);
    if (Child == 0u)
    {
        if (Flight >= Impact)
        {
            return Cube;
        }
        Cube.Centre = Ballistic(World, Velocity, Drag, Flight);
        Cube.Size = Size;
        Cube.Spin = vec4f(SpinAxis, SpinRate * Flight);
        Cube.Family = select(DerezGlowFamily, DerezHotFamily, Flight < EffectHotSeconds);
        if (Effect.Style == 1u)
        {
            Cube.Family = select(DerezGlitchFamily, DerezGlowFamily, (u32(Clock * 12.0) + Particle) % 3u == 0u);
            Cube.Centre += 0.06 * (vec3f(HashEffect(Seed ^ u32(Clock * 12.0)), HashEffect(Seed ^ u32(Clock * 12.0) ^ 7u), 0.5) - 0.5);
        }
        if (Faces || Debris)
        {
            Cube.Family = FaceFamily;
            Cube.Glow = select(ResolveFaceGlow(Flight, Effect.Colour.y), 0.0, Debris);
        }
        return Cube;
    }
    // 📝 Children: born at the impact point with the parent's bounce and a corner's direction, ground-clamped.
    let ChildTime = Flight - Impact;
    if (Impact >= Life || ChildTime < 0.0 || ChildTime > ChildLife)
    {
        return Cube;
    }
    let Hit = Ballistic(World, Velocity, Drag, Impact);
    let Incoming = BallisticVelocity(Velocity, Drag, Impact);
    let Corner = vec3f(select(-1.0, 1.0, ((Child - 1u) & 1u) != 0u), select(-1.0, 1.0, ((Child - 1u) & 2u) != 0u),
                       select(-1.0, 1.0, ((Child - 1u) & 4u) != 0u));
    let Scatter = normalize(Corner + 0.6 * HashDirection(Seed ^ (Child * 131u)));
    let Bounce = vec3f(Incoming.xy * 0.35, abs(Incoming.z) * 0.25);
    let ChildVelocity = Bounce + Scatter * (0.8 + 1.0 * HashEffect(Seed ^ (Child * 17u))) + vec3f(0.0, 0.0, 0.9);
    var Position = Ballistic(Hit + Corner * 0.25 * Size, ChildVelocity, 1.2, ChildTime);
    let ChildSize = 0.45 * Size * (1.0 - smoothstep(0.65 * ChildLife, ChildLife, ChildTime));
    Position.z = max(Position.z, 0.5 * ChildSize);
    Cube.Centre = Position;
    Cube.Size = ChildSize;
    Cube.Spin = vec4f(HashDirection(Seed ^ (Child * 29u)), 9.0 * ChildTime);
    Cube.Family = select(DerezGlowFamily, DerezGlitchFamily, Effect.Style == 1u && (Child & 1u) == 0u);
    if (Faces || Debris)
    {
        // 📝 The shards keep the colour of what they broke from, a last ember of the glow fading as they shrink.
        Cube.Family = FaceFamily;
        Cube.Glow = select(0.4 * Effect.Colour.y * exp(-ChildTime / 0.4), 0.0, Debris);
    }
    return Cube;
}

// 📝 The 36 corners of a unit cube (six faces of two triangles, counter-clockwise from outside) and their normals.
fn ResolveCubeCorner(Corner: u32, Normal: ptr<function, vec3f>) -> vec3f
{
    let Face = Corner / 6u;
    let Index = Corner % 6u;
    var Square = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
    let Q = Square[Index];
    let Sign = select(-1.0, 1.0, (Face & 1u) == 1u);
    let Axis = Face >> 1u;
    var Point = vec3f(0.0);
    if (Axis == 0u)      { Point = vec3f(Sign, Q.x * Sign, Q.y); *Normal = vec3f(Sign, 0.0, 0.0); }
    else if (Axis == 1u) { Point = vec3f(-Q.x * Sign, Sign, Q.y); *Normal = vec3f(0.0, Sign, 0.0); }
    else                 { Point = vec3f(Q.x, Q.y * Sign, Sign); *Normal = vec3f(0.0, 0.0, Sign); }
    return 0.5 * Point;
}

fn PlaceCorner(Cube: CubeState, Local: vec3f) -> vec3f
{
    return Cube.Centre + RotateAxisAngle(Local * Cube.Size, Cube.Spin.xyz, Cube.Spin.w);
}

@vertex
fn EffectVertex(@builtin(vertex_index) VertexIndex: u32, @builtin(instance_index) InstanceIndex: u32) -> EffectVarying
{
    let EmitterIndex = min(InstanceIndex / 4096u, 7u);
    let Particle = InstanceIndex % 4096u;
    let Emitter = Effect.Emitters[EmitterIndex];
    let Child = VertexIndex / 36u;
    var LocalNormal = vec3f(0.0, 0.0, 1.0);
    let Local = ResolveCubeCorner(VertexIndex % 36u, &LocalNormal);
    let Now = ResolveCube(Emitter, Particle, Child, Effect.Clock);
    let Before = ResolveCube(Emitter, Particle, Child, Effect.Clock - Effect.BodyMaximum.w);
    var Varying: EffectVarying;
    if (Now.Size <= 0.0)
    {
        Varying.Position = vec4f(0.0, 0.0, -2.0, 1.0);   // outside the clip volume: culled
        return Varying;
    }
    let World = PlaceCorner(Now, Local);
    let Previous = select(World, PlaceCorner(Before, Local), Before.Size > 0.0);
    Varying.Position      = Frame.ViewProjection * vec4f(World, 1.0);
    Varying.CurrentClip   = Frame.UnjitteredViewProjection * vec4f(World, 1.0);
    Varying.PreviousClip  = Frame.PreviousUnjitteredViewProjection * vec4f(Previous, 1.0);
    Varying.Normal        = RotateAxisAngle(LocalNormal, Now.Spin.xyz, Now.Spin.w);
    Varying.Family        = Now.Family;
    Varying.WorldPosition = World;
    Varying.Glow          = Now.Glow;
    return Varying;
}

fn ProjectEffectPixel(Clip: vec4f) -> vec2f
{
    let Ndc = Clip.xy / Clip.w;
    return vec2f(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5) * Frame.ScreenExtent;
}

struct EffectTargets
{
    @location(0) Attributes : vec4f,
    @location(1) Motion     : vec2f,
}

@fragment
fn EffectFragment(Varying: EffectVarying) -> EffectTargets
{
    var Normal = normalize(Varying.Normal);
    if (dot(Normal, Frame.CameraPosition - Varying.WorldPosition) < 0.0)
    {
        Normal = -Normal;
    }
    var Targets: EffectTargets;
    Targets.Attributes = vec4f(EncodeOctahedral(Normal), f32(Varying.Family), 1.0 + max(Varying.Glow, 0.0));
    Targets.Motion     = ProjectEffectPixel(Varying.CurrentClip) - ProjectEffectPixel(Varying.PreviousClip);
    return Targets;
}
