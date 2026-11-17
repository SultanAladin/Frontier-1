/*============================================================================================================================================
                                                      VOLUMETRICCLOUDCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Raymarched 3D Value-Noise FBM Planetary Volumetric Clouds, Bounded LocalCloudVolume & Beer-Powder Scattering

static const float CloudPiConst = 3.14159265359;

struct VolumetricCloudUniformBlock
{
    float4 OriginTimeVector;       // xyz = CameraOrigin, w = SimulationSeconds
    float4 SunAxisToggle;    // xyz = SunDirection, w = GlobalCloudsEnabled (0/1)
    float4 SolarIrradianceExt;  // xyz = SolarIrradiance, w = PlanetRadiusM
    float4 SkyColorWind;         // xyz = SkyLowAngleColor, w = WindFollow (0/1)
    float4 DeckGeometry;        // x = FloorM, y = ThickM, z = CloudFraction, w = DensityMul
    float4 DeckOptics;          // x = FeatureScale, y = Anvil, z = AnisotropyG, w = WindShearPerKm
    float4 WindVectorOcclude;    // x = WindVX, y = WindVY, z = ShadowEnabled (0/1), w = LocalEnabled (0/1)
    float4 LocalCenterExt;      // xyz = LocalCenterM, w = LocalFeatureScaleM
    float4 LocalExtentFraction;  // xyz = LocalHalfExtentM, w = LocalFraction
    float4 LocalDensityPhase;   // x = LocalDensityMul, y = LocalAnisotropyG, z = ViewportW, w = ViewportH
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

float CloudFbmGpu3(float3 P)
{
    float Sum  = 0.0;
    float Amp  = 0.52;
    float Freq = 1.0;
    for (uint Oct = 0u; Oct < 4u; ++Oct)
    {
        Sum  += Amp * LatticeNoiseCloud3(P * Freq);
        Freq *= 2.07;
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
    float C    = OriginZ * OriginZ - Radius * Radius;
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
    float  ShearPerKm)
{
    float Alt     = length(P) - PlanetR;
    float H       = clamp((Alt - FloorM) / max(1.0, ThickM), 0.0, 1.0);
    float Bot     = clamp(H / 0.16, 0.0, 1.0);
    float Top     = clamp((1.0 - H) / max(0.05, 0.28 - 0.16 * Anvil), 0.0, 1.0);
    float Profile = Bot * Top;
    if (Profile <= 0.001) return 0.0;

    float ShearShift = ((Alt - FloorM) * 0.001) * ShearPerKm * 45.0;
    float ShapeCov   = clamp(Cov + Anvil * H * H * 0.25, 0.0, 1.0);
    float N          = CloudFbmGpu3(float3((P.x + WindX + ShearShift) * FScale, (P.y + WindY) * FScale, (Alt - FloorM) * FScale * 1.4));
    float NNorm      = clamp((N - 0.28) / 0.44, 0.0, 1.0);
    float Thresh     = 1.0 - ShapeCov;
    return max(0.0, (NNorm - Thresh) / max(0.18, ShapeCov)) * Profile * DensMul;
}

float3 EvaluateVolumetricCloudsGpu(
    VolumetricCloudUniformBlock Cfg,
    float3                      RayDir,
    out float                   OutTransmittance)
{
    OutTransmittance = 1.0;
    float3 TotalRad  = float3(0.0, 0.0, 0.0);

    float3 SunDir = normalize(Cfg.SunAxisToggle.xyz);
    float  WindX  = (Cfg.SkyColorWind.w > 0.5) ? (Cfg.WindVectorOcclude.x * Cfg.OriginTimeVector.w) : 0.0;
    float  WindY  = (Cfg.SkyColorWind.w > 0.5) ? (Cfg.WindVectorOcclude.y * Cfg.OriginTimeVector.w) : 0.0;

    // 1. Optional Bounded Local Cloud Box
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
            float Dt       = (TExit - TEnter) / 12.0;
            float CosTheta = dot(RayDir, SunDir);
            float Phase    = 0.78 * EvaluateCloudPhaseHG(CosTheta, Cfg.LocalDensityPhase.y) +
                             0.22 * EvaluateCloudPhaseHG(CosTheta, -0.25);
            float LScale   = 1.0 / max(4.0, Cfg.LocalCenterExt.w);
            for (uint S = 0u; S < 12u; ++S)
            {
                float  T = TEnter + (float(S) + 0.5) * Dt;
                float3 P = Cfg.OriginTimeVector.xyz + RayDir * T;
                float3 Rel = abs(P - Cfg.LocalCenterExt.xyz) / max(float3(0.1), Cfg.LocalExtentFraction.xyz);
                float EdgeF = clamp(1.0 - max(Rel.x, max(Rel.y, Rel.z)), 0.0, 1.0);
                float N     = CloudFbmGpu3(float3((P.x + WindX) * LScale, (P.y + WindY) * LScale, P.z * LScale));
                float Thresh = 1.0 - Cfg.LocalExtentFraction.w;
                float Den   = max(0.0, (N - Thresh) / max(0.15, Cfg.LocalExtentFraction.w)) *
                              EdgeF * Cfg.LocalDensityPhase.x * 0.025;
                if (Den > 1.0e-5)
                {
                    float SegT    = exp(-Den * Dt);
                    float Scatter = 1.0 - SegT;
                    float Amb     = 0.35 + 0.45 * clamp((P.z - BoxMin.z) / max(1.0, BoxMax.z - BoxMin.z), 0.0, 1.0);
                    TotalRad += OutTransmittance * (Cfg.SolarIrradianceExt.xyz * Phase * 2.1 + Cfg.SkyColorWind.xyz * Amb * 0.55) * Scatter;
                    OutTransmittance *= SegT;
                }
            }
        }
    }

    // 2. Global Planetary Cloud Shell
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
    float  MaxDist  = min(ExitHit.y, 48000.0);
    if (MaxDist <= EnterHit.y || EnterHit.y < 0.0) return TotalRad;

    float Dt       = (MaxDist - EnterHit.y) / 16.0;
    float FScale   = 0.00065 / max(0.08, Cfg.DeckOptics.x);
    float Cov      = clamp(Cfg.DeckGeometry.z, 0.0, 1.0);
    float DensMul  = max(0.0, Cfg.DeckGeometry.w) * 0.008;
    float Anvil    = clamp(Cfg.DeckOptics.y, 0.0, 1.0);
    float G        = clamp(Cfg.DeckOptics.z, -0.5, 0.9);
    float CosTheta = dot(RayDir, SunDir);
    float Phase    = 0.78 * EvaluateCloudPhaseHG(CosTheta, G) + 0.22 * EvaluateCloudPhaseHG(CosTheta, -0.25);

    float  DeckT = 1.0;
    float3 DeckR = float3(0.0, 0.0, 0.0);
    for (uint S = 0u; S < 16u; ++S)
    {
        float  T = EnterHit.y + (float(S) + 0.5) * Dt;
        float3 P = float3(RayDir.x * T, RayDir.y * T, OrigZ + RayDir.z * T);
        float  D = SampleCloudDeckDensityGpu(P, PlanetR, FloorM, ThickM, Cov, Anvil, DensMul, FScale, WindX, WindY, Cfg.DeckOptics.w);
        if (D > 1.0e-6)
        {
            float SunTau = 0.0;
            if (Cfg.WindVectorOcclude.z > 0.5)
            {
                float SStep = (ThickM * 0.55) / 3.0;
                for (uint K = 1u; K <= 3u; ++K)
                {
                    SunTau += SampleCloudDeckDensityGpu(
                        P + SunDir * (float(K) * SStep),
                        PlanetR, FloorM, ThickM, Cov, Anvil, DensMul, FScale, WindX, WindY, Cfg.DeckOptics.w) * SStep;
                }
            }
            float SunAtt  = (0.28 + 0.72 * exp(-SunTau * 0.65)) *
                            (0.45 + 0.55 * (1.0 - exp(-SunTau * 2.2 + 0.25)));
            float SegT    = exp(-D * Dt);
            float Scatter = 1.0 - SegT;
            float Alt     = length(P) - PlanetR;
            float H       = clamp((Alt - FloorM) / ThickM, 0.0, 1.0);
            float Amb     = 0.45 + 0.55 * H;

            DeckR += DeckT * (Cfg.SolarIrradianceExt.xyz * (SunAtt * Phase * 2.4 + 0.08) + Cfg.SkyColorWind.xyz * Amb * 0.85) * Scatter;
            DeckT *= SegT;
            if (DeckT < 0.015) break;
        }
    }

    float LowAngleFade = smoothstep(0.008, 0.118, RayDir.z);
    float Haze = 1.0 - exp(-EnterHit.y * 0.000065);
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
    float3 Rad   = EvaluateVolumetricCloudsGpu(CloudUniforms, Rd, Trans);
    OutCloudRgba[DispatchThreadID.y * W + DispatchThreadID.x] = float4(Rad, Trans);
}
#endif
