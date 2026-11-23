//========================================================================================================================
//                                                   SeedTree.js                                                   🧩
//========================================================================================================================
//
// 📝 The tree the prototype opens with: AN IRREGULAR HULL, A HOLE THROUGH IT, AND WEATHER.
//
//    🔴 The starting shape must be irregular but not INTERESTING, and the distinction is the whole lesson of
//       M1. M1 opened on an analytically-sculpted fin-with-an-arch: the interesting geometry was authored
//       rather than earned, and it showed, because the surface between the authored features was flat.
//
//       The first M2 seed over-corrected to a plain cube. That proved the sim worked — every rounded corner
//       was visibly earned — but it wasted the erosion budget on undoing the cube. A hull is the middle
//       ground: irregular from the first frame, with no landform authored into it. Nothing here is a mesa,
//       an arch, or a hoodoo; those still have to be weathered out.
//
//        DomainWarp ──> RidgedRelief ──> HullMass ──> HullRelief ──┐
//                                                                  ├─> MassSubtract ──> SurfaceResolve.mass
//                                              TunnelMass ─────────┘                          │
//                                                                                             │
//        StratumBand ──> JointNetwork ───────────────────────────────────────────────────────>┤ .resist
//                             └──────────────────────> StratumTint ──────────────────────────>┤ .tint
//                                                                                             │
//        Thermal ─> Hydraulic ─> Aeolian ─> Spheroidal ─> SaltFreeze ────────────────────────>┘ .weather
//
//    ⚠️ The weather chain reads LEFT TO RIGHT as the order the processes run, but the LINKS point
//       rightward into the next entry's "before" intake. ComposeWeatherSequence walks upstream from the
//       root and reverses, so the visual order and the execution order agree.

import { InsertEntry, InsertLink } from "./TreeState.js";
import { ConstructionSpecificationTable } from "./ConstructionSpecifications.js";

