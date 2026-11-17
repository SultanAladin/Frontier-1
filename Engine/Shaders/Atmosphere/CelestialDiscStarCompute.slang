/*============================================================================================================================================
                                                      CELESTIALDISCSTARCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Analytical Sun Disc + Corona, 4x Multi-Moon Real 2K Atlas Discs & Deterministic Nocturnal Starfield

static const float CelestialPiConst  = 3.14159265359;
static const float CelestialTauConst = 6.28318530718;
static const uint  MoonAtlasTileW    = 512u;
static const uint  MoonAtlasTileH    = 256u;
static const uint  MoonAtlasPresets  = 6u;

struct GpuMoonDescriptor
{
    float4 DirectionRadiusRad; // xyz = UnitMoonDir, w = HalfAngularRadiusRad
    float4 TintSynodicPhase;   // xyz = LinearTint, w = SynodicPhase (0..1)
    float4 OpticsOrientation;  // x = Bright, y = HaloGlow, z = RollRad, w = PitchRad
    float4 MetaMask;           // x = Visible (0/1), y = PresetSeed, z = PresetIndex (0..5), w = Pad
};

struct CelestialFieldUniformBlock
{
    float4 SunAxisRadius;        // xyz = UnitSunDir, w = SunHalfAngleRad
    float4 SunTintIntensity;     // xyz = EffectiveSunTint * Transmittance, w = SunEnabledScale
    float4 StarParams;           // x = StarsEnabled(0/1), y = LimMag, z = Bright, w = PointSize
    float4 StarScintillationRot; // x = TwinkleEnabled(0/1), y = DepthPct, z = RateHz, w = RotRad
    float4 ClockViewportDims;    // x = SimulationSeconds, y = ViewportW, z = ViewportH, w = MoonCount
    GpuMoonDescriptor Moons[4];
};

float HashCelestial3(int IX, int IY, int IZ)
{
    uint N = uint(IX * 374761393 + IY * 668265263 + IZ * 2147483647);
    N = (N ^ (N >> 13u)) * 1274126177u;
    N = N ^ (N >> 16u);
    return float(N) * (1.0 / 4294967296.0);
}

float3 UnpackSrgbByteToLinear(uint PackedRgba8)
{
    float3 S = float3(
        float(PackedRgba8 & 0xFFu),
        float((PackedRgba8 >> 8u) & 0xFFu),
        float((PackedRgba8 >> 16u) & 0xFFu)) * (1.0 / 255.0);
    return select(S <= float3(0.04045), S / 12.92, pow((S + 0.055) / 1.055, float3(2.2)));
}

float3 SampleLunarAtlasGpu(
    StructuredBuffer<uint> LunarAtlas,
    uint                   PresetIndex,
    float                  LonU,
    float                  LatV)
{
    uint P = min(PresetIndex, MoonAtlasPresets - 1u);
    float WrappedU = LonU - floor(LonU);
    if (WrappedU < 0.0) WrappedU += 1.0;
    float ClampedV = clamp(LatV, 0.0, 1.0);

    float FX = WrappedU * float(MoonAtlasTileW) - 0.5;
    float FY = ClampedV * float(MoonAtlasTileH - 1u);
    int   X0 = ((int(floor(FX)) % int(MoonAtlasTileW)) + int(MoonAtlasTileW)) % int(MoonAtlasTileW);
    int   X1 = (X0 + 1) % int(MoonAtlasTileW);
    int   Y0 = clamp(int(floor(FY)), 0, int(MoonAtlasTileH - 1u));
    int   Y1 = min(Y0 + 1, int(MoonAtlasTileH - 1u));
    float TX = FX - floor(FX);
    float TY = FY - floor(FY);

    uint RowBase = P * MoonAtlasTileH;
    float3 C00 = UnpackSrgbByteToLinear(LunarAtlas[(RowBase + uint(Y0)) * MoonAtlasTileW + uint(X0)]);
    float3 C10 = UnpackSrgbByteToLinear(LunarAtlas[(RowBase + uint(Y0)) * MoonAtlasTileW + uint(X1)]);
    float3 C01 = UnpackSrgbByteToLinear(LunarAtlas[(RowBase + uint(Y1)) * MoonAtlasTileW + uint(X0)]);
    float3 C11 = UnpackSrgbByteToLinear(LunarAtlas[(RowBase + uint(Y1)) * MoonAtlasTileW + uint(X1)]);
    return lerp(lerp(C00, C10, TX), lerp(C01, C11, TX), TY);
}

float3 EvaluateCelestialBodiesGpu(
    CelestialFieldUniformBlock Cfg,
    StructuredBuffer<uint>     LunarAtlas,
    float3                     RayDir)
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

    // 2. Multi-Instance Moons (up to 4) sampling real 2K lunar equirectangular atlas
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

                float LonU = atan2(PX, SurfZ) / CelestialTauConst + 0.5;
                float LatV = acos(clamp(SurfY, -1.0, 1.0)) / CelestialPiConst;
                uint  PresetIdx = uint(M.MetaMask.z + 0.5);
                float3 TexAlbedo = SampleLunarAtlasGpu(LunarAtlas, PresetIdx, LonU, LatV);

                float PhAng = (M.TintSynodicPhase.w - 0.5) * CelestialTauConst;
                float3 PhL  = normalize(float3(sin(PhAng), 0.18, cos(PhAng)));
                float NdotL = max(0.0, dot(float3(PX, PY, PZ), PhL));

                float EdgeAa    = clamp((1.0 - sqrt(R2)) * 18.0, 0.0, 1.0);
                float LimbBoost = 0.78 + 0.22 * sqrt(max(0.0, 1.0 - R2));
                float Lum       = (0.025 + NdotL * 1.95) * LimbBoost * M.OpticsOrientation.x * EdgeAa;
                OutRgb += TexAlbedo * Lum;
            }
        }
    }

    // 3. Deterministic Nocturnal Starfield (Cube-face 3x3 angular PSF — zero popping under camera rotation!)
    if (Cfg.StarParams.x > 0.5 && RayDir.z > -0.02)
    {
        float DayFade = clamp(1.0 - (SunDir.z + 0.08) * 5.0, 0.0, 1.0);
        if (DayFade > 0.01)
        {
            float CR = cos(Cfg.StarScintillationRot.w);
            float SR = sin(Cfg.StarScintillationRot.w);
            float3 RotDir = normalize(float3(RayDir.x * CR - RayDir.y * SR, RayDir.x * SR + RayDir.y * CR, RayDir.z));

            float AX = abs(RotDir.x);
            float AY = abs(RotDir.y);
            float AZ = abs(RotDir.z);
            int   Face = 0;
            float FU   = 0.0;
            float FV   = 0.0;
            if (AX >= AY && AX >= AZ)
            {
                if (RotDir.x >= 0.0) { Face = 0; FU = -RotDir.z / AX; FV = RotDir.y / AX; }
                else                 { Face = 1; FU =  RotDir.z / AX; FV = RotDir.y / AX; }
            }
            else if (AY >= AX && AY >= AZ)
            {
                if (RotDir.y >= 0.0) { Face = 2; FU = RotDir.x / AY; FV = -RotDir.z / AY; }
                else                 { Face = 3; FU = RotDir.x / AY; FV =  RotDir.z / AY; }
            }
            else
            {
                if (RotDir.z >= 0.0) { Face = 4; FU = RotDir.x / AZ; FV =  RotDir.y / AZ; }
                else                 { Face = 5; FU = RotDir.x / AZ; FV = -RotDir.y / AZ; }
            }

            const float GridN = 88.0;
            float2 GridUv = float2(FU * 0.5 + 0.5, FV * 0.5 + 0.5) * GridN;
            int    CX     = clamp(int(floor(GridUv.x)), 0, 87);
            int    CY     = clamp(int(floor(GridUv.y)), 0, 87);

            float MagThresh   = clamp(Cfg.StarParams.y / 8.5, 0.05, 0.98);
            float StarProb    = 0.22 * MagThresh;
            float PixelAng    = 1.25 / max(320.0, Cfg.ClockViewportDims.z);
            float BaseAng     = 0.00058 * clamp(Cfg.StarParams.w, 0.4, 3.0);
            float SigmaAng    = max(BaseAng, PixelAng * 0.56);
            float EnergyNorm  = clamp((BaseAng * BaseAng) / (SigmaAng * SigmaAng), 0.16, 1.0);
            float AltFade     = clamp((RayDir.z + 0.01) * 4.5, 0.0, 1.0);

            for (int DY = -1; DY <= 1; ++DY)
            {
                int NY = CY + DY;
                if (NY < 0 || NY >= 88) continue;
                for (int DX = -1; DX <= 1; ++DX)
                {
                    int NX = CX + DX;
                    if (NX < 0 || NX >= 88) continue;

                    float H1 = HashCelestial3(Face * 131 + NX, NY, 17);
                    if (H1 <= 1.0 - StarProb) continue;

                    float H2 = HashCelestial3(Face * 131 + NX + 19, NY - 7, 31);
                    float H3 = HashCelestial3(Face * 131 + NX - 13, NY + 29, 53);
                    float SU = ((float(NX) + 0.20 + 0.60 * H2) / GridN) * 2.0 - 1.0;
                    float SV = ((float(NY) + 0.20 + 0.60 * H3) / GridN) * 2.0 - 1.0;

                    float3 StarDir;
                    if      (Face == 0) StarDir = normalize(float3( 1.0,  SV, -SU));
                    else if (Face == 1) StarDir = normalize(float3(-1.0,  SV,  SU));
                    else if (Face == 2) StarDir = normalize(float3( SU,  1.0, -SV));
                    else if (Face == 3) StarDir = normalize(float3( SU, -1.0,  SV));
                    else if (Face == 4) StarDir = normalize(float3( SU,  SV,  1.0));
                    else                StarDir = normalize(float3( SU, -SV, -1.0));

                    float Chord = length(RotDir - StarDir);
                    if (Chord < SigmaAng * 3.4)
                    {
                        float NormR   = Chord / SigmaAng;
                        float Falloff = exp(-0.5 * NormR * NormR);
                        float Twinkle = (Cfg.StarScintillationRot.x > 0.5)
                            ? (1.0 - (Cfg.StarScintillationRot.y * 0.008) *
                                  (0.5 + 0.5 * sin(Cfg.ClockViewportDims.x * Cfg.StarScintillationRot.z * 5.0 + H2 * 40.0)))
                            : 1.0;
                        float MagRank   = saturate((H1 - (1.0 - StarProb)) / max(1.0e-4, StarProb));
                        float Intensity = (0.35 + 2.25 * MagRank * MagRank) * Cfg.StarParams.z * DayFade * Twinkle * Falloff * AltFade * EnergyNorm;
                        OutRgb += float3(0.78 + 0.42 * H2, 0.88 + 0.12 * H2, 1.20 - 0.35 * H2) * Intensity;
                    }
                }
            }
        }
    }

    return OutRgb;
}

#ifndef FRONTIER_ATMOSPHERE_EMBEDDED
[[vk::binding(0, 0)]] ConstantBuffer<CelestialFieldUniformBlock> CelestialUniforms;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>                OutCelestialRgba;
[[vk::binding(2, 0)]] StructuredBuffer<uint>                    LunarSurfaceAtlasTexels;

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
    OutCelestialRgba[DispatchThreadID.y * W + DispatchThreadID.x] =
        float4(EvaluateCelestialBodiesGpu(CelestialUniforms, LunarSurfaceAtlasTexels, Rd), 1.0);
}
#endif
