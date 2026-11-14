/*============================================================================================================================================
                                                       PARTICLESPRITECUTOUTS.SLANG
============================================================================================================================================*/
// 🧩 Pure .slang library implementing all 20 procedural SpriteCutoutCategory SDF cutouts (0..19) & BokehApertureCategory optics

module ParticleSpriteCutouts;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROCEDURAL SDF HELPERS
//------------------------------------------------------------------------------------------------------------------------

float SdRegularPolygon(float2 P, float Radius, int Sides)
{
    float An = 3.14159265 / float(Sides);
    float En = 3.14159265 / float(Sides);
    float2 Acs = float2(cos(An), sin(An));
    float2 Ecs = float2(cos(En), sin(En));
    float Bn = fmod(atan2(P.x, P.y) + 6.2831853, 2.0 * An) - An;
    float2 Q = length(P) * float2(cos(Bn), abs(sin(Bn)));
    Q -= Radius * Acs;
    Q += Ecs * clamp(-dot(Q, Ecs), 0.0, Radius * Acs.y / Ecs.y);
    return length(Q) * sign(Q.x);
}

/// Evaluates normalized opacity [0, 1] for any of the 20 SpriteCutoutCategory cutouts (0..19) in local UV [-1, 1]^2.
public float EvaluateSpriteCutoutMask(uint CutoutCode, uint BokehCode, float2 LocalUv, float Seed)
{
    float R2 = dot(LocalUv, LocalUv);
    float R  = sqrt(R2);

    if (CutoutCode == 0u) // SoftGlow
        return exp(-R2 * 3.8) * saturate(1.0 - R);

    if (CutoutCode == 1u) // SparkStreak
    {
        float2 S = float2(LocalUv.x * 2.8, LocalUv.y * 0.65);
        return exp(-dot(S, S) * 4.2) * saturate(1.0 - length(S));
    }

    if (CutoutCode == 2u) // SmokeCloud
    {
        float Wisp = 0.72 + 0.28 * sin(LocalUv.x * 4.5 + Seed * 6.28) * cos(LocalUv.y * 4.5 - Seed * 4.1);
        return saturate((1.0 - R) * 1.15) * saturate(1.0 - R2) * Wisp;
    }

    if (CutoutCode == 3u) // RainDrop
    {
        float2 P = float2(LocalUv.x * 1.7, LocalUv.y - 0.15);
        float DropR = length(P) - (0.55 - 0.22 * LocalUv.y);
        return smoothstep(0.10, -0.05, DropR);
    }

    if (CutoutCode == 4u) // Starburst
    {
        float Ang = atan2(LocalUv.y, LocalUv.x);
        float Rays = 0.5 + 0.5 * cos(Ang * 4.0);
        float SparkHeart = exp(-R2 * 9.0);
        float Cross = exp(-R * (5.2 - 3.6 * Rays));
        return saturate(SparkHeart + Cross * 0.75);
    }

    if (CutoutCode == 5u) // LeafBlade
    {
        float2 P = float2(LocalUv.x * 1.65, LocalUv.y);
        float D = length(P) - 0.78 * (1.0 - 0.35 * abs(LocalUv.y));
        return smoothstep(0.06, -0.06, D);
    }

    if (CutoutCode == 6u) // SnowCrystal
    {
        float Ang = atan2(LocalUv.y, LocalUv.x);
        float Six = abs(cos(Ang * 3.0));
        float Spoke = smoothstep(0.16, 0.03, R * (1.0 - Six * 0.78));
        return saturate(Spoke * smoothstep(0.95, 0.55, R));
    }

    if (CutoutCode == 7u) // BubbleRing
    {
        float Rim = exp(-pow((R - 0.76) * 9.0, 2.0));
        float Spec = exp(-dot(LocalUv - float2(-0.32, 0.34), LocalUv - float2(-0.32, 0.34)) * 28.0);
        return saturate(0.12 * smoothstep(0.92, 0.78, R) + Rim * 0.85 + Spec);
    }

    if (CutoutCode == 8u) // BlossomPetal
    {
        float2 P = LocalUv;
        float Notch = 0.16 * exp(-pow(P.x * 5.0, 2.0)) * step(0.0, P.y);
        float D = length(float2(P.x * 1.25, P.y + Notch)) - 0.72;
        return smoothstep(0.06, -0.06, D);
    }

    if (CutoutCode == 9u) // BokehDisc (with BokehApertureCategory 0..5 optics)
    {
        float2 P = LocalUv;
        float EdgeD = R - 0.82;
        if (BokehCode == 1u) EdgeD = SdRegularPolygon(P, 0.82, 6);       // Hexagonal
        else if (BokehCode == 2u) EdgeD = SdRegularPolygon(P, 0.82, 8);  // Octagonal
        else if (BokehCode == 3u) EdgeD = max(length(P - float2(0.18, 0.0)) - 0.82, length(P + float2(0.18, 0.0)) - 0.82); // CatEye
        else if (BokehCode == 4u) EdgeD = length(float2(P.x * 1.55, P.y * 0.72)) - 0.78; // Anamorphic
        float Body = smoothstep(0.08, -0.04, EdgeD);
        float Rim  = 0.72 + 0.32 * smoothstep(0.50, 0.80, R);
        return saturate(Body * Rim);
    }

    if (CutoutCode == 10u) // FireflyOrb
        return saturate(exp(-R2 * 10.0) + 0.38 * exp(-R2 * 2.4));

    if (CutoutCode == 11u) // ConfettiRect
        return smoothstep(0.05, -0.02, max(abs(LocalUv.x) - 0.72, abs(LocalUv.y) - 0.44));

    if (CutoutCode == 12u) // SolidSphere
        return smoothstep(0.96, 0.82, R);

    if (CutoutCode == 13u) // CrystalShard
        return smoothstep(0.04, -0.03, abs(LocalUv.x) * 1.5 + abs(LocalUv.y) - 0.85);

    if (CutoutCode == 14u) // GeometricGlyph
        return smoothstep(0.05, -0.03, SdRegularPolygon(LocalUv, 0.78, 3));

    if (CutoutCode == 15u) // HeartOutline
    {
        float2 P = float2(abs(LocalUv.x), LocalUv.y + 0.18) * 1.25;
        float H = pow(dot(P, P) - 0.65, 3.0) - P.x * P.x * P.y * P.y * P.y;
        return smoothstep(0.06, -0.06, H);
    }

    if (CutoutCode == 16u) // HaloRing
        return saturate(exp(-pow((R - 0.70) * 7.5, 2.0)));

    if (CutoutCode == 17u) // ButterflyWing
    {
        float Ax = abs(LocalUv.x);
        float Upper = length(float2((Ax - 0.38) * 1.35, (LocalUv.y - 0.24) * 1.35)) - 0.44;
        float Lower = length(float2((Ax - 0.28) * 1.65, (LocalUv.y + 0.30) * 1.65)) - 0.34;
        return smoothstep(0.06, -0.04, min(Upper, Lower));
    }

    if (CutoutCode == 18u) // MistPuff
        return exp(-R2 * 2.2) * saturate(1.0 - R);

    // 19: InkDrop
    float Wobble = 0.08 * sin(atan2(LocalUv.y, LocalUv.x) * 5.0 + Seed * 12.0);
    return smoothstep(0.06, -0.04, R - (0.78 + Wobble));
}
