//============================================================================================================================================
//                                                         NEARESTDEPTHTRANSFER.WGSL
//============================================================================================================================================
// 🧩 Level 0 of the nearest-depth pyramid: the reversed depth attachment copied into a storable single-channel lattice.

// 📝 A depth attachment can be neither copied into a colour texture nor written as storage, so level 0 is transferred
//    texel for texel here; NearestDepthReduction.wgsl then builds every coarser level from the one beneath it.

@group(0) @binding(0) var SurfaceDepth : texture_depth_2d;
@group(0) @binding(1) var LevelTarget  : texture_storage_2d<r32float, write>;

@compute @workgroup_size(8, 8)
fn TransferNearestDepth(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Pixel = vec2i(Invocation.xy);
    if (any(Pixel >= vec2i(textureDimensions(LevelTarget))))
    {
        return;
    }
    textureStore(LevelTarget, Pixel, vec4f(textureLoad(SurfaceDepth, Pixel, 0), 0.0, 0.0, 1.0));
}