export function ComposeSeedTree(TreeState)
{
    // ① Noise first — a little domain warp so the block does not seed as a perfect machined cube.
    // ⚠️ Named Ridge, not Relief. HullRelief below is a different entry entirely — the displacement stack —
    //    and two locals a letter apart, both feeding "mass"-adjacent intakes, is a wiring mistake waiting to
    //    happen: linking the wrong one still transcribes, still compiles, and silently produces a body with
    //    no ridged warp on it.
    const Warp  = InsertEntry(TreeState, "DomainWarp",   { x: -960, y:  -60 });
    const Ridge = InsertEntry(TreeState, "RidgedRelief", { x: -730, y:   30 });

    // ② The resistance tree, kept entirely separate from the shape branch. This is what decides which
    //    parts of the block survive — the "some part of rock harder than others" mechanism.
    const Band   = InsertEntry(TreeState, "StratumBand",    { x: -960, y:  360 });
    const Joints = InsertEntry(TreeState, "JointNetwork",   { x: -730, y:  430 });

    // ③ The starting body: an irregular faceted hull, roughened, with a tunnel subtracted to open an arch.
    //
    // 📝 HullMass replaces the plain BlockMass the first M2 seed opened with. A cube was the right thing to
    //    start from while proving the erosion sim worked — it made it obvious that every rounded corner was
    //    earned by weather rather than authored. But a cube is also a poor STARTING body: real rock does not
    //    begin as a machined solid, and the weather has to spend its whole budget undoing the cube before it
    //    can do anything interesting. Beginning from a hull that is already irregular means the erosion
    //    budget goes into weathering rather than into de-cubing.
    const Hull     = InsertEntry(TreeState, "HullMass",     { x: -470, y:  -60 });
    const Relief   = InsertEntry(TreeState, "HullRelief",   { x: -470, y:  130 });
    const Tunnel   = InsertEntry(TreeState, "TunnelMass",   { x: -470, y:  320 });
    const Subtract = InsertEntry(TreeState, "MassSubtract", { x: -210, y:  130 });

    // ④ The weather chain — the part that actually makes the rock.
    const Thermal    = InsertEntry(TreeState, "ThermalWeather",    { x: -470, y:  620 });
    const Hydraulic  = InsertEntry(TreeState, "HydraulicWeather",  { x: -250, y:  620 });
    const Aeolian    = InsertEntry(TreeState, "AeolianWeather",    { x:  -30, y:  620 });
    const Spheroidal = InsertEntry(TreeState, "SpheroidalWeather", { x:  190, y:  620 });
    const SaltFreeze = InsertEntry(TreeState, "SaltFreezeWeather", { x:  410, y:  620 });

    // ⑤ Colour from the same resistance field that shaped the rock.
    const Tint = InsertEntry(TreeState, "StratumTint", { x: 190, y: 400 });

    // ⑥ The root.
    const Root = InsertEntry(TreeState, "SurfaceResolve", { x: 680, y: 280 });

    // -------------------------------------------------------------- noise chain
    InsertLink(TreeState, Warp,    Ridge, "warp");
    InsertLink(TreeState, Ridge,   Hull,  "warp");

    // -------------------------------------------------------------- resistance chain
    InsertLink(TreeState, Band,    Joints, "beneath");

    // -------------------------------------------------------------- shape chain
    //
    // 📝 Hull -> Relief -> Subtract. The displacement sits BETWEEN the hull and the boolean so the tunnel
    //    cuts a clean bore through an already-roughened body. Reversed — subtract then displace — the
    //    displacement would rough the cut walls too, and an arch's underside would look weathered before a
    //    single erosion step had run, which is exactly the authored-not-earned failure M1 died of.
    InsertLink(TreeState, Hull,    Relief,   "mass");
    InsertLink(TreeState, Relief,  Subtract, "mass");
    InsertLink(TreeState, Tunnel,  Subtract, "carver");

    // -------------------------------------------------------------- weather chain
    InsertLink(TreeState, Thermal,    Hydraulic,  "before");
    InsertLink(TreeState, Hydraulic,  Aeolian,    "before");
    InsertLink(TreeState, Aeolian,    Spheroidal, "before");
    InsertLink(TreeState, Spheroidal, SaltFreeze, "before");

    // -------------------------------------------------------------- colour + root
    InsertLink(TreeState, Joints,     Tint, "resist");
    InsertLink(TreeState, Subtract,   Root, "mass");
    InsertLink(TreeState, Joints,     Root, "resist");
    InsertLink(TreeState, SaltFreeze, Root, "weather");
    InsertLink(TreeState, Tint,       Root, "tint");

    // ⑦ Seed-specific dial overrides.
    //
    // 📝 A body wider than it is tall, so the tunnel leaves a span with legs either side rather than a ring.
    //    Height below Extent is what flattens it — the addon gets its Sandstone preset the same way.
    //
    //    🔴 Both in METRES against the 2.6 m domain half-extent (DeviceHost.js DomainRadius), and sized to
    //       nearly fill it. The hull's first version used a dimensionless extent of 1.0 and a GPU probe
    //       measured the body at 1.58% grid occupancy — a pebble floating in a mostly-empty grid, with the
    //       tunnel far too large to bore it. Undersizing here wastes most of the voxel budget on empty space,
    //       which costs resolution everywhere and shows up as a coarse, stair-stepped surface.
    SetDial(TreeState, Hull,   "faces",     13.0);
    SetDial(TreeState, Hull,   "spread",     0.46);
    SetDial(TreeState, Hull,   "extent",     2.20);
    SetDial(TreeState, Hull,   "height",     1.70);

    // 📝 The addon's own defaults, scaled: deform 5.0 -> 0.5 internally, rough 2.5 -> 0.025. Held a little
    //    under those here because the weather adds its own relief on top and the two compound.
    SetDial(TreeState, Relief, "deform",     0.30);
    SetDial(TreeState, Relief, "rough",      0.09);
    SetDial(TreeState, Relief, "grain",      1.00);
    SetDial(TreeState, Relief, "detail",     1.00);

    SetDial(TreeState, Tunnel, "radius",     0.72);
    SetDial(TreeState, Tunnel, "span",       3.60);
    SetDial(TreeState, Tunnel, "elevation", -0.42);

    // 📝 A short warp. The hull is already irregular, so this only needs to break the fibonacci regularity
    //    of the plane directions — a large warp here just moves the starting surface without adding history.
    SetDial(TreeState, Warp,   "amplitude",  0.09);
    SetDial(TreeState, Ridge,  "amplitude",  0.12);

    return {
        Warp, Ridge, Band, Joints, Hull, Relief, Tunnel, Subtract,
        Thermal, Hydraulic, Aeolian, Spheroidal, SaltFreeze, Tint, Root
    };
}

// 📝 Set a dial by KEY rather than lane index, so a reordered Dials array cannot silently move a value
//    into the wrong lane.
//
//    🔴 Throws on an unknown key rather than returning false. A mistyped key in the seed above would
//       otherwise leave the species default in place and read as a tuning problem, not a typo.
export function SetDial(TreeState, Identifier, Key, Value)
{
    const Entry = TreeState.Entries.get(Identifier);
    if (!Entry) throw new Error(`SetDial: no entry ${Identifier}`);

    const Specification = ConstructionSpecificationTable[Entry.Species];
    const Lane = Specification.Dials.findIndex(Dial => Dial[1] === Key);
    if (Lane < 0)
    {
        throw new Error(`SetDial: ${Entry.Species} has no dial "${Key}"`);
    }

    // ① Clamp to the declared range, so a seed override cannot sit outside what the slider represents
    //    and then appear to jump the first time it is touched.
    const Minimum = Specification.Dials[Lane][2];
    const Maximum = Specification.Dials[Lane][3];
    Entry.Dials[Lane] = Math.min(Math.max(Value, Minimum), Maximum);
    return true;
}
