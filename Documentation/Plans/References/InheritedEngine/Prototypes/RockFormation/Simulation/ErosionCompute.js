//========================================================================================================================
//                                                ErosionCompute.js                                                🧩
//========================================================================================================================
//
// 📝 The five weathering processes, as WGSL compute entry points over the voxel field.
//
//    This is the module that answers the M1 failure. M1 asked "what does an eroded rock look like?" and
//    answered with a formula. This asks "what does erosion DO?" and runs it. The difference shows up as
//    the absence of flat panels: a formula leaves untouched regions between its features, a process
//    touches every exposed cell every step.
//
//    🔴 EVERY entry point is GATHER-ONLY. A cell reads DensityRead at its own and its neighbours'
//       indices, and writes DensityWrite AT ITS OWN INDEX ONLY. WGSL has no f32 atomics, so a scatter
//       (writing to a neighbour) is an unsynchronised race — it will not fail validation, it will not
//       crash, it will produce a rock that looks fine and whose mass drifts. That is why
//       CheckMassConservation.py exists and why it is not optional.
//
//    📝 The shared shape of a gather-form transfer, used by thermal and hydraulic alike:
//
//        outgoing = how much THIS cell gives away, computed from this cell and its neighbours
//        incoming = sum over neighbours of how much EACH NEIGHBOUR gives to this cell
//        write      Mine - outgoing + incoming
//
//       Both halves are computed from the same read buffer with the same rule, so what one cell counts as
//       given, the receiving cell counts as taken. Mass conservation follows from the SYMMETRY of that
//       rule, not from any correction term. ⚠️ If the two halves use different expressions, mass drifts.

import { WorkgroupEdge, FieldProfileDeclaration, FieldAddressing } from "./VoxelField.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                    BINDINGS
//------------------------------------------------------------------------------------------------------------------------

const ComputeBindings = `
@group(0) @binding(0) var<uniform>             Field         : FieldProfile;
@group(0) @binding(1) var<storage, read>       DensityRead   : array<f32>;
@group(0) @binding(2) var<storage, read_write> DensityWrite  : array<f32>;
@group(0) @binding(3) var<storage, read>       Resistance    : array<f32>;
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                 SHARED HELPERS
//------------------------------------------------------------------------------------------------------------------------

const ErosionHelpers = `
// 📝 Exposure: how much of a cell's 6-neighbourhood is open. This is the single most important quantity
//    in the whole simulation, because it is what makes erosion a SURFACE process.
//
//    🔴 A BURIED CELL MUST RETURN EXACTLY 0.0, and getting that right is subtler than it looks.
//
//       The obvious form — sum of (1 - neighbour density) — is WRONG, and CheckMassConservation.py
//       caught it. Once weathering starts, a neighbour sits at 0.97 rather than 1.0, so a fully enclosed
//       cell scores a small positive exposure and erodes. Every buried cell in the body does this at
//       once, so the rock hollows out from within while its silhouette still looks correct, and there
//       is nothing on the surface to indicate why it eventually collapses.
//
//       The fix is a THRESHOLD, not a sum: a neighbour still counts as rock until it drops below the
//       isolevel the renderer treats as solid. Openness is then measured only across genuinely empty
//       neighbours, so an interior cell scores a hard zero however weathered its surroundings are.
//
//    ⚠️ SolidLevel here MUST match the isolevel GridMarch traces (0.5). If the sim considers a cell
//       solid that the renderer draws as empty, erosion stops one shell short of the visible surface.
fn EvaluateExposure(Cell : vec3i, Edge : i32) -> f32
{
    var Open = 0.0;
    Open = Open + OpennessToward(Cell + vec3i( 1, 0, 0), Edge);
    Open = Open + OpennessToward(Cell + vec3i(-1, 0, 0), Edge);
    Open = Open + OpennessToward(Cell + vec3i( 0, 1, 0), Edge);
    Open = Open + OpennessToward(Cell + vec3i( 0,-1, 0), Edge);
    Open = Open + OpennessToward(Cell + vec3i( 0, 0, 1), Edge);
    Open = Open + OpennessToward(Cell + vec3i( 0, 0,-1), Edge);
    return Open / 6.0;
}

// 📝 How open one neighbour is. Zero while it still counts as rock, then ramping to 1 as it empties —
//    a ramp rather than a step so the erosion rate does not jump discontinuously as a neighbour crosses
//    the isolevel, which would show as a visible terrace one cell wide.
fn OpennessToward(Cell : vec3i, Edge : i32) -> f32
{
    let Occupancy = ReadDensity(Cell, Edge);
    return clamp((0.5 - Occupancy) * 2.0, 0.0, 1.0);
}

