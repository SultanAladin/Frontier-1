//============================================================================================================================================
//                                                            FORWARDRASTER.WGSL
//============================================================================================================================================
// 🧩 A small lit forward pass over the scene's facets: the mirrors' reflected views and the HMI Garage's studio portrait of the car.

// 📝 The main view is deferred and globally illuminated — far too much for three side views and a 640 × 400 preview.
//    This pass draws the same vertex records (the same instanced runs, the same rigid wheel damage state, steering wheel and
//    derez mask) with direct light only:
//
//    | Mode   | Light                                                                                              |
//    |--------|----------------------------------------------------------------------------------------------------|
//    | scene  | the sun through the cascaded shadow depth, the sky view averaged over the normal's hemisphere, a    |
//    |        | sun-lit ground bounce from below, authored emission — what a mirror shows of the grounds           |
//    | studio | a key, a fill and a rim light fixed to the camera, a dim ambient, a soft floor glow — the car as a  |
//    |        | configurator shows it, its own camera and lighting (Docs/CarHmiReport.md)                           |
//
//    A mirror's view is the driver's eye reflected in the mirror's plane, through an off-axis frustum fitted to the
//    glass; its near plane is the mirror itself, so nothing behind the glass leaks into it.

//! include "FrameDescriptor.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "SkyProjection.wgsl"
//! include "EffectRecords.wgsl"
//! include "VehicleVisual.wgsl"
//! include "CabinVolume.wgsl"

struct ForwardView
{
    ViewProjection : mat4x4f,
    Camera         : vec4f,   // xyz [m] eye, w mode: 0 scene, 1 studio
    Key            : vec4f,   // xyz studio key light direction (toward the light), w its intensity
    Fill           : vec4f,   // xyz studio fill direction, w intensity
    Rim            : vec4f,   // xyz studio rim direction, w intensity; scene: x hides the car (derezzed)
}

struct CascadeReceiver
{
    LightViewProjection : array<mat4x4f, 3>,
    Splits              : vec4f,
    TexelWorld          : vec4f,
}

@group(0) @binding(1) var<uniform> View                 : ForwardView;
@group(0) @binding(2) var<storage, read> Occupants      : array<OccupantRecord>;
@group(0) @binding(3) var<storage, read> Materials      : array<MaterialRecord>;
@group(0) @binding(4) var<storage, read> VehicleState   : VehicleRecord;
@group(0) @binding(5) var<uniform> Effect               : EffectRecord;
@group(0) @binding(6) var ShadowDepth                   : texture_depth_2d_array;
@group(0) @binding(7) var ShadowSampler                 : sampler_comparison;
@group(0) @binding(8) var<uniform> Receiver             : CascadeReceiver;
@group(0) @binding(9) var SkyView                       : texture_2d<f32>;
@group(0) @binding(10) var SkySampler                   : sampler;

struct ForwardVarying
{
    @builtin(position) Position             : vec4f,
    @location(0) Normal                     : vec3f,
    @location(1) @interpolate(flat) Family  : u32,
    @location(2) WorldPosition              : vec3f,
    @location(3) @interpolate(flat) Vehicle : u32,
    @location(4) BodyLocal                  : vec3f,
    @location(5) Barycentric : vec3f,
}

@vertex
fn ForwardVertex(@location(0) Position: vec3f, @location(1) Family: u32, @location(2) Normal: vec3f,
                 @location(3) OccupantOffset: u32, @builtin(instance_index) OccupantIndex: u32, @builtin(vertex_index) VertexIndex: u32) -> ForwardVarying
{
    let Index = OccupantIndex + OccupantOffset;
    let Occupant = Occupants[Index];
    var Moved: DeformedVertex;
    Moved.Position = Position;
    Moved.Normal = Normal;
    if (Index >= 1u && Index < 5u)
    {
        Moved = ResolveWheelVertex(Index - 1u, Family, Position, Normal);
    }
    else if (Index == 0u)
    {
        Moved = DeformBodyVertex(Family, Position, Normal);
    }
    let World = Occupant.LocalToWorld * vec4f(Moved.Position, 1.0);
    var Varying: ForwardVarying;
    Varying.Position = View.ViewProjection * World;
    Varying.Normal = (Occupant.LocalToWorld * vec4f(Moved.Normal, 0.0)).xyz;
    Varying.Family = select(Family, Occupant.MaterialOverride, Occupant.MaterialOverride != NoMaterialOverride);
    Varying.WorldPosition = World.xyz;
    Varying.Vehicle = select(0u, 1u, Index < 5u);
    Varying.Barycentric = vec3f(select(0.0, 1.0, VertexIndex % 3u == 0u), select(0.0, 1.0, VertexIndex % 3u == 1u), select(0.0, 1.0, VertexIndex % 3u == 2u));
    Varying.BodyLocal = (Occupants[0].WorldToLocal * World).xyz;
    return Varying;
}

fn ResolveSunVisibility(Position: vec3f, Normal: vec3f) -> f32
{
    let ViewDistance = ResolveViewDepth(Position);
    let Count = u32(Receiver.Splits.w);
    if (Count == 0u || ViewDistance < 0.0 || ViewDistance > Receiver.Splits[Count - 1u])
    {
        return 1.0;
    }
    var Cascade = 0u;
    while (Cascade + 1u < Count && ViewDistance > Receiver.Splits[Cascade])
    {
        Cascade++;
    }
    let Offset = Position + Normal * (1.5 * Receiver.TexelWorld[Cascade]);
    let Clip = Receiver.LightViewProjection[Cascade] * vec4f(Offset, 1.0);
    let Ndc = Clip.xyz / Clip.w;
    let Coordinates = vec2f(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5);
    if (any(Coordinates < vec2f(0.0)) || any(Coordinates > vec2f(1.0)) || Ndc.z > 1.0)
    {
        return 1.0;
    }
    return textureSampleCompareLevel(ShadowDepth, ShadowSampler, Coordinates, Cascade, Ndc.z);
}

