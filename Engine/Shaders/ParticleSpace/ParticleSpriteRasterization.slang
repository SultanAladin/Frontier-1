/*============================================================================================================================================
                                                    PARTICLESPRITERASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Pure .slang GPU rasterization of ParticleSpace billboards (18 analytical cutouts 0..17), streaks, DOF bokeh, and additive halo glow
// Reference: Experimental/FluidEditor/Engine/ParticleShaders.js L870-L966 (ParticleRenderVertex) & L1088-L1340 (ParticleFragment)

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

/// Matches C++ ParticleSpriteUniformBlock byte-for-byte (512 bytes, 16-byte aligned).
public struct ParticleSpriteUniformBlock
{
    public ParticleShapeUniformBlock Shape;
    public ParticleShadeUniformBlock Shade;
    public float4x4 ViewProjection;       // Row-major world -> clip projection
    public float4 CameraPositionFocal;    // xyz = cameraPosition, w = focalPixels
    public float4 CameraForwardViewportX; // xyz = cameraForward, w = viewportWidthPx
    public float4 ViewportYSizeParams;    // x = viewportHeightPx, y = sizeStart, z = sizeEnd, w = sizeRandom
    public float4 PixelDofParams;         // x = pixelRatio, y = sizeMode (0=WorldMeters, 1=Pixels), z = focusDistance, w = aperture
    public float4 HaloSpriteStyleTrail;   // x = haloScale, y = haloAlpha, z = sprite (0..17), w = style (0=Point, 1=Streak)
};

public struct SpriteVertexOutput
{
    float4 ClipPosition : SV_Position;
    float4 Colour       : COLOR0;
    float  Soft         : TEXCOORD0;
    float  Across       : TEXCOORD1;
    float4 Extra        : TEXCOORD2; // x = effectiveSprite, y = wingW, z = seed, w = isScanBar
    float2 PointCoord   : TEXCOORD3; // [0..1]^2 across quad
};

[[vk::binding(0, 0)]] ConstantBuffer<ParticleSpriteUniformBlock> uParams;
[[vk::binding(1, 0)]] StructuredBuffer<ParticleStateRecord>      gStates;
[[vk::binding(2, 0)]] StructuredBuffer<float4>                   gPreviousPositions;
[[vk::binding(3, 0)]] StructuredBuffer<float4>                   gAnchorTexels;

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHARED VERTEX HELPER
//------------------------------------------------------------------------------------------------------------------------

void EvaluateSpriteVertexCore(
    int particle,
    int streakEnd,
    out float3 outPosition,
    out float4 outColour,
    out float4 outExtra,
    out float  outPixelDiameter)
{
    ParticleStateRecord state = gStates[particle];
    float4 current  = state.PositionAge;
    float4 velocity = state.VelocitySeed;
    float4 previous = gPreviousPositions[particle];

    bool respawned = (current.w < previous.w) || (distance(current.xyz, previous.xyz) > 1.0);
    float3 position = (streakEnd == 1 && !respawned) ? previous.xyz : current.xyz;

    ParticleShadeResult shaded = EvaluateParticleShade(
        uParams.Shape, uParams.Shade, gAnchorTexels, particle, current, velocity);

    float simTime = uParams.Shade.TimingRegionHeight.y;
    float glitchAmt = uParams.Shape.GlitchParams.x;
    float glitchRate = uParams.Shape.GlitchParams.y;
    float glitchStep = uParams.Shape.GlitchParams.z;
    int derezMode = int(uParams.Shape.EffectorAxisDerez.y + 0.5);
    float derezMix = uParams.Shape.EffectorAxisDerez.z;

    if ((glitchAmt > 0.001 && glitchStep > 0.001) || (derezMode == 5 && shaded.DerezReleased > 0.5))
    {
        float rate = max(glitchRate, 1.0);
        float stepTick = floor(simTime * rate);
        float3 jSeed = float3(float(particle) * 0.13 + stepTick, velocity.w * 31.7 + stepTick, stepTick * 0.73);
        float gate = step(0.42 - glitchAmt * 0.35, Hash31(jSeed + 4.1));
        float amp = glitchAmt * glitchStep * gate + ((derezMode == 5 && shaded.DerezReleased > 0.5) ? (0.09 * derezMix) : 0.0);
        position += (float3(Hash31(jSeed), Hash31(jSeed + 7.3), Hash31(jSeed + 13.9)) - 0.5) * 2.0 * amp;
    }

    float speed = length(velocity.xyz);
    float alpha = shaded.Alpha;
    if (streakEnd == 1) alpha *= 0.3 * smoothstep(0.2, 2.5, speed);

    float sizeSeed = frac(velocity.w * 41.7);
    float sizeRandom = exp2((sizeSeed - 0.5) * 3.0 * uParams.ViewportYSizeParams.w);
    float size = lerp(uParams.ViewportYSizeParams.y, uParams.ViewportYSizeParams.z, shaded.Life) * sizeRandom;

    int effMode = int(uParams.Shape.MocapKickEffector.y + 0.5);
    float effStrength = uParams.Shape.MocapKickEffector.z;
    float effScaleBoost = uParams.Shape.EffectorMotion.z;
    if (effMode > 0 && effStrength > 0.0001 && abs(effScaleBoost) > 0.0001)
    {
        size *= max(0.08, 1.0 + shaded.EffectorWeight * effScaleBoost);
    }

    int effectiveSprite = int(uParams.HaloSpriteStyleTrail.z + 0.5);
    float haloScale = uParams.HaloSpriteStyleTrail.x;
    float haloAlpha = uParams.HaloSpriteStyleTrail.y;
    if (haloScale <= 1.05 && derezMode == 4 && derezMix > 0.01 && shaded.DerezReleased > 0.5)
    {
        effectiveSprite = 6;
        size *= 1.85;
    }

    float wingRate = max(uParams.Shape.DerezEdgeColourWing.w, 2.0) * (0.85 + 0.35 * frac(velocity.w * 11.3));
    float flap = cos(simTime * wingRate + velocity.w * 6.2831853);
    float wingW = 0.22 + 0.78 * abs(flap);
    float isScanBar = frac(velocity.w * 19.7) > 0.74 ? 1.0 : 0.0;

    float depth = dot(position - uParams.CameraPositionFocal.xyz, uParams.CameraForwardViewportX.xyz);
    int sizeMode = int(uParams.PixelDofParams.y + 0.5);
    float pixels = (sizeMode == 0)
        ? (size * uParams.CameraPositionFocal.w / max(depth, 0.05))
        : (size * uParams.PixelDofParams.x);

    float focusDist = uParams.PixelDofParams.z;
    float aperture  = uParams.PixelDofParams.w;
    float coc = (focusDist > 0.0) ? clamp(abs(depth - focusDist) / max(focusDist, 0.1) * aperture * 3.0, 0.0, 1.0) : 0.0;
    float blurGrow = 1.0 + coc * 6.0;
    pixels *= blurGrow * haloScale;
    alpha *= haloAlpha / (blurGrow * blurGrow * 0.6 + 0.4);
    if (depth <= 0.05) alpha = 0.0;

    outPosition = position;
    outColour = float4(shaded.Colour, alpha);
    outExtra = float4(float(effectiveSprite), wingW, velocity.w, isScanBar);
    outPixelDiameter = clamp(pixels, 1.0, 256.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                            QUAD & STREAK VERTEX ENTRIES
//------------------------------------------------------------------------------------------------------------------------

/// Expands each particle into a screen-space quad (6 vertices / particle) matching WebGL gl_PointSize 1..256px cleanly.
[shader("vertex")]
SpriteVertexOutput ParticleSpriteVertexEntry(uint vertexId : SV_VertexID)
{
    static const float2 kQuadCorners[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2( 1.0,  1.0),
        float2(-1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0)
    };
    static const float2 kQuadUvs[6] = {
        float2(0.0, 1.0), float2(1.0, 1.0), float2(1.0, 0.0),
        float2(0.0, 1.0), float2(1.0, 0.0), float2(0.0, 0.0)
    };

    int particle = int(vertexId / 6u);
    uint corner = vertexId % 6u;

    float3 pos;
    float4 col, extra;
    float pixelDia;
    EvaluateSpriteVertexCore(particle, 0, pos, col, extra, pixelDia);

    float4 clip = mul(uParams.ViewProjection, float4(pos, 1.0));
    float2 viewport = max(float2(uParams.CameraForwardViewportX.w, uParams.ViewportYSizeParams.x), float2(1.0, 1.0));
    float2 ndcOffset = kQuadCorners[corner] * (pixelDia / viewport);
    clip.xy += ndcOffset * clip.w;
    if (col.a <= 0.0) clip = float4(0.0, 0.0, 2.0, 1.0);

    SpriteVertexOutput o;
    o.ClipPosition = clip;
    o.Colour       = col;
    o.Soft         = 1.0;
    o.Across       = 0.0;
    o.Extra        = extra;
    o.PointCoord   = kQuadUvs[corner];
    return o;
}

/// Draws 2-vertex motion streak line segments between current and previous position (Style == Streak).
[shader("vertex")]
SpriteVertexOutput ParticleStreakVertexEntry(uint vertexId : SV_VertexID)
{
    int particle = int(vertexId / 2u);
    int endIdx = int(vertexId - uint(particle) * 2u);

    float3 pos;
    float4 col, extra;
    float pixelDia;
    EvaluateSpriteVertexCore(particle, endIdx, pos, col, extra, pixelDia);

    SpriteVertexOutput o;
    o.ClipPosition = mul(uParams.ViewProjection, float4(pos, 1.0));
    o.Colour       = col;
    o.Soft         = 0.0;
    o.Across       = 0.0;
    o.Extra        = extra;
    o.PointCoord   = float2(0.5, 0.5);
    return o;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       18 ANALYTICAL SPRITE CUTOUTS (L1088-L1340)
//------------------------------------------------------------------------------------------------------------------------

[shader("fragment")]
float4 ParticleSpriteFragmentEntry(SpriteVertexOutput input) : SV_Target
{
    if (input.Colour.a <= 0.0) discard;
    float3 rgb = input.Colour.rgb;
    float alpha = input.Colour.a;
    int sprite = int(uParams.HaloSpriteStyleTrail.z + 0.5);
    int style  = int(uParams.HaloSpriteStyleTrail.w + 0.5);
    bool butterflyMorph = (int(floor(input.Extra.x + 0.5)) == 6) && (sprite == 5);

    if (input.Soft > 0.5)
    {
        float2 q = input.PointCoord - 0.5;
        float d = length(q) * 2.0;

        if (sprite == 0)
        {
            if (d > 1.0) discard;
            alpha *= exp(-d * d * 4.5) * (1.0 - smoothstep(0.8, 1.0, d));
        }
        else if (sprite == 1)
        {
            if (d > 1.0) discard;
            alpha *= 1.0 - smoothstep(0.6, 1.0, d);
        }
        else if (sprite == 2)
        {
            if (d > 1.0) discard;
            alpha *= 0.22 + 0.78 * smoothstep(0.5, 0.78, d) * (1.0 - smoothstep(0.86, 1.0, d));
        }
        else if (sprite == 3)
        {
            float arms = max(exp(-abs(q.x) * 14.0), exp(-abs(q.y) * 14.0));
            alpha *= max(arms * (1.0 - d * 0.9), exp(-d * d * 10.0));
            if (alpha < 0.01) discard;
        }
        else if (sprite == 6 || butterflyMorph)
        {
            float2 uv = float2(q.x, -q.y) * 2.0;
            float wingW = max(input.Extra.y, 0.16);
            float x = abs(uv.x);
            float xs = x / wingW;
            float dF = length(float2((xs - 0.50) / 0.50, (uv.y - 0.28) / 0.36));
            float dH = length(float2((xs - 0.40) / 0.36, (uv.y + 0.28) / 0.26));
            float dMin = min(dF, dH);
            float wing = 1.0 - smoothstep(0.84, 1.0, dMin);
            float body = (1.0 - smoothstep(0.035, 0.065, x)) * step(abs(uv.y), 0.62);
            if (wing < 0.01 && body < 0.01) discard;
            float edge = 0.76 + 0.48 * (1.0 - min(dMin, 1.0));
            rgb = lerp(rgb * edge, float3(0.09, 0.09, 0.09), body * 0.85);
            alpha *= max(wing, body);
        }
        else if (sprite == 5)
        {
            float2 p = float2(q.x, -q.y) * 2.15;
            float hex = max(abs(p.x) * 0.8660254 + abs(p.y) * 0.5, abs(p.y));
            if (hex > 0.96) discard;
            float mask = 1.0 - smoothstep(0.88, 0.96, hex);
            bool topFace = p.y > abs(p.x) * 0.57735;
            float shade = topFace ? 1.14 : (p.x < 0.0 ? 0.80 : 0.54);
            float seam = min(abs(p.y - abs(p.x) * 0.57735), p.y < abs(p.x) * 0.57735 ? abs(p.x) : 1.0);
            float rim = smoothstep(0.80, 0.90, hex) * 0.28 + (1.0 - smoothstep(0.0, 0.06, seam)) * 0.22;
            rgb = rgb * shade + rgb * rim;
            alpha *= mask;
        }
        else if (sprite == 7)
        {
            float2 p = abs(q * 2.1);
            float hex = max(p.x * 0.8660254 + p.y * 0.5, p.y);
            if (hex > 0.98) discard;
            float rim = smoothstep(0.70, 0.88, hex) * (1.0 - smoothstep(0.90, 0.98, hex));
            float core = (1.0 - smoothstep(0.0, 0.88, hex)) * 0.34;
            rgb = rgb * (0.65 + 1.15 * rim);
            alpha *= clamp(core + rim * 1.15, 0.0, 1.0);
        }
        else if (sprite == 8)
        {
            float2 uv = q * 2.0;
            if (input.Extra.w > 0.5)
            {
                if (abs(uv.y) > 0.24 || abs(uv.x) > 0.96) discard;
                float scan = 0.7 + 0.3 * sin(uv.x * 18.0 + input.Extra.z * 40.0);
                alpha *= (1.0 - smoothstep(0.14, 0.24, abs(uv.y))) * scan;
            }
            else
            {
                float2 grid = floor((q + 0.5) * 5.0);
                float symX = min(grid.x, 4.0 - grid.x);
                float cellHash = frac(sin(dot(float2(symX, grid.y) + floor(input.Extra.z * 31.0), float2(12.9898, 78.233))) * 43758.5453);
                float2 local = frac((q + 0.5) * 5.0) - 0.5;
                float box = step(max(abs(local.x), abs(local.y)), 0.38);
                float on = step(0.36, cellHash) * box;
                if (on < 0.01) discard;
                alpha *= on * (0.82 + 0.18 * sin(q.y * 36.0));
            }
        }
        else if (sprite == 9)
        {
            float2 uv = q * 2.0;
            float r = length(uv);
            if (r > 0.98) discard;
            float a = atan2(uv.y, uv.x) + 3.14159265;
            float arm = pow(abs(cos(a * 3.0)), (r > 0.16 ? 22.0 : 2.0));
            float branch = pow(abs(cos(a * 3.0 + 1.05)), 26.0) * smoothstep(0.26, 0.52, r) * (1.0 - smoothstep(0.62, 0.94, r));
            float hub = 1.0 - smoothstep(0.06, 0.20, r);
            float arms = max(arm * (1.0 - smoothstep(0.72, 0.98, r)), branch * 0.82);
            float mask = max(max(arms, hub), 0.0);
            if (mask < 0.012) discard;
            rgb = lerp(rgb, lerp(rgb, float3(1.0, 1.0, 1.0), 0.35), hub);
            alpha *= clamp(mask, 0.0, 1.0) * (0.72 + 0.55 * mask);
        }
        else if (sprite == 10)
        {
            float2 uv = float2(q.x * 2.0, -q.y * 2.0);
            float t = clamp((uv.y + 0.85) / 1.75, 0.0, 1.0);
            float halfW = 1.43 * sqrt(t) * pow(max(0.0, 1.0 - t), 1.35);
            float across = abs(uv.x) - halfW;
            float inY = smoothstep(-0.90, -0.82, uv.y) * (1.0 - smoothstep(0.86, 0.94, uv.y));
            float mask = (1.0 - smoothstep(-0.04, 0.04, across)) * inY;
            if (mask < 0.01 || halfW < 0.004) discard;
            float rim = smoothstep(0.45, 0.96, abs(uv.x) / max(halfW, 0.01));
            float bead = 1.0 - smoothstep(0.0, 0.28, length(uv - float2(-0.14, -0.32)));
            float coreShade = 0.76 + 0.26 * (1.0 - t) + 0.28 * rim;
            rgb = lerp(rgb * coreShade, float3(1.0, 1.0, 1.0), bead * 0.65);
            alpha *= mask;
        }
        else if (sprite == 11)
        {
            float2 uv = float2(q.x, -q.y) * 2.0;
            float width = (1.0 - uv.y * uv.y) * 0.62;
            float serration = 0.055 * sin(uv.y * 26.0);
            float across = abs(uv.x) - width - serration * step(0.0, width);
            float mask = (1.0 - smoothstep(-0.03, 0.05, across)) * step(abs(uv.y), 1.0);
            if (mask < 0.01) discard;
            float rib = 1.0 - smoothstep(0.0, 0.05 + abs(uv.y) * 0.03, abs(uv.x));
            rgb = lerp(rgb * (0.72 + 0.42 * (1.0 - abs(uv.x) / max(width, 1.0e-3))), rgb * 1.5 + 0.10, rib * 0.7);
            alpha *= mask;
        }
        else if (sprite == 12)
        {
            float2 uv = float2(q.x, -q.y) * 2.0;
            float lobe = length(float2(uv.x / 0.66, (uv.y - 0.02) / 0.90));
            float notch = length(float2(uv.x / 0.16, (uv.y - 0.96) / 0.22));
            float mask = (1.0 - smoothstep(0.86, 1.0, lobe)) * smoothstep(0.30, 0.62, notch + 0.34);
            if (mask < 0.01) discard;
            float vein = 1.0 - smoothstep(0.0, 0.09, abs(uv.x + sin(uv.y * 3.0) * 0.05));
            rgb = lerp(rgb * 0.90, rgb * 1.35 + 0.06, vein * 0.6);
            alpha *= mask;
        }
        else if (sprite == 13)
        {
            float2 p = float2(q.x * 2.0, -q.y * 2.0);
            float hex = max(abs(p.x) * 0.8660254 + abs(p.y) * 0.5, abs(p.y));
            float taper = 1.0 - smoothstep(0.55, 1.0, abs(p.y)) * 0.22;
            if (hex > 0.96 * taper) discard;
            float mask = 1.0 - smoothstep(0.88, 0.96 * taper, hex);
            float faceA = smoothstep(0.0, 0.55, p.x + 0.30);
            float faceB = smoothstep(0.0, 0.55, -p.x + 0.30);
            float faceC = smoothstep(0.10, 0.35, p.y);
            float shade = 0.62 + 0.62 * faceA + 0.28 * faceB + 0.34 * faceC;
            float edge = smoothstep(0.78, 0.92, hex) * 0.55;
            rgb = rgb * shade + float3(edge, edge, edge) * rgb;
            alpha *= mask;
        }
        else if (sprite == 14)
        {
            float2 uv = q * 2.0;
            float r = length(uv);
            if (r > 1.0) discard;
            float ring = exp(-pow((r - 0.78) / 0.10, 2.0));
            float inner = exp(-pow((r - 0.46) / 0.20, 2.0)) * 0.28;
            float mask = ring + inner;
            if (mask < 0.012) discard;
            rgb = lerp(rgb * 0.65, float3(1.0, 1.0, 1.0), ring * 0.55);
            alpha *= clamp(mask, 0.0, 1.0);
        }
        else if (sprite == 15)
        {
            float2 uv = q * 2.0;
            float r = length(uv);
            if (r > 1.0) discard;
            float a = atan2(uv.y, uv.x);
            if (style == 1)
            {
                float pulse = frac(r * 3.0 - input.Extra.z * 2.4);
                float wave = exp(-pow((pulse - 0.14) / 0.10, 2.0));
                float crossLines = (1.0 - smoothstep(0.015, 0.04, abs(uv.x))) * 0.25 + (1.0 - smoothstep(0.015, 0.04, abs(uv.y))) * 0.25;
                float rimNotch = step(0.84, r) * step(0.86, abs(cos(a * 12.0))) * 0.55;
                float mask = max(wave * (1.0 - r * 0.35), max(crossLines * step(0.12, r), rimNotch));
                if (mask < 0.012) discard;
                rgb = lerp(rgb, float3(0.45, 1.0, 0.90), wave * 0.65);
                alpha *= clamp(mask, 0.0, 1.0) * (1.0 - smoothstep(0.92, 1.0, r));
            }
            else
            {
                float sweep = input.Extra.z * 6.2831853;
                float rawWrap = a - sweep + 12.5663706;
                float wrapped = rawWrap - 6.2831853 * floor(rawWrap / 6.2831853);
                float wake = smoothstep(0.0, 1.9, wrapped) * step(wrapped, 2.6);
                float leading = exp(-pow(wrapped / 0.14, 2.0));
                float rings = 0.22 * smoothstep(0.38, 0.5, abs(frac(r * 3.0) - 0.5));
                float2 dA = uv - float2(0.42, 0.36);
                float2 dB = uv - float2(-0.52, -0.22);
                float blipA = exp(-dot(dA, dA) * 180.0);
                float blipB = exp(-dot(dB, dB) * 180.0);
                float blips = (blipA + blipB) * (0.45 + 0.55 * wake);
                float mask = max(max(leading, wake * 0.42), blips) + rings * step(0.18, r);
                if (mask < 0.01) discard;
                rgb = lerp(rgb, float3(0.65, 1.0, 0.85), max(leading * 0.55, blips * 0.85));
                alpha *= clamp(mask, 0.0, 1.0) * (1.0 - smoothstep(0.92, 1.0, r));
            }
        }
        else if (sprite == 16)
        {
            float2 uv = float2(q.x * 2.0, -q.y * 2.0);
            float ribbonA = sin(uv.x * 5.0 + input.Extra.z * 4.0) * 0.30;
            float ribbonB = sin(uv.x * 9.0 - input.Extra.z * 6.5) * 0.16;
            float band = 1.0 - smoothstep(0.10, 0.62, abs(uv.y - ribbonA - ribbonB));
            float verticalFade = smoothstep(-1.0, 0.55, uv.y) * (1.0 - smoothstep(0.72, 1.0, uv.y));
            float mask = band * verticalFade;
            if (mask < 0.012) discard;
            rgb = lerp(rgb, lerp(float3(0.22, 0.95, 0.72), float3(0.55, 0.42, 1.0), smoothstep(-0.4, 0.8, uv.y)), 0.45);
            alpha *= mask * 0.92;
        }
        else if (sprite == 17)
        {
            float2 uv = q * 2.0;
            float r = length(uv);
            if (r > 1.0) discard;
            float rim = smoothstep(0.72, 0.82, r) * (1.0 - smoothstep(0.88, 0.96, r));
            float milled = 0.5 + 0.5 * sin(atan2(uv.y, uv.x) * 44.0);
            float boss = 1.0 - smoothstep(0.16, 0.34, r);
            float face = 1.0 - smoothstep(0.94, 1.0, r);
            float relief = 0.86 + 0.24 * smoothstep(0.18, 0.66, r) + 0.26 * boss;
            rgb = rgb * relief + float3(rim * milled * 0.42, rim * milled * 0.42, rim * milled * 0.42) + float3(boss * 0.22, boss * 0.22, boss * 0.22);
            alpha *= face;
        }
    }
    else if (input.Soft < -0.5)
    {
        alpha *= 1.0 - input.Across * input.Across;
    }

    return float4(rgb, alpha);
}
