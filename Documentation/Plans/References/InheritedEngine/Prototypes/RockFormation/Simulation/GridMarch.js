//========================================================================================================================
//                                                   GridMarch.js                                                  🧩
//========================================================================================================================
//
// 📝 Fixed-step raymarch of the voxel density grid, replacing the analytic sphere trace.
//
//    🔴 A DENSITY GRID IS NOT A DISTANCE FIELD. This is the single most important fact in the file. The
//       sphere trace stepped by the field value because that value was a distance — a guaranteed-empty
//       radius. Density carries no such guarantee: a cell reading 0.0 says "this cell is empty", NOT
//       "space is empty for 0 metres around". Stepping by density would take a full-length step through
//       any gap and tunnel clean through the far wall of an arch.
//
//       The march is therefore FIXED-STEP, at a fraction of a cell. That costs more steps than sphere
//       tracing did, and the budget is recovered by the empty-space skip and the tight domain bound.
//
//    ⚠️ Nyquist: the step must be at most one cell, or thin features between samples are invisible. At
//       0.7 of a cell a one-cell-thick ligament is sampled at least once. Larger reads as sparkle and
//       holes that come and go as the camera moves — a symptom easily misread as a broken sim.

import { FieldProfileDeclaration } from "./VoxelField.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                    BINDINGS
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 Group 0 is the render's DialProfile (camera, sun, resolve mode) — unchanged from M1. Group 1 is the
//    voxel field. Splitting them is deliberate: group 0 is rewritten every frame from the camera, group 1
//    changes only on a parity flip, so they have different update rates and belong in different groups.

const MarchBindings = `
@group(1) @binding(0) var<uniform>       Field       : FieldProfile;
@group(1) @binding(1) var<storage, read> Density     : array<f32>;
@group(1) @binding(2) var<storage, read> Resistance  : array<f32>;
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SAMPLING
//------------------------------------------------------------------------------------------------------------------------

const GridSampling = `
fn GridOrdinal(Cell : vec3i, Edge : i32) -> i32
{
    return (Cell.z * Edge + Cell.y) * Edge + Cell.x;
}

fn SampleCell(Cell : vec3i, Edge : i32) -> f32
{
    if (any(Cell < vec3i(0)) || any(Cell >= vec3i(Edge))) { return 0.0; }
    return Density[GridOrdinal(Cell, Edge)];
}

fn SampleResistanceCell(Cell : vec3i, Edge : i32) -> f32
{
    if (any(Cell < vec3i(0)) || any(Cell >= vec3i(Edge))) { return 1.0; }
    return Resistance[GridOrdinal(Cell, Edge)];
}

// 📝 World position -> continuous grid coordinate, where integer values land on CELL CENTRES.
//    ⚠️ The -0.5 is the inverse of the +0.5 in CellToPosition. Dropping it here while keeping it there
//       offsets the render by half a cell against the sim, which reads as the lighting not quite matching
//       the silhouette — subtle enough to be chased in the shading for a long time.
fn PositionToGrid(Probe : vec3f) -> vec3f
{
    let Middle = (Field.Edge - 1.0) * 0.5;
    return Probe / Field.CellSize + vec3f(Middle);
}

