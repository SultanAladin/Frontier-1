//============================================================================================================================================
//                                                            CABINOVERLAY.WGSL
//============================================================================================================================================
// 🧩 What the deferred raster cannot carry: see-through glazing, the HMI's two screens and the mirrors' pictures, drawn over it.

// 📝 Drawn into the recombined radiance before the temporal antialiasing, depth-tested (read only) against the opaque
//    surface, premultiplied over what is there:
//
//    | Kind    | Geometry                                  | What it adds                                             |
//    |---------|-------------------------------------------|----------------------------------------------------------|
//    | glass   | the body's glazing facets                 | the sky's Fresnel reflection and a sun glint; lets 70 %   |
//    |         |                                           | of the cabin (or the world, from inside) through          |
//    | tablet  | the centre-stack screen, 1.5 mm proud      | the TabletPanel launcher (its canvas), with the Garage's  |
//    |         |                                           | live car composited into its preview well                |
//    | cluster | the binnacle's hexagon                     | the TabletPanel cluster (its canvas, alpha to the outline)|
//    | mirror  | each mirror's glass                        | the reflected view ForwardRaster rendered for it          |
//
//    The screens are self-lit (Luminance, in the scene's radiance units where sunlit white is about 5 — DriveCabin-
//    Layout's 3 and 3.5), so they read the same in a dark cabin and at noon; their cover glass still reflects the sky.
//    The derez mask applies here too: glass, screens and mirrors come apart with the car.

//! include "FrameDescriptor.wgsl"
//! include "SkyProjection.wgsl"
//! include "EffectRecords.wgsl"
//! include "VehicleVisual.wgsl"

struct OverlayRecord
{
    Tablet   : vec4f,   // x luminance, y brightness [0, 1], z garage preview shown (1), w unused
    Preview  : vec4f,   // xy top-left, zw bottom-right of the preview well in tablet UV
    Corner   : vec4f,   // xy the well's corner radius in UV, z cluster luminance, w unused
    Glass    : vec4f,   // x transmission at normal incidence, y mirror reflectance, z mirrors live (1), w unused
}

@group(0) @binding(1) var<storage, read> Occupants    : array<OccupantRecord>;
@group(0) @binding(2) var<uniform> Overlay            : OverlayRecord;
@group(0) @binding(3) var<uniform> Effect             : EffectRecord;
@group(0) @binding(4) var<storage, read> VehicleState : VehicleRecord;
@group(0) @binding(5) var TabletTexture               : texture_2d<f32>;
@group(0) @binding(6) var ClusterTexture              : texture_2d<f32>;
@group(0) @binding(7) var PreviewTexture              : texture_2d<f32>;
@group(0) @binding(8) var MirrorTexture               : texture_2d_array<f32>;
@group(0) @binding(9) var LinearSampler               : sampler;
@group(0) @binding(10) var SkyView                    : texture_2d<f32>;
@group(0) @binding(11) var SkySampler                 : sampler;

@group(0) @binding(12) var<storage, read> DisplayAdaptation : array<f32>;

const KindGlass   : u32 = 0u;
const KindTablet  : u32 = 1u;
const KindCluster : u32 = 2u;
const KindMirror  : u32 = 3u;

struct OverlayVarying
{
    @builtin(position) Position             : vec4f,
    @location(0) Normal                     : vec3f,
    @location(1) Coordinates                : vec2f,
    @location(2) WorldPosition              : vec3f,
    @location(3) BodyLocal                  : vec3f,
    @location(4) @interpolate(flat) Kind    : u32,
    @location(5) @interpolate(flat) Layer   : u32,
}

@vertex
fn OverlayVertex(@location(0) Position: vec3f, @location(1) Kind: u32, @location(2) Normal: vec3f, @location(3) Layer: f32,
                 @location(4) Coordinates: vec2f) -> OverlayVarying
{
    let Body = Occupants[0];
    // 📝 The two screens are cockpit fixtures: they scale and shift with it (the glazing and the mirrors stay with the
    //    shell). Screens and mirrors sit 1.5 mm proud of their cover facets so they win the depth test against them.
    var Placed = Position;
    if (Kind == KindTablet) { Placed = PlaceTablet(Position); }
    var TurnedNormal = Normal;
    if (Kind == KindCluster) {
        let Turned = TurnSteeringWheel(Position, Normal);
        Placed = Turned.Position;
        TurnedNormal = Turned.Normal;
    }
    let Lifted = Placed + TurnedNormal * select(0.0015, 0.0, Kind == KindGlass);
    let World = Body.LocalToWorld * vec4f(Lifted, 1.0);
    var Varying: OverlayVarying;
    Varying.Position = Frame.ViewProjection * World;
    Varying.Normal = (Body.LocalToWorld * vec4f(TurnedNormal, 0.0)).xyz;
    Varying.Coordinates = Coordinates;
    Varying.WorldPosition = World.xyz;
    Varying.BodyLocal = Lifted;
    Varying.Kind = Kind;
    Varying.Layer = u32(Layer + 0.5);
    return Varying;
}

