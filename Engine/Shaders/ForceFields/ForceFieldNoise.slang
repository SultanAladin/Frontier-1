/*============================================================================================================================================
                                                          FORCEFIELDNOISE.SLANG
============================================================================================================================================*/
// 🧩 Pure .slang 3D gradient, fractal value, vector potential, and divergence-free curl noise for ForceFields, Fluid, and Particles
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L234-L293 (NoiseChunk)

module ForceFieldNoise;

//------------------------------------------------------------------------------------------------------------------------
//                                                    HASH & GRADIENT NOISE
//------------------------------------------------------------------------------------------------------------------------

public float Hash31(float3 p)
{
    p = frac(p * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return frac((p.x + p.y) * p.z);
}

public float3 Hash33(float3 p)
{
    float3 q = float3(
        dot(p, float3(127.1, 311.7, 74.7)),
        dot(p, float3(269.5, 183.3, 246.1)),
        dot(p, float3(113.5, 271.9, 124.6)));
    return frac(sin(q) * 43758.5453123) * 2.0 - 1.0;
}

/// Gradient (Perlin-style) noise with quintic fade: no lattice-aligned blobs.
public float GradientNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    float n000 = dot(Hash33(i + float3(0.0, 0.0, 0.0)), f - float3(0.0, 0.0, 0.0));
    float n100 = dot(Hash33(i + float3(1.0, 0.0, 0.0)), f - float3(1.0, 0.0, 0.0));
    float n010 = dot(Hash33(i + float3(0.0, 1.0, 0.0)), f - float3(0.0, 1.0, 0.0));
    float n110 = dot(Hash33(i + float3(1.0, 1.0, 0.0)), f - float3(1.0, 1.0, 0.0));
    float n001 = dot(Hash33(i + float3(0.0, 0.0, 1.0)), f - float3(0.0, 0.0, 1.0));
    float n101 = dot(Hash33(i + float3(1.0, 0.0, 1.0)), f - float3(1.0, 0.0, 1.0));
    float n011 = dot(Hash33(i + float3(0.0, 1.0, 1.0)), f - float3(0.0, 1.0, 1.0));
    float n111 = dot(Hash33(i + float3(1.0, 1.0, 1.0)), f - float3(1.0, 1.0, 1.0));

    float x00 = lerp(n000, n100, u.x);
    float x10 = lerp(n010, n110, u.x);
    float x01 = lerp(n001, n101, u.x);
    float x11 = lerp(n011, n111, u.x);
    return lerp(lerp(x00, x10, u.y), lerp(x01, x11, u.y), u.z);
}

/// Three-octave fractal gradient noise remapped to [0, 1].
/// GLSL column-major mat3(0,0.8,0.6, -0.8,0.36,-0.48, -0.6,-0.48,0.64) transposed into Slang row-major float3x3.
public float ValueNoise(float3 p)
{
    static const float3x3 kOctaveRotation = float3x3(
         0.00, -0.80, -0.60,
         0.80,  0.36, -0.48,
         0.60, -0.48,  0.64);

    float sum = GradientNoise(p);
    p = mul(kOctaveRotation, p) * 2.02;
    sum += 0.5 * GradientNoise(p);
    p = mul(kOctaveRotation, p) * 2.03;
    sum += 0.25 * GradientNoise(p);
    return clamp(sum / 1.75 * 0.75 + 0.5, 0.0, 1.0);
}

public float3 PotentialNoise(float3 p)
{
    return float3(
        GradientNoise(p),
        GradientNoise(p + float3(31.7, 17.3, 9.1)),
        GradientNoise(p + float3(-12.9, 44.2, 23.6)));
}

/// Divergence-free curl of PotentialNoise for turbulence injection.
public float3 CurlNoise(float3 p)
{
    static const float kFiniteDifferenceStep = 0.03;
    float3 dx = PotentialNoise(p + float3(kFiniteDifferenceStep, 0.0, 0.0)) - PotentialNoise(p - float3(kFiniteDifferenceStep, 0.0, 0.0));
    float3 dy = PotentialNoise(p + float3(0.0, kFiniteDifferenceStep, 0.0)) - PotentialNoise(p - float3(0.0, kFiniteDifferenceStep, 0.0));
    float3 dz = PotentialNoise(p + float3(0.0, 0.0, kFiniteDifferenceStep)) - PotentialNoise(p - float3(0.0, 0.0, kFiniteDifferenceStep));
    return float3(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x) / (2.0 * kFiniteDifferenceStep);
}
