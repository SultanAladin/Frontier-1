//========================================================================================================================
//                                           DistanceExpression.js                🧩
//========================================================================================================================


//------------------------------------------------------------------------------------------------------------------------
//                                              DISTANCE EXPRESSION LIBRARY
//------------------------------------------------------------------------------------------------------------------------

// 📝 The WGSL preamble. Every entry's Transcribe() emits a call into this library, so the library is
//    fixed and only the straight-line evaluation body is regenerated on a topology edit.
//
//    🔴 Distance-bound discipline, since this is the thing most often gotten wrong:
//      - Union min(a,b) is EXACT for true distance fields.
//      - Subtraction max(a,-b) and intersection max(a,b) are LOWER BOUNDS only, and only OUTSIDE the
//        surface. Inside, they are meaningless.
//      - Smooth-min, non-uniform scale, and additive displacement all BREAK the Euclidean metric.
//      - Adding a displacement of amplitude A to the field raises the Lipschitz constant, so the
//        sphere trace must shorten each step. The march multiplies by StepScale for exactly this.
//    📝 The hull body lives in HullExpression.js and is appended at the bottom of this file, so every
//       consumer of the preamble gets it without threading a second string through six call sites. It is a
//       separate FILE because it is a separate provenance — a port of a specific published addon, with its
//       own verification notes — not because it is a separate compilation unit.
import { HullExpressionSource } from "./HullExpression.js";