// 📝 The derez mask as the surface raster applies it: released cells are gone, hidden is all gone.
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

@fragment
fn ForwardFragment(Varying: ForwardVarying) -> @location(0) vec4f
{
    // Derivatives evaluated uniformly; triangle-list barycentrics give pixel-width edges.
    let Width = max(fwidth(Varying.Barycentric), vec3f(0.00001));
    let EdgeDistance = Varying.Barycentric / Width;
    let Edge = 1.0 - smoothstep(0.6, 1.6, min(min(EdgeDistance.x, EdgeDistance.y), EdgeDistance.z));
    let Scanning = View.Camera.w > 3.5;
    let ScanPlane = mix(3.2, -3.2, fract(Frame.ElapsedSeconds * 0.2));
    if (Scanning && abs(Varying.BodyLocal.x - ScanPlane) < 0.035) { return vec4f(0.5, 0.95, 1.0, 1.0); }
    if (View.Camera.w > 1.5 && (!Scanning || Varying.BodyLocal.x > ScanPlane)) {
        if (!Scanning && View.Camera.w > 2.5 && Edge < 0.15) { discard; }
        // Screen-door transparency avoids sorting and leaves the world materials untouched.
        if ((Scanning || View.Camera.w < 2.5) && Edge < 0.15 && (u32(Varying.Position.x) + u32(Varying.Position.y)) % 4u != 0u) { discard; }
        return vec4f(mix(vec3f(0.035, 0.16, 0.2), vec3f(0.35, 0.85, 1.0), Edge), 1.0);
    }
    if (Varying.Vehicle == 1u)
    {
        // 📝 A mirror never sees mirror glass (each mirror's own lies on its near plane); see-through glazing is gone.
        if ((Varying.Family == VehicleGlassFamily && VehicleState.Cabin.y > 0.5) || Varying.Family == MirrorGlassFamily)
        {
            discard;
        }
        if (ClassifyMasked(Varying.BodyLocal))
        {
            discard;
        }
    }
    let Surface = ProjectSurfaceSample(Materials[Varying.Family]);
    let ViewDirection = normalize(View.Camera.xyz - Varying.WorldPosition);
    var Normal = normalize(Varying.Normal);
    if (dot(Normal, ViewDirection) < 0.0)
    {
        Normal = -Normal;
    }
    let Albedo = ResolveDiffuseAlbedo(Surface);
    var Radiance = Surface.Emission * Frame.EmissionScale;
    if (View.Camera.w > 0.5)
    {
        // 📝 Studio: three lights fixed to the camera, a dim cool ambient, a warm glow from the floor.
        Radiance += ResolveDirectReflectance(Surface, Normal, ViewDirection, normalize(View.Key.xyz)) * View.Key.w;
        Radiance += ResolveDirectReflectance(Surface, Normal, ViewDirection, normalize(View.Fill.xyz)) * View.Fill.w;
        Radiance += ResolveDirectReflectance(Surface, Normal, ViewDirection, normalize(View.Rim.xyz)) * View.Rim.w;
        Radiance += Albedo * (vec3f(0.10, 0.12, 0.14) * (0.6 + 0.4 * Normal.z) + vec3f(0.05, 0.045, 0.04) * max(-Normal.z, 0.0));
        let Reflected = reflect(-ViewDirection, Normal);
        Radiance += ResolveSpecularColour(Surface) * 0.08 * smoothstep(-0.2, 0.6, Reflected.z) * (1.0 - Surface.Roughness);
        // 📝 The preview is a picture on a screen (display-referred, 8-bit): a soft shoulder keeps the coat's highlights.
        return vec4f(Radiance * 1.5 / (vec3f(1.0) + Radiance * 1.5), 1.0);
    }
    // 📝 Scene: the sun with its cascaded shadow, the sky over the normal's hemisphere, the ground's bounce.
    let Visibility = ResolveSunVisibility(Varying.WorldPosition, Normal);
    Radiance += Frame.SunIlluminance * Visibility * ResolveDirectReflectance(Surface, Normal, ViewDirection, Frame.SunDirection);
    let SkyDirection = normalize(vec3f(Normal.xy, max(Normal.z, 0.0) + 0.35));
    let Sky = textureSampleLevel(SkyView, SkySampler, ProjectSkyCoordinates(SkyDirection), 0.0).rgb * Frame.SkyScale + Frame.NightSkyRadiance;
    let Bounce = Frame.SunIlluminance * (max(Frame.SunDirection.z, 0.0) * 0.18 * InversePi);
    let Enclosure = select(1.0, mix(1.0, 0.2, ResolveCabinShare(Varying.BodyLocal)), Varying.Vehicle == 1u);
    Radiance += Albedo * (Sky * (0.55 + 0.45 * Normal.z) + Bounce * (0.5 - 0.5 * Normal.z)) * Enclosure;
    let Reflected = reflect(-ViewDirection, Normal);
    let Glossy = textureSampleLevel(SkyView, SkySampler, ProjectSkyCoordinates(normalize(vec3f(Reflected.xy, max(Reflected.z, 0.02)))), 0.0).rgb * Frame.SkyScale;
    Radiance += ResolveSpecularColour(Surface) * Glossy * (1.0 - Surface.Roughness) * 0.6;
    return vec4f(Radiance, 1.0);
}