// 📝 Trilinear interpolation. 🔴 Nearest-neighbour would render visible cubic voxels — the "Minecraft
//    rock" failure — and would make the gradient normal piecewise constant, so the whole surface would
//    shade in flat facets. Eight taps is the price of a continuous surface.
fn SampleDensity(Probe : vec3f) -> f32
{
    let Edge = i32(Field.Edge);
    let Grid = PositionToGrid(Probe);
    let Base = vec3i(floor(Grid));
    let Frac = fract(Grid);

    let C000 = SampleCell(Base + vec3i(0, 0, 0), Edge);
    let C100 = SampleCell(Base + vec3i(1, 0, 0), Edge);
    let C010 = SampleCell(Base + vec3i(0, 1, 0), Edge);
    let C110 = SampleCell(Base + vec3i(1, 1, 0), Edge);
    let C001 = SampleCell(Base + vec3i(0, 0, 1), Edge);
    let C101 = SampleCell(Base + vec3i(1, 0, 1), Edge);
    let C011 = SampleCell(Base + vec3i(0, 1, 1), Edge);
    let C111 = SampleCell(Base + vec3i(1, 1, 1), Edge);

    let X00 = mix(C000, C100, Frac.x);
    let X10 = mix(C010, C110, Frac.x);
    let X01 = mix(C001, C101, Frac.x);
    let X11 = mix(C011, C111, Frac.x);

    return mix(mix(X00, X10, Frac.y), mix(X01, X11, Frac.y), Frac.z);
}

fn SampleResistance(Probe : vec3f) -> f32
{
    let Edge = i32(Field.Edge);
    let Grid = PositionToGrid(Probe);
    let Base = vec3i(floor(Grid));
    let Frac = fract(Grid);

    let X00 = mix(SampleResistanceCell(Base + vec3i(0,0,0), Edge), SampleResistanceCell(Base + vec3i(1,0,0), Edge), Frac.x);
    let X10 = mix(SampleResistanceCell(Base + vec3i(0,1,0), Edge), SampleResistanceCell(Base + vec3i(1,1,0), Edge), Frac.x);
    let X01 = mix(SampleResistanceCell(Base + vec3i(0,0,1), Edge), SampleResistanceCell(Base + vec3i(1,0,1), Edge), Frac.x);
    let X11 = mix(SampleResistanceCell(Base + vec3i(0,1,1), Edge), SampleResistanceCell(Base + vec3i(1,1,1), Edge), Frac.x);

    return mix(mix(X00, X10, Frac.y), mix(X01, X11, Frac.y), Frac.z);
}

