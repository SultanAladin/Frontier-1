/*============================================================================================================================================
                                                        SIMULATIONPOSTOVERLAY.SLANG
============================================================================================================================================*/
// 🧩 Stage D (Slice S17) 1:1 Screen-space simulation overlay composite:
//    Gas Volume Heat Shimmer (Layer 109), Particle Screen-Space Glitch (Layer 217), Distortion / Gravitational Lensing (Layer 218)
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js PostOverlayFragment (L1758-L1909)

//------------------------------------------------------------------------------------------------------------------------
//                                                    UNIFORM BLOCK & BINDINGS
//------------------------------------------------------------------------------------------------------------------------

/// Matches C++ SimulationPostOverlayUniformBlock byte-for-byte (1088 bytes, 16-byte aligned).
public struct SimulationPostOverlayUniformBlock
{
    public float4 ViewportTime;    // xy = viewport (px), z = simulationTime (s), w = reserved
    public int4   EffectCounts;    // x = lensCount (0..16), y = glitchCount (0..16), z = 0, w = 0
    public float4 ShimmerArea;     // xy = screen uv, z = screen-height radius, w = strength
    public float4 ShimmerParams;   // x = frequency, y = speed, z = chromatic, w = 0
    public float4 LensA[16];       // xy = screen uv, z = strength, w = screen-height radius
    public float4 LensB[16];       // x = mode (1=BlackHole, 2=Shockwave, 3=Bubble), y = horizon, z = chromatic, w = 0
    public float4 GlitchA[16];     // xy = screen uv, z = screen-height radius, w = amount
    public float4 GlitchB[16];     // x = rate, y = scanlines, z = chromatic, w = 0
};

[[vk::binding(0, 0)]] ConstantBuffer<SimulationPostOverlayUniformBlock> uParams;
[[vk::binding(1, 0)]] Texture2D<float4>                                 gSceneColour;
[[vk::binding(2, 0)]] SamplerState                                      gLinearClamp;

struct FullscreenVertexOutput
{
    float4 Position : SV_Position;
};

[shader("vertex")]
FullscreenVertexOutput FullscreenVertexEntry(uint vertexId : SV_VertexID)
{
    FullscreenVertexOutput outV;
    float2 pos = float2((vertexId == 1u) ? 3.0 : -1.0, (vertexId == 2u) ? 3.0 : -1.0);
    outV.Position = float4(pos, 0.0, 1.0);
    return outV;
}

float Hash21(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453123);
}

float SmoothNoise2D(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(
        lerp(Hash21(i), Hash21(i + float2(1.0, 0.0)), u.x),
        lerp(Hash21(i + float2(0.0, 1.0)), Hash21(i + float2(1.0, 1.0)), u.x),
        u.y);
}

