//========================================================================================================================
//                                                 HullExpression.js                                               🧩
//========================================================================================================================
//
// 📝 The STARTING BODY, built the way Blender's rock generator builds it — adapted from meshes to an SDF.
//
//    Verified against the addon source (add_mesh_rocks/rockgen.py, randomize_texture.py, utils.py). The
//    algorithm there is, in order:
//
//      ① one of TWELVE hand-authored 7/8/10-vertex hulls, chosen at random     <- NOT an icosphere
//      ② every vertex coordinate drawn independently from a SKEWED gaussian    <- per-vertex, not per-body
//      ③ edge creases set per shape, jittered  gauss(0.5, 0.125) sharp / gauss(0.25, 0.05) medium
//      ④ TWO subsurf levels, which the creases then selectively resist
//      ⑤ Displace  MUSGRAVE MULTIFRACTAL   mid_level 0   <- coarse shape, additive
//      ⑥ Displace  VORONOI (euclidean)     mid_level 0   <- coarse shape, additive
//      ⑦ Displace  drawn from a pool       mid_level 0.5 <- fine roughness, signed
//      ⑧ Displace  drawn from a pool       mid_level 0.5 <- fine roughness, signed
//      ⑨ optional SMOOTH modifier; all-smooth shading; normals made consistent
//
//    🔴 THE TRANSFERABLE IDEA IS ③+④, NOT THE MESH. Creasing edges and then subdividing is what makes the
//       addon's output read as rock rather than as a smooth ball: some edges survive as facet arrises while
//       others round away. The SDF analogue is a plane-intersection hull where EACH PLANE CARRIES ITS OWN
//       BLEND RADIUS — a small radius is a crease that survived, a large one is a crease the subdivision
//       ate. That is why this file smooth-maxes plane by plane instead of taking one hard max and rounding
//       the result uniformly: a single global round gives every arris the same softness, which is precisely
//       the machined look the whole exercise is trying to escape.
//
//    ⚠️ WHAT IS DELIBERATELY NOT PORTED. The addon's material block (mat_hard, mat_spec, mat_IOR in
//       factory.xml) is VESTIGIAL — rockgen.py contains no material code at all, and `mat_hard` was
//       Blender-Internal specular hardness, a shading term that never influenced geometry. So there is no
//       hardness-driven displacement to borrow. Hardness in this prototype comes from the resistance field
//       and acts through erosion RATE, which is a stronger mechanism than anything the addon had.
//
//    📝 The addon draws per-body randomness from a seed and bakes it into vertex coordinates. Here the body
//       is a function of position evaluated on the GPU, so the equivalent is a hash of the plane index —
//       same effect (no two bodies alike, stable for a given seed), no CPU-side mesh.

//------------------------------------------------------------------------------------------------------------------------
//                                                  PLANE HULL
//------------------------------------------------------------------------------------------------------------------------

