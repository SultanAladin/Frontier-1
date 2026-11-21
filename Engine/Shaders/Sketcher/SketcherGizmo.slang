/*====================================================================================================================================
                                                          SOLIDARCGIZMO.SLANG
====================================================================================================================================*/
// 🧩 Transform gizmo handles (Slate GizmoPRO): CPU-built world triangles, lit cones / cylinders, flat arcs, quads and ring;
//    drawn last over a cleared depth so the gizmo is never hidden by the model and still occludes itself

#include "Engine/Shaders/Sketcher/SketcherProjection.slang"

[[vk::push_constant]] ConstantBuffer<SketcherProjection> Projection;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INPUT/OUTPUT STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct GizmoVertex
{
    [[vk::location(0)]] float3 Position : POSITION0;                                         // [len] - World point
    [[vk::location(1)]] float3 Normal   : NORMAL0;                                           // [-]   - Shading normal, (0, 0, 0) = unlit
    [[vk::location(2)]] float4 Colour   : COLOR0;                                            // [0-1] - RGBA8 UNORM
};

struct GizmoPixel
{
    float4 Position : SV_Position;                                                           // [-]   - Clip position
    float3 Normal   : NORMAL0;                                                               // [-]   - Shading normal
    float4 Colour   : COLOR0;                                                                // [0-1] - Base colour and opacity
};

struct GizmoTargets
{
    float4 Colour : SV_Target0;                                                              // [0-1] - Blended over the scene
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROGRAMS
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
GizmoPixel SketcherGizmoVertex(GizmoVertex Vertex)
{
    GizmoPixel Pixel;
    Pixel.Position = mul(Projection.ClipFromWorld, float4(Vertex.Position, 1.0));
    Pixel.Normal   = Vertex.Normal;
    Pixel.Colour   = Vertex.Colour;
    return Pixel;
}

[shader("fragment")]
GizmoTargets SketcherGizmoFragment(GizmoPixel Pixel)
{
    // 📝 Slate MeshStandardMaterial look, simplified: two-sided headlight (ambient 0.45 + diffuse 0.55 toward the eye)
    //    for the round handles; flat colour for the unlit parts.
    float3 Colour = Pixel.Colour.rgb;
    float  Size   = dot(Pixel.Normal, Pixel.Normal);
    if (Size > 1.0e-6)
    {
        float3 Normal = Pixel.Normal * rsqrt(Size);
        float  Facing = abs(dot(Normal, -Projection.ViewForward.xyz));
        Colour = Colour * (0.45 + 0.55 * Facing);
    }
    GizmoTargets Targets;
    Targets.Colour = float4(Colour, Pixel.Colour.a);
    return Targets;
}
