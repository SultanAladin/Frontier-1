/*============================================================================================================================================
                                                      VOLUMETRICCLOUDCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Anti-Banded 64-Step IGN Raymarched 3D FBM Planetary Volumetric Clouds (12 Formations), Bounded LocalCloudVolume & 3-Octave Multi-Scattering

static const float CloudPiConst = 3.14159265359;

struct VolumetricCloudUniformBlock
{
    float4 OriginTimeVector;    // xyz = CameraOrigin, w = SimulationSeconds
    float4 SunAxisToggle;       // xyz = SunDirection, w = GlobalCloudsEnabled (0/1)
    float4 SolarIrradianceExt;  // xyz = SolarIrradiance, w = PlanetRadiusM
    float4 SkyColorWind;        // xyz = SkyLowAngleColor, w = WindFollow (0/1)
    float4 DeckGeometry;        // x = FloorM, y = ThickM, z = CloudFraction, w = DensityMul
    float4 DeckOptics;          // x = FeatureScale, y = Anvil, z = AnisotropyG, w = WindShearPerKm
    float4 WindVectorOcclude;   // x = WindVX, y = WindVY, z = ShadowEnabled (0/1), w = LocalEnabled (0/1)
    float4 LocalCenterExt;      // xyz = LocalCenterM, w = LocalFeatureScaleM
    float4 LocalExtentFraction; // xyz = LocalHalfExtentM, w = LocalFraction
    float4 LocalDensityPhase;   // x = LocalDensityMul, y = LocalAnisotropyG, z = ViewportW, w = ViewportH
    float4 FormationOptics;     // x = FormationIndex (0..11), y = Pad, z = Pad, w = Pad
};

float HashCloudInt3(int IX, int IY, int IZ)
{
    uint N = uint(IX * 374761393 + IY * 668265263 + IZ * 2147483647);
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
    float Sum  = 0.0;
    float Amp  = 0.52;
    float Freq = 1.0;
    for (uint Oct = 0u; Oct < 5u; ++Oct)
    {
        float W = (Oct >= 3u) ? HighOctaveFade : 1.0;
        Sum  += Amp * (W * LatticeNoiseCloud3(P * Freq) + (1.0 - W) * 0.5);
        Freq *= 2.05;
        Amp  *= 0.48;
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
    float  DistanceMeters)
{
    float Alt     = length(P) - PlanetR;
    float H       = clamp((Alt - FloorM) / max(1.0, ThickM), 0.0, 1.0);
    float Bot     = smoothstep(0.0, 0.22, H);
    float Top     = 1.0 - smoothstep(max(0.20, 0.72 - 0.24 * Anvil), 1.0, H);
    float Profile = Bot * Top;
    if (Profile <= 0.001) return 0.0;

    float HighOctFade = 1.0 - smoothstep(6500.0, 22000.0, DistanceMeters);
    float ShearShift  = ((Alt - FloorM) * 0.001) * ShearPerKm * 45.0;
    float WX = P.x + WindX + ShearShift;
    float WY = P.y + WindY;
    float WZ = Alt - FloorM;

    float ShapeCov = clamp(Cov + Anvil * H * H * 0.22, 0.0, 1.0);
    float NNorm    = 0.0;

    if (FormationIndex == 6u) // Lenticular
    {
        float MacroLobes    = LatticeNoiseCloud3(float3(WX * FScale * 0.22, WY * FScale * 0.55, 1.7));
        float SubtleWarp    = LatticeNoiseCloud3(float3(WX * FScale * 0.65, WY * FScale * 0.65, WZ * FScale * 0.25));
        float LaminarSlices = pow(0.5 + 0.5 * sin(H * CloudPiConst * 5.0 - 0.6), 1.35);
        float LensMask      = smoothstep(0.46, 0.76, MacroLobes + (SubtleWarp - 0.5) * 0.22);
        NNorm   = clamp(LensMask * (0.45 + 0.55 * LaminarSlices), 0.0, 1.0);
        Profile = smoothstep(0.04, 0.22, H) * (1.0 - smoothstep(0.75, 0.96, H));
    }
    else if (FormationIndex == 7u) // Mammatus
    {
        float BaseFbm    = CloudFbmGpu3(float3(WX * FScale * 0.95, WY * FScale * 0.95, WZ * FScale * 1.15), HighOctFade);
        float CellPhaseX = sin(WX * FScale * 3.8 + BaseFbm * 2.6);
        float CellPhaseY = sin(WY * FScale * 3.8 - BaseFbm * 2.6);
        float PouchLobe  = pow(abs(CellPhaseX * CellPhaseY), 0.65);
        float LowerMask  = 1.0 - smoothstep(0.18, 0.65, H);
        NNorm    = clamp(((BaseFbm - 0.25) / 0.44) * 0.55 + PouchLobe * LowerMask * 0.62, 0.0, 1.0);
        ShapeCov = clamp(Cov + LowerMask * 0.14 + Anvil * H * 0.16, 0.0, 1.0);
    }
    else if (FormationIndex == 8u) // KelvinHelmholtz
    {
        float MacroBand = smoothstep(0.40, 0.72, LatticeNoiseCloud3(float3(WX * FScale * 0.28, WY * FScale * 0.28, 3.1)));
        float CurlSkew  = 280.0 * sin(H * CloudPiConst) + 190.0 * H * H;
        float CurlX     = WX + CurlSkew;
        float WavePhase = CurlX * FScale * 4.4 + LatticeNoiseCloud3(float3(WX * FScale * 0.9, WY * FScale * 0.9, 0.4)) * 2.4;
        float Crest     = 0.5 + 0.5 * sin(WavePhase + H * 3.6);
        float BreakCurl = smoothstep(0.32, 0.86, Crest) * smoothstep(0.12, 0.65, H + 0.24 * Crest);
        float Billow    = CloudFbmGpu3(float3(CurlX * FScale * 1.05, WY * FScale * 0.85, WZ * FScale * 1.4), HighOctFade);
        NNorm = clamp(MacroBand * (BreakCurl * 0.68 + ((Billow - 0.26) / 0.46) * 0.42), 0.0, 1.0);
    }
    else if (FormationIndex == 9u) // Asperitas
    {
        float Warp   = CloudFbmGpu3(float3(WX * FScale * 0.75, WY * FScale * 0.75, WZ * FScale), HighOctFade);
        float W1     = sin(WX * FScale * 4.2 + WY * FScale * 1.6 + Warp * 3.2);
        float W2     = cos(WY * FScale * 4.6 - WX * FScale * 1.8 - Warp * 2.8);
        float Swell  = 0.5 + 0.25 * (W1 + W2);
        float Detail = CloudFbmGpu3(float3(WX * FScale * 1.15, WY * FScale * 1.15, WZ * FScale * 1.3), HighOctFade);
        NNorm = clamp(((Detail - 0.24) / 0.46) * 0.62 + Swell * 0.46, 0.0, 1.0);
    }
    else if (FormationIndex == 10u) // Nacreous
    {
        float MacroEnv = smoothstep(0.36, 0.72, LatticeNoiseCloud3(float3(WX * FScale * 0.35, WY * FScale * 0.35, 2.3)));
        float Ribbon   = 0.5 + 0.5 * sin((WX * 0.42 + WY * 0.88) * FScale * 3.2 +
                                         LatticeNoiseCloud3(float3(WX * FScale * 0.8, WY * FScale * 0.8, 0.5)) * 3.5);
        float Silk     = LatticeNoiseCloud3(float3(WX * FScale * 1.2, WY * FScale * 1.2, WZ * FScale));
        NNorm = clamp(MacroEnv * (Ribbon * 0.68 + Silk * 0.38), 0.0, 1.0);
    }
    else if (FormationIndex == 11u) // Pyrocumulus
    {
        float PlumeColumn = LatticeNoiseCloud3(float3(WX * FScale * 0.36, WY * FScale * 0.36, 4.7));
        float PlumeMask   = smoothstep(0.46, 0.74, PlumeColumn + H * 0.10);
        float Base        = CloudFbmGpu3(float3(WX * FScale * 0.90, WY * FScale * 0.90, WZ * FScale * 1.15), HighOctFade);
        float BillowRidge = 1.0 - abs(LatticeNoiseCloud3(float3(WX * FScale * 2.1, WY * FScale * 2.1, WZ * FScale * 2.3)) * 2.0 - 1.0);
        NNorm    = clamp(PlumeMask * (((Base - 0.20) / 0.44) * 0.68 + BillowRidge * 0.38), 0.0, 1.0);
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

float3 EvaluateVolumetricCloudsGpu(
    VolumetricCloudUniformBlock Cfg,
    float3                      RayDir,
    float2                      PixelCoord,
    out float                   OutTransmittance)
{
    OutTransmittance = 1.0;
    float3 TotalRad  = float3(0.0, 0.0, 0.0);

    float Ign    = frac(52.9829189 * frac(dot(PixelCoord, float2(0.06711056, 0.00583715))));
    float Jitter = 0.5 + 0.72 * (Ign - 0.5);

    float3 SunDir = normalize(Cfg.SunAxisToggle.xyz);
    float  WindX  = (Cfg.SkyColorWind.w > 0.5) ? (Cfg.WindVectorOcclude.x * Cfg.OriginTimeVector.w) : 0.0;
    float  WindY  = (Cfg.SkyColorWind.w > 0.5) ? (Cfg.WindVectorOcclude.y * Cfg.OriginTimeVector.w) : 0.0;

    // 1. Optional Bounded Local Cloud Box (24 jittered steps)
    if (Cfg.WindVectorOcclude.w > 0.5 && Cfg.LocalExtentFraction.w > 0.01 && Cfg.LocalDensityPhase.x > 0.01)
    {
        float3 BoxMin = Cfg.LocalCenterExt.xyz - Cfg.LocalExtentFraction.xyz;
        float3 BoxMax = Cfg.LocalCenterExt.xyz + Cfg.LocalExtentFraction.xyz;
        float3 InvD   = 1.0 / select(abs(RayDir) > float3(1.0e-6), RayDir, float3(1.0e-6));
        float3 T0     = (BoxMin - Cfg.OriginTimeVector.xyz) * InvD;
        float3 T1     = (BoxMax - Cfg.OriginTimeVector.xyz) * InvD;
        float3 TN     = min(T0, T1);
        float3 TF     = max(T0, T1);
        float  TEnter = max(0.05, max(TN.x, max(TN.y, TN.z)));
        float  TExit  = min(TF.x, min(TF.y, TF.z));
        if (TExit > TEnter)
        {
            float Dt       = (TExit - TEnter) / 24.0;
            float CosTheta = dot(RayDir, SunDir);
            float Phase    = 0.76 * EvaluateCloudPhaseHG(CosTheta, Cfg.LocalDensityPhase.y) +
                             0.24 * EvaluateCloudPhaseHG(CosTheta, -0.22);
            float LScale   = 1.0 / max(4.0, Cfg.LocalCenterExt.w);
            for (uint S = 0u; S < 24u; ++S)
            {
                float  T = TEnter + (float(S) + Jitter) * Dt;
                float3 P = Cfg.OriginTimeVector.xyz + RayDir * T;
                float3 Rel = abs(P - Cfg.LocalCenterExt.xyz) / max(float3(0.1), Cfg.LocalExtentFraction.xyz);
                float EdgeF = 1.0 - smoothstep(0.65, 1.0, max(Rel.x, max(Rel.y, Rel.z)));
                float N     = CloudFbmGpu3(float3((P.x + WindX) * LScale, (P.y + WindY) * LScale, P.z * LScale), 1.0);
                float Thresh = 1.0 - Cfg.LocalExtentFraction.w;
                float Den   = max(0.0, (N - Thresh) / max(0.15, Cfg.LocalExtentFraction.w)) *
                              EdgeF * Cfg.LocalDensityPhase.x * 0.028;
                if (Den > 1.0e-5)
                {
                    float SegT    = exp(-Den * Dt);
                    float Scatter = 1.0 - SegT;
                    float Amb     = 0.35 + 0.55 * clamp((P.z - BoxMin.z) / max(1.0, BoxMax.z - BoxMin.z), 0.0, 1.0);
                    TotalRad += OutTransmittance * (Cfg.SolarIrradianceExt.xyz * Phase * 2.2 + Cfg.SkyColorWind.xyz * Amb * 0.65) * Scatter;
                    OutTransmittance *= SegT;
                    if (OutTransmittance < 0.01) break;
                }
            }
        }
    }

    // 2. Global Planetary Cloud Shell (64 jittered steps + LOD FBM + 3-octave multi-scattering + Beer-Powder)
    if (Cfg.SunAxisToggle.w < 0.5 || RayDir.z <= 0.008 || Cfg.DeckGeometry.z <= 0.01 || Cfg.DeckGeometry.w <= 0.01)
    {
        return TotalRad;
    }

    float PlanetR = max(500000.0, Cfg.SolarIrradianceExt.w);
    float FloorM  = max(100.0, Cfg.DeckGeometry.x);
    float ThickM  = max(100.0, Cfg.DeckGeometry.y);
    float OrigZ   = PlanetR + 100.0;

    float2 EnterHit = IntersectCloudShell(OrigZ, RayDir.z, PlanetR + FloorM);
    float2 ExitHit  = IntersectCloudShell(OrigZ, RayDir.z, PlanetR + FloorM + ThickM);
    float  MaxDist  = min(ExitHit.y, EnterHit.y + 28000.0);
    if (MaxDist <= EnterHit.y || EnterHit.y < 0.0 || EnterHit.y > 65000.0) return TotalRad;

    float Dt       = (MaxDist - EnterHit.y) / 64.0;
    float FScale   = 0.00065 / max(0.08, Cfg.DeckOptics.x);
    float Cov      = clamp(Cfg.DeckGeometry.z, 0.0, 1.0);
    float DensMul  = max(0.0, Cfg.DeckGeometry.w) * 0.0085;
    float Anvil    = clamp(Cfg.DeckOptics.y, 0.0, 1.0);
    float G        = clamp(Cfg.DeckOptics.z, -0.5, 0.9);
    float CosTheta = dot(RayDir, SunDir);
    uint  FmtIdx   = uint(Cfg.FormationOptics.x + 0.5);

    static const float SunOffsets[5] = { 0.08, 0.22, 0.44, 0.78, 1.35 };
    static const float SunWeights[5] = { 0.14, 0.22, 0.28, 0.36, 0.50 };

    float  DeckT = 1.0;
    float3 DeckR = float3(0.0, 0.0, 0.0);
    for (uint S = 0u; S < 64u; ++S)
    {
        float  T = EnterHit.y + (float(S) + Jitter) * Dt;
        float3 P = float3(RayDir.x * T, RayDir.y * T, OrigZ + RayDir.z * T);
        float  D = SampleCloudDeckDensityGpu(
            P, PlanetR, FloorM, ThickM, Cov, Anvil, DensMul, FScale, WindX, WindY, Cfg.DeckOptics.w, FmtIdx, T);
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
                        P + SunDir * KS,
                        PlanetR, FloorM, ThickM, Cov, Anvil, DensMul, FScale, WindX, WindY, Cfg.DeckOptics.w, FmtIdx, T) *
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

    float LowAngleFade = smoothstep(0.015, 0.145, RayDir.z);
    float Haze = 1.0 - exp(-EnterHit.y * 0.000042);
    DeckR = lerp(DeckR, Cfg.SkyColorWind.xyz * (1.0 - DeckT), Haze) * LowAngleFade;
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
