//============================================================================================================================================
//                                                            PLUMEPRESSURE.WGSL
//============================================================================================================================================
// 🧩 Pressure projection of the coarse velocity: divergence, warm-started Jacobi sweeps, gradient subtraction.

// 📝 Solves ∇²q = ∇·u and replaces u with u − ∇q, the divergence-free part. Boundary conditions per neighbour:
//    a solid cell answers with the centre's q (no flow through the wall — Neumann); a neighbour outside the window
//    sideways or above is open air at q = 0 (Dirichlet); below the floor is ground (Neumann). q persists between
//    steps at toroidal texel addresses, so each step starts from the last solution of the same world cells — a few
//    sweeps then do the work of many (Frontier's CoarseGasField.h starts every step from zero on the CPU). A cell the
//    window has only just covered restarts from zero.
//
//    Bindings per pass: ResolveDivergence 1 → 2 (and clears 3, 4 where newly covered); RelaxPressure 1, 2, 3 → 4;
//    ProjectVelocity 1, 3 → 5.

//! include "PlumeRecords.wgsl"

@group(0) @binding(0) var<uniform> Plume                       : PlumeRecord;
@group(0) @binding(1) var VelocityState                        : texture_3d<f32>;
@group(0) @binding(2) var<storage, read_write> Divergence      : array<f32>;
@group(0) @binding(3) var<storage, read_write> PressureSource  : array<f32>;
@group(0) @binding(4) var<storage, read_write> PressureTarget  : array<f32>;
@group(0) @binding(5) var LatticeTarget                        : texture_storage_3d<rgba16float, write>;

fn ClassifyInside(Local: vec3i) -> bool
{
    return all(Local >= vec3i(0)) && all(Local < vec3i(Plume.VelocityExtent.xyz));
}

fn LoadVelocity(Local: vec3i) -> vec4f
{
    let Clamped = clamp(Local, vec3i(0), vec3i(Plume.VelocityExtent.xyz) - vec3i(1));
    return textureLoad(VelocityState, ResolveVelocityTexel(Plume, Clamped), 0);
}

fn ResolveLocalIndex(Local: vec3i) -> u32
{
    let Extent = vec3i(Plume.VelocityExtent.xyz);
    return u32((Local.z * Extent.y + Local.y) * Extent.x + Local.x);
}

fn ResolvePressureIndex(Local: vec3i) -> u32
{
    let Extent = vec3i(Plume.VelocityExtent.xyz);
    let Texel = ResolveVelocityTexel(Plume, Local);
    return u32((Texel.z * Extent.y + Texel.y) * Extent.x + Texel.x);
}

// 📝 q at a neighbour under the boundary rules above; Centre is the cell's own q.
fn ResolveNeighbourPressure(Local: vec3i, Centre: f32) -> f32
{
    if (Local.z < 0)
    {
        return Centre;
    }
    if (!ClassifyInside(Local))
    {
        return 0.0;
    }
    if (LoadVelocity(Local).w > 0.5)
    {
        return Centre;
    }
    return PressureSource[ResolvePressureIndex(Local)];
}

@compute @workgroup_size(4, 4, 4)
fn ResolveDivergence(@builtin(global_invocation_id) Invocation: vec3u)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
    {
        return;
    }
    let Local = vec3i(Invocation);
    let Own = LoadVelocity(Local);
    let Scale = 0.5 / Plume.Lattice.y;
    let Flux = (LoadVelocity(Local + vec3i(1, 0, 0)).x - LoadVelocity(Local - vec3i(1, 0, 0)).x)
             + (LoadVelocity(Local + vec3i(0, 1, 0)).y - LoadVelocity(Local - vec3i(0, 1, 0)).y)
             + (LoadVelocity(Local + vec3i(0, 0, 1)).z - LoadVelocity(Local - vec3i(0, 0, 1)).z);
    Divergence[ResolveLocalIndex(Local)] = select(Flux * Scale, 0.0, Own.w > 0.5);
    let Position = ProjectVelocityWorld(Plume, Invocation);
    if (!ClassifyWindowInterior(Position, Plume.PriorVelocityOrigin.xyz, Plume.VelocityExtent.xyz, Plume.Lattice.y, Plume.Lattice.z))
    {
        let Index = ResolvePressureIndex(Local);
        PressureSource[Index] = 0.0;
        PressureTarget[Index] = 0.0;
    }
}

@compute @workgroup_size(4, 4, 4)
fn RelaxPressure(@builtin(global_invocation_id) Invocation: vec3u)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
    {
        return;
    }
    let Local = vec3i(Invocation);
    let Index = ResolvePressureIndex(Local);
    let Centre = PressureSource[Index];
    if (LoadVelocity(Local).w > 0.5)
    {
        PressureTarget[Index] = Centre;
        return;
    }
    let Sum = ResolveNeighbourPressure(Local + vec3i(1, 0, 0), Centre) + ResolveNeighbourPressure(Local - vec3i(1, 0, 0), Centre)
            + ResolveNeighbourPressure(Local + vec3i(0, 1, 0), Centre) + ResolveNeighbourPressure(Local - vec3i(0, 1, 0), Centre)
            + ResolveNeighbourPressure(Local + vec3i(0, 0, 1), Centre) + ResolveNeighbourPressure(Local - vec3i(0, 0, 1), Centre);
    let Edge = Plume.Lattice.y;
    PressureTarget[Index] = (Sum - Divergence[ResolveLocalIndex(Local)] * Edge * Edge) / 6.0;
}

@compute @workgroup_size(4, 4, 4)
fn ProjectVelocity(@builtin(global_invocation_id) Invocation: vec3u)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
    {
        return;
    }
    let Local = vec3i(Invocation);
    let Own = LoadVelocity(Local);
    if (Own.w > 0.5)
    {
        textureStore(LatticeTarget, ResolveVelocityTexel(Plume, Local), Own);
        return;
    }
    let Centre = PressureSource[ResolvePressureIndex(Local)];
    let Scale = 0.5 / Plume.Lattice.y;
    let Gradient = vec3f(ResolveNeighbourPressure(Local + vec3i(1, 0, 0), Centre) - ResolveNeighbourPressure(Local - vec3i(1, 0, 0), Centre),
                         ResolveNeighbourPressure(Local + vec3i(0, 1, 0), Centre) - ResolveNeighbourPressure(Local - vec3i(0, 1, 0), Centre),
                         ResolveNeighbourPressure(Local + vec3i(0, 0, 1), Centre) - ResolveNeighbourPressure(Local - vec3i(0, 0, 1), Centre)) * Scale;
    var Velocity = Own.xyz - Gradient;
    let Speed = length(Velocity);
    Velocity = select(Velocity, Velocity * (PlumeSpeedCeiling / Speed), Speed > PlumeSpeedCeiling);
    textureStore(LatticeTarget, ResolveVelocityTexel(Plume, Local), vec4f(Velocity, 0.0));
}
