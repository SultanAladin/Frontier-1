/*====================================================================================================================================
                                                          SOLIDARCEDGE.SLANG
====================================================================================================================================*/
// 🧩 B-rep edges as antialiased pixel-width strokes (six corners per instanced segment); the colour programs blend the
//    whole soft fringe, the identity program writes the R32 pick code for the stroke core only. The x-ray program draws
//    the occluded part of every edge as a thinner, faded stroke that stays pickable

#include "Engine/Shaders/Sketcher/SketcherProjection.slang"
#include "Engine/Shaders/Sketcher/SketcherCadStroke.slang"                                                                   // Frontier stroke quad

[[vk::push_constant]] ConstantBuffer<SketcherProjection> Projection;
[[vk::binding(1, 0)]] StructuredBuffer<uint> EdgeStates;
// 📝 Section (Alt+S): [0] = (n, w), the kept side n·P + w ≥ 0; [1] = (eye, enabled flag).
[[vk::binding(2, 0)]] StructuredBuffer<float4> Section;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INPUT/OUTPUT STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct EdgeSegment
{
    [[vk::location(0)]] float3 Start : POSITION0;                                            // [len] - World point (per instance)
    [[vk::location(1)]] float3 End   : POSITION1;                                            // [len] - World point (per instance)
    [[vk::location(2)]] uint   Edge  : BLENDINDICES0;                                        // [idx] - Scene edge (per instance)
};

struct EdgePixel
{
    float4                 Position : SV_Position;                                           // [-]  - Clip position
    noperspective float    Distance : TEXCOORD0;                                             // [px] - Signed distance from the centre line
    nointerpolation uint   Edge     : BLENDINDICES0;                                         // [idx] - Scene edge
};

