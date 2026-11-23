//============================================================================================================================================
//                                                        ATMOSPHERESCATTERING.WGSL
//============================================================================================================================================
// 🧩 Single-scattering quadrature of a Rayleigh, Mie and ozone atmosphere into the sky-view radiance texture.

// 📝 Constants are Bruneton/Hillaire's Earth: R_g 6360 km, R_t 6460 km, Rayleigh scale height 8 km, Mie 1.2 km with
//    g = 0.8, an ozone tent at 25 km. The observer stands at ObserverAltitude. The texture is recomputed only when
//    the light moves; every escaped ray in the exhibit reads it through SampleSkyRadiance.

//! include "SkyProjection.wgsl"

struct AtmosphereRecord
{
    SunDirection     : vec3f,   // [-]  unit, toward the sun
    SolarIlluminance : f32,     // [-]  top-of-atmosphere illuminance scale
    ObserverAltitude : f32,     // [m]
    Pad0             : f32,
    Pad1             : f32,
    Pad2             : f32,
}

@group(0) @binding(0) var<uniform> Atmosphere : AtmosphereRecord;
@group(0) @binding(1) var SkyViewTarget       : texture_storage_2d<rgba16float, write>;

const GroundRadius      : f32   = 6360000.0;                                  // [m]
const AtmosphereRadius  : f32   = 6460000.0;                                  // [m]
const RayleighScatter   : vec3f = vec3f(5.802e-6, 13.558e-6, 33.1e-6);       // [1/m]
const RayleighHeight    : f32   = 8000.0;                                     // [m]
const MieScatter        : f32   = 3.996e-6;                                   // [1/m]
const MieExtinction     : f32   = 4.40e-6;                                    // [1/m]
const MieHeight         : f32   = 1200.0;                                     // [m]
const MieAsymmetry      : f32   = 0.8;                                        // [-]
const OzoneAbsorption   : vec3f = vec3f(0.650e-6, 1.881e-6, 0.085e-6);       // [1/m]
const ViewSamples       : u32   = 40u;
const LightSamples      : u32   = 12u;

// 📐 Ray–sphere intersection about the planet centre; returns (near, far), far < 0 when the sphere is missed.
fn IntersectSphere(Origin: vec3f, Direction: vec3f, Radius: f32) -> vec2f
{
    let b = dot(Origin, Direction);
    let c = dot(Origin, Origin) - Radius * Radius;
    let Discriminant = b * b - c;
    if (Discriminant < 0.0)
    {
        return vec2f(-1.0, -1.0);
    }
    let Root = sqrt(Discriminant);
    return vec2f(-b - Root, -b + Root);
}

fn ResolveDensities(Altitude: f32) -> vec3f
{
    let Ozone = max(0.0, 1.0 - abs(Altitude - 25000.0) / 15000.0);
    return vec3f(exp(-Altitude / RayleighHeight), exp(-Altitude / MieHeight), Ozone);
}

fn ResolveExtinction(Densities: vec3f) -> vec3f
{
    return RayleighScatter * Densities.x + vec3f(MieExtinction * Densities.y) + OzoneAbsorption * Densities.z;
}

fn ResolveSunTransmittance(Position: vec3f) -> vec3f
{
    if (IntersectSphere(Position, Atmosphere.SunDirection, GroundRadius).x > 0.0)
    {
        return vec3f(0.0);
    }
    let Exit = IntersectSphere(Position, Atmosphere.SunDirection, AtmosphereRadius).y;
    let Step = Exit / f32(LightSamples);
    var Depth = vec3f(0.0);
    for (var Sample = 0u; Sample < LightSamples; Sample++)
    {
        let Point = Position + Atmosphere.SunDirection * ((f32(Sample) + 0.5) * Step);
        Depth += ResolveExtinction(ResolveDensities(length(Point) - GroundRadius)) * Step;
    }
    return exp(-Depth);
}

@compute @workgroup_size(8, 8)
fn IntegrateSkyView(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Extent = textureDimensions(SkyViewTarget);
    if (any(Invocation.xy >= Extent))
    {
        return;
    }
    let Coordinates = (vec2f(Invocation.xy) + 0.5) / vec2f(Extent);
    var Direction = ReconstructSkyDirection(Coordinates);
    // 📝 Below the horizon the view is lifted onto it: the scene's own ground answers those rays first.
    Direction.z = max(Direction.z, 0.0);
    Direction = normalize(Direction + vec3f(0.0, 0.0, 1.0e-4));
    let Origin = vec3f(0.0, 0.0, GroundRadius + Atmosphere.ObserverAltitude);

    let Ground = IntersectSphere(Origin, Direction, GroundRadius);
    var Travel = IntersectSphere(Origin, Direction, AtmosphereRadius).y;
    if (Ground.x > 0.0)
    {
        Travel = Ground.x;
    }
    let Step = Travel / f32(ViewSamples);
    let Cosine = dot(Direction, Atmosphere.SunDirection);
    let RayleighPhase = 3.0 / (16.0 * 3.14159265358979) * (1.0 + Cosine * Cosine);
    let g2 = MieAsymmetry * MieAsymmetry;
    let MiePhase = 3.0 / (8.0 * 3.14159265358979) * ((1.0 - g2) * (1.0 + Cosine * Cosine))
                 / ((2.0 + g2) * pow(1.0 + g2 - 2.0 * MieAsymmetry * Cosine, 1.5));

    var Transmittance = vec3f(1.0);
    var Radiance = vec3f(0.0);
    for (var Sample = 0u; Sample < ViewSamples; Sample++)
    {
        let Point = Origin + Direction * ((f32(Sample) + 0.5) * Step);
        let Densities = ResolveDensities(length(Point) - GroundRadius);
        let Extinction = ResolveExtinction(Densities);
        let Scattering = RayleighScatter * Densities.x * RayleighPhase + vec3f(MieScatter * Densities.y * MiePhase);
        let SegmentTransmittance = exp(-Extinction * Step);
        // 📐 Energy-conserving segment integral: ∫ T(s)·S ds = S·(1 − T_seg)/σ_t, times the transmittance so far.
        let Integral = (vec3f(1.0) - SegmentTransmittance) / max(Extinction, vec3f(1.0e-12));
        Radiance += Transmittance * Scattering * ResolveSunTransmittance(Point) * Integral;
        Transmittance *= SegmentTransmittance;
    }
    textureStore(SkyViewTarget, vec2i(Invocation.xy), vec4f(Radiance * Atmosphere.SolarIlluminance, 1.0));
}
