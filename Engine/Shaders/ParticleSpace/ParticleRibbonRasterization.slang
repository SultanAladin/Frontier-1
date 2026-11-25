/*============================================================================================================================================
                                                    PARTICLERIBBONRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Pure .slang screen-space extruded Catmull-Rom spline ribbons (1..4 subdivisions, taper, fade, linger, halo glow)
// Reference: Experimental/FluidEditor/Engine/ParticleShaders.js L994-L1087 (TrailVertex) & L1335-L1339

import ForceFieldNoise;
import ParticleShapeAnchor;
import ParticleShadingCommon;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DATA & UNIFORM BLOCKS
//------------------------------------------------------------------------------------------------------------------------

public struct ParticleStateRecord
{
    public float4 PositionAge;  // xyz = world position (m), w = age (s)
    public float4 VelocitySeed; // xyz = velocity (m/s), w = seed [0..1]
};

/// Matches C++ ParticleRibbonUniformBlock byte-for-byte (512 bytes, 16-byte aligned).
public struct ParticleRibbonUniformBlock
{
    public ParticleShapeUniformBlock Shape;
    public ParticleShadeUniformBlock Shade;
    public float4x4 ViewProjection;       // Row-major world -> clip
    public float4 CameraPositionFocal;    // xyz = cameraPosition, w = focalPixels
    public float4 CameraForwardViewportX; // xyz = cameraForward, w = viewportWidthPx
    public float4 ViewportYTrailLayout;   // x = viewportHeightPx, y = trailLength, z = headSlot, w = subdivisions (1..4)
    public float4 RibbonWidthTaper;       // x = stride, y = width, z = sizeMode (0=World, 1=Pixels), w = taper
    public float4 RibbonFadeColourHalo;   // x = trailFade, y = trailColour (0=Particle, 1=Along), z = haloScale, w = haloAlpha
};

public struct RibbonVertexOutput
{
    float4 ClipPosition : SV_Position;
    float4 Colour       : COLOR0;
    float  Across       : TEXCOORD0; // [-1..+1] across ribbon width
};

[[vk::binding(0, 0)]] ConstantBuffer<ParticleRibbonUniformBlock> uParams;
[[vk::binding(1, 0)]] StructuredBuffer<ParticleStateRecord>      gStates;
[[vk::binding(2, 0)]] StructuredBuffer<float4>                   gTrailHistory;
[[vk::binding(3, 0)]] StructuredBuffer<float4>                   gAnchorTexels;

//------------------------------------------------------------------------------------------------------------------------
//                                               SPLINE & HISTORY HELPERS
//------------------------------------------------------------------------------------------------------------------------

float3 TrailPoint(int particle, int k, int trailLength, int head)
{
    if (k <= 0) return gStates[particle].PositionAge.xyz;
    int offset = head - (k - 1);
    int slot = ((offset % trailLength) + trailLength) % trailLength;
    return gTrailHistory[particle * trailLength + slot].xyz;
}

public float3 EvaluateCatmullRom(float3 p0, float3 p1, float3 p2, float3 p3, float u)
{
    float u2 = u * u;
    float u3 = u2 * u;
    return 0.5 * (
        (2.0 * p1)
        + (-p0 + p2) * u
        + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * u2
        + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * u3);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                VERTEX & FRAGMENT ENTRIES
//------------------------------------------------------------------------------------------------------------------------

[shader("vertex")]
RibbonVertexOutput ParticleRibbonVertexEntry(uint vertexId : SV_VertexID)
{
    int trailLength  = max(2, int(uParams.ViewportYTrailLayout.y + 0.5));
    int head         = int(uParams.ViewportYTrailLayout.z + 0.5);
    int subdivisions = max(1, int(uParams.ViewportYTrailLayout.w + 0.5));
    int stride       = max(1, int(uParams.RibbonWidthTaper.x + 0.5));

    int segments    = trailLength * subdivisions;
    int perParticle = segments * 6;
    int slotIndex   = int(vertexId) / perParticle;
    int particle    = slotIndex * stride;
    int rest        = int(vertexId) - slotIndex * perParticle;
    int segment     = rest / 6;
    int corner      = rest - segment * 6;

    ParticleStateRecord state = gStates[particle];
    ParticleShadeResult shaded = EvaluateParticleShade(
        uParams.Shape, uParams.Shade, gAnchorTexels, particle, state.PositionAge, state.VelocitySeed);

    float alpha = shaded.Linger * shaded.Flicker * smoothstep(0.0, 0.03, shaded.Life);
    float3 colour = shaded.Colour;

    int span = segment / subdivisions;
    int sub  = segment - span * subdivisions;
    float uA = float(sub) / float(subdivisions);
    float uB = float(sub + 1) / float(subdivisions);
    int kPrev = max(span - 1, 0);
    int kNext = min(span + 2, trailLength);

    float3 p0 = TrailPoint(particle, kPrev, trailLength, head);
    float3 p1 = TrailPoint(particle, span, trailLength, head);
    float3 p2 = TrailPoint(particle, span + 1, trailLength, head);
    float3 p3 = TrailPoint(particle, kNext, trailLength, head);

    float3 a = EvaluateCatmullRom(p0, p1, p2, p3, uA);
    float3 b = EvaluateCatmullRom(p0, p1, p2, p3, uB);

    bool endVertex = (corner == 2 || corner == 4 || corner == 5);
    float side = (corner == 0 || corner == 3 || corner == 5) ? -1.0 : 1.0;
    float along = (float(span) + (endVertex ? uB : uA)) / float(trailLength);
    float3 world = endVertex ? b : a;

    float4 clipA = mul(uParams.ViewProjection, float4(a, 1.0));
    float4 clipB = mul(uParams.ViewProjection, float4(b, 1.0));
    float4 clip  = endVertex ? clipB : clipA;

    float2 viewport = max(float2(uParams.CameraForwardViewportX.w, uParams.ViewportYTrailLayout.x), float2(1.0, 1.0));
    float2 ndcA = clipA.xy / max(clipA.w, 1.0e-4);
    float2 ndcB = clipB.xy / max(clipB.w, 1.0e-4);
    float2 screenDirection = (ndcB - ndcA) * viewport;
    float directionLength = length(screenDirection);
    float2 normal = (directionLength > 1.0e-4)
        ? (float2(-screenDirection.y, screenDirection.x) / directionLength)
        : float2(0.0, 1.0);

    float depth = dot(world - uParams.CameraPositionFocal.xyz, uParams.CameraForwardViewportX.xyz);
    int sizeMode = int(uParams.RibbonWidthTaper.z + 0.5);
    float widthBase = uParams.RibbonWidthTaper.y;
    float widthPixels = (sizeMode == 0)
        ? (widthBase * uParams.CameraPositionFocal.w / max(depth, 0.05))
        : widthBase;

    float taper     = uParams.RibbonWidthTaper.w;
    float haloScale = uParams.RibbonFadeColourHalo.z;
    float haloAlpha = uParams.RibbonFadeColourHalo.w;
    widthPixels = max(widthPixels * lerp(1.0, 1.0 - along, taper) * haloScale, 0.75);
    clip.xy += normal * side * (widthPixels / viewport) * clip.w;

    int trailColourMode = int(uParams.RibbonFadeColourHalo.y + 0.5);
    if (trailColourMode == 1)
    {
        colour = SampleSwatchGradient(uParams.Shade, along) * uParams.Shade.ColourJitterCycle.w * (0.85 + 0.3 * shaded.Flicker);
    }

    float trailFade = uParams.RibbonFadeColourHalo.x;
    alpha *= pow(max(1.0 - along, 0.0), max(trailFade, 0.05)) * haloAlpha;
    if (depth <= 0.05 || distance(p1, p2) > 2.0) alpha = 0.0;

    RibbonVertexOutput o;
    o.ClipPosition = clip;
    o.Colour       = float4(colour, alpha);
    o.Across       = side;
    return o;
}

[shader("fragment")]
float4 ParticleRibbonFragmentEntry(RibbonVertexOutput input) : SV_Target
{
    if (input.Colour.a <= 0.0) discard;
    float alpha = input.Colour.a * (1.0 - input.Across * input.Across);
    return float4(input.Colour.rgb, alpha);
}