// 📝 Gradient of the interpolated density, by central difference at one cell spacing. Points INTO the
//    rock (density rises inward), so the outward normal is its negation.
//
//    ⚠️ Sample at one CELL, not at some small epsilon. Below a cell the trilinear interpolant is exactly
//       linear, so a sub-cell difference returns the same value scaled — no extra detail, just a smaller
//       number that amplifies f32 cancellation.
fn EvaluateGridNormal(Probe : vec3f) -> vec3f
{
    let Delta = Field.CellSize;
    let Dx = vec3f(Delta, 0.0, 0.0);
    let Dy = vec3f(0.0, Delta, 0.0);
    let Dz = vec3f(0.0, 0.0, Delta);

    let Gradient = vec3f(
        SampleDensity(Probe + Dx) - SampleDensity(Probe - Dx),
        SampleDensity(Probe + Dy) - SampleDensity(Probe - Dy),
        SampleDensity(Probe + Dz) - SampleDensity(Probe - Dz));

    let Span = length(Gradient);
    if (Span < 1e-6) { return vec3f(0.0, 1.0, 0.0); }
    return -Gradient / Span;
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                     MARCH
//------------------------------------------------------------------------------------------------------------------------

const GridTrace = `
struct GridOutcome
{
    Range     : f32,
    StepTally : f32,
    Contact   : bool
};

// 📝 Slab test against the grid's axis-aligned bounds. This replaces the bounding SPHERE of the analytic
//    march because the domain genuinely is a box; a sphere around it would admit corner rays that can
//    never hit anything and make them march the full budget.
fn IntersectDomain(Origin : vec3f, Bearing : vec3f) -> vec2f
{
    let Extent   = vec3f(Field.DomainRadius);
    let Inverse  = 1.0 / Bearing;                                   // 📝 inf on an axis-parallel ray is fine: min/max sort it out
    let NearPlan = (-Extent - Origin) * Inverse;
    let FarPlan  = ( Extent - Origin) * Inverse;

    let Lower = min(NearPlan, FarPlan);
    let Upper = max(NearPlan, FarPlan);

    let Entry = max(max(Lower.x, Lower.y), Lower.z);
    let Leave = min(min(Upper.x, Upper.y), Upper.z);
    return vec2f(max(Entry, 0.0), Leave);
}

// 🔴 THE SURFACE IS AN ISOSURFACE, not a zero crossing. Density runs 0..1, so "solid" is a THRESHOLD.
//    0.5 is the half-occupied level, which is where a linear ramp between an empty and a full cell puts
//    the geometric surface. Changing it inflates or deflates the whole rock uniformly.
const SolidLevel : f32 = 0.5;

fn TraceDensityGrid(Origin : vec3f, Bearing : vec3f) -> GridOutcome
{
    var Outcome = GridOutcome(0.0, 0.0, false);

    let Span = IntersectDomain(Origin, Bearing);
    if (Span.y <= Span.x) { return Outcome; }                        // ① ray misses the grid entirely

    // ② Fixed step at a fraction of a cell -- see the Nyquist note in the header.
    let Stride  = Field.CellSize * clamp(U.StepScale, 0.15, 1.0);
    let Ceiling = i32(U.StepCeiling);

    var Range = Span.x + Stride * 0.5;
    var Prior = 0.0;                                                 // 📝 density at the previous sample

    for (var Step : i32 = 0; Step < Ceiling; Step = Step + 1)
    {
        if (Range > Span.y || Range > U.FarDistance) { break; }

        let Here = SampleDensity(Origin + Bearing * Range);
        Outcome.StepTally = f32(Step);

        if (Here >= SolidLevel)
        {
            // ③ Refine by linear interpolation between the two bracketing samples. Snapping to the
            //    sample point would quantise the surface to the step length and stair-step every face
            //    at grazing angles; one lerp removes that for the cost of nothing.
            let Reach = select(0.0, (SolidLevel - Prior) / max(Here - Prior, 1e-5), Here > Prior);
            Outcome.Range   = Range - Stride * (1.0 - clamp(Reach, 0.0, 1.0));
            Outcome.Contact = true;
            return Outcome;
        }

        Prior = Here;
        Range = Range + Stride;
    }
    return Outcome;
}

// 📝 Shadow march. Coarser than the primary — a shadow edge half a cell out is invisible, and this runs
//    once per lit pixel on top of the primary march.
fn TraceGridShadow(Origin : vec3f, Bearing : vec3f) -> f32
{
    let Span   = IntersectDomain(Origin, Bearing);
    let Stride = Field.CellSize * 1.4;

    var Range = Field.CellSize * 2.0;                                // 🔴 offset off the surface, or every point shadows itself
    for (var Step : i32 = 0; Step < 48; Step = Step + 1)
    {
        if (Range > Span.y) { break; }
        if (SampleDensity(Origin + Bearing * Range) >= SolidLevel) { return 0.0; }
        Range = Range + Stride;
    }
    return 1.0;
}

// 📝 Ambient occlusion by DIRECT OCCUPANCY sampling along the normal. The analytic version compared the
//    distance field against travel; there is no distance field here, so this integrates how much rock
//    sits in front of the surface instead — simpler and closer to what occlusion actually means.
fn EvaluateGridOcclusion(Probe : vec3f, Normal : vec3f) -> f32
{
    var Occlusion = 0.0;
    var Weight    = 1.0;
    for (var Step : i32 = 1; Step <= 5; Step = Step + 1)
    {
        let Offset = Field.CellSize * f32(Step) * 1.7;
        Occlusion  = Occlusion + SampleDensity(Probe + Normal * Offset) * Weight;
        Weight     = Weight * 0.68;
    }
    return clamp(1.0 - Occlusion * 0.62, 0.0, 1.0);
}

// 📝 Curvature from the density Laplacian, at one cell spacing. Concave reads positive here (rock curls
//    around the point), which is the cavity term the shading darkens.
fn EvaluateGridCurvature(Probe : vec3f) -> f32
{
    let Delta  = Field.CellSize * 1.5;
    let Centre = SampleDensity(Probe);
    var Sum    = 0.0;
    Sum = Sum + SampleDensity(Probe + vec3f( Delta, 0.0, 0.0));
    Sum = Sum + SampleDensity(Probe + vec3f(-Delta, 0.0, 0.0));
    Sum = Sum + SampleDensity(Probe + vec3f(0.0,  Delta, 0.0));
    Sum = Sum + SampleDensity(Probe + vec3f(0.0, -Delta, 0.0));
    Sum = Sum + SampleDensity(Probe + vec3f(0.0, 0.0,  Delta));
    Sum = Sum + SampleDensity(Probe + vec3f(0.0, 0.0, -Delta));
    return Sum / 6.0 - Centre;
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHADING
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 Carried over from the analytic resolve with the field calls swapped for grid samples. The lighting
//    model is unchanged and deliberately so: it was never the problem — the SHAPE was.

const GridShading = `
fn ShadeGridContact(Probe : vec3f, Normal : vec3f, Tint : vec3f) -> vec3f
{
    let Solar = normalize(U.SolarBearing);

    // ① Direct sun, shadowed.
    let Incidence = clamp(dot(Normal, Solar), 0.0, 1.0);
    var Shadow    = 1.0;
    if (U.ShadowWeight > 0.01 && Incidence > 0.001)
    {
        Shadow = mix(1.0, TraceGridShadow(Probe, Solar), U.ShadowWeight);
    }
    var Radiance = Tint * vec3f(1.0, 0.93, 0.82) * Incidence * Shadow * 2.35;

    // ② Sky dome above, cool. ③ Ground bounce below, warm.
    Radiance = Radiance + Tint * vec3f(0.34, 0.44, 0.62) * clamp(0.5 + 0.5 * Normal.y, 0.0, 1.0) * U.AmbientWeight;
    Radiance = Radiance + Tint * vec3f(0.40, 0.26, 0.17) * clamp(0.5 - 0.5 * Normal.y, 0.0, 1.0) * U.AmbientWeight * 0.62;

    // ④ Occlusion and cavity darkening.
    Radiance = Radiance * mix(1.0, EvaluateGridOcclusion(Probe, Normal), 0.85);
    let Cavity = clamp(EvaluateGridCurvature(Probe) * 2.4, 0.0, 1.0);
    Radiance = Radiance * mix(1.0, 1.0 - Cavity, U.CavityWeight);

    // ⑤ Pale dust on upward ledges.
    Radiance = mix(Radiance, Radiance * vec3f(1.16, 1.10, 0.99), clamp(Normal.y, 0.0, 1.0) * U.SlopeWeight * 0.55);
    return Radiance;
}

fn ResolveTonemap(Radiance : vec3f) -> vec3f
{
    let Exposed = Radiance * U.Exposure;
    let Shaped  = (Exposed * (2.51 * Exposed + vec3f(0.03)))
                / (Exposed * (2.43 * Exposed + vec3f(0.59)) + vec3f(0.14));
    return pow(clamp(Shaped, vec3f(0.0), vec3f(1.0)), vec3f(1.0 / 2.2));
}

fn ResolveSkyBackdrop(Bearing : vec3f) -> vec3f
{
    let Elevation = clamp(Bearing.y * 0.5 + 0.5, 0.0, 1.0);
    var Backdrop  = mix(vec3f(0.30, 0.33, 0.38), vec3f(0.055, 0.075, 0.115), pow(Elevation, 0.62));
    let Glow      = pow(clamp(dot(Bearing, normalize(U.SolarBearing)), 0.0, 1.0), 128.0);
    return Backdrop + vec3f(0.85, 0.72, 0.52) * Glow * 0.55;
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                  ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

const GridEntryPoints = `
struct QuadYield
{
    @builtin(position) Coordinate : vec4f,
    @location(0)       Screen     : vec2f
};

@vertex
fn TranscribeQuadVertex(@builtin(vertex_index) VertexIndex : u32) -> QuadYield
{
    var Corners = array<vec2f, 6>(
        vec2f(-1.0, -1.0), vec2f( 1.0, -1.0), vec2f(-1.0,  1.0),
        vec2f(-1.0,  1.0), vec2f( 1.0, -1.0), vec2f( 1.0,  1.0));

    let Corner = Corners[VertexIndex];
    var Result : QuadYield;
    Result.Coordinate = vec4f(Corner, 0.0, 1.0);
    Result.Screen     = Corner;
    return Result;
}

@fragment
fn InscribeSurfaceFragment(Stream : QuadYield) -> @location(0) vec4f
{
    let Aspect  = U.Viewport.x / max(U.Viewport.y, 1.0);
    let Plane   = vec2f(Stream.Screen.x * Aspect, Stream.Screen.y);
    let Bearing = normalize(U.Forward + U.Rightward * Plane.x + U.Upward * Plane.y);

    let Outcome = TraceDensityGrid(U.Origin, Bearing);

    // ① Step-count resolve, for seeing where the march is expensive.
    if (U.ResolveMode > 2.5)
    {
        let Load = Outcome.StepTally / max(U.StepCeiling, 1.0);
        return vec4f(Load, 1.0 - Load, 0.14, 1.0);
    }

    if (!Outcome.Contact) { return vec4f(ResolveSkyBackdrop(Bearing), 1.0); }

    let Probe  = U.Origin + Bearing * Outcome.Range;
    let Normal = EvaluateGridNormal(Probe);

    // ② Normal resolve.
    if (U.ResolveMode > 1.5) { return vec4f(Normal * 0.5 + vec3f(0.5), 1.0); }

    // ③ Resistance resolve. 🔴 This one matters more than it did in M1: resistance is now the SOLE
    //    explanation for why the rock has the shape it has, so being able to look at it directly is how
    //    a strange result gets diagnosed.
    if (U.ResolveMode > 0.5)
    {
        let Hardness = clamp(SampleResistance(Probe), 0.0, 1.0);
        let Ramp = mix(vec3f(0.72, 0.22, 0.16), vec3f(0.86, 0.84, 0.76), Hardness);
        return vec4f(pow(Ramp * mix(0.45, 1.0, EvaluateGridOcclusion(Probe, Normal)), vec3f(1.0 / 2.2)), 1.0);
    }

    let Tint = EvaluateConstructionTint(Probe);

    // ④ Preview resolve — matcap while dragging. No shadow, no occlusion, no curvature.
    if (U.PreviewMode > 0.5)
    {
        let Solar     = normalize(U.SolarBearing);
        let Incidence = clamp(dot(Normal, Solar), 0.0, 1.0);
        let Wrapped   = clamp(0.5 + 0.5 * dot(Normal, Solar), 0.0, 1.0);
        var Preview   = Tint * (0.34 + 1.55 * Incidence * 0.72 + 0.30 * Wrapped);
        Preview = Preview + Tint * vec3f(0.10, 0.13, 0.19) * clamp(0.5 + 0.5 * Normal.y, 0.0, 1.0);
        return vec4f(ResolveTonemap(Preview), 1.0);
    }

    return vec4f(ResolveTonemap(ShadeGridContact(Probe, Normal, Tint)), 1.0);
}
`;

// 📝 The resolve half, assembled. Spliced after the preamble, the DialProfile declaration and the
//    transcribed tint expression — exactly where SphereTraceResolve used to sit, so DeviceHost's
//    assembly order is unchanged.
export const GridMarchResolve = [
    FieldProfileDeclaration,
    MarchBindings,
    GridSampling,
    GridTrace,
    GridShading,
    GridEntryPoints
].join("\n");