const DistanceExpressionBody = /* wgsl */`

const Tau : f32 = 6.28318530718;

struct ViewProfile
{
    Origin        : vec3f,
    StepScale     : f32,
    Forward       : vec3f,
    StepCeiling   : f32,
    Rightward     : vec3f,
    HitTolerance  : f32,
    Upward        : vec3f,
    FarDistance   : f32,
    SolarBearing  : vec3f,
    AmbientWeight : f32,
    Viewport      : vec2f,
    CavityWeight  : f32,
    SlopeWeight   : f32,
    ResolveMode   : f32,
    ShadowWeight  : f32,
    Exposure      : f32,
    SceneRadius   : f32,
    PreviewMode   : f32
};

//------------------------------------------------------------- noise basis

fn HashToUnit(Lattice : vec3f) -> f32
{
    let Folded = fract(Lattice * 0.3183099 + vec3f(0.1, 0.2, 0.3));
    let Mixed  = Folded * 17.0;
    return fract(Mixed.x * Mixed.y * Mixed.z * (Mixed.x + Mixed.y + Mixed.z));
}

fn HashToVector(Lattice : vec3f) -> vec3f
{
    return vec3f(HashToUnit(Lattice),
                 HashToUnit(Lattice + vec3f(37.1, 11.7, 5.3)),
                 HashToUnit(Lattice + vec3f(19.3, 71.9, 23.1)));
}

// 📝 Value noise with a quintic fade. Quintic is C2, so derived normals stay smooth where a cubic
//    fade would show faceting along the lattice planes.
fn EvaluateValueNoise(Sample : vec3f) -> f32
{
    let Cell     = floor(Sample);
    let Fraction = Sample - Cell;
    let Fade     = Fraction * Fraction * Fraction * (Fraction * (Fraction * 6.0 - 15.0) + 10.0);

    let C000 = HashToUnit(Cell + vec3f(0.0, 0.0, 0.0));
    let C100 = HashToUnit(Cell + vec3f(1.0, 0.0, 0.0));
    let C010 = HashToUnit(Cell + vec3f(0.0, 1.0, 0.0));
    let C110 = HashToUnit(Cell + vec3f(1.0, 1.0, 0.0));
    let C001 = HashToUnit(Cell + vec3f(0.0, 0.0, 1.0));
    let C101 = HashToUnit(Cell + vec3f(1.0, 0.0, 1.0));
    let C011 = HashToUnit(Cell + vec3f(0.0, 1.0, 1.0));
    let C111 = HashToUnit(Cell + vec3f(1.0, 1.0, 1.0));

    let X00 = mix(C000, C100, Fade.x);
    let X10 = mix(C010, C110, Fade.x);
    let X01 = mix(C001, C101, Fade.x);
    let X11 = mix(C011, C111, Fade.x);
    return mix(mix(X00, X10, Fade.y), mix(X01, X11, Fade.y), Fade.z) * 2.0 - 1.0;
}

fn EvaluateFractalNoise(Sample : vec3f, OctaveCount : i32, Lacunarity : f32) -> f32
{
    var Accumulation = 0.0;
    var Weight       = 0.5;
    var Scaled       = Sample;
    for (var Octave : i32 = 0; Octave < OctaveCount; Octave = Octave + 1)
    {
        Accumulation = Accumulation + Weight * EvaluateValueNoise(Scaled);
        Scaled       = Scaled * Lacunarity;
        Weight       = Weight * 0.5;
    }
    return Accumulation;
}

// 📝 Ridged multifractal. The "ridged" part is the transform r = (1 - |n|)^2 — squaring after the fold
//    sharpens the crease. Each octave is weighted by the previous, which is what makes it
//    multiplicative (multifractal) rather than a plain sum.
fn EvaluateRidgedNoise(Sample : vec3f, OctaveCount : i32, Lacunarity : f32) -> f32
{
    var Accumulation = 0.0;
    var Weight       = 0.5;
    var Carry        = 1.0;
    var Scaled       = Sample;
    for (var Octave : i32 = 0; Octave < OctaveCount; Octave = Octave + 1)
    {
        var Folded   = 1.0 - abs(EvaluateValueNoise(Scaled));
        Folded       = Folded * Folded;
        Accumulation = Accumulation + Weight * Folded * Carry;
        Carry        = clamp(Folded, 0.0, 1.0);
        Scaled       = Scaled * Lacunarity;
        Weight       = Weight * 0.5;
    }
    return Accumulation * 2.0 - 1.0;
}

// 📝 Voronoi CELL-BOUNDARY distance, the key to angular rock. Smooth noise gives clay; the distance
//    to the bisector between the two nearest seeds gives flat faces meeting at sharp arrises, which
//    is how joint-bounded blocks actually read.
fn EvaluateJointCellDistance(Sample : vec3f, Jitter : f32) -> f32
{
    let Cell     = floor(Sample);
    let Fraction = Sample - Cell;

    var NearestOffset = vec3f(0.0);
    var NearestRange  = 1e9;

    // ① First sweep: locate the nearest seed.
    for (var Zi : i32 = -1; Zi <= 1; Zi = Zi + 1) {
    for (var Yi : i32 = -1; Yi <= 1; Yi = Yi + 1) {
    for (var Xi : i32 = -1; Xi <= 1; Xi = Xi + 1) {
        let Neighbour = vec3f(f32(Xi), f32(Yi), f32(Zi));
        let Seed      = Neighbour + HashToVector(Cell + Neighbour) * Jitter - Fraction;
        let Range     = dot(Seed, Seed);
        if (Range < NearestRange) { NearestRange = Range; NearestOffset = Seed; }
    }}}

    // ② Second sweep: distance to the bisecting plane between the nearest seed and each other seed.
    var BoundaryRange = 1e9;
    for (var Zi : i32 = -2; Zi <= 2; Zi = Zi + 1) {
    for (var Yi : i32 = -2; Yi <= 2; Yi = Yi + 1) {
    for (var Xi : i32 = -2; Xi <= 2; Xi = Xi + 1) {
        let Neighbour  = vec3f(f32(Xi), f32(Yi), f32(Zi));
        let Seed       = Neighbour + HashToVector(Cell + Neighbour) * Jitter - Fraction;
        let Separation = Seed - NearestOffset;
        let Spread     = dot(Separation, Separation);
        if (Spread > 1e-5)
        {
            let Reach     = dot(0.5 * (NearestOffset + Seed), normalize(Separation));
            BoundaryRange = min(BoundaryRange, Reach);
        }
    }}}
    return BoundaryRange;
}

//------------------------------------------------------------- primitive distances

fn DistanceToSphere(Probe : vec3f, Radius : f32) -> f32
{
    return length(Probe) - Radius;
}

// 📝 Rounded box (IQ). Exact outside; the min(max(...)) term keeps it usable at interior points too.
fn DistanceToRoundedBox(Probe : vec3f, HalfExtent : vec3f, Arris : f32) -> f32
{
    let Outward = abs(Probe) - HalfExtent + vec3f(Arris);
    return length(max(Outward, vec3f(0.0))) + min(max(Outward.x, max(Outward.y, Outward.z)), 0.0) - Arris;
}

// 📝 Exact capsule — this is the arch carver. A capsule swept horizontally and subtracted from a mass
//    is precisely how a sea arch or a fin window reads, and unlike an ellipsoid it is a TRUE distance
//    (sdEllipsoid is a lower bound only, which is why no ellipsoid appears in this library).
fn DistanceToCapsule(Probe : vec3f, Head : vec3f, Tail : vec3f, Radius : f32) -> f32
{
    let Along  = Probe - Head;
    let Axis   = Tail - Head;
    let Travel = clamp(dot(Along, Axis) / dot(Axis, Axis), 0.0, 1.0);
    return length(Along - Axis * Travel) - Radius;
}

//------------------------------------------------------------- combination

// 📝 Polynomial smooth minimum (IQ). Reduces to exact min() as Softness goes to zero. The result is a
//    BOUND, not a distance — hence the step clamp in the march.
fn SmoothMinimum(Left : f32, Right : f32, Softness : f32) -> f32
{
    if (Softness <= 1e-5) { return min(Left, Right); }
    let Blend = clamp(0.5 + 0.5 * (Right - Left) / Softness, 0.0, 1.0);
    return mix(Right, Left, Blend) - Softness * Blend * (1.0 - Blend);
}

fn SmoothMaximum(Left : f32, Right : f32, Softness : f32) -> f32
{
    if (Softness <= 1e-5) { return max(Left, Right); }
    let Blend = clamp(0.5 - 0.5 * (Right - Left) / Softness, 0.0, 1.0);
    return mix(Right, Left, Blend) + Softness * Blend * (1.0 - Blend);
}

fn SmoothSubtraction(Mass : f32, Carver : f32, Softness : f32) -> f32
{
    return SmoothMaximum(Mass, -Carver, Softness);
}

//------------------------------------------------------------- resistance field

// 📝 Paris 2019 GeoStrata, adapted: Wyvill cubic falloff (1 - d^2/r^2)^3 on the SQUARED vertical
//    distance to a bed plane. Bed thicknesses default from measured NPS values (Claron pink member
//    122-213 m, Slick Rock 61-107 m), rescaled to this prototype's metre-scale scene.
//    Dials: x = Thickness, y = Dip, z = Contrast, w = Offset
fn EvaluateStratumBand(Probe : vec3f, Beneath : f32, Dials : vec4f) -> f32
{
    let BedThickness = max(Dials.x, 0.02);
    let DipPlane     = Probe.y + Probe.x * Dials.y + Probe.z * Dials.y * 0.6 + Dials.w;

    // ① Which bed are we in, and where within it?
    let BedIndex  = floor(DipPlane / BedThickness);
    let WithinBed = DipPlane / BedThickness - BedIndex;

    // ② Alternate hard and soft beds, with a per-bed hash so the sequence is not a strict alternation.
    let BedHash   = HashToUnit(vec3f(BedIndex, 0.0, 0.0));
    let BedIsHard = step(0.5, fract(BedIndex * 0.5 + BedHash * 0.34));

    // ③ Wyvill cubic falloff toward the bed centre, so the transition is C1 rather than a step.
    let Centred = (WithinBed - 0.5) * 2.0;
    let Falloff = 1.0 - Centred * Centred;
    let Shaped  = Falloff * Falloff * Falloff;

    let BedStrength = mix(1.0 - Dials.z, 1.0, BedIsHard * 0.5 + Shaped * 0.5);
    return clamp(mix(Beneath, BedStrength, 0.82), 0.0, 1.0);
}

// 📝 Joint network. Spacing defaults to the measured spacing-to-bed-thickness ratio S/T ~= 1.0
//    (Ji 2022, 16 sandstone localities, median 0.99; Bai & Pollard 2000 critical value 0.976).
//    Two joint sets crossed at Bearing, matching the Arches field geometry where sets meet at 35 deg.
//    Dials: x = Spacing, y = Aperture, z = Bearing, w = Weakening
fn EvaluateJointNetwork(Probe : vec3f, Beneath : f32, Dials : vec4f) -> f32
{
    let Spacing = max(Dials.x, 0.05);
    let Cosine  = cos(Dials.z);
    let Sine    = sin(Dials.z);
    let Turned  = vec3f(Probe.x * Cosine - Probe.z * Sine,
                        Probe.y,
                        Probe.x * Sine   + Probe.z * Cosine);

    let CellRange = EvaluateJointCellDistance(Turned / Spacing, 0.85) * Spacing;
    let Fracture  = 1.0 - smoothstep(0.0, max(Dials.y, 1e-3), CellRange);
    return clamp(Beneath - Fracture * Dials.w, 0.0, 1.0);
}

//------------------------------------------------------------- warp

// 📝 Domain warp (IQ). Amplitude is the Lipschitz hazard: displacing the domain by A raises the
//    gradient bound, so the host reports the resulting bound and scales the march step rather than
//    leaving it to chance.
//    Dials: x = Amplitude, y = Frequency, z = Octaves, w = Twist
fn ApplyDomainWarp(Probe : vec3f, Dials : vec4f) -> vec3f
{
    let OctaveCount  = max(i32(Dials.z), 1);
    let Displacement = vec3f(
        EvaluateFractalNoise(Probe * Dials.y + vec3f(0.0, 0.0, 0.0), OctaveCount, 2.0),
        EvaluateFractalNoise(Probe * Dials.y + vec3f(5.2, 1.3, 7.1), OctaveCount, 2.0),
        EvaluateFractalNoise(Probe * Dials.y + vec3f(2.7, 8.3, 3.9), OctaveCount, 2.0));

    var Warped = Probe + Displacement * Dials.x;

    // ② Twist about the vertical, which reads as the helical fluting in a slot canyon wall.
    if (Dials.w > 1e-4)
    {
        let Angle  = Probe.y * Dials.w;
        let Cosine = cos(Angle);
        let Sine   = sin(Angle);
        Warped = vec3f(Warped.x * Cosine - Warped.z * Sine,
                       Warped.y,
                       Warped.x * Sine   + Warped.z * Cosine);
    }
    return Warped;
}

// Dials: x = Amplitude, y = Frequency, z = Octaves, w = Lacunarity
fn ApplyRidgedRelief(Probe : vec3f, Dials : vec4f) -> vec3f
{
    let OctaveCount = max(i32(Dials.z), 1);
    let Relief      = EvaluateRidgedNoise(Probe * Dials.y, OctaveCount, max(Dials.w, 1.2));
    return Probe + vec3f(0.0, Relief * Dials.x, 0.0);
}

// 📝 Limited domain repetition (IQ). An unbounded modulo would tile to infinity and destroy the
//    bounding-sphere early-out, so the lattice index is clamped to a finite extent.
//    Dials: x = Spacing, y = Extent, z = Jitter, w = Axis
fn ApplyLateralRepeat(Probe : vec3f, Dials : vec4f) -> vec3f
{
    let Spacing = max(Dials.x, 0.1);
    let Extent  = floor(Dials.y);
    var Folded  = Probe;

    if (Dials.w < 0.5)
    {
        let Lattice = clamp(round(Probe.x / Spacing), -Extent, Extent);
        Folded.x    = Probe.x - Spacing * Lattice;
        Folded.z    = Probe.z + (HashToUnit(vec3f(Lattice, 3.0, 7.0)) - 0.5) * Spacing * Dials.z;
    }
    else if (Dials.w < 1.5)
    {
        let Lattice = clamp(round(Probe.z / Spacing), -Extent, Extent);
        Folded.z    = Probe.z - Spacing * Lattice;
        Folded.x    = Probe.x + (HashToUnit(vec3f(Lattice, 5.0, 2.0)) - 0.5) * Spacing * Dials.z;
    }
    else
    {
        let LatticeX = clamp(round(Probe.x / Spacing), -Extent, Extent);
        let LatticeZ = clamp(round(Probe.z / Spacing), -Extent, Extent);
        Folded.x = Probe.x - Spacing * LatticeX;
        Folded.z = Probe.z - Spacing * LatticeZ;
        Folded.y = Probe.y + (HashToUnit(vec3f(LatticeX, 1.0, LatticeZ)) - 0.5) * Dials.z;
    }
    return Folded;
}

//------------------------------------------------------------- carvers

// 📝 A swept horizontal capsule — the arch carver, subtracted from a block.
//
//    🔴 This is the ONLY compound shape left. M1 had five (boulder, mesa, hoodoo, fin, aperture), each an
//       analytic guess at a weathered landform, and they were the design error: a landform is the OUTPUT
//       of erosion, so building it as a primitive means the erosion never happens and the surface between
//       features stays untouched. That is the flat wall in the screenshot.
//
//    📝 This one survives because it is not a landform. It is a hole — the initial perforation a stream
//       or a spall makes through a fin, which the weather then widens into an arch. The formation
//       sequence is real: joints open fins, something punches through, weathering does the rest.
//
//    ⚠️ Note what it does NOT do any more: M1's version biased the aperture by the resistance field, so
//       the hole was pre-placed in soft rock. It no longer needs to — differential erosion now finds the
//       soft rock by itself, over time, which is the entire point.
//    Dials: x = Radius, y = Span, z = Elevation, w = Bearing
fn EvaluateSweptTunnel(Probe : vec3f, Dials : vec4f) -> f32
{
    let Cosine = cos(Dials.w);
    let Sine   = sin(Dials.w);
    let Turned = vec3f(Probe.x * Cosine - Probe.z * Sine,
                       Probe.y,
                       Probe.x * Sine   + Probe.z * Cosine);

    let Head = vec3f(-Dials.y * 0.5, Dials.z, 0.0);
    let Tail = vec3f( Dials.y * 0.5, Dials.z, 0.0);
    return DistanceToCapsule(Turned, Head, Tail, max(Dials.x, 0.02));
}

//------------------------------------------------------------- tint

// 📝 Munsell chips converted renotation -> xyY -> XYZ -> Bradford C-to-D65 -> sRGB. All landed inside
//    the sRGB gamut, so none are clipped approximations.
//    Palette 0 = Bryce Claron · 1 = Entrada/Aztec · 2 = weathered granite · 3 = varnished canyon wall.
//    Dials: x = Palette, y = Saturation, z = Mottling, w = Varnish
fn EvaluateStratumTint(Probe : vec3f, Resistance : f32, Dials : vec4f) -> vec3f
{
    let Clamped = clamp(Resistance, 0.0, 1.0);
    let Palette = i32(clamp(Dials.x, 0.0, 3.0));

    var Weak   = vec3f(0.604, 0.282, 0.196);   // hematite fine   #9A4832  10R 4/8
    var Strong = vec3f(0.839, 0.776, 0.686);   // calcite white   #D6C6AF  10YR 8/2

    if (Palette == 1)
    {
        Weak   = vec3f(0.702, 0.396, 0.239);   // Aztec orange    #B3653D  2.5YR 5/8
        Strong = vec3f(0.812, 0.780, 0.737);   // Aztec bleached  #CFC7BC  10YR 8/1
    }
    else if (Palette == 2)
    {
        Weak   = vec3f(0.631, 0.435, 0.247);   // goethite fine   #A16F3F  7.5YR 5/6
        Strong = vec3f(0.863, 0.773, 0.643);   // gypsum pale     #DCC5A4  10YR 8/3
    }
    else if (Palette == 3)
    {
        Weak   = vec3f(0.216, 0.188, 0.161);   // Mn varnish      #373029  10YR 2/1
        Strong = vec3f(0.820, 0.490, 0.384);   // Aztec red       #D17D62  10R 6/8
    }

    // ① Resistance indexes the ramp, because the cement sets both hue and strength.
    var Tint = mix(Weak, Strong, Clamped);

    // ② Mineral mottling from a mid-frequency field — patchy goethite, not a uniform wash.
    let Mottle = EvaluateFractalNoise(Probe * 2.3, 3, 2.0) * 0.5 + 0.5;
    Tint = mix(Tint, Tint * (0.66 + 0.68 * Mottle), Dials.z);

    // ③ Desert varnish streaks. Mn oxide accumulates on faces shielded from runoff, so it reads as
    //    vertical streaking; growth is 1-15 um/ky, so it is a slow patchy overlay, not a coating.
    let Streak  = clamp(EvaluateFractalNoise(vec3f(Probe.x * 5.5, Probe.y * 0.7, Probe.z * 5.5), 3, 2.0), -1.0, 1.0);
    let Varnish = smoothstep(0.1, 0.75, Streak) * Dials.w;
    Tint = mix(Tint, vec3f(0.216, 0.188, 0.161), Varnish * 0.72);

    // ④ Desaturate toward luminance rather than scaling, so Saturation 0 gives true grey.
    let Luminance = dot(Tint, vec3f(0.2126, 0.7152, 0.0722));
    return clamp(mix(vec3f(Luminance), Tint, Dials.y), vec3f(0.0), vec3f(1.0));
}
`;

// 🔴 Hull source AFTER the body, not before. WGSL requires a function to be declared before it is called,
//    and the hull calls HashToUnit, HashToVector, EvaluateValueNoise, EvaluateFractalNoise and
//    SmoothMaximum — all defined in the body above. Prepending it instead fails to compile with an
//    "unresolved identifier" naming the noise helper, which points at the wrong file entirely.
export const DistanceExpressionPreamble = DistanceExpressionBody + HullExpressionSource;
