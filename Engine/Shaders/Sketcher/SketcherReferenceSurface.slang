/*====================================================================================================================================
                                                   SKETCHERREFERENCESURFACE.SLANG
====================================================================================================================================*/
// 🧩 High-poly GPU reference vehicle shading (automotive studio clay/metallic, MatCap, Zebra, ghost X-ray) + GPU crease lines

#include "Engine/Shaders/Sketcher/SketcherProjection.slang"
#include "Engine/Shaders/Sketcher/SketcherCadStroke.slang"
#include "Engine/Shaders/Sketcher/SketcherMatCapLookup.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUSH CONSTANTS & BINDINGS
//------------------------------------------------------------------------------------------------------------------------

// 📝 128-byte push block matching SketcherReferencePush in SketcherRasterization.h.
//    ViewRight.w   = orthographic condition (0 perspective, 1 ortho)
//    ViewUp.w      = zebra stripe count (0 off)
//    ViewForward.w = surface opacity (0.1 .. 1.0)
//    SurfaceTintMode   = (R, G, B, MatcapLayer) for surface draws; (Width, Height, StrokePx, CreaseOpacity) for crease draws.
struct SketcherReferencePush
{
    column_major float4x4 ClipFromWorld;
    float4                ViewRight;
    float4                ViewUp;
    float4                ViewForward;
    float4                SurfaceTintMode;
};

[[vk::push_constant]] ConstantBuffer<SketcherReferencePush> ReferencePush;
[[vk::binding(2, 0)]] StructuredBuffer<float4> Section;
[[vk::binding(0, 1)]] Sampler2DArray MatcapArray;

static const uint IdentityReferenceBit = 0x40000000u;                                        // Non-overlapping with B-rep faces/edges

//------------------------------------------------------------------------------------------------------------------------
//                                                    SURFACE STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ReferenceCornerInput
{
    [[vk::location(0)]] float3 Position   : POSITION0;
    [[vk::location(1)]] float3 Normal     : NORMAL0;
    [[vk::location(2)]] uint   PartIndex  : BLENDINDICES0;
    [[vk::location(3)]] uint   CreaseBits : BLENDINDICES1;
};

struct ReferenceSurfacePixel
{
    float4               Position   : SV_Position;
    float3               WorldPos   : TEXCOORD0;
    float3               WorldNorm  : TEXCOORD1;
    nointerpolation uint PartIndex  : BLENDINDICES0;
    float                CreaseGlow : TEXCOORD2;
};

