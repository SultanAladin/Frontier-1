//============================================================================================================================================
//                                                            PLUMERECORDS.WGSL
//============================================================================================================================================
// 🧩 The smoke-and-fire cell lattices: the step record, the source and fire-light records, and toroidal addressing shared by every plume program.

// 📝 Two cell lattices follow the vehicle, addressed toroidally exactly as the scene distance field's cascades are:
//    world cell c lives at texel c mod N, so the window moves by relabelling texels, never by copying them (Frontier's
//    SmokeSpace.h relocates its whole lattice by a copy whenever the carrier leaves the guard band).
//
//    | Lattice  | Edge | Carries                                                                                   |
//    |----------|------|-------------------------------------------------------------------------------------------|
//    | velocity | 2h   | xyz velocity [m/s], w 1 where the scene distance field is solid                           |
//    | thermo   | h    | r rubber smoke [-], g temperature above ambient [K], b flame [-], a soot [-]               |
//    | light    | 2h   | r transmittance toward the sun, g toward the zenith, through the plume                    |
//
//    Two particulates, because a burnout and a tyre fire are different smokes: vapourised rubber condenses into a
//    bright, highly scattering white-grey cloud (albedo ≈ 0.9), while burning rubber sheds black soot that absorbs
//    almost everything it meets (albedo ≈ 0.1). Both ride the same velocity; the march mixes their optics per sample.
//    XY wrap; Z does not — the window's floor is fixed under the course and its height covers the plume.

struct PlumeRecord
{
    VelocityOrigin      : vec4i,   // [cell] xyz world coarse cell of the window's minimum corner (z is 0), w step ordinal
    PriorVelocityOrigin : vec4i,   // [cell] xyz the window the lattice state was written in, w source count
    VelocityExtent      : vec4u,   // [cell] xyz coarse lattice extent, w unused
    ThermoExtent        : vec4u,   // [cell] xyz fine lattice extent, w inspection (1 shows the cells)
    Lattice             : vec4f,   // x fine edge h [m], y coarse edge 2h [m], z floor height [m], w step duration [s]
    Forces              : vec4f,   // x buoyancy [m/s²/K], y smoke weight [m/s²], z vorticity confinement [-], w curl turbulence [m/s]
    Retention           : vec4f,   // [-] x smoke, y heat, z flame, w velocity — multipliers over this step
    Wind                : vec4f,   // xyz ambient flow [m/s], w elapsed seconds [s]
    SourceMinimum       : vec4f,   // xyz bounds of every source [m], w frame duration for moving-solid velocity [s]
    SourceMaximum       : vec4f,   // xyz [m], w flame emission scale [-]
    Optical             : vec4f,   // x smoke extinction per density [1/m], y smoke albedo [-], z soot extinction per density [1/m], w march samples
    Sun                 : vec4f,   // xyz unit direction toward the sun, w soot albedo [-]
    Combustion          : vec4f,   // x soot shed per flame per second [1/s], y heat released per flame per second [K/s], z soot retention [-], w unused
    Scattering          : vec4f,   // x multiple-scattering strength [-], y phase anisotropy g [-], z ambient scale [-], w fire-light scale [-]
    Detail              : vec4f,   // x sub-cell detail strength [-], y detail frequency [1/m], z turbulence frequency [1/m], w unused
    Flame               : vec4f,   // x core temperature [K], y tip temperature [K], z heat at which a cell reaches the core [K], w unused
}

struct PlumeSource
{
    PositionRadius : vec4f,   // xyz [m] world centre, w [m] Gaussian radius
    Velocity       : vec4f,   // xyz [m/s] injected momentum
    Rates          : vec4f,   // x smoke [1/s], y heat [K/s], z flame [1/s], w soot [1/s]
}

// 📝 Burning tyres and exhaust pops as point lights: the march scatters them inside the smoke and the direct-radiance
//    pass lights the car and the road with them. Radiance is flux per steradian [lx·m²·k], position the flame centre.
struct FireLightRecord
{
    Count          : vec4u,              // x lights in use
    PositionRadius : array<vec4f, 4>,    // xyz [m], w [m] source radius that softens the inverse square
    Radiance       : array<vec4f, 4>,    // rgb intensity, w unused
}

const PlumeSpeedCeiling : f32 = 40.0;   // [m/s] - no cell is allowed to outrun this, whatever the sources say

//------------------------------------------------------------------------------------------------------------------------
//                                                 TOROIDAL ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

fn WrapLattice(Cell: vec3i, Extent: vec3u) -> vec3i
{
    let Edge = vec3i(Extent);
    return vec3i(((Cell.xy % Edge.xy) + Edge.xy) % Edge.xy, Cell.z);
}

// 📝 Texel of a local velocity cell (0 … extent − 1 inside the current window); local may step one cell outside.
fn ResolveVelocityTexel(Plume: PlumeRecord, Local: vec3i) -> vec3i
{
    return WrapLattice(Plume.VelocityOrigin.xyz + Local, Plume.VelocityExtent.xyz);
}

fn ResolveThermoTexel(Plume: PlumeRecord, Local: vec3i) -> vec3i
{
    return WrapLattice(vec3i(2 * Plume.VelocityOrigin.xy, 0) + Local, Plume.ThermoExtent.xyz);
}

