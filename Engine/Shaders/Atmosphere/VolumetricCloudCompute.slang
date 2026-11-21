/*============================================================================================================================================
                                                      VOLUMETRICCLOUDCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Single-source PlanetaryClouds bridge (CloudNoise/Density/Transport/AerialPerspective) + Multi-Shape LocalCloudVolumes

#ifndef FRONTIER_ATMOSPHERE_EMBEDDED
[[vk::binding(2, 0)]] StructuredBuffer<uint> PlanetaryCloudNoise;
#endif
#include "CloudAerialPerspective.slang"

static const float CloudPiConst = 3.14159265359;

struct GpuLocalCloudVolume
{
    float4 CenterShape;          // xyz = CenterWorld, w = Shape (0..6)
    float4 HalfExtentYaw;        // xyz = HalfExtentM, w = YawRad
    float4 DensityCovScaleSides; // x = Density, y = Coverage, z = InvFeatureScale, w = float(PrismSides)
    float4 WindVelCurl;          // xyz = ResolvedWindVelMps, w = CurlScalar
    float4 SoftAnisoPad;         // x = EdgeSoftness, y = AnisotropyG, z = FollowWind (0/1), w = Pad
};

struct VolumetricCloudUniformBlock
{
    float4 OriginTimeVector;     // xyz = CameraOrigin, w = SimulationSeconds
    float4 SunAxisToggle;        // xyz = SunDirection, w = GlobalCloudsEnabled (0/1)
    float4 SolarIrradianceExt;   // xyz = Ground SolarIrradiance, w = PlanetRadiusM
    float4 SkyColorWind;         // xyz = SkyLowAngleColor, w = WindFollow (0/1)
    float4 DeckGeometry;         // x = FloorM, y = ThickM, z = CloudFraction, w = DensityMul
    float4 DeckOptics;           // x = FeatureScale, y = Anvil, z = AnisotropyG, w = WindShearPerKm
    float4 WindVectorOcclude;    // x = WindVX, y = WindVY, z = ShadowEnabled (0/1), w = LocalEnabled (0/1)
    float4 LocalCenterExt;       // xyz = LocalCenterM, w = LocalFeatureScaleM
    float4 LocalExtentFraction;  // xyz = LocalHalfExtentM, w = LocalFraction
    float4 LocalDensityPhase;    // x = LocalDensityMul, y = LocalAnisotropyG, z = ViewportW, w = ViewportH
    float4 FormationOptics;      // x = FormationIndex (0..11), y = VeerRadPerKm, z = WindTurbulence, w = LocalCloudCount (0..8)
    float4 CloudShadowOptics;    // x = ShadowFraction, y = ShadowDensityMul, z = ShadowScale, w = ShadowAnvil
    float4 CloudPassControl;     // x = CloudPassW, y = CloudPassH, z = CloudMarchSteps, w = TanHalfFovY
    float4 WindForceMeta;        // x = CouplingScaleHeightM, y = GlobalWindVX, z = GlobalWindVY, w = ObserverAltitudeM
    float4 CloudToaSolarIrrad;   // xyz = TOA SolarIrradiance for PlanetaryClouds, w = Seed
    float4 CloudSkyAmbient;      // xyz = SkyAmbientRGB, w = Reserved
    float4 CloudAtmoStrengths;   // x = RayMul, y = MieMul, z = OzoneMul, w = AtmoThicknessMeters
    float4 CloudAirScalesCyclone;// x = RayScaleH, y = MieScaleH, z = CycloneSpeedMps, w = CycloneEyeM
    GpuLocalCloudVolume LocalClouds[8];
};

CloudSpecification BuildCloudSpecificationFromUniformGpu(
    VolumetricCloudUniformBlock C,
    bool                        ShadowOnly,
    uint                        FormationOverride = 0xFFFFFFFFu)
{
    CloudSpecification Spec;
    Spec.RadiusM             = max(500000.0, C.SolarIrradianceExt.w);
    Spec.Coverage            = clamp(C.DeckGeometry.z * (ShadowOnly ? C.CloudShadowOptics.x : 1.0), 0.0, 1.0);
    Spec.Extinction          = max(0.0, C.DeckGeometry.w * (ShadowOnly ? C.CloudShadowOptics.y : 1.0));
    Spec.FloorM              = max(100.0, C.DeckGeometry.x);
    Spec.ThicknessM          = max(100.0, C.DeckGeometry.y);
    Spec.FeatureScale        = max(0.08, C.DeckOptics.x) * (ShadowOnly ? max(0.1, C.CloudShadowOptics.z) : 1.0);
    Spec.Anvil               = clamp(C.DeckOptics.y * (ShadowOnly ? C.CloudShadowOptics.w : 1.0), 0.0, 1.0);
    Spec.PhaseG              = clamp(C.DeckOptics.z, -0.5, 0.92);
    Spec.Seed                = C.CloudToaSolarIrrad.w;
    float TanY               = (C.CloudPassControl.w > 1.0e-4) ? C.CloudPassControl.w : 0.55;
    float ViewportH          = max(64.0, C.LocalDensityPhase.w);
    Spec.PixelAngle          = (2.0 * TanY) / ViewportH;
    Spec.UniformExtinction   = -1.0;
    Spec.TimeSeconds         = (C.SkyColorWind.w > 0.5) ? C.OriginTimeVector.w : 0.0;
    Spec.WindVelocityMps     = float2(C.WindVectorOcclude.x, C.WindVectorOcclude.y);
    Spec.Shear               = C.DeckOptics.w;
    Spec.Veer                = C.FormationOptics.y;
    Spec.Turbulence          = C.FormationOptics.z;
    Spec.CycloneSpeedMps     = C.CloudAirScalesCyclone.z;
    Spec.CycloneEyeM         = max(10000.0, C.CloudAirScalesCyclone.w);
    Spec.CycloneAxis         = normalize(float3(0.05, 0.14, 0.988));
    Spec.SunDirection        = normalize(C.SunAxisToggle.xyz);
    Spec.SolarIrradiance     = C.CloudToaSolarIrrad.xyz;
    Spec.SkyAmbient          = C.CloudSkyAmbient.xyz;
    Spec.AtmosphereStrengths = C.CloudAtmoStrengths.xyz;
    Spec.AirScaleHeightsM    = float2(max(400.0, C.CloudAirScalesCyclone.x), max(200.0, C.CloudAirScalesCyclone.y));
    Spec.Steps               = clamp(uint(C.CloudPassControl.z + 0.5), 16u, 128u);
    Spec.LightSteps          = clamp(uint(C.CloudSkyAmbient.w + 0.5), 3u, 16u);
    Spec.Formation           = (FormationOverride != 0xFFFFFFFFu) ? FormationOverride : uint(C.FormationOptics.x + 0.5);
    Spec.UseClouds           = (C.SunAxisToggle.w >= 0.5);
    Spec.UseCloudShadows     = (C.SunAxisToggle.w >= 0.5) && (C.WindVectorOcclude.z >= 0.5);
    return Spec;
}

float3 ResolvePlanetaryViewOriginGpu(VolumetricCloudUniformBlock C, float3 ScenePos)
{
    float PlanetR = max(500000.0, C.SolarIrradianceExt.w);
    float ObsAlt  = max(5.0, C.WindForceMeta.w);
    return float3(ScenePos.x, ScenePos.y, PlanetR + ObsAlt + ScenePos.z);
}

float EvaluateCloudPhaseHG(float CosTheta, float G)
{
    float G2  = G * G;
    float Den = max(1.0e-3, 1.0 + G2 - 2.0 * G * CosTheta);
    return (1.0 - G2) / (4.0 * CloudPiConst * pow(Den, 1.5));
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
    float3 H = max(float3(0.1, 0.1, 0.1), HalfExt);
    float3 D = P - Center;
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
    else
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

float SampleLocalCloudFieldGpu(float3 AdvP, float LScale)
{
    float PeriodM = max(12.0, 2.5 / max(1.0e-4, LScale));
    float3 Coord  = AdvP / PeriodM + float3(0.37, 0.61, 0.19);
    float2 S = SampleCloudShape(Coord, 1.5, PeriodM).rg;
    float  E = SampleCloudErosion(Coord * 1.9 + float3(0.23, 0.51, 0.79), 1.5, PeriodM * 0.5).r;
    return clamp(0.62 * S.x + 0.24 * S.y + 0.14 * E, 0.0, 1.0);
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
            float3 AdvP = DoWind
                ? float3(P.x - Vol.WindVelCurl.x * TimeSec, P.y - Vol.WindVelCurl.y * TimeSec, P.z - Vol.WindVelCurl.z * TimeSec * 0.35)
                : P;
            float N   = SampleLocalCloudFieldGpu(AdvP, LScale);
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

    float Jitter   = CloudBlueJitter(PixelCoord);
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

                float3 AdvP = DoWind
                    ? float3(P.x - Vol.WindVelCurl.x * TimeSec, P.y - Vol.WindVelCurl.y * TimeSec, P.z - Vol.WindVelCurl.z * TimeSec * 0.35)
                    : P;

                float N   = SampleLocalCloudFieldGpu(AdvP, LScale);
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
                float3 Rel   = abs(P - Cfg.LocalCenterExt.xyz) / max(float3(0.1, 0.1, 0.1), Cfg.LocalExtentFraction.xyz);
                float  EdgeF = 1.0 - smoothstep(0.65, 1.0, max(Rel.x, max(Rel.y, Rel.z)));
                float  N     = SampleLocalCloudFieldGpu(float3(P.x - WindX, P.y - WindY, P.z), LScale);
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

    if (IncludeLocalClouds)
    {
        TotalRad += EvaluateLocalCloudsSegmentGpu(
            Cfg, Cfg.OriginTimeVector.xyz, RayDir, 60000.0, PixelCoord, OutTransmittance);
    }

    if (Cfg.SunAxisToggle.w < 0.5 || Cfg.DeckGeometry.z <= 0.01 || Cfg.DeckGeometry.w <= 0.01)
    {
        return TotalRad;
    }

    CloudSpecification Spec = BuildCloudSpecificationFromUniformGpu(Cfg, false);
    if (StepCountOverride > 0u) Spec.Steps = clamp(StepCountOverride, 16u, 256u);

    float3 Origin  = ResolvePlanetaryViewOriginGpu(Cfg, Cfg.OriginTimeVector.xyz);
    float  GroundM = PlanetCloudGroundDistance(Origin, RayDir, Spec.RadiusM);
    CloudTransportIntegral Cloud = TracePlanetaryClouds(Spec, Origin, RayDir, PixelCoord, GroundM);
    if (Cloud.Transmittance < 0.999999)
    {
        CloudAirSegment Front = IntegrateCloudAir(Spec, Origin, RayDir, Cloud.DistanceM, 12u, false);
        float3 DeckRad = Front.Transmittance * Cloud.Radiance + (1.0 - Cloud.Transmittance) * Front.Radiance;
        TotalRad += OutTransmittance * DeckRad;
        OutTransmittance *= Cloud.Transmittance;
    }
    return TotalRad;
}

float3 EvaluatePlanetaryCloudsOnlyGpu(
    VolumetricCloudUniformBlock Cfg,
    float3                      RayDir,
    float2                      PixelCoord,
    out float                   OutTransmittance)
{
    OutTransmittance = 1.0;
    if (Cfg.SunAxisToggle.w < 0.5 || Cfg.DeckGeometry.z <= 0.01 || Cfg.DeckGeometry.w <= 0.01)
    {
        return float3(0.0, 0.0, 0.0);
    }
    CloudSpecification Spec = BuildCloudSpecificationFromUniformGpu(Cfg, false);
    float3 Origin  = ResolvePlanetaryViewOriginGpu(Cfg, Cfg.OriginTimeVector.xyz);
    float  GroundM = PlanetCloudGroundDistance(Origin, RayDir, Spec.RadiusM);
    CloudTransportIntegral Cloud = TracePlanetaryClouds(Spec, Origin, RayDir, PixelCoord, GroundM);
    if (Cloud.Transmittance < 0.999999)
    {
        CloudAirSegment Front = IntegrateCloudAir(Spec, Origin, RayDir, Cloud.DistanceM, 8u, false);
        OutTransmittance = Cloud.Transmittance;
        return Front.Transmittance * Cloud.Radiance + (1.0 - Cloud.Transmittance) * Front.Radiance;
    }
    return float3(0.0, 0.0, 0.0);
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
    float  Az = (Uv.x - 0.5) * 6.2831853;
    float  El = (1.0 - Uv.y) * 1.5707963;
    float3 Rd = float3(cos(El) * cos(Az), cos(El) * sin(Az), sin(El));
    float  Trans = 1.0;
    float3 Rad   = EvaluateVolumetricCloudsGpu(CloudUniforms, Rd, float2(DispatchThreadID.xy), Trans);
    OutCloudRgba[DispatchThreadID.y * W + DispatchThreadID.x] = float4(Rad, Trans);
}
#endif
