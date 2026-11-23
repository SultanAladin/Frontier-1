//============================================================================================================================================
//                                                             PLUMEMARCH.WGSL
//============================================================================================================================================
// 🧩 Scattering quadrature of the smoke-and-fire lattice at trace resolution: sun, sky, fire light, flame emission and transmittance.

// 📝 Per resampling pixel, the camera ray is clipped to the coverage box the transport wrote and to the opaque depth,
//    then marched at about one sample per cell from a per-pixel offset that rotates every frame, so the temporal
//    antialiasing integrates the quadrature noise away. What each sample receives:
//
//    | Term             | How                                                                                       |
//    |------------------|-------------------------------------------------------------------------------------------|
//    | sun              | cascaded shadow depth × the light lattice's transmittance, in three scattering octaves     |
//    |                  | (Wrenninge): each octave sees a thinner medium and a rounder phase, so dense smoke glows   |
//    |                  | through instead of turning grey                                                           |
//    | sky              | the sky view averaged, occluded by the zenith transmittance — a thick column has a dark   |
//    |                  | belly — plus a sun-lit ground bounce from below                                           |
//    | fire light       | the burning tyres and exhaust pops as point lights inside the volume: smoke above a fire  |
//    |                  | is lit orange from beneath                                                                |
//    | flame            | blackbody emission at a temperature between tip and core set by the cell's heat,          |
//    |                  | flickering with the detail noise                                                          |
//
//    Optics mix per sample: rubber smoke scatters (albedo ≈ 0.9), soot absorbs (≈ 0.1). Sub-cell detail erodes the
//    trilinear blobs of the lattice with a tiling noise volume drifting with the wind and the rise of the plume.
//    The result is premultiplied radiance and transmittance, composited before antialiasing by the recombination.

//! include "FrameDescriptor.wgsl"
//! include "PlumeRecords.wgsl"
//! include "SkyProjection.wgsl"

struct CascadeReceiver
{
    LightViewProjection : array<mat4x4f, 3>,
    Splits              : vec4f,   // [m] xyz far distance of each cascade, w cascade count
    TexelWorld          : vec4f,   // [m] world size of one shadow texel per cascade
}

@group(0) @binding(1) var<uniform> Plume                : PlumeRecord;
@group(0) @binding(2) var ThermoState                   : texture_3d<f32>;
@group(0) @binding(3) var LatticeSampler                : sampler;
@group(0) @binding(4) var SurfaceDepth                  : texture_depth_2d;
@group(0) @binding(5) var<storage, read> Coverage       : array<i32, 8>;
@group(0) @binding(6) var ShadowDepth                   : texture_depth_2d_array;
@group(0) @binding(7) var ShadowSampler                 : sampler_comparison;
@group(0) @binding(8) var<uniform> Receiver             : CascadeReceiver;
@group(0) @binding(9) var SkyView                       : texture_2d<f32>;
@group(0) @binding(10) var SkySampler                   : sampler;
@group(0) @binding(11) var PlumeTarget                  : texture_storage_2d<rgba16float, write>;
@group(0) @binding(12) var LightState                   : texture_3d<f32>;
@group(0) @binding(13) var<uniform> FireLights          : FireLightRecord;
@group(0) @binding(14) var DetailState                  : texture_3d<f32>;
@group(0) @binding(15) var DetailSampler                : sampler;

fn SampleThermo(Position: vec3f) -> vec4f
{
    return max(textureSampleLevel(ThermoState, LatticeSampler, ProjectThermoCoordinates(Plume, Position), 0.0), vec4f(0.0));
}

// 📐 Henyey–Greenstein, normalised over the sphere.
fn ResolveHenyeyGreenstein(Cosine: f32, Anisotropy: f32) -> f32
{
    let Squared = Anisotropy * Anisotropy;
    return (1.0 - Squared) / (12.56637061436 * pow(max(1.0 + Squared - 2.0 * Anisotropy * Cosine, 1.0e-4), 1.5));
}