export const HullExpressionSource = `

// 📝 A hull face direction. The addon picks one of twelve authored hulls; the analogue here is a
//    deterministic spread of plane normals over the sphere, jittered per body.
//
//    🔴 The FIBONACCI SPHERE, not a random direction per plane. Random normals clump — two planes a few
//       degrees apart cut nearly the same slab, so the body loses a face and gains a near-duplicate arris,
//       and with enough planes it degenerates toward a sphere. The spiral guarantees even angular coverage,
//       and the jitter below is what keeps it from looking like a manufactured polyhedron.
fn HullPlaneNormal(Index : i32, Tally : i32, Jitter : f32, Seed : f32) -> vec3f
{
    let Count = max(f32(Tally), 1.0);
    let Ordinal = f32(Index) + 0.5;

    // ① Even spacing in cos(latitude) is what makes the area per plane equal.
    let Rise = 1.0 - 2.0 * Ordinal / Count;
    let Ring = sqrt(max(1.0 - Rise * Rise, 0.0));

    // 📝 The golden angle. 2*pi*(2 - phi) = 2.39996323, the least-rational turn available, so successive
    //    planes never fall into a repeating pattern for any plane count.
    let Turn = 2.39996323 * Ordinal;

    let Base = vec3f(Ring * cos(Turn), Rise, Ring * sin(Turn));

    // ② Jitter the direction so the hull is not a textbook solid. Scaled by Jitter, hashed on the plane
    //    index and the seed so it is stable across frames but different per body.
    let Skew = (HashToVector(Base * 17.31 + vec3f(Seed * 7.77)) - vec3f(0.5)) * 2.0;
    return normalize(Base + Skew * Jitter * 0.55);
}

// 📝 How far this plane sits from the centre, and how sharp its arris is.
//
//    This is where the addon's SKEWED GAUSSIAN per-vertex draw is reproduced. utils.skewedGauss draws
//    gauss(mu, sigma) and affinely squeezes only the skewed tail into the bound, so the distribution is
//    lopsided rather than clamped — a rock is likelier to be somewhat flat than very flat.
//
//    ⚠️ A true gaussian needs Box-Muller and two hashes. The cheap stand-in here is the sum of three
//       uniforms, which is already visually indistinguishable at this amplitude (Irwin-Hall n=3 is close to
//       normal), and it costs one hash instead of a log and a cosine per plane.
fn HullPlaneOffset(Index : i32, Extent : vec3f, Normal : vec3f, Spread : f32, Seed : f32) -> f32
{
    // 📝 The addon's skewedGauss takes a skew argument per axis. It is fixed here rather than dialled: the
    //    species has four lanes and Faces/Jitter/Spread/Squash all change the silhouette more visibly. This
    //    value biases very slightly inward, which reads as a rock that has already lost its corners.
    let Skew = -0.12;

    // ① The unjittered support distance of an ellipsoid-ish body in this direction. Anisotropic Extent is
    //    what gives a rock proportions rather than being equilateral.
    let Support = length(Normal * Extent);

    // ② Three hashes summed, centred, giving a bell-ish variate on [-1.5, 1.5].
    let Ordinal = f32(Index);
    let Draw = HashToUnit(vec3f(Ordinal * 0.113, Seed, 3.7))
             + HashToUnit(vec3f(Ordinal * 0.291, Seed, 9.1))
             + HashToUnit(vec3f(Ordinal * 0.577, Seed, 5.3)) - 1.5;

    // ③ Skew it. Positive Skew biases planes OUTWARD (a blockier, fuller rock); negative biases inward
    //    (a flatter, more slab-like one). The asymmetric squeeze mirrors skewedGauss rather than a clamp.
    let Signed = select(Draw * (1.0 + Skew), Draw * (1.0 - Skew), Draw < 0.0);

    // ⚠️ Floored well above zero. A plane through the origin cuts the body in half, and several such planes
    //    leave nothing at all -- an empty grid, which presents as the sim having failed rather than as an
    //    over-driven dial.
    return max(Support * (1.0 + Signed * Spread * 0.45), Support * 0.25);
}

// 📝 The hull itself: intersect every half-space, each with ITS OWN blend radius.
//
//    🔴 This is the crease-then-subdivide analogue and the heart of the file. Per-plane radii mean some
//       arrises stay sharp while others round off. Using one global radius instead -- max() then a uniform
//       round -- makes every edge identically soft, which is the machined look the addon avoids by
//       creasing selectively. The addon's three crease tiers (sharp 0.5, medium 0.25, soft 0.125) map onto
//       three radius bands drawn by the same hash.
//
//    ⚠️ The result is a BOUND, not an exact distance -- SmoothMaximum guarantees only that. Fine for a
//       fixed-step seed dispatch, and fine for the analytic preview whose step is already clamped. It would
//       NOT be safe for an unclamped sphere trace.
fn DistanceToPlaneHull(Probe : vec3f, Extent : vec3f, Tally : i32, Jitter : f32, Spread : f32,
                       Crease : f32, Seed : f32) -> f32
{
    let Faces = clamp(Tally, 4, 24);

    // ① Start from a sphere just enclosing the extent, so the hull is bounded even before any plane cuts.
    //    Starting from a large constant instead would leave the body unbounded wherever the plane spread
    //    happens to leave a gap, and the march would run to its ceiling looking for a surface.
    var Distance = length(Probe) - max(Extent.x, max(Extent.y, Extent.z)) * 1.35;

    for (var Index = 0; Index < Faces; Index = Index + 1)
    {
        let Normal = HullPlaneNormal(Index, Faces, Jitter, Seed);
        let Offset = HullPlaneOffset(Index, Extent, Normal, Spread, Seed);

        // ② The half-space. Positive outside, negative inside -- an exact plane distance.
        let Slab = dot(Probe, Normal) - Offset;

        // ③ This plane's own crease radius. Three bands, mirroring the addon's three crease tiers.
        let Band = HashToUnit(vec3f(f32(Index) * 0.731, Seed * 1.31, 11.9));
        let Radius = Crease * select(select(0.28, 0.62, Band > 0.38), 1.00, Band > 0.74);

        Distance = SmoothMaximum(Distance, Slab, Radius);
    }

    return Distance;
}

//------------------------------------------------------------- displacement stack
//
// 📝 The addon's four Displace modifiers, in its order and with its mid_level convention:
//
//      coarse  MUSGRAVE MULTIFRACTAL  mid_level 0    additive, pushes outward only
//      coarse  VORONOI euclidean      mid_level 0    additive
//      fine    pooled noise           mid_level 0.5  signed, in and out about the midpoint
//      fine    pooled noise           mid_level 0.5  signed
//
//    🔴 The mid_level split is not decoration. mid_level 0 means the displacement only ADDS material, so the
//       coarse stage inflates the hull into lumps without ever cutting into it; mid_level 0.5 means the fine
//       stage cuts as much as it adds, which is what roughness is. Applying the coarse stage signed instead
//       eats through thin parts of the hull and can open holes the author never asked for.
//
//    📝 MULTIFRACTAL is hardcoded in randomize_texture.py -- it is NOT fBm, hetero-terrain, or ridged (those
//       sit commented out on lines 72-73). Multifractal MULTIPLIES octave contributions, so amplitude varies
//       with position: some regions come out smooth and others violently rough from one texture. That
//       variability is a good part of why the addon's output does not look uniformly noisy, so it is worth
//       reproducing properly rather than substituting the fBm already in the library.
fn EvaluateMultifractal(Probe : vec3f, Octaves : i32, Lacunarity : f32, Dimension : f32) -> f32
{
    var Total     = 1.0;
    var Frequency = 1.0;
    // 📝 Multifractal weights each octave by the running product, so the exponent is applied per octave.
    let Falloff = pow(Lacunarity, -Dimension);
    var Weight  = 1.0;

    for (var Step = 0; Step < Octaves; Step = Step + 1)
    {
        // ⚠️ Product, not sum. Summing here would be fBm and would lose the whole point -- the
        //    position-dependent amplitude that makes one part of the rock smooth and another jagged.
        Total     = Total * (1.0 + Weight * EvaluateValueNoise(Probe * Frequency) * 2.0 - Weight);
        Weight    = Weight * Falloff;
        Frequency = Frequency * Lacunarity;
    }

    return Total - 1.0;
}

// 📝 Voronoi F1 distance, euclidean. At the addon's coarse level the metric is restricted to euclidean or
//    euclidean-squared (randint(0,1)); the exotic Minkovsky metrics only appear at fine detail.
//
//    ⚠️ This is the SAME cell distance M1 used as a SHAPE source, which was the M1 failure -- subtracting
//       Voronoi from a slab draws cracks on a flat face and calls it rock. Here it is a DISPLACEMENT of an
//       already-irregular hull with mid_level 0, so it swells the body into lobes instead of scoring seams
//       into it. Same function, opposite role.
fn EvaluateVoronoiSwell(Probe : vec3f, Squared : f32) -> f32
{
    let Base = floor(Probe);
    var Nearest = 8.0;

    for (var z = -1; z <= 1; z = z + 1) {
    for (var y = -1; y <= 1; y = y + 1) {
    for (var x = -1; x <= 1; x = x + 1) {
        let Cell   = Base + vec3f(f32(x), f32(y), f32(z));
        let Site   = Cell + HashToVector(Cell);
        let Offset = Site - Probe;
        let Span   = select(length(Offset), dot(Offset, Offset), Squared > 0.5);
        Nearest = min(Nearest, Span);
    }}}

    return Nearest;
}

// 📝 The whole stack applied to a hull distance. Displacement of an SDF is subtraction: a positive
//    displacement pushes the surface outward, so it comes off the distance.
//
//    Dials: Deform = coarse amplitude, Rough = fine amplitude, both matching the addon's deform/rough
//    split (which it internally scales by /10 and /100 respectively before use).
fn ApplyHullDisplacement(Probe : vec3f, Distance : f32, Deform : f32, Rough : f32,
                         Detail : f32, Seed : f32) -> f32
{
    var Result = Distance;
    let Shifted = Probe + vec3f(Seed * 3.17, Seed * 1.93, Seed * 5.11);
    let Fine = max(Detail, 0.05);

    // ① Coarse, MUSGRAVE MULTIFRACTAL, mid_level 0 -> additive only.
    //    Lacunarity 1.8-10.0 in the addon via beta(3,8)*8.2+1.8; 2.4 sits in the dense part of that beta.
    let Coarse = EvaluateMultifractal(Shifted * 0.85, 4, 2.4, 0.92);
    Result = Result - max(Coarse, 0.0) * Deform;

    // ② Coarse, VORONOI euclidean, mid_level 0 -> additive only. 1 - F1 so cell CENTRES swell outward and
    //    the boundaries stay put, which reads as lobes rather than as scored seams.
    let Swell = max(1.0 - EvaluateVoronoiSwell(Shifted * 1.45, 0.0), 0.0);
    Result = Result - Swell * Deform * 0.55;

    // ③ Fine, pooled noise, mid_level 0.5 -> SIGNED. Two octave bands standing in for the addon's two
    //    independently-drawn fine textures; the second is offset and higher frequency so they do not
    //    correlate into one louder copy of the same pattern.
    let FineA = EvaluateFractalNoise(Shifted * 4.30 * Fine, 4, 2.02) - 0.5;
    let FineB = EvaluateValueNoise(Shifted * 9.70 * Fine + vec3f(19.3)) - 0.5;
    Result = Result - (FineA * 0.70 + FineB * 0.30) * Rough;

    return Result;
}
`;
