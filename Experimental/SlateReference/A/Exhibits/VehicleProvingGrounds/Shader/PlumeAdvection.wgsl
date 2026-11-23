//============================================================================================================================================
//                                                           PLUMEADVECTION.WGSL
//============================================================================================================================================
// 🧩 Semi-Lagrangian transport of both plume lattices, obstruction by the scene distance field, combustion, sources, decay and coverage.

// 📝 AdvectVelocity (coarse), AdvectThermo (fine) and ResolveSunTransmittance (coarse) share one layout; the host
//    binds, per step parity,
//      AdvectVelocity:           1 = velocity state,  2 = thermo state,  4 = next velocity
//      AdvectThermo:             1 = final velocity,  2 = thermo state,  4 = next thermo
//      ResolveSunTransmittance:  1 = final velocity,  2 = next thermo,   4 = light
//    A cell the ONE scene distance field calls solid — the ground plane, a wall, a container, the moving ControlVehicle
//    — holds no gas, and a velocity cell inside a moving occupant takes that occupant's point velocity, so the car's
//    body drags and parts the plume. Frontier gathers the same obstruction through a separate intake
//    (GasSceneDistanceIntake.h); here it is one sample of the field the GI already traces.

//! include "PlumeRecords.wgsl"
//! include "SceneRecords.wgsl"
//! include "DistanceTraversal.wgsl"

@group(0) @binding(0) var<uniform> Plume                    : PlumeRecord;
@group(0) @binding(1) var VelocityState                     : texture_3d<f32>;
@group(0) @binding(2) var ThermoState                       : texture_3d<f32>;
@group(0) @binding(3) var LatticeSampler                    : sampler;
@group(0) @binding(4) var LatticeTarget                     : texture_storage_3d<rgba16float, write>;
@group(0) @binding(5) var<storage, read> Sources            : array<PlumeSource>;
@group(0) @binding(6) var<storage, read_write> Coverage     : array<atomic<i32>, 8>;
@group(0) @binding(7) var<storage, read> Occupants          : array<OccupantRecord>;

var<workgroup> CoverageLow  : array<atomic<i32>, 3>;
var<workgroup> CoverageHigh : array<atomic<i32>, 3>;

//------------------------------------------------------------------------------------------------------------------------
//                                                     OBSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Returns (velocity, 1) inside geometry and (0, 0) in open air. A velocity cell is solid once its centre lies inside
//    a surface, a thermo cell once its centre is a quarter cell inside, so the first layer of gas above the road and
//    around a tyre survives and a wall still holds no smoke. The velocity reach was a quarter cell outside until the
//    flame seats moved beside the tyres: at 0.5 m cells the cell beside a sidewall counted as solid, its velocity was
//    zero, and a fire pooled under the arch instead of rising. The point velocity of a moving occupant is the
//    displacement of the material point now at Position since the previous frame's transform, over that frame.
const VelocitySolidReach : f32 = 0.0;     // [cells]
const ThermoSolidReach   : f32 = -0.25;   // [cells]

// 📝 Sub-grid buoyant rise: a hot thread of gas or a flame tongue rises faster than the coarse lattice resolves, so the
//    thermo backtrace adds a slip toward +Z that saturates with the cell's heat and flame.
const SubgridHeatRise  : f32 = 1.4;    // [m/s] at 40 K and above
const SubgridFlameRise : f32 = 1.1;    // [m/s] at unit flame and above