// 📐 Frontier's smoke phase generalised: a forward lobe (g) mixed 72 % with a backward one (−0.65 g), which is what
//    gives a back-lit plume its silver lining. Frontier's own pair is g = 0.38, backward −0.25.
fn ResolvePhase(Cosine: f32, Anisotropy: f32) -> f32
{
    return mix(ResolveHenyeyGreenstein(Cosine, -0.65 * Anisotropy), ResolveHenyeyGreenstein(Cosine, Anisotropy), 0.72);
}

// 📝 One hardware-filtered comparison in the cascade that holds the sample; beyond the last cascade the sun is seen.
fn ResolveSunVisibility(Position: vec3f) -> f32
{
    let ViewDistance = ResolveViewDepth(Position);
    let Count = u32(Receiver.Splits.w);
    if (Count == 0u || ViewDistance > Receiver.Splits[Count - 1u])
    {
        return 1.0;
    }
    var Cascade = 0u;
    while (Cascade + 1u < Count && ViewDistance > Receiver.Splits[Cascade])
    {
        Cascade++;
    }
    let Clip = Receiver.LightViewProjection[Cascade] * vec4f(Position, 1.0);
    let Ndc = Clip.xyz / Clip.w;
    let Coordinates = vec2f(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5);
    if (any(Coordinates < vec2f(0.0)) || any(Coordinates > vec2f(1.0)) || Ndc.z > 1.0)
    {
        return 1.0;
    }
    return textureSampleCompareLevel(ShadowDepth, ShadowSampler, Coordinates, Cascade, Ndc.z);
}

// 📝 Transmittance toward the sun (x) and the zenith (y) through the plume, resolved once per coarse cell by the
//    transport (PlumeAdvection.wgsl ResolveSunTransmittance) and read here with one filtered sample.
fn ResolveSelfShadow(Position: vec3f) -> vec2f
{
    return clamp(textureSampleLevel(LightState, LatticeSampler, ProjectVelocityCoordinates(Plume, Position), 0.0).xy, vec2f(0.0), vec2f(1.0));
}

// 📝 Isotropic sky term: the zenith and four horizon directions of the sky view.
fn ResolveSkyAmbient() -> vec3f
{
    var Sky = textureSampleLevel(SkyView, SkySampler, ProjectSkyCoordinates(vec3f(0.0, 0.0, 1.0)), 0.0).rgb * 2.0;
    for (var Side = 0u; Side < 4u; Side++)
    {
        let θ = f32(Side) * 1.5707963;
        Sky += textureSampleLevel(SkyView, SkySampler, ProjectSkyCoordinates(normalize(vec3f(cos(θ), sin(θ), 0.15))), 0.0).rgb;
    }
    return (Sky / 6.0) * Frame.SkyScale + Frame.NightSkyRadiance;
}

// 📝 The burning tyres and exhaust pops as softened point lights: irradiance over 4π, the phase taken as isotropic.
fn ResolveFireLight(Position: vec3f) -> vec3f
{
    var Light = vec3f(0.0);
    for (var Index = 0u; Index < min(FireLights.Count.x, 4u); Index++)
    {
        let Offset = FireLights.PositionRadius[Index].xyz - Position;
        let Radius = FireLights.PositionRadius[Index].w;
        Light += FireLights.Radiance[Index].rgb / (dot(Offset, Offset) + Radius * Radius);
    }
    return Light * (0.0795774715 * Plume.Scattering.w);
}

// 📐 Sub-cell detail: three noise octaves (one per channel) folded into a value around 0.5, drifting downwind and
//    upward with the plume so the detail reads as motion below the lattice. Erosion takes density away where the
//    noise is low, more of it where the cell is thin — edges turn to wisps while a dense core keeps its mass.
fn ResolveDetail(Position: vec3f) -> vec4f
{
    let Time = Plume.Wind.w;
    let Drift = vec3f(-Plume.Wind.x * 0.35, -Plume.Wind.y * 0.35, -0.55) * Time;
    let Coordinates = (Position + Drift) * Plume.Detail.y;
    return textureSampleLevel(DetailState, DetailSampler, Coordinates, 0.0);
}

