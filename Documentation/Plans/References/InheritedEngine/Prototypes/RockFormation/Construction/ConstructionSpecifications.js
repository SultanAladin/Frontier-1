//========================================================================================================================
//                                         ConstructionSpecifications.js                                           🧩
//========================================================================================================================
//
// 📝 The species catalogue. Each entry declares what it yields, what it takes, its dials, and a
//    Transcribe() that emits ONE WGSL expression calling into DistanceExpression.js.
//
//    🔴 Transcribe() must call only functions that DistanceExpression.js actually defines, and must pass
//       dials in the order that function documents. A mismatch here surfaces as a shader compile error
//       with a line number pointing at generated code, which is painful to trace — so the WGSL signature
//       is quoted in a comment above every Transcribe that takes a vec4f of dials.
//
//    Dial tuple: [Label, Key, Minimum, Maximum, Default, Precision, Unit]
//    The four dials of an entry pack into U.Entry<Slot> as xyzw IN DIAL ORDER. An entry with fewer than
//    four dials leaves the remaining lanes at zero; an entry with more than four needs a second slot,
//    which no species currently does and which the host asserts against.

import { GeologyReference, SelfTunedReference, ReferenceToSceneScale } from "./GeologyReference.js";

const Scale = ReferenceToSceneScale;

