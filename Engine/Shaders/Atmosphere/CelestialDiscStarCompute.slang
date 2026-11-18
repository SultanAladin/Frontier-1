/*============================================================================================================================================
                                                      CELESTIALDISCSTARCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Smooth Eddington Sun + Corona, 6x Multi-Moon Real 2K Atlas Discs w/ Off-Limb Volumetric Atmosphere & Pinpoint Optical Starfield

static const float CelestialPiConst  = 3.14159265359;
static const float CelestialTauConst = 6.28318530718;
static const uint  MoonAtlasTileW    = 512u;
static const uint  MoonAtlasTileH    = 256u;
static const uint  MoonAtlasPresets  = 6u;
static const uint  MaxGpuMoons       = 6u;

struct GpuMoonDescriptor
{
    float4 DirectionRadiusRad; // xyz = UnitMoonDir, w = HalfAngularRadiusRad
    float4 TintSynodicPhase;   // xyz = AtmosphereTint (or PresetTint), w = SynodicPhase (0..1)
    float4 OpticsOrientation;  // x = Bright, y = HaloGlowMultiplier, z = RollRad, w = PitchRad
    float4 MetaMask;           // x = Visible (0/1), y = AtmoIntensity, z = PresetIndex (0..5), w = AtmoThicknessScale (>0 if HasAtmo)
};

struct CelestialFieldUniformBlock
{
    float4 SunAxisRadius;        // xyz = UnitSunDir, w = SunHalfAngleRad
    float4 SunTintIntensity;     // xyz = EffectiveSunTint * Transmittance, w = SunEnabledScale
    float4 StarParams;           // x = StarsEnabled(0/1), y = LimMag, z = Bright, w = PointSize
    float4 StarScintillationRot; // x = TwinkleEnabled(0/1), y = DepthPct, z = RateHz, w = RotRad
    float4 ClockViewportDims;    // x = SimulationSeconds, y = ViewportW, z = ViewportH, w = MoonCount
    float4 AtmoMediumPack;       // x = RayScale, y = MieScale, z = OzScale, w = FlagBits (1=Atmo, 2=Orbital, 4=GalBand, 8=Catalog)
    GpuMoonDescriptor Moons[6];
};

float HashCelestial3(int IX, int IY, int IZ)
{
    uint N = uint(IX) * 374761393u + uint(IY) * 668265263u + uint(IZ) * 1442695041u;
    N = (N ^ (N >> 13u)) * 1274126177u;
    N = N ^ (N >> 16u);
    return float(N) * (1.0 / 4294967296.0);
}

float SmoothNoiseCel3(float3 P)
{
    int3 I = int3(floor(P));
    float3 F = P - float3(I);
    float3 U = F * F * (3.0 - 2.0 * F);

    float C000 = HashCelestial3(I.x,     I.y,     I.z);
    float C100 = HashCelestial3(I.x + 1, I.y,     I.z);
    float C010 = HashCelestial3(I.x,     I.y + 1, I.z);
    float C110 = HashCelestial3(I.x + 1, I.y + 1, I.z);
    float C001 = HashCelestial3(I.x,     I.y,     I.z + 1);
    float C101 = HashCelestial3(I.x + 1, I.y,     I.z + 1);
    float C011 = HashCelestial3(I.x,     I.y + 1, I.z + 1);
    float C111 = HashCelestial3(I.x + 1, I.y + 1, I.z + 1);

    float X00 = lerp(C000, C100, U.x);
    float X10 = lerp(C010, C110, U.x);
    float X01 = lerp(C001, C101, U.x);
    float X11 = lerp(C011, C111, U.x);
    return lerp(lerp(X00, X10, U.y), lerp(X01, X11, U.y), U.z);
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

float3 EvaluateCelestialAtmoTransmittance(CelestialFieldUniformBlock Cfg, float3 RayDir)
{
    uint  FlagBits    = uint(Cfg.AtmoMediumPack.w + 0.5);
    bool  AtmoEnabled = (FlagBits & 1u) != 0u;
    bool  OrbitalView = (FlagBits & 2u) != 0u;
    if (OrbitalView || !AtmoEnabled || RayDir.z < -0.05)
    {
        return float3(1.0, 1.0, 1.0);
    }

    float RayScale = Cfg.AtmoMediumPack.x;
    float MieScale = Cfg.AtmoMediumPack.y;
    float OzScale  = Cfg.AtmoMediumPack.z;

    float CosZ    = clamp(RayDir.z, 0.012, 1.0);
    float ElevDeg = asin(CosZ) * (180.0 / CelestialPiConst);
    float AirMass = 1.0 / (CosZ + 0.50572 * pow(max(0.5, ElevDeg + 6.07995), -1.6364));
    AirMass       = min(AirMass, 38.0);

    float3 RayTau = float3(0.005802, 0.013558, 0.033100) * (RayScale * 7.8 * AirMass);
    float3 MieTau = float3(0.004440, 0.004440, 0.004440) * (MieScale * 1.45 * AirMass);
    float3 OzTau  = float3(0.001050, 0.001920, 0.000090) * (OzScale  * 6.2  * min(AirMass, 18.0));
    return exp(-(RayTau + MieTau + OzTau));
}

void QueryLunarExosphereGpu(
    GpuMoonDescriptor M,
    uint              PresetIdx,
    out float3        OutLimbTint,
    out float         OutLimbStrength,
    out float         OutTermWrap,
    out float         OutShellThickness)
{
    if (PresetIdx == 1u)      { OutLimbTint = float3(1.00, 0.44, 0.16); OutLimbStrength = 0.55; OutTermWrap = 0.20; OutShellThickness = 0.18; }
    else if (PresetIdx == 2u) { OutLimbTint = float3(0.42, 0.76, 1.00); OutLimbStrength = 0.68; OutTermWrap = 0.22; OutShellThickness = 0.20; }
    else if (PresetIdx == 3u) { OutLimbTint = float3(1.00, 0.84, 0.32); OutLimbStrength = 0.58; OutTermWrap = 0.18; OutShellThickness = 0.17; }
    else if (PresetIdx == 4u) { OutLimbTint = float3(0.54, 0.74, 1.00); OutLimbStrength = 0.96; OutTermWrap = 0.28; OutShellThickness = 0.24; }
    else if (PresetIdx == 5u) { OutLimbTint = float3(0.44, 0.94, 0.86); OutLimbStrength = 0.54; OutTermWrap = 0.18; OutShellThickness = 0.16; }
    else                      { OutLimbTint = float3(0.56, 0.78, 1.00); OutLimbStrength = 0.00; OutTermWrap = 0.04; OutShellThickness = 0.00; }

    if (M.MetaMask.w > 0.001)
    {
        float Inten = clamp(M.MetaMask.y, 0.0, 3.0);
        float Thick = clamp(M.MetaMask.w, 0.04, 0.65);
        OutLimbTint       = M.TintSynodicPhase.xyz;
        OutLimbStrength   = max(OutLimbStrength, 0.95) * Inten;
        OutShellThickness = max(OutShellThickness, Thick);
        OutTermWrap       = max(OutTermWrap, 0.26);
    }
    else
    {
        OutLimbStrength *= clamp(M.OpticsOrientation.y, 0.0, 2.0);
    }
}

float3 EvaluateStarColorFromBvGpu(float BvIndex)
{
    float BV = clamp(BvIndex, -0.40, 2.00);
    float3 Col;
    if (BV < 0.0)
    {
        float T = (BV + 0.40) / 0.40;
        Col = float3(0.61 + 0.22 * T, 0.73 + 0.19 * T, 1.00);
    }
    else if (BV < 0.40)
    {
        float T = BV / 0.40;
        Col = float3(0.83 + 0.17 * T, 0.92 + 0.07 * T, 1.00 - 0.05 * T);
    }
    else if (BV < 1.00)
    {
        float T = (BV - 0.40) / 0.60;
        Col = float3(1.00, 0.99 - 0.17 * T, 0.95 - 0.42 * T);
    }
    else
    {
        float T = (BV - 1.00) / 1.00;
        Col = float3(1.00, 0.82 - 0.34 * T, 0.53 - 0.38 * T);
    }
    return pow(clamp(Col, float3(0.0), float3(1.0)), float3(1.8));
}

float3 EvaluateOpticalStarContributionGpu(
    float3 RotDir,
    float3 StarDir,
    float  Magnitude,
    float  BvIndex,
    float  TwinkleSeed,
    float  LimMag,
    float  StarBright,
    float  SigmaCore,
    float  EnergyNorm,
    float  DayFade,
    float  AltFade,
    bool   OrbitalView,
    float  SimSeconds,
    bool   TwinkleEnabled,
    float  TwinkleDepthPct,
    float  TwinkleRateHz,
    float3 AtmoTrans)
{
    if (Magnitude > LimMag) return float3(0.0);
    float Chord = length(RotDir - StarDir);
    if (Chord > SigmaCore * 14.0) return float3(0.0);

    float NormR    = Chord / SigmaCore;
    float AiryCore = exp(-2.6 * NormR * NormR);
    float AiryHalo = 0.095 * exp(-1.05 * NormR);

    float Spike = 0.0;
    if (Magnitude < 1.8 && Chord < SigmaCore * 13.0)
    {
        float3 UpS   = (abs(StarDir.z) < 0.95) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
        float3 StU   = normalize(cross(UpS, StarDir));
        float3 StV   = cross(StarDir, StU);
        float3 Delta = RotDir - StarDir;
        float  DU    = dot(Delta, StU);
        float  DV    = dot(Delta, StV);
        float  RotU  = DU * 0.7071 - DV * 0.7071;
        float  RotV  = DU * 0.7071 + DV * 0.7071;
        float  BW    = SigmaCore * 0.42;
        float  BL    = SigmaCore * (4.5 + 2.5 * max(0.0, 1.8 - Magnitude));
        float  S1    = exp(-(RotU * RotU) / (BW * BW)) * exp(-abs(RotV) / BL);
        float  S2    = exp(-(RotV * RotV) / (BW * BW)) * exp(-abs(RotU) / BL);
        Spike = (S1 + S2) * 0.22 * clamp((1.8 - Magnitude) / 2.5, 0.0, 1.0);
    }

    float Psf = AiryCore + AiryHalo + Spike;
    if (Psf < 1.0e-4) return float3(0.0);

    float Flux = pow(10.0, -0.4 * (Magnitude - 1.2));
    float Twinkle = (!OrbitalView && TwinkleEnabled)
        ? (1.0 - (TwinkleDepthPct * 0.0075) * (0.5 + 0.5 * sin(SimSeconds * TwinkleRateHz * 5.0 + TwinkleSeed * 40.0)))
        : 1.0;
    float3 Col = EvaluateStarColorFromBvGpu(BvIndex);
    float  Int = min(12.0, Flux * 1.35) * StarBright * DayFade * Twinkle * Psf * AltFade * EnergyNorm;
    return Col * Int * AtmoTrans;
}

static const uint J2000CatalogCount = 24u;
static const float4 J2000StarDirsAndMags[24] = {
    float4(-0.1879,  0.9390, -0.2876, -1.46), // Sirius
    float4(-0.0630,  0.6027, -0.7954, -0.74), // Canopus
    float4(-0.3746, -0.3112, -0.8732, -0.27), // Alpha Centauri
    float4(-0.7834, -0.5257,  0.3286, -0.05), // Arcturus
    float4( 0.1257, -0.7692,  0.6264,  0.03), // Vega
    float4( 0.1303,  0.6824,  0.7193,  0.08), // Capella
    float4( 0.1950,  0.9704, -0.1426,  0.13), // Rigel
    float4(-0.4150,  0.9052,  0.0911,  0.34), // Procyon
    float4( 0.4930,  0.2248, -0.8409,  0.46), // Achernar
    float4( 0.0209,  0.9914,  0.1289,  0.50), // Betelgeuse
    float4(-0.4240, -0.2546, -0.8693,  0.61), // Hadar
    float4( 0.4590, -0.8750,  0.1542,  0.76), // Altair
    float4( 0.3407,  0.8965,  0.2842,  0.86), // Aldebaran
    float4(-0.9122, -0.3612, -0.1936,  0.97), // Spica
    float4(-0.3474, -0.8253, -0.4451,  1.06), // Antares
    float4(-0.3910,  0.7916,  0.4699,  1.14), // Pollux
    float4( 0.8371, -0.2339, -0.4943,  1.16), // Fomalhaut
    float4( 0.4547, -0.5400,  0.7106,  1.25), // Deneb
    float4(-0.8648,  0.4573,  0.2073,  1.35), // Regulus
    float4(-0.2245,  0.8456, -0.4844,  1.50), // Adhara
    float4(-0.3372,  0.7791,  0.5283,  1.58), // Castor
    float4(-0.0920, -0.7922, -0.6033,  1.62), // Shaula
    float4( 0.1506,  0.9825,  0.1106,  1.64), // Bellatrix
    float4( 0.0080,  0.0128,  0.9999,  1.98), // Polaris
};
static const float J2000StarBvs[24] = {
     0.00,  0.15,  0.71,  1.23,  0.00,  0.80, -0.03,  0.42,
    -0.16,  1.85, -0.23,  0.22,  1.54, -0.23,  1.83,  1.00,
     0.09,  0.09, -0.11, -0.21,  0.03, -0.22, -0.22,  0.60
};

float3 EvaluateCelestialBodiesGpu(
    CelestialFieldUniformBlock Cfg,
    StructuredBuffer<uint>     LunarAtlas,
    float3                     RayDir)
{
    uint  FlagBits        = uint(Cfg.AtmoMediumPack.w + 0.5);
    bool  OrbitalView     = (FlagBits & 2u) != 0u;
    bool  EnableGalBand   = (FlagBits & 4u) != 0u;
    bool  EnableCatalog   = (FlagBits & 8u) != 0u;

    float3 OutRgb    = float3(0.0, 0.0, 0.0);
    float3 SunDir    = normalize(Cfg.SunAxisRadius.xyz);
    float  DayVis    = OrbitalView ? 0.0 : clamp((SunDir.z + 0.06) * 6.5, 0.0, 1.0);

    // 1. Smooth Eddington Solar Disc + Continuous Corona / Mie Aureole
    if (Cfg.SunTintIntensity.w > 0.0 && (OrbitalView || RayDir.z >= -0.04))
    {
        float CosS     = clamp(dot(RayDir, SunDir), -1.0, 1.0);
        float HalfR    = max(0.0008, Cfg.SunAxisRadius.w);
        if (CosS > cos(min(CelestialPiConst * 0.48, HalfR * 46.0)))
        {
            float Ang      = acos(CosS);
            float RNorm    = Ang / HalfR;
            float DiscMask = 1.0 - smoothstep(0.92, 1.14, RNorm);
            float MuSun    = sqrt(max(0.0, 1.0 - min(1.0, RNorm * RNorm)));
            float LimbDark = 0.42 + 0.58 * pow(MuSun, 0.45);
            float EnvFade  = 1.0 - smoothstep(HalfR * 18.0, HalfR * 45.0, Ang);
            float CorScale = OrbitalView ? 0.55 : 1.0;
            float AurScale = OrbitalView ? 0.08 : 1.0;
            float InnerCor = exp(-max(0.0, Ang - HalfR * 0.70) / max(0.002, HalfR * (OrbitalView ? 1.25 : 2.4))) * (0.80 * CorScale);
            float MidAur   = exp(-Ang / max(0.010, HalfR * 9.0)) * 0.20 * EnvFade * AurScale;
            float OutGlare = exp(-Ang / max(0.045, HalfR * 24.0)) * 0.025 * EnvFade * AurScale;
            float LimbFade = OrbitalView ? 1.0 : clamp((RayDir.z + 0.02) * 18.0, 0.0, 1.0);
            float DiscRad  = DiscMask * LimbDark * 28.0 + InnerCor + MidAur + OutGlare;
            OutRgb += Cfg.SunTintIntensity.xyz * DiscRad * (Cfg.SunTintIntensity.w * LimbFade);
        }
    }

    uint MoonCount = min(MaxGpuMoons, uint(Cfg.ClockViewportDims.w + 0.5));
    bool AnyMoonVisible = false;
    for (uint MCheck = 0u; MCheck < MoonCount; ++MCheck)
    {
        if (Cfg.Moons[MCheck].MetaMask.x >= 0.5)
        {
            AnyMoonVisible = true;
            break;
        }
    }
    if (!AnyMoonVisible && Cfg.StarParams.x <= 0.5)
    {
        return OutRgb;
    }
    float3 AtmoTrans = EvaluateCelestialAtmoTransmittance(Cfg, RayDir);

    // 2. Multi-Instance Moons (up to 6) sampling real 2K lunar equirectangular atlas + off-limb volumetric atmosphere shell
    for (uint MIdx = 0u; MIdx < MoonCount; ++MIdx)
    {
        GpuMoonDescriptor M = Cfg.Moons[MIdx];
        if (M.MetaMask.x < 0.5 || (!OrbitalView && RayDir.z < -0.05)) continue;

        float3 MoonDir = normalize(M.DirectionRadiusRad.xyz);
        float  AngR    = max(0.001, M.DirectionRadiusRad.w);
        float  CosM    = clamp(dot(RayDir, MoonDir), -1.0, 1.0);

        uint   PresetIdx = uint(M.MetaMask.z + 0.5);
        float3 ExoTint;
        float  ExoStr, ExoWrap, ExoThick;
        QueryLunarExosphereGpu(M, PresetIdx, ExoTint, ExoStr, ExoWrap, ExoThick);
        float MaxShellMul  = 1.0 + max(0.0, ExoThick);
        float CosShellEdge = cos(min(CelestialPiConst * 0.48, AngR * MaxShellMul * 1.05));
        if (CosM <= CosShellEdge) continue;

        float3 UpRef = (abs(MoonDir.z) < 0.98) ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
        float3 TangentT = normalize(cross(UpRef, MoonDir));
        float3 TangentB = cross(MoonDir, TangentT);
        float  CosR  = cos(M.OpticsOrientation.z);
        float  SinR  = sin(M.OpticsOrientation.z);
        float3 T     = TangentT * CosR + TangentB * SinR;
        float3 B     = TangentB * CosR - TangentT * SinR;

        float SinAng = max(1.0e-4, sin(AngR));
        float PX     = dot(RayDir, T) / SinAng;
        float PY     = dot(RayDir, B) / SinAng;
        float R2     = PX * PX + PY * PY;
        float RNorm  = sqrt(R2);

        float  PhAng = (M.TintSynodicPhase.w - 0.5) * CelestialTauConst;
        float3 PhL   = normalize(float3(sin(PhAng), 0.18, cos(PhAng)));

        if (R2 < 1.0)
        {
            float PZ    = sqrt(max(0.0, 1.0 - R2));
            float CosP  = cos(M.OpticsOrientation.w);
            float SinP  = sin(M.OpticsOrientation.w);
            float SurfY = PY * CosP - PZ * SinP;
            float SurfZ = PY * SinP + PZ * CosP;

            float LonU = atan2(PX, SurfZ) / CelestialTauConst + 0.5;
            float LatV = acos(clamp(SurfY, -1.0, 1.0)) / CelestialPiConst;
            float3 TexAlbedo = SampleLunarAtlasGpu(LunarAtlas, PresetIdx, LonU, LatV);

            float RawDot = dot(float3(PX, PY, PZ), PhL);
            float NdotL  = max(0.0, RawDot);

            float EdgeAa     = smoothstep(1.0, 0.965, RNorm);
            float Earthshine = 0.014 * (1.0 - 0.85 * DayVis);
            float Lommel     = NdotL / max(0.08, NdotL + 0.45 * PZ + 0.08);
            float OppSurge   = 1.0 + 0.25 * exp(-acos(clamp(PhL.z, -1.0, 1.0)) * 5.0);
            float Lum        = (Earthshine + Lommel * 1.48 * OppSurge) * M.OpticsOrientation.x * EdgeAa;

            float TermWrap   = smoothstep(-ExoWrap - 0.05, 0.55, RawDot);
            float OnDiscAtmo = (ExoStr > 0.001)
                ? (pow(1.0 - PZ, 2.2) * ExoStr * TermWrap * M.OpticsOrientation.x * EdgeAa * 0.65)
                : 0.0;

            float  LimbTau = ExoStr * pow(1.0 - PZ, 1.6) * 0.42;
            float3 DiscT   = exp(-LimbTau * (float3(1.15) - 0.5 * ExoTint));
            OutRgb += (TexAlbedo * Lum * DiscT + ExoTint * OnDiscAtmo) * AtmoTrans;
        }

        // Physical off-limb volumetric atmospheric shell
        if (ExoStr > 0.001 && ExoThick > 0.001 && RNorm >= 0.96 && RNorm < MaxShellMul)
        {
            float LimbAlt   = max(0.0, RNorm - 1.0);
            float ScaleH    = max(0.02, ExoThick * 0.30);
            float ChordGeom = sqrt(max(0.0, 1.0 - (LimbAlt / ExoThick) * (LimbAlt / ExoThick)));
            float RadDen    = exp(-LimbAlt / ScaleH) * ChordGeom * smoothstep(0.955, 1.005, RNorm);

            float2 Norm2D   = float2(PX, PY) / max(1.0e-4, RNorm);
            float  LimbDot  = Norm2D.x * PhL.x + Norm2D.y * PhL.y + 0.20 * PhL.z;
            float  TwiScat  = smoothstep(-ExoWrap - 0.16, 0.58, LimbDot);
            float  FwdScat  = 0.72 + 0.55 * pow(max(0.0, 0.5 - 0.5 * PhL.z), 1.6);
            float  ShellLum = RadDen * TwiScat * FwdScat * ExoStr * M.OpticsOrientation.x * 0.92;
            OutRgb += ExoTint * ShellLum * AtmoTrans;
        }
    }

    // 3. Pinpoint Optical Starfield (J2000 Catalog + 2-Tier Cubemap + Planckian B-V Color + Milky Way Band)
    if (Cfg.StarParams.x > 0.5 && (OrbitalView || RayDir.z > -0.02))
    {
        float DayFade = OrbitalView ? 1.0 : clamp(1.0 - (SunDir.z + 0.08) * 5.0, 0.0, 1.0);
        if (DayFade > 0.01)
        {
            float CR = cos(Cfg.StarScintillationRot.w);
            float SR = sin(Cfg.StarScintillationRot.w);
            float3 RotDir = normalize(float3(RayDir.x * CR - RayDir.y * SR, RayDir.x * SR + RayDir.y * CR, RayDir.z));

            float AltFade = OrbitalView ? 1.0 : clamp((RayDir.z + 0.01) * 4.5, 0.0, 1.0);

            float GalLat      = dot(RotDir, float3(-0.34, 0.74, 0.58));
            float GalBandMask = exp(-GalLat * GalLat * 18.0);
            if (EnableGalBand)
            {
                float Rift     = SmoothNoiseCel3(RotDir * 6.0 + float3(1.4, -0.9, 0.0)) * 0.68 +
                                 SmoothNoiseCel3(RotDir * 14.5 + float3(-0.7, 2.1, 0.5)) * 0.32;
                float DustLane = smoothstep(0.34, 0.66, Rift) * exp(-GalLat * GalLat * 85.0);
                float GalCloud = SmoothNoiseCel3(RotDir * 3.4);
                float GalLum   = GalBandMask * (0.55 + 0.70 * GalCloud) * (1.0 - 0.65 * DustLane) *
                                 0.032 * Cfg.StarParams.z * DayFade * AltFade;
                OutRgb += float3(0.72, 0.80, 1.05) * GalLum * AtmoTrans;
            }

            float PixelAng   = 1.05 / max(200.0, Cfg.ClockViewportDims.z);
            float CoreBase   = 0.000145 * clamp(Cfg.StarParams.w, 0.35, 2.5);
            float SigmaCore  = max(CoreBase, PixelAng * 0.45);
            float EnergyNorm = clamp((CoreBase * CoreBase * 5.0) / (SigmaCore * SigmaCore), 0.38, 1.0);
            bool  TwinkleOn  = (Cfg.StarScintillationRot.x > 0.5);

            if (EnableCatalog)
            {
                for (uint SIdx = 0u; SIdx < J2000CatalogCount; ++SIdx)
                {
                    float4 DM = J2000StarDirsAndMags[SIdx];
                    OutRgb += EvaluateOpticalStarContributionGpu(
                        RotDir, DM.xyz, DM.w, J2000StarBvs[SIdx], DM.x * 19.0 + DM.y * 7.0,
                        Cfg.StarParams.y, Cfg.StarParams.z, SigmaCore, EnergyNorm, DayFade, AltFade,
                        OrbitalView, Cfg.ClockViewportDims.x, TwinkleOn, Cfg.StarScintillationRot.y, Cfg.StarScintillationRot.z, AtmoTrans);
                }
            }

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

            float MagScale  = clamp(Cfg.StarParams.y / 5.5, 0.15, 1.8);
            float BandBoost = EnableGalBand ? (1.0 + 1.35 * GalBandMask) : 1.0;

            for (uint Tier = 0u; Tier < 2u; ++Tier)
            {
                int   GridCount = (Tier == 0u) ? 72 : 196;
                float GridN     = float(GridCount);
                float BaseProb  = (Tier == 0u) ? (0.14 * MagScale) : (0.28 * MagScale);
                float StarProb  = min(0.65, BaseProb * BandBoost);
                float MagMin    = (Tier == 0u) ? 1.35 : 3.60;
                float MagMax    = (Tier == 0u) ? 4.20 : max(4.0, Cfg.StarParams.y);
                int   SeedBase  = (Tier == 0u) ? 17 : 101;

                int CX = clamp(int(floor((FU * 0.5 + 0.5) * GridN)), 0, GridCount - 1);
                int CY = clamp(int(floor((FV * 0.5 + 0.5) * GridN)), 0, GridCount - 1);

                for (int DY = -1; DY <= 1; ++DY)
                {
                    int NY = CY + DY;
                    if (NY < 0 || NY >= GridCount) continue;
                    for (int DX = -1; DX <= 1; ++DX)
                    {
                        int NX = CX + DX;
                        if (NX < 0 || NX >= GridCount) continue;

                        float H1 = HashCelestial3(Face * 193 + NX, NY, SeedBase);
                        if (H1 <= 1.0 - StarProb) continue;

                        float H2 = HashCelestial3(Face * 193 + NX + 19, NY - 7, SeedBase + 14);
                        float H3 = HashCelestial3(Face * 193 + NX - 13, NY + 29, SeedBase + 36);
                        float H4 = HashCelestial3(Face * 193 + NX + 41, NY - 23, SeedBase + 72);
                        float SU = ((float(NX) + 0.18 + 0.64 * H2) / GridN) * 2.0 - 1.0;
                        float SV = ((float(NY) + 0.18 + 0.64 * H3) / GridN) * 2.0 - 1.0;

                        float3 StarDir;
                        if      (Face == 0) StarDir = normalize(float3( 1.0,  SV, -SU));
                        else if (Face == 1) StarDir = normalize(float3(-1.0,  SV,  SU));
                        else if (Face == 2) StarDir = normalize(float3( SU,  1.0, -SV));
                        else if (Face == 3) StarDir = normalize(float3( SU, -1.0,  SV));
                        else if (Face == 4) StarDir = normalize(float3( SU,  SV,  1.0));
                        else                StarDir = normalize(float3( SU, -SV, -1.0));

                        float Rank = saturate((H1 - (1.0 - StarProb)) / max(1.0e-4, StarProb));
                        float Mag  = MagMax - (MagMax - MagMin) * pow(Rank, 2.2);
                        float BV   = -0.28 + 2.05 * pow(H4, 1.35);
                        OutRgb += EvaluateOpticalStarContributionGpu(
                            RotDir, StarDir, Mag, BV, H2,
                            Cfg.StarParams.y, Cfg.StarParams.z, SigmaCore, EnergyNorm, DayFade, AltFade,
                            OrbitalView, Cfg.ClockViewportDims.x, TwinkleOn, Cfg.StarScintillationRot.y, Cfg.StarScintillationRot.z, AtmoTrans);
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