[shader("fragment")]
float4 FragmentEntry(FullscreenVertexOutput input) : SV_Target0
{
    float2 viewport = max(uParams.ViewportTime.xy, float2(1.0, 1.0));
    float  simTime  = uParams.ViewportTime.z;
    float2 uv       = input.Position.xy / viewport;
    float  aspect   = viewport.x / viewport.y;

    float2 src           = uv;
    float  insideHorizon = 0.0;
    float  photonRing    = 0.0;
    float  chromaSpread  = 0.0;
    float2 chromaDir     = uv - float2(0.5, 0.5);

    // 1. Particle Distortion / Gravitational Lensing (Layer 218)
    int lensCount = min(uParams.EffectCounts.x, 16);
    for (int k = 0; k < 16; ++k)
    {
        if (k >= lensCount) break;
        float2 c          = uParams.LensA[k].xy;
        float  strength   = uParams.LensA[k].z;
        float  rad        = max(uParams.LensA[k].w, 0.02);
        int    mode       = int(floor(uParams.LensB[k].x + 0.5));
        float  horizon    = uParams.LensB[k].y;
        float  lensChroma = uParams.LensB[k].z;

        float2 d   = (uv - c) * float2(aspect, 1.0);
        float  r   = length(d);
        float2 dir = (r > 1.0e-5) ? (d / r) : float2(1.0, 0.0);

        if (mode == 1)
        {
            float einstein = rad * (0.22 + 0.65 * strength);
            float rs       = r - (einstein * einstein) / max(r, 1.0e-4);
            float taper    = 1.0 - smoothstep(rad * 0.65, rad * 1.45, r);
            float rr       = lerp(r, rs, taper);
            src += (c + dir * rr / float2(aspect, 1.0) - uv);
            if (horizon > 0.001)
            {
                insideHorizon = max(insideHorizon, 1.0 - smoothstep(horizon * 0.88, horizon, r));
                float q = (r - 1.28 * horizon) / max(0.14 * horizon, 1.0e-4);
                photonRing += exp(-q * q) * (0.55 + 0.85 * strength);
            }
            chromaSpread = max(chromaSpread, lensChroma * taper * 0.014);
            if (taper > 0.01) chromaDir = dir;
        }
        else if (mode == 2)
        {
            float fall = 1.0 - smoothstep(rad * 0.35, rad * 1.15, r);
            float wave = sin(r / rad * 22.0 - simTime * 6.5);
            src += (dir / float2(aspect, 1.0)) * (wave * 0.024 * strength * fall);
            chromaSpread = max(chromaSpread, lensChroma * fall * abs(wave) * 0.012);
            if (fall > 0.01) chromaDir = dir;
        }
        else if (mode == 3)
        {
            float fall = 1.0 - smoothstep(rad * 0.25, rad, r);
            float2 np = uv * 12.0 + float2(simTime * 0.8, -simTime * 1.4);
            float2 wobble = float2(SmoothNoise2D(np), SmoothNoise2D(np + float2(5.2, 1.7))) - 0.5;
            src += wobble * (0.032 * strength * fall);
            chromaSpread = max(chromaSpread, lensChroma * fall * 0.010);
        }
    }

    // 2. Gas Volume Heat Shimmer (Layer 109)
    if (uParams.ShimmerArea.w > 0.0005)
    {
        float2 sd   = (uv - uParams.ShimmerArea.xy) * float2(aspect, 0.72);
        float  sr   = length(sd) / max(uParams.ShimmerArea.z, 0.02);
        float sFall = 1.0 - smoothstep(0.45, 1.05, sr);
        if (sFall > 0.001)
        {
            float  freq = uParams.ShimmerParams.x;
            float  rise = simTime * uParams.ShimmerParams.y;
            float2 hp   = float2(uv.x * freq, uv.y * freq - rise * 1.65);
            float2 n1   = float2(SmoothNoise2D(hp), SmoothNoise2D(hp + float2(5.2, 1.3))) - 0.5;
            float2 n2   = float2(SmoothNoise2D(hp * 2.15 + float2(1.7, -rise * 0.9)),
                                 SmoothNoise2D(hp * 2.15 + float2(8.4, 3.1))) - 0.5;
            src += (n1 * 0.68 + n2 * 0.32) * (0.026 * uParams.ShimmerArea.w * sFall);
            chromaSpread = max(chromaSpread, uParams.ShimmerParams.z * sFall * uParams.ShimmerArea.w * 0.009);
        }
    }

    // 3. Localized Screen-Space Glitch (Layer 217)
    float scanMod = 1.0;
    int glitchCount = min(uParams.EffectCounts.y, 16);
    for (int g = 0; g < 16; ++g)
    {
        if (g >= glitchCount) break;
        float gd   = length((uv - uParams.GlitchA[g].xy) * float2(aspect, 1.0)) / max(uParams.GlitchA[g].z, 0.02);
        float mask = 1.0 - smoothstep(0.50, 1.05, gd);
        float amt  = uParams.GlitchA[g].w * mask;
        if (amt > 0.001)
        {
            float stepT = floor(simTime * max(uParams.GlitchB[g].x, 1.0));
            float band  = floor(uv.y * 36.0);
            float gate  = step(0.74 - amt * 0.50, Hash21(float2(band, stepT + float(g) * 19.0)));
            float shift = (Hash21(float2(band + 17.0, stepT * 1.3)) - 0.5) * 0.06 * amt * uParams.GlitchB[g].y * gate;
            src.x += shift;
            scanMod *= lerp(1.0, 0.84 + 0.16 * sin(input.Position.y * 1.8), clamp(amt * uParams.GlitchB[g].y, 0.0, 1.0));
            chromaSpread = max(chromaSpread, amt * uParams.GlitchB[g].z * 0.014);
            chromaDir = float2(1.0, 0.0);
        }
    }

    float2 clampedSrc = clamp(src, float2(0.001, 0.001), float2(0.999, 0.999));
    float4 sampled    = gSceneColour.SampleLevel(gLinearClamp, clampedSrc, 0.0);
    float3 col        = sampled.rgb;

    if (chromaSpread > 0.0002)
    {
        float2 off = normalize(chromaDir + float2(1.0e-5, 0.0)) * chromaSpread;
        float  cr  = gSceneColour.SampleLevel(gLinearClamp, clamp(clampedSrc + off, float2(0.001, 0.001), float2(0.999, 0.999)), 0.0).r;
        float  cb  = gSceneColour.SampleLevel(gLinearClamp, clamp(clampedSrc - off, float2(0.001, 0.001), float2(0.999, 0.999)), 0.0).b;
        col = float3(cr, col.g, cb);
    }

    col *= (1.0 - insideHorizon) * scanMod;
    col += float3(1.0, 0.86, 0.62) * photonRing;
    return float4(col, sampled.a);
}
