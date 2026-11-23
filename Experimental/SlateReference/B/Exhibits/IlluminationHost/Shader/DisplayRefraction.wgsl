//============================================================================================================================================
//                                                           DISPLAYREFRACTION.WGSL
//============================================================================================================================================
// 🧩 Refraction for transmissive dielectrics: the recombined radiance is copied out, and each refractive pixel adds the
//    Fresnel-weighted light that lies behind it along Snell's bent direction. A screen-space approximation, not a ray trace.

//! include "FrameDescriptor.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "PackingCodec.wgsl"

@group(0) @binding(1) var<storage, read> Materials : array<MaterialRecord>;
@group(0) @binding(2) var SurfaceAttributes        : texture_2d<f32>;
@group(0) @binding(3) var SurfaceDepth             : texture_depth_2d;
@group(0) @binding(4) var Backdrop                 : texture_2d<f32>;                  // the recombined radiance, copied before this pass
@group(0) @binding(5) var RefractedTarget          : texture_storage_2d<rgba16float, write>;

// 📐 Stand-in for the path a bent ray takes through a glass body, in metres: the screen-space offset is the projection of
//    the point this far along the refracted direction. A ray through a sphere of radius R crosses about 2R; half that
//    keeps the offset inside the object's own silhouette.
const RefractionDepth : f32 = 0.5;

fn ProjectRefractionPixel(Position: vec3f) -> vec2f
{
    let Clip = Frame.UnjitteredViewProjection * vec4f(Position, 1.0);
    let Ndc = Clip.xy / max(Clip.w, 1.0e-4);
    return vec2f(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5) * Frame.ScreenExtent;
}

// 📝 Transmitted light is what lies behind the surface along Snell's refraction, weighted by the Fresnel transmittance. The
//    opaque shading, including the Fresnel-weighted reflection, is already in the backdrop at this pixel and is kept. A
//    body whose path leaves the screen, or that totally reflects, samples its own pixel.
@compute @workgroup_size(8, 8, 1)
fn ResolveRefraction(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Pixel = vec2i(Invocation.xy);
    if (any(vec2f(Pixel) >= Frame.ScreenExtent) || Frame.InspectionMode != 0u)
    {
        return;
    }
    let Depth = textureLoad(SurfaceDepth, Pixel, 0);
    if (Depth <= 0.0)
    {
        return;
    }
    let Attributes = textureLoad(SurfaceAttributes, Pixel, 0);
    let Material = Materials[u32(Attributes.z + 0.5)];
    if (Material.IndexOfRefraction <= 1.0)
    {
        return;
    }
    let Normal = DecodeOctahedral(Attributes.xy);
    let Position = ReconstructWorldPosition(vec2f(Pixel) + 0.5, Depth);
    let View = normalize(Frame.CameraPosition - Position);
    let NdotV = max(dot(Normal, View), 1.0e-3);
    // 📐 Air to body: eta = n_air / n_body. refract() returns zero on total internal reflection; the offset is then zero.
    let Bent = refract(-View, Normal, 1.0 / Material.IndexOfRefraction);
    let Offset = ProjectRefractionPixel(Position + Bent * RefractionDepth) - ProjectRefractionPixel(Position);
    let Sampled = clamp(vec2i(vec2f(Pixel) + Offset), vec2i(0), vec2i(Frame.ScreenExtent) - vec2i(1));
    let Behind = textureLoad(Backdrop, Sampled, 0).rgb;
    let Existing = textureLoad(Backdrop, Pixel, 0).rgb;
    let Transmittance = (vec3f(1.0) - ResolveFresnel(vec3f(Material.SpecularLevel), NdotV)) * Material.Transmission;
    textureStore(RefractedTarget, Pixel, vec4f(Existing + Transmittance * Behind, 1.0));
}