struct ReferenceTargets
{
    float4 Colour   : SV_Target0;
    uint   Identity : SV_Target1;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    SURFACE SHADING
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
ReferenceSurfacePixel SketcherReferenceSurfaceVertex(ReferenceCornerInput Corner)
{
    ReferenceSurfacePixel Out;
    Out.Position   = mul(ReferencePush.ClipFromWorld, float4(Corner.Position, 1.0));
    Out.WorldPos   = Corner.Position;
    Out.WorldNorm  = Corner.Normal;
    Out.PartIndex  = Corner.PartIndex;
    Out.CreaseGlow = (Corner.CreaseBits & 1u) != 0u ? 1.0 : 0.0;
    return Out;
}

float3 AutomotiveStudioShading(float3 N, float3 V, float3 Right, float3 Up, float3 AlbedoTint)
{
    // Studio 3-point automotive surfacing rig: warm key, cool fill, crisp overhead specular ribbon, Fresnel contour
    const float3 KeyDir  = normalize(Right * 0.42 + Up * 0.72 + V * 0.56);
    const float3 FillDir = normalize(-Right * 0.65 + Up * 0.25 + V * 0.72);
    const float3 RimDir  = normalize(-Right * 0.30 + Up * 0.85 - V * 0.42);

    const float KeyDiffuse  = max(dot(N, KeyDir), 0.0);
    const float FillDiffuse = max(dot(N, FillDir), 0.0);
    const float SkyHem      = 0.5 + 0.5 * dot(N, Up);

    const float3 HKey  = normalize(KeyDir + V);
    const float3 HRim  = normalize(RimDir + V);
    const float  Spec1 = pow(max(dot(N, HKey), 0.0), 48.0);
    const float  Spec2 = pow(max(dot(N, HRim), 0.0), 22.0);
    const float  Fres  = pow(1.0 - max(dot(N, V), 0.0), 3.2);

    const float3 Ambient = lerp(float3(0.11, 0.13, 0.17), float3(0.24, 0.27, 0.32), SkyHem);
    const float3 Diffuse = AlbedoTint * (Ambient + KeyDiffuse * float3(0.76, 0.75, 0.73) + FillDiffuse * float3(0.24, 0.28, 0.35));
    const float3 Spec    = float3(0.36, 0.38, 0.42) * Spec1 + float3(0.14, 0.19, 0.26) * Spec2;
    const float3 Contour = float3(0.16, 0.24, 0.34) * Fres;
    return saturate(Diffuse + Spec + Contour);
}

[shader("fragment")]
ReferenceTargets SketcherReferenceSurfaceFragment(ReferenceSurfacePixel Pixel, bool FrontFacing : SV_IsFrontFace)
{
    if (Section[1].w > 0.5 && dot(Section[0].xyz, Pixel.WorldPos) + Section[0].w < 0.0) discard;

    float3 N = normalize(Pixel.WorldNorm);
    if (!FrontFacing) N = -N;

    const float3 V = ReferencePush.ViewRight.w > 0.5
                   ? normalize(-ReferencePush.ViewForward.xyz)
                   : normalize(Section[1].xyz - Pixel.WorldPos);
    if (dot(N, V) < 0.0) N = -N;

    float3 Colour;
    const float ZebraCount = ReferencePush.ViewUp.w;
    const uint  MatcapSlot = uint(max(ReferencePush.SurfaceTintMode.w, 0.0) + 0.5);

    if (ZebraCount > 0.5)
    {
        const float3 R = reflect(-V, N);
        const float  Band = cos(dot(R, ReferencePush.ViewUp.xyz) * 3.14159265 * ZebraCount);
        const float  Edge = max(fwidth(Band), 0.002);
        const float  Stripe = smoothstep(-Edge, Edge, Band);
        Colour = lerp(float3(0.08, 0.10, 0.13), float3(0.92, 0.94, 0.97), Stripe);
    }
    else if (MatcapSlot > 0u)
    {
        const float3 ViewNormal = float3(
            dot(N, ReferencePush.ViewRight.xyz),
            dot(N, ReferencePush.ViewUp.xyz),
            dot(N, -ReferencePush.ViewForward.xyz));
        const float2 Uv = MatCapCoordinates(ViewNormal);
        Colour = MatcapArray.Sample(float3(Uv, float(MatcapSlot - 1u))).rgb;
    }
    else
    {
        Colour = AutomotiveStudioShading(N, V, ReferencePush.ViewRight.xyz, ReferencePush.ViewUp.xyz, ReferencePush.SurfaceTintMode.rgb);
    }

    ReferenceTargets Out;
    Out.Colour   = float4(Colour, clamp(ReferencePush.ViewForward.w, 0.05, 1.0));
    Out.Identity = IdentityReferenceBit | (Pixel.PartIndex + 1u);
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GPU CREASE / FEATURE LINES
//------------------------------------------------------------------------------------------------------------------------

struct ReferenceCreaseInput
{
    [[vk::location(0)]] float3 Start     : POSITION0;
    [[vk::location(1)]] float3 End       : POSITION1;
    [[vk::location(2)]] uint   PartIndex : BLENDINDICES0;
};

struct ReferenceCreasePixel
{
    float4              Position : SV_Position;
    noperspective float Distance : TEXCOORD0;
};

float ReferenceWorldPerPixel(float3 Point, float4 Clip)
{
    const float4 Step   = mul(ReferencePush.ClipFromWorld, float4(ReferencePush.ViewUp.xyz, 0.0));
    const float  Slope  = (Step.y * Clip.w - Clip.y * Step.w) / max(Clip.w * Clip.w, 1.0e-12);
    const float  Pixels = abs(Slope) * 0.5 * ReferencePush.SurfaceTintMode.y;
    return 1.0 / max(Pixels, 1.0e-6);
}

float ReferenceEndpointPull(float3 Point, float4 Clip)
{
    const float Margin = 2.0 * ReferenceWorldPerPixel(Point, Clip);
    const float4 Near  = mul(ReferencePush.ClipFromWorld, float4(Point - ReferencePush.ViewForward.xyz * Margin, 1.0));
    if (Clip.w <= 1.0e-5 || Near.w <= 1.0e-5) return 0.0;
    return Clip.z / Clip.w - Near.z / Near.w;
}

[shader("vertex")]
ReferenceCreasePixel SketcherReferenceCreaseVertex(ReferenceCreaseInput Seg, uint VertexIndex : SV_VulkanVertexID)
{
    ReferenceCreasePixel Pixel;
    Pixel.Position = float4(-2.0, -2.0, 0.0, 1.0);
    Pixel.Distance = 1000.0;

    float3 Head = Seg.Start;
    float3 Tail = Seg.End;
    if (Section[1].w > 0.5)
    {
        const float HeadSide = dot(Section[0].xyz, Head) + Section[0].w;
        const float TailSide = dot(Section[0].xyz, Tail) + Section[0].w;
        if (HeadSide < 0.0 && TailSide < 0.0) return Pixel;
        const float3 Crossing = lerp(Head, Tail, HeadSide / (HeadSide - TailSide));
        if (HeadSide < 0.0) Head = Crossing;
        if (TailSide < 0.0) Tail = Crossing;
    }

    float4 Start = mul(ReferencePush.ClipFromWorld, float4(Head, 1.0));
    float4 End   = mul(ReferencePush.ClipFromWorld, float4(Tail, 1.0));
    const float StartPull = ReferenceEndpointPull(Head, Start);
    const float EndPull   = ReferenceEndpointPull(Tail, End);
    if (!ClipCadSegment(Start, End)) return Pixel;

    const float Radius = max(ReferencePush.SurfaceTintMode.z, 0.75) * 0.5;
    CadStrokeCorner Corner;
    if (!ExpandCadStroke(Start, End, VertexIndex, ReferencePush.SurfaceTintMode.xy, Radius + 0.75, Radius + 0.5, Corner)) return Pixel;

    Pixel.Position   = Corner.Position;
    const float Pull = Corner.EndCondition ? EndPull : StartPull;
    Pixel.Position.z = clamp(Pixel.Position.z - Pull * Pixel.Position.w, 0.0, Pixel.Position.w);
    Pixel.Distance   = Corner.Distance;
    return Pixel;
}

[shader("fragment")]
ReferenceTargets SketcherReferenceCreaseFragment(ReferenceCreasePixel Pixel)
{
    const float Radius   = max(ReferencePush.SurfaceTintMode.z, 0.75) * 0.5;
    const float Coverage = CadStrokeCoverage(Pixel.Distance, max(Radius - 0.5, 0.0), Radius + 0.5);
    if (Coverage < (1.0 / 255.0)) discard;

    ReferenceTargets Out;
    Out.Colour   = float4(0.11, 0.14, 0.18, Coverage * clamp(ReferencePush.SurfaceTintMode.w, 0.15, 0.95));
    Out.Identity = 0u;
    return Out;
}