fn ResolveSolid(Position: vec3f, Edge: f32) -> vec4f
{
    let Sampled = SampleSceneDistance(Position);
    if (Sampled.x >= VelocitySolidReach * Edge)
    {
        return vec4f(0.0);
    }
    var Velocity = vec3f(0.0);
    let Level = u32(Sampled.y);
    if (Level < Distance.ClipCount && Plume.SourceMinimum.w > 0.0)
    {
        let Identity = ResolveClipIdentity(Level, Position);
        if (Identity < arrayLength(&Occupants))
        {
            let Occupant = Occupants[Identity];
            if ((Occupant.Roles & RoleDynamic) != 0u)
            {
                let Prior = (Occupant.PreviousLocalToWorld * (Occupant.WorldToLocal * vec4f(Position, 1.0))).xyz;
                Velocity = (Position - Prior) / Plume.SourceMinimum.w;
                let Speed = length(Velocity);
                Velocity = select(Velocity, Velocity * (PlumeSpeedCeiling / Speed), Speed > PlumeSpeedCeiling);
            }
        }
    }
    return vec4f(Velocity, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  VELOCITY ADVECTION
//------------------------------------------------------------------------------------------------------------------------

@compute @workgroup_size(4, 4, 4)
fn AdvectVelocity(@builtin(global_invocation_id) Invocation: vec3u)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
    {
        return;
    }
    let Edge     = Plume.Lattice.y;
    let Seconds  = Plume.Lattice.w;
    let Position = ProjectVelocityWorld(Plume, Invocation);
    let Texel    = ResolveVelocityTexel(Plume, vec3i(Invocation));
    let Solid    = ResolveSolid(Position, Edge);
    if (Solid.w > 0.5)
    {
        textureStore(LatticeTarget, Texel, Solid);
        return;
    }

    // ① self-advection; a departure point outside the previous window brings the ambient wind
    let Wind = Plume.Wind.xyz;
    var Velocity = Wind;
    if (ClassifyPriorVelocity(Plume, Position))
    {
        let Own = textureSampleLevel(VelocityState, LatticeSampler, ProjectVelocityCoordinates(Plume, Position), 0.0).xyz;
        let Departure = Position - Own * Seconds;
        if (ClassifyPriorVelocity(Plume, Departure))
        {
            Velocity = textureSampleLevel(VelocityState, LatticeSampler, ProjectVelocityCoordinates(Plume, Departure), 0.0).xyz;
        }
    }

    // ② viscous relaxation toward the ambient flow, then buoyancy: hot gas rises, particulate sinks. The lift saturates
    //    with temperature — Frontier's heat scale puts a burning tyre near 75 K, which at 2.4 m/s²/K would fire the
    //    flame out of its cells at 180 m/s²; flames rise at metres per second, so the lift tends to 8× the coefficient.
    Velocity = Wind + (Velocity - Wind) * Plume.Retention.w;
    if (ClassifyPriorThermo(Plume, Position))
    {
        let Thermo = textureSampleLevel(ThermoState, LatticeSampler, ProjectThermoCoordinates(Plume, Position), 0.0);
        let Lift = Plume.Forces.x * Thermo.g / (1.0 + 0.125 * Thermo.g);
        Velocity.z += (Lift - Plume.Forces.y * (Thermo.r + Thermo.a)) * Seconds;
    }

    // ③ sources impart their momentum as a relaxation toward the source velocity, so a strong source cannot overshoot
    if (ClassifySourceBounds(Plume, Position))
    {
        for (var Index = 0u; Index < u32(Plume.PriorVelocityOrigin.w); Index++)
        {
            let Source = Sources[Index];
            let Weight = ResolveSourceWeight(Source, Position, Edge);
            let Push   = 1.0 - exp(-Weight * Seconds * (4.0 + 0.15 * (Source.Rates.x + Source.Rates.w) + 0.05 * Source.Rates.z));
            Velocity = mix(Velocity, Source.Velocity.xyz, Push);
        }
    }
    let Speed = length(Velocity);
    Velocity = select(Velocity, Velocity * (PlumeSpeedCeiling / Speed), Speed > PlumeSpeedCeiling);
    textureStore(LatticeTarget, Texel, vec4f(Velocity, 0.0));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THERMO ADVECTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 The coarse velocity plus divergence-free detail below the lattice: the curl of a world-anchored trigonometric
//    potential, each component omitting its own coordinate (Frontier's SmokeTransport.slang FineVelocity), now in two
//    octaves and scaled by how much is happening at the point — clean air stays still, a boiling plume churns.
fn SampleLatticeVelocity(Position: vec3f, Activity: f32) -> vec3f
{
    var Velocity = textureSampleLevel(VelocityState, LatticeSampler, ProjectVelocityCoordinates(Plume, Position), 0.0).xyz;
    let Time = Plume.Wind.w;
    let Frequency = Plume.Detail.z;
    let First = Position * Frequency + vec3f(0.43, -0.31, 0.27) * Time;
    let Second = Position * (Frequency * 2.37) + vec3f(-0.61, 0.53, -0.89) * Time + vec3f(1.7, 4.1, 2.9);
    let Swirl = vec3f(sin(First.y) - sin(First.z), sin(First.z) - sin(First.x), sin(First.x) - sin(First.y))
              + 0.45 * vec3f(sin(Second.y) - sin(Second.z), sin(Second.z) - sin(Second.x), sin(Second.x) - sin(Second.y));
    return Velocity + Plume.Forces.w * (0.3 + 0.7 * Activity) * Swirl;
}

@compute @workgroup_size(4, 4, 4)
fn AdvectThermo(@builtin(global_invocation_id) Invocation: vec3u, @builtin(local_invocation_index) Lane: u32)
{
    if (Lane < 3u)
    {
        atomicStore(&CoverageLow[Lane], 2147483647);
        atomicStore(&CoverageHigh[Lane], -2147483647);
    }
    workgroupBarrier();

    if (all(Invocation < Plume.ThermoExtent.xyz))
    {
        let Edge     = Plume.Lattice.x;
        let Seconds  = Plume.Lattice.w;
        let Position = ProjectThermoWorld(Plume, Invocation);
        var Thermo   = vec4f(0.0);
        if (SampleSceneDistance(Position).x >= ThermoSolidReach * Edge)
        {
            // ① second-order (midpoint) backtrace through the final velocity of this step
            var Activity = 0.0;
            var Rise = vec2f(0.0);
            if (ClassifyPriorThermo(Plume, Position))
            {
                let Here = textureSampleLevel(ThermoState, LatticeSampler, ProjectThermoCoordinates(Plume, Position), 0.0);
                Activity = clamp(0.6 * (Here.r + Here.a) + 0.02 * Here.g + Here.b, 0.0, 1.0);
                Rise = clamp(vec2f(Here.g / 40.0, Here.b), vec2f(0.0), vec2f(1.0));
            }
            let Slip      = vec3f(0.0, 0.0, SubgridHeatRise * Rise.x + SubgridFlameRise * Rise.y);
            let Midpoint  = Position - (SampleLatticeVelocity(Position, Activity) + Slip) * (0.5 * Seconds);
            let Departure = Position - (SampleLatticeVelocity(Midpoint, Activity) + Slip) * Seconds;
            if (ClassifyPriorThermo(Plume, Departure))
            {
                Thermo = textureSampleLevel(ThermoState, LatticeSampler, ProjectThermoCoordinates(Plume, Departure), 0.0);
            }

            // ② combustion: a flame cell keeps heating its gas and sheds soot as it burns — a tyre fire's black column
            let Burning = max(Thermo.b, 0.0) * Seconds;
            Thermo.g += Burning * Plume.Combustion.y;
            Thermo.a += Burning * Plume.Combustion.x;

            // ③ per-second losses, converted for this step by the host
            Thermo = vec4f(Thermo.r * Plume.Retention.x, Thermo.g * Plume.Retention.y, Thermo.b * Plume.Retention.z, Thermo.a * Plume.Combustion.z);

            // ④ sources: smoke, heat, flame and soot per second at each Gaussian centre
            if (ClassifySourceBounds(Plume, Position))
            {
                for (var Index = 0u; Index < u32(Plume.PriorVelocityOrigin.w); Index++)
                {
                    let Source = Sources[Index];
                    Thermo += Source.Rates * (ResolveSourceWeight(Source, Position, Edge) * Seconds);
                }
            }
            Thermo = clamp(Thermo, vec4f(0.0), vec4f(64.0, 4096.0, 16.0, 64.0));
        }
        textureStore(LatticeTarget, ResolveThermoTexel(Plume, vec3i(Invocation)), Thermo);

        // ⑤ coverage: the march only enters the box of cells that hold anything visible
        if (Thermo.r > 0.002 || Thermo.a > 0.002 || Thermo.b > 0.01)
        {
            let Local = vec3i(Invocation);
            for (var Axis = 0u; Axis < 3u; Axis++)
            {
                atomicMin(&CoverageLow[Axis], Local[Axis]);
                atomicMax(&CoverageHigh[Axis], Local[Axis]);
            }
        }
    }

    workgroupBarrier();
    if (Lane < 3u)
    {
        let Low  = atomicLoad(&CoverageLow[Lane]);
        let High = atomicLoad(&CoverageHigh[Lane]);
        if (Low <= High)
        {
            atomicMin(&Coverage[Lane], Low);
            atomicMax(&Coverage[4u + Lane], High);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SUN TRANSMITTANCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Transmittance through the plume once per coarse cell per step, toward the sun (r) and toward the zenith (g):
//    eight steps each that lengthen with distance through the new thermo lattice. The march then reads both with one
//    filtered sample per point — sunlight self-shadowing, and the sky occluded by the smoke above, which is what
//    darkens the belly of a thick column.
fn ResolveOpticalDepth(Position: vec3f, Toward: vec3f) -> f32
{
    var Depth = 0.0;
    var Travel = 0.25 * Plume.Lattice.y;
    for (var Step = 0u; Step < 8u; Step++)
    {
        let Length = Plume.Lattice.x * (1.0 + 0.6 * f32(Step));
        let Sample = Position + Toward * (Travel + 0.5 * Length);
        if (!ClassifyWindowInterior(Sample, vec3i(2 * Plume.VelocityOrigin.xy, 0), Plume.ThermoExtent.xyz, Plume.Lattice.x, Plume.Lattice.z))
        {
            break;
        }
        let Thermo = max(textureSampleLevel(ThermoState, LatticeSampler, ProjectThermoCoordinates(Plume, Sample), 0.0), vec4f(0.0));
        Depth += (Thermo.r * Plume.Optical.x + Thermo.a * Plume.Optical.z) * Length;
        Travel += Length;
    }
    return Depth;
}

@compute @workgroup_size(4, 4, 4)
fn ResolveSunTransmittance(@builtin(global_invocation_id) Invocation: vec3u)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
    {
        return;
    }
    let Position = ProjectVelocityWorld(Plume, Invocation);
    let Sun = exp(-ResolveOpticalDepth(Position, Plume.Sun.xyz) * 0.65);
    let Zenith = exp(-ResolveOpticalDepth(Position, vec3f(0.0, 0.0, 1.0)) * 0.5);
    textureStore(LatticeTarget, ResolveVelocityTexel(Plume, vec3i(Invocation)), vec4f(Sun, Zenith, 0.0, 1.0));
}
