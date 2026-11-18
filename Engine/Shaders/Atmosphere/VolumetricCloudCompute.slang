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

struct GpuCloudForceProbe
{
    float4 PosRadius;       // xyz = EvaluatedCentreMeters, w = RadiusMeters
    float4 ForwardStrength; // xyz = UnitForwardAxis, w = Strength
    float4 Params;          // x = ModelId, y = SwirlRatio, z = CoreRadiusM, w = TurbulenceScaleM
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
    float4 CloudPassControl;     // x = CloudPassW, y = CloudPassH, z = CloudMarchSteps (12..96), w = ForceWindEnabled (0/1)
    float4 WindProbeMeta;        // x = ForceProbeCount (0..8), y = CouplingScaleHeightM, z = ProjectToCloudDeck (0/1), w = Pad
    GpuLocalCloudVolume LocalClouds[8];
    GpuCloudForceProbe  WindProbes[8];
};

float HashCloudInt3(int IX, int IY, int IZ)
{
    uint N = uint(IX) * 374761393u + uint(IY) * 668265263u + uint(IZ) * 1442695041u;
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

float CloudFbmGpu3(float3 P, float HighOctaveFade, uint MaxOctaves = 5u)
{
    // Orthonormal 3D rotation between octaves eliminates lattice grid artifacts & crosshatching
    float Sum = 0.0;
    float Amp = 0.52;
    float3 Q  = P;
    uint Limit = min(5u, MaxOctaves);
    for (uint Oct = 0u; Oct < 5u; ++Oct)
    {
        if (Oct >= Limit)
        {
            Sum += Amp * 0.5;
        }
        else if (Oct >= 3u && HighOctaveFade <= 0.01)
        {
            Sum += Amp * 0.5;
        }
        else
        {
            float W = (Oct >= 3u) ? HighOctaveFade : 1.0;
            Sum += Amp * (W * LatticeNoiseCloud3(Q) + (1.0 - W) * 0.5);
        }
        Q = float3(
            (0.80 * Q.x - 0.60 * Q.y) * 2.04 + 11.3,
            (0.48 * Q.x + 0.64 * Q.y - 0.60 * Q.z) * 2.04 - 7.1,
            (0.36 * Q.x + 0.48 * Q.y + 0.80 * Q.z) * 2.04 + 5.7);
        Amp *= 0.48;
    }
    return Sum;
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
        float SubtleWarp    = CloudFbmGpu3(float3(WX * FScale * 0.58, WY * FScale * 0.58, WZ * FScale * 0.30), HighOctFade);
        float LaminarSlices = 0.5 + 0.5 * sin(H * CloudPiConst * 2.5 - 0.3 + (SubtleWarp - 0.5) * 0.85);
        float LensMask      = smoothstep(0.45, 0.76, MacroLobes + (SubtleWarp - 0.5) * 0.26);
        NNorm   = clamp(LensMask * (0.52 + 0.48 * HighOctFade * LaminarSlices), 0.0, 1.0);
        Profile = smoothstep(0.04, 0.24, H) * (1.0 - smoothstep(0.74, 0.96, H));
    }
    else if (FormationIndex == 7u) // Mammatus (3-axis hexagonal pouches, zero dot-matrix)
    {
        float BaseFbm  = CloudFbmGpu3(float3(WX * FScale * 0.95, WY * FScale * 0.95, WZ * FScale * 1.15), HighOctFade);
        float InvScale = 1.0 / max(1.0e-5, FScale);
        float WarpX    = WX + (BaseFbm - 0.5) * (1.2 * InvScale);
        float WarpY    = WY + (LatticeNoiseCloud3(float3(WX * FScale * 0.8 + 5.1, WY * FScale * 0.8, WZ * FScale)) - 0.5) * (1.2 * InvScale);
        float K        = FScale * 2.4;
        float Hex0     = 0.5 + 0.5 * cos(WarpX * K);
        float Hex1     = 0.5 + 0.5 * cos((-0.5 * WarpX + 0.866025 * WarpY) * K + 1.3);
        float Hex2     = 0.5 + 0.5 * cos((-0.5 * WarpX - 0.866025 * WarpY) * K - 0.9);
        float PouchLobe = pow((Hex0 + Hex1 + Hex2) * 0.3333, 1.35) * (0.28 + 0.72 * HighOctFade);
        float LowerMask = 1.0 - smoothstep(0.18, 0.68, H);
        NNorm    = clamp(((BaseFbm - 0.24) / 0.44) * 0.62 + PouchLobe * LowerMask * 0.54, 0.0, 1.0);
        ShapeCov = clamp(Cov + LowerMask * 0.14 + Anvil * H * 0.16, 0.0, 1.0);
    }
    else if (FormationIndex == 8u) // KelvinHelmholtz
    {
        float MacroBand = smoothstep(0.38, 0.72, LatticeNoiseCloud3(float3(WX * FScale * 0.28, WY * FScale * 0.28, 3.1)));
        float CurlSkew  = 280.0 * sin(H * CloudPiConst) + 190.0 * H * H;
        float CurlX     = WX + CurlSkew;
        float WavePhase = CurlX * FScale * 3.8 + CloudFbmGpu3(float3(WX * FScale * 0.85, WY * FScale * 0.85, 0.4), HighOctFade) * 2.6;
        float Crest     = 0.5 + 0.5 * sin(WavePhase + H * 3.0);
        float BreakCurl = smoothstep(0.28, 0.88, Crest) * smoothstep(0.12, 0.65, H + 0.24 * Crest);
        float Billow    = CloudFbmGpu3(float3(CurlX * FScale * 1.05, WY * FScale * 0.85, WZ * FScale * 1.4), HighOctFade);
        NNorm = clamp(MacroBand * (BreakCurl * (0.35 + 0.35 * HighOctFade) + ((Billow - 0.24) / 0.46) * 0.48), 0.0, 1.0);
    }
    else if (FormationIndex == 9u) // Asperitas (4-direction rotated waves, zero 2-dir crosshatch)
    {
        float Warp   = CloudFbmGpu3(float3(WX * FScale * 0.75, WY * FScale * 0.75, WZ * FScale), HighOctFade);
        float W1     = sin((WX * 0.96 + WY * 0.28) * FScale * 2.7 + Warp * 3.6);
        float W2     = cos((WX * 0.62 + WY * 0.78) * FScale * 3.1 - Warp * 2.9);
        float W3     = sin((-WX * 0.19 + WY * 0.98) * FScale * 2.3 + Warp * 2.4);
        float W4     = cos((-WX * 0.77 + WY * 0.64) * FScale * 2.9 - Warp * 3.1);
        float Swell  = 0.5 + 0.14 * (W1 + W2 + W3 + W4) * (0.28 + 0.72 * HighOctFade);
        float Detail = CloudFbmGpu3(float3(WX * FScale * 1.15, WY * FScale * 1.15, WZ * FScale * 1.3), HighOctFade);
        NNorm = clamp(((Detail - 0.24) / 0.46) * 0.66 + Swell * 0.42, 0.0, 1.0);
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

void EvaluateCloudForceProbesGpu(
    VolumetricCloudUniformBlock Cfg,
    float3                      WorldPos,
    float                       TimeSec,
    bool                        ProjectToDeck,
    out float3                  OutVel,
    out float                   OutCurl)
{
    OutVel  = float3(0.0, 0.0, 0.0);
    OutCurl = 0.0;
    uint ProbeCount = min(8u, uint(Cfg.WindProbeMeta.x + 0.5));
    if (ProbeCount == 0u || Cfg.CloudPassControl.w < 0.5) return;

    float ScaleH = max(250.0, Cfg.WindProbeMeta.y);
    for (uint I = 0u; I < ProbeCount; ++I)
    {
        GpuCloudForceProbe Pr = Cfg.WindProbes[I];
        float3 QueryPos = WorldPos;
        float  VertGain = 1.0;
        if (ProjectToDeck && Cfg.WindProbeMeta.z > 0.5)
        {
            float Dz = abs(WorldPos.z - Pr.PosRadius.z);
            float V01 = exp(-0.5 * (Dz / ScaleH) * (Dz / ScaleH));
            VertGain  = 0.30 + 0.70 * V01;
            QueryPos.z = Pr.PosRadius.z;
        }

        float  Radius = max(0.25, Pr.PosRadius.w);
        float3 D      = QueryPos - Pr.PosRadius.xyz;
        float  Dist   = length(D);
        float  T      = saturate(Dist / Radius);
        float  Atten  = 1.0 - smoothstep(0.0, 1.0, T);
        if (Atten <= 1.0e-4) continue;

        uint   ModelId  = uint(Pr.Params.x + 0.5);
        float  Strength = Pr.ForwardStrength.w * Atten * VertGain;
        float  CoreR    = max(0.25, Pr.Params.z);
        float  Swirl    = Pr.Params.y;
        float3 Fwd      = normalize(select(dot(Pr.ForwardStrength.xyz, Pr.ForwardStrength.xyz) > 1.0e-5, Pr.ForwardStrength.xyz, float3(1.0, 0.0, 0.0)));

        if (ModelId == 0u || ModelId == 11u || ModelId == 16u)
        {
            float Gust = (ModelId == 11u) ? (0.75 + 0.5 * sin(TimeSec * 2.2 + dot(QueryPos.xy, float2(0.18, -0.14)))) : 1.0;
            OutVel  += Fwd * (Strength * Gust);
            OutCurl += abs(Strength) * 0.04;
        }
        else if (ModelId == 2u || ModelId == 4u)
        {
            float3 Dir = (Dist > 1.0e-3) ? (D / Dist) : float3(0.0, 0.0, 1.0);
            OutVel  += Dir * Strength;
            OutCurl += abs(Strength) * 0.08 * (1.0 - T);
        }
        else if (ModelId == 3u || ModelId == 5u || ModelId == 6u)
        {
            float3 Dir = (Dist > 1.0e-3) ? (-D / Dist) : float3(0.0, 0.0, 0.0);
            OutVel  += Dir * Strength * (Dist / (Dist + CoreR));
            OutCurl += abs(Strength) * 0.06;
        }
        else if (ModelId == 7u || ModelId == 8u || ModelId == 12u)
        {
            float2 Dxy   = D.xy;
            float  Rxy   = max(0.08, length(Dxy));
            float2 TanXY = float2(-Dxy.y / Rxy, Dxy.x / Rxy);
            float  Prof  = (Rxy * CoreR) / (Rxy * Rxy + CoreR * CoreR) * 2.0;
            OutVel  += float3(TanXY.x * Strength * Prof, TanXY.y * Strength * Prof, Strength * Swirl * 0.28 * Prof);
            OutCurl += abs(Strength) * (0.18 + 0.25 * Prof);
        }
        else if (ModelId == 10u || ModelId == 13u)
        {
            float  TurbS = 1.0 / max(1.0, Pr.Params.w);
            float3 NP    = QueryPos * TurbS + float3(TimeSec * 0.25, -TimeSec * 0.18, TimeSec * 0.12);
            float3 CDir  = float3(
                LatticeNoiseCloud3(NP + float3(0.0, 3.1, 1.7)) - 0.5,
                LatticeNoiseCloud3(NP + float3(5.3, 0.0, 2.9)) - 0.5,
                LatticeNoiseCloud3(NP + float3(1.9, 4.7, 0.0)) - 0.5) * 2.0;
            OutVel  += CDir * Strength;
            OutCurl += abs(Strength) * 0.22;
        }
        else
        {
            OutVel  += Fwd * Strength;
            OutCurl += abs(Strength) * 0.08;
        }
    }
}

float3 AdvectCloudPointBackwardGpu(
    VolumetricCloudUniformBlock Cfg,
    float3                      WorldPos,
    float3                      BaseVel,
    float                       TimeSec,
    bool                        ProjectToDeck,
    out float                   OutCurl)
{
    OutCurl = 0.0;
    if (abs(TimeSec) < 1.0e-4) return WorldPos;

    float  Dt  = TimeSec * 0.25;
    float3 Pos = WorldPos;
    float  AccCurl = 0.0;
    for (uint I = 0u; I < 4u; ++I)
    {
        float  TNow = TimeSec - float(I) * Dt;
        float3 V1, V2;
        float  C1, C2;
        EvaluateCloudForceProbesGpu(Cfg, Pos, TNow, ProjectToDeck, V1, C1);
        float3 Mid = Pos - (BaseVel + V1) * (0.5 * Dt);
        EvaluateCloudForceProbesGpu(Cfg, Mid, TNow - 0.5 * Dt, ProjectToDeck, V2, C2);
        Pos -= (BaseVel + V2) * Dt;
        AccCurl += C2;
    }
    OutCurl = AccCurl * 0.25;
    return Pos;
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
                if (uint(Cfg.WindProbeMeta.x + 0.5) > 0u && Cfg.CloudPassControl.w > 0.5)
                {
                    float UnusedC = 0.0;
                    AdvP = AdvectCloudPointBackwardGpu(Cfg, P, Vol.WindVelCurl.xyz, TimeSec, false, UnusedC);
                }
                else
                {
                    AdvP = float3(P.x - Vol.WindVelCurl.x * TimeSec, P.y - Vol.WindVelCurl.y * TimeSec, P.z - Vol.WindVelCurl.z * TimeSec * 0.35);
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
                    uint ProbeCount = min(8u, uint(Cfg.WindProbeMeta.x + 0.5));
                    if (ProbeCount > 0u && Cfg.CloudPassControl.w > 0.5)
                    {
                        float UnusedCurl = 0.0;
                        AdvP = AdvectCloudPointBackwardGpu(Cfg, P, Vol.WindVelCurl.xyz, TimeSec, false, UnusedCurl);
                    }
                    else
                    {
                        AdvP = float3(
                            P.x - Vol.WindVelCurl.x * TimeSec,
                            P.y - Vol.WindVelCurl.y * TimeSec,
                            P.z - Vol.WindVelCurl.z * TimeSec * 0.35);
                        if (Vol.WindVelCurl.w > 1.0e-3)
                        {
                            float2 Dxy = P.xy - Center.xy;
                            float  SwirlA = clamp(Vol.WindVelCurl.w * 0.14, -2.5, 2.5) * sin(TimeSec * 0.8 + 0.06 * length(Dxy));
                            float  Cs = cos(SwirlA), Sn = sin(SwirlA);
                            AdvP.x = Center.x + (Dxy.x * Cs - Dxy.y * Sn) - Vol.WindVelCurl.x * TimeSec * 0.35;
                            AdvP.y = Center.y + (Dxy.x * Sn + Dxy.y * Cs) - Vol.WindVelCurl.y * TimeSec * 0.35;
                        }
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
    float Jitter = 0.25 + 0.50 * Ign;

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
    float OrigZ   = PlanetR + 100.0;

    float2 EnterHit = IntersectCloudShell(OrigZ, RayDir.z, PlanetR + FloorM);
    float2 ExitHit  = IntersectCloudShell(OrigZ, RayDir.z, PlanetR + FloorM + ThickM);
    float  MaxDist  = min(ExitHit.y, EnterHit.y + 32000.0);
    if (MaxDist <= EnterHit.y || EnterHit.y < 0.0 || EnterHit.y > 85000.0) return TotalRad;

    uint  MarchSteps = (StepCountOverride > 0u)
        ? clamp(StepCountOverride, 12u, 96u)
        : ((Cfg.CloudPassControl.z > 1.0) ? clamp(uint(Cfg.CloudPassControl.z + 0.5), 12u, 96u) : 64u);
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
    bool  HasProbes = (Cfg.SkyColorWind.w > 0.5) && (Cfg.CloudPassControl.w > 0.5) && (uint(Cfg.WindProbeMeta.x + 0.5) > 0u);

    static const float SunOffsets[4] = { 0.10, 0.30, 0.65, 1.30 };
    static const float SunWeights[4] = { 0.18, 0.28, 0.38, 0.56 };

    float  DeckT = 1.0;
    float3 DeckR = float3(0.0, 0.0, 0.0);
    for (uint S = 0u; S < MarchSteps; ++S)
    {
        float  T = EnterHit.y + (float(S) + Jitter) * Dt;
        float3 P = float3(RayDir.x * T, RayDir.y * T, OrigZ + RayDir.z * T);
        float3 EvalP = P;
        float  ExtraTurb = 0.0;
        if (HasProbes)
        {
            float AltNow = length(P) - PlanetR;
            float SwirlNow = 0.0;
            float3 Adv = AdvectCloudPointBackwardGpu(Cfg, float3(P.x, P.y, AltNow), float3(0.0, 0.0, 0.0), TimeSec, true, SwirlNow);
            EvalP.x = Adv.x;
            EvalP.y = Adv.y;
            ExtraTurb = clamp(SwirlNow * 0.25, 0.0, 1.25);
        }
        float  D = SampleCloudDeckDensityGpu(
            EvalP, PlanetR, FloorM, ThickM, Cov, Anvil, DensMul, FScale, WindX, WindY, Cfg.DeckOptics.w, FmtIdx, T, VeerRadKm, TurbAmp + ExtraTurb, TimeSec);
        if (D > 1.0e-6)
        {
            float SunTau = 0.0;
            if (Cfg.WindVectorOcclude.z > 0.5)
            {
                float ConeReach = ThickM * 0.42;
                for (uint K = 0u; K < 4u; ++K)
                {
                    float KS = SunOffsets[K] * ConeReach;
                    SunTau += SampleCloudDeckDensityGpu(
                        EvalP + SunDir * KS,
                        PlanetR, FloorM, ThickM, Cov, Anvil, DensMul, FScale, WindX, WindY, Cfg.DeckOptics.w, FmtIdx, 24000.0, VeerRadKm, 0.0, TimeSec) *
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
