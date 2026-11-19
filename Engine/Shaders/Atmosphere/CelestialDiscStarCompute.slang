/*============================================================================================================================================
                                                      CELESTIALDISCSTARCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Neckel & Labs (1994) Limb-Darkened Sun Disc + erfc PSF + K/F Corona + Radiant Glare + Sub-Pixel SolarErf, 6x Multi-Moon & Starfield

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
    float4 MetaMask;           // x = Visible (0/1), y = ExosphereIntensity, z = PresetIndex (0..5), w = ExosphereThicknessFrac
};

struct CelestialFieldUniformBlock
{
    float4 SunAxisRadius;        // xyz = UnitSunDir, w = SunHalfAngleRad
    float4 SunTintIntensity;     // xyz = EffectiveSolarTint, w = SunDiscIntensity
    float4 StarParams;           // x = StarsEnabled(0/1), y = LimMag, z = Bright, w = PointSize
    float4 StarScintillationRot; // x = TwinkleEnabled(0/1), y = DepthPct, z = RateHz, w = RotRad
    float4 ClockViewportDims;    // x = SimulationSeconds, y = ViewportW, z = ViewportH, w = MoonCount (up to 6)
    float4 AtmoMediumPack;       // x = RayleighScale, y = MieScale, z = OzoneScale, w = Flags (bit0=AtmoEnabled, bit1=OrbitalView, bit2=GalacticBand)
    float4 SolarOpticsParams;    // x = SunGlareStrength, y = PixelAngularSizeRad, z = ObserverAltitudeMeters, w = PlanetRadiusMeters
    GpuMoonDescriptor Moons[6];
};

float HashCelestial3(int IX, int IY, int IZ)
{
    uint N = uint(IX) * 374761393u + uint(IY) * 668265263u + uint(IZ) * 2147483647u;
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

float3 EvaluateStarBlackbodyGpu(float BvIndex)
{
    float TNorm = clamp((BvIndex + 0.35) / 2.20, 0.0, 1.0);
    if (TNorm < 0.25)
    {
        float K = TNorm / 0.25;
        return float3(0.68 + 0.26 * K, 0.82 + 0.14 * K, 1.00);
    }
    if (TNorm < 0.55)
    {
        float K = (TNorm - 0.25) / 0.30;
        return float3(0.94 + 0.06 * K, 0.96 - 0.04 * K, 1.00 - 0.18 * K);
    }
    float K = (TNorm - 0.55) / 0.45;
    return float3(1.00, 0.92 - 0.28 * K, 0.82 - 0.46 * K);
}

float EvaluateSolarErfGpu(float Argument)
{
    float Variable = 1.0 / (1.0 + 0.3275911 * abs(Argument));
    float Polynomial =
        (((((1.061405429 * Variable - 1.453152027) * Variable + 1.421413741) * Variable
            - 0.284496736) * Variable + 0.254829592) * Variable);
    float Result = 1.0 - Polynomial * exp(-Argument * Argument);
    return (Argument < 0.0) ? -Result : Result;
}

float3 EvaluateNeckelLabsLimbDarkeningGpu(float MuPhotosphere)
{
    float Mu  = clamp(MuPhotosphere, 0.0, 1.0);
    float Mu2 = Mu * Mu;
    float Mu3 = Mu2 * Mu;
    float Mu4 = Mu2 * Mu2;
    float Mu5 = Mu4 * Mu;

    // Exact published Neckel & Labs (1994, Solar Physics 153) polynomial coefficients at 650 / 550 / 450 nm
    float3 Ld = float3(0.34685, 0.23698, 0.11903)
              + float3(1.37539, 1.37916, 1.24089) * Mu
              + float3(-2.04425, -1.83226, -1.19977) * Mu2
              + float3(2.70493, 2.34540, 1.33936) * Mu3
              + float3(-1.94290, -1.65319, -0.98690) * Mu4
              + float3(0.55999, 0.52391, 0.48739) * Mu5;
    return clamp(Ld, float3(0.0, 0.0, 0.0), float3(1.05, 1.05, 1.05));
}

float EvaluateSolarGlareSpreadGpu(float SeparationRadians, float DiscRadiusRadians)
{
    static const float SolarGlareReachRadians   = 30.0 * 0.01745329251994329577;
    static const float SolarGlareBloomWeight    = 1.50;
    static const float SolarGlareAureoleWeight  = 0.25;
    static const float SolarGlareAureoleDegrees = 6.0;

    if (SeparationRadians >= SolarGlareReachRadians)
    {
        return 0.0;
    }
    float SepDegrees   = SeparationRadians * 57.2957795130823208768;
    float DiscDegrees  = DiscRadiusRadians * 57.2957795130823208768;
    float Effective    = sqrt(SepDegrees * SepDegrees + DiscDegrees * DiscDegrees);
    float Window       = 1.0 - SeparationRadians / SolarGlareReachRadians;
    float Bloom        = SolarGlareBloomWeight / ((Effective + 0.02) * (Effective + 0.02));
    float AureoleRatio = Effective / SolarGlareAureoleDegrees;
    float Aureole      = SolarGlareAureoleWeight / (1.0 + AureoleRatio * AureoleRatio);
    return (Bloom + Aureole) * Window * Window;
}

bool QueryGroundHorizonBlockedGpu(CelestialFieldUniformBlock Cfg, float DirZ)
{
    if (DirZ >= 0.0)
    {
        return false;
    }
    float PlanetR = (Cfg.SolarOpticsParams.w > 1000.0) ? Cfg.SolarOpticsParams.w : 6360000.0;
    float ObsAlt  = max(5.0, Cfg.SolarOpticsParams.z);
    float ViewerR = PlanetR + ObsAlt;
    float Disc    = ViewerR * ViewerR * (DirZ * DirZ - 1.0) + PlanetR * PlanetR;
    return Disc >= 0.0;
}

float3 EvaluateCelestialAtmoTransmittance(CelestialFieldUniformBlock Cfg, float3 RayDir)
{
    uint Flags        = uint(Cfg.AtmoMediumPack.w + 0.5);
    bool AtmoEnabled  = (Flags & 1u) != 0u;
    bool OrbitalView  = (Flags & 2u) != 0u;
    if (!AtmoEnabled || OrbitalView || RayDir.z < -0.05)
    {
        return float3(1.0, 1.0, 1.0);
    }
    float RayScale = max(0.0, Cfg.AtmoMediumPack.x);
    float MieScale = max(0.0, Cfg.AtmoMediumPack.y);
    float OzScale  = max(0.0, Cfg.AtmoMediumPack.z);

    float CosZ    = clamp(RayDir.z, 0.012, 1.0);
    float ElevDeg = asin(CosZ) * (180.0 / CelestialPiConst);
    float AirMass = 1.0 / (CosZ + 0.50572 * pow(max(0.5, ElevDeg + 6.07995), -1.6364));
    AirMass       = min(AirMass, 38.0);

    float3 RayTau = float3(0.005802, 0.013558, 0.033100) * (RayScale * 7.8 * AirMass);
    float3 MieTau = float3(0.004440, 0.004440, 0.004440) * (MieScale * 1.45 * AirMass);
    float3 OzTau  = float3(0.000650, 0.001881, 0.000085) * (OzScale  * 12.0 * AirMass);
    return exp(-(RayTau + MieTau + OzTau));
}

struct LunarExosphereGpuProfile
{
    float3 LimbTint;
    float  LimbStrength;
    float  Roughness;
    float  LommelSeeligerW;
    float  OppositionAmp;
    float  OppositionWidth;
    float  ReliefStrength;
    float  IceSpecular;
    float  RayScaleFrac;
    float  MieScaleFrac;
    float  MieAsymmetry;
    float3 RayScatter;
    float3 MieScatter;
    float3 MieExtinction;
    float  DetachedHazeFrac;
    float  DetachedWidthFrac;
    float3 DetachedScatter;
};

LunarExosphereGpuProfile QueryLunarExosphereGpuProfile(uint PresetIdx)
{
    LunarExosphereGpuProfile P;
    if (PresetIdx == 1u)
    {
        P.LimbTint = float3(1.00, 0.44, 0.16); P.LimbStrength = 0.48;
        P.Roughness = 0.55; P.LommelSeeligerW = 0.55; P.OppositionAmp = 0.35; P.OppositionWidth = 0.100; P.ReliefStrength = 1.25; P.IceSpecular = 0.00;
        P.RayScaleFrac = 0.16; P.MieScaleFrac = 0.07; P.MieAsymmetry = 0.70;
        P.RayScatter = float3(0.35, 0.82, 1.98); P.MieScatter = float3(2.25, 1.32, 0.58); P.MieExtinction = float3(2.65, 1.75, 0.98);
        P.DetachedHazeFrac = 0.00; P.DetachedWidthFrac = 0.06; P.DetachedScatter = float3(0.0, 0.0, 0.0);
        return P;
    }
    if (PresetIdx == 2u)
    {
        P.LimbTint = float3(0.40, 0.74, 1.00); P.LimbStrength = 0.62;
        P.Roughness = 0.30; P.LommelSeeligerW = 0.35; P.OppositionAmp = 0.45; P.OppositionWidth = 0.050; P.ReliefStrength = 1.10; P.IceSpecular = 0.45;
        P.RayScaleFrac = 0.22; P.MieScaleFrac = 0.16; P.MieAsymmetry = 0.80;
        P.RayScatter = float3(0.28, 0.92, 1.75); P.MieScatter = float3(0.44, 0.64, 0.82); P.MieExtinction = float3(0.48, 0.68, 0.86);
        P.DetachedHazeFrac = 0.00; P.DetachedWidthFrac = 0.06; P.DetachedScatter = float3(0.0, 0.0, 0.0);
        return P;
    }
    if (PresetIdx == 3u)
    {
        P.LimbTint = float3(1.00, 0.84, 0.34); P.LimbStrength = 0.52;
        P.Roughness = 0.48; P.LommelSeeligerW = 0.60; P.OppositionAmp = 0.40; P.OppositionWidth = 0.090; P.ReliefStrength = 1.00; P.IceSpecular = 0.00;
        P.RayScaleFrac = 0.24; P.MieScaleFrac = 0.12; P.MieAsymmetry = 0.66;
        P.RayScatter = float3(0.58, 0.34, 1.48); P.MieScatter = float3(1.75, 1.24, 0.34); P.MieExtinction = float3(1.98, 1.54, 0.72);
        P.DetachedHazeFrac = 0.00; P.DetachedWidthFrac = 0.06; P.DetachedScatter = float3(0.0, 0.0, 0.0);
        return P;
    }
    if (PresetIdx == 4u)
    {
        P.LimbTint = float3(0.44, 0.68, 0.98); P.LimbStrength = 0.84;
        P.Roughness = 0.50; P.LommelSeeligerW = 0.40; P.OppositionAmp = 0.15; P.OppositionWidth = 0.200; P.ReliefStrength = 0.75; P.IceSpecular = 0.00;
        P.RayScaleFrac = 0.14; P.MieScaleFrac = 0.15; P.MieAsymmetry = 0.64;
        P.RayScatter = float3(0.20, 0.44, 1.05); P.MieScatter = float3(2.45, 1.55, 0.58); P.MieExtinction = float3(3.15, 2.30, 1.42);
        P.DetachedHazeFrac = 0.72; P.DetachedWidthFrac = 0.07; P.DetachedScatter = float3(0.42, 0.82, 1.95);
        return P;
    }
    if (PresetIdx == 5u)
    {
        P.LimbTint = float3(0.42, 0.95, 0.86); P.LimbStrength = 0.48;
        P.Roughness = 0.58; P.LommelSeeligerW = 0.65; P.OppositionAmp = 0.50; P.OppositionWidth = 0.070; P.ReliefStrength = 1.45; P.IceSpecular = 0.00;
        P.RayScaleFrac = 0.20; P.MieScaleFrac = 0.10; P.MieAsymmetry = 0.70;
        P.RayScatter = float3(0.48, 0.78, 1.85); P.MieScatter = float3(0.26, 0.28, 0.32); P.MieExtinction = float3(0.34, 0.36, 0.42);
        P.DetachedHazeFrac = 0.48; P.DetachedWidthFrac = 0.08; P.DetachedScatter = float3(0.48, 0.74, 1.48);
        return P;
    }
    P.LimbTint = float3(0.78, 0.86, 0.98); P.LimbStrength = 0.06;
    P.Roughness = 0.62; P.LommelSeeligerW = 0.78; P.OppositionAmp = 0.60; P.OppositionWidth = 0.075; P.ReliefStrength = 1.40; P.IceSpecular = 0.00;
    P.RayScaleFrac = 0.33; P.MieScaleFrac = 0.13; P.MieAsymmetry = 0.62;
    P.RayScatter = float3(0.14, 0.19, 0.30); P.MieScatter = float3(0.95, 0.78, 0.35); P.MieExtinction = float3(1.10, 0.92, 0.46);
    P.DetachedHazeFrac = 0.00; P.DetachedWidthFrac = 0.06; P.DetachedScatter = float3(0.0, 0.0, 0.0);
    return P;
}

float3 ResolveRadialChordSegmentMoonGpu(uint Ordinal, uint Count, float Start, float End, float ImpactR)
{
    float ImpactSq   = ImpactR * ImpactR;
    float Pivot      = clamp(0.0, Start, End);
    float PivotRad   = sqrt(ImpactSq + Pivot * Pivot);
    float FrontRange = max(0.0, sqrt(ImpactSq + Start * Start) - PivotRad);
    float RearRange  = max(0.0, sqrt(ImpactSq + End * End) - PivotRad);

    uint FrontCount = 0u;
    if (FrontRange > 1.0e-6 && RearRange > 1.0e-6)
    {
        FrontCount = clamp(uint(floor(float(Count) * FrontRange / (FrontRange + RearRange) + 0.5)), 1u, Count - 1u);
    }
    else if (FrontRange > 1.0e-6)
    {
        FrontCount = Count;
    }

    float Lo, Hi, Mid;
    if (Ordinal < FrontCount)
    {
        float StartRad = sqrt(ImpactSq + Start * Start);
        float Inc      = FrontRange / float(FrontCount);
        float RL = StartRad - Inc * float(Ordinal);
        float RH = StartRad - Inc * float(Ordinal + 1u);
        float RM = StartRad - Inc * (float(Ordinal) + 0.5);
        Lo  = (Ordinal == 0u) ? Start : -sqrt(max(0.0, RL * RL - ImpactSq));
        Hi  = (Ordinal + 1u == FrontCount) ? Pivot : -sqrt(max(0.0, RH * RH - ImpactSq));
        Mid = -sqrt(max(0.0, RM * RM - ImpactSq));
    }
    else
    {
        uint  RearOrd   = Ordinal - FrontCount;
        uint  RearCount = max(1u, Count - FrontCount);
        float Inc       = RearRange / float(RearCount);
        float RL = PivotRad + Inc * float(RearOrd);
        float RH = PivotRad + Inc * float(RearOrd + 1u);
        float RM = PivotRad + Inc * (float(RearOrd) + 0.5);
        Lo  = (RearOrd == 0u) ? Pivot : sqrt(max(0.0, RL * RL - ImpactSq));
        Hi  = (RearOrd + 1u == RearCount) ? End : sqrt(max(0.0, RH * RH - ImpactSq));
        Mid = sqrt(max(0.0, RM * RM - ImpactSq));
    }
    return float3(Lo, Hi, clamp(Mid, Lo, Hi));
}

void EvaluateLunarExosphereMediumGpu(
    LunarExosphereGpuProfile Exo,
    float3                   ExoTint,
    float                    ExoStr,
    float                    ExoThick,
    float                    AltNorm,
    out float3               OutRayScatter,
    out float3               OutMieScatter,
    out float3               OutScattering,
    out float3               OutExtinction)
{
    if (AltNorm > ExoThick || ExoStr <= 1.0e-4)
    {
        OutRayScatter = float3(0.0, 0.0, 0.0);
        OutMieScatter = float3(0.0, 0.0, 0.0);
        OutScattering = float3(0.0, 0.0, 0.0);
        OutExtinction = float3(0.0, 0.0, 0.0);
        return;
    }
    float Alt   = max(0.0, AltNorm);
    float Taper = 1.0 - smoothstep(ExoThick * 0.72, ExoThick, Alt);
    float HR    = max(0.005, Exo.RayScaleFrac * ExoThick);
    float HM    = max(0.004, Exo.MieScaleFrac * ExoThick);
    float DenR  = exp(-Alt / HR) * Taper;
    float DenM  = exp(-Alt / HM) * Taper;

    float DenH = 0.0;
    if (Exo.DetachedHazeFrac > 0.0)
    {
        float HazeAlt   = Exo.DetachedHazeFrac * ExoThick;
        float HazeWidth = max(0.005, Exo.DetachedWidthFrac * ExoThick);
        float Off       = (Alt - HazeAlt) / HazeWidth;
        DenH = exp(-Off * Off) * Taper;
    }

    float  Gain = (ExoStr * 1.15) / max(0.05, ExoThick);
    float3 MixT = float3(0.55) + 0.45 * ExoTint;
    OutRayScatter = Exo.RayScatter * (DenR * Gain) * MixT;
    OutMieScatter = (Exo.MieScatter * DenM + Exo.DetachedScatter * DenH) * Gain * MixT;
    OutScattering = OutRayScatter + OutMieScatter;
    OutExtinction = OutRayScatter + (Exo.MieExtinction * DenM + Exo.DetachedScatter * DenH) * Gain;
}

float3 EvaluateLunarSunTransmittanceGpu(
    LunarExosphereGpuProfile Exo,
    float3                   ExoTint,
    float                    ExoStr,
    float                    ExoThick,
    float3                   SamplePos,
    float3                   SunLocal,
    uint                     Steps)
{
    float AlongSun = dot(SamplePos, SunLocal);
    float RadSq    = dot(SamplePos, SamplePos);
    float ImpactSq = max(0.0, RadSq - AlongSun * AlongSun);

    float Umbra = 1.0;
    if (AlongSun < 0.0)
    {
        float PenumbraW = 0.008 * abs(AlongSun) + 0.015;
        Umbra = smoothstep(1.0 - PenumbraW, 1.0 + PenumbraW, sqrt(ImpactSq));
        if (Umbra <= 0.0) return float3(0.0, 0.0, 0.0);
    }
    if (ExoStr <= 1.0e-4) return float3(Umbra, Umbra, Umbra);

    float TopR     = 1.0 + ExoThick;
    float ExitDisc = AlongSun * AlongSun - (RadSq - TopR * TopR);
    if (ExitDisc <= 0.0) return float3(Umbra, Umbra, Umbra);
    float ExitDist = -AlongSun + sqrt(ExitDisc);
    if (ExitDist <= 0.0) return float3(Umbra, Umbra, Umbra);

    float  SunImpact = sqrt(ImpactSq);
    uint   NSteps    = max(4u, Steps);
    float3 OptDepth  = float3(0.0, 0.0, 0.0);
    for (uint S = 0u; S < NSteps; ++S)
    {
        float3 Seg   = ResolveRadialChordSegmentMoonGpu(S, NSteps, AlongSun, ExitDist + AlongSun, SunImpact);
        float  Delta = Seg.y - Seg.x;
        float3 P     = SamplePos + SunLocal * (Seg.z - AlongSun);
        float3 RSc, MSc, Sc, Ext;
        EvaluateLunarExosphereMediumGpu(Exo, ExoTint, ExoStr, ExoThick, length(P) - 1.0, RSc, MSc, Sc, Ext);
        OptDepth += Ext * Delta;
    }
    return Umbra * exp(-OptDepth);
}

float EvaluateLunarRegolithBrdfGpu(float3 ShadingNormal, float3 SunLocal, float3 ViewLocal, float Roughness, float LommelSeeligerW)
{
    float Mu0 = dot(ShadingNormal, SunLocal);
    if (Mu0 <= 0.0) return 0.0;
    float MuObs = max(1.0e-3, dot(ShadingNormal, ViewLocal));
    float Ls    = 2.0 * Mu0 / (Mu0 + MuObs);

    float  Var   = Roughness * Roughness;
    float  TermA = 1.0 - 0.5 * Var / (Var + 0.33);
    float  TermB = 0.45 * Var / (Var + 0.09);
    float3 SunT  = SunLocal - ShadingNormal * Mu0;
    float3 ViewT = ViewLocal - ShadingNormal * MuObs;
    float  LenP  = length(SunT) * length(ViewT);
    float  AzCos = (LenP > 1.0e-6) ? max(0.0, dot(SunT, ViewT) / LenP) : 0.0;
    float  IncA  = acos(clamp(Mu0, 0.0, 1.0));
    float  EmA   = acos(clamp(MuObs, 0.0, 1.0));
    float  Alpha = max(IncA, EmA);
    float  Beta  = min(IncA, EmA);
    float  On    = Mu0 * (TermA + TermB * AzCos * sin(Alpha) * tan(min(Beta, 1.5)));

    float W = clamp(LommelSeeligerW, 0.0, 1.0);
    return W * Ls + (1.0 - W) * On;
}

float3 EvaluateLunarDiscPointGpu(
    StructuredBuffer<uint>   LunarAtlas,
    uint                     PresetIdx,
    LunarExosphereGpuProfile Exo,
    float3                   ExoTint,
    float                    ExoStr,
    float                    ExoThick,
    float                    CosP,
    float                    SinP,
    float3                   SunLocal,
    float3                   ViewLocal,
    float                    BrightnessMul,
    float                    Earthshine,
    float                    SX,
    float                    SY)
{
    float SZ    = sqrt(max(0.0, 1.0 - SX * SX - SY * SY));
    float SurfY = SY * CosP - SZ * SinP;
    float SurfZ = SY * SinP + SZ * CosP;

    float  LonU      = atan2(SX, SurfZ) / CelestialTauConst + 0.5;
    float  LatV      = acos(clamp(SurfY, -1.0, 1.0)) / CelestialPiConst;
    float3 TexAlbedo = SampleLunarAtlasGpu(LunarAtlas, PresetIdx, LonU, LatV);

    float TexU = 1.0 / float(MoonAtlasTileW);
    float TexV = 1.0 / float(MoonAtlasTileH);
    float3 LumW = float3(0.2126, 0.7152, 0.0722);
    float HEast  = dot(SampleLunarAtlasGpu(LunarAtlas, PresetIdx, LonU + TexU, LatV), LumW);
    float HWest  = dot(SampleLunarAtlasGpu(LunarAtlas, PresetIdx, LonU - TexU, LatV), LumW);
    float HNorth = dot(SampleLunarAtlasGpu(LunarAtlas, PresetIdx, LonU, clamp(LatV - TexV, 0.0, 1.0)), LumW);
    float HSouth = dot(SampleLunarAtlasGpu(LunarAtlas, PresetIdx, LonU, clamp(LatV + TexV, 0.0, 1.0)), LumW);

    float  CosLat = max(0.25, sqrt(max(0.0, 1.0 - SurfY * SurfY)));
    float  GradE  = (HEast - HWest) * 0.5 * Exo.ReliefStrength * 1.4 / CosLat;
    float  GradN  = (HNorth - HSouth) * 0.5 * Exo.ReliefStrength * 1.4;
    float  Phi    = atan2(SX, SurfZ);
    float3 BodyN  = float3(SX, SurfY, SurfZ);
    float3 EastB  = float3(cos(Phi), 0.0, -sin(Phi));
    float3 NorthB = normalize(cross(BodyN, EastB));
    float3 PertB  = normalize(BodyN - EastB * GradE - NorthB * GradN);
    float3 ShadeN = float3(PertB.x, PertB.y * CosP + PertB.z * SinP, -PertB.y * SinP + PertB.z * CosP);

    float3 SurfN    = float3(SX, SY, SZ);
    float  GeoDot   = dot(SurfN, SunLocal);
    float  TermGate = smoothstep(-0.06, 0.02, GeoDot);
    float  PhaseAbs = acos(clamp(dot(SunLocal, ViewLocal), -1.0, 1.0));
    float  HalfTan  = tan(clamp(0.5 * PhaseAbs, 0.0, 1.55));
    float  HapkeOpp = 1.0 + Exo.OppositionAmp / (1.0 + HalfTan / max(1.0e-3, Exo.OppositionWidth));
    float  Brdf     = EvaluateLunarRegolithBrdfGpu(ShadeN, SunLocal, ViewLocal, Exo.Roughness, Exo.LommelSeeligerW);
    float3 SurfSunT = (ExoStr > 0.01)
        ? EvaluateLunarSunTransmittanceGpu(Exo, ExoTint, ExoStr, ExoThick, SurfN, SunLocal, 6u)
        : float3(1.0, 1.0, 1.0);

    float Spec = 0.0;
    if (Exo.IceSpecular > 0.0 && GeoDot > 0.0)
    {
        float3 HalfV = normalize(SunLocal + ViewLocal);
        float  NH    = max(0.0, dot(ShadeN, HalfV));
        float  A2    = 0.22 * 0.22;
        float  Den   = NH * NH * (A2 - 1.0) + 1.0;
        float  D     = A2 / (CelestialPiConst * Den * Den);
        float  VDot  = clamp(dot(ViewLocal, HalfV), 0.0, 1.0);
        float  F     = 0.02 + 0.98 * pow(1.0 - VDot, 5.0);
        Spec = Exo.IceSpecular * D * F * TermGate * 0.25;
    }

    float DirectGain = Brdf * HapkeOpp * TermGate * 0.88 * BrightnessMul;
    float NightGain  = Earthshine * BrightnessMul;
    return TexAlbedo * (DirectGain * SurfSunT + float3(NightGain)) + float3(Spec * BrightnessMul) * SurfSunT;
}

static const float SiteSiderealZeroGpu = 5.45 * (6.28318530718 / 24.0);
static const float SiteObsLatRadGpu    = -26.19 * (3.14159265359 / 180.0);
static const uint  StellarTileStrideGpu = 65u;

struct GpuProjectedStar
{
    float4 PixPosFlux;       // x = PixX, y = PixY, z = PogsonFlux, w = Seed01
    float4 ChromaticityElev; // rgb = PlanckianRec709, w = HorizonSinElev
};

float3 TransformHorizonToEquatorialGpu(float3 HorizonDir, float CelestialRotRad)
{
    float North      = HorizonDir.x;
    float Up         = HorizonDir.z;
    float SinPhi     = sin(SiteObsLatRadGpu);
    float CosPhi     = cos(SiteObsLatRadGpu);
    float SinDec     = clamp(SinPhi * Up + CosPhi * North, -1.0, 1.0);
    float CosDecCosH = CosPhi * Up - SinPhi * North;
    float CosDecSinH = HorizonDir.y;
    float Lst        = SiteSiderealZeroGpu + CelestialRotRad;
    float CosL       = cos(Lst);
    float SinL       = sin(Lst);
    return normalize(float3(
        CosL * CosDecCosH + SinL * CosDecSinH,
        SinL * CosDecCosH - CosL * CosDecSinH,
        SinDec));
}

float EvaluateKastenYoungAirmassGpu(float SinElevation)
{
    float SinClamped = max(SinElevation, 0.004);
    float ElevDeg    = asin(clamp(SinClamped, -1.0, 1.0)) * (180.0 / CelestialPiConst);
    return 1.0 / (SinClamped + 0.50572 * pow(max(0.5, ElevDeg + 6.07995), -1.6364));
}

float3 TransformEquatorialToGalacticGpu(float3 Eq)
{
    float GX = -0.0548755604 * Eq.x - 0.8734370902 * Eq.y - 0.4838350155 * Eq.z;
    float GY =  0.4941094279 * Eq.x - 0.4448296300 * Eq.y + 0.7469822445 * Eq.z;
    float GZ = -0.8676661490 * Eq.x - 0.1980763734 * Eq.y + 0.4559837762 * Eq.z;
    return float3(GX, GY, GZ);
}

float EvaluateGalacticQuintic3Gpu(float3 P)
{
    float3 I = floor(P);
    float3 F = P - I;
    float3 U = F * F * F * (F * (F * 6.0 - 15.0) + 10.0);
    int IX = int(I.x);
    int IY = int(I.y);
    int IZ = int(I.z);
    float N000 = HashCelestial3(IX,      IY,      IZ);
    float N100 = HashCelestial3(IX + 1,  IY,      IZ);
    float N010 = HashCelestial3(IX,      IY + 1,  IZ);
    float N110 = HashCelestial3(IX + 1,  IY + 1,  IZ);
    float N001 = HashCelestial3(IX,      IY,      IZ + 1);
    float N101 = HashCelestial3(IX + 1,  IY,      IZ + 1);
    float N011 = HashCelestial3(IX,      IY + 1,  IZ + 1);
    float N111 = HashCelestial3(IX + 1,  IY + 1,  IZ + 1);
    float X00  = lerp(N000, N100, U.x);
    float X10  = lerp(N010, N110, U.x);
    float X01  = lerp(N001, N101, U.x);
    float X11  = lerp(N011, N111, U.x);
    return lerp(lerp(X00, X10, U.y), lerp(X01, X11, U.y), U.z);
}

float EvaluateGalacticFbm3Gpu(float3 P)
{
    float Value = 0.0;
    float Amp   = 0.50;
    float3 Pos  = P;
    for (int Oct = 0; Oct < 5; ++Oct)
    {
        Value += Amp * EvaluateGalacticQuintic3Gpu(Pos);
        Pos = float3(
             0.80 * Pos.x - 0.48 * Pos.y + 0.36 * Pos.z,
             0.48 * Pos.x + 0.80 * Pos.y - 0.36 * Pos.z,
            -0.36 * Pos.x + 0.48 * Pos.y + 0.80 * Pos.z) * 2.02 + float3(13.1, 7.7, 5.3);
        Amp *= 0.50;
    }
    return Value;
}

float3 EvaluateGalacticBulgeRadianceGpu(float3 EqDir)
{
    float3 Gal    = TransformEquatorialToGalacticGpu(normalize(EqDir));
    float  LatSin = Gal.z;
    float  Lon    = atan2(Gal.y, Gal.x);

    float DiskProfile  = exp(-abs(LatSin) / 0.140) * exp(-(LatSin * LatSin) / 0.055);
    float BulgeR2      = (Lon * Lon) / 0.22 + (LatSin * LatSin) / 0.040;
    float BulgeProfile = exp(-BulgeR2);

    float Warp1 = EvaluateGalacticFbm3Gpu(Gal * 4.5 + float3(1.2, 3.4, 5.6));
    float Warp2 = EvaluateGalacticFbm3Gpu(Gal * 9.0 + float3(Warp1 * 0.65, 2.1, 8.3));
    float Cloud = EvaluateGalacticFbm3Gpu(Gal * 15.0 + float3(Warp1, Warp2, 0.0));

    float RiftLat = LatSin - 0.024 * sin(Lon * 2.2 + 0.5) - 0.042 * (Warp1 - 0.5);
    float Rift1   = exp(-(RiftLat * RiftLat) / (0.0011 + 0.0008 * Cloud));
    float Rift2   = exp(-((LatSin + 0.032 * cos(Lon * 3.1 - 0.8) + 0.02 * (Warp2 - 0.5)) * (LatSin + 0.032 * cos(Lon * 3.1 - 0.8) + 0.02 * (Warp2 - 0.5))) / 0.0008);
    float DustNoise = smoothstep(0.28, 0.78, Warp2 * 0.55 + Cloud * 0.45);
    float DarkRift  = (Rift1 * 0.82 + Rift2 * 0.46) * pow(DustNoise, 1.3);

    float CosCoal    = dot(Gal, normalize(float3(0.5403, -0.8415, 0.0065)));
    float CoalsackD  = exp(-(max(0.0, 1.0 - CosCoal)) * 260.0);
    float DustAtten  = clamp(1.0 - (DarkRift + CoalsackD * 0.78) * 0.88, 0.08, 1.0);

    float3 DiskCol   = float3(0.56, 0.72, 1.05);
    float3 BulgeCol  = float3(1.18, 0.88, 0.54);
    float  LonMod    = 0.50 + 0.50 * cos(Lon * 0.85);
    float  FineStar  = 0.28 + 0.72 * pow(clamp(Cloud * 1.20, 0.0, 1.0), 1.4);

    float3 Light = DiskCol * (DiskProfile * LonMod * (0.38 + 0.62 * Warp1) * 0.55)
                 + BulgeCol * (BulgeProfile * (0.45 + 0.55 * Warp2) * 1.18);
    Light *= FineStar * DustAtten;
    Light += float3(0.45, 0.58, 0.82) * (0.045 * exp(-abs(LatSin) / 0.28));
    return Light;
}

void ResolveNebulaTangentGpu(float3 EqDir, float3 TargetAxis, out float OutEast, out float OutNorth)
{
    float3 East  = normalize(cross(float3(0.0, 0.0, 1.0), TargetAxis));
    float3 North = cross(TargetAxis, East);
    float  Fwd   = max(1.0e-4, dot(EqDir, TargetAxis));
    OutEast  = dot(EqDir, East) / Fwd;
    OutNorth = dot(EqDir, North) / Fwd;
}

float3 EvaluateDeepSkyNebulaeGpu(float3 EqDir)
{
    float3 Dir = normalize(EqDir);
    float3 Out = float3(0.0, 0.0, 0.0);
    float  Deg = CelestialPiConst / 180.0;
    float  East = 0.0;
    float  North = 0.0;

    // M42 Orion
    {
        float3 Center = float3(0.107176, 0.989793, -0.093935);
        if (dot(Dir, Center) > 0.995)
        {
            ResolveNebulaTangentGpu(Dir, Center, East, North);
            float D2  = (East * East + North * North) / (Deg * Deg);
            float Fil = EvaluateGalacticFbm3Gpu(Dir * 120.0 + float3(2.1, 4.3, 6.5));
            float Glw = 0.030 * exp(-D2 / 0.10) + 0.010 * exp(-D2 / 0.55) * (0.4 + 1.2 * Fil);
            float Cav = 0.016 * exp(-D2 / 0.012);
            Out += float3(1.00, 0.30, 0.46) * Glw + float3(0.42, 0.92, 0.86) * Cav;
        }
    }
    // M45 Pleiades
    {
        float3 Center = float3(0.502764, 0.761902, 0.408330);
        if (dot(Dir, Center) > 0.997)
        {
            ResolveNebulaTangentGpu(Dir, Center, East, North);
            float D2   = (East * East + North * North) / (Deg * Deg);
            float Wisp = EvaluateGalacticFbm3Gpu(float3(Dir.x * 140.0, Dir.y * 45.0, Dir.z * 95.0));
            float Glo  = 0.0045 * exp(-D2 / 0.30) * (0.3 + 1.4 * Wisp);
            Out += float3(0.42, 0.62, 1.00) * Glo;
        }
    }
    // M31 Andromeda
    {
        float3 Center = float3(0.738590, 0.139291, 0.659608);
        if (dot(Dir, Center) > 0.997)
        {
            ResolveNebulaTangentGpu(Dir, Center, East, North);
            float PA  = 38.0 * Deg;
            float Maj = (North * cos(PA) + East * sin(PA)) / (1.6 * Deg);
            float Min = (East * cos(PA) - North * sin(PA)) / (0.42 * Deg);
            float Ell = sqrt(Maj * Maj + Min * Min);
            float Blg = 0.020 * exp(-Ell * Ell * 22.0);
            float Dsc = 0.0042 * exp(-Ell * 3.2);
            Out += float3(1.00, 0.86, 0.70) * Blg + float3(0.86, 0.88, 1.00) * Dsc;
        }
    }
    // Large Magellanic Cloud (LMC)
    {
        float3 Center = float3(0.054835, 0.341580, -0.938252);
        if (dot(Dir, Center) > 0.97)
        {
            ResolveNebulaTangentGpu(Dir, Center, East, North);
            float D2    = (East * East + North * North) / (Deg * Deg * 3.4 * 3.4);
            float Clump = EvaluateGalacticFbm3Gpu(Dir * 160.0 + float3(3.7, 8.1, 1.9));
            Out += float3(0.86, 0.90, 1.00) * (0.0060 * exp(-D2) * (0.35 + 1.3 * Clump));
        }
    }
    // Small Magellanic Cloud (SMC)
    {
        float3 Center = float3(0.287461, 0.067185, -0.955433);
        if (dot(Dir, Center) > 0.97)
        {
            ResolveNebulaTangentGpu(Dir, Center, East, North);
            float D2    = (East * East + North * North) / (Deg * Deg * 1.8 * 1.8);
            float Clump = EvaluateGalacticFbm3Gpu(Dir * 160.0 + float3(6.2, 2.4, 9.5));
            Out += float3(0.86, 0.90, 1.00) * (0.0035 * exp(-D2) * (0.35 + 1.3 * Clump));
        }
    }
    // M8 Lagoon Nebula
    {
        float3 Center = float3(0.014307, -0.910715, -0.412787);
        if (dot(Dir, Center) > 0.996)
        {
            ResolveNebulaTangentGpu(Dir, Center, East, North);
            float D2   = (East * East + North * North) / (Deg * Deg * 0.45 * 0.45);
            float Knot = EvaluateGalacticFbm3Gpu(Dir * 110.0 + float3(5.3, 1.4, 7.8));
            Out += float3(1.00, 0.36, 0.50) * (0.010 * exp(-D2) * (0.3 + 1.4 * Knot));
        }
    }
    // NGC 3372 Carina Nebula
    {
        float3 Center = float3(-0.475325, 0.161351, -0.864889);
        if (dot(Dir, Center) > 0.996)
        {
            ResolveNebulaTangentGpu(Dir, Center, East, North);
            float D2   = (East * East + North * North) / (Deg * Deg * 0.80 * 0.80);
            float Knot = EvaluateGalacticFbm3Gpu(Dir * 110.0 + float3(9.4, 2.7, 5.1));
            Out += float3(1.00, 0.36, 0.50) * (0.012 * exp(-D2) * (0.3 + 1.4 * Knot));
        }
    }
    return Out;
}

float3 EvaluateZodiacalLightGpu(float3 EqDir, float3 SolarEqDir)
{
    float3 EclipticPole = float3(0.0, -0.397777, 0.917482);
    static const float ProfileLongitudes[16] = {
        15.0, 20.0, 25.0, 30.0, 35.0, 40.0, 45.0, 60.0,
        75.0, 90.0, 105.0, 120.0, 140.0, 160.0, 170.0, 180.0
    };
    static const float ProfileS10[16] = {
        9000.0, 5000.0, 3500.0, 2690.0, 2000.0, 1440.0, 1080.0, 588.0,
        371.0, 261.0, 210.0, 185.0, 165.0, 170.0, 185.0, 200.0
    };
    float PoleS10     = 60.0;
    float RadToDeg    = 180.0 / CelestialPiConst;
    float LatSin      = clamp(dot(EqDir, EclipticPole), -1.0, 1.0);
    float LatDeg      = abs(asin(LatSin)) * RadToDeg;
    float3 ViewPlane  = EqDir - EclipticPole * LatSin;
    float3 SolarPlane = SolarEqDir - EclipticPole * dot(SolarEqDir, EclipticPole);
    float PlaneLens   = length(ViewPlane) * length(SolarPlane);
    if (PlaneLens < 1.0e-6)
    {
        return float3(1.0, 0.93, 0.80) * PoleS10;
    }

    float LonDeg      = acos(clamp(dot(ViewPlane, SolarPlane) / PlaneLens, -1.0, 1.0)) * RadToDeg;
    float EclipticS10 = ProfileS10[0];
    for (int Knot = 1; Knot < 16; ++Knot)
    {
        if (LonDeg <= ProfileLongitudes[Knot])
        {
            float Span = log(ProfileLongitudes[Knot] / ProfileLongitudes[Knot - 1]);
            float Mix  = clamp(log(max(LonDeg, ProfileLongitudes[0]) / ProfileLongitudes[Knot - 1]) / Span, 0.0, 1.0);
            EclipticS10 = exp(log(ProfileS10[Knot - 1]) + Mix * log(ProfileS10[Knot] / ProfileS10[Knot - 1]));
            break;
        }
    }

    float AntiSolar   = 180.0 - LonDeg;
    float Gegenschein = 45.0 * exp(-(AntiSolar * AntiSolar + LatDeg * LatDeg) / (2.0 * 7.0 * 7.0));
    float Width       = 20.0 + 0.10 * LonDeg;
    float SmoothLat   = sqrt(LatDeg * LatDeg + 16.0) - 4.0;
    float CosLat      = sqrt(max(0.0, 1.0 - LatSin * LatSin));
    float S10         = PoleS10 + (EclipticS10 - PoleS10) * exp(-SmoothLat / Width) * CosLat + Gegenschein;
    return float3(1.0, 0.93, 0.80) * S10;
}

float EvaluateStellarErfMoffatMassGpu(float DeltaPxX, float DeltaPxY, float PointScale)
{
    float PsfScale = clamp(PointScale, 0.35, 2.5);
    float SigmaPx  = 0.44 * PsfScale;
    float InvS2    = 1.0 / (1.4142135623730951 * SigmaPx);
    float CoreMass = 0.25 * (EvaluateSolarErfGpu((DeltaPxX + 0.5) * InvS2) - EvaluateSolarErfGpu((DeltaPxX - 0.5) * InvS2))
                          * (EvaluateSolarErfGpu((DeltaPxY + 0.5) * InvS2) - EvaluateSolarErfGpu((DeltaPxY - 0.5) * InvS2));

    static const float Nodes[4]   = { -0.86113631, -0.33998104, 0.33998104, 0.86113631 };
    static const float Weights[4] = {  0.34785485,  0.65214515, 0.65214515, 0.34785485 };
    float MoffatAlpha = 2.1 * PsfScale;
    float InvAlpha2   = 1.0 / (MoffatAlpha * MoffatAlpha);
    float NormFactor  = 1.5 / (CelestialPiConst * MoffatAlpha * MoffatAlpha);
    float WingMass    = 0.0;
    for (int IY = 0; IY < 4; ++IY)
    {
        float YP = DeltaPxY + 0.5 * Nodes[IY];
        for (int IX = 0; IX < 4; ++IX)
        {
            float XP = DeltaPxX + 0.5 * Nodes[IX];
            float R2 = XP * XP + YP * YP;
            float M  = NormFactor * pow(1.0 + R2 * InvAlpha2, -2.5);
            WingMass += (Weights[IX] * Weights[IY] * 0.25) * M;
        }
    }
    return 0.88 * CoreMass + 0.12 * WingMass;
}

float3 EvaluateSolarMoonBodiesGpu(
    CelestialFieldUniformBlock Cfg,
    StructuredBuffer<uint>     LunarAtlas,
    float3                     UnitRay,
    float3                     SunDir,
    bool                       Orbital,
    float3                     AtmoTrans,
    out float3                 OutStarTrans)
{
    float3 OutRgb   = float3(0.0, 0.0, 0.0);
    OutStarTrans    = float3(1.0, 1.0, 1.0);
    float  HorizonF = Orbital ? 0.0 : exp(-max(0.0, UnitRay.z) * 9.0);
    float  MieScale = max(0.0, Cfg.AtmoMediumPack.y);
    float  DayVis   = Orbital ? 0.0 : clamp((SunDir.z + 0.06) * 6.5, 0.0, 1.0);

    // 1. Neckel & Labs (1994) Limb-Darkened Photosphere + erfc PSF + < 3 px Sub-Pixel SolarErf + 3-Term Monotonic K/F Corona + Radiant/Muted Glare
    float DiscIntensity = Cfg.SunTintIntensity.w;
    if (DiscIntensity > 0.0)
    {
        float HalfR      = max(0.0008, Cfg.SunAxisRadius.w);
        float HalfChord  = 0.5 * length(UnitRay - SunDir);
        float Separation = 2.0 * asin(clamp(HalfChord, 0.0, 1.0));
        float PixelSizeR = max(0.0, Cfg.SolarOpticsParams.y);
        float MaxConeR   = max(HalfR * 48.0, PixelSizeR * 8.0);

        if (Separation <= MaxConeR)
        {
            bool RayBlocked = (!Orbital) && QueryGroundHorizonBlockedGpu(Cfg, UnitRay.z);
            float3 LineTransmittance = Orbital ? float3(1.0, 1.0, 1.0) : AtmoTrans;
            if (!RayBlocked && (LineTransmittance.x + LineTransmittance.y + LineTransmittance.z > 1.0e-6))
            {
                float NormalizedRadius = Separation / HalfR;

                float SmallWeight = 0.0;
                if (PixelSizeR > 1.0e-6)
                {
                    float RadiusPixels = HalfR / PixelSizeR;
                    float Blend        = clamp((6.0 - RadiusPixels) / 3.0, 0.0, 1.0);
                    SmallWeight        = Blend * Blend * (3.0 - 2.0 * Blend);
                }

                float3 ResolvedPhotosphere = float3(0.0, 0.0, 0.0);
                if (SmallWeight < 1.0 && NormalizedRadius < 1.32)
                {
                    float ClampedR  = min(1.0, NormalizedRadius);
                    float MuSun     = sqrt(max(0.0, 1.0 - ClampedR * ClampedR));
                    float3 LimbDark = EvaluateNeckelLabsLimbDarkeningGpu(MuSun);
                    float EdgeErfc  = 0.5 * (1.0 - EvaluateSolarErfGpu((NormalizedRadius - 0.997) / 0.012));
                    ResolvedPhotosphere = LimbDark * (EdgeErfc * DiscIntensity);
                }

                float3 SubPixelPhotosphere = float3(0.0, 0.0, 0.0);
                if (SmallWeight > 0.0 && dot(UnitRay, SunDir) > 0.0)
                {
                    float3 RefAxis       = (abs(SunDir.z) < 0.98) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
                    float3 TangentT      = normalize(cross(RefAxis, SunDir));
                    float3 TangentB      = normalize(cross(SunDir, TangentT));
                    float  DeltaX        = dot(UnitRay, TangentT);
                    float  DeltaY        = dot(UnitRay, TangentB);
                    float  SmallSigma    = sqrt(0.25 * HalfR * HalfR + 0.20 * PixelSizeR * PixelSizeR);
                    float  InvSqrt2Sigma = 1.0 / (1.4142135623730951 * max(1.0e-6, SmallSigma));
                    float  LowX          = (DeltaX - 0.5 * PixelSizeR) * InvSqrt2Sigma;
                    float  HighX         = (DeltaX + 0.5 * PixelSizeR) * InvSqrt2Sigma;
                    float  LowY          = (DeltaY - 0.5 * PixelSizeR) * InvSqrt2Sigma;
                    float  HighY         = (DeltaY + 0.5 * PixelSizeR) * InvSqrt2Sigma;
                    float  XMass         = max(0.0, 0.5 * (EvaluateSolarErfGpu(HighX) - EvaluateSolarErfGpu(LowX)));
                    float  YMass         = max(0.0, 0.5 * (EvaluateSolarErfGpu(HighY) - EvaluateSolarErfGpu(LowY)));
                    float  SinHalf       = sin(0.5 * HalfR);
                    float  SolidAngle    = 4.0 * CelestialPiConst * SinHalf * SinHalf;
                    float  PixelArea     = max(1.0e-12, PixelSizeR * PixelSizeR);
                    float  SubGain       = (SolidAngle / PixelArea) * XMass * YMass * DiscIntensity;
                    SubPixelPhotosphere  = float3(0.83276, 0.77447, 0.69061) * SubGain;
                }

                float3 Photosphere = lerp(ResolvedPhotosphere, SubPixelPhotosphere, SmallWeight);

                float3 CoronaColour    = float3(0.0, 0.0, 0.0);
                float  CoronaBlurAngle = 0.30 * PixelSizeR * SmallWeight;
                float  CoronaNormR     = sqrt(Separation * Separation + CoronaBlurAngle * CoronaBlurAngle) / HalfR;
                if (CoronaNormR < 48.0)
                {
                    float R1            = CoronaNormR / 1.18;
                    float R2            = CoronaNormR / 3.40;
                    float R3            = CoronaNormR / 11.5;
                    float InnerTerm     = 1.0 / pow(1.0 + R1 * R1, 1.85);
                    float MiddleTerm    = 1.0 / pow(1.0 + R2 * R2, 1.38);
                    float OuterTerm     = 1.0 / pow(1.0 + R3 * R3, 1.22);
                    float ScatterFactor = Orbital ? 0.38 : clamp(0.55 + 0.85 * (1.0 - LineTransmittance.z), 0.55, 1.35);
                    float CoronaProfile = DiscIntensity * (0.042 * InnerTerm + 0.0085 * ScatterFactor * MiddleTerm + 0.0014 * ScatterFactor * OuterTerm);
                    float WindowFade    = clamp(1.0 - CoronaNormR / 48.0, 0.0, 1.0);
                    float SmoothWindow  = WindowFade * WindowFade;
                    CoronaColour        = CoronaProfile * SmoothWindow * float3(1.04, 0.99, 0.91);
                }

                OutRgb += (Photosphere + CoronaColour) * LineTransmittance * Cfg.SunTintIntensity.xyz;
            }
        }

        float GlareStrength = max(0.0, Cfg.SolarOpticsParams.x);
        if (GlareStrength > 0.0)
        {
            float Spread = EvaluateSolarGlareSpreadGpu(Separation, HalfR);
            if (Spread > 0.0)
            {
                bool SunAxisBlocked = (!Orbital) && QueryGroundHorizonBlockedGpu(Cfg, SunDir.z);
                if (!SunAxisBlocked)
                {
                    float3 AxisTrans   = Orbital ? float3(1.0, 1.0, 1.0) : EvaluateCelestialAtmoTransmittance(Cfg, SunDir);
                    float  GlareFactor = GlareStrength * Spread * (0.045 * DiscIntensity);
                    OutRgb += float3(1.02, 1.00, 0.96) * GlareFactor * AxisTrans * Cfg.SunTintIntensity.xyz;
                }
            }
        }
    }

    // 2. Multi-Instance Moons (up to 6 simultaneously):
    //    eos 3D self-shadowed exosphere + a1dad11b 20-step radial quadrature + Beer-Lambert ViewTransmittance
    //    + Rayleigh / Cornette-Shanks Mie + 4x4 stratified edge Lommel-Seeliger/Oren-Nayar/Hapke regolith AA
    float3 StarTransmittance = float3(1.0, 1.0, 1.0);
    float  PixelAngSize = max(1.0e-4, max(Cfg.SolarOpticsParams.y, 0.85 / max(720.0, Cfg.ClockViewportDims.z)));
    uint   MoonCount    = min(6u, uint(Cfg.ClockViewportDims.w + 0.5));
    for (uint MIdx = 0u; MIdx < MoonCount; ++MIdx)
    {
        GpuMoonDescriptor M = Cfg.Moons[MIdx];
        if (M.MetaMask.x < 0.5 || (!Orbital && UnitRay.z < -0.06)) continue;

        float3 MoonDir = normalize(M.DirectionRadiusRad.xyz);
        float  AngR    = max(0.001, M.DirectionRadiusRad.w);
        float  SinAng  = max(1.0e-4, sin(AngR));
        float  CosM    = clamp(dot(UnitRay, MoonDir), -1.0, 1.0);

        uint PresetIdx = uint(M.MetaMask.z + 0.5);
        LunarExosphereGpuProfile Exo = QueryLunarExosphereGpuProfile(PresetIdx);
        float  ExoStr   = (M.MetaMask.y > 0.0) ? max(Exo.LimbStrength, 0.65 * M.MetaMask.y) : (Exo.LimbStrength * 0.85);
        float  ExoThick = clamp((M.MetaMask.w > 0.01) ? M.MetaMask.w : (0.18 * Exo.LimbStrength), 0.04, 0.45);
        float3 ExoTint  = Exo.LimbTint;

        if (!Orbital)
        {
            float GlowBoost = 0.45 + 0.55 * min(1.8, MieScale * (0.4 + 0.6 * HorizonF));
            float Glow      = pow(max(0.0, CosM), 640.0) * M.OpticsOrientation.y * 0.065 * M.OpticsOrientation.x * GlowBoost;
            OutRgb += StarTransmittance * lerp(M.TintSynodicPhase.xyz, ExoTint, 0.35) * Glow * AtmoTrans;
        }

        float PixelNorm   = PixelAngSize / SinAng;
        float MaxOuterAng = AngR * (1.0 + ExoThick) + PixelAngSize;
        if (CosM > cos(MaxOuterAng))
        {
            float3 UpRef    = (abs(MoonDir.z) < 0.98) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
            float3 TangentT = normalize(cross(UpRef, MoonDir));
            float3 TangentB = cross(MoonDir, TangentT);
            float  CosR     = cos(M.OpticsOrientation.z);
            float  SinR     = sin(M.OpticsOrientation.z);
            float3 T        = TangentT * CosR + TangentB * SinR;
            float3 B        = TangentB * CosR - TangentT * SinR;

            float PX    = dot(UnitRay, T) / SinAng;
            float PY    = dot(UnitRay, B) / SinAng;
            float R2    = PX * PX + PY * PY;
            float RNorm = sqrt(R2);

            float  PhAng      = (M.TintSynodicPhase.w - 0.5) * CelestialTauConst;
            float3 SunLocal   = normalize(float3(sin(PhAng), 0.18, cos(PhAng)));
            float3 ViewLocal  = float3(0.0, 0.0, 1.0);
            float  Earthshine = 0.016 * (1.0 - 0.78 * DayVis);

            float  TopR          = 1.0 + ExoThick;
            float  AtmoHalfChord = (RNorm < TopR) ? sqrt(max(0.0, TopR * TopR - R2)) : 0.0;
            bool   SolidCenter   = (R2 < 1.0);
            float  SolidEntry    = SolidCenter ? -sqrt(max(0.0, 1.0 - R2)) : 0.0;

            float3 ExoScattered      = float3(0.0, 0.0, 0.0);
            float3 ViewTransmittance = float3(1.0, 1.0, 1.0);
            if (ExoStr > 0.01 && AtmoHalfChord > 1.0e-5)
            {
                float SegStart = -AtmoHalfChord;
                float SegEnd   = SolidCenter ? SolidEntry : AtmoHalfChord;
                float SegLen   = max(0.0, SegEnd - SegStart);
                if (SegLen > 1.0e-5)
                {
                    float CosScatter = -SunLocal.z;
                    float PhaseR     = (3.0 / (16.0 * CelestialPiConst)) * (1.0 + CosScatter * CosScatter);
                    float G          = clamp(Exo.MieAsymmetry, 0.0, 0.96);
                    float G2         = G * G;
                    float Denom      = max(1.0e-3, 1.0 + G2 - 2.0 * G * CosScatter);
                    float PhaseM     = (3.0 / (8.0 * CelestialPiConst)) * ((1.0 - G2) * (1.0 + CosScatter * CosScatter)) /
                                       ((2.0 + G2) * pow(Denom, 1.5));
                    float IsoPhase   = 1.0 / (4.0 * CelestialPiConst);

                    for (uint S = 0u; S < 20u; ++S)
                    {
                        float3 Seg   = ResolveRadialChordSegmentMoonGpu(S, 20u, SegStart, SegEnd, RNorm);
                        float  Delta = Seg.y - Seg.x;
                        if (Delta <= 1.0e-6) continue;

                        float3 SamplePos = float3(PX, PY, -Seg.z);
                        float3 RSc, MSc, Sc, Ext;
                        EvaluateLunarExosphereMediumGpu(Exo, ExoTint, ExoStr, ExoThick, length(SamplePos) - 1.0, RSc, MSc, Sc, Ext);
                        if (Ext.x + Ext.y + Ext.z <= 1.0e-9) continue;

                        float3 SunT    = EvaluateLunarSunTransmittanceGpu(Exo, ExoTint, ExoStr, ExoThick, SamplePos, SunLocal, 8u);
                        float  SunMean = (SunT.x + SunT.y + SunT.z) * 0.333333;

                        float3 SegT = exp(-Ext * Delta);
                        float3 IntS = (float3(1.0) - SegT) / max(Ext, float3(1.0e-8));
                        float3 Src  = (RSc * PhaseR + MSc * PhaseM) * SunT + float3(0.30 * IsoPhase * SunMean) * Sc;

                        ExoScattered      += ViewTransmittance * Src * IntS * M.OpticsOrientation.x;
                        ViewTransmittance *= SegT;
                    }
                }
            }

            float CosP = cos(M.OpticsOrientation.w);
            float SinP = sin(M.OpticsOrientation.w);
            float3 SurfaceAvg  = float3(0.0, 0.0, 0.0);
            float DiscCoverage = 0.0;
            float EdgeBandHalf = 0.75 * PixelNorm;
            if (RNorm <= 1.0 - EdgeBandHalf)
            {
                SurfaceAvg = EvaluateLunarDiscPointGpu(
                    LunarAtlas, PresetIdx, Exo, ExoTint, ExoStr, ExoThick,
                    CosP, SinP, SunLocal, ViewLocal, M.OpticsOrientation.x, Earthshine, PX, PY);
                DiscCoverage = 1.0;
            }
            else if (RNorm <= 1.0 + EdgeBandHalf)
            {
                uint HitSamples = 0u;
                for (uint GY = 0u; GY < 4u; ++GY)
                {
                    float OY = ((float(GY) + 0.5) / 4.0 - 0.5) * PixelNorm;
                    for (uint GX = 0u; GX < 4u; ++GX)
                    {
                        float OX = ((float(GX) + 0.5) / 4.0 - 0.5) * PixelNorm;
                        float SX = PX + OX;
                        float SY = PY + OY;
                        if (SX * SX + SY * SY >= 1.0) continue;
                        SurfaceAvg += EvaluateLunarDiscPointGpu(
                            LunarAtlas, PresetIdx, Exo, ExoTint, ExoStr, ExoThick,
                            CosP, SinP, SunLocal, ViewLocal, M.OpticsOrientation.x, Earthshine, SX, SY) * (1.0 / 16.0);
                        HitSamples += 1u;
                    }
                }
                DiscCoverage = float(HitSamples) * (1.0 / 16.0);
            }

            float  OffLimbFrac = 1.0 - DiscCoverage;
            float3 SlotCol     = ExoScattered + ViewTransmittance * SurfaceAvg;
            OutRgb            += StarTransmittance * SlotCol * AtmoTrans;
            StarTransmittance *= OffLimbFrac * ViewTransmittance;
        }
    }

    OutStarTrans = StarTransmittance;
    return OutRgb;
}

float3 EvaluateGalacticNebulaBackdropGpu(
    CelestialFieldUniformBlock Cfg,
    float3                     EqDir,
    float                      DayFade,
    float                      AltFade,
    float3                     AtmoTrans,
    float3                     StarTransmittance)
{
    uint Flags = uint(Cfg.AtmoMediumPack.w + 0.5);
    if ((Flags & 4u) == 0u)
    {
        return float3(0.0, 0.0, 0.0);
    }
    float3 Bulge  = EvaluateGalacticBulgeRadianceGpu(EqDir);
    float3 Nebula = EvaluateDeepSkyNebulaeGpu(EqDir);
    float  Gain   = Cfg.StarParams.z * DayFade * AltFade;
    float3 SkyCol = (Bulge * 0.0135 + Nebula * 1.45) * Gain;
    return StarTransmittance * SkyCol * AtmoTrans;
}

float3 EvaluateZodiacalBackdropGpu(
    CelestialFieldUniformBlock Cfg,
    float3                     EqDir,
    float3                     SunDir,
    float                      DayFade,
    float                      AltFade,
    float3                     AtmoTrans,
    float3                     StarTransmittance)
{
    uint Flags = uint(Cfg.AtmoMediumPack.w + 0.5);
    if ((Flags & 8u) == 0u)
    {
        return float3(0.0, 0.0, 0.0);
    }
    float  Scale    = float(Flags >> 4u) * (1.0 / 255.0);
    if (Scale <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }
    float3 SolarEq  = TransformHorizonToEquatorialGpu(SunDir, Cfg.StarScintillationRot.w);
    float3 Zodiacal = EvaluateZodiacalLightGpu(EqDir, SolarEq);
    float  ZGain    = (0.0135 / 533.1) * Scale * Cfg.StarParams.z * DayFade * AltFade;
    return StarTransmittance * (Zodiacal * ZGain) * AtmoTrans;
}

float3 EvaluateDeepCubemapStarsGpu(
    CelestialFieldUniformBlock Cfg,
    float3                     EqDir,
    float                      DayFade,
    float                      AltFade,
    float                      ScintAmp,
    float                      ScintFreq,
    float                      PixelAng,
    float3                     AtmoTrans,
    float3                     StarTransmittance)
{
    float3 OutRgb = float3(0.0, 0.0, 0.0);
    float  AX     = abs(EqDir.x);
    float  AY     = abs(EqDir.y);
    float  AZ     = abs(EqDir.z);
    int    Face   = 0;
    float  FU     = 0.0;
    float  FV     = 0.0;
    if (AX >= AY && AX >= AZ)
    {
        if (EqDir.x >= 0.0) { Face = 0; FU = -EqDir.z / AX; FV = EqDir.y / AX; }
        else                { Face = 1; FU =  EqDir.z / AX; FV = EqDir.y / AX; }
    }
    else if (AY >= AX && AY >= AZ)
    {
        if (EqDir.y >= 0.0) { Face = 2; FU = EqDir.x / AY; FV = -EqDir.z / AY; }
        else                { Face = 3; FU = EqDir.x / AY; FV =  EqDir.z / AY; }
    }
    else
    {
        if (EqDir.z >= 0.0) { Face = 4; FU = EqDir.x / AZ; FV =  EqDir.y / AZ; }
        else                { Face = 5; FU = EqDir.x / AZ; FV = -EqDir.y / AZ; }
    }

    float3 RefEq = (abs(EqDir.z) < 0.90) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 TangU = normalize(cross(RefEq, EqDir));
    float3 TangV = cross(EqDir, TangU);

    const float GridN = 96.0;
    float2 GridUv = float2(FU * 0.5 + 0.5, FV * 0.5 + 0.5) * GridN;
    int    CX     = clamp(int(floor(GridUv.x)), 0, 95);
    int    CY     = clamp(int(floor(GridUv.y)), 0, 95);

    float MagLim = Cfg.StarParams.y + 0.5;
    uint  Flags  = uint(Cfg.AtmoMediumPack.w + 0.5);
    bool  Gal    = (Flags & 4u) != 0u;
    float GalSin = TransformEquatorialToGalacticGpu(EqDir).z;
    float GalW   = Gal ? (0.40 + 1.40 * exp(-(GalSin * GalSin) / 0.065)) : 0.70;
    float StarP  = clamp(0.28 * GalW, 0.05, 0.72);

    for (int DY = -1; DY <= 1; ++DY)
    {
        int NY = CY + DY;
        if (NY < 0 || NY >= 96) continue;
        for (int DX = -1; DX <= 1; ++DX)
        {
            int NX = CX + DX;
            if (NX < 0 || NX >= 96) continue;

            float H1 = HashCelestial3(Face * 131 + NX, NY, 17);
            if (H1 <= 1.0 - StarP) continue;

            float H2 = HashCelestial3(Face * 131 + NX + 19, NY - 7, 31);
            float H3 = HashCelestial3(Face * 131 + NX - 13, NY + 29, 53);
            float H4 = HashCelestial3(Face * 131 + NX + 41, NY + 17, 79);
            float SU = ((float(NX) + 0.12 + 0.76 * H2) / GridN) * 2.0 - 1.0;
            float SV = ((float(NY) + 0.12 + 0.76 * H3) / GridN) * 2.0 - 1.0;

            float3 StarDir;
            if      (Face == 0) StarDir = normalize(float3( 1.0,  SV, -SU));
            else if (Face == 1) StarDir = normalize(float3(-1.0,  SV,  SU));
            else if (Face == 2) StarDir = normalize(float3( SU,  1.0, -SV));
            else if (Face == 3) StarDir = normalize(float3( SU, -1.0,  SV));
            else if (Face == 4) StarDir = normalize(float3( SU,  SV,  1.0));
            else                StarDir = normalize(float3( SU, -SV, -1.0));

            if (dot(EqDir, StarDir) < 0.9985) continue;
            float MagRank = saturate((H1 - (1.0 - StarP)) / max(1.0e-4, StarP));
            float MagV    = 5.05 + 3.45 * pow(MagRank, 0.42);
            if (MagV > MagLim) continue;

            float HalfChord = 0.5 * length(EqDir - StarDir);
            float SepRad    = 2.0 * asin(clamp(HalfChord, 0.0, 1.0));
            if (SepRad > 9.0 * PixelAng) continue;

            float DXPx = dot(StarDir, TangU) / PixelAng;
            float DYPx = dot(StarDir, TangV) / PixelAng;
            float Mass = EvaluateStellarErfMoffatMassGpu(DXPx, DYPx, Cfg.StarParams.w);

            float Twinkle = 1.0;
            float Chroma  = 0.0;
            if (ScintAmp > 0.0)
            {
                float Phase   = H2 * CelestialTauConst;
                float Flicker = 0.62 * sin(Cfg.ClockViewportDims.x * ScintFreq * 11.4 + Phase * 3.0)
                              + 0.38 * sin(Cfg.ClockViewportDims.x * ScintFreq * 24.4 + Phase * 7.0);
                Twinkle = max(0.0, 1.0 + ScintAmp * Flicker);
                Chroma  = 0.35 * ScintAmp * sin(Cfg.ClockViewportDims.x * ScintFreq * 17.6 + Phase * 5.0);
            }

            float  Flux  = 14.0 * pow(10.0, -0.4 * MagV);
            float  BvVal = (H4 < 0.36) ? (-0.28 + (H4 / 0.36) * 0.42)
                         : (H4 < 0.74) ? ( 0.14 + ((H4 - 0.36) / 0.38) * 0.52)
                                       : ( 0.85 + ((H4 - 0.74) / 0.26) * 0.95);
            float3 BbCol = EvaluateStarBlackbodyGpu(BvVal);
            float  Lum   = Flux * Cfg.StarParams.z * DayFade * Twinkle * AltFade * Mass;
            OutRgb += StarTransmittance * (Lum * float3(BbCol.x * (1.0 + Chroma), BbCol.y, BbCol.z * (1.0 - Chroma))) * AtmoTrans;
        }
    }
    return OutRgb;
}

float3 EvaluateTiledCatalogStarsGpu(
    CelestialFieldUniformBlock       Cfg,
    StructuredBuffer<GpuProjectedStar> ProjectedStars,
    StructuredBuffer<uint>           TileBins,
    float2                           PixelCoord,
    float                            DayFade,
    float                            ScintAmp,
    float                            ScintFreq,
    float3                           AtmoTrans,
    float3                           StarTransmittance)
{
    uint W    = max(1u, uint(Cfg.ClockViewportDims.y + 0.5));
    uint H    = max(1u, uint(Cfg.ClockViewportDims.z + 0.5));
    uint Cols = (W + 15u) / 16u;
    uint Rows = (H + 15u) / 16u;
    uint TX   = min(Cols - 1u, uint(max(0.0, PixelCoord.x)) / 16u);
    uint TY   = min(Rows - 1u, uint(max(0.0, PixelCoord.y)) / 16u);
    uint Base = (TY * Cols + TX) * StellarTileStrideGpu;
    uint Cnt  = min(64u, TileBins[Base]);

    float3 OutRgb = float3(0.0, 0.0, 0.0);
    for (uint I = 0u; I < Cnt; ++I)
    {
        uint             StarIdx = TileBins[Base + 1u + I];
        GpuProjectedStar S       = ProjectedStars[StarIdx];
        if (S.PixPosFlux.z <= 0.0) continue;

        float DX = PixelCoord.x - S.PixPosFlux.x;
        float DY = PixelCoord.y - S.PixPosFlux.y;
        if (DX * DX + DY * DY > 144.0) continue;

        float Mass = EvaluateStellarErfMoffatMassGpu(DX, DY, Cfg.StarParams.w);
        if (Mass < 1.0e-6) continue;

        float Twinkle = 1.0;
        float Chroma  = 0.0;
        if (ScintAmp > 0.0)
        {
            float Phase   = S.PixPosFlux.w * CelestialTauConst;
            float Flicker = 0.62 * sin(Cfg.ClockViewportDims.x * ScintFreq * 11.4 + Phase * 3.0)
                          + 0.38 * sin(Cfg.ClockViewportDims.x * ScintFreq * 24.4 + Phase * 7.0);
            Twinkle = max(0.0, 1.0 + ScintAmp * Flicker);
            Chroma  = 0.35 * ScintAmp * sin(Cfg.ClockViewportDims.x * ScintFreq * 17.6 + Phase * 5.0);
        }

        float  AltFade = clamp((S.ChromaticityElev.w + 0.01) * 4.5, 0.0, 1.0);
        float  Lum     = S.PixPosFlux.z * Cfg.StarParams.z * DayFade * Twinkle * AltFade * Mass;
        float3 Tint    = S.ChromaticityElev.xyz * float3(1.0 + Chroma, 1.0, 1.0 - Chroma);
        OutRgb += StarTransmittance * (Lum * Tint) * AtmoTrans;
    }
    return OutRgb;
}

float3 EvaluateCelestialBodiesUnifiedGpu(
    CelestialFieldUniformBlock         Cfg,
    StructuredBuffer<uint>             LunarAtlas,
    StructuredBuffer<GpuProjectedStar> ProjectedStars,
    StructuredBuffer<uint>             TileBins,
    float2                             PixelCoord,
    float3                             RayDir,
    bool                               UseTiled)
{
    float3 UnitRay   = normalize(RayDir);
    float3 SunDir    = normalize(Cfg.SunAxisRadius.xyz);
    uint   Flags     = uint(Cfg.AtmoMediumPack.w + 0.5);
    bool   Orbital   = (Flags & 2u) != 0u;
    float3 AtmoTrans = EvaluateCelestialAtmoTransmittance(Cfg, UnitRay);

    float3 StarTransmittance = float3(1.0, 1.0, 1.0);
    float3 OutRgb = EvaluateSolarMoonBodiesGpu(Cfg, LunarAtlas, UnitRay, SunDir, Orbital, AtmoTrans, StarTransmittance);

    if (Cfg.StarParams.x > 0.5 && (Orbital || UnitRay.z > -0.02) && (StarTransmittance.x + StarTransmittance.y + StarTransmittance.z) > 1.0e-5)
    {
        float DayFade = Orbital ? 1.0 : clamp(1.0 - (SunDir.z + 0.08) * 5.0, 0.0, 1.0);
        if (DayFade > 0.01)
        {
            float3 EqDir    = TransformHorizonToEquatorialGpu(UnitRay, Cfg.StarScintillationRot.w);
            float  AltFade  = Orbital ? 1.0 : clamp((UnitRay.z + 0.01) * 4.5, 0.0, 1.0);
            float  PixelAng = max(0.00025, max(Cfg.SolarOpticsParams.y, 0.85 / max(720.0, Cfg.ClockViewportDims.z)));

            float ScintAmp = 0.0;
            if (Cfg.StarScintillationRot.x > 0.5 && !Orbital && Cfg.StarScintillationRot.y > 0.0)
            {
                float Airmass = EvaluateKastenYoungAirmassGpu(UnitRay.z);
                ScintAmp = clamp((Cfg.StarScintillationRot.y * 0.01) * 0.12 * pow(min(Airmass, 38.0), 1.5), 0.0, 0.85);
            }
            float ScintFreq = max(0.1, Cfg.StarScintillationRot.z);

            OutRgb += EvaluateGalacticNebulaBackdropGpu(Cfg, EqDir, DayFade, AltFade, AtmoTrans, StarTransmittance);
            OutRgb += EvaluateZodiacalBackdropGpu(Cfg, EqDir, SunDir, DayFade, AltFade, AtmoTrans, StarTransmittance);
            if (UseTiled)
            {
                OutRgb += EvaluateTiledCatalogStarsGpu(Cfg, ProjectedStars, TileBins, PixelCoord, DayFade, ScintAmp, ScintFreq, AtmoTrans, StarTransmittance);
            }
            OutRgb += EvaluateDeepCubemapStarsGpu(Cfg, EqDir, DayFade, AltFade, ScintAmp, ScintFreq, PixelAng, AtmoTrans, StarTransmittance);
        }
    }

    return OutRgb;
}

#ifndef FRONTIER_ATMOSPHERE_EMBEDDED
float3 EvaluateCelestialBodiesGpu(
    CelestialFieldUniformBlock Cfg,
    StructuredBuffer<uint>     LunarAtlas,
    float3                     RayDir)
{
    float3 UnitRay   = normalize(RayDir);
    float3 SunDir    = normalize(Cfg.SunAxisRadius.xyz);
    uint   Flags     = uint(Cfg.AtmoMediumPack.w + 0.5);
    bool   Orbital   = (Flags & 2u) != 0u;
    float3 AtmoTrans = EvaluateCelestialAtmoTransmittance(Cfg, UnitRay);

    float3 StarTransmittance = float3(1.0, 1.0, 1.0);
    float3 OutRgb = EvaluateSolarMoonBodiesGpu(Cfg, LunarAtlas, UnitRay, SunDir, Orbital, AtmoTrans, StarTransmittance);

    if (Cfg.StarParams.x > 0.5 && (Orbital || UnitRay.z > -0.02) && (StarTransmittance.x + StarTransmittance.y + StarTransmittance.z) > 1.0e-5)
    {
        float DayFade = Orbital ? 1.0 : clamp(1.0 - (SunDir.z + 0.08) * 5.0, 0.0, 1.0);
        if (DayFade > 0.01)
        {
            float3 EqDir    = TransformHorizonToEquatorialGpu(UnitRay, Cfg.StarScintillationRot.w);
            float  AltFade  = Orbital ? 1.0 : clamp((UnitRay.z + 0.01) * 4.5, 0.0, 1.0);
            float  PixelAng = max(0.00025, max(Cfg.SolarOpticsParams.y, 0.85 / max(720.0, Cfg.ClockViewportDims.z)));

            float ScintAmp = 0.0;
            if (Cfg.StarScintillationRot.x > 0.5 && !Orbital && Cfg.StarScintillationRot.y > 0.0)
            {
                float Airmass = EvaluateKastenYoungAirmassGpu(UnitRay.z);
                ScintAmp = clamp((Cfg.StarScintillationRot.y * 0.01) * 0.12 * pow(min(Airmass, 38.0), 1.5), 0.0, 0.85);
            }
            float ScintFreq = max(0.1, Cfg.StarScintillationRot.z);

            OutRgb += EvaluateGalacticNebulaBackdropGpu(Cfg, EqDir, DayFade, AltFade, AtmoTrans, StarTransmittance);
            OutRgb += EvaluateZodiacalBackdropGpu(Cfg, EqDir, SunDir, DayFade, AltFade, AtmoTrans, StarTransmittance);
            OutRgb += EvaluateDeepCubemapStarsGpu(Cfg, EqDir, DayFade, AltFade, ScintAmp, ScintFreq, PixelAng, AtmoTrans, StarTransmittance);
        }
    }

    return OutRgb;
}

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
