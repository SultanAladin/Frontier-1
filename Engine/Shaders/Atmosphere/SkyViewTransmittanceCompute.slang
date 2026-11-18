/*============================================================================================================================================
                                                    SKYVIEWTRANSMITTANCECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Bruneton/Hillaire Rayleigh + Mie + Ozone Atmospheric Transmittance & 96x48 SkyView LUT Compute Shader

static const float AtmoPiConst   = 3.14159265359;
static const float AtmoTauConst  = 6.28318530718;
static const float ViewerAltM  = 100.0;

struct AtmosphericSkyUniformBlock
{
    float4 SunDirectionIntensity; // xyz = UnitSunDir, w = SolarIntensityScale
    float4 ScatteringStrengths;   // x = Rayleigh, y = Mie, z = MieAnisotropy, w = Ozone
    float4 PlanetDimensions;      // x = PlanetRadiusM, y = AtmoThicknessM, z = RayleighScaleH, w = MieScaleH
    float4 SkyTintBrightness;     // xyz = SkyTint, w = SkyBrightnessMultiplier
    float4 GroundAlbedoLimbGlow;  // xyz = GroundAlbedo, w = LimbGlowIntensity
    uint4  LutDimensions;         // x = SkyViewW (256), y = SkyViewH (128), z = Pad, w = Pad
    float4 ObserverParams;        // x = ObserverAltitudeM, y = OrbitalViewEnabled (0/1), z = Pad, w = Pad
};

float HashUnitSkyGpu(int3 Cell)
{
    uint H = uint(Cell.x) * 374761393u + uint(Cell.y) * 668265263u + uint(Cell.z) * 1442695041u;
    H = (H ^ (H >> 13u)) * 1274126177u;
    H ^= (H >> 16u);
    return float(H & 0x00FFFFFFu) / 16777216.0;
}

float NoiseSkyGpu3(float3 P)
{
    int3 I = int3(floor(P));
    float3 F = P - float3(I);
    float3 U = F * F * (3.0 - 2.0 * F);

    float C000 = HashUnitSkyGpu(I + int3(0, 0, 0));
    float C100 = HashUnitSkyGpu(I + int3(1, 0, 0));
    float C010 = HashUnitSkyGpu(I + int3(0, 1, 0));
    float C110 = HashUnitSkyGpu(I + int3(1, 1, 0));
    float C001 = HashUnitSkyGpu(I + int3(0, 0, 1));
    float C101 = HashUnitSkyGpu(I + int3(1, 0, 1));
    float C011 = HashUnitSkyGpu(I + int3(0, 1, 1));
    float C111 = HashUnitSkyGpu(I + int3(1, 1, 1));

    float X00 = lerp(C000, C100, U.x);
    float X10 = lerp(C010, C110, U.x);
    float X01 = lerp(C001, C101, U.x);
    float X11 = lerp(C011, C111, U.x);
    return lerp(lerp(X00, X10, U.y), lerp(X01, X11, U.y), U.z);
}

float FbmSkyGpu3(float3 P, uint Octaves)
{
    float Sum = 0.0;
    float Amp = 0.5;
    for (uint I = 0u; I < Octaves; ++I)
    {
        Sum += Amp * NoiseSkyGpu3(P);
        P = float3(P.x * 1.62 - P.y * 1.18 + 13.7, P.x * 1.18 + P.y * 1.62 - 7.3, P.z * 1.98 + 5.1);
        Amp *= 0.5;
    }
    return Sum;
}

float2 IntersectOriginSphereAtmo(float OriginZ, float DirZ, float Radius)
{
    float B    = OriginZ * DirZ;
    float C    = OriginZ * OriginZ - Radius * Radius;
    float Disc = B * B - C;
    if (Disc < 0.0) return float2(-1.0, -1.0);
    float Root = sqrt(Disc);
    return float2(-B - Root, -B + Root);
}

float2 IntersectVecSphereAtmo(float3 Pos, float3 Dir, float Radius)
{
    float B    = dot(Pos, Dir);
    float C    = dot(Pos, Pos) - Radius * Radius;
    float Disc = B * B - C;
    if (Disc < 0.0) return float2(-1.0, -1.0);
    float Root = sqrt(Disc);
    return float2(-B - Root, -B + Root);
}

float3 ComputeAtmoTransmittanceAlongRay(
    float3 StartPos,
    float3 RayDir,
    float  PlanetR,
    float  AtmoR,
    float  RayMul,
    float  MieMul,
    float  OzoneMul,
    float  RayH,
    float  MieH)
{
    float2 GHit = IntersectVecSphereAtmo(StartPos, RayDir, PlanetR);
    if (GHit.x > 0.0) return float3(0.0, 0.0, 0.0);

    float2 AHit = IntersectVecSphereAtmo(StartPos, RayDir, AtmoR);
    if (AHit.y <= 0.0) return float3(1.0, 1.0, 1.0);

    float  MaxT = min(AHit.y, 180000.0);
    float  Step = MaxT / 16.0;
    float3 RayBeta   = float3(5.802e-6, 13.558e-6, 33.1e-6);
    float3 OzoneBeta = float3(0.650e-6, 1.881e-6, 0.085e-6);
    float3 OptDepth  = float3(0.0, 0.0, 0.0);

    for (uint I = 0u; I < 16u; ++I)
    {
        float  T   = (float(I) + 0.5) * Step;
        float3 P   = StartPos + RayDir * T;
        float  Alt = max(0.0, length(P) - PlanetR);
        float  DenR = exp(-Alt / RayH) * RayMul;
        float  DenM = exp(-Alt / MieH) * MieMul;
        float  DenO = max(0.0, 1.0 - abs(Alt - 25000.0) / 15000.0) * OzoneMul;
        OptDepth += (RayBeta * DenR + float3(4.40e-6 * DenM) + OzoneBeta * DenO) * Step;
    }
    return exp(-OptDepth);
}

float3 EvaluateSkyViewLutSample(StructuredBuffer<float4> SkyLut, uint WidthPx, uint HeightPx, float3 Dir)
{
    if (WidthPx < 2u || HeightPx < 2u) return float3(0.16, 0.23, 0.36);

    float Az   = atan2(Dir.y, Dir.x);
    float El   = asin(clamp(Dir.z, -1.0, 1.0));
    float U    = frac(Az / AtmoTauConst + 0.5);
    float SignE = (El >= 0.0) ? 1.0 : -1.0;
    float V    = clamp(0.5 - 0.5 * SignE * sqrt(abs(El) / (AtmoPiConst * 0.5)), 0.0, 1.0);

    float FX = U * float(WidthPx) - 0.5;
    float FY = V * float(HeightPx) - 0.5;
    int   IX0 = ((int(floor(FX)) % int(WidthPx)) + int(WidthPx)) % int(WidthPx);
    int   IX1 = (IX0 + 1) % int(WidthPx);
    int   IY0 = clamp(int(floor(FY)), 0, int(HeightPx) - 1);
    int   IY1 = clamp(IY0 + 1, 0, int(HeightPx) - 1);
    float TX  = FX - floor(FX);
    float TY  = FY - floor(FY);
    float SX  = TX * TX * (3.0 - 2.0 * TX);
    float SY  = TY * TY * (3.0 - 2.0 * TY);

    float3 C00 = SkyLut[uint(IY0) * WidthPx + uint(IX0)].xyz;
    float3 C10 = SkyLut[uint(IY0) * WidthPx + uint(IX1)].xyz;
    float3 C01 = SkyLut[uint(IY1) * WidthPx + uint(IX0)].xyz;
    float3 C11 = SkyLut[uint(IY1) * WidthPx + uint(IX1)].xyz;
    return lerp(lerp(C00, C10, SX), lerp(C01, C11, SX), SY);
}

float3 ComputePhysicalSkyTexel(AtmosphericSkyUniformBlock Uni, float2 Uv)
{
    float3 SunDir   = normalize(Uni.SunDirectionIntensity.xyz);
    float  SolarScale = max(0.0, Uni.SunDirectionIntensity.w) * 0.24;
    float  RayMul   = max(0.0, Uni.ScatteringStrengths.x);
    float  MieMul   = max(0.0, Uni.ScatteringStrengths.y);
    float  MieG     = clamp(Uni.ScatteringStrengths.z, 0.0, 0.99);
    float  OzoneMul = max(0.0, Uni.ScatteringStrengths.w);
    float  PlanetR  = max(500000.0, Uni.PlanetDimensions.x);
    float  AtmoTop  = max(10000.0, Uni.PlanetDimensions.y);
    float  RayH     = max(500.0, Uni.PlanetDimensions.z);
    float  MieH     = max(200.0, Uni.PlanetDimensions.w);
    float  AtmoR    = PlanetR + AtmoTop;

    float Az = (Uv.x - 0.5) * AtmoTauConst;
    float SV = (0.5 - Uv.y) * 2.0;
    float El = ((SV >= 0.0) ? 1.0 : -1.0) * SV * SV * (AtmoPiConst * 0.5);
    float CosE = cos(El);
    float3 RawDir = float3(CosE * cos(Az), CosE * sin(Az), sin(El));

    bool   GroundRay = (RawDir.z < 0.0);
    float  RdZ       = GroundRay ? 0.002 : RawDir.z;
    float  RdHoriz   = sqrt(max(1.0e-8, RawDir.x * RawDir.x + RawDir.y * RawDir.y));
    float  RdScale   = sqrt(max(0.0, 1.0 - RdZ * RdZ)) / RdHoriz;
    float3 Rd        = GroundRay ? float3(RawDir.x * RdScale, RawDir.y * RdScale, RdZ) : RawDir;

    float  ObsAltClamped = (Uni.ObserverParams.y > 0.5)
                               ? ViewerAltM
                               : clamp(max(10.0, Uni.ObserverParams.x), 10.0, AtmoTop * 0.96);
    float  OrigZ     = PlanetR + ObsAltClamped;
    float2 AExit     = IntersectOriginSphereAtmo(OrigZ, Rd.z, AtmoR);
    float  MaxDist   = min(AExit.y, 420000.0);
    float  CosTheta  = dot(Rd, SunDir);

    float  PhaseR    = (3.0 / (16.0 * AtmoPiConst)) * (1.0 + CosTheta * CosTheta);
    float  G2        = MieG * MieG;
    float  Denom     = max(1.0e-3, 1.0 + G2 - 2.0 * MieG * CosTheta);
    float  PhaseM    = (3.0 / (8.0 * AtmoPiConst)) * ((1.0 - G2) * (1.0 + CosTheta * CosTheta)) /
                       ((2.0 + G2) * pow(Denom, 1.5));

    float3 RayBeta   = float3(5.802e-6, 13.558e-6, 33.1e-6);
    float3 OzoneBeta = float3(0.650e-6, 1.881e-6, 0.085e-6);
    float  OverheadSunFade = smoothstep(-0.14, 0.04, SunDir.z);
    float  NightTwilFade   = smoothstep(-0.28, -0.14, SunDir.z);
    float3 OverheadT = ComputeAtmoTransmittanceAlongRay(
        float3(0.0, 0.0, OrigZ), normalize(float3(SunDir.x, SunDir.y, max(0.05, SunDir.z))),
        PlanetR, AtmoR, RayMul, MieMul, OzoneMul, RayH, MieH) * OverheadSunFade;

    float3 TAccum   = float3(1.0, 1.0, 1.0);
    float3 RadAccum = float3(0.0, 0.0, 0.0);
    float3 MsAccum  = float3(0.0, 0.0, 0.0);
    float  Step     = max(1.0, MaxDist / 28.0);
    float  AvgGnd   = (Uni.GroundAlbedoLimbGlow.x + Uni.GroundAlbedoLimbGlow.y + Uni.GroundAlbedoLimbGlow.z) * 0.333333;
    float  MsBounce = 0.24 + 0.18 * AvgGnd;

    for (uint S = 0u; S < 28u; ++S)
    {
        float  T   = (float(S) + 0.5) * Step;
        float3 P   = float3(0.0, 0.0, OrigZ) + Rd * T;
        float  Alt = max(0.0, length(P) - PlanetR);

        float  DenR = exp(-Alt / RayH) * RayMul;
        float  DenM = exp(-Alt / MieH) * MieMul;
        float  DenO = max(0.0, 1.0 - abs(Alt - 25000.0) / 15000.0) * OzoneMul;

        float3 SRay = RayBeta * DenR;
        float  SMie = 3.996e-6 * DenM;
        float  EMie = 4.40e-6 * DenM;

        float3 Ext  = max(float3(1.0e-7), SRay + float3(EMie) + OzoneBeta * DenO);
        float3 SegT = exp(-Ext * Step);
        float3 IntS = (float3(1.0) - SegT) / Ext;

        float3 SunT = ComputeAtmoTransmittanceAlongRay(P, SunDir, PlanetR, AtmoR, RayMul, MieMul, OzoneMul, RayH, MieH) * NightTwilFade;
        float  AntiCool = lerp(lerp(0.35, 1.0, smoothstep(-0.35, 0.30, CosTheta)), 1.0, smoothstep(0.06, 0.24, SunDir.z));
        SunT *= AntiCool;
        float3 Scat = SRay * PhaseR + float3(SMie * PhaseM);
        float  MsDir = 0.42 + 0.58 * (CosTheta * 0.5 + 0.5);

        RadAccum += TAccum * Scat * SunT * IntS;
        MsAccum  += TAccum * (SRay + float3(SMie)) * (1.0 / (4.0 * AtmoPiConst)) *
                    (SunT * (0.65 * MsDir) + OverheadT * (0.35 + 0.20 * (1.0 - MsDir))) * MsBounce * IntS;
        TAccum   *= SegT;
    }

    float LimbFactor = exp(-max(0.0, Rd.z) * 7.5);
    float GlowScale  = 1.0 + (Uni.GroundAlbedoLimbGlow.w - 1.0) * LimbFactor * 0.45;
    float SunElevDeg = asin(clamp(SunDir.z, -1.0, 1.0)) * (180.0 / AtmoPiConst);
    float DayVis     = clamp((SunElevDeg + 6.0) / 14.0, 0.0, 1.0);
    float3 NightAir  = float3(0.0005, 0.0008, 0.0020) + float3(0.0007, 0.0011, 0.0022) * LimbFactor;

    // Pure physical Rayleigh + Mie + Ozone scattering (no artificial 360-deg white DawnMist stripe)
    float3 OutRgb = ((RadAccum + MsAccum) * SolarScale * GlowScale + NightAir * (1.0 - DayVis * 0.9)) *
                    Uni.SkyTintBrightness.xyz * Uni.SkyTintBrightness.w;

    if (GroundRay)
    {
        float SunUp     = max(0.0, SunDir.z);
        float Depth     = clamp(-RawDir.z, 0.0, 1.0);
        float HazeBlend = smoothstep(0.0, 0.095, Depth);
        float NadirFade = smoothstep(0.05, 0.42, Depth);
        float DayScale  = 0.14 + 0.86 * SunUp;
        float NightAdd  = 1.0 - DayVis * 0.85;

        float3 CoolMix = float3(
            min(Uni.GroundAlbedoLimbGlow.x, Uni.GroundAlbedoLimbGlow.z * 0.95) * 0.03,
            min(Uni.GroundAlbedoLimbGlow.y, Uni.GroundAlbedoLimbGlow.z) * 0.04,
            Uni.GroundAlbedoLimbGlow.z * 0.06);

        float3 HorizonHaze = OutRgb * lerp(1.0, 0.18, HazeBlend);
        float3 AtmoNadir   = (float3(0.022, 0.052, 0.115) + CoolMix) * DayScale * OverheadT +
                             NightAir * NightAdd * 1.25;
        float3 LowerAtmo   = lerp(HorizonHaze, AtmoNadir, HazeBlend);
        OutRgb = lerp(LowerAtmo, AtmoNadir * 0.62, NadirFade * 0.78);
    }
    return max(float3(0.0), OutRgb);
}

float3 EvaluateOrbitalSpaceGpu(
    float3                     RayDir,
    float3                     CamOriginM,
    float3                     SunDir,
    AtmosphericSkyUniformBlock Uni,
    float                      SimulationSeconds,
    float                      CloudCoverage01,
    out float3                 OutSpaceTransmittance)
{
    float PlanetR  = max(500000.0, Uni.PlanetDimensions.x);
    float AtmoTop  = max(60000.0, Uni.PlanetDimensions.y * 1.65);
    float AtmoR    = PlanetR + AtmoTop;
    float ObsAlt   = max(AtmoTop * 1.15, Uni.ObserverParams.x);
    float RayMul   = max(0.0, Uni.ScatteringStrengths.x);
    float MieMul   = max(0.0, Uni.ScatteringStrengths.y);
    float MieG     = clamp(Uni.ScatteringStrengths.z, 0.0, 0.99);
    float OzoneMul = max(0.0, Uni.ScatteringStrengths.w);
    float RayH     = max(500.0, Uni.PlanetDimensions.z);
    float MieH     = max(200.0, Uni.PlanetDimensions.w);

    float3 Origin  = CamOriginM + float3(0.0, 0.0, PlanetR + ObsAlt);
    float2 AtmoHit = IntersectVecSphereAtmo(Origin, RayDir, AtmoR);
    if (AtmoHit.y <= 0.0)
    {
        OutSpaceTransmittance = float3(1.0, 1.0, 1.0);
        return float3(0.0, 0.0, 0.0);
    }

    float2 PlanetHit = IntersectVecSphereAtmo(Origin, RayDir, PlanetR);
    bool   HitPlanet = (PlanetHit.x > 0.0 && PlanetHit.x < AtmoHit.y);
    float  RayStart  = max(0.0, AtmoHit.x);
    float  RayEnd    = HitPlanet ? PlanetHit.x : AtmoHit.y;
    float  PathLen   = max(0.0, RayEnd - RayStart);
    if (PathLen <= 1.0)
    {
        OutSpaceTransmittance = float3(1.0, 1.0, 1.0);
        return float3(0.0, 0.0, 0.0);
    }

    float CosTheta = dot(RayDir, SunDir);
    float PhaseR   = (3.0 / (16.0 * AtmoPiConst)) * (1.0 + CosTheta * CosTheta);
    float G2       = MieG * MieG;
    float Denom    = max(1.0e-3, 1.0 + G2 - 2.0 * MieG * CosTheta);
    float PhaseM   = (3.0 / (8.0 * AtmoPiConst)) * ((1.0 - G2) * (1.0 + CosTheta * CosTheta)) /
                     ((2.0 + G2) * pow(Denom, 1.5));

    float3 RayBeta   = float3(5.802e-6, 13.558e-6, 33.1e-6);
    float3 OzoneBeta = float3(0.650e-6, 1.881e-6, 0.085e-6);
    float  Step      = PathLen / 32.0;
    float3 TAccum    = float3(1.0, 1.0, 1.0);
    float3 RadAccum  = float3(0.0, 0.0, 0.0);

    for (uint S = 0u; S < 32u; ++S)
    {
        float  T   = RayStart + (float(S) + 0.5) * Step;
        float3 P   = Origin + RayDir * T;
        float  Alt = max(0.0, length(P) - PlanetR);

        float  DenR = exp(-Alt / RayH) * RayMul;
        float  DenM = exp(-Alt / MieH) * MieMul;
        float  DenO = max(0.0, 1.0 - abs(Alt - 25000.0) / 15000.0) * OzoneMul;

        float3 SRay = RayBeta * DenR;
        float  SMie = 3.996e-6 * DenM;
        float  EMie = 4.40e-6 * DenM;

        float3 Ext  = max(float3(1.0e-7), SRay + float3(EMie) + OzoneBeta * DenO);
        float3 SegT = exp(-Ext * Step);
        float3 IntS = (float3(1.0) - SegT) / Ext;

        float3 SunT = ComputeAtmoTransmittanceAlongRay(P, SunDir, PlanetR, AtmoR, RayMul, MieMul, OzoneMul, RayH, MieH);
        float3 Scat = SRay * PhaseR + float3(SMie * PhaseM);
        RadAccum += TAccum * Scat * SunT * IntS;
        TAccum   *= SegT;
    }

    float SolarScale = max(0.0, Uni.SunDirectionIntensity.w) * 0.30;
    float3 OutRgb = RadAccum * SolarScale * Uni.SkyTintBrightness.xyz * Uni.SkyTintBrightness.w;

    if (HitPlanet)
    {
        float3 HitP = Origin + RayDir * PlanetHit.x;
        float3 N    = normalize(HitP);

        float ContN = FbmSkyGpu3(N * 3.1 + float3(1.2, -0.8, 0.5), 4u);
        float Land  = smoothstep(0.49, 0.54, ContN);
        float Moist = FbmSkyGpu3(N * 6.5 + float3(-2.1, 1.4, 0.0), 3u);
        float Ice   = smoothstep(0.74, 0.90, abs(N.y) + 0.14 * (ContN - 0.5));

        float3 OceanAlb = float3(0.015, 0.055, 0.140);
        float3 LandAlb  = float3(0.10 + 0.12 * (1.0 - Moist), 0.14 + 0.08 * Moist, 0.06 + 0.04 * (1.0 - Moist));
        float3 SurfAlb  = lerp(lerp(OceanAlb, LandAlb, Land), float3(0.78, 0.82, 0.88), Ice);

        float Drift     = SimulationSeconds * 0.008;
        float Swirl     = FbmSkyGpu3(N * 4.5 + float3(Drift, 0.0, 0.0), 3u);
        float CloudN    = FbmSkyGpu3(float3(N.x * 7.2 + Drift * 1.4 + Swirl * 0.65, N.y * 9.0 - Swirl * 0.45, N.z * 7.2 + Drift * 0.7), 4u);
        float CovThresh = 1.0 - clamp(CloudCoverage01, 0.05, 0.95) * 0.68;
        float CloudMask = smoothstep(CovThresh - 0.14, CovThresh + 0.16, CloudN);

        float  NdotL    = max(0.0, dot(N, SunDir));
        float  TwiWrap  = clamp((dot(N, SunDir) + 0.16) / 0.34, 0.0, 1.0);
        float3 SurfSunT = ComputeAtmoTransmittanceAlongRay(HitP + N * 120.0, SunDir, PlanetR, AtmoR, RayMul, MieMul, OzoneMul, RayH, MieH);

        float3 HalfV = normalize(SunDir - RayDir);
        float  Spec  = (1.0 - Land) * (1.0 - Ice) * (1.0 - CloudMask) * pow(max(0.0, dot(N, HalfV)), 64.0) * 0.55;

        float  SurfShade  = NdotL * (1.0 - CloudMask * 0.72);
        float  CloudShade = max(0.04, NdotL * 0.95 + TwiWrap * 0.08);
        float3 LitPlanet  = (SurfAlb * SurfShade + float3(0.90, 0.92, 0.96) * CloudMask * CloudShade + float3(Spec * NdotL)) *
                            SurfSunT * SolarScale * 1.45;

        float NightMask = smoothstep(0.06, -0.14, dot(N, SunDir)) * Land * (1.0 - Ice);
        float CityGlow  = smoothstep(0.76, 0.94, NoiseSkyGpu3(N * 42.0)) * NightMask * (1.0 - CloudMask * 0.7) * 0.045;
        OutRgb += TAccum * (LitPlanet + float3(1.0, 0.65, 0.25) * CityGlow);

        OutSpaceTransmittance = float3(0.0, 0.0, 0.0);
    }
    else
    {
        OutSpaceTransmittance = TAccum;
    }

    return max(float3(0.0), OutRgb);
}

#ifndef FRONTIER_ATMOSPHERE_EMBEDDED
[[vk::binding(0, 0)]] ConstantBuffer<AtmosphericSkyUniformBlock> SkyUniforms;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>               OutSkyViewLut;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint W = max(1u, SkyUniforms.LutDimensions.x);
    uint H = max(1u, SkyUniforms.LutDimensions.y);
    if (DispatchThreadID.x >= W || DispatchThreadID.y >= H) return;

    float2 Uv = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(W), float(H));
    float3 SkyCol = ComputePhysicalSkyTexel(SkyUniforms, Uv);
    OutSkyViewLut[DispatchThreadID.y * W + DispatchThreadID.x] = float4(SkyCol, 1.0);
}
#endif
