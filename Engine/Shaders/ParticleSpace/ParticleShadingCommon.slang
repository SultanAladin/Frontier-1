/*============================================================================================================================================
                                                       PARTICLESHADINGCOMMON.SLANG
============================================================================================================================================*/
// 🧩 Shared pure .slang analytic colour ramps (Turbo/Engineering/Plasma/Diverging) and per-particle shading (7 modes, derez glow, flicker)
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L1077-L1122 & ParticleShaders.js L753-L869

module ParticleShadingCommon;

import ForceFieldNoise;
import ParticleShapeAnchor;

//------------------------------------------------------------------------------------------------------------------------
//                                                    UNIFORM BLOCK LAYOUT
//------------------------------------------------------------------------------------------------------------------------

/// Matches C++ ParticleShadeUniformBlock byte-for-byte (192 bytes, 16-byte aligned).
public struct ParticleShadeUniformBlock
{
    public float4 SwatchStops[8];       // rgb = sRGB stop colour, w = unused
    public float4 ModeRampSwatch;       // x = colourMode (0..6), y = rampMode (0..3), z = rampSource (0..1), w = swatchCount (1..8)
    public float4 ColourJitterCycle;    // x = hueJitter, y = brightnessJitter, z = cycleSpeed, w = brightness
    public float4 LifeRangeFlicker;     // x = lifetime, y = linger, z = maxSpeed, w = displacementRange
    public float4 TimingRegionHeight;   // x = flicker, y = simulationTime, z = regionHeight, w = reserved
};

