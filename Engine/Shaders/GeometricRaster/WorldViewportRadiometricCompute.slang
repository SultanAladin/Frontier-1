/*============================================================================================================================================
                                                    WORLDVIEWPORTRADIOMETRICCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 6-Engine Unified GPU Compute Shader:
// 0=MatCap, 1=OpenPbrDirect, 2=RealTimeGiGtxLowEnd, 3=RealTimeGiHighEnd, 4=PathTracerGameRealtime, 5=PathTracerSceneFinalRender

#define FRONTIER_ATMOSPHERE_EMBEDDED 1
[[vk::binding(20, 0)]] StructuredBuffer<uint> PlanetaryCloudNoise;
#include "../Atmosphere/SkyViewTransmittanceCompute.slang"
#include "../Atmosphere/VolumetricCloudCompute.slang"
#include "../Atmosphere/VolumetricFogCompute.slang"
#include "../Atmosphere/CelestialDiscStarCompute.slang"
#include "../Atmosphere/LensFlareCompute.slang"
[[vk::binding(21, 0)]] StructuredBuffer<GpuRawCatalogStar> RawCatalogStars;

struct GpuAreaQuadLight
{
    float4 CenterHalfWidth; // xyz = CenterWorld, w = HalfWidthMeters
    float4 RightHalfHeight; // xyz = RightTangent, w = HalfHeightMeters
    float4 UpTwoSided;      // xyz = UpBitangent, w = TwoSidedRadiance (1.0 / 0.0)
    float4 RadiancePacked;  // xyz = EmittedRadiance, w = Pad
};

struct GpuGlassSphere
{
    float4 CenterRadiusPacked; // xyz = CenterWorld, w = RadiusMeters
    float4 OpticsSlotPacked;   // x = DielectricIOR, y = AbbeDispersion, z = MaterialSlot, w = Roughness
};

struct WorldViewportUniformBlock
{
    float4 CameraOriginTanX;         // xyz = CameraOrigin, w = TanHalfFovX
    float4 CameraRightTanY;          // xyz = CameraRight,  w = TanHalfFovY
    float4 CameraUpWidth;            // xyz = CameraUp,     w = ViewportWidth
    float4 CameraForwardHeight;      // xyz = CameraForward,w = ViewportHeight
    float4 SunDirConeTan;            // xyz = SunDirection, w = TanHalfSolarCone
    float4 SolarIrradiancePad;       // xyz = SolarIrradiance, w = Reserved
    float4 SkyRadianceBvhCount;      // xyz = SkyRadiance,  w = BvhEntryCount
    uint4  LightGlassMaterialDebug;  // x = AreaLightCount, y = GlassSphereCount, z = MaterialCount, w = DebugChannel
    uint4  EngineShadowReflPad;      // x = EngineMode (0..1), y = ShadowAlg (0..4), z = ReflSource (0..1), w = FrameIndex
    float4 SunCascadeCenterSpan;     // xyz = CenterWorld,  w = HalfSpanMeters
    float4 SunCascadeRightRes;       // xyz = LightRight,   w = ResolutionTexels
    float4 SunCascadeUpDepth;        // xyz = LightUp,      w = DepthExtentMeters
    float4 SunCascadeForwardPad;     // xyz = LightForward, w = Reserved
    float4 FineCascadeCenterSpan;    // xyz = FineCenter,   w = FineHalfSpanMeters
    float4 FineCascadeParams;        // x = FineRes, y = FineDepthExtent, z = CoarseOffsetTexels, w = Reserved
    uint4  DenoiseStepParams;        // x = StridePx, y = Flags (bit0=FinalComposite, bit1=SpatialDenoise), z = Reserved, w = FrameSequence
    GpuAreaQuadLight AreaLights[4];
    GpuGlassSphere   GlassSpheres[4];
    uint4                       AtmosphereMaskPad; // x = AtmosphereEnabled (0/1), y = SkyViewW, z = SkyViewH, w = FlagBits
    VolumetricCloudUniformBlock CloudParams;
    VolumetricFogUniformBlock   FogParams;
    CelestialFieldUniformBlock  CelestialParams;
    LensFlareUniformBlock       LensFlareParams;
};

struct GpuWideTreeEntry128
{
    float4 BranchBoundsMinX;
    float4 BranchBoundsMaxX;
    float4 BranchBoundsMinY;
    float4 BranchBoundsMaxY;
    float4 BranchBoundsMinZ;
    float4 BranchBoundsMaxZ;
    uint4  BranchReference;
    uint4  BranchPrimitiveCount;
};

struct GpuShadedTriangle96
{
    float4 Corner0Pad;
    float4 Edge01Pad;
    float4 Edge02Pad;
    float4 Normal0Pad;
    float4 Normal1Pad;
    float4 Normal2Slot; // xyz = SmoothNormal2, w = float(MaterialSlot)
};

struct GpuWorldMaterial80
{
    float4 DiffuseAlbedoRoughness; // xyz = EffectiveDiffuseAlbedo, w = DiffuseRoughness
    float4 SpecParams;             // x = SpecRoughness, y = Metalness, z = DielectricIOR, w = Anisotropy
    float4 EdgeTintCoatWeight;     // xyz = EdgeTintF82, w = CoatWeight
    float4 CoatRoughnessOptics;    // x = CoatRoughness, y = TransmissionWeight, z = AbbeDispersion, w = Pad
    float4 EmissiveRadiancePad;    // xyz = Planckian Blackbody Emissive RGB, w = Pad
};

struct GpuPhotometricLamp64
{
    float4 PositionRange;      // xyz = PositionWorld, w = RangeMeters
    float4 BeamAxisRadius;     // xyz = BeamAxis,      w = RadiusMeters
    float4 ZeroAzimuthProfile; // xyz = ZeroAzimuth,   w = float(ProfileOrdinal)
    float4 PeakIntensityPad;   // xyz = PeakIntensity, w = Reserved
};

[[vk::binding(0,  0)]] ConstantBuffer<WorldViewportUniformBlock> WorldUniforms;
[[vk::binding(1,  0)]] StructuredBuffer<GpuWideTreeEntry128>     SceneBvhEntries;
[[vk::binding(2,  0)]] StructuredBuffer<GpuShadedTriangle96>     SceneTriangles;
[[vk::binding(3,  0)]] StructuredBuffer<GpuWorldMaterial80>      MaterialEntries;
[[vk::binding(4,  0)]] RWStructuredBuffer<uint>                  OutDisplayRgba8;
[[vk::binding(5,  0)]] RWStructuredBuffer<float>                 SolarShadowDepthBuffer;
[[vk::binding(6,  0)]] RWStructuredBuffer<float4>                AccumulationHdrBuffer;
[[vk::binding(7,  0)]] StructuredBuffer<float4>                  AtmosphericSkyViewLut;
[[vk::binding(8,  0)]] StructuredBuffer<uint>                    LunarSurfaceAtlasTexels;
[[vk::binding(9,  0)]] StructuredBuffer<uint>                    BakedLensFlareAtlasTexels;
[[vk::binding(10, 0)]] RWStructuredBuffer<float4>                GBufPosMask;
[[vk::binding(11, 0)]] RWStructuredBuffer<float4>                GBufNormAo;
[[vk::binding(12, 0)]] RWStructuredBuffer<float4>                GBufDirectBase;
[[vk::binding(13, 0)]] RWStructuredBuffer<float4>                GBufSunContrib;
[[vk::binding(14, 0)]] RWStructuredBuffer<float4>                GBufAlbedo;
[[vk::binding(15, 0)]] RWStructuredBuffer<float4>                PingShadowAo;
[[vk::binding(16, 0)]] RWStructuredBuffer<float4>                PongShadowAo;
[[vk::binding(17, 0)]] RWStructuredBuffer<float4>                CloudRadiometricBuffer;
[[vk::binding(18, 0)]] StructuredBuffer<GpuProjectedStar>        ProjectedCatalogStars;
[[vk::binding(19, 0)]] StructuredBuffer<uint>                    StellarTileBins;
[[vk::binding(22, 0)]] StructuredBuffer<float>                   PhotometricProfileTables;
[[vk::binding(23, 0)]] StructuredBuffer<GpuPhotometricLamp64>    PhotometricLampEntries;

static const float PiConst    = 3.14159265359;
static const float TauConst   = 6.28318530718;
static const float InvPiConst = 0.31830988618;

struct RayHitReading
{
    bool   Hit;
    float  Distance;
    float3 Position;
    float3 ShadingNormal;
    float3 GeomNormal;
    uint   MaterialSlot;
};

uint PcgNext(inout uint State)
{
    State = State * 747796405u + 2891336453u;
    uint Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

float PcgFloat01(inout uint State)
{
    return float(PcgNext(State) & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

float2 PixelR2(uint2 PixelXY)
{
    float IgnA = frac(52.9829189 * frac(float(PixelXY.x) * 0.06711056 + float(PixelXY.y) * 0.00583715));
    uint  Rng  = (PixelXY.x * 747796405u + PixelXY.y * 2891336453u + 277803737u) | 1u;
    float RandB = PcgFloat01(Rng);
    return float2(IgnA, RandB);
}

bool IsReflectionsEnabled() { return (WorldUniforms.AtmosphereMaskPad.w & 2u) == 0u; }
bool IsRaytracingEnabled()  { return (WorldUniforms.AtmosphereMaskPad.w & 8u) == 0u; }

float3 EvaluateSkyDome(float3 Dir)
{
    if (WorldUniforms.AtmosphereMaskPad.x != 0u)
    {
        float PlanetRadiusM = max(500000.0, WorldUniforms.CelestialParams.SolarOpticsParams.w);
        float ObsAltM       = WorldUniforms.CelestialParams.SolarOpticsParams.z;
        return EvaluateSkyViewLutSample(
            AtmosphericSkyViewLut,
            WorldUniforms.AtmosphereMaskPad.y,
            WorldUniforms.AtmosphereMaskPad.z,
            Dir,
            PlanetRadiusM,
            ObsAltM);
    }
    float Up = saturate(Dir.z * 0.5 + 0.5);
    return WorldUniforms.SkyRadianceBvhCount.xyz * (0.65 + 0.70 * Up);
}

float4 SampleCloudBufferBilinear(float2 PixelUv, out float OutCloudTrans)
{
    uint CW = uint(WorldUniforms.CloudParams.CloudPassControl.x + 0.5);
    uint CH = uint(WorldUniforms.CloudParams.CloudPassControl.y + 0.5);
    if (CW == 0u || CH == 0u)
    {
        OutCloudTrans = 1.0;
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    float FX = clamp(PixelUv.x * float(CW) - 0.5, 0.0, max(0.0, float(CW) - 1.0));
    float FY = clamp(PixelUv.y * float(CH) - 0.5, 0.0, max(0.0, float(CH) - 1.0));
    int   IX = int(floor(FX));
    int   IY = int(floor(FY));
    float TX = FX - float(IX);
    float TY = FY - float(IY);

    uint FullW = uint(WorldUniforms.CameraUpWidth.w + 0.5);
    if (CW >= FullW || CW < 4u || CH < 4u)
    {
        uint X0 = uint(clamp(IX, 0, int(CW) - 1));
        uint Y0 = uint(clamp(IY, 0, int(CH) - 1));
        uint X1 = min(CW - 1u, X0 + 1u);
        uint Y1 = min(CH - 1u, Y0 + 1u);
        float4 C00 = CloudRadiometricBuffer[Y0 * CW + X0];
        float4 C10 = CloudRadiometricBuffer[Y0 * CW + X1];
        float4 C01 = CloudRadiometricBuffer[Y1 * CW + X0];
        float4 C11 = CloudRadiometricBuffer[Y1 * CW + X1];
        float4 Res = lerp(lerp(C00, C10, TX), lerp(C01, C11, TX), TY);
        OutCloudTrans = clamp(Res.w, 0.0, 1.0);
        return Res;
    }

    float OmX = 1.0 - TX;
    float TX2 = TX * TX;
    float TX3 = TX2 * TX;
    float WX[4] = {
        (OmX * OmX * OmX) * (1.0 / 6.0),
        (3.0 * TX3 - 6.0 * TX2 + 4.0) * (1.0 / 6.0),
        (-3.0 * TX3 + 3.0 * TX2 + 3.0 * TX + 1.0) * (1.0 / 6.0),
        TX3 * (1.0 / 6.0)
    };
    float OmY = 1.0 - TY;
    float TY2 = TY * TY;
    float TY3 = TY2 * TY;
    float WY[4] = {
        (OmY * OmY * OmY) * (1.0 / 6.0),
        (3.0 * TY3 - 6.0 * TY2 + 4.0) * (1.0 / 6.0),
        (-3.0 * TY3 + 3.0 * TY2 + 3.0 * TY + 1.0) * (1.0 / 6.0),
        TY3 * (1.0 / 6.0)
    };

    float4 Res = float4(0.0, 0.0, 0.0, 0.0);
    int MaxX = int(CW) - 1;
    int MaxY = int(CH) - 1;
    [unroll]
    for (int DY = 0; DY < 4; ++DY)
    {
        uint SY = uint(clamp(IY + DY - 1, 0, MaxY));
        float4 Row = float4(0.0, 0.0, 0.0, 0.0);
        [unroll]
        for (int DX = 0; DX < 4; ++DX)
        {
            uint SX = uint(clamp(IX + DX - 1, 0, MaxX));
            Row += CloudRadiometricBuffer[SY * CW + SX] * WX[DX];
        }
        Res += Row * WY[DY];
    }

    OutCloudTrans = clamp(Res.w, 0.0, 1.0);
    return max(float4(0.0, 0.0, 0.0, 0.0), Res);
}

float3 EvaluateCloudDeckReflectionGpu(float3 RayDir, out float OutCloudTrans)
{
    OutCloudTrans = 1.0;
    VolumetricCloudUniformBlock C = WorldUniforms.CloudParams;
    if (C.SunAxisToggle.w < 0.5 || RayDir.z <= 0.015 || C.DeckGeometry.z <= 0.01 || C.DeckGeometry.w <= 0.01)
    {
        return float3(0.0, 0.0, 0.0);
    }
    CloudSpecification Spec = BuildCloudSpecificationFromUniformGpu(C, false);
    float3 Origin           = ResolvePlanetaryViewOriginGpu(C, C.OriginTimeVector.xyz);
    CloudDeck Layer         = PlanetCloudLayer(Spec, 0u);
    float MidAlt            = (Layer.BottomM + Layer.TopM) * 0.5;
    float DistM             = min(38000.0, MidAlt / max(0.03, RayDir.z));
    float3 SamplePos        = Origin + RayDir * DistM;
    float3 Adv              = CloudWindCoordinates(Spec, SamplePos, Layer.Category);
    CloudWeatherSample Wx   = CloudWeatherFromAdvectedNormal(Spec, Adv / length(Adv));
    float Sigma             = SamplePlanetCloudShadowDensity(Spec, Layer, SamplePos, Adv, Wx, max(12.0, DistM * Spec.PixelAngle));
    if (Sigma <= 1.0e-6)
    {
        return float3(0.0, 0.0, 0.0);
    }
    float Tau = Sigma * (Layer.TopM - Layer.BottomM) * 0.45 / max(0.15, RayDir.z);
    OutCloudTrans = exp(-Tau);
    float SunT = exp(-CloudSolarOpticalDepthWithWeather(Spec, SamplePos, Wx, 3u, max(16.0, DistM * Spec.PixelAngle)));
    return (Spec.SolarIrradiance * (0.28 * SunT) + Spec.SkyAmbient * 0.55) * (1.0 - OutCloudTrans);
}

float OrbitalGlobeFbmGpu3(float3 P)
{
    float  Sum = 0.0;
    float  Amp = 0.52;
    float3 Q   = P;
    for (uint Oct = 0u; Oct < 5u; ++Oct)
    {
        Sum += Amp * CloudLatticeNoise(Q);
        Q = float3(
             0.80 * Q.x - 0.48 * Q.y + 0.36 * Q.z,
             0.48 * Q.x + 0.86 * Q.y + 0.16 * Q.z,
            -0.36 * Q.x + 0.04 * Q.y + 0.93 * Q.z) * 2.05 + float3(13.7, -7.3, 4.1);
        Amp *= 0.48;
    }
    return Sum;
}

float3 EvaluateOrbitalSpaceGpu(
    float3     RayOrigin,
    float3     RayDir,
    float3     SunDir,
    float2     PixelCoord,
    out bool   OutHitsPlanet,
    out float3 OutSpaceTrans)
{
    VolumetricCloudUniformBlock C = WorldUniforms.CloudParams;
    float PlanetR = max(500000.0, C.SolarIrradianceExt.w);
    float AtmoThk = max(20000.0, C.CloudAtmoStrengths.w > 100.0 ? C.CloudAtmoStrengths.w : 100000.0);
    float ObsAlt  = max(AtmoThk + 5000.0, C.WindForceMeta.w);
    float3 CamPos = float3(RayOrigin.x, RayOrigin.y, PlanetR + ObsAlt + RayOrigin.z);

    float GroundM = PlanetCloudGroundDistance(CamPos, RayDir, PlanetR);
    OutHitsPlanet = (GroundM < 1.0e8);

    uint BaseFmt = uint(C.FormationOptics.x + 0.5);
    CloudSpecification OrbSpec = BuildCloudSpecificationFromUniformGpu(C, false, (BaseFmt == 2u) ? 12u : BaseFmt);
    OrbSpec.Steps      = clamp(OrbSpec.Steps, 24u, 64u);
    OrbSpec.LightSteps = clamp(OrbSpec.LightSteps, 3u, 6u);

    CloudTransportIntegral Cloud = TracePlanetaryClouds(OrbSpec, CamPos, RayDir, PixelCoord, GroundM);
    CloudAirSegment Air = IntegrateCloudAir(OrbSpec, CamPos, RayDir, GroundM, 24u, false);

    float3 Behind = Air.Radiance;
    if (OutHitsPlanet)
    {
        float3 HitP = CamPos + RayDir * GroundM;
        float3 N    = normalize(HitP);
        float  NdotLRaw = dot(N, SunDir);
        float  SunM     = max(0.0, NdotLRaw);
        float  Cont     = OrbitalGlobeFbmGpu3(N * 3.1 + float3(4.1, -1.8, 0.9));
        float  PolarIce = smoothstep(0.83 + 0.06 * Cont, 0.92 + 0.06 * Cont, abs(N.z));
        float  LandMask = smoothstep(0.47, 0.515, Cont);
        float  Shelf    = smoothstep(0.415, 0.48, Cont) * (1.0 - LandMask);
        float  Moist    = OrbitalGlobeFbmGpu3(N * 7.5 + float3(-2.3, 5.1, 0.0));
        float  Elev     = saturate((Cont - 0.49) / 0.24);
        float3 LandCol  = lerp(float3(0.055, 0.145, 0.048), float3(0.350, 0.265, 0.165), saturate((Moist - 0.44) / 0.24)) * (0.85 + 0.35 * Elev);
        float3 OceanCol = float3(0.012 + 0.018 * Shelf, 0.042 + 0.058 * Shelf, 0.135 + 0.055 * Shelf);
        float3 Alb      = lerp(OceanCol, LandCol, LandMask);
        Alb             = lerp(Alb, float3(0.72, 0.76, 0.82), PolarIce);

        float3 HVec      = normalize(SunDir - RayDir);
        float  OceanSpec = (1.0 - LandMask) * (1.0 - PolarIce) * pow(max(0.0, dot(N, HVec)), 42.0) * 0.45 * (0.2 + 0.8 * SunM);
        float  TermWarm  = exp(-abs(NdotLRaw) * 14.0) * saturate((NdotLRaw + 0.08) / 0.16);
        float  SVBase    = 0.35 + 0.65 * saturate((NdotLRaw + 0.04) / 0.25);
        float3 SunVisC   = float3(SVBase * (1.0 + 0.65 * TermWarm), SVBase * (1.0 + 0.22 * TermWarm), SVBase * (1.0 - 0.10 * TermWarm));

        float FootprintM = max(1.0, GroundM * OrbSpec.PixelAngle);
        float CldShad    = (C.SunAxisToggle.w > 0.5)
            ? exp(-CloudSolarOpticalDepth(OrbSpec, HitP, 8u, FootprintM))
            : 1.0;
        float3 Surf = (Alb * SunM * CldShad + float3(OceanSpec)) * SunVisC * 0.28;

        float NightMask = saturate((-NdotLRaw - 0.04) / 0.14) * LandMask * (1.0 - PolarIce);
        if (NightMask > 0.01)
        {
            float City = saturate((OrbitalGlobeFbmGpu3(N * 16.0) - 0.60) / 0.12) * NightMask * (0.35 + 0.65 * CldShad);
            Surf += float3(City * 0.38, City * 0.22, City * 0.06);
        }
        Behind        = Air.Radiance + Air.Transmittance * Surf;
        OutSpaceTrans = float3(0.0, 0.0, 0.0);
    }
    else
    {
        OutSpaceTrans = Air.Transmittance * Cloud.Transmittance;
    }

    float3 Radiance = Behind * Cloud.Transmittance + CloudRadianceThroughAir(OrbSpec, CamPos, RayDir, Cloud, 16u);
    return max(float3(0.0, 0.0, 0.0), Radiance);
}

float3 EvaluateBackgroundSkyCloudsGpu(float3 RayDir, float2 PixelCoord)
{
    float2 SubPix    = PixelCoord + float2(0.5, 0.5);
    float2 VpDims    = max(float2(1.0, 1.0), float2(WorldUniforms.CameraUpWidth.w, WorldUniforms.CameraForwardHeight.w));
    float2 PrimUv    = SubPix / VpDims;
    float3 PrimDir   = normalize(WorldUniforms.CameraForwardHeight.xyz
                               + WorldUniforms.CameraRightTanY.xyz * ((PrimUv.x * 2.0 - 1.0) * WorldUniforms.CameraOriginTanX.w)
                               + WorldUniforms.CameraUpWidth.xyz   * ((1.0 - PrimUv.y * 2.0) * WorldUniforms.CameraRightTanY.w));
    bool   UseTiled  = (dot(normalize(RayDir), PrimDir) > 0.999);

    int AtmoFlags = int(round(max(0.0, WorldUniforms.CelestialParams.AtmoMediumPack.w)));
    bool IsOrbital = (AtmoFlags & 2) != 0;
    if (!IsOrbital && WorldUniforms.AtmosphereMaskPad.x == 0u)
    {
        return EvaluateSkyDome(RayDir);
    }

    float3 BaseSky    = float3(0.0, 0.0, 0.0);
    float3 SpaceTrans = float3(1.0, 1.0, 1.0);
    if (IsOrbital)
    {
        bool HitsPlanet = false;
        BaseSky = EvaluateOrbitalSpaceGpu(
            WorldUniforms.CameraOriginTanX.xyz, RayDir, normalize(WorldUniforms.SunDirConeTan.xyz), SubPix, HitsPlanet, SpaceTrans);
        if (HitsPlanet)
        {
            return BaseSky;
        }
    }
    else
    {
        BaseSky = EvaluateSkyDome(RayDir);
    }

    float3 CelestialRad = EvaluateCelestialBodiesUnifiedGpu(
        WorldUniforms.CelestialParams,
        AtmosphericSkyViewLut,
        WorldUniforms.CloudParams.CloudAtmoStrengths.w,
        LunarSurfaceAtlasTexels,
        ProjectedCatalogStars,
        StellarTileBins,
        RawCatalogStars,
        SubPix,
        RayDir,
        UseTiled);
    if (IsOrbital)
    {
        return BaseSky + CelestialRad * SpaceTrans;
    }

    float3 SkyRad = BaseSky;
    if (WorldUniforms.CloudParams.SunAxisToggle.w < 0.5)
    {
        return SkyRad + CelestialRad;
    }

    float  CloudTrans = 1.0;
    float3 CloudRad   = float3(0.0, 0.0, 0.0);
    uint   CW         = uint(WorldUniforms.CloudParams.CloudPassControl.x + 0.5);
    uint   CH         = uint(WorldUniforms.CloudParams.CloudPassControl.y + 0.5);
    float  FwdProj    = dot(RayDir, WorldUniforms.CameraForwardHeight.xyz);
    if (CW > 0u && CH > 0u && FwdProj > 0.05)
    {
        float NdcX = dot(RayDir, WorldUniforms.CameraRightTanY.xyz) / (FwdProj * max(1.0e-4, WorldUniforms.CameraOriginTanX.w));
        float NdcY = dot(RayDir, WorldUniforms.CameraUpWidth.xyz)   / (FwdProj * max(1.0e-4, WorldUniforms.CameraRightTanY.w));
        if (abs(NdcX) <= 1.001 && abs(NdcY) <= 1.001)
        {
            float2 Uv = float2(NdcX * 0.5 + 0.5, 0.5 - NdcY * 0.5);
            CloudRad  = SampleCloudBufferBilinear(Uv, CloudTrans).xyz;
            return (SkyRad + CelestialRad) * CloudTrans + CloudRad;
        }
    }

    CloudRad = EvaluateCloudDeckReflectionGpu(RayDir, CloudTrans);
    return (SkyRad + CelestialRad) * CloudTrans + CloudRad;
}

float EvaluateSolarCloudTransmittanceGpu(float3 WorldPos, float3 SunDir)
{
    VolumetricCloudUniformBlock C = WorldUniforms.CloudParams;
    if (WorldUniforms.AtmosphereMaskPad.x == 0u || SunDir.z <= 0.01)
    {
        return 1.0;
    }
    float Trans = 1.0;
    if (C.SunAxisToggle.w >= 0.5 && C.WindVectorOcclude.z >= 0.5)
    {
        CloudSpecification Spec = BuildCloudSpecificationFromUniformGpu(C, true);
        float3 PlanetP = ResolvePlanetaryViewOriginGpu(C, WorldPos);
        uint   ShSteps = clamp(min(Spec.LightSteps, 4u), 2u, 4u);
        float  Tau     = CloudSolarOpticalDepth(Spec, PlanetP, ShSteps, 24.0);
        Trans *= (0.12 + 0.88 * exp(-Tau));
    }
    Trans *= EvaluateLocalCloudsShadowGpu(C, WorldPos, SunDir);
    return clamp(Trans, 0.08, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           WIDE TREE (BVH4) RAY INTERSECTION
//------------------------------------------------------------------------------------------------------------------------

float IntersectTriangleMT(float3 Origin, float3 Dir, float MinT, float MaxT, uint TriIndex, out float2 OutUV)
{
    OutUV = float2(0.0, 0.0);
    float3 Edge01  = SceneTriangles[TriIndex].Edge01Pad.xyz;
    float3 Edge02  = SceneTriangles[TriIndex].Edge02Pad.xyz;
    float3 PVec    = cross(Dir, Edge02);
    float  Det     = dot(Edge01, PVec);
    float  AbsDet  = abs(Det);
    if (AbsDet <= 1.0e-8) return -1.0;

    float  SignDet = (Det >= 0.0) ? 1.0 : -1.0;
    float3 Corner0 = SceneTriangles[TriIndex].Corner0Pad.xyz;
    float3 TVec    = Origin - Corner0;
    float  UDet    = dot(TVec, PVec) * SignDet;
    if (UDet < 0.0 || UDet > AbsDet) return -1.0;

    float3 QVec  = cross(TVec, Edge01);
    float  VDet  = dot(Dir, QVec) * SignDet;
    if (VDet < 0.0 || (UDet + VDet) > AbsDet) return -1.0;

    float  TDet  = dot(Edge02, QVec) * SignDet;
    if (TDet < MinT * AbsDet || TDet > MaxT * AbsDet) return -1.0;

    float  InvAbsDet = 1.0 / AbsDet;
    OutUV = float2(UDet, VDet) * InvAbsDet;
    return TDet * InvAbsDet;
}

bool IntersectTriangleShadowMT(float3 Origin, float3 Dir, float MinT, float MaxT, uint TriIndex)
{
    float3 Edge01  = SceneTriangles[TriIndex].Edge01Pad.xyz;
    float3 Edge02  = SceneTriangles[TriIndex].Edge02Pad.xyz;
    float3 PVec    = cross(Dir, Edge02);
    float  Det     = dot(Edge01, PVec);
    float  AbsDet  = abs(Det);
    if (AbsDet <= 1.0e-8) return false;

    float  SignDet = (Det >= 0.0) ? 1.0 : -1.0;
    float3 Corner0 = SceneTriangles[TriIndex].Corner0Pad.xyz;
    float3 TVec    = Origin - Corner0;
    float  UDet    = dot(TVec, PVec) * SignDet;
    if (UDet < 0.0 || UDet > AbsDet) return false;

    float3 QVec  = cross(TVec, Edge01);
    float  VDet  = dot(Dir, QVec) * SignDet;
    if (VDet < 0.0 || (UDet + VDet) > AbsDet) return false;

    float  TDet  = dot(Edge02, QVec) * SignDet;
    return (TDet >= MinT * AbsDet && TDet <= MaxT * AbsDet);
}

float4 IntersectFourBoxes(float3 Origin, float3 InvDir, float MinT, float MaxT, uint EntryIndex)
{
    float4 TX0 = (SceneBvhEntries[EntryIndex].BranchBoundsMinX - Origin.xxxx) * InvDir.xxxx;
    float4 TX1 = (SceneBvhEntries[EntryIndex].BranchBoundsMaxX - Origin.xxxx) * InvDir.xxxx;
    float4 TY0 = (SceneBvhEntries[EntryIndex].BranchBoundsMinY - Origin.yyyy) * InvDir.yyyy;
    float4 TY1 = (SceneBvhEntries[EntryIndex].BranchBoundsMaxY - Origin.yyyy) * InvDir.yyyy;
    float4 TZ0 = (SceneBvhEntries[EntryIndex].BranchBoundsMinZ - Origin.zzzz) * InvDir.zzzz;
    float4 TZ1 = (SceneBvhEntries[EntryIndex].BranchBoundsMaxZ - Origin.zzzz) * InvDir.zzzz;

    float4 NearT = max(max(min(TX0, TX1), min(TY0, TY1)), max(min(TZ0, TZ1), float4(MinT)));
    float4 FarT  = min(min(max(TX0, TX1), max(TY0, TY1)), min(max(TZ0, TZ1), float4(MaxT)));
    return select(NearT <= FarT, NearT, float4(1.0e30));
}

RayHitReading TraceSceneClosest(float3 Origin, float3 Dir, float MinT, float MaxT)
{
    RayHitReading Res;
    Res.Hit           = false;
    Res.Distance      = MaxT;
    Res.Position      = Origin;
    Res.ShadingNormal = float3(0.0, 0.0, 1.0);
    Res.GeomNormal    = float3(0.0, 0.0, 1.0);
    Res.MaterialSlot  = 0u;

    uint EntryCount = uint(WorldUniforms.SkyRadianceBvhCount.w + 0.5);
    if (EntryCount == 0u) return Res;

    float3 SafeDir = select(abs(Dir) > float3(1.0e-8), Dir, float3(1.0e-8));
    float3 InvDir  = 1.0 / SafeDir;

    uint Stack[20];
    int  StackTop = 0;
    Stack[0]      = 0u;

    while (StackTop >= 0)
    {
        uint CurIdx = Stack[StackTop--];
        if (CurIdx >= EntryCount) continue;

        float4 BoxT = IntersectFourBoxes(Origin, InvDir, MinT, Res.Distance, CurIdx);
        uint4  Refs = SceneBvhEntries[CurIdx].BranchReference;
        uint4  Cnts = SceneBvhEntries[CurIdx].BranchPrimitiveCount;

        uint4 Order = uint4(0u, 1u, 2u, 3u);
        if (BoxT[Order.x] < BoxT[Order.y]) { uint Tmp = Order.x; Order.x = Order.y; Order.y = Tmp; }
        if (BoxT[Order.z] < BoxT[Order.w]) { uint Tmp = Order.z; Order.z = Order.w; Order.w = Tmp; }
        if (BoxT[Order.x] < BoxT[Order.z]) { uint Tmp = Order.x; Order.x = Order.z; Order.z = Tmp; }
        if (BoxT[Order.y] < BoxT[Order.w]) { uint Tmp = Order.y; Order.y = Order.w; Order.w = Tmp; }
        if (BoxT[Order.y] < BoxT[Order.z]) { uint Tmp = Order.y; Order.y = Order.z; Order.z = Tmp; }

        for (int LaneIdx = 0; LaneIdx < 4; ++LaneIdx)
        {
            uint Lane = Order[LaneIdx];
            if (BoxT[Lane] >= Res.Distance) continue;

            uint Ref  = Refs[Lane];
            uint Cnt  = Cnts[Lane];
            if (Ref == 0xFFFFFFFFu) continue;

            if (Cnt > 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                for (uint P = 0u; P < Cnt; ++P)
                {
                    uint   TriIdx = Start + P;
                    float2 BaryUV;
                    float  HitT = IntersectTriangleMT(Origin, Dir, MinT, Res.Distance, TriIdx, BaryUV);
                    if (HitT > 0.0)
                    {
                        Res.Hit        = true;
                        Res.Distance   = HitT;
                        Res.Position   = Origin + Dir * HitT;
                        float  W0      = 1.0 - BaryUV.x - BaryUV.y;
                        float3 Edge01  = SceneTriangles[TriIdx].Edge01Pad.xyz;
                        float3 Edge02  = SceneTriangles[TriIdx].Edge02Pad.xyz;
                        float4 N2Slot  = SceneTriangles[TriIdx].Normal2Slot;
                        float3 RawGeom = normalize(cross(Edge01, Edge02));
                        float3 SmoothN = normalize(
                            SceneTriangles[TriIdx].Normal0Pad.xyz * W0 +
                            SceneTriangles[TriIdx].Normal1Pad.xyz * BaryUV.x +
                            N2Slot.xyz * BaryUV.y);
                        if (dot(RawGeom, Dir) > 0.0)
                        {
                            RawGeom = -RawGeom;
                            SmoothN = -SmoothN;
                        }
                        Res.GeomNormal    = RawGeom;
                        Res.ShadingNormal = SmoothN;
                        Res.MaterialSlot  = uint(N2Slot.w + 0.5);
                    }
                }
            }
            else if (StackTop < 18)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }
    return Res;
}

bool TraceSceneAnyHit(float3 Origin, float3 Dir, float MinT, float MaxT)
{
    uint EntryCount = uint(WorldUniforms.SkyRadianceBvhCount.w + 0.5);
    if (EntryCount == 0u) return false;

    float3 SafeDir = select(abs(Dir) > float3(1.0e-8), Dir, float3(1.0e-8));
    float3 InvDir  = 1.0 / SafeDir;

    uint Stack[20];
    int  StackTop = 0;
    Stack[0]      = 0u;

    while (StackTop >= 0)
    {
        uint CurIdx = Stack[StackTop--];
        if (CurIdx >= EntryCount) continue;

        float4 BoxT = IntersectFourBoxes(Origin, InvDir, MinT, MaxT, CurIdx);
        uint4  Refs = SceneBvhEntries[CurIdx].BranchReference;
        uint4  Cnts = SceneBvhEntries[CurIdx].BranchPrimitiveCount;

        for (uint Lane = 0u; Lane < 4u; ++Lane)
        {
            if (BoxT[Lane] >= MaxT) continue;
            uint Ref = Refs[Lane];
            uint Cnt = Cnts[Lane];
            if (Ref == 0xFFFFFFFFu) continue;

            if (Cnt > 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                for (uint P = 0u; P < Cnt; ++P)
                {
                    if (IntersectTriangleShadowMT(Origin, Dir, MinT, MaxT, Start + P))
                    {
                        return true;
                    }
                }
            }
            else if (StackTop < 18)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      OPENPBR BRDF & LINEAR COSINE AREA LIGHTS
//------------------------------------------------------------------------------------------------------------------------

void ConstructOrthonormalBasis(float3 N, out float3 T, out float3 B)
{
    float SignZ = (N.z >= 0.0) ? 1.0 : -1.0;
    float A     = -1.0 / (SignZ + N.z);
    float D     = N.x * N.y * A;
    T = float3(1.0 + SignZ * N.x * N.x * A, SignZ * D, -SignZ * N.x);
    B = float3(D, SignZ + N.y * N.y * A, -N.y);
}

float IntegrateSphericalEdgeZ(float3 V1, float3 V2)
{
    float X    = clamp(dot(V1, V2), -0.99999, 0.99999);
    float AbsX = abs(X);
    float A    = 5.42031 + (3.12829 + 0.0902326 * AbsX) * AbsX;
    float B    = 3.45068 + (4.18814 + AbsX) * AbsX;
    float ThetaOverSin = A / B;
    if (X < 0.0)
    {
        float SinTheta = sqrt(max(1.0e-8, 1.0 - X * X));
        ThetaOverSin   = PiConst / SinTheta - ThetaOverSin;
    }
    return (V1.x * V2.y - V1.y * V2.x) * ThetaOverSin;
}

float3 EvaluateSurfaceBrdf(GpuWorldMaterial80 Mat, float3 N, float3 V, float3 L)
{
    float NdotL = max(0.0, dot(N, L));
    float NdotV = max(1.0e-4, dot(N, V));
    if (NdotL <= 1.0e-5) return float3(0.0);

    float3 Albedo    = Mat.DiffuseAlbedoRoughness.xyz;
    float  DiffRough = Mat.DiffuseAlbedoRoughness.w;
    float  SpecRough = clamp(Mat.SpecParams.x, 0.025, 1.0);
    float  Metalness = saturate(Mat.SpecParams.y);
    float  Ior       = max(1.02, Mat.SpecParams.z);
    float3 EdgeF82   = Mat.EdgeTintCoatWeight.xyz;
    float  CoatW     = saturate(Mat.EdgeTintCoatWeight.w);
    float  CoatRough = clamp(Mat.CoatRoughnessOptics.x, 0.025, 1.0);
    float  TransW    = saturate(Mat.CoatRoughnessOptics.y);

    float CoeffA  = 1.0 / (1.0 + DiffRough * 0.2877934);
    float CoeffB  = DiffRough * CoeffA;
    float SVal    = dot(V, L) - NdotV * NdotL;
    float InvMax  = (SVal > 0.0) ? (1.0 / max(1.0e-4, max(NdotV, NdotL))) : 1.0;
    float3 Diff   = Albedo * (InvPiConst * (CoeffA + CoeffB * SVal * InvMax)) * (1.0 - Metalness) * (1.0 - TransW);

    float3 H       = normalize(V + L);
    float  NdotH   = max(0.0, dot(N, H));
    float  VdotH   = saturate(dot(V, H));
    float  Alpha   = SpecRough * SpecRough;
    float  Alpha2  = Alpha * Alpha;
    float  Denom   = NdotH * NdotH * (Alpha2 - 1.0) + 1.0;
    float  D_GGX   = Alpha2 / max(1.0e-6, PiConst * Denom * Denom);
    float  KVis    = 0.5 * Alpha;
    float  Vis     = 0.25 / max(1.0e-4, (NdotV * (1.0 - KVis) + KVis) * (NdotL * (1.0 - KVis) + KVis));
    float  F0Diel  = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
    float3 F0      = lerp(float3(F0Diel), Albedo, Metalness);
    float  Schlick = pow(1.0 - VdotH, 5.0);
    float3 Fresnel = F0 + (float3(1.0) - F0) * Schlick;
    if (Metalness > 0.5)
    {
        float Lazanyi = VdotH * pow(1.0 - VdotH, 6.0);
        Fresnel -= (float3(1.0) - EdgeF82) * F0 * Lazanyi * 4.5;
    }
    float3 Spec = max(float3(0.0), Fresnel * (D_GGX * Vis));

    if (CoatW > 0.01)
    {
        float CA   = CoatRough * CoatRough;
        float CA2  = CA * CA;
        float CDen = NdotH * NdotH * (CA2 - 1.0) + 1.0;
        float CD   = CA2 / max(1.0e-6, PiConst * CDen * CDen);
        float CF   = 0.04 + 0.96 * Schlick;
        Spec      += float3(CoatW * CF * CD * 0.25);
        Diff      *= (1.0 - CoatW * CF);
    }

    return (Diff + Spec) * NdotL;
}

float IntegrateClippedQuadFormFactor(float3 LocalQ[4])
{
    float3 Clipped[8];
    uint   ClipCount = 0u;
    for (uint I = 0u; I < 4u; ++I)
    {
        float3 Cur  = LocalQ[I];
        float3 Next = LocalQ[(I + 1u) & 3u];
        bool   CurA = (Cur.z >= 0.0);
        bool   NxtA = (Next.z >= 0.0);
        if (CurA) Clipped[ClipCount++] = Cur;
        if (CurA != NxtA)
        {
            float Alpha = Cur.z / (Cur.z - Next.z);
            float3 Hit  = Cur + (Next - Cur) * Alpha;
            Hit.z       = 0.0;
            Clipped[ClipCount++] = Hit;
        }
    }
    if (ClipCount < 3u) return 0.0;

    for (uint I = 0u; I < ClipCount; ++I)
    {
        Clipped[I] = normalize(Clipped[I]);
    }
    float ContourSum = 0.0;
    for (uint I = 0u; I < ClipCount; ++I)
    {
        ContourSum += IntegrateSphericalEdgeZ(Clipped[I], Clipped[(I + 1u) % ClipCount]);
    }
    return saturate(abs(ContourSum) / TauConst);
}

float3 EvaluateAreaQuadLight(GpuAreaQuadLight Quad, float3 Pos, float3 N, float3 V, GpuWorldMaterial80 Mat)
{
    float3 RightT = Quad.RightHalfHeight.xyz;
    float3 UpB    = Quad.UpTwoSided.xyz;
    float3 QuadN  = normalize(cross(RightT, UpB));
    float3 Center = Quad.CenterHalfWidth.xyz;
    if (Quad.UpTwoSided.w < 0.5 && dot(Pos - Center, QuadN) <= 1.0e-4)
    {
        return float3(0.0);
    }

    float3 HalfU = RightT * Quad.CenterHalfWidth.w;
    float3 HalfV = UpB * Quad.RightHalfHeight.w;

    uint   PenumbraMode = (WorldUniforms.AtmosphereMaskPad.w >> 16u) & 0x7u;
    float3 RayOrig      = Pos + N * 0.010;
    float  VisFactor    = 0.0;
    if (PenumbraMode == 0u)
    {
        float3 ToL  = Center - RayOrig;
        float  Dist = length(ToL);
        if (Dist > 1.0e-4)
        {
            float3 DirL = ToL / Dist;
            if (dot(N, DirL) > 0.0 && !TraceSceneAnyHit(RayOrig, DirL, 0.032, max(0.040, Dist - 0.035)))
            {
                VisFactor = 1.0;
            }
        }
    }
    else
    {
        uint TapCount = (PenumbraMode >= 4u) ? 6u : 4u;
        uint VisCnt   = 0u;
        for (uint S = 0u; S < TapCount; ++S)
        {
            float  Rad    = sqrt((float(S) + 0.5) / float(TapCount)) * 0.82;
            float  Ang    = float(S) * 2.39996323 + dot(Pos, float3(12.9898, 78.233, 37.719));
            float3 Target = Center + HalfU * (Rad * cos(Ang)) + HalfV * (Rad * sin(Ang));
            float3 ToL    = Target - RayOrig;
            float  Dist   = length(ToL);
            if (Dist <= 1.0e-4) continue;
            float3 DirL   = ToL / Dist;
            if (dot(N, DirL) <= 0.0) continue;
            if (!TraceSceneAnyHit(RayOrig, DirL, 0.032, max(0.040, Dist - 0.035)))
            {
                ++VisCnt;
            }
        }
        VisFactor = float(VisCnt) / float(TapCount);
    }
    if (VisFactor <= 1.0e-4) return float3(0.0);
    if (PenumbraMode == 3u)
    {
        VisFactor = min(1.0, VisFactor * 1.08);
    }

    float  NdotV = clamp(dot(N, V), 1.0e-4, 1.0);
    float3 T1    = V - N * NdotV;
    float  T1Len = dot(T1, T1);
    if (T1Len <= 1.0e-6)
    {
        float3 FallbackB;
        ConstructOrthonormalBasis(N, T1, FallbackB);
    }
    else
    {
        T1 = T1 * rsqrt(T1Len);
    }
    float3 T2 = normalize(cross(N, T1));

    float3 WorldC[4] = {
        Center - HalfU - HalfV,
        Center + HalfU - HalfV,
        Center + HalfU + HalfV,
        Center - HalfU + HalfV
    };
    float3 DiffLocal[4];
    for (uint I = 0u; I < 4u; ++I)
    {
        float3 Rel   = WorldC[I] - Pos;
        DiffLocal[I] = float3(dot(Rel, T1), dot(Rel, T2), dot(Rel, N));
    }
    float DiffFormFactor = IntegrateClippedQuadFormFactor(DiffLocal);

    float Roughness = clamp(Mat.SpecParams.x, 0.025, 1.0);
    float Alpha     = clamp(Roughness * Roughness, 0.004, 1.0);
    float SinV      = sqrt(max(0.0, 1.0 - NdotV * NdotV));
    float InvScaleX = 1.0 / max(0.04, Alpha * (0.85 + 0.15 * NdotV));
    float InvScaleY = 1.0 / max(0.04, Alpha);
    float ShearXZ   = -SinV / max(0.08, NdotV + Alpha);

    float3 SpecLocal[4];
    for (uint I = 0u; I < 4u; ++I)
    {
        float3 D      = DiffLocal[I];
        float MirrorX = -D.x * NdotV + D.z * SinV;
        float MirrorZ =  D.x * SinV  + D.z * NdotV;
        SpecLocal[I]  = float3(
            (MirrorX + ShearXZ * MirrorZ * Alpha) * InvScaleX,
            D.y * InvScaleY,
            MirrorZ);
    }
    float SpecFormFactor = IntegrateClippedQuadFormFactor(SpecLocal);

    float3 Albedo    = Mat.DiffuseAlbedoRoughness.xyz;
    float  Metalness = saturate(Mat.SpecParams.y);
    float  TransW    = saturate(Mat.CoatRoughnessOptics.y);
    float  Ior       = max(1.02, Mat.SpecParams.z);
    float  DielF0    = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
    float3 SpecF0    = lerp(float3(DielF0), Albedo, Metalness);
    float  Schlick   = pow(1.0 - NdotV, 5.0);
    float3 SpecAlb   = SpecF0 + (float3(1.0) - SpecF0) * (Schlick * saturate(1.0 - Roughness * 0.65));

    float3 DiffContrib = Albedo * (1.0 - Metalness) * (1.0 - TransW) * DiffFormFactor;
    float3 SpecContrib = SpecAlb * SpecFormFactor;
    return Quad.RadiancePacked.xyz * (DiffContrib + SpecContrib) * VisFactor;
}

float EvaluatePhotometricProfile128x32(
    uint   ProfileOrdinal,
    float3 TowardSurface,
    float3 BeamAxis,
    float3 ZeroAzimuth)
{
    float  CosTheta    = clamp(dot(BeamAxis, TowardSurface), -1.0, 1.0);
    float  VerticalPos = acos(CosTheta) * (127.0 / PiConst);
    float3 OrthoBit    = cross(BeamAxis, ZeroAzimuth);
    float  HorizPos    = atan2(dot(TowardSurface, OrthoBit), dot(TowardSurface, ZeroAzimuth)) * (32.0 / TauConst);
    if (HorizPos < 0.0)
    {
        HorizPos += 32.0;
    }

    uint  V0   = min(uint(VerticalPos), 126u);
    float Vt   = saturate(VerticalPos - float(V0));
    uint  H0   = min(uint(HorizPos), 31u);
    float Ht   = saturate(HorizPos - float(H0));
    uint  H1   = (H0 + 1u) & 31u;
    uint  Base = ProfileOrdinal * 4096u;
    uint  I00  = Base + H0 * 128u + V0;
    uint  I10  = Base + H1 * 128u + V0;

    float NearRow = lerp(PhotometricProfileTables[I00], PhotometricProfileTables[I00 + 1u], Vt);
    float FarRow  = lerp(PhotometricProfileTables[I10], PhotometricProfileTables[I10 + 1u], Vt);
    return lerp(NearRow, FarRow, Ht);
}

float3 EvaluatePhotometricLampGpu(
    GpuPhotometricLamp64 Lamp,
    uint                 ProfileCount,
    float3               SafePos,
    float3               N,
    float3               V,
    GpuWorldMaterial80   Mat)
{
    uint ProfileOrd = uint(Lamp.ZeroAzimuthProfile.w + 0.5);
    if (ProfileOrd >= ProfileCount)
    {
        return float3(0.0);
    }

    float3 Delta = Lamp.PositionRange.xyz - SafePos;
    float  Dist2 = dot(Delta, Delta);
    float  Dist  = sqrt(max(1.0e-5, Dist2));
    float  Range = Lamp.PositionRange.w;
    if (Dist >= Range)
    {
        return float3(0.0);
    }

    float3 LDir  = Delta / Dist;
    float  NdotL = dot(N, LDir);
    if (NdotL <= 1.0e-4)
    {
        return float3(0.0);
    }

    float ProfileGain = EvaluatePhotometricProfile128x32(
        ProfileOrd,
        -LDir,
        normalize(Lamp.BeamAxisRadius.xyz),
        normalize(Lamp.ZeroAzimuthProfile.xyz));
    if (ProfileGain <= 1.0e-5)
    {
        return float3(0.0);
    }

    float Radius = max(0.025, Lamp.BeamAxisRadius.w);
    if (TraceSceneAnyHit(SafePos, LDir, 2.5e-3, max(0.02, Dist - Radius)))
    {
        return float3(0.0);
    }

    float Ratio  = saturate(Dist / max(0.5, Range));
    float Ratio2 = Ratio * Ratio;
    float Window = max(0.0, 1.0 - Ratio2 * Ratio2);
    float Atten  = (ProfileGain * Window * Window) / max(0.01, Dist2 + Radius * Radius);
    return EvaluateSurfaceBrdf(Mat, N, V, LDir) * Lamp.PeakIntensityPad.xyz * Atten;
}

//------------------------------------------------------------------------------------------------------------------------
//                      SHADOW LAB ALGORITHMS (depth32float Sun Cascades + BVH4 Ray Queries)
//------------------------------------------------------------------------------------------------------------------------

bool ProjectCascadeLevel(
    float3     WorldPos,
    float3     CenterWorld,
    float      HalfSpan,
    float3     RightAxis,
    float3     UpAxis,
    float3     FwdAxis,
    float      DepthExtent,
    uint       Res,
    out float2 OutTexelXY,
    out float  OutRecD,
    out float  OutTexelM)
{
    OutTexelXY = float2(0.0);
    OutRecD    = 0.0;
    OutTexelM  = 0.015;
    if (Res < 4u || HalfSpan <= 1.0e-4) return false;

    float3 Rel    = WorldPos - CenterWorld;
    float  LocalU = dot(Rel, RightAxis);
    float  LocalV = dot(Rel, UpAxis);
    float  LocalZ = dot(Rel, FwdAxis);
    float  Margin = HalfSpan * 0.94;
    if (abs(LocalU) > Margin || abs(LocalV) > Margin) return false;

    float ResF     = float(Res);
    float InvSpan2 = 1.0 / (2.0 * HalfSpan);
    OutTexelXY.x   = ((LocalU + HalfSpan) * InvSpan2) * ResF;
    OutTexelXY.y   = ((HalfSpan - LocalV) * InvSpan2) * ResF;
    OutRecD        = LocalZ + DepthExtent;
    OutTexelM      = (2.0 * HalfSpan) / ResF;
    return true;
}

bool ProjectBestSunCascade(
    float3     WorldPos,
    out float2 OutTexelXY,
    out float  OutRecD,
    out float  OutTexelM,
    out uint   OutRes,
    out uint   OutBufferOffset)
{
    uint FineRes    = uint(WorldUniforms.FineCascadeParams.x + 0.5);
    uint CoarseOffs = uint(WorldUniforms.FineCascadeParams.z + 0.5);
    if (FineRes >= 4u && CoarseOffs >= FineRes * FineRes)
    {
        if (ProjectCascadeLevel(
                WorldPos,
                WorldUniforms.FineCascadeCenterSpan.xyz,
                WorldUniforms.FineCascadeCenterSpan.w,
                WorldUniforms.SunCascadeRightRes.xyz,
                WorldUniforms.SunCascadeUpDepth.xyz,
                WorldUniforms.SunCascadeForwardPad.xyz,
                WorldUniforms.FineCascadeParams.y,
                FineRes,
                OutTexelXY,
                OutRecD,
                OutTexelM))
        {
            OutRes          = FineRes;
            OutBufferOffset = 0u;
            return true;
        }
    }

    OutRes          = uint(WorldUniforms.SunCascadeRightRes.w + 0.5);
    OutBufferOffset = CoarseOffs;
    return ProjectCascadeLevel(
        WorldPos,
        WorldUniforms.SunCascadeCenterSpan.xyz,
        WorldUniforms.SunCascadeCenterSpan.w,
        WorldUniforms.SunCascadeRightRes.xyz,
        WorldUniforms.SunCascadeUpDepth.xyz,
        WorldUniforms.SunCascadeForwardPad.xyz,
        WorldUniforms.SunCascadeUpDepth.w,
        OutRes,
        OutTexelXY,
        OutRecD,
        OutTexelM);
}

float SampleShadowVisBilinear(float2 TexelXY, float RecD, float Bias, uint Res, uint BufferOffset)
{
    int   MaxIdx = int(Res) - 1;
    float FX     = TexelXY.x - 0.5;
    float FY     = TexelXY.y - 0.5;
    int   X0     = clamp(int(floor(FX)), 0, MaxIdx);
    int   Y0     = clamp(int(floor(FY)), 0, MaxIdx);
    int   X1     = min(X0 + 1, MaxIdx);
    int   Y1     = min(Y0 + 1, MaxIdx);
    float TX     = saturate(FX - floor(FX));
    float TY     = saturate(FY - floor(FY));

    float V00 = ((RecD - SolarShadowDepthBuffer[BufferOffset + uint(Y0) * Res + uint(X0)]) <= Bias) ? 1.0 : 0.0;
    float V10 = ((RecD - SolarShadowDepthBuffer[BufferOffset + uint(Y0) * Res + uint(X1)]) <= Bias) ? 1.0 : 0.0;
    float V01 = ((RecD - SolarShadowDepthBuffer[BufferOffset + uint(Y1) * Res + uint(X0)]) <= Bias) ? 1.0 : 0.0;
    float V11 = ((RecD - SolarShadowDepthBuffer[BufferOffset + uint(Y1) * Res + uint(X1)]) <= Bias) ? 1.0 : 0.0;
    return lerp(lerp(V00, V10, TX), lerp(V01, V11, TX), TY);
}

float EvaluateSolarShadow(RayHitReading Hit, float3 SunDir, uint2 PixelXY, out float OutBlockerDist)
{
    OutBlockerDist = 0.0;
    uint ShadowAlg = WorldUniforms.EngineShadowReflPad.y;
    float NdotL    = dot(Hit.ShadingNormal, SunDir);
    if (NdotL <= 1.0e-5) return 0.0;

    float  TanSun = max(0.004, WorldUniforms.SunDirConeTan.w);
    float2 R2P    = PixelR2(PixelXY);
    float  RotPhi = R2P.x * TauConst;

    // Algorithm 4: WideTreeRayQuery (8 Golden-Angle Vogel BVH4 Solar Disc Rays + Harmonic Blocker Distance)
    if (ShadowAlg == 4u)
    {
        float3 T, B;
        ConstructOrthonormalBasis(SunDir, T, B);
        float3 Orig      = Hit.Position + Hit.GeomNormal * 0.003;
        float  Vis       = 0.0;
        float  InvBlkSum = 0.0;
        uint   BlkCnt    = 0u;
        for (uint R = 0u; R < 8u; ++R)
        {
            float DiscR = sqrt((float(R) + 0.5) * 0.125) * TanSun;
            float Ang   = float(R) * 2.39996323 + RotPhi;
            float3 JDir = normalize(SunDir + T * (DiscR * cos(Ang)) + B * (DiscR * sin(Ang)));
            RayHitReading Blk = TraceSceneClosest(Orig, JDir, 0.003, 90.0);
            if (!Blk.Hit)
            {
                Vis += 0.125;
            }
            else
            {
                InvBlkSum += 1.0 / max(0.02, Blk.Distance);
                ++BlkCnt;
            }
        }
        OutBlockerDist = (BlkCnt > 0u) ? (float(BlkCnt) / max(1.0e-5, InvBlkSum)) : 0.0;
        return Vis;
    }

    float2 CenterTXY;
    float  RecD, TexelM;
    uint   Res, BufOffs;
    float3 BiasedPos = Hit.Position + Hit.GeomNormal * 0.004 + Hit.ShadingNormal * 0.004;
    if (!ProjectBestSunCascade(BiasedPos, CenterTXY, RecD, TexelM, Res, BufOffs))
    {
        float3 Orig = Hit.Position + Hit.GeomNormal * 0.012;
        return TraceSceneAnyHit(Orig, SunDir, 0.028, 90.0) ? 0.0 : 1.0;
    }

    int   MaxIdx     = int(Res) - 1;
    float SlopeScale = sqrt(max(0.0, 1.0 - NdotL * NdotL)) / max(0.18, NdotL);
    float Bias       = TexelM * (1.20 + 0.90 * SlopeScale);

    // Algorithm 3: PcfBilinear (16-Tap Smooth Bilinear Vogel PCF)
    if (ShadowAlg == 3u)
    {
        float Vis = 0.0;
        float RadiusPx = 1.85;
        for (uint Tap = 0u; Tap < 16u; ++Tap)
        {
            float R   = sqrt((float(Tap) + 0.5) / 16.0) * RadiusPx;
            float Ang = float(Tap) * 2.39996323 + RotPhi;
            Vis += SampleShadowVisBilinear(CenterTXY + float2(cos(Ang), sin(Ang)) * R, RecD, Bias + TexelM * 0.12 * R, Res, BufOffs);
        }
        Vis *= (1.0 / 16.0);
        OutBlockerDist = (Vis > 0.01 && Vis < 0.99) ? 0.28 : 0.0;
        return Vis;
    }

    // Algorithm 2: ShadowRayMarch (Quadratic SMRT with Segment-Crossing Continuity)
    if (ShadowAlg == 2u)
    {
        float MaxMarchM  = 6.50;
        float MaxStepPx  = clamp((MaxMarchM * TanSun) / TexelM, 2.5, 44.0);
        float ThicknessM = max(0.65, TexelM * 42.0);
        float Lit        = 0.0;
        float BlkDistSum = 0.0;
        uint  BlkHits    = 0u;
        for (uint R = 0u; R < 8u; ++R)
        {
            uint   StepRng = (PixelXY.x * 747796405u + PixelXY.y * 2891336453u + R * 277803737u) | 1u;
            float  StepJit = PcgFloat01(StepRng);
            float  Rad     = sqrt((float(R) + 0.5) * 0.125);
            float  Ang     = float(R) * 2.39996323 + RotPhi;
            float2 StepDir = float2(cos(Ang), sin(Ang)) * (Rad * MaxStepPx);
            bool   Occ     = false;
            float  PrevRayD = RecD;
            for (uint S = 1u; S <= 16u; ++S)
            {
                float TNorm = (float(S) - 0.5 + StepJit) / 16.0;
                float TQuad = TNorm * TNorm;
                float RayD  = RecD - MaxMarchM * TQuad;
                float2 SampleTXY = clamp(CenterTXY + StepDir * TQuad, float2(0.0), float2(float(MaxIdx)));
                int TX0 = int(floor(SampleTXY.x));
                int TY0 = int(floor(SampleTXY.y));
                int TX1 = min(TX0 + 1, MaxIdx);
                int TY1 = min(TY0 + 1, MaxIdx);
                float D00 = SolarShadowDepthBuffer[BufOffs + uint(TY0) * Res + uint(TX0)];
                float D10 = SolarShadowDepthBuffer[BufOffs + uint(TY0) * Res + uint(TX1)];
                float D01 = SolarShadowDepthBuffer[BufOffs + uint(TY1) * Res + uint(TX0)];
                float D11 = SolarShadowDepthBuffer[BufOffs + uint(TY1) * Res + uint(TX1)];
                float CascadeD = min(min(D00, D10), min(D01, D11));
                float DiffCurr = RayD - CascadeD;
                float DiffPrev = PrevRayD - CascadeD;
                if (DiffCurr > Bias && DiffPrev < ThicknessM)
                {
                    Occ = true;
                    BlkDistSum += max(0.04, RecD - CascadeD);
                    ++BlkHits;
                    break;
                }
                PrevRayD = RayD;
            }
            if (!Occ) Lit += 0.125;
        }
        OutBlockerDist = (BlkHits > 0u) ? (BlkDistSum / float(BlkHits)) : 0.0;
        return Lit;
    }

    // Algorithms 0 (ContactPcss) & 1 (PcssPlaneAware): Fine+Coarse Vogel Blocker Search + Smooth Bilinear Penumbra PCF
    float SearchRadPx = clamp((TanSun * 4.5) / TexelM, 2.75, 24.0);
    float BlockerSum  = 0.0;
    uint  Blockers    = 0u;
    for (uint Tap = 0u; Tap < 16u; ++Tap)
    {
        float R   = sqrt((float(Tap) + 0.5) / 16.0) * SearchRadPx;
        float Ang = float(Tap) * 2.39996323 + RotPhi;
        float2 SXY = CenterTXY + float2(cos(Ang), sin(Ang)) * R;
        int TX = clamp(int(floor(SXY.x)), 0, MaxIdx);
        int TY = clamp(int(floor(SXY.y)), 0, MaxIdx);
        float D = SolarShadowDepthBuffer[BufOffs + uint(TY) * Res + uint(TX)];
        float Diff = RecD - D;
        if (Diff > Bias)
        {
            BlockerSum += Diff;
            ++Blockers;
        }
    }

    float Vis = 1.0;
    if (Blockers > 0u)
    {
        float MeanBlocker = BlockerSum / float(Blockers);
        OutBlockerDist    = MeanBlocker;
        float FilterPx    = clamp((MeanBlocker * TanSun * 2.35) / TexelM, 2.25, 24.0);
        float LitSum      = 0.0;
        for (uint Tap = 0u; Tap < 24u; ++Tap)
        {
            float R   = sqrt((float(Tap) + 0.5) / 24.0) * FilterPx;
            float Ang = float(Tap) * 2.39996323 + RotPhi + R2P.y * 1.9;
            float2 SampleCoord = CenterTXY + float2(cos(Ang), sin(Ang)) * R;
            LitSum += SampleShadowVisBilinear(SampleCoord, RecD, Bias + TexelM * 0.14 * R, Res, BufOffs);
        }
        Vis = LitSum * (1.0 / 24.0);
    }

    if (ShadowAlg == 0u)
    {
        float3 CT, CB;
        ConstructOrthonormalBasis(SunDir, CT, CB);
        float3 COrig    = Hit.Position + Hit.GeomNormal * 0.006;
        float  GateVis  = 0.0;
        for (uint G = 0u; G < 3u; ++G)
        {
            float R   = sqrt((float(G) + 0.5) / 3.0) * TanSun;
            float Ang = float(G) * 2.39996323 + RotPhi;
            float3 JDir = normalize(SunDir + CT * (R * cos(Ang)) + CB * (R * sin(Ang)));
            if (!TraceSceneAnyHit(COrig, JDir, 0.003, 0.24))
            {
                GateVis += (1.0 / 3.0);
            }
        }
        Vis = min(Vis, lerp(0.03, 1.0, GateVis));
    }
    return Vis;
}

//------------------------------------------------------------------------------------------------------------------------
//                           SPECULAR REFLECTIONS (BVH4 WideTreeRaytrace or Analytical Sky Environment)
//------------------------------------------------------------------------------------------------------------------------

float3 ShadeSecondaryBounce(float3 RayOrigin, float3 RayDir)
{
    uint ReflSource = WorldUniforms.EngineShadowReflPad.z;
    if (ReflSource == 1u || !IsRaytracingEnabled())
    {
        return EvaluateSkyDome(RayDir);
    }

    RayHitReading Hit = TraceSceneClosest(RayOrigin, RayDir, 2.0e-3, 80.0);
    if (!Hit.Hit)
    {
        return EvaluateSkyDome(RayDir);
    }

    uint MatCount = max(1u, WorldUniforms.LightGlassMaterialDebug.z);
    GpuWorldMaterial80 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
    float3 V      = -RayDir;
    float3 N      = Hit.ShadingNormal;
    float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr = WorldUniforms.SolarIrradiancePad.xyz;

    float3 SafeSecondaryPos = Hit.Position + Hit.GeomNormal * 0.012 + N * 0.006;
    float3 ColorHdr = Mat.EmissiveRadiancePad.xyz;
    if (dot(N, SunDir) > 0.0 && !TraceSceneAnyHit(SafeSecondaryPos, SunDir, 0.032, 80.0))
    {
        float TermGate = saturate(dot(Hit.GeomNormal, SunDir) * 6.0 + 0.35);
        ColorHdr += EvaluateSurfaceBrdf(Mat, N, V, SunDir) * SunIrr * TermGate;
    }

    uint LightCnt = min(4u, WorldUniforms.LightGlassMaterialDebug.x);
    for (uint L = 0u; L < LightCnt; ++L)
    {
        ColorHdr += EvaluateAreaQuadLight(WorldUniforms.AreaLights[L], SafeSecondaryPos, N, V, Mat);
    }

    uint PackedPhotoCounts = WorldUniforms.DenoiseStepParams.z;
    uint PhotoProfileCount = PackedPhotoCounts >> 16u;
    uint PhotoLampCount    = PackedPhotoCounts & 0xFFFFu;
    if (PhotoProfileCount > 0u && PhotoLampCount > 0u)
    {
        for (uint PL = 0u; PL < PhotoLampCount; ++PL)
        {
            ColorHdr += EvaluatePhotometricLampGpu(PhotometricLampEntries[PL], PhotoProfileCount, SafeSecondaryPos, N, V, Mat);
        }
    }

    float3 Albedo  = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - saturate(Mat.SpecParams.y)) * (1.0 - saturate(Mat.CoatRoughnessOptics.y));
    float3 SkyFill = EvaluateSkyDome(N) * 0.22;
    ColorHdr += Albedo * SkyFill;
    return ColorHdr;
}

float3 EvaluateMatCapSurface(RayHitReading Hit)
{
    uint MatCount = max(1u, WorldUniforms.LightGlassMaterialDebug.z);
    GpuWorldMaterial80 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
    float3 N      = Hit.ShadingNormal;
    float3 CamR   = WorldUniforms.CameraRightTanY.xyz;
    float3 CamU   = WorldUniforms.CameraUpWidth.xyz;
    float3 CamFwd = WorldUniforms.CameraForwardHeight.xyz;

    float3 Vn = normalize(float3(dot(N, CamR), dot(N, CamU), max(0.0, dot(N, -CamFwd))));
    float3 KeyDir  = normalize(float3(0.42, 0.62, 0.66));
    float3 FillDir = normalize(float3(-0.68, 0.25, 0.68));
    float3 RimDir  = normalize(float3(0.0, 0.85, 0.52));

    float KeyDiff  = max(0.0, dot(Vn, KeyDir));
    float FillDiff = max(0.0, dot(Vn, FillDir));
    float RimGlow  = pow(saturate(1.0 - Vn.z), 2.6) * (0.35 + 0.65 * max(0.0, dot(Vn, RimDir)));
    float SpecPow  = lerp(96.0, 6.0, clamp(Mat.SpecParams.x, 0.02, 1.0));
    float KeySpec  = pow(max(0.0, dot(Vn, normalize(KeyDir + float3(0.0, 0.0, 1.0)))), SpecPow);
    float SpecGain = lerp(1.45, 0.18, saturate(Mat.SpecParams.x));
    float Metal    = saturate(Mat.SpecParams.y);
    float StudioL  = 0.14 + 0.88 * KeyDiff + 0.34 * FillDiff;

    float3 Base = Mat.DiffuseAlbedoRoughness.xyz;
    return Base * (StudioL * (1.0 - Metal * 0.78) + KeySpec * SpecGain * Metal)
         + float3(KeySpec * SpecGain * (1.0 - Metal))
         + float3(0.26, 0.28, 0.32) * RimGlow
         + Mat.EmissiveRadiancePad.xyz;
}

void ShadePrimarySurfaceDemod(
    RayHitReading Hit,
    float3        ViewDir,
    uint2         PixelXY,
    out float3    OutDirectBaseHdr,
    out float3    OutSpecReflHdr,
    out float3    OutUnoccludedSunHdr,
    out float3    OutDiffuseAlbedoMatte,
    out float     OutSunVis,
    out float     OutBlockerDist,
    out float     OutDiffAo,
    out float     OutSpecRough,
    out float     OutSpecAo)
{
    uint MatCount = max(1u, WorldUniforms.LightGlassMaterialDebug.z);
    GpuWorldMaterial80 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
    float3 N      = Hit.ShadingNormal;
    float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr = WorldUniforms.SolarIrradiancePad.xyz;

    // 1. Direct Solar Shadow
    OutDirectBaseHdr      = Mat.EmissiveRadiancePad.xyz;
    OutSpecReflHdr        = float3(0.0);
    OutUnoccludedSunHdr   = float3(0.0);
    OutSunVis             = 1.0;
    OutBlockerDist        = 0.0;
    float3 SafePrimaryPos = Hit.Position + Hit.GeomNormal * 0.012 + N * 0.006;
    float  NdotL          = dot(N, SunDir);
    if (NdotL > 1.0e-5)
    {
        float EnvSunTrans = EvaluateSolarCloudTransmittanceGpu(Hit.Position, SunDir);
        OutUnoccludedSunHdr = EvaluateSurfaceBrdf(Mat, N, ViewDir, SunDir) * SunIrr * EnvSunTrans;
        OutSunVis           = EvaluateSolarShadow(Hit, SunDir, PixelXY, OutBlockerDist);
    }
    else
    {
        OutSunVis = 0.0;
    }

    // 2. Upper-Hemisphere Clipped Polygonal LTC Quad Area Lights
    uint LightCnt = min(4u, WorldUniforms.LightGlassMaterialDebug.x);
    for (uint L = 0u; L < LightCnt; ++L)
    {
        OutDirectBaseHdr += EvaluateAreaQuadLight(WorldUniforms.AreaLights[L], SafePrimaryPos, N, ViewDir, Mat);
    }

    // 2b. ANSI/IESNA LM-63 Type C Photometric Point Emitters (128x32 Candela Table + BVH4 Ray Occlusion)
    uint PackedPhotoCounts = WorldUniforms.DenoiseStepParams.z;
    uint PhotoProfileCount = PackedPhotoCounts >> 16u;
    uint PhotoLampCount    = PackedPhotoCounts & 0xFFFFu;
    if (PhotoProfileCount > 0u && PhotoLampCount > 0u)
    {
        for (uint PL = 0u; PL < PhotoLampCount; ++PL)
        {
            OutDirectBaseHdr += EvaluatePhotometricLampGpu(PhotometricLampEntries[PL], PhotoProfileCount, SafePrimaryPos, N, ViewDir, Mat);
        }
    }

    // 3. Fast BVH4 Ambient Occlusion (IGN-jittered 8-ray) + Lagarde Specular AO
    float2 R2P = PixelR2(PixelXY);
    {
        float3 T, B;
        ConstructOrthonormalBasis(N, T, B);
        float3 AoOrig = Hit.Position + Hit.GeomNormal * 0.008;
        float  OccSum = 0.0;
        for (uint R = 0u; R < 8u; ++R)
        {
            float U1 = (float(R) + 0.15 + 0.7 * R2P.y) * 0.125;
            float Phi = float(R) * 2.39996323 + R2P.x * TauConst;
            float SinT = sqrt(saturate(U1));
            float CosT = sqrt(max(0.0, 1.0 - U1));
            float3 AoDir = T * (SinT * cos(Phi)) + B * (SinT * sin(Phi)) + N * CosT;
            if (!TraceSceneAnyHit(AoOrig, AoDir, 0.006, 0.85))
            {
                OccSum += 0.125;
            }
        }
        OutDiffAo = saturate(0.28 + 0.72 * OccSum);
    }
    float NdotV  = max(1.0e-3, dot(N, ViewDir));
    OutSpecRough = clamp(Mat.SpecParams.x, 0.025, 1.0);
    OutSpecAo    = saturate(pow(NdotV + OutDiffAo, exp2(-16.0 * OutSpecRough - 1.0)) - 1.0 + OutDiffAo);

    // 4. Matte Diffuse Albedo (SkyFill evaluated cleanly in DenoiseCompositeEntry)
    float Metalness       = saturate(Mat.SpecParams.y);
    float TransW          = saturate(Mat.CoatRoughnessOptics.y);
    OutDiffuseAlbedoMatte = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - Metalness) * (1.0 - TransW);

    // 5. Secondary Specular Reflection (BVH4 WideTreeRaytrace or Analytical Sky Dome)
    uint  EngineMode = WorldUniforms.EngineShadowReflPad.x;
    float CoatW      = saturate(Mat.EdgeTintCoatWeight.w);
    if (IsReflectionsEnabled() && (Metalness > 0.20 || CoatW > 0.20 || OutSpecRough < 0.24 || EngineMode == 3u))
    {
        float3 ReflDir = reflect(-ViewDir, N);
        if (EngineMode == 3u && OutSpecRough > 0.04)
        {
            float3 BasisT, BasisB;
            ConstructOrthonormalBasis(N, BasisT, BasisB);
            float3 ViewLoc   = float3(dot(ViewDir, BasisT), dot(ViewDir, BasisB), max(1.0e-4, dot(ViewDir, N)));
            float  Alpha     = max(1.0e-3, OutSpecRough * OutSpecRough);
            float3 Stretched = normalize(float3(Alpha * ViewLoc.x, Alpha * ViewLoc.y, ViewLoc.z));
            float  LenSq     = Stretched.x * Stretched.x + Stretched.y * Stretched.y;
            float3 DiskT1    = (LenSq > 1.0e-10) ? (float3(-Stretched.y, Stretched.x, 0.0) * rsqrt(LenSq)) : float3(1.0, 0.0, 0.0);
            float3 DiskT2    = cross(Stretched, DiskT1);
            float  Rad       = sqrt(saturate(R2P.x));
            float  Phi       = TauConst * R2P.y;
            float  P1        = Rad * cos(Phi);
            float  P2        = Rad * sin(Phi);
            float  Blend     = 0.5 * (1.0 + Stretched.z);
            P2               = (1.0 - Blend) * sqrt(max(0.0, 1.0 - P1 * P1)) + Blend * P2;
            float3 Hemi      = P1 * DiskT1 + P2 * DiskT2 + sqrt(max(0.0, 1.0 - P1 * P1 - P2 * P2)) * Stretched;
            float3 HalfLoc   = normalize(float3(Alpha * Hemi.x, Alpha * Hemi.y, max(1.0e-5, Hemi.z)));
            float3 HalfWorld = normalize(BasisT * HalfLoc.x + BasisB * HalfLoc.y + N * HalfLoc.z);
            float3 CandDir   = reflect(-ViewDir, HalfWorld);
            if (dot(CandDir, Hit.GeomNormal) > 0.01)
            {
                ReflDir = CandDir;
            }
        }
        float3 EnvRad  = ShadeSecondaryBounce(Hit.Position + Hit.GeomNormal * 3.0e-3, ReflDir);
        if (OutSpecRough > 0.12)
        {
            float MaxSecondary = lerp(32.0, 3.5, saturate((OutSpecRough - 0.12) / 0.68));
            EnvRad = min(EnvRad, float3(MaxSecondary));
        }
        float  Ior     = max(1.02, Mat.SpecParams.z);
        float  F0Diel  = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
        float3 F0      = lerp(float3(F0Diel), Mat.DiffuseAlbedoRoughness.xyz, Metalness);
        float  Schlick = pow(1.0 - NdotV, 5.0);
        float3 Fresnel = F0 + (float3(1.0) - F0) * Schlick;
        float  Gloss   = saturate(1.0 - OutSpecRough * 0.85);
        if (CoatW > 0.15 && Metalness < 0.2)
        {
            Fresnel = max(Fresnel, float3((0.04 + 0.96 * Schlick) * CoatW));
            Gloss   = max(Gloss, 1.0 - Mat.CoatRoughnessOptics.x);
        }
        OutSpecReflHdr = EnvRad * Fresnel * Gloss;
    }

    // 6. Order 2 Real-Time Indirect Diffuse Bounce GI (Engines 2 = GtxLowEnd, 3 = HighEnd)
    if ((EngineMode == 2u || EngineMode == 3u) && dot(OutDiffuseAlbedoMatte, float3(1.0)) > 1.0e-4)
    {
        uint   BounceRays  = (EngineMode == 3u) ? 8u : 4u;
        float3 BasisT, BasisB;
        ConstructOrthonormalBasis(N, BasisT, BasisB);
        float3 BounceOrig  = Hit.Position + Hit.GeomNormal * 0.014 + N * 0.006;
        float3 IndirectSum = float3(0.0);
        for (uint B = 0u; B < BounceRays; ++B)
        {
            float  U1   = frac((float(B) + 0.5) / float(BounceRays) + R2P.x);
            float  U2   = frac(float(B) * 0.6180339887 + R2P.y);
            float  SinT = sqrt(saturate(U1));
            float  CosT = sqrt(max(0.0, 1.0 - U1));
            float  Phi  = TauConst * U2;
            float3 Dir  = normalize(BasisT * (SinT * cos(Phi)) + BasisB * (SinT * sin(Phi)) + N * CosT);
            float3 Rad  = min(ShadeSecondaryBounce(BounceOrig, Dir), float3(6.5));
            IndirectSum += Rad;
        }
        OutDirectBaseHdr += OutDiffuseAlbedoMatte * (IndirectSum / float(BounceRays));
    }
}

bool IntersectGlassSphere(float3 Origin, float3 Dir, float3 Center, float Radius, out float TEnter, out float TExit)
{
    TEnter = -1.0;
    TExit  = -1.0;
    float3 Oc   = Origin - Center;
    float  B    = dot(Oc, Dir);
    float  C    = dot(Oc, Oc) - Radius * Radius;
    float  Disc = B * B - C;
    if (Disc <= 0.0) return false;
    float SqrtD = sqrt(Disc);
    TEnter      = -B - SqrtD;
    TExit       = -B + SqrtD;
    return TExit > 1.0e-3;
}

int FindClosestGlassSphere(float3 RayO, float3 RayD, float MinT, float MaxT, out float OutT)
{
    OutT = MaxT;
    int  BestIdx  = -1;
    uint GlassCnt = min(4u, WorldUniforms.LightGlassMaterialDebug.y);
    for (uint G = 0u; G < GlassCnt; ++G)
    {
        float TEn = 0.0, TEx = 0.0;
        if (IntersectGlassSphere(
                RayO,
                RayD,
                WorldUniforms.GlassSpheres[G].CenterRadiusPacked.xyz,
                WorldUniforms.GlassSpheres[G].CenterRadiusPacked.w,
                TEn,
                TEx))
        {
            float HitT = (TEn > MinT) ? TEn : TEx;
            if (HitT > MinT && HitT < OutT)
            {
                OutT    = HitT;
                BestIdx = int(G);
            }
        }
    }
    return BestIdx;
}

float3 TraceThroughGlassSphere(float3 EntryPos, float3 EntryNrm, float3 IncidentDir, float3 Center, float Radius, float IorChannel)
{
    float3 BounceOrig = EntryPos + EntryNrm * 2.5e-3;
    float3 BounceDir  = reflect(IncidentDir, EntryNrm);
    float3 InsideDir  = refract(IncidentDir, EntryNrm, 1.0 / IorChannel);
    if (dot(InsideDir, InsideDir) >= 1.0e-5)
    {
        float TIn = 0.0, TOut = 0.0;
        if (IntersectGlassSphere(EntryPos + InsideDir * 2.0e-3, InsideDir, Center, Radius, TIn, TOut) && TOut > 0.0)
        {
            float3 ExitPos = EntryPos + InsideDir * (2.0e-3 + TOut);
            float3 OutNrm  = normalize(ExitPos - Center);
            float3 ExitDir = refract(InsideDir, -OutNrm, IorChannel);
            if (dot(ExitDir, ExitDir) < 1.0e-5)
            {
                ExitDir = reflect(InsideDir, -OutNrm);
            }
            BounceOrig = ExitPos + OutNrm * 3.0e-3;
            BounceDir  = normalize(ExitDir);
        }
        else
        {
            BounceOrig = EntryPos + InsideDir * (Radius * 2.1);
            BounceDir  = InsideDir;
        }
    }
    return ShadeSecondaryBounce(BounceOrig, BounceDir);
}

float3 ApplyAcesFilmicGpu(float3 Hdr)
{
    float3 V = max(float3(0.0), Hdr * 0.85);
    return saturate((V * (2.51 * V + 0.03)) / (V * (2.43 * V + 0.59) + 0.14));
}

uint PackLinearToSrgb8Rgba(float3 LinearColor, uint2 PixelXY)
{
    float3 FiniteCol = select(isnan(LinearColor) | isinf(LinearColor), float3(0.0), LinearColor);
    float3 C         = saturate(FiniteCol);
    float3 Srgb      = select(C <= float3(0.0031308), C * 12.92, 1.055 * pow(max(C, float3(1.0e-6)), float3(1.0 / 2.4)) - 0.055);
    float  IgnA = frac(52.9829189 * frac(float(PixelXY.x) * 0.06711056 + float(PixelXY.y) * 0.00583715));
    float  IgnB = frac(52.9829189 * frac(float(PixelXY.x + 17u) * 0.06711056 + float(PixelXY.y + 31u) * 0.00583715));
    float  Tpdf = (IgnA + IgnB - 1.0) / 255.0;
    uint3  Byte = uint3(clamp(round((Srgb + float3(Tpdf)) * 255.0), float3(0.0), float3(255.0)));
    return Byte.x | (Byte.y << 8u) | (Byte.z << 16u) | (255u << 24u);
}

float3 EvaluateVolumetricGodRaysGpu(
    float3 RayOrigin,
    float3 RayDir,
    float  MaxDistM,
    float3 SunDir,
    float3 SolarIrrad,
    float2 PixelCoord)
{
    VolumetricFogUniformBlock Fog = WorldUniforms.FogParams;
    if (Fog.GodRayParams0.x < 0.5 || Fog.GodRayParams0.y <= 1.0e-4 || SunDir.z <= 0.005 || MaxDistM <= 0.25)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float  RangeM  = min(max(0.25, MaxDistM), max(10.0, Fog.GodRayParams1.x));
    uint   Steps   = clamp(uint(Fog.GodRayParams1.y + 0.5), 8u, 48u);
    float  Dt      = (RangeM - 0.15) / float(Steps);
    float  Ign     = frac(52.9829189 * frac(dot(PixelCoord, float2(0.06711056, 0.00583715))));
    float  Jitter  = 0.5 + 0.72 * (Ign - 0.5);

    float  CosTheta   = dot(RayDir, SunDir);
    float  G          = clamp(Fog.GodRayParams0.z, 0.0, 0.94);
    float  G2         = G * G;
    float  Den        = max(1.0e-3, 1.0 + G2 - 2.0 * G * CosTheta);
    float  MiePhase   = (1.0 - G2) / (4.0 * PiConst * pow(Den, 1.5));
    float  Phase      = 0.85 * MiePhase + 0.15 * (1.0 / (4.0 * PiConst));
    float  DecayScale = max(15.0, Fog.GodRayParams1.x * (1.35 - 0.85 * saturate(Fog.GodRayTint.w)));

    float3 Accum    = float3(0.0, 0.0, 0.0);
    float  CamTrans = 1.0;
    bool   DoClouds = (Fog.GodRayParams1.z > 0.5);
    bool   DoFog    = (Fog.GodRayParams1.w > 0.5);
    float  BaseDen  = max(0.0, Fog.GodRayParams0.w);

    for (uint S = 0u; S < Steps; ++S)
    {
        float  T = 0.15 + (float(S) + Jitter) * Dt;
        float3 P = RayOrigin + RayDir * T;

        if (TraceSceneAnyHit(P + SunDir * 0.02, SunDir, 0.02, 140.0))
        {
            continue;
        }
        float SceneVis = 1.0;
        float RawCloudVis = DoClouds ? EvaluateSolarCloudTransmittanceGpu(P, SunDir) : 1.0;
        float CloudVis    = DoClouds ? pow(saturate((RawCloudVis - 0.12) / 0.88), 2.35) : 1.0;

        float MediumSigma = 0.0048 * BaseDen;
        if (DoFog)
        {
            float FogRho = EvaluateFogMediumDensityAtPointGpu(Fog, P);
            MediumSigma += FogRho * 0.68 * max(0.1, BaseDen);
        }

        float DecayW = exp(-T / DecayScale);
        float StepWt = CamTrans * SceneVis * CloudVis * MediumSigma * Phase * Fog.GodRayParams0.y * DecayW * Dt * 0.72;
        Accum       += SolarIrrad * Fog.GodRayTint.xyz * StepWt;
        CamTrans    *= exp(-MediumSigma * Dt * 0.35);
        if (CamTrans < 0.02) break;
    }

    return Accum;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           COMPUTE ENTRYPOINTS
//------------------------------------------------------------------------------------------------------------------------

float3 EvaluateProgressiveViewportPath(float3 CamOrigin, float3 PrimaryDir, uint2 PixelXY, uint SampleIndex, uint EngineMode)
{
    uint   MaxBounces = (EngineMode == 5u) ? 4u : 3u;
    uint   MatCount   = max(1u, WorldUniforms.LightGlassMaterialDebug.z);
    float3 SunDir     = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr     = WorldUniforms.SolarIrradiancePad.xyz;
    float  TanSun     = max(0.004, WorldUniforms.SunDirConeTan.w);
    uint   LightCnt   = min(4u, WorldUniforms.LightGlassMaterialDebug.x);
    uint   PackedPhot = WorldUniforms.DenoiseStepParams.z;
    uint   PhotProfs  = PackedPhot >> 16u;
    uint   PhotLamps  = PackedPhot & 0xFFFFu;

    float2 BaseShift = (EngineMode == 4u)
        ? PixelR2(PixelXY)
        : float2(0.0, 0.0);

    float3 RayO       = CamOrigin;
    float3 RayD       = PrimaryDir;
    float3 Throughput = float3(1.0, 1.0, 1.0);
    float3 Radiance   = float3(0.0, 0.0, 0.0);

    for (uint Bounce = 0u; Bounce < MaxBounces; ++Bounce)
    {
        RayHitReading Hit   = TraceSceneClosest(RayO, RayD, (Bounce == 0u) ? 0.08 : 0.003, 120.0);
        float         TriT  = Hit.Hit ? Hit.Distance : 120.0;
        float         GlsT  = TriT;
        int           GlsId = FindClosestGlassSphere(RayO, RayD, (Bounce == 0u) ? 0.08 : 0.003, TriT, GlsT);

        if (GlsId >= 0)
        {
            GpuGlassSphere Glass  = WorldUniforms.GlassSpheres[GlsId];
            float3         EntryP = RayO + RayD * GlsT;
            float3         EntryN = normalize(EntryP - Glass.CenterRadiusPacked.xyz);
            float          Ior    = max(1.03, Glass.OpticsSlotPacked.x);
            Radiance += Throughput * TraceThroughGlassSphere(EntryP, EntryN, RayD, Glass.CenterRadiusPacked.xyz, Glass.CenterRadiusPacked.w, Ior);
            break;
        }

        if (!Hit.Hit)
        {
            Radiance += Throughput * ((Bounce == 0u) ? EvaluateBackgroundSkyCloudsGpu(RayD, float2(PixelXY)) : EvaluateSkyDome(RayD));
            break;
        }

        GpuWorldMaterial80 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
        float3 N        = Hit.ShadingNormal;
        float3 V        = -RayD;
        float3 SafePos  = Hit.Position + Hit.GeomNormal * 0.012 + N * 0.006;
        float3 Emissive = Mat.EmissiveRadiancePad.xyz;
        if (Bounce > 0u)
        {
            Emissive = min(Emissive, float3(18.0));
        }
        Radiance += Throughput * Emissive;

        float2 Xi = frac(BaseShift + float2(
            frac(float(SampleIndex * 7u + Bounce * 3u + 1u) * 0.7548776662),
            frac(float(SampleIndex * 7u + Bounce * 3u + 1u) * 0.5698402910)));

        // Direct solar NEE with soft solar-disc jitter
        if (dot(N, SunDir) > 1.0e-5)
        {
            float3 SunT, SunB;
            ConstructOrthonormalBasis(SunDir, SunT, SunB);
            float  DiscR  = sqrt(Xi.x) * TanSun;
            float  DiscPh = TauConst * Xi.y;
            float3 JitSun = normalize(SunDir + SunT * (DiscR * cos(DiscPh)) + SunB * (DiscR * sin(DiscPh)));
            if (dot(N, JitSun) > 0.0 && !TraceSceneAnyHit(SafePos, JitSun, 0.025, 110.0))
            {
                float CloudTr = EvaluateSolarCloudTransmittanceGpu(Hit.Position, SunDir);
                Radiance += Throughput * EvaluateSurfaceBrdf(Mat, N, V, JitSun) * SunIrr * CloudTr;
            }
        }

        // Direct area & photometric luminaires
        for (uint L = 0u; L < LightCnt; ++L)
        {
            Radiance += Throughput * EvaluateAreaQuadLight(WorldUniforms.AreaLights[L], SafePos, N, V, Mat);
        }
        if (PhotProfs > 0u && PhotLamps > 0u)
        {
            for (uint PL = 0u; PL < PhotLamps; ++PL)
            {
                Radiance += Throughput * EvaluatePhotometricLampGpu(PhotometricLampEntries[PL], PhotProfs, SafePos, N, V, Mat);
            }
        }

        // BSDF stochastic bounce selection (specular vs diffuse)
        float  Rough    = clamp(Mat.SpecParams.x, 0.025, 1.0);
        float  Metal    = saturate(Mat.SpecParams.y);
        float  TransW   = saturate(Mat.CoatRoughnessOptics.y);
        float  CoatW    = saturate(Mat.EdgeTintCoatWeight.w);
        float  NdotV    = max(1.0e-4, dot(N, V));
        float  Ior      = max(1.02, Mat.SpecParams.z);
        float  F0Scalar = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
        float3 F0       = lerp(float3(F0Scalar), Mat.DiffuseAlbedoRoughness.xyz, Metal);
        float3 Fresnel  = F0 + (float3(1.0) - F0) * pow(1.0 - NdotV, 5.0);
        if (CoatW > 0.15 && Metal < 0.2)
        {
            Fresnel = max(Fresnel, float3((0.04 + 0.96 * pow(1.0 - NdotV, 5.0)) * CoatW));
        }
        float3 DiffAlb  = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - Metal) * (1.0 - TransW);
        float  SpecLuma = dot(Fresnel, float3(0.2126, 0.7152, 0.0722));
        float  DiffLuma = dot(DiffAlb, float3(0.2126, 0.7152, 0.0722));
        float  SpecProb = clamp(SpecLuma / max(1.0e-4, SpecLuma + DiffLuma), 0.10, 0.92);

        float2 BounceXi = frac(BaseShift + float2(
            frac(float(SampleIndex * 11u + Bounce * 5u + 2u) * 0.6180339887),
            frac(float(SampleIndex * 11u + Bounce * 5u + 2u) * 0.3247179572)));
        float  LobeSel  = frac(BaseShift.x * 0.73 + BaseShift.y * 0.37 + float(SampleIndex * 3u + Bounce) * 0.41421356);

        float3 BasisT, BasisB;
        ConstructOrthonormalBasis(N, BasisT, BasisB);
        if (LobeSel < SpecProb)
        {
            float3 ViewLoc   = float3(dot(V, BasisT), dot(V, BasisB), max(1.0e-4, dot(V, N)));
            float  Alpha     = max(1.0e-3, Rough * Rough);
            float3 Stretched = normalize(float3(Alpha * ViewLoc.x, Alpha * ViewLoc.y, ViewLoc.z));
            float  LenSq     = Stretched.x * Stretched.x + Stretched.y * Stretched.y;
            float3 DiskT1    = (LenSq > 1.0e-10) ? (float3(-Stretched.y, Stretched.x, 0.0) * rsqrt(LenSq)) : float3(1.0, 0.0, 0.0);
            float3 DiskT2    = cross(Stretched, DiskT1);
            float  Rad       = sqrt(saturate(BounceXi.x));
            float  Phi       = TauConst * BounceXi.y;
            float  P1        = Rad * cos(Phi);
            float  P2        = Rad * sin(Phi);
            float  Blend     = 0.5 * (1.0 + Stretched.z);
            P2               = (1.0 - Blend) * sqrt(max(0.0, 1.0 - P1 * P1)) + Blend * P2;
            float3 Hemi      = P1 * DiskT1 + P2 * DiskT2 + sqrt(max(0.0, 1.0 - P1 * P1 - P2 * P2)) * Stretched;
            float3 HalfLoc   = normalize(float3(Alpha * Hemi.x, Alpha * Hemi.y, max(1.0e-5, Hemi.z)));
            float3 HalfWorld = normalize(BasisT * HalfLoc.x + BasisB * HalfLoc.y + N * HalfLoc.z);
            RayD             = reflect(-V, HalfWorld);
            if (dot(RayD, Hit.GeomNormal) <= 0.0)
            {
                break;
            }
            Throughput *= Fresnel / SpecProb;
        }
        else
        {
            float  SinT = sqrt(saturate(BounceXi.x));
            float  CosT = sqrt(max(0.0, 1.0 - BounceXi.x));
            float  Phi  = TauConst * BounceXi.y;
            RayD        = normalize(BasisT * (SinT * cos(Phi)) + BasisB * (SinT * sin(Phi)) + N * CosT);
            if (dot(RayD, Hit.GeomNormal) <= 0.0)
            {
                break;
            }
            Throughput *= DiffAlb / (1.0 - SpecProb);
        }
        RayO = Hit.Position + Hit.GeomNormal * 0.006;
    }
    return min(Radiance, float3(32.0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CloudComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint CW = uint(WorldUniforms.CloudParams.CloudPassControl.x + 0.5);
    uint CH = uint(WorldUniforms.CloudParams.CloudPassControl.y + 0.5);
    if (CW == 0u || CH == 0u || DispatchThreadID.x >= CW || DispatchThreadID.y >= CH)
    {
        return;
    }

    uint PIdx = DispatchThreadID.y * CW + DispatchThreadID.x;
    if (WorldUniforms.AtmosphereMaskPad.x == 0u || WorldUniforms.CloudParams.SunAxisToggle.w < 0.5)
    {
        CloudRadiometricBuffer[PIdx] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float2 PixelUv = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(CW), float(CH));
    float  NdcX    = PixelUv.x * 2.0 - 1.0;
    float  NdcY    = 1.0 - PixelUv.y * 2.0;
    float3 RayDir  = normalize(
        WorldUniforms.CameraForwardHeight.xyz +
        WorldUniforms.CameraRightTanY.xyz * (NdcX * WorldUniforms.CameraOriginTanX.w) +
        WorldUniforms.CameraUpWidth.xyz   * (NdcY * WorldUniforms.CameraRightTanY.w));

    float  CloudTrans = 1.0;
    float3 CloudRad   = EvaluatePlanetaryCloudsOnlyGpu(
        WorldUniforms.CloudParams, RayDir, float2(DispatchThreadID.xy), CloudTrans);
    CloudRadiometricBuffer[PIdx] = float4(CloudRad, CloudTrans);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width  = uint(WorldUniforms.CameraUpWidth.w + 0.5);
    uint Height = uint(WorldUniforms.CameraForwardHeight.w + 0.5);
    if (DispatchThreadID.x >= Width || DispatchThreadID.y >= Height)
    {
        return;
    }

    uint   PixelIdx   = DispatchThreadID.y * Width + DispatchThreadID.x;
    uint   EngineMode = WorldUniforms.EngineShadowReflPad.x;
    uint   DebugMode  = WorldUniforms.LightGlassMaterialDebug.w;
    float3 CamOrigin  = WorldUniforms.CameraOriginTanX.xyz;

    float2 PixelUv = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(Width), float(Height));
    float  NdcX    = PixelUv.x * 2.0 - 1.0;
    float  NdcY    = 1.0 - PixelUv.y * 2.0;
    float3 RayDir  = normalize(
        WorldUniforms.CameraForwardHeight.xyz +
        WorldUniforms.CameraRightTanY.xyz * (NdcX * WorldUniforms.CameraOriginTanX.w) +
        WorldUniforms.CameraUpWidth.xyz   * (NdcY * WorldUniforms.CameraRightTanY.w));

    RayHitReading PrimaryHit = TraceSceneClosest(CamOrigin, RayDir, 0.08, 120.0);

    float TriHitT      = PrimaryHit.Hit ? PrimaryHit.Distance : 120.0;
    float BestGlassT   = TriHitT;
    int   BestGlassIdx = -1;
    if (EngineMode != 0u)
    {
        BestGlassIdx = FindClosestGlassSphere(CamOrigin, RayDir, 0.08, TriHitT, BestGlassT);
    }

    float HitDistForClouds = (BestGlassIdx >= 0) ? BestGlassT : (PrimaryHit.Hit ? PrimaryHit.Distance : 60000.0);
    float HitDistForFog    = (BestGlassIdx >= 0) ? BestGlassT : (PrimaryHit.Hit ? PrimaryHit.Distance : 3500.0);

    bool UsesShadingDebug = (DebugMode >= 8u && DebugMode <= 11u);
    if (DebugMode != 0u && !UsesShadingDebug)
    {
        float3 DbgHdr = float3(0.0);
        if (BestGlassIdx >= 0)
        {
            GpuGlassSphere Glass = WorldUniforms.GlassSpheres[BestGlassIdx];
            float3 HitPos = CamOrigin + RayDir * BestGlassT;
            float3 HitNrm = normalize(HitPos - Glass.CenterRadiusPacked.xyz);
            if (DebugMode == 1u)      DbgHdr = float3(pow(saturate(1.0 - BestGlassT / 11.0), 1.8));
            else if (DebugMode == 2u) DbgHdr = float3(0.95, 0.98, 1.00);
            else if (DebugMode == 3u) DbgHdr = HitNrm * 0.5 + 0.5;
            else if (DebugMode == 4u) DbgHdr = float3(clamp(Glass.OpticsSlotPacked.w, 0.025, 1.0));
            else if (DebugMode == 5u) DbgHdr = float3(0.0);
            else if (DebugMode == 6u) DbgHdr = float3(0.28, 0.85, 0.96);
            else if (DebugMode == 7u) DbgHdr = HitNrm * 0.5 + 0.5;
        }
        else if (PrimaryHit.Hit)
        {
            uint MatCount = max(1u, WorldUniforms.LightGlassMaterialDebug.z);
            GpuWorldMaterial80 Mat = MaterialEntries[min(PrimaryHit.MaterialSlot, MatCount - 1u)];
            if (DebugMode == 1u)      DbgHdr = float3(pow(saturate(1.0 - PrimaryHit.Distance / 11.0), 1.8));
            else if (DebugMode == 2u) DbgHdr = Mat.DiffuseAlbedoRoughness.xyz;
            else if (DebugMode == 3u) DbgHdr = PrimaryHit.ShadingNormal * 0.5 + 0.5;
            else if (DebugMode == 4u) DbgHdr = float3(Mat.SpecParams.x);
            else if (DebugMode == 5u) DbgHdr = float3(saturate(Mat.SpecParams.y));
            else if (DebugMode == 6u)
            {
                uint SId = PrimaryHit.MaterialSlot + 1u;
                float H1 = frac(float(SId) * 0.3819660113);
                float H2 = frac(float(SId) * 0.6180339887 + 0.27);
                float H3 = frac(float(SId) * 0.7548776662 + 0.53);
                DbgHdr = float3(0.18 + 0.78 * H1, 0.18 + 0.78 * H2, 0.18 + 0.78 * H3);
            }
            else if (DebugMode == 7u) DbgHdr = PrimaryHit.GeomNormal * 0.5 + 0.5;
        }
        GBufPosMask[PixelIdx]    = float4(0.0, 0.0, 0.0, 0.0);
        GBufNormAo[PixelIdx]     = float4(0.0, 0.0, 1.0, 1.0);
        GBufDirectBase[PixelIdx] = float4(DbgHdr, HitDistForClouds);
        GBufSunContrib[PixelIdx] = float4(0.0, 0.0, 0.0, HitDistForFog);
        GBufAlbedo[PixelIdx]     = float4(0.0, 0.0, 0.0, 1.0);
        PingShadowAo[PixelIdx]   = float4(1.0, 1.0, 0.0, 1.0);
        return;
    }

    if (BestGlassIdx >= 0 && (DebugMode == 0u || DebugMode == 11u))
    {
        GpuGlassSphere Glass = WorldUniforms.GlassSpheres[BestGlassIdx];
        float3 Center        = Glass.CenterRadiusPacked.xyz;
        float  Radius        = Glass.CenterRadiusPacked.w;
        float3 HitPos        = CamOrigin + RayDir * BestGlassT;
        float3 HitNrm        = normalize(HitPos - Center);
        float3 ViewDir       = -RayDir;
        float  CrownIor      = max(1.05, Glass.OpticsSlotPacked.x);
        float  Dispersion    = clamp(0.45 / max(10.0, Glass.OpticsSlotPacked.y), 0.004, 0.028);

        float  CosI    = saturate(dot(HitNrm, ViewDir));
        float  F0      = pow((CrownIor - 1.0) / (CrownIor + 1.0), 2.0);
        float  Fresnel = IsReflectionsEnabled() ? (F0 + (1.0 - F0) * pow(1.0 - CosI, 5.0)) : 0.0;

        float3 RefrRgb = float3(0.0);
        for (int Ch = 0; Ch < 3; ++Ch)
        {
            float  ChIor = (Ch == 0) ? (CrownIor - Dispersion) : ((Ch == 1) ? CrownIor : (CrownIor + Dispersion * 1.15));
            float3 ChRad = TraceThroughGlassSphere(HitPos, HitNrm, RayDir, Center, Radius, ChIor);
            if (Ch == 0) RefrRgb.r = ChRad.r;
            else if (Ch == 1) RefrRgb.g = ChRad.g;
            else RefrRgb.b = ChRad.b;
        }
        RefrRgb *= float3(0.95, 0.98, 1.00);
        float3 ReflRgb = IsReflectionsEnabled() ? ShadeSecondaryBounce(HitPos + HitNrm * 2.5e-3, reflect(RayDir, HitNrm)) : float3(0.0);

        float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
        float3 HSun   = normalize(ViewDir + SunDir);
        float  SpecGl = IsReflectionsEnabled() ? (pow(max(0.0, dot(HitNrm, HSun)), 380.0) * 6.5) : 0.0;
        if (SpecGl > 0.0 && TraceSceneAnyHit(HitPos + HitNrm * 3.0e-3, SunDir, 2.0e-3, 60.0))
        {
            SpecGl = 0.0;
        }
        float3 GlassHdr = (DebugMode == 11u)
            ? ApplyAcesFilmicGpu(ReflRgb * Fresnel + WorldUniforms.SolarIrradiancePad.xyz * SpecGl)
            : (lerp(RefrRgb, ReflRgb, Fresnel) + WorldUniforms.SolarIrradiancePad.xyz * SpecGl);
        GBufPosMask[PixelIdx]    = float4(0.0, 0.0, 0.0, 0.0);
        GBufNormAo[PixelIdx]     = float4(HitNrm, 1.0);
        GBufDirectBase[PixelIdx] = float4(GlassHdr, HitDistForClouds);
        GBufSunContrib[PixelIdx] = float4(0.0, 0.0, 0.0, HitDistForFog);
        GBufAlbedo[PixelIdx]     = float4(0.0, 0.0, 0.0, 1.0);
        PingShadowAo[PixelIdx]   = float4(1.0, 1.0, 0.0, 1.0);
        return;
    }

    if (!PrimaryHit.Hit)
    {
        float3 SkyHdr = UsesShadingDebug ? float3(0.03, 0.04, 0.07) : EvaluateBackgroundSkyCloudsGpu(RayDir, float2(DispatchThreadID.xy));
        GBufPosMask[PixelIdx]    = float4(0.0, 0.0, 0.0, 0.0);
        GBufNormAo[PixelIdx]     = float4(0.0, 0.0, 1.0, 1.0);
        GBufDirectBase[PixelIdx] = float4(SkyHdr, HitDistForClouds);
        GBufSunContrib[PixelIdx] = float4(0.0, 0.0, 0.0, HitDistForFog);
        GBufAlbedo[PixelIdx]     = float4(0.0, 0.0, 0.0, 1.0);
        PingShadowAo[PixelIdx]   = float4(1.0, 1.0, 0.0, 1.0);
        return;
    }

    if (EngineMode == 0u && DebugMode == 0u)
    {
        float3 MatCapHdr = EvaluateMatCapSurface(PrimaryHit);
        GBufPosMask[PixelIdx]    = float4(0.0, 0.0, 0.0, 0.0);
        GBufNormAo[PixelIdx]     = float4(PrimaryHit.ShadingNormal, 1.0);
        GBufDirectBase[PixelIdx] = float4(MatCapHdr, HitDistForClouds);
        GBufSunContrib[PixelIdx] = float4(0.0, 0.0, 0.0, HitDistForFog);
        GBufAlbedo[PixelIdx]     = float4(0.0, 0.0, 0.0, 1.0);
        PingShadowAo[PixelIdx]   = float4(1.0, 1.0, 0.0, 1.0);
        return;
    }

    if ((EngineMode == 4u || EngineMode == 5u) && DebugMode == 0u)
    {
        uint   SampleIdx = WorldUniforms.EngineShadowReflPad.w;
        float3 SampleHdr = EvaluateProgressiveViewportPath(CamOrigin, RayDir, DispatchThreadID.xy, SampleIdx, EngineMode);
        float3 NextAccum = (SampleIdx == 0u)
            ? SampleHdr
            : lerp(AccumulationHdrBuffer[PixelIdx].xyz, SampleHdr, 1.0 / float(SampleIdx + 1u));
        AccumulationHdrBuffer[PixelIdx] = float4(NextAccum, float(SampleIdx + 1u));
        GBufPosMask[PixelIdx]           = float4(0.0, 0.0, 0.0, 0.0);
        GBufNormAo[PixelIdx]            = float4(PrimaryHit.ShadingNormal, 1.0);
        GBufDirectBase[PixelIdx]        = float4(NextAccum, HitDistForClouds);
        GBufSunContrib[PixelIdx]        = float4(0.0, 0.0, 0.0, HitDistForFog);
        GBufAlbedo[PixelIdx]            = float4(0.0, 0.0, 0.0, 1.0);
        PingShadowAo[PixelIdx]          = float4(1.0, 1.0, 0.0, 1.0);
        return;
    }

    float3 DirectBaseHdr      = float3(0.0);
    float3 SpecReflHdr        = float3(0.0);
    float3 UnoccludedSunHdr   = float3(0.0);
    float3 DiffuseAlbedoMatte = float3(0.0);
    float  SunVis = 1.0, BlockerDist = 0.0, DiffAo = 1.0, SpecRough = 0.5, SpecAo = 1.0;
    ShadePrimarySurfaceDemod(
        PrimaryHit,
        -RayDir,
        DispatchThreadID.xy,
        DirectBaseHdr,
        SpecReflHdr,
        UnoccludedSunHdr,
        DiffuseAlbedoMatte,
        SunVis,
        BlockerDist,
        DiffAo,
        SpecRough,
        SpecAo);

    AccumulationHdrBuffer[PixelIdx] = float4(SpecReflHdr, SpecRough);
    GBufPosMask[PixelIdx]           = float4(PrimaryHit.Position, 1.0 + SpecRough);
    GBufNormAo[PixelIdx]            = float4(PrimaryHit.ShadingNormal, DiffAo);
    GBufDirectBase[PixelIdx]        = float4(DirectBaseHdr, HitDistForClouds);
    GBufSunContrib[PixelIdx]        = float4(UnoccludedSunHdr, HitDistForFog);
    GBufAlbedo[PixelIdx]            = float4(DiffuseAlbedoMatte, SpecAo);
    PingShadowAo[PixelIdx]          = float4(SunVis, DiffAo, BlockerDist, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void DenoiseCompositeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width  = uint(WorldUniforms.CameraUpWidth.w + 0.5);
    uint Height = uint(WorldUniforms.CameraForwardHeight.w + 0.5);
    if (DispatchThreadID.x >= Width || DispatchThreadID.y >= Height)
    {
        return;
    }

    uint   PixelIdx         = DispatchThreadID.y * Width + DispatchThreadID.x;
    uint   DebugMode        = WorldUniforms.LightGlassMaterialDebug.w;
    int    StridePx         = max(1, int(WorldUniforms.DenoiseStepParams.x));
    bool   IsFinalComposite = (WorldUniforms.DenoiseStepParams.y & 1u) != 0u;
    bool   SpatialDenoise   = (WorldUniforms.DenoiseStepParams.y & 2u) != 0u;

    float4 CenterPos  = GBufPosMask[PixelIdx];
    float4 CenterNorm = GBufNormAo[PixelIdx];
    float4 InShadAo   = PingShadowAo[PixelIdx];
    float4 CenterAlb  = GBufAlbedo[PixelIdx];

    float  OutSunVis  = InShadAo.x;
    float  OutDiffAo  = InShadAo.y;
    float  OutBlocker = InShadAo.z;

    if (SpatialDenoise && CenterPos.w > 0.0)
    {
        float3 CenterN   = normalize(CenterNorm.xyz);
        float3 CamOrig   = WorldUniforms.CameraOriginTanX.xyz;
        float  CamDist   = max(0.25, length(CenterPos.xyz - CamOrig));
        float  TanSun    = max(0.004, WorldUniforms.SunDirConeTan.w);
        float  EdgeBlock = (OutSunVis > 0.005 && OutSunVis < 0.995) ? max(OutBlocker, 0.30) : OutBlocker;
        float  Penumbra  = EdgeBlock * TanSun;
        float  SigmaSh   = clamp(1.10 + (Penumbra / CamDist) * float(Height) * 1.65, 1.10, 9.0);
        float  Inv2SigS  = 1.0 / (2.0 * SigmaSh * SigmaSh);
        float  SigmaAo   = max(1.0, float(StridePx) * 1.15);
        float  Inv2SigA  = 1.0 / (2.0 * SigmaAo * SigmaAo);

        float  SumVis   = 0.0;
        float  SumWVis  = 0.0;
        float  SumAo    = 0.0;
        float  SumWAo   = 0.0;
        float  MaxBlock = OutBlocker;

        for (int KY = -1; KY <= 1; ++KY)
        {
            for (int KX = -1; KX <= 1; ++KX)
            {
                int2 SCoord = clamp(int2(DispatchThreadID.xy) + int2(KX, KY) * StridePx,
                                    int2(0, 0),
                                    int2(int(Width) - 1, int(Height) - 1));
                uint SIdx   = uint(SCoord.y) * Width + uint(SCoord.x);
                float4 NPos = GBufPosMask[SIdx];
                if (NPos.w <= 0.0) continue;

                float3 NNrm   = normalize(GBufNormAo[SIdx].xyz);
                float4 NShAo  = PingShadowAo[SIdx];

                float DistSq    = float((KX * StridePx) * (KX * StridePx) + (KY * StridePx) * (KY * StridePx));
                float PlaneDist = abs(dot(CenterN, NPos.xyz - CenterPos.xyz));
                float WPlane    = exp(-PlaneDist * 65.0);
                float WNrm      = pow(saturate(dot(CenterN, NNrm)), 24.0);
                float WGeom     = WPlane * WNrm;

                float WVis = exp(-DistSq * Inv2SigS) * WGeom;
                SumVis  += NShAo.x * WVis;
                SumWVis += WVis;

                float WAo = exp(-DistSq * Inv2SigA) * WGeom;
                SumAo  += NShAo.y * WAo;
                SumWAo += WAo;

                if (PlaneDist < 0.03 && WNrm > 0.5)
                {
                    MaxBlock = max(MaxBlock, NShAo.z);
                }
            }
        }

        OutSunVis  = (SumWVis > 1.0e-6) ? (SumVis / SumWVis) : InShadAo.x;
        OutDiffAo  = (SumWAo  > 1.0e-6) ? (SumAo  / SumWAo)  : InShadAo.y;
        OutBlocker = MaxBlock;
    }

    if (!IsFinalComposite)
    {
        PongShadowAo[PixelIdx] = float4(OutSunVis, OutDiffAo, OutBlocker, InShadAo.w);
        return;
    }

    float4 DirectBase = GBufDirectBase[PixelIdx];
    float4 SunContrib = GBufSunContrib[PixelIdx];
    float4 AlbedoPack = CenterAlb;

    float3 FinalHdr = DirectBase.xyz;
    if (CenterPos.w > 0.0 && DebugMode == 0u)
    {
        float3 CamOrigin = WorldUniforms.CameraOriginTanX.xyz;
        float3 VDir      = normalize(CamOrigin - CenterPos.xyz);
        float3 SurfN     = normalize(CenterNorm.xyz);
        float  NdotV     = max(1.0e-3, dot(SurfN, VDir));
        float4 SpecPack  = AccumulationHdrBuffer[PixelIdx];
        float  SpecRough = clamp(SpecPack.w, 0.025, 1.0);
        float  SpecAo    = saturate(pow(NdotV + OutDiffAo, exp2(-16.0 * SpecRough - 1.0)) - 1.0 + OutDiffAo);
        float3 SkyFill   = EvaluateSkyDome(SurfN);
        FinalHdr = DirectBase.xyz
                 + SpecPack.xyz * SpecAo
                 + SunContrib.xyz * OutSunVis
                 + AlbedoPack.xyz * SkyFill * 0.22 * (0.35 + 0.65 * OutDiffAo);
    }
    else if (CenterPos.w > 0.0 && DebugMode == 8u)
    {
        FinalHdr = float3(OutSunVis);
    }
    else if (CenterPos.w > 0.0 && DebugMode == 9u)
    {
        FinalHdr = float3(OutDiffAo);
    }
    else if (CenterPos.w > 0.0 && DebugMode == 10u)
    {
        FinalHdr = ApplyAcesFilmicGpu(DirectBase.xyz + SunContrib.xyz * OutSunVis);
    }
    else if (CenterPos.w > 0.0 && DebugMode == 11u)
    {
        float3 CamOrigin = WorldUniforms.CameraOriginTanX.xyz;
        float3 VDir      = normalize(CamOrigin - CenterPos.xyz);
        float  NdotV     = max(1.0e-3, dot(normalize(CenterNorm.xyz), VDir));
        float4 SpecPack  = AccumulationHdrBuffer[PixelIdx];
        float  SpecRough = clamp(SpecPack.w, 0.025, 1.0);
        float  SpecAo    = saturate(pow(NdotV + OutDiffAo, exp2(-16.0 * SpecRough - 1.0)) - 1.0 + OutDiffAo);
        FinalHdr = ApplyAcesFilmicGpu(SpecPack.xyz * SpecAo);
    }

    if (DebugMode == 0u)
    {
        float2 PixelUv   = (float2(DispatchThreadID.xy) + float2(0.5, 0.5)) / float2(float(Width), float(Height));
        float  NdcX      = PixelUv.x * 2.0 - 1.0;
        float  NdcY      = 1.0 - PixelUv.y * 2.0;
        float3 CamOrigin = WorldUniforms.CameraOriginTanX.xyz;
        float3 RayDir    = normalize(
            WorldUniforms.CameraForwardHeight.xyz +
            WorldUniforms.CameraRightTanY.xyz * (NdcX * WorldUniforms.CameraOriginTanX.w) +
            WorldUniforms.CameraUpWidth.xyz   * (NdcY * WorldUniforms.CameraRightTanY.w));

        float HitDistForClouds = DirectBase.w;
        float HitDistForFog    = SunContrib.w;

        float  LocalCloudTrans = 1.0;
        float3 LocalCloudRad   = EvaluateLocalCloudsSegmentGpu(
            WorldUniforms.CloudParams, CamOrigin, RayDir, HitDistForClouds, float2(DispatchThreadID.xy), LocalCloudTrans);
        FinalHdr = FinalHdr * LocalCloudTrans + LocalCloudRad;

        float  FogTrans = 1.0;
        float3 FogRad   = EvaluateVolumetricFogGpu(
            WorldUniforms.FogParams,
            CamOrigin,
            RayDir,
            HitDistForFog,
            normalize(WorldUniforms.SunDirConeTan.xyz),
            WorldUniforms.SolarIrradiancePad.xyz,
            EvaluateSkyDome(RayDir),
            float2(DispatchThreadID.xy),
            FogTrans);
        FinalHdr = FinalHdr * FogTrans + FogRad;
        FinalHdr += EvaluateVolumetricGodRaysGpu(
            CamOrigin,
            RayDir,
            HitDistForFog,
            normalize(WorldUniforms.SunDirConeTan.xyz),
            WorldUniforms.SolarIrradiancePad.xyz,
            float2(DispatchThreadID.xy));

        if (WorldUniforms.AtmosphereMaskPad.x != 0u && WorldUniforms.LensFlareParams.GhostRotationTint.w > 0.5)
        {
            FinalHdr += EvaluateBakedLensFlareGpu(WorldUniforms.LensFlareParams, BakedLensFlareAtlasTexels, PixelUv);
        }
    }

    float3 DisplayLinear = (DebugMode != 0u) ? FinalHdr : ApplyAcesFilmicGpu(FinalHdr);
    OutDisplayRgba8[PixelIdx] = PackLinearToSrgb8Rgba(DisplayLinear, DispatchThreadID.xy);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ShadowCascadeComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint CascadeIdx = DispatchThreadID.z;
    if (CascadeIdx > 1u) return;

    uint   Res         = (CascadeIdx == 0u) ? uint(WorldUniforms.FineCascadeParams.x + 0.5) : uint(WorldUniforms.SunCascadeRightRes.w + 0.5);
    float  HalfSpan    = (CascadeIdx == 0u) ? WorldUniforms.FineCascadeCenterSpan.w : WorldUniforms.SunCascadeCenterSpan.w;
    float  DepthExtent = (CascadeIdx == 0u) ? WorldUniforms.FineCascadeParams.y : WorldUniforms.SunCascadeUpDepth.w;
    float3 Center      = (CascadeIdx == 0u) ? WorldUniforms.FineCascadeCenterSpan.xyz : WorldUniforms.SunCascadeCenterSpan.xyz;
    uint   OutOffset   = (CascadeIdx == 0u) ? 0u : uint(WorldUniforms.FineCascadeParams.z + 0.5);

    if (Res < 4u || DispatchThreadID.x >= Res || DispatchThreadID.y >= Res) return;

    float3 LightRight = WorldUniforms.SunCascadeRightRes.xyz;
    float3 LightUp    = WorldUniforms.SunCascadeUpDepth.xyz;
    float3 LightFwd   = WorldUniforms.SunCascadeForwardPad.xyz;

    float LocalU = (((float(DispatchThreadID.x) + 0.5) / float(Res)) * 2.0 - 1.0) * HalfSpan;
    float LocalV = (1.0 - ((float(DispatchThreadID.y) + 0.5) / float(Res)) * 2.0) * HalfSpan;
    float3 RayOrig = Center + LightRight * LocalU + LightUp * LocalV - LightFwd * DepthExtent;

    RayHitReading Hit = TraceSceneClosest(RayOrig, LightFwd, 0.0, DepthExtent * 2.0 + 4.0);
    SolarShadowDepthBuffer[OutOffset + DispatchThreadID.y * Res + DispatchThreadID.x] = Hit.Hit ? Hit.Distance : 1.0e6;
}