fn ProjectVelocityWorld(Plume: PlumeRecord, Local: vec3u) -> vec3f
{
    let Cell = vec3f(Plume.VelocityOrigin.xyz + vec3i(Local)) + 0.5;
    return vec3f(Cell.xy * Plume.Lattice.y, Plume.Lattice.z + Cell.z * Plume.Lattice.y);
}

fn ProjectThermoWorld(Plume: PlumeRecord, Local: vec3u) -> vec3f
{
    let Cell = vec3f(vec3i(2 * Plume.VelocityOrigin.xy, 0) + vec3i(Local)) + 0.5;
    return vec3f(Cell.xy * Plume.Lattice.x, Plume.Lattice.z + Cell.z * Plume.Lattice.x);
}

// 📝 Sampling coordinates: the repeat address mode on U and V performs the wrap; W is clamped.
fn ProjectVelocityCoordinates(Plume: PlumeRecord, Position: vec3f) -> vec3f
{
    let Span = vec3f(Plume.VelocityExtent.xyz) * Plume.Lattice.y;
    return vec3f(Position.xy / Span.xy, (Position.z - Plume.Lattice.z) / Span.z);
}

fn ProjectThermoCoordinates(Plume: PlumeRecord, Position: vec3f) -> vec3f
{
    let Span = vec3f(Plume.ThermoExtent.xyz) * Plume.Lattice.x;
    return vec3f(Position.xy / Span.xy, (Position.z - Plume.Lattice.z) / Span.z);
}

// 📝 A position lies in a window's interior when every trilinear tap it takes is a texel of that window — half a cell
//    in from each wrapped face, so a sample beside one edge can never read the opposite edge through the wrap.
fn ClassifyWindowInterior(Position: vec3f, Origin: vec3i, Extent: vec3u, Edge: f32, Floor: f32) -> bool
{
    let Local = vec3f(Position.xy / Edge - vec2f(Origin.xy), (Position.z - Floor) / Edge);
    return all(Local >= vec3f(0.5)) && all(Local <= vec3f(Extent) - vec3f(0.5));
}

fn ClassifyPriorVelocity(Plume: PlumeRecord, Position: vec3f) -> bool
{
    return ClassifyWindowInterior(Position, Plume.PriorVelocityOrigin.xyz, Plume.VelocityExtent.xyz, Plume.Lattice.y, Plume.Lattice.z);
}

fn ClassifyPriorThermo(Plume: PlumeRecord, Position: vec3f) -> bool
{
    return ClassifyWindowInterior(Position, vec3i(2 * Plume.PriorVelocityOrigin.xy, 0), Plume.ThermoExtent.xyz, Plume.Lattice.x, Plume.Lattice.z);
}

fn ClassifySourceBounds(Plume: PlumeRecord, Position: vec3f) -> bool
{
    return Plume.PriorVelocityOrigin.w > 0 && all(Position >= Plume.SourceMinimum.xyz) && all(Position <= Plume.SourceMaximum.xyz);
}

// 📐 Gaussian source weight exp(−1.7·d²/r²), r floored at three quarters of a cell so a source narrower than the
//    lattice still deposits; zero beyond 3.5 radii (Frontier's SmokeTransport.slang injection). The centre is lifted
//    0.55 of a cell, as Frontier's is: a contact patch lies on the supporting plane, and a Gaussian centred there
//    would spend half its weight in cells the plane makes solid.
fn ResolveSourceWeight(Source: PlumeSource, Position: vec3f, Edge: f32) -> f32
{
    let Radius = max(Source.PositionRadius.w, 0.75 * Edge);
    let Offset = (Position - Source.PositionRadius.xyz - vec3f(0.0, 0.0, 0.55 * Edge)) / Radius;
    let Squared = dot(Offset, Offset);
    return select(exp(-1.7 * Squared), 0.0, Squared > 12.25);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FLAME COLOUR
//------------------------------------------------------------------------------------------------------------------------

// 📐 The colour of a blackbody at Temperature [K], linear Rec.709, normalised to unit maximum channel: Helland's fit of
//    the Planckian locus (sRGB-encoded) decoded to linear. Valid 1000–6600 K; a tyre fire's soot glows 1100–1500 K at
//    the tips and approaches 1800 K in the core.
fn ResolveBlackbodyColour(Temperature: f32) -> vec3f
{
    let Hecto = clamp(Temperature, 1000.0, 6600.0) / 100.0;
    let Green = clamp((99.4708025861 * log(Hecto) - 161.1195681661) / 255.0, 0.0, 1.0);
    let Blue = select(clamp((138.5177312231 * log(max(Hecto - 10.0, 1.0)) - 305.0447927307) / 255.0, 0.0, 1.0), 0.0, Hecto <= 19.0);
    return pow(vec3f(1.0, Green, Blue), vec3f(2.2));
}

// 📐 Stefan–Boltzmann in relative form: radiance rises with the fourth power of temperature, referred to 1500 K.
fn ResolveBlackbodyRadiance(Temperature: f32) -> vec3f
{
    let Ratio = Temperature / 1500.0;
    return ResolveBlackbodyColour(Temperature) * (Ratio * Ratio * Ratio * Ratio);
}