public struct ParticleShadeResult
{
    public float3 Colour;
    public float  Life;
    public float  Alpha;
    public float  Flicker;
    public float  Linger;
    public float  DerezReleased;
    public float  EffectorWeight;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               ANALYTIC COLOUR RAMPS (L1077-L1122)
//------------------------------------------------------------------------------------------------------------------------

public float3 TurboRamp(float t)
{
    t = clamp(t, 0.0, 1.0);
    static const float4 kR  = float4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
    static const float4 kG  = float4(0.09140261, 2.19418839,   4.84296658, -14.18503333);
    static const float4 kB  = float4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
    static const float2 kR2 = float2(-152.94239396, 59.28637943);
    static const float2 kG2 = float2(   4.27729857,  2.82956604);
    static const float2 kB2 = float2( -89.90310912, 27.34824973);
    float4 v4 = float4(1.0, t, t * t, t * t * t);
    float2 v2 = v4.zw * v4.z;
    return float3(dot(v4, kR) + dot(v2, kR2), dot(v4, kG) + dot(v2, kG2), dot(v4, kB) + dot(v2, kB2));
}

public float3 PlasmaRamp(float t)
{
    t = clamp(t, 0.0, 1.0);
    float3 c0 = float3(0.0509, 0.0298, 0.5279);
    float3 c1 = float3(2.1763, 0.2383, 0.7539);
    float3 c2 = float3(-2.6895, -7.4559, 3.1107);
    float3 c3 = float3(6.1303, 42.3461, -28.5188);
    float3 c4 = float3(-11.1074, -82.6663, 60.1398);
    float3 c5 = float3(10.0231, 71.4136, -54.0721);
    float3 c6 = float3(-3.6587, -22.9315, 18.1919);
    return c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
}

public float3 EngineeringRamp(float t)
{
    t = clamp(t, 0.0, 1.0) * 6.0;
    static const float3 stops[7] = {
        float3(0.043, 0.122, 0.227), float3(0.071, 0.318, 0.541), float3(0.102, 0.627, 0.784),
        float3(0.373, 0.816, 0.627), float3(0.898, 0.890, 0.416), float3(0.949, 0.545, 0.176),
        float3(0.851, 0.157, 0.110)
    };
    int i = int(min(floor(t), 5.0));
    return lerp(stops[i], stops[i + 1], frac(t));
}

public float3 DivergingRamp(float t)
{
    t = clamp(t, 0.0, 1.0) * 6.0;
    static const float3 stops[7] = {
        float3(0.129, 0.400, 0.675), float3(0.404, 0.663, 0.812), float3(0.820, 0.898, 0.941),
        float3(0.969, 0.969, 0.969), float3(0.992, 0.859, 0.780), float3(0.937, 0.541, 0.384),
        float3(0.698, 0.094, 0.169)
    };
    int i = int(min(floor(t), 5.0));
    return lerp(stops[i], stops[i + 1], frac(t));
}

public float3 EvaluateAnalyticRamp(int mode, float t)
{
    if (mode == 1) return EngineeringRamp(t);
    if (mode == 2) return PlasmaRamp(t);
    if (mode == 3) return DivergingRamp(t);
    return TurboRamp(t);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SWATCH & HUE HELPERS (L772-L802)
//------------------------------------------------------------------------------------------------------------------------

public float3 HueToRgb(float h)
{
    float3 raw = float3(5.0, 3.0, 1.0) + h * 6.0;
    float3 k = raw - 6.0 * floor(raw / 6.0);
    return 1.0 - clamp(min(k, 4.0 - k), 0.0, 1.0) * 0.85;
}

public float3 SampleSwatchGradient(ParticleShadeUniformBlock shade, float t)
{
    int count = max(int(shade.ModeRampSwatch.w + 0.5), 1);
    if (count == 1) return shade.SwatchStops[0].rgb;
    float x = clamp(t, 0.0, 1.0) * float(count - 1);
    int i = int(min(floor(x), float(count - 2)));
    return lerp(shade.SwatchStops[i].rgb, shade.SwatchStops[i + 1].rgb, x - float(i));
}

public float3 SampleSwatchEntry(ParticleShadeUniformBlock shade, float seed)
{
    int count = max(int(shade.ModeRampSwatch.w + 0.5), 1);
    int i = int(min(floor(seed * float(count)), float(count - 1)));
    return shade.SwatchStops[i].rgb;
}

public float3 SampleColourGradient(ParticleShadeUniformBlock shade, float t)
{
    int rampSource = int(shade.ModeRampSwatch.z + 0.5);
    int rampMode   = int(shade.ModeRampSwatch.y + 0.5);
    return (rampSource == 1) ? EvaluateAnalyticRamp(rampMode, t) : SampleSwatchGradient(shade, t);
}

public float3 HueRotate(float3 colour, float angle)
{
    static const float3 k = float3(0.57735, 0.57735, 0.57735);
    float c = cos(angle), s = sin(angle);
    return colour * c + cross(k, colour) * s + k * dot(k, colour) * (1.0 - c);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              PER-PARTICLE SHADING (L804-L868)
//------------------------------------------------------------------------------------------------------------------------

public ParticleShadeResult EvaluateParticleShade(
    ParticleShapeUniformBlock shape,
    ParticleShadeUniformBlock shade,
    StructuredBuffer<float4> anchorTexels,
    int particle,
    float4 current,
    float4 velocity)
{
    ParticleShadeResult res;
    float simTime = shade.TimingRegionHeight.y;
    int mode = int(shape.ShapeCounts.w + 0.5);
    int count = max(int(shape.ShapeCounts.y + 0.5), 1);
    int shapeCode = int(shape.ShapeCounts.x + 0.5);
    int anchorCount = int(shape.AnchorLayout.y + 0.5);

    float3 limbVel;
    float3 restHome = ShapePointAtTime(shape, anchorTexels, particle, float3(0.0, 0.0, 0.0), true, simTime, limbVel);
    res.EffectorWeight = EvaluateEffector(shape, restHome, simTime);
    DerezEvaluationResult derez = EvaluateDerez(shape, restHome, simTime);
    res.DerezReleased = derez.Released ? 1.0 : 0.0;

    float lifetime = shade.LifeRangeFlicker.x;
    float linger   = shade.LifeRangeFlicker.y;
    float alive = ((mode == 1 || current.w < lifetime) && particle < count) ? 1.0 : 0.0;
    res.Life = (mode == 1) ? 0.5 : clamp(current.w / max(lifetime, 1.0e-3), 0.0, 1.0);

    int colourMode = int(shade.ModeRampSwatch.x + 0.5);
    float speed = length(velocity.xyz);
    float3 colour;

    if (colourMode == 1)
    {
        float t = clamp(speed / max(shade.LifeRangeFlicker.z, 1.0e-3), 0.0, 1.0);
        colour = SampleColourGradient(shade, t);
    }
    else if (colourMode == 2)
    {
        float t = clamp(distance(current.xyz, restHome) / max(shade.LifeRangeFlicker.w, 1.0e-3), 0.0, 1.0);
        colour = SampleColourGradient(shade, t);
    }
    else if (colourMode == 3)
    {
        float t = clamp(current.y / max(shade.TimingRegionHeight.z, 1.0e-3) + 0.5, 0.0, 1.0);
        colour = SampleColourGradient(shade, t);
    }
    else if (colourMode == 4)
    {
        colour = HueToRgb(velocity.w);
    }
    else if (colourMode == 5)
    {
        colour = SampleSwatchEntry(shade, frac(velocity.w * 7.31 + 0.137));
    }
    else if (colourMode == 6)
    {
        float t = frac(simTime * shade.ColourJitterCycle.z + velocity.w * 0.35 + float(particle) / float(count) * 0.5);
        colour = SampleSwatchGradient(shade, abs(t * 2.0 - 1.0));
    }
    else
    {
        colour = SampleSwatchGradient(shade, res.Life);
    }

    if (shapeCode == 15 && anchorCount > 0)
    {
        float3 fwTint;
        ObjectAnchorPosition(anchorTexels, anchorCount, particle, fwTint);
        colour = lerp(colour, fwTint * (1.15 - 0.35 * res.Life), 0.78);
    }

    float effColourShift = shape.EffectorMotion.w;
    if (effColourShift > 0.001 && res.EffectorWeight > 0.001)
    {
        float shifted = clamp(res.EffectorWeight * effColourShift, 0.0, 1.0);
        colour = lerp(colour, SampleSwatchGradient(shade, shifted), shifted);
    }

    float hueJitter = shade.ColourJitterCycle.x;
    float brightJitter = shade.ColourJitterCycle.y;
    if (hueJitter > 0.0)
    {
        colour = HueRotate(colour, (frac(velocity.w * 13.7) - 0.5) * hueJitter * 6.2831853);
    }
    if (brightJitter > 0.0)
    {
        colour *= 1.0 + (frac(velocity.w * 29.3) - 0.5) * 2.0 * brightJitter;
    }
    colour = max(colour, float3(0.0, 0.0, 0.0)) * shade.ColourJitterCycle.w;

    int derezMode = int(shape.EffectorAxisDerez.y + 0.5);
    float derezMix = shape.EffectorAxisDerez.z;
    float derezFade = 1.0;
    if (mode == 1 && derezMode > 0 && derezMix > 0.001)
    {
        colour += shape.DerezEdgeColourWing.xyz * (derez.EdgeBand * shape.DerezFrontParams.z * derezMix);
        if (derez.Released && derezMode != 2)
        {
            float releaseLife = clamp(current.w / max(lifetime, 0.25), 0.0, 1.0);
            if (derezMode == 3) derezFade = 1.0 - smoothstep(0.25, 0.85, releaseLife);
            else derezFade = 1.0 - smoothstep(0.55, 1.0, releaseLife);
        }
        else if (derez.Released && derezMode == 2)
        {
            derezFade = 0.55 + 0.45 * derez.EdgeBand;
        }
    }

    float flickerAmt = shade.TimingRegionHeight.x;
    res.Flicker = (flickerAmt > 0.0)
        ? lerp(1.0, 0.35 + 0.65 * (0.5 + 0.5 * sin(simTime * (3.0 + 5.0 * velocity.w) + velocity.w * 50.0)), flickerAmt)
        : 1.0;

    float glitchAmt = shape.GlitchParams.x;
    float glitchRate = shape.GlitchParams.y;
    float glitchScan = shape.GlitchParams.w;
    if (glitchAmt > 0.001 && glitchScan > 0.001)
    {
        float gTick = floor(simTime * max(glitchRate, 1.0));
        float gHash = frac(sin(dot(float2(float(particle) * 0.17, gTick), float2(12.9898, 78.233))) * 43758.5453);
        res.Flicker *= lerp(1.0, step(0.18, gHash) * (0.65 + 0.7 * gHash), clamp(glitchAmt * glitchScan, 0.0, 1.0));
    }

    float fade = (mode == 1) ? derezFade : (smoothstep(0.0, 0.05, res.Life) * (1.0 - smoothstep(0.75, 1.0, res.Life)));
    res.Alpha = alive * fade * res.Flicker;
    res.Linger = (mode == 1)
        ? derezFade
        : ((particle < count) ? clamp(1.0 - (current.w - lifetime) / max(linger, 1.0e-3), 0.0, 1.0) : 0.0);

    if (colourMode != 4) colour *= 0.85 + 0.3 * res.Flicker;
    res.Colour = colour;
    return res;
}
