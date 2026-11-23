//============================================================================================================================================
//                                                           FRAMEDESCRIPTOR.WGSL
//============================================================================================================================================
// 🧩 The frame record at group 0, slot 0 — every program that reads the camera, the light or the toggles includes this.

//! include "SceneRecords.wgsl"

@group(0) @binding(0) var<uniform> Frame : FrameRecord;

// 📝 Reconstructs the world position of a screen pixel centre from reversed depth through the jittered inverse.
fn ReconstructWorldPosition(Pixel: vec2f, Depth: f32) -> vec3f
{
    let Ndc = vec2f(Pixel.x * Frame.InverseScreenExtent.x * 2.0 - 1.0, 1.0 - Pixel.y * Frame.InverseScreenExtent.y * 2.0);
    let Clip = Frame.InverseViewProjection * vec4f(Ndc, Depth, 1.0);
    return Clip.xyz / Clip.w;
}

// 📝 Linear view distance along −Z of a world position; used by the similarity tests of every reprojection.
fn ResolveViewDepth(Position: vec3f) -> f32
{
    return -(Frame.View * vec4f(Position, 1.0)).z;
}

// 📝 View distance the same world point had in the previous frame: w of its previous unjittered clip position.
fn ResolvePriorViewDepth(Position: vec3f) -> f32
{
    return (Frame.PreviousUnjitteredViewProjection * vec4f(Position, 1.0)).w;
}

// 📝 A prior guide depth agrees when it matches either where this point was last frame (a static surface under a
//    moving camera) or where it is now (a surface riding with the camera, such as the chased vehicle).
fn ClassifyPriorDepth(PriorDepth: f32, ExpectedDepth: f32, ViewDepth: f32) -> bool
{
    return abs(PriorDepth - ExpectedDepth) < 0.08 * ExpectedDepth + 0.03 || abs(PriorDepth - ViewDepth) < 0.08 * ViewDepth + 0.03;
}