export const ConstructionSpecificationTable =
{
    //--------------------------------------------------------------------------------------------------
    //                                            MASS
    //--------------------------------------------------------------------------------------------------

    // 🔴 THE STARTING BODY, and the point of the whole redesign.
    //
    //    M1 had five compound species — Boulder, Mesa, Hoodoo, Fin, Aperture — each an analytic guess at
    //    what a weathered landform looks like. They are gone. The premise was wrong: they encoded an END
    //    STATE, and an end state has no history, so hardness had nowhere to accumulate damage and the
    //    surface between features was never touched. That is what produced the flat wall.
    //
    //    What replaces them is a plain block. Everything that makes it look like rock happens downstream,
    //    in the Weather chain, over simulated years.
    //
    // 🔴 THE PREFERRED STARTING BODY — the Blender rock generator's algorithm, ported to an SDF.
    //
    //    Verified against add_mesh_rocks/rockgen.py. The addon does NOT start from an icosphere: it picks one
    //    of twelve authored 7/8/10-vertex hulls, draws every vertex coordinate from a skewed gaussian, sets
    //    per-edge creases in three jittered tiers, and only THEN subdivides twice. The creasing is what
    //    matters — it makes some arrises survive subdivision as facets while others round away.
    //
    //    The SDF analogue is a plane-intersection hull where each plane carries its OWN blend radius, which
    //    is what "faces" and "crease" below control. See HullExpression.js for the full mapping.
    //
    //    📝 Split across TWO species — HullMass for the shape, HullRelief for the displacement stack —
    //       because the algorithm needs eight parameters and an entry gets exactly one four-lane slot. The
    //       split is not merely a workaround: it mirrors the addon's own separation of hull generation from
    //       its four Displace modifiers, and it lets the author preview the bare hull before roughening it.
    //
    // WGSL: DistanceToPlaneHull(Probe, Extent, Tally, Jitter, Spread, Crease, Seed)
    HullMass:
    {
        Naming: "Hull Mass", Family: "Mass", Glyph: "Hull", Yields: "Distance",
        Summary: "an irregular faceted block — the Blender rock generator's hull, as a distance field",
        Intakes:
        [
            { Naming: "warp", Category: "Warp", Optional: true }
        ],
        Dials:
        [
            // 📝 Faces: the addon's hulls carry 7-10 vertices, so a comparable face count sits in the low
            //    teens. Fewer than about 6 reads as a crystal; past ~20 the facets stop being legible and
            //    it converges on a sphere.
            ["Faces",  "faces",  4.0, 24.0, 11.0, 0, "-"],
            // 📝 Spread is the addon's per-vertex skewed-gaussian draw: how unequally the planes sit from
            //    centre, hence how lopsided the rock is. This is the addon's ACTUAL irregularity mechanism,
            //    which is why it keeps a lane where Jitter lost one.
            //
            //    ⚠️ Jitter was the fourth dial and is now fixed inside the shader. It perturbed the plane
            //       DIRECTIONS while Spread perturbs their DISTANCES, and at the amplitudes that look right
            //       the two are visually near-indistinguishable — both just make the solid less regular. One
            //       of the four lanes had to fund Extent, and giving up the duplicate of a mechanism beats
            //       giving up the mechanism itself. Jitter's default 0.55 is baked in below.
            ["Spread", "spread", 0.0,  1.0,  0.42, 2, "-"],
            // 🔴 EXTENT IN METRES, and it must stay in metres. The first version of this species hardcoded
            //    the extent as vec3f(1.0, squash, 0.94) — dimensionless, while BlockMass (the species it
            //    replaced) dialled 2.40 m half-extents and the tunnel that bores the arch is dialled in
            //    metres too. A GPU probe measured the result at 1.58% grid occupancy against a 2.6 m domain:
            //    the body came out ~2.4x too small, and the 0.72 m tunnel then swallowed it whole instead of
            //    boring through it, so no arch could ever appear. Nothing about that is visible in a shader
            //    that compiles perfectly — the units have to be carried, not implied.
            //
            //    📝 Squash folded away into Height. It was a separate dimensionless multiplier, but a height
            //       in metres expresses the same proportion directly and reads against the other species.
            ["Extent", "extent", 0.4,  2.5,  2.20, 2, "m"],
            ["Height", "height", 0.3,  2.5,  1.75, 2, "m"]
        ],
        Transcribe: (Operands, Dials, Slot) =>
        {
            const Probe = Operands.warp || "Probe";

            // ⚠️ Anisotropy is applied to the EXTENT, not to the probe. Scaling the probe non-uniformly and
            //    then taking a plane distance breaks the metric — dot(Probe*S, Normal) is not a distance in
            //    the scaled space — so the hull would read as a valid surface while every arris sat slightly
            //    off, and the seed would quantise it wrong. Anisotropy in the extent stays exact.
            //
            //    📝 The z half-extent is Extent * 0.94 rather than a fifth lane. A rock wants unequal
            //       proportions, but the plane spread already varies the silhouette far more than a third
            //       independent axis would. Crease (0.13) and Jitter (0.55) are likewise fixed: the crease
            //       scale reproduces the addon's three jittered tiers (sharp 0.5 / medium 0.25 / soft 0.125,
            //       scaled to metres), with the per-plane band draw inside the shader varying it.
            return `DistanceToPlaneHull(${Probe}, ` +
                   `vec3f(U.Entry[${Slot}].z, U.Entry[${Slot}].w, U.Entry[${Slot}].z * 0.94), ` +
                   `i32(U.Entry[${Slot}].x), 0.55, U.Entry[${Slot}].y, ` +
                   `0.13, f32(${Slot}) * 1.7 + 0.3)`;
        }
    },

    // 📝 The addon's four Displace modifiers, as one species.
    //
    //    🔴 The mid_level split is preserved and is not cosmetic: the two COARSE displaces run at
    //       mid_level 0 (additive only — they inflate the hull into lumps), the two FINE ones at
    //       mid_level 0.5 (signed — they cut as much as they add, which is what roughness is). Running the
    //       coarse stage signed instead eats through thin parts of the hull and opens holes nobody asked for.
    //
    // WGSL: ApplyHullDisplacement(Probe, Distance, Deform, Rough, Seed)
    HullRelief:
    {
        Naming: "Hull Relief", Family: "Mass", Glyph: "Rlf", Yields: "Distance",
        Summary: "multifractal + voronoi swell then fine noise — the addon's four Displace modifiers",
        Intakes:
        [
            { Naming: "mass", Category: "Distance", Optional: false }
        ],
        Dials:
        [
            // 📝 The addon's `deform`, which it internally divides by 10 before use. Its default preset is
            //    5.0 -> 0.5; River Rock 3.0, Asteroid 7.5. Scaled here to scene metres.
            ["Deform", "deform", 0.0, 1.2, 0.34, 2, "m"],
            // 📝 The addon's `rough`, internally /100. Default 2.5, Asteroid 3.0, Sandstone 0.5.
            ["Rough",  "rough",  0.0, 0.5, 0.11, 3, "m"],
            // 📝 Frequency of the coarse multifractal band. The addon draws noise_scale from
            //    gauss(0.625, 1/24) at its coarse level, so a little either side of 1.0 here.
            ["Grain",  "grain",  0.3, 2.5, 1.00, 2, "-"],
            // 📝 Fine-band frequency. The addon PINS its fine textures to noise_scale 0.15 — much higher
            //    frequency than the coarse band — which is why detail and silhouette stay separable.
            ["Detail", "detail", 0.5, 4.0, 1.00, 2, "-"]
        ],
        Transcribe: (Operands, Dials, Slot) =>
        {
            // 📝 Displacement of an SDF acts on the distance the upstream body already computed, so this
            //    entry does NOT touch the probe.
            //
            //    🔴 It CANNOT touch the probe. The transcriber emits each upstream entry to its own `let`
            //       binding BEFORE the consumer's expression runs, so by the time this expression is
            //       evaluated the hull distance is already a number computed at the unmodified Probe.
            //       Scaling `Probe` here would leave the hull sampled at one point and the noise at another
            //       — the relief would slide across the surface as the dial turned, looking like a
            //       correlation bug. Anisotropy therefore belongs upstream, where the probe enters: it is
            //       HullMass's Squash dial, fed through the Warp intake, exactly as DomainWarp does it.
            return `ApplyHullDisplacement(Probe * vec3f(U.Entry[${Slot}].z), ${Operands.mass}, ` +
                   `U.Entry[${Slot}].x, U.Entry[${Slot}].y, ` +
                   `U.Entry[${Slot}].w, f32(${Slot}) * 2.3 + 1.1)`;
        }
    },

    // WGSL: DistanceToRoundedBox(Probe, Extent, Rounding)
    BlockMass:
    {
        Naming: "Block Mass", Family: "Mass", Glyph: "Block", Yields: "Distance",
        Summary: "the unweathered block of stone — a cube for the weather to work on",
        Intakes:
        [
            { Naming: "warp", Category: "Warp", Optional: true }
        ],
        Dials:
        [
            ["Width",    "width",    0.3, 8.0, 2.40, 2, "m"],
            ["Height",   "height",   0.3, 8.0, 2.40, 2, "m"],
            ["Depth",    "depth",    0.3, 8.0, 2.40, 2, "m"],
            // 📝 A trace of rounding, not zero. A mathematically sharp arris occupies less than one cell,
            //    so it seeds as a staircase; 0.04 m lands the corner on a resolvable curve. The weather
            //    rounds it far further within the first few hundred steps regardless.
            ["Rounding", "rounding", 0.0, 1.2, 0.04, 3, "m"]
        ],
        Transcribe: (Operands, Dials, Slot) =>
        {
            const Probe = Operands.warp || "Probe";
            return `DistanceToRoundedBox(${Probe}, U.Entry[${Slot}].xyz, U.Entry[${Slot}].w)`;
        }
    },

    // WGSL: DistanceToSphere(Probe, Radius)
    // 📝 Mostly a CARVER for the boolean intakes — subtract a sphere from the block to open an arch.
    SphereMass:
    {
        Naming: "Sphere Mass", Family: "Mass", Glyph: "Sph", Yields: "Distance",
        Summary: "a ball — as a body, or as the carver that punches an arch through a block",
        Intakes:
        [
            { Naming: "warp", Category: "Warp", Optional: true }
        ],
        Dials:
        [
            ["Radius",  "radius",  0.1, 6.0,  1.10, 2, "m"],
            ["Offset X","offsetX",-6.0, 6.0,  0.00, 2, "m"],
            ["Offset Y","offsetY",-6.0, 6.0,  0.00, 2, "m"],
            ["Offset Z","offsetZ",-6.0, 6.0,  0.00, 2, "m"]
        ],
        Transcribe: (Operands, Dials, Slot) =>
        {
            const Probe = Operands.warp || "Probe";
            return `DistanceToSphere(${Probe} - vec3f(U.Entry[${Slot}].y, U.Entry[${Slot}].z, U.Entry[${Slot}].w), U.Entry[${Slot}].x)`;
        }
    },

    // WGSL: DistanceToCapsule(Probe, Start, End, Radius)
    // 📝 The arch carver. A horizontal capsule swept through a block leaves a span above and legs either
    //    side — the same geometry a stream cuts through a fin before the weather widens it.
    TunnelMass:
    {
        Naming: "Tunnel Mass", Family: "Mass", Glyph: "Tun", Yields: "Distance",
        Summary: "a swept capsule — subtract it from a block and what remains is an arch",
        Intakes:
        [
            { Naming: "warp", Category: "Warp", Optional: true }
        ],
        Dials:
        [
            ["Radius",    "radius",    0.1, 4.0,  0.85, 2, "m"],
            ["Span",      "span",      0.2, 9.0,  3.20, 2, "m"],
            ["Elevation", "elevation",-4.0, 4.0, -0.30, 2, "m"],
            ["Bearing",   "bearing",  -3.15,3.15, 0.00, 2, "rad"]
        ],
        Transcribe: (Operands, Dials, Slot) =>
        {
            const Probe = Operands.warp || "Probe";
            return `EvaluateSweptTunnel(${Probe}, U.Entry[${Slot}])`;
        }
    },

    //--------------------------------------------------------------------------------------------------
    //                                         RESISTANCE
    //--------------------------------------------------------------------------------------------------

    // WGSL: EvaluateStratumBand(Probe, Beneath, Dials) — x=Thickness y=Dip z=Contrast w=Offset
    // Thickness default = Claron pink member (167 m) brought to scene scale.
    StratumBand:
    {
        Naming: "Stratum Band", Family: "Resistance", Glyph: "Strat", Yields: "Resistance",
        Summary: "alternating hard and soft beds with a Wyvill cubic transition",
        Intakes:
        [
            { Naming: "beneath", Category: "Resistance", Optional: true },
            { Naming: "warp",    Category: "Warp",       Optional: true }
        ],
        Dials:
        [
            ["Thickness", "thickness", 0.05, 3.0, GeologyReference.ClaronPinkThickness * Scale, 3, "m"],
            ["Dip",       "dip",      -0.5,  0.5, 0.07, 3, "-"],
            ["Contrast",  "contrast",  0.0,  1.0, 0.62, 2, "-"],
            ["Offset",    "offset",   -4.0,  4.0, 0.00, 2, "m"]
        ],
        Transcribe: (Operands, Dials, Slot) =>
        {
            const Probe   = Operands.warp    || "Probe";
            const Beneath = Operands.beneath || "0.5";
            return `EvaluateStratumBand(${Probe}, ${Beneath}, U.Entry[${Slot}])`;
        }
    },

    // WGSL: EvaluateJointNetwork(Probe, Beneath, Dials) — x=Spacing y=Aperture z=Bearing w=Weakening
    // Spacing default = S/T ratio 1.00 applied to the same bed thickness, per Ji 2022.
    JointNetwork:
    {
        Naming: "Joint Network", Family: "Resistance", Glyph: "Joint", Yields: "Resistance",
        Summary: "Voronoi cell-boundary fractures — flat faces meeting at sharp arrises",
        Intakes:
        [
            { Naming: "beneath", Category: "Resistance", Optional: true },
            { Naming: "warp",    Category: "Warp",       Optional: true }
        ],
        Dials:
        [
            ["Spacing",   "spacing",  0.05, 4.0,
                GeologyReference.ClaronPinkThickness * Scale * GeologyReference.JointSpacingRatio, 3, "m"],
            ["Aperture",  "aperture", 0.001, 0.4, 0.045, 3, "m"],
            ["Bearing",   "bearing", -3.15,  3.15, 0.61,  2, "rad"],
            ["Weakening", "weaken",   0.0,   1.0, 0.55,  2, "-"]
        ],
        Transcribe: (Operands, Dials, Slot) =>
        {
            const Probe   = Operands.warp    || "Probe";
            const Beneath = Operands.beneath || "0.5";
            return `EvaluateJointNetwork(${Probe}, ${Beneath}, U.Entry[${Slot}])`;
        }
    },

    //--------------------------------------------------------------------------------------------------
    //                                           WEATHER
    //--------------------------------------------------------------------------------------------------
    //
    // 📝 The erosion chain. These do NOT transcribe into the distance expression — they name a compute
    //    dispatch. Transcribe() returns the upstream operand unchanged so the chain stays walkable, and
    //    the host reads the chain order to decide which dispatches to run and in what sequence.
    //
    //    🔴 ORDER IS MEANINGFUL, unlike everywhere else in this tree. Distance operands commute; weather
    //       does not. Running aeolian before thermal sand-blasts material that was about to collapse
    //       anyway, so the talus apron never forms. The chain order IS the geological sequence.
    //
    //    ⚠️ Every dial here restarts the simulation from the seed block. That is not a limitation to be
    //       worked around — erosion has history, so changing a rate part-way through would give a result
    //       that corresponds to no consistent set of conditions.

    ThermalWeather:
    {
        Naming: "Thermal", Family: "Weather", Glyph: "Ther", Yields: "Weather",
        Summary: "slope past the angle of repose collapses and piles below — conserves mass exactly",
        Process: "Thermal",
        Intakes:
        [
            { Naming: "before", Category: "Weather", Optional: true }
        ],
        Dials:
        [
            // ⚠️ Self-tuned to the cell metric. Angular talus stands at 33-37 deg, tan(35 deg) ~= 0.70,
            //    but expressed as a density difference over one cell rather than a measured gradient.
            ["Repose",    "repose",    0.02, 0.60, 0.16, 3, "-"],
            ["Rate",      "rate",      0.0,  1.0,  0.42, 2, "-"],
            ["Intensity", "intensity", 0.0,  4.0,  1.00, 2, "-"],
            ["Reserved",  "reserved",  0.0,  1.0,  0.00, 2, "-"]
        ],
        Transcribe: (Operands) => Operands.before || ""
    },

    HydraulicWeather:
    {
        Naming: "Hydraulic", Family: "Weather", Glyph: "Hydr", Yields: "Weather",
        Summary: "water accumulates downhill and abrades in proportion to flow — cuts channels",
        Process: "Hydraulic",
        Intakes:
        [
            { Naming: "before", Category: "Weather", Optional: true }
        ],
        Dials:
        [
            ["Rainfall",  "rainfall",  0.0, 2.0, 0.85, 2, "-"],
            ["Abrasion",  "abrasion",  0.0, 2.0, 0.70, 2, "-"],
            ["Channeling","channel",   0.0, 2.0, 1.00, 2, "-"],
            ["Intensity", "intensity", 0.0, 4.0, 1.00, 2, "-"]
        ],
        Transcribe: (Operands) => Operands.before || ""
    },

    AeolianWeather:
    {
        Naming: "Aeolian", Family: "Weather", Glyph: "Wind", Yields: "Weather",
        Summary: "windward faces sand-blasted, strongest near the ground — undercuts a boulder",
        Process: "Aeolian",
        Intakes:
        [
            { Naming: "before", Category: "Weather", Optional: true }
        ],
        Dials:
        [
            ["Bearing",   "bearing",  -3.15, 3.15, 0.90, 2, "rad"],
            ["Strength",  "strength",  0.0,  2.0,  0.75, 2, "-"],
            ["Saltation", "saltation", 0.05, 1.0,  0.42, 2, "-"],
            ["Intensity", "intensity", 0.0,  4.0,  1.00, 2, "-"]
        ],
        Transcribe: (Operands) => Operands.before || ""
    },

    SpheroidalWeather:
    {
        Naming: "Spheroidal", Family: "Weather", Glyph: "Sphe", Yields: "Weather",
        Summary: "convex corners round shell by shell toward a corestone — kills the cube's edges",
        Process: "Spheroidal",
        Intakes:
        [
            { Naming: "before", Category: "Weather", Optional: true }
        ],
        Dials:
        [
            ["Rounding",  "rounding",  0.0, 2.0, 0.60, 2, "-"],
            ["Intensity", "intensity", 0.0, 4.0, 1.00, 2, "-"],
            ["Reserved",  "reserved2", 0.0, 1.0, 0.00, 2, "-"],
            ["Reserved",  "reserved3", 0.0, 1.0, 0.00, 2, "-"]
        ],
        Transcribe: (Operands) => Operands.before || ""
    },

    SaltFreezeWeather:
    {
        Naming: "Salt & Freeze", Family: "Weather", Glyph: "Salt", Yields: "Weather",
        Summary: "sheltered hollows deepen into alcoves and honeycomb — tafoni",
        Process: "SaltFreeze",
        Intakes:
        [
            { Naming: "before", Category: "Weather", Optional: true }
        ],
        Dials:
        [
            ["Fretting",  "fretting",  0.0, 2.0, 0.65, 2, "-"],
            ["Shelter",   "shelter",   0.0, 1.0, 0.55, 2, "-"],
            ["Pore Size", "pore",      2.0, 20.0, 9.00, 1, "-"],
            ["Intensity", "intensity", 0.0, 4.0, 1.00, 2, "-"]
        ],
        Transcribe: (Operands) => Operands.before || ""
    },

    //--------------------------------------------------------------------------------------------------
    //                                            WARP
    //--------------------------------------------------------------------------------------------------

    // WGSL: ApplyDomainWarp(Probe, Dials) — x=Amplitude y=Frequency z=Octaves w=Twist
    // 🔴 Amplitude raises the Lipschitz bound. The host lowers StepScale as amplitude climbs.
    DomainWarp:
    {
        Naming: "Domain Warp", Family: "Warp", Glyph: "Warp", Yields: "Warp",
        Summary: "fractal displacement of the probe — the noise the shapes are built on",
        Intakes:
        [
            { Naming: "warp", Category: "Warp", Optional: true }
        ],
        Dials:
        [
            ["Amplitude", "amplitude", 0.0, 1.2, 0.22, 3, "m"],
            ["Frequency", "frequency", 0.1, 6.0, 1.15, 2, "-"],
            ["Octaves",   "octaves",   1.0, 6.0, 3.00, 0, "-"],
            ["Twist",     "twist",     0.0, 1.0, 0.00, 3, "-"]
        ],
        Transcribe: (Operands, Dials, Slot) =>
        {
            const Probe = Operands.warp || "Probe";
            return `ApplyDomainWarp(${Probe}, U.Entry[${Slot}])`;
        }
    },

    // WGSL: ApplyRidgedRelief(Probe, Dials) — x=Amplitude y=Frequency z=Octaves w=Lacunarity
    RidgedRelief:
    {
        Naming: "Ridged Relief", Family: "Warp", Glyph: "Ridge", Yields: "Warp",
        Summary: "ridged multifractal lift — creased crests rather than rounded swells",
        Intakes:
        [
            { Naming: "warp", Category: "Warp", Optional: true }
        ],
        Dials:
        [
            ["Amplitude",  "amplitude",  0.0, 1.5, 0.30, 3, "m"],
            ["Frequency",  "frequency",  0.1, 6.0, 0.85, 2, "-"],
            ["Octaves",    "octaves",    1.0, 6.0, 4.00, 0, "-"],
            ["Lacunarity", "lacunarity", 1.2, 3.2, 2.05, 2, "-"]
        ],
        Transcribe: (Operands, Dials, Slot) =>
        {
            const Probe = Operands.warp || "Probe";
            return `ApplyRidgedRelief(${Probe}, U.Entry[${Slot}])`;
        }
    },

    //--------------------------------------------------------------------------------------------------
    //                                        COMBINATION
    //--------------------------------------------------------------------------------------------------

    // WGSL: SmoothMinimum(Left, Right, Softness) — exact min() at Softness 0
    SmoothUnion:
    {
        Naming: "Smooth Union", Family: "Combination", Glyph: "Union", Yields: "Distance",
        Summary: "merge two masses; exact union at zero softness",
        Intakes:
        [
            { Naming: "left",  Category: "Distance" },
            { Naming: "right", Category: "Distance" }
        ],
        Dials:
        [
            ["Softness", "softness", 0.0, 1.0, 0.12, 3, "m"]
        ],
        Transcribe: (Operands, Dials, Slot) =>
        {
            const Left  = Operands.left  || "1e9";
            const Right = Operands.right || "1e9";
            return `SmoothMinimum(${Left}, ${Right}, U.Entry[${Slot}].x)`;
        }
    },

    // WGSL: SmoothSubtraction(Mass, Carver, Softness)
    // 📝 THE ARCH MAKER, now as a plain boolean. M1 folded a synthesised capsule into this operator so
    //    that "carve an arch" was one node; that hid the geometry inside the operator and made the shape
    //    unauthorable. Here the carver is an ORDINARY MASS on an ordinary intake — a Tunnel, a Sphere, or
    //    anything else — which is what the user asked for by "booleans to get arcs/ridges".
    MassSubtract:
    {
        Naming: "Subtract", Family: "Combination", Glyph: "Sub", Yields: "Distance",
        Summary: "cut one mass out of another — a tunnel through a block leaves an arch",
        Intakes:
        [
            { Naming: "mass",   Category: "Distance" },
            { Naming: "carver", Category: "Distance" }
        ],
        Dials:
        [
            ["Softness", "softness", 0.0, 0.8, 0.06, 3, "m"]
        ],
        Transcribe: (Operands, Dials, Slot) =>
        {
            const Mass   = Operands.mass   || "1e9";
            const Carver = Operands.carver || "1e9";
            return `SmoothSubtraction(${Mass}, ${Carver}, U.Entry[${Slot}].x)`;
        }
    },

    // WGSL: SmoothMaximum(Left, Right, Softness)
    MassIntersect:
    {
        Naming: "Intersect", Family: "Combination", Glyph: "And", Yields: "Distance",
        Summary: "keep only what both masses share — trims a block to a ridge or a wedge",
        Intakes:
        [
            { Naming: "left",  Category: "Distance" },
            { Naming: "right", Category: "Distance" }
        ],
        Dials:
        [
            ["Softness", "softness", 0.0, 0.8, 0.06, 3, "m"]
        ],
        Transcribe: (Operands, Dials, Slot) =>
        {
            const Left  = Operands.left  || "1e9";
            const Right = Operands.right || "1e9";
            return `SmoothMaximum(${Left}, ${Right}, U.Entry[${Slot}].x)`;
        }
    },

    // WGSL: ApplyLateralRepeat(Probe, Dials) — x=Spacing y=Extent z=Jitter w=Axis
    // 🔴 Yields Warp, not Distance: it displaces the domain, so it must sit UPSTREAM of a mass.
    LateralRepeat:
    {
        Naming: "Lateral Repeat", Family: "Combination", Glyph: "Repeat", Yields: "Warp",
        Summary: "finite domain repetition — a fin field or hoodoo stand from one body",
        Intakes:
        [
            { Naming: "warp", Category: "Warp", Optional: true }
        ],
        Dials:
        [
            ["Spacing", "spacing", 0.1, 6.0, 1.80, 2, "m"],
            ["Extent",  "extent",  0.0, 6.0, 2.00, 0, "idx"],
            ["Jitter",  "jitter",  0.0, 1.0, 0.35, 2, "-"],
            ["Axis",    "axis",    0.0, 2.0, 0.00, 0, "idx"]
        ],
        Transcribe: (Operands, Dials, Slot) =>
        {
            const Probe = Operands.warp || "Probe";
            return `ApplyLateralRepeat(${Probe}, U.Entry[${Slot}])`;
        }
    },

    //--------------------------------------------------------------------------------------------------
    //                                            TINT
    //--------------------------------------------------------------------------------------------------

    // WGSL: EvaluateStratumTint(Probe, Resistance, Dials) — x=Palette y=Saturation z=Mottling w=Varnish
    StratumTint:
    {
        Naming: "Stratum Tint", Family: "Tint", Glyph: "Tint", Yields: "Tint",
        Summary: "Munsell-derived palettes indexed by resistance — cement sets hue and strength alike",
        Intakes:
        [
            { Naming: "resist", Category: "Resistance", Optional: true },
            { Naming: "warp",   Category: "Warp",       Optional: true }
        ],
        Dials:
        [
            ["Palette",    "palette",    0.0, 3.0, 0.00, 0, "idx"],
            ["Saturation", "saturation", 0.0, 1.0, 1.00, 2, "-"],
            ["Mottling",   "mottling",   0.0, 1.0, 0.34, 2, "-"],
            ["Varnish",    "varnish",    0.0, 1.0, 0.42, 2, "-"]
        ],
        Transcribe: (Operands, Dials, Slot) =>
        {
            const Probe      = Operands.warp   || "Probe";
            const Resistance = Operands.resist || "0.5";
            return `EvaluateStratumTint(${Probe}, ${Resistance}, U.Entry[${Slot}])`;
        }
    },

    //--------------------------------------------------------------------------------------------------
    //                                           RESOLVE
    //--------------------------------------------------------------------------------------------------

    // 📝 The root. Emits nothing itself — the transcriber reads its operands and builds the three entry
    //    points around them. Exactly one per tree, enforced by Singular.
    SurfaceResolve:
    {
        Naming: "Surface Resolve", Family: "Resolve", Glyph: "Root", Yields: "Distance",
        Summary: "the root; the block seeds the grid, the weather chain shapes it, the tint colours it",
        Singular: true,
        Intakes:
        [
            // 📝 "mass" is now the SEED, not the finished shape. Renamed from "distance" deliberately:
            //    calling it distance invited the M1 reading that what arrives here is what renders.
            { Naming: "mass",    Category: "Distance" },
            { Naming: "resist",  Category: "Resistance", Optional: true },
            { Naming: "weather", Category: "Weather",    Optional: true },
            { Naming: "tint",    Category: "Tint",       Optional: true }
        ],
        Dials: [],
        Transcribe: () => ""
    }
};