// 📝 How much rock stands around a cell at a DISTANCE, as a fraction in [0,1].
//
//    🔴 This is not the same question as the 6-tap Laplacian, and conflating the two is what made salt
//       fretting a dead gate. A cell on the floor of an alcove has an ordinary-looking immediate
//       neighbourhood — it is a surface like any other. What distinguishes it is rock standing around
//       it two or three cells out. Shelter is a NEIGHBOURHOOD-SCALE property; curvature is a local one.
//
//    📝 Sampled on the 6 axes at radius 2 and 3 rather than a full 5x5x5 box: 12 taps instead of 124,
//       for a measurement that only needs to distinguish "open face" from "inside a pocket".
fn SurroundingOccupancy(Cell : vec3i, Edge : i32) -> f32
{
    var Total = 0.0;
    for (var Reach = 2; Reach <= 3; Reach = Reach + 1)
    {
        Total = Total + ReadDensity(Cell + vec3i( Reach, 0, 0), Edge);
        Total = Total + ReadDensity(Cell + vec3i(-Reach, 0, 0), Edge);
        Total = Total + ReadDensity(Cell + vec3i( 0, Reach, 0), Edge);
        Total = Total + ReadDensity(Cell + vec3i( 0,-Reach, 0), Edge);
        Total = Total + ReadDensity(Cell + vec3i( 0, 0, Reach), Edge);
        Total = Total + ReadDensity(Cell + vec3i( 0, 0,-Reach), Edge);
    }
    return Total / 12.0;
}

// 📝 Central-difference gradient of density. Points from empty INTO rock, so the outward surface normal
//    is its negation. Used for aeolian incidence and for cavity detection.
fn EvaluateFieldGradient(Cell : vec3i, Edge : i32) -> vec3f
{
    return vec3f(
        ReadDensity(Cell + vec3i(1, 0, 0), Edge) - ReadDensity(Cell - vec3i(1, 0, 0), Edge),
        ReadDensity(Cell + vec3i(0, 1, 0), Edge) - ReadDensity(Cell - vec3i(0, 1, 0), Edge),
        ReadDensity(Cell + vec3i(0, 0, 1), Edge) - ReadDensity(Cell - vec3i(0, 0, 1), Edge)) * 0.5;
}

