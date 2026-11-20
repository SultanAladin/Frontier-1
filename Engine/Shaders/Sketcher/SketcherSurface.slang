/*====================================================================================================================================
                                                        SOLIDARCSURFACE.SLANG
====================================================================================================================================*/
// 🧩 B-rep face shading (analytic Studio or a matcap layer per face) with selection tints and the R32 identity output

#include "Engine/Shaders/Sketcher/SketcherProjection.slang"
#include "Engine/Shaders/Sketcher/SketcherMatCapLookup.slang"                                                                // Frontier lit-sphere lookup

[[vk::push_constant]] ConstantBuffer<SketcherProjection> Projection;
[[vk::binding(0, 0)]] StructuredBuffer<uint> FaceStates;
[[vk::binding(0, 1)]] Sampler2DArray         Matcaps;                                       // Layer k − 1 = matcap layer k
// 📝 Section (Alt+S): [0] = (n, w), the kept side n·P + w ≥ 0; [1] = (eye, enabled flag).
[[vk::binding(2, 0)]] StructuredBuffer<float4> Section;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INPUT/OUTPUT STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceCorner
{
    [[vk::location(0)]] float3 Position : POSITION0;                                         // [len] - World point
    [[vk::location(1)]] float3 Normal   : NORMAL0;                                           // [-]   - Outward unit normal
    [[vk::location(2)]] uint   Face     : BLENDINDICES0;                                     // [idx] - Scene face
};

// 📝 Centroid: with multisampling a border pixel is shaded once at a point inside the covered samples, so the section
//    test and the lighting never extrapolate past the triangle.
struct SurfacePixel
{
    float4                 Position : SV_Position;                                           // [-]   - Clip position
    centroid float3        Normal   : NORMAL0;                                               // [-]   - World normal
    centroid float3        World    : TEXCOORD1;                                             // [len] - World point
    nointerpolation uint   Face     : BLENDINDICES0;                                         // [idx] - Scene face
};

