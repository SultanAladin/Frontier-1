//==========================================================================================================================================
//                                                  STUDIOENVIRONMENTRADIANCE.SLANG
//==========================================================================================================================================
// Calibrated GI-free studio prefiltered IBL with rectangular softbox panels & ACES filmic sRGB8 encoding for Direct Studio OpenPBR preview

module StudioEnvironmentRadiance;

float EvaluateSoftboxPanel(float3 Dir, float3 CenterDir, float3 AxisU, float3 AxisV, float HalfU, float HalfV, float Roughness)
{
    float Facing = dot(Dir, CenterDir);
    if (Facing <= 0.05)
    {
        return 0.0;
    }
    float3 Proj = Dir / max(0.15, Facing) - CenterDir;
    float  DU   = abs(dot(Proj, AxisU));
    float  DV   = abs(dot(Proj, AxisV));
    float  Blur = lerp(0.035, 0.58, saturate(Roughness * Roughness));
    float  MaskU = 1.0 - smoothstep(HalfU - Blur * 0.35, HalfU + Blur, DU);
    float  MaskV = 1.0 - smoothstep(HalfV - Blur * 0.35, HalfV + Blur, DV);
    float  EnergyNorm = 1.0 / (1.0 + 4.5 * Roughness * Roughness);
    return MaskU * MaskV * EnergyNorm;
}

public float3 SampleCalibratedStudioEnvironment(float3 WorldDir, float Roughness)
{
    float3 Dir = normalize(WorldDir);
    float  UpT = saturate(0.5 * (Dir.y + 1.0));

    float3 FloorCol  = float3(0.036, 0.039, 0.046);
    float3 ZenithCol = float3(0.115, 0.145, 0.195);
    float3 Env       = lerp(FloorCol, ZenithCol, UpT);

    // Overhead right rectangular warm softbox panel
    float3 KeyC = normalize(float3(0.46, 0.72, 0.52));
    float3 KeyU = normalize(cross(float3(0.0, 1.0, 0.0), KeyC));
    float3 KeyV = cross(KeyC, KeyU);
    float  KeyP = EvaluateSoftboxPanel(Dir, KeyC, KeyU, KeyV, 0.34, 0.24, Roughness);
    Env += float3(1.06, 1.00, 0.93) * (KeyP * 2.85);

    // Left vertical cool softbox strip
    float3 FillC = normalize(float3(-0.76, 0.26, 0.58));
    float3 FillU = normalize(cross(float3(0.0, 1.0, 0.0), FillC));
    float3 FillV = cross(FillC, FillU);
    float  FillP = EvaluateSoftboxPanel(Dir, FillC, FillU, FillV, 0.18, 0.42, Roughness);
    Env += float3(0.76, 0.90, 1.12) * (FillP * 1.65);

    // Subtle overhead studio back-rim light strip
    float3 RimC = normalize(float3(-0.18, 0.70, -0.69));
    float3 RimU = normalize(cross(float3(0.0, 1.0, 0.0), RimC));
    float3 RimV = cross(RimC, RimU);
    float  RimP = EvaluateSoftboxPanel(Dir, RimC, RimU, RimV, 0.45, 0.14, Roughness);
    Env += float3(0.84, 0.93, 1.08) * (RimP * 1.80);

    return Env;
}

public float3 SampleStudioBackdropRgb(float2 ScreenUv)
{
    float2 Centered = ScreenUv * 2.0 - 1.0;
    float  Radial   = dot(Centered, Centered);
    float  Grad     = saturate(1.0 - 0.48 * Radial) * lerp(0.036, 0.068, saturate(1.0 - ScreenUv.y));
    return float3(Grad, Grad * 1.02, Grad * 1.08);
}

public uint PackAcesSrgb8Rgba(float3 LinearRadiance)
{
    float3 X      = max(float3(0.0), LinearRadiance * 0.88);
    float3 Mapped = saturate((X * (2.51 * X + 0.03)) / (X * (2.43 * X + 0.59) + 0.14));
    float3 Lo     = 12.92 * Mapped;
    float3 Hi     = 1.055 * pow(max(Mapped, float3(1.0e-6)), float3(1.0 / 2.4)) - 0.055;
    float3 Srgb   = float3(
        (Mapped.r <= 0.0031308) ? Lo.r : Hi.r,
        (Mapped.g <= 0.0031308) ? Lo.g : Hi.g,
        (Mapped.b <= 0.0031308) ? Lo.b : Hi.b);
    uint R = uint(clamp(round(Srgb.r * 255.0), 0.0, 255.0));
    uint G = uint(clamp(round(Srgb.g * 255.0), 0.0, 255.0));
    uint B = uint(clamp(round(Srgb.b * 255.0), 0.0, 255.0));
    return (255u << 24u) | (B << 16u) | (G << 8u) | R;
}
