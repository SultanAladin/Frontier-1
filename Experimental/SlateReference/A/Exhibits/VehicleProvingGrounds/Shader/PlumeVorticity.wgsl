//============================================================================================================================================
//                                                           PLUMEVORTICITY.WGSL
//============================================================================================================================================
// 🧩 Vorticity confinement on the coarse velocity lattice: the curl, then a force along N × ω that restores lost swirl.

// 📝 Semi-Lagrangian transport smears small eddies away within a few steps. Confinement (Fedkiw, Stam & Jensen 2001)
//    measures ω = ∇ × u, finds N = ∇|ω| / ‖∇|ω|‖ pointing toward stronger rotation, and adds ε·h·(N × ω), which spins
//    the eddies that remain back up — the rolling billows of tyre smoke, at no cost in lattice resolution.
//    ResolveCurl reads the advected velocity (binding 1) and writes the curl; ConfineVorticity reads both and writes
//    the confined velocity (binding 3). Cells outside the window read as their own neighbour (zero gradient).

//! include "PlumeRecords.wgsl"

@group(0) @binding(0) var<uniform> Plume               : PlumeRecord;
@group(0) @binding(1) var VelocityState                : texture_3d<f32>;
@group(0) @binding(2) var<storage, read_write> Curl    : array<vec4f>;
@group(0) @binding(3) var LatticeTarget                : texture_storage_3d<rgba16float, write>;

fn LoadVelocity(Local: vec3i) -> vec4f
{
    let Clamped = clamp(Local, vec3i(0), vec3i(Plume.VelocityExtent.xyz) - vec3i(1));
    return textureLoad(VelocityState, ResolveVelocityTexel(Plume, Clamped), 0);
}

fn ResolveLocalIndex(Local: vec3i) -> u32
{
    let Extent = vec3i(Plume.VelocityExtent.xyz);
    let Clamped = clamp(Local, vec3i(0), Extent - vec3i(1));
    return u32((Clamped.z * Extent.y + Clamped.y) * Extent.x + Clamped.x);
}

@compute @workgroup_size(4, 4, 4)
fn ResolveCurl(@builtin(global_invocation_id) Invocation: vec3u)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
    {
        return;
    }
    let Local = vec3i(Invocation);
    let Scale = 0.5 / Plume.Lattice.y;
    let Xp = LoadVelocity(Local + vec3i(1, 0, 0)).xyz;
    let Xm = LoadVelocity(Local - vec3i(1, 0, 0)).xyz;
    let Yp = LoadVelocity(Local + vec3i(0, 1, 0)).xyz;
    let Ym = LoadVelocity(Local - vec3i(0, 1, 0)).xyz;
    let Zp = LoadVelocity(Local + vec3i(0, 0, 1)).xyz;
    let Zm = LoadVelocity(Local - vec3i(0, 0, 1)).xyz;
    let ω = vec3f((Yp.z - Ym.z) - (Zp.y - Zm.y), (Zp.x - Zm.x) - (Xp.z - Xm.z), (Xp.y - Xm.y) - (Yp.x - Ym.x)) * Scale;
    Curl[ResolveLocalIndex(Local)] = vec4f(ω, length(ω));
}

@compute @workgroup_size(4, 4, 4)
fn ConfineVorticity(@builtin(global_invocation_id) Invocation: vec3u)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
    {
        return;
    }
    let Local = vec3i(Invocation);
    let Own = LoadVelocity(Local);
    var Velocity = Own.xyz;
    if (Own.w < 0.5 && Plume.Forces.z > 0.0)
    {
        let Scale = 0.5 / Plume.Lattice.y;
        let η = vec3f(Curl[ResolveLocalIndex(Local + vec3i(1, 0, 0))].w - Curl[ResolveLocalIndex(Local - vec3i(1, 0, 0))].w,
                      Curl[ResolveLocalIndex(Local + vec3i(0, 1, 0))].w - Curl[ResolveLocalIndex(Local - vec3i(0, 1, 0))].w,
                      Curl[ResolveLocalIndex(Local + vec3i(0, 0, 1))].w - Curl[ResolveLocalIndex(Local - vec3i(0, 0, 1))].w) * Scale;
        let Length = length(η);
        if (Length > 1.0e-5)
        {
            let ω = Curl[ResolveLocalIndex(Local)].xyz;
            Velocity += Plume.Forces.z * Plume.Lattice.y * cross(η / Length, ω) * Plume.Lattice.w;
        }
    }
    textureStore(LatticeTarget, ResolveVelocityTexel(Plume, Local), vec4f(Velocity, Own.w));
}
