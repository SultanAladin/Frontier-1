//========================================================================================================================
//                                                   FieldSeed.js                                                  🧩
//========================================================================================================================
//
// 📝 The two dispatches that fill the grid before any erosion runs:
//
//      ① DriveMassSeed        analytic SDF -> fractional density
//      ② DriveResistanceSeed  strata + noise + joints -> the static hardness buffer
//
//    Both consume the transcribed tree, so the node editor still authors the STARTING BODY — a cube, or
//    a cube with booleans cut through it for an arch. What changed from M1 is what happens next: M1
//    rendered this directly and called it rock. Here it is only the block of stone before the weather
//    gets to it.
//
//    🔴 The seed writes to BOTH halves of the ping-pong pair, via two dispatches with the parity flipped
//       between. Seeding only one leaves the other holding the previous run's density, and the first
//       erosion step reads the stale half — which shows as the rock flickering between two shapes on
//       alternate steps, a symptom that looks like a race and is not one.

import { WorkgroupEdge, FieldProfileDeclaration } from "./VoxelField.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                  SDF -> DENSITY
//------------------------------------------------------------------------------------------------------------------------

export const FieldSeedSource = `

// 📝 Convert a signed distance to fractional occupancy. Inside is negative distance, so occupancy runs
//    from 1 deep inside to 0 well outside, crossing 0.5 exactly at the surface — which is the isolevel
//    GridMarch traces.
//
//    🔴 The transition MUST be spread over about a cell, not a step function. A hard step quantises the
//       starting body to whole cells, so a face that is not axis-aligned begins the sim as a staircase,
//       and every later process faithfully erodes the staircase into a permanent artefact.
//
//    ⚠️ Too wide a transition and the body starts as fog: nothing reaches density 1, the isosurface sits
//       inside a gradient rather than on a boundary, and thermal collapse immediately slumps the lot.
fn DistanceToOccupancy(Distance : f32, CellSize : f32) -> f32
{
    let Softness = CellSize * 0.85;
    return clamp(0.5 - Distance / (2.0 * Softness), 0.0, 1.0);
}

@compute @workgroup_size(${WorkgroupEdge}, ${WorkgroupEdge}, ${WorkgroupEdge})
fn DriveMassSeed(@builtin(global_invocation_id) Invocation : vec3u)
{
    let Edge = i32(Field.Edge);
    let Cell = vec3i(Invocation);
    if (any(Cell >= vec3i(Edge))) { return; }

    let Ordinal  = CellToOrdinal(Cell, Edge);
    let Position = CellToPosition(Cell, Field.Edge, Field.CellSize);

    let Distance = EvaluateConstructionTree(Position);
    var Occupancy = DistanceToOccupancy(Distance, Field.CellSize);

    // 🔴 Seal the domain boundary to empty. A body that touches the wall of the grid has no exposed face
    //    there, so erosion cannot reach it and the rock ends up with a flat, unweathered panel exactly
    //    where the grid happens to end — the M1 flat-wall failure reproduced by a different route.
    if (any(Cell <= vec3i(1)) || any(Cell >= vec3i(Edge - 2))) { Occupancy = 0.0; }

    DensityWrite[Ordinal] = Occupancy;
}

// 📝 Resistance: how hard the rock is here. Written ONCE, read for the whole run.
//
//    This is the field that answers "some part of rock harder than others". It is authored by the same
//    strata/joint/noise species M1 already had — they were always the most physically honest part of the
//    tree, they just had nothing to act on. Now erosion rate is 1/resistance, so they act on everything.
@compute @workgroup_size(${WorkgroupEdge}, ${WorkgroupEdge}, ${WorkgroupEdge})
fn DriveResistanceSeed(@builtin(global_invocation_id) Invocation : vec3u)
{
    let Edge = i32(Field.Edge);
    let Cell = vec3i(Invocation);
    if (any(Cell >= vec3i(Edge))) { return; }

    let Ordinal  = CellToOrdinal(Cell, Edge);
    let Position = CellToPosition(Cell, Field.Edge, Field.CellSize);

    // ⚠️ Clamped to a floor, not to zero. Susceptibility is 1/resistance, so a zero would be an infinite
    //    erosion rate — one step and the cell is gone, leaving a sharp pit rather than a hollow.
    ResistanceWrite[Ordinal] = clamp(EvaluateConstructionResistance(Position), 0.08, 1.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                  SEED BINDINGS
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 The seed needs resistance WRITABLE, which the erosion layout deliberately does not allow. So it gets
//    its own layout rather than loosening the erosion one — the read-only binding on resistance during
//    erosion is a guarantee worth keeping.

//    🔴 GROUP 1, not group 0. The seed also binds the render's DialProfile as U at group 0 binding 0,
//       because the transcribed tree reads U.Entry for every dial. Putting the field at group 0 too
//       collides on binding 0 and the module fails to parse — "Bindings for [1] conflict with other
//       resource", which names the field rather than the collision and is easy to misread.
//
//    📝 The same split is what GridMarch does (group 0 dials, group 1 field), so the two agree.
export const SeedBindings = `
@group(1) @binding(0) var<uniform>             Field           : FieldProfile;
@group(1) @binding(1) var<storage, read_write> DensityWrite    : array<f32>;
@group(1) @binding(2) var<storage, read_write> ResistanceWrite : array<f32>;
`;

// 📝 Addressing without the ReadDensity/ReadResistance helpers — the seed has no read bindings at all,
//    so including the shared FieldAddressing would reference storage buffers that do not exist here.
export const SeedAddressing = `
fn CellToOrdinal(Cell : vec3i, Edge : i32) -> i32
{
    return (Cell.z * Edge + Cell.y) * Edge + Cell.x;
}

fn CellToPosition(Cell : vec3i, Edge : f32, CellSize : f32) -> vec3f
{
    let Middle = (Edge - 1.0) * 0.5;
    return (vec3f(Cell) - vec3f(Middle)) * CellSize;
}
`;

export function ComposeSeedLayout(Device)
{
    return Device.createBindGroupLayout({
        label: "SeedLayout",
        entries:
        [
            { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } },
            { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } }
        ]
    });
}

// 📝 One bind group per ping-pong half, so the seed can fill both. See the hazard note in the header.
export function ComposeSeedBindings(Device, Layout, Field)
{
    return Field.DensityPair.map((Density, Index) => Device.createBindGroup({
        layout  : Layout,
        label   : `Seed${Index}`,
        entries :
        [
            { binding: 0, resource: { buffer: Field.Profile } },
            { binding: 1, resource: { buffer: Density } },
            { binding: 2, resource: { buffer: Field.Resistance } }
        ]
    }));
}

// 📝 Assemble the seed module: shared preamble (noise, primitives, booleans, strata, joints) + the
//    transcribed tree + the two entry points.
export function ComposeSeedSource(Preamble, DialDeclaration, Transcription)
{
    return [
        Preamble,
        DialDeclaration,
        FieldProfileDeclaration,
        SeedBindings,
        SeedAddressing,
        Transcription,
        FieldSeedSource
    ].join("\n");
}