// 📝 The weather chain, root-first, as an ordered list of process names. The host turns this into the
//    dispatch sequence for one simulated step.
//
//    🔴 Walks the "before" intake UPSTREAM from the root, then reverses. The chain is authored pointing
//       at the root, so the entry nearest the root is the LAST process to run — reading it forwards
//       would run the sequence backwards, which is a silent difference: it still erodes, it just erodes
//       into the wrong shape.
export function ComposeWeatherSequence(TreeState)
{
    const RootLink = TreeState.Links.find(Link =>
    {
        const Target = TreeState.Entries.get(Link.TargetEntry);
        return Target && Target.Species === "SurfaceResolve" && Link.TargetIntake === "weather";
    });
    if (!RootLink) return [];

    const Sequence = [];
    const Seen     = new Set();
    let Cursor     = RootLink.SourceEntry;

    while (Cursor !== undefined && !Seen.has(Cursor))
    {
        Seen.add(Cursor);
        const Entry = TreeState.Entries.get(Cursor);
        if (!Entry) break;

        const Specification = ConstructionSpecificationTable[Entry.Species];
        if (Specification && Specification.Process && !Entry.Bypassed)
        {
            Sequence.push({ Process: Specification.Process, Slot: Entry.Slot, Identifier: Entry.Identifier });
        }

        const Upstream = TreeState.Links.find(Link =>
            Link.TargetEntry === Cursor && Link.TargetIntake === "before");
        Cursor = Upstream ? Upstream.SourceEntry : undefined;
    }

    return Sequence.reverse();                                      // 🔴 see the hazard note above
}

// 📝 Palette names, for the interface to show the index as something readable.
export const TintPaletteNaming =
    ["Bryce Claron", "Entrada / Aztec", "Weathered granite", "Varnished canyon wall"];

export const RepeatAxisNaming = ["Eastward", "Northward", "Both"];

// 🔴 Four dial lanes per entry is the uniform layout's hard limit. Assert at load rather than letting a
//    fifth dial silently write into the next entry's slot.
for (const [Species, Specification] of Object.entries(ConstructionSpecificationTable))
{
    if (Specification.Dials.length > 4)
    {
        throw new Error(`${Species} declares ${Specification.Dials.length} dials; the uniform packs 4`);
    }
}
