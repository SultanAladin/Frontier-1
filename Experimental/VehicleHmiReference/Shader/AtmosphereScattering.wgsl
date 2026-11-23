// Hillaire-style isotropic multiple-scattering closure + quadratic sky integration.
//! include "SkyProjection.wgsl"
//! include "NativeTwilight.wgsl"
//! include "AtmosphereMedium.wgsl"
@group(0) @binding(1) var SkyViewTarget : texture_storage_2d<rgba16float,write>;
@group(0) @binding(2) var TransmittanceLut : texture_2d<f32>;
@group(0) @binding(3) var MultipleLut : texture_2d<f32>;
@group(0) @binding(4) var LutSampler : sampler;
@group(0) @binding(5) var SkyTransmissionTarget : texture_storage_2d<rgba16float,write>;
@compute @workgroup_size(8, 8)
fn IntegrateSkyView(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Extent = textureDimensions(SkyViewTarget);
    if (any(Invocation.xy >= Extent))
    {
        return;
    }
    if(Atmosphere.Heights.w<0.5){textureStore(SkyViewTarget,vec2i(Invocation.xy),vec4f(0.0));textureStore(SkyTransmissionTarget,vec2i(Invocation.xy),vec4f(1.0));return;}
    let Samples=u32(clamp(Atmosphere.Ground.w,8.0,64.0));
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

    let Cosine = dot(Direction, Atmosphere.SunDirection);
    let RayleighPhase = 3.0 / (16.0 * 3.14159265358979) * (1.0 + Cosine * Cosine);
    let g = Atmosphere.Medium.w;
    let g2 = g * g;
    let MiePhase = 3.0 / (8.0 * 3.14159265358979) * ((1.0 - g2) * (1.0 + Cosine * Cosine))
                 / ((2.0 + g2) * pow(1.0 + g2 - 2.0 * g * Cosine, 1.5));

    var Transmittance = vec3f(1.0);
    var Radiance = vec3f(0.0);
    for (var Sample = 0u; Sample < Samples; Sample++)
    {
        let A = f32(Sample) / f32(Samples);
        let B = f32(Sample + 1u) / f32(Samples);
        let Step = Travel * (B * B - A * A);
        let Point = Origin + Direction * (Travel * 0.5 * (A * A + B * B));
        let Densities = ResolveDensities(length(Point) - GroundRadius);
        let Extinction = ResolveExtinction(Densities);
        let Scattering = RayleighScatter * Atmosphere.Medium.x * Densities.x * RayleighPhase + vec3f(MieScatter * Atmosphere.Medium.y * Densities.y * MiePhase);
        let SegmentTransmittance = exp(-Extinction * Step);
        // 📐 Energy-conserving segment integral: ∫ T(s)·S ds = S·(1 − T_seg)/σ_t, times the transmittance so far.
        let Integral = (vec3f(1.0) - SegmentTransmittance) / max(Extinction, vec3f(1.0e-12));
        let Direct = Scattering * LookupTransmittance(TransmittanceLut, LutSampler, Point, Atmosphere.SunDirection);
        let SigmaS = RayleighScatter * Atmosphere.Medium.x * Densities.x + vec3f(MieScatter * Atmosphere.Medium.y * Densities.y);
        let Multiple = SigmaS * LookupMultiple(MultipleLut,LutSampler,Point,Atmosphere.SunDirection) * Atmosphere.Heights.z;
        Radiance += Transmittance * (Direct + Multiple) * Integral;
        Transmittance *= SegmentTransmittance;
    }
    textureStore(SkyTransmissionTarget, vec2i(Invocation.xy), vec4f(Transmittance, 1.0));
    textureStore(SkyViewTarget, vec2i(Invocation.xy), vec4f(Radiance * Atmosphere.SolarIlluminance * Atmosphere.SolarTint.rgb + NativeTwilight(Direction,Atmosphere.SunDirection,Atmosphere.Pad0), 1.0));
}