fn ClassifyMasked(BodyLocal: vec3f) -> bool
{
    if (Effect.MaskMode == 0u)
    {
        return false;
    }
    if (Effect.MaskMode == 3u)
    {
        return true;
    }
    let Unit = (BodyLocal - Effect.BodyMinimum.xyz) / max(Effect.BodyMaximum.xyz - Effect.BodyMinimum.xyz, vec3f(1.0e-3));
    let Elapsed = Effect.Clock - Effect.MaskStart;
    let Seed = u32(Effect.Emitters[0].Centre.w);
    if (Effect.MaskMode == 1u)
    {
        return Elapsed >= ResolveDerezRelease(Unit, Seed, Effect.Style, 1.0);
    }
    return Elapsed < ResolveRezArrival(Unit, Seed, Effect.Style, 1.0);
}

fn SampleSky(Direction: vec3f) -> vec3f
{
    return textureSampleLevel(SkyView, SkySampler, ProjectSkyCoordinates(Direction), 0.0).rgb * Frame.SkyScale + Frame.NightSkyRadiance;
}

// 📐 Rounded-rectangle coverage of the preview well in tablet UV (aspect-correct corner radii).
fn ResolveWellCoverage(Coordinates: vec2f) -> f32
{
    let Low = Overlay.Preview.xy;
    let High = Overlay.Preview.zw;
    if (any(Coordinates < Low) || any(Coordinates > High))
    {
        return 0.0;
    }
    let Radius = Overlay.Corner.xy;
    let Inner = clamp(Coordinates, Low + Radius, High - Radius);
    let Offset = (Coordinates - Inner) / max(Radius, vec2f(1.0e-5));
    return select(1.0, 0.0, dot(Offset, Offset) > 1.0);
}

@fragment
fn OverlayFragment(Varying: OverlayVarying) -> @location(0) vec4f
{
    if (ClassifyMasked(Varying.BodyLocal))
    {
        discard;
    }
    let View = normalize(Frame.CameraPosition - Varying.WorldPosition);
    var Normal = normalize(Varying.Normal);
    if (dot(Normal, View) < 0.0)
    {
        Normal = -Normal;
    }
    let NdotV = clamp(dot(Normal, View), 0.0, 1.0);
    let Fresnel = 0.04 + 0.96 * pow(1.0 - NdotV, 5.0);
    let Reflected = reflect(-View, Normal);
    var Reflection = SampleSky(Reflected) * Fresnel;
    // 📝 A sun glint on the glass: a narrow lobe of the sun's illuminance.
    let Glint = pow(max(dot(Reflected, Frame.SunDirection), 0.0), 900.0) * 60.0;
    Reflection += Frame.SunIlluminance * (Glint * Fresnel);

    if (Varying.Kind == KindGlass)
    {
        if (VehicleState.Cabin.y < 0.5)
        {
            discard;
        }
        let Transmission = Overlay.Glass.x * (1.0 - Fresnel);
        return vec4f(Reflection, 1.0 - Transmission);
    }
    if (Varying.Kind == KindTablet)
    {
        var Picture = textureSampleLevel(TabletTexture, LinearSampler, Varying.Coordinates, 0.0).rgb;
        if (Overlay.Tablet.z > 0.5)
        {
            var Well = ResolveWellCoverage(Varying.Coordinates);
            if (Overlay.Tablet.w > 0.5) {
                let P = abs((Varying.Coordinates - vec2f(Overlay.Corner.w, Overlay.Glass.w)) * vec2f(1200.0, 750.0));
                let Q = P - vec2f(54.0, 13.0);
                let Distance = length(max(Q, vec2f(0.0))) + min(max(Q.x,Q.y),0.0) - 12.0;
                Well *= smoothstep(-1.0, 1.0, Distance);
            }
            if (Well > 0.0)
            {
                let Inside = (Varying.Coordinates - Overlay.Preview.xy) / max(Overlay.Preview.zw - Overlay.Preview.xy, vec2f(1.0e-5));
                Picture = mix(Picture, textureSampleLevel(PreviewTexture, LinearSampler, Inside, 0.0).rgb, Well);
            }
        }
        // An antiglare display is display-referred, not HDR sky-lit glass. Decode
        // sRGB once at texture sampling; undo the final filmic/exposure transform
        // so auto exposure cannot bleach the authored UI. Zero brightness is black.
        return vec4f(ScreenRadiance(Picture * Overlay.Tablet.y), 1.0);
    }
    if (Varying.Kind == KindCluster)
    {
        let Texel = textureSampleLevel(ClusterTexture, LinearSampler, Varying.Coordinates, 0.0);
        return vec4f(ScreenRadiance(Texel.rgb * Texel.a * Overlay.Tablet.y), 1.0);
    }
    // 📝 Mirror: the reflected view, or the sky alone until a mirror has been rendered.
    var Mirrored = SampleSky(Reflected);
    if (Overlay.Glass.z > 0.5)
    {
        Mirrored = textureSampleLevel(MirrorTexture, LinearSampler, Varying.Coordinates, Varying.Layer, 0.0).rgb;
    }
    return vec4f(Mirrored * Overlay.Glass.y, 1.0);
}

// Invert the same Narkowicz ACES fit used by DisplayPresent.wgsl.
fn ScreenRadiance(LinearDisplay: vec3f) -> vec3f {
    let Y = clamp(LinearDisplay, vec3f(0.0), vec3f(0.99));
    let A = 2.51 - 2.43 * Y;
    let B = 0.03 - 0.59 * Y;
    let X = (-B + sqrt(B*B + 0.56*A*Y)) / (2.0*A);
    let Exposure = select(1.0, DisplayAdaptation[0], DisplayAdaptation[0] > 0.0);
    return X / Exposure;
}
