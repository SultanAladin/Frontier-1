/*============================================================================================================================================
                                                      CELESTIALDISCSTARCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Analytical Sun Disc + Corona, 4x Multi-Moon Phase/Crater/Halo Discs & Deterministic Nocturnal Starfield

static const float CelestialPiConst  = 3.14159265359;
static const float CelestialTauConst = 6.28318530718;

struct GpuMoonDescriptor
{
    float4 DirectionRadiusRad; // xyz = UnitMoonDir, w = HalfAngularRadiusRad
    float4 TintSynodicPhase;       // xyz = LinearTint, w = SynodicPhase (0..1)
    float4 OpticsOrientation; // x = Bright, y = HaloGlow, z = RollRad, w = PitchRad
    float4 MetaMask;          // x = Visible (0/1), y = PresetSeed, z = Pad, w = Pad
};

struct CelestialFieldUniformBlock
{
    float4 SunAxisRadius;  // xyz = UnitSunDir, w = SunHalfAngleRad
    float4 SunTintIntensity; // xyz = EffectiveSunTint * Transmittance, w = SunEnabledScale
    float4 StarParams;          // x = StarsEnabled(0/1), y = LimMag, z = Bright, w = PointSize
    float4 StarScintillationRot;      // x = TwinkleEnabled(0/1), y = DepthPct, z = RateHz, w = RotRad
    float4 ClockViewportDims;     // x = SimulationSeconds, y = ViewportW, z = ViewportH, w = MoonCount
    GpuMoonDescriptor Moons[4];
};

float HashCelestial3(int IX, int IY, int IZ)
{
    uint N = uint(IX * 374761393 + IY * 668265263 + IZ * 2147483647);
    N = (N ^ (N >> 13u)) * 1274126177u;
    N = N ^ (N >> 16u);
    return float(N) * (1.0 / 4294967296.0);
}

float LatticeNoiseCelestial3(float3 P)
{
    int3 I = int3(floor(P));
    float3 F = P - float3(I);
    float3 U = F * F * (3.0 - 2.0 * F);

    float N000 = HashCelestial3(I.x, I.y, I.z),         N100 = HashCelestial3(I.x + 1, I.y, I.z);
    float N010 = HashCelestial3(I.x, I.y + 1, I.z),     N110 = HashCelestial3(I.x + 1, I.y + 1, I.z);
    float N001 = HashCelestial3(I.x, I.y, I.z + 1),     N101 = HashCelestial3(I.x + 1, I.y, I.z + 1);
    float N011 = HashCelestial3(I.x, I.y + 1, I.z + 1), N111 = HashCelestial3(I.x + 1, I.y + 1, I.z + 1);

    return lerp(
        lerp(lerp(N000, N100, U.x), lerp(N010, N110, U.x), U.y),
        lerp(lerp(N001, N101, U.x), lerp(N011, N111, U.x), U.y),
        U.z);
}

float3 EvaluateCelestialBodiesGpu(CelestialFieldUniformBlock Cfg, float3 RayDir)
{
    float3 OutRgb = float3(0.0, 0.0, 0.0);
    float3 SunDir = normalize(Cfg.SunAxisRadius.xyz);

    // 1. Solar Disc + Corona
    if (Cfg.SunTintIntensity.w > 0.0 && RayDir.z >= -0.04)
    {
        float CosS     = clamp(dot(RayDir, SunDir), -1.0, 1.0);
        float HalfR    = max(0.0008, Cfg.SunAxisRadius.w);
        float Ang      = acos(CosS);
        float DiscEdge = clamp(1.0 - (Ang - HalfR * 0.92) / max(0.0005, HalfR * 0.25), 0.0, 1.0);
        float Corona   = exp(-Ang / max(0.008, HalfR * 3.8)) * 0.55 + pow(max(0.0, CosS), 240.0) * 0.28;
        float LimbFade = clamp((RayDir.z + 0.02) * 18.0, 0.0, 1.0);
        OutRgb += Cfg.SunTintIntensity.xyz * (DiscEdge * 26.0 + Corona) * (Cfg.SunTintIntensity.w * LimbFade);
    }

    // 2. Multi-Instance Moons (up to 4)
    uint MoonCount = min(4u, uint(Cfg.ClockViewportDims.w + 0.5));
    for (uint MIdx = 0u; MIdx < MoonCount; ++MIdx)
    {
        GpuMoonDescriptor M = Cfg.Moons[MIdx];
        if (M.MetaMask.x < 0.5 || RayDir.z < -0.05) continue;

        float3 MoonDir = normalize(M.DirectionRadiusRad.xyz);
        float  AngR    = max(0.001, M.DirectionRadiusRad.w);
        float  CosM    = clamp(dot(RayDir, MoonDir), -1.0, 1.0);
        float  CosEdge = cos(AngR);

        float  Glow = pow(max(0.0, CosM), 420.0) * M.OpticsOrientation.y * 0.28 * M.OpticsOrientation.x;
        OutRgb += M.TintSynodicPhase.xyz * Glow;

        if (CosM > CosEdge)
        {
            float3 UpRef = (abs(MoonDir.z) < 0.98) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
            float3 TangentT = normalize(cross(UpRef, MoonDir));
            float3 TangentB = cross(MoonDir, TangentT);
            float  CosR  = cos(M.OpticsOrientation.z);
            float  SinR  = sin(M.OpticsOrientation.z);
            float3 T     = TangentT * CosR + TangentB * SinR;
            float3 B     = TangentB * CosR - TangentT * SinR;

            float SinAng = max(1.0e-4, sin(AngR));
            float PX = dot(RayDir, T) / SinAng;
            float PY = dot(RayDir, B) / SinAng;
            float R2 = PX * PX + PY * PY;
            if (R2 < 1.0)
            {
                float PZ    = sqrt(max(0.0, 1.0 - R2));
                float CosP  = cos(M.OpticsOrientation.w);
                float SinP  = sin(M.OpticsOrientation.w);
                float SurfY = PY * CosP - PZ * SinP;
                float SurfZ = PY * SinP + PZ * CosP;

                float PhAng = (M.TintSynodicPhase.w - 0.5) * CelestialTauConst;
                float3 PhL  = normalize(float3(sin(PhAng), 0.18, cos(PhAng)));
                float NdotL = max(0.0, dot(float3(PX, PY, PZ), PhL));

                float Mare   = 0.62 + 0.38 * LatticeNoiseCelestial3(float3(PX * 3.8 + 4.1 + M.MetaMask.y, SurfY * 3.8 - 2.3, SurfZ * 3.8));
                float EdgeAa = clamp((1.0 - sqrt(R2)) * 14.0, 0.0, 1.0);
                float Lum    = (0.015 + NdotL * 1.45) * Mare * M.OpticsOrientation.x * EdgeAa;
                OutRgb += M.TintSynodicPhase.xyz * Lum;
            }
        }
    }

    // 3. Deterministic Nocturnal Starfield
    if (Cfg.StarParams.x > 0.5 && RayDir.z > 0.005)
    {
        float DayFade = clamp(1.0 - (SunDir.z + 0.08) * 5.0, 0.0, 1.0);
        if (DayFade > 0.01)
        {
            float CR = cos(Cfg.StarScintillationRot.w);
            float SR = sin(Cfg.StarScintillationRot.w);
            float3 RotDir = float3(RayDir.x * CR - RayDir.y * SR, RayDir.x * SR + RayDir.y * CR, RayDir.z);
            float3 G = RotDir * 175.0;
            int3   I = int3(floor(G));

            float H1 = HashCelestial3(I.x, I.y, I.z);
            float H2 = HashCelestial3(I.x + 19, I.y - 7, I.z + 31);
            float H3 = HashCelestial3(I.x - 13, I.y + 29, I.z - 5);
            float MagThresh = clamp(Cfg.StarParams.y / 8.5, 0.05, 0.98);

            if (H1 > 1.0 - 0.18 * MagThresh)
            {
                float3 StarC = float3(I) + float3(0.2 + 0.6 * H2, 0.2 + 0.6 * H3, 0.2 + 0.6 * frac(H1 * 7.13));
                float3 Delta = G - StarC;
                float  D2    = dot(Delta, Delta);
                float  Rad   = 0.08 * clamp(Cfg.StarParams.w, 0.4, 3.0);
                if (D2 < Rad * Rad)
                {
                    float Falloff = exp(-D2 / (Rad * Rad * 0.35));
                    float Twinkle = (Cfg.StarScintillationRot.x > 0.5)
                        ? (1.0 - (Cfg.StarScintillationRot.y * 0.008) *
                              (0.5 + 0.5 * sin(Cfg.ClockViewportDims.x * Cfg.StarScintillationRot.z * 5.0 + H2 * 40.0)))
                        : 1.0;
                    float AltFade   = clamp(RayDir.z * 4.0, 0.0, 1.0);
                    float Intensity = (0.45 + 1.55 * H3 * H3) * Cfg.StarParams.z * DayFade * Twinkle * Falloff * AltFade;
                    OutRgb += float3(0.78 + 0.42 * H2, 0.88 + 0.12 * H2, 1.20 - 0.35 * H2) * Intensity;
                }
            }
        }
    }

    return OutRgb;
}

#ifndef FRONTIER_ATMOSPHERE_EMBEDDED
[[vk::binding(0, 0)]] ConstantBuffer<CelestialFieldUniformBlock> CelestialUniforms;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>                OutCelestialRgba;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint W = max(1u, uint(CelestialUniforms.ClockViewportDims.y + 0.5));
    uint H = max(1u, uint(CelestialUniforms.ClockViewportDims.z + 0.5));
    if (DispatchThreadID.x >= W || DispatchThreadID.y >= H) return;

    float2 Uv = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(W), float(H));
    float Az  = (Uv.x - 0.5) * CelestialTauConst;
    float El  = (1.0 - Uv.y) * (CelestialPiConst * 0.5);
    float3 Rd = float3(cos(El) * cos(Az), cos(El) * sin(Az), sin(El));
    OutCelestialRgba[DispatchThreadID.y * W + DispatchThreadID.x] = float4(EvaluateCelestialBodiesGpu(CelestialUniforms, Rd), 1.0);
}
#endif