struct EdgeTargets
{
    float4 Colour   : SV_Target0;                                                            // [0-1] - Display colour (blended)
    uint   Identity : SV_Target1;                                                            // [-]   - IdentityEdgeBit | edge + 1
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float StrokeRadius(uint State)
{
    float Width = Projection.DisplayStroke.z;
    if ((State & (StateSelected | StateActive)) != 0u) Width *= 2.0;
    else if ((State & StateHovered) != 0u)             Width *= 1.6;
    return Width * 0.5;
}

// 📝 World length of one pixel at Point: the exact derivative of the device y under a unit step along the camera up
//    (constant in orthographic views, growing with the view depth in perspective ones).
float WorldPerPixel(float3 Point, float4 Clip)
{
    const float4 Step   = mul(Projection.ClipFromWorld, float4(Projection.ViewUp.xyz, 0.0));
    const float  Slope  = (Step.y * Clip.w - Clip.y * Step.w) / max(Clip.w * Clip.w, 1.0e-12);
    const float  Pixels = abs(Slope) * 0.5 * Projection.DisplayStroke.y;
    return 1.0 / max(Pixels, 1.0e-6);
}

// 📝 Device-depth drop of Point moved Margin toward the eye (along −Forward); 0 when either point is behind the eye.
float DepthPull(float3 Point, float4 Clip, float Margin)
{
    const float4 Near = mul(Projection.ClipFromWorld, float4(Point - Projection.ViewForward.xyz * Margin, 1.0));
    if (Clip.w <= 1.0e-5 || Near.w <= 1.0e-5) return 0.0;
    return Clip.z / Clip.w - Near.z / Near.w;
}

// 📝 Pull of one endpoint. DisplayStroke.w ≥ 0 (visible strokes): w pixels of world length at that endpoint, so an edge
//    sits in front of its own (curved, tilted) faces by the same few pixels at every zoom — a fixed device-depth bias
//    shrinks to nothing close up in perspective and the faces then cut the stroke into dashes. DisplayStroke.w < 0
//    (x-ray occluded draw): the world margin −w, so a stroke passes the GREATER test only when it lies more than that
//    behind the face in front.
float EndpointPull(float3 Point, float4 Clip)
{
    const float Setting = Projection.DisplayStroke.w;
    const float Margin  = Setting >= 0.0 ? Setting * WorldPerPixel(Point, Clip) : -Setting;
    return DepthPull(Point, Clip, Margin);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROGRAMS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
// 📝 Direct Vulkan indexing avoids the optional shader-draw-parameters feature implied by SV_VertexID.
EdgePixel SketcherEdgeVertex(EdgeSegment Segment, uint VertexIndex : SV_VulkanVertexID)
{
    EdgePixel Pixel;
    Pixel.Position = float4(-2.0, -2.0, 0.0, 1.0);
    Pixel.Distance = 1000.0;
    Pixel.Edge     = Segment.Edge;
    // 📝 Section (Alt+S): the world segment is cut at the plane before projection (exact, unlike a per-pixel test).
    float3 Head = Segment.Start;
    float3 Tail = Segment.End;
    if (Section[1].w > 0.5)
    {
        const float HeadSide = dot(Section[0].xyz, Head) + Section[0].w;
        const float TailSide = dot(Section[0].xyz, Tail) + Section[0].w;
        if (HeadSide < 0.0 && TailSide < 0.0) return Pixel;
        const float3 Crossing = lerp(Head, Tail, HeadSide / (HeadSide - TailSide));
        if (HeadSide < 0.0) Head = Crossing;
        if (TailSide < 0.0) Tail = Crossing;
    }
    float4 Start = mul(Projection.ClipFromWorld, float4(Head, 1.0));
    float4 End   = mul(Projection.ClipFromWorld, float4(Tail, 1.0));
    const float StartPull = EndpointPull(Head, Start);
    const float EndPull   = EndpointPull(Tail, End);
    // 📝 Frontier CadStroke: the same clipping and quad as upstream CadEdge, stretched along the segment too, so polyline
    //    joints overlap instead of leaving notches.
    if (!ClipCadSegment(Start, End)) return Pixel;
    // 📝 Overlap past each end: the core radius plus half the fringe closes the outer notch of a polyline joint; a longer
    //    overlap blends the soft fringe twice there and beads a curved edge (many short segments) into dashes.
    const float     Radius = StrokeRadius(EdgeStates[Segment.Edge]);
    const float     Reach  = Radius + 1.0;
    CadStrokeCorner Corner;
    if (!ExpandCadStroke(Start, End, VertexIndex, Projection.DisplayStroke.xy, Reach, Radius + 0.5, Corner)) return Pixel;
    Pixel.Position = Corner.Position;
    // 📝 The corner takes the depth of its endpoint moved toward the eye (EndpointPull); the screen position is unchanged.
    const float Pull = Corner.EndCondition ? EndPull : StartPull;
    Pixel.Position.z = clamp(Pixel.Position.z - Pull * Pixel.Position.w, 0.0, Pixel.Position.w);
    Pixel.Distance = Corner.Distance;
    return Pixel;
}

// 📝 Coverage is a one-pixel box ramp around the stroke boundary (Radius ± 0.5 px, smoothed); the colour programs keep
//    every pixel above 1/255, so a stroke ends in a soft fringe instead of the stair steps a hard cut leaves.
static const float FringeCoverage = 1.0 / 255.0;                                             // [0-1] - Colour programs keep more
static const float CoreCoverage   = 0.35;                                                    // [0-1] - Identity program keeps more

float StrokeCoverage(float Distance, float Radius)
{
    return CadStrokeCoverage(Distance, max(Radius - 0.5, 0.0), Radius + 0.5);
}

[shader("fragment")]
EdgeTargets SketcherEdgeFragment(EdgePixel Pixel)
{
    uint  State    = EdgeStates[Pixel.Edge];
    float Radius   = StrokeRadius(State);
    float Coverage = StrokeCoverage(Pixel.Distance, Radius);
    if (Coverage < FringeCoverage) discard;
    float3 Colour = float3(0.075, 0.085, 0.105);
    if ((State & StateHovered) != 0u)  Colour = HoverTint * 0.85;
    if ((State & StateSelected) != 0u) Colour = SelectedTint;
    if ((State & StateActive) != 0u)   Colour = ActiveTint;
    EdgeTargets Targets;
    Targets.Colour   = float4(Colour, Coverage);
    Targets.Identity = IdentityEdgeBit | (Pixel.Edge + 1u);
    return Targets;
}

// 📝 X-ray (Plasticity CurveEdge_XRay layer): drawn with depth test GREATER after the surface depth, so only the stroke
//    pixels hidden behind a face arrive here. Unselected strokes are a pale steel at 55 % and 0.7 × the width; selected,
//    active and hovered ones keep their tints at 85 %. The identity is written, so a hidden edge is hovered and picked
//    through the faces in front of it.
[shader("fragment")]
EdgeTargets SketcherEdgeOccludedFragment(EdgePixel Pixel)
{
    uint  State    = EdgeStates[Pixel.Edge];
    float Radius   = StrokeRadius(State) * ((State & (StateSelected | StateActive | StateHovered)) != 0u ? 1.0 : 0.7);
    float Coverage = StrokeCoverage(Pixel.Distance, Radius);
    if (Coverage < FringeCoverage) discard;
    float3 Colour  = float3(0.780, 0.830, 0.910);
    float  Opacity = 0.55;
    if ((State & StateHovered) != 0u)  { Colour = HoverTint;    Opacity = 0.85; }
    if ((State & StateSelected) != 0u) { Colour = SelectedTint; Opacity = 0.85; }
    if ((State & StateActive) != 0u)   { Colour = ActiveTint;   Opacity = 0.85; }
    EdgeTargets Targets;
    Targets.Colour   = float4(Colour, Coverage * Opacity);
    Targets.Identity = IdentityEdgeBit | (Pixel.Edge + 1u);
    return Targets;
}

// 📝 Pick codes: the same strokes drawn with the colour write masked, keeping only the core (the pick zone the strokes
//    always had). DisplayStroke.w < 0 is the x-ray occluded draw, whose unselected strokes are 0.7 × as wide.
[shader("fragment")]
EdgeTargets SketcherEdgeIdentityFragment(EdgePixel Pixel)
{
    uint  State    = EdgeStates[Pixel.Edge];
    bool  Thinned  = Projection.DisplayStroke.w < 0.0 && (State & (StateSelected | StateActive | StateHovered)) == 0u;
    float Radius   = StrokeRadius(State) * (Thinned ? 0.7 : 1.0);
    float Coverage = CadStrokeCoverage(Pixel.Distance, Radius - 0.5, Radius + 0.75);
    if (Coverage < CoreCoverage) discard;
    EdgeTargets Targets;
    Targets.Colour   = float4(0.0, 0.0, 0.0, 0.0);
    Targets.Identity = IdentityEdgeBit | (Pixel.Edge + 1u);
    return Targets;
}
