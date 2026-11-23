//========================================================================================================================
//                                               SphereTrace.js                🧩
//========================================================================================================================


//------------------------------------------------------------------------------------------------------------------------
//                                                 SPHERE TRACE RESOLVE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The march, shading and screen quad. This half of the shader is also fixed; the transcribed tree
//    is spliced in between as EvaluateConstructionTree() and EvaluateConstructionTint().
export const SphereTraceResolve = /* wgsl */`

//------------------------------------------------------------- differential geometry

// 📝 Central differences. The tetrahedron trick saves one evaluation but couples the axes, and with a
//    voxel bake landing in M2 the extra clarity is worth more than one texture fetch.
fn EvaluateSurfaceNormal(Probe : vec3f) -> vec3f
{
    let Delta = 0.0016;
    let Dx = vec3f(Delta, 0.0, 0.0);
    let Dy = vec3f(0.0, Delta, 0.0);
    let Dz = vec3f(0.0, 0.0, Delta);
    return normalize(vec3f(
        EvaluateConstructionTree(Probe + Dx) - EvaluateConstructionTree(Probe - Dx),
        EvaluateConstructionTree(Probe + Dy) - EvaluateConstructionTree(Probe - Dy),
        EvaluateConstructionTree(Probe + Dz) - EvaluateConstructionTree(Probe - Dz)));
}

// 📝 Discrete Laplacian of the field = mean curvature up to a factor. Concave (cavity) reads negative,
//    convex (arris) positive. This is what darkens undercuts: shadow, moisture retention and varnish
//    all accumulate in concavities, and wetting lowers Munsell value while holding chroma.
fn EvaluateFieldCurvature(Probe : vec3f) -> f32
{
    let Delta  = 0.028;
    let Centre = EvaluateConstructionTree(Probe);
    var Sum    = 0.0;
    Sum = Sum + EvaluateConstructionTree(Probe + vec3f( Delta, 0.0, 0.0));
    Sum = Sum + EvaluateConstructionTree(Probe + vec3f(-Delta, 0.0, 0.0));
    Sum = Sum + EvaluateConstructionTree(Probe + vec3f(0.0,  Delta, 0.0));
    Sum = Sum + EvaluateConstructionTree(Probe + vec3f(0.0, -Delta, 0.0));
    Sum = Sum + EvaluateConstructionTree(Probe + vec3f(0.0, 0.0,  Delta));
    Sum = Sum + EvaluateConstructionTree(Probe + vec3f(0.0, 0.0, -Delta));
    return (Sum - 6.0 * Centre) / (Delta * Delta);
}

//------------------------------------------------------------- march

struct TraceOutcome
{
    Range      : f32,
    StepTally  : f32,
    Contact    : bool
};

// 📝 Sphere tracing with a Lipschitz step scale. 🔴 The field is NOT a true distance once smooth-min,
//    additive relief or domain warp enter the tree, so a full d-sized step overshoots and punches
//    through thin ligaments — exactly the arch ligaments this prototype exists to show. StepScale
//    below 1.0 is the correction, and the panel exposes it because getting it wrong is silent.
fn TraceConstructionTree(Origin : vec3f, Bearing : vec3f) -> TraceOutcome
{
    var Outcome = TraceOutcome(0.0, 0.0, false);

    // ① Bounding-sphere entry. The scene is authored inside a fixed radius, so skip the empty run-up
    //    instead of stepping through it.
    //
    // 📝 The radius is a dial, not a constant. It was 14.0 against a seed that fits inside ~4, so most
    //    rays spent their whole step budget crossing empty space before reaching anything. Tightening it
    //    is the cheapest speed win available: it costs nothing per step, it removes steps.
    //    ⚠️ Too tight CLIPS the subject — the far side of the arch vanishes rather than degrading, so
    //       widen it whenever a piece of geometry is missing before suspecting the tree.
    let SceneRadius   = U.SceneRadius;
    let ToCentre      = -Origin;
    let Projection    = dot(ToCentre, Bearing);
    let PerpendicularSq = dot(ToCentre, ToCentre) - Projection * Projection;
    if (PerpendicularSq > SceneRadius * SceneRadius) { return Outcome; }
    let HalfChord = sqrt(max(SceneRadius * SceneRadius - PerpendicularSq, 0.0));
    var Range     = max(Projection - HalfChord, 0.0);

    let StepCeiling = i32(U.StepCeiling);
    for (var Step : i32 = 0; Step < StepCeiling; Step = Step + 1)
    {
        let Probe = Origin + Bearing * Range;
        let Field = EvaluateConstructionTree(Probe);

        // ② Tolerance widens with range so distant pixels stop early — a cheap cone-march surrogate.
        let Tolerance = U.HitTolerance * (1.0 + Range * 0.11);
        if (Field < Tolerance)
        {
            Outcome.Range     = Range;
            Outcome.StepTally = f32(Step);
            Outcome.Contact   = true;
            return Outcome;
        }

        Range = Range + max(Field * U.StepScale, U.HitTolerance * 0.4);
        if (Range > U.FarDistance || Range > Projection + HalfChord) { break; }
        Outcome.StepTally = f32(Step);
    }
    return Outcome;
}

// 📝 Hard shadow by re-marching toward the sun. A soft-shadow variant would accumulate min(1, d/(k*t))
//    but doubles the field evaluations; at this tree depth the hard version already dominates cost.
fn TraceShadowRatio(Origin : vec3f, Bearing : vec3f) -> f32
{
    var Range      = 0.05;
    var Occlusion  = 1.0;
    for (var Step : i32 = 0; Step < 40; Step = Step + 1)
    {
        let Field = EvaluateConstructionTree(Origin + Bearing * Range);
        if (Field < 0.001) { return 0.0; }
        Occlusion = min(Occlusion, 14.0 * Field / Range);
        Range     = Range + max(Field * U.StepScale, 0.012);
        if (Range > 9.0) { break; }
    }
    return clamp(Occlusion, 0.0, 1.0);
}

// 📝 IQ's five-tap ambient occlusion: walk out along the normal and compare the field against the
//    distance actually travelled. Where the field lags behind, geometry is nearby.
fn EvaluateAmbientOcclusion(Probe : vec3f, Normal : vec3f) -> f32
{
    var Occlusion = 0.0;
    var Weight    = 1.0;
    for (var Step : i32 = 0; Step < 5; Step = Step + 1)
    {
        let Offset = 0.014 + 0.12 * f32(Step) / 4.0;
        let Field  = EvaluateConstructionTree(Probe + Normal * Offset);
        Occlusion  = Occlusion + (Offset - Field) * Weight;
        Weight     = Weight * 0.72;
    }
    return clamp(1.0 - 2.6 * Occlusion, 0.0, 1.0);
}

//------------------------------------------------------------- illumination

// 📝 Rock is close to Lambertian, so there is no specular lobe here. The sky term is deliberately blue
//    and the bounce term deliberately warm-red: on a sunlit desert wall the fill light really is
//    reflected ground, and neutral fill is what makes procedural rock read as plastic.
fn ShadeContactPoint(Probe : vec3f, Normal : vec3f, Tint : vec3f, StepTally : f32) -> vec3f
{
    let Solar = normalize(U.SolarBearing);

    // ① Direct sun, shadowed.
    let Incidence = clamp(dot(Normal, Solar), 0.0, 1.0);
    var Shadow    = 1.0;
    if (U.ShadowWeight > 0.01 && Incidence > 0.001)
    {
        Shadow = mix(1.0, TraceShadowRatio(Probe, Solar), U.ShadowWeight);
    }
    let SunColour = vec3f(1.0, 0.93, 0.82);
    var Radiance  = Tint * SunColour * Incidence * Shadow * 2.35;

    // ② Sky dome from above, cool.
    let SkyFacing = clamp(0.5 + 0.5 * Normal.y, 0.0, 1.0);
    Radiance = Radiance + Tint * vec3f(0.34, 0.44, 0.62) * SkyFacing * U.AmbientWeight;

    // ③ Ground bounce from below, warm.
    let GroundFacing = clamp(0.5 - 0.5 * Normal.y, 0.0, 1.0);
    Radiance = Radiance + Tint * vec3f(0.40, 0.26, 0.17) * GroundFacing * U.AmbientWeight * 0.62;

    // ④ Ambient occlusion and cavity darkening. Curvature drives the cavity term because concavities
    //    hold moisture and varnish as well as shadow.
    let Occlusion = EvaluateAmbientOcclusion(Probe, Normal);
    Radiance = Radiance * mix(1.0, Occlusion, 0.85);

    let Curvature = EvaluateFieldCurvature(Probe);
    let Cavity    = clamp(-Curvature * 0.011, 0.0, 1.0);
    Radiance = Radiance * mix(1.0, 1.0 - Cavity, U.CavityWeight);

    // ⑤ Slope masking. Ledges catch pale dust and spalled debris; steep faces stay clean and darker.
    let Ledge = clamp(Normal.y, 0.0, 1.0);
    Radiance = mix(Radiance, Radiance * vec3f(1.16, 1.10, 0.99), Ledge * U.SlopeWeight * 0.55);

    return Radiance;
}

// 📝 ACES-style filmic curve (Narkowicz approximation). Reinhard washes out the sunlit face; this
//    holds the highlight while keeping the shadow chroma the Munsell palette is there to provide.
fn ResolveTonemap(Radiance : vec3f) -> vec3f
{
    let Exposed = Radiance * U.Exposure;
    let Shaped  = (Exposed * (2.51 * Exposed + vec3f(0.03)))
                / (Exposed * (2.43 * Exposed + vec3f(0.59)) + vec3f(0.14));
    let Clamped = clamp(Shaped, vec3f(0.0), vec3f(1.0));
    return pow(Clamped, vec3f(1.0 / 2.2));
}

fn ResolveSkyBackdrop(Bearing : vec3f) -> vec3f
{
    let Elevation = clamp(Bearing.y * 0.5 + 0.5, 0.0, 1.0);
    let Horizon   = vec3f(0.30, 0.33, 0.38);
    let Vault     = vec3f(0.055, 0.075, 0.115);
    var Backdrop  = mix(Horizon, Vault, pow(Elevation, 0.62));

    // ① A faint solar bloom, so the sun bearing dial is legible against the backdrop.
    let Solar = normalize(U.SolarBearing);
    let Glow  = pow(clamp(dot(Bearing, Solar), 0.0, 1.0), 128.0);
    Backdrop  = Backdrop + vec3f(0.85, 0.72, 0.52) * Glow * 0.55;
    return Backdrop;
}

//------------------------------------------------------------- entry points

struct QuadYield
{
    @builtin(position) Coordinate : vec4f,
    @location(0)       Screen     : vec2f
};

@vertex
fn TranscribeQuadVertex(@builtin(vertex_index) VertexIndex : u32) -> QuadYield
{
    // 📝 Two triangles as a strip-free list, so no vertex buffer is bound at all.
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
    // ① Build the ray. Aspect correction on x keeps the field of view square.
    let Aspect  = U.Viewport.x / max(U.Viewport.y, 1.0);
    let Plane   = vec2f(Stream.Screen.x * Aspect, Stream.Screen.y);
    let Bearing = normalize(U.Forward + U.Rightward * Plane.x + U.Upward * Plane.y);

    let Outcome = TraceConstructionTree(U.Origin, Bearing);

    // ② Step-count resolve, for diagnosing where the march is expensive.
    if (U.ResolveMode > 2.5)
    {
        let Load = Outcome.StepTally / max(U.StepCeiling, 1.0);
        return vec4f(Load, 1.0 - Load, 0.14, 1.0);
    }

    if (!Outcome.Contact)
    {
        return vec4f(ResolveSkyBackdrop(Bearing), 1.0);
    }

    let Probe  = U.Origin + Bearing * Outcome.Range;
    let Normal = EvaluateSurfaceNormal(Probe);

    // ③ Normal resolve.
    if (U.ResolveMode > 1.5)
    {
        return vec4f(Normal * 0.5 + vec3f(0.5), 1.0);
    }

    // ④ Resistance resolve — the field that drives both shape and colour, shown directly.
    if (U.ResolveMode > 0.5)
    {
        let Resistance = clamp(EvaluateConstructionResistance(Probe), 0.0, 1.0);
        let Ramp = mix(vec3f(0.72, 0.22, 0.16), vec3f(0.86, 0.84, 0.76), Resistance);
        let Occlusion = EvaluateAmbientOcclusion(Probe, Normal);
        return vec4f(pow(Ramp * mix(0.45, 1.0, Occlusion), vec3f(1.0 / 2.2)), 1.0);
    }

    // ⑤ Preview resolve — a matcap-style lit tint, for while the view or a dial is being dragged.
    //
    // 📝 This exists purely to make interaction responsive. It keeps the tint and the normal, because
    //    those are what you steer the tree by, and drops the three costly terms: the 40-step shadow
    //    re-march, the 5-tap AO and the 6-tap curvature. That is ~51 of ~206 field evaluations per hit
    //    pixel, and combined with half-resolution it is the difference between draggable and not.
    //    ⚠️ Preview is NOT the look. Judge shading, cavity darkening and dust on the idle full render.
    if (U.PreviewMode > 0.5)
    {
        let Tint      = EvaluateConstructionTint(Probe);
        let Solar     = normalize(U.SolarBearing);
        let Incidence = clamp(dot(Normal, Solar), 0.0, 1.0);

        // A wrapped diffuse term stands in for the sky dome and ground bounce at no extra march cost.
        let Wrapped = clamp(0.5 + 0.5 * dot(Normal, Solar), 0.0, 1.0);
        var Preview = Tint * (0.34 + 1.55 * Incidence * 0.72 + 0.30 * Wrapped);

        // Keep the sky facing tint so up and down still read apart while orbiting.
        Preview = Preview + Tint * vec3f(0.10, 0.13, 0.19) * clamp(0.5 + 0.5 * Normal.y, 0.0, 1.0);
        return vec4f(ResolveTonemap(Preview * U.Exposure), 1.0);
    }

    // ⑥ Full resolve.
    let Tint     = EvaluateConstructionTint(Probe);
    let Radiance = ShadeContactPoint(Probe, Normal, Tint, Outcome.StepTally);

    // ⑦ Distance haze, so depth reads without a depth buffer.
    let Haze     = 1.0 - exp(-Outcome.Range * 0.020);
    let Hazed    = mix(Radiance, vec3f(0.30, 0.33, 0.38) * 1.5, Haze * 0.42);
    return vec4f(ResolveTonemap(Hazed), 1.0);
}
`;
