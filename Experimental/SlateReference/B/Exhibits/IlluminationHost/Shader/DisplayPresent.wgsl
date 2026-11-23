//============================================================================================================================================
//                                                           DISPLAYPRESENT.WGSL
//============================================================================================================================================
// 🧩 Exposure, ACES tonal projection and the distance-slice inset, written to the swapchain through an sRGB view.

// 📝 The inset is a top-down slice of the ONE scene distance field at SliceHeight, centred on the cascade focus and
//    spanning cascade 1: blue inside, amber outside with 0.25 m isolines, the cascade-0 window outlined, bricks a
//    moving occupant rewrote glowing magenta and bricks the scrolling window rewrote flashing teal. Driving the
//    ControlVehicle shows the union pass chasing it: only the bricks its old and new bounds touch light up.

//! include "FrameDescriptor.wgsl"
//! include "DistanceTraversal.wgsl"

@group(0) @binding(1) var ResolvedRadiance                : texture_2d<f32>;
@group(0) @binding(2) var<storage, read> Adaptation       : array<f32, 4>;

struct PresentVarying
{
    @builtin(position) Position : vec4f,
}

@vertex
fn PresentVertex(@builtin(vertex_index) Vertex: u32) -> PresentVarying
{
    let Corner = vec2f(f32((Vertex << 1u) & 2u), f32(Vertex & 2u));
    var Varying: PresentVarying;
    Varying.Position = vec4f(Corner * 2.0 - 1.0, 0.0, 1.0);
    return Varying;
}

// 📐 Narkowicz's ACES filmic fit.
fn ProjectFilmic(Radiance: vec3f) -> vec3f
{
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    return clamp((Radiance * (a * Radiance + b)) / (Radiance * (c * Radiance + d) + e), vec3f(0.0), vec3f(1.0));
}

fn ResolveInsetColour(Coordinates: vec2f) -> vec3f
{
    let Window = Distance.Clips[min(1u, Distance.ClipCount - 1u)];
    let Span = f32(Window.Resolution) * Window.VoxelExtent;
    let Planar = Frame.FocusPosition.xy + vec2f(Coordinates.x - 0.5, 0.5 - Coordinates.y) * Span;
    let Position = vec3f(Planar, Frame.SliceHeight);
    let Level = ResolveFinestClip(Position, 1.0);
    if (Level >= Distance.ClipCount)
    {
        return vec3f(0.05);
    }
    let Signed = SampleClipDistance(Level, Position);
    var Colour = select(vec3f(0.95, 0.62, 0.22), vec3f(0.18, 0.42, 0.95), Signed < 0.0);
    Colour *= 0.35 + 0.65 * exp(-abs(Signed) * 0.9);
    let Isoline = abs(fract(Signed / 0.25 + 0.5) - 0.5);
    Colour = mix(Colour * 0.55, Colour, smoothstep(0.0, 0.06, Isoline));
    if (abs(Signed) < 0.5 * Distance.Clips[Level].VoxelExtent)
    {
        Colour = vec3f(1.0);
    }
    let Voxel = vec3i(floor(Position / Distance.Clips[Level].VoxelExtent));
    let Brick = vec3i(floor(vec3f(Voxel) / f32(BrickEdge)));
    let Age = ResolveBrickAge(BrickEpochs[ResolveBrickSlot(Level, Brick, Distance.BricksPerAxis)], Frame.FrameIndex);
    if (Age.y > 0.5 && Age.x < 24.0)
    {
        Colour = mix(Colour, vec3f(1.0, 0.1, 0.85), 0.6 * (1.0 - Age.x / 24.0));
    }
    else if (Age.x < 6.0)
    {
        Colour = mix(Colour, vec3f(0.2, 0.85, 0.9), 0.35 * (1.0 - Age.x / 6.0));
    }
    let Finest = Distance.Clips[0];
    let FinestLow = vec2f(Finest.OriginVoxel.xy) * Finest.VoxelExtent;
    let FinestHigh = FinestLow + vec2f(f32(Finest.Resolution) * Finest.VoxelExtent);
    let EdgeLow = abs(Planar - FinestLow);
    let EdgeHigh = abs(Planar - FinestHigh);
    let Thickness = Span / 256.0;
    let InsideX = Planar.x >= FinestLow.x && Planar.x <= FinestHigh.x;
    let InsideY = Planar.y >= FinestLow.y && Planar.y <= FinestHigh.y;
    if ((InsideY && (EdgeLow.x < Thickness || EdgeHigh.x < Thickness)) || (InsideX && (EdgeLow.y < Thickness || EdgeHigh.y < Thickness)))
    {
        Colour = vec3f(0.3, 1.0, 0.45);
    }
    return Colour;
}

@fragment
fn PresentFragment(Varying: PresentVarying) -> @location(0) vec4f
{
    let Pixel = vec2i(Varying.Position.xy);
    let Radiance = textureLoad(ResolvedRadiance, Pixel, 0).rgb;
    var Exposure = Adaptation[0];
    if (Exposure <= 0.0)
    {
        Exposure = 1.0;
    }
    var Colour = ProjectFilmic(Radiance * Exposure);
    if (Frame.InspectionMode == 6u || Frame.InspectionMode == 8u || Frame.InspectionMode == 9u)
    {
        Colour = clamp(Radiance, vec3f(0.0), vec3f(1.0));
    }
    if (Frame.InsetEnabled != 0u)
    {
        let Size = floor(Frame.ScreenExtent.y * 0.34);
        let Origin = vec2f(Frame.ScreenExtent.x - Size - 16.0, 16.0);
        let Local = (Varying.Position.xy - Origin) / Size;
        if (all(Local >= vec2f(0.0)) && all(Local <= vec2f(1.0)))
        {
            Colour = ResolveInsetColour(Local);
            let Border = min(min(Local.x, 1.0 - Local.x), min(Local.y, 1.0 - Local.y)) * Size;
            if (Border < 1.5)
            {
                Colour = vec3f(0.9);
            }
        }
    }
    return vec4f(Colour, 1.0);
}