struct SurfaceTargets
{
    float4 Colour   : SV_Target0;                                                            // [0-1] - Display colour
    uint   Identity : SV_Target1;                                                            // [-]   - Face index + 1
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROGRAMS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
SurfacePixel SketcherSurfaceVertex(SurfaceCorner Corner)
{
    SurfacePixel Pixel;
    Pixel.Position = mul(Projection.ClipFromWorld, float4(Corner.Position, 1.0));
    Pixel.Normal   = Corner.Normal;
    Pixel.World    = Corner.Position;
    Pixel.Face     = Corner.Face;
    return Pixel;
}

// 📝 View-space normal → studio lighting: soft key from the upper left, cool fill, hemispherical ambient and a rim term.
//    Matcap layer k ≥ 1 (bits 8 … 15 of the face state) replaces it with Matcaps[k − 1] at uv = (½ + ½x, ½ − ½y) of the
//    view-space normal (SketcherMatcap.h: pixel (i, j) holds normal x = 2(i + ½)/E − 1, y = 1 − 2(j + ½)/E).
//    Back faces (open sheets, inside views) are shaded darker so the inside of a body never reads as its outside.
//    X-ray (ViewForward.w = face opacity α > 0): the colour leaves with alpha α and the ghost pipeline blends it over
//    the grid and image planes drawn first; α = 0 (x-ray off) writes opaque alpha 1.
[shader("fragment")]
SurfaceTargets SketcherSurfaceFragment(SurfacePixel Pixel, bool FrontFacing : SV_IsFrontFace)
{
    // 📝 Section (Alt+S): the removed side (n·P + w < 0) is discarded. A back face that is still the nearest kept surface
    //    is seen through the opening from inside the material, so it becomes the cap: the hatched section colour lit as
    //    the plane, identity IdentitySectionCap. It keeps its own depth — the plane crossing would be nearer than kept
    //    front faces wherever the eye ray crosses the plane outside the material — and the viewport draws the grid and
    //    image planes before the surfaces while sectioning, so nothing shows through a cap. Sheet faces (StateSheet)
    //    keep their dark back side.
    const float4 Plane     = Section[0];
    const bool   Sectioned = Section[1].w > 0.5;
    const float  Side      = dot(Plane.xyz, Pixel.World) + Plane.w;
    if (Sectioned && Side < 0.0) discard;
    if (Sectioned && !FrontFacing && (FaceStates[Pixel.Face] & StateSheet) == 0u)
    {
        const float3 Facing   = -Plane.xyz;                                                  // The cap faces the removed side
        const float3 CapView  = float3(dot(Facing, Projection.ViewRight.xyz), dot(Facing, Projection.ViewUp.xyz), -dot(Facing, Projection.ViewForward.xyz));
        const float  CapKey   = saturate(dot(CapView, normalize(float3(-0.45, 0.55, 0.70))));
        const float3 Base     = float3(0.86, 0.38, 0.29) * (0.62 + 0.38 * CapKey);
        // 45° screen hatch every 9 px, anti-aliased over one pixel.
        const float  Phase    = (Pixel.Position.x + Pixel.Position.y) / 9.0;
        const float  Gap      = abs(frac(Phase + 0.5) - 0.5);
        const float  Ink      = 1.0 - smoothstep(0.07, 0.07 + max(fwidth(Phase), 1.0e-4) * 1.2, Gap);
        SurfaceTargets Cap;
        Cap.Colour   = float4(lerp(Base, Base * 0.42, Ink), Projection.ViewForward.w > 0.0 ? Projection.ViewForward.w : 1.0);
        Cap.Identity = IdentitySectionCap;
        return Cap;
    }
    float3 Normal = normalize(Pixel.Normal) * (FrontFacing ? 1.0 : -1.0);
    float3 View   = float3(dot(Normal, Projection.ViewRight.xyz), dot(Normal, Projection.ViewUp.xyz), -dot(Normal, Projection.ViewForward.xyz));
    float  Key    = saturate(dot(View, normalize(float3(-0.45, 0.55, 0.70))));
    float  Fill   = saturate(dot(View, normalize(float3(0.60, -0.10, 0.80))));
    float  Sky    = 0.5 + 0.5 * View.y;
    float  Rim    = pow(1.0 - saturate(View.z), 3.0);
    uint   State  = FaceStates[Pixel.Face];
    uint   Layer  = (State >> 8u) & 0xFFu;
    float3 Colour;
    if (Layer == 0u)
    {
        float3 Albedo = float3(0.70, 0.72, 0.76);
        Colour  = Albedo * (0.22 + 0.18 * Sky) + Albedo * Key * 0.62 + float3(0.62, 0.70, 0.82) * Fill * 0.16 + Rim * 0.10;
        Colour += pow(saturate(dot(reflect(float3(0.0, 0.0, -1.0), View), normalize(float3(-0.45, 0.55, 0.70)))), 24.0) * 0.18;
    }
    else
    {
        // 📝 Frontier MatCapCoordinates, pulled 0.5 % inside the disc so the silhouette never samples the outer texels.
        float2 Lookup = MatCapCoordinates(View);
        float2 Offset = Lookup - 0.5;
        float  Reach  = length(Offset);
        if (Reach > 0.4975) Lookup = 0.5 + Offset * (0.4975 / Reach);
        Colour = Matcaps.Sample(float3(Lookup, float(Layer - 1u))).rgb;
    }
    // 📝 Zebra (vehicle plan V8, ViewUp.w = stripe count): the view direction reflected in the normal, its angle to the
    //    camera up × count / π; stripe k covers phase [k, k + ½). Mirrors SketcherZebraPhase (SketcherScene.cpp).
    if (Projection.ViewUp.w > 0.0)
    {
        float3 Forward   = Projection.ViewForward.xyz;
        float3 Reflected = Forward - 2.0 * dot(Forward, Normal) * Normal;
        float  Phase     = Projection.ViewUp.w * acos(clamp(dot(Reflected, Projection.ViewUp.xyz), -1.0, 1.0)) / 3.14159265;
        float  Wave      = abs(frac(Phase) - 0.5) * 2.0;                                     // 1 at phase k, 0 at k + ½
        float  Blur      = max(fwidth(Phase) * 2.0, 1.0e-4);                                 // Pixel-wide anti-aliasing
        float  Light     = smoothstep(0.5 - Blur, 0.5 + Blur, Wave);
        Colour = lerp(float3(0.05, 0.05, 0.06), float3(0.96, 0.96, 0.97), Light) * (0.80 + 0.20 * Key);
    }

    if ((State & StateHovered) != 0u)  Colour = lerp(Colour, HoverTint, 0.28);
    if ((State & StateSelected) != 0u) Colour = lerp(Colour, SelectedTint * (0.55 + 0.45 * Key), 0.62);
    if ((State & StateActive) != 0u)   Colour = lerp(Colour, ActiveTint * (0.55 + 0.45 * Key), 0.62);
    if (!FrontFacing)                  Colour = Colour * float3(0.55, 0.42, 0.42);

    SurfaceTargets Targets;
    Targets.Colour   = float4(saturate(Colour), Projection.ViewForward.w > 0.0 ? Projection.ViewForward.w : 1.0);
    Targets.Identity = Pixel.Face + 1u;
    return Targets;
}
