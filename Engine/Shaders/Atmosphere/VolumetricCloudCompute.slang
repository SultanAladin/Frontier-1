/*============================================================================================================================================
                                                      VOLUMETRICCLOUDCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Anti-Banded 64-Step IGN Raymarched 3D FBM Planetary Volumetric Clouds (12 Formations), Bounded LocalCloudVolume & 3-Octave Multi-Scattering

static const float CloudPiConst = 3.14159265359;

struct GpuLocalCloudVolume
{
    float4 CenterShape;          // xyz = CenterWorld, w = Shape (0..6)
    float4 HalfExtentYaw;        // xyz = HalfExtentM, w = YawRad
    float4 DensityCovScaleSides; // x = Density, y = Coverage, z = InvFeatureScale, w = float(PrismSides)
    float4 WindVelCurl;          // xyz = ResolvedWindVelMps, w = CurlScalar
    float4 SoftAnisoPad;         // x = EdgeSoftness, y = AnisotropyG, z = FollowWind (0/1), w = Pad
};

struct GpuCloudWindForceProbe
{
    float4 PosRadius;       // xyz = EvaluatedCenterWorld, w = RadiusMeters
    float4 ForwardStrength; // xyz = ForwardUnitAxis,      w = StrengthMps
    float4 Params;          // x = ModelId, y = SwirlRatio, z = CoreRadiusM, w = CouplingGain (0=disabled)
};

struct VolumetricCloudUniformBlock
{
    float4 OriginTimeVector;     // xyz = CameraOrigin, w = SimulationSeconds
    float4 SunAxisToggle;        // xyz = SunDirection, w = GlobalCloudsEnabled (0/1)
    float4 SolarIrradianceExt;   // xyz = SolarIrradiance, w = PlanetRadiusM
    float4 SkyColorWind;         // xyz = SkyLowAngleColor, w = WindFollow (0/1)
    float4 DeckGeometry;         // x = FloorM, y = ThickM, z = CloudFraction, w = DensityMul
    float4 DeckOptics;           // x = FeatureScale, y = Anvil, z = AnisotropyG, w = WindShearPerKm
    float4 WindVectorOcclude;    // x = WindVX, y = WindVY, z = ShadowEnabled (0/1), w = LocalEnabled (0/1)
    float4 LocalCenterExt;       // xyz = LocalCenterM, w = LocalFeatureScaleM
    float4 LocalExtentFraction;  // xyz = LocalHalfExtentM, w = LocalFraction
    float4 LocalDensityPhase;    // x = LocalDensityMul, y = LocalAnisotropyG, z = ViewportW, w = ViewportH
    float4 FormationOptics;      // x = FormationIndex (0..11), y = VeerRadPerKm, z = WindTurbulence, w = LocalCloudCount (0..8)
    float4 CloudShadowOptics;    // x = ShadowFraction, y = ShadowDensityMul, z = ShadowScale, w = ShadowAnvil
    float4 CloudPassControl;     // x = CloudPassW, y = CloudPassH, z = CloudMarchSteps (12..96), w = WindProbeCount (0..8)
    float4 WindForceMeta;        // x = CouplingScaleHeightM, y = GlobalWindVX, z = GlobalWindVY, w = Pad
    GpuLocalCloudVolume      LocalClouds[8];
    GpuCloudWindForceProbe   WindProbes[8];
};

float HashCloudInt3(int IX, int IY, int IZ)
{
    uint N = uint(IX) * 374761393u + uint(IY) * 668265263u + uint(IZ) * 2147483647u;
    N = (N ^ (N >> 13u)) * 1274126177u;
    N = N ^ (N >> 16u);
    return float(N) * (1.0 / 4294967296.0);
}

float LatticeNoiseCloud3(float3 P)
{
    int3 I = int3(floor(P));
    float3 F = P - float3(I);
    float3 U = F * F * (3.0 - 2.0 * F);

    float N000 = HashCloudInt3(I.x, I.y, I.z),         N100 = HashCloudInt3(I.x + 1, I.y, I.z);
    float N010 = HashCloudInt3(I.x, I.y + 1, I.z),     N110 = HashCloudInt3(I.x + 1, I.y + 1, I.z);
    float N001 = HashCloudInt3(I.x, I.y, I.z + 1),     N101 = HashCloudInt3(I.x + 1, I.y, I.z + 1);
    float N011 = HashCloudInt3(I.x, I.y + 1, I.z + 1), N111 = HashCloudInt3(I.x + 1, I.y + 1, I.z + 1);

    return lerp(
        lerp(lerp(N000, N100, U.x), lerp(N010, N110, U.x), U.y),
        lerp(lerp(N001, N101, U.x), lerp(N011, N111, U.x), U.y),
        U.z);
}

float CloudFbmGpu3(float3 P, float HighOctaveFade)
{
    float  Sum = 0.0;
    float  Amp = 0.52;
    float3 Q   = P;
    for (uint Oct = 0u; Oct < 5u; ++Oct)
    {
        float W = (Oct >= 3u) ? HighOctaveFade : 1.0;
        Sum += Amp * (W * LatticeNoiseCloud3(Q) + (1.0 - W) * 0.5);
        Q = float3(
             0.80 * Q.x - 0.48 * Q.y + 0.36 * Q.z,
             0.48 * Q.x + 0.86 * Q.y + 0.16 * Q.z,
            -0.36 * Q.x + 0.04 * Q.y + 0.93 * Q.z) * 2.05 + float3(13.7, -7.3, 4.1);
        Amp *= 0.48;
    }
    return Sum;
}

float3 EvaluateCloudWindProbesVelocityGpu(
    VolumetricCloudUniformBlock Cfg,
    float3                      WorldPos,
    bool                        ProjectAcrossCloudDeck)
{
    float3 Vel = float3(Cfg.WindForceMeta.y, Cfg.WindForceMeta.z, 0.0);
    uint ProbeCount = min(8u, uint(Cfg.CloudPassControl.w + 0.5));
    float CouplingSpan = max(500.0, Cfg.WindForceMeta.x);
    for (uint I = 0u; I < ProbeCount; ++I)
    {
        GpuCloudWindForceProbe Pr = Cfg.WindProbes[I];
        float Gain = Pr.Params.w;
        if (Gain <= 1.0e-5) continue;

        float3 QPos = WorldPos;
        float  AltW = 1.0;
        if (ProjectAcrossCloudDeck)
        {
            float Dz        = WorldPos.z - Pr.PosRadius.z;
            uint  EnvelopeK = Pr.ModeBits & 3u;
            float DirectLim = (EnvelopeK == 0u)
                ? max(10.0, Pr.PosRadius.w * 0.35)
                : max(4.0, min(Pr.PosRadius.w, Pr.HalfExt.z) * 0.45);
            float AbsDz     = abs(Dz);
            if (AbsDz > DirectLim)
            {
                float Excess  = AbsDz - DirectLim;
                float SmoothT = smoothstep(0.0, max(4.0, DirectLim * 0.65), Excess);
                QPos.z        = lerp(WorldPos.z, Pr.PosRadius.z, SmoothT);
                float NormZ   = Excess / CouplingSpan;
                AltW          = exp(-0.5 * NormZ * NormZ);
            }
        }
        float3 D    = QPos - Pr.PosRadius.xyz;
        float  Rad  = max(1.0, Pr.PosRadius.w);
        float  Dist = length(D);
        if (Dist >= Rad) continue;

        float Mask = 1.0 - smoothstep(0.0, Rad, Dist);
        uint  Mid  = uint(Pr.Params.x + 0.5);
        float Str  = Pr.ForwardStrength.w * Mask * Gain * AltW;

        if (Mid == 7u || Mid == 8u || Mid == 12u) // RotationalColumn / Torque / VorticityConfinement
        {
            float Core = max(1.0, Pr.Params.z);
            float R2D  = max(Core, length(D.xy));
            float3 Tang = float3(-D.y / R2D, D.x / R2D, 0.0);
            float3 Inw  = float3(-D.x / R2D, -D.y / R2D, 0.0);
            Vel += (Tang * Pr.Params.y + Inw * 0.25) * Str;
        }
        else if (Mid == 10u || Mid == 13u) // CurlNoise / BrownianNoise
        {
            float TurbS = 0.012;
            float3 NP = QPos * TurbS;
            float3 CDir = float3(
                LatticeNoiseCloud3(NP + float3(0.0, 3.1, 1.7)) - 0.5,
                LatticeNoiseCloud3(NP + float3(5.3, 0.0, 2.9)) - 0.5,
                LatticeNoiseCloud3(NP + float3(1.9, 4.7, 0.0)) - 0.5) * 2.0;
            Vel += CDir * Str;
        }
        else if (Mid == 2u || Mid == 3u || Mid == 4u) // RadialBurst / Attractor / Repulsor
        {
            float Sign = (Mid == 3u) ? -1.0 : 1.0;
            float3 Dir = (Dist > 0.1) ? (D / Dist) : float3(0.0, 0.0, 1.0);
            Vel += Dir * (Sign * Str);
        }
        else
        {
            Vel += normalize(Pr.ForwardStrength.xyz) * Str;
        }
    }
    return Vel;
}

float3 AdvectCloudPointBackwardGpu(
    VolumetricCloudUniformBlock Cfg,
    float3                      WorldPos,
    float                       TotalSeconds,
    bool                        ProjectAcrossCloudDeck)
{
    if (abs(TotalSeconds) <= 1.0e-4) return WorldPos;
    float Dt = TotalSeconds * 0.25;
    float3 Pos = WorldPos;
    for (uint S = 0u; S < 4u; ++S)
    {
        float3 K1 = EvaluateCloudWindProbesVelocityGpu(Cfg, Pos, ProjectAcrossCloudDeck);
        float3 Mid = Pos - K1 * (0.5 * Dt);
        float3 K2 = EvaluateCloudWindProbesVelocityGpu(Cfg, Mid, ProjectAcrossCloudDeck);
        Pos -= K2 * Dt;
    }
    return Pos;
}

float EvaluateCloudPhaseHG(float CosTheta, float G)
{
    float G2  = G * G;
    float Den = max(1.0e-3, 1.0 + G2 - 2.0 * G * CosTheta);
    return (1.0 - G2) / (4.0 * CloudPiConst * pow(Den, 1.5));
}

float2 IntersectCloudShell(float OriginZ, float DirZ, float Radius)
{
    float B    = OriginZ * DirZ;
    float C    = (OriginZ - Radius) * (OriginZ + Radius);
    float Disc = B * B - C;
    if (Disc < 0.0) return float2(-1.0, -1.0);
    float Root = sqrt(Disc);
    return float2(-B - Root, -B + Root);
}

float SampleCloudDeckDensityGpu(
    float3 P,
    float  PlanetR,
    float  FloorM,
    float  ThickM,
    float  Cov,
    float  Anvil,
    float  DensMul,
    float  FScale,
    float  WindX,
    float  WindY,
    float  ShearPerKm,
    uint   FormationIndex,
    float  DistanceMeters,
    float  VeerRadPerKm = 0.0,
    float  TurbulenceAmp = 0.0,
    float  TimeSec = 0.0)
{
    float Alt     = length(P) - PlanetR;
    float H       = clamp((Alt - FloorM) / max(1.0, ThickM), 0.0, 1.0);
    float Bot     = smoothstep(0.0, 0.22, H);
    float Top     = 1.0 - smoothstep(max(0.20, 0.72 - 0.24 * Anvil), 1.0, H);
    float Profile = Bot * Top;
    if (Profile <= 0.001) return 0.0;

    float HighOctFade = 1.0 - smoothstep(5500.0, 19000.0, DistanceMeters);
    float AltKm       = (Alt - FloorM) * 0.001;
    float VeerAng     = AltKm * VeerRadPerKm;
    float CsV         = cos(VeerAng);
    float SnV         = sin(VeerAng);
    float VeeredWX    = WindX * CsV - WindY * SnV;
    float VeeredWY    = WindX * SnV + WindY * CsV;
    float ShearShift  = AltKm * ShearPerKm * 45.0;
    float BoilPhase   = TimeSec * (0.045 + 0.095 * TurbulenceAmp);
    float TurbWarpX   = (TurbulenceAmp > 1.0e-4)
        ? (LatticeNoiseCloud3(float3((P.x - VeeredWX) * FScale * 0.45, (P.y - VeeredWY) * FScale * 0.45, AltKm * 0.8 + BoilPhase)) - 0.5) * (TurbulenceAmp * 210.0)
        : 0.0;
    float TurbWarpY   = (TurbulenceAmp > 1.0e-4)
        ? (LatticeNoiseCloud3(float3((P.y - VeeredWY) * FScale * 0.45 + 7.3, (P.x - VeeredWX) * FScale * 0.45, AltKm * 0.8 - BoilPhase * 0.85)) - 0.5) * (TurbulenceAmp * 210.0)
        : 0.0;
    float WX = P.x - VeeredWX - ShearShift + TurbWarpX;
    float WY = P.y - VeeredWY + TurbWarpY;
    float WZ = (Alt - FloorM) + TimeSec * (1.8 + 4.2 * TurbulenceAmp);

    float ShapeCov = clamp(Cov + Anvil * H * H * 0.22, 0.0, 1.0);
    float NNorm    = 0.0;

    if (FormationIndex == 6u) // Lenticular (smooth FBM-warped laminar mountain lenses)
    {
        float MacroLobes    = LatticeNoiseCloud3(float3(WX * FScale * 0.22, WY * FScale * 0.55, 1.7));
        float SubtleWarp    = LatticeNoiseCloud3(float3(WX * FScale * 0.58, WY * FScale * 0.58, WZ * FScale * 0.30));
        float LaminarSlices = 0.5 + 0.5 * sin(H * CloudPiConst * 2.5 - 0.3 + (SubtleWarp - 0.5) * 0.85);
        float LensMask      = smoothstep(0.45, 0.76, MacroLobes + (SubtleWarp - 0.5) * 0.26);
        NNorm   = clamp(LensMask * (0.52 + 0.48 * HighOctFade * LaminarSlices), 0.0, 1.0);
        Profile = smoothstep(0.04, 0.24, H) * (1.0 - smoothstep(0.74, 0.96, H));
    }
    else if (FormationIndex == 7u) // Mammatus: non-axial 3-azimuth cellular hanging pouches
    {
        float BaseFbm   = CloudFbmGpu3(float3(WX * FScale * 0.95, WY * FScale * 0.95, WZ * FScale * 1.15), HighOctFade);
        float WarpedX   = WX + (BaseFbm - 0.5) * (2.6 / max(1.0e-5, FScale * 3.4));
        float WarpedY   = WY - (BaseFbm - 0.5) * (2.6 / max(1.0e-5, FScale * 3.4));
        float K0        = cos(WarpedX * FScale * 3.2);
        float K1        = cos((-0.5 * WarpedX + 0.866025 * WarpedY) * FScale * 3.2);
        float K2        = cos((-0.5 * WarpedX - 0.866025 * WarpedY) * FScale * 3.2);
        float HexPouch  = pow(clamp((K0 + K1 + K2 + 1.5) / 4.5, 0.0, 1.0), 1.15) * (0.28 + 0.72 * HighOctFade);
        float LowerMask = 1.0 - smoothstep(0.18, 0.68, H);
        NNorm    = clamp(((BaseFbm - 0.24) / 0.44) * 0.62 + HexPouch * LowerMask * 0.62, 0.0, 1.0);
        ShapeCov = clamp(Cov + LowerMask * 0.14 + Anvil * H * 0.16, 0.0, 1.0);
    }
    else if (FormationIndex == 8u) // KelvinHelmholtz
    {
        float MacroBand = smoothstep(0.38, 0.72, LatticeNoiseCloud3(float3(WX * FScale * 0.28, WY * FScale * 0.28, 3.1)));
        float CurlSkew  = 280.0 * sin(H * CloudPiConst) + 190.0 * H * H;
        float CurlX     = WX + CurlSkew;
        float WavePhase = CurlX * FScale * 3.8 + LatticeNoiseCloud3(float3(WX * FScale * 0.85, WY * FScale * 0.85, 0.4)) * 2.6;
        float Crest     = 0.5 + 0.5 * sin(WavePhase + H * 3.0);
        float BreakCurl = smoothstep(0.28, 0.88, Crest) * smoothstep(0.12, 0.65, H + 0.24 * Crest);
        float Billow    = CloudFbmGpu3(float3(CurlX * FScale * 1.05, WY * FScale * 0.85, WZ * FScale * 1.4), HighOctFade);
        NNorm = clamp(MacroBand * (BreakCurl * (0.35 + 0.35 * HighOctFade) + ((Billow - 0.24) / 0.46) * 0.48), 0.0, 1.0);
    }
    else if (FormationIndex == 9u) // Asperitas: isotropic 4-azimuth gravity-wave swell (zero 2-dir banding)
    {
        float Warp   = CloudFbmGpu3(float3(WX * FScale * 0.75, WY * FScale * 0.75, WZ * FScale), HighOctFade);
        float W1     = sin((WX * 0.966 + WY * 0.259) * FScale * 3.6 + Warp * 3.2);
        float W2     = cos((WX * 0.391 + WY * 0.920) * FScale * 3.9 - Warp * 2.8);
        float W3     = sin((-WX * 0.391 + WY * 0.920) * FScale * 4.2 + Warp * 2.4);
        float W4     = cos((-WX * 0.927 + WY * 0.375) * FScale * 3.5 - Warp * 3.1);
        float Swell  = 0.5 + 0.125 * (W1 + W2 + W3 + W4) * (0.28 + 0.72 * HighOctFade);
        float Detail = CloudFbmGpu3(float3(WX * FScale * 1.15, WY * FScale * 1.15, WZ * FScale * 1.3), HighOctFade);
        NNorm = clamp(((Detail - 0.24) / 0.46) * 0.64 + Swell * 0.44, 0.0, 1.0);
    }
    else if (FormationIndex == 10u) // Nacreous
    {
        float MacroEnv = smoothstep(0.32, 0.74, LatticeNoiseCloud3(float3(WX * FScale * 0.34, WY * FScale * 0.34, 2.3)));
        float WarpN    = LatticeNoiseCloud3(float3(WX * FScale * 0.75, WY * FScale * 0.75, 0.5));
        float RibSin   = 0.5 + 0.5 * sin((WX * 0.42 + WY * 0.88) * FScale * 2.6 + WarpN * 3.8);
        float Ribbon   = RibSin * (1.45 - 0.45 * RibSin);
        float Silk     = LatticeNoiseCloud3(float3(WX * FScale * 1.05, WY * FScale * 1.05, WZ * FScale));
        NNorm = clamp(MacroEnv * ((0.22 + 0.38 * HighOctFade) * Ribbon + Silk * 0.52), 0.0, 1.0);
    }
    else if (FormationIndex == 11u) // Pyrocumulus
    {
        float PlumeColumn = LatticeNoiseCloud3(float3(WX * FScale * 0.36, WY * FScale * 0.36, 4.7));
        float PlumeMask   = smoothstep(0.45, 0.74, PlumeColumn + H * 0.10);
        float Base        = CloudFbmGpu3(float3(WX * FScale * 0.90, WY * FScale * 0.90, WZ * FScale * 1.15), HighOctFade);
        float RidgeRaw    = LatticeNoiseCloud3(float3(WX * FScale * 1.6, WY * FScale * 1.6, WZ * FScale * 1.7)) * 2.0 - 1.0;
        float BillowRidge = 1.0 - RidgeRaw * RidgeRaw;
        NNorm    = clamp(PlumeMask * (((Base - 0.20) / 0.44) * 0.76 + BillowRidge * 0.28 * (0.25 + 0.75 * HighOctFade)), 0.0, 1.0);
        ShapeCov = clamp(Cov + 0.14 * H, 0.0, 1.0);
    }
    else // Classic 0..5
    {
        float N = CloudFbmGpu3(float3(WX * FScale, WY * FScale, WZ * FScale * 1.4), HighOctFade);
        NNorm = clamp((N - 0.26) / 0.44, 0.0, 1.0);
    }

    float Thresh = 1.0 - ShapeCov;
    return max(0.0, (NNorm - Thresh) / max(0.18, ShapeCov)) * Profile * DensMul;
}

float EvaluateCloudVolumeShapeMaskGpu(
    float3 P,
    float3 Center,
    float3 HalfExt,
    uint   Shape,
    float  YawRad,
    float  PrismSides,
    float  Softness)
{
    float3 H  = max(float3(0.1), HalfExt);
    float3 D  = P - Center;
    float  Cs = cos(YawRad);
    float  Sn = sin(YawRad);
    float3 L  = float3(D.x * Cs + D.y * Sn, -D.x * Sn + D.y * Cs, D.z);
    float3 N  = L / H;

    float NormDist = 0.0;
    if (Shape == 0u)
    {
        NormDist = max(abs(N.x), max(abs(N.y), abs(N.z)));
    }
    else if (Shape == 1u)
    {
        float MinR = min(H.x, min(H.y, H.z));
        NormDist = length(L) / MinR;
    }
    else if (Shape == 2u)
    {
        NormDist = length(N);
    }
    else if (Shape == 3u)
    {
        NormDist = max(length(N.xy), abs(N.z));
    }
    else if (Shape == 4u)
    {
        float T   = clamp(0.5 * (1.0 - N.z), 0.0, 1.0);
        float Rad = length(N.xy) / max(0.08, T);
        NormDist  = max(Rad, abs(N.z));
    }
    else if (Shape == 5u)
    {
        NormDist = abs(N.x) + abs(N.y) + abs(N.z);
    }
    else if (Shape == 6u)
    {
        float Sides = clamp(PrismSides, 3.0, 16.0);
        float Theta = atan2(N.y, N.x);
        float Sec   = 6.2831853 / Sides;
        float ModA  = (Theta - Sec * floor(Theta / Sec)) - 0.5 * Sec;
        float Rad   = length(N.xy) * cos(ModA) / max(0.25, cos(0.5 * Sec));
        NormDist    = max(Rad, abs(N.z));
    }
    else // HalfSphere (7u)
    {
        float BaseDepth = (L.z < 0.0) ? (-L.z / max(0.05, 0.14 * H.z)) : 0.0;
        float DomeRad   = (L.z >= 0.0) ? length(N) : length(N.xy);
        NormDist        = max(DomeRad, BaseDepth);
    }

    float Soft = clamp(Softness, 0.04, 0.92);
    float Mask = 1.0 - smoothstep(max(0.0, 1.0 - Soft), 1.0, NormDist);
    if (Shape == 7u)
    {
        float RadialDecay = pow(saturate(1.0 - NormDist * 0.82), 0.72);
        Mask *= (0.35 + 0.65 * RadialDecay);
    }
    return Mask;
}

float EvaluateLocalCloudsShadowGpu(VolumetricCloudUniformBlock Cfg, float3 WorldPos, float3 SunDir)
{
    uint VolCount = min(8u, uint(Cfg.FormationOptics.w + 0.5));
    if (VolCount == 0u || SunDir.z <= 0.01) return 1.0;

    float TimeSec = Cfg.OriginTimeVector.w;
    float3 InvD   = 1.0 / select(abs(SunDir) > float3(1.0e-6), SunDir, float3(1.0e-6));
    float Trans   = 1.0;

    for (uint I = 0u; I < VolCount; ++I)
    {
        GpuLocalCloudVolume Vol = Cfg.LocalClouds[I];
        if (Vol.DensityCovScaleSides.x <= 0.01 || Vol.DensityCovScaleSides.y <= 0.01) continue;

        float3 Center  = Vol.CenterShape.xyz;
        float3 HalfExt = Vol.HalfExtentYaw.xyz;
        float  MaxExt  = max(HalfExt.x, max(HalfExt.y, HalfExt.z)) * 1.45;
        float3 BoxMin  = Center - float3(MaxExt, MaxExt, HalfExt.z);
        float3 BoxMax  = Center + float3(MaxExt, MaxExt, HalfExt.z);
        float3 T0      = (BoxMin - WorldPos) * InvD;
        float3 T1      = (BoxMax - WorldPos) * InvD;
        float3 TN      = min(T0, T1);
        float3 TF      = max(T0, T1);
        float  TEnter  = max(0.02, max(TN.x, max(TN.y, TN.z)));
        float  TExit   = min(250.0, min(TF.x, min(TF.y, TF.z)));
        if (TExit <= TEnter) continue;

        float Dt       = (TExit - TEnter) / 6.0;
        float LScale   = Vol.DensityCovScaleSides.z;
        float Cov      = clamp(Vol.DensityCovScaleSides.y, 0.01, 1.0);
        float Thresh   = 1.0 - Cov;
        uint  ShapeIdx = uint(Vol.CenterShape.w + 0.5);
        bool  DoWind   = (Vol.SoftAnisoPad.z > 0.5);
        float Tau      = 0.0;

        for (uint S = 0u; S < 6u; ++S)
        {
            float  T    = TEnter + (float(S) + 0.5) * Dt;
            float3 P    = WorldPos + SunDir * T;
            float  Mask = EvaluateCloudVolumeShapeMaskGpu(
                P, Center, HalfExt, ShapeIdx, Vol.HalfExtentYaw.w, Vol.DensityCovScaleSides.w, Vol.SoftAnisoPad.x);
            if (Mask <= 1.0e-4) continue;
            float3 AdvP = P;
            if (DoWind)
            {
                if (Cfg.CloudPassControl.w > 0.5)
                {
                    AdvP = AdvectCloudPointBackwardGpu(Cfg, P, TimeSec, true);
                }
                else
                {
                    AdvP = float3(
                        P.x - Vol.WindVelCurl.x * TimeSec,
                        P.y - Vol.WindVelCurl.y * TimeSec,
                        P.z - Vol.WindVelCurl.z * TimeSec * 0.35);
                }
            }
            float N   = CloudFbmGpu3(AdvP * LScale, 1.0);
            float Den = max(0.0, (N - Thresh) / max(0.15, Cov)) * Mask * Vol.DensityCovScaleSides.x * 0.085;
            Tau += Den * Dt;
        }
        Trans *= (0.14 + 0.86 * exp(-Tau));
    }
    return clamp(Trans, 0.08, 1.0);
}

float3 EvaluateLocalCloudsSegmentGpu(
    VolumetricCloudUniformBlock Cfg,
    float3                      RayOrigin,
    float3                      RayDir,
    float                       MaxDistM,
    float2                      PixelCoord,
    inout float                 InOutTransmittance)
{
    float3 TotalRad = float3(0.0, 0.0, 0.0);
    float  SafeMax  = max(0.0, MaxDistM);
    if (SafeMax <= 0.05) return TotalRad;

    float Ign      = frac(52.9829189 * frac(dot(PixelCoord, float2(0.06711056, 0.00583715))));
    float Jitter   = Ign;
    float3 SunDir  = normalize(Cfg.SunAxisToggle.xyz);
    float CosTheta = dot(RayDir, SunDir);
    float TimeSec  = Cfg.OriginTimeVector.w;
    float3 InvD    = 1.0 / select(abs(RayDir) > float3(1.0e-6), RayDir, float3(1.0e-6));

    uint VolCount = min(8u, uint(Cfg.FormationOptics.w + 0.5));
    if (VolCount > 0u)
    {
        for (uint I = 0u; I < VolCount; ++I)
        {
            GpuLocalCloudVolume Vol = Cfg.LocalClouds[I];
            if (Vol.DensityCovScaleSides.x <= 0.01 || Vol.DensityCovScaleSides.y <= 0.01) continue;

            float3 Center  = Vol.CenterShape.xyz;
            float3 HalfExt = Vol.HalfExtentYaw.xyz;
            float  MaxExt  = max(HalfExt.x, max(HalfExt.y, HalfExt.z)) * 1.45;
            float3 BoxMin  = Center - float3(MaxExt, MaxExt, HalfExt.z);
            float3 BoxMax  = Center + float3(MaxExt, MaxExt, HalfExt.z);
            float3 T0      = (BoxMin - RayOrigin) * InvD;
            float3 T1      = (BoxMax - RayOrigin) * InvD;
            float3 TN      = min(T0, T1);
            float3 TF      = max(T0, T1);
            float  TEnter  = max(0.05, max(TN.x, max(TN.y, TN.z)));
            float  TExit   = min(SafeMax, min(TF.x, min(TF.y, TF.z)));
            if (TExit <= TEnter) continue;

            float Dt       = (TExit - TEnter) / 24.0;
            float LScale   = Vol.DensityCovScaleSides.z;
            float Cov      = clamp(Vol.DensityCovScaleSides.y, 0.01, 1.0);
            float Thresh   = 1.0 - Cov;
            float Phase    = 0.76 * EvaluateCloudPhaseHG(CosTheta, Vol.SoftAnisoPad.y) +
                             0.24 * EvaluateCloudPhaseHG(CosTheta, -0.22);
            uint  ShapeIdx = uint(Vol.CenterShape.w + 0.5);
            bool  DoWind   = (Vol.SoftAnisoPad.z > 0.5);

            for (uint S = 0u; S < 24u; ++S)
            {
                float  T    = TEnter + (float(S) + Jitter) * Dt;
                float3 P    = RayOrigin + RayDir * T;
                float  Mask = EvaluateCloudVolumeShapeMaskGpu(
                    P, Center, HalfExt, ShapeIdx, Vol.HalfExtentYaw.w, Vol.DensityCovScaleSides.w, Vol.SoftAnisoPad.x);
                if (Mask <= 1.0e-4) continue;

                float3 AdvP = P;
                if (DoWind)
                {
                    if (Cfg.CloudPassControl.w > 0.5)
                    {
                        AdvP = AdvectCloudPointBackwardGpu(Cfg, P, TimeSec, true);
                    }
                    else
                    {
                        AdvP = float3(
                            P.x - Vol.WindVelCurl.x * TimeSec,
                            P.y - Vol.WindVelCurl.y * TimeSec,
                            P.z - Vol.WindVelCurl.z * TimeSec * 0.35);
                    }
                }

                float N   = CloudFbmGpu3(AdvP * LScale, 1.0);
                float Den = max(0.0, (N - Thresh) / max(0.15, Cov)) * Mask * Vol.DensityCovScaleSides.x * 0.048;
                if (Den > 1.0e-5)
                {
                    float SegT    = exp(-Den * Dt);
                    float Scatter = 1.0 - SegT;
                    float Amb     = 0.38 + 0.58 * clamp((P.z - BoxMin.z) / max(1.0, BoxMax.z - BoxMin.z), 0.0, 1.0);
                    TotalRad += InOutTransmittance * (Cfg.SolarIrradianceExt.xyz * Phase * 2.2 + Cfg.SkyColorWind.xyz * Amb * 0.68) * Scatter;
                    InOutTransmittance *= SegT;
                    if (InOutTransmittance < 0.01) break;
                }
            }
            if (InOutTransmittance < 0.01) break;
        }
        return TotalRad;
    }

    // Legacy single-AABB fallback when LocalClouds[] is empty and LocalEnabled is set
    if (Cfg.WindVectorOcclude.w > 0.5 && Cfg.LocalExtentFraction.w > 0.01 && Cfg.LocalDensityPhase.x > 0.01)
    {
        float  WindX  = (Cfg.SkyColorWind.w > 0.5) ? (Cfg.WindVectorOcclude.x * TimeSec) : 0.0;
        float  WindY  = (Cfg.SkyColorWind.w > 0.5) ? (Cfg.WindVectorOcclude.y * TimeSec) : 0.0;
        float3 BoxMin = Cfg.LocalCenterExt.xyz - Cfg.LocalExtentFraction.xyz;
        float3 BoxMax = Cfg.LocalCenterExt.xyz + Cfg.LocalExtentFraction.xyz;
        float3 T0     = (BoxMin - RayOrigin) * InvD;
        float3 T1     = (BoxMax - RayOrigin) * InvD;
        float3 TN     = min(T0, T1);
        float3 TF     = max(T0, T1);
        float  TEnter = max(0.05, max(TN.x, max(TN.y, TN.z)));
        float  TExit  = min(SafeMax, min(TF.x, min(TF.y, TF.z)));
        if (TExit > TEnter)
        {
            float Dt     = (TExit - TEnter) / 24.0;
            float Phase  = 0.76 * EvaluateCloudPhaseHG(CosTheta, Cfg.LocalDensityPhase.y) +
                           0.24 * EvaluateCloudPhaseHG(CosTheta, -0.22);
            float LScale = 1.0 / max(4.0, Cfg.LocalCenterExt.w);
            for (uint S = 0u; S < 24u; ++S)
            {
                float  T     = TEnter + (float(S) + Jitter) * Dt;
                float3 P     = RayOrigin + RayDir * T;
                float3 Rel   = abs(P - Cfg.LocalCenterExt.xyz) / max(float3(0.1), Cfg.LocalExtentFraction.xyz);
                float  EdgeF = 1.0 - smoothstep(0.65, 1.0, max(Rel.x, max(Rel.y, Rel.z)));
                float  N     = CloudFbmGpu3(float3((P.x - WindX) * LScale, (P.y - WindY) * LScale, P.z * LScale), 1.0);
                float  Thresh = 1.0 - Cfg.LocalExtentFraction.w;
                float  Den   = max(0.0, (N - Thresh) / max(0.15, Cfg.LocalExtentFraction.w)) *
                               EdgeF * Cfg.LocalDensityPhase.x * 0.028;
                if (Den > 1.0e-5)
                {
                    float SegT    = exp(-Den * Dt);
                    float Scatter = 1.0 - SegT;
                    float Amb     = 0.35 + 0.55 * clamp((P.z - BoxMin.z) / max(1.0, BoxMax.z - BoxMin.z), 0.0, 1.0);
                    TotalRad += InOutTransmittance * (Cfg.SolarIrradianceExt.xyz * Phase * 2.2 + Cfg.SkyColorWind.xyz * Amb * 0.65) * Scatter;
                    InOutTransmittance *= SegT;
                    if (InOutTransmittance < 0.01) break;
                }
            }
        }
    }
    return TotalRad;
}

float3 EvaluateVolumetricCloudsGpu(
    VolumetricCloudUniformBlock Cfg,
    float3                      RayDir,
    float2                      PixelCoord,
    out float                   OutTransmittance,
    bool                        IncludeLocalClouds = true,
    uint                        StepCountOverride = 0u)
{
    OutTransmittance = 1.0;
    float3 TotalRad  = float3(0.0, 0.0, 0.0);

    float Ign    = frac(52.9829189 * frac(dot(PixelCoord, float2(0.06711056, 0.00583715))));
    float Jitter = Ign;

    float3 SunDir = normalize(Cfg.SunAxisToggle.xyz);
    float  WindX  = (Cfg.SkyColorWind.w > 0.5) ? (Cfg.WindVectorOcclude.x * Cfg.OriginTimeVector.w) : 0.0;
    float  WindY  = (Cfg.SkyColorWind.w > 0.5) ? (Cfg.WindVectorOcclude.y * Cfg.OriginTimeVector.w) : 0.0;

    if (IncludeLocalClouds)
    {
        TotalRad += EvaluateLocalCloudsSegmentGpu(
            Cfg, Cfg.OriginTimeVector.xyz, RayDir, 60000.0, PixelCoord, OutTransmittance);
    }

    // 2. Global Planetary Cloud Shell (96 IGN-jittered steps + LOD FBM + 3-octave multi-scattering + Beer-Powder + Ekman Veer + Turbulence)
    if (Cfg.SunAxisToggle.w < 0.5 || RayDir.z <= 0.006 || Cfg.DeckGeometry.z <= 0.01 || Cfg.DeckGeometry.w <= 0.01)
    {
        return TotalRad;
    }

    float PlanetR = max(500000.0, Cfg.SolarIrradianceExt.w);
    float FloorM  = max(100.0, Cfg.DeckGeometry.x);
    float ThickM  = max(100.0, Cfg.DeckGeometry.y);
    float ObsAltM = clamp((Cfg.WindForceMeta.w > 0.0) ? Cfg.WindForceMeta.w : 100.0, 10.0, max(15.0, FloorM - 20.0));
    float OrigZ   = PlanetR + ObsAltM + Cfg.OriginTimeVector.z;

    float2 EnterHit = IntersectCloudShell(OrigZ, RayDir.z, PlanetR + FloorM);
    float2 ExitHit  = IntersectCloudShell(OrigZ, RayDir.z, PlanetR + FloorM + ThickM);
    float  MaxDist  = min(ExitHit.y, EnterHit.y + 32000.0);
    if (MaxDist <= EnterHit.y || EnterHit.y < 0.0 || EnterHit.y > 85000.0) return TotalRad;

    uint  MarchSteps = (StepCountOverride > 0u)
        ? clamp(StepCountOverride, 12u, 96u)
        : ((Cfg.CloudPassControl.z > 1.0) ? clamp(uint(Cfg.CloudPassControl.z + 0.5), 12u, 96u) : 36u);
    float Dt        = (MaxDist - EnterHit.y) / float(MarchSteps);
    float FScale    = 0.00065 / max(0.08, Cfg.DeckOptics.x);
    float Cov       = clamp(Cfg.DeckGeometry.z, 0.0, 1.0);
    float DensMul   = max(0.0, Cfg.DeckGeometry.w) * 0.0085;
    float Anvil     = clamp(Cfg.DeckOptics.y, 0.0, 1.0);
    float G         = clamp(Cfg.DeckOptics.z, -0.5, 0.9);
    float CosTheta  = dot(RayDir, SunDir);
    uint  FmtIdx    = uint(Cfg.FormationOptics.x + 0.5);
    float VeerRadKm = Cfg.FormationOptics.y;
    float TurbAmp   = Cfg.FormationOptics.z;
    float TimeSec   = (Cfg.SkyColorWind.w > 0.5) ? Cfg.OriginTimeVector.w : 0.0;

    static const float SunOffsets[5] = { 0.08, 0.22, 0.44, 0.78, 1.35 };
    static const float SunWeights[5] = { 0.14, 0.22, 0.28, 0.36, 0.50 };

    float  DeckT = 1.0;
    float3 DeckR = float3(0.0, 0.0, 0.0);
    bool   UseForceProbes = (Cfg.SkyColorWind.w > 0.5 && Cfg.CloudPassControl.w > 0.5 && abs(TimeSec) > 1.0e-4);
    float  EffWindX = UseForceProbes ? 0.0 : WindX;
    float  EffWindY = UseForceProbes ? 0.0 : WindY;

    for (uint S = 0u; S < MarchSteps; ++S)
    {
        float  T = EnterHit.y + (float(S) + Jitter) * Dt;
        float3 P = float3(RayDir.x * T, RayDir.y * T, OrigZ + RayDir.z * T);
        float3 EvalP = P;
        if (UseForceProbes)
        {
            float AltW = max(FloorM, length(P) - PlanetR);
            float3 AdvW = AdvectCloudPointBackwardGpu(Cfg, float3(P.x, P.y, AltW), TimeSec, true);
            EvalP = float3(AdvW.x, AdvW.y, sqrt(max(1.0, (PlanetR + AdvW.z) * (PlanetR + AdvW.z) - AdvW.x * AdvW.x - AdvW.y * AdvW.y)));
        }
        float  D = SampleCloudDeckDensityGpu(
            EvalP, PlanetR, FloorM, ThickM, Cov, Anvil, DensMul, FScale, EffWindX, EffWindY, Cfg.DeckOptics.w, FmtIdx, T, VeerRadKm, TurbAmp, TimeSec);
        if (D > 1.0e-6)
        {
            float SunTau = 0.0;
            if (Cfg.WindVectorOcclude.z > 0.5)
            {
                float ConeReach = ThickM * 0.42;
                for (uint K = 0u; K < 5u; ++K)
                {
                    float KS = SunOffsets[K] * ConeReach;
                    SunTau += SampleCloudDeckDensityGpu(
                        EvalP + SunDir * KS,
                        PlanetR, FloorM, ThickM, Cov, Anvil, DensMul, FScale, EffWindX, EffWindY, Cfg.DeckOptics.w, FmtIdx, T, VeerRadKm, TurbAmp, TimeSec) *
                        (SunWeights[K] * ConeReach);
                }
            }

            float MultiScatter = 0.0;
            float OctAlbedo = 1.0;
            float OctExt    = 1.0;
            float OctG      = 1.0;
            for (uint Oct = 0u; Oct < 3u; ++Oct)
            {
                float Ph     = 0.76 * EvaluateCloudPhaseHG(CosTheta, G * OctG) +
                               0.24 * EvaluateCloudPhaseHG(CosTheta, -0.22 * OctG);
                float Att    = exp(-SunTau * 0.85 * OctExt);
                float Powder = 1.0 - 0.52 * exp(-D * 320.0 * OctExt - SunTau * 1.4);
                MultiScatter += OctAlbedo * Att * Powder * Ph;
                OctAlbedo *= 0.54;
                OctExt    *= 0.42;
                OctG      *= 0.68;
            }

            float SegT         = exp(-D * Dt);
            float Scatter      = (1.0 - SegT) * 0.96;
            float Alt          = length(P) - PlanetR;
            float H            = clamp((Alt - FloorM) / ThickM, 0.0, 1.0);
            float SkyAmb       = 0.38 + 0.62 * H;
            float GroundBounce = 0.14 * (1.0 - H) * clamp(SunDir.z * 2.5 + 0.30, 0.05, 1.0);

            float3 Iridescence = float3(1.0, 1.0, 1.0);
            float3 EmberGlow   = float3(0.0, 0.0, 0.0);
            if (FmtIdx == 10u)
            {
                float SpecPhase = CosTheta * 9.0 + H * 5.0 + (P.x + P.y) * 0.00020;
                Iridescence = float3(
                    0.98 + 0.28 * sin(SpecPhase),
                    0.95 + 0.28 * sin(SpecPhase + 2.094),
                    1.04 + 0.30 * sin(SpecPhase + 4.188));
            }
            else if (FmtIdx == 11u)
            {
                float CoreDense   = smoothstep(DensMul * 0.25, DensMul * 0.90, D);
                float EmberFactor = pow(1.0 - H, 2.8) * CoreDense * 1.25;
                EmberGlow = float3(1.55, 0.42, 0.06) * EmberFactor;
            }

            float DirectGain = MultiScatter * 2.35 + 0.06;
            DeckR += DeckT * (Cfg.SolarIrradianceExt.xyz * DirectGain * Iridescence +
                              Cfg.SkyColorWind.xyz * (SkyAmb * 0.85 + GroundBounce) +
                              EmberGlow) * Scatter;
            DeckT *= SegT;
            if (DeckT < 0.01) break;
        }
    }

    float LowAngleFade = smoothstep(0.008, 0.165, RayDir.z) * (1.0 - smoothstep(42000.0, 82000.0, EnterHit.y));
    float Haze = 1.0 - exp(-EnterHit.y * 0.000032);
    float3 HorizonTint = max(Cfg.SkyColorWind.xyz, float3(0.18, 0.25, 0.36) * (0.25 + 0.75 * max(0.0, SunDir.z)));
    DeckR = lerp(DeckR, HorizonTint * (1.0 - DeckT), Haze * 0.78) * LowAngleFade;
    DeckT = 1.0 - (1.0 - DeckT) * LowAngleFade;
    TotalRad += OutTransmittance * DeckR;
    OutTransmittance *= DeckT;
    return TotalRad;
}

#ifndef FRONTIER_ATMOSPHERE_EMBEDDED
[[vk::binding(0, 0)]] ConstantBuffer<VolumetricCloudUniformBlock> CloudUniforms;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>                 OutCloudRgba;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint W = max(1u, uint(CloudUniforms.LocalDensityPhase.z + 0.5));
    uint H = max(1u, uint(CloudUniforms.LocalDensityPhase.w + 0.5));
    if (DispatchThreadID.x >= W || DispatchThreadID.y >= H) return;

    float2 Uv = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(W), float(H));
    float Az  = (Uv.x - 0.5) * 6.2831853;
    float El  = (1.0 - Uv.y) * 1.5707963;
    float3 Rd = float3(cos(El) * cos(Az), cos(El) * sin(Az), sin(El));
    float  Trans = 1.0;
    float3 Rad   = EvaluateVolumetricCloudsGpu(CloudUniforms, Rd, float2(DispatchThreadID.xy), Trans);
    OutCloudRgba[DispatchThreadID.y * W + DispatchThreadID.x] = float4(Rad, Trans);
}
#endif
