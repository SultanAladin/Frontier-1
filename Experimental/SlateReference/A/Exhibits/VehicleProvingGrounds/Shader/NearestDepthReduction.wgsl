//============================================================================================================================================
//                                                        NEARESTDEPTHREDUCTION.WGSL
//============================================================================================================================================
// 🧩 One level of the nearest-depth pyramid the screen traces walk: each texel keeps the nearest depth of its source tile.

// 📝 Reversed depth stores larger values nearer the eye, so "nearest" is a maximum, and the clear value 0 (nothing
//    rastered) never wins over a surface. A level-L texel therefore bounds every pixel of its 2^L × 2^L tile from in
//    front: a ray in front of that value is in front of the whole tile and may cross it in a single step.
//
//    Level extents follow max(1, ⌊extent / 2⌋). Where the source extent is odd its last row or column has no partner,
//    so the final target texel takes three source texels on that axis instead of two — no pixel is ever left unbounded.

@group(0) @binding(0) var LevelSource : texture_2d<f32>;
@group(0) @binding(1) var LevelTarget : texture_storage_2d<r32float, write>;

@compute @workgroup_size(8, 8)
fn ReduceNearestDepth(@builtin(global_invocation_id) Invocation: vec3u)
{
    let TargetExtent = vec2i(textureDimensions(LevelTarget));
    let Pixel = vec2i(Invocation.xy);
    if (any(Pixel >= TargetExtent))
    {
        return;
    }
    let SourceExtent = vec2i(textureDimensions(LevelSource));
    let Unpartnered = ((SourceExtent & vec2i(1)) == vec2i(1)) & (Pixel == TargetExtent - vec2i(1));
    let Span = vec2i(2) + select(vec2i(0), vec2i(1), Unpartnered);
    var Nearest = 0.0;
    for (var Row = 0; Row < Span.y; Row++)
    {
        for (var Column = 0; Column < Span.x; Column++)
        {
            let Texel = min(Pixel * 2 + vec2i(Column, Row), SourceExtent - vec2i(1));
            Nearest = max(Nearest, textureLoad(LevelSource, Texel, 0).r);
        }
    }
    textureStore(LevelTarget, Pixel, vec4f(Nearest, 0.0, 0.0, 1.0));
}