// 📝 Susceptibility: erosion rate is INVERSELY proportional to resistance. This one coupling is the whole
//    mechanism the user asked for — "some part of rock harder than others". Hard rock does not need a
//    special rule to protrude; it protrudes because it and the soft rock beside it were exposed for the
//    same length of time and wore at different rates.
//
//    ⚠️ Clamped away from zero. Resistance 0 would be an infinite rate, which removes a cell in one step
//       and produces a sharp-edged hole rather than a hollow.
fn EvaluateSusceptibility(Cell : vec3i, Edge : i32) -> f32
{
    return 1.0 / max(ReadResistance(Cell, Edge), 0.08);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                  ① THERMAL
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 Thermal weathering / talus. Musgrave-style: material on a slope steeper than the angle of repose
//    detaches and slides downhill, piling at the foot. This is what builds the debris apron under a cliff
//    and what rounds a sharp corner into a shoulder.
//
//    🔴 THIS PROCESS CONSERVES MASS EXACTLY. Nothing is destroyed — material is relocated. That makes it
//       the sharpest available test of the gather form, because any asymmetry between the give and take
//       halves shows as a total that drifts. CheckMassConservation.py asserts it to within f32 rounding.
//
//    📝 Repose: talus stands at 33-37 deg for angular rock debris. As a slope expressed over one cell,
//       tan(35 deg) ~= 0.70. ⚠️ Self-tuned to the cell metric rather than measured in this geometry.
//
//    📝 The local is named Mine, not Self: `Self` is a WGSL RESERVED KEYWORD and the module will not
//       parse. Caught by the compile probe, invisible to every JS-side check.

const ThermalWeathering = `
@compute @workgroup_size(${WorkgroupEdge}, ${WorkgroupEdge}, ${WorkgroupEdge})
fn DriveThermalCollapse(@builtin(global_invocation_id) Invocation : vec3u)
{
    let Edge = i32(Field.Edge);
    let Cell = vec3i(Invocation);
    if (any(Cell >= vec3i(Edge))) { return; }

    let Ordinal = CellToOrdinal(Cell, Edge);
    let Mine    = DensityRead[Ordinal];

    // ① How much THIS cell gives away, split over the lower neighbours it can slide to.
    var Outgoing = 0.0;
    // ② How much this cell RECEIVES, by asking each neighbour the same question in reverse.
    var Incoming = 0.0;

    let Repose = Field.ReposeSlope;
    let Rate   = Field.StepDelta * Field.GravityDrop;

    for (var Axis = 0; Axis < 6; Axis = Axis + 1)
    {
        var Step = vec3i(0);
        switch Axis
        {
            case 0: { Step = vec3i( 1, 0, 0); }
            case 1: { Step = vec3i(-1, 0, 0); }
            case 2: { Step = vec3i( 0, 1, 0); }
            case 3: { Step = vec3i( 0,-1, 0); }
            case 4: { Step = vec3i( 0, 0, 1); }
            default: { Step = vec3i( 0, 0,-1); }
        }

        let Neighbour = Cell + Step;
        let Other     = ReadDensity(Neighbour, Edge);

        // 📝 Gravity bias: a downward step (Step.y < 0) is favoured, an upward step resisted. Without it
        //    thermal collapse becomes isotropic diffusion and the rock melts into a ball instead of
        //    shedding talus downhill.
        let DownBias = select(select(1.0, 1.6, Step.y < 0), 0.25, Step.y > 0);

        // ③ Give: this cell sheds to a lower neighbour when the difference exceeds repose.
        let Fall = (Mine - Other) * DownBias;
        if (Fall > Repose) { Outgoing = Outgoing + (Fall - Repose) * Rate; }

        // ④ Take: the SAME rule evaluated from the neighbour's side, with the step reversed. Symmetry
        //    here is what conserves mass. 🔴 Do not "simplify" this to reuse Fall — the neighbour's bias
        //    is computed from -Step, and collapsing the two makes downhill flow bidirectional.
        let BackBias = select(select(1.0, 1.6, Step.y > 0), 0.25, Step.y < 0);
        let Rise     = (Other - Mine) * BackBias;
        if (Rise > Repose) { Incoming = Incoming + (Rise - Repose) * Rate; }
    }

    // 🔴 Cap the outflow at what the cell actually holds. Without this a steep cell gives away more than
    //    it has, goes negative, and the neighbours that received it gain mass from nowhere.
    Outgoing = min(Outgoing, Mine);

    DensityWrite[Ordinal] = clamp(Mine - Outgoing + Incoming, 0.0, 1.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                 ② HYDRAULIC
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 Water carving channels. Flow accumulates downhill, and abrasion goes as flow divided by resistance,
//    so a channel that starts as a chance hollow deepens because it collects more water than the ridge
//    beside it — positive feedback is what makes drainage look drainage-like rather than like scratches.
//
//    ⚠️ Unlike thermal, this LOSES mass: abraded material is carried out of the domain in suspension.
//       The check is therefore monotone decrease, not conservation.
//
//    📝 Flow is approximated from the local surface, not routed globally. A true flow-accumulation pass
//       needs a sort or a multi-pass scan; the local proxy is that a cell with rock above it and space
//       below it is on a drainage line. This reads correctly at this scale and costs one dispatch.

const HydraulicErosion = `
@compute @workgroup_size(${WorkgroupEdge}, ${WorkgroupEdge}, ${WorkgroupEdge})
fn DriveHydraulicCarve(@builtin(global_invocation_id) Invocation : vec3u)
{
    let Edge = i32(Field.Edge);
    let Cell = vec3i(Invocation);
    if (any(Cell >= vec3i(Edge))) { return; }

    let Ordinal = CellToOrdinal(Cell, Edge);
    let Mine    = DensityRead[Ordinal];
    if (Mine <= 0.0) { DensityWrite[Ordinal] = Mine; return; }

    let Exposure = EvaluateExposure(Cell, Edge);
    if (Exposure <= 0.0) { DensityWrite[Ordinal] = Mine; return; }

    // ① Catchment proxy: how much rock stands ABOVE this cell within a short column. More rock above
    //    means more water arriving here, so more abrasion. This is the feedback that cuts channels.
    var Catchment = 0.0;
    for (var Lift = 1; Lift <= 6; Lift = Lift + 1)
    {
        Catchment = Catchment + ReadDensity(Cell + vec3i(0, Lift, 0), Edge) / f32(Lift);
    }

    // ② Water runs off, so an OVERHANGING face sheds and a ledge collects. Upward-facing surfaces take
    //    the most; the gradient's y component tells them apart.
    let Gradient = EvaluateFieldGradient(Cell, Edge);
    let Facing   = clamp(-Gradient.y / (length(Gradient) + 1e-4), -1.0, 1.0);
    let Wetted   = clamp(0.35 + 0.65 * Facing, 0.0, 1.0);

    // ③ Abrade. Rate goes as flow / resistance -- differential hardness expressed through time.
    let Removed = Field.StepDelta * 0.55
                * Exposure
                * (0.30 + Catchment * 0.42)
                * Wetted
                * EvaluateSusceptibility(Cell, Edge);

    DensityWrite[Ordinal] = clamp(Mine - Removed, 0.0, 1.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                  ③ AEOLIAN
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 Wind-driven sand abrasion. Two signatures make this recognisable rather than generic:
//
//      ① it is DIRECTIONAL — only faces turned into the wind are cut, so a rock develops a windward
//         flank and a sheltered lee;
//      ② it is strongest NEAR THE GROUND — saltating sand hops within roughly the lowest metre, which is
//         what undercuts a boulder into a mushroom rather than sanding it evenly.

const AeolianAbrasion = `
@compute @workgroup_size(${WorkgroupEdge}, ${WorkgroupEdge}, ${WorkgroupEdge})
fn DriveAeolianAbrasion(@builtin(global_invocation_id) Invocation : vec3u)
{
    let Edge = i32(Field.Edge);
    let Cell = vec3i(Invocation);
    if (any(Cell >= vec3i(Edge))) { return; }

    let Ordinal = CellToOrdinal(Cell, Edge);
    let Mine    = DensityRead[Ordinal];
    if (Mine <= 0.0) { DensityWrite[Ordinal] = Mine; return; }

    let Exposure = EvaluateExposure(Cell, Edge);
    if (Exposure <= 0.0) { DensityWrite[Ordinal] = Mine; return; }

    // ① Outward normal is the NEGATED gradient -- the gradient climbs into the rock.
    let Gradient = EvaluateFieldGradient(Cell, Edge);
    let Outward  = -Gradient / (length(Gradient) + 1e-4);

    let Wind = vec3f(sin(Field.WindBearing), 0.0, cos(Field.WindBearing));

    // ② Only windward faces are cut. dot > 0 means the face looks into the wind.
    let Incidence = clamp(dot(Outward, Wind), 0.0, 1.0);

    // ③ Saltation profile: sand hops low. Height is measured from the domain floor, and the falloff
    //    reaches ~0 by a fifth of the domain height. 🔴 Without this the wind planes the rock evenly and
    //    the result is a smoothed blob with no undercut.
    let Position   = CellToPosition(Cell, Field.Edge, Field.CellSize);
    let Height     = Position.y + Field.DomainRadius;
    let Saltation  = exp(-Height / max(Field.DomainRadius * 0.42, 1e-3));

    let Removed = Field.StepDelta * Field.WindStrength * 0.85
                * Exposure
                * Incidence
                * (0.18 + Saltation)
                * EvaluateSusceptibility(Cell, Edge);

    DensityWrite[Ordinal] = clamp(Mine - Removed, 0.0, 1.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                ④ SPHEROIDAL
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 Spheroidal weathering / corestones. Water penetrates along joints and attacks a block from every
//    face at once; a corner is attacked from three faces, an edge from two, a face from one. The block
//    therefore rounds from the outside in, leaving a spherical corestone in a shell of weathered rind.
//
//    📝 This is CURVATURE-DRIVEN, which is exactly what M1's Voronoi could not do: it removes most where
//       the surface is most convex, so sharp corners round and the cube stops reading as a cube.
//       Mean curvature is the Laplacian of density — a discrete 6-point stencil.

const SpheroidalRounding = `
@compute @workgroup_size(${WorkgroupEdge}, ${WorkgroupEdge}, ${WorkgroupEdge})
fn DriveSpheroidalRounding(@builtin(global_invocation_id) Invocation : vec3u)
{
    let Edge = i32(Field.Edge);
    let Cell = vec3i(Invocation);
    if (any(Cell >= vec3i(Edge))) { return; }

    let Ordinal = CellToOrdinal(Cell, Edge);
    let Mine    = DensityRead[Ordinal];
    if (Mine <= 0.0) { DensityWrite[Ordinal] = Mine; return; }

    // 🔴 GATE ON EXPOSURE, exactly as the abrading processes do.
    //
    //    This gate was missing, and CheckMassConservation.py caught it: a FULLY ENCLOSED cell at density
    //    1.0, whose six neighbours are all solid but slightly under 1.0 because they have started to
    //    weather, registers a negative Laplacian and erodes. Nothing about that cell is a convex corner
    //    — it is buried — but the curvature term cannot tell the difference on its own.
    //
    //    ⚠️ Left in, this dissolves the rock FROM WITHIN while the silhouette still looks correct: the
    //       body hollows out and thins until it collapses, with no visible cause on the surface. It is
    //       the same "erosion is not a surface process" failure the header warns about, arriving through
    //       curvature rather than through a bad rate.
    let Exposure = EvaluateExposure(Cell, Edge);
    if (Exposure <= 0.0) { DensityWrite[Ordinal] = Mine; return; }

    // ① Laplacian: the neighbourhood mean minus this cell. Negative on a convex bulge (a corner sticking
    //    out into space), positive in a concave notch.
    var Around = 0.0;
    Around = Around + ReadDensity(Cell + vec3i( 1, 0, 0), Edge);
    Around = Around + ReadDensity(Cell + vec3i(-1, 0, 0), Edge);
    Around = Around + ReadDensity(Cell + vec3i( 0, 1, 0), Edge);
    Around = Around + ReadDensity(Cell + vec3i( 0,-1, 0), Edge);
    Around = Around + ReadDensity(Cell + vec3i( 0, 0, 1), Edge);
    Around = Around + ReadDensity(Cell + vec3i( 0, 0,-1), Edge);

    let Curvature = Around / 6.0 - Mine;

    // ② Remove only where CONVEX. 🔴 Letting the concave case add material would turn this into plain
    //    diffusion, which fills the hollows the other processes just cut and erases all the relief.
    let Convex = max(-Curvature, 0.0);

    let Removed = Field.StepDelta * 0.75
                * Convex
                * Exposure
                * EvaluateSusceptibility(Cell, Edge);

    DensityWrite[Ordinal] = clamp(Mine - Removed, 0.0, 1.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                              ⑤ SALT / FREEZE
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 Tafoni: alcoves and honeycomb. Salt crystallising in pores, and ice wedging, prise grains loose —
//    and they do it FASTEST IN SHELTERED HOLLOWS, where moisture lingers and salt concentrates instead of
//    being washed off. That is the inverse of spheroidal rounding, and running the two together is what
//    produces the pitted, cavernous look of a real sandstone face.
//
//    🔴 This is a POSITIVE FEEDBACK: a hollow deepens because it is a hollow. Left unbounded it drills
//       needles through the rock, so the concavity term is clamped and gated on genuine exposure.

const SaltFreezeFret = `
@compute @workgroup_size(${WorkgroupEdge}, ${WorkgroupEdge}, ${WorkgroupEdge})
fn DriveSaltFreezeFret(@builtin(global_invocation_id) Invocation : vec3u)
{
    let Edge = i32(Field.Edge);
    let Cell = vec3i(Invocation);
    if (any(Cell >= vec3i(Edge))) { return; }

    let Ordinal = CellToOrdinal(Cell, Edge);
    let Mine    = DensityRead[Ordinal];
    if (Mine <= 0.0) { DensityWrite[Ordinal] = Mine; return; }

    let Exposure = EvaluateExposure(Cell, Edge);

    // 📝 Gate on a surface that is genuinely open but not a thin spur. A cell with almost every neighbour
    //    empty is a filament, and fretting it further just makes dust.
    if (Exposure <= 0.05 || Exposure > 0.72) { DensityWrite[Ordinal] = Mine; return; }

    // ① Shelter geometry, measured over a WIDE neighbourhood rather than the 6-tap Laplacian.
    //
    //    🔴 The Laplacian form was a DEAD GATE and CheckSaltFreeze.py proved it: on a grid where solid
    //       cells are 1.0, the mean-minus-self term is at best exactly 0 (every neighbour full) and
    //       negative otherwise, so the clamp to [0, 0.5] pinned it at 0 for every cell in the domain.
    //       The dial moved, the shader ran, and it removed precisely nothing — the worst kind of
    //       failure, because nothing anywhere reports it.
    //
    //    ⚠️ These process bodies live inside JS TEMPLATE LITERALS. A backtick in a comment ends the
    //       string and the module stops parsing — and because the emitted .wgsl is then simply STALE
    //       rather than absent, the compile probe happily re-validates the previous shader and reports
    //       success. Never use backticks in this file's WGSL comments.
    //
    //    📝 The error was geometric, not arithmetic. A cell on the floor of an alcove is not locally
    //       concave — its immediate neighbourhood looks like any other surface. What makes it sheltered
    //       is rock standing around it FURTHER OUT. So the measurement has to be taken at a radius, by
    //       asking how enclosed the cell is across a wider stencil than the surface it sits on.
    let Nearby = SurroundingOccupancy(Cell, Edge);

    // ② Enclosure: how much rock stands around this cell at a distance. A flat face sees about half its
    //    wide neighbourhood filled; the floor of a pocket sees considerably more.
    let Enclosure = clamp((Nearby - 0.45) * 2.6, 0.0, 1.0);

    // ③ Sheltered from rain: overhangs and undersides keep their salt. Upward faces get washed clean.
    let Gradient = EvaluateFieldGradient(Cell, Edge);
    let Facing   = clamp(-Gradient.y / (length(Gradient) + 1e-4), -1.0, 1.0);
    let Shelter  = clamp(0.55 - 0.45 * Facing, 0.0, 1.0);

    // ④ A pore-scale hash so the honeycomb has cells rather than being a smooth dish. This is what
    //    turns an even hollow into the pitted texture tafoni actually have.
    let Position = CellToPosition(Cell, Field.Edge, Field.CellSize);
    let Pore     = 0.55 + 0.45 * EvaluateValueNoise(Position * 9.0 + vec3f(Field.SeedTally));

    // 📝 A small floor under the enclosure term, so a flat face frets very slowly rather than not at
    //    all. Without it the process cannot NUCLEATE its own pockets — it can only deepen ones another
    //    process happened to open, which is what made it read as dead on a clean cube.
    let Susceptible = 0.12 + 0.88 * Enclosure;

    let Removed = Field.StepDelta * 1.30
                * Susceptible
                * Exposure
                * Shelter
                * Pore
                * EvaluateSusceptibility(Cell, Edge);

    DensityWrite[Ordinal] = clamp(Mine - Removed, 0.0, 1.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                  ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

// 📝 The process table. Order matters when they run in sequence within one step:
//
//      thermal first    — relocate what is already loose before cutting more
//      hydraulic        — cut channels into the settled surface
//      aeolian          — undercut the windward flank
//      spheroidal       — round what the cutting left sharp
//      saltfreeze last  — fret the hollows the others opened
//
//    ⚠️ Each is a SEPARATE dispatch with a parity flip between them, because a process must read a
//       consistent snapshot. Chaining two inside one dispatch would have half the grid reading pre-step
//       values and half post-step, according to workgroup scheduling order — nondeterministic and
//       untestable.
export const ErosionProcessTable =
[
    { Naming: "Thermal",    Entry: "DriveThermalCollapse",    Source: ThermalWeathering,
      Summary: "slope past repose collapses and piles below — conserves mass" },
    { Naming: "Hydraulic",  Entry: "DriveHydraulicCarve",     Source: HydraulicErosion,
      Summary: "water accumulates downhill and abrades in proportion to flow" },
    { Naming: "Aeolian",    Entry: "DriveAeolianAbrasion",    Source: AeolianAbrasion,
      Summary: "windward faces sand-blasted, strongest near the ground" },
    { Naming: "Spheroidal", Entry: "DriveSpheroidalRounding", Source: SpheroidalRounding,
      Summary: "convex corners round shell by shell toward a corestone" },
    { Naming: "SaltFreeze", Entry: "DriveSaltFreezeFret",     Source: SaltFreezeFret,
      Summary: "sheltered hollows deepen into alcoves and honeycomb" }
];

// 📝 One module holding all five entry points, plus the seed. Compiling once and selecting by entry point
//    costs a single createShaderModule instead of five, and guarantees they cannot drift apart in their
//    shared helpers.
//
//    ⚠️ Preamble must supply HashToUnit / EvaluateValueNoise / EvaluateFractalNoise and the resistance
//       builders — they are reused VERBATIM from the render's DistanceExpression preamble.
export function ComposeErosionSource(Preamble, SeedTranscription)
{
    return [
        Preamble,
        FieldProfileDeclaration,
        ComputeBindings,
        FieldAddressing,
        ErosionHelpers,
        SeedTranscription,
        ...ErosionProcessTable.map(Process => Process.Source)
    ].join("\n");
}
