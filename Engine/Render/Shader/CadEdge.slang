/*==========================================================================================================================================
                                                               CADEDGE.SLANG
==========================================================================================================================================*/
// 🧩 Expand authored topology edges into antialiased pixel-width strokes without optional wide lines or geometry shaders.

#include "SurfaceProjection.slang"

[[vk::push_constant]] ConstantBuffer<SurfaceProjection> Projection;

//----------------------------------------------------------------------------------------------------------------------
//                                                EDGE INPUT AND OUTPUT
//----------------------------------------------------------------------------------------------------------------------

struct EdgeInput
{
    float3 Start : POSITION0;
    float3 End : POSITION1;
};
struct EdgePixel
{
    float4 Position : SV_Position;
    noperspective float Distance : TEXCOORD0;
};

bool ClipEdgeBoundary(inout float4 Start, inout float4 End, float StartDistance, float EndDistance)
{
    if (StartDistance < 0 && EndDistance < 0) return false;
    if (StartDistance < 0 || EndDistance < 0)
    {
        float4 Intersection = lerp(Start, End, StartDistance / (StartDistance - EndDistance));
        if (StartDistance < 0) Start = Intersection;
        else End = Intersection;
    }
    return true;
}

[shader("vertex")]
// 📝 Direct Vulkan indexing avoids the optional shader-draw-parameters feature implied by SV_VertexID.
EdgePixel CadEdgeVertex(EdgeInput Edge, uint VertexIndex : SV_VulkanVertexID)
{
    EdgePixel Pixel;
    Pixel.Position = float4(-2, -2, 0, 1);
    Pixel.Distance = 1000;
    float4 Start = mul(Projection.ClipFromSurface, float4(Edge.Start, 1));
    float4 End = mul(Projection.ClipFromSurface, float4(Edge.End, 1));
    // 📝 Clip before perspective division, including the near-zero homogeneous denominator.
    if (!ClipEdgeBoundary(Start, End, Start.w - 1e-5, End.w - 1e-5)) return Pixel;
    if (!ClipEdgeBoundary(Start, End, Start.z, End.z)) return Pixel;
    static const uint Corners[6] = {0, 1, 2, 2, 1, 3};
    uint Corner = Corners[VertexIndex];
    float2 Direction = (End.xy / End.w - Start.xy / Start.w) * Projection.DisplayStroke.xy * 0.5;
    float Length = length(Direction);
    if (Length < 1e-5) return Pixel;
    float2 Perpendicular = float2(-Direction.y, Direction.x) / Length;
    float Side = (Corner == 1 || Corner == 3) ? 1.0 : -1.0;
    float Radius = Projection.DisplayStroke.z * 0.5 + 1.0;
    Pixel.Position = Corner < 2 ? Start : End;
    Pixel.Position.xy += Perpendicular * Side * Radius * 2.0 / Projection.DisplayStroke.xy * Pixel.Position.w;
    Pixel.Distance = Side * Radius;
    return Pixel;
}

[shader("fragment")]
float4 CadEdgeFragment(EdgePixel Pixel) : SV_Target
{
    float Radius = Projection.DisplayStroke.z * 0.5;
    float Coverage = 1.0 - smoothstep(Radius, Radius + 1.0, abs(Pixel.Distance));
    return float4(Projection.EdgeColour.rgb, Projection.EdgeColour.a * Coverage);
}