fn ErodeDensity(Density: f32, Noise: f32) -> f32
{
    let Strength = Plume.Detail.x;
    let Thin = 1.0 - clamp(Density * 0.8, 0.0, 1.0);
    return max(0.0, Density * (1.0 + Strength * (Noise - 0.5) * 1.6) - Strength * Thin * (1.0 - Noise) * 0.35 * min(Density, 1.0));
}

@compute @workgroup_size(8, 8)
fn MarchPlume(@builtin(global_invocation_id) Invocation: vec3u)
{
    let TracePixel = vec2i(Invocation.xy);
    if (any(vec2f(TracePixel) >= Frame.TraceExtent))
    {
        return;
    }
    let Low  = vec3i(Coverage[0], Coverage[1], Coverage[2]);
    let High = vec3i(Coverage[4], Coverage[5], Coverage[6]);
    if (any(Low > High))
    {
        textureStore(PlumeTarget, TracePixel, vec4f(0.0, 0.0, 0.0, 1.0));
        return;
    }

    // ① the ray, clipped to the coverage box (grown by a cell) and to the opaque surface
    let Edge = Plume.Lattice.x;
    let Origin = vec3f(vec2f(2 * Plume.VelocityOrigin.xy) * Edge, Plume.Lattice.z);
    let BoxMinimum = Origin + (vec3f(Low) - 1.0) * Edge;
    let BoxMaximum = Origin + (vec3f(High) + 2.0) * Edge;
    let ScreenPixel = min(TracePixel * i32(Frame.TraceScale), vec2i(Frame.ScreenExtent) - vec2i(1));
    let Depth = textureLoad(SurfaceDepth, ScreenPixel, 0);
    let Far = ReconstructWorldPosition(vec2f(ScreenPixel) + 0.5, max(Depth, 1.0e-6));
    let Toward = Far - Frame.CameraPosition;
    let Reach = length(Toward);
    let Direction = Toward / Reach;
    let Opaque = select(Reach, 1.0e5, Depth <= 0.0);
    let Inverse = 1.0 / select(Direction, vec3f(1.0e-9), abs(Direction) < vec3f(1.0e-9));
    let Near = (BoxMinimum - Frame.CameraPosition) * Inverse;
    let Distant = (BoxMaximum - Frame.CameraPosition) * Inverse;
    let Enter = max(max(min(Near.x, Distant.x), min(Near.y, Distant.y)), max(min(Near.z, Distant.z), 0.0));
    let Exit  = min(min(max(Near.x, Distant.x), max(Near.y, Distant.y)), min(max(Near.z, Distant.z), Opaque));
    if (Exit <= Enter)
    {
        textureStore(PlumeTarget, TracePixel, vec4f(0.0, 0.0, 0.0, 1.0));
        return;
    }

    // ② about one sample per cell, from an interleaved-gradient offset rotated by the golden ratio each frame
    let Count = u32(clamp(ceil((Exit - Enter) / (0.9 * Edge)), 4.0, max(Plume.Optical.w, 4.0)));
    let Step = (Exit - Enter) / f32(Count);
    let Offset = fract(52.9829189 * fract(0.06711056 * f32(TracePixel.x) + 0.00583715 * f32(TracePixel.y))
                       + 0.61803398875 * f32(Frame.FrameIndex % 256u));
    let Cosine = dot(Direction, Frame.SunDirection);
    let Anisotropy = Plume.Scattering.y;
    let PhaseOctaves = vec3f(ResolvePhase(Cosine, Anisotropy), ResolvePhase(Cosine, 0.5 * Anisotropy), ResolvePhase(Cosine, 0.25 * Anisotropy));
    let SkyAmbient = ResolveSkyAmbient() * Plume.Scattering.z;
    let GroundBounce = Frame.SunIlluminance * (max(Frame.SunDirection.z, 0.0) * 0.12 * InversePi * Plume.Scattering.z);
    let Cells = Plume.ThermoExtent.w == 1u;
    let Strength = Plume.Scattering.x;
    let Flicker = 0.5 + 0.5 * sin(Plume.Wind.w * 23.0);

    var Radiance = vec3f(0.0);
    var Transmittance = 1.0;
    for (var Index = 0u; Index < Count; Index++)
    {
        if (Transmittance < 0.01)
        {
            break;
        }
        let Position = Frame.CameraPosition + Direction * (Enter + (f32(Index) + Offset) * Step);
        var Thermo = SampleThermo(Position);
        if (Thermo.r < 1.0e-3 && Thermo.a < 1.0e-3 && Thermo.b < 1.0e-3)
        {
            continue;
        }
        if (Cells)
        {
            // 🔍 the lattice itself: smoke cyan, soot violet, heat red, flame yellow, unlit
            let Absorbed = 1.0 - exp(-(Thermo.r * 1.5 + Thermo.a * 1.5 + Thermo.b) * Step);
            let Colour = vec3f(0.35, 0.75, 1.0) * min(Thermo.r, 2.0) + vec3f(0.55, 0.3, 0.9) * min(Thermo.a, 2.0)
                       + vec3f(1.0, 0.25, 0.05) * min(Thermo.g * 0.02, 2.0) + vec3f(1.0, 0.85, 0.2) * min(Thermo.b, 2.0);
            Radiance += Transmittance * Colour * Absorbed * 4.0;
            Transmittance *= 1.0 - Absorbed;
            continue;
        }

        // ③ sub-cell detail on both particulates; the flame flickers with the noise's fourth channel
        if (Plume.Detail.x > 0.0)
        {
            let Noise = ResolveDetail(Position);
            let Folded = dot(Noise.rgb, vec3f(0.5, 0.3, 0.2));
            Thermo.r = ErodeDensity(Thermo.r, Folded);
            Thermo.a = ErodeDensity(Thermo.a, Folded);
            Thermo.b *= mix(1.0, 0.45 + 1.1 * mix(Noise.a, Flicker, 0.25), min(Plume.Detail.x, 1.0));
        }

        // ④ optics of the mixture
        let SmokeExtinction = Thermo.r * Plume.Optical.x;
        let SootExtinction = Thermo.a * Plume.Optical.z;
        let Particulate = SmokeExtinction + SootExtinction;
        let Extinction = Particulate + Thermo.b * 0.8;
        if (Extinction < 1.0e-4)
        {
            continue;
        }
        let Absorbed = 1.0 - exp(-Extinction * Step);
        let Albedo = (SmokeExtinction * Plume.Optical.y + SootExtinction * Plume.Sun.w) / max(Particulate, 1.0e-5);
        let Share = Particulate / Extinction;

        // ⑤ sun in three octaves, sky occluded from above, fire light from within
        let Shadow = ResolveSelfShadow(Position);
        let Visibility = ResolveSunVisibility(Position);
        // 📝 Each further octave is light that scattered again inside the medium, so it is weighted by the albedo once
        //    more: white rubber smoke glows through, soot — which absorbs at every event — keeps only its single bounce.
        let Octaves = PhaseOctaves.x * Shadow.x + Strength * Albedo * (0.6 * PhaseOctaves.y * pow(Shadow.x, 0.25)
                                                                       + 0.36 * PhaseOctaves.z * pow(Shadow.x, 0.0625));
        let Sunlight = Frame.SunIlluminance * (Visibility * Octaves);
        let Sky = SkyAmbient * (0.18 + 0.82 * pow(Shadow.y, 0.6)) + GroundBounce;
        let Scattered = (Sunlight + Sky + ResolveFireLight(Position)) * (Albedo * Share * Absorbed);

        // ⑥ flame: blackbody between tip and core, brighter where the gas is hotter
        let Heat = Thermo.g / (Thermo.g + Plume.Flame.z);
        let Temperature = mix(Plume.Flame.y, Plume.Flame.x, Heat);
        let Emission = ResolveBlackbodyRadiance(Temperature) * (Plume.SourceMaximum.w * min(Thermo.b, 1.5) * Step);

        Radiance += Transmittance * (Scattered + Emission);
        Transmittance *= 1.0 - Absorbed;
    }
    textureStore(PlumeTarget, TracePixel, vec4f(Radiance, Transmittance));
}
